#!/bin/sh
# Make a macOS build folder SELF-CONTAINED: put the SDL libraries beside the binaries and
# make the binaries load them from THERE rather than from this machine's Homebrew.
#
#   tools/bundle-sdl.sh <folder>                 fix every Mach-O in <folder>
#   tools/bundle-sdl.sh <folder> cnc3d cnc_eyes  fix just these
#
# WHY THIS EXISTS. True of every macOS release cut before this.
#
# The linker records the ABSOLUTE path of every dylib it links against. `sdl2-config`
# can point at Intel Homebrew, in which case both binaries carry
#
#     /usr/local/opt/sdl2-compat/lib/libSDL2-2.0.0.dylib
#
# as a hard LC_LOAD_DYLIB, there was no LC_RPATH to redirect it, and the dylib was not in
# the shipped folder. Anyone who downloaded a release and did not happen to have Intel
# Homebrew sdl2-compat installed at exactly that path died in dyld before main() ran. On
# Apple Silicon the Homebrew prefix is /opt/homebrew, so even `brew install sdl2` does not
# create the path the binary asks for. That breaks the release rule's "A RELEASE IS
# PLAYABLE": unzip and double-click has to work on a Mac that is not this one.
#
# TWO libraries have to travel, not one, and the second is the one that is easy to miss:
#
#  * libSDL2-2.0.0.dylib -- linked normally, fixed by rewriting the load command to
#    @executable_path. @executable_path (not @loader_path) is correct here because the
#    thing that loads it IS the main executable, and it stays correct for the .app route:
#    C&C3D.app/Contents/MacOS/cnc3d-launch is a shell script that cd's to playable/ and
#    execs ./cnc3d, so the main executable is still playable/cnc3d and @executable_path is
#    still playable/. Nothing is loaded from inside the bundle.
#
#  * libSDL3.dylib -- because what Homebrew installs as "SDL2" today is sdl2-compat, a
#    shim that implements the SDL2 API on top of SDL3 and LOADS SDL3 AT RUNTIME WITH
#    dlopen. That dependency is invisible to `otool -L`: the shim's own dependency list is
#    system frameworks only. Verified by reading the shim's string table, which holds the
#    dlopen candidate list in order:
#
#        @loader_path/libSDL3.dylib
#        @loader_path/../Frameworks/SDL3.framework/Versions/A/SDL3
#        @executable_path/libSDL3.dylib
#        @executable_path/../Frameworks/SDL3.framework/Versions/A/SDL3
#        /Library/Frameworks/SDL3.framework/Versions/A/SDL3
#        libSDL3.dylib
#
#    On a build machine the game is rescued by the LAST entry, which finds
#    /usr/local/lib/libSDL3.0.dylib. On a clean Mac that entry finds nothing, so fixing
#    only the SDL2 path would have moved the failure one library along and produced
#    exactly the same "it works here" result. The first candidate is what we satisfy: a
#    real copy named exactly `libSDL3.dylib` in the same folder as the shim. The name is
#    literal -- `libSDL3.0.dylib` is not in the list and would not be found.
#
# REAL FILES, NEVER SYMLINKS. `cp` here dereferences on purpose and the result is
# asserted below, for the same reason tools/release.sh packs the Mac folder with `cp -RL`:
# a symlink into /usr/local/Cellar archives as a link and expands on the other machine as
# a dangling pointer. That failure is silent until dyld gives up.
#
# This script is idempotent and safe to run on an already-fixed folder: a binary whose
# load command already starts with @ is left alone, and the libraries are re-copied only
# when they are absent.
set -e

FOLDER="${1:-}"
[ -n "$FOLDER" ] || { echo "usage: bundle-sdl.sh <folder> [binary ...]" >&2; exit 1; }
[ -d "$FOLDER" ] || { echo "bundle-sdl: no such folder: $FOLDER" >&2; exit 1; }
FOLDER=$(cd "$FOLDER" && pwd)
shift

# The dylib path a Mach-O actually asks for. NR>1 skips otool's header line (which is the
# file's own name and would match on a file called libSDL2-anything), and the leading-space
# test keeps us to load-command lines.
sdl_load_path() {
    otool -L "$1" 2>/dev/null | awk 'NR>1 && /^[ \t]/ && /libSDL2/ { print $1; exit }'
}

# Which files are we fixing? An explicit list when given; otherwise every top-level file in
# the folder that turns out to be a Mach-O with an SDL2 load command. The scan is what
# make-build.sh uses, because the WHOLE folder is what ships: a stray older binary left in
# playable/ would otherwise go out in the zip still pointing at Homebrew.
BINS=""
if [ "$#" -gt 0 ]; then
    for b in "$@"; do
        case "$b" in /*) p="$b" ;; *) p="$FOLDER/$b" ;; esac
        [ -f "$p" ] || { echo "bundle-sdl: no such binary: $p" >&2; exit 1; }
        BINS="$BINS $p"
    done
else
    for p in "$FOLDER"/*; do
        [ -f "$p" ] || continue
        case "$p" in *.pack|*.png|*.txt|*.cfg|*.MIX|*.command|*.sh|*.py|*.plist) continue ;; esac
        [ -n "$(sdl_load_path "$p")" ] || continue
        BINS="$BINS $p"
    done
fi

if [ -z "$BINS" ]; then
    echo "bundle-sdl: nothing in $FOLDER links SDL2; nothing to do"
    exit 0
fi

# ---------------------------------------------------------------- where SDL2 comes from
# Preference order, and the first one is the important one: whatever absolute path the
# LINKER actually recorded. That is by definition the library this binary was built
# against, so it cannot disagree with the headers it was compiled with. The rest are for
# the case where every binary has already been rewritten (a fresh play folder assembled
# from fixed binaries) and the recorded path no longer names a source.
SDL2_BASE=""
SDL2_SRC=""
for b in $BINS; do
    p=$(sdl_load_path "$b")
    [ -n "$p" ] || continue
    [ -n "$SDL2_BASE" ] || SDL2_BASE=$(basename "$p")
    case "$p" in /*) SDL2_SRC="$p"; break ;; esac
done
[ -n "$SDL2_BASE" ] || SDL2_BASE="libSDL2-2.0.0.dylib"

LIBDIRS=""
if [ -n "${CNC3D_SDL_LIBDIR:-}" ]; then LIBDIRS="$CNC3D_SDL_LIBDIR"; fi
LIBDIRS="$LIBDIRS $(sdl2-config --libs 2>/dev/null | tr ' ' '\n' | sed -n 's/^-L//p')"
LIBDIRS="$LIBDIRS /usr/local/lib /opt/homebrew/lib
         /usr/local/opt/sdl2-compat/lib /opt/homebrew/opt/sdl2-compat/lib
         /usr/local/opt/sdl2/lib /opt/homebrew/opt/sdl2/lib"

if [ -z "$SDL2_SRC" ] && [ ! -f "$FOLDER/$SDL2_BASE" ]; then
    for d in $LIBDIRS; do
        if [ -f "$d/$SDL2_BASE" ]; then SDL2_SRC="$d/$SDL2_BASE"; break; fi
    done
fi

if [ -n "$SDL2_SRC" ] && [ ! -f "$FOLDER/$SDL2_BASE" ]; then
    cp "$SDL2_SRC" "$FOLDER/$SDL2_BASE"
    chmod u+w "$FOLDER/$SDL2_BASE"
    echo "bundle-sdl: copied $SDL2_SRC"
fi
[ -f "$FOLDER/$SDL2_BASE" ] || {
    echo "bundle-sdl: cannot find $SDL2_BASE to copy into $FOLDER." >&2
    echo "            Looked in: $LIBDIRS" >&2
    echo "            Set CNC3D_SDL_LIBDIR to the folder that holds it." >&2
    exit 1
}

# ---------------------------------------------------------------- SDL3, if this is the shim
# The test is the shim's own dlopen name, read out of the file. grep rather than `strings`
# so this does not depend on which developer tools are installed. A real SDL2 (not the
# compat shim) has no such string and needs no second library, and this correctly does
# nothing in that case rather than shipping a library the build does not use.
if LC_ALL=C grep -qa 'libSDL3\.dylib' "$FOLDER/$SDL2_BASE"; then
    if [ ! -f "$FOLDER/libSDL3.dylib" ]; then
        SDL3_SRC=""
        for d in $LIBDIRS /usr/local/opt/sdl3/lib /opt/homebrew/opt/sdl3/lib; do
            for n in libSDL3.0.dylib libSDL3.dylib; do
                if [ -f "$d/$n" ]; then SDL3_SRC="$d/$n"; break; fi
            done
            if [ -n "$SDL3_SRC" ]; then break; fi
        done
        [ -n "$SDL3_SRC" ] || {
            echo "bundle-sdl: $SDL2_BASE is sdl2-compat and dlopens libSDL3.dylib at run" >&2
            echo "            time, but no SDL3 was found to ship beside it. The build" >&2
            echo "            would start on this machine and die on any other." >&2
            exit 1
        }
        # Named libSDL3.dylib on purpose: that literal string is what the shim dlopens.
        # NOT re-id'd or otherwise touched, because SDL3's Homebrew bottle is ad-hoc
        # signed and install_name_tool would invalidate that signature for no gain --
        # the shim opens it by explicit path, so its LC_ID_DYLIB is never consulted.
        cp "$SDL3_SRC" "$FOLDER/libSDL3.dylib"
        chmod u+w "$FOLDER/libSDL3.dylib"
        echo "bundle-sdl: copied $SDL3_SRC -> libSDL3.dylib (sdl2-compat dlopens it)"
    fi
    SDL3_BASE="libSDL3.dylib"
else
    SDL3_BASE=""
fi

# ---------------------------------------------------------------- rewrite the load commands
# install_name_tool invalidates any embedded code signature. These binaries are unsigned
# (measured: `codesign -dv` says "not signed at all" on both, and on the sdl2-compat
# bottle), so today this does nothing. It is here so that the day something in the chain
# arrives ad-hoc signed, the tool does not leave behind a dylib dyld refuses to load. It
# re-signs ad-hoc ONLY what was already ad-hoc signed; it does not sign anything for
# distribution, which needs a certificate and is the human's job.
resign_if_adhoc() {
    codesign -dv "$1" 2>&1 | grep -q 'adhoc' || return 0
    codesign -f -s - "$1" >/dev/null 2>&1 \
        || echo "bundle-sdl: WARNING could not re-sign $1 after rewriting its paths" >&2
}

install_name_tool -id "@executable_path/$SDL2_BASE" "$FOLDER/$SDL2_BASE" 2>/dev/null || true
resign_if_adhoc "$FOLDER/$SDL2_BASE"

for b in $BINS; do
    p=$(sdl_load_path "$b")
    case "$p" in
        /*) install_name_tool -change "$p" "@executable_path/$SDL2_BASE" "$b"
            resign_if_adhoc "$b"
            echo "bundle-sdl: $(basename "$b") -> @executable_path/$SDL2_BASE" ;;
        @*) : ;;   # already bundled
    esac
done

# ---------------------------------------------------------------- prove it, do not assume it
# Three claims, each checked rather than trusted, because every one of them has a silent
# failure mode: a leftover absolute path loads from Homebrew on this machine and nowhere
# else; a symlink survives `cp -RL` as a dangling pointer; a missing SDL3 dies at the
# first SDL call on a clean Mac.
FAILED=0
for f in $BINS "$FOLDER/$SDL2_BASE"; do
    LEAK=$(otool -L "$f" 2>/dev/null | awk 'NR>1 && /^[ \t]/ && ($1 ~ /^\/usr\/local\// || $1 ~ /^\/opt\/homebrew\//) { print $1 }')
    [ -z "$LEAK" ] || { echo "bundle-sdl: FAIL $f still loads $LEAK" >&2; FAILED=1; }
done
for f in "$SDL2_BASE" $SDL3_BASE; do
    if [ ! -f "$FOLDER/$f" ]; then
        echo "bundle-sdl: FAIL $f is not in $FOLDER" >&2; FAILED=1
    fi
    if [ -L "$FOLDER/$f" ]; then
        echo "bundle-sdl: FAIL $FOLDER/$f is a symlink, not a real file" >&2; FAILED=1
    fi
done
[ "$FAILED" = "0" ] || exit 1

echo "bundle-sdl: $FOLDER is self-contained ($SDL2_BASE${SDL3_BASE:+ + $SDL3_BASE})"
