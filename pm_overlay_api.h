// C boundary for the OS-owned ELF overlay file provider.
#ifndef PM_OVERLAY_API_H
#define PM_OVERLAY_API_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Open an overlay file read-only; return its slot or -1 on failure. */
int pm_elfo_open(const char *path);
/** Read exactly len bytes at offset into buf; return zero or -1 on failure. */
int pm_elfo_read(int slot, size_t offset, void *buf, size_t len);
/** Return the file size for slot, or zero for an invalid slot. */
size_t pm_elfo_size(int slot);
/** Release slot; the overlay port has no close-result channel. */
void pm_elfo_close(int slot);

#ifdef __cplusplus
}
#endif

#endif
