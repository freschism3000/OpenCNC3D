#!/usr/bin/env python3
"""Bake the 128x96 cameo art into a TRUE-COLOUR pack for the 640x480 HUD.

WHY THIS IS NOT cameos95.py
---------------------------
cameos95.py quantises to the game's 256-colour palette and stores 8-bit indices, because
the DOS sidebar composes its cell in index space (the build clock is a lookup through the
pack's translucency table, not paint). The 128x96 art is full colour, and putting it
through the palette costs a visible mean per-pixel RGB error. The 640 HUD's cameo buffer
is already RGBA, so the palette step is avoidable there and is required to be avoided:
the new HUD is 16-bit colour, the art is downscaled to 64x48 to fit, and the colour is
kept.

So this writes RGBA and nothing else. The clock and the pips are still the DOS pack's,
drawn by the sidebar; see cnc_sidebar.h for how a faded pixel is told from a pip.

THE DOWNSCALE
-------------
128x96 -> 64x48 is exactly 2:1, and every output pixel is the mean of one 2x2 block
(PIL's BOX filter). No pixel is invented, no edge is resampled across, and the operation
is the same for every file.

FORMAT
------
    "CNC3DTDR" | u32 count | count x { char key[8]; u32 w; u32 h; u8 rgba[w*h*4] }
key is the game's own buildable ident (E1, MTNK, NUKE...), NUL-padded to 8.
The loader in cnc_sidebar.h rejects any entry whose w/h are not 64x48.

WHERE THE ART COMES FROM
------------------------
art/cameos-tdr holds exactly one 128x96 PNG per buildable, all named
TD_<Name>_128x96.png. 49 of the 54 are remakes traced from the 1995 PDF manual renders;
the other five are the earlier art set, kept because no manual render exists for them
(see RETAINED_OLD_ART below). Both generations share the one naming convention on
purpose, so nothing here needs a second code path.

THIS SCRIPT FAILS RATHER THAN WRITING A SHORT PACK
--------------------------------------------------
Four separate ways, because a pack that is missing a cameo does not crash the game: the
HUD just falls back to the 8-bit DOS art for that one cell, which looks like a rendering
choice rather than a bug. So a stray file that does not follow the naming convention, an
unmapped filename, two files claiming one key, and a mapped key with no art at all each
stop the bake with the name printed.
"""
import os, struct, sys, glob
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
ART  = os.path.join(ROOT, "art", "cameos-tdr")
OUT  = os.path.join(ROOT, "game", "cameos_tdr.pack")
W, H = 64, 48
PREFIX, SUFFIX = "TD_", "_128x96.png"

# Art filename stem -> the game's buildable key. Derived from the same MAP that
# cameos95.py uses, so the two sets stay in step; the notes are where the art's names
# differ from that file's.
MAP = {
    "A10": "A10", "Airstrip": "AFLD", "APC": "APC", "Artillery": "ARTY",
    "NuclearStrike": "ATOM", "AdvGuardTower": "ATWR", "BarbedWire": "BARB",
    "Buggy": "BGGY", "ReconBike": "BIKE", "Gunboat": "BOAT", "AirStrike": "BOMB",
    "ConcreteWall": "BRIK", "ChainLink": "CYCL", "Minigunner": "E1", "Grenadier": "E2",
    "RocketSoldier": "E3", "Flamethrower": "E4", "ChemWarrior": "E5", "Engineer": "E6",
    "AdvCommCenter": "EYE", "ConYard": "FACT", "RepairFacility": "FIX",
    "FlameTank": "FTNK", "GuardTower": "GTWR", "Turret": "GUN", "HandOfNod": "HAND",
    "Harvester": "HARV", "Apache": "HELI", "Helipad": "HPAD", "CommCenter": "HQ",
    "MammothTank": "HTNK", "IonCannon": "ION",
    # The Hum-vee's ident is JEEP. The earlier art set filed this one under "Light Scout".
    "Humvee": "JEEP",
    "Hovercraft": "LST", "LightTank": "LTNK", "MCV": "MCV",
    # The GDI rocket launcher's ident is MSAM, settled in cameos95.py's own notes. The
    # earlier art set filed this one under "Rocket Launcher".
    "MLRS": "MSAM",
    "MediumTank": "MTNK", "AdvPowerPlant": "NUK2", "PowerPlant": "NUKE",
    "ObeliskOfLight": "OBLI", "Orca": "ORCA", "Refinery": "PROC", "Barracks": "PYLE",
    "Commando": "RMBO", "SAMSite": "SAM", "Sandbags": "SBAG", "Silo": "SILO",
    "StealthTank": "STNK", "TempleOfNod": "TMPL", "Transport": "TRAN",
    "WeaponsFactory": "WEAP", "WoodFence": "WOOD",
}
# Drawn, but Tiberian Dawn's sidebar has no buildable for it. Kept in the repo, left out
# of the pack, and named here so it reads as a decision rather than an oversight.
UNUSED = {"SSMLauncher"}

# THE FIVE THE MANUAL-RENDER REMAKE DOES NOT COVER, and why they are still here.
#
# That set is traced from the 1995 PDF manual renders, and the manual has no render for
# these five, so there was nothing to trace. The earlier art for them is kept rather than
# dropped: without it the HUD would fall back to the 8-bit DOS cameo for these five cells
# only, and the new sidebar would be true colour everywhere except five conspicuous
# holes. Deliberate, not an oversight. Replace a file here if a render for one of them
# ever turns up, and shorten this set when you do.
RETAINED_OLD_ART = {"AirStrike", "BarbedWire", "IonCannon", "NuclearStrike", "WoodFence"}


def build():
    if not os.path.isdir(ART):
        sys.exit("missing art dir: %s" % ART)

    # Every PNG in the directory has to follow the one naming convention. Globbing the
    # convention alone would skip a misnamed file in silence, and a skipped file is a
    # cameo the HUD quietly does without.
    stray = [os.path.basename(p) for p in sorted(glob.glob(os.path.join(ART, "*.png")))
             if not (os.path.basename(p).startswith(PREFIX)
                     and os.path.basename(p).endswith(SUFFIX))]
    if stray:
        sys.exit("art/cameos-tdr holds PNGs that are not named %s<Name>%s: %s"
                 % (PREFIX, SUFFIX, ", ".join(stray)))

    entries, seen, unknown = [], {}, []
    for path in sorted(glob.glob(os.path.join(ART, PREFIX + "*" + SUFFIX))):
        stem = os.path.basename(path)[len(PREFIX):-len(SUFFIX)]
        if stem in UNUSED:
            continue
        key = MAP.get(stem)
        if not key:
            unknown.append(stem)
            continue
        if key in seen:
            sys.exit("two files claim key %s: %s and %s" % (key, seen[key], stem))
        im = Image.open(path).convert("RGBA")
        if im.size != (128, 96):
            sys.exit("%s is %dx%d, expected 128x96" % (stem, im.size[0], im.size[1]))
        im = im.resize((W, H), Image.BOX)
        entries.append((key, im.tobytes()))
        seen[key] = stem
    if unknown:
        sys.exit("unmapped cameo art, add it to MAP or UNUSED: %s" % ", ".join(unknown))

    # And the other direction: a key in MAP whose file has gone missing. This is the one
    # the glob cannot notice, and it is the one that ships a short pack.
    missing = sorted(set(MAP.values()) - set(seen))
    if missing:
        sys.exit("no art for %d buildable(s): %s\n"
                 "Each needs art/cameos-tdr/%s<Name>%s, or its entry removed from MAP."
                 % (len(missing), ", ".join(missing), PREFIX, SUFFIX))

    entries.sort()
    with open(OUT, "wb") as f:
        f.write(b"CNC3DTDR")
        f.write(struct.pack("<I", len(entries)))
        for key, rgba in entries:
            f.write(key.encode().ljust(8, b"\0"))
            f.write(struct.pack("<2I", W, H))
            f.write(rgba)
    old = sorted(MAP[s] for s in RETAINED_OLD_ART)
    print("wrote %s: %d cameos at %dx%d RGBA (%.1f MB)"
          % (OUT, len(entries), W, H, os.path.getsize(OUT) / 1048576.0))
    print("  %d from the manual-render remakes, %d kept from the earlier set (%s)"
          % (len(entries) - len(old), len(old), ", ".join(old)))


if __name__ == "__main__":
    build()
