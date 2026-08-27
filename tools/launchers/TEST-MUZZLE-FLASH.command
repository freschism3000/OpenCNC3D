#!/bin/bash
# ============================================================================
#  TEST THE CANNON MUZZLE FLASH -- double-click this file.
#
#  GDI mission 5, which starts you with TWO MEDIUM TANKS of your own. No
#  explosion loop running, so nothing competes with what you are looking for.
#
#  WHAT TO DO
#    Select a Medium Tank, right-click an enemy, and watch the END OF THE
#    BARREL as it fires.
#
#  WHAT YOU ARE LOOKING FOR
#    A brief bright puff at the muzzle, lasting a fraction of a second, in the
#    same style as the flash a Jeep or Buggy already makes when its machine gun
#    fires. It is a FLASH, not a jet -- roughly ten particles over two ticks.
#
#  IF IT LOOKS TOO SMALL OR TOO BIG
#    1 / 2   makes it smaller / bigger, live.
#    The cannon flash is built from the cartridge's own muzzle-glow art, which
#    belongs to the "blast" group, so keys 1 and 2 resize it along with the
#    other bright flashes. Press 0 to print the numbers.
#
#  TO COMPARE AGAINST THE REAL CONSOLE
#    The N64 draws NO cannon flash at all -- the cartridge's own recipe for it
#    is empty. This one was added because you asked for it. To see the console's
#    actual behaviour, quit and run this file again after adding
#    --nocannonflash to the last line.
#
#  Also worth firing: the Guard Tower and the artillery, which use the same
#  flash. Jeeps and Buggies were never affected -- they always had one.
#
#  SPACE pauses, ESC opens options, C flips the camera, + and - zoom.
# ============================================================================
cd "$(dirname "$0")"

echo "=================================================================="
echo "  Select a MEDIUM TANK, right-click an enemy, watch the barrel."
echo "  1 / 2  resize the flash      0  print the numbers"
echo "=================================================================="
echo

exec ./cnc_eyes --scen SCG05EA --pack SCG05EA.pack \
     --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack \
     --dylib TiberianDawn.dylib --dir missions/ --content content/
