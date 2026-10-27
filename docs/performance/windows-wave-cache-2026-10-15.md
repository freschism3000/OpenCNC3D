# Windows damaged-Barracks flag performance

## Diagnosis

Measured on Windows 11, Core i5-12400F, 16 GB RAM, RTX 3070, NVIDIA 581.32.
The display is 2560x1440 at 59 Hz; controlled rendering tests use 1920x1080.
No driver settings, power plan, resolution reduction or quality reduction is
used to obtain the improvement.

Seven of ten native debugger samples were inside the Barracks wave calculation,
including its power/trigonometric functions and twelve-sample cloth-length
integration. Samples reached it from shadow, normal and glow rendering. These
samples identify the hotspot; attaching a debugger perturbs timing, so their
frame times are excluded from the performance comparison.

The renderer shares a model's mesh among buildings and detached pieces. The
standing flag needs the current pose, but torn pieces need their tear-time
pose. Partial posing invalidates the mesh's whole-pose time, even when the next
pass asks for precisely the same wave calculation again. Shadow, color, normal
and glow passes repeatedly redo expensive pure mathematics. This is CPU work,
not evidence that the GPU's low observed clock is itself a driver defect.

## Fix and bounds

`game/ebld_mod.h` retains two exact-time poses per source wave triangle. The
cache stores position, normal and the pre-gamma fold-light factor. Color
conversion, mesh writes, deformation revisions and draw ordering remain on
their existing paths. No simulation or rendered detail is removed. Current
animation and frozen tear poses can coexist. A third time replaces one slot;
there is no unbounded history or approximation of the animation clock.

Storage belongs to the model and is cleared when its immutable wave records
are rebuilt. Each of the intact and cut sets is capped at 2048 cache entries.
Triangles beyond the cap use the original calculation. Allocation failure
also uses that calculation. The shipped Barracks has 64 intact and 204 cut
wave triangles, approximately 50 KB of cache payload with this compiler.

Diagnostic controls, sampled at process startup:

- `OPENCNC3D_EBLD_WAVE_CACHE=0`: original calculation, in the same executable.
- `OPENCNC3D_EBLD_WAVE_CACHE_ALLOC_OK=0`: exercise allocation-failure fallback.

## Initial diagnostic timing (superseded by final matched-runtime validation)

An OFF, ON, ON, OFF sequence on the same optimized native executable gave these
per-run median milliseconds, with live simulation ticks and sub-tick alpha
changes between frame samples:

| Scene phase | OFF 1 | ON 1 | ON 2 | OFF 2 |
| --- | ---: | ---: | ---: | ---: |
| Moving debris | 91.200 | 27.150 | 26.865 | 88.655 |
| Settling | 90.940 | 26.415 | 26.545 | 93.450 |
| Sinking | 86.215 | 16.770 | 16.895 | 83.415 |
| Late | 84.085 | 16.625 | 16.070 | 86.080 |

These initial native runs used a reused UCRT engine DLL with an MSVCRT renderer.
Later matrix and transition tests caught missing world snapshots in the OFF
reference runs, including a frozen exported engine frame and disappearing
objects. They were rejected, not accepted as rendering parity. The native
builder now detects a compiler/baseline C-runtime mismatch and rebuilds the
engine; it verifies all three game entries and the engine import the same
runtime before staging. The rebuilt matched-runtime candidate passed both the
healthy-to-damaged and mode-switch comparisons, including allocation fallback.
Final proof must use the final clean committed matched-runtime build.

These are preliminary scripted synchronized frame times, not release acceptance
results or a promise of displayed FPS.
The packet's phase called `intact` already contains a damaged Barracks at
313/800 health; do not describe it as a healthy-building baseline. A separate
healthy-to-damaged test asserts both actual engine stages.

The released v0.8.5 executable was kept untouched and separately verified
against all 942 manifest entries. Its compiler differs from the native GCC
16.2 build; comparisons against it are separate from the causal same-binary
OFF/ON experiment. Early exploratory runs during setup/build activity are not
the final controlled timing evidence.

## Verification and reproduction

Requires the verified baseline, native candidate and comparison packet from
`tools/diagnostics/windows-performance/Setup-Windows-Performance.ps1`, plus
Python 3 and Pillow. Run one renderer test at a time, with no concurrent build.

```
python tools/diagnostics/windows-performance/verify-wave-cache.py --game playable/development --output build/windows-performance/wave-cache-final --suite all
python tools/diagnostics/windows-performance/measure-scenes.py --game playable/development --output build/windows-performance/live-active --case active --visible --live 60
```

The verification runner covers 28 destruction/animation/fog/camera checkpoints,
mission reload, mode switches, shroud, relighting, healthy-to-damaged Barracks,
and allocation-failure fallback. It compares decoded PNG pixels, decompressed
render targets and engine/physics/effect records exactly. It refuses missing
outputs or changed executable/assets. The first 28-checkpoint comparison
matched all 196 image/buffer outputs and 15,520 state records exactly.

The verified Windows v0.8.5 NUKE asset reports 80 wreck bodies. These tests keep
that shipped asset unchanged; they do not claim the separate Mac fixture's
321-body geometry. Every candidate/reference input asset hash is recorded.

The scene measurement runner also supports Siege, GDI 1 and GDI 2, visible or
hidden, with GPU clock telemetry and live frame percentiles. Diagnostic
profiling is explicitly recorded. The final release report carries the final
binary hashes and completed test results; these initial measurements are not
a substitute for testing the exact packaged executable.

## Prerelease policy

The design review explicitly approved a Windows-only portable prerelease because no
Mac host is available and GitHub Actions is blocked by spending limits. Use
`package-prerelease.py` only with that explicit authorization. It requires a
clean committed main, a matching native build receipt, all verification cases,
unchanged baseline engine source, verified runtime data and exact ZIP content
hashes. It does not publish to the automatic update feed. The normal
`tools/release.sh` two-platform stable-release process remains unchanged.
