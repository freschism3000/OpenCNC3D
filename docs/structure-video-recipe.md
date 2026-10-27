# Structure and vehicle showcase video recipe

Current direction: 19 October 2026. This is the current rule set for new structure and vehicle films and their revised capture tools. The historical implementation and original 22-film plans live in the turntable tools of the earlier film branch (`tools/turntable/`); those old binaries and captures do not establish the current build or visual settings.

## Camera direction

Begin each new rotating 3D structure shot on the structure's **right side**, preferably its front-right three-quarter view. Move smoothly through the front toward the opposite side. Apply this in both N64 and Enhanced, including revised vehicle showcase camera paths. A vehicle’s heading and a structure’s front are different references: never label a positive world yaw “right” without viewing that asset and shot. Confirm the side against the actual structure's front/door/gate; a yaw sign alone is not sufficient proof. Classic retains its authentic fixed projection.

Use the actual front/door and a saved native view to identify the side. A world yaw sign is not a universal physical-side rule. The generic historical sign reversal was insufficient and is superseded by asset-specific camera references. For the current War Factory, the rejected view started at **+55°** and showed the plain wall; the requested **chimney/right-side** view starts at **−55°**, then sweeps **+1.5°/s toward the front**. Apply this in N64 and Enhanced, including vehicle-production openings. The actual closed/open/rollout comparison is saved in the current film’s `v05-production/factory-side-review/`; use its −55° views as the matching reference. Classic retains its authentic fixed sprite projection.

Aim at the complete footprint center. Maintain continuous position, yaw, pitch and distance within a shot and across a mode-comparison wipe of that shot. Establish the target and distance before entering freecam; drive each pose from absolute film time rather than elapsed capture wall time. Keep the accepted wider framing from the current showcase reference. Never horizontally flip models, insignia or footage to change sides.

## Smooth capture

Capture rotating N64 and Enhanced shots at **native 60 fps**. The engine remains at 15 ticks per second: explicitly enable smooth animations and render all four subframe phases `0, .25, .5, .75`. Update the camera on every output frame. A 30 fps recording merely relabelled 60 fps, duplicated frames, and optical-flow fabrication do not satisfy this rule.

The v04 vehicle films demonstrated that 60 fps metadata and four phases alone do not establish satisfactory motion. For the v05 Enhanced rerenders, use 1.5× internal spatial anti-aliasing and four real native exposure samples per output frame, over a 180° shutter: offsets .3125, .4375, .5625, .6875 within the 60 fps frame, with tick alpha (frame-within-tick + offset)/4. Drive camera and simulation interpolation at every sample; average in linear light before badges, swipes and picture fades. Record this capture-only AA override separately from the accepted F5 lighting defaults. Keep the camera sweep slow and continuous.

Review a contiguous full-resolution rotating section, including the mode wipes. Check camera pose increments, frame timestamps and adjacent frame differences for holds or jumps, not only contact sheets. Ground, model, turret and effects should follow the intended clock. Classic may retain its original sprite stepping. If further temporal sampling is used, record its shutter and sample times.

## Current build, assets and visual tuning

Use the renderer from the current canonical main checkout and current approved installed assets. Freeze the executable and engine for each capture batch; record executable, engine, model, paired material/specular files and relevant terrain inputs with hashes. Compare the filmed model against current main's asset before filming. Do not reuse an old isolated Mammoth filming build or its footage for a new-build revision.

Dump fresh compiled F5 defaults from current main and verify the live Enhanced settings against them. The accepted 19 October tuning and heading interpolation remain required. Camera composition and hiding the HUD are capture controls; do not replace the accepted lighting with old filming defaults. Use native GDI/Nod material mapping rather than multiplayer RED. Film existing approved models without geometry changes.

## Music selection

Randomize music independently for each new film from the nine approved structure-film tracks:

- We Will Stop Them / Deception (OST Version)
- Die (Remastered)
- ReCon (Remastered)
- Iron Fist (Remastered)
- Depth Charge (Remastered)
- Creeping Upon (Remastered)
- Airstrike (Remastered)
- Fight Win Prevail (Remastered)
- March To Doom (Remastered)

Use `tools/video/select_showcase_music.py PLAN.json SUBJECT...` to create and save a random batch seed and draw **without replacement** within the batch. Existing saved plans retain their selection; `--seed` reproduces a new plan deterministically. Mammoth and Harvester must receive different tracks. If a batch exceeds the pool, reshuffle only after exhausting it and avoid an immediate repeat. An explicit track request for a particular film overrides the random draw for that film only. In Trouble was requested for the trailer; do not hardcode it into every showcase.

Save the chosen title/version, source URL, file hash, seed and approved pool in each film's plan. Keep a chosen track fixed for reproducible technical rerenders unless the design-review requests new music. Use approved source recordings, including the requested official Frank Klepacki Topic uploads where available.

## Vehicle story — current general rule

All vehicle showcase films **except Harvester** begin with GDI and repeat a real War Factory opening in each comparison mode:

1. **MS-DOS:** the War Factory opens its doors and the featured vehicle drives out.
2. **Amber left-to-right swipe.**
3. **N64:** repeat the same GDI factory-door rollout.
4. **Amber left-to-right swipe.**
5. **Enhanced:** repeat the GDI rollout. Once the vehicle is clear, ease the camera wider with both an eased start and stop. Follow the vehicle driving for **2–3 seconds**, then have enemies attack it and show it defending itself.
6. **Amber left-to-right swipe.**
7. Continue the defense in the other theater as **Nod**, using Nod’s native faction materials rather than multiplayer RED. Keep the vehicle alive until the end of the final section.

Use native production, door animation, movement, incoming fire and projectiles. Measure the native rollout before timing the edit: a Mammoth needs roughly seven seconds to clear this factory. Do not interrupt its tethered exit track by ordering it through a wall. For Mammoth, turn its turret between several vehicles and include both cannon and Tusk rocket salvos. The v05 reference uses a 40-second edit to give the rollout, 2.5-second follow and multi-target fight room; it supersedes the older stationary-fight opening.

### Straight exits, arriving enemies and dressed bases — 19 October revision

The featured vehicle must drive **straight out through the center of the War Factory door**, with its hull pointing along its direction of travel. Keep that straight line until the complete vehicle has cleared the doorway and slab; only then turn toward the follow route. Do not let the original curved exit track make the vehicle slide diagonally through the jamb, and do not issue a movement order while its factory track is still tethered. Validate the actual centerline and hull heading in the rendered rollout, including N64 and Enhanced, before full capture. Preserve the right/chimney camera side.

**No enemy may materialize inside the filmed frame.** Create attackers beyond the actual camera frustum, with clearance for their whole model and shadow, then let them drive toward and attack the featured vehicle using native movement, targeting and weapons. Check the camera pose at the exact creation frame, including pullback and orbit; a map-cell distance alone is not evidence of being off camera. Record creation positions, projected screen positions, subsequent native movement, first visible entry and incoming fire. Do not use stationary in-frame spawns or merely hide them under an impact effect. Keep the initial 2–3-second hero follow before the fight begins.

Stage factory and refinery shots as **small believable bases**, rather than isolated structures on empty ground. Include a few appropriate supporting structures, short perimeter wall sections and open vehicle entrances. Add a few trees, foliage clusters and natural edge details that belong to the theater; use sparse desert trees/cacti in Desert rather than Temperate vegetation. Preserve a clear view of the hero and door, the straight factory exit lane, enemy approaches and the Harvester's resource/refinery route. Dress the same set consistently across the mode comparison. Use existing approved native assets; this staging does not authorize model redesigns.

The v06 production controls use explicit private filming environment flags for a measured native straight factory rail and enemy attack orders; ordinary launches retain the original simulation. Save the exact flags, binaries and scripts in the capture manifest. Do not make this a user setting or silently change normal or multiplayer production rules.

**Harvester is the exception:** retain the approved harvesting and refinery-unloading story, different theaters and maximum 30-second duration. Do not add a combat or War Factory opening. Its current revised edit remains 29 seconds. Correct the actual N64/Enhanced camera paths, rather than only updating this document. The v04 rejected GDI start was normalized −107° and Nod +45°; v05 physically reverses those starts to approximately +107° and −45°, then sweeps slowly at 1.5°/s. Do not flip footage or markings.

For a moving follow shot, record the deterministic native unit positions first, then interpolate that measured path at each exposure sample. Keep the pullback and target handoff continuous. Save the actual start views with each film so later captures can match the camera side from visible evidence.

## Story, transitions and audio

Retain the approved per-film story and timings, subject to the current vehicle opening above. Structure films show Classic placement, N64 placement, Enhanced placement, attack, damaged stage and destruction. Preserve a pause for the Enhanced placement's impact and dust. Use amber left-to-right swipes throughout, the existing platform badges and OpenCNC3D branding. Fade the entire picture and all audio to/from black/silence. Keep game action and effects audible under the music, and keep the whoosh subtle.

The native WAV tap records 66 ms for each 15 Hz tick: measured recorded time is approximately .99 times simulation time. Production/build-ready preludes advance the simulation too: read the actual engine frame from native TICKALPHA logs rather than counting only explicit tick commands. Map source samples from the actual tick clock and measured recorder ratio, including every subframe phase. Do not reuse the historical fixed-start-shortfall approximation. Check encoded true peak and fades.

## Background capture and delivery

Use hidden `--script` capture with `OPENCNC3D_BACKGROUND=1`, private missions, private settings/visuals/saves paths and no visible or focused game window. Preserve the user's settings and unrelated assets. Do not replace the Dock installation with a filming executable.

Verify the final export's actual dimensions, native frame cadence, frame count, duration, audio streams, full decode, black end frames, framing and movement. Classic's usable tactical viewport is narrower than the capture window; widening may require verified adjacent scene views, rather than cropping into black margins. Preserve original sprite animation cadence when taking those views. Deliver hash-verified versioned files to the existing Desktop film folder and retain prior versions.

A recipe update does not itself rerender the historical 22 structure films. New captures and requested rerenders must use these current rules.

## Verified v05 capture notes

The current factory fixture must be discovered by a friendly unit for the engine to expose its build choices. A private scout reveals it and moves off camera before production. `build HTNK; ready HTNK 3000` then reaches the native door sequence. Keep filming helpers, tech/immunity staging, settings and saves private. The Mammoth capture uses a private invulnerability switch until the final native destruction cue; this is film staging, not a delivered gameplay change.

The current renderer writes PNG with zlib level 6, which unnecessarily slows multi-sample capture. A capture-only `png_transport.dylib` interposes compression effort at level 0; it changes lossless PNG storage, not pixels, rendering or simulation. Freeze and hash that helper beside the unchanged current-main renderer/engine. Four source PNGs are integrated into one lossless exposure before compositing. No Dock installation or user setting is changed by these filming controls.

Leave enough map around the entire pullback and orbit. A factory placed near the original campaign-map edge passed timing/weapon tests but exposed a black void in the wide shots. The v05 final Mammoth set was moved into the interior of the full Temperate map before delivery. Review source starts, widest midpoints and ends for edge-connected black regions as well as ordinary Classic viewport margins; framing checks must cover N64 and Enhanced too.

Make repeated captures independent of saved private visual settings. A prior capture’s remembered Perspective flag could change the free camera’s frozen starting pitch on the next run. Start with a fresh private visuals profile each time and explicitly enable Enhanced perspective **before** entering freecam. v05’s final Enhanced shots use the current F5 53° tilt; N64 keeps its native 44.69° tilt. Keep the recorded yaw path and footage framing stable. For Harvester’s interior field, translate the complete set (units, structures, terrain, Tiberium/other overlays and ground tiles) together; moving only units and buildings removes the harvestable resource from the shot. Validate actual `harvesting=1` before full capture.

## Current v05 delivery reference

The 19 October v05 technical revision contains a 40-second Mammoth and 29-second Harvester at 2560×1440/60 fps. Production scripts, frozen runtime/input manifests, private missions, actual camera-side references and full QC evidence are kept outside the repository in the vehicle-showcase output's `v05-production/` folder; final film/QC folders are its siblings `mammoth-v05/` and `harvester-v05/`, and the versioned films are delivered beside them. Technical checks pass; artistic acceptance of the new films remains with the user. This filming revision changes the recipe and capture controls, not the game runtime or Dock installation.

## Current v06 staging and delivery reference

The 19 October v06 revision applies the straight-exit, arriving-enemy, small-base and foliage requirements to the 40-second Mammoth and 29-second Harvester films. Both remain 2560×1440 at native 60 fps, with amber swipes, current accepted F5 lighting, four real Enhanced exposure samples and the existing different approved music selections. Harvester keeps its mining/unloading exception.

Native rollout QA compares 119 non-limbo samples against the pre-change engine: normal launches match exactly. The explicit private filming rail has a maximum cross-track error of 0.00204 cells and a constant moving hull heading of 135/256 turns. This is the measured Mammoth/WEAP reference, not permission to assume every future vehicle has the same door clearance. All six enemies are created outside the actual filmed image and move across native cells under Attack missions aimed at the hero. Validate whole-model/shadow clearance, not just an anchor; the saved native counterfactual birth-frame comparison shows no in-frame enemy body or shadow. Diffuse illumination from an off-screen headlamp may reach the frame naturally before its vehicle arrives. GDI incoming fire begins after the measured 2.5-second follow. Both Harvester sets still show native mining and refinery lift/unloading.

The small sets use existing native buildings, perimeter wall sections, open travel lanes and theater-specific trees/cacti. Match set locations across mode comparisons. Freeze private INI/BIN, capture scripts, flags, camera/clock traces and asset/runtime hashes with the film. v06 integrates source samples to lossless exposures and records their hashes before removing new temporary PNGs; previous film sources and deliveries remain intact.

Production and QA: the vehicle-showcase output's `v06-production/` folder, outside the repository. Final media/QC: its siblings `mammoth-v06/` and `harvester-v06/`, with the versioned films delivered beside them. The opt-in filming controls were built from canonical main and delivered to the Dock installation, with refreshed TD and separate RA executables; hidden installed-runtime HUD, fog and RA selection/movement checks passed. Ordinary launches retain their original simulation. Technical verification does not imply artistic acceptance of the new films.

## Base layout, closer framing and terrain — 19 October v07 direction

The user's v06 screenshots reject wall T junctions and isolated wall segments. Use a coherent perimeter with deliberate entrances aligned to roads. Remove inherited wall overlays before placing the new perimeter. Validate its native cell graph: no degree-three/four junctions, no isolated pieces, one connected boundary path, with endpoints only at the intended entrance. Check the native N64 and Enhanced views too; a cell-graph check alone does not establish that a junction looks right.

Every supporting structure, including the Repair Bay and Power Plant, must fit inside the perimeter. Measure complete native building/apron footprints from current source; guessed 2×2 bounds missed several larger aprons. Keep all footprints separate and inside the boundary, and preserve the straight factory exit, travel lanes and refinery approach.

Frame all vehicle sections **15% closer than v06**. Apply a consistent scale change: Classic/N64 crop 1.472 instead of 1.28; Enhanced requests a lens narrowed by tan(old_fov/2)/1.15. The native F5 lens rounds to whole degrees: the v07 request of 35.1247° becomes 35°, approximately 15.4% closer than the former 40° lens. Record the actual saved value rather than claiming exact sub-degree precision. Preserve the approved camera side, slow orbit, eased pullback and motion blur.

Author a few coherent native road routes connecting entrances and working areas. Give the terrain visible relief, with raised terrain and cliffs beyond the usable base ground, and coherent road routes through deliberate entrances. Cliff art must carry matching non-flat elevation; painted cliff tiles on a flat heightmap fail this rule. Private showcase maps must include their own complete 65×65 `.HGT`, passed through `--reskin`, and the native log must confirm it was applied. Without that file the renderer deliberately flattens the borrowed pack. Verify actual height differences and native views, keep building aprons level, and keep ramps/passability compatible with unit routes. Use theater-correct cliff and road templates.

**No fence or wall may run over a cliff face or hang across an elevation break.** Design the perimeter and terrain together. Keep the entire perimeter on level, buildable ground, with a clear cell between it and cliff art; place raised terrain outside it. Validate every wall cell against the native cliff tiles and all four heightmap corners, and inspect the actual orbit views. A connected cell graph does not establish safe terrain placement. Keep complete building aprons level too.

Preserve the existing Amber-only transitions, 1440p60 format, per-film approved music, native faction materials, backgrounds and foliage. Capture and deliver the revised films as new versions, retaining prior exports.

## Verified v07 scene revision

The 19 October v07 films retain 1440p60, the 40-second Mammoth and 29-second Harvester stories, real Enhanced shutter sampling and different persisted approved tracks. Every initial perimeter is one connected path with a deliberate road entrance; all full native/apron footprints fit inside. Road cell networks are connected. Wall cells and their four height corners are flat, with at least one clear cell to native cliff art. Every cliff cell has real corner-height relief, and native logs confirm the private HGT was loaded. GDI comparison INI/BIN/HGT files match across all three platforms. Combat may damage walls after the initial set is established.

Mammoth's six attackers are created outside the actual closer camera view, then approach and fire under native AI. GDI spawn-frame paired renders show zero changed pixels; Nod has a small diffuse headlight spill at the bottom edge, with no vehicle body or shadow in frame. Full decode, frame cadence, source camera phases, native behavior, black ends, audio peak and Desktop copy hashes pass. Production evidence is `output/vehicle-showcase/v07-production/` in the filming workspace; delivered files are versioned v07 in the existing Desktop film folder. The user responded “Better” to Harvester v07, then requested the suspension improvement below.

## Harvester suspension — 19 October v08 amendment

The Enhanced Harvester must have visible weight while driving: a sprung chassis above grounded running gear, with a damped response to road input, acceleration/braking and turns. It settles when stopped; avoid an idle jiggle. Keep the approved geometry, faction materials, claw/ram motion and native refinery dock rig. This is runtime animation, not a post-production camera effect or an asset redesign. Classic and N64 retain their native animation.

The runtime implementation takes contact points and the body pivot from the installed HARV wheel records, not an older model's guessed wheelbase. The render-only spring advances on native tick edges and interpolates at each real exposure sample; static hull, collector and lights share its body transform, while wheel/tyre parts keep their contact pose. The normal/material passes use the same posed geometry. Limit compression and reset history for limbo/teleports. The refinery dock clip takes over normally.

Native paired on/off renders show body movement above the wheels, continuous subframe poses and zero collector ram gap. The stopped native preview settles below 0.0001 cells/radians. An installed-runtime N64 frame matches v07 pixel-for-pixel. Current-main TD and separate RA app aliases are refreshed together, with hidden installed-runtime HUD, shroud/superweapon and RA selection/movement checks passing. The mesh/material files remain unchanged. New Enhanced Harvester filming is tracked separately under `output/vehicle-showcase/v08-production/`; preserve the accepted v07 film while that technical rerender completes.

## Factory movement and latest-profile amendment — 19 October v10

A factory exit rail must end at a native cell centre before normal path driving. The prior filming rail stopped at subcell (28.6875,29.0); the next order recentered it to the stock path, causing a 0.574-cell native jump. The opt-in straight rail now runs from the unchanged doorway spawn to centre (28.5,29.5), 32 evenly spaced records with matching heading 137. Actual per-tick measurements around the seven-second order show maximum 0.043-cell command-window movement; maximum throughout rollout is 0.065 cells. Normal launches without the filming flag match the prior engine's native positions exactly. Never conceal a native handoff error with interpolation.

Freeze the user's latest saved F5 CFG and compare every render dial against compiled defaults before capturing. The 21:34 profile supplies the current grass saturation, terrain brightness, shadows, contact light, fog, structures and vehicle brightness; preserve machine resolution/display preferences and hide stats in films. The latest bloom instruction explicitly forbids reducing overall bloom. Keep overall intensity at the user’s 0.92857, with threshold, radius, passes and clamp unchanged. The new explosion_bloom dial is 0.30 and affects only the bright-pass halo beneath a depth-tested mask of luminous particles classified at birth as explosion emitters. Smoke, muzzle flashes, headlights, materials, the source particle art and scene exposure retain their settings. Lingering explosion emitters keep their classification after the parent anim is gone. A gain of 1 bypasses isolation and retains the previous picture. These films use the current Enhanced particle effects path; a change solely to the legacy experimental Unity pass has no effect on them.

Recordings must use current canonical-main renderer/engine and current matching model, specular, grass and terrain files. Check actual file hashes, including the Dock installation: an up-to-date executable alongside old models is not current delivery. Refresh the required current-main assets and both TD/RA executables without altering the launcher or user profiles. Harvester retains the sprung chassis, outside turn lean and normal refinery rig. v10 replaces both films with current inputs; the undelivered partial v09 overall-bloom attempt is superseded and its logs/hashes/selected QA samples are retained. v07 delivered exports remain preserved. Production/QC is tracked in the filming workspace at output/vehicle-showcase/v10-production; final delivery is recorded only after capture and media checks.

The particle-only scope check uses the actually deployed Dock renderer. Matched frames with scene highlights and a native cannon muzzle are pixel-identical at explosion_bloom 1 and 0.30; an actual explosion is also pixel-identical when bloom is disabled. Ordinary bloom still changes the control scene when its own switch is changed. Three same-tick combat comparisons show reduced explosion halos while the full latest user profile retains its overall bloom value. Hidden TD HUD/fog/superweapon and separate RA input checks pass; Harvester’s paired sprung-body check retains its grounded running gear and zero collector ram gap.

For long shutter captures, use lossless PNG transport with bounded integration backlog. The private transport can wait after GPU readback while the reducer writes already captured exposures; waiting must never advance an engine tick, particle/RNG state, camera or input event. Check native clock/audio ratios after capture. Preserve the exact source/exposure hashes and final decode checks.


## Film storage lifecycle — 19 October 2026

The retention rule replaces historical instructions to retain every raw
frame/version. Keep delivered MP4s, art/music, code, native scenes, settings,
hash manifests, receipts and QA contact sheets; delete superseded raw PNGs.
Before pruning, resolve actual symlinks and script imports. Materialize every
active input in the current production, verify copies, then register only those
current frame roots in `storage-retention.json`. Do not traverse symlinks.

`tools/clean-showcase.py` removes frame/screenshot/exposure PNGs only inside
explicitly retired version folders. `--retire VERSION --apply` requires a
hash-verified Desktop delivery receipt and refuses a current dependency.
Delivery must invoke it immediately after its export copy hash passes. The
scheduled cleanup is a fallback for interrupted delivery cleanup, not a reason
to retain delivered frames. Current v12 has local fixture/helper/art/music,
old movement-control baseline, scene metadata and reused Classic/N64 frames;
it no longer imports frame sequences from v04-v11 or old subject versions.
The fully verified Harvester v10 export was visually reviewed and delivered;
its PNGs were then retired. Older metadata provenance can retain historical
paths without implying that those frame sequences still exist.

Completed disposable session temp folders use `.cnc3d-cleanup.json` receipts.
`tools/clean-session-temp.py` requires 72 idle hours, no live cwd/open-file/command
reference, and no Git checkout or authoring/project files. Release staging also
requires a published receipt. Unrecognized and active temp paths are kept.
Both helpers are included in `tools/clean-builds.sh`; daily follow-up runs the
same narrow helpers without invoking broad worktree cleanup.

## Muzzle adjustment — 19 October 2026

The design review rejected the first scoped reduction (.30 source/.15 light/.08 halo) as too
faint. The brighter middle setting is .60 source, .35 muzzle-only light spill,
.25 muzzle-only bloom; explosion-particle bloom remains .30. Ordinary scene
bloom stays at the user's .92857 and the full saved rendering profile remains
unchanged. Only actual GUNFIRE/MINIGUN/SAMFIRE particle ancestry and corresponding
local muzzle lights receive the additional muzzle controls; smoke, flame/chem,
other emitters and ordinary highlights retain their previous treatment.
Matched actual native-tick/camera shots were inspected; the stronger core is
visible without the former broad blown-out halo. Final Mammoth recapture and
installed-runtime verification are recorded separately from these QA previews.


### Verified final delivery and cleanup — 19 October 2026, 22:44

Mammoth v12: 2560×1440, 60 fps, 2400 frames / 40 s, SHA256
`7f814004eb54849f6549448e967bf12b1200651e76305670aa04bbd0afdc625d`.
Harvester v10: 2560×1440, 60 fps, 1740 frames / 29 s, SHA256
`e3871f59834d4819a4ece783b7ead74b273968a0c092197ea146f17acb58270b`.
Both exports are hash-verified in Desktop/OpenCNC3D Vehicle Films. Actual decoded
contact sheets and matched muzzle comparisons were inspected. Full decode,
constant native cadence, source yaw/shutter, black ends, audio, current F5
profile, correct native drive handoff, initial wall/road/elevation constraints
and offscreen births pass. Actual latest installed output is pixel-identical
to retained Classic/N64 and Harvester controls under matched draw history;
72 native/art/terrain inputs are unchanged. Both TD/RA installed aliases match
current-main builds, with hidden HUD/fog and separate RA input checks passing.

Historical superseded frame deletion reclaimed 428.05 decimal GB (398.65 GiB).
All 32 pre-existing final movie hashes remain unchanged. After the new Mammoth
export was copied and verified, delivery immediately deleted another 14.87 GiB
of its temporary raw/exposure/per-frame QA images. No raw film sequences or
active frame dependencies remain. Final media, contact sheets, scene/settings
files, recipes, scripts, immutable inputs, masks/art/music and receipts remain.
Future versions must capture fresh raw frames; do not assume a delivered
version still contains reusable sequences. Materialize needed inputs locally
before retiring a still-live source. Daily narrow cleanup runs at 09:00, and
completed disposable temp/staging folders require 72 idle hours and activity /
authoring / Git safety guards. The three old Oct15 temp staging paths were
already absent and are not included in our reclaimed-byte total.


## Bib freshness correction — 19 October 2026

The design review found pink/purple trails on the Enhanced building aprons and asked for the
approved RA3D treatment. The Dock install had BIBENH3 while the current canonical
baker and the separate latest-build folder had BIBENH4. Version 3 omits the ground
calibration and silently uses the older multiply fallback, which tints roads pink.
This is a stale delivery artifact, not a need to repaint the approved gravel.

Fresh source bake SHA256:
`632e9039774b6b287b83900aa4caa29260e68bea46007a70ff0978c04f634a4f`.
It is byte-identical to the latest-build pack, using the approved g3s art shared
with RA3D. All six apron RGBA payloads, footprints and colour gains match the old
pack exactly; only the missing calibration records and pack format are restored.
Matched actual game views show the road strips becoming neutral dirt/gravel.
The temperate probe's pink fraction falls from 32093/1033769 (3.10 percent) to
2872/1039430 (0.28 percent). Bare ground is pixel-identical; pixels away from bibs
are unchanged. Desert calibration was also checked. F5 values stay unchanged.
Evidence: filming workspace output/bib-fix-2026-10-19/review/.

A pair of identical captures can share a stale input. For future films, validate
asset freshness against a fresh canonical-source bake as well as matching capture
hashes. In particular, require a calibrated version-4 bib pack rather than merely
matching an older installed copy. Both Mac game/app builds now bake and stage this
required asset even in code-only mode. App staging rejects a pre-v4 pack before
replacing executables. Publication is atomic. The staging regression proves stale
pack rejection and delivery of a fresh bake to the TD/RA install. Native builds
also freeze their build-header identity so parallel game/app builds cannot mix
version stamps between translation units.

Both v13 films recapture all eight native sections, using local non-frame inputs
and the current installed assets; no retired sequences are restored. Preserve
camera/zoom, eased follow, actual suspension/aiming, amber wipes, persisted distinct
approved music and the .60/.35/.25 muzzle and .30 explosion bloom controls. Overall
bloom stays .92857. Required native scene/height/perimeter/action checks and actual
runtime asset hashes pass. Delivery status and final hashes are recorded after
media verification; delivery immediately retires raw sequences and per-frame QA.


### Verified bib-corrected final delivery — 19 October 2026

Mammoth v13 (40 s, 1440p60), SHA256
`0cade1ac213a880e8ba902deb85306d9a0989420f076940e962448ca7124eb4e`;
Harvester v13 (29 s, 1440p60), SHA256
`65913577c8eb8f65acf468fbf737f50d1d59e2f76cb3dc2164a5653850e5cc58`.
Both are hash-verified in Desktop/OpenCNC3D Vehicle Films. Actual decoded contacts
were inspected and show the corrected neutral dirt/gravel bibs. Full decode,
native cadence/yaw/shutter, black fades, audio and source action checks pass.
All eight sections were captured afresh; only bibenh.pack changed among 72
native/art/terrain inputs, and all live inputs still matched their freeze hashes
at capture end. The Dock and separate RA executable aliases are refreshed and
hidden runtime checks pass. Existing running games must relaunch to load the pack.

Delivery immediately removed 24.69 GiB of native/exposure source PNGs plus the
two exports' redundant individual QA frame dumps. No raw sequences or active frame
dependencies remain. Prior delivered exports are preserved. Contact sheets and
source/scene/settings/input evidence remain for review and reproducibility.
