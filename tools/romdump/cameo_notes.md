# The cartridge's sidebar cameos

Everything below is read out of `data/rom/cnc_eu.z64` by `tools/romdump/cameos.py` and
re-asserted by its `check` command (11 assertions, 0 failures). Nothing
here is inferred from the DOS release.

```
python3 tools/romdump/cameos.py data/rom/cnc_eu.z64 check
python3 tools/romdump/cameos.py data/rom/cnc_eu.z64 list
python3 tools/romdump/cameos.py data/rom/cnc_eu.z64 png   data/assets/cameos_n64
python3 tools/romdump/cameos.py data/rom/cnc_eu.z64 order data/assets/cameos_n64/sidebar_order.json
python3 tools/romdump/cameos.py data/rom/cnc_eu.z64 sheet /tmp/cameos_sheet.png 5
```

## 1. The count

**102 records, 101 distinct names, 91 distinct images.** `CAMMSAM2.IMG` is stored twice,
back to back at ROM `0x896D22` and `0x897252`, byte identical. That redundancy is the
cartridge's, not ours. The gap between 101 names and 91 images is section 4.

## 2. Two tables, two meanings

The trap: three of the ROM's directory tables list `.IMG` names with **offset, size and
method all zero**. `archive.py` already flags them as "different sub-format". They are not
damaged records and they hold no data. They are **name lists in sidebar slot order**.

| table | records | what it is |
|---|---|---|
| `0x019EE70` | 53 | sidebar order, GDI colourway (`CAM_*`) |
| `0x019F380` | 53 | sidebar order, Nod colourway (`CAM*2`) |
| `0x019F890` | **4** | the radar/logo plates, both sides, 96x77 (section 6) |
| `0x019F8F0` | **6** | the bottom plates, three per side, 96x35 |
| `0x0460540` | 1422 | the MAIN table. The actual art lives here, 102 CAM records |

**CORRECTED: `0x019F890` is TWO arrays, not one of ten.** Adjacency made them look like a
single table. They are separate C arrays and the sidebar's art loader is called with each
one and its own count: `(0x801FDAA0, 4)` at ROM `0x16C370` and `(0x801FDB00, 6)` at ROM
`0x16C380`. They also cannot be one atlas, because the cell sizes differ: the first four are
96x77 radar and logo plates, the last six are 96x35 bottom plates. Records 0..3 are
`GLOGOTP` `GRADARTP` `NLOGOTP` `NRADARTP` and records 4..9 are `GDI_BOT1..3` `NOD_BOT1..3`,
which is readable straight out of the ROM at 24 bytes a record. A third structure begins at
`0x019F980` and is not a name table at all: it is seven pointers, the localised READY and
HOLD plates in three languages plus `BRACKETS`.

The art records carry real offsets against data base `0x0468A90` and none of them are
compressed.

Which list is used comes from the selector at RAM `0x8002BB4C`, which reads **character 2
of the scenario name**: `'G'` (SCG01EA, the GDI campaign) picks `0x019EE70`, anything else
(`'B'`) picks `0x019F380`. That is the same switch that chooses the infantry colourway, so
the sidebar and the soldiers on the field cannot disagree about which side you are.

The two lists are positionally aligned: slot *i* is the same buildable in both. Slot 0 is
`CAM_BLNK` (empty well) and slot 52 is `CAM_SLCT` (selection frame) in BOTH lists. Slots
9, 10 and 41 all repeat `CAM_SILO`, so 53 slots resolve to **50 distinct pictures a side**.

`sidebar_order.json` is the machine-readable version, written by `cameos.py order`.

## 3. Format

Ordinary `.IMG` (see `img.py`). **32x25** for 100 of the 101; `CAM_RMBO.IMG` is **32x24**,
one row shorter in the cartridge itself, not a decode error. Nearly all are CI8 with a
trimmed RGBA5551 palette (117 to 256 entries, most files carry only the colours they use).
Three genuine exceptions:

| file | why it is different |
|---|---|
| `CAM_BLNK.IMG` | RGBA5551 direct colour, no palette at all |
| `CAM_SSM.IMG` | CI4, 16-entry palette, 448 bytes |
| `CAM_RMBO.IMG` | CI8 but 32x24 |

**Every cameo is fully opaque.** Not one transparent pixel in all 101. `CAM_SLCT` is a red
border around solid black, not a see-through overlay, so whatever the console does with it
is a composite the renderer performs, not alpha in the art.

## 4. The two sides are drawn twice, not recoloured

Of the 50 distinct slots, **41 are genuinely different art** between GDI and Nod. The
other 9 slots are the same picture for both sides:

- **shared name**, one file serving both lists: slots 0 `CAM_BLNK`, 30 `CAM_RMBO`,
  52 `CAM_SLCT`
- **byte identical under two names**: slots 1 `SBAG`, 2 `CYCL`, 3 `BRIK`, 28 `E5`,
  50 `ION`, 51 `ATOM`

For the other 41, the livery **and the background** are redrawn: the GDI set sits on green
grass with tan/gold vehicles, the Nod set on brown earth with grey/red ones. Zero pairs
decode to identical pixels under different palettes, which was tested for directly. A
renderer must therefore load the side's own file; there is no recolour shortcut, and the
"palette 0 is GDI, palette 1 is Nod" rule that governs the infantry does **not** carry
over to the cameos.

**Two pictures are also reused WITHIN a side**, which is why 101 names hold only 91
images. The eight byte-identical groups in full:

| group | kind |
|---|---|
| `CAM_BRIK` = `CAM_CONC` = `CAMBRIK2` = `CAMCONC2` | concrete has no art of its own; it points at the brick wall, on both sides |
| `CAM_MLRS` = `CAM_MSAM` | Rocket Launcher and Mobile SAM share one cameo (GDI set) |
| `CAMMLRS2` = `CAMMSAM2` | the same reuse in the Nod set, and `CAMMSAM2` is the record stored twice |
| `CAM_SBAG` = `CAMSBAG2` | cross-set |
| `CAM_CYCL` = `CAMCYCL2` | cross-set |
| `CAM_E5` = `CAME52` | cross-set |
| `CAM_ION` = `CAMION2` | cross-set |
| `CAM_ATOM` = `CAMATOM2` | cross-set |

The pattern is consistent: walls and superweapons are side-neutral, everything with a
chassis or a uniform is not.

## 5. Four orphans: art with no sidebar slot

Present as art, named by neither order list, so the shipped sidebar can never show them:

| file | subject |
|---|---|
| `CAM_FACT.IMG` | Construction Yard |
| `CAM_CONC.IMG` / `CAMCONC2.IMG` | concrete, both colourways |
| `CAM_SSM.IMG` | the SSM launcher (cut from the console release) |

`CAM_CONC` / `CAMCONC2` never had art of their own: both are byte identical to the brick
wall cameo of their side (section 4), so concrete was a name with a borrowed picture even
before it lost its sidebar slot. `CAM_FACT` and `CAM_SSM` are distinct pictures. All four
are extracted anyway and `cameos.py list` marks them ORPHAN.

## 6. Found on the way: the radar plates, including the Nod one

Table `0x019F890` is a third name-only list, and it resolves to real art in the main table:

| file | size | what it is |
|---|---|---|
| `GLOGOTP.IMG` | 96x77 | GDI eagle on a gold-framed plate (radar-off state) |
| `NLOGOTP.IMG` | 96x77 | Nod scorpion on a steel plate (radar-off state) |
| `GRADARTP.IMG` | 96x77 | the same gold plate with an empty black window (radar on) |
| `NRADARTP.IMG` | 96x77 | the same steel plate, empty window |
| `GDI_BOT1/2/3.IMG` | 96x35, 96x10, 96x11 | GDI lower sidebar plates, CI4 |
| `NOD_BOT1/2/3.IMG` | 96x35, 96x10, 96x11 | Nod equivalents |

This matters because a standing gap was recorded: "the Nod radar emblem at 136x120 does not
exist yet" as a standing gap. **The cartridge has one**, at 96x77. It is not a drop-in for
the 640x480 HUD, whose radar surface is 136x120, so it is a source to work from rather
than a finished asset. Extracted to `data/assets/sidebar_plates_n64/`.

## 7. What is NOT answered here

- **Where the cameo is drawn, and at what scale.** The console's sidebar geometry (slot
  rectangles, whether 32x25 is blitted 1:1 or stretched) was not looked for in this pass.
- **What `CAM_SLCT` is composited with.** It is opaque, so the draw order and any blend
  mode are in code that has not been read.
- **The 32x25 to 61x45 question.** The 640x480 HUD wants 61x45 cameo wells. 32x25 does not
  scale to that by an integer factor (1.9x / 1.8x). Upscaling cartridge art into those
  wells would be a visible compromise; this is a project decision, not a fact from the
  ROM, and it is recorded as a known gap.

  **ANSWERED: the cartridge cameos do not go into our HUD at all.** The project's own
  cameo art was redone on purpose and stands. The console set belongs to a separate
  optional feature recreating the console's whole sidebar, which is a 3D object rather
  than a 2D panel, so no fit against our wells is needed and the integer-factor problem
  does not arise. The scaling question above is therefore closed, not deferred.
