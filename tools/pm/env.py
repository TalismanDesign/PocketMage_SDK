"""Environment introspection and self-repair primitives for the pm CLI."""

from __future__ import annotations

import os
import subprocess
import sys
from dataclasses import dataclass

SDK_MARKERS = ("tools/app.mk", "symbols.list", "pocketmage_globals.cpp")
DIST_NAME = "pocketmage-sdk"
_ROOT_PACKAGE = "tools"


def is_sdk_root(path: str) -> bool:
    return all(os.path.isfile(os.path.join(path, marker)) for marker in SDK_MARKERS)


def _ancestors(path: str) -> list[str]:
    path = os.path.abspath(path)
    chain = []
    while True:
        chain.append(path)
        parent = os.path.dirname(path)
        if parent == path:
            break
        path = parent
    return chain


def discover_sdk_root(explicit: str | None = None) -> str | None:
    """Resolve a live SDK checkout without importing `tools`."""
    for candidate in (explicit, os.environ.get("PM_SDK_ROOT")):
        if candidate:
            resolved = os.path.abspath(os.path.expanduser(candidate))
            if is_sdk_root(resolved):
                return resolved
            return None
    for parent in _ancestors(os.getcwd()):
        if is_sdk_root(parent):
            return parent
    return None


def _read_version(sdk_root: str) -> str | None:
    try:
        with open(os.path.join(sdk_root, "VERSION"), encoding="utf-8") as handle:
            return handle.read().strip() or None
    except OSError:
        return None


@dataclass
class InstallInfo:
    """What the running interpreter believes about the pocketmage-sdk install."""

    installed: bool
    version: str | None
    editable: bool
    editable_target: str | None
    target_exists: bool | None
    importable: bool


def _editable_target() -> str | None:
    """The directory an editable install maps the `tools` package to."""
    try:
        import importlib.metadata as md

        dist = md.distribution(DIST_NAME)
    except Exception:
        return None

    files = dist.files or []
    for entry in files:
        name = os.path.basename(str(entry))
        if name.startswith("__editable__") and name.endswith("_finder.py"):
            try:
                source = (dist.locate_file(entry)).read_text(encoding="utf-8")
            except OSError:
                return None
            return _parse_mapping_target(source, _ROOT_PACKAGE)
    for entry in files:
        if os.path.basename(str(entry)).startswith("__editable__") and str(entry).endswith(".pth"):
            try:
                line = (dist.locate_file(entry)).read_text(encoding="utf-8").strip()
            except OSError:
                return None
            # A .pth may contain an import hook line; only a bare path is useful.
            if line and not line.startswith("import ") and os.path.isabs(line):
                return line
    return None


def _parse_mapping_target(finder_source: str, package: str) -> str | None:
    """Pull MAPPING['<package>'] out of a setuptools editable finder module."""
    import ast

    try:
        tree = ast.parse(finder_source)
    except SyntaxError:
        return None
    for node in ast.walk(tree):
        # The finder spells MAPPING with a type annotation, so it parses as an
        # AnnAssign; accept a plain Assign too in case the format changes.
        if isinstance(node, ast.AnnAssign):
            name = node.target.id if isinstance(node.target, ast.Name) else None
            targets = [name] if name else []
        elif isinstance(node, ast.Assign):
            targets = [t.id for t in node.targets if isinstance(t, ast.Name)]
        else:
            continue
        if "MAPPING" not in targets or not isinstance(node.value, ast.Dict):
            continue
        for key, value in zip(node.value.keys, node.value.values):
            if (
                isinstance(key, ast.Constant)
                and key.value == package
                and isinstance(value, ast.Constant)
                and isinstance(value.value, str)
            ):
                return value.value
    return None


def inspect_install() -> InstallInfo:
    """Describe the current pocketmage-sdk install from the running interpreter."""
    try:
        import importlib.metadata as md

        version = md.version(DIST_NAME)
        installed = True
    except Exception:
        version = None
        installed = False

    target = _editable_target()
    # Strip a trailing /tools so the reported target is the SDK root the user
    # can act on, matching what --sdk expects.
    editable_root = target
    if target and os.path.basename(os.path.normpath(target)) == _ROOT_PACKAGE:
        editable_root = os.path.dirname(os.path.normpath(target))

    target_exists = None if target is None else os.path.isdir(target)

    try:
        import tools  # noqa: F401

        importable = True
    except Exception:
        importable = False

    return InstallInfo(
        installed=installed,
        version=version,
        editable=target is not None,
        editable_target=editable_root,
        target_exists=target_exists,
        importable=importable,
    )


def pip_install_command(sdk_root: str, *, editable: bool = True) -> list[str]:
    cmd = [sys.executable, "-m", "pip", "install"]
    if editable:
        cmd.append("-e")
    cmd.append(sdk_root)
    return cmd


def run_pip_install(sdk_root: str, *, editable: bool = True) -> int:
    """Reinstall the SDK package; returns the pip exit code."""
    return subprocess.run(pip_install_command(sdk_root, editable=editable)).returncode


def checkout_version(sdk_root: str) -> str | None:
    return _read_version(sdk_root)
