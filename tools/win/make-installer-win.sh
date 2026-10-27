#!/bin/sh
# Build a per-user Windows installer from matching release inputs.
#   make-installer-win.sh <Windows base stage>             empty launcher
#   make-installer-win.sh <Windows base stage> td <ZIP>    Dawn3D Complete
#   make-installer-win.sh <Windows base stage> ra <ZIP>    RA3D Complete
# Complete variants embed the verified module and offer per-game media choices.
set -e
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
die() { echo "$*" >&2; exit 1; }
command -v makensis >/dev/null 2>&1 || die "Install NSIS (brew install makensis)."
VERSION=$(tr -d '[:space:]' < VERSION)
TAG="v$VERSION"
NSI="$ROOT/tools/win/installer/opencnc3d.nsi"

# A Complete installer uses its included verified ZIP. No variant downloads a
# whole game through the old bootstrap path.
grep -F -- '--install-game' "$NSI" >/dev/null && die "legacy whole-game download in Setup"
MODULE="${2:-}"
MODULE_ZIP="${3:-}"
case "$MODULE" in
    '') PREFIX=OpenCNC3DInstaller; TITLE='OpenCNC3D Installer'; GAME_TITLE='OpenCNC3D launcher' ;;
    td) PREFIX=OpenCNC3DComplete; TITLE='OpenCNC3D Complete'; GAME_TITLE=Dawn3D; MODULE_ARG=td; MEDIA_GAME=td ;;
    ra) PREFIX=RA3DComplete; TITLE='RA3D Complete'; GAME_TITLE=RA3D; MODULE_ARG=ra3d; MEDIA_GAME=ra ;;
    *) die "installer module must be td, ra or empty" ;;
esac
SETUP_STAGE="${OPENCNC3D_SETUP_STAGE:-$ROOT/build/win-setup}"
# Only our known staging directory is removed, never the input package.
SETUP_STAGE=$(python3 -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).resolve())' "$SETUP_STAGE")
case "$SETUP_STAGE" in "$ROOT/build/"*) ;; *) die "Setup staging must be private to this checkout's build/ directory" ;; esac
PKG="${1:-}"
if [ -n "$PKG" ]; then
    [ -d "$PKG" ] || die "no Windows package at $PKG"
    PKG=$(cd "$PKG" && pwd -P)
    case "$PKG" in "$SETUP_STAGE"|"$SETUP_STAGE/"*) die "Setup staging contains its own input package" ;; esac
fi
PAYLOAD="$SETUP_STAGE/payload"
[ "$SETUP_STAGE" != / ] && [ "$SETUP_STAGE" != "$ROOT" ] || die "unsafe Setup staging path"
rm -rf "$SETUP_STAGE"
mkdir -p "$PAYLOAD/ecur"
if [ -n "$PKG" ]; then
    [ -d "$PKG" ] || die "no Windows package at $PKG"
    PKG=$(cd "$PKG" && pwd)
    for f in OpenCNC3D.exe SDL2.dll READ-ME-WINDOWS.txt LICENSE NOTICE.md dosmenu.pack enhanced-menu.pack ecur/ecur.bin; do
        [ -s "$PKG/$f" ] || die "missing bootstrap input: $PKG/$f"
        cp "$PKG/$f" "$PAYLOAD/$f"
    done
else
    tools/win/build-launcher-win.sh
    cp "$ROOT/build/win/OpenCNC3D.exe" "$ROOT/build/win/SDL2.dll" "$PAYLOAD/"
    GITDESC="${OPENCNC3D_BUILD_ID:-$(git describe --always --dirty 2>/dev/null || echo untracked)}"
    sh "$ROOT/tools/win/installer/readme-windows.sh" "$PAYLOAD/READ-ME-WINDOWS.txt" "$GITDESC"
    cp "$ROOT/LICENSE" "$PAYLOAD/"
    python3 "$ROOT/tools/make-package-notice.py" "$PAYLOAD/NOTICE.md" --package-root "$PAYLOAD" --no-ra 1
    ART="${OPENCNC3D_PLAYABLE:-$ROOT/playable}"
    for f in dosmenu.pack enhanced-menu.pack ecur/ecur.bin; do
        [ -s "$ART/$f" ] || die "missing launcher art: $ART/$f; stage the matching release first"
        cp "$ART/$f" "$PAYLOAD/$f"
    done
fi
ICON="$ROOT/tools/launchers/opencnc3d.ico"
WIZARD="$ROOT/tools/win/installer/wizard.bmp"
[ -s "$ICON" ] && [ -s "$WIZARD" ] || die "missing installer artwork"
cp "$ICON" "$PAYLOAD/opencnc3d.ico"
if [ "$MODULE" = ra ]; then
    WIZARD="$SETUP_STAGE/ra-wizard.bmp"
    "${OPENCNC3D_ART_PYTHON:-python3}" "$ROOT/tools/win/installer/make-ra-wizard.py" "$WIZARD"
fi
EXTRA=$(cd "$PAYLOAD" && find . -type f | sed 's|^./||' | sort |
    grep -vxE 'OpenCNC3D\.exe|SDL2\.dll|opencnc3d\.ico|READ-ME-WINDOWS\.txt|LICENSE|NOTICE\.md|dosmenu\.pack|enhanced-menu\.pack|ecur/ecur\.bin' || true)
[ -z "$EXTRA" ] || die "non-bootstrap files in Setup: $EXTRA"
LC_ALL=C grep -aqF '/api/builds' "$PAYLOAD/OpenCNC3D.exe" || die "OpenCNC3D.exe is not the launcher"
OUTDIR="${OPENCNC3D_WIN_ZIP:-$HOME/Desktop}"
mkdir -p "$OUTDIR"
OUTFILE="$(cd "$OUTDIR" && pwd)/$PREFIX-windows-$TAG.exe"
rm -f "$OUTFILE"
LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT
set -- -V4 "-DPAYLOAD=$PAYLOAD" "-DVERSION=$VERSION" "-DOUTFILE=$OUTFILE" \
    "-DICONFILE=$ICON" "-DWIZARDBMP=$WIZARD" "-DPACKAGE_TITLE=$TITLE" "-DGAME_TITLE=$GAME_TITLE"
if [ -n "$MODULE" ]; then
    [ -s "$MODULE_ZIP" ] || die "Complete installer requires its matching module ZIP"
    MODULE_ZIP="$(cd "$(dirname "$MODULE_ZIP")" && pwd -P)/$(basename "$MODULE_ZIP")"
    # Verify the actual producer manifest and every payload entry before embedding.
    VALUES=$(python3 - "$ROOT" "$MODULE_ZIP" "$MODULE" "$VERSION" <<'PY'
import hashlib,importlib.util,sys,zipfile
from pathlib import Path
spec=importlib.util.spec_from_file_location('modules',Path(sys.argv[1])/'tools/launcher/package-modules.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
p=Path(sys.argv[2]);m.verify_archive(p,sys.argv[3],'windows',sys.argv[4])
with zipfile.ZipFile(p) as z: size=sum(i.file_size for i in z.infolist())
print(m.digest(p), (size+p.stat().st_size+1023)//1024)
PY
    ) || die "Complete module ZIP failed verification"
    MODULE_SHA=${VALUES%% *}; MODULE_KB=${VALUES#* }
    set -- "$@" "-DMODULE=$MODULE_ARG" "-DMEDIA_GAME=$MEDIA_GAME" \
        "-DMODULE_ZIP=$MODULE_ZIP" "-DMODULE_SHA=$MODULE_SHA" "-DMODULE_KB=$MODULE_KB"
fi
makensis "$@" "$NSI" > "$LOG" 2>&1 || {
    cat "$LOG" >&2; die "makensis failed"; }
[ -s "$OUTFILE" ] || die "makensis left no installer"
WANT=$(find "$PAYLOAD" -type f | wc -l | tr -d ' ')
[ -z "$MODULE" ] || WANT=$((WANT + 1))
GOT=$(grep -cE '^File: .+ [0-9]+(/[0-9]+)? bytes' "$LOG" || true)
[ "$GOT" = "$WANT" ] || { rm -f "$OUTFILE"; die "Setup embedded $GOT of $WANT bootstrap files"; }
if [ -z "$MODULE" ]; then
    grep -E '^ExecWait: .*--(install-module|media-install)' "$LOG" >/dev/null && die "empty Setup contains a game/media worker"
    echo "SETUP|empty_launcher=1|automatic_download=0|files=$GOT|bytes=$(wc -c < "$OUTFILE" | tr -d ' ')"
else
    echo "SETUP|module=$MODULE|embedded_verified=1|media_checkboxes=2|files=$GOT|bytes=$(wc -c < "$OUTFILE" | tr -d ' ')"
fi
echo "built $OUTFILE"
