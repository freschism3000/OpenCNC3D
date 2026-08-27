#!/bin/sh
# C&C 3D v0.3.1: Nod first mission (SCB01EA). GDI Hunt teams walk in; hold the outpost.
cd "$(dirname "$0")"
exec ./cnc_eyes --scen SCB01EA --pack SCB01EA.pack --cameos cameos.pack \
    --dospack dossidebar.pack --dosinf dosinfantry.pack --dylib TiberianDawn.dylib --dir missions/ --content content/
