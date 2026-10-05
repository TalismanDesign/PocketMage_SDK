"""Tests for the app-facing API generator.
"""

import os
import sys
import unittest

SDK_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.abspath(__file__)))))

sys.path.insert(0, os.path.join(SDK_ROOT, "tools"))

import gen_app_api as gen  # noqa: E402  path must be set first


class TypeProjection(unittest.TestCase):
    """One SDK parameter or return type in, one C type out."""

    def param(self, raw):
        return gen.project_param(raw, 0)

    def ret(self, raw):
        return gen.project_ret(raw)

    def test_string_parameter_becomes_const_char_ptr(self):
        c_type, passthrough, arg, why = self.param("const String&")
        self.assertIsNone(why)
        self.assertEqual(c_type, "const char*")
        self.assertFalse(passthrough)
        self.assertEqual(arg, "String(a0)")

    def test_bare_string_parameter_becomes_const_char_ptr(self):
        c_type, _passthrough, _arg, why = self.param("String")
        self.assertIsNone(why)
        self.assertEqual(c_type, "const char*")

    def test_bare_char_star_parameter_keeps_its_constness(self):
        const, _p, _a, _w = self.param("const char*")
        self.assertEqual(const, "const char*")
        mutable, _p, _a, _w = self.param("char*")
        self.assertEqual(mutable, "char*")

    def test_filesystem_reference_is_filled_from_global_fs(self):
        c_type, _passthrough, arg, why = self.param("fs::FS &")
        self.assertIsNone(why)
        self.assertEqual(c_type, "DROP")
        self.assertEqual(arg, "*global_fs")

    def test_enum_parameter_is_cast_at_the_call_site(self):
        c_type, passthrough, arg, why = self.param("DisplayTarget")
        self.assertIsNone(why)
        self.assertEqual(c_type, "int")
        self.assertFalse(passthrough)
        self.assertEqual(arg, "static_cast<DisplayTarget>(a0)")

    def test_scalar_reference_becomes_pointer(self):
        c_type, passthrough, arg, why = self.param("ulong&")
        self.assertIsNone(why)
        self.assertEqual(c_type, "unsigned long*")
        self.assertTrue(passthrough)
        self.assertEqual(arg, "*a0")

    def test_opaque_type_is_rejected_with_a_reason(self):
        for raw in ("DateTime", "const WifiApInfo&", "const FontTable*"):
            _c, _p, _a, why = self.param(raw)
            self.assertIn("opaque", why, raw)

    def test_scalars_pass_through_unchanged(self):
        for raw in ("int", "uint16_t", "size_t", "bool", "float"):
            c_type, passthrough, arg, why = self.param(raw)
            self.assertIsNone(why, raw)
            self.assertTrue(passthrough, raw)
            self.assertEqual(arg, "a0", raw)

    def test_string_return_is_pooled_and_borrowed_return_is_not(self):
        self.assertEqual(self.ret("String")[1:3], (True, "string"))
        self.assertEqual(self.ret("const char*")[1:3], (True, "borrowed"))

    def test_void_and_enum_returns(self):
        self.assertEqual(self.ret("void")[0], "void")
        self.assertEqual(self.ret("Mode")[0], "int")
        self.assertEqual(self.ret("Mode")[2], "enum")

    def test_opaque_and_reference_returns_are_rejected(self):
        self.assertIn("opaque return", self.ret("DateTime")[3])
        self.assertIn("reference return", self.ret("FontEntry &")[3])


class Naming(unittest.TestCase):
    def test_camel_case_becomes_snake_case(self):
        self.assertEqual(gen.snake("getWorkingFile"), "get_working_file")
        self.assertEqual(gen.snake("dispatchEvents"), "dispatch_events")
        self.assertEqual(gen.snake("loadSavedCredentials"),
                         "load_saved_credentials")

    def test_acronyms_do_not_split_into_letters(self):
        self.assertEqual(gen.snake("readFileToString"), "read_file_to_string")

    def test_vendor_prefix_is_dropped(self):
        self.assertEqual(gen.module_prefix("PocketmageEink"), "pm_eink")
        self.assertEqual(gen.module_prefix("PocketMageWifi"), "pm_wifi")
        self.assertEqual(gen.module_prefix("I18n"), "pm_i18n")


class EnumParsing(unittest.TestCase):
    """Enum values come from the headers, never a transcribed copy."""

    def test_values_are_read_from_the_header(self):
        tables = gen.load_enums(SDK_ROOT)
        names = {c_name for c_name, _host, _v, _t in tables}
        self.assertEqual(names, {"pm_target", "pm_style", "pm_lang",
                                 "pm_refresh", "pm_jingle"})

    def test_refresh_enum_comes_from_the_ui_header(self):
        modes = dict((c, v) for c, _h, values, _t in gen.load_enums(SDK_ROOT)
                     for c, v in values if c.startswith("PM_REFRESH_"))
        self.assertEqual(modes, {"PM_REFRESH_NORMAL": "Normal",
                                 "PM_REFRESH_FORCE_FULL": "ForceFull"})

    def test_lang_order_matches_the_sdk_header(self):
        langs = dict((c, v) for c, _h, values, _t in gen.load_enums(SDK_ROOT)
                     for c, v in values if c.startswith("PM_LANG_"))
        self.assertEqual(langs["PM_LANG_ENGLISH"], "English")
        self.assertEqual(langs["PM_LANG_FRENCH"], "French")
        self.assertEqual(langs["PM_LANG_SPANISH"], "Spanish")
        self.assertEqual(langs["PM_LANG_GERMAN"], "German")

    def test_commented_out_enumerators_are_ignored(self):
        text = "enum Lang : uint8_t {\n  A,\n  // B,\n  C\n};"
        self.assertEqual(gen.parse_enum_text(text, "Lang"), ["A", "C"])

    def test_explicit_values_are_parsed(self):
        text = "enum E { A = 4, B, C = 9 };"
        self.assertEqual(gen.parse_enum_text(text, "E"), ["A", "B", "C"])

    def test_missing_enum_is_an_error(self):
        with self.assertRaises(SystemExit):
            gen.parse_enum_text("enum Other { A };", "Lang")


class NamespaceConstParsing(unittest.TestCase):
    """Jingles are `const Jingle` objects, not enum values, but index the same."""

    def setUp(self):
        self.sdk_root = SDK_ROOT

    def test_jingle_order_comes_from_the_sdk_namespace(self):
        tables = gen.load_namespace_const_tables(self.sdk_root)
        self.assertEqual(len(tables), 1)
        c_name, namespace, values, type_name = tables[0]
        self.assertEqual((c_name, namespace, type_name),
                         ("pm_jingle", "Jingles", "Jingle"))
        names = [name for _const, name in values if name is not None]
        self.assertEqual(names, ["Startup", "Shutdown"])

    def test_count_sentinel_is_last_and_carries_no_object(self):
        _c, _ns, values, _t = gen.load_namespace_const_tables(self.sdk_root)[0]
        self.assertEqual(values[-1], ("PM_JINGLE_COUNT", None))
        self.assertEqual(values[-1][1], None)
        self.assertEqual(len(values) - 1, 2)

    def test_braces_inside_the_namespace_do_not_end_it(self):
        text = ('namespace Jingles { constexpr static const Note notes[] = {'
                ' {1, 2} }; const Jingle Startup = {notes, 1}; }')
        self.assertEqual(gen.parse_namespace_consts(text, "Jingles", "Jingle"),
                         ["Startup"])

    def test_missing_namespace_is_an_error(self):
        with self.assertRaises(SystemExit):
            gen.parse_namespace_consts("namespace Other { }", "Jingles",
                                       "Jingle")


class FreeFunctionParsing(unittest.TestCase):
    """File-scope declarations become wrappers; class members must not."""

    def names(self, header):
        return {e["name"] for e in gen.parse_free_functions(
            os.path.join(SDK_ROOT, header))}

    def test_io_header_functions(self):
        self.assertEqual(self.names("pocketmage_io/pocketmage_io.h"),
                         {"splitString", "joinString", "removeChar",
                          "stringToInt"})

    def test_layout_header_functions(self):
        self.assertEqual(self.names("pocketmage_layout/pocketmage_layout.h"),
                         {"sliceThatFits", "truncateWithEllipsis",
                          "wordWrap", "einkRowPitch"})

    def test_ui_header_functions(self):
        self.assertEqual(self.names("pocketmage_ui/pocketmage_ui.h"),
                         {"drawScrollbar", "beginEinkScreen", "endEinkScreen",
                          "drawListItem", "drawChipText",
                          "drawCyclePickerOLED"})

    def test_enum_definition_is_not_a_function(self):
        entries = gen.parse_free_functions(
            os.path.join(SDK_ROOT, "pocketmage_ui/pocketmage_ui.h"))
        for entry in entries:
            self.assertNotIn("EinkRefresh", entry["ret"])

    def test_default_argument_is_dropped_from_the_type(self):
        entry = next(e for e in gen.parse_free_functions(
            os.path.join(SDK_ROOT, "pocketmage_ui/pocketmage_ui.h"))
            if e["name"] == "endEinkScreen")
        self.assertEqual([p["type"] for p in entry["params"]],
                         ["const char*", "EinkRefresh"])

    def test_template_arguments_are_not_comparisons(self):
        entries = gen.parse_free_functions(
            os.path.join(SDK_ROOT, "pocketmage_io/pocketmage_io.h"))
        join = next(e for e in entries if e["name"] == "joinString")
        self.assertEqual(join["ret"], "String")


class FreeFunctionProjection(unittest.TestCase):
    def build(self, basename, module):
        header = os.path.join(
            SDK_ROOT, gen.FREE_FUNCTION_HEADERS_PATH[basename])
        return gen.build_free_module(basename, module,
                                     gen.parse_free_functions(header))

    def by_name(self, methods):
        return {m.name: m for m in methods}

    def test_plain_string_return_is_pooled_not_borrowed(self):
        methods, _ = self.build("pocketmage_io.h", "io")
        remove = self.by_name(methods)["pm_io_remove_char"]
        self.assertEqual(remove.ret, "const char*")
        self.assertIs(remove.borrowed, False)

    def test_vector_return_becomes_count_and_getter(self):
        methods, _ = self.build("pocketmage_io.h", "io")
        names = self.by_name(methods)
        self.assertIn("pm_io_split_string_count", names)
        getter = names["pm_io_split_string_get"]
        self.assertEqual([p.c_type for p in getter.params],
                         ["const char*", "char", "int", "char*", "size_t"])
        self.assertIn("v.size()", getter.statement)
        self.assertGreaterEqual(len(getter.statement.split("\n")), 4)

    def test_vector_param_becomes_array_plus_count(self):
        methods, _ = self.build("pocketmage_io.h", "io")
        join = self.by_name(methods)["pm_io_join_string"]
        self.assertEqual([p.c_type for p in join.params],
                         ["const char* const*", "int", "char"])
        self.assertIn("std::vector<String>(a0_items", join.call)

    def test_enum_params_are_cast(self):
        methods, _ = self.build("pocketmage_ui.h", "ui")
        end = self.by_name(methods)["pm_ui_end_eink_screen"]
        self.assertIn("static_cast<EinkRefresh>(a1)", end.call)

    def test_opaque_type_is_skipped_with_a_reason(self):
        methods, skipped = self.build("pocketmage_ui.h", "ui")
        self.assertNotIn("pm_ui_draw_cycle_picker", [m.name for m in methods])
        reasons = {s.method: s.reason for s in skipped}
        self.assertEqual(reasons["drawCyclePickerOLED"], "opaque type U8G2")


class CallConstruction(unittest.TestCase):
    def test_singleton_accessor_uses_a_dot(self):
        self.assertEqual(
            gen.build_call("PocketmageEink", "refresh", []),
            "EINK().refresh()")

    def test_global_reference_accessor_uses_a_dot(self):
        self.assertEqual(
            gen.build_call("PocketMageWifi", "getRssi", []),
            "P_WIFI.getRssi()")

    def test_static_class_uses_a_scope_operator(self):
        self.assertEqual(
            gen.build_call("I18n", "languageCount", []),
            "I18n::languageCount()")


if __name__ == "__main__":
    unittest.main()
