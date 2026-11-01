---
type: api
title: "Internationalization (i18n)"
description: "Lang/StringID tables, the TR() lookup, runtime language switching, and command aliases."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/api/i18n-runtime/"
path: /api/i18n-runtime/
updated: 2026-11-01
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-11-01T01:09:38.803Z"
---
---
title: "Internationalization (i18n)"
description: "Lang/StringID tables, the TR() lookup, runtime language switching, and command aliases."
---

# Internationalization API

The i18n module turns every user-visible string into a `StringID`, and a per-language table supplies the active-language text. Apps get translated strings with `TR(STR_X)` and can switch the active language at runtime.

## Access

```cpp
#include <pocketmage.h>
```

`I18n` is a static-only class; there is no instance. The `Lang` and `StringID` enums are **generated** from the catalogs in `pocketmage_i18n/` into `pocketmage_i18n_gen.h` (that header drives the tables, not the other way round). `StringID` ids look like `STR_INVALID`, `STR_TODAY`, `STR_SETTINGS`; `Lang` has one member per shipped language.

## Lookup

```cpp
static const char* get(StringID id);
#define TR(id) I18n::get(id)
```

`TR(STR_X)` returns the active language's text for the id, pointing into the table storage (do not free it). The canonical reference strings live in the English table.

Indexed blocks make whole tables selectable:

```cpp
static const char* monthName(int month);    // 1..12, else the ERR entry
static const char* dayName(int idx);        // 0..6, Sunday-first
static const char* appName(int idx);        // home grid label, 0..10
static const char* kbAppName(int idx);      // app-switcher badge, 0..11
```

## Language switching

```cpp
static void setLanguage(Lang lang);
static bool setLanguageByCode(const char* code);
static Lang language();
static int  languageCount();
static const char* code();              // active two-letter code ("en")
static const char* code(int idx);
static const char* nativeName();        // active native name ("Français")
static const char* nativeName(int idx);
```

- `setLanguage` switches the active table; out-of-range values clamp to English.
- `setLanguageByCode("fr")` switches by two-letter code and returns false (leaving the current language untouched) for an unknown code.
- `languageCount()` is the number of shipped languages for building a language picker; `code(idx)`/`nativeName(idx)` back its rows in catalog order.

The OS persists the choice to NVS ([configuration](configuration.md)); on setup the app applies it, and the font table is re-applied because displayed text may now use different accents ([font](font.md)).

## Command aliases

```cpp
static String normalizeCommand(const String& raw);
```

The command parser's front door. It:

1. Folds the input (UTF-8 diacritics to ASCII, lowercased). Curly quotes, dashes, and truncated sequences are dropped.
2. Resolves the first word (or the whole line for multi-word aliases such as the easter eggs) against the alias catalog to the canonical English verb.
3. Preserves any argument text (filenames, etc.) byte-for-byte.

So both `"réglages"` and `"reglages"` resolve to settings, while file arguments keep their exact bytes. Unmatched input returns unchanged.

## Adding a language

The string tables are generated (see `pocketmage_i18n/README`): add a `.po` catalog and re-run the generator, and the `Lang` enum, `StringID`-ordered table, and string block for `appName`/`kbAppName`/months/days all follow. Fallback rules: a missing (empty `msgstr`) id in a non-English table falls back to the English string at generation time, so every `StringID` always resolves to a non-empty string. An out-of-range language index clamps to English; an out-of-range `StringID` returns the empty string.

## C ABI (`pm_*`)

External ELF apps call these C wrappers rather than the C++ class above; they are generated from it by `gen_app_api.py` and declared in `pm_sdk_app.h` (pulled in via `pm_app_api.h`). See the [function index](functions.md) and the [projection model](index.md#api-reference-the-c-abi-pm) for the general rules.

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

These project the static `I18n` methods. Every `const char*` return points into the rotating string pool; copy it before the next SDK call if it must outlive four further string-returning calls.

- `pm_i18n_set_language(a0)` is `setLanguage(lang)`; `a0` is a `pm_lang` value as int, out-of-range clamping to English.
- `pm_i18n_set_language_by_code(a0)` is `setLanguageByCode(code)`; `a0` is a two-letter code. Returns false and leaves the language unchanged for an unknown code.
- `pm_i18n_language()` is `language()`. Returns the active `pm_lang` as int.
- `pm_i18n_language_count()` is `languageCount()`.
- `pm_i18n_code()` / `pm_i18n_code_at(a0)` are `code()` / `code(idx)`; `a0` is the catalog index.
- `pm_i18n_native_name()` / `pm_i18n_native_name_at(a0)` are `nativeName()` / `nativeName(idx)`.
- `pm_i18n_get(a0)` is `get(id)`; `a0` is a `StringID` value as int (for example `STR_INVALID`). This is the `TR()` lookup.
- `pm_i18n_month_name(a0)` is `monthName(month)`; `a0` is 1..12.
- `pm_i18n_day_name(a0)` is `dayName(idx)`; `a0` is 0..6, Sunday-first.
- `pm_i18n_app_name(a0)` is `appName(idx)`; `a0` is the home-grid index 0..10.
- `pm_i18n_kb_app_name(a0)` is `kbAppName(idx)`; `a0` is the app-switcher index 0..11.
- `pm_i18n_normalize_command(a0)` is `normalizeCommand(raw)`; `a0` is the raw command line. Returns the canonical English verb with argument bytes preserved.
