#!/usr/bin/env python3
"""sand_to_n64bank.py -- build the SAND terrain bank: Arrakis (Dune 2000) on TD's
template set.

Same aliasing pattern as SNOW (see snow_to_n64bank.py): the TD brain has no sand
theater, so SAND maps say Theater=DESERT to the engine and CNC3DTheater=SAND to
everything of ours. The bank's skeleton is TD's DESERT-flagged template set out of
DESERT.MIX (.DES icon sets); the ART is Dune 2000's:

  - BLOXBASE.R8 (800 32x32 8bpp tiles, 29-byte record headers -- format verified by
    exact whole-file parse) supplies the ground: CLEAR1 becomes the 16 flattest
    plain-sand tiles; slopes/cliffs and boulders GRAFT rock/edge tiles and patches
    graft dune tiles, per cell, by the same amplitude-blind structure correlation
    the snow bank uses. Every D2K tile is Lanczos-resampled 32->24 and requantised
    to the D2K palette -- a documented resample, recorded as an open gap.
  - Families Dune 2000 has no art for -- roads, and every water template -- keep TD
    DESERT art under the luminance-rank ramp transfer so their ground matches D2K
    sand. Arrakis has no water; desert water recolors to the nearest thing the D2K
    palette knows and is recorded as a gap, not hidden.

Tile families come from a labels JSON (index -> family) produced by a fan-out of
readers over the numbered contact sheet; the tool refuses to run without it rather
than guess. Run:

  python3 tools/sand_to_n64bank.py --d2k <dir with BLOXBASE.R8 + PALETTE.BIN> \\
      --labels <labels.json> [--sheets DIR]
"""
import argparse
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
EX = os.path.join(ROOT, "tools", "bakery", "sharecopy", "assets", "extracted")
DEFAULT_DESERT_MIX = os.path.expanduser(
    "~/Library/Application Support/OpenRA/Content/cnc/desert.mix")

sys.path.insert(0, os.path.join(ROOT, "menu", "tools"))
sys.path.insert(0, HERE)
import mixshp  # noqa: E402
import win_to_n64bank as WB  # noqa: E402
import re  # noqa: E402


def template_dims():
    """IniName -> (w, h) straight off TD's cdata constructors."""
    src = open(WB.CDATA, errors="ignore").read()
    out = {}
    for m in re.finditer(
            r'TemplateTypeClass\s+const\s*\n?\s*\w+\(\s*TEMPLATE_[A-Z0-9]+\s*,'
            r'\s*[^"]*?"([A-Z0-9]+)"\s*,\s*\w+\s*,\s*\w+\s*,\s*(\d+)\s*,\s*(\d+)',
            src, re.S):
        out[m.group(1)] = (int(m.group(2)), int(m.group(3)))
    return out
from snow_to_n64bank import (Classify, rank_transfer, _structure36,  # noqa: E402
                             _pal_rgb, _d2, load_pal, TS, ICON_BYTES)


def read_r8(path):
    """[(w, h, pixels)] -- the record layout proved against BLOXBASE.R8:
    {u8 type; i32 w; i32 h; 16 bytes of runtime junk; u8 bpp; u8 w2; u8 h2; u8 pad}
    then w*h bytes. 800 records, all type 1, all 32x32, file ends exactly."""
    d = open(path, "rb").read()
    out, p = [], 0
    while p < len(d):
        t = d[p]
        if t == 0:
            p += 1
            continue
        w, h = struct.unpack_from("<ii", d, p + 1)
        out.append((w, h, d[p + 29:p + 29 + w * h]))
        p += 29 + w * h
    return out


def downscale24(px, w, h, pal, cache):
    """32x32 indexed -> 24x24 indexed in the SAME palette, Lanczos through RGB."""
    from PIL import Image
    im = Image.new("RGB", (w, h))
    im.putdata([_pal_rgb(pal, b) for b in px])
    im = im.resize((TS, TS), Image.LANCZOS)
    out = bytearray()
    for rgb in im.getdata():
        if rgb not in cache:
            best, bd = 0, 1 << 30
            for j in range(256):
                d = _d2(rgb, _pal_rgb(pal, j))
                if d < bd:
                    best, bd = j, d
            cache[rgb] = best
        out.append(cache[rgb])
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--d2k", required=True)
    ap.add_argument("--labels", required=True)
    ap.add_argument("--desert-mix", default=DEFAULT_DESERT_MIX)
    ap.add_argument("--sheets", default=None)
    ap.add_argument("--slope-rects", default=None,
                    help="JSON {S01: topLeftIndex, ...}: per-slope 2x2 rectangles in "
                         "the D2K sheet's native 20-wide grid, picked and reconciled "
                         "by a vision pass. With this, slopes stop grafting cell by "
                         "cell and take a REAL patch of one D2K formation, which is "
                         "what makes adjacent cliff cells join.")
    a = ap.parse_args()

    sand_pal = load_pal(os.path.join(a.d2k, "PALETTE.BIN"))
    dmix = mixshp.MixFile(a.desert_mix)
    dpal_raw = dmix.read("DESERT.PAL")
    desert_pal = bytes((v << 2) | (v >> 4) for v in dpal_raw)

    labels = {int(k): v for k, v in json.load(open(a.labels)).items()}
    tiles = read_r8(os.path.join(a.d2k, "BLOXBASE.R8"))
    print("d2k: %d tiles, %d labeled" % (len(tiles), len(labels)))

    cache = {}
    rects = {}
    if a.slope_rects:
        dims = template_dims()
        raw = json.load(open(a.slope_rects))
        for ini, val in raw.items():
            tw, th = dims[ini.upper()]        # slopes are MOSTLY 2x2; S21 is 1x2 etc.
            if isinstance(val, list):
                # Explicit per-cell tile ids -- the map-statistics derivation emits
                # these: real windows out of shipped D2K maps, not sheet rects.
                if len(val) != tw * th:
                    raise SystemExit("slope %s: %d ids for a %dx%d grid"
                                     % (ini, len(val), tw, th))
                ids = [int(v) for v in val]
            else:
                tl = int(val)
                if tl % 20 + tw > 20 or tl + (th - 1) * 20 + tw - 1 >= len(tiles):
                    raise SystemExit("slope rect %s=%d does not fit a %dx%d patch of "
                                     "the 20-wide grid" % (ini, tl, tw, th))
                ids = [tl + dy * 20 + dx for dy in range(th) for dx in range(tw)]
            cellpx = {}
            for k, t in enumerate(ids):
                w, h, px = tiles[t]
                cellpx[k] = downscale24(px, w, h, sand_pal, cache)
            rects[ini.upper()] = (ids[0], cellpx)
        print("slope rects: %d templates pinned to D2K formations" % len(rects))

    fam_px = {}
    for i, (w, h, px) in enumerate(tiles):
        f = labels.get(i)
        if f in ("sand", "dune", "rock", "edge", "other"):
            fam_px.setdefault(f, []).append((i, downscale24(px, w, h, sand_pal, cache)))
    for f in sorted(fam_px):
        print("  family %-5s: %d tiles" % (f, len(fam_px[f])))

    # CLEAR1's 16 variants: the flattest sand, deterministically.
    def variance(px):
        from snow_to_n64bank import _lum
        ls = [_lum(_pal_rgb(sand_pal, b)) for b in px]
        m = sum(ls) / len(ls)
        return sum((v - m) ** 2 for v in ls)
    sand_sorted = sorted(fam_px["sand"], key=lambda t: (variance(t[1]), t[0]))
    clear16 = [px for _, px in sand_sorted[:16]]
    if len(clear16) < 16:
        raise SystemExit("fewer than 16 plain sand tiles labeled")

    # Graft pools with structure vectors (no water anywhere on Arrakis).
    def pool_of(fams):
        pool = []
        for f in fams:
            for i, px in fam_px.get(f, []):
                pool.append(("blox.%d" % i, px, _structure36(px, sand_pal)))
        return pool
    POOLS = {
        "slope": pool_of(["edge", "rock"]),
        "patch": pool_of(["dune", "other", "sand"]),
    }

    # Desert ships W1 but no W2 art; seed water with what exists.
    dwater = []
    for wn in ("W1", "W2"):
        if dmix.has(wn + ".DES"):
            dwater += [px for _, px in WB.parse_win(dmix.read(wn + ".DES"), wn)]
    dcls = Classify(desert_pal,
                    [px for _, px in WB.parse_win(dmix.read("CLEAR1.DES"), "CLEAR1")],
                    dwater)
    scls = Classify(sand_pal, clear16, [])   # Arrakis: no water seeds at all
    recolor = rank_transfer(dcls, scls)

    import math

    def graft(px, pool):
        st = _structure36(px, dcls.pal)
        stn = math.sqrt(sum(v * v for v in st))
        best, bs = None, None
        for label, rpx, rst in pool:
            rn = math.sqrt(sum(v * v for v in rst))
            cos = (sum(x * y for x, y in zip(st, rst)) / (stn * rn)
                   if stn > 1.0 and rn > 1.0 else 0.0)
            sc = -cos + rn * 1e-6
            if bs is None or sc < bs:
                best, bs = (label, rpx), sc
        return best

    def family_of(ini):
        if ini == "CLEAR1":
            return "clear"
        if ini.startswith(("W", "SH", "RV", "FORD", "FALLS", "BRIDGE", "BR", "D")):
            return "recolor"          # water families and roads: TD art, recolored
        if ini.startswith("S"):
            return "slope"
        return "patch"                # P*, B*

    da8, tl8 = bytearray(), bytearray()
    counts = {}
    sheets = []
    for tid, ini in WB.theater_templates("THEATERF_DESERT"):
        if not dmix.has(ini + ".DES"):
            # TD ships desert without a few flagged templates (ROAD33 is the known
            # one -- bin_to_n64map carries it in KNOWN_ABSENT). No art, no slot;
            # said out loud, same as everywhere else.
            print("  absent %-8s: no .DES in the desert mix -- no slot" % ini)
            continue
        wdata = dmix.read(ini + ".DES")
        td_cells = WB.parse_win(wdata, ini)
        fam = family_of(ini)
        counts[fam] = counts.get(fam, 0) + 1
        grafted = []
        row = []
        for k, (cell, dpx) in enumerate(td_cells):
            if ini.upper() in rects:
                tl, cellpx = rects[ini.upper()]
                out = cellpx[cell]          # cell index IS the 2x2 position
                if k == 0:
                    grafted.append("rect@%d" % tl)
            elif fam == "clear":
                out = clear16[k % 16]
            elif fam == "recolor":
                out = bytes(recolor[b] for b in dpx)
            else:
                label, out = graft(dpx, POOLS[fam])
                grafted.append(label)
            da8 += out
            tl8 += bytes((tid, cell))
            row.append((dpx, out))
        if grafted:
            print("  graft %-8s <- %s" % (ini, " ".join(grafted)))
        if a.sheets and fam in ("slope", "patch", "clear"):
            sheets.append((ini, row))

    n8 = len(tl8) // 2
    for ext, blob in (("DA8", bytes(da8)), ("TL8", bytes(tl8)),
                      ("ND8", bytes(n8)), ("PA8", sand_pal),
                      ("DA4", b""), ("TL4", b""), ("ND4", b""), ("PA4", b"")):
        open(os.path.join(EX, ext, "SAND." + ext), "wb").write(blob)
    print("SAND bank: %d tiles; families: %s" % (n8, counts))

    if a.sheets:
        from PIL import Image
        os.makedirs(a.sheets, exist_ok=True)
        dp = [tuple(desert_pal[i * 3:i * 3 + 3]) for i in range(256)]
        sp = [tuple(sand_pal[i * 3:i * 3 + 3]) for i in range(256)]
        for ini, row in sheets:
            n = len(row)
            im = Image.new("RGB", (n * TS, 2 * TS), (40, 40, 48))
            for k, (dpx, out) in enumerate(row):
                for y in range(TS):
                    for x in range(TS):
                        im.putpixel((k * TS + x, y), dp[dpx[y * TS + x]])
                        im.putpixel((k * TS + x, TS + y), sp[out[y * TS + x]])
            im = im.resize((n * TS * 3, 2 * TS * 3), Image.NEAREST)
            im.save(os.path.join(a.sheets, ini + ".png"))
        print("proof sheets: %d in %s (top TD desert, bottom SAND)"
              % (len(sheets), a.sheets))


if __name__ == "__main__":
    main()
