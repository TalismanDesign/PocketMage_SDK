---
type: guide
title: Internationalization
description: "Give an app translatable strings: the catalog an app ships, how it merges with the SDK base catalog, and resolving strings at runtime."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/guides/i18n/"
path: /guides/i18n/
updated: 2026-11-01
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-11-01T03:55:48.228Z"
---
---
title: "Internationalization"
description: "Give an app translatable strings: the catalog an app ships, how it merges with the SDK base catalog, and resolving strings at runtime."
---

# Internationalization

PocketMage translates every user-facing string through a single merged catalog. The SDK owns the base catalog of its own strings; an app ships its own catalog, and the two merge at build time into one enum and one set of string tables. This guide covers the app side, start to finish. The generator, the merge rules, and the validation it enforces are the reference material in [i18n catalogs](../i18n.md).

## Add a catalog to your app

Create one catalog directory with a subdirectory per language, named by the language code. `en` is required and defines the order of the strings your catalog contributes:

```text
myapp/src/i18n/
  en/pocketmage_i18n.po
  fr/pocketmage_i18n.po
  fr/pocketmage_i18n.aliases
```

Each string is a standard gettext entry. `msgctxt` is the string's `StringID` enum name, `msgid` is the English text, and `msgstr` is the translation:

```po
msgctxt "STR_MYAPP_HELLO"
msgid "Hello"
msgstr "Bonjour"
```

Keep your `msgctxt` names in your own namespace (for example `STR_MYAPP_*`). They must be globally unique across the merged catalogs; reusing a name the SDK already defines is a build error. A language missing from your catalog falls back to its English text, so an English-only catalog is valid.

## Wire it into the build

Point the generator at both your catalog and the SDK base catalog from your app's `platformio.ini`; the generator doubles as the build hook:

```ini
extra_scripts = pre:lib/PocketMage_SDK/pocketmage_i18n/tools/gen_i18n.py

custom_i18n_catalogs =
    lib/PocketMage_SDK/pocketmage_i18n/languages
    src/i18n
custom_i18n_out_dir = .pio/build/${PIOENV}/i18n
```

The base catalog must come first. The merge keeps base strings first and appends your catalog after, so your strings never shift the base enum values. The generated files are build artifacts and are never committed.

## Resolve strings at runtime

The runtime engine is the `I18n` singleton, exposed to apps through the `pm_*` i18n API. Set the active language, then fetch strings by their enum:

```cpp
pm_i18n_set_language_by_code("fr");
const char *hello = pm_i18n_get(STR_MYAPP_HELLO);
```

The indexed helpers resolve names from fixed ranges in the base catalog: `pm_i18n_month_name`, `pm_i18n_day_name`, `pm_i18n_app_name`, and `pm_i18n_kb_app_name`. The full runtime surface is on the [i18n (runtime) API page](../api/i18n-runtime.md).

Returned strings point into the shared rotating string pool, so copy a string before further SDK calls if you need to keep it. See the [string return lifetime note](../api/functions.md#function-index-string-return-lifetime).

## Add a language

Append the language code to the base catalog's `en` file `# Languages:` directive and add that language's directory to each catalog that translates it. Never reorder existing languages: the index is the value stored in NVS. The full rules, including aliases for command input and the validation the generator enforces, are in [i18n catalogs](../i18n.md).
