---
type: api
title: "Function index (pm_*)"
description: "Every pm_* wrapper an external app can call, grouped by module, with its exact C signature and a link to the page that documents it."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/api/functions/"
path: /api/functions/
updated: 2026-10-31
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-10-31T22:45:59.831Z"
---
---
title: "Function index (pm_*)"
description: "Every pm_* wrapper an external app can call, grouped by module, with its exact C signature and a link to the page that documents it."
---

# Function index

An external ELF app does not call the SDK's C++ classes directly. It calls a flat C ABI, the `pm_*` functions, declared in the generated header `pm_sdk_app.h` and pulled in through `pm_app_api.h`. This page is the complete index of that surface: every wrapper, its exact signature, and the module page that documents its behavior.

```cpp
#include <pm_app_api.h>
```

The signatures here are generated verbatim from `pm_sdk_app.h`. Parameter names are positional (`a0`, `a1`, ...) in the generated header; the module pages give them meaning. See [the projection model](index.md#api-reference-the-c-abi-pm) for how C++ methods become these C functions, and [symbols.md](../symbols.md) for how they are gated at load time.

Enums are plain integers in the app: `pm_target`, `pm_style`, `pm_lang`, `pm_refresh`, and `pm_jingle`, with values generated in the SDK's own declaration order (`PM_TARGET_*`, `PM_STYLE_*`, and so on). `PM_APP_API_ABI` tracks incompatible changes to this surface; an app compares it against `pm_app_abi()` at startup.

## Version and host

| Function                              | Returns        |
| ------------------------------------- | -------------- |
| `pm_app_abi(void)`                    | `uint32_t`     |
| `pm_host_sdk_version(void)`           | `const char*`  |
| `delay(unsigned long ms)`             | `void`         |

Documented on [System](sys.md).

## Buzzer

Documented on [Buzzer](bz.md).

```c
bool pm_bz_begin(int a0);
void pm_bz_end();
void pm_bz_play_jingle(int a0);
```

## Clock

Documented on [Clock](clock.md).

```c
bool pm_clock_begin();
void pm_clock_set_time_from_string(const char* a0);
bool pm_clock_valid();
```

## E-Ink

Documented on [E-Ink](eink.md).

```c
void pm_eink_refresh();
void pm_eink_set_fast_full_refresh(bool a0);
void pm_eink_status_bar(const char* a0, bool a1);
void pm_eink_draw_status_bar(const char* a0);
void pm_eink_reset_display(bool a0, uint16_t a1);
int pm_eink_count_lines(const char* a0, size_t a1);
uint8_t pm_eink_get_font_height();
int pm_eink_max_lines();
uint16_t pm_eink_get_eink_text_width(const char* a0);
uint8_t pm_eink_get_line_spacing();
void pm_eink_force_slow_full_update(bool a0);
```

## Fonts and text

Documented on [Fonts & text](font.md).

```c
int pm_font_engine_char_width(int a0, uint16_t a1, int a2);
int pm_font_engine_font_ascent(int a0, int a1);
int pm_font_engine_font_descent(int a0, int a1);
int pm_font_engine_font_height_txt(uint8_t a0, uint8_t a1, uint8_t a2);
int pm_text_width(int a0, const char* a1, int a2);
int pm_font_height(int a0, int a1);
void pm_text_color(int a0, uint16_t a1);
```

## Internationalization (runtime)

Documented on [i18n (runtime)](i18n-runtime.md).

```c
void pm_i18n_set_language(int a0);
bool pm_i18n_set_language_by_code(const char* a0);
int pm_i18n_language();
int pm_i18n_language_count();
const char* pm_i18n_code();
const char* pm_i18n_code_at(int a0);
const char* pm_i18n_native_name();
const char* pm_i18n_native_name_at(int a0);
const char* pm_i18n_get(int a0);
const char* pm_i18n_month_name(int a0);
const char* pm_i18n_day_name(int a0);
const char* pm_i18n_app_name(int a0);
const char* pm_i18n_kb_app_name(int a0);
const char* pm_i18n_normalize_command(const char* a0);
```

## Keyboard

Documented on [Keyboard](kb.md).

```c
int pm_kb_state();
void pm_kb_toggle_shift();
void pm_kb_toggle_fn();
bool pm_kb_accept_key();
char pm_kb_read();
void pm_kb_check_usbkb();
void pm_kb_flush();
```

## Layout

Documented on [Layout](layout.md).

```c
int pm_layout_eink_row_pitch(int a0);
size_t pm_layout_slice_that_fits(const char* a0, size_t a1, int a2, int a3);
const char* pm_layout_truncate_with_ellipsis(const char* a0, int a1, int a2, int a3);
int pm_layout_word_wrap_count(const char* a0, int a1, int a2);
int pm_layout_word_wrap_get(const char* a0, int a1, int a2, int index, char* out, size_t out_size);
```

## OLED

Documented on [OLED](oled.md).

```c
void pm_oled_oled_word(const char* a0, bool a1, bool a2, const char* a3);
void pm_oled_oled_line(const char* a0, int a1, bool a2, const char* a3, bool a4);
void pm_oled_sysmsg(const char* a0, int a1);
void pm_oled_oled_scroll();
void pm_oled_info_bar();
void pm_oled_set_power_save(bool a0);
bool pm_oled_power_save();
```

## SD card

Documented on [SD card](sd.md).

```c
int pm_sd_get_mode();
void pm_sd_save_file();
void pm_sd_write_metadata(const char* a0);
void pm_sd_load_file(bool a0);
void pm_sd_del_file(const char* a0);
void pm_sd_delete_metadata(const char* a0);
void pm_sd_ren_file(const char* a0, const char* a1);
void pm_sd_ren_metadata(const char* a0, const char* a1);
void pm_sd_copy_file(const char* a0, const char* a1);
void pm_sd_append_to_file(const char* a0, const char* a1);
void pm_sd_begin_io();
void pm_sd_end_io();
bool pm_sd_get_no_sd();
const char* pm_sd_get_working_file();
const char* pm_sd_get_editing_file();
const char* pm_sd_get_files_list_index(int a0);
void pm_sd_list_dir(const char* a1);
void pm_sd_read_file(const char* a1);
const char* pm_sd_read_file_to_string(const char* a1);
void pm_sd_write_file(const char* a1, const char* a2);
void pm_sd_append_file(const char* a1, const char* a2);
void pm_sd_rename_file(const char* a1, const char* a2);
void pm_sd_delete_file(const char* a1);
bool pm_sd_read_binary_file(const char* a0, uint8_t* a1, size_t a2);
size_t pm_sd_get_file_size(const char* a0);
```

## Text utilities

Documented on [Text utilities](io.md).

```c
int pm_io_split_string_count(const char* a0, char a1);
int pm_io_split_string_get(const char* a0, char a1, int index, char* out, size_t out_size);
const char* pm_io_join_string(const char* const* a0_items, int a0_count, char a1);
const char* pm_io_remove_char(const char* a0, char a1);
int pm_io_string_to_int(const char* a0, int a1);
```

## Touch

Documented on [Touch](touch.md).

```c
void pm_touch_update_scroll_from_touch();
bool pm_touch_update_scroll(int a0, unsigned long* a1, int a2);
int pm_touch_get_scroll_vector();
long pm_touch_get_dynamic_scroll();
long pm_touch_get_prev_dynamic_scroll();
int pm_touch_get_last_touch();
unsigned long pm_touch_get_last_touch_time();
int pm_touch_get_diff();
```

## UI helpers

Documented on [UI helpers](ui.md).

```c
void pm_ui_draw_scrollbar(int a0, int a1, int a2, int a3, int a4, int a5, int a6, bool a7, uint16_t a8, uint16_t a9);
void pm_ui_begin_eink_screen(bool a0);
void pm_ui_end_eink_screen(const char* a0, int a1);
void pm_ui_draw_list_item(int a0, int a1, const char* a2, int a3);
int pm_ui_draw_chip_text(int a0, int a1, const char* a2, int a3, int a4, bool a5, int a6, int a7, int a8, int a9);
```

## WiFi

Documented on [WiFi](wifi.md).

```c
void pm_wifi_begin();
void pm_wifi_stop();
void pm_wifi_enable();
void pm_wifi_disable();
void pm_wifi_scan();
void pm_wifi_connect(const char* a0, const char* a1, bool a2);
void pm_wifi_disconnect();
void pm_wifi_reconnect();
int pm_wifi_get_state();
bool pm_wifi_is_connected();
bool pm_wifi_is_scanning();
const char* pm_wifi_get_status_message();
const char* pm_wifi_get_connected_ssid();
const char* pm_wifi_get_ip_address();
int pm_wifi_get_rssi();
const char* pm_wifi_get_last_error();
uint16_t pm_wifi_get_scan_result_count();
bool pm_wifi_has_saved_credentials(const char* a0);
bool pm_wifi_load_saved_credentials(const char* a0, char* a1, size_t a2);
void pm_wifi_clear_saved_credentials(const char* a0);
void pm_wifi_dispatch_events();
```

## String return lifetime

Every `const char*` return points into a small rotating pool shared by all string-returning wrappers. The pool holds four slots; the fifth string-returning call overwrites the first slot's buffer. Copy a returned string before making further SDK calls if you need to keep it. The buffer is 256 bytes; longer values are truncated. Functions that can produce arbitrary length, such as `pm_layout_word_wrap_get` and `pm_io_split_string_get`, write into a caller-owned buffer instead and return a length or `-1`.
