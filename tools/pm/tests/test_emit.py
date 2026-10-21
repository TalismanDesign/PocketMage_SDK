
import os
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
    HostRef,
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

_CALLGRAPH = os.path.join(os.path.dirname(__file__), "fixtures", "callgraph")


def _stub(name, canonical, overlay_id, body_offset, index, count):
    return FunctionStub(name, canonical, overlay_id, body_offset,
                        StubLayout(count).stub_offset(index),
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

    def test_slots_are_the_first_two_words_of_the_block(self):
        self.assertEqual(StubLayout(3).slots, StubSlot(0, 4))

    def test_stubs_follow_the_key_words(self):
        layout = StubLayout(3)
        self.assertEqual(layout.stub_offset(0), 8 + 3 * 4)
        self.assertEqual(layout.stub_offset(1), 8 + 3 * 4 + emit_mod.STUB_STRIDE)
        self.assertEqual(layout.stub_offset(2), 8 + 3 * 4 + 2 * emit_mod.STUB_STRIDE)

    def test_size_covers_slots_keys_and_code(self):
        layout = StubLayout(3)
        self.assertEqual(layout.size, 8 + 3 * 4 + 3 * emit_mod.STUB_STRIDE)

    def test_zero_stubs_block_is_just_the_slots(self):
        self.assertEqual(StubLayout(0).size, 8)
        self.assertEqual(StubLayout(0).stub_offset(0), 8)


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

    def test_order_keys_and_block_offsets(self):
        layout = StubLayout(3)
        stubs = emit_mod.place_stubs(self.blobs, layout)
        self.assertEqual([s.canonical for s in stubs], [0x110, 0x120, 0x130])
        self.assertEqual([s.overlay_id for s in stubs], [0, 0, 1])
        self.assertEqual([s.body_offset for s in stubs], [0, 4, 0])
        self.assertEqual([s.key for s in stubs], [0, 4, 0x10000])
        self.assertEqual([s.offset for s in stubs],
                         [layout.stub_offset(i) for i in range(3)])


class OverlayImages(unittest.TestCase):

    def test_offsets_are_aligned_and_contiguous_from_the_region_base(self):
        blobs = [OverlayBlob(overlay_id=0, data=b"A" * 6),
                 OverlayBlob(overlay_id=1, data=b"B" * 5)]
        images = emit_mod.build_overlays(blobs, first_offset=0x40)
        self.assertEqual(images, [OverlayImage(0, 0x40, 8), OverlayImage(1, 0x48, 8)])

    def test_concat_pads_between_blobs(self):
        blobs = [OverlayBlob(overlay_id=0, data=b"A" * 6),
                 OverlayBlob(overlay_id=1, data=b"B" * 5)]
        self.assertEqual(emit_mod.concat_blobs(blobs), b"A" * 6 + b"\x00\x00" + b"B" * 5 + b"\x00" * 3)

    def test_concat_pads_the_last_blob_to_a_word(self):
        blobs = [OverlayBlob(overlay_id=0, data=b"A" * 5)]
        self.assertEqual(emit_mod.concat_blobs(blobs), b"A" * 5 + b"\x00" * 3)


class ResolveRedirects(unittest.TestCase):

    def setUp(self):
        self.image = _redirect_image()
        self.clustering = _redirect_clustering()
        self.stubs = [_stub("_Z1Av", 0x110, 0, 0, 0, 1)]

    def _resolve(self, relocs):
        return emit_mod.resolve_redirects(self.image, self.clustering, relocs, self.stubs)

    def test_resident_pointer_to_paged_function_is_redirected(self):
        relocs = [Relocation(0x200, R_RELATIVE)]
        self.assertEqual(self._resolve(relocs), [Redirect(0x200, StubLayout(1).stub_offset(0))])

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

    def test_one_block_section_holding_slots_keys_and_code(self):
        asm = emit_mod.render_stubs([_stub("_Z1Av", 0x110, 1, 0x18, 0, 1)])
        self.assertEqual(asm.count(".section"), 1)
        self.assertIn('.section .overlay.stubblock, "awx", @progbits', asm)
        self.assertIn("overlay_enter_slot:", asm)
        self.assertIn("overlay_leave_slot:", asm)
        self.assertIn(".Lkey_0:", asm)
        self.assertIn(".word 0x00010018", asm)
        self.assertLess(asm.index("overlay_enter_slot:"), asm.index(".Lkey_0:"))
        self.assertLess(asm.index(".Lkey_0:"), asm.index("ov_stub_0:"))

    def test_one_instruction_block_per_stub(self):
        asm = emit_mod.render_stubs([_stub("a", 0x110, 0, 0, 0, 2),
                                     _stub("b", 0x120, 0, 4, 1, 2)])
        instructions = [line for line in asm.splitlines()
                        if line.startswith("    ") and not line.startswith("    .")]
        self.assertEqual(len(instructions), 2 * len(emit_mod._STUB_SEQUENCE))


class RenderHeader(unittest.TestCase):

    def test_counts_and_slot_offset_defines(self):
        manifest = Manifest(app="callgraph")
        header = emit_mod.render_header(manifest)
        self.assertIn("#define CALLGRAPH_ENTER_SLOT_OFF 0x00000000u", header)
        self.assertIn("#define CALLGRAPH_LEAVE_SLOT_OFF 0x00000004u", header)
        self.assertIn("#define CALLGRAPH_OVERLAY_COUNT 0", header)
        self.assertIn("#define CALLGRAPH_STUB_COUNT 0", header)
        self.assertIn("#define CALLGRAPH_REDIRECT_COUNT 0", header)
        self.assertIn("#define CALLGRAPH_PATCH_COUNT 0", header)
        self.assertIn("#define CALLGRAPH_STUB_PATCH_COUNT 0", header)
        self.assertIn("#define CALLGRAPH_HOST_COUNT 0", header)

    def test_empty_tables_stay_compilable(self):
        header = emit_mod.render_header(Manifest(app="x"))
        self.assertIn("x_stubs[1] = { 0 }", header)
        self.assertIn("x_redirects[1] = { 0 }", header)
        self.assertIn("x_strtab[1] = { 0 }", header)

    def test_rows_carry_relative_offsets(self):
        manifest = Manifest(app="x",
                            overlays=(OverlayImage(3, 0x10, 0x20),),
                            stubs=(_stub("f", 0x110, 2, 0x18, 0, 1),),
                            host_refs=(HostRef(1, 0x2C, "pm_text_width"),))
        header = emit_mod.render_header(manifest)
        self.assertIn("{ 3, 0x00000010, 0x00000020}", header)
        self.assertIn("{ 0x00000110, 2, 0x0018, 0x0000000c}", header)
        self.assertIn('{ 1, 0x002c, 0x00000000}, /* pm_text_width */', header)
        self.assertIn('x_strtab[] = "pm_text_width\\0"', header)

    def test_ident_is_sanitized(self):
        self.assertEqual(emit_mod._app_ident("callgraph"), "callgraph")
        self.assertEqual(emit_mod._app_ident("my-app"), "my_app")
        self.assertEqual(emit_mod._app_ident("1foo"), "_1foo")
        self.assertEqual(emit_mod._app_ident(""), "_")


class RenderBlobHeader(unittest.TestCase):

    def _manifest(self):
        layout = StubLayout(1)
        return Manifest(app="callgraph",
                        stubs=(_stub("f", 0x110, 0, 0, 0, 1),),
                        overlays=(OverlayImage(0, 0, 0x20),),
                        slots=StubSlot(0, 4),
                        redirects=(Redirect(0x200, layout.stub_offset(0)),),
                        patches=(PoolPatch(0, 0x24),),
                        stub_patches=(PoolPatch(0, 0x28),),
                        host_refs=(HostRef(0, 0x2C, "pm_text_width"),),
                        stub_block_size=layout.size)

    def test_fixed_layout(self):
        header = emit_mod.render_blob_header(self._manifest())
        self.assertEqual(header[:4], emit_mod.BLOB_MAGIC)
        version, flags = struct.unpack_from("<HH", header, 4)
        self.assertEqual(version, emit_mod.BLOB_VERSION)
        self.assertEqual(flags, 0)
        self.assertEqual(struct.unpack_from("<6H", header, 8), (1, 1, 1, 1, 1, 1))
        stub_block_off, stub_block_size, enter, leave, strtab_off, strtab_size = \
            struct.unpack_from("<6I", header, 20)
        self.assertEqual((enter, leave), (0, 4))
        self.assertEqual(stub_block_size, StubLayout(1).size)
        self.assertEqual(strtab_off, 44 + 12 + 12 + 8 + 4 + 4 + 8)
        self.assertEqual(header[strtab_off:strtab_off + strtab_size], b"pm_text_width\x00")
        self.assertEqual(stub_block_off, relink_mod.align_up(strtab_off + strtab_size, 4))
        self.assertEqual(len(header), stub_block_off)
        self.assertEqual(struct.unpack_from("<HHII", header, 44), (0, 0, 0, 0x20))
        self.assertEqual(struct.unpack_from("<IHHI", header, 56),
                         (0x110, 0, 0, StubLayout(1).stub_offset(0)))
        self.assertEqual(struct.unpack_from("<II", header, 68), (0x200, StubLayout(1).stub_offset(0)))
        self.assertEqual(struct.unpack_from("<HH", header, 76), (0, 0x24))
        self.assertEqual(struct.unpack_from("<HH", header, 80), (0, 0x28))
        self.assertEqual(struct.unpack_from("<HHI", header, 84), (0, 0x2C, 0))

    def test_strtab_offsets_accumulate(self):
        manifest = Manifest(app="x",
                            host_refs=(HostRef(0, 0x10, "alpha"),
                                       HostRef(0, 0x14, "beta")))
        header = emit_mod.render_blob_header(manifest)
        _o, _b, _e, _l, strtab_off, strtab_size = struct.unpack_from("<6I", header, 20)
        self.assertEqual(header[strtab_off:strtab_off + strtab_size], b"alpha\x00beta\x00")
        self.assertEqual(struct.unpack_from("<HHI", header, 44), (0, 0x10, 0))
        self.assertEqual(struct.unpack_from("<HHI", header, 52), (0, 0x14, 6))

    def test_empty_manifest_still_has_the_fixed_header(self):
        header = emit_mod.render_blob_header(Manifest(app="x"))
        self.assertEqual(len(header), 44)
        self.assertEqual(struct.unpack_from("<6H", header, 8), (0, 0, 0, 0, 0, 0))


class BuildOverlayBlob(unittest.TestCase):

    def _emission(self):
        return emit_mod.emit(
            _redirect_image(),
            Analysis(functions=[FunctionSymbol("_Z1Av", 0x110, 0x10, ".text")]),
            _redirect_clustering(), [Relocation(0x200, R_RELATIVE)],
            app="x")

    def test_metadata_then_stub_block_then_bodies(self):
        emission = self._emission()
        stub_block = b"\xAA" * emission.manifest.stub_block_size
        blob = emit_mod.build_overlay_blob(emission, stub_block)
        self.assertTrue(blob.startswith(emit_mod.BLOB_MAGIC))
        metadata = emit_mod.render_blob_header(emission.manifest)
        self.assertEqual(blob[:len(metadata)], metadata)
        self.assertEqual(blob[len(metadata):len(metadata) + len(stub_block)], stub_block)
        first = emission.manifest.overlays[0]
        self.assertEqual(first.offset, len(metadata) + len(stub_block))
        self.assertEqual(blob[first.offset:], emission.blob)

    def test_wrong_sized_stub_block_is_rejected(self):
        emission = self._emission()
        with self.assertRaises(overlay_mod.OverlayError):
            emit_mod.build_overlay_blob(emission, b"\xAA" * 4)


def _pool_patch_image(slot_word):
    sections = [Section(6, ".text", 0x100, 0x100, 0x20),
                Section(7, ".got.loc", 0x200, 0x200, 0x10)]
    data = bytearray(0x300)
    data[0x204:0x208] = slot_word.to_bytes(4, "little")
    data[0x110:0x113] = relink_mod.encode_l32r(0x110, 8, 0x204)
    return Image(sections=sections, data=bytes(data))


def _pool_patch_emit(slot_word, reloc):
    func = FunctionSymbol("_Z1Av", 0x110, 3, ".text")
    return emit_mod.emit(
        _pool_patch_image(slot_word),
        Analysis(functions=[func]),
        Clustering(overlays=[OverlayPlan(overlay_id=0, functions=(func,), size=3)]),
        [reloc],
        app="x")


class PoolWords(unittest.TestCase):

    def test_data_words_keep_canonical_offsets_and_get_patched(self):
        emission = _pool_patch_emit(0x2A0, Relocation(0x204, R_RELATIVE))
        self.assertEqual(emission.manifest.patches, (PoolPatch(0, 4),))
        self.assertEqual(emission.manifest.stub_patches, ())
        self.assertEqual(int.from_bytes(emission.blob[4:8], "little"), 0x2A0)

    def test_paged_function_words_carry_stub_offsets(self):
        emission = _pool_patch_emit(0x110, Relocation(0x204, R_RELATIVE))
        self.assertEqual(emission.manifest.patches, ())
        self.assertEqual(emission.manifest.stub_patches, (PoolPatch(0, 4),))
        self.assertEqual(int.from_bytes(emission.blob[4:8], "little"),
                         StubLayout(1).stub_offset(0))

    def test_host_calls_carry_a_name_and_a_zero_word(self):
        emission = _pool_patch_emit(0, Relocation(0x204, R_JMP_SLOT, "pm_text_width"))
        self.assertEqual(emission.manifest.host_refs,
                         (HostRef(0, 4, "pm_text_width"),))
        self.assertEqual(int.from_bytes(emission.blob[4:8], "little"), 0)


class WriteEmission(unittest.TestCase):

    def test_writes_the_three_artifacts(self):
        emission = emit_mod.emit(
            _redirect_image(),
            Analysis(functions=[FunctionSymbol("_Z1Av", 0x110, 0x10, ".text")]),
            _redirect_clustering(), [Relocation(0x200, R_RELATIVE)],
            app="callgraph")
        stub_block = b"\xAA" * emission.manifest.stub_block_size
        with tempfile.TemporaryDirectory() as tmp:
            paths = emit_mod.write_emission(emission, tmp, stub_block)
            self.assertEqual({os.path.basename(p) for p in paths.values()},
                             {"callgraph.overlay.bin", "callgraph.overlay_stubs.S",
                              "callgraph.overlay_manifest.h"})
            with open(paths["bin"], "rb") as handle:
                self.assertEqual(handle.read(),
                                 emit_mod.build_overlay_blob(emission, stub_block))
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
            app="callgraph")


class CallGraphEmission(_CallGraphFixture, unittest.TestCase):

    def setUp(self):
        self._build()

    def test_manifest_counts(self):
        manifest = self.emission.manifest
        self.assertEqual(len(manifest.stubs), 8)
        self.assertEqual(len(manifest.overlays), 3)
        self.assertEqual(manifest.slots, StubSlot(0, 4))

    def test_pointer_slots_live_in_text_so_no_redirects(self):
        self.assertEqual(self.emission.manifest.redirects, ())

    def test_stub_offsets_follow_the_block_layout(self):
        layout = StubLayout(len(self.emission.manifest.stubs))
        self.assertEqual(self.emission.manifest.stub_block_size, layout.size)
        for index, stub in enumerate(self.emission.manifest.stubs):
            self.assertEqual(stub.offset, layout.stub_offset(index))
            self.assertEqual(stub.key, (stub.overlay_id << 16) | stub.body_offset)

    def test_deferred_tables_cover_every_base_dependent_word(self):
        manifest = self.emission.manifest
        self.assertEqual(len(manifest.patches), 1)
        self.assertGreaterEqual(len(manifest.stub_patches), 1)
        self.assertEqual({h.name for h in manifest.host_refs}, {"pm_text_width"})
        overlay_sizes = {o.overlay_id: o.length for o in manifest.overlays}
        for table in (manifest.patches, manifest.stub_patches):
            for patch in table:
                self.assertEqual(patch.blob_offset % 4, 0)
                self.assertLess(patch.blob_offset, overlay_sizes[patch.overlay_id])

    def test_bodies_keep_their_canonical_layout_within_an_overlay(self):
        by_overlay: dict[int, list] = {}
        for stub in self.emission.manifest.stubs:
            by_overlay.setdefault(stub.overlay_id, []).append(stub)
        for stubs in by_overlay.values():
            stubs.sort(key=lambda s: s.canonical)
            for earlier, later in zip(stubs, stubs[1:]):
                self.assertEqual(later.body_offset - earlier.body_offset,
                                 later.canonical - earlier.canonical)

    def test_assembled_block_matches_the_layout(self):
        block = emit_mod.assemble_stub_block(self.emission.stubs_asm,
                                             self.info.readelf)
        self.assertEqual(len(block), self.emission.manifest.stub_block_size)
        self.assertEqual(block[:8], b"\x00" * 8)  
        for index, stub in enumerate(self.emission.manifest.stubs):
            key, = struct.unpack_from("<I", block, 8 + 4 * index)
            self.assertEqual(key, stub.key)
            word = int.from_bytes(
                block[stub.offset + 3:stub.offset + 6], "little")
            register, literal = relink_mod.decode_l32r(stub.offset + 3, word)
            self.assertEqual(register, 10)
            self.assertEqual(literal, 8 + 4 * index)

    def test_write_emission_round_trips_to_disk(self):
        block = emit_mod.assemble_stub_block(self.emission.stubs_asm,
                                             self.info.readelf)
        with tempfile.TemporaryDirectory() as tmp:
            paths = emit_mod.write_emission(self.emission, tmp, block)
            with open(paths["bin"], "rb") as handle:
                blob = handle.read()
            self.assertTrue(blob.startswith(emit_mod.BLOB_MAGIC))
            last = self.emission.manifest.overlays[-1]
            self.assertEqual(len(blob), last.offset + last.length)
            with open(paths["stubs"], encoding="utf-8") as handle:
                self.assertEqual(handle.read(), self.emission.stubs_asm)
            with open(paths["header"], encoding="utf-8") as handle:
                self.assertEqual(handle.read(), self.emission.header)
            self.assertEqual(os.path.basename(paths["bin"]), "callgraph.overlay.bin")

    def test_binary_and_c_manifests_agree(self):
        manifest = self.emission.manifest
        header = emit_mod.render_blob_header(manifest)
        counts = struct.unpack_from("<6H", header, 8)
        self.assertEqual(counts, (len(manifest.overlays), len(manifest.stubs),
                                  len(manifest.redirects), len(manifest.patches),
                                  len(manifest.stub_patches), len(manifest.host_refs)))
        for macro, count in (("OVERLAY_COUNT", counts[0]), ("STUB_COUNT", counts[1]),
                             ("REDIRECT_COUNT", counts[2]), ("PATCH_COUNT", counts[3]),
                             ("STUB_PATCH_COUNT", counts[4]), ("HOST_COUNT", counts[5])):
            self.assertIn(f"#define CALLGRAPH_{macro} {count}", self.emission.header)

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


if __name__ == "__main__":
    unittest.main()
