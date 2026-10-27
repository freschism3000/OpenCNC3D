# Red Alert structures in Enhanced mode

The runtime import carries the approved intact models of 29 building types, with
FACF, WEAF, DOMF, SYRF and SPEF drawing their matching real building. Files live in
`game/ebld_ra` and ship in `ra3d/ebld`; matching C&C names never resolve to this art.

Included: AFLD, AGUN, APWR, ATEK, BARR, DOME, FACT, FCOM, FIX, FTUR, GAP, GUN, HBOX,
HPAD, IRON, KENN, MSLO, PBOX, PDOX, POWR, PROC, SAM, SILO, SPEN, STEK, SYRD, TENT,
TSLA and WEAP. GUN, SAM and AGUN use their exported pivot and authored forward
direction to follow the engine facing. Pitch, recoil and Tesla charge motion are pending.

An intact model replaces the original building and all its engine subobjects, including
WEAP2, while alive and outside construction, sale and an open factory door. Health loss
keeps the approved standing geometry until a separately approved damage model is available.
Other states retain Red Alert's original sprite presentation. Classic and N64 modes retain
their original art. Walls and fences draw OpenCNC3D's connection pieces (below). The lab's unapproved damage,
destruction, placement and effects are excluded. Effects are excluded even where the
intact model is approved with them: the import carries no emitters, so the Flame Tower
draws its soot but not the lab's pilot flames. A Red Alert structure below half strength
takes the cartridge's damage plume from its model's roof instead, and the engine's fire
anims on it burn on the roof; its death is Enhanced's rebuilt building blast and smoke
column at the middle of its plot.

Each model stands at its size on the plot (`art/remaster-ra/scale.json`, measured by
`tools/art/remaster/ra/scale_fit.py`): one uniform factor, never above 1, about the
footprint's centre on the ground, so a Red Alert building fills its cells as OpenCNC3D's
remasters fill theirs and a soldier reads the same against it as in Tiberian Dawn (the
Construction Yard 0.803, 1.25 cells high; the Power Plant 0.886, 1.40; the War Factory
0.855; the SAM Site 0.593; the Barracks, Tent, Radar Dome, Advanced Power Plant, Forward
Command Post, Turret and Helipad 1). The bake applies it, so the drawn model, its turret
pivots, its flag's wave, its pick silhouette, shadows, overlays and the Tesla Coil's bolt
crown all come from the smaller model; the engine's footprint, bib and apron cells do not
change. The lab shows it by its As OpenCNC3D size switch.

A model may reach past its plot (`_overhang` in `art/remaster-ra/yaw.json`, of the model at
its size): the Shipyard 0.221 cells past its plot, the Chronosphere 0.242, the Airfield's
control tower 0.011 past the plot's north edge (0.111 at its approved size). The model is
drawn whole and is not shrunk to fit its overhang; the per-building view test allows four
cells beyond the footprint, so the overhang is never culled on its own.

The art Red Alert shares with OpenCNC3D is drawn from OpenCNC3D's own approved files,
where the Red Alert lab approved the reuse: `tools/ra3d/stage-reuse.py` (run by
`stage-structures.py`) copies them from `game/ebld` into `ra3d/ebld` with
`reuse-manifest.json`. The civilian buildings V01 to V19, BIO, HOSP and MISS keep all
three stages (worn, broken at half strength, wrecked and sunk over their scorch, the
engine's death craters held back); the trees T01, T02, T03, T05, T07, T10, T12 and T13
stand at the engine's trunk point on temperate maps with their burning and snag stages,
and snow keeps its 1996 trees, since no reused tree has a winter look. The walls and
fences SBAG, CYCL, BRIK, BARB and WOOD are read from the engine's map (connection nibble
and damage level from OverlayData) and draw their connection pieces and stages; Red
Alert's own fence FENC keeps its 1996 picture. The steel crate and the field overlays are
models, and the craters and scorch marks are OpenCNC3D's remastered decals, with a grey
burn on snow. Under Enhanced effects Red Alert's 3D pass draws OpenCNC3D's particles
(`game/ra3d_effects_mod.h`), so the lab's blasts, smoke and flames on these models show,
and the engine's own explosions and fires on them stand down as in Tiberian Dawn.

Exterior gravel aprons reuse OpenCNC3D's approved softened g3s temperate art in
`bibenh.pack`, with the same cover/multiply passes and dials. RA supplies every apron
cell and BIB1/BIB2/BIB3 frame; no model footprint or apron position changes. Grass and
snow use that temperate set. Snow uses painted coverage at the shared strength without
the grass-to-brown multiply gain, which otherwise turns pale snow pink. Interior, Classic,
N64 and the disabled dial retain
original art. The apron follows the terrain's corners and stays beneath solid meshes.

The exporter reads footprint dimensions from Red Alert's current `bdata.cpp`, checks them
against the lab metadata, applies the RA plot turn and preserves the exported geometry.
It packs explicit house masks and uses the lab's blue/red finish for the object's exported
remap colour. Grey and blue remap indices differ between the two games. Barracks flags,
radar motion, fills and flipbooks use the exported intact motion on the simulation clock.
A building's original construction state, stage, make count, door stage and hit flash are
read from the brain; the renderer does not create a second gameplay state.

`tools/ra3d/structure-inputs.json` locks the approved export's sampled file hashes, turn and
footprint. `tools/ra3d/bake-structures.py` refuses changed inputs and detects a concurrent
revision during export. Updating that record requires approval of the changed intact art.
It never writes the lab or its review records. The packed manifest records the exact input
and output hashes. Each runtime building has fewer than 10,000 triangles.

Rebuild art with `python3 tools/ra3d/bake-structures.py --export-root build-native/remaster-ra`.
Run `tools/ra3d/gate-ra-structures.sh playable` for the hidden synthetic instance test.
`stage-structures.py` refreshes only module building art. `stage-runtime.py` copies a staged
module's data and the supplied platform binaries into a portable folder, leaving out the
movies and score that `tools/media/media-paths.txt` names under `ra3d/`.

RA reads OpenCNC3D's current `opencnc3d-fx.cfg` for Enhanced presentation and display settings,
including camera dials, resolution and vsync. RA snow maps also use the shared cool
temperate distance-fog dials; TD winter and RA interior retain their existing behavior.
Only RA's last Classic/Enhanced choice
comes from `ra3d-fx.cfg`. Without the shared file, its existing display settings remain.

Verified on Apple Silicon in hidden runs: 33 standing fixture instances including three
decoys and two damaged buildings, blue/red player masks, Classic fallback, flag waves,
and OpenCNC3D g3s gravel. A rifleman damaged the power plant from 200 to 157 health while it
kept its model. Root and subobject building draw records are both suppressed when their
model draws; the layer gate checks those actual records. Without a model, the original
body's last occupied sprite row is lifted to the ground and its subobjects share that
lift, preventing terrain depth clipping. Actual snow views confirm full Tesla and Kennel
models and brown aprons. Fog on/off changes the far ground, with no foreground/sidebar
change. Four original missions across snow, temperate and interior pass the
RA3D movement/scene gate. A real Soviet power plant builds through its original
construction animation and then switches to the approved intact model. Windows is
cross-compiled from the same source; gameplay there still needs a Windows machine.

All 66 converted RA scenes retain OpenCNC3D's shared cursor rigs and flipbooks in their lean
packs. The existing 23 remastered 3D cursor models attach without changing their art.
Pointer, move, attack, repair and sell were inspected in hidden RA views. They use the
shared `cursor_scale` (0.7), `ecur_scale` (0.75), and `curgreen_scale` (0.7) settings;
Classic and sidebar/dialog cursors keep RA's original sprite and action mapping.
The intact geometry also supplies the selection silhouette, so action cursors and
clicks follow the larger imported building rather than the old fallback box.
