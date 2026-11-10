"""Read the exact static resolver registry from linked ESP32-S3 firmware.

ELF32 layout: https://refspecs.linuxfoundation.org/elf/gabi4+/ch4.intro.html
"""

from dataclasses import dataclass
import struct


_HEADER = struct.Struct("<16sHHIIIIIHHHHHH")
_SECTION = struct.Struct("<10I")
_SYMBOL = struct.Struct("<IIIBBH")
_POINTER = struct.Struct("<I")
_EXPORT = struct.Struct("<II")
_SHT_SYMTAB, _SHT_STRTAB, _SHT_NOBITS, _SHT_DYNSYM = 2, 3, 8, 11
_SHF_ALLOC, _STT_OBJECT, _SHN_ABS = 2, 1, 0xFFF1
_ET_EXEC, _EM_XTENSA = 2, 94
_REGISTRY = "g_pm_elf_static_tables"


@dataclass(frozen=True)
class _Section:
    kind: int
    flags: int
    address: int
    offset: int
    size: int
    link: int
    entry_size: int


@dataclass(frozen=True)
class _Symbol:
    name: str
    address: int
    size: int
    kind: int
    section: int


def _string(data: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(data):
        raise ValueError("ELF string offset outside string data")
    end = data.find(b"\0", offset)
    if end < 0:
        raise ValueError("Unterminated ELF string")
    return data[offset:end].decode("utf-8")


class _Image:
    def __init__(self, data: bytes):
        self.data = data
        header = _HEADER.unpack(self.slice(0, _HEADER.size))
        if (header[0][:7] != b"\x7fELF\x01\x01\x01" or header[1] != _ET_EXEC or
                header[2] != _EM_XTENSA or header[3] != 1):
            raise ValueError("Expected ELF32 little-endian Xtensa executable firmware")
        if header[8] != _HEADER.size or header[11] != _SECTION.size or not header[12]:
            raise ValueError("Unsupported ELF header or section table layout")
        section_data = self.slice(header[6], header[11] * header[12])
        self.sections = []
        for raw in _SECTION.iter_unpack(section_data):
            section = _Section(raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[9])
            if section.kind != _SHT_NOBITS:
                self.slice(section.offset, section.size)
            self.sections.append(section)
        self.symbols = self.read_symbols()

    def slice(self, offset: int, size: int) -> bytes:
        if offset < 0 or size < 0 or offset > len(self.data) - size:
            raise ValueError(f"ELF file range out of bounds: {offset}+{size}")
        return self.data[offset:offset + size]

    def read_symbols(self) -> list[_Symbol]:
        symbols = []
        for section in self.sections:
            if section.kind not in (_SHT_SYMTAB, _SHT_DYNSYM):
                continue
            if section.entry_size != _SYMBOL.size or section.size % _SYMBOL.size:
                raise ValueError("Invalid ELF symbol table size")
            if section.link >= len(self.sections):
                raise ValueError("ELF symbol string table index out of bounds")
            strings = self.sections[section.link]
            if strings.kind != _SHT_STRTAB:
                raise ValueError("ELF symbol names do not reference a string table")
            names = self.slice(strings.offset, strings.size)
            for name, address, size, info, _, index in _SYMBOL.iter_unpack(
                    self.slice(section.offset, section.size)):
                if not name or not index:
                    continue
                symbols.append(_Symbol(_string(names, name), address, size, info & 0xF, index))
        return symbols

    def address_section(self, address: int, size: int) -> _Section:
        for section in self.sections:
            if section.kind == _SHT_NOBITS or not section.flags & _SHF_ALLOC:
                continue
            if section.address <= address and address + size <= section.address + section.size:
                return section
        raise ValueError(f"ELF address is not file-backed: {address:#x}+{size}")

    def address_bytes(self, address: int, size: int) -> bytes:
        section = self.address_section(address, size)
        return self.slice(section.offset + address - section.address, size)

    def address_string(self, address: int) -> str:
        section = self.address_section(address, 1)
        data = self.slice(section.offset, section.size)
        return _string(data, address - section.address)

    def table(self, address: int, entry_size: int) -> bytes:
        sizes = {symbol.size for symbol in self.symbols
                 if symbol.address == address and symbol.kind == _STT_OBJECT and symbol.size}
        if len(sizes) != 1:
            raise ValueError(f"Missing or ambiguous ELF table object at {address:#x}")
        size = sizes.pop()
        if size % entry_size:
            raise ValueError(f"Misaligned ELF table size at {address:#x}")
        return self.address_bytes(address, size)

    def valid_target(self, address: int) -> bool:
        if not address:
            return False
        # ROM entry points are absolute symbols rather than file-backed sections.
        if any(symbol.section == _SHN_ABS and symbol.address == address for symbol in self.symbols):
            return True
        return any(section.flags & _SHF_ALLOC and
                   section.address <= address < section.address + section.size
                   for section in self.sections)


def parse_compiled_exports(data: bytes) -> dict[str, int]:
    """Return loader-visible names and targets from firmware bytes.

    The linked registry controls table inclusion and first-match precedence.
    Raise ValueError for unsupported/malformed ELF data or a missing registry.
    Runtime-registered symbols are not statically provable and are excluded.
    """
    image = _Image(data)
    registries = {symbol.address for symbol in image.symbols if symbol.name == _REGISTRY}
    if len(registries) != 1:
        raise ValueError("Missing or ambiguous compiled loader registry; rebuild the host firmware")
    registry = image.table(registries.pop(), _POINTER.size)
    pointers = [pointer for (pointer,) in _POINTER.iter_unpack(registry)]
    if not pointers or pointers[-1] != 0 or 0 in pointers[:-1]:
        raise ValueError("Invalid compiled loader registry terminator")
    result = {}
    for address in pointers[:-1]:
        entries = list(_EXPORT.iter_unpack(image.table(address, _EXPORT.size)))
        if not entries or entries[-1] != (0, 0):
            raise ValueError("Missing compiled export table terminator")
        for name_address, target in entries[:-1]:
            if not name_address or not image.valid_target(target):
                raise ValueError("Invalid compiled export name or target pointer")
            name = image.address_string(name_address)
            if not name:
                raise ValueError("Empty compiled export name")
            result.setdefault(name, target)
    return result
