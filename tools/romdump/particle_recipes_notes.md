# N64 particle recipe decode — n64specialeffect.c + n64particle.c

Read-only RE pass over `data/rom/cnc_eu.z64`. Companion to `particle_recipes.json`
(machine-readable, renderer-driving). This file records the record layouts, addresses,
and HOW each field was verified, so the JSON can be trusted or re-derived.

Address convention: CodeOverlay RAM = ROM + 0x8005E210 (ROM 0x1643F0-0x1B67D0,
RAM 0x801C2600). Resident RAM = ROM - 0x1000 + 0x80000400.

## The three data structures

### 1. Anim table — 66 entries, CodeOverlay ROM 0x1B6444 (RAM 0x80214654)

Entry = 12 bytes `{ char* name, SourceSlot* slots, u32 nslots }`. Names are the ROM's
own `ANIM_*` strings (ANIM_FBALL1 ... ANIM_OILFIELD_BURN). nslots = 4 for all but
ANIM_ION_CANNON and ANIM_ATOM_BLAST (10).

SourceSlot = 16 bytes `{ u8 source_id, u8 fire_stage, u16 pad, s32 dx, s32 dy, s32 dz }`.
source_id 0 = empty. Offsets are world units (**cell = 256**, not 192 -- an earlier draft
of this file said 192 and that is wrong; the cell pitch is 256 per heightmap_notes.md and
docs/n64-camera-math.md, and building on 192 makes every offset, speed and size 33% too
large); **dy is added to the terrain
height at (x,z)** — the spawn wrapper calls the ground-height function (RAM 0x801F7DFC)
and uses `height + dy`, ignoring the anim's own y. 16 of the 66 anims have all slots
empty (MUZZLE_FLASH, FBALL1, SMOKE_M, ION_CANNON, the crate anims, ATOM_DOOR,
MOVE_FLASH, OILFIELD_BURN...): the console really spawns nothing extra for these — the
anim's own billboard art is the whole effect. ATOM_BLAST is 7 x SOURCE_Fire_L at ring
offsets with staggered fire_stage 0,3,5,2,1,8,9.

### 2. Source table — 65 records ("SOURCE_" enum 0x00-0x40), CodeOverlay ROM 0x1B41C0 (RAM 0x802123D0)

Record = 0x44 bytes. The ROM names them itself (`SOURCE_None, SOURCE_Sam_N..NW,
SOURCE_Gun_N..NW, SOURCE_Flame_N..NW, SOURCE_Debris, SOURCE_Debris2, SOURCE_Sparks,
SOURCE_Dirt, SOURCE_Shock, SOURCE_Burn_Fire, SOURCE_Burn_Smoke, SOURCE_Ion_Spark,
SOURCE_Burn_L/M/S, SOURCE_Flame_Spout, SOURCE_Napalm_S/M/L, SOURCE_Fire_S/M/L,
SOURCE_Smoke_L/M, SOURCE_Smokey, SOURCE_Smoke_Burn_L/M/S, SOURCE_Smoke_Spout,
SOURCE_Napalm_Smoke_S/M/L, SOURCE_Explode_S/M/L, SOURCE_Explode_Debris,
SOURCE_Lightning_1..7, SOURCE_LandingSmoke`).

| off | type | meaning | how verified |
|---|---|---|---|
| +0x00 | char* | name (SOURCE_*) | read |
| +0x04 | ParticleSystem* | 0x800EC1B0 + idx*0x34 | registry (below) |
| +0x08 | int | visual-variant count | spawn code clamps 5th caller arg to it |
| +0x0C | void* | visual sub-record array (0x40B each, resident RAM) | `sll v1,s5,6; addu` in spawn |
| +0x10 | float | particles emitted per tick (trunc'd) | integrator loop bound `trunc.w.s` at 0x8004F068 |
| +0x14 | float | per-tick multiplier on that count | `mul.s` + writeback at 0x8004F3F8 |
| +0x18 | int | burst_ticks; 0 = continuous | action life decrement 0x8004F404; fire-condition branch 0x801FC8B4 |
| +0x1C/+0x20 | float | speed base / random | `f22 = base + rand8/256*rand` 0x8004F0F8 |
| +0x24/+0x28 | int | life base / random (ticks) | same pattern 0x8004F11C; encoded into particle+0x30 |
| +0x2C/+0x30 | float | size base / random | same pattern 0x8004F150; lands in particle+0x28 (the field the integrator grows by particle+0x2C) |
| +0x34/+0x38 | float | angle range A (radians) — feeds vel z | trig at 0x8004F170-0x8004F1B8 |
| +0x3C/+0x40 | float | angle range B (radians) — feeds vel x | same |

Velocity formula (from the trig sequence; sinf = RAM 0x80060760, cosf = 0x8005C0E0):

```
a = uniform(angleA_min, angleA_max)      # +0x34/+0x38
b = uniform(angleB_min, angleB_max)      # +0x3C/+0x40
vel = speed * ( sin(b), cos(b)*cos(a), sin(a) )     # (x, y-up, z)
spawn_pos = emitter_pos + 3.0 * vel                 # vec scale by 3.0f at 0x8004F1B4
```

Sanity anchors: SOURCE_Sam_N has a∈[-1,0] ⇒ vel.z ∈ [-0.84,0]·speed (north = -z) with
strong +y — an up-and-north smoke plume; the eight Sam_/Gun_/Flame_ variants differ
ONLY in these two ranges, exactly one compass step apart. SOURCE_Explode_* = symmetric
±0.5 rad cone, straight up.

### 3. The 10 particle systems — BSS gGameParticleSystems 0x800EC1B0, stride 0x34

Name binding is the ROM's own registry at **ROM 0xA2BD4**: pairs `{sys*, name*}` using
the `gGameParticleSystems.System_*` strings (resident rodata 0x5D80-0x5F10):

| idx | RAM | name | art (from init, ROM 0x57544 / RAM 0x80056944) |
|---|---|---|---|
| 0 | 0x800EC1B0 | Intensity | 32x32, 1 frame, tex ROM 0x9BFA0 — Sam_*/Gun_*/Sparks/Dirt puffs |
| 1 | 0x800EC1E4 | FireBall | 16x8, **12 frames**, tex ROM 0x9C3A0 — Explode_S/M/L, LandingSmoke |
| 2 | 0x800EC218 | VehicleDamage | explosion system; chunk table 0x800A1FA0 (7 chunks) — SOURCE_Debris |
| 3 | 0x800EC24C | StructureDamage | explosion system; chunk table 0x800A1FBC (7) — SOURCE_Debris2 |
| 4 | 0x800EC280 | Shock | 64x64, 1 frame, tex ROM 0x9DBA0 |
| 5 | 0x800EC2B4 | Burn_Fire | 12x16, **8 frames**, tex ROM 0x9FBA0 — all fire/burn/napalm |
| 6 | 0x800EC2E8 | Burn_Smoke | 16x16, **6 frames**, tex ROM 0x9CFA0 — all smoke |
| 7 | 0x800EC31C | Ion_Spark | 16x16, 1 frame, tex ROM 0xA07A0 — Ion_Spark + Lightning_1..7 |
| 8 | 0x800EC350 | Debris | 8x16, 8 frames, tex ROM 0xA0BA0 — no source uses it directly; default chunk-trail system |
| 9 | 0x800EC384 | FlameThrower | 8x16, 8 frames, tex ROM 0xA1BA0 — Flame_N..NW |

Frame counts match the already-proven efx art (FireBall 12-frame burst, Burn_Fire
8-frame lick, Burn_Smoke 6-puff — same counts effects_mod.h documents). The `draw_type`
1/2/3 in the JSON is the init's sp+0x10 argument; type 3 (Ion/Debris/FlameThrower)
takes a different in-spawn path (fn 0x8004E8AC instead of the plain allocator).

## The code path (all verified by disassembly)

- **Spawn wrapper** RAM 0x801FC7F0 (CodeOverlay ROM 0x19E5E0) — the only xref to the
  anim table. Args (animId, stage, x, z, variantIdx). Per slot: skip if stage <
  fire_stage; if source.burst_ticks != 0 additionally require stage == fire_stage
  (single burst); continuous sources re-fire every stage.
- **Emitter alloc** RAM 0x8004E6DC (n64particle.c line 199): 0x40-byte node
  {next, pos xyz, vel xyz, accel xyz, size, sizeGrow, life/count bytes, RGB 0xFF...}
  linked into system list at sys+0. From the anim path vel/accel = 0, colour white.
- **Action alloc** RAM 0x8004E9A8 (lines 327/334): copies the source-record fields into
  a 0x40-byte "action" linked at sys+8.
- **Action integrator** RAM 0x8004EFE4: per tick, per action: emit trunc(count)
  particles (each built by re-using the 0x8004E6DC allocator: pos = emitter pos +
  3*vel, velocity from the trig formula, life/size randomized); then
  `count *= per_tick_multiplier`, `remaining_ticks--`, freed at 0 (walker 0x8004F458).
- **Node integrator** RAM ~0x8004ED00: pos += vel; vel += accel (vec add fn
  0x8007AF68); size += sizeGrow; life--.
- **Debris special case** (spawn wrapper 0x801FC9D0): if the source's system is
  VehicleDamage or StructureDamage, an extra 0x80-byte ParticleExplosion object is
  built: visual = the anonymous chunk record 0x800A202C, constants
  {2, 1, -2, 20, 5} from CodeOverlay ROM 0x16B3E0-0x16B3F0 (RAM 0x801C95F0) —
  chunk y-accel -2/tick, elasticity 0.7 (from the chunk visual record), and each chunk
  carries a child emitter chain (the smoke trail behind flying debris in the reference video).
  For non-debris sources that arg defaults to &System_Debris (0x800EC350).

## Visual sub-records (the "pcltmp" records)

0x40 bytes each, resident data (e.g. Debris chunks 0x800A202C ROM 0xA2C2C). First
0x20 bytes are runtime scratch (zero in ROM). Constants:
+0x20 float y-accel per tick (-2 debris chunks, +0.4 rising smoke/puffs),
+0x28 float damping/elasticity (0.7 debris, 1.0 sprites), +0x24/+0x2C more floats
(fade/spin candidates, unnamed), +0x30.. packed bytes (colour/blend/frame flags,
raw in the JSON). A separate NAME registry for these exists at ROM 0xA4170
(pairs {char* "pcltmpXxx", void* record}) — note its data pointers are the
DEBUG-MENU labels and sit one 0x80 block off from the source-table pointers; the
authoritative binding is the source record's +0x0C, not that registry.

Struct-size debug strings ("Size of Particle_t = %d" etc., rodata 0x55C0-0x5644)
belong to n64particle.c's own prints; the sizes observed in code are: emitter/particle
node 0x40, action node 0x40, ParticleExplosion object 0x80.

## What remains open

- **Tick rate**: fire_stage and all "per tick" values advance on the effect tick that
  also advances anim stages. Whether that is every frame or every anim-stage advance
  (DOS anims advance at their own rate) should be confirmed against video timing;
  the safe bet is the anim-stage clock, since the wrapper receives `stage` directly.
- The exact roles of visual sub-record +0x24/+0x2C and the packed bytes at +0x30..0x3F
  (colour/blend/frame-select). Raw values are preserved in the JSON.
- The Intensity system (idx 0) also runs the screen-flash path (code at 0x8004C830
  reads tables at 0x8009 9920-area); not needed for world particles.
- ANIM_ION_CANNON has 10 slots, all empty: the ion beam/lightning is spawned from
  code, not this table (Lightning_1..7 sources exist but no anim references them —
  find the code site that fires them when implementing the ion cannon).
- s5 (5th wrapper arg) selects the visual variant (clamped to nsub); from the anim
  driver it is effectively 0. Only Debris/Debris2 have 2 variants (grey / house-colour
  chunk?).
- The per-system particle DRAW (billboard vs 3D chunk vs lightning line) is in the
  type-1/2/3 branches; not decoded — the renderer already has the textures (efx.pack)
  and the model-viewer has the chunk meshes if needed.

## Provenance of every number in particle_recipes.json

Generated by a script that reads the ROM directly; tables at ROM 0x1B6444 (anims), 0x1B41C0 (sources), 0xA2BD4 (system names),
sub-records per source +0x0C. Nothing hand-copied except the system init args
(transcribed from the disassembly at ROM 0x57544) and the field names above.
