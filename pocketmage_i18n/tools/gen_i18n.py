#!/usr/bin/env python3
"""Merge pocketmage_i18n .po catalogs into the generated enums and tables.

The first --catalog is the base; it owns the language list (the
'# Languages:' header in its en file) and the leading enum order. Later
catalogs append after it, so an app can add strings without touching the SDK.

Also runs as a PlatformIO pre-build hook:

    extra_scripts = pre:lib/PocketMage_SDK/pocketmage_i18n/tools/gen_i18n.py

and as a standalone tool:

    gen_i18n.py --catalog <sdk> --catalog <app> --out-dir <build dir>
"""

import argparse
import os
import re
import sys
import unicodedata

GEN_H_NAME = "pocketmage_i18n_gen.h"
GEN_CPP_NAME = "pocketmage_i18n_gen.cpp"

CATALOGS_ENV = "POCKETMAGE_I18N_CATALOGS"
OUT_DIR_ENV = "POCKETMAGE_I18N_OUT_DIR"

# appName()/dayName()/monthName()/kbAppName() index these; keep each one a
# contiguous block, in this order.
HELPER_RANGES = [
    ("STR_KB_APP_CANCEL", "STR_KB_APP_LOADER"),
    ("STR_GRID_TXT", "STR_GRID_LOADER"),
    ("STR_DAY_SUNDAY", "STR_DAY_SATURDAY"),
    ("STR_MONTH_JAN", "STR_MONTH_ERR"),
]

ID_RE = re.compile(r"^[A-Z][A-Z0-9_]*$")

ENGLISH_NAMES = {"en": "English", "fr": "French", "es": "Spanish", "de": "German"}
NATIVE_NAMES = {"en": "English", "fr": "Français", "es": "Español", "de": "Deutsch"}


def error(msg):
    print(f"[gen_i18n] ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


def sdk_i18n_dir():
    """The pocketmage_i18n directory, or None.

    SCons does not define __file__ for extra scripts, so this is only usable
    for standalone runs; as a build hook the paths come from project options.
    """
    try:
        return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    except NameError:
        return None


def default_catalog_dir():
    i18n_dir = sdk_i18n_dir()
    if i18n_dir is None:
        error(f"no catalog configured; set custom_i18n_catalogs or {CATALOGS_ENV}")
    # 'languages' is the older name, kept so existing checkouts still build.
    for name in ("catalog", "languages"):
        candidate = os.path.join(i18n_dir, name)
        if os.path.isdir(os.path.join(candidate, "en")):
            return candidate
    return os.path.join(i18n_dir, "languages")


def default_out_dir():
    i18n_dir = sdk_i18n_dir()
    if i18n_dir is None:
        error(f"no output dir configured; set custom_i18n_out_dir or {OUT_DIR_ENV}")
    return i18n_dir


def parse_po(path):
    """Return (languages, entries) for a .po file.

    entries hold {ctxt, msgid, msgstr, comments}; multi-line quoted values are
    concatenated and unescaped.
    """
    with open(path, encoding="utf-8") as fh:
        text = fh.read()

    entries = []
    cur = None
    pending_comments = []
    languages = None
    last_field = None
    header_seen = False

    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith("#"):
            comment = line.lstrip("# ").strip()
            if not header_seen and comment.startswith("Languages:"):
                languages = [x.strip() for x in
                             comment[len("Languages:"):].split() if x.strip()]
            pending_comments.append(comment)
            continue
        header_seen = True
        if line.startswith("msgctxt "):
            cur = {"ctxt": parse_quoted(line[len("msgctxt "):]),
                   "msgid": "", "msgstr": "", "comments": list(pending_comments)}
            pending_comments = []
            last_field = "msgid"
            entries.append(cur)
        elif line.startswith("msgid "):
            if cur is None:
                cur = {"ctxt": None, "msgid": "", "msgstr": "",
                       "comments": list(pending_comments)}
                entries.append(cur)
            cur["msgid"] += parse_quoted(line[len("msgid "):])
            last_field = "msgid"
        elif line.startswith("msgstr "):
            if cur is None:
                cur = {"ctxt": None, "msgid": "", "msgstr": "",
                       "comments": list(pending_comments)}
                entries.append(cur)
            cur["msgstr"] += parse_quoted(line[len("msgstr "):])
            last_field = "msgstr"
        elif line.startswith('"') and cur is not None:
            field = "msgstr" if last_field == "msgstr" else "msgid"
            cur[field] += parse_quoted(line)

    entries = [e for e in entries if e["ctxt"] is not None]
    return languages, entries


def parse_quoted(s):
    s = s.strip()
    if s.startswith('"') and s.endswith('"'):
        s = s[1:-1]
    out = []
    i = 0
    while i < len(s):
        ch = s[i]
        if ch == "\\" and i + 1 < len(s):
            nxt = s[i + 1]
            if nxt == "n":
                out.append("\n")
            elif nxt == "t":
                out.append("\t")
            elif nxt == '"':
                out.append('"')
            elif nxt == "\\":
                out.append("\\")
            else:
                out.append(nxt)
            i += 2
        else:
            out.append(ch)
            i += 1
    return "".join(out)


def fold(s):
    # NFD splits accents from the base letter, so dropping the combining marks
    # turns 'é' into 'e' for alias lookup.
    return "".join(c for c in unicodedata.normalize("NFD", s.lower())
                   if unicodedata.category(c) != "Mn")


def parse_aliases(path):
    pairs = []
    with open(path, encoding="utf-8") as fh:
        for raw in fh:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if "->" not in line:
                error(f"{os.path.basename(path)}: bad alias line: {line!r}")
            left, right = line.split("->", 1)
            left, right = left.strip(), right.strip()
            if not left or not right:
                error(f"{os.path.basename(path)}: empty alias: {line!r}")
            folded = fold(left)
            if not folded:
                error(f"{os.path.basename(path)}: alias folds to empty: {left!r}")
            pairs.append((folded, right))
    folded_dupes = [k for k in dict.fromkeys(p[0] for p in pairs)
                    if sum(1 for p in pairs if p[0] == k) > 1]
    if folded_dupes:
        error(f"{os.path.basename(path)}: duplicate folded aliases: {folded_dupes}")
    return pairs


def load_catalog(catalog_dir, languages):
    """Load one catalog directory.

    languages is None for the base catalog (taken from its en header) and the
    base's list otherwise. by_lang maps a code to its entries, or None when
    the catalog omits that language.
    """
    en_path = os.path.join(catalog_dir, "en", "pocketmage_i18n.po")
    if not os.path.exists(en_path):
        error(f"{en_path}: missing catalog")

    header_langs, en_entries = parse_po(en_path)

    if languages is None:
        if not header_langs:
            error(f"{en_path}: header is missing the '# Languages:' directive")
        if header_langs[0] != "en":
            error(f"{en_path}: first language must be 'en', got {header_langs[0]!r}")
        languages = header_langs

    ctxts = [e["ctxt"] for e in en_entries]
    if any(c is None for c in ctxts):
        error(f"{en_path}: entry without msgctxt")
    for c in ctxts:
        if not ID_RE.match(c):
            error(f"{en_path}: bad StringID name {c!r}")
    dupes = sorted({c for c in ctxts if ctxts.count(c) > 1})
    if dupes:
        error(f"{en_path}: duplicate msgctxt: {dupes}")

    by_lang = {"en": en_entries}
    for lang in languages[1:]:
        lang_path = os.path.join(catalog_dir, lang, "pocketmage_i18n.po")
        if not os.path.exists(lang_path):
            by_lang[lang] = None
            continue
        _, entries = parse_po(lang_path)
        if [e["ctxt"] for e in entries] != ctxts:
            error(f"{lang_path}: msgctxt set/order must match "
                  f"{os.path.relpath(en_path)} exactly")
        for e, en in zip(entries, en_entries):
            if e["msgid"] != en["msgid"]:
                error(f"{lang_path}: msgid mismatch for {e['ctxt']}: "
                      f"{en['msgid']!r} != {e['msgid']!r}")
        by_lang[lang] = entries

    aliases = {}
    for lang in languages[1:]:
        alias_path = os.path.join(catalog_dir, lang, "pocketmage_i18n.aliases")
        aliases[lang] = parse_aliases(alias_path) if os.path.exists(alias_path) else []

    return languages, by_lang, aliases


def merge_catalogs(catalog_dirs):
    """Merge catalogs in order; returns (languages, en_entries, by_lang, aliases)."""
    languages = None
    en_all = []
    by_lang = {}
    aliases = {}
    origin = {}

    for catalog_dir in catalog_dirs:
        languages, cat_by_lang, cat_aliases = load_catalog(catalog_dir, languages)
        en_entries = cat_by_lang["en"]

        for e in en_entries:
            ctxt = e["ctxt"]
            if ctxt in origin:
                error(f"{catalog_dir}: StringID {ctxt!r} already defined by {origin[ctxt]}")
            origin[ctxt] = catalog_dir

        en_all.extend(en_entries)
        for lang in languages:
            by_lang.setdefault(lang, []).extend(cat_by_lang.get(lang) or en_entries)
        for lang in languages[1:]:
            aliases.setdefault(lang, []).extend(cat_aliases.get(lang, []))

    for lang, pairs in aliases.items():
        seen = set()
        for folded, _ in pairs:
            if folded in seen:
                error(f"duplicate folded alias across catalogs for {lang!r}: {folded!r}")
            seen.add(folded)

    return languages, en_all, by_lang, aliases


def validate_ranges(entries):
    """Check the indexed helper ranges are present, ordered and disjoint.

    Enum values equal their index, so the helpers only work if each range
    stays contiguous and no two ranges interleave.
    """
    positions = {e["ctxt"]: i for i, e in enumerate(entries)}

    boundaries = []
    for head, tail in HELPER_RANGES:
        if head not in positions:
            error(f"helper range head {head} not found in catalog")
        if tail not in positions:
            error(f"helper range tail {tail} not found in catalog")
        if positions[head] > positions[tail]:
            error(f"helper range {head}..{tail} is inverted")
        boundaries.extend([positions[head], positions[tail]])

    if boundaries != sorted(boundaries):
        error("helper ranges overlap or are out of order; keep "
              + ", ".join(f"{h}..{t}" for h, t in HELPER_RANGES)
              + " as separate contiguous blocks in that order")


def esc(s):
    out = []
    for ch in s:
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        else:
            out.append(ch)
    return "".join(out)


def emit_header(languages, entries):
    ids = [e["ctxt"] for e in entries]
    pinned_heads = {h for h, _ in HELPER_RANGES}
    lines = [
        "#pragma once",
        "#include <stdint.h>",
        "",
        "// Generated by pocketmage_i18n/tools/gen_i18n.py from the .po catalogs.",
        "// Do not edit.",
        "",
        "// Index is what NVS stores. Only append languages, never reorder.",
        "enum Lang : uint8_t {",
    ]
    for i, lang in enumerate(languages):
        lines.append(f"  {ENGLISH_NAMES.get(lang, lang)} = {i},")
    lines.append("  _LANG_COUNT")
    lines.append("};")
    lines.append("")
    lines.append("// Order matches the merged catalogs. The helper heads are pinned")
    lines.append("// so the indexed lookups survive strings added above them.")
    lines.append("enum StringID : uint16_t {")
    for i, ident in enumerate(ids):
        lines.append(f"  {ident} = {i}," if ident in pinned_heads else f"  {ident},")
    lines.append(f"  _STR_COUNT = {len(ids)}")
    lines.append("};")
    lines.append("")
    lines.append(f"extern const char* const kLanguageCodes[{len(languages)}];")
    lines.append(f"extern const char* const kLanguageNames[{len(languages)}];")
    lines.append(f"extern const char* const* const kStrings[{len(languages)}];")
    lines.append(f"extern const char* const* const kCommandAliases[{len(languages)}];")
    lines.append(f"extern const uint8_t kCommandAliasCounts[{len(languages)}];")
    lines.append("")
    return "\n".join(lines)


def emit_cpp(languages, by_lang, aliases):
    ids = [e["ctxt"] for e in by_lang["en"]]
    lines = [
        f'#include "{GEN_H_NAME}"',
        "",
        f"const char* const kLanguageCodes[{len(languages)}] = "
        + "{ " + ", ".join(f'"{l}"' for l in languages) + " };",
        "",
        f"const char* const kLanguageNames[{len(languages)}] = {{",
        "  " + ", ".join(f'"{NATIVE_NAMES.get(l, l)}"' for l in languages) + "};",
        "",
    ]

    table_arrays = []
    for lang in languages:
        lines.append(f"static const char* const kStrings{lang.capitalize()}[{len(ids)}] = {{")
        items = [f'"{esc(e["msgstr"] or en["msgstr"])}"'
                 for e, en in zip(by_lang[lang], by_lang["en"])]
        lines.append("  " + ",\n  ".join(items))
        lines.append("};")
        lines.append("")
        table_arrays.append(f"kStrings{lang.capitalize()}")

    lines.append(f"const char* const* const kStrings[{len(languages)}] = {{")
    lines.append("  " + ", ".join(table_arrays))
    lines.append("};")
    lines.append("")

    alias_arrays = ["nullptr"]
    counts = ["0"]
    for lang in languages:
        if lang == "en":
            continue
        pairs = aliases.get(lang, [])
        lines.append(f"static const char* const kAliases{lang.capitalize()}[] = {{")
        lines.append("  " + ",\n  ".join(f'"{esc(a)}"' for pair in pairs for a in pair))
        lines.append("};")
        lines.append("")
        alias_arrays.append(f"kAliases{lang.capitalize()}")
        counts.append(str(len(pairs)))

    lines.append(f"const char* const* const kCommandAliases[{len(languages)}] = "
                 + "{ " + ", ".join(alias_arrays) + " };")
    lines.append(f"const uint8_t kCommandAliasCounts[{len(languages)}] = "
                 + "{ " + ", ".join(counts) + " };")
    lines.append("")
    return "\n".join(lines)


def generate(catalog_dirs, out_dir):
    """Merge the catalogs and write the generated tables into out_dir."""
    languages, en_entries, by_lang, aliases = merge_catalogs(catalog_dirs)
    validate_ranges(en_entries)

    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, GEN_H_NAME), "w",
              encoding="utf-8", newline="\n") as fh:
        fh.write(emit_header(languages, en_entries))
    with open(os.path.join(out_dir, GEN_CPP_NAME), "w",
              encoding="utf-8", newline="\n") as fh:
        fh.write(emit_cpp(languages, by_lang, aliases))

    print(f"[gen_i18n] {len(en_entries)} strings x {len(languages)} languages "
          f"({', '.join(languages)}), catalogs: "
          + ", ".join(os.path.basename(os.path.normpath(c)) for c in catalog_dirs)
          + ", aliases: "
          + ", ".join(f"{l}={len(aliases.get(l, []))}" for l in languages[1:]))


def _as_list(value):
    if value is None:
        return []
    items = value if isinstance(value, (list, tuple)) else str(value).replace(",", " ").split()
    return [os.path.expandvars(str(item).strip()) for item in items if str(item).strip()]


def resolve_settings(env=None):
    """Return (catalog_dirs, out_dir) from project options, env vars, or defaults.

    Relative paths are resolved against the PlatformIO project directory, so
    an app can configure them without hardcoding absolute paths.
    """
    catalogs = []
    out_dir = None
    project_dir = None

    if env is not None:
        project_dir = env.subst("$PROJECT_DIR")
        catalogs = _as_list(env.GetProjectOption("custom_i18n_catalogs", None))
        out_dir = env.GetProjectOption("custom_i18n_out_dir", None)

    if not catalogs:
        catalogs = _as_list(os.environ.get(CATALOGS_ENV))
    if not out_dir:
        out_dir = os.environ.get(OUT_DIR_ENV)
    if not catalogs:
        catalogs = [default_catalog_dir()]
    if not out_dir:
        out_dir = default_out_dir()

    base = project_dir or os.getcwd()
    catalogs = [c if os.path.isabs(c) else os.path.join(base, c) for c in catalogs]
    if not os.path.isabs(out_dir):
        out_dir = os.path.join(base, out_dir)
    return catalogs, out_dir


def is_stale(catalog_dirs, out_dir):
    outputs = [os.path.join(out_dir, GEN_H_NAME), os.path.join(out_dir, GEN_CPP_NAME)]
    if not all(os.path.exists(path) for path in outputs):
        return True
    newest_source = 0.0
    for catalog_dir in catalog_dirs:
        for root, _dirs, files in os.walk(catalog_dir):
            for name in files:
                if name.endswith((".po", ".aliases")):
                    newest_source = max(newest_source,
                                        os.path.getmtime(os.path.join(root, name)))
    return newest_source > min(os.path.getmtime(path) for path in outputs)


def run_hook(env):
    catalogs, out_dir = resolve_settings(env)
    if is_stale(catalogs, out_dir):
        generate(catalogs, out_dir)
        print(f"[i18n] regenerated -> {out_dir}")
    else:
        print(f"[i18n] up to date -> {out_dir}")


def run_hook_if_platformio():
    """Run as a pre-build hook when loaded by SCons, else return False."""
    try:
        Import("env")  # noqa: F821  (SCons injects this into the script globals)
    except NameError:
        return False
    run_hook(globals().get("env"))
    return True


def cli(argv=None):
    parser = argparse.ArgumentParser(
        description="Merge and generate pocketmage_i18n catalogs.")
    parser.add_argument("--catalog", action="append", metavar="DIR",
                        help="catalog directory; repeatable, first is the base")
    parser.add_argument("--out-dir", metavar="DIR",
                        help="where to write the generated tables")
    parser.add_argument("--force", action="store_true",
                        help="regenerate even when the output is up to date")
    args = parser.parse_args(argv)

    catalogs = args.catalog or [default_catalog_dir()]
    out_dir = args.out_dir or default_out_dir()
    if args.force or is_stale(catalogs, out_dir):
        generate(catalogs, out_dir)
        print(f"[i18n] regenerated -> {out_dir}")
    else:
        print(f"[i18n] up to date -> {out_dir}")


if not run_hook_if_platformio() and __name__ == "__main__":
    cli()
