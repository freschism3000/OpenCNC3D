# The pip row, read out of the cartridge

**Read-only reverse engineering of `data/rom/cnc_eu.z64` (EU). Nothing in the repository is changed
except this file.**

There was an option: the shortcut of dressing the row with the 1995 `PIPS.SHP` frames and
chose the console's own answer instead. This is that answer, complete enough to implement
the row without opening the ROM again. Everything below is tagged **DECODED** (the
instructions are quoted or the bytes are printed), **INFERRED** (a consistent reading, not
proven) or **NOT RECOVERED**.

Address conventions, as used everywhere else in `docs/`:

```
resident      rom = ram - 0x7FFFF400      (ROM 0x001000..0x0C6EA0)
CodeOverlay   rom = ram - 0x8005E210      (ROM 0x1643F0..0x1B67D0)
```

Units: **leptons**, 256 per cell, the engine's own coordinate. The renderer works in
cells, so every world number below is also given as `n/256`.

---

## 0. The one-paragraph answer

**DECODED.** The type-8 2-D command is consumed by RAM `0x8004D760` / ROM `0x04E360`, an
arm of the one big 2-D draw dispatcher at RAM `0x8004BF80`. It does **not** rasterise
`PIPS.SHP` and it does **not** draw a plain box row. It builds two textured quads standing
**vertically in the world**, out of the ground, immediately west of the health bar, and
tiles a **4x4 RGBA5551 texture** across each one: a green-cored tile for the filled run and
a hollow-cored tile for the empty run. Each pip is exactly **48 x 48 leptons** and the pips
touch, with no gap. The cartridge names the block itself: the string `"Pips"` at RAM
`0x800048C4` labels the display-list budget record whose address this very arm passes to
the space check, and the two textures sit in the 64 bytes immediately after it.

So: the console's pip row is the same *kind* of object as its health bar (a world-space,
non-billboarded strip with constant world size), not the same kind of object as the DOS
game's pip row (a 3-pixel-pitch screen-space SHP blit).

---

## 1. The call chain, end to end

| what | RAM | ROM |
|---|---|---|
| per-object overlay draw (health bar, pips, brackets) | `0x801D0A60` | `0x172850` |
| `HouseClass::Is_Ally(HouseClass*)` | `0x80037D8C` | `0x03898C` |
| `TechnoClass::Draw_Pips` | `0x801D4530` | `0x176320` |
| type-8 enqueue (pips), sort bucket `0x298` | `0x8004B638` | `0x04C238` |
| type-9 enqueue (health bar), sort bucket `0x299` | `0x8004B6B4` | `0x04C2B4` |
| command allocator, takes the sort bucket | `0x801FCBC8` | `0x19E9B8` |
| bucket flatten pass (ascending bucket = drawn first) | `0x801FCD28`.. | `0x19EB18`.. |
| **the 2-D dispatcher**, reads `cmd+0x08` | `0x8004BF80` (test at `0x8004C058`) | `0x04CB80` |
| **the type-8 arm: THE PIP CONSUMER** | `0x8004D760` | `0x04E360` |
| type-9 arm (health bar), for comparison | `0x8004DBE4` | `0x04E7E4` |
| display-list space check | `0x8004BE20` | `0x04CA20` |
| on-map / on-screen test | `0x801F7B48` | `0x199938` |
| terrain height at (x, y) | `0x801F7DFC` | `0x199BEC` |
| textured-quad emitter (4 `Vtx` + `G_VTX` + `G_TRI2`) | `0x8004B39C` | `0x04BF9C` |

**The type-8 enqueue has exactly ONE caller in the whole ROM.** A scan of every `jal`
word for `0x8004B638` returns a single hit, ROM `0x1764A0`, inside `Draw_Pips`. Type 8 is
the pip row and nothing else.

### The dispatcher (DECODED)

```
ROM 04CC58 / RAM 8004C058  8F030008  lw    $v1, 8($t8)     ; t8 = drawCmd, v1 = cmd+0x08 = TYPE
...
ROM 04CC70 / RAM 8004C070  28620008  slti  $v0, $v1, 8
ROM 04CC74 / RAM 8004C074  10400012  beqz  $v0, 0x8004c0c0  ; type >= 8 goes here
...
ROM 04CCC0 / RAM 8004C0C0  2402000A  addiu $v0, $zero, 0xa
ROM 04CCC4 / RAM 8004C0C4  10620755  beq   $v1, $v0, 0x8004de1c
ROM 04CCC8 / RAM 8004C0C8  2862000B  slti  $v0, $v1, 0xb
ROM 04CCCC / RAM 8004C0CC  10400007  beqz  $v0, 0x8004c0ec
ROM 04CCD0 / RAM 8004C0D0  24020008  addiu $v0, $zero, 8
ROM 04CCD4 / RAM 8004C0D4  106205A2  beq   $v1, $v0, 0x8004d760   ; TYPE 8 -> the pip arm
```

It is a plain comparison tree on the type word, not a jump table (a whole-ROM search for
the raw word `0x8004C108`, the known model arm, finds zero hits).

---

## 2. When the row is drawn: the gate (question 5)

**DECODED, and the diagnosis's shorthand needs one correction.** The gate is not
"the object's house is the player's". It is the 1995 line compiled verbatim
(`brain/vanilla/tiberiandawn/techno.cpp:1127`,
`if ((window == WINDOW_VIRTUAL) || (selected && House->Is_Ally(PlayerPtr)))`):

```
ROM 172890 / RAM 801D0AA0  8E020010  lw    $v0, 0x10($s0)    ; obj+0x10 flags
ROM 172894 / RAM 801D0AA4  30420004  andi  $v0, $v0, 4       ; bit 2 = IsSelected
ROM 172898 / RAM 801D0AA8  14400005  bnez  $v0, 0x801d0ac0
ROM 1728A0 / RAM 801D0AB0  8C422830  lw    $v0, 0x2830($v0)  ; *0x80092830, the Special word
ROM 1728A4 / RAM 801D0AB4  30424000  andi  $v0, $v0, 0x4000
ROM 1728A8 / RAM 801D0AB8  1040006F  beqz  $v0, 0x801d0c78   ; not selected and flag clear -> nothing
...
ROM 1728E0 / RAM 801D0AF0  8E040034  lw    $a0, 0x34($s0)    ; a0 = obj->House
ROM 1728E4 / RAM 801D0AF4  8E2566C8  lw    $a1, 0x66c8($s1)  ; a1 = *0x800966C8 = PlayerPtr
ROM 1728E8 / RAM 801D0AF8  0C00DF63  jal   0x80037d8c        ; House->Is_Ally(PlayerPtr)
ROM 1728F0 / RAM 801D0B00  10400006  beqz  $v0, 0x801d0b1c   ; not an ally -> no pips
ROM 1728F8 / RAM 801D0B08  8E020014  lw    $v0, 0x14($s0)    ; obj vtable
ROM 172900 / RAM 801D0B10  8C4201D4  lw    $v0, 0x1d4($v0)   ; vtable[+0x1D4] = Draw_Pips
ROM 172904 / RAM 801D0B14  0040F809  jalr  $v0
```

`0x80037D8C` is `HouseClass::Is_Ally(HouseClass const *)`: it takes the argument's first
word as its `Class` and reads the `HousesType` byte at `Class+0x14`, then tails into
`0x80037D5C`, the `Is_Ally(HousesType)` overload.

So, in order:

1. `RTTI == RTTI_BUILDING (7)` **and** `obj+0x20 == 18` skips *all* overlays
   (ROM `0x172870`-`0x172888`). `obj+0x20` is the mission byte; **18 is
   `MISSION_DECONSTRUCTION` if the N64's `MissionType` matches the 1995 enum, which is NOT
   settled** (see §9).
2. **selected** (`obj+0x10 & 4`) **or** the global bit `*0x80092830 & 0x4000` is set. That
   bit is the console's equivalent of `Special.ResourceBarDisplayMode == RB_ALWAYS`
   (**INFERRED**; the word at `0x80092830` is read from about a hundred sites and is
   plainly a general Special/flags word, so the bit's real name is not recovered).
3. **and** `obj->House->Is_Ally(PlayerPtr)`. Allies, not only the player.
4. Health bar first (`0x801D0854`), pips second, selection brackets third
   (`0x8004B364`, and the brackets need *actual* selection, not the always-show bit).
5. Inside `Draw_Pips`, `Max_Pips() == 0` skips the row entirely (ROM `0x176408`).

---

## 3. `TechnoClass::Draw_Pips`, RAM `0x801D4530` / ROM `0x176320` (DECODED)

Signature `Draw_Pips(this, int x, int y)`. `x`/`y` are the object's world position in
leptons (world X and world Z; world Y is up and is supplied by the consumer).

**Transporter branch** (`Class+0x38 & 0x00400000` = `IsTransporter`, ROM `0x176358`): walks
the cargo chain from `obj+0x38` through `+0x08` (`Next`) for at most `Max_Pips()`
iterations and counts occupants into `$s3`. The 1995 per-occupant pip colours are
**compiled in but dead**: when the occupant's `RTTI == 1` (`RTTI_INFANTRY`) it still makes
the two `Class_Of()` calls (`0x801DFFE4`, which is just `return *(void**)(obj+4)`) that the
`INFANTRY_RAMBO` and `INFANTRY_E7` comparisons in `techno.cpp:4441-4446` need, and throws
both results away. The constants `6` and `5` are still hoisted into `$s6`/`$s5` at ROM
`0x176374` and `0x176388` and never read. **The console collapsed the transporter row to a
plain count.**

**Everything else**: `s4 = Class->vtbl[+0x14]()` = `Max_Pips()`, then
`s3 = obj->vtbl[+0x18C]()` = `Pip_Count()`.

**Common tail** (ROM `0x176408` onward):

```
ROM 176408 / RAM 801D4618  12800027  beqz  $s4, 0x801d46b8     ; Max_Pips()==0 -> draw nothing
ROM 176418 / RAM 801D4628  8C420018  lw    $v0, 0x18($v0)      ; Class->vtbl[+0x18] = Dimensions(&w,&h)
ROM 176428 / RAM 801D4638  8FA30018  lw    $v1, 0x18($sp)      ; v1 = w
ROM 176430 / RAM 801D4640  00031A00  sll   $v1, $v1, 8
ROM 176434 / RAM 801D4644  00640018  mult  $v1, $a0            ; a0 = 0x2AAAAAAB
ROM 176444 / RAM 801D4654  00081083  sra   $v0, $t0, 2         ; s0 = (w << 8) / 24 = L, in leptons
ROM 17644C / RAM 801D465C  82430012  lb    $v1, 0x12($s2)      ; RTTI
ROM 176454 / RAM 801D4664  14620005  bne   $v1, $v0, 0x801d467c ; v0 = 5 = RTTI_AIRCRAFT
ROM 17645C / RAM 801D466C  86430088  lh    $v1, 0x88($s2)      ; Altitude (s16)
ROM 176468 / RAM 801D4678  00028840  sll   $s1, $v0, 1         ; s1 = Altitude * 10
ROM 176474 / RAM 801D4684  8C42001C  lw    $v0, 0x1c($v0)      ; Class->vtbl[+0x1C]
ROM 176488 / RAM 801D4698  00042043  sra   $a0, $a0, 1
ROM 17648C / RAM 801D469C  02E42023  subu  $a0, $s7, $a0       ; a0 = x - L/2
ROM 176490 / RAM 801D46A0  03C22821  addu  $a1, $fp, $v0       ; a1 = y + Class->vtbl[+0x1C]()
ROM 176494 / RAM 801D46A4  02A03021  move  $a2, $s5            ; a2 = Dimensions()'s RETURN value
ROM 176498 / RAM 801D46A8  02203821  move  $a3, $s1            ; a3 = Altitude*10 (0 unless aircraft)
ROM 17649C / RAM 801D46AC  AFB30010  sw    $s3, 0x10($sp)      ; arg5 = Pip_Count()
ROM 1764A0 / RAM 801D46B0  0C012D8E  jal   0x8004b638
ROM 1764A4 / RAM 801D46B4  AFB40014  sw    $s4, 0x14($sp)      ; arg6 = Max_Pips()
```

`0x2AAAAAAB` with `hi >> 2` is the signed divide by 24. `RTTI == 5` is `RTTI_AIRCRAFT`
(confirmed in §7); the ROM's `RTTIType` is byte-for-byte the 1995 enum in `defines.h:179`.

The health-bar producer at `0x801D0854` computes the *same* `x - L/2` anchor, the same
`Altitude*10`, and calls the same `Class->vtbl[+0x1C]`, so the two overlays share one
anchor by construction.

### The command record (DECODED)

`0x8004B638` allocates from bucket `0x298` and fills:

| offset | value |
|---|---|
| `+0x08` | `8` (the type) |
| `+0x0C` | `x - L/2`, world X in leptons |
| `+0x10` | `y + Class->vtbl[+0x1C]()`, world Z in leptons |
| `+0x14` | `Dimensions()`'s return value. **Written and never read** by the consumer |
| `+0x18` | `Altitude * 10` for `RTTI_AIRCRAFT`, else `0` |
| `+0x1C` | `Pip_Count()` |
| `+0x20` | `Max_Pips()` |

`+0x14` being dead is not a mis-read: the type-8 arm's only loads off the record are at
`+0x0C`, `+0x10`, `+0x18`, `+0x1C` and `+0x20` (ROM `0x04E360`-`0x04E38C`), and the type-9
arm does the same. The health-bar record is identical up to `+0x14` and then carries the
bar lengths and an RGBA byte quad at `+0x24`.

---

## 4. What the consumer rasterises (questions 1, 2, 4)

RAM `0x8004D760` / ROM `0x04E360`. It first calls the on-map/on-screen test
`0x801F7B48(x, y)` and bails on 0 (ROM `0x04E390`-`0x04E39C`), then checks display-list
space with `0x8004BE20(0x80097080)` (ROM `0x04E438`). **That argument is the load-bearing
identification: `0x80097080` is a 20-byte budget record whose name field, at `+0x10` =
`0x80097090`, points at the string `"Pips"` (RAM `0x800048C4`, ROM `0x0054C4`).** The
type-9 arm passes `0x800970D8`, whose name field points at `"HealthBar"`. The cartridge
labels its own subsystems, and these two records are neighbours in the same table:

```
0x8009706C  0x00000400 ...            name at 0x8009707C -> "Laser"
0x80097080  0x00000400 ...            name at 0x80097090 -> "Pips"
0x80097098  the EMPTY pip texture, 32 bytes
0x800970B8  the FULL  pip texture, 32 bytes
0x800970D8  0x00000400 ...            name at 0x800970E8 -> "HealthBar"
```

### The RSP/RDP state it sets (DECODED, raw words)

Sixteen 64-bit commands are emitted before the first quad. Decoded from the words stored
at ROM `0x04E53C`-`0x04E660`:

| # | word0 / word1 | meaning |
|---|---|---|
| 0 | `E7000000 00000000` | `G_RDPPIPESYNC` |
| 1 | `D9DDF9FB 00000000` | `G_GEOMETRYMODE`, clear `0x220604` = `G_SHADING_SMOOTH \| G_LIGHTING \| G_CULL_BOTH \| G_SHADE`. Set: nothing |
| 2 | `D7000002 80008000` | `G_TEXTURE` on, tile 0, s-scale = t-scale = `0x8000` (0.5) |
| 3 | `E3001201 00000000` | `G_SETOTHERMODE_H`, `TEXTFILT` = `G_TF_POINT` |
| 4 | `E3001001 00000000` | `G_SETOTHERMODE_H`, `TEXTLUT` = `G_TT_NONE`. **No palette. This is not a CI texture** |
| 5 | `E200001C 005049D8` | `G_SETOTHERMODE_L` rendermode = `G_RM_AA_ZB_XLU_SURF` |
| 6 | `FCFFFFFF FFFCF279` | `G_SETCOMBINE` = `G_CC_DECALRGBA` in both cycles: colour and alpha are the texel, untouched by shade, primitive or environment |
| 7 | `E200001C 00553078` | `G_SETOTHERMODE_L` rendermode = `G_RM_AA_ZB_TEX_EDGE`. Written after #5, so **this is the mode that survives**: antialiased, z-tested AND z-written, `CVG_X_ALPHA \| ALPHA_CVG_SEL` |
| 8 | `E2001E01 00000001` | `G_SETOTHERMODE_L`, `ALPHACOMPARE` = `G_AC_THRESHOLD` |
| 9 | `F9000000 00000064` | `G_SETBLENDCOLOR` `(0,0,0, 100)`: the alpha threshold |
| 10 | `FD100000 800970B8` | `G_SETTIMG`, fmt `G_IM_FMT_RGBA`, siz `G_IM_SIZ_16b`, **image = the FULL pip texture** |
| 11 | `F5100000 07008C23` | `G_SETTILE` load tile 7 |
| 12 | `E6000000 00000000` | `G_RDPLOADSYNC` |
| 13 | `F3000000 0700F800` | `G_LOADBLOCK` tile 7, `lrs = 15` (16 texels), `dxt = 0x800` (one 64-bit word per row) |
| 14 | `E7000000 00000000` | `G_RDPPIPESYNC` |
| 15 | `F5100200 00008C23` | `G_SETTILE` tile 0: RGBA16, `line = 1`, `tmem = 0`, `masks = maskt = 2`, `shifts = shiftt = 3`, `cms = cmt = 0` (wrap) |
| 16 | `F2000000 0000C00C` | `G_SETTILESIZE` tile 0, `lrs = lrt = 12` in 10.2 = 3.0, i.e. **4 x 4 texels** |

Then the first quad. Then the same block again from ROM `0x04E68C` with
`G_SETTIMG 0x80097098` (the EMPTY texture) and the second quad. Then the state is
restored: `ALPHACOMPARE` back to `G_AC_NONE`, `TEXTFILT` back to `G_TF_BILERP`,
rendermode back to `G_RM_ZB_OPA_SURF` (`0x00552230`), the same combiner, and
`G_SETPRIMCOLOR` white.

`dxt = 0x800` means one 64-bit word per texture row, i.e. 4 texels at 16bpp; 16 texels
total; `mask = 2` on both axes. Three independent numbers, one conclusion: **the texture
is 4 x 4 RGBA16**.

### The two textures (DECODED, printed from the ROM)

Format is RGBA5551: `r = bits 15..11`, `g = 10..6`, `b = 5..1`, `a = bit 0`.
8-bit expansion below is the standard `v<<3 | v>>2`.

**EMPTY pip. RAM `0x80097098` / ROM `0x097C98`, 32 bytes:**

```
5295 318d 318d 18c7
318d 0000 0000 18c7
318d 0000 0000 18c7
18c7 18c7 18c7 0001
```

| memory row | col 0 | col 1 | col 2 | col 3 |
|---|---|---|---|---|
| 0 | `#525252` | `#313131` | `#313131` | `#181818` |
| 1 | `#313131` | **transparent** | **transparent** | `#181818` |
| 2 | `#313131` | **transparent** | **transparent** | `#181818` |
| 3 | `#181818` | `#181818` | `#181818` | `#000000` |

**FULL pip. RAM `0x800970B8` / ROM `0x097CB8`, 32 bytes:**

```
5295 318d 318d 18c7
318d d781 a707 18c7
318d a707 8e43 18c7
18c7 18c7 18c7 0001
```

Identical bevel; the 2x2 core is filled with a yellow-green diagonal ramp:

| raw | 5-bit rgba | 8-bit |
|---|---|---|
| `D781` | 26, 30, 0, 1 | `#D6F700` |
| `A707` | 20, 28, 3, 1 | `#A5E718` |
| `8E43` | 17, 25, 1, 1 | `#8CCE08` |

**So the answer to the colour question: two TEXTURES, not two colours, not two frames of
one sheet, and not a palette swap.** `G_TT_NONE` rules out a TLUT and `G_CC_DECALRGBA`
rules out any tinting. There is no house colour, no per-count colour, and **no variation
whatsoever by what is being counted**: the `G_SETTIMG` addresses are two hard-coded
immediates in the arm (ROM `0x04E5FC`-`0x04E608` and `0x04E6BC`-`0x04E6D0`), selected by
nothing. Harvester tiberium, Orca ammo, silo and refinery storage and transport occupancy
all draw the same green pip.

Because alpha in RGBA5551 is one bit and the threshold is `100/255`, the empty pip's core
is a **hole**: the scene shows through it. It is not black.

---

## 5. Geometry (question 3)

The quad emitter `0x8004B39C` / ROM `0x04BF9C` writes four 16-byte `Vtx` records and two
display-list words, `G_VTX` (`0x01004008`: 4 vertices into slot 0) and `G_TRI2`
(`0x06000204 / 0x00000406`: triangles 0-1-2 and 0-2-3). Its arguments, from the stores at
ROM `0x04C024`-`0x04C070`:

```
f(dl, zWorld, x0, y0, x1, y1, sMax, tMax)

  v0 = (x0, y0, zWorld)  s=0     t=0
  v1 = (x1, y0, zWorld)  s=sMax  t=0
  v2 = (x1, y1, zWorld)  s=sMax  t=tMax
  v3 = (x0, y1, zWorld)  s=0     t=tMax
```

The vertex fields are the standard `Vtx_t`: `ob[0]` at `+0`, `ob[1]` at `+2`, `ob[2]` at
`+4`, `tc[0]` at `+8`, `tc[1]` at `+0x0A`. So the strip runs along **world Y (up)** and is
a fixed width along **world X**, at one constant **world Z**. That is the same shape as
the health bar, which `game/cnc_eyes.cpp:8813`-`8841` already documents and draws.

Two constants govern the size. Both live in the resident image, and both are **the ROM's
initialised values, runtime-mutable through the scale toggle at RAM `0x80049644`** (§5.1):

| RAM | ROM | value | role |
|---|---|---|---|
| `0x80097AEC` | `0x0986EC` | **16** | stand-off west of the anchor. The health-bar arm reads the same word for its own strip width |
| `0x80097AF4` | `0x0986F4` | **48** | the pip pitch, in leptons, on both axes |

### 5.1 They are not read-only, and this file used to say they were

An earlier revision of this section claimed "both are read-only (a whole-ROM xref finds
only `lw`/`lwc1`, never a store)". **That is false**, and it is worth recording HOW two
separate passes reached it, because the same trap is waiting in every other xref in this
document: the stores sit **10 and 14 instructions after** the `lui` that forms their base
register, so an 8-instruction xref window finds nothing and a 40-instruction one finds
them. The window, not the ROM, was the problem.

Each word has two stores, both inside one scale toggle at RAM `0x80049644` / ROM
`0x04A244`. Disassembled and re-checked:

| arm | RAM | ROM | effect |
|---|---|---|---|
| double, taken on `flags & 1` | `0x80049718` | `0x04A318` | `*0x80097AEC <<= 1`  (16 -> 32) |
| double | `0x80049740` | `0x04A340` | `*0x80097AF4 <<= 1`  (48 -> 96) |
| halve, taken on `flags & 2` | `0x80049790` | `0x04A390` | `*0x80097AEC >>= 1`, arithmetic (32 -> 16) |
| halve | `0x800497C8` | `0x04A3C8` | `*0x80097AF4 >>= 1`, arithmetic (96 -> 48) |

The flags word is `lw 0x10($a2)` with `$a2 = 0x8011EF48`. The two arms are a matched pair:
the double arm is `sll v0, v0, 1`, the halve arm is the signed `srl/addu/sra` divide-by-two
idiom, so the toggle is a clean factor of two in both directions with no other state.

**It is not a pip-specific toggle.** The same two arms scale a whole group of overlay
geometry constants together: a third integer `*0x80097AF0 == 100`, and three floats
`*0x80097984 == 0.25`, `*0x80097988 == 1.0` and `*0x800979F8 == 10.0` (the halve arm
multiplies those by the `0.5` stored at RAM `0x80004794`). Whatever this is -- the most
likely reading is a resolution or overlay-scale switch -- it moves the health bar and the
pip row as one.

**Nothing implementable changes, and that is the point of recording it.** 16 and 48 are the
values the cartridge boots with, the toggle is a uniform doubling and halving, and a
renderer that hardcodes 16 and 48 matches the console in its default state. What changes is
the confidence attached to them: they are initialised values, not immutable ones. If a
later pass ever finds console footage with a differently sized pip row, this toggle is the
first place to look rather than a re-derivation of the geometry.

With `L = (Dimensions().width << 8) / 24` leptons, `X0 = objX - L/2`,
`Z0 = objZ + Class->vtbl[+0x1C]()`, `count = Pip_Count()`, `max = Max_Pips()`,
`base = TerrainHeight(X0, Z0) + (RTTI_AIRCRAFT ? Altitude*10 : 0)`:

```
FULL  quad:  x from X0-64 to X0-16,  y from base                 to base + count*48
EMPTY quad:  x from X0-64 to X0-16,  y from base + count*48      to base + max*48
both at z = Z0
```

Derived from ROM `0x04E3B4`-`0x04E428` (the `f20 = TerrainHeight + Altitude*10` base and
the two truncated tops) and the two emitter calls at ROM `0x04E684` and `0x04E750`.

**Texture coordinates.** `sMax` is always `0x800` and `tMax` is `count << 11` for the
filled run and `(max - count) << 11` for the empty one (ROM `0x04E678` and `0x04E744`).
The chain that turns `0x800` into 4 texels: `0x800 * 0x8000/0x10000 = 0x400`, S10.5 gives
`0x400/32 = 32` texels, tile `shift = 3` gives `32/8 = 4`. Exactly the tile width. So:

* **each pip is one repeat of the 4x4 tile, 48 x 48 leptons (0.1875 x 0.1875 cells);**
* **spacing is zero.** The pips touch. There is no per-pip geometry at all: the whole
  filled run is ONE quad and the texture wraps (`cms = cmt = 0`, `mask = 2`) `count` times
  up its length;
* **filled pips are at the bottom**, empty above, so the row reads like a gauge.

**Anchoring across footprints.** The only footprint input is `Dimensions().width` through
`L = (width << 8) / 24`, and it only moves the row sideways: the strip's east edge sits at
`X0 - 16`, which is exactly the health bar's west edge. The two overlays are contiguous:
health bar 16 leptons wide, pips 48 leptons wide immediately west of it. The row's LENGTH
does not depend on the footprint at all, only on `Max_Pips()`.

**In the renderer's frame** (cells; mirrors `collect_health_bars`, `cnc_eyes.cpp:8852`):

```
L      = dimw / 24.0                       cells   (same L the health bar uses)
x1     = o.wx - L*0.5 - 16.0/256.0
x0     = x1 - 48.0/256.0
z      = o.wz + zOff                       see section 6; 0 for everything but buildings
yBase  = terrain_y(o.wx - L*0.5, z) + yLift
yFill  = yBase + count * 48.0/256.0
yTop   = yBase + max   * 48.0/256.0
```

Note the terrain is sampled at `X0`, the strip's *east* edge, not at its middle. The
health-bar pass in `cnc_eyes.cpp:8892` samples at the strip midpoint instead; that is a
half-strip, 8-lepton difference and it is recorded as known gaps.

**Degenerate runs are still emitted.** There is no `count == 0` or `count == max`
early-out; the arm always emits both quads, one of which collapses to zero height. A
re-implementation may skip a zero-height quad freely, but must not skip the *row* when
`count == 0`: an empty harvester shows seven hollow pips.

---

## 6. `Class->vtbl[+0x1C]`, the world-Z offset that `cnc_eyes.cpp:8839` records as unknown

**DECODED for buildings, and it is 0 for everything else.**

The type-class vtables recovered this pass (the class identifications are anchored on
functions whose bodies match the 1995 sources exactly):

| type class | vtable RAM | `+0x14` `Max_Pips` | `+0x18` `Dimensions` | `+0x1C` |
|---|---|---|---|---|
| `ObjectTypeClass` (base) | `0x800024C0` | `0x800208C8` (returns 0) | `0x800208D0` | `0x800208E4` (returns 0) |
| `BuildingTypeClass` | `0x801C4188` | `0x801E6674` | `0x801E622C` | **`0x801E62CC`** |
| `UnitTypeClass` | `0x801C7350` | `0x801E9A6C` | `0x801E9948` | `0x800208E4` (returns 0) |
| `AircraftTypeClass` | `0x801C86F0` | `0x801F4138` | `0x801F418C` | `0x800208E4` (returns 0) |
| `InfantryTypeClass` | `0x801C8498` | `0x800208C8` (returns 0) | `0x801EF8D0` | `0x800208E4` (returns 0) |

`0x801C7350` is confirmed as `UnitTypeClass` because its `+0x18` is `0x801E9948`, the
function `game/cnc_eyes.cpp:6150` already names `UnitTypeClass::Dimensions` and whose
`*48/1000`-with-a-floor-of-8 arithmetic `cart_dimw_for_mesh` reproduces.

The building override, RAM `0x801E62CC` / ROM `0x1880BC`, complete:

```
ROM 1880BC / RAM 801E62CC  3C02800F  lui  $v0, 0x800f
ROM 1880C0 / RAM 801E62D0  8C830000  lw   $v1, ($a0)          ; v1 = Class->[0x00] = MODEL INDEX
ROM 1880C4 / RAM 801E62D4  2442BE40  addiu $v0, $v0, -0x41c0  ; v0 = 0x800EBE40
ROM 1880C8 / RAM 801E62D8  00031840  sll  $v1, $v1, 1
ROM 1880CC / RAM 801E62DC  00621821  addu $v1, $v1, $v0
ROM 1880D0 / RAM 801E62E0  94620000  lhu  $v0, ($v1)
ROM 1880D4 / RAM 801E62E4  03E00008  jr   $ra
ROM 1880D8 / RAM 801E62E8  000210C2  srl  $v0, $v0, 3         ; return zHalfExtent[model] >> 3
```

`0x800EBE40` is **not in the ROM image**; it is resident BSS, filled by the model-load
pass at RAM `0x800562A0` / ROM `0x056EA0` (the `0x800EBE40` store is at RAM `0x80056398` /
ROM `0x056F98`), which calls the model bounding-box helper `0x80076934` and stores, per
model index, three `u16`s:

| table RAM | filled with |
|---|---|
| `0x800EBFF8` | `max(abs(bbox.min.x), abs(bbox.max.x))` |
| `0x800EBE40` | `max(abs(bbox.min.z), abs(bbox.max.z))` |
| `0x800EBC88` | `bbox.max.y / 4` (rounds toward zero) |

Cross-check that this reading is right: `UnitTypeClass::Dimensions` (`0x801E9948`) computes
its width as `max(8, xExtent*48/1000, zExtent*48/1000)` from the first two of those tables,
which is exactly what `cart_dimw_for_mesh` was built to reproduce from our own meshes.

**Practical consequence.** For units, infantry and aircraft the offset is 0 and
`cnc_eyes.cpp`'s "we use 0" is *correct*. For buildings it is one eighth of the model's
world-Z half-extent, which pulls the health bar and the pip row toward the camera by
roughly an eighth of the building's depth. That is recoverable from our own meshes with
the same walk `cart_dimw_for_mesh` already does, so it is a small, honest fix rather than
an open question. What is NOT settled is the exact unit scale of the ROM's bbox against
our `MODEL_SCALE`; see §9.

`Dimensions()`'s *return* value (a per-model `u16` from `0x800EBC88`, `bbox.max.y/4`, or a
flat `100` from the base) reaches the command record at `+0x14` and is then never read by
either 2-D arm. It cannot matter to the pip row.

---

## 7. `Max_Pips` and `Pip_Count`, and the harvester (questions 3, 6)

All four are the 1995 bodies compiled. Verified in the ROM this pass:

**`UnitTypeClass::Max_Pips`, RAM `0x801E9A6C` / ROM `0x18B85C`** (DECODED)

```
if ((signed char)Class->[0x40] == 9)      return 7;                  ; Type == HARV
if (Class->[0x38] & 0x00400000)           return Max_Passengers();   ; IsTransporter
return 0;
```

**`Type == 9` is the harvester on the N64, settled rather than assumed.** The
`UnitTypeClass` constructor `0x801E92F0` is called sixteen times from RAM `0x801E9AB8`;
the HARV call at RAM `0x801EA3FC` passes `a1 = $s7`, and `$s7` is loaded once, at
RAM `0x801E9BA8` / ROM `0x18B998`, with `addiu $s7, $zero, 9`. The neighbours bracket it:
`BGGY = 8`, `ARTY = 10`. (The 1995 enum puts `UNIT_HARVESTER` at 10; the console's list has
16 units and no Visceroid, so it is shifted by one. That shift is **INFERRED**; the value 9
for HARV is not.)

**`AircraftTypeClass::Max_Pips`, RAM `0x801F4138` / ROM `0x195F28`** (DECODED)

```
if (Class->[0x38] & 0x00400000)           return Max_Passengers();
if ((signed char)Class->[0x33] != -1)     return 5;                  ; Primary != WEAPON_NONE
return 0;
```

**`BuildingTypeClass::Max_Pips`, RAM `0x801E6674` / ROM `0x188464`** (DECODED)

```
v = Capacity (u32 at Class+0x9C) / 100;   if (v > 10) v = 10;   return v;
```

`Bound(Capacity/100, 0, 10)` with the lower bound optimised away because the field is
unsigned. Matches `bdata.cpp:4512`.

**`InfantryTypeClass`**: no override. `Max_Pips()` is the base stub `0x800208C8`, which
returns 0, so `Draw_Pips` bails at ROM `0x176408` and **infantry never draw a pip row**.

**`UnitClass::Pip_Count`, RAM `0x80016A30` / ROM `0x017630`** (DECODED)

```
if (Class->[0x38] & 0x00400000)  return (this->[0x48] >> 18) & 0xF;   ; How_Many()
if (Class->[0x44] & 0x1000)      return Fixed_To_Cardinal(7, Tiberium_Load());
                                   ; = (Tiberium_Load()*7 + 0x80) >> 8, saturating at 0xFFFF
return 0;
```

**`BuildingClass::Pip_Count`, RAM `0x80044C24` / ROM `0x045824`** (DECODED)

```
m = Class->Max_Pips();
t = House->[0xE0];  c = House->[0xEC];
f = (t == 0) ? 0 : (c == 0 ? t : (t << 8) / c);
return Fixed_To_Cardinal(m, f);
```

Independent corroboration of `house+0xE0` = Tiberium and `house+0xEC` = Capacity, the two
offsets the silo texture-book work also uses.

### So: does the harvester get anything special? (question 6)

**No.** On the 1995 side the harvester's tiberium load is drawn with a different pip colour
via `Class_Of().PipShapes`. On the console there is exactly one filled-pip texture and one
empty-pip texture in the whole path, chosen by nothing. What the harvester gets is a
**length**: `Max_Pips() == 7`, and `Pip_Count()` is `Fixed_To_Cardinal(7, Tiberium_Load())`,
so a full harvester shows seven green pips and an empty one shows seven hollow ones. The
Orca shows five, and a silo or refinery shows `min(Capacity/100, 10)`.

---

## 8. What the console dropped, relative to 1995

**DECODED**, all four by absence in `0x801D4530`:

* **the per-occupant transporter colours** (`PIP_COMMANDO`, `PIP_ENGINEER`,
  `PIP_CIVILIAN`) are dead code, §3;
* **the primary-building star.** 1995's `Draw_Pips` ends with
  `if (IsLeader) CC_Draw_Pip(..., PIP_PRIMARY, x-2, y-3, ...)` (`techno.cpp:4477`). The
  console's function ends at the enqueue: nothing follows it before the epilogue at ROM
  `0x1764A8`. There is no `PIP_PRIMARY` arm anywhere in the type-8 path;
* **`PipShapes`**, the per-type pip sheet, is not consulted at all;
* **the DOS pip pitch of 3 pixels in screen space** is replaced by 48 leptons in world
  space, which does not scale with zoom the way the DOS row did.

---

## 9. Corrections to existing docs

**`docs/animation-drivers.md:174-179` lists the four object vtables eight bytes too high.**
Confirmed twice this pass. The `UnitClass` constructor stores the vtable at
RAM `0x80012958`:

```
ROM 013538 / RAM 80012938  3C028000  lui   $v0, 0x8000
ROM 013544 / RAM 80012944  24421B10  addiu $v0, $v0, 0x1b10
ROM 013558 / RAM 80012958  AE420014  sw    $v0, 0x14($s2)     ; obj+0x14 = 0x80001B10
```

and reading at the true base reproduces the doc's own numbers at slot `+0x88`, not `+0x80`:
`*(0x80001B10 + 0x88) = 0x80013C68` and `*(0x80004000 + 0x88) = 0x8003DB94`, and the doc's
marker word `0x801D4464` is at `+0x1A8`, not `+0x1A0`. The correct bases are
**`0x80001B10` (UNIT), `0x80004000` (BUILDING), `0x801C3658` (INFANTRY), `0x801C39D8`
(AIRCRAFT)**, and the "3-D draw" virtual is slot **`+0x88`**. Read at the doc's base,
`+0x1D4` disassembles to `TechnoClass::Do_Cloak` rather than `Draw_Pips`, which is what
sent the previous pass looking. Checks that fixed the bases: `+0x84` is
`TechnoClass::Health_Ratio` (`0x80021854` in both), `+0x18C` is `Pip_Count`
(`0x80016A30` UNIT, `0x80044C24` BUILDING) and `+0x1D4` is `TechnoClass::Draw_Pips`
(`0x801D4530` in both, no override).

The `RTTIType` values seen in the four object constructors, all matching
`defines.h:179`: `sb 1` at RAM `0x801DBE80` (INFANTRY), `sb 3` at `0x80012A5C` (UNIT),
`sb 5` at `0x801E02B4` (AIRCRAFT), `sb 7` at `0x800402CC` (BUILDING).

---

## 10. What is NOT recovered

Written down rather than guessed, with where to look.

1. **`*0x80092830 & 0x4000`, the always-show bit.** The test is decoded (ROM `0x1728A4`)
   but the word is read from about a hundred sites and is plainly a general Special/flags
   word; the bit's name and the UI that sets it are not recovered. To settle it: xref the
   *stores* to `0x80092830` and follow them back to the options screen.
2. **`obj+0x20 == 18` in the overlay gate.** `obj+0x20` is the mission byte and 18 is
   `MISSION_DECONSTRUCTION` **if** the console's `MissionType` matches the 1995 enum. It
   may not: the console's `UnitType` demonstrably does not (§7), and `MISSION_TIMED_HUNT`
   is a late multiplayer addition. To settle it: `docs/animation-drivers.md` decoded the
   Construction Yard's `MISSION_CONSTRUCTION` arm; compare the constant it tests.
3. **The unit scale of `0x800EBE40` against our meshes.** The building z-offset is
   `zHalfExtent[model] >> 3` in whatever units `0x80076934` returns. `UnitTypeClass::
   Dimensions` treats the same tables as `u16 model units` and scales by `48/1000`, which
   is the convention `cart_dimw_for_mesh` (`cnc_eyes.cpp:6166`) already reproduces, so the
   likely answer is "the same model units the mesh walk already yields". **Not proven.**
   To settle it: disassemble `0x80076934` and compare one building's recovered extent
   against `mesh_world_box` for the same mesh.
4. **Why `cmd+0x14` exists.** Both producers compute `Dimensions()`'s return value and
   store it; neither consumer reads it. Probably vestigial. Nothing depends on it.
5. **The screen orientation of the 4x4 tile.** The arithmetic is unambiguous, `t = 0` is at
   `yBase` and `t` grows with world Y, so **memory row 0 renders at the BOTTOM of the pip**
   and the `#525252` highlight lands bottom-left. That is an odd light direction and a
   vertical flip is the classic silent error in exactly this spot, so the first
   implementation should be checked against a screenshot before the row is called done.
6. **Whether any other object class reaches `Draw_Pips`.** All four concrete classes
   inherit `TechnoClass::Draw_Pips` unchanged (`+0x1D4` is `0x801D4530` in every object
   vtable checked). Only `BuildingTypeClass` overrides `Max_Pips` with a non-zero result
   besides `UnitTypeClass` and `AircraftTypeClass`, so on the evidence here the row appears
   for buildings with Capacity, harvesters, transports and armed aircraft, and for nothing
   else. Not exhaustively proven for the civilian and special type classes.

---

## 11. Tools, and a receipt

Everything above was produced with the project's own readers: `tools/romdump/segments.py`
for the segment map and `capstone` MIPS64BE for the disassembly, driven by a throwaway
RAM-addressed wrapper in a scratch directory. No new reader was added to the repo and
nothing under `tools/` was modified.

The eighteen load-bearing numbers re-derive straight from the cartridge with no
disassembler at all. Paste this at the repo root; it printed **18/18 pass** on
`data/rom/cnc_eu.z64` when this file was written.

```python
import struct
ROM = open("data/rom/cnc_eu.z64", "rb").read()
u32 = lambda o: struct.unpack_from(">I", ROM, o)[0]
ok = []
def chk(name, got, want):
    ok.append(got == want)
    print(("PASS " if got == want else "FAIL ") + name)

jw = 0x0C000000 | ((0x8004B638 & 0x0FFFFFFF) >> 2)          # jal 0x8004B638
chk("type-8 enqueue has exactly one caller, in Draw_Pips",
    [i for i in range(0, len(ROM), 4) if u32(i) == jw], [0x1764A0])
chk("dispatcher reads cmd+0x08",              u32(0x04CC58), 0x8F030008)
chk("dispatcher: type 8 -> 0x8004D760",       u32(0x04CCD4), 0x106205A2)
chk("enqueue writes type 8",                  u32(0x04C270), 0x24020008)
chk("enqueue uses sort bucket 0x298",         u32(0x04C264), 0x24040298)
chk("EMPTY pip texture", ROM[0x097C98:0x097CB8].hex(),
    "5295318d318d18c7318d0000000018c7318d0000000018c718c718c718c70001")
chk("FULL pip texture",  ROM[0x097CB8:0x097CD8].hex(),
    "5295318d318d18c7318dd781a70718c7318da7078e4318c718c718c718c70001")
chk("label pointer at RAM 0x80097090",        u32(0x097C90), 0x800048C4)
chk("label text is 'Pips'",                   ROM[0x0054C4:0x0054C8], b"Pips")
chk("stand-off *0x80097AEC == 16",            u32(0x0986EC), 16)
chk("pip pitch  *0x80097AF4 == 48",           u32(0x0986F4), 48)
chk("UnitTypeClass::Max_Pips tests Type==9",  u32(0x18B864), 0x24020009)
chk("UnitTypeClass::Max_Pips returns 7",      u32(0x18B874), 0x24020007)
chk("AircraftTypeClass::Max_Pips returns 5",  u32(0x195F4C), 0x24020005)
chk("BuildingTypeClass::Max_Pips clamps 10",  u32(0x188488), 0x2403000A)
chk("HARV constructed with UnitType 9",       u32(0x18B998), 0x24170009)
chk("UnitClass vtable is 0x80001B10",         u32(0x013544), 0x24421B10)
chk("UNIT vtable +0x1D4 is Draw_Pips",        u32(0x80001B10 + 0x1D4 - 0x7FFFF400), 0x801D4530)
print("%d/%d pass" % (sum(ok), len(ok)))
```
