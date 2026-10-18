
from __future__ import annotations

import re
import subprocess
from dataclasses import dataclass, field

_DIRECT_CALLS = ("call0", "call4", "call8", "call12")

_SECTION_RE = re.compile(r"^\s*\[\s*(\d+)\]\s+(\S+)\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)")
_FUNC_RE = re.compile(
    r"^\s*\d+:\s+([0-9a-f]+)\s+(\d+)\s+FUNC\s+\S+\s+\S+\s+(\d+)\s+(\S+)\s*$"
)
_FUNC_HEADER_RE = re.compile(r"^([0-9a-f]+) <(.+)>:\s*$")
_INSN_RE = re.compile(r"^\s*([0-9a-f]+):\s+[0-9a-f]{2,}(?:\s+[0-9a-f]{2,})*\s+([.\w]+)\s*(.*)$")
_L32R_RE = re.compile(r"^([a-z]\w*)\s*,\s*([0-9a-f]+)\s*(?:<([^>]+)>)?")
_HEX_TOKEN_RE = re.compile(r"^([0-9a-f]+)\s*(?:<([^>]+)>)?")


class OverlayError(Exception):
    pass


@dataclass(frozen=True)
class Section:

    index: int
    name: str
    address: int
    offset: int
    size: int


@dataclass(frozen=True)
class FunctionSymbol:

    name: str
    address: int
    size: int
    section: str


@dataclass(frozen=True)
class CallSite:

    func: str
    address: int
    via: str
    target: int | None = None
    symbol: str | None = None


@dataclass
class Image:

    sections: list[Section]
    data: bytes

    def word_at(self, address: int) -> int | None:
        raw = self.bytes_at(address, 4)
        return None if raw is None else int.from_bytes(raw, "little")

    def bytes_at(self, address: int, size: int) -> bytes | None:
        for section in self.sections:
            delta = address - section.address
            if 0 <= delta and delta + size <= section.size:
                start = section.offset + delta
                return self.data[start:start + size]
        return None


@dataclass
class Analysis:

    functions: list[FunctionSymbol] = field(default_factory=list)
    calls: list[CallSite] = field(default_factory=list)

    def by_address(self) -> dict[int, FunctionSymbol]:
        return {f.address: f for f in self.functions}

    def calls_from(self, name: str) -> list[CallSite]:
        return [c for c in self.calls if c.func == name]

    def edges(self) -> list[tuple[str, str]]:
        index = self.by_address()
        out: list[tuple[str, str]] = []
        for call in self.calls:
            if call.target is None:
                continue
            callee = index.get(call.target)
            if callee is not None:
                out.append((call.func, callee.name))
        return out


def parse_sections(stdout: str) -> list[Section]:
    sections: list[Section] = []
    for line in stdout.splitlines():
        m = _SECTION_RE.match(line)
        if not m:
            continue
        index, name, _type, address, offset, size = m.groups()
        sections.append(Section(
            index=int(index),
            name=name,
            address=int(address, 16),
            offset=int(offset, 16),
            size=int(size, 16),
        ))
    return sections


def parse_functions(stdout: str, sections: list[Section] | None = None) -> list[FunctionSymbol]:
    names = {s.index: s.name for s in (sections or [])}
    funcs: list[FunctionSymbol] = []
    for line in stdout.splitlines():
        m = _FUNC_RE.match(line)
        if not m:
            continue
        address, size, section, name = m.groups()
        funcs.append(FunctionSymbol(
            name=name,
            address=int(address, 16),
            size=int(size),
            section=names.get(int(section), section),
        ))
    funcs.sort(key=lambda f: f.address)
    return funcs


def _dest_register(operands: str) -> str | None:
    token = operands.split(",", 1)[0].strip()
    return token if re.fullmatch(r"a\d+", token) else None


def _export_symbol(symbol: str | None) -> str | None:
    if symbol is not None and "+" in symbol:
        return None
    return symbol


def parse_calls(stdout: str, image: Image) -> list[CallSite]:
    calls: list[CallSite] = []
    pending: dict[str, tuple[int, str | None]] = {}
    func = ""
    for line in stdout.splitlines():
        header = _FUNC_HEADER_RE.match(line)
        if header:
            func = header.group(2)
            pending.clear()
            continue

        insn = _INSN_RE.match(line)
        if not insn:
            continue
        address, mnemonic, operands = insn.groups()

        if mnemonic == "l32r":
            m = _L32R_RE.match(operands)
            if m:
                reg, slot, symbol = m.groups()
                pending[reg] = (int(slot, 16), _export_symbol(symbol))
            continue

        if mnemonic.startswith("callx"):
            reg = operands.strip()
            slot = pending.get(reg)
            if slot is None:
                calls.append(CallSite(func=func, address=int(address, 16), via="indirect"))
            elif slot[1] is not None:
                calls.append(CallSite(func=func, address=int(address, 16),
                                      via="indirect", symbol=slot[1]))
            else:
                calls.append(CallSite(func=func, address=int(address, 16),
                                      via="indirect", target=image.word_at(slot[0])))
            continue

        if mnemonic in _DIRECT_CALLS:
            m = _HEX_TOKEN_RE.match(operands)
            target = int(m.group(1), 16) if m else None
            symbol = _export_symbol(m.group(2)) if m else None
            calls.append(CallSite(func=func, address=int(address, 16),
                                  via="direct", target=target, symbol=symbol))
            continue

        dest = _dest_register(operands)
        if dest is not None:
            pending.pop(dest, None)

    return calls


def _tool_sibling(readelf: str, name: str) -> str:
    if readelf.endswith("readelf"):
        return readelf[: -len("readelf")] + name
    return name


def run_tool(binary: str, args: list[str]) -> str:
    result = subprocess.run([binary, *args], capture_output=True, text=True)
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise OverlayError(f"{binary} {' '.join(args)} failed: {detail}")
    return result.stdout


def analyze(elf_path: str, readelf: str = "xtensa-esp32s3-elf-readelf") -> Analysis:
    with open(elf_path, "rb") as handle:
        data = handle.read()

    sections = parse_sections(run_tool(readelf, ["-S", "-W", elf_path]))
    image = Image(sections=sections, data=data)
    functions = parse_functions(run_tool(readelf, ["-s", "-W", elf_path]), sections)
    objdump = _tool_sibling(readelf, "objdump")
    calls = parse_calls(run_tool(objdump, ["-d", elf_path]), image)
    return Analysis(functions=functions, calls=calls)


DEFAULT_FRAME_SIZE = 16 * 1024


@dataclass(frozen=True)
class OverlayPlan:

    overlay_id: int
    functions: tuple[FunctionSymbol, ...]
    size: int

    @property
    def names(self) -> tuple[str, ...]:
        return tuple(f.name for f in self.functions)


@dataclass
class Clustering:

    overlays: list[OverlayPlan] = field(default_factory=list)
    oversized: list[tuple[str, ...]] = field(default_factory=list)
    max_call_depth: int = 0

    def overlay_of(self, name: str) -> int | None:
        for plan in self.overlays:
            if any(f.name == name for f in plan.functions):
                return plan.overlay_id
        return None


def _strongly_connected(names: list[str],
                        adjacency: dict[str, list[str]]) -> list[list[str]]:
    index: dict[str, int] = {}
    low: dict[str, int] = {}
    on_stack: dict[str, bool] = {}
    stack: list[str] = []
    components: list[list[str]] = []
    counter = 0

    for root in names:
        if root in index:
            continue
        work: list[tuple[str, int]] = [(root, 0)]
        while work:
            node, child = work[-1]
            if child == 0:
                index[node] = low[node] = counter
                counter += 1
                stack.append(node)
                on_stack[node] = True
            neighbors = adjacency.get(node, [])
            descended = False
            for i in range(child, len(neighbors)):
                nxt = neighbors[i]
                if nxt not in index:
                    work[-1] = (node, i + 1)
                    work.append((nxt, 0))
                    descended = True
                    break
                if on_stack.get(nxt):
                    low[node] = min(low[node], index[nxt])
            if descended:
                continue
            if low[node] == index[node]:
                component: list[str] = []
                while True:
                    popped = stack.pop()
                    on_stack[popped] = False
                    component.append(popped)
                    if popped == node:
                        break
                components.append(component)
            work.pop()
            if work:
                parent = work[-1][0]
                low[parent] = min(low[parent], low[node])
    return components


def cluster(analysis: Analysis, frame_size: int = DEFAULT_FRAME_SIZE) -> Clustering:
    paged = [f for f in analysis.functions if f.section == ".text" and f.size > 0]
    by_name = {f.name: f for f in paged}
    names = [f.name for f in paged]

    adjacency: dict[str, list[str]] = {name: [] for name in names}
    for caller, callee in analysis.edges():
        if caller in by_name and callee in by_name:
            adjacency[caller].append(callee)

    components = _strongly_connected(names, adjacency)
    unit_of: dict[str, int] = {}
    for unit_id, members in enumerate(components):
        for name in members:
            unit_of[name] = unit_id

    unit_size = [sum(by_name[n].size for n in members) for members in components]
    cross: dict[tuple[int, int], int] = {}
    unit_adjacency: dict[int, set[int]] = {i: set() for i in range(len(components))}
    for caller, callees in adjacency.items():
        for callee in callees:
            a, b = unit_of[caller], unit_of[callee]
            if a == b:
                continue
            cross[(a, b)] = cross.get((a, b), 0) + 1
            cross[(b, a)] = cross.get((b, a), 0) + 1
            unit_adjacency[a].add(b)

    result = Clustering()
    order = sorted(range(len(components)), key=lambda i: -unit_size[i])
    for unit_id in order:
        if unit_size[unit_id] > frame_size:
            result.oversized.append(tuple(sorted(components[unit_id])))
            continue
        best_id, best_affinity = None, -1
        for plan in result.overlays:
            if plan.size + unit_size[unit_id] > frame_size:
                continue
            affinity = sum(cross.get((unit_id, unit_of[f.name]), 0)
                           for f in plan.functions)
            if best_id is None or affinity > best_affinity:
                best_id, best_affinity = plan.overlay_id, affinity
        if best_id is None:
            best_id = len(result.overlays)
            result.overlays.append(OverlayPlan(overlay_id=best_id, functions=(), size=0))
        plan = result.overlays[best_id]
        members = tuple(by_name[n] for n in components[unit_id])
        result.overlays[best_id] = OverlayPlan(
            overlay_id=best_id,
            functions=tuple(sorted(plan.functions + members,
                                   key=lambda f: f.address)),
            size=plan.size + unit_size[unit_id],
        )

    result.max_call_depth = _max_depth(unit_adjacency)
    return result


def _max_depth(unit_adjacency: dict[int, set[int]]) -> int:
    depth: dict[int, int] = {}

    def visit(unit_id: int) -> int:
        if unit_id in depth:
            return depth[unit_id]
        depth[unit_id] = 1
        for nxt in unit_adjacency.get(unit_id, ()):
            depth[unit_id] = max(depth[unit_id], visit(nxt) + 1)
        return depth[unit_id]

    return max((visit(u) for u in unit_adjacency), default=0)

