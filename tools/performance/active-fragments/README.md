# Exact active-fragment regression

This drives the real renderer and current installed full NUKE asset (321 bodies),
with a private short-path DESERT scenario. It never substitutes older 80-piece
geometry. It checks the engine's original map fields (3,3,57,58), zero script
failures, actual wreck body count, complete decoded PNGs, every decompressed
float-target byte and gameplay/body/physics/FX state lines. Pillow is required.

Run on macOS with a native renderer and brain built from the checkout under test:

```sh
python3 tools/performance/active-fragments/verify-active-fragments.py \
  --repo "$PWD" --binary /absolute/path/cnc_eyes \
  --playable "$PWD/playable" \
  --brain "$PWD/playable/TiberianDawn.dylib" \
  --gfx /absolute/path/private-test-preset.cfg \
  --output /private/tmp/active-fragments-regression --suite all
python3 tools/performance/active-fragments/analyse-active-fragments.py \
  --output /private/tmp/active-fragments-regression
```

The preset should allocate the normal and sun targets. The fidelity suite has28
captures through movement, settling, sinking, fading, camera and fog changes.
Lifecycle adds2 captures across a real mission reboot. Matrix adds mode changes,
synthetic DARK shroud, an edge camera and relight, plus source-budget, source-
allocation, compact-allocation, compact-client, retained-budget and input-limit
fallback controls. Classic/N64 captures compare PNGs; their prior Enhanced FBOs
are not presented as newly rendered targets. All launch settings/saves and captures
stay private. The actual source/asset/binary/input hashes are recorded.

ABBA uses4 independent processes in OFF/ON/ON/OFF order, with140 measured frames
per process. Every `framebench1` follows `tick1` and a changed interpolation alpha;
these are active frames. The complete sequence holds an advisory exclusive lock
at `/private/tmp/opencnc3d-performance-gpu.lock`. Report cold first frames separately
and the distribution of paired warm work. Native timings do not predict Windows
FPS; `glFinish` totals include GPU completion and CPU section times may include
driver stalls. The source-preparation template cache is32MiB, compact scratch16MiB,
and the existing exact retained fragment cache64MiB. All are cleared on mission
shutdown. Failed compact preparation retains the original ordered batch/client
path; failed GPU verification uses exact client attributes, including compact
records when they are already prepared. Tier1 keeps its separate renderer.

Diagnostic environment controls (launch-time only):

- `OPENCNC3D_MESH_FRAGMENT_PREPARE=0`: original per-vertex source preparation.
- `OPENCNC3D_MESH_FRAGMENT_COALESCE=0`: separate original triangle draw ranges.
- `OPENCNC3D_MESH_FRAGMENT_PACK=0`: original36-byte fragment upload records.
- `OPENCNC3D_MESH_PREPARED_BUDGET_FULL=0`: force256-byte template budget.
- `OPENCNC3D_MESH_PREPARED_ALLOC_OK=0`: force source-template allocation fallback.
- `OPENCNC3D_MESH_FRAGMENT_PACK_OK=0`: force full-record assembly.
- `OPENCNC3D_MESH_FRAGMENT_PACK_ALLOC_OK=0`: force compact scratch failure after
  an ordered scope has begun.
- `OPENCNC3D_MESH_FRAGMENT_BUFFER_OK=0`: force exact client-array submission.
- Existing `MESH_FRAGMENT_GPU_OK`, `BUDGET_FULL` and `INPUT_FULL` controls remain.

`meshperfdump` emits `FRAGMENTPREP` with template hits/builds/vertices, charged
cache bytes, compact scratch, allocation fallbacks, draw ranges saved and64-bit
uploaded/full-equivalent byte totals. OFF controls conservatively retain the
metadata ownership move; that change removes a transient run-list allocation
without changing any consumed attributes or command identity.

The two scenario files are the unchanged existing flat-desert regression payload.
No source geometry, texture, debris population, timing, material, shroud rule,
shadow casting or user quality setting is altered by the optimization.

Adjacent triangle coalescing follows the existing `glDrawArrays(GL_TRIANGLES)`
semantics: complete contiguous triangles under identical state retain primitive
order. See the [Khronos reference](https://wikis.khronos.org/opengl/GLAPI/glDrawArrays).
