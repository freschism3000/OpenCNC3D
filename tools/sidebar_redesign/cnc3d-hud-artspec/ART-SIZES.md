# C&C3D sidebar — element sizes

**Target: 640x480. Sidebar 160x480, at screen x=480, y=0.**
Every asset is drawn 1:1. Nothing is scaled at runtime.

## Why the last set looked squashed

My earlier spec sizes had the wrong aspect ratios, so conforming your art to them
distorted it before it ever reached the renderer:

| piece | your art | my old spec | distortion |
|---|---|---|---|
| `button_repair` | 278x167 (1.67) | 63x22 (2.86) | **+72%** |
| `meter` | 108x809 (0.13) | 18x240 (0.075) | **-44%** |
| `cell_frame` | 279x298 (0.94) | 64x48 (1.33) | **+42%** |
| `panel_header` | 634x92 (6.89) | 160x18 (8.89) | **+29%** |
| `cell_well` | 279x268 (1.04) | 64x48 (1.33) | **+28%** |
| `arrow_up` | 342x325 (1.05) | 32x24 (1.33) | **+27%** |
| `button_map` | 253x167 (1.52) | 42x22 (1.91) | **+26%** |
| `button_sell` | 274x167 (1.64) | 41x22 (1.86) | **+14%** |

The sizes below fix that. Buttons, arrows and the meter now sit within **1.5%** of your
original proportions.

## The list

| asset | size | notes |
|---|---|---|
| `panel_body` | **160x480** | backing panel |
| `panel_header` | **160x23** | title strip, at 0,0 |
| `panel_radar_bezel` | **160x160** | at 0,25. **Leave a 128x128 hole at (16,16)** |
| `button_repair` | **43x26** | at 11,193 |
| `button_sell` | **43x26** | at 59,193 |
| `button_map` | **39x26** | at 107,193 |
| `button_*_pressed` | same as above | 3 files, one per button |
| `meter_empty` | **26x192** | at 2,227 |
| `meter_full` | **26x192** | same channel, fully lit |
| `cell_well` | **64x48** | empty build slot |
| `cell_frame` | **64x48** | overlay; keep the border **<= 3px** per side |
| `arrow_up` / `arrow_down` | **32x30** | |
| `arrow_*_pressed` | **32x30** | 2 files |
| **cameo** | **64x48** | **x61**. Safe area **58x42 at (3,3)** |
| `radar_emblem_gdi` / `_nod` | **128x128** | shown when radar is unpowered |

`templates/` contains a blank PNG at every one of these sizes, with the safe areas marked.
Draw straight into them and the size can't drift.

## Positions

- **radar surface** 128x128 at **(16,41)**
- **build grid** columns x = **30, 94**; rows y = **227, 275, 323, 371** (2 x 4 = 8 slots)
- **arrows** y = **427**, x = **7, 45, 83, 121**
- **meter** at **(2,227)**

## One decision left: cameo aspect

The cells above are **64x48**, so cameos are **4:3**, matching the DOS originals. Your
delivered cell art was near-square (1.04), which is why it still shows a difference in the
table above. That one is deliberate, not an oversight.

- **4:3 (recommended, as specified):** unit art is wider than tall, and existing 32x24
  cameo compositions translate directly. Needs the cell frame drawn wider than your last one.
- **Near-square:** if you prefer your existing cell proportions, use **56x54** cells and
  columns at x = **32, 92**. Everything else is unchanged. Cameos then become near-square.

Tell me which and I'll lock it.

## Radar

128x128 is **exactly 2 pixels per cell for a full 64x64 map**, so every mission is crisp:

| mission | playable | zoom | rendered |
|---|---|---|---|
| SCG01EA | 26x23 | 4x | 104x92 |
| SCB01EA | 37x24 | 3x | 111x72 |
| SCB13EA | 60x60 | 2x | 120x120 |
| SCB09EA (largest of 50) | 61x59 | 2x | 122x118 |
| full engine grid | 64x64 | 2x | 128x128 |

The minimap is generated at runtime, not authored. Pick the largest integer zoom, then
centre. Never stretch to fill.

## Draw order

The bezel's opening is opaque, not transparent, so the minimap goes on **after** it.
Per slot: `cell_well`, then cameo, then `cell_frame`.
