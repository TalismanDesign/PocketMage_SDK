"""Version bumping and the cross-file release rewrite."""

import json
import os
import tempfile
import unittest

from tools.pm import release as release_mod


def make_root(tmp: str, version: str = "0.1.0",
              changelog: str | None = None) -> str:
    root = os.path.join(tmp, "sdk")
    os.makedirs(root, exist_ok=True)
    with open(os.path.join(root, "VERSION"), "w") as f:
        f.write(version + "\n")
    with open(os.path.join(root, "pocketmage_globals.cpp"), "w") as f:
        f.write(f'extern "C" const char pocketmage_sdk_version[] = "{version}";\n')
    with open(os.path.join(root, "library.json"), "w") as f:
        f.write(json.dumps({"name": "pocketmagesdk", "version": version}) + "\n")
    if changelog is not None:
        with open(os.path.join(root, "CHANGELOG.md"), "w") as f:
            f.write(changelog)
    return root


class BumpVersion(unittest.TestCase):
    def test_patch_minor_major_rollovers(self):
        self.assertEqual(release_mod.bump_version("0.1.0", "patch"), "0.1.1")
        self.assertEqual(release_mod.bump_version("0.1.9", "minor"), "0.2.0")
        self.assertEqual(release_mod.bump_version("0.9.9", "major"), "1.0.0")

    def test_malformed_version_rejected(self):
        for bad in ("v0.1.0", "0.1", "1.2.3.4", "0.01.0"):
            with self.subTest(bad=bad):
                with self.assertRaises(release_mod.PmError):
                    release_mod.bump_version(bad, "patch")

    def test_unknown_part_rejected(self):
        with self.assertRaises(release_mod.PmError):
            release_mod.bump_version("0.1.0", "banana")


class ReleaseFile(unittest.TestCase):
    def test_rewrites_every_version_source(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = make_root(tmp, changelog="# Changelog\n\n## [Unreleased]\n\n- initial\n")
            self.assertEqual(release_mod.release(root, "minor", None), "0.2.0")

            with open(os.path.join(root, "VERSION")) as f:
                self.assertEqual(f.read().strip(), "0.2.0")
            with open(os.path.join(root, "pocketmage_globals.cpp")) as f:
                self.assertIn('pocketmage_sdk_version[] = "0.2.0"', f.read())
            with open(os.path.join(root, "library.json")) as f:
                self.assertEqual(json.load(f)["version"], "0.2.0")
            with open(os.path.join(root, "CHANGELOG.md")) as f:
                text = f.read()
            self.assertIn("## [Unreleased]", text)
            self.assertIn("## [0.2.0] - ", text)
            self.assertIn("- initial", text)

    def test_changelog_created_when_missing(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = make_root(tmp)
            release_mod.release(root, "patch", "fix widget")
            with open(os.path.join(root, "CHANGELOG.md")) as f:
                text = f.read()
            self.assertIn("## [Unreleased]", text)
            self.assertIn("## [0.1.1] - ", text)
            self.assertIn("fix widget", text)

    def test_message_becomes_a_bullet(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = make_root(
                tmp, version="1.0.0",
                changelog="# Changelog\n\n## [Unreleased]\n\n## [1.0.0] - 2026-01-01\n- old\n",
            )
            release_mod.release(root, "major", "big change")
            with open(os.path.join(root, "CHANGELOG.md")) as f:
                text = f.read()
            self.assertIn("## [2.0.0] - ", text)
            self.assertIn("- big change", text)
            self.assertIn("- old", text)

    def test_dangling_library_version_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = make_root(tmp)
            with open(os.path.join(root, "library.json"), "w") as f:
                f.write(json.dumps({"name": "pocketmagesdk", "version": "9.9.9"}) + "\n")
            with self.assertRaises(release_mod.PmError):
                release_mod.release(root, "patch", None)
            with open(os.path.join(root, "VERSION")) as f:
                self.assertEqual(f.read().strip(), "0.1.0")


if __name__ == "__main__":
    unittest.main()