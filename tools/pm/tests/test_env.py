import os
import tempfile
import unittest
from unittest import mock

from tools.pm import env as env_mod

_FINDER_SOURCE = """\
from __future__ import annotations
import sys
MAPPING: dict[str, str] = {'tools': '/private/var/folders/xx/T/pmapp/sdk/tools'}
NAMESPACES: dict[str, list[str]] = {}
PATH_PLACEHOLDER = '...'
"""


def _make_sdk(root: str) -> None:
    os.makedirs(os.path.join(root, "tools"), exist_ok=True)
    for marker in env_mod.SDK_MARKERS:
        path = os.path.join(root, marker)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write("marker\n")
    with open(os.path.join(root, "VERSION"), "w", encoding="utf-8") as handle:
        handle.write("1.2.3\n")


class IsSdkRoot(unittest.TestCase):
    def test_all_markers_required(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertFalse(env_mod.is_sdk_root(tmp))
            _make_sdk(tmp)
            self.assertTrue(env_mod.is_sdk_root(tmp))


class DiscoverSdkRoot(unittest.TestCase):
    def test_explicit_valid_wins(self):
        with tempfile.TemporaryDirectory() as tmp:
            _make_sdk(tmp)
            self.assertEqual(env_mod.discover_sdk_root(tmp), os.path.abspath(tmp))

    def test_explicit_invalid_returns_none(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertIsNone(env_mod.discover_sdk_root(tmp))

    def test_env_var_used_when_no_explicit(self):
        with tempfile.TemporaryDirectory() as tmp:
            _make_sdk(tmp)
            with mock.patch.dict(os.environ, {"PM_SDK_ROOT": tmp}):
                self.assertEqual(env_mod.discover_sdk_root(), os.path.abspath(tmp))

    def test_cwd_ancestor_walk(self):
        with tempfile.TemporaryDirectory() as tmp:
            _make_sdk(tmp)
            deep = os.path.join(tmp, "a", "b")
            os.makedirs(deep, exist_ok=True)
            with mock.patch.dict(os.environ, {}, clear=True), \
                 mock.patch.object(env_mod.os, "getcwd", return_value=deep):
                self.assertEqual(env_mod.discover_sdk_root(), os.path.abspath(tmp))

    def test_nothing_found_returns_none(self):
        with tempfile.TemporaryDirectory() as tmp:
            with mock.patch.dict(os.environ, {}, clear=True), \
                 mock.patch.object(env_mod.os, "getcwd", return_value=tmp):
                self.assertIsNone(env_mod.discover_sdk_root())


class ParseMappingTarget(unittest.TestCase):
    def test_extracts_tools_target(self):
        target = env_mod._parse_mapping_target(_FINDER_SOURCE, "tools")
        self.assertEqual(target, "/private/var/folders/xx/T/pmapp/sdk/tools")

    def test_absent_package_returns_none(self):
        self.assertIsNone(env_mod._parse_mapping_target(_FINDER_SOURCE, "other"))

    def test_malformed_source_returns_none(self):
        self.assertIsNone(env_mod._parse_mapping_target("def (", "tools"))


class PipInstallCommand(unittest.TestCase):
    def test_editable_uses_sys_executable(self):
        cmd = env_mod.pip_install_command("/some/sdk", editable=True)
        self.assertEqual(cmd[0], env_mod.sys.executable)
        self.assertEqual(cmd[1:], ["-m", "pip", "install", "-e", "/some/sdk"])

    def test_non_editable_drops_e_flag(self):
        cmd = env_mod.pip_install_command("/some/sdk", editable=False)
        self.assertNotIn("-e", cmd)
        self.assertEqual(cmd[-1], "/some/sdk")


class CheckoutVersion(unittest.TestCase):
    def test_reads_version(self):
        with tempfile.TemporaryDirectory() as tmp:
            _make_sdk(tmp)
            self.assertEqual(env_mod.checkout_version(tmp), "1.2.3")

    def test_missing_version_is_none(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertIsNone(env_mod.checkout_version(tmp))


if __name__ == "__main__":
    unittest.main()
