#!/bin/bash
# ============================================================================
#  TUNE THE CLOUD SHADOWS -- double-click this file.
#
#  Starts the game with weather on and the F5 panel already open. Scroll to
#  the group called "3b  CLOUD SHADOWS" and drag. Everything takes effect on
#  the next frame; nothing needs a restart.
#
#    cloud shadows   off and on
#    strength        how dark it gets under one. 0 is invisible
#    coverage        how much of the ground is in shadow, and it is EXACT --
#                    0.40 means 40 per cent of the map, at any sharpness
#    sharpness       0 is a haze with no edge at all, 1 is a hard cut edge
#    size            how many cells the pattern repeats over, and so how big
#                    one mass is -- about an eighth of it. Large is drifting
#                    slabs; small is fine tonal variation, which is where the
#                    shipped set sits. What does not work is a size near 30:
#                    that is one mass across the whole view
#    speed           cells per second the deck travels. 0 parks it, which is
#                    the setting to use while judging shape
#    wind bearing    which way it blows, as a compass angle
#    deck height     how far above the ground the cloud sits. Higher slides
#                    the shadow further off a tall roof, the way a real one
#                    does; too high and it stops looking attached to anything
#
#  The sun's own bearing and height are in the group above this one, and they
#  move the cloud shadows too: it is the same sun, so a low sun stretches them
#  in the same direction it stretches everything else.
#
#  Coverage and sharpness do not fight each other. Sharpening the edges will
#  not darken the map, and covering more will not soften anything -- the mask
#  is built so that those two dials are independent.
#
#  When it looks right press SAVE at the top of the panel. That writes
#  cnc3d-clouds.cfg beside the game; send that file back and the numbers in it
#  become the new defaults.
#
#  SPACE pauses, ESC opens options, C flips the camera, + and - zoom, F5
#  closes the panel again.
# ============================================================================
cd "$(dirname "$0")"

echo "==========================================================="
echo "  F5 opens and closes the panel."
echo "  Scroll to the group  3b  CLOUD SHADOWS  and drag."
echo "  SAVE writes cnc3d-clouds.cfg beside the game."
echo "==========================================================="
echo

exec ./cnc3d --menupack dosmenu.pack \
     --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack \
     --dylib ./TiberianDawn.dylib --dir ./missions/ --content ./content/ \
     --w 1280 --h 720 \
     --gfx cnc3d-clouds.cfg --gfxpanel --gfxsave cnc3d-clouds.cfg
