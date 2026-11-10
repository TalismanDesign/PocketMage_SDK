import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SDK_ROOT = Path(__file__).resolve().parents[3]
GENERATOR = SDK_ROOT / "tools/gen_app_api.py"


class ApiGeneration(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "sdk"
        self.root.mkdir()
        for source in SDK_ROOT.glob("pocketmage_*/*.h"):
            target = self.root / source.relative_to(SDK_ROOT)
            target.parent.mkdir(exist_ok=True)
            shutil.copyfile(source, target)
        for source in SDK_ROOT.glob("pm_*api.h"):
            shutil.copyfile(source, self.root / source.name)
        self.host = Path(self.temp.name) / "host"
        (self.host / "src/ELF_SYMS").mkdir(parents=True)

    def generate(self, *args):
        return subprocess.run(
            [sys.executable, str(GENERATOR), "--root", str(self.root),
             "--host-root", str(self.host), *args],
            capture_output=True, text=True, check=False)

    def assert_generated(self):
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)

    def outputs(self):
        return [self.root / "pm_sdk_app.h", self.root / "symbols.list",
                self.host / "src/ELF_SYMS/pm_sdk_app.cpp",
                self.host / "src/ELF_SYMS/host_exports.list"]

    def test_regeneration_preserves_existing_exports_and_is_repeatable(self):
        self.assert_generated()
        names = (self.root / "symbols.list").read_text().splitlines()
        for name in ("pm_oled_clear", "pm_oled_init", "pm_oled_oled_line",
                     "pm_elfo_open", "pm_elfo_read", "pm_elfo_size", "pm_elfo_close",
                     "pm_sd_append_binary_file", "pm_audio_get_playback_result",
                     "delay", "pm_clock_timestamp", "pm_bz_play_jingle"):
            self.assertEqual(names.count(name), 1, name)
        before = [path.read_bytes() for path in self.outputs()]
        self.assert_generated()
        self.assertEqual(before, [path.read_bytes() for path in self.outputs()])

    def test_generated_full_stream_contract_compiles_for_c_consumer(self):
        compiler = (shutil.which(os.environ.get("CC", "cc")) or
                    shutil.which("clang") or shutil.which("gcc"))
        if compiler is None:
            self.skipTest("C compiler required for the consumer contract check")
        self.assert_generated()
        source = self.root / "consumer.c"
        source.write_text('''#include <pm_app_api.h>
static int (*open_stream)(const char *, char) = pm_sd_stream_open_mode;
static bool (*seek_stream)(int, long, int) = pm_sd_stream_seek_from;
static bool (*close_stream)(int) = pm_sd_stream_close_checked;
static size_t (*read_stream)(int, void *, size_t) = pm_sd_stream_read;
static size_t (*write_stream)(int, const void *, size_t) = pm_sd_stream_write;
int main(void) {
  return !(open_stream && seek_stream && close_stream && read_stream && write_stream);
}
''')
        result = subprocess.run(
            [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
             "-fsyntax-only", "-I", str(self.root), str(source)],
            capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_generated_enum_guard_rejects_values_before_narrowing(self):
        self.assert_generated()
        generated = (self.host / "src/ELF_SYMS/pm_sdk_app.cpp").read_text()
        wrapper = re.search(r"void pm_ui_end_eink_screen\([^\n]+\) \{.*?\n\}",
                            generated, re.S).group(0)
        source = self.root / "checked.cpp"
        source.write_text('''#include <cassert>
#include <cstdint>
enum class EinkRefresh : uint8_t { Normal, ForceFull };
int calls = 0;
void endEinkScreen(const char *, EinkRefresh) { ++calls; }
''' + wrapper + '''
int main() {
  pm_ui_end_eink_screen("title", 0);
  assert(calls == 1);
  pm_ui_end_eink_screen("title", 256);
  pm_ui_end_eink_screen("title", -1);
  assert(calls == 1);
}
''')
        binary = self.root / "checked"
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Werror",
                        str(source), "-o", str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_generated_image_output_copies_fields_and_preserves_output_on_failure(self):
        self.assert_generated()
        generated = (self.host / "src/ELF_SYMS/pm_sdk_app.cpp").read_text()
        match = re.search(r"bool pm_image_probe_region\([^\n]+\) \{.*?\n\}", generated, re.S)
        self.assertIsNotNone(match, "ImageInfo needs an explicit C output projection")
        source = self.root / "structure.cpp"
        source.write_text('''#include <pm_sdk_app.h>
#include <cassert>
enum class ImageFormat { None, JPEG, PNG };
struct ImageInfo { int width, height; ImageFormat format; };
struct Image {
  bool probe(const char *, ImageInfo &out, uint32_t offset, uint32_t) {
    out = {30, 40, ImageFormat::PNG};
    return offset == 0;
  }
} P_IMAGE;
''' + match.group(0) + '''
int main() {
  pm_image_info out{};
  assert(pm_image_probe_region("cover", &out, 0, 0));
  assert(out.width == 30 && out.height == 40 && out.format == 2);
  out.width = 99;
  assert(!pm_image_probe_region("cover", &out, 1, 0));
  assert(out.width == 99);
  assert(!pm_image_probe_region("cover", nullptr, 0, 0));
}
''')
        binary = self.root / "structure"
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Werror",
                        "-I", str(self.root), str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)

    def test_new_handwritten_declaration_is_exported_without_a_second_registry(self):
        header = self.root / "pm_app_api.h"
        with header.open("a") as handle:
            handle.write("\nint pm_test_adapter(const char *path);\n")
        self.assert_generated()
        self.assertIn("pm_test_adapter\n", (self.root / "symbols.list").read_text())

    def test_explicit_sparse_enums_preserve_values_and_following_increment(self):
        header = self.root / "pocketmage_audio/pocketmage_audio.h"
        header.write_text(header.read_text().replace("  Piezo,", "  Piezo = 0x13,"))
        self.assert_generated()
        generated = (self.root / "pm_sdk_app.h").read_text()
        self.assertIn("PM_AUDIO_OUTPUT_PIEZO = 19,", generated)
        self.assertIn("PM_AUDIO_OUTPUT_USB = 20,", generated)
        self.assertIn("PM_AUDIO_OUTPUT_AUTO = 21,", generated)

    def test_namespaced_calls_and_jingle_indices_execute_the_native_target(self):
        self.assert_generated()
        generated = (self.host / "src/ELF_SYMS/pm_sdk_app.cpp").read_text()
        wrappers = []
        for name in ("pm_activity", "pm_bz_play_jingle"):
            match = re.search(r"void " + name + r"\([^\n]+\) \{.*?\n\}", generated, re.S)
            self.assertIsNotNone(match, name)
            wrappers.append(match.group(0))
        source = self.root / "native_calls.cpp"
        source.write_text('''#include <cassert>
int activity = 0, played = 0;
namespace pocketmage { void reportActivity() { ++activity; } }
struct Jingle { int id; } startup{7}, shutdown{9};
const Jingle *const pm_jingle_table[] = {&startup, &shutdown};
constexpr int PM_JINGLE_COUNT = 2;
struct Buzzer { void playJingle(const Jingle &jingle) { played = jingle.id; } } buzzer;
Buzzer &BZ() { return buzzer; }
''' + "\n".join(wrappers) + '''
int main() {
  pm_activity();
  assert(activity == 1);
  pm_bz_play_jingle(1);
  assert(played == 9);
  pm_bz_play_jingle(-1);
  pm_bz_play_jingle(2);
  assert(played == 9);
}
''')
        binary = self.root / "native_calls"
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Werror",
                        str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)

    def test_duplicate_owner_is_rejected_before_outputs_change(self):
        self.assert_generated()
        before = [path.read_bytes() for path in self.outputs()]
        with (self.root / "pm_app_api.h").open("a") as handle:
            handle.write("\nvoid pm_audio_play(const char *path);\n")
        result = self.generate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("collision", result.stderr)
        self.assertEqual(before, [path.read_bytes() for path in self.outputs()])

    def test_verify_detects_stale_signature_and_missing_export(self):
        self.assert_generated()
        result = self.generate("--verify")
        self.assertEqual(result.returncode, 0, result.stderr)
        for path in self.outputs()[:2]:
            original = path.read_bytes()
            path.write_bytes(original.replace(b"pm_audio_play", b"pm_stale_play"))
            result = self.generate("--verify")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("stale", result.stderr)
            self.assertNotEqual(path.read_bytes(), original)
            path.write_bytes(original)

    def test_manual_adapter_signature_drift_is_rejected(self):
        self.assert_generated()
        header = self.root / "pocketmage_font/pocketmage_font.h"
        text = header.read_text().replace("static int fontHeight(",
                                          "static long fontHeight(")
        header.write_text(text)
        result = self.generate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("signature", result.stderr)

    def test_same_arity_overloads_with_different_c_types_are_rejected(self):
        header = self.root / "pocketmage_audio/pocketmage_audio.h"
        text = header.read_text().replace(
            "  void play(const char *path);",
            "  void play(int track);\n  void play(const char *path);")
        header.write_text(text)
        result = self.generate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("collision", result.stderr)

    def test_native_overloads_collapsing_to_the_same_c_type_are_rejected(self):
        header = self.root / "pocketmage_audio/pocketmage_audio.h"
        text = header.read_text().replace(
            "  void play(const char *path);",
            "  void play(const String &path);\n  void play(const char *path);")
        header.write_text(text)
        result = self.generate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("collision", result.stderr)


if __name__ == "__main__":
    unittest.main()
