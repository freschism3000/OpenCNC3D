Documentation complete. Here is the full specification.

---

# C&C: Tiberian Dawn TRIGGER system -- complete specification

All citations are `file:line` under `brain/vanilla/tiberiandawn/` unless noted. Source is the EA GPL release (GPLv3, `trigger.h:1-14`).

**One caveat up front:** the `.cpp` files in this checkout are ISO-8859 encoded, so plain `grep` treats them as binary and silently reports nothing. Use `grep -a`. (This is why a naive search "proves" `UnitClass::Read_INI` does not exist. It does, at `unit.cpp:3770`.)

---

## 1. The `[Triggers]` INI line format

Section name: `"Triggers"` (`trigger.h:149-152`).

**Format** (`trigger.cpp:988`, `trigger.cpp:1068`, `trigger.cpp:1140`):

```
Triggername = Eventname, Actionname, Data, Housename, TeamName, IsPersistant
```

Parsed field by field in `TriggerClass::Fill_In` (`trigger.cpp:1083-1134`); written back by `Write_INI` with `sprintf(buf, "%s,%s,%d,%s,%s,%d", ...)` (`trigger.cpp:1194-1201`).

| # | Field | Parse | Legal values / meaning |
|---|-------|-------|------------------------|
| key | `Triggername` | `Set_Name(name)` `trigger.cpp:1094` | **Max 4 chars** + NUL. `char Name[5]` (`trigger.h:275`), `strncpy(Name, buf, sizeof(Name))` (`trigger.h:176-180`). Lookup is case-insensitive `stricmp` (`trigger.cpp:1234`). Longer names are silently truncated to 4. Names `XXXX`/`YYYY`/`ZZZZ` are magic (see ACTION_DESTROY_*). |
| 1 | `Eventname` | `Event_From_Name(strtok(buf, ","))` `trigger.cpp:1099` | One of the 17 `EventText` strings (§2). Case-insensitive. Unrecognised or NULL ⇒ `EVENT_NONE` (`trigger.cpp:1304-1319`). |
| 2 | `Actionname` | `Action_From_Name(strtok(NULL, ","))` `trigger.cpp:1104` | One of the 18 `ActionText` strings (§3). Case-insensitive. Unrecognised or NULL ⇒ `ACTION_NONE` (`trigger.cpp:1356-1371`). |
| 3 | `Data` | `DataCopy = Data = atol(strtok(NULL, ","))` `trigger.cpp:1109` | `int`. Meaning depends on Event (§2). **Must be present**: `atol(NULL)` is undefined behaviour, so a line with fewer than 3 tokens is unsafe. `DataCopy` is the reload value for `EVENT_TIME`. |
| 4 | `Housename` | `HouseTypeClass::From_Name(strtok(NULL, ","))` `trigger.cpp:1114` | `GoodGuy`, `BadGuy`, `Neutral`, `Special`, `Multi1`…`Multi6` (`hdata.cpp:48`, `:61`, `:74`, `:86`, `:98`-`:160`; enum `defines.h:660-673`). Case-insensitive (`hdata.cpp:253-263`). Anything else, including the literal `None` written by `Write_INI` (`trigger.cpp:1183`), yields `HOUSE_NONE`. **Special rule:** if `House == HOUSE_NONE && Event == EVENT_PLAYER_ENTERED`, House is forced to `PlayerPtr->Class->House` (`trigger.cpp:1115-1117`). |
| 5 | `TeamName` | `TeamTypeClass::As_Pointer(strtok(NULL, ","))` `trigger.cpp:1122` | A `[TeamTypes]` `IniName`, case-insensitive (`teamtype.cpp:657-672`). `None` / unknown / missing ⇒ `NULL`. |
| 6 | `IsPersistant` | `atoi(p)` if token present, else `VOLATILE` `trigger.cpp:1128-1133` | `0`/`1`/`2` (§4). Field is optional for backward compatibility. |

**Whitespace:** `INIClass::Get_String` calls `strtrim` on the whole value only (`ini.cpp:1035`, common/). `Fill_In` splits on a bare `","` with no per-token trimming, so **spaces after commas break the name matches**. Write `Time,Win,5,GoodGuy,None,0`, not `Time, Win, 5, ...`.

**Buffer limit:** the whole value is read into `char buf[128]` (`trigger.cpp:1024`, `:1087`).

**Global cap:** `TRIGGER_MAX 80` triggers (`defines.h:2021`); storage is `TFixedIHeapClass<TriggerClass> Triggers` (`globals.cpp:73`).

---

## 2. Every EVENT type

Enum, verbatim (`trigger.h:40-77`):

```c
typedef enum EventType : signed char
{
    EVENT_NONE = -1,

    /*
    .......................... Cell-specific events ..........................
    */
    EVENT_PLAYER_ENTERED, // player enters this square
    EVENT_CELLFIRST = EVENT_PLAYER_ENTERED,

    /*
    ......................... Object-specific events .........................
    */
    EVENT_DISCOVERED, // player discovers this object
    EVENT_OBJECTFIRST = EVENT_DISCOVERED,
    EVENT_ATTACKED,  // player attacks this object
    EVENT_DESTROYED, // player destroys this object
    EVENT_ANY,       // Any object event will cause the trigger.

    /*
    ......................... House-specific events ..........................
    */
    EVENT_HOUSE_DISCOVERED, // any object in this house discovered
    EVENT_HOUSEFIRST = EVENT_HOUSE_DISCOVERED,
    EVENT_UNITS_DESTROYED,      // all house's units destroyed
    EVENT_BUILDINGS_DESTROYED,  // all house's buildings destroyed
    EVENT_ALL_DESTROYED,        // all house's units & buildings destroyed
    EVENT_CREDITS,              // house reaches this many credits
    EVENT_TIME,                 // time elapses for this house
    EVENT_NBUILDINGS_DESTROYED, // Number of buildings destroyed.
    EVENT_NUNITS_DESTROYED,     // Number of units destroyed.
    EVENT_NOFACTORIES,          // No factories left.
    EVENT_EVAC_CIVILIAN,        // Civilian has been evacuated.
    EVENT_BUILD,                // If specified building has been built.

    EVENT_COUNT,
    EVENT_FIRST = 0
} EventType;
```

INI name table, verbatim (`trigger.cpp:68-84`), indexed as `EventText[event + 1]`:

```c
static const char* EventText[EVENT_COUNT + 1] = {"None",
                                                 "Player Enters",
                                                 "Discovered",
                                                 "Attacked",
                                                 "Destroyed",
                                                 "Any",
                                                 "House Discov.",
                                                 "Units Destr.",
                                                 "Bldgs Destr.",
                                                 "All Destr.",
                                                 "Credits",
                                                 "Time",
                                                 "# Bldgs Dstr.",
                                                 "# Units Dstr.",
                                                 "No Factories",
                                                 "Civ. Evac.",
                                                 "Built It"};
```

### Per-event table

| Value | Enum | INI name | Needs object? | Needs house? | Needs Data? | Fires when |
|---|---|---|---|---|---|---|
| 0 | `EVENT_PLAYER_ENTERED` | `Player Enters` | yes | yes | no | **Two paths.** (a) Cell: `FootClass::Per_Cell_Process` when a non-cloaked foot unit enters a cell whose trigger's `House` equals the mover's `Owner()` (`foot.cpp:1425-1428`). (b) Object: `TechnoClass::Captured` when the object changes owner (`techno.cpp:2971-2973`) -- the house check there is commented out, so **any** capture springs it. |
| 1 | `EVENT_DISCOVERED` | `Discovered` | yes | no | no | `TechnoClass::Revealed` when `PlayerPtr` first reveals an object not owned by the player, single-player only (`techno.cpp:563-566`; dead duplicate at `:647-650`). Also fired from `Record_The_Kill` when killed by a source (`techno.cpp:3144-3145`). |
| 2 | `EVENT_ATTACKED` | `Attacked` | yes | (also house, see below) | no | `ObjectClass::Take_Damage` when damaged by a source and not destroyed (`object.cpp:1513-1515`); and from `Record_The_Kill` with a source (`techno.cpp:3141-3142`). **House-based variant:** `HouseClass::Attacked` iterates `HouseTriggers` and springs `EVENT_ATTACKED` for the house (`house.cpp:1692-1694`), gated on `SpeakAttackDelay` having expired and the attacked house being the player's. |
| 3 | `EVENT_DESTROYED` | `Destroyed` | yes | no | no | `TechnoClass::Record_The_Kill` unconditionally (no source required) (`techno.cpp:3147-3148`). |
| 4 | `EVENT_ANY` | `Any` | yes | no | no | Wildcard. `Spring(event, ObjectClass*)` accepts when `event != Event && Event != EVENT_ANY` is false (`trigger.cpp:358-360`), i.e. any object-routed event. **The cell and house Spring overloads do not honour `EVENT_ANY`** (`trigger.cpp:556-558`, `:750-752`). |
| 5 | `EVENT_HOUSE_DISCOVERED` | `House Discov.` | no | yes | no | `HouseClass::AI` when `IsDiscovered` is set (`house.cpp:1484-1487`); the flag is cleared on spring. Set by `techno.cpp:571` / `:655`. |
| 6 | `EVENT_UNITS_DESTROYED` | `Units Destr.` | no | yes | no | `HouseClass::AI` when `!((ActiveUScan & ~UNITF_GUNBOAT) | IScan | (ActiveAScan & ~(TRANSPORT|CARGO|A10)))` (`house.cpp:1505-1512`). Gated by `!EndCountDown` (`house.cpp:1493`). |
| 7 | `EVENT_BUILDINGS_DESTROYED` | `Bldgs Destr.` | no | yes | no | `HouseClass::AI` when `!ActiveBScan` (`house.cpp:1497-1502`). Gated by `!EndCountDown`. |
| 8 | `EVENT_ALL_DESTROYED` | `All Destr.` | no | yes | no | `HouseClass::AI` when buildings AND units scans are all clear (`house.cpp:1516-1523`). Gated by `!EndCountDown`. |
| 9 | `EVENT_CREDITS` | `Credits` | no | yes | **yes** | `HouseClass::AI` passes `Credits` (`house.cpp:1528`). Springs when `data >= Data` (rejects if `data < Data`, `trigger.cpp:757-759`). |
| 10 | `EVENT_TIME` | `Time` | no | yes | **yes** | `HouseClass::AI` when `is_time` (`house.cpp:1535`). `Data` is decremented once per check and springs at `Data <= 0`, then reloads `Data = DataCopy` (`trigger.cpp:782-788`). **Unit is 1/10 minute (6 seconds), not minutes**: `TriggerTime = TICKS_PER_MINUTE / 10` (`house.cpp:1233`), `TICKS_PER_SECOND 15`, `TICKS_PER_MINUTE (TICKS_PER_SECOND*60)` (`defines.h:2219-2220`). The header comment claiming minutes (`trigger.h:264-266`) is wrong. |
| 11 | `EVENT_NBUILDINGS_DESTROYED` | `# Bldgs Dstr.` | no | yes | **yes** | `HouseClass::AI` passes `BuildingsLost` (`house.cpp:1470`). Springs when `data >= Data` (`trigger.cpp:773-777`). `BuildingsLost++` in `Record_The_Kill` only if `WhoLastHurtMe != HOUSE_NONE` (`techno.cpp:3178-3180`). |
| 12 | `EVENT_NUNITS_DESTROYED` | `# Units Dstr.` | no | yes | **yes** | `HouseClass::AI` passes `UnitsLost` (`house.cpp:1477`). Springs when `data >= Data`. `UnitsLost++` for aircraft/infantry/units (`techno.cpp:3199`, `:3216`, `:3233`). |
| 13 | `EVENT_NOFACTORIES` | `No Factories` | no | yes | no | `HouseClass::AI` when `!(BScan & (STRUCTF_AIRSTRIP|STRUCTF_HAND|STRUCTF_WEAP|STRUCTF_BARRACKS))` (`house.cpp:1542-1545`). |
| 14 | `EVENT_EVAC_CIVILIAN` | `Civ. Evac.` | no | yes | no | `HouseClass::AI` when `IsCivEvacuated` (`house.cpp:1463`). Set when a non-technician civilian infantry detaches from a retreating aircraft off-radar (`aircraft.cpp:889-893`). **The flag is never cleared by the spring**, unlike `IsDiscovered`. |
| 15 | `EVENT_BUILD` | `Built It` | no | yes | **yes** | `HouseClass::AI` passes `JustBuilt` (`house.cpp:1453-1457`); springs on exact match `data == Data` (`trigger.cpp:764-766`), then clears `JustBuilt`. `Data` is the **numeric `StructType` index** (`defines.h`, `typedef enum StructType`: `STRUCT_WEAP=0, STRUCT_GTOWER=1, STRUCT_ATOWER=2, STRUCT_OBELISK=3, STRUCT_RADAR=4, STRUCT_TURRET=5, STRUCT_CONST=6, STRUCT_REFINERY=7, …`). `JustBuilt` is set in `BuildingClass::Revealed` when not `ScenarioInit` (`building.cpp:4971`), so it means "revealed", which for player buildings is completion. |

**Classifier helpers** (the editor and any compiler should use these to decide which INI fields are meaningful):
- `Event_Need_Object` (`trigger.cpp:158-169`): `PLAYER_ENTERED, DISCOVERED, ATTACKED, DESTROYED, ANY`.
- `Event_Need_House` (`trigger.cpp:186-204`): `PLAYER_ENTERED, HOUSE_DISCOVERED, UNITS_DESTROYED, BUILDINGS_DESTROYED, ALL_DESTROYED, CREDITS, TIME, NBUILDINGS_DESTROYED, NUNITS_DESTROYED, NOFACTORIES, EVAC_CIVILIAN, BUILD`.
- `Event_Need_Data` (`trigger.cpp:221-232`): `CREDITS, TIME, NBUILDINGS_DESTROYED, NUNITS_DESTROYED, BUILD`.

Note `EVENT_PLAYER_ENTERED` is the only event in **both** the object and house lists.

---

## 3. Every ACTION type

Enum, verbatim (`trigger.h:82-108`):

```c
    typedef enum ActionType
    {
        ACTION_NONE = -1,

        ACTION_WIN,              // player wins!
        ACTION_LOSE,             // player loses.
        ACTION_BEGIN_PRODUCTION, // computer begins production
        ACTION_CREATE_TEAM,      // computer creates a certain type of team
        ACTION_DESTROY_TEAM,
        ACTION_ALL_HUNT,       // all enemy units go into hunt mode (teams destroyed).
        ACTION_REINFORCEMENTS, // player gets reinforcements
                               // (house that gets them is determined by
                               // the Reinforcement instance)
        ACTION_DZ,             // Deploy drop zone smoke.
        ACTION_AIRSTRIKE,      // Enable airstrike.
        ACTION_NUKE,           // Enable nuke for computer.
        ACTION_ION,            // Give ion cannon to computer.
        ACTION_DESTROY_XXXX,   // Destroy trigger XXXX.
        ACTION_DESTROY_YYYY,   // Destroy trigger YYYY.
        ACTION_DESTROY_ZZZZ,   // Destroy trigger ZZZZ.
        ACTION_AUTOCREATE,     // Computer to autocreat teams.
        ACTION_WINLOSE,        // Win if captured, lose if destroyed.
        ACTION_ALLOWWIN,       // Allows winning if triggered.

        ACTION_COUNT,
        ACTION_FIRST = 0
    } ActionType;
```

INI name table, verbatim (`trigger.cpp:86-103`), indexed as `ActionText[action + 1]`:

```c
static const char* ActionText[TriggerClass::ACTION_COUNT + 1] = {"None",
                                                                 "Win",
                                                                 "Lose",
                                                                 "Production",
                                                                 "Create Team",
                                                                 "Dstry Teams",
                                                                 "All to Hunt",
                                                                 "Reinforce.",
                                                                 "DZ at 'Z'",
                                                                 "Airstrike",
                                                                 "Nuclear Missile",
                                                                 "Ion Cannon",
                                                                 "Dstry Trig 'XXXX'",
                                                                 "Dstry Trig 'YYYY'",
                                                                 "Dstry Trig 'ZZZZ'",
                                                                 "Autocreate",
                                                                 "Cap=Win/Des=Lose",
                                                                 "Allow Win"};
```

### CRITICAL: the action body is different in each of the three `Spring` overloads

There is no shared action dispatcher. `Spring(EventType, ObjectClass*)` (`trigger.cpp:407-518`), `Spring(EventType, CELL)` (`trigger.cpp:606-706`) and `Spring(EventType, HousesType, int)` (`trigger.cpp:795-897`) each contain their own `switch (Action)`. Several actions **behave differently or are entirely absent** depending on how the trigger was reached. A compiler must know which route an action will take, which is determined by the Event.

| Action | Object route | Cell route | House route |
|---|---|---|---|
| `ACTION_NONE` | no-op `:471` | no-op `:661` | no-op `:852` |
| `ACTION_WIN` | `PlayerPtr->Flag_To_Win()` `:474-476` | same `:664-666` | same `:859-861` |
| `ACTION_LOSE` | `PlayerPtr->Flag_To_Lose()` `:478-480` | same `:668-670` | same `:863-865` |
| `ACTION_BEGIN_PRODUCTION` | `HouseClass::As_Pointer(House)->Begin_Production()` `:482-484` | **Different:** the enemy of the player, i.e. `HOUSE_BAD` if player is GDI else `HOUSE_GOOD` `:672-678` | `As_Pointer(House)->Begin_Production()` `:867-869` |
| `ACTION_CREATE_TEAM` | `ScenarioInit++; Team->Create_One_Of(); ScenarioInit--;` if `Team` `:492-498` | same `:680-686` | same `:871-877` |
| `ACTION_DESTROY_TEAM` | `Team->Destroy_All_Of()` `:500-504` | same `:688-692` | same `:879-883` |
| `ACTION_ALL_HUNT` | `Do_All_To_Hunt()` `:512-514` | same `:700-702` | same `:891-893` |
| `ACTION_REINFORCEMENTS` | `success = Do_Reinforcements(Team)` `:506-510` | same `:694-698` | same `:885-889` |
| `ACTION_DZ` | `new AnimClass(ANIM_LZ_SMOKE, Cell_Coord(Scen.Waypoint[25]))` `:467-469` | same `:657-659` | same `:855-857` |
| `ACTION_AIRSTRIKE` | **`PlayerPtr->IsAirstrikePending = true`** (deferred; realised in `house.cpp:1679-1697`) `:462-465` | `As_Pointer(House)->AirStrike.Enable(false,true)`, plus sidebar add if `House == PlayerPtr` `:647-655` | **`PlayerPtr->AirStrike.Enable(false,true)`** (note: `PlayerPtr`, not `House`) `:843-850` |
| `ACTION_NUKE` | `HOUSE_BAD NukeStrike.Enable(true,false)` + `Forced_Charge(player is NOD)` `:408-411` | same `:607-610` | same `:797-800` |
| `ACTION_ION` | `HOUSE_GOOD IonCannon.Enable(true,false)` + `Forced_Charge(player is GDI)` `:413-416` | same `:612-615` | same `:802-805` |
| `ACTION_DESTROY_XXXX` | `As_Pointer("XXXX")->Remove(); delete trig;` `:438-444` | same `:623-629` | same `:819-825` |
| `ACTION_DESTROY_YYYY` | `:446-452` | `:631-637` | `:827-833` |
| `ACTION_DESTROY_ZZZZ` | `:454-460` | `:639-645` | `:835-841` |
| `ACTION_AUTOCREATE` | **only the springing object's house**: `if (obj && obj->Is_Techno()) ((TechnoClass*)obj)->House->IsAlerted = true` `:486-490` | **ALL houses**: loop `Houses.Count()`, `IsAlerted = true` `:617-621` | `As_Pointer(House)->IsAlerted = true` `:815-817` |
| `ACTION_WINLOSE` | **Object route only.** `EVENT_DESTROYED` ⇒ `Flag_To_Lose()` if `!PlayerPtr->IsToWin \|\| Blockage > 0`; `EVENT_PLAYER_ENTERED` ⇒ `Flag_To_Win()` if `!IsToLose`; any other event ⇒ `success = false` `:418-436` | **absent** (falls to `default:`) | **absent** |
| `ACTION_ALLOWWIN` | **absent** (`default:`) | **absent** | present but empty `:812-813`; the real effect is the destructor decrementing `Blockage` |

Notes on the odd ones:

- **`ACTION_DESTROY_XXXX/YYYY/ZZZZ`** call `trig->Remove()` and then `delete trig` on the same pointer. `Remove()` already ends with `delete this` (`trigger.cpp:979`), so this is a double-delete against the fixed heap. Reproduce carefully or fix deliberately.
- **`ACTION_ALLOWWIN`** is a win *blocker*, not a win. `Read_INI` increments `Houses.Ptr(House)->Blockage` for every `ACTION_ALLOWWIN` trigger with a house (`trigger.cpp:1050-1052`). `~TriggerClass` decrements it and sets `BorrowedTime = TICKS_PER_SECOND * 4` (`trigger.cpp:303-310`). A house only actually wins when `IsToWin && BorrowedTime.Expired() && Blockage <= 0` (`house.cpp:939-947`). So `Allow Win` triggers must all be destroyed (i.e. sprung while volatile) before `Win` takes effect.
- **`Flag_To_Win` / `Flag_To_Lose` are deferred**, not immediate: they set `IsToWin`/`IsToLose` and `BorrowedTime = TICKS_PER_SECOND * 3` (v1.07) or `* 1` (`house.cpp:4566-4579`, `:4596-4610`). Resolution happens in `HouseClass::AI` (`house.cpp:936-958`). `Flag_To_Win` refuses if any of `IsToWin/IsToDie/IsToLose` is already set; `Flag_To_Lose` clears `IsToWin` first and so overrides a pending win.
- **`Do_All_To_Hunt`** (`trigger.cpp:1427-1450`): every non-human `UnitClass` and `InfantryClass` that `IsDown && !IsInLimbo` is removed from its team and assigned `MISSION_HUNT`. Buildings and aircraft are untouched.
- **`Do_Reinforcements`** (`reinf.cpp:59`) is the only action that can return `false`: no team / `ClassCount == 0` (`reinf.cpp:64-65`), team allocation failure (`:78-79`), or unresolvable `SOURCE_NONE` edge (`:135-140`).

**`Action_Need_Team`** (`trigger.cpp:250-259`): `ACTION_CREATE_TEAM`, `ACTION_DESTROY_TEAM`, `ACTION_REINFORCEMENTS`.

---

## 4. Persistence / repeat semantics

Enum, verbatim (`trigger.h:110-115`):

```c
    typedef enum PersistantType
    {
        VOLATILE = 0,
        SEMIPERSISTANT = 1,
        PERSISTANT = 2,
    } PersistantType;
```

Author's own description (`trigger.h:220-237`):

```
    **	0 = trigger destroys itself immediately after going off, and removes
    **	    itself from all objects it's attached to
    **	1 = trigger is "Semi-Persistent"; it maintains a count of all objects
    **	    it's attached to, and only actually "springs" after its been
    **		 triggered from all the objects; then, it removes itself.
    **	2 = trigger is Fully Persistent; it just won't go away.
```

Default when field 6 is absent: `VOLATILE` (`trigger.cpp:1132`); also the constructor default (`trigger.cpp:278`).

### Exact mechanics

**`VOLATILE (0)`** -- after the action switch, `if (success && IsPersistant == VOLATILE) Remove();` (`trigger.cpp:526-528`, `:714-716`, `:905-907`). `Remove()` is total: it clears the trigger from every cell in `CellTriggers`, from every `Infantry`, `Buildings`, `Units` and `Terrains` object, from every `HouseTriggers[h]` list, then `delete this` (`trigger.cpp:927-982`). Fires exactly once, globally.

**`SEMIPERSISTANT (1)`** -- implemented **only in the object and cell overloads** (`trigger.cpp:378-400`, `:576-598`). On each spring it:
1. detaches itself from the caller (`obj->Trigger = NULL` / `Map[cell].IsTrigger = 0`),
2. `AttachCount--`,
3. if `AttachCount > 0`, returns `false` without performing any action,
4. otherwise sets `IsPersistant = VOLATILE` and falls through to the action, and is therefore `Remove()`d.

So: "fire once all attached things have each triggered it."

**`PERSISTANT (2)`** -- nothing special; it simply fails the `IsPersistant == VOLATILE` test, so it is never removed and re-fires every time its event occurs.

### Two behaviours that a compiler must model exactly

**(a) The house overload ignores semi-persistence entirely.** `Spring(EventType, HousesType, int)` (`trigger.cpp:744-910`) contains no `SEMIPERSISTANT` block at all. For a house-routed event, `1` behaves identically to `2`: not volatile ⇒ never removed ⇒ repeats forever.

**(b) `AttachCount` includes a `+1` for the house registration.** `Read_INI` does:

```c
        if (trigger->House != HOUSE_NONE) {
            if (trigger->Action == ACTION_ALLOWWIN) {
                HouseClass::As_Pointer(trigger->House)->Blockage++;
            }
            HouseTriggers[trigger->House].Add(trigger);
            trigger->AttachCount++;
        }
```
(`trigger.cpp:1049-1055`)

Nothing ever decrements that house reference. So a `SEMIPERSISTANT` trigger with a house and N cell/object attachments has `AttachCount == N + 1`; after all N springs it sits at 1 and **never reaches 0, so it never fires**. This bites `Player Enters` cell triggers in particular, because `Fill_In` forces a house onto them (`trigger.cpp:1115-1117`), guaranteeing the `+1`. Reported as as-written behaviour of this source.

**(c) Object death does not decrement `AttachCount`.** `ObjectClass` has no trigger detach path; only `Spring` (semi-persistent branch) and `TriggerClass::Remove` ever touch attachment. Destroying an attached object without springing the trigger permanently inflates the count.

**(d) `EVENT_TIME` retry on failure:** `if (!success && Event == EVENT_TIME) Data = 1;` (`trigger.cpp:520-521`, `:708-709`, `:899-900`), so a failed timed reinforcement retries on the next 6-second check instead of waiting a full period.

---

## 5. How a trigger is attached

### 5a. To a house

`TriggerClass::Read_INI` (`trigger.cpp:1049-1055`, quoted above) adds the trigger to `HouseTriggers[House]`, a `DynamicVectorClass<TriggerClass*> HouseTriggers[HOUSE_COUNT]` (`globals.cpp:393`, `externs.h:186`). On save-game load the same registration is redone by `TriggerClass::Load()` (`trigger.cpp:105-110`).

This list is the **only** thing `HouseClass::AI` iterates (`house.cpp:1447`), so a trigger with `Housename = None` can never receive any house-routed event. It can still be attached to cells/objects.

### 5b. To a cell -- `[CellTriggers]`

Section `"CellTriggers"`, format:

```
CellNumber = Triggername
```

Read in `DisplayClass::Read_INI` (`display.cpp:1359-1391`):

```c
    int len = ini.Entry_Count("CellTriggers");
    for (int index = 0; index < len; index++) {
        char const* cellentry = ini.Get_Entry("CellTriggers", index);
        CELL cell = atoi(cellentry);
        ...
        if (cell > 0 && cell < MAP_CELL_TOTAL && !(*this)[cell].IsTrigger) {
            CellTriggers[cell] = ini.Get_Trigger("CellTriggers", cellentry);
            if (CellTriggers[cell]) {
                (*this)[cell].IsTrigger = 1;
                if (CellTriggers[cell]) {
                    CellTriggers[cell]->AttachCount++;
                }
            }
        }
    }
```

- The entry **key** is the cell number, the **value** is the 4-char trigger name; resolved by `CCINIClass::Get_Trigger` ⇒ `TriggerClass::As_Pointer` (`ccini.cpp:307-314`).
- `cell > 0` -- cell 0 cannot carry a trigger.
- `!IsTrigger` -- **one trigger per cell, first entry wins**; duplicates for the same cell are silently dropped.
- Both the `CellClass::IsTrigger` bit and the `CellTriggers[cell]` pointer must be set; `CellClass::Get_Trigger` returns `NULL` unless `IsTrigger` (`cell.cpp:1605-1612`), and `Get_Trigger` is the only read path used at runtime (`foot.cpp:1425`).
- Written back by `DisplayClass::Write_INI` (`display.cpp:1450-1467`) as `ini.Put_Trigger(CELLTRIG, entry, tp)`, i.e. just the name (`ccini.cpp:337-340`).
- `CellTriggers` is `DynamicVectorClass<TriggerClass*>` sized `MAP_CELL_TOTAL` (`globals.cpp:392`, `map.cpp:405-407`, `:424-426`).
- Save/load codes the pointer to a `TARGET` and back (`iomap.cpp:193`, `:268`).

**Only `EVENT_PLAYER_ENTERED` ever reaches a cell trigger.** `foot.cpp:1425-1428` is the sole caller of `Spring(EventType, CELL)`:

```c
    TriggerClass* trigger = Map[Coord_Cell(Coord)].Get_Trigger();
    if (Cloak != CLOAKED && trigger && trigger->House == Owner()) {
        trigger->Spring(EVENT_PLAYER_ENTERED, Coord_Cell(Coord));
    }
```

Conditions: a `FootClass` (unit/infantry/aircraft, not buildings) completing `Per_Cell_Process`, not cloaked, and `trigger->House == Owner()` where `Owner()` is `House->Class->House` (`techno.cpp:1850-1853`). All other action code paths in the cell overload (`EVENT_TIME` decrement, etc.) are unreachable in practice.

### 5c. To an object -- trailing field on the object line

`Read_INI` order is fixed and load-bearing: TeamTypes, then Houses, then **Triggers**, then Map (`[CellTriggers]`), then Terrain, Units, Aircraft, Infantry, Buildings (`scenarioini.cpp:335-449`; duplicated at `:744-860`). Triggers must exist before anything can name one (`trigger.cpp:1016`).

| Section | Line format | Trigger field | Code |
|---|---|---|---|
| `[UNITS]` (`unit.h:198-201`) | `NNN=Housename,Typename,Strength,Cell,Facing,Missionname,Triggername` (`unit.cpp:3758`) | 7th | `unit.cpp:3809-3812` |
| `[INFANTRY]` (`infantry.h:228-231`) | `NNN=Housename,Typename,Strength,Cellnum,CellSublocation,Missionname,Facingnum,Triggername` (`infantry.cpp:2932-2933`) | 8th (**note: mission before facing here, unlike UNITS**) | `infantry.cpp:3017-3020` |
| `[STRUCTURES]` (`building.h:308-311`) | `NNN=Housename,Typename,Strength,Cell,Facing,Triggername` (`building.cpp:3325`) | 6th | `building.cpp:3392`, `:3402-3405` |
| `[AIRCRAFT]` (`aircraft.h:192-195`) | `NNN=Housename,Typename,Strength,Cell,Facing,Missionname` | **none** -- `AircraftClass::Read_INI` never reads a trigger (`aircraft.cpp:502-549`) | -- |
| `[TERRAIN]` (`terrain.h:156-159`) | header comment claims `cellnum = TypeName, Triggername` (`terrain.cpp:784`) but **the parser reads only the type** (`terrain.cpp:795-817`). `TerrainClass::Trigger` exists and is cleared by `TriggerClass::Remove` (`trigger.cpp:964-968`), but nothing ever sets it from INI. | -- | -- |

The pattern is identical in all three real cases: resolve by name, and if non-NULL, `AttachCount++`. Example (`building.cpp:3402-3405`):

```c
                        b->Trigger = TriggerClass::As_Pointer(trigname);
                        if (b->Trigger) {
                            b->Trigger->AttachCount++;
                        }
```

The trigger field is optional; `strtok` returning `NULL` yields `As_Pointer(NULL) == NULL` (`trigger.cpp:1227-1229`). Storage is `TriggerClass* Trigger` on `ObjectClass` (`object.h:113-117`, "the same trigger can be assigned to multiple objects"), initialised to 0 in the constructor (`object.cpp:344`).

### 5d. Hardcoded attachments (engine, not INI)

Scenario-specific fix-ups in `scenarioini.cpp:493-551` bypass the INI:
- `scb09ea`: attaches `"dely"` to the GDI Radar (`:507-511`), with `AttachCount++`.
- `scb13eb`: constructs a trigger named `"prod"` (`EVENT_PLAYER_ENTERED` / `ACTION_BEGIN_PRODUCTION` / `HOUSE_BAD`) and assigns `CellTriggers[276/340/404/468]` with four `AttachCount++` (`:518-533`). **It never sets `Map[cell].IsTrigger`**, so unless those cells already carried a trigger from `[CellTriggers]`, `CellClass::Get_Trigger` returns `NULL` and the trigger cannot fire. Also forces `"xxxx"` to `PERSISTANT` (`:535-537`).
- `scb13ec`: attaches `"delx"` to the GDI Radar (`:539-550`).
- `building.cpp:1963`: `Drop_Debris` spawns Chan on NOD mission 10 and attaches trigger `"win"` **without** `AttachCount++`.

---

## 6. Ordering and evaluation rules

### Where in the tick

One tick = one `Main_Loop` ⇒ `Logic.AI()` (`conquer.cpp:1653`). `LogicClass::AI` order (`logic.cpp:178-330`):

1. Crate regeneration (multiplayer).
2. **Team AI** -- `Teams.Ptr(index)->AI()` (`logic.cpp:195-197`).
3. **Object AI** -- every sentient object (`logic.cpp:203-217`). This is where object-attached triggers fire (damage, discovery, death, capture) and where `FootClass::Per_Cell_Process` fires **cell** triggers. The loop back-corrects `index` when the object count shrinks, so no object is skipped by a deletion.
4. Scan-bit accumulation passes for units / infantry / aircraft / buildings (`logic.cpp:222-260`). This is what `EVENT_*_DESTROYED` later reads.
5. `Map.Logic()`.
6. Factory AI.
7. **House AI** -- `hptr->AI()` for `HOUSE_FIRST..HOUSE_COUNT` in enum order in single-player (`logic.cpp:311-317`); multiplayer runs MULTI1..MULTI6, then NEUTRAL, then JP (`logic.cpp:287-309`).

So within one tick: **object and cell triggers fire before house triggers**, and houses are evaluated in `HousesType` order (GoodGuy, BadGuy, Neutral, Special, Multi1…Multi6).

`EndCountDown` is set to `TICKS_PER_SECOND * 30` at scenario start (`scenario.cpp:349`) and decremented once per frame (`conquer.cpp:1693-1694`), gating the three "all destroyed" events for the first 30 seconds (`house.cpp:1493`).

### Order within `HouseClass::AI`

`house.cpp:1447-1547`. A single pass over `HouseTriggers[Class->House]`; for **each trigger**, events are tested in this fixed order, and the first one that returns true issues `continue` for that trigger:

1. `EVENT_BUILD` (only if `JustBuilt != STRUCT_NONE`) -- `:1453`
2. `EVENT_EVAC_CIVILIAN` (only if `IsCivEvacuated`) -- `:1463`
3. `EVENT_NBUILDINGS_DESTROYED` -- `:1470`
4. `EVENT_NUNITS_DESTROYED` -- `:1477`
5. `EVENT_HOUSE_DISCOVERED` (only if `IsDiscovered`) -- `:1484`
6. *(inside `!EndCountDown`)* `EVENT_BUILDINGS_DESTROYED` -- `:1499`
7. `EVENT_UNITS_DESTROYED` -- `:1509`
8. `EVENT_ALL_DESTROYED` -- `:1519`
9. `EVENT_CREDITS` -- `:1528`
10. `EVENT_TIME` (only if `is_time`) -- `:1535`
11. `EVENT_NOFACTORIES` -- `:1543`

A trigger only responds to the one event it declares, so this ordering matters mainly for the **side effects on the first match**: `JustBuilt = STRUCT_NONE` (`:1455`) and `IsDiscovered = false` (`:1485`) are cleared by the *first* trigger that consumes them. If two triggers in the same house both watch `Built It` for the same structure, **only the first in list order fires**; the second sees `JustBuilt == STRUCT_NONE`. `IsCivEvacuated` is *not* cleared, so all `Civ. Evac.` triggers fire.

List order is INI entry order, since `Read_INI` walks `Entry_Count`/`Get_Entry` sequentially (`trigger.cpp:1026-1056`) and `Add` appends.

### Multiple matches and the removal hazard

`HouseTriggers[h]` is a `DynamicVectorClass`. `TriggerClass::Remove` calls `HouseTriggers[h].Delete(this)` (`trigger.cpp:975-977`), and `DynamicVectorClass<T>::Delete(int)` compacts the array by shifting every later element down one (`common/vector.h:671-688`). The `house.cpp:1447` loop increments `index` regardless. Consequence: **when a VOLATILE trigger springs and removes itself, the next trigger in the list is skipped for that tick.** It will be seen on the following frame.

`LogicClass::AI` compensates for this in the object loop (`logic.cpp:210-215`) but `HouseClass::AI` does not.

### Return value

All three `Spring` overloads return `true` for "this trigger matched and I ran the action", `false` for "not my event / condition not met yet / semi-persistent, still counting" (`trigger.cpp:358-360`, `:395-396`, `:530`). Note `true` is returned even when the action itself failed (`success == false`), so a failed `Reinforce.` still consumes the `continue` in the house loop and still clears `JustBuilt`/`IsDiscovered` if that was the matching event.

### Save / load

`TriggerClass::Code_Pointers` / `Decode_Pointers` convert only `Team` to/from a `TARGET` (`ioobj.cpp:343-374`). `AttachCount`, `Data`, `DataCopy` and `IsPersistant` are snapshotted raw, so a semi-persistent trigger resumes mid-count. House list membership is rebuilt by `TriggerClass::Load()` (`trigger.cpp:105-110`). Cell trigger pointers are coded per-cell (`iomap.cpp:193`, `:268`).

---

## 7. Compiler-target summary (the minimal contract)

A visual language compiling to TD triggers must emit, per trigger:

```
[Triggers]
<name≤4> = <EventText>,<ActionText>,<int>,<HouseIniName>,<TeamIniName|None>,<0|1|2>
```

and honour these hard constraints:

1. Name ≤ 4 chars, unique case-insensitively, and never `XXXX`/`YYYY`/`ZZZZ` unless you intend those to be destroy targets.
2. No spaces around commas.
3. Field 3 must be emitted even when unused (write `0`).
4. Route determines semantics. Pick the route from the Event: object events (`Player Enters` on objects, `Discovered`, `Attacked`, `Destroyed`, `Any`) ⇒ object overload; `Player Enters` on cells ⇒ cell overload; the twelve house events ⇒ house overload. Then use the §3 table, because `Production`, `Airstrike`, `Autocreate`, `Cap=Win/Des=Lose` and `Allow Win` all differ by route.
5. `Any` only works on the object route.
6. `Cap=Win/Des=Lose` only works on the object route, and only for `Destroyed`/`Player Enters`.
7. Semi-persistence (`1`) is a no-op on the house route, and is effectively broken on the cell/object route whenever the trigger also has a house (§4b). Prefer `0` or `2` and model repetition explicitly.
8. `Time` values are in tenths of a minute.
9. `Built It` data is a raw `StructType` ordinal.
10. `Win` does not win while any `Allow Win` trigger for that house still exists.
11. Do not emit two house triggers for the same house that both consume `Built It` or `House Discov.`; only the first in file order will see it.
12. Cell triggers: one per cell, cell number > 0, and the trigger's house must equal the entering unit's house.
13. Aircraft and terrain cannot carry triggers in this engine.