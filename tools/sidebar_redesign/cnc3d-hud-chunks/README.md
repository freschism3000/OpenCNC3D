# C&C3D sidebar — chunks from `sidebar_ref.png`

## Fixed this pass

- **White lines down both edges.** Your `sidebar_ref.png` is art on a **white canvas**;
  the outer columns measure luminance ~253. I was scaling the whole canvas, margin
  included. The slicer now crops to the content bbox (x 6..717) first. Edge columns now
  read luminance 63 and 75 instead of 255.
- **Radar drawing over its own frame.** True interior is **136x120 at (11,31)**.
- **REPAIR clipped by the meter.** The channel starts at y=211, below the buttons, not 202.
- **Cell grid drift.** Region-growing was unstable on this dark art; the slicer now
  profiles the build area, which lands on every well consistently.

## Layout (derived, in `chunks/layout.json`)

| what | position | size |
|---|---|---|
| radar surface | **11, 31** | **136x120** |
| build slots | cols x = **[18, 87]** · rows y = **[210, 259, 306, 353, 401]** | **61x45** |
| meter channel | x **0**, y **211..449** | 17 wide, 12px pitch |

10 slots and a 2x radar. Largest shipped mission `SCB09EA` (61x59) renders at 2x = 122x118.

`hud.py` reads `layout.json`, so the code cannot drift from the art.

## Still to be drawn

| asset | size |
|---|---|
| cameos x61 | **61x45** |
| `radar_emblem_gdi` / `_nod` | **136x120** |
| button pressed states | 46x26 / 44x26 / 42x26 |
| arrow pressed states | 34x26 |

## Resolution

`reference/screen_final.png` is **640x480 native**. The @2x file is only for viewing.

The layout is fully native, everything composited 1:1. The chassis art is not: it comes
from a 725x2170 source, a non-integer **4.45x** reduction, so it carries ~3000 colours and
reads slightly soft. Authoring the source at **160x480**, or an exact multiple like
**320x960** / **640x1920**, would make it truly crisp.
