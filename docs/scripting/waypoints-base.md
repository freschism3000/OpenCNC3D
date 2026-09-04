All paths below are relative to `brain/vanilla/tiberiandawn/`.

---

# 1. `[Waypoints]`

## Storage and constants

```c
// defines.h:2551-2560
/**********************************************************************
** These are special indices into the Waypoint array; slots 0-25 are
** reserved for letter-designated Waypoints, the others are special.
*/
typedef enum WaypointEnum : unsigned char
{
    WAYPT_HOME = 26, // Home-cell for this scenario
    WAYPT_REINF,     // cell where reinforcements arrive
    WAYPT_COUNT,
} WaypointType;
```

So `WAYPT_HOME == 26`, `WAYPT_REINF == 27`, `WAYPT_COUNT == 28`.

```c
// scenario.h:63-69
/*
**	This is an array of waypoints; each waypoint corresponds to a letter of
** the alphabet, and points to a cell number.  -1 means unassigned.
** The CellClass has a bit that tells if that cell has a waypoint attached to
** it; the only way to find which waypoint it is, is to scan this array. ...
*/
CELL Waypoint[WAYPT_COUNT];
```

Sentinel is `-1`, set three ways: `scenario.cpp:77-79` (ctor loop), `init.cpp:149` `memset(Scen.Waypoint, 0xFF, sizeof(Scen.Waypoint));`, and the INI default at `display.cpp:1335`.

A `CELL` is a 12-bit packed cell number, `MAP_CELL_MAX_X_BITS 6` / `MAP_CELL_MAX_Y_BITS 6` → `MAP_CELL_W = MAP_CELL_H = 64`, `MAP_CELL_TOTAL = 4096` (`defines.h:262-272`). So a waypoint value is `y*64 + x` over the whole 64x64 grid, **not** relative to the `[MAP] X/Y` origin. (MEGAMAPS builds use 7/7 bits and run old values through `Confine_Old_Cell` -- `display.cpp:1337-1343`.)

## INI format

Read at `display.cpp:1329-1346` (inside `DisplayClass::Read_INI`):

```c
for (int i = 0; i < WAYPT_COUNT; i++) {
    char buf[20];
    sprintf(buf, "%d", i);
    Scen.Waypoint[i] = ini.Get_Int("Waypoints", buf, -1);
    if (Scen.Waypoint[i] != -1) { ... (*this)[Scen.Waypoint[i]].IsWaypoint = 1; }
}
```

Written at `display.cpp:1438-1447`:

```c
static char const* const WAYNAME = "Waypoints";
ini.Clear(WAYNAME);
for (int i = 0; i < WAYPT_COUNT; i++) {
    if (Scen.Waypoint[i] != -1) {
        sprintf(entry, "%d", i);
        ini.Put_Int(WAYNAME, entry, Scen.Waypoint[i]);
    }
}
```

Format is therefore:

```
[Waypoints]
<index>=<cellnumber>
```

- Key is the **decimal index**, `"%d"` -- not zero-padded (unlike `[Base]`, which is `"%03d"`). Range 0..27; anything ≥ 28 is never read.
- Value is a decimal `CELL`.
- **Unassigned waypoints are omitted from the file entirely.** The index is carried by the key, so the on-disk list is sparse and *not* renumbered on save. There is no compaction at the file level.
- Indices 0-25 are the letter waypoints A..Z (`cell.cpp:1073-1084` renders `waypt[0] = 'A' + i`; the editor binds ALT+A..ALT+Z to `waypt_idx = To_ASCII(...) - KA_a` at `mapedit.cpp:940-982`).

## Special indices

| Index | Meaning | Evidence |
|---|---|---|
| 0-25 | Letter waypoints A-Z. Multiplayer **start positions** are drawn from this range only. Team `Move`/`Unload` mission arguments index into it. | `scenarioini.cpp:1533`, `team.cpp:476` |
| **25 ('Z')** | **Hardcoded drop-zone / LZ smoke cell.** Trigger action `ACTION_DZ` spawns `ANIM_LZ_SMOKE` at `Scen.Waypoint[25]` with no bounds check. Three copies (three trigger-action switch variants): `trigger.cpp:468`, `trigger.cpp:658`, `trigger.cpp:856` -- `new AnimClass(ANIM_LZ_SMOKE, Cell_Coord(Scen.Waypoint[25]));` | as cited |
| **26 = `WAYPT_HOME`** | Scenario **home cell** -- the initial tactical/camera position, and the seed for all four F-key view bookmarks. | `display.cpp:1352-1357` |
| **27 = `WAYPT_REINF`** | **Air/drop reinforcement arrival cell** (`SOURCE_AIR`). Also the default `Argument` for auto-generated `TMISSION_UNLOAD` reinforcement teams. | `display.cpp:2838`, `reinf.cpp:572` |

`WAYPT_HOME` fallback and view seeding (`display.cpp:1351-1357`):

```c
if (Scen.Waypoint[WAYPT_HOME] == -1) {
    Scen.Waypoint[WAYPT_HOME] = XY_Cell(MapCellX, MapCellY);
}
Set_Tactical_Position(Coord_Whole(Cell_Coord(Scen.Waypoint[WAYPT_HOME])));
Scen.Views[0] = Scen.Views[1] = Scen.Views[2] = Scen.Views[3] = Scen.Waypoint[WAYPT_HOME];
```

`WAYPT_REINF` consumed by `DisplayClass::Calculated_Cell(SOURCE_AIR, ...)` (`display.cpp:2835-2853`):

```c
case SOURCE_AIR:
    cell = Scen.Waypoint[WAYPT_REINF];
    if (cell < 1) {
        cell = Coord_Cell(TacticalCoord);
        return (cell);
    } else {
        if ((*this)[cell].Cell_Techno()) {
            for (int radius = 1; radius < 7; radius++) { ... scatter to a free cell ... }
        }
    }
```

Note `cell < 1` (not `== -1`): cell 0 is also treated as "unset", falling back to the current tactical view.

Waypoints-as-team-mission-arguments (`team.cpp:470-481`):

```c
case TMISSION_MOVE:
case TMISSION_UNLOAD:
    /*
    **	Argument can be a waypoint index or a direct target.
    */
    if (mission->Argument < WAYPT_COUNT) {
        Assign_Mission_Target(::As_Target((CELL)Scen.Waypoint[mission->Argument]));
    } else {
        Assign_Mission_Target((TARGET)mission->Argument);
    }
```

That `< WAYPT_COUNT` (28) test is the *whole* disambiguation between "waypoint index" and "raw target" -- teamtype missions `Move` and `Unload` with argument < 28 are waypoint references, ≥ 28 are targets. `TMISSION_MOVECELL` (`team.cpp:466`) is always a raw cell number.

Auto-reinforcement teams default to the reinforcement waypoint (`reinf.cpp:569-573`):

```c
team->MissionList[0].Mission = TMISSION_UNLOAD;
team->MissionList[0].Argument = WAYPT_REINF;
```

## The compaction behaviour

Compaction is **runtime-only, in-memory, and applies to indices 0-25 only**. `Create_Units` (`scenarioini.cpp:1526-1545`):

```c
num_waypts = 0; // counts # waypoints

for (i = 0; i < 26; i++) {
    if (Scen.Waypoint[i] != -1) {
        waypts[num_waypts] = Scen.Waypoint[i];
        num_waypts++;
    }
}

#ifndef USE_GLYPHX_START_LOCATIONS
    Sort_Cells(waypts, num_waypts, sorted_waypts);
#endif
```

with `CELL waypts[26]; CELL sorted_waypts[26]; int num_waypts;` at `scenarioini.cpp:1446-1448`.

**Consequence:** "start location index N" means *the Nth defined letter waypoint*, not waypoint N. If a map defines A, C, D (skipping B), then start-location 1 is waypoint C. `WAYPT_HOME`/`WAYPT_REINF` are excluded because the loop stops at 26.

The same compaction is repeated independently in the remaster's house assignment (`dllinterface.cpp:933-950`):

```c
int random_start_locations[26];
int num_start_locations = 0;
int num_random_start_locations = 0;
for (i = 0; i < 26; i++) {
    if (Scen.Waypoint[i] != -1) {
        preassigned = false;
        for (j = 0; !preassigned && (j < MPlayerCount); j++) {
            if (MPlayerStartLocations[j] == num_start_locations) { preassigned = true; }
        }
        if (!preassigned && i < MAX_PLAYERS) {
            random_start_locations[num_random_start_locations] = num_start_locations;
            num_random_start_locations++;
        }
        num_start_locations++;
    }
}
```

Two things to flag here:
- `if (!preassigned && i < MAX_PLAYERS)` tests the **raw waypoint letter index `i`**, not the compacted `num_start_locations`. On a map whose waypoints are, say, A,B,C,D,E,F,G,H with a gap pattern, the pool of randomly-assignable start slots is cut by letter position rather than by seat number. Looks like a bug; document it as observed behaviour if you're reimplementing.
- The shuffle at `dllinterface.cpp:952-959` uses `rand()` seeded by `srand(timeGetTime())` at `dllinterface.cpp:919` -- i.e. random start assignment is **outside** the game's deterministic `Random_Pick` stream.

The compacted index then lands in `HouseClass::StartLocationOverride` (`house.h:409`, initialised `-1` at `house.cpp:474`), assigned at `dllinterface.cpp:1003-1010`, and consumed as a direct index into the *uncompacted-order compacted array*:

```c
// scenarioini.cpp:1561-1567
#ifdef USE_GLYPHX_START_LOCATIONS
    centroid = waypts[hptr->StartLocationOverride];
```

No bounds check; `StartLocationOverride == -1` reads `waypts[-1]`.

## How `Create_Units` uses waypoints for player start positions

`Create_Units` (`scenarioini.cpp:1401`) is called only for multiplayer/2-player/skirmish and only when not in the editor (`scenarioini.cpp:566`, `608-614`):

```c
if (!Debug_Map) {
    int save_init = ScenarioInit; // turn ScenarioInit off
    ScenarioInit = 0;
    Create_Units();
    ScenarioInit = save_init;
}
```

Per-house flow (`scenarioini.cpp:1552-1601`):

```c
for (h = HOUSE_MULTI1; h < (HOUSE_MULTI1 + MPlayerMax); h++) {
    hptr = HouseClass::As_Pointer(h);
    if (!hptr) continue;

#ifdef USE_GLYPHX_START_LOCATIONS
    centroid = waypts[hptr->StartLocationOverride];
#else
    try_count = 0;
    while (true) {
        j = Random_Pick(0, MPlayerMax - 1);
        if (sorted_waypts[j] != -1) {
            centroid = sorted_waypts[j];
            sorted_waypts[j] = -1;
            break;
        }
        try_count++;
        if (try_count > 200) {
            while (true) {
                centroid = Random_Pick(0, MAP_CELL_TOTAL - 1);
                if (Map.In_Radar(centroid)) break;
            }
            break;
        }
    }
#endif
```

`USE_GLYPHX_START_LOCATIONS` is defined iff `REMASTER_BUILD` (`scenarioini.cpp:1334-1335`).

**Original (DOS/95) path:** the compacted list is run through `Sort_Cells` (`scenarioini.cpp:1954-1985`), a max-min spread: pick one waypoint at random, then repeatedly append whichever remaining waypoint is *furthest from all already-chosen ones* (`Furthest_Cell`, `scenarioini.cpp:2007-2058`, using `Distance()`). Houses then draw from `sorted_waypts[Random_Pick(0, MPlayerMax - 1)]` -- i.e. **only the first `MPlayerMax` (6) entries of the spread-sorted list are ever candidate start positions**, and each is consumed by nulling it. After 200 failed draws it degrades to any random `In_Radar` cell.

**Remaster path:** `Sort_Cells` is skipped entirely; `centroid` is the raw compacted `waypts[]` entry at the lobby-assigned index. Start positions are therefore whatever the host chose, not spread-sorted.

`centroid` is then the anchor for everything the house gets:
- MCV / MHQ unlimboed at `Cell_Coord(centroid)`, falling back to `Scan_Place_Object(obj, centroid)` (`scenarioini.cpp:1596-1601` region, `1665-1680` for the `USE_RA_AI` MCV, `Scan_Place_Object` at `scenarioini.cpp:1849`).
- Each unit/infantry *category* gets its own cluster centre `centerpt = Clip_Scatter(centroid, 4)` (`scenarioini.cpp:1710`, `1767`), i.e. up to 4 cells off the start cell in each axis, clamped to `MapCellX/Y/Width/Height` (`Clip_Scatter`, `scenarioini.cpp:2068-2133`).
- `hptr->FlagHome = 0; hptr->FlagLocation = 0;` then `Flag_Attach` to the MCV for CTF (`scenarioini.cpp:1626-1631`) -- the function header comment at `scenarioini.cpp:1344-1345` still claims it "sets each house's FlagHome & FlagLocation to the Waypoint selected as that house's home cell", which the code no longer does; it zeroes them and attaches the flag to the MCV instead.

Composition tables are at `scenarioini.cpp:1414-1424` (units) and `1428-1436` (infantry), gated by `BuildLevel`; split is `tot_units = (MPlayerUnitCount * 2) / 3` and `tot_infantry = MPlayerUnitCount - tot_units` (`scenarioini.cpp:1482`, `1503`). `MPlayerUnitCount` defaults to 10 (`globals.cpp:515`).

### Two latent defects in this path worth knowing before you port it

- `Sort_Cells` overruns its output buffer. `scenarioini.cpp:1954-1985`: `num_sorted` starts at 1, and the loop `for (i = 0; i < numcells; i++)` writes `outcells[num_sorted]` with `num_sorted` reaching `numcells`. With all 26 letter waypoints defined, it writes `sorted_waypts[26]` -- one past the end of `CELL sorted_waypts[26]` (`scenarioini.cpp:1447`).
- Infantry limit is computed from the **unit** table. `scenarioini.cpp:1472-1475`:
  ```c
  for (i = 0; i < NUM_INFANTRY_CATEGORIES; i++) {
      if (BuildLevel >= (unsigned)utable[i].MinLevel)
          i_limit = i;
  }
  ```
  `utable` should be `itable`. Both `u_limit` and `i_limit` are also uninitialised if no row qualifies.

Also note the non-remaster tail of `Read_Scenario_Ini` (`scenarioini.cpp:627-638`) **overwrites the reinforcement waypoint** after unit creation:

```c
Map.Compute_Start_Pos(start_x, start_y);
for (int i = 0; i < ARRAY_SIZE(Scen.Views); ++i) { Scen.Views[i] = XY_Cell(start_x, start_y); }
Scen.Waypoint[27] = XY_Cell(start_x, start_y);   // 27 == WAYPT_REINF
```

`Compute_Start_Pos` (`display.cpp:4140-4191`) is the centroid of the local player's units/infantry (weight 1) and buildings (weight 16). So in a non-remaster multiplayer game the map's authored `[Waypoints] 27=` is discarded and air drops land on the local player's own centre of mass.

---

# 2. `[Base]` -- the AI prebuilt base list

Section name `"Base"` (`base.h:81-84`). Global singleton `BaseClass Base;` (`globals.cpp:398`, declared `externs.h:188`). Read at `scenarioini.cpp:454` (and `848` in the split `Read_Scenario_Ini_File`), written at `scenarioini.cpp:1150`.

## Format

Documented in the header comment at `base.cpp:113-115` and implemented at `base.cpp:130-177`:

```
[Base]
Count=<n>
000=<BLDG>,<COORDINATE>
001=<BLDG>,<COORDINATE>
...
```

Reader:

```c
// base.cpp:136-144 -- House is derived, NOT stored
ini.Get_String("BASIC", "Player", "GoodGuy", buf, 20);
if (HouseTypeClass::From_Name(buf) == HOUSE_GOOD) {
    House = HOUSE_BAD;
} else {
    House = HOUSE_GOOD;
}

// base.cpp:150
int count = ini.Get_Int(INI_Name(), "Count", 0);

// base.cpp:155-177
for (int i = 0; i < count; i++) {
    sprintf(uname, "%03d", i);
    ini.Get_String(INI_Name(), uname, NULL, buf, sizeof(buf) - 1);
    node.Type  = BuildingTypeClass::From_Name(strtok(buf, ","));
    node.Coord = atol(strtok(NULL, ","));
    Nodes.Add(node);
}
```

Line-by-line meaning:

- `Count=` is **mandatory and load-bearing**. Entries are read by generated key `"%03d"` from `000` to `Count-1`, not by enumerating the section. Keys outside that window are silently ignored; a `Count` larger than the real entry set yields nodes with garbage from `strtok(NULL)` on an empty buffer.
- Key is **zero-padded to 3 digits** (`base.cpp:160`, `base.cpp:221`). This differs from `[Waypoints]`, which is unpadded.
- First field: building INI name (`BuildingTypeClass::From_Name`), e.g. `NUKE`, `WEAP`, `HAND`.
- Second field: a **`COORDINATE`, not a cell number** -- `atol` of the packed 32-bit lepton coord (`Y<<16 | X`, high byte of each half = cell index, low byte = sub-cell lepton; see `Coord_Cell`, `coord.cpp:62-69`). This is the one place in the scenario INI where positions are coords rather than cells; `[Waypoints]`, `[CellTriggers]` and the object sections all use cell numbers.
- **List order is significant** -- it is the build order. The writer comment says so explicitly (`base.cpp:210-213`): *"they must be read in the same order they were created, so `000` must be read first"*.
- **Only one base per scenario.** Both `Read_INI` and `Write_INI` carry the warning *"This routines assumes there is only one base defined for the scenario"* (`base.cpp:125`, `base.cpp:194`).
- The owning house is **not in the section**. It is inferred as "the side opposite `[BASIC] Player`" (`base.cpp:136-144`). If `Player=GoodGuy` the base is `BadGuy`; anything else (including `Multi*`) yields `GoodGuy`. The map editor can override `Base.House` to `HOUSE_MULTI4` for multiplayer scenarios (`mapeddlg.cpp:130`, `222`, `325`) but that choice is not persisted in `[Base]`.

Writer (`base.cpp:200-224`) round-trips exactly:

```c
ini.Put_Int(INI_Name(), "Count", Nodes.Count());
for (int i = 0; i < Nodes.Count(); i++) {
    sprintf(uname, "%03d", i);
    sprintf(buf, "%s,%d", BuildingTypeClass::As_Reference(Nodes[i].Type).IniName, Nodes[i].Coord);
    ini.Put_String(INI_Name(), uname, buf);
}
```

## How the AI uses it to rebuild

The "is this node built?" test is **positional and type-exact**, not a stored flag (`base.cpp:353-405`):

```c
bool BaseClass::Is_Built(int index) { return (Get_Building(index) != NULL); }

BuildingClass* BaseClass::Get_Building(int index)
{
    CELL cell = Coord_Cell(Nodes[index].Coord);
    obj[0] = Map[cell].Cell_Building();
    ... plus Map[cell].Overlapper[] ...
    for (int i = 0; i < count; i++) {
        if (obj[i] && obj[i]->Coord == Nodes[index].Coord && obj[i]->What_Am_I() == RTTI_BUILDING
            && ((BuildingClass*)obj[i])->Class->Type == Nodes[index].Type) { bldg = ...; break; }
    }
}
```

A node counts as built iff a building of *exactly* that type sits at *exactly* that COORDINATE. This is why the base list and the `[STRUCTURES]` section overlap: the header comment at `base.h:119-122` spells it out -- *"Portions of this list can be pre-built by simply saving those buildings in the INI along with non-base buildings, so Is_Built will return true for them."* The list is the **full intended base**; whichever entries also appear in `[STRUCTURES]` start already standing, and the rest are the rebuild queue.

The queue pointer (`base.cpp:475-500`):

```c
BaseNodeClass* BaseClass::Next_Buildable(StructType type)
{
    for (int i = 0; i < Nodes.Count(); i++) {
        if (type == STRUCT_NONE) {
            if (!Is_Built(i)) return (&Nodes[i]);
        } else {
            if (Nodes[i].Type == type && !Is_Built(i)) return (&Nodes[i]);
        }
    }
```

First hole in list order wins. Because "hole" is recomputed from the map every call, destroying a base building automatically reopens its slot -- that is the entire rebuild mechanism.

### Important: in this tree the runtime consumer is effectively dead code

`Next_Buildable` has exactly two call sites:

- `house.cpp:6100-6106`, inside `HouseClass::AI_Building` -- wrapped in `#if (0) // Not used for GAME_NORMAL in C&C. ST - 7/23/2019 3:04PM`:
  ```c
  #if (0)
  if (Session.Type == GAME_NORMAL && Base.House == Class->House) {
      BaseNodeClass* node = Base.Next_Buildable();
      if (node) { BuildStructure = node->Type; }
  }
  #endif
  ```
- `house.cpp:3773`, inside `HouseClass::Suggest_New_Object` -- and **`Suggest_New_Object` has no caller anywhere in `tiberiandawn/`** (repo-wide grep across `brain` finds callers only in `vanilla/redalert/building.cpp:5707`). Declared `house.h:504`, defined `house.cpp:3492`.

`Create_Units`' own comment agrees (`scenarioini.cpp:1548-1550`): *"Computer-controlled houses, with MPlayerBases ON, are treated as though bases are OFF (since we have no base-building AI logic.)"*

The live consumers of `[Base]` in this tree are:
- the **map editor**: `MapEditClass::Build_Base_To(percent)` (`mapedplc.cpp:2010-2054`) tears down and re-unlimbos the first `Nodes.Count() * percent / 100` nodes at their exact coords; node insertion at `mapedplc.cpp:1220-1225`; node deletion at `mapedit.cpp:1170-1172`; drag-move keeps the node in sync at `mapedsel.cpp:511-512`; you cannot change a base node's house (`mapedsel.cpp:573-576`).
- the **GDI mission 7 sabotage carry-over**, which surgically deletes a node so the AI never re-erects the sabotaged structure -- duplicated at `scenario.cpp:592-600` and `dllinterface.cpp:1124-1131`.
- **save/load**: `base.cpp:243` / `base.cpp:299`, called from `saveload.cpp:475` / `saveload.cpp:244`.

So: for a faithful port, treat `[Base]` as *data the map author writes and the shipped AI mostly ignores*. If you resurrect base rebuilding, `Next_Buildable` + `Is_Built` is the complete contract; note that placement would come from `HouseClass::Find_Build_Location` (`house.cpp:5019`), which is **zone-heuristic and does not use the node's stored coord** -- only the editor's `Build_Base_To` honours the exact coordinate.

---

# 3. How many players a multiplayer map can seat

**There is no per-map player-count field anywhere in the TD scenario INI.** The seat count is derived entirely from the waypoint table at load time.

The derivation, in three clamps:

1. **Candidate pool = count of non-`-1` entries in `Scen.Waypoint[0..25]`.**
   `scenarioini.cpp:1531-1538` (`num_waypts`), mirrored at `dllinterface.cpp:934-949` (`num_start_locations`). `WAYPT_HOME` (26) and `WAYPT_REINF` (27) are excluded because both loops are bounded by the literal `26`, not `WAYPT_COUNT`.

2. **Hard ceiling of 6.**
   ```c
   // defines.h:2562-2565
   /****************************************************************************
   **	Max # of players total, including the game's owner
   */
   #define MAX_PLAYERS 6
   ```
   ```c
   // globals.cpp:499
   int MPlayerMax = 6;
   ```
   `MPlayerMax` is assigned in only one other place in the whole tree -- `options.cpp:695-698`, an obfuscated `CONQUER.INI` cheat key that sets it to 6 again:
   ```c
   ini.Get_String(OPTIONS, "Players", "", workbuf, sizeof(workbuf));
   if (Obfuscate(workbuf) == PARM_6PLAYER) { MPlayerMax = 6; }
   ```
   Note that key lives in the **options file, not the scenario**.

3. **The consumption loop.** `Create_Units` iterates `for (h = HOUSE_MULTI1; h < (HOUSE_MULTI1 + MPlayerMax); h++)` (`scenarioini.cpp:1552`), and in the original path draws start cells only from the first `MPlayerMax` slots of the spread-sorted array: `j = Random_Pick(0, MPlayerMax - 1);` (`scenarioini.cpp:1580`). Houses `HOUSE_MULTI1..HOUSE_MULTI6` are the only multiplayer slots (`Assign_Houses`, `scenarioini.cpp:1196-1290`, all loops bounded by `MAX_PLAYERS`).

**Effective seat count = `min(count of defined waypoints in 0..25, 6)`.**

Corollaries a map tool needs to respect:
- A map with 8 lettered waypoints still seats 6; the extra two are inert in the original path (never drawn from `sorted_waypts[0..5]`) and reachable in the remaster path only if the host hands out `StartLocationIndex` ≥ 6.
- A map with 3 waypoints and 6 houses active: the original path retries `Random_Pick` up to 200 times and then dumps the house at a random `In_Radar` cell (`scenarioini.cpp:1590-1599`); the remaster path indexes `waypts[]` past `num_waypts` and reads stack garbage (`scenarioini.cpp:1567`).
- The remaster's `max_players` is an **input parameter from the host**, not a map property: `CNC_Set_Multiplayer_Data(..., int max_players)` at `dllinterface.cpp:695-699`, validated `if (num_players > min(MAX_PLAYERS, max_players)) return false;` (`dllinterface.cpp:706`). Per-player seat choice arrives as `player_info.StartLocationIndex` → `MPlayerStartLocations[i]` (`dllinterface.cpp:760`), with sentinel `#define RANDOM_START_POSITION 0x7f` (`dllinterface.cpp:94`) and a custom-map fallback at `dllinterface.cpp:762-767`:
  ```c
  /*
  ** Temp fix for custom maps that don't have valid start positions set from matchmaking
  */
  if (i > 0 && MPlayerStartLocations[i] == 0 && MPlayerStartLocations[0] == 0) {
      MPlayerStartLocations[i] = i;
  }
  ```
- The map editor exposes only 4 CTF flag homes (ALT+1..ALT+4 → `HOUSE_MULTI1 + n`, `mapedit.cpp:986-1013`), and `nulldlg.cpp:397` uses `int maxp = 4 /*Rule.MaxPlayers - 2*/;` -- historical evidence the shipped ceiling was 4 for some UI paths even though the engine allows 6.

---

# 4. Other INI sections that affect mission scripting

Beyond `[Triggers]` and `[TeamTypes]`, these carry scripting-relevant state. Read order matters and is fixed in `Read_Scenario_Ini` (`scenarioini.cpp:208-467`): TeamTypes → Houses → Triggers → Map → objects → Base → Overlay → Smudge. `TriggerClass::Read_INI`'s header warns *"This function must be called before any other class's Read_INI"* (`trigger.cpp:1016`) because objects resolve trigger references by name.

### `[Basic]`

Case-insensitive; note `BaseClass::Read_INI` spells it `"BASIC"` (`base.cpp:140`) while everything else uses `"Basic"`.

| Key | Effect | Line |
|---|---|---|
| `Intro`, `Brief`, `Win`, `Win2`, `Win3`, `Win4`, `Lose`, `Action` | VQA movie names. Default `"x"` = none. `Win2/3/4` are the branching-outcome variants. | `scenarioini.cpp:288-295` |
| `Player` | Sets `PlayerPtr` and `IsHuman` in single-player (`scenarioini.cpp:368-369`); **also silently determines `[Base]`'s owning house** (`base.cpp:140-144`). | `scenarioini.cpp:368` |
| `Theme` | `Scen.TransitTheme` -- score that starts on the action movie and continues into play. | `scenarioini.cpp:329` |
| `BuildLevel` | Tech level. In `GAME_NORMAL` it is normally forced to the scenario number for `Scenario <= 15`; only above 15 does the INI key get read, defaulting to the scenario number. Hardcoded override to 15 for `scg30ea` / `scg90ea` / `scb22ea` ("N64 missions"). `Special.IsJurassic` forces 98. Also gates the `Create_Units` composition tables. | `scenarioini.cpp:305-320` |
| `CarryOverMoney` | Percent (0-100) of previous scenario's leftover cash, run through `Cardinal_To_Fixed`. Default 100. | `scenarioini.cpp:365-366` |
| `CarryOverCap` | Absolute cap on carry-over; `-1` = uncapped. | `scenarioini.cpp:367`, applied `372-376` |

### House sections -- `[GoodGuy]`, `[BadGuy]`, `[Neutral]`, `[Special]`, `[Multi1]`..`[Multi6]`

`HouseClass::Read_INI`, `house.cpp:1901-1950`. **Every house in `HOUSE_FIRST..HOUSE_COUNT` is constructed whether or not its section exists.**

| Key | Effect |
|---|---|
| `Credits` | Starting money, **multiplied by 100** (`house.cpp:1918`). |
| `MaxUnit` / `MaxBuilding` | Caps; both floored at 150 by `MAX(maxunit, 150)` (`house.cpp:1908`, `1913`), so values below 150 in the INI are ignored. Defaults `EACH_UNIT_MAX` / `EACH_BUILDING_MAX` = heap/4 (`defines.h:2034-2035`). |
| **`Edge`** | `SourceType` -- **the map edge this house's ground reinforcements walk in from.** Default `SOURCE_NORTH`. `house.cpp:1924`. This is the piece triggers/teamtypes recon most often misses: a `Reinforcement` trigger action names a teamtype, but *where it enters* comes from the house's `Edge=`. Enum at `defines.h:2507-2522`: `SOURCE_NORTH/EAST/SOUTH/WEST`, plus `SOURCE_AIR` (uses `WAYPT_REINF`), `SOURCE_VISIBLE`, `SOURCE_ENEMYBASE`, `SOURCE_HOMEBASE`, `SOURCE_OCEAN`. Edge selection logic in `DisplayClass::Calculated_Cell`, `display.cpp:~2790-2870`. |
| `Allies` | Comma/space/tab-separated house list. **Single-player only** (`if (GameToPlay == GAME_NORMAL)`, `house.cpp:1926`). If absent, `HOUSE_GOOD` alone defaults to allying `HOUSE_NEUTRAL` (`house.cpp:1943-1946`) -- so civilians are hostile to Nod by default and friendly to GDI. |

### `[MAP]`

`DisplayClass::Read_INI`, `display.cpp:1276-1298`. `X`, `Y`, `Width`, `Height` (defaults `1,1,62,62`), `Theater` (default `THEATER_TEMPERATE`, with `THEATER_NONE → THEATER_DESERT` fixup), and `Version` (MEGAMAPS only, `0` = normal 64x64 cell numbering, `1` = mega; bounded 0..1). The playable rectangle bounds `Clip_Scatter` (`scenarioini.cpp:2094-2098`) and edge-reinforcement scans, and clamps `WAYPT_HOME` in the editor (`mapeddlg.cpp:1544-1558`).

### `[CellTriggers]`

`display.cpp:1360-1387`. `<cellnumber>=<triggername>`. Attaches a trigger to a map cell and sets `CellClass::IsTrigger`; guarded by `cell > 0 && cell < MAP_CELL_TOTAL && !(*this)[cell].IsTrigger` -- so **cell 0 can never hold a cell trigger** and duplicates are dropped. Bumps `AttachCount`. This is the only place cell triggers are created; the `[Triggers]` section alone tells you nothing about *where* an `EVENT_PLAYER_ENTERED` fires.

### Object sections carry trigger attachments

`[UNITS]`, `[INFANTRY]`, `[STRUCTURES]`, `[TERRAIN]` each carry a trigger-name field on the object line. `TriggerClass::Read_INI`'s header enumerates the owners (`trigger.cpp:999-1005`):

> *"Object's pointers are set in: `InfantryClass::Read_INI()` / `BuildingClass::Read_INI()` / `UnitClass::Read_INI()` / `TerrainClass::Read_INI()`"*

Section names from `INI_Name()`: `UNITS` (`unit.h`), `INFANTRY` (`infantry.h`), `STRUCTURES` (`building.h`), `AIRCRAFT` (`aircraft.h`), `TERRAIN` (`terrain.h`), `OVERLAY` (`overlay.h`), `SMUDGE` (`smudge.h`), `TEMPLATE` (`template.h`), `Triggers` (`trigger.h`), `TeamTypes` (`teamtype.h`), `Base` (`base.h`).

**Gap to flag:** `UnitClass::Read_INI`, `InfantryClass::Read_INI`, `BuildingClass::Read_INI` and `AircraftClass::Read_INI` are *declared* (`unit.h:196`, `infantry.h:226`, `building.h:306`, `aircraft.h:190`) and *called* (`scenarioini.cpp:433`, `436`, `442`, `448`) but **have no definition anywhere in `brain/vanilla/tiberiandawn/`**. Repo-wide grep for `UnitClass::Read_INI` finds only the call sites, a comment in `trigger.cpp`, and the Red Alert copies (`vanilla/redalert/unit.cpp`). If CNC3D links this tree, those four are being supplied or stubbed elsewhere -- worth an entry in `./known-gap notes`. The exact TD field order for those lines cannot be cited from this tree.

Sections whose readers *are* present, for format reference: `TerrainClass::Read_INI` (`terrain.cpp:795`), `OverlayClass::Read_INI` (`overlay.cpp:342`), `SmudgeClass::Read_INI` (`smudge.cpp:273`), `TemplateClass::Read_INI` (`template.cpp:98`).

### `[TEMPLATE]` is a fallback only

`scenarioini.cpp:417-421`: the terrain tile map comes from the **binary `.BIN`** file; `TemplateClass::Read_INI` runs only `if (!Map.Read_Binary(root, &ScenarioCRC))`. Both feed `ScenarioCRC`, which is the multiplayer sync ID (`ini.Get_Unique_ID()`, `scenarioini.cpp:280`).

### `[Briefing]` + the external `MISSION.INI`

`scenarioini.cpp:472-483`:

```c
ini.Get_TextBlock("Briefing", Scen.BriefingText, sizeof(Scen.BriefingText));
if (Scen.BriefingText[0] == '\0') {
    INIClass mini;
    CCFileClass missionIniFile("MISSION.INI");
    mini.Load(missionIniFile);
    mini.Get_TextBlock(root, Scen.BriefingText, sizeof(Scen.BriefingText));
}
```

A text block, not key=value. Capped at 512 bytes (`scenario.h:~90`, `char BriefingText[512]`). **If absent, the engine falls back to a section named after the scenario root inside a global `MISSION.INI`** -- so briefing text for stock missions may not be in the map file at all.

### Hardcoded per-scenario script patches (no INI section at all)

`scenarioini.cpp:492-551` mutates scripting state by scenario filename after the INI is parsed. Any tool that reasons only from INI content will get these wrong:

- `scb07ea`: `Map[(CELL)2795].Override_Land_Type(LAND_ROCK);`
- `scb09ea`: attaches trigger `"dely"` to the first GDI `STRUCT_RADAR` found -- **a trigger attachment that exists in no section**.
- `scb10eb`: `Map[(CELL)2015].Override_Land_Type(LAND_ROCK);`
- `scb13eb`: **constructs a trigger at runtime** -- `EVENT_PLAYER_ENTERED` / `ACTION_BEGIN_PRODUCTION` / `HOUSE_BAD`, named `"prod"`, wired into `CellTriggers[276]`, `[340]`, `[404]`, `[468]`; and forces the existing trigger `"xxxx"` to `PERSISTANT`.
- `scb13ec`: attaches trigger `"delx"` to a GDI radar.

Then `Fixup_Scenario()` (`scenarioini.cpp:556`, `scenario.cpp:814-854`, also re-run on save-load at `saveload.cpp:493`) mutates **type-class data** by scenario identity: `IsAvoidingTiberium` on all fraidycat civilians for `scg08eb`; Orca primary weapon → `WEAPON_OBELISK_LASER` for scenario 72 (PATSUX); and the `Special.ModernBalance` deltas (Weapons Factory `MaxStrength` 400↔520, APC prerequisite `STRUCTF_REPAIR`).

### Multiplayer options arrive from outside the map

`MPlayerBases`, `MPlayerCredits`, `MPlayerTiberium`, `MPlayerGoodies`, `MPlayerGhosts`, `MPlayerUnitCount`, `Special.IsCaptureTheFlag`, `Special.IsMCVDeploy`, `Special.IsEarlyWin`, `Rule.AllowSuperWeapons` are all set by `CNC_Set_Multiplayer_Data` (`dllinterface.cpp:718-740`), not by the scenario INI -- but they change what `Create_Units` produces (MCV vs. unit pile, MHQ, crate count) at `scenarioini.cpp:1607-1700`.

---

## Quick reference: file:line index

| Topic | Location |
|---|---|
| `WAYPT_HOME` / `WAYPT_REINF` / `WAYPT_COUNT` | `defines.h:2551-2560` |
| `MAX_PLAYERS 6` | `defines.h:2565` |
| `CELL Waypoint[WAYPT_COUNT]` | `scenario.h:69` |
| Waypoint INI read | `display.cpp:1329-1346` |
| Waypoint INI write | `display.cpp:1438-1447` |
| Home-cell fallback + view seeding | `display.cpp:1351-1357` |
| `SOURCE_AIR` → `WAYPT_REINF` | `display.cpp:2836-2853` |
| `ACTION_DZ` → `Waypoint[25]` | `trigger.cpp:468`, `658`, `856` |
| Team mission waypoint-vs-target test | `team.cpp:470-481` |
| Reinforcement default `WAYPT_REINF` | `reinf.cpp:572` |
| `Create_Units` | `scenarioini.cpp:1401-1826` |
| Waypoint compaction (0-25) | `scenarioini.cpp:1531-1538` |
| `Sort_Cells` / `Furthest_Cell` / `Clip_Scatter` | `scenarioini.cpp:1954`, `2007`, `2068` |
| Start-position pick (both paths) | `scenarioini.cpp:1561-1601` |
| Remaster start-location shuffle | `dllinterface.cpp:919-1010` |
| `Waypoint[27]` overwritten by start pos | `scenarioini.cpp:627-638` |
| `BaseClass` / `BaseNodeClass` | `base.h:40-127` |
| `[Base]` read / write | `base.cpp:130-177` / `base.cpp:200-224` |
| `Is_Built` / `Get_Building` / `Next_Buildable` | `base.cpp:353`, `377`, `475` |
| `[Base]` consumers (mostly disabled) | `house.cpp:3773`, `house.cpp:6100` (`#if (0)`) |
| Editor base build-to-percent | `mapedplc.cpp:2010-2054` |
| `Read_Scenario_Ini` (section order) | `scenarioini.cpp:208-467` |
| `HouseClass::Read_INI` (`Edge`, `Allies`) | `house.cpp:1901-1950` |
| `[MAP]` | `display.cpp:1276-1298` |
| `[CellTriggers]` | `display.cpp:1360-1387` |
| `[Briefing]` + `MISSION.INI` | `scenarioini.cpp:472-483` |
| Hardcoded scenario patches | `scenarioini.cpp:492-551` |
| `Fixup_Scenario` | `scenario.cpp:814-854` |