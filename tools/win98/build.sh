#!/bin/sh
# Build the Windows 98 (Tier 1) binaries from macOS.
#
# THIS IS NOT tools/win/build-win.sh. That one targets MODERN Windows and links SDL2,
# which does not exist on Win9x. This one targets Windows 98 SE on real hardware and
# links nothing but the operating system.
#
# ------------------------------------------------------------------------------
# THE ONE THING THAT MATTERS IN THIS FILE: -nodefaultlibs AND THE LIBRARY GROUP.
# ------------------------------------------------------------------------------
# Homebrew's mingw-w64 is built UCRT-only. Its libmsvcrt.a is a RELABELLED UCRT import
# library (its members are literally named lib32_libucrt_*), so an ordinary link, with
# or without -mcrtdll=msvcrt, produces a binary that imports api-ms-win-crt-*.dll.
# Windows 98 has no such DLL and says so in a dialog box, verbatim:
#
#     "A required .DLL file, API-MS-WIN-CRT-CONVERT-L1-1-0.DLL, was not found."
#
# That was measured on the real box on 19 Aug 2026, not inferred.
#
# The fix needs no new toolchain. mingw-w64 also ships libmsvcrt-os.a, which is the
# GENUINE msvcrt.dll import library, so we discard the default library set and name
# every library ourselves. The group is required because winpthreads (pulled in by
# libgcc_eh, because this GCC uses the posix threading model) calls back into the CRT
# for snprintf, _setjmp3, longjmp and _endthreadex, which are all msvcrt.dll exports.
#
# Proven on the box: a C++14 program built this way runs on Windows 98 SE with
# std::vector, std::map, std::string, std::sort, an 8 MB new[], static constructors
# and try/catch all working. See docs/win98-port.md for the transcript.
set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT="$ROOT/build/win98"
mkdir -p "$OUT"

CC=${CC:-i686-w64-mingw32-gcc}
CXX=${CXX:-i686-w64-mingw32-g++}

# -march=pentium2 is deliberately BELOW the box we test on (a Pentium III Coppermine,
# family 6 model 8 stepping 3, measured). The declared Tier 1 target is Pentium II era
# hardware, so the build must not require SSE. If we later decide SSE is allowed, that
# is a design decision with a docs/tier1-gap.md entry, not a quiet flag change.
ARCH="-march=pentium2 -mtune=pentium3"
WARN="-Wall -Wno-unused-parameter"
# __USE_MINGW_ANSI_STDIO routes printf through mingw's own implementation in libmingwex
# instead of straight to msvcrt.dll. It is not cosmetic. msvcrt.dll predates C99 and does
# not understand %zu, %lld or %a, and on Windows 98 it prints the letters instead of the
# number: the brain host's struct-size line came out as "object=zu list_hdr=zu", which is
# exactly the diagnostic you need when the engine then reports zero objects. Any project
# code with a C99 conversion in a format string is silently wrong on Win98 without this.
CFLAGS="$ARCH -O2 -g0 $WARN -DWIN98 -D_WIN32_WINNT=0x0400 -DWINVER=0x0400 -D__USE_MINGW_ANSI_STDIO=1"

# Windows 98 refuses to load a PE that claims a newer OS than itself. mingw defaults to
# subsystem 4.0 already, but the OS and image versions are set explicitly so that a
# toolchain change cannot silently raise them.
PEVER="-Wl,--major-subsystem-version,4 -Wl,--minor-subsystem-version,0 \
       -Wl,--major-os-version,4 -Wl,--minor-os-version,0 \
       -Wl,--major-image-version,4 -Wl,--minor-image-version,0"

# The library group described at the top. Order inside the group does not matter;
# the group is what lets the linker resolve the winpthreads/CRT cycle.
LIBS="-nodefaultlibs \
      -Wl,--start-group -lmingw32 -lmingwex -lmsvcrt-os -lgcc -lgcc_eh -l:libwinpthread.a -Wl,--end-group \
      -lkernel32 -luser32 -lgdi32 -lwinmm"
CXXLIBS="-nodefaultlibs \
      -Wl,--start-group -l:libstdc++.a -lmingw32 -lmingwex -lmsvcrt-os -lgcc -lgcc_eh -l:libwinpthread.a -Wl,--end-group \
      -lkernel32 -luser32 -lgdi32 -lwinmm"

# Compile with 3dfx.h's own noise filtered out, WITHOUT handing the exit status to grep.
#
# 3dfx.h ends its compiler ladder in `#warning define FX_ENTRY & FX_CALL for your
# compiler`, which t1_glide_compat.h then overrides, so it is the one message worth
# hiding. The obvious `... 2>&1 | grep -v` makes the PIPELINE's status grep's, and grep
# succeeds whenever it printed anything, so a genuine compile error would pass as a
# successful build. That is the same false-green shape as the import-table check at the
# bottom of this file, which really did happen once.
cc_quiet() {
    _e="$OUT/.ccerr"
    if "$@" 2>"$_e"; then
        grep -v "FX_ENTRY" "$_e" >&2 || true
        rm -f "$_e"
        return 0
    fi
    cat "$_e" >&2
    rm -f "$_e"
    return 1
}

echo "== Windows 98 Tier 1 build =="
echo "   cc     : $($CC -dumpmachine) $($CC -dumpversion)"
echo "   out    : $OUT"

# ---------------------------------------------------------------------------
# w98proto: the first native Win98 prototype. Pure software rendering, no GL,
# no Glide, no SDL. It links the project's OWN dosbar.c unedited, which is the
# whole point: this is CNC3D code, not a demo written to look like it.
# ---------------------------------------------------------------------------
SRC="$ROOT/tier1/w98_gfx.c $ROOT/tier1/softras.c $ROOT/tier1/t1_draw.c $ROOT/tier1/w98_proto.c $ROOT/game/dosbar.c"
INC="-I$ROOT/tier1 -I$ROOT/game"

for f in $SRC; do
    [ -f "$f" ] || { echo "MISSING SOURCE: $f" >&2; exit 1; }
done

# dosbar.c is C89 in the Mac build (game/build.sh:28) and stays C89 here. If the two
# ever disagree the sidebar is being compiled two different ways, which is exactly the
# class of drift tools/win/check-sources.sh exists to catch on the modern Windows build.
# tier1/ is gnu89 rather than c89 because it is Win32-specific by definition and uses
# two GCC extensions on purpose: __int64 for the cycle counter and an __asm__ block for
# RDTSC. game/dosbar.c stays strict c89, the same as the Mac build compiles it.
OBJ=""
for f in $SRC; do
    o="$OUT/$(basename "$f" .c).o"
    case "$f" in
        */game/*) STD=c89 ;;
        *)        STD=gnu89 ;;
    esac
    $CC -std=$STD $CFLAGS $INC -c "$f" -o "$o"
    OBJ="$OBJ $o"
done

$CC $CFLAGS -o "$OUT/w98proto.exe" $OBJ $PEVER $LIBS -mwindows
echo "   built  : $OUT/w98proto.exe ($(wc -c < "$OUT/w98proto.exe" | tr -d ' ') bytes)"

# ---------------------------------------------------------------------------
# cncbrain.exe: the headless brain host, on Windows 98.
#
# brain/host/cnc_host.c is dependency-free C whose Win32 LoadLibrary branch was written
# in advance "so the same file builds for the Win98 target" (its own comment). It is
# compiled here UNEDITED, the same way game/dosbar.c is: this branch reuses project code,
# it does not fork it. It answers the question that matters most after the renderer,
# which is whether the actual Command & Conquer simulation runs on this machine at all,
# and it answers it with no graphics in the way.
# ---------------------------------------------------------------------------
# gnu99, not gnu89: cnc_host.c declares loop variables in for statements and uses
# _Static_assert to re-check its byte-exact mirrors of the brain's structs. It is
# compiled unedited, so the build bends and the source does not.
$CC -std=gnu99 $CFLAGS -I"$ROOT/brain/host" -c "$ROOT/brain/host/cnc_host.c" -o "$OUT/cnc_host.o"
$CC $CFLAGS -o "$OUT/cncbrain.exe" "$OUT/cnc_host.o" $PEVER $LIBS
echo "   built  : $OUT/cncbrain.exe ($(wc -c < "$OUT/cncbrain.exe" | tr -d ' ') bytes)"

# ---------------------------------------------------------------------------
# w98game.exe: the actual game. Brain plus renderer plus the real sidebar.
#
# The program icon is built from the game's OWN 1995 cameo art, straight out of
# dossidebar.pack, so the thing on the Windows 98 desktop is made of the same pixels as
# the thing it launches. Regenerate with tools/win98/mkicon.py; it is a build product and
# so is not committed.
# ---------------------------------------------------------------------------
ICONRES=""
if [ -f "$OUT/cnc3d.ico" ]; then
    printf '1 ICON "%s"\n' "$OUT/cnc3d.ico" > "$OUT/icon.rc"
    i686-w64-mingw32-windres "$OUT/icon.rc" -O coff -o "$OUT/icon.res"
    ICONRES="$OUT/icon.res"
else
    echo "   note   : no $OUT/cnc3d.ico, building without a program icon"
    echo "            (tools/win98/mkicon.py <dossidebar.pack> $OUT/cnc3d.ico)"
fi
$CC -std=gnu89 $CFLAGS -I"$ROOT/tier1" -c "$ROOT/tier1/w98_brain.c" -o "$OUT/w98_brain.o"
$CC -std=gnu89 $CFLAGS -I"$ROOT/tier1" -c "$ROOT/tier1/t1_terrain.c" -o "$OUT/t1_terrain.o"
$CC -std=gnu89 $CFLAGS -I"$ROOT/tier1" -c "$ROOT/tier1/t1_mesh.c" -o "$OUT/t1_mesh.o"
$CC -std=gnu89 $CFLAGS -I"$ROOT/tier1" -I"$ROOT/game" -c "$ROOT/tier1/w98_game.c" -o "$OUT/w98_game.o"
$CC $CFLAGS -o "$OUT/w98game.exe" "$OUT/w98_game.o" "$OUT/w98_brain.o" "$OUT/w98_gfx.o" \
    "$OUT/softras.o" "$OUT/t1_terrain.o" "$OUT/t1_mesh.o" "$OUT/t1_draw.o" \
    "$OUT/dosbar.o" $ICONRES $PEVER $LIBS -mwindows
echo "   built  : $OUT/w98game.exe ($(wc -c < "$OUT/w98game.exe" | tr -d ' ') bytes)"

# ---------------------------------------------------------------------------
# w98braintest.exe: the brain bridge, with no renderer in the way.
#
# It answers "is the world actually there" on the real machine, so that when the
# renderer later draws nothing it is obvious which half is at fault.
# ---------------------------------------------------------------------------
$CC -std=gnu89 $CFLAGS -I"$ROOT/tier1" -c "$ROOT/tier1/w98_braintest.c" -o "$OUT/w98_braintest.o"
$CC $CFLAGS -o "$OUT/w98braintest.exe" "$OUT/w98_brain.o" "$OUT/w98_braintest.o" $PEVER $LIBS
echo "   built  : $OUT/w98braintest.exe ($(wc -c < "$OUT/w98braintest.exe" | tr -d ' ') bytes)"

# ---------------------------------------------------------------------------
# w98glidetest.exe: proof of life on the 3dfx Voodoo 2, and the first framerate
# number off the hardware. Built only when the Glide SDK is present, because it is
# the one thing here that needs a third-party SDK.
#
# Run tools/win98/setup-glide.sh once to assemble it: headers from sezero/glide, and
# an import library built with dlltool from the REAL glide2x.dll off the Voodoo 2 machine, so we
# link against exactly the entry points that driver exports.
# ---------------------------------------------------------------------------
GLIDE="${CNC3D_GLIDE_SDK:-$HOME/.cnc3d-win98sdk/glide}"
if [ -f "$GLIDE/lib/libglide2x.a" ] && [ -f "$GLIDE/include/glide.h" ]; then
    cc_quiet $CC -std=gnu89 $CFLAGS -I"$GLIDE/include" -I"$ROOT/tier1" \
        -c "$ROOT/tier1/t1_glidetest.c" -o "$OUT/t1_glidetest.o"
    $CC $CFLAGS -o "$OUT/w98glidetest.exe" "$OUT/t1_glidetest.o" $PEVER \
        -L"$GLIDE/lib" -lglide2x $LIBS -mwindows
    echo "   built  : $OUT/w98glidetest.exe ($(wc -c < "$OUT/w98glidetest.exe" | tr -d ' ') bytes)"
    # The real thing on the card: same scene assembly, same camera, triangles to the
    # Voodoo. t1_draw.o carries the backend seam and is linked into every target.
    cc_quiet $CC -std=gnu89 $CFLAGS -I"$GLIDE/include" -I"$ROOT/tier1" \
        -c "$ROOT/tier1/t1_glide.c" -o "$OUT/t1_glide.o"
    cc_quiet $CC -std=gnu89 $CFLAGS -I"$GLIDE/include" -I"$ROOT/tier1" -I"$ROOT/game" \
        -c "$ROOT/tier1/t1_dosinf.c" -o "$OUT/t1_dosinf.o"
    $CC -std=gnu89 $CFLAGS -I"$ROOT/tier1" -c "$ROOT/tier1/t1_script.c" -o "$OUT/t1_script.o"
    cc_quiet $CC -std=gnu89 $CFLAGS -I"$GLIDE/include" -I"$ROOT/tier1" \
        -c "$ROOT/tier1/t1_shroud.c" -o "$OUT/t1_shroud.o"
    $CC -std=gnu89 $CFLAGS -I"$ROOT/tier1" -c "$ROOT/tier1/t1_over.c" -o "$OUT/t1_over.o"
    $CC -std=gnu89 $CFLAGS -I"$ROOT/tier1" -c "$ROOT/tier1/t1_anim.c" -o "$OUT/t1_anim.o"
    cc_quiet $CC -std=gnu89 $CFLAGS -I"$GLIDE/include" -I"$ROOT/tier1" \
        -c "$ROOT/tier1/t1_place.c" -o "$OUT/t1_place.o"
    cc_quiet $CC -std=gnu89 $CFLAGS -I"$GLIDE/include" -I"$ROOT/tier1" \
        -c "$ROOT/tier1/t1_cursor.c" -o "$OUT/t1_cursor.o"
    # menu/dosmenu.c is the 1995 main menu, compiled UNEDITED exactly as game/dosbar.c and
    # game/hud640.c are. C89 like the rest of the modules it sits beside.
    $CC -std=c89 $CFLAGS -I"$ROOT/game" -I"$ROOT/menu" \
        -c "$ROOT/menu/dosmenu.c" -o "$OUT/dosmenu.o"
    cc_quiet $CC -std=gnu89 $CFLAGS -I"$GLIDE/include" -I"$ROOT/tier1" -I"$ROOT/game" -I"$ROOT/menu" \
        -c "$ROOT/tier1/t1_menu.c" -o "$OUT/t1_menu.o"
    # video/vqaplay.c is the VQA decoder: pure C89 over libc, no SDL, no GL, no threads,
    # and it STREAMS, which is what makes a 21 MB movie playable on this machine. Taken
    # unedited like the other borrowed modules. video/movieplay.c is NOT taken: SDL owns
    # its window, its clock and its signature, so tier1/t1_movie.c replaces it.
    $CC -std=c89 $CFLAGS -I"$ROOT/video" -c "$ROOT/video/vqaplay.c" -o "$OUT/vqaplay.o"
    cc_quiet $CC -std=gnu89 $CFLAGS -I"$GLIDE/include" -I"$ROOT/tier1" -I"$ROOT/video" \
        -c "$ROOT/tier1/t1_movie.c" -o "$OUT/t1_movie.o"
    cc_quiet $CC -std=gnu89 $CFLAGS -I"$GLIDE/include" -I"$ROOT/tier1" \
        -c "$ROOT/tier1/t1_tib.c" -o "$OUT/t1_tib.o"
    cc_quiet $CC -std=gnu89 $CFLAGS -I"$GLIDE/include" -I"$ROOT/tier1" -I"$ROOT/game" \
        -c "$ROOT/tier1/t1_efx.c" -o "$OUT/t1_efx.o"

    # ---- SOUND -------------------------------------------------------------------
    # The audio engine is platform-free C89 with one backend file, and audio_win98.c was
    # written for exactly this target ("SDL2 needs Windows 2000 or later; this file is the
    # whole of the difference") but had never been compiled for it, let alone run. It is
    # taken UNEDITED, like dosbar.c and hud640.c. winmm is already in $LIBS for the
    # performance counter, so waveOut costs no new dependency.
    AUDIO_SRC="sosadpcm.c wsadpcm.c wsaud.c mixfile.c sndbank.c mixer.c sfxtable.c \
               sfxname.c cncaudio.c audiotap.c audioboot.c wavio.c audio_win98.c"
    AUDIO_OBJ=""
    for f in $AUDIO_SRC; do
        $CC -std=gnu89 $CFLAGS -I"$ROOT/audio" -c "$ROOT/audio/$f" -o "$OUT/au_${f%.c}.o"
        AUDIO_OBJ="$AUDIO_OBJ $OUT/au_${f%.c}.o"
    done
    # game/hud640.c is the 640x480 sidebar, compiled UNEDITED exactly as dosbar.c is, and
    # for the same reason: this branch reuses the project's code, it does not fork it.
    # C89 like the rest of game/, so the two builds cannot be compiling it two ways.
    $CC -std=c89 $CFLAGS -I"$ROOT/game" -c "$ROOT/game/hud640.c" -o "$OUT/hud640.o"
    cc_quiet $CC -std=gnu89 $CFLAGS -I"$GLIDE/include" -I"$ROOT/tier1" -I"$ROOT/game" \
        -c "$ROOT/tier1/t1_hud.c" -o "$OUT/t1_hud.o"
    cc_quiet $CC -std=gnu89 $CFLAGS -I"$GLIDE/include" -I"$ROOT/tier1" -I"$ROOT/game" -I"$ROOT/menu" -I"$ROOT/video" -I"$ROOT/audio" \
        -c "$ROOT/tier1/w98_glidegame.c" -o "$OUT/w98_glidegame.o"
    $CC $CFLAGS -o "$OUT/w98glide.exe" "$OUT/w98_glidegame.o" "$OUT/t1_glide.o" \
        "$OUT/t1_draw.o" "$OUT/t1_terrain.o" "$OUT/t1_mesh.o" "$OUT/softras.o" \
        "$OUT/w98_brain.o" "$OUT/dosbar.o" "$OUT/hud640.o" "$OUT/t1_hud.o" \
        "$OUT/t1_script.o" "$OUT/t1_shroud.o" "$OUT/t1_over.o" "$OUT/t1_tib.o" "$OUT/t1_efx.o" "$OUT/t1_dosinf.o" "$OUT/t1_anim.o" "$OUT/t1_place.o" "$OUT/t1_cursor.o" "$OUT/t1_menu.o" "$OUT/dosmenu.o" "$OUT/t1_movie.o" "$OUT/vqaplay.o" $AUDIO_OBJ $ICONRES $PEVER -L"$GLIDE/lib" -lglide2x $LIBS -mwindows
    echo "   built  : $OUT/w98glide.exe ($(wc -c < "$OUT/w98glide.exe" | tr -d ' ') bytes)"
    GLIDEBIN="$OUT/w98glidetest.exe $OUT/w98glide.exe"
else
    echo "   note   : no Glide SDK at $GLIDE, skipping w98glidetest.exe"
    echo "            (tools/win98/setup-glide.sh)"
    GLIDEBIN=""
fi

# The import table is the acceptance test for "will this load on Win98 at all", so it
# is checked here rather than discovered on the box. Anything outside this set is a
# regression, and api-ms-win-crt-* means the CRT flags above were lost.
#
# Extracted with awk, not sed: the first field separator in objdump's output is a TAB
# and BSD sed on macOS does not understand \t in a bracket expression, so the obvious
# sed one-liner silently matches NOTHING and the check passes by finding no DLLs at
# all. That false green happened on the first run of this script.
DLLS=$(for b in "$OUT/w98proto.exe" "$OUT/w98game.exe" "$OUT/cncbrain.exe" "$OUT/w98braintest.exe" $GLIDEBIN; do
    i686-w64-mingw32-objdump -p "$b" | awk '/DLL Name:/ { print $NF }'
done | sort -u)
if [ -z "$DLLS" ]; then
    echo "REFUSING: could not read an import table out of the binary at all." >&2
    exit 1
fi
BAD=$(echo "$DLLS" | grep -viE '^(KERNEL32|USER32|GDI32|WINMM|MSVCRT|ADVAPI32|SHELL32|GLIDE2X)\.dll$' || true)
if [ -n "$BAD" ]; then
    echo "REFUSING: imports a DLL Windows 98 does not have:" >&2
    echo "$BAD" >&2
    exit 1
fi
echo "   imports: $(echo "$DLLS" | tr '\n' ' ')"
echo "   OK"
