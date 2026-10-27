#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
. tools/mac/deployment-target.sh
. launcher/sources.sh
# Generated headers are absent in a fresh source checkout, including release CI.
sh tools/launcher/make-config.sh
work=$(mktemp -d "${TMPDIR:-/tmp}/opencnc3d-update-modules.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
# The fixture includes production lupdate.c so it can exercise its private parser.
sources=$(printf '%s' "$LAUNCHER_SOURCES" | sed 's/^launcher.c //' | sed 's/lupdate.c //')
cd launcher
cc -O1 -Wall -Wextra -std=gnu89 -I. -I../game -I../menu -I../video \
   -DGL_SILENCE_DEPRECATION -o "$work/gate" \
   ../tools/launcher/gate-update-module-selection.c $sources \
   $(sdl2-config --cflags --libs) -lcurl -lz -framework OpenGL
cd ..
sh tools/bundle-sdl.sh "$work" gate >/dev/null
python3 tools/launcher/gate-update-module-selection.py "$work/gate" "$work/fixtures"
