#!/usr/bin/env python3
"""Tests for pocketmage_i18n/tools/gen_i18n.py catalog merging."""

import importlib.util
import os
import tempfile
import unittest

TOOLS_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                         "tools")
GEN_PATH = os.path.join(TOOLS_DIR, "gen_i18n.py")

_spec = importlib.util.spec_from_file_location("gen_i18n", GEN_PATH)
gen_i18n = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gen_i18n)

BASE_EN = """\
# Languages: en fr

msgctxt "STR_KB_APP_CANCEL"
msgid "Cancel"
msgstr "Cancel"

msgctxt "STR_KB_APP_LOADER"
msgid "Loading"
msgstr "Loading"

msgctxt "STR_GRID_TXT"
msgid "Text"
msgstr "Text"

msgctxt "STR_GRID_LOADER"
msgid "Loader"
msgstr "Loader"

msgctxt "STR_DAY_SUNDAY"
msgid "Sunday"
msgstr "Sunday"

msgctxt "STR_DAY_SATURDAY"
msgid "Saturday"
msgstr "Saturday"

msgctxt "STR_MONTH_JAN"
msgid "January"
msgstr "January"

msgctxt "STR_MONTH_ERR"
msgid "ERR"
msgstr "ERR"

msgctxt "STR_SYS_HELLO"
msgid "Hello"
msgstr "Hello"
"""

BASE_FR = BASE_EN.replace("Bonjour", "Bonjour").replace(
    'msgid "Cancel"\nmsgstr "Cancel"', 'msgid "Cancel"\nmsgstr "Annuler"'
).replace(
    'msgid "Loading"\nmsgstr "Loading"', 'msgid "Loading"\nmsgstr "Chargement"'
).replace(
    'msgid "Text"\nmsgstr "Text"', 'msgid "Text"\nmsgstr "Texte"'
).replace(
    'msgid "Loader"\nmsgstr "Loader"', 'msgid "Loader"\nmsgstr "Chargeur"'
).replace(
    'msgid "Sunday"\nmsgstr "Sunday"', 'msgid "Sunday"\nmsgstr "Dimanche"'
).replace(
    'msgid "Saturday"\nmsgstr "Saturday"', 'msgid "Saturday"\nmsgstr "Samedi"'
).replace(
    'msgid "January"\nmsgstr "January"', 'msgid "January"\nmsgstr "Janvier"'
).replace(
    'msgid "Hello"\nmsgstr "Hello"', 'msgid "Hello"\nmsgstr "Bonjour"'
)

APP_EN = """\
msgctxt "STR_TERM_PROMPT"
msgid "Prompt"
msgstr "Prompt"
"""

APP_FR = """\
msgctxt "STR_TERM_PROMPT"
msgid "Prompt"
msgstr "Invite"
"""


class CatalogCase(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = self._tmp.name
        self.addCleanup(self._tmp.cleanup)

    def write(self, relpath, text):
        path = os.path.join(self.root, relpath)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(text)

    def make_base(self, en=BASE_EN, fr=BASE_FR):
        self.write("base/en/pocketmage_i18n.po", en)
        if fr is not None:
            self.write("base/fr/pocketmage_i18n.po", fr)
        return os.path.join(self.root, "base")

    def make_app(self, en=APP_EN, fr=APP_FR):
        self.write("app/en/pocketmage_i18n.po", en)
        if fr is not None:
            self.write("app/fr/pocketmage_i18n.po", fr)
        return os.path.join(self.root, "app")

    def gen(self, catalogs):
        out = os.path.join(self.root, "out")
        gen_i18n.generate(catalogs, out)
        with open(os.path.join(out, "pocketmage_i18n_gen.h"),
                  encoding="utf-8") as fh:
            header = fh.read()
        with open(os.path.join(out, "pocketmage_i18n_gen.cpp"),
                  encoding="utf-8") as fh:
            cpp = fh.read()
        return header, cpp


class MergeTests(CatalogCase):
    def test_base_alone_emits_all_strings(self):
        header, _ = self.gen([self.make_base()])
        self.assertIn("_STR_COUNT = 9", header)
        self.assertIn("French = 1", header)

    def test_app_strings_append_after_base(self):
        header, cpp = self.gen([self.make_base(), self.make_app()])
        self.assertIn("_STR_COUNT = 10", header)
        self.assertIn("STR_TERM_PROMPT,", header)
        self.assertLess(cpp.index("Hello"), cpp.index("Prompt"))

    def test_app_language_missing_falls_back_to_english(self):
        _, cpp = self.gen([self.make_base(), self.make_app(fr=None)])
        self.assertIn('"Hello",\n  "Prompt"', cpp)
        self.assertIn('"Bonjour",\n  "Prompt"', cpp)

    def test_helper_heads_are_pinned_to_their_index(self):
        header, _ = self.gen([self.make_base()])
        self.assertIn("STR_KB_APP_CANCEL = 0,", header)
        self.assertIn("STR_DAY_SUNDAY = 4,", header)
        self.assertIn("STR_MONTH_JAN = 6,", header)

    def test_aliases_merge_across_catalogs(self):
        self.write("app/fr/pocketmage_i18n.aliases", "invite -> STR_TERM_PROMPT\n")
        _, cpp = self.gen([self.make_base(), self.make_app()])
        self.assertIn('"invite",\n  "STR_TERM_PROMPT"', cpp)

    def test_alias_count_matches_array_contents(self):
        self.write("app/fr/pocketmage_i18n.aliases", "invite -> STR_TERM_PROMPT\n")
        _, cpp = self.gen([self.make_base(), self.make_app()])
        self.assertIn("kCommandAliasCounts[2] = { 0, 1 };", cpp)


class ValidationTests(CatalogCase):
    def test_duplicate_id_across_catalogs_rejected(self):
        app = self.make_app(en='msgctxt "STR_SYS_HELLO"\nmsgid "Hello"\nmsgstr "Hey"\n')
        with self.assertRaises(SystemExit):
            self.gen([self.make_base(), app])

    def test_duplicate_id_within_catalog_rejected(self):
        base = self.make_base(en=BASE_EN + '\nmsgctxt "STR_SYS_HELLO"\nmsgid "Hello"\nmsgstr "Hi"\n')
        with self.assertRaises(SystemExit):
            self.gen([base])

    def test_language_order_mismatch_rejected(self):
        base = self.make_base(fr=BASE_FR.replace(
            'msgctxt "STR_DAY_SATURDAY"', 'msgctxt "STR_DAY_TUESDAY"'))
        with self.assertRaises(SystemExit):
            self.gen([base])

    def test_msgid_mismatch_rejected(self):
        base = self.make_base(fr=BASE_FR.replace('msgid "Hello"', 'msgid "Hellos"'))
        with self.assertRaises(SystemExit):
            self.gen([base])

    def test_missing_base_language_header_rejected(self):
        base = self.make_base(
            en='msgctxt "STR_SYS_HELLO"\nmsgid "Hello"\nmsgstr "Hello"\n', fr=None)
        with self.assertRaises(SystemExit):
            self.gen([base])

    def test_bad_string_id_rejected(self):
        base = self.make_base(en=BASE_EN + '\nmsgctxt "lower_case"\nmsgid "x"\nmsgstr "x"\n')
        with self.assertRaises(SystemExit):
            self.gen([base])

    def test_duplicate_folded_alias_rejected(self):
        self.write("app/fr/pocketmage_i18n.aliases",
                   "invite -> STR_TERM_PROMPT\nInvite -> STR_TERM_PROMPT\n")
        with self.assertRaises(SystemExit):
            self.gen([self.make_base(), self.make_app()])

    def test_interleaved_helper_ranges_rejected(self):
        block = ('msgctxt "STR_DAY_SATURDAY"\nmsgid "Saturday"\nmsgstr "Saturday"\n\n')
        interleaved = BASE_EN.replace(block, "") + '\n' + block.rstrip("\n") + "\n"
        with self.assertRaises(SystemExit):
            self.gen([self.make_base(en=interleaved, fr=None)])

    def test_missing_helper_range_rejected(self):
        base = self.make_base(en=BASE_EN
                              .replace('msgctxt "STR_MONTH_JAN"\nmsgid "January"\nmsgstr "January"\n\n', "")
                              .replace('msgctxt "STR_MONTH_ERR"\nmsgid "ERR"\nmsgstr "ERR"\n\n', ""),
                              fr=None)
        with self.assertRaises(SystemExit):
            self.gen([base])


class ParseTests(unittest.TestCase):
    def test_parse_quoted_unescapes(self):
        self.assertEqual(gen_i18n.parse_quoted('"a\\nb\\"c"'), 'a\nb"c')

    def test_fold_strips_diacritics(self):
        self.assertEqual(gen_i18n.fold("Paramètres Général"), "parametres general")


if __name__ == "__main__":
    unittest.main()
