# CNC3D — N64 ROM Reverse-Engineering Log

Running log of what we actually know about the Command & Conquer (N64) ROM,
verified against the bytes. This is the **territory**; the charter is the map.
When this log and any summary disagree, this log wins (it's checked against the ROM).

---

## ROM identity (verified)

| Field | Value |
|---|---|
| Source | archive.org `roms-bestset-nintendo-64` (zip mislabeled "USA", actually EU) |
| File | `rom/cnc_eu.z64` (git-ignored — never committed) |
| Format | `.z64` big-endian native (magic `80 37 12 40`) |
| Internal name | `Command&Conquer` |
| Cart ID | `NCCP` — region `P` = **Europe / PAL (En,Fr)** |
| Size | 32 MB (33,554,432 bytes) |
| MD5 | `42da4c7d040f9e7cd046a42ec3e68027` |

We wanted USA (`NCCE`); we got EU (`NCCP`). EU is arguably the better RE target —
it bundles the Covert Operations missions, so there is *more* content, and the
formats are identical across regions.

## Coarse ROM map (verified by entropy survey)

- `0x0000000 – 0x0460000` — header, boot, MIPS code overlays, data tables, and
  **uncompressed text** (mission INI/BRF, filename pools, debug strings). Entropy ~4.5–6.
- `0x0470000 – 0x0800000` — first **high-entropy (compressed)** region. Assets.
- `0x0800000 – 0x0A50000` — mixed code/data/text.
- `0x0A50000 – 0x1640000` — large (~11 MB) **compressed** blob. Bulk of the 3D
  models / textures / audio almost certainly lives here.
- **Real data ends at `0x1646215`** (~22.4 MB). `0x1646216 – 0x2000000` = 9.7 MB
  of zero padding to fill the 32 MB cart.

Re-run any time: `python3 tools/romdump/survey.py rom/cnc_eu.z64 entropy`

## THE key discovery: the ROM is NOT stripped

Debug strings survived in the retail ROM. It carries **its own source-module names
and function names**, so the "undocumented" formats are self-documenting — we read
the game's own code to learn them instead of guessing.

### Format → owning module (from ROM strings)

| Format / subsystem | N64 source module | Evidence |
|---|---|---|
| In-ROM filesystem / archive | `../src/archive.c` | `ROM_Read()` |
| `.IMG` texture loader | `../src/n64image.c` | `N64Image_Load() cannot find file '%s'` @0x5700 |
| Image compression codec | `../src/n64wavelet.c` | **wavelet** compression + `N64SPR_InitImageSqueeze2()` ("squeeze") |
| 3D terrain (height-map) | `../src/gterrain.c` | — |
| Infantry sprites (2D) | `../src/n64soldier.c`, `n64soldier_cache.c`, `n64spr.cpp` | S2DEX sprite microcode |
| Sprite/2D microcode | `../src/s2dex.c`, `be_s2dex.c` | Nintendo S2DEX |
| Draw commands / display lists | `../src/n64drawcmd.c`, `f3.c` | F3D-family |
| Core engine | `../src/n64engine.cpp` | — |
| Movie / briefing render | `../src/n64movie_render.c`, `briefing_engine.c`, `n64intro.c` | — |
| Particles / FX | `../src/particle.c`, `n64particle.c`, `n64specialeffect.c` | — |
| **Ported from PC** | `../src/conquer.cpp`, `scenario.cpp`, `ccfont.cpp`, `saveload.cpp` | Westwood filenames |
| Object model / scenario | `objnode.c`, `ex_objects.c`, `be_objects.c`, `ioobj.cpp` | — |
| Scripting VM (?) | `be_api.c`, `be_auto_api.c`, `be_parser.c`, `be_executor.c` | "BE" bytecode exec |
| Memory / pointer bank | `mem.c`, `memory.cpp`, `ptrbank.c` | `Mem_Dyn*`, `800xxxxx` ptr table @0x5180 |
| Audio | `soundlib.c`, `trackctrl.c` | `m01music.bin`…`nomercy.bin` track names @0x5780 |
| Save to cart | `n64flashrom.cpp` | `osFlashWriteBuffer()` |

Full lists: `python3 tools/romdump/survey.py rom/cnc_eu.z64 modules`

### Data footholds located

- **Mission INI, uncompressed, PC-compatible format**: `[BASIC]`, `[SMUDGE]`,
  `[START]`, and a real mission section `[SCG01EA]` (Single/GDI/01) around `0x0696000`.
  Confirms the research: N64 kept the PC `.INI`/`.BRF` text format verbatim.
- **Filename pools**: fonts (`BRFFONT.IMG`, `MENUFONT.IMG`) and `flat.img` @~`0x5250`;
  palette files `DESERT.PA4/PA8/DA4/DA8/ND4/ND8`, `TEMPERAT.*` @~`0x5280`;
  music/sfx `.bin` (`m01music.bin`…`m06sfx.bin`, `nomercy.bin`, `war.bin`…) @~`0x5780`.
- **Terrain-texture directory** @~`0x16A76C`: `water1.img`, `bottom.img`,
  `any_ti01..06.img` (any-theater tiles), `tem_bib*.img` (temperate bibs).
- Runtime filename format strings: `SC%c%02d%c%c.INI`, `%s.MAP`, `%s.img`.

## M1 — Archive / filesystem CRACKED (verified)

The ROM's `archive.c` filesystem is **11 directory tables** of fixed **24-byte records**:

```c
struct Entry {            // big-endian
    char name[12];        // null-padded; exactly-12 => no terminator
    u32  offset;          // relative to the archive-group DATA BASE
    u32  uncomp_size;     // decompressed size
    u32  method_and_comp; // (method << 16) | compressed_size
};
```

- **Tiling by compressed size:** `off[i] + comp[i] == off[i+1]` (verified across files).
- **Method** = high 16 bits of the 3rd word: `0x0000` stored (1890 files),
  `0x2200` LZ-compressed (3482), `0x1100` a third method (78), a few odd small ones.
- **DATA BASE for archives 1–9 = `0x468A90`** (VERIFIED, see M2: 3454/3454 files
  decompress to their exact recorded size at this base — a base that is even one byte
  wrong fails every length check). An earlier guess of `0x470000` was **wrong**; it put
  the decoder mid-stream in a neighbouring file, which still produced plausible-looking
  text and briefly fooled the eye. Length-exactness is the only trustworthy oracle here.
  The two trailing archives (`0x0A4D778`, `0x0D45008`) use a **different base — TODO**.
- The 3 tiny IMG dirs (`0x019EE70/0x019F380/0x019F890`) have all-zero offsets — a
  different sub-format; revisit.

**Directory locations — SIX tables** (a first scan reported eleven; six of those were
overlapping sub-ranges of the main table and inflated every count ~3.4×):

| table | recs | note |
|---|--:|---|
| `0x019EE70` | 53 | IMG, all-zero offsets — different sub-format, revisit |
| `0x019F380` | 53 | IMG, ditto |
| `0x019F890` | 10 | IMG, ditto |
| **`0x0460540`** | **1422** | **MAIN table — ends at exactly `0x0468A90` = the data base** |
| `0x0A4D778` | 43 | trailing archive (base unknown) |
| `0x0D45008` | 43 | trailing archive, duplicate of the above |

The main table ending exactly at `0x0468A90` is an independent confirmation of the data
base found in M2: **the directory is immediately followed by its data.**

**Full contents = 1,624 records / 1,615 unique** (`docs/rom-manifest.csv`, regenerate
with `python3 tools/romdump/archive.py rom/cnc_eu.z64 manifest docs/rom-manifest.csv`):

| ext | n | | ext | n | | ext | n |
|---|--:|---|---|--:|---|---|--:|
| IMG (textures) | 889 | | BRF (briefings) | 53 | | TEM | 12 |
| WLT (wavelet) | 337 | | MRT (sfx) | 46 | | ENG/FRE/GER (text) | 16 |
| **INI (missions)** | **81** | | BIN (audio) | 38 | | palettes PA/DA/ND/TL 4&8 | 16 |
| **MAP (terrain)** | **80** | | | | | | |
| **JIM (3D models?)** | **56** | | | | | | |

By method: `0x0000` stored 755 · `0x2200` hash-LZ 845 · `0x1100` 19 · misc 5.

Tools: `tools/romdump/archive.py {list|manifest|extract}`. Raw extraction dumps each
file's stored bytes (compressed ones stay compressed pending M2).

## M2 — the main codec (method `0x22`) CRACKED (verified 3454/3454)

Reversed from the MIPS at RAM `0x80052570` (ROM `0x53170`). Implementation:
`tools/romdump/decomp.py`. **It is NOT Westwood LCW** — the literal runs merely made it
look like one. It is a **hash-indexed LZ77**: a match does not encode a distance, it
encodes a **12-bit hash-table slot**, and encoder + decoder maintain byte-identical
4096-entry tables keyed on a 3-byte rolling hash (8-way buckets, 3-bit rotating way).

```
u8[4] header                     ; src[0]==1 diverts to the alt codec at 0x80058070
repeat:
  u16le flags loaded as (flags | 0x10000)   ; 17th bit = "exhausted, reload" sentinel
  bit 0 -> LITERAL : copy 1 byte
  bit 1 -> MATCH   : u8 b1,b2; slot = ((b1&0xF0)<<4)|b2 ; len = 3 + (b1&0x0F)
                     copy len bytes from H[slot] (byte-wise, overlap allowed)
hash: h(p) = ((40543 * ((d[p]<<8) ^ (d[p+1]<<4) ^ d[p+2])) >> 1) & 0xFF8
insert:      H[h + rot] = p ; rot = (rot+1) & 7
```
Bookkeeping that MUST mirror the encoder or the tables desync (this was the hard part):
every 3rd consecutive literal inserts at `pos-3` and then holds the pending count at 2;
a match first flushes 1–2 pending-literal inserts, then inserts itself at
`H[(slot & 0xFF8) + rot] = match_start`. 16 flag bits are consumed per pass, but only
**one** once the input pointer passes `end - 0x20` (drawing from the same register
without reloading). All 4096 slots start pointing at a 19-byte filler string in rodata
(RAM `0x80004EAC` = ROM `0x5AAC`, `"1234567890123456\0 78"`) — so a match against a
never-populated slot yields visible `123456…` filler. **That filler is the tell-tale of
a desynced table or a wrong base.**

**Result: every method-`0x22` file in the main archive decompresses to its exact
recorded size — 817 unique files, 0 failures.** (An earlier "3454/3454" figure counted
the same records several times over, via the overlapping-directory bug above; the
validation itself was sound, the denominator was not.) The only method-`0x22` failures
anywhere are the 28 `.BRF` in the two trailing archives, whose base is still unknown.
Missions come out as byte-perfect PC-format C&C `.INI` (`[Triggers] [MAP] [Waypoints]
[CellTriggers] [UNITS] [INFANTRY]`, original `; Scenario 1 control for house BadGuy.`
comments intact). `.BRF` files are the **briefing scripts with the original developers'
comments** (e.g. `// GDI Scenario 1 Briefing - John Spence`). `.MAP` files are exactly
**8192 bytes = 64×64 × u16BE**, clear terrain `FFFF` — matching the ModdingWiki spec.

Extractor: `tools/romdump/extract.py rom/cnc_eu.z64 <outdir> [--only INI,MAP,BRF]`.
The only failures are 28 `.BRF` in the two trailing archives (unknown base, above)
and 2 `.MAP` using method `0x11`.

## M3 — image formats decoded; `.JIM` identified (and it is NOT models)

### `.JIM` = pre-rendered **Japanese** briefing text — not a model container
I predicted `.JIM` (56 files) was the 3D-model format. **That was wrong.** They are
1-bit text bitmaps, one per mission (`SCB01EA.JIM` pairs with `SCB01EA.INI`), holding the
briefing rendered as kanji artwork — "JIM" reads as *Japanese IMage*. The N64 can't
cheaply rasterise kanji from a font, so the text was pre-rendered.

Verified by decoding and reading them: `SCB01EA` is the Nod 1 briefing (orders to build a
base, and to kill Nikoomba, the nearby village leader); `SCG01EA` is the GDI 1 MCV
briefing. **This is a preservation find in its own right:** the Europe (En,Fr) cart carries
a complete Japanese localisation that the PAL build never exposes.

Layout (`tools/romdump/jim.py`, 55/55 consistent): the shared 16-byte image header,
8bpp indices using only 0/255, plus a 512-byte palette that is uniformly `0x0001`.

### `.IMG` decoded (`tools/romdump/img.py`) — 756 / 770 files (98%)
Same 16-byte header as `.JIM`. Bits-per-pixel is not stated outright; solve it from the
file size, where exactly one combination fits:

```c
struct ImgHeader {      // big-endian
    u32 header_size;    // 0x10
    u32 body_size;      // 0x10 + packed pixel bytes (sometimes 0)
    u16 width, height;
    u8  fmt;            // 0x00 = RGBA5551, 0x02 = colour-indexed, 0x04 = intensity
    u8  depth_code;     // 0 = 4bpp, 1 = 8bpp, 2 = 16bpp
    u8  unk;
    u8  pal_count;      // palette entries; 0 means 256
};
<packed pixels>  <u16 palette[pal_count]>   // RGBA5551 palette at END of file
```
Format spread: 310 × RGBA5551 16bpp · 134 × CI4 · 157 × CI8 (palettes of 256/244/19/16/13/10).

**Recovered art, visually confirmed:** `CYARD1` is the Construction Yard with `CYARD5/9/15`
its destruction sequence (fireball → flash → smoke); `CAM_*` (53) are the sidebar cameos;
`TEM_/DES_/ANY_*` are per-theater terrain tiles and bibs; `E1_*`…`E6_*` are **infantry
sprite animation strips** (`STAND/RUN/SHOOT/CRAWL/DETH1/DETH2/FDETH/IDLE/CSHOT`) — direct
confirmation of the research claim that N64 infantry stayed 2D.

### `.TEM` (12 files) = terrain templates
`CLEAR1.TEM`, `CLEAR1_A…K`. Header reads **little-endian** (unlike everything else):
`w=24, h=24, 16, size, 32`. 24×24 is the classic C&C cell size; ~16 tiles per file.

### Where the 3D actually is — partial
Scanning the ROM for RSP command streams found **756 F3DEX2 display lists**, clustered in
ROM `0x100000–0x500000` (the overlay region — neither resident code nor archive data).
These are genuine geometry, confirmed by decoding: `G_VTX` loading 30/32/7 vertices,
coherent `G_TRI1` quad splits `(0,1,2),(2,3,0)`, and a proper state tail (`G_GEOMETRYMODE`,
`G_SETCOMBINE`, `G_SETPRIMCOLOR`, `G_ENDDL`). Vertex-buffer addresses differ by exactly
`n × 16`, confirming the standard 16-byte `Vtx` struct.

**But the meshes are not static arrays at those addresses.** The referenced pointers
(e.g. `0x80126e60`) sit *past the end of BSS* (`0x80122b60`) — i.e. in heap — so the
vertex pointers are **patched at runtime**. A strict search for matching static `Vtx`
arrays anywhere in ROM `0x20000–0x460000` returned zero hits. So the geometry is either
built at runtime (very likely for the height-map terrain) or loaded from a source not yet
identified. **This is the open question for M4.**

## M4 — SOLVED. The overlay table is the key to all 3D geometry.

**`tools/romdump/segments.py` holds the game's real overlay table.** Recovered by
disassembling the loader at RAM `0x800559B4` (ROM `0x565B4`), whose own debug strings state
the semantics outright: `"Loading Overlay #%d, Number of Segments = %d"` and
`"Loading Segment #%d, Name = %s, ROM Size = %u, RAM Size = %u"`.

```
+0x00 char* name      +0x10 u32 ramEnd        loader inner loop (proves the layout):
+0x04 u32   romStart  +0x14 u32 bssStart        lw $a1,4($s0)   ; romStart
+0x08 u32   romEnd    +0x18 u32 bssEnd          lw $a2,8($s0)   ; romEnd
+0x0C u32   ramStart  +0x1C void(*init)()       lw $a0,0xc($s0) ; ramStart
                                                jal 0x80077958  ; dma(dst,src,len)
20 descriptors, 32 bytes each, in resident      subu $a2,$a2,$a1; len (delay slot)
.data at ROM 0x090DB4 / 0x09101C / 0x0911E8
```

### Why every earlier search missed it
`romStart`/`romEnd` are stored as **PI cart-physical addresses `0xB0000000 | romOffset`**,
not bare ROM offsets. Scanning for raw `{romStart, romEnd, ramStart}` triplets can never
match. Mask with `0x0FFFFFFF`. (Also: this game does **not** use N64 segment addressing —
zero `G_SEGMENT` encodings anywhere, no opcode `0xDB` in any display list; every pointer is
plain KSEG0.)

### The mapping
`rom = ram − delta`, `delta = ramStart − romStart`. Key overlays:

| segment | ROM | RAM | delta |
|---|---|---|---|
| ScriptModels | `0C6EA0–0DA420` | `80122B60` | `8005BCC0` |
| GameModelsTextures | `0DA420–0FE210` | `801369B0` | `8005C590` |
| **GameModelsGeometry** | `0FE210–1643F0` | `8015A7A0` | `8005C590` |
| BriefingCode | `1B67D0–20E400` | `801C2600` | `8000BE30` |
| IntroSequence | `250BF0–266700` | `80122B60` | `7FED1F70` |
| Movie1…Movie6_1 | `28CCA0–4578A0` | `8017C100` | (varies) |

**Validation: 887/887 well-formed `G_VTX` resolve (100%).** The 30 rejects have non-KSEG0
operands (`0xFFFFFF00`, `0x00008100`) and are false-positive display lists parsed out of
texture data. Independent confirmation: `IntroSequence`'s delta `0x7FED1F70` is *exactly*
the delta derived by hand for the known-good C&C logo mesh.

### Result
**202 meshes exported to `assets/models/`** (+ `_index.json` with per-mesh metrics, segment
and delta). Visually verified: the C&C logo, the **Nintendo 64 logo**, the "EVA" wordmark,
the GDI eagle emblem, buildings with sloped roofs, faceted boulders, a wrench/repair icon,
landing-pad decals, hexagonal wall rings, directional markers. **Zero shard clouds.**
Browser viewer: `assets/model-viewer.html` (self-contained WebGL, no libraries).

Several overlays are **co-resident** (a movie overlay has ScriptModels /
GameModelsTextures / MovieTextures / MovieN loaded at once), so a display list may point
outside its own segment; within a group the RAM ranges are disjoint, so
`segments.make_resolver()` stays unambiguous.

### Method note worth keeping
The statistical approach that preceded this got c0's delta right (`0x8005C590`) but was
refuted for the other clusters — because the "clusters" (grouped by ROM proximity) straddle
overlay boundaries. **Read the loader; don't infer the mapping.** The decisive geometric
test, if you ever need one again, is **edge pairing**: in a correct mesh nearly every edge
is used by exactly 2 triangles (ground-truth logo = 1.000); a delta off by one vertex
collapses it to ~0.6, and a random wrong delta to ~0.4.

---

## (historical) M4 first attempt — DID NOT WORK (1 verified model)

**Read this before touching the model tools.** An earlier version of this section claimed
"158 solid meshes... globe, emblems, aircraft". **That claim was wrong.** Rendered
properly, essentially all of them are shattered triangle soup. Exactly **one** model is
verifiably correct: the extruded **"COMMAND & CONQUER" logo** (`dl_025F270`, 1495 verts /
1024 tris) — it renders pixel-crisp head-on and is genuinely right. Everything else in
`assets/models/` should be treated as **mis-extracted until proven otherwise**.

Two lessons about *evaluating* this work, not just doing it:
- **Look at the output before reporting it.** The first renders used an oblique camera,
  fake area-based shading and a painter's algorithm, which made a *correct* mesh (the
  logo) look broken and made *broken* meshes look plausibly like low-poly models.
- **The "quality score" in the viewer is not trustworthy.** `dl_0264D40` scores 100% and
  is scattered junk: small triangles inside a large bbox pass the test regardless of
  whether they connect into a surface. Don't use it as evidence.

### Why it fails
The pipeline itself is sound (it produced the logo). The failure is **locating each
display list's vertex block**. DLs reference vertices by RAM address; the overlays are not
resident; so each needs a delta `D = RAM − ROM`, and I have been *guessing* D by sliding
the DL's relative G_VTX layout over ROM and taking the first position where the arrays
parse as plausible `Vtx`. Many ROM regions pass that test, so the search locks onto a
wrong block and the triangles then index scrambled vertices — precisely the shard-cloud
appearance.

### What has been ruled out (don't redo these)
- **Static overlay/segment table**: none. Scanned for `{romStart, romEnd, ramStart}`
  records, and searched the ROM for the cluster start addresses as u32 — only archive
  directory records turn up. The runtime descriptor array at `0x800EB760` is BSS.
- **One delta per overlay**: false as clustered. Grouping the 379 DLs by ROM proximity
  gives 5 clusters; a joint solve explains 4 of 5 barely at all (best 1/289, 4/214,
  1/142, 6/244). Only the small cluster `0x038B9F0–0x03992D0` solved cleanly
  (D=`0x7FE4EBE0`, 27/28) — so *some* bundles do share a delta.
- **Models stored as archive files**: no. Scanned all 1,427 extracted files for display
  lists — **zero hits**. All geometry lives in raw ROM overlays (and the DL region
  `0xCB8C0–0x4578A0` sits below the archive base `0x468A90` anyway).
- **G_DL branch Hough**: unavailable — there are 0 `G_DL` commands in the real DLs.

### The one correct path left
Stop inferring the mapping from data and **read the loader**: find the code that DMAs
these overlay regions (start from the DMA helper at RAM `0x80077958`, already known from
`archive.c`, and from `objnode.c` / `n64engine.cpp`), and recover each overlay's ROM
start + RAM destination directly. With true deltas every DL resolves exactly, and the 352
`G_SETTIMG` addresses also resolve — which is what would finally give **textures**
(currently nothing is textured; the exporter writes UVs but no materials).

### How the mapping is recovered
Display lists reference vertices by **RAM** address, and the overlays are not in the
resident segment, so each bundle needs its own delta `D = RAM − ROM`. There is no static
overlay table (searched; the runtime descriptor array at `0x800EB760` is BSS). Instead:
every `G_VTX` in a list points into one contiguous block, so the *relative* spacing of the
arrays is known even when the base is not — slide that pattern over ROM and accept the
position where all arrays parse as valid `Vtx`. **Constrain the search to ±0x40000 around
the DL's own ROM offset** (measured blocks sit within ~0x11000 of their list).

```
G_VTX (0x01): w0 = 01 | numv<<12 | end<<1, w1 = RAM addr; loads into slots [end-numv,end)
G_TRI1(0x05): w0 = 05 aa bb cc                     -> tri (aa/2, bb/2, cc/2)
G_TRI2(0x06): w0 = 06 aa bb cc, w1 = 00 dd ee ff   -> two tris
Vtx (16B, BE): s16 x,y,z ; u16 flag(=0) ; s16 u,v ; u8 r,g,b,a
```
The RSP vertex buffer is nominally 32 slots but some lists here index higher — use 64
and bounds-check, or the walker throws.

### Two traps that cost real time (both now fixed in the tools)
1. **`G_NOOP` (0x00) must NOT be in the opcode set.** With it, every zero-filled region
   parses as an enormous display list — one false positive claimed 20,250 vertices, and
   the "756 DLs / 428 G_DL branches" from the first pass were mostly noise. Requiring
   `>=1 G_VTX` and `>=3 tri` commands gives the true count: **379 display lists,
   917 G_VTX, 352 G_SETTIMG, and 0 G_DL** (so DL-branch Hough is not available here).
2. **Do not reuse one bundle's delta for other display lists.** It finds *a* plausible
   vertex block rather than *the* right one and silently yields garbled meshes. Solve
   each DL in its own local window.

### Coverage, honestly
233/379 lists solved; after discarding degenerate results (a flat axis, or <12 tris)
**158 are solid**, 75 are flat/thin and probably still mis-localised. Model→unit naming is
not yet established — nothing ties a mesh to `MTNK`/`JEEP`/`NUKE` yet.

**Next step for full coverage:** stop pattern-matching and recover the real overlay
mapping — find the loader that DMAs each overlay (its ROM start/end and RAM destination).
With true per-overlay deltas every DL resolves exactly, the 146 unsolved lists come in,
and `G_SETTIMG` addresses (352 of them) can be resolved to real textures for UVs.

## M5 — TEXTURES WORKING (164/202 models)

`tools/romdump/textures.py` walks each display list's RDP tile state and decodes the bound
texture. **164 of 202 models are textured.** Output: `assets/textured/` (OBJ + MTL + PNG
per model) and the textured WebGL viewer.

Coverage is principled, not maximised: 149 models bind their own texture, 20 more carry
real UVs but bind nothing themselves (they inherit via state carry-over, below), and 33
have neither UVs nor a binding and are **genuinely untextured** — including the C&C logo,
which is prim-coloured geometry with all-zero UVs and no texture command anywhere in its
display list. An intermediate build textured 189 by applying carry-over unconditionally;
that painted meshes with zero UVs a single arbitrary texel. Gating on "does this mesh
actually have texture coordinates" is the correct rule.

### Three things that had to be right
1. **The resolver shadowing bug (mine).** Segments share RAM *slots* — five of them, e.g.
   `GameModelsGeometry` and `MovieTextures` both load at `0x8015A7A0`. The first
   `make_resolver` walked the segment list in declaration order, so GameModelsGeometry
   permanently shadowed MovieTextures and **all 70 movie texture pointers resolved into
   display-list opcode bytes** and decoded as coloured noise. Geometry was unaffected
   (0/887 G_VTX changed) — a texture-only bug. Fixed by resolving within the display
   list's own **overlay group**; the groups are parsed from the ROM's three overlay
   managers (`segments.parse_overlays()`, verified to match the baked-in table exactly).
2. **CI textures need the global palette.** Most display lists never issue `G_LOADTLUT` —
   the palette is loaded once at startup and left in TMEM. Fall back to the shared
   256-entry RGBA5551 palette at ROM **`0x99130`** (byte-identical to the palettes the ROM
   genuinely does load at `0x028D470` / `0x032DB90` / `0x04022F0`). This took decodable
   textures from 70 → 149 models.
3. **RDP tile state persists ACROSS display lists.** A per-DL walk that starts blank
   leaves every list that doesn't set its own texture untextured. Walking all lists in ROM
   order per segment and carrying state forward took 145 → **189**.

### The UV rule — right number, and now the right reason
`texel = u / 32` (plain S10.5). But **not** because "scale ≈ 1.0": every tile in this ROM
has `shiftS=shiftT=15` (RDP: coord<<1, ×2) and every `G_TEXTURE` is `scaleS=scaleT=0x8000`
(×0.5). They cancel exactly. Confirmed two ways: visually on a vehicle wheel (`/32` = one
centred tire, `/64` = a cropped quadrant, `/16` = a 2×2 grid of tires), and statistically —
**137 models use exactly one texture with a median UV span of exactly 1.00**, i.e. a perfect
single-tile mapping.

Also verified: texture data is **plain linear row-major** in ROM. The N64's odd-row 64-bit
interleave is a TMEM-side concern and must NOT be undone (checked by A/B decode).

Formats present: CI8+TLUT (most common), RGBA5551, I8, CI4, IA. Pool strides corroborate
the tile sizes independently of the state machine: `0x800`=32×32 RGBA5551,
`0x400`=32×32 8-bit, `0x100`=16×16 8-bit, `0x200`=256-entry TLUT.

Recovered art includes hazard chevrons, Tiberium crystal, tank tread, brick, foliage,
fire/explosion sheets, and the **Nod scorpion insignia** (the most-referenced texture in the
ROM, 18 uses).

### Two bugs the adversarial verifiers caught in shipped code
- **`model.py write_obj` emitted `vt u/1024.0`**, which silently hardcodes a 32×32 tile
  (32 texels × 32). Correct is `(u/32)/tile_w`. Right for the 110 32×32 groups, wrong for
  every other size — 16×16 came out at 0.5, 2×2 at 0.0625, 64×16 at 2.0. Fixed; the
  function now takes the real tile size and leaves UVs in raw texels when it isn't given.
- **The C&C logo cannot validate UVs** — it has no texture at all. Using it as the UV
  ground truth (as an earlier plan did) proves nothing; it only validates geometry.

**Still open:** 38 models untextured (33 legitimately, 5 whose texture failed to decode);
tile-size derivation is imperfect for a tail of models — median UV span is 0.998, but only
74/164 land within ±0.05 of 1.0 (some of that is legitimate texture repeat, some is a
wrong tile size); 12 models use 2–3 textures and render with the dominant one only
(per-material groups are written into the OBJ/MTL but the viewer binds one per model).

## Open unknowns (ranked) — updated after M3

1. **Static mesh source for vehicles/buildings** — where the runtime fills those vertex
   buffers from. Next step: find the code that writes `0x80126e60`-range buffers
   (`objnode.c`, `n64engine.cpp`, `gterrain.c`) rather than searching for data.
2. **Wavelet codec** (`n64wavelet.c`) for `.WLT` (337 files) — briefing/UI stills.
3. ~~Method `0x11`~~ **SOLVED.** It is the *same* hash-indexed LZ77 as method `0x22`,
   differing in one place: a **flat 4096-entry hash table** addressed by a full 12-bit hash
   (`slot = ((40543*x) >> 4) & 0xFFF`, no rotating way), where `0x22` uses 512 buckets × 8
   ways with a 3-bit rotating counter. All 19 files decode to exactly their recorded size;
   both `.MAP` oracles produce exactly 8192 bytes. Decoder: `tools/romdump/decomp11.py`.
4. ~~Trailing archives' data base~~ **SOLVED.** `dir@0x0A4D778 → base 0x00A4DB80` and
   `dir@0x0D45008 → base 0x00D45410`. The rule is uniform: **base = dir_start + 24×nrecs**
   (the byte right after the last directory record) — the same rule gives the main table's
   `0x0460540 + 24×1422 = 0x468A90`. All 28 method-`0x22` `.BRF` files decompress to their
   exact size. The two archives are the **English and French** builds of one 43-file
   briefing/audio set.
5. **Audio** (`.BIN`/`.MRT`, `soundlib.c`, `trackctrl.c`).
6. The 14 unsolved `.IMG` and the 3 zero-offset IMG directories.

### Tooling bug to fix
`tools/romdump/archive.py` still carries `GROUP_BASE = 0x470000`, the early wrong guess.
The correct value is **`0x468A90`** (as already used in `extract.py`).

### Method notes that cost time (read before the next format)
- **Do not trust "looks like readable text" as proof of a correct base.** Files tile
  contiguously, so a wrong base lands mid-stream in a *neighbouring file of the same
  type* and still emits convincing fragments. Use an **exact-length oracle**.
- Output length depends only on the control stream, never on the hash table — so
  length-exactness isolates "is the base/parse right" from "is the table right."
- Runtime tables (e.g. the archive descriptor array at RAM `0x800EB760`) live in **BSS**
  and are zero in the ROM image; don't expect static values there.

## Method notes

- All work so far is **read-only**. The ROM is never modified.
- Endianness: we hold the native `.z64` (big-endian). `.MAP` tile IDs are `UINT16BE`.
- To crack the compression/model formats, the fast path is a small MIPS disassembly
  of the named functions (`ROM_Read`, `N64Image_Load`, wavelet decode) rather than
  blind format-guessing — the code is right there in the ROM.

## M8 addendum — three asset defects, closed

Three defects came out of the renderer verification pass. Two were real bugs in our
extraction and are fixed; the third turned out not to be a defect at all.

### 1. Infantry sprites had no palette. SOLVED, the ROM holds all 206 of them.

The 67 infantry strips are CI4 and their `.IMG` header declares `pal_count = 16`, but the
file body stops on the last pixel: the palette is genuinely not in the asset. Decoding
without one gave `grey = index * 17`, which is why civilians (who use only indices 0..5)
rendered near-black.

The TLUTs are in the game code, in the `CodeOverlay` segment (`ram = rom + 0x8005E210`):

| what | ROM | RAM | shape |
|---|---|---|---|
| palette bank | `0x1B20D8` | `0x802102E8` | 206 x 16-entry RGBA5551, 32 bytes each |
| sprite descriptors | `0x1B3AA0` | `0x80211CB0` | 67 x 20 bytes, in `Solfiles.eng` order |
| sprite-set table | `0x1B3FDC` | `0x802121EC` | 8 pointers: E1 E2 E3 E4 E6 RB MB DP |
| type -> set/palette switch | `0x16AAF8` | `0x801C8D08` | 20 cases, ids 2000..2019 |

```c
struct SolSprite {           // 20 bytes, big-endian
    u32 pixels;              // patched at load time (heap)
    u32 zero;
    u32 tlut;                // -> palette bank; the drawn TLUT is tlut + palIndex*32
    u8  facings, stages;     // facings*stages == frame count
    u8  width, height;       // per frame
    u16 frame_bytes;         // width*height/2
    u16 tmem_rows;
};
```

Index 15 is the transparent background in all 206 palettes, which matches the 86% of
sprite pixels that carry index 15. **Proof the table is the right one:** all 67 records
agree with the extracted `.IMG` headers on width, frame height and frame count. 67 of 67,
zero mismatches (`python3 tools/romdump/soldier.py list`).

The palette-per-type switch (RAM `0x801FB6E4` does `tlut + palIndex*32`) resolves to:

- 2000..2003, 2005, 2006 = E1 E2 E3 E4 E6 RMBO, palette supplied by the caller.
- **2004 = E5 the chem warrior forces set id `0x7D0`, which is E1.** On the N64 he is a
  recoloured minigunner, not a recoloured flamethrower. Our old mapping said E4.
- 2007 = Moebius (MB palette 0), 2008 = Chan (MB palette 1). The two palettes differ in
  exactly one entry: index 0 is `A4947B` (Moebius's tan hat) or near-black (Chan's hair).
- 2009..2018 = C1..C10 (DP palettes 0..9), 2019 = Delphi (DP palette 10). Civilians are
  eleven differently dressed people, not ten copies of one.

**Which combat palette is which side.** Each combat set has two palettes, a tan/khaki
uniform and a blue-grey one. The sidebar ships two complete cameo sets, `CAM_*.IMG` and
`CAM*2.IMG`, drawn as the same soldiers in exactly those two colourways, and the selector
at RAM `0x8002BB4C` reads character 2 of the scenario name: `'G'` (SCG01EA, the GDI
campaign) selects the `CAM_*` tan table, anything else (`'B'`, SCB01EA) selects the
`CAM*2` blue-grey table. So palette 0 is GDI and palette 1 is Nod.

Tool: `tools/romdump/soldier.py` (`list`, `png`, `sheet`). `assets/png/*.png` for the
infantry are regenerated: `NAME.png` is palette 0, `NAME#k.png` is palette k.

### 2. The Gun Turret was not a wrong slot. A model is a tree, and we read only its root.

Slot 16 is correct. What was missing is that the last word of each 16-byte model-table
record points at a scene-graph node, and nodes have children:

```c
struct ModelNode {           // 0x1C bytes, in GameModelsGeometry
    u32 gfx;                 // display list for this part
    u32 rest_mtx;            // NOT a second display list. See the correction below.
    u32 handle;              // {classTag, data}; classTag 0x800AC2C0 CAFEDEAD holds the
                             // static pose AND, at +0x40/+0x44/+0x48, three animation
                             // TRACK pointers. 0x800AC2D8 BEEFED02 is runtime-driven
                             // (every turret points at it).
    u32 children;            // NULL-terminated array of node pointers, or 0
    u32 scratch[3];          // zero in the ROM
};

**CORRECTION (wave 4): `+0x04` is NOT "the same geometry, second skin".**
It is the node's own pre-baked 4x4 N64 `Mtx` for its REST pose, and the old description
would send anyone hunting a damaged skin straight into geometry-shaped garbage. Decoded:
`0x8012C880` reads as an N64 Mtx (16 s16 integer parts then 16 u16 fractional parts,
0x40 bytes) whose row 3 is

    x = -745 + 0x44FC/65536 = -744.7306
    y =  244 + 0xA444/65536 =  244.6417
    z = -161 - 0xC1B6/65536 = -161.7566

and the CAFEDEAD pose record for that same node (RAM `0x801355E4`) holds
`T = (-744.731, -161.757, 244.642)` in the author (Z-up) frame -- the same translation
reordered as (x, z, -y), which is exactly `objgraph2.py`'s `P_AUTHOR_TO_MESH`. Rotation
is identity there, matching quat (1,0,0,0). Parsed as a display list instead, it yields
a nonsense `G_SETTIMG` of `0x00A10001`.

**There is no second skin anywhere in the scene graph.** That matters for damage states:
see the damage-state row as a known gap, which records that the cartridge has no
damaged building mesh at all.

**And `+0x08` is the door to the animation system.** The handle's CAFEDEAD pose record is
0x4C bytes: `+00 float3 T, +0C float4 quat wxyz, +1C float3 S, +28 float4 scale-orient
quat, +38 float mirror, +3C u32 flags, +40 Track* translation, +44 Track* rotation,
+48 Track* scale`. `objgraph2.py` reads +00..+38 and has never read the three track
pointers, which is why the console's building idle animations and its MCV deploy rig have
never reached a pack. The FEEB class table at RAM `0x800AC2F0` has 12 records giving each
track class its keyframe stride (0001:0x40 0002:0x5c 0003:0x00 0004:0x28 0005:0x2c
0006:0x38 0007:0x3c 0008:0x14 0009:0x10 000a:0x14 000b:0x20 000c:0x08), and
`CAFEDEAD::apply` at ROM 0x8AEB4 memcpys the static pose then evaluates the three tracks
over it.
```

GUN's root is the 19-triangle hexagonal base pad. Its one child, RAM `0x801AE068` /
ROM `0x151AD8`, is a 32-triangle turret with a barrel (bbox 426 x 126 x 779 model units,
long in z). Reading the child list also gives the medium and light tanks their turrets,
the Jeep and Buggy their gun mounts, the Advanced Comm Center its dish gantry, the
Weapons Factory its doors and the helicopter its rotor disc.

`bake.py` now bakes root plus children. Tool: `tools/romdump/objgraph.py`.
**Known limit (since resolved for the static case, see the mount transform work):** the
nodes were thought to carry no translation, so a child was baked at the parent's origin. In the fixed isometric view that reads correctly, but a tank turret is sunk into
its hull rather than sitting on it, and nothing rotates a turret independently of its
body. Where the mount height comes from is still open: it is not in the node, not in the
model-table record, and there is no `G_MTX` in any of these display lists.

### 3. V20 is not 0.457 cells off. NEGATIVE RESULT, nothing changed.

**There is no per-model offset field anywhere.** Over the 229 model-table slots that hold
a model pointer (0..228), the 12 bytes before the pointer are zero in every single one:
they are runtime scratch. Across all 237 scene-graph nodes reachable from the table, the
only non-zero fields are pointers (gfx, gfx_alt, behaviour, child lists); no node holds a
coordinate.

The 0.457 figure is an artefact of the metric, not a placement error. V20 is a farmhouse
**plus a detached outbuilding**. The farmhouse straddles the mesh origin correctly; the
bbox centre and the area centroid both land in the empty yard between the two buildings,
about 0.6 cells along +x. `--diamonds` shows the farmhouse sitting inside its 2x2
footprint. V20 is left exactly as it was, and no per-type fudge table was added.
