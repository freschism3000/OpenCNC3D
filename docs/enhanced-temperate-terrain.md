# Enhanced Temperate terrain

Enhanced uses three photographic ground materials on original CLEAR1 map cells:
CLEAR1 has the most grass, CLEAR3 is intermediate, and CLEAR2 has the most neutral
sand and gravel. Classic and N64 retain their existing terrain paths.

`game/clearblend_TEMPERAT.pack` stores eight unchanged 576-square source images
(three CLEAR1, two CLEAR2, three CLEAR3), at 144 texels per cell. Their explicit
player-colour masks are black: terrain is intentionally not recolourable. The
versioned source images and paired masks are in
`art/remaster/textures/clear_variations/2026-10-17/revision_v04/sources/`.

The renderer chooses material families in irregular world-space clumps. Within each
family, stochastic sites choose layouts, quarter-turns and sample offsets, with
smooth joins and explicit texture gradients. Site spacing is smaller than the
source images' internal two-cell motif, so a single site cannot expose a complete
repeat. Source UV scale, filtering and source pixels remain unchanged. The terrain
correction was explicitly approved.

Grass-bearing cliffs, boulders, roads, shores, rivers, bridges and patches share the
same world grass through saved Outline Check masks. Stone has a darker, richer,
grittier finish with stronger existing ledges. Canonical paintings and seam strips
remain under `art/remaster/textures/TEMPERAT/` and `RATEMP/`; the original batch
outlines and grass/shade masks are preserved under `grass_features/2026-10-18/`.
`tools/bakery/pack_remaster_terrain.py --cell 144` packs the canonical paintings and
seam strips. `game/grassmask_TEMPERAT.pack` carries 2,167 grass-bearing cells.

`game/coastmask_TEMPERAT.pack` contains 1,442 explicit A8 land coverage cells from
192 complete Temperate templates. Complete-image masks are saved under
`art/remaster/textures/water/2026-10-18/coast_v03/masks/`. Grass and stone outlines
are protected, small shadow crevices stay dry, and the land-side fade is computed
before slicing into cells. The surface, seabed, terrain cut and seam strips use
consistent water eligibility. Runtime code never classifies diffuse colour as water.

Tiberian Dawn Enhanced Temperate uses Red Alert's W2 water painting through
`game/waterbase_RA_TEMPERAT.pack`, with darker blue-grey open water, blue-green
shallows, fine crossing ripples and restrained glints. Both flow phases use the
same smooth crossfade for colour, fine normals and broad normals. A phase reset
therefore has no raw normal sample that can jump in highlights or refraction.

World options place 3D Grass directly beneath 3D Trees. Grass defaults on in
Enhanced; F5 tree controls affect the current Enhanced meshes. F5 also provides
independent neutral Snow terrain brightness, contrast and saturation multipliers.
The exact latest saved user tuning is mirrored in `docs/tuning/opencnc3d-fx.cfg`.

## Verification

Both macOS architectures build. Hidden GPU checks compile the actual shaders and
exercise flow resets at 2, 4, 6 and 8 seconds for original, TD-painted and RA-painted
water. The largest mean reset jump fell from 30.56 to 0.0055 RGB bytes. A real-game
61-frame shoreline capture no longer has a reset spike. Isolated ground-family
checks reduce worst local two-cell correlations from 0.94/1.00/0.99 to
0.49/0.60/0.69, with mean luminance changing by less than 0.08 byte and contrast
within two percent. Matched TD and RA game captures verify the resulting appearance.

Mask checks preserve all 7,448,905 saved grass pixels and exact complete-image cell
slices. Dock checks cover both games, Enhanced/Classic/N64 isolation, HUD, fog,
controls, saved tuning and installed assets. Technical validation and artistic
approval are separate; new art revisions still require their applicable review.

## Shared lighting and Red Alert snow follow-up — 19 October 2026

The current unified-daylight default replaces broad painted shadows with actual
sun visibility and one common direct/sky response in both games. The earlier
painted-shade correction remains a legacy-path control only. Do not reintroduce
whole-cliff-cell lighting eligibility: open grass and identical grass beside cliffs
must respond to the same light. See `docs/terrain-lighting-controls.md` and
the engineering reports for the current source/geometry contract.

Red Alert snow uses `clearblend_RA_SNOW.pack` and `groundmask_RA_SNOW.pack`.
The latter preserves 41 exact native Outline Checks and 163 keyed coverage cells
under `art/remaster/textures/SNOW/2026-10-19/ground_continuity_v02/`.
Red in an Outline Check means painted rock outside the native footprint; it is
not a rock mask. Only the exact beige agreement label blocks shared snow. The
original albedos and Outline Checks stay unchanged. Apply shared snow after the
legacy seam strips, with authoritative coverage inside explicit feature masks;
otherwise those strips or eligibility fades paint the rejected fragments back.
Mirrors use the same snow before their own ground pass ends.

Adjacent outdoor RA road placements register their existing source exit UVs and
blend the narrow join band through the same geometry and light. Internal cells
of a single placement keep their source mapping. `OPENCNC3D_ROADJOINS=off` is a
control; native and Classic retain their existing renderer paths.
