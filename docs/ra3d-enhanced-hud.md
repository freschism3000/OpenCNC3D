# RA3D Enhanced HUD — accepted v06 runtime contract

The design review approved the exact v06 browser design on19 October2026 and authorized full
Enhanced-mode implementation. Keep accepted source assets/manifest unchanged at
art/interface/ra-enhanced-hud-v06-plain-tool-wing/. Earlier v03–v05 experiments are
preserved separately. This document supplements the UI notes.

## Architecture and controls

- tools/ra3d/bake-enhanced-hud.py verifies approved PNG hashes and packs35 runtime
  records (six-state atlases plus native components), emitting ra_hud_layout.h.
  Approved six-row art is copied exactly. Compact rows2–5 preserve native rails,
  corner fittings, both112px glass source frames and whole9px tick periods.
- game/ra_hud_pixels.h provides alpha-correct native-pixel raster helpers and pack
  contract validation; cnc_sidebar_ra_enhanced.h connects the presentation to the
  existing sidebar/controller state. TD art and Classic remain separate.
- CreditsCounter, PowerProduced/Drained, real radar/shroud/camera transforms, real
  build-potential and live entries remain authoritative. Categories include Ships;
  captured-faction builds and special weapons keep their engine identities.
- Shared controllers handle repair, sell, synchronized/saved building power, build,
  hold/resume/cancel, placement, queues and scrolling. Naval units also queue.
  Disabled/spectator controls stay silent. HUD audio keeps its existing hit route.
- The272x166 RA card reads real selection, health and group membership. The six small
  additional-selection thumbnails use existing selection controls. Main portrait
  uses RA cameos, never TD look-alikes. Groups include naval units.
- Fixed-function UI textures reserve512 columns to fit the272px card; UVs and all
  uploads use that same allocation. UI passes isolate alpha/blend/texture state.
- Native reference216x693,80x60 cameos, six rows, tool wing85x165 at(-63,27),7px
  tool clearance,22px underlap. Surface/hit/radar coordinates share the layout.
  For sub693px windows,2–5 rows fit at native density; minimum two-row housing450px.
  Narrow640px windows use fitted132px left header plates without stretching ends.
- Every layout recomputes RA's HUD eligibility after scene fallback/mode switches;
  an unavailable Enhanced scene cannot accidentally display TD chrome in Classic.

## Build and staging

The RA Enhanced HUD uses its own validated `ra3d/enhanced-hud.pack`; TD's optional
`hud640.pack` is not a prerequisite. Shared layout, rows, scale, chrome hits,
radar coordinates, Database and selection-card eligibility use the same
module-aware active-HUD predicate. Actual TD asset reads remain on the TD branch,
and the two packs retain separate ownership. A missing/corrupt RA HUD pack or
Classic fallback disables the RA Enhanced path safely.

The25October correction removes the stale TD-pack guard that could load the
approved RA pack yet leave its controls in the DOS layout. Actual-source native
CPU comparisons against the prior code pass144 cases per revision: TD behavior
and RA with the TD foundation remain exact; RA without it has the same layout,
radar, header hits, Database/card eligibility and native housing composition.
The combined candidate passed the hidden native matrix in both foundation arms:
both factions at640x480,1280x720 and2560x1440, all five categories, exact approved
opaque HUD/tool/minimap-frame pixels and identical controller walks. Database
and selected-card rectangles and their controls match exactly across the arms.
G356 passes all special weapons without either TD HUD sheet; TD G53 controls
and HUD pixels remain exact against the sealed prior renderer, and G326 soft
shroud checks pass on both. Independent live-world frames are not claimed byte
identical. Local receipt: `docs/verification/2026-10-25-module-release/ra-hud-independence/NATIVE-RESULTS.json`,
SHA256 `d9890d4c5e4bf30c1d9db6a75dc2d4f60c34a0273ca278eb31658211f546c3f0`.
Approved artwork and geometry are unchanged. Current-main release integration
and verified Dock delivery remain pending; this candidate proof is not delivery.

Both native build scripts bake the header/pack before compilation. make-build.sh
and RA module frontend staging install ra3d/enhanced-hud.pack. Mac build scripts
refresh the existing TD and separate RA app aliases via their normal stage helper;
keep the Dock bundle/launcher and all user saves/settings intact.

## Verification and Dock delivery — 19 October 2026

Status: approved v06 implemented and installed. Native changes are committed as
`561eb664a` and `cc9cf8af6` in the shared main source, and integrated as scoped
commits `39e403241` and `52aa5857a` on GitHub main. The app/renderer were built from current main in
`./`, including the current HUD, fog, vehicle-turn and Desert fixes; both TD
and separate RA app aliases are refreshed. The Dock bundle/launcher, settings and
saves were preserved. Hidden tests ran from that installation without a visible
window. Exact hashes, reproducible scripts and actual captures are in
`docs/verification/2026-10-19-ra-enhanced-hud/`.

Both factions passed at640x480 (2 rows/1x),1280x720 (6 rows/1x),2560x1440
(6 rows/2x): five category picks, source-frame pixel identity, power warning/input,
collapse, Classic isolation and return. The final matrix follows the radar fix.
Live engine tests placed a barracks and naval shipyard, held/resumed/queued
infantry, spawned two queued Patrol Boats, selected/grouped units and displayed
real health. Repair healed a damaged FACT; selling removed the chosen POWR.
Power switching changed a real plant's `poweroff` field and production300->200W,
and save/load restored its off state at frame56. No browser timing fixtures are
used by the runtime.

The installed RA brain predated existing source support for power/build-potential;
rebuilt/staged the current brain before judging controls. The installed RA menu
pack was also stale; rebaked356 current assets and passed the existing campaign,
pause/save/load, skirmish, multiplayer room and debrief gate. Missing24 original
multiplayer INIs were extracted from the owned RA archive; existing missions stay.
TD fog regression G326 and its HUD/card/group17-probe check passed.

Failure lessons: recompute eligibility after forced Classic fallback; test an
onscreen building at observed coordinates; validate the installed engine rather
than assuming its source version; back transparent radar fog with black inside
its opening. First test used a placeholder scene, an offscreen camera and later
two unsupported TD diagnostic commands; final checks use own scenes, engine
coordinates and supported band/group verbs. Approved6720 tool-wing and16836
minimap-frame opaque pixels remain exact in every matrix case.

## RA Enhanced settings correction — 19 October 2026

The design review found New HUD and3D Trees greyed out and requested HUD/trees/grass on by default
in Enhanced. The old Enhanced menu capability list still disabled implemented RA
features; remove that restriction and the obsolete no-HUD-art message. RA grass's
compiled override and shipped/saved defaults were off. All three now default on;
old RA presets migrate once via `ra_enhanced_features`, while later saved opt-outs
remain valid. Classic/N64 mode preferences stay separate.

Required delivery lesson: test the menu seats used by the player, not only sidebar
script setters. `tools/ra3d/gate-enhanced-options.py` clicks HUD/trees/grass off/on,
saves and reloads, checks actual tree models/grass geometry and unchanged display
preferences. Native tests exposed missing approved tree files and an unrelated
custom-resolution overwrite;100 exact approved current-main tree assets are now
staged, and only explicit resolution selection/display-mode changes commit sizes.
No new artwork or geometry was authored. Approved v06 HUD bytes stay unchanged.

Both factions passed46-step hidden installed scripts (zero failures), the shared
native menu gate (90 RA pictures), default/migration unit checks and all six HUD
matrix cases. Soviet2 drew34 enhanced trees (0 off,34 restored) and55,127 grass
blades. Both current-main TD/RA app variants and renderer are delivered to the Dock
installation. Only requested feature settings/marker changed; display, other tuning,
controls, saves and Dock launcher preserved. Exact hashes and actual screenshots:
`docs/verification/2026-10-19-ra-enhanced-settings/`.
