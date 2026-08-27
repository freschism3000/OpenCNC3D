#!/bin/sh
# ---------------------------------------------------------------------------
#  C&C 3D -- boots the 1995 DOS main menu with the NEW 640x480 sidebar HUD.
#  Pick TEST MAP to play. ESC in game opens the Options dialog.
#
#  What to look at on the sidebar:
#    * build cells carry the C&C95 cameos, with their name bars
#    * REPAIR / SELL / MAP and the scroll arrows light under the pointer, sink
#      when held, and REPAIR and SELL latch gold while engaged
#    * M drops the radar and shows the faction emblem
#    * start something building and watch the clock wedge sweep off the cameo
#
#  PLAY.command is the same menu on the 1995 DOS sidebar, for comparison.
#  WINDOW SIZE IS LOAD-BEARING: 960 is 2 x the HUD's native 480, so the sidebar
#  magnifies by a whole number and every 1995 pixel stays an exact 2x2 square.
#  At 720 the scale would fall back to 1x and letterbox, because a 1.5x step
#  doubles one source pixel in every two and visibly scrambles small text.
# ---------------------------------------------------------------------------
cd "$(dirname "$0")"
CNC3D_HUD=new exec ./cnc3d --menupack dosmenu.pack --cameos cameos.pack \
        --dospack dossidebar.pack --dosinf dosinfantry.pack --dylib ./TiberianDawn.dylib \
        --dir ./missions/ --content ./content/ --w 1600 --h 960
