#pragma once

#include "sd_gateway.h"

namespace pm_sd_gateway {

/** Open request.path as a task-owned cursor; set ok and a unique positive retVal handle on success.
 * Calls must be serialized by the gateway; failure does not change retVal.
 */
void directoryOpen(Request *request);
/** Peek/read one name including NUL into request.nameOut/len; zero retVal means EOF.
 * Null output and zero capacity peek without advancing. Failure leaves outputs unchanged.
 */
void directoryRead(Request *request);
/** Return whether handle is live and belongs to owner; null owners are rejected. */
bool directoryOwned(int handle, TaskHandle_t owner);
/** Release a live handle; return false for invalid handles or failed POSIX close. */
bool directoryClose(int handle);
/** Release owner's cursors; return false if any close fails. Other tasks are unaffected. */
bool directoryCloseOwner(TaskHandle_t owner);
/** Invalidate every cursor before remount; return false if any close fails. */
bool directoryCloseAll();
/** Return whether any cursor is open, including an exhausted cursor awaiting close. */
bool directoriesOpen();

} // namespace pm_sd_gateway
