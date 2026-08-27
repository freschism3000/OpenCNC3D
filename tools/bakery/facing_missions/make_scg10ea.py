#!/usr/bin/env python3
"""SCG10EA: independent facing-verification range (verify_facing lens, NOT the facing
module's own SCG09EA). All-clear 64x64 temperate map with a water lake in the south for
the gunboat. Written from scratch; only the INI boilerplate shape follows the engine's
required format.

GoodGuy: E1 (12,12), BGGY (28,12), MTNK (44,12), HARV (28,26), BOAT (32,46) on water.
BadGuy:  HAND building at (56,16) (unarmed turret-test target),
         decoy E1 sticky at (58,6) so the enemy house is never empty.
Water:   W1 (template 1) rectangle x 12..52, y 38..54.
INI cells are y*64+x (old 64x64 format)."""
import os

HERE = os.path.dirname(os.path.abspath(__file__))

W1 = 1          # TEMPLATE_WATER, 1x1, LAND_WATER (brain cdata.cpp)
CLEAR = 0xFF

cells = bytearray([CLEAR, 0x00] * 4096)
for y in range(38, 55):
    for x in range(12, 53):
        i = (y * 64 + x) * 2
        cells[i] = W1
        cells[i + 1] = 0
open(os.path.join(HERE, 'SCG10EA.BIN'), 'wb').write(bytes(cells))


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
003=GoodGuy,BOAT,256,{boat},0,Guard,None

[INFANTRY]
000=GoodGuy,E1,256,{e1},0,Guard,0,None
001=BadGuy,E1,256,{decoy},0,Sticky,0,None

[STRUCTURES]
000=BadGuy,HAND,256,{hand},0,None

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
    e1=cell(12, 12),
    bggy=cell(28, 12),
    mtnk=cell(44, 12),
    harv=cell(28, 26),
    boat=cell(32, 46),
    hand=cell(56, 16),
    decoy=cell(58, 6),
)

open(os.path.join(HERE, 'SCG10EA.INI'), 'w').write(INI)
print('wrote SCG10EA.BIN + SCG10EA.INI')
