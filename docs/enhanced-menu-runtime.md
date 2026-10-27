# Enhanced menu runtime integration — 11 October 2026

The accepted menu v06 and authorized integration into current main and Dock delivery. Native RGBA chrome preserves the reviewed source corners, guard rails, bevels, grain, button states and original font shadow classes. Scale follows the HUD integer density, with fit checks for the wider lobby. CPU composition and GL 1.1 nearest presentation remain separate from game state.

Enhanced Tiberian Dawn uses these screens. Enhanced Red Alert uses the same screens in its own kit (`ra3d/enhanced-menu.pack`, baked by `menu/bake_ra_enhanced_menu.py` from the 1996 data; design `art/interface/ra-enhanced-menu-v01`), chosen once per process by `em_set_red_alert`. Classic keeps the original menu paths for both games. The separate RA executable must still be refreshed from the combined build. Existing controller code owns settings callbacks, save/delete/load, mission identity, faction/team/colour/start constraints, host permissions, real chat and network readiness. The Database now shares the Enhanced menu housing and actual installed Enhanced model importers; see the v08 amendment below.

`menu/gate_enhanced_menu.cpp` is a standalone hidden controller/render test. Its in-memory test states are not shipping content. End-to-end evidence must also come from the deployed executable in the Dock installation, and installation hashes/saves/settings must be checked before handoff.

## User amendment: native resolution and accepted trees

Supersampling is removed from every Visuals mode list and native Display controls.
Defaults, preset loading and Visuals callbacks use 1x. The old stable enum remains
for compatibility, with no visible or keyboard-selectable row. Current Lab source
has the same amendment; archived accepted v06 artwork is preserved exactly.

`3D Trees` now selects the accepted remastered Txx terrain models (including trees
composed into clumps) for every render pass. Off falls back to the cartridge models.
Old settings predate this meaning, so `enh_tree_toggle` migrates that unrelated old
setting once and preserves subsequent explicit choices. Existing files need no rewrite.

The imported `data/trees/tree3d.pack`, its default loader/CLI path, per-tree renderer
and instance were removed. The shared mesh/atlas reader was retained as
`foliage_mesh_pack.h`: scattered bushes and rocks still require its format, shader
and buffer utilities, and their authoring tools import the shared baker helpers.
Those shared utilities are not an installed legacy tree asset.

## User amendment: shared Special Ops mission categories

Enhanced Special Ops now offers only Covert Ops and Special Ops. Each category lists
GDI missions before Nod, retaining each original mission index and pack pairing.
Test Maps is excluded from display, paging, Home/End and keyboard navigation.
Left/Right switches categories; mission selection and Play use the original controller.
Classic keeps its original list. User Maps remains independent.

The 28px row emblems reuse the website's exact FBX geometry, normals, UVs and embedded
32px textures: GDI 908 triangles, Nod 717. Gold is warmed to the requested orange;
Nod stays red and silver/black remain intact. They rotate at 0.2 radians/sec, one
revolution in about 31 seconds. Hidden captures use a fixed readable angle.
`art/interface/enhanced-menu-logos-v01/manifest.json` records source hashes, measured
bounds, texture/UV pairing and intentional all-black player masks for decorative icons.
No new mesh design was created. The original website FBXs and accepted v06 artwork
are preserved unchanged. The v07 review delta is archived separately.

Verification: Lab preflight 97 layouts / 1186 controls; native source identity,
category filtering, faction ordering and keyboard checks; animation witness changes
421 GDI and 355 Nod pixels with zero changes outside their seats. Hidden installed
captures show the real 14 Covert Ops and 7 Special Ops missions. Imported tree pack
is absent; accepted trees switch off/on with exact restoration (14/0/14 draws,
2139 changed pixels). TD and separate RA hidden game/menu round trips, HUD G318 and
fog G17 checks are recorded with deployment/preservation hashes at delivery.

## v08: Database and website faction logos

The Enhanced Database uses the native 1216 × 636 lobby housing, original bitmap
font with black shadow, quiet inset wells and hard metal bevels. Four categories
filter the real CODEX table; GDI and Nod buttons include rotating website emblems
beside their labels. The roster, model well and Info / Stats / Matchups / Tech tabs
retain faction identity and original engine data. Tech shows unlocks. Opening the
screen pauses the world and ducks music; closing restores the HUD.

The model viewport resolves the same installed EBLD, EVHC and Enhanced C3M assets
as the battlefield, with actual materials and faction colours. It frames opaque
geometry without shadow planes and uses independent GL state. All 50 roster entries
were audited: 49 have installed Enhanced models. MLRS (S.S.M. Launcher) has none;
the screen explicitly says “Enhanced model not installed” rather than substituting
cartridge geometry. No mesh, UV, texture or player mask was authored or modified.

The main menu also displays the exact website GDI and Nod meshes in its two outer
lower seats. Both main-menu emblems and faction-button emblems rotate at 0.2 rad/sec.
Hidden capture angles are fixed for repeatable visual checks.

Versioned source and captures are in `art/interface/enhanced-database-v08/`.
Native animation witnesses prove changes in both logo seats and zero changes outside
them. Browser preflight covers 386 layouts and 7051 controls; all 71 faction/model
captures stay inside the model well. Hidden Dock checks cover both module roundtrips,
G147 data/frame/classic exclusion, G318 queues, G17 fog and Enhanced tree restoration.
The Lab uses static actual-engine model captures; the runtime models rotate.
