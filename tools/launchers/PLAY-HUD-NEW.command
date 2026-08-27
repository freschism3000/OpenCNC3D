#!/bin/sh
# ---------------------------------------------------------------------------
#  C&C 3D -- the new 640x480 sidebar HUD, GDI mission 1.
#
#  What to look at:
#    * the build cells now carry the C&C95 cameos with their name bars
#    * REPAIR / SELL / MAP and the scroll arrows light on hover, sink when
#      pressed, and REPAIR and SELL latch gold while they are engaged
#    * press M to drop the radar and see the GDI emblem
#
#  PLAY-HUD-OLD.command is the same mission on the 1995 DOS bar, for comparison.
#  WINDOW SIZE IS LOAD-BEARING: 960 is 2 x the HUD's native 480, so the sidebar
#  magnifies by a whole number and every 1995 pixel stays an exact 2x2 square.
#  At 720 the scale would fall back to 1x and letterbox, because a 1.5x step
#  doubles one source pixel in every two and visibly scrambles small text.
# ---------------------------------------------------------------------------
cd "$(dirname "$0")"
CNC3D_HUD=new exec ./cnc_eyes --scen SCG01EC --pack SCG01EA.pack --cameos cameos.pack \
    --dospack dossidebar.pack --dosinf dosinfantry.pack --dylib TiberianDawn.dylib \
    --dir missions/ --content content/ \
    --w 1600 --h 960
