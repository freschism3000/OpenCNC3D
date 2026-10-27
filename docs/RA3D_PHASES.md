# Red Alert 3D roadmap

Last updated: 17 October 2026

## Current position

**Current phase: Phase 3, the RA3D brain and the shared OpenCNC3D ABI**

Status: **IN PROGRESS**

### Direction revised 2 October 2026

These decisions supersede the parts of Phase 1 below that conflict with them. The full
text is under "Decisions of 2 October 2026" at the end of this file.

- RA3D is a NEW MODULE: a 3D Red Alert built the way OpenCNC3D rebuilt Tiberian Dawn. A
  player adds it from the launcher's "+" button.
- RA3D may have its own forked brain, built from the Red Alert source in
  `brain/vanilla/redalert`. It does not replace the Tiberian Dawn brain.
- Core capabilities are shared between the modules rather than built twice, big maps
  and high player counts first. RA3D must support big maps.
- Mixed GDI, Nod, Allied and Soviet matches belong to All Stars mode, which is on the
  back burner. Every installed module must eventually work in All Stars, so shared IDs and
  versioned definitions stay in the design, but Phase 5 is deferred.
- Red Alert data comes from the original 1996 discs as released free by EA in 2008.
- The next player-facing milestone is a real Red Alert module in the launcher's
  second slot. Launch must enter Red Alert's own menu and brain; the regular OpenCNC3D
  menu functions remain available, while N64 Visuals is present but greyed out.
  Port Red Alert music, effects, speech and FMVs with that menu. Then build the
  original Win95 tactical UI, campaign map and score screens, and Classic mode from
  Red Alert's source and disc art. Full-game playability and visual parity are exit
  gates, not claims made by an early launcher tile.

Phase 1 is complete as a historical specification. Its separate faction, house and
content-ID model remains useful; its one-brain and mixed-match-first direction was
superseded by the module decision above. The Tiberian Dawn brain stays in place.

Phase 2 is complete. It proved that the Red Alert brain can build through OpenCNC3D's normal
Windows and macOS pipelines, load the synthetic Red Alert bootstrap scenario through the
stock brain ABI, advance deterministic simulation ticks, and produce an equivalent
canonical state report on both platforms.

Work carried forward into its owning later phase:

- Phase 3 builds the RA brain's OpenCNC3D exports and validates how big-map changes are
  shared with the XL TD brain without claiming unsupported wire compatibility.
- Phase 4 owns module manifests and the content registry.
- Mixed-match work in Phases 5 and 12 remains deferred with All Stars mode.

## How agents must maintain this file

This file is the standing plan and progress register for RA3D. Any agent doing material
RA3D work must:

1. Read this file before starting.
2. Work against the current phase unless the task explicitly belongs to a later phase.
3. Update the relevant phase checklist when work lands.
4. Add links to supporting documents, tests or commits under that phase.
5. Change the current-phase marker only when the stated exit gate has been demonstrated.
6. Record blockers and deliberate deferrals rather than silently skipping them.
7. Keep completed requirements in the file so later agents can see what was proved.

Status vocabulary:

- **NOT STARTED:** no implementation work has landed.
- **IN PROGRESS:** work has started but the exit gate is not yet met.
- **BLOCKED:** progress requires a recorded decision or external dependency.
- **COMPLETE:** every required deliverable is present and the exit gate has passed.

Some asset work may overlap engineering phases after the relevant formats are stable, but
only one phase is the primary current phase.

## Module architecture

OpenCNC3D and RA3D have separate game brains and one renderer-facing OpenCNC3D ABI. Core
capabilities such as big maps and the scaled pathfinder must be shared in implementation
or kept equal by a verified gate. Stable content identities and explicit format versions
reserve a path for future All Stars matches; mixed simulation is deferred.

```text
OpenCNC3D module -> Tiberian Dawn brain --+
                                    +-> shared renderer ABI
RA3D module  -> Red Alert brain -----+
              shared big-map core or a parity-gated port
```

## Phase 1: Unified-engine specification

Status: **COMPLETE**

Goal: define the combined simulation before changing either game deeply.

Deliverables:

- [x] Record the one-brain, multiple-module direction.
- [x] Record four-faction mixed multiplayer as a primary requirement.
- [x] Complete the initial renderer, brain and asset reuse audit.
- [x] Inventory every meaningful Tiberian Dawn and Red Alert gameplay difference.
- [x] Decide base-game and expansion scope.
- [x] Define faction and house ownership independently of module identity.
- [x] Define stable global IDs for all networked and serialized types.
- [x] Define resources, storage, harvesting and mixed-map behavior.
- [x] Define production categories and prerequisites.
- [x] Define movement zones, including ships and submarines.
- [x] Define shared weapons, warheads, armor and targeting.
- [x] Define superweapon and special-unit behavior.
- [x] Define map, save, replay and script compatibility.
- [x] Define multiplayer versioning, handshakes and content hashes.
- [x] Decide how existing OpenCNC3D saves and maps are migrated or rejected.

Exit gate:

- Every known TD and RA feature has a documented destination in the unified model.
- No networked or serialized concept depends on a game-specific enum position.
- The mixed-match contract is precise enough to implement without guessing.

Evidence:

- Initial analysis performed on branch `RA3D`.
- Red Alert already has a remaster-style shared-library target under
  `brain/vanilla/redalert`, but OpenCNC3D-specific patches currently target Tiberian Dawn.
- `docs/ra3d/phase1-unified-brain-spec.md` defines the normative shared simulation,
  identity, economy, production, movement, combat, map, save and wire contracts.
- `docs/ra3d/td-ra-delta.md` records the source-driven TD versus RA feature inventory.

## Phase 2: Red Alert brain bootstrap

Status: **COMPLETE**

Goal: run Red Alert logic through OpenCNC3D's build and brain-loading infrastructure.

Deliverables:

- [x] Build the Red Alert library through the normal OpenCNC3D pipeline.
- [x] Produce matching Windows and macOS brain binaries.
- [x] Port minimum initialization and ABI validation.
- [x] Load a Red Alert scenario headlessly.
- [x] Advance the scenario through deterministic simulation ticks.
- [x] Add crash-stage diagnostics and deterministic state reporting.
- [x] Add automated bootstrap coverage to the normal gates.

Exit gate:

- A Red Alert map loads and advances reliably through OpenCNC3D host tools on both supported
  platforms.
- Equivalent inputs produce equivalent state reports.

This is a bootstrap milestone, not permission to retain a separate RA multiplayer brain.

Evidence:

- `brain/vanilla/redalert` now has the minimum headless initialization path, a fixed-seed
  release option, startup and scenario stage diagnostics, stock-ABI theater reporting,
  asset-free map and palette fallbacks, and safe compiled weapon and warhead heap minimums
  for the bootstrap scenario.
- `tools/mac/build-brain-mac.sh` and `tools/win/build-win.sh` have been extended to build
  the Red Alert remaster library alongside the existing Tiberian Dawn brain. Red Alert is
  not added to the shipped module package by this bootstrap work.
- `brain/tests/ra3d/RA3DBOOT.mpr` is a project-authored, asset-free single-player fixture.
  `brain/host/ra_bootstrap.c` is a stock remaster-ABI probe that checks API version 258,
  loads that fixture with seed 19005, advances exactly 60 ticks, and reports the static
  map and mission-timer samples without using the Phase 3 OpenCNC3D object exports.
- The normal `tools/win/build-win.sh` pipeline produced the 32-bit MinGW
  `RedAlert.dll` and `ra_bootstrap.exe` outputs locally.
  `tools/ra3d/run-bootstrap-gate.py` ran the probe twice successfully, and both runs
  matched `brain/tests/ra3d/expected-report.txt`: API 258, requested seed input 19005,
  scenario
  `RA3DBOOT`, theater 2, rectangle `20,20,24,24`, all samples alive, and mission timer
  20 at tick 0 through 16 at tick 60. The generated local evidence is
  `build/ra3d/ra-bootstrap-windows.txt`.
- `tools/ra3d/gate-ra-bootstrap.sh` drives the native macOS probe,
  `tools/ra3d/run-bootstrap-gate.py` enforces two identical runs against the expected
  report, and `tools/ra3d/compare-bootstrap-reports.py` performs the platform comparison.
  CI has native macOS and Windows runtime jobs plus an equivalence job.
- GitHub Actions run #872
  built both brains on macOS and Windows, passed the deterministic bootstrap gate on each
  native platform, and passed the CRLF-normalized byte comparison between their canonical
  reports. The run completed successfully from commit `8f496f05`.

The Phase 2 exit gate is complete. Both supported platforms load `RA3DBOOT`, advance the
same 60 deterministic ticks through the stock OpenCNC3D ABI, and report equivalent state.

## Phase 3: RA3D brain and shared ABI

Status: **IN PROGRESS**

Goal: turn the Red Alert runtime into the RA3D brain, speaking the same OpenCNC3D ABI the
renderer already uses for Tiberian Dawn, so the shared renderer can draw either module.

Revised 2 October 2026: the original goal was one brain replacing the TD brain. The TD
brain stays; RA3D gets its own fork, and the two share the ABI, the core capabilities
and the content IDs instead of sharing one binary.

Deliverables:

- [ ] Port the OpenCNC3D brain extensions the renderer depends on into the RA brain.
  The first read-only `OpenCNC3D_Dump_Objects` slice exports live infantry, units, vessels,
  aircraft, buildings and terrain with identity, position, health and facing. It now
  also exports building size and target cell, secondary facing, local selection,
  movement and attack targets, control group and aircraft height. Renderer
  presentation fields, overlay and effect state still need work.
- [ ] Export infantry, vehicle, vessel, aircraft, building, terrain, overlay and effect
  state.
- [ ] Port input, selection, production and control-group requests.
- [ ] Port save/load, rally points, debugging, scripting and editor exports. Save/load
  (its own folder and game tag, round trip exact, `tools/ra3d/gate-ra-save.sh`), rally
  points, the cheat switches, the AI takeover, trigger queries and the proximity query are
  ported; editor exports are not.
- [x] Port multiplayer event posting, draining and ABI validation. `OpenCNC3D_Event_ABI`,
  `Drain_Events`, `Post_Event`, `Set_Lockstep`, `Get_Lockstep` and `Set_Seed`; two brains in
  one process agree for 6000 ticks of computer play and on all 24 multiplayer maps
  (`tools/ra3d/gate-ra-lockstep.sh`, in CI), and two copies of the game play a match over
  loopback with no desync (`tools/ra3d/gate-ra-net.sh`); a Tiberian Dawn copy is refused
  for its order wire.
- [ ] Introduce stable global object and event IDs.
- [ ] Add explicit ABI and simulation-format versions.
- [x] Add deterministic state hashes and divergence diagnostics. The renderer's world hash
  (NETSYNC every 15 frames, NETDESYNC naming the seats) runs on Red Alert's object dump,
  which now carries every field Tiberian Dawn's does; the two-brain harness names the
  first differing object and field.
- [x] Decide how big-map capabilities are shared. The director chose one common
  implementation for map dimensions, coordinates, player limits and pathfinding,
  with TD and RA game rules remaining in their respective engines.
- [ ] Extract that common machinery from the XL TD work behind game-specific
  adapters, then build both engines against it and gate classic-map parity before
  enabling larger Red Alert maps.
- [x] Load an original Red Alert mission from the 1996 disc data, not only the synthetic
  fixture. `tools/ra3d/run-mission-gate.py` extracts SCG01EA.INI, RULES.INI and
  TUTORIAL.INI from the committed encrypted archives and compares two 60-tick object
  reports with `brain/tests/ra3d/expected-mission-report.txt`. CI is configured to run
  the same gate natively on macOS and Windows and compare the canonical reports.

Evidence in progress:

- The disc mission starts at rectangle 49,45,30,36 in the Snow theater. The first
  object dump has 73 live objects; at tick 60 it has 75. The export covers six
  categories, including a vessel heap that is empty in this mission. Counts and
  per-object state are checked, so map load alone cannot pass the gate.
- Headless startup now loads tutorial messages when present and supplies empty strings
  for missing messages. The mission also needs its original RULES.INI for real health
  and survival; missing rules left units and buildings at zero strength.
- Headless Red Alert startup mounts the original `LORES.MIX` when present so the
  engine can export infantry draw records. The frontend registers the RA data
  directory before `CNC_Init` mounts `CONQUER.MIX`; otherwise a shared TD install
  provides its same-named archive first and hides most RA structures.
- The fixed seed now applies to campaign starts through `CNC_Start_Instance_Variation`.
- The object export remains read only. The new fields are compared over two fixed-seed
  SCG01EA runs and include changing navigation, targets and aircraft altitude. This
  does not yet prove that RA3D renders, accepts orders, saves, or runs multiplayer
  through the shared ABI.
- Six more read-only renderer exports now report the Red Alert viewport, convert a
  cell to input pixels, describe selected objects, return compact selection state,
  name a mission, and probe the object and action under a pointer. The disc mission
  gate selects an Allied jeep through the original RA API, reads it back through the
  renderer ABI, locates it at its converted screen pixel, and confirms the probe did
  not change selection. Both object and renderer reports pass twice with fixed seed.
  Renderer integration and a playable input loop remain unverified.
- The object stream now carries all active house balances and counts plus the
  mission's opening view cell. SCG01EA identifies Greece as the local player,
  starts its camera at cell 63,47, and reports Soviet funds and storage from the
  RA engine. The fixed-seed report checks those fields at tick 0 and tick 60.
- The shared big-map architecture is approved. Its extraction and parity gates have
  not started; the current RA brain still uses the original 128-cell stride and
  eight-player limit.
- The previous branch CI ran the Mac and Windows native bootstrap jobs successfully.
  GitHub stopped the separate equivalence job before it started because an Actions
  budget prevented further use. The new disc-mission gate therefore still needs native
  Windows and cross-platform CI evidence.

Exit gate:

- The RA brain loads a real Red Alert scenario and exposes the same versioned OpenCNC3D ABI
  as the TD brain.
- Repeated scripted runs produce identical state hashes.
- The TD brain and its gates are unchanged.

## Phase 4: Module and content architecture

Status: **IN PROGRESS** (local second-slot frontend discovery; manifest and gameplay work open)

Goal: replace hardcoded Tiberian Dawn assumptions with module-owned data.

Deliverables:

- [ ] Define and version the module manifest.
- [ ] Add faction and technology-tree definitions.
- [ ] Add brain, rules and content version requirements.
- [ ] Add registries for models, sprites, effects, sounds, music and movies.
- [ ] Add theater and resource definitions.
- [ ] Add HUD, menu, campaign and house-color definitions.
- [ ] Implement module discovery and installation paths.
- [ ] Implement deterministic gameplay-content hashing.
- [ ] Convert OpenCNC3D itself to the new module format.
- [ ] Add RA3D as the second discoverable launcher module.

Exit gate:

- OpenCNC3D launches through its own module manifest without losing functionality.
- RA3D appears as a real second module.
- The shared renderer no longer assumes TD factions, Tiberium or TD-only production.

## Phase 5: Minimal mixed-match vertical slice

Status: **DEFERRED** (All Stars mode is on the back burner; the
standalone RA3D module comes first.)

Goal: prove the central promise before expanding the content catalog.

Initial slice:

- One TD faction and one RA faction.
- One shared test map.
- Construction Yard, power, barracks and refinery.
- Harvester and one shared collectable resource.
- One infantry type, one tank and one defensive structure per faction.
- Selection, movement, production, harvesting, combat and victory.

Deliverables:

- [ ] Join one match from an OpenCNC3D frontend and an RA3D frontend.
- [ ] Simulate all objects identically on both clients.
- [ ] Render every participating object's required visual state on both clients.
- [ ] Validate brain, ABI, rules, map and deterministic content hashes before launch.
- [ ] Complete a match through the normal victory condition.
- [ ] Detect and explain any state divergence.

Exit gate:

- A mixed-module multiplayer match can be played to completion without desynchronizing.

This is a separate, high-risk All Stars milestone. It is deferred while the standalone
RA3D module is built.

## Phase 6: Shared world and economy

Status: **NOT STARTED**

Goal: generalize maps, terrain and resources for both games.

Deliverables:

- [ ] Support Tiberium, ore and gems as explicit resource types.
- [ ] Implement values, density, spreading, harvesting and storage rules.
- [ ] Provide common harvester and refinery interfaces.
- [ ] Import native RA Temperate and Snow template catalogs.
- [ ] Support water, shoreline, bridge, cliff and naval-zone metadata.
- [ ] Support RA walls, crates, smudges and terrain objects.
- [ ] Import RA map formats and required scenario metadata.
- [ ] Preserve explicit resource types and interfaces for a later mixed-economy policy.
- [ ] Generalize map previews and editor terrain handling.

Exit gate:

- TD harvesters retain classic behavior and RA harvesters use original ore and gem rules.
- Native RA Temperate and Snow maps render and simulate correctly.

## Phase 7: Complete four-faction ground game

Status: **NOT STARTED**

Goal: make GDI, Nod, Allies and Soviet mechanically complete on land.

Deliverables:

- [ ] Complete all four technology trees and prerequisites.
- [ ] Implement base-game infantry and ground vehicles.
- [ ] Implement core structures and defenses.
- [ ] Implement engineers, spies, thieves, medics, dogs, mines and transports.
- [ ] Implement ownership colors and faction-specific behavior.
- [ ] Complete shared weapon, armor, movement and targeting definitions.
- [ ] Extend AI construction, economy, attack and defense to all four factions.
- [ ] Add regression coverage for original TD faction behavior.

Exit gate:

- TD and RA land matches are playable within their own modules without placeholder
  gameplay logic.
- Every faction's AI can build, harvest, attack, defend and recover.

Placeholder visuals may remain at this point, but mechanics may not be substituted.

## Phase 8: Naval, aircraft and superweapons

Status: **NOT STARTED**

Goal: add the major capabilities that distinguish Red Alert.

Deliverables:

- [ ] Add vessels to the shared renderer and object ABI.
- [ ] Implement ship movement, pathfinding, targeting and wakes.
- [ ] Implement Shipyards, Sub Pens and naval production.
- [ ] Implement loading, transport and unloading behavior.
- [ ] Implement submarine submerging, surfacing, detection and combat.
- [ ] Complete RA aircraft, Airfield and Helipad behavior.
- [ ] Implement Chronosphere and chrono movement.
- [ ] Implement Iron Curtain behavior.
- [ ] Implement Tesla and Gap Generator systems.
- [ ] Implement nuclear and missile-silo systems.
- [ ] Implement paratroopers and parabombs.
- [ ] Keep a versioned interface for later cross-module superweapon sharing.

Exit gate:

- RA land, air and naval matches remain deterministic.
- Every implemented RA superweapon uses the RA targeting and network-event model.

## Phase 9: RA3D HUD and shared user interface

Status: **IN PROGRESS** (second-slot frontend preview; gameplay and full menu content open)

Goal: give RA3D complete player-facing functionality while retaining OpenCNC3D's menu
content and behavior.

Deliverables:

- [x] Add RA3D title and module identity to the launcher and frontend preview.
- [x] Render the Red Alert 3D title at the original Windows 95 menu's 640x400
  size, keeping the centered 3D lettering. The menu pack carries the original
  high-resolution title fonts, and the first menu shows all nine requested
  entries. Load, Skirmish, Multiplayer, Special Ops and User Maps still need
  working Red Alert routes; other menus and the tactical HUD remain low-resolution.
- [x] Add Allied and Soviet faction selection. Start New Game selects either original
  first mission; the headless launcher gate selects and moves an Allied jeep and a
  Soviet rifleman through their respective original engines.
- [x] Present Easy, Normal and Hard campaign difficulty before faction selection
  and pass the choice to the Red Alert engine. The remaster build reads the
  original difficulty entries from `RULES.INI`; campaign-wide balance parity
  still needs original-game comparison.
- [x] Add original low-resolution Red Alert sidebar artwork and Allied/Soviet
  variants. The selected theater palette follows the mission.
- [x] Draw the Classic tactical scene from Red Alert's 640x400 Win95 art,
  with a 480-pixel-wide map and the original 160-pixel-wide sidebar,
  credit tab, theater palette and Allied/Soviet artwork from `HIRES.MIX`.
  Keep the map at whole-pixel magnification; size the sidebar separately so
  16:9 windows show five usable cameo rows. Taller windows repeat the clean
  48-pixel sidebar strip section to add rows. Wider windows extend the map
  while keeping the sidebar at the right. This
  is a first render pass; full control behavior and visual parity remain below.
- [x] Keep the full Classic viewport inside the mission rectangle. The Allied
  opening and both extreme scripted camera jumps hit the correct bounds.
- [x] Put a green Red Alert circle in the launcher's second tile and size the
  OpenCNC3D emblem to match it. The hidden launcher render was inspected.
- [ ] Match the original Win95 HUD layout, radar, power, credits, sidebar strips,
  cameo art and production clock in Classic mode.
- [ ] Add infantry, vehicle, aircraft, vessel and building production categories.
- [ ] Match building placement, legal and blocked placement previews, construction
  animation, hold, cancel and deployment behavior to the original interface.
- [ ] Add Radar Dome behavior and power/credit presentation.
- [ ] Add repair, sell, map and superweapon controls.
- [ ] Port the original pointer shapes and animations for selection, movement,
  attack, capture, repair, sell, placement and blocked actions. Both theater
  palettes and all 222 frames of `MOUSE.SHP` are staged, and the Red Alert cursor
  control table is active. Verify each action and transition against the original.
- [ ] Add Red Alert names, descriptions and tooltips.
- [x] Draw Red Alert's menus under Enhanced in its own approved kit
  (`art/interface/ra-enhanced-menu-v01`, `menu/bake_ra_enhanced_menu.py`): main menu with
  the lifted RA3D lettering and two 3D emblems, the Enhanced side and difficulty box, every
  options page red at the title and blue in a mission, Load and Save on Red Alert's own
  index, Restate on ALIPAPER or SOVPAPER. Gates G325 (Red Alert pass) and G341 (the walk).
  Skirmish runs in the shared lobby with Red Alert's 24 maps, eight countries and tech to
  10; Multiplayer opens the shared room screens on Red Alert's maps and countries; both
  lobbies' rules carry Shroud Regrows, Capture The Flag and the computers' difficulty; a
  match ends on Red Alert's Enhanced debrief (TOP SCORES).
- [x] Reuse the shared multiplayer lobby with Red Alert's maps, countries and rules.
- [x] Show N64 on RA3D Visuals in the disabled style and refuse activation by mouse
  and keyboard.
- [ ] Reserve module and faction identities for later All Stars selection.
- [ ] Add clear missing-content and version-mismatch messages.

Exit gate:

- Every implemented Red Alert feature is usable without developer controls.
- OpenCNC3D and RA3D provide equivalent menu functionality.

## Phase 10: Complete visual and audio assets

Status: **IN PROGRESS** (disc media decoding proved; integration and visual assets open)

Goal: replace development placeholders with final Red Alert presentation.

Deliverables:

- [ ] Produce or import approved Red Alert structure models. **The Construction Yard deploys
  out of its MCV** under Enhanced (G346, 17 Oct): the approved placement baked onto the yard
  as the game draws it (tools/ra3d/bake-placements.py, game/eplc_ra), started the tick the MCV
  goes.
- [ ] Produce Red Alert tanks and support vehicles. **The reused ones are in** (gate G343):
  under Enhanced the Artillery, Phase Transport, Light Tank, APC and Medium Tank are
  OpenCNC3D's remastered models (`ra3d/evhc`, `tools/ra3d/stage-vehicles.py`); a Red
  Alert remaster of its own drops in as `game/evhc_ra/<TYPE>.evhc`. **Red Alert's own, first
  three in** (gate G345, 17 Oct): the Heavy Tank, the V2 Rocket (its launch section raised
  to 45 degrees on its telescopic rams while the engine has it standing to fire, the rocket
  launched from the rail's tip and flying as the model's own rocket on an arc onto the
  engine's fuse point, the empty section lowered while it reloads, by the brain's doing= 3,
  dostage=, ammo= and the projectile's tlx=/tly=), the Ore Truck (its scoop and dump body)
  and the Ranger (its gun turning and kicking, its wheels rolling and steering). Open: every
  other type (own remasters).
- [ ] Produce aircraft and naval vessels. The Chinook and the Longbow are OpenCNC3D's
  (G343). **The Gun Boat is in** (G345, 17 Oct): ships are in evhc's rule, the Gun Boat at
  sea from its own model, its bow gun the engine's turret with its kick, its twin turret
  fixed astern, selected, hurt and sunk as a vehicle is. **The Transport is in** (G345, 17
  Oct): it turns with its facing and lowers its four ramps in the 1996 order while the
  engine has its door open (the brain's door= for ships), each resting on the beach where
  the ground rises. Open: the other aircraft and every other ship (no file yet).
- [ ] Produce terrain objects and the Interior theater.
- [ ] Red Alert's own ground under Enhanced. **First slice in:** the placeholder pack's
  ground is replaced by the mission's own map (from the engine's static map export),
  flat, over the whole mission rectangle, with Red Alert's terrain set at 144 texels a
  cell for SNOW and TEMPERAT (the 1995 tiles for Interior), variants per placement and
  the join strips drawn by the code Tiberian Dawn's terrain uses (`game/ra3d_terrain.h`,
  gate G310). Everything Enhanced already drew stands on it. Open: the final paintings
  (only enlarged 1995 tiles so far), the sea and the coast, heights, ground detail.
- [ ] Bake complete RA infantry animation sets.
- [ ] Render every original Classic infantry, vehicle, structure, aircraft, vessel,
  projectile and effect sprite with its correct theater palette and player color.
  The first Allied and Soviet missions draw from 329 original Red Alert shapes and
  3,066 theater icons. Complete type, remap, animation and shroud parity is open.
- [x] Give every mission a 3D scene for the Enhanced mode. All 42 campaign missions and
  all 24 multiplayer maps (`tools/ra3d/missions_3d.py`) have their own pack,
  `ra3d/3d/<SCEN>.pack` (`tools/ra3d/bake-3d-mission.py`, baked lean: terrain, heights,
  water and shadows only, about 8.8 MB each): Red Alert's own 128 x 128 terrain, keyed by
  Red Alert's own template numbers on a bank per theater, RASNOW, RATEMP and RAINT (every
  icon of the disc's templates for that theater; `tools/ra3d/bake_ra_terrain3d.py bank`),
  so the templates Tiberian Dawn never had (shore cliffs, Red Alert's own shores, bridges)
  draw their own art. Enhanced paints snow and temperate with Tiberian Dawn's 144-texel
  sets, carried BY PICTURE: a Red Alert icon takes the painting of the Tiberian Dawn icon
  its 1995 pixels match (692 icons in each theater, 77 to 99 percent of each map's cells);
  names are not trusted, because Red Alert's SH01..SH56 are different shore pieces from
  Tiberian Dawn's SH1..SH63. Interior has no set. Heights are the auto heightmap
  (`tools/ra3d/ra_heights.py`, the editor's generator on Red Alert's own cells, read by
  picture match, then close namesake, then Red Alert's land type, with building
  footprints), committed as `data/ra3d/autoheight/RA<...>.HGT`; interiors are flat floors.
  Every object, overlay and animation is Red Alert's own sprite standing on the 3D ground
  (`game/ra3d_sprites_mod.h`), except soldiers with an Enhanced model, which draw as that
  model. Gates: `tools/ra3d/gate-ra-3d.sh` (Allied 1 in Enhanced),
  `tools/ra3d/check-3d-packs.py` (all 66 packs carry their own theater and heights); all
  42 campaign missions were booted in Enhanced headless (`--rascen`). Open: the multiplayer
  scenes cannot be seen until Red Alert has skirmish, the heightmaps want a map-by-map
  review, paintings for Red Alert's own templates, 3D models for the other objects, and
  water through the water shader.
- [ ] Bring Tiberian Dawn's Enhanced presentation to the 3D scene. **First pass in**
  (gate G335, `game/gate_ra3dparity.py`): the Tesla Coil's bolt draws under its model,
  from the crown to the target; trees and the ore mine stand on their own cells
  (TerrainClass draws from the top left); tread marks, blood, brass and spray; the cliff
  softening on Red Alert's own cliff templates; footsteps for the spy, thief, general,
  dog and civilians; drive sounds for Red Alert's own vehicles; the temperate ambience
  bed on RATEMP; shader prewarm, building-texture preparation and the terrain sun cache.
  Open: the remastered effects, the Enhanced menu and HUD, an ambience for snow and
  interior, a paw recording, per-hull track gauges, the cold first frame's ground build
  (see the progress history). The remastered decals, trees, civilian buildings and walls
  the lab approved as reuse are in (gate G338, the progress history), and since 16 Oct
  their snow looks too: the one winter file each shares with Tiberian Dawn's winter
  (`<T>_WIN.ebld`, staged by stage-reuse.py); the haystacks' snowman and igloo wait on
  their concepts' review.
- [ ] Produce production icons and HUD art.
- [ ] Wire original RA movies into intro, briefing, map and ending flows. The existing
  VQA decoder scanned REDINTRO.VQA end to end: 188 frames, 573436 PCM bytes at 15 fps.
  This is a decode result, not menu playback or visual fidelity proof.
- [ ] Produce projectiles, special effects and destruction states.
  A first-mission combat probe confirms the attacked building flashes while an
  untouched building remains steady; the move marker and explosion probes pass.
- [ ] Import and map approved unit speech, sound effects and music. The original
  SOUNDS, SPEECH, SCORES, ALLIES and RUSSIAN archives have verified staging through
  `tools/ra3d/stage-audio.py`. Named engine effect and EVA events resolve; Allied
  and Soviet selection and move acknowledgments played in headless mission runs.
  Separate clicks now re-enable Red Alert's selection voice latch; a hidden
  replay selected the same Allied jeep twice and heard two acknowledgments.
  Red Alert's 39 original theme rows and English titles are generated from its
  source; the first Allied and Soviet tracks and their side restrictions pass
  the mission gate. Full event, jukebox interaction and device playback coverage
  remains open.
- [ ] Match unit acknowledgments, EVA voiceovers, weapon and ambient sounds, music
  selection and volume behavior in actual mission play.
- [ ] Import and map approved campaign movies and presentation assets.
- [ ] Declare a Tier 1 implementation or recorded fallback for every visual feature.
- [ ] Record the provenance and distribution status of every shipped asset family.

Priority order:

1. Common Red Alert multiplayer units and structures.
2. Complete Allied and Soviet base-game rosters.
3. Campaign-only objects.
4. Expansion content.

Exit gate:

- No base-game multiplayer object uses an unrelated OpenCNC3D placeholder.
- Every known missing or substituted asset is recorded explicitly.

## Phase 11: Campaigns, missions and editor

Status: **IN PROGRESS**

Goal: complete the Red Alert single-player experience and its authoring support.

Deliverables:

- [ ] Add Allied and Soviet campaign flows.
- [ ] Add briefings, objectives and mission progression.
- [ ] Match the original Allied and Soviet map screens, mission choices, score
  screens, win and loss movies, and campaign endings.
- [ ] Port required mission triggers and scripted events.
- [ ] Add score screens, movies and campaign speech.
- [ ] Extend the editor for RA terrain, resources and naval placement.
- [ ] Extend trigger editing for RA-specific behaviors.
- [ ] Add campaign-level regression and completion tests.
- [ ] Run TD campaign regressions on the TD brain and RA campaign regressions on the RA brain.

Current evidence: `tools/ra3d/bake-campaign.py` stages Red Alert's two original
score transitions and 26 Allied/Soviet map reels without TD campaign artwork.
The Red Alert-specific score screen draws the original WSA plate and its source
time, leadership, economy and total labels and values. The map screen uses the
original WSA reel and `MapCoords` choice rectangles from Red Alert's `mapsel.cpp`.
The Win95 score path loads the original 640x400 `ALIBACKH.PCX` or
`SOVBACKH.PCX` from `CONQUER.MIX`, plus its high resolution font and shapes.
The renderer composes that resolution after the WSA transition. It now carries
the engine's base points and ending credits to the score screen, and sizes
casualty and building bars by the source's common maximum. The first Red Alert
hall-of-fame pass displays seven faction-colored rows, accepts an editable
commander name and saves a separate 24-byte-per-row table. A hidden Allied flow
entered `OpenCNC3D` with score 1234 and rendered that row. The original count-up
choreography and native Windows interaction remain open.
For the high resolution font, the score renderer zeroes palette classes 0 and 1
as Red Alert's print call does. Both faction screenshots were inspected without
the pale boxes that the raw source palette tables had painted behind every glyph;
the campaign gate checks the rendered label and its background.
An engine-driven Allied final-mission victory produced score 3000, base points
1500 and 1800 ending credits on the rendered Win95 plate. Both faction flows
passed two-mission checks in the exact Desktop installation after the updated
frontend and campaign pack were copied there. The Mac and Windows cross-builds
passed locally; native Windows execution remains to verify.
The two-mission synthetic flow gate enters each faction's second original mission
through those screens. A separate Instant Win gate takes the engine's actual
first Allied mission result and observes `SNOWBOMB` and its score. These checks
also capture the Classic battlefield announcement, reject opaque font cells,
and visually confirm the matching loss banner. The exact Desktop play gate passed
with the updated Red Alert frontend and brain; its Tiberian Dawn executable was
unchanged. The latest full Windows cross-build passed. These checks do not prove
mission objectives, full score animations, all map choices or carryover.
The shell now retains all four game-over movie names and
the post-score ending name. A two-round flow probe verified Allied 10A goes
straight into 10B with no score or map screen. Direct final-mission verdict probes confirmed the
engine names `ALLIEND` and `SOVFINAL` after the score; their playback and full
mission victories remain open. The Allied 10A INI's skip-score/skip-map flags route to
10B, and both base campaigns have their source finale at mission 14. The expanded
gate boots the full Allied all-A route (15 missions including 10B) and the Soviet
all-A route (14 missions), processes both finale results and returns to the
menu. This is a synthetic progression gate; actual mission victories and the
alternative choices still need gameplay gates. The engine's `ALLIEND` ending name
does not match the disc entry's name hash: `_C1FB1EE5.BIN` matches
`ALLYEND.VQA` and contains a VQA movie. Staging preserves those bytes and adds
an `ALLIEND.VQA` runtime alias. The VQA decoder read all 2,620 frames. Bounded
app flows on both mission 14s now request a real engine verdict, receive the
ending movie name, open that reel and return to the menu. The app flow caps
movies at 24 frames, so uninterrupted full-length playback and speaker output
remain open.

Exit gate:

- The original OpenCNC3D campaigns remain complete on the TD brain, and both base Red Alert
  campaigns can be completed on the RA brain.

## Phase 12: Multiplayer hardening and balance

Status: **DEFERRED**

Goal: make mixed multiplayer reliable, diagnosable and intentionally balanced.

Deliverables:

- [ ] Run long-duration determinism and AI stress tests.
- [ ] Support mixed-match save/resume and replays where in scope.
- [ ] Harden disconnect, defeat and reconnect behavior.
- [ ] Add content and version mismatch diagnostics. Red Alert rooms and matches refuse a
  Tiberian Dawn copy (order wire), other map bytes and an older wire by name
  (`tools/ra3d/gate-ra-net.sh`, `tools/ra3d/gate-ra-room.sh`); mixed matches do not exist.
- [ ] Complete divergence logs and automated state comparisons.
- [ ] Balance the four factions for combined play.
- [ ] Classify maps by supported factions, theaters and economies.
- [ ] Provide `TD Classic`, `RA Classic` and `Combined` rule presets.
- [ ] Keep classic presets faithful while allowing deliberate combined balancing.

Exit gate:

- Automated mixed matches run for hours without divergence.
- Incompatible clients are rejected before a match begins.
- Every rules preset is identified clearly and behaves predictably.

## Phase 13: Platform completion and release

Status: **NOT STARTED**

Goal: ship OpenCNC3D and RA3D as two installable modules using a shared renderer contract.

Deliverables:

- [ ] Produce Windows and macOS builds from the same commit.
- [ ] Complete RA3D launcher installation and updates.
- [ ] Implement module dependencies and match-content acquisition.
- [ ] Provide an upgrade path for existing OpenCNC3D installations.
- [ ] Extend release gates to both standalone modules. Mixed-match gates belong to
  the deferred All Stars phase.
- [ ] Validate Win98 and Voodoo 2 behavior and documented fallbacks.
- [ ] Test performance in large battles for each module.
- [ ] Test clean installation, repair and update flows.
- [ ] Complete licensing and asset-distribution review.
- [ ] Publish player and contributor documentation.

Exit gate:

- A clean installation can install and launch either module with its required content.
- OpenCNC3D remains fully playable on its TD brain.
- RA3D is complete as a standalone multiplayer and single-player Red Alert port.
- Cross-module mixed matches remain a separate All Stars milestone.

## Progress history

### 22 September 2026

- Created branch `RA3D` from the current OpenCNC3D main line.
- Audited the OpenCNC3D brain, renderer, launcher, menus, terrain systems and existing model
  catalog against the local Red Alert source.
- Confirmed that the Red Alert source already exposes a remaster-style library interface,
  but does not contain OpenCNC3D's project-specific brain extensions.
- Confirmed that the renderer currently lacks a vessel category and uses TD-specific
  Tiberium and sidebar paths.
- Agreed that one unified brain must support GDI, Nod, Allies and Soviet in the same
  deterministic multiplayer matches.
- Began Phase 1.
- Wrote the first complete unified-brain specification and TD versus RA delta inventory.
- Selected base Red Alert as the initial complete scope while reserving a stable expansion
  namespace for Counterstrike, Aftermath, ants and carrier content.
- Defined canonical string keys with collision-checked 64-bit TypeIds, monotonic instance
  IDs, separated house/faction/country/seat identities, shared economy rules and the
  versioned mixed-match handshake.
- Completed the Phase 1 exit gate and made Phase 2 the next active phase.
- Began Phase 2 by adding the Red Alert headless bootstrap path, deterministic seed
  control, crash-stage diagnostics and asset-free scenario support.
- Added the synthetic `RA3DBOOT.mpr` fixture and a stock-ABI bootstrap host designed to
  produce the same canonical 60-tick report on Windows and macOS.
- Began integrating Red Alert into the normal Windows and macOS brain build drivers.
- Passed the local two-run Windows bootstrap gate with API 258, fixed seed 19005 and the
  expected mission-timer progression through 60 ticks.
- Fixed the copied source-artifact modes exposed by the normalized source-tree guard.
- Completed Phase 2 in CI run #872: macOS and Windows both passed the native two-run
  bootstrap gate, and their canonical reports passed the cross-platform comparison.
- Made Phase 3 the next active roadmap phase without starting its implementation.

### 2 October 2026

- Audited the branch against main, which had gained 612 commits since `RA3D` was cut.
- Merged main into `RA3D`. Two conflicts, both resolved; the one that matters was in
  `tools/mac/build-brain-mac.sh`, where main had made the Mac
  brain universal (arm64 and x86_64). Both brains now build universal and both are checked
  for the deployment floor and for both CPU slices.
- `tools/check-brain-checkout.sh` passes on the merged tree: the auto-merged
  `vanilla-opencnc3d.patch` still reproduces the committed checkout exactly.
- Re-ran the Phase 2 bootstrap gate natively on Apple Silicon against the universal
  `RedAlert.dylib`: two identical runs, matching `brain/tests/ra3d/expected-report.txt`.
  The x86_64 slice was built and verified present but not executed. The Windows half is
  re-proved by CI on the pushed merge.
- Phase 3 remains NOT STARTED. Open direction questions recorded for decision before it
  starts: see "Open decisions before Phase 3" below.
- Downloaded the two 1996 disc images of EA's 2008 freeware release (Internet Archive
  item `cnc-red-alert`); both match the archive's md5.
- Added `tools/ra3d/ramix.py`, which opens Red Alert's encrypted MIX archives with the
  public key and Blowfish tables read from the source in `brain/vanilla`. Checked against
  the standard Blowfish test vectors and by every archive's header arithmetic.
- Added `tools/ra3d/extract_ra_disc.py` and committed its output, `data/radata`: 17
  inner archives byte for byte and the cinematics split out of the two movie archives,
  112 files, 754 MB. Every archive present on both discs is identical on both. Both
  campaigns' mission files and the rules file are present.
- Follow-up: `tools/snow_to_n64bank.py` still reads the SNOW theater from an OpenRA
  content folder and refuses encrypted archives. Moving it onto `data/radata/SNOW.MIX`
  needs `ramix.py`, and the disc's archive has 331 entries where the OpenRA copy has
  332, so the rebuilt bank must be compared before anything that ships changes.

### Phase 3 implementation start

- Loaded SCG01EA from the original disc archives with its original rules and tutorial
  messages in the headless host. Two fixed-seed 60-tick runs produced identical object
  reports: 73 live objects at start, 75 at tick 60.
- Added the first read-only RA object dump, including the vessel heap, and a native
  mission gate for both supported platforms. Cross-platform CI evidence is pending.
- Kept the synthetic fixture gate unchanged.

### Second-slot frontend preview

- Added Red Alert as a discovered second launcher tile when its staged title pack,
  brain, sound, speech and score archives, local audio archive and intro are present.
  Launch passes `--module ra3d` to a separate RA frontend executable; the RA route
  has its own title plate, menu score, intro and Visuals configuration file.
- `tools/ra3d/bake-menu.py` now builds a separate RA menu pack from the original
  `TITLE.CPS` palette, low-resolution pointer and fonts. The five main buttons use
  Red Alert's original layout and the `PCOLOR_DIALOG_BLUE` remap values read from
  `PALETTE.CPS`; no TD dialog is drawn over the RA title. The original title art
  supplies the red panel and copyright line. The supplied 320x200 Red Alert 3D title
  art at `menu/art/ra3d-title.png` replaces the original title pixels in the RA pack;
  the bake maps it into the original title palette and leaves the source PNG intact.
- `tools/ra3d/stage-frontend.py` stages that pack, `REDINTRO.VQA` for startup,
  `PROLOG.VQA` for the Intro button, the RA brain and original audio archives.
  Native execution of the full movie and later cinematic chain remains to verify.
- N64 is visible but disabled in RA Visuals, including the Advanced tab. A direct
  input gate covers mouse and keyboard, and the TD state remains selectable.
- The RA tile now opens the original Allied SCG01EA mission from Start New Game.
  The RA simulation runs in the shared renderer: a headless app test clicks that menu
  button, selects JEEP 0, gives a move order, and observes it at cell 60,50 after
  90 ticks. The app returns to the menu and passes the same route a second time.
  `tools/ra3d/gate-ra-play.sh` preserves that end-to-end check. This is a first
  playable mission, not completion of the campaign or the Phase 4 and 9 exit gates.
  Original Red Alert snow and temperate terrain and original object sprites are
  drawn in Classic. The renderer now uses the original RA low-resolution sidebar
  pieces, radar emblems, button images, 80 base-game cameos and theater palettes.
  The canvas is still 320x200 rather than the requested Win95 640x400 presentation,
  with incomplete fog
  and campaign screens. Mission music opens on Red Alert's original first retail
  track, BIGF226M, while Soviet SCU01EA opens CRUS226M. This does not meet the
  full Classic parity gate.
- The Allied opening gate now reads the source home waypoint as the centre of
  the Classic view. It checks 22 infantry and 26 building draw records, then
  advances the original SCG01EA mission until Tanya unloads at cell 63,48.
  There is one live transport; the second `TRAN` layer is its shadow and draws
  dark on the ground. `tools/ra3d/gate-ra-opening.sh` checks the installed
  module and captures both opening and Tanya frames. This verifies this one
  mission opening, not the full Win95 campaign.
- The original `TRANS.ICN` foundation stamps are now drawn in Classic while placing
  a building. The Soviet SCU02EA construction gate produces a power plant through
  its sidebar cameo, checks a legal and blocked footprint, clicks a legal map cell,
  and observes a new USSR power plant at that cell. The source stamp uses white
  hatching for clear cells and red hatching for blocked cells. This is one structure
  in one original mission; the remaining production roster and campaign flow are
  still open.
  In the same mission, a second gate builds a Barracks through its cameo, places it
  by a visible map click, then trains a Soviet rifleman and produces a Yak through
  their newly available original cameos. The engine reports all three live objects.
  A later original Soviet mission, SCU08EA, exposes the submarine cameo; a production
  gate confirms one new live Soviet submarine after the build completes. This checks
  one naval production path, not naval combat or the complete fleet.
  In SCU09EA, an original Soviet mission with a Refinery, the War Factory cameo builds
  and places a factory; its newly available Heavy Tank cameo produces one new live
  tank. The gate compares engine object IDs and map cells before and after to
  separate the new tank from the mission's starting tanks even when a destroyed
  tank's heap slot is reused. Vehicle combat and the complete roster remain open.
- The 1996 audio archive payloads and intro decoder pass direct checks. The menu
  uses `INTRO.AUD` from `LOCAL.MIX`, as Red Alert's `menus.cpp` specifies; the map
  selection theme `MAP.AUD` is in that archive too. The audio stage verifies and
  repacks the entire original archive as `RA_LOCAL.MIX`. A headless
  launcher-to-app menu capture verifies the active track. The first 4.46 seconds of
  INTRO decoded to non-silent 16-bit PCM (peak 8600, RMS 2610.0).
  An app playback probe rendered frames 0, 8 and 20 of `REDINTRO.VQA`; the same
  frames of `PROLOG.VQA` also rendered with a non-silent 2.4-second audio recording.
  RA menu controls now load `ra3d-controls.cfg`, since the TD Desktop control file can
  contain a zero music volume and should not silence the new module. Jukebox
  interaction, complete effects and speech mapping, other movies, Win95 HUD and campaign
  screens remain open. The mission jukebox now reads the source-derived Red Alert
  theme table, filtering by original side flags and archive availability; its
  player interaction still needs a visual and functional gate.
- The Soviet SCU01EA route now waits for its starting rifleman to become visible,
  selects his original sprite at cell 42,81, orders a move to 43,80, and checks the
  engine reports that destination. Classic picking uses the drawn sprite rectangle;
  map input follows the Red Alert engine's mission rectangle rather than the
  temporary 64-cell terrain pack. The same gate observes original `YESSIR1.Rxx`
  and `RITAWAY.Rxx` clips resolving. Allied `YESSIR1.Vxx`, `AFFIRM1.Vxx`, and
  `REINFOR1` EVA clips also resolved in headless playback. These are event and
  decoder results, not a speaker playback or full mission-completion claim.
  The same gate checks the Soviet move pointer's source frame count, rate and
  hotspot (4, 4, 14,11) and confirms Allied `BIGF226M` and Soviet `CRUS226M`.
- The Mac app and launcher builds and the Windows cross-builds pass locally. GitHub's
  latest RA3D run did not start either native platform job because the Actions budget
  prevented further use, so native Windows execution remains unverified for this work.
- The RA brain now exports active animation objects to the shared renderer.
  Classic resolves `MOVEFLSH` and explosion art from the selected theater pack.
  A local scripted jeep order showed the ground marker at its destination, and
  a killed jeep produced the original `FRAG1` animation and a played `KABOOM30`
  sound lookup. The effects gate also
  compares screenshots at the effect cell, so an emitted animation record alone
  cannot pass it. This checks those two effects, not the complete effect roster
  or speaker playback.
  A source-to-pack audit found art for all 62 base-game animation names.
  `BEACON` is remaster-only and `ANTDIE` is for the separate ant expansion;
  neither is present on the two retail discs. The sprite baker now fails if
  any base-game animation name loses its original art.
  The integrated gate passed against the Desktop `OpenCNC3D` installation; the Mac
  and Windows cross-builds passed locally. Native Windows execution is pending.
- The campaign pack now contains the original Win95 Allied and Soviet 640x400
  score plates, high resolution font and score ornaments. The screen renders
  at source resolution after the original transition reel. Scores and bars are
  presently calculated from the existing campaign summary; source timing and
  hall of fame data still need implementation and visual comparison.
  The local campaign gate passed both full synthetic campaign routes and the
  original ending media inventory with these score assets installed.
- A desktop preview needs its own RA frontend executable and local SDL libraries under
  `ra3d/`. The launcher selects that executable for the second tile, while the
  existing TD executable remains at the installation root. Module discovery checks
  for the RA executable as well as its brain and media. This keeps the two frontend
  builds independent while the RA gameplay path is still under construction.

### First 3D scene: Allied mission 1

Allied mission 1 can be switched to Enhanced (or N64) and plays in 3D on its own ground,
generated relief and Red Alert's own sprites; see the Phase 10 item. The Classic gates
(`gate-ra-play.sh`, `gate-ra-opening.sh`, `gate-ra-effects.sh`) pass unchanged alongside
`gate-ra-3d.sh`.

### Red Alert's ground under Enhanced

Enhanced's world in the Red Alert module stood on a placeholder: the Tiberian Dawn pack
the module loads, temperate grass under every mission and 64 cells of a 128-cell map.
It now stands on the mission's own map.

- **What is drawn.** The map from `GAME_STATE_STATIC_MAP` (template INI name and icon per
  cell, the mission rectangle, the theater), flat on the world grid, in `draw_frame`'s
  place for the placeholder's sea, terrain and coast; its ground detail (patches, clumps,
  rocks) is off in Red Alert, and its Enhanced sheet is not built. Every height query
  returns 0 in this mode, so the infantry, the building boxes, the vehicle stand-ins,
  trees and effects stand on the new ground. Lit as Tiberian Dawn's Enhanced flat ground,
  through the same post chain, with its own flat normals for the sun. The pointer, the
  shroud blanket and the camera's grid rule follow the mission rectangle up to 128 cells;
  the radar plots the Red Alert cells.
- **The terrain set.** `ra3d/terrainenh_RA_<TH>.pack` (`C3DRA144`, keys of template
  name, icon, variant and template size) becomes one sheet with a slot per key; a cell
  the pack lacks takes its 1995 tile from `ra3d/terrain.pack`, enlarged. Texset 1, and
  Interior (no Enhanced set), draw the 1995 tiles at 24 texels a cell. Variants are
  chosen per placement by the rule Tiberian Dawn's cliffs use (`enh_variant_pick`).
- **The join blend.** `ra3d/terrainseam_RA_<TH>.pack` (`C3DRASEM`) strips are laid by
  the drawing code now shared with Tiberian Dawn (`ts_strips_begin`, `ts_strip_emit`,
  `ts_strips_end` in `game/terrain_seam.h`) under the same rules: the right or lower
  piece's strip over its neighbour, one direction per join, a feature's strip never
  over a cliff, per variant, none on the clear ground. `OPENCNC3D_TERRAINSEAM=off` turns
  both games' strips off.
- **Proof.** Tiberian Dawn's Enhanced frames (GDI 4 at its south wall, GDI 1) and Red
  Alert's Classic frames are byte-identical before and after the change. G310 asserts
  the sheet, the theater, the strips drawn and switched off, the objects still drawn the
  same over either set, the temperate and interior theaters, the Visuals dialog route,
  and that Classic reads none of it; a build drawing the placeholder again fails every
  ground claim. The shots so far use enlarged 1995 tiles: the paintings are still being
  made.
- **Gaps**: those listed as open under Phase 10 above. Absent on Win98
  (`docs/tier1-gap.md`).

### Red Alert's ground kept on the GPU

Red Alert's Enhanced ground was drawn in immediate mode, every corner of every cell in
view, every frame, in each pass that draws it; Tiberian Dawn's has been kept on the GPU
since its own cache (`game/terrain_cache.h`). Red Alert's now is too.

- **What is kept.** Per sheet (a mission, a set), in GPU buffers: the ground (one record
  per map cell), the sea floor (one per wet cell, with the texture coordinates the sea's
  source was built with), every join strip of both passes with an index list, and the
  normal pass's colours. Each cell's light (corner shade times the shroud's corner light)
  is recomputed once a frame for the cells drawn and sent again only when it changes, to
  its ground, its floor and the strips it lays; the cells a pass admits go down in runs, in
  the loops' order. The reflection pass uses the same buffers. Rebuilt with the sheet and
  whenever the heights, the variants switch or the map rectangle move; the loops remain
  for a driver without buffers and `OPENCNC3D_TERRAIN_CACHE=0`.
- **The sheet's first build** gives the sea's soft coast only to the wet slots the map
  draws: 308 to 151 ms on Allied 5, 311 to 83 ms on Allied 1.
- **Measured** (2560x1440, a player's settings, old and new alternated): the ground's CPU
  section on Allied 5 0.75 to 0.33 ms (of which 0.21 ms is the sea's surface, still
  immediate mode in water_mod.h), the live frame 5.94 to 5.40 ms; Soviet 11 1.02 to
  0.33 ms; the cold first frame about 100 to 250 ms shorter. Numbers:
  `docs/perf/2026-10-16-ra-ground-cache-metrics.json`.
- **Proof.** 42 Red Alert Enhanced shots (snow, temperate, interior, the 1995 set, the
  shroud on, off, switched and darkened, the sea off and on, the set switched at run time)
  and 27 Tiberian Dawn shots byte-identical old against new; Red Alert Classic differs
  only in its clock-seeded ore. Gate G334: the buffers against the loops in one binary
  (nine pairs byte-identical, each step shown to move the picture), and the instrument
  `OPENCNC3D_RA3DCACHE_VERIFY=1` checking every drawn record against the per-frame
  computation over a played stretch with the shroud moving; a build that never refreshes a
  cell's light fails both. G310 and gate-ra-3d.sh pass; G311 fails identically with the
  old renderer (its water_fx 0 control moves between two ticks; not traced).
- **Gaps**: the sea's surface (`water_fx_draw`, both games) is still immediate mode; each
  cell in view still has its light recomputed every frame, only the sending is saved;
  the terrain-shadow target is not kept for Red Alert; the cold frame's first sun map and
  first sea field are untouched. Windows cross-compiled, not run.

### Catching up with Tiberian Dawn's Enhanced

Work Tiberian Dawn's Enhanced gained after Red Alert 3D landed, brought to the module,
each a commit of its own with pictures before and after on Red Alert maps (snow and
temperate) and Tiberian Dawn's frames byte-identical (GDI 1, GDI 4 and Bad Neighborhood,
Enhanced and Classic, at tick 0 and after 90 ticks):

- **Ships can be selected**, in Enhanced and in Classic: the renderer had no kind for the
  brain's VESSEL heap (K_VESSEL now). A gunboat on Allied 7 is selected and moves.
- **The brain's selling-factory guard** (Update_Buildables offers nothing for a factory
  being sold), ported from Tiberian Dawn. Red Alert's house scan already skipped such a
  building, so play is unchanged; `tools/regressions/sale-options/test-sale-options-ra.py`
  compiles the real function against counters. The bootstrap gate
  (`tools/ra3d/gate-ra-bootstrap.sh`) fails on the brain both before and after this pass;
  the mission and construction gates pass.
- **Loading**: shaders are compiled and standing buildings' textures uploaded before the
  first frame; Allied 5's first frame (2560 x 1440) falls from about 1.25 to 0.81 s, the
  rest being the sea's and ground's first build and building textures past the shared
  preparation budget. The steady frame is unchanged (about 5.6 ms); the terrain sun cache
  reuses the ground's depth from the second frame on.
- **Presentation**: the bolt, tree anchors, tread marks, blood and brass, cliff
  softening, footsteps, drive sounds and the temperate bed, as listed under Phase 10.
  Script verbs `ra3dprobe`, `ra3dzap 0|1` and `ra3danchor 0|1` show what the sprite pass
  drew and restore the old drawing for a gate's control.

### The sprite cards stand on their own feet

Every 1996 sprite drawn in the 3D modes (all of them in the hidden N64 mode; in Enhanced
the vehicles, ships, aircraft, the snow trees, the barbed-wire fence, the barrels and any
building going up, being sold or with its door open, everything without a model) now stands its lowest drawn row on the ground where the 2D game lays that row
(`ra3d_stand`, `game/ra3d_sprites_mod.h`), its parts on its own card. Before, a card was
centred on its point and its lower half hidden under the terrain (a tank showed 0.45 of
itself, a gunboat 0.17), and a building card stood at its footprint's centre row. G335
claims 7 and 8 measure both with occlusion queries and the footprint grid (`ra3dcardvis`,
`footdump`, control `ra3dstand 0`). Measured at the same time: the camera and the soldiers
are already Tiberian Dawn's exactly; the Enhanced building models are larger than Tiberian
Dawn's and than their 1996 sprites; their size is left as approved and is an open art
decision (the scale would go into the structures bake). Tiberian Dawn byte-identical (GDI 1, GDI 4; Classic,
N64, Enhanced; tick 0 and 90); Red Alert Classic byte-identical on the seeded skirmishes.

### The art Red Alert shares with OpenCNC3D

Under Enhanced, every asset the Red Alert lab approved as reuse of an OpenCNC3D remaster
is drawn from that remaster (gate G338, `game/gate_ra3dreuse.py`):

- **Civilian buildings** V01 to V19, BIO, HOSP and MISS stand as their models with their
  turns, in every outdoor theater, and play all their stages: worn chip by chip, broken at
  half strength (a house first seen hurt is shown as it would be), blown apart, sunk and
  gone over their scorch, the engine's death craters held back under it. The oil pump's
  beam runs on the engine's own idle stage.
- **Trees** T01 T02 T03 T05 T07 T10 T12 T13 stand as their models on temperate maps, at
  the engine's trunk point, with their burning stage and charred snag; snow keeps its 1996
  frosted trees (no reused tree has a winter look; the temperate ones read wrong there).
- **Walls and fences** (SBAG, CYCL, BRIK, BARB, WOOD) are their connection pieces, read
  from the engine's map with their damage levels, and wear down and wreck cell by cell.
  The steel crate and the field overlays are their models.
- **Craters and scorch marks** are OpenCNC3D's remastered decals (snow with a grey burn).
- `tools/ra3d/stage-reuse.py`, called by `stage-structures.py`, copies the approved files
  from `game/ebld` into the module's `ebld/` with a manifest of their hashes. Script verb
  `ebld 0|1` switches the remasters inside one run for a gate's A/B.

Still short: no reused tree or building has a snow look (snow villages have bare roofs);
Red Alert's 3D pass drew no particles, so a burning tree had no smoke and a dying house
kept the 1996 explosions (since drawn: "OpenCNC3D's effects in place of the anim cards" below); Red Alert's own crater drawings, FENC, the wooden crate and
the trees not approved for Red Alert keep their 1996 art, so a wood mixes models and
cards; the Technology Center's model is half its sprite's width.

### The engine under the menu's buttons

Branch `ra-brain-parity`, landed on main a commit per step, each with its gate. What a menu
calls: `ra_skirmish_maps`, `ra_skirmish_defaults` and `ra_skirmish_apply` in
`app/opencnc3d.cpp` (the map list with a picture each, the seats and options, the start),
and for Load the existing `game_load_menu_open` and `load_slot_prepare`, which already
serve the module's own saves.

- **Brain parity**: the lockstep order wire, the seed, rally points, the AI takeover and
  blow-up, the cheat switches, wall fill, the proximity query, the build potential, trigger
  queries and the power switch, ported from the Tiberian Dawn brain into the Red Alert
  fork and its patch; a lockstep match resets its frame and queues at start and reseeds
  from the agreed seed. `tools/ra3d/gate-ra-lockstep.sh` (CI).
- **The object dump** carries Tiberian Dawn's remaining 17 fields (cloak, cstage,
  poweroff, repairing, wrench, pips, harv, primary, flashme, act and the rest) with the
  same meanings; the radar paints each Red Alert house in its own colour.
- **Saves** are per game: Red Alert's in `ra3d/` inside the per-user folder, each record
  tagged with its game, older untagged Red Alert records told apart by their pack; a slot
  from another mission hands over to the shell. `tools/ra3d/gate-ra-save.sh`.
- **Skirmish**: countries, eight colours, tech 1 to 10, ore and shroud regrowth, up to
  seven computers, any of the 24 maps with a picture each; thirty minutes identical on one
  seed with the computers building and fighting. `tools/ra3d/gate-ra-skirmish.sh`.
- **Network**: two copies over loopback through the command-line handshake, people and
  computers, five minutes in lockstep; the lobby room is still Tiberian Dawn's.
  `tools/ra3d/gate-ra-net.sh`.

- **The room** (later the same day): a network room in the Red Alert module plays Red
  Alert: its maps, a country and colour per seat, computers in seats, its rules and tech
  level, by address, off a game list and through a relay (`tools/ra3d/gate-ra-room.sh`).
  The wire (version 13) carries the room's engine, the match seed, shroud regrowth,
  capture the flag and the computers' difficulty.
- **The seed**: every network match starts from the host's seed on every peer.
- **Computer difficulty**: easy, normal and hard, Red Alert's skirmish slider; the brain
  gives a computer player the computer's handicap.
- **The debrief**: a Red Alert match ends on Red Alert's score data (battles won, kills per
  game), shown in a notice until the menu's screen draws it.

Still short: no screen calls any of it yet (the room's screens are Tiberian Dawn's); the
LAN browser was not driven with Red Alert; nothing of it has been run on Windows. Tiberian Dawn's pictures byte-identical before and after (GDI 1, Nod 1, GDI 4; N64,
Enhanced, Classic) and its lobby, lockstep and save gates green (G54, G194 to G201, G205,
G237, G238).

### The buildings at OpenCNC3D's size

Every Red Alert building model stands on its cells as OpenCNC3D's remasters stand on theirs:
one uniform factor per type about the footprint's centre on the ground, never grown
(`art/remaster-ra/scale.json`, measured by `tools/art/remaster/ra/scale_fit.py` against the
C&C building of the same kind, or against OpenCNC3D's buildings of the same footprint for
Red Alert's own, the 1996 sprite as the check), baked into `game/ebld_ra` and shown in the
lab by its As OpenCNC3D size switch. 22 types shrink (Construction Yard 0.803, Power Plant
0.886, War Factory 0.855, SAM Site 0.593, Service Depot 0.564...), 7 stay 1. G335 claim 9
asserts the drawn sizes. The rule and the 1996 sprite still differ by more than 0.1 on six
types (the Service Depot most: Tiberian Dawn's Repair Facility is a small pad, so Red
Alert's halves; also the Ore Refinery, Soviet Barracks, Tesla Coil, Gap Generator and
Naval Yard), kept to the rule and open to a look in the game.

### OpenCNC3D's effects in place of the anim cards

Under Enhanced effects every Red Alert engine anim with a Tiberian Dawn counterpart is
drawn as that anim's OpenCNC3D effect, through Tiberian Dawn's particle recipes, remastered
art and Enhanced additions, and its 1996 card stands down (`game/ra3d_effects_mod.h`, gate
G342): explosions and hits, fires and burns, smoke trails, the cannon, chain-gun and SAM
fire, a dying structure's single rebuilt blast and smoke column at its plot's middle, a
damaged structure's roof plume, burning trees' smoke, the Tesla bolt's glowing ends, the
modelled riflemen's flashes. Effects riding a card start where the 2D picture puts them on
it; fires on a model burn on its roof. The Red Alert brain prints EFX|FIRE and EFX|BULLET,
and projectile cards fly at their height. Kept as cards: pickups, corpses, parachutes,
electrocutions, water splashes, the move flash. Idle effects of Red Alert structures (the
power plant's smoke, the Flame Tower's pilot flames) are not drawn yet: they need the
structure bake to carry the lab's emitters into the game's structure files. Tiberian Dawn
byte-identical, battles included.

### Snow looks for the shared villages and trees

- Snow villages and trees: Red Alert's snow draws OpenCNC3D's winter looks of the reused
  civilian buildings (V01 to V18) and trees (T01 T02 T03 T05 T07 T10 T12 T13), one file each
  shared with Tiberian Dawn's WINTER theater (no `_SNO` of their own). The 1996 snow sprites
  and the 1995 winter ones are drawn alike, measured side by side. stage-reuse.py stages each
  `_WIN` file beside its summer file; G338's claim (5) now asserts them. The pilot (Church,
  Ricktor's House, T01) approved in review, the rest by the same method; the broadleaf trees bare
  and frosted as both games' winter art draws them.

### The Ore Truck and Red Alert's own trees

- **The Ore Truck** (HARV) has a model of its own, its 3D concept approved: a six-wheeled
  dump truck (the 1996 sprite's layout and house paint, the Remastered cameo's detail, the
  approved Harvester's finish, tyres and scale), its charred stage and its wreck, its
  lights; the Red Alert lab's row vehicles/HARV, rebuilt by
  `tools/art/remaster/ra/run_HARV.sh`, baked into `game/evhc_ra/HARV.evhc`. It declares its
  two motions as data (painter.moving: the scoop's swing over the cab, the dump body's
  lift and tip) and the ore load as a part of its own; the game draws it at rest until the
  engine drives them.
- **Trees**: T11, T15 and T17 are Red Alert versions of OpenCNC3D's approved trees in the
  1996 palette (greyer, darker, a little olive, measured per tree by
  `tools/art/remaster/ra/tint_tree.py`), T14 a version with its crown filled out as the
  1996 art redraws it (`build_broadleaf.py` row T14RA), recoloured by the palette shift
  measured on T11 (T14's own crown is redrawn, so its own pixels compare different leaves
  and gave a shift dark enough to lose the tree against the grass); their winter looks by
  the winter method (T11, T15 and T17's bare winter trees come out byte for byte the
  Tiberian Dawn ones). T16, T08's temperate look and the five clumps are made as one model
  for both games, their concepts approved, in review in both labs.
- **The versions' game files**: `tools/art/remaster/ra/bake_versions.py` bakes them into
  `game/ebld_ra_versions` under the names of the type they stand in for, keeping only the
  files a version changes (12: the four trees' temperate looks and burning stages, T14's
  snag and winter look); `tools/ra3d/stage-reuse.py` stages a version once its row is
  approved, the unchanged files (a recoloured tree's snag, the bare winter looks) from
  `game/ebld`, and `--include-review` stages the ones in review for a look. Measured with
  all four approved in a test copy of the reviews: G338's five runs hold every claim, 14 of
  21 trees of Allied 5 drawn as models where 9 were, and on snow T14, T15 and T17 drawn from
  their winter files.

Since then (17 October): the four versions and their winter looks are approved and staged,
and T16, T08 and the five clumps are approved, baked and drawn in both games, each clump
whole from its own model at its centre coordinate (gate G344; G338 now counts every tree of
Allied 5 a model, 21 of 21), their winter looks approved and drawn on snow (31 trees of
Soviet 1 as winter models). Still short: the Ore Truck is being reworked in review.

### OpenCNC3D's vehicles and aircraft

Under Enhanced a Red Alert unit or aircraft whose type has a remastered model staged in
`ra3d/evhc` is drawn, moved and broken apart by OpenCNC3D's vehicle system
(`game/evhc_mod.h`) instead of its 1996 card: tracks, turret, recoil, lamps, rotors and
discs, the sun's shadow, the light pass's normals, the pick volume, the wreck, a craft
shot down in flight, in Red Alert's side colours. The seven reused types (ARTY, TRAN,
HELI, STNK from the same art; 1TNK, APC, 2TNK by choice) are staged from the lab's reuse
rows with their headers renamed; the Wood Crate and tree T06 joined the reused cell
decorations and trees. Gate G343. The brain's dump now says a unit's door (the APC's
door opens for a passenger) and the Ore Truck's harvest and dump frames, which drive its
scoop, dump body and ore load (baker and runtime drivers 9 to 11). A build stages approved
art only; `stage-structures.py --include-review` stages what is in review for the test
folder. Open: ships, every type without a file.

### Red Alert's own vehicles: the concept stage (16 and 17 October 2026)

The eighteen vehicles, aircraft and ships whose 1996 art Red Alert does not share with an
approved OpenCNC3D vehicle go to a real model through a Phase 0 proxy and a 3D concept, both
reviewed in the Red Alert lab (Vehicles), by the method in `tools/art/remaster/ra/VEHICLES.md`.
A first round of simple box proxies and concepts made from them was rejected for all eighteen;
the method was redone on three pilots: each proxy now modelled after the Remastered
Collection's in-game sprites and the 1996 frames in its real forms, its sizes fitted to the
art's silhouettes and checked at every facing against the best any shape can score at the
art's pixels (the 1996 model's own carved hull), and the concept taking its design from the
Remastered and 1996 art and its finish from the approved OpenCNC3D redesigns. State: the V2
Rocket's, the Gun Boat's and the Heavy Tank's concepts are approved and their real models are in
review; the Ranger, the MCV and the Transport, the five aircraft (the MiG, Yak, Hind, Badger and
Spy Plane, each built after the real aircraft its art is drawn after) and the Supply Truck, the
Mine Layer, the Gap Generator, the Submarine, the Destroyer and the Cruiser have their faithful
proxies and new concepts in review; the Radar Jammer's row proposes OpenCNC3D's approved Mobile
HQ, whose art it is pixel for pixel. The game draws none of them yet; an own model stages
from `game/evhc_ra/<T>.evhc` once its row is approved (`tools/ra3d/stage-vehicles.py`).

## Decisions of 2 October 2026

These answer the four questions the audit raised.

1. **Forking is allowed.** RA3D may have its own brain forked from the Red Alert source.
   The end goal is a Red Alert 3D module, a new version of Red Alert made the way OpenCNC3D
   remade Tiberian Dawn, which the player adds from the launcher with the "+" button. The
   classic TD brain stays as it is.
2. **Core features are shared.** RA3D must also support big maps. Capabilities like that
   are meant to be shared between the two modules in the end, not reimplemented in each.
   The XL brain's big-map work is therefore an input to the RA brain, not a separate path.
3. **Red Alert data source: the original 1996 discs.** Done: `data/radata`. Red Alert was released as freeware
   by EA in 2008 and its source is open. The data is extracted from the Allied and Soviet
   disc images of that release, and the extracted data is committed under `data/` like
   the Tiberian Dawn data. The OpenRA content folder is no longer the source.
4. **One design with WarGames and All Stars.** All Stars mode, the cross-module mixed
   matches, is on the back burner. When it comes, every module the player has installed
   must work in it, so the module, identity and content model is one design for OpenCNC3D,
   RA3D and the WarGames port.
