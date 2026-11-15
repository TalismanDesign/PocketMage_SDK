#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import surface_measure as sm

ACCESSORS = {
    "PocketmageSD": ("object", "PM_SD()"),
    "PocketMageWifi": ("object", "P_WIFI"),
    "PocketmageEink": ("object", "EINK()"),
    "I18n": ("static", "I18n"),
    "PocketmageCLOCK": ("object", "CLOCK()"),
    "PocketmageKB": ("object", "KB()"),
    "FontEngine": ("static", "FontEngine"),
    "PocketmageOled": ("object", "OLED()"),
    "PocketmageBZ": ("object", "BZ()"),
    "PocketmageTOUCH": ("object", "TOUCH()"),
    "PocketMageAudio": ("object", "P_AUDIO"),
    "PocketMageImage": ("object", "P_IMAGE"),
    "PocketMageMedia": ("object", "P_MEDIA"),
    "PocketMageApp": ("object", "P_APP"),
}

HOST_OWNED = set(ACCESSORS)

POOL_SLOTS = 4
POOL_BYTES = 256

ENUM_SOURCES = [
    ("pm_target", "PM_TARGET", "pocketmage_font/pocketmage_font.h", "DisplayTarget"),
    ("pm_style", "PM_STYLE", "pocketmage_font/pocketmage_font.h", "FontStyle"),
    ("pm_lang", "PM_LANG", "pocketmage_i18n/pocketmage_i18n_gen.h", "Lang"),
    ("pm_refresh", "PM_REFRESH", "pocketmage_ui/pocketmage_ui.h", "EinkRefresh"),
    ("pm_audio_state", "PM_AUDIO_STATE", "pocketmage_audio/pocketmage_audio.h", "AudioState"),
    ("pm_audio_output", "PM_AUDIO_OUTPUT", "pocketmage_audio/pocketmage_audio.h", "AudioOutput"),
    ("pm_audio_result", "PM_AUDIO_RESULT", "pocketmage_audio/playback_session.h",
     "PlaybackSession::Result"),
    ("pm_image_format", "PM_IMAGE_FORMAT", "pocketmage_image/pocketmage_image.h", "ImageFormat"),
    ("pm_key", "PM", "pocketmage_kb/key_codes.h", "KeyCode"),
    ("pm_transport_icon", "PM_TRANSPORT_ICON", "pocketmage_ui/pocketmage_ui.h", "TransportIcon"),
]

STRUCT_SOURCES = {
    "ImageInfo": ("pm_image_info", "pocketmage_image/pocketmage_image.h"),
    "MediaInfo": ("pm_media_info", "pocketmage_media/pocketmage_media.h"),
}

NAMESPACE_CONST_SOURCES = [
    ("pm_jingle", "PM_JINGLE", "pocketmage_bz/pocketmage_bz.h", "Jingles",
     "Jingle"),
]

ENUM_VALIDATION_SOURCES = {
    host.rsplit("::", 1)[-1]: (path, host)
    for _c, _prefix, path, host in ENUM_SOURCES
}
ENUM_VALIDATION_SOURCES.update({
    "Mode": ("pocketmage_sd/pocketmage_sd.h", "PocketmageSD::Mode"),
    "WifiRadioState": ("pocketmage_wifi/pocketmage_wifi.h", "WifiRadioState"),
    "StringID": ("pocketmage_i18n/pocketmage_i18n_gen.h", "StringID"),
})

SENTINEL_NAMES = {
    ("pm_style", "_StyleCount"): "PM_STYLE_COUNT",
    ("pm_lang", "_LANG_COUNT"): "PM_LANG_COUNT",
}

ENUM_BODY_RE = re.compile(
    r"enum\s+(?:class\s+)?(?P<name>[A-Za-z_][A-Za-z0-9_]*)[^\{;]*\{(?P<body>[^\}]*)\}",
    re.S)


def parse_enum_text(text, enum_name):
    for m in ENUM_BODY_RE.finditer(text):
        if m.group("name") != enum_name:
            continue
        names = []
        for raw in m.group("body").split(","):
            item = raw.split("=")[0].strip()
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", item or ""):
                names.append(item)
        if not names:
            raise SystemExit(f"enum {enum_name} parsed empty")
        return names
    raise SystemExit(f"enum {enum_name} not found")


def read_source(path, rel_path):
    if not os.path.exists(path):
        raise SystemExit(f'source not found: {rel_path}')
    with open(path, encoding='utf-8') as handle:
        return sm.strip_comments(handle.read())


def namespace_body(text, namespace):
    """Extract a balanced namespace body without flattening nested declarations."""
    match = re.search(
        r'namespace\s+' + re.escape(namespace) + r'\s*\{', text)
    if not match:
        raise SystemExit(f'namespace {namespace} not found')
    depth, start = 1, match.end()
    for index in range(start, len(text)):
        char = text[index]
        if char == '{':
            depth += 1
        elif char == '}':
            depth -= 1
            if depth == 0:
                return text[start:index]
    else:
        raise SystemExit(f'namespace {namespace} not closed')
def parse_namespace_consts(text, namespace, type_name):
    body = namespace_body(text, namespace)
    pattern = r'\bconst\s+' + re.escape(type_name) + r'\s*&?\s*([A-Za-z_]\w*)\s*='
    names = re.findall(pattern, body)
    if not names:
        raise SystemExit(f'namespace {namespace} declares no {type_name}')
    return names


def load_namespace_const_tables(root):
    tables = []
    for c_name, prefix, rel_path, namespace, type_name in NAMESPACE_CONST_SOURCES:
        text = read_source(os.path.join(root, rel_path), rel_path)
        values = [(f'{prefix}_{snake(name).upper()}', name)
                  for name in parse_namespace_consts(
                      text, namespace, type_name)]
        values.append((f'{prefix}_COUNT', None))
        tables.append((c_name, namespace, values, type_name))
    return tables


def parse_enum(root, rel_path, enum_name):
    return parse_enum_text(read_source(os.path.join(root, rel_path), rel_path),
                           enum_name.rsplit("::", 1)[-1])


def load_enums(root):
    tables = []
    for c_name, prefix, rel_path, host_name in ENUM_SOURCES:
        values = []
        for value_name in parse_enum(root, rel_path, host_name):
            const = SENTINEL_NAMES.get(
                (c_name, value_name),
                prefix + "_" + snake(value_name).upper())
            values.append((const, value_name))
        tables.append((c_name, host_name, values, None))
    tables.extend(load_namespace_const_tables(root))
    return tables


def enum_numbers(root):
    """Resolve literal enum assignments; reject expressions instead of changing ABI values."""
    numbers = {}
    for _c, _prefix, path, host in ENUM_SOURCES:
        text = read_source(os.path.join(root, path), path)
        body = next(match.group("body") for match in ENUM_BODY_RE.finditer(text)
                    if match.group("name") == host.rsplit("::", 1)[-1])
        value = 0
        for raw in body.split(","):
            if not raw.strip():
                continue
            name, separator, expression = raw.partition("=")
            if separator:
                literal = expression.strip()
                if not re.fullmatch(r"-?(?:0[xX][0-9a-fA-F]+|0[bB][01]+|0|[1-9][0-9]*)", literal):
                    raise SystemExit(f"unsupported enum value {host}::{name.strip()}: {literal}")
                value = int(literal, 0)
            numbers[f"{host}::{name.strip()}"] = value
            value += 1
    return numbers


ENUM_TYPES = {"DisplayTarget", "FontStyle", "Lang", "Mode", "WifiRadioState",
              "StringID", "Jingle", "EinkRefresh", "AudioState", "AudioOutput", "ImageFormat",
              "KeyCode", "TransportIcon"}

SCALARS = {"bool", "char", "int", "unsigned", "long", "short", "float",
           "double", "size_t", "ulong", "unsigned long", "int8_t", "int16_t",
           "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
           "time_t"}

OPAQUE = {
    "DateTime", "RTC_PCF8563", "Adafruit_TCA8418", "Adafruit_MPR121",
    "U8G2", "DisplayT", "WifiApInfo", "FontTable",
}

CALLBACK_TYPES = {
    "AppExitCallback": ("pm_app_exit_callback", "void"),
    "AppDestructor": ("pm_app_destructor", "void*"),
}

FREE_FUNCTION_HEADERS = {
    "pocketmage_io.h": "io",
    "pocketmage_layout.h": "layout",
    "pocketmage_ui.h": "ui",
    "pocketmage_sys.h": "sys",
}

FREE_FUNCTION_NAMESPACES = {"pocketmage_sys.h": "pocketmage"}
FREE_FUNCTION_NAMES = {("pocketmage_sys.h", "reportActivity"): "pm_activity"}
FREE_FUNCTION_EXCLUDED = {
    ("pocketmage_sys.h", "idleSleepDue"),
    ("pocketmage_sys.h", "checkIdleSleep"),
    ("pocketmage_sys.h", "PWR_BTN_irq"),
}

FREE_FUNCTION_HEADERS_PATH = {
    "pocketmage_io.h": "pocketmage_io/pocketmage_io.h",
    "pocketmage_layout.h": "pocketmage_layout/pocketmage_layout.h",
    "pocketmage_ui.h": "pocketmage_ui/pocketmage_ui.h",
    "pocketmage_sys.h": "pocketmage_sys/pocketmage_sys.h",
}

VECTOR_STRING = "std::vector<String>"

VECTOR_PARAM_SUFFIX = "_items"

NOT_SURFACE = {"ScopedCpuBoost"}

OVERRIDES = {
    ("PocketMageApp", "beginSession"): "skip",
    ("PocketMageApp", "finishSession"): "skip",
    ("FontEngine", "textWidth"): "keep_first",
    ("PocketmageCLOCK", "setToCompileTimeUTC"): "skip",
    ("PocketmageCLOCK", "getTimeDiff"): "skip",
    ("PocketmageCLOCK", "getTimeoutMillis"): "skip",
    ("PocketmageCLOCK", "getPrevTimeMillis"): "skip",
    ("PocketmageKB", "disableInterrupts"): "skip",
    ("PocketmageKB", "enableInterrupts"): "skip",
    ("PocketmageEink", "lockPanel"): "skip",
    ("PocketmageEink", "unlockPanel"): "skip",
}

MANUAL_HEADERS = ("pm_app_api.h", "pm_overlay_api.h")

EXTERN_DECLS = {
    "delay": "void delay(unsigned long ms)",
}

ADOPTED = {
    ("FontEngine", "drawText"): "pm_text",
    ("FontEngine", "textWidth"): "pm_text_width",
    ("FontEngine", "fontHeight"): "pm_font_height",
    ("FontEngine", "setTextColor"): "pm_text_color",
}

GENERATED_NAMES = {
    ("PocketmageOled", "sysMessage"): "pm_oled_sysmsg",
    ("PocketmageOled", "getPowerSave"): "pm_oled_power_save",
    ("PocketmageKB", "updateKeypress"): "pm_kb_read",
    ("PocketmageKB", "getKeyboardState"): "pm_kb_state",
    ("PocketmageCLOCK", "isValid"): "pm_clock_valid",
    ("PocketmageSD", "mkdirPath"): "pm_sd_mkdir",
    ("PocketmageSD", "dirEnumOpen"): "pm_sd_dir_open",
    ("PocketmageSD", "dirEnumEntry"): "pm_sd_dir_entry",
    ("PocketmageSD", "dirEnumClose"): "pm_sd_dir_close",
    ("PocketmageSD", "streamOpen"): "pm_sd_stream_open_mode",
    ("PocketmageSD", "streamSeek"): "pm_sd_stream_seek_from",
    ("PocketmageSD", "streamClose"): "pm_sd_stream_close_checked",
    ("PocketMageImage", "decodeTo1Bit"): "pm_image_decode",
    ("PocketMageImage", "decodeRegionTo1Bit"): "pm_image_decode_region_to_1bit",
    ("PocketMageImage", "probe"): "pm_image_probe_region",
}

NAME_OVERRIDES = {
    ("I18n", "code", 0): "code",
    ("I18n", "code", 1): "code_at",
    ("I18n", "nativeName", 0): "native_name",
    ("I18n", "nativeName", 1): "native_name_at",
}

PASCAL_TO_SNAKE = [
    (re.compile(r"(?<=[a-z0-9])(?=[A-Z])"), "_"),
    (re.compile(r"(?<=[A-Z])(?=[A-Z][a-z])"), "_"),
]


TEMPLATE_ARG_RE = re.compile(r'<[A-Za-z_][^<>]*>')


def strip_template_args(text: str) -> str:
    return TEMPLATE_ARG_RE.sub('T', text)


def parse_free_functions(path: str, namespace: str | None = None) -> list[dict]:
    text = read_source(path, os.path.basename(path))
    if namespace:
        text = namespace_body(text, namespace)
    out = []
    skip_depth = None
    skip_type = False
    depth = 0
    buffer = ''

    def flush(statement: str) -> None:
        statement = ' '.join(statement.split())
        if not statement or statement.startswith(('#', '//', '*')):
            return
        m = sm.DECL_RE.match(statement)
        if not m:
            return
        name = m.group('name')
        if name in sm.SKIP_NAMES or name.startswith(('~', 'operator')):
            return
        if ';' not in m.group('tail'):
            return
        ret = ' '.join(m.group('ret').split())
        args_raw = m.group('args')
        if sm.NOT_A_DECL_RE.search(ret) or sm.COMPARISON_RE.search(
                strip_template_args(ret)):
            return
        if sm.COMPARISON_RE.search(strip_template_args(args_raw)):
            return
        params = []
        for raw in [a.strip() for a in args_raw.split(',') if a.strip()]:
            info = sm.classify_param(raw)
            params.append({'type': info.get('type', raw),
                           'name': info.get('name', n(len(params))),
                           'has_default': info['has_default']})
        out.append({'class': None, 'name': name, 'ret': ret, 'namespace': namespace,
                    'params': params, 'constructor': False})

    for line in text.split('\n'):
        if skip_depth is None and re.match(r'^\s*(class|struct)\s+[A-Za-z_]', line):
            skip_depth = 0
        if skip_depth is not None:
            depth += line.count('{') - line.count('}')
            if depth <= 0:
                skip_depth, depth = None, 0
            continue
        if skip_type:
            depth += line.count('{') - line.count('}')
            if depth <= 0:
                skip_type, depth = False, 0
            continue

        stripped = line.strip()
        if not stripped or stripped.startswith(('#', '//', '*', '}')):
            continue
        if re.match(r'^(enum|typedef|struct|union|namespace)\b', stripped):
            skip_type = True
            depth = stripped.count('{') - stripped.count('}')
            if depth <= 0:
                skip_type, depth = False, 0
            continue
        buffer += ' ' + stripped
        balanced = buffer.count('(') == buffer.count(')')
        if not (balanced and buffer.rstrip().endswith(';')):
            continue
        flush(buffer)
        buffer = ''

    return out


def snake(name: str) -> str:
    out = name
    for pattern, repl in PASCAL_TO_SNAKE:
        out = pattern.sub(repl, out)
    return re.sub(r"_+", "_", out).lower()


def module_prefix(class_name: str) -> str:
    name = re.sub(r"^Pocket[Mm]age", "", class_name)
    return "pm_" + snake(name)


class Param:

    def __init__(self, c_type: str, name: str, passthrough: bool, native_type=None):
        self.c_type = c_type
        self.name = name
        self.passthrough = passthrough
        self.native_type = native_type


class Method:

    def __init__(self, name, ret, params, call, borrowed=False, statement=None):
        self.name = name
        self.ret = ret
        self.params = params
        self.call = call
        self.borrowed = borrowed
        self.statement = statement
        self.adopted = None


class Skipped:

    def __init__(self, class_name, method, reason):
        self.class_name = class_name
        self.method = method
        self.reason = reason


def normalize_type(raw: str) -> str:
    text = re.sub(r"\s+", " ", raw.strip())
    text = text.replace(" *", "*").replace("* ", "*")
    text = text.replace("long int", "long").replace("long long int", "long long")
    text = text.replace("unsigned int", "unsigned").replace("unsigned long int", "unsigned long")
    text = text.replace("short int", "short").replace("signed int", "int")
    text = text.replace("ulong", "unsigned long")
    return text


def project_param(raw: str, index: int):
    t = normalize_type(raw)
    is_ref = t.endswith("&")
    is_ptr = "*" in t
    base = t.rstrip("&").rstrip("*").strip()
    base = re.sub(r"^const\s+", "", base).strip()

    if base in CALLBACK_TYPES and not is_ref and not is_ptr:
        return CALLBACK_TYPES[base][0], True, n(index), None

    if base in STRUCT_SOURCES:
        if not is_ref or t.startswith("const"):
            return None, None, None, f"struct {t} needs an input adapter"
        return f"struct {STRUCT_SOURCES[base][0]}*", False, f"native_{n(index)}", None

    if base in OPAQUE:
        return None, None, None, f"opaque type {base}"

    if base in ("fs::FS", "FS"):
        return "DROP", False, "*global_fs", None

    if base == "String":
        return "const char*", False, f"String({n(index)})", None

    if base == VECTOR_STRING and (is_ref or is_ptr):
        return "VECTOR_PARAM", False, f"VECTOR_PARAM_CALL({n(index)})", None

    if base in ENUM_TYPES:
        if (is_ptr or is_ref) and base != "Jingle":
            return None, None, None, f"enum indirection {t} needs an adapter"
        if base == "Jingle":
            return "int", False, f"*pm_jingle_table[{n(index)}]", None
        native_type = "PocketmageSD::Mode" if base == "Mode" else base
        return "int", False, f"static_cast<{native_type}>({n(index)})", None

    if base == "void" and is_ptr and not is_ref:
        return t, True, n(index), None

    if is_ptr and base == "char":
        return "const char*" if t.startswith("const") else "char*", True, n(index), None

    if base in SCALARS:
        if is_ref:
            return base + "*", True, f"*{n(index)}", None
        return t, True, n(index), None

    if is_ptr and base in SCALARS:
        return base + "*", True, n(index), None

    return None, None, None, f"unmapped type {t}"


def n(index: int) -> str:
    return f"a{index}"


def project_ret(raw: str):
    t = normalize_type(raw)
    is_ref = t.endswith("&")
    is_ptr = "*" in t
    base = re.sub(r"^const\s+", "", t.rstrip("&").rstrip("*").strip()).strip()

    if base == "void":
        return "void", False, None, None
    if base == "String":
        return "const char*", True, "string", None
    if base == VECTOR_STRING:
        return "VECTOR_RET", False, "vector", None
    if base in OPAQUE:
        return None, False, None, f"opaque return {base}"
    if base in ENUM_TYPES:
        return "int", False, "enum", None
    if is_ref:
        return None, False, None, f"reference return {t}"
    if is_ptr and base == "char":
        return "const char*", True, "borrowed", None
    if not is_ptr and base in SCALARS:
        return base, False, None, None
    return None, False, None, f"unmapped return {t}"


def build_free_call(module, name, args):
    return f"{name}({', '.join(args)})"


def vector_from_c(items: str, count: str) -> str:
    return f"std::vector<String>({items}, {items} + {count})"


def build_call(class_name, method, args):
    kind, accessor = ACCESSORS[class_name]
    joined = ", ".join(args)
    if kind == "object":
        return f"{accessor}.{method}({joined})"
    return f"{accessor}::{method}({joined})"


def build_free_module(basename, module, entries):
    prefix = f"pm_{module}"
    methods, skipped = [], []
    for entry in entries:
        name = entry["name"]
        if (basename, name) in FREE_FUNCTION_EXCLUDED:
            skipped.append(Skipped(basename, name, "OS lifecycle implementation"))
            continue
        ret_raw, _is_string, ret_kind, ret_why = project_ret(entry["ret"])
        if ret_raw is None:
            skipped.append(Skipped(basename, name, ret_why))
            continue

        params, args = [], []
        bad = None
        for i, p in enumerate(entry["params"]):
            c_type, pass_through, arg, why = project_param(p["type"], i)
            if why:
                bad = why
                break
            if c_type == "DROP":
                args.append(arg)
                continue
            if c_type == "VECTOR_PARAM":
                params.append(Param("const char* const*", f"a{i}_items", False))
                params.append(Param("int", f"a{i}_count", False))
                args.append(vector_from_c(f"a{i}_items", f"a{i}_count"))
                continue
            params.append(Param(c_type, n(i), pass_through, p["type"]))
            args.append(arg)
        if bad:
            skipped.append(Skipped(basename, name, bad))
            continue

        qualified = f"{entry['namespace']}::{name}" if entry.get("namespace") else name
        call = build_free_call(module, qualified, args)
        if ret_raw == "VECTOR_RET":
            methods.append(Method(
                f"{prefix}_{snake(name)}_count", "int",
                [Param(c.c_type, c.name, c.passthrough) for c in params],
                f"static_cast<int>({call}.size())"))
            getter_params = [Param(c.c_type, c.name, c.passthrough)
                             for c in params]
            getter_params.append(Param("int", "index", False))
            getter_params.append(Param("char*", "out", False))
            getter_params.append(Param("size_t", "out_size", False))
            methods.append(Method(
                f"{prefix}_{snake(name)}_get", "int", getter_params, None,
                statement=vector_get_body(call)))
            continue
        exported = FREE_FUNCTION_NAMES.get((basename, name), f"{prefix}_{snake(name)}")
        methods.append(Method(exported, ret_raw, params, call,
                              ret_kind == "borrowed"))
    return methods, skipped


def vector_get_body(call: str) -> str:
    lines = [
        '  { const auto& v = ' + call + ';',
        '    if (index < 0 || static_cast<size_t>(index) >= v.size()) return -1;',
        '    const String& s = v[index];',
        '    if (out_size == 0) return 0;',
        '    const size_t n = s.length() < out_size - 1 ? s.length()'
        ' : out_size - 1;',
        '    memcpy(out, s.c_str(), n);',
        '    out[n] = 0;',
        '    return static_cast<int>(n); }',
    ]
    return '\n'.join(lines)


def build(headers, seen_pairs):
    methods, skipped = [], []
    projected_signatures = {}
    for path in headers:
        base = os.path.basename(path)
        if base in FREE_FUNCTION_HEADERS:
            built, missed = build_free_module(
                base, FREE_FUNCTION_HEADERS[base],
                parse_free_functions(path, FREE_FUNCTION_NAMESPACES.get(base)))
            methods.extend(built)
            skipped.extend(missed)
            continue
        parsed, class_name = sm.parse_header(path)
        if not parsed:
            continue
        if class_name in NOT_SURFACE:
            skipped.append(Skipped(class_name, "<class>", "host-only RAII helper"))
            continue
        if class_name not in HOST_OWNED:
            skipped.append(Skipped(class_name, "<class>", "no host accessor"))
            continue
        for entry in parsed:
            if entry["constructor"]:
                continue
            name = entry["name"]
            cls = entry["class"]
            key = (cls, name)

            if OVERRIDES.get(key) == "skip":
                skipped.append(Skipped(cls, name, "excluded in OVERRIDES"))
                continue

            ret_raw, _, borrowed, ret_why = project_ret(entry["ret"])
            if ret_raw is None:
                skipped.append(Skipped(cls, name, ret_why))
                continue

            params, args = [], []
            bad = None
            for i, p in enumerate(entry["params"]):
                c_type, pass_through, arg, why = project_param(p["type"], i)
                if why:
                    bad = why
                    break
                if c_type == "DROP":
                    args.append(arg)
                    continue
                params.append(Param(c_type, n(i), pass_through, p["type"]))
                args.append(arg)
            if bad:
                skipped.append(Skipped(cls, name, bad))
                continue

            fn_name = GENERATED_NAMES.get(key, f"{module_prefix(cls)}_{snake(name)}")
            arity = len(params)
            if (cls, name, arity) in NAME_OVERRIDES:
                fn_name = f"{module_prefix(cls)}_{NAME_OVERRIDES[(cls, name, arity)]}"
            dedupe_key = (cls, name, arity)
            native_signature = None if key in ADOPTED else (
                normalize_type(entry["ret"]),
                tuple(normalize_type(p["type"]) for p in entry["params"]))
            signature = (ret_raw, tuple(p.c_type for p in params), native_signature)
            if dedupe_key in seen_pairs:
                if projected_signatures.get(dedupe_key) != signature:
                    raise SystemExit(f"projected overload collision: {cls}::{name}")
                skipped.append(Skipped(cls, name, "duplicate signature"))
                continue
            seen_pairs.add(dedupe_key)
            projected_signatures[dedupe_key] = signature
            if (cls, name) in ADOPTED:
                adopted = Method(ADOPTED[(cls, name)], ret_raw, params, None,
                                 borrowed == "borrowed")
                adopted.adopted = ADOPTED[(cls, name)]
                methods.append(adopted)
                continue

            call = build_call(cls, name, args)
            methods.append(Method(fn_name, ret_raw, params, call,
                                  borrowed == "borrowed"))

    return methods, skipped


def emit_header(methods, path, root, verify=False):
    out = []
    out.append("// App-facing C ABI for external ELF apps. Regenerate, do not edit.")
    out.append("// String returns stay valid for the next POOL_SLOTS string-returning calls.")
    out.append("")
    out.append("#ifndef PM_SDK_APP_H")
    out.append("#define PM_SDK_APP_H")
    out.append("")
    out.append("#include <stdbool.h>")
    out.append("#include <stddef.h>")
    out.append("#include <stdint.h>")
    out.append("#include <pm_overlay_api.h>")
    out.append("")
    out.append("#ifdef __cplusplus")
    out.append('extern "C" {')
    out.append("#endif")
    out.append("")
    out.append("// Bump on any incompatible change here. Apps compare at startup.")
    out.append("#define PM_APP_API_ABI 2u")
    out.append("")
    numbers = enum_numbers(root)
    for c_name, host_name, values, _type in load_enums(root):
        out.append(f"// Mirrors {host_name}, values read from the SDK header.")
        out.append(f"enum {c_name} {{")
        for ordinal, (const, value) in enumerate(values):
            number = numbers.get(f"{host_name}::{value}", ordinal)
            out.append(f"  {const} = {number},")
        out.append("};")
        out.append("")

    for native, (c_name, _path) in STRUCT_SOURCES.items():
        out.append(f"// Field-wise projection of {native}; never aliases its native layout.")
        out.append(f"struct {c_name} {{")
        for field_type, name, extent in struct_fields(root, native):
            c_type = "int" if field_type in ENUM_TYPES else field_type
            suffix = f"[{extent}]" if extent else ""
            out.append(f"  {c_type} {name}{suffix};")
        out.append("};")
        out.append("")

    for name, argument in CALLBACK_TYPES.values():
        out.append(f"typedef void (*{name})({argument});")
    out.append("")

    out.append("// version")
    out.append("")
    out.append("// ABI identifier of the running host.")
    out.append("uint32_t pm_app_abi(void);")
    out.append("")
    out.append("// SDK version string the running host was built with.")
    out.append("const char* pm_host_sdk_version(void);")
    out.append("")
    out.append("// Declared here, not via pocketmage_globals.h: the export table pulls")
    out.append("// the address and globals.h clashes with its bare newlib prototypes.")
    out.append('#ifdef __cplusplus')
    out.append('extern "C" {')
    out.append("#endif")
    out.append("extern const char pocketmage_sdk_version[];")
    out.append("#ifdef __cplusplus")
    out.append("}")
    out.append("#endif")
    out.append("")

    if EXTERN_DECLS:
        out.append("// Host primitives exported from the firmware core.")
        out.append("#ifdef __cplusplus")
        out.append('extern "C" {')
        out.append("#endif")
        for name in sorted(EXTERN_DECLS):
            out.append(EXTERN_DECLS[name] + ";")
        out.append("#ifdef __cplusplus")
        out.append('}  // extern "C"')
        out.append("#endif")
        out.append("")

    groups = {}
    for m in methods:
        groups.setdefault(module_prefix_of(m), []).append(m)
    for prefix in sorted(groups):
        out.append(f"// {prefix[3:]}")
        out.append("")
        for m in groups[prefix]:
            sig = ", ".join(f"{p.c_type} {p.name}" for p in m.params) or "void"
            if m.adopted:
                out.append("// Defined by hand in pm_app_api.cpp.")
            out.append(f"{m.ret} {m.adopted or m.name}({sig});")
        out.append("")

    out.append("#ifdef __cplusplus")
    out.append('}  // extern "C"')
    out.append("#endif")
    out.append("")
    out.append("#endif  // PM_SDK_APP_H")

    write_output(path, "\n".join(out) + "\n", verify)




def module_prefix_of(method):
    for c_name in ACCESSORS:
        prefix = module_prefix(c_name)
        if method.name.startswith(prefix + "_"):
            return prefix
    return "pm_misc"


def param_guard(param, root):
    """Validate projected integers and references before C++ narrowing/dereference."""
    if param.native_type is None:
        return None
    raw = normalize_type(param.native_type)
    base = re.sub(r"^const\s+", "", raw.rstrip("&*").strip())
    if base == "Jingle":
        return f"{param.name} < 0 || {param.name} >= PM_JINGLE_COUNT"
    if base in ENUM_VALIDATION_SOURCES:
        path, host = ENUM_VALIDATION_SOURCES[base]
        names = parse_enum(root, path, host)
        allowed = [name for name in names if name not in ("_StyleCount", "_LANG_COUNT", "_STR_COUNT")]
        return " && ".join(f"{param.name} != static_cast<int>({host}::{name})"
                           for name in allowed)
    if raw.endswith("&") and (base in SCALARS or base in STRUCT_SOURCES):
        return f"!{param.name}"
    return None


def invalid_return(ret):
    """Return a neutral ABI result for rejected arguments without calling native code."""
    return "return;" if ret == "void" else 'return "";' if ret == "const char*" else "return {};"


def struct_fields(root, native):
    """Read supported value fields; fail rather than exporting a C++ object layout."""
    _c_name, path = STRUCT_SOURCES[native]
    text = read_source(os.path.join(root, path), path)
    match = re.search(r"struct\s+" + re.escape(native) + r"\s*\{([^}]+)\}", text, re.S)
    if match is None:
        raise SystemExit(f"struct {native} not found")
    fields = []
    for declaration in match.group(1).split(";"):
        if not declaration.strip():
            continue
        field = re.fullmatch(r"\s*(\w+)\s+(\w+)\s*(?:\[(\d+)\])?\s*", declaration)
        if field is None or field[1] not in SCALARS | ENUM_TYPES:
            raise SystemExit(f"unsupported field in {native}: {declaration.strip()}")
        fields.append(field.groups())
    return fields


def output_struct_type(param):
    raw = normalize_type(param.native_type or "")
    native = raw.rstrip("&").strip()
    return native if raw.endswith("&") and native in STRUCT_SOURCES else None


def emit_source(methods, path, header_basename, root, verify=False):
    out = []
    out.append("// Host wrappers backing pm_sdk_app.h. Regenerate, do not edit.")
    out.append("// Each wrapper is extern \"C\", so SDK mangling stays local to this file.")
    out.append("")
    out.append("#include <globals.h>")
    out.append("#include <pocketmage_image/pocketmage_image.h>")
    out.append("#include <pocketmage_media/pocketmage_media.h>")
    out.append("")
    out.append(f"#include <{header_basename}>")
    out.append("")
    out.append("// Fail the build if an SDK enumerator moves under a C constant.")
    for c_name, host_name, values, type_name in load_enums(root):
        if type_name is None:
            for const, value_name in values:
                out.append(
                    f"static_assert(static_cast<int>({host_name}::{value_name})"
                    f" == {const},")
                out.append(
                    f'              "{const} parity: {host_name}::{value_name}");')
        else:
            out.append(f"extern const {type_name}* const {c_name}_table[] = {{")
            for _const, value_name in values:
                if value_name is not None:
                    out.append(f"  &{host_name}::{value_name},")
            out.append("};")
            out.append(
                f"static_assert(sizeof({c_name}_table) /"
                f" sizeof({c_name}_table[0]) == {c_name.upper()}_COUNT,")
            out.append(f'              "{c_name.upper()}_COUNT parity: {host_name}");')
    out.append("")
    out.append("namespace {")
    out.append("")
    out.append(f"constexpr size_t kPoolSlots = {POOL_SLOTS};")
    out.append(f"constexpr size_t kPoolBytes = {POOL_BYTES};")
    out.append("")
    out.append("// Backs every const char* return. Not locked: only the owning app")
    out.append("// task calls these, so there is nothing to race with.")
    out.append("char g_pool[kPoolSlots][kPoolBytes];")
    out.append("size_t g_next_slot = 0;")
    out.append("")
    out.append("char* pool_slot() {")
    out.append("  char* slot = g_pool[g_next_slot];")
    out.append("  g_next_slot = (g_next_slot + 1) % kPoolSlots;")
    out.append("  slot[0] = '\\0';")
    out.append("  return slot;")
    out.append("}")
    out.append("")
    out.append("// toCharArray always null terminates and truncates.")
    out.append("char* pool_string(const String& value) {")
    out.append("  char* slot = pool_slot();")
    out.append("  value.toCharArray(slot, kPoolBytes);")
    out.append("  return slot;")
    out.append("}")
    out.append("")
    out.append("char* pool_borrowed(const char* value) {")
    out.append("  char* slot = pool_slot();")
    out.append("  if (value == nullptr) {")
    out.append("    return slot;")
    out.append("  }")
    out.append("  strncpy(slot, value, kPoolBytes - 1);")
    out.append("  slot[kPoolBytes - 1] = '\\0';")
    out.append("  return slot;")
    out.append("}")
    out.append("")
    out.append("}  // namespace")
    out.append("")
    out.append('extern "C" {')
    out.append("")
    out.append("uint32_t pm_app_abi(void) { return PM_APP_API_ABI; }")
    out.append("")
    out.append("const char* pm_host_sdk_version(void) {")
    out.append("  return pocketmage_sdk_version;")
    out.append("}")
    out.append("")

    for m in methods:
        if m.adopted:
            continue
        sig = ", ".join(f"{p.c_type} {p.name}" for p in m.params) or "void"
        out.append(f"{m.ret} {m.name}({sig}) {{")
        for param in m.params:
            guard = param_guard(param, root)
            if guard:
                out.append(f"  if ({guard}) {invalid_return(m.ret)}")
        structures = [(param, output_struct_type(param)) for param in m.params
                      if output_struct_type(param)]
        if structures:
            if m.ret != "bool":
                raise SystemExit(f"{m.name}: struct output requires a checked bool result")
            for param, native in structures:
                out.append(f"  {native} native_{param.name}{{}};")
            out.append(f"  if (!{m.call}) return false;")
            for param, native in structures:
                for field_type, name, extent in struct_fields(root, native):
                    src, dst = f"native_{param.name}.{name}", f"{param.name}->{name}"
                    if extent:
                        out.append(f"  memcpy({dst}, {src}, sizeof({dst}));")
                    else:
                        value = f"static_cast<int>({src})" if field_type in ENUM_TYPES else src
                        out.append(f"  {dst} = {value};")
            out.append("  return true;")
        elif m.statement:
            for line in m.statement.split('\n'):
                out.append(line)
        elif m.ret == "void":
            out.append(f"  {m.call};")
        elif m.ret == "const char*":
            if m.borrowed:
                out.append(f"  return pool_borrowed({m.call});")
            else:
                out.append(f"  return pool_string({m.call});")
        elif m.ret == "int":
            out.append(f"  return static_cast<int>({m.call});")
        else:
            out.append(f"  return {m.call};")
        out.append("}")
        out.append("")

    out.append('}  // extern "C"')

    write_output(path, "\n".join(out) + "\n", verify)


LIST_HEADER = """# {title}
# The pm_* block is generated by tools/gen_app_api.py. Do not hand edit.
{pair_note}
pocketmage_sdk_version
"""


def emit_exports(names, path, title, pair_note, verify=False):
    out = [LIST_HEADER.format(title=title, pair_note=pair_note)]
    out.append("")
    out.append("# app-facing C ABI (pm_sdk_app.h)")
    out.extend(sorted(names))
    write_output(path, "\n".join(out) + "\n", verify)


def write_output(path, text, verify=False):
    if verify:
        if not os.path.isfile(path):
            raise SystemExit(f"stale generated output (missing): {path}")
        with open(path, encoding="utf-8") as handle:
            if handle.read() != text:
                raise SystemExit(f"stale generated output: {path}; run gen_app_api.py")
        return
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def manual_declarations(root):
    declarations = {}
    pattern = re.compile(r"^\s*([\w][\w\s*]*?)\b(pm_\w+)\s*\(([^;{}]*)\)\s*;", re.M)
    for name in MANUAL_HEADERS:
        text = read_source(os.path.join(root, name), name)
        for ret, symbol, args in pattern.findall(text):
            if symbol in declarations:
                raise SystemExit(f"manual declaration collision: {symbol}")
            params = [] if args.strip() in ("", "void") else sm.split_args(args)
            declarations[symbol] = (
                normalize_type(ret),
                tuple(normalize_type(sm.classify_param(p)["type"]) for p in params))
    return declarations


def export_names(methods, manual):
    seen = set()
    for method in methods:
        if method.name in seen:
            raise SystemExit(f"generated name collision: {method.name}")
        seen.add(method.name)
        if method.adopted:
            signature = (normalize_type(method.ret),
                         tuple(normalize_type(p.c_type) for p in method.params))
            if manual.get(method.name) != signature:
                raise SystemExit(f"adapter signature mismatch: {method.name}")
        elif method.name in manual:
            raise SystemExit(f"generated/manual ownership collision: {method.name}")
    return seen | set(manual) | set(EXTERN_DECLS) | {"pm_app_abi", "pm_host_sdk_version"}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", default=".", help="SDK root")
    ap.add_argument("--header-out", default="pm_sdk_app.h")
    ap.add_argument("--source-out", default=None,
                    help="wrapper path; defaults to <host-root>/src/ELF_SYMS/pm_sdk_app.cpp")
    ap.add_argument("--host-root", default="../..",
                    help="PocketMageOS root, parent of lib/")
    ap.add_argument("--check", action="store_true",
                    help="print the projection and skipped methods")
    ap.add_argument("--verify", action="store_true",
                    help="fail if generated files differ; do not write files")
    args = ap.parse_args()

    headers = sorted(
        os.path.join(args.root, name, h)
        for name in os.listdir(args.root)
        if name.startswith("pocketmage_")
        and os.path.isdir(os.path.join(args.root, name))
        for h in os.listdir(os.path.join(args.root, name))
        if h.startswith("pocketmage_") and h.endswith(".h")
    )

    methods, skipped = build(headers, set())
    emitted = [m for m in methods if not m.adopted]

    names = export_names(methods, manual_declarations(args.root))

    if args.check:
        print(f"headers scanned      : {len(headers)}")
        print(f"methods projected    : {len(emitted)}")
        print(f"adopted from pm_app_api.h : {len(methods) - len(emitted)}")
        print(f"skipped              : {len(skipped)}")
        print()
        for prefix in sorted({module_prefix_of(m) for m in emitted}):
            group = [m for m in emitted if module_prefix_of(m) == prefix]
            print(f"  {prefix[3:]:<12} {len(group)}")
            for m in group:
                sig = ", ".join(p.c_type for p in m.params)
                print(f"      {m.ret:<12} {m.name}({sig})")
        print()
        print("skipped, with reasons:")
        for s in skipped:
            print(f"  {s.class_name}::{s.method:<22} {s.reason}")
        return 0

    header_out = os.path.join(args.root, args.header_out)
    source_out = args.source_out or os.path.join(
        args.host_root, "src", "ELF_SYMS", "pm_sdk_app.cpp")
    emit_header(methods, header_out, args.root, args.verify)
    emit_source(emitted, source_out, os.path.basename(header_out), args.root, args.verify)

    sdk_list = os.path.join(args.root, "symbols.list")
    os_list = os.path.join(args.host_root, "src", "ELF_SYMS", "host_exports.list")
    emit_exports(names, sdk_list, "Curated host-export surface for external apps.",
                 "# Keep this file in sync with the OS copy at\n"
                 "# PocketMageOS/src/ELF_SYMS/host_exports.list. tools/symbols.py\n"
                 "# validates it against the built firmware.", args.verify)
    emit_exports(names, os_list, "PocketMageOS host export surface",
                 "# Keep this file in sync with the SDK copy at\n"
                 "# lib/PocketMage_SDK/symbols.list. tools/pm/gate.py validates\n"
                 "# apps against the built firmware; apps may only reference\n"
                 "# these symbols.", args.verify)

    action = "verified" if args.verify else "wrote"
    for path in (header_out, source_out, sdk_list, os_list):
        print(f"{action} {path}")
    print(f"{len(names)} symbols exported, {len(skipped)} methods skipped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
