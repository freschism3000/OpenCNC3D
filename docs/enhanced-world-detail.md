# Enhanced world detail

Both games default to 3D Trees, 3D resources (Ore in RA), Bushes, Rocks and Ground
Patches. The menu uses the same live fields as rendering and no longer replaces
RA controls with unchecked, disabled seats. The existing native component art,
424×36 checkbox seats, labels and hit geometry remain unchanged.

Older saves adopt these five Enhanced defaults once via `enh_world_detail`.
New saves write the marker, so every subsequent on/off choice survives reopening
and restarting. Classic/N64 profiles and all other saved dials stay separate.

RA detail placement reads its native exported map and selected template slots,
not the TD donor pack: CLEAR1 allows plants and patches, S/B/WC/RC mark rock
faces, and water/shore cells exclude scatter. Buildings, trees, resources,
walls, crates and smudges retain their existing placement exclusions. Plants
and grass patches use the Temperate material only; snow retains its own rock
bank and does not grow Temperate shrubs or moss. All available controls remain
editable regardless of the current theater.

Approved TC01–TC05 composed tree files must stage alongside T01-style trees.
Rejecting TC rows as “not a single tree” removed their models and restored the
engine's sprite fallback despite the 3D Trees switch being on. The staging
contract retains approved clumps in both theater looks; it does not approve
new art or convert unapproved concepts to runtime models.

Validation must include actual RA rendering counters and on/off restoration,
menu mouse clicks, Save/Load, both Dock locations, and fog/HUD preservation.
The menu's existing CPU render gate also clicks all seven implemented RA
world/interface switches and checks their real seat values.

## Verified delivery — 20 October 2026

Current-main build `0.8.7+2f719e2-dirty` is deployed to both existing Dock
installations. Three installed RA mission walks pass in each: SCU02EA, SCG01EA
and SCG05EA. Tree-model counts switch 34→0→34, 22→0→22 and 35→0→35;
in the Temperate fixture all 34 sprite cards stand down while tree models are on.
The ore fixture switches 120 model cells→0→120 and 0 ore decals→156→0.
Bush/rock scatter and ground patches likewise draw, disappear and restore.
All seven menu switches save their actual values, reload opt-outs and preserve
display preferences. The source-alpha-correct HUD matrix passes six cases; the
native menu gate passes every screen including 90 RA captures. Settings, saves,
launchers, fog and the existing HUD remain intact. See
`docs/verification/2026-10-20-enhanced-world-detail/runtime.json` and its actual
runtime images. These checks confer runtime validation, not new asset approval.
