"""`pm new`: scaffold an app from the official PocketMage_App template."""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile

from tools.pm.app import PmError

_NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")
_TEMPLATE_URL = os.environ.get(
    "PM_TEMPLATE_REPO",
    "https://github.com/TalismanDesign/PocketMage_App",
)


def _resolve(dest_dir: str, name: str) -> str:
    return os.path.abspath(os.path.join(dest_dir, name))


def _copy_tree(src: str, dst: str) -> None:
    for entry in os.scandir(src):
        if entry.name == ".git":
            continue
        src_path = entry.path
        dst_path = os.path.join(dst, entry.name)
        if entry.is_dir():
            shutil.copytree(src_path, dst_path)
        else:
            shutil.copy2(src_path, dst_path)


def _regenerate_icon(app_path: str, name: str) -> None:
    """Pre-generate <name>_ICON.bin from the template artwork. Needs Pillow;
    when it is missing the build-time Makefile rule regenerates it."""
    icon_src = os.path.join(app_path, "assets", "icon.png")
    icon_out = os.path.join(app_path, f"{name}_ICON.bin")
    gen = os.path.join(app_path, "tools", "make_icon.py")
    if not (os.path.isfile(icon_src) and os.path.isfile(gen)):
        return
    result = subprocess.run(
        ["python3", gen, icon_src, icon_out], capture_output=True, text=True
    )
    if result.returncode != 0:
        print(
            f"pm: warning: could not pre-generate {name}_ICON.bin "
            f"(run `make pack` to build it): "
            f"{result.stderr.strip() or result.stdout.strip()}"
        )


def scaffold_from_dir(name: str, dest_dir: str, template_dir: str) -> str:
    """Copy a template directory to <dest_dir>/<name>; returns the app path.
    Separated from scaffold() so tests exercise a local fixture and the clone
    stays a thin network wrapper."""
    if not _NAME_RE.match(name):
        raise PmError(
            f"invalid app name {name!r}: use [A-Za-z][A-Za-z0-9_]* "
            "(the name becomes the ELF, tar, icon, and slot name)"
        )

    app_path = _resolve(dest_dir, name)
    if os.path.exists(app_path) and os.listdir(app_path):
        raise PmError(f"{app_path} exists and is not empty; refusing to overwrite")

    os.makedirs(app_path, exist_ok=True)
    _copy_tree(template_dir, app_path)
    _regenerate_icon(app_path, name)
    return app_path


def scaffold(name: str, dest_dir: str = ".") -> str:
    """Scaffold an app called <name> from the official template repo; returns
    the app path. The app name matches _NAME_RE; it becomes the ELF, tar, and
    icon name via the Makefile's APP_NAME default."""
    with tempfile.TemporaryDirectory(prefix="pm-new-") as work_dir:
        clone_dir = os.path.join(work_dir, "template")
        result = subprocess.run(
            ["git", "clone", "--quiet", "--depth", "1", _TEMPLATE_URL, clone_dir],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            detail = result.stderr.strip() or result.stdout.strip()
            raise PmError(
                f"could not fetch the app template from {_TEMPLATE_URL}: {detail}"
            )
        return scaffold_from_dir(name, dest_dir, clone_dir)