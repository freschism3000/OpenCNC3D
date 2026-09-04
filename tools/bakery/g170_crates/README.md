# G170: the 3D goodie crates

The cartridge draws a goodie crate as a MODEL. We drew a flat quad of the 1995 DOS
sprite. This bundle is the bake plus the draw plus the gate that keeps them honest,
prepared and measured outside the working tree so nothing here was written into a
contended file.

Everything below was built and run end to end before it was written down: the two meshes
bake, both cubes reach the glass in temperate and in desert, an old pack still draws the
1995 sprites byte for byte as it did, and the gate has been made to fail twice, once by
removing the mesh from the pack and once by disabling the cube pass in the renderer.

## What the ROM says

    cell-decoration draw table   RAM 0x8020FF58, 39 records of {mode, descriptor}
      record 34 -> RAM 0x8020FF48 = the two s32 {88, -1}    body 88, no shadow set
      record 36 -> RAM 0x8020FF50 = the two s32 {90, -1}    body 90, no shadow set
    model slot = type id + 10     ->  slots 98 and 100
      slot 98  node RAM 0x801B775C  dl 0x011D4C8  8 verts, 10 tris   grey steel
      slot 100 node RAM 0x801B6930  dl 0x010F3C8  8 verts, 10 tris   olive wood

Ten triangles and not twelve: the unseen bottom face is left off, the top face is there.
Both cubes measure 185 x 185 x 184 mesh units, which is 0.18 of a cell on a side at the
renderer's cell of 1024 model units.

The two OverlayTypeClass registrations name themselves. Read straight off the calls:

    jal at ROM 0x195A18   a1 = 0x1D = 29   a2 -> RAM 0x801C8630 = "SCRATE"   a3 = 0x22 = 34
    jal at ROM 0x1959C4   a1 = 0x1C = 28   a2 -> RAM 0x801C8628 = "WCRATE"   a3 = 0x24 = 36

29 and 28 are OVERLAY_STEEL_CRATE and OVERLAY_WOOD_CRATE counted from the engine's own
enum in tiberiandawn/defines.h, so the enum, the INI name and the draw index agree three
ways and neither binding is a guess.

## Seven slots, one mesh

Model-table records 99, 100, 101, 102, 103, 104 and 105 all carry the IDENTICAL node
pointer RAM 0x801B6930, and the cartridge's name catalogue calls all eight of 98..105
`ANY_STR_CRATESZ1`. They are not seven crates and not seven crate contents.

The mode-2 draw arm at RAM 0x801F57B4 takes a piece index from bits 9..14 of the cell
word (`srl 9` / `andi 0x3F`), indexes the 16-record 12-byte table at RAM 0x802100D0, and
ADDS that record's first word to the body model id at RAM 0x801F5848
(`addu $a0, $a1, $v1`) before the draw call at RAM 0x801F5450. That table's offsets are
0, 0, 0, 1, 0, 0, 1, 2, 0, 1, 0, 2, 1, 2, 2, 3, so the run of duplicates is what keeps
ids 90..93 all landing on a crate rather than on some unrelated model. It is a guard
rail, not a set of variants.

Two consequences, both measured:

  * Every crate carries OverlayData 0 (`MapClass::Place_Random_Crate` in
    tiberiandawn/map.cpp writes it), record 0's offset is 0, so a crate always resolves
    to exactly 88 or exactly 90. Only two cubes can ever draw.
  * That has to be true for the steel crate in particular: 89..95 are all the WOODEN
    node, so a non-zero offset on a steel crate would draw wood.

So the answer to "should all seven draw the same cube" is that there is only one wooden
cube to draw, and the bake ships one mesh per kind. Ids 94 and 95 (slots 104, 105) are
past even the offset's reach: no descriptor anywhere produces them.

The draw SCALE needed no separate answer either. The same mode-2 arm passes 0x400 = 1024
in sp+0x10 for the crates and for the walls alike, and 1024 is the renderer's own
MODEL_SCALE denominator, so the cube takes the scale the walls already take.

## What to apply, and where

1. `unit_models_additions.json` -> `tools/bakery/support/unit_models.json`

   Two new entries, `SCRATE` and `WCRATE`. Insert the SCRATE block immediately before the
   line `  "SILO": {` and the WCRATE block immediately before `  "WEAP": {`; both anchors
   occur exactly once and both keep the file's own alphabetical ordering. The bakery
   sorts its keys, so placement only matters for reading.

2. `cnc_eyes.patch` -> `game/cnc_eyes.cpp`  (4 hunks; `patch -p0` from the repo root, or
   apply by hand)

   * removes the crate block from inside `draw_tiberium`
   * adds `draw_crates()` and `crate_art_init()` after `draw_walls`
   * calls `draw_crates()` after `draw_walls(WALLPASS_OPAQUE)`
   * adds the `crates 0|1` and `cratedump` script verbs

3. `gate_crate3d.txt`, `gate_crate3d_off.txt`, `gate_crate3d.py` -> `game/`

4. `gates_sh_G170_block.sh` -> paste into `game/gates.sh` immediately before its footer
   (`echo "----- $LABEL: $PASS pass, $FAIL fail -----"`).

5. RE-BAKE AND INSTALL. Nothing in step 2 shows a cube until the packs carry the meshes;
   until then the renderer draws the 1995 sprites and says so once on stderr. Sweep every
   mission and copy the packs into the play folder the way any other bake is installed.

## What was measured

    bake, SCG32EA, before -> after
      meshes    258 -> 260   added dl_010F3C8, dl_011D4C8, none removed
      triangles 11178 -> 11198  (+10 +10)
      textures  329 -> 331   as a SET: 2 added, 0 removed
      types     586 -> 588   added SCRATE, WCRATE, 0 removed, 0 re-bound
      every pre-existing mesh identical in geometry, colour and RESOLVED texture
      (the raw bytes move because two new textures shift the bank's indices; the
       comparison resolves each triangle's index to its texture content first)
    the same bake in the desert theater, SCB06EA: identical counts, both types conf 2

    the picture, SCG32EA at cam 7.0 60.0 zoom max, crates on minus crates off
      cube    steel 313 px, 16 x 21, mean rgb (83,82,80), r-b =  +3
              wood  312 px, 16 x 21, mean rgb (89,77,30), r-b = +59
      sprite  steel 646 px, 34 x 25, mean rgb (94,94,114), r-b = -20
              wood  646 px, 34 x 25, mean rgb (94,87,48), r-b = +46
      nothing outside the two crate windows changes in either case

    nothing else moved
      SCG01EA at cam 51.5 51.5 zoom min, pre-change binary vs post: diff bbox = None
      SCG32EA on a pack with no crate mesh, pre vs post: 0 differing pixels

## A bug this found on the way

The crate block was nested inside `draw_tiberium`, inside its per-sheet rebind, and
`draw_tiberium` returns early when the field list is empty. So a mission with crates and
NO tiberium drew no crate at all: the bonus arrived with no picture, which is the exact
failure the sprite pass was added to fix.

Three shipped missions are in that state: SCB37EA (2 crates), SCB21EA (1) and SCG38EA (2).
Measured on SCG38EA, whose two steel crates sit at (42,12) and (43,12): the current build
paints 0 crate pixels there and the fixed build paints 1448. `draw_crates()` standing on
its own closes it, and the same move is what lets the cube be a mesh at all, since
`draw_mesh` cannot be called between a `glBegin` and its `glEnd`.

## One thing for the director to look at, not a defect

The cartridge's crate is SMALLER than the sprite we have been drawing: 0.18 of a cell
against 0.42. Faithful, and visibly different. If it reads too small in play the honest
knob is a draw scale in `draw_crates`, not a different mesh.
