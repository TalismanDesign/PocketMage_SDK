
import contextlib
import io
import os
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

from tools.pm import app as app_mod
from tools.pm import cli as cli_mod
from tools.pm import emit as emit_mod
from tools.pm import overlay as overlay_mod
from tools.pm import relink as relink_mod

_TEMPLATE = os.path.join(os.path.dirname(__file__), "fixtures", "template")
_CALLGRAPH = os.path.join(os.path.dirname(__file__), "fixtures", "callgraph")

READELF_S = """\
Symbol table '.symtab' contains 21 entries:
   Num:    Value  Size Type    Bind   Vis      Ndx Name
    16: 000007fc    45 FUNC    LOCAL  DEFAULT    6 _ZL8center_xiPKci
    20: 0000082c   679 FUNC    LOCAL  DEFAULT    6 app_main
    21: 00000000     0 NOTYPE  GLOBAL DEFAULT  UND printf
"""

READELF_SECTIONS = """\
There are 19 section headers, starting at offset 0x2d74:

Section Headers:
  [Nr] Name              Type            Addr     Off    Size   ES Flg Lk Inf Al
  [ 1] .symtab           SYMTAB          00000000 0025d4 000354 10      2  19  4
  [ 6] .text             PROGBITS        000005c0 0005c0 000513 00   A  0   0  4
  [ 7] .rodata           PROGBITS        00000ad4 000ad4 00001c 00   A  0   0  4
"""

DISASM = """\
000007fc <_ZL8center_xiPKci>:
 7fc:	008136        	entry	a1, 64
 800:	0208c6        	l32r	a8, 618 <pm_text_width> (6bc <_GLOBAL_OFFSET_TABLE_+0xfc>)
 803:	0008e0        	callx8	a8
 806:	0f89c6        	l32r	a9, 678 <_GLOBAL_OFFSET_TABLE_+0xb8> (7fc <_ZL8center_xiPKci>)
 809:	0009e0        	callx8	a9
 80c:	0f89c6        	l32r	a9, 678 <_GLOBAL_OFFSET_TABLE_+0xb8> (7fc <_ZL8center_xiPKci>)
 80f:	0209c6        	movi.n	a9, 0
 812:	0009e0        	callx8	a9
 815:	0008e0        	callx8	a8
 818:	0007e0        	call8	7fc <_ZL8center_xiPKci>
"""


def _text_image():
    sections = [overlay_mod.Section(index=6, name=".text", address=0x5C0,
                                    offset=0, size=0x200)]
    data = bytearray(0x200)
    slot = 0x678 - 0x5C0
    data[slot:slot + 4] = (0x7FC).to_bytes(4, "little")
    return overlay_mod.Image(sections=sections, data=bytes(data))


class ParseSections(unittest.TestCase):
    def test_addresses_and_offsets(self):
        sections = {s.name: s for s in overlay_mod.parse_sections(READELF_SECTIONS)}
        text = sections[".text"]
        self.assertEqual(text.index, 6)
        self.assertEqual(text.address, 0x5C0)
        self.assertEqual(text.offset, 0x5C0)
        self.assertEqual(text.size, 0x513)


class ParseFunctions(unittest.TestCase):
    def test_defined_funcs_only_with_section_names(self):
        sections = overlay_mod.parse_sections(READELF_SECTIONS)
        funcs = overlay_mod.parse_functions(READELF_S, sections)
        self.assertEqual([f.name for f in funcs], ["_ZL8center_xiPKci", "app_main"])
        self.assertEqual(funcs[1].address, 0x82C)
        self.assertEqual(funcs[1].size, 679)
        self.assertEqual(funcs[1].section, ".text")


class ParseCalls(unittest.TestCase):
    def setUp(self):
        self.calls = overlay_mod.parse_calls(DISASM, _text_image())

    def test_external_indirect_keeps_symbol(self):
        call = self.calls[0]
        self.assertEqual(call.address, 0x803)
        self.assertEqual(call.symbol, "pm_text_width")
        self.assertIsNone(call.target)

    def test_internal_indirect_resolves_slot_word(self):
        call = self.calls[1]
        self.assertEqual(call.address, 0x809)
        self.assertEqual(call.target, 0x7FC)
        self.assertIsNone(call.symbol)

    def test_clobbered_register_is_unresolved(self):
        call = self.calls[2]
        self.assertEqual(call.address, 0x812)
        self.assertIsNone(call.target)
        self.assertIsNone(call.symbol)

    def test_untouched_register_survives_a_clobber_of_another(self):
        self.assertEqual(self.calls[3].symbol, "pm_text_width")

    def test_direct_call_carries_target_and_symbol(self):
        call = self.calls[4]
        self.assertEqual(call.via, "direct")
        self.assertEqual(call.target, 0x7FC)
        self.assertEqual(call.symbol, "_ZL8center_xiPKci")

    def test_all_attributed_to_the_enclosing_function(self):
        self.assertEqual({c.func for c in self.calls}, {"_ZL8center_xiPKci"})


class Edges(unittest.TestCase):
    def test_pairs_internal_calls_only(self):
        sections = overlay_mod.parse_sections(READELF_SECTIONS)
        analysis = overlay_mod.Analysis(
            functions=overlay_mod.parse_functions(READELF_S, sections),
            calls=overlay_mod.parse_calls(DISASM, _text_image()),
        )
        self.assertEqual(analysis.edges(), [("_ZL8center_xiPKci", "_ZL8center_xiPKci"),
                                            ("_ZL8center_xiPKci", "_ZL8center_xiPKci")])


class ToolArtifact(unittest.TestCase):

    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        dest = os.path.join(tmp.name, "app")
        shutil.copytree(_TEMPLATE, dest)
        self.info = app_mod.query_app(dest)
        if not self.info.readelf:
            self.skipTest("no xtensa toolchain for the app build")
        result = subprocess.run(["make", "-C", dest, "all"], env=app_mod._make_env())
        self.assertEqual(result.returncode, 0, "fixture app build failed")
        if not os.path.isfile(self.info.tool):
            self.skipTest("app.mk produced no .app.tool")

    def test_analyze_finds_app_main_in_text(self):
        analysis = overlay_mod.analyze(self.info.tool, self.info.readelf)
        names = {f.name: f for f in analysis.functions}
        self.assertIn("app_main", names)
        self.assertEqual(names["app_main"].section, ".text")
        self.assertGreater(names["app_main"].size, 0)


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
        self.analysis = overlay_mod.analyze(self.info.tool, self.info.readelf)

    def test_recovers_intra_app_edges(self):
        edges = set(self.analysis.edges())
        for edge in [("_ZL4pingi", "_ZL4pongi"), ("_ZL4pongi", "_ZL4pingi"),
                     ("_ZL6chain1i", "_ZL6chain2i"), ("_ZL6chain2i", "_ZL6chain3i"),
                     ("_ZL3hubi", "_ZL6leaf_ai"), ("_ZL3hubi", "_ZL6chain1i")]:
            self.assertIn(edge, edges)

    def test_host_call_keeps_symbol_not_target(self):
        host = [c for c in self.analysis.calls if c.symbol == "pm_text_width"]
        self.assertEqual(len(host), 1)
        self.assertIsNone(host[0].target)
        self.assertNotIn("pm_text_width",
                         {callee for _, callee in self.analysis.edges()})

    def test_mutual_recursion_is_one_overlay(self):
        plan = overlay_mod.cluster(self.analysis)
        self.assertEqual(plan.overlay_of("_ZL4pingi"), plan.overlay_of("_ZL4pongi"))


class OverlayCommand(unittest.TestCase):
    def _run(self, app_info, analysis=None, frame_size=overlay_mod.DEFAULT_FRAME_SIZE,
             frame_count=3):
        args = type("Args", (), {"app_dir": ".", "frame_size": frame_size,
                                 "frame_count": frame_count, "emit": False})()
        ui = cli_mod.UI(ci_mode=True)
        buffer = io.StringIO()
        with mock.patch.object(cli_mod.app_mod, "query_app", return_value=app_info), \
             mock.patch.object(cli_mod.app_mod, "require_toolchain"), \
             mock.patch.object(cli_mod.overlay_mod, "analyze", return_value=analysis), \
             contextlib.redirect_stdout(buffer):
            code = cli_mod.cmd_overlay(ui, args)
        return code, buffer.getvalue()

    def test_missing_artifact_asks_for_a_build(self):
        info = app_mod.AppInfo(path=".", name="app", out="x", tar="x", icon="x",
                               sdk_root=".", readelf="readelf", tool="")
        code, out = self._run(info)
        self.assertEqual(code, 1)
        self.assertIn("pm build", out)

    def test_reports_functions_and_edges(self):
        with tempfile.NamedTemporaryFile() as handle:
            info = app_mod.AppInfo(path=".", name="app", out="x", tar="x", icon="x",
                                   sdk_root=".", readelf="readelf", tool=handle.name)
            analysis = overlay_mod.Analysis(
                functions=[overlay_mod.FunctionSymbol("foo", 0x10, 4, ".text"),
                           overlay_mod.FunctionSymbol("bar", 0x14, 4, ".text")],
                calls=[overlay_mod.CallSite("foo", 0x12, "indirect", target=0x14)],
            )
            code, out = self._run(info, analysis)
        self.assertEqual(code, 0)
        self.assertIn("foo", out)
        self.assertIn("-> bar", out)

    def test_oversized_unit_fails_the_build(self):
        with tempfile.NamedTemporaryFile() as handle:
            info = app_mod.AppInfo(path=".", name="app", out="x", tar="x", icon="x",
                                   sdk_root=".", readelf="readelf", tool=handle.name)
            analysis = overlay_mod.Analysis(
                functions=[overlay_mod.FunctionSymbol("huge", 0x10, 999, ".text")],
            )
            code, out = self._run(info, analysis, frame_size=256)
        self.assertEqual(code, 1)
        self.assertIn("huge", out)


class OverlayArgParsing(unittest.TestCase):

    def test_emit_is_a_flag_with_no_address_arguments(self):
        args = cli_mod._build_parser().parse_args(["overlay", "--emit"])
        self.assertTrue(args.emit)

    def test_emit_defaults_off(self):
        args = cli_mod._build_parser().parse_args(["overlay"])
        self.assertFalse(args.emit)

    def test_address_flags_are_gone(self):
        for flag in ("--stub-base", "--slot-base", "--app-base", "--host-elf"):
            with self.assertRaises(SystemExit):
                cli_mod._build_parser().parse_args(["overlay", flag, "0x1000"])


class OverlayEmitCommand(unittest.TestCase):

    def _info(self, directory):
        tool = os.path.join(directory, "app.app.tool")
        with open(tool, "wb") as handle:
            handle.write(b"\x7fELF fake tool artifact")
        return app_mod.AppInfo(path=directory, name="app", out="x", tar="x",
                               icon="x", sdk_root=".", readelf="readelf", tool=tool)

    def _run_emit(self, info, analysis, emit_side_effect=None):
        emission = emit_mod.Emission(
            manifest=emit_mod.Manifest(app=info.name, stub_block_size=8),
            blob=b"", stubs_asm="", header="")
        written = {key: os.path.join(info.path, "build", f"app.overlay{suffix}")
                   for key, suffix in (("bin", ".bin"), ("stubs", "_stubs.S"),
                                       ("header", "_manifest.h"))}
        args = type("Args", (), {"app_dir": info.path,
                                 "frame_size": overlay_mod.DEFAULT_FRAME_SIZE,
                                 "frame_count": 3, "emit": True})()
        emit_kwargs = ({"side_effect": emit_side_effect} if emit_side_effect
                       else {"return_value": emission})
        buffer = io.StringIO()
        with mock.patch.object(cli_mod.app_mod, "query_app", return_value=info), \
             mock.patch.object(cli_mod.app_mod, "require_toolchain"), \
             mock.patch.object(cli_mod.overlay_mod, "analyze", return_value=analysis), \
             mock.patch.object(cli_mod.overlay_mod, "run_tool", return_value=""), \
             mock.patch.object(cli_mod.relink_mod, "read_relocations", return_value=[]), \
             mock.patch.object(cli_mod.emit_mod, "emit", **emit_kwargs) as emit, \
             mock.patch.object(cli_mod.emit_mod, "assemble_stub_block",
                               return_value=b"\x00" * 8) as assemble, \
             mock.patch.object(cli_mod.emit_mod, "write_emission",
                               return_value=written) as write, \
             contextlib.redirect_stdout(buffer):
            code = cli_mod.cmd_overlay(cli_mod.UI(ci_mode=True), args)
        return code, buffer.getvalue(), emit, assemble, write

    def _analysis(self):
        return overlay_mod.Analysis(
            functions=[overlay_mod.FunctionSymbol("f", 0x10, 4, ".text")])

    def test_emit_assembles_the_stub_block_and_writes_artifacts(self):
        with tempfile.TemporaryDirectory() as tmp:
            info = self._info(tmp)
            code, out, emit, assemble, write = self._run_emit(info, self._analysis())
            self.assertEqual(code, 0)
            _, kwargs = emit.call_args
            self.assertEqual(kwargs, {"app": "app"})
            assemble.assert_called_once()
            write.assert_called_once()
            self.assertEqual(write.call_args.args[2], b"\x00" * 8)
            self.assertIn("app.overlay.bin", out)

    def test_emit_errors_are_reported_not_raised(self):
        with tempfile.TemporaryDirectory() as tmp:
            info = self._info(tmp)
            code, out, _, _, write = self._run_emit(
                info, self._analysis(),
                emit_side_effect=overlay_mod.OverlayError("no frames fit"))
            self.assertEqual(code, 1)
            self.assertIn("no frames fit", out)
            write.assert_not_called()

    def test_deferred_blob_is_announced(self):
        with tempfile.TemporaryDirectory() as tmp:
            info = self._info(tmp)
            code, out, _, _, _ = self._run_emit(info, self._analysis())
            self.assertEqual(code, 0)
            self.assertIn("loader resolves", out)


class Clustering(unittest.TestCase):

    def _analysis(self):
        funcs = [("main", 0x100, 100), ("a", 0x200, 50),
                 ("b", 0x300, 60), ("c", 0x400, 40)]
        calls = [
            overlay_mod.CallSite("main", 0x110, "direct", target=0x200),
            overlay_mod.CallSite("main", 0x120, "direct", target=0x400),
            overlay_mod.CallSite("a", 0x210, "direct", target=0x300),
            overlay_mod.CallSite("b", 0x310, "direct", target=0x200),
        ]
        return overlay_mod.Analysis(
            functions=[overlay_mod.FunctionSymbol(n, a, s, ".text") for n, a, s in funcs],
            calls=calls,
        )

    def test_mutual_recursion_is_one_atomic_unit(self):
        plan = overlay_mod.cluster(self._analysis(), frame_size=200)
        holder = next(p for p in plan.overlays if "a" in p.names)
        self.assertEqual(holder.names, ("a", "b"))
        self.assertLessEqual(holder.size, 200)

    def test_affinity_groups_callers_with_their_heavy_callee(self):
        plan = overlay_mod.cluster(self._analysis(), frame_size=200)
        self.assertEqual(len(plan.overlays), 2)
        self.assertEqual(plan.overlay_of("main"), plan.overlay_of("c"))
        self.assertNotEqual(plan.overlay_of("main"), plan.overlay_of("a"))

    def test_max_call_depth_is_the_longest_unit_chain(self):
        plan = overlay_mod.cluster(self._analysis(), frame_size=200)
        self.assertEqual(plan.max_call_depth, 2)

    def test_disconnected_units_share_a_fitting_overlay(self):
        analysis = overlay_mod.Analysis(functions=[
            overlay_mod.FunctionSymbol("x", 0x10, 30, ".text"),
            overlay_mod.FunctionSymbol("y", 0x40, 30, ".text"),
        ])
        plan = overlay_mod.cluster(analysis, frame_size=200)
        self.assertEqual(len(plan.overlays), 1)
        self.assertEqual(plan.overlays[0].names, ("x", "y"))

    def test_units_larger_than_a_frame_are_reported(self):
        plan = overlay_mod.cluster(self._analysis(), frame_size=50)
        self.assertIn(("a", "b"), plan.oversized)
        self.assertIn(("main",), plan.oversized)
        self.assertEqual(plan.overlay_of("c"), 0)
        self.assertIsNone(plan.overlay_of("main"))

    def test_every_placed_function_is_accounted_for_once(self):
        plan = overlay_mod.cluster(self._analysis(), frame_size=200)
        placed = [f.name for p in plan.overlays for f in p.functions]
        self.assertEqual(sorted(placed), ["a", "b", "c", "main"])


if __name__ == "__main__":
    unittest.main()
