// C adapters whose semantics or types differ from the native SDK surface.
// Ordinary service calls and drawing primitives live in generated pm_sdk_app.h.
#ifndef PM_APP_API_H
#define PM_APP_API_H

#include <pm_sdk_app.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Draw UTF-8 text with y as its baseline; invalid target/style is a no-op. */
void pm_text(int target, int x, int y, const char *text, int style);
/** Measure text in pixels; return zero for an invalid target/style. */
int pm_text_width(int target, const char *text, int style);
/** Return font height in pixels, or zero for an invalid target/style. */
int pm_font_height(int target, int style);
/** Select black ink (nonzero) or white (zero); ignore invalid targets.
 * This legacy ink convention differs from FontEngine::setTextColor.
 */
void pm_text_color(int target, uint16_t color);

/** Probe path; fill width, height, and format (0=none, 1=JPEG, 2=PNG) on success. */
bool pm_image_probe(const char *path, int *width, int *height, int *format);
/** Legacy fit-only decoder; new callers use pm_image_decode with explicit fill. */
bool pm_image_decode_to_1bit(const char *path, uint8_t *buf, int buf_w, int buf_h,
                            int out_x, int out_y, int max_size);

/** Legacy read-only open; new callers use pm_sd_stream_open_mode(path, 'r'). */
int pm_sd_stream_open(const char *path);
/** Legacy absolute seek; offsets exceeding LONG_MAX fail rather than wrap. */
bool pm_sd_stream_seek(int slot, uint32_t offset);
/** Legacy close without a result; use pm_sd_stream_close_checked to detect errors. */
void pm_sd_stream_close(int slot);


#ifdef __cplusplus
}
#endif

#endif
