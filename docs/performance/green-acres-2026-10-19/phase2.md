# Phase two performance report

**Build `v0.8.7+27f0fc5-dirty` is verified and deployed for both OpenCNC3D/Tiberian Dawn and RA3D/Red Alert.** It includes current main's concurrent terrain, material, lighting and HUD changes. The measured live results were:

| Game | Control → deployed frame time | Time reduction | Derived FPS |
|---|---:|---:|---:|
| TD | 28.68 → 22.80 ms | 20.5% | 34.9 → 43.9 |
| RA | 29.28 → 20.33 ms | 30.6% | 34.2 → 49.2 |

Each game used ABBA order, 10 seconds of warmup and 15 seconds of live simulation/draw/present per run at 3008 × 1547. Values are medians of two per-run mean frame times per arm; FPS is their reciprocal. TD run means ranged 28.16–29.20 ms for control and 22.74–22.86 ms deployed; RA ranged 29.20–29.36 and 19.44–21.23 ms. **Host isolation was not verified:** a macOS process-name truncation bug made the monitor's filter unreliable. These are observed shared-host results from approximate screenshot cameras, not a sustained or all-map FPS guarantee.

## Cause and fixes

The renderer repeatedly deformed duplicate tree corners and rescanned unchanged geometry for hovering. A TD stack sample attributed 617/2,980 main-thread samples to wind in shadows and 421/2,980 to picking bounds; these are sample counts, not frame-time percentages or GPU timings.

The shared fixes keep wind bounds local, cache exact picking bounds, and deform identical position/sway/phase inputs once before restoring the original corner order. Memory is bounded and refusal retains the original rendering path. No mesh-detail, resolution, AO-sample or shadow-quality reduction was applied.

In the final live runs, median CPU/driver shadow timing fell from 6.50 to 3.80 ms in TD and 12.06 to 7.64 ms in RA. Events/tick/refresh fell from 3.12 to 0.64 ms and 5.58 to 1.05 ms respectively. These section timers can include driver waits; they are not isolated GPU costs.

## Validation and delivery

The matched v5 builds passed **42 exact comparisons** of PNG pixels, raw render targets and matrices: 20 states per game at 1280 × 800 and a 3008 × 1547 base view per game. Full-resolution images match the previously inspected captures. ARM64/x86-64 native proofs cover exact arithmetic, cache invalidation and allocation failures, with tested code bound to v5.

All **13 hidden installed checks passed**, including exact TD/RA images with prepared wind disabled or forced through allocation/scratch fallback. Deployment completed 26 file copies; 115 protected paths and the Dock target were preserved. Asset checks, independent deployment audit, installed hashes and final integrity checks passed. Normal cleanup in both clones reported zero removals.

## Timing-policy correction and remaining cost

The original timing parser—and its enclosing monitor—reported failure after one RA reference run took one optional depth-path fallback in 28,195 measured attempts. This was a resource-path refusal, not a reported arithmetic mismatch; the renderer continued through its fallback path. Separate review retained **all eight runs and every measured frame**, including that cost, with no reruns or exclusions. The original failed receipts remain preserved. Per-draw verification was disabled during timing; image/native checks provide the equivalence evidence.

All four RA runs reached the existing 2,048-store pool limit and showed static-cache turnover. That pressure is consistent with the refusal, but its exact cause is not uniquely proven. The new prepared-wind cache remained bounded with zero measured evictions/fallbacks. Existing pool and static-cache churn remain candidates for further investigation.

## Earlier experiments

Earlier frozen prepared-wind tests showed 8.7–23.6% lower measured frame time across four TD/RA views; they forced GPU completion and are not live FPS. Isolated picking and helper gains cannot be added to the final combined result. V3's live results are retained as superseded history, including its failed post-run source-integrity receipt; v4 was rejected after a comment-only source update. Private AO experiments showed no consistent gain and were not adopted.

The scoped code was published as `f04d1183c`, `06f83ec9d` and `159a9c439`. All run values, failed originals, policy correction, source/binary hashes and delivery proofs are in the included [evidence summary](phase2-summary.json). Private raw-record aliases identify retained evidence, not external links.

## Compatibility follow-up — 20 October 2026

**The three performance fixes are already integrated with unified daylight; no additional runtime patch is required.** Local wind bounds, exact picking bounds, prepared wind inputs, `draw_mesh` and `mesh_batch.h` match between shipped v5, canonical `bf783633` working source and fetched `origin/main` at `48404c5eb`.

The remote update since `8d683b20` changes RA temperate ore assets, tooling and documentation only. Local effects/suspension differences are pre-existing divergent work, not removals in that update.

Fresh ARM64/x86-64 picker and prepared-wind cache tests passed, including allocation failures and expected corruption controls. Three hidden checks of the **existing Dock installation** passed. TD standalone and RA app exercised exact wind/picking verification with unified lighting, shadows and relief enabled: zero unexpected mismatches, fog changes and exact reveal restoration. TD app covered launch/scene rendering. Settings, protected paths and the Dock target were unchanged.

There was **no new renderer build, deployment or benchmark**. Installed v5 already combines lighting and performance fixes; current dirty infantry changes are not claimed built or delivered. Earlier timing limitations remain. See the [compatibility evidence](main-compatibility-2026-10-20.json).
