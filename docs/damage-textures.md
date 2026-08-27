# The cartridge's per-building DAMAGE TEXTURES

Corrects the earlier finding that this art did not exist.
**Status: decoded, NOT YET IMPLEMENTED.**

## Summary

That earlier finding is wrong. The cartridge ships per-building damage art for every structure it has a model for, and the mechanism is exactly the DOS rule. `BuildingClass::Draw3D` calls the object's `Health_Ratio()` virtual and does `sltiu $v0,$v0,0x80` (ROM 0x03E934 / RAM 0x8003DD34), shifts the result left by 3 and passes it as draw-flag bit 3 into every model draw command. The model draw-command handler at RAM 0x8004C108 tests that bit twice and, when set, looks up a 24-record x 24-byte damage table at RAM 0x800A36C0 (ROM 0x0A42C0) through a 123-entry model-index jump table at RAM 0x80005660, then appends `gsSPDisplayList(record[+0x04])` to the building's own display list and swaps its particle emitter chain. Those 21 appended display lists are the damaged art: real textured geometry (2 to 78 triangles each) carrying 16 textures that NO intact model binds, and decoding them to PNG shows unmistakable blast holes, torn panels, cracks and glowing embers. Nothing found them before because there is no damaged MESH (the model-slot sweep was correct) and no fourth TLUT (the TLUT sweep was correct): the damage is a separate stand-alone display list reachable only from that one table, unreferenced by any scene-graph node, so both prior searches were structurally blind to it.

## Evidence

### ROM 0x03E930-0x03E93C / RAM 0x8003DD30-0x8003DD3C (BuildingClass::Draw3D)

MEASURED. `lw $v0,0x14($s6); lw $v0,0x84($v0); jalr $v0` then `sltiu $v0,$v0,0x80; sll $s1,$v0,3`. obj+0x14 is the vtable (the same idiom at RAM 0x8003DC00 fetches Get_Mission from +0xD0). +0x84 and +0xD0 are 19 slots apart; in brain/vanilla/tiberiandawn/object.h Health_Ratio (line 229) and Get_Mission (line 267) are 24 virtuals apart, and exactly 5 of those (lines 250-254, the per-player selection set the 2020 GPL release added for multiplayer) cannot exist in the 1995/N64 build. 24 - 5 = 19. So +0x84 is Health_Ratio and the test is literally the 1995 rule Health_Ratio() < 0x0080.

### ROM 0x03EE04-0x03EE10 / RAM 0x8003E204 (common tail of every StructType arm)

MEASURED. `move $a3,$s5; sw $s1,0x10($sp); jal 0x8004a420; sw $s2,0x14($sp)`. s1 (0 or 8) is the 5th argument. In the callee (RAM 0x8004A420, ROM 0x04B0A4) `lw $v0,0x40($sp); sw $v0,0x20($v1)` writes it to drawCmd+0x20, the draw-flags word. Every one of the 59 per-StructType arms converges here, so every building carries the bit.

### ROM 0x04D238 / RAM 0x8004C638

MEASURED. `jal 0x80057108; move $a0,$s1` where s1 = drawCmd+0x10 = model index; `move $s0,$v0`. This is the ONLY call site of 0x80057108 in the whole ROM (scanned for the jal encoding 0x0C015C42).

### RAM 0x80057108 / ROM 0x0057D08, jump table RAM 0x80005660 / ROM 0x0004A60, record array RAM 0x800A36C0 / ROM 0x0A42C0

MEASURED. `sltiu $v0,$a0,0x7b` (model index < 123), 123-entry jump table, each arm loads a small damage index into a1; tail at RAM 0x800571EC does `record = (a1 == -1) ? NULL : 0x800A36C0 + a1*24`. Dumped all 123 arms: exactly 24 model indices map to a record, 0..7, 9, 11..23 (model slots 10..33, i.e. every structure model the cartridge has), plus 39 (slot 49 GUNBOAT) and 122 (slot 132). Model indices 8 and 10 (the MCV deploy rig and the refinery's second command) and every V01-V37 civilian map to -1.

### ROM 0x04D3DC-0x04D428 / RAM 0x8004C7DC-0x8004C828

MEASURED, and this is the whole answer. After `jal 0x8008fd14` evaluates the model's scene graph into the display-list buffer at s7+0x198, the code does `beqz $s0,...; andi $v0,$s5,8; beqz $v0,0x8004c824`. Damaged path: `lw $v0,4($s0)`, and if nonzero it writes `0xDE000000` and that pointer as a two-word command into the DL stream (`lui $v0,0xDE00; sw $v0,($v1); sw $v0,4($v1)`), i.e. a gsSPDisplayList APPENDED after the intact building. Then a1 = record[+0x08] instead of record[+0x00] and both go to the particle spawner at RAM 0x800567C0. So the damaged art is an ADDITIVE OVERLAY on the intact mesh, not a replacement.

### ROM 0x04D2C0-0x04D2F8 / RAM 0x8004C6C0-0x8004C6F8

MEASURED. The second bit-3 test, on the branch that pushes a G_MTX first: if damaged and record[+0x14] != 0, it forces the animation time to `t = record[0x14]*160 + *0x80004958`, overriding the per-StructType frame from the 59-entry table. Only three records use it: EYE (record+0x14 = 1000), HQ (1000), WEAP (100). Those are the three structures with a moving part (two dishes and a door), so a damaged one is pinned to a fixed pose.

### Damage records, RAM 0x800A36C0 + i*24

MEASURED layout from the four fields the code reads: +0x00 healthy particle-emitter chain head, +0x04 pointer to the damaged display-list stub, +0x08 damaged particle-emitter chain head, +0x14 forced damaged animation frame. +0x0C (1 for OBLI, GUN, SAM, HOSP, MISS) and +0x10 (61 for WEAP, 961 for EYE and HQ, i.e. record[+0x14] - 39 in both cases) are read NOWHERE: the base address 0x800A36C0 is materialised in exactly one function and s0 never leaves that stack frame. Dead fields in the shipped build.

### The 21 damaged display lists, e.g. ATWR stub RAM 0x8019FC80 -> DL RAM 0x8019FBE8 / ROM 0x143658

MEASURED. record[+0x04] always points at a two-command stub `gsSPDisplayList(realDL); gsSPEndDisplayList()`. The real DL is a complete textured mesh: `E7 RDPPIPESYNC; E3 SETOTHERMODE_H TEXTLUT=G_TT_RGBA16; FD SETTIMG CI8 w=32 <texture>; F5 SETTILE; F4 LOADBLOCK; F5 SETTILE; F2 SETTILESIZE 32x32; D7 TEXTURE; E2 SETOTHERMODE_L 0x00553078; 01 VTX; 05 TRI1 ...; E2 SETOTHERMODE_L 0x00552078; E3 restore; DF ENDDL`.

### All 21 damaged DLs walked out of the ROM

MEASURED counts: TMPL 16v/8t, EYE 12v/12t, WEAP 6v/4t, GTWR 23v/13t, ATWR 4v/2t, GUN 50v/31t, FACT 18v/12t, PROC 20v/10t, SILO 82v/36t, HPAD 8v/6t, HQ 12v/12t (shares EYE's DL), SAM 4v/2t, AFLD 4v/2t, NUKE 12v/6t, NUK2 28v/25t, HOSP 14v/8t, BIO 58v/34t, PYLE 72v/34t, HAND 20v/20t, FIX 4v/2t, MISS 201v/78t. Total 668 vertices, 359 triangles.

### The 16 damage textures decoded to PNG through the resident Nod/neutral TLUT at ROM 0x99130

MEASURED AND LOOKED AT. Every one is damage art: blast craters with glowing orange embers on grey concrete (0x80123950, the shared one), torn steel panels with fire behind them (0x80123D50), the Temple's own punched grid (0x80125090), red interior showing through ruptured plating (0x8014D9F0 EYE/HQ, 0x80148AA8 GTWR, 0x801486A8 PROC/SILO/NUKE, 0x8013DDF0 NUK2), thin structural cracks (0x80140D30 SILO). Rendered to <scratch>/dmgtex/labeled.png. This is not an inference from names; it is the pixels.

### Bounding boxes, damaged geometry vs intact model (objgraph2.display_lists)

MEASURED, in the same 1024-units-per-cell model space. ATWR damaged x[-170,285] y[929,991] z[-24,404] against intact x[-427,674] y[-121,1566] z[-560,584]: a hole high on the tower. FACT damaged x[-837,1191] y[88,657] z[-944,1018] against intact x[-1273,1758] y[-363,708] z[-1905,1072]. WEAP, NUKE and PYLE likewise sit strictly inside their own building's box. The overlays are co-located with the building, not floating.

### Whole-tree cross-check against all 236 model slots

MEASURED. Walking every display list reachable from every model slot's scene-graph node (objgraph2.display_lists plus G_DL recursion) reaches 259 display lists and binds 186 distinct textures. NONE of the 21 damaged display lists and NONE of the 20 stubs appears in any model tree, and 15 of the 16 damage textures are bound by nothing in the intact set (the exception is 0x80125B90, a plain grey concrete tile used as the second tile of a two-cycle blend on GUN and NUK2). This is the surplus lead 5 predicted, measured: 186 intact + 15 damage-only.

### ROM-wide scan for the `sltiu rX,rY,0x80` followed by `sll rZ,rX,3` idiom over ROM 0x1000-0x220000 (resident plus CodeOverlay)

MEASURED. Exactly one hit, ROM 0x03E934. Only BuildingClass::Draw3D sets the damaged flag. UnitClass::Draw3D and AircraftClass::Draw3D never do, so the GUNBOAT record (index 23, healthy emitter only) and slot 132's record are effectively a funnel-smoke emitter and dead data respectively.

### Particle spawner RAM 0x800567C0 / ROM 0x0573C0

MEASURED. Emitter record layout: +0x00 next-in-chain (walked to NULL at RAM 0x800568BC), +0x04 a frame mask ANDed with s7+0xC0 so the emitter fires every 2^n frames, +0x08 a float jitter magnitude, +0x0C onward the particle template. The healthy chains are the smokestack plumes (WEAP 2 emitters, PROC 2, NUKE 1, NUK2 2, GUNBOAT 1) and the damaged chains are the damage smoke (TMPL 2, WEAP 2, PROC 2, NUKE 2, NUK2 2, and 1 each for EYE, GTWR, ATWR, OBLI, GUN, FACT, SILO, HPAD, HQ, SAM, AFLD, PYLE, HAND, FIX). HOSP, BIO and MISS have geometry but no emitter.

### Structure-type array ROM 0x165FD4 + i*0xAC

MEASURED. Re-dumped all 64 records (model index at +0x00, Ident at +0x08, StructType at +0xA5) to pin the damage index to a building name rather than to the deduplicated name-array string. Model indices 18, 19 and 23 (slots 28, 29, 33) are HOSP, BIO and MISS, not the three extra 'ANY_STR_SILOZ1' entries the name array at ROM 0x1DE924 shows.

## Design

## The complete table: which art, which building, which health

Selector: `damaged = (Health_Ratio() < 0x80)`, i.e. `hb_ratio256(o.str, o.maxstr) < 0x80` which game/shroud_mod.h:627 already computes and which is byte for byte the engine's `Cardinal_To_Fixed(MaxStrength, Strength)`. ONE threshold, ONE damage level. There is no second stage.

```
dmg  bld   ST  mi slot  stub RAM   dmgDL RAM  dmgDL ROM  textures (RAM / ROM)
 0   TMPL  19   0   10  80130768   80130680   0x0D49C0   80125090/0C93D0
 1   EYE   20   1   11  8019E880   8019E798   0x142208   8014D9F0/0F1460
 2   WEAP   0   2   12  801A4C98   801A4BF8   0x148668   80150480/0F3EF0
 3   GTWR   1   3   13  801A1C08   801A1A48   0x1454B8   80123950/0C7C90 + 80148AA8/0EC518
 4   ATWR   2   4   14  8019FC80   8019FBE8   0x143658   80123950/0C7C90
 5   OBLI   3   5   15      -          -          -      none (damage smoke only)
 6   GUN    5   6   16  801AEBF8   801AE9B0   0x152420   80147EA8/0EB918 + 80125B90/0C9ED0
 7   FACT   6   7   17  8012F198   8012F070   0x0D33B0   80123950/0C7C90 + 80123D50/0C8090
 8   PROC   7   9   19  80175688   80175570   0x118FE0   801486A8/0EC118 + 801401F0/0E3C60
 9   SILO   8  11   21  801799C8   801797B0   0x11D220   801486A8/0EC118 + 80140D30/0E47A0
10   HPAD   9  12   22  801A2BA0   801A2AC8   0x146538   8014EA80/0F24F0
11   HQ     4  13   23  8019E880   8019E798   0x142208   8014D9F0/0F1460   (shares EYE's)
12   SAM   10  14   24  8012A4F0   8012A438   0x0CE778   80123950/0C7C90
13   AFLD  11  15   25  801AAE78   801AADC0   0x14E830   80123950/0C7C90
14   NUKE  12  16   26  80172430   80172318   0x115D88   801486A8/0EC118 + 801482A8/0EBD18
15   NUK2  13  17   27  80164480   80164230   0x107CA0   8013DDF0/0E1860 + 80125B90/0C9ED0
16   HOSP  14  18   28  8016C768   8016C6A0   0x110110   80123950/0C7C90
17   BIO   17  19   29  80166768   801664E8   0x109F58   80123950/0C7C90
18   PYLE  15  20   30  801A18C8   801A16C0   0x145130   80123950/0C7C90 + 80123D50/0C8090
19   HAND  18  21   31  801AD688   801AD518   0x150F88   80123950/0C7C90 + 801536E0/0F7150
20   FIX   16  22   32  80176580   801764E8   0x119F58   80140830/0E42A0   (16x16, not 32x32)
21   MISS  21  23   33  8016F528   8016F200   0x112C70   80123950/0C7C90
```
All textures are CI8 32x32 except FIX's, which is CI8 16x16. All go through the same house TLUT selector as every other building texture, so they need the same `gl` / `gl_gdi` pair PackTex already gives every texture (game/cnc_eyes.cpp:749). Overlay group is ov1 (ScriptModels + GameModelsTextures + GameModelsGeometry), so `segments.py` resolves all of them unambiguously; note that five of the assets (TMPL, FACT and SAM's display lists, and textures 0x80123950 / 0x80123D50 / 0x80125090 / 0x80125B90) live in ScriptModels, not in the geometry segment, so a resolver that only knows GameModelsGeometry will miss them.

RDP state is uniform across all 21: enter with `G_SETOTHERMODE_H TEXTLUT = G_TT_RGBA16` and `G_SETOTHERMODE_L 0x00553078`, leave restoring `0x00552078` and TEXTLUT 0. 0x3078 decodes as AA_EN | Z_CMP | Z_UPD | IM_RD | CVG_X_ALPHA | ALPHA_CVG_SEL with ZMODE_OPA: an alpha-cutout opaque surface with normal Z, NOT a decal mode. Eleven of the 21 also set combiner `FC127E24 / FFFFF3F9` and restore `FCFFFFFF / FFFCF279`; the other ten set no combiner and inherit whatever the model's own list left behind.

## Renderer (game/cnc_eyes.cpp)

No brain change. `SimObject` already carries `str` / `maxstr` (cnc_eyes.cpp:1853, parsed at :2908) and `hb_ratio256` already computes Health_Ratio exactly. So:

```c
/* The cartridge's own rule, ROM 0x03E934: Health_Ratio() < 0x80 turns on
   draw-flag bit 3, and bit 3 appends the building's damage overlay list. */
static inline int cart_is_damaged(const SimObject& o)
{ return hb_ratio256(o.str, o.maxstr) < 0x80u; }
```

Then, right after the structure body is drawn (the `draw_mesh` call for `mesh_for(o)`, cnc_eyes.cpp:5412 / :7178), draw the overlay mesh with the SAME transform, same cell centre, same facing, same terrain lift, in the same pass. Cleanest shape is a lookup keyed on the mesh index:

```c
/* dmg_mesh_for(mi) -> pack mesh index of the damage overlay, or -1.
   Table transcribed from RAM 0x800A36C0 (24 records) via the model-index
   jump table at RAM 0x80005660: 22 structures, OBLI has none. */
int dmi = cart_is_damaged(o) ? dmg_mesh_for(mi) : -1;
if (dmi >= 0) {
    draw_mesh(dmi, wx, wz, face, MODE_OPAQUE, ...same args as the body...);
    draw_mesh(dmi, wx, wz, face, MODE_CUTOUT, ...);
}
```
`--nodamageart` for the A/B, and a new diagnostic line on its OWN prefix (never appended to `OBJ|` or `STRUCT|`, which are gate contracts):
```
DMGART|<type>|id=%d|str=%d/%d|ratio=0x%02X|mesh=%d|drawn=%d
```

Two implementation notes that will bite otherwise. (1) The overlay is ADDITIVE: the cartridge appends `gsSPDisplayList` after the whole model, so draw the intact building too, never instead of it. (2) The cartridge's ZMODE is OPA, not DECAL, and the overlay geometry is authored slightly proud of the building surface. On our GL depth range that may still z-fight; if it does, a small `glPolygonOffset` (or a fixed epsilon lift) is OURS and must be registered as such, the same way the +0.012 soldier-shadow lift is.

Also worth doing at the same time, because it falls out of the same table: EYE, HQ and WEAP force their animation frame when damaged (record+0x14 = 1000, 1000, 100). Our `object_anim_t` should clamp those three to that frame while damaged. Low value visually, one line, and it is decoded rather than invented.

## Bakery (tools/bakery/bake5.py, objgraph2.py)

The damage art is NOT in the pack today and cannot be: proven above, no model tree reaches it. So the bakery needs a new source of meshes that is not the scene-graph walk.

- Add a transcribed table (a JSON beside `unit_models.json`, generated by a new `tools/romdump/damage_art.py` that reads RAM 0x800A36C0 and the jump table at RAM 0x80005660 and SELF-ASSERTS the 24 records and the 123 jump-table arms, refusing to write if any constant moved, exactly the way `soldier_shadow.py` does its 25 assertions).
- Bake each of the 21 damaged display lists through the existing `bake_mesh_prim` path as an ordinary PKB mesh named by its ROM offset, so `dl_0143658` (ATWR) and friends, and register the 16 textures through the ordinary PK6 texture path with their `gl_gdi` house variants. Total cost: 21 meshes, 359 triangles, 668 vertices, 16 textures. Trivial against the 6.8 MB pack.
- Ten of the 21 lists set no `G_SETCOMBINE` and inherit the model's. `bake_mesh_prim`'s rule that an unknown combiner STOPS the bake (added when the ATTACK cursor baked grey) will fire on those. Seed the walker with the trailing combiner of the corresponding building's own list, and assert by EQUALITY that exactly 11 of the 21 carry their own combiner, so the day that set changes the bake fails rather than guessing.
- Add a tripwire asserting the 22-entry mapping by equality (21 with geometry + OBLI without), for the same reason.

The `gl_gdi` precedent the brief points at is the right one and it needs no extension: these are ordinary CI8 textures under the ordinary house TLUT, so the existing two-variant PackTex carries them unchanged.

## Tier 1 (Win98 / Voodoo 2) answer

Fully reachable, no new debt. The overlay is fixed-function: one extra textured mesh per damaged building, alpha-cutout (`grAlphaTestFunction` / `GR_CMP_GEQUAL`), z-buffered, no shader, no render target, no second pass over the frame. It is the same draw the building body already takes. The only Tier 1 question is the z-fight epsilon, and a constant depth bias is available on Glide via `grDepthBiasLevel`. Nothing here goes in docs/tier1-gap.md.

## Open questions

Row 139(c) "There is no damaged MESH" is CORRECT as written and WRONG as a conclusion, and both halves should be said out loud so nobody re-runs either search. There is no damaged mesh and no fourth TLUT; there are 21 damaged DISPLAY LISTS reachable only from a table nothing else in the ROM points at. Row 117 (BRIK 206-209) is a genuinely different mechanism: walls get damaged art as whole model slots selected by the table at ROM 0x1B1D48, buildings get it as an appended display list selected by Health_Ratio. Two systems, not one.

## Tier 1

Fully Tier 1 capable, no new gap. Each damaged building draws ONE extra textured mesh (2 to 78 triangles, one or two CI8 32x32 textures) with the same transform as its body, in alpha-cutout mode with ordinary Z compare and Z update. The cartridge's own render mode is 0x00553078 = AA_EN|Z_CMP|Z_UPD|IM_RD|CVG_X_ALPHA|ALPHA_CVG_SEL, ZMODE_OPA, which maps directly onto Glide alpha test plus the depth buffer. No shader, no render target, no second framebuffer pass, no wrap mode beyond what the building textures already use. Budget on a fully built base is at most 22 extra meshes and 359 extra triangles per frame, and only for buildings actually below half health. The single Tier 1 detail is that the overlay geometry sits slightly proud of the building surface rather than using a decal mode; if it z-fights at 16-bit depth, grDepthBiasLevel supplies the constant bias, and that bias is OURS and gets registered as ours.

## Verification plan

["Re-run the two decisive disassemblies and match the words quoted here: ROM 0x03E934 must read 2C420080 (sltiu $v0,$v0,0x80) and ROM 0x03E93C must read 000288C0 (sll $s1,$v0,3); ROM 0x04D3E0 must read 32A20008 (andi $v0,$s5,8).","Dump RAM 0x800A36C0 as 24 records of 6 big-endian words and check them against the table in this report; dump the 123-entry jump table at RAM 0x80005660 and check that exactly model indices 0-7, 9, 11-23, 39 and 122 resolve to a record.","Walk every one of the 21 damaged display lists and assert the vertex and triangle counts listed here (total 668 vertices, 359 triangles). Assert that every list opens with G_SETOTHERMODE_L 0x00553078 and closes restoring 0x00552078.","Re-run the negative that makes this a new finding: walk all 236 model slots' scene-graph display lists with G_DL recursion, and assert that the intersection with the 21 damaged lists and the 20 stubs is EMPTY, and that 15 of the 16 damage textures are bound by nothing in the intact set (186 textures bound by intact art).","Decode the 16 textures to PNG through the resident TLUT and LOOK at them. That is the step that settles the question; the montage is at <scratch>/dmgtex/labeled.png and is trivially regenerated.","Once built: pixel A/B on a shipped mission. Shoot the identical frame with --nodamageart on and off while a Barracks or Refinery is below half health, and assert a non-zero changed-pixel count confined to that building's footprint. Assert the count is ZERO while the same building is above half health. That is the gate.","Assert the threshold is the cartridge's: at ratio 0x80 exactly, nothing draws; at 0x7F it draws. Off-by-one here is the easiest way to ship a wrong version of a correct finding."]

## Open questions

- Should the three forced damaged frames (EYE 1000, HQ 1000, WEAP 100) be implemented at the same time as the overlay geometry, or held back? They are decoded and cheap, but they change the look of an animated part rather than adding art, and they should probably be A/B-able separately.
- What is model slot 132? It has a damage record with an emitter, no structure type points at it, and its identity was not established. Worth ten minutes from whoever next walks the model table, and it may be a second finding rather than part of this one.
- The GUNBOAT healthy emitter (record index 23, RAM 0x8009ABE0) and the four other healthy emitter chains (WEAP, PROC, NUKE, NUK2) are smokestack plumes the renderer does not draw at all today. They are a separate, adjacent feature that this pass uncovered. Does it get folded into the same build or its own?
- The emitter records are a linked-list particle format at RAM 0x8009A980..0x8009B34C which does not obviously match the recipe format already transcribed into game/efx_recipes.h. Decoding it is the difference between a damaged building that shows damage art and one that also smokes. Should that be scoped into this work or split?
- Do the damaged overlays want the same house-TLUT treatment on the shared texture 0x80123950? It is bound by ten different buildings across both houses, so a GDI Barracks and a Nod Hand of Nod would show the same scorch in different tints. That is what the hardware does, but it should be looked at once on screen.