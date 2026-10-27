#!/usr/bin/env python3
"""The Enhanced terrain packs keep every template's border as approved, in the four banks
whose daylight processing is faded at template edges.

    python3 game/gate_terrainedges.py <checkout> [<packs dir>]    prints "1 message" or "0 message"

The packs are the ones game/build.sh writes (tools/bakery/pack_remaster_terrain.py), by
default beside each bank's set in <checkout>/art/remaster/textures/<BANK>/. Red Alert's
snow (RASNOW) and temperate (RATEMP), Tiberian Dawn's winter (WINTER) and the editor's snow
(SNOW). Claims, for every template the daylight processing touched (its MANIFEST rows):

  (1) a shore template (SH...) is its approved painting, every texel (within 1 level);
  (2) any other template's outermost texels, along its edge and along any icon it does not
      have, are its approved painting under the bank's own material pass (the temperate
      rock grade on RATEMP), within EDGE_TOL levels on average;
  (3) every join strip at the template's edge whose authored painting has opacity keeps
      some opacity in the pack (the strips of the painting the template draws: Red Alert's
      temperate cliffs draw Tiberian Dawn's shared cliff material, and its strips).

The daylight processing re-exposed each template on its own and gave the strips at its
edges no opacity, so a processed template met its neighbour on a straight cell-edge line:
a beach cell drew as a dark square, two cliff templates stepped in brightness down their
shared edge. Measured on the packs before the fix: Red Alert temperate 80 templates' edges
off and 181 strips with no opacity, Tiberian Dawn winter 22 shores, 39 edges and 132
strips, the editor's snow 22, 38 and 126; after, none in any bank.
NOT COVERED: how the joins look in the game (the shots taken for the fix, and G362 on Red
Alert's snow); the inside of each template, which keeps the daylight processing.
"""
import json
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

BANKS = ['RASNOW', 'RATEMP', 'WINTER', 'SNOW']
C = 144
EDGE_TOL = 2.0


def tiles(path):
    b = path.read_bytes()
    _, cell, n = struct.unpack_from('<3I', b, 8)
    keys = [struct.unpack_from('<2H', b, 20 + 4 * i) for i in range(n)]
    base = 20 + 4 * n
    return {k: np.frombuffer(b, np.uint8, cell * cell * 3, base + i * cell * cell * 3).reshape(cell, cell, 3)
            for i, k in enumerate(keys)}


def strips(path):
    b = path.read_bytes()
    _, cell, margin, n = struct.unpack_from('<4I', b, 8)
    recs = [struct.unpack_from('<HHBB2x', b, 24 + 8 * i) for i in range(n)]
    off, res = 24 + 8 * n, {}
    for tid, icon, side, _ in recs:
        w, h = (margin, cell) if side == 0 else (cell, margin)
        res[(tid, icon, side)] = np.frombuffer(b, np.uint8, w * h * 4, off).reshape(h, w, 4)
        off += w * h * 4
    return res


def check(root, packs):
    sys.path.insert(0, str(root / 'tools/bakery'))
    import terrain_daylight as T
    report, ok = [], True
    for bank in BANKS:
        d = packs if packs else root / 'art/remaster/textures' / bank
        enh, seam = d / f'terrainenh_{bank}.pack', d / f'terrainseam_{bank}.pack'
        if not enh.exists() or not seam.exists():
            return '0 %s: no built packs at %s (game/build.sh makes them)' % (bank, d)
        tl, st = tiles(enh), strips(seam)
        sets = {}
        shore_bad = edge_bad = dead = rows = 0
        worst = 0.0
        for r in T.rows(bank):
            if r['variant']:
                continue
            rows += 1
            material = T.surface_source(r)
            w, h = r['cells']
            have, k = T.edge_weight(r)
            got = np.zeros((h * C, w * C, 3), np.float32)
            for icon in r['have']:
                y, x = divmod(icon, w)
                got[y * C:(y + 1) * C, x * C:(x + 1) * C] = tl[(r['tid'], icon)]
            approved = Image.open(root / material['source']).convert('RGB')
            if r['name'].startswith('SH'):
                want = np.asarray(Image.open(root / r['source']).convert('RGB'), np.float32)
                if np.abs(got - want)[have].max() > 1:
                    shore_bad += 1
            else:
                want = np.asarray(T.bank_pass(bank, material, approved), np.float32)
                ring = have & (k < 0.002)
                err = float(np.abs(got - want)[ring].mean())
                worst = max(worst, err)
                if err > EDGE_TOL:
                    edge_bad += 1
            # the strips of the painting the template draws (Red Alert's temperate cliffs draw
            # Tiberian Dawn's shared cliff material, and its strips)
            th = material['theater']
            if th not in sets:
                sets[th] = json.loads((root / 'art/remaster/textures' / th / 'set_144.json').read_text())['templates']
            for mar in sets[th][material['name']].get('margins', []):
                cy, cx = divmod(mar['icon'], w)
                outer = (cx == 0) if mar['side'] == 'L' else (cy == 0)
                authored = np.asarray(Image.open(root / 'art/remaster/textures' / th / material['name'] / mar['file']).convert('RGBA'))
                packed = st.get((r['tid'], mar['icon'], 0 if mar['side'] == 'L' else 1))
                if outer and packed is not None and authored[..., 3].any() and not packed[..., 3].any():
                    dead += 1
        bad = shore_bad or edge_bad or dead
        ok = ok and not bad
        report.append('%s %d templates: shores off their painting %d, edges off %d (worst %.1f), strips with no opacity %d'
                      % (bank, rows, shore_bad, edge_bad, worst, dead))
    return ('1 ' if ok else '0 ') + '; '.join(report)


if __name__ == '__main__':
    if len(sys.argv) not in (2, 3):
        print(__doc__)
        sys.exit(2)
    print(check(Path(sys.argv[1]).resolve(), Path(sys.argv[2]) if len(sys.argv) == 3 else None))
