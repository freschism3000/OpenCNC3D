# C&C3D sidebar HUD - art pieces and pixel rules

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
