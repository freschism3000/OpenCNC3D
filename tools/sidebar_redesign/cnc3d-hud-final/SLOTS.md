# C&C3D sidebar HUD — final integration

## How it works

`reference/chassis_source.png` (724x2172) scaled to **160x480** *is* the HUD. It is drawn
once as a static background. Only genuinely dynamic things composite on top. This is why
the result matches your reference exactly rather than approximately: nothing is
re-assembled from parts, so nothing can drift.

`hud.py` does the whole thing:

```python
import hud
frame = hud.render(cameos=[...], power=0.62, radar_img=minimap,
                   pressed_button="repair", pressed_arrow=2)
```

## Placement

Sidebar is **160x480** at screen **x=480, y=0** on a 640x480 display.

| what | position | size | static or dynamic |
|---|---|---|---|
| chassis | 0, 0 | 160x480 | **static** |
| radar screen | 22, 33 | **116x97** | dynamic (minimap) |
| build slots | see grid | **46x41** each | dynamic (cameos) |
| power meter | 11, 191 | 18x240 piece | dynamic (fill level) |
| button pressed | repair 7,162 · sell 72,162 · map 117,162 | | dynamic (state) |
| arrow pressed | 19 · 52 · 90 · 123, all y=434 | 32x24 | dynamic (state) |

**Build grid:** columns **x = 33, 89**; rows **y = 191, 239, 286, 334, 380**.
Ten slots, 2 wide by 5 tall.

## The number that matters for your next art job

**Cameos must be authored at 46x41**, not 64x48.

64x48 is the size of the `cell_well` / `cell_frame` *pieces*, but once the frame is drawn
the visible opening is only 46x41. A cameo drawn at 64x48 loses about 9px per side
horizontally and 3px vertically under the bezel. Aspect is **1.12**, so near-square, not
the 4:3 of the DOS originals.

## Power meter

The meter is not a simple crop. Your `meter_assembled` only carries green for rows
96..225, so it cannot express 0% or 100% directly. `hud.py` takes the bottom-most green
**segment** (12px pitch) and tiles it upward from the channel floor, so any level from 0
to 100% renders correctly. See `reference/meter_levels.png`.

## Still open

- **Radar content.** The 116x97 opening is empty. Minimap rendering and the faction
  emblem for the unpowered state both go here.
- **61 cameos at 46x41.** The renders use the old 32x24 DOS art upscaled, purely as
  stand-ins so the slots are visible.
- **Pressed states** are wired for buttons and arrows but untested against real input.
