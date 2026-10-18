
from __future__ import annotations

import os
import re
import shutil
import subprocess
from dataclasses import dataclass

from tools.pm.output import UI

_VERSION_ABI_RE = re.compile(r'pocketmage_sdk_version\s*\[\s*\]\s*=\s*"([^"]+)"')

_SDK_MARKERS = ("tools/app.mk", "symbols.list", "pocketmage_globals.cpp")


def _is_sdk_root(path: str) -> bool:
    return all(os.path.isfile(os.path.join(path, m)) for m in _SDK_MARKERS)


def _parent_chain(path: str) -> list[str]:
    path = os.path.abspath(path)
    chain = []
    while True:
        chain.append(path)
        parent = os.path.dirname(path)
        if parent == path:
            break
        path = parent
    return chain


@dataclass
class AppInfo:

    path: str
    name: str
    out: str
    tar: str
    icon: str
    sdk_root: str
    readelf: str
    tool: str = ""
    map: str = ""


class PmError(Exception):


def sdk_root() -> str:
    env = os.environ.get("PM_SDK_ROOT")
    if env:
        candidate = os.path.abspath(env)
        if _is_sdk_root(candidate):
            return candidate
        raise PmError(f"PM_SDK_ROOT={env} is not a PocketMage SDK root")

    checkout = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    if _is_sdk_root(checkout):
        return checkout

    for parent in _parent_chain(os.getcwd()):
        if _is_sdk_root(parent):
            return parent

    raise PmError(
        "could not locate the SDK checkout. Set PM_SDK_ROOT to the repo root, "
        "or run pm from inside a PocketMage_SDK checkout."
    )


def sdk_version() -> str:
    path = os.path.join(sdk_root(), "VERSION")
    with open(path, encoding="utf-8") as f:
        version = f.read().strip()
    if not version:
        raise PmError(f"{path} is empty")
    return version


def abi_version() -> str:
    path = os.path.join(sdk_root(), "pocketmage_globals.cpp")
    with open(path, encoding="utf-8") as f:
        content = f.read()
    m = _VERSION_ABI_RE.search(content)
    if not m:
        raise PmError(f"pocketmage_sdk_version not found in {path}")
    return m.group(1)


def _parse_info(output: str) -> dict[str, str]:
    info: dict[str, str] = {}
    for line in output.splitlines():
        line = line.rstrip()
        if not line or "=" not in line:
            continue
        key, value = line.split("=", 1)
        info[key] = value
    return info


def _make_env() -> dict[str, str]:
    env = dict(os.environ)
    env["PM_SDK_ROOT"] = sdk_root()
    return env


def query_app(app_dir: str) -> AppInfo:

    def run_make_info(path: str) -> str:
        cmd = ["make", "-C", path, "-s", "pm-info"]
        result = subprocess.run(cmd, capture_output=True, text=True, env=_make_env())
        if result.returncode != 0:
            detail = result.stderr.strip() or result.stdout.strip()
            raise PmError(f"make pm-info failed in {path}: {detail}")
        return result.stdout

    path = os.path.abspath(app_dir)
    info = _parse_info(run_make_info(path))

    required = ("APP_NAME", "APP_OUT", "APP_TAR", "APP_ICON", "SDK_ROOT")
    missing = [k for k in required if not info.get(k)]
    if missing:
        raise PmError(f"pm-info in {path} is incomplete: missing {', '.join(missing)}")

    def resolve(rel: str) -> str:
        return rel if os.path.isabs(rel) else os.path.join(path, rel)

    return AppInfo(
        path=path,
        name=info["APP_NAME"],
        out=resolve(info["APP_OUT"]),
        tar=resolve(info["APP_TAR"]),
        icon=resolve(info["APP_ICON"]),
        sdk_root=info["SDK_ROOT"],
        readelf=info.get("XTENSA_READELF", ""),
        tool=resolve(info["APP_TOOL"]) if info.get("APP_TOOL") else "",
        map=resolve(info["APP_MAP"]) if info.get("APP_MAP") else "",
    )


def run_make(ui: UI, app_dir: str, targets: list[str]) -> int:
    cmd = ["make", "-C", app_dir, *targets]
    ui.command(cmd)
    result = subprocess.run(cmd, env=_make_env())
    return result.returncode


def find_makefile(app_dir: str) -> str | None:
    for candidate in ("Makefile", "makefile", "GNUmakefile"):
        path = os.path.join(app_dir, candidate)
        if os.path.isfile(path):
            return path
    return None


def require_toolchain(readelf: str) -> None:
    if readelf:
        return
    probe = shutil.which("xtensa-esp32s3-elf-readelf")
    if probe:
        return
    raise PmError(
        "no xtensa toolchain on PATH. Install the espressif xtensa-esp-elf release "
        "(see docs/docs/build.md) so app.mk's probe resolves it."
    )