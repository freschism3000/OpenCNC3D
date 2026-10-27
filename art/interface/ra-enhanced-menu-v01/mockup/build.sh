#!/bin/sh
# Rebuild every Red Alert Enhanced menu mockup and review sheet from the 1996 data:
# the inputs into build-native/ra-enhanced-menu-v01, the pictures into ../mockups.
set -e
cd "$(dirname "$0")"
python3 extract.py
python3 render_all.py
python3 sheets.py
