#!/usr/bin/env python3
"""SCG09EA: the facing-audit range. All-clear 64x64 temperate map, four GoodGuy units
parked far apart on open ground, no enemies near enough to interfere, no win trigger
that could fire early. INI cells are y*64+x (old 64x64 format).

Units: E1 (16,16), BGGY (32,16), MTNK (48,16), HARV (32,40).
A lone BadGuy E1 sits in the far corner (60,60) so 'All Destr.' style logic never sees
an empty enemy house; nothing hunts, nothing shoots at 40+ cells."""
import os

HERE = os.path.dirname(os.path.abspath(__file__))

# ---- BIN: 4096 records x 2 bytes, template 0xFF = clear ----
open(os.path.join(HERE, 'SCG09EA.BIN'), 'wb').write(bytes([0xFF, 0x00]) * 4096)

def cell(x, y):
    return y * 64 + x

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
Credits=1000
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
000=GoodGuy,BGGY,256,{bggy},0,Guard,None
001=GoodGuy,MTNK,256,{mtnk},0,Guard,None
002=GoodGuy,HARV,256,{harv},0,Guard,None

[INFANTRY]
000=GoodGuy,E1,256,{e1},0,Guard,0,None
001=BadGuy,E1,256,{decoy},0,Sticky,0,None

[STRUCTURES]

[TERRAIN]

[TEMPLATE]

[OVERLAY]

[SMUDGE]

[Triggers]
LOSE=All Destr.,Lose,0,GoodGuy,None,0

[CellTriggers]

[Teams]

[Waypoints]
0={tac}

[TeamTypes]

[Base]
Count=0
""".format(
    tac=cell(20, 14),
    e1=cell(16, 16),
    bggy=cell(32, 16),
    mtnk=cell(48, 16),
    harv=cell(32, 40),
    decoy=cell(60, 60),
)

open(os.path.join(HERE, 'SCG09EA.INI'), 'w').write(INI)
print('wrote SCG09EA.BIN + SCG09EA.INI  (E1@16,16 BGGY@32,16 MTNK@48,16 HARV@32,40)')
