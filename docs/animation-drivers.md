# Where the animation time `t` comes from — C&C N64 (cnc_eu.z64, EU)

Read-only reverse engineering of the cartridge; nothing in the repository is modified.

Mappings used (given, not re-derived):
`resident rom = ram - 0x7FFFF400` · `CodeOverlay rom = ram - 0x8005E210`.

Every claim below is tagged **DECODED** (I disassembled it and quote the
instructions), **INFERRED** (consistent reading, not proven), or **NOT FOUND**.

---

## 0. Executive answer

**DECODED.** The animation time is **(c) a field of the draw command**, and that
field is written by the object's own 3-D draw virtual. For buildings the value
written is a pure function of

* the **StageClass counter at `building + 0x28`** (this is C&C's `Fetch_Stage()`,
  i.e. the quantity the CNC3D brain exports as `dostage`), and
* the building's **StructType**, through a 59-entry **jump table of code arms**
  at RAM `0x80003D18` — one arm per structure type, each with its own hard-coded
  formula. There is no data table of clip ranges.

`BState` reaches the animation exactly the way the DOS game does it: **DECODED**
`BuildingClass::Begin_Mode` at RAM `0x8003EA90` indexes
`Class->Anims[BState]` (`class + 0x48 + BState*12`, the classic
`AnimControlType {int Start; int Count; int Rate;}`) and writes `Start` into the
stage counter and `Rate` into the stage rate. The `Anims` table survives in the
cartridge and **is flat — `{0,1,0}` for all 6 states of all 64 structure types**
(§6), so on the N64 the *interesting* motion comes from the per-StructType arms,
not from `Anims`.

And the single most valuable line in the whole pass: **DECODED** — for a
Construction Yard whose mission is `MISSION_CONSTRUCTION`, the ROM does **not**
draw model slot 17 (FACT). It draws **model index 8 → slot 18 = `MCVANIM`, the
MCV deploy rig**, at `frame = dostage * 1.5625`, i.e. `t = dostage*250 + 1`
(RAM `0x8003DC68`–`0x8003DC94`). The rig substitution the project currently does
by hand is what the cartridge itself does, and the frame formula is now decoded
rather than guessed.

---

## 1. The draw command and its float — `drawCmd + 0x14` IS the clip time

### 1.1 The producer side (DECODED)

Eight sibling enqueue functions build a type-1 (MODEL) draw command. All have the
same signature `f(int model, float frame, int x, int y, int flags, int house, …)`.
The canonical one, RAM `0x8004A03C` / ROM `0x004AC3C`:

```
ROM 004AC3C / RAM 8004A03C  addiu $sp, $sp, -0x30
ROM 004AC44 / RAM 8004A044  mtc1  $a1, $f20          ; a1 = the FLOAT argument
ROM 004AC64 / RAM 8004A064  lw    $s0, 0x44($sp)     ; house
ROM 004AC6C / RAM 8004A06C  jal   0x801fcc3c         ; alloc(model, house)
ROM 004AC80 / RAM 8004A080  sw    $v0, 8($v1)        ; cmd+0x08 = type 1 (MODEL)
ROM 004AC84 / RAM 8004A084  sw    $s0, 0xc($v1)      ; cmd+0x0C = house
ROM 004AC88 / RAM 8004A088  sw    $s1, 0x10($v1)     ; cmd+0x10 = MODEL INDEX
ROM 004AC8C / RAM 8004A08C  swc1  $f20, 0x14($v1)    ; cmd+0x14 = THE FLOAT   <<<<
ROM 004AC90 / RAM 8004A090  sw    $s2, 0x18($v1)     ; cmd+0x18 = x
ROM 004AC94 / RAM 8004A094  sw    $s3, 0x1c($v1)     ; cmd+0x1C = y
ROM 004AC9C / RAM 8004A09C  sw    $zero, 0x24($v1)   ; cmd+0x24 = 0  (mode flags)
ROM 004ACA0 / RAM 8004A0A0  sw    $v0, 0x20($v1)     ; cmd+0x20 = draw flags
```

The other seven are byte-identical up to which extra fields they append and what
they put in `cmd+0x24`:

| enqueue (RAM) | cmd+0x24 | extra fields |
|---|---|---|
| `0x8004A03C` | 0 | — |
| `0x8004A254` | 2 | `cmd+0x2C` |
| `0x8004A2E8` | 6 | `cmd+0x2C`, **`cmd+0x30` = float** (stealth fade) |
| `0x8004A384` | 3 | `cmd+0x28`, `cmd+0x2C` |
| `0x8004A420` | 0 | (+ a model-index remap 0x67..0x79 → +0x61) |
| `0x8004A4D0` | 2 | `cmd+0x2C` |
| `0x8004A564` | 3 | `cmd+0x28`, `cmd+0x2C` |
| `0x8004A600` | **8** | `cmd+0x14 = 0`, **`cmd+0x34` = float** (build-up reveal) |

### 1.2 The consumer side (DECODED)

RAM `0x8004C108` is the model-draw-command handler. Raw words verified:

```
ROM 004CD08 / RAM 8004C108  8FAF0470  lw    $t7, 0x470($sp)   ; t7 = drawCmd
ROM 004CD0C / RAM 8004C10C  25F2000C  addiu $s2, $t7, 0xc     ; s2 = drawCmd+0x0C
ROM 004CD1C / RAM 8004C11C  3C03800A  lui   $v1, 0x800A
ROM 004CD20 / RAM 8004C120  24639A38  addiu $v1, $v1, -0x65C8 ; v1 = 0x80099A38
ROM 004CD28 / RAM 8004C128  8E510004  lw    $s1, 4($s2)       ; s1 = cmd+0x10 = model idx
ROM 004CD2C / RAM 8004C12C  C6560008  lwc1  $f22, 8($s2)      ; f22 = cmd+0x14 (float)
ROM 004CD30 / RAM 8004C130  8E550014  lw    $s5, 0x14($s2)    ; s5 = cmd+0x20 = flags
ROM 004CD34 / RAM 8004C134  00111100  sll   $v0, $s1, 4
ROM 004CD38 / RAM 8004C138  0043A021  addu  $s4, $v0, $v1     ; s4 = &modelTable[idx+10]
```

`0x80099A38 - 0x80099998 = 0xA0 = 10 * 16`, so the +10 bias is materialised right
here as an immediate. There is no `lb rX,0xa5(rY)` anywhere near ROM `0x4CD08`
(nearest are `0x46BF4` and `0x173038`), so the "type test" reading of this address
does not survive contact with the bytes.

### 1.3 `f22` → the track evaluator (DECODED — the old "fade or size" note is wrong)

Default path, RAM `0x8004C7A8`:

```
ROM 004D3A8 / RAM 8004C7A8  lui   $at, 0x8000
ROM 004D3AC / RAM 8004C7AC  lwc1  $f0, 0x4964($at)   ; *0x80004964 = 160.0f
ROM 004D3B0 / RAM 8004C7B0  mul.s $f0, $f22, $f0     ; cmd+0x14 * 160
ROM 004D3B8 / RAM 8004C7B8  lwc1  $f6, 0x4968($at)   ; *0x80004968 = 1.0f
ROM 004D3BC / RAM 8004C7BC  lw    $v0, 0x30($sp)
ROM 004D3C0 / RAM 8004C7C0  add.s $f6, $f0, $f6      ; t = frame*160 + 1
ROM 004D3C4 / RAM 8004C7C4  lw    $a2, 0x198($s7)    ; a2 = display list ptr
ROM 004D3C8 / RAM 8004C7C8  lw    $a0, 0xc($v0)      ; a0 = scene-graph node
ROM 004D3CC / RAM 8004C7CC  mfc1  $a1, $f6           ; a1 = t
ROM 004D3D0 / RAM 8004C7D0  jal   0x8008fd14
```

`0x8008FD14` is the scene-graph evaluator, **proved** because the known evaluator
entry `0x8007BE30(scene, dl, t)` calls it with its own `t`:

```
ROM 007CADC / RAM 8007BEDC  lw   $v0, ($s5)
ROM 007CAE0 / RAM 8007BEE0  mfc1 $a1, $f20      ; f20 = 0x8007BE30's 3rd arg = t
ROM 007CAE4 / RAM 8007BEE4  lw   $a0, 0xc($v0)
ROM 007CAE8 / RAM 8007BEE8  jal  0x8008fd14
ROM 007CAEC / RAM 8007BEEC  move $a2, $s3
```

and `0x8008FD14` tail-calls the node walker `0x8008F988(node, t, dl)`, which hands
`t` straight to the handle dispatcher:

```
ROM 0090660 / RAM 8008FA60  lw   $v0, 0x38($fp)
ROM 0090664 / RAM 8008FA64  lw   $a0, 8($v0)        ; node->handle
ROM 0090668 / RAM 8008FA68  lw   $a1, 0x3c($fp)     ; t
ROM 009066C / RAM 8008FA6C  lw   $a2, 0x1c($fp)     ; out matrix
ROM 0090670 / RAM 8008FA70  jal  0x8008a860
```

and `0x8008A860` does the virtual dispatch into the FEEB/CAFEDEAD `apply`:

```
ROM 008B48C / RAM 8008A88C  lw   $v1, ($v0)         ; handle->cls
ROM 008B494 / RAM 8008A894  lw   $v0, 8($v1)        ; cls+0x08 = apply
ROM 008B498 / RAM 8008A898  lw   $a0, 4($a0)        ; handle->data
ROM 008B49C / RAM 8008A89C  lw   $a1, 0x5c($fp)     ; t
ROM 008B4A4 / RAM 8008A8A4  jalr $v0
```

**Therefore `drawCmd+0x14` is the animation frame; `t = frame*160 + 1`.** The
`anim_extract.py` note ("NOT CONFIRMED … reads as a fade or size argument") is
**refuted**. The fade *does* exist but it lives at `cmd+0x30` and is written only
by enqueue `0x8004A2E8` (stealth, §7).

### 1.4 Two alternate time paths in the same handler (DECODED)

* **Model-clock path**, RAM `0x8004C6CC`, taken when `cmd+0x20 & 8` and the
  per-slot record `0x80057108(idx)` has a non-zero field `+0x14`:
  `t = rec[0x14]*160 + 1.0` (`sll/addu/sll` = ×160; bias `*0x80004958 = 1.0`).
* **Build-up reveal path**, RAM `0x8004C6FC`, taken when `cmd+0x24 & 8` (only
  enqueue `0x8004A600` sets that) **and** `cmd+0x34 < 1.0` — see §5.

---

## 2. Who calls the enqueues: the per-class 3-D draw virtual (slot +0x80)

**DECODED.** Objects carry a vtable at `obj+0x14`; **vtable slot `+0x80` is the
"draw me as a 3-D model" virtual.** Six sibling vtables exist (found by the shared
marker word `0x801D4464` at vtable+0x1A0):

| vtable base (RAM) | nearby class-name string | slot +0x80 |
|---|---|---|
| `0x80001B18` | `"UNITS"` (ROM 0x26BC) | `0x80013C68`  (UnitClass) |
| `0x80004008` | **`"STRUCTURES"`** (ROM ~0x4Bxx) | **`0x8003DB94`  (BuildingClass)** |
| `0x801C2BE0` | — | `0x801D0A60` (base/no-op) |
| `0x801C3290` | — | `0x801D0A60` |
| `0x801C3660` | `"INFANTRY"` (ROM 0x1653xx) | `0x801DC4C0` |
| `0x801C39E0` | `"Aircraft was firing LASER!"` | `0x801E0464` (AircraftClass) |

---

## 3. BUILDINGS — `BuildingClass::Draw3D`, RAM `0x8003DB94` / ROM `0x03E794`

### 3.1 Prologue (DECODED)

```
ROM 003E7CC / RAM 8003DBCC  lw    $v0, 4($s6)      ; s6 = building; v0 = Class
ROM 003E7D0 / RAM 8003DBD0  lw    $s3, ($v0)       ; s3 = Class->modelIndex
ROM 003E7D4 / RAM 8003DBD4  beqz  $s6, 0x8003dbe0
ROM 003E7DC / RAM 8003DBDC  addiu $a1, $s6, 0x28   ; &StageClass sub-object
ROM 003E7E8 / RAM 8003DBE8  lhu   $s0, ($a1)       ; s0 = STAGE  (u16)  <<<< dostage
ROM 003E7F4 / RAM 8003DBF4  lb    $v1, 0x66($s6)   ; v1 = BState (signed byte)
ROM 003E7F8 / RAM 8003DBF8  bnez  $v1, 0x8003dd20  ; BState != 0 -> the big switch
```

So the two inputs are **`building+0x28` (u16 stage)** and **`building+0x66`
(BState)**.

`building+0x28` is a C++ base sub-object (`beqz obj; addiu r,obj,0x28` is the
null-checked base cast the compiler emits). Its layout, read off the writers in
§4: `+0x28 u16 Stage`, `+0x2A u8 Rate`, `+0x2B u8 Timer`. **INFERRED (strongly):**
this is C&C's `StageClass`, and `Stage` is what `Fetch_Stage()` returns —
i.e. the brain's `dostage`.

### 3.2 `BState == 0` (BSTATE_CONSTRUCTION) — the build-up / sell branch (DECODED)

```
ROM 003E800 / RAM 8003DC00  lw   $v0, 0x14($s6); lw $v0,0xd0($v0); jalr  ; Mission
ROM 003E818 / RAM 8003DC18  addiu $v1,$zero,0x11
ROM 003E81C / RAM 8003DC1C  bne  $v0,$v1, 0x8003dc2c
ROM 003E828 / RAM 8003DC28  addiu $s1,$zero,1        ; s1 = 1  (MISSION 0x11)
ROM 003E844 / RAM 8003DC44  addiu $v1,$zero,0x12
ROM 003E848 / RAM 8003DC48  beql $v0,$v1, 0x8003dc50
ROM 003E84C / RAM 8003DC4C  move $s1,$zero           ; s1 = 0  (MISSION 0x12)
ROM 003E850 / RAM 8003DC50  jal  0x8004577c          ; = { return obj->Class; }
ROM 003E858 / RAM 8003DC58  lb   $v1, 0xa5($v0)      ; Class+0xA5 = StructType
ROM 003E85C / RAM 8003DC5C  addiu $v0,$zero,6        ; 6 = STRUCT_CONST (FACT)
ROM 003E860 / RAM 8003DC60  bne  $v1,$v0, 0x8003dcb4
```

**INFERRED (high confidence):** mission `0x11` = `MISSION_CONSTRUCTION`,
`0x12` = `MISSION_DECONSTRUCTION` (they are adjacent in the TD `MissionType`
enum and are exactly the two missions a `BSTATE_CONSTRUCTION` building can be in).

**FACT (StructType 6), constructing — THE MCV RIG (DECODED):**

```
ROM 003E868 / RAM 8003DC68  beqz  $s1, 0x8003dca4
ROM 003E86C / RAM 8003DC6C  addiu $a0, $zero, 8       ; MODEL INDEX 8 -> SLOT 18 = MCVANIM
ROM 003E874 / RAM 8003DC74  lwc1  $f2, 0x3d14($at)    ; *0x80003D14 = 1.5625f
ROM 003E878 / RAM 8003DC78  mtc1  $s0, $f0            ; stage
ROM 003E87C / RAM 8003DC7C  cvt.s.w $f0, $f0
ROM 003E880 / RAM 8003DC80  mul.s $f0, $f0, $f2       ; frame = stage * 1.5625
ROM 003E888 / RAM 8003DC88  mfc1  $a1, $f0
ROM 003E894 / RAM 8003DC94  jal   0x8004a420          ; ordinary MODEL command
```

⇒ **`t = 1.5625 * dostage * 160 + 1 = 250*dostage + 1`.** Slot 18's clip domain is
`0..16048`, so the clip is fully traversed at `dostage = 64`.

**FACT, deconstructing (DECODED):** `a0 = 7` (slot 17 = FACT), then the common tail.

**Any other structure (DECODED):**

```
; constructing:
ROM 003E8B8 / RAM 8003DCB8  move $a0, $s3                  ; Class->modelIndex
ROM 003E8C4 / RAM 8003DCC4  add.s $f0, $f0, $f0            ; f0 = 2*stage
ROM 003E8CC / RAM 8003DCCC  lwc1 $f2, 0x4c($v0)            ; Class+0x4C
ROM 003E8D0 / RAM 8003DCD0  cvt.s.w $f2, $f2
ROM 003E900 / RAM 8003DD00  div.s $f0, $f0, $f2            ; u = 2*stage / Class[0x4C]
ROM 003E910 / RAM 8003DD10  jal  0x8004a600                ; the REVEAL command
; deconstructing:
ROM 003E8E4 / RAM 8003DCE4  subu $v0, $v1, $s0             ; Class[0x4C] - stage
ROM 003E8F0 / RAM 8003DCF0  add.s $f0, $f0, $f0            ; *2
ROM 003E900 / RAM 8003DD00  div.s $f0, $f0, $f2            ; u = 2*(cnt-stage)/cnt
```

**`Class + 0x4C` is `Anims[BSTATE_CONSTRUCTION].Count`** — see §4/§6 — i.e. the
brain's **`makecnt`**. So, in the cartridge's own arithmetic:

```
u  =  2 * dostage / makecnt              (building up)
u  =  2 * (makecnt - dostage) / makecnt  (selling)
```

Note the **factor 2** and the **absence of any `+1`**. The CNC3D note that uses
`u = (dostage+1)/makecnt` is off by the +1 and by the 2× rate.

### 3.3 `BState != 0` — the per-StructType jump table (DECODED)

```
ROM 003E920 / RAM 8003DD20  lw   $v0,0x14($s6); lw $v0,0x84($v0); jalr  ; virtual
ROM 003E934 / RAM 8003DD34  sltiu $v0, $v0, 0x80
ROM 003E93C / RAM 8003DD3C  sll  $s1, $v0, 3          ; draw-flag bit 3
ROM 003E938 / RAM 8003DD38  jal  0x8004577c           ; Class
ROM 003E940 / RAM 8003DD40  lb   $v1, 0xa5($v0)       ; StructType
ROM 003E944 / RAM 8003DD44  sltiu $v0, $v1, 0x3b      ; < 59
ROM 003E948 / RAM 8003DD48  beqz $v0, 0x8003e1f8      ; walls etc -> frame 0
ROM 003E950 / RAM 8003DD50  addiu $v0, $v0, 0x3d18    ; table base RAM 0x80003D18
ROM 003E954 / RAM 8003DD54  sll  $v1, $v1, 2
ROM 003E958 / RAM 8003DD58  addu $v1, $v1, $v0
ROM 003E95C / RAM 8003DD5C  lw   $v0, ($v1)
ROM 003E960 / RAM 8003DD60  jr   $v0                  ; <<<< 59-entry jump table
```

All arms converge on `RAM 0x8003E204: jal 0x8004a420` (the ordinary MODEL command,
`cmd+0x14 = a1`), except PROC which emits two commands.

**The decoded table** (`s0` = stage at `building+0x28`; "model" is the value put in
`cmd+0x10`, so the model-table slot is `model + 10`):

| StructType | arm (RAM) | model → slot | frame written to `cmd+0x14` |
|---|---|---|---|
| 0 WEAP | `0x8003DD68` | 2 → 12 | `doorStage(bldg) * 6.55556` (`*0x80003E04`); doorStage from `0x801D3C70` |
| 1 GTWR | `0x8003DD94` | 3 → 13 | `stage * 10` |
| 2 ATWR | `0x8003DDB8` | 4 → 14 | `stage` |
| 3 OBLI | `0x8003E1F8` | Class->model | `0` |
| 4 HQ | `0x8003DDD0` | 13 → 23 | `stage` |
| 5 GUN | `0x8003DEE8` | 6 → 16 | `wrap(facing[+0x3C] * 0.15625, 40)` — turret facing |
| 6 FACT | `0x8003DFC0` | 7 → 17 | `stage` |
| 7 PROC | `0x8003DFD8` | 9 → 19 **and** 10 → 20 | piecewise, see below |
| 8 SILO | `0x8003E0D4` | 11 → 21 | `clamp(tiberium*5/capacity, 0..4) * 10` |
| 9 HPAD | `0x8003E15C` | 12 → 22 | `stage * 10` |
| 10 SAM | `0x8003DE24` | 14 → 24 | state-dependent, see below |
| 11 AFLD | `0x8003DF48` | 15 → 25 | `stage * 2` |
| 12 NUKE | `0x8003DF64` | 16 → 26 | `stage < 20 ? stage : 39 - stage` |
| 13 NUK2 | `0x8003DF64` | 17 → 27 | same fold |
| 14 HOSP | `0x8003E1F0` | Class->model | `0` |
| 15 PYLE | `0x8003E1A4` | 20 → 30 | `stage * 10` |
| 16 FIX | `0x8003E180` | 22 → 32 | `stage * 10` |
| 17 BIO | `0x8003E1F8` | Class->model | `0` |
| 18 HAND | `0x8003DE00` | 21 → 31 | `stage * 10` |
| 19 TMPL | `0x8003E1F8` | Class->model | `0` |
| 20 EYE | `0x8003DDE8` | 1 → 11 | `stage` |
| 21 MISS | `0x8003E1F8` | Class->model | `0` |
| 22–39 V01–V18 | `0x8003E1F0` | Class->model | `0` |
| **40 V19** | `0x8003E1C8` | 63 → 73 | `stage * 2.5` (double `*0x80003E38`) |
| 41–58 V20–V37 | `0x8003E1F0` | Class->model | `0` |
| 59–63 SBAG/CYCL/BRIK/BARB/WOOD | (guard) | Class->model | `0` |

Cross-check that pins the indexing: the user's nine one-shot-clip buildings —
FACT, WEAP, ATWR, SAM, AFLD, NUKE, NUK2, HAND, V19 — **every single one** has a
dedicated arm here. Nothing else would line up if the enum decode were wrong.

**SAM detail (DECODED), `0x8003DE24`:**
```
lbu $v1, 0x23($s6)                     ; a state byte
if (v1==2 || v1==3 || v1==6):          ; tracking
    f = wrap(facing[+0x3C]*0.15625, 40)          ; 0x80003E08/0E0C/0E10
    frame = (40 - f)*5 + 200                     ; 0x80003E14=5, 0x80003E18=200
else:
    if (stage >= 0x21) stage = 0x40 - stage
    frame = stage * 12.5                         ; 0x80003E1C
```
**Open discrepancy (NOT RESOLVED):** those frames reach 400, i.e. `t` up to 64001,
while `anim/index.json` reports slot 24's clip as `t = 0..16000`. Either the
extractor under-reports SAM's track domain (it under-reported slot 18's node set
before) or the evaluator's end-clamp is doing the work.
Worth a targeted re-extract of slot 24 before anyone builds on the SAM numbers.

**PROC detail (DECODED), `0x8003DFD8`:**
```
if (stage >= 60) return (nothing drawn)
if (stage >= 30) stage -= 30
if (stage < 6)   -> model 9,  frame = 10.0
elif (stage <12) -> model 9,  frame = (stage % 2) * 10        ; 0x80003E2C = 10
else:
     f20 = (stage<19) ? stage-11 : (stage<24 ? stage-11 : 29-stage)
     f0  = (stage in [19,24)) ? stage-17 : 1.0                ; 0x80003E30 = 1.0
     model 9  frame = f0  * 10                                ; 0x80003E34 = 10
     if (stage != 29) model 10 (slot 20) frame = f20 * 10
```
Slot 20 — the model-table entry whose name-array string is empty and which the
project already flagged as a one-shot — is the refinery's **second** command.

**Consistency spot-checks (my arithmetic, against `anim/index.json`):**
NUKE/NUK2 fold to frames 0..19 and their clips are `0..3200` = 20 frames ✔ ·
AFLD `stage*2` and its clip is `0..6400` = 40 frames ✔ · HAND `stage*10`,
clip `0..16000` = 100 frames ✔ · WEAP `door*6.5556`, clip 100 frames, so a
16-step door lands at 98.3 ✔ · SAM ✘ (above).

---

## 4. What writes the stage — `Begin_Mode`, RAM `0x8003EA90` (DECODED)

This is the BState → animation link, and it is the DOS design intact:

```
ROM 003F690 / RAM 8003EA90  lb    $v1, 0x66($s1)     ; BState
ROM 003F694 / RAM 8003EA94  lw    $a0, 4($s1)        ; Class
ROM 003F698 / RAM 8003EA98  sll   $v0, $v1, 1
ROM 003F69C / RAM 8003EA9C  addu  $v0, $v0, $v1      ; BState*3
ROM 003F6A0 / RAM 8003EAA0  sll   $v0, $v0, 2        ; BState*12
ROM 003F6A4 / RAM 8003EAA4  addiu $v0, $v0, 0x48     ; + 0x48
ROM 003F6A8 / RAM 8003EAA8  lbu   $v1, 0x66($s1)
ROM 003F6AC / RAM 8003EAAC  sltiu $v1, $v1, 2        ; BState < 2 ?
ROM 003F6B0 / RAM 8003EAB0  beqz  $v1, 0x8003eae4
ROM 003F6B4 / RAM 8003EAB4  addu  $s2, $a0, $v0      ; s2 = &Class->Anims[BState]
; --- BState < 2 (CONSTRUCTION, IDLE): rate goes through a global scaler
ROM 003F6C0 / RAM 8003EAC0  addiu $s0, $s1, 0x28
ROM 003F6C8 / RAM 8003EAC8  lw    $a0, 0x661c($v0)   ; global 0x8009661C
ROM 003F6CC / RAM 8003EACC  lw    $a1, 8($s2)        ; Anims[BState].Rate
ROM 003F6D0 / RAM 8003EAD0  jal   0x80021f00
ROM 003F6D8 / RAM 8003EAD8  sb    $v0, 2($s0)        ; Stage.Rate
ROM 003F6E0 / RAM 8003EAE0  sb    $v0, 3($s0)        ; Stage.Timer
; --- BState >= 2: rate taken raw
ROM 003F6F0 / RAM 8003EAF0  lbu   $v0, 0xb($s2)      ; (u8)Anims[BState].Rate
ROM 003F6F4 / RAM 8003EAF4  sb    $v0, 2($v1)
ROM 003F6F8 / RAM 8003EAF8  sb    $v0, 3($v1)
; --- both
ROM 003F708 / RAM 8003EB08  lw    $v0, ($s2)         ; Anims[BState].Start
ROM 003F70C / RAM 8003EB0C  sh    $v0, ($v1)         ; Stage.Stage = Start   <<<<
```

`class + 0x48 + BState*12` with a `{ +0x00 Start, +0x04 Count, +0x08 Rate }`
record is `AnimControlType Anims[BSTATE_COUNT]` byte for byte, and it confirms
`Class+0x4C = Anims[BSTATE_CONSTRUCTION].Count = makecnt` used in §3.2.

Other decoded stage writers (all `building+0x28`):
`0x8003E794` `sh $zero` + `0x8003E7B4/B8` rate/timer 0 (full reset) ·
`0x8003ED94`, `0x8003EE00`, `0x80043E58` `sh $zero` ·
`0x8003EDAC` `sb 0x0F` rate · `0x800439AC` `sb 2` rate · `0x80043DE4` `sb 2` rate ·
`0x80043DF8` `sh 0x30` (stage := 48, rate 7) · `0x800421E4/F8` a generic
`Set_Rate(scaled)/Set_Stage(*(int*)s1)` helper at `0x800421C8`.

**NOT FOUND:** the per-tick `StageClass::Graphic_Logic` that decrements the timer
and increments `Stage`. I did not isolate it. It is certainly there (the rate/timer
byte pair at `+0x2A/+0x2B` is written in the classic `Rate; Timer=Rate` idiom), but
I stopped at the writers above rather than assert an address I had not read.

---

## 5. The section-by-section build-up reveal is NOT a keyframed clip (DECODED)

Enqueue `0x8004A600` writes `cmd+0x14 = 0`, `cmd+0x24 = 8`, `cmd+0x34 = u`:

```
ROM 004B250 / RAM 8004A650  sw   $zero, 0x14($v1)     ; frame = 0
ROM 004B260 / RAM 8004A660  swc1 $f20, 0x34($v1)      ; u = 2*dostage/makecnt
ROM 004B268 / RAM 8004A668  addiu $v0, $zero, 8
ROM 004B26C / RAM 8004A66C  sw   $v0, 0x24($v1)       ; mode flag 8
```

and the handler takes a completely different route for it:

```
ROM 004D2FC / RAM 8004C6FC  lw    $v0, 0x18($s2)      ; cmd+0x24
ROM 004D300 / RAM 8004C700  andi  $v0, $v0, 8
ROM 004D304 / RAM 8004C704  beqz  $v0, 0x8004c7a8     ; not a reveal -> normal t
ROM 004D30C / RAM 8004C70C  lwc1  $f0, 0x28($s2)      ; u  (cmd+0x34)
ROM 004D314 / RAM 8004C714  lwc1  $f8, 0x495c($at)    ; 1.0
ROM 004D318 / RAM 8004C718  c.lt.s $f0, $f8           ; u < 1.0 ?
ROM 004D320 / RAM 8004C720  bc1f  0x8004c7a8          ; u >= 1 -> ordinary full draw
ROM 004D330 / RAM 8004C730  mul.s $f0, $f22, $f0      ; (cmd+0x14 = 0) * 160
ROM 004D338 / RAM 8004C738  add.s $f6, $f0, $f6       ; t = 1.0
ROM 004D33C / RAM 8004C73C  lw    $a2, 0x474($sp)     ; a SCRATCH display list
ROM 004D348 / RAM 8004C748  jal   0x8008fd14          ; evaluate scene at t into it
...
ROM 004D36C / RAM 8004C76C  lhu   $v0, 2($v0)         ; N   (primitive count)
ROM 004D370 / RAM 8004C770  lwc1  $f2, 0x28($s2)      ; u
ROM 004D37C / RAM 8004C77C  mul.s $f2, $f2, $f0       ; u * N
ROM 004D390 / RAM 8004C790  trunc.w.s $f6, $f2
ROM 004D394 / RAM 8004C794  swc1  $f6, 0x460($sp)
ROM 004D398 / RAM 8004C798  jal   0x80076504          ; emit only the first u*N
```

**So the ordinary structure build-up is a partial display-list emit gated by
`u = 2*dostage/makecnt`, not a track evaluation.** It runs at 2× and finishes at
`dostage = makecnt/2`; from there on the normal full draw takes over. That is a
decoded basis for the project's "2× rate" note, and it explains why FACT's
"crane cycle" was never a build-up rig.

---

## 6. Is there a per-slot / per-state clip-range table? (DECODED — yes, and it is inert)

The 64-record structure-type array is at **RAM `0x801C41E4` / ROM `0x0165FD4`,
stride `0xAC`**; the pointer array into it is at RAM `0x801C3D44` / ROM `0x0165B34`.
Verified field map (relative to the record base = `inline Ident[8]` minus 8):

| off | meaning | evidence |
|---|---|---|
| `+0x00` | model index (`-1` = none) | FACT 7→slot 17, PROC 9→19, NUKE 16→26, HAND 21→31, V19 63→73 — all match `unit_models.json` |
| `+0x08` | `char Ident[8]` | `"FACT"`, `"NUKE"`, … |
| `+0x1C` | TechLevel | FACT 3, EYE 10 |
| `+0x20` | Cost | FACT 5000, PROC 2000, NUKE 300, TMPL 3000, GTWR 500, SAM 750 — exact TD costs |
| `+0x48` | `AnimControlType Anims[6]` | proved by `Begin_Mode` `class+0x48+BState*12` |
| `+0xA5` | `StructType` | proved by the jump-table index and by 0/1/2/3=WEAP/GTWR/ATWR/OBLI |

**Every one of the 64 records has `Anims[0..5] = {Start 0, Count 1, Rate 0}`.**
I dumped all 384 triples; they are byte-identical. So:

* The DOS state→frame-range table **survives in the cartridge but carries no
  animation**. Nothing in the N64 build plays a sub-range of a clip per BState.
* `makecnt` (= `Anims[0].Count`) is **1** for every structure in the ROM data.
  Which means `u = 2*dostage/1` — the reveal in §5 completes as soon as
  `dostage >= 1`, unless something at runtime overwrites the count. **NOT FOUND:**
  any runtime write to `Class+0x4C`; the class array lives in the read-only
  CodeOverlay image, so I would expect it to stay 1. Flagging this as the one
  place where my decode and the project's measured `makecnt = 32` disagree, and I
  have not resolved it. The likely reading (**INFERRED**) is that the CNC3D brain's
  `makecnt` comes from the *PC* GPL build and the N64 build simply does not use
  the same number.

**The real per-type mapping is code, not data:** the 59-entry jump table at RAM
`0x80003D18` / ROM `0x0004918`, §3.3. That is the "which sub-range do I play in
state X" mechanism, and it is a switch statement.

Around the name-pointer array at ROM `0x1DE924` I found **no** clip-range table —
searched, **NOT FOUND**.

---

## 7. The other classes, for completeness (DECODED)

**UnitClass::Draw3D, RAM `0x80013C68`.** `frame` for `cmd+0x14` is:
* stealth fade path (`obj+0x48 & 0x00C00000`, enqueue `0x8004A2E8`):
  `cmd+0x14 = 0` and the float goes to **`cmd+0x30`** — `RAM 0x80013D8C swc1 $f0,0x1c($sp)`.
  Constants `0x80001A44/48/4C/50 = 0.0263158, 1.0, 0.0263158, 0.8`. **This is the
  "fade" the old note was looking at — it is a different command field.**
* turret path: `frame = (obj[0x84] - obj[0x3C]) * 0.15625` wrapped into `[0,40)`
  (`0x80001A60/64/68`) — a *facing*, re-using the same float slot.
* motion path, RAM `0x800140C0` (needs `obj+0x78 & 0x80`, `obj[0x3D]==obj[0x3C]`,
  `obj+0x6A == 0`, `!(obj+0x78 & 0x200)`):
  `f = (obj[0x28] * 10) % 80; if (f > 40) f = 80 - f;` — a ping-pong over `0..40`,
  which is exactly the `0..6400` (= 40 frames) clip domain every vehicle slot has
  in `index.json`. Same `+0x28` field as buildings.
* otherwise `frame = 0`.

**AircraftClass::Draw3D, RAM `0x801E0464`.** `f20 = (obj[0x28] * K) % 10` with
`K` from `0x80097A4C`/`0x80097A50`; plus a fixed model `0xC2` rotor command.

**Unreferenced:** RAM `0x801D38AC` computes `frame = 2 * a2` and calls
`0x8004A420`. It has **no `jal`, no `j` and no data reference anywhere in the
ROM** — dead code, or reached by a pointer I could not find. Do not build on it.

---

## 8. Where trails died

* **`StageClass::Graphic_Logic`** (the per-tick stage advance) — NOT FOUND. I
  found the seeders and resetters (§4) but did not isolate the incrementer.
* ~~**`makecnt` = 1 in ROM data vs 32 measured in the brain** -- unresolved (§6).~~
  **RESOLVED, and there was never a disagreement.** They are the same C++
  field in two different states. `Anims[BSTATE_CONSTRUCTION]` is initialised to
  `{Start 0, Count 1, Rate 0}` by the `BuildingTypeClass` constructor
  (`bdata.cpp:3739-3741`) and PATCHED in `One_Time` from the building's own buildup art
  (`bdata.cpp:3839-3849`): `count = Get_Build_Frame_Count("<NAME>MAKE.SHP")` and
  `timedelay = (5 * TICKS_PER_SECOND) / count`. The cartridge ships no MAKE.SHP art, so
  its 384 identical `{0,1,0}` triples are an UNPATCHED table rather than a different
  design decision. Nobody had put those two passages of the same file side by side.

  **And "32" was never the brain's answer for every structure.** It is the Construction
  Yard's frame count specifically. The real values are per building: 36 TMPL, 32 FACT,
  30 SAM, 20 for the power plant, the barracks and most of the game, 16 and 14 for
  others. The "32 for everything" reading leaked out of the MCV-deploy work, which only
  ever looks at FACT, and it is contradicted twice inside the very file that asserts it
  (`cnc_eyes.cpp` says "HAND reports 20, AFLD 14" and "PROC's makecnt of 20" elsewhere).
* **SAM frames reach 400 vs a 100-frame clip** — unresolved (§3.3). Suspect the
  extractor under-reports slot 24's domain; needs a re-extract, not more disasm.
* **Which of `building+0x23` / `+0x66` values map to which `BStateType` names** —
  I read the *offsets* and the *branch structure*, but I did not decode the
  numeric enum values back to `BSTATE_*` / mission names beyond the two
  construction missions. The 0x11/0x12 mission identification is INFERRED.
* **`obj+0x28` = `StageClass::Stage`** is INFERRED (from the base-cast idiom, the
  `{u16 Stage, u8 Rate, u8 Timer}` layout, and `Begin_Mode` writing `Anims[].Start`
  into it) — not proven by a symbol.

---

## 9. Addendum — four of the five dead trails in §8, walked

Same ROM, same mappings. Everything below is **DECODED** unless marked otherwise.

### 9.1 `Graphic_Logic` found: it is inlined at the top of `BuildingClass::AI`, RAM `0x8003E8B4`

§8 said the per-tick stage incrementer was NOT FOUND. It is the first twenty
instructions of `BuildingClass::AI`, which is the DOS routine compiled essentially
verbatim. `building+0x2A` is the countdown **Timer**, `+0x2B` the reload **Rate**:

```
8003E8E0  lbu  $a0, 3($v1)      ; Rate
8003E8E4  beqz $a0, 0x8003e91c  ; rate 0 -> the counter never moves
8003E8EC  lbu  $v0, 2($v1)      ; Timer
8003E8F0  addiu $v0, $v0, -1
8003E8FC  bnez $v0, ...
8003E904  lhu  $v0, ($v1)       ; Stage
8003E910  addiu $v0, $v0, 1     ; Stage++
```

The rest of the function maps onto `building.cpp` line for line and pins several
things that were inferred before:

| RAM | DOS source | what it pins |
|---|---|---|
| `0x8003E920`-`64` | `(!Class->IsTurretEquipped && *this != STRUCT_OBELISK) \|\| Mission == MISSION_CONSTRUCTION \|\| Mission == MISSION_DECONSTRUCTION` | **`building+0x20` is Mission**, and `0x11`/`0x12` really are CONSTRUCTION/DECONSTRUCTION (§3.2 had this INFERRED). `Class+0x38 & 0x00080000` is `IsTurretEquipped`. StructType 3 is OBLI. |
| `0x8003E9C8`-`0x8003EA08` | `Special.IsMCVDeploy && *this == STRUCT_CONST && Mission == MISSION_DECONSTRUCTION && Fetch_Stage() == (42 - 19)` | the literal `0x17` in the ROM is DOS's `42-19`. Global `0x80092830 & 0x00040000` is `Special.IsMCVDeploy`. |
| `0x8003EA88`-`0x8003EB0C` | the `if (toloop) { Set_Rate(...); Set_Stage(ctrl->Start); }` block | **this is not `Begin_Mode`** — §4 named it that. It is AI's animation-restart, and it reads the same `Anims[BState]` record, which is why the field map in §4 stands. |
| `0x8003ED40`-`0x8003EE18` | the Obelisk charging block | `Set_Stage(0); Set_Rate(OBELISK_ANIMATION_RATE)` — the `rate 15` writer §4 could not place. `building+0x68` carries `IsCharging` at bit `0x04000000`. |

### 9.2 `0x801D3C70` is `DoorClass::Door_Stage()`, and it settles the War Factory

```
lw   $v0, 0x48($a0) ; srl 12 ; andi 3      ; State, a 2-bit bitfield
 0 IS_CLOSED  -> 0
 1 IS_OPENING -> *(u16*)(a0+0x30)                       ; Control.Fetch_Stage()
 2 IS_OPEN    -> *(u8*)(a0+0x46) - 1                    ; Stages - 1
 3 IS_CLOSING -> *(u8*)(a0+0x46) - (*(u16*)(a0+0x30)+1) ; Stages - 1 - Fetch_Stage()
```

That is `door.cpp`'s `Door_Stage()` case for case. `TechnoClass` derives from
`DoorClass`, and the war factory opens with `Open_Door(2, 11)` (`Mission_Unload`),
so `Stages` is 10 and the returned stage runs **0..9**. WEAP's arm multiplies it by
`*0x80003E04 = 6.55556`, and **6.55556 is exactly 59/9** — so the cartridge maps the
ten door stages onto clip frames **0..59** and never touches 60..100. Independently,
the baked pack's per-frame node motion changes character at exactly frame 60.

### 9.3 The SAM's stage writers are `Mission_Attack`, DOS code verbatim

The `rate 2` and `stage := 48, rate 7` writers of §4 sit in an 8-arm jump table at
RAM `0x80003F38` indexed by `building+0x23`, which is **`Status`**. The eight arms
are `SAM_UNDERGROUND … SAM_LOWERING` and the code is `building.cpp`'s
`Mission_Attack` unchanged: `Set_Rate(2); Set_Stage(0)` to rise, threshold
`Fetch_Stage() == 15`; `Set_Rate(2); Set_Stage(48)` to lower, threshold
`Fetch_Stage() >= 63`.

That also **resolves §3.3's open SAM discrepancy**. Stage 0..15 gives frames 0..187.5
and stage 48..63 folds to 200..12.5, so the two run the same band in opposite
directions; tracking uses 200..400. The clip is **501 frames**, not 100 — the pack
carries `t = 0..80000`. The extractor was right and the note was reading the wrong row.

### 9.4 The consequence nobody had drawn: on the console these buildings do not animate

§6 reported `Anims[0..5] = {0,1,0}` for all 64 structure types and flagged it as a
disagreement with the brain. Re-dumped independently here — all 384 triples, same
result — and then searched for the thing that would fix it at runtime:

* DOS's `_anims[]` table (`bdata.cpp`, 30 records) is **NOT FOUND** in the ROM, as
  ints, as shorts, or as bytes.
* No code anywhere writes through `Class + 0x48 + BState*12`. The one candidate the
  scan turned up disassembles to a stack frame.
* The only readers of the class-pointer array at `0x801C3D44` are `As_Reference`.

So `Fetch_Rate()` is 0 for every building in every state, §9.1's incrementer never
fires, and **`Fetch_Stage()` is 0 forever** except where mission code writes it by
hand. Feed 0 through the arms of §3.3 and every one of `frame = stage`,
`stage * 10`, `stage * 2`, `stage * 2.5` and the NUKE fold yields **frame 0**.

The structures that genuinely move on the cartridge are exactly the ones whose arm
reads something that is not the stage counter: **WEAP** (door), **GUN** (turret
facing), **SAM** (facing, plus the stage `Mission_Attack` writes itself), **SILO**
(tiberium fill). Everything else — FACT, EYE, HQ, PYLE, HAND, AFLD, NUKE, NUK2,
ATWR, V19, HPAD, FIX, PROC — stands still on hardware, with its clip sitting unused
in the cartridge.

**This is a fact about the port, not a licence.** Reproducing it would delete almost
every structure animation in the game. What CNC3D does instead is stated in
the header comment of `structure_anim_frame()`: the arms
and the clips are the cartridge's, the *triggers* are the 1995 engine's (which our
brain still runs, `_anims[]` and all), and the counter that walks a clip is ours.

Still **NOT FOUND**: the console's animation clock source (§8's last open item), and
therefore the `ANIM_VIDEO_VS_TICK = 2` constant in the renderer remains a hypothesis.
