
import os
import re
import shutil
import struct
import subprocess
import tempfile
import unittest

from tools.pm import app as app_mod
from tools.pm import emit as emit_mod
from tools.pm import overlay as overlay_mod
from tools.pm import relink as relink_mod
from tools.pm.emit import (
    FunctionStub,
    Manifest,
    OverlayImage,
    PoolPatch,
    Redirect,
    StubLayout,
    StubSlot,
)
from tools.pm.overlay import (
    Analysis,
    Clustering,
    FunctionSymbol,
    Image,
    OverlayPlan,
    Section,
)
from tools.pm.relink import (
    R_JMP_SLOT,
    R_RELATIVE,
    BodyPlacement,
    OverlayBlob,
    Relocation,
)

from tools.pm.tests.test_relink import FakeResolver

_CALLGRAPH = os.path.join(os.path.dirname(__file__), "fixtures", "callgraph")
_STUB_BASE = 0x40000000
_SLOT_BASE = 0x40002000
_HOSTS = {"pm_text_width": 0x40001234}

_OBJDUMP_SYMBOL_RE = re.compile(
    r"^([0-9a-f]+)\s+g\s+([FO])\s+(\S+)\s+([0-9a-f]+)\s+(\S+)\s*$"
)


def _stub(name, canonical, overlay_id, body_offset, index):
    address = _STUB_BASE + index * emit_mod.STUB_STRIDE
    return FunctionStub(name, canonical, overlay_id, body_offset, address,
                        emit_mod.pack_key(overlay_id, body_offset))


def _redirect_image():
    sections = [
        Section(6, ".text", 0x100, 0x100, 0x20),
        Section(7, ".got.loc", 0x200, 0x200, 0x20),
    ]
    data = bytearray(0x300)
    data[0x100:0x104] = (0x110).to_bytes(4, "little")
    data[0x200:0x204] = (0x110).to_bytes(4, "little")
    data[0x204:0x208] = (0x2A0).to_bytes(4, "little")
    return Image(sections=sections, data=bytes(data))


def _redirect_clustering():
    func = FunctionSymbol("_Z1Av", 0x110, 0x10, ".text")
    return Clustering(overlays=[OverlayPlan(overlay_id=0, functions=(func,), size=0x10)])


class PackKey(unittest.TestCase):

    def test_packs_overlay_and_offset(self):
        self.assertEqual(emit_mod.pack_key(0, 0), 0)
        self.assertEqual(emit_mod.pack_key(1, 0x18), 0x10018)
        self.assertEqual(emit_mod.pack_key(2, 0x3A), 0x2003A)

    def test_rejects_out_of_range_fields(self):
        with self.assertRaises(overlay_mod.OverlayError):
            emit_mod.pack_key(0x10000, 0)
        with self.assertRaises(overlay_mod.OverlayError):
            emit_mod.pack_key(0, 0x10000)


class StubLayoutSpec(unittest.TestCase):

    def test_stride_matches_the_code_size(self):
        self.assertEqual(emit_mod.STUB_CODE_SIZE, 42)
        self.assertEqual(emit_mod.STUB_STRIDE, relink_mod.align_up(42, 4))
        self.assertEqual(StubLayout(_STUB_BASE, _SLOT_BASE).stride, emit_mod.STUB_STRIDE)

    def test_addresses_step_by_stride(self):
        layout = StubLayout(stub_base=0x40000000, slot_base=0x40001000)
        self.assertEqual(layout.address(0), 0x40000000)
        self.assertEqual(layout.address(1), 0x40000000 + emit_mod.STUB_STRIDE)
        custom = StubLayout(stub_base=0x1000, slot_base=0x2000, stride=64)
        self.assertEqual(layout.address(3), 0x40000000 + 3 * emit_mod.STUB_STRIDE)
        self.assertEqual(custom.address(2), 0x1000 + 128)


class PlaceStubs(unittest.TestCase):

    def setUp(self):
        self.blobs = [
            OverlayBlob(overlay_id=0, data=b"x" * 8, placements=[
                BodyPlacement("_Z1Av", 0x110, 4, 0),
                BodyPlacement("_Z1Bv", 0x120, 4, 4),
            ]),
            OverlayBlob(overlay_id=1, data=b"y" * 4,
                        placements=[BodyPlacement("_Z1Cv", 0x130, 4, 0)]),
        ]

    def test_order_keys_and_addresses(self):
        stubs = emit_mod.place_stubs(
            self.blobs, StubLayout(_STUB_BASE, _SLOT_BASE))
        self.assertEqual([s.canonical for s in stubs], [0x110, 0x120, 0x130])
        self.assertEqual([s.overlay_id for s in stubs], [0, 0, 1])
        self.assertEqual([s.body_offset for s in stubs], [0, 4, 0])
        self.assertEqual([s.key for s in stubs], [0, 4, 0x10000])
        self.assertEqual([s.address for s in stubs],
                         [_STUB_BASE + i * emit_mod.STUB_STRIDE for i in range(3)])


class OverlayImages(unittest.TestCase):

    def test_offsets_are_aligned_and_contiguous(self):
        blobs = [OverlayBlob(overlay_id=0, data=b"A" * 6),
                 OverlayBlob(overlay_id=1, data=b"B" * 5)]
        images = emit_mod.build_overlays(blobs)
        self.assertEqual(images, [OverlayImage(0, 0, 6), OverlayImage(1, 8, 5)])

    def test_concat_pads_between_blobs(self):
        blobs = [OverlayBlob(overlay_id=0, data=b"A" * 6),
                 OverlayBlob(overlay_id=1, data=b"B" * 5)]
        self.assertEqual(emit_mod.concat_blobs(blobs), b"A" * 6 + b"\x00\x00" + b"B" * 5)

    def test_concat_does_not_pad_after_the_last_blob(self):
        blobs = [OverlayBlob(overlay_id=0, data=b"A" * 5)]
        self.assertEqual(emit_mod.concat_blobs(blobs), b"A" * 5)


class ResolveRedirects(unittest.TestCase):

    def setUp(self):
        self.image = _redirect_image()
        self.clustering = _redirect_clustering()
        self.stubs = [_stub("_Z1Av", 0x110, 0, 0, 0)]

    def _resolve(self, relocs):
        return emit_mod.resolve_redirects(self.image, self.clustering, relocs, self.stubs)

    def test_resident_pointer_to_paged_function_is_redirected(self):
        relocs = [Relocation(0x200, R_RELATIVE)]
        self.assertEqual(self._resolve(relocs), [Redirect(0x200, _STUB_BASE)])

    def test_pointer_inside_the_paged_section_is_left_to_the_blob(self):
        relocs = [Relocation(0x100, R_RELATIVE)]
        self.assertEqual(self._resolve(relocs), [])

    def test_resident_pointer_to_data_is_not_redirected(self):
        relocs = [Relocation(0x204, R_RELATIVE)]
        self.assertEqual(self._resolve(relocs), [])

    def test_non_relative_relocations_are_ignored(self):
        relocs = [Relocation(0x208, R_JMP_SLOT, "pm_text_width")]
        self.assertEqual(self._resolve(relocs), [])

    def test_redirects_are_sorted_by_site(self):
        relocs = [Relocation(0x20C, R_RELATIVE), Relocation(0x200, R_RELATIVE)]
        image = bytearray(self.image.data)
        image[0x20C:0x210] = (0x110).to_bytes(4, "little")
        self.image = Image(sections=self.image.sections, data=bytes(image))
        self.assertEqual([r.site for r in self._resolve(relocs)], [0x200, 0x20C])


class RenderStubs(unittest.TestCase):

    def test_sequence_length_defines_the_stub_size(self):
        self.assertEqual(len(emit_mod._STUB_SEQUENCE), 17)
        self.assertEqual(sum(length for _, length in emit_mod._STUB_SEQUENCE),
                         emit_mod.STUB_CODE_SIZE)

    def test_labels_section_and_keys(self):
        asm = emit_mod.render_stubs([_stub("_Z1Av", 0x110, 1, 0x18, 0)])
        self.assertIn(".section .overlay.slots", asm)
        self.assertIn(".section .overlay.stubs", asm)
        self.assertIn("overlay_enter_slot:", asm)
        self.assertIn("overlay_leave_slot:", asm)
        self.assertIn("ov_stub_0:", asm)
        self.assertIn(".Lkey_0:", asm)
        self.assertIn(".word 0x00010018", asm)

    def test_one_instruction_block_per_stub(self):
        asm = emit_mod.render_stubs([_stub("a", 0x110, 0, 0, 0),
                                     _stub("b", 0x120, 0, 4, 1)])
        instructions = [line for line in asm.splitlines()
                        if line.startswith("    ") and not line.startswith("    .")]
        self.assertEqual(len(instructions), 2 * len(emit_mod._STUB_SEQUENCE))


class RenderHeader(unittest.TestCase):

    def test_counts_and_slot_defines(self):
        manifest = Manifest(app="callgraph",
                            slots=StubSlot(_SLOT_BASE, _SLOT_BASE + 4))
        header = emit_mod.render_header(manifest)
        self.assertIn("#define CALLGRAPH_ENTER_SLOT 0x40002000u", header)
        self.assertIn("#define CALLGRAPH_LEAVE_SLOT 0x40002004u", header)
        self.assertIn("#define CALLGRAPH_OVERLAY_COUNT 0", header)
        self.assertIn("#define CALLGRAPH_STUB_COUNT 0", header)
        self.assertIn("#define CALLGRAPH_REDIRECT_COUNT 0", header)

    def test_empty_tables_stay_compilable(self):
        header = emit_mod.render_header(Manifest(app="x"))
        self.assertIn("x_stubs[1] = { 0 }", header)
        self.assertIn("x_redirects[1] = { 0 }", header)

    def test_rows_carry_overlay_and_entry_fields(self):
        manifest = Manifest(app="x",
                            overlays=(OverlayImage(3, 0x10, 0x20),),
                            stubs=(_stub("f", 0x110, 2, 0x18, 0),))
        header = emit_mod.render_header(manifest)
        self.assertIn("{ 3, 0x00000010, 0x00000020}", header)
        self.assertIn("{ 0x00000110, 2, 0x0018, 0x40000000}", header)

    def test_ident_is_sanitized(self):
        self.assertEqual(emit_mod._app_ident("callgraph"), "callgraph")
        self.assertEqual(emit_mod._app_ident("my-app"), "my_app")
        self.assertEqual(emit_mod._app_ident("1foo"), "_1foo")
        self.assertEqual(emit_mod._app_ident(""), "_")


class EmitRejectsBadLayout(unittest.TestCase):

    def setUp(self):
        self.image = _redirect_image()
        self.analysis = Analysis(functions=[FunctionSymbol("_Z1Av", 0x110, 0x10, ".text")])
        self.relocs = [Relocation(0x200, R_RELATIVE)]

    def _emit(self, stub_base, slot_base):
        return emit_mod.emit(self.image, self.analysis, _redirect_clustering(),
                             self.relocs, FakeResolver(hosts=_HOSTS),
                             app="x", stub_base=stub_base, slot_base=slot_base)

    def test_misaligned_stub_base_is_rejected(self):
        with self.assertRaises(overlay_mod.OverlayError):
            self._emit(_STUB_BASE + 2, _SLOT_BASE)

    def test_misaligned_slot_base_is_rejected(self):
        with self.assertRaises(overlay_mod.OverlayError):
            self._emit(_STUB_BASE, _SLOT_BASE + 2)


class RenderBlobHeader(unittest.TestCase):

    def _manifest(self):
        return Manifest(app="callgraph",
                        stubs=(_stub("f", 0x110, 0, 0, 0),),
                        overlays=(OverlayImage(0, 0, 0x20),),
                        slots=StubSlot(_SLOT_BASE, _SLOT_BASE + 4),
                        redirects=(Redirect(0x200, _STUB_BASE),),
                        patches=(PoolPatch(0, 0x24),))

    def test_fixed_layout(self):
        header = emit_mod.render_blob_header(self._manifest(), app_base=0x42000000)
        self.assertEqual(header[:4], emit_mod.BLOB_MAGIC)
        version, flags = struct.unpack_from("<HH", header, 4)
        self.assertEqual(version, emit_mod.BLOB_VERSION)
        self.assertEqual(flags, emit_mod.BLOB_FLAG_APP_BASE)
        app_base, enter, leave = struct.unpack_from("<III", header, 8)
        self.assertEqual((app_base, enter, leave), (0x42000000, _SLOT_BASE, _SLOT_BASE + 4))
        self.assertEqual(struct.unpack_from("<HHHH", header, 20), (1, 1, 1, 1))
        self.assertEqual(struct.unpack_from("<HHII", header, 28), (0, 0, 0, 0x20))
        self.assertEqual(struct.unpack_from("<IHHI", header, 40), (0x110, 0, 0, _STUB_BASE))
        self.assertEqual(struct.unpack_from("<II", header, 52), (0x200, _STUB_BASE))
        self.assertEqual(struct.unpack_from("<HH", header, 60), (0, 0x24))
        self.assertEqual(len(header), 64)

    def test_deferred_mode_marks_no_app_base(self):
        header = emit_mod.render_blob_header(self._manifest(), app_base=None)
        _version, flags = struct.unpack_from("<HH", header, 4)
        self.assertEqual(flags, 0)
        app_base, = struct.unpack_from("<I", header, 8)
        self.assertEqual(app_base, 0)

    def test_empty_manifest_still_has_the_fixed_header(self):
        header = emit_mod.render_blob_header(Manifest(app="x"))
        self.assertEqual(len(header), 28)
        self.assertEqual(struct.unpack_from("<HHHH", header, 20), (0, 0, 0, 0))


class BuildOverlayBlob(unittest.TestCase):

    def test_header_then_bodies(self):
        emission = emit_mod.emit(
            _redirect_image(),
            Analysis(functions=[FunctionSymbol("_Z1Av", 0x110, 0x10, ".text")]),
            _redirect_clustering(), [Relocation(0x200, R_RELATIVE)],
            FakeResolver(hosts=_HOSTS),
            app="x", stub_base=_STUB_BASE, slot_base=_SLOT_BASE)
        blob = emit_mod.build_overlay_blob(emission, app_base=0x42000000)
        self.assertTrue(blob.startswith(emit_mod.BLOB_MAGIC))
        header_size = 28 + 12 + 12 + 8  # fixed + 1 overlay + 1 stub + 1 redirect
        self.assertEqual(len(blob), header_size + len(emission.blob))
        self.assertEqual(blob[header_size:], emission.blob)


def _pool_patch_image(slot_word):
    sections = [Section(6, ".text", 0x100, 0x100, 0x20),
                Section(7, ".got.loc", 0x200, 0x200, 0x10)]
    data = bytearray(0x300)
    data[0x204:0x208] = slot_word.to_bytes(4, "little")
    data[0x110:0x113] = relink_mod.encode_l32r(0x110, 8, 0x204)
    return Image(sections=sections, data=bytes(data))


def _pool_patch_emit(slot_word):
    func = FunctionSymbol("_Z1Av", 0x110, 3, ".text")
    return emit_mod.emit(
        _pool_patch_image(slot_word),
        Analysis(functions=[func]),
        Clustering(overlays=[OverlayPlan(overlay_id=0, functions=(func,), size=3)]),
        [Relocation(0x204, R_RELATIVE)],
        relink_mod.Resolver(app_base=None, host_symbols={}),
        app="x", stub_base=_STUB_BASE, slot_base=_SLOT_BASE)


class PoolPatches(unittest.TestCase):

    def test_deferred_mode_records_data_words_for_rebasing(self):
        emission = _pool_patch_emit(slot_word=0x2A0)
        self.assertEqual(emission.manifest.patches, (PoolPatch(0, 4),))
        self.assertEqual(int.from_bytes(emission.blob[4:8], "little"), 0x2A0)

    def test_stubbed_function_words_are_absolute_and_not_patched(self):
        emission = _pool_patch_emit(slot_word=0x110)
        self.assertEqual(emission.manifest.patches, ())
        self.assertEqual(int.from_bytes(emission.blob[4:8], "little"), _STUB_BASE)


class WriteEmission(unittest.TestCase):

    def _emission(self):
        return emit_mod.emit(
            _redirect_image(),
            Analysis(functions=[FunctionSymbol("_Z1Av", 0x110, 0x10, ".text")]),
            _redirect_clustering(), [Relocation(0x200, R_RELATIVE)],
            FakeResolver(hosts=_HOSTS),
            app="callgraph", stub_base=_STUB_BASE, slot_base=_SLOT_BASE)

    def test_writes_the_three_artifacts(self):
        emission = self._emission()
        with tempfile.TemporaryDirectory() as tmp:
            paths = emit_mod.write_emission(emission, tmp, app_base=0x42000000)
            self.assertEqual({os.path.basename(p) for p in paths.values()},
                             {"callgraph.overlay.bin", "callgraph.overlay_stubs.S",
                              "callgraph.overlay_manifest.h"})
            with open(paths["bin"], "rb") as handle:
                self.assertEqual(handle.read(),
                                 emit_mod.build_overlay_blob(emission, app_base=0x42000000))
            with open(paths["stubs"], encoding="utf-8") as handle:
                self.assertEqual(handle.read(), emission.stubs_asm)
            with open(paths["header"], encoding="utf-8") as handle:
                self.assertEqual(handle.read(), emission.header)


class _CallGraphFixture:

    def _build(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        dest = os.path.join(tmp.name, "callgraph")
        shutil.copytree(_CALLGRAPH, dest)
        self.info = app_mod.query_app(dest)
        if not self.info.readelf:
            self.skipTest("no xtensa toolchain for the app build")
        result = subprocess.run(["make", "-C", dest, "all"], env=app_mod._make_env())
        self.assertEqual(result.returncode, 0, "callgraph fixture build failed")
        if not os.path.isfile(self.info.tool):
            self.skipTest("app.mk produced no .app.tool")

        prefix = self.info.readelf[: -len("readelf")]
        self.as_bin = prefix + "as"
        self.gcc_bin = prefix + "gcc"
        self.objdump_bin = prefix + "objdump"
        with open(self.info.tool, "rb") as handle:
            data = handle.read()
        sections = overlay_mod.parse_sections(
            overlay_mod.run_tool(self.info.readelf, ["-S", "-W", self.info.tool]))
        self.image = Image(sections=sections, data=data)
        self.analysis = overlay_mod.analyze(self.info.tool, self.info.readelf)
        self.relocations = relink_mod.read_relocations(self.info.tool, self.info.readelf)
        self.clustering = overlay_mod.cluster(self.analysis, frame_size=120)
        self.emission = emit_mod.emit(
            self.image, self.analysis, self.clustering, self.relocations,
            FakeResolver(hosts=_HOSTS),
            app="callgraph", stub_base=_STUB_BASE, slot_base=_SLOT_BASE)


class CallGraphEmission(_CallGraphFixture, unittest.TestCase):

    def setUp(self):
        self._build()

    def test_manifest_counts(self):
        manifest = self.emission.manifest
        self.assertEqual(len(manifest.stubs), 8)
        self.assertEqual(len(manifest.overlays), 3)
        self.assertEqual(manifest.slots, StubSlot(_SLOT_BASE, _SLOT_BASE + 4))

    def test_pointer_slots_live_in_text_so_no_redirects(self):
        self.assertEqual(self.emission.manifest.redirects, ())

    def test_stub_addresses_and_keys(self):
        for index, stub in enumerate(self.emission.manifest.stubs):
            self.assertEqual(stub.address, _STUB_BASE + index * emit_mod.STUB_STRIDE)
            self.assertEqual(stub.key, (stub.overlay_id << 16) | stub.body_offset)

    def test_blob_covers_the_overlays(self):
        manifest = self.emission.manifest
        for overlay in manifest.overlays:
            self.assertEqual(overlay.offset % 4, 0)
        last = manifest.overlays[-1]
        self.assertEqual(last.offset + last.length, len(self.emission.blob))

    def test_bodies_keep_their_canonical_layout_within_an_overlay(self):
        by_overlay: dict[int, list] = {}
        for stub in self.emission.manifest.stubs:
            by_overlay.setdefault(stub.overlay_id, []).append(stub)
        for stubs in by_overlay.values():
            stubs.sort(key=lambda s: s.canonical)
            for earlier, later in zip(stubs, stubs[1:]):
                self.assertEqual(later.body_offset - earlier.body_offset,
                                 later.canonical - earlier.canonical)

    def test_emitted_stubs_assemble(self):
        if not os.path.isfile(self.as_bin):
            self.skipTest("no xtensa assembler")
        with tempfile.TemporaryDirectory() as tmp:
            src = os.path.join(tmp, "stubs.S")
            obj = os.path.join(tmp, "stubs.o")
            with open(src, "w", encoding="utf-8") as handle:
                handle.write(self.emission.stubs_asm)
            result = subprocess.run([self.as_bin, "-o", obj, src],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            symbols = self._symbols(obj)
            self.assertEqual(symbols["overlay_enter_slot"][0], 0)
            self.assertEqual(symbols["overlay_leave_slot"][0], 4)
            for index in range(len(self.emission.manifest.stubs)):
                address, size, _ = symbols[f"ov_stub_{index}"]
                self.assertEqual(address, index * emit_mod.STUB_STRIDE)
                self.assertEqual(size, emit_mod.STUB_CODE_SIZE)

    def test_emitted_header_compiles(self):
        if not os.path.isfile(self.gcc_bin):
            self.skipTest("no xtensa compiler")
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "manifest.h")
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(self.emission.header)
            result = subprocess.run(
                [self.gcc_bin, "-std=gnu11", "-fsyntax-only", path],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_write_emission_round_trips_to_disk(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = emit_mod.write_emission(self.emission, tmp)
            with open(paths["bin"], "rb") as handle:
                blob = handle.read()
            self.assertTrue(blob.startswith(emit_mod.BLOB_MAGIC))
            header_size = 28 + 12 * 3 + 12 * 8  # fixed + overlays + stubs
            self.assertEqual(len(blob), header_size + len(self.emission.blob))
            with open(paths["stubs"], encoding="utf-8") as handle:
                self.assertEqual(handle.read(), self.emission.stubs_asm)
            with open(paths["header"], encoding="utf-8") as handle:
                self.assertEqual(handle.read(), self.emission.header)
            self.assertEqual(os.path.basename(paths["bin"]), "callgraph.overlay.bin")

    def test_binary_and_c_manifests_agree(self):
        manifest = self.emission.manifest
        header = emit_mod.render_blob_header(manifest)
        counts = struct.unpack_from("<HHHH", header, 20)
        self.assertEqual(counts, (len(manifest.overlays), len(manifest.stubs),
                                  len(manifest.redirects), len(manifest.patches)))
        for macro, count in (("OVERLAY_COUNT", counts[0]), ("STUB_COUNT", counts[1]),
                             ("REDIRECT_COUNT", counts[2]), ("PATCH_COUNT", counts[3])):
            self.assertIn(f"#define CALLGRAPH_{macro} {count}", self.emission.header)

    def _symbols(self, obj):
        out = overlay_mod.run_tool(self.objdump_bin, ["-t", obj])
        symbols = {}
        for line in out.splitlines():
            m = _OBJDUMP_SYMBOL_RE.match(line)
            if m:
                address, _kind, _section, size, name = m.groups()
                symbols[name] = (int(address, 16), int(size, 16), _kind)
        return symbols


if __name__ == "__main__":
    unittest.main()
