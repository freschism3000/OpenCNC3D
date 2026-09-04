#!/bin/bash
# ============================================================================
#  CLOUD SHADOWS -- double-click this file.
#
#  The ordinary game, with weather switched on: soft shadows drift across the
#  ground, the buildings and the units, on the same bearing as the sun and at
#  a speed measured in cells per second rather than in frames, so they cross
#  the map at the same rate whatever the machine is doing.
#
#  Nothing in the cartridge has this. It is ours, and it is off in the shipped
#  defaults for that reason -- this launcher is what turns it on.
#
#  To CHANGE it, use TUNE-CLOUDS.command instead: same thing with the F5 panel
#  already open on the dials.
#
#  SPACE pauses, ESC opens options, C flips the camera, + and - zoom.
# ============================================================================
cd "$(dirname "$0")"

exec ./cnc3d --menupack dosmenu.pack \
     --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack \
     --dylib ./TiberianDawn.dylib --dir ./missions/ --content ./content/ \
     --w 1280 --h 720 \
     --gfx cnc3d-clouds.cfg --gfxsave cnc3d-clouds.cfg
