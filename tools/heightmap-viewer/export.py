#!/usr/bin/env python3
"""CNC3D -- heightmap viewer data exporter.

Emits, for every shipped N64 scenario, a compact JSON the browser viewer reads:

    public/data/index.json        the map list + per-map summary
    public/data/<SCEN>.json       heights, tiles, template ids
    public/data/<TH>_tiles.png    the theater tile atlas (copied from the bakery)

Every field comes from the cartridge, nothing is invented:

  heights   65x65 bytes, the per-CORNER heightmap = <SCEN>.IMG payload after its
            16-byte header (FLAT.IMG, all zero, when the scenario ships none).
            World Y = byte * 4; one cell is 256 world units wide, so 64 bytes of
            height is exactly one cell width.
  tiles     64x64 u16, the atlas slot each cell draws, computed the same way
            n64_terrain.py builds the atlas: slot = tid for the 4bpp bank,
            n4 + (tid-1024) for the 8bpp bank, 0xFFFF -> CLEAR1 icon
            (x&3)|((y&3)<<2). 32 columns, 24px tiles, 1px gutter.
  tmpl      64x64 u8, the PC TemplateType of each cell via <THEATER>.TL4/.TL8,
            255 = clear. This is what the family colouring reads.

Usage:  python3 export.py [--ex <extracted dir>] [--out public/data]
"""
import base64, json, os, re, shutil, struct, sys

import export_assets
import export_dos

HERE = os.path.dirname(os.path.abspath(__file__))
EX = os.path.join(HERE, "..", "bakery", "sharecopy", "assets", "extracted")
ATLAS_SRC = os.path.join(HERE, "..", "bakery", "sharecopy", "assets", "terrain")
OUT = os.path.join(HERE, "public", "data")
DEFINES = os.path.join(HERE, "..", "..", "brain", "vanilla", "tiberiandawn", "defines.h")

ROM = os.path.join(HERE, "..", "..", "data", "rom", "cnc_eu.z64")
TS, GUTTER, COLS = 24, 1, 32
PITCH = TS + 2 * GUTTER
BANK8_BASE = 1024


def template_names():
    names, on = [], False
    for line in open(DEFINES, errors="ignore"):
        s = line.split("//")[0].strip()
        if s.startswith("TEMPLATE_CLEAR1"):
            on = True
        if on:
            if s.startswith("TEMPLATE_COUNT"):
                break
            if s.startswith("TEMPLATE_"):
                names.append(s.rstrip(",").split()[0].rstrip(",")
                             .replace("TEMPLATE_", ""))
    return names


FAMILIES = ("CLEAR", "WATER", "SHORE", "SLOPE", "BRUSH", "PATCH", "BOULDER",
            "ROAD", "RIVER", "FORD", "FALLS", "BRIDGE")

FAMILIES = ("CLEAR", "WATER", "SHORE", "SLOPE", "BRUSH", "PATCH", "BOULDER",
            "ROAD", "RIVER", "FORD", "FALLS", "BRIDGE")


def family(name):
    for f in FAMILIES:
        if name.startswith(f):
            return f
    return "CLEAR"


def ini(scen):
    d, sec = {}, None
    p = os.path.join(EX, "INI", scen + ".INI")
    if not os.path.exists(p):
        return {}
    for line in open(p, errors="ignore"):
        s = line.strip()
        if s.startswith("["):
            sec = s.strip("[]").upper()
        elif "=" in s and sec:
            k, v = s.split("=", 1)
            d[sec + "." + k.strip().upper()] = v.strip()
    return d


def theater_root(d):
    th = d.get("MAP.THEATER", d.get("BASIC.THEATER", "TEMPERATE")).upper()
    return {"DESERT": "DESERT", "TEMPERATE": "TEMPERAT",
            "WINTER": "WINTER"}.get(th, th[:8])


_th = {}


def theater(root):
    if root in _th:
        return _th[root]
    sz = lambda p: os.path.getsize(os.path.join(EX, p))
    n4 = sz(f"DA4/{root}.DA4") // (TS * TS // 2)
    n8 = sz(f"DA8/{root}.DA8") // (TS * TS)
    rd = lambda p: open(os.path.join(EX, p), "rb").read()
    tl4, tl8 = rd(f"TL4/{root}.TL4"), rd(f"TL8/{root}.TL8")
    _th[root] = dict(n4=n4, n8=n8, tl4=tl4, tl8=tl8,
                     rows=(n4 + n8 + COLS - 1) // COLS)
    return _th[root]


def tmpl_of(th, tid):
    """PC (template, icon) for an N64 tile id; None when out of bank."""
    if tid < th["n4"]:
        i = tid * 2
        return (th["tl4"][i], th["tl4"][i + 1]) if i + 1 < len(th["tl4"]) else None
    if BANK8_BASE <= tid < BANK8_BASE + th["n8"]:
        i = (tid - BANK8_BASE) * 2
        return (th["tl8"][i], th["tl8"][i + 1]) if i + 1 < len(th["tl8"]) else None
    return None


# ---------------------------------------------------------------- categories
# Which bucket a scenario is in is NOT guessed from its name. The cartridge's own
# resident code says it, and this reads the tables straight out of the ROM.
#
#   arrA  ROM 0x21b260   the SPECIAL OPS roster. The menu dispatcher at ROM
#                        0x21ced4 sends mode 1 (SPECIAL OPS) to arrA+0 for GDI
#                        and arrA+8 for Nod, with a row count of 2 on both paths,
#                        and the screen init at ROM 0x21d660 stores the row
#                        bitmask 3 (bits 0 and 1) for both sides. So the menu
#                        offers FOUR missions: arrA[0..1] GDI, arrA[2..3] Nod.
#                        arrA reads { SCG30EA, SCG90EA, SCB21EA, SCB22EB,
#                                     SCG01EA, NULL, SCB01EA, NULL }.
#                        Those four are also the only non-campaign scenarios that
#                        ship a heightmap, a CM tint map and a .JIM 3D scene:
#                        they are the cartridge's own 3D-authored missions.
#   roster ROM 0x020eb80 55 names ending at the literal "DYNAMIC": the four above
#                        followed by the 26 GDI and 25 Nod campaign scenarios.
#
# Anything the ROM ships that neither table reaches falls in DOS Tiberian Dawn's
# expansion numbering. expand.cpp scans 20..59 for the Covert Operations dialog
# and 60..62 for the five C&C95 Bonus Missions; 70..79 are the tail rows of the
# Nod REPLAY MISSION list, which is gated on a debug flag (RAM 0x80138A30) and
# unreachable in a retail save.
SCEN_RE = re.compile(r"^SC[GBJM][0-9]{2}[EW][A-C]$")

ARR_A = 0x21B260        # SPECIAL OPS roster, 8 pointers
ARR_B = 0x21B280        # REPLAY MISSION roster, 56 pointers
OVL_DELTA = 0x7FF14760  # the Compress overlay: RAM = ROM + delta


def rom_rosters():
    """-> (campaign, specops, debugTail), read out of the cartridge's own two
    pointer arrays rather than inferred from file names."""
    d = open(ROM, "rb").read()

    def deref(base, n):
        out = []
        for i in range(n):
            p = struct.unpack_from(">I", d, base + i * 4)[0]
            if p == 0:
                out.append(None)
                continue
            off = p - OVL_DELTA
            if not (0 <= off < len(d) - 16):
                out.append(None)
                continue
            try:
                out.append(d[off:off + 16].split(b"\0")[0].decode("ascii"))
            except UnicodeDecodeError:
                out.append(None)
        return out

    # arrA[0..3] are the four SPECIAL OPS rows the dispatcher exposes (two per
    # side, row bitmask 3). arrA[4] and arrA[6] are the campaign-start pair.
    spec = [x for x in deref(ARR_A, 4) if x and SCEN_RE.match(x)]

    # arrB is the REPLAY MISSION list: the campaign in menu order, then a tail of
    # debug-only rows. DYNAMIC and STATIC are row LABELS, not scenarios; the
    # picker at ROM 0x21cf5c strcmps them and substitutes SCG71EB and SCG72EA.
    b = deref(ARR_B, 56)
    campaign, tail, in_tail = [], [], False
    LABEL_TO_SCEN = {"DYNAMIC": "SCG71EB", "STATIC": "SCG72EA"}
    for x in b:
        if x is None:
            continue
        if x in LABEL_TO_SCEN:
            in_tail = True
            tail.append(LABEL_TO_SCEN[x])
            continue
        if not SCEN_RE.match(x):
            continue
        (tail if in_tail else campaign).append(x)
    return campaign, spec, tail


def category(scen, campaign, spec, tail, has_name=False):
    if scen[2] == "M":
        return "multiplayer"
    if scen[2] == "J":
        return "dinosaur"
    if scen in spec:
        return "specops"
    if scen in campaign:
        return "singleplayer"
    if scen in tail:
        return "debug"              # tail of the debug-gated Nod REPLAY list
    n = int(scen[3:5])
    if 20 <= n <= 59:
        # The Covert Operations dialog titles every row from the mission's own
        # [Basic] Name=, and every real one carries it. A scenario in that range
        # without a title is named by no table on the cartridge either, so it is
        # not Covert Ops content, it is just present. (A PC running these files
        # WOULD still list it, as "GDI: x", because Is_Available() is the
        # dialog's only test.)
        return "covertops" if has_name else "unused"
    if 60 <= n <= 62:
        return "bonus"              # the five C&C95 Bonus Missions
    return "unclassified"


_holes = {}


def hole_slots(root):
    """Which atlas slots of a theater bank contain an alpha-0 texel.

    That is the cartridge's own statement that the cell shows SEA: the N64 terrain
    palettes carry no water colour at all, the tile art punches a hole, and the
    console draws the sea separately underneath (cnc_eyes.cpp:4444). The viewer
    needs the same set to know where to put water."""
    if root in _holes:
        return _holes[root]
    from PIL import Image
    src = os.path.join(ATLAS_SRC, root + "_tiles.png")
    im = Image.open(src).convert("RGBA")
    a = im.split()[3]
    th = theater(root)
    out = set()
    for slot in range(th["n4"] + th["n8"]):
        col, row = slot % COLS, slot // COLS
        x0, y0 = col * PITCH + GUTTER, row * PITCH + GUTTER
        if a.crop((x0, y0, x0 + TS, y0 + TS)).getextrema()[0] == 0:
            out.add(slot)
    _holes[root] = out
    return out


def holes_mask(slots, root):
    """64x64 bits, LSB first, one per cell: does this cell's tile show sea."""
    hs = hole_slots(root)
    m = bytearray(512)
    for i in range(4096):
        slot = struct.unpack_from("<H", slots, i * 2)[0]
        if slot in hs:
            m[i >> 3] |= 1 << (i & 7)
    return bytes(m)


def bleed_rgba(im, passes=4):
    """Push colour into the transparent texels, leaving alpha alone.

    Both theater atlases have alpha values of exactly {0, 255} and EVERY alpha-0
    texel is pure black (83,888 of them in TEMPERAT, 108,257 in DESERT). Sample one
    of those under bilinear and the shoreline averages toward black, so every coast
    grows a dark fringe. game/fx_filter.h's fx_bleed_rgba is the shipped answer and
    this is it: four passes, and in each pass a transparent texel whose eight
    neighbours include one that was OPAQUE AT THE START OF THAT PASS takes the mean
    of those neighbours and is itself marked for later passes. The per-pass frontier
    snapshot is the point; without it the flood races in raster order and smears.
    Alpha is never touched, so the artist's cut stays exactly where it was."""
    import numpy as np
    a = np.array(im.convert("RGBA"), dtype=np.int32)
    solid = a[:, :, 3] > 0
    h, w = solid.shape
    for _ in range(passes):
        frontier = solid.copy()
        acc = np.zeros((h, w, 3), np.int32)
        cnt = np.zeros((h, w), np.int32)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dx == 0 and dy == 0:
                    continue
                src = np.roll(np.roll(frontier, dy, 0), dx, 1)
                col = np.roll(np.roll(a[:, :, :3], dy, 0), dx, 1)
                acc += np.where(src[:, :, None], col, 0)
                cnt += src
        fill = (~frontier) & (cnt > 0)
        safe = np.maximum(cnt, 1)[:, :, None]
        a[:, :, :3] = np.where(fill[:, :, None], acc // safe, a[:, :, :3])
        solid = frontier | fill
    from PIL import Image
    return Image.fromarray(a.astype("uint8"), "RGBA")


def briefings():
    """scenario -> its mission briefing, from the cartridge's own ENG/MISSION.ENG.
    Verified byte-identical to the ROM copy. That file is also where the cartridge
    names its own exclusives: the two `;; N64 Special Ops` section headers sit
    directly above [SCG30EA]/[SCG90EA] and [SCB21EA]/[SCB22EB], which is the same
    four the SPECIAL OPS pointer array at ROM 0x21b260 holds."""
    p = os.path.join(EX, "ENG", "MISSION.ENG")
    if not os.path.exists(p):
        return {}, {}
    out, tags, sec, tag = {}, {}, None, ""
    for line in open(p, errors="ignore"):
        t = line.rstrip("\r\n")
        st = t.strip()
        if st.startswith(";"):
            label = st.lstrip("; ").strip()
            if label:
                tag = label
            continue
        if st.startswith("[") and st.endswith("]"):
            sec = st[1:-1].upper()
            out[sec] = []
            tags[sec] = tag
            continue
        if sec and "=" in st:
            out[sec].append(st.split("=", 1)[1])
    return ({k: " ".join(v).replace("  ", " ").strip() for k, v in out.items()},
            tags)


# ---------------------------------------------------------------- names
# These are NOT in the cartridge. The 51 campaign missions have no title in any file
# on this machine (checked against the N64 string table, the DOS one, the BRF scripts
# and every INI), and the four N64 exclusives carry no title either: the cartridge
# labels a campaign row by its number and files the exclusives under the plain header
# ";; N64 Special Ops" in ENG/MISSION.ENG.
#
# So the names below come from the community record, at the project owner's instruction, and the
# viewer marks them as such rather than passing them off as cartridge data. Sources:
# the Command & Conquer Wiki's "GDI/Nod '99 Special Ops M1/M2" pages, which confirm
# the count is four and match the four the ROM's own SPECIAL OPS pointer array holds;
# StrategyWiki and the Wiki's Covert Operations category for the 15 expansion titles;
# and the Wiki's Funpark pages for the dinosaur set.
#
# M1/M2 is positional, taken from the order of the ROM's pointer array at 0x21b260
# (GDI takes entries 0 and 1, Nod entries 2 and 3), not from any string.
EXTERNAL_NAMES = {
    # the four N64 exclusives. SCB21EA's INI still carries the Covert Ops title
    # "Eviction Notice" it was cut from; the cartridge does not use it that way, so
    # the Spec Ops name wins here.
    "SCG30EA": "GDI '99 Special Ops M1",
    "SCG90EA": "GDI '99 Special Ops M2",
    "SCB21EA": "Nod '99 Special Ops M1",
    "SCB22EB": "Nod '99 Special Ops M2",
    # the Funpark set, on the DOS CD only. The record names the first and numbers
    # the rest, so that is what is shown.
    "SCJ01EA": "Funpark mission 1: Strange Behavior",
    "SCJ02EA": "Funpark mission 2",
    "SCJ03EA": "Funpark mission 3",
    "SCJ04EA": "Funpark mission 4",
    "SCJ05EA": "Funpark mission 5",
}
# Covert Operations, the full 15 as the record has them, for cross-checking the INI
# titles the cartridge does carry. 13 of the 15 match a cartridge INI exactly; the
# 14th ("Eviction Notice") is the map the N64 reused for Nod Special Ops M1, and
# "Deceit" is on neither the cartridge nor the project owner's CD.
COVERT_OPS_RECORD = {
    "GDI": ["Blackout", "Hell's Fury", "Infiltrated", "Elemental Imperative",
            "Ground Zero", "Twist of Fate", "Blindsided"],
    "Nod": ["Bad Neighborhood", "Cloak and Dagger", "Deceit", "Eviction Notice",
            "Hostile Takeover", "Nod Death Squad", "The Tiberium Strain",
            "Under Siege"],
}


def label_for(scen, cat, spec):
    """A POSITIONAL label, never a title. Only 31 of the 100 maps have a title in
    any file on this machine, all of them from their own [Basic] Name=. The
    cartridge itself labels a campaign row by its NUMBER (two string-pointer
    arrays at ROM 0x2238FC and 0x223BA0 point at the literals "1","2","3",...),
    so a number is what the viewer shows, marked as a label rather than a name.
    Nothing here is invented; if it cannot be derived it stays empty."""
    if scen in EXTERNAL_NAMES:
        return EXTERNAL_NAMES[scen]
    side = "GDI" if scen[2] == "G" else "Nod"
    n = int(scen[3:5])
    if cat == "singleplayer":
        return f"{side} mission {n}"
    if cat == "specops" and scen in spec:
        i = spec.index(scen)
        return f"Special Ops: {side} {1 if i % 2 == 0 else 2}"
    if cat == "bonus":
        return f"Bonus Mission {n - 59}"
    return ""


def b64(bs):
    return base64.b64encode(bytes(bs)).decode()


def main():
    os.makedirs(OUT, exist_ok=True)
    names = template_names()
    campaign, spec, tail = rom_rosters()
    print(f"ROM rosters: campaign {len(campaign)}, special ops {len(spec)} {spec}, "
          f"debug tail {len(tail)} {tail}")
    model_index, model_bytes = export_assets.export_models(OUT)
    print(f"models: {len(model_index)} meshes, {model_bytes} bytes")
    obj_types = {}          # type code -> index, shared by every map
    brief_text, brief_tag = briefings()
    print(f"briefings: {len(brief_text)} scenarios, "
          f"{sum(1 for v in brief_tag.values() if 'Special Ops' in v)} tagged N64 Special Ops")
    # The cartridge ships GDI and Nod scenarios and nothing else: checked straight
    # against the ROM, which holds zero SCM entries. The shared extracted tree is a
    # working directory a concurrent edit also writes to, and DOS-derived SCM*.MAP
    # files have appeared in it; those are not cartridge files and the DOS half of
    # this exporter reads their originals out of GENERAL.MIX anyway, so anything that
    # is not SCG/SCB is skipped here rather than mislabelled as ROM content.
    all_maps = sorted(f[:-4] for f in os.listdir(os.path.join(EX, "MAP"))
                      if f.endswith(".MAP"))
    scens = [x for x in all_maps if x[:3] in ("SCG", "SCB")]
    skipped = [x for x in all_maps if x not in scens]
    if skipped:
        print(f"skipping {len(skipped)} non-cartridge .MAP in the extracted tree: "
              f"{', '.join(skipped)}")
    index, atlases = [], {}
    for scen in scens:
        d = ini(scen)
        root = theater_root(d)
        th = theater(root)
        raw = open(os.path.join(EX, "MAP", scen + ".MAP"), "rb").read()
        tids = [struct.unpack_from(">H", raw, i * 2)[0] for i in range(64 * 64)]

        # No own .IMG means the engine loads FLAT.IMG, whose 4225-byte payload is
        # all zero (checked straight out of the ROM, usize 4241 = 16 + 4225). The
        # extracted tree's FLAT.IMG is a stale still-compressed blob, so it is not
        # read here; zeros are the same answer and cannot be stale.
        himg = os.path.join(EX, "IMG", scen + ".IMG")
        own = os.path.exists(himg)
        if own:
            blob = open(himg, "rb").read()
            hdr, _b, w, h = struct.unpack_from(">IIHH", blob, 0)
            fmt, depth = blob[12], blob[13]
            assert (hdr, w, h, fmt, depth) == (0x10, 65, 65, 4, 1), \
                (scen, w, h, fmt, depth)
            heights = blob[16:16 + 65 * 65]
        else:
            heights = bytes(65 * 65)

        slots, tmpls = bytearray(), bytearray()
        for i, tid in enumerate(tids):
            x, y = i % 64, i // 64
            if tid == 0xFFFF or not (tid < th["n4"] or
                                     BANK8_BASE <= tid < BANK8_BASE + th["n8"]):
                draw, t = (x & 3) | ((y & 3) << 2), 255
            else:
                draw = tid
                ti = tmpl_of(th, tid)
                t = ti[0] if ti else 255
            slot = draw if draw < th["n4"] else th["n4"] + (draw - BANK8_BASE)
            slots += struct.pack("<H", slot)
            tmpls.append(t)

        objs, obj_counts = export_assets.objects_of(
            os.path.join(EX, "INI", scen + ".INI"), obj_types)

        X = int(d.get("MAP.X", 0) or 0); Y = int(d.get("MAP.Y", 0) or 0)
        W = int(d.get("MAP.WIDTH", 64) or 64); H = int(d.get("MAP.HEIGHT", 64) or 64)
        json.dump(dict(
            scenario=scen, theater=root, ownHeightmap=own,
            source=(scen + ".IMG" if own else "FLAT.IMG (engine fallback, all zero)"),
            playable=[X, Y, W, H],
            atlas=root + "_tiles.png",
            atlasWidth=COLS * PITCH, atlasHeight=th["rows"] * PITCH,
            tileSize=TS, gutter=GUTTER, cols=COLS,
            heights=b64(heights), tiles=b64(slots), tmpl=b64(tmpls),
            objects=b64(objs), objectCounts=obj_counts,
            holes=b64(holes_mask(bytes(slots), root)),
            briefing=brief_text.get(scen, ""), briefingTag=brief_tag.get(scen, ""),
        ), open(os.path.join(OUT, scen + ".json"), "w"), separators=(",", ":"))
        atlases[root] = True

        inner = [heights[y * 65 + x] for y in range(Y, min(65, Y + H + 1))
                 for x in range(X, min(65, X + W + 1))] or [0]
        side = "GDI" if scen[2] == "G" else "Nod"
        index.append(dict(
            id=scen, side=side, theater=root, ownHeightmap=own, media="rom",
            name=("" if scen in EXTERNAL_NAMES
                  else d.get("BASIC.NAME", "").strip()),
            label=label_for(scen, category(scen, campaign, spec, tail,
                                           bool(d.get("BASIC.NAME", "").strip())),
                            spec),
            category=category(scen, campaign, spec, tail,
                              bool(d.get("BASIC.NAME", "").strip())),
            multiplayer=False, dinosaur=False,
            mission=scen[3:5], variant=scen[5:],
            brief=d.get("BASIC.BRIEF", ""), playable=[X, Y, W, H],
            minH=min(inner), maxH=max(inner),
            relief=max(inner) - min(inner),
            hasBriefing=bool(brief_text.get(scen)),
            objects=sum(obj_counts.values())))

    # ---- the DOS half: the SCM multiplayer maps and the SCJ dinosaur missions,
    # which the cartridge never carried. See export_dos.py for the formats.
    for m in export_dos.scenarios():
        scen = m["scenario"]
        objs, obj_counts = export_assets.objects_from(m["ini"], obj_types)
        heights = bytes(65 * 65)          # DOS maps have no elevation, and cannot
        json.dump(dict(
            scenario=scen, theater=m["theater"], ownHeightmap=False,
            source=m["source"], playable=m["playable"],
            atlas=m["theater"] + "_tiles.png",
            atlasWidth=COLS * PITCH, atlasHeight=theater(m["theater"])["rows"] * PITCH,
            tileSize=TS, gutter=GUTTER, cols=COLS,
            heights=b64(heights), tiles=b64(m["tiles"]), tmpl=b64(m["tmpl"]),
            objects=b64(objs), objectCounts=obj_counts, briefing="", briefingTag="",
            holes=b64(holes_mask(m["tiles"], m["theater"])),
            dosTheater=m["theaterDos"], unresolvedTiles=m["unresolved"],
        ), open(os.path.join(OUT, scen + ".json"), "w"), separators=(",", ":"))
        atlases[m["theater"]] = True
        index.append(dict(
            id=scen, side=m["side"], theater=m["theater"], ownHeightmap=False,
            category=m["category"], multiplayer=(m["category"] == "multiplayer"),
            dinosaur=m["dinosaur"], dosTheater=m["theaterDos"],
            mission=scen[3:5], variant=scen[5:], brief="",
            name=m["name"], media="dosdata",
            label=EXTERNAL_NAMES.get(scen, ""),
            playable=m["playable"], minH=0, maxH=0, relief=0,
            objects=sum(obj_counts.values())))

    for root in atlases:
        from PIL import Image
        src = Image.open(os.path.join(ATLAS_SRC, root + "_tiles.png"))
        bleed_rgba(src).save(os.path.join(OUT, root + "_tiles.png"))
    json.dump(dict(maps=index, templates=names,
                   covertOpsRecord=COVERT_OPS_RECORD,
                   families=list(FAMILIES),
                   familyOf=[family(n) for n in names],
                   objectKinds=export_assets.KINDS,
                   houses=export_assets.HOUSES,
                   objectTypes=[t for t, _ in sorted(obj_types.items(),
                                                     key=lambda kv: kv[1])],
                   tiberium=sorted(export_assets.TIBERIUM),
                   walls=sorted(export_assets.WALLS),
                   crates=sorted(export_assets.CRATES)),
              open(os.path.join(OUT, "index.json"), "w"), separators=(",", ":"))
    print(f"{len(index)} maps -> {OUT}   "
          f"({sum(1 for m in index if m['ownHeightmap'])} with their own heightmap)")


if __name__ == "__main__":
    main()
