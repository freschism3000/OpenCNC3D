All paths below are relative to `brain/vanilla/tiberiandawn/`.

# TEAMTYPES -- complete specification (Tiberian Dawn, EA GPL source)

## 0. Where it lives / load order

| Thing | Location |
|---|---|
| Type declaration | `teamtype.h:82-268` |
| Type implementation | `teamtype.cpp` |
| Runtime instance declaration | `team.h:49-274` |
| Runtime instance implementation | `team.cpp` |
| Heaps | `init.cpp:139` `TeamTypes.Set_Heap(TEAMTYPE_MAX)`, `init.cpp:141` `Teams.Set_Heap(TEAM_MAX)` |
| Caps | `defines.h:2018` `#define TEAM_MAX 60`, `defines.h:2023` `#define TEAMTYPE_MAX 60` |
| Ticked | `logic.cpp:194-197` -- `for (index…Teams.Count()) Teams.Ptr(index)->AI();` once per frame, before object AI |

**Load order is load-bearing.** `scenarioini.cpp:335` calls `TeamTypeClass::Read_INI(ini)` *before* `TriggerClass::Read_INI(ini)` at `scenarioini.cpp:349`, because a trigger resolves its team by name (`trigger.cpp:1122`). Comment at `scenarioini.cpp:332-334`: "The team types must be created before any triggers can be created." Same ordering in the custom-map path (`scenarioini.cpp:744` then `:758`).

Section name comes from `teamtype.h:111-114`:
```cpp
static const char* INI_Name(void) { return "TeamTypes"; };
```

---

## 1. The `[TeamTypes]` INI line format, field by field

Canonical comment, `teamtype.cpp:158-163` (repeated at `:213-216` and `:381-384`):
```
 * INI entry format:                                                       *
 *      TeamName = Housename,Roundabout,Learning,Suicide,Spy,Mercenary,    *
 *       RecruitPriority,MaxAllowed,InitNum,Fear,                          *
 *       ClassCount,Class:Num,Class:Num,...,                               *
 *       MissionCount,Mission:Arg,Mission:Arg,Mission:Arg,...              *
```
The comment is **incomplete**: two trailing flags follow (`teamtype.cpp:367-374` on read, `:472-481` on write). All 996 team entries in the shipped DOS scenario set carry them.

Parser is `TeamTypeClass::Fill_In(name, entry)` at `teamtype.cpp:231-375`, driven by `strtok` with delimiter `","` (and `",:"` inside the class/mission lists).

### Key (left of `=`)
`teamtype.cpp:250` `Set_Name(name)`. Stored in `AbstractTypeClass::IniName[9]` (`type.h:243`), i.e. **max 8 chars, silently truncated** (`type.h:268-272` uses `strncpy` + forced NUL). Lookup by name is case-insensitive (`teamtype.cpp:666` `stricmp`).

### Value fields, in order

| # | Field | Parse site | Type / meaning |
|---|---|---|---|
| 1 | `Housename` | `teamtype.cpp:255` | `HouseTypeClass::From_Name()`. Valid: `GoodGuy`, `BadGuy`, `Neutral`, `Special`, `Multi1`…`Multi6` (`hdata.cpp:50,62,74,86,98`). Unknown name yields `HOUSE_NONE` (`hdata.cpp:253-263`), which is legal but makes the team un-ownable. Shipped campaigns use only `GoodGuy`, `BadGuy`, `Neutral`. |
| 2 | `Roundabout` | `teamtype.cpp:260` | `atoi` into 1-bit `IsRoundAbout`. |
| 3 | `Learning` | `teamtype.cpp:265` | `atoi` into `IsLearning`. **Dead**: no read site anywhere outside `teamtype.cpp` / `mapedtm.cpp`. |
| 4 | `Suicide` | `teamtype.cpp:270` | `atoi` into `IsSuicide`. |
| 5 | `Spy` | `teamtype.cpp:275` | **Misnomer.** Parsed into `IsAutocreate`. The INI comment says "Spy"; the code says `IsAutocreate = atoi(strtok(NULL, ","));` and `Write_INI` writes `team->IsAutocreate` back into that slot (`teamtype.cpp:446`). Editor labels it "Autocreate" (`mapedtm.cpp:906`). |
| 6 | `Mercenary` | `teamtype.cpp:280` | `atoi` into `IsMercenary`. **Dead**: never read. Header comment (`teamtype.h:189-191`) claims "Mercenaries will change sides if they start to lose" -- unimplemented. |
| 7 | `RecruitPriority` | `teamtype.cpp:285` | `int`. Header says scale 0-15 (`teamtype.h:215-217`); shipped data uses 4…30. Live. |
| 8 | `MaxAllowed` | `teamtype.cpp:290` | `unsigned char`. Live (see §5). |
| 9 | `InitNum` | `teamtype.cpp:295` | `unsigned char`. **Dead in TD**: read, stored, written back, exposed in the editor, never consulted by game logic. (It is live in Red Alert; do not port RA behaviour by assumption.) |
| 10 | `Fear` | `teamtype.cpp:300` | `unsigned char`. **Dead in TD**: `TeamTypeClass::Fear` has no reader. (`InfantryClass::Fear` at `infantry.h:110` is a different, unrelated field.) All shipped data writes 0. |
| 11 | `ClassCount` | `teamtype.cpp:305` | `int num_classes`. Count of the `Class:Num` pairs that follow. |
| 12..11+N | `Class:Num` × ClassCount | `teamtype.cpp:310-350` | See §3. |
| 12+N | `MissionCount` | `teamtype.cpp:355` | Count of the `Mission:Arg` pairs that follow. |
| … | `Mission:Arg` × MissionCount | `teamtype.cpp:359-365` | See §2. |
| penultimate | `IsReinforcable` | `teamtype.cpp:367-370` | Optional; if the token is absent the constructor default `true` stands. |
| last | `IsPrebuilt` | `teamtype.cpp:371-374` | Optional; constructor default `true` stands if absent. |

### Hard limits
- **Entry value length ≤ 255 chars.** `Fill_In` does `char buf[INIClass::MAX_LINE_LENGTH]; strcpy(buf, entry);` (`teamtype.cpp:243,245`) with `MAX_LINE_LENGTH = 256` (`common/ini.h:204`). Longer values are a stack smash, not a rejection. Longest shipped entry is ~205 chars.
- **ClassCount ≤ 5** (`MAX_TEAM_CLASSCOUNT`, `teamtype.h:87`), enforced defensively at `teamtype.cpp:344` -- extra pairs are parsed and *silently discarded*, not an error.
- **MissionCount ≤ 20** (`MAX_TEAM_MISSIONS`, `teamtype.h:88`), **not enforced**. `teamtype.cpp:359-365` writes `MissionList[i]` for `i < MissionCount` with no bounds check. A MissionCount above 20 overruns the struct. Shipped data maxes at exactly 20 (`SCB04EA.INI` `hummer1`).
- Team-type count ≤ 60 (`TEAMTYPE_MAX`); `operator new` at `teamtype.cpp:780-787` returns NULL past that and `Read_INI` (`teamtype.cpp:188-193`) skips silently.

### Defaults (constructor, `teamtype.cpp:114-136`)
```cpp
IsPrebuilt = true;  IsReinforcable = true;  IsRoundAbout = false;
IsLearning = false; IsSuicide = false;      IsAutocreate = false;
IsTransient = false; IsMercenary = false;
RecruitPriority = 7; MaxAllowed = 0; Fear = 0; InitNum = 0;
House = HOUSE_NONE; MissionCount = 0; ClassCount = 0;
```

### Write path
`TeamTypeClass::Write_INI` at `teamtype.cpp:398-485`. Note `ini.Clear("Teams")` at `:409` -- it wipes the legacy section name. The `sprintf` format string at `:440-452` is the authoritative field order for the first eleven fields.

### Legacy format (`Read_Old_INI`, `teamtype.cpp:507-640`) -- dead code
Documented format at `:491-492`:
`TeamName = Housename,Roundabout,Learning,Suicide,Spy,Mercenary,RecruitPriority,MaxAllowed,InitNum,Class:Num,...,Fear`
(mission name in slot 10 is read and thrown away, `:585`). It is unreachable: `Read_INI` calls it only when `TeamTypes.Count() == 0` (`teamtype.cpp:199-201`), but it then iterates `ini.Entry_Count(INI_Name())` -- the same `"TeamTypes"` section that was just proven empty (`:511`), while reading values from `"Teams"` (`:535`). Loop count is always zero. **Do not implement it.**

### Worked examples (real shipped data, `tools/bakery/sharecopy/assets/extracted/INI/`)
```
SCG09EA.INI
auto8=BadGuy,1,0,0,1,0,15,1,0,0,2,E4:2,BGGY:1,5,Move:1,Move:2,Move:3,Guard:4,Loop:3,0,0
```
House=BadGuy, RoundAbout=1, Learning=0, Suicide=0, **Autocreate=1**, Mercenary=0, RecruitPriority=15, MaxAllowed=1, InitNum=0, Fear=0, ClassCount=2 → {E4×2, BGGY×1}, MissionCount=5 → Move(wp1), Move(wp2), Move(wp3), Guard(4), Loop(→index 3), IsReinforcable=0, IsPrebuilt=0.

```
SCG12EB.INI
gdi1=GoodGuy,1,0,0,0,0,7,0,0,0,2,E2:4,APC:1,6,Move:0,Move:1,Move:4,Unload:4,Guard:2,Loop:4,0,0
```
An APC carrying 4 minigunners: drive wp A, wp B, wp E, unload at wp E, hold 12 s, loop back to mission index 4.

---

## 2. Team missions

### The enum, quoted verbatim (`teamtype.h:41-62`)

```cpp
/*
**	TeamMissionType: the various missions that a team can have.
*/
typedef enum TeamMissionType : signed char
{
    TMISSION_NONE = -1,
    TMISSION_ATTACKBASE,      // Attack nearest enemy base.
    TMISSION_ATTACKUNITS,     // Attack all enemy units.
    TMISSION_ATTACKCIVILIANS, // Attack all civilians
    TMISSION_RAMPAGE,         // attack & destroy anything that's not mine
    TMISSION_DEFENDBASE,      // Protect my base.
                              //	TMISSION_HARVEST,					// stake out a Tiberium claim, defend & harvest it
    TMISSION_MOVE,            // moves to waypoint specified.
    TMISSION_MOVECELL,        // moves to cell # specified.
    TMISSION_RETREAT,         // order given by superior team, for coordinating
    TMISSION_GUARD,           // works like an infantry's guard mission
    TMISSION_LOOP,            // loop back to start of mission list
    TMISSION_ATTACKTARCOM,    // attack tarcom
    TMISSION_UNLOAD,          // Unload at current location.
    TMISSION_COUNT,
    TMISSION_FIRST = 0
} TeamMissionType;
```

`TMISSION_HARVEST` is commented out of the enum, so **enum values are contiguous 0..11** with no gap. Its handler is also commented out at `team.cpp:558-560`.

And the struct (`teamtype.h:70-77`):
```cpp
typedef struct TeamMissionTag
{
    TeamMissionType Mission;
    int Argument;
} TeamMissionStruct;
```

### The INI name table, quoted verbatim (`teamtype.cpp:55-69`)

```cpp
char const* TeamTypeClass::TMissions[TMISSION_COUNT] = {
    "Attack Base",
    "Attack Units",
    "Attack Civil.",
    "Rampage",
    "Defend Base",
    //	"Harvest",
    "Move",
    "Move to Cell",
    "Retreat",
    "Guard",
    "Loop",
    "Attack Tarcom",
    "Unload",
};
```
Matched case-insensitively by `Mission_From_Name` (`teamtype.cpp:726-739`), returning `TMISSION_NONE` on no match. Names contain spaces and, for `Attack Civil.`, a trailing period -- they must be reproduced exactly. Because `strtok` splits on `,` and `:` only, embedded spaces survive intact.

### Argument semantics

Argument decoding happens once, at mission-advance time, `team.cpp:461-490`:

```cpp
TeamMissionStruct const* mission = &Class->MissionList[CurrentMission];
TimeOut = mission->Argument * (TICKS_PER_MINUTE / 10);
Target = TARGET_NONE;
switch (mission->Mission) {
case TMISSION_MOVECELL:
    Assign_Mission_Target(::As_Target((CELL)mission->Argument));
    break;
case TMISSION_MOVE:
case TMISSION_UNLOAD:
    if (mission->Argument < WAYPT_COUNT) {
        Assign_Mission_Target(::As_Target((CELL)Scen.Waypoint[mission->Argument]));
    } else {
        Assign_Mission_Target((TARGET)mission->Argument);
    }
    break;
case TMISSION_ATTACKTARCOM:
    Assign_Mission_Target(mission->Argument);
    break;
default:
    Assign_Mission_Target(TARGET_NONE);
    break;
}
```

Note **`TimeOut` is set for every mission unconditionally** (`team.cpp:464`) even when the argument is a waypoint or a cell. `TICKS_PER_MINUTE = 900` (`defines.h:2219-2220`: `TICKS_PER_SECOND 15`), so `TICKS_PER_MINUTE / 10 == 90 ticks == 6 seconds`. **Argument unit for timing missions is 6 seconds.**

| Mission | INI name | Argument means | Executed by | Times out? |
|---|---|---|---|---|
| `TMISSION_ATTACKBASE` (0) | `Attack Base` | duration, ×6 s | `team.cpp:517-524` → `Coordinate_Attack()` | yes (`:589`) |
| `TMISSION_ATTACKUNITS` (1) | `Attack Units` | duration, ×6 s | `team.cpp:526-533` | yes (`:590`) |
| `TMISSION_ATTACKCIVILIANS` (2) | `Attack Civil.` | duration, ×6 s | `team.cpp:535-542` | yes (`:591`) |
| `TMISSION_RAMPAGE` (3) | `Rampage` | duration, ×6 s | `team.cpp:544-552` (shares the `ATTACKTARCOM` case) | yes (`:592`) |
| `TMISSION_DEFENDBASE` (4) | `Defend Base` | duration, ×6 s | `team.cpp:554-556` → `Coordinate_Move()` | yes (`:593`) |
| `TMISSION_MOVE` (5) | `Move` | **waypoint index if `< 28`, else raw TARGET** | `team.cpp:566-568` → `Coordinate_Move()` | **no** (advances on arrival) |
| `TMISSION_MOVECELL` (6) | `Move to Cell` | raw `CELL` number | `team.cpp` default case; no execution case, so it falls through to `Coordinate_Regroup()` at `:603-609` only when the team is not fully assembled. In practice the target is set and `Coordinate_Move` is never invoked for it. | **no**, and no execution case either -- see quirks |
| `TMISSION_RETREAT` (7) | `Retreat` | duration, ×6 s | `team.cpp:570-572` → `Coordinate_Move()` | yes (`:595`) |
| `TMISSION_GUARD` (8) | `Guard` | duration, ×6 s | `team.cpp:574-576` → `Coordinate_Regroup()` | yes (`:596`) |
| `TMISSION_LOOP` (9) | `Loop` | **destination mission index (0-based)** | `team.cpp:578-581` | n/a |
| `TMISSION_ATTACKTARCOM` (10) | `Attack Tarcom` | **raw TARGET value** | `team.cpp:544-552` | **no** (absent from the timeout switch) |
| `TMISSION_UNLOAD` (11) | `Unload` | **waypoint index if `< 28`, else raw TARGET, AND simultaneously a timeout** | `team.cpp:562-564` → `Coordinate_Unload()` | yes (`:594`) |

Target-acquisition detail for the four attack missions: when `MissionTarget` is illegal the team asks its captain for a threat and, if none is found, immediately advances (`team.cpp:518-522` etc.):
- `ATTACKBASE` → `Member->Greatest_Threat(THREAT_BUILDINGS)`
- `ATTACKUNITS` → `THREAT_VEHICLES | THREAT_INFANTRY`
- `ATTACKCIVILIANS` → `THREAT_CIVILIANS`
- `ATTACKTARCOM` / `RAMPAGE` → `THREAT_NORMAL`

`Loop` implementation (`team.cpp:578-581`):
```cpp
case TMISSION_LOOP:
    CurrentMission = mission->Argument - 1;
    IsNextMission = true;
    break;
```
`CurrentMission` is then pre-incremented at `team.cpp:460`, so **`Loop:N` jumps to mission index N**, 0-based. `Loop:0` restarts the list.

Mission list exhaustion: `team.cpp:461`/`:491-494` -- when `CurrentMission >= MissionCount`, `Delete_This()` disbands the team. A mission list is therefore a finite script unless it ends in `Loop`.

### Mission quirks that matter for a reimplementation
1. **`Unload:N` double-books its argument.** N is both the waypoint and a `N × 6 s` timeout (`team.cpp:464` + `:594`). `Unload:4` means "unload at waypoint E, and give up after 24 s". Real data does this (`SCG12EB.INI` `gdi1`).
2. **`Move to Cell` has no execution handler.** It appears in the target-decode switch (`team.cpp:467-469`) but in neither the execution switch (`:516-582`) nor the timeout switch (`:588-601`). A team whose current mission is `Move to Cell` sets a target and then does nothing until something else advances it. Only 7 uses across all shipped scenarios.
3. **`TMISSION_NONE` (-1) is a soft-lock.** An unrecognised mission name yields `-1` (`teamtype.cpp:738`), which matches no case in either switch, so the team never advances and never times out.
4. **No waypoint validity check.** `Scen.Waypoint[N]` may be `-1` (unassigned, `scenario.cpp:78`); `team.cpp:477` converts it to a target regardless.
5. **`Coordinate_Move` reads one mission past the current one** (`team.cpp:1236-1237`): `Class->MissionList[CurrentMission + 1].Mission != TMISSION_MOVE`, used to decide whether an aircraft may stay airborne. At `CurrentMission == 19` this reads past the array.
6. `MissionCount` overrun is tolerated by design. `teamtype.cpp:357-365`, comment: "Some missions may have an incorrect MissionCount e.g. GDI SCG03EA.INI"; the guard is `mission.Argument = p2 ? atoi(p2) : 0;`. A missing name yields `TMISSION_NONE`. (No mismatch actually exists in the DOS INI set I checked; the guard is for other variants.)

### Actual usage across all 996 shipped team entries
`Move` 2974, `Attack Base` 340, `Attack Units` 319, `Guard` 277, `Loop` 159, `Unload` 88, `Attack Civil.` 9, `Move to Cell` 7, `Defend Base` 6, `Rampage` 5. **`Retreat` and `Attack Tarcom` are never used by shipped content.**

---

## 3. Member list

Storage (`teamtype.h:245-259`):
```cpp
unsigned int ClassCount;
TechnoTypeClass const* Class[MAX_TEAM_CLASSCOUNT];   // 5
unsigned char DesiredNum[MAX_TEAM_CLASSCOUNT];
```

Syntax: `ClassCount` followed by exactly that many `Name:Count` pairs, comma-separated. Parsed at `teamtype.cpp:310-350` with `strtok(NULL, ",:")` twice per pair, so `:` and `,` are interchangeable separators at this point.

Name resolution, `teamtype.cpp:316-338`, tried in this order with **no early-out**:
```cpp
i_id = InfantryTypeClass::From_Name(p1);   if (i_id != INFANTRY_NONE) otype = &InfantryTypeClass::As_Reference(i_id);
u_id = UnitTypeClass::From_Name(p1);       if (u_id != UNIT_NONE)     otype = &UnitTypeClass::As_Reference(u_id);
a_id = AircraftTypeClass::From_Name(p1);   if (a_id != AIRCRAFT_NONE) otype = &AircraftTypeClass::As_Reference(a_id);
```
Consequences: **aircraft wins any name collision, then units, then infantry.** Buildings and terrain cannot be team members (no `BuildingTypeClass::From_Name` attempt). An unresolved name is *dropped silently* (`:343`) and does **not** consume a `ClassCount` slot -- `ClassCount` is recounted from successful resolutions only (`:310` resets to 0, `:347` increments), so the stored `ClassCount` can be lower than the INI's declared count while parsing stays in sync.

Overflow past 5 distinct classes: the pair is parsed and thrown away (`:344`).

`DesiredNum[i] = atoi(p2)` (`:346`), an `unsigned char`, i.e. 0-255 per class. Real data ranges 1-10.

Names seen in shipped teams: infantry `E1 E2 E3 E4 E6 RMBO C1..C9 DELPHI`; units `LTNK MTNK HTNK STNK FTNK BGGY JEEP BIKE ARTY MSAM MLRS APC MCV HARV BOAT LST`; aircraft `A10 ORCA TRAN HELI`.

Runtime side: `TeamClass::Quantity[MAX_TEAM_CLASSCOUNT]` (`team.h:260`) mirrors `DesiredNum` as the current fill. Recruitment is per-slot (`TeamClass::Recruit(typeindex)`, `team.cpp:854-922`), scanning `Infantry` then `Units` globally for matching house + exact class, and stopping when `Quantity[typeindex] >= DesiredNum[typeindex]`. **Aircraft are never recruited** -- `Recruit` only handles `RTTI_INFANTRYTYPE` (`:868`) and `RTTI_UNITTYPE` (`:889`). Aircraft can only enter a team via `Do_Reinforcements`.

Transport special case (`team.cpp:900-906`): when a unit is added, everything currently attached to it is added too.

`Add()` rejection rules (`team.cpp:633-725`): dead/limbo objects (except during `ScenarioInit`), objects in radio contact, wrong house (`:643`); objects on `MISSION_STICKY`, `MISSION_SLEEP`, `MISSION_GUARD_AREA`, `MISSION_HUNT`, `MISSION_HARVEST` (`:661-664`); objects already on a team of equal-or-higher `RecruitPriority` (`:671-675` -- this is the member-stealing rule); and slots already full (`:694-696`).

---

## 4. The flags

All are 1-bit fields on `TeamTypeClass`.

### `IsRoundAbout` (`teamtype.h:166-169`, INI field 2)
> "If RoundAbout, the team avoids high-threat areas"

Single consumer, `findpath.cpp:557-563`:
```cpp
if (Team && Team->Class->IsRoundAbout) {
    unit_threat = (Team) ? Team->Risk : Risk();
    threat_stage = 0;
    threat = 0;
} else {
    unit_threat = threat = -1;
}
```
It arms threat-avoidance in `FootClass::Find_Path`. `threat == -1` disables threat weighting entirely; setting it to 0 with `unit_threat = Team->Risk` makes the pathfinder route around cells whose accumulated threat exceeds the team's own combined risk (`TeamClass::Risk`, summed in `Add`/`Remove` at `team.cpp:715` / `:803`). Applies to every member individually, since each member's `Find_Path` consults its `Team`.

### `IsSuicide` (`teamtype.h:176-180`, INI field 4)
> "If Suicide, the team won't stop until it achieves its mission or it's dead"

Single consumer, `team.cpp:1050-1053` in `Took_Damage`:
```cpp
if ((result != RESULT_NONE) && (!Class->IsSuicide)) { ... }
```
A non-suicide team retargets its attacker when damaged (`team.cpp:1058-1075`), subject to a stickiness rule: it will not abandon a target that is itself armed and currently in range of the team's center (`:1065-1073`). A **suicide team ignores incoming fire completely** and never retargets -- it prosecutes its scripted mission list only. Note it does not disable member-level self-defence; individual units still shoot back through normal `MISSION_ATTACK`/guard logic.

### `IsAutocreate` (`teamtype.h:182-186`, INI field 5, mislabelled "Spy")
> "Is this team type allowed to be created automatically by the computer when the appropriate trigger indicates?"

Two consumers.

1. `TeamTypeClass::Suggested_New_Team`, `teamtype.cpp:870-871`:
```cpp
if (ttype && ttype->House == house->Class->House
    && TeamClass::Number[index] < ((alerted || !ttype->IsAutocreate) ? ttype->MaxAllowed : 0)) {
```
Read it as: a **non**-autocreate team is eligible whenever its live count is under `MaxAllowed`; an autocreate team is eligible **only once the house is alerted** (otherwise its cap collapses to 0).

2. AI production gating, `house.cpp:3614` (vehicles) and `house.cpp:3714` / `:6495` / `:6643` / `:6766` (infantry and the RA-AI variants):
```cpp
if (team->House == Class->House && team->IsPrebuilt && (!team->IsAutocreate || IsAlerted)) {
```
So the flag gates *both* team spawning and pre-building of the members.

`IsAlerted` is set by `ACTION_AUTOCREATE` (`trigger.cpp:486-490`) and by combat events at `house.cpp:932`.

### `IsPrebuilt` (`teamtype.h:193-198`, second-to-last INI field, defaults `true`)
> "This flag tells the computer that it should build members to fill a team of this type regardless of whether there actually is a team of this type active."

Consumed only by the house production planner. `house.cpp:3607-3623` (unit branch):
```cpp
/*
**	Team types that are flagged as prebuilt, will always try to produce enough
**	to fill one team of this type regardless of whether there is a team active
**	of that type.
*/
for (index = 0; index < TeamTypes.Count(); index++) {
    TeamTypeClass const* team = TeamTypes.Ptr(index);
    if (team) {
        if (team->House == Class->House && team->IsPrebuilt && (!team->IsAutocreate || IsAlerted)) {
            for (unsigned subindex = 0; subindex < team->ClassCount; subindex++) {
                if (team->Class[subindex]->What_Am_I() == RTTI_UNITTYPE) {
                    int subtype = ((UnitTypeClass const*)(team->Class[subindex]))->Type;
                    counter[subtype] = MAX(counter[subtype], (int)team->DesiredNum[subindex]);
                }
            }
        }
    }
}
```
Mirror for infantry at `house.cpp:3711-3723`; RA-AI mirrors at `:6493-6500`, `:6640-6650`, `:6763-6773`. The counters are then reduced by the count of teamless units already in play (`house.cpp:3627-3635`) and drive the build queue. `IsPrebuilt` has **no effect on team creation itself**, only on stockpiling.

### `IsReinforcable` (`teamtype.h:200-205`, last INI field, defaults `true`)
> "If this team should allow recruitment of new members, then this flag will be true. A false value results in a team that fights until it is dead. This is similar to IsSuicide, but they will defend themselves."

Three live consumers.

1. Under-strength threshold, `team.cpp:303-316`:
```cpp
if (Class->IsReinforcable) {
    IsUnderStrength = (Total <= desired / 3);
} else {
    /*
    **	Teams that are not flagged as reinforcable are never considered under
    **	strength if the team has already started its main mission. ...
    */
    IsUnderStrength = !IsHasBeen;
}
```
A reinforcable team drops back into regroup mode below 1/3 strength; a non-reinforcable one, once launched, never does.

2. Recruitment gate in the tick, `team.cpp:436`:
```cpp
if (!IsMoving || (!IsFullStrength && Class->IsReinforcable) && !House->IsHuman) {
```
(Note the C precedence: `&&` binds tighter than `||`, so a stationary team always recruits regardless of the flag or of being human-owned. Almost certainly a bug in the original; preserve it if you want bit-exact behaviour.)

3. Production planning, `house.cpp:3695`, `:6474`, `:6623`, `:6743`:
```cpp
if ((team->IsReinforcable || !tptr->IsFullStrength) && team->House == Class->House) {
```
Also `house.cpp:6751`: `1 + (team->IsReinforcable ? 1 : 0)` -- reinforcable teams get one spare queued.

A dead check remains commented out at `team.cpp:639-641`.

### Also present but not INI-addressable
- **`IsTransient`** (`teamtype.h:207-212`): set only in code (`reinf.cpp:563`) for ad-hoc reinforcement team types. Its effect is in the team destructor, `team.cpp:145-147`: when the last team of a transient type dies, the *type itself* is deleted, freeing the heap slot.
- **`IsActive`** (`teamtype.h:160-164`): heap-slot liveness, maintained by `operator new`/`delete` (`teamtype.cpp:784`, `:807`).
- **`IsLearning`**, **`IsMercenary`**: parsed from INI, never read (see §1).

---

## 5. How teams get created

There are exactly four construction paths, all funnelling into `TeamClass::TeamClass(type, owner)` at `team.cpp:151-177` (which does `Number[TeamTypes.ID(Class)]++`).

The gatekeeper is `TeamTypeClass::Create_One_Of`, `teamtype.cpp:812-818`:
```cpp
TeamClass* TeamTypeClass::Create_One_Of(void) const
{
    if (ScenarioInit || TeamClass::Number[TeamTypes.ID(this)] < MaxAllowed) {
        return (new TeamClass(this, HouseClass::As_Pointer(House)));
    }
    return (NULL);
}
```
**`ScenarioInit` non-zero bypasses `MaxAllowed` entirely.** This is exploited deliberately by callers.

### (a) Autocreate / periodic AI creation
`HouseClass::Suggested_New_Team` (`house.cpp:2239-2242`) forwards to `TeamTypeClass::Suggested_New_Team` (`teamtype.cpp:862-909`) passing the house's `UScan`/`IScan` bitmasks of owned unit and infantry types.

Selection scores each eligible type (`teamtype.cpp:894-899`):
```cpp
int value = 0;
if ((ineeded & itypes) || (uneeded & utypes)) {
    value = ttype->RecruitPriority;
} else {
    value = ttype->RecruitPriority / 2;
}
```
Highest value wins; ties go to the first found (`:901-904`). Only `RTTI_INFANTRYTYPE` and `RTTI_UNITTYPE` members contribute to the needed-mask (`:878-888`), so aircraft-only teams always score at half priority.

Two callers in `HouseClass::AI`:

- **Alerted attack wave**, `house.cpp:990-1013`:
```cpp
if (IsAlerted && AlertTime.Expired()) {
    int maxteams = Random_Pick(2, (int)(((BuildLevel - 1) / 3) + 1));
    for (int index = 0; index < maxteams; index++) {
        TeamTypeClass const* ttype = Suggested_New_Team(true);
        if (ttype) { ScenarioInit++; ttype->Create_One_Of(); ScenarioInit--; }
    }
    ...
}
```
Note `ScenarioInit++` again bypasses `MaxAllowed`. Re-arm interval by difficulty (`:1004-1012`): hard `4-10` min, normal `5-20` min, easy `16-40` min, all `× TICKS_PER_MINUTE`.

- **Background trickle**, `house.cpp:1019-1023`:
```cpp
if (TeamTime.Expired()) {
    TeamTypeClass const* ttype = Suggested_New_Team(false);
    if (ttype) { ttype->Create_One_Of(); }
```
No `ScenarioInit` wrapper here, so `MaxAllowed` **is** enforced, and `alerted=false` means autocreate types are excluded. Reset at `house.cpp:1092` `TeamTime.Set(TEAM_DELAY)`, with `TEAM_DELAY = TICKS_PER_MINUTE / 10` (`house.h:752`), i.e. 90 ticks / 6 seconds. Initial value `house.cpp:462`.

Because shipped autocreate teams use `MaxAllowed=1` and the non-autocreate ones use `MaxAllowed=0`, the trickle path effectively creates nothing until `IsAlerted` flips.

### (b) Trigger action
Trigger actions (`trigger.h:84-107`):
```cpp
ACTION_NONE = -1,
ACTION_WIN,              // player wins!
ACTION_LOSE,             // player loses.
ACTION_BEGIN_PRODUCTION, // computer begins production
ACTION_CREATE_TEAM,      // computer creates a certain type of team
ACTION_DESTROY_TEAM,
ACTION_ALL_HUNT,       // all enemy units go into hunt mode (teams destroyed).
ACTION_REINFORCEMENTS, // player gets reinforcements
...
ACTION_AUTOCREATE,     // Computer to autocreat teams.
```
INI names, `trigger.cpp:86-103`: `"Create Team"`, `"Dstry Teams"`, `"Reinforce."`, `"Autocreate"`, `"All to Hunt"`.

Which actions carry a team name (`trigger.cpp:250-257`):
```cpp
bool TriggerClass::Action_Need_Team(TriggerClass::ActionType action)
{
    switch (action) {
    case ACTION_CREATE_TEAM:
    case ACTION_DESTROY_TEAM:
    case ACTION_REINFORCEMENTS:
        return (true);
    }
    return (false);
}
```

Trigger INI format (`trigger.cpp:988`, parsed at `trigger.cpp:1083-1152`):
`Triggername = Eventname, Actionname, Data, Housename, TeamName, IsPersistant`
with the team resolved by name at `trigger.cpp:1122`: `Team = TeamTypeClass::As_Pointer(strtok(NULL, ","));` (NULL if unknown; every team action null-guards).

Handlers, identical in all three `Spring` overloads (`trigger.cpp:352` object, `:550` cell, `:744` house/data). Object version at `trigger.cpp:486-511`:
```cpp
case ACTION_AUTOCREATE:
    if (obj && obj->Is_Techno()) {
        ((TechnoClass*)obj)->House->IsAlerted = true;
    }
    break;

case ACTION_CREATE_TEAM:
    if (Team) {
        ScenarioInit++;
        Team->Create_One_Of();
        ScenarioInit--;
    }
    break;

case ACTION_DESTROY_TEAM:
    if (Team) {
        Team->Destroy_All_Of();
    }
    break;

case ACTION_REINFORCEMENTS:
    if (Team) {
        success = Do_Reinforcements(Team);
    }
    break;
```
(Duplicates at `:617-700` and `:815-890`.)

**The `ScenarioInit++` around `Create_One_Of` is why `MaxAllowed=0` teams work at all.** Almost every shipped non-autocreate team has `MaxAllowed=0` and is trigger-spawned; the flag is bypassed by design.

`Destroy_All_Of` (`teamtype.cpp:826-836`) walks `Teams` and deletes every instance of this type, with the standard `index--` fixup for the compacting heap.

`ACTION_ALL_HUNT` (`Do_All_To_Hunt`, `trigger.cpp:1427-1451`) yanks every non-human, deployed unit and infantryman out of its team (`Team->Remove(unit)`) and assigns `MISSION_HUNT`. Teams left empty then self-delete via `team.cpp:448-451`.

Trigger removal nulls dangling team pointers the other way too: `TeamTypeClass::Remove` (`teamtype.cpp:689-709`) scans `Triggers` and clears `trigger->Team`.

### (c) Reinforcement
`Do_Reinforcements(TeamTypeClass*)`, `reinf.cpp:59-470`. Called from `ACTION_REINFORCEMENTS` and from `Create_Special_Reinforcement`.

- Rejects a type with `ClassCount == 0` (`reinf.cpp:64-65`).
- **Creates the controlling team only if `MissionCount != 0`** (`reinf.cpp:72-78`), and does so with `new TeamClass(...)` directly, so `MaxAllowed` is not consulted at all. Then `team->Force_Active()` (`reinf.cpp:77`), which sets `IsForcedActive = true; IsUnderStrength = false;` (`team.h:209-213`) so the team runs its mission list immediately without waiting for full strength.
- With `MissionCount == 0` there is no team: the objects are spawned and given ad-hoc missions themselves (`reinf.cpp:292-297`, `:395-416`, `:443-448`).
- Members are instantiated from `Class[]`/`DesiredNum[]` (`reinf.cpp:147-204`) inside `ScenarioInit++`/`--` brackets, and added with `team->Add(temp)`; the LST (`UNIT_HOVER`) is explicitly excluded from team membership (`reinf.cpp:161`).
- Delivery edge is chosen from the composition (`reinf.cpp:87-129`): aircraft present → `SOURCE_AIR`; hover lander present → `SOURCE_BEACH`; `UNIT_GUNBOAT` → `SOURCE_SHIPPING`; otherwise the owning house's `Edge`.
- Transports carrying cargo are marked `IsALoaner` (`reinf.cpp:170-179`), which later makes `Coordinate_Unload` detach them from the team once unloaded (`team.cpp:1371-1375`).
- Cargo-plane special case rewrites the team's **first mission argument in place** to the airstrip's target (`reinf.cpp:381-383`): `teamtype->MissionList[0].Argument = building->As_Target();`. This is a mutation of shared type data at runtime, and it is why `Move`/`Unload` accept a raw TARGET above `WAYPT_COUNT`.
- Announces `VOX_REINFORCEMENTS` if the team belongs to the human player (`reinf.cpp:465-467`).

`Create_Special_Reinforcement` (`reinf.cpp:498-593`) synthesises a throwaway `TeamTypeClass` at runtime for harvester replacements and airfield deliveries:
```cpp
strcpy((char*)&team->IniName[0], "TEMP");
team->IsReinforcable = false;
team->IsTransient = true;
team->ClassCount = 1;
team->Class[0] = type;
team->DesiredNum[0] = 1;
team->MissionCount = 1;
```
(`reinf.cpp:561-567`), defaulting to `TMISSION_UNLOAD` with `Argument = WAYPT_REINF` (`:571-572`) or `TMISSION_MOVECELL` at a calculated edge cell (`:543-544`, `:553-555`).

### (d) Scenario-init pre-placement -- does not exist in TD
`InitNum` is parsed but nothing consumes it. There is no "create N teams of this type at scenario start" pass in Tiberian Dawn. (If you are cross-referencing Red Alert, this is a divergence.)

### Destruction
- Mission list exhausted → `Delete_This()` (`team.cpp:491-494`).
- No members and `IsHasBeen` → `Delete_This()` (`team.cpp:448-451`).
- Human-owned team with zero members → `Delete_This()` (`team.cpp:330-333`).
- `ACTION_DESTROY_TEAM` / `ACTION_ALL_HUNT`.
- `HouseClass` defeat deletes all team *types* of that house (`house.cpp:3200-3203`, `:4507-4510`).
- Destructor (`team.cpp:137-149`) decrements `Number[]`, removes all members, and deletes the type if `IsTransient` and the count hit zero.

### Suspension
`TeamClass::Suspend_Teams(int priority)` (`team.cpp:1468-1487`), called once, from `techno.cpp:3878` `TeamClass::Suspend_Teams(20)` when a base is under attack and needs its units back. Every team with `RecruitPriority < 20` is stripped of members and parked for `TICKS_PER_MINUTE * 2`. This is the main gameplay meaning of `RecruitPriority` beyond member-stealing: it is a survival threshold. Shipped base-defence teams use 20-30 precisely to survive this.

---

## 6. Teams and waypoints

### Waypoint storage
`scenario.h:69`: `CELL Waypoint[WAYPT_COUNT];` on the global `Scen`. Initialised to `-1` at `scenario.cpp:78`.

`defines.h:2552-2560`:
```cpp
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
So **`WAYPT_COUNT == 28`**; indices 0-25 are `A`-`Z`, 26 is Home, 27 is Reinforcement.

### INI
Section `[Waypoints]`, keys are the **decimal index**, values are cell numbers. Read at `display.cpp:1330-1346`:
```cpp
for (int i = 0; i < WAYPT_COUNT; i++) {
    char buf[20];
    sprintf(buf, "%d", i);
    Scen.Waypoint[i] = ini.Get_Int("Waypoints", buf, -1);
    if (Scen.Waypoint[i] != -1) {
        ...
        (*this)[Scen.Waypoint[i]].IsWaypoint = 1;
    }
}
```
Written at `display.cpp:1437-1447`, skipping `-1`. `WAYPT_HOME` defaults to the map's top-left cell if unset (`display.cpp:1353-1355`) and seeds the tactical view and all four bookmarks (`:1356-1357`).

Waypoints are read as part of `Map.Read_INI`, which runs *after* `TeamTypeClass::Read_INI` (`scenarioini.cpp:335` vs `:356`). That is fine because team mission arguments are stored as raw ints and only dereferenced at runtime.

### The team connection
Exactly one site, `team.cpp:471-481`:
```cpp
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
    break;
```

So, precisely:
- **Only `Move` and `Unload` consult the waypoint table.** `Move to Cell` takes a literal cell (`team.cpp:467-469`); `Attack Tarcom` takes a literal target (`:483-485`).
- **The threshold is `Argument < 28`**, not `< 26`. Arguments 26 and 27 legally resolve to Home and Reinforcement respectively.
- Arguments ≥ 28 are reinterpreted as an encoded `TARGET`. Since a bare `CELL` and a `TARGET` are different encodings, a scenario author cannot reach an arbitrary cell this way; the branch exists for the runtime-injected building targets set by `reinf.cpp:382`.
- `Scen.Waypoint[N] == -1` is not checked.

### Editor representation
`mapedtm.cpp` displays and accepts these two missions' arguments as **letters**, everything else as decimal:
```cpp
if (missions[i].Mission == TMISSION_MOVE || missions[i].Mission == TMISSION_UNLOAD) {
    missions[i].Argument = toupper(arg_buf[0]) - 'A';
} else {
    missions[i].Argument = atoi(arg_buf);
}
```
(`mapedtm.cpp:1382-1385`; display mirrors at `:1045-1048`, `:1329-1332`, `:2226-2237`). The INI always stores the number. So `Move:0` is waypoint **A**, `Move:12` is **M**.

### Other waypoint uses reachable from team behaviour
- `WAYPT_REINF` (27) is the default unload point for ad-hoc reinforcements (`reinf.cpp:572`) and the fallback destination for reinforcement aircraft (`aircraft.cpp:1202`).
- Waypoint 25 (`Z`) is hardcoded as the drop-zone smoke location for `ACTION_DZ` (`trigger.cpp:468`, `:658`, `:856`).
- Waypoint 27 is overwritten with the computed start position in multiplayer (`scenarioini.cpp:637`).

---

## 7. Runtime state a reimplementation must carry per team

From `team.h` and the tick at `team.cpp:260-610`:

`IsForcedActive` (`team.h:70`), `IsHasBeen` (`:77`), `IsFullStrength` (`:83`), `IsUnderStrength` (`:90`), `IsReforming` (`:98`), `IsLagging` (`:104`), `IsAltered` (`:114`), `IsMoving` (`:121`), `IsNextMission` (`:128`), `Suspended` (`:132`); `Center` and `ObjectiveCenter` cells (`:141-142`); `MissionTarget` and the overrideable `Target` (`:149-150`); `Total`, `Risk`, `SuspendTimer`, `CurrentMission`, `TimeOut`, singly-linked `Member` list, `Quantity[5]`.

Statics: `TeamClass::Number[TEAMTYPE_MAX]` (live count per type, `team.h:230` / `team.cpp:60`) and `TeamClass::Success[TEAMTYPE_MAX]` (`team.h:273` / `team.cpp:65`) -- **`Success` is written nowhere; it is zeroed in `Init` and otherwise dead**, despite the header comment promising success-weighted team selection.

`STRAY_DISTANCE 2` (`team.h:47`) is the cohesion radius used by `Coordinate_Regroup` (`team.cpp:1163`), `Coordinate_Move` (`:1230`, `:1235`), `Lagging_Units` (`:1312`) and `Coordinate_Conscript` (`:1413`).

Regroup destination when under strength (`team.cpp:361-385`): nearest friendly **unarmed** building weighted by `Cell_Distance × (Cell_Threat + 1)`, with `STRUCT_REPAIR` getting its distance halved (`:371-373`), then offset to a `Safety_Point`.

Team launch gesture (`team.cpp:406-418`): on going active, all infantry members play `DO_GESTURE1` or `DO_GESTURE2`, picked once per team by `Random_Pick(1, 2)`.