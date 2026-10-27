#!/bin/sh
# Write the small manifest asset that rides with a release.
#
#   tools/launcher/make-manifest.sh <vX.Y.Z> <out.txt> <zip> <zip> <zip> <zip>
#
# tools/release.sh calls this with the four zips it has just built, and attaches
# the result to the GitHub release as a fifth asset. Nothing else publishes
# anywhere: opencnc3d.com's /api/builds already lists every asset of the newest
# release, so a file added here is a file the launcher can see, with no site
# change and no second host.
#
# WHY IT EXISTS AT ALL, given the site already lists the builds. /api/builds hands
# out a name, an id and a size per asset, and that is everything except the two
# things the launcher needs to be careful:
#
#   1. A CHECKSUM. Without one, a download can only be checked by its length,
#      which catches a transfer that stopped early and nothing subtler. The
#      launcher then overwrites a player's game with it.
#   2. A DATA FINGERPRINT. Without one the launcher cannot prove the 15 MB
#      binary-only package is enough for this player, so every update is the
#      500 MB one. The fingerprint is not computed here: it is READ out of each
#      full zip's own opencnc3d-install.txt, which the packagers wrote with
#      tools/launcher/data-id.sh. One implementation, quoted rather than
#      recomputed, because two that disagreed by one file would silently turn
#      every update back into a full download and nothing would report it.
#
# A release without this file still updates. It just downloads more and checks
# less, and the launcher says so. That is the fallback, and it is why nothing
# here can break a release.
set -e
cd "$(dirname "$0")/../.."

TAG="$1"
OUT="$2"
shift 2
[ -n "$TAG" ] && [ -n "$OUT" ] || {
    echo "usage: make-manifest.sh <vX.Y.Z> <out.txt> <zips...>" >&2; exit 1; }
VER=${TAG#v}

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d" " -f1
    else shasum -a 256 "$1" | cut -d" " -f1; fi
}

# Out of the FULL package zip, which is the same file the player will unpack, so
# the number in the manifest is literally the number their install will carry.
# One small file out of a 500 MB archive costs nothing: unzip seeks the central
# directory rather than streaming the whole thing.
read_data_id() {
    id=$(unzip -p "$1" '*/opencnc3d-install.txt' 2>/dev/null \
         | awk '$1=="data_id"{print $2; exit}')
    [ -n "$id" ] || {
        echo "$1 has no opencnc3d-install.txt with a data_id in it." >&2
        echo "The packagers write that file; without it this platform can only ever" >&2
        echo "be offered the whole 500 MB package." >&2
        exit 1
    }
    echo "$id"
}

MACDATA=""
WINDATA=""
TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT HUP INT TERM
{
    echo "# OpenCNC3D $TAG. Written by tools/launcher/make-manifest.sh and published"
    echo "# as a release asset, which is how the launcher sees it: opencnc3d.com's"
    echo "# /api/builds lists every asset of the newest release."
    echo "manifest 1"
    echo "version $VER"
} > "$TMP"

for z in "$@"; do
    [ -f "$z" ] || { echo "no such zip: $z" >&2; exit 1; }
    name=$(basename "$z")
    case "$name" in
        *-module-*|*-modules.*)
            echo "module assets belong in the separately verified module catalog: $name" >&2
            exit 1 ;;
    esac
    size=$(wc -c < "$z" | tr -d ' ')
    sha=$(sha256_of "$z")
    case "$name" in
        OpenCNC3DInstaller-macos-*.pkg) key=macos_installer ;;
        OpenCNC3DComplete-macos-*.pkg) key=macos_td_complete ;;
        RA3DComplete-macos-*.pkg) key=macos_ra_complete ;;
        OpenCNC3DInstaller-windows-*.exe) key=windows_installer ;;
        OpenCNC3DComplete-windows-*.exe) key=windows_td_complete ;;
        RA3DComplete-windows-*.exe) key=windows_ra_complete ;;
        RA3DBinaries-macos-*.zip) key=macos_ra_bins ;;
        RA3DBinaries-windows-*.zip) key=windows_ra_bins ;;
        OpenCNC3DBinaries-macos-*.zip) key=macos_bins ;;
        OpenCNC3DBinaries-windows-*.zip) key=windows_bins ;;
        *macos*-bins.zip)   key=macos_bins ;;
        *macos*.zip)        key=macos_full;   MACDATA=$(read_data_id "$z") ;;
        *windows*-bins.zip) key=windows_bins ;;
        *windows*.zip)      key=windows_full; WINDATA=$(read_data_id "$z") ;;
        *) echo "cannot tell what $name is; name it OpenCNC3D-<platform>-<tag>[-bins].zip" >&2
           exit 1 ;;
    esac
    # name size sha, and the launcher matches the NAME against what the site is
    # serving. Matching on the name rather than on this file's own platform key is
    # what stops a manifest that mentions a file the site does not have from
    # attaching its hash to the wrong download.
    echo "$key $name $size $sha" >> "$TMP"
    echo "   $key  $name  $size  ${sha%${sha#????????}}..."
done

[ -n "$MACDATA" ] && echo "macos_data_id $MACDATA" >> "$TMP"
[ -n "$WINDATA" ] && echo "windows_data_id $WINDATA" >> "$TMP"

# Both full packages are required, for the same reason release.sh requires both
# builds: an install whose data has moved on can only be updated by the whole
# thing, and a manifest missing one describes a release half its players cannot
# check.
for k in macos_full windows_full; do
    grep -q "^$k " "$TMP" || { echo "the manifest has no $k" >&2; exit 1; }
done

mv "$TMP" "$OUT"
echo "   data macos   ${MACDATA:-none}"
echo "   data windows ${WINDATA:-none}"
echo "wrote $OUT"
