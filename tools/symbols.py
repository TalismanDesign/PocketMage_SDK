#!/usr/bin/env python

# SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0

import argparse
import os
import re
import subprocess
import sys


def read_curated_list(list_path):
    symbols = []
    with open(list_path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            symbols.append(line)
    return symbols


def get_host_globals(readelf, host_elf, symbol_types=None):
    cmd = [readelf, '-s', '-W', host_elf]
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if result.returncode != 0:
        sys.exit(f'readelf failed on {host_elf}: {result.stderr}')
    pattern = re.compile(
        r'^\s*\d+:\s+(\S+)\s+(\d+)\s+(FUNC|OBJECT)\s+GLOBAL\s+DEFAULT\s+(?:\d+|ABS|UND|COM|DEBUG)\s+(\S+)',
        re.MULTILINE,
    )
    globals_ = set()
    for line in result.stdout.splitlines():
        m = pattern.match(line)
        if not m:
            continue
        address, size, symbol_type, name = m.groups()
        if symbol_types and symbol_type not in symbol_types:
            continue
        globals_.add(name)
    return globals_


RUNTIME_ARCHIVES = ('libc', 'libm', 'libstdc++', 'libsupc++')
GCC_ARCHIVES = ('libgcc',)

FRAMEWORK_ARCHIVES = ('libnewlib',)

ESLSYM_ENTRY_BYTES = 8

# GCC treats these as language builtins: taking their address directly is a
# hard error, so they are declared under a private identifier with an asm label.
BUILTIN_PREFIXES = ('__atomic', '__sync', '__builtin')


def is_gcc_builtin(name):
    """Whether the compiler reserves the name as a builtin function.

    Args:
        name: Symbol name.

    Returns:
        True when the name must be exported through an asm-labelled alias.
    """
    return name.startswith(BUILTIN_PREFIXES)


def read_archive_symbols(nm, archive):
    """Text symbols a toolchain archive defines, from `nm -g --defined-only`.

    Args:
        nm: Path to the target `nm`.
        archive: Path to the `.a` to inspect.

    Returns:
        Sorted list of symbol names, or empty if the archive is unreadable.
    """
    if not os.path.isfile(archive):
        print(f'warning: archive not found, skipping: {archive}', file=sys.stderr)
        return []
    cmd = [nm, '--defined-only', archive]
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if result.returncode != 0:
        print(f'warning: nm failed on {archive}: {result.stderr}', file=sys.stderr)
        return []
    names = set()
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[1] in ('T', 'W', 'i'):
            names.add(fields[2])
    return sorted(names)


def find_gcc_archive(lib_dir, name):
    """GCC runtime archive: `<toolchain>/lib/gcc` on older toolchains,
    `<toolchain>/picolibc/lib/gcc` on newer ones.

    Args:
        lib_dir: The toolchain `lib/no-rtti` directory.
        name: Archive base name, e.g. `libgcc`.

    Returns:
        Path to the archive, or None when it cannot be found.
    """
    toolchain_root = os.path.normpath(
        os.path.join(lib_dir, os.pardir, os.pardir))
    roots = [os.path.join(toolchain_root, prefix, 'lib', 'gcc')
             for prefix in ('lib', 'picolibc')]
    matches = []
    for root in roots:
        if not os.path.isdir(root):
            continue
        for dirpath, _dirnames, filenames in os.walk(root):
            if f'{name}.a' in filenames:
                matches.append(os.path.join(dirpath, f'{name}.a'))
    if not matches:
        return None
    matches.sort(key=lambda p: ('no-rtti' not in p, 'psram' not in p, p))
    return matches[0]


def toolchain_lib_dir(readelf, explicit=None):
    """Runtime archive directory, defaulting to `<toolchain>/lib/no-rtti`
    derived from the readelf path.

    Args:
        readelf: Path to the target `readelf`.
        explicit: User-supplied override.

    Returns:
        Path to the archive directory, or None when it cannot be located.
    """
    if explicit:
        return explicit
    bin_dir = os.path.dirname(os.path.abspath(readelf))
    triple = os.path.basename(readelf).replace('-readelf', '')
    candidates = [
        os.path.normpath(os.path.join(bin_dir, os.pardir, triple, 'lib', 'no-rtti')),
        os.path.normpath(os.path.join(bin_dir, os.pardir, 'lib', 'no-rtti')),
    ]
    for candidate in candidates:
        if os.path.isdir(candidate):
            return candidate
    return None


def framework_lib_dir(readelf, explicit=None):
    """Arduino-ESP32 framework archive directory for the readelf's target.

    Args:
        readelf: Path to the target `readelf`.
        explicit: User-supplied override.

    Returns:
        Path to the directory holding `libnewlib.a`, or None when absent.
    """
    if explicit:
        return explicit if os.path.isdir(explicit) else None
    triple = os.path.basename(readelf).replace('-readelf', '')
    parts = triple.split('-')
    target = parts[1] if len(parts) > 2 else triple
    home = os.path.expanduser('~')
    candidates = [
        os.path.join(home, '.platformio', 'packages',
                     'framework-arduinoespressif32-libs', target, 'lib'),
    ]
    for candidate in candidates:
        if os.path.isdir(candidate):
            return candidate
    return None


def classify_runtime_symbols(nm, lib_dir, host_globals, framework_dir=None):
    """Symbols an app can link, grouped by supplying archive.

    Args:
        nm: Path to the target `nm`.
        lib_dir: Directory holding the runtime archives.
        host_globals: Symbols the firmware already exports as GLOBAL FUNC/OBJECT.
        framework_dir: Directory holding the Arduino-ESP32 archives, or None.

    Returns:
        Dict of archive name to sorted list of exportable symbols.
    """
    result = {}
    for name in RUNTIME_ARCHIVES:
        archive = os.path.join(lib_dir, f'{name}.a')
        available = set(read_archive_symbols(nm, archive))
        result[name] = sorted(available & host_globals)
    for name in GCC_ARCHIVES:
        archive = find_gcc_archive(lib_dir, name)
        if archive is None:
            print(f'warning: {name}.a not found next to the toolchain; skipping',
                  file=sys.stderr)
            continue
        available = set(read_archive_symbols(nm, archive))
        result[name] = sorted(available & host_globals)
    for name in FRAMEWORK_ARCHIVES:
        if framework_dir is None:
            print(f'warning: {name}.a needs --framework-lib-dir; skipping',
                  file=sys.stderr)
            continue
        archive = os.path.join(framework_dir, f'{name}.a')
        available = set(read_archive_symbols(nm, archive))
        result[name] = sorted(available & host_globals)
    return result


def table_cost_bytes(symbols):
    """Flash a generated export table adds: entry bytes plus the name strings.

    Args:
        symbols: Names in the table.

    Returns:
        Entry bytes plus the total length of the NUL-terminated names.
    """
    entries = len(symbols) * ESLSYM_ENTRY_BYTES
    names = sum(len(name) + 1 for name in symbols)
    return entries + names


def save_c_file(symbols, output, symbol_table, exclude_symbols=None,
                cpp=False, include_headers=None, runtime_symbols=None,
                extra_externs=None):
    if exclude_symbols is None:
        exclude_symbols = ['elf_find_sym']
    if runtime_symbols is None:
        runtime_symbols = frozenset()
    if extra_externs is None:
        extra_externs = frozenset()

    filtered_symbols = [name for name in symbols if name not in exclude_symbols]

    buf = '/*\n'
    buf += ' * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD\n'
    buf += ' *\n'
    buf += ' * SPDX-License-Identifier: Apache-2.0\n'
    buf += ' *\n'
    buf += ' * Generated from the curated PocketMage SDK export list.\n'
    buf += ' * DO NOT EDIT: regenerate with tools/symbols.py.\n'
    buf += ' */\n\n'

    buf += '#include <stddef.h>\n\n'
    buf += '#include "private/elf_symbol.h"\n\n'

    if cpp:
        for header in (include_headers or []):
            buf += f'#include <{header}>\n'
        if include_headers:
            buf += '\n'
        runtime = [name for name in filtered_symbols
                   if name in runtime_symbols or name in extra_externs]
        builtins = [name for name in runtime if is_gcc_builtin(name)]
        plain_runtime = [name for name in runtime if not is_gcc_builtin(name)]
        if plain_runtime:
            buf += '/* Runtime symbols: unmangled prototypes so the emitted reference is\n'
            buf += ' * the literal archive symbol, whatever its real C++ type. The\n'
            buf += ' * builtin mismatch warning is expected and harmless, since only the\n'
            buf += ' * address is taken and never called through this type. */\n'
            buf += '#pragma GCC diagnostic push\n'
            buf += '#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"\n'
            buf += 'extern "C" {\n'
            for symbol_name in plain_runtime:
                buf += f'int {symbol_name}();\n'
            buf += '}\n'
            buf += '#pragma GCC diagnostic pop\n\n'
        if builtins:
            buf += '/* Builtin-named symbols: a private identifier keeps the address\n'
            buf += ' * takeable while the asm label emits the real symbol name. */\n'
            buf += 'extern "C" {\n'
            for symbol_name in builtins:
                buf += f'int pm_elfsym_{symbol_name}() asm("{symbol_name}");\n'
            buf += '}\n\n'
    elif filtered_symbols:
        buf += '/* Extern declarations from curated export list */\n\n'
        buf += '#pragma GCC diagnostic push\n'
        buf += '#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"\n'
        for symbol_name in filtered_symbols:
            if is_gcc_builtin(symbol_name):
                continue
            buf += f'extern int {symbol_name};\n'
        buf += '#pragma GCC diagnostic pop\n\n'
        builtins = [name for name in filtered_symbols if is_gcc_builtin(name)]
        if builtins:
            for symbol_name in builtins:
                buf += f'int pm_elfsym_{symbol_name}() __asm__("{symbol_name}");\n'
            buf += '\n'

    symbol_table_var = f'g_{symbol_table}_elfsyms'
    buf += f'/* Available ELF symbols table: {symbol_table_var} */\n'
    if cpp:
        buf += '/* C linkage: the loader core is C and references this unmangled. */\n'
        buf += '/* Explicit extern: namespace-scope const defaults to internal\n'
        buf += ' * linkage in C++, which would let the compiler discard the table. */\n'
        buf += '\nextern "C" {\n'
        buf += f'\nextern const struct esp_elfsym {symbol_table_var}[] = {{\n'
    else:
        buf += f'\nconst struct esp_elfsym {symbol_table_var}[] = {{\n'

    for symbol_name in filtered_symbols:
        if is_gcc_builtin(symbol_name):
            buf += f'    {{ "{symbol_name}", (void*)&pm_elfsym_{symbol_name} }},\n'
        else:
            buf += f'    ESP_ELFSYM_EXPORT({symbol_name}),\n'

    buf += '    ESP_ELFSYM_END\n'
    buf += '};\n'
    if cpp:
        buf += '}\n'

    with open(output, 'w+') as f:
        f.write(buf)


def main():
    parser = argparse.ArgumentParser(
        description='Generate the curated PocketMage SDK host-export table',
        prog='symbols',
    )
    parser.add_argument(
        '--list',
        required=True,
        help='Path to the curated symbol list (one symbol per line, # comments allowed)',
    )
    parser.add_argument(
        '--host-elf',
        default=None,
        help='Host firmware ELF to validate the curated list against. Symbols missing '
             'from the GLOBAL view of this ELF are reported as warnings so the host '
             'build is checked to export every app-facing symbol.',
    )
    parser.add_argument(
        '--readelf',
        default=None,
        help='Path to the toolchain readelf; defaults to xtensa-esp32s3-elf-readelf on PATH',
    )
    parser.add_argument(
        '--symbol-table',
        default='customer',
        help='Symbol table name suffix; default "customer" produces g_customer_elfsyms',
    )
    parser.add_argument(
        '--output-file',
        required=True,
        help='Output path for the generated table',
    )
    parser.add_argument(
        '--cpp',
        action='store_true',
        help='Emit C++ (for hosts exporting C++ symbols): includes from '
             '--include-header replace the `extern int` declarations and the '
             'table gets C linkage',
    )
    parser.add_argument(
        '--include-header',
        action='append',
        default=[],
        help='Header owning exported symbols (repeatable, --cpp only)',
    )
    parser.add_argument(
        '--exclude',
        nargs='+',
        default=[],
        help='Additional symbols to exclude from the generated table',
    )
    parser.add_argument(
        '--extra-extern',
        action='append',
        default=[],
        help='Curated symbol to declare as an unmangled extern "C" prototype '
             'instead of taking it from an --include-header. Repeatable. '
             'Use for symbols whose declaring header pulls in a toolchain we '
             'do not want in this translation unit.',
    )
    parser.add_argument(
        '--no-runtime',
        dest='runtime',
        action='store_false',
        help='Export only the curated SDK list, omitting the C and C++ runtime. '
             'Apps then cannot link third-party libraries that pull in libc, '
             'soft-float helpers, or the C++ runtime.',
    )
    parser.set_defaults(runtime=True)
    parser.add_argument(
        '--nm',
        default=None,
        help='Path to the toolchain nm for reading the runtime archives; '
             'defaults to xtensa-esp32s3-elf-nm on PATH',
    )
    parser.add_argument(
        '--toolchain-lib-dir',
        default=None,
        help='Directory holding libc.a, libgcc.a and libstdc++.a; defaults to '
             'lib/no-rtti relative to the readelf path',
    )
    parser.add_argument(
        '--framework-lib-dir',
        default=None,
        help='Directory holding libnewlib.a; defaults to the '
             'Arduino-ESP32 framework libs for the readelf target',
    )
    parser.add_argument(
        '--print-cost',
        action='store_true',
        help='Print the generated table size per category and exit summary',
    )
    args = parser.parse_args()

    curated = read_curated_list(args.list)
    if not curated:
        sys.exit(f'no symbols found in curated list {args.list}')

    readelf = args.readelf or os.environ.get('XTENSA_READELF') or 'xtensa-esp32s3-elf-readelf'
    runtime_categories = {}
    if args.runtime:
        if not args.host_elf:
            sys.exit('--runtime requires --host-elf so the export list only '
                     'covers symbols the firmware actually links')
        nm = args.nm or os.environ.get('XTENSA_NM') or 'xtensa-esp32s3-elf-nm'
        lib_dir = toolchain_lib_dir(readelf, args.toolchain_lib_dir)
        if not lib_dir:
            sys.exit('could not locate the toolchain lib directory; pass '
                     '--toolchain-lib-dir')
        host_globals = get_host_globals(readelf, args.host_elf)
        framework_dir = framework_lib_dir(readelf, args.framework_lib_dir)
        runtime_categories = classify_runtime_symbols(nm, lib_dir, host_globals,
                                                      framework_dir)
        runtime_symbols = set()
        for category, names in runtime_categories.items():
            runtime_symbols.update(names)
        curated = sorted(set(curated) | runtime_symbols)

    if args.host_elf:
        host_globals = get_host_globals(readelf, args.host_elf)
        missing = [name for name in curated if name not in host_globals]
        for name in missing:
            print(f'warning: {name} is not a GLOBAL symbol in {args.host_elf}; '
                  'rebuild the host if it is a new export')

    exclude = ['elf_find_sym', 'g_customer_elfsyms'] + args.exclude
    runtime_union = set()
    for names in runtime_categories.values():
        runtime_union.update(names)
    save_c_file(curated, args.output_file, args.symbol_table,
                exclude_symbols=exclude, cpp=args.cpp,
                include_headers=args.include_header,
                runtime_symbols=runtime_union,
                extra_externs=set(args.extra_extern))
    print(f'saved {len(curated)} exported symbols to {args.output_file}')

    if args.print_cost:
        for category, names in runtime_categories.items():
            if names:
                print(f'  {category:<10} {len(names):>4} symbols '
                      f'{table_cost_bytes(names):>7,} bytes')
        print(f'  {"total":<10} {len(curated):>4} symbols '
              f'{table_cost_bytes(curated):>7,} bytes '
              f'({table_cost_bytes(curated) / 1024:.1f} KB)')


if __name__ == '__main__':
    main()
