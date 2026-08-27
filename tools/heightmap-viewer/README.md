# C&C 64 map viewer and editor

A browser tool for looking at every heightmap the Command & Conquer Nintendo 64
cartridge ships, in 3D, one map at a time. Built to answer the question in
`docs/terrain-elevation-grammar.md`: what the elevation is made of and what it
follows.

* orbit / pan / zoom **100 maps**: the cartridge's 79, plus the 16 SCM multiplayer maps
  and 5 SCJ dinosaur missions from `GENERAL.MIX` on the MS-DOS CD, which the N64 never
  carried. DOS maps are drawn with the cartridge's own tile art by inverting the theater
  TL4/TL8 tables; see `export_dos.py`
* **the 3D asset layer**, on by default: every object each mission's INI places, with
  the game's own TEXTURED meshes. Geometry, UVs, per-vertex colours and textures all
  come out of a baked mission pack (`playable/<SCEN>.pack`), which is exactly what the
  shipped game renders; measured, all 293 textures and all 220 meshes in a pack are
  identical across every pack, so one pack is the whole global asset set. The renderer
  is unlit because the cartridge is: GL_LIGHTING appears nowhere in `cnc_eyes.cpp` and
  the baked per-vertex colour IS the shading. Triangles are split by the cartridge's
  own TriMode so shadow and translucent faces blend instead of drawing as solid shards.
* **the real sea**, on by default. Not a plane: the water re-emits the terrain's own
  corner vertices for every cell whose tile art punches an alpha hole, with the sea
  floor under it and WATER1 x WATER2 modulated over it at alpha 170/255, scrolling and
  warping on the cartridge's own rates.
* **tiberium** from the cartridge's ANY_TI filmstrips, at the growth frame the engine
  itself would compute (`CellClass::Tiberium_Adjust` counts the eight neighbours).
* **wall snapping**: each wall cell takes its connectivity mask from the engine's rule
  and then its piece and its turn from the cartridge's own 16-way table, so four wall
  cells in a square make a square.
* **infantry as the cartridge's own sprites**, not markers: one camera-facing billboard
  per man out of the pack's sprite strips, fw/24 by fh/24 cells, picking its strip row
  from its facing and mirroring for the eastern octants because the strips store only
  the western half. The tan and blue-grey uniform sets follow the house.
* **graded building pads**, on by default and toggleable. A building placed on a ramp
  otherwise has one corner buried and another in the air, because the 1995 engine has
  no height. The cartridge's artists levelled the ground under a building by hand (169
  of 211 pre-placed buildings stand on corners within one height unit), and the
  renderer generalises that: every building levels its own footprint to the HIGHEST
  ground it stands on, taller pad winning on shared corners. This is the renderer's
  choice, not the cartridge's, exactly as `cnc_eyes.cpp:822` describes it. Buildings, trees and rocks, vehicles, walls, tiberium and
  craters, each group separately toggleable. Nothing here is re-derived: the transform
  (`MODEL_SCALE` 1/1024, facing `-face*2pi/256`, buildings and terrain drawn unrotated,
  units biased +128 and BOAT +64) is copied from `game/cnc_eyes.cpp`; the anchors come
  from `BuildingClass::CenterOffset`, `TerrainTypeClass::CenterBase` and
  `StoppingCoordAbs` in EA's GPL source; and each wall cell picks its piece and its turn
  from the cartridge's own 16-entry variant table. The 20 real wall meshes are read out
  of the ROM display lists, because the ones named SBAG/CYCL/BRIK/BARB/WOOD in
  `models_full` are the bullet models, not walls
* filter the map list by mode (Singleplayer / Multiplayer / Spec Ops / Covert Ops /
  Dinosaurs / Bonus / Unused), side, media (Cartridge / DOS CD) and elevation. Chips
  inside a group OR together, groups AND together. Every bucket is read out of the
  cartridge's own two scenario pointer arrays (ROM 0x21b260 and 0x21b280), not guessed
  from the file name: see `rom_rosters()` in `export.py` and section 0 of
  `docs/terrain-elevation-grammar.md`
* mission names. Where the data has them they come from the data: 29 of 100 carry a
  real `[Basic] Name=` (the 13 Covert Operations titles the cartridge ships, and all
  16 SCM multiplayer maps). The four N64 exclusives and the five Funpark missions have
  no title in any file here, so those nine take the community record's names
  (`GDI/Nod '99 Special Ops M1/M2`, `Funpark mission 1..5`) and the exporter says in
  its own comment that they are external, not cartridge data. M1/M2 is positional,
  from the order of the ROM's SPECIAL OPS pointer array. Note that SCB21EA's INI still
  carries the Covert Ops title "Eviction Notice" it was cut from; the cartridge does
  not use it that way, so the Spec Ops name wins. The 51 campaign missions have no
  title anywhere and the cartridge labels them by number, so the viewer does too, in
  italics, and shows the real briefing from `ENG/MISSION.ENG` underneath
* **Texture** (the cartridge's own tile art, the default view), **White** (unlit
  albedo, shading only, for reading the shape), **Tile type** (each cell coloured
  by its PC TemplateType family, which is what makes the cliff-tile correlation
  visible), **Tiers** (coloured by the 0 / 64 / 128 / 191 / 255 rungs)
* wireframe overlay, playable-border overlay, a movable water probe plane
* a vertical-scale slider; **1.0x is the cartridge's true geometry** (height byte
  x4 world units, cell pitch 256, so 64 bytes of height is one cell width)
* hover readout: cell, template name, all four corner heights, the drop across
  the cell

## The editor

Press **E**, or the *Edit this map* button. The plan and the research behind it are in
`docs/design-map-editor.md`; this is what is built so far.

The loop is **arm, point, click**, not drag and drop: you click a palette item to arm it,
a translucent ghost of the real mesh follows the cursor snapped to the cell grid, and a
click stamps it. The brush stays armed so you can place ten in a row, and Esc disarms.
Dragging is reserved for MOVING something already placed, which is the split every C&C
and RTS editor has converged on. Tiberium and walls paint along a held drag.

* **Ownership** is a four-colour selector, not a per-object dropdown: the palette and the
  live ghost recolour the instant you switch, and Tab cycles. Four slots is all the data
  needs; no shipped map uses more than four houses.
* **Validity is per cell.** Each footprint cell is green or red, and the red draws OVER
  the ghost so it punches through the art rather than hiding behind it.
* **The no-go overlay is not our opinion.** It is `Ground[LandType].Build` out of EA's GPL
  source, keyed on each cell's (template, icon) with the AltLand exception list that makes
  11 of the 38 cliff templates walkable. That is why beach and tiberium read as passable
  but unbuildable, and it never consults height. `export_editor.py` ships the table.
* **Footprints include the bib.** 17 of the 66 building types lay a dirt apron and occupy
  Width x (Height+1); without that a Construction Yard looks legal here and is refused in
  the game.
* **Everything is undoable** (Ctrl+Z, Ctrl+Shift+Z) and autosaves to local storage per map.
  *Revert map* throws the lot away.
* **Check and save** runs a linter and writes the mission `.INI`. Every failure it catches
  is otherwise completely silent: a map with no pack does not error, it simply never
  appears in the menu; an illegal tile is quietly turned to clear; nothing cross-checks a
  borrowed pack's theater against the INI's. It shows you the file before you take it.

Terrain painting and elevation are not in yet, and `docs/design-map-editor.md` section 4
explains why elevation in particular is not a matter of writing a brush: heights cannot
reach the game at all today, and the nine converted skirmish maps are flat in-game as a
result.

## Building the data

```
python3 export.py
```

reads `tools/bakery/sharecopy/assets/extracted` (the ROM's own files), `data/rom`
(the scenario rosters and the briefings) and `data/dosdata/GENERAL.MIX` (the DOS
maps), and writes `public/data/`: one small JSON per scenario (65x65 heights, 64x64
atlas slots, 64x64 template ids, the placed objects) plus the two theater tile
atlases and `models.bin`, every named N64 mesh as a 96 KB int16 triangle soup.
About 4 MB for all 100 maps. `export_assets.py`, `export_dos.py`, `export_pack.py`,
`export_editor.py` and `export_dosinf.py` are its helpers and each carries its
format notes at the top. `export_dosinf.py` is the one that does not come from the
cartridge: it bakes the 1995 MS-DOS infantry sprites out of `data/dosdata/CONQUER.MIX`
with `TEMPERAT.PAL` from `LOCAL.MIX`, reusing the game's own `bake_dosinfantry.py`
tables so the browser and the game cannot disagree. Run it alongside `export.py`:

```
python3 tools/heightmap-viewer/export.py
python3 tools/heightmap-viewer/export_editor.py
python3 tools/heightmap-viewer/export_dosinf.py
```

Without the third the viewer falls back to the cartridge's five-facing billboards
rather than losing its infantry. Nothing in `public/data` is committed, for the same reason no baked pack is:
it is regenerated from `data/rom` by one command. See `DATA.md`.

`public/vendor/` is three.js 0.185.1, also not committed. Restore it with:

```
npm pack three@0.185.1 && tar xzf three-0.185.1.tgz
cp package/build/three.module.min.js  public/vendor/three.module.js
cp package/build/three.core.min.js    public/vendor/
cp package/examples/jsm/controls/OrbitControls.js public/vendor/
```

## Putting it on cnc3dgame.com/maps

```
./install-into-site.sh /path/to/cnc3d-web
```

copies `public/` into the site's `public/maps/` and prints the one `next.config.mjs`
change `/maps` needs (a redirect to `/maps/` plus a rewrite to the static page). The
viewer carries its own trailing-slash redirect as a fallback, so it works mounted at
any path, root or subdirectory.

## Running it

```
python3 -m http.server 8931 --directory public
```

then open <http://localhost:8931>. It is a static page; there is no build step.

## A note on what is ours and what is the cartridge's

Heights, tile ids, template ids and tile art all come from the ROM. Two things on
screen do not, and both say so in the UI: the flat colour behind the alpha holes
in water-capable tiles (the console draws its own water surface there and no
water colour exists in the terrain palettes), and the water probe plane, which is
a ruler the viewer supplies, not a height the cartridge stores.
