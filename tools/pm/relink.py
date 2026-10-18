
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Protocol

from tools.pm.overlay import Analysis, Clustering, Image, OverlayError, OverlayPlan, run_tool

R_RELATIVE = "R_XTENSA_RELATIVE"
R_JMP_SLOT = "R_XTENSA_JMP_SLOT"
R_RTLD = "R_XTENSA_RTLD"

_L32R_OP0 = 0x1
_SHORT_OP0 = range(0x8, 0xE)
_L32R_LENGTH = 3
_POOL_ALIGNMENT = 4


@dataclass(frozen=True)
class Relocation:

    offset: int
    type: str
    symbol: str | None = None


@dataclass(frozen=True)
class L32rSite:

    offset: int
    register: int
    literal: int


@dataclass(frozen=True)
class BodyPlacement:

    name: str
    address: int
    size: int
    offset: int


@dataclass
class OverlayBlob:

    overlay_id: int
    data: bytes
    placements: list[BodyPlacement] = field(default_factory=list)
    literal_offset: int = 0
    literal_count: int = 0


class AddressResolver(Protocol):

    def function(self, canonical: int) -> int:

    def data(self, address: int) -> int:

    def host(self, symbol: str) -> int:


def parse_relocations(stdout: str) -> list[Relocation]:
    relocs: list[Relocation] = []
    for line in stdout.splitlines():
        parts = line.split()
        if len(parts) < 3 or not parts[2].startswith("R_XTENSA_"):
            continue
        try:
            offset = int(parts[0], 16)
        except ValueError:
            continue
        symbol = parts[parts.index("+") - 1] if "+" in parts else None
        relocs.append(Relocation(offset=offset, type=parts[2], symbol=symbol))
    return relocs


def read_relocations(elf_path: str, readelf: str) -> list[Relocation]:
    return parse_relocations(run_tool(readelf, ["-r", "-W", elf_path]))


def _instruction_length(first_byte: int) -> int:
    return 2 if (first_byte & 0xF) in _SHORT_OP0 else _L32R_LENGTH


def _sign16(value: int) -> int:
    return value - 0x10000 if value >= 0x8000 else value


def decode_l32r(pc: int, word: int) -> tuple[int, int]:
    return (word >> 4) & 0xF, l32r_literal(pc, (word >> 8) & 0xFFFF)


def encode_l32r(pc: int, register: int, literal: int) -> bytes:
    delta = literal - ((pc + 3) & ~3)
    if delta % 4 or not -0x8000 <= (delta >> 2) <= 0x7FFF:
        raise OverlayError(
            f"literal {literal:#x} is out of l32r range from {pc:#x}")
    imm = (delta >> 2) & 0xFFFF
    return (_L32R_OP0 | (register << 4) | (imm << 8)).to_bytes(3, "little")


def l32r_literal(pc: int, imm: int) -> int:
    return ((pc + 3) & ~3) + (_sign16(imm) << 2)


def l32r_sites(code: bytes, base_pc: int) -> list[L32rSite]:
    sites: list[L32rSite] = []
    offset = 0
    while offset < len(code):
        first = code[offset]
        if (first & 0xF) == _L32R_OP0 and offset + _L32R_LENGTH <= len(code):
            word = int.from_bytes(code[offset:offset + _L32R_LENGTH], "little")
            register, literal = decode_l32r(base_pc + offset, word)
            sites.append(L32rSite(offset=offset, register=register, literal=literal))
        offset += _instruction_length(first)
    return sites


def _align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def _resolve_word(slot: int, reloc: Relocation | None, word: int,
                  by_address: dict[int, object], resolver: AddressResolver) -> int:
    if reloc is None:
        return word
    if reloc.type == R_RELATIVE:
        func = by_address.get(word)
        return resolver.function(word) if func is not None else resolver.data(word)
    if reloc.type == R_JMP_SLOT:
        if reloc.symbol is None:
            raise OverlayError(
                f"literal slot {slot:#x} is a host call with no symbol")
        return resolver.host(reloc.symbol)
    raise OverlayError(
        f"literal slot {slot:#x} has unsupported relocation {reloc.type}")


def build_blob(image: Image, analysis: Analysis, plan: OverlayPlan,
               relocations: list[Relocation],
               resolver: AddressResolver) -> OverlayBlob:
    by_address = analysis.by_address()
    reloc_by_offset: dict[int, Relocation] = {}
    for reloc in relocations:
        reloc_by_offset.setdefault(reloc.offset, reloc)

    bodies: list[tuple[OverlayPlan, BodyPlacement, bytearray]] = []
    offset = 0
    for func in plan.functions:
        raw = image.bytes_at(func.address, func.size)
        if raw is None or len(raw) != func.size:
            raise OverlayError(
                f"cannot read {func.size} bytes of {func.name} at {func.address:#x}")
        placement = BodyPlacement(name=func.name, address=func.address,
                                  size=func.size, offset=offset)
        bodies.append((func, placement, bytearray(raw)))
        offset += func.size

    literal_offset = _align_up(offset, _POOL_ALIGNMENT)
    pool: dict[int, int] = {}
    pool_bytes = bytearray()
    for func, placement, code in bodies:
        for site in l32r_sites(bytes(code), func.address):
            if site.literal not in pool:
                word = image.word_at(site.literal)
                if word is None:
                    raise OverlayError(
                        f"{func.name} reads unmapped literal {site.literal:#x}")
                resolved = _resolve_word(site.literal, reloc_by_offset.get(site.literal),
                                         word, by_address, resolver)
                pool[site.literal] = literal_offset + len(pool_bytes)
                pool_bytes += resolved.to_bytes(4, "little")
            pc = placement.offset + site.offset
            code[site.offset:site.offset + _L32R_LENGTH] = encode_l32r(
                pc, site.register, pool[site.literal])

    padding = bytes(literal_offset - offset)
    data = b"".join(bytes(code) for _, _, code in bodies) + padding + bytes(pool_bytes)
    return OverlayBlob(
        overlay_id=plan.overlay_id,
        data=data,
        placements=[placement for _, placement, _ in bodies],
        literal_offset=literal_offset,
        literal_count=len(pool),
    )


def build_blobs(image: Image, analysis: Analysis, clustering: Clustering,
                relocations: list[Relocation],
                resolver: AddressResolver) -> list[OverlayBlob]:
    if clustering.oversized:
        raise OverlayError(
            "cannot page oversized units: " + ", ".join(
                "(" + ", ".join(group) + ")" for group in clustering.oversized))
    return [build_blob(image, analysis, plan, relocations, resolver)
            for plan in clustering.overlays]
