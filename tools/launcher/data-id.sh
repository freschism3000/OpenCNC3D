#!/bin/sh
# The DATA FINGERPRINT of a staged package: one line on stdout.
#
#   tools/launcher/data-id.sh <staged-package-folder>
#
# ONE IMPLEMENTATION, CALLED FROM BOTH ENDS. The packagers write this number into
# the folder's cnc3d-install.txt, and tools/launcher/make-manifest.sh reads it
# back out of the same file rather than recomputing it. That is deliberate: the
# launcher only takes the small 15 MB update when its own number matches the
# manifest's, so two implementations that disagreed by one file would silently
# turn every update back into a 500 MB download and nothing would report it.
#
# WHAT IT COVERS: every file in the package that the binary-only update does NOT
# carry. So it changes when the DATA changes and not when the code does, which is
# exactly the question the launcher is asking.
#
# WHY PATH AND SIZE RATHER THAN CONTENT: hashing 500 MB of packs would add minutes
# to every release for a number that only has to answer "is this the same data
# set". Every baker in this project writes whole files, so a pack that changed
# changed size or name. If that ever stops being true the failure is a full
# package download, which is correct anyway, just larger.
set -e
DIR="$1"
[ -d "$DIR" ] || { echo "usage: data-id.sh <package-folder>" >&2; exit 1; }

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum | cut -d" " -f1
    else shasum -a 256 | cut -d" " -f1; fi
}

# The exclusions ARE the binary-only zip's file list, on both platforms, plus the
# two files the launcher itself writes into an install. Keep this in step with
# what make-build-win.sh and release.sh put in their -bins zips.
( cd "$DIR" && find . -type f \
    ! -name 'cnc3d'      ! -name 'cnc3d.exe' \
    ! -name 'cnc_eyes'   ! -name 'cnc_eyes.exe' \
    ! -name 'C&C3D.exe'  ! -name 'cnc3d-launcher' \
    ! -name 'TiberianDawn.dylib' ! -name 'TiberianDawn.dll' \
    ! -name 'SDL2.dll'   ! -name 'libSDL*.dylib' \
    ! -name 'gates.sh'   ! -name 'gate_optlayout' \
    ! -name 'cnc3d-install.txt' ! -name 'CHANGELOG.txt' \
    ! -name 'cnc3d-log.txt'     ! -name '.DS_Store' \
    ! -path './C&C3D.app/Contents/MacOS/*' \
    -print0 \
  | LC_ALL=C sort -z \
  | xargs -0 -n1 sh -c 'printf "%s %s\n" "$1" "$(wc -c < "$1" | tr -d " ")"' _ ) \
  | sha256_of
