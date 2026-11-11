// App-facing C ABI for external ELF apps. Regenerate, do not edit.
// String returns stay valid for the next POOL_SLOTS string-returning calls.

#ifndef PM_SDK_APP_H
#define PM_SDK_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pm_overlay_api.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bump on any incompatible change here. Apps compare at startup.
#define PM_APP_API_ABI 2u

// Mirrors DisplayTarget, values read from the SDK header.
enum pm_target {
  PM_TARGET_OLED = 0,
  PM_TARGET_EINK = 1,
};

// Mirrors FontStyle, values read from the SDK header.
enum pm_style {
  PM_STYLE_TINY = 0,
  PM_STYLE_BODY = 1,
  PM_STYLE_BODY_BOLD = 2,
  PM_STYLE_BODY_ITALIC = 3,
  PM_STYLE_BODY_BOLD_ITALIC = 4,
  PM_STYLE_MEDIUM = 5,
  PM_STYLE_SMALL = 6,
  PM_STYLE_BODY_NARROW = 7,
  PM_STYLE_MONO = 8,
  PM_STYLE_MONO_BOLD = 9,
  PM_STYLE_MONO_ITALIC = 10,
  PM_STYLE_MONO_BOLD_ITALIC = 11,
  PM_STYLE_SANS = 12,
  PM_STYLE_SANS_BOLD = 13,
  PM_STYLE_SANS_ITALIC = 14,
  PM_STYLE_SANS_BOLD_ITALIC = 15,
  PM_STYLE_CAPTION = 16,
  PM_STYLE_HEADING3 = 17,
  PM_STYLE_HEADING2 = 18,
  PM_STYLE_HEADING1 = 19,
  PM_STYLE_LARGE = 20,
  PM_STYLE_OLED_WORD = 21,
  PM_STYLE_TERMINAL = 22,
  PM_STYLE_TERMINAL_BIG = 23,
  PM_STYLE_CLOCK_DIGIT = 24,
  PM_STYLE_COUNT = 25,
};

// Mirrors Lang, values read from the SDK header.
enum pm_lang {
  PM_LANG_ENGLISH = 0,
  PM_LANG_FRENCH = 1,
  PM_LANG_SPANISH = 2,
  PM_LANG_GERMAN = 3,
  PM_LANG_COUNT = 4,
};

// Mirrors EinkRefresh, values read from the SDK header.
enum pm_refresh {
  PM_REFRESH_NORMAL = 0,
  PM_REFRESH_FORCE_FULL = 1,
};

// Mirrors AudioState, values read from the SDK header.
enum pm_audio_state {
  PM_AUDIO_STATE_IDLE = 0,
  PM_AUDIO_STATE_OPENING = 1,
  PM_AUDIO_STATE_PLAYING = 2,
  PM_AUDIO_STATE_PAUSED = 3,
  PM_AUDIO_STATE_STOPPING = 4,
};

// Mirrors AudioOutput, values read from the SDK header.
enum pm_audio_output {
  PM_AUDIO_OUTPUT_PIEZO = 0,
  PM_AUDIO_OUTPUT_USB = 1,
  PM_AUDIO_OUTPUT_AUTO = 2,
};

// Mirrors PlaybackSession::Result, values read from the SDK header.
enum pm_audio_result {
  PM_AUDIO_RESULT_PENDING = 0,
  PM_AUDIO_RESULT_ENDED = 1,
  PM_AUDIO_RESULT_FAILED = 2,
  PM_AUDIO_RESULT_CANCELLED = 3,
};

// Mirrors ImageFormat, values read from the SDK header.
enum pm_image_format {
  PM_IMAGE_FORMAT_NONE = 0,
  PM_IMAGE_FORMAT_JPEG = 1,
  PM_IMAGE_FORMAT_PNG = 2,
};

// Mirrors KeyCode, values read from the SDK header.
enum pm_key {
  PM_KEY_NONE = 0,
  PM_KEY_FN_RIGHT = 6,
  PM_KEY_FN_SELECT = 7,
  PM_KEY_BACKSPACE = 8,
  PM_KEY_TAB = 9,
  PM_KEY_ESCAPE = 12,
  PM_KEY_ENTER = 13,
  PM_KEY_BACK_TAB = 14,
  PM_KEY_SHIFT = 17,
  PM_KEY_FN = 18,
  PM_KEY_LEFT = 19,
  PM_KEY_SELECT = 20,
  PM_KEY_RIGHT = 21,
  PM_KEY_UP = 24,
  PM_KEY_DOWN = 25,
  PM_KEY_FN_SHIFT_RIGHT = 26,
  PM_KEY_ASCII_ESCAPE = 27,
  PM_KEY_SHIFT_LEFT = 28,
  PM_KEY_SHIFT_SELECT = 29,
  PM_KEY_SHIFT_RIGHT = 30,
  PM_KEY_SPACE = 32,
};

// Mirrors TransportIcon, values read from the SDK header.
enum pm_transport_icon {
  PM_TRANSPORT_ICON_PLAY = 0,
  PM_TRANSPORT_ICON_PAUSE = 1,
  PM_TRANSPORT_ICON_STOP = 2,
};

// Mirrors Jingles, values read from the SDK header.
enum pm_jingle {
  PM_JINGLE_STARTUP = 0,
  PM_JINGLE_SHUTDOWN = 1,
  PM_JINGLE_COUNT = 2,
};

// Field-wise projection of ImageInfo; never aliases its native layout.
struct pm_image_info {
  int width;
  int height;
  int format;
};

// Field-wise projection of MediaInfo; never aliases its native layout.
struct pm_media_info {
  char title[128];
  char artist[128];
  char album[128];
  uint64_t durationMs;
  uint32_t audioOffset;
  uint32_t coverOffset;
  uint32_t coverLength;
  int coverFormat;
};

typedef void (*pm_app_exit_callback)(void);
typedef void (*pm_app_destructor)(void*);

// version

// ABI identifier of the running host.
uint32_t pm_app_abi(void);

// SDK version string the running host was built with.
const char* pm_host_sdk_version(void);

// Declared here, not via pocketmage_globals.h: the export table pulls
// the address and globals.h clashes with its bare newlib prototypes.
#ifdef __cplusplus
extern "C" {
#endif
extern const char pocketmage_sdk_version[];
#ifdef __cplusplus
}
#endif

// Host primitives exported from the firmware core.
#ifdef __cplusplus
extern "C" {
#endif
void delay(unsigned long ms);
#ifdef __cplusplus
}  // extern "C"
#endif

// app

int pm_app_at_exit(pm_app_exit_callback a0);
int pm_app_register_destructor(pm_app_destructor a0, void* a1, void* a2);
void pm_app_finalize_destructors(void* a0);

// audio

void pm_audio_begin(void);
bool pm_audio_poll_usb(void);
void pm_audio_stop(void);
bool pm_audio_stop_checked(void);
void pm_audio_play(const char* a0);
void pm_audio_stop_playback(void);
void pm_audio_pause(void);
void pm_audio_resume(void);
void pm_audio_set_volume(int8_t a0);
void pm_audio_set_volume_level(uint8_t a0);
uint8_t pm_audio_get_volume_level(void);
bool pm_audio_load_settings(void);
bool pm_audio_save_settings(void);
int pm_audio_get_state(void);
bool pm_audio_is_playing(void);
bool pm_audio_owns_pin(void);
const char* pm_audio_get_current_path(void);
const char* pm_audio_get_last_error(void);
int8_t pm_audio_get_volume(void);
uint32_t pm_audio_get_position(void);
uint64_t pm_audio_get_position_ms(void);
uint32_t pm_audio_get_playback_id(void);
int pm_audio_get_playback_result(uint32_t a0);
void pm_audio_set_output(int a0);
int pm_audio_get_output(void);
int pm_audio_get_active_output(void);
bool pm_audio_usb_connected(void);
void pm_audio_dispatch_events(void);

// bz

bool pm_bz_begin(int a0);
void pm_bz_end(void);
void pm_bz_play_jingle(int a0);
void pm_bz_drain_pending(void);

// clock

bool pm_clock_begin(void);
void pm_clock_set_time_from_string(const char* a0);
bool pm_clock_valid(void);
int64_t pm_clock_epoch(void);
const char* pm_clock_timestamp(void);
void pm_clock_set_timeout_millis(long a0);
void pm_clock_set_prev_time_millis(long a0);

// eink

void pm_eink_set_line_spacing(uint8_t a0);
void pm_eink_set_full_refresh_after(uint8_t a0);
int pm_eink_width(void);
int pm_eink_height(void);
void pm_eink_clear(void);
void pm_eink_pixel(int a0, int a1, bool a2);
void pm_eink_rect(int a0, int a1, int a2, int a3, bool a4, bool a5);
void pm_eink_triangle(int a0, int a1, int a2, int a3, int a4, int a5, bool a6, bool a7);
void pm_eink_refresh(void);
void pm_eink_set_fast_full_refresh(bool a0);
void pm_eink_status_bar(const char* a0, bool a1);
void pm_eink_draw_status_bar(const char* a0);
void pm_eink_reset_display(bool a0, uint16_t a1);
int pm_eink_count_lines(const char* a0, size_t a1);
uint8_t pm_eink_get_font_height(void);
int pm_eink_max_lines(void);
uint16_t pm_eink_get_eink_text_width(const char* a0);
uint8_t pm_eink_get_line_spacing(void);
void pm_eink_force_slow_full_update(bool a0);
void pm_eink_mark_panel_needs_full_refresh(void);

// font_engine

void pm_font_engine_draw_glyph(int a0, int a1, int a2, uint16_t a3, int a4);
int pm_font_engine_char_width(int a0, uint16_t a1, int a2);
int pm_font_engine_font_ascent(int a0, int a1);
int pm_font_engine_font_descent(int a0, int a1);
void pm_font_engine_draw_text_txt(int a0, int a1, int a2, const char* a3, uint8_t a4, uint8_t a5, uint8_t a6);
void pm_font_engine_draw_glyph_txt(int a0, int a1, int a2, uint16_t a3, uint8_t a4, uint8_t a5, uint8_t a6);
int pm_font_engine_text_width_txt(int a0, const char* a1, uint8_t a2, uint8_t a3, uint8_t a4);
int pm_font_engine_char_width_txt(int a0, uint16_t a1, uint8_t a2, uint8_t a3, uint8_t a4);
int pm_font_engine_font_height_txt(uint8_t a0, uint8_t a1, uint8_t a2);

// i18n

void pm_i18n_set_language(int a0);
bool pm_i18n_set_language_by_code(const char* a0);
int pm_i18n_language(void);
int pm_i18n_language_count(void);
const char* pm_i18n_code(void);
const char* pm_i18n_code_at(int a0);
const char* pm_i18n_native_name(void);
const char* pm_i18n_native_name_at(int a0);
const char* pm_i18n_get(int a0);
const char* pm_i18n_month_name(int a0);
const char* pm_i18n_day_name(int a0);
const char* pm_i18n_app_name(int a0);
const char* pm_i18n_kb_app_name(int a0);
const char* pm_i18n_normalize_command(const char* a0);

// image

bool pm_image_probe_region(const char* a0, struct pm_image_info* a1, uint32_t a2, uint32_t a3);
bool pm_image_decode(const char* a0, uint8_t* a1, int a2, int a3, int a4, int a5, int a6, bool a7);
bool pm_image_decode_region_to_1bit(const char* a0, uint32_t a1, uint32_t a2, uint8_t* a3, int a4, int a5, int a6, int a7, int a8, bool a9);
bool pm_image_decode_from_file(const char* a0, uint8_t* a1, int a2, int a3, int a4, int a5, int a6, bool a7);
void pm_image_clear(void);

// kb

void pm_kb_set_keyboard_state(int a0);
int pm_kb_state(void);
void pm_kb_toggle_shift(void);
void pm_kb_toggle_fn(void);
bool pm_kb_accept_key(void);
char pm_kb_read(void);
void pm_kb_check_usbkb(void);
void pm_kb_flush(void);
void pm_kb_set_tca8418_event(void);

// media

bool pm_media_probe(const char* a0, struct pm_media_info* a1);

// misc

// Defined by hand in pm_app_api.cpp.
void pm_text(int a0, int a1, int a2, const char* a3, int a4);
// Defined by hand in pm_app_api.cpp.
int pm_text_width(int a0, const char* a1, int a2);
// Defined by hand in pm_app_api.cpp.
int pm_font_height(int a0, int a1);
// Defined by hand in pm_app_api.cpp.
void pm_text_color(int a0, uint16_t a1);
int pm_io_split_string_count(const char* a0, char a1);
int pm_io_split_string_get(const char* a0, char a1, int index, char* out, size_t out_size);
const char* pm_io_join_string(const char* const* a0_items, int a0_count, char a1);
const char* pm_io_remove_char(const char* a0, char a1);
int pm_io_string_to_int(const char* a0, int a1);
int pm_layout_eink_row_pitch(int a0);
size_t pm_layout_slice_that_fits(const char* a0, size_t a1, int a2, int a3);
const char* pm_layout_truncate_with_ellipsis(const char* a0, int a1, int a2, int a3);
int pm_layout_word_wrap_count(const char* a0, int a1, int a2);
int pm_layout_word_wrap_get(const char* a0, int a1, int a2, int index, char* out, size_t out_size);
void pm_sys_set_cpu_speed(int a0);
void pm_activity(void);
void pm_sys_deep_sleep(bool a0);
void pm_ui_draw_transport_icon(int a0, int a1, int a2, int a3, int a4);
void pm_ui_draw_scrollbar(int a0, int a1, int a2, int a3, int a4, int a5, int a6, bool a7, uint16_t a8, uint16_t a9);
void pm_ui_begin_eink_screen(bool a0);
void pm_ui_end_eink_screen(const char* a0, int a1);
void pm_ui_draw_list_item(int a0, int a1, const char* a2, int a3);
int pm_ui_draw_chip_text(int a0, int a1, const char* a2, int a3, int a4, bool a5, int a6, int a7, int a8, int a9);

// oled

void pm_oled_init(void);
void pm_oled_send(void);
void pm_oled_clear(void);
int pm_oled_width(void);
int pm_oled_height(void);
void pm_oled_box(int a0, int a1, int a2, int a3, bool a4);
void pm_oled_triangle(int a0, int a1, int a2, int a3, int a4, int a5, bool a6);
void pm_oled_oled_word(const char* a0, bool a1, bool a2, const char* a3);
void pm_oled_oled_line(const char* a0, int a1, bool a2, const char* a3, bool a4);
void pm_oled_sysmsg(const char* a0, int a1);
void pm_oled_oled_scroll(void);
void pm_oled_info_bar(void);
void pm_oled_set_power_save(bool a0);
bool pm_oled_power_save(void);

// sd

void pm_sd_set_mode(int a0);
int pm_sd_get_mode(void);
void pm_sd_save_file(void);
void pm_sd_write_metadata(const char* a0);
void pm_sd_load_file(bool a0);
void pm_sd_del_file(const char* a0);
void pm_sd_delete_metadata(const char* a0);
void pm_sd_ren_file(const char* a0, const char* a1);
void pm_sd_ren_metadata(const char* a0, const char* a1);
void pm_sd_copy_file(const char* a0, const char* a1);
void pm_sd_append_to_file(const char* a0, const char* a1);
void pm_sd_begin_io(void);
void pm_sd_end_io(void);
bool pm_sd_get_no_sd(void);
void pm_sd_set_no_sd(bool a0);
const char* pm_sd_get_working_file(void);
void pm_sd_set_working_file(const char* a0);
const char* pm_sd_get_editing_file(void);
void pm_sd_set_editing_file(const char* a0);
const char* pm_sd_get_files_list_index(int a0);
void pm_sd_set_files_list_index(int a0, const char* a1);
void pm_sd_list_dir(const char* a0);
void pm_sd_read_file(const char* a0);
const char* pm_sd_read_file_to_string(const char* a0);
void pm_sd_write_file(const char* a0, const char* a1);
void pm_sd_append_file(const char* a0, const char* a1);
void pm_sd_rename_file(const char* a0, const char* a1);
void pm_sd_delete_file(const char* a0);
bool pm_sd_read_binary_file(const char* a0, uint8_t* a1, size_t a2);
bool pm_sd_append_binary_file(const char* a0, const uint8_t* a1, size_t a2);
size_t pm_sd_get_file_size(const char* a0);
bool pm_sd_exists(const char* a0);
bool pm_sd_stat_is_dir(const char* a0);
bool pm_sd_stat_path(const char* a0, size_t* a1, bool* a2);
bool pm_sd_mkdir(const char* a0);
bool pm_sd_remove_file(const char* a0);
bool pm_sd_remove_tree(const char* a0);
bool pm_sd_rename_path(const char* a0, const char* a1);
bool pm_sd_copy_file_raw(const char* a0, const char* a1);
bool pm_sd_write_binary_file(const char* a0, const uint8_t* a1, size_t a2);
int pm_sd_stream_open_mode(const char* a0, char a1);
size_t pm_sd_stream_read(int a0, void* a1, size_t a2);
size_t pm_sd_stream_write(int a0, const void* a1, size_t a2);
bool pm_sd_stream_seek_from(int a0, long a1, int a2);
size_t pm_sd_stream_tell(int a0);
size_t pm_sd_stream_size(int a0);
bool pm_sd_stream_close_checked(int a0);
int pm_sd_dir_open(const char* a0);
bool pm_sd_dir_entry(int a0, char* a1, size_t a2, bool* a3);
void pm_sd_dir_close(void);
const char* pm_sd_sd_path(const char* a0);
bool pm_sd_sd_exists(const char* a0);
bool pm_sd_sd_mkdir(const char* a0);
bool pm_sd_sd_remove(const char* a0);
bool pm_sd_sd_rename(const char* a0, const char* a1);
bool pm_sd_sd_get_file_info(const char* a0, size_t* a1);

// touch

void pm_touch_update_scroll_from_touch(void);
bool pm_touch_update_scroll(int a0, unsigned long* a1, int a2);
int pm_touch_get_scroll_vector(void);
long pm_touch_get_dynamic_scroll(void);
void pm_touch_set_dynamic_scroll(long a0);
void pm_touch_set_prev_dynamic_scroll(long a0);
void pm_touch_set_last_touch(int a0);
void pm_touch_set_last_touch_time(unsigned long a0);
void pm_touch_reset_last_touch(void);
long pm_touch_get_prev_dynamic_scroll(void);
int pm_touch_get_last_touch(void);
unsigned long pm_touch_get_last_touch_time(void);
int pm_touch_get_diff(void);

// wifi

void pm_wifi_begin(void);
void pm_wifi_stop(void);
void pm_wifi_enable(void);
void pm_wifi_disable(void);
void pm_wifi_scan(void);
void pm_wifi_connect(const char* a0, const char* a1, bool a2);
void pm_wifi_disconnect(void);
void pm_wifi_reconnect(void);
int pm_wifi_get_state(void);
bool pm_wifi_is_connected(void);
bool pm_wifi_is_scanning(void);
const char* pm_wifi_get_status_message(void);
const char* pm_wifi_get_connected_ssid(void);
const char* pm_wifi_get_ip_address(void);
int pm_wifi_get_rssi(void);
const char* pm_wifi_get_last_error(void);
uint16_t pm_wifi_get_scan_result_count(void);
bool pm_wifi_has_saved_credentials(const char* a0);
bool pm_wifi_load_saved_credentials(const char* a0, char* a1, size_t a2);
void pm_wifi_clear_saved_credentials(const char* a0);
void pm_wifi_dispatch_events(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // PM_SDK_APP_H
