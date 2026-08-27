# The pack bakery

The complete, runnable harness that bakes the mission packs (`SCG01EA.pack` and
friends, magic `CNC3DPK8`). Rescued into the repo from a scratch directory
temp folder that macOS would eventually have cleaned; before that, this existed
nowhere durable.

## Run it

```
cd tools/bakery
python3 bake5.py SCG01EA SCB01EA        # -> tools/bakery/game/<scen>.pack
```

**The SCG10EA splice is obsolete.** `bake5.py SCG10EA` now bakes that map
directly and correctly, including its own heightmap (62..194) and its own water cells, so
`game/missions/bake_scg10ea_pack.py` is no longer part of the pipeline. It was a cell-table
splice from an SCG01EA donor that only recognised template W1, which is why it is being
retired rather than updated. Bake every scenario the same way:

```
python3 bake5.py SCB01EA SCG01EA SCG02EA SCG10EA
```

`bake5.py` here is a symlink to `game/bake5.py`, which is the ONE canonical copy.

## The PK4 baker cannot be run from this snapshot

`bake5.py` and `survey_parts.py` open with `import pk4mod`, and `pk4mod.py` exists only
to load and patch `bake_pk4.pyc` -- compiled bytecode whose source was lost. Neither the
loader nor the bytecode is published here, so both scripts stop at that import.

Everything else in `tools/romdump/` and the `game/bake_*.py` slices runs normally. What
is missing is the final PK4 pack step; reproducing it means reading the pack format out
of the loader in `game/` and writing a new baker, which is a real piece of work and is
not pretended otherwise.

## Inspecting a pack (`packinspect.py`)

`bake5.py` writes; `packinspect.py` reads back, so a bake can be PROVEN instead of
assumed. It parses every section the baker writes, including the PK8 tail.

```
python3 packinspect.py summary  game/SCG01EA.pack          # counts + PK8 tail check
python3 packinspect.py texdiff  OLD.pack NEW.pack          # which textures moved
python3 packinspect.py meshdiff OLD.pack NEW.pack          # which meshes moved
python3 packinspect.py texpng   game/SCG01EA.pack 24 /tmp/missile.png 24
```

`tail_ok()` is the hard check that a PK8 pack really carries its water indices; it
refuses a tail of -1, a duplicated index, or one out of the bank's range.

## Water (PK8)

The cartridge's rivers are the PRODUCT of two 32x32 RGBA5551 tiles, `WATER1.IMG`
x `WATER2.IMG`, at prim alpha 170/255, the two layers scrolling in opposite
senses. Both are baked into the ordinary texture bank and their bank indices are
appended as two `int32` at the very END of the file, which is why the magic could
go to `CNC3DPK8` without invalidating any PK5/6/7 reader: such a reader consumes
the whole file and simply stops 8 bytes early (proven).

**THE TAIL, and the one rule that governs it.** The current magic is `CNC3DPKC`,
version 12, and the file now ends with four blocks, first to last on disk:

| block | size | read at |
|---|---|---|
| PKC cm tint, 4225 u16 LE | 8450 B | `-(8450 + 4225 + 12)` from EOF |
| PK9 heights, 4225 u8 | 4225 B | `-(4225 + 12)` from EOF |
| PKA seabed bank index, i32 | 4 B | `-12` from EOF |
| PK8 water bank indices, 2 x i32 | 8 B | `-8` from EOF |

Every one of those is an **EOF-relative** seek in `cnc_eyes.cpp`, so **a new block
goes at the FRONT of the tail and never between two existing ones.** This is not a
style note. PKA inserted the seabed `int32` between the heights and the water tail
and nobody moved the heightmap reader, so for three formats the renderer drew every
terrain corner four columns to the east (1306 wrong corners on SCB01EA, worst 1.97
cells) and no gate objected. `check_pk8_tail` now reads BOTH per-corner blocks back
at those exact offsets and compares them byte for byte against what the baker was
handed, which is the check that would have caught it.

They were long believed undecodable, recorded as a known gap under "the
compressed IMG family". They are not a second format: they are ordinary 16-byte-header
.IMG files stored with archive method 0x2200, and `tools/romdump/imgsqueeze.py`
decodes 771 of 771 .IMG records. `bake5.py` loads that module by explicit path,
because it must bind to `tools/romdump/archive.py` and NOT to the older copy in
`sharecopy/tools/romdump/` (that one predates `unpack()` and the stored-size fix).

`WATERTST.IMG` is deliberately not baked: 16 of its 1024 texels differ from
WATER1 and the cartridge's own `gterrain.c` name table never references it.

## Two bakery defects fixed (why the rockets looked wrong)

**Texture stride.** `support/vx_rdp.py`'s `decode()` was being handed the tile's
TMEM `line` as the DRAM row stride (`bake_pk4.pyc decode_tex` computes
`line * 64 // bpp` and passes it whenever it is `>= w`). `line` is the TMEM
DESTINATION stride, rounded up to whole 64-bit words; the RDP reads DRAM rows at
the **G_SETTIMG image width**. For any tile narrower than one TMEM word the two
differ and the decode walks into unrelated texture memory. `decode()` now ignores
a stride that disagrees with `w` and warns once per tile.

Surgical, and measured: exactly **5 of 241** textures per pack have `uw < 8` --
indices 24, 59, 60, 96, 101 -- and exactly those five changed; the other 236 are
byte-identical. All five were checked against the cartridge's own commands: every
one has a `G_SETTIMG` image width equal to its tile width. Texture 24 (2x16 CI8,
ROM 0xF6C30, the DRAGON / MISSILE / BOMBLET body) went from a grey striped smear
to its real black nosecone, white body and one house-coloured band, and because
palette index 1 now appears in it the GDI-variant count rose 57 -> 58: the band is
gold `DEBD6B` through the GDI TLUT and red `841908` through the Nod one.

**Normals baked as vertex colours.** `dl_01051A0` (the missile body) never issues
`G_GEOMETRYMODE` anywhere in its call graph, so it inherits `G_LIGHTING` and its
vertex colour slot holds packed unit normals, not colours; all 36 of its vertices
match, and modulating the texture by them drew the rocket as near-black red,
green and blue faces. (`dl_0102848`, the 120MM box, does issue it -- twice -- and
has real colours.) `bake5.py flatten_lit_meshes()` bakes such a mesh white and
**asserts that exactly one mesh per pack trips it**, by name, so a future bake
that trips a second one stops rather than quietly repainting geometry. White
rather than lit is a deliberate simplification: recorded as a known gap.

## What is in here

- **`sharecopy/eyes/bake_pk4.pyc`** -- the PK4 baker, **bytecode only**: its source
  was lost, and this compiled form is the only
  surviving copy, which is exactly why it is committed despite being a build-shaped
  file. `pk4mod.py` loads it via `marshal` with its one hardcoded path constant
  swapped to this folder. Treat it as source. Do not delete it.
- `pk4mod.py` -- the loader/redirector for that bytecode.
- `objgraph2.py` -- scene-graph walker (both segments), the turret-round work.
- `support/` -- the baker's private inputs: `unit_models.json` (code-derived
  type->mesh table), `vx_rdp.py`, the TLUT/palette maps, and the `share/CNC3D`
  symlink the bytecode resolves through.
- `sharecopy/` -- the baker's world: `assets/` (extracted terrain atlases + jsons
  the bake reads), `tools/` (the romdump copies it imports), `eyes/` (the old eyes
  tree the pyc came from). `sharecopy/rom/cnc_eu.z64` is a symlink onto
  `data/rom/`, so the ROM exists once in the repo.
- `romdump_local/`, `facing_missions/`, `integration/` -- the turret round's
  private tool copies and proof missions, kept because the pyc imports some of
  them by relative path.

`tools/bakery/game/` is the output folder and is gitignored.

## House colours (PK6)

The cartridge keeps three resident 256-entry TLUTs and selects one per draw
(selector function at resident RAM `0x80055d08`, selector = HousesType):

| selector | ROM | look |
|---|---|---|
| 0 (HOUSE_GOOD) | `0x98F30` | GDI: entries 16..31 a sand ramp, gold accents |
| > 0 | `0x99130` | Nod/neutral: same entries blue-grey, red accents (the old bakes used this for everything) |
| `0x80000001` | `0x99330` | all-white damage flash |

`bake5.py` bakes every mesh texture through both house tables and stores a GDI
variant where they differ (57 of 165 textures); the renderer binds the variant
for GoodGuy objects. Proven against a photo of the real console: GDI vehicles
and building accents are sand/gold on hardware.

## Construction sections (PK7)

`bake5.py` also writes, per mesh, the table of its display list's G_VTX vertex
batches as ascending starting-triangle indices ("sections"). During construction
the renderer draws a growing prefix of sections, which reproduces the console's
piece-by-piece assembly (verified against video of the real cart). The section
walker is independent of the pyc baker and its per-part triangle totals are
asserted against the pyc's output on every bake, so the two walks cannot drift
apart silently.
