# eyetail -- what the EYE and ATWR "discharge" clips actually move

Angle: walk the t = 153280..160000 tail of model slots 11 and 14 node by node, look at the
geometry each moving node carries, and test whether anything becomes visible only in the tail.

**Headline: the tail is not a weapon discharge. It is the antenna masts and dishes falling
over. Slot 11 is not the Ion Cannon; the cartridge names it GDI_STR_ADVCOMZ1, the Advanced
Communications Centre. There is no beam geometry, no beam texture and no visibility channel
anywhere in either model.**

A separate and genuinely new find fell out of the visibility half of the requirement: the scene
graph carries a **per-node TEXTURE FLIPBOOK channel at node+0x10** that no tool in the tree
reads. Ten model slots use it. Neither EYE nor ATWR does. Section 6.

---

## 1. The two slots are named by the cartridge, and one of the names is wrong in our notes

Model-table name-pointer array, ROM 0x1DE924, pointer delta 0x8000BE30, entry index =
model slot - 10:

| slot | entry | name pointer | string ROM | name |
|---|---|---|---|---|
| 11 | 1 | 0x801C33B4 | 0x01B7584 | `GDI_STR_ADVCOMZ1` |
| 14 | 4 | 0x801C3378 | 0x01B7548 | `GDI_STR_AGTWZ1` |

Slot 11 is the **Advanced Communications Centre**. In the 1995 engine that is `STRUCT_EYE`,
the building that *unlocks* the Ion Cannon; the Ion Cannon itself is an orbital weapon with
no building-side muzzle. `known-gap notes` calls slot 11's tail "the Ion Cannon fire
sequence". That name was an inference from the slot id, not a reading of the data, and the
data does not support it.

## 2. The whole rig, node by node, with its geometry

Every ROM offset below is a display-list start in GameModelsGeometry (RAM = ROM + 0x8005C590).
Local bbox is in model units in the mesh (Y-up) frame.

### Slot 11 EYE / GDI_STR_ADVCOMZ1, root node RAM 0x801BDD3C (ROM 0x01617AC)

| node RAM | depth | gfx ROM | verts/tris | local bbox | what it is |
|---|---|---|---|---|---|
| 0x801BDD3C | 0 | 0x0141DC8 | 65 / 34 | 2543 x 709 x 2769 | building shell + ground shadow quad |
| 0x801BD194 | 1 | 0x0140B30 | 49 / 39 | 883 x 426 x 493 | the dish drum (BEEFED02, the idle sweep) |
| 0x801BD5D0 | 1 | 0x01410E8 | 12 / 6 | 338 x **792** x 262 | **mast A**, a tapered column |
| 0x801BD494 | 2 | 0x0140EB8 | 17 / 24 | 171 x 242 x 171 | pod on mast A, 632 up |
| 0x801BDD10 | 1 | 0x0141890 | 12 / 6 | 38 x **153** x 36, node scale 7.743 -> ~294 x 1184 x 279 | **mast B**, a tapered column |
| 0x801BD8D0 | 2 | 0x01413A0 | 17 / 24 | 201 x 286 x 202 | upper pod on mast B, 1069 up |
| 0x801BDBD0 | 2 | 0x0141660 | 17 / 24 | 171 x 242 x 171 | lower pod on mast B, 632 up |

### Slot 14 ATWR / GDI_STR_AGTWZ1, root node RAM 0x801BE604 (ROM 0x0162074)

| node RAM | depth | gfx ROM | verts/tris | local bbox | what it is |
|---|---|---|---|---|---|
| 0x801BE604 | 0 | 0x0143178 | 89 / 50 | 1101 x 1574 x 1144 | tower shell, **its mast is part of this mesh** (triangles run from y 425 to y 1566, min edge 28) |
| 0x801BE394 | 1 | 0x0142490 | 21 / 8 | 171 x 242 x 171 | the dish pod at the mast top, 1141 up |
| 0x801BE5DC | 1 | 0x0142A60 | 72 / 25 | 28 x 40 x 38, node scale 11.869 -> ~332 x 475 x 451 | the horizontal weapon pod; **takes no part in the tail** |

Neither root display list contains a single `G_DL` branch (ROM 0x0141DC8 and 0x0143178 walked
command by command), so there is no sub-list hiding geometry outside the scene graph.

## 3. There ARE two long tapered columns. They are the masts, and they are not beams.

`0x01410E8` (mast A) and `0x0141890` (mast B) are the only long-thin meshes in either slot.
Both are truncated tapered prisms with a top cap:

* mast A: base ring at y = 14 spanning x[-169,169] z[-18,169], top ring at y = 806 spanning
  x[-92,93] z[-93,95]. Six triangles: two are the top cap, four are the sides.
* mast B: base at y = 0 spanning x[-25,13] z[-19,17], top at y = 153 spanning x[-17,5]
  z[-10,9]. Same six-triangle build.

Four reasons these are structure and not a discharge:

1. **They are drawn from t = 0.** Their nodes hang unconditionally off the root, the walk has
   no gate, and `CAFEDEAD::apply` (RAM 0x8008A2B4) only ever builds a matrix. Renders at
   t = 1 show both masts standing.
2. **Render mode is opaque, z-writing cutout, not additive.** Both lists issue
   `G_SETOTHERMODE_L` rendermode `0x00553078` (ROM 0x0141148 and 0x01418F0). Flags 0x3078 =
   AA_EN | Z_CMP | Z_UPD | IM_RD | CVG_X_ALPHA | ALPHA_CVG_SEL; blender word 0x0055 =
   `GBL_c1/c2(CLR_IN, A_IN, CLR_MEM, A_MEM)` in both cycles. That is exactly
   `G_RM_AA_ZB_TEX_EDGE`. An energy beam would be an additive or XLU mode with Z_UPD off.
3. **The texture is a lattice, not a glow.** `G_SETTIMG` RAM 0x8014DDF0 = **ROM 0x00F1860**,
   CI8, tile 16 x 32, `G_SETTILE` w1 0x00017D4F -> maskS 4, maskT 5, **cmS = MIRROR**,
   cmT = WRAP. Palette indices are 0x07 / 0x63 / 0x67 (dark red-brown speckle) across the
   left 8 columns and a constant 0xAD across the right 8 columns; 0xAD is alpha 0 in both
   the Nod TLUT (ROM 0x99130) and the GDI TLUT (ROM 0x98F30). Mirrored on S that gives
   alternating opaque and cut-out bands -- girder work. The UVs repeat it about 3x along
   the mast's length.
4. **Only these two display lists in the whole model table bind that texture** (scanned every
   `G_SETTIMG` of every geometry-bearing node of all 236 slots). It is a private mast texture.

## 4. What the tail actually does: measured node by node

Composed world transforms, mesh frame, from the same maths `tools/anim/anim_extract.py`
uses (its `compose`, `eval_track`, `beefed_local_at`). "own-Y off world-up" is the angle
between the node's own +Y axis and world up, i.e. how far it has tipped.

### EYE, slot 11

| node | f 0 .. 958 | f 973 | f 983 | f 990 | f 995 | f 1000 |
|---|---|---|---|---|---|---|
| 0x801BD5D0 mast A tip | 0.0 deg | 11.0 | 20.1 | 26.5 | 31.0 | **35.5 deg** (rotation since t=1: 43.2 deg) |
| 0x801BD494 pod A move | 0 | 132 | 242 | 311 | 358 | **396 units**, tipped 54.7 deg |
| 0x801BDD10 mast B tip | 0.0 deg | 28.1 | 51.3 | 67.4 | 78.8 | **exactly 90.0 deg** |
| 0x801BD8D0 upper pod move | 0 | 520 | 910 | 1161 | 1317 | **1466 units**, ends at y = -10.2, i.e. **on the ground** (started at y = 1069.6) |
| 0x801BDBD0 lower pod move | 0 | 277 | 478 | 616 | 692 | **775 units**, y 632 -> 245 |
| 0x801BD194 dish drum | sweeping all clip | 37.5 | 53.5 | 56.5 | 58.7 | **60.0 deg off vertical** |

Both mast nodes keep their origin fixed to the last decimal at every sample: they pivot at
their own base. Mast B ends at exactly 90.0 degrees, its top pod on the deck. The dish's
BEEFED02 R curve finishes on the clean pair (0.86602, -0.5, 0, 0) = 60 degrees, which is a
deliberately authored end pose, not a sweep position.

That is a **collapse**. Nothing extends, nothing emits, nothing grows: three tall things
fall down and one drum tips over.

### ATWR, slot 14

| node | f 0 .. 958 | f 973 | f 983 | f 990 | f 995 | f 1000 |
|---|---|---|---|---|---|---|
| 0x801BE394 dish pod | still | moved 64 | 118 | 134 | **157 units**, rotated 94.4 deg | held (last key at t=159201) |
| 0x801BE5DC weapon pod | untouched -- its only track's last key is t = 32000 | | | | | |
| root shell (incl. the mast) | no transform node at all | | | | | |

ATWR's tail moves exactly one part: the dish pod slides 79 units down the mast, 130 units
out, and rolls 94 degrees. Read on its own that is "the dish comes off its mount". It is a
weaker read than EYE's, and I label it as such -- but it is certainly not a discharge, and
the tower's own weapon pod (0x801BE5DC) does not move at all during the tail.

## 5. The two tails are ONE authored event, and the motif appears nowhere else

Swept every FEEB0002 spin angle and every key time of all 236 slots. Two spin angles recur:
**1.047360 rad (60.01 deg)** and **0.908665 rad (52.06 deg)**. They occur on exactly four
nodes, in exactly two slots:

| node | key | t | angle | axis |
|---|---|---|---|---|
| EYE 0x801BD494 | k4 / k5 | 157280 / 158880 | 1.047360 / 0.908665 | (-0.8245, -0.2035, **0.5281**) / (0.0379, -0.4009, **0.9153**) |
| EYE 0x801BD8D0 | k4 / k5 | 157280 / 158880 | same | (-0.1858, -0.8286, **0.5281**) / (0.3757, -0.1450, **0.9153**) |
| EYE 0x801BDBD0 | k4 / k5 | 157280 / 158880 | same | (-0.8492, -0.0054, **0.5281**) / (-0.0568, -0.3987, **0.9153**) |
| ATWR 0x801BE394 | k13 / k14 | 157601 / 159201 | same | (-0.8302, 0.1785, **0.5281**) / (-0.1417, -0.3770, **0.9153**) |

Same angle, same axis Z component to four decimals, the XY part of each axis the same length
(0.8492 and 0.4027) and only its direction differing per node: one rigid tumble expressed in
four different local frames. Keys ROM 0x0160C7C, 0x01610B8, 0x01613B8 and 0x0161840.

Matching T tracks (all FEEB0009, 5 keys, hold then travel):

| node | keys ROM | held value, t = -320 .. 153761 | 157601 | 158401 | 159201 |
|---|---|---|---|---|---|
| EYE 0x801BD494 | 0x0160C20 | (-0.306, -127.569, 640.747) | (-41.34, -28.15, 570.74) | (-49.14, -18.56, 580.84) | (-50.91, -1.80, 561.36) |
| EYE 0x801BD8D0 | 0x016105C | (13.222, -0.045, 137.533) | (-0.02, 4.15, 128.49) | (-1.62, 4.17, 129.80) | (-3.42, 5.39, 127.28) |
| EYE 0x801BDBD0 | 0x016135C | (-20.636, -14.673, 81.011) | (-33.88, -10.48, 71.97) | (-35.47, -10.45, 73.27) | (-37.28, -9.24, 70.76) |
| ATWR 0x801BE394 | 0x01617E4 | (-199.384, 126.241, 1141.580) | (-168.71, 229.33, 1071.56) | (-168.74, 241.69, 1081.67) | (-159.57, 255.83, 1062.18) |

Author frame is Z-up. Every one of them loses about 79 author-Z (the two pods under the
7.743-scaled mast lose 10.25 x 7.743 = 79.4) and gains about 130 in Y. They all drop by the
same amount. That is gravity, not aiming.

Slots 11 and 14 are also the only two slots in the entire model table with a 160000-tick
(1000-frame) timeline; every other animated slot runs 3200 to 80000 ticks.

## 6. Visibility: there is no per-frame visibility channel, and I can now say why

`tools/bakery/bake5.py` writes a `vis` byte per part per frame and hardcodes every one of
them to 1 (line ~1220, "every part starts at identity and visible"). the requirement asked whether
the cartridge carries the visibility the baker never populates. Four places it could live,
all checked, all negative:

1. **The CAFEDEAD pose record has exactly three track pointers.** The record is 0x4C bytes:
   +0x00 T3, +0x0C Q4, +0x1C S3, +0x28 SO4, +0x38 mirror, +0x3C flags, **+0x40 T track,
   +0x44 R track, +0x48 S track**. Dumped the raw bytes past +0x48 on all seven EYE/ATWR
   pose records: the very next word is `0x800AC2C0`, the CAFEDEAD class tag of the *next*
   handle. There is no room for a fourth channel and no fourth pointer. `CAFEDEAD::apply`
   (RAM 0x8008A2B4) `memcpy`s only 0x3C bytes and then evaluates exactly those three tracks.
2. **The pose `flags` word at +0x3C is a constant.** Every one of the 81 CAFEDEAD nodes
   reachable from the model table has `flags = 0x01010000` and `mirror = 1.0`. Nothing there
   distinguishes a node, let alone a frame.
3. **The BEEFED02 curve block is exactly four descriptors.** Node 0x801BD194's curve block at
   RAM 0x801BD114 (ROM 0x0160B84): T at +0x00, R at +0x1C, S at +0x38, SO at +0x54, and at
   +0x70 the handle data block begins (`{0x801BD114, 0x801C1300}` then tag `0x800AC2D8`).
   No fifth curve.
4. **The node struct's spare words are zero.** Node is 0x1C bytes; +0x14 and +0x18 are
   `00000000` on every node in the model table (293 of 293 for +0x14/+0x18).
5. Bonus: the third handle class **DEADCAFE** exists (RAM 0x800AC2CC, apply 0x8008A3D4) but
   **zero** nodes in the model table use it. Class census: 208 nodes with a null handle,
   81 CAFEDEAD, 15 BEEFED02, 0 DEADCAFE.
6. Neither slot has an **S track at all**, and no rest scale is zero, so nothing is hidden by
   being scaled to nothing and revealed later either.

### 6b. What node+0x10 IS: an undecoded per-node TEXTURE FLIPBOOK, and PROC/SILO are in it

Node word +0x10 is non-zero on exactly **11 nodes across 10 slots** and zero everywhere else.
Following it:

```
node+0x10 -> record   +0x00 sub     +0x0C nkeys   +0x20 self
sub                   +0x00 -> time array (int32 ticks, n+1 entries)
                      +0x04 -> texture-pointer array (n entries) = end of the time array
                      +0x08    a G_SETTIMG command word0 TEMPLATE (fmt/siz/width)
                      +0x0C    the currently bound texture address
                      +0x14    texture count in the HIGH half-word
```

The whole census, times converted at 160 ticks/frame:

| slot | node | sub | fmt/siz/width | times (frames) | textures (ROM) |
|---|---|---|---|---|---|
| 1 | 0x801B5618 | 0x801B5560 | I8 w32 | 0,1,2,3,4,5,6,7,8 | 0x0DF860 +0x400 x8 |
| 2 | 0x801B5758 | 0x801B5730 | RGBA32 w32 | 0,3,6,9,109 | 0x0ED260 +0x1000 x4 |
| 3 | 0x801B5898 | 0x801B5870 | RGBA32 w32 | 0,5,8,11,111 | same four |
| 4 | 0x801B59D8 | 0x801B59B0 | RGBA32 w32 | 0,7,10,13,113 | same four |
| 5 | 0x801B5B18 | 0x801B5AF0 | RGBA32 w32 | 0,9,12,15,115 | same four |
| 17 FACT | 0x801359F8 | 0x801359D0 | CI8 w16 | 0,1,2,3 | 0x0C67B0, 0x0C68B0, 0x0C69B0 |
| 19 PROC | 0x801B6E18 | 0x801B6DE8 | CI8 w16 | 0,10,100 | 0x0E2B60, 0x0E2A60 |
| 19 PROC | 0x801B6DC8 | 0x801B6DA0 | CI8 w16 | 0,20,30,40,50,60,70 | 0x0E4060 +0x20 x6 |
| 21 SILO | 0x801B7700 | 0x801B76D8 | CI8 w2 | 0,10,20,30,40,50 | 0x0E4BA0 +0x10 x5 |
| 22 HPAD | 0x801BEE48 | 0x801BEE20 | CI8 w16 | 0,10,20,30,40,50,60 | 0x0F28F0 +0x200 x6 |
| 32 FIX | 0x801B74D8 | 0x801B74B0 | CI8 w8 | 0,10,20,30,40,50 | 0x0E4120 +0x40 x5 |

**SILO gets FIVE textures.** `known-gap notes` says the silo's decoded arm has five tiberium
fill poses and complains that the baked clip is identity. It is identity because the silo's
animation was never a transform: it is a five-frame texture swap on one node. PROC, HPAD and
FIX are the same shape of thing. This looks like the real answer to the standing
"PROC and SILO bake to identity" gap. I am labelling it a **strong hypothesis** rather than a
decode, because I read the record's layout off its own contents and its consumer function has
not been disassembled; the layout is self-consistent across all 11 records (ntimes always
ntex+1, the template's fmt/siz/width always matching the node's own G_SETTIMG, and the texture
pointers always evenly strided by exactly the texture's byte size) which is why I think it is
right.

**Slot 11 EYE and slot 14 ATWR have no such record.** So the last channel that could have
hidden a beam does not exist on either building.

## 7. Dead ends, so nobody repeats them

* **node+0x04 is NOT a second display list.** `objgraph2.parts_of_node()` treats it as an
  alternate skin. It is the baked N64 rest Mtx: row 3 of node 0x801BD5D0's is
  (269.818, -8.758, -770.366) against P*(static T) = (269.818, -8.758, -770.366), and the
  same for 0x801BD494, 0x801BDD10 and 0x801BE394. Walking it as a display list "works" only
  because the walker skips unknown opcodes and then runs into the real list further on, which
  is how I briefly believed each mast node carried a second pod mesh. It does not.
* The `+0x04` fall-through in `parts_of_node` may therefore be feeding the baker matrix bytes
  as geometry for PROC's mast, SILO's dome and FACT's crane, which the comment there names as
  the three cases where +0x00 is NULL. Not chased -- flagged for whoever owns the bakery.
* No `G_DL` branch exists anywhere in either building's display lists.
* The mast texture is not shared with any other model in the game, so "find the beam by
  finding who else uses the beam texture" has nothing to find here.
* The 60.01/52.06-degree motif exists on no other slot, so there is no third building with a
  hidden matching event.


## 7b. The sibling weapon buildings say the same thing (corroboration)

While the census was open I checked the other buildings that fire something, because if the
cartridge ever drew a beam inside a building's own model it would show up here too:

| slot | cartridge name | nodes | geometry | animation | node+0x10 flipbook |
|---|---|---|---|---|---|
| 10 | `NOD_STR_TMPL_Z1` (Temple of Nod, the **nuclear strike** building) | 1 | ROM 0x00D4158 (ScriptModels), 199v / 101t, bbox 4084 x 2107 x 3143, no `G_DL` branch | none | none |
| 15 | `NOD_STR_OBELISKZ1` (**the laser**) | 1 | ROM 0x01513D8, 45v / 20t, bbox 1166 x 1769 x 1242, no `G_DL` branch | none | none |
| 13 | `GDI_STR_GTOWZ1` | 1 | ROM 0x0145B78, 79v / 35t | none | none |
| 16 | `NOD_STR_TURRETZ1` | 2 | root + one BEEFED02 barrel (the known facing turret) | facing only | none |

The Obelisk of Light is the cleanest control there is: a building whose entire visual identity
is a beam, and its model is a **single static mesh with twenty triangles**. The Temple of Nod,
which launches the nuclear strike, is likewise one static mesh. Whatever the cartridge draws
for a beam or a warhead, it does not live in the firing building's model. That is one more
independent reason to read slot 11's tail as a collapse rather than a discharge.

## 8. What this means for the two effects

* **Ion Cannon beam: not in slot 11.** The Advanced Communications Centre's model contains no
  beam, no emitter, no additively-blended geometry and no channel that could reveal one. The
  "already baked, never plays" tail at bake indices 963..1002 is the building's masts and dish
  collapsing. It should be renamed in `known-gap notes` and, if anything, driven off
  destruction rather than BSTATE_ACTIVE. Note also that the entry's claim that nodes
  0x801BD5D0 and 0x801BDD10 "exist for nothing else" is wrong: they carry the two mast meshes
  and are drawn for the whole clip; only their *rotation keys* start at 153760.
* **Ion Cannon beam: still consistent with living at the TARGET.** `house.cpp:2675-2688`
  spawning `ANIM_ION_CANNON` at the target cell remains the only place it can be, which is
  the recipe-table and firing-path agents' ground.
* **Nuclear strike: nothing in this angle.** Slots 26 and 27, which our tables call NUKE and
  NUK2, are named `ANY_STR_PPLANTZ1` and `ANY_STR_APPZ1` by the cartridge -- the Power Plant
  and the Advanced Power Plant. They are not nuclear weapons and neither carries a tail.
* **The one live lead this angle produced** is section 6b: an eight-frame 32x32 I8 texture
  flipbook on slot 1 and a four-frame 32x32 RGBA32 flipbook shared by slots 2..5 at staggered
  start frames, all played by a channel that no tool in this tree reads. If either effect is
  a texture flipbook rather than a mesh, that is the mechanism it would be played by, and
  those five ScriptModel slots are where to look.

## 9. Reproduce

Scripts under `scratch work`: `et_rig.py` (rig + world transforms),
`et_walk.py` (section 4), `et_classes.py` / `et_p10.py` / `et_p10b.py` (section 6),
`et_flip2.py` (section 6b), `et_root.py` (root display lists), `et_texusers.py`.
Renders in `scratch work*.png` and `r_ATWR_*.png` (tail nodes drawn red).
All read-only against `data/rom/cnc_eu.z64`; nothing in the repo was touched.
