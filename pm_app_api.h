// Host primitives the generator cannot produce. The SDK class surface lives in
// the generated pm_sdk_app.h, which this header includes.

#ifndef PM_APP_API_H
#define PM_APP_API_H

#include <pm_sdk_app.h>

#ifdef __cplusplus
extern "C" {
#endif

// OLED framebuffer

// Commit the framebuffer. Draw with pm_text(PM_TARGET_OLED, ...) first.
void pm_oled_send(void);

// e-ink canvas. These reach the panel driver directly, so there is no SDK
// method for the generator to read.

int pm_eink_width(void);
int pm_eink_height(void);

// Fill the whole panel white.
void pm_eink_clear(void);

// Set one pixel. ink=true draws black, false white.
void pm_eink_pixel(int x, int y, bool ink);

// Rectangle including both corners; corner order does not matter.
void pm_eink_rect(int x0, int y0, int x1, int y1, bool fill, bool ink);

// Draw UTF-8 text with y as the baseline. Out of range target or style is
// ignored rather than trapping: FontEngine indexes its font tables unchecked.
void pm_text(int target, int x, int y, const char* text, int style);

// Write len bytes verbatim, truncating any existing file. True on full write.
bool pm_sd_write_binary_file(const char* path, const uint8_t* buf, size_t len);

// Create path (one level, like POSIX mkdir). True if it exists afterwards.
bool pm_sd_mkdir(const char* path);

// Directory enumeration. pm_sd_list_dir is flat and capped at MAX_FILES; this
// walks an arbitrary directory with no cap and reports subdirectories, so apps
// can recurse. One enumeration is live at a time, held in host memory.
//
// Open path for enumeration. Returns the entry count, or -1 on error. A prior
// open is discarded.
int pm_sd_dir_open(const char* path);

// Copy entry i's leaf name (no directory prefix) into name_out, set *is_dir to
// whether it is a directory, and return true. False if i is out of range or no
// enumeration is open. name_out is always NUL-terminated when true.
bool pm_sd_dir_entry(int i, char* name_out, size_t name_size, bool* is_dir);

// Release the open enumeration and its entry table.
void pm_sd_dir_close(void);

// Clock fields. nowDT returns a DateTime by value, which has no C spelling.

// UNIX epoch in seconds (UTC).
int64_t pm_clock_epoch(void);

// Local time as "YYYY-MM-DD HH:MM:SS". Valid until the next call.
const char* pm_clock_timestamp(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // PM_APP_API_H
