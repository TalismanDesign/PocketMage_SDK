
import os
import shutil
import subprocess
import tempfile
import unittest

from tools.pm import app as app_mod
from tools.pm import overlay as overlay_mod
from tools.pm import relink as relink_mod
from tools.pm.overlay import Analysis, CallSite, FunctionSymbol, Image, Section

_CALLGRAPH = os.path.join(os.path.dirname(__file__), "fixtures", "callgraph")


class FakeResolver:

    def __init__(self, function_base=0x3F000000, data_base=0x3E000000, hosts=None):
        self.function_base = function_base
        self.data_base = data_base
        self.hosts = hosts or {}

    def function(self, canonical):
        return self.function_base + canonical

    def data(self, address):
        return self.data_base + address

    def host(self, symbol):
        return self.hosts[symbol]


def _image():
    sections = [
        Section(6, ".text", 0x100, 0x100, 0x18),
        Section(7, ".got.loc", 0x200, 0x200, 0x10),
    ]
    data = bytearray(0x300)
    data[0x200:0x204] = (0x100).to_bytes(4, "little")
    data[0x204:0x208] = (0).to_bytes(4, "little")
    data[0x208:0x20C] = (0x12345678).to_bytes(4, "little")

    def body(at, literals):
        raw = bytearray((0x004136).to_bytes(3, "little"))  
        for register, literal in literals:
            raw += relink_mod.encode_l32r(at + len(raw), register, literal)
        return raw

    data[0x100:0x106] = body(0x100, [(8, 0x200)])
    data[0x106:0x10C] = body(0x106, [(8, 0x200)])
    data[0x10C:0x115] = body(0x10C, [(8, 0x204), (8, 0x208)])
    return Image(sections=sections, data=bytes(data))


def _analysis():
    return Analysis(functions=[
        FunctionSymbol("_Z1Av", 0x100, 6, ".text"),
        FunctionSymbol("_Z1Bv", 0x106, 6, ".text"),
        FunctionSymbol("_Z1Cv", 0x10C, 9, ".text"),
    ])


def _plan(analysis):
    return overlay_mod.OverlayPlan(
        overlay_id=0, functions=tuple(analysis.functions), size=21)


def _relocations():
    return [relink_mod.Relocation(0x200, relink_mod.R_RELATIVE),
            relink_mod.Relocation(0x204, relink_mod.R_JMP_SLOT, "pm_text_width")]


_RELOCATIONS = """\
Relocation section '.rela.dyn' at offset 0x134 contains 10 entries:
 Offset     Info    Type                Sym. Value  Symbol's Name + Addend
000001c4  00000005 R_XTENSA_RELATIVE                 0
000001b8  00000002 R_XTENSA_RTLD                     1
Relocation section '.rela.plt' at offset 0x1ac contains 1 entry:
 Offset     Info    Type                Sym. Value  Symbol's Name + Addend
000001c8  00000104 R_XTENSA_JMP_SLOT      00000000   pm_text_width + 0
"""


class ParseRelocations(unittest.TestCase):
    def test_keeps_offsets_types_and_jmp_slot_symbol(self):
        by_offset = {r.offset: r for r in relink_mod.parse_relocations(_RELOCATIONS)}
        self.assertEqual(by_offset[0x1C4].type, relink_mod.R_RELATIVE)
        self.assertIsNone(by_offset[0x1C4].symbol)
        self.assertEqual(by_offset[0x1C8].type, relink_mod.R_JMP_SLOT)
        self.assertEqual(by_offset[0x1C8].symbol, "pm_text_width")
        self.assertEqual(by_offset[0x1B8].type, relink_mod.R_RTLD)
        self.assertIsNone(by_offset[0x1B8].symbol)


class L32rEncoding(unittest.TestCase):

    def test_decode_matches_objdump(self):
        self.assertEqual(relink_mod.decode_l32r(0x1EB, 0xFFF381), (8, 0x1B8))
        self.assertEqual(relink_mod.decode_l32r(0x208, 0xFFF081), (8, 0x1C8))

    def test_encode_is_the_inverse(self):
        self.assertEqual(relink_mod.encode_l32r(0x1EB, 8, 0x1B8), bytes.fromhex("81f3ff"))
        self.assertEqual(relink_mod.encode_l32r(0x208, 8, 0x1C8), bytes.fromhex("81f0ff"))

    def test_out_of_range_literal_is_rejected(self):
        with self.assertRaises(overlay_mod.OverlayError):
            relink_mod.encode_l32r(0x1000, 8, 0x100000)

    def test_sites_walk_mixed_width_instructions(self):
        code = (0x004136).to_bytes(3, "little")           
        code += relink_mod.encode_l32r(0x1003, 8, 0x1010)  
        code += (0xF01D).to_bytes(2, "little")            
        self.assertEqual(relink_mod.l32r_sites(code, 0x1000),
                         [relink_mod.L32rSite(offset=3, register=8, literal=0x1010)])


class BuildBlob(unittest.TestCase):
    def setUp(self):
        self.image = _image()
        self.analysis = _analysis()
        self.resolver = FakeResolver(hosts={"pm_text_width": 0x40001234})
        self.blob = relink_mod.build_blob(
            self.image, self.analysis, _plan(self.analysis),
            _relocations(), self.resolver)

    def test_layout_is_bodies_then_aligned_pool(self):
        self.assertEqual(self.blob.literal_offset, 24)
        self.assertEqual(self.blob.literal_count, 3)
        self.assertEqual(len(self.blob.data), 24 + 3 * 4)
        self.assertEqual([p.offset for p in self.blob.placements], [0, 6, 12])

    def _pool_word(self, index):
        at = self.blob.literal_offset + 4 * index
        return int.from_bytes(self.blob.data[at:at + 4], "little")

    def test_pool_holds_resolved_values(self):
        self.assertEqual(self._pool_word(0), 0x3F000100)  
        self.assertEqual(self._pool_word(1), 0x40001234)  
        self.assertEqual(self._pool_word(2), 0x12345678)  

    def test_relative_slot_holding_non_function_is_data(self):
        data = bytearray(0x300)
        data[0x200:0x204] = (0x2A0).to_bytes(4, "little")  
        data[0x100:0x106] = (0x004136).to_bytes(3, "little") + \
            relink_mod.encode_l32r(0x103, 8, 0x200)
        image = Image(sections=[Section(6, ".text", 0x100, 0x100, 0x18),
                                Section(7, ".got.loc", 0x200, 0x200, 0x10)],
                      data=bytes(data))
        analysis = Analysis(functions=[FunctionSymbol("_Z1Av", 0x100, 6, ".text")])
        plan = overlay_mod.OverlayPlan(
            overlay_id=0, functions=analysis.functions, size=6)
        blob = relink_mod.build_blob(
            image, analysis, plan,
            [relink_mod.Relocation(0x200, relink_mod.R_RELATIVE)], self.resolver)
        at = blob.literal_offset
        self.assertEqual(int.from_bytes(blob.data[at:at + 4], "little"), 0x3E0002A0)

    def test_every_l32r_in_the_blob_reads_the_pool(self):
        pool = range(self.blob.literal_offset,
                     self.blob.literal_offset + 4 * self.blob.literal_count, 4)
        seen = 0
        for placement in self.blob.placements:
            code = self.blob.data[placement.offset:placement.offset + placement.size]
            for site in relink_mod.l32r_sites(code, placement.offset):
                self.assertIn(site.literal, pool)
                seen += 1
        self.assertEqual(seen, 4)

    def test_shared_literal_is_deduplicated(self):
        self.assertEqual(self.blob.literal_count, 3)

    def test_rtld_literal_is_rejected(self):
        relocs = [relink_mod.Relocation(0x200, relink_mod.R_RTLD),
                  relink_mod.Relocation(0x204, relink_mod.R_JMP_SLOT, "pm_text_width")]
        with self.assertRaises(overlay_mod.OverlayError):
            relink_mod.build_blob(self.image, self.analysis, _plan(self.analysis),
                                  relocs, self.resolver)

    def test_unmapped_literal_is_rejected(self):
        bad = bytearray(self.image.data)
        bad[0x103:0x106] = relink_mod.encode_l32r(0x103, 8, 0x900)
        image = Image(sections=self.image.sections, data=bytes(bad))
        with self.assertRaises(overlay_mod.OverlayError):
            relink_mod.build_blob(image, self.analysis, _plan(self.analysis),
                                  _relocations(), self.resolver)

    def test_oversized_clustering_is_rejected(self):
        clustering = overlay_mod.Clustering(oversized=[("huge",)], max_call_depth=1)
        with self.assertRaises(overlay_mod.OverlayError):
            relink_mod.build_blobs(self.image, self.analysis, clustering,
                                   _relocations(), self.resolver)


class CallGraphFixture(unittest.TestCase):

    def setUp(self):
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

    def test_every_overlay_relocates_cleanly_into_its_pool(self):
        readelf = self.info.readelf
        analysis = overlay_mod.analyze(self.info.tool, readelf)
        relocs = relink_mod.read_relocations(self.info.tool, readelf)
        with open(self.info.tool, "rb") as handle:
            data = handle.read()
        image = Image(
            sections=overlay_mod.parse_sections(
                overlay_mod.run_tool(readelf, ["-S", "-W", self.info.tool])),
            data=data)
        blobs = relink_mod.build_blobs(
            image, analysis, overlay_mod.cluster(analysis, frame_size=120), relocs,
            FakeResolver(hosts={"pm_text_width": 0x40001234}))
        self.assertEqual(len(blobs), 3)
        self.assertTrue(all(blob.data for blob in blobs))

        spaces = set()
        for blob in blobs:
            pool = range(blob.literal_offset,
                         blob.literal_offset + 4 * blob.literal_count, 4)
            self.assertGreater(blob.literal_count, 0)
            for at in pool:
                spaces.add(self._high_byte(blob, at))
            for placement in blob.placements:
                code = blob.data[placement.offset:placement.offset + placement.size]
                for site in relink_mod.l32r_sites(code, placement.offset):
                    self.assertIn(site.literal, pool)
        self.assertLessEqual({0x3E, 0x3F, 0x40}, spaces)

    @staticmethod
    def _high_byte(blob, at):
        return blob.data[at + 3]


_HOST_SYMTAB = """\
Symbol table '.symtab' contains 4 entries:
   Num:    Value  Size Type    Bind   Vis      Ndx Name
     0: 00000000     0 NOTYPE  LOCAL  DEFAULT  UND
     1: 40001234    27 FUNC    GLOBAL DEFAULT    6 pm_text_width
     2: 3f400100   128 OBJECT  GLOBAL DEFAULT   10 pm_display_buffer
     3: 00000000     0 NOTYPE  GLOBAL DEFAULT  UND printf
"""


class ParseHostSymbols(unittest.TestCase):

    def test_defined_globals_carry_their_addresses(self):
        symbols = relink_mod.parse_host_symbols(_HOST_SYMTAB)
        self.assertEqual(symbols, {"pm_text_width": 0x40001234,
                                   "pm_display_buffer": 0x3F400100})


class AppBaseResolver(unittest.TestCase):

    def test_resolved_mode_offsets_functions_and_data(self):
        resolver = relink_mod.Resolver(app_base=0x42000000, host_symbols={})
        self.assertFalse(resolver.deferred)
        self.assertEqual(resolver.function(0x110), 0x42000110)
        self.assertEqual(resolver.data(0x200), 0x42000200)

    def test_deferred_mode_passes_canonical_offsets_through(self):
        resolver = relink_mod.Resolver(app_base=None, host_symbols={})
        self.assertTrue(resolver.deferred)
        self.assertEqual(resolver.function(0x110), 0x110)
        self.assertEqual(resolver.data(0x200), 0x200)

    def test_host_symbols_resolve_from_the_table(self):
        resolver = relink_mod.Resolver(app_base=None,
                                       host_symbols={"pm_text_width": 0x40001234})
        self.assertEqual(resolver.host("pm_text_width"), 0x40001234)

    def test_unknown_host_symbol_is_rejected_with_its_name(self):
        resolver = relink_mod.Resolver(app_base=None, host_symbols={})
        with self.assertRaises(overlay_mod.OverlayError) as caught:
            resolver.host("pm_mystery")
        self.assertIn("pm_mystery", str(caught.exception))

    def test_host_call_without_a_host_elf_is_rejected(self):
        resolver = relink_mod.Resolver(app_base=0x42000000, host_symbols=None)
        with self.assertRaises(overlay_mod.OverlayError) as caught:
            resolver.host("pm_text_width")
        self.assertIn("--host-elf", str(caught.exception))


def _direct_call_image():
    sections = [Section(6, ".text", 0x100, 0x100, 0x40)]
    data = bytearray(0x200)
    # caller at 0x100: call8 0x120 (delta 0x20 -> imm18 0x8), then retw.n
    data[0x100:0x103] = (0x5 | (((0x120 - 0x100) >> 2) << 4)).to_bytes(3, "little")
    data[0x103:0x105] = (0xF01D).to_bytes(2, "little")
    data[0x120:0x122] = (0xF01D).to_bytes(2, "little")  # callee: retw.n
    return Image(sections=sections, data=bytes(data))


def _direct_call_analysis():
    return Analysis(
        functions=[FunctionSymbol("caller", 0x100, 5, ".text"),
                   FunctionSymbol("callee", 0x120, 2, ".text")],
        calls=[CallSite("caller", 0x100, "direct", target=0x120, symbol="callee")],
    )


class DirectCalls(unittest.TestCase):

    def test_cross_overlay_direct_call_is_rejected(self):
        analysis = _direct_call_analysis()
        plans = [overlay_mod.OverlayPlan(0, (analysis.functions[0],), 5),
                 overlay_mod.OverlayPlan(1, (analysis.functions[1],), 2)]
        clustering = overlay_mod.Clustering(overlays=plans)
        with self.assertRaises(overlay_mod.OverlayError) as caught:
            relink_mod.build_blobs(_direct_call_image(), analysis, clustering,
                                   [], FakeResolver())
        message = str(caught.exception)
        self.assertIn("caller", message)
        self.assertIn("callee", message)

    def test_same_overlay_direct_call_keeps_canonical_layout(self):
        analysis = _direct_call_analysis()
        plan = overlay_mod.OverlayPlan(0, tuple(analysis.functions), 7)
        blob = relink_mod.build_blob(_direct_call_image(), analysis, plan,
                                     [], FakeResolver())
        # the callee keeps its canonical delta from the caller, so the
        # PC-relative call8 displacement stays valid at any frame base
        self.assertEqual(blob.placements[0].offset, 0)
        self.assertEqual(blob.placements[1].offset, 0x20)
        self.assertEqual(blob.data[0:3], bytes.fromhex("850000"))


if __name__ == "__main__":
    unittest.main()
