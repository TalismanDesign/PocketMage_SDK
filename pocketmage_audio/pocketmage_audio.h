#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <atomic>

#include <usb_stream.h>
#include "playback_session.h"
#include "audio_format.h"
#include "audio_pcm.h"

namespace pm_media {
struct WavFormat;
}

// Playback state machine
enum class AudioState {
  Idle,    // No clip loaded
  Opening, // Decoder is opening the file
  Playing, // Actively decoding and feeding the sink
  Paused,  // Decode suspended, pin still owned
  Stopping // Teardown in progress
};

// Output sink selection
enum class AudioOutput {
  Piezo, // PWM on piezo / buzzer
  Usb,   // USB UAC host speaker
  Auto,  // Follow USB speaker presence during an explicit audio session
};

// pwm_audio shares the piezo (BZ_PIN) with the buzzer; both drive LEDC. The
// sink is torn down when playback ends so the buzzer can reclaim the pin.
typedef std::function<void(void)> AudioEventCallback;

class PocketMageAudio {
public:
  static PocketMageAudio &getInstance();

  // Lifecycle
  /** Start an audio session; retain serial unless a USB accessory needs probing. */
  void begin();
  /** OS connector poll: follow Type-C attachment and release unused USB hosts.
   * Return true while audio owns or is probing the PHY, or handoff is unsafe.
   * False allows keyboard polling. Call from the control task, not a USB callback.
   */
  bool pollUsb();
  /** Stop the service, waiting up to five seconds for decoder cleanup.
   * On timeout, retain live resources and reject playback until stop() succeeds;
   * getLastError() describes the failure. Safe to retry from the control task.
   */
  void stop();
  /** Stop the service and return true only after decoder and USB host teardown.
   * False retains live resources; getLastError() describes the retryable failure.
   */
  bool stopChecked();

  // Control methods (all non-blocking, dispatch to the decode task)
  /** Request playback of path from the active FS, superseding the previous clip.
   * Every call publishes a new ID and resets PCM time. Invalid paths or service
   * failures produce Failed with getLastError() detail, without starting decode.
   */
  void play(const char *path);
  void stopPlayback(); // Stop the current clip and release the pin
  /** Cancel playback and wait for decoder/sink cleanup, retaining the audio session.
   * Call from the control task. False on timeout keeps cancellation pending and
   * rejects new playback until cleanup completes. No artwork should load on false.
   */
  bool stopPlaybackChecked(uint32_t timeoutMs = 5000);
  void pause();  // Suspend decode, keep the pin owned
  void resume(); // Resume a paused clip
  /** Set the volume of the sink that plays next: 0 mutes, 1..255 spans -48..0 dB
   * without boosting PCM. USB and piezo keep separate levels; the USB level is
   * used while a speaker is streaming or connected and selectable.
   */
  void setVolumeLevel(uint8_t level);
  /** Return the exact normalized volume, 0 to 255, of the sink that plays next. */
  uint8_t getVolumeLevel() const;
  /** Set one sink's level directly (Usb or Piezo; Auto is ignored). */
  void setVolumeLevelFor(AudioOutput output, uint8_t level);
  /** Return one sink's level (Auto reports the piezo level). */
  uint8_t getVolumeLevelFor(AudioOutput output) const;
  /** Load and validate shared NVS audio preferences without claiming USB.
   * Return false on I/O or schema errors, leaving the live settings unchanged.
   */
  bool loadSettings();
  /** Atomically persist both volumes and output; return false on NVS failure. */
  bool saveSettings();

  // Status queries (thread-safe)
  AudioState getState() const;
  bool isPlaying() const;
  bool ownsPin() const; // True while the sink holds BZ_PIN
  String getCurrentPath() const;
  String getLastError() const;
  uint32_t getPosition() const; // Encoded file bytes consumed, including skipped tags
  /** Return accepted PCM milliseconds, excluding tags and paused time.
   * Sink buffering can lead audible playback by its queued PCM duration.
   */
  uint64_t getPositionMs() const;

  /** Return the latest play request ID, or zero before the first request. */
  uint32_t getPlaybackId() const;
  /** Return PlaybackSession::Result for id; a superseded request is cancelled. */
  int getPlaybackResult(uint32_t id) const;

  // Output sink selection. Auto is the default; piezo is the fallback when no USB
  // audio device is plugged in. Usb routes decoded PCM to a USB UAC host
  // speaker (usb_stream) instead of the piezo.
  void setOutput(AudioOutput output);
  AudioOutput getOutput() const;
  /** Return the sink currently holding playback resources, falling back to piezo. */
  AudioOutput getActiveOutput() const;
  bool usbConnected() const;

  // Event callback (fired on state change: use to trigger a display update)
  void setEventCallback(AudioEventCallback cb);
  void dispatchEvents();

private:
  PocketMageAudio();
  ~PocketMageAudio();
  PocketMageAudio(const PocketMageAudio &) = delete;
  PocketMageAudio &operator=(const PocketMageAudio &) = delete;

  // Task function
  static void audioTaskFunc(void *param);
  void taskLoop();

  // Internal operations (run in task context)
  void doPlay();
  void doStop();
  void decodeLoop(uint32_t playbackId);
  bool decodeFloatWav(uint32_t playbackId, int slot, const pm_media::WavFormat &wav, uint8_t *inBuf,
                      uint8_t *outBuf, uint32_t inCapacity);
  bool writePcm(void *buffer, size_t bytes, int sampleRate, int channels, uint32_t &connection,
                bool &rejected, bool &accepted, bool &scaled);

  // Pin arbitration against the buzzer
  bool acquirePin(int sampleRate, int bits, int channels);
  void releasePin();

  // USB UAC host speaker sink
  bool acquireUsb(int sampleRate, int bits, int channels);
  void releaseUsb();
  bool beginUsb();
  bool stopUsb();
  esp_err_t writeUsb(void *data, size_t size, size_t timeoutMs);
  uint32_t pendingDecodeBytes() const;

  // USB device state callback (static: passed to usb_streaming_state_register)
  static void usbStateCb(usb_stream_state_t state, void *user_ptr);

  // Helpers
  /** Sink whose level applies to the next frame: Usb or Piezo. */
  AudioOutput volumeOutput() const;
  void setError(const char *msg);
  void publishEvent();

  // Command queue types
  enum class Command : uint8_t { None = 0, Play, Stop };

  // Thread-safe state
  mutable SemaphoreHandle_t _mutex;
  PlaybackSession _playback;
  pm_audio::PlaybackTimeline _clock;
  std::atomic<AudioState> _state;
  char _currentPath[256];
  char _pendingPath[256];
  char _lastError[64];
  std::atomic<uint8_t> _piezoVolume;
  std::atomic<uint8_t> _usbVolume;
  std::atomic<uint32_t> _bytesDecoded;

  // FreeRTOS handles
  TaskHandle_t _taskHandle;
  QueueHandle_t _commandQueue;
  SemaphoreHandle_t _shutdownSem;
  SemaphoreHandle_t _taskExitSem;
  uint32_t _taskStackBytes = 0;
  pm_audio::AudioFormat _taskFormat = pm_audio::AudioFormat::None;
  std::atomic<bool> _taskResizePending{false};

  // Event callback
  AudioEventCallback _eventCallback;

  // Flags
  std::atomic<bool> _initialized;
  bool _settingsLoaded = false;
  std::atomic<bool> _pinOwned;
  std::atomic<bool> _eventPending;
  std::atomic<bool> _stopRequested;
  std::atomic<bool> _paused;
  std::atomic<bool> _shutdownRequested{false};
  std::atomic<bool> _stopWaitPending{false};

  // Output sink selection
  std::atomic<AudioOutput> _output;
  std::atomic<bool> _usbConnected;
  std::atomic<uint32_t> _usbConnectionId;
  bool _usbConfigured;
  std::atomic<bool> _usbStarted;
  std::atomic<bool> _usbProbeSuppressed{false};
  std::atomic<bool> _usbSpeakerSeen{false};
  std::atomic<bool> _usbMemoryBackoff{false};
  std::atomic<uint32_t> _usbMemoryRetryStartedAt{0};
  // Clip memory already allocated, so the USB heap check does not count it twice.
  std::atomic<bool> _decodeBuffersResident{false};
  std::atomic<bool> _decoderResident{false};
  uint32_t _usbProbeStartedAt = 0;

  static constexpr int TASK_PRIORITY = 2;
  static constexpr int TASK_CORE = 0;

  // Ring buffer and read-chunk sizes (bytes).
  static constexpr uint32_t SINK_RINGBUF_LEN = 8 * 1024;
  static constexpr uint32_t USB_RINGBUF_LEN = 8 * 1024;
  static constexpr uint32_t USB_REPROBE_WAIT_MS = 5000;
  static constexpr uint32_t READ_CHUNK = 4 * 1024;
  static constexpr uint32_t PCM_OUT_CHUNK = 4608; // 1152 samples × 2 ch × 2 bytes
  static_assert(USB_RINGBUF_LEN >= PCM_OUT_CHUNK,
                "The USB byte ring must accept a complete decoded PCM frame");

  // Default sink parameters until the decoder reports the real format.
  static constexpr int DEFAULT_SAMPLE_RATE = 44100;
  static constexpr int DEFAULT_BITS = 16;
  static constexpr int DEFAULT_CHANNELS = 2;
};

// Global accessor for convenience
extern PocketMageAudio &P_AUDIO;
