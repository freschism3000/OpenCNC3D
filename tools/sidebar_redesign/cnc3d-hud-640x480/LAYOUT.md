# C&C3D sidebar HUD — 640x480, 8 slots, full-size radar

Composed from the delivered pieces. **No new art needed**: the standalone
`panel_radar_bezel` already has a big enough opening. The flattened chassis is not used,
because its built-in radar opening (116x97) is too small.

## Why 8 slots

I scanned all 50 mission INIs. The largest playable area is **61x59** (`SCB09EA`), with
several at 60x60. At 2 pixels per cell that needs **122x120**.

| opening | source | 60x60 map | verdict |
|---|---|---|---|
| 116x97 | baked into the flattened chassis | 1 px/cell | too small |
| **139x123** | your standalone `panel_radar_bezel` | **2 px/cell** | fits, 17px and 3px spare |

The 5th cell row was my invention, not the engine's: `DB_MAX_VISIBLE` is **4** in
`dosbar.h`. Dropping back to 4 rows frees the height the radar needs, and the scroll
arrows exist precisely so the buildable list can exceed the visible rows.

## Layout (sidebar-local; sidebar sits at screen 480,0)

| what | position | size | static / dynamic |
|---|---|---|---|
| `panel_body` | 0, 0 | 160x480 | static |
| `panel_header` | 0, 0 | 160x18 | static |
| `panel_radar_bezel` | 0, 18 | 160x160 | static |
| **radar surface** | **9, 27** | **139x123** | dynamic (minimap) |
| `button_repair` | 6, 184 | 63x22 | + pressed state |
| `button_sell` | 74, 184 | 41x22 | + pressed state |
| `button_map` | 117, 184 | 42x22 | + pressed state |
| meter | 2, 212 | 18x240 | dynamic (fill) |
| build cells | cols **x = 24, 92** · rows **y = 214, 270, 326, 382** | 64x48 | dynamic (cameos) |
| arrows | x = 10, 46, 86, 122 · y = 444 | 32x24 | + pressed states |

## Cameos

Author at **64x48**, drawn at the cell origin. `cell_frame` overlays a 3px border, so the
visible hole is **58x42** (aspect 1.38, close to the DOS 4:3). Keep anything that must be
read inside that inner 58x42.

Note this differs from the flattened-chassis route, where the visible opening was only
46x41. Composing from pieces gives the cameos more room.

## Draw order matters

The bezel's opening is **opaque dark, not transparent**. The minimap must be drawn
**after** the bezel, or it will be hidden. Same for cameos: well, then cameo, then frame.

## Radar zoom

Pick the largest integer zoom that fits: `z = min(139 // map_w, 123 // map_h)`, then
centre. Every shipped mission lands on z=2 or better; small ones like `SCG01EA` (26x23)
reach z=5. Avoid non-integer scaling, which aliases badly on a cell grid.

**Unverified:** I have not checked how the engine currently chooses its radar zoom. If it
fits-to-window with arbitrary scaling rather than integer steps, that wants changing to
integer steps before this pays off.

## Still open
- Minimap rendering into the 139x123 surface, and the unpowered-state faction emblem.
- 61 cameos at 64x48.
