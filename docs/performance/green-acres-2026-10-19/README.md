# Green Acres performance investigation, 19 October 2026

**Follow-up:** The [phase two report](phase2.md) records the later CPU wind and picking optimizations, final live measurements and current Dock delivery. The results below retain the first investigation as historical evidence.


The reported TD scene is the first skirmish, SCM01EA / Green Acres, with fog of war disabled. The installed launch log records eight players, seven Normal AI, build level 7, 5,000 credits, and no starting combat units. The saved renderer uses 3008×1547, scale 1, a 4096 shadow map, 18 SSAO samples and three contact-shadow steps. The Oct 18 wind-matrix optimization is present in the installed binary.

A close reproduction uses camera 44,36, distance 4200, tick 400, seed 77 and starts 5,1,2,3,4,0. The camera is inferred from the screenshot and launch telemetry; the exact moved camera and random seed were not saved. The reference scene is therefore a close reconstruction, not an exact replay.

## Findings

The installed renderer measured 27.26ms median over 120 GPU-completed frozen frames. A later live-play window averaged 30.25ms, with a 50.3ms worst frame. Shadow-map submission consumed about 10–12ms of measured main-thread frame time. A native stack sample confirms substantial work in both the general mesh vertex loop and the existing wind-position helper.

The opaque sun pass still rebuilt full tree vertex records and copied their positions into a second upload buffer. It also repeated wind-direction trigonometry per vertex. The Oct 18 matrix work covered another path. The geometry cache was below its 64 MiB cap and direct-position fallbacks were zero in the reproduction.

GPU timestamps are unavailable on this Mac's OpenGL 2.1 Metal driver. An opt-in serialized diagnostic now completes GPU work at each pass marker. These numbers change CPU/GPU overlap and are **not live frame times**. Its full-resolution toggle run measured 28.75ms baseline and 28.57ms on returning to baseline; disabling SSAO measured 22.32ms, contact shadows 25.03ms, sun shadows 23.52ms, bloom 28.61ms and tiberium fog 28.44ms. Tree removal was a diagnostic only and measured 14.76ms. Full-resolution ambient occlusion and tree/shadow work remain material costs; fog-of-war visibility exposes more of that work.

Other applications and a separate compiler were active during testing. Timings varied considerably, including within ABBA runs. The included results must not be represented as a guaranteed FPS gain or a verified 60 FPS target.

## Changes

- Hoist unchanged wind coefficients out of the general vertex loop and reject empty passes before wind work.
- Submit eligible opaque wind-driven sun casters directly as positions, keeping their original vertex order, bounds and draw state. Later passes can reuse the same frame-owned position buffer.
- Keep cutout alpha, normals, animation, ground-conforming geometry, shroud and existing fallback behavior intact. A separate counter and original-path switch make coverage measurable.
- Add opt-in per-pass synchronized profiling for drivers without timestamp queries. Normal profiling and gameplay remain asynchronous.

All renderer changes are shared by TD and RA3D. No geometry, textures, shadow resolution, AO samples or saved tuning are reduced.

## Validation and reproduction

Native extraction tests compare production positions and bounds against the saved pre-change pipeline on arm64 and x86_64. Each depth run covers 24,000 fixtures, including 7,659 opaque-depth cases, strict eligibility and negative controls.

The hidden shadow suite covers both games, nine states each, three zoom/camera views, changing wind phases, nonuniform tree transforms, fog and reveal restoration. The optimized and verifier arms match all 252 compared PNG/raw targets exactly: scene, depth, normals, sun, terrain-sun and infantry-sun. Allocation and upload failure arms also preserve all 54 compared final images.

Use `tools/perf/shared-tree-compare.py --optimization shadow --td-skirmish` with private presets. `OPENCNC3D_MESH_WIND_DEPTH=0` restores the full opaque shadow path. `OPENCNC3D_MESH_WIND_DIRECT_VERIFY=1` compares generated positions and bounds to the full loop. `OPENCNC3D_FRAMEPROF=1 OPENCNC3D_FRAMEPROF_SYNC=1` enables serialized attribution, explicitly labeled in output. Keep that last switch off for normal benchmarks.

Local evidence is kept under `${EVIDENCE_ROOT}`. Machine-specific launch paths remain in those private receipts; portable summary data accompanies this report. Completed delivery and final combined-source verification are recorded below. A dry-weather shader experiment was reverted because its same-binary ABBA test did not demonstrate a reliable improvement.

<!-- final-delivery:start -->
## Final combined-source verification and delivery

The delivered build is `v0.8.7+396eaf9-dirty`, from combined main `396eaf98c81b320cd57c0c80d3fe2e1868d12051`. Both the standalone renderer and unified app contain arm64 and x86_64 slices. The matched arm64 control uses the same fresh auxiliary objects, build stamp and source base, with only this wind/shadow optimization reversed and an exact source roundtrip verified.

The following values are medians of the two per-run medians in baseline/candidate/candidate/baseline order. Each view uses 20 warm frames followed by 120 GPU-completed frames at 3008×1547. A positive reduction means less frame time; negative values are measured regressions.

| Game / camera x,z / distance | Control | Delivered | Frame-time reduction | CPU shadow control → delivered |
|---|---:|---:|---:|---:|
| TD Green Acres / 44,36 / 4200 | 24.24 ms | 21.35 ms | +11.9% | 8.83 → 5.94 ms |
| TD Green Acres / 42,34 / 3000 | 23.16 ms | 19.04 ms | +17.8% | 7.99 → 5.10 ms |
| RA Coastal Influence / 32,43 / 4200 | 23.46 ms | 21.66 ms | +7.7% | 13.18 → 8.09 ms |
| RA Coastal Influence / 27,36 / 3000 | 24.75 ms | 24.39 ms | +1.5% | 11.36 → 7.33 ms |

All views showed lower frame times in this run.
The host was shared, so timing is noisy and these results neither guarantee live-match FPS nor verify 60 FPS. The cameras are approximate reconstructions, not recovered exact screenshot cameras. CPU shadow values include only profile windows after each 20-frame warmup result and before its 120-frame result; initialization is excluded. These are CPU/driver timings, not isolated GPU timestamps.

The final fidelity suite passed 252 byte-exact image/raw-target comparisons across both games, including scene colour, depth, normals and shadow targets. All verifier mismatch counts are zero. The full-resolution matched-control screenshots also match exactly.

All six hidden installed smoke cases passed across the canonical and actual Dock installations. The deployment preserved 70 captured settings/save/bundle files; the Dock target stayed unchanged during smoke, and tested fog/reveal restoration was exact. Both modules use the verified binaries. The four already-approved RA ore files required by the merged source were staged to both installations after checking all nine approval stamps; every destination hash matches.

### Short TD live-loop check

A separate Green Acres check ran the real tick/present loop for eight seconds per run in ABBA order. Only the final profile window after approximately five seconds of live warmup is summarized. The combined means and CPU shadow values below are weighted by each tail window’s frame count; the ranges retain both individual runs.

| Arm | Run mean range | Weighted mean | Run worst-frame range | Weighted CPU shadow |
|---|---:|---:|---:|---:|
| Control | 20.94–23.32 ms | 22.07 ms | 28.7–29.8 ms | 9.64 ms |
| Delivered | 20.43–22.40 ms | 21.36 ms | 36.0–36.9 ms | 7.39 ms |

The delivered build’s weighted mean was 3.2% lower in this check. This short shared-host sample does not guarantee sustained FPS or 60 FPS. The run ranges show variation, and the camera remains an approximate reconstruction.

One reviewed concurrent preset update at 22:36 preceded deployment. It changed terrain-shadow strength, Desert colour controls and muzzle/explosion tuning; it was preserved as found. Ore staging, binary deployment and installed smoke each kept all 70 captured protected files unchanged during their respective checks. Matched benchmarks intentionally use the captured original preset so both builds receive the same settings.

An additional hidden TD check used the current installed preset and the delivered renderer. The preset hash remained unchanged, fog changed the image, and revealing the map restored the exact pixels.

Overall live performance improvement remains inconclusive; the low-FPS complaint is not fully resolved. GPU ambient-occlusion and lighting work remains a material cost. The next candidates are a dedicated AO pass with depth-aware lower-resolution evaluation and reconstruction, or cached AO for static terrain. Either requires matched image-quality and regression validation. Neither rendering-quality change ships in this update.

Exact binary/source hashes, individual runs, comparison counts and preservation details are in [final-delivery.json](final-delivery.json). Receipt paths use `${SOURCE_ROOT}`, `${DOCK_RUNTIME}` and `${EVIDENCE_ROOT}` aliases; aliases are descriptive until resolved locally.
<!-- final-delivery:end -->
