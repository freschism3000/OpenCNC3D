#!/usr/bin/env python3
"""CNC3D -- the DOS half of the viewer's map corpus.

The cartridge ships 79 maps. the project owner's own 1995 MS-DOS CD ships more, and they are
the ones the N64 never got: the SCM multiplayer maps and the SCJ dinosaur
missions. Both live in data/dosdata/GENERAL.MIX.

  SCM01EA..SCM09EA, SCM70EA..SCM74EA, SCM77EA, SCM96EA   16 multiplayer maps
  SCJ01EA..SCJ05EA                                        5 dinosaur missions

Why the names look like that: Set_Scenario_Name in the GPL Tiberian Dawn source
builds SC + house prefix + number + dir + variant, and the prefix comes from
HouseTypeClass::Prefix. MULTI1's prefix is 'M' and HOUSE_JP's is 'J', which is
why multiplayer maps are SCM and the Funpark missions are SCJ.

FORMAT NOTES, all from the GPL source rather than guessed:

  <SCEN>.BIN   4096 records of {u8 TemplateType, u8 icon}, cell order y*64+x.
               MapClass::Read_Binary_File, map.cpp:1076. 255 = clear.
               This is NOT the cartridge's .MAP, which is 64x64 big-endian N64
               tile-bank ids.
  MIX          TD-era header: u16 count, u32 datasize, then count records of
               {u32 id, i32 offset, i32 size}, data follows. The id is the
               filename hash; mix_id() below is audio/mixfile.c:25 in python.

  Rendering a DOS map with the cartridge's art is possible because the theater
  TL4/TL8 tables map an N64 bank slot to exactly the (template, icon) pair the
  DOS engine uses, so inverting them turns a DOS cell into an N64 atlas slot.
  Measured: every (template, icon) in every DESERT and TEMPERATE map here
  resolves, zero unresolved cells. WINTER has no bank on the cartridge at all
  and is handled explicitly, never silently.

  These maps have NO heightmap and cannot have one: the heightmap is a
  per-scenario N64 file and the DOS game has no elevation. They export as 4225
  zero bytes, exactly like the cartridge's own FLAT.IMG fallback.

Read-only on the MIX.
"""
import collections, os, re, struct

HERE = os.path.dirname(os.path.abspath(__file__))
EX = os.path.join(HERE, "..", "bakery", "sharecopy", "assets", "extracted")
GENERAL = os.path.join(HERE, "..", "..", "data", "dosdata", "GENERAL.MIX")

MULTIPLAYER = [f"SCM{n:02d}EA" for n in
               list(range(1, 10)) + [70, 71, 72, 73, 74, 77, 96]]
DINOSAUR = [f"SCJ{n:02d}EA" for n in range(1, 6)]

# The cartridge has a DESERT and a TEMPERAT tile bank and nothing else. A winter
# map is drawn with the temperate bank and the viewer says so; it is not passed
# off as the real thing.
THEATER_MAP = {"DESERT": "DESERT", "TEMPERATE": "TEMPERAT", "WINTER": "TEMPERAT"}


def mix_id(name):
    """audio/mixfile.c:25, in python. Uppercased, four bytes at a time."""
    v, b, i = 0, name.upper().encode(), 0
    L = len(b)
    while i < L:
        a = 0
        for j in range(4):
            a >>= 8
            if i + j < L:
                a |= b[i + j] << 24
            a &= 0xFFFFFFFF
        v = ((v << 1) | (v >> 31)) & 0xFFFFFFFF
        v = (v + a) & 0xFFFFFFFF
        i += 4
    return v


class Mix:
    def __init__(self, path):
        self.d = open(path, "rb").read()
        count, _size = struct.unpack_from("<Hi", self.d, 0)
        base = 6 + count * 12
        self.ids = {}
        for i in range(count):
            fid, off, sz = struct.unpack_from("<Iii", self.d, 6 + i * 12)
            self.ids[fid & 0xFFFFFFFF] = (base + off, sz)

    def get(self, name):
        e = self.ids.get(mix_id(name))
        return self.d[e[0]:e[0] + e[1]] if e else None


_inv = {}


def inverse_tl(root):
    """(template, icon) -> N64 atlas slot, from the theater's own TL4/TL8."""
    if root in _inv:
        return _inv[root]
    n4 = os.path.getsize(os.path.join(EX, f"DA4/{root}.DA4")) // 288
    n8 = os.path.getsize(os.path.join(EX, f"DA8/{root}.DA8")) // 576
    t4 = open(os.path.join(EX, f"TL4/{root}.TL4"), "rb").read()
    t8 = open(os.path.join(EX, f"TL8/{root}.TL8"), "rb").read()
    inv = {}
    for i in range(n4):
        inv.setdefault((t4[2 * i], t4[2 * i + 1]), i)
    for k in range(n8):
        inv.setdefault((t8[2 * k], t8[2 * k + 1]), n4 + k)
    _inv[root] = (inv, n4, n8)
    return _inv[root]


def parse_ini_text(txt):
    secs, sec = collections.defaultdict(list), None
    for line in txt.splitlines():
        s = line.strip()
        if s.startswith("["):
            sec = s.strip("[]")
        elif s and sec and "=" in s:
            k, v = s.split("=", 1)
            secs[sec].append((k.strip(), v.strip()))
    return secs


def scenarios():
    """-> list of dicts the exporter can treat exactly like a cartridge map."""
    if not os.path.exists(GENERAL):
        return []
    mix = Mix(GENERAL)
    out = []
    for scen in MULTIPLAYER + DINOSAUR:
        ini_raw, binf = mix.get(scen + ".INI"), mix.get(scen + ".BIN")
        if not ini_raw or not binf or len(binf) < 8192:
            continue
        txt = ini_raw.decode("latin-1")
        secs = parse_ini_text(txt)
        mp = {k.upper(): v for k, v in secs.get("Map", []) + secs.get("MAP", [])}
        theater_dos = mp.get("THEATER", "TEMPERATE").upper()
        root = THEATER_MAP.get(theater_dos, "TEMPERAT")
        inv, n4, n8 = inverse_tl(root)

        slots, tmpls, unresolved = bytearray(), bytearray(), 0
        for i in range(4096):
            t, ic = binf[i * 2], binf[i * 2 + 1]
            x, y = i % 64, i // 64
            if t == 255:
                slot, tm = (x & 3) | ((y & 3) << 2), 255       # CellClass::Clear_Icon
            else:
                s = inv.get((t, ic))
                if s is None:
                    unresolved += 1
                    slot, tm = (x & 3) | ((y & 3) << 2), t
                else:
                    slot, tm = s, t
            slots += struct.pack("<H", slot)
            tmpls.append(tm)

        name = ""
        m = re.search(r"^\s*Name\s*=\s*(.+)$", txt, re.M | re.I)
        if m:
            name = m.group(1).strip()
        out.append(dict(
            scenario=scen, theater=root, theaterDos=theater_dos,
            source=f"GENERAL.MIX ({scen}.BIN, DOS template/icon pairs)",
            ini_text=txt, ini=secs,
            playable=[int(mp.get("X", 1) or 1), int(mp.get("Y", 1) or 1),
                      int(mp.get("WIDTH", 62) or 62), int(mp.get("HEIGHT", 62) or 62)],
            tiles=bytes(slots), tmpl=bytes(tmpls),
            unresolved=unresolved, name=name,
            category="multiplayer" if scen[2] == "M" else "dinosaur",
            dinosaur=scen[2] == "J",
            side="Nod" if scen[2] == "B" else ("GDI" if scen[2] == "G" else "-"),
        ))
    return out


if __name__ == "__main__":
    for s in scenarios():
        print(f"{s['scenario']:9s} {s['theaterDos']:10s} -> {s['theater']:8s} "
              f"unresolved {s['unresolved']:4d}  {s['name']}")
