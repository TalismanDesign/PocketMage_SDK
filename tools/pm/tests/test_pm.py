"""Tests for PocketMageOS SDK tools, including app and gate modules.
"""

import os
import tempfile
import unittest
from unittest import mock

from tools.pm import app as app_mod
from tools.pm import gate as gate_mod
from tools.pm import scaffold as scaffold_mod

READELF_H_LSB = """\
ELF Header:
  Magic:   7f 45 4c 46 01 01 01 00 00 00 00 00 00 00 00 00
  Class:                             ELF32
  Data:                              2's complement, little endian
  Version:                           1 (current)
  OS/ABI:                            UNIX - System V
  Type:                              DYN (Shared object file)
  Machine:                           Xtensa
  Entry point address:               0x21c
"""

READELF_H_BE = """\
ELF Header:
  Magic:   7f 45 4c 46 01 02 01 00 00 00 00 00 00 00 00 00
  Class:                             ELF32
  Data:                              2's complement, big endian
  Type:                              EXEC (Executable file)
  Machine:                           Xtensa
  Entry point address:               0x4056ac
"""

READELF_S = """\
Symbol table '.symtab' contains 21 entries:
  Num:    Value  Size Type    Bind   Vis      Ndx Name
    0: 00000000     0 NOTYPE  LOCAL  DEFAULT  UND
    9: 00000548    27 FUNC    GLOBAL DEFAULT    1 app_main
   10: 00000000     0 NOTYPE  GLOBAL DEFAULT  UND printf
   11: 00000000     0 NOTYPE  GLOBAL DEFAULT  UND puts
   12: 00000000     0 NOTYPE  GLOBAL DEFAULT  UND sleep
   13: 00000000     0 NOTYPE  LOCAL  DEFAULT  UND local_thunk
   14: 00000000     0 NOTYPE  WEAK   DEFAULT  UND _ITM_registerTMCloneTable
"""

PM_INFO = """\
APP_NAME=hello_app
APP_OUT=build/hello_app.app.elf
APP_TAR=build/hello_app.tar
APP_ICON=/abs/hello_app_ICON.bin
SDK_ROOT=/abs/sdk
XTENSA_READELF=/abs/toolchain/bin/xtensa-esp32s3-elf-readelf
"""


class ParseReadelfH(unittest.TestCase):
    def test_lsb_dyn(self):
        header = gate_mod.parse_readelf_h(READELF_H_LSB)
        self.assertTrue(header.is_little_endian)
        self.assertTrue(header.is_shared)
        self.assertEqual(header.entry, "0x21c")

    def test_be_exec_detected(self):
        header = gate_mod.parse_readelf_h(READELF_H_BE)
        self.assertFalse(header.is_little_endian)


class ParseSymbols(unittest.TestCase):
    def test_und_globals_and_weak_only(self):
        und = gate_mod.parse_undefined(READELF_S)
        self.assertEqual(und, ["printf", "puts", "sleep", "_ITM_registerTMCloneTable"])

    def test_app_main_address_found(self):
        self.assertEqual(gate_mod.find_app_main(READELF_S), "00000548")


class Classify(unittest.TestCase):
    def test_curated_and_table_ok(self):
        curated = {"pm_sdk_version"}
        result = gate_mod.classify(["printf", "pm_sdk_version"], curated,
                                   host_view=None, available_table={"printf"})
        self.assertTrue(result.passed)
        self.assertEqual(result.ok, ["printf", "pm_sdk_version"])

    def test_unknown_is_unconfirmed_without_host(self):
        result = gate_mod.classify(["pm_sdk_version", "mystery_api"], {"pm_sdk_version"},
                                   host_view=None)
        self.assertFalse(result.passed)
        self.assertEqual(result.unconfirmed, ["mystery_api"])
        self.assertEqual(result.broken, [])

    def test_unknown_is_broken_with_host(self):
        host_view = {"printf", "pm_sdk_version"}
        result = gate_mod.classify(["pm_sdk_version", "mystery_api"],
                                   {"pm_sdk_version"}, host_view=host_view)
        self.assertEqual(result.ok, ["pm_sdk_version"])
        self.assertEqual(result.broken, ["mystery_api"])


class SymbolInGeneratedTable(unittest.TestCase):
    """The gate reads the shipped table, so it cannot pass a missing symbol."""

    def test_parses_export_lines(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "table.cpp")
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(
                    "const struct esp_elfsym g_customer_elfsyms[] = {\n"
                    "    ESP_ELFSYM_EXPORT(memmove),\n"
                    "    ESP_ELFSYM_EXPORT(_Znwj),\n"
                    "    ESP_ELFSYM_END\n"
                    "};\n"
                )
            self.assertEqual(
                gate_mod.generated_table_symbols(path), {"memmove", "_Znwj"}
            )

    def test_absent_file_is_empty(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "missing.cpp")
            self.assertEqual(gate_mod.generated_table_symbols(path), set())
            self.assertEqual(gate_mod.generated_table_symbols(""), set())

    def test_prefers_table_over_fallback(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "table.cpp")
            with open(path, "w", encoding="utf-8") as handle:
                handle.write("    ESP_ELFSYM_EXPORT(memmove),\n")
            available = gate_mod.resolve_available({"pm_sdk_version"}, path)
            self.assertEqual(available, {"memmove", "pm_sdk_version"})

    def test_missing_table_adds_nothing(self):
        available = gate_mod.resolve_available({"pm_sdk_version"}, None)
        self.assertEqual(available, {"pm_sdk_version"})

    def test_former_false_pass_symbols_are_not_invented(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "table.cpp")
            with open(path, "w", encoding="utf-8") as handle:
                handle.write("    ESP_ELFSYM_EXPORT(memcpy),\n")
            available = gate_mod.resolve_available({"pm_sdk_version"}, path)
            self.assertNotIn("memmove", available)
            self.assertNotIn("sprintf", available)


class ReadCurated(unittest.TestCase):
    def test_unions_sdk_and_host_lists(self):
        with tempfile.TemporaryDirectory() as tmp:
            sdk = os.path.join(tmp, "sdk")
            os.makedirs(sdk, exist_ok=True)
            os.makedirs(os.path.join(tmp, "src", "ELF_SYMS"), exist_ok=True)
            with open(os.path.join(sdk, "symbols.list"), "w") as f:
                f.write("# sdk\nOLED\nKB\nBZ\n")
            with open(os.path.join(tmp, "src", "ELF_SYMS", "host_exports.list"), "w") as f:
                f.write("EINK\nCLOCK\n")
            curated = gate_mod.read_curated(sdk)
            self.assertEqual(curated, {"OLED", "KB", "BZ", "EINK", "CLOCK"})


class IconValidate(unittest.TestCase):
    def test_missing_icon_is_legal(self):
        self.assertIsNone(gate_mod.validate_icon("/no/such/app_ICON.bin"))

    def test_exact_size_is_ok(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "app_ICON.bin")
            with open(path, "wb") as f:
                f.write(b"\x00" * 200)
            self.assertIsNone(gate_mod.validate_icon(path))

    def test_wrong_size_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "app_ICON.bin")
            with open(path, "wb") as f:
                f.write(b"\x00" * 201)
            error = gate_mod.validate_icon(path)
            self.assertIsNotNone(error)
            self.assertIn("expected 200", error)


class InitArray(unittest.TestCase):
    def test_absent_section(self):
        self.assertEqual("", gate_mod.init_array_size("  [ 2] .text PROGBITS ..."))

    def test_nonempty_section_detected(self):
        line = "  [ 4] .init_array INIT_ARRAY 00000000 0001b4 000004 04  WA  0   0  4\n"
        self.assertEqual(gate_mod.init_array_size(line), "000004")

    def test_empty_section_ignored(self):
        line = "  [ 4] .init_array INIT_ARRAY 00000000 0001b4 000000 04  WA  0   0  4\n"
        self.assertEqual(gate_mod.init_array_size(line), "")


class Reconcile(unittest.TestCase):
    def test_reports_missing_and_extra(self):
        with tempfile.TemporaryDirectory() as tmp:
            sdk = os.path.join(tmp, "symbols.list")
            host = os.path.join(tmp, "host_exports.list")
            with open(sdk, "w") as f:
                f.write("OLED\nKB\n")
            with open(host, "w") as f:
                f.write("OLED\nCLOCK\n")
            missing, extra = gate_mod.reconcile_exports(sdk, host)
            self.assertEqual(missing, ["KB"])
            self.assertEqual(extra, ["CLOCK"])


class ParseInfo(unittest.TestCase):
    def test_parses_key_value_lines(self):
        info = app_mod._parse_info(PM_INFO)
        self.assertEqual(info["APP_NAME"], "hello_app")
        self.assertEqual(info["XTENSA_READELF"], "/abs/toolchain/bin/xtensa-esp32s3-elf-readelf")


_FIXTURE_TEMPLATE = os.path.join(os.path.dirname(__file__), "fixtures", "template")


class Scaffold(unittest.TestCase):
    def test_creates_app_from_template(self):
        with tempfile.TemporaryDirectory() as tmp:
            app_path = scaffold_mod.scaffold_from_dir("myapp", tmp, _FIXTURE_TEMPLATE)
            self.assertTrue(os.path.isfile(os.path.join(app_path, "main.cpp")))
            self.assertTrue(os.path.isfile(os.path.join(app_path, "assets", "icon.png")))
            with open(os.path.join(app_path, "Makefile")) as f:
                makefile = f.read()
            self.assertIn("SDK_ROOT ?= $(PM_SDK_ROOT)", makefile)
            self.assertIn("include $(SDK_ROOT)/tools/app.mk", makefile)

    def test_refuses_existing_nonempty_dir(self):
        with tempfile.TemporaryDirectory() as tmp:
            os.makedirs(os.path.join(tmp, "myapp"))
            with open(os.path.join(tmp, "myapp", "sentinel"), "w") as f:
                f.write("occupied")
            with self.assertRaises(app_mod.PmError):
                scaffold_mod.scaffold_from_dir("myapp", tmp, _FIXTURE_TEMPLATE)

    def test_invalid_name_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(app_mod.PmError):
                scaffold_mod.scaffold_from_dir("1bad-name", tmp, _FIXTURE_TEMPLATE)


class Version(unittest.TestCase):
    def test_reads_sdk_version(self):
        version = app_mod.sdk_version()
        self.assertRegex(version, r"^\d+\.\d+\.\d+$")

    def test_abi_literal_matches_version_ssot(self):
        self.assertEqual(app_mod.sdk_version(), app_mod.abi_version())


class SdkRoot(unittest.TestCase):
    def test_env_override_wins(self):
        root = app_mod.sdk_root()
        with mock.patch.dict(os.environ, {"PM_SDK_ROOT": root, "PWD": "/tmp"}):
            self.assertEqual(app_mod.sdk_root(), root)

    def test_invalid_env_rejected(self):
        with mock.patch.dict(os.environ, {"PM_SDK_ROOT": "/definitely/not/a/sdk"}):
            with self.assertRaises(app_mod.PmError):
                app_mod.sdk_root()

    def test_ancestor_walk_finds_checkout(self):
        sdk = app_mod.sdk_root()
        deep = os.path.join(sdk, "docs", "docs", "build")
        with mock.patch.dict(os.environ, {}, clear=True), \
             mock.patch.object(app_mod, "_is_sdk_root", side_effect=lambda p: p == sdk), \
             mock.patch.object(app_mod.os, "getcwd", return_value=deep):
            self.assertEqual(app_mod.sdk_root(), sdk)


if __name__ == "__main__":
    unittest.main()