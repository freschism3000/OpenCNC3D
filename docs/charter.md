# CNC3D — Project Charter

**A "PC version of C&C 64":** the Nintendo 64 3D presentation of *Command & Conquer:
Tiberian Dawn*, driven by the GPL DOS game logic and skinned with the N64's 3D assets.

It targets **Windows and macOS**. The renderer is held to fixed-function OpenGL 1.1 so
that a native **Windows 98 + 3dfx Voodoo 2** build stays reachable; that is a stretch
goal, not the platform the project is for.

A preservation and reverse-engineering project. The cartridge and the 1995 disc data
are never redistributed; only what is written here is. Design authority: this charter +
`docs/re-findings.md` (the verified RE territory). When they disagree, findings win
on facts; the charter wins on intent.

---

## The architecture: three organs from three donors

The two C&C builds **share data, not code**. So this is a transplant, not a merge:

1. **Brain → the 2020 GPL DOS logic** (`electronicarts/CnC_Remastered_Collection`).
   Reuse units/AI/rules/missions via the flat C ABI seam:
   `CNC_Advance_Instance` (one 15 Hz tick) + `CNC_Get_Game_State` (structured state
   for a renderer). The N64 already speaks the same `.INI`/`.BRF` mission language,
   so no N64 logic code is needed. **Known-feasible** (Vanilla Conquer builds this
   logic natively with mingw).

2. **Skin → extracted N64 assets** — 3D models, `.IMG` textures, `.MAP`/`.IMG`
   height-mapped terrain, the photo-derived terrain art. The only content that exists
   nowhere else. Formats are undocumented publicly, but the ROM is **not stripped** and
   names the code that parses each one (see findings). **This is the project's risk.**

3. **Eyes → a new Win98/Voodoo2 renderer we write**, fed by `CNC_Get_Game_State`,
   drawing the skin, with **infantry billboarded as sprites** (exactly how the N64 did
   it). Renderer API (Glide vs Direct3D 6/7 vs MiniGL) is **deferred** until we have a
   live state feed *and* real assets to draw — deciding now is premature.

## Build / deploy environment

**Windows and macOS**, developed and built on both, from one source tree. The Windows
binary is produced with a **mingw-w64 i686** toolchain; the macOS one with clang and SDL.
A native **Windows 98 SE** build over the same sources stays a stretch goal, which is why
the renderer is held to fixed-function OpenGL 1.1 and the renderer API for that tier
(Glide vs MiniGL) is still deferred.

**RE tooling** (this repo's `tools/`) is Python and runs on either machine; it emits
extracted assets, not shipped code.

## Scope

**In:** ROM asset extraction pipeline · GPL logic as a headless native sim (state feed)
· a 3D renderer for the N64's fixed-isometric camera (terrain height-map + 3D
buildings/vehicles + billboarded infantry) · load N64 missions (`.INI`/`.BRF`) ·
one theater end-to-end first (Temperate), then Desert.

**Out (for now):** multiplayer/skirmish (the N64 cut it too) · Winter theater (N64 has
no snow tiles) · the modern GlyphX HD renderer · FMV (N64 replaced it with voiced
slideshows) · net code · map editor.

## Milestones (critical-path order — assets first, they gate everything)

- **M0 — Recon (DONE).** ROM identified, structure mapped, module→format map
  recovered, text/mission footholds located. Risk collapsed: formats are
  self-documenting. See `docs/re-findings.md`.
- **M1 — Crack the archive (DONE).** 24-byte directory records decoded across 6
  tables; **1,615 unique files** enumerated (`docs/rom-manifest.csv`).
- **M2 — Decode the main codec (DONE).** Method `0x22` reversed from MIPS — a
  hash-indexed LZ77, not Westwood LCW. **817/817 main-archive files decompress
  exactly.** 1,458 real files extracted to `assets/extracted/`: missions, terrain maps
  and briefing scripts are byte-perfect.
- **M3 — Image formats (DONE).** `.IMG` decoded → PNG (756/770, `tools/romdump/img.py`):
  buildings, cameos, terrain tiles, infantry sprite strips all recovered. `.JIM` turned
  out to be **pre-rendered Japanese briefing text**, not models (a bonus find: the EU
  cart holds an unexposed Japanese localisation). `.TEM` = 24×24 terrain templates.
- **M4 — 3D models (NOT WORKING — 1 of 379).** The F3DEX2 → OBJ pipeline is sound (it
  produced a pixel-perfect "COMMAND & CONQUER" logo), but locating each display list's
  vertex block is currently guesswork, so essentially every other mesh is triangle soup.
  No textures at all yet. Ruled out: static overlay table, one-delta-per-overlay,
  models-as-archive-files, G_DL branch tracing (see `re-findings.md`). **The remaining
  path is to read the overlay loader in MIPS** and recover real ROM→RAM mappings.
  A browser model viewer exists at `assets/model-viewer.html` (WebGL, self-contained).
- **M5 — Textures (DONE).** RDP tile state machine in `tools/romdump/textures.py`;
  **189/202 models textured**, OBJ+MTL+PNG in `assets/textured/`, textured WebGL viewer
  at `assets/model-viewer.html`. Key fixes: overlay-group-scoped pointer resolution
  (segments sharing a RAM slot were shadowing each other), the global CI palette at
  ROM 0x99130, and carrying RDP tile state across display lists.
- **M6 — Brain online.** Build the GPL logic headless; prove the tick +
  `CNC_Get_Game_State` feed round-trips a loaded N64 mission.
- **M7 — Eyes.** Choose renderer API; draw one mission: terrain + buildings +
  vehicles + billboarded infantry, fixed-iso camera, on Win98/Voodoo2.

## Principles

- **Read-only on the ROM.** Never modify it; never commit it or extracted assets.
- **Verify against bytes, not lore.** Every format claim gets checked in `re-findings.md`.
- **The tick is the clock** (inherited from the GPL logic: 15 ticks/sec, deterministic).
- **Small runnable slices**, one theater/mission before breadth.
- **Surface decisions, don't invent them.** Where a format or a design choice is
  undecided, say so and record it rather than guessing at an answer.
