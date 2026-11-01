---
type: api
title: "Text utilities (pocketmage_io)"
description: "Small string helpers apps use for command parsing and data marshaling."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/api/io/"
path: /api/io/
updated: 2026-11-01
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-11-01T01:09:38.804Z"
---
---
title: "Text utilities (pocketmage_io)"
description: "Small string helpers apps use for command parsing and data marshaling."
---

# Text utilities API

`pocketmage_io` is a set of small string helpers for parsing and formatting. They operate on `Arduino String` and `std::vector<String>`.

## Reference

```cpp
std::vector<String> splitString(const String& str, char delimiter);
String joinString(const std::vector<String>& parts, char delimiter);
String removeChar(String str, char character);
int stringToInt(const String& str, int defaultVal = -1);
```

- **`splitString`** splits on `delimiter`, keeping empty tokens: `"a||b"` with `'|'` becomes `{"a", "", "b"}`. Use it for command or CSV-style input where empty fields matter.
- **`joinString`** is the inverse: joins `parts` with `delimiter` between pairs.
- **`removeChar`** strips every occurrence of `character` and returns the new string (a copy; the argument is taken by value).
- **`stringToInt`** parses a decimal integer and returns `defaultVal` when the string is empty, contains non-digits, or fails to parse. This is the safe integer reader for user input (never `atoi` on untrusted text).

## Example

```cpp
#include <pocketmage.h>

int parseDuration(String raw) {
  // "play 15" -> 15, anything else -> -1
  std::vector<String> parts = splitString(raw, ' ');
  if (parts.size() != 2) return -1;
  return stringToInt(parts[1], -1);
}
```

## C ABI (`pm_*`)

External ELF apps call these C wrappers rather than the C++ functions above; they are generated from them by `gen_app_api.py` and declared in `pm_sdk_app.h` (pulled in via `pm_app_api.h`). See the [function index](functions.md) and the [projection model](index.md#api-reference-the-c-abi-pm) for the general rules.

```c
int pm_io_split_string_count(const char* a0, char a1);
int pm_io_split_string_get(const char* a0, char a1, int index, char* out, size_t out_size);
const char* pm_io_join_string(const char* const* a0_items, int a0_count, char a1);
const char* pm_io_remove_char(const char* a0, char a1);
int pm_io_string_to_int(const char* a0, int a1);
```

- `pm_io_split_string_count` / `pm_io_split_string_get` project `splitString()`, which returns `std::vector<String>`. Call `pm_io_split_string_count(a0, a1)` to get the token count, then `pm_io_split_string_get(a0, a1, index, out, out_size)` per index to copy a token into the caller buffer `out` (at most `out_size` bytes). `a0` is the input, `a1` is the delimiter. `_get` returns the token length or -1 for a bad index. These write into the caller buffer, not the rotating pool, so an arbitrary-length token is not truncated to 256 bytes.
- `pm_io_join_string(a0_items, a0_count, a1)` is `joinString(parts, delimiter)`; the `std::vector<String>` becomes a `const char* const*` array `a0_items` of length `a0_count`, and `a1` is the delimiter. Returns a pointer into the rotating string pool.
- `pm_io_remove_char(a0, a1)` is `removeChar(str, character)`; strips every `a1` from `a0`. Returns a pointer into the rotating string pool.
- `pm_io_string_to_int(a0, a1)` is `stringToInt(str, defaultVal)`; `a1` is the fallback returned when `a0` is empty or not a decimal integer.
