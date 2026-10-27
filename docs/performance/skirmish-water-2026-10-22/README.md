# Skirmish frame rate on water maps: causes and five fixes

Two revealed skirmish maps with nothing happening ran at about 30 FPS (Tiberian Dawn,
SCM06EA Lakefront Clash, desert lake) and 23-40 FPS (Red Alert, SCM01EA Coastal
Influence, snow coast) at 3008x1547 on an Apple M5 Max (build `4ad1ea1ba`). This report
says where the time goes, how it got there, and how to remove each of the five largest
costs without changing a pixel. Every finding and every fix covers both modules.

## Summary

- **The frame is CPU and driver bound, not GPU bound.** Frame time barely moves between
  3008x1547 and 752x387, and the GPU is only 5-16 points busier with the game running
  than without it.
- **The slowdown is new Enhanced water, sent to OpenGL one vertex at a time.** The shore
  swash lattice (`7c4308ee5`) and the closed submerged lake bed (`cacb3b702`, merged in
  `ea6d5d0a1`) rebuild their geometry on the CPU every frame in immediate mode; the bed
  does it twice. A TD frame went from 1.2 million GL calls to 2.6 million at
  `4ad1ea1ba`, and to 15.3 million at `d1edca344` (16-step lattice), where the two scenes
  ran at 7 FPS (TD) and 4 FPS (RA).
- **Two prototypes keep exactly the same geometry on the GPU and give a byte-identical
  picture:** `d1edca344` goes from 140 to 10.7 ms per frame on TD (7 to 94 FPS) and from
  229 to 25 ms on RA (4 to 39 FPS).
- **`a5889084d` replaced the generated lake bed with a shader "virtual bed"**, which
  removes its cost but changes the look (no bank geometry), and `314cfbf4e` draws flat
  shore cells with 2 triangles instead of 128. Main is back to 11.5 ms on TD (87 FPS) and
  19.8 ms on RA (51 FPS) live.
- **Every building placed, sold or lost by any player rebuilds the whole map's ground:**
  one frame of 289 ms on TD and 125 ms on RA on `a5889084d` (unchanged by `314cfbf4e`).
- On `314cfbf4e` the remaining per-frame costs are the sun shadow pass (6.5 ms of every
  RA frame), the shore lattice (about 3 ms) and vegetation re-posed in every pass.
- **K3, K4 and K5 are implemented** with byte-identical pictures: RA SCM01EA goes from 62 to
  107 FPS and TD SCM06EA from 122 to 130 FPS live (see What landed).

## How it was measured

All runs are headless (`cnc_eyes --script` in a hidden background window), 3008x1547,
saved Enhanced settings, `--noshroud`, 8 players (7 AI). Cameras are fixed: TD
`cam 13 24`, RA `cam 55 40` with `--seed 4242`, both `dist 4200` unless noted. Red Alert
skirmish `--seed 0` deals random start positions, so any comparison across runs needs a
fixed seed and camera. Two clocks:

- **live**: `livebench` runs the real tick and present loop, which is what a player sees.
- **frozen**: `framebench` finishes every frame on the GPU, so CPU and GPU time add.

Historic commits were built from `git archive` exports (renderer only, arm64) and run
against the same data. Arms alternate (A B C ... C B A), each run twice. Identity is a
pixel compare of `--shot` PNGs against a second run of the reference arm.

## Timeline

Live frame time, fixed views:

| Commit | What it added | TD ms | TD FPS | RA ms | RA FPS |
|---|---|---:|---:|---:|---:|
| `3771661e7` | reference | 12.6 | 79 | 19.0 | 53 |
| `b808a1a93` | before the shore lattice | 9.3-13.6 | 73-108 | 17.7-20.4 | 49-56 |
| `cd2b05c22` | shore lattice, before the lake bed | 17.4-18.7 | 54-57 | 24.8-27.5 | 36-40 |
| `4ad1ea1ba` | closed submerged lake bed (8 steps) | 33-36 | 28-30 | 54-57 | 18 |
| `d1edca344` | bed and crest at 16 steps, 5 samples per node | 140-153 | 6.5-7 | 229-247 | 4 |
| `a5889084d` | shader virtual bed instead of geometry | 13.7-16.4 | 61-73 | 20.4-21.4 | 47-49 |
| `314cfbf4e` | flat shore cells drawn as 2 triangles | 11.5 | 87 | 19.8 | 51 |

A 20-commit bisection from `3771661e7` to `4ad1ea1ba` shows exactly two steps on TD:
`7c4308ee5` (shore lattice, about +6 ms) and `ea6d5d0a1` (lake bed, about +17 ms). Older
builds draw less (no desert bushes, no new shoreline), so part of the early difference
is content.

## Where the time goes

**CPU bound.** Frozen frames on `cd2b05c22`: TD 20.0 ms at 3008x1547 and 16.7-18.5 ms at
752x387. On `d1edca344`, 137 ms at every size. GPU busy (AGXAccelerator Device
Utilization) was 40-54% with the game against 38% without it.

**Stack sample, TD `4ad1ea1ba`** (share of `draw_frame`): `water_bed_draw` 49% (half in
the colour pass, half again inside `fx_draw_terrain_normals`), `water_shore_draw` 20%,
sun shadow casters 7%. RA: `water_bed_draw` 57%, shore 13%, sun casters 14%.

**Lake bed counters, `4ad1ea1ba`, TD:** 16 ms per frame for both passes, 521,000 node
map lookups and 477,000 immediate-mode vertices from 1,690 visible water cells. The node
cache is cleared only on building placements, never by its cap on these views.

**GL calls per frame:**

| Build | TD calls | TD glVertex3f | RA calls | RA glVertex3f |
|---|---:|---:|---:|---:|
| `cd2b05c22` | 1.22 M | 298 k | 1.36 M | |
| `4ad1ea1ba` | 2.63 M | 770 k | 3.83 M | 1.16 M |
| `d1edca344` | 15.3 M | 3.27 M | 23.4 M | 4.93 M |

For comparison, the VBO draws are 554 glDrawElements with 9.06 M indices per TD frame.
RA re-uploads about 14 MB of vertex data per frame (wind-posed trees).

**Knock-out matrices** (frozen, one switch at a time between baselines; noise about
+-2 ms; the RA matrices used one random view each):

| Switch off | TD `4ad1ea1ba` | RA `4ad1ea1ba` | RA `cd2b05c22` |
|---|---:|---:|---:|
| `water_fx` (all Enhanced water) | -31.3 ms | -15.6 ms | -1.1 ms |
| `fol_on` (bushes, clumps, details) | -6.4 | | |
| `shadow_on` | -2.6 | -6.5 | -4.9 |
| `tree3d` | -1.2 | -5.9 | -5.1 |
| `rock_shadows` / `rock_on` | -0.3 | -3.8 | -4.0 |
| `unified_lighting` (bake pass) | -2.3 | -3.9 | -2.1 |
| `bloom_on` / `ssao_on` (GPU) | -4.1 / -3.5 | | |

GPU-only items count fully in frozen frames but overlap with CPU work in live play.

**Hitches.** The frame after a building placement rebuilds the relief surface (including
a full `glGetTexImage` of the terrain atlas, 4992x5148 on TD desert and 6240x6084 on
RA), foliage, the RA ground cache and every water node: 351 ms (TD, `cd2b05c22`), 465 ms
(TD, `d1edca344`), 289 ms (TD, `a5889084d`), 92-126 ms (RA). The profiler books the
relief rebuild under "sun shadow map".

## The five largest costs and how to remove them

Each fix keeps the vertices, attributes, draw order, state and shaders that reach the
GPU, so every render target stays byte-identical. Each is behind a switch and checked by
a verify mode that recomputes the old result and compares it. All are in shared code
reached by both modules (TD through `draw_frame`, RA through `ra3d_world_ground` and the
shared passes). Designs were judged against the code of `d1edca344`.

| | Cost | `4ad1ea1ba` | `314cfbf4e` |
|---|---|---|---|
| K1 | lake bed in immediate mode, twice | 16 ms TD, ~30 ms RA | gone (replaced by a shader) |
| K2 | shore lattice in immediate mode | 6-8 ms TD | about 3 ms TD and RA |
| K3 | whole-map rebuild per placement | 230-465 ms frame | 289 ms TD, 125 ms RA frame (`a5889084d`) |
| K4 | sun pass draws every caster on the map | 2.6 ms TD, 6.5 ms RA | 1-3 ms TD, 6.5 ms RA |
| K5 | vegetation re-posed and re-submitted per pass | 6 ms TD bushes, 5-6 ms RA trees | similar |

### K1. The submerged lake bed (`game/water_bed.h`)

**Cause.** `water_bed_draw` runs twice per frame (colour from `draw_seabed` or
`ra3d_draw_floor`, normals from the first line of `fx_draw_terrain_normals`). For every
visible covered cell it tests flatness with up to 289 `std::map` finds, then emits up to
1,536 immediate-mode vertices, each with a map find, four shroud corner reads and five
GL calls. Everything it emits is a pure function of the water plane, the relief surface
and the shroud.

**Fix.** Build each cell's lattice once into a GPU buffer in `water_cells()` order;
recompute a cell's colour only when its four shroud corners change (the rule
`terrain_surface.h` already uses); draw the visible cells' ranges in both passes from
the same buffer. The judged design adds cached flatness with an exact provable-flat
shortcut, 901 instead of 1,445 samples per new cell by sharing exact half-lattice
points, a cell-indexed accessor for the shore, and partial invalidation after placements
(K3).

**Prototype** (`prototype-bed-gpu-buffer.patch`, against `d1edca344`, switch
`OPENCNC3D_PERFTEST_BEDVBO`): 0 differing pixels on TD and RA at two zooms. TD 140 to
65 ms, RA 229 to 105 ms live. Relevant only if the generated banks of `4ad1ea1ba` or
`d1edca344` are wanted back instead of the shader bed of `a5889084d`.

### K2. The shore swash lattice (`water_shore_draw`, `game/water_mod.h`)

**Cause.** Every visible coast cell emits an 8x8 beach lattice (384 vertices; since
`314cfbf4e` a flat cell with linear corner weights emits 6) and a crest lattice per frame
in immediate mode, each vertex with node lookups, a coast-field sample,
plane lookups with a string compare and 4-5 GL calls. Only the shroud and light at the
cell corners change between frames; the animation is in the shader.

**Fix.** The same as K1, per coast cell, built lazily the first time the cell is visible
after its inputs change. The order must stay all beach, then all crest (the shore blends).
One immediate-mode detail must be reproduced: the crest loop never sets `TEXTURE5`, so it
inherits the last beach vertex's value; the array path sets that current value
explicitly and restores the other current attributes after drawing. The judged design
adds pages with a 48 MB LRU budget for the 32-bit Windows process, a complete cache key
with a log line naming the changed field, and a buffer-free quick win: evaluate each
cell's nodes once instead of once per vertex.

**Prototype** (`prototype-shore-gpu-buffer.patch`, with K1, switch
`OPENCNC3D_PERFTEST_SHOREVBO`): 0 differing pixels on TD at two zooms and RA at
`dist 3000`; RA at 4200 differs by 1 pixel (2 levels) inside a region where two runs of
the reference differ by 9. With K1: TD 65 to 10.7 ms, RA 105 to 25.4 ms live.

### K3. The whole-map rebuild after every building placement

**Cause.** Any pad change bumps `g_padEpoch`, which rebuilds the whole relief surface
(atlas readback, distance transform, blur, every vertex), every foliage chunk, the RA
ground cache, and drops every water node, re-sampled over the next frames. A placement
changes a few cells.

**Fix: diff-driven local refresh.** Only two inputs depend on pads, `corner_raw` and the
surface lift. Diff them bit for bit at each pad change, stamp the changed cells in a
per-cell revision grid (not a union rectangle: AI placements land all over the map in one
tick), and let each cache refresh only items whose read set touches a stamped cell. Quick
win, exact and shared: when only the pad epoch moved, reuse the coast base (no
`glGetTexImage`, no distance transform), hoist the per-vertex corner-normal encodes to
per cell, and erase only the water nodes whose samples fall in changed cells. A verify
mode recomputes the legacy result after every patch and compares bytes, including the
GPU copy.

### K4. The sun shadow pass draws every revealed caster on the map

**Cause.** The sun box (`fx_sun_matrix`) is about twice the view across and four times
along the sun at the shipped 28 degree elevation, so it covers the whole map, and every
culling test asks "inside the box?" rather than "can this reach a texel anyone reads?".
Every revealed building, vehicle, wall, rock, bush and RA tree is submitted, objects
twice (opaque then cutout), RA trees wind-posed twice. RA's terrain sun-map cache never
runs (it needs `shadow_terrain_rise <= 0`, a TD-only rise path).

**Fix: receiver-window culling.** The sun maps are read only by the light pass, only at
pixels with depth, at a projected point plus a bounded kernel. Per map and frame, take
the camera frustum from the exact inverse matrix the light pass uses, clip it to the
lowest depth-writing ground, project it with the map's matrix, take the convex hull and
pad it by the kernel and normal offset. A caster whose light-space box misses that window
cannot change a sampled texel, so skipping it leaves the picture identical. The box,
resolution, bias, kernel and shaders stay unchanged (shrinking the box would change
texel density and pixels). Applied only at reviewed sites: foliage cast chunks and rocks
first (most of RA's 3.8 ms), then buildings after residency, vehicles, walls and
soldiers. Then tight per-model cast boxes, one object walk instead of two, and the
terrain cache enabled for RA with a hardened key. Modelled gain: 2.5-4.5 ms per RA frame,
1-2 ms TD, including while scrolling.

### K5. Vegetation and objects re-posed and re-submitted in every pass

**Cause.** A building is visited by up to 13 object passes per frame; each visit resolves
its model, builds and hashes a draw key and issues state calls. The unified-lighting bake
pass re-poses every visible tree and uploads it again. Foliage is prepared and drawn per
chunk and model in four passes.

**Fix: do each piece of work once per frame.**
- The bake consumes the frame's existing tree pose (consume only; producing would break
  sharing for later passes).
- `mesh_perf_switch` becomes O(1) per call site; program binds and uniform locations are
  cached instead of asked from the driver.
- Model resolution is memoised per frame; an exact early-out skips passes a model has no
  triangles for.
- Wall loops share one client-array scope; foliage moves into one arena.
- In the depth-only sun map only, foliage cast batches are grouped by texture: a depth
  target with blending off keeps the minimum depth whatever the order, so that one
  reorder is exact.

Rejected because they move pixels: wind in a vertex shader (GPU float contraction
differs), merging the normal, class, bake and specular targets with MRT (each target keeps
the last passing fragment, in submission order), and any cross-frame pose cache (the wind
time changes every frame). Modelled gain on RA: 3-6 ms in total, overlapping K4.

## What landed: K3, K4 and K5

Merged on top of `4e5024fc0`. Every change sits behind a switch (`=0` restores the old
path), and the merged build was compared against that commit's own binary: the final PNG
and the scene, depth, normals and bake targets are byte-identical on seven scenes (TD
SCM06EA at two zooms, TD SCM01EA, TD winter SCG04WA, RA SCM01EA at two zooms, RA SCM04EA)
and on 20 captures taken across AI building placements. The sun maps differ only in texels
the light pass never reads (K4, proven texel by texel against every lit pixel's read
reach). Gates G1-G5 pass with the same digests as the reference; G142, G196, G268
(placement), G300 and G334 pass; G267, G268 (rock settling) and G322 fail identically on
the reference in the same run folder.

Live frame time, 3008x1547, reference against this change (A B B A):

| Scene | Reference `4e5024fc0` | With K3, K4, K5 |
|---|---:|---:|
| TD SCM06EA, `dist 4200` | 8.23 ms (122 FPS) | 7.69 ms (130 FPS) |
| RA SCM01EA, `dist 4200` | 16.16 ms (62 FPS) | 9.37 ms (107 FPS) |
| RA SCM01EA, `dist 3000` | 13.62 ms (73 FPS) | 8.41 ms (119 FPS) |

- **K3.** A placement patches the relief surface in 0.3-1.4 ms instead of rebuilding it
  (20-107 ms); foliage, the TD terrain cache, the RA ground cache and the sun's ground
  store are patched in place (0.2-0.5 ms each). The coast base is reused unless the atlas
  it came from is written. A full relief build at map load fell from 20-22 to 10-11 ms on
  TD. What is left of a placement spike is the first sight of a new building type (its
  file read, textures painted and uploaded: 100-200 ms), a separate item.
- **K4.** The CPU sun-pass section on RA fell from about 6.5 to 2.2 ms (6.7 to 2.3 ms while
  scrolling), on TD from 1.6 to 1.1 ms. Its one premise is the receiver floor (the lowest
  depth-writing ground minus one cell); `OPENCNC3D_SUN_RECV_VERIFY=1` reports any lit pixel
  below it and any read texel outside a culling window.
- **K5.** About 0.5-1.5 ms per frame on both modules (the specular pass halves on TD), and
  about 420 driver queries per TD frame are no longer sent, which matters more on Windows
  drivers. A suspected GPU cost of the program-bind shadow in frozen RA frames did not
  reproduce on a quiet machine (17.74 ms off, 17.02 ms on).

Switches (each `=0` is the old path):
- K3: `OPENCNC3D_SURFACE_BASE`, `_SURFACE_NODES`, `_SURFACE_SLOTS`, `_SURFACE_LOCAL_LIFT`,
  `_SURFACE_CELLS`, `_GROUND_PATCH`, `_GROUND_VBO_DIFF`. Verify: `OPENCNC3D_SURFACE_VERIFY=1`,
  `OPENCNC3D_GROUND_PATCH_VERIFY=1` (with `OPENCNC3D_RA3DCACHE_VERIFY=1`).
- K4: `OPENCNC3D_SUN_RECV_CULL`, `OPENCNC3D_RA_TERRAIN_SHADOW_CACHE`. Verify:
  `OPENCNC3D_SUN_RECV_VERIFY=1`; per-frame counts: `OPENCNC3D_SUN_RECV_LOG=1`.
- K5: `OPENCNC3D_BAKE_SHARED_POSE`, `_MESH_SWITCH_ADDRESS`, `_PROGRAM_SHADOW`,
  `_RESOLVE_MEMO`, `_EBLD_PASS_EARLY_OUT`, `_WALL_WALK_SCOPE`, `_MESH_WIND_UNIQUE_BOUNDS`.
  Verify: `OPENCNC3D_PROGRAM_SHADOW_VERIFY=1`, `OPENCNC3D_RESOLVE_VERIFY=1`,
  `OPENCNC3D_MESH_WIND_DIRECT_VERIFY=1`.

Not done from the designs: K4's cutout early-out, tight per-model cast boxes, single object
walk and scissor; K5's foliage arena, order-free cast grouping and indexed tree soup; K3's
shore node cache (it still clears on a pad change, about 0.5 ms) and the terrain sun map,
which is still redrawn after a placement. Windows was compile-checked only.

**K3b, the first sight of a new building type, is not on main.** It is on branch
`perf/k3b-first-sight-textures` (`game/ebld_foresee.h`, switch `OPENCNC3D_EBLD_FORESEE`,
`=0` the old path): the textures of a building type in production are decoded and
house-painted on a worker thread and uploaded in time-boxed slices before the building is
placed. Its pictures are byte-identical (seven scenes with raw targets, six-capture
placement sequences on both modules, with and without the synchronous mode). It is held
back on timing and memory:

| 60 s live, three runs per arm | Off: frames over 25 ms, worst | On: frames over 25 ms, worst |
|---|---|---|
| TD SCM06EA | 4, 1, 1; 140, 139, 142 ms | 5, 18, 4; 132, 149, 47 ms |
| RA SCM01EA | 14, 7, 6; 226, 219, 294 ms | 18, 18, 6; 144, 232, 66 ms |

| Building textures on the card | Off | On |
|---|---:|---:|
| RA, end of a 60 s live run | 488 MB | 966 MB |
| RA, end of a 180 s live run | 787 MB | 1314 MB |

The stall it targets (about 140 ms on TD) is gone in most runs, but medium frames (30 to
75 ms) become more frequent, and on RA the large frames move into the sun, terrain and
specular passes rather than going away, which points at the extra texture memory. The
newest commit on the branch caps what the preparation holds at 256 MB of the card on both
modules, its specular maps counted. Measured once per module with it: RA held 244 MB (card
732 against 488 MB) and its worst frame was 55 ms against 216 and 219 ms off; TD held 218 MB.
Two faults remain, found by reading the code: a production charge is given back as soon as
any building of that type stands, even one that is never drawn (so the cap can leak), and the
2 ms upload time box counts only CPU submission, so the driver's share of a 2048 texture's
chains can land in a later pass (a 138 ms TD frame fell inside one such upload window).

## Verification for any of these fixes

- Same binary, switch on and off: pixel compare of `--shot` PNGs and raw render targets
  (scene colour, depth, normals, bake, specular, sun maps) on TD SCM06EA, a TD winter or
  snow coast, a TD river with falls, RA SCM01EA and an RA temperate sea; two runs of the
  reference arm as the noise floor.
- Scripted placements, sales and destructions inland and at the shore, compared every
  frame, for K1-K3.
- Live and frozen timing in alternating order, as above, and the Windows PerfProbe log for
  the Windows build.

## Caveats

- Windows was not measured; immediate mode is slow on Windows drivers too.
- Mid-game bases were not measured; object and caster costs grow with every building.
- The prototypes are measurement code: no memory budget, no partial invalidation, no
  verify mode.

## Files

- `measurements.json`: every run summarised (bisection, A/B runs, matrices, census, sweeps).
- `prototype-bed-gpu-buffer.patch`, `prototype-shore-gpu-buffer.patch`: the K1 and K2
  prototypes, against `d1edca344`.
