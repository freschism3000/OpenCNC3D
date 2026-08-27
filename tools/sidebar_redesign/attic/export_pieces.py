"""Export the Machined HUD chrome (PREVIOUS PASS) as individual editable PNGs."""
from PIL import Image, ImageDraw, ImageFont
import numpy as np, os, shutil, sys
sys.path.insert(0, ".")
import kit as K, hires as X

T = X.DESIGNS["H1-machined"]
W, H = X.W, X.H
ROOT = "cnc3d-hud-pieces"
if os.path.isdir(ROOT): shutil.rmtree(ROOT)
for d in ("pieces", "reference"): os.makedirs(f"{ROOT}/{d}")
def save(im, n): im.save(f"{ROOT}/pieces/{n}.png"); return im

# panels
body = X.new(W, H, T["face"])
X.vgrad(body, (0, 0, W-1, H-1), T["gtop"], T["gbot"])
save(body, "panel_body")
save(X.header(T), "panel_header")
save(X.radar(T, with_coin=False), "panel_radar_bezel")

# buttons
for x0, x1, lab in X.BTN_BOXES: save(X.button(T, lab, x1-x0+1), f"button_{lab.lower()}")

# cells
save(X.cell(T, True),  "cell_well")
save(X.cell(T, False), "cell_frame")

# arrows
for up, nm in ((True, "up"), (False, "down")):
    save(X.arrow(T, up, False), f"arrow_{nm}")
    save(X.arrow(T, up, True),  f"arrow_{nm}_pressed")

# meter
save(X.meter(T), "meter_assembled")
save(X.meter(T, level=0.0, mark=-1.0), "meter_empty")

# engraved text, as its own piece
def text_piece(mask):
    h, w = mask.shape
    im = Image.new("RGBA", (w, h+1), (0, 0, 0, 0))
    X.stamp(im, mask, 0, 1, T["hi1"] + (255,))
    X.stamp(im, mask, 0, 0, T["lo2"] + (255,))
    return im
save(text_piece(X.TXT), "text_sidebar")
for lab in ("REPAIR", "SELL", "MAP"): save(text_piece(X.LABEL[lab]), f"text_{lab.lower()}")

# references
clean = X.build(T, with_coin=False, with_cameos=False)
clean.save(f"{ROOT}/reference/hud_reference.png")
clean.resize((W*3, H*3), Image.NEAREST).save(f"{ROOT}/reference/hud_reference@3x.png")
X.build(T).save(f"{ROOT}/reference/hud_with_placeholder_art.png")

def fnt(b, s):
    p = "/System/Library/Fonts/Supplemental/Courier New%s.ttf" % (" Bold" if b else "")
    try: return ImageFont.truetype(p, s)
    except Exception: return ImageFont.load_default()
FB, FSm = fnt(True, 13), fnt(False, 10)

SWATCH = [("face   panel body", T["face"]), ("gtop   gradient top", T["gtop"]),
          ("gbot   gradient bottom", T["gbot"]),
          ("hi[0]  lit edge", T["hi"][0]), ("hi[1]  lit edge 2", T["hi"][1]),
          ("hi[2]  lit edge 3", T["hi"][2]),
          ("lo[0]  shadow edge", T["lo"][0]), ("lo[1]  shadow edge 2", T["lo"][1]),
          ("lo[2]  shadow edge 3", T["lo"][2]),
          ("mid    corner/neutral", T["mid"]), ("well   cell floor", T["well"]),
          ("screen radar interior", T["screen"]), ("ink    engraved light", T["ink"]),
          ("accent power bar", T["accent"]), ("accent2 tick marker", T["accent2"])]
RH, SW = 26, 150
img = Image.new("RGB", (560, 24 + RH*len(SWATCH)), (18,18,22)); d = ImageDraw.Draw(img)
d.text((10, 6), "H1 MACHINED  palette", fill=(255,215,120), font=FB)
for i, (nm, c) in enumerate(SWATCH):
    y = 24 + i*RH
    d.rectangle([10, y, 10+SW, y+RH-5], fill=c)
    lum = 0.3*c[0]+0.59*c[1]+0.11*c[2]
    d.text((16, y+5), "#%02x%02x%02x" % c, fill=(0,0,0) if lum > 140 else (240,240,240), font=FB)
    d.text((SW+24, y+5), nm, fill=(215,215,220), font=FB)
img.save(f"{ROOT}/reference/palette.png")

# contact sheet, shelf-packed so one 160x480 panel does not blow out the grid
names = sorted(os.listdir(f"{ROOT}/pieces"))
ims = [(n[:-4], Image.open(f"{ROOT}/pieces/{n}")) for n in names]
SC, PAD, LBL, MAXW = 2, 20, 15, 1500
def board(w, h):
    b = Image.new("RGB", (w, h), (52,52,60)); bd = ImageDraw.Draw(b)
    for yy in range(0, h, 8):
        for xx in range(0, w, 8):
            if (xx//8+yy//8) % 2: bd.rectangle([xx,yy,xx+7,yy+7], fill=(40,40,46))
    return b
order = sorted(ims, key=lambda p: -p[1].height)
rows, cur, cw = [], [], 0
for nm, im in order:
    w = im.width*SC + PAD
    if cur and cw + w > MAXW: rows.append(cur); cur, cw = [], 0
    cur.append((nm, im)); cw += w
if cur: rows.append(cur)
tot_h = sum(max(i.height for _, i in r)*SC + PAD + LBL + 12 for r in rows) + PAD
sheet = Image.new("RGB", (MAXW + PAD, tot_h), (18,18,22)); sd = ImageDraw.Draw(sheet)
y = PAD
for r in rows:
    x = PAD
    rh = max(i.height for _, i in r)*SC
    for nm, im in r:
        big = im.resize((im.width*SC, im.height*SC), Image.NEAREST)
        bg = board(big.width, big.height); bg.paste(big, (0,0), big)
        sheet.paste(bg, (x, y + LBL))
        lbl = "%s %dx%d" % (nm, im.width, im.height)
        avail = im.width*SC + PAD - 2
        while lbl and sd.textlength(lbl, font=FSm) > avail:
            lbl = lbl[:-1]
        sd.text((x, y+1), lbl, fill=(255,215,120), font=FSm)
        if sd.textlength("%s %dx%d" % (nm, im.width, im.height), font=FSm) > avail:
            sd.text((x, y + LBL + im.height*SC + 1), "%dx%d" % (im.width, im.height),
                    fill=(150,150,160), font=FSm)
        x += im.width*SC + PAD
    y += rh + PAD + LBL + 12
sheet.save(f"{ROOT}/reference/pieces_contact.png")
print("exported", len(ims), "pieces")
for nm, im in ims: print("  %-22s %dx%d" % (nm, im.width, im.height))

# ---- the rules doc is WRITTEN BY THIS SCRIPT, so re-running can never lose it again
README = """# C&C3D sidebar HUD - art pieces and pixel rules

Design **H1 "Machined"**, the generated chrome set. These are chrome only: panels,
frames, buttons, arrows, meter and text. **No cameos and no faction emblem.**

> **Status.** This set has since been superseded in review's externally-generated art.
> For the layout actually in use, see `LAYOUT.md` in `cnc3d-hud-assembled.zip`, which is
> measured off that art. The **palette in section 4 below describes THIS generated set
> only** and does not match the newer dark-gunmetal art. Sections 1, 2, 3, 6 and 7 are
> general and still apply to any piece you draw.

---

## 1. Target

| | |
|---|---|
| Screen | **640x480** |
| Colour | **16-bit** (RGB565), the Tier 1 / Voodoo 2 floor |
| Sidebar | **160x480**, anchored right at screen **x = 480, y = 0** |
| Pixels | **square**, unlike the 320x200 DOS original |

16-bit means you are not restricted to an indexed palette, so gradients are allowed. But
RGB565 gives green 6 bits and red/blue only 5, so long smooth ramps **band visibly**.
Every gradient in these pieces is 2x2 ordered-dithered for that reason.

## 2. The three hard rules

**1. Sizes are locked.** Every layout position is absolute. A piece that changes size
lands in the wrong place. Edit the pixels, never the canvas.

**2. Alpha 0 means transparent.** `cell_frame` is a ring with a hollow centre; that hole
is where the cameo shows through.

**3. One light, from the top-left.** Top faces light, left faces slightly less, right
faces dark, bottom faces darkest. New forms lit any other way read as pasted on.

## 3. Layout (this generated set)

| piece | size | position (x, y) |
|---|---|---|
| `panel_body` | 160x480 | 0, 0 |
| `panel_header` | 160x18 | 0, 0 |
| *(separator)* | 160x1 | 0, 18 |
| `panel_radar_bezel` | 160x160 | 0, 19 |
| `button_repair` | 63x22 | 4, 184 |
| `button_sell` | 41x22 | 70, 184 |
| `button_map` | 42x22 | 114, 184 |
| `meter_assembled` | 18x240 | 0, 210 |
| `cell_well` / `cell_frame` | 64x48 | grid below |
| `arrow_up` | 32x24 | 20, 454 and 90, 454 |
| `arrow_down` | 32x24 | 52, 454 and 122, 454 |

Cell grid: columns **x = 18, 88**; rows **y = 210, 258, 306, 354, 402**.
Draw order per slot: `cell_well`, then cameo, then `cell_frame`.

Radar screen opening: **x 12..147, y 31..150**, so **136x120**.

## 4. Palette (this generated set only - NOT the newer art)

| role | hex | used for |
|---|---|---|
| `face` | `#8a93a0` | flat panel body |
| `gtop` / `gbot` | `#a2acba` / `#6e7784` | panel gradient, top to bottom |
| `hi[0]` `hi[1]` `hi[2]` | `#f2f7ff` `#c4cdda` `#a6afbc` | lit edges, step 1 / 2 / 3 |
| `lo[0]` `lo[1]` `lo[2]` | `#3e4652` `#2a313b` `#181d25` | shadow edges, step 1 / 2 / 3 |
| `mid` | `#6a7380` | corners where lit meets shadow |
| `well` | `#333a44` | cell floor |
| `screen` | `#0a0e13` | radar interior |
| `ink` | `#f2f7ff` | engraved text, lit row |
| `accent` | `#35d066` | power bar fill |
| `accent2` | `#ffc83a` | power tick marker |

See `reference/palette.png`.

## 5. The two slots left empty

**Radar screen**, `x 12..147, y 31..150`, so **136x120**. Anything drawn there is clipped
by the bezel.

**Cameo slots**, each a full **64x48** at the cell origin. `cell_frame` then overlays
about 3px on every edge, so keep the outer ~3px clear of anything that must be seen.

## 6. How the bevels are built

- Raised plates have **chamfered corners**, cut at 45 degrees, 3 to 9 px by piece.
- The edge steps inward: outermost row `hi[0]` or `lo[0]`, next `hi[1]` or `lo[1]`, then
  it meets the flat face.
- Which ramp an edge uses depends on which way it faces: **top and left take `hi`, bottom
  and right take `lo`**, and the diagonal chamfers take `mid`.
- A **recess** (cell wells, radar opening, meter channel) is that exact rule inverted:
  top and left take `lo`, bottom and right take `hi`. **That inversion is the only thing
  making a hole read as a hole instead of a bump.**
- **Engraved text** is two rows: the glyph in `lo[2]`, the same glyph one pixel below in
  `ink`. That is what makes it look cut in rather than printed on.
- The panel face carries faint **vertical brush striations**, about one column in ten
  shifted a single step. Sparse and deliberate; uniform noise reads as dirt.

## 7. Things that will look wrong

- **Pillow shading**, glowing evenly around an outline instead of picking a light
  direction. The most common tell.
- **Undithered gradients**, which band on 16-bit hardware.
- **Resampling** art with a smooth filter next to crisp pixels. Redraw, or scale by a
  whole number.
- **Changing a canvas size.** See rule 1.

## 8. Files

```
pieces/      18 PNGs, exact size, alpha preserved - the editable art
reference/
  hud_reference.png             160x480 assembled, no cameos, no emblem
  hud_reference@3x.png          the same at 3x
  hud_with_placeholder_art.png  WITH stand-in cameos, for scale only
  palette.png                   labelled swatches
  pieces_contact.png            every piece, labelled, on a checkerboard
```
"""
open(f"{ROOT}/README.md", "w").write(README)
print("wrote README.md")
