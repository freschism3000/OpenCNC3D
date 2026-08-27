# USER90 -- the worked example

A small mission that uses every part of the editor's scripting layer once, so the whole
thing can be read in five minutes instead of inferred from a shipped mission that is
doing eleven things at the same time.

It is staged into `playable/missions/user_maps/` by `game/build.sh` if it is not already
there. Edit it freely -- build.sh never overwrites a copy that already exists.

**The name matters.** The game's own User Maps menu finds maps by PROBING `USER00` to
`USER99` rather than reading the directory, because `app/cnc3d.cpp` compiles for Windows
98 against a freestanding mingw and will not take `dirent.h`. So a user map keeps a name
from that space, which is why this one is `USER90` and not something readable -- the
readable part is `[Basic] Name=`, which both the editor's Open Map and the game's menu
show. 90 is chosen to stay far away from `USER00` upward, where new maps are named from.

Open it from the editor (**Open Map**, User Maps tab) or play it from the game's main
menu (**User Maps**).

Open it and press each tab.

## What is in it, and what each piece is there to show

**The world.** A GDI base bottom-left, a Nod base top-right, built through the editor's
own placement path. Fifteen objects, nothing exotic.

**STARTS.** `HOME` on the GDI base, so the mission opens looking at it. `REINFORCE` on the
west edge. Waypoints 0-3 are the route the Nod teams walk, which is what general-purpose
waypoints are actually for.

**TEAMS.** Three, each showing something different:

| team | shows |
|---|---|
| `patrol` | a LOOPING order chain -- Move, Move, Guard, Loop back to 0. It never disbands. |
| `raid1` | a chain that ends in `Loop:2`, so it re-attacks rather than going home |
| `storm` | Suicide set, so it ignores incoming fire and prosecutes its orders only |

Open TEAMS and look at the chain drawn on the left. The green line running back up the
gutter is the `Loop`, and it is a real edge: it points at the order it returns to.

**RULES.** Six, covering four shapes:

| rule | shows |
|---|---|
| `pat1` | a Time trigger, PERSISTENT -- it keeps making patrols forever |
| `rad1` | a Time trigger, volatile -- one raid, six minutes in |
| `scou` | `Player Enters` with a ZONE painted on four cells. Walk a unit in and it fires |
| `winn` / `lose` | the two `All Destr.` rules every mission needs |
| `strm` | a CARRIER: its event is None, so the engine never springs it |

**ENHANCED.** One rule, and it is the reason the tier exists:

```
storm = when ALL of:  100 tenths of a minute have passed
                      NOT (BadGuy owns 1 HAND or more)
        then fire carrier strm, and set counter stormed to 1
```

"Ten minutes have passed **AND** the Hand of Nod is gone" is two conditions joined by AND,
which a Tiberian Dawn trigger cannot express at all. The map is tagged `Enhanced=1`
because of it.

## Things to try

- **CHECK MISSION** (the menu, or the badge on the readout). It reports zero errors and
  zero warnings, which is the point: this is what a clean map looks like.
- **TRACE** (beside PLAY). Run 20 minutes. `rad1` fires at 5.9 minutes; `pat1` reports as
  *not traceable*, because a persistent trigger never leaves the game and so leaves no
  signal; the Enhanced rule reports NEVER, and the world readout underneath says why --
  nothing destroyed the Hand of Nod, so its second condition never became true.
- **PLAY**, and watch the script feed in the bottom left as each rule goes off. F7 hides it.
- Delete the Hand of Nod in the editor, save, and trace again. Now the Enhanced rule fires
  at ten minutes, because both of its conditions hold.

---

## USER91 -- the big-map worked example (128 x 128)

The first map that could not exist before the 128 tier, and the proof of every piece
of it. Open it and look at what it demonstrates:

- **[MAP] Version=1** is its first key. That single line is the contract: the brain
  reads a Version=1 map through `Read_Binary_Big`, whose .BIN is not the legacy
  8192-byte dense grid but SPARSE records (`{CELL cell; TemplateType tmpl; u8 icon}`,
  ascending) -- which is why this file is 472 bytes rather than 32768. Its INI cell
  numbers are y*128 + x; a legacy 64 map keeps y*64 + x and its dense .BIN, byte for
  byte, forever.
- **Terrain painted across y=63/64**, the old boundary, in one continuous stroke.
- **Eight starts** in [Waypoints] 0..7, the seats the 8-player brain patch and the
  lobby now fill.
- **A 16641-byte .HGT**: the corner heightmap is (W+1)^2, so elevation, ramps and the
  cliff dressing all work at 128 the way they do at 64.

Play it in the editor, then bake it (`sh tools/stage-skirmish-maps.sh --from
playable/missions/user_maps USER91`) and the pack comes out **CNC3DPKF** -- the pack
format that carries its own width and height instead of implying 64.
