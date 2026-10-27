# Hidden Harvester field-effect regression

The implementation is `game/harvester_fx_mod.h`, attached to current main by
small tick, draw, scene-reset and diagnostic hooks in `game/cnc_eyes.cpp`.
This document describes the behavior and verification procedure; it does not
assert that the runtime checks have passed. Completed JSON reports and their
recorded source/binary hashes are the evidence for a particular build.

## Runtime behavior and existing art

In Enhanced Tiberian Dawn, a visible, non-limbo `HARV` throws small tumbling
crystals outward and backward from behind its rear wheels as it moves through live
Tiberium cells. Emission follows actual distance traveled, rather than the
Harvest mission or a stationary harvesting animation. Each sampled wheel
contact must lie on a cell in the engine's current `TIB|` data. Leaving the field
or stopping ends new emission; the existing particles finish their short lives.
Other unit types, Classic/N64 presentation, disabled 3D Tiberium and Red Alert
do not emit this effect.

The debris follows the current theater:

| Theater | Alongside the green crystals |
| --- | --- |
| TEMPERATE | Soil dust, grass tufts and brown dirt clods |
| WINTER | Soil dust, muted grass tufts and brown dirt clods |
| DESERT, SAND | Dry sandy dust and occasional sand flecks |
| SNOW | Pale snow powder |

Crystal triangles, facet normals and UVs come from connected pieces of the
currently loaded `tib3d` clumps; stage-zero soil pods are excluded. The draw uses
the existing Tiberium atlas, retaining its green crystal identity. A pack/atlas
change invalidates the derived shard cache. Dust uses a small procedural radial
alpha texture; grass tufts use crossed procedural triangles and dirt clods use
small tumbling polygon fragments. There are no new
authored model, texture or pack assets, and no new player-colour mask to deliver:
the reused crystal material is intentionally non-player-coloured. Its atlas
alpha represents emissive information, not coverage or a recolour mask; particle
life supplies coverage without interpreting atlas hue or alpha as player colour.

The wheel contacts use measurements of the installed Enhanced `HARV.evhc`, in
origin-relative local coordinates measured in world cells: wheel centers
X = +/-0.2498, outer edge X = +/-0.2974, front/rear axle pivots
Z = +0.286/-0.295. Local +Z points toward the front. Emitters sit at X = +/-0.29
near the outer belts and Z = -0.34, 0.045 cells behind the rear axle, and rotate
with the actual Harvester facing. Both rear contacts are sampled every 0.13 cells
of travel. The original v01 alternated front/rear axle samples every 0.065 cells;
the lighter v02 therefore halves births per distance and moves the whole spray
rearward. Crystal fragments are 25% smaller, with a lower arc, less sideways
speed and shorter lives. The v02 grass fragments and dust/snow puffs were also
smaller and shorter-lived, with lower puff opacity. The v03 surface-debris revision
preserves the v02 crystal sizes, birth spacing and rear contacts. It broadens the
soft dust footprint and strengthens its opacity moderately, uses small crossed
grass blades, and replaces the second grass speck with a brown dirt clod. Dry
theaters add sand flecks only at alternate sampled contacts. The same current
crystal geometry and atlas remain in use.

The simulation advances once per 15 Hz engine tick, uses stateless seeds from
unit/frame/contact identity, and caps the shared pool at 768 particles. Drawing
can interpolate between ticks without advancing the particle simulation or
consuming gameplay randomness.

The world pass runs after combat effects and before rain, shroud, post-process
fog and the HUD. It uses terrain height, depth testing and per-particle shroud
visibility; it does not change field geometry, harvested resources or Harvester
assets. Fixed-function draw state is saved/restored around the pass.

## Hidden verification

Run against the installed Dock runtime after both current-main executables have
been rebuilt and deployed:

```sh
python3 tools/regressions/harvester-field-fx/test-harvester-field-fx.py \
  --runtime "$HOME/CNC3D/playable" \
  --out /tmp/cnc3d-harvester-field-fx
```

The output directory must be empty and outside the installation. Pillow is the
only Python dependency. A macOS sandbox needs access to the video service even
for a hidden OpenGL context. TD's standalone `cnc_eyes` automatically sets hidden
mode for `--script`; it does not accept a separate `--hidden` flag. RA's shell
receives `--hidden` explicitly. Both use `OPENCNC3D_BACKGROUND=1`, `--nosound`,
private graphics/visual settings, private saves, and no desktop input.

The runner creates private all-clear `FF00` terrain with a flat heightmap, a real
player Harvester, and `TI1` overlays. It loads each theater's actual installed
donor pack and uses `--reskin`. The real Harvester drives to a Tiberium target,
which its brain turns into a harvest order; the preflight checks arrival before
it starts harvesting and roaming. `watch` and `expectcell` read the brain's
actual unit positions; no fake transform or test emitter is used.

Coverage:

- TEMPERATE, DESERT, WINTER, SNOW and SAND crystals plus appropriate surface debris.
- Stationary, moving, stopping, moving outside the field, disabled, Classic,
  Tiberium-off, no-field and non-Harvester cases.
- Exact repeated seeded particle diagnostics and frozen-frame simulation state.
- Actual rendered effect response with unchanged opaque HUD pixels.
- Same frozen-frame all/none/only-category/all-without-category captures for
  crystal, dust, grass, snow and dirt. Expected categories must produce distinct
  pixels both in isolation and in the combined effect; absent theater categories
  must produce zero pixels. Reports record pixel counts, contrast, bounds, live
  counts and unchanged physics/HUD. Birth counters alone are not rendering proof.
- Fifty disconnected crystal pieces and the current Tiberium atlas reused by
  the runtime, with the renderer's extraction diagnostic recorded.
- A hidden enemy Harvester field with matched effect-enabled/disabled frames to
  detect any particles leaking through shroud.
- TD's enhanced HUD hit-grid, RA's native sidebar rendering and pixel isolation,
  and valid hidden/clear fog state in both executables.
- Installed executable SHA-256s, unchanged installed `.cfg` files and saves,
  invocation manifests, scripts, logs and PNG captures in `verification.json`.
- macOS `lsappinfo` samples proving each game process is `BackgroundOnly`.

The diagnostic contract is `harvestfx 0|1`, `harvestfx reset`,
`harvestfx dump`, and `harvestfx drawmask N`. Dumps begin `HARVESTFX|` and expose
`enabled`, `enhanced`, `active`, `emitters`, `births`, `crystal`, `dust`, `grass`,
`snow`, `dirt`, `statehash`, `drawmask`, and `live_` counts for all five categories.
Unprefixed category counts are cumulative births; `live_` counts describe particles
present at that engine tick. Reset clears particles and counters while
preserving previous unit positions. `statehash` describes simulation state and
must remain unchanged during draw-only `frames` calls.

`harvestfx 0` disables emission/drawing and clears live particles; it leaves
cumulative birth counters intact. `harvestfx 1` enables the local effect gate;
the normal mode, Tiberium and visibility gates still apply. `harvestfx reset`
also clears the last-tick emitter count, while preserving the position/contact
tracking needed for an uninterrupted drive. `harvestfx dump` only reports state.
Every diagnostic command emits one dump and returns without falling through to
the parser's unknown-command branch. `active` counts live particles, `emitters`
counts Harvesters that emitted on the most recent engine tick, and `enhanced`
reports mode/art eligibility separately from the local `enabled` switch.
`statehash` covers particle positions, ages, angles and seeds; it is a regression
fingerprint rather than a serialized save payload. A full scene reset separately
clears tracks, particle state and the derived shard cache.

`harvestfx drawmask N` filters rendering only, without changing the particles,
seeds, counts or engine tick. Its bits are crystal=1, dust=2, grass=4, snow=8 and
dirt=16. The normal default is 31; 0 draws no categories without clearing them.
This differs from `harvestfx 0`, which disables and clears the effect. The runner
restores 31 after each set of category captures and requires the complete dump to
match the original frozen state. Pixel differences establish rendering, while
the actual review footage is still needed to judge how readable the debris is.

`--motion-only` validates the private fixture and real drive order using a build
that predates the new effect. `--skip-ra` is an explicit partial-run option; a
complete delivery runs both executables. Screenshots are runtime validation,
not artistic approval of the effect.

For close-up loops in all five theaters, run the separate preview tool after
delivery:

```sh
python3 tools/regressions/harvester-field-fx/theater-preview.py \
  --runtime "$HOME/CNC3D-latest/playable" \
  --out /tmp/cnc3d-harvester-theater-previews
```

It records 24 full-size actual runtime frames per theater, plus lossless WebP,
cropped GIF, poster and contact sheet. These use private flat-field verification
missions, actual installed theater/Harvester/Tiberium assets and real movement;
they are labeled as fixture footage rather than stock missions or generated art.
The manifest records frame hashes, live category counts, movement, installed
inputs and settings/saves preservation. `--theaters temperate desert` selects a
subset. The existing `stock-preview.py` retains the original SCB13EC stock-mission
camera and tick sequence for direct before/after comparisons.

## Canonical build and Dock delivery

Merge only the new module and scoped hooks into the current `main` checkout at
`$HOME/CNC3D`; never replace its renderer with the older
workspace's complete file. `integrate.py` checks each insertion anchor and keeps
the original canonical source and merge hashes under `proof/`. It is a one-time
integration helper, not something to rerun on an already integrated source.

Snapshot the final source and both installations before the final build, using
a fresh state directory for each attempt. `proof/build-v02/source-manifest.json`
records the second build attempt; a subsequent source correction needs its own
new snapshot and rebuild, such as `proof/build-v03/`. Source manifests identify
the main HEAD, source hashes and file modes independently of runtime screenshots.

```sh
python3 tools/regressions/harvester-field-fx/deliver.py --snapshot \
  --state-dir tools/regressions/harvester-field-fx/proof/build-v03
```

Build these sequentially with the working directory set to the canonical main
checkout. The renderer's code-only flag preserves the installed approved terrain
and MIX content while refreshing the required decal pack.

```sh
OPENCNC3D_CODE_ONLY=1 sh game/build.sh
sh app/build.sh
```

Then run the delivery helper from the workspace with the same state directory:

```sh
python3 tools/regressions/harvester-field-fx/deliver.py --deliver \
  --state-dir tools/regressions/harvester-field-fx/proof/build-v03
```

The helper requires unchanged canonical main source and universal binaries
built after the snapshot. It atomically installs the renderer and combined app
into both `$HOME/CNC3D/playable` and the observed Dock target,
`$HOME/CNC3D-latest/playable`. It refreshes the separate
`ra3d/opencnc3d` executable and existing `cnc3d` aliases. Existing launcher,
settings, saves and assets are protected by hashes. The four build markers are
excluded because the canonical build scripts own them; the helper does not edit
those markers. `delivery.json` records the copied executable hashes and the
preservation check. A completed copy is not evidence that runtime checks passed.

Run the full hidden regression above separately for each installation, with
distinct empty output directories. Check each `verification.json`, its logs,
screenshots and executable hashes against the delivery report. Both the shared
HUD/fog checks and separate RA executable checks are required. Preserve the Dock
shortcut and application bundles; do not use `open`, `--showwindow`,
`--edgeplay`, interactive gates or desktop control to run a visible game without
specific permission for that run.
