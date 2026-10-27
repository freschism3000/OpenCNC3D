#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
. tools/mac/deployment-target.sh
. launcher/sources.sh
sh tools/launcher/make-config.sh
work=${OPENCNC3D_LOCAL_NOTES_GATE_OUT:-$(mktemp -d "${TMPDIR:-/tmp}/opencnc3d-local-notes.XXXXXX")}
mkdir -p "$work"
# Include actual lupdate.c; replace only transport by deterministic counting stubs.
sources=$(printf '%s' "$LAUNCHER_SOURCES" | sed 's/^launcher.c //' | sed 's/lupdate.c //' | sed 's/lnet.c //')
cd launcher
cc -O1 -Wall -Wextra -std=gnu89 -I. -I../game -I../menu -I../video \
   -DGL_SILENCE_DEPRECATION -o "$work/gate" \
   ../tools/launcher/gate-local-changelog.c $sources \
   $(sdl2-config --cflags --libs) -lz -framework OpenGL
cd ..
sh tools/bundle-sdl.sh "$work" gate >/dev/null
python3 tools/launcher/gate-local-changelog.py "$work/gate" "$work/fixtures"
