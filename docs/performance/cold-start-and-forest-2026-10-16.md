# Cold start and animated forest performance

This change removes unnecessary texture residency and repeated tree attribute
construction without reducing the picture, wind animation or shadow quality.
Source is prepared for `main`; publication remains a release decision.

## Cold start texture work

A damaged building previously requested every texture belonging to its model,
including the large progressive-damage overlays used only by partly damaged
buildings and the separate destroyed-state atlases. An already broken building
draws neither of those sets. Native per-texture instrumentation separates
decompression, service variants, faction data, upload calls and livery work.
The initial Forest reference decoded and uploaded 74 texture slots during its
first live frame after optional loading preparation had filled its budget.

Texture masks now come from the original triangles and the actual renderer
stage: intact, held, damaged or wrecked. Held includes progressive overlays;
damaged includes its cut surfaces and glow; wrecked includes the destroyed set
or existing two-set fallback, glow and scorch. A repaired building requests its
intact textures normally. The borrowed cartridge shadow plate does not require
the absent intact building atlases. Shared wall textures, Red Alert and
single-set models retain the original residency path.

Optional preparation prioritizes the union of currently required stages before
future textures, including multiple instances of one model, queued construction
and persisted wrecks. Its 128 MiB CPU and 256 MiB logical GPU admission limits
remain unchanged. Allocation or driver refusal retains the original demand
fallback; logical upload charges are not a VRAM measurement.

The loading-to-live boundary separately prepares the finite current-stage
working set of shroud-visible enhanced objects. This is conservative about
camera visibility because damaged objects also draw in auxiliary passes.
It does not draw a hidden frame, advance the engine or scan the asset catalog.
It checks the complete missing-cost estimate against actual physical and
32-bit virtual headroom with a 256 MiB reserve, rejects jobs above the original
96 MiB transient bound, and admits at most 8,192 individual texture jobs.
Each upload remains transactional; refusal preserves ordinary demand fallback.
Platforms without the Windows headroom query keep the original preparation
budgets as a conservative admission bound rather than assuming unlimited RAM.
The required working set may exceed the speculative 256 MiB GPU allowance,
but its charges reduce the remaining optional allowance rather than creating
a second speculative budget. Diagnostics report the ledgers separately.

This scheduling moves necessary texture work before gameplay timing starts;
it is not by itself an end-to-end loading speedup. Only the independently
measured process-to-ready total can establish that. Scripted live tests defer
optional preparation until their final camera and shroud settings are applied.
Shot-only runs keep their original loading preparation path.

The required census includes the separate wall-cell list and the actual visible
parts of composed tree clumps. Species that exist only inside a clump can still
be lazy at this boundary: their finite current-theater model families are
prepared before the texture census. This does not preload unrelated species or
change their geometry, burning stages or demand-loading fallback.

Standing enhanced objects now run the existing conservative, current-pass
frustum rejection before requesting texture residency. Bounds and rejection
arithmetic are unchanged. An offscreen object still loads normally when it
enters any active pass. A missing offscreen asset can consequently report its
read failure later; visible fallback behaviour is unchanged. Red Alert's
earlier readiness path is not accelerated by this ordering change.

Already bled pixel buffers can also be passed directly to unchanged texture
upload routines instead of being copied and, for plain uploads, bled again.
Unbled textures and Red Alert's plain recoloured uploads retain their old path.
Mip levels, filtering, alpha, faction paint and service variants are unchanged.

Pure procedural water foam and normal textures are generated during loading
on wet maps. This moves their first-use cost out of tactical rendering; it is
not counted as an end-to-end loading speedup by itself. No wave simulation,
animation clock, boat trail or frame is advanced by preparation.

The current shoreline atlas is also prepared during loading. Its original
GPU readback, shore-alpha generation and replacement upload are unchanged;
they previously ran on the first terrain shadow pass. Preparation preserves
the caller's texture state and falls back to ordinary demand loading if the
current atlas or memory headroom is unsuitable. This is not a hidden warmup
frame and is accounted for in end-to-end loading measurements.

The same boundary prepares the existing water classification field and only
the currently audible ambience clips. It uses the original coverage and gain
calculation and original PCM loop analysis, without starting voices, fading
them or advancing the mixer clock. Silent diagnostic launches still execute
these CPU audio paths; `--nosound` is not an audio-work exclusion.

## Animated trees

Tree UVs, colours, normals and texture runs already had an exact attribute
cache, but the first animated pass still rebuilt all of those attributes to
discover that they matched. The new path computes only the current positions
using the original arithmetic and draws with the matching cached attributes.
It reuses the existing frame GPU pool and admission-limited shared scratch
storage. The detailed limits and retained vector-capacity distinction are in
`tools/diagnostics/windows-performance/wind-direct-verification.txt`.

Eligibility excludes other deformation, animation, affine transforms, turret
and rotor movement, treads, per-vertex terrain following, special fragment and
depth-only paths. Missing attributes, allocation refusal or upload refusal
returns to the original full construction path. Shroud, geometry, pass, tint,
texture and terrain identity remain part of the existing keys. Later passes
reuse only the current pose, never a previous frame's wind.

This introduces no visual feature or new backend requirement. It uses the
existing Tier 2 frame VBO pool; unsupported paths retain their original drawing.

## Reproducible checks

The independent controls are `OPENCNC3D_EBLD_STAGE_RESIDENCY`,
`OPENCNC3D_EBLD_CULL_BEFORE_RESIDENCY`,
`OPENCNC3D_EBLD_UPLOAD_REUSE`, `OPENCNC3D_WATER_PREPARE` and
`OPENCNC3D_MESH_WIND_DIRECT`. Set a control to `0` for its reference path.
`OPENCNC3D_EBLD_REQUIRED_PREPARE` and `OPENCNC3D_AMBIENCE_PREPARE` independently
control initial working-set and ambience preparation.
`OPENCNC3D_WATER_SOFT_PREPARE` controls initial shoreline-atlas preparation.
`OPENCNC3D_EBLD_TEXTURE_PROFILE=1` enables per-texture phase timing.
`OPENCNC3D_MESH_WIND_DIRECT_VERIFY=1` compares candidate positions, bounds,
ordered texture runs and final inherited attributes with the original loop.
The wind allocation/upload refusal controls exercise the unchanged fallback.

Focused tests extract actual production functions rather than reimplementing
the algorithm under test. GL recording fixtures check upload bytes and state;
native hidden game checks additionally compare decoded screenshots,
decompressed colour/depth/normal/shadow targets and deterministic world state.

```
python tools/regressions/texture-upload/test-upload.py --help
python tools/regressions/texture-upload/test-stage.py --help
python tools/regressions/resource-preparation/test-preparation.py --help
python tools/diagnostics/windows-performance/test-wind-direct.py --help
python tools/diagnostics/windows-performance/test-water-prepare.py --help
python tools/diagnostics/windows-performance/verify-wind-direct.py --help
python tools/diagnostics/windows-performance/verify-wave-cache.py --help
python tools/diagnostics/windows-performance/measure-cold-start.py --help
```

Cold measurements include parent process creation, boot, preparation and the
first completed nonempty tactical frame using the same Windows performance
counter. Optional GPU retirement is a one-time diagnostic, not enabled during
steady-state timing. Process-cold means a fresh process and GL context, not a
purged OS file cache or vendor shader cache. Hidden timing is not displayed
frame-rate proof. The default fully revealed stress case is distinct from
`--no-reveal`, which preserves the original mission shroud. Frame CSV timing
begins after loading; the separate startup QPC trace includes all preparation.
Performance runs must be serial with no concurrent compiler
or debugger. macOS compilation and driver behaviour require separate checks.

## Measured Windows result

Candidate `d40e176ea5b50e52ac74e5f6d66940f32bda06a0` was measured on Windows 11,
Core i5-12400F, 16 GB RAM, RTX 3070 and NVIDIA 581.32. Its renderer SHA-256 is
`50235638f2bc1645793356031b098ddcac9cae944831f32a3865e153cec4a35a`.
All timing runs use hidden 1920x1080 windows and unchanged Enhanced quality,
with advancing simulation and no concurrent compiler or second game.

Each sustained case has four 30-second runs, in OFF/ON/ON/OFF order for the
eight controls listed above. Statistics use the fixed 5-to-25-second window
and the median of two per-run statistics per arm, not pooled frames.

| Scene | Mean frame ms, OFF to ON | Reduction | p95 ms, OFF to ON |
| --- | --- | --- | --- |
| Mission 1 | 11.885 to 10.244 | 13.81% | 14.984 to 13.010 |
| Forest | 30.621 to 26.415 | 13.73% | 34.983 to 30.694 |
| Siege | 23.496 to 20.596 | 12.34% | 29.233 to 24.991 |

Fresh-process first-live-frame CPU wall times are medians of two runs per arm:

| Scene | Original shroud ms, OFF to ON | Revealed stress ms, OFF to ON |
| --- | --- | --- |
| Mission 1 | 509.494 to 141.475 | 567.918 to 176.927 |
| Forest | 788.709 to 168.592 | 2931.809 to 308.046 |
| Siege | 1266.998 to 249.789 | 2047.056 to 322.462 |

All twelve enabled first-frame CSV rows report zero EBLD texture calls, bytes
and CPU time. Required preparation completes without advancing engine/tick
clocks. The revealed Forest process-to-first-completed-frame time falls from
11.231 to 10.006 seconds; other end-to-end changes are around 1% or less.
This is principally removal of work from gameplay, not a universal load-time
speedup. Individual enabled first frames still take 136 to 327 ms, largely in
first rendering passes. The machine is not proved universally stall-free.
All twelve sustained traces have no frame over 100 ms after five seconds,
including the controls, so that observation alone does not prove a fix.

Actual Forest/Siege readiness checks cover fourteen launches and 28 mission
boots with original/revealed shroud, frame 0/frame 30, reload and model refusal.
They produce 280 exact image/raw-target comparisons and 60,696 matching state
records. The wind matrix covers 25 launches across forest, multiple species,
desert, fire and lifecycle scenes, including allocation/upload refusal; 1,116
image/raw comparisons are exact and all 3,113 direct verification draws match.
Clock-driven pixel changes are checked in forest/species/desert, not claimed
for the fire/lifecycle fixtures. Four camera-entry launches produce 102 exact
captures and 12,990 matching state records while deferred textures load on
entry. The inactive reflection target is not covered by these checks.

Evidence is retained under the local `build/windows-performance/` final-05
startup, steady, real-readiness and wind/cull test directories. Raw CSVs,
captures and input hashes are independently checked. Fresh process/context
does not imply a cold OS file or shader cache. These Windows measurements do
not certify macOS driver performance or Red Alert graphical gameplay.

## Integrated replication

The same serial protocol was repeated on integrated candidate
`e9f8c034663489111e2127ee3dbcd5f6b0c3b989`, renderer SHA-256
`8eb7af96579da8da0366a30906c66000bd1bb1aef9b818a8311e41ba7a922cb5`.

| Scene | Mean frame ms, OFF to ON | Reduction | p95 ms, OFF to ON |
| --- | --- | --- | --- |
| Mission 1 | 11.854 to 10.167 | 14.23% | 14.741 to 12.974 |
| Forest | 30.681 to 26.387 | 13.99% | 35.089 to 30.730 |
| Siege | 23.313 to 20.625 | 11.53% | 29.054 to 25.195 |

| Scene | Original shroud first frame ms, OFF to ON | Revealed stress first frame ms, OFF to ON |
| --- | --- | --- |
| Mission 1 | 505.917 to 136.189 | 570.939 to 183.872 |
| Forest | 789.043 to 171.397 | 2944.792 to 325.692 |
| Siege | 1260.776 to 252.718 | 2049.496 to 319.465 |

All twelve enabled first frames again have zero EBLD texture calls, bytes and
CPU time. The revealed Forest process-to-first-completed-frame time changes
from 11.185 to 9.995 seconds; other totals change by about 1% or less. Individual
enabled first frames still take 133 to 327 ms. No universal stall-free claim is
made, and the fixed-window absence of frames above 100 ms also holds for the
disabled controls.

The final-06 nine-suite regression batch passes 2,632 exact image/raw-buffer
comparisons and 41 exact state-sequence comparisons containing 249,041 matched
exported records across comparisons. Four detached Diagnostics image checks
use their declared tolerance instead of bitwise identity. TD repeats 3,000
ticks with 421,090 identical simulation lines; RA repeats its 60-tick bootstrap,
which does not certify full graphical RA gameplay. The separate full wind
matrix repeats its 1,116 exact captures and 3,113 direct verification draws;
camera-entry culling repeats its 102 exact captures and 12,990 state records.
The inactive reflection target remains untested.

Same-commit GitHub CI passes macOS, Windows, Windows runtime and cross-platform
engine equivalence jobs:
the development repository
This restores compiler coverage, not local macOS GPU performance evidence.
Original final-06 evidence retains its original binary identity. Mutable input
files are preserved with an original-path-to-archive hash mapping before the
next integration build; later source or executable stamps are not substituted
into these reports. No release is published by this validation work.
