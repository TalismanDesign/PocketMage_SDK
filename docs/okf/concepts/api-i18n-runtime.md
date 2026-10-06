---
type: api
title: "Internationalization (i18n)"
description: "Lang/StringID tables, the TR() lookup, runtime language switching, and command aliases."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/api/i18n-runtime/"
path: /api/i18n-runtime/
updated: 2026-10-06
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-10-06T07:52:17.221Z"
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

The string tables are generated (see `pocketmage_i18n/README`): add a `.po` catalog and re-run the generator, and the `Lang` enum, `StringID`-ordered table, and string block for `appName`/`kbAppName`/months/days all follow. Fallback rules: a missing id in a non-English table falls back to the
