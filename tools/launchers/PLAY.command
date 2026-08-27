#!/bin/sh
# C&C 3D. Boots to the 1995 DOS main menu with music; pick TEST MAP to play.
# ESC in game opens the Options dialog.
cd "$(dirname "$0")"
./cnc3d --menupack dosmenu.pack --cameos cameos.pack --dospack dossidebar.pack \
        --dosinf dosinfantry.pack --dylib ./TiberianDawn.dylib \
        --dir ./missions/ --content ./content/ --w 1280 --h 720
