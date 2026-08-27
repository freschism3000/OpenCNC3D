# C&C3D sidebar — native sizes

**640x480. Sidebar 160x480, anchored at screen x=480, y=0.**

**The rule: every element is drawn 1:1 and never scaled.** Anything authored at a size
other than the one below gets centred inside its slot rather than stretched, so it will
sit in a box of dead space instead of distorting. See `reference/sizes_chart.png`.

## Delivered — correct, nothing to redo

| element | size | notes |
|---|---|---|
| `panel_body` | 160x480 | backing panel, drawn at 0,0 |
| `panel_header` | 160x18 | at 0,0 |
| `panel_radar_bezel` | 160x160 | at 0,18 |
| `button_repair` | 63x22 | at 6,184 |
| `button_sell` | 41x22 | at 74,184 |
| `button_map` | 42x22 | at 117,184 |
| `cell_well` | 64x48 | empty slot |
| `cell_frame` | 64x48 | overlays a filled slot |
| `arrow_up` / `arrow_down` | 32x24 | at y=450, x = 10, 46, 86, 122 |
| `arrow_up_pressed` / `arrow_down_pressed` | 32x24 | |
| `meter_empty` / `meter_assembled` | 18x240 | at 2,208 |

## Still needed

| element | count | size | notes |
|---|---|---|---|
| `button_repair_pressed` | 1 | **63x22** | you supplied pressed arrows but not pressed buttons |
| `button_sell_pressed` | 1 | **41x22** | |
| `button_map_pressed` | 1 | **42x22** | |
| **cameos** | **61** | **64x48** | buildings, vehicles, infantry |
| `radar_emblem_gdi` | 1 | **139x123** | shown when radar is unpowered |
| `radar_emblem_nod` | 1 | **139x123** | |

### Cameos: 64x48, safe area 58x42

Draw the full **64x48**. `cell_frame` then overlays a 3px border, so the visible opening is
**58x42** at offset (3,3). Anything that must read — the unit silhouette, a rank pip —
belongs inside that inner 58x42. The outer 3px is bezel and will be covered.

Aspect of the visible area is 1.38, close to the DOS 4:3, so existing 32x24 cameo
compositions translate without reframing.

## Minimap: not authored art

The minimap is generated at runtime from map cells. It is **not** a fixed-size asset.

- Radar surface is **139x123** at sidebar coords **(9, 27)**.
- Pick the largest **integer** zoom that fits, then **centre**. Never stretch to fill:
  the surface is 1.13 aspect and maps are near-square, so filling distorts.

```
z = min(139 // map_w, 123 // map_h)
```

| mission | playable | zoom | rendered |
|---|---|---|---|
| SCG01EA | 26x23 | 5x | 130x115 |
| SCB01EA | 37x24 | 3x | 111x72 |
| SCG09EA | 60x48 | 2x | 120x96 |
| SCB13EA | 60x60 | 2x | 120x120 |
| SCB09EA (largest of 50) | 61x59 | 2x | 122x118 |

Every shipped mission reaches 2x or better. A hypothetical 62x62+ map would drop to 1x;
getting 2x there would need the opening 5px taller.

## Draw order

The bezel's opening is **opaque dark, not transparent**:

1. `panel_body`, `panel_header`, `panel_radar_bezel`
2. **then** the minimap into the radar surface
3. per slot: `cell_well`, then cameo, then `cell_frame`
4. meter, buttons, arrows (swap in the `_pressed` variant per state)

Drawing the minimap before the bezel hides it completely.
