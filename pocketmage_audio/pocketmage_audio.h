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
#include "audio_pcm.h"

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
  void stopPlayback();         // Stop the current clip and release the pin
  void pause();                // Suspend decode, keep the pin owned
  void resume();               // Resume a paused clip
  void setVolume(int8_t vol);  // -16..16, see pwm_audio_set_volume
  /** Set shared volume from 0 (mute) to 255; quantized to the legacy gain steps. */
  void setVolumeLevel(uint8_t level);
  /** Return the shared normalized volume, including legacy setVolume changes. */
  uint8_t getVolumeLevel() const;
  /** Load and validate shared NVS audio preferences without claiming USB.
   * Return false on I/O or schema errors, leaving the live settings unchanged.
   */
  bool loadSettings();
  /** Atomically persist volume and output; return false on NVS failure. */
  bool saveSettings();

  // Status queries (thread-safe)
  AudioState getState() const;
  bool isPlaying() const;
  bool ownsPin() const; // True while the sink holds BZ_PIN
  String getCurrentPath() const;
  String getLastError() const;
  int8_t getVolume() const;
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

  // Pin arbitration against the buzzer
  bool acquirePin(int sampleRate, int bits, int channels);
  void releasePin();

  // USB UAC host speaker sink
  bool acquireUsb(int sampleRate, int bits, int channels);
  void releaseUsb();
  bool beginUsb();
  bool stopUsb();
  esp_err_t writeUsb(void *data, size_t size, size_t timeoutMs);

  // USB device state callback (static: passed to usb_streaming_state_register)
  static void usbStateCb(usb_stream_state_t state, void *user_ptr);

  // Helpers
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
  std::atomic<int8_t> _volume;
  std::atomic<uint32_t> _bytesDecoded;

  // FreeRTOS handles
  TaskHandle_t _taskHandle;
  QueueHandle_t _commandQueue;
  SemaphoreHandle_t _shutdownSem;

  // Event callback
  AudioEventCallback _eventCallback;

  // Pre-allocated decode buffers
  uint8_t *_inBuf;
  uint8_t *_outBuf;
  uint32_t _outCap;
  uint32_t _leftoverLen;

  // Flags
  std::atomic<bool> _initialized;
  bool _settingsLoaded = false;
  std::atomic<bool> _pinOwned;
  std::atomic<bool> _eventPending;
  std::atomic<bool> _stopRequested;
  std::atomic<bool> _paused;
  std::atomic<bool> _shutdownRequested{false};

  // Output sink selection
  std::atomic<AudioOutput> _output;
  std::atomic<bool> _usbConnected;
  std::atomic<uint32_t> _usbConnectionId;
  bool _usbConfigured;
  std::atomic<bool> _usbStarted;
  std::atomic<bool> _usbProbeSuppressed{false};
  uint32_t _usbProbeStartedAt = 0;

  // Decode task stack: the simple decoder plus file I/O needs headroom.
#ifdef BOARD_HAS_PSRAM
  static constexpr uint32_t TASK_STACK_BYTES = 20480;
#else
  static constexpr uint32_t TASK_STACK_BYTES = 8192;
#endif
  static constexpr int TASK_PRIORITY = 2;
  static constexpr int TASK_CORE = 0;

  // Ring buffer and read-chunk sizes (bytes).
  static constexpr uint32_t SINK_RINGBUF_LEN = 8 * 1024;
  static constexpr uint32_t READ_CHUNK = 4 * 1024;
  static constexpr uint32_t PCM_OUT_CHUNK = 4608; // 1152 samples × 2 ch × 2 bytes

  // Default sink parameters until the decoder reports the real format.
  static constexpr int DEFAULT_SAMPLE_RATE = 44100;
  static constexpr int DEFAULT_BITS = 16;
  static constexpr int DEFAULT_CHANNELS = 2;
  static constexpr int8_t DEFAULT_VOLUME = 4;
};

// Global accessor for convenience
extern PocketMageAudio &P_AUDIO;
