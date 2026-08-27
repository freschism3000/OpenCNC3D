# C&C3D sidebar — layout derived from the delivered art

The previous spec guessed at sub-sizes. This one is **measured off `panel_body`**, which
is the chassis: its own openings decide where everything goes. Pieces keep their native
aspect ratio; nothing is squashed.

## Target
- Screen **640x480**, 16-bit. Sidebar **160x480** at screen **x=480, y=0**.
- `panel_body` is 681x2115 (aspect 0.322) and scales to 160x480 (0.333). A 3.4% vertical
  squash, which is not visible.

## Slots, in sidebar-local coordinates

| slot | x, y | size | notes |
|---|---|---|---|
| SIDEBAR title | 30, 2 | 100x17 | `text_sidebar`, sits between the two amber lamps |
| radar screen | 15, 24 | **129x108** | empty; map / faction emblem goes here |
| button band | 26, 135 | 124x24 | `button_repair` / `_sell` / `_map`, centred, auto-spaced |
| build area | 26, 161 | 124x296 | the cell grid |
| power meter | 10, 185 | 15x259 | `meter_full` / `meter_empty`, centre-cropped to fill |
| arrow bar | 26, 458 | 124x21 | two up/down pairs |

## Cell grid
- **62x59 per cell**, 2 columns x **5 rows** = 10 visible slots
- columns at **x = 26, 88**
- rows at **y = 161, 220, 279, 338, 397**
- empty slot: draw `cell_well` only. Filled slot: `cell_well`, then the cameo inset 3px,
  then `cell_frame` over the top.

## The one thing that changed shape
Your cell art is **square** (`cell_well` 279x268 = 1.04, `cell_frame` 279x298 = 0.94), so
the slot came out **62x59, aspect 1.05**. The C&C cameo has always been 4:3 (32x24, and
64x48 in my earlier spec). Cameos drawn for these frames need to be **near-square**, not
4:3. If you would rather keep 4:3, the cell art needs redrawing wider, not stretching.

## Pieces not used by this assembly
- `panel_header` — the chassis already has a title strip with lamps, so only the
  `text_sidebar` glyphs were needed. Keep as a spare or an alternate top bar.
- `panel_radar_bezel` — the chassis has its own radar surround built in. This standalone
  bezel is a spare.
- `text_repair` / `text_sell` / `text_map` — the buttons already carry their labels.
  Useful if you ever want code-drawn buttons instead.

## Still missing
The radar screen and all ten cameo slots are empty by design. Those are the faction
emblem and the 61 buildable icons.
