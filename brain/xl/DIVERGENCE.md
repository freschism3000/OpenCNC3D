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

- THE HOUSE MASKS ARE 64 BIT, 30 Aug 2026. Phase 3's first landing, and it
  grows no houses: the roster is still the EIGHTPLAYERS twelve. What changed is
  the WIDTH every house bit lives in, because the widths were the thing that
  would have failed silently when the roster did grow.

  `defines.h` HOUSEF_* now shift `1ULL`; `cell.h`
  IsMappedByPlayerMask/IsVisibleByPlayerMask, `house.h` Allies and its
  Get_Allies accessor, `object.h` IsSelectedMask and CNC3D_InvincibleHouses,
  and `type.h` Ownable all become `uint64_t`. The `Get_Ownable` virtual widens
  with them, which is why `object.h`, `techno.h`, `type.h`, `abstract.cpp`,
  `object.cpp` and `techno.cpp` all appear: the compiler refuses a mismatched
  override, and that is the whole reason the signature is the safe way to carry
  the width. `mapedit.cpp` and `dllinterface.cpp` hold the two remaining
  `1ULL <<` sites.

  WHY THE SHIFT AND NOT ONLY THE FIELD. A wider field does not fix `1 << house`:
  a bare 1 is an int, so at house 32 that is undefined behaviour and not a lost
  bit, and it is undefined at COMPILE time in a simulation that every peer has to
  agree on. `tools/xl-house-mask-guard.sh` fails on a bare shift
  anywhere in the fork and carries a self-test, because a grep guard that has
  stopped matching reports a clean tree forever.

  TWO NARROWINGS ARE DELIBERATE AND GUARDED. `HouseClass::Get_Ally_Flags` and
  the `IsSelectedMask` copy in `dllinterface.cpp` hand a 64 bit mask to a 32 bit
  export field, because every dllinterface struct the host reads is held
  byte-identical to the classic brain's so one host drives either brain. Both
  now cast explicitly and both carry a `static_assert(HOUSE_COUNT <= 32)`, so
  growing the roster past 32 stops the build rather than dropping the alliances
  and selection of every house above the 32nd. Widening those fields is a change
  to the brain AND the host in one commit, and it is not this one.

  NOT DONE HERE: the multi-first enum reorder, and any growth in the roster.
  Both change what a house INDEX means, and their failure modes (a comparison
  like `h >= HOUSE_MULTI1` that silently reverses, an index that was serialised)
  are exactly the ones a compiler cannot see. They need a brain that can boot a
  scenario as the oracle.

- PORTED 27 Aug 2026: the four `CNC3D_*` exports the classic brain gained
  after the fork point and XL lacked -- `CNC3D_Set_Build_Anywhere`,
  `CNC3D_Get_Build_Anywhere`, `CNC3D_Proximity_Ok` and
  `CNC3D_Force_Verdict` (the v0.6.3 cheat menu). With them the symbol
  surfaces differ by exactly one name in one direction: XL exports
  `CNC3D_ABI_Facts` and the classic brain does not. Gate XLb asserts that
  and nothing looser, so the host can call every other export
  unconditionally on either brain.

  The Build Anywhere machinery came with them, at all THREE copies of the
  adjacency rule (DisplayClass, DLLExportClass and BuildingClass), because
  hooking only the cursor routines gives a green cursor and a click that is
  silently refused -- which is what the classic brain shipped for an hour
  on 26 Aug 2026. `DisplayClass::CNC3D_BuildAnywhereHouses` is uint64_t
  here where the classic brain has unsigned: the 64-house phase widens
  every house mask, and this one would otherwise be found by a player at
  slot 32 rather than by a compiler.

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
  forkpoint's `cy * 64 + cx` convention. That formula is in flight on the
  classic side, and the parity comparator treats inicell as stride-derived
  either way, so it is dropped from the comparison rather than normalized.
  Re-sync the field when the classic formula settles.

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
- CLOSED 27 Aug 2026 (was a Phase 1 known gap): MAP_VERSION_MEGA content --
  the 128-wide format, which is the retail skirmish conversions and
  everything the mission editor makes -- did not load correctly on this
  brain. The gap as recorded was the missing stride confinement. It was
  three faults, and the other two were worse:

  1. STRIDE. A scenario's cell numbers are at the AUTHORING build's stride,
     never at ours. Nine reader sites confined MAP_VERSION_NORMAL and let
     MAP_VERSION_MEGA through raw, so every mega cell landed at
     (y*128 + x) in a 1024-wide array. Now one helper does both formats:
     `Confine_Scenario_Cell(cell, binary_version)` in function.h, with
     `Scenario_Cell_Of` as its inverse for the writers. Sites: overlay.cpp,
     smudge.cpp, terrain.cpp, unit.cpp, infantry.cpp, building.cpp,
     display.cpp (waypoints and cell triggers), map.cpp (both .BIN paths).
  2. THE RECORD WAS THE WRONG SIZE, IN TWO DIFFERENT WAYS. The sparse
     big-map .BIN record is four bytes on disk: `{u16 cell, u8 tmpl,
     u8 icon}`. It was spelled with this brain's own CELL, which is
     int32_t here and `signed short` in the classic brain, so
     Read_Binary_Big (no pragma pack) read EIGHT byte records and
     Write_Binary_Big (packed) wrote SIX. The two halves of one file's
     format did not agree with each other, and neither agreed with a single
     shipped map. It is now an explicit `MegaBinaryRecord` in map.cpp with
     a static_assert on its size. THE GENERAL RULE: a type whose width is a
     property of the BUILD must never be a field in a file whose layout is
     a property of the FORMAT.
  3. THE TIBERIUM SCAN WINDOW followed the same wrong test, so a mega map
     swept the full 1024 stride and sheared the seeded random stream
     against the classic brain on identical content. Both shipped formats
     are classic content; the test now names the XL-native case as the
     exception, which does not exist yet.

  A TENTH READER was found afterwards and is fixed: `TemplateClass::Read_INI`
  (template.cpp:104) reads scenario cell numbers and was never confined. It
  runs only when a map has a `[TEMPLATE]` section AND its `.BIN` did not
  load, and no shipped map has both, so nothing exercised it.

  `Read_Binary_Big`'s post-condition was `i == MAP_CELL_TOTAL`, a quantity
  of the BUILD tested against a file whose length is a quantity of the
  FORMAT. The format is sparse, so a complete file never has 1,048,576
  records: the function always returned false and a truncated `.BIN` was
  indistinguishable from a complete one. Now `i == file_size`. It changes
  no reachable behaviour (both callers treat false as "no binary" and fall
  through to a `[TEMPLATE]` section that shipped maps do not have), but a
  post-condition that cannot hold is not a post-condition.

- **MAP_VERSION_XL (2), a scenario format that carries its own stride.**
  Added 27 Aug 2026, and it exists because the two legacy formats cannot be
  extended. Their cell numbers are at the AUTHORING build's stride and
  neither the `.BIN` nor the INI records which that was; the reader has to
  already know. That works while there are exactly two and `MAP_VERSION`
  picks between them, and it runs out immediately after: the sparse key is
  16 bits, which reaches 256x256 exactly and is dead one cell past it, and
  `[Base]` packs 16 bits per axis for the same ceiling.

  Version 2's cell numbers ARE this brain's own, at the fixed 1024 stride
  the whole XL ladder shares, so `Confine_Scenario_Cell` is the identity for
  it and there is no conversion to get wrong. Every tier -- 256, 512, 1024 --
  is Version 2; the size of a map is `[MAP] Width/Height` and the STRIDE is
  a property of the format, not of the map. That is what fixing the address
  space at 1024 from the first XL commit bought.

  Its `.BIN` (`MapClass::Read_Binary_XL` / `Write_Binary_XL`, map.cpp) is a
  twelve byte header -- magic `C3XB`, version, the stride it was written
  against, the record count -- followed by eight byte records with a 32 bit
  key. Both structs are declared in their own terms and asserted, per the
  rule above. The stride field means a file from another address space is
  REFUSED rather than misread; the explicit count means a truncated file is
  refused rather than read short; and an empty map is a twelve byte file
  rather than a zero byte one, which the mega format cannot distinguish
  from a failed write.

  `display.cpp`'s version clamp had to move with it, and that was the whole
  bug on the first attempt: `Bound(MapBinaryVersion, 0, 1)` did not reject a
  Version=2 map, it read it AS the mega format, converting every cell from a
  stride it was never written at, and landed as a segfault in the waypoint
  loop. The clamp is now `Bound(..., 0, MAP_VERSION_XL)` and the comment
  says why a narrowing clamp is worse than a refusal.

  `Write_INI` no longer forces `MAP_VERSION_MEGA` on every save. It kept the
  format the map came in as, which is right now that the writers convert
  through `Scenario_Cell_Of`, and it promotes a NORMAL-format map whose rect
  has outgrown 64 cells rather than aliasing two cells onto one INI key.

  Gate XLg covers all three tiers plus determinism, and carries a NEGATIVE
  CONTROL: a refused `.BIN` is not fatal (the loader falls back to the INI's
  `[TEMPLATE]` section) and the object dump has no terrain channel, so a
  corrupted map passed every leg with a byte-identical dump hash until the
  control was added.

## The unshroud debug flag reaches the shroud export

`Get_Shroud_State` filled `IsVisible` and `IsMapped` straight from
`cellptr->Is_Visible/Is_Mapped(PlayerPtr)` with no `Debug_Unshroud` term, which is
the same defect the unforked engine carried and which was fixed there first. Every
other answer to "can the player see this" in that file already folds the flag in:
the object export, the cell-draw export, and the tactical cursor path.

The consequence was a disagreement rather than a missing feature. With the fog
cheat on, the engine lifted the shroud and would accept an attack, while the
snapshot the host reads still reported the cell unseen -- so the pointer offered a
move over an enemy the click would in fact attack.

**This is NOT a divergence from the unforked engine.** Both copies now carry the
identical two lines, and the fork is listed here only because its own copy had to be
changed by hand: `tiberiandawn/` is the directory this fork is allowed to differ in,
so nothing asserts the two files are equal and a fix in one does not travel to the
other. A big map is where a player is most likely to reach for that cheat, which is
why the fork wanted it as much as the engine it forked from.
