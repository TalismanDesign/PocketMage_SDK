#pragma once

#include <cstddef>
#include <cstdint>
#include <hid_host.h>

/** Internal USB keyboard host and report bridge, owned by the OS control task. */
namespace pm_keyboard {

/** Claim the connector and start HID. False leaves any incomplete cleanup retryable. */
bool beginUsb();
/** Disconnect HID, join workers and restore serial. False retains ownership for retry.
 * Call from the control task, never from a USB callback.
 * See https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/peripherals/usb_host.html
 */
bool stopUsb();
/** Return whether a shutdown is pending, including after a failed startup. */
bool stopping();
/** Allocate the firmware-lifetime input queue before starting report callbacks. */
bool prepareInput();
/** Clear queued characters and held keys after all HID callbacks have stopped. */
void resetInput();
/** Translate a validated input report from the HID driver task into queued keys. */
void handleReport(const hid_host_dev_params_t &params, const uint8_t *data, size_t length);

} // namespace pm_keyboard
