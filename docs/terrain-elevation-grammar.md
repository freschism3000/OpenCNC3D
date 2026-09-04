# The elevation grammar of the N64 maps

**Question (the project owner, 23 Aug 2026):** every map has a heightmap, and every heightmap seems to
use the same kinds of rise and fall, tied to particular tile types used in particular
ways. Why?

**Short answer.** Because the heightmap is not a terrain sculpt. It is a **3D reading of
the 2D map's own cliff-and-water art**, written on a five rung ladder whose rung spacing
is exactly one cell width. The 1995 DOS map format has no elevation field at all: the
only thing in it that says "this is high ground" is which TemplateType the designer
painted. So the N64 porters took those templates as the instruction and drew a heightmap
that agrees with them, one rung per plateau, the climb spread across the cliff template's
own 2x2 block, and the water tiles carved a little below their banks.

That is why every map looks like it uses the same vocabulary of rise and fall: they all
grew from the same tile set, read the same way.

It is **not** a generated file. See section 6: two scenarios ship byte-identical tile maps
and different heightmaps. It was authored, by hand, to follow the art.

Where the heights physically live (file, header, address, the vertex builder that reads
them) is `tools/romdump/heightmap_notes.md` and is not repeated here.

**Reproduce every number below with `python3 tools/romdump/height_rules.py`.** Each section
here corresponds to a section of that tool's output. Nothing in this document is asserted
that the tool does not compute from the cartridge.

---

## 0. The corpus

79 scenarios ship a `.MAP`. **55 ship their own `<SCEN>.IMG` heightmap**; the other 24 have
none and get `FLAT.IMG`, whose 4225 byte payload is all zero, so they are flat by
definition. All measurements below are on the 55, and inside each map's playable rectangle
(`[MAP] X/Y/Width/Height` in the `.INI`), because the border cells outside it are never
drawn and their heights are noise.

**Which 55 is not a coincidence, and the cartridge says so itself.** Its resident code
carries two scenario pointer arrays in the Compress overlay (RAM = ROM + 0x7FF14760), both
pointing into one string pool at ROM 0x020eb80:

* **ROM 0x21b260, the SPECIAL OPS roster.** Eight pointers:
  `SCG30EA, SCG90EA, SCB21EA, SCB22EB, SCG01EA, NULL, SCB01EA, NULL`. The menu dispatcher
  at ROM 0x21ced4 sends mode 1 to `arrA+0` for GDI and `arrA+8` for Nod with a row count of
  2 on both paths, and the screen init at ROM 0x21d660 stores the row bitmask 3 into both
  side masks. **Four missions: SCG30EA and SCG90EA for GDI, SCB21EA and SCB22EB for Nod.**
* **ROM 0x21b280, the REPLAY MISSION roster.** 26 GDI then 25 Nod campaign scenarios (51),
  then a tail of `DYNAMIC, SCG73EA, SCG74EA, STATIC, SCB70EA`. DYNAMIC and STATIC are row
  LABELS, not scenarios: the picker at ROM 0x21cf5c strcmps the selected row and
  substitutes SCG71EB and SCG72EA. Those five tail rows only appear when the debug flag at
  RAM 0x80138A30 is set, so a retail save never reaches them.

**51 campaign + 4 Special Ops = 55, and that set is exactly, name for name, the set that
ships a heightmap.** Checked as a set equality, not by counting. The rule is not "some maps
have elevation": it is **every mission the console can put you in has a hand-drawn
heightmap, and nothing else does.**

The four Special Ops missions are the cartridge's own, and it says so in English, not just
in pointers: `ENG/MISSION.ENG` (byte-identical to the ROM copy) carries two section headers
reading `;; N64 Special Ops`, one directly above `[SCG30EA]`/`[SCG90EA]` and one directly
above `[SCB21EA]`/`[SCB22EB]`. They are also the only non-campaign scenarios with a
`<SCEN>.IMG` heightmap, a `CM<scen>.IMG` tint map and a `.JIM` 3D scene. They were authored
in 3D for this console and for nothing else.

The remaining 24 cartridge maps are DOS Tiberian Dawn expansion content the cartridge
carries and wires to nothing. `expand.cpp` scans scenario numbers **20..59** for the Covert
Operations dialog and titles every row from that mission's own `[Basic] Name=`; **13** of
these carry one, and they are the Covert Operations set: Bad Neighborhood, Blackout, Hell's
Fury, Infiltrated!, Elemental Imperative, Ground Zero, Twist of Fate, Blindsided, The
Tiberium Strain, Cloak and Dagger, Hostile Takeover, Under Siege: C&C, Nod Death Squad.
`SCG32EA` sits in that range with no title, is byte-identical to `SCG73EA` in the debug
tail, and is named by no table on the cartridge, so it is counted as merely present rather
than as Covert Ops content. `expand.cpp`'s `Bonus_Dialog` scans **60..62** for the five
C&C95 Bonus Missions, and the debug tail at 70..79 accounts for the last five. None of the
24 was given a heightmap.

**There is no multiplayer scenario on the cartridge, and there are twenty-one maps it never
carried.** DOS multiplayer maps take the MULTI1 house prefix `M` and the Funpark
(dinosaur) missions take HOUSE_JP's `J` (`Set_Scenario_Name`, scenarioini.cpp), and
`data/dosdata/GENERAL.MIX` on the 1995 MS-DOS CD holds **16 SCM multiplayer maps**
(SCM01EA..SCM09EA, SCM70EA..SCM74EA, SCM77EA, SCM96EA, all named in their own INIs, from
Green Acres to Tiberium Garden) and **5 SCJ dinosaur missions** (SCJ01EA..SCJ05EA). Neither
set is on the cartridge and neither has, or could have, a heightmap: elevation is a
per-scenario N64 file and the DOS map format has no height field at all. They do render in
the viewer, because a DOS `.BIN` is 4096 `{u8 template, u8 icon}` records
(`MapClass::Read_Binary_File`, map.cpp:1076) and inverting a theater's TL4/TL8 turns each
pair back into an N64 tile-bank slot; measured over every DESERT and TEMPERATE map here,
every pair resolves. WINTER has no bank on the cartridge, so SCM70EA and SCM74EA are drawn
with the temperate one and the viewer says so.

---

## 1. There is a ladder, and its rung spacing is one cell

Two byte values carry over half the corners in the game:

| byte | corners | share | world Y |
|---:|---:|---:|---:|
| 64  | 71528 | 30.8% | 256 |
| 128 | 60037 | 25.8% | 512 |
| 191 |  8913 |  3.8% | 764 |

The full ladder is **0, 64, 128, 191, 255**: the quarter points of the 8 bit range
(255/4 = 63.75). It is worth being precise about which reading that is, because two
candidates fit the bottom of the ladder and only one fits the top:

```
third rung:   189:399   190:3433   191:8913   192:680   193:516
fourth rung:  252:27    253:1725   254:143    255:1802
```

191 beats 192 by thirteen to one. So the rungs are **quarters of 0..255**, not multiples of
64, which is exactly the picture a paint tool leaves when someone works in 0 / 25 / 50 /
75 / 100 percent greys. The heightmap was painted as an image, in an image tool.

Measured independently: take the flat land cells in each map, keep the height levels that
hold at least 1% of them, and look at the gap between consecutive levels.

```
  43 bytes  x  3
  44 bytes  x  2
  62 bytes  x  7
  63 bytes  x  9
  64 bytes  x 51   <-- and this is one cell width, in the engine's own units
  65 bytes  x  7
```

**74 of 90 gaps (82%) are one full step.** A terrace is one cell taller than the terrace
below it. The remaining gaps at 43 and 44 are a handful of maps that inset an extra
half terrace; they are the exception and they are visible in the viewer.

---

## 2. The relief sits on the cliff and water art, and nowhere else

145690 playable cells, classified by the PC TemplateType family of the tile drawn there.
"Steep" means the cell's four corners differ by at least 16 bytes, a quarter step.

| family | cells | % of map | flat | steep | % of steep | enrichment |
|---|---:|---:|---:|---:|---:|---:|
| SLOPE | 20633 | 14.2% | **3.6%** | 18101 | 52.0% | **3.67x** |
| CLEAR | 93090 | 63.9% | 65.3% | 7186 | 20.6% | 0.32x |
| RIVER | 7689 | 5.3% | **5.2%** | 4772 | 13.7% | 2.60x |
| SHORE | 10718 | 7.4% | 60.3% | 3075 | 8.8% | 1.20x |
| BRIDGE | 1377 | 1.0% | 30.9% | 436 | 1.3% | 1.32x |
| FORD | 714 | 0.5% | **4.9%** | 377 | 1.1% | 2.21x |
| ROAD | 5573 | 3.8% | 72.0% | 308 | 0.9% | 0.23x |
| FALLS | 321 | 0.2% | **1.2%** | 304 | 0.9% | **3.96x** |
| BRUSH | 1151 | 0.8% | 57.6% | 129 | 0.4% | 0.47x |
| PATCH | 2803 | 1.9% | 72.8% | 113 | 0.3% | 0.17x |
| BOULDER | 74 | 0.1% | 40.5% | 15 | 0.0% | 0.85x |
| WATER | 1547 | 1.1% | **96.3%** | 14 | 0.0% | 0.04x |

Read the flat column and the enrichment column together and the grammar is plain:

* **SLOPE is almost never flat (3.6%)**. A cliff tile that carried no height change would
  be a cliff drawn on level ground, and the porters essentially never did that.
* **FALLS is the steepest thing in the game (1.2% flat, 3.96x enriched).** A waterfall
  tile is a vertical drop, and it is treated as one.
* **WATER is almost always flat (96.3%)**, because open water is a plane.
* **PATCH, ROAD, CLEAR are the flat families (0.17x to 0.32x enriched).** Ground that is
  supposed to be ground is ground.

And when the height does change on a tile that is not cliff or water art, it is because
the cliff is next door:

```
for a steep cell, the nearest cliff/water TILE is:
  on the tile      27065   77.7%
  1 cell away       5613   16.1%
  2 cells away      1320    3.8%
  further off        832    2.4%
```

**93.8% of every steep cell in the game is on, or touching, a cliff or water tile.**

---

## 3. The 38 cliff templates are a compass, and the heightmap knows it

`TEMPLATE_SLOPE1` through `SLOPE38` (ids 13..50) are the DOS cliff set. Averaging the
height gradient across every instance of each template in every map gives one clean
direction per template:

| templates | high side |
|---|---|
| SLOPE1 .. SLOPE7 | **north** |
| SLOPE8 .. SLOPE14 | **east** |
| SLOPE15 .. SLOPE21 | **south** |
| SLOPE22 .. SLOPE28 | **west** |
| SLOPE29, 35, 38 | north-east |
| SLOPE30, 36, 37 | south-east |
| SLOPE31, 33 | south-west |
| SLOPE32, 34 | north-west |

Four straight edges of seven variants each, plus ten corner pieces. That is how Westwood
organised the 1995 cliff art, and **the N64 heightmap agrees with it on all 38 templates
without a single exception.** The mean rise per template runs 18 to 32 bytes per cell,
which is the next section.

**Asked per PLACEMENT rather than per cell, the same table answers 97.96% of the time.**
The averages above are taken over cells. The map editor's AUTO HEIGHTMAP works on whole
stamped templates instead, so `tools/romdump/cliff_compass.py` asks the question that
way: of the 4603 SLOPE placements sitting wholly inside their map's playable rectangle,
4509 have a mean gradient agreeing with the entry above, 18 point the other way and 76
are level (under 3 bytes per cell). Run it to reproduce those four numbers:

```
python3 tools/romdump/cliff_compass.py
```

The fit does not write those gradients into the corners directly. It reduces them to one
TIER per 2x2 block and writes the ground through `elev_coarse` and `elev_heights`, the
same path a brush stroke takes, so every corner it writes lands on a rung and the map it
hands back is on the ladder rather than off it.

---

## 4. A climb takes a template, not an edge

Walking every row and column of the 65x65 grid and measuring how many cells a one rung
climb takes:

```
  2 cells   3442   32.7%
  3 cells   3992   37.9%
  4 cells   1442   13.7%
  5 cells    622    5.9%
```

and the shape of the climb is close to linear, slightly eased:

```
  2 cells: [0.0, 0.5,  1.0]        x1348
  3 cells: [0.0, 0.25, 0.625, 1.0] x712
```

**70.6% of climbs take 2 or 3 cells.** Most SLOPE templates are 4 icons, that is a 2x2
block of art; a few (SLOPE2, 9, 13, 23, 27) are 6 icons. So the rise spans the cliff
template's own footprint. That is why the per cell gradient is only about a third of a
rung: the rung is delivered across the whole block, not at one cell edge. A one cell rise
over two or three cells of run is a face of roughly 18 to 27 degrees, which is the gentle
stepped-mesa look of the console game rather than a sheer wall.

---

## 5. The other direction: what gets carved down

Same measurement, inverted: how far below the surrounding land each water family tile
sits (bytes, negative is below).

| family | n | mean | median | p90 | % below |
|---|---:|---:|---:|---:|---:|
| WATER | 875 | -14.2 | -20.0 | 0.0 | 58.4% |
| FALLS | 320 | -17.6 | -14.1 | 11.0 | 75.3% |
| SHORE | 8918 | -15.8 | -14.0 | 0.0 | 80.8% |
| FORD | 714 | -13.5 | -12.0 | -4.5 | **96.2%** |
| RIVER | 7688 | -12.7 | -11.4 | -1.2 | 88.2% |
| BRIDGE | 1377 | -4.1 | **0.0** | 0.5 | 39.7% |

Two things worth naming:

* **Channels are shallow.** 11 to 20 bytes, a fifth to a third of a cell, not a whole rung.
  Rivers are grooves in the ground, not canyons; where a map does have a canyon (SCB01EA)
  the canyon is made of SLOPE tiers and the river is a groove in the floor of it.
* **BRIDGE sits at bank height** (median exactly 0). A bridge deck spans the groove
  instead of dipping into it, which is both correct and a sign somebody was paying
  attention rather than running a filter.

---

## 6. The control: this was authored, not generated

The obvious next theory is that a tool generated the heightmap from the tile map. It did
not, and the cartridge proves it by shipping the same tile map twice:

```
SCG05EA and SCG05WA have IDENTICAL 64x64 tile maps (both MD5 8e1402fa...),
and their heightmaps differ in 2259 of 4225 corners, max delta 191.

SCG13EA and SCG13EB have IDENTICAL tile maps,
and their heightmaps differ in 19 of 4225 corners, max delta 13.
```

Checked against the ROM directly, not against the extracted tree. `SCG05WA` takes the same
ground as `SCG05EA` and lifts a whole region from rung 1 to rung 4: 1702 corners that read
64 in one map read 253 in the other. Same map, different mountain.

The 19 corner difference between SCG13EA and EB is the other end of the same story: a copy
of a heightmap, touched by hand in one small place.

So the pipeline was: **a designer painted a greyscale image over the 2D map, following its
cliff and water art, snapping to a five value palette, and was free to depart from it.**
Two departures survive in the shipped data.

---

## 7. What this means for us

1. **Our bake can and should read `<SCEN>.IMG` directly** (already noted in
   `heightmap_notes.md`). There is no rule to reimplement and no rule that would be
   correct if we did: section 6 says a generator cannot reproduce the shipped files.
2. **If we ever author terrain of our own** (skirmish maps, a map editor), this document
   is the style guide: rungs 64 apart, climbs spread over the cliff template's block,
   channels 12 to 20 bytes deep, bridges at bank height, and cliff facing taken from the
   SLOPE template id.
3. **The 24 flat cartridge maps are flat on the console too**, and every one of them is
   expansion or debug content no retail menu reaches. If they ever look wrong in our build,
   the cartridge looks the same way, and inventing heights for them would be a departure
   that belongs in `known-gap notes` before it belongs in the renderer.
4. **`app/cnc3d.cpp`'s SPECIAL OPS list is not the cartridge's.** `specops_scan` offers 27
   missions (everything numbered 20..89 that has a pack); the console offers four. It also
   cannot ever offer SCG90EA, because its probe loop is `for (n = 20; n < 90; n++)` and so
   never forms that name, and `SCG90EA` is separately mislabelled in that file as "the test
   map" borrowing SCG01EA's pack. SCG90EA is a real N64 Special Ops mission with its own
   heightmap (range 62..255), its own tint map and its own briefing. Not this session's
   file to change, but it is worth a look. If those ever look wrong
   in our build, the cartridge looks the same way, and inventing heights for them would be
   a departure that belongs in `known-gap notes` before it belongs in the renderer.

---

## Appendix A: what this document got wrong on the first pass

The first version of this file called the five debug-tail scenarios (SCG71EB, SCG72EA,
SCG73EA, SCG74EA, SCB70EA) "the five behind the SPECIAL OPS menu", and folded SCG30EA,
SCG90EA, SCB21EA and SCB22EB into the campaign. Both were wrong, and the project owner caught it from
the shape of the data rather than the code: he said the Special Ops missions were
N64-exclusive and all had heightmaps, which is true of the four and false of the five. The
correction came from the menu dispatcher and, more plainly, from the cartridge's own
`;; N64 Special Ops` section headers in `ENG/MISSION.ENG`. It also said there were no
multiplayer maps, which is true only of the cartridge; there are 16 of them one archive
away, on the CD this project already reads for its audio and its sidebar art.

## Appendix B: a stale intermediate worth knowing about

`tools/bakery/sharecopy/assets/extracted` was written before `archive.py` learned to
decompress method-0x11 entries, and **15 of its 943 files are still-compressed blobs under
their plain names**. `n64_terrain.py` already knows about four of them
(`DESERT.PA8`, `TEMPERAT.PA4`, `DESERT.ND8`, `TEMPERAT.ND8`) and recovers those from the
ROM at load, so the tile atlases are correct. The rest, including `FLAT.IMG` (531 bytes in
the tree, 4241 in the ROM) and `CMFLAT.IMG` (1026 vs 8466), are not used by any current
tool. `height_rules.py` and the viewer's `export.py` both avoid the tree's `FLAT.IMG` and
emit 4225 zero bytes instead, which is what the ROM's copy contains. Re-running
`archive.py extract` would fix the tree; nothing today depends on it being fixed.
