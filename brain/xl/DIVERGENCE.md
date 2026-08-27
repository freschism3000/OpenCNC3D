# brain/xl -- divergence log

The XL brain is a REAL SOURCE FORK of `brain/vanilla` taken at git tag
`brain-xl-forkpoint`, per `docs/design-xl-brain.md` (the contract). This file is
the honest record of every way this tree differs from that tag, grouped by
concern. The rule (same spirit as the changelog rule): a file that diverges from
the forkpoint without an entry here is a gate failure; a claimed divergence that
does not exist is a lie in the log. `common/` may never diverge at all --
`tools/check-xl-common.sh` enforces byte equality with `brain/vanilla/common`
forever, so upstream and portability fixes to common land in one place and flow
to both brains.

Re-pinning vanilla to a newer upstream re-applies here BY HAND, with an entry.
That manual cost is the honest price of the fork.

## Fork shape (what was and was not copied)

- Copied from the tag: `tiberiandawn/`, `common/`, `cmake/`, `CMakeLists.txt`.
- NOT copied (per the contract's layout): `redalert/`, `tests/`, `tools/`,
  `resources/`, `scripts/`, `CMakePresets.json`, `License.txt`, `README.md`.
  The GPL text governing this tree is `brain/vanilla/License.txt` in the same
  repository; source publications that carry brain/xl must carry it too.
- The networking sources (`ipx*.cpp`, `netdlg.cpp`) and the internal map-editor
  sources remain in `tiberiandawn/` as copied but are not compiled into the XL
  target, exactly as vanilla's remaster DLL never compiled them.

## Build system

- `CMakeLists.txt` (root): reduced to the single-purpose fork root -- builds
  `common` + `commonr` + `TiberianDawnXL` only. All vanilla options are pinned
  (`set()`, not `option()`): no RA, no game executables, no SDL/OpenAL/DSOUND/
  DDRAW, no networking, no tests, no tools. Same module path, same
  `REMASTER_DEFS`, same MS-extension flags, so `common/` compiles byte-identical
  source under identical definitions.
- `tiberiandawn/CMakeLists.txt`: the `TIBDAWN_SRC` list is verbatim from the
  tag; everything after it is replaced by the one `TiberianDawnXL` SHARED target
  (PREFIX "", same shape as vanilla's `TiberianDawn` target). Dropped with their
  targets: VanillaTD, icon/version-resource generation, macOS bundle plumbing,
  the remaster_mods POST_BUILD copy (needs `resources/`, not copied), ClangFormat.
  Defines: `TIBERIAN_DAWN` (in the brain build it only selects the 128-wide
  `MAP_MAX_CELL_WIDTH` branch it would get anyway, and it matches how the host
  includes `dllinterface.h`) and `XLMAPS` (the fork marker; never an option).
  `MEGAMAPS` + `EIGHTPLAYERS` are NOT defined: their conditionals are collapsed
  to the ON values in the source (below), the tree being the fork.

## Types (coordinate/cell widening)

- ALL `#ifdef MEGAMAPS` / `#ifdef EIGHTPLAYERS` conditionals collapsed to their
  ON branches across `tiberiandawn/` (22 files, directives only -- no code line
  changed). At this step the fork still compiles the exact classic engine
  (7-bit stride, 16-bit COORDINATE axes); the collapse exists so the widening
  diffs that follow are readable code changes, not ifdef surgery. Comment
  mentions of the two macro names survive where they explain history.

THE WIDENING (docs/design-xl-brain.md, coordDesign/cellDesign, applied verbatim
except where noted):

- `defines.h`: `COORDINATE` = uint64_t scalar, X low u32 / Y high u32, each
  axis an absolute lepton count; `LEPTON` = uint32_t; `CELL` = int32_t;
  `LEPTON_COMPOSITE` and `CELL_COMPOSITE` DELETED; `COORD_COMPOSITE` is now
  `union { COORDINATE Coord; struct { uint32_t X, Y; } Sub; }` with a
  compile-time refusal of big-endian builds. `MAP_CELL_MAX_X/Y_BITS` = 10
  (1024 stride) from this first widened commit; `HIGH_COORD_MASK` reborn as
  `0xFFF00000FFF00000ULL`. New named constants replace the packed-hex idiom
  family: `COORD_CENTER`, `COORD_KEEP_CELL`, `COORD_FRACTION_MASK`, and the
  constexpr makers `XY_Delta(dx,dy)` (relative, two's-complement per axis) and
  `XY_Coord_Const(x,y)` (absolute). `OLD_MAP_CELL_W/TOTAL` (64/4096) name the
  legacy scenario geometry the classic build spelled `MAP_CELL_TOTAL/4`;
  `CLASSIC_MAP_CELL_W/TOTAL` (128/16384) name the classic brain's stride for
  the scan-window rule below; `TIBERIUM_LIST_MAX` pins the growth/spread list
  capacity at the classic 64 (see the determinism entry below).
- `function.h`: the whole inline suite regenerated as shift/mask forms --
  XY_Coord, Coord_X/Y, Coord_XCell/YCell, Cell_X/Y (masked exactly as the old
  bitfields masked), XY_Cell, Coord_XLepton/YLepton, Coord_Whole/Fraction/
  Snap/Add/Sub/Mid, Cell_Coord, XYP_Coord, XYPixel_Coord (was MAKE_LONG),
  the XYP_COORD macro (was a 16-bit shift pack), Confine_Old_Cell generalized
  to `(cell % 64) + (cell / 64) * MAP_CELL_W`. Coord_Add/Sub are two
  independent 32-bit axis adds with no cross-axis carry -- the old
  `(int)(short)` truncation casts died with the composites.
- `coord.cpp`: Coord_Cell via shifts; Coord_Move/Move_Point axis locals and
  reference parameters widened short -> int (declaration copies in function.h
  and startup.cpp, pixel-space caller locals in aircraft.cpp). Move_Point
  casts common's calcx/calcy results back through int16: common/ is frozen
  byte-identical, and its helpers return the fixed-point product truncated to
  the classic unsigned 16-bit axis pattern, so the sign is restored at the one
  caller. Exact for per-call distances under 32768 leptons (128 cells);
  per-tick motion and scatter distances are orders of magnitude below.
- Packed-literal regeneration: `const.cpp` AdjacentCoord[8] and
  StoppingCoordAbs[5]; `drive.cpp` 406 track-table entries via
  `tools/xl-parity/regen-track-tables.py` (kept for upstream re-pins);
  singles in aircraft/anim/building/cell/display/dllinterface/foot/infantry/
  terrain/unit.cpp including BuildingClass::CenterOffset, the war-factory
  half-lepton snap mask (0xFF80FF80 -> 0xFFFFFF80FFFFFF80ULL), and the
  keep-cell masks -> COORD_KEEP_CELL. Dead `#ifdef NEVER` debug blocks in
  debug.cpp keep their classic hex (the compiler never sees them).
- OVERLOAD COLLISIONS from CELL becoming int (the same scalar as TARGET):
  AbstractClass::Direction(CELL) and Distance(CELL) members deleted; every
  former caller now spells the global cell-resolution form out. The callers
  were found BY THE COMPILER, not by grep: a scratch build with
  `typedef long CELL` makes every such call ambiguous (long converts equally
  to TARGET/int and COORDINATE/u64), which enumerated Direction callers in
  building.cpp and unit.cpp and FIVE Distance callers -- four in team.cpp
  (stray-distance checks against the team's CELL-typed Center/ObjectiveCenter)
  and one in findpath.cpp (Passable_Cell). A first grep-based survey had
  called Distance(CELL) caller-free and the parity oracle caught the lie
  within four ticks: `unit->Distance(Center)` silently bound Distance(TARGET),
  team movement never issued, and the first divergent field was an enemy
  infantryman's nav. The scratch-enumeration trick is the tool to reuse on
  any future overload retirement.
  EventClass's (RTTIType, CELL) PLACE ctor merged into the (RTTIType, int)
  ctor and the (int, CELL) SPECIAL_PLACE ctor into (TARGET, TARGET) --
  Data.Place aliases Data.Specific and Data.Special aliases Data.NavCom
  field-for-field (static_asserts in event.cpp pin it; the classic engine
  already used the Place/Specific pun for sidebar placement with cell -1).
  RESIDUAL RISK RECORDED: a CELL argument reaching a TARGET overload no
  longer errors at compile time; the compiler enumeration above plus the
  cross-brain parity oracle are the net.
- Occupy/overlap/exit/spillage offset lists STAY `short` (REFRESH_EOL 32767
  sentinel unchanged); local pointer/array decls in building.cpp, bullet.cpp,
  display.cpp corrected from CELL* back to short*. At the 1024 stride a row
  step is 1024, so a short offset reaches +/-31 rows -- every authored list is
  under 3.
- `map.cpp` .BIN read/write loops: `MAP_CELL_TOTAL/4` -> `OLD_MAP_CELL_TOTAL`
  (the classic identity only held at a 128 stride).
- `queue.cpp` desync-debug CRC folds `(int)Coord` -> both axes folded
  (`coord ^ (coord >> 32)`), and the `%x` coordinate prints -> `%llx`.
  GameCRC values differ from classic by construction; nothing in the AI-only
  host consumes them, and cross-peer comparison only ever happens between
  identical builds.
- DETERMINISM: THE TIBERIUM SCAN WINDOW. MapClass::Logic's growth/spread
  sweep is chunked off MAP_CELL_TOTAL and its candidate lists were sized off
  MAP_CELL_W -- both stride-coupled, and both feed Random_Pick draws whose
  ORDER in the one seeded stream depends on when the sweep reaches a
  candidate. On identical confined content the XL brain must draw exactly as
  the classic brain draws, or the cross-brain parity oracle (and any future
  replay of classic content) shears at the first sweep. So: classic-format
  scenarios (MapBinaryVersion == MAP_VERSION_NORMAL) sweep the CLASSIC window
  -- 16384 indices mapped row-by-row into the 1024 stride at the classic
  chunk rate (120 cells/tick, ~137-tick cadence); XL-native (Version=1)
  scenarios sweep the full stride space. TiberiumGrowth/TiberiumSpread stay
  at the classic 64-entry capacity (TIBERIUM_LIST_MAX) for the same reason:
  overflow past capacity consumes the stream. Revisit both as TUNING when
  XL-native content outgrows them; they are gameplay numbers as well as
  determinism surface.
- Known stride-coupled random consumer left as-is: scenarioini.cpp's
  200-retry fallback `Random_Pick(0, MAP_CELL_TOTAL - 1)` for a start
  centroid (multiplayer scenario setup only; unreachable in the Phase 1
  campaign-content gates, and XL-vs-classic parity is not a goal for
  XL-native skirmish content). Flagged for the Phase 3 house work.
- The never-placed object's ctor sentinel (object.cpp `Coord = 0xFFFFFFFF`)
  widened to `~0ULL`: the 32-bit literal zero-extends into the 64-bit scalar,
  which put "bogus" in the X axis and ZERO in Y -- a limbo object at row 0
  instead of nowhere. All-ones in both axes restores the classic semantics
  at the new width; the parity comparator collapses the classic and XL
  spellings of that sentinel to one token (positions of never-placed limbo
  objects are meaningless by the dump's own documentation).
- The remaster's vestigial 8-bit render page (externs.h GBUFF_INIT 3072x3072,
  the collapsed MEGAMAPS value) deliberately does NOT scale with the stride:
  1024 cells x 24px would be a 576MB page for output CNC3D never reads. A
  tactical view confined to the page's 128-cell reach is the accepted cost;
  revisit only if some later phase starts consuming the brain's own pixels.
- INTEGER LAW notes: negative >> and negative %/ division rely on arithmetic
  shift and C++11 truncation, exactly as the classic engine already did
  (calcx's `tmp >> 7`); Cell_Y masks after the shift as the old bitfield
  masked.

## Houses

- (nothing yet -- Phase 3 territory; the fork still builds the EIGHTPLAYERS
  roster collapsed to ON)

## Pathfinder

- (nothing yet -- Phase 4 territory; `findpath.cpp` is the vanilla edge-follower
  compiled over the widened CELL, bounds UNCHANGED at 400 so that classic-content
  parity holds. The 400->1200 scaffolding bump is deliberately NOT taken in
  Phase 1: a 64x64 route that hits the bound must hit it identically on both
  brains or the parity oracle lies.)

## ABI and host

- Phase 1 keeps EVERY dllinterface struct the host reads at the CLASSIC
  layout, deliberately: the boot and parity gates run the XL dylib through
  the UNMODIFIED host (`--dylib`), which compiled against vanilla's
  dllinterface.h. So MAX_EXPORT_CELLS stays 128*128 (per the phase plan; the
  1M-cell export sizing is the 256-through-the-eyes phase), HomeCellX/Y and
  the dynamic-map cell fields keep their classic widths, and the house
  arrays keep MAX_HOUSES 32 (houses are Phase 3). Confined 64-wide content
  fits every classic field.
- NEW EXPORT `CNC3D_ABI_Facts(struct CNC3DAbiFactsStruct*)`
  (dllinterface.h/.cpp): {AbiVersion, MapStride=1024, MaxExportCells,
  MaxHouses, MaxPlayers, sizeof(CNCMapDataStruct), sizeof(CNCPlayerInfoStruct),
  sizeof(CNCShroudEntryStruct)} so the host's vtable phase can assert at
  dlopen and refuse a mismatched dylib loudly.
- The OBJ| dump's `inicell` field (CNC3D_Print_One) still emits the
  forkpoint's `cy * 64 + cx` convention; brain/vanilla has an uncommitted
  in-flight change of that formula elsewhere in the tree, and the parity
  comparator treats inicell as stride-derived either way. Re-sync when that
  lands.

## Formats

- `[Base]` INI nodes (base.cpp Read_INI/Write_INI): the file format stores the
  CLASSIC packed 32-bit coordinate as a decimal (X axis low 16 bits, Y high
  16, absolute lepton counts). Read now unpacks per-axis into the widened
  scalar; write packs the same classic form back, so classic INIs round-trip
  byte-identical. Found by the parity oracle on SCG10EA: the raw atol landed
  the whole packed value in the X axis and the AI's pre-build FACT node
  became a phantom limbo building. INI coordinate fields elsewhere are cell
  numbers or per-axis values (audited: this was the only packed-coordinate
  INI surface in the reader). Write_INI masks each axis to 16 bits -- exact
  for classic content; an XL-native base past 256 cells per axis needs the
  Phase 2 map-format work.
- KNOWN GAP, not fixed in Phase 1: MAP_VERSION_MEGA content (the 128-wide
  .BIN format Read_Binary_Big reads, e.g. the retail skirmish conversions
  and the editor's new 128x128 maps) stores raw 128-stride cell NUMBERS,
  which land unconfined -- and therefore WRONG -- in the 1024 stride. The
  Phase 1 gates exercise only MAP_VERSION_NORMAL missions, which confine
  correctly through Confine_Old_Cell. The mega loader needs its own
  confinement (cell%128 + cell/128*MAP_CELL_W) when the 256-through-the-eyes
  phase brings real content to the XL brain; the tiberium scan-window rule
  above must learn the same case then.
