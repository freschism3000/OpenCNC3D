#!/usr/bin/env python3
"""
CNC3D -- the cartridge's sidebar CAMEOS.

Every cameo the N64 build of C&C can draw on its sidebar, straight out of the ROM.

WHERE THEY LIVE
---------------
Two different things in the ROM are both called "the cameo table", and confusing them
costs an afternoon:

1. **The art** is 102 records in the MAIN directory table (dir 0x0460540, data base
   0x0468A90). These carry real offsets and real sizes. 101 distinct names; CAMMSAM2.IMG
   is stored TWICE, back to back, byte identical, so the cartridge itself ships one
   redundant copy.

2. **The order** is two 53-record tables at dir 0x019EE70 and 0x019F380 whose offset,
   size and method words are all ZERO. They are not broken records and they hold no data:
   they are the sidebar's own name lists, in sidebar slot order, one per colourway.
   Table 0x019EE70 is the `CAM_*` set, table 0x019F380 the `CAM*2` set. The selector at
   RAM 0x8002BB4C reads character 2 of the scenario name: 'G' (SCG01EA, GDI campaign)
   picks the first, anything else picks the second -- the same switch that chooses the
   infantry colourway (docs/re-findings.md, "Which combat palette is which side").

   The two lists are positionally aligned: slot i of one is the same buildable as slot i
   of the other. Slot 0 is CAM_BLNK (the empty well) and slot 52 is CAM_SLCT in BOTH
   lists, i.e. those two are shared rather than duplicated per side. Slots 9, 10 and 41
   repeat CAM_SILO, so 53 slots resolve to 50 distinct pictures per side.

HOW MUCH THE TWO SIDES ACTUALLY DIFFER
--------------------------------------
Of the 50 distinct slots, **41 are genuinely different art** and 9 are the same picture:

    slot 0, 30, 52     CAM_BLNK, CAM_RMBO, CAM_SLCT -- one name, used by both lists
    slot 1, 2, 3       SBAG, CYCL, BRIK  -- byte identical under two names
    slot 28, 50, 51    E5, ION, ATOM     -- byte identical under two names

The `CAM*2` set is NOT a palette swap of `CAM_*`. Both the livery and the BACKGROUND are
redrawn: the GDI set sits on green grass with tan/gold vehicles, the Nod set on brown
earth with grey/red ones. Zero pairs decode to the same pixels under different palettes.
So a renderer must load the side's own file; it cannot recolour one set into the other.

Two pictures are reused WITHIN a side as well, so 101 names hold only **91 distinct
images**: concrete borrows the brick wall on both sides (CAM_BRIK = CAM_CONC =
CAMBRIK2 = CAMCONC2), and the Rocket Launcher and Mobile SAM share one cameo on both
sides (CAM_MLRS = CAM_MSAM, CAMMLRS2 = CAMMSAM2). Walls and superweapons are
side neutral; anything with a chassis or a uniform is not.

Every cameo is FULLY OPAQUE. Not one transparent pixel in all 101, including CAM_SLCT
(the red selection frame, which is a red border around solid black, not a see-through
overlay) and CAM_BLNK. Anything drawn over a cameo has to be composited by the renderer.

FORMAT
------
Ordinary .IMG files (see img.py); nothing here is compressed. All 32x25 except
CAM_RMBO.IMG, which is 32x24 -- one row shorter, in the cartridge, not a decode error.
Almost all are CI8 with a trimmed RGBA5551 palette (117..256 entries). Three exceptions,
all genuine:

    CAM_BLNK.IMG   RGBA5551 direct colour, no palette   (the empty well plate)
    CAM_SSM.IMG    CI4, 16-entry palette                (the cut SSM launcher)
    CAM_RMBO.IMG   CI8 but 32x24

FOUR ORPHANS
------------
CAM_FACT.IMG, CAM_CONC.IMG, CAMCONC2.IMG and CAM_SSM.IMG are present as art but named by
NEITHER order list, so the shipped sidebar can never show them: the Construction Yard,
concrete (both colourways) and the SSM launcher. Extracted anyway; flagged in `list`.

Usage:
    python3 cameos.py <rom> list                  # every record, with format and slot
    python3 cameos.py <rom> png <outdir>          # write all 101 as RGBA PNGs
    python3 cameos.py <rom> order [out.json]      # the two sidebar order lists
    python3 cameos.py <rom> sheet <out.png> [scale]   # labelled contact sheet
    python3 cameos.py <rom> check                 # self-test, exit 1 on any problem

Read-only on the ROM.
"""
import sys, os, json, hashlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import archive
import img as imglib
from jim import write_png

MAIN_DIR = 0x0460540          # the art: real offsets, 102 records
ORDER_GDI = 0x019EE70         # name-only list, CAM_*  set, sidebar slot order
ORDER_NOD = 0x019F380         # name-only list, CAM*2   set, sidebar slot order

# Named by neither order list. See the module docstring.
ORPHANS = ("CAM_FACT.IMG", "CAM_CONC.IMG", "CAMCONC2.IMG", "CAM_SSM.IMG")


def _arc(d, dir_start):
    for a in archive.all_archives(d):
        if a["dir"] == dir_start:
            return a
    raise SystemExit("directory table %#x not found in this ROM" % dir_start)


def records(d):
    """Every CAM* art record in the main table, in ROM order, with its bytes decoded."""
    a = _arc(d, MAIN_DIR)
    out = []
    for r in a["recs"]:
        if not r["name"].upper().startswith("CAM"):
            continue
        if r["method"]:
            raise SystemExit("unexpected compression on %s (%#x)" % (r["name"], r["method"]))
        blob = d[a["base"] + r["off"]: a["base"] + r["off"] + r["usize"]]
        m = imglib.solve(blob)
        out.append(dict(name=r["name"], rom=a["base"] + r["off"], size=r["usize"],
                        blob=blob, meta=m, md5=hashlib.md5(blob).hexdigest()))
    return out


def order_lists(d):
    """The two sidebar name lists, in slot order. Returns {'gdi': [...], 'nod': [...]}."""
    return {"gdi": [r["name"] for r in _arc(d, ORDER_GDI)["recs"]],
            "nod": [r["name"] for r in _arc(d, ORDER_NOD)["recs"]]}


def _slot_index(orders):
    """name -> 'G12' / 'N07' / 'G12 N12', or '' for an orphan."""
    where = {}
    for tag, key in (("G", "gdi"), ("N", "nod")):
        for i, n in enumerate(orders[key]):
            where.setdefault(n, []).append("%s%02d" % (tag, i))
    return {n: " ".join(v) for n, v in where.items()}


def cmd_list(d):
    recs = records(d)
    slots = _slot_index(order_lists(d))
    seen = {}
    print("%-13s %10s %6s %5s %4s %5s %5s  %s"
          % ("name", "rom", "bytes", "wxh", "bpp", "pal", "slots", "note"))
    for r in recs:
        m = r["meta"]
        note = []
        if r["name"] in ORPHANS:
            note.append("ORPHAN, no sidebar slot")
        if r["md5"] in seen:
            note.append("DUPLICATE of the copy at %#x" % seen[r["md5"]])
        else:
            seen[r["md5"]] = r["rom"]
        if not m["ok"]:
            note.append("UNSOLVED HEADER")
        print("%-13s %#10x %6d %2dx%-2d %4s %5s %5s  %s"
              % (r["name"], r["rom"], r["size"], m["w"], m["h"],
                 m["bpp"], m["palc"], slots.get(r["name"], "-"), ", ".join(note)))
    uniq = {r["md5"] for r in recs}
    print("\n%d records, %d distinct names, %d distinct images"
          % (len(recs), len({r["name"] for r in recs}), len(uniq)))


def cmd_png(d, outdir):
    os.makedirs(outdir, exist_ok=True)
    n = 0
    for r in records(d):
        res = imglib.decode(r["blob"])
        if res is None:
            print("SKIP (unsolved) %s" % r["name"])
            continue
        m, rows = res
        dst = os.path.join(outdir, r["name"][:-4] + ".png")
        if os.path.exists(dst):
            continue                      # the one duplicated record, already written
        write_png(dst, m["w"], m["h"], rows)
        n += 1
    print("wrote %d PNGs -> %s" % (n, outdir))
    return n


def cmd_order(d, dst=None):
    orders = order_lists(d)
    out = {"note": "sidebar slot order; slot i is the same buildable in both lists",
           "selector": "RAM 0x8002BB4C reads scenario name char 2: 'G' -> gdi, else nod",
           "slots": [{"slot": i, "gdi": g, "nod": n}
                     for i, (g, n) in enumerate(zip(orders["gdi"], orders["nod"]))]}
    text = json.dumps(out, indent=2)
    if dst:
        open(dst, "w").write(text + "\n")
        print("wrote", dst)
    else:
        print(text)
    return out


def cmd_sheet(d, dst, scale=4):
    from PIL import Image, ImageDraw
    recs = [r for r in records(d) if r["meta"]["ok"]]
    by_name = {}
    for r in recs:
        by_name.setdefault(r["name"], r)
    names = sorted(by_name)
    cols, pad, label = 8, 10, 12
    cw = 32 * scale + pad
    ch = 25 * scale + pad + label
    rows_n = (len(names) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cw + pad, rows_n * ch + pad), (24, 24, 28))
    dr = ImageDraw.Draw(sheet)
    for i, name in enumerate(names):
        r = by_name[name]
        m, prows = imglib.decode(r["blob"])
        im = Image.frombytes("RGBA", (m["w"], m["h"]), b"".join(prows))
        im = im.resize((m["w"] * scale, m["h"] * scale), Image.NEAREST)
        x = pad + (i % cols) * cw
        y = pad + (i // cols) * ch
        sheet.paste(im, (x, y), im)
        dr.text((x, y + 25 * scale + 2), name[:-4], fill=(200, 200, 205))
    sheet.save(dst)
    print("wrote %s (%d cameos, %dx)" % (dst, len(names), scale))


def cmd_check(d):
    """Self-test. Prints every assertion and its result; exits 1 if any fails."""
    recs = records(d)
    orders = order_lists(d)
    names = {r["name"] for r in recs}
    fails = []

    def ok(label, cond, detail=""):
        print("%-58s %s %s" % (label, "PASS" if cond else "FAIL", detail))
        if not cond:
            fails.append(label)

    ok("102 art records in the main table", len(recs) == 102, "got %d" % len(recs))
    ok("101 distinct names", len(names) == 101, "got %d" % len(names))
    ok("every header solves", all(r["meta"]["ok"] for r in recs))
    ok("nothing is compressed", True)          # records() raises if it ever is
    ok("both order lists are 53 slots",
       len(orders["gdi"]) == 53 and len(orders["nod"]) == 53,
       "%d / %d" % (len(orders["gdi"]), len(orders["nod"])))
    missing = sorted((set(orders["gdi"]) | set(orders["nod"])) - names)
    ok("every ordered name has art", not missing, ",".join(missing))
    ok("slot 0 and slot 52 are shared by both sides",
       orders["gdi"][0] == orders["nod"][0] == "CAM_BLNK.IMG"
       and orders["gdi"][52] == orders["nod"][52] == "CAM_SLCT.IMG")
    orphans = sorted(names - (set(orders["gdi"]) | set(orders["nod"])))
    ok("the four known orphans, no more", orphans == sorted(ORPHANS), ",".join(orphans))
    dupes = [n for n in names if sum(1 for r in recs if r["name"] == n) > 1]
    ok("exactly one duplicated record", dupes == ["CAMMSAM2.IMG"], ",".join(dupes))
    d_blobs = [r["md5"] for r in recs if r["name"] == "CAMMSAM2.IMG"]
    ok("the duplicate is byte identical", len(set(d_blobs)) == 1)
    odd = sorted(r["name"] for r in recs
                 if (r["meta"]["w"], r["meta"]["h"]) != (32, 25))
    ok("only CAM_RMBO departs from 32x25", odd == ["CAM_RMBO.IMG"], ",".join(odd))

    groups = {}
    for r in recs:
        groups.setdefault(r["md5"], set()).add(r["name"])
    ok("101 names hold 91 distinct images", len(groups) == 91, "got %d" % len(groups))
    shared = sorted(tuple(sorted(v)) for v in groups.values() if len(v) > 1)
    expect = sorted([
        ("CAMATOM2.IMG", "CAM_ATOM.IMG"),
        ("CAMBRIK2.IMG", "CAMCONC2.IMG", "CAM_BRIK.IMG", "CAM_CONC.IMG"),
        ("CAMCYCL2.IMG", "CAM_CYCL.IMG"),
        ("CAME52.IMG", "CAM_E5.IMG"),
        ("CAMION2.IMG", "CAM_ION.IMG"),
        ("CAMMLRS2.IMG", "CAMMSAM2.IMG"),
        ("CAMSBAG2.IMG", "CAM_SBAG.IMG"),
        ("CAM_MLRS.IMG", "CAM_MSAM.IMG"),
    ])
    ok("the eight byte-identical groups are exactly these", shared == expect,
       "" if shared == expect else str(shared))

    # No cameo carries a transparent pixel; anything drawn over one is a composite.
    trans = []
    for r in recs:
        res = imglib.decode(r["blob"])
        if res and any(px[i] == 0 for px in (b"".join(res[1]),) for i in range(3, len(px), 4)):
            trans.append(r["name"])
    ok("every cameo is fully opaque", not trans, ",".join(sorted(set(trans))))

    print("\n%d checks, %d failed" % (14, len(fails)))
    return 1 if fails else 0


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    rom, cmd = sys.argv[1], sys.argv[2]
    d = archive.load(rom)
    if cmd == "list":
        cmd_list(d)
    elif cmd == "png":
        cmd_png(d, sys.argv[3])
    elif cmd == "order":
        cmd_order(d, sys.argv[4] if len(sys.argv) > 4 else
                  (sys.argv[3] if len(sys.argv) > 3 else None))
    elif cmd == "sheet":
        cmd_sheet(d, sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 4)
    elif cmd == "check":
        return cmd_check(d)
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
