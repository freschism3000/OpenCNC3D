#!/bin/sh
# C&C 3D v0.3.1: GDI first mission, stock (SCG01EA).
cd "$(dirname "$0")"
exec ./cnc_eyes --scen SCG01EA --pack SCG01EA.pack --cameos cameos.pack \
    --dospack dossidebar.pack --dosinf dosinfantry.pack --dylib TiberianDawn.dylib --dir missions/ --content content/
