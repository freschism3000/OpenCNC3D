"""G328: Enhanced draws the remastered craters and scorch marks, and they belong to the ground.

usage: gate_decalenh.py SHOTS_DIR PACK FINALS_DIR

Two views, each one frame shot with the remaster on and off and the same two in N64
(gate_decalenh.txt), plus the frame with no decals at all (--nosmudge), which is the ground
the decals lie on:

  tem    SCG05EB, GDI's fifth mission: twenty authored scorch marks and small craters on
         temperate grass round the Nod base.
  des    SCB08EB, Nod's eighth: thirty on the desert sand.

A decal is two layers (game/decalenh_mod.h): a burn, the ground times twice a texel, over the
3 x 3 cells round the decal's cell, and for a crater a soil body over its own cell.

Claims:
  (1) ENGINE STATE: with the dial on, `decaldump` reports the pass drawn from the pack
      (path=enh) at 144 texels a cell on at least 15 cells; off, none; in N64, none.
  (2) THE PACK HOLDS THE FINALS: every crater body is its committed final times the
      theater's gain, every burn is its committed soot through the theater's burn
      (bake_decalenh.soot_texel), each to within 2 levels; the body gutters are transparent
      and the burn gutters neutral.
  (3) NOTHING IS CUT OFF: every burn is neutral (within 3 levels of 128) along the border
      of its 3 x 3 cells, and every crater body is all but transparent (alpha at most 8)
      along its cell's border, so neither ends in a straight line.
  (4) THE GROUND SHOWS THROUGH: inside what the decals change, the fine grain of the frame
      follows the bare ground's (the correlation of the two after a 2-pixel blur is taken
      out) at 0.5 or more. A decal laid over the ground replaces its grain, and the eye reads
      the patch as a sticker: the cartridge's art Enhanced drew before measures 0.23 on the
      grass and 0.12 on the sand.
  (5) NOTHING ELSE MOVES: every pixel more than 6 px from a decal is the same with the
      remaster on and off.
  (6) N64 IS UNTOUCHED: the dial changes no pixel there.
"""
import os
import re
import struct
import sys
import zlib

import numpy as np
from PIL import Image
from scipy import ndimage

d, pack, finals = sys.argv[1], sys.argv[2], sys.argv[3]
VIEWS = ("tem", "des")
W = np.array([0.2126, 0.7152, 0.0722])
bad, notes = [], []


def img(name):
    return np.asarray(Image.open(os.path.join(d, name)).convert("RGB")).astype(np.float64)


def grow(m, n):
    m = m.copy()
    for _ in range(n):
        g = m.copy()
        g[1:] |= m[:-1]; g[:-1] |= m[1:]; g[:, 1:] |= m[:, :-1]; g[:, :-1] |= m[:, 1:]
        m = g
    return m


def grain(a):
    l = a @ W
    return l - ndimage.gaussian_filter(l, 2)


# (2) and (3): the pack against the finals
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                                "tools", "art", "remaster", "decals"))
import bake_decalenh as bk
raw = open(pack, "rb").read()
if raw[:8] != b"DECALEN3":
    bad.append("the pack is not a DECALEN3 pack")
else:
    ver, nth, cell, gut, types, maxf, scell, sspan, sgut = struct.unpack_from("<9I", raw, 8)
    o, slot, ssize = 44, cell + 2 * gut, scell * sspan
    sslot = ssize + 2 * sgut
    frames = 0
    for _ in range(nth):
        nm = raw[o:o + 12].rstrip(b"\0").decode(); o += 12
        tid, bw, bh, sw, sh = struct.unpack_from("<5I", raw, o); o += 20
        counts = struct.unpack_from("<%dI" % types, raw, o); o += 4 * types
        layers = struct.unpack_from("<%dI" % types, raw, o); o += 4 * types
        zb, = struct.unpack_from("<I", raw, o); o += 4
        body = np.frombuffer(zlib.decompress(raw[o:o + zb]), np.uint8).reshape(bh, bw, 4).astype(np.float64); o += zb
        zs, = struct.unpack_from("<I", raw, o); o += 4
        soot = np.frombuffer(zlib.decompress(raw[o:o + zs]), np.uint8).reshape(sh, sw, 3).astype(np.float64); o += zs
        th = next(t[1] for t in bk.THEATERS if t[0] == nm)
        gain = np.array(bk.GAIN[th])
        for row, art in enumerate(bk.TYPES):
            n = counts[row]
            sf = os.path.join(finals, "%s_%s_soot.png" % (th, art))
            bf = os.path.join(finals, "%s_%s.png" % (th, art))
            if not (layers[row] & 2) or not os.path.exists(sf):
                bad.append("%s %s has no burn" % (nm, art))
                continue
            s_fin = np.asarray(Image.open(sf).convert("RGB")).astype(np.float64) / 255.0
            for k in range(n):
                y0, x0 = row * sslot, k * sslot
                tile = soot[y0:y0 + sslot, x0:x0 + sslot]
                inner = tile[sgut:sgut + ssize, sgut:sgut + ssize]
                want = np.clip(bk.soot_texel(s_fin[:, k * ssize:(k + 1) * ssize], th, bk.SOOT[th]) * 255 + 0.5, 0, 255)
                if np.abs(inner - np.floor(want)).max() > 2:
                    bad.append("%s %s frame %d: the burn is not its final" % (nm, art, k))
                gm = np.ones((sslot, sslot), bool); gm[sgut:sgut + ssize, sgut:sgut + ssize] = False
                if np.abs(tile[gm] - 128).max() > 0:
                    bad.append("%s %s frame %d: the burn's gutter is not neutral" % (nm, art, k))
                rim = np.concatenate([inner[0], inner[-1], inner[:, 0], inner[:, -1]])
                if np.abs(rim - 128).max() > 3:
                    bad.append("%s %s frame %d: the burn is cut off at its 3 x 3 border (%d levels off neutral)"
                               % (nm, art, k, np.abs(rim - 128).max()))
                frames += 1
            if layers[row] & 1:
                b_fin = np.asarray(Image.open(bf).convert("RGBA")).astype(np.float64)
                for k in range(n):
                    tile = body[row * slot:(row + 1) * slot, k * slot:(k + 1) * slot]
                    inner = tile[gut:gut + cell, gut:gut + cell]
                    f = b_fin[:, k * cell:(k + 1) * cell]
                    adapted = bk.body_texel(f, th, gain)
                    want = np.floor(adapted)
                    vis = want[..., 3] > 0  # zero-alpha fringe colours are deliberately bled
                    err = np.abs(inner[..., :3] - want[..., :3])[vis].max() if vis.any() else 0
                    if np.abs(inner[..., 3] - want[..., 3]).max() > 0 or err > 2:
                        bad.append("%s %s frame %d: the body is not its final (colour off by %d)" % (nm, art, k, err))
                    gm = np.ones((slot, slot), bool); gm[gut:gut + cell, gut:gut + cell] = False
                    if tile[..., 3][gm].max() > 0:
                        bad.append("%s %s frame %d: the body's gutter is not transparent" % (nm, art, k))
                    rim = np.concatenate([inner[0, :, 3], inner[-1, :, 3], inner[:, 0, 3], inner[:, -1, 3]])
                    if rim.max() > 8:
                        bad.append("%s %s frame %d: the body is cut off at its cell's border (alpha %d)"
                                   % (nm, art, k, rim.max()))
    notes.append("%d frames in the pack match their finals, none cut off" % frames)

for tag in VIEWS:
    log = open(os.path.join(d, "g328_%s.log" % tag), errors="replace").read()
    dumps = re.findall(r"DECALENH\|path=(\w+)\|.*\|enhanced=(\d)\|.*\|cells=(\d+)\|cell=(\d+)", log)
    # (1) on, off, n64 on
    if len(dumps) < 3:
        bad.append("%s: %d decaldump lines (want 3): a binary without the verb" % (tag, len(dumps)))
        continue
    on, off, n64 = dumps[0], dumps[1], dumps[2]
    if on[0] != "enh" or int(on[2]) < 15 or on[3] != "144":
        bad.append("%s: with the dial on the pass drew path=%s on %s cells at %s texels" % (tag, on[0], on[2], on[3]))
    if off[0] != "none" or n64[0] != "none":
        bad.append("%s: the remaster drew with the dial off (%s) or in N64 (%s)" % (tag, off[0], n64[0]))
    ON, OFF, N = img("g328_%s_on.png" % tag), img("g328_%s_off.png" % tag), img("g328_%s_none.png" % tag)
    # the building aprons are the same in on and off and absent from none: not decals
    bib = (np.abs(ON - N).sum(2) > 6) & (np.abs(OFF - N).sum(2) > 6) & (np.abs(ON - OFF).sum(2) < 3)
    # (4)
    m = (np.abs(ON - N).sum(2) > 12) & ~bib
    if m.sum() < 2000:
        bad.append("%s: only %d decal pixels on screen" % (tag, m.sum()))
        continue
    c = np.corrcoef(grain(ON)[m], grain(N)[m])[0, 1]
    mo = (np.abs(OFF - N).sum(2) > 12) & ~bib
    co = np.corrcoef(grain(OFF)[mo], grain(N)[mo])[0, 1] if mo.sum() > 200 else float("nan")
    if c < 0.5:
        bad.append("%s: the ground's grain carries through the decals at %.3f (want 0.5 or more; the cartridge's %.3f)"
                   % (tag, c, co))
    # (5)
    near = grow((np.abs(ON - N).sum(2) > 0) | (np.abs(OFF - N).sum(2) > 0), 6)
    moved = (np.abs(ON - OFF).sum(2) > 0) & ~near
    if moved.sum():
        bad.append("%s: %d pixels away from any decal changed with the dial" % (tag, moved.sum()))
    # (6)
    if not np.array_equal(img("g328_%s_n64on.png" % tag), img("g328_%s_n64off.png" % tag)):
        bad.append("%s: the dial changes the N64 picture" % tag)
    notes.append("%s %s cells, the ground's grain through them at %.3f (the cartridge's %.3f)" % (tag, on[2], c, co))

if bad:
    print("0 " + "; ".join(bad[:8]) + ("; and %d more" % (len(bad) - 8) if len(bad) > 8 else ""))
else:
    print("1 " + "; ".join(notes) + "; nothing else moves; N64 untouched")
