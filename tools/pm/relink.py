
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Protocol

from tools.pm.overlay import Analysis, Clustering, FunctionSymbol, Image, OverlayError, OverlayPlan, run_tool

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
    patches: list[int] = field(default_factory=list)
    host_refs: list[tuple[int, str]] = field(default_factory=list)


class AddressResolver(Protocol):

    def function(self, canonical: int) -> int:
        ...

    def data(self, address: int) -> int:
        ...

    def host(self, symbol: str) -> int:
        ...


class DeferredResolver:

    def function(self, canonical: int) -> int:
        return canonical

    def data(self, address: int) -> int:
        return address

    def host(self, symbol: str) -> int:
        return 0


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


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def build_blob(image: Image, analysis: Analysis, plan: OverlayPlan,
               relocations: list[Relocation],
               resolver: AddressResolver) -> OverlayBlob:
    if not plan.functions:
        raise OverlayError(f"overlay {plan.overlay_id} has no functions to emit")
    by_address = analysis.by_address()
    reloc_by_offset: dict[int, Relocation] = {}
    for reloc in relocations:
        reloc_by_offset.setdefault(reloc.offset, reloc)

    base = min(func.address for func in plan.functions)
    bodies: list[tuple[FunctionSymbol, BodyPlacement, bytearray]] = []
    code_end = 0
    for func in plan.functions:
        raw = image.bytes_at(func.address, func.size)
        if raw is None or len(raw) != func.size:
            raise OverlayError(
                f"cannot read {func.size} bytes of {func.name} at {func.address:#x}")
        placement = BodyPlacement(name=func.name, address=func.address,
                                  size=func.size, offset=func.address - base)
        bodies.append((func, placement, bytearray(raw)))
        code_end = max(code_end, placement.offset + func.size)

    patches: list[int] = []
    host_refs: list[tuple[int, str]] = []
    literal_offset = align_up(code_end, _POOL_ALIGNMENT)
    pool: dict[int, int] = {}
    pool_bytes = bytearray()
    for func, placement, code in bodies:
        for site in l32r_sites(bytes(code), func.address):
            if site.literal not in pool:
                word = image.word_at(site.literal)
                if word is None:
                    raise OverlayError(
                        f"{func.name} reads unmapped literal {site.literal:#x}")
                reloc = reloc_by_offset.get(site.literal)
                pool_offset = literal_offset + len(pool_bytes)
                pool[site.literal] = pool_offset
                if reloc is None:
                    resolved = word
                elif reloc.type == R_RELATIVE:
                    resolved = (resolver.function(word) if word in by_address
                                else resolver.data(word))
                    patches.append(pool_offset)
                elif reloc.type == R_JMP_SLOT:
                    if reloc.symbol is None:
                        raise OverlayError(
                            f"literal slot {site.literal:#x} is a host call "
                            "with no symbol")
                    resolved = resolver.host(reloc.symbol)
                    host_refs.append((pool_offset, reloc.symbol))
                else:
                    raise OverlayError(
                        f"literal slot {site.literal:#x} has unsupported "
                        f"relocation {reloc.type}")
                pool_bytes += resolved.to_bytes(4, "little")
            pc = placement.offset + site.offset
            code[site.offset:site.offset + _L32R_LENGTH] = encode_l32r(
                pc, site.register, pool[site.literal])

    code_area = bytearray(code_end)
    for _func, placement, code in bodies:
        code_area[placement.offset:placement.offset + placement.size] = code
    padding = bytes(literal_offset - code_end)
    data = bytes(code_area) + padding + bytes(pool_bytes)
    return OverlayBlob(
        overlay_id=plan.overlay_id,
        data=data,
        placements=[placement for _, placement, _ in bodies],
        literal_offset=literal_offset,
        literal_count=len(pool),
        patches=patches,
        host_refs=host_refs,
    )


def _guard_direct_calls(analysis: Analysis, plans: list[OverlayPlan]) -> None:
    plan_of_name: dict[str, int] = {}
    plan_of_address: dict[int, int] = {}
    for plan in plans:
        for func in plan.functions:
            plan_of_name[func.name] = plan.overlay_id
            plan_of_address[func.address] = plan.overlay_id
    for call in analysis.calls:
        if call.via != "direct" or call.target is None:
            continue
        caller_plan = plan_of_name.get(call.func)
        callee_plan = plan_of_address.get(call.target)
        if caller_plan == callee_plan:
            continue
        callee = call.symbol or f"{call.target:#x}"
        raise OverlayError(
            f"direct call in {call.func or '?'} to {callee} crosses the "
            "paged/resident boundary; PC-relative calls cannot be retargeted "
            "to a stub (cluster them together or call through a pointer)")


def build_blobs(image: Image, analysis: Analysis, clustering: Clustering,
                relocations: list[Relocation],
                resolver: AddressResolver) -> list[OverlayBlob]:
    if clustering.oversized:
        raise OverlayError(
            "cannot page oversized units: " + ", ".join(
                "(" + ", ".join(group) + ")" for group in clustering.oversized))
    _guard_direct_calls(analysis, clustering.overlays)
    return [build_blob(image, analysis, plan, relocations, resolver)
            for plan in clustering.overlays]
