# A visual node language for Tiberian Dawn mission scripting

## 0. Verdict first

**The graph must not be the artifact. The INI is the artifact.** The graph is a sidecar view with named bindings back into the INI, and "compile" is a *patch* of the original file, not a rewrite of it. I verified why below: across the 95 shipped mission files there are 66 distinct section orderings, cell-trigger keys are ascending in 1 of 64 files, and trigger keys are alphabetical in 4 of 84. No canonical writer can reproduce those layouts, so byte-equivalence is only reachable by preserving the carrier document.

The good news is that the *semantics* import perfectly clean. I checked all 95 files: **0 trigger names over 4 chars, 0 trigger lines with anything but exactly 6 fields, 0 duplicate keys in any section, 0 dangling trigger references, 0 dangling team references, all 968 teamtype lines carry both trailing flags, and all 807 `[Base]` coordinates are exactly cell-aligned** (low bytes zero, so `Coord = celly<<24 | cellx<<8` is a total bijection with cells). There is nothing pathological in the data model. Only the file layout is unreproducible.

---

## 1. Five facts from the recon that dictate the shape of the language

1. **A TD trigger is one event paired with one action and a 4-character name. There is no flow, no boolean composition, no variables, no counters** beyond the five `Event_Need_Data` cases. Therefore this is **not a Blueprint execution graph and must not have white exec pins.** It is a wiring diagram over resources. Anyone who draws exec wires here is lying about the engine, and the lie will surface the first time a designer tries to AND two conditions.

2. **Attachment is optional, not the spine.** I re-derived the split: 433 `Time` and 125 `All Destr.` triggers attach to nothing at all. House-global rules are the majority case.

3. **80% of attachments are painted cells** (4,462 of 5,575), collapsing into ~305 regions averaging 18.9 cells. The primary authoring gesture is *painting on the map*, not placing a node. The node canvas and the map canvas have to be one document, not two tabs.

4. **The route determines the semantics.** The same `ActionText` behaves differently through `Spring(obj)` / `Spring(cell)` / `Spring(house, data)`. `Production`, `Airstrike`, `Autocreate`, `Cap=Win/Des=Lose` and `Allow Win` all differ or vanish by route. So the event choice is a *type* that constrains which actions are legal. This is a real type system, and it is the single largest thing the editor can give a designer that the raw INI cannot.

5. **The only "cancel" primitive is a name hack.** Three hardcoded names, used in 35 of 82 campaign missions. I confirmed the pairing: 33 `Dstry Trig 'XXXX'`, 25 `'YYYY'`, 7 `'ZZZZ'`, of which 65 resolve to a real trigger and exactly 1 dangles (a harmless no-op, since `delete NULL` is safe).

---

## 2. Data model: three layers

```
Layer 0  CARRIER      the .INI as an ordered token stream (lines, comments, blank
                      lines, CRLF, section-name casing, trailing spaces)
Layer 1  SEMANTIC     parsed entities: Rule[], TeamType[], Zone[], Tag[], Waypoint[28],
                      BaseNode[], House[], MissionSettings
Layer 2  GRAPH        .mgraph.json: node positions, groups, comments, macro bindings,
                      colours, collapsed state. Purely presentational plus macro identity.
```

- Layer 1 is derived from Layer 0 on import and is the authority for *meaning*.
- Layer 0 is the authority for *bytes*.
- Layer 2 never affects output. Delete it and the mission still compiles identically.

**Compile = diff Layer 1 against the parse of Layer 0, then apply a minimal line patch to Layer 0.** Unchanged lines are never re-emitted. This is the only design that satisfies the losslessness requirement, and it is a strictly stronger discipline than the current `edit_write_ini` (`edit_mod.h:4561`), which rewrites its six owned sections and copies the rest through.

### Patch rules

| Operation | Rule |
|---|---|
| Field changed on an existing rule | rewrite that one line in place, preserve the key's original spelling and the line's original position |
| New rule | append at the end of the `[Triggers]` section, before the section's trailing blank line |
| Deleted rule | delete the line; also delete every `[CellTriggers]` line whose value is that name, and blank the trailing field on every object line that referenced it |
| Section missing entirely | create it immediately after `[Triggers]` (or at EOF for `[CellTriggers]`), using the file's dominant line ending |
| Everything else | never touched |

### Things the patcher must carry through untouched, measured

- 88 of 95 files are CRLF; the other 7 are LF. Never normalise.
- Every file begins with a blank line or a comment before `[BASIC]`.
- Section-name casing varies (`[BASIC]` in 14 files, `[Basic]` in 80). `[Triggers] ` has a trailing space in 2 files.
- Empty legacy sections that must survive: `[Teams]` (12 files), `[REINFORCEMENTS]` (14), and a literally unnamed `[]` section carrying `Edge=None` / `Credits=26507` in 5 files.
- Keys the engine never reads but which are present: `[MAP] TacticalPos` (14 files), `[Basic] Name` (26), house `Quota` (6), `FlagHome`/`FlagLocation` (314).
- Comments such as `; Scenario 1 control for house BadGuy.`

### The gate

Add a build gate alongside G54: for each of the 95 `.INI` files, `import → compile with zero edits → byte-compare`. Must be 0 differing bytes on all 95, or the language does not ship. This is cheap to run and it is the only honest proof of the losslessness claim.

---

## 3. Node catalogue

### 3.1 Pin types

There are seven, and none of them is an exec pin.

`HouseRef` · `TeamRef` · `WaypointRef` · `CellSet` · `ObjectSet` · `RuleRef` · `int`

### 3.2 The Rule node (one node ⇔ one `[Triggers]` line, always)

This 1:1 invariant is what makes round-tripping tractable. Compiles to:

```
<name> = <EventText>,<ActionText>,<Data>,<House>,<Team>,<Persist>
```

```
┌── Rule  "atk3"                          persist: [Once ▾] ──┐
│  WHEN   [On Timer ▾]   after [14.0] min                     │
│  THEN   [Create Team ▾]                                     │
│  ◂ House                                          Team ▸    │
│  extent: (none)                            Cancels ▸        │
└─────────────────────────────────────────────────────────────┘
```

| Pin | Direction | Enabled when | Compiles to |
|---|---|---|---|
| `House` | in, `HouseRef` | `Event_Need_House` | field 4 |
| `Team` | in, `TeamRef` | `Action_Need_Team` (Create Team / Dstry Teams / Reinforce.) | field 5 |
| `Cancels` | out, `RuleRef` | action is one of the three `Dstry Trig` | the compiler assigns the XXXX/YYYY/ZZZZ slot and **renames the target rule** |
| `extent` | facet, not a pin | `Event_Need_Object` or cell route | `[CellTriggers]` lines, or object trailing fields |

**Extent is a facet of the Rule, not a reusable node.** In TD the attachment *is* the trigger name: a cell holds one trigger (`display.cpp:1364`, first entry wins), and an object holds one `Trigger` pointer. So a zone or a tag cannot be shared between two rules, and modelling them as free-floating reusable nodes would let designers draw graphs that cannot exist. Render them as chips hanging off the rule ("45 cells", "4 objects") that select-and-highlight on the map when clicked.

**Names are sticky.** On import, every rule keeps its original 4-char name, so recompilation is byte-stable. New rules get generated names checked case-insensitively against the whole set (`stricmp`, `trigger.cpp:1234`) and against the three magic names.

### 3.3 Event nodes

Counts are over the 82 campaign missions. "Route" is which `Spring` overload the trigger will actually take, which is the thing the designer cannot see in the INI.

| Node | `Eventname` | Route | Data means | Extent | Uses | Tier |
|---|---|---|---|---|---|---|
| **On Timer** | `Time` | house | delay, **units of 6 s** (`TriggerTime = TICKS_PER_MINUTE/10`) | none | 435 (39.3%) | **v1** |
| **On Enter Zone** | `Player Enters` | cell | unused, emit 0 | `CellSet`, required | 232 | **v1** |
| **On House Wiped Out** | `All Destr.` | house | unused | none | 125 (11.3%) | **v1** |
| **On All Of These Destroyed** | `Destroyed` + persist `1` | object | unused | `ObjectSet`, required | 112 | **v1** |
| **On Any Of These Destroyed** | `Destroyed` + persist `0` | object | unused | `ObjectSet`, required | 93 | **v1** |
| **On Attacked** | `Attacked` | object, plus a house variant with no extent | unused | `ObjectSet` optional | 44 | v2 |
| **On Discovered** | `Discovered` | object | unused | `ObjectSet` | 24 | v2 |
| **On N Units Lost** | `# Units Dstr.` | house | N, `>=` | none | 8 | v3 |
| **On Civilian Evacuated** | `Civ. Evac.` | house | unused | none | 6 | v3 |
| **On Buildings Wiped Out** | `Bldgs Destr.` | house | unused | none | 5 | v3 |
| **On N Buildings Lost** | `# Bldgs Dstr.` | house | N, `>=` | none | 4 | v3 |
| **On Captured** | `Player Enters` | **object** | unused | `ObjectSet` | 4 | v3 |
| **On Any Object Event** | `Any` | object | unused | `ObjectSet` | 3 | v3 |
| **On Structure Built** | `Built It` | house | **raw `StructType` ordinal**, exact match | none | 3 | v3 |
| **On Units Wiped Out** | `Units Destr.` | house | unused | none | 2 | v3 |
| (not offered) | `House Discov.`, `Credits`, `No Factories` | | | | **0** | never |

Two splits worth defending:

- **`Destroyed` is two nodes, not one with a checkbox.** The persistence flag changes it from "kill any one of these" to "kill every one of these", and the shipped corpus splits 93/112. A checkbox buried in a properties panel would hide the game's second-most-important objective primitive. Give it two distinct nodes with distinct icons.
- **`Player Enters` is two nodes, not one.** Attached to cells (232 uses) it is `FootClass::Per_Cell_Process`; attached to objects (4 uses) it is `TechnoClass::Captured`, an entirely different mechanic. Same `EventText`, different route, different meaning. The importer disambiguates by looking at where the name appears.

**v1 event coverage: 5 nodes = 997 of 1,107 trigger lines = 90.1%.**

### 3.4 Action nodes

| Node | `Actionname` | Extra pin | Uses | Tier |
|---|---|---|---|---|
| **Spawn AI Team** | `Create Team` | `Team` | 351 (31.7%) | **v1** |
| **Deliver Reinforcement** | `Reinforce.` | `Team` | 257 (23.2%) | **v1** |
| **Player Loses** | `Lose` | | 108 | **v1** |
| **Player Wins** | `Win` | | 79 | **v1** |
| **Start AI Production** | `Production` | `House` | 57 | **v1** |
| **Enable AI Autocreate** | `Autocreate` | `House` | 51 | **v1** |
| **All Enemies To Hunt** | `All to Hunt` | | 45 | **v1** |
| **Cancel Rule** | `Dstry Trig 'XXXX'/'YYYY'/'ZZZZ'` | `Cancels` out | 66 | **v1** |
| **Pop LZ Smoke** | `DZ at 'Z'` | (implicitly waypoint 25) | 31 | **v1** |
| **Grant Airstrike** | `Airstrike` | | 27 | v2 |
| **Grant Nuke (Nod)** | `Nuclear Missile` | | 14 | v2 |
| **Grant Ion Cannon (GDI)** | `Ion Cannon` | | 10 | v2 |
| **Block Victory Until** | `Allow Win` | `House` | 7 | v2 |
| **Capture=Win / Destroy=Lose** | `Cap=Win/Des=Lose` | `ObjectSet` | 3 | v3 |
| **Destroy AI Team** | `Dstry Teams` | `Team` | 1 | v3 |

**v1 action coverage: 9 nodes = 1,045 of 1,107 = 94.4%.**

Three of these deserve special treatment in the UI because the raw INI actively misleads:

- **Cancel Rule** is rendered as a real edge, not a name. The compiler owns the XXXX/YYYY/ZZZZ assignment, errors at more than 3 distinct cancel targets in a mission, and warns when the target rule is `Allow Win` (see the lint table).
- **Block Victory Until** is a win *blocker*. Label it "Victory is blocked until this fires", never "Allow Win", because 7 uses in 7 missions means almost no designer will have internalised the `Blockage` counter.
- **Pop LZ Smoke** hard-codes waypoint 25 (`trigger.cpp:468`). The node shows waypoint Z's map position read-only and errors if Z is undefined (40 of 82 missions define it).

### 3.5 TeamType node (compound)

One node, one `[TeamTypes]` line. Header carries the flags; the body is an ordered strip of verb chips.

```
┌── TeamType  "nod4"                        [BadGuy ▾] ────────┐
│  members  LTNK x2                                            │
│  flags    ☑ Avoid threat   ☐ Suicide   ☑ Autocreate          │
│           ☐ Prebuilt       ☐ Reinforcable                    │
│  priority 15   max 1                                         │
│  script  [Move A]→[Move B]→[Move C]→[Attack Base 50]         │
└──────────────────────────────────────────────────────────────┘
```

Verb chips:

| Chip | `TMissions` name | Argument | Uses | Tier |
|---|---|---|---|---|
| **Move to** | `Move` | waypoint letter (idx < 28) | 2,984 (71.7%) | **v1** |
| **Attack Base** | `Attack Base` | duration in 6 s units | 327 | **v1** |
| **Attack Units** | `Attack Units` | duration | 312 | **v1** |
| **Hold** | `Guard` | duration | 268 | **v1** |
| **Loop to step** | `Loop` | 0-based mission index | 159 | **v1** |
| **Unload at** | `Unload` | waypoint **and** timeout, same number | 88 | **v1** |
| Attack Civilians | `Attack Civil.` | duration | 9 | v3 |
| Move to cell | `Move to Cell` | raw cell | 7 | v3 |
| Rampage | `Rampage` | duration | 5 | v3 |
| Defend Base | `Defend Base` | duration | 3 | v3 |
| (not offered) | `Retreat`, `Attack Tarcom` | | **0** | never |

**v1 verb coverage: 6 chips = 4,138 of 4,162 steps = 99.4%.**

The **Unload** chip must render its double meaning explicitly ("unload at waypoint E, give up after 24 s") because `team.cpp:464` sets `TimeOut` unconditionally and this trips everyone.

Flags: `Learning`, `Mercenary`, `InitNum` and `Fear` are parsed and never read in TD. Show them in an "inert, preserved" collapsed drawer so imports round-trip, and never surface them in new-team authoring.

### 3.6 Resource nodes

| Node | Compiles to | Notes |
|---|---|---|
| **Waypoint** | `[Waypoints] <idx>=<cell>` | 28 slots. 0-25 shown as A-Z, 25 badged "LZ smoke", 26 "Home", 27 "Reinforcement drop". Unassigned slots are omitted, never written as `-1`. |
| **Base Plan** | `[Base] Count=` + `%03d=TYPE,coord` | Ordered list, order is build order. Owner is derived from `[Basic] Player`, **not authorable**. Coord is `celly<<24 \| cellx<<8`, verified cell-aligned in all 807 shipped nodes. |
| **House** | `[GoodGuy]` etc. | `Credits` (shown x100), `Edge` (the reinforcement entry side, which is the field most authors miss), `Allies`, `MaxUnit`/`MaxBuilding` (note the engine floors both at 150). |
| **Mission Settings** | `[Basic]` + `[MAP]` | `BuildLevel`, `Percent` (AI rebuild rate), the eight movie slots, `CarryOverMoney`/`CarryOverCap`. |

---

## 4. The validator: what the editor knows that the INI does not

This is where the language earns its cost. Every rule below is grounded in a recon citation and, where possible, checked against the shipped corpus.

**Errors (block compile of newly authored content):**

| # | Rule | Source |
|---|---|---|
| E1 | Semi-persistent ("all of these") rule must have `House = None`. With a house, `AttachCount = N+1` and it can never reach 0, so it never fires. | `trigger.cpp:1049-1055`. **Confirmed empirically: 112 of 113 shipped semi-persistent triggers have `House=None`.** The one exception is, by this rule, a shipped trigger that can never fire. |
| E2 | At most 3 distinct Cancel targets per mission. | only XXXX/YYYY/ZZZZ exist |
| E3 | An object-route event cannot use `Allow Win` or `Cap=Win/Des=Lose` outside `Destroyed`/`Player Enters`; a cell or house route cannot use `Cap=Win/Des=Lose` at all. | the three `switch (Action)` bodies |
| E4 | `Any` only on the object route. | `trigger.cpp:556`, `:750` have no `EVENT_ANY` acceptance |
| E5 | One trigger per cell. Painting zone B over zone A's cells must repaint, not silently drop. | `display.cpp:1364` `!IsTrigger` |
| E6 | Cell 0 cannot carry a trigger. | `display.cpp:1363` |
| E7 | Aircraft and terrain cannot carry a trigger. `AircraftClass::Read_INI` reads no trigger field; the `[TERRAIN]` parser reads only the type despite the header comment. | `aircraft.cpp:502-549`, `terrain.cpp:795-817` |
| E8 | Trigger name unique case-insensitively, at most 4 chars. | `char Name[5]`, `stricmp` |
| E9 | No spaces around commas in emitted lines. | `Fill_In` does not trim tokens |
| E10 | `Data` field always emitted, even when unused. | `atol(NULL)` is UB |

**Warnings (allowed, because shipped content violates them and must round-trip):**

| # | Rule | Evidence |
|---|---|---|
| W1 | **Orphan extent.** An object-route event with no attachment can never fire. **16 shipped triggers are provably dead this way**: 7 `Player Enters` with no cells and no objects, 6 `Destroyed` with nothing attached, and 3 `Destroyed` attached to *cells* (the cell route only ever springs `PLAYER_ENTERED`). |
| W2 | **Dangling Cancel.** One shipped `Dstry Trig 'YYYY'` has no `YYYY` in its mission. It is a safe no-op (`delete NULL`), but it is almost certainly a bug. |
| W3 | **Cancel targeting an `Allow Win` rule.** I read the action body: `if (trig) trig->Remove(); delete trig;` and `Remove()` already ends in `delete this`. So the destructor runs twice, and `~TriggerClass` decrements `Blockage` each time (guarded, so it floors at 0, but a 2 can become a 0 in one action) and re-arms `BorrowedTime`. Real, observable, and worth a warning. |
| W4 | **Two house rules consuming the same latch.** `JustBuilt` and `IsDiscovered` are cleared by the first matching trigger in `HouseTriggers` order (`house.cpp:1455`, `:1485`), so a second `Built It` or `House Discov.` rule for the same house never sees it. `IsCivEvacuated` is *not* cleared, so `Civ. Evac.` rules all fire. |
| W5 | **Volatile removal skips the next rule.** `HouseClass::AI` increments its index while `Remove()` compacts the vector, so a self-removing rule causes the next one in file order to be skipped for that tick. Warn when two house rules for the same house could fire on the same tick. |
| W6 | **Semi-persistence on the house route is a no-op.** The house `Spring` overload has no `SEMIPERSISTANT` block, so `1` behaves as `2`. |
| W7 | **`Win` is inert while any `Allow Win` for that house survives.** Show this as a live badge on every `Win` node. |
| W8 | **Route disagreement on `Airstrike`.** Object route sets `PlayerPtr->IsAirstrikePending`; cell route enables it on the trigger's house; house route enables it on `PlayerPtr` regardless of the trigger's house. Surface the actual recipient per route in the node body. |
| W9 | **`Autocreate` scope varies by route**: springing object's house / all houses / the trigger's house. |
| W10 | Waypoint referenced by a Move/Unload chip is unassigned (`-1`); `team.cpp:477` converts it anyway. |

The error/warning split is not cosmetic. **The compiler must be able to emit provably broken content**, or the 95-file byte-equivalence gate fails on day one.

---

## 5. What this language explicitly cannot express

State these in the UI, not just the docs.

1. **The five hardcoded scenario patches.** `scb07ea` and `scb10eb` override land type on a cell; `scb09ea` and `scb13ec` attach triggers `dely`/`delx` to a GDI Radar that appear in no INI section; `scb13eb` *constructs* a trigger named `prod` at runtime, wires it into four cells (without setting `IsTrigger`, so it probably never fires), and forces `xxxx` to persistent. Plus `building.cpp:1963` attaches `win` to Chan on Nod 10. Importing these missions produces a graph that is correct about the file and silent about the code. Show a red "engine override present" banner on those six scenarios.
2. **`Fixup_Scenario` type-class mutations** (Orca laser on scenario 72, fraidycat civilians on `scg08eb`, `ModernBalance` deltas).
3. **Boolean composition.** No AND, no OR, no NOT. Every compound condition must be faked with the Cancel latch or with sequenced timers, and the editor should say so rather than offering a fake `AND` node that silently compiles to something else.
4. **Counters and variables.** The only stateful primitives are `Data` on the five data events and the semi-persistent `AttachCount`.
5. **Which house owns `[Base]`.** Derived from `[Basic] Player`, never stored. The editor shows it read-only with the derivation.
6. **Multiplayer seat count.** Derived as `min(count of defined waypoints 0-25, 6)`. Not a field.
7. **`[Briefing]` fallback** to a section named after the scenario root inside a global `MISSION.INI`. If a mission has no `[Briefing]`, the graph cannot show you the mission briefing text without reading a second file.
8. **Aircraft reinforcement teams** cannot be recruited, only delivered (`TeamClass::Recruit` handles infantry and units only).
9. **Base rebuilding is dead in this tree.** `Next_Buildable`'s only live call site is inside `#if (0)`, and `Suggest_New_Object` has no caller in `tiberiandawn/`. A Base Plan node authors data the shipped AI mostly ignores. Say so on the node, or a designer will spend a day tuning a rebuild list that does nothing.
10. **Retreat, Attack Tarcom, House Discov., Credits, No Factories.** Supported by the engine, used zero times in 82 missions, deliberately not offered. An "engine escape hatch" raw-line node covers anyone who insists.

---

## 6. Simulation in the editor

Following the sim recon's own recommendation, and adding one thing it identified but did not sequence.

### v0 prerequisites (fix before shipping any of this)

- **`SDLK_SPACE` at `cnc_eyes.cpp:18217` is not edit-gated.** Pressing space in the editor unpauses the tick block, `edit_sim_frozen()` correctly skips the advance, and then `refresh_objects()` at `:18888` overwrites the document with the brain's boot-time view. Every unsaved edit is gone, silently. One-line guard.
- **`edit_mod.h:4388`'s prior key is `house|type|cell`.** Move a trigger-attached object one cell and its trigger silently becomes `None` and its order silently becomes `Guard`. This is fatal for a mission-script editor: nudging a unit would detach it from the rule you just authored. Once the graph owns attachments, the trigger half of `g_editPrior` disappears entirely; the order half should move into the object record.

### v1: Trace Run (headless, no brain change)

The timing math forces this. Time triggers poll every 90 ticks, so SCG15EB's `chn1=Time,Reinforce.,310` fires at 27,900 ticks, which is 31 minutes of staring at 15 Hz. Headless with a full dump runs at roughly 39x real time (measured: 7,200 ticks in ~12.3 s), so the same question costs about 48 seconds.

Flow, using only machinery that already exists and is gated:

1. Ctrl+P → `edit_service_play()` → save, shutdown, `game_boot(edit=0)`. One reboot, ~2.24 s, only when the document is dirty.
2. Immediately `game_save_slot()` into a scratch slot outside the player's sixteen. That is the tick-0 keyframe. `CNC_Save_Load` snapshots Houses, TeamTypes, Teams and Triggers, so the scripting layer rewinds exactly (G54 already asserts this; the recon re-verified it byte-for-byte on SCG05EA).
3. Run N ticks headless. Keyframe every 900 ticks (about 222 KB each on SCG05EA).
4. `game_load_slot()` to scrub. It already performs every discontinuity reset a hand-rolled rewind would get wrong: shroud re-init, `efx_pool`, `g_vanished`, `g_unitPrev*`, `shatter_reset`, `dmg_reset`, placement cancel.

**Readout without a brain change.** The existing dump carries `mission=`, `nav=`, `tar=` per object plus `HOUSE|` credits and counts. From consecutive dumps you can *infer* most rule firings: new objects of a known TeamType composition appearing at a house edge is `Reinforce.`, appearing inside the base is `Create Team`, `sim_over` is `Win`/`Lose`, a house's building count starting to climb is `Production`. Light up the corresponding graph node with a confidence badge. This is lossy and must be labelled lossy, but it needs zero brain changes and answers the actual question ("did `atk3` ever fire?") for the 55% of rules whose action produces objects.

Never hash the payload for comparison. Two runs of the identical deterministic sim differ by ~272 bytes because vtable pointers are written raw and move with ASLR. Assert the state, not the file.

### v2: exact trace (needs the project owner's go-ahead, brain change)

Extend `CNC3D_Dump_Objects` with `TRIG|` and `TEAM|` lines. Triggers are free: every field is public and `Name_From_Event`/`Name_From_Action` already exist, so it is about fifteen lines. Teams need one accessor for `CurrentMission` and the `Member` chain, with `AnimClass::CNC3D_Stage()` as precedent. Cost is a two-platform brain rebuild and a growth of `brain/patches/vanilla-cnc3d.patch`.

With that, the graph becomes the debugger: a timeline scrubber under the canvas, nodes flashing as they fire, the Cancel edge animating when a latch is cut, a per-rule "fired at tick N" annotation, and a red "never fired in 27,900 ticks" badge that would have caught all 16 dead shipped triggers.

### Deliberately not v1

Simulating inside the editor chrome (splitting `g_editOn` across 43 sites in `cnc_eyes.cpp` and 22 in `edit_mod.h`). It is the expensive option, it is only correct when stacked on the reboot anyway, and the trace timeline is a better answer to the real question than a live picture at 15 Hz.

---

## 7. The graph file

`SCG15EB.mgraph.json`, sitting beside `SCG15EB.INI` in the missions directory. The engine opens files by name through `CCFileClass` and never asks for `.mgraph.json`, so it is invisible to it. If missions are later packed, add it as an extra member the engine never requests.

I considered embedding the graph as `;` comment lines inside the INI and rejected it: it would perturb the carrier document the whole design depends on, and the DOS `INIClass` line handling is not somewhere I want to bet a few thousand lines of JSON.

```jsonc
{
  "format": "cnc3d.mgraph/1",
  "scenario": "SCG15EB",
  "ini_sha256": "…",          // carrier bytes at last successful compile
  "semantic_digest": "…",      // canonical hash of Layer 1 only
  "nodes": [
    { "id": "n17", "kind": "rule", "bind": "xxxx",
      "pos": [420, 180], "group": "g2", "note": "infinite infantry bleed" },
    { "id": "n18", "kind": "rule", "bind": "delx", "pos": [780, 180] },
    { "id": "n31", "kind": "teamtype", "bind": "nod4", "pos": [420, 420] },
    { "id": "n40", "kind": "macro", "macro": "escalating_pressure",
      "emits": ["xxxx", "delx"], "pos": [420, 120], "collapsed": true }
  ],
  "edges": [
    { "from": "n18.Cancels", "to": "n17.CancelSlot" },
    { "from": "n31.out",     "to": "n17.Team" }
  ],
  "groups": [ { "id": "g2", "title": "East flank pressure", "colour": "#8a3" } ],
  "layout": { "grid": 20 }
}
```

Rules:

- Every node binds to a Layer 1 entity **by INI key** (`bind`), never by array index. Renaming a rule updates every `bind` atomically.
- If the sidecar is missing, the editor imports flat: one rule node per line, auto-laid-out in dependency order (event kind as columns, house as rows). Fully usable, just unpretty.
- If `ini_sha256` differs but `semantic_digest` matches, someone reformatted the INI. Re-anchor, keep the graph.
- If `semantic_digest` differs, someone hand-edited the mission. Re-import, keep positions for entities whose keys survived, park new ones in a "needs placement" tray.
- **Macros** are persistent nodes with an explicit `emits: [names]` binding. That is what makes the escalate-then-relieve idiom (`xxxx` persistent spawner + `delx` zone that cancels it, 41 uses across 25 missions) foldable into one node *and* re-foldable on import: the importer pattern-matches a persistent rule whose name is XXXX/YYYY/ZZZZ against a rule that cancels it. Any macro that fails to re-fold degrades to its constituent rule nodes, which is always safe because macro expansion is 1:1 with lines.

---

## 8. Prioritised roadmap

**v1 (the 90% cut).** Carrier-preserving parse and patch, the 95-file byte-equivalence gate, one Rule node with 5 events and 9 actions, the TeamType node with 6 verb chips, zone painting, object tagging, the waypoint placer, Mission Settings, errors E1-E10, warnings W1-W3, Cancel as a real edge, and Trace Run v1. Plus the two v0 bug fixes.

**v2.** `Attacked`, `Discovered`, the three superweapon grants, `Allow Win` with the live blockage badge, Base Plan editing (labelled inert), the macro library seeded from the survey's top idioms (timer→spawn 203, timer→reinforce 134, zone→ambush 82, wipeout→lose 76, escalate-and-relieve 41, tagged-group→hunt 35), warnings W4-W10, and the `TRIG|`/`TEAM|` brain extension with the exact trace timeline.

**v3.** The long tail: 7 remaining events, 2 remaining actions, 4 remaining verb chips, 28 uses total at 2.5%. A raw-line escape-hatch node covers them until then.

**Never.** `House Discov.`, `Credits`, `No Factories`, `Retreat`, `Attack Tarcom`.

---

## 9. Where the recon is uncertain or the reports contradict each other

1. **Direct contradiction, resolved.** The waypoints/base report claims `UnitClass::Read_INI`, `InfantryClass::Read_INI`, `BuildingClass::Read_INI` and `AircraftClass::Read_INI` "have no definition anywhere in `tiberiandawn/`" and recommends an entry in `known-gap notes`. The triggers report says they do exist and blames the ISO-8859 encoding for defeating plain `grep`. **The triggers report is correct.** I verified: `unit.cpp` is ISO-8859 text, `grep -a "UnitClass::Read_INI" unit.cpp` returns line 3770, and the INI entry format comment sits at line 3758. Do not file that gap.

2. **`Time` units.** The header comment at `trigger.h:264` says minutes; the code says `TICKS_PER_MINUTE / 10`, so 6 seconds. The code wins. Every timer node must display both ("14.0 min / Data=140") because half the community documentation on this is wrong.

3. **`[Base]` liveness.** The base report demonstrates that the rebuild consumer is behind `#if (0)` and that `Suggest_New_Object` is uncalled, but `Percent` is nonetheless present in all 95 files and is 100 in 50 missions. I have not traced whether some other path (the remaster's RA-AI variants at `house.cpp:6493+`) revives rebuilding. Treat "Base Plan is inert" as *probable, not proven*, and label the node accordingly rather than hiding it.

4. **`Special` house sections.** The house report says every house in `HOUSE_FIRST..HOUSE_COUNT` is constructed whether or not its section exists. 86 files carry a `[Special]` section. Nothing in the recon explains what `Special` does in single-player scripting. I did not chase it. Preserve, do not offer as an authorable house until someone traces it.

5. **`Attacked` house route.** The triggers report describes `HouseClass::Attacked` springing `EVENT_ATTACKED` gated on `SpeakAttackDelay` and on the attacked house being the player's. I found 4 shipped `Attacked` triggers with no attachment, which can only be reaching that path. The exact gating conditions matter for the v2 node and should be re-read at `house.cpp:1692` before implementing, not taken from the summary.

6. **The `Dstry Trig` double-delete.** I read it directly and it is real: `Remove()` ends in `delete this`, then the action does `delete trig` on the same pointer. The observable consequence is that `~TriggerClass` runs twice, and for an `Allow Win` target that means `Blockage` can drop by 2 in one action (floored at 0 by the `if (Blockage)` guard) and `BorrowedTime` is re-armed. Whether the second `operator delete` on a `TFixedIHeapClass` slot is benign is not something I confirmed; shipped missions survive it, which is weak evidence at best. Reproduce it deliberately if bit-exactness matters, and guard it if it does not.