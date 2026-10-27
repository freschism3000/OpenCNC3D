# Enhanced follow-up evidence, 13 October 2026

Research against released source 67df3dea9468fa6c454195541c82768903373795. The report is ../reports/2026-10-13-enhanced-performance-followup.md.

- measurements.json: baseline, paired completed-render results, real-loop frame/tick counts, fidelity outcomes and Dock preservation.
- crowd-summary.json / crowd-validation.json: 80 full-detail moving infantry acceptance and timing. No isolated first-skin/clip duration or whole-run upload total is claimed.
- visual-*.json: all individual exact screenshot and float-target comparisons, including the initial range failure. The full repeat and identical-binary controls are separate evidence, not a replacement for that failure.
- source-provenance.json / patches/: complete private experiment diffs and compiled-input/binary SHA-256. Patches use zero unified context and must be applied only to the pinned source hashes (patch -p1 supports them; git apply requires --unidiff-zero). Each patch was applied to clean copies of its baseline files and reproduced the exact changed-source hashes.
- infantry-load-inventory.json: active versus unselected experimental infantry payload inventory. Only those sets are proposed for deferred reads/uploads; active vehicles/structures and all package assets stay intact.
- shipping-pe-headers.json: actual release binary PE metadata.
- official-sources.json: primary documentation URLs and supported claims, checked during research.
- baseline-fx.cfg and the four benchmark drivers preserve the measured protocol. The scripts are portable replay derivatives of the original local drivers: --runtime selects an installation and --output selects private output. Path placeholders in metadata preserve asset identities without requiring one machine; original tested-driver hashes are recorded in driver-provenance.json. benchmark.py supports the renderer --exe option and run-paired.py uses off/on/on/off. Optional GL call interposition was not used for reported timings or conclusions; a Mac caller must supply --glcount-library to enable it.
- crowded-map/, crowded-infantry-script.txt and its invocation descriptor preserve the private ordinary-order test fixture. Assign new output/save directories before another run. The INI is archived byte for byte, including its terminal blank line, to preserve the recorded tested-input hash.

Raw logs, images, float readbacks and build outputs remain under the original private research directory (local raw captures are not required by these portable drivers); raw-evidence-hashes.json records their hashes. The repository retains the small measurements and reproducible changed-source patches rather than large render captures.

The prototypes are unpromoted and are not in the Dock installation or v0.8.4. Do not apply them to production based on this evidence alone. Build an isolated copy with no playable directory/symlink present during game/build.sh, because that script otherwise stages its binary. Use current runtime assets as read-only inputs, private settings/save/output locations and OPENCNC3D_BACKGROUND=1. Future delivered runtime work must be merged and compiled in current main before Dock deployment.

All native timing is Mac. Windows estimates are conditional and overlapping. GPU timers were unavailable; CPU section marks are not per-pass GPU durations.
