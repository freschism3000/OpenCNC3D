#!/bin/sh
# The same mission as PLAY-HUD-NEW on the 1995 DOS sidebar, for side-by-side.
cd "$(dirname "$0")"
exec ./cnc_eyes --scen SCG01EC --pack SCG01EA.pack --cameos cameos.pack \
    --dospack dossidebar.pack --dosinf dosinfantry.pack --dylib TiberianDawn.dylib \
    --dir missions/ --content content/ \
    --w 1600 --h 960
