# Quantitative survey: C&C Tiberian Dawn mission scripting

Source: `game/missions/*.INI` (95 files). Engine semantics cross-checked against the GPL source in `brain/vanilla/tiberiandawn/trigger.h`, `trigger.cpp`, `teamtype.h`, `teamtype.cpp`, `house.cpp`.

## 0. Corpus split (this matters, do not average over all 95)

| group | files | scripted? |
|---|---|---|
| `SCB*` Nod campaign | 37 | yes |
| `SCG*` GDI campaign | 45 | yes |
| `SCM*` multiplayer | 11 | **zero** triggers, zero teamtypes, zero celltriggers |
| `TSTBOAT`/`TSTFACE` dev test maps | 2 | 1 trigger each |

**All statistics below are over the 82 campaign missions**, which carry 1,107 trigger lines and 967 teamtypes.

---

## 1. Section usage

| section | header present | non-empty | total entries | mean/mission | max |
|---|---|---|---|---|---|
| `[Triggers]` | 82/82 | **82/82** | 1,107 | 13.5 | 26 |
| `[TeamTypes]` | 82/82 | **80/82** | 967 | 11.8 | 22 |
| `[Waypoints]` | 82/82 | **82/82** | 2,408 | 29.4 | 28 slots |
| `[CellTriggers]` | 75/82 | **64/82** | 4,462 | 69.7 | 221 |
| `[Base]` | 82/82 | **50/82** | 807 | 16.1 | 43 |
| `[Briefing]` | 16/82 | 16/82 | 58 | 3.6 | 4 |

Triggers per mission: mean 13.5, median 14, range 2 to 26.
TeamTypes per mission: mean 11.8, median 13, range 0 to 22.

The two missions with no teamtypes at all (`SCG71EB`, `SCG74EA`) are 3-trigger scripted-cutscene maps.

### How triggers are attached (5,575 attachment records)

| attachment point | count | share |
|---|---|---|
| CellTrigger (a map cell) | 4,462 | 80.0% |
| Structure | 705 | 12.6% |
| Infantry | 296 | 5.3% |
| Unit (vehicle) | 111 | 2.0% |
| Terrain object | 1 | 0.02% |

Critical structural fact: **604 of 1,107 triggers (54.6%) are attached to nothing at all.** They are global house-level triggers, overwhelmingly `Time` (433 of the 604) and `All Destr.` (125). Only 503 triggers are hooked to an object or a cell.

CellTriggers are painted regions, not single cells: 4,462 cells collapse into only **~305 distinct named regions**, mean 3.7 regions per mission (median 3, max 10), averaging **18.9 cells per region**. A visual language needs a *paint-a-zone* tool, not a per-cell node.

Object-attached triggers are also groups: 274 distinct object-attached triggers, mean **4.1 carrier objects each** (median 3, max 38). Only 34% are attached to exactly one object.

---

## 2. Trigger EVENTS, ranked

The engine defines 16 events. **13 are used.** Never used anywhere: `House Discov.`, `Credits`, `No Factories`.

| rank | event | uses | share | cumulative | missions (of 82) |
|---|---|---|---|---|---|
| 1 | `Time` | 435 | 39.3% | 39.3% | 77 |
| 2 | `Player Enters` | 243 | 22.0% | 61.2% | 64 |
| 3 | `Destroyed` | 205 | 18.5% | 79.8% | 68 |
| 4 | `All Destr.` | 125 | 11.3% | **91.1%** | 75 |
| 5 | `Attacked` | 44 | 4.0% | 95.0% | 27 |
| 6 | `Discovered` | 24 | 2.2% | 97.2% | 17 |
| 7 | `# Units Dstr.` | 8 | 0.7% | 97.9% | 8 |
| 8 | `Civ. Evac.` | 6 | 0.5% | 98.5% | 6 |
| 9 | `Bldgs Destr.` | 5 | 0.5% | 98.9% | 5 |
| 10 | `# Bldgs Dstr.` | 4 | 0.4% | 99.3% | 4 |
| 11 | `Any` | 3 | 0.3% | 99.5% | 3 |
| 12 | `Built It` | 3 | 0.3% | 99.8% | 3 |
| 13 | `Units Destr.` | 2 | 0.2% | 100% | 2 |

**Four events cover 91% of all scripting.**

`Time` units are tenths of a minute (`TriggerTime = TICKS_PER_MINUTE / 10` in `house.cpp:1232`), so 1 unit = 6 seconds. Observed delays: n=435, min 0, p25 = 5 (30 s), median 55 (5.5 min), p75 = 150 (15 min), max 1200 (2 hours). 81 distinct values, so designers hand-tuned rather than snapping to a grid.

### The persistence flag is a hidden third event type

Field 6 of a trigger line is `PersistantType`: `0 = VOLATILE` (fires once, then deletes itself), `1 = SEMIPERSISTANT`, `2 = PERSISTANT` (never deletes). Counts: 926 / 113 / 68.

`SEMIPERSISTANT` decrements `AttachCount` on each object hit and only fires when the count reaches zero (`trigger.cpp:378-395`). In plain language it means **"destroy EVERY object tagged with this, then fire"**. 112 of the 113 semipersistent triggers are `Destroyed`:

| | count | mean group size | top actions |
|---|---|---|---|
| `Destroyed` + SEMIPERSISTANT ("kill all of these") | 112 | 6.3 objects (median 5, max 31) | All to Hunt 32, Airstrike 22, Reinforce 18, Lose 17, Win 9 |
| `Destroyed` + VOLATILE ("kill any one of these") | 93 | | Create Team 22, Reinforce 20, Dstry Trig 15, Lose 11, Win 8 |

So `Destroyed` is really **two distinct nodes**: *destroy-any* and *destroy-all-tagged*. A visual language that ships only one of them cannot express half the mission objectives in the game.

---

## 3. Trigger ACTIONS, ranked

The engine defines 17 actions. **All 17 are used**, but the tail is very thin.

| rank | action | uses | share | cumulative | missions |
|---|---|---|---|---|---|
| 1 | `Create Team` | 351 | 31.7% | 31.7% | 73 |
| 2 | `Reinforce.` | 257 | 23.2% | 54.9% | 63 |
| 3 | `Lose` | 108 | 9.8% | 64.7% | 77 |
| 4 | `Win` | 79 | 7.1% | **71.8%** | 74 |
| 5 | `Production` | 57 | 5.1% | 77.0% | 57 |
| 6 | `Autocreate` | 51 | 4.6% | 81.6% | 49 |
| 7 | `All to Hunt` | 45 | 4.1% | 85.6% | 40 |
| 8 | `Dstry Trig 'XXXX'` | 33 | 3.0% | 88.6% | 32 |
| 9 | `DZ at 'Z'` | 31 | 2.8% | **91.4%** | 30 |
| 10 | `Airstrike` | 27 | 2.4% | 93.9% | 26 |
| 11 | `Dstry Trig 'YYYY'` | 26 | 2.3% | 96.2% | 25 |
| 12 | `Nuclear Missile` | 14 | 1.3% | 97.5% | 9 |
| 13 | `Ion Cannon` | 10 | 0.9% | 98.4% | 8 |
| 14 | `Allow Win` | 7 | 0.6% | 99.0% | 7 |
| 15 | `Dstry Trig 'ZZZZ'` | 7 | 0.6% | 99.6% | 7 |
| 16 | `Cap=Win/Des=Lose` | 3 | 0.3% | 99.9% | 3 |
| 17 | `Dstry Teams` | 1 | 0.1% | 100% | 1 |

House field on triggers: GoodGuy 424, BadGuy 394, None 275, Neutral 14. Every trigger is scoped to a house or explicitly to none.

### Top 20 event -> action pairs (the actual vocabulary)

```
203  Time            -> Create Team          38  Destroyed       -> Reinforce.
134  Time            -> Reinforce.           35  Destroyed       -> All to Hunt
 82  Player Enters   -> Create Team          33  Time            -> Autocreate
 76  All Destr.      -> Lose                 28  Destroyed       -> Create Team
 60  Player Enters   -> Reinforce.           28  Destroyed       -> Lose
 49  All Destr.      -> Win                  26  Attacked        -> Create Team
 43  Time            -> Production           23  Destroyed       -> Airstrike
 20  Player Enters   -> Dstry Trig 'YYYY'    20  Player Enters   -> DZ at 'Z'
 17  Destroyed       -> Win                  17  Player Enters   -> Dstry Trig 'XXXX'
 16  Destroyed       -> Dstry Trig 'XXXX'    13  Player Enters   -> Production
```

---

## 4. Team missions (the AI script verbs)

The engine defines 12 team missions. **10 are used.** Never used: `Retreat`, `Attack Tarcom`.

4,162 script steps across 967 teamtypes:

| rank | team mission | uses | share | cumulative | missions |
|---|---|---|---|---|---|
| 1 | `Move` (to waypoint) | 2,984 | 71.7% | 71.7% | 78 |
| 2 | `Attack Base` | 327 | 7.9% | 79.6% | 51 |
| 3 | `Attack Units` | 312 | 7.5% | 87.0% | 72 |
| 4 | `Guard` (timed hold) | 268 | 6.4% | **93.5%** | 52 |
| 5 | `Loop` (jump to step 0) | 159 | 3.8% | 97.3% | 53 |
| 6 | `Unload` | 88 | 2.1% | **99.4%** | 42 |
| 7 | `Attack Civil.` | 9 | 0.2% | 99.6% | 3 |
| 8 | `Move to Cell` | 7 | 0.2% | 99.8% | 1 |
| 9 | `Rampage` | 5 | 0.1% | 99.9% | 4 |
| 10 | `Defend Base` | 3 | 0.1% | 100% | 1 |

**Six verbs cover 99.4%.** `Move` alone is 72%: an AI team script is essentially a waypoint path with a verb bolted on the end.

Script shape is extremely stereotyped:

- **First step**: `Move` 779, `Attack Units` 67, `Attack Base` 51, everything else 11.
- **Last step**: `Attack Base` 303, `Attack Units` 287, `Loop` 159, `Move` 97, `Unload` 25, `Guard` 21.
- Script length: median 4 steps; 59 teams (6%) have an empty script (pure reinforcement drops); the longest is 20 steps.

So the canonical team is: *Move, Move, Move, ..., Attack X*. Patrols are: *Move, Guard, Move, Guard, ..., Loop*.

Waypoints are consumed almost entirely by `Move` (2,984) and `Unload` (88). Waypoint indices 0-10 carry 74% of all references, and usage decays monotonically to index 24. Indices 25/26/27 are reserved by the engine: **wp25 = the drop-zone smoke target** (`ACTION_DZ` hardcodes `Scen.Waypoint[25]`, `trigger.cpp:468`), **wp26 = home/tactical start**, **wp27 = reinforcement arrival cell** (`defines.h:2557`). All 82 missions define wp26; 79 define wp27; 40 define wp25.

### Team flags (967 teams)

| flag | set | meaning |
|---|---|---|
| `IsRoundAbout` | 702 (73%) | prefer safe pathing |
| `IsAutocreate` | 368 (38%) | eligible for AI autocreate pool |
| `IsPrebuilt` | 119 (12%) | exists at scenario start |
| `IsSuicide` | 65 (7%) | ignore losses |
| `IsReinforcable` | 53 (5%) | can be topped up |
| `IsLearning` | 20 (2%) | |

Team composition is 1 to 3 unit classes (`E1` 247, `E3` 219, `E2` 142, `LTNK` 106, `TRAN` 100 are the top five).

**543 of 967 teamtypes are named directly by a trigger. The other 424 (44%) are only reachable via `Autocreate` / `Production`,** i.e. they are the AI's ambient attack-wave pool, not scripted beats.

---

## 5. Three walkthroughs, increasing complexity

### 5a. `SCG01EA.INI` (GDI 1) - 8 triggers, 4 teams, 0 cell triggers, 0 base

Mission-level properties: `Player=GoodGuy`, `BuildLevel=1`, `Percent=0`, GDI credits 20 (x100 = 2,000), GDI `Edge=South`, Nod `Edge=North`. Map 26x23. On-map objects: 1 GDI MCV, 1 GDI gunboat set to `Hunt`, 4 GDI riflemen, 10 Nod riflemen, 3 Nod gun turrets. No `[Base]` list, so Nod rebuilds nothing.

```
RNF1=Time,Reinforce.,3,GoodGuy,GDIR1,0     ; t=18s   drop 3x E1 by LST
RFN2=Time,Reinforce.,6,GoodGuy,GDIR1,0     ; t=36s   drop 3x E1 by LST
RNF4=Time,Reinforce.,10,GoodGuy,GDIR2,0    ; t=60s   drop 1x Jeep by LST
RNF6=Time,Reinforce.,16,GoodGuy,GDIR2,0    ; t=96s   drop 1x Jeep by LST
ATK2=Time,Create Team,0,BadGuy,NOD1,0      ; t=0     spawn the single Nod patrol
ATK4=Bldgs Destr.,Reinforce.,0,BadGuy,NOD3,0  ; all 3 Nod turrets dead -> last-ditch buggy
WIN =All Destr.,Win,0,BadGuy,None,0        ; Nod has nothing left -> win
LOSE=All Destr.,Lose,0,GoodGuy,None,0      ; GDI has nothing left -> lose
```

Teams:
```
GDIR1=GoodGuy,...,E1:3 + LST:1, 0 mission steps, Reinforcable=1, Prebuilt=1
GDIR2=GoodGuy,...,JEEP:1 + LST:1, 0 mission steps, Reinforcable=1, Prebuilt=1
NOD1 =BadGuy,...,E1:2, 4 steps: Move wp0 (37,46) -> Move wp1 (38,55) -> Move wp2 (52,55) -> Attack Units:6
NOD3 =BadGuy,IsSuicide=1,...,BGGY:1, 1 step: Attack Units:5
```

In plain language: **it is a metronome.** Four timers hand you an escalating trickle of GDI infantry and jeeps arriving by hovercraft at wp27. One Nod two-man patrol walks a fixed three-point path and then engages. When you have flattened all three Nod turrets, Nod throws one suicide buggy at you. Kill everything Nod owns and you win; lose everything and you lose. There is no zone, no base rebuilding, no AI economy at all. Total scripting surface: **timer, spawn-team, reinforce, destroy-all-of-a-house -> win/lose.**

### 5b. `SCB06EA.INI` (Nod 6) - 14 triggers, 8 teams, 45 cells in 4 zones, 0 base

`Player=BadGuy`, `BuildLevel=6`, map 46x45 desert. Nod starts as a raiding party in the north-west (x19-27, y18-19): 3 light tanks, 1 bike, 1 buggy, 12 infantry, **no base and no production**. A neutral civilian village sits immediately east (x34-45, y17-24): 17 civilian structures, 3 civilians. GDI holds a small base in the south-east (x48-57, y32-46) with 6 medium tanks, 4 jeeps, 22 infantry, 2 guard towers.

Zones painted in `[CellTriggers]`:
```
dzne  24 cells  x57-61 y41-45   the objective area, east of the GDI base
win    1 cell   x59    y43      one cell inside dzne
chn3   8 cells  x48-49 y55-58   a strip in the far south
win2  12 cells  x52-54 y55-58   the extraction pad, next to wp27 (54,58)
```

Triggers:
```
grd1=Time,Create Team,3,GoodGuy,gdi3,0    ; t=18s, GDI 2x MTNK begins a 12-step patrol loop
dzne=Player Enters,DZ at 'Z',0,BadGuy,None,0   ; entering the objective area smokes wp25 (56,56)
win =Player Enters,Allow Win,0,BadGuy,None,0   ; one cell; clears the victory Blockage counter
chn3=Player Enters,Reinforce.,0,BadGuy,nod1,0  ; a Nod TRAN flies in to wp17 (55,57)
win2=Player Enters,Win,0,BadGuy,None,0         ; reach the extraction pad -> win
gdi =All Destr.,Win,0,GoodGuy,None,0           ; alternate win: wipe GDI out
nod =All Destr.,Lose,0,BadGuy,None,0
lose=All Destr.,Lose,0,BadGuy,None,0
grd2=Discovered,Create Team,0,None,gdi4,0      ; attached to 3 GDI E2 at (17,31)/(1,31)/(2,32)
atk1=Attacked,Create Team,0,None,gdi5,0        ; attached to 1 E1 + 1 GTWR
atk2=Attacked,Create Team,0,None,gdi6,0        ; attached to 1 E1 + 1 GTWR
chn1=Destroyed,Reinforce.,0,None,gdi1,0        ; attached to 5 CIVILIAN buildings
chn2=Destroyed,Reinforce.,0,None,gdi2,0        ; attached to 3 CIVILIAN buildings
atk3=Attacked,Reinforce.,0,None,a10s,0         ; attached to 1 CIVILIAN building -> A10 airstrike
```

Teams:
```
gdi3=MTNK:2, 12 steps: Move wp1, Guard 1, Move wp3, Guard 1, Move wp7, Guard 1, Move wp8,
                       Guard 1, Move wp9, Guard 1, Move wp10, Loop 1     ; the roaming patrol
gdi4=E1:2+E2:1+JEEP:1, 7 steps: Move 4,10,9,11,9,10, Loop 0              ; second patrol
gdi1=E1:5 + TRAN:1, 3 steps: Move wp5, Unload wp5, Attack Units:20       ; chopper-borne response
gdi2=E2:5 + TRAN:1, 3 steps: Move wp6, Unload wp6, Attack Units:20
gdi5=E1:3+E2:3, IsSuicide=1, 4 steps: Move 0, Move 1, Move 4, Attack Units:30
gdi6=MTNK:1+JEEP:1, IsSuicide=1, 3 steps: Move 2, Move 3, Attack Units:30
a10s=A10:1, 0 steps                                                       ; a single strike plane
nod1=TRAN:1, 1 step: Move wp17                                            ; the player's evac bird
```

In plain language: **a stealth-and-consequence infiltration.** You start north-west with a fixed force you cannot replace. Two GDI patrols circle the map on looping waypoint routes. Anything you shoot has a bill attached: attacking a tagged GDI rifleman or guard tower spawns a suicide counterattack team, and **attacking or destroying the neutral village calls in GDI chopper-borne infantry (5 riflemen or 5 rocket troopers) or an A10 strike**. You must reach the objective zone in the east, which both unblocks victory (`Allow Win` decrements a per-house `Blockage` counter, `trigger.cpp:1050-1051`, so `Win` is inert until it fires) and pops LZ smoke at wp25 to show you where extraction is. Crossing the southern strip calls your own transport in, and stepping on the extraction pad ends the mission.

New vocabulary over 5a: **painted zones, allow-win gating, per-object attacked/discovered hooks, collateral-damage punishment via civilian buildings, patrol scripts (Move/Guard/Loop), transport Move+Unload, LZ smoke.**

### 5c. `SCG15EB.INI` (GDI 15) - 20 triggers, 20 teams, 110 cells in 4 zones, 32-node base

`Player=GoodGuy`, `BuildLevel=15`, `Percent=98`, map 61x60. GDI starts with **nothing on the map**. Nod owns 54 structures, 18 vehicles, 22 infantry, plus a 32-entry `[Base]` rebuild list (2x OBLI, 6x NUK2, 3x HAND, 2x AFLD, 6x SAM, 6x SILO, 2x PROC, 2x FACT, 3x GUN).

Zones:
```
prod  45 cells  x1-24  y33-54   the whole south-west landing quadrant
dely  29 cells  x32-40 y36-45
delx  14 cells  x39-40 y38-44
auto  22 cells  x42-43 y36-46   a north-south strip on the east flank
```

Triggers, grouped by what they do:

*Bootstrap*
```
rnf1=Time,Reinforce.,0,GoodGuy,mcv,0       ; t=0: LST + MCV. this IS your starting force
rnf2=Discovered,Reinforce.,0,None,gdi1,0   ; attached to one Nod FTNK; discovering it sends MTNK + LST
prod=Player Enters,Production,0,GoodGuy,None,0   ; 45-cell zone: once you land, Nod starts building
auto=Player Enters,Autocreate,0,GoodGuy,None,0   ; east strip: Nod's autocreate pool switches on
```

*The scripted attack ladder (four one-shot timers)*
```
xxxx=Time,Create Team,70,BadGuy,xxxx,2     ; t=7min,  PERSISTANT -> repeats forever
yyyy=Time,Create Team,100,BadGuy,yyyy,2    ; t=10min, PERSISTANT -> repeats forever
atk2=Time,Create Team,90,BadGuy,nod4,0     ; t=9min,  2x LTNK, Attack Base
atk3=Time,Create Team,140,BadGuy,nod5,0    ; t=14min, 1x FTNK, Rampage
atk4=Time,Create Team,180,BadGuy,nod6,0    ; t=18min, 1x STNK, Attack Base
chn1=Time,Reinforce.,310,BadGuy,chin1,0    ; t=31min, chopper drops 5x E6 engineers, Attack Base
chn2=# Bldgs Dstr.,Reinforce.,450,BadGuy,chin2,0
```

*Pressure valves (the latch idiom)*
```
delx=Player Enters,Dstry Trig 'XXXX',0,GoodGuy,None,0   ; 14 cells at x39-40 y38-44
dely=Player Enters,Dstry Trig 'YYYY',0,GoodGuy,None,0   ; 29 cells at x32-40 y36-45
```
`xxxx` and `yyyy` are persistence 2, so they respawn a 8-man infantry team forever. Pushing your front line east across the `delx`/`dely` bands deletes those triggers and stops the bleed. **This is the game's only mechanism for "the harassment stops once you take ground."**

*Reactive defence and superweapons*
```
atk1=Attacked,Create Team,0,None,nod3,0            ; attached to 2 BGGY; shooting them spawns a suicide pair
nuk2=Attacked,Nuclear Missile,0,None,None,0        ; attached to the Temple of Nod
nuk1=Destroyed,Nuclear Missile,0,None,None,1       ; SEMIPERSISTANT over 2 NUK2 power plants
air1=Destroyed,Airstrike,0,None,None,1             ; SEMIPERSISTANT over 4 SAM sites
hunt=Destroyed,All to Hunt,0,None,None,1           ; SEMIPERSISTANT over 11 core Nod buildings
```
Reading these correctly: **kill all 4 SAM sites and you get the airstrike**; touch the Temple or kill both advanced power plants and Nod gets the nuke; **when the last of 11 tagged core buildings dies, every remaining Nod unit drops its script and goes to Hunt** so the mission cannot stalemate.

*Win/lose*
```
win =All Destr.,Win,0,BadGuy,None,0
lose=All Destr.,Lose,0,GoodGuy,None,0
```

Teams: 20 of them. `nod7`-`nod14` all have `IsAutocreate=1` and share the same opening path `Move 0 (36,16) -> Move 1 (48,21) -> Move 2 (57,32) -> Move 3 (58,42)` before diverging into `Attack Base:50` or `Attack Units:60`. That is the AI's standing attack corridor down the east side of the map. `nod1` and `nod2` are `Move/Guard/Loop` patrols. `chin1`/`chin2` are `Move -> Unload -> Attack Base` chopper insertions.

In plain language: **a full base-building slugfest.** You arrive by hovercraft with one MCV. The moment you set foot in the south-west, Nod's economy switches on (`Production` plus a 32-node rebuild list at 98% rebuild rate). A fixed ladder of scripted attacks arrives at 7, 9, 10, 14, 18 and 31 minutes, two of which repeat forever until you physically advance past two painted bands. Nod's ambient wave pool (`Autocreate`) turns on when you probe the east flank. Superweapons on both sides are gated on destroying specific tagged building groups, and killing the last of the eleven tagged core buildings collapses Nod into an all-out final rush.

---

## 6. Recurring patterns, with counts

Classified across all 1,107 campaign trigger lines:

| idiom | uses | share | missions |
|---|---|---|---|
| **timer -> spawn AI attack team** | 203 | 18.3% | 56 |
| **timer -> reinforce (convoy / air drop)** | 134 | 12.1% | 51 |
| **enter zone -> spawn ambush team** | 82 | 7.4% | 37 |
| **lose everything -> Lose** | 76 | 6.9% | 75 |
| **enter zone -> reinforce** | 60 | 5.4% | 30 |
| **destroy all enemy -> Win** | 49 | 4.4% | 49 |
| **timer -> switch AI production on** | 43 | 3.9% | 43 |
| **enter zone -> cancel a repeating spawner** | 41 | 3.7% | 25 |
| **object dies -> reinforce** | 38 | 3.4% | 21 |
| **kill tagged building group -> all enemies go Hunt** | 35 | 3.2% | 33 |
| **timer -> switch AI autocreate on** | 33 | 3.0% | 33 |
| **object dies -> spawn team** | 28 | 2.5% | 19 |
| **object dies -> Lose (protect this)** | 28 | 2.5% | 22 |
| **kill tagged group -> grant superweapon** | 27 | 2.4% | 23 |
| **unit attacked -> counterattack team** | 26 | 2.3% | 19 |
| **object dies -> cancel a repeating spawner** | 24 | 2.2% | 21 |
| **enter zone -> switch AI economy on** | 21 | 1.9% | 16 |
| **enter zone -> pop LZ smoke** | 20 | 1.8% | 19 |
| **object dies -> Win (kill this one thing)** | 17 | 1.5% | 17 |
| everything else (39 more idioms) | 122 | 11.0% | |

Named sub-patterns worth calling out:

- **The XXXX/YYYY/ZZZZ latch.** 65 triggers are literally named `XXXX`, `YYYY` or `ZZZZ` across **35 of 82 missions (43%)**, and the same 35 missions contain `Dstry Trig` actions. 34 of the 65 are persistence 2 (infinite repeat). This is the engine's only "cancel a running behaviour" primitive, and level designers built an entire escalate-then-relieve system on top of three hardcoded magic names.
- **Base bootstrap.** 50 of 82 missions ship a `[Base]` rebuild list (mean 16 nodes, max 43), 57 have a `Production` trigger and 49 have an `Autocreate` trigger. The economy is almost never on at t=0: it is gated behind a timer (43x) or a zone (21x). AI rebuild rate `Percent` is 100 in 50 missions and 0 in 18.
- **Anti-stalemate.** 45 `All to Hunt` actions across 40 missions, 35 of them fired by "the last of a tagged building group died".
- **Escort/protect** is expressed only as `Destroyed -> Lose` attached to a specific object (28 uses, 22 missions). There is no dedicated escort node.
- **Timed superweapon grants** exist but are rare (`Ion Cannon` 10, `Nuclear Missile` 14, `Airstrike` 27) and are usually earned by destroying a tagged group rather than by a clock.

---

## 7. What this implies for a visual language

**Day one (covers ~91% of events, ~91% of actions, ~99% of team behaviour):**

*Event nodes (5):* `Time elapsed` (per house, with a repeat flag), `Player enters zone`, `Object destroyed` in two flavours (**any-of-group** and **all-of-group**), `All of house destroyed`.

*Action nodes (9):* `Create team`, `Reinforce (deliver team)`, `Win`, `Lose`, `Start AI production`, `Enable AI autocreate`, `All enemies to Hunt`, `Cancel trigger`, `Pop LZ smoke`.

*Team-script verbs (6):* `Move to waypoint`, `Guard for N`, `Loop`, `Unload`, `Attack Base`, `Attack Units`.

*Authoring primitives (non-negotiable):* a **zone paint tool** (median 3 zones/mission, ~19 cells each), a **tag tool** for attaching one trigger to N objects (mean 4.1 carriers, max 38), a **waypoint placer** with reserved slots 25/26/27, a **base rebuild list** editor, and mission properties (`BuildLevel`, `Percent`, starting credits, house edges).

**Second wave (the next ~7%):** `Attacked` event, `Discovered` event, `Airstrike` / `Ion Cannon` / `Nuclear Missile` grants, `Allow Win` gating (only 7 uses but it is the only way to express "do the objective before you may leave").

**Long tail, safe to defer:** `# Units Dstr.`, `# Bldgs Dstr.`, `Civ. Evac.`, `Built It`, `Any`, `Units Destr.`, `Bldgs Destr.` (28 uses total, 2.5%), plus actions `Cap=Win/Des=Lose` (3) and `Dstry Teams` (1), plus team verbs `Attack Civil.`, `Move to Cell`, `Rampage`, `Defend Base` (24 steps total, 0.6%).

**Never implement:** events `House Discov.`, `Credits`, `No Factories`, and team missions `Retreat`, `Attack Tarcom`. The engine supports them; 82 shipped missions use them zero times.

Scratch analysis scripts are at `<scratch>/` (`parse.py` builds `db.json`; `q1.py` through `q12.py` produce each table above).