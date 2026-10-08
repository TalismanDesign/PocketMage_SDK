"""Tests for the export-table generator, tools/symbols.py."""

import os
import tempfile
import unittest

from tools import symbols as symbols_mod
from tools.pm import gate as gate_mod


class BuiltinDetection(unittest.TestCase):
    def test_builtin_prefixes_are_recognized(self):
        for name in ("__atomic_fetch_add_4", "__sync_synchronize",
                     "__builtin_trap"):
            self.assertTrue(symbols_mod.is_gcc_builtin(name), name)

    def test_ordinary_symbols_are_not_builtins(self):
        for name in ("memcpy", "pm_eink_pixel", "_Znwj", "__adddf3"):
            self.assertFalse(symbols_mod.is_gcc_builtin(name), name)


class BuiltinTableEmission(unittest.TestCase):
    def _generate(self, curated, runtime):
        out = tempfile.NamedTemporaryFile(suffix=".cpp", delete=False)
        out.close()
        self.addCleanup(os.unlink, out.name)
        symbols_mod.save_c_file(
            curated, out.name, "customer",
            cpp=True, include_headers=["pm_app_api.h"],
            runtime_symbols=set(runtime))
        with open(out.name, encoding="utf-8") as handle:
            return handle.read(), out.name

    def test_builtin_gets_asm_alias_and_string_entry(self):
        text, path = self._generate(
            ["__atomic_fetch_add_4", "memcpy"],
            ["__atomic_fetch_add_4", "memcpy"])
        self.assertIn(
            'int pm_elfsym___atomic_fetch_add_4() asm("__atomic_fetch_add_4");',
            text)
        self.assertIn(
            '{ "__atomic_fetch_add_4", '
            "(void*)&pm_elfsym___atomic_fetch_add_4 },",
            text)
        self.assertNotIn("ESP_ELFSYM_EXPORT(__atomic_fetch_add_4)", text)
        self.assertIn("ESP_ELFSYM_EXPORT(memcpy)", text)

    def test_gate_parses_alias_and_plain_entries(self):
        _, path = self._generate(
            ["__atomic_fetch_add_4", "memcpy"],
            ["__atomic_fetch_add_4", "memcpy"])
        self.assertEqual(
            gate_mod.generated_table_symbols(path),
            {"__atomic_fetch_add_4", "memcpy"})


if __name__ == "__main__":
    unittest.main()
