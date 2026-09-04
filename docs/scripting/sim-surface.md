## 1. How the renderer drives the brain

**The tick call.** `CNC_Advance_Instance` is resolved by `dlsym` into a function pointer at `game/cnc_eyes.cpp:13480` (declared `:187`, typedef `:177`), and every caller goes through one wrapper:

```c
/* cnc_eyes.cpp:11001 */
static bool brain_advance(uint64 player)
{
    const bool alive = BrainAdvance(player);
    cheat_post_advance();
    return alive;
}
```

**The live loop** is `game_loop`, `game/cnc_eyes.cpp:17669`. The tick block is `:18810-18896`:

```
if (!paused && !g_optOpen && !sim_over) {
  while (now >= next_tick && guard < 5) {       // :18812, catch-up capped at 5
     cheat_tick();                              // :18817
     if (edit_sim_frozen()) { } else            // :18818
     if (!brain_advance(0)) { ...verdict or stop... break; }   // :18819
     g_simTicks++; tick_count++;
     next_tick += 1000.0 / SPEED_HZ[game_speed_setting()];     // :18849
     refresh_objects();                         // :18888  <-- every tick
  }
  if (guard) sync_selection_from_brain();       // :18892
}
```

`SPEED_HZ` is `{7.5, 10, 12.5, 15, 18, 22, 27}` (`:17706`), default index 3 = 15 Hz, the anchor everything is gated against.

**How state comes back.** The renderer never reads engine memory. `refresh_objects()` (`:2770`) calls `capture_brain_call(BrainDump)` (`:2810`), which dups `stdout` onto a scratch file, calls `CNC3D_Dump_Objects` (`brain/vanilla/tiberiandawn/dllinterface.cpp:8152`), rewinds it and parses the text. The whole world arrives as pipe-delimited lines: `OBJ|` (`:8100`), `CARGO|` (`:8266`), `EFX|ANIM` (`:8326`), `EFX|BULLET` (`:8358`), `HOUSE|` (`:8380`), `TIB|` (`:8407`), `WALL|` (`:8463`), `SMUDGE|` (`:8509`), `VIEW|` (`:8518`), bracketed by `OBJDUMP-BEGIN` / `OBJDUMP-END total= frame=`.

**There are six tick sites, all identical in shape:** the live loop (`:18818`), the script runner's `script_tick` (`:14007`), `game_run_shot`'s warm-up (`:17369`), `game_run_script`'s warm-up (`:17340`), the `remission` verb's warm-up (`:14645`), and the TAB single-step key (`:18311`).

---

## 2. What `edit_sim_frozen()` actually gates

```c
/* game/edit_mod.h:180 */
static inline bool edit_sim_frozen(void) { return g_editOn; }
```

It gates **exactly one thing: the call to `brain_advance()`**, at those six sites and nowhere else. The comment above it (`edit_mod.h:163-179`) says why it is a function and not `game_loop`'s `paused` local: the freeze has to cover the script runner and the shot path too, and it was added after a round-trip test caught a Nod turret's facing drifting 160 -> 124 because it had been tracking a target while the editor believed the world was still.

**What it does NOT gate**, and this matters:

- `refresh_objects()` (`cnc_eyes.cpp:18888`, `:14022`, `:17388`) still runs. It unconditionally clears and rebuilds `g_objects`, `g_walls`, `g_tib`, `g_smudges` from the brain dump (`:2781-2786`). Those four vectors **are** the editor's document.
- `cheat_tick()`, `sb_poll()`, `shroud_fetch()`, `audio_frame()`, the draw.

The thing that actually protects the document in the interactive editor is a second, separate belt: `bool paused = g_editOn` at `cnc_eyes.cpp:17719`, which skips the whole tick block including the `refresh_objects()` at `:18888`.

**A live bug falls straight out of that split.** `SDLK_SPACE` is not edit-gated:

```c
/* cnc_eyes.cpp:18217 */
case SDLK_SPACE:  if (!sim_over) paused = !paused; break;
```

It is the only SPACE handler in either file. Press SPACE in the editor: `paused` goes false, the tick block runs, `edit_sim_frozen()` correctly skips the advance, and then `refresh_objects()` at `:18888` overwrites the document with the brain's view of the mission **as booted**. Every unsaved edit is silently gone, and nothing is printed. (The comment at `:17714-17717` says SPACE is deliberately left live in edit mode "so Play is reachable from the same key", which is how the hole got there.) I have not changed anything.

Separately, `g_editOn` itself is overloaded: it means freeze (`edit_mod.h:180`), draw the editor chrome and own the mouse (~43 sites in `cnc_eyes.cpp`), hide the game sidebar (`edit_mod.h:4960`), suppress the shroud (`cnc_eyes.cpp:17078` -> `:11188`), and swallow the pause and cheat dialogs (`:11030`, `:11050`, `:11132`). Any "simulate in the editor" feature has to split that bundle.

---

## 3. Run forward N ticks and reset: what exists

**Yes for the brain, and the primitive is already built and gated. No for the editor's document, which is the harder half.**

### The brain side: a complete snapshot/restore path exists

`CNC_Save_Load` is resolved at `cnc_eyes.cpp:13501` (typedef and contract at `:165-169`) and wrapped by `game_save_slot` (`:16812`) and `game_load_slot` (`:16884`). The engine's `Save_Game` writes all fifteen object heaps plus the map, score, base and `Save_Misc_Values` (including `Frame`), and critically **it saves the scripting layer too**:

```
brain/vanilla/tiberiandawn/saveload.cpp:203
  Houses.Save() || TeamTypes.Save() || Teams.Save() || Triggers.Save() || Aircraft.Save() ...
:440  the matching Load
:793-795 / :889-891  Code_Pointers / Decode_Pointers for all three
```

The renderer adds a 128-byte index record for what the brain does not know it has: camera, `.pack` name, campaign position, music (`cnc_eyes.cpp:16846-16862`, `game/dossave.h`).

`game_load_slot` also does the discontinuity work a rewind needs, and this is the part you would otherwise have to rediscover (`:16925-16966`): reset `g_viewApplied`, re-init the shroud, clear `efx_pool`, clear `g_vanished` / `g_unitPrev*` (the vanished-unit tracker diffs consecutive dumps and would spawn wreckage for every unit that "disappeared"), `shatter_reset()`, `dmg_reset()`, cancel placement, `refresh_objects()`, `sync_selection_from_brain()`.

**Gate G54 already asserts exactly the operation you want** (`game/gates.sh:1991-2010`, script `playable/gate_saveload.txt`): tick 300, save, tick 200, load, and the object dump must come back byte-identical.

I re-ran that round trip against SCG05EA (a trigger-heavy campaign map, not the gate's SCG01EA) to confirm it is not scenario-specific:

```
SAVE|slot=0|frame=300|scen=SCG05EA|bytes=222742
LOAD|slot=0|frame=300|scen=SCG05EA|objects=131
448 dump lines (OBJ|/TIB|/WALL|/HOUSE|/SMUDGE|) identical pre-save vs post-load
```

Note `known-gap notes:1614`: **never hash the payload**. Two runs of the identical deterministic sim differ by ~272 bytes because vtable pointers are written raw and move with ASLR. Assert the state, not the file.

**Two refusals to know about:** save and load are both refused outright during a skirmish (`cnc_eyes.cpp:16825-16833` and `:16891-16897`), because `CNC_Save_Load` takes the game mode as an argument and *sets* it. And `game_load_slot` refuses a slot whose scenario differs from `g_bootScen` (`:16912-16919`).

### The editor side: the edits are not in the brain

`edit_mod.h:31-38` states the model plainly: *"Not edit the live sim. The brain exposes no create-with-house-and-cell, no delete and no move."* I checked that claim. The nearest thing is `DEBUG_REQUEST_SPAWN_OBJECT` (`dllinterface.cpp:6791` -> `Debug_Spawn_Unit` `:7091`), and it is not sufficient: the house is `PlayerPtr`'s or "the enemy with the most stuff" (`:7113-7120`), the position comes from a screen pixel through `Map.Pixel_To_Coord`, and `Try_Debug_Spawn_Unlimbo` (`:6908-6930`) scans forward for the first legal cell rather than honouring the one you asked for.

So the only route from edited document to running sim is the file and a restart, which is what `edit_service_play` does (`game/edit_mod.h:4907`):

```
edit_save();            // :4944  writes .INI/.BIN/.HGT
game_shutdown();        // :4952
game_boot(win, &next);  // :4953  next.edit = 0
```

I measured that reboot: **~2.24 s wall** for boot alone (SCG05EA, 7.9 MB pack), on this machine.

### What would have to be saved and restored, precisely

The renderer-side half already has its own snapshot type, and it is exactly the right shape:

```c
/* edit_mod.h:2344 */
struct EditSnap {
    std::vector<SimObject> objects; std::vector<WallCell> walls;
    std::vector<TibCell> tib; std::vector<SmudgeCell> smudges;
    std::vector<unsigned char> tmpl, icon;    // the .BIN
    std::vector<unsigned char> corner;        // the .HGT
    std::vector<unsigned char> tier, pass, ramp;
    int waypoint[28]; char label[40];
};
```
`edit_snapshot()` `:2360`, `edit_restore()` `:2385`, 200-deep undo stack `:2356`. A busy map is a few tens of KB. So the full recipe for "simulate then reset" is: `EditSnap` for the document + a `game_save_slot` payload for the world + the discontinuity resets `game_load_slot` already performs.

**One trap in the current reboot path:** `game_boot` clears `g_undo` and `g_redo` on every edit-mode boot (`cnc_eyes.cpp:17095-17096`) and resets the REVERT baseline (`:17097`). So today, playtesting costs you your undo history.

---

## 4. Can you see what the AI is doing?

**Per unit, yes. Per team or per trigger, nothing at all.**

### What the dump does carry

`OBJ|` (`dllinterface.cpp:8100-8152`) carries three fields that are genuine AI intent:

- `mission=` : `MissionClass::Mission_Name(Get_Mission())` (`:7788-7791`), so `Guard`, `Hunt`, `Attack`, `Move`, `Harvest`, `Unload`...
- `nav=` : `As_Cell(foot->NavCom)`, where the object has been ordered to go (`:7896-7901`)
- `tar=` : `As_Cell(techno->TarCom)`, what it has been ordered to shoot (`:7906`)

plus `act=` (the owning house's `ActLike`, `:8092-8096`), and `HOUSE|` lines with credits, unit and building counts (`:8380`). `EFX|ANIM` and `EFX|BULLET` show shots landing. Combined, you can watch an attack wave form and move without seeing the team that made it.

### What is missing

I searched both the dump and the renderer's parser. There is **no `TEAM|`, no `TEAMTYPE|` and no `TRIG|` line anywhere.** `CNC3D_Dump_Objects` walks Infantry, Units, Buildings, Terrains, Aircraft, Anims, Bullets, Houses and three per-cell sweeps, and stops (`:8156-8520`). The only hits for "TEAM" in `cnc_eyes.cpp` are the player's **control groups** (`Handle_Team`, `cnc_eyes.cpp:9560-9568`), which are a different thing entirely, and the only hit for "TRIGGER" (`:14102`) is an animation trigger in the `anims` script verb.

### What it would take, concretely

All three live in ordinary heaps beside the ones already walked:

```
brain/vanilla/tiberiandawn/externs.h:175-177
  extern TFixedIHeapClass<TriggerClass>  Triggers;
  extern TFixedIHeapClass<TeamTypeClass> TeamTypes;
  extern TFixedIHeapClass<TeamClass>     Teams;
externs.h:185  extern DynamicVectorClass<TriggerClass*> CellTriggers;
```

- **Triggers are free.** Every field you want is public: `Name`, `Event`, `Action`, `House`, `Data`, `AttachCount`, `IsPersistant`, `Team` (`trigger.h:213-260`), and there are static `Name_From_Event` / `Name_From_Action` helpers (`trigger.h:190-193`). A `TRIG|` loop is about fifteen lines and nothing in the engine needs touching.
- **Teams are mostly free.** `Class`, `House`, `Center`, `ObjectiveCenter`, `MissionTarget`, `Target`, `Total`, `Risk`, and the state flags `IsForcedActive / IsHasBeen / IsFullStrength / IsUnderStrength / IsReforming / IsLagging` are public (`team.h:49-165`). `CurrentMission` and the `Member` chain are private (`team.h:230-256`), so those two need an accessor. There is precedent for exactly that in the patch: `AnimClass::CNC3D_Stage()`, used at `dllinterface.cpp:8333`.

Cost: this is a brain change, so `brain/patches/vanilla-cnc3d.patch` grows and it is a two-platform rebuild (`tools/mac/build-brain-mac.sh`, not part of `game/build.sh`; G30 now checks the deployed brain as a chain). Per the skirmish precedent in `engineering notes:409` a brain commit needs the project owner's go-ahead.

### The timing number you need before any of this is useful

Time triggers are polled once every `TICKS_PER_MINUTE / 10` = **90 engine ticks** (`house.cpp:1225-1235`, `defines.h:2219-2220`), and each poll decrements `Data` by one (`trigger.cpp:365-371`). So SCG05EA's `grd2=Time,Create Team,40` fires at **3,600 ticks** and `auto=Time,Autocreate,140` at **12,600 ticks**, i.e. 4 and 14 minutes of game time. Watching a mission's script play out at 15 Hz is a long sit. That single fact drives the answer to question 5.

### One editor-side hazard, directly on this topic

The editor deliberately owns only six INI sections (`edit_mod.h:4333`: `TERRAIN, OVERLAY, SMUDGE, STRUCTURES, UNITS, INFANTRY`); `[Triggers]`, `[CellTriggers]`, `[TeamTypes]`, `[Base]`, `[Waypoints]` and `[REINFORCEMENTS]` are copied through untouched (`edit_write_ini`, `:4561-4600`). Per-object trigger and order strings are preserved by re-reading them at boot into `g_editPrior` (`edit_read_prior`, `:4425`) and looking them back up on write.

But the lookup key is `house|type|cell`:

```c
/* edit_mod.h:4388 */
snprintf(out, n, "%s|%s|%d", house, type, cell);
/* :4471, :4477 */
return e ? e->order : "Guard";
return e ? e->trig  : "None";
```

**Move a trigger-attached object one cell in the editor and the key misses, so its trigger silently becomes `None` and its order silently becomes `Guard`.** That is a real defect for anyone editing a scripted mission, and it is precisely the class of thing a simulate-and-watch feature would be used to chase.

---

## 5. Cheapest honest path to "press play, watch, rewind"

### Option A: it already exists, and is worth naming as the baseline

Ctrl+P (`cnc_eyes.cpp:18369-18376`) -> `edit_request_play()` -> `edit_service_play()` (`edit_mod.h:4907`) -> save, shutdown, boot with `edit=0`. Ctrl+P again reboots back into the editor from the file. Zero new code.

Tradeoffs, all measured or cited: every playtest **writes the map to disk** (`:4944`), the reboot costs **~2.24 s**, the undo stack is cleared (`cnc_eyes.cpp:17095`), the camera snaps back to the console camera (`edit_mod.h:4962-4967`), and "rewind" is a re-read of the file, so the run you just watched cannot be stepped back into or replayed. Also the gate-only script verb `editplay play|edit` (`cnc_eyes.cpp:14583-14605`) drives the same function, so a harness for this needs no new plumbing.

### Option B (recommended core): reboot once, then rewind with the save file

After `edit_service_play(req=1)` has booted the mission, immediately `game_save_slot()` into a scratch slot; that is your tick-0 keyframe. Run as long as you like; `game_load_slot()` snaps back exactly, including Teams, TeamTypes and Triggers. Save every N ticks and you get scrubbable keyframes.

Why this is the honest cheap path: the primitive is built, gated (G54) and I re-verified it byte-for-byte on a trigger-heavy map; `game_load_slot` already contains all the discontinuity fixes that a hand-rolled rewind would get wrong.

Tradeoffs: one 2.24 s reboot per *edit* (not per replay). Skirmish is excluded (`:16825`, `:16891`). It needs a scratch slot outside the player's sixteen, or it eats one (`game/dossave.h`). And the payload is 222 KB per keyframe on SCG05EA, so keyframes are cheap but not free.

### Option C: make it feel native, by splitting `g_editOn`

To watch the sim inside the editor chrome (editor camera, no game sidebar, panel still up), add a third state:

1. `edit_sim_frozen()` becomes `return g_editOn && !g_editSimRun;` (`edit_mod.h:180`).
2. On entry, park the document with `edit_snapshot()` (`:2360`), because `refresh_objects()` (`cnc_eyes.cpp:2770`) will overwrite `g_objects/g_walls/g_tib/g_smudges` the moment the sim ticks.
3. On exit, `edit_restore()` (`:2385`) plus `game_load_slot()`.
4. Fix `SDLK_SPACE` (`:18217`) at the same time, since it is the same hole.

Honest caveat: **on its own this simulates the map on disk, not the edits on screen**, because the brain still holds the scenario as loaded at boot. Option C is only correct stacked on B: reboot once when the document is dirty, then C for everything after. Cost is mostly the `g_editOn` untangle (43 sites in `cnc_eyes.cpp`, 22 in `edit_mod.h`), not the freeze flag.

### Option D: headless simulate-and-report, no live picture

Run the sim forward in the background and show a *timeline* of what fired, rather than a moving picture. I measured throughput on SCG05EA with a full object dump every tick: **7,200 ticks (8 minutes of game time) in ~12.3 s of sim, ~39x real time**; 3,600 ticks in ~5.7 s.

That is the right shape for the actual question a mission author asks ("does `atk3` ever fire? does `grd1` ever reach its waypoint?"), because at 15 Hz that question costs four to fourteen minutes of staring. It also needs no chrome decoupling at all. But it is worth nothing without question 4's `TRIG|`/`TEAM|` lines, so it is really "the brain dump extension, plus a report view".

### Option E: a second sim process or a second brain instance. Not viable.

The dylib is `dlopen`'d once behind a `g_brainLoaded` latch (`cnc_eyes.cpp:13414`, `:13431-13468`) and the engine is all file-scope globals (`brain/vanilla/tiberiandawn/globals.cpp`). Two worlds in one process would need a second physical copy of `TiberianDawn.dylib` under a different filename, and the project owner's stated requirement (`docs/design-map-editor.md` section 0e) is one window, one application. Rule it out and say so.

### Suggested order

1. Fix the SPACE hole (question 2). It is a one-line guard and it currently eats unsaved work.
2. **B**: tick-0 keyframe on entering play, `game_load_slot` on Ctrl+P back. Small, uses only built and gated machinery, and turns "rewind" from a 2.2 s file reload into an exact snap-back.
3. **The brain dump extension** (`TRIG|`, `TEAM|`, `TEAMTYPE|`). Fifteen to forty lines in `CNC3D_Dump_Objects`, one accessor on `TeamClass`, one two-platform brain rebuild. This is what turns watching into understanding, and it is the only item that needs the project owner's go-ahead.
4. **D** on top of 3: a fast-forward-and-report button, since the trigger clock makes real-time watching impractical.
5. **C** last, once it is clear the project owner wants the sim inside the editor chrome rather than a report plus the existing Ctrl+P.

Also worth fixing alongside, since it will bite anyone testing a scripted mission: the `house|type|cell` prior-key at `edit_mod.h:4388` detaches an object's trigger the moment you move it.