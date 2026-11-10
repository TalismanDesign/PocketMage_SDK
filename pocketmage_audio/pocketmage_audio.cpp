#include "pocketmage_audio.h"

#include <pocketmage.h>
#include <pocketmage_globals.h>
#include <pocketmage_kb/usb_keyboard.h>
#include <pocketmage_media/media_reader.h>
#include <pocketmage_sys/usb_role.h>

#include <driver/ledc.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

#include <cstdio>
#include <climits>
#include <cstdlib>
#include <cstring>

#include "piezo_filter.h"

extern "C" {
#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_audio_types.h"
#include "pwm_audio.h"
}

static const char *TAG = "PocketMageAudio";
static constexpr uint32_t kShutdownTimeoutMs = 5000;
static constexpr uint32_t kCommandWaitMs = 200;
// An attached accessory gets one bounded enumeration window per connection.
static constexpr uint32_t kUsbProbeTimeoutMs = 5000;

// pwm_audio owns these LEDC resources while it holds BZ_PIN. The buzzer's
// Arduino tone() path auto-allocates its own channel, which we detach on
// acquire, so there is no static collision to coordinate.
static constexpr ledc_channel_t AUDIO_LEDC_CHANNEL = LEDC_CHANNEL_0;
static constexpr ledc_timer_t AUDIO_LEDC_TIMER = LEDC_TIMER_0;
static constexpr ledc_timer_bit_t AUDIO_DUTY_RES = LEDC_TIMER_8_BIT;

// Base decoders register once per boot; the register-default calls are
// idempotent-guarded by this flag since there is no host unregister path.
static bool s_decodersRegistered = false;

// USB audio device state callback (static: passed to usb_streaming_state_register).
// Runs in the usb_stream task context; publishes presence for decoder/UI polling.
void PocketMageAudio::usbStateCb(usb_stream_state_t state, void *user_ptr) {
  auto *audio = static_cast<PocketMageAudio *>(user_ptr);
  size_t frames = 0;
  bool connected = state == STREAM_CONNECTED &&
                   uac_frame_size_list_get(STREAM_UAC_SPK, nullptr, &frames, nullptr) == ESP_OK &&
                   frames > 0;
  audio->_usbConnected = connected;
  ++audio->_usbConnectionId;
  if (!connected)
    audio->_usbStarted = false;
  audio->publishEvent();
}

PocketMageAudio &PocketMageAudio::getInstance() {
  static PocketMageAudio instance;
  return instance;
}

PocketMageAudio &P_AUDIO = PocketMageAudio::getInstance();

PocketMageAudio::PocketMageAudio()
    : _mutex(xSemaphoreCreateRecursiveMutex()), _state(AudioState::Idle), _volume(DEFAULT_VOLUME),
      _bytesDecoded(0),
      _taskHandle(nullptr), _commandQueue(nullptr), _shutdownSem(xSemaphoreCreateBinary()),
      _eventCallback(nullptr), _inBuf(nullptr), _outBuf(nullptr), _outCap(0), _leftoverLen(0),
      _initialized(false), _pinOwned(false), _eventPending(false),
      _stopRequested(false), _paused(false), _output(AudioOutput::Auto), _usbConnected(false),
      _usbConnectionId(0), _usbConfigured(false), _usbStarted(false) {
  if (!_mutex || !_shutdownSem) {
    ESP_LOGE(TAG, "Cannot create audio synchronization primitives");
    std::abort();
  }
  _currentPath[0] = 0;
  _pendingPath[0] = 0;
  _lastError[0] = 0;
}

PocketMageAudio::~PocketMageAudio() {
  stop();
  if (_taskHandle || _usbConfigured) {
    ESP_LOGE(TAG, "Cannot destroy audio service while workers remain active");
    std::abort();
  }
  if (_mutex)
    vSemaphoreDelete(_mutex);
  if (_shutdownSem)
    vSemaphoreDelete(_shutdownSem);
}

void PocketMageAudio::begin() {
#if PM_TARGET_HOST
  if (CurrentAppState == USB_APP)
    return;
#endif
  if (_initialized || _shutdownRequested)
    return;
  if (!_settingsLoaded && !loadSettings())
    ESP_LOGE(TAG, "Audio settings load failed; retaining current settings");
  _commandQueue = xQueueCreate(4, sizeof(Command));
  // Pre-allocate decode buffers once at boot while the internal heap is
  // still unfragmented
  _inBuf = (uint8_t *)malloc(READ_CHUNK);
  _outBuf = (uint8_t *)malloc(PCM_OUT_CHUNK);
  _outCap = PCM_OUT_CHUNK;
  _leftoverLen = 0;
  if (!_commandQueue || !_inBuf || !_outBuf) {
    setError("audio initialization failed");
    stop();
    return;
  }
  _initialized = true;
  pollUsb();
  ESP_LOGI(TAG, "begin(): pmaudio task will be created lazily on first play()");
}

void PocketMageAudio::stop() {
  if (!stopChecked())
    ESP_LOGE(TAG, "Audio stop incomplete: %s", getLastError().c_str());
}

bool PocketMageAudio::stopChecked() {
  xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
  _shutdownRequested = true;
  _playback.cancel();
  _stopRequested = true;
  xSemaphoreGiveRecursive(_mutex);

  if (_taskHandle) {
    // The worker checks shutdown independently of the command queue, including
    // when it is full. A timeout must never free memory still used by decoding.
    if (xSemaphoreTake(_shutdownSem, pdMS_TO_TICKS(kShutdownTimeoutMs)) != pdTRUE) {
      setError("audio shutdown timed out; retry stop");
      return false;
    }
    _taskHandle = nullptr;
  }
  if (!stopUsb())
    return false;
  if (_commandQueue) {
    vQueueDelete(_commandQueue);
    _commandQueue = nullptr;
  }
  free(_inBuf);
  free(_outBuf);
  _inBuf = nullptr;
  _outBuf = nullptr;
  _outCap = 0;
  _leftoverLen = 0;
  _initialized = false;
  _usbProbeSuppressed = false;
  _shutdownRequested = false;
  return true;
}

void PocketMageAudio::play(const char *path) {
  const bool validPath = path && path[0] && strlen(path) < sizeof(_pendingPath);
  // begin() may acquire the USB lock or clean up a failed initialization. Finish
  // it before taking the audio mutex and publishing the new request.
  if (validPath && !_initialized)
    begin();
  xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
  _clock.request(_playback.request());
  // Even a rejected request supersedes the previous clip and its queued work.
  _stopRequested = true;
  const char *error = nullptr;
  if (!validPath) {
    error = "invalid audio path";
  } else if (_shutdownRequested) {
    error = "audio shutdown pending; retry stop";
  } else if (!_initialized) {
    error = "audio initialization unavailable or failed";
  } else if (!_taskHandle &&
             xTaskCreatePinnedToCore(audioTaskFunc, "pmaudio", TASK_STACK_BYTES, this, TASK_PRIORITY,
                                     &_taskHandle, TASK_CORE) != pdPASS) {
    error = "audio task creation failed";
  } else {
    strncpy(_pendingPath, path, sizeof(_pendingPath));
    _pendingPath[sizeof(_pendingPath) - 1] = 0;
    Command cmd = Command::Play;
    if (xQueueSend(_commandQueue, &cmd, 0) != pdTRUE)
      error = "audio command queue full";
  }
  if (error) {
    _playback.fail();
    setError(error);
  }
  xSemaphoreGiveRecursive(_mutex);
}

void PocketMageAudio::stopPlayback() {
  xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
  _playback.cancel();
  _stopRequested = true;
  Command cmd = Command::Stop;
  if (_commandQueue && xQueueSend(_commandQueue, &cmd, 0) != pdTRUE)
    setError("audio stop queue full");
  xSemaphoreGiveRecursive(_mutex);
}

void PocketMageAudio::pause() {
  _paused = true;
}

void PocketMageAudio::resume() {
  _paused = false;
}

void PocketMageAudio::setVolume(int8_t vol) {
  if (vol < -16)
    vol = -16;
  if (vol > 16)
    vol = 16;
  _volume = vol;
}

AudioState PocketMageAudio::getState() const {
  return _state;
}

bool PocketMageAudio::isPlaying() const {
  return _state == AudioState::Playing;
}

bool PocketMageAudio::ownsPin() const {
  return _pinOwned;
}

String PocketMageAudio::getCurrentPath() const {
  xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
  String p = String(_currentPath);
  xSemaphoreGiveRecursive(_mutex);
  return p;
}

String PocketMageAudio::getLastError() const {
  xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
  String e = String(_lastError);
  xSemaphoreGiveRecursive(_mutex);
  return e;
}

int8_t PocketMageAudio::getVolume() const {
  return _volume;
}

uint32_t PocketMageAudio::getPosition() const {
  xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
  uint32_t pos = _bytesDecoded;
  xSemaphoreGiveRecursive(_mutex);
  return pos;
}

uint64_t PocketMageAudio::getPositionMs() const {
  xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
  uint64_t position = _clock.milliseconds();
  xSemaphoreGiveRecursive(_mutex);
  return position;
}

uint32_t PocketMageAudio::getPlaybackId() const {
  xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
  uint32_t id = _playback.id();
  xSemaphoreGiveRecursive(_mutex);
  return id;
}

int PocketMageAudio::getPlaybackResult(uint32_t id) const {
  xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
  int result = _playback.result(id);
  xSemaphoreGiveRecursive(_mutex);
  return result;
}

void PocketMageAudio::setOutput(AudioOutput output) {
  if (output != AudioOutput::Piezo && output != AudioOutput::Usb && output != AudioOutput::Auto)
    return;
  xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
  _output = output;
  _usbProbeSuppressed = false;
  xSemaphoreGiveRecursive(_mutex);
  publishEvent();
}

AudioOutput PocketMageAudio::getOutput() const {
  xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
  AudioOutput o = _output;
  xSemaphoreGiveRecursive(_mutex);
  return o;
}

AudioOutput PocketMageAudio::getActiveOutput() const {
  return _usbConnected && _usbStarted ? AudioOutput::Usb : AudioOutput::Piezo;
}

bool PocketMageAudio::usbConnected() const {
  return _usbConnected;
}

void PocketMageAudio::setEventCallback(AudioEventCallback cb) {
  _eventCallback = cb;
}

void PocketMageAudio::audioTaskFunc(void *param) {
  static_cast<PocketMageAudio *>(param)->taskLoop();
}

void PocketMageAudio::taskLoop() {
  Command cmd = Command::None;
  while (!_shutdownRequested) {
    if (xQueueReceive(_commandQueue, &cmd, pdMS_TO_TICKS(kCommandWaitMs)) == pdTRUE) {
      if (_shutdownRequested)
        break;
      switch (cmd) {
      case Command::Play:
        doPlay();
        break;
      case Command::Stop:
        doStop();
        break;
      default:
        break;
      }
    }
    vTaskDelay(10);
  }
  doStop();
  // After acknowledgement the control task may release every service resource.
  // https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/freertos_idf.html
  if (xSemaphoreGive(_shutdownSem) != pdTRUE)
    ESP_LOGE(TAG, "Audio shutdown acknowledgement failed");
  vTaskDelete(nullptr);
}

// Map a file path to a simple-decoder type. Containers (WAV/M4A/TS/OGG) and
// raw codecs (MP3/AAC/FLAC) all route through the simple decoder.
static esp_audio_simple_dec_type_t typeFromPath(const char *path) {
  const char *dot = strrchr(path, '.');
  if (!dot)
    return ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
  char ext[8];
  size_t i = 0;
  for (const char *p = dot + 1; *p && i < sizeof(ext) - 1; ++p)
    ext[i++] = (char)tolower((unsigned char)*p);
  ext[i] = 0;
  if (!strcmp(ext, "mp3"))
    return ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
  if (!strcmp(ext, "wav"))
    return ESP_AUDIO_SIMPLE_DEC_TYPE_WAV;
  if (!strcmp(ext, "aac"))
    return ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
  if (!strcmp(ext, "flac"))
    return ESP_AUDIO_SIMPLE_DEC_TYPE_FLAC;
  if (!strcmp(ext, "m4a"))
    return ESP_AUDIO_SIMPLE_DEC_TYPE_M4A;
  if (!strcmp(ext, "ts"))
    return ESP_AUDIO_SIMPLE_DEC_TYPE_TS;
  if (!strcmp(ext, "ogg"))
    return ESP_AUDIO_SIMPLE_DEC_TYPE_OGG;
  return ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
}

bool PocketMageAudio::acquirePin(int sampleRate, int bits, int channels) {
  if (_pinOwned)
    return true;
  pwm_audio_config_t cfg = {};
  cfg.gpio_num_left = BZ_PIN;
  cfg.gpio_num_right = -1;
  cfg.ledc_channel_left = AUDIO_LEDC_CHANNEL;
  cfg.ledc_channel_right = AUDIO_LEDC_CHANNEL;
  cfg.ledc_timer_sel = AUDIO_LEDC_TIMER;
  cfg.duty_resolution = AUDIO_DUTY_RES;
  cfg.ringbuf_len = SINK_RINGBUF_LEN;

  pocketmage::setCpuSpeed(240);
  ESP_LOGI(TAG, "cpu=%lu MHz apb=%lu Hz init pwm_audio", (unsigned long)getCpuFrequencyMhz(),
           (unsigned long)getApbFrequency());
  esp_err_t err = pwm_audio_init(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "acquirePin: pwm_audio_init -> %s", esp_err_to_name(err));
    setError("audio sink init failed");
    return false;
  }
  err = pwm_audio_set_param(sampleRate, (ledc_timer_bit_t)DEFAULT_BITS, channels);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "acquirePin: set_param -> %s", esp_err_to_name(err));
    pwm_audio_deinit();
    setError("audio sink param failed");
    return false;
  }
  // Gain is applied to PCM once for both transports; PWM stays at unity.
  if (pwm_audio_set_volume(0) != ESP_OK || pwm_audio_start() != ESP_OK) {
    pwm_audio_deinit();
    setError("audio sink start failed");
    return false;
  }
  _pinOwned = true;
  return true;
}

void PocketMageAudio::releasePin() {
  if (!_pinOwned)
    return;
  pwm_audio_stop();
  pwm_audio_deinit();
  _pinOwned = false;
  // Hand the pin back so queued jingles can sound.
  BZ().drainPending();
}

bool PocketMageAudio::beginUsb() {
  pm_usb::Lock usbLock;
  if (_usbConfigured)
    return true;
  if (!pm_usb::claim(pm_usb::Owner::Audio)) {
    setError("USB controller busy or unavailable");
    return false;
  }
  // Enumeration requires a running host even before a speaker is connected.
  // Keep the speaker suspended until its PCM format is known.
  // https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_host/usb_stream.html
  uac_config_t cfg = {};
  cfg.spk_ch_num = UAC_CH_ANY;
  cfg.spk_bit_resolution = DEFAULT_BITS;
  cfg.spk_samples_frequence = UAC_FREQUENCY_ANY;
  cfg.spk_buf_size = SINK_RINGBUF_LEN;
  cfg.mic_buf_size = 0;
  cfg.mic_cb = nullptr;
  cfg.mic_cb_arg = nullptr;
  // Optional descriptor fields left at 0: driver parses them from the device.
  cfg.spk_interface = 0;
  cfg.spk_ep_addr = 0;
  cfg.spk_ep_mps = 0;
  cfg.ac_interface = 0;
  cfg.spk_fu_id = 0;
  cfg.flags = FLAG_UAC_SPK_SUSPEND_AFTER_START;

  esp_err_t err = uac_streaming_config(&cfg);
  if (err == ESP_OK)
    err = usb_streaming_state_register(usbStateCb, this);
  if (err == ESP_OK)
    err = usb_streaming_start();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "acquireUsb: usb_streaming_start -> %s", esp_err_to_name(err));
    if (pm_usb::release(pm_usb::Owner::Audio))
      setError("usb sink start failed");
    else
      setError("USB serial restore failed; retry stop");
    return false;
  }
  _usbConfigured = true;
  _usbProbeStartedAt = millis();
  return true;
}

bool PocketMageAudio::stopUsb() {
  // Sink calls share this lock, so teardown cannot free an in-flight USB write.
  pm_usb::Lock usbLock;
  if (_usbConfigured) {
    const esp_err_t error = usb_streaming_stop();
    if (error != ESP_OK) {
      setError("USB host shutdown failed; retry stop");
      return false;
    }
    _usbConfigured = false;
    _usbConnected = false;
    _usbStarted = false;
    publishEvent();
  }
  if (pm_usb::owner() == pm_usb::Owner::Audio && !pm_usb::release(pm_usb::Owner::Audio)) {
    setError("USB serial restore failed; retry stop");
    return false;
  }
  return true;
}

bool PocketMageAudio::pollUsb() {
  pm_usb::Lock usbLock;
  if (!_initialized && pm_usb::owner() != pm_usb::Owner::Audio)
    return false;
  if (_shutdownRequested)
    return true;
  if (_output == AudioOutput::Piezo) {
    if (!stopUsb())
      ESP_LOGE(TAG, "USB audio release incomplete; will retry on the next poll");
    return pm_usb::owner() == pm_usb::Owner::Audio;
  }
  bool attached = false;
  if (!PowerSystem.getOTGNeed(attached)) {
    setError("USB attachment check failed");
    return true;
  }
  if (!attached)
    _usbProbeSuppressed = false;
  if (_usbConfigured && !_usbConnected &&
      static_cast<uint32_t>(millis() - _usbProbeStartedAt) >= kUsbProbeTimeoutMs)
    _usbProbeSuppressed = true;
  if (!attached || _usbProbeSuppressed) {
    if (!stopUsb())
      ESP_LOGE(TAG, "USB audio release incomplete; will retry on the next poll");
    return pm_usb::owner() == pm_usb::Owner::Audio;
  }
  if (!_usbConfigured) {
    if (pm_usb::owner() == pm_usb::Owner::Keyboard && !pm_keyboard::stopUsb()) {
      setError("USB keyboard shutdown pending; retrying audio");
      return true;
    }
    if (pm_usb::owner() == pm_usb::Owner::Audio && !stopUsb())
      return true;
    if (!beginUsb()) {
      _usbProbeSuppressed = true;
      ESP_LOGE(TAG, "USB audio unavailable; retaining piezo output");
      return pm_usb::owner() == pm_usb::Owner::Audio;
    }
  }
  return true;
}

esp_err_t PocketMageAudio::writeUsb(void *data, size_t size, size_t timeoutMs) {
  pm_usb::Lock usbLock;
  if (!_usbConfigured || !_usbStarted || !_usbConnected || _output == AudioOutput::Piezo)
    return ESP_ERR_INVALID_STATE;
  return uac_spk_streaming_write(data, size, timeoutMs);
}

bool PocketMageAudio::acquireUsb(int sampleRate, int bits, int channels) {
  pm_usb::Lock usbLock;
  if (!_usbConfigured || !_usbConnected || _output == AudioOutput::Piezo)
    return false;
  esp_err_t err = uac_frame_size_reset(STREAM_UAC_SPK, channels, bits, sampleRate);
  if (err == ESP_OK)
    err = usb_streaming_control(STREAM_UAC_SPK, CTRL_RESUME, nullptr);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "USB format/resume failed: %s", esp_err_to_name(err));
    return false;
  }
  _usbStarted = true;
  return true;
}

void PocketMageAudio::releaseUsb() {
  pm_usb::Lock usbLock;
  if (!_usbStarted)
    return;
  esp_err_t err = usb_streaming_control(STREAM_UAC_SPK, CTRL_SUSPEND, nullptr);
  if (err != ESP_OK && _usbConnected)
    ESP_LOGE(TAG, "USB suspend failed: %s", esp_err_to_name(err));
  _usbStarted = false;
}

void PocketMageAudio::doPlay() {
  // Drain any clip in flight before starting the new one.
  if (_pinOwned || _usbStarted)
    doStop();

  xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
  if (_shutdownRequested || _playback.result(_playback.id()) != PlaybackSession::Pending) {
    xSemaphoreGiveRecursive(_mutex);
    return;
  }
  _playback.start();
  const uint32_t playbackId = _playback.id();
  strncpy(_currentPath, _pendingPath, sizeof(_currentPath));
  _currentPath[sizeof(_currentPath) - 1] = 0;
  _stopRequested = false;
  xSemaphoreGiveRecursive(_mutex);

  if (!s_decodersRegistered) {
    esp_audio_err_t derr = esp_audio_dec_register_default();
    esp_audio_err_t serr = esp_audio_simple_dec_register_default();
    if (derr != ESP_AUDIO_ERR_OK || serr != ESP_AUDIO_ERR_OK) {
      ESP_LOGE(TAG, "doPlay: register decoders dec=%d simple=%d", (int)derr, (int)serr);
      setError("decoder register failed");
      xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
      _playback.finish(false);
      xSemaphoreGiveRecursive(_mutex);
      _state = AudioState::Idle;
      publishEvent();
      return;
    }
    s_decodersRegistered = true;
  }

  _paused = false;
  _lastError[0] = 0;
  _bytesDecoded = 0;
  _state = AudioState::Opening;
  publishEvent();

  decodeLoop(playbackId);
  doStop();
  xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
  // decodeLoop marks natural EOF itself. An early return is a failed request.
  _playback.finish(false);
  xSemaphoreGiveRecursive(_mutex);
}

void PocketMageAudio::decodeLoop(uint32_t playbackId) {
  if (!PM_SD().exists(_currentPath)) {
    setError("no filesystem");
    return;
  }
  esp_audio_simple_dec_type_t type = typeFromPath(_currentPath);
  if (type == ESP_AUDIO_SIMPLE_DEC_TYPE_NONE) {
    setError("unsupported file type");
    return;
  }

  int slot = PM_SD().streamOpen(_currentPath, 'r');
  if (slot < 0) {
    setError("open failed");
    return;
  }

  esp_audio_simple_dec_cfg_t dcfg = {};
  dcfg.dec_type = type;
  dcfg.dec_cfg = nullptr;
  dcfg.cfg_size = 0;
  dcfg.use_frame_dec = false;

  esp_audio_simple_dec_handle_t dec = nullptr;
  ESP_LOGI(TAG, "heap before dec_open: free=%u largest=%u",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (esp_audio_simple_dec_open(&dcfg, &dec) != ESP_AUDIO_ERR_OK || dec == nullptr) {
    setError("decoder open failed");
    PM_SD().streamClose(slot);
    return;
  }

  if (!_inBuf || !_outBuf) {
    setError("out of memory");
    esp_audio_simple_dec_close(dec);
    PM_SD().streamClose(slot);
    return;
  }

  uint32_t outCap = _outCap;
  bool sinkReady = false;
  bool ok = true;
  int srcCh = DEFAULT_CHANNELS; // source channel count for piezo downmix
  int sampleRate = DEFAULT_SAMPLE_RATE;
  uint32_t usbConnection = _usbConnectionId;
  bool usbRejected = false;
  static constexpr size_t kUsbWriteTimeoutMs = 100;

  // MP3 decoding needs 240 MHz (Ashtf's rule)
  pocketmage::setCpuSpeed(240);

  // Fresh track: no leftover bytes from a previous file.
  _leftoverLen = 0;

  uint32_t audioOffset = 0;
  if (!pm_media::skipId3(slot, audioOffset)) {
    setError("audio tag or seek failed");
    ok = false;
  } else {
    _bytesDecoded = audioOffset;
  }

  bool ended = false;
  while (ok && !_stopRequested) {
    // Pause: stop the sink (silences output, keeps the pin) and block without
    // touching the decoder so stream position is preserved.
    if (_paused) {
      if (sinkReady && _pinOwned)
        pwm_audio_stop();
      releaseUsb();
      if (_state == AudioState::Playing) {
        _state = AudioState::Paused;
        publishEvent();
      }
      while (_paused && !_stopRequested)
        vTaskDelay(pdMS_TO_TICKS(50));
      if (!_stopRequested && sinkReady) {
        if (_pinOwned)
          pwm_audio_start();
        _state = AudioState::Playing;
        publishEvent();
      }
    }
    if (_stopRequested)
      break;

    int readLen = (int)PM_SD().streamRead(slot, _inBuf + _leftoverLen, READ_CHUNK - _leftoverLen);
    bool eos = (readLen <= 0);
    uint32_t avail = (readLen > 0) ? (uint32_t)readLen + _leftoverLen : _leftoverLen;
    uint32_t offset = 0;

    // Feed the whole read chunk; the parser consumes it piecewise.
    do {
      esp_audio_simple_dec_raw_t raw = {};
      raw.buffer = _inBuf + offset;
      raw.len = avail - offset;
      raw.eos = eos;

      esp_audio_simple_dec_out_t out = {};
      out.buffer = _outBuf;
      out.len = outCap;

      esp_audio_err_t derr = esp_audio_simple_dec_process(dec, &raw, &out);
      if (derr == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
        uint32_t need = out.needed_size ? out.needed_size : (outCap * 2);
        if (need > _outCap) {
          ESP_LOGE(TAG, "decode: output buffer too small (%u < %u)", _outCap, need);
          ESP_LOGE(
              TAG, "heap at error: free=%u largest=%u",
              (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
          setError("out of memory");
          ok = false;
          break;
        }
        ESP_LOGE(TAG, "decode: unexpected BUFF_NOT_ENOUGH need=%u cap=%u", need, _outCap);
        setError("decode error");
        ok = false;
        break;
      }
      if (derr != ESP_AUDIO_ERR_OK) {
        const char *derrs = "unknown";
        if (derr == ESP_AUDIO_ERR_MEM_LACK)
          derrs = "MEM_LACK";
        else if (derr == ESP_AUDIO_ERR_DATA_LACK)
          derrs = "DATA_LACK";
        ESP_LOGE(TAG, "decode process -> %s (%d)", derrs, (int)derr);
        ESP_LOGE(TAG, "heap at error: free=%u largest=%u",
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        setError(derr == ESP_AUDIO_ERR_MEM_LACK ? "out of memory" : "decode error");
        ok = false;
        break;
      }

      if (out.decoded_size > 0) {
        if (!sinkReady) {
          esp_audio_simple_dec_info_t info = {};
          if (esp_audio_simple_dec_get_info(dec, &info) != ESP_AUDIO_ERR_OK ||
              !info.sample_rate || info.sample_rate > INT_MAX || info.bits_per_sample != 16 ||
              (info.channel != 1 && info.channel != 2)) {
            setError("unsupported PCM format");
            ok = false;
            break;
          }
          sampleRate = static_cast<int>(info.sample_rate);
          srcCh = info.channel;
          sinkReady = true;
          _state = AudioState::Playing;
          publishEvent();
        }

        // Sink changes happen on the decoder task before writing any PCM.
        // A rejected format is retried on the next device connection or track.
        if (usbConnection != _usbConnectionId) {
          usbConnection = _usbConnectionId;
          usbRejected = false;
        }
        bool wantUsb = _output != AudioOutput::Piezo && _usbConnected && !usbRejected;
        if (wantUsb && !_usbStarted) {
          releasePin();
          if (!acquireUsb(sampleRate, DEFAULT_BITS, srcCh)) {
            usbRejected = true;
            wantUsb = false;
          }
        }
        if (!wantUsb) {
          releaseUsb();
          if (!_pinOwned) {
            hpfInit(s_hpf, (float)sampleRate, PIEZO_HPF_HZ);
            if (!acquirePin(sampleRate, DEFAULT_BITS, 1)) {
              ok = false;
              break;
            }
          }
        }
        size_t written = 0;
        if (!pm_audio::scalePcm16(out.buffer, out.decoded_size, _volume)) {
          setError("invalid PCM frame");
          ok = false;
          break;
        }
        bool accepted = false;
        if (wantUsb) {
          esp_err_t err;
          do {
            err = writeUsb(out.buffer, out.decoded_size, kUsbWriteTimeoutMs);
          } while (err == ESP_ERR_TIMEOUT && !_stopRequested && !_paused &&
                    _usbConnected && _output != AudioOutput::Piezo);
          if (err != ESP_OK && _usbConnected && !_stopRequested && !_paused &&
              _output != AudioOutput::Piezo) {
            ESP_LOGE(TAG, "usb write -> %s", esp_err_to_name(err));
            setError("usb sink write failed");
            ok = false;
            break;
          }
          accepted = err == ESP_OK;
        } else {
          // Piezo: downmix to mono, high-pass, leave headroom.
          size_t len = shapeForPiezo((int16_t *)out.buffer, out.decoded_size, srcCh);
          if (pwm_audio_write(out.buffer, len, &written, portMAX_DELAY) != ESP_OK ||
              written != len) {
            setError("piezo sink write failed");
            ok = false;
            break;
          }
          accepted = true;
        }
        if (accepted) {
          xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
          bool advanced = _clock.advance(playbackId, out.decoded_size, srcCh, sampleRate);
          xSemaphoreGiveRecursive(_mutex);
          if (!advanced) {
            setError("invalid PCM timing");
            ok = false;
            break;
          }
        }
      }

      offset += raw.consumed;
      _bytesDecoded += raw.consumed;
      if (raw.consumed == 0)
        break;
    } while (offset < avail && !_stopRequested && !_paused);

    // Inner loop exited: save any unconsumed bytes (pause/stop exit paths too).
    if (offset < avail) {
      _leftoverLen = avail - offset;
      memmove(_inBuf, _inBuf + offset, _leftoverLen);
    } else {
      _leftoverLen = 0;
    }
    // A full buffer with zero consumption means the parser is stalled.
    if (_leftoverLen >= READ_CHUNK) {
      setError("parser stalled");
      break;
    }

    if (!ok || eos) {
      ended = ok && eos && !_stopRequested;
      break;
    }
  }

  if (SAVE_POWER)
    pocketmage::setCpuSpeed(POWER_SAVE_FREQ);

  esp_audio_simple_dec_close(dec);
  PM_SD().streamClose(slot);
  xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
  _playback.finish(ended);
  xSemaphoreGiveRecursive(_mutex);
}

void PocketMageAudio::doStop() {
  _stopRequested = false;
  _paused = false;
  if (_pinOwned)
    releasePin();
  if (_usbStarted)
    releaseUsb();
  if (_state != AudioState::Idle) {
    _state = AudioState::Idle;
    _currentPath[0] = 0;
    publishEvent();
  }
}

void PocketMageAudio::setError(const char *msg) {
  xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
  strncpy(_lastError, msg, sizeof(_lastError));
  _lastError[sizeof(_lastError) - 1] = 0;
  xSemaphoreGiveRecursive(_mutex);
  publishEvent();
}

void PocketMageAudio::publishEvent() {
  _eventPending = true;
}

void PocketMageAudio::dispatchEvents() {
  if (_eventPending.exchange(false)) {
    if (_eventCallback)
      _eventCallback();
  }
}
