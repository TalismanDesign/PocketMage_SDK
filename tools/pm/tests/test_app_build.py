"""Compile a real app against the generated API and check it can resolve.
"""

import os
import re
import shutil
import subprocess
import unittest

SDK_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.abspath(__file__)))))
SYMBOLS_LIST = os.path.join(SDK_ROOT, "symbols.list")

SAMPLE_CALLS = """
    pm_i18n_set_language(PM_LANG_ENGLISH);
    pm_eink_refresh();
    pm_eink_pixel(1, 2, 3);
    pm_text_width(2, "x", 2);
    pm_text(PM_TARGET_OLED, 0, 0, "x", PM_STYLE_BODY_BOLD);
    pm_kb_state();
    pm_kb_flush();
    pm_sd_get_mode();
    pm_wifi_get_rssi();
    pm_bz_play_jingle(PM_JINGLE_STARTUP);
    pm_touch_get_scroll_vector();
    pm_clock_epoch();
    pm_io_string_to_int("42", -1);
    pm_layout_slice_that_fits("abc", 3, 100, PM_STYLE_BODY);
    pm_ui_begin_eink_screen(false);
    pm_ui_end_eink_screen("done", PM_REFRESH_NORMAL);
    pm_io_split_string_count("a,b", ',');
"""

CPP_SOURCE = f"""#include <pm_app_api.h>

extern "C" int app_main() {{
{SAMPLE_CALLS}
    return 0;
}}
"""

C_SOURCE = f"""#include <pm_app_api.h>

int probe(void) {{
{SAMPLE_CALLS}
    return 0;
}}
"""


def find_compiler():
    """Target C++ compiler, or None when the toolchain is not installed."""
    override = os.environ.get('PM_APP_CXX')
    if override and os.path.exists(override):
        return override
    local = os.path.expanduser(
        '~/.platformio/packages/toolchain-xtensa-esp-elf/bin/'
        'xtensa-esp32s3-elf-g++')
    if os.path.exists(local):
        return local
    return shutil.which('xtensa-esp32s3-elf-g++')


def c_compiler_for(cxx):
    return cxx.replace('-g++', '-gcc')


def export_list():
    with open(SYMBOLS_LIST, encoding='utf-8') as handle:
        return {line.strip() for line in handle if line.strip()}


class GeneratedAppCompiles(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cxx = find_compiler()

    def setUp(self):
        if not self.cxx:
            self.skipTest('no xtensa-esp32s3-elf-g++ available')

    def compile(self, compiler, source, is_cpp, tmp):
        suffix = '.cpp' if is_cpp else '.c'
        path = os.path.join(tmp, f'sample{suffix}')
        with open(path, 'w', encoding='utf-8') as handle:
            handle.write(source)
        result = subprocess.run(
            [compiler, '-std=gnu++17' if is_cpp else '-std=c11',
             '-fsyntax-only', '-Wall', '-Wextra', '-I', SDK_ROOT, path],
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stderr

    def test_sample_compiles_as_cpp(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            stderr = self.compile(self.cxx, CPP_SOURCE, True, tmp)
            self.assertNotIn('warning:', stderr)

    def test_header_is_usable_from_c(self):
        import tempfile
        cc = c_compiler_for(self.cxx)
        with tempfile.TemporaryDirectory() as tmp:
            self.compile(cc, C_SOURCE, False, tmp)

    def test_every_called_symbol_is_exported(self):
        called = set(re.findall(r'\b(pm_[a-z_0-9]+)\s*\(', SAMPLE_CALLS))
        self.assertGreater(len(called), 8, 'sample should span the surface')
        missing = called - export_list()
        self.assertEqual(missing, set(),
                         f'called but not in symbols.list: {sorted(missing)}')

    def test_vector_getter_takes_a_caller_buffer(self):
        import tempfile
        source = ('#include <pm_app_api.h>\n'
                  'int probe(void) {\n'
                  '  char line[64];\n'
                  '  int n = pm_layout_word_wrap_count("a bb ccc", 40,'
                  ' PM_STYLE_BODY);\n'
                  '  if (n > 0) return pm_layout_word_wrap_get("a bb ccc", 40,'
                  ' PM_STYLE_BODY, 0, line, sizeof line);\n'
                  '  return 0;\n}\n')
        with tempfile.TemporaryDirectory() as tmp:
            self.compile(self.cxx, source, True, tmp)

    def test_sample_names_cover_each_module(self):
        called = set(re.findall(r'\b(pm_[a-z_0-9]+)\s*\(', SAMPLE_CALLS))
        modules = {name.split('_')[1] for name in called}
        for expected in ('i18n', 'eink', 'text', 'kb', 'sd', 'wifi', 'bz',
                         'touch', 'clock', 'io', 'layout', 'ui'):
            self.assertIn(expected, modules)


if __name__ == '__main__':
    unittest.main()
