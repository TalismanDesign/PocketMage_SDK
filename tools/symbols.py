#!/usr/bin/env python
#
# Export generator for the PocketMage SDK.

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


def save_c_file(symbols, output, symbol_table, exclude_symbols=None,
                cpp=False, include_headers=None):
    if exclude_symbols is None:
        exclude_symbols = ['elf_find_sym']

    filtered_symbols = [name for name in symbols if name not in exclude_symbols]

    buf = '/*\n'
    buf += ' * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD\n'
    buf += ' *\n'
    buf += ' * SPDX-License-Identifier: Apache-2.0\n'
    buf += ' *\n'
    buf += f' * Generated from the curated PocketMage SDK export list.\n'
    buf += ' * DO NOT EDIT: regenerate with tools/symbols.py.\n'
    buf += ' */\n\n'

    buf += '#include <stddef.h>\n\n'
    buf += '#include "private/elf_symbol.h"\n\n'

    if cpp:
        # C++ hosts export functions/objects whose real prototypes live in
        # headers; a bare `extern int` declaration cannot name them, so the
        # caller passes the owning headers instead.
        for header in (include_headers or []):
            buf += f'#include <{header}>\n'
        if include_headers:
            buf += '\n'
    elif filtered_symbols:
        buf += '/* Extern declarations from curated export list */\n\n'
        buf += '#pragma GCC diagnostic push\n'
        buf += '#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"\n'
        for symbol_name in filtered_symbols:
            buf += f'extern int {symbol_name};\n'
        buf += '#pragma GCC diagnostic pop\n\n'

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
    args = parser.parse_args()

    curated = read_curated_list(args.list)
    if not curated:
        sys.exit(f'no symbols found in curated list {args.list}')

    readelf = args.readelf or os.environ.get('XTENSA_READELF') or 'xtensa-esp32s3-elf-readelf'
    if args.host_elf:
        host_globals = get_host_globals(readelf, args.host_elf)
        missing = [name for name in curated if name not in host_globals]
        for name in missing:
            print(f'warning: {name} is not a GLOBAL symbol in {args.host_elf}; '
                  'apps referencing it will fail to load')
        curated = [name for name in curated if name in host_globals]

    exclude = ['elf_find_sym', 'g_customer_elfsyms'] + args.exclude
    save_c_file(curated, args.output_file, args.symbol_table,
                exclude_symbols=exclude, cpp=args.cpp,
                include_headers=args.include_header)
    print(f'saved {len(curated)} exported symbols to {args.output_file}')


if __name__ == '__main__':
    main()