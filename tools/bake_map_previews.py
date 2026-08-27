#!/usr/bin/env python3
"""
bake_map_previews.py -- one thumbnail per skirmish map, baked into `mappreview.pack`
so the lobby can show you the ground before you commit to it, the way the 1995 and
later Westwood lobbies do.

WHY THIS EXISTS

The skirmish flow currently ends in a plain list of map names. A name is not a map:
"Sand Trap" does not say whether there is water between you and the computer, where
the tiberium is, or how many players the thing was cut for. Every one of those facts
is already in the data. This turns them into a picture.

WHAT IS DRAWN, AND WHERE EACH PART COMES FROM

  ground     terrain_<SCEN>.json gives, per cell, the exact patch of the theater
             atlas the game draws there. The preview reads that patch out of the
             atlas PNG, so a preview is made of the same texels as the tactical view
             and no colour here is invented.

  water      A texel with alpha 0 in the atlas is a WATER HOLE: the terrain art has
             a gap and the renderer supplies the water surface underneath. In the
             tactical view that surface is two scrolling cartridge textures, which is
             not a colour at all, so the preview paints the same flat (28,51,74) the
             in-game radar paints a water cell with. A lobby preview and the radar are
             the same map at the same scale and they should agree.

  tiberium   The INI's [OVERLAY] section names TI1..TI12 per cell. The SHAPE comes
             from the real overlay art in dostib.pack, at the same density frame the
             engine would pick: neighbour count -> {0,1,3,4,6,7,8,10,11}, the engine's
             own Tiberium_Adjust table. So a thin field covers a fifth of its cells'
             texels and a thick one covers half, and the downsample turns that into a
             faint tint or a solid slab exactly in proportion.
             The COLOUR is a legend green, not the art's own. The cartridge's tiberium
             is olive (92,106,67 at full density), which against temperate grass
             (48,92,40) is a change of about six per cent per channel: composited
             faithfully it is invisible at this size, and a preview that hides where
             the money is has failed at the one job a lobby thumbnail has. So the mask
             is the cartridge's and the fill is a legend colour, the way the in-game
             radar paints an object in a GUI ramp colour rather than its own. Green is
             reserved for it: no start marker is allowed a green.

  starts     The INI's [Waypoints]. Waypoints 0..25 that are not -1, COMPACTED in
             ascending order, are the legal start positions: the engine builds that
             compacted array and indexes it with the player's start location without
             a bound check, so the count is per-map and it is not always 8. Waypoints
             26 and 27 are the home cell and the reinforcement cell, not starts, and
             are excluded. Each start gets a diamond in its own colour, and the pack
             also carries the pixel coordinates so a lobby that wants to colour them
             by who actually took the slot can draw its own.

  NOT drawn  [TERRAIN] (trees, rocks), [SMUDGE], [STRUCTURES], [UNITS], [INFANTRY].
             Their art is not part of this bake, and a made-up blob standing in for a
             tree would be an invented fact on a picture whose whole value is that it
             is not invented. Blocking terrain is therefore missing from the preview;
             that is a known gap, not an oversight.

THE SIZE, AND WHY IT IS 120x120

The screen this is drawn on is 320x200. A lobby needs a map list, a side plate and
buttons on the same screen, so the preview panel gets at most a third of the width.
Inset 8 pixels and 4 clear of the panel edge, a 120 wide preview leaves 150 columns
for a ten-row list and still 80 rows under the panel for the buttons; --proof draws
that layout at 1:1 rather than asserting it.

It is also enough resolution to be worth having. The nine retail maps measure 56x49
to 62x62 cells, so the frame gives between 1.94 and 2.45 pixels per cell: a tiberium
field of a dozen cells is a 25 pixel blob and the smallest lake still has shape. One
pixel per cell would fit a whole 64 cell map in 64 pixels and lose all of that.

The frame is SQUARE and fixed, so the caller blits one rectangle whatever map is
selected. Maps are not square, so the map is fitted inside it with its aspect kept and
the leftover margin filled with palette index 0, which the DOS shape blitter treats as
transparent (dosbar.h DB_TBLACK). Blit the frame over the dialog and the margin simply
does not paint. The record also carries the sub-rectangle the map really occupies, so
a caller that wants a border tight against the map can draw one.

THE PALETTE

The lobby draws into an 8-bit surface, so the previews are 8-bit indices, not RGB.
The palette is the one the menu runs on: the 256 entries embedded in TITLE.CPS, the
same ones bake_dosmenu.py puts in dosmenu.pack, so a preview blits into a menu plate
with no palette change and no remap. Quantisation is the renderer's own rule
(cnc_eyes.cpp pal_index): nearest entry in RGB, over a 5:5:5 cube, with index 0
excluded because it is the transparent one. No preview pixel is ever index 0 except
the margin, and the self-check asserts that.

INPUTS, and every one of them is either tracked or regenerable by a named command

  game/missions/<SCEN>.INI                          tracked
  tools/bakery/sharecopy/assets/terrain/
      terrain_<SCEN>.json                           sh tools/stage-skirmish-maps.sh
      TEMPERAT_tiles.png / DESERT_tiles.png         tracked
  data/dosdata/LOCAL.MIX  (TITLE.CPS palette)       tracked
  game/dostib.pack or playable/dostib.pack          python3 game/bake_dostiberium.py

Missing any of them stops the run with the command that makes it, rather than baking
a grey rectangle and calling it a fallback.

OUTPUT

  game/mappreview.pack            the previews
  game/mappreview.manifest.json   what went into them, for anyone reading rather than
                                  blitting. Both land in game/ because that is where
                                  the standalone bakers write, and the build's pack
                                  refresh scans that directory: the pack reaches the
                                  play folder without any script learning its name.

FORMAT (all little-endian), magic "MAPPREV1":

    char  magic[8]        "MAPPREV1"
    u32   version         1
    u32   count           number of maps in the pack
    u32   frame_w         120
    u32   frame_h         120
    u32   max_starts      26   (slots reserved in EVERY record, so records are fixed
                                size and the n-th one is at a computable offset)
    u8    pal6[768]       TITLE.CPS's palette, 6-bit DAC values, as it ships in the CPS
    u8    pal8[768]       the same widened with v << 2, i.e. what an index resolves to
    count x {
        char  scenario[12]      "SCM01EA", NUL padded
        char  name[24]          the INI [Basic] Name=, NUL padded
        char  theater[12]       "TEMPERATE" / "DESERT", NUL padded
        u16   cell_x, cell_y, cell_w, cell_h    the INI [MAP] playable rect, in cells
        u16   img_x, img_y, img_w, img_h        where the map sits inside the frame
        u16   starts                            legal start positions, 0..max_starts
        u16   tiberium                          cells carrying a tiberium overlay
        max_starts x {
            u16 cell_x, cell_y                  map cell of this start
            u16 px, py                          its centre in frame pixels
            u8  colour                          palette index its diamond is drawn in
            u8  reserved                        0
        }                                       slots past `starts` are all zero
        u8    plain [frame_w*frame_h]           terrain only, row major, top row first
        u8    marked[frame_w*frame_h]           the same with the start diamonds on it
    }

    Record size is 12+24+12+8+8+4 + max_starts*10 + 2*frame_w*frame_h, fixed.

    Two planes because there are two kinds of caller. A lobby that just wants a
    picture blits `marked` and is done. A lobby that colours each start by who took
    the slot blits `plain` and draws its own markers at the recorded px,py.

THE PROOF THAT THE GROUND IS THE GAME'S GROUND

`--verify` is not an opinion either. n64_terrain.py writes, beside each terrain JSON it
resolves, a terrain_<SCEN>.png: its own 64x64 by 24 pixel render of that map. Four of
those are tracked, from both theaters, and running the map pipeline produces more.
--verify finds every one that is present, rebuilds it with the code in this file, and
compares texel by texel. On a checkout with the nine skirmish maps resolved:

    OPAQUE TEXELS IDENTICAL, 13 scenarios, 29192890 of 29192890 (100.0000%)

The only texels excluded are the water holes, and they are excluded because the two
renders disagree about one unit of green there and nowhere else: this file paints the
(28,51,74) the in-game radar paints a water cell with, n64_terrain.py paints its own
documented placeholder (28,52,74). Neither is sampled from the cartridge's water tiles,
which are an animated textured surface and not a colour at all; the radar's value is
used here because a lobby preview and the radar showing the same map should agree.

USAGE

    python3 tools/bake_map_previews.py                     # all nine, to the pack below
    python3 tools/bake_map_previews.py SCM02EA SCM07EA     # a subset
    python3 tools/bake_map_previews.py --png /tmp/prev     # also plain PNGs, 1:1 and 4:1
    python3 tools/bake_map_previews.py --proof /tmp/sheet.png   # a labelled contact sheet
    python3 tools/bake_map_previews.py --verify            # the fidelity check, alone
"""

import argparse
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "menu", "tools"))

MISSIONS = os.path.join(ROOT, "game", "missions")
TERRAIN = os.path.join(ROOT, "tools", "bakery", "sharecopy", "assets", "terrain")
DOSDATA = os.path.join(ROOT, "data", "dosdata")
OUT = os.path.join(ROOT, "game", "mappreview.pack")

MAGIC = b"MAPPREV1"
VERSION = 1
FRAME_W = 120
FRAME_H = 120
MAX_STARTS = 26          # the size of the engine's own compacted waypoint array

CELL_PX = 24             # the atlas is 24 texels per cell, display.h ICON_PIXEL_W

# The nine retail multiplayer scenarios off the 1995 disc, in disc order.
DEFAULT_SCENARIOS = ["SCM%02dEA" % n for n in range(1, 10)]

# cnc_eyes.cpp: the flat colour the in-game radar paints a water cell with, and the
# fallback mean colour it gives a cell that is nothing but hole. Reused here so the
# lobby preview and the radar are the same picture of the same map.
WATER_RGB = (28, 51, 74)

# cell.cpp Tiberium_Adjust: OverlayData (the frame index) from the number of the eight
# neighbouring cells that also carry tiberium.
TIB_ADJ = (0, 1, 3, 4, 6, 7, 8, 10, 11)

# The legend colour tiberium is painted in. It is palette entry 4 exactly, so an
# undiluted tiberium texel quantises back to itself with no error and only the
# downsample's blend with the ground moves it.
TIB_RGB = (84, 252, 84)

# How the start colours are chosen out of the menu palette. TITLE.CPS is the title
# screen's own palette: fire, sky and desert, so it has plenty of reds, oranges,
# yellows and warm greys, exactly one cyan, one strong green ramp, and NO blue and no
# magenta at all. Asking it for "the six Westwood house colours" gets three washed-out
# greys, so the set is derived from what is actually there instead of imposed.
#
#   seeds        the four the palette does well, in the order a player expects
#   candidates   bright enough to read against dark water; never green-dominant,
#                because green is the tiberium legend and a green diamond in the
#                middle of a green field is not a marker; and never within
#                START_GROUND_CLEARANCE of a theater's mean ground colour, measured
#                off the theater atlases themselves. Without that last rule the
#                selection happily hands out (148,112,96), which is 34 from the desert
#                atlas's mean (133,96,70): a marker the colour of the ground it stands
#                on is not a marker.
#   the rest     greedy farthest-point: each new colour is the candidate whose
#                nearest already-chosen colour is furthest away
START_SEEDS = [(5, "yellow"), (127, "red"), (15, "white"), (2, "cyan")]
START_COUNT = 10               # SCM07EA has ten legal start positions
START_MIN_LUMA = 70
START_MIN_SEPARATION = 75      # euclidean in RGB; the bake stops if the set is closer
START_GROUND_CLEARANCE = 90    # euclidean in RGB from each theater's mean ground

# The start diamond: |dx|+|dy| <= 3 painted in opaque black, then |dx|+|dy| <= 2 in the
# slot colour. 7 pixels across, about three and a half cells, which is the smallest
# marker that still reads as a deliberate mark rather than a speck of terrain.
MARK_RIM = 3
MARK_FILL = 2


def die(msg):
    raise SystemExit("bake_map_previews: " + msg)


# ---------------------------------------------------------------------------
# the menu palette
# ---------------------------------------------------------------------------

def load_menu_palette():
    """The 256 entries embedded in TITLE.CPS, the palette the whole DOS menu runs on.

    Read the same way bake_dosmenu.py reads it, from the same file, so a preview and
    the plate it is drawn on cannot disagree about what an index means."""
    try:
        from mixshp import MixSet
    except ImportError as exc:
        die("cannot import menu/tools/mixshp.py (%s)" % exc)
    local = os.path.join(DOSDATA, "LOCAL.MIX")
    if not os.path.exists(local):
        die("no LOCAL.MIX at %s. It carries TITLE.CPS, whose embedded palette is the "
            "one the menu draws in; there is no substitute for it." % local)
    mixes = MixSet(DOSDATA, ["LOCAL.MIX"])
    data = mixes.read("TITLE.CPS")
    if not data:
        die("TITLE.CPS is not in %s" % local)
    _size, _comp, _uncomp, palsize = struct.unpack_from("<HHIH", data, 0)
    if palsize != 768:
        die("TITLE.CPS carries a %d byte palette, expected 768" % palsize)
    pal6 = bytes(data[10:10 + 768])
    if max(pal6) > 63:
        die("TITLE.CPS palette is not 6-bit (max %d)" % max(pal6))
    # video_sdl2.cpp:653 -- the engine widens a 6-bit DAC value with v << 2.
    pal8 = bytes(min(252, v << 2) for v in pal6)
    return pal6, pal8


def build_pal_lut(pal8):
    """cnc_eyes.cpp build_pal_lut, verbatim: nearest palette entry for every cell of a
    5:5:5 cube, with index 0 excluded because it is the transparent one."""
    import numpy as np
    idx = np.arange(32768)
    r = ((idx >> 10) & 31) * 255 // 31
    g = ((idx >> 5) & 31) * 255 // 31
    b = (idx & 31) * 255 // 31
    pal = np.frombuffer(pal8, dtype=np.uint8).reshape(256, 3).astype(np.int32)
    best = np.zeros(32768, dtype=np.uint8)
    bestd = np.full(32768, 1 << 30, dtype=np.int64)
    for c in range(1, 256):
        d = ((r - pal[c, 0]) ** 2 + (g - pal[c, 1]) ** 2 + (b - pal[c, 2]) ** 2)
        hit = d < bestd
        bestd = np.where(hit, d, bestd)
        best = np.where(hit, np.uint8(c), best)
    return best


def quantise(rgb, lut):
    """RGB array (h, w, 3) -> palette indices (h, w), by cnc_eyes.cpp pal_index."""
    import numpy as np
    a = rgb.astype(np.uint16)
    key = ((a[:, :, 0] >> 3).astype(np.int32) << 10) \
        | ((a[:, :, 1] >> 3).astype(np.int32) << 5) \
        | (a[:, :, 2] >> 3).astype(np.int32)
    return lut[key]


def nearest_index(rgb, lut):
    r, g, b = rgb
    return int(lut[((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)])


def ground_colours():
    """The mean opaque colour of every theater atlas in the terrain directory. This is
    what "the ground" is, measured rather than guessed, and it is what a start marker
    has to stand out from."""
    import numpy as np
    from PIL import Image
    out = {}
    for f in sorted(os.listdir(TERRAIN)):
        if not f.endswith("_tiles.png"):
            continue
        a = np.asarray(Image.open(os.path.join(TERRAIN, f)).convert("RGBA"))
        op = a[a[:, :, 3] >= 128][:, :3].astype(float)
        if not len(op):
            die("%s has no opaque texels at all" % f)
        out[f[:-len("_tiles.png")]] = [int(round(v)) for v in op.mean(axis=0)]
    if not out:
        die("no <THEATER>_tiles.png in %s" % TERRAIN)
    return out


def pick_start_colours(pal8, grounds, count):
    """The start-marker colours, derived from the palette rather than wished for.

    Seeded farthest-point selection over the entries that are bright enough to read on
    dark water and are not green (green belongs to tiberium). Returns the seeds first,
    in their fixed order, so start 0 is always yellow and start 1 always red however
    many colours the rest of the run needs.

    `count` is how many the maps in this run actually need, never fewer than
    START_COUNT. Colours are never cycled: two starts a player can pick and cannot tell
    apart is the failure this whole selection exists to avoid, so if the palette cannot
    supply that many at the separation floor, the bake stops."""
    def rgb(i):
        return (pal8[i * 3], pal8[i * 3 + 1], pal8[i * 3 + 2])

    def luma(c):
        return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]

    def d2(a, b):
        return sum((a[k] - b[k]) ** 2 for k in range(3))

    def green_dominant(c):
        r, g, b = c
        return g > r + 24 and g > b + 24

    def clear_of_ground(c):
        return all(d2(c, g) ** 0.5 >= START_GROUND_CLEARANCE for g in grounds.values())

    chosen = [i for i, _ in START_SEEDS]
    labels = {i: lab for i, lab in START_SEEDS}
    cand = [i for i in range(1, 256)
            if luma(rgb(i)) >= START_MIN_LUMA and not green_dominant(rgb(i))
            and clear_of_ground(rgb(i))]
    for i in chosen:
        if green_dominant(rgb(i)):
            die("seed colour %d %r is green, which is reserved for tiberium"
                % (i, rgb(i)))
        if not clear_of_ground(rgb(i)):
            die("seed colour %d %r is within %d of a theater's mean ground %r"
                % (i, rgb(i), START_GROUND_CLEARANCE, grounds))
    while len(chosen) < count:
        best, bestd = None, -1
        for i in cand:
            if i in chosen:
                continue
            m = min(d2(rgb(i), rgb(j)) for j in chosen)
            if m > bestd:
                bestd, best = m, i
        if best is None:
            die("the palette has %d usable start colours and this run needs %d"
                % (len(chosen), count))
        chosen.append(best)
    sep = min(d2(rgb(a), rgb(b)) ** 0.5
              for n, a in enumerate(chosen) for b in chosen[n + 1:])
    if sep < START_MIN_SEPARATION:
        die("the closest two start colours are %.0f apart in RGB, under the %d floor. "
            "Two starts a player cannot tell apart is worse than no colour at all."
            % (sep, START_MIN_SEPARATION))
    return [{"slot": n, "index": i, "rgb": list(rgb(i)),
             "label": labels.get(i, "derived"),
             "clearance_from_ground": {k: int(round(d2(rgb(i), g) ** 0.5))
                                       for k, g in grounds.items()}}
            for n, i in enumerate(chosen)], sep


# ---------------------------------------------------------------------------
# the scenario INI
# ---------------------------------------------------------------------------

def ini_sections(text):
    out = {}
    cur = None
    for line in text.splitlines():
        line = line.rstrip()
        m = re.match(r"^\[([^\]]+)\]", line.strip())
        if m:
            cur = m.group(1).upper()
            out.setdefault(cur, [])
            continue
        if cur is None or not line.strip() or line.strip().startswith(";"):
            continue
        out[cur].append(line.strip())
    return out


def kv(lines):
    out = {}
    for line in lines:
        if "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip()
    return out


def read_ini(scen):
    path = os.path.join(MISSIONS, scen + ".INI")
    if not os.path.exists(path):
        die("no %s. The nine retail skirmish INIs are staged out of GENERAL.MIX by\n"
            "       sh tools/stage-skirmish-maps.sh" % path)
    secs = ini_sections(open(path, errors="replace").read())
    for need in ("MAP", "WAYPOINTS"):
        if need not in secs:
            die("%s.INI has no [%s] section" % (scen, need))

    mp = kv(secs["MAP"])
    try:
        rect = (int(mp["X"]), int(mp["Y"]), int(mp["Width"]), int(mp["Height"]))
    except KeyError as exc:
        die("%s.INI [MAP] has no %s" % (scen, exc))
    theater = mp.get("Theater", "").upper()
    if not theater:
        die("%s.INI [MAP] has no Theater" % scen)

    name = kv(secs.get("BASIC", [])).get("Name", "").strip()
    if not name:
        die("%s.INI [Basic] has no Name; the lobby row and the preview would disagree"
            % scen)

    if not (0 <= rect[0] and 0 <= rect[1] and rect[0] + rect[2] <= 64
            and rect[1] + rect[3] <= 64 and rect[2] > 0 and rect[3] > 0):
        die("%s.INI [MAP] rect %r does not fit the 64x64 cell grid" % (scen, rect))

    wp = {}
    for line in secs["WAYPOINTS"]:
        m = re.match(r"^(\d+)\s*=\s*(-?\d+)\s*$", line)
        if not m:
            die("%s.INI [Waypoints] line %r is not <number>=<cell>" % (scen, line))
        wp[int(m.group(1))] = int(m.group(2))
    # Waypoints 0..25 that are not -1, compacted in ascending order. That compacted
    # array IS the start table the engine indexes; 26 (home) and 27 (reinforcement)
    # are not starts.
    starts = [wp[i] for i in sorted(k for k in wp if 0 <= k < 26) if wp[i] >= 0]
    if not starts:
        die("%s.INI has no valid waypoint below 26, so it has no start positions at all"
            % scen)

    # [OVERLAY] carries walls and crates as well as tiberium; only TI1..TI12 is
    # tiberium and only tiberium is drawn. Anything else is left alone silently
    # because it is not a preview's business, not because it failed to parse.
    tib = {}
    for line in secs.get("OVERLAY", []):
        m = re.match(r"^(\d+)\s*=\s*(\S+)\s*$", line)
        if not m:
            die("%s.INI [OVERLAY] line %r is not <cell>=<name>" % (scen, line))
        o = re.match(r"^TI(\d+)$", m.group(2).upper())
        if o:
            kind = int(o.group(1)) - 1               # TI1 -> kind 0
            if not 0 <= kind < 12:
                die("%s.INI [OVERLAY] names TI%s; the engine has twelve"
                    % (scen, o.group(1)))
            tib[int(m.group(1))] = kind
    return {"scenario": scen, "name": name, "theater": theater, "rect": rect,
            "starts": starts, "tiberium": tib}


# ---------------------------------------------------------------------------
# the terrain
# ---------------------------------------------------------------------------

def read_terrain(scen):
    path = os.path.join(TERRAIN, "terrain_%s.json" % scen)
    if not os.path.exists(path):
        die("no %s.\n"
            "       That file is the resolved terrain, and it is a regenerated\n"
            "       intermediate rather than tracked data. Make it with:\n"
            "           sh tools/stage-skirmish-maps.sh %s" % (path, scen))
    with open(path) as fh:
        t = json.load(fh)
    if t.get("tileSize") != CELL_PX:
        die("%s says tileSize=%s; this bake assumes %d texels per cell"
            % (path, t.get("tileSize"), CELL_PX))
    st = t.get("stats", {})
    if st.get("unmapped"):
        die("%s reports %d unmapped cells. Those draw as clear ground, so a preview "
            "made from it would quietly lie about the map." % (path, st["unmapped"]))
    atlas = os.path.join(TERRAIN, t["atlas"])
    if not os.path.exists(atlas):
        die("no theater atlas at %s" % atlas)
    return t, atlas


def load_tiberium():
    """The tiberium overlay art out of dostib.pack, as 12 kinds x 12 stages of 24x24
    RGBA. Format is bake_dostiberium.py's own, read here with a parser that knows only
    what that file's header comment documents."""
    import numpy as np
    cands = [os.path.join(ROOT, "game", "dostib.pack"),
             os.path.join(ROOT, "playable", "dostib.pack")]
    path = next((p for p in cands if os.path.exists(p)), None)
    if path is None:
        die("no dostib.pack in game/ or playable/. The preview composites the REAL\n"
            "       tiberium art rather than a green rectangle, so it needs that pack:\n"
            "           python3 game/bake_dostiberium.py")
    blob = open(path, "rb").read()
    if blob[:8] != b"DOSTIB1\x00":
        die("%s does not start with the DOSTIB1 magic" % path)
    ver, _src, nsets, fw, fh, types, frames = struct.unpack_from("<7I", blob, 8)
    if ver != 1 or (fw, fh) != (CELL_PX, CELL_PX):
        die("%s is version %d with %dx%d frames; expected version 1, %dx%d"
            % (path, ver, fw, fh, CELL_PX, CELL_PX))
    if nsets < 1:
        die("%s carries no sets" % path)
    o = 8 + 28 + 12                                   # header + the first set's name
    nsheets, texw, texh, _cols, _rows = struct.unpack_from("<5I", blob, o)
    o += 20
    slots = []
    for _ in range(types * frames):
        slots.append(struct.unpack_from("<3H", blob, o))
        o += 6
    sheets = []
    for _ in range(nsheets):
        sheets.append(np.frombuffer(blob, dtype=np.uint8, count=texw * texh * 4,
                                    offset=o).reshape(texh, texw, 4))
        o += texw * texh * 4
    art = {}
    for kind in range(types):
        for stage in range(frames):
            sh, x0, y0 = slots[kind * frames + stage]
            art[(kind, stage)] = sheets[sh][y0:y0 + fh, x0:x0 + fw, :]
    return art, os.path.relpath(path, ROOT), types, frames


def render_full(ini, terr, atlas_path, tib_art):
    """The playable rect at 24 pixels per cell, RGB, water resolved and tiberium
    composited. This is the honest full-resolution picture; the fit to the frame is a
    downsample of it."""
    import numpy as np
    from PIL import Image

    x0, y0, w, h = ini["rect"]
    atlas = np.asarray(Image.open(atlas_path).convert("RGBA"))
    aw, ah = terr["atlasWidth"], terr["atlasHeight"]
    if atlas.shape[1] != aw or atlas.shape[0] != ah:
        die("%s is %dx%d but terrain_%s.json says %dx%d"
            % (atlas_path, atlas.shape[1], atlas.shape[0], ini["scenario"], aw, ah))

    cells = {}
    for c in terr["cells"]:
        cells[(c["x"], c["y"])] = c

    tib = ini["tiberium"]
    tset = set(tib)

    out = np.zeros((h * CELL_PX, w * CELL_PX, 3), dtype=np.uint8)
    holes = 0
    for cy in range(y0, y0 + h):
        for cx in range(x0, x0 + w):
            c = cells.get((cx, cy))
            if c is None:
                die("terrain_%s.json has no cell (%d,%d) but [MAP] says it is inside "
                    "the playable rect" % (ini["scenario"], cx, cy))
            px, py = c["px"], c["py"]
            patch = atlas[py:py + CELL_PX, px:px + CELL_PX, :].astype(np.int32)
            rgb = patch[:, :, :3].copy()
            # A texel with alpha 0 is a water hole: the art has a gap and the renderer
            # paints water under it. Same colour the renderer uses.
            mask = patch[:, :, 3] < 128
            if mask.any():
                holes += 1
                rgb[mask] = WATER_RGB

            cell_no = cy * 64 + cx
            if cell_no in tib:
                n = 0
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        if dx == 0 and dy == 0:
                            continue
                        nx, ny = cx + dx, cy + dy
                        if 0 <= nx < 64 and 0 <= ny < 64 and (ny * 64 + nx) in tset:
                            n += 1
                stage = TIB_ADJ[n]
                frame = tib_art.get((tib[cell_no], stage))
                if frame is None:
                    die("dostib.pack has no frame for kind %d stage %d"
                        % (tib[cell_no], stage))
                # The cartridge's own mask, the legend's colour. See the note at the
                # top of this file for why the art's own olive is not used.
                rgb[frame[:, :, 3] >= 128] = TIB_RGB

            oy = (cy - y0) * CELL_PX
            ox = (cx - x0) * CELL_PX
            out[oy:oy + CELL_PX, ox:ox + CELL_PX, :] = rgb.astype(np.uint8)
    return out, holes


def fit_frame(full, cell_w, cell_h):
    """Fit the full-resolution render into the fixed square frame, aspect kept and
    centred. BOX is an exact area average, which is what a downsample of ground cover
    wants: every source texel contributes to the pixel it lands in and to no other."""
    import numpy as np
    from PIL import Image
    scale = min(FRAME_W / float(cell_w), FRAME_H / float(cell_h))
    iw = max(1, min(FRAME_W, int(round(cell_w * scale))))
    ih = max(1, min(FRAME_H, int(round(cell_h * scale))))
    small = Image.fromarray(full).resize((iw, ih), Image.BOX)
    ix = (FRAME_W - iw) // 2
    iy = (FRAME_H - ih) // 2
    canvas = np.zeros((FRAME_H, FRAME_W, 3), dtype=np.uint8)
    canvas[iy:iy + ih, ix:ix + iw, :] = np.asarray(small)
    return canvas, (ix, iy, iw, ih)


def stamp_start(plane, px, py, colour, black):
    """The diamond, drawn straight into the index plane. Rim first, fill second."""
    for dy in range(-MARK_RIM, MARK_RIM + 1):
        for dx in range(-MARK_RIM, MARK_RIM + 1):
            d = abs(dx) + abs(dy)
            if d > MARK_RIM:
                continue
            x, y = px + dx, py + dy
            if x < 0 or y < 0 or x >= FRAME_W or y >= FRAME_H:
                continue
            plane[y][x] = colour if d <= MARK_FILL else black


# ---------------------------------------------------------------------------
# the pack
# ---------------------------------------------------------------------------

def pack_name(s, n):
    try:
        b = s.encode("ascii")
    except UnicodeEncodeError:
        die("%r is not ASCII; the 8-bit surface has no way to print it" % s)
    if len(b) > n:
        die("%r does not fit in %d bytes" % (s, n))
    return b + b"\x00" * (n - len(b))


RECORD_BYTES = 12 + 24 + 12 + 8 + 8 + 4 + MAX_STARTS * 10 + 2 * FRAME_W * FRAME_H
HEADER_BYTES = 8 + 5 * 4 + 768 + 768


def write_pack(path, pal6, pal8, maps):
    with open(path, "wb") as fh:
        fh.write(MAGIC)
        fh.write(struct.pack("<5I", VERSION, len(maps), FRAME_W, FRAME_H, MAX_STARTS))
        fh.write(pal6)
        fh.write(pal8)
        for m in maps:
            if len(m["starts"]) > MAX_STARTS:
                die("%s has %d start positions and a record reserves %d. Raise "
                    "MAX_STARTS rather than dropping starts a player can pick."
                    % (m["scenario"], len(m["starts"]), MAX_STARTS))
            if len(m["plain"]) != FRAME_W * FRAME_H or \
                    len(m["marked"]) != FRAME_W * FRAME_H:
                die("%s: a plane is not %d bytes" % (m["scenario"], FRAME_W * FRAME_H))
            fh.write(pack_name(m["scenario"], 12))
            fh.write(pack_name(m["name"], 24))
            fh.write(pack_name(m["theater"], 12))
            fh.write(struct.pack("<4H", *m["rect"]))
            fh.write(struct.pack("<4H", *m["img"]))
            fh.write(struct.pack("<2H", len(m["starts"]), m["tiberium"]))
            for i in range(MAX_STARTS):
                if i < len(m["starts"]):
                    cx, cy, px, py, col = m["starts"][i]
                    fh.write(struct.pack("<4H2B", cx, cy, px, py, col, 0))
                else:
                    fh.write(b"\x00" * 10)
            fh.write(m["plain"])
            fh.write(m["marked"])
    return os.path.getsize(path)


def readback(path, maps):
    """Re-read the pack with a parser that knows ONLY the format comment at the top of
    this file, and compare every field and every pixel with what was baked. Three
    separate claims are checked, not one: that the bytes round-trip, that the record
    stride really is fixed (the n-th record is found by arithmetic, not by walking),
    and that the reader ends exactly at the last byte with nothing left over."""
    blob = open(path, "rb").read()
    if blob[:8] != MAGIC:
        die("readback: bad magic")
    ver, count, fw, fh, ms = struct.unpack_from("<5I", blob, 8)
    if (ver, fw, fh, ms) != (VERSION, FRAME_W, FRAME_H, MAX_STARTS):
        die("readback: header is %r" % ((ver, fw, fh, ms),))
    if count != len(maps):
        die("readback: %d records, baked %d" % (count, len(maps)))
    if len(blob) != HEADER_BYTES + count * RECORD_BYTES:
        die("readback: file is %d bytes, format says %d"
            % (len(blob), HEADER_BYTES + count * RECORD_BYTES))
    plane = fw * fh
    for i, m in enumerate(maps):
        o = HEADER_BYTES + i * RECORD_BYTES        # by arithmetic, on purpose
        scen = blob[o:o + 12].split(b"\x00")[0].decode()
        name = blob[o + 12:o + 36].split(b"\x00")[0].decode()
        thr = blob[o + 36:o + 48].split(b"\x00")[0].decode()
        if (scen, name, thr) != (m["scenario"], m["name"], m["theater"]):
            die("readback: record %d is %r, baked %r"
                % (i, (scen, name, thr), (m["scenario"], m["name"], m["theater"])))
        o += 48
        rect = struct.unpack_from("<4H", blob, o); o += 8
        img = struct.unpack_from("<4H", blob, o); o += 8
        ns, ntib = struct.unpack_from("<2H", blob, o); o += 4
        if rect != tuple(m["rect"]) or img != tuple(m["img"]):
            die("readback: %s geometry %r %r" % (scen, rect, img))
        if ns != len(m["starts"]) or ntib != m["tiberium"]:
            die("readback: %s counts %d %d" % (scen, ns, ntib))
        for j in range(ms):
            rec = struct.unpack_from("<4H2B", blob, o); o += 10
            if j < ns:
                if rec[:5] != m["starts"][j] or rec[5] != 0:
                    die("readback: %s start %d is %r" % (scen, j, rec))
            elif any(rec):
                die("readback: %s unused start slot %d is not zero" % (scen, j))
        if blob[o:o + plane] != m["plain"]:
            die("readback: %s plain plane differs" % scen)
        o += plane
        if blob[o:o + plane] != m["marked"]:
            die("readback: %s marked plane differs" % scen)
        o += plane
        if o != HEADER_BYTES + (i + 1) * RECORD_BYTES:
            die("readback: %s record stride is wrong" % scen)
    return count


# ---------------------------------------------------------------------------
# verification
# ---------------------------------------------------------------------------

# n64_terrain.py's own placeholder for a water hole in the proof renders it writes. One
# unit of green away from the radar's colour, which is the whole reason the water texels
# are compared separately instead of being allowed to fail the check silently.
TERRAIN_PROOF_WATER = (28, 52, 74)


def verify():
    """Rebuild every tracked terrain_<SCEN>.png with this file's own atlas reader and
    compare texel by texel. Any scenario with both a tracked JSON and a tracked proof
    render is used; there is no list to go stale."""
    import numpy as np
    from PIL import Image

    scens = sorted(f[len("terrain_"):-len(".json")]
                   for f in os.listdir(TERRAIN)
                   if f.startswith("terrain_") and f.endswith(".json")
                   and os.path.exists(os.path.join(
                       TERRAIN, f[:-len(".json")] + ".png")))
    if not scens:
        die("no terrain_<SCEN>.json with a matching terrain_<SCEN>.png in %s, so there "
            "is nothing to check this render against." % TERRAIN)

    print("verifying the ground against n64_terrain.py's own proof renders")
    total = same = holes = holes_expected = 0
    failed = []
    for scen in scens:
        with open(os.path.join(TERRAIN, "terrain_%s.json" % scen)) as fh:
            t = json.load(fh)
        atlas = np.asarray(Image.open(os.path.join(TERRAIN, t["atlas"]))
                           .convert("RGBA")).astype(np.int32)
        ref = np.asarray(Image.open(os.path.join(TERRAIN, "terrain_%s.png" % scen))
                         .convert("RGB")).astype(np.int32)
        w, h = t["width"], t["height"]
        if ref.shape[:2] != (h * CELL_PX, w * CELL_PX):
            die("terrain_%s.png is %dx%d, expected %dx%d"
                % (scen, ref.shape[1], ref.shape[0], w * CELL_PX, h * CELL_PX))
        mine = np.zeros((h * CELL_PX, w * CELL_PX, 3), dtype=np.int32)
        alpha = np.zeros((h * CELL_PX, w * CELL_PX), dtype=np.int32)
        for c in t["cells"]:
            p = atlas[c["py"]:c["py"] + CELL_PX, c["px"]:c["px"] + CELL_PX]
            y, x = c["y"] * CELL_PX, c["x"] * CELL_PX
            mine[y:y + CELL_PX, x:x + CELL_PX] = p[:, :, :3]
            alpha[y:y + CELL_PX, x:x + CELL_PX] = p[:, :, 3]
        opaque = alpha >= 128
        agree = (mine == ref).all(axis=2)
        n_op = int(opaque.sum())
        n_ok = int((agree & opaque).sum())
        n_hole = int((~opaque).sum())
        n_hole_ok = int((~opaque & (ref == TERRAIN_PROOF_WATER).all(axis=2)).sum())
        total += n_op
        same += n_ok
        holes += n_hole
        holes_expected += n_hole_ok
        flag = "OK  " if n_ok == n_op else "FAIL"
        if n_ok != n_op:
            failed.append(scen)
        print("  %s %-8s %-8s opaque %8d/%8d   holes %7d, %7d of them the "
              "placeholder %s"
              % (flag, scen, t["theater"], n_ok, n_op, n_hole, n_hole_ok,
                 TERRAIN_PROOF_WATER))
    print("  OPAQUE TEXELS IDENTICAL, %d scenarios, %d of %d (%.4f%%)"
          % (len(scens), same, total, 100.0 * same / total if total else 0.0))
    print("  water holes are excluded and accounted for: %d of %d carry n64_terrain.py's"
          % (holes_expected, holes))
    print("  placeholder %s, against the radar's %s that this bake paints."
          % (TERRAIN_PROOF_WATER, WATER_RGB))
    if failed:
        die("the ground does not match on: %s" % ", ".join(failed))
    print("  VERDICT: PASS")


# ---------------------------------------------------------------------------
# proof
# ---------------------------------------------------------------------------

def to_rgb_image(plane, pal8):
    import numpy as np
    from PIL import Image
    a = np.frombuffer(plane, dtype=np.uint8).reshape(FRAME_H, FRAME_W)
    pal = np.frombuffer(pal8, dtype=np.uint8).reshape(256, 3)
    return Image.fromarray(pal[a])


def write_pngs(directory, maps, pal8):
    from PIL import Image
    os.makedirs(directory, exist_ok=True)
    written = []
    for m in maps:
        for suffix, key in (("", "marked"), ("-plain", "plain")):
            p = os.path.join(directory, "%s%s.png" % (m["scenario"], suffix))
            to_rgb_image(m[key], pal8).save(p)
            written.append(p)
        p = os.path.join(directory, "%s@4x.png" % m["scenario"])
        to_rgb_image(m["marked"], pal8).resize(
            (FRAME_W * 4, FRAME_H * 4), Image.NEAREST).save(p)
        written.append(p)
    return written


def footprint_panel(m, pal8, scale=3):
    """The size argument, drawn rather than asserted: one preview at 1:1 inside an
    otherwise empty 320x200 screen, so the space left for the list and the buttons is
    the space you can see."""
    from PIL import Image, ImageDraw
    screen = Image.new("RGB", (320, 200), (24, 28, 24))
    d = ImageDraw.Draw(screen)
    d.rectangle([8, 8, 311, 191], outline=(96, 104, 96))
    px, py = 320 - 8 - 4 - FRAME_W, 8 + 4
    # Blitted the way the DOS shape blitter blits: index 0 does not paint, so the
    # letterbox is the dialog and the border is drawn tight to the map's own rect.
    mask = Image.frombytes("L", (FRAME_W, FRAME_H),
                           bytes(255 if v else 0 for v in m["marked"]))
    screen.paste(to_rgb_image(m["marked"], pal8), (px, py), mask)
    ix, iy, iw, ih = m["img"]
    d.rectangle([px + ix - 1, py + iy - 1, px + ix + iw, py + iy + ih],
                outline=(168, 168, 168))
    for row in range(10):
        y = 16 + row * 12
        d.rectangle([16, y, 16 + 150, y + 8], fill=(40, 48, 40))
    return screen.resize((320 * scale, 200 * scale), Image.NEAREST)


def write_proof(path, maps, pal8, scale=3):
    """LOOK AT IT: every preview at 3:1 nearest, over a checkerboard so the margin's
    index 0 shows up as the transparent thing it is, each labelled with what the record
    says about it, and one panel showing the footprint at 1:1 on a 320x200 screen."""
    from PIL import Image, ImageDraw
    cols = 3
    rows = (len(maps) + cols - 1) // cols + 1
    cw, ch = FRAME_W * scale + 16, FRAME_H * scale + 40
    sheet = Image.new("RGB", (cols * cw, rows * ch), (16, 16, 16))
    d = ImageDraw.Draw(sheet)
    fp = footprint_panel(maps[0], pal8, scale=2)
    sheet.paste(fp, (8, (rows - 1) * ch + 8))
    d.text((8, (rows - 1) * ch + 8 + fp.size[1] + 4),
           "footprint: one 120x120 preview at 1:1 on the whole 320x200 screen "
           "(shown at 2:1), with room left for the list", fill=(230, 230, 230))
    for i, m in enumerate(maps):
        cx, cy = (i % cols) * cw, (i // cols) * ch
        bg = Image.new("RGB", (FRAME_W * scale, FRAME_H * scale))
        for y in range(FRAME_H * scale):
            for x in range(FRAME_W * scale):
                bg.putpixel((x, y), (60, 40, 60) if ((x // 12 + y // 12) % 2 == 0)
                            else (40, 30, 40))
        img = to_rgb_image(m["marked"], pal8).resize(
            (FRAME_W * scale, FRAME_H * scale), Image.NEAREST)
        mask = Image.frombytes("L", (FRAME_W, FRAME_H),
                               bytes(255 if v else 0 for v in m["marked"])).resize(
            (FRAME_W * scale, FRAME_H * scale), Image.NEAREST)
        bg.paste(img, (0, 0), mask)
        sheet.paste(bg, (cx + 8, cy + 8))
        d.text((cx + 8, cy + FRAME_H * scale + 12),
               "%s  %s" % (m["scenario"], m["name"]), fill=(230, 230, 230))
        d.text((cx + 8, cy + FRAME_H * scale + 24),
               "%s  %dx%d cells  %d starts  %d tiberium cells"
               % (m["theater"], m["rect"][2], m["rect"][3],
                  len(m["starts"]), m["tiberium"]), fill=(150, 150, 150))
    sheet.save(path)
    return sheet.size


# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    ap.add_argument("scenarios", nargs="*", default=None,
                    help="scenario codes; default is the nine retail skirmish maps")
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--png", metavar="DIR", help="also write plain PNGs, 1:1 and 4:1")
    ap.add_argument("--proof", metavar="FILE", help="write a labelled contact sheet")
    ap.add_argument("--verify", action="store_true",
                    help="check the ground against n64_terrain.py's proof renders")
    a = ap.parse_args()

    try:
        import numpy  # noqa: F401
        from PIL import Image  # noqa: F401
    except ImportError as exc:
        die("needs numpy and Pillow (%s)" % exc)

    if a.verify:
        # A check and a bake are two different jobs; silently doing only one of them
        # while the flags asked for both is how a green light comes to mean nothing.
        if a.scenarios or a.png or a.proof:
            die("--verify checks the ground and stops. Run it on its own, then run the "
                "bake.")
        verify()
        return

    import numpy as np

    scenarios = a.scenarios or DEFAULT_SCENARIOS

    pal6, pal8 = load_menu_palette()
    lut = build_pal_lut(pal8)
    black = nearest_index((0, 0, 0), lut)

    # Every INI first, so the colour selection knows how many starts this run has to
    # colour before it picks a single one.
    inis = [read_ini(scen) for scen in scenarios]
    need = max(max(len(i["starts"]) for i in inis), START_COUNT)

    grounds = ground_colours()
    colours, separation = pick_start_colours(pal8, grounds, need)
    idxs = [c["index"] for c in colours]
    if black in idxs:
        die("a start colour resolved to the same index as the diamond's rim (%d)" % black)
    tib_index = nearest_index(TIB_RGB, lut)
    if tib_index in idxs:
        die("a start colour is the tiberium legend colour (index %d)" % tib_index)

    tib_art, tib_path, tib_types, tib_frames = load_tiberium()

    maps = []
    for ini in inis:
        scen = ini["scenario"]
        terr, atlas_path = read_terrain(scen)
        if terr["theater"] not in ini["theater"] and \
                ini["theater"] not in terr["theater"]:
            die("%s: the INI says theater %s, terrain_%s.json says %s. Nothing else "
                "cross-checks these two, which is exactly why this does."
                % (scen, ini["theater"], scen, terr["theater"]))

        full, hole_cells = render_full(ini, terr, atlas_path, tib_art)
        rgbframe, img = fit_frame(full, ini["rect"][2], ini["rect"][3])
        plane = quantise(rgbframe, lut)

        ix, iy, iw, ih = img
        # The margin is index 0, which the DOS shape blitter skips. Nothing inside the
        # map may be 0, or a hole would appear in the middle of the picture.
        plane[:iy, :] = 0
        plane[iy + ih:, :] = 0
        plane[:, :ix] = 0
        plane[:, ix + iw:] = 0
        inner = plane[iy:iy + ih, ix:ix + iw]
        if (inner == 0).any():
            die("%s: %d pixels inside the map quantised to index 0, the transparent "
                "one" % (scen, int((inner == 0).sum())))

        plain = plane.tolist()
        marked = [row[:] for row in plain]

        x0, y0, cw, chh = ini["rect"]
        starts = []
        for n, cell in enumerate(ini["starts"]):
            cx, cy = cell % 64, cell // 64
            if not (x0 <= cx < x0 + cw and y0 <= cy < y0 + chh):
                die("%s: start %d is cell (%d,%d), outside the [MAP] rect %r"
                    % (scen, n, cx, cy, ini["rect"]))
            px = ix + int((cx - x0 + 0.5) * iw / float(cw))
            py = iy + int((cy - y0 + 0.5) * ih / float(chh))
            col = colours[n]["index"]
            stamp_start(marked, px, py, col, black)
            starts.append((cx, cy, px, py, col))

        maps.append({
            "scenario": scen, "name": ini["name"], "theater": ini["theater"],
            "rect": list(ini["rect"]), "img": list(img), "starts": starts,
            "tiberium": len(ini["tiberium"]), "hole_cells": hole_cells,
            "plain": np.asarray(plain, dtype=np.uint8).tobytes(),
            "marked": np.asarray(marked, dtype=np.uint8).tobytes(),
        })
        print("  %-8s %-18s %-9s  rect %2d,%2d %2dx%2d  fit %3dx%3d at %2d,%2d  "
              "%2d starts  %4d tiberium  %4d water cells"
              % (scen, ini["name"][:18], ini["theater"], x0, y0, cw, chh,
                 iw, ih, ix, iy, len(starts), len(ini["tiberium"]), hole_cells))

    size = write_pack(a.out, pal6, pal8, maps)
    n = readback(a.out, maps)

    manifest = {
        "pack": os.path.basename(a.out),
        "magic": MAGIC.decode(),
        "version": VERSION,
        "bytes": size,
        "count": n,
        "frame": [FRAME_W, FRAME_H],
        "max_starts": MAX_STARTS,
        "header_bytes": HEADER_BYTES,
        "record_bytes": RECORD_BYTES,
        "palette": "embedded in TITLE.CPS (data/dosdata/LOCAL.MIX), the menu's own",
        "palette_widening": "v << 2",
        "quantiser": "cnc_eyes.cpp pal_index: nearest in RGB over a 5:5:5 cube, "
                     "index 0 excluded",
        "margin_index": 0,
        "water_rgb": list(WATER_RGB),
        "tiberium_mask_source": tib_path,
        "tiberium_shapes": tib_types,
        "tiberium_frames": tib_frames,
        "tiberium_legend_rgb": list(TIB_RGB),
        "tiberium_legend_index": tib_index,
        "start_colours": colours,
        "start_colour_min_separation": round(separation, 1),
        "start_colour_ground_clearance": START_GROUND_CLEARANCE,
        "theater_mean_ground": grounds,
        "marker": {"shape": "diamond", "rim_radius": MARK_RIM,
                   "fill_radius": MARK_FILL, "rim_index": black},
        "not_drawn": ["TERRAIN", "SMUDGE", "STRUCTURES", "UNITS", "INFANTRY"],
        "maps": [{"scenario": m["scenario"], "name": m["name"],
                  "theater": m["theater"], "cell_rect": m["rect"],
                  "image_rect": m["img"], "starts": len(m["starts"]),
                  "start_cells": [[s[0], s[1]] for s in m["starts"]],
                  "tiberium_cells": m["tiberium"],
                  "cells_touching_water": m["hole_cells"]} for m in maps],
    }
    mpath = os.path.splitext(a.out)[0] + ".manifest.json"
    with open(mpath, "w") as fh:
        json.dump(manifest, fh, indent=1, sort_keys=True)

    if a.png:
        w = write_pngs(a.png, maps, pal8)
        print("wrote %d PNGs into %s" % (len(w), a.png))
    if a.proof:
        sz = write_proof(a.proof, maps, pal8)
        print("wrote proof sheet %s (%dx%d)" % (a.proof, sz[0], sz[1]))

    print("wrote %s: %d previews, %d bytes, readback clean" % (a.out, n, size))
    print("wrote %s" % mpath)


if __name__ == "__main__":
    main()
