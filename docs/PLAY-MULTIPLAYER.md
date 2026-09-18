# Playing a network match, 2 to 8 people

Everything here has been run. Where something has NOT been run, it says so.

## The shape of it

One machine HOSTS and the others JOIN it. The host relays: joiners talk only to the host,
and the host passes each joiner's orders to the others. **Only the host's machine has to be
reachable.** Nobody has to open a port on a joining machine.

**The host's machine must let UDP port 17421 in.** Seeing the game in the browser proves
nothing about that: the advert is outbound. Windows asks once, the first time MULTIPLAYER
is opened, whether `cnc3d.exe` may accept connections; choose Allow, and on a Private
network. A cancelled or hidden prompt, or a Public profile, drops the joiner's first
packet silently. On a Mac, System Settings > Network > Firewall must allow `cnc3d` if the
firewall is on, and **System Settings > Privacy & Security > Local Network must list the
game and have it on, on BOTH Macs**: without that permission a Mac refuses the game's
packets to LAN addresses while still receiving the host's advert, which looks like a
host that never answers. The game asks for it the first time the launcher starts and
again when MULTIPLAYER opens ("C&C3D would like to find and connect to devices on your
local network"): click Allow. If you never saw that dialog, open the Local Network list
and switch the game on there. The log then shows `NET|send-failed` while it is refused. From v0.6.8 a joiner that
gets no answer for ten seconds is told so, on the browser's status line, with the address
it tried. A PRIVATE GAME with a passcode shows a `*` in the browser; a joiner without the
passcode is refused by name.

**The log.** Windows writes `cnc3d-log.txt` beside `cnc3d.exe` (or under
`%APPDATA%\Slipgate Ironworks\CNC3D\` if that folder is read-only); from v0.6.8 a Mac
started from Finder writes the same file under
`~/Library/Application Support/Slipgate Ironworks/CNC3D/`. Both are overwritten on every
start, so copy them before launching again. Every `NET|` line is in there.

Every machine simulates the whole world and only ORDERS cross the wire, so all of them must
start from exactly the same map and the same build. The handshake checks that and refuses
by name rather than letting a mismatched pair start and fall apart on the first tick.

## What every machine needs

The same build and the same map files. The handshake compares:

| It checks | If it differs |
|---|---|
| the wire version | `refused ... reason=version` |
| the brain's order layout | `refused ... reason=order-wire layout` |
| the scenario's own bytes, the `.INI` and the `.BIN` | `refused ... reason=map bytes` |

The art does not have to match, and is deliberately not checked: two people with different
packs draw different pictures of one identical world, which is not a desync.

## Hosting

```
cd playable          # wherever this build is installed
./cnc_eyes --scen SCM01EA --pack SCM01EA.pack \
    --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack \
    --dylib TiberianDawn.dylib --dir missions/ --content content/ \
    --skirmish --side gdi --ai 0 --players 4 --host
```

`--players N` is how many PEOPLE are in the match, 2 to 8, and it is the host's decision
alone. `--ai N` adds computers after the people; people plus computers cannot pass 8 seats,
and if you ask for more the computers give way and it says so.

The host prints the address to join, which is the thing nobody is ever sure of:

```
NET|hosting|port=17421|scenario=SCM01EA|humans=4|seats=4|waiting=120s
NET|join-me-at|192.168.1.204:17421
```

It then waits up to 120 seconds for the other three. It names each one as it arrives:

```
NET|joiner|seat=1|peer=192.168.1.51:59310|seated=2/4
```

## Joining

Take the address the host printed:

```
cd playable          # wherever this build is installed
./cnc_eyes --scen SCM01EA --pack SCM01EA.pack \
    --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack \
    --dylib TiberianDawn.dylib --dir missions/ --content content/ \
    --skirmish --side nod --ai 0 --join 192.168.1.204
```

Add the port after the address if the host used a different one: `--join 192.168.1.204 17421`.

`--players` on a joiner is ignored: the host says how many people are in the match, exactly
as it says which map. Everything else the joiner asks for loses to the host too, except its
own side.

Nobody starts until everybody is in. Each machine then prints its own seat:

```
NET|match|seat=2|humans=4|seats=4|scenario=SCM01EA|speed=3|abi=C6D08568|scen=E4DC23AD
```

## Before you blame the game

`netcheck` is shipped in both packages and exercises the whole scheduler and socket layer
with no game attached. Run it on each computer first:

```
./netcheck selftest
```

Under a second, ends in PASSED, and prints `ORDER DIGEST E2668540`. If that passes on both
machines, whatever happens next is not the binary. Then the pair, host first:

```
./netcheck host 17421           # on the host
./netcheck join <host-ip> 17421 # on the other computer
```

Both print the same digest and a round-trip time. **That round trip is the number that
decides how the match feels**: an order is stamped three turns ahead, which at 15 Hz is
200 ms of cover, so the link keeps up while the round trip stays under about 400 ms.

## When it goes wrong

| What you see | What it means |
|---|---|
| `NET|error=the host never answered` | Nothing reached the host. Wrong address, or a firewall on the HOST's machine. Only the host needs to be reachable |
| `NET|refused-by-host|reason=map bytes` | The two ends hold different files for the same map name. Copy the host's `missions/<SCEN>.INI` and `.BIN` |
| `NET|refused-by-host|reason=order-wire layout` | Different builds. Every player must run the same one |
| `NET|error=only 2 of 4 players joined` | The host gave up waiting. Everyone has to be in within 120 seconds |
| `NETDESYNC|frame=N|seat=S|...` | The worlds parted. Keep every machine's output: the line names the frame, the seat, and both fingerprints |
| `NET|peer-left|seat=S` | Somebody quit or dropped. The match ends for everyone today; takeover and rejoin are a later phase |

The lobby (HOST / JOIN from the multiplayer screen) prints its own lines: on the host
`NET|hosting|`, one `NET|join-me-at|` per address it has, `NET|joiner|seat=` when someone
is seated, `NET|refused|peer=...|reason=...|theirs=|mine=` when someone is turned away,
and `NET|ignored|peer=...` when a packet it does not understand arrives; on a joiner
`NET|joining|peer=`, then `NET|seated|seat=` or `NET|lobby|refused|...` or
`NET|lobby|failed|no answer from ...`. A host log with `join-me-at` and then neither
`joiner` nor `refused` nor `ignored` means the joiner's packets never reached it.

## What is NOT done, stated rather than left to be discovered

- **This has been proven with eight peers on ONE computer, over loopback, and has never run
  across a real network.** Gate G199 runs a host and seven joiners in one match and checks
  that all eight agree on every world hash, but a real network adds latency and loss that
  loopback does not. The first cross-machine run is the honest test and it has not happened.
- **The internet is not addressed.** No hole punching, no relay service, no lobby. Across
  the internet the host's port has to be forwarded by hand, and this project's own record
  of trying that is a list of networks that refused.
- **A dropped peer ends the match** for everybody, with a message. Takeover and rejoin are
  Phase 5.
- **There is no lobby.** The command line is the lobby.
- **Sixteen players is a decision, not a capability.** The wire and the roster seat eight.
  The `abi=` field in the match line reports the classic brain's order wire; sixteen needs
  the Enhanced brain underneath and that number changing.
