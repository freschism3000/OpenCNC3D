# Exact prepared terrain rays

## Change

Ambient water coverage samples 336 screen rays per frame. Each terrain ray
tests a horizon plane, marches up to 48 planes and bisects up to 20 times.
Previously every plane recomputed the same perspective camera trigonometry,
focal length, aspect, eye and ray direction. Native debugger samples identified
this repeated work in `ambience_update` through `screen_to_world`.

`ScreenPlaneRay` now holds those double-precision values for one call only.
Each tested height uses the same subtraction, division and multiply/add order,
with the same final float conversion. Nothing is reused across calls or frames.
Camera changes, terrain edits, water changes and mission reloads therefore need
no new invalidation mechanism. This also serves picking, zoom/pan anchoring and
other callers of the shared terrain intersection, not only ambient audio.

Classic's screen mapping, the flat-pack short path and orthographic equations
remain unchanged. The terrain march and bisection counts and sample order stay
unchanged. There is no audio muting, lower sampling density or rendering change.

Set `OPENCNC3D_PREPARED_PICK_RAY=0` before launch to repeat the original per-plane
calculation in the same executable. This is a diagnostic control, not a setting
players need to change. Standalone `screen_to_plane` calls prepare their own ray.

## Validation

The focused extraction test compares actual current production functions with
the committed v0.8.6 implementation, including world traversal, exact output
bits, hit flags and terrain probe sequences. It uses the native 32-bit SSE2
compiler configuration. Synthetic test worlds are not a substitute for the
in-game tests.

The scene verifier runs both diagnostic paths against unchanged game data and
compares decoded images, decompressed render targets and emitted world/picking/
ambience state. It includes camera changes, modes, horizon handling and reload.
The native `--picktest` sweeps provide additional projection/inverse coverage.

Run tests serially, with no build or debugger attached during frame timing:

```
python tools/diagnostics/windows-performance/test-screen-ray.py --help
python tools/diagnostics/windows-performance/verify-screen-ray.py --help
```

The previously accepted flag-wave checks remain relevant regression coverage.
Live comparison runs should alternate disabled/enabled/enabled/disabled using
the same executable and settings; report full frame times and pre-draw work,
not the isolated ray-math microbenchmark as FPS. Keep cold frames separately
visible, because this change does not fix texture residency or tree geometry.

## Initial native evidence

The ray-only Windows build from `71b5d1d` passed 8,280,000 exact plane checks,
240,000 world checks and 6,413,516 ordered terrain-query comparisons against
v0.8.6. Both deliberately corrupted negative controls failed. The in-game
comparison passed 12 decoded screenshots, 50 raw render targets, 9,389 world
records and 340 camera/picking/ambience records across twelve views and modes.
The native ambience signal gate also passed all seven shipped beds.

Twelve hidden live runs alternated OFF, ON, ON, OFF at 1920x1080, thirty
seconds each, with no concurrent build or debugger. The equal-run-weight
means for frames starting between five and twenty-five seconds were:

| Scenario | Ray OFF | Ray ON | Change |
| --- | ---: | ---: | ---: |
| GDI 1 | 12.325 ms | 12.037 ms | -2.34% |
| Forest | 30.706 ms | 31.961 ms | +4.09% |
| Siege | 23.441 ms | 23.077 ms | -1.55% |

Median pre-draw preparation fell from 2.145 to 0.367 ms, 2.209 to 0.516 ms,
and 2.781 to 0.717 ms respectively. Draw-submission time rose, so saved CPU
preparation time cannot be equated with the whole-frame improvement. Forest
p95 rose from 35.124 to 41.473 ms in this set. This is mixed end-to-end
evidence, not proof that forest performance or cold stalls are resolved.
The original traces and per-run statistics are retained under
`build/windows-performance/prepared-ray-timings`. Final build reports must
identify their own binary and distinguish subsequent measurements from this
ray-only build.

The first enabled Forest run contained three warm frames above 100 ms; the
other three runs contained none. Its warm mean was 33.078 ms, versus
29.911 ms in the second enabled run and 30.835/30.575 ms disabled. GPU clock
samples also differed, but cannot be aligned precisely to individual frames.
That correlation does not establish a power-state cause. The slow run remains
in the aggregate rather than being discarded; repeat comparisons are required.

## Platform scope

This work is locally validated on Windows. No Mac host or full macOS gate suite
is available. The normal two-platform stable-release rules and update feed are
unchanged. The earlier explicitly authorized Windows-only preview does not
establish cross-platform correctness. Reports must identify the exact binary
and source commit they tested, and retain any failed comparisons as evidence.
