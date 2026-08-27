#!/bin/bash
# ============================================================================
#  TUNE THE PRESENTATION -- double-click this file.
#
#  Boots the DOS main menu with the Tier 2 presentation chain ON and its panel
#  already open. Start a mission (or TEST MAP) and the panel is down the left
#  hand side of the screen.
#
#    F5              open and close the panel
#    click a box     toggles it
#    drag a slider   sets it, live. The game keeps playing behind the panel.
#    mouse wheel     scrolls the panel when the pointer is over it
#
#  The three buttons at the top:
#
#    SAVE       writes cnc3d-fx.cfg NEXT TO THIS FILE. THAT IS THE FILE TO SEND
#               BACK. Its numbers become the game's new defaults.
#    DEFAULTS   puts the numbers back without switching anything off
#    ALL OFF    the picture the game shipped with, for an instant A/B
#
#  What is on the panel, top to bottom:
#
#    bilinear filtering   smooths every texture, the 3D art AND the sidebar
#    1  VI output gamma   the console's own output curve. THE ONE DIAL THAT IS
#                         NOT A TASTE VALUE: the cartridge's number is near 0.50
#                         and 1.00 is off. Worth settling first, against footage.
#    2  supersampling     render scale. 2.00 is a good place to sit.
#    3  sun and shadows   bearing, height, strength, softness, colour. While
#                         this is ON the cartridge's own shadows are switched
#                         off, so you are looking at one shadow, not two.
#    4  bloom             glow on the bright things: fire, explosions, tracers
#    5  ambient occlusion contact shade where things meet the ground
#    6  dynamic light     explosions and gunfire lighting the ground around them
#    7  colour grade      exposure, contrast, saturation, warmth, blacks, whites
#    10 CRT               scanlines, phosphor mask, curvature, signal bleed
#
#  Everything starts switched on except the CRT and bilinear filtering, so the
#  first thing you see is the whole chain at once. Use ALL OFF to compare.
#
#  If the panel says SHADERS UNAVAILABLE in red, nothing else on it will do
#  anything: send the first dozen lines of the Terminal window behind the game.
# ============================================================================
cd "$(dirname "$0")"

echo "======================================================"
echo "  F5 opens and closes the tuning panel"
echo "  SAVE writes cnc3d-fx.cfg here. Send that file back."
echo "======================================================"
echo

exec ./cnc3d --menupack dosmenu.pack \
     --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack \
     --dylib ./TiberianDawn.dylib --dir ./missions/ --content ./content/ \
     --w 1280 --h 720 \
     --gfx --gfxpanel --gfxsave cnc3d-fx.cfg
