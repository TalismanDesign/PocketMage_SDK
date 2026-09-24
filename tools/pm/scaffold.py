"""`pm new`: scaffold an app from examples/hello_app."""

from __future__ import annotations

import os
import re
import shutil

from tools.pm.app import PmError, sdk_root

_NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")


def _resolve(dest_dir: str, name: str) -> str:
    return os.path.abspath(os.path.join(dest_dir, name))


def scaffold(name: str, dest_dir: str = ".") -> str:
    """Copy examples/hello_app to <dest_dir>/<name>; returns the app path."""
    if not _NAME_RE.match(name):
        raise PmError(
            f"invalid app name {name!r}: use [A-Za-z][A-Za-z0-9_]* "
            "(the name becomes the ELF, tar, icon, and slot name)"
        )

    root = sdk_root()
    template = os.path.join(root, "examples", "hello_app")
    template_main = os.path.join(template, "main.cpp")
    template_icon = os.path.join(template, "hello_app_ICON.bin")
    for required in (template_main, template_icon):
        if not os.path.isfile(required):
            raise PmError(f"template is missing {required}")

    app_path = _resolve(dest_dir, name)
    if os.path.exists(app_path) and os.listdir(app_path):
        raise PmError(f"{app_path} exists and is not empty; refusing to overwrite")

    os.makedirs(app_path, exist_ok=True)
    shutil.copy2(template_main, os.path.join(app_path, "main.cpp"))
    shutil.copy2(template_icon, os.path.join(app_path, f"{name}_ICON.bin"))

    # The template Makefile does `SDK_ROOT ?= $(abspath ../..)`, which only
    # holds inside the repo. Emit an absolute path for an anywhere scaffold.
    makefile = (
        f"SDK_ROOT ?= {root}\n"
        f"include $(SDK_ROOT)/tools/app.mk\n"
    )
    with open(os.path.join(app_path, "Makefile"), "w", encoding="utf-8") as f:
        f.write(makefile)

    return app_path