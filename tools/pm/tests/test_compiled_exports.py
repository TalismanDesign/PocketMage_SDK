import struct
import unittest

from tools.pm import gate


def firmware(*, registry=True, tables=("libc", "customer"),
             null_target=False, terminated=True, invalid_name=False, invalid_registry=False):
    base = 0x3C000000
    rodata = bytearray()

    def append(data):
        address = base + len(rodata)
        rodata.extend(data)
        return address

    def string(value):
        return append(value.encode("ascii") + b"\0")

    shared = string("shared")
    if invalid_name:
        shared = 0xDEADBEEF
    sdk = string("pm_audio_play")
    hidden = string("disabled_export")
    while len(rodata) % 4:
        rodata.append(0)
    addresses = {}
    symbols = []
    for name, entries in (
        ("libc", ((shared, 0x40001000),)),
        ("customer", ((shared, 0x40001004), (sdk, 0 if null_target else 0x40001008))),
        ("disabled", ((hidden, 0x4000100C),)),
    ):
        data = b"".join(struct.pack("<II", *entry) for entry in entries)
        if terminated:
            data += b"\0" * 8
        address = append(data)
        addresses[name] = address
        symbols.append((name, address, len(data), 0x11, 1))
    if registry:
        data = b"".join(struct.pack("<I", addresses[name]) for name in tables) + b"\0" * 4
        if invalid_registry:
            data = struct.pack("<I", 0xDEADBEEF) + data[4:]
        address = append(data)
        symbols.append(("g_pm_elf_static_tables", address, len(data), 0x11, 1))
    symbols.append(("global_but_not_exported", 0x40001010, 4, 0x12, 2))
    names = bytearray(b"\0")
    symtab = bytearray(16)
    for name, address, size, info, section in symbols:
        name_offset = len(names)
        names.extend(name.encode("ascii") + b"\0")
        symtab.extend(struct.pack("<IIIBBH", name_offset, address, size, info, 0, section))
    sections = [(0,) * 10]
    data = bytearray(52)
    for kind, flags, address, content, link, entry_size in (
        (1, 2, base, rodata, 0, 0),
        (1, 6, 0x40001000, b"\0" * 20, 0, 0),
        (3, 0, 0, names, 0, 0),
        (2, 0, 0, symtab, 3, 16),
    ):
        sections.append((0, kind, flags, address, len(data), len(content), link, 0, 4, entry_size))
        data.extend(content)
    offset = len(data)
    for section in sections:
        data.extend(struct.pack("<10I", *section))
    ident = b"\x7fELF\x01\x01\x01" + b"\0" * 9
    struct.pack_into("<16sHHIIIIIHHHHHH", data, 0, ident, 2, 94, 1, 0x40001000,
                     0, offset, 0, 52, 0, 0, 40, len(sections), 0)
    return bytes(data)


class CompiledExports(unittest.TestCase):
    def test_enabled_tables_are_authoritative_and_first_definition_wins(self):
        self.assertEqual(gate.parse_compiled_exports(firmware()),
                         {"shared": 0x40001000, "pm_audio_play": 0x40001008})

    def test_disabled_linked_table_is_not_available(self):
        self.assertEqual(gate.parse_compiled_exports(firmware(tables=("libc",))),
                         {"shared": 0x40001000})

    def test_reversed_registry_changes_resolver_precedence(self):
        self.assertEqual(gate.parse_compiled_exports(firmware(tables=("customer", "libc")))["shared"],
                         0x40001004)

    def test_missing_registry_requires_a_host_rebuild(self):
        with self.assertRaisesRegex(ValueError, "registry.*rebuild"):
            gate.parse_compiled_exports(firmware(registry=False))

    def test_null_targets_and_unterminated_tables_are_rejected(self):
        for options in ({"null_target": True}, {"terminated": False}):
            with self.subTest(options=options), self.assertRaises(ValueError):
                gate.parse_compiled_exports(firmware(**options))

    def test_truncated_or_wrong_architecture_elf_is_rejected(self):
        valid = firmware()
        variants = [valid[:length] for length in (0, 16, 51, len(valid) - 1)]
        for offset, replacement in ((4, 2), (5, 2), (18, 3)):
            data = bytearray(valid)
            data[offset] = replacement
            variants.append(bytes(data))
        for data in variants:
            with self.subTest(length=len(data)), self.assertRaises(ValueError):
                gate.parse_compiled_exports(data)

    def test_invalid_string_or_registry_pointer_is_rejected(self):
        for options in ({"invalid_name": True}, {"invalid_registry": True}):
            with self.subTest(options=options), self.assertRaises(ValueError):
                gate.parse_compiled_exports(firmware(**options))
