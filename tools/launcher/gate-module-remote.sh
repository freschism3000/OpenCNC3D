#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
. tools/mac/deployment-target.sh
. launcher/sources.sh
# Use the authoritative config producer before any native fixture compiles.
sh tools/launcher/make-config.sh
work=$(mktemp -d "${TMPDIR:-/tmp}/opencnc3d-module-remote.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sources=$(printf '%s' "$LAUNCHER_SOURCES" | sed 's/^launcher.c //')
cd launcher
cc -O1 -Wall -Wextra -std=gnu89 -I. -I../game -I../menu -I../video -DGL_SILENCE_DEPRECATION -o "$work/gate" ../tools/launcher/gate-module-remote.c $sources $(sdl2-config --cflags --libs) -lcurl -lz -framework OpenGL
cd ..
sh tools/bundle-sdl.sh "$work" gate
python3 tools/launcher/gate-module-remote.py "$work/gate" "$work/fixture"

if [ -n "${OPENCNC3D_MODULE_GATE_EVIDENCE:-}" ]; then
    mkdir -p "$OPENCNC3D_MODULE_GATE_EVIDENCE"
    cp "$work/fixture/public-browser.png" "$work/fixture/update-browser.png" "$work/fixture/td-options.png" "$work/fixture/ra-options.png" "$OPENCNC3D_MODULE_GATE_EVIDENCE/"
fi
