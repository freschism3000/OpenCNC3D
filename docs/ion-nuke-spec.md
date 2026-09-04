# ION CANNON and NUCLEAR STRIKE on the N64 cartridge: one buildable specification

Synthesis of five ROM sweeps (recipes, models, eyetail, geometry, callsite). Every claim
below that either mattered or was disputed I re-measured against
`data/rom/cnc_eu.z64` myself; where two agents disagreed I say which one was right and
what the ROM actually says. Nothing here is averaged.

**Headline: both effects were found, and they are the same kind of thing.** Each is a
group of four textured 3D meshes in a ten-entry effects table at the head of the model
table, drawn by an explicit two-armed special case in the anim draw path that keys on the
AnimType, with the clip time driven by the AnimClass stage counter. The Ion Cannon uses no
particles at all; the Nuclear Strike uses the meshes *and* its seven-emitter particle
recipe. Confidence in the account below: high. The two real holes are the nuke's render
state and one unidentified per-stage "level" call, both named in section 5.

---

## 1. WHAT THE CARTRIDGE ACTUALLY DOES

### 1.0 The shared path (both effects)

```
HouseClass::Place_Special_Blast   RAM 0x8003883C / ROM 0x03943C
  arm which==1 (SPC_ION_CANNON)   RAM 0x800388AC / ROM 0x0394AC
      new AnimClass(52, Cell_Coord(cell) + 0x00800080)
      the literal is `addiu $a1,$zero,0x34` at ROM 0x039500, jal at ROM 0x039538  [verified]
      it never touches the firing building
  arm which==2 (SPC_NUCLEAR_BOMB) RAM 0x80038990 / ROM 0x039590
      finds the Temple (Class[+0xA5]==19), gives it MISSION_MISSILE (0x15), sets NukeDest
      the BLAST anim is created later, by BulletClass::AI
          RAM 0x801D5E04 / ROM 0x177BF4, guard ROM 0x178138, spawn jal ROM 0x1781C8,
          AnimType read from BulletTypeClass+0x23 (= ANIM_ATOM_BLAST for ClassNukeDown)

AnimClass::Draw_It   RAM 0x800331EC / ROM 0x033DEC
  reads AnimTypeClass+0x40, which the ctor at ROM 0x188660 writes straight from the
  AnimType (`sb $s7,0x40($v0)` at ROM 0x188724). There is NO remap table.
QueueAnim            RAM 0x8004BCFC / ROM 0x04C8FC
  builds a 0x29C draw command: kind 12 at +0x08, type at +0x0C, stage at +0x10, x/y at
  +0x14/+0x18.
DrawObject kind-12 arm  RAM 0x8004E0D8 / ROM 0x04ECD8
```

The kind-12 arm, disassembled word for word (ROM 0x04ED14..0x04EE78):

```
ROM 0x04ED14  addiu $v0,$zero,0x34   ; 52 = ANIM_ION_CANNON
ROM 0x04ED18  beq   -> ion arm  (RAM 0x8004E130 / ROM 0x04ED30)
ROM 0x04ED1C  addiu $v0,$zero,0x35   ; 53 = ANIM_ATOM_BLAST
ROM 0x04ED20  beq   -> nuke arm (RAM 0x8004E1C0 / ROM 0x04EDC0)
ROM 0x04ED28  j     0x8004E260       ; EVERY OTHER ANIM skips the model draws entirely
```

Both arms do the same three things:

1. call `0x8004BE00((15.0 - stage) * (1/15), 10)` (constants RAM 0x800049A4/A8 for ion,
   0x800049AC/B0 for nuke, both 15.0 and 0.06666667). See section 5, item 2.
2. compute a clip time in frames: ion `f = stage + stage` (`add.s $f24,$f24,$f24` at
   ROM 0x04ED50), nuke `f = stage * 1.1111112` (const RAM 0x800049B4, `mul.s` at
   ROM 0x04EDE8).
3. call `drawModel(slot, f, x, y)` four times.
   `drawModel` = RAM 0x8004BB9C / ROM 0x04C79C. It indexes the effects table at
   RAM 0x80099998, scales by `MODEL_SCALE[slot]` (RAM 0x80099970 / ROM 0x09A570, verified
   as `[1.0, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.0, ...]`), and multiplies the
   frame by 160.0 (RAM 0x80004824, verified) to get clip ticks.

The slot literals, raw words re-read from the ROM:

```
ion   ROM 0x04ED70 24040001  a0=1     ROM 0x04ED88 24040003  a0=3
      ROM 0x04EDA0 24040004  a0=4     ROM 0x04EDBC 24040005  a0=5
nuke  ROM 0x04EE08 24040006  a0=6     ROM 0x04EE20 24040007  a0=7
      ROM 0x04EE38 24040008  a0=8     ROM 0x04EE50 24040009  a0=9
```

**Slots 0 and 2 are never drawn.** There are seven `jal 0x8004BB9C` sites for eight draws
because the ion arm ends with `j 0x8004E254` at ROM 0x04EDB8 and sets `a0=5` in the delay
slot, landing on the nuke arm's last jal (ROM 0x04EE54) and sharing it. That reconciles
"seven jal sites" (models, callsite) with eight draw calls.

After the model draws, both arms fall into the common tail at ROM 0x04EE60 which calls the
particle spawner `0x801FC7F0`. So the Nuclear Strike draws meshes **and** spawns its
recipe; the Ion Cannon draws meshes and spawns an empty recipe, which is nothing.

**Dispute resolved: `models` and `callsite` were right about which slots are drawn
(1,3,4,5 and 6,7,8,9). `geometry` described slots 2,3,4,5 as the ion rings; slot 2 is the
fourth, top ring and it is authored but never reached.**

### 1.1 Why four passes missed this

`tools/bakery/objgraph2.py` reads node fields +0x00, +0x08 and +0x0C. For slots 1..5,
**node+0x00 is zero** and the geometry hangs off node+0x10. Verified directly:

```
slot 1 node 0x801B5618 (ROM 0x159088):
   +00 00000000  +04 00000000  +08 801B5558  +0C 00000000  +10 801B5608
slot 6 node 0x801B4A18 (ROM 0x158488):
   +00 8015CE18  +04 00000000  +08 801B4A10  +0C 00000000  +10 00000000
```

That is the whole reason `known-gap notes` records slots 1..5 as "pure transform rigs with
no geometry at all". They are not rigs. The cartridge's own node renderer
(RAM 0x8008F988 / ROM 0x090588) reads node+0x10 and I disassembled that arm:

```
ROM 0x0906FC  if (node->[0x10]) {
ROM 0x090734       emit G_DL(  node->[0x10]->[0x08]  )      ; the display list
ROM 0x090760       gfx = texAnimEval(gfx, node->[0x10]->[0x00], time, node->[0x10]->[0x0C]);
              } else if (node->[0x00]) { emit G_DL(node->[0x00]); }
```

So the payload is `{ TexAnim* ta, Gfx* src, Gfx* dst, u32 maxSplits }` and the drawn list
is `dst` at payload+0x08 (equal to `src` in the ROM). This is exactly the channel
`tools/bakery/texbook.py` already decodes for the Construction Yard's fans, including the
load-time splitter at RAM 0x80080E28 that fills the `tail` field the ROM leaves NULL.

### 1.2 ION CANNON, complete account

Four models. All positions below are the bakery's own composed world values from
`tools/bakery/anim/slot_00{1,3,4,5}.json`, which I re-derived as
`world_t = C.M * rest_world.t + C.t` and cross-checked against the raw tracks.

**Slot 1, the beam.** Node RAM 0x801B5618, payload RAM 0x801B5608, display list
ROM 0x01035E0 (RAM 0x8015FB70), 119 verts / 101 tris in five G_VTX batches
(ROM 0x102E70/30, 0x103050/3, 0x103080/32, 0x103280/32, 0x103480/22).

Geometry, measured by radius histogram over all 119 vertices about the fan centre:
three concentric open column shells spanning mesh y -140..5959 (6099 units), radii
**172 (7-sided), 222 (9-sided), 273 (9-sided)**, plus a 3-vertex flat triangle at y=0.
*(Dispute: `geometry` said "four nested heptagons", `models` said "three concentric
octagons". Both are partly wrong. Three shells; the core is a regular heptagon at
360/7 steps, the two outer shells are 9-sided at 360/9 steps.)*

Render state, read from the display list:
- `G_SETOTHERMODE_L 0x005049D8` at ROM 0x103930 (G_RM_AA_ZB_XLU_SURF2, no depth write),
  restored to `0x00552078` at ROM 0x103F78.
- `G_SETCOMBINE FFFFFF / FFFDF2F9` at ROM 0x103928 = colour PRIM, alpha TEXEL0_A. The
  texture is I8, so the streak texture is a pure alpha stencil over a flat colour.
- prim colours: `E5A6D7` lilac (ROM 0x103890), `E2FDFF` near-white cyan (ROM 0x1038D0),
  `56B9FF` sky blue (ROM 0x103CD8); restored to `FFFFFF` at ROM 0x103F80.
- `G_GEOMETRYMODE clr=0xFFFBFF` clears G_CULL_BACK for the shells, set again with
  `0x00000400`. The shells are two-sided.

Motion: FEEB0005 translation track, 2 keys, t 0..800 (keys at ROM 0x0158E54). Baked frames:

```
f0 t=1     world z(height) 4078.8
f1 t=161                   3280.4
f2 t=321                   2482.0
f3 t=481                   1683.6
f4 t=641                    885.2
f5 t=801                     91.8      <- lands
f6, f7                       91.8      held
```

**The beam falls out of the sky and plants itself.** There is also a FEEB0002 rotation
track at t 800..1120 (ROM 0x0158EB8) whose two keys are both identity, so nothing rotates.
Node rest scale (0.5102, 0.5102, 1.0) in mesh XYZ, so the column is squeezed in X and Z
and full height in Y.

Texture: a **7-split flipbook**, one split per `G_SETTIMG` in the display list, which I
verified is exactly seven (`maxSplits = 7`). Records at RAM 0x801B5560, stride 0x18:

```
splits 0..5   I8    32x32, 8 frames, ROM 0x0DF860 stride 0x400
              (0DF860 0DFC60 0E0060 0E0460 0E0860 0E0C60 0E1060 0E1460)
split 6       RGBA16 32x32, 8 frames, ROM 0x0DB860 stride 0x800
              (0DB860 0DC060 0DC860 0DD060 0DD860 0DE060 0DE860 0DF060)
all seven     times [0,160,320,480,640,800,960,1120,1280], period 1280
```

One image per animation frame, an 8-frame loop, all seven splits in lockstep. Both books
decoded to PNG by two agents independently: vertical white/blue energy streaks.
*(`callsite` called 0x0DF860 IA8; the G_SETTIMG word is 0xFD88001F, fmt=4 siz=1, which is
I8. `geometry` and `models` were right.)*

**Slots 3, 4, 5, the shock rings.** One 28-vertex / 28-triangle flat 14-segment annulus at
y=0, authored three times (four counting the undrawn slot 2). Display lists
ROM 0x0104580, 0x01049A0, 0x0104DC0 (slot 2 at 0x0104160). Inner radius ~533..572, outer
~817..854. Vertex colours: **outer ring white (255,255,255), inner ring blue (0,132,255)**.
Combiner `127E24 / FFFFF3F9` = TEXEL0 * SHADE, so those vertex colours are the ring's
colour. Render mode `0x005049D8` (XLU), geometry mode set `0x00200004`.

Texture: one shared 4-frame RGBA32 32x32 book at ROM 0x0ED260 (stride 0x1000), which
dissolves a cyan-blue band into droplets and then to nothing. Per-slot schedules, read
from the ROM:

```
slot 2 (undrawn)  times [0,  480,  960, 1440, 17440]   clip t    0.. 640   height 2974
slot 3            times [0,  800, 1280, 1760, 17760]   clip t  320.. 960   height 1979
slot 4            times [0, 1120, 1600, 2080, 18080]   clip t  640..1280   height 1015
slot 5            times [0, 1440, 1920, 2400, 18400]   clip t  960..1600   height   40
```

Each ring holds image 0 for 480 ticks after its own clip start, then advances every 480
ticks through images 1, 2, 3, then holds the empty image for the rest of a ~17.5k period.
Its FEEB0007 scale track ramps over its own 640-tick clip. Composed world scale at the end
of each clip: slot 3 x1.61, slot 4 x2.22, slot 5 x2.88 (from rest scales of ~1.4e-4 and
~2.5e-3, which is why anyone baking the rest pose alone draws a point: this answers
`models`' blocking unknown #1, and the bakery already computes it).

So the ion event, in one sentence: **a thin bright column drops out of the sky over five
frames while three blue-white shock rings fire top down 320 ticks apart at descending
heights and expand and dissolve, the whole thing over by clip tick 1600.**

Timing against the engine: ANIM_ION_CANNON declares 15 stages, the arm doubles the stage,
so stage 0..14 maps to clip ticks 0..4480. The tracks all end by 1600, so the last two
thirds of the anim's life is the finished pose holding while the per-stage level call
(section 5) counts down. *(`models` claimed every ion curve ends at t=4800; it does not.
The ion clips end at 1120/640/960/1280/1600. 4800 is the nuke group's clip length.)*

World size, using the renderer's own 1/1024 cells-per-unit and the 0.3 effect scale:
beam 1.79 cells tall and 0.08 cells across, landing from 1.2 cells up; rings 0.39, 0.54
and 0.70 cell radius at heights 0.58, 0.30 and 0.01 cells.

**The Ion Cannon spawns no particles.** Recipe index 52 (ROM 0x1B66B4 -> slot array
ROM 0x1B6004, count 10) is 160 bytes of zero, re-dumped byte for byte. The spawn wrapper
skips source id 0 with no fallback. This is correct data, not a gap in our decode.

### 1.3 NUCLEAR STRIKE, complete account

Four models sharing **one** texture: RGBA16 32x32 at ROM 0x0DB060, an orange and yellow
fire gradient. A whole-ROM aligned search for its RAM address 0x801375F0 returns exactly
four hits, all `G_SETTIMG` w1, in the four display lists below.

```
slot 6  dome / fireball cap  node 0x801B4A18  DL ROM 0x0100888   76 v /  96 t  r<=481
slot 7  tapering stem        node 0x801B4BD8  DL ROM 0x0100E68   22 v /  20 t
slot 8  collar lens          node 0x801B4D98  DL ROM 0x01011F0   30 v /  60 t
slot 9  ground cloud         node 0x801B4F94  DL ROM 0x0101E88  160 v / 160 t
```

**Dispute resolved, and it matters: slot 6 IS animated.** I read the handle class word
directly: slot 6's node+0x08 = 0x801B4A10 whose class is **0x800AC2D8 = BEEFED02**
(a curve set), while slots 7, 8, 9 carry **0x800AC2C0 = CAFEDEAD** (a static pose) plus
FEEB channel tracks. `geometry` read `"tracks": {}, "animated": false` out of
`slot_006.json` and concluded slot 6 has no motion. That is a misreading of the baker's
schema: the BEEFED02 curves are baked into the per-frame composed matrix `C`, which varies
every frame. `models` read the curves out of the ROM and was right.

Composed world motion over the 30-frame clip (my recomputation from the baked frames):

```
slot 6 dome     height -285 -> 3976   uniform scale 1.00 -> 4.676   (also 136 deg of yaw)
slot 7 stem     height -333 -> 3303   scale 2.74 -> 25.08   (peaks then eases)
slot 8 collar   does not exist before frame 8 (clip t 1280..4800)
                height 1074 at f30    scale 1.00 -> 22.18, saturating at f18
slot 9 cloud    height -475 -> 2981   scale 1.00 -> 6.585
```

That is a mushroom: a cap that climbs and swells 4.7x, a stem that stretches 25x under it,
a collar ring that appears a third of the way in and rides up the stem, and a ground cloud
that spreads at the foot. Clip 0..4800 ticks / 30 frames, which the 10/9 stage multiplier
maps exactly onto ATOMSFX's 27 declared stages (27 x 10/9 = 30). Two independent readings
of the stage count agree.

**The nuke also fires its particle recipe**, and both agents read it identically. I
re-dumped it: index 53, ROM 0x1B66C0 -> slot array ROM 0x1B60A4, 7 of 10 used, every one
source 42 = SOURCE_Fire_L, `stage` is byte +1 of the record:

```
ROM 0x1B60A4  2a 00 ...  stage 0  ( 548, 0,  279)
ROM 0x1B60B4  2a 03 ...  stage 3  (-357, 0,  423)
ROM 0x1B60C4  2a 05 ...  stage 5  ( 365, 0, -234)
ROM 0x1B60D4  2a 02 ...  stage 2  (-208, 0, -345)
ROM 0x1B60E4  2a 01 ...  stage 1  ( 245, 0,  455)
ROM 0x1B60F4  2a 08 ...  stage 8  (-254, 0,  567)   <- |offset| = 621.4, the true maximum
ROM 0x1B6104  2a 09 ...  stage 9  ( 346, 0,  356)
```

These match `game/efx_recipes.h` exactly, so nothing needs re-baking there. One
correction: `effects_mod.h:524` says the ring radius is `<= 615`; it is 621.

SOURCE_Fire_L is fully decoded at ROM 0x1B4CE8 (system 5 = System_Burn_Fire, sheet
ROM 0x9FBA0, continuous burst, life 20, size 24 growing 5.43/tick, y accel +1.82,
decayspeed 50, maxalpha 136, white). Seven ground fires in a ring 1.6 to 2.4 cells out,
lit in a stagger, around the mesh mushroom.

Chain: ANIM_ATOM_BLAST chains to recipe index 8 = ANIM_ART_EXP1 on completion (the DOS
table does the same); ANIM_ION_CANNON chains to nothing (DOS chains it to ART_EXP1, so the
cartridge deliberately removed that).

### 1.4 Two things the sweep refuted, and they need to reach `known-gap notes`

**(a) The EYE / ATWR tail at t 153280..160000 is NOT an Ion Cannon discharge.** I verified
the names out of the model-table name array (ROM 0x1DE924, delta 0x8000BE30, entry =
slot - 10):

```
slot 10 NOD_STR_TMPL_Z1     slot 11 GDI_STR_ADVCOMZ1   slot 14 GDI_STR_AGTWZ1
slot 15 NOD_STR_OBELISKZ1   slot 26 ANY_STR_PPLANTZ1   slot 27 ANY_STR_APPZ1
```

Slot 11 is the Advanced Communications Centre, the building that *unlocks* the Ion Cannon.
The `eyetail` sweep showed its tail tips two antenna masts over (one reaches exactly 90.0
degrees off vertical, its top pod ends on the ground at y = -10.2) and that the same
1.047360 / 0.908665 rad rotation motif occurs on four nodes in only these two slots. It is
a collapse, not a discharge. The Obelisk of Light (slot 15) is a single static 20-triangle
mesh with no animation at all: **the cartridge never draws a beam inside a firing
building.**

**(b) Slots 26/27 are the Power Plant and Advanced Power Plant**, not "NUKE"/"NUK2" as
`tools/bakery/support/unit_models.json` labels them. Anyone grepping our own tables for
NUKE finds power plants.

### 1.5 What happened to IONSFX / ATOMSFX

The `recipes` sweep found the anim art names in the cartridge's own adata table
(`IONSFX` ROM 0x168EC8, `ATOMSFX` ROM 0x168ED0, `ATOMDOOR` ROM 0x168ED8) and called
"where are those pixels" its single blocking unknown. **The answer is that they have no
pixels, because nothing draws them.** The kind-12 anim arm has exactly two type-specific
branches (0x34, 0x35) and every other anim jumps straight to the particle spawner at
ROM 0x04EE60. There is no billboard draw anywhere in the anim path. The DOS art-name field
survives in the ctor because the cartridge inherited `adata.cpp` wholesale; the N64 draw
path never resolves it. That also explains why FBALL1, SMOKE_M and MUZZLE_FLASH draw
nothing on the console.

Caveat worth one line: `QueueAnim` routes types 0x37 (55) and 0x40 (64) out to other paths
before the kind-12 arm. Neither is ours.

---

## 2. THE ASSET INVENTORY

Everything that must come out of the ROM, with where it lives. Nothing here needs to be
invented.

### Ion Cannon

| what | where | notes |
|---|---|---|
| beam mesh | DL ROM 0x01035E0, verts ROM 0x102E70 / 0x103050 / 0x103080 / 0x103280 / 0x103480 | 119 v / 101 t, three shells r 172 / 222 / 273, y -140..5959 |
| beam render state | ROM 0x103928 combine, 0x103930 othermode_L, prim at 0x103890 / 0x1038D0 / 0x103CD8 | XLU no z-write, colour=PRIM alpha=TEXEL0_A, cull off |
| beam glow book | ROM 0x0DF860 + k*0x400, k=0..7 | I8 32x32, 6 of the 7 splits |
| beam core book | ROM 0x0DB860 + k*0x800, k=0..7 | RGBA16 32x32, split 6 |
| beam book schedule | TexAnim RAM 0x801B5560..0x801B55F0 (ROM 0x0158FD0), times ROM 0x0158DCC / 0x0158E10 | 8 frames at 160, period 1280 |
| beam motion | FEEB0005 keys ROM 0x0158E54 (T), FEEB0002 keys ROM 0x0158EB8 (R, identity) | already baked in `tools/bakery/anim/slot_001.json` |
| ring mesh x3 | DL ROM 0x0104580, 0x01049A0, 0x0104DC0; verts ROM 0x1043C0, 0x1047E0, 0x104C00 | 28 v / 28 t each, one mesh authored three times |
| ring render state | combine `127E24/FFFFF3F9`, othermode_L `0x005049D8` | TEXEL0*SHADE, vertex colours white outer / (0,132,255) inner |
| ring book | ROM 0x0ED260 + k*0x1000, k=0..3 | RGBA32 32x32, shared by all rings |
| ring schedules | TexAnim RAM 0x801B5870 / 0x801B59B0 / 0x801B5AF0 = ROM 0x01592E0 / 0x0159420 / 0x0159560 | times listed in 1.2 |
| ring motion | FEEB0007 scale keys, `slot_003/004/005.json` | rest scales are ~1e-4; the track is mandatory |

### Nuclear Strike

| what | where | notes |
|---|---|---|
| dome mesh | DL ROM 0x0100888, verts ROM 0x1003C8 / 0x1005C8 / 0x1007B8 | 76 v / 96 t |
| stem mesh | DL ROM 0x0100E68, verts ROM 0x100D08 | 22 v / 20 t |
| collar mesh | DL ROM 0x01011F0, verts ROM 0x101010 | 30 v / 60 t |
| cloud mesh | DL ROM 0x0101E88, verts ROM 0x101488 / 688 / 888 / A88 / C88 | 160 v / 160 t |
| fire texture | ROM 0x0DB060 | RGBA16 32x32, shared, exclusive to these four lists |
| dome motion | BEEFED02 handle RAM 0x801B4A10, curves RAM 0x801B4998 | baked in `slot_006.json` as per-frame matrices |
| stem/collar/cloud motion | FEEB0005 T keys ROM 0x01584A4 / 0x0158664 / 0x0158824, FEEB0007 S keys ROM 0x0158534 / 0x01586F4 / 0x01588B4 | `slot_007/008/009.json` |
| particle recipe | ROM 0x1B60A4, 7 x source 42 | ALREADY in `game/efx_recipes.h`, nothing to extract |
| SOURCE_Fire_L | ROM 0x1B4CE8, visual template ROM 0xA35EC, sheet ROM 0x9FBA0 | already in the effects system |
| render state | **absent from all four display lists** | see section 5, item 1 |

### Not needed, and named so nobody extracts them

- ATOMICUP / ATOMICDN (object slot 97, DL ROM 0x0100250): one shared mesh, a finned
  rocket, the warhead in flight. Already covered by the bullet table.
- Slot 0 (khaki quad, DL ROM 0x0105310, I8 tex ROM 0x0F6430) and slot 2 (the top ring at
  height 2974): authored, never drawn.
- IONSFX / ATOMSFX / ATOMDOOR: no pixels exist. See 1.5.
- SOURCE_Ion_Spark and SOURCE_Lightning_1..7: referenced by no recipe and no code. The
  only live Ion_Spark emitters are four text-scripted ones in the movie overlays
  (ROM 0x032D9C5, 0x032DA5F, 0x032DAFC, 0x03A1AA1) which cannot reach the game's source
  table at all.

---

## 3. THE BUILD PLAN

### 3.1 What the BAKERY must newly extract

1. **Follow node+0x10 for geometry.** `objgraph2.parts_of_node` must, when node+0x00 is
   zero, take the display list from the node+0x10 payload's `dst` (payload+0x08; `src` at
   +0x04 is the same pointer in the ROM). `bake5.bake_struct_flipbooks` already does
   exactly this for the six structure books, so the code exists and only has to be
   generalised. `node_ptr(slot)` already reaches slots 0..9 with no table change, because
   MODEL_TABLE is 0x80099998 and the effects records are the first ten.
2. **Teach `texbook.py` multi-split books.** It reads only TexAnim[0] today and asserts
   `count <= 16`. The beam has `maxSplits = 7`, seven records, six sharing one book. Assert
   that the number of `G_SETTIMG` in the source list equals `maxSplits`, and that every
   split's `match` is one of them. All seven of the beam's splits carry identical times, so
   the effective schedule is a single 8-frame cycle.
3. **New textures into the bank:** 8 x I8 32x32 (ROM 0x0DF860 stride 0x400), 8 x RGBA16
   32x32 (0x0DB860 stride 0x800), 4 x RGBA32 32x32 (0x0ED260 stride 0x1000), 1 x RGBA16
   32x32 (0x0DB060). Twenty-one images, all 32x32.
4. **Nothing new for animation.** `tools/bakery/anim/slot_00{1,3,4,5,6,7,8,9}.json` already
   exist and are correct, including the BEEFED02 dome and the FEEB scale ramps that make
   the rings visible. The extractor already records `script_model_rate: 0.3` for these
   slots, which is `MODEL_SCALE[1..9]` out of the ROM.
5. **Populate the per-frame visibility array**, which is where the flipbook lands. See
   below.

### 3.2 What the PACK must carry: nothing new

The pack already has everything. **No format change.**

- `PKB` (pack v11+) already carries, per mesh: `frames`, `ticks_per_frame`, `clips[]`, a
  per-frame per-part 12-float matrix array, **and a per-frame per-part visibility byte
  array**. `cnc_eyes.cpp:1245-1275` reads all of it into `animMat` / `animVis`.
- The type table already maps 8-byte codes to mesh indices, and
  `bake_struct_flipbooks` / `bake_cursor_flipbooks` already establish the precedent of
  baking one mesh variant per texture-book image.

**The proposal: one mesh per effect, texture variants as extra PARTS, selected by the
existing `animVis` array.**

```
mesh "ION"   30 frames, ticks_per_frame 160
  parts  0.. 7  beam mesh, 8 texture variants (glow book frame k on the six I8 splits,
                core book frame k on split 6) -- exactly one visible per frame
  parts  8..11  ring at height 1979, 4 texture variants
  parts 12..15  ring at height 1015, 4 texture variants
  parts 16..19  ring at height   40, 4 texture variants
  animMat  per frame, the composed world matrix of the owning node (all variants of a
           part share it)
  animVis  per frame, 1 for the variant whose book slot covers that frame, 0 otherwise;
           0 for a ring before its clip starts

mesh "NUKE"  30 frames, ticks_per_frame 160
  parts 0..3   dome, stem, collar, cloud (single texture, no variants)
  animVis      0 for the collar before frame 8, which is what its clip t0 = 1280 means
```

That is 20 parts x 30 frames x 12 floats = 28.8 KB for ION and 5.8 KB for NUKE. The
visibility array is the mechanism that has been sitting unused in the format;
`eyetail` correctly observed there is nothing in the EYE/ATWR data to populate it from,
and here there finally is.

Two type codes, `ION` and `NUKE`. No per-frame codes, unlike the cursors, because these
carry motion as well as a texture swap and the vis array does both in one mesh.

### 3.3 What the RENDERER must draw

In `game/effects_mod.h`, the anim draw currently returns false for an empty recipe
(`effects_mod.h:1237`, `EFX_EMPTY_PROC` defaulting to 0), so ANIM_ION_CANNON draws
literally nothing and the only trace on screen is the `FXLC_BLAST` light that
`cnc_eyes.cpp:10631` puts in the scene with no visible source.

Add one arm, mirroring the cartridge's:

```
if (an.type == 59 /* ANIM_ION_CANNON */)  draw_effect_mesh("ION",  an.stage * 2.0f,  an.wx, an.wz);
if (an.type == 60 /* ANIM_ATOM_BLAST */)  draw_effect_mesh("NUKE", an.stage * (10.0f/9.0f), an.wx, an.wz);
```

with clip tick `t = frame * 160`, clamped at the last key (the tracks hold, they do not
loop, for every part except the dome whose BEEFED02 clip loops and which never gets there
inside 27 stages). Scale: the renderer's existing 1/1024 cells-per-unit **times 0.3**
(`MODEL_SCALE[1..9]`, in the ROM, already recorded in the anim JSON as
`script_model_rate`).

For ATOM_BLAST the existing particle path must keep running as well. The cartridge does
both: the model draws fall through to the particle spawner at ROM 0x04EE70. Do not replace
one with the other.

Blend and colour:
- Ion beam: alpha blend, depth test on, **depth write off** (that is what 0x005049D8
  means), two-sided, colour = prim, alpha = the I8 texel. Three draw groups with prims
  `E5A6D7`, `E2FDFF`, `56B9FF`.
- Ion rings: alpha blend, no depth write, colour = texel x vertex colour, vertex colours
  baked as authored (white outer, 0/132/255 inner).
- Nuke: **undecided, see section 5 item 1 and section 6 item 1.**

Tier 1 (Voodoo 2): everything here is fixed-function alpha blending with 32x32 textures,
which the Voodoo 2 draws natively. The only note for `docs/tier1-gap.md` is that the
RGBA32 ring book must be down-converted to 16-bit, which is what the rest of the pack
already does.

### 3.4 What SIGNAL the brain must provide: none, it already does

`effects_mod.h:361` already parses an `EFX|ANIM` line carrying `id`, `type`, `stage`,
`stages`, `size`, `lx`, `ly`. That is precisely the tuple the cartridge's own draw path
uses: `QueueAnim(type, stage, x, y)`.

- Ion Cannon: the brain's `HouseClass::Place_Special_Blast` spawns `ANIM_ION_CANNON` at the
  target cell, so a type-59 anim appearing in the export **is** the fire event. Latch on
  it, exactly as `known-gap notes` already suggests.
- Nuclear Strike: `BulletClass::AI` spawns `ANIM_ATOM_BLAST` at the impact cell from
  `BulletTypeClass::Explosion`. Also already exported.

**Neither effect needs a per-building fire signal.** The `known-gap notes` note that ATWR
would need one belongs to the EYE/ATWR tail clip, and that clip is a collapse, not a
discharge (1.4a). It is not part of either superweapon.

### 3.5 Order of work

1. `objgraph2` node+0x10 fall-through, plus a `texbook` multi-split extension. Prove it by
   dumping the beam's 101 triangles and the ring's 28.
2. Bake the 21 textures and the two meshes with their PKB animation and vis arrays.
3. Renderer arm + the gate below.
4. Only then: the nuke's render state (section 5 item 1), because until that is settled
   the nuke's look is a guess.

---

## 4. THE GATE

Rendered pixels and engine state, and each of these fails if the thing under it is wrong.

**G1, the beam descends (pixels).** Scripted mission, ion cannon fired at a known cell,
camera fixed. Capture at anim stages 0, 2, 4 and 6. Take the bounding box of pixels whose
colour is within the beam's own palette band (the three prim colours are all high blue,
low-to-mid red; the terrain is not). Assert:
- at stage 0 the box's bottom edge is at least 40 screen px above the target cell's screen
  point;
- the box's bottom edge decreases monotonically across stages 0, 2, 4;
- at stage 6 the box's bottom edge is within 8 px of the target cell.
Fails if the beam is baked at its rest pose, which is the single most likely wrong build.

**G2, the flipbook runs (pixels).** With the beam stationary (stages 6 and 7, where the
translation track is held), diff the pixels inside the beam's bounding box between the two
frames. Assert the count of changed pixels is above a floor (say 10% of the box). Fails if
the texture book is not applied, which is the second most likely wrong build and is
invisible to any geometry check.

**G3, the rings cascade (pixels).** At stages 2, 3, 4, 5 assert that a bright annulus is
present, that its screen-space bounding box grows between consecutive samples, and that its
centre y moves down the screen across the three rings. Fails if the scale track is dropped
(the rest scale draws a point) or if all three rings are drawn at once.

**G4, the mushroom rises and swells (pixels).** Nuke at a known cell. At frames 2, 10, 20,
29 assert the fire-coloured silhouette's height above the ground point increases
monotonically and its width increases monotonically, and that at frame 2 the collar is
absent (a horizontal band count of one connected component) while at frame 20 it is present
(two). Fails if slot 6's BEEFED02 curves are dropped, which is exactly the mistake one of
the five sweeps made on paper.

**G5, engine state.** Assert the `EFX|ANIM` export carries `type=59` at the ion target cell
within one tick of the fire command, and `type=60` at the impact cell after the nuke
projectile lands, and that the renderer's per-type draw counter for 59 and 60 is non-zero
on those frames. Fails if the trigger is latched off the wrong thing.

**G6, bake-time assertions against the ROM** (the `texbook.py` house style, not a substitute
for the pixel gates): the ion mesh must bake to exactly 101 triangles and 119 vertices; the
beam book must resolve to 7 splits over exactly 2 distinct image sets of 8; the ring mesh
must bake to 28 triangles; the nuke's four meshes must bake to 96 / 20 / 60 / 160 triangles
and all four must reference the same bank entry. Any of these failing means the walker read
something else.

None of these is an exit code. G1 through G4 are pixels, G5 is engine state, G6 is a
measured count against a number this document takes from the cartridge.

---

## 5. WHAT IS STILL UNKNOWN

1. **The nuke's render state.** None of the four nuke display lists issues a
   `G_SETCOMBINE`, a `G_SETOTHERMODE_L`, a `G_SETPRIMCOLOR` or a `G_GEOMETRYMODE`. I dumped
   slot 7's list command by command to be sure: it is `G_SETTIMG`, tile setup, `G_TEXTURE`,
   `G_VTX`, 20 `G_MODIFYVTX` and 20 `G_TRI1`, and nothing else. Every other model in the
   cartridge declares its own state (the EYE root, the Obelisk and the ion beam all do), so
   the nuke inherits whatever the previous list left. Every model list I checked *restores*
   `combine FFFCF279 / othermode_L 0x00552078` before its `G_ENDDL`, and if the nuke really
   inherits that combiner then the fire texture would not appear at all, which cannot be
   right. **Settled by:** reading the render state the scene draw (`0x8007BE30`, reached
   from `drawModel` at RAM 0x8004BB9C) sets before it runs node display lists. That is one
   function, and it decides whether the mushroom is translucent, additive or opaque.
2. **The per-stage "level" call.** Both arms call `0x8004BE00((15 - stage)/15, 10)` ->
   `0x80078294` (ROM 0x078E94), which indexes 176-byte records at RAM 0x8011EF48 (index 0),
   requires `record[0] == 1` and `v > 0.1`, then writes `10` to +0x20 and
   `(int)(10 - v*10)` to +0x3C. The same table is reached from IntroSequence, MapSel,
   FactionSel and the 0x80077DB0..0x80078514 range. A level that counts 0 -> 8 over the
   first 14 stages, shared with the menus, reads like a **screen fade or flash**, and that
   would be a real part of both effects' look. It also reads like an audio duck. The
   constant is 15 in *both* arms even though the nuke has 27 stages, so whatever it is
   stops updating less than halfway through the nuke. The table is in BSS so it cannot be
   read statically. **Settled by:** finding the consumer that reads +0x3C of those records.
3. **Whether the load-time splitter reaches these nodes.** `record+0x10` (`tail`) is 0 in
   the ROM for all eleven known books and for all seven ion splits, and I confirmed the
   evaluator at RAM 0x80080FAC returns immediately when it is 0. `texbook.py` already
   documents the splitter at RAM 0x80080E28 that fills it at load, and the Construction Yard
   fans are the proof that the mechanism is live. I did not verify that the splitter walks
   the *effects* table as well as the model table. **Settled by:** disassembling
   RAM 0x80080E28's caller and checking which node sets it iterates. If it turns out the
   effects table is not walked, the ion beam draws one static texture and gate G2 must be
   deleted rather than made to pass.
4. **Why slot 2 and slot 0 exist and are never drawn.** Slot 2 is a fully textured and
   time-tabled fourth ring at height 2974 with its own staggered schedule; slot 0 is a
   4-vertex khaki quad with an I8 16x16 soft gradient that looks like a scorch decal (both
   anims declare `isscorcher` and `iscrater`). No `jal 0x8004BB9C` passes 0 or 2, and there
   is no second draw entry: each of the ten node pointers occurs exactly once in the ROM
   and the table base 0x80099998 has one code reader. So they are authored and shipped
   dead. Fact, not explanation.
5. **The vertical anchor.** The cartridge places the anim at `Cell_Coord(cell) + 0x00800080`
   and passes x/y to `QueueAnim`; how the draw path derives the ground height under that
   point (and therefore where the beam's foot lands on a slope) is not decoded here.
6. **Sound.** ANIM_ION_CANNON declares N64 sound id 11 and ANIM_ATOM_BLAST id 15 in the
   cartridge ctor table. Neither id has been mapped to the audio bank.
7. **A tooling bug, verified, that another agent should fix.** `tools/romdump/segments.py`
   ends Movie6_1 at ROM 0x04578A0. The segment descriptor at ROM 0x00910DC reads
   `romStart 0xB0442DA0, romEnd 0xB0460190, ramStart 0x8017C100`, so the real end is
   ROM 0x0460190 and 0x88F0 = 35,056 bytes are unresolvable today. The `geometry` sweep
   found a variant copy of the ion rig inside the movie overlays (same image pointer
   arrays, different animation floats, eight ring tables where the game has four) which
   this bug partly hides. Not needed for the build; needed for the tool.

---

## 6. WHAT WOULD BE OURS RATHER THAN THE CARTRIDGE'S

If this were built today, these are the parts the ROM does not answer. They must be named
in `known-gap notes` rather than shipped as if decoded.

1. **The nuclear blast's blend mode and combiner.** Unknown 1 above. Whatever we pick,
   translucent or additive or opaque, is our choice until the inherited state is read out
   of the draw path. This is the single biggest "ours" in the build, because it decides
   whether the mushroom looks like fire or like plastic.
2. **Any screen flash.** Unknown 2. If we add a white flash on the ion strike or the
   detonation, that is ours, however plausible.
3. **The existing `FXLC_BLAST` light** that `cnc_eyes.cpp:10631` already puts in the scene
   for ION_CANNON. There is no decoded scene light in the cartridge for either effect. It
   is ours today and stays ours.
4. **Sound.** Unknown 6. Any sample we attach is ours.
5. **Slot 2 and slot 0.** If we draw the fourth ring or the khaki ground decal because they
   look better, that is an addition, not a restoration. Recommendation: do not draw them,
   and record them as authored-but-dead.
6. **Behaviour past the tracks.** The ion tracks end at clip tick 1600 while the anim lives
   15 stages (tick 4480). The cartridge holds the last key; if we instead fade the meshes
   out over the remaining stages because holding looks wrong, that fade is ours.
7. **Ground conforming.** Unknown 5. Whatever we do about a beam landing on a slope is ours
   until the anchor is decoded.
8. **Anything drawn for IONSFX / ATOMSFX / ATOMDOOR.** These names exist in the cartridge
   and address nothing. If a future session draws a billboard for them, that is invention.

---

## 7. CONFIDENCE, BLUNTLY

The beam and the mushroom were both found, and the account in section 1 is measured, not
inferred: the dispatch words, the slot literals, the node fields, the handle classes, the
display lists, the render state, the texture books and their schedules, the composed
per-frame motion and the two particle recipes were all re-read from the ROM for this
document. The two effects can be built from this file.

What I would not claim:
- The nuke's appearance is not fully specified until unknown 1 is settled. Build the ion
  cannon first; it is complete.
- The flipbook (unknown 3) is inherited belief from `texbook.py`, which earned it on the
  Construction Yard fans, but I did not personally confirm the splitter walks the effects
  table.
- Two of the five sweeps reached wrong conclusions on points that mattered (slot 6 not
  animated; slot 2 drawn; the ion clip ending at 4800; the I8 book called IA8), and one
  sweep's central conclusion (that the effects are 2D billboard art named IONSFX and
  ATOMSFX) is refuted by the draw path. That is the normal shape of a five-way sweep and it
  is why every disputed point above was re-measured rather than voted on.
