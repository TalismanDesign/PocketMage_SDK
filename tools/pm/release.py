"""Release version bumping.

A release rewrites VERSION, the pocketmage_sdk_version literal in
pocketmage_globals.cpp, the PlatformIO library.json version, and the CHANGELOG
entry, so no other file keeps a version of its own.
"""

from __future__ import annotations

import datetime
import json
import os
import re

from tools.pm.app import PmError

_VERSION_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)$")
_ABI_LITERAL_RE = re.compile(r'pocketmage_sdk_version\[\s*\]\s*=\s*"([^"]+)"')

_CHANGELOG_UNRELEASED = "## [Unreleased]"
_CHANGELOG_HEADING = re.compile(r"^## \[.+\]")

_CANONICAL_PART = re.compile(r"0|[1-9]\d*")


def bump_version(version: str, part: str) -> str:
    """Next semver for major/minor/patch; rejects leading-zero components."""
    m = _VERSION_RE.match(version)
    if not m or any(not _CANONICAL_PART.fullmatch(g) for g in m.groups()):
        raise PmError(f"VERSION is not a plain semver triple: {version!r}")
    major, minor, patch = (int(x) for x in version.split("."))
    if part == "major":
        return f"{major + 1}.0.0"
    if part == "minor":
        return f"{major}.{minor + 1}.0"
    if part == "patch":
        return f"{major}.{minor}.{patch + 1}"
    raise PmError(f"unknown release part {part!r}; use major, minor, or patch")


def _rewrite_file(path: str, old: str, new: str) -> None:
    with open(path, encoding="utf-8") as f:
        content = f.read()
    if old not in content:
        raise PmError(f"{path} does not contain {old}; refusing to rewrite")
    with open(path, "w", encoding="utf-8") as f:
        f.write(content.replace(old, new, 1))


def _changelog_path(root: str) -> str:
    return os.path.join(root, "CHANGELOG.md")


def _read_changelog(root: str) -> list[str]:
    path = _changelog_path(root)
    if os.path.isfile(path):
        with open(path, encoding="utf-8") as f:
            return f.read().splitlines()
    return []


def _write_changelog(root: str, lines: list[str]) -> None:
    text = "\n".join(lines).rstrip() + "\n"
    with open(_changelog_path(root), "w", encoding="utf-8") as f:
        f.write(text)


def _fold_unreleased(lines: list[str], version: str, message: str | None,
                     today: str) -> list[str]:
    """Move Unreleased bullets under the new version heading.

    The Unreleased section stays on top so contributors keep adding to it; the
    bullets that accumulated there become the new version's body.
    """
    title: list[str] = []
    rest = list(lines)
    if rest and rest[0].startswith("# ") and not rest[0].startswith("## "):
        title.append(rest.pop(0))
        if rest and not rest[0].strip():
            title.append(rest.pop(0))

    unreleased_idx = None
    for i, line in enumerate(rest):
        if line.rstrip() == _CHANGELOG_UNRELEASED:
            unreleased_idx = i
            break

    moved: list[str] = []
    if unreleased_idx is not None:
        j = unreleased_idx + 1
        while j < len(rest) and not _CHANGELOG_HEADING.match(rest[j]):
            moved.append(rest[j])
            j += 1
        rest = rest[:unreleased_idx] + rest[j:]

    section = [f"## [{version}] - {today}", ""]
    if message:
        section += [f"- {message}", ""]
    section += [line for line in moved if line.strip()] + [""]

    return title + [_CHANGELOG_UNRELEASED, ""] + section + rest


def release(root: str, part: str, message: str | None) -> str:
    """Bump the version everywhere. Returns the new version string.

    Every source is validated before the first write so a halfway release never
    leaves VERSION and the generated files disagreeing.
    """
    version_file = os.path.join(root, "VERSION")
    with open(version_file, encoding="utf-8") as f:
        version = f.read().strip()
    new_version = bump_version(version, part)
    today = datetime.date.today().isoformat()

    globals_cpp = os.path.join(root, "pocketmage_globals.cpp")
    with open(globals_cpp, encoding="utf-8") as f:
        globals_content = f.read()
    old_literal = f'pocketmage_sdk_version[] = "{version}"'
    if old_literal not in globals_content:
        raise PmError(f"pocketmage_globals.cpp has no {old_literal}; refusing to rewrite")

    library_json = os.path.join(root, "library.json")
    with open(library_json, encoding="utf-8") as f:
        lib = json.load(f)
    if lib.get("version") != version:
        raise PmError(
            f"library.json version {lib.get('version')!r} does not match VERSION {version!r}"
        )

    changelog = _read_changelog(root)
    next_changelog = _fold_unreleased(changelog, new_version, message, today)

    _rewrite_file(version_file, version, new_version)
    with open(globals_cpp, "w", encoding="utf-8") as f:
        f.write(
            globals_content.replace(
                old_literal, f'pocketmage_sdk_version[] = "{new_version}"', 1
            )
        )
    lib["version"] = new_version
    with open(library_json, "w", encoding="utf-8") as f:
        json.dump(lib, f, indent=4)
        f.write("\n")
    _write_changelog(root, next_changelog)

    return new_version