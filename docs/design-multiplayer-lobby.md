# The multiplayer lobby: hosting, browsing and joining

THE REQUIREMENT: click MULTIPLAYER in the main menu, create a named server with an optional
password, set the match up the way the Skirmish screen does, and have other people find it
in a browser with LAN and INTERNET filters, mark themselves READY, and be started by the
host. Modelled on the C&C Remastered Collection's flow.

## 0. What the Remastered Collection actually does, and how much of it we know

There is no Remastered Collection on this machine, so this was researched rather than
inspected. What the sources establish, and these are the load-bearing facts:

| Confirmed | Source |
|---|---|
| **LAN Play is its own MAIN MENU entry**, beside the online one, not buried inside it | patch 740600, and the cnc.community LAN guide |
| The online path is **"Skirmish and Online"**, which carries **tabs**, one of them **"Host"** | EA's own help pages |
| Hosting sets **the map, starting units and the other conditions** on that Host tab | EA help |
| There are two distinct multiplayer screens: a **listing view** (the games on offer) and a **game view** (the lobby you are in) | cnc.community's guide names both |
| **The host's START button is greyed out until every joining player clicks READY** | patch coverage, and it is exactly what was asked for |
| The **READY button sits bottom right** and shows its active state | patch notes |
| A row in the list shows **what a joiner would need in order to join it** -- for them the required mods | patch 740600 |
| **Private lobbies with a password** were added in a later patch, so they are not original | GameSpot, Geek Culture |

**The structure maps onto the requirement almost exactly**, which is the useful outcome:
a listing view with a filter, a host view that is the skirmish setup plus a name, per player
ready, and a start the host cannot press early.

**What the sources do NOT give, and this file will not pretend otherwise:** the pixel
layout, the exact control labels, the column set of the listing, and where the password
field sits on the host screen. Every source consulted said in as many words that it was
describing behaviour and not showing the interface.

So: **the FLOW is copied, and the LAYOUT is this project's own** -- the skirmish lobby's
widgets, which already have a design doc and a layout self-check. Anything below marked
**UNVERIFIED** is a choice made in that gap rather than a fact about Remastered, and each
one is cheap to change once somebody looks at the real thing.

## 1. The screens, and the route between them

Remastered splits HOST and JOIN as **tabs on one screen** rather than as two buttons on a
menu, and keeps the LAN entry separate on the main menu. Ours folds those together, because
we have one main-menu slot rather than two and because LAN and INTERNET were asked for as
filters within the browser:

```
MAIN MENU
  └─ MULTIPLAYER ──► [ HOST | JOIN ] one screen, two tabs
                       │        └─ the LISTING: rows, with LAN / INTERNET filters
                       └─ the SETUP: name, password, players, and the skirmish options
                                  │
                                  └──► LOBBY ──► the match
```

`DM_MULTIPLAYER` already exists in `menu/dosmenu.h` and today prints "is not implemented
yet". That is the entry point.

**Why tabs rather than two buttons**, since it differs from the first sketch: it is what
Remastered does, it is what the skirmish lobby's own map list already does (the tab widget
exists and has a layout check), and it means the browser is visible the moment the screen
opens, so somebody who wants to join sees games without choosing a menu item first.

### CREATE
The skirmish lobby plus three things it does not have:
- **GAME NAME**, text, with a default so the field is never empty. Shown in the browser.
- **PASSWORD**, text, optional. Empty means open.
- **PLAYERS**, 2 to 8, how many people the game seats. Computers fill the rest, and people
  plus computers cannot pass 8.

Everything else -- map, side, colour, team, credits, tech level, tiberium, crates,
superweapons, bases, unit count -- is the skirmish lobby's, unchanged, because it is
already the same set of decisions and already has a screen.

### BROWSER
A list of games, one row each: NAME, MAP, PLAYERS (2/4), PING, and a padlock when the game
has a password. Two filter tabs at the top, **LAN** and **INTERNET**, in the same tab
widget the map list already uses. REFRESH re-asks. JOIN enters the selected game, and asks
for the password first if the row has a padlock.

### LOBBY
The roster the skirmish screen already draws, with a **READY** column. A joiner toggles its
own ready with a button **bottom right, showing its active state**, which is where and how
Remastered puts it. The host's **START GAME** is greyed until every human seat is ready,
which is Remastered's behaviour and the requirement in the same sentence, and the button
says why while it is grey rather than merely being dead. Anybody may leave; the host
leaving ends the game.

**The host is always ready** and has no toggle: it is the one pressing START, so a ready
state for it would be a control that gates itself. **UNVERIFIED against Remastered.**

## 2. Finding games on a LAN

**A beacon, not a registry.** The host broadcasts a short UDP datagram to 255.255.255.255
on a fixed discovery port every second while it is waiting in the lobby. A browser opens
that port, listens, and lists what it hears. No service, no configuration, and it works on
a network with no internet at all.

```
CNC3D_LAN_PORT   17420        one below the default match port, deliberately adjacent
```

The beacon says only what the browser needs to draw a row and decide whether to try:

| field | why |
|---|---|
| magic + wire version | a browser must not list a game it could not join |
| the match port | the beacon port is fixed; the game's is not |
| game name | the row |
| map name | the row |
| players now / players wanted | the row, and whether it is full |
| password flag | the padlock, and whether to prompt |
| the order-wire hash and the scenario hash | so a row that cannot work is refused in the browser rather than after a join |

**A beacon carries no password and no player identity.** It is broadcast to a whole
network in clear, so it must contain nothing that matters. The password is checked at the
join, not at the listing.

**Liveness is the beacon stopping.** A row not heard from for a few seconds leaves the
list. Nothing has to announce that it has gone, which means a host that crashes disappears
correctly rather than lingering.

## 3. Finding games on the internet, and exactly what service that needs

The LAN beacon cannot cross a router, so the internet list needs somewhere for hosts to
announce to. Decision 1 in `docs/design-multiplayer.md` already recommended one small
always-on service, and `cnc3dgame.com` already serves `/api/builds`, `/api/changelog` and
`/api/download`, so this is a fourth route on a host that exists rather than a new thing to
run.

### The whole of it: four routes and a timeout

```
POST   /api/games            a host announces. Body is the same fields as the LAN beacon:
                             name, scenario, port, players_now, players_max, has_password,
                             abi, scen_hash. The server records the SOURCE IP itself and
                             ignores any address in the body. Returns {id, token}.
POST   /api/games/{id}/beat  heartbeat, every 15 s, carrying the token and the current
                             player count. Keeps the row alive and updates the count.
DELETE /api/games/{id}       a clean exit. Optional: the timeout covers a crash.
GET    /api/games            the browser's list. Returns the live rows.
```

**Liveness is a timeout, exactly as on the LAN.** A row not heartbeated for ~45 seconds
disappears. That is the whole of the state machine: no sessions, no accounts, no
matchmaking, no ranking. A host that crashes vanishes on its own.

**The source IP is taken from the connection and never from the body.** A host that could
name its own address could point joiners at somebody else, which turns a game list into a
way to aim traffic at a stranger.

### What it costs, said plainly

- **Writing it is small**: four routes, one table, a timeout. It is the smallest part of
  this feature by a wide margin.
- **Running it is not free forever.** It is a permanent dependency: if it stops, the
  INTERNET tab stops, and every player sees an empty list.
- **It only makes games VISIBLE, not JOINABLE.** This is the part worth being blunt about.
  The star means the host's port must be reachable, and on the internet that means the host
  forwards a port by hand. Roughly 15 to 30 percent of home connections will not manage a
  direct connection at all. So a listing service alone produces a browser full of games
  that some people cannot join, which is a worse experience than no list. **Making the
  INTERNET tab genuinely work needs hole punching and a relay as well**, which is decision
  2 and Phase 5, and is a monthly bill rather than an afternoon.
- **A public list exposes addresses.** Peer to peer lockstep means every player learns
  every other player's home address by construction. Design decision 8 records that this is
  still open, with two answers: relay every match, or say so publicly.

### So the recommendation is to do LAN now and the list later

Nothing about the LAN path depends on the service, and the LAN path is the one about to be
tested. **Until the endpoint exists the INTERNET tab is built, selectable, and honest**:
it says in one sentence that no server is configured, rather than showing an empty list
that reads as "nobody is playing". Wiring it up later is a client change of about an hour,
because the row it lists is the same `NbGame` the beacon already fills.

## 4. What the wire gains

Three additions to the handshake, all of them small, and one new packet type.

- **A password.** The joiner sends a hash of it in the HELLO; the host compares and refuses
  with `reason=password`. **UNVERIFIED whether Remastered hashes or sends it plainly**;
  hashing costs nothing and a beacon-discovered game on a shared network is exactly where
  a plaintext password would be read by somebody else. This is not security -- a hash
  replayed is as good as the password -- and the doc says so rather than implying otherwise.
- **A game name**, carried so the joiner's lobby can show what it joined.
- **READY**, a packet a joiner sends when its toggle changes and the host relays, so every
  lobby shows the same ticks.
- **START**, which already exists: today the host sends it when everyone has acknowledged
  the setup, and it becomes the thing the host's START GAME button sends instead.

## 5. What this deliberately does not do

- **No matchmaking, no ranking, no accounts.** A browser and a password.
- **No NAT traversal.** The star means only the host needs a reachable port, and on the
  internet that still means the host forwards one. Hole punching and a relay are Phase 5.
- **No chat.** Remastered has it in the lobby. It was not asked for and it is not free:
  it needs the text widget, a per-line wire message and a scrollback. Worth doing, later.
  **UNVERIFIED: exactly where Remastered puts the chat box.**
- **No mid-match join and no rejoin.** A dropped player still ends the match.

## 6. The order this gets built, and why

1. **The wire and the beacon**, with a headless gate. A browser that lists a game it cannot
   join is worse than no browser, so discovery is proven before a pixel is drawn.
2. **A text-entry widget** for the menu toolkit, which has none: the lobby today is gauges,
   tabs, lists and toggles, and a name and a password both need typing. It needs SDL text
   input plumbed into the menu shell, and it is the one piece with no precedent in the tree.
3. **The three screens**, on the skirmish lobby's widgets and its layout self-check.
4. **The menu button**, last, because until then there is nothing behind it.

Each step is testable on its own, and steps 1 and 2 are the ones that can surprise.

## 7. What a five-angle research pass corrected, and what it says is still missing

Researched after the first draft of this file, across patch notes, EA help, player forums,
video descriptions and the open-sourced repository. It changed four things and registered
several more.

### Applied

- **A private game is a CHECKBOX plus a FOUR DIGIT PASSCODE**, not a free-text password.
  Patch 735514, the first patch after launch: "The host must check the Private Game box in
  the setup screen / Hosts can then choose a four digit passcode." That is simpler than
  what was planned AND it is what the game does, so the screen has a tick box and a numeric
  field rather than a general text editor.
- **An empty seat reads "OPEN SLOT"**, which says the game is waiting for somebody rather
  than that a row is broken.
- **The passcode field is dead until the box is ticked**, and says "ANYONE MAY JOIN" while
  it is, so the state is legible without pressing anything.

### Deliberately NOT copied, with the reason

- **Hosting there publishes the room IMMEDIATELY and the match is configured INSIDE it**,
  while strangers are already arriving. Ours sets the match up first and then hosts,
  because the setup screen is the existing skirmish lobby and rebuilding all of it inside
  the waiting room would be a second copy of a screen that already has a layout check. The
  host can still reopen the setup from the waiting room. **This is the largest deliberate
  divergence in this document.**
- **Seat count comes from the MAP there, not from a host dial.** Ours is a dial because our
  maps all carry eight starts, so a dial is the only thing that can express "four of us".
- **Quickmatch, Steam invites that bypass the passcode, kicking, and a right-hand read-only
  detail pane on the join tab.** All of them need an identity system, a friends list or a
  service. We have none of those and inventing their shape would be cargo culting.
- **A host name in the list row.** Remastered lists the HOST'S PLAYER NAME because it has
  accounts. We have none, so a game NAME the host types is the honest substitute.

### Registered, not built

- **The random seed is lobby data the host distributes.** We rely instead on every peer
  starting from the same generator state, which is an invariant restored per scenario and
  gated. A seed on the wire would make it true by construction, and the research says that
  is what the shipped game does. This corroborates a note already made elsewhere.
- **The frame lag is negotiated at launch there**; ours is a fixed constant.
- **A "player requires map" indicator and map syncing on join.** Ours refuses a mismatched
  joiner by name at the handshake, which is honest but cannot fix it for them.
- **Tiberian Dawn caps at 6 players in that game** (4 at launch). Ours seats 8. Not a gap.
