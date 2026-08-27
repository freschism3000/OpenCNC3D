# C&C3D Sidebar HUD — Art Production Spec

**Audience:** the artist or agent producing the image assets.
**You do not need any prior context.** Everything required is in this document.

---

## 1. What this is

A command sidebar for a real-time strategy game in the style of *Command & Conquer*
(1995). It runs down the **right edge of a 640x480 screen**, occupying a strip
**160 pixels wide by 480 pixels tall**.

It contains, top to bottom: a title strip, a radar/minimap housing, three command
buttons, a vertical power meter, a grid of eight build slots, and four scroll arrows.

**Visual style:** heavy dark gunmetal industrial panelling. Machined bevels, chamfered
(45°-cut) corners, recessed wells, small **amber indicator lamps**, and a **green**
segmented power bar. Think armoured military hardware, not glossy consumer UI.

---

## 2. Hard rules — assets are rejected if these are broken

1. **Exact canvas size.** Every asset must be delivered at precisely the pixel dimensions
   in section 5. Not approximately. Not a different aspect ratio to be resized later.
   These assets are composited 1:1 at runtime and are never scaled.

2. **Transparent background, real alpha channel.** PNG with alpha. Not white, not black,
   not a checkerboard. Anything outside the object's silhouette must be alpha 0.

3. **Nothing may extend past the canvas edge.** No drop shadows, no glows bleeding
   outside. If the design has a shadow, it must fit inside the stated size.

4. **No soft downscaling.** See section 3. This is the most common failure.

5. **One asset per file**, named exactly as in section 5, lowercase, `.png`.

---

## 3. How to produce crisp art at these sizes (important)

Most of these assets are small — the smallest is 32x30. Generating a large smooth image
and shrinking it produces a blurry, muddy result that looks wrong next to crisp UI.

**Do one of these:**

- **Preferred:** author directly at the target size as pixel art, placing pixels
  deliberately. Hard edges, no anti-aliasing against transparency.
- **Acceptable:** work at an exact **integer multiple** of the target (4x, 8x) with
  hard-edged, blocky forms and no soft gradients, then downscale by that exact integer
  using **nearest-neighbour** only.

**Do not** generate at an arbitrary large resolution (1024x1024 or similar) and resample
down with bilinear/bicubic/Lanczos. That is what produced the previous unusable set.

Edges against transparency must be hard. A halo of semi-transparent pixels will show as a
grey fringe when composited.

---

## 4. Palette

Sampled from the approved existing artwork. Match this range.

| role | hex | use |
|---|---|---|
| deepest shadow / outline | `#000000` – `#101010` | outlines, recess floors |
| dark panel body | `#181818` – `#282828` | main panel faces |
| mid metal | `#303030` – `#404038` | raised surfaces |
| light metal | `#484840` – `#585850` | lit bevel faces |
| highlight | `#d8d8d0` – `#e8e0d8` | top-left bevel edges, specular hits |
| **amber lamp** | `#f0b800` – `#f8e800` | indicator lamps, active accents |
| **power green** | `#00b010` – `#00c020` | power bar fill segments |

The metal has a very slightly **warm/olive** cast at the mid tones (note `#404038`,
`#383830`), not pure neutral grey. Keep that.

**Lighting:** single light source from the **top-left**. Top and left faces catch the
highlight; bottom and right faces fall into shadow. Apply consistently to every asset.

---

## 5. Asset list

Deliver all 20. Sizes are **exact**.

### Panels

| filename | size | description |
|---|---|---|
| `panel_body.png` | **160x480** | The full backing panel. A plain dark chassis with a subtle machined texture and a thin raised border. It sits behind everything else, so it must have **no radar hole and no cell grid** drawn into it. |
| `panel_header.png` | **160x23** | Title strip across the very top. Reads `SIDEBAR` in a blocky pixel font, centred, with a small amber lamp at each end. |
| `panel_radar_bezel.png` | **160x160** | The radar housing. A heavy bevelled frame with a **128x128 opening at (16,16)**. The opening must be filled with **opaque near-black** (`#000000`–`#080808`), not transparency — the map is drawn on top of it at runtime. Add corner screws and a vent grille below the opening if space allows. |

### Buttons

| filename | size | description |
|---|---|---|
| `button_repair.png` | **43x26** | Raised chamfered pad, engraved label `REPAIR`, small amber lamp beneath the text. |
| `button_sell.png` | **43x26** | Same, label `SELL`. |
| `button_map.png` | **39x26** | Same, label `MAP`. |
| `button_repair_pressed.png` | **43x26** | Pressed state: bevel inverted (recessed), face darker, lamp brighter. |
| `button_sell_pressed.png` | **43x26** | Pressed state. |
| `button_map_pressed.png` | **39x26** | Pressed state. |

### Power meter

| filename | size | description |
|---|---|---|
| `meter_empty.png` | **26x192** | A tall recessed channel, empty. Segmented ticks running its length, unlit. |
| `meter_full.png` | **26x192** | Identical channel, every segment lit **green**. Segments must be **evenly spaced with a consistent vertical pitch**, because the game tiles them to show partial fill. |

### Build slots

| filename | size | description |
|---|---|---|
| `cell_well.png` | **64x48** | An empty build slot: a recessed rectangular well with a bevelled edge, dark floor, small amber corner accents. |
| `cell_frame.png` | **64x48** | The **overlay** for a filled slot. A frame with a **fully transparent centre** — the unit icon shows through the hole. Border must be **3 pixels or less** on every side, leaving a **58x42** transparent opening at offset (3,3). |

### Scroll arrows

| filename | size | description |
|---|---|---|
| `arrow_up.png` | **32x30** | Square-ish chamfered pad with a chevron pointing **up**, amber lamp at the base. |
| `arrow_down.png` | **32x30** | Same, chevron pointing **down**. |
| `arrow_up_pressed.png` | **32x30** | Pressed: bevel inverted, face darker. |
| `arrow_down_pressed.png` | **32x30** | Pressed. |

### Radar emblems

| filename | size | description |
|---|---|---|
| `radar_emblem_gdi.png` | **128x128** | Faction crest shown in the radar opening when the radar has no power. Metallic relief on a dark field. Must read clearly at this size — bold silhouette, minimal fine detail. |
| `radar_emblem_nod.png` | **128x128** | The opposing faction's crest, same treatment. |

---

## 6. Unit icons ("cameos") — 61 required

Separate batch, all at **64x48**.

Each is a small illustration of one buildable: a building, a vehicle, or an infantry unit,
shown three-quarter view on a neutral background, in the style of 1990s RTS build icons.

- **Canvas: exactly 64x48.**
- **Safe area: 58x42 at offset (3,3).** A 3px frame is drawn over the outer edge at
  runtime, so nothing important — no part of the silhouette, no rank pips — may sit in
  that outer 3px border.
- Fill the frame. The subject should occupy most of the safe area, not float small in
  the middle.
- Opaque background is fine and expected; these sit inside a well.

---

## 7. Delivery

- PNG, RGBA, 8 bits per channel.
- Exact filenames from section 5.
- Flat folder, or `chrome/` and `cameos/`.
- Blank templates at every correct size are provided in `templates/`. Drawing directly
  into them guarantees the dimensions are right.

---

## 8. Pre-delivery checklist

- [ ] Every file is at its exact stated pixel size
- [ ] Every file has a real alpha channel; background is alpha 0, not white
- [ ] `cell_frame.png` has a genuinely transparent 58x42 centre
- [ ] `panel_radar_bezel.png` has an **opaque** 128x128 dark opening at (16,16)
- [ ] `panel_body.png` has no radar hole and no cell grid drawn on it
- [ ] `meter_full.png` segments are evenly pitched
- [ ] No shadow, glow or stray pixel outside any canvas
- [ ] No blurry or soft edges from downscaling
- [ ] Light comes from the top-left in every asset
- [ ] Pressed variants match their unpressed counterparts pixel-for-pixel in size
