#!/bin/sh
# ---------------------------------------------------------------------------
#  C&C 3D -- the new 640x480 sidebar HUD, NOD mission 1.
#  Same as PLAY-HUD-NEW but the Nod side, so the radar-off emblem is the
#  scorpion shield rather than the GDI eagle. Press M to drop the radar.
#  WINDOW SIZE IS LOAD-BEARING: 960 is 2 x the HUD's native 480, so the sidebar
#  magnifies by a whole number and every 1995 pixel stays an exact 2x2 square.
#  At 720 the scale would fall back to 1x and letterbox, because a 1.5x step
#  doubles one source pixel in every two and visibly scrambles small text.
# ---------------------------------------------------------------------------
cd "$(dirname "$0")"
CNC3D_HUD=new exec ./cnc_eyes --scen SCB01EA --pack SCB01EA.pack --cameos cameos.pack \
    --dospack dossidebar.pack --dosinf dosinfantry.pack --dylib TiberianDawn.dylib \
    --dir missions/ --content content/ \
    --w 1600 --h 960
