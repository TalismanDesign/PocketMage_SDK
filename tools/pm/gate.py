"""Resolve an app's undefined symbols against the host.

With a host firmware ELF the check is exact: every undefined symbol must exist
as a host global. Without one it falls back to the curated export lists plus a
libc allowlist, and anything outside that set is unconfirmed.
"""

from __future__ import annotations

import os
import re
import subprocess
from dataclasses import dataclass

from tools import symbols as symbols_mod

# libc symbols the host's newlib satisfies. Used only when no host ELF is
# available; --host-elf is the real gate.
LIBC_ALLOWLIST = frozenset(
    {
        "malloc", "calloc", "realloc", "free",
        "memcpy", "memmove", "memset", "memcmp",
        "strlen", "strcmp", "strncmp", "strcpy", "strncpy",
        "strcat", "strncat", "strchr", "strrchr", "strstr",
        "sprintf", "snprintf", "printf", "puts", "putchar",
        "sleep", "usleep",
        "write", "read", "open", "close", "lseek", "stat", "fstat",
        "floor", "ceil", "abs",
    }
)

#       Num:    Value  Size Type    Bind   Vis      Ndx Name
# 10: 00000000     0 NOTYPE  GLOBAL DEFAULT  UND printf
_SYM_LINE = re.compile(
    r"^\s*\d+:\s+\S+\s+\d+\s+\S+\s+(\S+)\s+\S+\s+UND\s+(\S+)\s*$"
)


@dataclass(frozen=True)
class ElfHeader:
    """The bits of `readelf -h` we care about."""

    data: str
    etype: str
    machine: str
    entry: str

    @property
    def is_little_endian(self) -> bool:
        return "little" in self.data

    @property
    def is_shared(self) -> bool:
        # readelf prints the mnemonic, not the constant: "DYN (Shared object file)".
        return self.etype.split(None, 1)[0] == "DYN"


@dataclass
class GateResult:
    """How the undefined symbols came out."""

    ok: list[str]
    unconfirmed: list[str]
    broken: list[str]
    entry: str = ""
    app_main_stripped: bool = False
    init_array_size: str = ""

    @property
    def passed(self) -> bool:
        return not self.broken and not self.unconfirmed


def parse_readelf_h(stdout: str) -> ElfHeader:
    """Pull the fields we check out of `readelf -h -W` output."""
    data = ""
    etype = ""
    machine = ""
    entry = ""
    for line in stdout.splitlines():
        if line.lstrip().startswith("Data:"):
            data = line.split(":", 1)[1].strip()
        elif line.lstrip().startswith("Type:"):
            etype = line.split(":", 1)[1].strip()
        elif line.lstrip().startswith("Machine:"):
            machine = line.split(":", 1)[1].strip()
        elif line.lstrip().startswith("Entry point address:"):
            entry = line.split(":", 1)[1].strip()
    return ElfHeader(data=data, etype=etype, machine=machine, entry=entry)


def find_app_main(stdout: str) -> str | None:
    """Symbol-table value of `app_main`, or None if stripped from the binary."""
    pattern = re.compile(
        r"^\s*\d+:\s+(\S+)\s+\d+\s+FUNC\s+GLOBAL\s+DEFAULT\s+\S+\s+app_main.*$"
    )
    for line in stdout.splitlines():
        m = pattern.match(line)
        if m:
            return m.group(1)
    return None


_APP_ICON_SIZE = 200  # 40x40, 1 bit per pixel, 5 bytes per row


def validate_icon(path: str) -> str | None:
    """Error message for a malformed app icon, or None when it is fine.

    A missing icon is legal (the host falls back to its default). A present
    icon must be exactly the 40x40 1-bpp payload the loader expects.
    """
    if not os.path.isfile(path):
        return None
    size = os.path.getsize(path)
    if size != _APP_ICON_SIZE:
        return (
            f"{os.path.basename(path)} is {size} bytes, expected {_APP_ICON_SIZE}: "
            "a 40x40 1-bit-per-pixel bitmap (5 bytes per row)"
        )
    return None


#     Name         Type        Address   Off    Size   EntSize Flags ...  
#   [ 4] .init_array INIT_ARRAY 00000000 0001b4 000004 04  WA  0   0  4
_INIT_ARRAY_RE = re.compile(
    r"^\s*\[\s*\d+\]\s+\.init_array\s+INIT_ARRAY\s+\S+\s+\S+\s+(\S+)"
)


def init_array_size(section_stdout: str) -> str:
    """Hex size of .init_array, or "" when the app runs no static constructors."""
    for line in section_stdout.splitlines():
        m = _INIT_ARRAY_RE.match(line)
        if m and int(m.group(1), 16) > 0:
            return m.group(1)
    return ""


def parse_undefined(stdout: str) -> list[str]:
    """Ordered unique GLOBAL/WEAK undefined names from `readelf -s -W`."""
    seen: list[str] = []
    for line in stdout.splitlines():
        m = _SYM_LINE.match(line)
        if not m:
            continue
        bind, name = m.groups()
        if bind not in ("GLOBAL", "WEAK"):
            continue
        if name not in seen:
            seen.append(name)
    return seen


def host_exports_candidates(sdk_root: str) -> list[str]:
    """Where the OS repo's host_exports.list might live.

    The SDK is vendored at Code/PocketMageOS/lib/PocketMage_SDK, so the OS src
    is two parents up; a standalone copy may only be one parent away.
    """
    return [
        os.path.abspath(os.path.join(sdk_root, "..", "src", "ELF_SYMS", "host_exports.list")),
        os.path.abspath(os.path.join(sdk_root, "..", "..", "src", "ELF_SYMS", "host_exports.list")),
    ]


def read_curated(sdk_root: str) -> set[str]:
    """Union of the SDK's symbols.list and the OS host_exports.list when present."""
    paths = [os.path.join(sdk_root, "symbols.list")]
    for candidate in host_exports_candidates(sdk_root):
        if os.path.isfile(candidate):
            paths.append(candidate)
            break

    curated: set[str] = set()
    for path in paths:
        if os.path.isfile(path):
            curated.update(symbols_mod.read_curated_list(path))
    return curated


def reconcile_exports(sdk_symbols: str, host_exports: str,
                      ) -> tuple[list[str], list[str]]:
    """Diff the curated surface against the host's export table.

    Returns (missing, extra): symbols in the SDK curated list that no host
    export covers, then host exports outside the curated list. missing must be
    empty for a release; extra is advisory (host-only symbols are normal).
    """
    curated = set(symbols_mod.read_curated_list(sdk_symbols))
    host = set(symbols_mod.read_curated_list(host_exports))
    return sorted(curated - host), sorted(host - curated)


def host_globals(readelf: str, host_elf: str) -> set[str]:
    """GLOBAL FUNC/OBJECT exports of the host firmware ELF."""
    return symbols_mod.get_host_globals(readelf, host_elf, symbol_types=("FUNC", "OBJECT"))


def classify(und: list[str], curated: set[str], host_view: set[str] | None,
             entry: str = "") -> GateResult:
    """Sort undefined symbols into ok / unconfirmed / broken.

    With host_view, anything outside the curated+host union is broken: the load
    fails on it. Without one, anything outside the curated+libc surface is
    unconfirmed until a host ELF says otherwise.
    """
    available = curated | LIBC_ALLOWLIST
    ok: list[str] = []
    unconfirmed: list[str] = []
    broken: list[str] = []
    for name in und:
        if host_view is not None:
            if name in available or name in host_view:
                ok.append(name)
            else:
                broken.append(name)
        else:
            (ok if name in available else unconfirmed).append(name)
    return GateResult(ok=ok, unconfirmed=unconfirmed, broken=broken, entry=entry)


def run_readelf(readelf: str, *args: str) -> str:
    """readelf stdout; raises with the tool's own error when it exits non-zero."""
    cmd = [readelf, *args]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise RuntimeError(f"readelf {args[0]} failed ({cmd}): {detail}")
    return result.stdout


def check_app(info, readelf: str | None, host_elf: str | None) -> GateResult:
    """Gate a built app; raises when the artifact itself is invalid."""
    tool = readelf or info.readelf or "xtensa-esp32s3-elf-readelf"

    header_out = run_readelf(tool, "-h", "-W", info.out)
    header = parse_readelf_h(header_out)
    if not header.is_little_endian:
        raise RuntimeError(
            f"{os.path.basename(info.out)} is {header.data}; the loader only accepts "
            "little-endian ELFs (use the espressif xtensa-esp-elf toolchain)"
        )
    if not header.is_shared:
        raise RuntimeError(
            f"{os.path.basename(info.out)} is {header.etype}, expected DYN"
        )

    syms_out = run_readelf(tool, "-s", "-W", info.out)
    app_main = find_app_main(syms_out)
    app_main_stripped = app_main is None
    if app_main is not None and header.entry:
        # readelf on dash-h prefixes hex with 0x, the symtab omits it. Normalize
        # through int() so both columns compare as addresses.
        entry_addr = int(header.entry, 16)
        app_main_addr = int(app_main, 16)
        if entry_addr != app_main_addr:
            raise RuntimeError(
                f"entry {header.entry} does not match app_main at {app_main}; the "
                "ELF entry must be the app_main symbol (-e app_main)"
            )

    und = parse_undefined(syms_out)
    curated = read_curated(info.sdk_root)
    host_view = host_globals(tool, host_elf) if host_elf else None
    result = classify(und, curated, host_view, entry=header.entry)
    result.app_main_stripped = app_main_stripped
    sections_out = run_readelf(tool, "-S", "-W", info.out)
    result.init_array_size = init_array_size(sections_out)
    return result