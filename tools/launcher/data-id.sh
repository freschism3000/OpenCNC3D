#!/bin/sh
# The DATA FINGERPRINT of a staged package: one line on stdout.
#
#   tools/launcher/data-id.sh <staged-package-folder>
#
# ONE IMPLEMENTATION, CALLED FROM BOTH ENDS. The packagers write this number into
# the folder's opencnc3d-install.txt, and tools/launcher/make-manifest.sh reads it
# back out of the same file rather than recomputing it. That is deliberate: the
# launcher only takes the small 15 MB update when its own number matches the
# manifest's, so two implementations that disagreed by one file would silently
# turn every update back into a 500 MB download and nothing would report it.
#
# WHAT IT COVERS: every file in the package that the binary-only update does NOT
# carry. So it changes when the DATA changes and not when the code does, which is
# exactly the question the launcher is asking.
#
# WHAT IT NEVER COVERS: the movies and the music. No package carries them; the Windows
# installer and the launcher's Setup download them into the install afterwards, for the
# player who wants them. They are left out here by name anyway, so that a media file
# that strays into a staged folder cannot give one platform, or one package, a
# different number from the other. The same goes for the two files a media download
# leaves behind: its record, opencnc3d-media.txt, and its partial files, *.ocpart.
#
# WHY PATH AND SIZE RATHER THAN CONTENT: hashing 500 MB of packs would add minutes
# to every release for a number that only has to answer "is this the same data
# set". Every baker in this project writes whole files, so a pack that changed
# changed size or name. If that ever stops being true the failure is a full
# package download, which is correct anyway, just larger.
set -e
DIR="$1"
[ -d "$DIR" ] || { echo "usage: data-id.sh <package-folder>" >&2; exit 1; }
HERE=$(cd "$(dirname "$0")" && pwd)

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum | cut -d" " -f1
    else shasum -a 256 | cut -d" " -f1; fi
}

# THE MEDIA PATHS, from the one list the packagers read (tools/media/media-paths.txt):
# one path per line relative to the package root, "/dosdata/movies/" a folder,
# "/dosdata/SCORES.MIX" a file, '#' a comment, and the leading "/" optional, read the same
# way by every reader of the list. Red Alert's movies and score are on it. The copy here
# is the same list, for a checkout that does not have that file.
MEDIA_LIST="$HERE/../media/media-paths.txt"
if [ -f "$MEDIA_LIST" ]; then
    MEDIA=$(sed -e 's/#.*//' -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' \
                -e 's|^\./||' -e 's|^/||' "$MEDIA_LIST" | grep -v '^$' || true)
else
    MEDIA="dosdata/movies/
dosdata/music/
dosdata/SCORES.MIX
dosdata/TRANSIT.MIX
dosdata/transit/TRANSIT.MIX
movies/
ra3d/movies/
ra3d/audio/SCORES.MIX
ra3d/data/SCORES.MIX"
fi

# The exclusions ARE the binary-only zip's file list, on both platforms, plus the
# files the launcher itself writes into an install, plus the media above. Keep this
# in step with what make-build-win.sh and release.sh put in their -bins zips.
set -- \
    ! -name 'opencnc3d'      ! -name 'opencnc3d-game.exe' \
    ! -name 'cnc_eyes'   ! -name 'cnc_eyes.exe' \
    ! -name 'OpenCNC3D-Diagnostics.exe' \
    ! -name 'OpenCNC3D.exe'  ! -name 'opencnc3d-launcher' \
    ! -name 'TiberianDawn.dylib' ! -name 'TiberianDawn.dll' \
    ! -name 'SDL2.dll'   ! -name 'libSDL*.dylib' \
    ! -name 'gates.sh'   ! -name 'gate_optlayout' \
    ! -name 'opencnc3d-install.txt' ! -name 'CHANGELOG.txt' \
    ! -name 'opencnc3d-log.txt'     ! -name '.DS_Store' \
    ! -name 'opencnc3d-media.txt'   ! -name '*.ocpart' \
    ! -ipath './Logs/*' \
    ! -path './OpenCNC3D.app/Contents/MacOS/*'
# Split on lines, and never glob: a list entry is a find pattern, not a shell word.
set -f
_ifs=$IFS
IFS='
'
for p in $MEDIA; do
    case "$p" in
        */) set -- "$@" ! -path "./${p}*" ;;   # a folder: everything under it
        *)  set -- "$@" ! -path "./${p}" ;;    # one file
    esac
done
IFS=$_ifs
set +f

( cd "$DIR" && find . -type f "$@" -print0 \
  | LC_ALL=C sort -z \
  | xargs -0 -n1 sh -c 'printf "%s %s\n" "$1" "$(wc -c < "$1" | tr -d " ")"' _ ) \
  | sha256_of
