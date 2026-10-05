---
type: api
title: "Text utilities (pocketmage_io)"
description: "Small string helpers apps use for command parsing and data marshaling."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/api/io/"
path: /api/io/
updated: 2026-10-05
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-10-05T06:26:55.149Z"
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
