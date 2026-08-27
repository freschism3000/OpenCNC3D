# GDI radar emblem — source art for redrawing

Shown in the radar window when the radar has **no power**. In the DOS original this is
frame 0 of `RADARGDI.SHP`.

## Target size

**136x120** — the radar opening in the new 640x480 sidebar, at (11,31) in sidebar-local
coordinates. Draw to exactly this.

An opaque background is correct: this fills the whole radar window, it is not a sprite
with transparency. Match the dark near-black of the powered radar screen at the edges if
you want it to sit naturally in the bezel.

## Files

| file | what it is |
|---|---|
| `01_original_80x69.png` | the untouched DOS art, 49 colours. The authority on shape. |
| `02_isolated_80x69.png` | same, background flood-filled to transparent, so the medallion silhouette is clear |
| `03_target_136x120_nearest.png` | scaled to target, blocky. Shows the pixel budget you actually have. |
| `04_target_136x120_smooth.png` | scaled to target, smooth. Easier to read as a shape, but soft. |
| `05_working_8x_640x552.png` | 8x integer blow-up, a large canvas to paint over |
| `06_isolated_target_136x120.png` | isolated medallion at target size |

## Notes

The original is only 80x69 with 49 colours, so **nothing here has real detail to recover**
at 136x120. Treat these as a shape reference and redraw, rather than as something to
upscale and clean.

Aspect: original is 1.159, target is 1.133. Close enough that the medallion stays round,
but do not stretch to fit — draw at 136x120 directly.

The eagle should read at a glance at this size. Bold silhouette, few tones. The chrome it
sits in uses a warm-cast metal ramp (`#181818` to `#585850`, highlights near `#e8e0d8`)
with amber lamps `#f0b800`–`#f8e800`, if you want it to feel part of the same set.
