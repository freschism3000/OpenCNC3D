# Shared tree-wind performance — 18 October 2026

The saved tree-size settings disabled the existing wind fast path in both games. Both installed presets use `etree_height=1.28929`, `etree_width=1.13839`, and `etree_vary=0.09643`, which require a nonidentity matrix. The renderer excluded every such draw from its immutable-attribute and direct-position paths. Animated trees consequently rebuilt UVs, colours, normals and positions repeatedly, particularly during sun-shadow preparation.

The fix in `game/cnc_eyes.cpp` admits fitted matrices into those paths. It preserves the original arithmetic order: wind, fill clamp, matrix, facing, world placement. All twelve matrix floats already participate in both cache keys. Normals retain their original calculations; animated geometry still changes each frame. Wind direction trigonometry and flutter scale are computed once per draw. Production assembly confirmed one sin/cos pair per draw instead of three per triangle in the old direct helper.

This is shared renderer code used by Tiberian Dawn and Red Alert, including their unified apps. No resolution, shadow quality, model detail, material, wind, fog or saved tuning was reduced. `OPENCNC3D_MESH_WIND_MATRIX=0` restores the former matrix exclusion for comparison; normal launches need no flags.

## Baseline and diagnosis

The screenshot reports 3008×1547, 24 FPS / 41.2 ms on the first RA skirmish map, SCM01EA (Coastal Influence, snow), with shroud disabled. Its moved camera was not logged. Camera 32,43 at distance 4200 is a reconstructed match, not a recovered exact camera.

The actual Dock shortcut points at `${DOCK_RUNTIME}/OpenCNC3D.app`. The original installed app and presets were pinned before testing. A nearby 3008×1547 view measured 41.30 ms (100 frames); the direct-wind draw count was zero. A separate coast view measured 47.24 ms, with approximately 25 ms charged to CPU/driver sun-shadow preparation. Temporarily disabling shadows brought that view to 20.49 ms; fog/water/SSAO controls did not remove the bottleneck. The sampled CPU stack also concentrated in tree `draw_mesh` work. These are CPU/wall-clock timings, not isolated GPU timestamps.

The initial tick-five probe was rejected: existing lazy tree initialization had not loaded enhanced trees yet. All accepted regressions advance to tick 160, after the first building activates the tree model cache. That separate startup issue was not changed.

## Full-resolution paired measurements

Final universal renderer, 3008×1547, original saved presets, revealed map. Each camera uses 15 warm frames followed by 120 completed frames, in off/on/on/off order. Values below are medians of the two per-run medians. Simulation is held fixed within each timed view; wind time advances between views. All screenshots match exactly across all four runs. Background work on the machine was present, so these measure relative rendering improvements, not a promised live-game FPS.

| Module | Camera / distance | Original path | Optimized | Frame-time reduction |
|---|---|---:|---:|---:|
| RA3D / SCM01EA | 32,43 / 4200 | 62.16 ms | 37.06 ms | 40.4% |
| RA3D / SCM01EA | 27,36 / 3000 | 52.89 ms | 34.46 ms | 34.9% |
| RA3D / SCM01EA | 80,55 / 3000 | 60.59 ms | 37.04 ms | 38.9% |
| OpenCNC3D / SCG05EA | 11,29 / 3000 | 32.52 ms | 22.51 ms | 30.8% |
| OpenCNC3D / SCG05EA | 31,41 / 4200 | 69.65 ms | 43.45 ms | 37.6% |
| OpenCNC3D / SCG05EA | 11,29 / 1800 | 30.02 ms | 25.91 ms | 13.7% |

The original installed app is also compared against the delivered RA app separately; see the installed-app results below. This avoids treating a renderer control as a comparison against an older entire build.

## Correctness and portability

The final renderer passed nine states in both modules: three camera/zoom views, advancing wind time, two nonuniform size settings, returning to an earlier size, restoring authored settings, fog, and reveal restoration. For each optimized/verifier arm, all nine PNGs and all 54 raw targets are byte-exact against the original path. Raw targets include scene colour, depth, normals and all three sun-depth targets; sun maps remain 4096×4096. Across both games this is 252 exact comparisons, plus 2,963 exact RA and 9,162 exact TD recorded state rows per comparison. RA verified 3,394 draws and TD 2,854, with zero mismatches. An earlier longer matrix-only run independently passed the same 252 comparisons; the final run includes the trigonometry hoist.

Production-extracted arithmetic tests pass on arm64 and x86_64: 24,000 cases per architecture, including 20,571 matrices, 151,032 position/bounds vertices, 96,000 normal-property checks, eligibility exclusions, negative controls, allocation refusal and upload checks. The Windows/Mac source-list guard also passes. Windows runtime FPS was not measured.

The RA fog test hides all trees; the TD fog test retains visible optimized draws. This is not exhaustive coverage of every species at every partial-shroud boundary. Allocation/upload failures are covered by the native extraction fixtures; the full rendered suite used normal, old-path and verifier arms.

## Delivery and reproducibility

Recorded paths use `${SOURCE_ROOT}` for the canonical main checkout, `${DOCK_RUNTIME}` for the existing Dock play folder, and `${EVIDENCE_ROOT}` for the retained local benchmark output directory. These aliases describe the recorded locations; receipt command arrays are not directly executable until those aliases are resolved. Measurements and content hashes are unchanged.

Both universal binaries were built with the standard build scripts from current canonical main at `${SOURCE_ROOT}`. The build stamp is `v0.8.7+f100f58-dirty`; the dirty source includes this scoped change and unrelated ongoing work. The only committed change since that desktop build was in the separate iOS layer. A concurrent build subsequently overwrote shared build outputs with binaries that lacked `OPENCNC3D_MESH_WIND_MATRIX`. A hash guard stopped delivery, and the verified main-built artifacts were restored only after checking that current desktop source still matched.

Both the canonical play folder and the actual Dock play folder have refreshed `opencnc3d`, `cnc3d`, their separate RA aliases, the standalone renderer and required bundled SDL libraries. No app bundle, Dock shortcut, launcher, settings, saves or unrelated asset was replaced. All 58 captured settings/save/bundle hashes remained unchanged at staging. The build and deployment receipts include exact hashes.

Local detailed logs, scripts, screenshots, raw captures and pinned builds are retained at `${EVIDENCE_ROOT}/`. Compact receipts are beside this report. Re-run the portable comparison with:

```sh
python3 tools/perf/shared-tree-compare.py \
  --candidate /path/to/cnc_eyes --runtime /path/to/playable \
  --ra-gfx /path/to/ra3d-fx.cfg --td-gfx /path/to/opencnc3d-fx.cfg \
  --output /new/output/directory
python3 tools/diagnostics/windows-performance/test-wind-direct.py \
  --native --output /new/arithmetic/output
```

## Installed-app results

The original installed RA app versus the delivered RA app was tested in before/after/after/before order at 3008×1547, camera 32,43, distance 4200, seed 77, tick 160, revealed map and the original saved preset. Each run used 15 warm frames and 120 completed timed frames. Median-of-medians: **52.47 ms → 33.76 ms**, or **35.7% less frame time**. This is a rendering benchmark on a busy machine, not a live-match FPS guarantee. The old app was preserved separately and was never copied over the installed result.

All four installed hidden launch checks passed: actual Dock TD and RA, plus canonical TD and RA. Captures show the existing HUD and fog behavior. Final post-test verification confirmed all deployed hashes, the unchanged actual Dock target, and all 58 captured settings/save/bundle files; see `dock-smoke.json` and `final-delivery-verification.json`.
