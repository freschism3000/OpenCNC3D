#!/usr/bin/env python3
"""Bake original Tiberian Dawn SHP frames for the 320x200 Classic renderer.

The source archives and decoder are already part of this repository. Names are
resolved from the engine's asset tables, then looked up by their original MIX hash.
Each shape is zlib-compressed as palette indices; no recolouring or resampling occurs.
One record, TRANS, is the placement cursor's icon set rather than a SHP (placement_stamps).

Format DOS2D003: magic, u32 shape count, the eight player remap bands (8 x 16), then
TEMPERAT.PAL and DESERT.PAL as stored (768 bytes each, six bits a channel), then the
shapes. The renderer decodes every sprite through the palette of the map's theater, as
the 1995 GamePalette is. Winter's palette is byte-identical to Temperate's.
"""
import argparse
import pathlib
import re
import struct
import sys
import zlib

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(REPO / 'menu' / 'tools'))
from mixshp import MixFile, Shape, ShpError

TABLES = ('bdata', 'bbdata', 'udata', 'uadata', 'idata', 'aadata',
          'adata', 'tdata', 'odata', 'animdata', 'bullet', 'smudge', 'sdata',
          'overlay', 'terrain', 'object', 'unit', 'infantry')
EXTRA = ('WAKE', 'SMOKEY', 'DRAGON', 'RROTOR', 'LROTOR', 'SHADOW')
REMAP_NAMES = ('Gold', 'LtBlue', 'Red', 'Green', 'Orange', 'Blue', 'Grey', 'Brown')


def remap_bands():
    source = (REPO / 'brain' / 'vanilla' / 'tiberiandawn' / 'const.cpp').read_text()
    result = []
    for name in REMAP_NAMES:
        match = re.search(r'unsigned char const Remap'+name+r'\[256\]\s*=\s*\{(.*?)\};', source, re.S)
        if not match:
            raise ValueError('missing original player remap '+name)
        values = [int(x) for x in re.findall(r'\d+', re.sub(r'//[^\n]*', '', match.group(1)))]
        if len(values) != 256 or any(x < 0 or x > 255 for x in values):
            raise ValueError('invalid original player remap '+name)
        result.extend(values[176:192])
    return bytes(result)


def names_from_engine():
    source = REPO / 'brain' / 'vanilla' / 'tiberiandawn'
    names = set(EXTRA)
    for stem in TABLES:
        path = source / (stem + '.cpp')
        if path.exists():
            names.update(re.findall(r'"([A-Z0-9_-]{2,8})"', path.read_text(errors='ignore')))
    # BuildingTypeClass loads <IniName>MAKE.SHP for the construction state;
    # those names are assembled at runtime in bdata.cpp and never appear as literals.
    building_source = (source / 'bdata.cpp').read_text(errors='ignore')
    building_names = set(re.findall(r'"([A-Z0-9_-]{2,8})"', building_source))
    names.update(name + 'MAKE' for name in building_names if len(name) <= 8)
    return sorted(names)


def theater_palettes(source):
    out = b''
    for name in ('TEMPERAT', 'DESERT'):
        pal = MixFile(str(source / (name + '.MIX'))).read(name + '.PAL')
        if len(pal) != 768 or max(pal) > 63:
            raise ValueError(name + '.PAL is not a six-bit VGA palette')
        out += pal
    return out


def placement_stamps(conquer):
    """The placement cursor's hash stamps, DisplayClass::TransIconset.

    TRANS.ICN is an icon set, not a SHP: CellClass::Draw_It (cell.cpp) stamps frame 0
    (white hatching) on a pending building's clear cells and frame 2 (red) on blocked
    ones. Tiberian Dawn's IControl_Type header is i16 Width, Height, Count, Allocated;
    i32 Size, Icons, Palettes, Remaps, TransFlag, Map. All four frames are kept, as
    24x24 palette indices in the map's order, so the record reads like any SHP's.
    """
    trans = conquer.read('TRANS.ICN')
    if trans is None:
        raise ValueError('CONQUER.MIX is missing the placement cursor TRANS.ICN')
    width, height, count, _alloc, size, icons, _pal, _rem, _trans, imap = \
        struct.unpack_from('<4h6i', trans)
    if (width, height, count, size, icons) != (24, 24, 4, len(trans), 32):
        raise ValueError('TRANS.ICN has an unexpected format')
    frames = []
    for icon in trans[imap:imap + count]:
        if icon == 0xFF or icons + (icon + 1) * 576 > len(trans):
            raise ValueError('TRANS.ICN has a missing placement frame')
        frames.append(trans[icons + icon * 576:icons + (icon + 1) * 576])
    return ('TRANS', 24, 24, count, zlib.compress(b''.join(frames), 9))


def bake(out):
    source = REPO / 'data' / 'dosdata'
    mixes = [MixFile(str(source / n)) for n in ('CONQUER.MIX', 'GENERAL.MIX')]
    theater = [(prefix, ext, MixFile(str(source / (name + '.MIX'))))
               for prefix, name, ext in (('T', 'TEMPERAT', 'TEM'), ('D', 'DESERT', 'DES'))
               if (source / (name + '.MIX')).is_file()]
    records = []
    for name in names_from_engine():
        member = name + '.SHP'
        match = next((m for m in mixes if m.has(member)), None)
        if match is None:
            continue
        try:
            shp = Shape(match.read(member))
            if shp.width > 192 or shp.height > 192 or shp.frames > 1024:
                continue
            raw = b''.join(shp.frame(i) for i in range(shp.frames))
        except (ShpError, IndexError, ValueError) as exc:
            print('skip', member, exc, file=sys.stderr)
            continue
        records.append((name, shp.width, shp.height, shp.frames, zlib.compress(raw, 9)))
    records.append(placement_stamps(mixes[0]))
    # Theater-specific scenery is stored as SHP-format frames under .TEM/.DES names.
    # Keep separate T/ and D/ variants for shared type names.
    for prefix, ext, mix in theater:
        for name in names_from_engine():
            member = name + '.' + ext
            if not mix.has(member):
                continue
            try:
                shp = Shape(mix.read(member))
                if shp.width > 192 or shp.height > 192 or shp.frames > 1024:
                    continue
                raw = b''.join(shp.frame(i) for i in range(shp.frames))
            except (ShpError, IndexError, ValueError) as exc:
                print('skip', prefix, member, exc, file=sys.stderr)
                continue
            records.append((prefix+'/'+name, shp.width, shp.height, shp.frames,
                            zlib.compress(raw, 9)))
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open('wb') as f:
        f.write(b'DOS2D003')
        f.write(struct.pack('<I', len(records)))
        f.write(remap_bands())
        f.write(theater_palettes(source))
        for name, w, h, frames, blob in records:
            f.write(struct.pack('<16sHHII', name.encode(), w, h, frames, len(blob)))
            f.write(blob)
    print('DOS2D|shapes=%d|bytes=%d|path=%s' % (len(records), out.stat().st_size, out))


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('output', nargs='?', type=pathlib.Path, default=HERE / 'dosclassic.pack')
    args = ap.parse_args()
    bake(args.output)
