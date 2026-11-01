#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>

#include <usb_stream.h>

// Playback state machine
enum class AudioState {
  Idle,     // No clip loaded
  Opening,  // Decoder is opening the file
  Playing,  // Actively decoding and feeding the sink
  Paused,   // Decode suspended, pin still owned
  Stopping  // Teardown in progress
};

// Output sink selection
enum class AudioOutput {
  Piezo,  // PWM on piezo / buzzer
  Usb,    // USB UAC host speaker
};

// pwm_audio shares the piezo (BZ_PIN) with the buzzer; both drive LEDC. The
// sink is torn down when playback ends so the buzzer can reclaim the pin.
typedef std::function<void(void)> AudioEventCallback;

class PocketMageAudio {
 public:
  static PocketMageAudio& getInstance();

  // Lifecycle
  void begin();  // Call once at startup to create the decode task
  void stop();   // Call at shutdown to cleanly stop the service

  // Control methods (all non-blocking, dispatch to the decode task)
  void play(const char* path);  // Decode and play a file from the active FS
  void stopPlayback();          // Stop the current clip and release the pin
  void pause();                 // Suspend decode, keep the pin owned
  void resume();                // Resume a paused clip
  void setVolume(int8_t vol);   // -16..16, see pwm_audio_set_volume

  // Status queries (thread-safe)
  AudioState getState() const;
  bool isPlaying() const;
  bool ownsPin() const;  // True while the sink holds BZ_PIN
  String getCurrentPath() const;
  String getLastError() const;
  int8_t getVolume() const;
  uint32_t getPosition() const;  // Bytes decoded so far in the current clip

  // Output sink selection. Piezo is the default and fallback when no USB
  // audio device is plugged in. Usb routes decoded PCM to a USB UAC host
  // speaker (usb_stream) instead of the piezo.
  void setOutput(AudioOutput output);
  AudioOutput getOutput() const;
  bool usbConnected() const;

  // Event callback (fired on state change: use to trigger a display update)
  void setEventCallback(AudioEventCallback cb);
  void dispatchEvents();

 private:
  PocketMageAudio();
  ~PocketMageAudio();
  PocketMageAudio(const PocketMageAudio&) = delete;
  PocketMageAudio& operator=(const PocketMageAudio&) = delete;

  // Task function
  static void audioTaskFunc(void* param);
  void taskLoop();

  // Internal operations (run in task context)
  void doPlay();
  void doStop();
  void decodeLoop();

  // Pin arbitration against the buzzer
  bool acquirePin(int sampleRate, int bits, int channels);
  void releasePin();

  // USB UAC host speaker sink
  bool acquireUsb(int sampleRate, int bits, int channels);
  void releaseUsb();

  // USB device state callback (static: passed to usb_streaming_state_register)
  static void usbStateCb(usb_stream_state_t state, void* user_ptr);

  // Helpers
  void setError(const char* msg);
  void publishEvent();

  // Command queue types
  enum class Command : uint8_t {
    None = 0,
    Play,
    Stop,
    Pause,
    Resume,
    Shutdown
  };

  // Thread-safe state
  mutable SemaphoreHandle_t _mutex;
  volatile AudioState _state;
  char _currentPath[128];
  char _pendingPath[128];
  char _lastError[64];
  volatile int8_t _volume;
  volatile uint32_t _bytesDecoded;

  // FreeRTOS handles
  TaskHandle_t _taskHandle;
  QueueHandle_t _commandQueue;
  SemaphoreHandle_t _shutdownSem;

  // Event callback
  AudioEventCallback _eventCallback;

  // Flags
  bool _initialized;
  volatile bool _pinOwned;
  volatile bool _eventPending;
  volatile bool _stopRequested;
  volatile bool _paused;

  // Output sink selection
  volatile AudioOutput _output;
  volatile bool _usbConnected;
  bool _usbConfigured;
  bool _usbStarted;

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
  static constexpr uint32_t PCM_OUT_CHUNK = 4 * 1024;

  // Default sink parameters until the decoder reports the real format.
  static constexpr int DEFAULT_SAMPLE_RATE = 44100;
  static constexpr int DEFAULT_BITS = 16;
  static constexpr int DEFAULT_CHANNELS = 2;
  static constexpr int8_t DEFAULT_VOLUME = 0;
};

// Global accessor for convenience
extern PocketMageAudio& P_AUDIO;
