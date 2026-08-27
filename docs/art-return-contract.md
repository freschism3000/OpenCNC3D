# Sending a model out to an artist, and taking it back

What it takes to raise a vehicle's polycount and texture resolution. Three things: what the vehicles ARE, what has to
come back for a model to be usable, and what does not exist yet.

**Read the last section first if you are about to promise a turnaround.** The export
side is built and proven. The import side is NOT built. A model can go out today;
nothing takes one back in.

---

## 1. What a vehicle is, in this project

There is no model FILE. A cartridge vehicle is a **Nintendo 64 F3DEX2 display list** in
ROM: a stream of RSP commands that loads vertex batches and draws triangles from them.
`tools/bakery/bake5.py` walks that stream once per build, applies each scene-graph node's
rest transform, and writes the result into the mission `.pack` as a flat triangle list.
The renderer draws that list. So the chain is:

```
cartridge ROM  ->  display list  ->  bake5.py  ->  .pack triangle list  ->  cnc_eyes
```

Every number below is measured off `game/SCG01EA.pack`.

| | |
|---|---|
| Units | cartridge model units. **1 map cell = 1024 units.** `MODEL_SCALE` is 1/1024 |
| Axes | x east, y **up**, z south, right handed |
| Geometry | unindexed triangles, 3 vertices each, no welding, no normals stored |
| Per vertex | position, one UV, one RGBA colour |
| Vertex colour | the cartridge's **baked lighting**. The renderer multiplies the texture by it |
| Textures | 16x16 for most vehicle parts. The whole object texture bank is 0.76 MB at 16-bit |
| Typical vehicle | 38 to 139 triangles. Medium Tank 91, Mammoth 138, Orca 139, Missile launcher 38 |
| Whole mission pack | 188 meshes, 7,737 triangles, 271 textures |

Per-triangle the pack also stores a **draw mode** (opaque / cutout / translucent /
shadow) and a **wrap mode** (repeat, clamp or mirror, taken from the RDP's own cm bits).
Both are properties of the source display list, not of the texture, and both have to be
restated by hand for new art.

## 2. There is no skeleton

No bones, no skinning, no weights. Nothing in the pack or the renderer can deform a
vertex by a bone.

What exists instead is a **rigid part hierarchy**, one part per cartridge scene-graph
node:

- a part is a contiguous run of triangles plus a **pivot point**
- a part has a **role**: `static`, `turret` (rotates to the engine's turret facing) or
  `rotor` (spins)
- the pack's triangles are stored **already posed** by the node's rest transform

So the Medium Tank is two parts: hull (50 triangles, pivot at the origin) and turret
(41 triangles, pivot at 3.7, 212.8, -34.0). The Mammoth is the same shape, 62 + 76.
The Chinook is hull plus two rotors. Most vehicles have no turret part at all and turn
as one piece.

Buildings additionally carry **PKB node animation**: per-node keyframed transforms
resampled onto a fixed frame grid, stored as a delta against the rest pose. Twenty
meshes in a mission pack have it, the Construction Yard crane among them. No vehicle
does, apart from the MCV's deploy rig, which is decoded but not built.

## 3. What goes out

```
python3 tools/art/fbxout.py game/SCG01EA.pack MTNK -o ~/Desktop/art
```

Emits a **binary FBX 7.4** (Blender's importer reads binary only, not ASCII), one PNG
per texture the model uses, and a `.json` manifest carrying the pivots, the roles, the
per-node triangle counts and the draw modes.

The FBX carries the part hierarchy as real nodes with real transforms, which is the
reason it exists: the renderer's own `dumpobj` verb writes a perfectly good OBJ, but OBJ
has nowhere to put a **pivot**, and a turret without its pivot cannot be hooked back up.

## 4. What has to come back

1. **Keep the node names.** `MTNK_p0_static`, `MTNK_p1_turret`. They are how the parts
   are matched back to roles.
2. **Keep the pivots.** Move the geometry, not the node transform. If the turret must
   sit somewhere else, say so and the game's pivot changes with it.
3. **Triangles only.** Quads and n-gons have to be triangulated somewhere; do it before
   sending so the topology is the artist's decision and not the importer's.
4. **Vertex colours are load-bearing, not decoration.** The renderer has no lights: it
   multiplies texture by vertex colour and that is the entire shading model. A model
   delivered with no vertex colours draws flat and wrong. Deliver white if the intent is
   "let the texture carry it", but deliver something.
5. **UVs in 0..1 over the PNG as delivered.** The exporter has already undone the
   cartridge's padded-versus-used texture distinction, so what is seen is what there is.
6. **Textures: power of two, 256 maximum on a side, 8:1 aspect maximum.** This is the
   3dfx Voodoo 2 limit, not a stylistic one, and it is enforced by hardware that fails
   SILENTLY: a texture that will not upload makes the triangles using it disappear.
7. **Two texture sets, or a house mask.** The cartridge recolours GDI and Nod through
   two palettes, and 64 of a pack's 271 textures differ between them. All three Medium
   Tank textures do. A modern RGBA PNG has no palette, so the recolour is lost unless
   the artist delivers a GDI set and a Nod set, or marks which texels are house colour.
8. **Say which faces are cutout, translucent or shadow.** Put them in their own material.
   Sixty-nine faces over sixteen models are translucent in the cartridge (canopies,
   windscreens, the Weapons Factory bay, both power plants' coolant pools) and they were
   drawing solid until recently because a texture format cannot tell a canopy from a
   panel.

### Budget

**Tier 2 (modern desktop) has room.** The renderer draws meshes in immediate mode, so
the cost is per vertex and linear, and current whole-pack geometry is under 8k triangles.
Ten times the vehicle polycount is not a problem there.

**Tier 1 (Win98, Voodoo 2) is the constraint, and it is a texture constraint before it is
a triangle one.** The TMU is 4 MB, is not virtualised, and overrunning it draws
untextured rather than failing. Today a mission's texture bank is 2.85 MB at 16-bit, of
which 2.1 MB is the single 1024x1024 terrain atlas, leaving 0.76 MB for every object in
the game. Raising every vehicle from 16x16 to 64x64 costs about 0.5 MB and fits. Raising
them to 128x128 does not.

## 4b. Animation, which is three separate systems

Only the first of these travels in the FBX. A remodelled BUILDING has to answer all three.

### 1. Node animation (PKB), and it IS in the FBX

Rigid per-node keyframes, resampled by the baker onto a fixed frame grid and stored as a
**delta against the rest pose**. `fbxout.py` turns each part's track into real FBX
translation / rotation / scale curves on that part's node, so the clip plays on the
artist's timeline. Channels that never move are not written, so what appears is what
actually animates.

The Power Plant is the simple case. 21 frames at the 15 Hz sim tick, one moving part: a
**two-triangle translucent plate** inside the cooling tower, pivot (-7.8, -9.6, -1.7),
which rises about 70 model units and sinks back. Pure translation, no rotation, no scale.
The Advanced Power Plant is the same clip driving two such plates. The codebase calls
them the coolant pools; they are the faces that were drawing solid until recently.

Elsewhere in the pack rotation and scale ARE used (the Construction Yard crane, rotors,
fan plates), which is why the exporter decomposes a full 3x4 rather than lifting a
translation out of it.

There is also a **visibility track**: a node can be marked not-drawn on a given frame,
because a node before its first keyframe is absent rather than sitting at rest. The
manifest lists those frames per node.

### 2. Construction sections (PK7), and they are NOT in any FBX

During `BSTATE_CONSTRUCTION` the renderer draws a growing prefix of the mesh's triangles,
which is how the console assembles a building piece by piece. The pieces are the display
list's own **G_VTX vertex batches**: the Power Plant has **10**, the Advanced Power Plant
**14**. They are read out of the cartridge and have no counterpart in an FBX.

**A remodelled building has no G_VTX batches, so its assembly order has to be authored by
hand or the building will pop up in one lump.** The manifest lists the current section
boundaries as triangle indices so the shape of the existing order is at least visible.

### 3. Texture books, which move nothing

Some motion is not geometry at all: the Construction Yard's fan discs animate by cycling
textures. If a remodel replaces such a part with real spinning geometry, that is a change
to the renderer, not just to the art.

### And the thing to know before spending time on any of it

**The engine picks the frame, not the clip.** Each structure type has its own decoded
formula (`docs/animation-drivers.md`, the arm table at RAM `0x80003D18`). Both power
plants use `frame = stage < 20 ? stage : 39 - stage`: a **triangular fold**, played
forward and then backward. Author a clip that reads correctly in both directions.

**And on real hardware most of this never plays.** The cartridge ships
`Anims[BState] = {0, 1, 0}` for all six states of all 64 structure types, so the console's
own counter never advances and the buildings stand still. The free-running counter that
walks these clips is **ours**, a deliberate choice, recorded as a known gap.
The Power Plant's bob is something this project decided to show. That is worth knowing
before an artist spends a day on a clip.

## 5. What does not exist

**There is no import path.** `bake5.py` reads the ROM and only the ROM. Nothing in the
repo can put an artist's triangles into a pack, and no format, tool or code path is
waiting for them. Getting one model back in needs, at minimum:

- a mesh override that reads the returned FBX and replaces a type's triangle list,
  part table and pivots
- texture-bank injection: the artist's PNG becomes a bank entry, with the house variant
  either supplied or derived, and the Voodoo 2 rules checked at bake time rather than
  discovered on the box
- per-triangle draw mode and wrap mode carried from the artist's materials
- for buildings only, a construction section table, which today comes from the display
  list's own vertex batches and has no counterpart in an FBX
- a gate that renders the replaced model and compares it against the cartridge's, so a
  bad import is caught by pixels rather than in review

**And it is a charter question before it is a work estimate.** The charter's first line
is "a faithful PC recreation of the Nintendo 64 presentation". Higher-poly, higher-res
vehicles are a deliberate departure from that, not an improvement to it. Worth doing as
an explicit, switchable second art set rather than by quietly replacing the cartridge's.
That is a project call.

---

## 6. Bones and vertex weights

**Yes, for Enhanced mode, and the renderer is closer to it than it looks.**

### Why it is not a rewrite

`draw_mesh` already transforms **every vertex on the CPU, every frame**. Look at the loop:
each vertex goes through the runtime 3x4, then the baked animation delta, then the part
spin about its pivot, then the facing rotation, then the wobble, then `MODEL_SCALE` and
the ground height, and only then reaches `glVertex3f`. There are 93 `glVertex3f` call
sites in the renderer and **zero vertex buffers and zero vertex shaders** in the object
path (shaders exist only in the Tier 2 post chain).

Skinning is the same loop with the single part matrix replaced by a weighted sum of
several bone matrices. It is an extension of a transform stack that already exists, not a
new pipeline.

### What it costs, measured

Immediate-mode throughput on a Radeon PRO W6800X, same vertex shape
the renderer emits, `glFinish`-bracketed:

| triangles / frame | ms, rigid | ms, 4-bone skin | vertices / sec |
|---|---|---|---|
| 24,000 | 1.20 | 1.38 | 60M / 52M |
| 96,000 | 4.03 | 5.14 | 71M / 56M |
| 192,000 | 7.96 | 10.19 | 72M / 56M |
| 384,000 | 15.76 | 20.43 | 73M / 56M |

**A four-bone blend costs about 23% of throughput.** At 60 fps, roughly **192,000 skinned
triangles per frame** fits with room to spare; 384,000 does not. A whole mission pack is
**8,172 triangles today**, so the headroom is around twenty times the current art, with
skinning, without leaving immediate mode. Measured on one fast desktop GPU: a slower
machine scales down, and this is the ceiling on draw submission, not on fill rate.

### What has to be built

1. **Pack format.** A bone table (name, parent, rest transform) and per-vertex indices and
   weights. New blocks go at the **FRONT** of the tail and never between two existing ones,
   because every tail read in `cnc_eyes.cpp` is an EOF-relative seek. The last time that
   rule was broken the terrain drew four columns east for three format versions and no
   gate objected. Current magic is `CNC3DPKE`, version 14.
2. **FBX import.** `fbxout.py` already writes the node hierarchy; the return path needs to
   read `Deformer`/`SubDeformer` (Skin and Cluster) nodes back out.
3. **The renderer.** One weighted blend in place of one matrix, in the loop above.
4. **The clip.** Today's animation is the cartridge's own per-node rigid deltas. A skinned
   model's motion has to be **authored by the artist**; there is nothing in the ROM to
   derive it from.

### Two things skinning does NOT replace

**The engine still drives the rigid parts.** A turret turns to the engine's turret facing,
a rotor spins on the engine's clock, a building assembles by drawing a prefix of its
sections. A skinned vehicle still needs a **turret bone the engine can drive by facing**
and still needs a construction order. Skinning is additive.

**And most of this art has nothing to deform.** Nothing on a power plant bends; its whole
clip is one flat plate translating. For a hull-plus-turret vehicle, a rigid bone hierarchy
with no weights is simpler and matches what the engine actually drives. Weights earn their
keep on continuous deformation, and the honest candidate for that in this project is
**3D infantry**, which today are 2D billboarded sprites exactly as the N64 had them.
That is a much bigger design decision than a remodelled power plant.

### Tier 1

Enhanced mode already exists as a switch (`DOPT_V_CLASSIC` / `DOPT_V_ENHANCED` on the
Visuals page). The clean declaration is: **the enhanced art set,
and therefore skinning, is Tier 2 only, and Windows 98 keeps the cartridge's rigid-part
art.** That is a valid Tier 1 answer and costs nothing to keep.

If skinning on the Voodoo 2 were ever wanted, CPU skinning is the only option, because
Glide is fixed function with no vertex stage at all. **Estimate, not a measurement:** a
four-bone blend on a Pentium II class CPU lands somewhere around 10,000 to 20,000 skinned
triangles per frame at 30 fps. That is above the whole current pack, so it is not absurd,
but nobody has measured it and it should not be planned on until somebody does.
