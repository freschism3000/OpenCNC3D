#!/usr/bin/env python3
"""sand_rects_from_maps.py -- derive the SAND theater's slope art from Dune 2000's
OWN MAPS instead of anyone's eyes.

Three vision passes over the tilesheet each left seams that were visible, because the
sheet's layout only SUGGESTS which tiles join; the shipped .MAP files STATE it: a
D2K map is a plain grid of tile indices (u16 w, u16 h, then w*h * {u16 tile, u16
attr}), so every 2x2 window that occurs in a real map is a combination Westwood's
own designers shipped. This tool:

  1. harvests every WxH window (per TD template grid, mostly 2x2) from every .MAP,
     with occurrence counts and the observed VERTICAL/HORIZONTAL chaining between
     window positions;
  2. profiles each TD desert slope template's art (quadrant featureness against the
     desert ground colours) and each candidate window (quadrant rockness from the
     five-agent family labels);
  3. picks, per template, the real window whose profile matches best -- run triples
     must additionally CHAIN in the maps (window B observed directly below window A
     somewhere), so a wall stacked from the variants reproduces arrangements that
     exist in shipped content;
  4. writes the slope table as explicit per-cell tile-id lists for sand_to_n64bank
     --slope-rects (which accepts lists as well as sheet rects).

Deterministic: counts and fixed tie-breaks, no vision anywhere.
"""
import argparse
import glob
import json
import os
import struct
import sys
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "menu", "tools"))
import mixshp  # noqa: E402
import win_to_n64bank as WB  # noqa: E402
from snow_to_n64bank import _pal_rgb, _d2, _lum  # noqa: E402
from sand_to_n64bank import template_dims  # noqa: E402

TS = 24


def load_maps(d2k):
    out = []
    for p in sorted(glob.glob(os.path.join(d2k, "*.MAP"))):
        d = open(p, "rb").read()
        w, h = struct.unpack_from("<HH", d, 0)
        if len(d) - 4 != w * h * 4:
            continue
        grid = [struct.unpack_from("<H", d, 4 + 4 * i)[0] for i in range(w * h)]
        out.append((os.path.basename(p), w, h, grid))
    return out


def harvest(maps, tw, th, nt):
    """windows[(t0..)] = count over every map position; below[(A,B)] += 1 when
    window B sits exactly th rows below window A; right likewise tw cols right."""
    wins = Counter()
    below = Counter()
    right = Counter()
    at = {}
    for _, w, h, g in maps:
        pos = {}
        for y in range(h - th + 1):
            for x in range(w - tw + 1):
                ids = tuple(g[(y + dy) * w + (x + dx)]
                            for dy in range(th) for dx in range(tw))
                if any(t >= nt for t in ids):
                    continue
                wins[ids] += 1
                pos[(x, y)] = ids
        for (x, y), a in pos.items():
            b = pos.get((x, y + th))
            if b is not None:
                below[(a, b)] += 1
            r = pos.get((x + tw, y))
            if r is not None:
                right[(a, r)] += 1
    return wins, below, right


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--d2k", required=True)
    ap.add_argument("--labels", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--desert-mix", default=os.path.expanduser(
        "~/Library/Application Support/OpenRA/Content/cnc/desert.mix"))
    a = ap.parse_args()

    labels = {int(k): v for k, v in json.load(open(a.labels)).items()}
    maps = load_maps(a.d2k)
    print("maps: %d (%s...)" % (len(maps), ", ".join(m[0] for m in maps[:4])))

    # D2K per-tile SUB-QUADRANT featureness from the PIXELS, not the labels: the
    # labels cannot say which half of an edge tile the rock sits on, and that is
    # exactly what tells an NE corner from an SE corner. Sand reference colours =
    # the colours of every tile the labelers called plain sand.
    from sand_to_n64bank import read_r8
    d2pal = open(os.path.join(a.d2k, "PALETTE.BIN"), "rb").read()
    d2pal = bytes((v << 2) | (v >> 4) for v in d2pal)
    tiles32 = read_r8(os.path.join(a.d2k, "BLOXBASE.R8"))
    # DOMINANT sand colours only: a sand tile carries incidental dark speckles,
    # and admitting every speck made the whole palette "sand" -- rockness went to
    # zero everywhere and no window matched anything. Keep indices covering at
    # least 0.5% of all sand-tile texels: the actual dune ramp.
    hist = Counter(b for i, (w, h, px) in enumerate(tiles32)
                   if labels.get(i) == "sand" for b in px)
    total = sum(hist.values()) or 1
    sand_idx = sorted(i for i, n in hist.items() if n >= total * 0.005)
    sandcols = [_pal_rgb(d2pal, i) for i in sand_idx]
    print("sand ramp: %d of %d indices seen in sand tiles" % (len(sand_idx), len(hist)))

    def subquads(px, w, h, pal, refcols, cap):
        """4 floats: rock fraction of the NW/NE/SW/SE quarters of one tile."""
        out = []
        for qy in range(2):
            for qx in range(2):
                far = tot = 0
                for y in range(qy * h // 2, (qy + 1) * h // 2):
                    for x in range(qx * w // 2, (qx + 1) * w // 2):
                        b = px[y * w + x]
                        if min(_d2(_pal_rgb(pal, b), c) for c in refcols) > cap:
                            far += 1
                        tot += 1
                out.append(far / float(tot))
        return out

    d2sub = []
    for i, (w, h, px) in enumerate(tiles32):
        if labels.get(i) in ("blank", "ice"):
            d2sub.append(None)                 # never pick these
        else:
            d2sub.append(subquads(px, w, h, d2pal, sandcols, 1600))

    dmix = mixshp.MixFile(a.desert_mix)
    dpal = bytes((v << 2) | (v >> 4) for v in dmix.read("DESERT.PAL"))
    gseed = sorted(set(b for _, px in WB.parse_win(dmix.read("CLEAR1.DES"), "CLEAR1")
                       for b in px))
    gcols = [_pal_rgb(dpal, i) for i in gseed]

    def td_profile(ini, tw, th):
        """Sub-quadrant featureness of the TD desert art, 4 values per cell."""
        cells = dict(WB.parse_win(dmix.read(ini + ".DES"), ini))
        prof = []
        for c in range(tw * th):
            px = cells.get(c)
            if px is None:
                prof.extend([0.0, 0.0, 0.0, 0.0])
                continue
            prof.extend(subquads(px, TS, TS, dpal, gcols, 1600))
        return prof

    def win_profile(ids):
        out = []
        for t in ids:
            q = d2sub[t]
            if q is None:
                return None                    # window touches blank/ice: reject
            out.extend(q)
        return out

    dims = template_dims()
    slopes = [(tid, ini) for tid, ini in WB.theater_templates("THEATERF_DESERT")
              if ini.startswith("S") and ini[1].isdigit()]

    RUN_TRIPLES = [("S03", "S04", "S05"), ("S10", "S11", "S12"),
                   ("S17", "S18", "S19"), ("S24", "S25", "S26")]
    triple_members = {t for tri in RUN_TRIPLES for t in tri}

    harvests = {}
    result = {}
    report = []
    for tid, ini in slopes:
        tw, th = dims[ini]
        if (tw, th) not in harvests:
            harvests[(tw, th)] = harvest(maps, tw, th, 800)
        wins, below, right = harvests[(tw, th)]
        tp = td_profile(ini, tw, th)
        tmax = max(tp) or 1.0
        tpn = [v / tmax for v in tp]
        best, bs = None, None
        import math
        for ids, freq in wins.items():
            wp = win_profile(ids)
            if wp is None:
                continue
            if max(wp) < 0.15 and max(tpn) > 0.3:
                continue                       # all-sand window for rocky art: no
            d = sum((x - y) ** 2 for x, y in zip(tpn, wp)) / len(wp)
            sc = d - 0.01 * math.log(freq + 1)
            if bs is None or sc < bs or (sc == bs and ids < best):
                best, bs = ids, sc
        if best is None:
            report.append("%s: NO WINDOW -- left to the previous table" % ini)
            continue
        if ini == "S14":
            # THE RAMP reads as a walkable channel, not a rock checker: the most
            # frequent all-sand window. Its cliff shoulders come from the run caps
            # on either side, same reading as the undressed ramps of other faces.
            sandy = [(ids, f) for ids, f in wins.items()
                     if win_profile(ids) is not None
                     and max(win_profile(ids)) < 0.10]
            if sandy:
                best = max(sandy, key=lambda kv: (kv[1], kv[0]))[0]
        if ini not in triple_members:
            result[ini] = list(best)
            report.append("%s <- %s (freq %d)" % (ini, best, wins[best]))

    # Run triples: three windows that pairwise CHAIN vertically (N-S runs) or
    # horizontally (E-W runs) in the shipped maps.
    for tri in RUN_TRIPLES:
        tw, th = dims[tri[0]]
        wins, below, right = harvests[(tw, th)]
        ns = tri[0] in ("S10", "S24")          # S10/S11/S12, S24/S25/S26 vertical
        chain = below if ns else right
        tp = [td_profile(i, tw, th) for i in tri]
        tmax = max(max(p) for p in tp) or 1.0
        tpn = [[v / tmax for v in p] for p in tp]

        import math
        succ = defaultdict(list)
        for (x, b), n in chain.items():
            if n > 0:
                succ[x].append(b)
        for k in succ:
            succ[k].sort()
        cands = sorted(wins.items(), key=lambda kv: (-kv[1], kv[0]))[:400]

        def fit(ids, prof):
            wp = win_profile(ids)
            if wp is None:
                return 1e9
            if sum(wp) / len(wp) < 0.12:
                return 1e9                     # a run piece is never mostly sand --
                                               # one flat variant punches a visible
                                               # hole in every wall it lands in
            sc = (sum((x - y) ** 2 for x, y in zip(prof, wp)) / len(wp)
                  - 0.01 * math.log(wins[ids] + 1))
            # A wall repeats its variants, so a window the designers tiled against
            # ITSELF joins seamlessly when the resolver repeats it.
            if chain.get((ids, ids), 0) > 0:
                sc -= 0.05
            return sc
        best, bs = None, None
        for ids_a, _ in cands:
            for ids_b in succ.get(ids_a, ()):
                for ids_c in succ.get(ids_b, ()):
                    sc = fit(ids_a, tpn[0]) + fit(ids_b, tpn[1]) + fit(ids_c, tpn[2])
                    key = (ids_a, ids_b, ids_c)
                    if bs is None or sc < bs or (sc == bs and key < best):
                        best, bs = key, sc
        if best is None:
            report.append("TRIPLE %s: no chaining trio found in the maps" % (tri,))
            continue
        for ini, ids in zip(tri, best):
            result[ini] = list(ids)
        report.append("TRIPLE %s <- %s / %s / %s (chained in shipped maps)"
                      % ("/".join(tri), best[0], best[1], best[2]))

    json.dump(result, open(a.out, "w"))
    print("\n".join(report))
    print("wrote %s: %d templates" % (a.out, len(result)))


if __name__ == "__main__":
    main()
