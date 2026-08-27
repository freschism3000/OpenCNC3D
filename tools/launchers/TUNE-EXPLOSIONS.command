#!/bin/bash
# ============================================================================
#  TUNE THE EXPLOSIONS -- double-click this file.
#
#  Opens a map with explosions going off NON-STOP right in front of the camera,
#  so you can hold a key and watch the size change live.
#
#    1 / 2    THE BLAST   smaller / bigger  (the bright flash and sparks)
#    3 / 4    THE TRAILS  smaller / bigger  (the orange dotted arcs that stream
#                                            behind each flying chunk)
#    5 / 6    THE DEBRIS  smaller / bigger  (the chunks of wreckage themselves)
#    7 / 8    SMOKE/FIRE  smaller / bigger  (what hangs in the air afterwards)
#
#    9        reset everything to the cartridge's own values
#    0        print the current numbers
#
#  Every explosion here is a real FRAG1 -- the same one a dying vehicle makes --
#  fired through the same recipe as in a real game. Nothing is faked for the
#  test, so what you tune is what you get.
#
#  A key affects the NEXT explosion, not the ones already burning. That is true
#  on the real console too: a particle's size is set when it is born. Since a
#  fresh set goes off about twice a second, just hold the key and watch.
#
#  When it looks right, press 0 to print the three numbers. They also
#  print in the Terminal window behind the game, one line per keypress.
#
#  Blast, debris and smoke start at 1.00 = exactly what the ROM says. TRAILS
#  start at 4.00, which is the original setting against the console -- the ROM's
#  own ember size reads far too thin. Press 9 to put everything back to 1.00.
#
#  SPACE pauses, ESC opens options, C flips the camera, + and - zoom.
# ============================================================================
cd "$(dirname "$0")"

echo "========================================================"
echo "  1 / 2   BLAST   smaller / bigger  (the flash)"
echo "  3 / 4   TRAILS  smaller / bigger  (the orange arcs)"
echo "  5 / 6   DEBRIS  smaller / bigger  (flying chunks)"
echo "  7 / 8   SMOKE   smaller / bigger  (what lingers)"
echo "  9  reset to cartridge      0  print the numbers"
echo "========================================================"
echo

exec ./cnc_eyes --scen SCB01EA --pack SCB01EA.pack \
     --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack \
     --dylib TiberianDawn.dylib --dir missions/ --content content/ \
     --efxloop 30 --blastscale 1.00 --trailscale 4.00 \
     --chunkscale 1.00 --smokescale 1.00
