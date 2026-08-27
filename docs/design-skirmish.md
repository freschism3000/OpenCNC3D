# Skirmish vs AI: what the Remastered source gives us, and what CNC3D has to build

Investigation. Claims are read out of the source in `brain/vanilla/tiberiandawn`
(the checkout at `brain/patches/UPSTREAM.txt`, EA's GPL Tiberian Dawn with Vanilla-Conquer's
additions) or measured against the original data. Line numbers are that checkout's.

> **STATUS: BUILT.** This document was the investigation. The feature it describes has
> since been implemented on the `skirmish` branch: the brain patch, the lobby, the house
> colours, the opening camera, the refused-deploy cursor, the menu entry, the nine
> converted maps and five gates. Where the plan below and the shipped code disagree, the
> code wins and the changelog says what landed. Sections 6 and 7 are kept as written
> because the reasoning is still the reason each piece is the shape it is.

**This is no longer only a code read.** `brain/host/skirmish_probe.cpp` drives the shipped
`playable/TiberianDawn.dylib` through the exact call sequence in section 3, on the retail
skirmish map SCM01EA extracted from the 1995 disc, and everything below marked MEASURED came
out of running it. Where a run contradicted the first draft of this document, the run wins
and the correction is marked as one.

---

## 1. The verdict, first

**The skirmish AI is already inside the brain we ship, it is switched on, and it works.**
It is Red Alert's Expert System, ported into Tiberian Dawn by Vanilla-Conquer under
`USE_RA_AI` (`defines.h:56`, an unconditional `#define`), and it is the same AI the
Remastered Collection's skirmish runs. We do not have to write an AI and we must not: it is
brain, not eyes.

MEASURED, on the shipped dylib, with no rebuild and no client input of any kind:

```
tick    AI house (Multi2, human=0)      human house (Multi3, nothing driving it)
        bld unit inf  credits           bld unit inf  credits
    0     0    9   4     5000             0    8   4     5000
  500     2    8   4     4826             0    8   4     5000
 1500     5    8   5     2256             0    8   4     5000
 4000     7   11   5        0             0    8   4     5000
 5000     7   10   6        0             0    8   4     5000
```

The AI deployed its MCV into a Construction Yard around tick 325 and then built
`FACT -> NUKE -> PROC -> HAND + AFLD -> GUN -> second PROC` on its own, produced a
harvester, an APC, artillery and flamethrower infantry that appear in no INI, spent its 5000
credits to zero, and attacked. A 60,000 tick run reached 30 buildings, 43 units, 19 infantry
and 9 aircraft. The human house is the control: nothing drives it and it never built
anything, so the growth is the AI's and not the engine's.

**But a human-vs-AI skirmish cannot currently END. It crashes the process.** See 2.6. That
is the most important finding in this investigation and it was invisible to a code read.

**The human half works too, and the player's first click does not.** The full player loop
closes on the shipped dylib with no brain change: the sidebar populates, the MCV deploys,
production runs, buildings place, orders reach units, the economy earns. But on **7 of the 8
start positions** on SCM01EA the player's own escort is standing on the Construction Yard
pad, so the very first thing anyone does in a skirmish silently does nothing. See 3.2.

What is missing is otherwise all on our side:

1. **We never ask for it.** `game/cnc_eyes.cpp:11591` and `:11683` both call
   `CNC_Start_Custom_Instance(..., multiplayer = false)`, which sets
   `GameToPlay = GAME_NORMAL`, and every arm of the AI is gated on `GameToPlay != GAME_NORMAL`.
2. **We never call `CNC_Set_Multiplayer_Data`**, the only way to say "player 2 is a computer".
3. **The renderer decides GDI vs Nod by `strcmp(house, "GoodGuy")`.** In skirmish every house
   is `Multi1`..`Multi6`, so the player's own units draw as Nod. MEASURED on screen: see 6.2,
   and the symptom is not what this document first claimed.
4. **No skirmish map**, because the N64 port cut multiplayer. Solved: section 5. All nine
   retail skirmish maps now bake into valid CNC3D packs.

---

## 2. The AI, as it actually exists

### 2.1 How it switches on

```
house.cpp:929   if (!IsHuman && GameToPlay != GAME_NORMAL && (IsBaseBuilding || IQ >= Rule.IQProduction)) {
                    IsBaseBuilding = true;
                    IsStarted      = true;
                    IsAlerted      = true;
                }
```

`IQ` is set to `Rule.MaxIQ` in the HouseClass constructor (`house.cpp:520`) and both `MaxIQ`
and `IQProduction` default to 5 (`rules.cpp:108,110`), so `IQ >= Rule.IQProduction` is true
from frame one. An AI house starts base building with no INI, no trigger and no team type.
CONFIRMED at runtime: a hand-made INI with no `[Base]`, `[STRUCTURES]`, `[UNITS]`,
`[TeamTypes]` or `[Triggers]` still produced a six-building AI base.

`IsHuman` comes from us: `GlyphX_Assign_Houses` copies it from `MPlayerIsHuman[]`
(`dllinterface.cpp:997`), which comes from `CNCPlayerInfoStruct::IsAI`.

> **Correction.** The first draft cited `house.cpp:56` for the `USE_RA_AI` define. It is
> `defines.h:56`. It is also confirmed live in the dylib we ship, not merely present in the
> tree: `REMASTER_DEFS = _USRDLL REMASTER_BUILD` (`brain/vanilla/CMakeLists.txt:106`) and
> nothing switches `USE_RA_AI` off.

### 2.2 The think loop

```
house.cpp:1588   if (GameToPlay != GAME_NORMAL && IsHuman == false) {
house.cpp:1594       if (IsBaseBuilding && AITimer == 0) AITimer = Expert_AI();
house.cpp:1597       if (!IsBaseBuilding && State == STATE_ENDGAME) { Fire_Sale(); Do_All_To_Hunt(); }
house.cpp:1602       AI_Building(); AI_Unit(); AI_Vessel(); AI_Infantry(); AI_Aircraft();
                 }
```

`Expert_AI` (`house.cpp:5239`) is the strategist: it picks an enemy, tracks a state machine
(BUILDUP, BROKE, THREATENED, ATTACKED, ENDGAME) and scores urgencies for build-power,
build-defence, build-income, raise-money, raise-power, fire-sale and attack. `AI_Building`
(`house.cpp:6057`), `AI_Unit` (`house.cpp:6401`) and `AI_Infantry` (`house.cpp:6680`) write
`BuildStructure` / `BuildUnit` / `BuildInfantry`.

**The AI never designates an `Enemy` in a GlyphX skirmish**, and this degrades it rather than
stalling it. The enemy scan bails on the first non-allied active house that has not started
(`house.cpp:5285-5288`), and `HouseClass::Read_INI` creates HOUSE_GOOD/BAD/NEUTRAL/JP
unconditionally in every scenario, none of which is ever `IsStarted` on the GlyphX path.
HOUSE_GOOD is visited first, so `Enemy` stays `HOUSE_NONE` all match. The blast radius is
bounded: `Enemy` is read only for anti-air urgency (`house.cpp:6098`, with an all-houses
fallback at `:6310`) and infantry mix matching (`house.cpp:6790`, falling back to money and
base size). `AI_Attack` never consults it, which is why the AI still attacked in every run.

### 2.3 The build loop closes

Worth stating plainly, because a first read makes the port look stranded:
`Suggest_New_Object` and `Suggest_New_Building` appear to have no callers in `house.cpp`.
They are called from `building.cpp`.

```
building.cpp:1160   if (!House->IsHuman && Mission != MISSION_CONSTRUCTION && ...) {
building.cpp:1213       TechnoTypeClass const* techno = House->Suggest_New_Object(Class->ToBuild);
                        ... new FactoryClass; Factory->Set(*techno, *House); Factory->Start();
                    }
```

Every AI-owned building with `Class->ToBuild != RTTI_NONE` starts a factory itself, each
tick. There is a third gate inside that block which the ellipsis hides: production begins
only when `House->IsStarted && House->Available_Money() > 10`. `IsStarted` is the flag 2.1
sets, so the two are joined.

Placement closes in `BuildingClass::Exit_Object`:

```
building.cpp:2347   BaseNodeClass* node = Base.Next_Buildable(type);            // campaign base list
building.cpp:2378   coord = House->Find_Build_Location((BuildingClass*)base);   // multiplayer
```

In `GAME_NORMAL` the code prefers the scenario's `[Base]` node list; only outside
`GAME_NORMAL` does it fall through to `Find_Build_Location` (`house.cpp:4984`), the RA base
layout routine. So the AI lays out its own base only in skirmish, which is the mode we want.

`Find_Build_Location`'s last-ditch "try any zone" arm returns a `CELL` where the caller needs
a `COORDINATE`, so that arm can never succeed. It does not bite in practice (the
zone-directed arms work, as the runs show) but it means the AI has no recovery if its base is
fully boxed in.

### 2.4 The attack loop

`HouseClass::AI_Attack` (`house.cpp:5707`) uses no team types. It walks `Aircraft`, `Units`
and `Infantry` and re-missions armed objects to `MISSION_HUNT`. Two gates the first draft
left out: the whole routine is skipped on a 33% shuffle roll, and each object then rolls
`Percent_Chance(75)`, so the effective rate is well below "75% of everything". Afterwards
`Attack = Rule.AttackInterval * Random_Pick(TICKS_PER_MINUTE/2, TICKS_PER_MINUTE*2)`.

This matters because a skirmish INI has no `[TeamTypes]` and no `[Triggers]`. The campaign
AI's attack waves come from team types; the skirmish AI's come from here. MEASURED: combat
happened, both sides lost objects, and the AI eventually destroyed the passive human house.

### 2.5 Starting forces

`Create_Units` (`scenarioini.cpp:1401`) runs for every multiplayer house. Under
`USE_GLYPHX_START_LOCATIONS` (`scenarioini.cpp:1335`) it places each house at
`waypts[hptr->StartLocationOverride]` (`scenarioini.cpp:1567`). With bases on, `USE_RA_AI`
gives **the AI an MCV too** (`scenarioini.cpp:1666-1682`); without that define only humans
got one.

Two conditions the first draft left out: `Create_Units` runs only when `!Debug_Map`, and the
MCV block only when `MPlayerBases` is set, so with bases off nobody gets an MCV and the match
is a units-only brawl. And `waypts[StartLocationOverride]` is an **unchecked index**; see
section 4 for why that is not academic.

The Remaster's own balance note, which is a comment and not a mystery:

```
scenarioini.cpp:1643   scaleval = 1;  // Set to 1 since EA QA can't beat skirmish with scaleval set higher.
```

The AI used to get three times the starting units. It gets the same as the player.

### 2.6 Winning and losing: THE BLOCKER

The paths exist. One of them crashes the process.

```
house.cpp:4086   sprintf(txt, Text_String(TXT_PLAYER_DEFEATED), MPlayerName);
```

`Text_String` is `Extract_String(SystemStrings, n)` (`function.h:940`), and `Extract_String`
returns `nullptr` the moment `SystemStrings` is NULL (`common/dipthong.cpp:313`). A NULL
format string to `sprintf` is an immediate segfault. `SystemStrings` **is** NULL in this
brain: it is loaded only at `init.cpp:288-291`, inside the `Init_Game` path, which `CNC_Init`
never reaches. `FontPtr`, `Font8Ptr`, `Green12FontPtr` and `ScoreFontPtr` are NULL for the
same reason. The same NULL format appears again at `house.cpp:4102` and `:4107`.

MEASURED, twice, from two different working directories with two different content sets,
crashing on the identical tick:

```
*** SIGNAL 11 during tick 59584 ***
3   libsystem_c.dylib   vsprintf_l + 184
4   libsystem_c.dylib   sprintf + 160
5   TiberianDawn.dylib  HouseClass::MPlayer_Defeated + 573
6   TiberianDawn.dylib  HouseClass::AI + 445
7   TiberianDawn.dylib  LogicClass::AI + 938
8   TiberianDawn.dylib  CNC_Advance_Instance + 1022
```

`p (void*)SystemStrings` at that stop is `0x0`. Dropping a loose `CONQUER.ENG` into the
working directory does not fix it, it moves the crash earlier into `String_Pixel_Width`,
because the fonts are still NULL.

Who crashes and who does not: `house.cpp:4078` guards on `PlayerPtr == this` and
`house.cpp:4101` on `IsHuman`, so **a defeated AI house is safe and a defeated human house
kills the process.** Player wins, fine. AI wins, crash. That is exactly the wrong way round
for a first playable build.

`MPlayer_Defeated` is the only route to `PlayerWins`/`PlayerLoses` (`house.cpp:4178`), which
is the only route to `On_Multiplayer_Game_Over` (`dllinterface.cpp:2532`), which is the only
source of `CALLBACK_EVENT_GAME_OVER`. **The whole of 6.4 is unreachable until this is
fixed.** The fix is a NULL guard on three `sprintf` calls, and it is Win98 safe.

Two further corrections to the first draft:

- `Check_Pertinent_Structures` (`house.cpp:4788`) does not call `MPlayer_Defeated`. It calls
  `Flag_To_Die()` (`house.cpp:4840`), and the defeat lands about a second later at
  `house.cpp:963-971` when `IsToDie` expires. It is gated on `Special.IsEarlyWin`, which we
  set through `CNCMultiplayerOptionsStruct::DestroyStructures` (`dllinterface.cpp:728`).
  **Bases OFF plus DestroyStructures ON kills every house one second into the match**,
  because no house then has a building or an MCV. Those two options must be interlocked in
  the setup screen.
- The attrition test at `house.cpp:1442` mixes scan kinds: `ActiveBScan`/`ActiveAScan`/
  `ActiveIScan` exclude limboed objects but plain `UScan` does not, so a house whose last
  surviving object is limboed is never attrited. And its `Frame > 0` guard is defeated after
  the first scenario in a process, because `Frame` is reset only in `Select_Game`, which the
  DLL path never runs.

### 2.7 What the AI does NOT have

- **No difficulty setting.** `CNC_Set_Difficulty` returns immediately unless
  `GameToPlay == GAME_NORMAL` (`dllinterface.cpp:1843`). Multiplayer houses get
  `Assign_Handicap(DIFF_NORMAL)` and nothing changes it. If we want Easy/Hard we are
  inventing a mechanic. The honest levers that already exist are `BuildLevel`, starting
  credits and `Rule` values through `CNC_Config`.
- **No ganging up.** `Computer_Paranoid`, which makes computers turn on humans together,
  returns immediately in `GAME_GLYPHX_MULTIPLAYER` (`house.cpp:7996`, with EA's own comment
  saying so). Multiple AIs fight each other and the player independently.
- **No base-node list.** The AI's layout is `Find_Build_Location`'s spiral, so a skirmish base
  looks like an RA base. That is what the Remaster ships.

---

## 3. The exact call sequence, now executed

Order matters: `CNC_Start_Custom_Instance` reads the multiplayer globals while it parses the
INI, so the data must be in place first. This sequence has been run and works verbatim.

```c
CNC_Init("", event_callback);            /* once per process, as today */
CNC_Config(rules);                       /* all biases 1.0f, as cnc_eyes already does */

CNCMultiplayerOptionsStruct opts = {0};  /* memset first */
opts.MPlayerBases      = 1;      /* MCV + base building. 0 = units only, see the interlock in 2.6 */
opts.MPlayerCredits    = 5000;
opts.MPlayerTiberium   = 1;      /* ALSO sets Special.IsTGrowth/IsTSpread, which LEAK. See 6.9 */
opts.MPlayerGoodies    = 0;      /* crates */
opts.MPlayerUnitCount  = 10;     /* only used when MPlayerBases == 0 */
opts.EnableSuperweapons = true;
opts.DestroyStructures = true;   /* -> Special.IsEarlyWin */
opts.ModernBalance     = false;  /* keep 1995 balance */

CNCPlayerInfoStruct players[2] = {0};
/* [0] MUST be the human and MUST carry GlyphxPlayerID 0: every CNC3D call passes
       player_id 0 and Set_Player_Context matches on that id. */
players[0].GlyphxPlayerID = 0; players[0].House = GDI; players[0].ColorIndex = 0;
players[0].Team = 0; players[0].StartLocationIndex = 0; players[0].IsAI = false;
players[1].GlyphxPlayerID = 1; players[1].House = NOD; players[1].ColorIndex = 1;
players[1].Team = 1; players[1].StartLocationIndex = 1; players[1].IsAI = true;

CNC_Set_Multiplayer_Data(scenario_index, opts, 2, players, 6);
CNC_Start_Custom_Instance(content, dir, "SCM01EA", /*build_level*/ 7, /*multiplayer*/ true);
```

Facts behind that block:

- `dir` **must end in a slash**: the engine does `snprintf("%s%s.INI", directory_path,
  scenario_name)` with no separator (`dllinterface.cpp:1409`).
- `MAX_PLAYERS` is 6 (`defines.h:2565`), `MPLAYER_NAME_MAX` is 12 (`defines.h:2586`).
- `IsAI = true` sets `MPlayerGhosts = true` for you (`dllinterface.cpp:756`), which keeps
  computer houses alive through the "remove unused AI players" pass.
- `DLLExportClass::Init()` runs inside `CNC_Set_Multiplayer_Data`, so there is no separate
  init call to sequence.
- **The player does not always get Multi1.** `GlyphX_Assign_Houses` (`dllinterface.cpp:904`)
  picks the `HOUSE_MULTI*` slot with `Random_Pick`. MEASURED: the human landed on Multi3 and
  the AI on Multi2 in every run. Nothing may assume Multi1 is the player. The `player=1` flag
  on the `HOUSE|` dump line is the correct join key and the renderer already uses it.
- `StartLocationIndex` indexes the map's compacted waypoint list; `0x7f` means random
  (`dllinterface.cpp:94`). Set it explicitly for every player: that avoids the quirk at
  `dllinterface.cpp:766` and removes the `rand()` shuffle at `:954`.
- All four exports we need are already in the shipped dylib. **No brain rebuild is needed to
  START a skirmish.** Three brain changes are needed to SHIP one; see 6.10.
- Passing `player_id = 0` works: `CNC_Advance_Instance` maps 0 to `GlyphxPlayerIDs[0]`
  (`dllinterface.cpp:1566`) and every `Get_*_State` calls `Set_Player_Context(player_id)`,
  which matches `GlyphxPlayerIDs[0] == 0`. MEASURED: `CNC_Advance_Instance` never returned
  false in a clean run. Note that 0 is **also** the engine's "no player" value, which breaks
  the game-over recipe in 6.4.

### 3.1 The skirmish is already deterministic, and that was a surprise

> **Correction, MEASURED.** The first draft said a skirmish is not reproducible because
> `GlyphX_Assign_Houses` uses `Random_Pick` and `Seed = timeGetTime()`. Both halves are true
> and the conclusion is wrong: the two are not connected on our path.

`Seed` is copied into the generators only by `Init_Random` (`init.cpp:2547`), whose only
caller is `Select_Game` (`init.cpp:1354`), which the DLL path never runs.
`CNC_Start_Custom_Instance` has its `Start_Scenario` call commented out
(`dllinterface.cpp:1437-1441`). So `RandNumb` sits at its static initialiser `0x12349876`
(`common/irandom.cpp:37`) for the whole process and `Scen.RandomNumber` starts at 0.

MEASURED: four separate 9,000 tick processes produced byte-identical logs, and two 60,000
tick processes from different working directories with different content sets produced
byte-identical logs and crashed on the same tick.

> **Refinement, MEASURED.** The **sim** is reproducible; the **callback stream** is not.
> `IRandom()` is libc `rand()`, and `GlyphX_Assign_Houses` calls `srand(timeGetTime())`
> (`dllinterface.cpp:918`). Over 30 repeat runs, 5 fired an AI-taunt MESSAGE callback, each at
> a different tick with a different index (ticks 292/651/675/679/874, ids 2/4/6/10/11). A gate
> that diffs whole logs will flap on those lines; a gate that diffs sim state will not.

Consequences: the existing two-run determinism gate can be pointed at a skirmish map if it
ignores the message callbacks. **Drop the `CNC3D_Set_Seed` idea rather than deferring it**:
`Seed` is written and never read on our path, so there is nothing to pin. This determinism is
**accidental**, depending on `Start_Scenario` staying commented out, so a gate should assert
it rather than assume it.

### 3.2 The human half works, and the opening click does not

Six lanes proved the AI builds a base and not one of them ever issued a player command.
Driving the player side through the API, MEASURED:

| step | verdict | evidence |
|---|---|---|
| `GAME_STATE_SIDEBAR` for player 0 | WORKS | returns TRUE from tick 0; `EntryCount=[0,0]` while the human owns no buildings, `[4,0]` the moment the Construction Yard exists, `[10,4]` later, and it is the human's own **GDI** list while the AI is Nod |
| deploy the MCV | **blocked, see below** | both routes select correctly; the deploy itself is `COMMAND_AT_POSITION` on the MCV's own pixel |
| start production | WORKS | a real `FactoryClass` starts and `Progress`/`Completed` advance |
| place the building | WORKS | `GAME_STATE_PLACEMENT` + `SIDEBAR_REQUEST_PLACE` puts a second and third human building on the map |
| unit production | WORKS | infantry leaves the factory with no client PLACE call |
| move and attack orders | WORKS | `Mission`, `NavCom` and `TarCom` all change |
| shroud | WORKS | `GAME_STATE_SHROUD` returns TRUE, only the human start area explored |
| economy | WORKS | refinery, free harvester, 700 tiberium banked. The money lands in `Tiberium`/`CreditsCounter`, not `Credits` |
| sell, band-select | WORKS | on the human MULTI house |

So the plan needs **no fifth phase** for the player-command surface. It does need one work
item it did not have, and it is the first thing a player ever does.

**The opening click.** `UnitClass::What_Action` downgrades `ACTION_SELF` to
`ACTION_NO_DEPLOY` unless `Legal_Placement` succeeds at the cell NW of the MCV
(`unit.cpp:3686-3691`). The Construction Yard pad is **3x3**, not 2x2: `STRUCT_CONST`'s
`OccupyList` is `List32 = {0,1,2,MCW,MCW+1,MCW+2}` (`bdata.cpp:123`, referenced at `:581`),
three wide by two tall, and `Legal_Placement` uses `Occupy_List(true)` which appends the bib
row. MEASURED across all eight start positions of SCM01EA at `MPlayerUnitCount = 10`:
**seven of eight report `ACTION_NO_DEPLOY` and produce zero buildings from the first click.**
Only start 5 is clear. Confirmed independently from our own tick-0 dump: the human's E1
escort stands at (4,19), inside the pad (4,18)..(6,20).

Three things make it worse than a one-off:

- **The escort size silently decides whether the first click works**, and it was measured
  again after this document was written, across all nine shipped maps at both start
  positions. **No value is safe everywhere.** An escort of 0 leaves 17 of 18 starts
  deployable, 1 leaves 10, 2 leaves 9, 5 leaves 4, and 6 or more leaves none at all. The
  earlier claim here that "at 0 or 1 every position tested deploys" was measured on one map
  and is wrong: at 1, eight of the eighteen starts are dead. The shipped default is 0 and
  gate G76 asserts the 17.
- **Retrying does not help for about 140 ticks.** Ordering the blockers away and clicking
  again fails, because the escort keeps driving across the pad: measured `ACTION_NO_DEPLOY`
  for 140 consecutive ticks, flipping to `ACTION_SELF` only at tick 142.
- **The renderer confirms an order the engine will not execute.** `game/cursors.h` has no
  `case CUR_ACTION_NO_DEPLOY` at all (verified: zero occurrences), so the cursor stays the
  plain arrow; and `ui_order_at` in `cnc_eyes.cpp` computes
  `refused = vis && !ctrl && (probe == 0 || probe == 2)`, which does not include 21, so it
  paints the green order-accepted mark and logs MOVE. The engine meanwhile reaches
  `case ACTION_NO_DEPLOY: Speak(VOX_DEPLOY); break;` (`foot.cpp:1290`) and does nothing, and
  CNC3D plays no audio for it. **That is a legibility-rail violation: the client tells the
  player the order was heard.**

### 3.3 Nothing takes effect before the first tick

MEASURED with zero ticks run: `CNC_Select_Object` returns TRUE while `CNC3D_Sel_Info`
reports 0 selected, `CNC3D_Probe_Object_At` returns `ACTION_NONE`, and
`GAME_STATE_PLAYER_INFO` gives `SelectedID = -1`. One tick later all three are correct.

`Set_Selected_By_Player` adds into `CurrentObject`'s bucket for the owner house
(`object.cpp:1619-1631`) while every reader uses the **Active** bucket, which is still the
constructor default `HOUSE_FIRST`. `Reset_Player_Context` never calls `Set_Active_Context`,
and `Set_Player_Context` early-returns when the index has not changed, so no client call can
establish it. Vanilla-Conquer's own fix for exactly this is compiled out of our build
(`scenarioini.cpp:409-411`, `#ifndef REMASTER_BUILD`). What repairs it in practice is
accidental: the first `Logic.AI()` switches context away and back.

Today the window is one frame because cnc_eyes starts unpaused. Any "start paused", briefing
screen, or client-side pre-selection of the MCV would silently do nothing.

---

## 4. What a skirmish INI looks like

From the retail `SCM01EA.INI`, inside `GENERAL.MIX`:

```ini
[Basic]
BuildLevel=1          ; we override with the build_level argument
Player=Multi1
Name=GREEN ACRES

[MAP]
Height=49 Width=58 Y=11 X=3
Theater=TEMPERATE     ; NOTE: Theater lives in [MAP], not [Basic]

[Multi1] .. [Multi6]  ; MaxBuilding/MaxUnit/Edge/Credits/Allies/FlagHome/FlagLocation
[Base]                ; empty
[TERRAIN] [OVERLAY] [SMUDGE]
[INFANTRY] [UNITS] [STRUCTURES]   ; all three EMPTY on all nine maps
[Waypoints]
```

> **Correction.** The first draft's sample put `Theater=` under `[Basic]`. The engine reads
> it from `[MAP]` (`display.cpp:1291`). This matters for section 5.5, which tells you to
> hand-author an INI: `n64_terrain.py` matches `THEATER=` anywhere in the file regardless of
> section, so a hand-authored DESERT map with `Theater` in the wrong section would bake a
> DESERT pack and load as TEMPERATE in the engine, and nothing cross-checks the two.

> **Correction.** The first draft said waypoints "0..7 are the eight start positions". That
> holds on five of the nine maps and fails on four. `Create_Units` compacts waypoints 0..25
> only, into `CELL waypts[26]` (`scenarioini.cpp:1446`), and indexes it with
> `StartLocationOverride` **with no bound check** (`:1567`). The legal range is the count of
> valid waypoints below 26, which differs per map: SCM04EA has 0,1,2,3,4,5,17 so index 6
> lands on an interior marker; SCM05EA lacks 6; SCM06EA lacks 7; SCM07EA has 0..9. Separately
> the engine's own random assignment can never use waypoints 6 or 7 even where they exist,
> because `GlyphX_Assign_Houses` admits a waypoint only when `i < MAX_PLAYERS` and
> `MAX_PLAYERS` is 6. A setup screen offering "start position 1..8" would be wrong on four of
> the nine maps.

Starting forces come entirely from `Create_Units`, not from the INI. Every `[MultiN]` carries
Capture-the-Flag data and an `Allies=` line, and `HouseClass::Read_INI` reads `Allies=` only
in `GAME_NORMAL` (`house.cpp:1927`), so it is inert in a skirmish.

---

## 5. The maps: solved

### 5.1 The problem

CNC3D draws terrain from the cartridge's own `<SCEN>.MAP` (64x64 big-endian u16 N64 tile
IDs), which `n64_terrain.py` turns into `terrain_<SCEN>.json` and `bake5.py:1509` turns into
`<SCEN>.pack`. The ROM holds 79 `.MAP` files and none is an `SCM*`, and there is no
`SCM*.IMG` heightmap or `CMM*.IMG` tint map either. The N64 port cut multiplayer.

### 5.2 The converter, and the proof it is faithful

`<THEATER>.TL4`/`.TL8` give, for each N64 tile slot, the PC `(template, icon)` pair it was
made from. Inverting them gives PC pair to N64 tile ID. `tools/bin_to_n64map.py` does that,
and `--selftest` validates it against ground truth:

```
SELFTEST: PC .BIN -> N64 .MAP against the cartridge's own maps
  scenarios converted : 78
  reproduced exactly  : 78
  cells identical     : 319488 / 319488 (100.0000%)
  excluded (our own hand-authored .MAP, not cartridge data): SCG90EA
  VERDICT: PASS
```

Every genuine cartridge map is reproduced bit for bit from its PC `.BIN`. On the nine retail
skirmish maps seven are exact and two lose three cells each to road templates the cartridge
has no tile for at all (`TEMPLATE_ROAD43` = 135 in TEMPERATE on SCM03EA, `TEMPLATE_ROAD33` =
125 in DESERT on SCM06EA): **9265 of 9271 non-clear cells, 99.94%**. The tool refuses to
write a map containing a silent substitution unless `--allow-substitutions` is passed.

### 5.3 The whole pipeline runs, and all nine packs exist

MEASURED. The three-step pipeline runs on this machine today and all nine skirmish maps have
been taken end to end into valid `CNC3DPKE` packs, both theaters. The harness was proved
faithful the only way that counts: **baking SCG01EA through the same driver reproduces
`playable/SCG01EA.pack` byte-identically** (md5 `9e80b22f446f47cc4437c19e82747256`).

```
SCM01EA.BIN (GENERAL.MIX)
   -> tools/bin_to_n64map.py     validated 78/78; --allow-substitutions for SCM03EA, SCM06EA
   -> n64_terrain.py <SCEN>      unedited; resolves all nine, both theaters, unmapped=0
   -> bake5.py <SCEN>            unedited; 5.5 s each; FLAT.IMG/CMFLAT.IMG fallback observed
   -> <SCEN>.pack
```

`n64_terrain.py`'s `share/CNC3D` tree is a set of relative symlinks into
`tools/bakery/sharecopy` and `data/rom`, so it runs from a plain checkout with no SMB mount.
The one real gap is small: `n64_terrain.py` reads the theater from `EX/INI/<SCEN>.INI` and no
`SCM*.INI` exists in the extracted tree, so the nine INIs must be staged out of `GENERAL.MIX`
first (about 30 KB, one command, into a git-tracked directory).

**A skirmish pack differs from a campaign pack in exactly three things**, checked against
every use of the scenario name inside `build()`: the terrain cells, the 65x65 heightmap, the
65x65 CM tint, plus the 16-byte name field. The other 7.8 MB (220 meshes, 293 textures,
sprites, infantry, animation, water, seabed) is identical across every scenario. There is no
fourth missing thing waiting to be discovered.

### 5.4 Flat ground is not a skirmish problem

> **Correction.** The first draft framed flat terrain as the price of skirmish maps. In fact
> only 55 of the 79 campaign scenarios ship a heightmap and only 53 ship a CM map. **The
> other 24 already take the identical `FLAT.IMG`/`CMFLAT.IMG` path**, and their packs are
> already built and shipped in `playable/`. A skirmish map lands in a visual condition that
> already ships. It still earns a known-gap entry, but as a pre-existing property of
> the port, not new debt that skirmish introduces.

### 5.5 The cheapest first map, and a better one

For the vertical slice, do what `SCG90EA` already does (`game/missions/make_testmap.py`):
author a skirmish INI over an existing campaign `.BIN` and borrow that scenario's pack with
`--pack`. Real cartridge terrain, real elevation, zero new tooling. Mind the `Theater=`
section and the waypoint indices above, and note that **nothing cross-checks a borrowed
pack's theater against the scenario's**.

If instead a retail map is used, do not pick SCM01EA to prove the pipeline: it is the only
one of the nine with no water, so it exercises the least. Pick one with water, and a DESERT
one.

---

## 6. The CNC3D work list

In dependency order. **6.13 is the highest player-visible severity of the lot** despite
sitting last; it is numbered late only so the cross-references above stay valid.

### 6.1 Ask for a multiplayer game  (`game/cnc_eyes.cpp`, `game/cnc_game.h`)

`GameOpts` gains a skirmish block; `boot_brain()` calls `CNC_Set_Multiplayer_Data` then
`BrainStart(..., true)`. One new `dlsym`: `CNC_Set_Multiplayer_Data`. Both restart arms of
`boot_brain` (first mission, and the re-arm at `cnc_eyes.cpp:11591`) must set the data again.
`game_shutdown` and `game_loop`'s body reuse unchanged; `game_boot` does not.

### 6.2 House colour  (the bug a player sees first)

`!strcmp(o.house, "GoodGuy")` decides the house colour table, the radar blip, the death
debris palette and more, at `cnc_eyes.cpp:2725`, `:2735`, `:2856`, `:3354`, `:3360` and
`:8977`. In skirmish all of them go false.

> **Correction, MEASURED on screen.** The symptom is not the "Nod blue-grey" the first draft
> described. `mesh_house` (`cnc_eyes.cpp:3352-3354`) selects between two **baked texture
> sets**, so an ex-GDI vehicle comes back in the cartridge's Nod **red-and-white** livery; the
> blue-grey at `:3363` is only the fallback-box tint for objects with no mesh. A
> `Player=Multi1` scenario rendered against a GoodGuy control differs by 1891 pixels.
> Corollary: in skirmish the pack's GDI texture variants are never bound at all, so the sand
> table is dead art.

The first draft's list was also incomplete. It misses the **infantry uniform row**, the
**construction animation**, the **band-select rectangle**, and the radar, where every blip
turns light blue rather than merely losing GDI gold.

The fix is to ask the engine instead of parsing a name. Add one field to the OBJ line in
`brain/patches/vanilla-cnc3d.patch`:

```c
int act = -1;
if (obj->Is_Techno()) act = (int)((TechnoClass*)obj)->House->ActLike;   /* HOUSE_GOOD or HOUSE_BAD */
... "|act=%d"
```

`ActLike` is set for multiplayer houses by `Init_Data` (`house.cpp:4601`) from the
`CNCPlayerInfoStruct::House` we pass. The OBJ parser is key=value with defaults
(`cnc_eyes.cpp:2675`), so `field(f, nf, "act", -1)` degrades cleanly on an older brain. See
6.10 for the CI guard this trips.

The cartridge has only two house tint tables, so six-player colours are not on the table, and
a GDI-vs-GDI skirmish would be two visually identical armies that `act=` cannot separate.

### 6.3 Sidebar side  (`cnc_eyes.cpp:14553`)

`hk.nod = (scen[2] == 'B')`. `SCM01EA` has `M` there, so a Nod player gets the GDI bezel. It
has a second consumer the first draft did not name: the 640 HUD's faction emblem. Take the
side from `GameOpts.side` when `skirmish` is set; keep the scenario-letter rule for campaign.

### 6.4 Game over  (blocked on 6.10 until the brain is patched)

`cnc_eyes.cpp:351` is `if (!e.GameOver.Multiplayer)` with no else, so a skirmish that ends
falls through and the shell treats it as `GAME_EXIT_ERROR`, quitting the whole application
with rc=1. Add the else arm: read the player's row from `MultiPlayerPlayersData[]`, set
`g_gameOver.win = row.IsWinner`, play `ACCOM1.AUD`/`FAIL1.AUD`, run the existing verdict
banner, return to the menu.

> **Warning.** Do not match that row on `GlyphXPlayerID == 0`. Zero is the struct's default
> and the engine's "no player" value, so it matches seven rows instead of one. Match on the
> house instead, or give the human a non-zero id and thread it through every call rather than
> relying on the 0 shortcut.

### 6.5 The menu  (`menu/dosmenu.c:61`, `app/cnc3d.cpp:987`)

`DM_MULTI` already exists, drawn disabled the way the 1995 engine drew an unavailable gadget.
Flip `dm_items[DM_MULTI].disabled` to 0 and add an arm. Reuse `DM_MULTI` rather than adding a
ninth item: a ninth would collide with the version line. The arm to copy is **Special Ops,
not Test Map**, and `g_camp.active` must be 0.

The setup screen is the long pole. The faithful model is `Com_Scenario_Dialog` in
`nulldlg.cpp` (the skirmish one), not `Net_New_Dialog`. `menu/dosops.c` gives a list and a
plate for free and nothing else: `menu/` contains no gauge, check list, drop list, edit box,
slider or colour row, so every option widget is new work.

### 6.6 Map install

`make-build.sh` copies every `.pack` from the bakery output directory, so the nine appear
there automatically; a second named-list destination needs editing by hand. Both packagers
already ship `missions/` by wildcard. `tools/win/check-sources.sh` guards **source files
only**, so it would not catch a missing data file.

### 6.7 Determinism

Superseded by 3.1. Do not build `CNC3D_Set_Seed` to write `Seed`; it would do nothing.
Phase 1 gets the free rule (explicit `StartLocationIndex` for every player) and a gate that
**asserts** the accidental determinism rather than assuming it.

### 6.8 Save and load  (silent data loss, must be in Phase 1)

`game_load_slot` calls `BrainSaveLoad(false, path, "GAME_NORMAL")` with the string hardcoded
(`cnc_eyes.cpp:14366`). `CNC_Save_Load` uses that string to **set** the mode, not to check it
(`dllinterface.cpp:1801-1810`), and the save file does not carry `GameToPlay`. So loading a
skirmish save **succeeds** and drops the match into `GAME_NORMAL`: the AI goes inert
(`house.cpp:1588`), the attrition test stops, and `Get_Sidebar_State` switches to the
never-populated `Map.Column[]` branch so the build list empties. The slot gate only compares
the scenario name, which matches. Save and Load sit on the pause dialog of **every** mission
(`game/dosopt.h:25`), so this is live the moment skirmish boots.

Either thread the real mode through, or disable Save and Load in skirmish for v1.

### 6.9 Globals that leak across a mode switch

> **Correction, MEASURED.** The first draft said returning to a campaign mission after a
> skirmish is safe. It is not. `CNC_Set_Multiplayer_Data` writes `Special.IsTGrowth` and
> `Special.IsTSpread` from `MPlayerTiberium` (`dllinterface.cpp:733`). `MapClass::Logic`
> reads them **with no `GameToPlay` guard** (`map.cpp:1363`), and nothing restores `Special`:
> `Clear_Scenario` does not touch it. A skirmish played with tiberium off permanently
> disables tiberium growth for every campaign mission afterwards in the same process.
> Isolated with two otherwise identical runs: tiberium-off skirmish then campaign gives
> 49 -> 38 cells over 3000 ticks; tiberium-on gives 49 -> 55; campaign alone gives 49 -> 56.

The sidebar half of the original claim does hold: `Get_Sidebar_State` branches on
`GameToPlay` (`dllinterface.cpp:4098`) so `Map.Column[]` and `MultiplayerSidebars[]` never
cross. But `Special`, `Rule.AllowSuperWeapons`, `BuildLevel` and `MPlayerCount` all survive
the switch and need an explicit reset.

### 6.10 The brain patch is the real critical path

Three brain changes are now known to be required, and nobody had costed them:

1. the `act=` field (6.2), which blocks Phase 1;
2. the NULL-format guard at `house.cpp:4086/4102/4107` (2.6), which blocks Phase 2 entirely;
3. optionally the seed export (3.1).

Each carries the same fixed overhead, so batch them into one commit:

- `brain/patches/vanilla-cnc3d.patch` must be regenerated in the same commit or
  `tools/check-brain-checkout.sh` fails, and CI runs it on every push
  (`.github/workflows/build.yml:60-61`).
- There is a **second** CI guard at `build.yml:65-72` that greps `dllinterface.cpp` for
  `|<field>=%d` over the literal list `dimw makecnt dostage sel nav tar`. `act` must be added
  to that list, or the guard passes while a stale checkout silently drops the new field,
  which is precisely what the guard exists to catch.
- Both the Mac dylib and the Windows DLL must be rebuilt from the same commit (rule 4).

### 6.11 Smaller, but visible in the first minute

- **The opening camera lands on the map centre, not the player's base, on a black screen.**
  The renderer takes the camera from the brain's `VIEW|` line, which prints `Scen.Views[0]`.
  Nothing writes the local player's start position there in multiplayer:
  `Calculate_Start_Positions` fills `MultiplayerStartPositions[]` and returns,
  `CNC_Set_Home_Cell` is never dlsym'd by cnc_eyes, and `DisplayClass::Read_INI` sets all four
  Views to waypoint 26. MEASURED on SCM01EA: `VIEW|32|35` while the human's MCV is at cell
  (5,19), about 43 cells away, on unexplored and therefore black ground. Fix with
  `CNC_Get_Start_Game_Info` or `CNC_Set_Home_Cell`, both already exported.
- **The AI's EVA voice plays to the player.** `ev_cb` never reads `GlyphXPlayerID`, so the
  computer's speech and sound effects are heard as if they were the player's own.
- **Computer messages** (`MESSAGE_TYPE_COMPUTER_TAUNT`, `MESSAGE_TYPE_PLAYER_DEFEATED`) are
  the one skirmish-only engine event the renderer drops on the floor.
- **`g_playerHouse` is never cleared between missions**, and the renderer has no ally concept:
  anything not yours is hostile.
- `HOUSE| buildings=` is trustworthy for an AI-builds gate. `HOUSE| units=` is not, because
  aircraft on an airstrip are limboed in and out of the count.
- **Operational trap for anyone writing a gate:** `CNC_Init` calls
  `Read_Scenario_Descriptions`, which readdir-scans the working directory once per candidate
  filename. Run from `playable/` (350 entries, 1.3 GB) that scan takes minutes and looks
  exactly like a hang. Run probes from a small directory.

### 6.12 New gates

- **G-skirmish-boot**: start a skirmish, advance, assert objects owned by two different
  `Multi*` houses.
- **G-skirmish-ai-builds**: advance to about 4000 ticks and assert the AI house's
  `buildings=` grew past its start count. This is the only gate that proves the AI is alive;
  everything else can pass with a dead AI. Baseline from the measured run: 7 buildings by
  tick 4000.
- **G-skirmish-colour**: pixels, per rule 7. The `Player=Multi1` trick in 5.5 gives this a
  target on day one, before any multiplayer code exists.
- **G-skirmish-verdict**: blocked on 6.10.
- **G-skirmish-determinism**: two runs, identical logs, asserting 3.1 rather than trusting it.

Note for whoever writes these: `HOUSE|` only reaches stdout through the `objdump` script verb.
Do **not** use the AI house's sidebar as a control: MEASURED, an AI house's sidebar is
`[0,0]` forever while it grows to six buildings, because `BuildingClass::Unlimbo` reveals an
AI house's own building through `Revealed(House)` (`building.cpp:1433`) and
`TechnoClass::Revealed` sets `IsDiscoveredByComputer` instead of the per-player mask
(`techno.cpp:589-598`), so `Update_Buildables`' `Is_Discovered_By_Player()` gate
(`building.cpp:2435`) is false. Decisive A/B on one bit: `IsAI = false` gives `[4,0]`,
`IsAI = true` gives `[0,0]`.

### 6.13 The opening click (diagnosis in 3.2; this is the work)

Four small fixes, all in the eyes, none needing the brain:

1. `game/cursors.h`: add `case CUR_ACTION_NO_DEPLOY` and return `MOUSE_NO_MOVE`. There are
   currently zero cases for it, so the cursor is the plain arrow.
2. `cnc_eyes.cpp` `ui_order_at`: add 21 to the `refused` test, which is today
   `probe == 0 || probe == 2`. Without this the client paints the green order-accepted mark
   for an order the engine discards, which is a legibility-rail violation.
3. Either default `MPlayerUnitCount` to 0 or 1, or have the client scatter the escort off the
   3x3 pad at match start. MEASURED: at 0 or 1 every position tested deploys on the first
   click; at 4 every position tested is blocked; at 10 it is 7 of 8.
4. Drive the opening camera from the player's start position rather than `Scen.Views[0]`
   (6.11), because both faults land on the same first frame: the view is 43 cells from the
   base AND the first click does nothing.

Plus one rule, from 3.3: **the client must advance one frame before it accepts input**, and a
gate should assert it.

---

## 7. Phasing

**Phase 0, the brain patch.** `act=` and the three NULL-format guards. The CI guard list, the
patch regenerated, both platforms rebuilt. One commit. Everything else waits on it or is
cheaper after it. The seed export is **dropped, not deferred**: `Seed` is written and never
read on our path (3.1), so there would be nothing to pin.

**Phase 1, the slice.** 6.1, 6.2, 6.3, 6.8, 6.11's camera, and **6.13**. One hand-authored
skirmish map borrowing a campaign pack. The renderer half (6.2, 6.3) can start **before** the
multiplayer call sequence using the `Player=Multi1` trick, which decouples pixel work from
brain work.

6.13 is not optional here. Phase 1's exit criterion is "the player and an AI both have an MCV
and the AI builds a base", and as measured the player cannot deploy theirs on 7 of 8 start
positions, on a black screen, with the client acknowledging the dead click. Without 6.13 this
phase ships a match the player cannot start.

**Phase 2, it ends.** 6.4 and the first gates. Only reachable after Phase 0.

**Phase 3, the maps.** Stage the nine INIs, run the three tools, install, write the register
entries. Small: the pipeline is proved and the packs already bake.

**Phase 4, the front end.** 6.5. Now the long pole, because every option widget is new work.
Ship v1 with a list-only screen reusing `dosops.c` if it needs to be shorter.

---

## 8. Decisions still open

1. **How many players?** 1v1 is the honest target: two house colour tables, and multiple AIs
   do not gang up (2.7). Recommend 1v1, with the structures sized for more.
2. **Which maps?** The nine retail skirmish maps, flat, is faithful and now proven to bake.
   Flat is not a skirmish penalty (5.4).
3. **AI difficulty.** None exists. If you want Easy/Hard it is a design question about which
   levers we turn.
4. **What does losing look like?** Verdict banner and back to the menu needs no new art.
5. **Can both sides be the same faction?** Nothing forces opposition. GDI vs GDI would be two
   visually identical armies and `act=` cannot help. Either the screen forces opposition or we
   need a third look that does not exist.
6. **Does the AI get a voice?** Silencing it is a one-line test in `ev_cb`. Leaving it in is
   also a choice, and it is audible from the first minute.
7. **Is Save offered in skirmish for v1?** Disabling it is far cheaper than recording the mode
   in the save index.
8. **Is Capture the Flag in or out?** All nine retail maps ship flag data and
   `Special.IsCaptureTheFlag` is one field of a struct we already fill. It is currently out by
   accident rather than by decision.
9. **Bases off?** It is a real 1995 mode, but it interlocks with DestroyStructures (2.6) and
   changes what the AI does entirely.

---

## 9. Register entries this work owes

Known gaps this work owes:

- **`MPlayer_Defeated` crashes on a NULL format string.** `SystemStrings` is NULL in the DLL
  build because `CNC_Init` never runs `Init_Game`'s language load, so `Text_String` returns
  NULL and `house.cpp:4086/4102/4107` pass it to `sprintf`. A defeated human house segfaults
  the process. Reproduced twice at tick 59584 from different working directories. Until the
  guard lands, no skirmish can end.
- **Skirmish and 24 campaign scenarios render dead flat.** No `<SCEN>.IMG` or `CM*.IMG` in the
  cartridge, so `bake5.py` takes the `FLAT.IMG`/`CMFLAT.IMG` fallback. Pre-existing, not
  skirmish-specific.
- **Six cells have no cartridge tile.** `TEMPLATE_ROAD43` (TEMPERATE 135) on SCM03EA and
  `TEMPLATE_ROAD33` (DESERT 125) on SCM06EA are absent from their theater's `TL4`/`TL8`.
  `tools/bin_to_n64map.py` lists them and refuses to write silently.
- **TD skirmish has no AI difficulty setting** in the Remastered source. Anything CNC3D offers
  is ours, not the engine's.
- **`Special` leaks across a mode switch.** A skirmish with tiberium off disables tiberium
  growth for every later campaign mission in the same process.
- **Loading a skirmish save silently converts it to single player**, because CNC3D hardcodes
  `"GAME_NORMAL"` and `CNC_Save_Load` uses that string to set the mode.
- **The AI never designates an `Enemy`** in a GlyphX skirmish, so two urgency heuristics run
  on their fallbacks.
- **`packinspect.py` is stale for PKD/PKE** and reports the wrong heightmap and tint for any
  current pack. Found while validating the skirmish packs; not a skirmish bug.
- **The MCV cannot deploy where the starting escort stands**, and the engine's only feedback
  is `Speak(VOX_DEPLOY)` (`foot.cpp:1290`), which CNC3D does not play. 7 of 8 start positions
  on SCM01EA at `MPlayerUnitCount = 10`; retrying stays dead for about 140 ticks because the
  escort drives back across the pad. Faithful to 1995 in the engine, but 1995 had the voice
  line and never painted a positive order mark.
- **An AI house's sidebar is never populated**, because its buildings are revealed to
  `IsDiscoveredByComputer` rather than to the per-player mask. Harmless (nothing reads an AI
  sidebar) but it makes the obvious control probe useless.
- **Client input is ignored before the first `CNC_Advance_Instance`.** `CurrentObject`'s
  Active context is the constructor default until the first `Logic.AI()` switches it. Vanilla
  Conquer's fix is `#ifndef REMASTER_BUILD` and compiled out (`scenarioini.cpp:409-411`).
- **`GAME_STATE_LAYERS` returns false headless**, because it counts draw-intercept callbacks
  and no shape art is loaded. Pre-existing and campaign-wide, not skirmish; the CNC3D client
  never calls it. No gate may depend on it.
- **Two cell-grid conventions in one API.** `PLAYER_INFO`'s `ActionWithSelected` is indexed
  58x49 on SCM01EA while `SHROUD`, `PLACEMENT` and `STATIC_MAP` are 60x51. Anything reading
  both must convert.
- **The callback stream is not reproducible even though the sim is.** `IRandom()` is libc
  `rand()` and `GlyphX_Assign_Houses` calls `srand(timeGetTime())` (`dllinterface.cpp:918`),
  so AI taunt messages fire at different ticks across otherwise identical runs.

For `docs/tier1-gap.md`: nothing new is expected. A skirmish adds no rendering feature, and it
puts **fewer** objects on the field than the worst campaign mission, so the first draft's
performance worry was unfounded. The Win98 side is genuinely untested: not one measurement in
this investigation came from Windows, and the AI has never been started on the Win98 brain.
That is the largest remaining unknown after the crash.
