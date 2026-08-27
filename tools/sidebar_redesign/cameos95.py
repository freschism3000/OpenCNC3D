"""Bake the C&C95 (Win95) 64x48 cameos into a pack the sidebar can draw.
import os

# Repo root, resolved at runtime. CNC3D_ROOT overrides it for an out-of-tree checkout.
_ROOT = os.environ.get("CNC3D_ROOT") or os.path.abspath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


WHY A PACK AND NOT PNGs
-----------------------
The build clock is not painted, it is a LOOKUP on the destination pixel through the DOS
pack's translucency table (sidebar.cpp:1307, `TLucentType ClockCols = {GREEN, LTGREY,
180, 0}`). That only works in the game's own 8-bit colour space. So the cameos are
quantised to the DOS 256-colour palette and stored as 8-bit indices, exactly like the
DOS cameos, and the cell is composed in 8-bit and converted to RGBA once at the end.
Reimplementing that blend in RGBA would be a guess at the table.

The output reuses the DOSBAR01 container so `db_pack_load()` reads it with no new C.

SIZE - AND WHY NOTHING IS CROPPED ANY MORE
------------------------------------------
C&C95 cameos are 64x48 and OPAQUE. They are stored at exactly that, untouched.

They used to be centre-cropped to 61x45, the measured size of the chassis well, on the
reasoning that the 3px lost was their own bevel. That was wrong, and it was spotted
against a real C&C95 screenshot: the CAPTION runs the full width of the cameo and sits
hard on its bottom edge, so cropping 3 columns takes the first and last letter and
cropping 3 rows takes the underside of the glyphs. That is the whole reason the names
were hard to read.

Isolating the two steps proved the palette pass is innocent - these files quantise
98 colours to 98, losslessly, because the wiki art is already in the game's own palette.
The crop was doing all the damage.

The cameo is therefore drawn at its native 64x48, CENTRED on the 61x45 well, overhanging
it by 1-2px. That is also what C&C95 itself looks like, where cameos butt together with
no gap. It costs nothing structurally: 64x48 is exactly 2x the DOS cameo, so the DOS
fallback now doubles into the same box with no clipping either.

CLOCK and PIPS are carried in at 2x, nearest-neighbour in INDEX space so no colour is
invented, because they have to overlay a cell that is now twice the DOS size.

PROVENANCE - READ THIS
----------------------
The source art is `data/assets/cameos_wiki/cnc95_textless`, which that folder's own README
states plainly is a set of FAN UPLOADS from cnc.fandom.com, not an extraction from
original media, and recommends treating as reference until checked against a disc. The C&C95
cameos were chosen knowing this. The gap is recorded as a known gap.
"""
from PIL import Image
import numpy as np
import os, struct, sys, json

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))

# data/ is gitignored and lives in the main checkout, not in this worktree, so look in
# both. Nothing is ever copied out of it - the pack is the only artefact that leaves.
_ART_ROOTS = [os.path.join(ROOT, "data"),
              os.path.join(_ROOT, "data")]
_WIKI = next((os.path.join(r, "assets", "cameos_wiki")
              for r in _ART_ROOTS
              if os.path.isdir(os.path.join(r, "assets", "cameos_wiki"))),
             os.path.join(ROOT, "data", "assets", "cameos_wiki"))
ART = os.path.join(_WIKI, "cnc95_textless")
ART_CAP = os.path.join(_WIKI, "cnc95_captioned")
DOSPACK = os.path.join(ROOT, "playable", "dossidebar.pack")
OUT = os.path.join(ROOT, "playable", "cameos95.pack")

CELL_W, CELL_H = 61, 45     # the chassis well, for reference only - see SIZE above
SRC_W, SRC_H = 64, 48       # C&C95 native

# Stem -> wiki file stem. The stem side is the engine's own 4-char ident; the readable
# name each one carries was read out of brain/vanilla (bdata/udata/idata/adata.cpp,
# the TXT_ that precedes each "NAME" field) rather than recalled, because several are
# counter-intuitive: ARCO is a tanker truck, HTNK is the Mammoth, E6 carries TXT_E7.
MAP = {
    "A10":  "A10",             "AFLD": "Airstrip",        "APC":  "APC",
    "ARTY": "Artillery",       "ATOM": "NuclearStrike",   "ATWR": "AdvGuardTower",
    "BARB": "BarbedWire",      "BGGY": "Buggy",           "BIKE": "ReconBike",
    "BOAT": "Gunboat",         "BOMB": "AirStrike",       "BRIK": "ConcreteWall",
    "CYCL": "ChainLink",       "E1":   "Minigunner",      "E2":   "Grenadier",
    "E3":   "RocketSoldier",   "E4":   "Flamethrower",    "E5":   "ChemWarrior",
    "E6":   "Engineer",        "EYE":  "AdvCommCenter",   "FACT": "ConYard_Ren",
    "FIX":  "RepairFacility",  "FTNK": "FlameTank",       "GTWR": "GuardTower",
    "GUN":  "Turret",          "HAND": "HandOfNod",       "HARV": "Harvester",
    "HELI": "Apache",          "HPAD": "Helipad",         "HQ":   "CommCenter",
    "HTNK": "MammothTank",     "ION":  "IonCannon",       "JEEP": "Humvee",
    "LST":  "Hovercraft",      "LTNK": "LightTank",       "MCV":  "MCV",
    "MSAM": "MLRS",            "MTNK": "MediumTank",      "NUK2": "AdvPowerPlant",
    "NUKE": "PowerPlant",      "OBLI": "ObeliskOfLight",  "ORCA": "Orca",
    "PROC": "Refinery",        "PYLE": "Barracks",        "RMBO": "Commando",
    "SAM":  "SAMSite",         "SBAG": "Sandbags",        "SILO": "Silo",
    "STNK": "StealthTank",     "TMPL": "TempleOfNod",     "TRAN": "Transport",
    "WEAP": "WeaponsFactory",  "WOOD": "WoodFence",
}

# The CAPTIONED set, which was confirmed is correct, and which therefore WINS over
# the textless file for the same buildable. It covers 53 of the 61 and its filenames follow
# no pattern at all - 26 PNG, 31 GIF, three with no TD_ prefix - so they are listed
# rather than derived. (An earlier scan that only counted .png saw 26 of them and
# concluded the set was half the size it is.)
#
# In C&C95 the cameo carries the unit's name burned into a bar along its bottom - that is
# what the release actually looked like, so "captioned" is not a variant, it is the real
# thing and "textless" is the retouched one.
#
# Two of these fix problems the textless set had: ConYard_cameo replaces the
# Renegade-sourced TD_ConYard_Ren_cameo.png, and Rocket_Launcher_Icons settles the MLRS /
# MSAM ident swap - the GDI rocket launcher is stem MSAM.
CAPTIONED = {
    "A10":  "TD_Support_Aircraft_Icons.png",
    "AFLD": "TD_Airstrip_Icons.gif",
    "APC":  "TD_APC_Icons.png",
    "ARTY": "TD_Artillery_Icons.gif",
    "ATOM": "TD_Nuclear_Strike_Icons.gif",
    "ATWR": "TD_Advanced_Guard_Tower_Icons.gif",
    "BARB": "TD_BarbedWire_cameo.png",
    "BGGY": "TD_Buggy_Icons.gif",
    "BIKE": "TD_Recon_Bike_Icons.gif",
    "BOAT": "TD_Gunboat_Icons.png",
    "BOMB": "TD_Air_Strike_Icons.gif",
    "BRIK": "TD_Concrete_Wall_Icons.gif",
    "CYCL": "TD_Chain_Link_Fence_Icons.gif",
    "E1":   "TD_Minigunner_Icons.png",
    "E2":   "TD_Grenadier_Icons.png",
    "E3":   "TD_Rocket_Soldier_Icons.png",
    "E4":   "TD_Flamethrower_Icons.gif",
    "E5":   "TD_Chemical_Warrior_Icons.gif",
    "E6":   "TD_Engineer_Cameo.png",
    "EYE":  "TD_Advanced_Communications_Center_Icons.gif",
    "FACT": "TD_ConYard_cameo.png",
    "FIX":  "TD_Repair_Facility_Icons.gif",
    "FTNK": "TD_Flame_Tank_Icons.gif",
    "GTWR": "TD_Guard_Tower_Icons.gif",
    "GUN":  "TD_Turret_Icons.gif",
    "HAND": "TD_Hand_of_Nod_Icons.gif",
    "HARV": "TD_Harvester_Icons.png",
    "HELI": "TD_Apache_Icons.gif",
    "HPAD": "TD_Helipad_Icons.gif",
    "HQ":   "TD_Communications_Center_Icons.gif",
    "HTNK": "TD_Mammoth_Tank_Icons.png",
    "ION":  "TD_Ion_Cannon_Icons.gif",
    "JEEP": "TD_Humvee_Icons.png",
    "LST":  "TD_Hovercraft_Icons.gif",
    "LTNK": "TD_Light_Tank_Icons.gif",
    "MCV":  "TD_MCV_Icons.png",
    "MSAM": "TD_Rocket_Launcher_Icons.png",
    "MTNK": "TD_Medium_Tank_Icons.png",
    "NUK2": "TD_Advanced_Powerplant_Icons.png",
    "NUKE": "TD_Powerplant_Icons.png",
    "OBLI": "TD_Obelisk_of_Light_Icons.gif",
    "ORCA": "TD_ORCA_Assault_Craft_Icons.png",
    "PROC": "TD_Tiberium_Refinery_Icons.gif",
    "PYLE": "TD_Barracks_Icons.png",
    "RMBO": "TD_Commando_Icons.png",
    "SAM":  "TD_SAM_Site_Icons.gif",
    "SBAG": "TD_Sandbags_Icons.gif",
    "SILO": "TD_Tiberium_Silo_Icons.gif",
    "STNK": "TD_Stealth_Tank_Icons.gif",
    "TMPL": "TD_Temple_of_Nod_Icons.gif",
    "TRAN": "TD_Transport_Icons.png",
    "WEAP": "TD_Weapons_Factory_Icons.png",
    "WOOD": "TD_WoodFence_cameo.png",
}

# Deliberately NOT mapped, with the reason. Nothing here gets a stand-in: a cameo that
# silently shows the wrong unit is worse than one that falls back to its DOS original.
UNMAPPED = {
    "ARCO": "TXT_TANKER, the oil tanker truck - no C&C95 cameo on the wiki",
    "BIO":  "TXT_BIO_LAB, a mission structure - no C&C95 cameo on the wiki",
    "C17":  "the airstrip's delivery plane - no C&C95 cameo on the wiki",
    "HOSP": "TXT_HOSPITAL, a civilian structure - no C&C95 cameo on the wiki",
    "MHQ":  "TXT_MHQ, the mobile HQ - no C&C95 cameo on the wiki",
    "MLRS": "TD swaps these two: stem MLRS carries TXT_MSAM (the Nod Mobile SAM), which "
            "has no cameo in either wiki set. The GDI rocket launcher is stem MSAM and IS "
            "mapped, from the captioned set's Rocket_Launcher_Icons.png.",
    "PUMP": "73-byte blank in CONQUER.MIX, byte-identical to STRIP.SHP",
    "ROAD": "73-byte blank in CONQUER.MIX, byte-identical to STRIP.SHP",
}


def read_dospack(path):
    blob = open(path, "rb").read()
    p = 8
    ver, ns, nf, npal = struct.unpack_from("<4I", blob, p)
    p += 16
    pal6 = blob[p:p + 768]; p += 768
    pal8 = blob[p:p + 768]; p += 768
    clocktab = blob[p:p + 512]; p += 512
    shapes = {}
    for _ in range(ns):
        name = blob[p:p + 12].split(b"\0")[0].decode()
        fr, w, h = struct.unpack_from("<3I", blob, p + 12)
        p += 24
        shapes[name] = (fr, w, h, blob[p:p + fr * w * h])
        p += fr * w * h
    return pal6, pal8, clocktab, shapes


def quantise(img, pal8):
    """Snap RGB to the game's own 256 palette. Index 0 is TRANSPARENT in this container,
    so it is excluded as a target - a cameo pixel that happened to be palette-black would
    otherwise punch a hole in the art."""
    pal = np.frombuffer(pal8, np.uint8).reshape(256, 3).astype(int)
    a = np.array(img.convert("RGB")).astype(int)
    flat = a.reshape(-1, 1, 3)
    d = ((flat - pal[None, :, :]) ** 2).sum(axis=2)
    d[:, 0] = 1 << 30
    return d.argmin(axis=1).astype(np.uint8).reshape(a.shape[:2])


def double(px, fr, w, h):
    """Nearest 2x in INDEX space. No colour is invented and no index is blended."""
    src = np.frombuffer(px, np.uint8).reshape(fr, h, w)
    return np.repeat(np.repeat(src, 2, axis=1), 2, axis=2).tobytes()


def build():
    if not os.path.isdir(ART):
        sys.exit("missing art dir: %s" % ART)
    pal6, pal8, clocktab, dos = read_dospack(DOSPACK)

    shapes = []
    missing = []
    from_cap = 0
    for stem in sorted(set(MAP) | set(CAPTIONED)):
        # Most of the set is TD_<name>_EU_cameo.png, but not all of it - the construction
        # yard is TD_ConYard_Ren_cameo.png, with no _EU_ and a _Ren suffix that says the
        # uploader took it from Renegade rather than C&C95. Flagged as a known gap.
        # TEXTLESS first. The captioned art is what C&C95 actually shipped, but the name
        # is burned into the bottom of the cameo and that is now the tooltip's job -
        # baked-in text cannot be localised, cannot be restyled, and is the one part of
        # the image that has to survive magnification perfectly to stay legible.
        # Captioned is kept as the fallback so a buildable that only exists there still
        # gets C&C95 art rather than dropping to its DOS cameo.
        cands = []
        if stem in MAP:
            cands += [os.path.join(ART, p % MAP[stem])
                      for p in ("TD_%s_EU_cameo.png", "TD_%s_cameo.png")]
        if stem in CAPTIONED:
            cands.append(os.path.join(ART_CAP, CAPTIONED[stem]))
        fn = next((c for c in cands if os.path.exists(c)), None)
        if fn is None:
            missing.append((stem, "no file matching %s" % " or ".join(
                os.path.basename(c) for c in cands)))
            continue
        if stem in CAPTIONED and fn == os.path.join(ART_CAP, CAPTIONED[stem]):
            from_cap += 1
        im = Image.open(fn)
        if im.size != (SRC_W, SRC_H):
            missing.append((stem, "wrong size %dx%d, expected %dx%d" % (im.size + (SRC_W, SRC_H))))
            continue
        idx = quantise(im, pal8)          # native 64x48, no crop
        shapes.append((stem, 1, SRC_W, SRC_H, idx.tobytes()))

    # CLOCK and PIPS at 2x so they can overlay a cell that is no longer 32x24.
    for nm in ("CLOCK", "PIPS", "STRIP"):
        if nm not in dos:
            missing.append((nm, "absent from dossidebar.pack"))
            continue
        fr, w, h, px = dos[nm]
        shapes.append((nm, fr, w * 2, h * 2, double(px, fr, w, h)))

    out = bytearray()
    out += b"DOSBAR01" + struct.pack("<IIII", 1, len(shapes), 0, 256)
    out += pal6 + pal8 + clocktab
    for name, fr, w, h, blob in shapes:
        out += name.encode()[:12].ljust(12, b"\0") + struct.pack("<III", fr, w, h) + blob
    open(OUT, "wb").write(out)

    print("wrote %s  (%d KB, %d shapes)" % (OUT, len(out) // 1024, len(shapes)))
    print("cameos mapped: %d  (%d textless, %d still captioned - no textless art exists)"
          % (len(shapes) - 3, len(shapes) - 3 - from_cap, from_cap))
    if missing:
        print("\nNOT BAKED (%d):" % len(missing))
        for s, why in missing:
            print("  %-6s %s" % (s, why))
    print("\nDELIBERATELY UNMAPPED (%d) - these fall back to their DOS 32x24 cameo:"
          % len(UNMAPPED))
    for s in sorted(UNMAPPED):
        print("  %-6s %s" % (s, UNMAPPED[s]))
    json.dump({"mapped": sorted(MAP), "unmapped": UNMAPPED,
               "not_baked": dict(missing), "well": [CELL_W, CELL_H], "stored": [SRC_W, SRC_H]},
              open(os.path.join(HERE, "chunks", "cameos95.json"), "w"), indent=1)
    return missing


if __name__ == "__main__":
    build()
