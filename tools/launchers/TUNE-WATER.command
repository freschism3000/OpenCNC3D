#!/bin/bash
# ============================================================================
#  THE SEA -- double-click this file.
#
#  The ordinary game with the F5 panel already open. Scroll to the group
#  3c  WATER  (ours) and drag: foam, its width, the spacing and speed of the
#  crests, the flow and its bearing, the shallows, the deep, the wet rim.
#  water_fx off is the cartridge's own water, for comparison, live.
#
#  The first GDI mission's beach and the rocks in its bay are the place to
#  look; every rock standing in the sea has its own ring of surf.
#
#  SAVE writes cnc3d-water.cfg beside the game. Send that file back and its
#  numbers become the shipped defaults.
#
#  SPACE pauses, ESC opens options, C flips the camera, + and - zoom.
# ============================================================================
cd "$(dirname "$0")"
echo "==========================================================="
echo "  F5 opens and closes the panel."
echo "  Scroll to the group  3c  WATER  and drag."
echo "  SAVE writes cnc3d-water.cfg beside the game."
echo "==========================================================="
echo
exec ./cnc3d --menupack dosmenu.pack \
     --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack \
     --dylib ./TiberianDawn.dylib --dir ./missions/ --content ./content/ \
     --w 1280 --h 720 \
     --gfx cnc3d-water.cfg --gfxpanel --gfxsave cnc3d-water.cfg
