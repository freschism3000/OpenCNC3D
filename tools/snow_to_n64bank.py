#!/usr/bin/env python3
"""snow_to_n64bank.py -- build the SNOW terrain bank: Red Alert's snow LOOK on TD's
template set.

The TD brain has no snow theater, so SNOW is an aliased look: maps say Theater=WINTER
to the engine and CNC3DTheater=SNOW to the pipeline. The bank this tool writes is
therefore shaped exactly like the WINTER one (tools/win_to_n64bank.py): the SAME 140
TD winter-flagged templates, the same TL8 built per (template, cell) -- only the ART
differs per template:

  - RA snow art (<NAME>.SNO out of an RA-format snow.mix) where the RA template is
    the SAME inherited template: the name matches (allowing RA's zero padding,
    SH1 -> SH01) AND the icon set covers every cell TD's .WIN covers at 24x24.
  - TD winter art otherwise (slopes above all: RA has no slope templates), remapped
    index-by-index into the snow palette by nearest RGB.

Every choice is printed: the per-template source table is the tool's main output, and
a proof sheet (TD row vs RA row per mapped template) lands beside the bank for eyes
to check. Nothing is substituted silently.

Run:  python3 tools/snow_to_n64bank.py [--mix PATH] [--pal PATH] [--sheets DIR]
"""
import argparse
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
EX = os.path.join(ROOT, "tools", "bakery", "sharecopy", "assets", "extracted")
DEFAULT_SNOW_MIX = os.path.expanduser(
    "~/Library/Application Support/OpenRA/Content/ca/snow.mix")
DEFAULT_WINTER_MIX = os.path.expanduser(
    "~/Library/Application Support/OpenRA/Content/cnc/winter.mix")

sys.path.insert(0, os.path.join(ROOT, "menu", "tools"))
sys.path.insert(0, HERE)
import mixshp  # noqa: E402
import win_to_n64bank as WB  # noqa: E402  (TD enum + winter template list + .WIN parser)

TS = 24
ICON_BYTES = TS * TS


class RaMix(object):
    """An RA-format MIX: u32 flags (0 = plain), then the TD-shaped index.

    Offsets are relative to the end of the index. The name hash is TD's own
    (mixshp.mix_id) -- RA kept it for 8.3 names.
    """
    def __init__(self, path):
        self.data = open(path, "rb").read()
        flags = struct.unpack_from("<I", self.data, 0)[0]
        if flags & 0x00020000:
            raise SystemExit(path + " is encrypted; this tool reads plain RA mixes")
        base = 4 if (flags & 0xFFFF) == 0 else 0   # TD mixes have count in the low u16
        count, size = struct.unpack_from("<HI", self.data, base)
        self.index = {}
        p = base + 6
        body = p + 12 * count
        for _ in range(count):
            fid, off, sz = struct.unpack_from("<iII", self.data, p)
            self.index[fid & 0xFFFFFFFF] = (body + off, sz)
            p += 12
        if body + size != len(self.data):
            raise SystemExit("%s: index arithmetic off (%d + %d != %d)"
                             % (path, body, size, len(self.data)))

    def has(self, name):
        return (mixshp.mix_id(name) & 0xFFFFFFFF) in self.index

    def read(self, name):
        e = self.index.get(mixshp.mix_id(name) & 0xFFFFFFFF)
        if e is None:
            return None
        off, sz = e
        return self.data[off:off + sz]


def parse_sno(data, name):
    """An RA icon set -> {cell_index: 576-byte image}, from the RA IconControlType.

    struct (brain/vanilla/common/stamp.cpp): i16 Width, Height, Count, Allocated,
    MapWidth, MapHeight; i32 Size, Icons(==0x28), Palettes, Remaps, TransFlag,
    ColorMap, Map. Map[cell] = stored image index, 0xFF = hole.
    """
    w, h, count, _alloc, _mw, _mh, size, icons, _pal, _rem, _trans, _cmap, imap = \
        struct.unpack_from("<6h7i", data, 0)
    if not (w == TS and h == TS and icons == 0x28):
        raise SystemExit("%s: unexpected RA header w=%d h=%d icons=%#x"
                         % (name, w, h, icons))
    if size != len(data):
        raise SystemExit("%s: size field %d != file %d" % (name, size, len(data)))
    out = {}
    cmap = data[imap:imap + count]
    for cell, img in enumerate(cmap):
        if img == 0xFF:
            continue
        o = icons + img * ICON_BYTES
        px = data[o:o + ICON_BYTES]
        if len(px) != ICON_BYTES:
            raise SystemExit("%s: image %d runs off the file" % (name, img))
        out[cell] = px
    return out


def ra_candidates(td_ini):
    """RA spellings to try for a TD IniName, most specific first."""
    c = [td_ini]
    m = re.match(r"^([A-Z]+)(\d+)$", td_ini)
    if m and len(m.group(2)) == 1:
        c.append("%s0%s" % (m.group(1), m.group(2)))       # SH1 -> SH01
    return c


def load_pal(path):
    raw = open(path, "rb").read()
    if len(raw) != 768:
        raise SystemExit(path + " is not a 768-byte palette")
    if max(raw) > 63:
        return bytes(raw)                                   # already 8-bit
    return bytes((v << 2) | (v >> 4) for v in raw)


def _pal_rgb(pal, i):
    return (pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2])


def _d2(a, b):
    return (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2


def _lum(c):
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


class Classify(object):
    """WATER / GROUND / OTHER for every index of one palette, seeded by the theater's
    own W1+W2 (water) and CLEAR1 (ground) art. Colour-distance to the nearest seed,
    capped -- rock and detail stay OTHER. This replaced a histogram-membership rule
    that only knew the 17 indices CLEAR1 itself uses, which left every other ground
    pixel of a shore tile unclassified and turned the recolored shores into static."""
    CAP = 1600  # 40^2 in RGB

    def __init__(self, pal, ground_icons, water_icons):
        gseed = sorted(set(b for px in ground_icons for b in px))
        wseed = sorted(set(b for px in water_icons for b in px))
        self.pal = pal
        self.gcol = [_pal_rgb(pal, i) for i in gseed]
        self.wcol = [_pal_rgb(pal, i) for i in wseed]
        self.kind = bytearray(256)          # 0 other, 1 ground, 2 water
        for i in range(256):
            c = _pal_rgb(pal, i)
            dg = min(_d2(c, g) for g in self.gcol) if self.gcol else 1 << 30
            # A theater with no water at all (Arrakis) simply classifies none.
            dw = min(_d2(c, w) for w in self.wcol) if self.wcol else 1 << 30
            if dw <= dg and dw <= self.CAP:
                self.kind[i] = 2
            elif dg <= self.CAP:
                self.kind[i] = 1

    def masks(self, px):
        """(water_mask, feature_mask) as bytes of 0/1 per texel."""
        wm = bytes(1 if self.kind[b] == 2 else 0 for b in px)
        fm = bytes(1 if self.kind[b] == 0 else 0 for b in px)
        return wm, fm


def rank_transfer(src_cls, dst_cls):
    """index-in-src -> index-in-dst preserving TEXTURE: ground maps to ground and
    water to water BY LUMINANCE RANK (monotone -- similar darks stay similar pales,
    so dither survives as dither instead of noise); OTHER takes plain nearest."""
    def ramp(pal, kind, want):
        idx = [i for i in range(256) if kind[i] == want]
        return sorted(idx, key=lambda i: _lum(_pal_rgb(pal, i)))
    sg, sw = ramp(src_cls.pal, src_cls.kind, 1), ramp(src_cls.pal, src_cls.kind, 2)
    dg, dw = ramp(dst_cls.pal, dst_cls.kind, 1), ramp(dst_cls.pal, dst_cls.kind, 2)
    table = bytearray(256)
    for i in range(256):
        k = src_cls.kind[i]
        if k == 1 and sg and dg:
            table[i] = dg[min(len(dg) - 1, (sg.index(i) * len(dg)) // len(sg))]
        elif k == 2 and sw and dw:
            table[i] = dw[min(len(dw) - 1, (sw.index(i) * len(dw)) // len(sw))]
        else:
            c = _pal_rgb(src_cls.pal, i)
            best, bd = 0, 1 << 30
            for j in range(256):
                d = _d2(c, _pal_rgb(dst_cls.pal, j))
                if d < bd:
                    best, bd = j, d
            table[i] = best
    return bytes(table)


def _structure36(px, pal):
    """6x6 zero-mean block luminances -- the SHAPE of the cell's art, colour-blind.
    Rock masses, road bands and cliff spines survive this; flat filler reads flat."""
    lums = [_lum(_pal_rgb(pal, b)) for b in px]
    out = []
    for by in range(6):
        for bx in range(6):
            t = 0.0
            for dy in range(4):
                row = (by * 4 + dy) * TS + bx * 4
                for dx in range(4):
                    t += lums[row + dx]
            out.append(t / 16.0)
    m = sum(out) / 36.0
    return [v - m for v in out]


def graft_pool(snow, names, cls):
    """[(label, px, water_mask, structure)] over every cell of the named RA
    templates that exist in the mix."""
    pool = []
    for n in names:
        if not snow.has(n + ".SNO"):
            continue
        for cell, px in sorted(parse_sno(snow.read(n + ".SNO"), n).items()):
            wm, _ = cls.masks(px)
            pool.append(("%s.%d" % (n, cell), px, wm, _structure36(px, cls.pal)))
    return pool


def graft_pick(px, src_cls, pool, land):
    """The RA cell that best stands in for this TD cell.

    WATER families match on the water-mask silhouette (a shoreline on the wrong side
    is the worst possible failure), with structure as the tie-scale. LAND families
    match on structure alone -- a colour classifier cannot tell winter rock from
    winter ground (both are brown within 40 RGB), which is exactly how the first
    attempt melted every slope into flat CLEAR1 -- plus a heavy penalty for any
    water pixels in the candidate, so a slope never grafts a beach. Deterministic."""
    wm, _ = src_cls.masks(px)
    st = _structure36(px, src_cls.pal)
    import math
    stn = math.sqrt(sum(v * v for v in st))
    best, bs = None, None
    for label, rpx, rwm, rst in pool:
        if land:
            # CORRELATION, not distance: winter draws rock-on-dark-ground at a
            # fraction of the snow theater's rock-on-white contrast, and raw SSD
            # therefore preferred flat CLEAR1 over any correctly-shaped rocky cell.
            # Zero-mean cosine is amplitude-blind: shape against shape. A TD cell
            # that really is flat correlates with nothing and falls through to the
            # least-structured candidate, which is CLEAR1 -- the right answer.
            rn = math.sqrt(sum(v * v for v in rst))
            cos = (sum(a * b for a, b in zip(st, rst)) / (stn * rn)
                   if stn > 1.0 and rn > 1.0 else 0.0)
            sc = -cos + 10.0 * sum(rwm) + (rn * 1e-6)
        else:
            ssd = sum((a - b) * (a - b) for a, b in zip(st, rst))
            sc = 1000.0 * sum(a != b for a, b in zip(wm, rwm)) + ssd
        if bs is None or sc < bs:
            best, bs = (label, rpx), sc
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mix", default=DEFAULT_SNOW_MIX)
    ap.add_argument("--winter-mix", default=DEFAULT_WINTER_MIX)
    ap.add_argument("--pal", required=True, help="RA SNOW.PAL (768 bytes)")
    ap.add_argument("--sheets", default=None, help="write per-template proof sheets here")
    ap.add_argument("--force-fallback", default="",
                    help="comma-separated TD IniNames to keep on TD winter art even "
                         "though RA art exists (visual-judge verdicts)")
    a = ap.parse_args()

    forced = set(x for x in a.force_fallback.upper().split(",") if x)
    snow = RaMix(a.mix)
    wmix = mixshp.MixFile(a.winter_mix)

    snow_pal = load_pal(a.pal)
    wpal_raw = wmix.read("WINTER.PAL")
    winter_pal = bytes((v << 2) | (v >> 4) for v in wpal_raw)

    wcls = Classify(winter_pal,
                    [px for _, px in WB.parse_win(wmix.read("CLEAR1.WIN"), "CLEAR1")],
                    [px for _, px in WB.parse_win(wmix.read("W1.WIN"), "W1")]
                    + [px for _, px in WB.parse_win(wmix.read("W2.WIN"), "W2")])
    scls = Classify(snow_pal,
                    list(parse_sno(snow.read("CLEAR1.SNO"), "CLEAR1").values()),
                    list(parse_sno(snow.read("W1.SNO"), "W1").values())
                    + list(parse_sno(snow.read("W2.SNO"), "W2").values()))
    print("classify: winter %d ground / %d water indices; snow %d / %d"
          % (sum(1 for k in wcls.kind if k == 1), sum(1 for k in wcls.kind if k == 2),
             sum(1 for k in scls.kind if k == 1), sum(1 for k in scls.kind if k == 2)))
    bridge_map = rank_transfer(wcls, scls)

    # Graft pools: genuine RA snow cells, grouped by what they can stand in for.
    def _n(fmt, lo, hi):
        return [fmt % i for i in range(lo, hi + 1)]
    POOLS = {
        "water": graft_pool(snow, ["CLEAR1", "W1", "W2"] + _n("SH%02d", 1, 56)
                            + _n("RV%02d", 1, 14) + ["FORD1", "FORD2",
                                                     "FALLS1", "FALLS2"], scls),
        "slope": graft_pool(snow, ["CLEAR1"] + _n("S%02d", 1, 38), scls),
        "road":  graft_pool(snow, ["CLEAR1"] + _n("D%02d", 1, 43), scls),
        "patch": graft_pool(snow, ["CLEAR1", "P01", "P02", "P03", "P04", "P07",
                                   "P08", "P13", "P14", "P15", "B1", "B2", "B3"]
                            + _n("RF%02d", 1, 11), scls),
    }
    for k in POOLS:
        print("pool %-5s: %d RA cells" % (k, len(POOLS[k])))

    def family_of(ini):
        if ini.startswith("BRIDGE"):
            return None                       # keep the bridge structure: recolor
        if ini.startswith(("SH", "RV", "W", "FORD", "FALLS")):
            return "water"
        if ini.startswith("S"):
            return "slope"
        if ini.startswith("D"):
            return "road"
        return "patch"                        # P*, B*

    da8, tl8 = bytearray(), bytearray()
    src_ra, src_fb, forced_used = [], [], []
    sheets = []
    for tid, ini in WB.winter_templates():
        wdata = wmix.read(ini + ".WIN")
        if wdata is None:
            raise SystemExit("no %s.WIN in the winter mix -- the TD side is the "
                             "structure and must be complete" % ini)
        td_cells = WB.parse_win(wdata, ini)          # [(cell, px)] in cell order

        ra_cells = None
        ra_name = None
        if ini not in forced:
            for cand in ra_candidates(ini):
                if snow.has(cand + ".SNO"):
                    got = parse_sno(snow.read(cand + ".SNO"), cand)
                    if all(c in got for c, _ in td_cells):
                        ra_cells = got
                        ra_name = cand
                    break                            # first existing name decides
        elif any(snow.has(c + ".SNO") for c in ra_candidates(ini)):
            forced_used.append(ini)

        fam = family_of(ini) if ra_cells is None else None
        grafted = []
        for cell, wpx in td_cells:
            if ra_cells is not None:
                da8 += ra_cells[cell]
            elif fam is None:
                da8 += bytes(bridge_map[b] for b in wpx)   # bridges: recolor, keep art
            else:
                label, rpx = graft_pick(wpx, wcls, POOLS[fam], fam != "water")
                da8 += rpx
                grafted.append(label)
            tl8 += bytes((tid, cell))
        if grafted:
            print("  graft %-8s <- %s" % (ini, " ".join(grafted)))
        (src_ra if ra_cells is not None else src_fb).append(
            "%s%s" % (ini, ("<-" + ra_name) if ra_name and ra_name != ini else ""))
        if a.sheets and ra_cells is not None:
            sheets.append((ini, td_cells, ra_cells))

    n8 = len(tl8) // 2
    for ext, blob in (("DA8", bytes(da8)), ("TL8", bytes(tl8)),
                      ("ND8", bytes(n8)), ("PA8", snow_pal),
                      ("DA4", b""), ("TL4", b""), ("ND4", b""), ("PA4", b"")):
        open(os.path.join(EX, ext, "SNOW." + ext), "wb").write(blob)

    print("SNOW bank: %d tiles over %d templates" % (n8, len(src_ra) + len(src_fb)))
    print("  RA snow art (%d): %s" % (len(src_ra), " ".join(src_ra)))
    print("  TD winter art remapped to the snow palette (%d): %s"
          % (len(src_fb), " ".join(src_fb)))
    if forced_used:
        print("  forced to fallback by the visual judge (%d): %s"
              % (len(forced_used), " ".join(forced_used)))

    if a.sheets:
        from PIL import Image
        os.makedirs(a.sheets, exist_ok=True)
        wp = [tuple(winter_pal[i * 3:i * 3 + 3]) for i in range(256)]
        sp = [tuple(snow_pal[i * 3:i * 3 + 3]) for i in range(256)]
        for ini, td_cells, ra_cells in sheets:
            n = len(td_cells)
            im = Image.new("RGB", (n * TS, 2 * TS), (40, 40, 48))
            for k, (cell, wpx) in enumerate(td_cells):
                for y in range(TS):
                    for x in range(TS):
                        im.putpixel((k * TS + x, y), wp[wpx[y * TS + x]])
                        im.putpixel((k * TS + x, TS + y),
                                    sp[ra_cells[cell][y * TS + x]])
            im = im.resize((n * TS * 3, 2 * TS * 3), Image.NEAREST)
            im.save(os.path.join(a.sheets, ini + ".png"))
        print("proof sheets: %d in %s (top row TD winter, bottom row RA snow)"
              % (len(sheets), a.sheets))


if __name__ == "__main__":
    main()
