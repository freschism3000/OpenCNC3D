#!/bin/sh
# Build the Windows install wizard, on the Mac, from a staged Windows package.
#
#   tools/win/make-installer-win.sh                       stage a build, then wrap it
#   tools/win/make-installer-win.sh <folder>              wrap a folder already staged
#
# Output: ~/Desktop/CNC3D-Setup-vX.Y.Z.exe
#
# It needs NSIS, which cross builds happily from macOS:
#
#   brew install makensis
#
# WHY AN INSTALLER AT ALL, when the zip works. Three things the zip cannot do,
# and all three are things players hit:
#
#   1. A Start Menu entry and a desktop icon. A folder in Downloads is not an
#      installed game, and "where did it go" is a real support question.
#   2. An entry in Add/Remove Programs, so uninstalling is the ordinary thing
#      rather than deleting a folder and hoping.
#   3. A place to put it that is WRITABLE. The launcher updates the game in
#      place; the installer's per-user location makes that work with no UAC
#      prompt at all. See the long note at the top of installer/cnc3d.nsi.
#
# The zip does not go away. It stays for anyone who wants the folder, and it
# stays because the launcher's binary-only update is a zip unpacked over exactly
# that folder.
set -e
cd "$(dirname "$0")/../.."
ROOT=$(pwd)

command -v makensis >/dev/null 2>&1 || {
    echo "makensis is not installed, so the Windows installer cannot be built." >&2
    echo "  brew install makensis" >&2
    exit 1
}

VERSION=$(tr -d '[:space:]' < VERSION)
TAG="v$VERSION"

PAYLOAD="$1"
if [ -z "$PAYLOAD" ]; then
    echo "== staging the Windows package"
    tools/win/make-build-win.sh --no-zip >/dev/null
    PAYLOAD=$(ls -dt "$HOME/Desktop"/CNC3D-windows-* 2>/dev/null | head -1)
fi
[ -d "$PAYLOAD" ] || { echo "no staged Windows folder at: $PAYLOAD" >&2; exit 1; }

# ABSOLUTE, ALWAYS. makensis resolves a relative path against the directory of the
# .nsi FILE, not against the working directory, so a payload passed as
# `build/win-dist/CNC3D-windows-v0.6.2` is looked for under tools/win/installer/
# and the build dies on the license page with "open failed". release.sh happens to
# pass an absolute path and would never have shown this; running it by hand on a
# staged folder does, immediately.
PAYLOAD=$(cd "$PAYLOAD" && pwd)

# THE LAUNCHER MUST BE IN IT. An installer whose Start Menu shortcut points at a
# file that is not there installs a broken game and says nothing, so this is
# checked here rather than discovered on the Windows test machine.
[ -f "$PAYLOAD/C&C3D.exe" ] || {
    echo "no C&C3D.exe (the launcher) in $PAYLOAD." >&2
    echo "Run tools/win/build-launcher-win.sh and re-stage." >&2
    exit 1
}
# And it is the LAUNCHER, not the engine wearing its name. Every shortcut this
# installer creates points at that one file, so an installer built over a payload
# with the old arrangement puts a Start Menu entry and a desktop icon on the
# machine that go straight past the launcher into the game.
strings -a "$PAYLOAD/C&C3D.exe" 2>/dev/null | grep -q '/api/builds' || {
    echo "C&C3D.exe in $PAYLOAD is not the launcher: it has no update route in it." >&2
    echo "Every shortcut this installer makes points at that file." >&2
    exit 1
}

INFO="$PAYLOAD/READ-ME-WINDOWS.txt"
[ -f "$INFO" ] || INFO="$PAYLOAD/READ-ME-FIRST.txt"
[ -f "$INFO" ] || { echo "no READ-ME in $PAYLOAD to show on the info page" >&2; exit 1; }

OUTFILE="$HOME/Desktop/CNC3D-Setup-$TAG.exe"
rm -f "$OUTFILE"

echo "== makensis"
LOG=$(mktemp)
makensis -V4 \
    "-DPAYLOAD=$PAYLOAD" \
    "-DVERSION=$VERSION" \
    "-DOUTFILE=$OUTFILE" \
    "-DICONFILE=$ROOT/tools/launchers/cnc3d.ico" \
    "-DINFOFILE=$INFO" \
    tools/win/installer/cnc3d.nsi > "$LOG" 2>&1 \
    || { sed 's/^/  /' "$LOG"; rm -f "$LOG"; echo "makensis failed" >&2; exit 1; }

[ -f "$OUTFILE" ] || { rm -f "$LOG"; echo "makensis reported success and produced no file" >&2; exit 1; }

# COUNT WHAT LANDED, rather than trusting the exit status. This project has been
# bitten twice by packagers that exited 0 having quietly shipped a short set (the
# release script's own notes tell both stories), and an installer is the worst
# place for it: a missing pack does not show up until a player is three menus in.
# makensis -V4 prints one "File: ..." line per file it embeds, so the two numbers
# can simply be compared.
WANT=$(find "$PAYLOAD" -type f | wc -l | tr -d ' ')
# Only the lines that name a file and its size, IN EITHER OF THE TWO FORMS
# makensis prints. It writes
#
#     File: "cnc3d.exe" 9096562 bytes            when the file is stored
#     File: "LOGO.VQA" 0/3562630 bytes           when it went through the compressor
#
# and it also prints `File: Descending to: <dir>` / `File: Returning to: <dir>`
# while it recurses. The first version of this pattern counted the recursion
# lines, which made a correct 5-file installer report 7. The second stopped doing
# that and matched only the single-number form, which made a correct 415-file
# installer report 400 and would have failed every real release: on the small
# test payload nothing was big enough to take the compressed path, so both bugs
# hid until this ran against the actual game.
GOT=$(grep -cE '^File: "[^"]+" [0-9]+(/[0-9]+)? bytes' "$LOG" || true)
sed -n 's/^Total size:/   total size:/p' "$LOG"
rm -f "$LOG"
if [ "$GOT" != "$WANT" ]; then
    echo "the installer embeds $GOT files and the staged folder has $WANT." >&2
    echo "An installer that is missing part of the game still installs, and says nothing." >&2
    exit 1
fi
echo "   $GOT files embedded, which is every file in the staged folder"

ls -la "$OUTFILE"
echo "built $OUTFILE"
