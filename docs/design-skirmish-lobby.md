# The skirmish lobby: what Red Alert actually has, and what CNC3D should build

Investigation and spec, August 2026. Every claim about the 1995 dialogs is read out of the
GPL checkout in `brain/vanilla` (`brain/patches/UPSTREAM.txt`), and line numbers are that
checkout's. The four files read in full are `redalert/nulldlg.cpp` (skirmish),
`redalert/netdlg.cpp` (network host), `tiberiandawn/nulldlg.cpp` (skirmish) and
`tiberiandawn/netdlg.cpp` (network host and join).

Companion document: `docs/design-skirmish.md`, which is the investigation behind the
skirmish itself. This one is only about the screen in front of it.

---

## 0. The short version

The current front end is a side plate and a plain list of map names, and the complaint is
that it is too bare. It is. But the fix is not "copy the Red Alert dialog", because a
straight copy would draw seven controls that do nothing in our build and would still not
show the one thing that makes a lobby feel like a lobby.

Four findings drive the whole spec:

1. **Red Alert's skirmish dialog has no player list.** `ColorListClass playerlist` is
   constructed at `redalert/nulldlg.cpp:1541` and is never added to the gadget chain
   (`playerlist.Add_Tail` appears zero times in the file), so it is never drawn. The
   dialog is a name box, a side droplist, a colour row, a map list, four gauges, a
   five item check list and a difficulty slider. Tiberian Dawn's skirmish dialog
   (`tiberiandawn/nulldlg.cpp:84`) does not even construct one.
2. **Tiberian Dawn's own network host lobby does have one, and that is the look we want.**
   `Net_New_Dialog` (`tiberiandawn/netdlg.cpp:2654`) puts a `ColorListClass` roster at the
   top left, one row per player, printed in that player's colour, formatted
   `"%s\t%s"` = name, tab, GDI or NOD (`tiberiandawn/netdlg.cpp:3063-3070`). That single
   widget is the difference between "a list of maps" and "a lobby".
3. **RA draws no map preview anywhere.** The string `preview` does not occur once in any
   `.cpp`, `.h`, `.c` or `.cs` file in the whole `brain/vanilla` tree outside of
   `tools/mixtool/mixnamedb_data.cpp`. There is no precedent to copy. What we can honestly
   put in its place is a map info line read from the scenario INI.
4. **About half of RA's controls have no path into our engine.** The only way our client
   configures a match is `CNCMultiplayerOptionsStruct` and `CNCPlayerInfoStruct`
   (`tiberiandawn/dllinterface.h`). Difficulty, colour, player name and Shadow Regrows have
   no field, no effect, or no field that Tiberian Dawn reads. Drawing them would be
   decoration that lies.

The spec in section 7 is one screen: a live player roster, the map list, and a short
control list in which every control writes a field the engine actually reads.

---

## 1. Red Alert's skirmish dialog, control by control

`Com_Scenario_Dialog(bool skirmish)`, `redalert/nulldlg.cpp:1337`. The same function serves
the serial (null modem) game and skirmish; `gameoptions = Session.Type == GAME_SKIRMISH`
(`:1498`) and the OK button is only added in skirmish (`:1619`).

### 1.1 Geometry

All positions below are computed at `factor == 1`, that is the 320x200 layout, which is the
one CNC3D's menu reproduces. The 640x400 build doubles every number
(`factor = (SeenBuff.Get_Width() == 320) ? 1 : 2`, `:1339`).

The dialog is the full screen: `d_dialog_w = 320, d_dialog_h = 200, x = 0, y = 0`
(`:1343-1346`).

### 1.2 The controls, in layout order

| # | control | class | x, y | w, h | range | default | source |
|---|---------|-------|------|------|-------|---------|--------|
| 1 | Your Name | `EditClass` (ALPHANUMERIC) | 45, 15 | 70, 9 | 12 chars (`MPLAYER_NAME_MAX`) | `Session.Handle` | `:1519` |
| 2 | Side | `DropListClass` | 130, 15 | 60, 40 | 6 items, HOUSE_USSR..HOUSE_FRANCE | `Session.House` | `:1531`, items `:1636-1638` |
| 3 | Colour | none, hand drawn | 210, 15 | 8 cells of 10x9 | 0..7 (`MAX_MPLAYER_COLORS` 8) | `Session.PrefColor` | draw `:1866-1880`, hit test `:1947-1952` |
| 4 | (player list) | `ColorListClass` | 15, 33 | 118, 39 | never drawn | n/a | `:1541`, no `Add_Tail` |
| 5 | Scenarios | `ListClass` | 79, 33 | 162, 78 | one row per map, 12 rows visible | index 0 | `:1549`, filled `:1716-1746` |
| 6 | Count | `GaugeClass` | 94, 114 | 25, 7 | `CountMax[bases] - CountMin[bases]`, so 0..49 bases off and 0..12 bases on | `(max+min)/2` | `:1557`, range `:1679` |
| 7 | Count readout | `StaticButtonClass` | 122, 114 | auto | text only | "0" | `:1560` |
| 8 | Level | `GaugeClass` | 94, 121 | 25, 7 | 0..9 (`MPLAYER_BUILD_LEVEL_MAX - 1`, RA's max is 10) | `BuildLevel - 1` | `:1562`, range `:1683` |
| 9 | Level readout | `StaticButtonClass` | 122, 121 | auto | text only, "**" above max | | `:1565` |
| 10 | Credits | `GaugeClass` | 94, 128 | 25, 7 | 0..`Rule.MPMaxMoney` = 10000 | `Rule.MPDefaultMoney` = 3000 | `:1567`, range `:1686` |
| 11 | Credits readout | `StaticButtonClass` | 122, 128 | auto | text only | | `:1570` |
| 12 | AI Players | `GaugeClass` | 94, 135 | 25, 7 | 0..`Rule.MaxPlayers - 2` = 6, value is count minus 1 | 1 | `:1572`, range `:1689-1696` |
| 13 | AI readout | `StaticButtonClass` | 122, 135 | auto | text only | | `:1575` |
| 14 | Options | `CheckListClass` | 171, 114 | 106, 34 | 5 items, see below | see below | `:1577`, items `:1667-1677` |
| 15 | Difficulty | `SliderClass` | 45, 158 | 230, 8 | 0..3 coarse, 0..5 fine | 1 coarse, 2 fine | `:1589-1601` |
| 16 | OK | `TextButtonClass` | 31, 180 | 45, 9 | | | `:1585` |
| 17 | Cancel | `TextButtonClass` | 244, 180 | 45, 9 | moved to the Load slot at `:1622` | | `:1587` |
| 18 | Load | `TextButtonClass` | 244, 180 | 45, 9 | **never added to the chain** | | `:1586` |

The five check list items, in order (`:1667-1677`):

| index | label | initial state |
|-------|-------|---------------|
| 0 | Bases | `Rule.IsMPBasesOn` = true |
| 1 | Ore Spreads | `Rule.IsMPTiberiumGrow` = true |
| 2 | Crates | `Rule.IsMPCrates` = true |
| 3 | Shadow Regrows | `Rule.IsMPShadowGrow` = true |
| 4 | Capture The Flag | `Rule.IsMPCaptureTheFlag` = false |

Labels printed as static text, not gadgets (`:1813-1860`): "Your Name", "Side:", "Color:",
"Easy"/"Normal"/"Hard" over the difficulty slider, "Scenarios", "Count", "Level",
"Credits:", "AI Players:".

### 1.3 What is NOT in the dialog

No player roster (see above). No map preview. No start position control. No team control.
No colour choice for the computer players. No difficulty per opponent. No chat: the message
area is laid out at `:1407-1410` but the difficulty slider is placed on top of it at y=158
and nothing in the skirmish path feeds it.

---

## 2. What each control does to the game

| control | writes | what the engine then does |
|---------|--------|---------------------------|
| Your Name | `Session.Handle`, copied to `who->Name` at `:2196` | the string appears in the engine's own 2D chrome and in multiplayer score screens |
| Side | `Session.House` (`:1985`) | becomes `HouseClass::ActLike`, which picks the build list and the unit tables |
| Colour | `Session.PrefColor` and `Session.ColorIdx` (`:1951-1952`) | becomes `HouseClass::RemapColor`, which the engine's own sprite renderer uses to recolour units |
| Scenarios | `Session.Options.ScenarioIndex` (`:2000`) | selects the INI to load |
| Count | `Session.Options.UnitCount` (`:2010`) | the number of starting units per house |
| Level | `BuildLevel` (`:2024`) | gates the whole build list and the starting unit mix |
| Credits | `Session.Options.Credits`, rounded to the nearest 500 at `:2041` | every house's opening bank |
| AI Players | `Session.Options.AIPlayers` (`:2055-2069`) | how many computer houses are created |
| Options 0 Bases | `Session.Options.Bases`, and rescales the Count gauge (`:2082-2100`) | with bases off nobody gets an MCV |
| Options 1 Ore Spreads | `Session.Options.Tiberium`, plus `Special.IsTGrowth`, `Rule.IsTGrowth`, `Special.IsTSpread`, `Rule.IsTSpread` (`:2101-2105`) | resource regrowth |
| Options 2 Crates | `Session.Options.Goodies` (`:2107`) | bonus crates scattered on the map |
| Options 3 Shadow Regrows | `Special.IsShadowGrow` (`:2108`) | explored ground re-shrouds over time |
| Options 4 Capture The Flag | `Special.IsCaptureTheFlag` (`:2109`) | flag victory mode |
| Difficulty | `Scen.Difficulty` and `Scen.CDifficulty` (`:2206-2233`) | per-house handicap biases |
| OK | ends the loop, then fills `Session.Players` (`:2195-2200`) | |

Two facts about the difficulty slider worth recording, because they explain why it is not
worth copying even in a game where it worked. It is coarse by default
(`Rule.IsFineDifficulty` is false in both games, `redalert/rules.cpp:181`,
`tiberiandawn/rules.cpp:123`), which gives it a maximum of 3, and the mapping is
`diff = value * 2` into a switch with cases 0..4 (`:2206`). Value 3 therefore produces
`diff = 6`, no case matches, and the difficulty is left at whatever it was. The coarse
slider has four positions and three of them work.

---

## 3. The map preview

**Red Alert does not draw one, and neither does Tiberian Dawn.** Not in the skirmish dialog,
not in the network host lobby, not in the join screen. Searched the entire `brain/vanilla`
tree for the substring `preview` in every `.c`, `.cpp`, `.h` and `.cs` file: one file
matches, `tools/mixtool/mixnamedb_data.cpp`, and it is a filename table. The 1995 lobby
identifies a map by its description string and nothing else. The Remastered Collection's
own front end draws previews, but that front end is C# and is not in this tree, so there is
no code precedent here to follow.

What we can put there honestly is a **map info line**, read from the scenario INI the client
already opens to get the map name. Everything below is a real, measured field of the nine
shipped INIs in `game/missions`:

| scenario | Name | Theater | size | waypoints below 26 |
|----------|------|---------|------|--------------------|
| SCM01EA | GREEN ACRES | TEMPERATE | 58x49 | 0..7 |
| SCM02EA | Sand Trap | DESERT | 57x58 | 0..7 |
| SCM03EA | Lost Arena | TEMPERATE | 59x61 | 0..7 |
| SCM04EA | River Raid | TEMPERATE | 56x59 | 0,1,2,3,4,5,17 |
| SCM05EA | Eye of the Storm | DESERT | 57x61 | 0,1,2,3,4,5,7 |
| SCM06EA | Lakefront Clash | DESERT | 62x61 | 0..6 |
| SCM07EA | Desert Madness | DESERT | 62x57 | 0..9 |
| SCM08EA | Pitfall | TEMPERATE | 62x62 | 0..7 |
| SCM09EA | Moosehead Barrens | DESERT | 62x62 | 0..7 |
| SCM90EA | CANYON RUN | DESERT | 61x53 | 0,1 |

`Theater` is in `[MAP]`, not `[Basic]` (`tiberiandawn/display.cpp:1291` and the correction in
`docs/design-skirmish.md` section 4). Reading it from the wrong section is a known trap.

An actual rendered preview is possible later from the `.pack` terrain, but it is new tooling,
it is not something either 1995 game had, and it is not the thing that makes the screen feel
bare. The roster is.

---

## 4. House, colour, and the computer opponents

**House.** RA uses a `DropListClass` with six items, the playable countries
(`redalert/nulldlg.cpp:1636-1638`). TD's skirmish dialog uses the same widget with two items,
"GDI" and "NOD" (`tiberiandawn/nulldlg.cpp:348-350`), both read only
(`Set_Read_Only(true)`). TD's network **join** screen instead uses two plain toggle buttons,
`TextButtonClass gdibtn` and `nodbtn` (`tiberiandawn/netdlg.cpp:893` and `:901`). Both are
authentic; the two button form is the cheaper one for us because we own no droplist widget.

**Colour.** Neither game uses a widget. The row of swatches is drawn by hand with
`Fill_Rect` plus `Draw_Box`, and clicks are caught in the raw `KN_LMOUSE` arm by a bounding
box test that divides the x offset by the cell width
(`redalert/nulldlg.cpp:1866-1880` and `:1947-1952`;
`tiberiandawn/nulldlg.cpp:564-578` and `:659-665`). RA has eight colours, TD six
(`MAX_MPLAYER_COLORS`, `redalert/session.h:65` and `tiberiandawn/defines.h:2591`). The six TD
colours are yellow, blue grey, red, green, orange and blue green
(`tiberiandawn/globals.cpp:469-484`), and every one of their text indices is inside the same
sixteen colour GUI ramp our menu already prints with: `CC_GDI_COLOR = YELLOW`,
`CC_NOD_COLOR = RED`, `CC_BLUE_GREY = LTBLUE`, `CC_GREEN = GREEN`, `CC_ORANGE = PURPLE`,
`CC_BLUE_GREEN = CYAN` (`tiberiandawn/defines.h:2873-2878`).

**Computer opponents.** Both games call them **AI Players** and both use a `GaugeClass`
labelled `TXT_AI_PLAYERS_COLON`. There is no "add opponent" button, no per opponent row and
no naming: it is one number.

- RA: gauge maximum `Rule.MaxPlayers - 2` = 6, value is the count minus 1, clamped so that
  AI plus one human is under `Rule.MaxPlayers` = 8, giving **1 to 7 computer opponents**
  (`redalert/nulldlg.cpp:1689-1697`, `:2054-2072`).
- TD: gauge maximum hardcoded to 4 with `Rule.MaxPlayers` commented out, clamped against a
  literal 6, giving **1 to 5 computer opponents** (`tiberiandawn/nulldlg.cpp:397-405`,
  `:765-780`). `MAX_PLAYERS` is 6 (`tiberiandawn/defines.h:2565`).

A wart to be aware of when reading TD: the skirmish dialog stores the AI player count in
`MPlayerGhosts`, a variable that everywhere else in the engine is a boolean meaning "houses
with no player still play". On our path that reuse does not reach us: our client sets the
opponent count by how many `CNCPlayerInfoStruct` entries carry `IsAI = true`, and
`CNC_Set_Multiplayer_Data` forces `MPlayerGhosts = true` by itself the moment it sees one
(`tiberiandawn/dllinterface.cpp:756`).

---

## 5. Tiberian Dawn against Red Alert: the differences that bite

We run the TD engine. Anything RA draws that TD cannot honour is a trap.

| RA has | TD | consequence for us |
|--------|----|--------------------|
| Shadow Regrows check item | **does not exist**. `IsShadowGrow` occurs exactly once in the whole TD tree and it is inside a commented out line (`tiberiandawn/nulldlg.cpp:379` and `:385`). There is no `Special.IsShadowGrow` member. | never draw it. `CNCMultiplayerOptionsStruct::MPlayerShadowRegrow` exists in the header but is read by nothing: the string occurs once in the entire TD source, in `dllinterface.h` |
| "Ore Spreads" | the same field, labelled `"Tiberium Regrows"` as a bare literal (`tiberiandawn/nulldlg.cpp:377`) | use the tiberium wording |
| Build level max 10 | `MPLAYER_BUILD_LEVEL_MAX` is 7 (`tiberiandawn/defines.h:2581`) | a 1..10 tech gauge would be wrong by three |
| `Rule.MPMaxMoney` = 10000, default 3000 | no `MPMaxMoney` in TD; the gauge maximum is the literal `10000 /* TODO: Rule.MPMaxMoney*/` and the dialog default is 10000 (`tiberiandawn/nulldlg.cpp:394`, `:360`) | pick our own default, 10000 is the ceiling |
| Credits rounded to 500 | TD does not round (`tiberiandawn/nulldlg.cpp:749`) | rounding is ours to add, and it is a kindness on a 25 pixel gauge |
| 8 colours, 8 players | 6 and 6 | any colour work is six wide |
| Capture the Flag | real in TD: `Special.IsCaptureTheFlag` is read in `house.cpp`, `scenarioini.cpp` and `unit.cpp`, and every shipped skirmish INI carries flag data | the engine would honour it; **CNC3D has no flag art**, `capture the flag` occurs zero times in `game/cnc_eyes.cpp` |
| difficulty slider | present in TD's dialog and it writes `Scen.Difficulty` | dead on our path, see section 6 |

Two TD only options exist in the export struct that no 1995 dialog offers:
`IsMCVDeploy` (the MCV can undeploy instead of being sold) and `SpawnVisceroids`. Both are
read by the engine (`building.cpp`, `house.cpp`). They are Remaster era additions, not
1995 lobby controls.

---

## 6. The only path into our engine

Our client cannot set an engine global. It fills two structs and hands them to
`CNC_Set_Multiplayer_Data`, and passes `build_level` to `CNC_Start_Custom_Instance`. Anything
outside that is unreachable. The mapping, read from
`tiberiandawn/dllinterface.cpp:695-780`:

| lobby control | struct field | engine effect | honoured by TD | visible in CNC3D |
|---|---|---|---|---|
| Side | `CNCPlayerInfoStruct::House` | `HouseClass::ActLike` via `Init_Data` (`dllinterface.cpp:998`) | yes | yes, it is what `act=` reports and what picks the texture set |
| AI opponents | count of entries with `CNCPlayerInfoStruct::IsAI` | `MPlayerIsHuman[]`, which gates the whole expert system (`house.cpp:929`) | yes | yes |
| Tech level | `CNC_Start_Custom_Instance` argument | `BuildLevel = build_level` (`dllinterface.cpp:1403`) | yes | yes, sidebar and starting units |
| Starting credits | `MPlayerCredits` | `Init_Data(color, house, MPlayerCredits)` | yes | yes |
| Bases | `MPlayerBases` | MCV creation and base building (`scenarioini.cpp:1606`) | yes | yes |
| Early win | `DestroyStructures` | `Special.IsEarlyWin` | yes | yes, it is the only end condition that fires reliably |
| Tiberium regrows | `MPlayerTiberium` | `Special.IsTGrowth` and `IsTSpread` (`dllinterface.cpp:734-739`) | yes | yes, and it **leaks into later campaign missions in the same process** |
| Crates | `MPlayerGoodies` | crates scattered | yes | **no.** The brain patch dumps only tiberium and wall overlays, so a crate is an invisible pickup |
| Superweapons | `EnableSuperweapons` | `Rule.AllowSuperWeapons` | yes | yes, the renderer arms and targets specials |
| Unit count | `MPlayerUnitCount` | starting escort, both with bases on and off (`scenarioini.cpp:1483,1506`) | yes | yes, and above 0 it breaks the first click on most starts |
| Capture the flag | `CaptureTheFlag` | `Special.IsCaptureTheFlag` | yes | no art |
| Teams and allies | `CNCPlayerInfoStruct::Team` | equal team numbers become `Make_Ally` (`dllinterface.cpp:1044-1056`) and decide the winning team at game over (`:2807`) | yes | the renderer has no ally concept: anything not yours is drawn hostile |
| Start position | `CNCPlayerInfoStruct::StartLocationIndex` | `StartLocationOverride`, an unchecked index into the compacted waypoint list | yes | yes, but the legal range differs per map |
| MCV undeploy | `IsMCVDeploy` | `Special.IsMCVDeploy` | yes | untested in CNC3D |
| Visceroids | `SpawnVisceroids` | `Special.IsVisceroids` | yes | no visceroid art path checked |
| **Difficulty** | none | `CNC_Set_Difficulty` returns immediately unless `GameToPlay == GAME_NORMAL` (`dllinterface.cpp:1843`) | **no** | drawing it would be a lie |
| **Colour** | `CNCPlayerInfoStruct::ColorIndex` | `HouseClass::RemapColor`, used only by the engine's own 2D sprite renderer | set, but | **no.** CNC3D picks a texture set from `act=`, and the cartridge carries two |
| **Player name** | `CNCPlayerInfoStruct::Name` | `HouseClass::Name`, 12 chars | set, but | **no.** The name never reaches the screen in CNC3D |
| **Shadow regrows** | `MPlayerShadowRegrow` | nothing. The identifier appears once in the TD tree, in the header | **no** | no |
| **Aftermath units** | `MPlayerAftermathUnits` | nothing, same as above | **no** | no |

---

## 7. The spec: the CNC3D skirmish lobby

### 7.1 Shape

**One screen, not two.** The side plate goes away for skirmish and the side choice moves
into the lobby, so that picking a side, an opponent count and a map is one act. The campaign
keeps its own side plate untouched.

320x200, 8 bit, drawn with the `db_*` primitives into one surface, hit tested in menu
pixels, exactly like `menu/dosops.c`. No new rendering path and no cost to the Voodoo 2 tier.

The plate is the one the mission list already draws, so the two screens are siblings:
`DO_DLG_X 16, DO_DLG_Y 6, DO_DLG_W 288, DO_DLG_H 188` (`menu/dosops.h`). Caption drawn with
the filigree pair the way `dosops` and `dosmenu` already do.

### 7.2 Layout

All coordinates are 320x200 menu pixels. Row height 8 to match `DO_ROW_H`.

```
 +----------------------------------------------------------------+ 16,6  288x188
 |                       MULTIPLAYER GAME                         |  caption y=12
 |                                                                |
 |   Players                        Battlefield                   |  labels y=26
 |   +------------------------+     +--------------------------+  |
 |   | PLAYER          GDI    |     | GREEN ACRES              |  |  roster 24,34 124x51
 |   | COMPUTER 1      NOD    |     | Sand Trap                |  |  maps  156,34 140x51
 |   |                        |     | Lost Arena               |  |  6 rows each
 |   +------------------------+     +--------------------------+  |
 |   [   GDI   ] [   NOD   ]     TEMPERATE  58x49  6 starts       |  side btns y=92
 |                                                                |  info line y=94
 |   AI Players: [====   ]  2       [x] Bases     (locked)        |  gauges y=110,120,130
 |   Tech Level: [=======]  7       [x] Tiberium Regrows          |  checks x=160
 |      Credits: [====   ]  5000    [x] Superweapons              |
 |                                                                |  row y=140 reserved
 |   Bases cannot be turned off yet: the match would end at once.  |  status y=154
 |                                                                |
 |            [   PLAY   ]        [  CANCEL  ]                    |  buttons y=168
 +----------------------------------------------------------------+
```

Exact rectangles:

| element | x | y | w | h |
|---|---|---|---|---|
| dialog plate | 16 | 6 | 288 | 188 |
| caption text | centred 160 | 12 | | |
| "Players" label | centred 86 | 26 | | |
| "Battlefield" label | centred 226 | 26 | | |
| roster box | 24 | 34 | 124 | 51 (6 rows of 8 plus 3) |
| map list box | 156 | 34 | 140 | 51 (6 rows of 8 plus 3) |
| GDI button | 24 | 92 | 60 | 9 |
| NOD button | 88 | 92 | 60 | 9 |
| map info line | 156 | 94 | 140 | 7 |
| gauge labels, right aligned | 100 | 110 / 120 / 130 | | 7 |
| gauges | 102 | 110 / 120 / 130 | 44 | 7 |
| gauge readouts | 150 | same rows | | 7 |
| checkbox squares | 160 | 110 / 120 / 130 | 7 | 7 |
| checkbox labels | 171 | same rows | | 7 |
| reserved fourth row, both columns | | 140 | | 7 |
| status line | 24 | 154 | 272 | 7 |
| PLAY | 84 | 168 | 60 | 9 |
| CANCEL | 176 | 168 | 60 | 9 |

PLAY and CANCEL reuse `DO_PLAY_X/W`, `DO_CANCEL_X/W`, `DO_BTN_Y`, `DO_BTN_H` unchanged.

The fourth row on both columns is laid out and left empty. It is where Unit Count and Crates
go if they ever earn their place (7.5), and leaving the slot means adding them later moves
nothing.

### 7.3 The roster

This is the one addition that answers "too bare", and it is read only: it is a live readout
of the side buttons and the AI Players gauge, not a control. No hit test, no scrollbar,
because it can never exceed six rows.

Row format is TD's own, `name`, tab at x offset 78, side
(`tiberiandawn/netdlg.cpp:3063-3070`). Row 0 is the human, rows 1..n the computers.

```
PLAYER            GDI
COMPUTER 1        NOD
COMPUTER 2        NOD
```

Each row is printed in that player's colour, taken from TD's own table
(`tiberiandawn/globals.cpp:478-485`) and expressed in the sixteen colour GUI ramp our menu
already has: yellow, light blue, red, green, purple, cyan for players 0..5. That gives six
distinguishable rows for free. Note that the row colour is a **lobby** colour only: on the
battlefield there are two texture sets and both armies take theirs from the side, so two
computers on the same side will look alike in play. Say so in the status line rather than
pretending otherwise.

Names are fixed: "PLAYER" and "COMPUTER n". There is no name box, because
`CNCPlayerInfoStruct::Name` never reaches a CNC3D pixel.

### 7.4 The map list and the info line

The map list is the existing `dosops` list behaviour, restyled to fit the narrower box: same
scroll, same selection, same source. `skirmish_scan` in `app/cnc3d.cpp:309` already finds the
maps and reads the name out of `[Basic] Name=`.

The info line under it shows, for the selected map, three facts read from the same INI:
theater from `[MAP] Theater=`, size from `[MAP] Width=` and `Height=`, and the start position
count computed by the rule in 7.7. Three facts and no picture is a legible substitute for a
preview and it is honest about what we know.

### 7.5 The controls

| control | widget | range | default | writes | in v1 |
|---|---|---|---|---|---|
| GDI / NOD | two toggle text buttons, exactly one lit | | GDI | `GameOpts.side`, then `CNCPlayerInfoStruct::House` for every player: the human takes the chosen side, every computer takes the other | **yes** |
| Map list | list, existing | one row per installed map | first row | `GameOpts.scen`, `GameOpts.pack` | **yes** |
| AI Players | gauge | 1 .. (map cap minus 1), never above 5 | 1 | number of `CNCPlayerInfoStruct` entries with `IsAI = true` | **yes** |
| Tech Level | gauge | 1 .. 7 | 7 | `build_level` argument of `CNC_Start_Custom_Instance` | **yes** |
| Credits | gauge | 0 .. 10000, snapped to 500 | 5000 | `MPlayerCredits` | **yes** |
| Superweapons | checkbox | on / off | on | `EnableSuperweapons` | **yes** |
| Bases | checkbox drawn **disabled** | locked on | on | `MPlayerBases` and `DestroyStructures` together | **shown, locked** |
| Tiberium Regrows | checkbox | on / off | on | `MPlayerTiberium` | **only with the leak fix**, see below |
| Unit Count | gauge | 0 .. 10 | 0 | `MPlayerUnitCount` | **yes**, with a warning: see below |
| Crates | checkbox | | off | `MPlayerGoodies` | **yes**, drawn and clickable |
| PLAY / CANCEL | text buttons | | | | **yes** |

The gauge readouts are printed as plain text to the right of each gauge, which is what both
1995 dialogs do (`StaticButtonClass` in RA, a raw `Fancy_Text_Print` in TD).

Every default above is already the command line default in `game/cnc_eyes.cpp:14303-14316`,
so the lobby and the switches agree from day one and neither has to move.

Three controls need their reasoning on the record.

**Bases is drawn and locked.** Bases off is a real 1995 mode and we cannot offer it yet:
with bases off no house has a building or an MCV, and `Special.IsEarlyWin` then declares
every house dead about a second into the match. The two settings are interlocked, and until
they are properly interlocked the honest thing is to draw the box the engine has, check it,
grey it, and put the reason in the status line when the player clicks it. That is the
legibility rail: the control exists, its state is true, and the reason it will not move is
stated.

**Tiberium Regrows must not ship before the leak is fixed.** `CNC_Set_Multiplayer_Data`
writes `Special.IsTGrowth` and `Special.IsTSpread` (`tiberiandawn/dllinterface.cpp:734-739`),
`MapClass::Logic` reads them with no mode guard, and nothing restores them. A skirmish played
with tiberium off permanently disables tiberium growth for every campaign mission afterwards
in the same process. Measured, in `docs/design-skirmish.md` section 6.9. Either the reset
lands in the same change or the checkbox stays out and the value stays at its default.

**Unit Count SHIPS, with a warning, and this paragraph used to say it stayed out of v1.**
The slider was then asked for. The measurement below is unchanged and is the reason
the DEFAULT is zero and the reason the status line says what it says; it is no longer the
reason the control is absent. What was actually wrong was smaller than this ruling assumed:
the gauge was fully built all along, with a range, a rectangle, a caption, a mouse arm, a
keyboard arm and a field in the result, and `sk_draw`'s loop simply stopped one item short of
it, so it took input and drew nothing. The same was true of the Crates box one column over.
The original ruling, kept because its measurement is still the argument for the default: The engine scatters the escort within about four cells of the
MCV and any unit inside the 3x3 Construction Yard pad makes the player's first click do
nothing. Swept across all nine maps at both start positions: 0 leaves 17 of 18 starts
deployable, 1 leaves 10, 2 leaves 9, 5 leaves 4, and 6 or more leaves none. There is no safe
value, the default is the measured best, and a gauge whose every position but one damages the
opening minute is not a feature. If it is wanted later, it belongs behind the same kind of
warning the Bases box gets.

### 7.6 Deliberately not drawn

Each of these would be a control that drives nothing, and saying so is more useful than
drawing it.

- **Difficulty slider.** `CNC_Set_Difficulty` is a no op unless the game is
  `GAME_NORMAL`. Every multiplayer house takes `DIFF_NORMAL` and nothing changes it. Any
  Easy or Hard we offer would be a mechanic we invented, not one we exposed.
- **Colour swatches.** The cartridge carries two house texture sets, chosen by `act=`.
  A colour choice would change a number nothing draws.
- **Player name box.** The name never reaches a CNC3D pixel, and we own no edit widget.
- **Shadow Regrows.** Tiberian Dawn has no such field. The struct member exists and is read
  by nothing.
- **Capture the Flag.** The engine would honour it. We have no flag art and no flag handling
  in the renderer, so the mode would be unplayable and unreadable.
- **Start position picker.** Real field, real effect, but the legal index range differs per
  map because `Create_Units` compacts waypoints 0..25 and indexes the compacted list with no
  bound check. On SCM04EA index 6 lands on an interior marker rather than a start. A picker
  that offers "position 1..8" is wrong on four of the nine maps. Later, and only with the
  per map rule in 7.7 driving it.
- **Teams.** The field is real and works. The renderer has no ally concept: anything not
  yours is drawn hostile. A 2v2 would look like a 1v3.
- **Chat and the Send Message button.** There is nobody to talk to.

### 7.7 The per map player cap

The AI Players gauge must not offer more opponents than the selected map has start
positions, and the count is a property of the map, not a constant.

Rule, from `Create_Units` (`tiberiandawn/scenarioini.cpp:1446-1476`) and
`GlyphX_Assign_Houses` (`tiberiandawn/dllinterface.cpp:934-958`): read the `[Waypoints]`
section, count the **contiguous** run of valid entries starting at 0, and cap the result at
`MAX_PLAYERS` = 6. Contiguity matters because the engine compacts the list, so a gap makes
index n mean waypoint n+1, and on at least one map that lands on a marker that is not a start
at all.

Applied to what ships: every one of the nine retail maps yields 6, and the hand authored
SCM90EA yields 2. So the gauge is 1..5 on the retail maps and 1..1 on the canyon, computed
rather than assumed, and the info line can say "6 starts" honestly.

### 7.8 Input

Mouse and keyboard, the same contract `dosops` and `dopt` already have. Arrow keys walk the
focused list, tab moves between the roster side, the map list and the control block, space
toggles a checkbox, left and right nudge a gauge, Enter is PLAY, Escape is CANCEL. A gauge is
dragged with the mouse the way `gauge.cpp` does it and the way `dopt` already implements it,
including the click offset so the thumb does not jump.

---

## 8. Widgets: what already exists and what is new

`docs/design-skirmish.md` section 6.5 said "every option widget is new work". That was true
of `menu/`, and it is not true of the program. The gauge and the checkbox both already exist,
drawn in the same idiom, on the same kind of surface, in `game/dosopt.c`: the Game Controls
page is sliders quoted from `gamedlg.cpp` and `sounddlg.cpp`, and the Advanced page is a
column of checkboxes. They hit test, they drag with the correct click offset, and they draw
disabled correctly.

So the inventory for this screen is:

| needed | status |
|---|---|
| dialog plate, caption, filigree | exists, `menu/dosmenu.c` and `menu/dosops.c` |
| text button, enabled and disabled | exists, `menu/dosmenu.c` |
| scrolling list with selection | exists, `menu/dosops.c` |
| gauge with drag | exists in `game/dosopt.c`, needs lifting into a shared place |
| checkbox | exists in `game/dosopt.c`, same |
| toggle text button, lit and unlit | exists: `DOPT_V_CLASSIC` and `DOPT_V_ENHANCED` in `game/dosopt.h:483-484` are already a mutually exclusive pair. TD's own form of this is `Turn_On` plus `Set_Text` (`tiberiandawn/netdlg.cpp:2992-3006`) |
| coloured roster rows | new, and it is a list draw with a per row colour |
| droplist, edit box, colour row | **not needed**, see 7.6 |

Nothing here needs a new drawing primitive. A companion inventory with the exact addresses
of each of those widgets is in `docs/design-skirmish-lobby-widgets.md`; where the two
disagree about what already exists, that one is the closer read of the working tree.

---

## 9. Decisions for the project owner

1. **Does the side plate stay?** The spec folds the side choice into the lobby so skirmish is
   one screen. The alternative is to keep the plate and drop the two buttons.
2. **Is Tiberium Regrows offered in v1?** Only if the `Special` reset lands in the same
   change. Otherwise it is a checkbox that quietly damages the campaign.
3. **Is Unit Count offered at all?** Recommended out. It is the setting that decides whether
   the player's first click works.
4. **Do we want a real map preview later?** It is new tooling and neither 1995 game had one.
   The three fact info line is the cheap honest version.
5. **Six computer opponents or one?** The engine allows five, the maps allow five, and
   multiple computers do not gang up on the player. 1v1 is still the honest default; the
   gauge costs nothing to offer.
6. **Teams.** The field works today. The renderer would draw an ally as an enemy. Worth a
   separate small piece of work if 2v2 is wanted.

---

## 10. Register entries this document owes

For `known-gap notes`:

- **Crates are invisible in CNC3D.** The brain patch dumps only tiberium and wall overlays,
  so `MPlayerGoodies` scatters pickups that never appear on screen. The option is therefore
  not offered.
- **`CNCMultiplayerOptionsStruct::MPlayerShadowRegrow` and `MPlayerAftermathUnits` are read
  by nothing in Tiberian Dawn.** Each identifier occurs exactly once in the whole TD source
  tree, in the header that declares it.
- **The coarse difficulty slider in both 1995 dialogs has a dead position.** Maximum 3,
  mapping `value * 2` into a switch with cases 0..4, so value 3 changes nothing. Recorded
  because it is evidence the control is not worth reproducing, not because we reproduce it.
- **Red Alert's skirmish dialog constructs a player list and never draws it.**
  `ColorListClass playerlist` at `redalert/nulldlg.cpp:1541` is never added to the gadget
  chain. Anyone using that dialog as the reference for a roster will not find one there; the
  roster lives in `tiberiandawn/netdlg.cpp`.
