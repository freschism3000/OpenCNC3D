#!/bin/bash
# ============================================================================
#  TUNE THE LOOK -- double-click this file.
#
#  Opens Nod mission 1. While it is running:
#
#    SHADOWS
#      ]   bigger              [   smaller
#      '   further north-east  ;   back south-west
#
#  (Explosions have their own launcher now: TUNE-EXPLOSIONS.command)
#
#    \   print every current number
#
#    SPACE pauses, ESC opens options, C flips the camera, + and - zoom.
#
#  Shadow keys take effect immediately.
#
#  When it looks right press \ to print the numbers. They also print in
#  the Terminal window behind the game, one line per keypress, so you can just
#  read the last one.
#
#  Starting point is the cartridge's own: shadows at their authored size, with
#  the north-east nudge you asked for.
#
# ============================================================================
cd "$(dirname "$0")"

echo "======================================================"
echo "  SHADOWS      ]  bigger      [  smaller"
echo "                '  further NE  ;  back SW"
echo "  \\  print the current numbers"
echo "======================================================"
echo

exec ./cnc_eyes --scen SCB01EA --pack SCB01EA.pack \
     --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack \
     --dylib TiberianDawn.dylib --dir missions/ --content content/ \
     --shadowscale 1.00 --shadownudge 24
