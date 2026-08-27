#!/usr/bin/env python3
"""
bake_dosinfantry.py -- flatten the 1995 MS-DOS C&C infantry sprites into one
little-endian pack, `dosinfantry.pack`, so the Win98 / Voodoo 2 renderer can swap
its N64 infantry billboards for the DOS art with a minimal diff.

Everything here is transcribed from the GPL engine sources or decoded by the
already-proven SHP decoder (sidebar/tools/mixshp.py, a port of common/lcw.cpp,
common/xordelta.cpp, common/keyframe.cpp). Nothing is guessed.

WHAT THE ENGINE SAYS (citations, all under brain/vanilla/tiberiandawn):

  * Frame selection  -- infantry.cpp:574 InfantryClass::Draw_It:
        shapenum = DoControls[doit].Frame
                 + HumanShape[Facing_To_32(PrimaryFacing)] * DoControls[doit].Jump
                 + Fetch_Stage() % max(DoControls[doit].Count, 1)
    so a strip is FACING-MAJOR with `Count` stages per facing and `Jump` frames
    between facings. Jump == 0 means non-directional (the deaths).
  * Facing order     -- infantry.cpp:84 HumanShape[32]: facenum 0..7 is
    COUNTER-CLOCKWISE from north: N, NW, W, SW, S, SE, E, NE. All 8 facings are
    stored in the DOS art; no mirroring.
  * Per-type tables  -- idata.cpp: MiniGunnerDos (E1), GrenadierDos (E2),
    BazookaDos (E3), FlamethrowerDos (E4), ChemwarriorDos (E5, byte-identical to
    E4's), EngineerDos (INI name "E6", idata.cpp:410 wired to class E7 at :447),
    CommandoDos (RMBO), CivilianDos1..9 + NikoombaDos (C1..C10, all ten verified
    byte-identical), and since MoebiusDos / DelphiDos / DrChanDos
    (MOEBIUS, DELPHI, CHAN -- see NAMED below; they were missing for weeks and drew
    as N64 billboards beside DOS art). Transcription cross-checked: for every SHP the last table
    entry (SALUTE2 / FIRE_DEATH) start+span equals the SHP's frame count exactly
    (E1 508+7*3+3 = 532 = E1.SHP frames, civ 357+18 = 375 = C1.SHP frames).
  * House colours    -- house.cpp:2186 Remap_Table(GAME_NORMAL): GoodGuy units
    draw through RemapGold, BadGuy UNITS through RemapLtBlue (red is buildings
    only), Neutral through RemapNone. const.cpp:274ff: RemapGold and RemapNone
    are both the identity; RemapLtBlue rewrites only indices 176..191 (the gold
    uniform range). So exactly two pixel variants exist: identity and ltblue.
  * Shadow           -- display.cpp:357 UShadowCols {LTGREEN, BLACK, 130}:
    palette index 4 (wwstd.h ColorType, LTGREEN == 4) in unit art is a ghost
    colour: the DOS engine darkens whatever is under it by 130/256. We keep the
    index in the pack but the renderer maps it to alpha 0 (the N64 billboards it
    replaces cast no shadow either); a later pass can blend it without rebaking.
  * Palette          -- read from OUR copy of LOCAL.MIX (verified byte-identical
    to the CD theater mix's TEMPERAT.PAL and to the palette inside the shipped
    dossidebar.pack). NOTE: next/game/content/TEMPERAT.MIX is a REPACKED mix
    whose TEMPERAT.PAL member is NOT the 1995 palette -- never source it there.
    The real TEMPERAT and DESERT palettes differ in 97 entries (terrain ramps),
    but the union of indices actually used by every baked strip was extracted
    and each is byte-identical across the two theaters, so ONE bake serves both
    shipped missions. Widening is v << 2 clamped to 252 (video_ddraw.cpp:910).
  * World scale      -- the mission pack's terrain atlas was measured at exactly
    24.00 texels per cell (PackCell UVs x atlas uw/uh), and the renderer's
    sprite_texels_per_unit() is 24 in CAM_N64. DOS art is drawn at 24 px/cell,
    so baking DOS pixels 1:1 and keeping tpu == 24 puts the art at native world
    scale: an E1 stands ~11 px = 0.46 cells tall, next to the N64 strip's 12 px.

CROP + ANCHOR (measured, not assumed):

  DOS frames are 50x39 with the figure in the middle. Draw_It draws the frame
  SHAPE_CENTERed at (x-2, y+4), which puts frame column 27 on the object's
  ground x. Vertically we anchor the FEET at the ground point: the crop is the
  union of the body bounding boxes (indices other than 0/transparent and
  4/shadow) over all frames of the strip, made symmetric about column 27, and
  the quad's bottom edge is the crop's bottom row. STAND and WALK of the same
  type share a unified bottom row so the feet line cannot jump 1 px between
  standing and walking. Deaths keep their own bottom (the sprawl IS the ground
  contact). The DOS "+4 rows below centre" is a 2.5D projection nicety that a
  true 3D billboard must not replicate.

FORMAT (all little-endian), magic "DOSINF01" version 4:

    char  magic[8]      "DOSINF01"
    u32   version       3
    u32   strip_count
    u32   type_count
    u8    pal6[768]     TEMPERAT.PAL as it sits on the CD (6-bit DAC)
    u8    pal8[768]     v << 2, max 252
    strip_count x {
        char name[24]   e.g. "E1_WALK#0"  (#0 gold/identity row, #1 ltblue row).
                        24 since v3: "MOEBIUS_D_EXPL2#0" is 17 bytes. Diagnostic
                        only -- the renderer resolves strips by index, never name.
        u32  frames, facings, stages, fw, fh
        u32  cols, texw, texh    frame f sits at grid (f%cols, f/cols); texw/texh
                                 are power-of-two and <= 256 (Voodoo 2 limit)
        u32  src_stages          the ENGINE's DoInfoStruct Count for this anim.
                                 == stages unless the strip had to be evenly
                                 subsampled to fit the Voodoo 2 texture; the
                                 renderer maps the engine's Fetch_Stage() through
                                 stage = (dostage % src_stages) * stages / src_stages
        u8   idx[texw*texh]      8-bit palette indices, house remap ALREADY
                                 applied; 0 = transparent, 4 = shadow ghost
    }
    type_count x {
        char ini[8]
        i32  strip[2][15]   house rows GOLD, LTBLUE; anim slots STAND, WALK,
                            D_GUN, D_EXPL, D_EXPL2, D_GREN, D_FIRE, FIRE,
                            PRONE, FIRE_PRONE, LIE_DOWN, CRAWL, GET_UP;
                            -1 = none (E6 the engineer has no FIRE rows).
                            Civilians point both rows at the same strips.
    }

Death slot <- engine DoType (defines.h:1523): 22 D_GUN, 23 D_EXPL, 24 D_EXPL2,
25 D_GREN, 26 D_FIRE (E6 is the one type whose EXPL2 art differs from EXPL).
The live-combat slots map onto the engine's own DoTypes, read per tick off the
OBJ dump: 4 FIRE (standing DO_FIRE_WEAPON), 2 PRONE, 8 FIRE_PRONE, 5 LIE_DOWN,
6 CRAWL, 7 GET_UP; the pose starts and stops exactly when the engine says so.
Version 2 appended all of them after the deaths so the slot order of version 1
is a strict prefix.
"""

import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, ".."))
# mixshp used to be reached through a temporary scratch tree ("../../sidebar/tools"),
# which stopped existing when the project moved into the repo. Look in the repo's own
# tool folders and say which ones were tried if it is genuinely absent.
_CAND = [os.path.join(REPO, "menu", "tools"),
         os.path.join(REPO, "sidebar", "tools"),
         os.path.join(os.path.dirname(REPO), "sidebar", "tools")]
for _d in _CAND:
    if os.path.isfile(os.path.join(_d, "mixshp.py")):
        sys.path.insert(0, _d)
        break
else:
    raise SystemExit("bake_dosinfantry: cannot find mixshp.py in any of %s" % _CAND)

from mixshp import MixFile, Shape  # noqa: E402

# The 1995 CD archives live in the repo's data/ tree.
_DOSDATA = os.path.join(REPO, "data", "dosdata")
if not os.path.isdir(_DOSDATA):
    _DOSDATA = os.path.join(HERE, "dosdata")
CONQUER = os.path.join(_DOSDATA, "CONQUER.MIX")
# The 1995 palette, from our own LOCAL.MIX copy (see the docstring: the game
# tree's content/TEMPERAT.MIX is repacked and carries a DIFFERENT palette).
LOCAL = os.path.join(_DOSDATA, "LOCAL.MIX")
OUT = os.path.join(HERE, "dosinfantry.pack")

MAGIC = b"DOSINF01"
# v3 widened the per-strip NAME field from 16 to 24 bytes. Adding the
# three named characters produced "MOEBIUS_D_EXPL2#0", which is 17 bytes, and the
# assert in pack_name caught it rather than truncating. The name is diagnostic only
# -- the renderer resolves strips by INDEX out of the type table and never by name --
# so this could have been solved by abbreviating, but a field too small to hold the
# thing it names is a trap for the next type someone adds, and the loader rejects a
# wrong version loudly, so widening costs nothing. dosinfantry.pack is regenerated
# per build and never committed, so there is no old pack to be compatible with.
# v4 widened the per-type slot table from 13 to 15 for IDLE1/IDLE2. The
# reader indexes that table by a COMPILE-TIME count, so a v3 pack read by a v4 build would
# walk off the end of each type into the next one's data -- silently, for the types that
# happen to stay in range. The version gate is what makes that a loud failure instead.
VERSION = 4
NAME_BYTES = 24

TRANSPARENT, SHADOW = 0, 4
ANCHOR_COL = 27          # frame column that Draw_It lands on the object's x
MAX_TEX = 256            # Voodoo 2 texture limit, and POT below

# ---------------------------------------------------------------------------
# idata.cpp DO-table rows we bake: (Frame, Count, Jump) exactly as in source.
# Slots: STAND, WALK, D_GUN, D_EXPL, D_EXPL2, D_GREN, D_FIRE.
# ---------------------------------------------------------------------------

# v2 appends the whole live ground-combat family from idata.cpp: FIRE is the
# standing DO_FIRE_WEAPON row, then PRONE, FIRE_PRONE, LIE_DOWN, CRAWL, GET_UP
# (the engine drops a man under fire prone: doing walks 5 -> 2/8 -> 7 live, all
# exported per tick, so the renderer can follow every step). None = the type has
# no such art (EngineerDos FIRE and FIRE_PRONE rows are 0,0,0). Civilian PRONE /
# LIE_DOWN / GET_UP are the engine's own N/A rows (0,1,1 = the stand frame) and
# bake to a tiny 8-frame strip; their CRAWL (8,6,6) is the DOS scared-run row.
# The subsample guard below only kicks in when a row overflows 256x256.
COMBAT = {
    #        STAND      WALK        D_GUN      D_EXPL     D_EXPL2    D_GREN      D_FIRE      FIRE          PRONE         FIRE_PRONE     LIE_DOWN     CRAWL        GET_UP
    "E1":   [(0, 1, 1), (16, 6, 6), (382, 8, 0), (398, 8, 0), (398, 8, 0), (406, 12, 0), (418, 18, 0), (64, 8, 8),   (192, 1, 8),  (192, 6, 8),   (128, 2, 2), (144, 4, 4), (176, 2, 2), (256, 16, 0), (272, 16, 0)],
    "E2":   [(0, 1, 1), (16, 6, 6), (510, 8, 0), (526, 8, 0), (526, 8, 0), (534, 12, 0), (546, 18, 0), (64, 20, 20), (288, 1, 12), (288, 8, 12),  (224, 2, 2), (240, 4, 4), (272, 2, 2), (384, 16, 0), (400, 16, 0)],
    "E3":   [(0, 1, 1), (16, 6, 6), (398, 8, 0), (414, 8, 0), (414, 8, 0), (422, 12, 0), (434, 18, 0), (64, 8, 8),   (192, 1, 10), (192, 10, 10), (128, 2, 2), (144, 4, 4), (176, 2, 2), (272, 16, 0), (288, 16, 0)],
    "E4":   [(0, 1, 1), (16, 6, 6), (510, 8, 0), (526, 8, 0), (526, 8, 0), (534, 12, 0), (546, 18, 0), (64, 16, 16), (256, 1, 16), (256, 16, 16), (192, 2, 2), (208, 4, 4), (240, 2, 2), (384, 16, 0), (400, 16, 0)],
    "E5":   [(0, 1, 1), (16, 6, 6), (510, 8, 0), (526, 8, 0), (526, 8, 0), (534, 12, 0), (546, 18, 0), (64, 16, 16), (256, 1, 16), (256, 16, 16), (192, 2, 2), (208, 4, 4), (240, 2, 2), (384, 16, 0), (400, 16, 0)],
    "E6":   [(0, 1, 1), (16, 6, 6), (146, 8, 0), (154, 8, 0), (162, 8, 0), (170, 12, 0), (182, 18, 0), None,         (82, 1, 4),   None,          (67, 2, 2),  (82, 4, 4),  (114, 2, 2), (130, 16, 0), None],
    "RMBO": [(0, 1, 1), (16, 6, 6), (318, 8, 0), (334, 8, 0), (334, 8, 0), (342, 12, 0), (354, 18, 0), (64, 4, 4),   (160, 1, 4),  (160, 4, 4),   (96, 2, 2),  (112, 4, 4), (144, 2, 2), (192, 16, 0), (208, 16, 0)],
}
CIVILIAN_DO = [(0, 1, 1), (56, 6, 6), (329, 8, 0), (337, 8, 0), (337, 8, 0), (345, 12, 0), (357, 18, 0), (205, 4, 4),
               (0, 1, 1), (205, 4, 4), (0, 1, 1), (8, 6, 6), (0, 1, 1), (189, 10, 0), (199, 6, 0)]
CIVILIANS = ["C1", "C2", "C3", "C4", "C5", "C6", "C7", "C8", "C9", "C10"]

# ---------------------------------------------------------------------------
# THE THREE NAMED CHARACTERS -- added, and they were a real defect.
#
# The engine's InfantryType enum has TWENTY types; this baker covered SEVENTEEN.
# MOEBIUS, DELPHI and CHAN had no DOS strips, and because draw_sprite in
# cnc_eyes.cpp falls THROUGH to the N64 billboards for any type the DOS pack does
# not know, they drew as cartridge art standing next to DOS art in the same frame,
# with no warning at all: the "unknown infantry type" counter never fires for them
# because the N64 pack DOES know them. Silent, and visible on screen.
#
# They are not obscure. All three appear in the campaign we ship:
#   MOEBIUS  SCG08EB, SCG12EA, SCG12EB
#   DELPHI   SCG11EA
#   CHAN     SCB10EA
# and MOEBIUS.SHP / DELPHI.SHP / CHAN.SHP are all present in CONQUER.MIX.
#
# Their tables, read out of idata.cpp rather than assumed:
#   DelphiDos  is BYTE-IDENTICAL to CivilianDos1, so DELPHI reuses CIVILIAN_DO.
#   MoebiusDos and DrChanDos are byte-identical to EACH OTHER and different from
#              the civilians: their SHPs are shorter, so the death rows sit at
#              212/220/228/240 rather than 329/337/345/357, and DO_FIRE_PRONE is
#              (0,0,0) -- no prone-fire art at all, hence the None below.
# Every one of those numbers is checked against idata.cpp at bake time by
# verify_against_idata(); none of it is taken on trust.
NAMED_DO = [(0, 1, 1), (56, 6, 6), (212, 8, 0), (220, 8, 0), (228, 12, 0), (228, 12, 0), (240, 18, 0), (205, 4, 4),
            (0, 1, 1), None, (0, 1, 1), (8, 6, 6), (0, 1, 1), (104, 16, 0), (120, 20, 0)]
NAMED = [("MOEBIUS", NAMED_DO), ("CHAN", NAMED_DO), ("DELPHI", CIVILIAN_DO)]

# IDLE1 / IDLE2 appended. They were the two live Dos this baker never took:
# every type has them, they are 16 frames each (10 and 6 for the civilians), and the 1995
# engine plays them out of the guard state machine with a rate path of their own that no
# other Do gets. Engineer IDLE2 and nothing else is (0,0,0) in idata.cpp, hence its None.
SLOT_NAMES = ["STAND", "WALK", "D_GUN", "D_EXPL", "D_EXPL2", "D_GREN", "D_FIRE", "FIRE",
              "PRONE", "FPRONE", "LIEDOWN", "CRAWL", "GETUP", "IDLE1", "IDLE2"]

# Which row of idata.cpp's DO_COUNT table each of our 13 slots is. Verified against
# all seventeen pre-existing types with ZERO mismatches before the three new ones
# were added, which is the only reason the new tables above can be trusted.
IDATA_SLOT_ROW = [0, 3, 22, 23, 24, 25, 26, 4, 2, 8, 5, 6, 7, 9, 10]

# idata.cpp variable name for every type we bake.
IDATA_TABLE_NAME = {
    "E1": "MiniGunnerDos", "E2": "GrenadierDos", "E3": "BazookaDos",
    "E4": "FlamethrowerDos", "E5": "ChemwarriorDos", "E6": "EngineerDos",
    "RMBO": "CommandoDos",
    "C1": "CivilianDos1", "C2": "CivilianDos2", "C3": "CivilianDos3",
    "C4": "CivilianDos4", "C5": "CivilianDos5", "C6": "CivilianDos6",
    "C7": "CivilianDos7", "C8": "CivilianDos8", "C9": "CivilianDos9",
    "C10": "NikoombaDos",
    "MOEBIUS": "MoebiusDos", "DELPHI": "DelphiDos", "CHAN": "DrChanDos",
}


def verify_against_idata(all_tables):
    """Check every hand-transcribed DO row against the GPL source it came from.

    The tables above are TRANSCRIPTIONS, and a transcription is exactly the kind of
    thing that is right when written and wrong three edits later. A wrong row here
    does not crash: it plays the wrong frames, or an off-by-one death animation,
    which nobody notices. So the source of truth is parsed and every number
    compared.

    If brain/vanilla is not checked out this SAYS so and skips, rather than passing
    quietly -- a check that silently does nothing is worse than no check, and this
    project has shipped several.
    """
    import re
    path = os.path.normpath(os.path.join(HERE, "..", "brain", "vanilla",
                                         "tiberiandawn", "idata.cpp"))
    if not os.path.isfile(path):
        sys.stderr.write("bake_dosinfantry: SKIPPING the idata.cpp cross-check -- %s is "
                         "not here. The DO tables are UNVERIFIED transcriptions in this "
                         "run.\n" % path)
        return 0
    src = open(path, errors="ignore").read()

    def table(name):
        m = re.search(r"int\s+%s\[DO_COUNT\]\[3\]\s*=\s*\{(.*?)\n\};" % name, src, re.S)
        if not m:
            return None
        body = re.sub(r"//[^\n]*", "", m.group(1))
        body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
        n = [int(x) for x in re.findall(r"-?\d+", body)]
        return [tuple(n[i:i + 3]) for i in range(0, len(n), 3)]

    checked = bad = 0
    for ini, rows in all_tables:
        sname = IDATA_TABLE_NAME.get(ini)
        t = table(sname) if sname else None
        if t is None:
            sys.stderr.write("bake_dosinfantry: no idata.cpp table for %s (%s)\n"
                             % (ini, sname))
            bad += 1
            continue
        for k, row in enumerate(IDATA_SLOT_ROW):
            want, got = t[row], rows[k]
            # None means "this type has no such art", which in the source is (0,0,0).
            if got is None:
                if want != (0, 0, 0):
                    sys.stderr.write("bake_dosinfantry: %s %s baked None, %s row %d is %s\n"
                                     % (ini, SLOT_NAMES[k], sname, row, want))
                    bad += 1
            elif tuple(got) != want:
                sys.stderr.write("bake_dosinfantry: %s %s baked %s, %s row %d is %s\n"
                                 % (ini, SLOT_NAMES[k], tuple(got), sname, row, want))
                bad += 1
            checked += 1
    if bad:
        raise SystemExit("bake_dosinfantry: %d DO rows disagree with idata.cpp" % bad)
    print("idata.cpp cross-check: %d rows over %d types, all exact"
          % (checked, len(all_tables)))
    return checked
NSLOTS = len(SLOT_NAMES)
NFACINGS = 8             # HumanShape spans 8 facings, CCW from N

# THE HOUSE BAND IS THE CARTRIDGE'S, NOT THE 1995 DOS GAME'S.
#
# The DOS original recolours Nod with const.cpp:404 RemapLtBlue, which is a SATURATED
# TEAL. The console does something else entirely: it draws infantry as CI sprites
# through the same house TLUT its models use (the 2D sprite draw at RAM 0x8004F6FC
# calls the selector at RAM 0x80055D08 with a signed per-object house byte), and Nod's
# band there is a DESATURATED BLUE-GREY. Measured off reference console footage, a Nod
# rifleman's body pixels are (112,118,132)/(116,115,146)/(111,119,132); our teal build
# measured (0,112,112)/(4,92,100)/(16,60,80). That is the reported "Nod colours are wrong".
#
# The band identity is provable rather than assumed: DOS palette index 176+k IS N64 TLUT
# entry 16+k, because the GDI table is the DOS gold ramp quantised to RGBA5551 --
#   DOS 176 (244,212,120) vs GDI[16] (246,213,123)
#   DOS 177 (220,188,104) vs GDI[17] (222,189,106)
#   DOS 178 (196,168, 92) vs GDI[18] (197,172, 98)
# so GDI needs no remap at all (our gold band already IS the cartridge's, to within the
# 5551 quantisation) and Nod becomes a straight substitution of 16 colours.
#
# ROM 0x98F30 entries 16..31 (GDI) and ROM 0x99130 entries 16..31 (Nod/neutral), RGB8.
# The DOS RemapLtBlue table is kept below only as the record of what we replaced.
LTBLUE_176_191 = [2, 119, 118, 135, 136, 138, 112, 12, 118, 135, 136, 137, 138, 139, 114, 112]

N64_BAND_NOD = [(115, 106, 115), (115, 123, 131), (106, 115, 115), (98, 106, 106),
                (74, 82, 90),    (82, 82, 82),    (65, 65, 74),    (65, 74, 74),
                (57, 57, 57),    (57, 65, 65),    (32, 41, 41),    (32, 41, 41),
                (32, 32, 41),    (16, 16, 16),    (24, 16, 16),    (0, 0, 0)]


def remap_ltblue(i):
    return LTBLUE_176_191[i - 176] if 176 <= i <= 191 else i


def next_pot(v):
    p = 1
    while p < v:
        p <<= 1
    return p


def frames_of(shp, frame, count, jump, stage_pick=None):
    """Frame numbers of one strip, facing-major, per Draw_It's arithmetic.
    stage_pick (optional) selects a subset of the stage indices 0..count-1
    (the Voodoo 2 texture-fit subsample); default is all of them."""
    stages = stage_pick if stage_pick is not None else list(range(count))
    if jump:
        return [frame + f * jump + s for f in range(NFACINGS) for s in stages]
    return [frame + s for s in stages]


def body_bbox(buf, w, h):
    x0 = y0 = 1 << 30
    x1 = y1 = -1
    for y in range(h):
        row = buf[y * w:(y + 1) * w]
        for x in range(w):
            v = row[x]
            if v == TRANSPARENT or v == SHADOW:
                continue
            if x < x0:
                x0 = x
            if x > x1:
                x1 = x
            if y < y0:
                y0 = y
            if y > y1:
                y1 = y
    if x1 < 0:
        raise SystemExit("empty frame")
    return x0, y0, x1, y1


def pack_name(s, n):
    b = s.encode("ascii")
    assert len(b) <= n, s
    return b + b"\x00" * (n - len(b))


def main():
    cq = MixFile(CONQUER)
    pal6 = MixFile(LOCAL).read("TEMPERAT.PAL")
    assert len(pal6) == 768 and max(pal6) <= 63
    # guard against the repacked-content-mix mistake: the CD palette's gold
    # range starts (61,53,30) at index 176
    assert tuple(pal6[176 * 3:176 * 3 + 3]) == (61, 53, 30), "wrong TEMPERAT.PAL source"
    pal8 = bytes(min(252, v << 2) for v in pal6)

    # ---- the cartridge's Nod band needs sixteen palette slots of its own ----------
    # The pack carries ONE 256-entry palette and per-house INDEX remaps, so switching
    # Nod from the DOS teal to the console's blue-grey means parking those 16 colours
    # somewhere in that palette. Rather than overwrite live DOS entries (112, 118, 119,
    # 135..139 and friends are real body and gun colours elsewhere in the same art),
    # find slots no infantry frame actually uses and claim those. Computed, not assumed,
    # and it asserts rather than quietly colliding.
    used = set()
    for _nm in sorted(set(list(COMBAT.keys()) + list(CIVILIANS))):
        try:
            _sh = Shape(cq.read(_nm + ".SHP"))
        except Exception:
            continue
        for _f in range(_sh.frames):
            used.update(_sh.frame(_f))
    NOD_SLOTS = [i for i in range(255, 0, -1)
                 if i not in used and not (176 <= i <= 191)][:16]
    NOD_SLOTS.sort()
    assert len(NOD_SLOTS) == 16, \
        ("only %d unused palette slots for the cartridge's Nod band -- the infantry art "
         "now uses more of the palette than it did, so the band needs a real second "
         "palette rather than spare slots" % len(NOD_SLOTS))
    _pal = bytearray(pal8)
    for _k, _rgb in enumerate(N64_BAND_NOD):
        _pal[NOD_SLOTS[_k] * 3:NOD_SLOTS[_k] * 3 + 3] = bytes(_rgb)
    pal8 = bytes(_pal)
    print("infantry: Nod house band -> palette slots %d..%d (the cartridge's own "
          "ROM 0x99130 entries 16..31, not the 1995 RemapLtBlue teal)"
          % (NOD_SLOTS[0], NOD_SLOTS[-1]))

    def remap_nod(i):
        return NOD_SLOTS[i - 176] if 176 <= i <= 191 else i

    strips = []          # (name, frames, facings, stages, fw, fh, cols, texw, texh, idxbytes)
    types = []           # (ini, [[7 gold], [7 ltblue]])
    report = {}

    def bake_type(ini, shpname, do_rows, houses):
        shp = Shape(cq.read(shpname + ".SHP"))
        w, h = shp.width, shp.height
        # last table row must end exactly at or inside the SHP (cross-check the
        # transcription against the physical file). None = the type has no such
        # anim (E6's DO_FIRE_WEAPON row in idata.cpp is 0,0,0).
        #
        # ONE MEASURED EXCEPTION, AND IT IS THE 1995 DATA'S, NOT OURS. MOEBIUS.SHP
        # and CHAN.SHP each hold exactly 257 frames, while MoebiusDos/DrChanDos both
        # ask for frames 240..257 in the DO_FIRE (burn-to-death) row -- one frame
        # past the end. Two different files and two different tables agreeing on the
        # same off-by-one is a systematic quirk of the shipped data, not a corrupt
        # copy and not a bad transcription: the idata.cpp cross-check passes all 260
        # rows exactly, and DELPHI, whose SHP has the full 375 frames, needs no
        # clamp at all. We cannot bake a frame that does not exist, so the row is
        # shortened to what the file really holds and the loss is printed.
        #
        # The tolerance is ONE frame deliberately. A transcription that is actually
        # wrong overshoots by a lot, and that must still be a hard failure -- this
        # assert has already earned its keep and is not being weakened into a shrug.
        do_rows = list(do_rows)
        for i, row in enumerate(do_rows):
            if row is None:
                continue
            fr, cnt, jmp = row
            last = fr + (NFACINGS - 1) * jmp + cnt - 1 if jmp else fr + cnt - 1
            over = last - (shp.frames - 1)
            if over > 0:
                assert over <= 1 and not jmp, \
                    ("%s %s row %s overruns %s.SHP by %d frames (has %d) -- that is "
                     "too much to be the known 1995 one-frame quirk"
                     % (ini, SLOT_NAMES[i], row, shpname, over, shp.frames))
                do_rows[i] = (fr, cnt - over, jmp)
                print("  %s %s: %s.SHP holds %d frames, idata.cpp asks for %d; "
                      "baking %d frames instead of %d (1995 data overrun)"
                      % (ini, SLOT_NAMES[i], shpname, shp.frames, last + 1,
                         cnt - over, cnt))
                fr, cnt, jmp = do_rows[i]
                last = fr + cnt - 1
            assert last < shp.frames, (ini, fr, cnt, jmp, shp.frames)

        # crops first (identical for both house rows; the remap never maps a
        # body colour to 0 or 4, asserted below)
        crops = []
        for slot, row in enumerate(do_rows):
            if row is None:
                crops.append(None)
                continue
            fr, cnt, jmp = row
            fl = frames_of(shp, fr, cnt, jmp)
            bx0 = by0 = 1 << 30
            bx1 = by1 = -1
            for f in fl:
                x0, y0, x1, y1 = body_bbox(shp.frame(f), w, h)
                bx0, by0 = min(bx0, x0), min(by0, y0)
                bx1, by1 = max(bx1, x1), max(by1, y1)
            half = max(ANCHOR_COL - bx0, bx1 - ANCHOR_COL)
            crops.append([ANCHOR_COL - half, by0, ANCHOR_COL + half, by1])
        # unified feet line for the live poses (STAND, WALK, FIRE): the ground
        # anchor must not jump a pixel when a standing man raises his rifle.
        # Deaths keep their own bottom (the sprawl IS the ground contact).
        FIRE_SLOT = SLOT_NAMES.index("FIRE")
        live = [0, 1] + ([FIRE_SLOT] if crops[FIRE_SLOT] is not None else [])
        feet = max(crops[s][3] for s in live)
        for s in live:
            crops[s][3] = feet

        row_ids = []
        for hname, remap in houses:
            ids = []
            for slot, row in enumerate(do_rows):
                if row is None:
                    ids.append(-1)
                    continue
                fr, cnt, jmp = row
                x0, y0, x1, y1 = crops[slot]
                fw, fh = x1 - x0 + 1, y1 - y0 + 1
                # Voodoo 2 fit guard: if all Count stages cannot fit a 256x256
                # sheet, subsample the stages evenly (keeping stage 0) until they
                # do. src_stages stays the engine Count so the renderer can map
                # Fetch_Stage() onto the baked frames.
                stage_pick = list(range(cnt))
                while True:
                    n = (NFACINGS if jmp else 1) * len(stage_pick)
                    cols = min(n, max(1, MAX_TEX // fw))
                    rows = (n + cols - 1) // cols
                    texw, texh = next_pot(cols * fw), next_pot(rows * fh)
                    if texw <= MAX_TEX and texh <= MAX_TEX:
                        break
                    assert len(stage_pick) > 1, (ini, slot, fw, fh, n)
                    stage_pick = stage_pick[::2]
                fl = frames_of(shp, fr, cnt, jmp, stage_pick)
                sheet = bytearray(texw * texh)
                for i, f in enumerate(fl):
                    buf = shp.frame(f)
                    cx, cy = (i % cols) * fw, (i // cols) * fh
                    for yy in range(fh):
                        sy = y0 + yy
                        for xx in range(fw):
                            sx = x0 + xx
                            v = buf[sy * w + sx] if (0 <= sx < w and 0 <= sy < h) else 0
                            if remap and v not in (TRANSPARENT, SHADOW):
                                v2 = remap(v)
                                assert v2 not in (TRANSPARENT, SHADOW), (ini, v, v2)
                                v = v2
                            sheet[(cy + yy) * texw + cx + xx] = v
                name = "%s_%s#%d" % (ini, SLOT_NAMES[slot], hname)
                ids.append(len(strips))
                strips.append((name, len(fl), NFACINGS if jmp else 1, len(stage_pick),
                               fw, fh, cols, texw, texh, cnt, bytes(sheet)))
            row_ids.append(ids)
        if len(row_ids) == 1:            # civilians: both house rows same strips
            row_ids.append(row_ids[0])
        types.append((ini, row_ids))
        report[ini] = {
            "shp": shpname + ".SHP", "shp_frames": shp.frames,
            "crops": {SLOT_NAMES[i]: {"box": crops[i],
                                      "fw": crops[i][2] - crops[i][0] + 1,
                                      "fh": crops[i][3] - crops[i][1] + 1}
                      for i in range(len(crops)) if crops[i] is not None},
        }

    # Prove the transcriptions before baking a single pixel from them, so a wrong
    # row fails here rather than shipping as a subtly wrong death animation.
    verify_against_idata([(ini, rows) for ini, rows in COMBAT.items()]
                         + [(ini, CIVILIAN_DO) for ini in CIVILIANS]
                         + [(ini, rows) for ini, rows in NAMED])

    for ini, rows in COMBAT.items():
        bake_type(ini, ini, rows, [(0, None), (1, remap_nod)])
    for ini in CIVILIANS:
        bake_type(ini, ini, CIVILIAN_DO, [(0, None)])
    # MOEBIUS / CHAN / DELPHI. Civilians in every sense the renderer cares about --
    # neutral house, no Nod row -- they simply have their own SHPs and, for two of
    # them, their own frame layout.
    for ini, rows in NAMED:
        bake_type(ini, ini, rows, [(0, None)])

    with open(OUT, "wb") as fh:
        fh.write(MAGIC)
        fh.write(struct.pack("<III", VERSION, len(strips), len(types)))
        fh.write(pal6)
        fh.write(pal8)
        for name, frames, facings, stages, fw, fh2, cols, texw, texh, src, px in strips:
            fh.write(pack_name(name, NAME_BYTES))
            fh.write(struct.pack("<9I", frames, facings, stages, fw, fh2, cols, texw, texh, src))
            fh.write(px)
        for ini, rows in types:
            fh.write(pack_name(ini, 8))
            for r in range(2):
                for s in range(NSLOTS):
                    fh.write(struct.pack("<i", rows[r][s]))

    manifest = {
        "pack": os.path.basename(OUT), "magic": MAGIC.decode(), "version": VERSION,
        "bytes": os.path.getsize(OUT),
        "palette": "TEMPERAT.PAL from the 1995 LOCAL.MIX (every index the strips "
                   "use is byte-identical in DESERT.PAL; content/TEMPERAT.MIX is "
                   "repacked and carries a wrong palette, never source it)",
        "anchor": "crop symmetric about frame column 27 (Draw_It ground x); quad "
                  "bottom = crop bottom = feet line (STAND/WALK unified per type)",
        "shadow": "index 4 kept in data, renderer maps it to alpha 0",
        "facing_order": "facing-major, facenum 0..7 CCW from N per HumanShape[32]",
        "slots": SLOT_NAMES,
        "death_dotype": {"22": "D_GUN", "23": "D_EXPL", "24": "D_EXPL2",
                         "25": "D_GREN", "26": "D_FIRE",
                         "17_20_punch_kick": "fallback D_GUN"},
        "live_dotype": {"4": "FIRE", "2": "PRONE", "8": "FIRE_PRONE",
                        "5": "LIE_DOWN", "6": "CRAWL", "7": "GET_UP"},
        "fire": "the v2 live-combat slots are idata.cpp's own DO rows; the "
                "renderer picks them per tick from doing/dostage. E6 has no "
                "FIRE/FIRE_PRONE (-1). src_stages is the engine Count; == "
                "stages unless subsampled to fit the Voodoo 2 256x256 limit.",
        "types": report,
        "strips": {s[0]: {"frames": s[1], "facings": s[2], "stages": s[3],
                          "fw": s[4], "fh": s[5], "cols": s[6],
                          "tex": [s[7], s[8]], "src_stages": s[9]} for s in strips},
    }
    with open(os.path.splitext(OUT)[0] + ".manifest.json", "w") as f:
        json.dump(manifest, f, indent=1, sort_keys=True)

    print("wrote %s: %d strips, %d types, %.1f KB"
          % (OUT, len(strips), len(types), os.path.getsize(OUT) / 1024.0))
    for ini, rows in types:
        r = report[ini]["crops"]
        fire = ("fire %dx%d" % (r["FIRE"]["fw"], r["FIRE"]["fh"])
                if "FIRE" in r else "fire none")
        print("  %-5s stand %dx%d walk %dx%d dgun %dx%d dfire %dx%d %s"
              % (ini, r["STAND"]["fw"], r["STAND"]["fh"], r["WALK"]["fw"], r["WALK"]["fh"],
                 r["D_GUN"]["fw"], r["D_GUN"]["fh"], r["D_FIRE"]["fw"], r["D_FIRE"]["fh"],
                 fire))


if __name__ == "__main__":
    main()
