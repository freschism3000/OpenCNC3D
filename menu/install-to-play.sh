#!/bin/sh
# Install the menu into the playable build folder, so it launches next to the game.
#
#   ./install-to-play.sh                     -> ~/Desktop/CNC3D-Play
#   ./install-to-play.sh /path/to/CNC3D-Play
#
# The play folder's convention is flat and path relative: a binary, its packs, a
# subfolder for bulk data, and a PLAY-*.command per thing you can start. This follows
# it exactly and touches nothing that is already there.
#
# Rebuild first if the source changed: ./build.sh && python3 bake_dosmenu.py
set -e
cd "$(dirname "$0")"

DEST="${1:-$HOME/Desktop/CNC3D-Play}"
[ -d "$DEST" ] || { echo "no such folder: $DEST" >&2; exit 1; }
[ -x ./preview ] || { echo "build it first: ./build.sh" >&2; exit 1; }
[ -f ./dosmenu.pack ] || { echo "bake it first: python3 bake_dosmenu.py" >&2; exit 1; }

mkdir -p "$DEST/movies" "$DEST/music"

# The binary is named for what it is, next to cnc_eyes.
cp -f ./preview "$DEST/cnc_menu"
cp -f ./dosmenu.pack "$DEST/dosmenu.pack"
cp -f ../dosdata/movies/LOGO.VQA "$DEST/movies/"
cp -f ../dosdata/movies/INTRO2.VQA "$DEST/movies/"
cp -f ../dosdata/music/MAP1.AUD "$DEST/music/"
cp -f ../dosdata/music/AOI.AUD "$DEST/music/"
cp -f ../dosdata/music/FWP.AUD "$DEST/music/" 2>/dev/null || true
cp -f ../dosdata/music/IND2.AUD "$DEST/music/" 2>/dev/null || true
cp -f ./READ-ME-MENU.txt "$DEST/READ-ME-MENU.txt"

cat > "$DEST/PLAY-MENU.command" <<'EOF'
#!/bin/sh
# CNC3D: the 1995 MS-DOS main menu, with the logo animation and music.
# Any key or click skips a movie. Arrows and return work; ESC quits.
cd "$(dirname "$0")"
exec ./cnc_menu -p dosmenu.pack \
    --logo movies/LOGO.VQA --intro movies/INTRO2.VQA \
    --music music/MAP1.AUD -s 3
EOF
chmod +x "$DEST/PLAY-MENU.command"

echo "installed into $DEST:"
echo "  cnc_menu, dosmenu.pack, movies/, music/, PLAY-MENU.command, READ-ME-MENU.txt"
du -sh "$DEST" | sed 's/^/  total now: /'
