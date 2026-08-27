#!/bin/sh
# One-time setup of the Windows cross-compile toolchain on macOS.
#
# WHY THIS EXISTS AS A SCRIPT AND NOT AS INSTRUCTIONS IN A DOC.
# The Windows half of every build has to be as reproducible as the Mac half, and a doc
# that says "download SDL2 somewhere" produces a different toolchain on every machine.
# This puts the two third-party SDKs mingw-w64 does not ship (SDL2 and zlib) in one
# known place, pinned to one version, so build-win.sh can just use them.
#
# It installs OUTSIDE the repo on purpose: the SDK is binaries, project rule 2 keeps
# binaries out of git, and there can be more than one worktree of this repo checked out.
#
#   ./setup-toolchain.sh          -> ~/.cnc3d-winsdk
#   CNC3D_WINSDK=/some/where ./setup-toolchain.sh
#
# Safe to re-run: anything already unpacked is left alone.
set -e

SDK="${CNC3D_WINSDK:-$HOME/.cnc3d-winsdk}"
SDL2_VER=2.32.8
ZLIB_VER=1.3.1

# Pinned by content, not just by version. A download that silently returns something
# other than the archive is the failure this catches: zlib.net answered a CI runner with
# an 11,962-byte HTML page instead of the tarball, and the only symptom was
# "gzip: stdin: not in gzip format" three lines later, which says nothing about why.
SDL2_SHA256=f590b3707689562483ded05bf15eaf0f5d33eb97f49d0fd3b894225fe17cc52b
ZLIB_SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23

# sha256sum on Linux, shasum -a 256 on macOS. Both exist in CI and on a current macOS SDK.
sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d" " -f1
    else shasum -a 256 "$1" | cut -d" " -f1; fi
}
fetch() {   # fetch <url> <file> <expected-sha256>
    [ -f "$2" ] || curl -fL --retry 3 -o "$2" "$1"
    got=$(sha256_of "$2")
    if [ "$got" != "$3" ]; then
        echo "DOWNLOAD IS NOT WHAT IT SHOULD BE: $2" >&2
        echo "  from     $1" >&2
        echo "  expected $3" >&2
        echo "  got      $got  ($(wc -c < "$2" | tr -d " ") bytes)" >&2
        echo "  If this is a genuine upstream version bump, update the SHA above." >&2
        rm -f "$2"
        exit 1
    fi
}

# THE WINDOWS BUILD IS 32-BIT, ON PURPOSE. See BUILDING.md for the argument;
# the short version is that the Remaster DLL casts pointers through `unsigned int` in its
# save/load path, which is lossless at 32 bits and silently wrong at 64, and that the
# Tier 1 target this project has promised (Windows 98 on a Voodoo 2) has no 64-bit at
# all. x86_64 is built too, so the choice stays reversible, but i686 is what ships.
# Overridable so a machine that only has one of the two can still produce the SDK the
# shipping build needs. An MSYS2 install may carry the i686 toolchain and no
# x86_64 one; the default is unchanged, so macOS and CI still build both.
HOSTS="${CNC3D_WIN_HOSTS:-i686-w64-mingw32 x86_64-w64-mingw32}"

for h in $HOSTS; do
    command -v $h-gcc >/dev/null 2>&1 || {
        echo "no $h-gcc on PATH. Install it with:  brew install mingw-w64" >&2
        exit 1
    }
done

mkdir -p "$SDK/src"
cd "$SDK/src"

# ---- SDL2 -----------------------------------------------------------------------
# The mingw development package, which ships headers, import libs and SDL2.dll for
# both architectures. We use the x86_64 half; the i686 half is what a future Win98
# attempt would want, so it is kept rather than deleted.
if [ ! -d "$SDK/SDL2-$SDL2_VER" ]; then
    echo "fetching SDL2 $SDL2_VER (mingw devel)"
    fetch "https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VER/SDL2-devel-$SDL2_VER-mingw.tar.gz" \
          "SDL2-devel-$SDL2_VER-mingw.tar.gz" "$SDL2_SHA256"
    tar xf "SDL2-devel-$SDL2_VER-mingw.tar.gz" -C "$SDK"
fi

# ---- zlib -----------------------------------------------------------------------
# mingw-w64 does not ship zlib and the renderer links -lz for the PNG writer. Built
# from source as a static library so the .exe carries it and there is one less DLL
# beside the binary on Windows.
for HOST in $HOSTS; do
    [ -f "$SDK/lib/$HOST/libz.a" ] && continue
    echo "building zlib $ZLIB_VER for $HOST"
    # madler/zlib on GitHub, NOT zlib.net. zlib.net answered the CI runner with an HTML
    # page rather than the tarball, and the release asset is the copy that is actually
    # reachable from everywhere.
    fetch "https://github.com/madler/zlib/releases/download/v$ZLIB_VER/zlib-$ZLIB_VER.tar.gz" \
          "zlib-$ZLIB_VER.tar.gz" "$ZLIB_SHA256"
    rm -rf "zlib-$ZLIB_VER"
    tar xf "zlib-$ZLIB_VER.tar.gz"
    cd "zlib-$ZLIB_VER"
    # zlib's ./configure does not cross-compile. Its own win32 makefile does, and the
    # PREFIX variable is the documented way to point it at a cross toolchain. Only the
    # static library is asked for, so the .exe carries zlib and there is one less DLL to
    # ship beside the executable.
    make -f win32/Makefile.gcc libz.a PREFIX=$HOST- -j4
    mkdir -p "$SDK/lib/$HOST" "$SDK/include"
    cp libz.a "$SDK/lib/$HOST/"
    cp zlib.h zconf.h "$SDK/include/"
    cd ..
done

echo
echo "Windows SDK ready in $SDK"
for HOST in $HOSTS; do
    echo "  $HOST"
    echo "    SDL2   $SDK/SDL2-$SDL2_VER/$HOST"
    echo "    zlib   $SDK/lib/$HOST/libz.a"
done
