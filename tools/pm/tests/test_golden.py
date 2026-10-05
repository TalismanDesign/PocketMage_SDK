"""Gate checks against a real built artifact, the golden fixture.

The fixture is a built sample app ELF committed under fixtures/ (see
CONTRIBUTING for how to regenerate it). Running the gate against it proves the
parse paths and the libc allowlist agree with a real toolchain output, not
just canned strings.
"""

import os
import shutil
import unittest

from tools.pm import gate as gate_mod

_FIXTURE = os.path.join(os.path.dirname(__file__), "fixtures", "hello_app.app.elf")

_ESPRESSIF_PATHS = (
    "~/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin",
    "~/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin",
    "~/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20241119/xtensa-esp-elf/bin",
    "~/.platformio/packages/toolchain-xtensa-esp32s3/bin",
)


def _readelf() -> str | None:
    env = os.environ.get("PM_READELF")
    if env and os.path.isfile(env):
        return env
    for candidate in _ESPRESSIF_PATHS:
        path = os.path.join(os.path.expanduser(candidate), "xtensa-esp32s3-elf-readelf")
        if os.path.isfile(path):
            return path
    return shutil.which("xtensa-esp32s3-elf-readelf")


READELF = _readelf()


@unittest.skipUnless(READELF, "no xtensa readelf on this machine (set PM_READELF)")
class GoldenArtifact(unittest.TestCase):
    def setUp(self):
        self.assertTrue(os.path.isfile(_FIXTURE),
                        "golden fixture missing; rebuild via CONTRIBUTING")

    def test_header_is_little_endian_shared(self):
        header = gate_mod.parse_readelf_h(gate_mod.run_readelf(READELF, "-h", "-W", _FIXTURE))
        self.assertTrue(header.is_little_endian)
        self.assertTrue(header.is_shared)
        self.assertGreater(int(header.entry, 16), 0)

    def test_gate_passes_without_host_elf(self):
        und = gate_mod.parse_undefined(gate_mod.run_readelf(READELF, "-s", "-W", _FIXTURE))
        sdk_root = os.path.normpath(os.path.join(os.path.dirname(_FIXTURE), "..", "..", "..", ".."))
        curated = gate_mod.read_curated(sdk_root)
        table = gate_mod.find_export_table(sdk_root)
        result = gate_mod.classify(
            und, curated, None,
            available_table=gate_mod.generated_table_symbols(table or ""))
        self.assertTrue(result.passed)
        self.assertEqual(result.broken, [])
        self.assertEqual(result.unconfirmed, [])
        self.assertGreater(len(result.ok), 0)

    def test_example_has_no_static_constructors(self):
        sections = gate_mod.run_readelf(READELF, "-S", "-W", _FIXTURE)
        self.assertEqual(gate_mod.init_array_size(sections), "")

    def test_undefined_set_is_resolvable(self):
        und = gate_mod.parse_undefined(gate_mod.run_readelf(READELF, "-s", "-W", _FIXTURE))
        sdk_root = os.path.normpath(os.path.join(os.path.dirname(_FIXTURE), "..", "..", "..", ".."))
        available = gate_mod.resolve_available(gate_mod.read_curated(sdk_root),
                                               gate_mod.find_export_table(sdk_root))
        self.assertTrue({u for u in und} <= available)


@unittest.skipUnless(READELF, "no xtensa readelf on this machine (set PM_READELF)")
class RuntimeExports(unittest.TestCase):
    """Runtime symbols must resolve for real library-linked apps."""

    def setUp(self):
        sdk_root = os.path.normpath(
            os.path.join(os.path.dirname(__file__), "..", "..", "..")
        )
        self.table = gate_mod.find_export_table(sdk_root)
        if self.table is None:
            self.skipTest("generated export table not present; build the firmware first")
        self.curated = set(gate_mod.symbols_mod.read_curated_list(
            os.path.join(sdk_root, "symbols.list")))

    def test_finds_table_from_sdk_root(self):
        self.assertTrue(os.path.isfile(self.table))
        self.assertTrue(self.table.endswith(
            os.path.join("src", "ELF_SYMS", "esp_all_symbol.cpp")))

    def test_cxx_runtime_symbols_are_exported(self):
        available = gate_mod.resolve_available(self.curated, self.table)
        for name in ("_Znwj", "_ZdlPv", "_ZSt20__throw_length_errorPKc"):
            self.assertIn(name, available,
                          f"{name} is needed by std::vector apps")

    def test_vector_app_undefined_set_is_confirmed(self):
        und = ["_ZdlPv", "_Znwj", "memcpy", "puts"]
        result = gate_mod.classify(und, self.curated, None,
                                   available_table=gate_mod.generated_table_symbols(self.table))
        self.assertTrue(result.passed)
        self.assertEqual(result.broken, [])
        self.assertEqual(result.unconfirmed, [])

    def test_table_covers_curated_plus_many_runtime_symbols(self):
        exported = gate_mod.generated_table_symbols(self.table)
        for name in self.curated:
            self.assertIn(name, exported, f"curated symbol {name} missing")
        self.assertGreater(len(exported) - len(self.curated), 300)


if __name__ == "__main__":
    unittest.main()