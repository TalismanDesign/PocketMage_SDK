
from __future__ import annotations

import os
import re
import struct
import subprocess
import tempfile
from dataclasses import dataclass

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

    count: int

    @property
    def slots(self) -> "StubSlot":
        return StubSlot(enter=0, leave=4)

    @property
    def keys_offset(self) -> int:
        return 8

    def key_offset(self, index: int) -> int:
        return self.keys_offset + index * 4

    def stub_offset(self, index: int) -> int:
        return self.keys_offset + self.count * 4 + index * STUB_STRIDE

    @property
    def size(self) -> int:
        return self.keys_offset + self.count * 4 + self.count * STUB_STRIDE


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
    offset: int  
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


@dataclass(frozen=True)
class HostRef:

    overlay_id: int
    blob_offset: int  
    name: str


@dataclass
class Manifest:

    app: str
    stubs: tuple[FunctionStub, ...] = ()
    overlays: tuple[OverlayImage, ...] = ()
    slots: StubSlot = StubSlot(0, 4)
    redirects: tuple[Redirect, ...] = ()
    patches: tuple[PoolPatch, ...] = ()
    stub_patches: tuple[PoolPatch, ...] = ()
    host_refs: tuple[HostRef, ...] = ()
    stub_block_size: int = 0


@dataclass
class Emission:

    manifest: Manifest
    blob: bytes
    stubs_asm: str
    header: str


class _StubResolver:

    def __init__(self, addresses: dict[int, int]):
        self._addresses = addresses
        self.host_names: list[str] = []

    def function(self, canonical: int) -> int:
        stub = self._addresses.get(canonical)
        return stub if stub is not None else canonical

    def data(self, address: int) -> int:
        return address

    def host(self, symbol: str) -> int:
        self.host_names.append(symbol)
        return 0


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
        func.address: layout.stub_offset(index)
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
                offset=layout.stub_offset(index),
                key=pack_key(blob.overlay_id, placement.offset),
            ))
            index += 1
    return stubs


def build_overlays(blobs: list[OverlayBlob], first_offset: int) -> list[OverlayImage]:
    overlays: list[OverlayImage] = []
    offset = first_offset
    for blob in blobs:
        length = align_up(len(blob.data), STUB_ALIGNMENT)
        overlays.append(OverlayImage(
            overlay_id=blob.overlay_id, offset=offset, length=length))
        offset += length
    return overlays


def concat_blobs(blobs: list[OverlayBlob]) -> bytes:
    chunks: list[bytes] = []
    for blob in blobs:
        chunks.append(blob.data)
        pad = align_up(len(blob.data), STUB_ALIGNMENT) - len(blob.data)
        if pad:
            chunks.append(bytes(pad))
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
    stub_by_address = {stub.canonical: stub.offset for stub in stubs}

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
        '    .section .overlay.stubblock, "awx", @progbits',
        "    .p2align 2",
        f"    .type {_ENTER_SLOT}, @object",
        f"    .size {_ENTER_SLOT}, 4",
        f"{_ENTER_SLOT}:",
        "    .word 0",
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
    lines.append("    .p2align 2")
    return "\n".join(lines) + "\n"


def _tool_sibling(readelf: str, name: str) -> str:
    if readelf.endswith("readelf"):
        return readelf[: -len("readelf")] + name
    return name


def assemble_stub_block(asm: str, readelf: str) -> bytes:
    assembler = _tool_sibling(readelf, "as")
    objcopy = _tool_sibling(readelf, "objcopy")
    if not os.path.isfile(assembler) or not os.path.isfile(objcopy):
        raise OverlayError(
            f"no xtensa assembler/objcopy next to {readelf}; cannot build the "
            "stub block")
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "stubs.S")
        obj = os.path.join(tmp, "stubs.o")
        with open(src, "w", encoding="utf-8") as handle:
            handle.write(asm)
        result = subprocess.run([assembler, "-o", obj, src],
                                capture_output=True, text=True)
        if result.returncode != 0:
            raise OverlayError(
                f"assembling the generated stubs failed: {result.stderr.strip()}")
        out = os.path.join(tmp, "stubs.bin")
        result = subprocess.run(
            [objcopy, "-O", "binary", "--only-section=.overlay.stubblock", obj, out],
            capture_output=True, text=True)
        if result.returncode != 0:
            raise OverlayError(
                f"extracting .overlay.stubblock failed: {result.stderr.strip()}")
        with open(out, "rb") as handle:
            return handle.read()


def _struct(name: str, fields: list[tuple[str, str]]) -> list[str]:
    lines = ["typedef struct {"]
    lines += [f"    {ctype} {fname};" for ctype, fname in fields]
    lines.append(f"}} {name};")
    return lines


def _c_escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"')


def render_header(manifest: Manifest) -> str:
    macro = _app_ident(manifest.app).upper()
    ident = _app_ident(manifest.app)

    lines = [
        "/* generated by tools/pm/emit.py -- do not edit */",
        "/* all addresses are relative: the loader adds the stub-block base",
        "   to stub offsets and the app load base to patch/redirect sites */",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        f"#define {macro}_ENTER_SLOT_OFF 0x{manifest.slots.enter:08x}u",
        f"#define {macro}_LEAVE_SLOT_OFF 0x{manifest.slots.leave:08x}u",
        "",
        f"#define {macro}_OVERLAY_COUNT {len(manifest.overlays)}",
        f"#define {macro}_STUB_COUNT {len(manifest.stubs)}",
        f"#define {macro}_REDIRECT_COUNT {len(manifest.redirects)}",
        f"#define {macro}_PATCH_COUNT {len(manifest.patches)}",
        f"#define {macro}_STUB_PATCH_COUNT {len(manifest.stub_patches)}",
        f"#define {macro}_HOST_COUNT {len(manifest.host_refs)}",
        "",
    ]
    lines += _struct(f"{ident}_overlay_t", [
        ("uint16_t", "id"), ("uint32_t", "offset"), ("uint32_t", "length")])
    lines.append("")
    lines += _struct(f"{ident}_stub_t", [
        ("uint32_t", "entry"), ("uint16_t", "overlay"),
        ("uint16_t", "body"), ("uint32_t", "stub_off")])
    lines.append("")
    lines += _struct(f"{ident}_redirect_t", [
        ("uint32_t", "site"), ("uint32_t", "stub_off")])
    lines.append("")
    lines += _struct(f"{ident}_patch_t", [
        ("uint16_t", "overlay"), ("uint16_t", "offset")])
    lines.append("")
    lines += _struct(f"{ident}_host_t", [
        ("uint16_t", "overlay"), ("uint16_t", "offset"),
        ("uint32_t", "name_off")])
    lines.append("")

    lines += _array(f"{ident}_overlay_t", f"{ident}_overlays",
                    f"{macro}_OVERLAY_COUNT", [
                        (f"{o.overlay_id}", f"{o.offset:#010x}", f"{o.length:#010x}")
                        for o in manifest.overlays])
    lines += _array(f"{ident}_stub_t", f"{ident}_stubs",
                    f"{macro}_STUB_COUNT", [
                        (f"{s.canonical:#010x}", f"{s.overlay_id}", f"{s.body_offset:#06x}",
                         f"{s.offset:#010x}") + (f"/* {s.name} */",)
                        for s in manifest.stubs])
    lines += _array(f"{ident}_redirect_t", f"{ident}_redirects",
                    f"{macro}_REDIRECT_COUNT", [
                        (f"{r.site:#010x}", f"{r.stub:#010x}")
                        for r in manifest.redirects])
    lines += _array(f"{ident}_patch_t", f"{ident}_patches",
                    f"{macro}_PATCH_COUNT", [
                        (f"{p.overlay_id}", f"{p.blob_offset:#06x}")
                        for p in manifest.patches])
    lines += _array(f"{ident}_patch_t", f"{ident}_stub_patches",
                    f"{macro}_STUB_PATCH_COUNT", [
                        (f"{p.overlay_id}", f"{p.blob_offset:#06x}")
                        for p in manifest.stub_patches])

    names: dict[str, int] = {}
    strtab = b""
    for ref in manifest.host_refs:
        if ref.name not in names:
            names[ref.name] = len(strtab)
            strtab += ref.name.encode() + b"\x00"
    lines += _array(f"{ident}_host_t", f"{ident}_host_refs",
                    f"{macro}_HOST_COUNT", [
                        (f"{h.overlay_id}", f"{h.blob_offset:#06x}",
                         f"{names[h.name]:#010x}") + (f"/* {h.name} */",)
                        for h in manifest.host_refs])
    if strtab:
        c_text = "".join(f"{_c_escape(part)}\\0" for part in
                         strtab.rstrip(b"\x00").decode().split("\x00"))
        lines.append(f'static const char {ident}_strtab[] = "{c_text}";')
    else:
        lines.append(f"static const char {ident}_strtab[1] = {{ 0 }};")
    lines.append("")

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
         relocations: list[Relocation], *, app: str) -> Emission:
    layout = StubLayout(sum(len(plan.functions) for plan in clustering.overlays))

    resolver = _StubResolver(_stub_addresses(clustering, layout))
    blobs = build_blobs(image, analysis, clustering, relocations, resolver)
    stubs = place_stubs(blobs, layout)
    for index, stub in enumerate(stubs):
        expected = layout.stub_offset(index)
        if stub.offset != expected:
            raise OverlayError(f"stub placement for {stub.name} is inconsistent")

    addresses = set(resolver._addresses.values())
    stub_patches: list[PoolPatch] = []
    patches: list[PoolPatch] = []
    for blob in blobs:
        for offset in blob.patches:
            word = int.from_bytes(blob.data[offset:offset + 4], "little")
            entry = PoolPatch(blob.overlay_id, offset)
            (stub_patches if word in addresses else patches).append(entry)

    manifest = Manifest(
        app=app,
        stubs=tuple(stubs),
        overlays=tuple(OverlayImage(b.overlay_id, 0, len(b.data))
                       for b in blobs),
        slots=layout.slots,
        redirects=tuple(resolve_redirects(image, clustering, relocations, stubs)),
        patches=tuple(patches),
        stub_patches=tuple(stub_patches),
        host_refs=tuple(HostRef(blob.overlay_id, offset, name)
                        for blob in blobs for offset, name in blob.host_refs),
        stub_block_size=layout.size,
    )
    manifest.overlays = tuple(build_overlays(blobs, _first_overlay_offset(manifest)))
    return Emission(
        manifest=manifest,
        blob=concat_blobs(blobs),
        stubs_asm=render_stubs(stubs),
        header=render_header(manifest),
    )


BLOB_MAGIC = b"PMOV"
BLOB_VERSION = 2

_BLOB_HEADER = struct.Struct("<4sHH6H6I")
_BLOB_OVERLAY = struct.Struct("<HHII")
_BLOB_STUB = struct.Struct("<IHHI")
_BLOB_REDIRECT = struct.Struct("<II")
_BLOB_PATCH = struct.Struct("<HH")
_BLOB_HOST = struct.Struct("<HHI")


def render_blob_header(manifest: Manifest) -> bytes:
    names: dict[str, int] = {}
    strtab = b""
    for ref in manifest.host_refs:
        if ref.name not in names:
            names[ref.name] = len(strtab)
            strtab += ref.name.encode() + b"\x00"

    fixed = _BLOB_HEADER.size
    strtab_off = (fixed + 12 * len(manifest.overlays) + 12 * len(manifest.stubs)
                  + 8 * len(manifest.redirects) + 4 * len(manifest.patches)
                  + 4 * len(manifest.stub_patches) + 8 * len(manifest.host_refs))
    stub_block_off = align_up(strtab_off + len(strtab), STUB_ALIGNMENT)

    out = bytearray(_BLOB_HEADER.pack(
        BLOB_MAGIC, BLOB_VERSION, 0,
        len(manifest.overlays), len(manifest.stubs), len(manifest.redirects),
        len(manifest.patches), len(manifest.stub_patches), len(manifest.host_refs),
        stub_block_off, manifest.stub_block_size,
        manifest.slots.enter, manifest.slots.leave,
        strtab_off, len(strtab)))
    for overlay in manifest.overlays:
        out += _BLOB_OVERLAY.pack(overlay.overlay_id, 0,
                                  overlay.offset, overlay.length)
    for stub in manifest.stubs:
        out += _BLOB_STUB.pack(stub.canonical, stub.overlay_id,
                               stub.body_offset, stub.offset)
    for redirect in manifest.redirects:
        out += _BLOB_REDIRECT.pack(redirect.site, redirect.stub)
    for patch in manifest.patches:
        out += _BLOB_PATCH.pack(patch.overlay_id, patch.blob_offset)
    for patch in manifest.stub_patches:
        out += _BLOB_PATCH.pack(patch.overlay_id, patch.blob_offset)
    for ref in manifest.host_refs:
        out += _BLOB_HOST.pack(ref.overlay_id, ref.blob_offset, names[ref.name])
    out += strtab
    out += bytes(stub_block_off - len(out))
    return bytes(out)


def _first_overlay_offset(manifest: Manifest) -> int:
    return align_up(len(render_blob_header(manifest)) + manifest.stub_block_size,
                    STUB_ALIGNMENT)


def build_overlay_blob(emission: Emission, stub_block: bytes) -> bytes:
    if len(stub_block) != emission.manifest.stub_block_size:
        raise OverlayError(
            f"stub block is {len(stub_block)} bytes but the manifest expects "
            f"{emission.manifest.stub_block_size}")
    return render_blob_header(emission.manifest) + stub_block + emission.blob


def write_emission(emission: Emission, out_dir: str,
                   stub_block: bytes) -> dict[str, str]:
    ident = _app_ident(emission.manifest.app)
    paths = {
        "bin": os.path.join(out_dir, f"{ident}.overlay.bin"),
        "stubs": os.path.join(out_dir, f"{ident}.overlay_stubs.S"),
        "header": os.path.join(out_dir, f"{ident}.overlay_manifest.h"),
    }
    with open(paths["bin"], "wb") as handle:
        handle.write(build_overlay_blob(emission, stub_block))
    with open(paths["stubs"], "w", encoding="utf-8") as handle:
        handle.write(emission.stubs_asm)
    with open(paths["header"], "w", encoding="utf-8") as handle:
        handle.write(emission.header)
    return paths
