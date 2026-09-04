# Multiplayer: how CNC3D gets real network play, and what 16-24 players costs

Investigation and design, 30 Aug 2026. Every claim about the engine is read out of the
checkout in `brain/vanilla` and `brain/xl` (`brain/patches/UPSTREAM.txt`), and line numbers
are that checkout's. Where a number is given it was measured or compiled on the day, not
estimated. Where something is unproven this document says so rather than rounding it up.

Companion documents: `docs/design-xl-brain.md` is the Enhanced engine contract and this
document depends on it; `docs/design-skirmish.md` and `docs/design-skirmish-lobby.md` are
the lobby and match setup this one extends.

> **STATUS: DESIGN, NOTHING BUILT.** No netcode exists. The one experiment that decides
> whether the recommended architecture is possible at all has not been run. It is Phase 0
> in section 10 and it needs no sockets.

---

## 0. The short version

**Deterministic lockstep on `EventClass` orders, through a star, on the XL brain.** Every
machine runs its own brain and only the orders cross the wire. All traffic goes through one
endpoint, the host's machine or a relay, so connecting is N links rather than N squared.

Five findings drive the whole design:

1. **The engine was converted away from peer to peer, and the conversion is what we drive.**
   EA's own comment on `DLLExportClass` says it holds "additional game state required by the
   conversion from peer/peer to client/server" (`dllinterface.cpp:222`). The `CNC_*` ABI is
   the client/server side of that. The peer to peer machinery it replaced is still in the
   source tree and is **not in the shipped binary**: every build script passes
   `-DNETWORKING=OFF`, and four independent CMake paths exclude it besides.
2. **Lockstep is not close to a fair fight on cost.** The wire unit is 22 bytes, compiled
   and confirmed. Orders cost roughly 10 KB/s per client at 24 players. Streaming game state
   to thin clients instead costs tens of MB/s per client, and cannot be done at all without
   a new serialiser, because the ABI's object record begins with a raw `void*` and
   `Get_Layer_State` discards its `player_id` (`dllinterface.cpp:3427`) and hands every
   caller the whole world.
3. **The simulation is cheap and the XL brain has already proved it.** The XL brain runs
   20,000 ticks in 133 seconds, 150 ticks per second against the 15 Hz the game needs, on a
   1024x1024 map. Eight houses with 336 units cost about 10.5 seconds per 1500 ticks. In
   lockstep every machine simulates every army, so that ten times headroom is the number
   that makes 24 players arguable at all.
4. **16 players is not a networking question. It is the `ID:4` wire field.** `EventClass`
   carries the originating house in four bits (`event.h:103`), and this project's own
   `static_assert` pins it (`defines.h:2874-2877`): the highest house index must stay inside
   0..15, and `HOUSE_MULTI8` is 11. The classic wire cannot name a sixteenth house. **XL has
   not done the respin either**: `brain/xl/tiberiandawn/event.h` still reads `Frame:27 /
   ID:4 / IsExecuted:1`, and `brain/xl/DIVERGENCE.md:167` says of houses, "nothing yet --
   Phase 3 territory".
5. **The precondition has never been tested.** Lockstep requires bit identical simulation,
   and rule 4 means every real match pairs a macOS arm64 build against a 32 bit Windows
   build of the same commit. Every determinism gate in the repo runs one binary twice on one
   machine. Nobody has ever compared two architectures. That test needs no netcode and is
   Phase 0.

**On the four questions asked:** plain UDP with order redundancy; a small rendezvous
service is unavoidable for connecting but no game server is; yes to create-and-join lobbies;
and 16-24 players is feasible on the maths but lands squarely on XL Phase 3 plus a design
that never hard stalls.

---

## 1. What the brain actually gives us

### 1.1 One simulation, all houses, already

`CNC_Advance_Instance(player_id)` advances the **whole world once**: `Logic.AI()` runs once
(`dllinterface.cpp:1662`) and `Frame++` happens once (`:1750`). The `player_id` only selects
a context for the call's duration. `Set_Player_Context` swaps `PlayerPtr`, `MPlayerLocalID`,
`CurrentLocalPlayerIndex` and the selection context together (`:6189-6225`), and
`HouseClass::AI` already rotates `PlayerPtr` per house under `REMASTER_BUILD`
(`house.cpp:927-934`).

`EventClass::Execute` dispatches through `Houses.Raw_Ptr(ID)` rather than the local
`PlayerPtr`, so **an order created on any machine executes identically on every machine**.
"Simulate every house, accept input for one" is not a convention we impose. It is what the
code does today, and it is exactly lockstep's shape.

### 1.2 What the GlyphX event pump does not do

`DLLExportClass::Glyphx_Queue_AI` (`dllinterface.cpp:6072-6152`) drains `OutList` into
`DoList` and executes everything whose frame has arrived, **immediately, in arrival order**.
Its outer loop over `MPlayerCount` is vestigial: the inner test at `:6127` has no
`MPlayerID` match, so the first human house's pass marks every eligible event executed and
the remaining iterations do nothing.

That is correct for one authoritative simulation and meaningless for lockstep, where
ordering must be identical everywhere. The algorithm that does it correctly is
`Execute_DoList`'s outer loop over `MPlayerID` (`queue.cpp:2821-2860`), which **is compiled
into the shipped library and is currently unreachable**, because `Queue_AI`'s switch
(`queue.cpp:319-350`) has no `GAME_GLYPHX_MULTIPLAYER` case.

### 1.3 The wire unit is 22 bytes

Compiled and confirmed rather than read: with `#pragma pack(push,1)` (`event.h:44`) and
`BITFIELD_STRUCT` (`common/bitfields.h:19-23`, `__attribute__((ms_struct))`, applied
unconditionally on GCC and clang):

```
sizeof(SpecialClass) = 16
sizeof(EventClass)   = 22
offsetof(Data)       = 6
```

`EventClass` contains no pointers, no `long` and no `size_t`, so it is byte identical
between the 64 bit Mac build and the 32 bit Windows build. That is the property the whole
design rests on, and it is the reason the wire can be the struct.

**Never send raw `sizeof` anyway.** Send the compressed per type form, `EventLength[type]`
plus a tag byte, which is 5 to 14 bytes per event, and run length compress MEGAMISSION runs
the way `Add_Compressed_Events` does: a 50 unit move order goes from about 1,100 bytes to
about 217.

### 1.4 The 1995 stack is a trap, and four things in it are worth stealing

It is absent from the shipped binary four independent ways: `ipxconn`/`ipxgconn`/`ipxmgr`/
`netdlg` live in `TIBDAWN_NET_SRC`, consumed only by the `VanillaTD` executable we never
build; `NETWORKING` is appended to `VANILLA_DEFS` while the shared library uses
`REMASTER_DEFS`; `connect`/`ipxaddr`/`wspudp` go into `commonv` while the library links
`commonr`; and every build script passes `-DNETWORKING=OFF`
(`tools/mac/build-brain-mac.sh:46`, `tools/win/build-win.sh:195`,
`tools/win98/build-brain.sh:48`, `tools/xl-parity/run-xl-gates.sh:73`).

Reviving it would mean editing the vendored engine's CMake, which collides head on with the
byte for byte upstream-plus-patch guard. And it would not be worth having: its address type
is four bytes of IPv4 with **no port** (every `sendto` uses one global
`PlanetWestwoodPortNumber`), so nothing behind NAT is addressable; `CONNECT_MAX` is 7; and
`Wait_For_Players` calls `Map.Input`, `Map.Render` and `Keyboard->Check` from inside the
DLL, so it owns the frame loop that `game/cnc_eyes.cpp` owns.

Worth taking, by copying rather than by enabling:

- `Execute_DoList`'s per `MPlayerID` ordering (`queue.cpp:2821-2860`).
- `Add_Compressed_Events`' MEGAMISSION run length encoding.
- The `GlobalPacketType.ScenarioInfo` pre-game agreement checklist. Note it already carries
  a `Seed` field and the 1995 lobby exchanged it (`netdlg.cpp:2474`, `:3672`).
- `Compute_Game_CRC` as a desync alarm, with the caveat in section 9.1.

---

## 2. Why lockstep, and not the architecture the ABI was built for

| | Lockstep (A) | Host authoritative streaming (B) |
|---|---|---|
| Wire per client at 24 players | about 10 KB/s | tens of MB/s |
| CPU per machine | every machine simulates everything | only the host simulates |
| Can desync | yes, and it is fatal | no, structurally |
| Host disconnect | any peer can continue | match is over |
| Replays and spectators | free, the order log is the replay | needs separate work |
| Weakest machine | sets the pace for everyone | only renders |

**B is not merely expensive, it is unbuildable without a new serialiser.** The ABI's object
record begins with a raw `void* CNCInternalObjectPointer`; it is a host ABI struct, not a
wire format. `Get_Layer_State` discards `player_id` on its first line
(`dllinterface.cpp:3427`) and builds output by calling `object->Draw_It(...)` on every
object (`:3490`), so a streaming host would run the 1995 draw path once per tick while also
rendering its own game, and would ship every object to every client anyway, giving away
exactly the same information lockstep does.

**The honest case for B**, which must survive into the design: lockstep's precondition is
bit identical simulation across an arm64 clang build and a 32 bit MinGW build, forever, and
that has never been tested. B cannot desync. That argument loses only because the test is
one session's work and the bandwidth gap is three orders of magnitude. If Phase 0 fails, we
reconsider having spent one session rather than one quarter.

**One claim to strike:** the "18 to 79 KB per tick" dump figure in `cnc_eyes.cpp:2744` is
the brain's human readable debug **text**, and the 45 ms attached to it was a Windows only
`WriteFile` per character pathology since fixed. It is an upper bound on a naive encoding,
not a floor for streaming. The real argument against B is the struct sizes and the discarded
`player_id`.

---

## 3. What must be fixed before any netcode is written

These are not networking work. They are correctness bugs that lockstep would expose, and
several are live defects today.

### 3.1 The four input paths that bypass the event queue

Eleven paths mutate the simulation without producing an event. Four are reachable in CNC3D
today, and each would change the world on one machine and nowhere else:

| Path | Brain | Reached from |
|---|---|---|
| Repair | `dllinterface.cpp:6554` | `cnc_eyes.cpp:10006` |
| Sell | `dllinterface.cpp:6612` | `cnc_eyes.cpp:10007` |
| Wall sell | `dllinterface.cpp:3746` | `cnc_eyes.cpp:10030` |
| Building placement | `dllinterface.cpp:5267` | `game/cnc_sidebar.h:1082` |

The events all already exist and already execute correctly (`event.cpp:437`, `:452`,
`:459`, `:497`). Routing these four back through them is the fix. Placement is the only one
with a real design cost, because the sidebar reads `Place_Object`'s return code and would
have to stop expecting an answer in the same call.

A twelfth problem is structural rather than a hole: `MP_Construction_Action` calls
`Glyphx_Queue_AI` **reentrantly** at `:5116` to get immediate sidebar feedback, so a PRODUCE
event executes at input time rather than on a frame boundary.

### 3.2 Calculated_Cell reads the local camera and draws from the shared RNG

This is the single highest value fix in the whole investigation.
`DisplayClass::Calculated_Cell` has two separate mechanisms:

- `SOURCE_AIR` falls back to `cell = Coord_Cell(TacticalCoord)`, the local camera, when no
  reinforcement waypoint exists (`display.cpp:2901-2903`).
- `SOURCE_VISIBLE` derives its cell from `TacticalCoord` **and calls `Random_Pick` twice**
  (`display.cpp:2922-2923`).

The second is the dangerous one. Even where the resulting cell does not matter, those two
draws advance the shared random stream, so two peers diverge permanently from that tick
onward.

**CNC3D's current exposure is lower than the general case and must not be relied on.** The
renderer deliberately never moves `TacticalCoord` (`cnc_eyes.cpp:308`), which is set only at
scenario start (`dllinterface.cpp:1354-1357`) and restored after the start position sweep
(`:6390`). So it is currently identical on every machine by accident of the host's design,
not by contract. Under lockstep it needs to be a contract.

### 3.3 The random seed is frozen, and that is two findings at once

`Init_Random` is called from exactly one place, `Select_Game` (`init.cpp:1354`), and
`Select_Game` returns early when `RunningAsDLL` (`init.cpp:855-857`). Both streams are
seeded only inside it (`init.cpp:2547-2548`). `Seed = timeGetTime()` at
`dllinterface.cpp:1339` and `:1417` is therefore a **dead store**.

So `RandNumb` keeps its static `0x12349876` (`common/irandom.cpp:37`) and
`Scen.RandomNumber` keeps `RandomClass(0)`. Consequences:

1. **Good for lockstep.** No seed exchange is needed to bootstrap, and it is why the
   existing two run gates pass.
2. **A live gameplay defect nobody has logged.** Every skirmish match runs from an identical
   seed. Registered as a gap in section 12.
3. **A trap waiting for the lobby.** `GlyphX_Assign_Houses` does `srand(timeGetTime())`
   (`dllinterface.cpp:924`) and a Fisher-Yates shuffle with `rand()` (`:960`), guarded by
   `num_random_start_locations > 1`. It is unreached only because `arm_skirmish` always
   writes an explicit `StartLocationIndex`. **The moment a lobby offers "random start
   position", players spawn differently on different machines before tick 0.**

### 3.4 The tick rate must stop being a local slider

`cnc_eyes.cpp:23481` takes the tick interval from a per machine game speed setting, and
`:23525` silently drops ticks when the accumulator debt exceeds one second. Under lockstep
the rate is a match property agreed in the lobby, and dropped ticks are a desync.

### 3.5 Two smaller ones

- The beacon and animation expiry at `event.cpp:481-491` and `anim.cpp:788-801` is wall
  clock and Windows only, so the Mac and Windows builds already disagree. It becomes a frame
  count.
- Selection leaks into the shared simulation exactly once, through the spillage radius at
  `infantry.cpp:1812`. It shipped in 1995. Either drop the selection term or replicate
  selection.

---

## 4. The wire

**Transport: plain UDP.** The traffic is tiny and must all arrive, so TCP's head of line
blocking buys nothing and its retransmit timing costs latency. QUIC and WebRTC both mandate
encryption, which drags in a TLS stack that Windows 98 cannot host and that a 22 byte order
does not need.

**Reliability by redundancy, not retransmit.** Every packet carries the last N turns' orders.
A lost packet is covered by the next one, so there is no retransmit path, no ack timer and no
head of line stall. This is the standard lockstep trick and it is why the bandwidth stays
trivial.

**Turn scheduling.** Orders are stamped for execution `MaxAhead` turns in the future. The
1995 defaults are `MPlayerMaxAhead = 3` and `FrameSendRate = 3` (`globals.cpp:539`,
`session.h:106`), floored by `Generate_Real_Timing_Event` at `FrameSendRate * 3 = 9` frames,
which is 600 ms at 15 Hz. That is a lot of felt input lag and it is adaptive for a reason:
at 24 players the turn must float to the slowest peer rather than stalling on it.

**Licences, all verified GPL v3 compatible:** ENet is MIT; libjuice (ICE, STUN, TURN client,
dual stack, no dependencies) is MPL 2.0 and compatible under its section 3.3; miniupnpc and
libnatpmp are BSD 3 clause; `cncnet-server`, a working relay for this exact game family, is
GPL 3.0. **Steam Datagram Relay and Epic Online Services are both unavailable to us**: each
requires linking a proprietary SDK into a binary that `dlopen`s EA's GPL v3 brain. That
constrains which relay we can borrow, not which transport we can write.

Note a gap the research left open and the implementation must close: ENet has no hole
punching and is IPv4 only, and libjuice is UDP only. Nothing in that pair serves a TCP-on-443
fallback for UDP blocked networks. Either accept that rung is missing or write it.

---

## 5. Connecting players without running game servers

**The honest answer: no game server, but not no server.** Two players behind home routers
cannot find each other unaided; something must tell each its own public address and
coordinate the moment they both start sending. Four things get conflated and only the first
is what "dedicated servers" means:

| | What it does | Do we need one |
|---|---|---|
| Game server | runs the simulation | **No.** Peers simulate. |
| Lobby list | advertises open games | Only for a public list |
| Rendezvous | address exchange, seconds per match | **Yes**, for internet play |
| Relay | carries match traffic throughout | For the minority who cannot punch through |

**The connection ladder**, best first:

1. **LAN broadcast.** Zero infrastructure, works today, and it is what the 1995 game did
   (`NET_QUERY_GAME` broadcast answered by `NET_ANSWER_GAME`, `netdlg.cpp:285-296`).
2. **Direct IP with a forwarded port.** The escape hatch that always works for the host who
   can do it.
3. **UPnP or NAT-PMP**, which asks the router to forward automatically.
4. **UDP hole punching through the rendezvous.** The largest public measurement puts per
   link success at about 70 percent.
5. **Relay** for the rest, including carrier grade NAT on both ends, where no direct answer
   exists at any effort.

**Why a star and not a mesh.** A full mesh needs N(N-1)/2 independent traversals: 28 links
at 8 players, 120 at 16, **276 at 24**. At even an optimistic 85 percent per link, the odds
that a 24 player mesh is all direct are effectively zero. A star needs 23 links, and a star
through a relay needs none. This is decided on connectability, not latency: the research's
original latency argument for the star was computed round trip on one side and one way on
the other, and once corrected the star's advantage at 24 players is about 28 ms, under half
a tick. The star still wins, for the other reason.

**Cost.** Because we relay 22 byte orders rather than video, a fully relayed 45 minute
24 player match is on the order of a few hundred megabytes. CnCNet, the reference project for
this game family, runs its entire public infrastructure on donations plus community hosted
tunnels. Publishing the endpoint list so others can host is what keeps one outage from
being the end of multiplayer.

**Ship a game scoped tunnel, never a generic open TURN server**, which is an open proxy with
a CVE history.

---

## 6. The lobby

The lobby already exists and the precedent for the network version is in the engine's own
source. `Net_New_Dialog` (`netdlg.cpp:2654`) is Tiberian Dawn's network host lobby: a
`ColorListClass` roster at the top left, one row per player in that player's colour,
formatted name-tab-side (`:3063-3070`). That is the widget CNC3D's skirmish lobby was already
modelled on, per `docs/design-skirmish-lobby.md` section 0.

What the multiplayer version adds over the skirmish one: a network roster with join and
leave, ready flags, a ping column, chat, and the host's authority over map and options. What
it must agree before tick 0 is the `ScenarioInfo` checklist the 1995 lobby already had:
scenario and its CRC, seed, house and colour assignment, start positions, credits, tech
level, the toggles, **and the tick rate** per section 3.4.

The scenario CRC check matters more than it looks. It is what makes a version mismatch a
polite refusal at the door instead of a desync ten minutes in.

---

## 7. Is 16-24 players feasible

### 8 players: yes, no engine work
`EIGHTPLAYERS` is on by default (`brain/vanilla/tiberiandawn/CMakeLists.txt:183`),
`MAX_PLAYERS` is 8, and the host already builds the full eight seat roster in `arm_skirmish`
(`cnc_eyes.cpp:15610-15736`). Lockstep bandwidth at 8 is a few KB/s per client. The work is
netcode and lobby, plus everything in section 3.

### 16 players: the engine, not the network
Lockstep at 16 is around 1.5 Mbit/s of host upstream and about one tick more latency than 8.
The wall is `ID:4`, pinned by `defines.h:2874-2877`, so 16 house indices total including the
four campaign houses, which is 12 multiplayer slots at absolute best. Behind it sit
`ObjectClass::IsSelectedMask` at exactly 16 bits (`object.h:99`), `MAX_HOUSES 32` in the ABI,
the `MPlayerID` colour nybble, and `PlacementDistance[MAX_PLAYERS][MAP_CELL_TOTAL]`
(`dllinterface.cpp:416`), which at `MAX_PLAYERS 64` and the XL 1024 stride becomes about
67 MB of static storage and is **not** in the XL design's conversion list.

This is XL Phase 3, and the `EventClass` respin to `Frame:24 / ID:7 / IsExecuted:1` that
Phase 3 specifies is precisely what widens the wire from 12 houses to 128, in the same 32
bits, still giving 12.9 days of frames at 15 Hz.

### 24 players: the same engine build as 16, plus never stalling
`MAX_PLAYERS` goes to 64 in that same phase and `ID:7` covers 128 houses, so 24 costs no
extra engine work beyond 16. Bandwidth is around 3.4 Mbit/s of host upstream. On CPU the
measured XL headroom is the encouraging number: 150 ticks per second against 15 needed.

What has to be true is entirely about resilience:

- **The rate floats to the slowest peer rather than stalling.** The 1995 engine already half
  does this by setting `DesiredFrameRate` from the slowest player (`queue.cpp:1176-1197`).
- **Drop is fast and deterministic.** 1995 waits 25 seconds on LAN and 300 on the internet.
  At 24 players, 300 seconds means 23 people sit still for five minutes. Start at 30.
- **`DoList` overflow becomes loud.** It holds 2,048 and `Glyphx_Queue_AI` discards overflow
  with an empty statement (`dllinterface.cpp:6079-6081`). One order over a large selection
  queues one event per object.

**The unmeasured number that actually decides it** is not the simulation, it is the
renderer: nobody has measured 15 Hz on a 256x256 map with 16 or 24 armies visible, and under
lockstep the whole match runs at the slowest machine's rate.

**And one cheap question worth asking before any of this:** 24 humans and 24 seats mostly
filled by AI are different products. The second needs no networking at all and is available
as soon as XL Phase 3 lands.

---

## 8. The Tier 1 answer

Per rule 1 this must be declared before anything ships. **Multiplayer is Tier 2 only.**

> **See `docs/tier1-freeze.md`.** Decision 9 (Enhanced becomes the shipping brain) made this
> section load bearing, so it was checked properly for the first time on 1 Sep 2026 and that
> file is the dated record of what Tier 1 has at the freeze. Two of its findings correct this
> section rather than supporting it. Win98 stopped following the main line on **26 Aug**, when
> the terrain pack format moved and `tools/win98/mkterrain.py` did not follow, which is five
> days before any of this was decided. And **"Enhanced never ships to Win98" is unbuilt and
> undecided rather than impossible**: `brain/xl/common` is byte identical to
> `brain/vanilla/common`, the `WIN9X` switch is intact in the fork, `MAX_HOUSES` is still 32,
> and nobody has ever tried the build. One cmake line would settle it. Also recorded there: a
> `docs/tier1-gap.md` row for the Enhanced brain is now OWED and does not exist.

The blocker is not the one you would guess. Windows 98 SE ships Winsock 2.2, plain UDP works,
hole punching needs no encryption, and the box already runs the brain. The real cost is that
**Tier 1 is a separate program**: `tools/win98/build.sh` builds `w98glide.exe` from
`tier1/*.c` and never compiles `game/cnc_eyes.cpp`. Multiplayer on Tier 1 means writing the
lobby, the turn scheduler and the ack logic twice, in two unrelated codebases.

Two further reasons, either sufficient on its own:

- **Lockstep runs at the slowest peer.** A Pentium class machine simulating 24 players'
  armies drags every other player below 15 Hz.
- **The two brain law.** 16-24 players requires the XL brain, which by `docs/design-xl-brain.md`
  is Enhanced only and never ships to Win98. Large matches are inherently Tier 2.

The menu already tells the truth: `menu/dosmenu.c:62` marks Multiplayer disabled and
`tier1/t1_menu.c:103` refuses the click.

**A LAN and direct-IP-only Tier 1 build stays possible** and is genuinely the 1995
experience, at a cost of a session or two once the transport seam exists. It is not planned
and is not promised. The `docs/tier1-gap.md` entry is in section 12.

---

## 9. Desync, dropping, and cheating

### 9.1 Detection
`Compute_Game_CRC` (`queue.cpp`) is **a canary, not a state hash**. It covers coordinates,
facings, layer membership and the RNG seed only, with no strength, mission, credits, power
or production, and `Add_CRC` accumulates a *sum* of coordinate and facing per object, so
equal and opposite drifts cancel. Worth exporting as a live alarm because it is nearly free
and already written. The **oracle** for gates is the existing per tick `OBJ|`/`HOUSE|` dump,
which is strictly stronger, and `tools/xl-parity/compare.py` already diffs two captures and
names the first divergent tick and field.

Never hash save files: raw vptrs under ASLR make save bytes irreproducible, which the gap
register already carries.

### 9.2 Dropping
**AI takeover is not free, contrary to the obvious reading.**
`#define KILL_PLAYER_ON_DISCONNECT 1` sits unconditionally at `dllinterface.cpp:102`, so the
compiled branch of `CNC_Handle_Player_Switch_To_AI` is `PlayerPtr->Flag_To_Die()`
(`:1902-1913`). The takeover arm that sets `WasHuman` and hands the house to the AI is the
`#else` and is compiled out (`:1915`). Getting it back is a brain patch with a behavioural
change, and the switch must run from an agreed event on an agreed tick on every machine.
Calling it locally desyncs instantly.

### 9.3 Cheating
**Maphack is unavoidable in lockstep and should be stated publicly rather than fought.**
Every machine holds the whole world because it is simulating it. In this build it does not
even need a hack: `--noshroud` and `--noobjshroud` are shipping switches. Every 1995 era RTS
had this property.

The one cheat worth engineering against is **order injection**, because `EventClass::Execute`
never checks that the issuing house owns the subject. A deterministic ownership filter
running identically on every peer closes it cheaply. Everything else is theatre.

Related and needing a decision now: the cheat menu has no multiplayer guard, `cheat_tick`
has five call sites, and `g_cheatsArmed` has two setters including a script switch at
`cnc_eyes.cpp:19093`, so blocking the dialog alone is not a sufficient guard.

---

## 10. Phases

**The implementation map is a separate file.** `docs/design-multiplayer-integration.md` says
where each phase below actually lands, by file and line, and what each change costs. It exists
because this document's own line references had already drifted by the time the work started.
Read it before implementing any phase; read this one for why the shape is what it is.

**Where this stands, 1 Sep 2026.** Phase 3's first half is built and the rest of the ladder
is not, which is out of order on purpose. The transport seam and the scheduler are the one
part of the ladder whose oracle is itself: peers agreeing is checkable with no game at all.
So `net/lockstep.c`, `net/net_udp.c`, two gates and the `netcheck` tool exist and are tested.

**The environment excuse is gone.** The sessions that wrote the ladder ran in a Linux
container with no macOS, no Windows and no retail content, so Phases 0 to 2 could be written
but not believed. That is no longer true, and two things followed from running it on a real
Mac:

- The scheduler and the transport were exercised on real hardware for the first time.
  `gate_lockstep` 25 legs, `gate_netloop` 16 legs over real loopback datagrams, and the
  shipped `netcheck selftest` at `ORDER DIGEST E2668540`, which is the digest this
  document's own handoff predicts. The wire is sound on this machine.
- **Phase 0's oracle already existed and nobody had noticed.** `CNC3D_Dump_Objects`
  (`brain/vanilla/tiberiandawn/dllinterface.cpp:8338`) walks the real object heaps rather
  than the draw intercept, so it works with no art, no window and no renderer, and
  `tools/xl-parity/compare.py` already parses exactly what it prints. It was never wired
  into `brain/host/cnc_host.c`, which resolved ten `CNC_*` symbols and never asked for it.
  It is wired in now, behind `CNC3D_TICKDUMP`, and the Mac half of Phase 0 has been
  captured: 20,000 ticks of SCB01EA, run twice, identical at every tick.

**One earlier recipe would have lied.** Comparing the host's END STATE rather than a per
tick dump looks cheaper and proves nothing: `GAME_STATE_LAYERS` returns FALSE with `Count=0`
in single player and the sidebar reads all zeros, so each end would have compared an empty
dump against an identical empty dump and agreed. The per tick object dump is the oracle, not the state
readout.

What that half does not include, and what Phase 3's gate still asks for: the scenario CRC
handshake, the desync alarm, and any contact between the scheduler and the engine.

**THE LADDER BELOW WAS REWRITTEN ON 1 SEP 2026** for the director's decision that the
promise is SIXTEEN players and that the lobby is BOTH public and friends only (section 11,
decisions 3 and 8). Three things moved. The `EventClass` respin came FORWARD out of the old
Phase 5 into Phase 1, because it is free today and a flag day later. The sixteen house
engine work came forward AHEAD of the lobby, because the lobby's roster plate, colour picker
and score screen are all sized by the roster and building them twice is paying for the same
art twice. The resilience work came forward into the lobby phase, because a public list is
exactly where a stranger drops mid match. The old Phase 6 is deleted.

**Phase 0. The cross architecture determinism gate. No sockets, no brain patch.**
This is the one test that can kill the recommendation, and it is the cheapest thing here.
`brain/host/cnc_host.c` is dependency free and already builds on macOS, on the 32 bit Windows
build and on the Win98 box. Build it from one commit on macOS arm64 and on i686 MinGW, run
the same scripted skirmish for 20,000 ticks with `CNC3D_TICKDUMP=1`, and compare with
`tools/xl-parity/compare.py`.
**GATE:** the per tick digests are byte identical at all 20,000 ticks on both platforms.
**FAIL** prints the first divergent tick and field, which is bisectable rather than a mystery.
**PASSES ON THE ENGINE STATE, 3 Sep 2026, pending one rehash on the Mac.** 20,000 ticks
of SCB01EA hash identically on macOS arm64 and 32 bit Windows once line endings and the
host's own chatter are kept out of the hash; `tools/phase0/README.md` records all three
the whole story and the one command that closes it. The paragraph below is the state
before that and is kept for its preconditions, which still hold.
**HALF DONE.** The Mac reference is captured and self identical over two runs. What remains
is the Windows half, and one piece of missing wiring: there is no build rule for the headless
host on modern Windows, only the Win98 one at `tools/win98/build.sh:137-138`. Three
preconditions: pin the rules on both sides (the renderer sends 1.0f for all seven difficulty
biases while `cnc_host.c:1083-1085` sends distinct rows, so "same commit" does not currently
imply "same rules"); no seed export is needed per section 3.3; and run a SINGLE PLAYER
scenario until the wall clock start position shuffle is fixed, or each end deals itself a
different army and the comparison proves nothing. Run the same brain under
`-fsanitize=undefined` alongside.
**SIZE: half a session, the Mac half being done.**

**Phase 1. Close the holes, and respin the wire while it is still free.** Section 3 in full:
the four input paths back onto their events, the reentrant pump call, `Calculated_Cell`, the
frame count expiry, the spillage selection leak, the tick rate as a match property. Plus
three things pulled forward by the sixteen player decision:

- **The `EventClass` respin, in BOTH brains, in one commit.** `Frame:27 / ID:4 / IsExecuted:1`
  becomes `Frame:24 / ID:7 / IsExecuted:1`. It is free TODAY and it is a flag day for every
  player once Phase 3 ships a working wire. It stays in one 32 bit word, so the event stays
  22 bytes with its payload at offset 6 and nothing in `net/` moves. Nothing reads those bits
  by position, `NETWORKING` is OFF in both brains so every wire path is compiled out, and no
  file on disk carries the structure. **Westwood already did this exact respin**:
  `brain/vanilla/redalert/event.h:113-118` is the same class at `Frame:26 / ID:5`, sitting in
  this repository as a worked reference. Add a `static_assert` on `sizeof(EventClass)` in the
  same commit, because the 22 bytes are currently reasoned rather than enforced.
  **Take seven bits, not the five sixteen players strictly need.** Seven shortens the longest
  possible single continuous match from about 104 days to about 13, which is absurd either
  way, and it means never doing this again.
- **The remap table overrun.** `DisplayClass::One_Time` (`display.cpp:246-247`) writes 112
  bytes past the end of `RemapTables`, declared `[HOUSE_COUNT][3][256]` at `display.h:111`,
  on its last loop iteration. In BOTH brains, in the shipping game, at every startup. It is
  stock 1995 arithmetic sized for six houses, and the overrun grows with the roster, so it
  has to be dead before Phase 4 adds eight more.
- **The house mask guard, which does not work.** `tools/xl-house-mask-guard.sh` prints
  "clean" and exits 0 over a tree containing roughly sixteen bare `1 <<` shifts against a
  house index, because its pattern only matches a bare identifier ending in "house" while
  the fork's habit is `int shift = (int)house;`. Repair the guard before Phase 4 adds code it
  is supposed to be watching, and widen `brain/xl/tiberiandawn/techno.h:210`, the one house
  mask the 30 Aug widening missed.

**STATUS 3 Sep 2026:** section 3's four input paths, the reentrant pump, both
`Calculated_Cell` arms, the frame expiry, the spillage leak and the wire respin are done
and gated, in both brains; G138's SCM01EA leg proves each gesture crosses (handoff section
0f). The tick rate as a match property landed with Phase 3's first milestone the same day.
NOT done: the remap table overrun, still registered; the house mask guard, repaired on
`main` on 31 Aug.
**GATE:** existing suite green, plus a new gate asserting that a scripted session's every
world mutation is preceded by an event. **SIZE: 2-3 sessions.**

**Phase 2. Two brains, one machine, in lockstep.** No sockets yet. Run two brain instances in
one process from the same seed, feed both the same order stream through a new
`CNC3D_Post_Event` export, and compare dumps per tick.
**GATE:** 20,000 ticks, dumps identical, and an order issued to instance A appears in
instance B's world on the agreed tick. **SIZE: 1-2 sessions.**

**Phase 3. The transport seam, and two peers on a LAN.** New files under `net/`, transport
behind an interface exactly as the renderer backends are, so a different Tier 1 transport
stays possible. UDP, order redundancy, `MaxAhead` scheduling, `Execute_DoList` ordering,
scenario CRC handshake, desync alarm. Eight seats, on a wire that is already sixteen ready,
so it carries no debt forward. Wire version field from the first commit.
**STATUS 3 Sep 2026: the first milestone is in** (handoff section 0g). Two copies of the
game, `--host` and `--join`, arm one two-human skirmish from a handshake carrying the host's
setup and play it in lockstep with the wire layout checked, a keepalive resend, a desync
alarm on the world hash every fifteen frames, and gate G139 over loopback. Not yet: eight
players, the map bytes in the handshake, a peer dropping, real latency, any lobby.
**GATE:** two peers, 8 players, a full match to a verdict, no desync. **SIZE: 3-4.**

**Phase 4. The sixteen house engine.** *(This was the second half of the old Phase 5, and it
now comes BEFORE the lobby.)* Grow the roster `HOUSE_MULTI9` through `MULTI16` with their
`HOUSEF_*` macros, house records and the pointer table, and the roughly 104 hand written
ownership lists across `bdata.cpp`, `idata.cpp`, `udata.cpp`, where a missed list means seats
9 to 16 silently cannot build one thing and it surfaces as a player complaint rather than a
compiler error. Eight new player colours and their palette tables. `MAX_PLAYERS` 8 to 16 and
everything it drags. A sixteen row score screen, which is a pixel layout and not a constant.
`LS_MAX_SEATS` 8 to 16 at `net/lockstep.h:55`. The desync forensics loops in `queue.cpp`,
which still stop at `HOUSE_MULTI6`. Host side: the lobby roster plate, the eight wide loops
in `game/cnc_eyes.cpp` and `EDIT_MAX_PLAYERS`.
**Two things here are NOT needed and were being budgeted for.** The two deliberate 32 house
narrowings still hold at twenty houses and need no work, and the `MPlayerID` nybble survives
sixteen untouched, because its low nybble holds a preferred SIDE (GDI or Nod) and its high
nybble holds a colour index that fits sixteen exactly. Both are seventeen-plus problems.
**GATE:** a 16 way skirmish against the computer runs to a verdict, deterministic across
machines. **SIZE: 5-7 sessions**, which is three times the old estimate, mostly because that
estimate assumed the Enhanced fork's roster work was largely done and it has not started.

**Phase 5. The lobby and the connection ladder, public AND friends only, built at sixteen
rows from the first pixel.** *(Absorbs the resilience half of the old Phase 6.)* Rendezvous
service, hole punching, relay fallback, the network roster over the lobby, the public session
list with its liveness and filtering, plus fast and deterministic drop, AI takeover, `DoList`
overflow and order rate caps. The resilience work lands HERE rather than after, because a
public list is precisely where a stranger drops out mid match or floods the queue, and
shipping a public list without it is shipping a known failure.
**GATE:** two players on different networks connect with no port forwarding, and a 16 way
match survives one peer being killed. **SIZE: 4-6.**

**Phase 6 is deleted.** Its 24 player half is gone because 16 is the decision, and 24 was
never extra engine work over 16 anyway. Its resilience half moved into Phase 5.

Phases 0 to 3 deliver 8 player multiplayer and are worth shipping on their own. Phases 4 and
5 deliver the sixteen the director promised, and they run on the Enhanced brain.

---

## 11. Decisions for the director

1. **Do we run one small always-on service, permanently?** No game server, but connecting
   two players behind home routers needs a third party for a few seconds. It is tiny, and
   `cnc3dgame.com` already serves `/api/builds`. Say no and multiplayer means typing an IP
   and forwarding a port. **Recommendation: yes, and publish the endpoint list so it can be
   community hosted.**
2. **Do we pay for a relay for the players who cannot connect directly?** Roughly 15 to 30
   percent of connections fail direct. Because we forward orders and not video it is cents
   per match. **Recommendation: relay always on, never a user setting, starting free.**
3. **Is 8 the promise, or is 16 the headline? DECIDED 1 SEP 2026: SIXTEEN.** This overrules
   the recommendation that stood here, which was to promise 8. It is recorded rather than
   deleted because the cost it named is real and was accepted, not missed: 8 needs no engine
   work, and 16 needs the Enhanced ladder, whose last estimate ran roughly three times over.
   **What the decision actually buys and costs.** The wire respin moves into Phase 1 where it
   is free. The roster work moves ahead of the lobby, so the lobby is built at sixteen rows
   once instead of at eight rows twice. 24 players were never extra engine work over 16, so
   nothing was given up by dropping them. And multiplayer now ships on the **Enhanced brain**,
   which was previously optional and is now on the critical path: see decision 9.
4. **Does Windows 98 get multiplayer?** Section 8 says no, and rule 1 permits that if it is
   declared. A LAN only Tier 1 build stays possible later. **Recommendation: Tier 2 only,
   declared now.**
5. **Must Mac and Windows play each other on day one?** Players will assume yes, and rule 4
   already ships both. It makes cross architecture determinism a permanent release blocker.
   **Recommendation: require it, but run Phase 0 first so we know we are promising something
   true.**
6. **When somebody drops, does the computer take over or does the match end?** Takeover costs
   a brain patch that changes vanilla behaviour (section 9.2). **And decide the timeout:** 30
   seconds, not 1995's 300.
7. **Maphack is unavoidable. Do we say so?** **Recommendation: yes, plainly, in the release
   notes and the gap register.**
8. **Public lobby, or friends only invite codes? DECIDED 1 SEP 2026: BOTH.**
   **The engineering delta is the small half**, 1 to 2 sessions: a registry games announce
   themselves to, liveness so a crashed game leaves the list, filtering and sorting, and a
   join path for someone who was not invited. Transport, hole punching and relay are reused
   unchanged. **Friends only was never the cheap baseline it sounds like**: it still needs an
   identity system, a rendezvous service, hole punching and a relay, which is the bulk of the
   ladder either way. There is no lobby of any kind today; `menu/doslobby.c` is a local setup
   screen for a game against the computer.
   **The real cost is the half that never ends**, and it is accepted with the decision:
   moderation with someone who can remove a game and block a host, a name filter list that is
   maintained forever rather than written once, and the address exposure below.
   **Address exposure, in plain language.** Peer to peer lockstep means every machine talks
   directly to every other machine, which is what makes it cheap and low latency, and it means
   each player learns every other player's home address by construction. In a friends only
   room that is people who chose each other, roughly the risk of a voice call. On a public
   list it is any stranger who joins. There are exactly two answers: relay every match, which
   hides addresses and turns a one off engineering item into a monthly bill that scales with
   success, or say so publicly, the way this design already proposes saying openly that map
   hacking is unavoidable. **This one is still open** and is decision 10.

12. **Do the determinism fixes change the campaign, or only a match? DECIDED 1 SEP 2026:
    ONLY A MATCH.** The fixes in section 3 are gated on a lockstep flag. Outside a network
    match the engine takes the 1995 path exactly as it always has, and inside one it takes the
    deterministic path. The flag is `CNC3D_Lockstep`, false until the `CNC3D_Set_Lockstep`
    export is called, and nothing in the engine sets it.
    **Why this is the right shape and not a shortcut.** There is one engine, so a fix made for
    multiplayer otherwise lands in the campaign too, and the campaign is what ships today. The
    alternative was to prove every fix free against a 20,000 tick oracle, which works but pays
    a verification run for changes no player will ever see, and which eventually runs out: the
    sell gesture cannot be routed through the event queue without a second click cancelling a
    demolition where today it does nothing. Gating turns that from a decision the director has
    to make into a difference that only exists in a match.
    **The rule the flag comes with, because a switch like this attracts unrelated work.** It may
    gate ONLY a place where a peer would otherwise read state local to its own machine: the
    camera, the viewport, the local player, a wall clock, libc `rand()`, the local selection. It
    is not a general "Enhanced behaves differently" switch and must never carry a balance or
    content change. Where a fix is provably value identical, as the `SOURCE_AIR` arm turned out
    to be, it stays ungated: a branch that can never be observed is worse than no branch.

**Four decisions the sixteen player choice created. All four were taken on 1 Sep 2026, the
same day, so Phase 4 is unblocked.**

9. **Does the Enhanced fork become the game? DECIDED 1 SEP 2026: YES, ENHANCED IS THE GAME
   BY DEFAULT.** All the sixteen player groundwork is in `brain/xl`, and the build scripts
   ship `brain/vanilla`, so the alternative was repeating every widening inside the classic
   brain, which cannot even name a seventeenth house without widening two 16 bit fields
   first. Enhanced is promoted instead.
   **What this ends.** `brain/xl/DIVERGENCE.md` records a deliberate rule that every structure
   the outer program reads is kept byte identical between the two brains, so one build of the
   game can drive either. Sixteen seats ends that property. From the promotion onward the
   brain and the game ship together, and the classic brain becomes a reference rather than a
   target.
   **What this obliges.** The gate suite has to run against Enhanced rather than beside it,
   Enhanced needs the CI guard it has never had, and every determinism fix in Phase 1 lands in
   the fork that ships rather than the one that does not. That last point is a simplification,
   not a cost: the old plan had every fix landing twice.

10. **Relay everything, or tell players their address is visible? DECIDED 1 SEP 2026: RELAY
    EVERY INTERNET MATCH.** The direction given was "whatever is the most stable", and at
    sixteen players that is not a close call, for a reason the eight player version of this
    question did not have.
    **The arithmetic.** A star needs N-1 links to the host. At a generous 85 percent direct
    success per link, the odds that EVERY link in a match is direct are 32 percent at eight
    players and **8.7 percent at sixteen**. So at sixteen, nine matches in ten already contain
    at least one relayed link, and under lockstep every peer runs at the speed of the slowest
    link. The latency advantage of "direct where possible" mostly evaporates at exactly the
    player count that was chosen, while the failure modes of a mixed path (hole punch flake,
    NAT rebinding mid match, one peer silently worse than the rest) all remain.
    **So: relay is the path, not the fallback, for internet play.** Uniform and predictable
    latency for every peer, no hole punching to fail, and addresses hidden as a side effect
    rather than as a feature. **LAN stays direct**, because it is free, faster, and everyone
    on a LAN can see each other anyway.
    **The cost, stated plainly because it was accepted rather than avoided:** this is a
    monthly bill that scales with how well the game does, where the alternative was free. It
    is orders and not video, so it is cents per match rather than dollars, and section 5's
    guidance still holds: ship a game scoped tunnel, never a generic open TURN server, and
    publish the endpoint list so others can host and one outage is not the end of multiplayer.
    The disclosure in section 9.3 is still worth making for LAN and direct IP play.

11. **Sixteen player colours, and a sixteen spawn map. DECIDED 1 SEP 2026: BOTH, AND THE MAP
    IS WANTED FOR TESTING NOW.** Eight new colour schemes have to be invented that stay
    tellable apart as small dots on a minimap over a green and brown battlefield, and the
    existing eight were already hand derived because the automatic method Red Alert used could
    not be copied. That part remains an art judgement, and the candidates go in front of the
    director before the code is written.
    **The map is being authored AHEAD of the engine deliberately.** `MAX_PLAYERS` is 8 today,
    so a sixteen spawn map will only seat eight of its sixteen until Phase 4 lands. Building
    it now is worth it anyway: it is the test fixture Phase 4 is verified against, and what
    the engine does with the spare eight is itself a finding about what Phase 4 has to change.

---

## 12. What this design registered

All of these are written into the project's registers as part of the same change, rather
than left as an intention. Repeated here so this document stands on its own.

**The Tier 1 declaration**, per rule 1, which requires every feature to have a declared
Tier 1 answer before it ships:

| Feature | Tier 2 | Tier 1 fallback |
|---|---|---|
| Network multiplayer | lockstep over UDP with rendezvous, hole punching and relay fallback; 8 players on the classic brain, 16-24 on XL | **none, Tier 2 only.** Not a Winsock limit: Win98 SE has Winsock 2.2 and plain UDP is enough. Tier 1 is a separate program (`tools/win98/build.sh` builds from `tier1/*.c` and never compiles `game/cnc_eyes.cpp`), so parity means writing the lobby, turn scheduler and transport twice. Lockstep also runs at the slowest peer, and 16-24 players requires the XL brain, which never ships to Win98. A LAN and direct-IP-only Tier 1 build remains possible and is unplanned. The menu already refuses the click (`menu/dosmenu.c:62`, `tier1/t1_menu.c:103`) |

**Three gaps, all of them live in the shipping build and none of them about multiplayer.**
They were found while reading the brain for this design:

1. **Every match runs from the same random seed**, with the armed start-position shuffle
   behind it, per section 3.3. Skirmish matches are not randomised.
2. **Difficulty selection currently does nothing.** `game/cnc_eyes.cpp:16048-16057` fills all
   three difficulty rows with identical numbers, every bias `1.0f`, while
   `brain/host/cnc_host.c:1083-1085` sends distinct easy, normal and hard rows. So the
   control does nothing, and "same commit" does not imply "same rules".
3. **Maphack is inherent to lockstep multiplayer** and will not be defended against, per
   section 9.3.

---

## 13. What is still unproven

Said plainly, because rule 7 exists:

- **Cross architecture determinism has never been tested.** Every gate in the repo runs one
  binary twice on one machine. This is Phase 0 and the design depends on it entirely.
- **Nobody has counted the real order rate.** Three separate estimates were invented and each
  admitted as much. The brain emits one `EventClass` per selected object, so one box select
  move breaks all of them. The project has a self play gate and a per tick dump; count it.
- **Nobody has measured the renderer at 16 or 24 armies.** Under lockstep the slowest machine
  sets everyone's rate, so this is the number that actually decides 24 players.
- **The cost of un-doing player 0 in the renderer is unsized.** Every state fetch in
  `game/cnc_eyes.cpp` passes id 0 by design. In a 24,095 line file this is plausibly the
  largest unestimated block of work in this document.
- **Replays and spectators are asserted to be nearly free and are not designed.**
  `Queue_Record` and `Queue_Playback` are unreachable on the GlyphX path. There is no replay
  header, no format and no versioning.
