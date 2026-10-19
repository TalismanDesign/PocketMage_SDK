
from __future__ import annotations

import os
import re
import struct
from dataclasses import dataclass, field

from tools.pm.overlay import Analysis, Clustering, Image, OverlayError
from tools.pm.relink import (
    R_RELATIVE,
    AddressResolver,
    OverlayBlob,
    Relocation,
    align_up,
    build_blobs,
)

STUB_ALIGNMENT = 4

_STUB_SEQUENCE: tuple[tuple[str, int], ...] = (
    ("entry a1, 32", 3),
    ("l32r a10, {key}", 3),
    ("l32r a8, overlay_enter_slot", 3),
    ("callx8 a8", 3),
    ("mov.n a8, a10", 2),
    ("mov.n a10, a2", 2),
    ("mov.n a11, a3", 2),
    ("mov.n a12, a4", 2),
    ("mov.n a13, a5", 2),
    ("mov.n a14, a6", 2),
    ("mov.n a15, a7", 2),
    ("callx8 a8", 3),
    ("mov.n a2, a10", 2),
    ("l32r a10, {key}", 3),
    ("l32r a8, overlay_leave_slot", 3),
    ("callx8 a8", 3),
    ("retw.n", 2),
)

STUB_CODE_SIZE = sum(length for _, length in _STUB_SEQUENCE)
STUB_STRIDE = align_up(STUB_CODE_SIZE, STUB_ALIGNMENT)

_ENTER_SLOT = "overlay_enter_slot"
_LEAVE_SLOT = "overlay_leave_slot"

_KEY_MAX = 0xFFFF


@dataclass(frozen=True)
class StubLayout:

    stub_base: int
    slot_base: int
    stride: int = STUB_STRIDE

    def address(self, index: int) -> int:
        return self.stub_base + index * self.stride


@dataclass(frozen=True)
class StubSlot:

    enter: int
    leave: int


@dataclass(frozen=True)
class FunctionStub:

    name: str
    canonical: int
    overlay_id: int
    body_offset: int
    address: int
    key: int


@dataclass(frozen=True)
class OverlayImage:

    overlay_id: int
    offset: int
    length: int


@dataclass(frozen=True)
class Redirect:

    site: int
    stub: int


@dataclass(frozen=True)
class PoolPatch:

    overlay_id: int
    blob_offset: int


@dataclass
class Manifest:

    app: str
    stubs: tuple[FunctionStub, ...] = ()
    overlays: tuple[OverlayImage, ...] = ()
    slots: StubSlot = StubSlot(0, 0)
    redirects: tuple[Redirect, ...] = ()
    patches: tuple[PoolPatch, ...] = ()


@dataclass
class Emission:

    manifest: Manifest
    blob: bytes
    stubs_asm: str
    header: str


class _StubResolver:

    def __init__(self, inner: AddressResolver, addresses: dict[int, int]):
        self._inner = inner
        self._addresses = addresses

    @property
    def deferred(self) -> bool:
        return getattr(self._inner, "deferred", False)

    def function(self, canonical: int) -> int:
        stub = self._addresses.get(canonical)
        return stub if stub is not None else self._inner.function(canonical)

    def data(self, address: int) -> int:
        return self._inner.data(address)

    def host(self, symbol: str) -> int:
        return self._inner.host(symbol)


def pack_key(overlay_id: int, body_offset: int) -> int:
    if not 0 <= overlay_id <= _KEY_MAX:
        raise OverlayError(
            f"overlay id {overlay_id} does not fit in the {_KEY_MAX:#x} key field")
    if not 0 <= body_offset <= _KEY_MAX:
        raise OverlayError(
            f"body offset {body_offset:#x} does not fit in the {_KEY_MAX:#x} key field")
    return (overlay_id << 16) | body_offset


def _stub_addresses(clustering: Clustering, layout: StubLayout) -> dict[int, int]:
    return {
        func.address: layout.address(index)
        for index, func in enumerate(
            func for plan in clustering.overlays for func in plan.functions)
    }


def place_stubs(blobs: list[OverlayBlob], layout: StubLayout) -> list[FunctionStub]:
    stubs: list[FunctionStub] = []
    index = 0
    for blob in blobs:
        for placement in blob.placements:
            stubs.append(FunctionStub(
                name=placement.name,
                canonical=placement.address,
                overlay_id=blob.overlay_id,
                body_offset=placement.offset,
                address=layout.address(index),
                key=pack_key(blob.overlay_id, placement.offset),
            ))
            index += 1
    return stubs


def build_overlays(blobs: list[OverlayBlob]) -> list[OverlayImage]:
    overlays: list[OverlayImage] = []
    offset = 0
    for blob in blobs:
        overlays.append(OverlayImage(
            overlay_id=blob.overlay_id, offset=offset, length=len(blob.data)))
        offset = align_up(offset + len(blob.data), STUB_ALIGNMENT)
    return overlays


def concat_blobs(blobs: list[OverlayBlob]) -> bytes:
    chunks: list[bytes] = []
    emitted = 0
    for index, blob in enumerate(blobs):
        chunks.append(blob.data)
        emitted += len(blob.data)
        if index + 1 < len(blobs):
            pad = align_up(emitted, STUB_ALIGNMENT) - emitted
            chunks.append(bytes(pad))
            emitted += pad
    return b"".join(chunks)


def _section_of(image: Image, address: int) -> str | None:
    for section in image.sections:
        if 0 <= address - section.address < section.size:
            return section.name
    return None


def resolve_redirects(image: Image, clustering: Clustering,
                      relocations: list[Relocation],
                      stubs: list[FunctionStub]) -> list[Redirect]:
    paged = {func.section for plan in clustering.overlays for func in plan.functions}
    stub_by_address = {stub.canonical: stub.address for stub in stubs}

    redirects: dict[int, Redirect] = {}
    for reloc in relocations:
        if reloc.type != R_RELATIVE:
            continue
        if _section_of(image, reloc.offset) in paged:
            continue
        word = image.word_at(reloc.offset)
        target = None if word is None else stub_by_address.get(word)
        if target is not None:
            redirects.setdefault(reloc.offset, Redirect(site=reloc.offset, stub=target))
    return [redirects[site] for site in sorted(redirects)]


def _app_ident(app: str) -> str:
    ident = re.sub(r"[^0-9A-Za-z_]", "_", app)
    if not ident or ident[0].isdigit():
        ident = "_" + ident
    return ident


def render_stubs(stubs: list[FunctionStub]) -> str:
    lines = [
        "/* generated by tools/pm/emit.py -- do not edit */",
        "",
        "    .section .overlay.slots, \"aw\", @progbits",
        "    .p2align 2",
        f"    .globl {_ENTER_SLOT}",
        f"    .type {_ENTER_SLOT}, @object",
        f"    .size {_ENTER_SLOT}, 4",
        f"{_ENTER_SLOT}:",
        "    .word 0",
        f"    .globl {_LEAVE_SLOT}",
        f"    .type {_LEAVE_SLOT}, @object",
        f"    .size {_LEAVE_SLOT}, 4",
        f"{_LEAVE_SLOT}:",
        "    .word 0",
    ]
    for index, stub in enumerate(stubs):
        lines += [
            f"    .type .Lkey_{index}, @object",
            f"    .size .Lkey_{index}, 4",
            f".Lkey_{index}:",
            f"    .word {stub.key:#010x}    /* {stub.name} */",
        ]

    lines += ["", "    .section .overlay.stubs, \"ax\", @progbits", "    .p2align 2"]
    for index, stub in enumerate(stubs):
        name = f"ov_stub_{index}"
        lines += [
            "",
            "    .p2align 2",
            f"    .globl {name}",
            f"    .type {name}, @function",
            f"{name}:",
        ]
        lines += [
            "    " + template.format(key=f".Lkey_{index}")
            for template, _ in _STUB_SEQUENCE
        ]
        lines.append(f"    .size {name}, . - {name}")
    return "\n".join(lines) + "\n"


def _struct(name: str, fields: list[tuple[str, str]]) -> list[str]:
    lines = ["typedef struct {"]
    lines += [f"    {ctype} {fname};" for ctype, fname in fields]
    lines.append(f"}} {name};")
    return lines


def render_header(manifest: Manifest) -> str:
    macro = _app_ident(manifest.app).upper()
    ident = _app_ident(manifest.app)

    lines = [
        "/* generated by tools/pm/emit.py -- do not edit */",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        f"#define {macro}_ENTER_SLOT 0x{manifest.slots.enter:08x}u",
        f"#define {macro}_LEAVE_SLOT 0x{manifest.slots.leave:08x}u",
        "",
        f"#define {macro}_OVERLAY_COUNT {len(manifest.overlays)}",
        f"#define {macro}_STUB_COUNT {len(manifest.stubs)}",
        f"#define {macro}_REDIRECT_COUNT {len(manifest.redirects)}",
        f"#define {macro}_PATCH_COUNT {len(manifest.patches)}",
        "",
    ]
    lines += _struct(f"{ident}_overlay_t", [
        ("uint16_t", "id"), ("uint32_t", "offset"), ("uint32_t", "length")])
    lines.append("")
    lines += _struct(f"{ident}_stub_t", [
        ("uint32_t", "entry"), ("uint16_t", "overlay"),
        ("uint16_t", "body"), ("uint32_t", "stub")])
    lines.append("")
    lines += _struct(f"{ident}_redirect_t", [
        ("uint32_t", "site"), ("uint32_t", "stub")])
    lines.append("")
    lines += _struct(f"{ident}_patch_t", [
        ("uint16_t", "overlay"), ("uint16_t", "offset")])
    lines.append("")

    lines += _array(f"{ident}_overlay_t", f"{ident}_overlays",
                    f"{macro}_OVERLAY_COUNT", [
                        (f"{o.overlay_id}", f"{o.offset:#010x}", f"{o.length:#010x}")
                        for o in manifest.overlays])
    lines += _array(f"{ident}_stub_t", f"{ident}_stubs",
                    f"{macro}_STUB_COUNT", [
                        (f"{s.canonical:#010x}", f"{s.overlay_id}", f"{s.body_offset:#06x}",
                         f"{s.address:#010x}") + (f"/* {s.name} */",)
                        for s in manifest.stubs])
    lines += _array(f"{ident}_redirect_t", f"{ident}_redirects",
                    f"{macro}_REDIRECT_COUNT", [
                        (f"{r.site:#010x}", f"{r.stub:#010x}") for r in manifest.redirects])
    lines += _array(f"{ident}_patch_t", f"{ident}_patches",
                    f"{macro}_PATCH_COUNT", [
                        (f"{p.overlay_id}", f"{p.blob_offset:#06x}")
                        for p in manifest.patches])

    text = "\n".join(lines).rstrip() + "\n"
    return text


def _array(ctype: str, name: str, count_macro: str,
           rows: list[tuple[str, ...]]) -> list[str]:
    if not rows:
        return [f"static const {ctype} {name}[1] = {{ 0 }};  /* {count_macro} == 0 */", ""]
    lines = [f"static const {ctype} {name}[{count_macro}] = {{"]
    for row in rows:
        comment = ""
        if row and row[-1].startswith("/*"):
            comment = " " + row[-1]
            row = row[:-1]
        lines.append("    { " + ", ".join(row) + "}," + comment)
    lines.append("};")
    lines.append("")
    return lines


def emit(image: Image, analysis: Analysis, clustering: Clustering,
         relocations: list[Relocation], resolver: AddressResolver,
         *, app: str, stub_base: int, slot_base: int) -> Emission:
    layout = StubLayout(stub_base=stub_base, slot_base=slot_base)
    if stub_base % STUB_ALIGNMENT or slot_base % STUB_ALIGNMENT:
        raise OverlayError("stub_base and slot_base must be 4-byte aligned")
    if layout.stride < STUB_CODE_SIZE or layout.stride % STUB_ALIGNMENT:
        raise OverlayError(
            f"stub stride {layout.stride} cannot hold {STUB_CODE_SIZE} bytes")

    predicted = _stub_addresses(clustering, layout)
    blobs = build_blobs(image, analysis, clustering, relocations,
                        _StubResolver(resolver, predicted))
    stubs = place_stubs(blobs, layout)
    for stub in stubs:
        if predicted.get(stub.canonical) != stub.address:
            raise OverlayError(f"stub placement for {stub.name} is inconsistent")

    manifest = Manifest(
        app=app,
        stubs=tuple(stubs),
        overlays=tuple(build_overlays(blobs)),
        slots=StubSlot(enter=slot_base, leave=slot_base + 4),
        redirects=tuple(resolve_redirects(image, clustering, relocations, stubs)),
        patches=tuple(PoolPatch(blob.overlay_id, offset)
                      for blob in blobs for offset in blob.patches),
    )
    return Emission(
        manifest=manifest,
        blob=concat_blobs(blobs),
        stubs_asm=render_stubs(stubs),
        header=render_header(manifest),
    )


BLOB_MAGIC = b"PMOV"
BLOB_VERSION = 1
BLOB_FLAG_APP_BASE = 0x1

_BLOB_HEADER = struct.Struct("<4sHHIIIHHHH")
_BLOB_OVERLAY = struct.Struct("<HHII")
_BLOB_STUB = struct.Struct("<IHHI")
_BLOB_REDIRECT = struct.Struct("<II")
_BLOB_PATCH = struct.Struct("<HH")


def render_blob_header(manifest: Manifest, app_base: int | None = None) -> bytes:
    flags = 0 if app_base is None else BLOB_FLAG_APP_BASE
    out = bytearray(_BLOB_HEADER.pack(
        BLOB_MAGIC, BLOB_VERSION, flags, app_base or 0,
        manifest.slots.enter, manifest.slots.leave,
        len(manifest.overlays), len(manifest.stubs),
        len(manifest.redirects), len(manifest.patches)))
    for overlay in manifest.overlays:
        out += _BLOB_OVERLAY.pack(overlay.overlay_id, 0,
                                  overlay.offset, overlay.length)
    for stub in manifest.stubs:
        out += _BLOB_STUB.pack(stub.canonical, stub.overlay_id,
                               stub.body_offset, stub.address)
    for redirect in manifest.redirects:
        out += _BLOB_REDIRECT.pack(redirect.site, redirect.stub)
    for patch in manifest.patches:
        out += _BLOB_PATCH.pack(patch.overlay_id, patch.blob_offset)
    return bytes(out)


def build_overlay_blob(emission: Emission, app_base: int | None = None) -> bytes:
    return render_blob_header(emission.manifest, app_base=app_base) + emission.blob


def write_emission(emission: Emission, out_dir: str,
                   app_base: int | None = None) -> dict[str, str]:
    ident = _app_ident(emission.manifest.app)
    paths = {
        "bin": os.path.join(out_dir, f"{ident}.overlay.bin"),
        "stubs": os.path.join(out_dir, f"{ident}.overlay_stubs.S"),
        "header": os.path.join(out_dir, f"{ident}.overlay_manifest.h"),
    }
    with open(paths["bin"], "wb") as handle:
        handle.write(build_overlay_blob(emission, app_base=app_base))
    with open(paths["stubs"], "w", encoding="utf-8") as handle:
        handle.write(emission.stubs_asm)
    with open(paths["header"], "w", encoding="utf-8") as handle:
        handle.write(emission.header)
    return paths
