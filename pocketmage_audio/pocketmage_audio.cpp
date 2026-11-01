#include "pocketmage_audio.h"

#include <pocketmage.h>
#include <pocketmage_globals.h>

#include <driver/ledc.h>
#include <esp_log.h>

#include <cstring>

extern "C" {
#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_audio_types.h"
#include "pwm_audio.h"
}
#include <usb_stream.h>

static const char* TAG = "PocketMageAudio";

// pwm_audio owns these LEDC resources while it holds BZ_PIN. The buzzer's
// Arduino tone() path auto-allocates its own channel, which we detach on
// acquire, so there is no static collision to coordinate.
static constexpr ledc_channel_t AUDIO_LEDC_CHANNEL = LEDC_CHANNEL_0;
static constexpr ledc_timer_t AUDIO_LEDC_TIMER = LEDC_TIMER_0;
static constexpr ledc_timer_bit_t AUDIO_DUTY_RES = LEDC_TIMER_10_BIT;

// Base decoders register once per boot; the register-default calls are
// idempotent-guarded by this flag since there is no host unregister path.
static bool s_decodersRegistered = false;

// USB audio device state callback (static: passed to usb_streaming_state_register).
// Runs in the usb_stream task context; only sets a flag and wakes the audio
// task via publishEvent().
void PocketMageAudio::usbStateCb(usb_stream_state_t state, void* user_ptr) {
  auto* audio = static_cast<PocketMageAudio*>(user_ptr);
  audio->_usbConnected = (state == STREAM_CONNECTED);
  audio->publishEvent();
}

PocketMageAudio& PocketMageAudio::getInstance() {
  static PocketMageAudio instance;
  return instance;
}

PocketMageAudio& P_AUDIO = PocketMageAudio::getInstance();

PocketMageAudio::PocketMageAudio()
    : _mutex(xSemaphoreCreateRecursiveMutex()),
      _state(AudioState::Idle),
      _volume(DEFAULT_VOLUME),
      _taskHandle(nullptr),
      _commandQueue(nullptr),
      _shutdownSem(xSemaphoreCreateBinary()),
      _eventCallback(nullptr),
      _initialized(false),
      _pinOwned(false),
      _eventPending(false),
      _stopRequested(false),
      _paused(false),
      _output(AudioOutput::Piezo),
      _usbConnected(false),
      _usbConfigured(false),
      _usbStarted(false) {
  _currentPath[0] = 0;
  _pendingPath[0] = 0;
  _lastError[0] = 0;
}

PocketMageAudio::~PocketMageAudio() {
  stop();
  if (_mutex)
    vSemaphoreDelete(_mutex);
  if (_shutdownSem)
    vSemaphoreDelete(_shutdownSem);
}

void PocketMageAudio::begin() {
  if (_initialized)
    return;
  // Register USB audio device state callback before the USB host is started
  // so we see connect/disconnect events for UAC speaker selection.
  usb_streaming_state_register(usbStateCb, this);
  _commandQueue = xQueueCreate(4, sizeof(Command));
  xTaskCreatePinnedToCore(audioTaskFunc, "pmaudio", TASK_STACK_BYTES, this, TASK_PRIORITY,
                          &_taskHandle, TASK_CORE);
  _initialized = true;
  ESP_LOGI(TAG, "begin(): pmaudio task created");
}

void PocketMageAudio::stop() {
  if (_taskHandle) {
    Command cmd = Command::Shutdown;
    xQueueSend(_commandQueue, &cmd, portMAX_DELAY);
    xSemaphoreTake(_shutdownSem, pdMS_TO_TICKS(5000));
    _taskHandle = nullptr;
  }
  if (_commandQueue) {
    vQueueDelete(_commandQueue);
    _commandQueue = nullptr;
  }
  _initialized = false;
}

void PocketMageAudio::play(const char* path) {
  if (path == nullptr)
    return;
  xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
  strncpy(_pendingPath, path, sizeof(_pendingPath));
  _pendingPath[sizeof(_pendingPath) - 1] = 0;
  xSemaphoreGiveRecursive(_mutex);
  // A new clip supersedes whatever is playing; signal decode to abort first.
  _stopRequested = true;
  Command cmd = Command::Play;
  xQueueSend(_commandQueue, &cmd, 0);
}

void PocketMageAudio::stopPlayback() {
  _stopRequested = true;
  Command cmd = Command::Stop;
  xQueueSend(_commandQueue, &cmd, 0);
}

void PocketMageAudio::pause() {
  _paused = true;
  Command cmd = Command::Pause;
  xQueueSend(_commandQueue, &cmd, 0);
}

void PocketMageAudio::resume() {
  _paused = false;
  Command cmd = Command::Resume;
  xQueueSend(_commandQueue, &cmd, 0);
}

void PocketMageAudio::setVolume(int8_t vol) {
  if (vol < -16)
    vol = -16;
  if (vol > 16)
    vol = 16;
  _volume = vol;
  if (_pinOwned)
    pwm_audio_set_volume(vol);
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

void PocketMageAudio::setOutput(AudioOutput output) {
  xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
  _output = output;
  xSemaphoreGiveRecursive(_mutex);
  publishEvent();
}

AudioOutput PocketMageAudio::getOutput() const {
  xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
  AudioOutput o = _output;
  xSemaphoreGiveRecursive(_mutex);
  return o;
}

bool PocketMageAudio::usbConnected() const {
  return _usbConnected;
}

void PocketMageAudio::setEventCallback(AudioEventCallback cb) {
  _eventCallback = cb;
}

void PocketMageAudio::audioTaskFunc(void* param) {
  static_cast<PocketMageAudio*>(param)->taskLoop();
}

void PocketMageAudio::taskLoop() {
  Command cmd = Command::None;
  while (true) {
    if (xQueueReceive(_commandQueue, &cmd, pdMS_TO_TICKS(200)) == pdTRUE) {
      switch (cmd) {
        case Command::Play:
          doPlay();
          break;
        case Command::Stop:
          doStop();
          break;
        case Command::Pause:
        case Command::Resume:
          break;
        case Command::Shutdown:
          doStop();
          xSemaphoreGive(_shutdownSem);
          vTaskDelete(NULL);
          return;
        default:
          break;
      }
    }
    vTaskDelay(10);
  }
}

// Map a file path to a simple-decoder type. Containers (WAV/M4A/TS/OGG) and
// raw codecs (MP3/AAC/FLAC) all route through the simple decoder.
static esp_audio_simple_dec_type_t typeFromPath(const char* path) {
  const char* dot = strrchr(path, '.');
  if (!dot)
    return ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
  char ext[8];
  size_t i = 0;
  for (const char* p = dot + 1; *p && i < sizeof(ext) - 1; ++p)
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
  // Take the pin away from the buzzer: stop any tone and free its LEDC
  // channel so pwm_audio can claim the GPIO cleanly.
  noTone(BZ_PIN);
  ledcDetach(BZ_PIN);

  pwm_audio_config_t cfg = {};
  cfg.gpio_num_left = BZ_PIN;
  cfg.gpio_num_right = -1;
  cfg.ledc_channel_left = AUDIO_LEDC_CHANNEL;
  cfg.ledc_channel_right = AUDIO_LEDC_CHANNEL;
  cfg.ledc_timer_sel = AUDIO_LEDC_TIMER;
  cfg.duty_resolution = AUDIO_DUTY_RES;
  cfg.ringbuf_len = SINK_RINGBUF_LEN;

  esp_err_t err = pwm_audio_init(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "acquirePin: pwm_audio_init -> %s", esp_err_to_name(err));
    setError("audio sink init failed");
    return false;
  }
  err = pwm_audio_set_param(sampleRate, AUDIO_DUTY_RES, channels);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "acquirePin: set_param -> %s", esp_err_to_name(err));
    pwm_audio_deinit();
    setError("audio sink param failed");
    return false;
  }
  pwm_audio_set_volume(_volume);
  pwm_audio_start();
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

// Configure and start the USB UAC host speaker sink. Called from the audio
// task when the first PCM chunk is ready and the USB sink is selected.
// Blocks until usb_stream is started; device enumeration happens asynchronously
// and is reported via usbStateCb().
bool PocketMageAudio::acquireUsb(int sampleRate, int bits, int channels) {
  if (_usbStarted)
    return true;
  if (!_usbConnected) {
    setError("no usb audio device");
    return false;
  }
  uac_config_t cfg = {};
  cfg.spk_ch_num = (uint8_t)channels;
  cfg.spk_bit_resolution = (uint16_t)bits;
  cfg.spk_samples_frequence = (uint32_t)sampleRate;
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
  cfg.flags = 0;

  esp_err_t err = uac_streaming_config(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "acquireUsb: uac_streaming_config -> %s", esp_err_to_name(err));
    setError("usb sink config failed");
    return false;
  }
  err = usb_streaming_start();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "acquireUsb: usb_streaming_start -> %s", esp_err_to_name(err));
    setError("usb sink start failed");
    return false;
  }
  _usbConfigured = true;
  _usbStarted = true;
  ESP_LOGI(TAG, "acquireUsb: usb sink started (%d Hz, %d bit, %d ch)", sampleRate, bits, channels);
  return true;
}

void PocketMageAudio::releaseUsb() {
  if (!_usbStarted)
    return;
  usb_streaming_stop();
  _usbStarted = false;
  _usbConfigured = false;
  ESP_LOGI(TAG, "releaseUsb: usb sink stopped");
}

void PocketMageAudio::doPlay() {
  // Drain any clip in flight before starting the new one.
  if (_pinOwned || _usbStarted)
    doStop();

  if (!s_decodersRegistered) {
    esp_audio_err_t derr = esp_audio_dec_register_default();
    esp_audio_err_t serr = esp_audio_simple_dec_register_default();
    if (derr != ESP_AUDIO_ERR_OK || serr != ESP_AUDIO_ERR_OK) {
      ESP_LOGE(TAG, "doPlay: register decoders dec=%d simple=%d", (int)derr, (int)serr);
      setError("decoder register failed");
      _state = AudioState::Idle;
      publishEvent();
      return;
    }
    s_decodersRegistered = true;
  }

  xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
  strncpy(_currentPath, _pendingPath, sizeof(_currentPath));
  _currentPath[sizeof(_currentPath) - 1] = 0;
  xSemaphoreGiveRecursive(_mutex);

  _stopRequested = false;
  _paused = false;
  _lastError[0] = 0;
  _state = AudioState::Opening;
  publishEvent();

  decodeLoop();
  doStop();
}

void PocketMageAudio::decodeLoop() {
  if (global_fs == nullptr) {
    setError("no filesystem");
    return;
  }
  esp_audio_simple_dec_type_t type = typeFromPath(_currentPath);
  if (type == ESP_AUDIO_SIMPLE_DEC_TYPE_NONE) {
    setError("unsupported file type");
    return;
  }

  File file = global_fs->open(_currentPath, FILE_READ);
  if (!file) {
    setError("open failed");
    return;
  }

  esp_audio_simple_dec_cfg_t dcfg = {};
  dcfg.dec_type = type;
  dcfg.dec_cfg = nullptr;
  dcfg.cfg_size = 0;
  dcfg.use_frame_dec = false;

  esp_audio_simple_dec_handle_t dec = nullptr;
  if (esp_audio_simple_dec_open(&dcfg, &dec) != ESP_AUDIO_ERR_OK || dec == nullptr) {
    setError("decoder open failed");
    file.close();
    return;
  }

  uint8_t* inBuf = (uint8_t*)malloc(READ_CHUNK);
  uint8_t* outBuf = (uint8_t*)malloc(PCM_OUT_CHUNK);
  if (!inBuf || !outBuf) {
    setError("out of memory");
    if (inBuf)
      free(inBuf);
    if (outBuf)
      free(outBuf);
    esp_audio_simple_dec_close(dec);
    file.close();
    return;
  }

  uint32_t outCap = PCM_OUT_CHUNK;
  bool sinkReady = false;
  bool ok = true;

  while (!_stopRequested) {
    // Pause: stop the sink (silences output, keeps the pin) and block without
    // touching the decoder so stream position is preserved.
    if (_paused) {
      if (sinkReady && _pinOwned)
        pwm_audio_stop();
      if (_state == AudioState::Playing) {
        _state = AudioState::Paused;
        publishEvent();
      }
      while (_paused && !_stopRequested)
        vTaskDelay(pdMS_TO_TICKS(50));
      if (!_stopRequested && sinkReady && _pinOwned) {
        pwm_audio_start();
        _state = AudioState::Playing;
        publishEvent();
      }
    }
    if (_stopRequested)
      break;

    int readLen = file.read(inBuf, READ_CHUNK);
    bool eos = (readLen <= 0);
    uint32_t avail = (readLen > 0) ? (uint32_t)readLen : 0;
    uint32_t offset = 0;

    // Feed the whole read chunk; the parser consumes it piecewise.
    do {
      esp_audio_simple_dec_raw_t raw = {};
      raw.buffer = inBuf + offset;
      raw.len = avail - offset;
      raw.eos = eos;

      esp_audio_simple_dec_out_t out = {};
      out.buffer = outBuf;
      out.len = outCap;

      esp_audio_err_t derr = esp_audio_simple_dec_process(dec, &raw, &out);
      if (derr == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
        uint32_t need = out.needed_size ? out.needed_size : (outCap * 2);
        uint8_t* bigger = (uint8_t*)realloc(outBuf, need);
        if (!bigger) {
          setError("out of memory");
          ok = false;
          break;
        }
        outBuf = bigger;
        outCap = need;
        continue;  // retry same offset with a larger buffer
      }
      if (derr != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "decode process -> %d", (int)derr);
        setError("decode error");
        ok = false;
        break;
      }

      if (out.decoded_size > 0) {
        if (!sinkReady) {
          esp_audio_simple_dec_info_t info = {};
          int rate = DEFAULT_SAMPLE_RATE;
          int ch = DEFAULT_CHANNELS;
          if (esp_audio_simple_dec_get_info(dec, &info) == ESP_AUDIO_ERR_OK &&
              info.sample_rate > 0) {
            rate = (int)info.sample_rate;
            ch = info.channel ? info.channel : DEFAULT_CHANNELS;
          }
          if (_output == AudioOutput::Usb && _usbConnected) {
            if (!acquireUsb(rate, DEFAULT_BITS, ch)) {
              ok = false;
              break;
            }
          } else if (!acquirePin(rate, DEFAULT_BITS, ch)) {
            ok = false;
            break;
          }
          sinkReady = true;
          _state = AudioState::Playing;
          publishEvent();
        }

        size_t written = 0;
        if (_output == AudioOutput::Usb && _usbConnected && _usbStarted) {
          esp_err_t err = uac_spk_streaming_write(out.buffer, out.decoded_size, portMAX_DELAY);
          if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
            ESP_LOGE(TAG, "usb write -> %s", esp_err_to_name(err));
            setError("usb sink write failed");
            ok = false;
            break;
          }
        } else {
          pwm_audio_write(out.buffer, out.decoded_size, &written, portMAX_DELAY);
        }
      }

      offset += raw.consumed;
      // No progress and no data left means the parser needs the next read.
      if (raw.consumed == 0)
        break;
    } while (offset < avail && !_stopRequested && !_paused);

    if (!ok || eos)
      break;
  }

  free(inBuf);
  free(outBuf);
  esp_audio_simple_dec_close(dec);
  file.close();
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

void PocketMageAudio::setError(const char* msg) {
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
  if (_eventPending) {
    _eventPending = false;
    if (_eventCallback)
      _eventCallback();
  }
}
