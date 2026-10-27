# Enhanced terrain controls

## Latest shared visual preset —26 October2026

The latest user-saved RA3D picture, SHA256
`b0f67027ba5d9703dec4dc05a7d451fec9df9ff004ac6f3b5fcaa37a8486b0e0`,
now defines the shared Enhanced visual defaults for Dawn3D and RA3D. The dated
`game/enhanced_defaults_20261026.inc` follows the retained22/25October layers
before mode defaults are captured. Exactly22 visual values change: clear-terrain
saturation; water alignment, bump, highlight strength, reflection bearing,
refraction, natural shading and darkness; tree gain/shadow fill; grass gain,
green/tint, root fade/alpha, AO/tip and five grass-terrain colour values.
The reflection offset is explicitly **90°** independently of sun rotation.
This replaces the older zero-default contract below; zero still gives the
unrotated reflection direction.

No shader, native height/depth, wave/shore solver, mesh, terrain pack or artwork
changes. Twelve local fields remain untouched: `show_stats`, `vsync`,
`display_mode`, `res_w`, `res_h`, `ui_scale`, `swap_buttons`,
`right_drag_scroll`, `cash_tick`, `enabled`, `dos_classic` and `smooth_anim`.
Classic/N64 saved rows and migration markers are not imported, and no personal
CFG is shipped. The full saved input is retained only as independent local
verification authority, with Enhanced rows taking precedence over live copies.

`docs/verification/2026-10-26-shared-defaults/COMPILED-DEFAULTS-RESULTS.json`
records the independent compiled comparison: all519 visual keys match the
approved save within its five-decimal precision, exactly22 fields change,
all remaining default bytes and inactive mode rows stay exact, and old/new
complete loaded states are byte-identical with that same frozen preset.
Current-main native and Dock verification remain pending and require a final
receipt in the same directory. No gate threshold or historical fixture changes;
the earlier dated material below remains provenance.

## Shared25October visual preset

The approved shared save at SHA256
`1e5ddb3c9dbd99d743598f309c71abab80e5b19a5a7740593e2e443e2caf448c`
supersedes the22October visual tuning for both games. A dated84-assignment layer,
`game/enhanced_defaults_20261025.inc`, follows the retained22October base before
mode defaults are captured. Only the measured visual differences are imported;
the user's personal CFG, display resolution/mode, input, debug and selected
graphics mode are not imported into public defaults.

The layer includes12 water values from that save, together with its lighting,
terrain, effects/infantry brightness and other visual grades. It changes no shader,
geometry, wave/shore solver, source artwork or depth ownership. Reflection rotation
continues to inherit zero when the older preset omits it. The original base and
dated historical measurements below remain available as provenance.

Independent compiled TD/RA checks pass all visual-key comparisons to the exact
approved saved input, within its five-decimal serialization precision. They prove
exact84-field coverage, preservation of every other default-state byte/mode row,
and byte-identical complete loaded states before/after this amendment with the
approved full preset. Public fresh-default versus approved-preset native scenes
and final release delivery remain separate pending checks. No gate threshold or
historical fixture is changed by this defaults amendment.

## Independent water reflection bearing —25 October2026

The Water group exposes `water_reflection_yaw` as **Reflection rotation**,
from-180to180degrees clockwise on the map. It is an offset from the current sun
bearing and does not change the sun's azimuth or elevation. Its default is zero,
including when loading an older preset. The shared F5 schema saves and loads the
offset in both Dawn3D and RA3D.

Only the surface shader's specular half-vector uses the rotated direction.
`uSunDir` and its diffuse/painted-water relief remain unchanged; the independent
`uReflectionDir` preserves the light's elevation and length. Zero copies the
previous components exactly before upload. The same shader/upload applies to
TD unpainted water and TD/RA painted water. Shore shaders, the virtual bed,
absorption, native height, normals, waves and shared lighting are untouched.
This control does not reintroduce the retired mirror-scene reflection pass.

`tools/tests/test_water_reflection_rotation.py` compiles the actual state and
direction helper for exact neutral components, rotation direction, elevation,
TD/RA defaults, legacy inheritance, label fit, panel visibility and preset
roundtrip. Native validation must separately check zero/rotated/restored water,
unchanged terrain/sun and each frontend's saved preset. Current-main combined
build and verified Dock delivery remain pending.

The hidden actual-production-GLSL probe passes the unpainted TD, painted TD and
painted RA variants. It compares the old half-vector against the new zero value
and restores zero after90degrees: both comparisons have zero pixel error. The
rotation changes32,704/32,758/32,758of32,768pixels respectively, with exact alpha;
specular-disabled body pixels remain exact, and every active shared-sun uniform
keeps its original vector. Constant fixture textures isolate shader behavior;
this is separate from full-game scene and installed frontend proof. The local
`water-reflection/NATIVE-RESULTS.json` binds source, executable and SDL closure.

## Pale-slope headroom under unified daylight — 25 October 2026

Red Alert's pale-ground highlight rolloff now reads the final unified sky and
direct-sun factor. Previously that factor replaced the already bounded legacy
slope factor, bypassing the rolloff while unified daylight was enabled. The
existing knee and white-input limit now bound the unified boost before it
multiplies the painted ground. The legacy branch, darker slopes and disabled
rolloff retain their prior arithmetic. Saved lighting and colour values are
unchanged. Source-extracted CPU controls reproduce the overwritten bound and
check legacy equality, disabled-rolloff equality, retained hue and highlight
headroom; native slope captures remain a separate runtime check.

## Submerged banks and continuous Snow relief — 22 October 2026

Enhanced water keeps the existing body plane and low-bank easing. Its opaque
bed closes the terrain cut and slopes into a flat basin three cells below water
(one and a half for narrow river channels). Shared sixteenth-cell bank vertices
carry interpolated surface normals, so exposed closures no longer receive a
separate flat normal for each triangle. The short bank lip eases over0.18cells.

Above-water banks continue the actual adjacent terrain atlas, sampled through a
per-map-cell UV lookup and the nearest dry foot. Cliff banks continue that local
rock into the shallow water; ordinary banks transition to the native BOTTOM dirt
material with depth. The source material extends inward as the bank drops,
without repeating another complete cliff silhouette. The nearest dry colour is
the fallback when the atlas has no valid source coverage.

Swash runs up only onto banks within0.035cells of the water, fading out by0.09.
Raised banks instead receive a narrow cellular foam rim at their physical water
contact, using bank depth and slope. They do not receive beach run-up on top.
Swash opacity still controls this rim; disabling both swash and foam removes it.
The shared clock, Hold timing and river swash exclusion remain unchanged.

Narrow connected lakes require authored river/ford/falls/bridge terrain before
the width rule can classify them as rivers. Lakefront Clash (TD SCM06EA) has two
lakes; both now retain ocean pan. Its right lake was previously classified as a
river solely because its maximum radius was below the configured river width.

RA Enhanced WC/RC cliff materials remove blue surf and its attached thin white
fringe only near the immutable native water cut. Repair works on each complete
template and variant, with dry source RGB as the boundary condition. Matching
join-strip RGB is repaired where the source edge was repaired; all blend alpha
is preserved. Snow and Temperate banks are audited across84template/variant
groups (352icons). The original atlas and strips remain available when water FX
are off and in Classic rendering; source terrain packs are not rewritten.

Absorption uses vertical water depth: texture contrast drops continuously and
is exactly zero beyond2.7cells. The post normal pass writes the actual visible
water surface depth, so lighting and fog reconstruct the plane rather than the
deep floor. Opaque projected terrain and later object normals keep priority.
Authored waterfalls and unsupported border water retain their native floor.
Classic water, the body solver, shoreward chart, swash clock, Hold, opacity and
the exact13:51defaults remain unchanged. Installed user CFG files are preserved.

Snow relief now feathers in the shared world lattice before the authoritative
water cut. Its zero-height neighbours carry matching edge normals; its base
normal interpolation uses the native SW–NE triangles without renormalizing the
flat supports. This removes template rectangles around Snow rock feet while
preserving the source albedo and saved feature mattes. Building pads remain
flat and wet supports retain zero relief. No model, HUD or F5 control changes.

Validation targets include SCM01EA, Coastal Influence, the user's first RA
skirmish map, at native and low cameras. Isolated production-GLSL absorption,
actual bed supports, water depth/material ownership, Snow boundary curvature
and both installed game regressions provide separate technical evidence.
`gate-water-bank-material.py` renders the production bank GLSL against an
independent interpolated-normal field and two distinct local atlas cells.
`gate-water-bank-contact.py` renders all three shoreline variants across32cycle
samples: low beach, raised beach, raised water contact and disabled foam.
`gate-submerged-terrain.py` uses the optional authoritative `C3DPLN1` dump to
intersect camera rays with the per-cell surface before selecting expected wet
pixels. Bed seen beyond a map edge is not evidence of a water hole. Above-water
bank occlusion is evaluated from bed geometry. Foreground sprites and native
cliffs are confirmed by an above-water depth point on dry or partially covered native coast, or the
authoritative native relief. Selection never uses RGB; missing material, including
a zero normal buffer, and incorrect surface depth remain failures.

## Foam build-up and backwash — 22 October 2026, 13:51 save

The foam material now follows the reversible swash lip rather than the ocean's
always-forward accumulated travel. It builds a dense, granular bubble rim as the
front reaches maximum run-up. The rim broadens during early backwash; a separate
shore-anchored residue spreads towards the sea and breaks apart as the water
retreats. Both layers fade before the next approach, so their periodic reset is
invisible. The held phase still pins peak run-up for exactly the requested Hold;
zero Hold reverses immediately. Opacity scales film and both foam layers together.
The shore-only band, projected contact coverage and ordinary-ocean fade remain.
The residue begins only after the incoming lip reaches maximum run-up; it cannot
appear ahead of the water during approach. No panel rows, label geometry or artwork change.

The exact latest F5 save is `docs/tuning/opencnc3d-fx-2026-10-22-1351.cfg`, SHA256
`a5945491a3934f0ab794c1271651770a8cf0d2592de78251dafdd4376a142805`.
The canonical generator updates the shared Enhanced include for TD and RA:
Hold0sec, opacity0.48214, reach0, blend3.31518cells, legacy foam width0.67857,
water bump0.41518, specular0.53571 and refraction0.02196. Earlier snapshots and
measurements below are dated history. Display preferences, saved Classic/N64
rows and existing installation CFGs remain user-local.

The source-extracted lifecycle gate and negative controls reject constant-width,
unfading, forward-only and sliding-residue variants. An isolated hidden GPU
transect pins phase and uses black ground/water plus white foam input to measure
the production material envelope without lighting/noise confounds. Coverage grows
while the foam spreads, then drains; that growth must not be mistaken for a fade
failure. Full-game hidden GDI1/Allied1 loops check the real procedural bubbles.
Source checks, isolated probes, visible game captures and clean-main Dock delivery
are separate evidence; their results do not imply design-review artistic acceptance.


## Adjustable swash hold and opacity — 22 October 2026

F5 > Water adds **swash hold (sec)** (`water_swash_hold`, 0–10 seconds,
default 0) and **swash opacity** (`water_swash_opacity`, 0–1, default 1).
Hold inserts seconds at maximum inland run-up, keeping foam age and shoreward
chart presence at their shared peak. Approach and retreat durations retain the
saved crest-speed timing. Thus the 10:24 baseline has a roughly 10.69-second
cycle plus the selected hold. Crest speed zero still freezes the cycle.

The 13:04 user test supersedes the original quintic envelope: its nearly flat
endpoints hid 2–3 seconds of apparent holding even at zero. Quadratic run-up
and backwash now reverse promptly at zero hold; only the chosen interval parks
the crest. The cycle integrates these envelopes analytically, including the
peak interval. Its mean-normalized forward travel remains continuous across
cycle boundaries; it never multiplies elapsed time by changing instantaneous
speed. Both water and beach film use the same held clock, and retreat still
fades the aligned chart into the ordinary ocean. Opacity scales the combined
film/foam alpha after deriving its unpremultiplied colour, avoiding darkening
or changing foam colour. It affects both the exact beach mesh and incoming
plane foam, independently of legacy foam, beach-fade and ocean controls.
The swash crest starts at signed shore distance zero and travels inland. Its
shore band ends through a 0.04-cell antialias feather, removing the previous
offshore foam row while retaining foam over the projected lower shore lip.

The exact saved 10:24 CFG and its 523 supported values remain unchanged.
Existing presets inherit zero hold/full opacity until edited; ordinary
Save/Load/reset stores the two new plain keys. Source/state, hidden GPU hold
and opacity, F5 capture and clean-main Dock checks have distinct receipts.
See the UI notes for fitting and verification scope.

The current default is **unified daylight**, shared by Tiberian Dawn and Red Alert.
F5 > Enhanced > Sun and Shadows has unified daylight, sun energy, sky fill,
terrain sky fill, sun bearing/height and the existing shadow quality controls.
The latest 22 October 10:24 F5 save supplies the shared Enhanced visual defaults
for both games. The exact snapshot is
`docs/tuning/opencnc3d-fx-2026-10-22-1024.cfg` and the generated include is
`game/enhanced_defaults_20261022.inc`. Enhanced mode rows override live copies;
Classic/N64 rows and machine/display state are excluded. The compiled defaults
retain the supported saved visual/mode values in TD and RA. Earlier dated
snapshots remain preserved. The new save keeps shore reach0, blend2.53437,
flow0.25, flow rate0.02, crest speed0.19643 and beach fade width0.93036.
Shadows remove direct sunlight once; ambient sky fill survives them. Zero sun
and zero sky darken non-emissive surfaces. Dynamic lights and emissions remain
independent. Turning off sun shadows does not restore cartridge shadow plates.

F5 > Terrain has geometric rock relief and its maximum height (default 0.32
world cells, range 0–0.6), brightness and contrast. The saved physical masks
produce one bounded mesh used by colour, normals, depth, reflections, placement,
picking and the terrain sun map. The native map heightfield and building pads
remain authoritative. Missing packs or refused GPU buffers retain native terrain.
Fine-grid and geometry-memory caps bound large-map allocations.

Terrain shadow strength now controls actual terrain cast-shadow opacity.
There are independent Desert, Sand and Snow groups; Snow covers both games.
Other theaters use the generic Terrain value. Old CFG files inherit their final
saved generic value for missing new keys, regardless of key order. Explicit
values stay independent through Save/Load. Object shadows retain their setting.
Desert brightness/contrast/saturation and Snow brightness/contrast/saturation
retain their existing controls. All values use the normal saved CFG schema.

Enhanced physical cliff/boulder/shore albedos no longer contain the broad fixed
cast-shadow skirts; saved common-ground shade is neutral. The source deltas and
explicit black player-colour masks are in
`art/remaster/textures/unified_daylight_v01/`. Sand derives matching Enhanced
Desert template detail with its measured native pale palette. Native and DOS
Classic sources are unchanged. Fine source crevices and the approximate source-led
relief remain asset fidelity limits; this is not a claim of full PBR replacement
of every historical sprite and translucent effect.

Brightness/contrast affect ground before lighting. Visibility is carried by the
same smooth shroud field; actual water coverage excludes water from ground grade.
The older painted-shade correction and shade-floor/one-sun controls remain only
for the legacy daylight path. They must not be reintroduced as modern shadows.

Run `python3 tools/gate-unified-daylight.py --playable <installation> --cfg <cfg>
--output <private-directory> --native-fixtures` for hidden runtime checks.
The gate never opens/focuses a visible window and uses private settings/saves.
It covers both games, eight theater paths, opposing sun, independent sun/sky,
zero-energy material classes, physical cliff cells, F5 reachability, timing,
Native/Classic isolation and preservation of saved user CFGs. The older
`gate-terrain-controls.py` describes legacy shade-softening semantics.

See the engineering reports for the diagnosis, iterations and
runtime evidence. Deliver from current main to the existing Dock installation,
including both frontends and all required masks/surface packs; keep launchers,
saves, user tuning and unrelated approved assets intact.


## Shadow detail and Temperate surfaces — 20 October 2026

Sun and Shadows adds **shadow detail floor** (`shadow_black_limit`, 0–0.15,
default 0.035). It softens the colour grade's black clipping with a smooth toe;
it is a display response, not an extra light or a minimum brightness painted into
albedo. Sky fill scales it. Zero incoming radiance remains black, including
unexplored fog and a zero-sun/zero-sky scene. Set 0 for the previous hard clipping.
Source foliage AO settings remain separate from this display curve.

The separate **object shadow fill** (`object_shadow_fill`, 0–1, default 0.30)
raises sun self-shadowing and ambient occlusion on structures and trees. It uses
the structure class mask even when the other Structures dials are neutral. It does
not weaken the same object's cast shadow on terrain. The 3D tree material shader
uses it for canopy and bark occlusion as well.

Temperate cliffs/boulders use the same explicit mineral mask with a darker,
neutral-grey albedo grade. Fine grit and fissures remain; grass stays unchanged.
The authored RGB/height/normal cliff packs are shared with RA3D; its pack loader
also checks the parent `playable/` directory when launched from `playable/ra3d`,
so the separate frontend resolves the same OpenCNC3D cliffs. Both TD and RA
Temperate banks receive the neutral surface grade.

The normal Enhanced RA bank now shares all 42 matching TD cliff templates and
variants by exact name, variant, occupied cells and dimensions. The compiler
remaps only template IDs: colour, physical height, ground/mineral masks and the
complete external feather strips come from the same TD source. RA's former
cliff strips are removed before the shared strips are written. The legacy RGB
pack lookup alone does not provide this native-bank contract.

Desert and Sand ground blending fades out over the physical foot of raised
rock. A neighbouring clear cell cannot spread sand up a cliff face merely
because it crosses a cell boundary. Ground-level transitions retain the smooth
authored matte. The sandstone's material chroma is reduced by 35 percent while
retaining its luminance and grain; saved theater tint/RGB values remain intact.

The complete original Desert cliff footprint is restored for all 56 S-template
versions. A previous colour-seeded graph cut wrongly discarded shaded lower rock
faces. The explicit original outline now determines material and physical relief;
all original rock texels remain stone and the 3.5-texel sand feather lies outside.
Broad source illumination is divided out independently of coverage. Run
`tools/gate-desert-footprint.py --playable <installation>` to verify the saved
outline and the installed Desert/Sand ground and physical surface packs.

`python3 tools/gate-shared-cliffs.py --playable <installation>` verifies every
native TD/RA cliff cell, height tile, ground mask and external feather strip,
and rejects residual warm mineral streaks in the explicit rock interior.
Normal updates of an existing RA module now also refresh its native terrain
packs and RA infantry exports; these must not be confined to first-time module
installation. Existing non-cliff seam kind/reserved bytes remain intact.

Shore wet/dry soil is material colour, not cliff illumination. The compiler keeps
the continuous accepted shoreline source and its complete external blend strips.
The previous cliff normalization changed some cells independently and threw away
external margins; that caused the rectangular seams. Water cuts, native topology
and lighting geometry remain shared with the sun/depth passes.

Unified daylight now uses the approved authored gravel bib composition in both
games, as the RA snow path already did. The same g3s/version 4 pack is required;
this replaces TD's old substrate multiply, which hid the gravel detail and tinted
it with the ground. Strength and the authored feathered alpha control the blend.
The legacy daylight path retains its calibrated cover/multiply controls.


## Independent terrain colours

F5 > Enhanced has Grass, Desert, Sand and Snow groups. Each adds warm/cool and
green/magenta tint (−1..1, neutral 0), plus red, green and blue balance (0..2,
neutral 1). Keys are `grass_terrain_*`, `desert_terrain_*`, `sand_terrain_*` and
`snow_terrain_*`, ending in`warmth`, `tint`, `red`, `green`, `blue`. These 20 sliders
are independent of the existing 3D grass blade tint, and they use normal Save/Load.
Snow colours also cover Winter. RA Interior keeps its own source palette.

The controls grade ground and cliffs before sunlight, shadows and lights. Source
material classes exclude objects; the coast field excludes open water and fades
the grade smoothly through 0.75 cell at the inland wash in both games. This removes
the remaining brown/pale shoreline contour; only Desert had that fade previously.
The relief colour pass also retains its parent's feathered alpha blend.

All new colours default neutral. The latest RA3D CFG supplies the shared compiled
Enhanced defaults, while local display values and existing per-mode choices remain
user-local. New shadow controls keep their own compiled baseline unless present in
the CFG.

Hidden checks:
`python3 tools/gate-terrain-colours.py --playable <install> --cfg <private-cfg>
--output <private-output>` exercises every colour control, inactive biomes, exact
object isolation, F5 and all 20 saved values over eight paths. Editor comparisons
exclude the animated palette icon and use actual painted cliff fixtures.
`tools/gate-shadow-detail.py` accepts the same arguments and checks the display
toe, zero radiance, F5, saved roundtrip and native modes in both games.
See the engineering reports for verified delivery.


## Earlier Dock delivery — before the water-plane correction

Clean pushed-main build `0.8.7+9a11a66` is installed in both existing Dock
locations, including TD and separate RA executables. It includes the bank/crown
coverage repairs, paired stone normals/specular, native mineral sampling and the
latest saved Enhanced defaults. Approved shore grass-only mattes are preserved;
physical ground overlays are clipped by the actual saved coast alpha.
Eight daylight paths, native/Classic isolation, zero energy, F5, six RA HUD and
four RA first-frame cases pass hidden installed checks. All8,520delivered hashes
match, and70settings/save/bundle files retain their bytes and modes.
The earlier5,204-file dirty-source receipt is superseded and retained as dated evidence.
The current receipt and combined-source hashes are in
`docs/verification/2026-10-21-shoreward-water/` (the earlier coverage receipt remains historical).
The20October world-detail and terrain-polish receipts remain dated history.

The shared animated run-up/backwash investigation is saved in
`art/studies/shoreline_swash_v08/`. Its four actual engine loops use a separate
prototype renderer. The animated swash is not installed or enabled as a default;
the study does not modify the user's saved water settings.
Version08 retains the physical shore mesh and cellular foam detail of version07,
then aligns the water, ripples and prototype foam motion with the local coast.


## Earlier steady shoreward motion — superseded by the pulse below

Enhanced water uses the shared continuous coast field in both games. Offshore
water retains the saved current bearing. Across a 1.5–3.5-cell approach band
(derived from the existing foam-width control), colour, both ripple normal layers,
broad waves and shore film blend into coastal coordinates. The inner0.35cell of
sea excludes the offshore motion; river banks use a0.10–0.65-cell band and keep
the channel's downstream flow where a bank normal is undefined.

Coastal texture V is `-signedDistance - uniformSpeed*time`. Its U uses two spatially
blended world-axis charts, weighted to suppress the singular projection. Every
chart advances toward smaller water distance regardless of texture orientation
or offshore bearing; periodic texture sampling supplies the loop. Normals decode
through the same chart gradients. A spatially varying `direction*time` would
accumulate shear, and temporal phase resets can make correlated texture features
oscillate, so neither is used for the coastal branch. Field-gradient and coastal
texture reads are skipped outside the approach band. Existing F5 values/defaults
are unchanged; zero pan and flow still intentionally stop texture motion.

Environment-only `OPENCNC3D_COAST_FLOW_AUDIT` modes expose the ungraded distance/
coastal weight/river field(1), colour UV markers(2), ripple layers(3/4), or broad
wave UVs(5). `OPENCNC3D_COAST_FLOW_ROTATION` rotates diagnostic marker artwork in
degrees before sampling. These are test overrides, never saved UI controls. Use
raw scene field captures, fixed cameras, camera shake and battle lights off, and
exclude occluded/shore-film/pyramid-border pixels when measuring image motion.
The animated swash/foam artwork remains an isolated review study.


## Water plane, shoreline coverage and pulsed swash — 21 October 2026

Enhanced uses one horizontal surface per connected water body. Source wet masks
connect across edges and diagonals; opaque TD bridge footprints join existing
wet banks without turning the deck into water. Authored FALLS1/2 retain their
native drop and remain barriers. Body elevation comes from modal native pure-water
corners inside the playable map rectangle, with a weighted native fallback. The
outer pyramid border cannot vote a playable body to a different elevation. Dry
land, bridge support and object placement retain their native support; actual
wet support uses the same exact body plane as the water draw.

The water draw extends underneath partial-alpha banks, with its opaque bed kept
0.04 cell below the surface. Fully opaque land writes depth first; feathered
shore colour blends without writing false raised depth. Colour, physical normals
and coast masks share the accepted atlas alpha. Snow whole-template water seeds,
occupied-icon context and wet-side RGB extension remove painted static water and
opaque tile fringes while retaining actual rock, snow and native palette pixels.
Physical relief tapers to zero at wet source pixels. Cache keys include terrain,
hole, pad, relief, map rectangle, scene and water-enabled generations as relevant.

The normal pass marks visible water as class0.28 with a planar upward normal.
A per-frame screen-space mask redraws the production opaque terrain prepass at
the land's actual projected height, with the same alpha>0.999 threshold and
read-only scene depth. This protects dry cliffs below a nearby river plane as
well as equal-height land. Sampling coast alpha at the lower plane cannot replace
this ownership test: elevated, partially transparent banks project elsewhere.
The mask has a separate colour attachment and detaches borrowed depth before
the water shader samples it, avoiding framebuffer feedback. Strict depth testing
and disabled polygon offset preserve other foreground occluders. Water
receives sky fill and direct sun visibility, excludes ground colour grading and
opaque surface ambient occlusion, and preserves real terrain/object occluders.
Blended shoreline pixels cannot inherit the old opaque-object black outline.
`tools/gate-water-normal-ownership.py` checks actual mask/depth/normal readbacks
against an opaque-terrain reference and independently sampled wet/dry source
support. The SCB01 dry-cliff regression rejects the earlier helper's 186,586
false-water pixels while requiring the fixed 400,000-pixel dry region to retain
its original normals and material class exactly. A freshly redrawn water-disabled
reference allows six float32 depth units of roundoff (measured maximum five,
less than 0.000015 world-cell height); earlier stale-frame comparisons are not
independent reference proof.

F5 > Water Shader adds independent **shoreline swash** (`water_swash`). Enhanced
defaults on; Classic/N64 default off. Existing presets inherit that new control;
explicit opt-outs survive Save/Load. Legacy `water_foam 0` and every other saved
value remain unchanged. With swash enabled its cellular bubble foam replaces the
legacy smooth rim. Physical shore film advances and retreats over a tide-dependent
low-bank region; rock relief and high cliffs stay out of the film.

Water colour, both ripple layers, broad waves and shore film use one phase clock.
A six-second cycle at crest speed0.35 accelerates with beach run-up, peaks
at phase0.28 and gradually slows during retreat. Speed ranges from0.20 to1.8667
times the mean, with no stopped interval. The exact integral of the quintic surge
keeps travel forward and continuous through the loop; multiplying elapsed time
by the changing speed would instead cause reversals and jumps. Crest speed0
holds the beach cycle and retains steady water drift.

F5 **shore reach (cells)** (`water_align_length`,0–8, default0) is the fully
aligned distance from shore. **shore blend (cells)** (`water_align_blend`,0.05–8,
default2.53437) is the additional transition back to the offshore current. Their sum
is the outer limit: the latest saved default is2.53437cells, and the maximum is16cells, within
the precise field range. This fades sampled colours and normals together without
stretching ocean UVs. Both controls save/load normally; older CFGs inherit the
defaults and existing saved values remain unchanged.
As the swash settles, the same surge value fades aligned colour, both ripple
normal octaves and broad reflection slopes back to their ordinary ocean samples.
At zero surge the ocean chart is exact; the next run-up smoothly restores shore
alignment. A frozen crest clock or zero flow/pan also removes the aligned chart.
The separate beach foam remains on its physical swash cycle. This is a temporal
sample crossfade; it does not rotate or deform UV coordinates.

River bank steering retains its0.10–0.65cell band. Continuous dual chart weights
avoid branch cuts where opposing shore normals cancel.

The actual GPU distance field is RGBA16 over ±16cells (step32/65535cell). Flow and
coverage bytes retain exact byte×257 promotion. The legacy range4 diagnostic stays
compatible. `OPENCNC3D_WATER_FIELD16_DUMP` records actual GPU readback; audit mode6
shows phase, run-up and positive motion; audit mode7 shows the actual speed gain
divided by2. Audit mode8 records temporal presence, spatial band and their combined influence. See the versioned verification helpers and
receipt in `docs/verification/2026-10-21-water-plane-and-swash/`. Source contracts,
GPU checks, visual review and installed Dock delivery are separate evidence.

Desert FORD1/BRIDGE3/BRIDGE4 now use complete saved rock-bank regions, shared cliff
texel density and UV-paired height/normal/specular data. Native crossing tracks,
bridge masonry and accepted coast alpha remain exact. The reproducible source
and53-cell gate are in `desert_crossings_v05/` plus the reversible `desert_crossings_v06/` approach
repair and
`tools/gate-desert-crossings.py`. Destroyed bridge IDs170/172 still use native
broken-deck fallback art; no new destroyed-bridge model or artwork is implied.


## Runtime mineral staging order

`stage_desert_details.py` refreshes raw theater scatter packs as part of coupled
scenery delivery. The mineral normal/specular stage must run after that refresh.
The app-alias stager now reapplies mineral maps last when the installation already
contains mineral data; fresh full builds retain their existing final material
stage. `tools/gate-installed-stone-materials.py` verifies actual deployed packs,
paired images/masks, physical keys and preserved scatter geometry against the
active cache, with an optional mirrored-install check. Validating only authoring
cache maps cannot detect a later raw-pack overwrite. The regression includes a
negative raw-scatter reset and preserves exact source albedo/UV/geometry.

The small Temperate coast gate retains its former green-hue count for diagnosis,
but evaluates actual water material and depth ownership. Current natural-colour
water can satisfy that old hue threshold: the diagnosed4354green pixels were all
water class0.28 in an exactly matching depth/normal capture. The older steady-flow
renderer fails the new ownership gate (25,800nonwater pixels), so the regression
still rejects the fault without treating accepted water tint as grass.


## Projected shoreline contact and swash approach — 22 October 2026

The raised beach and the sea have different projected edges. The previous late
surface first protected actual opaque terrain, then cut water a second time at
the flat map-space signed-distance boundary. That left a strip of static painted
water below the raised beach. The normal pass already classified this strip as
planar water. Coverage now follows the projected opaque-terrain ownership mask,
with a short 0.08-cell feather limited to 12 screen pixels. Colour sampling uses
linear filtering only during those passes; exact pixel-centre ownership and the
normal/depth mask retain their existing nearest filtering.

Swash uses two supports with the same clock, field, crest and cellular texture:
the exact native beach mesh for wet sand, and the existing flat water body for
approaching foam. The beach pass no longer snaps a subset of its lattice vertices
to sea level, which bent mixed triangles below the ground and exposed hard joins.
Its coverage follows the actual ground alpha; the fallback without a painted base
uses the source wet mask. The water pass carries foam without a second sand wash
and rejects projected opaque land. Crests begin 0.40 cell offshore, cross the
contact edge and reach the saved inland span. Both ends of the envelope fade
before the geometry/shader cull, avoiding a hard line at the previous cutoff.
Flow pacing, reach/blend sliders, swash timing and all saved CFG values are unchanged.
Existing coast-flow debug views isolate their water data by suppressing this new
foam-only plane; production swash checks use ordinary final-colour captures.

The controlled GDI1 candidate comparison selected the reported contact strip by
actual GPU water material, projected source field and opaque-bank mask, without
hue classification. All 1,142 eligible strip pixels move and receive foam during
the 91-frame cycle (the previous build moved 69.7%). All 48,963 sampled approaching
water pixels receive foam. The matched no-shore control changes zero offshore
or far-land pixels. `tools/analyze-water-contact.py` records and replays this
measure; actual installed repeats and delivery receipts accompany the local
verification archive. Allied1, Desert and RA Temperate candidate captures also
pass. These are technical checks, not universal artistic approval.

## Coastal swash scope and Desert contact repairs — 22 October 2026

Beach swash and its pulsed shoreward water chart now exclude rivers. The saved
width classification is combined with native river, ford, falls and bridge
identities, including channels attached to a wider ocean. Dry bank vertices
inherit the nearest wet sample's class; both shore passes and the water material
use the same interpolated permission. Ordinary river flow, water geometry and the
shared phase clock are unchanged. The F5 swash help states that rivers are
excluded. No controls, ranges or saved defaults change; the exact 13:51 CFG remains
the Enhanced factory snapshot for both games.

Desert P01 and P03 now use explicit rock keeper mattes with an eight-source-pixel
soil feather. The original paintings, rock faces, relief and native footprints
are preserved. Separately painted Desert road exits share existing D10 earth
detail over twelve source pixels at external template ports. Internal road art
and all coverage/geometry remain exact. Saved sources, hashes, all-black paired
player masks and reproducible exporters live in
`art/remaster/textures/unified_daylight_v01/desert_feature_edges_v01/` and
`desert_road_exits_v01/`. Source tests exercise actual permission sampling, bank
extension, conserved rock pixels and matching road-port overlap texels. Hidden
candidate and installed comparisons, shader checks and delivery receipts are
recorded in the dated local verification archive. Technical validation does not
confer general artistic acceptance.

### Nod 1 road-rock identification follow-up

The map confirmation identifies the orange two-rock cluster as a detached
painted feature in D29 (template121, icon2), rather than P03. The dedicated
`desert_feature_edges_v02` matte feathers D29's two detached rock/soil components
with explicit keeper polygons. Its road component, original painting and geometry
remain exact. The earlier P01/P03 correction remains independently versioned.
`desert_road_exits_v02` restricts road-exit material to each template's connected
road body, preserving detached rock artwork even when it touches a cell edge.
Nod1 captures cover the actual D29/D20/D22 location and its reported seam. This
follow-up changes Enhanced assets and compilation only; water shaders, controls,
saved defaults and timing are unchanged from the preceding verified build.

## Effects and 3D infantry grades — 24 October 2026

F5 > Enhanced exposes two independent colour grades under Effects and Soldiers.
`efx_` controls affect effects; `inf_` controls affect 3D infantry. Brightness,
contrast, gamma, saturation and RGB balance use neutral1; exposure, warm/cool and
green/magenta use neutral0. Exposure ranges−3..+3EV in linear-light stops.
Contrast ranges0.25..3 and fixes black/white; gamma0.4..2.5 adjusts midtones.
Saturation and RGB balance range0..2; colour balance ranges−1..+1. Existing
particle brightness keeps0.25..2; soldier brightness ranges0..2.

Keys in each group: `bright`, `contrast`, `exposure`, `gamma`, `sat`, `warmth`,
`tint`, `red`, `green`, `blue`, prefixed `efx_` or `inf_`. The existing
`efx_bright`, `efx_contrast`, `efx_sat` stay compatible with earlier presets.
Additions inherit neutral defaults from old presets and persist through ordinary
F5 Save/Load. Scene exposure/colour and other object groups remain separate.

The current soldiers' `m3inf_sun`, `m3inf_fill`, `m3inf_paint`, `m3inf_shadow`
and `m3inf_runrate` rows now live in Soldiers, reachable with experimental terrain
off. Their keys, ranges and previous values remain. The grade operates after
model lighting and before the existing display-space shroud multiplier; it does
not brighten hidden infantry. Non-soldier models, normal/depth targets and their
shadows retain their paths.

Effects grading retains source alpha and the neutral bypass. Unity effects grade
only their layer's radiance, leaving scene transmittance/refraction separate;
positive exposure/brightness retain intensity through the intermediate linear
composite. A uniform zero-preserving transform avoids a hard branch at faint
smoke edges. Native effect meshes mirror their texture combiner and only leave
batching when a grade differs from neutral. Bloom radiance comes from the graded
scene; its explosion/muzzle classification mask stays ungraded. Terrain/water/depth
settings remain unchanged.

RA Allied uniforms use source light blue(sRGB104,116,160) and the current
material's explicit ORM-alpha weighted linear albedo reference. This replaces a
single stale reference for the Enhanced Allied blue material. The user's
Soviet1 correction also maps campaign infantry in blue slot6 to Allied blue1;
multiplayer/skirmish owner colours keep their chosen source remaps. E6 keeps
its uncoloured uniform; other cloth remaps retain their calibration. No hue classification or new asset bake is used.
RA owner paint uses its source remap palette rather than TD house colours. The
Codex and pose-sheet fixtures select actual RA remaps as well.

`tools/gate-object-grades.cpp` checks both modules' visible schema, label fitting,
neutral defaults and modified-value roundtrip. The compiled production helpers in
`tools/tests/test_ra_infantry_colour.py` check remaps, empty masks and differing
atlas brightness. Actual hidden GPU/F5, installed frontends/HUD/fog/startup and
preservation proof belong to the dated delivery receipt, separate from CPU checks.

### Desert impact decal blending — 24 October 2026

Enhanced Desert craters retain their original RGB and irregular support. Their
body now mixes at up to60% coverage with a gradual inward30/144-cell edge fade;
soot uses35% of its original density around the existing neutral multiply. This
keeps the sand grain visible through the depression instead of layering an
opaque black bed over an already dark multiply. No new pixels extend the mark.
Tracks, other theaters, Classic/N64 and water retain their current rendering.
`tools/gate-desert-decal-blend.cpp` checks the actual delivered pack as well as
independent RGB/support/ground-grain fixtures. Hidden old/new/ground-only shots
cover all six crater types and scorch marks; the dated delivery retains proof.
