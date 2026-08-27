#!/bin/sh
# ============================================================================
#  Install Editor.app into playable/.
#
#  playable/ is gitignored, so the app that lives there cannot be committed and
#  would not survive a fresh clone. tools/launchers/Editor.app is the tracked
#  master; this puts a copy where it can be double-clicked.
#
#  Running it again is safe and is how a change to the master reaches the copy.
#  (The launcher also self-updates on its next run, so this is belt and braces
#  rather than the only route.)
#
#      sh tools/install-editor-app.sh
# ============================================================================
set -e
cd "$(dirname "$0")/.."
ROOT="$(pwd)"

MASTER="$ROOT/tools/launchers/Editor.app"
DEST="$ROOT/playable/Editor.app"

[ -d "$MASTER" ] || { echo "no $MASTER -- nothing to install" >&2; exit 1; }
[ -d "$ROOT/playable" ] || { echo "no playable/ -- build the game first" >&2; exit 1; }

# The icon is a build output, like the game's own. Rebuild it if the source art is newer,
# so an icon never silently lags a re-cut emblem.
ICON="$MASTER/Contents/Resources/AppIcon.icns"
EAGLE="$ROOT/tools/sidebar_redesign/gdi_eagle/02_isolated_80x69.png"
if [ ! -f "$ICON" ] || { [ -f "$EAGLE" ] && [ "$EAGLE" -nt "$ICON" ]; }; then
    echo "rebuilding the icon"
    python3 "$ROOT/tools/launchers/make_editor_icon.py"
fi

rm -rf "$DEST"
cp -R "$MASTER" "$DEST"
chmod +x "$DEST/Contents/MacOS/editor-launch"

# macOS caches app icons hard, and a bundle replaced in place very often keeps showing
# the old one -- or a blank page -- until something tells Finder it changed. Touching the
# bundle is the cheap half of that; the icon cache reset is the half that actually works
# when it does not.
touch "$DEST"
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister \
    -f "$DEST" >/dev/null 2>&1 || true

echo "installed $DEST"
echo
echo "Double-click playable/Editor.app. It picks the newest cnc_eyes build by itself,"
echo "asks which map, and logs to playable/Editor.log."
