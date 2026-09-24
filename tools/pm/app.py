"""App project and make interop.

pm is a wrapper around make. The build logic lives in tools/app.mk; pm asks
make for the app's pm-info, then runs make for the actual targets.
"""

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
    """The app's own configuration, parsed from `make -s pm-info`.

    Paths come back relative to the app directory; resolve() anchors them to
    the app path so pm never depends on the caller's cwd.
    """

    path: str
    name: str
    out: str
    tar: str
    icon: str
    sdk_root: str
    readelf: str


class PmError(Exception):
    """Expected failure with a message safe to show the user."""


def sdk_root() -> str:
    """Find the SDK checkout pm should operate on.

    pip installs tools/ into site-packages, so the package location is not the
    source tree. Order: PM_SDK_ROOT, the checkout this file lives in, then an
    ancestor of the current directory.
    """
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
    """SDK release version from VERSION.

    pip and pm read this file; pocketmage_globals.cpp carries the same string
    at runtime and abi_version() guards it from drifting.
    """
    path = os.path.join(sdk_root(), "VERSION")
    with open(path, encoding="utf-8") as f:
        version = f.read().strip()
    if not version:
        raise PmError(f"{path} is empty")
    return version


def abi_version() -> str:
    """The pocketmage_sdk_version literal exported to the host at runtime."""
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


def query_app(app_dir: str) -> AppInfo:
    """Run `make -s pm-info` in the app and parse the result."""

    def run_make_info(path: str) -> str:
        cmd = ["make", "-C", path, "-s", "pm-info"]
        result = subprocess.run(cmd, capture_output=True, text=True)
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
        readelf=info.get("XTENSA_READELF") or "",
    )


def run_make(ui: UI, app_dir: str, targets: list[str]) -> int:
    """make <targets> in the app, streaming output. Returns the exit code."""
    cmd = ["make", "-C", app_dir, *targets]
    ui.command(cmd)
    result = subprocess.run(cmd)
    return result.returncode


def find_makefile(app_dir: str) -> str | None:
    """Path to the app's Makefile, or None."""
    for candidate in ("Makefile", "makefile", "GNUmakefile"):
        path = os.path.join(app_dir, candidate)
        if os.path.isfile(path):
            return path
    return None


def require_toolchain(readelf: str) -> None:
    """Fail fast when no toolchain is resolvable for a build."""
    if readelf:
        return
    probe = shutil.which("xtensa-esp32s3-elf-readelf")
    if probe:
        return
    raise PmError(
        "no xtensa toolchain on PATH. Install the espressif xtensa-esp-elf release "
        "(see docs/docs/build.md) so app.mk's probe resolves it."
    )