# Wall model slots — SETTLED (visual proof + table archaeology)

Read-only RE pass. ROM: `data/rom/cnc_eu.z64` (big-endian). This settles bug #13's
open question: which model-table slots hold the five wall
types. The old guesses (SBAG=93..WOOD=97) are WRONG — those are the proven bullet
models. The true wall meshes live in the 150-209 range, four connectivity variants each.

## Verdict

| Wall | Slots | Variant order | Mesh look (rendered, textured) | Texture (ROM, fmt) |
|---|---|---|---|---|
| SBAG sandbag    | **153-156** | 153 straight, 154 L-corner, 155 T, 156 cross | low, thick run of tan/camo bags — matches the reference video frame p040 exactly | 32x16 CI8 @ 0xF9B30 |
| CYCL chainlink  | **161-164** | 161 straight, 162 L, 163 T, 164 cross | grey diamond-lattice wire panels between posts, straight piece is a zero-depth plane | 16x16 CI8 @ 0xE1F60 |
| BRIK concrete   | **176-179** | 176 straight, 177 L, 178 T, 179 cross | tall solid grey slab wall | 32x32 CI8 @ 0xF4B10 |
| BRIK damaged    | **206-209** | same order | same wall with crumbled/broken top — SAME texture as intact 176-179 | 32x32 CI8 @ 0xF4B10 |
| BARB barbwire   | **184-187** | 184 straight, 185 L, 186 T, 187 cross | thin dark wire strands on posts, zero-depth plane | 16x8 CI8 @ 0xE1C60 |
| WOOD fence      | **188-191** | 188 straight, 189 L, 190 T, 191 cross | brown split-rail ranch fence, zero-depth plane | 16x32 CI8 @ 0xF8850 |

Variant order is uniform: `base+0` straight, `+1` L-corner, `+2` T-junction, `+3` cross.
With rotations that covers the DOS 16-frame connectivity space (the DOS frame per cell is
already in the engine's overlay data; renderer maps frame -> {variant, rotation}).

Neighbouring discoveries made while eliminating candidates:

| Slots | What they really are |
|---|---|
| 157-160 | flat ground quads, same 4-variant footprints, every vertex colour `(0,0,0,89)` and untextured — SBAG's wall SHADOW set. All four are flat at y≈5 model units. Pairing to SBAG is by table adjacency and by elimination (BRIK's is proven below), not by an exact footprint match |
| 180-183 | BRIK's shadow 4-set. Slot 180's footprint `x[-512,511] z[-80,88]` is BYTE-IDENTICAL to BRIK's straight (176), and 182's x range `[-514,509]` is byte-identical to BRIK's T (178). **BUT NOT ALL FOUR ARE BLACK:** 180 and 183 carry `(0,0,0,89)` as expected, while **181 and 182 carry opaque `(211,211,211,255)` / `(255,255,255,255)`** and render as solid LIGHT plates. Corrected; measured with `pk4mod.bake_mesh`. A renderer that draws 181/182 in the blended shadow pass will paint bright white L and T patches on the ground where a shadow belongs |
| 165-175 | **CORRECTED — NOT a "civilian white lattice fence".** This is CYCL's SHADOW set. Every triangle is typed MODE_SHADOW (mode 2), vertex colour is `(13,13,13,255)`, height is flat at y≈1-2 model units, and the 32x16 texture is pure WHITE RGB (1 distinct RGB triple) with **111 distinct alpha values** — an intensity shadow mask, not lattice art. Eleven pieces is exactly the unrotated connectivity set (2 straights, 4 Ls, 4 Ts, 1 cross) |
| 192-202 | **CORRECTED — NOT a "civilian light rail-fence".** WOOD's SHADOW set, same construction: all mode 2, vertex colour `(12,12,12,255)`, flat at y≈±2, 32x16 pure-white texture with 83 distinct alpha values |
| 98      | one-cell grey cube with frame = STEEL crate (8x16 CI8 @ 0xFC7D0) |
| 99 (-105) | one-cell wooden slat crate = WCRATE (8x16 CI8 @ 0xE6730); slots 99-105 are seven table entries sharing one record |
| 142     | the floating green "PRIMARY" text label (68x12 CI8 texture @ 0xF8C50 is the word itself). NOTE: unit_models.json `_meta` calls 142-152 "flat green tiberium overlay quads" — 142 at least is actually the PRIMARY marker |
| 151     | untextured grey flat quad (placement/selection quad?) |

## Evidence chain

1. **Structure-type records (walls are records 59-63 at ROM 0x165FDC + i*0xAC).**
   The u16 at (name-6): buildings carry the unified enum id (TMPL=0 .. MISS=23,
   V01..V37=45..81 — reverified for all 64 records). The walls carry
   **1000..1004** (0x3E8..0x3EC): SBAG=1000, CYCL=1001, BRIK=1002, BARB=1003, WOOD=1004.
   So `model = id + 10` can never apply; the id is a sentinel range. (An earlier pass
   read only the low byte — "232-236" in unit_models.json `_meta` — same field.)
   The trailing u16 of each wall record (0xA6..0xAA) is the N64 name-string id, the same
   value the OverlayTypeClass registration passes at sp+0x10 (below); it is not a model.

2. **The runtime model table is 229 entries, not 236.** Resident data ROM 0x9A598
   (RAM 0x80099998), stride **16**, record = {w0,w1,w2, ptr@+0xC}; `ptr` is the per-model
   record in GameModelsGeometry RAM (delta 0x8005C590). Verified: consecutive known slot
   ptrs sit exactly 16 bytes apart (slot0 @0x9A5A4, slot1 @0x9A5B4, ... slot99-105 seven
   consecutive identical ptrs @0x9ABD4..0x9AC34). Immediately after slot 228 the pointer
   spacing switches to 20: a SECOND table of 19 records, stride 0x14, base RAM 0x8009A7EC
   (ROM 0x9B3EC), ptr also at +0xC — walked by the same init function (loop at ROM
   0x571A8, count 0x13). n64_model_table.json's entries 229-235 are misparsed bytes of
   this second table; **model indices 242-246 do not exist**, killing the "232-236 + 10"
   theory for good. The init loop at ROM 0x571D4 walks 219 records from slot 10
   (RAM 0x80099A38, count 0xDB) and fills three 219-entry u16 BSS arrays at 0x800EBC88 /
   0x800EBE40 / 0x800EBFF8 (handle/size tables; accessors at CodeOverlay ROM 0x18801C-
   0x1880D8 index them by the object's model index word).

3. **Registration passes no wall model.** The OverlayTypeClass registration chain is at
   CodeOverlay ROM **0x1950B0-0x1952xx**, ctor RAM **0x801F2F20**, called once per
   overlay type with a1=OverlayType (CONC=0, SBAG=1, CYCL=2, BRIK=3, BARB=4, WOOD=5),
   a2=name ptr (pool at ROM 0x16A380: CONC SBAG CYCL BRIK BARB WOOD TI1..TI12 SQUISH
   V12..V18 FPLS WCRATE), a3=TXT id, sp+0x14=LandType (walls 4=LAND_WALL, CONC
   1=LAND_ROAD), sp+0x18=damage levels (1/2/3 for SBAG/CYCL/BRIK), sp+0x1C=damage
   points (20/10/70), sp+0x10=N64 name-string id (0xA6..0xAA), remaining stack args
   booleans. Static 0x30-byte objects descending from 0x8021B9B0 (BSS). No model index
   anywhere in the call — unlike the TerrainTypeClass chain (0x18CE30) which passes
   per-theater model indices at sp+0x38/0x3C/0x40.

4. **No slot-base table exists in ROM.** Whole-ROM scans for {153,161,176,184,188} as
   u8/u16/u32 sequences and as strided tables (strides 2..0xAC): zero hits. The
   overlay-id -> slot-base mapping is code, not data (same conclusion the unified-enum
   work reached for id+10).

5. **Visual identification (decisive).** All candidate slots rendered textured straight
   off the ROM (walk_rdp + the DL's own CI8 textures + global TLUT 0x99130) with a
   game-like pitch. Renders + decoded textures:
   per-slot renders and decoded textures, produced by a throwaway renderer built on
   tools/romdump/segments.py + textures.py. Those images were working output and are
   not part of this repository.
   The sandbag family is unmistakably the tan segmented wall in the reference frame
   p040; chainlink/concrete/barbwire/wood are equally unambiguous. Former candidates
   eliminated: 69-75 are V15-V21 (high-confidence civilians), 92-97 are the proven
   bullets, 98/99 are the crates (the "62 62 / 62 63" record-tail hint bytes in
   BARB/WOOD records were a red herring).

## Still open (small)

- The exact draw-time code site that turns overlay id + connectivity frame into
  {slot base, variant, rotation}. The immediates 153/161/176/184/188 appearing in
  CodeOverlay chains at 0x18EF60+/0x190940+ are FALSE positives (sequential
  TemplateTypeClass-style id registrations, ctor 0x801EBFE0). Not needed for the
  renderer fix: base slots + variant order above are complete.
- ~~Which shadow 4-set (157-160) belongs to which fence type(s).~~ **CLOSED:**
  180-183 is BRIK's (exact footprint match on the straight and the T), 157-160 is SBAG's
  by elimination and table adjacency — SBAG is the only remaining solid wall, the three
  wire/rail fences having 11-piece shadow sets of their own. The SBAG pairing remains
  *inferred*, the BRIK one is *proven*; say which is which when quoting this.
- ~~Who consumes the 11-piece civilian fence families 165-175 / 192-202.~~ **CLOSED:**
  nobody — they are not fences. They are the CYCL and WOOD shadow sets
  (see the table above). The 11-piece order was recovered by undoing the shear (the
  shadow is the fence displaced toward the NORTH-EAST by its own height; CYCL h=373 gives
  displacement +378x / -373z, WOOD h=258 gives +258x / -258z) and reading each piece's
  arms straight out of its unsheared bounding box:
  `+0 {W,E}  +1 {N,S}  +2 {W,N}  +3 {N,E}  +4 {E,S}  +5 {S,W}  +6 {W,E,S}  +7 {N,S,W}
   +8 {W,E,N}  +9 {N,S,E}  +10 cross`.
  The CYCL and WOOD sets were unsheared independently and agree piece for piece; that
  agreement is the check.
- The wall body meshes themselves (153-164, 176-191, 206-209) carry **no** MODE_SHADOW
  triangle at all. That is *why* the separate shadow sets exist, and it means a renderer
  must draw them explicitly — a shadow will never fall out of the body draw.
- BARB (184-187) has **no** shadow set anywhere in the ROM, and its four display lists
  are entirely packed normals in the vertex-colour slot (measured 6/6, 12/12, 12/12,
  12/12), so the baker whitens them the same way it whitens the DRAGON/MISSILE/BOMBLET
  body. Both are registered deliberate gaps.
- Second model table (19 records, stride 0x14, RAM 0x8009A7EC): entries carry ids
  0x18-0x2C (the unit/aircraft type range) — likely secondary/turret meshes; unexplored.
