#!/usr/bin/env python3
"""SCG10EA: the gunboat flume. A purpose mission that makes the GUNBOAT sail NORTH and
SOUTH, which the retail engine never shows, so its N/S facings and aim could never be
audited on a stock map.

WHY THE RETAIL BOAT ONLY SAILS EAST-WEST (all citations = brain/vanilla/tiberiandawn)
  unit.cpp UnitClass::Mission_Guard  (~3979): a gunboat in guard is instantly reassigned
      MISSION_HUNT ("05/08/1995 JLB : Fixes gunboat problems").
  unit.cpp UnitClass::Mission_Move   (~4035): "Gunboats must always have the hunt
      mission" -- move is instantly reassigned MISSION_HUNT too.
  unit.cpp UnitClass::Can_Player_Move (~3748): returns false for the gunboat, so the
      player cannot order it anywhere. It refuses orders BY DESIGN.
  unit.cpp UnitClass::Mission_Hunt   (~2870): with NO legal NavCom the hunt handler
      invents one: due WEST off-map if PrimaryFacing == DIR_W else due EAST off-map,
      always at Cell_Y(current coord) -- the boat's OWN ROW. This is the ping-pong.
  unit.cpp Per_Cell_Process          (~1890): when the loaner boat exits the playable
      map it is turned around: destination = the OPPOSITE off-map edge, SAME ROW,
      PrimaryFacing forced to DIR_E / DIR_W.
  unit.cpp Can_Enter_Cell            (~3131): "The gunboat can always move." MOVE_OK for
      every cell -- terrain never constrains it, so its course is EXACTLY the NavCom
      assignments above: horizontal forever.

THE ONE LEVER THE ENGINE LEAVES (and this mission pulls it)
  Mission_Hunt only invents a destination when !Target_Legal(NavCom). A LEGAL NavCom is
  KEPT. TeamClass::Coordinate_Move (team.cpp ~1240) re-assigns MISSION_MOVE plus
  NavCom = team waypoint every coordination pass; the boat flips itself back to HUNT
  each time, but the NavCom SURVIVES the flip (foot.cpp Assign_Mission ~1084 does not
  even clear the gunboat's Path), so the boat sails to the team waypoint. Put the
  waypoints due north/south of each other and the patrol runs VERTICALLY.

  Recruiting a placed boat is impossible: TeamClass::Add (team.cpp ~662) refuses
  members whose mission is already HUNT, and the boat is in HUNT within a tick of
  spawning. A REINFORCEMENT team dodges this: Do_Reinforcements (reinf.cpp ~165) adds
  the freshly created boat to the team while it is still in its factory-default
  MISSION_GUARD (udata.cpp UnitGunBoat ORDERS field), and the gunboat special case
  (reinf.cpp ~121) brings it in via SOURCE_SHIPPING: DisplayClass::Calculated_Cell
  (display.cpp ~2775) picks the NORTHERNMOST map row that is water across the FULL
  playable width and spawns the boat at its east end facing DIR_W.

MAP (64x64 INI space, playable X=4 Y=4 W=56 H=56, all-clear except the water)
  - Shipping lane: row y=14, x=4..59 = W1 water (template 0x01), the ONLY full-width
    water row, so the SOURCE_SHIPPING scan lands on it deterministically.
  - Flume: a vertical water channel x=51..53, y=14..36 (3 wide, matching the boat's
    3-cell Occupy_List in udata.cpp ~1735). The engine does not need it (MOVE_OK
    everywhere) but the test is honest terrain, not a boat on grass.
  - Waypoint 5 = (52,16) channel top, waypoint 4 = (52,34) channel bottom: 18-cell
    pure north-south legs, far beyond the STRAY_DISTANCE=2 arrival slop (team.h:47).
  - Team GBTM: 1x BOAT, missions Move:5, Move:4, Move:5, Loop:1 -> S leg, N leg,
    forever. Trigger GBRF: Time(1) fires Reinforce. on the first trigger scan
    (house.cpp ~1231, TriggerTime starts expired; Data is in 1/10ths of a minute).
  - BadGuy E1 on the east shore at (55,25), mission Sticky: a target inside the
    boat's WEAPON_TOMAHAWK 7.5-cell range (const.cpp ~87: range 0x0780) so the deck
    gun (SecondaryFacing) has something real to track on the N and S legs.
  - GoodGuy E1 on land at (10,50): keeps GoodGuy's IScan nonzero so no all-destroyed
    edge case can fire while the only "unit" is a gunboat (house.cpp ~1507 masks
    UNITF_GUNBOAT OUT of the ActiveUScan check -- a boat alone counts as dead).

TWO BOATS, BECAUSE THE ENGINE SPLITS THE GUNBOAT INTO TWO DISJOINT ABILITIES
  Firing requires In_Range, and every In_Range overload demands IsLocked
  (techno.cpp ~1180/~1235/~1287). A unit becomes IsLocked by Unlimbo-ing ON the map
  (techno.cpp ~1155) or by Per_Cell_Process -- which EXPLICITLY refuses to lock
  UNIT_GUNBOAT and UNIT_HOVER so they may leave the map again (techno.cpp ~962-975).
  A shipping reinforcement always unlimbos OFF-map (display.cpp SOURCE_SHIPPING
  returns x = MapCellX + MapCellWidth), so:
    - the TEAM boat (reinforcement) can be steered N/S but is IsLocked=false FOREVER:
      its Can_Fire is FIRE_RANGE at any distance; it tracks targets with the turret
      but can never shoot. Verified live with an instrumented brain (see NOTES.md).
    - a PRE-PLACED boat ([UNITS], on-map) is IsLocked=true and fires normally, but
      can never join a team: TeamClass::Add (team.cpp ~662) refuses members whose
      mission is HUNT, and every gunboat mission collapses to HUNT within one tick
      (Mission_Guard/Mission_Move overrides), so it is row-locked E/W for life.
  BOAT 000 in [UNITS] at (20,14) facing 64 (east: Mission_Hunt sends any non-west
  facing EAST first, unit.cpp ~2870) mission Guard is the pre-placed
  specimen: it ping-pongs the lane row E/W exactly like retail GDI-1's boat and
  PROVES the weapon fires in this mission (lane-side BadGuy E1 at (48,17) is in its
  path's range). The reinforcement boat proves the N/S body facings. No single
  gunboat can do both; that split is engine law, not a mission bug.

Run:  python3 make_scg10ea.py     # writes SCG10EA.INI + SCG10EA.BIN alongside
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))

W1 = 0x01      # TEMPLATE_WATER, 1x1, LAND_WATER (defines.h:1092, cdata.cpp:486)
CLEAR = 0xFF   # no template = clear

LANE_Y = 14
CHAN_X0, CHAN_X1 = 51, 53           # inclusive, 3 wide
CHAN_Y0, CHAN_Y1 = 14, 36           # inclusive
WP_TOP = (52, 16)                   # waypoint 5
WP_BOT = (52, 34)                   # waypoint 4
SHORE_E1 = (55, 25)                 # BadGuy target on the east bank
LANE_E1 = (48, 17)                  # BadGuy target near the lane, for the firing boat
LAND_E1 = (10, 50)                  # GoodGuy IScan keeper
LANE_BOAT = (20, 14)                # pre-placed (IsLocked, firing) specimen


def cell(x, y):
    return y * 64 + x


def main():
    # ---- BIN: 4096 records x 2 bytes [template, icon] ----
    rec = bytearray([CLEAR, 0x00] * 4096)

    def water(x, y):
        rec[2 * cell(x, y)] = W1
        rec[2 * cell(x, y) + 1] = 0x00

    for x in range(4, 60):              # the full-playable-width shipping lane
        water(x, LANE_Y)
    for y in range(CHAN_Y0, CHAN_Y1 + 1):  # the vertical flume
        for x in range(CHAN_X0, CHAN_X1 + 1):
            water(x, y)

    open(os.path.join(HERE, 'SCG10EA.BIN'), 'wb').write(bytes(rec))

    INI = """
[BASIC]
CarryOverCap=-1
CarryOverMoney=0
Intro=x
BuildLevel=98
Theme=No theme
Percent=0
Player=GoodGuy
Win=CONSYARD
Lose=GAMEOVER
Brief=GDI1
Action=LANDING

[MAP]
TacticalPos={tac}
Height=56
Width=56
Y=4
X=4
Theater=TEMPERATE

[GoodGuy]
FlagHome=0
FlagLocation=0
MaxBuilding=150
Allies=GoodGuy,Neutral
MaxUnit=150
Credits=100
Edge=South

[BadGuy]
FlagHome=0
FlagLocation=0
MaxBuilding=150
Allies=BadGuy
MaxUnit=150
Credits=0
Edge=North

[Neutral]
FlagHome=0
FlagLocation=0
MaxBuilding=150
Allies=Neutral
MaxUnit=150
Edge=North
Credits=0

[UNITS]
000=GoodGuy,BOAT,256,{laneboat},64,Guard,None

[INFANTRY]
000=GoodGuy,E1,256,{land},0,Guard,0,None
001=BadGuy,E1,256,{shore},0,Sticky,0,None
002=BadGuy,E1,256,{lanee1},0,Sticky,0,None

[STRUCTURES]

[TERRAIN]

[TEMPLATE]

[OVERLAY]

[SMUDGE]

[Triggers]
GBRF=Time,Reinforce.,1,GoodGuy,GBTM,0

[CellTriggers]

[Teams]

[Waypoints]
0={tac}
4={wbot}
5={wtop}

[TeamTypes]
GBTM=GoodGuy,0,0,0,0,0,7,1,1,0,1,BOAT:1,4,Move:5,Move:4,Move:5,Loop:1,0,0

[Base]
Count=0
""".format(
        tac=cell(44, 12),
        land=cell(*LAND_E1),
        shore=cell(*SHORE_E1),
        lanee1=cell(*LANE_E1),
        laneboat=cell(*LANE_BOAT),
        wbot=cell(*WP_BOT),
        wtop=cell(*WP_TOP),
    )

    open(os.path.join(HERE, 'SCG10EA.INI'), 'w').write(INI)
    print('wrote SCG10EA.BIN + SCG10EA.INI')
    print('  lane row y=%d, flume x=%d..%d y=%d..%d' % (LANE_Y, CHAN_X0, CHAN_X1, CHAN_Y0, CHAN_Y1))
    print('  waypoint 5 (top) = %s ini %d' % (WP_TOP, cell(*WP_TOP)))
    print('  waypoint 4 (bot) = %s ini %d' % (WP_BOT, cell(*WP_BOT)))
    print('  shore E1 = %s ini %d' % (SHORE_E1, cell(*SHORE_E1)))
    print('  lane E1  = %s ini %d' % (LANE_E1, cell(*LANE_E1)))
    print('  lane boat= %s ini %d' % (LANE_BOAT, cell(*LANE_BOAT)))


if __name__ == '__main__':
    main()
