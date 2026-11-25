#pragma once

/** Internal arbitration for the shared connector and ESP32-S3 USB PHY. */
namespace pm_usb {

/** Debug serial is the unclaimed default; host and storage stacks are exclusive. */
enum class Owner { DebugSerial, Audio, Keyboard, Storage };

/** Serialize role changes across app and OS tasks; recursive within a task. */
class Lock {
public:
  /** Acquire the role mutex; abort if the synchronization invariant fails. */
  Lock();
  /** Release the role mutex. */
  ~Lock();
  Lock(const Lock &) = delete;
  Lock &operator=(const Lock &) = delete;
};

/** Return the current owner, including a stack starting or stopping. */
Owner owner();
/** Reserve a free PHY for requested; false leaves another owner's role intact. */
bool claim(Owner requested);
/** Restore serial after requested's stack has stopped. False retains ownership. */
bool release(Owner requested);
/** Apply Type-C VBUS policy. False reports an I2C failure and leaves routing intact. */
bool updatePower(bool hostAttached);
/** Restore serial registers before an immediate reset; no USB calls may follow. */
void restoreSerialForRestart();

} // namespace pm_usb
