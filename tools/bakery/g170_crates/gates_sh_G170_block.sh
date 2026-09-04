# G170 THE BONUS CRATES ARE THE CARTRIDGE'S OWN 3D CUBE, NOT THE 1995 FLAT SPRITE.
#
# The console draws a goodie crate as a model: the cell-decoration draw table at
# RAM 0x8020FF58 record 34 is {88, -1} and record 36 is {90, -1}, model slot = type id + 10,
# so slots 98 (grey steel, dl 0x011D4C8) and 100 (olive wood, dl 0x010F3C8), ten triangles
# each. We drew a flat quad of the 1995 DOS sprite instead, so this gate exists to keep the
# cubes on the glass once they are there.
#
# SCG32EA is the mission that can prove BOTH bindings in one picture: its [OVERLAY] places
# cell 3782 = SCRATE and 3783 = WCRATE, a steel crate at (6,59) with a wooden one directly
# east of it. So the left cluster must be steel and the right one wood, and a renderer that
# bound one mesh to both kinds fails on colour rather than on size.
#
# THREE LEGS, each of which has been made to fail on its own:
#   1. cratedump says two cells, both drawn, art=cube. A pack that predates the crate bake
#      reports art=sprite here and the gate reddens with the re-bake command in the message.
#   2. the same frame shot twice, crates on and crates off, diffed. The cube subtends
#      16 x 21 pixels and 313 changed pixels per crate; the sprite subtends 34 x 25 and 646.
#      The width band is 12..24, which the sprite cannot enter.
#   3. colour. The cartridge's steel cube is neutral grey (r - b = +3); the 1995 steel
#      sprite is blue-grey (r - b = -20). That is a witness leg 2 cannot fake, and the
#      kinddiff term is the one that catches both kinds bound to a single mesh.
# `stray` is the fourth and quietest: the crate pass must touch nothing outside its two
# windows, which is where a leaked GL state would show up.
gbegin shots/crate3d_on.png shots/crate3d_off.png
CRLOG=/tmp/g170_crate3d.txt
grun "$CRLOG" --scen SCG32EA --pack SCG32EA.pack $BASE --noshroud --script "$GATEDIR/gate_crate3d.txt"
grun -        --scen SCG32EA --pack SCG32EA.pack $BASE --noshroud --script "$GATEDIR/gate_crate3d_off.txt"
gshots shots/crate3d_on.png shots/crate3d_off.png
CRD=$(grep -c '^CRATEDUMP|cells=2|drawn=2|wood=[0-9][0-9]*|steel=[0-9][0-9]*|art=cube$' "$CRLOG")
CRPIX=$(python3 "$GATEDIR/gate_crate3d.py" shots/crate3d_on.png shots/crate3d_off.png | tee -a "$OUT")
CRV=$(echo "$CRPIX" | sed -n 's/.*verdict=\([a-z-]*\).*/\1/p')
if [ "$GRC" != "0" ]; then
  bad "G170 3D crates: the run failed or wrote no shot (GRC=$GRC). Any measurement below would be of stale pictures"
elif [ "${CRD:-0}" != "1" ]; then
  bad "G170 3D crates: cratedump did not report two crate cells, both drawn, both off a CUBE. It read [$(grep '^CRATEDUMP|' "$CRLOG")]. art=sprite means this pack carries no SCRATE/WCRATE mesh and the 1995 fallback is drawing; re-bake with tools/bakery/bake5.py"
elif [ "$CRV" != "cube" ]; then
  bad "G170 3D crates: the crate pixels are not the cube's. Want each crate 12..24 px wide, 150..520 changed pixels, the steel one neutral (|r-b| <= 10) and the wooden one olive (r-b >= 25), and nothing changed outside the two crate windows. It reads [$CRPIX]. steel_w near 34 with steel_rb near -20 is the 1995 sprite, i.e. the cube never reached the glass"
else
  ok "G170 3D crates: both cells draw the cartridge's own cube -- [$CRPIX]"
fi
