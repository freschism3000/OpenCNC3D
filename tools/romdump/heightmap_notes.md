# Terrain heightmap RE (bug #12) — SOLVED

Where the N64 gets its terrain heights (raised plateaus, stepped edges, carved
rivers in the console video, flat in our bake). ROM: `data/rom/cnc_eu.z64`
(big-endian .z64). Extractor: `tools/romdump/heights.py` (same folder, works).

## The answer in one line

**Mechanism (b): a separate per-scenario file.** Every campaign scenario ships a
**65x65 8-bit per-corner heightmap** disguised as an ordinary `.IMG` image named
after the scenario — `SCG01EA.IMG` — with fallback `FLAT.IMG` (all zeros) when a
scenario ships none. World-space vertex height = `byte * 4` (a cell is 256 world
units wide). 65x65 = one byte per VERTEX of the 64x64-cell map.

It hid in plain sight: the file decodes as a legitimate intensity image
(16-byte IMG header, w=65 h=65 fmt=4 depth=1), so it sat unremarked among the
773 IMG files our extractor already handled. It is data, not art.

There is a sibling: `CM<name>.IMG` (`CMG01EA.IMG`, fallback `CMFLAT.IMG`),
65x65 **16-bit** (header fmt=0 depth=2) = per-vertex RGBA5551 **colour map**
(baked lighting/shading), loaded into the second slot right next to the
heightmap. Not needed for geometry, but it is the console's baked vertex
lighting and will matter for look-matching later.

## Evidence trail (all addresses verified by disassembly, capstone 5.0.7)

### 1. The loader — n64engine.cpp, resident segment

Function RAM `0x80048574` (ROM `0x49174`), the N64Map/scenario terrain init
(global N64Map struct = `0x80097150`, stored at fn entry):

- `malloc(0x1081)` tagged n64engine.cpp line 1198 -> stored at **N64Map+0x30**.
  `0x1081 = 4225 = 65*65`.
- filename built by `sprintf(buf, "%s.img", scenName@0x80096694)`
  (fmt string `%s.img` = resident ROM 0x525C / RAM 0x8000465C);
  if missing (check fn 0x800520fc) falls back to `"flat.img"` (ROM 0x5264).
  File body copied with length 0x1081 via 0x80058070.
- second buffer `malloc(0x2102)` (= 65*65*2) -> **N64Map+0x34**; same name with
  the first two chars overwritten `c`,`m` -> `cmG01EA.img`; fallback
  `"cmflat.img"` (ROM 0x5270). That is the colour map.
- The archive REALLY holds these: `SCG01EA.IMG` usize 0x1091 (0x10 header +
  0x1081), `CMG01EA.IMG` 0x2112, `FLAT.IMG` 0x1091 (payload all zero),
  `CMFLAT.IMG` 0x2112. 56 heightmaps total (55 scenarios + FLAT): every SCB01..
  SCB13/21/22 + SCG01..SCG15/30/90 variant; the remaining .MAP scenarios
  (SCB20/31/32/33/35/37/60/61...) have no .IMG and use the flat fallback.

### 2. The consumer — gterrain.c, CodeOverlay

CodeOverlay ROM 0x1643F0..0x1B67D0 -> RAM 0x801C2600 (delta +0x8005E210).
`../src/gterrain.c` string ROM 0x16A610 / RAM 0x801C8820, gterrain code
~ROM 0x1963xx..0x19D100.

- **GTerrain_Init** RAM `0x801F6748` (ROM 0x198538): receives the heightmap
  pointer as stack arg 5 and colour map as arg 6 from the resident caller
  (single call site ROM 0x49610, passing N64Map+0x30 / +0x34); stores them at
  **gTerrain+4 / gTerrain+8** (gTerrain BSS struct `0x8021BB50`). Also takes
  the 64x64 u16BE `.MAP` tile ids (arg 7) and packs each cell record:
  cell init fn RAM 0x801F4E18: `word2 = (word2 & 1) | (tileId << 21)`.
  Cell grid: 64x64 records x 12 bytes at gTerrain+0xC, ordered `[CX][CY]`
  (offset = 0x300*CX + 0xC*CY); rec+0 = flags/links, rec+4 = ptr to the cell's
  4 malloc'd vertices (gterrain.c line 837), rec+8 = tile word.
- **Per-cell build** RAM `0x801F5024` (ROM 0x196E14): mallocs 0x40 = 4 vertices
  x 16 B, then calls the vertex builder 4x with corner offsets (dx,dy) =
  (0,0), (0,1), (1,0), (1,1).
- **Vertex builder** RAM `0x801F4984` (ROM 0x196774) — the proof of the format:
  ```
  x = cellX + dx ; y = cellY + dy
  h = heightmap[y*65 + x]          ; lbu, stride 65
  vtx.x@+0 = x*256                 ; vtx.y@+2 = h*4  <-- THE HEIGHT
  vtx.z@+4 = y*256                 ; s,t@+8,+0xA = gridOffset*1536+64
  ```
  and samples 65-stride neighbours (x±1, y±1) for the vertex normal/colour
  (vector helpers 0x8007ae00/0x8007af04/0x8007af68).

### 3. Corroboration

- SCG01EA.IMG payload: values 28..130; sea floor 28..31, beach/water plane 32,
  main land 64, high plateau 128 — the spatial ASCII render reproduces the
  beach-landing frame from the reference video exactly (sea bottom edge, beach strip,
  stepped rise to the green plateau, high ground NE).
- `FLAT.IMG` = 4225 zero bytes — the do-nothing fallback, and why multiplayer
  maps (and our bake) are flat.
- heights.py sanity across theaters: SCG01EA (temperate, coast): 24/27
  water-capable cells lower than nearby land (mean -19.2). SCB01EA (desert,
  river canyon): 44/57 lower (mean -4.9) — the river is carved below its banks
  even though it crosses the high half of the map (a GLOBAL water-vs-land mean
  is misleading there; use the local comparison). SCB20EA: flat fallback, all 0.
- Debug strings that led here: `'CX=%d CY=%d TX=%d TY=%d H=%f Tile=%d'`
  (resident 0x5478) and the gterrain rodata block `Top/Bottom/Left/Right/
  TopLeft/TopRight/BottomLeft/BottomRight :` + `'  [%d]=%d'` (ROM 0x16A63C..)
  — the latter is a debug dump (fn ROM 0x197DCC) of eight 64-entry
  {u32,u32} histogram arrays (0x200 B apart), edge-matching stats, NOT the
  height store; noted so nobody chases it again.
- The tile word read in the resident debug fns (`*(u32*)(gridPtr + 768*CX +
  12*CY + 0x14) >> 21`) is the same cell grid: the stored pointer is gTerrain
  itself, +0xC grid start +8 word2 = +0x14.

## What this means for the bake

The terrain baker and the per-scenario pack need one addition: read `<SCEN>.IMG`
(fall back to FLAT.IMG exactly like the engine), take the 4225-byte payload
after the 16-byte header as `corner_height[y][x]` (y=0 = north/map row 0, same
orientation as the .MAP), and give every cell quad its 4 corner heights
`* 4` world units (cell pitch 256). Stepped plateau edges then come out of the
data by themselves — neighbouring cells simply share corner bytes. The
`CM<scen>.IMG` RGBA5551 vertex colours are the console's baked lighting if we
want the exact look.

## Extractor

`tools/romdump/heights.py SCG01EA` -> `heights_SCG01EA.json`
(65x65 `corners[y][x]` raw bytes + 64x64 `cells[cy][cx]` = corner quadruples
[h(cx,cy), h(cx+1,cy), h(cx,cy+1), h(cx+1,cy+1)]; unit documented in-file),
plus the water-below-banks sanity print. Read-only on the ROM.
