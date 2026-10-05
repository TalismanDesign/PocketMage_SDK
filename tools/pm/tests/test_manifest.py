"""Unit tests for app.properties parsing and SDK scope gating.

The module under test is plain C with no Arduino dependency, so the build host
compiles it directly and runs the real parser rather than a copy of its logic.
"""

import os
import subprocess
import tempfile
import textwrap
import unittest

OS_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__),
                                      "..", "..", "..", "..", ".."))
SOURCE = os.path.join(OS_ROOT, "src", "elf_app_manifest.cpp")
HEADER = os.path.join(OS_ROOT, "include", "elf_app_manifest.h")

HARNESS = r"""
#include <elf_app_manifest.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  ElfAppManifest m;
  const char *text = $MANIFEST_TEXT;
  int fields = elfManifestParse(text, strlen(text), m);
  printf("FIELDS %d\n", fields);
  printf("NAME %s\n", m.name);
  printf("VERSION %s\n", m.version);
  printf("AUTHOR %s\n", m.author);
  printf("SCOPE %s\n", m.scope);

  ElfAppScope s;
  elfScopeFromManifest(m.scope, s);
  printf("ALL %d\n", s.all ? 1 : 0);
  printf("COUNT %d\n", s.count);
  for (int i = 0; i < s.count; i++) printf("PREFIX %s\n", s.prefix[i]);

  static const char *probes[] = {
      $PROBES
  };
  for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
    printf("ALLOW %s %d\n", probes[i],
           elfScopeAllows(s, probes[i]) ? 1 : 0);
  }
  printf("ALLOW_NULL %d\n", elfScopeAllows(s, NULL) ? 1 : 0);
  return 0;
}
"""


def c_string_literal(text):
    """Renders text as a C string literal, escaping quotes and newlines."""
    escaped = (text.replace("\\", "\\\\").replace('"', '\\"')
               .replace("\n", "\\n").replace("\r", "\\r"))
    return '"%s"' % escaped


class ManifestHarness:
    """Compiles the real parser once per test and runs a manifest through it."""

    def __init__(self, manifest, probes=()):
        with tempfile.TemporaryDirectory() as tmp:
            probe_list = "".join(c_string_literal(p) + ", " for p in probes)
            if not probe_list:
                probe_list = '""'
            source = (textwrap.dedent(HARNESS)
                      .replace("$MANIFEST_TEXT", c_string_literal(manifest))
                      .replace("$PROBES", probe_list))
            harness = os.path.join(tmp, "harness.cpp")
            with open(harness, "w", encoding="utf-8") as fh:
                fh.write(source)
            binary = os.path.join(tmp, "probe")
            subprocess.run(
                ["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                 "-I", os.path.dirname(HEADER), harness, SOURCE,
                 "-o", binary],
                check=True, capture_output=True)
            out = subprocess.run([binary], check=True, capture_output=True,
                                 text=True).stdout

        self._lines = out.splitlines()
        self.fields = int(self.value("FIELDS"))
        self.name = self.value("NAME")
        self.version = self.value("VERSION")
        self.author = self.value("AUTHOR")
        self.scope_text = self.value("SCOPE")
        self.all = bool(int(self.value("ALL")))
        self.count = int(self.value("COUNT"))
        self.prefixes = [line.split(" ", 1)[1]
                         for line in self._lines
                         if line.startswith("PREFIX ")]
        self.allows = {}
        self.allow_null = self.value("ALLOW_NULL") == "1"
        for line in self._lines:
            if line.startswith("ALLOW "):
                sym, verdict = line.split(" ")[1:3]
                self.allows[sym] = verdict == "1"

    def value(self, tag):
        for line in self._lines:
            if line.startswith(tag + " "):
                return line[len(tag) + 1:]
        raise AssertionError("no %s line in harness output" % tag)


class ManifestParsing(unittest.TestCase):
    def test_all_fields_are_read(self):
        h = ManifestHarness(
            "name=Stopwatch\nversion=1.2.0\nauthor=Ada\nscope=all\n")
        self.assertEqual(h.name, "Stopwatch")
        self.assertEqual(h.version, "1.2.0")
        self.assertEqual(h.author, "Ada")
        self.assertEqual(h.scope_text, "all")
        self.assertEqual(h.fields, 4)

    def test_missing_manifest_yields_empty_fields(self):
        h = ManifestHarness("")
        self.assertEqual(h.fields, 0)
        self.assertEqual(h.name, "")
        self.assertEqual(h.scope_text, "")

    def test_comments_and_blank_lines_are_ignored(self):
        h = ManifestHarness(
            "# a comment\n"
            "\n"
            "   ; another comment\n"
            "name=Notes\n"
            "\n")
        self.assertEqual(h.name, "Notes")
        self.assertEqual(h.fields, 1)

    def test_indentation_and_spacing_are_tolerated(self):
        h = ManifestHarness("   name   =   Stopwatch  \n")
        self.assertEqual(h.name, "Stopwatch")

    def test_unknown_keys_are_ignored(self):
        h = ManifestHarness("unknown=thing\nname=Notes\n")
        self.assertEqual(h.name, "Notes")
        self.assertEqual(h.fields, 1)

    def test_key_without_equals_is_not_a_field(self):
        h = ManifestHarness("name\n")
        self.assertEqual(h.name, "")
        self.assertEqual(h.fields, 0)

    def test_repeated_key_takes_the_last_value(self):
        h = ManifestHarness("name=First\nname=Second\n")
        self.assertEqual(h.name, "Second")

    def test_crlf_line_endings(self):
        h = ManifestHarness("name=Notes\r\nscope=eink\r\n")
        self.assertEqual(h.name, "Notes")
        self.assertEqual(h.scope_text, "eink")

    def test_value_longer_than_the_field_is_truncated(self):
        h = ManifestHarness("name=" + "N" * 80 + "\n")
        self.assertEqual(len(h.name), 31)

    def test_overlong_line_does_not_overflow(self):
        h = ManifestHarness("name=" + "N" * 400 + "\n")
        self.assertLessEqual(len(h.name), 31)

    def test_unterminated_last_line_is_parsed(self):
        h = ManifestHarness("name=Notes")
        self.assertEqual(h.name, "Notes")


class ScopeParsing(unittest.TestCase):
    def test_all_is_unrestricted(self):
        h = ManifestHarness("scope=all\n")
        self.assertTrue(h.all)
        self.assertEqual(h.count, 0)

    def test_empty_scope_is_unrestricted(self):
        h = ManifestHarness("scope=\n")
        self.assertTrue(h.all)

    def test_modules_become_prefixes(self):
        h = ManifestHarness("scope=eink,text\n")
        self.assertFalse(h.all)
        self.assertEqual(h.prefixes, ["pm_eink_", "pm_text_"])

    def test_pm_prefixed_tokens_are_normalized(self):
        h = ManifestHarness("scope=pm_eink,pm_text_\n")
        self.assertEqual(h.prefixes, ["pm_eink_", "pm_text_"])

    def test_tokens_are_trimmed(self):
        h = ManifestHarness("scope= eink , text \n")
        self.assertEqual(h.prefixes, ["pm_eink_", "pm_text_"])

    def test_all_anywhere_wins(self):
        h = ManifestHarness("scope=eink,all,text\n")
        self.assertTrue(h.all)
        self.assertEqual(h.count, 0)

    def test_case_of_all_is_ignored(self):
        h = ManifestHarness("scope=ALL\n")
        self.assertTrue(h.all)

    def test_too_many_tokens_truncates(self):
        h = ManifestHarness("scope=" + ",".join("m%d" % i for i in range(40)))
        self.assertEqual(h.count, 16)

    def test_unusable_tokens_leave_scope_unrestricted(self):
        h = ManifestHarness("scope=,,,\n")
        self.assertTrue(h.all)

    def test_overlong_token_is_dropped(self):
        h = ManifestHarness("scope=" + "x" * 80 + ",eink\n")
        self.assertEqual(h.prefixes, ["pm_eink_"])


class ScopeGating(unittest.TestCase):
    def allows(self, scope, probes):
        return ManifestHarness("scope=%s\n" % scope, probes).allows

    def test_listed_module_is_visible(self):
        self.assertTrue(self.allows("eink", ["pm_eink_clear"]))

    def test_unlisted_module_is_hidden(self):
        self.assertIs(self.allows("eink", ["pm_wifi_connect"])["pm_wifi_connect"],
                      False)

    def test_prefix_match_is_not_a_substring_match(self):
        result = self.allows("eink", ["pm_einkfoo_bar"])
        self.assertIs(result["pm_einkfoo_bar"], False)

    def test_non_pm_symbols_stay_visible(self):
        probes = ["malloc", "strcasecmp", "sqrtf", "esp_timer_get_time"]
        result = self.allows("eink", probes)
        for probe in probes:
            self.assertIs(result[probe], True, probe)

    def test_scoped_module_with_prefixed_token(self):
        self.assertTrue(self.allows("pm_eink", ["pm_eink_rect"]))

    def test_all_scope_admits_everything(self):
        self.assertTrue(self.allows("all", ["pm_wifi_connect", "pm_eink_rect"]))

    def test_null_symbol_name_does_not_crash_the_gate(self):
        self.assertTrue(ManifestHarness("scope=eink\n").allow_null)

    def test_empty_symbol_name_is_visible(self):
        self.assertTrue(self.allows("eink", [""]))


if __name__ == "__main__":
    unittest.main()
