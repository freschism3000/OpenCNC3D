# Multiplayer, the implementation map

`docs/design-multiplayer.md` argues for deterministic lockstep on `EventClass` orders and
says which phases exist. This file is the other half: where in the tree each phase actually
lands, by file and line, and what each change costs.

It exists because the design's own line references had already drifted by the time the work
started, and a map that names the wrong line is worse than no map. Every reference below was
read out of the tree rather than remembered.

**How to read the verification column.** A claim marked **verified here** was re-derived from
the source while writing this file. A claim marked **reported** came out of the survey and is
recorded because it is useful, not because it has been proven twice. One headline claim was
checked and found wrong; it is kept below, marked, because the wrong version is plausible and
somebody will find it again.

---

## 0. The six surfaces, and the order they have to land in

| # | Surface | Where | Lands |
|---|---|---|---|
| 1 | The four input paths that bypass the event queue | `brain/vanilla`, the additive patch | before any netcode |
| 2 | Determinism holes in the simulation | `brain/vanilla`, the additive patch | before any netcode |
| 3 | The brain exports lockstep needs | `brain/vanilla`, the additive patch | with the scheduler |
| 4 | The host turn gate | `game/cnc_eyes.cpp` | with the scheduler |
| 5 | The lobby, from skirmish arming to two peers | `game/`, `menu/`, `app/`, `net/` | after the gate |
| 6 | Build wiring for a new `net/` file | build scripts | continuously |

Surfaces 1 and 2 are the ones the design calls Phase 1, and nothing above them is safe until
they land. Surface 6 is the only one already done.

---

## 1. The four input paths that bypass the event queue

Four gestures mutate the world without producing an order, so under lockstep they would
happen on one peer and nowhere else. **All four have a matching `EventClass` arm already
compiled into the shipped binary**, so three of them are one-line swaps onto code EA already
wrote.

| Path | Site | The arm it should use |
|---|---|---|
| Repair | `dllinterface.cpp:6554` calls `building->Repair(-1)` directly | `EventClass::REPAIR`, `event.cpp:440` |
| Sell | `dllinterface.cpp:6612` calls `building->Sell_Back(1)` | `EventClass::SELL`, `event.cpp:455` |
| Wall sell | `dllinterface.cpp:3746` calls `PlayerPtr->Sell_Wall(cell)` | `EventClass::SELL` with a cell target |
| Placement | `dllinterface.cpp:5267` calls `PlayerPtr->Place_Object(...)` | `EventClass::PLACE`, `display.cpp:3922` |

**Placement is easier than the design assumed, and this is the finding worth carrying.** The
design treats it as hard because the call returns a legality answer the sidebar is waiting
for. **The answer never reaches the host**, which is the accurate form of it: `Place_Object`'s
bool IS read, one line later at `dllinterface.cpp:5267-5269`, and its only effect is clearing
`PlacementType[]`, which is presentation state the host learns on the next poll anyway. What
the host sees is `DLLExportClass::Place`'s own bool, and its sole caller discards that. Note
`Place` does NOT return `true` unconditionally: it returns `false` at `:5217` when
`Set_Player_Context` fails, and reaches `return true` at `:5272`. `CNC_Handle_Sidebar_Request`
is `void`, `sb_request` is `void`, and the host discards `sb_place_at`'s bool at
`cnc_eyes.cpp:24301`. What placement actually needs is not a round trip but a **double submit latch**,
because the click that used to be answered inside the call now leaves a window as wide as
`MaxAhead` in which a second click queues a second `PLACE`. Reported; the eight readers of
`PlacementType[]` were enumerated in the survey and all are presentation.

**Two deliberate behaviour changes come with this, and one is player visible.** Routing sell
through the arm calls `Sell_Back(-1)`, which toggles, where the direct call passes `1`, which
forces. That RESTORES the 1995 gesture where a second click cancels a demolition in progress.
**A second claim this document carried was wrong and is corrected here.** It said the ownership
tests at `:6553` and `:6611` compare SIDES (`->Class->House ==`) rather than house pointers, so
two players on one side could repair and sell each other's buildings. They do not. `Class->House`
is the house's own `HousesType`, not its side; the side is `ActLike` and neither test reads it.
Exactly one `HouseClass` exists per `HousesType`, which the engine relies on in
`HouseClass::As_Pointer`, so the comparison is equivalent to a pointer compare and is correct
today. **The real hazard runs the other way.** The REPAIR arm (`event.cpp:440-446`) is
`if (techno && techno->IsActive) { techno->Repair(-1); }` and carries no ownership test at all,
while the SELL arm beside it does (`event.cpp:455`). So routing repair through the queue REMOVES
the check the direct path performs, and the ownership filter has to be added to the arm in the
same commit. That is not a side effect that comes for free, and it is the one thing in this
surface that must not be forgotten.

The sell toggle belongs in front of a decision before it lands, and in the changelog.

**`EventClass::MPlayerID` is never initialised by any constructor.** Declared at
`event.h:115`, written by none of its eleven constructors (ten in `event.cpp`, plus the inline default at
`event.h:217`), and read by nothing
today, so it is uninitialised bytes copied into `DoList` and nothing notices. The moment
execution order keys on it, that garbage decides everything, and it would also go on the
wire. Stamping it is load bearing rather than tidy. **Verified here:** the constructors at
`event.cpp:105-111` and `:131-137` set `Type`, `Frame` and `ID` and stop.

Also in this surface: the reentrant `DLLExportClass::Glyphx_Queue_AI()` call at
`dllinterface.cpp:5116`, which pumps the queue from inside a request handler so the sidebar
has its factory link before the function returns. Deleting it costs nothing in the current
host, because `Sidebar_Glyphx_Recalc` already runs after the normal pump in the same tick.

---

## 2. Determinism, and one headline that did not survive checking

### The two `Calculated_Cell` arms

`DisplayClass::Calculated_Cell` reads the local camera and draws from the shared random
stream, which is the design's flagship determinism bug. The survey found both arms have exact
fixes:

- `display.cpp:2903`, the `SOURCE_AIR` arm: falls back to `Coord_Cell(TacticalCoord)` when
  the reinforcement waypoint is unset. Reported as provably value identical on every shipped
  map.
- `display.cpp:2923`, the `SOURCE_VISIBLE` arm: draws twice from `Random_Pick` against the
  local camera. Reported as unreachable from all 102 mission INIs, on the evidence of every
  `Edge=` value they carry.

The XL fork carries both arms byte for byte at an offset of 18 lines.

### IRandom is libc rand(), and where that actually bites

**Verified here:** `IRandom` is not the engine's RNG. `common/irandom.cpp:122` is
`while ((num = (rand() & mask) + minval) > maxval);`, so it draws from the C library's
`rand()`, whose sequence differs between platforms for the same seed.

**The survey's headline for this surface was that `scenarioini.cpp` feeds `IRandom` straight
into AI behaviour at load, so two peers on different platforms diverge before tick 0 on every
skirmish. That is wrong, and it is wrong in an interesting way.** All four sites
(`scenarioini.cpp:580`, `:595`, `:913`, `:928`) sit inside `#ifndef USE_RA_AI`, and
`defines.h:56` defines `USE_RA_AI` unconditionally with nothing anywhere undefining it. So
they are compiled out. `MPlayerBlitz` is initialised to `0` at `globals.cpp:581`, its only
non-zero writer is that dead block, and its one consumer at `foot.cpp:699` therefore always
takes the second half of its condition. `BlitzTime`'s only writer besides `Clear()` is the
same dead block. There is no divergence here on any platform. The patch file already carries
a comment about a neighbouring block "which USE_RA_AI compiles out", so this trap has caught
somebody in this tree before.

**Two live `rand()` sites survive the correction, and both are real.**

1. `dllinterface.cpp:1761`, immediately after `Frame++` in the frame logic:
   `if (GameToPlay != GAME_NORMAL && MPlayerGhosts && IRandom(0, 10000) == 1)`. This draws
   from `rand()` every tick of a multiplayer game with ghosts on. The result only gates a
   computer message, but the draw consumes the shared `rand()` stream. **Verified here.**
2. `dllinterface.cpp:924`, `srand(timeGetTime())` in `GlyphX_Assign_Houses`, followed at
   `:958-960` by a Fisher-Yates shuffle of start locations using `rand()`. **Verified here.**
   This is the same armed shuffle the design already registered: it is inert only because the
   host always passes an explicit start index, so a lobby that offers random starts trips it
   on the first match. Note the seed is wall clock, so two peers would differ even on the same
   platform.

### The rest of the surface

- `infantry.cpp:1810`: the selection spillage leak, where a selected infantryman reports a
  larger spillage list than an unselected one, putting local selection state into a shared
  structure.
- `event.cpp:482`, `anim.cpp:790`, `anim.h:104`: the beacon expiry reads a wall clock. Both
  reads are inside `#ifdef _WIN32`, so on macOS they are dead code. That asymmetry is itself
  the problem for a cross platform match.
- `options.cpp:938`, `OptionsClass::Normalize_Delay`, whose own banner says it assumes a fixed
  frame rate.

**The survey's correction to the design's section 3.4 is worth recording.** The engine reads
no wall clock in the live simulation path outside those two `#ifdef _WIN32` blocks, and the
host's accumulator advances exactly one tick per iteration regardless of the rate setting. So
the speed slider and the catch up clamp do not corrupt simulation STATE; what they do is make
peers run at different real time rates, which produces a stalled match rather than a divergent
one. Both still want fixing, for the reason that a match where one peer is permanently the one
everybody waits for is not a match.

---

## 3. The brain exports lockstep needs

Four exports and two guarded lines, all in `dllinterface.cpp` plus one enum in
`dllinterface.h`.

| Export | What it does |
|---|---|
| `CNC3D_Event_ABI` | reports the wire unit's size and layout as compile time constants |
| `CNC3D_Set_Lockstep` | sets the flag, which is `false` until something calls this |
| `CNC3D_Drain_Events` | takes this peer's pending orders out for the wire, stamping `MPlayerID` |
| `CNC3D_Post_Event` | injects a received order at its stamped frame |

The two edits to live code are `dllinterface.cpp:6079` (the `OutList` to `DoList` drain, which
also discards overflow through an empty statement at `:6081-6083`) and `:5116` (the reentrant
pump). Both become `if (!Lockstep) { <the existing code verbatim> }`.

**These belong in the additive patch to `brain/vanilla`, not in the Enhanced fork only**, and
the argument is worth keeping because it will be re-asked. The byte-for-byte CI guard is not a
prohibition: `tools/check-brain-checkout.sh --regen` regenerates the patch in the same commit,
which is exactly what its own header offers. The patch already covers 33 files. And the shipped
game loads the classic brain, while the design's Phases 0 to 4 deliver eight player multiplayer
on that brain and are worth shipping alone; XL-only would postpone all multiplayer behind a
phase that has not started.

The inertness argument is stronger than the precedent it would join. With the flag off, the
two live edits are a load and a branch on a bool whose only writer is an export the host does
not call. The rally point arms the patch already carries have no flag at all, and their
inertness rests on a hand survey of every writer of `ArchiveTarget`.

**The trap in this surface**: draining orders out and not feeding them back. Dropping the local
copy is correct only because the host posts its own bytes back through `CNC3D_Post_Event`. A
first implementation that drains without self-adding, in a host that does not loop back, loses
every local order and looks like a network fault.

---

## 4. The host turn gate

There is exactly **one** live advance, `cnc_eyes.cpp:24797`, inside the accumulator that starts
at `:24790`. Six more exist in scripted and shot paths, and all seven funnel through one
chokepoint, `brain_advance()` at `cnc_eyes.cpp:13286`, whose own header comment already says
every advance goes through it. That is what makes the barrier a small change.

Three places, and nowhere else:

1. **A block right after `brain_advance` (`:13299`)**: the barrier state and four functions,
   `net_turn_ready`, `net_begin_turn`, `net_end_turn`, `net_order_sink`. The loop then calls
   three functions and knows nothing about sockets, which is the seam `net/lockstep.h` already
   keeps between the rules and `net_udp.c`.
2. **Four edits inside the accumulator (`:24788` to `:24874`)**: do not let a local pause stall
   every peer; `if (!net_turn_ready()) break;` as the first statement of the while body;
   `net_begin_turn()` and `net_end_turn()` around the advance including on the false branch;
   and the debt clamp at `:24871` expressed in turns with a report rather than a silent second
   of forgiveness.
3. **The refusal inside `brain_advance` itself**, which is what makes the other six advance
   sites safe without editing six loops. TAB (`:23481`) is the one bypass a player can reach
   without a command line switch and wants a message.

Take the tick rate with it: `SPEED_HZ` at `:22278`, the interval at `:24827`, the interpolation
alpha at `:24891`, and the lobby field in `cnc_game.h:78`. The rate has to become a property of
the match rather than a local slider, or the fast peer spends its life at the barrier.

---

## 5. The lobby

**One function arms a match**: `arm_skirmish` at `cnc_eyes.cpp:16388`. It hardcodes three
things a network lobby has to take away from it: the human is seat 0 (`:16429`, `:16462`,
`:16471`), every other seat is AI, and start positions are always explicit (`:16470`).

**The seat-0 problem is 62 literal call sites, not an unbounded sweep.** They live in three
files that compile as one translation unit, and 47 of them need the local seat. The survey's
recommended shape:

1. **Seat identity, no network.** One `g_seat` above `cnc_eyes.cpp:357`, seats numbered from
   **1** rather than 0, and `--seat N` beside `--skirmish`. Numbering from 1 is the whole
   safety argument: `Set_Player_Context` returns false for an id no seat carries and every
   entry point except `CNC_Advance_Instance` opens by checking it, so a missed site fails
   loudly with a blank sidebar instead of silently acting as seat 0.
2. **The roster becomes data**, so `arm_skirmish` stops deriving `human` from `i == 0`.
   Provable with no sockets: arm a two human, zero AI skirmish and watch seat 1's house get no
   expert system AI.
3. **The handshake**, modelled on `netcheck`'s HELLO/WELCOME, `--host` and `--join <addr>`
   first so the gate needs no UI.
4. **The UI last and smallest.** There is no text box anywhere in the tree, so a typed address
   is the expensive option; LAN discovery through one `setsockopt(SO_BROADCAST)` lets a joiner
   pick a host off a list instead. The menu entry already exists and is disabled at
   `menu/dosmenu.c:62`.

**Three things in this surface no grep will find**, and they are the reason the 62 is a floor
rather than a ceiling:

- `game/shroud_mod.h:195` passes a literal `0` through a function pointer parameter, so it is
  invisible to any search for the API name. It was found by reading.
- Six call sites carry no player id at all and read whatever context was last set: five
  `BrainProbe` calls and one `BrainDumpSel`. At `cnc_eyes.cpp:10990` the probe runs BEFORE the
  input calls that follow it, so it uses leftover context.
- The only reliable sweep is to change the ABI typedefs to take a named seat type and let the
  compiler find the callers.

The random start guard belongs in step 1, before any lobby can offer the option, for the reason
in section 2.

---

## 6. Build wiring: what a new `net/` file has to touch

Done, and recorded so the next file is mechanical.

| File | What it needs |
|---|---|
| `game/build.sh` | a compile line, plus the name in `BUILD_SOURCES` for the content stamp |
| `app/build.sh` | a compile line and the object on the link line |
| `tools/win/sources.sh` | append to `WIN_NET_C`, and nothing else |
| `tools/win/build-win.sh` | join the `c89` line, or the `gnu89` one if it includes winsock |
| `game/gate_fresh.sh` | already globs `net/*.c` and `net/*.h` |
| `game/gates.sh` | a new headless test binary joins G130 or gets a sibling |
| `.gitignore` | any new standalone binary |
| `tools/win/check-sources.sh` | nothing for a normal module; a standalone binary is excluded by name. It also refuses a nested path or a repeated basename |

**Two constraints the guard mechanism imposes, and the guard now enforces both.**
`check-sources.sh` compares BASENAMES, through a pattern that matches exactly one directory
level, so a file at `net/sub/deep.c` used to match neither list: the comparison found nothing
to disagree about, said the lists agreed, and the Windows link failed much later with an
undefined symbol in a build nobody had touched. And object names are basenames with no
directory, so `net/mixer.c` would clobber `audio/mixer.c`'s object while `sort -u` collapsed
the pair into one entry that matched on both sides. Both are now separate refusals with the
offending name printed, so **source directories stay flat and basenames stay globally unique**
by refusal rather than by memory. Each arm was proven by adding the offending file and
watching the guard refuse it.

**Tier 1 cannot compile `net/`, and that is now declared rather than accidental.** The Win98
build names its sources explicitly and has no wildcard that could sweep `net/` in, and its
`-D_WIN32_WINNT=0x0400` leaves `getaddrinfo` undeclared, which `net_udp.c` calls. That is a
load bearing accident, so `net_udp.c` now carries an `#error` under `WIN98` that names the rule.

---

## 7. What this map does not settle

- Whether the sell gesture becoming a toggle is wanted. It restores 1995 and it is the one
  change in surface 1 a player will notice.
- Whether the REPAIR arm's missing ownership test should be added as part of routing repair
  through the queue, or as its own commit first. It has to happen either way: the arm has no
  check and the direct path it replaces does.
- Whether the `PlayerPtr` reads in `house.cpp` and `trigger.cpp` are closed. About forty of
  them were read quickly rather than traced; most are correct by construction because
  `HouseClass::AI` rotates `PlayerPtr` per house, but that surface is not closed and should not
  be described as such.
- Whether the per tick object dump or the engine's own CRC is the right oracle for proving any
  of this. The design argues for the dump and the comparison tool was not exercised against it.
