#!/usr/bin/env python3
"""Check the dedicated Windows diagnostic entry and its matching game provenance."""
import argparse
from pathlib import Path
import re
import struct
import sys

ENTRY_MARK = b'OPENCNC3D|diagnostics|1'
NAMES = ('opencnc3d-game.exe', 'cnc_eyes.exe', 'OpenCNC3D-Diagnostics.exe')


def inspect(path):
    data = path.read_bytes()
    if len(data) < 64 or data[:2] != b'MZ':
        raise ValueError(path.name + ': missing DOS executable header')
    pe = struct.unpack_from('<I', data, 60)[0]
    if pe > len(data) - 96 or data[pe:pe + 4] != b'PE\0\0':
        raise ValueError(path.name + ': invalid PE header')
    machine, _, _, _, _, opt_size, flags = struct.unpack_from('<HHIIIHH', data, pe + 4)
    opt = pe + 24
    if machine != 0x14c or opt_size < 70 or opt + opt_size > len(data):
        raise ValueError(path.name + ': expected a complete i386 PE32 header')
    if struct.unpack_from('<H', data, opt)[0] != 0x10b or not (flags & 0x20):
        raise ValueError(path.name + ': expected large-address-aware PE32')
    marks = set(re.findall(rb'OPENCNC3D\|build\|[^\0\r\n]{1,200}', data))
    if len(marks) != 1:
        raise ValueError(path.name + ': missing or conflicting build marks')
    mark = next(iter(marks)).decode('ascii')
    if not re.fullmatch(r'OPENCNC3D\|build\|v[0-9]+\.[0-9]+\.[0-9]+[^|\s]*(\|cooked)?', mark):
        raise ValueError(path.name + ': invalid game build mark')
    return dict(mark=mark, diagnostic=ENTRY_MARK in data,
                subsystem=struct.unpack_from('<H', data, opt + 68)[0])


def validate(directory, cooked=False, expected_build=None):
    identities = {name: inspect(directory / name) for name in NAMES}
    if len({value['mark'] for value in identities.values()}) != 1:
        raise ValueError('game, editor and diagnostic entry have different build marks')
    mark = identities[NAMES[0]]['mark']
    if expected_build is not None and mark.split('|')[2] != expected_build:
        raise ValueError('game build mark does not match the requested package build')
    if cooked and not mark.endswith('|cooked'):
        raise ValueError('the requested cooked package contains an uncooked game build')
    if identities[NAMES[0]]['diagnostic'] or identities[NAMES[1]]['diagnostic']:
        raise ValueError('automatic diagnostic marker appears in a normal game/editor entry')
    if not identities[NAMES[2]]['diagnostic']:
        raise ValueError('OpenCNC3D-Diagnostics.exe: automatic entry marker is absent')
    if identities[NAMES[2]]['subsystem'] != 2:
        raise ValueError('OpenCNC3D-Diagnostics.exe: expected the Windows GUI subsystem')
    return mark


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary_dir', type=Path)
    parser.add_argument('--cooked', action='store_true')
    parser.add_argument('--expected-build', help='Exact requested build ID; no fixed release or SHA gate')
    args = parser.parse_args()
    try:
        mark = validate(args.binary_dir, args.cooked, args.expected_build)
    except (OSError, UnicodeError, ValueError, struct.error) as error:
        print('Windows diagnostic package refused: ' + str(error), file=sys.stderr)
        return 1
    print('diagnostics: matching ' + mark + '; dedicated PE32 GUI entry, large-address-aware')
    return 0


if __name__ == '__main__':
    sys.exit(main())
