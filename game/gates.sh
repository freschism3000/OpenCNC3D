#!/bin/bash
# EVERY verification gate for CNC3D, in one script.
#
#   ./gates.sh <label>          runs all of them, writes ../gates/<label>.log
#
# It expects to be run from a RUNTIME FOLDER: the binaries (cnc_eyes, cnc3d), the
# packs, missions/, content/, dosdata/ and the gate scripts from game/*.txt all in
# one place. Copy this file and the gate_*.txt next to a built play folder, or point
# RUNDIR at one.
#
# G1..G6 are the gates that predate the sound round and must never regress:
# picktest both cameras, the SCG01EB sidebar build gate, self-play, two-run --shot
# determinism, the gunboat FACEWATCH digest, and the app menu round trip.
# G7..G13 came with the sound, the movies and the pause dialog.
#
# G5 is a byte-exact regression test against a STORED reference (../gates/facewatch.sha,
# created on first run): if a change alters the simulation at all, it fails.
#
# G4 is NOT the same thing, and this header used to say it was. It compares two runs of
# the SAME binary, so it proves reproducibility and cannot see a change of look. Apart
# from G23's verdict art the suite holds no stored pixel reference at all, which means
# none of wave 4's or wave 5's visual work has picture coverage. Recorded as a known
# gap rather than left as a false claim here.
# Resolve this script's own directory BEFORE the cd, or every later reference to a
# sibling script (G27's gate_campaign.sh) resolves against RUNDIR instead and vanishes.
GATEDIR="$(cd "$(dirname "$0")" && pwd)"
# Absolutise RUNDIR before the cd, and export it, because two gates resolve it a SECOND
# time inside a sibling script: G27's gate_campaign.sh and G30's gate_fresh.sh. Given a
# relative RUNDIR they resolve it against a working directory that is already the run
# folder, so "run/cnc3d" becomes "run/run/cnc3d" and G30 reports the deployed binaries
# MISSING and STALE when both are present and current. That is a false red on the one
# gate whose whole job is to be believed about staleness, so it is worth three lines.
RUNDIR="${RUNDIR:-$GATEDIR/../run}"
case "$RUNDIR" in /*) ;; *) RUNDIR="$PWD/$RUNDIR" ;; esac
export RUNDIR
cd "$RUNDIR"
LABEL="${1:-run}"
OUT="../gates/$LABEL.log"
: > "$OUT"
BASE="--cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack --dylib TiberianDawn.dylib --dir missions/ --content content/"
PASS=0; FAIL=0
# EVERY VERDICT GOES INTO THE LOG AS WELL AS ONTO THE TERMINAL, and it did not used to.
# These two lines echoed to stdout only. The gates write their own log ($OUT) as they go,
# and tools/release.sh keeps that file as the record of the build, so gates/v0.5.7.log is
# 1.4 MB of raw binary output with ZERO "PASS"/"FAIL" lines in it: the aggregate footer at
# the very bottom was the entire per-build record, and not one measured quantity survived.
# A log that cannot say which gate reported what, or what number it read, is not evidence,
# and this project's rule 7 is honesty over green lights.
# tee rather than a second echo, so there is one line in one order and the two copies
# cannot drift. The tally and the exit status are untouched: the increment runs in this
# shell, not in the pipeline, and tools/release.sh still reads the footer and $?.
ok()  { echo "PASS  $1" | tee -a "$OUT"; PASS=$((PASS+1)); }
bad() { echo "FAIL  $1" | tee -a "$OUT"; FAIL=$((FAIL+1)); }

# =====================================================================================
# THE STALE-SHOT WRAPPER. Every gate that measures a PNG must go through this.
#
# THE DEFECT IT CLOSES. shots/ persists between suite runs. A gate that runs the binary,
# does not delete the pictures it is about to write, and does not read the run's exit
# status will happily measure the PREVIOUS run's pictures. A build that cannot open its
# pack dies at startup, writes nothing, and the gate reports a confident green. This is
# not hypothetical: a stub cnc_eyes that rendered nothing and exited 1 was
# put against a stale shots/ folder and scored 10 of 12 assertions PASS across six gates,
# G36c cheerfully reporting "5 --shot runs, 1 distinct digest" with zero shots taken. The
# control run and its output are as a known gap.
#
# BOTH HALVES ARE NEEDED and neither substitutes for the other. The rm proves the file
# standing under that name belongs to THIS run; the exit status proves the run intended to
# write it. A "[ -s file ]" emptiness test closes neither on its own, because a stale file
# is not empty -- five of the six trapped gates already had one and it saved none of them.
#
# WHY A WRAPPER RATHER THAN THE SAME THREE LINES AGAIN. an earlier note recorded the
# structural half of this: there was no shared runner, so every new gate was written by
# copying a neighbour and whether the trap came with it depended on which neighbour. That
# is how the fault was reintroduced after it had been cleared once. Seven gates were fixed
# by hand on 20 Aug and six more were still carrying it the next morning.
#
#   gbegin shots/a.png shots/b.png ...   BEFORE the runs: clears GRC and deletes them
#   grun <log|-> <cnc_eyes args...>      one run; "-" appends to $OUT, else to that log
#   gshots shots/a.png ...               assert this run actually wrote them
#   [ "$GRC" != "0" ] -> bad ...         the FIRST branch of the verdict, always
#
# GRC accumulates and is cleared only by gbegin, so a gate with ten runs needs ten grun
# calls and ONE test instead of ten status variables. A block that forgets gbegin inherits
# the previous gate's GRC and fails loudly, which is the safe direction to fail in.
#
# POSIX only (no local, no arrays): the suite is run as "sh gates.sh" as often as it is
# executed directly, and the ge_ prefix is what stands in for "local" here.
# =====================================================================================
GRC=0
gbegin() { GRC=0; rm -f "$@"; }
grun() {
    ge_log="$1"; shift
    if [ "$ge_log" = "-" ]; then
        ./cnc_eyes "$@" >>"$OUT" 2>&1; ge_rc=$?
    else
        ./cnc_eyes "$@" >"$ge_log" 2>&1; ge_rc=$?
        cat "$ge_log" >>"$OUT"
    fi
    [ "$ge_rc" = "0" ] || GRC="$ge_rc"
    return "$ge_rc"
}
gshots() {
    for ge_f in "$@"; do
        [ -s "$ge_f" ] && continue
        echo "gates: $ge_f was not written by this run" >>"$OUT"
        GRC=91
        return 1
    done
    return 0
}

echo "===== gates $LABEL $(date '+%F %H:%M:%S') =====" >> "$OUT"

# G1 --picktest, both camera modes in one run
L=$(./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE --picktest 2>&1 | tee -a "$OUT" | grep "UNPROJ| overall")
case "$L" in
  *"N64 PASS, OLD PASS -> PASS") ok "G1 picktest both cameras" ;;
  *) bad "G1 picktest [$L]" ;;
esac

# G2 SCG01EB sidebar entries > 0
S=$(./cnc_eyes --scen SCG01EB --pack SCG01EA.pack $BASE --script gate_eb.txt 2>&1 | tee -a "$OUT" | grep -E "^SIDEBAR\|GATE")
N=$(echo "$S" | sed -n 's/.*entries=\([0-9]*\).*/\1/p')
if [ -n "$N" ] && [ "$N" -gt 0 ]; then ok "G2 sidebar entries=$N"; else bad "G2 sidebar [$S]"; fi

# G3 self-play, SCG01EC and SCG90EA
for pair in "SCG01EC selfplay.txt" "SCG90EA gate_90.txt"; do
  set -- $pair
  R=$(./cnc_eyes --scen $1 --pack SCG01EA.pack $BASE --script $2 2>&1 | tee -a "$OUT" | grep -E "^SCRIPT\|end")
  case "$R" in
    *"0 failures") ok "G3 self-play $1 [$R]" ;;
    *) bad "G3 self-play $1 [$R]" ;;
  esac
done

# G4 two-run --shot REPRODUCIBILITY. Note what this is and is not: it shoots the same
# frame twice from the SAME binary and compares the two to each other, so it catches
# wallclock, uninitialised memory and iteration-order nondeterminism. It is NOT a picture
# regression test and cannot see a change of look -- a commit that alters every pixel
# leaves both halves equal. The header above used to claim otherwise. The suite's only
# stored pixel reference is G23's verdict art; left open deliberately.
#
# The artefacts are DELETED first and both exit statuses are checked. They used to be
# neither, and shots/ persists between runs, so a binary that could not open its pack
# died at startup and this gate happily re-measured the PREVIOUS run's PNGs and passed.
rm -f shots/det_a.png shots/det_b.png
./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE --shot shots/det_a.png --ticks 120 >>"$OUT" 2>&1
RA=$?
./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE --shot shots/det_b.png --ticks 120 >>"$OUT" 2>&1
RB=$?
if [ "$RA" != "0" ] || [ "$RB" != "0" ]; then
  bad "G4 reproducibility: the run itself failed (exit $RA / $RB)"
elif [ ! -s shots/det_a.png ] || [ ! -s shots/det_b.png ]; then
  bad "G4 reproducibility: no shot was written"
else
  A=$(shasum shots/det_a.png | cut -d' ' -f1); B=$(shasum shots/det_b.png | cut -d' ' -f1)
  if [ "$A" = "$B" ]; then ok "G4 two-run reproducibility ${A:0:12}"
  else bad "G4 reproducibility $A vs $B"; fi
fi

# G5 gunboat matrix: the whole FACEWATCH stream must be identical to the baseline,
# and the script must report 0 failures. A hull that spins where it did not before
# changes the digest.
# TSTBOAT, NOT SCG10EA. This fixture used to be called SCG10EA, which is a REAL GDI
# scenario name, and the real SCG10EA arrived with the other 32 campaign
# missions and overwrote it. A purpose-built test map must never squat on a shipping
# scenario's name; the flume is now TSTBOAT and the campaign owns SCG10EA. The PACK is
# still the real SCG10EA.pack: the flume needs a temperate map with water, not that
# map's cell layout, and the gate passes on it unchanged.
G=$(./cnc_eyes --scen TSTBOAT --pack SCG10EA.pack $BASE --script gate_gunboat.txt 2>&1 | tee -a "$OUT")
FW=$(echo "$G" | grep -E "^FACEWATCH\|" | shasum | cut -d' ' -f1)
FN=$(echo "$G" | grep -cE "^FACEWATCH\|")
TURNS=$(echo "$G" | grep -E "^FACEWATCH\|" | grep -cv "dF=+0")
E5=$(echo "$G" | grep -E "^SCRIPT\|end")
REF=../gates/facewatch.sha
[ -f "$REF" ] || echo "$FW" > "$REF"
if [ "$FW" = "$(cat "$REF")" ] && [ "${E5}" != "${E5%0 failures}" ]; then
  ok "G5 gunboat $FN FACEWATCH lines, $TURNS hull turns, digest ${FW:0:12}"
else
  bad "G5 gunboat digest ${FW:0:12} vs $(cut -c1-12 "$REF") [$E5]"
fi

# G6 app: LOGO, INTRO2, then menu -> Test Map -> menu, twice in one process.
# The pristine menu digest (HARNESS|menu0) is taken BEFORE the movies run, so every
# later menu frame -- after the movies, after each mission -- must match a reference
# nothing had a chance to poison. Movie-induced GL corruption now fails this gate;
# the old order baked such damage into the reference and could not see it.
rm -rf shots/h; mkdir -p shots/h
H=$(./cnc3d --scen SCG01EC --pack SCG01EA.pack $BASE --menupack dosmenu.pack \
      --harness 120 --rounds 2 --shotdir shots/h 2>>"$OUT")
echo "$H" >> "$OUT"
E=$(echo "$H" | grep -E "^HARNESS\|end")
M0=$(echo "$H" | grep -c "^HARNESS|menu0")
# The harness prints its own failure COUNT on the end line, and this gate used to
# ignore it: a run that reported "2 failure(s)" still passed, because only an explicit
# HARNESS|FAIL line was looked for. That is exactly the "nothing objected" trap this
# project keeps falling into, so the count is now read and asserted.
NF=$(echo "$E" | sed -n 's/.*[,|] *\([0-9][0-9]*\) failure(s).*/\1/p')
# G6 HAS GONE RED UNDER LOAD WITH NOTHING WRONG, and was undiagnosable, because the gate
# printed only the end line and the harness's own reasons went to the log nobody read.
# It has exactly one assertion whose answer depends on the machine rather than on the
# build: the harness fails a round whose resident set grew more than RSS_GROWTH_LIMIT_KIB
# (30 MB in app/cnc3d.cpp) over the previous round, and RSS on a box that is also
# compiling is not the number it is on an idle one. That is not a licence to widen the
# limit, which guards a real known leak. It is a reason to make the failure SAY so, so
# the next occurrence is attributable instead of vanishing on a re-run.
G6WHY=$(echo "$H" | grep "^HARNESS|FAIL" | head -3 | tr '\n' ' ')
G6RSS=$(echo "$H" | grep "^HARNESS|rss" | grep "growth" | tr '\n' ' ')
if echo "$H" | grep -q "HARNESS|FAIL"; then bad "G6 app round trip: ${G6WHY:-no reason line} [$E] ${G6RSS}"
elif [ "$M0" != "1" ]; then bad "G6 app round trip: no pre-movie pristine digest was taken"
elif [ -z "$NF" ]; then bad "G6 app round trip: no failure count on the end line [$E]"
elif [ "$NF" != "0" ]; then bad "G6 app round trip: $NF harness failure(s) ${G6WHY:-no reason line} [$E] ${G6RSS}"
else ok "G6 app round trip, digest anchored pre-movie [$E]"; fi

# G7 headless paths must not need an audio device.
#
# THE OLD FORM OF THIS GATE COULD NOT GO RED. It set SDL_AUDIODRIVER to a bogus value and
# ran three modes that force `ab.silent` true (cnc_eyes.cpp: nosound || picktest || shot ||
# script), and the audio subsystem is only ever initialised on the NON-silent path, in
# audio_sdl.c. So the environment variable never reached a single SDL call, and the
# regression the gate describes could not fail it either: audioboot.h documents that a
# refused device is never fatal, so even if a mode stopped being silent the run would
# carry on and still write its shot.
#
# It now has a fourth leg that actually asks for a device with the bogus driver, and
# asserts BOTH that the device refused AND that the run still completed and wrote its
# output. The three original legs stay, and now assert the silent marker explicitly
# rather than inferring it from an exit code.
rm -f shots/na.png shots/na4.png
NA=$(SDL_AUDIODRIVER=nonexistentdriver ./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE \
       --shot shots/na.png --ticks 60 2>&1 | tee -a "$OUT" | grep -cE "shot: wrote .* -> ok")
NS=$(SDL_AUDIODRIVER=nonexistentdriver ./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE \
       --script selfplay.txt 2>&1 | tee -a "$OUT" | grep -E "^SCRIPT\|end")
NP=$(SDL_AUDIODRIVER=nonexistentdriver ./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE \
       --picktest 2>&1 | tee -a "$OUT" | grep -c "N64 PASS, OLD PASS -> PASS")
# The leg that exercises the seam the gate is named after: ask for a real device with a
# driver that cannot exist, and require the run to survive it and still produce a picture.
NDLOG=shots/na4.log
SDL_AUDIODRIVER=nonexistentdriver ./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE \
       --shot shots/na4.png --ticks 40 --musicvol 255 --soundvol 255 > "$NDLOG" 2>&1
ND=$?
cat "$NDLOG" >> "$OUT"
NDSHOT=0; [ -s shots/na4.png ] && NDSHOT=1
if [ "$NA" -ge 1 ] && [ "$NP" -ge 1 ] && [ "${NS}" != "${NS%0 failures}" ] \
   && [ "$ND" = "0" ] && [ "$NDSHOT" = "1" ]; then
  ok "G7 headless with no audio driver (shot, script, picktest, and a device-requesting run that survived it)"
else
  bad "G7 headless with no audio driver: shot=$NA picktest=$NP script=[$NS] device-leg exit=$ND wrote=$NDSHOT"
fi

# G8 the sounds themselves: combat must ask for them, the disc must have them, and
# the recorded mix must contain real signal.
S8=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --script gate_90.txt \
       --audiowav shots/g_combat.wav --dumpsound 2>>"$OUT")
echo "$S8" >> "$OUT"
NSFX=$(echo "$S8" | grep -c "^SOUND|sfx")
NEVA=$(echo "$S8" | grep -c "^SOUND|eva")
NSIL=$(echo "$S8" | grep -c "SILENT")
TAP=$(echo "$S8" | grep "^AUDIOTAP|")
RMS=$(echo "$TAP" | sed -n 's/.*rms=\([0-9.]*\).*/\1/p')
if [ "$NSFX" -gt 20 ] && [ "$NEVA" -gt 0 ] && [ "$NSIL" = "0" ] && \
   [ "${RMS%%.*}" -gt 200 ] 2>/dev/null; then
  ok "G8 combat sound: $NSFX effects, $NEVA EVA, 0 silent, mix rms $RMS"
else
  bad "G8 combat sound: sfx=$NSFX eva=$NEVA silent=$NSIL rms=$RMS"
fi

# G9 the 1995 placement rule, through the real game path: the same fight heard from
# the west must pan right and from the east must pan left.
PW=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --w 640 --h 360 --oldcam --zoom 40 \
       --script gate_pan.txt --nosound --dumpsound 2>>"$OUT" | grep -c "pan=1000")
PE=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --w 640 --h 360 --oldcam --zoom 40 \
       --script gate_pan_e.txt --nosound --dumpsound 2>>"$OUT" | grep -c "pan=-")
if [ "$PW" -gt 0 ] && [ "$PE" -gt 0 ]; then
  ok "G9 pan mirrors the camera: $PW hard-right from the west, $PE left from the east"
else
  bad "G9 pan: west right=$PW east left=$PE"
fi

# G10 the score: a mission opens on AOI, the playlist advances at the end of a track,
# and the two 1995 volume sliders each control their own bus.
# --musicvol 255 is REQUIRED here and is not decoration: a --script run is silent by
# default (see the note in cnc_eyes.cpp where script implies hidden), and this gate is one
# of the few that measures the score rather than merely tolerating it.
M1=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --script gate_playlist.txt \
       --theme IND2 --musicvol 255 --audiowav shots/g_playlist.wav 2>>"$OUT" | grep "^MUSIC|play")
echo "$M1" >> "$OUT"
NTRACK=$(echo "$M1" | grep -c "^MUSIC|play")
# THE SAME TRAP, ON AN ARTEFACT THAT IS NOT A PICTURE, and this one is sharper than any
# of the PNG cases: a stale shots/g_mute.wav can only have been written by a previous
# MUTED run, so its peak is 0 and "both sliders at 0 gives peak 0" passes on a run that
# never happened. The existence/size guard inside the python catches a file that was never
# written at all, which is exactly the case a persisting shots/ removes.
gbegin shots/g_mute.wav
grun - --scen SCG90EA --pack SCG01EA.pack $BASE --script gate_90.txt \
   --musicvol 0 --soundvol 0 --audiowav shots/g_mute.wav
gshots shots/g_mute.wav
MUTE=$(python3 - <<'PY'
import wave, numpy as np
import os, sys
if not os.path.exists("shots/g_mute.wav") or os.path.getsize("shots/g_mute.wav") == 0:
    print("G10MISSING|shots/g_mute.wav"); sys.exit(1)
w=wave.open("shots/g_mute.wav")
a=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2')
print(int(np.abs(a.astype(int)).max()))
PY
)
if [ "$GRC" != "0" ]; then
  bad "G10 score: the muted run failed or wrote no wav (GRC=$GRC) -- a stale mute wav reads as silence and would have passed"
elif [ "$NTRACK" -ge 2 ] && [ "$MUTE" = "0" ]; then
  ok "G10 score: $NTRACK tracks played in sequence, both sliders at 0 gives peak $MUTE"
else
  bad "G10 score: tracks=$NTRACK muted-peak=$MUTE"
fi

# G11 the movies: both play, both put lit pixels on the glass, and the menu drawn
# afterwards is byte for byte the menu drawn before them (that is G6's digest test,
# which the movies now run in front of).
MOVN=$(echo "$H" | grep -c "^HARNESS|movies")
LIT=$(python3 - <<'PYEOF' 2>>"$OUT"
# Per MOVIE, not per frame. Both of these open on black and fade in, so frame 0 is
# legitimately dark and a per-frame threshold would only be measuring the fade. What
# has to be true is that each movie put a real picture on the glass at some point.
import glob, sys
import numpy as np
from PIL import Image
bad = 0
for stem in ("LOGO", "INTRO2"):
    best = 0.0
    for f in sorted(glob.glob("shots/h/%s_f*.png" % stem)):
        a = np.asarray(Image.open(f).convert("RGB"))
        ink = float((a.max(axis=2) > 16).mean())
        print("%s ink=%.4f" % (f, ink), file=sys.stderr)
        best = max(best, ink)
    print("%s best ink=%.4f" % (stem, best), file=sys.stderr)
    if best < 0.05:
        bad += 1
print(bad)
PYEOF
)
if [ "$MOVN" -ge 1 ] && [ "$LIT" = "0" ]; then
  ok "G11 movies: LOGO and INTRO2 played and each put a real picture on the glass"
else
  bad "G11 movies: harness lines=$MOVN movies that never lit up=$LIT"
fi

# G12 the pause dialog: it opens, both pages work, the sliders reach the mixer, and
# Resume comes back. Driven through dopt_press/dopt_motion/dopt_release, which is what
# the mouse reaches, not through a setter no player can touch.
O=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --script gate_options.txt \
      --nosound 2>>"$OUT")
echo "$O" >> "$OUT"
OEND=$(echo "$O" | grep -E "^SCRIPT\|end")
OMUS=$(echo "$O" | grep "slider|Music" | sed -n 's/.*audio_music=\([0-9]*\).*/\1/p')
ORES=$(echo "$O" | grep -c "click|Resume Mission|.*act=1")
if [ "${OEND}" != "${OEND%0 failures}" ] && [ -n "$OMUS" ] && [ "$OMUS" -lt 120 ] && \
   [ "$ORES" = "1" ]; then
  ok "G12 pause dialog: both pages, music slider reached the mixer at $OMUS/255, Resume returned"
else
  bad "G12 pause dialog: [$OEND] music=$OMUS resume=$ORES"
fi

# G13 the pause actually pauses, and Abort returns to the menu. ESC at 3 s, then the
# dialog DWELLS OPEN for 6 s (--autoabort 6) before Abort is clicked. The old form
# clicked Abort on the next frame, so the same tick count came out whether the pause
# worked or not and the gate proved only that Abort works.
#
# G13 USED TO BE LOAD-SENSITIVE AND IS NOT ANY MORE. It slept on the wall clock and
# then asserted a tick COUNT (40..55 for ~3 s of live play at 15 Hz), so a machine that
# was also compiling starved the renderer of frames, the count came in low, and the
# gate went red with nothing wrong. That has happened more than once, and
# both times it passed on an immediate re-run, which is the worst kind of failure: it
# teaches everyone to re-run gates until they are green.
#
# The question the gate is actually asking is "did the WORLD advance while the dialog
# was up", and that has an exact answer that no amount of load can move: the brain's
# own Frame counter, printed on OPTIONS|open and again on OPTIONS|abort. Paused means
# the two are the same number. The wall clock now only decides how long we wait, never
# what passes.
( ./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --autoesc 3 --autoabort 6 --run 15 \
    > shots/esc.log 2>&1 & P=$!; sleep 40; kill $P 2>/dev/null; wait $P 2>/dev/null )
cat shots/esc.log >> "$OUT"
TICKS=$(sed -n 's/.*GAMELOOP|end|ticks=\([0-9]*\).*/\1/p' shots/esc.log)
OPENED=$(grep -c "^OPTIONS|open" shots/esc.log)
ABORTED=$(grep -c "^OPTIONS|abort" shots/esc.log)
DWELLED=$(grep -c "dwelling" shots/esc.log)
FOPEN=$(sed -n 's/^OPTIONS|open|frame=\(-*[0-9]*\).*/\1/p' shots/esc.log | head -1)
FABORT=$(sed -n 's/^OPTIONS|abort|frame=\(-*[0-9]*\).*/\1/p' shots/esc.log | head -1)
if [ "$OPENED" = "1" ] && [ "$ABORTED" = "1" ] && [ "$DWELLED" = "1" ] && \
   [ -n "$FOPEN" ] && [ -n "$FABORT" ] && [ "$FOPEN" = "$FABORT" ] && \
   [ -n "$TICKS" ] && [ "$TICKS" -ge 1 ]; then
  ok "G13 ESC pauses (world frozen at engine frame $FOPEN through 6 s inside the dialog, $TICKS ticks of live play before it) and Abort leaves"
else
  bad "G13 pause: opened=$OPENED aborted=$ABORTED dwelled=$DWELLED frame_open=$FOPEN frame_abort=$FABORT ticks=$TICKS"
fi

# G14 the campaign flow, hands-off: menu -> Start New Game -> side select (GDI) ->
# GDI1 briefing -> LANDING -> SCG01EA -> (synthetic win, labelled) -> CONSYARD ->
# score screen -> map selection -> GDI2 briefing -> SCG02EA boots and returns.
# Movies must actually PLAY (the files must open), not just be skipped over.
# Watchdogged like G15/G16's run_flow: a mission that fails to boot leaves the
# autopilot parked at the menu forever, and a hung gate is worse than a red one
# (it wedged a whole suite run once -- stale binary, mission failed to start).
./cnc3d --flowtest 60 --dylib ./TiberianDawn.dylib --dir ./missions/ \
      --content ./content/ --cameos cameos.pack --dospack dossidebar.pack \
      --dosinf dosinfantry.pack > shots/flow14.log 2>>"$OUT" &
FP=$!; FW=0
while kill -0 $FP 2>/dev/null && [ $FW -lt 300 ]; do sleep 1; FW=$((FW+1)); done
kill $FP 2>/dev/null; wait $FP 2>/dev/null
FL=$(cat shots/flow14.log)
echo "$FL" >> "$OUT"
FDONE=$(echo "$FL" | grep -c "FLOWTEST|complete")
FMOV=$(echo "$FL" | grep -cE "CAMPAIGN\|movie\|(GDI1|LANDING|CONSYARD|GDI2)\|played")
FSCORE=$(echo "$FL" | grep -c "CAMPAIGN|score|")
FMAP=$(echo "$FL" | grep -c "CAMPAIGN|mapsel|picked")
if [ "$FDONE" = "1" ] && [ "$FMOV" = "4" ] && [ "$FSCORE" = "1" ] && [ "$FMAP" = "1" ]; then
  ok "G14 campaign flow: side select, 4 movies played, score, map, mission 2 booted"
else
  bad "G14 campaign flow: complete=$FDONE movies=$FMOV score=$FSCORE map=$FMAP"
fi

# ---------------------------------------------------------------------------
# G15 / G16  THE REAL VERDICT. Read this before touching either of them.
#
# G14 above was GREEN for as long as the post-mission flow was completely dead.
# Its verdict is SYNTHETIC: --flowtest injects it from inside the tick loop while
# CNC_Advance_Instance keeps returning true. But a REAL win makes the DLL fire the
# game-over event and then RETURN FALSE (dllinterface.cpp:1668 win, :1688 loss).
# G14 therefore exercised every line of the win chain EXCEPT the one that decides
# whether the chain is entered at all, and play was dumped back to the main menu
# on every win he ever scored.
#
# G15 and G16 win and lose FOR REAL. They copy the missions to a scratch dir and
# rewrite the scenario's own trigger to fire on the first tick
# (`Time,Win,1,GoodGuy,None,0` -> trigger.cpp:475 ACTION_WIN -> Flag_To_Win, which
# fires at tick ~16 because TriggerTime starts expired, house.cpp:1229-1233).
# Nothing else in the INI is touched, and no source file is instrumented.
#
# BOTH gates forbid the string FLOWTEST|synthetic. That is not decoration: it is
# what stops these gates from decaying back into G14.
#
# flow_assert <logfile> WIN|LOSE  -> echoes the failures it found, empty means pass.
flow_assert() {
  local f="$1" want="$2" miss=""
  local movie score
  if [ "$want" = "WIN" ]; then movie=CONSYARD; else movie=GAMEOVER; fi
  if [ "$want" = "WIN" ]; then score=3; else score=4; fi

  grep -q "FLOWTEST|synthetic" "$f" && miss="$miss synthetic-verdict-present"
  grep -q "GAMEOVER|$want|movie=$movie" "$f" || miss="$miss no-GAMEOVER-$want-$movie"
  grep -qE "GAMELOOP\|end\|.*\|reason=$score\$" "$f" || miss="$miss no-GAMELOOP-reason=$score"
  grep -q "APP|game|round=1|reason=$score" "$f" || miss="$miss no-APP-round1-reason=$score"
  grep -q "CAMPAIGN|movie|$movie|played" "$f" || miss="$miss no-$movie-movie"
  if [ "$want" = "WIN" ]; then
    grep -q "CAMPAIGN|score|scen=1" "$f" || miss="$miss no-score-screen"
    grep -q "CAMPAIGN|mapsel|" "$f" || miss="$miss no-map-screen"
    grep -q "CAMPAIGN|brief|SCG02EA" "$f" || miss="$miss no-SCG02EA-brief"
  else
    grep -q "CAMPAIGN|retry|scenario=1" "$f" || miss="$miss no-retry"
  fi

  # The announcement must have been ON SCREEN, not merely reached. The bug drew
  # MISSION ACCOMPLISHED for exactly one frame before the loop tore down, so a
  # frame count is the one assertion the broken build cannot fake.
  local dframes dms
  dframes=$(sed -n 's/.*GAMEOVER|dwell|frames=\([0-9]*\)|.*/\1/p' "$f" | head -1)
  dms=$(sed -n 's/.*GAMEOVER|dwell|frames=[0-9]*|ms=\([0-9]*\).*/\1/p' "$f" | head -1)
  if [ -z "$dframes" ]; then miss="$miss no-dwell-line"
  elif [ "$dframes" -lt 60 ] || [ "$dms" -lt 2900 ]; then
    miss="$miss dwell-too-short(frames=$dframes,ms=$dms)"
  fi
  echo "$miss"
}

# run_flow <missions-dir> <logfile>: the campaign autopilot with the SYNTHETIC verdict
# disabled (--flowtest 999999 is never reached), watchdogged so a hang cannot wedge
# the suite. Both scratch scenarios end themselves, so this normally exits on its own.
run_flow() {
  ./cnc3d --flowtest 999999 --dylib ./TiberianDawn.dylib --dir "$1" \
          --content ./content/ --cameos cameos.pack --dospack dossidebar.pack \
          --dosinf dosinfantry.pack > "$2" 2>&1 &
  local p=$! w=0
  while kill -0 $p 2>/dev/null && [ $w -lt 120 ]; do sleep 1; w=$((w+1)); done
  kill $p 2>/dev/null; wait $p 2>/dev/null
}

# G15 a REAL win: SCG01EA and SCG02EA both decided by the engine, so the flow runs
# menu -> GDI -> GDI1 -> LANDING -> mission -> MISSION ACCOMPLISHED (3 s on screen)
# -> CONSYARD -> score -> map -> GDI2 -> SCG02EA, which is then won for real too.
# Patching mission 2 as well is what lets the run terminate itself, and it doubles
# as the proof that the engine resets cleanly across a real verdict.
GW=$(mktemp -d "${TMPDIR:-/tmp}/gate_realwin.XXXXXX")
cp missions/* "$GW"/
# The trigger NAME is arbitrary and differs per scenario (SCG01EA uses WIN=,
# SCG02EA uses win=), so match on the ACTION field, never on the name.
for f in SCG01EA.INI SCG02EA.INI; do
  sed -i.bak 's/^\([A-Za-z0-9_]*\)=[^,]*,Win,.*/\1=Time,Win,1,GoodGuy,None,0/' "$GW/$f"
done
run_flow "$GW/" shots/realwin.log
cat shots/realwin.log >> "$OUT"
MISS=$(flow_assert shots/realwin.log WIN)
GWDONE=$(grep -c "FLOWTEST|complete" shots/realwin.log)
if [ -z "$MISS" ] && [ "$GWDONE" = "1" ]; then
  DW=$(sed -n 's/.*GAMEOVER|dwell|frames=\([0-9]*\)|ms=\([0-9]*\).*/\1 frames \/ \2 ms/p' shots/realwin.log | head -1)
  ok "G15 REAL win: engine verdict reached the shell, announcement held $DW, CONSYARD + score + map + SCG02EA"
else
  bad "G15 REAL win:$MISS complete=$GWDONE"
fi
rm -rf "$GW"

# G16 a REAL loss, the mirror. dllinterface.cpp:1688 returns false the same way, and
# Do_Lose (scenario.cpp:624) has no score screen and no map screen: announcement,
# the GAMEOVER movie, then the same scenario again.
GL=$(mktemp -d "${TMPDIR:-/tmp}/gate_reallose.XXXXXX")
cp missions/* "$GL"/
sed -i.bak 's/^\([A-Za-z0-9_]*\)=[^,]*,Lose,.*/\1=Time,Lose,1,GoodGuy,None,0/' "$GL/SCG01EA.INI"
run_flow "$GL/" shots/reallose.log
cat shots/reallose.log >> "$OUT"
MISS=$(flow_assert shots/reallose.log LOSE)
GLDONE=$(grep -c "FLOWTEST|complete" shots/reallose.log)
GLRETRY=$(grep -c "CAMPAIGN|retry|scenario=1" shots/reallose.log)
if [ -z "$MISS" ] && [ "$GLDONE" = "1" ] && [ "$GLRETRY" = "1" ]; then
  ok "G16 REAL loss: MISSION FAILED held, GAMEOVER movie played, scenario 1 retried"
else
  bad "G16 REAL loss:$MISS complete=$GLDONE retry=$GLRETRY"
fi
rm -rf "$GL"

# G17 the shroud is WATERTIGHT over the heightfield. A playtest report: wide black
# stripes across the far field, sliver triangles in deep shroud, black wedges at the
# shoreline. All one bug: two of every core shroud quad's four corners were pinned to
# the old flat plane while the other two rode the ground, so each cell drew as a RAMP.
# Uphill it sank under the terrain and let ground show through; downhill it floated a
# whole cell up and painted black over sand and water.
#
# The gate is SELF-BASELINING, with no stored reference to rot: the HARD shroud path
# (`shroudsoft 0`) writes one flat square per cell and is geometrically exact, so it is
# the ground truth for WHICH cells must be black. Shoot the identical frame both ways
# and count pixels that the soft path leaves BRIGHT where the hard path says BLACK.
#
# The two paths tessellate differently (hard: one quad per cell; soft: a core quad plus
# a feather fan), so they disagree along the shroud EDGE by about a pixel. That is not a
# leak and must not fail the gate: the hard-black mask is eroded by 2 px first, and the
# threshold comes from a MEASUREMENT of both states on SCG01EA tick 8 rather than from
# taste -- with the ramped quads restored: 48146 lit pixels; with them fixed: 753, all of
# it boundary seam. 5000 sits ~6x above the healthy value and ~10x below the bug.
# Delete first: shots/ persists between suite runs, so a run that died at startup used
# to leave this gate measuring the PREVIOUS run's PNGs and passing green.
rm -f shots/g17_soft.png shots/g17_hard.png
printf 'tick 8\nshroudsoft 1\nshot shots/g17_soft.png\nquit\n' > /tmp/g17a.txt
printf 'tick 8\nshroudsoft 0\nshot shots/g17_hard.png\nquit\n' > /tmp/g17b.txt
./cnc_eyes --scen SCG01EA --pack SCG01EA.pack $BASE --script /tmp/g17a.txt >> "$OUT" 2>&1
./cnc_eyes --scen SCG01EA --pack SCG01EA.pack $BASE --script /tmp/g17b.txt >> "$OUT" 2>&1
LEAK=$(python3 - <<'PY'
from PIL import Image, ImageFilter
import os, sys
for _f in ('shots/g17_soft.png', 'shots/g17_hard.png'):
    if not os.path.exists(_f) or os.path.getsize(_f) == 0:
        print('G17MISSING|' + _f); sys.exit(1)
soft = Image.open('shots/g17_soft.png').convert('RGB')
hard = Image.open('shots/g17_hard.png').convert('RGB')
# deep shroud per the exact path, eroded 2 px so the boundary seam is not counted
mask = hard.point(lambda v: 255 if v < 12 else 0).convert('L')
mask = mask.filter(ImageFilter.MinFilter(5))
m, s = mask.load(), soft.load()
leak = 0
for y in range(soft.size[1]):
    for x in range(1040):             # left of the sidebar only
        if y < 40 and x < 320:        # the CAM/FOV HUD plate
            continue
        if m[x, y] < 128:
            continue
        sr, sg, sb = s[x, y]
        if sr + sg + sb > 120:        # lit ground showing through deep shroud
            leak += 1
print(leak)
PY
)
if [ -n "$LEAK" ] && [ "$LEAK" -lt 5000 ]; then
  ok "G17 shroud is watertight over the heightfield ($LEAK lit pixels in deep shroud, 48146 when the ramped quads are restored)"
else
  bad "G17 shroud leaks: $LEAK lit pixels inside deep shroud (the ramped-quad bug is back)"
fi

# G18 the ground SAMPLER matches the ground the console DRAWS. draw_terrain rasterises
# two triangles split SW-NE (the cartridge's own split, ROM 0x196FFC); terrain_y must
# evaluate that same surface. A bilinear patch -- what this used to do -- is a different
# surface on any cell whose four corners are not coplanar, by up to a tenth of a cell,
# which is what made units on a slope float or sink.
#
# The property tested is INDEPENDENT of the implementation: inside one triangle the
# height must be AFFINE in (x,z). Bilinear carries an x*z term and is not, so this
# distinguishes the two without re-deriving either. `terrainplanar` samples the map's
# most twisted cells and prints the worst deviation from the affine fit, in cells.
printf 'terrainplanar\nquit\n' > /tmp/g18.txt
./cnc_eyes --scen SCG01EA --pack SCG01EA.pack $BASE --script /tmp/g18.txt > shots/g18.log 2>>"$OUT"
cat shots/g18.log >> "$OUT"
PLAN=$(sed -n 's/^TERRAINPLANAR|.*|worst=\([0-9.e+-]*\).*$/\1/p' shots/g18.log)
PTW=$(sed -n 's/^TERRAINPLANAR|cells=[0-9]*|twist=\([0-9.]*\).*/\1/p' shots/g18.log)
# `terrainplanar` SKIPS any cell whose corner twist is under 1e-6 (planar either way, no
# evidence) and leaves `worst` at 0. So on a FLAT heightfield every cell is skipped and
# this gate reported a perfect 0.000000000 -- silent about exactly the regression it
# exists to catch, and this project has shipped a flat terrain before. The sample count
# and the twist are now part of the pass condition.
PCELLS=$(sed -n 's/^TERRAINPLANAR|cells=\([0-9]*\).*/\1/p' shots/g18.log)
if [ -z "$PLAN" ] || [ -z "$PCELLS" ]; then
  bad "G18 terrainplanar printed nothing usable (worst=$PLAN cells=$PCELLS)"
elif [ "$PCELLS" -lt 100 ]; then
  bad "G18 only $PCELLS twisted cells: the heightfield is flat or nearly so, and a flat map makes this gate vacuous"
elif [ "$PTW" = "0.000000" ] || [ -z "$PTW" ]; then
  bad "G18 worst twist is $PTW: nothing non-planar was sampled, so the test proves nothing"
elif python3 -c "import sys; sys.exit(0 if float('$PLAN') < 1e-5 else 1)"; then
  ok "G18 the height sampler is the drawn surface (worst affine deviation $PLAN cells over $PCELLS cells, twist $PTW)"
else
  bad "G18 sampler is not planar over the drawn triangles: worst=$PLAN (bilinear regression?)"
fi

# G19 the particle system is a POOL, not a billboard. A playtest report: "Particle
# Effects are still not working... buildings explode, units explode, trails from
# missiles". The old layer drew exactly one quad per engine anim, so nothing could
# scatter, arc or trail. The cartridge instead treats the anim as a TRIGGER that
# instantiates emitter sources, each spawning independent particles with their own
# velocity, life and gravity, all of which outlive the anim.
#
# Three legs, because each catches a different way of faking it:
#   expectefx   -- the anim happened at all (the old gate; keeps working)
#   expectpart  -- the recipe actually EMITTED particles, sprites and ballistic
#                  chunks counted separately. One billboard per anim scores 0 chunks.
#   expectmove  -- some particle MOVED between two dumps. A pool that spawns and never
#                  integrates looks perfect in a screenshot and fails right here.
# RETARGETED, and the change is the point: the CARTRIDGE'S own bullet
# table (ROM 0x169360, byte +0x23 per record, verified by direct read) moves SSM/SSM2/
# NUKE_UP off FRAG1 and GRENADE off VEH_HIT2 -- so on the console a rocket IMPACT
# never throws debris chunks; only a DEATH does (the unit table keeps FRAG1/FRAG2 as
# every vehicle's death explosion). Our brain now carries the same four values in
# bbdata.cpp. The old form of this gate asserted FRAG1 chunks from the tick-26 rocket
# impacts, i.e. it asserted the BUG. Now: the impact leg expects ART-EXP1 sprites and
# motion, and the chunk leg waits for the scenario's first real death.
# Measured on SCG01EC: ART-EXP1 x2 by tick 42; first death by tick 342 spawning 40
# ballistic chunks, best particle moved 3.07 cells. Thresholds sit well under those.
# The sim is seeded and the script is fixed, so the death lands on the same tick
# every run -- determinism is what makes a tick-index assertion legal here.
printf 'cam 49 55\ntick 26\ntick 8\nefxdump\ntick 8\nefxdump\nexpectefx ART-EXP1 1\nexpectpart ART-EXP1 sprite 6\nexpectmove 0.05\ntick 300\nexpectefx FBALL1 1\nexpectpart FBALL1 chunk 8\nquit\n' > /tmp/g19.txt
./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE --script /tmp/g19.txt > shots/g19.log 2>>"$OUT"
cat shots/g19.log >> "$OUT"
PFAIL=$(grep -cE "^(EXPECTPART|EXPECTMOVE|EXPECTEFX)\|.*\|FAIL$" shots/g19.log)
PCHUNK=$(sed -n 's/^EXPECTPART|FBALL1|chunk|.*have=\([0-9]*\)|.*/\1/p' shots/g19.log)
PMOVE=$(sed -n 's/^EXPECTMOVE|.*have=\([0-9.]*\)|.*/\1/p' shots/g19.log)
if [ "$PFAIL" = "0" ] && [ -n "$PCHUNK" ]; then
  ok "G19 particles: impact=ART-EXP1 (sprites, no chunks -- the cartridge's own bullet table), death=FBALL1 with $PCHUNK ballistic chunks, best particle moved $PMOVE cells"
else
  bad "G19 particles: $PFAIL assertion(s) failed (chunks=$PCHUNK move=$PMOVE)"
fi

# G20 WALLS reach the renderer at all. A playtest report: "Walls are not appearing,
# and dont have shadows". The engine keeps SBAG/CYCL/BRIK/BARB/WOOD as terrain OVERLAY
# cells, not techno objects, so they lived in none of the heaps the object dump walks and
# nothing ever told the renderer they existed. The brain now emits WALL| and this pass
# draws them.
#
# Three independent legs, because each catches a different way of faking it:
#   (a) the BRAIN exports them  -- 42 WALL| cells on SCG01EA, exactly the INI's =SBAG
#       count, of which 4 are inside the [MAP] rect (the other 38 sit at y=31..37, north
#       of it, and are correctly culled -- drawn=4 is the shape, not a shortfall)
#   (b) the RENDERER resolves cell -> variant, rotation and MESH -- the L at 48,54
#       (icon 6 = east+south) and the west stub at 49,54 (icon 8), each with the mesh
#       index the pack actually handed out. mesh=-1 here means a pack that predates the
#       wall bake, whose SBAG entry aliases a BULLET mesh
#   (c) the PIXELS change, and change into SANDBAGS. Self-baselining, with no stored
#       reference to rot: shoot the identical frame twice, once with the wall passes on
#       and once with `walls 0`, and diff. Measured on the first green run: 5901 pixels
#       differ, all of them inside the 240x110 box around the known wall run; over that
#       changed set the walls-on frame means RGB (88,78,42) -- tan, R>G, B far below --
#       while the walls-off frame means (29,54,24), which is grass. A gate that only
#       counted "tan pixels" would pass on this map's own brown rock speckle; requiring
#       the SAME pixels to be tan with the feature on and green with it off cannot.
cat > gate_walls.txt <<'EOS'
tick 20
cam 48 54
walldump
shot shots/g20_on.png
walls 0
shot shots/g20_off.png
quit
EOS
# Through the wrapper, and the run's stdout is read back OUT OF THE LOG rather than off a
# pipeline: the old form ended in tee, so even $? would have been tee's status and the
# exit code of the binary was unreachable.
gbegin shots/g20_on.png shots/g20_off.png shots/g20.log
grun shots/g20.log --scen SCG01EA --pack SCG01EA.pack $BASE --script gate_walls.txt
gshots shots/g20_on.png shots/g20_off.png
W=$(cat shots/g20.log)
WCELLS=$(echo "$W" | sed -n 's/^WALLDUMP-END|cells=\([0-9]*\)|.*/\1/p')
WDRAWN=$(echo "$W" | sed -n 's/^WALLDUMP-END|cells=[0-9]*|drawn=\([0-9]*\).*/\1/p')
WL=$(echo "$W" | grep -c '^WALLDUMP|48,54|SBAG|icon=6|dmg=0|variant=1|face=0|mesh=[0-9]')
WS=$(echo "$W" | grep -c '^WALLDUMP|49,54|SBAG|icon=8|dmg=0|variant=0|face=0|mesh=[0-9]')
WPIX=$(python3 - <<'PY'
from PIL import Image
import numpy as np
a = np.asarray(Image.open('shots/g20_on.png').convert('RGB')).astype(int)
b = np.asarray(Image.open('shots/g20_off.png').convert('RGB')).astype(int)
d = (a != b).any(axis=2)
box = np.zeros_like(d); box[360:470, 550:790] = True      # the known wall run
inside, outside = int((d & box).sum()), int((d & ~box).sum())
on, off = a[d], b[d]
tan   = on.size and on[:,0].mean() > on[:,1].mean() and (on[:,0]-on[:,2]).mean() >= 30
green = off.size and off[:,1].mean() > off[:,0].mean()
print("%d %d %d" % (inside, outside, 1 if (tan and green) else 0))
PY
)
set -- $WPIX
WIN="$1"; WOUT="$2"; WCOL="$3"
if [ "$GRC" != "0" ]; then
  bad "G20 walls: the run itself failed or wrote no shot (GRC=$GRC)"
elif [ "$WCELLS" = "42" ] && [ "$WDRAWN" = "4" ] && [ "$WL" = "1" ] && [ "$WS" = "1" ] \
   && [ -n "$WIN" ] && [ "$WIN" -ge 4400 ] && [ "$WOUT" = "0" ] && [ "$WCOL" = "1" ]; then
  ok "G20 walls: 42 SBAG cells exported, 4 in view, L at 48,54 + stub at 49,54, $WIN pixels turn from grass to sandbag (5901 measured)"
else
  bad "G20 walls: cells=$WCELLS(want 42) drawn=$WDRAWN(want 4) L=$WL stub=$WS changed-in-box=$WIN(want >=4400) changed-outside=$WOUT(want 0) tan-on-green-off=$WCOL"
fi

# G21 HOVERCRAFT RIDERS. A playtest report: "the vehicle/unit on the hovercraft is
# invisible". A unit riding a transport is put in LIMBO by the engine -- CargoClass::Attach
# calls Limbo(), which takes it off the map layer but never touches Coord -- so a
# reinforcement passenger still carries the ObjectClass constructor's 0xFFFFFFFF sentinel
# and its OBJ| line reports cell 127,127 with limbo=1. The renderer had never read limbo,
# and nothing in the dump said which transport a passenger was in.
#
# Five legs, on BOTH of SCG01EA's waves (three riflemen ~tick 190, one jeep ~tick 840):
#   (a) the deck resolves at all       -- expectcargo 3 and expectcargo 1
#   (b) the rider is AT the transport  -- at= within a quarter cell of the LST's own
#       position, ylift=0.0605 (the LST mesh's deck plate at 62/1024 cells), drawn=1
#   (c) ENGINE TRUTH IS UNTOUCHED      -- `obj JEEP 0` must STILL say cell=127,127. Only
#       a COPY was moved; if that number changes, the brain was mutated
#   (d) the limbo guard's blast radius  -- it is global, so cargodump prints every object
#       it hides. On the transport-free TSTBOAT flume that count must be 0, and on the jeep wave
#       exactly 1 (the jeep itself). This is the leg that catches the guard silently
#       taking something else off the screen
#   (e) PIXELS, self-baselining -- shoot the deck twice, once with `riders 0`. Measured
#       on the first green run: 972 pixels differ, all inside the deck box, and over that
#       changed set the riders-on frame means RGB (147,132,80) -- the GDI sand jeep --
#       against (59,57,51) for the bare grate underneath
# Plus: a rider must not be selectable. It lives in g_riders, never in g_objects, so no
# pick or band path can reach it; the gate proves it rather than trusting the design.
cat > gate_cargo.txt <<'EOS'
tick 190
cam 54 61
cargodump
expectcargo 3
tick 650
cam 48 59
obj JEEP 0
cargodump
expectcargo 1
shot shots/g21_on.png
riders 0
shot shots/g21_off.png
riders 1
clickobj JEEP 0
expectsel 0
band 0 0 1030 700
seldump
quit
EOS
gbegin shots/g21_on.png shots/g21_off.png shots/g21.log
grun shots/g21.log --scen SCG01EA --pack SCG01EA.pack $BASE --script gate_cargo.txt
gshots shots/g21_on.png shots/g21_off.png
C=$(cat shots/g21.log)
CFAIL=$(echo "$C" | grep -cE '^(EXPECTCARGO|EXPECTSEL)\|.*\|FAIL$')
CINF=$(echo "$C" | grep -c '^CARGO|E1#[0-9]*|kind=0|at=54\.[0-9]*,61\.[0-9]*|ylift=0\.0605|face=[0-9-]*|drawn=1$')
CJEEP=$(echo "$C" | grep -c '^CARGO|JEEP#[0-9]*|kind=1|at=48\.[0-9]*,59\.[0-9]*|ylift=0\.0605|face=[0-9-]*|drawn=1$')
CTRUTH=$(echo "$C" | grep -c '^OBJ|JEEP#0|UNIT|GoodGuy|cell=127,127|')
CLIMBO=$(echo "$C" | grep -c '^CARGO|riders=1|limboed=1$')
CSELR=$(echo "$C" | grep -c '^SEL|UNIT|JEEP|')
CPIX=$(python3 - <<'PY'
from PIL import Image
import numpy as np
a = np.asarray(Image.open('shots/g21_on.png').convert('RGB')).astype(int)
b = np.asarray(Image.open('shots/g21_off.png').convert('RGB')).astype(int)
d = (a != b).any(axis=2)
box = np.zeros_like(d); box[350:430, 640:710] = True          # the LST deck
inside, outside = int((d & box).sum()), int((d & ~box).sum())
on, off = a[d], b[d]
sand = on.size and on[:,0].mean() > on[:,1].mean() > on[:,2].mean() and on[:,0].mean() >= 110
dark = off.size and off.mean() <= 90
print("%d %d %d" % (inside, outside, 1 if (sand and dark) else 0))
PY
)
set -- $CPIX
CIN="$1"; COUT="$2"; CCOL="$3"
# the limbo guard must hide NOTHING on a scenario with no transport in it
CZERO=$(printf 'tick 300\ncargodump\nquit\n' > /tmp/g21z.txt; \
        ./cnc_eyes --scen TSTBOAT --pack SCG10EA.pack $BASE --script /tmp/g21z.txt 2>&1 \
        | tee -a "$OUT" | grep -c '^CARGO|riders=0|limboed=0$')
if [ "$GRC" != "0" ]; then
  bad "G21 riders: the run itself failed or wrote no shot (GRC=$GRC)"
elif [ "$CFAIL" = "0" ] && [ "$CINF" = "3" ] && [ "$CJEEP" = "1" ] && [ "$CTRUTH" -ge 1 ] \
   && [ "$CLIMBO" = "1" ] && [ "$CSELR" = "0" ] && [ "$CZERO" = "1" ] \
   && [ -n "$CIN" ] && [ "$CIN" -ge 700 ] && [ "$COUT" = "0" ] && [ "$CCOL" = "1" ]; then
  ok "G21 riders: 3 riflemen and 1 jeep resolve onto the LST deck, jeep still limboed at 127,127, unselectable, $CIN deck pixels turn to GDI sand (972 measured)"
else
  bad "G21 riders: asserts=$CFAIL inf=$CINF(want 3) jeep=$CJEEP(want 1) truth127=$CTRUTH limbo=$CLIMBO selectable=$CSELR(want 0) freezero=$CZERO deckpx=$CIN(want >=700) outside=$COUT(want 0) sand=$CCOL"
fi

rm -f shots/g22_near.png shots/g22_far.png shots/g22_sn.png shots/g22_sf.png shots/g22_panel.png
# G22 THE 3D CURSOR. A playtest report: "3D cursors does not seem to be enabled, which
# should also have shadows like in the N64 version". The console's pointer is not a sprite
# at all: it is one of fourteen MODELS drawn in the world, on the terrain, at the picked
# ground point, at the same 0.25 node scale as everything else -- so it has constant WORLD
# size and its "shadow" is the dark-grey extruded side wall along its lower-right edge.
# Ours was the 1995 MOUSE.SHP blitted in screen space.
#
# THE DECISIVE ASSERTION IS ONE LINE: put the cursor at two positions in the SAME screen
# column but different rows, so the far one stands further from the camera, and require
# its white bounding box to be measurably NARROWER. A screen-space sprite gives two
# identical boxes and cannot pass. Measured on the first green run: world cursor 32x59 px
# near, 23x30 far; the same two positions through `cursor3d 0` give 24x42 BOTH TIMES,
# which is this gate's own negative control and proves the test can fail.
# Plus: over the DOS sidebar the 1995 sprite must SURVIVE (the console has no sidebar and
# therefore no cursor art for one) -- 192 black outline pixels and 402 white in a 60x60
# crop where the bare panel has zero of each.
cat > gate_cursor.txt <<'EOS'
tick 30
cam 48 52
cursor 660 500
shot shots/g22_near.png
cursor 660 200
shot shots/g22_far.png
cursor3d 0
cursor 660 500
shot shots/g22_sn.png
cursor 660 200
shot shots/g22_sf.png
cursor3d 1
cursor 1150 400
shot shots/g22_panel.png
quit
EOS
# an earlier note listed G22 among the gates that "already did" delete and check. It did
# not: it had neither half, and it measures FIVE pictures. Corrected there as well as here.
gbegin shots/g22_near.png shots/g22_far.png shots/g22_sn.png shots/g22_sf.png \
       shots/g22_panel.png
grun - --scen SCG01EA --pack SCG01EA.pack $BASE --noshroud --script gate_cursor.txt
gshots shots/g22_near.png shots/g22_far.png shots/g22_sn.png shots/g22_sf.png \
       shots/g22_panel.png
CUR=$(python3 - <<'PY'
from PIL import Image
import numpy as np
def wbox(n, cx, cy):
    a = np.asarray(Image.open('shots/%s.png' % n).convert('RGB')).astype(int)
    s = a[cy-70:cy+90, cx-70:cx+90]
    m = (s[:,:,0] > 190) & (s[:,:,1] > 190) & (s[:,:,2] > 190)
    ys, xs = np.nonzero(m)
    if not len(xs): return (0, 0)
    return (int(xs.max()-xs.min()+1), int(ys.max()-ys.min()+1))
nw, nh = wbox('g22_near', 660, 500)
fw, fh = wbox('g22_far',  660, 200)
sn = wbox('g22_sn', 660, 500)
sf = wbox('g22_sf', 660, 200)
p  = np.asarray(Image.open('shots/g22_panel.png').convert('RGB')).astype(int)[380:440, 1140:1200]
q  = np.asarray(Image.open('shots/g22_near.png').convert('RGB')).astype(int)[380:440, 1140:1200]
blk = int((p < 25).all(axis=2).sum()); wht = int((p > 200).all(axis=2).sum())
bare = int((q < 25).all(axis=2).sum()) + int((q > 200).all(axis=2).sum())
print("%d %d %d %d %d %d %d %d %d" % (nw, nh, fw, fh,
      1 if (sn == sf and sn[0] > 0) else 0, blk, wht, bare,
      1 if (nw > 0 and fw > 0) else 0))
PY
)
set -- $CUR
CNW="$1"; CNH="$2"; CFW="$3"; CFH="$4"; CSPR="$5"; CBLK="$6"; CWHT="$7"; CBARE="$8"; CDRAWN="$9"
# determinism: the cursor must not read a wall clock
printf 'tick 30\ncam 48 52\ncursor 660 500\nshot shots/g22_d1.png\nquit\n' > /tmp/g22d.txt
# A DETERMINISM LEG IS THE TRAP'S FAVOURITE SHAPE, and it needs no rendering to pass:
# two runs that die at startup leave the PREVIOUS pair standing, and the previous pair is
# byte-identical because it was written by a working binary. So "two shots byte-identical"
# is exactly what a dead binary reports. Both files are deleted and both statuses read.
# Note gbegin is NOT called again here: it would clear the GRC the picture runs above
# accumulated, and the verdict tests one GRC for the whole gate.
rm -f shots/g22_d1.png shots/g22_d2.png
grun - --scen SCG01EA --pack SCG01EA.pack $BASE --noshroud --script /tmp/g22d.txt
sed 's/g22_d1/g22_d2/' /tmp/g22d.txt > /tmp/g22e.txt
grun - --scen SCG01EA --pack SCG01EA.pack $BASE --noshroud --script /tmp/g22e.txt
gshots shots/g22_d1.png shots/g22_d2.png
CDET=$(cmp -s shots/g22_d1.png shots/g22_d2.png && echo 1 || echo 0)
if [ "$GRC" != "0" ]; then
  bad "G22 cursor: a run failed or wrote no shot (GRC=$GRC)"
elif [ "$CDRAWN" = "1" ] && [ "$CFW" -lt "$CNW" ] && [ "$CFH" -lt "$CNH" ] \
   && [ "$CSPR" = "1" ] && [ "$CBLK" -ge 100 ] && [ "$CWHT" -ge 200 ] && [ "$CBARE" = "0" ] \
   && [ "$CDET" = "1" ]; then
  ok "G22 cursor is in the WORLD: ${CNW}x${CNH} px near, ${CFW}x${CFH} far (the flat sprite gives identical boxes), DOS pointer kept over the panel, two shots byte-identical"
else
  bad "G22 cursor: near=${CNW}x${CNH} far=${CFW}x${CFH} (far must be narrower AND shorter) sprite-control-identical=$CSPR panel black=$CBLK white=$CWHT bare=$CBARE(want 0) determinism=$CDET"
fi

# G23 THE VERDICT BANNER. A playtest report: "not the correct graphic". We were drawing
# the words with the sidebar's FONT.SHP over the live battlefield, which is the DOS
# behaviour (Do_Win prints two VCR.FNT strings) and not the console's. The cartridge
# blacks out the ENTIRE screen -- battlefield AND sidebar -- with two BLACK.IMG widgets
# and blits a pre-rendered 200x53 gold banner at (60,99) of a 320x240 screen.
#
# Four legs, all pixels:
#   (a) the frame is BLACK everywhere outside the banner rect, INCLUDING the 240 px strip
#       where the sidebar was -- that strip is what separates the console's presentation
#       from a banner floating over the battlefield
#   (b) ZERO near-white pixels anywhere, which is how we know the fallback did not fire.
#       This is not a vacuous test: the same run with `--verdict nosuchpack.pack` puts
#       43033 near-white FONT.SHP pixels on the glass. Measured, both ways.
#   (c) the banner sits exactly where the ROM puts it: s = floor(min(fbw/320, fbh/240)),
#       top-left at (ox + 60*s, oy + 99*s), size 200*s x 53*s. At 1280x720 that is s=3,
#       600x159 at (340,297)
#   (d) every opaque texel of the drawn banner EQUALS the pack's own art scaled by s.
#       GL_NEAREST at an integer scale, so this is exact rather than a tolerance -- 38151
#       opaque texels, zero mismatches. (The pack's art is in turn byte-identical to the
#       cartridge's MACCOMP.IMG; that is the bakery's own proof, and it was re-checked
#       against data/rom by hand when this gate was written.)
GV=$(mktemp -d "${TMPDIR:-/tmp}/gate_verdict.XXXXXX")
cp missions/* "$GV"/
sed -i.bak 's/^\([A-Za-z0-9_]*\)=[^,]*,Win,.*/\1=Time,Win,1,GoodGuy,None,0/' "$GV/SCG01EA.INI"
printf 'tick 20\nshot shots/g23_win.png\nquit\n' > /tmp/g23.txt
gbegin shots/g23_win.png shots/g23.log
grun shots/g23.log --scen SCG01EA --pack SCG01EA.pack $BASE --dir "$GV/" \
      --script /tmp/g23.txt
gshots shots/g23_win.png
V=$(cat shots/g23.log)
VOVER=$(echo "$V" | grep -c '^GAMEOVER|WIN|')
VPIX=$(python3 - <<'PY'
from PIL import Image
import numpy as np, struct, math

# the pack's own art, straight out of verdict.pack
b = open('verdict.pack', 'rb').read()
assert b[:8] == b'CNC3DVRD', 'not a verdict pack'
ver, count = struct.unpack_from('<II', b, 8)
o, art = 16, {}
for _ in range(count):
    name = b[o:o+8].split(b'\0')[0].decode(); o += 8
    w, h, uw, uh = struct.unpack_from('<IIII', b, o); o += 16
    px = np.frombuffer(b[o:o + w*h*4], dtype=np.uint8).reshape(h, w, 4).astype(int)
    o += w*h*4
    art[name] = (px[:uh, :uw], uw, uh)

shot = np.asarray(Image.open('shots/g23_win.png').convert('RGB')).astype(int)
fbh, fbw = shot.shape[0], shot.shape[1]
s  = max(1, int(math.floor(min(fbw / 320.0, fbh / 240.0))))
ox = int((fbw - 320 * s) // 2)
oy = int((fbh - 240 * s) // 2)
ref, uw, uh = art['MACCOMP']
x0, y0 = ox + 60 * s, oy + 99 * s
big = np.repeat(np.repeat(ref, s, axis=0), s, axis=1)
sub = shot[y0:y0 + uh*s, x0:x0 + uw*s]

mask = big[:, :, 3] > 127
bad_opaque = int((sub[:, :, :3] != big[:, :, :3])[mask].any(axis=-1).sum())
bad_clear  = int(sub[~mask].max()) if (~mask).any() else 0

# black everywhere outside the placed rect, sidebar strip included
out = shot.copy(); out[y0:y0 + uh*s, x0:x0 + uw*s] = 0
lit  = int((out > 25).any(axis=2).sum())
side = int((shot[:, 1040:] > 25).any(axis=2).sum())
white = int((shot > 200).all(axis=2).sum())
print("%d %d %d %d %d %d %d %d %d" % (s, x0, y0, bad_opaque, bad_clear, lit, side,
                                      white, int(mask.sum())))
PY
)
set -- $VPIX
VS="$1"; VX="$2"; VY="$3"; VBADO="$4"; VBADC="$5"; VLIT="$6"; VSIDE="$7"; VWHITE="$8"; VTEX="$9"
rm -rf "$GV"
if [ "$GRC" != "0" ]; then
  bad "G23 verdict: the run itself failed or wrote no shot (GRC=$GRC)"
elif [ "$VOVER" -ge 1 ] && [ "$VBADO" = "0" ] && [ "$VBADC" = "0" ] && [ "$VLIT" = "0" ] \
   && [ "$VSIDE" = "0" ] && [ "$VWHITE" = "0" ] && [ -n "$VTEX" ] && [ "$VTEX" -ge 30000 ]; then
  ok "G23 verdict: the cartridge's gold banner at ${VX},${VY} scale ${VS}x, $VTEX opaque texels exact against verdict.pack, everything else black including the sidebar strip, no FONT.SHP fallback"
else
  bad "G23 verdict: gameover=$VOVER badopaque=$VBADO badclear=$VBADC lit-outside=$VLIT sidebar-lit=$VSIDE white=$VWHITE(want 0; the fallback puts 43033 there) texels=$VTEX at ${VX},${VY} s=$VS"
fi

# G25 THE MCV DEPLOY RIG. (G24 is reserved for the cursor-animation gate landing in the
# same wave from a separate line of work; this one deliberately does not claim that
# number.) The console plays a separately authored 21-node rigged model, slot 18
# ANY_UNIT_MCVANIMZ1, for the MCV -> Construction Yard deploy, and before wave 5 we played
# nothing of the kind.
#
# This gate has to survive the ways gates in this file have actually been fooled, so it
# asserts four independent things and ANY one of them fails it:
#
#   1. ENGINE STATE. The deploy really happened: a FACT exists in BSTATE_CONSTRUCTION
#      (doing=0). Without this the whole gate could pass while the right-click was taken
#      as a MOVE -- which is exactly what a zoomed camera does to this script.
#   2. THE CLIP STEPPED. MCVRIG| lines appear, the frame numbers STRICTLY RISE, the first
#      is early in the clip and the last is late. A rig frozen on frame 0 would still put
#      pixels on the glass.
#   3. THE RIG IS NEITHER THE MCV UNIT NOR THE FINISHED YARD. Measured in a box around the
#      deploy site only: the early rig differs from the pre-deploy MCV, two clip frames
#      differ from each other, and the rig differs from the finished Construction Yard by
#      thousands of pixels. That triple separates "the deploy rig is drawn" from "the unit
#      never changed" and from "the yard was drawn all along".
#   4. DETERMINISM. Two whole runs give a byte-identical rig frame, because the clip is
#      driven off the engine's stage and must never touch a wallclock.
#
# Thresholds sit with margin under MEASURED values, and every measurement is printed in
# the failure line so a drift is diagnosable rather than mysterious. Measured:
# structure pixels pre 2690, early 3139, mid 3823, late 14944, yard 15226; picture diffs
# pre-early 3688, early-mid 3069, late-yard 7213.
# --nosmudge, and it is not a workaround. This gate counts STRUCTURE pixels in a window
# over the deploy site and asserts they rise as the rig plays. A Construction Yard lays a
# BIB apron the moment it is placed, so once the decals landed the window
# started at 14252 instead of 2683 and the rise it is measuring was swamped by a tan
# rectangle that is not the rig. Switching the decals off is what makes the gate measure
# the thing it names. The decals have their own gate, G35.
MR="--scen SCG01EC --pack SCG01EA.pack $BASE --nosound --nosmudge --script gate_mcvrig.txt"
# THESE FIVE PICTURES DO NOT LIVE IN shots/. gate_mcvrig.txt shoots them into the run
# directory itself, which is why every earlier sweep for "shots/" missed this gate
# entirely. They persist between runs exactly the same way.
gbegin mcvrig_0_pre.png mcvrig_1_early.png mcvrig_2_mid.png mcvrig_3_late.png \
       mcvrig_4_yard.png shots/g25.log
grun shots/g25.log $MR
gshots mcvrig_0_pre.png mcvrig_1_early.png mcvrig_2_mid.png mcvrig_3_late.png \
       mcvrig_4_yard.png
MG=$(cat shots/g25.log)
MDEP=$(echo "$MG" | grep -E "^OBJ\|BUILDING\|FACT\|" | grep -c "|doing=0|")
MFRAMES=$(echo "$MG" | grep -E "^MCVRIG\|" | sed -E 's/.*frame=([0-9.]+)\/.*/\1/')
MN=$(echo "$MFRAMES" | grep -c .)
MRISE=$(echo "$MFRAMES" | awk 'NR==1{p=$1;n=1;next}{if($1>p)n++;p=$1}END{print n+0}')
MFIRST=$(echo "$MFRAMES" | head -1)
MLAST=$(echo "$MFRAMES" | tail -1)
MPIX=$(python3 - <<'PY'
import numpy as np
from PIL import Image
# The deploy site, in the SCG01EC gate camera (cam 57 49, no zoom). Everything outside
# this box -- the old base, the sidebar, walking infantry -- is deliberately excluded, so
# the numbers below can only come from the thing being deployed.
X0, Y0, X1, Y1 = 580, 250, 800, 440
def site(p):
    a = np.array(Image.open(p).convert("RGB"), dtype=int)[Y0:Y1, X0:X1]
    # "structure": the models here are grey / gold / white, the ground is green grass and
    # the unlit fringe is near black, so a pixel whose red reaches its green is not grass.
    return a, int(((a[:, :, 0] + 12 >= a[:, :, 1]) & (a.sum(2) > 90)).sum())
im, n = {}, {}
for k, f in (("pre", "0_pre"), ("early", "1_early"), ("mid", "2_mid"),
             ("late", "3_late"), ("yard", "4_yard")):
    im[k], n[k] = site("mcvrig_%s.png" % f)
def d(a, b):
    return int((np.abs(im[a] - im[b]).sum(2) > 60).sum())
print("%d %d %d %d %d %d %d %d" % (n["pre"], n["early"], n["mid"], n["late"], n["yard"],
                                   d("pre", "early"), d("early", "mid"), d("late", "yard")))
PY
)
set -- $MPIX
MPRE="$1"; MEARLY="$2"; MMID="$3"; MLATEN="$4"; MYARD="$5"
DPE="$6"; DEM="$7"; DLY="$8"
# THE SHARPEST INSTANCE OF THE TRAP IN THE WHOLE SUITE, and it was hiding in the leg that
# looks most like proof. Both runs sent their status to /dev/null and NEITHER deleted the
# file between them, so the two shasums were of ONE undeleted file: two runs that never
# started hash the same stale picture and the gate reports "two runs identical". The
# delete between the hashes is the load-bearing line, not the status.
rm -f mcvrig_1_early.png
grun - $MR
gshots mcvrig_1_early.png
MD1=$(shasum mcvrig_1_early.png 2>/dev/null | cut -d' ' -f1)
rm -f mcvrig_1_early.png
grun - $MR
gshots mcvrig_1_early.png
MD2=$(shasum mcvrig_1_early.png 2>/dev/null | cut -d' ' -f1)
if [ "$GRC" != "0" ]; then
  bad "G25 MCV deploy rig: a run failed or wrote no shot (GRC=$GRC) -- note both digests would be EMPTY and therefore equal, which is what the old form reported as identical"
elif [ "$MDEP" -ge 1 ] && [ "$MN" -ge 3 ] && [ "$MRISE" = "$MN" ] \
   && [ "$(echo "$MFIRST < 10" | bc -l)" = "1" ] \
   && [ "$(echo "$MLAST > 70" | bc -l)" = "1" ] \
   && [ "$MPRE" -gt 1500 ] && [ "$MPRE" -lt 5000 ] \
   && [ "$MEARLY" -gt 1500 ] && [ "$MEARLY" -lt 8000 ] \
   && [ "$MLATEN" -gt 9000 ] && [ "$MYARD" -gt 9000 ] \
   && [ "$DPE" -gt 1500 ] && [ "$DEM" -gt 1200 ] && [ "$DLY" -gt 3000 ] \
   && [ "$MD1" = "$MD2" ]; then
  ok "G25 MCV deploy rig: yard in BSTATE_CONSTRUCTION, clip stepped $MN times $MFIRST -> $MLAST of 101 strictly rising; site structure px pre=$MPRE early=$MEARLY mid=$MMID late=$MLATEN yard=$MYARD; rig differs from the MCV by $DPE px, from its own next sampled frame by $DEM, from the finished yard by $DLY; two runs identical ${MD1:0:12}"
else
  bad "G25 MCV deploy rig: deployed=$MDEP frames=$MN rising=$MRISE first=$MFIRST last=$MLAST px pre=$MPRE early=$MEARLY mid=$MMID late=$MLATEN yard=$MYARD diffs pre-early=$DPE early-mid=$DEM late-yard=$DLY det=${MD1:0:12}/${MD2:0:12}"
fi

# G24 THE CURSOR ANIMATIONS. Ten of the twenty console cursor states carry a frame count
# of 100 and every one of them used to draw STATIC (Reported: "3D cursor animations seem
# bugged out"). Three separate things have to be true and this gate asserts all three
# against ENGINE STATE and PIXELS, never against an exit code:
#
#   1. The DEPLOY diamond's four arms are authored to travel exactly 200 mesh units
#      radially (tools/bakery/cursoranim.py proves that out of the ROM). At frame 92 the
#      renderer's own `cursor3ddump` must report moved=200.00 for all four and 0.00 for
#      the still centre node. A merely non-zero number would pass a wrong bake; 200.00
#      will not.
#   2. The picture must actually change. `cursor3danim 0` freezes every state at its rest
#      pose, so an animated frame against a rest frame at the SAME tick isolates the
#      cursor and nothing else. The negative control is two rest frames, which must differ
#      by ZERO pixels; without it "the scene moved" would read as "the cursor moved".
#   3. The two flipbook cursors must show the cartridge's own image on each of the 25
#      reachable frames. The renderer computes that index from a transcribed time table,
#      so the whole sequence is recomputed here from the ROM's own numbers and compared,
#      not just one frame.
#
# Plus determinism: the clock is the engine tick and never a wall clock, so two runs of
# the same script must be byte-identical.
cat > gate_curanim.txt <<'EOS'
tick 30
cam 48 52
cursor 660 500
cursor3dstate 7
cursor3danim 0
shot shots/g24_rest.png
shot shots/g24_rest2.png
cursor3danim 1
shot shots/g24_anim.png
EOS
for i in $(seq 1 26); do echo "cursor3ddump" >> gate_curanim.txt; echo "tick 1" >> gate_curanim.txt; done
echo "cursor3dstate 11" >> gate_curanim.txt
for i in $(seq 1 26); do echo "cursor3ddump" >> gate_curanim.txt; echo "tick 1" >> gate_curanim.txt; done
echo "cursor3dstate 12" >> gate_curanim.txt
for i in $(seq 1 26); do echo "cursor3ddump" >> gate_curanim.txt; echo "tick 1" >> gate_curanim.txt; done
echo "quit" >> gate_curanim.txt
# Delete first, then read the status: see the wrapper's header at the top of this file.
# G24 measures FOUR pictures and truncates its log per run, so the log-fed arm assertion
# was the only one that ever objected: the moved-pixel count, the frozen control and the
# two-run cmp all scored stale PNGs and all reported green against a binary that drew
# nothing.
gbegin shots/g24_rest.png shots/g24_rest2.png shots/g24_anim.png shots/g24_det.png shots/g24.log
grun shots/g24.log --scen SCG01EA --pack SCG01EA.pack $BASE --noshroud --script gate_curanim.txt
gshots shots/g24_rest.png shots/g24_rest2.png shots/g24_anim.png
# A SENTINEL THAT CANNOT PASS. The python below opens three PNGs, so on a run that wrote
# none it would die with a traceback rather than return a verdict. It is evaluated only
# when the run succeeded and left its pictures behind.
CA="0 -1.00 0 999 0 999"
if [ "$GRC" = "0" ]; then
CA=$(python3 - <<'PY'
import re
import numpy as np
from PIL import Image

log = open('shots/g24.log').read()

# 1. the deploy arms at frame 92
arms, centre = [], None
for b in re.split(r'\n(?=CURSOR3D\|)', log):
    if not re.match(r'CURSOR3D\|.*\|state=7\|code=0x06\|frames=100\|frame=92\|', b):
        continue
    mv = [float(m) for m in re.findall(r'CURSOR3DPART\|.*?\|moved=([0-9.]+)\|', b)]
    if len(mv) == 5:
        centre, arms = mv[0], mv[1:]
        break
armok = (centre is not None and abs(centre) < 0.01
         and len(arms) == 4 and all(abs(a - 200.0) < 0.05 for a in arms))

# 2. pixels: animated against rest, and the rest-against-rest control
def px(a, b):
    x = np.asarray(Image.open('shots/' + a).convert('RGB')).astype(int)
    y = np.asarray(Image.open('shots/' + b).convert('RGB')).astype(int)
    return int(((np.abs(x - y).sum(axis=2)) > 24).sum())
moved = px('g24_anim.png', 'g24_rest.png')
control = px('g24_rest2.png', 'g24_rest.png')

# 3. the flipbook index sequences, recomputed from the cartridge's own time tables.
#    0x0A: TexAnim time table RAM 0x801BBE80 / ROM 0x15F8F0.
#    0x0B: RAM 0x801BBC40 / ROM 0x15F6B0. Last entry is the period.
WANT = {0x0A: ([0, 4000, 8000, 12000, 16000], 4),
        0x0B: ([0, 1600, 3200, 4800], 3)}
seen = {0x0A: {}, 0x0B: {}}
for m in re.finditer(
        r'code=0x(0[AB])\|frames=100\|frame=(\d+)\|flipvariants=(\d+)\|flip=(-?\d+)', log):
    code = int(m.group(1), 16)
    if int(m.group(3)) == WANT[code][1]:
        seen[code][int(m.group(2))] = int(m.group(4))
flipbad = flipseen = 0
for code, (times, n) in WANT.items():
    period = times[n]
    for f in sorted(seen[code]):
        flipseen += 1
        tt = (f * 160 + 1) % period
        want = next(i for i in range(n) if tt <= times[i + 1])
        if seen[code][f] != want:
            flipbad += 1

print("%d %.2f %d %d %d %d" % (1 if armok else 0, (min(arms) if arms else -1.0),
                               moved, control, flipseen, flipbad))
PY
)
fi
set -- $CA
CARM="$1"; CMIN="$2"; CMOVED="$3"; CCTL="$4"; CFSEEN="$5"; CFBAD="$6"
sed 's#shots/g24_anim.png#shots/g24_det.png#' gate_curanim.txt > gate_curanim2.txt
grun - --scen SCG01EA --pack SCG01EA.pack $BASE --noshroud --script gate_curanim2.txt
gshots shots/g24_det.png
CDET2=$(cmp -s shots/g24_anim.png shots/g24_det.png && echo 1 || echo 0)
if [ "$GRC" != "0" ]; then
  bad "G24 cursor animation: the run itself failed or wrote no shot (GRC=$GRC) -- every number below would have been the previous suite run's"
elif [ "$CARM" = "1" ] && [ "$CMOVED" -ge 200 ] && [ "$CCTL" = "0" ] \
   && [ "$CFSEEN" -ge 40 ] && [ "$CFBAD" = "0" ] && [ "$CDET2" = "1" ]; then
  ok "G24 cursor animation: the deploy arms travel the cartridge's own 200.00 units (min $CMIN) while the centre node holds still, the picture moves by $CMOVED pixels where the frozen control moves 0, both flipbooks match the ROM's index on all $CFSEEN sampled frames, two runs byte-identical"
else
  bad "G24 cursor animation: arms200=$CARM (min $CMIN) movedpx=$CMOVED(want >=200) frozen-control=$CCTL(want 0) flipframes=$CFSEEN(want >=40) flipwrong=$CFBAD(want 0) determinism=$CDET2"
fi

# G26 THE TERRAIN CM TINT LAYER. The console's ground combiner does not draw
# texel * shade; it draws clamp8(((TEXEL0 - tint) * lit + 0x80) >> 8), where the tint
# is a per-corner colour out of CM<SCEN>.IMG. Four independent legs, and any one fails
# the gate:
#
#   1. THE DATA, against the ROM's own numbers. `cmdump` prints the renderer's own
#      per-corner grid -- the same bytes draw_terrain sends -- and it must match the
#      extractor exactly: SCB01EA 876 non-zero corners, per-channel maxima (33,24,20),
#      the full triple landing on grid (52,40) and nowhere else, three corners at
#      R = 33 at (52,40) (54,40) (51,41), and nothing anywhere above the ROM's own
#      48 ceiling. SCG01EA 223 corners, maxima (16,11,3), one corner at R = 16.
#      Counts alone would pass a shifted map; the COORDINATES will not.
#   2. THE COMBINER, against read-back pixels. `cmcombine` draws 180 known
#      (texel, tint, lit) triples through the identical two-stage env chain
#      draw_terrain uses and compares each returned pixel with the RDP expression.
#      The only permitted residual is the 255-vs-256 divide, which can make GL at
#      most ONE level BRIGHTER and never darker; the verb fails itself otherwise.
#   3. THE PICTURE, self-baselining with no stored reference to rot. Shoot the same
#      frame twice, once with `--nocmtint`. Every differing pixel must be DARKER with
#      the layer on (it is a subtract), the change must be confined to the tactical
#      view, and the worst channel loss must land in the 20..30 band the extractor
#      predicts for SCB01EA (28 on a mid-grey 160 texel). A gate that only counted
#      "some pixels changed" would pass a tint applied to the wrong corners.
#   4. THE NO-OP CASE. `--nocmtint` must put the renderer back on the single-stage
#      modulate: cmdump reports live=0 while the pack block is still present, so the
#      fallback is proven to be a real branch and not a coincidence.
cat > gate_cmtint.txt <<'EOS'
cmdump
cmcombine
quit
EOS
CMB=$(./cnc_eyes --scen SCB01EA --pack SCB01EA.pack $BASE --nosound --script gate_cmtint.txt 2>&1 | tee -a "$OUT")
CMG=$(./cnc_eyes --scen SCG01EB --pack SCG01EA.pack $BASE --nosound --script gate_cmtint.txt 2>&1 | tee -a "$OUT")
CMOFF=$(./cnc_eyes --scen SCB01EA --pack SCB01EA.pack $BASE --nosound --nocmtint --script gate_cmtint.txt 2>&1 | tee -a "$OUT")

CB=$(echo "$CMB" | grep -c '^CMDUMP-BEGIN live=1 block=1 corners=4225 nonzero=876 max=33,24,20 over48=0$')
CBFULL=$(echo "$CMB" | grep -c '^CMPEAK|52,40|33,24,20$')
CBR33=$(echo "$CMB" | grep -cE '^CMPEAK\|(52,40|54,40|51,41)\|33,')
CBR33ANY=$(echo "$CMB" | grep -cE '^CMPEAK\|[0-9]+,[0-9]+\|33,')
CG=$(echo "$CMG" | grep -c '^CMDUMP-BEGIN live=1 block=1 corners=4225 nonzero=223 max=16,11,3 over48=0$')
CGR16=$(echo "$CMG" | grep -cE '^CMPEAK\|[0-9]+,[0-9]+\|16,')
CCOMB=$(echo "$CMB" | grep -c '^CMCOMBINE-END cases=180 failures=0 exact=146 gl_brighter_by_1=34 gl_darker_by_1=0 ')
COFF=$(echo "$CMOFF" | grep -c '^CMDUMP-BEGIN live=0 block=1 ')

# The picture, at the strongest tint inside SCB01EA's own playable rect.
cat > gate_cmshot.txt <<'EOS'
tick 40
cam 28 36
zoom max
shot shots/g26_on.png
quit
EOS
# Only the two PICTURE runs go through the wrapper. The three cmdump runs above assert
# off stdout alone, so they cannot score a stale anything and are deliberately untouched.
gbegin shots/g26_on.png shots/g26_off.png
grun - --scen SCB01EA --pack SCB01EA.pack $BASE --nosound --noshroud \
    --script gate_cmshot.txt
sed 's/g26_on/g26_off/' gate_cmshot.txt > gate_cmshot2.txt
grun - --scen SCB01EA --pack SCB01EA.pack $BASE --nosound --noshroud --nocmtint \
    --script gate_cmshot2.txt
gshots shots/g26_on.png shots/g26_off.png
CPIX=$(python3 - <<'PY'
import numpy as np
from PIL import Image
on  = np.asarray(Image.open('shots/g26_on.png').convert('RGB')).astype(int)
off = np.asarray(Image.open('shots/g26_off.png').convert('RGB')).astype(int)
# The tactical view only; the sidebar starts around x=1040 and must not move at all.
view, bar = slice(0, 1040), slice(1040, on.shape[1])
d = (on[:, view] != off[:, view]).any(axis=2)
n = int(d.sum())
delta = (off[:, view] - on[:, view])[d]              # positive = the tint DARKENED it
lighter = int((delta < 0).any(axis=1).sum())         # any channel brighter with the tint on
worst = int(delta.max()) if n else 0
sidebar = int((on[:, bar] != off[:, bar]).any(axis=2).sum())
print("%d %d %d %d" % (n, lighter, worst, sidebar))
PY
)
set -- $CPIX
CN="$1"; CLIGHT="$2"; CWORST="$3"; CBAR="$4"
if [ "$GRC" != "0" ]; then
  bad "G26 terrain CM tint: a picture run failed or wrote no shot (GRC=$GRC)"
elif [ "$CB" = "1" ] && [ "$CBFULL" = "1" ] && [ "$CBR33" = "3" ] && [ "$CBR33ANY" = "3" ] \
   && [ "$CG" = "1" ] && [ "$CGR16" = "1" ] && [ "$CCOMB" = "1" ] && [ "$COFF" = "1" ] \
   && [ -n "$CN" ] && [ "$CN" -ge 100000 ] && [ "$CLIGHT" = "0" ] && [ "$CBAR" = "0" ] \
   && [ "$CWORST" -ge 20 ] && [ "$CWORST" -le 30 ]; then
  ok "G26 terrain CM tint: SCB01EA 876 corners max (33,24,20) with the full triple only at 52,40 and R=33 only at 52,40/54,40/51,41, SCG01EA 223 corners max (16,11,3) with one R=16; the combiner matches the RDP on 180 read-back pixels (146 exact, 34 one level brighter, 0 darker); $CN pixels change and every one is DARKER, worst channel $CWORST, sidebar untouched; --nocmtint returns to the plain modulate"
else
  bad "G26 terrain CM tint: SCB01EA-hdr=$CB full-triple-at-52,40=$CBFULL R33-at-expected=$CBR33(want 3) R33-anywhere=$CBR33ANY(want 3) SCG01EA-hdr=$CG R16=$CGR16(want 1) combiner=$CCOMB nocmtint-falls-back=$COFF changed=$CN(want >=100000) lighter=$CLIGHT(want 0) worst=$CWORST(want 20..30) sidebar-moved=$CBAR(want 0)"
fi

# G27 THE WHOLE CAMPAIGN COMES UP. Earlier four mission packs existed and the
# other thirty-two had never been run once; "the pipeline works, SCG02EA is the worked
# example" was true and said nothing about SCG12EB. gate_campaign.sh runs all 36
# state-machine-reachable missions and checks theater, a non-flat heightmap, objects
# instantiated, a frame really drawn, zero fallback boxes and a clean script. It is a
# separate file because it is a minute long and useful to run on its own; the criteria
# and the reasons each one earns its place are documented at the top of it.
if bash "$GATEDIR/gate_campaign.sh" > /tmp/g27_campaign.txt 2>&1; then
  ok "G27 campaign: $(tail -1 /tmp/g27_campaign.txt)"
else
  bad "G27 campaign: $(tail -1 /tmp/g27_campaign.txt); failures: $(grep -c 'FAIL:' /tmp/g27_campaign.txt) -- $(grep 'FAIL:' /tmp/g27_campaign.txt | head -3 | tr '\n' ' ')"
fi

# G28 VEHICLE AND AIRCRAFT SHADOWS. A wave-6 survey declared these did not exist and
# recorded "no vehicle shadow may be added" as settled; it was disproved with one
# screenshot of the real console. They come from a second node table at ROM 0x9B3E8 that
# nothing in a model points at. Each of the eighteen is a flat quad, colour = PRIM
# (black), alpha = an 8-bit intensity texture, blended over the ground -- so this asserts
# they are drawn, that they ONLY EVER DARKEN, and that --novshadow removes them exactly.
# The "only darken" check is the load-bearing one: a count alone passes just as happily
# on a wrong combiner, a lost blend, or an opaque black quad.
rm -f shots/g28_on.png shots/g28_off.png
printf 'tick 40\ncammode n64\ndist 2400\ncam 53 16\nshot shots/g28_on.png\nquit\n' > /tmp/g28.txt
VS=$(./cnc_eyes --noshroud --scen SCB01EA --pack SCB01EA.pack $BASE --script /tmp/g28.txt 2>&1 \
     | sed -n 's/^draw:.* \([0-9]*\) vehicle shadows$/\1/p')
printf 'tick 40\ncammode n64\ndist 2400\ncam 53 16\nshot shots/g28_off.png\nquit\n' > /tmp/g28b.txt
./cnc_eyes --noshroud --novshadow --scen SCB01EA --pack SCB01EA.pack $BASE --script /tmp/g28b.txt >>"$OUT" 2>&1
if [ ! -s shots/g28_on.png ] || [ ! -s shots/g28_off.png ]; then
  bad "G28 vehicle shadows: a shot was not written"
else
  set -- $(python3 "$GATEDIR/gate_vshadow.py" shots/g28_on.png shots/g28_off.png)
  SN="$1"; SLIGHT="$2"; SWORST="$3"
  if [ -n "$VS" ] && [ "$VS" -ge 2 ] && [ -n "$SN" ] && [ "$SN" -ge 200 ] \
     && [ "$SLIGHT" = "0" ] && [ "$SWORST" -ge 5 ] && [ "$SWORST" -le 90 ]; then
    ok "G28 vehicle shadows: $VS drawn, $SN pixels changed and every one DARKER, worst channel $SWORST; --novshadow removes them exactly"
  else
    bad "G28 vehicle shadows: drawn=$VS(want >=2) changed=$SN(want >=200) lighter=$SLIGHT(want 0) worst=$SWORST(want 5..90)"
  fi
fi

# G30 THE DEPLOYED BINARIES ARE NOT STALE. Runs FIRST in spirit even though it is last
# in the file: every other gate here drives cnc_eyes, so none of them can notice that
# cnc3d -- the binary PLAY.command launches, and therefore the one actually ships --
# was never rebuilt. That has bitten twice in one day: once leaving cnc3d rejecting every
# v13 pack, and once shipping a cursor shadow and a cursor depth fix that were simply
# absent from the menu route. Both were reported as regressions; neither was one. The
# check is on file times rather than pixels, because a picture gate cannot see it.
if bash "$GATEDIR/gate_fresh.sh" > /tmp/g30_fresh.txt 2>&1; then
  ok "G30 binaries fresh: $(tail -1 /tmp/g30_fresh.txt)"
else
  bad "G30 binaries STALE: $(grep -E 'STALE|MISSING' /tmp/g30_fresh.txt | head -2 | tr '\n' ' ')"
fi

# G31 STRUCTURE ANIMATION TRIGGERS. Before this, every structure looped its whole clip
# forever, which played the wrong animation at the wrong time -- the two
# worst cases were reported, and and both were the same bug. These clips hold more than one animation and
# the engine's own state picks which one and where in it.
#
# A: the Construction Yard. Frames 0..49 are the crane idling (constant per-frame node
#    motion in the pack); 50..100 are a twenty-times-larger motion, the yard building
#    something. BState 2 is BSTATE_ACTIVE, which the engine gives the yard for exactly
#    as long as a placed building is under construction. So: while a NUKE goes up the
#    yard must be in the SECOND segment, and once it is finished, back in the first.
CYA=$(./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE --build 6 \
      --script gate_structanim_cy.txt 2>&1 | tee -a "$OUT" | grep -E "^ANIM\|FACT")
CYACT=$(echo "$CYA" | awk -F'[|=]' '$6==2 && $NF>=50' | wc -l | tr -d ' ')
CYIDL=$(echo "$CYA" | awk -F'[|=]' '$6==1 && $NF<50'  | wc -l | tr -d ' ')
CYBAD=$(echo "$CYA" | awk -F'[|=]' '($6==2 && $NF<50) || ($6!=2 && $NF>=50)' | wc -l | tr -d ' ')
if [ "$CYACT" -ge 1 ] && [ "$CYIDL" -ge 1 ] && [ "$CYBAD" = "0" ]; then
  ok "G31a construction yard: $CYACT samples in the build segment while BSTATE_ACTIVE, $CYIDL idling, 0 crossed over"
else
  bad "G31a construction yard: active-in-segment=$CYACT(want >=1) idle-in-segment=$CYIDL(want >=1) crossed=$CYBAD(want 0)"
fi

# B: the War Factory door, which is not a loop and not a counter -- its clip frame is
#    DoorClass::Door_Stage() * (59/9), decoded from the cartridge's own arm. Building a
#    tank must open it 0 -> 9 (frames 0 -> 59), hold it, and close it back to 0, and no
#    sample may ever leave the door band or disagree with the formula.
DR=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --build 6 \
     --script gate_structanim_door.txt 2>&1 | tee -a "$OUT" | grep -E "^ANIM\|WEAP")
DOPEN=$(echo "$DR" | awk -F'[|=]' '$10==9 && $NF>58.9 && $NF<59.1' | wc -l | tr -d ' ')
DMOVE=$(echo "$DR" | awk -F'[|=]' '$10>0 && $10<9' | wc -l | tr -d ' ')
DSHUT=$(echo "$DR" | awk -F'[|=]' '$10==0 && $NF==0' | wc -l | tr -d ' ')
DBAD=$(echo "$DR" | awk -F'[|=]' '{d=$10; f=$NF; e=d*59/9; if (d<0 || f>59.01 || (f-e)>0.02 || (e-f)>0.02) print}' | wc -l | tr -d ' ')
if [ "$DOPEN" -ge 1 ] && [ "$DMOVE" -ge 2 ] && [ "$DSHUT" -ge 1 ] && [ "$DBAD" = "0" ]; then
  ok "G31b war factory door: $DMOVE samples travelling, $DOPEN fully open at frame 59, $DSHUT shut at frame 0, 0 off-formula or past the door band"
else
  bad "G31b war factory door: open=$DOPEN(want >=1) travelling=$DMOVE(want >=2) shut=$DSHUT(want >=1) off-formula=$DBAD(want 0)"
fi

# C: the SAM site. Its arm is decoded whole (RAM 0x8003DE24) and needs no counter of
#    ours: states 2/3/6 draw the launcher tracking, from PrimaryFacing, at frames
#    200..400; anything else draws it rising or lowering from the stage its own
#    Mission_Attack writes, at frames 0..200. Every sample must obey that formula, and
#    an idle SAM with nothing to shoot must HOLD at frame 0 rather than cycle -- which
#    is exactly what it did before it had an arm.
SM=$(./cnc_eyes --scen SCG08EB --pack SCG01EA.pack $BASE \
     --script gate_structanim_sam.txt 2>&1 | tee -a "$OUT" | grep -E "^ANIM\|SAM")
SMN=$(echo "$SM" | grep -c .)
SMBAD=$(echo "$SM" | awk -F'[|=]' '{
    st=$10; sg=$8; fa=$12; f=$NF;
    if (st==2 || st==3 || st==6) { x=fa*0.15625; if (x>40) x-=40; e=(40-x)*5+200 }
    else { if (sg>=33) sg=64-sg; e=sg*12.5 }
    if ((f-e)>0.02 || (e-f)>0.02) print }' | wc -l | tr -d ' ')
SMMOVE=$(echo "$SM" | awk -F'[|=]' '$10==0 && $NF!=0.00' | wc -l | tr -d ' ')
if [ "${SMN:-0}" -ge 8 ] && [ "$SMBAD" = "0" ] && [ "$SMMOVE" = "0" ]; then
  ok "G31c SAM site: $SMN samples over 2360 ticks, every one on the cartridge's formula, and every underground sample held at frame 0"
else
  bad "G31c SAM site: samples=$SMN(want >=8) off-formula=$SMBAD(want 0) moved-while-underground=$SMMOVE(want 0)"
fi

# D: the refinery. Its arm (RAM 0x8003DFD8) emits a SECOND model command, slot 20, for
#    stages 12..28 of the engine's own refinery animation and for no others. The engine
#    stage feeding it needs no normalisation at all: bdata.cpp gives STRUCT_REFINERY five
#    ranges covering stages 0..29 and the arm's thresholds are 6, 12, 19, 24, 29, 30.
PR=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE \
     --script gate_structanim_proc.txt 2>&1 | tee -a "$OUT" | grep -E "^ANIM\|PROC")
PRON=$(echo "$PR" | awk -F'[|=]' '$18>=0' | wc -l | tr -d ' ')
PRBAD=$(echo "$PR" | awk -F'[|=]' '{
    sg=$8; r=$18;
    if (sg>=60) { want=-1 } else { if (sg>=30) sg-=30;
        if (sg<12 || sg==29) want=-1;
        else { v = (sg<24) ? (sg-11) : (29-sg); want = v*10; if (want>80) want=80 } }
    if ((r-want)>0.02 || (want-r)>0.02) print }' | wc -l | tr -d ' ')
PRIDLE=$(echo "$PR" | awk -F'[|=]' '($8<12 || $8==29) && $18>=0' | wc -l | tr -d ' ')
if [ "${PRON:-0}" -ge 1 ] && [ "$PRBAD" = "0" ] && [ "$PRIDLE" = "0" ]; then
  ok "G31d refinery: the second command drew on $PRON samples, every sample on the arm's formula, 0 drawn during idle or the flashing-lights phase"
else
  bad "G31d refinery: rig-drawn=$PRON(want >=1) off-formula=$PRBAD(want 0) drawn-while-idle=$PRIDLE(want 0)"
fi
# WHAT G31d ABOVE CANNOT SEE, said plainly rather than left to be rediscovered: it runs
# SCG90EA, where the refinery is already standing. Measured 39 samples, BStates
# 1, 3 and 4 only, not one sample in BState 0 -- and its awk never reads the bstate field at
# all. The reported "when a Refinery is being Placed, it immediately plays the Unload animation"
# was therefore invisible to it, and it stayed green through the whole of that bug. G31e is
# the arm that reaches the state.

# E: the refinery UNDER CONSTRUCTION. The same arm, one state earlier. A building being
#    placed is BState 0 (BSTATE_CONSTRUCTION) and its stage counter is already running, so
#    the arm's formula wanted the rig on screen while the buildup was still going up.
#    procrig_for now refuses on that state, and this gate asserts both halves of it:
#
#    STATE  the WINDOW (bstate==0 AND stage>=12, the samples where the formula WOULD draw)
#           must be >= 1, and BAD (bstate==0 AND the rig drawn) must be 0. The window floor
#           is the anti-vacuity leg: without it, a scenario or a credit change that stopped
#           producing a placed refinery would give zero samples, zero of them bad, and a
#           green gate measuring nothing. Measured: window 21, bad 21 before the fix, 0
#           after.
#    PIXELS because the state leg reads a dump and the bug is on the draw path. The engine's
#           buildup reports frac=1.00 well before the window opens, so those frames must be
#           byte-identical inside the refinery's own silhouette. Measured: 7 moving frame
#           pairs and 2146 changed pixels before the fix, 0 and 0 after.
rm -f shots/pp_[0-9][0-9].png shots/g31e.log
./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE --noshroud --nominimap --nosidebar \
     --build 6 --w 800 --h 500 --script gate_structanim_procplace.txt > shots/g31e.log 2>&1
PPRC=$?
cat shots/g31e.log >> "$OUT"
PPBOX=$(sed -n 's/^CLICKOBJ|want=PROC[^|]*|silhouette=\([0-9]*\),\([0-9]*\)\.\.\([0-9]*\),\([0-9]*\).*/\1,\2,\3,\4/p' shots/g31e.log | head -1)
PPWIN=$(awk -F'[|=]' '/^ANIM\|PROC/{n++; if($6==0 && $8>=12) print n}' shots/g31e.log)
PPN=$(echo "$PPWIN" | grep -c '[0-9]')
PPBAD=$(awk -F'[|=]' '/^ANIM\|PROC/{if($6==0 && $18>=0) n++} END{print n+0}' shots/g31e.log)
PPF=$(echo "$PPWIN" | head -1); PPL=$(echo "$PPWIN" | tail -1)
if [ "$PPRC" != "0" ]; then
  bad "G31e refinery placement: the run itself failed (exit $PPRC)"
elif [ -z "$PPBOX" ] || [ "${PPN:-0}" -lt 1 ]; then
  bad "G31e refinery placement: this run never put a refinery into BSTATE_CONSTRUCTION -- window samples=$PPN(want >=1) crop=[$PPBOX]"
else
  set -- $(python3 "$GATEDIR/gate_procplace.py" shots pp "$PPBOX" "$PPF" "$PPL" | tr '|' ' ')
  PPPAIRS="$2"; PPCH="$4"
  if [ "${PPBAD:-1}" = "0" ] && [ "${PPPAIRS:-1}" = "0" ]; then
    ok "G31e refinery placement: $PPN samples where the arm's formula was live while the refinery was still going up, the unload rig drawn on 0 of them, and frames $PPF..$PPL byte-identical inside its own silhouette $PPBOX"
  else
    bad "G31e refinery placement: rig-drawn-during-BSTATE_CONSTRUCTION=$PPBAD(want 0 of $PPN) moving-frame-pairs=$PPPAIRS(want 0) changed-pixels=$PPCH(want 0) over frames $PPF..$PPL"
  fi
fi

# G32 THE MOVE-ORDER MARKER IS THE CARTRIDGE'S OWN MODEL. Slot 107, which the cartridge
# names CURSOR_CIRCLES itself (name array ROM 0x1DE924 entry 97): a flat ground wedge plus
# two rings whose only tracks are SCALE, one-shot over 21 frames. It replaced a diamond we
# drew by hand. Ordering a unit to a visible cell must put pixels on the GROUND at that
# cell, and they must be gone once the marker's life is up -- the second half matters
# because a marker that never expires would pass a "did it draw" test forever.
gbegin shots/gm_before.png shots/gm_mid.png shots/gm_after.png
grun - --scen SCG01EC --pack SCG01EA.pack $BASE --script gate_ordermark.txt
# The -s test below used to stand alone here, and a stale file is not empty: this gate
# passed against a binary that drew nothing, reporting 974 pixels of a marker it never
# rendered. gshots is that test, but it only means anything after the gbegin above.
gshots shots/gm_before.png shots/gm_mid.png shots/gm_after.png
if [ "$GRC" != "0" ]; then
  bad "G32 order marker: the run itself failed or wrote no shot (GRC=$GRC)"
else
  set -- $(python3 "$GATEDIR/gate_ordermark.py" shots/gm_before.png shots/gm_mid.png \
           shots/gm_after.png | sed 's/[A-Za-z|]*=/ /g')
  GMID="$1"; GAFT="$2"
  # A RATIO, not an absolute floor, for the second half. The window still contains a LIVE
  # mission -- units drive through it, shroud lifts -- so a handful of changed pixels after
  # the marker expires is the scene, not the marker. What cannot happen if the marker
  # really expired is the two counts being anywhere near each other.
  if [ "${GMID:-0}" -ge 300 ] && [ $((${GAFT:-9999} * 6)) -le "${GMID:-0}" ]; then
    ok "G32 order marker: $GMID pixels of the cartridge's rings on the ground mid-life, down to $GAFT once it expired"
  else
    bad "G32 order marker: mid-life=$GMID(want >=300) after-expiry=$GAFT(want <= mid/6)"
  fi
fi

# G35 SCORCH MARKS, CRATERS AND BUILDING APRONS. The cartridge keeps fifteen strips of
# 24x24 decal art (BIB1..3, CR1..6, SC1..6) and the engine keeps one per CELL, with
# SmudgeData already the console's frame number. Two things are asserted, because either
# alone would pass on a lie: the BRAIN must still export the cells (a silent export
# regression would leave the renderer with nothing to draw and no complaint), and the
# renderer must put pixels on the ground for them, measured as the difference against
# --nosmudge. And those pixels must LOSE GREEN DOMINANCE, because a decal replaces grass
# with earth; a count on its own would pass a blend that lit the whole scene up.
#
# The first version of this gate asserted the pixels got DARKER and failed on its own
# first run, which is the gate working: a dirt apron is LIGHTER than grass and only a
# scorch is darker. Measured over a real frame, the decals take (32,61,27) to (95,81,62)
# and 98.5% of them lose green share. The threshold is 90%.
SMEXP=$(./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE --script gate_smudge_dump.txt \
        2>&1 | tee -a "$OUT" | grep -c "^SMUDGE|")
rm -f shots/sm_on.png shots/sm_off.png
sed 's#shots/sm_x.png#shots/sm_on.png#'  "$GATEDIR/gate_smudge.txt" > /tmp/g35on.txt
sed 's#shots/sm_x.png#shots/sm_off.png#' "$GATEDIR/gate_smudge.txt" > /tmp/g35off.txt
./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE --noshroud --script /tmp/g35on.txt  >>"$OUT" 2>&1
./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE --noshroud --nosmudge \
    --script /tmp/g35off.txt >>"$OUT" 2>&1
if [ ! -s shots/sm_on.png ] || [ ! -s shots/sm_off.png ]; then
  bad "G35 smudges: a shot was not written"
else
  set -- $(python3 "$GATEDIR/gate_smudge.py" shots/sm_off.png shots/sm_on.png \
           | sed 's/[A-Za-z|]*=/ /g')
  SMCH="$1"; SMBR="$2"
  if [ "${SMEXP:-0}" -ge 1 ] && [ "${SMCH:-0}" -ge 2000 ] \
     && [ $((${SMBR:-0} * 10)) -ge $((${SMCH:-1} * 9)) ]; then
    ok "G35 smudges: $SMEXP cells exported, $SMCH pixels of earth on the ground, $SMBR of them less green than the grass they replaced"
  else
    bad "G35 smudges: exported=$SMEXP(want >=1) changed=$SMCH(want >=2000) less-green=$SMBR(want >=90% of changed)"
  fi
fi

# G44 A DYNAMIC LIGHT FADES WHEN ITS SOURCE ENDS. 19 Aug: "When it appears alongside
# the muzzleflash, it instantly disappears again... it should fade out." The lights are
# gathered from the engine's LIVE anim list, so before this they ceased to exist the frame
# their anim did -- for a muzzle flash totally, because GUNFIRE reports stages = 1 and its
# own ramp therefore never ran.
#
# Measured against a lights-OFF run of the same scene, which is the only way to separate
# the light from everything else moving in a live mission. Two claims, because either alone
# would pass on a lie: the light must SURVIVE its source, and it must be strictly FALLING
# while it does. One that lingered at constant brightness is as wrong as one that snaps off.
# BOTH fade dials are set, and set LONG on purpose. This gate is about the MECHANISM --
# that a light outlives its source and falls while it does -- not about the taste values
# that ship (0.45s for blasts and fire, 0.15s for muzzles, which tuned by eye). It failed
# the moment the muzzle got its own dial, because the light it happens to measure is a
# muzzle light and it was still only setting light_fade: exactly the kind of silent
# coupling a gate is supposed to surface. An exaggerated setting keeps the ramp long
# enough to sample at this script's 3-tick spacing.
cat > gfx_g44_lit.cfg <<'CFG'
enabled 1
lights_on 1
light_intensity 4.0
light_radius 12
light_fade 2.0
light_fade_muzzle 2.0
CFG
cat > gfx_g44_dark.cfg <<'CFG'
enabled 1
lights_on 0
CFG
rm -f shots/g44l_*.png shots/g44n_*.png
python3 - <<'PYG' > /tmp/g44_lit.txt
lines=['tick 200']
for i in range(20):
    lines += ['shot shots/g44l_%02d.png' % i, 'tick 3']
lines.append('quit')
print('\n'.join(lines))
PYG
sed 's#g44l_#g44n_#' /tmp/g44_lit.txt > /tmp/g44_dark.txt
./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE --noshroud --gfx gfx_g44_lit.cfg \
    --script /tmp/g44_lit.txt  >>"$OUT" 2>&1
./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE --noshroud --gfx gfx_g44_dark.cfg \
    --script /tmp/g44_dark.txt >>"$OUT" 2>&1
if [ ! -s shots/g44l_00.png ] || [ ! -s shots/g44n_00.png ]; then
  bad "G44 light fade: a shot was not written"
else
  set -- $(python3 "$GATEDIR/gate_lightfade.py" "shots/g44n_%02d.png" "shots/g44l_%02d.png" 20 \
           | sed 's/[A-Za-z|]*=/ /g')
  LFPK="$1"; LFST="$2"
  if [ "${LFPK:-0}" -ge 5000 ] && [ "${LFST:-0}" -ge 4 ]; then
    ok "G44 dynamic light fades: peak $LFPK lit pixels, then $LFST frames of strictly falling light after its source ended"
  else
    bad "G44 light fade: peak=$LFPK(want >=5000) decay-steps=$LFST(want >=4) -- the light is snapping off, not fading"
  fi
fi

# G48 THE JUKEBOX. 20 Aug: "Jukebox is missing and should be implemented." It is
# sounddlg.cpp's SoundControlsClass, and three things have to be true or it is a picture of
# a jukebox rather than one:
#   a  the PAGE opens, from Game Controls, where gamedlg.cpp:433 opens it
#   b  the generated table reached the SCREEN. gate_jukebox.py renders "Act On Instinct"
#      out of the pack's own GRAD6FNT, through db_print's own loop, and requires one row of
#      the list well to equal it PIXEL FOR PIXEL -- exact, not thresholded, because
#      dosopt_gl.h blits the 320x200 plate as one GL_NEAREST quad at an integer scale and
#      nothing in that path resamples. Plus nine rows that all carry a name in the name
#      column and no two of them the same
#   c  PLAY actually starts the chosen track, which the audio engine says out loud
#
# LEG (b) USED TO BE A BINARY GREP WEARING ITS CLOTHES. The comment claimed "Act On
# Instinct appearing in the shot's own pixels" and the code never opened the shot:
# jukebox.png was tested for being non-empty and the name was read out of the COMPILED
# BINARY. Measured on the shipped build: rows=9 named=9 distinct=9, the name matches on
# row 8 (the well scrolls to the track that is playing) with 0 mismatched pixels. The same
# helper pointed at a battlefield shot answers aoi=-1, which is its own negative control.
#
# AND THE GREP IS NOW DELETED OUTRIGHT. It survived as a separate leg on the
# argument that "the table is in the build" is its own claim. It is not a TEST of one:
#
#     JBTRK=$(LC_ALL=C tr -c '[:print:]' '\n' < ./cnc_eyes | grep -c '^Act On Instinct$')
#
# proves a string literal is present in a file. It cannot distinguish a working jukebox
# from a binary that draws "No score tracks are installed." -- a real branch in
# dopt_draw_sound -- because the literal is in the executable either way, put there by the
# compiler and not by anything that runs. Leg (b) reads the same name off the SCREEN,
# glyph for glyph, so the claim is made by a running program and the grep adds nothing but
# a green light. Deleted rather than repaired: there is no repair, the method is the fault.
# The same deletion was made to G56's pack grep for HPADT59, for the same reason.
#
# The shot is DELETED first and the exit status is READ, which is G4's lesson: shots/
# persists between suite runs, so a binary that died at startup used to leave the previous
# run's PNG for this gate to re-measure and pass on.
rm -f shots/jukebox.png shots/g48.log
./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE --w 1280 --h 800 \
      --script gate_jukebox.txt > shots/g48.log 2>&1
JBRC=$?
cat shots/g48.log >> "$OUT"
JBL=$(cat shots/g48.log)
JBPAGE=$(echo "$JBL" | grep -c "click|Sound Controls|.*page=4")
JBPLAY=$(echo "$JBL" | grep -cE "^MUSIC\|play\|")
if [ "$JBRC" != "0" ]; then
  bad "G48 jukebox: the run itself failed (exit $JBRC)"
elif [ ! -s shots/jukebox.png ]; then
  bad "G48 jukebox: the page shot was not written"
else
  set -- $(python3 "$GATEDIR/gate_jukebox.py" shots/jukebox.png dossidebar.pack \
           | sed 's/[A-Za-z|]*=/ /g')
  JBROWS="$1"; JBNAMED="$2"; JBDIST="$3"; JBAOI="$4"; JBMISS="$5"
  if [ "${JBPAGE:-0}" -ge 1 ] && [ "${JBPLAY:-0}" -ge 2 ] \
     && [ "${JBROWS:-0}" -ge 8 ] && [ "${JBNAMED:-0}" = "${JBROWS:-0}" ] \
     && [ "${JBDIST:-0}" = "${JBNAMED:-0}" ] && [ "${JBAOI:--1}" -ge 0 ] \
     && [ "${JBMISS:--1}" = "0" ]; then
    ok "G48 jukebox: the page opens from Game Controls, $JBROWS rows of the score are in the well with $JBDIST different names, 'Act On Instinct' is on row $JBAOI matching the pack's own GRAD6FNT exactly (0 mismatched pixels), and $JBPLAY tracks were started"
  else
    bad "G48 jukebox: page-opened=$JBPAGE(want 1) tracks-started=$JBPLAY(want >=2) rows=$JBROWS(want >=8) named=$JBNAMED(want =rows) distinct=$JBDIST(want =named) aoi-row=$JBAOI(want >=0) glyph-mismatch=$JBMISS(want 0)"
  fi
fi

# G48b THE JUKEBOX HANDS THE SCORE BACK. Reported: playing a track by hand in the
# Jukebox left the next track on the list unplayed when it finished.
#
# G48 above CANNOT go red on this and it is worth being exact about why, because the same
# shape of hole appears three more times in this file. It counts MUSIC|play lines over the
# whole log and wants two: the mission's own opening track plus the one manual pick give it
# two with zero elapsed time, and gate_jukebox.txt has no `tick` after the Play click, so
# that run can never reach an end of track at all. It is a gate for "a track started", and
# the report is about what happens when one FINISHES.
#
# Four legs, and every number in them was measured:
#   1  SELF-CHECK. The picked track's own printed duration D must satisfy D + 10 <= 60, or
#      this gate reports COULD NOT TEST and fails. The run is 900 ticks = 60 s of sim; if
#      the theme table or the row height ever moves this click onto a longer track, the run
#      stops being able to observe the thing the gate guards. Today the click lands on IND2
#      (38 s) and the mission opens on TARGET (173 s), which is why --theme TARGET is on
#      the command line: the opening track must NOT be able to finish, or "the score is
#      still playing" would be satisfied by the track that was already running.
#   2  ADVANCE. At least one further MUSIC|play after those two. Today 1 (JUSTDOIT); on the
#      shipped v0.5.7 binary, 0.
#   3  SPEAKER. The last 10 s of the recorded mix must be above RMS 500. Today 5764; on the
#      shipped binary 0.0, the mixer dead from second 39. THE --audiowav TAP IS LOAD
#      BEARING: measured, WITHOUT it the control arm also shows a single track, because
#      nothing drains the mixer ring, so a gate written without the tap is red on a fixed
#      build. --soundvol 0 is load-bearing the other way: with effects on, battle noise
#      holds the tail at RMS 2000-4000 with the score already dead.
#   4  STOP STILL STOPS. The same script with Stop: exactly one MUSIC|play and a silent
#      tail. The obvious wrong fix -- arming the playlist unconditionally -- passes legs
#      1-3 and fails this one.
rm -f shots/g48b.wav shots/g48b.log shots/g48c.wav shots/g48c.log
./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --w 1280 --h 800 \
     --theme TARGET --soundvol 0 --musicvol 255 --audiowav shots/g48b.wav \
     --script gate_jukebox_next.txt > shots/g48b.log 2>&1
JB2RC=$?
./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --w 1280 --h 800 \
     --theme TARGET --soundvol 0 --musicvol 255 --audiowav shots/g48c.wav \
     --script gate_jukebox_stop.txt > shots/g48c.log 2>&1
JB3RC=$?
cat shots/g48b.log shots/g48c.log >> "$OUT"
JBTRACKS=$(grep -E '^MUSIC\|play\|' shots/g48b.log)
JBCLICKS=$(grep -cE '^OPTIONS\|script\|click\|' shots/g48b.log)
JBN=$(echo "$JBTRACKS" | grep -cE '^MUSIC\|play\|')
JBNAMES=$(echo "$JBTRACKS" | cut -d'|' -f3 | tr '\n' ' ')
JBOPEN=$(echo "$JBTRACKS" | sed -n 1p | cut -d'|' -f3)
JBPICK=$(echo "$JBTRACKS" | sed -n 2p | cut -d'|' -f3)
JBPICKD=$(echo "$JBTRACKS" | sed -n 2p | sed -E 's/.*\|([0-9]+)s\|.*/\1/')
JBRMS=$(python3 "$GATEDIR/gate_rms.py" shots/g48b.wav 10 | cut -d'|' -f2)
JBSN=$(grep -cE '^MUSIC\|play\|' shots/g48c.log)
JBSRMS=$(python3 "$GATEDIR/gate_rms.py" shots/g48c.wav 10 | cut -d'|' -f2)
if [ "$JB2RC" != "0" ] || [ "$JB3RC" != "0" ]; then
  bad "G48b jukebox advance: the run itself failed (exit $JB2RC / $JB3RC)"
elif [ "${JBCLICKS:-0}" != "4" ] || [ "$JBOPEN" != "TARGET" ] || [ -z "$JBPICK" ] \
     || [ "$JBPICK" = "TARGET" ]; then
  bad "G48b jukebox advance: COULD NOT TEST -- dialog clicks=$JBCLICKS(want 4) opening track=$JBOPEN(want TARGET) hand-picked track=$JBPICK(want any other one)"
elif [ $((${JBPICKD:-999} + 10)) -gt 60 ]; then
  bad "G48b jukebox advance: COULD NOT TEST -- the row this click lands on is now $JBPICK at ${JBPICKD}s and the run is 60s, so it cannot reach an end of track. Move the click or lengthen the run."
elif [ "${JBN:-0}" -ge 3 ] && [ "${JBRMS:--1}" -gt 500 ] && [ "${JBSN:-0}" = "1" ] \
     && [ "${JBSRMS:--1}" = "0" ]; then
  ok "G48b jukebox advance: hand-picking $JBPICK (${JBPICKD}s) leaves the score running -- $JBN tracks played [$JBNAMES] and the last 10s of the recorded mix reads RMS $JBRMS; Stop still stops ($JBSN track, RMS $JBSRMS)"
else
  bad "G48b jukebox advance: tracks=$JBN(want >=3) [$JBNAMES] tail-RMS=$JBRMS(want >500) stop-tracks=$JBSN(want 1) stop-tail-RMS=$JBSRMS(want 0)"
fi

# G49 WALLS CAN BE SOLD, AND ONLY THE PLAYER'S OWN. Three assertions off one run: the
# wall goes up, selling it credits the player back, and a wall the SCENARIO painted refuses.
# The refusal is not a limitation, it is the 1995 rule: CellClass::Owner is HOUSE_NONE for
# anything in a scenario's [OVERLAY] and HouseClass::Sell_Wall tests exactly that.
WSL=$(./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE --build 15 --w 1280 --h 800 \
      --script gate_wallsell.txt 2>&1 | tee -a "$OUT")
WSSOLD=$(echo "$WSL" | grep -c "^STRUCT|sell|WALL|cell=49,46|owner=GoodGuy")
WSREFUSED=$(echo "$WSL" | grep -c "^STRUCT|sell|.*no own building here")
WSCRED=$(echo "$WSL" | sed -n 's/^PLAYER|house=GoodGuy|credits=\([0-9]*\).*/\1/p' | tr '\n' ' ')
WSBUILT=$(echo "$WSL" | grep -c "^WALLDUMP|49,46|SBAG")
set -- $WSCRED
if [ "${WSSOLD:-0}" -ge 1 ] && [ "${WSREFUSED:-0}" -ge 1 ] && [ "${WSBUILT:-0}" -ge 1 ] \
   && [ "${1:-0}" -lt "${2:-0}" ]; then
  ok "G49 walls sell: built for $((10000-$1)), refunded $(($2-$1)), and a scenario wall refused"
else
  bad "G49 walls sell: sold=$WSSOLD(want 1) refused=$WSREFUSED(want >=1) built=$WSBUILT(want 1) credits=[$WSCRED]"
fi

# G50 THE FLAMETHROWER'S TONGUE. Reported against v0.5.7 with this gate green: the Flame
# Trooper still showed no real fire when shooting -- a small orange particle was visible,
# but not a true fire effect.
#
# HOW THIS GATE USED TO PASS THROUGH THAT. It asserted `FLTHROW live >= 1` and
# `drawn >= live`. The bug produced exactly ONE particle per shot instead of ten, and one
# satisfies a floor of one. A count that is ten times low was invisible to it, and stayed
# invisible for a whole release. The draw-count leg survives below because it is a real and
# different claim (every live particle reached the draw pass, which is the defect this gate
# was ORIGINALLY written for), but it is no longer the whole gate.
#
# The replacement is five legs off the same single run, four of them engine state:
#   1  TEN PARTICLES PER ANIM. The cartridge's FLAME-* animations have no SHP art, so the
#      engine drops them after one tick; the renderer now clocks them itself from the
#      brain's `biggest` field for stages 0..9. So EFXSPAWN particles must be ten times
#      EFXSEEN anims -- never more, and short only by the one shot that is still in flight
#      when the dump is taken, which can be missing at most nine. Measured: 137 particles
#      for 14 anims after the fix (13 finished shots x 10 plus 7 ticks of one in flight),
#      and 14 for 14 before it. The single FLAME-W anim on this map cannot fail the floor
#      by construction (10*1-9 = 1); FLAME-SE with its fourteen carries the leg, which is
#      why leg 2 asserts that count.
#   2  ANTI-VACUITY. At least ten FLAME-SE anims must actually have been raised. Without
#      it a scenario that stopped producing flamethrower fire would pass every other leg.
#   3  THE GHOST IS ARMED FROM THE BRAIN. Every EFXGHOST|FLAME-* line must show its ceiling
#      at stage 9, which is the brain's `biggest` arriving intact; if the brain stops
#      exporting the field it reads 0, no ghost is armed, and this leg goes red instead of
#      the renderer silently going back to one particle per shot.
#   4  PIXELS AT THE MAN. Counted inside the firing trooper's own silhouette (grown by 40,
#      because the flame is thrown ahead of him) against an --efxscale 0 arm of the same
#      frame: 237 before, 616 after, and 616 of the frame's 637 changed pixels are at the
#      two troopers. A whole-frame count would be meaningless on a map with 300 particles
#      alive in a firefight.
rm -f shots/flame.png shots/flame_off.png shots/g50.log
./cnc_eyes --scen SCG36EA --pack SCG36EA.pack $BASE --noshroud --w 800 --h 600 \
     --script gate_flame.txt > shots/g50.log 2>&1
FLRC=$?
./cnc_eyes --scen SCG36EA --pack SCG36EA.pack $BASE --noshroud --w 800 --h 600 \
     --efxscale 0 --script gate_flame_off.txt >> shots/g50.log 2>&1
FLRC2=$?
cat shots/g50.log >> "$OUT"
FLLIVE=$(sed -n 's/^EFXPART|pool=live|live=\([0-9]*\)|.*/\1/p' shots/g50.log | head -1)
FLDRAWN=$(sed -n 's/^EFXPART|pool=live|live=[0-9]*|chunks=[0-9]*|drawn=\([0-9]*\).*/\1/p' shots/g50.log | head -1)
FLTHROW=$(sed -n 's/^EFXPART|sys=9|FLTHROW|live=\([0-9]*\).*/\1/p' shots/g50.log | head -1)
# +0 ON BOTH SIDES OF EVERY COMPARISON. substr() hands awk a STRING, and "14" < "131" is
# false as a string compare -- which is exactly how the first draft of this leg reported
# zero bad rows on a build that was ten times low. Measured, not reasoned about.
FLRATIO=$(awk -F'|' '
  /^EFXSEEN\|FLAME-/  { seen[$2] = $3 + 0 }
  /^EFXSPAWN\|FLAME-/ { for (i=1;i<=NF;i++) if ($i ~ /^particles=/) part[$2] = substr($i,11) + 0 }
  END { for (a in part) { n++
          if (part[a] > 10*seen[a] || part[a] < 10*seen[a] - 9)
              b = b " " a "(" part[a] " particles for " seen[a] " anims)" }
        printf "%d|%s", n, b }' shots/g50.log)
FLROWS=${FLRATIO%%|*}; FLBAD=${FLRATIO#*|}
FLSEEN=$(sed -n 's/^EFXSEEN|FLAME-SE|\([0-9]*\)/\1/p' shots/g50.log | head -1)
FLGH=$(grep -cE '^EFXGHOST\|FLAME-' shots/g50.log)
FLGHBAD=$(grep -E '^EFXGHOST\|FLAME-' shots/g50.log | grep -vc 'stage=[0-9]*/9|')
FLBOX=$(sed -n 's/^CLICKOBJ|want=E4[^|]*|silhouette=\([0-9]*\),\([0-9]*\)\.\.\([0-9]*\),\([0-9]*\).*/\1,\2,\3,\4/p' shots/g50.log | head -2 | tr '\n' ' ')
if [ "$FLRC" != "0" ] || [ "$FLRC2" != "0" ]; then
  bad "G50 flame tongue: the run itself failed (exit $FLRC / $FLRC2)"
elif [ "${FLROWS:-0}" -lt 3 ] || [ "${FLSEEN:-0}" -lt 10 ] || [ -z "$FLBOX" ]; then
  bad "G50 flame tongue: the scenario did not produce what this measures -- FLAME rows=$FLROWS(want 3) FLAME-SE anims=$FLSEEN(want >=10) trooper boxes=[$FLBOX]"
else
  FLPIX=$(python3 "$GATEDIR/gate_flamepix.py" shots/flame.png shots/flame_off.png $FLBOX)
  FLPMAX=$(echo "$FLPIX" | cut -d'|' -f2); FLPSUM=$(echo "$FLPIX" | cut -d'|' -f4)
  FLPWH=$(echo "$FLPIX" | cut -d'|' -f6)
  if [ -z "$FLBAD" ] && [ "${FLTHROW:-0}" -ge 8 ] && [ "${FLDRAWN:-0}" -ge "${FLLIVE:-999999}" ] \
     && [ "${FLGH:-0}" -ge 1 ] && [ "${FLGHBAD:-1}" = "0" ] \
     && [ "${FLPMAX:-0}" -ge 400 ] && [ $((${FLPSUM:-0} * 10)) -ge $((${FLPWH:-1} * 8)) ]; then
    ok "G50 flame tongue: every FLAME anim emits the cartridge's ten stages, $FLTHROW FLTHROW sprites alive at once, all $FLDRAWN particles reached the draw, and $FLPMAX pixels of fire sit on the firing trooper ($FLPSUM of the frame's $FLPWH changed pixels are at the two troopers)"
  else
    bad "G50 flame tongue: per-anim particle count=[$FLBAD](want 10 each) flthrow-live=$FLTHROW(want >=8) drawn=$FLDRAWN(want >= live=$FLLIVE) ghosts=$FLGH(want >=1) ghosts-not-ending-at-9=$FLGHBAD(want 0) trooper-pixels=$FLPMAX(want >=400) locality=$FLPSUM/$FLPWH"
  fi
fi

# G51 THE TARGET BLUSH, on THREE families of building rather than the one that worked.
#
# an earlier note called this "the white damage flash" for months and it is not one:
# nothing in this engine sets FlashCount on Take_Damage. TechnoClass::Clicked_As_Target
# sets it to 7 on the object an order is aimed at, and FlasherClass::Process lights
# IsBlushing on the ODD values -- so the object whitens on exactly three alternating ticks
# out of seven, and that is what this counts.
#
# WHY IT NEEDED REWRITING, and this is the clearest case in the file. G51 force-fired the
# GoodGuy Power Plant at cell 50,51 on SCG90EA -- the ONE building family in the game whose
# occupy list contains offset 0 AND whose texture book does not overdraw its own mesh. It
# measured the family that works. Meanwhile the Refinery, the Helipad and the Repair Bay
# had no blush at all: 0 changed pixels on every one of the twelve frames. It was reported
# two of them and the gate stayed green.
#
# So: three arms, run through one shell function, each with its own scenario and target.
#   power plant  the ANTI-VACUITY CONTROL. Green before the 20 Aug fixes and after them.
#                If this arm goes red the instrument is broken and the other two mean
#                nothing. Measured: 10904 pixels of blush.
#   refinery     needs BOTH fixes -- the target cell (the order was landing on bare ground
#                at the anchor cell) and the flash clear (the texture book repainted the
#                whitened faces). 0 -> 12534.
#   helipad      isolates the FLASH CLEAR: HPAD's occupy list does contain offset 0, so the
#                order always reached it. 0 -> 6854. Nobody reported this one.
#
# EVERY NUMBER IS TAKEN INSIDE THE TARGET'S OWN SILHOUETTE, read from the CLICKOBJ line of
# the same run. The old helper sampled every second pixel of the whole 800x500 frame at a
# threshold of 500, which cannot tell a building whitening from a harvester driving past.
#
# The --noflash arm is the same run with the blush switched off, so frame N against frame N
# differs by the blush ALONE and every unlit frame reads exactly 0 rather than "under a
# threshold". Both exit statuses are read and the whole numbered series is deleted first
# (G4's lesson): 24 shots are compared pairwise per arm, and a run that died after four of
# them would otherwise be scored against the previous run's other twenty.
blush() {
  # blush LABEL SCEN SCRIPT_ON SCRIPT_OFF PREFIX_ON PREFIX_OFF EXPECTED_TARGET
  _lab=$1; _scen=$2; _son=$3; _soff=$4; _pon=$5; _poff=$6; _want=$7
  rm -f shots/${_pon}_[0-9][0-9].png shots/${_poff}_[0-9][0-9].png shots/blush_$_pon.log
  ./cnc_eyes --scen $_scen --pack SCG01EA.pack $BASE --noshroud --nominimap --nosidebar \
      --w 800 --h 500 --script $_son > shots/blush_$_pon.log 2>&1
  _RA=$?
  ./cnc_eyes --scen $_scen --pack SCG01EA.pack $BASE --noshroud --nominimap --nosidebar \
      --noflash --w 800 --h 500 --script $_soff >> shots/blush_$_pon.log 2>&1
  _RB=$?
  cat shots/blush_$_pon.log >> "$OUT"
  _BOX=$(sed -n 's/^CLICKOBJ|want='"$_want"'[^|]*|silhouette=\([0-9]*\),\([0-9]*\)\.\.\([0-9]*\),\([0-9]*\).*/\1,\2,\3,\4/p' shots/blush_$_pon.log | head -1)
  _TGT=$(sed -n 's/^ORDER|.*|target=\([A-Z0-9]*\)|.*/\1/p' shots/blush_$_pon.log | head -1)
  if [ "$_RA" != "0" ] || [ "$_RB" != "0" ]; then
    bad "G51 target blush, $_lab: the run itself failed (exit $_RA / $_RB)"
  elif [ -z "$_BOX" ]; then
    bad "G51 target blush, $_lab: no $_want silhouette in the run, so there is nothing to measure inside"
  elif [ "$_TGT" != "$_want" ]; then
    bad "G51 target blush, $_lab: the order reached target=$_TGT, not the $_want it aimed at"
  elif [ ! -s shots/${_pon}_03.png ] || [ ! -s shots/${_poff}_03.png ]; then
    bad "G51 target blush, $_lab: a shot was not written"
  else
    _R=$(python3 "$GATEDIR/gate_flash.py" shots $_pon $_poff $_BOX)
    _N=$(echo "$_R" | cut -d'|' -f2);  _T=$(echo "$_R" | cut -d'|' -f4)
    _MIN=$(echo "$_R" | cut -d'|' -f6); _OFF=$(echo "$_R" | cut -d'|' -f8)
    if [ "${_N:-0}" = "3" ] && [ "$_T" = "3,5,7" ] && [ "${_MIN:-0}" -ge 2000 ] \
       && [ "${_OFF:-9999}" -lt 500 ]; then
      ok "G51 target blush, $_lab: lit on ticks $_T inside its own silhouette $_BOX, the three alternating frames FlasherClass gives, smallest blush $_MIN px and the largest unlit frame $_OFF px"
    else
      bad "G51 target blush, $_lab: lit=$_N(want 3) ticks=$_T(want 3,5,7) smallest-lit=$_MIN(want >=2000) largest-unlit=$_OFF(want <500) box=$_BOX"
    fi
  fi
}
blush "power plant" SCG90EA gate_flash.txt      gate_noflash.txt        flash  noflash  NUKE
blush "refinery"    SCG90EA gate_flash_proc.txt gate_flash_proc_off.txt flashp noflashp PROC
blush "helipad"     SCG41EA gate_flash_hpad.txt gate_flash_hpad_off.txt flashh noflashh HPAD

# G52 INFANTRY CAST SHADOWS, AT THEIR OWN FEET, AT THE SUN THE GAME SHIPS WITH.
#
# Reported against v0.5.7, where this was believed fixed and this gate was green:
# infantry sprites still cast no shadow in Enhanced mode. They were
# being cast. They were being cast in the wrong PLACE: fx_emit_sprite_shadow lifted the
# caster's base by `side + 0.02` to keep the card off the ground plane, so the blob started
# a sprite-width away from the man's boots and read as no shadow at all.
#
# THREE REASONS THE OLD GATE COULD NOT GO RED, all three fixed here:
#   1  IT RAN A SUN NEITHER PACKAGE SHIPS. gfx_lowsun.cfg puts the sun at 14 degrees, where
#      a man's shadow is long enough to survive almost any placement error. The shipped
#      default is 52. This version runs BOTH, and the shipped one is the arm that carries
#      the attachment assertion.
#   2  ITS OFF ARM STILL CAST. --nospriteshadow does not switch the sprite casters off:
#      g_spriteShadowPass is set from g_spriteShadowOn and the caster loop then runs
#      regardless, so the flag swaps the sun-turned card for the camera-facing one, which
#      casts a BIGGER shadow. Measured, and recorded as a known gap. The off arm is
#      now a preset with `shadow_on 0`, which really does switch the sun pass off.
#   3  ITS METRIC WAS A WHOLE-FRAME DARKER-PIXEL COUNT with a floor of 400 and no locality,
#      so a tree shadow moving could pay for four missing men. Every number below is taken
#      inside one man's own silhouette, read from the CLICKOBJ lines of the same run.
#
# THE GATE WRITES ITS OWN PRESETS, naming EVERY dial it depends on, and then requires the
# renderer to say it applied all four with none unknown. That closes the other half of the
# hole: `--gfx NOSUCHFILE.cfg` prints a warning and exits 0, so a gate that does not read
# the preset line can measure the defaults and never know.
#
# Measured on SCG01EC's four-man GDI squad at cell 56,55, shipped v0.5.7
# binary -> fixed tree: at 315/52 the per-man band under the boots goes 24,3,6,26 ->
# 88,76,38,52 (two of the four men had no shadow under them at all); at 315/14 the apron
# goes 440 -> 1067.
#
# WHAT IS DELIBERATELY NOT ASSERTED, because it would have to ship red: the LENGTH of the
# blob along the sun bearing. The caster's height is `hgt * g_bbUp[1]`, which at the N64
# camera's 44-degree pitch is 0.719 of the man's real height, so every shadow is about a
# third short. That is a live defect, not a gate gap; it is recorded as a known gap and the
# length leg belongs in gate_sprshadow.py the day it is fixed.
for SSEL in 52 14; do
  SSAZ=315
  printf 'enabled 1\nshadow_on 1\nsun_az %s\nsun_el %s\n' "$SSAZ" "$SSEL" > gfx_g52_on.cfg
  printf 'enabled 1\nshadow_on 0\nsun_az %s\nsun_el %s\n' "$SSAZ" "$SSEL" > gfx_g52_off.cfg
  rm -f shots/sprshadow_on.png shots/sprshadow_off.png shots/g52.log
  ./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE --noshroud --nominimap --nosidebar \
      --gfx gfx_g52_on.cfg --w 1000 --h 650 --script gate_sprshadow.txt > shots/g52.log 2>&1
  SSRC=$?
  ./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE --noshroud --nominimap --nosidebar \
      --gfx gfx_g52_off.cfg --w 1000 --h 650 --script gate_sprshadow_off.txt >> shots/g52.log 2>&1
  SSRC2=$?
  cat shots/g52.log >> "$OUT"
  SSPRE=$(grep -c "FX|preset|gfx_g52_o[nf]*\.cfg: 4 applied, 0 unknown" shots/g52.log)
  SSBOX=$(sed -n 's/^CLICKOBJ|want=E1[^|]*|silhouette=\([0-9]*\),\([0-9]*\)\.\.\([0-9]*\),\([0-9]*\).*/\1,\2,\3,\4/p' shots/g52.log | head -4 | tr '\n' ' ')
  SSNB=$(echo $SSBOX | wc -w | tr -d ' ')
  if [ "$SSRC" != "0" ] || [ "$SSRC2" != "0" ]; then
    bad "G52 infantry shadows at sun $SSAZ/$SSEL: the run itself failed (exit $SSRC / $SSRC2)"
  elif [ "$SSPRE" != "2" ]; then
    bad "G52 infantry shadows at sun $SSAZ/$SSEL: the preset did not apply in both arms -- $SSPRE of 2 runs said '4 applied, 0 unknown', so the numbers below would be the DEFAULT sun and not the one this gate asked for"
  elif [ "$SSNB" != "4" ]; then
    bad "G52 infantry shadows at sun $SSAZ/$SSEL: the squad is not in frame -- $SSNB of 4 silhouettes [$SSBOX]"
  elif [ ! -s shots/sprshadow_on.png ] || [ ! -s shots/sprshadow_off.png ]; then
    bad "G52 infantry shadows at sun $SSAZ/$SSEL: a shot was not written"
  else
    SSR=$(python3 "$GATEDIR/gate_sprshadow.py" shots/sprshadow_on.png shots/sprshadow_off.png $SSBOX)
    SSPER=$(echo "$SSR" | cut -d'|' -f2); SSMIN=$(echo "$SSR" | cut -d'|' -f4)
    SSTOT=$(echo "$SSR" | cut -d'|' -f6); SSAP=$(echo "$SSR" | cut -d'|' -f8)
    if [ "$SSEL" = "52" ]; then
      if [ "${SSMIN:-0}" -ge 35 ] && [ "${SSTOT:-0}" -ge 200 ]; then
        ok "G52 infantry shadows at the SHIPPED sun $SSAZ/$SSEL: all four men have a shadow under their own boots ($SSPER darkened pixels each, $SSTOT together, $SSAP in the whole apron)"
      else
        bad "G52 infantry shadows at the SHIPPED sun $SSAZ/$SSEL: per-man band=[$SSPER] smallest=$SSMIN(want >=35) together=$SSTOT(want >=200) apron=$SSAP"
      fi
    else
      if [ "${SSAP:-0}" -ge 600 ]; then
        ok "G52 infantry shadows at a LOW sun $SSAZ/$SSEL: the squad's blobs darken $SSAP pixels below them (per-man band $SSPER)"
      else
        bad "G52 infantry shadows at a LOW sun $SSAZ/$SSEL: apron=$SSAP(want >=600) per-man band=[$SSPER]"
      fi
    fi
  fi
done

# G53 THE WINDOW-PINNED HUD PLATES: OPTIONS, CREDITS and the SIDEBAR handle. Five claims
# off one run, because each of the three reports fails independently:
#   a  all three plates HIT-TEST, where sbhit used to answer "none" for every one of them
#   b  the POINTER belongs to the panel over OPTIONS (onmap=0, the 1995 sprite drawn),
#      which is the actual content of "the mouse cursor goes behind it"
#   c  clicking OPTIONS opens the pause dialog through the same route a player uses
#   d  the drawer SLIDES: g_dbX0 reaches the window's right edge and comes back
#   e  the handle is still hit-testable while the bar is hidden, so it can be reopened
# --gfx IS LOAD BEARING HERE, added. gate_hudchrome opens the pause dialog,
# and the dialog applies the whole visual state on the way in. With the chain OFF it reads
# that state as CLASSIC, which correctly means the 1995 DOS bar, and the new HUD this gate
# is testing vanished mid-run: the drawer slid to dbx0=960 instead of 1280 and two of the
# five claims below failed. The first fix made CNC3D_HUD a hard override so it could not
# be stomped, and that broke the player's own checkbox on a launcher that sets the
# variable. So the gate states its intent instead: chain on, hence ENHANCED, hence the
# HUD it asked for survives its own dialog.
HCL=$(CNC3D_HUD=new ./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE --w 1280 --h 960 \
      --gfx --script gate_hudchrome.txt 2>&1 | tee -a "$OUT")
# THREE PLATES ARE THREE CLAIMS, and one count over a three-alternative regex was not
# making any of them. It asked for four matches in TOTAL, and the run produces exactly
# four -- OPTIONS once, CREDITS once, and the SIDEBAR handle twice, because gate_hudchrome
# probes it again while the drawer is hidden, which is leg (e). So the aggregate was
# satisfied just as well by one plate answering four times while the other two regressed
# to "none", which is the state this gate was written to catch (sb_over_panel used to call
# every one of them part of the map). Each label is now counted on its own, and the
# handle's two probes are asserted as two: hit-testable with the bar OUT and again with it
# HIDDEN, or the drawer is a one-way door.
HCOPTH=$(echo "$HCL" | grep -c "^SBHIT|160,17|options")
HCCRDH=$(echo "$HCL" | grep -c "^SBHIT|800,17|credits")
HCSBH=$(echo "$HCL" | grep -c "^SBHIT|1120,17|sidebar")
HCPTR=$(echo "$HCL" | grep -c "^PANELPROBE|160,17|.*onmap=0|sprite=1")
HCOPT=$(echo "$HCL" | grep -c "^OPTIONS|open")
HCHIDE=$(echo "$HCL" | grep -c "^SBSLIDE|x=160|target=160|dbx0=1280")
HCSHOW=$(echo "$HCL" | grep -c "^SBSLIDE|x=0|target=0|dbx0=960")
if [ "${HCOPTH:-0}" -ge 1 ] && [ "${HCCRDH:-0}" -ge 1 ] && [ "${HCSBH:-0}" -ge 2 ] \
   && [ "${HCPTR:-0}" -ge 1 ] && [ "${HCOPT:-0}" -ge 1 ] \
   && [ "${HCHIDE:-0}" -ge 1 ] && [ "${HCSHOW:-0}" -ge 1 ]; then
  ok "G53 HUD plates: OPTIONS and CREDITS each hit-test, the SIDEBAR handle hit-tests $HCSBH times (out and hidden), the pointer owns OPTIONS, it opens the dialog, and the drawer slides out to 1280 and back to 960"
else
  bad "G53 HUD plates: options-hit=$HCOPTH(want >=1) credits-hit=$HCCRDH(want >=1) sidebar-hit=$HCSBH(want >=2, one of them with the bar hidden) pointer=$HCPTR(want 1) options-opened=$HCOPT(want 1) hidden=$HCHIDE(want 1) shown=$HCSHOW(want 1)"
fi

# G54 SAVE AND LOAD ROUND TRIP. Three assertions off one run: the save reports a payload
# and a frame, the load puts the frame back, and the OBJECT DUMP after the load is
# byte-identical to the one before the save. The last one is the real check -- a load that
# restored the frame counter and nothing else would pass the first two.
rm -rf shots/savetest && mkdir -p shots/savetest
SVL=$(./cnc_eyes --scen SCG01EA --pack SCG01EA.pack $BASE --savedir shots/savetest \
      --w 640 --h 400 --script gate_saveload.txt 2>&1 | tee -a "$OUT")
SVSAVE=$(echo "$SVL" | grep -c "^SAVE|slot=0|.*|frame=300|scen=SCG01EA")
SVLOAD=$(echo "$SVL" | grep -c "^LOAD|slot=0|frame=300|scen=SCG01EA")
SVSLOT=$(echo "$SVL" | grep -c "^SLOT|0|")
# the three dumps, in order: pre-save, post-advance, post-load
echo "$SVL" | awk '/^OBJDUMP-BEGIN/{n++} n==1&&/^(OBJ\||TIB\||WALL\|)/{print}' > /tmp/g54_a.txt
echo "$SVL" | awk '/^OBJDUMP-BEGIN/{n++} n==3&&/^(OBJ\||TIB\||WALL\|)/{print}' > /tmp/g54_c.txt
SVLINES=$(wc -l < /tmp/g54_a.txt | tr -d ' ')
if [ "${SVSAVE:-0}" -ge 1 ] && [ "${SVLOAD:-0}" -ge 1 ] && [ "${SVSLOT:-0}" -ge 1 ] \
   && [ "${SVLINES:-0}" -ge 50 ] && diff -q /tmp/g54_a.txt /tmp/g54_c.txt >/dev/null 2>&1; then
  ok "G54 save/load: round trip exact, $SVLINES dump lines identical across save -> +200 ticks -> load"
else
  bad "G54 save/load: saved=$SVSAVE(want 1) loaded=$SVLOAD(want 1) listed=$SVSLOT(want 1) lines=$SVLINES(want >=50) dump-identical=$(diff -q /tmp/g54_a.txt /tmp/g54_c.txt >/dev/null 2>&1 && echo yes || echo NO)"
fi

# G55 THE SCREEN-EDGE SCROLL ARROWS. Two claims: the model is in the PACK (a renderer that
# can draw it against a pack that cannot supply it draws nothing, which is how this stayed
# invisible), and the picture at three different edges differs from the picture mid-map AND
# from each other -- the second half is what proves the eight headings are applied, since one
# arrow drawn at one rotation would pass a "does anything change" test.
# strings(1) is not on PATH on Windows and 2>/dev/null hides the failure, so
# this reports 0 for a string that is present. Same trap as G43/G46; see the note at
# the G46 button check. tr is coreutils on both platforms and the shell redirect
# resolves the .exe by itself. $RUNDIR is optional (line 27 defaults it), so naming
# the pack plainly is also what the surrounding lines already do.
EAV=$(LC_ALL=C tr -c '[:print:]' '\n' < SCG01EA.pack 2>/dev/null | grep -c '^CUR0E$')
# The pack read carried BOTH of the faults G43 and G46 document, and neither is theirs
# alone: strings(1) is not on PATH on Windows, where 2>/dev/null turned "command
# not found" into a silent count of 0, and $RUNDIR is OPTIONAL (line 27 defaults it), so
# "$RUNDIR/SCG01EA.pack" read /SCG01EA.pack -- a file that does not exist -- on every run
# started by hand rather than by tools/release.sh. Either way this line reported 0 variants
# without ever having looked at a pack. tr is in coreutils on both platforms, the shell
# redirect needs no .exe suffix, and this script has already cd'd to the folder the pack is
# in, so the name is plain -- exactly as the --pack argument on the run below names it.
# Delete first and read the exit status, G4's lesson: shots/ persists between suite runs,
# so a binary that died at startup used to leave the previous run's PNGs here for the
# pixdiffs below to re-measure and pass on.
rm -f shots/edge_mid.png shots/edge_w.png shots/edge_n.png shots/edge_nw.png
./cnc_eyes --scen SCG01EA --pack SCG01EA.pack $BASE --noshroud --nominimap --nosidebar \
    --w 800 --h 500 --script gate_edgearrow.txt >>"$OUT" 2>&1
EARC=$?
if [ "$EARC" != "0" ]; then
  bad "G55 edge arrows: the run itself failed (exit $EARC)"
elif [ ! -s shots/edge_mid.png ] || [ ! -s shots/edge_w.png ] || \
     [ ! -s shots/edge_n.png ] || [ ! -s shots/edge_nw.png ]; then
  bad "G55 edge arrows: a shot was not written"
else
  EAW=$(python3 "$GATEDIR/gate_pixdiff.py" shots/edge_mid.png shots/edge_w.png)
  EAN=$(python3 "$GATEDIR/gate_pixdiff.py" shots/edge_mid.png shots/edge_n.png)
  EANW=$(python3 "$GATEDIR/gate_pixdiff.py" shots/edge_w.png shots/edge_nw.png)
  if [ "${EAV:-0}" -ge 1 ] && [ "${EAW:-0}" -ge 100 ] && [ "${EAN:-0}" -ge 100 ] \
     && [ "${EANW:-0}" -ge 100 ]; then
    ok "G55 edge arrows: CUR0E is in the pack, and west/north/north-west each differ from mid-map and from each other ($EAW/$EAN/$EANW px)"
  else
    bad "G55 edge arrows: in-pack=$EAV(want 1) west=$EAW north=$EAN nw-vs-w=$EANW (each want >=100)"
  fi
fi

# G56 THE OTHER FIVE TEXTURE BOOKS, measured on a building that HAS one.
#
# The Construction Yard's fans were the first; the other five are the refinery's warning
# triangle and chimney strip, the HELIPAD's landing markers, the repair bay's lamp and the
# silo's fill strip. The claim is that the book reaches the screen, and it is measured
# against --notexbook rather than across frames, because a unit walking past would also
# change pixels across frames.
#
# TWO THINGS WERE WRONG WITH THIS GATE AND BOTH ARE FIXED HERE:
#
#   1  ONE LEG WAS A PACK GREP, now DELETED outright rather than repaired:
#
#          TBV=$(LC_ALL=C tr -c '[:print:]' '\n' < SCG01EA.pack | grep -c '^HPADT59$')
#
#      That says a name is in a file. It says nothing about a running program: the slot
#      table can be complete in the pack and never be read, or be read and drawn at the
#      wrong period, and the count is 1 either way. There is no way to repair a method
#      whose whole content is "the baker wrote a string", so it is gone. Same reasoning,
#      same day, as G48's grep of the binary for "Act On Instinct".
#
#   2  THE PIXEL LEG RAN ON SCG90EA, WHICH HAS NO HELIPAD ON IT, and counted the whole
#      frame. It announced "the 60-slot helipad table" in its PASS text while measuring
#      something else entirely. It now runs SCG41EA with the camera on the GoodGuy helipad
#      at 54,58 and counts inside that helipad's own silhouette, read from the CLICKOBJ
#      line of the same run. Measured: 412 pixels inside the box change when the books go
#      off; with --notexbook forced on BOTH arms it reads 0, which is this gate's own red.
#
# Delete first and read both exit statuses (G4). This gate measures ONE shot against the
# OTHER, so a stale pair left by an earlier run agrees with itself perfectly and passes.
rm -f shots/texbook_on.png shots/texbook_off.png shots/g56.log
./cnc_eyes --scen SCG41EA --pack SCG01EA.pack $BASE --noshroud --nominimap --nosidebar \
    --w 900 --h 600 --script gate_texbook.txt > shots/g56.log 2>&1
TBRC=$?
./cnc_eyes --scen SCG41EA --pack SCG01EA.pack $BASE --noshroud --nominimap --nosidebar \
    --notexbook --w 900 --h 600 --script gate_texbook_off.txt >> shots/g56.log 2>&1
TBRC2=$?
cat shots/g56.log >> "$OUT"
TBBOX=$(sed -n 's/^CLICKOBJ|want=HPAD[^|]*|silhouette=\([0-9]*\),\([0-9]*\)\.\.\([0-9]*\),\([0-9]*\).*/\1,\2,\3,\4/p' shots/g56.log | head -1)
if [ "$TBRC" != "0" ] || [ "$TBRC2" != "0" ]; then
  bad "G56 texture books: the run itself failed (exit $TBRC / $TBRC2)"
elif [ -z "$TBBOX" ]; then
  bad "G56 texture books: no helipad silhouette in the run, so there is nothing to measure inside"
elif [ ! -s shots/texbook_on.png ] || [ ! -s shots/texbook_off.png ]; then
  bad "G56 texture books: a shot was not written"
else
  TBD=$(python3 "$GATEDIR/gate_pixdiff.py" shots/texbook_on.png shots/texbook_off.png $TBBOX)
  if [ "${TBD:-0}" -ge 100 ]; then
    ok "G56 texture books: $TBD pixels inside the helipad's own silhouette $TBBOX change when the books are switched off"
  else
    bad "G56 texture books: pixels-changed-inside-the-helipad=$TBD(want >=100) box=$TBBOX"
  fi
fi

# G47 THE CONSTRUCTION YARD'S CRANE. 20 Aug: "the Construction Yard is not playing
# its Crane Moving animation, when a building is being placed." It was not, and the cause was
# the v0.5.5 fix for the detached fan plate, which disabled the yard's clip ENTIRELY. That
# clip has two segments -- the arm table gives FACT seg1 = 50, so 0..49 idle and 50..100 the
# yard building something -- and killing both was too wide a rule.
#
# So this gate asserts BOTH halves, because either alone can pass while the other is broken:
# the yard is STILL while idle (that is what put the plate back on the vault) and it MOVES
# while a placed building is under construction. The engine separates the two cleanly:
# measured on SCG01EC, an idle yard reports doing=1 and it is doing=2 for exactly the span
# something is going up.
# Delete first and read the exit status (G4). This gate's idle leg asserts that two frames
# are nearly IDENTICAL, so a stale pair from an earlier run is the most comfortable thing
# it could possibly be handed: byte-identical PNGs score 0 moved pixels and sail through
# the half of the test that guards the fan plate.
rm -f shots/crane_i0.png shots/crane_i1.png shots/crane_a0.png shots/crane_a1.png
./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE --noshroud --nominimap --nosidebar \
    --w 900 --h 600 --script gate_crane.txt >>"$OUT" 2>&1
CRRC=$?
if [ "$CRRC" != "0" ]; then
  bad "G47 CY crane: the run itself failed (exit $CRRC)"
elif [ ! -s shots/crane_i0.png ] || [ ! -s shots/crane_i1.png ] || \
     [ ! -s shots/crane_a0.png ] || [ ! -s shots/crane_a1.png ]; then
  bad "G47 CY crane: a shot was not written"
else
  CRI=$(python3 "$GATEDIR/gate_crane.py" shots/crane_i0.png shots/crane_i1.png | cut -d'|' -f2)
  CRA=$(python3 "$GATEDIR/gate_crane.py" shots/crane_a0.png shots/crane_a1.png | cut -d'|' -f2)
  if [ "${CRI:-9999}" -le 60 ] && [ "${CRA:-0}" -ge 300 ]; then
    ok "G47 CY crane: $CRI pixels move while the yard idles, $CRA while it builds"
  else
    bad "G47 CY crane: idle-moved=$CRI(want <=60) building-moved=$CRA(want >=300)"
  fi
fi

# G46 SPECIAL OPS. 19 Aug: "Main menu should have a button called 'Special Ops',
# which should list the missions" -- "All of the Special Ops missions and the Covert Ops
# missions (Special Ops had some N64 exclusive missions)." Three claims, because a menu
# that lists a mission it cannot start is worse than no menu:
#
#   a  the BUTTON exists, which is the app's own menu item table
#   b  every listed mission has BOTH an INI and a pack, which is the rule specops_scan
#      applies -- a row with no pack is not offered
#   c  three of them, one Covert Operations and two cartridge-side, actually LOAD and
#      draw a world rather than an empty map
# strings(1) is not portable enough for this, and the reason is duller than it first looked.
# On the Windows host strings is simply NOT ON PATH: the only copy is
# C:/msys64/mingw32/bin/strings.exe and nothing puts it there. Invoked by its full path it
# reads a 32-bit mingw PE perfectly, 64852 lines, and it finds this very string. So neither
# the PE format nor the toolchain is at fault. What made this a SILENT wrong answer is the
# 2>/dev/null: it swallowed "strings: command not found", grep counted an empty stream, and
# the gate reported a missing button it had never once looked for. The .exe suffix is a
# second, independent trap for any NATIVE tool, which gets no MSYS fallback on its
# arguments. tr is in coreutils on both platforms and the shell redirect resolves the .exe
# by itself, so this line needs neither the tool nor the suffix.
SOBTN=$(LC_ALL=C tr -c '[:print:]' '\n' < ./cnc3d 2>/dev/null | grep -c '^Special Ops$')
SOINI=0; SOPACK=0; SOMISS=""
for f in missions/SC[BG][2-8][0-9]E[A-C].INI; do
  [ -e "$f" ] || continue
  b=$(basename "$f" .INI)
  SOINI=$((SOINI+1))
  if [ -f "$b.pack" ]; then SOPACK=$((SOPACK+1)); else SOMISS="$SOMISS $b"; fi
done
SODRAW=0
for s in SCG22EA SCG30EA SCB70EA; do
  [ -f "$s.pack" ] || continue
  # The `shot` is what forces a render. In --script mode game_run_script ticks the sim
  # and reports coverage but never calls draw_frame, so a tick-only script honestly
  # reports "0 meshes" for a mission that loaded perfectly. G28 carries a shot line for
  # the same reason; this gate was written without one and reported loaded=0 for three
  # missions that draw 78, 84 and 129 meshes.
  printf 'tick 20\nshot /tmp/gate_specops.png\nquit\n' > /tmp/gate_specops.txt
  M=$(./cnc_eyes --scen $s --pack $s.pack $BASE --noshroud --w 320 --h 200 \
      --script /tmp/gate_specops.txt 2>&1 | tee -a "$OUT" \
      | grep -m1 '^draw:' | sed 's/draw: \([0-9]*\) meshes.*/\1/')
  # An EMPTY capture and a real zero are different failures, and "${M:-0}" would report
  # them identically -- which is exactly what made loaded=0 above ambiguous.
  [ -z "$M" ] && SOMISS="$SOMISS $s(no-draw-line)"
  [ "${M:-0}" -ge 20 ] && SODRAW=$((SODRAW+1))
done
if [ "${SOBTN:-0}" -ge 1 ] && [ "${SOINI:-0}" -ge 20 ] && [ "$SOINI" = "$SOPACK" ] \
   && [ "${SODRAW:-0}" -eq 3 ]; then
  ok "G46 Special Ops: the menu carries the button, $SOINI missions installed with a pack each, 3 of 3 sampled load and draw"
else
  bad "G46 Special Ops: button=$SOBTN(want 1) inis=$SOINI(want >=20) with-pack=$SOPACK(want $SOINI, missing:$SOMISS) loaded=$SODRAW(want 3)"
fi

# G45 THE BUILDING PAD. 19 Aug: "The Conyard still has a missing texture." The
# missing texture was the ground. A building is a FLAT model standing at ONE height, and
# the test map stands the GDI base on the beach ramp -- corner heights 125/113/101/90
# across the Construction Yard's three cells -- so the whole uphill quarter of its
# concrete apron was under the terrain and the fan deck read as a box floating on grass.
# It was never one building: the power plants and the silos on the same ramp were cut
# off too. Two claims, because either alone can pass while the fix does nothing. First
# the pad must LEVEL something -- the engine's own count of corners it raised. Then the
# apron must actually be THERE, measured against the same shot rendered with
# --noflatpads, which is the picture that was reported.
# THIS GATE WAS THE WORST CASE OF THE TRAP G4 DOCUMENTS, because it did not merely leave
# a stale shot lying about, it RENAMED one into place. gate_pads.txt writes shots/pad.png
# and the mv below is what turns that into pad_on.png; silenced with 2>/dev/null, a run
# that wrote nothing at all left the PREVIOUS suite's pad_on.png standing, and the apron
# measurement then scored last week's picture and passed without a word. All three shots
# are deleted first, all three exit statuses are read, and the mv's own complaint goes to
# the log instead of to /dev/null.
rm -f shots/pad.png shots/pad_on.png shots/pad_off.png shots/g45.log
./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud --nominimap --nosidebar \
    --w 1280 --h 800 --script gate_pads.txt >>"$OUT" 2>&1
PADRC=$?
mv -f shots/pad.png shots/pad_on.png 2>>"$OUT"
# The second render is 320x200 and exists only for the engine's own PAD| report, so it is
# read from a file rather than through a pipe: $? after a pipeline is the last command's,
# which would have been grep's opinion and not the binary's.
./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud --nominimap \
    --nosidebar --w 320 --h 200 --script gate_pads.txt > shots/g45.log 2>&1
PADRC2=$?
cat shots/g45.log >> "$OUT"
PADRPT=$(grep -m1 '^PAD|' shots/g45.log)
sed 's#shots/pad.png#shots/pad_off.png#' gate_pads.txt > /tmp/gate_pads_off.txt
./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud --nominimap --nosidebar \
    --noflatpads --w 1280 --h 800 --script /tmp/gate_pads_off.txt >>"$OUT" 2>&1
PADRC3=$?
PADC=$(echo "$PADRPT" | sed 's/.*corners-raised=\([0-9]*\).*/\1/')
if [ "$PADRC" != "0" ] || [ "$PADRC2" != "0" ] || [ "$PADRC3" != "0" ]; then
  bad "G45 building pad: the run itself failed (exit $PADRC / $PADRC2 / $PADRC3)"
elif [ ! -s shots/pad_on.png ] || [ ! -s shots/pad_off.png ]; then
  bad "G45 building pad: a shot was not written"
else
  PADON=$(python3 "$GATEDIR/gate_pads.py" shots/pad_on.png | cut -d'|' -f2)
  PADOFF=$(python3 "$GATEDIR/gate_pads.py" shots/pad_off.png | cut -d'|' -f2)
  if [ "${PADC:-0}" -ge 8 ] && [ "${PADON:-9999}" -le 400 ] && [ "${PADOFF:-0}" -ge 800 ]; then
    ok "G45 building pad: $PADC corners levelled; apron window is $PADON grass pixels with the pad, $PADOFF without"
  else
    bad "G45 building pad: corners-raised=$PADC(want >=8) grass-with-pad=$PADON(want <=400) grass-without=$PADOFF(want >=800) -- $PADRPT"
  fi
fi

# G43 THE CONSTRUCTION YARD'S FANS. Two things, because either alone has already let a
# broken build reach testing. First the PACK must carry the three mesh variants the
# cartridge's texture book produces -- 35 of the 36 packs did not, the day the renderer
# learned to draw them. Then the SCREEN must actually change across consecutive frames,
# which is the only thing that separates "the fans turn" from "something turns".
# The same missing-strings trap as G46, plus two more faults on this one line.
# $RUNDIR is OPTIONAL (line 27 defaults it), so with RUNDIR unset this read /SCG01EA.pack
# and reported 0 variants on BOTH platforms; the --pack argument on this gate's own run
# names that same file plainly.
# And '_t[0-9]' counted 4 by swallowing a binary fragment '_t6)'. The renderer never
# resolves those names: cnc_eyes.cpp:8644 builds "%sT%d", so FACTT0..2 is what actually
# gets looked up, and anchoring on it measures the real thing. Shipped pack: exactly 3.
#
# THIS COMMENT USED TO ADD THAT THIS WAS "the only place in the whole script that
# dereferenced it as a path", AND BY v0.5.7 THAT WAS FALSE. It was true the hour it was
# written -- 1fdf28d cleared the last one -- and it stopped being true the same day, when
# G55 (5b2f290) and G56 (f2f63e7) were written from the older pattern and put
# strings "$RUNDIR/SCG01EA.pack" 2>/dev/null back into the file, twice. The note written to
# stop the fault spreading was thereafter read as proof that it could not, and both fresh
# copies shipped. Both are corrected now, and so is G48's read of the binary; there is no
# strings(1) and no $RUNDIR-as-a-path left in this script. The reasoning above is worth
# keeping and the census was not: a comment that counts occurrences elsewhere in its own
# file goes stale the moment somebody adds one, and nothing checks it.
FANV=$(LC_ALL=C tr -c '[:print:]' '\n' < SCG01EA.pack 2>/dev/null | grep -c '^FACTT[0-9]$')
# G43 is the half-guarded case: it deleted its shots but never read the status, and its
# emptiness test named fan0 and fan2 while gate_fans.py reads all THREE. A run that wrote
# the outer two and died before fan1 passed the guard and measured a stale middle frame.
gbegin shots/fan0.png shots/fan1.png shots/fan2.png
grun - --scen SCG01EC --pack SCG01EA.pack $BASE --noshroud --script gate_fans.txt
gshots shots/fan0.png shots/fan1.png shots/fan2.png
if [ "$GRC" != "0" ]; then
  bad "G43 fans: the run itself failed or wrote no shot (GRC=$GRC)"
else
  set -- $(python3 "$GATEDIR/gate_fans.py" shots/fan0.png shots/fan1.png shots/fan2.png \
           | sed 's/[A-Za-z|]*=/ /g')
  FANPX="$1"
  # An upper bound as well as a lower one, inside gate_fans.py's own box: the fans are a
  # small feature and the measured answer is 340, so a count near the box's whole area
  # would mean something else walked through it and the gate is reading the wrong thing.
  if [ "${FANV:-0}" -ge 3 ] && [ "${FANPX:-0}" -ge 100 ] && [ "${FANPX:-0}" -le 900 ]; then
    ok "G43 CY fans: $FANV variant meshes in the pack, $FANPX pixels turn across three frames"
  else
    bad "G43 CY fans: pack variants=$FANV(want >=3) pixels-changed=$FANPX(want 100..900)"
  fi
fi

# G33 INFANTRY IDLE ANIMATIONS. idata.cpp gives every type a DO_IDLE1 and DO_IDLE2 of 16
# frames (10 and 6 for civilians), and infantry.cpp plays them from the guard state machine
# with a rate path no other Do gets -- but bake_dosinfantry.py never extracted rows 9 and
# 10, so for months a standing man could only ever wear STAND. Men left alone must now be
# seen in an IDLE strip, and every frame they pick must be inside it.
IDL=$(./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE --script gate_infidle.txt \
      2>&1 | tee -a "$OUT" | grep -E "^ANIM\|")
IDN=$(echo "$IDL" | awk -F'|' '$9 ~ /_IDLE[12]#/' | wc -l | tr -d ' ')
IDT=$(echo "$IDL" | awk -F'|' '$9 ~ /_IDLE[12]#/ {print $9}' | sed 's/#.*//' | sort -u | tr '\n' ' ')
IDBAD=$(echo "$IDL" | awk -F'|' '$9 ~ /_IDLE[12]#/ && ($12 < 0 || $11 < 0)' | wc -l | tr -d ' ')
if [ "${IDN:-0}" -ge 5 ] && [ "$IDBAD" = "0" ]; then
  ok "G33 infantry idles: $IDN draws in an IDLE strip ($IDT), 0 with an out-of-range stage or frame"
else
  bad "G33 infantry idles: idle-draws=$IDN(want >=5) bad-stage-or-frame=$IDBAD(want 0)"
fi

# G34 CONSTRUCTION YARD FANS SPIN. the console capture proved the vent grilles rotate,
# refuting an earlier RE conclusion that they were static. They are a two-triangle part, so
# the motion is the grille TEXTURE turning under a fixed triangle. Two frames a few ticks
# apart must differ in the fan region.
gbegin shots/cyfan_a.png shots/cyfan_b.png
grun - --noshroud --scen SCG01EC --pack SCG01EA.pack $BASE --script gate_cyfans.txt
gshots shots/cyfan_a.png shots/cyfan_b.png
if [ "$GRC" != "0" ]; then
  bad "G34 CY fans: the run itself failed or wrote no shot (GRC=$GRC)"
else
  FANPX=$(python3 "$GATEDIR/gate_cyfans.py" shots/cyfan_a.png shots/cyfan_b.png | cut -d'|' -f2)
  if [ "${FANPX:-0}" -ge 40 ]; then
    ok "G34 CY fans spin: $FANPX pixels of the grille turned between two ticks"
  else
    bad "G34 CY fans: only $FANPX pixels changed (want >=40) -- the fans are not turning"
  fi
fi

# =====================================================================================
# G36 THE TIER 2 PRESENTATION CHAIN. Six separate claims, each measured in pixels off
# shots this gate takes, because every one of them is a way the chain could look like it
# was working while doing nothing:
#
#   a  it RUNS            the picture changes when the chain is switched on
#   b  THE SEAM HOLDS     and the 1995 sidebar does not change by one pixel
#   c  it is DETERMINISTIC two --shot runs are byte identical with the chain ON, which
#                          is what keeps the rest of this suite's --shot gates possible
#                          on any future day the chain is on by default
#   d  THE SUN IS A SUN   shadows appear AND the cartridge's own four shadow mechanisms
#                          disappear (pixels move in BOTH directions), and lowering the
#                          sun lengthens them. A static tint passes (a) and fails this.
#   e  THE FILE ROUND TRIPS  save -> load -> save is byte identical. That file is the
#                          deliverable of the whole feature; if it drops a dial, the
#                          tuning session it came from is lost.
#   f  BILINEAR REACHES THE UI  with the whole chain OFF, the toggle alone changes both
#                          the tactical view and the sidebar.
cat > gfx_g36_on.cfg <<'CFG'
enabled 1
CFG
cat > gfx_g36_sun.cfg <<'CFG'
enabled 1
gamma_on 0
ss_scale 1.0
shadow_on 1
sun_el 40
shadow_strength 1.0
bloom_on 0
ssao_on 0
lights_on 0
grade_on 0
crt_on 0
CFG
sed 's/^shadow_on 1/shadow_on 0/' gfx_g36_sun.cfg > gfx_g36_nosun.cfg
sed 's/^sun_el 40/sun_el 18/'     gfx_g36_sun.cfg > gfx_g36_lowsun.cfg
cat > gfx_g36_bilin.cfg <<'CFG'
enabled 0
bilinear 1
CFG
G36SCEN="--scen SCG01EB --pack SCG01EA.pack $BASE --ticks 40"
# TEN runs and ten pictures, which is why the wrapper exists rather than ten status
# variables copied ten times. G36c was the worst of the six trapped gates: five STALE
# files agree with each other perfectly, so it reported "5 --shot runs, 1 distinct
# digest" against a binary that had taken no shots at all.
gbegin shots/g36_off.png shots/g36_on.png shots/g36_on2.png shots/g36_on3.png \
       shots/g36_on4.png shots/g36_on5.png shots/g36_sun.png shots/g36_nosun.png \
       shots/g36_low.png shots/g36_bil.png
grun - $G36SCEN --shot shots/g36_off.png
grun - $G36SCEN --shot shots/g36_on.png    --gfx gfx_g36_on.cfg
grun - $G36SCEN --shot shots/g36_on2.png   --gfx gfx_g36_on.cfg
grun - $G36SCEN --shot shots/g36_on3.png   --gfx gfx_g36_on.cfg
grun - $G36SCEN --shot shots/g36_on4.png   --gfx gfx_g36_on.cfg
grun - $G36SCEN --shot shots/g36_on5.png   --gfx gfx_g36_on.cfg
grun - $G36SCEN --shot shots/g36_sun.png   --gfx gfx_g36_sun.cfg
grun - $G36SCEN --shot shots/g36_nosun.png --gfx gfx_g36_nosun.cfg
grun - $G36SCEN --shot shots/g36_low.png   --gfx gfx_g36_lowsun.cfg
grun - $G36SCEN --shot shots/g36_bil.png   --gfx gfx_g36_bilin.cfg
# ALL TEN are asserted, not just the two the old -s test named: G36c reads five of them,
# G36d three and G36f two, and a missing g36_low.png was as invisible as a missing on.png.
gshots shots/g36_off.png shots/g36_on.png shots/g36_on2.png shots/g36_on3.png \
       shots/g36_on4.png shots/g36_on5.png shots/g36_sun.png shots/g36_nosun.png \
       shots/g36_low.png shots/g36_bil.png

if [ "$GRC" != "0" ]; then
  bad "G36 tier 2: a run failed or wrote no shot (GRC=$GRC) -- the chain did not even boot"
else
  G36D=$(python3 "$GATEDIR/gate_gfx.py" diff shots/g36_off.png shots/g36_on.png)
  G36CH=$(echo "$G36D" | cut -d'|' -f2)
  G36ST=$(python3 "$GATEDIR/gate_gfx.py" strip shots/g36_off.png shots/g36_on.png 150 | cut -d'|' -f2)
  G36SUN=$(python3 "$GATEDIR/gate_gfx.py" diff shots/g36_nosun.png shots/g36_sun.png)
  G36SD=$(echo "$G36SUN" | cut -d'|' -f3); G36SB=$(echo "$G36SUN" | cut -d'|' -f4)
  G36LOW=$(python3 "$GATEDIR/gate_gfx.py" diff shots/g36_nosun.png shots/g36_low.png | cut -d'|' -f3)
  G36BIL=$(python3 "$GATEDIR/gate_gfx.py" diff shots/g36_off.png shots/g36_bil.png | cut -d'|' -f2)
  G36BILS=$(python3 "$GATEDIR/gate_gfx.py" strip shots/g36_off.png shots/g36_bil.png 150 | cut -d'|' -f2)

  # (a) it runs
  if [ "${G36CH:-0}" -ge 100000 ]; then
    ok "G36a chain runs: $G36CH pixels change when it is switched on"
  else
    bad "G36a chain: only $G36CH pixels changed (want >=100000) -- it is a no-op"
  fi

  # (b) the seam
  if [ "${G36ST:-1}" -eq 0 ]; then
    ok "G36b the seam holds: 0 of the sidebar's rightmost 150 columns moved"
  else
    bad "G36b the seam LEAKED: $G36ST sidebar pixels were post-processed"
  fi

  # (c) determinism with the chain on. FIVE runs, not two, and the reason is the bug
  # this check found on the day it was written: the lighting shader took dFdx/dFdy
  # AFTER a non-uniform early return, which is undefined in GLSL, and at the horizon it
  # flipped ONE pixel by ONE level about half the time. Two runs agree 50% of the time
  # against a fault like that, so a two-run check would have called it green and moved
  # on. Five runs catch it 94 times in 100.
  # md5(1) is BSD-only and does not exist on Windows, where the digest tool is
  # md5sum. Under 2>/dev/null that produced NO digests at all, and "0 distinct digests"
  # was then printed as a determinism FAILURE, accusing the lighting shader of a fault it
  # does not have on a machine where all five shots are byte-identical. shasum is what
  # G4, G5 and G25 already use in this same script, so it is the portable choice here.
  G36NH=$(shasum shots/g36_on.png shots/g36_on2.png shots/g36_on3.png shots/g36_on4.png shots/g36_on5.png 2>/dev/null | cut -d' ' -f1 | sort -u | wc -l | tr -d ' ')
  if [ "${G36NH:-0}" -eq 1 ]; then
    ok "G36c deterministic with the chain ON: 5 --shot runs, 1 distinct digest"
  else
    bad "G36c NOT deterministic with the chain on: 5 runs produced $G36NH distinct digests"
  fi

  # (d) the sun is a sun
  if [ "${G36SD:-0}" -ge 1000 ] && [ "${G36SB:-0}" -ge 200 ] && \
     [ "${G36LOW:-0}" -ge $((G36SD * 3)) ]; then
    ok "G36d the sun is a sun: $G36SD px darkened and $G36SB px brightened (the cartridge's own shadows withdrawing), and dropping it to 18 deg lengthens the shadows to $G36LOW px"
  else
    bad "G36d sun: darker=$G36SD(want >=1000) brighter=$G36SB(want >=200) low-sun=$G36LOW(want >=3x $G36SD)"
  fi

  # (f) BILINEAR REACHES THE WORLD AND NOT THE UI, and the second half is the half that
  # changed. The first version of this switch covered the sidebar, the cursors, the pause
  # dialog, the menus and the movies too, because that is what was asked for; the
  # opposite was then asked for -- "UI / Menus should not be affected". So this
  # gate was inverted rather than relaxed: the sidebar must not move by ONE pixel while
  # tens of thousands change in the tactical view beside it. A gate that merely stopped
  # requiring the UI to change would pass just as well with the UI still changing.
  if [ "${G36BIL:-0}" -ge 20000 ] && [ "${G36BILS:-1}" -eq 0 ]; then
    ok "G36f bilinear reaches the world and stops at the UI: $G36BIL pixels change with the chain OFF, and 0 of the sidebar's rightmost 150 columns"
  else
    bad "G36f bilinear: total=$G36BIL(want >=20000) sidebar=$G36BILS(want 0) -- it is reaching the UI"
  fi
fi

# (e) the preset file round-trips. The panel's SAVE writes it and --gfx reads it; if the
# two disagree about one dial, a tuning session comes back incomplete and nothing says so.
cat > gate_gfx_rt.txt <<'TXT'
tick 5
gfx bilinear 1
gfx sun_az 137.5
gfx shadow_bias 0.0375
gfx ss_scale 2.75
gfx shadow_res 3072
gfx crt_on 1
gfx saturation 1.42
gfxsave gfx_rt_a.cfg
gfxload gfx_rt_a.cfg
gfxsave gfx_rt_b.cfg
quit
TXT
G36RT=$(./cnc_eyes --scen SCG01EB --pack SCG01EA.pack $BASE --script gate_gfx_rt.txt --gfx 2>&1 \
        | tee -a "$OUT" | grep -E "^FX\|preset" | sed -n 's/.*: \([0-9]*\) applied, \([0-9]*\) unknown/\1 \2/p')
G36RTA=$(echo "$G36RT" | awk '{print $1}'); G36RTU=$(echo "$G36RT" | awk '{print $2}')
G36DIALS=$(grep -cE '^[a-z_]+ ' gfx_rt_a.cfg 2>/dev/null)
if cmp -s gfx_rt_a.cfg gfx_rt_b.cfg && [ "${G36RTU:-1}" -eq 0 ] && \
   [ -n "$G36RTA" ] && [ "${G36RTA:-0}" -eq "${G36DIALS:-0}" ]; then
  ok "G36e preset round-trips: all $G36DIALS dials written, all $G36RTA read back, 0 unknown, save->load->save byte identical"
else
  bad "G36e preset round trip: written=$G36DIALS applied=$G36RTA unknown=$G36RTU, files differ"
fi
# (g) BILINEAR MUST NOT FRINGE, and this check proves itself rather than trusting a
# threshold. The first run with the toggle on came back with two artefacts, and they
# have different causes, so the same frame is rendered twice -- once with --nobilfix and
# once without -- and the fault has to be present in one and absent from the other:
#
#   THE DECALS wore a purple outline. smudge.pack keeps MAGENTA (255,0,255) behind 9827
#   of its transparent texels, the DOS palette's own transparency key, and a bilinear
#   filter averages it into every edge. Fixed by bleeding the artwork's colour outwards
#   at load (fx_bleed_rgba). The fixed frame must carry ZERO key-colour pixels and the
#   unfixed frame must carry some, or the measurement cannot see the fault at all.
#
#   THE TERRAIN grew horizontal black hairlines. It is an ATLAS: a sample taken exactly
#   on a cell's edge blends the neighbouring tile, and where that is one of the atlas's
#   alpha-0 water holes the alpha test discards the fragment and the clear colour shows
#   through. Horizontal on screen because the N64 camera carries zero yaw, so the grid's
#   rows project straight across. Fixed by a half-texel inset.
#
# AT FULL MAGNIFICATION, deliberately. At `zoom min` the terrain is minified, the filter
# averages inside a tile rather than across its edge, and the hairlines are worth about
# 15 pixels: the first version of this gate sat at that zoom and could not fail.
cat > gate_gfx_bilA.txt <<'TXT'
tick 200
cam 51.5 51.5
zoom max
shot shots/g36_bil_nofix.png
quit
TXT
sed 's#g36_bil_nofix#g36_bil_fix#' gate_gfx_bilA.txt > gate_gfx_bilB.txt
gbegin shots/g36_bil_nofix.png shots/g36_bil_fix.png
grun - --noshroud --scen SCG01EC --pack SCG01EA.pack $BASE --script gate_gfx_bilA.txt \
    --gfx gfx_g36_bilin.cfg --nobilfix
grun - --noshroud --scen SCG01EC --pack SCG01EA.pack $BASE --script gate_gfx_bilB.txt \
    --gfx gfx_g36_bilin.cfg
gshots shots/g36_bil_nofix.png shots/g36_bil_fix.png
if [ "$GRC" != "0" ]; then
  bad "G36g bilinear fringe: a run itself failed or wrote no shot (GRC=$GRC)"
else
  # THE KEY FLOOR IS 70, NOT THE DEFAULT 110, and the reason is not a slackened gate.
  # The fringe is a DECAL EDGE, and the decal pass now carries the terrain's own
  # per-corner light (see G83), so the identical fringe reaches the framebuffer
  # multiplied by about 0.63. Nothing about it disappeared: its brightest
  # pixel only moved from (168,55,160) to (141,46,135). Left at 110 the unfixed control
  # collapsed from 423 to 19 and the gate went red at a fringe it could no longer see
  # rather than at one that had gone away.
  #
  # At 70 this build reads unfixed=521 fixed=0, a ten-fold margin on the first claim and
  # an exact zero on the second. Note that the SAME floor against the pre-shade binary
  # reads unfixed=806 fixed=8, so 70 would not have been a usable floor before the shade
  # landed; what those eight residual pixels are was not chased. Do not go below 55: the
  # water and the shadows start answering to the magenta test there.
  G36BF=$(python3 "$GATEDIR/gate_gfx.py" bilfix shots/g36_bil_nofix.png shots/g36_bil_fix.png 150 70)
  G36DK=$(echo "$G36BF" | cut -d'|' -f2)
  G36KA=$(echo "$G36BF" | cut -d'|' -f3)
  G36KB=$(echo "$G36BF" | cut -d'|' -f4)
  if [ "${G36KB:-1}" -eq 0 ] && [ "${G36KA:-0}" -ge 50 ] && [ "${G36DK:-0}" -ge 1500 ]; then
    ok "G36g bilinear does not fringe: the key colour goes $G36KA -> 0 on the decals, and $G36DK terrain pixels stop being darkened by the atlas seam"
  else
    bad "G36g bilinear fringe: key-colour unfixed=$G36KA(want >=50) fixed=$G36KB(want 0), seam pixels=$G36DK(want >=1500)"
  fi
fi
rm -f gate_gfx_bilA.txt gate_gfx_bilB.txt

rm -f gfx_g36_*.cfg gfx_rt_a.cfg gfx_rt_b.cfg gate_gfx_rt.txt

# =====================================================================================
# G37 THE LIVE INPUT PATH SURVIVES A WINDOW WHOSE DRAWABLE IS NOT THE SIZE IT ASKED FOR.
#
# CMD+F / ALT+ENTER fullscreen arrived on 19 Aug and took a bug in with it: two lines of
# the band-select drag unprojected the rectangle against o->w/o->h, the size the window
# was ASKED for, instead of the drawable size read fresh every frame. Windowed those two
# are the same number, so it had always worked; the moment the window filled the screen
# the selection ribbon drew 164 pixels away from the pointer. It was found in the first
# minute of fullscreen.
#
# --resize W H reproduces exactly that condition WITHOUT taking over the display: the
# window is created at --w/--h and then resized, so the drawable stops matching what was
# asked for. A gate that seizes the whole screen cannot be run while somebody is playing,
# which is not a hypothetical -- the first version of this check did.
#
# --autoplay is the right driver because it pushes REAL SDL events in window points
# through the live press/drag/release handler, which the --script verbs bypass. SCB01EA
# is the scenario its plan was written against; on any other map its click targets miss
# and five of its sixteen steps fail for reasons that have nothing to do with this.
#
# PROVEN TO FAIL: with the two lines put back, this reports roundtrip=164.58 px and the
# band assertion fails. With them fixed it is 0.00.
G37LOG=$(./cnc_eyes --scen SCB01EA --pack SCB01EA.pack $BASE \
             --w 1280 --h 720 --resize 900 620 --autoplay --run 9 2>&1 | tee -a "$OUT")
G37RS=$(echo "$G37LOG" | grep -c "^RESIZE|asked 1280x720|window now 900x620")
G37RT=$(echo "$G37LOG" | sed -n 's/^AUTOPLAY|band|roundtrip=\([0-9.]*\) px.*/\1/p' | head -1)
G37F=$(echo "$G37LOG" | sed -n 's/^AUTOPLAY|end|[0-9]* step(s) run, \([0-9]*\) failure(s)/\1/p' | head -1)
G37OK=$(awk -v v="${G37RT:-999}" 'BEGIN { print (v < 3.0) ? 1 : 0 }')
if [ "${G37RS:-0}" -ge 1 ] && [ "${G37F:-1}" = "0" ] && [ "$G37OK" = "1" ]; then
  ok "G37 resized window: all 16 autoplay steps pass at 900x620 after asking for 1280x720, and the band's 3D box lands ${G37RT} px from the drag it was made with"
else
  bad "G37 resized window: resize-line=$G37RS(want >=1) autoplay-failures=$G37F(want 0) band-roundtrip=${G37RT}px(want <3)"
fi

# =====================================================================================
# G38 THE 640x480 HUD GROWS BUILD ROWS ON A TALLER SCREEN.
#
# The bar is authored 160x480 and the zoom is a whole number, so any screen that is not a
# multiple of 480 tall used to letterbox: the 3008x1692 display drew it at 3x = 1440
# and left 252 rows of black. It spends that on cameos now -- one 48px band of the
# chassis repeated per extra row, everything below the split sliding down.
#
# Both halves are asserted, and the first is the one that protects the shipped picture:
#   AT A MULTIPLE OF 480 NOTHING CHANGES. 960 rows must still be exactly five, because
#   that is the authored art and every other sidebar gate assumes it.
#   AT 1692 IT GROWS. And every visible cell has to hit-test to its OWN row and column,
#   including the new ones, or the picture would be right and the clicks would not.
#
# sbwalk computes its own probe coordinates from the layout rather than carrying pixels,
# which matters because the row count is the variable: a test with hardcoded coordinates
# would need rewriting per window size and would stop testing what it is named after.
G38A=$(./cnc_eyes --noshroud --scen SCG01EC --pack SCG01EA.pack $BASE --w 1280 --h 960 \
           --script "$GATEDIR/gate_hudrows.txt" 2>&1 | tee -a "$OUT" | grep '^SBWALK|rows=')
G38B=$(CNC3D_HUD=new ./cnc_eyes --noshroud --scen SCG01EC --pack SCG01EA.pack $BASE --w 1280 --h 960 \
           --script "$GATEDIR/gate_hudrows.txt" 2>&1 | tee -a "$OUT" | grep '^SBWALK|rows=')
# 1200 AND NOT 1692, changed 25 Aug 2026, and the reason is worth writing down because
# the old number is still in the prose above and was right when it was written.
#
# This used to ask for 3008x1692, the development display's exact size in points. On 24 Aug a
# commit made the window SDL_WINDOW_RESIZABLE *always* rather than only under --resize,
# and macOS constrains a resizable window to the visible frame -- so a 1692-tall window on
# a 1692-tall display now caps at 1547 and the gate went red having tested nothing. The
# drawable line says it plainly: "surface 3008x1547".
#
# 1200 IS NOT MERELY SMALLER, IT IS A BETTER TEST. The row count is NOT monotonic in
# height, because the zoom is a whole number: measured on this display, 1200 gives 7 rows,
# 1400 gives 9, and 1547 gives FIVE -- fewer than 1200 -- because at 1547 the zoom steps
# up to 3 and each row costs three times as much. A height the window can actually be
# granted is therefore worth more than the display's nominal size, and 1200 also fits any
# development screen rather than only one.
G38C=$(CNC3D_HUD=new ./cnc_eyes --noshroud --scen SCG01EC --pack SCG01EA.pack $BASE --w 3008 --h 1200 \
           --script "$GATEDIR/gate_hudrows.txt" 2>&1 | tee -a "$OUT" | grep '^SBWALK|rows=')
g38f() { echo "$1" | sed -n "s/.*|$2=\([0-9]*\).*/\1/p"; }
G38BR=$(g38f "$G38B" rows); G38BP=$(g38f "$G38B" probes); G38BO=$(g38f "$G38B" ok)
G38CR=$(g38f "$G38C" rows); G38CP=$(g38f "$G38C" probes); G38CO=$(g38f "$G38C" ok)
G38CH=$(g38f "$G38C" barh)
if [ "${G38BR:-0}" = "5" ] && [ -n "$G38BP" ] && [ "$G38BP" = "$G38BO" ] && \
   [ "${G38CR:-0}" -gt 5 ] && [ -n "$G38CP" ] && [ "$G38CP" = "$G38CO" ]; then
  ok "G38 HUD rows follow the screen: 5 rows at 960 (the authored art, unchanged), $G38CR rows and a ${G38CH}px bar at 1200, and every one of $G38CP probed cells hit-tests to its own row"
else
  bad "G38 HUD rows: at 960 rows=$G38BR(want 5) probes=$G38BP ok=$G38BO; at 1200 rows=$G38CR(want >5; if the drawable line reads shorter than asked, the window manager clamped it and the height needs choosing again) probes=$G38CP ok=$G38CO"
fi

# =====================================================================================
# G39 THE POINTER IS VISIBLE ON THE F5 PANEL.
#
# It was not, and the cause is worth stating because no screenshot could have found it.
# The panel CONSUMED the mouse-motion event while the pointer was over it, so the game
# never learned the pointer had moved. update_cursor runs once a frame off g_mouseScrC/R,
# so with those frozen at the last on-map position it went on believing the pointer was
# over the battlefield -- and over the battlefield the DOS sprite is deliberately NOT
# drawn, because the cartridge's 3D world cursor draws instead. That cursor is in the
# world, underneath the panel. So the panel had no pointer on it at all and the sliders
# could not be aimed at (reported, first fullscreen run with F5 open).
#
# The `cursor` script verb sets the position DIRECTLY and would have passed throughout,
# which is why this probe pushes a real SDL_MOUSEMOTION through fxp_event first and then
# asks what the game concluded. Run at a drawable resized away from the requested size,
# because that is where it was hit.
#
# The three probes are map -> panel -> map, and the middle one is the whole test:
#   panel=1  the point really is over the panel
#   motion_eaten=0  the panel did not swallow the event (the regression itself)
#   onmap=0  so the game knows the pointer left the battlefield
#   sprite=1 so the DOS pointer is what draws there
G39L=$(./cnc_eyes --noshroud --scen SCG01EC --pack SCG01EA.pack $BASE \
           --w 1280 --h 720 --resize 3008 1692 --script "$GATEDIR/gate_panelcursor.txt" \
           --gfx --gfxpanel 2>&1 | tee -a "$OUT" | grep '^PANELPROBE|')
G39MAP=$(echo "$G39L" | grep -c 'panel=0|motion_eaten=0|onmap=1|sprite=0')
G39PAN=$(echo "$G39L" | grep -c 'panel=1|motion_eaten=0|onmap=0|sprite=1')
if [ "${G39MAP:-0}" -eq 2 ] && [ "${G39PAN:-0}" -eq 1 ]; then
  ok "G39 the pointer is on the panel: motion is not swallowed, the game follows it off the map, and the DOS sprite draws there instead of the world cursor that sits under the panel"
else
  bad "G39 panel pointer: map-probes matching=$G39MAP(want 2) panel-probe matching=$G39PAN(want 1). Lines: $G39L"
fi

# =====================================================================================
# G40 / G41 THE 640x480 HUD: ITS RADAR, AND ITS CAMEOS INSIDE THEIR FRAMES.
#
# Both were the, both were a producer-shaped hole rather than a broken drawing:
#
#   THE MINIMAP was never wired. H6_State.radar_rgba is declared and consumed but nothing
#   ever assigned it, so hud640_draw_bar took its "no map" branch and stamped the faction
#   emblem over the bezel for ever. It was not the toggle: radar_active was 1 the whole
#   time. And the DOS producer could not have filled it -- sb_draw_panel branches to the
#   new HUD and RETURNS above the RadarPlot call, so that hook is unreachable on this
#   path. One plotter serves both surfaces now, through a target rather than a copy.
#
#   THE CAMEOS sat ON the well's bevel instead of in it, because the cameo box is 64x48
#   against a 61x45 well and overhangs it. cell_frame -- 68x54, an opaque ring with its
#   centre punched out, in the pack since the HUD was baked and never once drawn -- goes
#   back over the picture. Offset (-3,-4), measured by correlating it against the
#   chassis's own baked frames, not chosen.
#
# Everything is at --w 1280 --h 960, where the bar scale is 2 and the bar starts at
# x=960, so the radar surface is (982,62)..(1254,302) and column 0 row 1 is (996,518).
G4SCEN="--noshroud --scen SCG01EC --pack SCG01EA.pack $BASE --w 1280 --h 960"
# THE RENAME-INTO-PLACE FORM, which the note at G45 calls the worst case of the trap and
# which this was the last instance of. Three runs each write shots/g40_x.png and a mv
# renames it into place; with the mv's complaint going to /dev/null, a run that wrote
# nothing left the PREVIOUS suite's g40_on.png standing under the name about to be
# measured, and said nothing about it. Three changes: the scratch name g40_x.png goes into
# gbegin as well (or run two would inherit run one's picture), the mv's stderr goes to the
# log rather than /dev/null, and the status of each run is read.
# CNC3D_HUD is exported rather than prefixed to the call, because grun is a shell function
# and an assignment prefixed to a function call does not reliably stay scoped to it.
gbegin shots/g40_x.png shots/g40_on.png shots/g40_off.png shots/g40_noframe.png
export CNC3D_HUD=new
# --forceradar since v0.6.0. The radar used to be forced ON for everybody by default,
# which is the bug players reported (a minimap before you have built a Communications
# Center). Now it obeys RadarMapActive, so this gate -- whose whole job is to photograph
# the radar -- has to ask for it explicitly. Without the flag it measures 0 changed
# pixels, correctly, because there is no radar to draw.
grun - $G4SCEN --forceradar --script "$GATEDIR/gate_hudradar.txt"
mv -f shots/g40_x.png shots/g40_on.png 2>>"$OUT"
grun - $G4SCEN --forceradar --script "$GATEDIR/gate_hudradar.txt" --nominimap
mv -f shots/g40_x.png shots/g40_off.png 2>>"$OUT"
grun - $G4SCEN --forceradar --script "$GATEDIR/gate_hudradar.txt" --nocellframe
mv -f shots/g40_x.png shots/g40_noframe.png 2>>"$OUT"
unset CNC3D_HUD
gshots shots/g40_on.png shots/g40_off.png shots/g40_noframe.png

if [ "$GRC" != "0" ]; then
  bad "G40 new HUD radar: a run failed or wrote no shot (GRC=$GRC)"
  bad "G41 new HUD cameo frame: a run failed or wrote no shot (GRC=$GRC)"
else
  G40R=$(python3 "$GATEDIR/gate_gfx.py" rect shots/g40_off.png shots/g40_on.png 982 62 1254 302 | cut -d'|' -f2)
  if [ "${G40R:-0}" -ge 20000 ]; then
    ok "G40 the new HUD has a radar: $G40R pixels inside the 136x120 bezel become map instead of the faction emblem, and --nominimap puts the emblem back"
  else
    bad "G40 new HUD radar: only $G40R pixels changed inside the bezel (want >=20000) -- the emblem is still being drawn"
  fi

  G41C=$(python3 "$GATEDIR/gate_gfx.py" rect shots/g40_noframe.png shots/g40_on.png 988 510 1126 616 | cut -d'|' -f2)
  G41RING=$(python3 "$GATEDIR/gate_gfx.py" ring shots/g40_noframe.png shots/g40_on.png 996 518 1118 608)
  G41A=$(echo "$G41RING" | cut -d'|' -f3); G41B=$(echo "$G41RING" | cut -d'|' -f4)
  # The corners are their own claim. The frame ring closes the four STRAIGHT edges and
  # leaves the 45-degree cuts open, which is exactly what was still visible, so a
  # gate that only measured the border would have called the half-fix done. cut=14 is
  # H6_CHAMFER 7 at the bar's zoom of 2.
  G41CH=$(python3 "$GATEDIR/gate_gfx.py" chamfer shots/g40_noframe.png shots/g40_on.png 996 518 1118 608 14)
  G41CT=$(echo "$G41CH" | cut -d'|' -f2)
  G41CA=$(echo "$G41CH" | cut -d'|' -f3); G41CB=$(echo "$G41CH" | cut -d'|' -f4)
  G41CW=$(awk -v t="${G41CT:-1}" 'BEGIN { print int(t * 0.75) }')
  if [ "${G41C:-0}" -ge 1500 ] && [ "${G41B:-0}" -gt "${G41A:-0}" ] && \
     [ "${G41CB:-0}" -ge "${G41CW:-999999}" ] && [ "${G41CB:-0}" -gt "${G41CA:-0}" ]; then
    ok "G41 the cameo is inside its frame: the treatment repaints $G41C pixels of one cell, its 1px border goes from $G41A to $G41B frame-coloured of 420, and the four 45-degree corner cuts go from $G41CA to $G41CB of $G41CT"
  else
    bad "G41 cameo frame: changed $G41C px (want >=1500), border $G41A -> $G41B (want up), corners $G41CA -> $G41CB of $G41CT (want up and >= $G41CW)"
  fi
fi

# =====================================================================================
# G42 THE VISUALS SCREEN, and the thing it has to get right is that the DIALOG and the
# CHAIN never disagree. The dialog holds nine booleans and a mode; the chain holds those
# plus a supersampling FLOAT. "The box is ticked" and "the picture has it" are two
# claims, so optvis prints both sides of every one and this walks them together:
#
#   open        ENHANCED, so ADVANCED is live and the chain is on
#   CLASSIC     ADVANCED greys out (gadget.cpp:632 draws a disabled gadget, never
#               hides it) and the chain switches OFF -- not "on with nothing ticked",
#               which is a different and nearly identical picture
#   ENHANCED    back on
#   Bloom       one checkbox, one element, both off
#   Supersample the float follows the checkbox down to 1.00
#
# The seventh pause-menu button and the grown dialog are covered by G13 already passing.
G42L=$(./cnc_eyes --noshroud --scen SCG01EC --pack SCG01EA.pack $BASE --w 1280 --h 800 \
           --script "$GATEDIR/gate_visuals.txt" --gfx 2>&1 | tee -a "$OUT" | grep '^OPTVIS|')
g42() { echo "$G42L" | sed -n "$1p"; }
G42OK=1
echo "$(g42 1)" | grep -q 'enhanced=1|advdisabled=0|.*fx_enabled=1' || G42OK=0
echo "$(g42 2)" | grep -q 'enhanced=0|advdisabled=1|.*fx_enabled=0' || G42OK=0
echo "$(g42 3)" | grep -q 'enhanced=1|advdisabled=0|.*fx_enabled=1' || G42OK=0
echo "$(g42 4)" | grep -q 'page=3|.*bloom=0|.*fx_bloom=0'           || G42OK=0
# SUPERSAMPLING: assert the PAIRING and the MOVEMENT, not a fixed direction.
# This line used to read `grep -q 'ss=0|.*fx_ss=1.00'`, which silently assumed the box
# starts TICKED because the shipped ss_scale was 2.0. On the original dials
# became the defaults and ss_scale is 1.0, so the box now starts clear and the click
# turns it ON. The dialog and the chain agreed perfectly in both directions the whole
# time -- the gate was asserting an accident of the default, not the thing it claims in
# its own header ("a checkbox moves its element").
#
# What replaces it is STRICTLY STRONGER, not looser. Two claims instead of one:
#   PAIRING   ss=0 must come with fx_ss=1.00 and ss=1 with fx_ss=2.00, either way round
#   MOVEMENT  line 5's ss must DIFFER from line 4's, so a checkbox that does nothing at
#             all fails here. The old form could not make that claim: with the old
#             default it passed on the state alone and never proved the click did it.
G42SS4=$(g42 4 | sed -n 's/.*|ss=\([01]\)|.*/\1/p')
G42SS5=$(g42 5 | sed -n 's/.*|ss=\([01]\)|.*/\1/p')
echo "$(g42 5)" | grep -qE 'ss=0\|.*fx_ss=1\.00|ss=1\|.*fx_ss=2\.00'  || G42OK=0
[ -n "$G42SS4" ] && [ -n "$G42SS5" ] && [ "$G42SS4" != "$G42SS5" ]        || G42OK=0
if [ "$G42OK" = "1" ] && [ "$(echo "$G42L" | wc -l | tr -d ' ')" = "5" ]; then
  ok "G42 the Visuals screen drives the chain: CLASSIC greys ADVANCED out and switches the chain off, ENHANCED brings both back, and a checkbox moves its element -- supersampling's float included, down to 1.00"
else
  bad "G42 Visuals screen: the dialog and the chain disagree. Lines: $G42L"
fi

# G64 SOFT DECAL EDGES, AND THE DEFAULT PATH LEFT ALONE.
#
# Reported: ground decals cut hard against the terrain, and the edges should blend
# into the ground below.
#
# They are hard because the cartridge's art carries a strict 1-bit alpha key -- and that is
# not an accident of the bake, bake_smudges.py ASSERTS it and refuses to bake anything else
# -- so the renderer drew them with a 0.5 alpha test, exactly equivalent to the console's
# own FORCE_BL composite and free on Tier 1. Softening therefore has to be manufactured: a
# box blur of the ALPHA only, per frame cell, at load, and a real blend at draw.
#
# WHAT THIS GATE IS REALLY GUARDING is the third claim, not the first two. `decal_soft`
# lives in fx_defaults at 0.60 for the player, and reaches the renderer ONLY through the
# Enhanced path and the Visuals dialog. If it ever reaches a bare --script run, every pixel
# gate with a scorch mark or a building apron in frame silently re-baselines, and G35
# measures smudges head on. So:
#
#   DEFAULT UNTOUCHED  a frame drawn with no dial set is BYTE-IDENTICAL to one with the
#                      dial explicitly at 0. This is the assertion that protects the suite.
#   IT SOFTENS         0.6 changes the picture by a clear margin.
#   ONLY THE EDGES     and by a BOUNDED one. A blur that touched more than a few percent of
#                      the frame would be softening something other than decal edges.
rm -f shots/g64_default.png shots/g64_hard.png shots/g64_soft.png
./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE --noshroud --nominimap --nosidebar \
    --nosound --w 900 --h 600 --script gate_decalsoft.txt >>"$OUT" 2>&1
DSRC=$?
if [ "$DSRC" != "0" ]; then
  bad "G64 soft decals: the run itself failed (exit $DSRC)"
elif [ ! -s shots/g64_default.png ] || [ ! -s shots/g64_hard.png ] || [ ! -s shots/g64_soft.png ]; then
  bad "G64 soft decals: a shot was not written"
else
  DSSAME=$(cmp -s shots/g64_default.png shots/g64_hard.png && echo 1 || echo 0)
  DSR=$(python3 - <<'PY'
from PIL import Image
import numpy as np
h = np.asarray(Image.open('shots/g64_hard.png').convert('RGB')).astype(int)
s = np.asarray(Image.open('shots/g64_soft.png').convert('RGB')).astype(int)
d = int((np.abs(h - s).sum(axis=2) > 16).sum())
print("%d %d" % (d, h.shape[0] * h.shape[1]))
PY
)
  set -- $DSR
  DSN="$1"; DSTOT="$2"
  DSBOUND=$(awk -v n="${DSN:-0}" -v t="${DSTOT:-1}" 'BEGIN { print (n <= t * 0.06) ? 1 : 0 }')
  if [ "$DSSAME" = "1" ] && [ "${DSN:-0}" -ge 2000 ] && [ "$DSBOUND" = "1" ]; then
    ok "G64 soft decals: a bare run is byte-identical to the dial at 0, so the suite's own decal measurements are untouched, and 0.6 softens $DSN pixels of $DSTOT"
  else
    bad "G64 soft decals: default-equals-hard=$DSSAME(want 1; if 0 the dial has leaked into the script path and every decal gate has re-baselined) softened=$DSN(want >=2000) bounded=$DSBOUND(want 1, <=6% of $DSTOT)"
  fi
fi

# G63 THE "NEW HUD" CHECKBOX SWITCHES THE SIDEBAR, WITH CNC3D_HUD SET.
#
# Reported within the hour of the dial shipping: toggling the New vs Old HUD did nothing
# at all. It did not, and the cause was an earlier fix made for G53.
#
# G53 went red because the pause dialog applies the whole visual state on the way in, and
# with the post chain off it reads that state as CLASSIC, which correctly means the DOS
# bar -- so the new HUD that gate sets up vanished mid-run. The fix made CNC3D_HUD a HARD
# OVERRIDE that nothing could stomp. That satisfied the gate and broke the feature: both
# `tools/launchers/C&C3D.app/Contents/MacOS/cnc3d-launch:39` and `PLAY-HUD-MENU.command`
# set CNC3D_HUD=new, so on the original launcher the checkbox did nothing in either
# direction. A gate was made green by disabling the thing it was meant to protect.
#
# THIS GATE ASSERTS STATE, NOT PIXELS, and the first draft of it did the opposite and was
# WORTHLESS. It compared sidebar pixels across the toggle and passed with the override
# restored: 63902 pixels change between any two frames anyway, because the radar sweeps,
# the credits count and the cameos animate. Same confounding that made the buildup
# unmeasurable in pixels. optvis now reports three things and the gate walks them:
#
#   hud     the checkbox
#   fx_hud  the dial behind it
#   sb_hud  WHICH SIDEBAR IS ACTUALLY BEING DRAWN
#
# The third is the one that matters. A gate asserting only the checkbox would have passed
# through the whole week the dial was inert, because the checkbox always moved.
rm -f shots/g63.log
CNC3D_HUD=new ./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE --w 1280 --h 800 \
    --noshroud --nosound --gfx --script gate_hudtoggle.txt > shots/g63.log 2>&1
HTRC=$?
cat shots/g63.log >> "$OUT"
HTBEF=$(sed -n 's/^OPTVIS|.*|hud=\([01]\)|fx_hud=\([01]\)|sb_hud=\([01]\)$/\1\2\3/p' shots/g63.log | sed -n 1p)
HTAFT=$(sed -n 's/^OPTVIS|.*|hud=\([01]\)|fx_hud=\([01]\)|sb_hud=\([01]\)$/\1\2\3/p' shots/g63.log | sed -n 2p)
if [ "$HTRC" != "0" ]; then
  bad "G63 New HUD toggle: the run itself failed (exit $HTRC)"
elif [ -z "$HTBEF" ] || [ -z "$HTAFT" ]; then
  bad "G63 New HUD toggle: optvis did not report the HUD triple twice [$HTBEF/$HTAFT]"
elif [ "$HTBEF" = "111" ] && [ "$HTAFT" = "000" ]; then
  ok "G63 New HUD toggle: checkbox, dial and DRAWN sidebar all go 1 -> 0 together when the box is clicked, with CNC3D_HUD=new set, so the player's dial outranks the launcher's environment"
else
  bad "G63 New HUD toggle: hud/fx_hud/sb_hud went [$HTBEF] -> [$HTAFT], want [111] -> [000]. If the first two moved and the third did not, the dial is inert and the drawn sidebar is ignoring it -- check that nothing has made CNC3D_HUD an override again"
fi

# G62 SELLING A STRUCTURE SHEDS IT FROM THE FIRST TICK.
#
# Reported: selling a structure (a power plant, for one) took a very long time to
# animate, unfold and disappear.
#
# It did, and the cause was NOT faithfulness, though it was recorded here as if it were.
# construction_frac's deconstruction arm was f = 2*(count - stage)/count, clamped, so the
# building stood COMPLETE for the whole first half and shed over the second: 2.00 s of
# 3.87 s motionless for a power plant.
#
# WHY THAT IS A BUG AND NOT THE CONSOLE. The cartridge's own Anims[BSTATE_CONSTRUCTION]
# .Count is 1 for all 64 structures -- 384 byte-identical {0,1,0} triples, because the ROM
# ships no MAKE.SHP for BuildingTypeClass::One_Time to patch the constructor defaults from
# (bdata.cpp:3739-3741 against :3849). On hardware the arm is f = 2*(1 - stage): complete
# at stage 0, gone at stage 1. The dead half exists only because the console's 2x formula
# was fed the PC build's frame counts (20 for a power plant, 32 for the yard). The 2x is
# the original observation and it is real -- but it was verified on CONSTRUCTION, which
# watched; the sell arm was never checked against anything and had never run.
#
#   IT REALLY SOLD    the mission reaches Selling and the stage counter runs, or the
#                     sbclick missed and this gate is measuring a building standing still.
#   IT SHEDS EARLY    triangles are already coming off within the first third of the run.
#                     This is the assertion the report maps onto directly.
#   IT SHEDS THROUGHOUT  the count falls monotonically and ends well below where it began.
gbegin shots/g62.log
grun shots/g62.log --scen SCG01EC --pack SCG01EA.pack $BASE --noshroud --nosound \
     --w 1280 --h 800 --script gate_sell.txt
SLL=$(sed -n 's/^STAGEPOS|NUKE|[0-9]*|bstate=[0-9-]*|stage=[0-9-]*|smooth=[0-9.-]*|on=[0-9]*|alpha=[0-9.]*|tris=\([0-9-]*\)|.*/\1/p' shots/g62.log | tr '\n' ' ')
SLN=$(echo $SLL | wc -w | tr -d ' ')
SLRES=$(python3 - "$SLL" <<'PY'
import sys
t = [int(v) for v in sys.argv[1].split()]
if len(t) < 12:
    print("0 0 0 0 0"); raise SystemExit
first, last = t[0], t[-1]
third = t[:max(3, len(t)//3)]
early = 1 if min(third) < first else 0          # shedding has begun in the first third
mono  = 1 if all(t[i] >= t[i+1] for i in range(len(t)-1)) else 0
gone  = 1 if last < first * 0.8 else 0          # meaningfully stripped by the end
print("%d %d %d %d %d" % (early, mono, gone, first, last))
PY
)
set -- $SLRES
SLEARLY="$1"; SLMONO="$2"; SLGONE="$3"; SLFIRST="$4"; SLLAST="$5"
SLSOLD=$(grep -c "mission=Selling\|SELL" shots/g62.log)
if [ "$GRC" != "0" ]; then
  bad "G62 selling: the run itself failed (GRC=$GRC)"
elif [ "${SLN:-0}" -lt 12 ]; then
  bad "G62 selling: wanted 22 samples, got $SLN"
elif [ "${SLFIRST:-0}" -le 0 ] || [ "${SLFIRST:-0}" = "${SLLAST:-0}" ]; then
  bad "G62 selling: the building never changed ($SLFIRST -> $SLLAST). The sbclick almost certainly MISSED the SELL button, which fails silently -- this gate needs --w 1280 --h 800"
elif [ "$SLEARLY" = "1" ] && [ "$SLMONO" = "1" ] && [ "$SLGONE" = "1" ]; then
  ok "G62 selling: the structure begins shedding inside the first third of the sell and comes apart monotonically, $SLFIRST triangles down to $SLLAST, with no motionless half"
else
  bad "G62 selling: sheds-early=$SLEARLY(want 1; this is the reported symptom) monotone=$SLMONO(want 1) stripped=$SLGONE(want 1) $SLFIRST -> $SLLAST [$SLL]"
fi

# G61 THE STRUCTURE ANIMATIONS THAT RUN OFF THE ENGINE'S STAGE COUNTER, AND THE BUILDUP.
#
# Reported: structure animations want the same interpolation -- while a building goes
# up, while something is acting, and on idle loops. Then, after that shipped: certain
# buildings being placed (power plants, barracks) still animated choppily rather than
# smoothly, and the same on the way out when sold.
#
# The crane and the idle loops took the sub-tick clock on 20 Aug (G58 proves the crane at
# 58.33 -> 58.67 -> 59.00). Two things were still stepping and they are DIFFERENT faults:
#
#   THE COUNTER. dostage holds still for many ticks and then increments. A counter is not
#   a position, so G60's box filter is the wrong tool: there is nothing to average, and a
#   lerp between consecutive samples gives two ticks of nothing and then a jump, which IS
#   the stepping. It is ramped across its own MEASURED rate instead, clamped so it can
#   never predict past the engine.
#
#   THE REVEAL, which is what was actually seen on a power plant. Smoothing the counter
#   does not help here at all: draw_mesh turned the fraction into a whole number of
#   display-list SECTIONS, so the building assembled in a handful of lurches however
#   smooth the number feeding it was. Measured on a NUKE, triangles drawn per tick:
#     off  6 6 6 6 6 22 22 22 36 36 36 53 53 53 73 73 73 101 101 101 119 ...
#     on   6 6 8 14 19 24 29 33 38 44 50 56 63 69 77 87 96 104 110 116 120 ...
#   Nine lurches of 14 to 28 triangles with three-tick freezes between them, against a
#   steady five or so per tick. Sub-section reveal is a DEVIATION from the cartridge, which
#   pops whole sections, and it is switched by "Smooth animations" so unticking the box
#   restores the console's behaviour exactly.
#
#   IDENTITY OFF   sample 0 has smoothing off: the smoothed counter equals the integer.
#   IT HOLDS       the engine's counter really does sit still, or there is nothing here.
#   IT RAMPS       the smoothed counter strictly increases while the counter holds.
#   NEVER PREDICTS it stays within one whole stage of the engine, always.
#   THE REVEAL IS SMOOTH  the triangle count takes many more distinct values than the mesh
#                  has sections, and its biggest single jump is a small fraction of the
#                  model. Both are needed: "many values" alone would pass on a mesh that
#                  crept and then leapt.
gbegin shots/g61.log
grun shots/g61.log --scen SCG01EC --pack SCG01EA.pack $BASE --noshroud --nosound \
     --script gate_stagesmooth.txt
STG=$(sed -n 's/^STAGEPOS|NUKE|[0-9]*|bstate=\([0-9-]*\)|stage=\([0-9-]*\)|smooth=\([0-9.-]*\)|on=\([0-9]*\)|alpha=[0-9.]*|tris=\([0-9-]*\)|sections=\([0-9-]*\)/\1 \2 \3 \4 \5 \6/p' shots/g61.log | tr '\n' ',')
STN=$(echo "$STG" | tr ',' '\n' | grep -c .)
STRES=$(python3 - "$STG" <<'PY'
import sys
rows = [r.split() for r in sys.argv[1].split(',') if r.strip()]
rows = [r for r in rows if len(r) == 6]
if len(rows) < 8:
    print("0 0 0 0 0 0 0 0"); raise SystemExit
off = [r for r in rows if r[3] == '0']
on  = [r for r in rows if r[3] == '1' and r[0] == '0']   # smoothing on, still building
if not off or len(on) < 8:
    print("0 0 0 0 0 0 0 0"); raise SystemExit
ident = 1 if abs(float(off[0][2]) - int(off[0][1])) < 1e-6 else 0
st = [int(r[1]) for r in on]
sm = [float(r[2]) for r in on]
tr = [int(r[4]) for r in on]
ns = int(on[0][5])
held  = 1 if len(set(st)) < len(st) else 0
ramps = 1 if all(sm[i] <= sm[i+1] for i in range(len(sm)-1)) and sm[-1] > sm[0] else 0
bound = 1 if all(sm[i] >= st[i] - 1e-6 and sm[i] <= st[i] + 1.0001
                 for i in range(len(sm))) else 0
distinct = len(set(tr))
jump = max((tr[i+1] - tr[i]) for i in range(len(tr)-1)) if len(tr) > 1 else 0
total = max(tr) if tr else 1
print("%d %d %d %d %d %d %d %d" % (ident, held, ramps, bound, distinct, ns, jump, total))
PY
)
set -- $STRES
STIDENT="$1"; STHELD="$2"; STRAMP="$3"; STBOUND="$4"; STDIST="$5"; STNS="$6"; STJUMP="$7"; STTOT="$8"
STSMOOTH=$(awk -v d="${STDIST:-0}" -v n="${STNS:-99}" -v j="${STJUMP:-999}" -v t="${STTOT:-1}" \
    'BEGIN { print (d > n && j <= t * 0.15) ? 1 : 0 }')
if [ "$GRC" != "0" ]; then
  bad "G61 structure smoothing: the run itself failed (GRC=$GRC)"
elif [ "${STN:-0}" -lt 10 ]; then
  bad "G61 structure smoothing: wanted 35 samples, got $STN -- did the NUKE reach BSTATE_CONSTRUCTION?"
elif [ "$STHELD" != "1" ]; then
  bad "G61 structure smoothing: the engine's own counter moved on every sample, so there is no hold to ramp across and the rest of this gate would pass on nothing"
elif [ "$STIDENT" = "1" ] && [ "$STRAMP" = "1" ] && [ "$STBOUND" = "1" ] && [ "$STSMOOTH" = "1" ]; then
  ok "G61 structure smoothing: the smoothed counter equals the engine's exactly with smoothing off, ramps across its holds with it on and never runs a whole stage ahead, and the buildup reveals $STDIST distinct triangle counts against the mesh's $STNS sections with a worst jump of $STJUMP of $STTOT"
else
  bad "G61 structure smoothing: identity-when-off=$STIDENT(want 1) ramps=$STRAMP(want 1) never-predicts=$STBOUND(want 1) reveal-smooth=$STSMOOTH(want 1; distinct=$STDIST must exceed sections=$STNS and worst jump $STJUMP must be <=15% of $STTOT)"
fi

# G60 MOVEMENT SMOOTHING: IT DRAWS BETWEEN TICKS, IT KILLS THE ENGINE'S OWN PULSE, AND
# TRUTH NEVER MOVES WITH IT.
#
# Reported: vehicles moving visibly cell by cell is what made this look bad. Then, once
# the first version shipped: small pulses remained as they moved between cells.
# Both halves are asserted here, because fixing the first without the
# second is what the first attempt did.
#
# THE PULSE IS THE ENGINE'S, NOT OURS. DriveClass spends movement in whole SCREEN PIXELS
# per tick and banks the remainder in SpeedAccum (drive.cpp:638-642, PIXEL_LEPTON_W = 10
# leptons), so a unit's per-tick step is not constant even at constant speed: the gunboat
# measured four steps of ~44 model units and then a STALL, a period of five. Interpolating
# between consecutive samples reproduces that exactly, as a visible throb. A box filter of
# K samples cancels any ripple whose period divides K, which is why the default is 5 and
# why 7 is measurably WORSE than 5 (spread 6.45 against 0.10): this is about matching the
# period, not about averaging harder.
#
# THE DANGEROUS PART IS THE SPLIT, NOT THE FILTER. o.wx/o.wz now trails the simulation.
# o.ex/o.ez stays the engine's own, and picking, ordering, the vanish sweep and the
# placement diagnostic were moved onto it FIRST, in their own step. A player clicking a
# smoothed silhouette would select by one truth and order by another.
#
#   TRUTH FROZEN   ex is byte-identical across part 1's five samples. THE assertion. If
#                  smoothing ever leaks into the engine's position, this goes red.
#   IDENTITY OFF   with smoothmove 0 the drawn position EQUALS truth exactly. Every other
#                  pixel gate in this suite depends on that: they all run with it off.
#   IT MOVES       the three phases inside one tick are strictly monotone.
#   THERE IS A PULSE TO REMOVE  the ENGINE's own per-tick step spread across part 2 must
#                  be large. Without this the next two assertions pass on a parked boat,
#                  and a gate that cannot spring its own trap proves nothing.
#   THE PULSE IS GONE  the DRAWN per-tick step spread must be a small fraction of it.
gbegin shots/g60.log
grun shots/g60.log --scen SCG01EC --pack SCG01EA.pack $BASE --noshroud --nosound \
     --script gate_smoothmove.txt
SMEX=$(sed -n 's/^DRAWPOS|BOAT|[0-9]*|ex=\([0-9.-]*\)|.*/\1/p' shots/g60.log | tr '\n' ' ')
SMWX=$(sed -n 's/^DRAWPOS|BOAT|[0-9]*|ex=[0-9.-]*|ez=[0-9.-]*|wx=\([0-9.-]*\)|.*/\1/p' shots/g60.log | tr '\n' ' ')
SMN=$(echo $SMWX | wc -w | tr -d ' ')
# Part 1 is the first five samples; part 2 is everything after, and its first few are
# dropped because the filter's window is still filling.
SMP1EX=$(echo $SMEX | cut -d' ' -f1-5)
SMNEX=$(echo $SMP1EX | tr ' ' '\n' | sort -u | grep -c .)
set -- $SMWX
SMOFF1="$1"; SMA0="$2"; SMA5="$3"; SMA9="$4"; SMOFF2="$5"
set -- $SMP1EX
SMTRUTH="$1"
SMRES=$(python3 - "$SMEX" "$SMWX" <<'PY'
import sys
ex = [float(v) for v in sys.argv[1].split()]
wx = [float(v) for v in sys.argv[2].split()]
def spread(a, skip):
    st = [(a[i+1] - a[i]) * 1024.0 for i in range(len(a) - 1)][skip:]
    return (max(st) - min(st)) if st else 0.0
# part 2 only, and past the window fill
print("%.3f %.3f" % (spread(ex[5:], 6), spread(wx[5:], 9)))
PY
)
set -- $SMRES
SMEXSPREAD="$1"; SMWXSPREAD="$2"
SMCHK=$(awk -v off1="${SMOFF1:-0}" -v a0="${SMA0:-0}" -v a5="${SMA5:-0}" -v a9="${SMA9:-0}" \
            -v off2="${SMOFF2:-0}" -v t="${SMTRUTH:-1}" -v es="${SMEXSPREAD:-0}" \
            -v ws="${SMWXSPREAD:-99}" 'BEGIN {
    ident = (off1 == t && off2 == t) ? 1 : 0;
    moved = ((a0 > a5 && a5 > a9) || (a0 < a5 && a5 < a9)) ? 1 : 0;
    pulse = (es >= 20.0) ? 1 : 0;              # the engine really is stuttering
    fixed = (ws <= es * 0.15) ? 1 : 0;         # and the drawn track is not
    print ident, moved, pulse, fixed }')
set -- $SMCHK
SMIDENT="$1"; SMMOVED="$2"; SMPULSE="$3"; SMFIXED="$4"
if [ "$GRC" != "0" ]; then
  bad "G60 movement smoothing: the run itself failed (GRC=$GRC)"
elif [ "${SMN:-0}" -lt 20 ]; then
  bad "G60 movement smoothing: wanted 25 samples, got $SMN -- is the gunboat still on Hunt?"
elif [ "$SMNEX" != "1" ]; then
  bad "G60 movement smoothing: THE ENGINE'S OWN POSITION MOVED with the drawn one. ex took $SMNEX distinct values inside one tick [$SMP1EX] and must take exactly 1. Smoothing has leaked into truth, so clicks and orders no longer agree with the simulation"
elif [ "$SMPULSE" != "1" ]; then
  bad "G60 movement smoothing: the ENGINE's own per-tick step spread is only $SMEXSPREAD, so there is no pulse here to remove and the rest of this gate would pass on a parked boat. The scenario or the unit has changed"
elif [ "$SMIDENT" = "1" ] && [ "$SMMOVED" = "1" ] && [ "$SMFIXED" = "1" ]; then
  ok "G60 movement smoothing: truth held at $SMTRUTH inside the tick, the drawn position matches it exactly with the filter off and walks $SMA0 -> $SMA5 -> $SMA9 with it on, and across 20 ticks the engine's own step spread of $SMEXSPREAD is filtered down to $SMWXSPREAD"
else
  bad "G60 movement smoothing: truth=$SMTRUTH identity-when-off=$SMIDENT(want 1) monotone=$SMMOVED(want 1) engine-spread=$SMEXSPREAD drawn-spread=$SMWXSPREAD pulse-removed=$SMFIXED(want 1, needs drawn <= 15% of engine)"
fi

# G59 THE CONSOLE'S 3D CURSORS SURVIVE A VISIT TO THE MENU'S VISUALS SCREEN.
#
# Reported minutes after the Visuals button was fixed: the 3D cursors were gone and only
# the 2D cursors appeared. A regression from that very fix, and the mechanism is worth
# more than the fix is.
#
# c3d_have() resolves the fourteen console cursor models out of g_pack LAZILY on first
# call and latches the answer in g_c3dReady for the LIFETIME OF THE PROCESS. Nothing ever
# reset that flag. Making the menu's Visuals screen draw a pointer meant something asked
# c3d_have() at MENU time, when g_pack is empty: it resolved 0 of 14, printed "keeping the
# 1995 DOS sprite everywhere", set the latch, and every mission booted afterwards in that
# process was stuck with the flat sprite.
#
# WHY THE WHOLE SUITE MISSED IT, and this is the general shape: every renderer gate drives
# cnc_eyes, which has no menu, so there the first c3d_have() has ALWAYS come after the
# pack. The defect needs the APP and it needs the two events in the wrong order. That is
# also why the fix is at the pack load rather than at the menu -- the latch caches a fact
# about g_pack, so it belongs where g_pack changes, and putting it there closes a second,
# latent instance nobody had hit: the shell plays mission after mission in one process and
# each loads its own pack, so mission two was already using mission one's resolution.
#
# --visualsfirst reproduces the sequence exactly: open that screen at menu time, close
# it, then play. MEASURED both ways on the day. With the fix the run says "14 of 14
# console cursor models resolved" after the pack lands; with the reset line removed, that
# line NEVER APPEARS and the mission runs on the DOS sprite.
rm -rf shots/h59; mkdir -p shots/h59
rm -f shots/g59.log
./cnc3d --scen SCG01EC --pack SCG01EA.pack $BASE --menupack dosmenu.pack --visualsfirst \
        --harness 60 --rounds 1 --shotdir shots/h59 > shots/g59.log 2>&1
C3RC=$?
cat shots/g59.log >> "$OUT"
# The menu arm must have happened, or this gate proves nothing: no Visuals visit, no trap.
C3MENU=$(grep -c "visuals: opened and closed at menu time" shots/g59.log)
C3EMPTY=$(grep -c "ONLY 0 of 14 console cursor" shots/g59.log)
C3FULL=$(grep -c "14 of 14 console cursor models resolved" shots/g59.log)
if [ "$C3RC" != "0" ]; then
  bad "G59 3D cursors survive the Visuals screen: the run itself failed (exit $C3RC)"
elif [ "$C3MENU" != "1" ]; then
  bad "G59 3D cursors survive the Visuals screen: the menu visit never happened ($C3MENU), so this run could not have sprung the trap and proves nothing"
elif [ "${C3EMPTY:-0}" -lt 1 ]; then
  bad "G59 3D cursors survive the Visuals screen: the menu visit did NOT resolve against an empty pack ($C3EMPTY) -- the trap has moved, and this gate is no longer watching it"
elif [ "${C3FULL:-0}" -ge 1 ]; then
  ok "G59 3D cursors survive the Visuals screen: the menu resolves 0 of 14 against an empty pack, and the mission then re-resolves all 14 from its own"
else
  bad "G59 3D cursors survive the Visuals screen: the mission never re-resolved them (14-of-14 lines=$C3FULL) -- g_c3dReady is latched and the console cursors are gone"
fi

# G58 ANIMATION IS BLENDED BETWEEN ENGINE TICKS, NOT STAIR-STEPPED ONTO THEM.
#
# Reported: vehicle and structure animations ran at a visibly lower framerate than the
# game itself. They did, and it was arithmetic. The brain
# ticks at 15 Hz, the window presents at 60, and every animation clock in the renderer was
# an INTEGER expression over g_engineFrame, so each pose was held for four presented
# frames. draw_mesh had been resolving two keyframes and lerping between them since PKB
# landed; every driver handed it a whole number, so the mix was always 0.
#
# THIS GATE ASSERTS ENGINE STATE FIRST AND PIXELS SECOND, because the state is exact and
# the pixels only prove it reached the screen. Measured on the day, one engine tick
# advances the yard's clip by 0.667 frames:
#
#     alpha 0.000 -> frame 58.33      alpha 0.999 -> frame 59.00
#     alpha 0.500 -> frame 58.67      next tick   -> frame 59.00
#
#   MONOTONE     the three in-tick samples strictly increase, with the brain not advancing
#                between them. A clock that ignored the fraction gives three equal numbers.
#   CONTINUOUS   alpha 0.999 lands where the NEXT TICK lands. This is the assertion that
#                separates real interpolation from a wobble: it proves the blend walks the
#                interval between two poses rather than adding motion of its own.
#   ON SCREEN    pixels of the crane move between phase 0.0 and phase 0.5, inside
#                gate_crane.py's own box, with no engine tick in between.
#
# And a fourth claim that is not asserted here because the whole suite asserts it: at
# alpha 0 the pose is what the old integer clock produced. --shot and --script never set
# g_tickAlpha, so every other pixel gate keeps measuring what it always measured. If that
# ever stops being true, they go red, not this one.
gbegin shots/g58_a0.png shots/g58_a5.png shots/g58.log
grun shots/g58.log --scen SCG01EC --pack SCG01EA.pack $BASE --noshroud --nominimap \
     --nosidebar --w 900 --h 600 --script gate_tickalpha.txt
gshots shots/g58_a0.png shots/g58_a5.png
TAF=$(sed -n 's/^ANIM|FACT|.*|frame=\([0-9.]*\)$/\1/p' shots/g58.log | tr '\n' ' ')
set -- $TAF
TA0="$1"; TA5="$2"; TA9="$3"; TAT="$4"
TAMONO=$(awk -v a="${TA0:-0}" -v b="${TA5:-0}" -v c="${TA9:-0}" \
             'BEGIN { print (a < b && b < c) ? 1 : 0 }')
TACONT=$(awk -v c="${TA9:-0}" -v t="${TAT:-99}" \
             'BEGIN { d = c - t; if (d < 0) d = -d; print (d <= 0.01) ? 1 : 0 }')
TAPX=$(python3 "$GATEDIR/gate_crane.py" shots/g58_a0.png shots/g58_a5.png 2>/dev/null | cut -d"|" -f2)
if [ "$GRC" != "0" ]; then
  bad "G58 sub-tick blending: the run itself failed or wrote no shot (GRC=$GRC)"
elif [ -z "$TA0" ] || [ -z "$TAT" ]; then
  bad "G58 sub-tick blending: the yard never reached its active segment -- frames read [$TAF]"
elif [ "$TAMONO" = "1" ] && [ "$TACONT" = "1" ] && [ "${TAPX:-0}" -ge 50 ]; then
  ok "G58 sub-tick blending: one engine tick walks the yard's clip $TA0 -> $TA5 -> $TA9 with the brain held still, the last landing on the next tick's own $TAT, and $TAPX pixels of the crane move at half phase"
else
  bad "G58 sub-tick blending: frames [$TAF] monotone=$TAMONO(want 1) continuous-with-next-tick=$TACONT(want 1) moved-at-half-phase=$TAPX(want >=50)"
fi

# G57 THE MAIN MENU'S VISUALS BUTTON DRAWS, AND CLOSING IT WRITES NOTHING.
#
# Two claims, one run, and both of them were unprovable earlier because the
# MENU had no headless entry point at all. That is not a footnote: it is the whole reason
# this button could be born broken in cd0240b on 19 Aug and still be broken the next day
# with the suite green. G42 looks like it covers this screen and does not -- it drives the
# PAUSE route, which reaches the same dialog by a different door and sets the one flag the
# menu route forgot. A gate that cannot fail on a defect is not coverage of it.
#
#   DRAWS      opt_draw's first line is `if (!g_optOpen || !g_dbPack) return;` and the
#              menu route never set g_optOpen, so the screen cleared to black and swapped.
#              MEASURED, both ways, on the day the fix landed: 558 lit pixels before and
#              63441 after, at 1280x720. The 558 is the pointer alone -- itself only there
#              because the same fix calls cur_init, which had also never run on this path.
#              The floor below sits an order of magnitude above the broken case.
#   NO WRITE   closing the screen used to save cnc3d-fx.cfg unconditionally, so merely
#              opening it pinned the player to those dials for ever and no future default
#              could reach them. The binary reports the file's size on the way in and out;
#              they must be equal, because nothing was touched.
rm -f shots/g57_vis.png
VISOUT=$(./cnc3d --visualsshot shots/g57_vis.png --nosound 2>&1)
VISRC=$?
echo "$VISOUT" >> "$OUT"
VISLINE=$(echo "$VISOUT" | grep -m1 '^VISUALSSHOT|')
VISWROTE=$(echo "$VISLINE" | sed -n 's/.*|cfg_written=\([01]\).*/\1/p')
if [ "$VISRC" != "0" ] || [ -z "$VISLINE" ]; then
  bad "G57 menu Visuals screen: the run itself failed (exit $VISRC) [$VISLINE]"
elif [ ! -s shots/g57_vis.png ]; then
  bad "G57 menu Visuals screen: no shot was written [$VISLINE]"
else
  VISPX=$(python3 - <<'PY'
from PIL import Image
import numpy as np
a = np.asarray(Image.open('shots/g57_vis.png').convert('RGB')).astype(int)
print(int((a.sum(axis=2) > 40).sum()))
PY
)
  if [ "${VISPX:-0}" -ge 20000 ] && [ "$VISWROTE" = "0" ]; then
    ok "G57 menu Visuals screen: the dialog draws ($VISPX lit pixels, against 558 measured with the flag missing), and closing it without moving a dial writes no preset"
  else
    bad "G57 menu Visuals screen: lit=$VISPX(want >=20000; 558 is the broken case) cfg_written=$VISWROTE(want 0) [$VISLINE]"
  fi
fi

# G65 THE CONTROL SCHEME: the LEFT button selects AND orders, the RIGHT one cancels.
#
# WHY THIS GATE EXISTS AT ALL. Every other order gate reaches ui_order_at through the
# `order` / `rclick` script verbs, which call the order function DIRECTLY. That is
# deliberate -- it is what lets a gate aim at a CELL instead of a pixel -- but it means
# the suite never presses a mouse button, so the WHOLE SUITE STAYED GREEN through the
# v0.5.9 button swap. G37's autoplay proves the left-click move through real SDL events;
# this proves the half that is easy to get wrong, which is WHICH HALF of the click you
# get for a given target.
#
# Under test is ui_click_action's decision, and it is the engine's own: take the
# selection half only when Best_Object_Action answers ACTION_SELECT(7) or
# ACTION_TOGGLE_SELECT(8), mirroring display.cpp:3492 Command_Object.
#
# PROVEN TO FAIL: drop the `if (ctrl || alt)` override out of ui_click_action and arm 3
# turns from ORDER into SELECT; wire the left button back to ui_left_click alone and
# arm 2 stops producing a CLICK line at all.
rm -f shots/controls_01.png
CTLLOG=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud --nominimap \
             --nosidebar --w 800 --h 500 --script gate_controls.txt 2>&1)
CTLRC=$?
echo "$CTLLOG" >> "$OUT"
# arm 2: a bare left click on the player's OWN Power Plant must SELECT, not order.
CTLSEL=$(echo "$CTLLOG" | grep -c '^CLICK|.*|SELECT$')
# arm 3: the same click with CTRL must reach the order path and come out an ATTACK.
CTLATK=$(echo "$CTLLOG" | grep -c '^ORDER|.*|target=NUKE|.*ATTACK|ctrl')
# arm 4: the right button deselects and issues nothing.
CTLCAN=$(echo "$CTLLOG" | grep -c '^CANCEL|deselect|')
# no order may have been issued by any of the bare left clicks in arms 1, 2 and 4.
CTLORD=$(echo "$CTLLOG" | grep -c '^ORDER|')
# The real line is `SCRIPT|end gate_controls.txt: 50 lines, 0 failures`. Matched exactly,
# because a pattern that misses leaves this EMPTY, and an empty count that defaults to a
# failure is the good direction but still hides which arm actually broke.
CTLFAIL=$(echo "$CTLLOG" | sed -n 's/^SCRIPT|end .*: [0-9]* lines, \([0-9]*\) failures.*/\1/p' | head -1)
if [ "$CTLRC" != "0" ]; then
  bad "G65 controls: the run itself failed (exit $CTLRC)"
elif [ ! -s shots/controls_01.png ]; then
  bad "G65 controls: no shot was written, so the run died before the end of the script"
elif [ "${CTLSEL:-0}" -ge 1 ] && [ "${CTLATK:-0}" -ge 1 ] && [ "${CTLCAN:-0}" -ge 1 ] \
     && [ "${CTLORD:-0}" = "1" ] && [ "${CTLFAIL:-1}" = "0" ]; then
  ok "G65 controls: a left click on the player's own building SELECTS it, the same click with CTRL ATTACKS it, right click deselects, and exactly one order was issued in the whole script"
else
  bad "G65 controls: select-not-order=$CTLSEL(want >=1) ctrl-attack=$CTLATK(want >=1) cancel=$CTLCAN(want >=1) total-orders=$CTLORD(want exactly 1; >1 means a bare left click ordered something) script-failures=$CTLFAIL(want 0)"
fi

# G66 THE SUPERWEAPONS: the cameo is under the right name, and the icon actually fires.
#
# Two independent breaks, one arm each, because fixing one alone leaves either a working
# icon nobody can see or a visible icon that does nothing.
#   (a) the DLL names them SW_Ion/SW_Nuke/SW_AirStrike; every cameo pack is keyed by the
#       1995 stems ION/ATOM/BOMB (sidebar.cpp:1146). The art was never missing.
#   (b) a click went out as a CONSTRUCTION request. CNC_Handle_SuperWeapon_Request is in
#       the shipped brain and was referenced nowhere under game/.
#
# THE LAST ARM IS THE ONE THAT CANNOT BE FAKED. Posting the event proves nothing, since
# the renderer can happily print a fire line for an event the engine drops. The proof is
# that the ENGINE SPENT THE WEAPON: ready (completed=1, progress=1.00) before, recharging
# (constructing=1, progress=0.00) thirty ticks later, with nothing but the click between.
#
# PROVEN TO FAIL: revert sb_art_name to a passthrough and arm 1 reports art=SW_AirStrike;
# drop the `e.type == SPECIAL` arm out of sb_click and arms 2..5 all go silent.
rm -f shots/superweapon_01.png
SWLOG=$(./cnc_eyes --scen SCB70EA --pack SCG01EA.pack $BASE --noshroud --nosound \
            --w 1280 --h 800 --script gate_superweapon.txt 2>&1)
SWRC=$?
echo "$SWLOG" >> "$OUT"
SWART=$(echo "$SWLOG" | grep -c '^SBENT|.*name=SW_AirStrike|art=BOMB|')
SWATOM=$(echo "$SWLOG" | grep -c '^SBENT|.*name=SW_Nuke|art=ATOM|')
SWARM=$(echo "$SWLOG" | grep -c '^SUPER|arm|SW_AirStrike|id=3')
SWDIS=$(echo "$SWLOG" | grep -c '^SUPER|disarm|SW_AirStrike')
SWFIRE=$(echo "$SWLOG" | grep -c '^SUPER|fire|SW_AirStrike|id=3|enginepx=')
# ready BEFORE the shot, recharging AFTER it: first and last dump of the same entry.
SWBEFORE=$(echo "$SWLOG" | grep '^SBENT|.*name=SW_AirStrike' | head -1 | grep -c 'completed=1')
SWAFTER=$(echo "$SWLOG" | grep '^SBENT|.*name=SW_AirStrike' | tail -1 | grep -c 'constructing=1|completed=0')
if [ "$SWRC" != "0" ]; then
  bad "G66 superweapons: the run itself failed (exit $SWRC)"
elif [ ! -s shots/superweapon_01.png ]; then
  bad "G66 superweapons: no shot was written, so the run died before the end of the script"
elif [ "${SWART:-0}" -ge 1 ] && [ "${SWATOM:-0}" -ge 1 ] && [ "${SWARM:-0}" -ge 2 ] \
     && [ "${SWDIS:-0}" -ge 1 ] && [ "${SWFIRE:-0}" = "1" ] \
     && [ "${SWBEFORE:-0}" = "1" ] && [ "${SWAFTER:-0}" = "1" ]; then
  ok "G66 superweapons: the specials carry the 1995 cameo stems (SW_AirStrike->BOMB, SW_Nuke->ATOM), a click arms targeting, right click disarms, a map click fires exactly once, and THE ENGINE SPENT IT (ready before, recharging after)"
else
  bad "G66 superweapons: airstrike-art=$SWART(want >=1, 0 means the SW_* name reached the art lookup) nuke-art=$SWATOM(want >=1) armed=$SWARM(want >=2) disarmed=$SWDIS(want >=1) fired=$SWFIRE(want exactly 1) ready-before=$SWBEFORE(want 1) recharging-after=$SWAFTER(want 1; 0 means the engine ignored the event and only the renderer thinks it fired)"
fi

# G67 THE PIP ROW: the cartridge's storage strip, and it is actually on the glass.
#
# Decoded in docs/pip-row.md (18/18 on its own receipt). The numbers pinned here are the
# ones that go wrong quietly:
#   maxpips 7 for a harvester   FULL_LOAD_CREDITS/100; -1 would mean the brain's field
#                               pair stopped being read and the row silently vanished.
#   height  = max * 48/256      the pitch *0x80097AF4 is 48 on both axes and the pips
#                               TOUCH, so 7 pips are exactly 1.3125 cells.
#   width   = 48/256            same constant, other axis.
#   fill    = count * 48/256    one 4x4 tile per pip, so the boundary lands on a pip edge.
#
# THE LAST ARM IS THE ANTI-VACUITY ONE, and it is the whole reason this gate is not just
# a dump reader: every number above can be perfect while nothing is drawn. The same frame
# is run again with --nopips and the two pictures must DIFFER. A renderer that computes a
# flawless strip and emits no geometry passes every other arm here.
#
# PROVEN TO FAIL: comment out the draw_pip_rows() call and the dump arms still pass while
# the A/B drops to 0 changed pixels.
rm -f shots/piprow_01.png shots/piprow_off.png
PRLOG=$(./cnc_eyes --scen SCG03EA --pack SCG01EA.pack $BASE --noshroud --nosound \
            --nominimap --nosidebar --w 800 --h 500 --script gate_piprow.txt 2>&1)
PRRC=$?
echo "$PRLOG" >> "$OUT"
./cnc_eyes --scen SCG03EA --pack SCG01EA.pack $BASE --noshroud --nosound --nominimap \
    --nosidebar --nopips --w 800 --h 500 --script gate_piprow_off.txt >> "$OUT" 2>&1
PRRC2=$?
PRROW=$(echo "$PRLOG" | grep -c '^PIPROW|HARV|.*|pips=[0-9]*/7|')
PRGEO=$(echo "$PRLOG" | sed -n 's/^PIPROW|HARV|.*|pips=\([0-9]*\)\/\([0-9]*\)|x=\([0-9.-]*\)\.\.\([0-9.-]*\)|y=\([0-9.-]*\)\.\.\([0-9.-]*\)\.\.\([0-9.-]*\)|.*/\1 \2 \3 \4 \5 \6 \7/p' | head -1)
PRGOK=$(python3 - "$PRGEO" <<'PY'
import sys
f = sys.argv[1].split()
if len(f) != 7:
    print("0 no-dump"); raise SystemExit
c, m, x0, x1, yb, yf, yt = int(f[0]), int(f[1]), *[float(v) for v in f[2:]]
P = 48.0 / 256.0
ok  = abs((x1 - x0) - P) < 1e-4                 # 48 leptons wide
ok &= abs((yt - yb) - m * P) < 1e-4             # max pips tall, pips touching
ok &= abs((yf - yb) - c * P) < 1e-4             # fill lands on a pip edge
print("%d w=%.4f h=%.4f fill=%.4f" % (1 if ok else 0, x1 - x0, yt - yb, yf - yb))
PY
)
set -- $PRGOK
PRG="$1"
PRAB=$(python3 - <<'PY'
from PIL import Image
import numpy as np, os
try:
    a = np.asarray(Image.open('shots/piprow_01.png').convert('RGB')).astype(int)
    b = np.asarray(Image.open('shots/piprow_off.png').convert('RGB')).astype(int)
    print(int((np.abs(a - b).sum(axis=2) > 16).sum()))
except Exception:
    print(0)
PY
)
if [ "$PRRC" != "0" ] || [ "$PRRC2" != "0" ]; then
  bad "G67 pip row: a run failed (on=$PRRC off=$PRRC2)"
elif [ ! -s shots/piprow_01.png ] || [ ! -s shots/piprow_off.png ]; then
  bad "G67 pip row: a shot was not written"
elif [ "${PRROW:-0}" -ge 1 ] && [ "$PRG" = "1" ] && [ "${PRAB:-0}" -ge 100 ]; then
  ok "G67 pip row: the harvester carries a 7-pip strip on the cartridge's own 48-lepton pitch ($PRGOK), and turning it off changes $PRAB pixels, so it is on the glass and not just in the dump"
else
  bad "G67 pip row: rows=$PRROW(want >=1, 0 means maxpips never reached the renderer) geometry=$PRGOK(want 1) ab-pixels=$PRAB(want >=100; 0 means the strip is computed and never drawn)"
fi

# G68 THE SILO FILL STRIP: a state readout, not an idle animation.
#
# texbook_mesh_for drove all six texture books off one clock, `(g_engineFrame / 2) % n`.
# The silo's 50 slots are FIVE fill images and the console's arm (RAM 0x8003E0D4) is
# clamp(tib*5/cap, 0, 4) * 10 against the OWNING HOUSE's store, so a player reads his
# silos to see how much tiberium he has. We played them as a 6.7-second loop, which made
# a full silo look exactly like an empty one.
#
# THE TELL DOES NOT NEED A FILL CHANGE, which is what keeps this gate fast: sample the
# same silo at three engine frames with the tiberium unchanged. A readout gives one slot
# three times; a timer gives three different ones. The gate also requires the CLOCK to
# have moved across those samples, so "all three the same" cannot pass by the clock
# happening to sit still.
#
# PROVEN TO FAIL: return -1 from texbook_state_slot and the three slots become 30/33/37.
rm -f shots/silo_01.png
SILOG=$(./cnc_eyes --scen SCG03EA --pack SCG01EA.pack $BASE --noshroud --nosound \
            --nominimap --nosidebar --w 800 --h 500 --script gate_silo.txt 2>&1)
SILRC=$?
echo "$SILOG" >> "$OUT"
# one silo, three samples
SILSTATES=$(echo "$SILOG" | sed -n 's/^SILOBOOK|id=2|.*|state=\([0-9-]*\)|clock=.*/\1/p' | tr '\n' ' ')
SILCLOCKS=$(echo "$SILOG" | sed -n 's/^SILOBOOK|id=2|.*|clock=\([0-9]*\)|.*/\1/p' | tr '\n' ' ')
SILRES=$(python3 - "$SILSTATES" "$SILCLOCKS" <<'PY'
import sys
st = [int(v) for v in sys.argv[1].split()]
ck = [int(v) for v in sys.argv[2].split()]
if len(st) < 3 or len(ck) < 3:
    print("0 0 0 samples=%d" % len(st)); raise SystemExit
readout = 1 if all(v >= 0 for v in st) else 0        # never fell back to the clock
steady  = 1 if len(set(st)) == 1 else 0              # same image at three frames
moved   = 1 if len(set(ck)) > 1 else 0               # and the clock really did move
print("%d %d %d states=%s clocks=%s" % (readout, steady, moved, st[:3], ck[:3]))
PY
)
set -- $SILRES
SILRO="$1"; SILST="$2"; SILMV="$3"
SILHOUSE=$(echo "$SILOG" | grep -c '^HOUSESTORE|BadGuy|tiberium=[0-9]*|capacity=[1-9]')
if [ "$SILRC" != "0" ]; then
  bad "G68 silo strip: the run itself failed (exit $SILRC)"
elif [ ! -s shots/silo_01.png ]; then
  bad "G68 silo strip: no shot was written"
elif [ "${SILHOUSE:-0}" -ge 1 ] && [ "$SILRO" = "1" ] && [ "$SILST" = "1" ] && [ "$SILMV" = "1" ]; then
  ok "G68 silo strip: the owning house's store reaches the renderer and the silo holds ONE fill image across three engine frames while the free-running clock moves underneath it ($SILRES)"
else
  bad "G68 silo strip: housestore=$SILHOUSE(want >=1, 0 means tiberium/capacity are still being thrown away) readout=$SILRO(want 1; 0 means it fell back to the clock) steady=$SILST(want 1; 0 means it is still an animation) clock-moved=$SILMV(want 1, else the steadiness proves nothing) [$SILRES]"
fi

# G69 THE POWER INDICATOR: the colour is decided on watts, as 1995 decides it.
#
# The DOS bar compared bar PIXELS (`dh > ph`, `dh > ph*2`) where power.cpp:475-482
# compares PlayerPtr->Drain against PlayerPtr->Power. Power_Height saturates -- a sixth
# of what is left per 100 units -- so the two rules genuinely disagree and RED was very
# nearly unreachable. The 640 art HUD had no warning of any kind and now shares the rule.
#
# ARM 1 is a MEASUREMENT: SCB35EA starts overdrawn (430 produced, 475 drained), so the
# live reading must be YELLOW and the watts must arrive unchanged from the brain.
# ARM 2 is a CALCULATION and is labelled as one: no shipped scenario begins with drain
# above twice production, so the RED threshold is exercised against the transcribed
# Power_Height instead of in a mission. It asserts the divergence EXISTS, which is the
# thing that makes the fix worth having.
#
# PROVEN TO FAIL: put `dh`/`ph` back into db_draw_power and arm 1 still passes (both
# rules say yellow at 430/475) while arm 2's divergence count is what changes -- which is
# exactly why arm 2 is here and not left as a comment.
rm -f shots/power_01.png
PWLOG=$(./cnc_eyes --scen SCB35EA --pack SCG01EA.pack $BASE --noshroud --nosound \
            --w 1280 --h 800 --script gate_power.txt 2>&1)
PWRC=$?
echo "$PWLOG" >> "$OUT"
PWLINE=$(echo "$PWLOG" | grep -o '^POWER|.*' | head -1)
PWSRC=$(echo "$PWLINE" | sed -n 's/.*|src=\([0-9]*\)\/\([0-9]*\).*/\1 \2/p')
PWW=$(echo "$PWLINE" | sed -n 's/^POWER|watts=\([0-9]*\)\/\([0-9]*\)|.*/\1 \2/p')
PWCW=$(echo "$PWLINE" | sed -n 's/.*|colour_by_watts=\([0-9]*\)|.*/\1/p')
PWMATCH=$([ "$PWSRC" = "$PWW" ] && echo 1 || echo 0)
PWDIV=$(python3 - <<'PY'
# Power_Height, power.cpp:425, transcribed. lim = DB_POW_HEIGHT - 2 = 108.
lim = 108
def ph(value):
    v = max(0, value); num = v // 100; ret = 0
    for _ in range(num):
        ret += (lim - ret) // 6; v -= 100
    if v: ret += ((lim - ret) // 6) * v // 100
    return max(0, min(lim, ret))
def cw(P, D): return 2 if D > P * 2 else (1 if D > P else 0)
def cp(P, D):
    p, d = ph(P), ph(D)
    return 2 if d > p * 2 else (1 if d > p else 0)
n = sum(1 for P in range(100, 1001, 50) for D in range(100, 2001, 50) if cw(P, D) != cp(P, D))
print(n)
PY
)
if [ "$PWRC" != "0" ]; then
  bad "G69 power indicator: the run itself failed (exit $PWRC)"
elif [ ! -s shots/power_01.png ]; then
  bad "G69 power indicator: no shot was written"
elif [ -n "$PWW" ] && [ "$PWMATCH" = "1" ] && [ "$PWCW" = "1" ] && [ "${PWDIV:-0}" -ge 50 ]; then
  ok "G69 power indicator: the brain's watts reach both sidebars unchanged ($PWW), an overdrawn base reads YELLOW on 1995's own watt rule, and the pixel rule it replaced disagrees with that rule on $PWDIV of the sampled power/drain pairs"
else
  bad "G69 power indicator: watts=$PWW src-matches=$PWMATCH(want 1) colour=$PWCW(want 1=yellow; SCB35EA starts 430/475) divergent-pairs=$PWDIV(want >=50; if this is 0 the pixel rule is not actually different and this fix is pointless) [$PWLINE]"
fi

# G70 AIRCRAFT: the heap the object dump walked past for the whole life of the project.
#
# CNC3D_Dump_Objects looped Infantry, Units, Buildings and Terrains and stopped, so no
# aircraft had ever reached the renderer. One missing loop, four player-visible symptoms:
# an invisible Orca on its helipad, an Airstrike whose A-10s never appeared, no Orca ammo
# in the pip row, and Chinook cargo nothing could name.
#
# THE LAST TWO ARMS ARE THE ONES THAT COULD NOT BE WRITTEN BEFORE. v0.5.9 could only
# prove the engine SPENT the airstrike; the aircraft it created were invisible, so
# "Airstrike never comes" stayed true. Here the strike must ADD THREE A-10s and each must
# fly at AircraftClass::FLIGHT_LEVEL, which is 24 (aircraft.h:83).
#
# PROVEN TO FAIL: drop the Aircraft loop out of the dump and arm 1 reports 0 aircraft;
# drop alt= and arms 4 and 5 report lift 0 for an airborne A-10, i.e. aircraft flying
# along the ground.
rm -f shots/aircraft_01.png
ACLOG=$(./cnc_eyes --scen SCB70EA --pack SCG01EA.pack $BASE --noshroud --nosound \
            --w 1100 --h 600 --script gate_aircraft.txt 2>&1)
ACRC=$?
echo "$ACLOG" >> "$OUT"
ACPARKED=$(echo "$ACLOG" | grep -c '^AIRDUMP|aircraft=4')
ACMESH=$(echo "$ACLOG" | grep '^AIR|' | grep -c 'mesh=-1')
ACAMMO=$(echo "$ACLOG" | grep -c '^AIR|ORCA|.*|pips=[0-9]*/5|')
ACGROUND=$(echo "$ACLOG" | grep -c '^AIR|HELI|.*|alt=0|lift=0.0000|')
ACFLY=$(echo "$ACLOG" | grep -c '^AIR|A10|.*|alt=24|lift=0.9375|')
ACAFTER=$(echo "$ACLOG" | grep -c '^AIRDUMP|aircraft=7')
# v0.6.1: the parked Apache must beat the helipad it is standing on, and once selected it
# must carry its ammo pips. Both were broken until aircraft got a pick class of their own.
ACPICK=$(echo "$ACLOG" | grep -c '^SELECT|.*|hit=AIRCRAFT|type=HELI|.*|selected$')
ACPIP=$(echo "$ACLOG" | grep -c '^PIPROW|HELI|.*|pips=[0-9]*/5|')
# v0.6.2: WHICH WAY IS IT POINTING. draw_facing admitted only K_UNIT, so aircraft were
# handed -1 and drawn at their authored pose, which for every aircraft mesh in the pack is
# nose-south. The eight arms above all passed while the entire air force pointed south.
#   FACE  the drawn nose must agree with the direction the aircraft actually flew.
#   BODY  the drawn direction must be the BODY facing (tface = SecondaryFacing, which is
#         what AircraftClass::Draw_It reads), not the flight path (face). Every AIR| line
#         must satisfy renderdir == tface, and none may carry renderdir=-1.
#   TURN  the three A-10s must end the run on three DIFFERENT drawn directions. This is the
#         arm that cannot be satisfied by a stuck value: -1 for all three, or any single
#         frozen pose, scores 1.
ACFACE=$(echo "$ACLOG" | grep -c '^FACEWATCH|A10#0|moved=[0-9]*|render-matches-motion=[0-9]*|.*|PASS$')
ACBODY=$(echo "$ACLOG" | grep -c '^AIR|')
ACBODYOK=$(echo "$ACLOG" | grep '^AIR|' | sed -n 's/.*|tface=\(-*[0-9]*\)|yaw=-*[0-9]*|renderdir=\(-*[0-9]*\)$/\1 \2/p' \
             | awk '$1 == $2 && $1 >= 0 { n++ } END { print n + 0 }')
ACTURN=$(echo "$ACLOG" | grep '^AIR|A10|' | sed -n 's/.*|renderdir=\(-*[0-9]*\)$/\1/p' | sort -u | wc -l | tr -d ' ')
if [ "$ACRC" != "0" ]; then
  bad "G70 aircraft: the run itself failed (exit $ACRC)"
elif [ ! -s shots/aircraft_01.png ]; then
  bad "G70 aircraft: no shot was written"
elif [ "${ACPARKED:-0}" -ge 1 ] && [ "${ACMESH:-1}" = "0" ] && [ "${ACAMMO:-0}" -ge 2 ] \
     && [ "${ACGROUND:-0}" -ge 2 ] && [ "${ACFLY:-0}" -ge 3 ] && [ "${ACAFTER:-0}" -ge 1 ] \
     && [ "${ACPICK:-0}" -ge 1 ] && [ "${ACPIP:-0}" -ge 1 ] && [ "${ACFACE:-0}" -ge 1 ] \
     && [ "${ACBODY:-0}" -ge 1 ] && [ "${ACBODYOK:-0}" = "${ACBODY:-0}" ] && [ "${ACTURN:-0}" -ge 3 ]; then
  ok "G70 aircraft: the Aircraft heap reaches the renderer (4 parked, every one with a mesh), Orcas carry the ROM's 5 ammo pips, parked aircraft sit at lift 0, firing the Airstrike adds THREE A-10s each flying at FLIGHT_LEVEL 24 = 0.9375 cells, a parked Apache WINS the pick against the helipad under it, it draws its ammo strip once selected, and every aircraft is DRAWN where it is pointing: the A-10's nose tracks its flight path over 40 moving ticks, all $ACBODY dumped aircraft draw at their body facing rather than unrotated, and the three A-10s end on $ACTURN different headings"
else
  bad "G70 aircraft: parked=$ACPARKED(want >=1, 0 means the heap is still not exported) meshless=$ACMESH(want 0) orca-ammo=$ACAMMO(want >=2) on-ground=$ACGROUND(want >=2) airborne-A10s=$ACFLY(want >=3; 0 means the strike is still invisible) after-strike=$ACAFTER(want >=1) picked-aircraft=$ACPICK(want >=1; 0 means the helipad won again) ammo-strip=$ACPIP(want >=1) facewatch-A10=$ACFACE(want >=1 PASS line) drawn-at-body-facing=$ACBODYOK/$ACBODY(want all; a shortfall means renderdir is -1 or is following face instead of tface) distinct-A10-headings=$ACTURN(want >=3; 1 means every plane is drawn at one frozen pose)"
fi

# G79 RIGHT EDGE SCROLLING WORKS WHILE THE SIDEBAR IS OUT.
#
# THE FAULT. The right-hand scroll strip was measured from the WINDOW's right edge, so it
# sat wholly underneath the sidebar; the live loop refuses to edge-scroll from over the
# panel, so there was no pixel that both triggered a right scroll and cleared the guard.
# Right edge scrolling was unreachable at every window size, in both HUDs, permanently on
# the default DOS bar because that bar has no drawer to slide out of the way. The strip is
# now measured from the TACTICAL VIEW's right edge instead, which puts it on the map just
# left of the bar.
#
# WHY NOTHING CAUGHT IT, which this gate also closes. The `edge` script verb called the
# scroller RAW, with none of the guard the live loop applies, so the harness scrolled from
# a pixel no player could scroll from. Both paths now go through edge_scroll_allowed, and
# every probe below prints whether the guard passed, so a silent divergence cannot come
# back.
#
# PROVEN TO FAIL: measure the right strip from the window width again (mx >= fbw-EDGE_PX)
# and arm 1 reports east=0.000 with the sidebar out while the --nosidebar control still
# reports 4.046, which is exactly the shape of the bug.
es_x() { echo "$1" | grep "^CAM|$2|" | sed -n "$3p" | sed 's/.*|x=\([0-9.-]*\)|.*/\1/'; }
ESL=$(./cnc_eyes --scen SCG01EB --pack SCG01EA.pack $BASE --nosound --w 1280 --h 720 \
      --script gate_edgescroll.txt 2>&1 | tee -a "$OUT")
ESNL=$(./cnc_eyes --scen SCG01EB --pack SCG01EA.pack $BASE --nosound --nosidebar \
      --w 1280 --h 720 --script gate_edgescroll.txt 2>&1 | tee -a "$OUT")
ESHL=$(CNC3D_HUD=new ./cnc_eyes --scen SCG01EB --pack SCG01EA.pack $BASE --nosound --gfx \
      --w 1280 --h 960 --script gate_edgescroll_h6.txt 2>&1 | tee -a "$OUT")
# The DOS bar is 240 wide at 1280x720, so the map ends at 1040 and the strip is
# [1028,1040). The verb has to AIM there, and printing where it aimed is half the point:
# an `edge R` that still aimed at 1278 would be measuring the dead pixel again.
ESAIM=$(echo "$ESL"  | grep -c '^EDGE|R|frames=20|mouse=1038,360|tacright=1040|guard=pass')
ESBLK=$(echo "$ESL"  | grep -c '^EDGEAT|frames=20|mouse=1278,360|tacright=1040|guard=blocked')
ESEAST=$(es_x "$ESL" edge 1); ESWEST=$(es_x "$ESL" edge 2)
ESBAR=$(es_x "$ESL" edgeat 1); ESOFF=$(es_x "$ESL" edgeat 2)
ESCTL=$(es_x "$ESNL" edge 1)
ESH6OUT=$(es_x "$ESHL" edge 1); ESH6HID=$(es_x "$ESHL" edge 2)
ESH6BAR=$(es_x "$ESHL" edgeat 1); ESH6TAB=$(es_x "$ESHL" edgeat 2)
# Six numeric claims, all against the same start of x=45.000, and each one fails on its
# own. The zero arms matter as much as the moving ones: a fix that scrolled from
# EVERYWHERE would satisfy arm 1 and wreck the game.
ESVERDICT=$(awk -v e="$ESEAST" -v w="$ESWEST" -v b="$ESBAR" -v o="$ESOFF" -v c="$ESCTL" \
                -v h1="$ESH6OUT" -v h2="$ESH6HID" -v h3="$ESH6BAR" -v h4="$ESH6TAB" '
function ab(v) { return v < 0 ? -v : v }
BEGIN{
  s = 45.0; eps = 0.002;
  de = e - s; dw = s - w; dc = c - s; d1 = h1 - s; d2 = h2 - s;
  print (de > 1.0 && ab(de - dw) < eps && ab(de - dc) < eps \
      && ab(b - s) < eps && ab(o - s) < eps \
      && d1 > 1.0 && ab(d1 - d2) < eps && ab(h3 - s) < eps && ab(h4 - s) < eps) ? 1 : 0
}')
if [ "${ESAIM:-0}" -ge 1 ] && [ "${ESBLK:-0}" -ge 1 ] && [ "${ESVERDICT:-0}" = "1" ]; then
  ok "G79 right edge scroll: with the DOS bar out the strip lands on the map at 1028..1039 and pans east $ESEAST-45.000, the same distance west and the same distance as the --nosidebar control ($ESCTL); the window edge at 1278 is on the bar and stays put ($ESBAR), and so does a pixel just left of the strip ($ESOFF). On the 640x480 HUD it pans east both deployed ($ESH6OUT) and with the drawer hidden ($ESH6HID), while the bar itself ($ESH6BAR) and the handle plate ($ESH6TAB) still refuse"
else
  bad "G79 right edge scroll: aimed-at-map=$ESAIM(want >=1; 0 means the verb is still aiming at the window edge) bar-blocked=$ESBLK(want >=1) east=$ESEAST west=$ESWEST control=$ESCTL on-bar=$ESBAR off-strip=$ESOFF h6-out=$ESH6OUT h6-hidden=$ESH6HID h6-bar=$ESH6BAR h6-tab=$ESH6TAB (start 45.000; east/west/control must all differ from it by the same amount, the other four must equal it)"
fi


# =====================================================================================
# G80 THE STAND-IN POINTER DOES NOT GROW WHEN THE CAMERA MOVES UNDER IT.
#
# Over the DOS bar the 1995 sprite stands in for the console's 3D cursor and is sized to
# match the mesh it replaces. That size used to be re-derived at BLIT time from the last
# picked world point, while the pointer sitting on the bar stops the point being picked at
# all -- so any camera motion (arrow keys, edge scroll, a radar jump) walked the camera
# away from a frozen point. px_per_cell_at's depth is linear in that distance, reaches
# zero about 17 cells out and is then clamped, so the scale ran into the tens of thousands
# and one arrow filled the screen. Reported as the cursor scaling enormous while placing a
# building and while clicking up and down the radar.
#
# The script parks the pointer on the bar and then jumps the camera 20 cells north. Two
# assertions, engine state and pixels, because either alone can pass for a bad reason:
#   - the blit scale printed on the CURSOR line must be the SAME string before and after
#   - the camera must actually have moved (a clamped no-op would satisfy the first)
#   - the arrow's own ink in the second picture must still be arrow-sized, and the region
#     right and below the hotspot must not be one flat colour, which is what the runaway
#     looked like: 130x320 px of a single grey, the magnified corner texel
rm -f shots/g80_a.png shots/g80_b.png
gbegin shots/g80_a.png shots/g80_b.png
CSL=$(./cnc_eyes --scen SCG01EA --pack SCG01EA.pack $BASE --noshroud --w 1280 --h 720 \
      --script gate_curscale.txt 2>&1 | tee -a "$OUT")
case "$CSL" in *"SHOT|shots/g80_b.png"*) ;; *) GRC=92 ;; esac
gshots shots/g80_a.png shots/g80_b.png
cs_scale() { echo "$CSL" | grep '^CURSOR|' | sed -n "$1p" | sed 's/.*|scale=\([0-9.]*\).*/\1/'; }
cs_camz()  { echo "$CSL" | grep '^CAM|cam|' | sed -n "$1p" | sed 's/.*|z=\([0-9.-]*\)|.*/\1/'; }
CSA=$(cs_scale 2); CSB=$(cs_scale 3)
CSZ1=$(cs_camz 1); CSZ2=$(cs_camz 2)
CSPIX=$(python3 - <<'PY'
from PIL import Image
import numpy as np
a = np.asarray(Image.open('shots/g80_b.png').convert('RGB')).astype(int)
# the arrow's hotspot is (0,0), so it is drawn right and down from 1150,400
q = a[400:720, 1150:1280].reshape(-1, 3)
flat = 1 if len(np.unique(q, axis=0)) == 1 else 0
m = ((a > 200).all(axis=2) | (a < 25).all(axis=2))[395:560, 1145:1260]
ys, xs = np.nonzero(m)
w = int(xs.max() - xs.min() + 1) if len(xs) else 0
h = int(ys.max() - ys.min() + 1) if len(ys) else 0
print("%d %d %d" % (w, h, flat))
PY
)
set -- $CSPIX
CSW="$1"; CSH="$2"; CSFLAT="$3"
CSMOVED=$(awk -v a="${CSZ1:-0}" -v b="${CSZ2:-0}" 'BEGIN{d=a-b; if(d<0)d=-d; print (d>=15)?1:0}')
if [ "$GRC" != "0" ]; then
  bad "G80 cursor scale: the run failed or wrote no shot (GRC=$GRC)"
elif [ -n "$CSA" ] && [ "$CSA" = "$CSB" ] && [ "$CSMOVED" = "1" ] && [ "$CSFLAT" = "0" ] \
   && [ "${CSW:-0}" -ge 20 ] && [ "${CSW:-0}" -le 80 ] \
   && [ "${CSH:-0}" -ge 30 ] && [ "${CSH:-0}" -le 120 ]; then
  ok "G80 stand-in pointer holds its size: blit scale $CSA before and $CSB after the camera jumped from z=$CSZ1 to z=$CSZ2 with the pointer parked on the bar, and its ink still measures ${CSW}x${CSH} px instead of flooding the quadrant"
else
  bad "G80 cursor scale: scale before=$CSA after=$CSB (must match) camera-moved=$CSMOVED(want 1, z $CSZ1 -> $CSZ2) quadrant-one-flat-colour=$CSFLAT(want 0) ink=${CSW}x${CSH} (want 20..80 by 30..120)"
fi

# =====================================================================================
# G81 A POSITION THAT JUMPS MUST SNAP, AND A SELECTED HULL MUST TAKE ITS BRACKET WITH IT.
#
# Inter-tick movement smoothing is ours, not the cartridge's, so what the filter does when
# its subject TELEPORTS is ours to define too. It stops filtering and draws at engine
# truth, which is what the unsmoothed game does. Two arms, two runs, because they need
# different missions.
#
# ARM A, engine state, on SCG01EA. Three riflemen ride an LST in limbo, reporting the
# object constructor's sentinel coordinate (cell 255.996) every tick of the voyage, and
# step off on engine frame 243. The gate springs its own trap first: frame 242 must really
# say riders=3|limboed=3, or the assertions below are being made about an empty deck.
#   - on 243 every one of the three must have wx == ex and wz == ez EXACTLY. Before this
#     fix the first of them was drawn 254.56 cells from where the engine had put it.
#   - on the four ticks after, the gap must stay inside the filter's own trail (0.05
#     cells; the steady-state trail for a walking rifleman is 0.068 of a cell at phase
#     0.5, and it is unchanged by this fix).
#
# ARM B, pixels, on SCG01EC's gunboat, the same subject G60 uses because it is on Hunt and
# actually travelling. ONE engine tick is drawn twice, at sub-tick phase 0.0 and 0.999.
#   - the drawn position must really differ between the two phases, or the picture test
#     below would pass on a parked boat
#   - the white bracket's centroid must MOVE between the two pictures. It used to move by
#     0.000 px with not one pixel of the mask differing, which is the bug as a number
#   - and it must move by what the HULL moved, measured independently by matching the two
#     pictures with the white pixels masked out. Agreement inside 1 px.
# The crop and the camera are locked together: changing cam, zoom, --w or --h moves the
# boat and the numbers with it.
gbegin shots/g81_a0.png shots/g81_a9.png
SJL=$(./cnc_eyes --scen SCG01EA --pack SCG01EA.pack $BASE --noshroud --nosound \
      --script gate_smoothjump.txt 2>&1 | tee -a "$OUT")
case "$SJL" in *"DRAWPOS|E1|16|"*) ;; *) GRC=93 ;; esac
SJABOARD=$(echo "$SJL" | grep -c '^CARGO|riders=3|limboed=3$')
SJASHORE=$(echo "$SJL" | grep -c '^CARGO|riders=0|limboed=0$')
SJSNAP=$(echo "$SJL" | grep '^DRAWPOS|E1|' | python3 -c '
import sys, re
n = 0
for L in list(sys.stdin)[:3]:          # the three men on the tick they leave limbo
    m = re.match(r"^DRAWPOS\|E1\|\d+\|ex=([-\d.]+)\|ez=([-\d.]+)\|wx=([-\d.]+)\|wz=([-\d.]+)", L)
    if m and m.group(1) == m.group(3) and m.group(2) == m.group(4):
        n += 1
print(n)')
SJTRAIL=$(echo "$SJL" | grep '^DRAWPOS|E1|14|' | python3 -c '
import sys, re
worst = 0.0
for L in sys.stdin:
    m = re.match(r"^DRAWPOS\|E1\|14\|ex=([-\d.]+)\|ez=([-\d.]+)\|wx=([-\d.]+)\|wz=([-\d.]+)", L)
    if not m: continue
    ex, ez, wx, wz = (float(v) for v in m.groups())
    worst = max(worst, abs(wx - ex), abs(wz - ez))
print("%.4f" % worst)')
SJTRAILOK=$(awk -v w="${SJTRAIL:-9}" 'BEGIN{print (w<=0.05)?1:0}')

grun shots/g81b.log --scen SCG01EC --pack SCG01EA.pack $BASE --noshroud --nosound \
     --w 1280 --h 720 --script gate_smoothbracket.txt
gshots shots/g81_a0.png shots/g81_a9.png
SJWX=$(sed -n 's/^DRAWPOS|BOAT|[0-9]*|ex=[0-9.-]*|ez=[0-9.-]*|wx=\([0-9.-]*\)|.*/\1/p' shots/g81b.log | tr '\n' ' ')
set -- $SJWX
SJPHASE=$(awk -v a="${1:-0}" -v b="${2:-0}" 'BEGIN{d=a-b; if(d<0)d=-d; print (d>0.0001)?1:0}')
SJPIX=$(python3 - <<'PY2'
from PIL import Image
import numpy as np
Y0, Y1, X0, X1 = 350, 430, 600, 760           # the boat, without the hull's own white specks
def img(p): return np.asarray(Image.open(p).convert('RGB')).astype(float)
def white(a): return (a[:,:,0]==255)&(a[:,:,1]==255)&(a[:,:,2]==255)
a, b = img('shots/g81_a0.png'), img('shots/g81_a9.png')
wa, wb = white(a), white(b)
def cen(w):
    ys, xs = np.nonzero(w[Y0:Y1, X0:X1])
    return (len(xs), xs.mean(), ys.mean()) if len(xs) else (0, 0.0, 0.0)
na, cx0, cy0 = cen(wa)
nb, cx9, cy9 = cen(wb)
bdx, bdy = cx9 - cx0, cy9 - cy0
# the hull's own shift: best sub-pixel match of the two pictures with the brackets masked
mask = ~(wa | wb)
ga, gb = a.mean(axis=2), b.mean(axis=2)
grid = {}
for dy in range(-4, 5):
    for dx in range(-4, 5):
        M = mask[Y0:Y1, X0:X1] & mask[Y0+dy:Y1+dy, X0+dx:X1+dx]
        grid[(dx, dy)] = (((ga[Y0:Y1, X0:X1] - gb[Y0+dy:Y1+dy, X0+dx:X1+dx])**2)[M]).mean()
dx, dy = min(grid, key=grid.get)
def sub(ax):
    c = grid[(dx, dy)]
    l = grid.get((dx-1, dy)) if ax == 'x' else grid.get((dx, dy-1))
    r = grid.get((dx+1, dy)) if ax == 'x' else grid.get((dx, dy+1))
    return 0.0 if l is None or r is None or (l - 2*c + r) == 0 else 0.5*(l - r)/(l - 2*c + r)
hdx, hdy = dx + sub('x'), dy + sub('y')
print("%d %.3f %.3f %.3f" % (min(na, nb),
                             (bdx*bdx + bdy*bdy)**0.5,
                             (hdx*hdx + hdy*hdy)**0.5,
                             ((bdx-hdx)**2 + (bdy-hdy)**2)**0.5))
PY2
)
set -- $SJPIX
SJN="$1"; SJBRACKET="$2"; SJHULL="$3"; SJGAP="$4"
SJPIXOK=$(awk -v n="${SJN:-0}" -v b="${SJBRACKET:-0}" -v h="${SJHULL:-0}" -v g="${SJGAP:-9}" \
          'BEGIN{print (n>=40 && b>1.0 && h>1.0 && g<1.0)?1:0}')
if [ "$GRC" != "0" ]; then
  bad "G81 smoothing discontinuity: a run failed or wrote no shot (GRC=$GRC)"
elif [ "$SJABOARD" = "1" ] && [ "$SJASHORE" = "1" ] && [ "$SJSNAP" = "3" ] \
   && [ "$SJTRAILOK" = "1" ] && [ "$SJPHASE" = "1" ] && [ "$SJPIXOK" = "1" ]; then
  ok "G81 discontinuity: all 3 riflemen leaving an LST are drawn AT engine truth on the tick they land (wx==ex to 4dp, was 254.56 cells out), the filter trail over the next four ticks stays at $SJTRAIL cells, and a moving gunboat's selection bracket now shifts $SJBRACKET px between sub-tick phases against the hull's $SJHULL px (gap $SJGAP px; it used to shift 0.000)"
else
  bad "G81 discontinuity: aboard-at-242=$SJABOARD(want 1) ashore-at-243=$SJASHORE(want 1) snapped=$SJSNAP(want 3) trail=$SJTRAIL(want <=0.05) phases-differ=$SJPHASE(want 1) bracket=$SJBRACKET px hull=$SJHULL px gap=$SJGAP px whitepx=$SJN(want >=40)"
fi

# =====================================================================================
# G82 A PARKED VEHICLE MUST NOT ANIMATE, AND A BUILDING MUST STILL ANIMATE.
#
# The cartridge drives a vehicle's clip from that vehicle's own state and holds frame 0
# when it is not moving. We had no driver for it at all: the clip time was the global
# engine tick wrapped by the clip length, so an APC parked on Guard turned its hull plate
# for the whole mission and a harvester standing still worked its arms at the same steady
# rate. The clip is now pinned to frame 0 for units, which is the closed-gate half of the
# decoded arm and the only half whose input exists.
#
# SCB13EC, 2560x1440, N64 camera at maximum magnification, engine ticks 180 and 200.
# Three crops, one per subject, all locked to that mission, that camera and that size:
#   APC  id 9  cell 7,60  on Guard          crop 1164,614..1238,693
#   HARV id 29 cell 40,55 facing 32         crop 1301,591..1456,644
#   PYLE id 43 cell 36,3  the control       crop 1302,556..1586,815
#
# THE HARVESTER CROP STOPS AT ROW 644 ON PURPOSE. It stands in a tiberium field it is
# actively eating, and the ground sprites under its lower half change between the two
# ticks whatever the vehicle does. Including them puts about 1200 px of ground animation
# into the count and the assertion below can never reach zero.
#
# THE CONTROL IS THE HALF THAT KEEPS THIS GATE HONEST. "Nothing changed between two
# frames" is exactly what a renderer that has stopped drawing reports, so the Barracks
# must go on differing between the same two ticks or the two zeroes above mean nothing.
#
# And the trap before that: both vehicles must report the same cell, the same facing and
# the same drawn position at both ticks. A subject that turned would move its pixels for a
# reason that has nothing to do with an animation clip.
#
# Measured against the binary from before the fix: APC 276 px, HARV 56 px, PYLE 2221 px.
# After: 0, 0 and 2221.
gbegin shots/g82_apc_a.png shots/g82_apc_b.png shots/g82_harv_a.png \
       shots/g82_harv_b.png shots/g82_pyle_a.png shots/g82_pyle_b.png
grun shots/g82.log --noshroud --nosound --scen SCB13EC --pack SCB13EC.pack $BASE \
     --w 2560 --h 1440 --script gate_vehanim.txt
gshots shots/g82_apc_a.png shots/g82_apc_b.png shots/g82_harv_a.png \
       shots/g82_harv_b.png shots/g82_pyle_a.png shots/g82_pyle_b.png
VASTILL=$(grep -c -e '^OBJ|APC#0|UNIT|GoodGuy|cell=7,60|str=200/200|mission=Guard|face=0$' \
                  -e '^OBJ|HARV#1|UNIT|GoodGuy|cell=40,55|str=600/600|mission=Harvest|face=32$' \
                  -e '^DRAWPOS|APC|9|ex=7.5000|ez=60.5000|wx=7.5000|wz=60.5000|smooth=0|alpha=0.000$' \
                  -e '^DRAWPOS|HARV|29|ex=40.5000|ez=55.5000|wx=40.5000|wz=55.5000|smooth=0|alpha=0.000$' \
                  shots/g82.log 2>/dev/null)
VAPIX=$(python3 - <<'PY3'
from PIL import Image
import numpy as np
def differing(a, b, box):
    A = np.asarray(Image.open(a).convert('RGB').crop(box))
    B = np.asarray(Image.open(b).convert('RGB').crop(box))
    return int((A != B).any(axis=2).sum())
print("%d %d %d" % (
    differing('shots/g82_apc_a.png',  'shots/g82_apc_b.png',  (1164, 614, 1238, 693)),
    differing('shots/g82_harv_a.png', 'shots/g82_harv_b.png', (1301, 591, 1456, 644)),
    differing('shots/g82_pyle_a.png', 'shots/g82_pyle_b.png', (1302, 556, 1586, 815))))
PY3
)
set -- $VAPIX
VAAPC="$1"; VAHARV="$2"; VAPYLE="$3"
if [ "$GRC" != "0" ]; then
  bad "G82 vehicle clips: the run failed or wrote no shot (GRC=$GRC)"
elif [ "$VASTILL" = "8" ] && [ "${VAAPC:-9}" = "0" ] && [ "${VAHARV:-9}" = "0" ] \
   && [ "${VAPYLE:-0}" -gt 500 ]; then
  ok "G82 vehicle clips: across engine ticks 180 and 200 a parked APC and a standing harvester draw byte-identical pixels ($VAAPC and $VAHARV differing, was 276 and 56), while the Barracks control still animates $VAPYLE px in the same two frames"
else
  bad "G82 vehicle clips: subjects-held-still=$VASTILL(want 8) apc=$VAAPC(want 0) harv=$VAHARV(want 0) pyle-control=$VAPYLE(want >500)"
fi


# G83 A DECAL IS LIT BY THE GROUND IT LIES ON. Bibs, scorch marks and craters did not
# read as part of the terrain under them, the power plant's apron most visibly of all.
# The decal pass emitted a flat white primary colour and no per-vertex colour at all, so
# no terrain light reached any bib, scorch or crater, while the ground beside them was
# lit to 160/255. Every decal in the game was therefore about 1.5x brighter than its own
# cell, on every map, flat ground included; smudge_mod.h had recorded the console's
# combiner as "texel * the terrain's own per-corner shade" the whole time.
#
# Three claims, because no one of them can tell the truth on its own.
#   ratio   decal luminance over the luminance of the ground BESIDE it, the ring of
#           pixels within 3 px of the decal read from the --nosmudge shot. Unlit reads
#           1.52; lit twice would read about 0.66. IT IS A WINDOW, NOT A CEILING. The
#           arm was written as "<= 1200" alone, which the doubly-lit 0.66 it names
#           passes comfortably, so the half of the claim the comment cares most about
#           was not being tested at all. The floor is 850: 1.09 measured against 0.66
#           feared, with about the same margin either side.
#   shade   the per-pixel quotient of the lit decal over the SAME decal rendered with
#           --noshade. Under GL_MODULATE that quotient IS the primary colour the pass
#           emitted, whatever the art beneath it, so it measures the light and not the
#           picture. Unlit reads exactly 1.0000.
#   spread  that quotient must VARY. The bibs in this view straddle two lighting bands
#           (corner lit=160 in the north, lit=183 in the south), so a pass emitting one
#           constant colour -- even the right average one -- reads 0 here and fails, and
#           quantisation alone cannot reach 0.05 on pixels this bright.
#
# Measured against the binary from before the fix: ratio=1.5152 shade=1.0000 spread=0.0000.
# After: ratio=1.0851 shade=0.7036 spread=0.1942. Locked to SCG01EC, cam 51.5 51.5,
# zoom min and the default window; move any of those and every number moves.
gbegin shots/bib_none.png shots/bib_lit.png shots/bib_nolit.png
for f in none lit nolit; do
  sed "s#shots/bib_x.png#shots/bib_$f.png#" "$GATEDIR/gate_bibshade.txt" > "/tmp/g83$f.txt"
done
grun - --scen SCG01EC --pack SCG01EA.pack $BASE --noshroud --nosmudge --script /tmp/g83none.txt
grun - --scen SCG01EC --pack SCG01EA.pack $BASE --noshroud --script /tmp/g83lit.txt
grun - --scen SCG01EC --pack SCG01EA.pack $BASE --noshroud --noshade --script /tmp/g83nolit.txt
gshots shots/bib_none.png shots/bib_lit.png shots/bib_nolit.png
if [ "$GRC" != "0" ]; then
  bad "G83 decal light: the run failed or wrote no shot (GRC=$GRC)"
else
  BSL=$(python3 "$GATEDIR/gate_bibshade.py" shots/bib_none.png shots/bib_lit.png \
        shots/bib_nolit.png | tee -a "$OUT")
  # r1000/s1000/p1000 are the ratio, the shade and the spread as integer thousandths.
  # gate_bibshade.py prints them so this shell never has to take a decimal apart, which
  # is where a "0703" that some `test` builds read as octal would come from.
  BSN=$(echo "$BSL" | sed -n 's/.*decalpx=\([0-9]*\).*/\1/p')
  BSR=$(echo "$BSL" | sed -n 's/.*r1000=\([0-9]*\).*/\1/p')
  BSS=$(echo "$BSL" | sed -n 's/.*s1000=\([0-9]*\).*/\1/p')
  BSP=$(echo "$BSL" | sed -n 's/.*p1000=\([0-9]*\).*/\1/p')
  if [ "${BSN:-0}" -ge 2000 ] && [ "${BSR:-9999}" -le 1200 ] && [ "${BSR:-0}" -ge 850 ] \
     && [ "${BSS:-9999}" -ge 600 ] && [ "${BSS:-0}" -le 800 ] \
     && [ "${BSP:-0}" -ge 50 ]; then
    ok "G83 decal light: $BSN decal pixels sit at ${BSR}/1000 of the ground beside them (was 1515), carrying ${BSS}/1000 of the terrain's own light (was 1000, i.e. none) and varying by ${BSP}/1000 across the frame (was 0)"
  else
    bad "G83 decal light: decalpx=$BSN(want >=2000) ratio=$BSR(want 850..1200) shade=$BSS(want 600..800) spread=$BSP(want >=50) -- $BSL"
  fi
fi

# G83b A DECAL IS LIT BY THE GROUND IT LIES ON *WITH THE SHROUD ON TOO*. G83 shoots all
# three of its frames with --noshroud, and that blind spot let a real regression through.
# The shroud became part of the terrain's own vertex light (see G84) 500 lines above the
# decal pass in the renderer, and the decal pass was not given the same factor. Each
# change was defensible on its own and the pair was worse than either: on partly covered
# ground the apron reached the framebuffer through the blanket's translucency alone while
# the ground beside it was dimmed twice, once in its vertex colour and once by the blanket
# over it, so the decal read nearly twice as bright as its own cell and outlined itself
# along the shroud edge. That is the exact symptom G83 was written to remove, an apron
# that does not read as part of the ground, reintroduced with G83 still green.
#
# THE MEASUREMENT IS A QUOTIENT OF QUOTIENTS, so it cannot be fooled by the art.
#   ground  the ground's own response to the shroud: the same pixel with the shroud on
#           over the same pixel with --noshroud, from the two --nosmudge frames. Defined
#           at every pixel, decal or not, because the ground is what those frames show.
#   decal   the same quotient taken from the two frames that DO draw the decals.
#   ratio   decal over ground. Whatever the decal art is, whatever the terrain under it
#           is, the two quotients are the same shroud arithmetic and must agree. Measured
#           before the fix: 1.7789. After: 0.9815. A decal that took the factor TWICE
#           would read about 0.54, which is why this is a window and not a ceiling.
#
# THE MASK AND BOTH DENOMINATORS COME FROM FRAMES THE FIX CANNOT MOVE (the two --noshroud
# frames and the shrouded --nosmudge one). Only the shrouded decal frame changes, so the
# gate cannot grow its own mask as the fix works, which is the trap G84 documents.
#
# WHY THE COVER IS SYNTHETIC. `shrouddarktest` reclassifies the frame's CLEAR cells as
# mapped-but-unseen for one frame, putting a known 0.45 blanket over the very decals G83
# already measures. It is used because no shipped mission frame has one: aprons sit under
# buildings and authored scorch marks sit in the starting base, both of them deep inside
# explored ground, and a search over the first GDI missions found no decal cell whose
# corners touch an unexplored one. The arithmetic under test is one multiply that does not
# care which cover it is: at the reveal feather's own ceiling the same gap is wider still.
# The ground arm doubles as the honesty trap -- if the cover ever stops arriving, the
# ground quotient goes to 1.000 and this fails rather than passing on a vacuous frame.
gbegin shots/g83b_clr_none.png shots/g83b_clr_lit.png shots/g83b_shr_none.png shots/g83b_shr_lit.png
printf 'tick 200\ncam 51.5 51.5\nzoom min\nshot shots/g83b_clr_none.png\nquit\n' > /tmp/g83bcn.txt
printf 'tick 200\ncam 51.5 51.5\nzoom min\nshot shots/g83b_clr_lit.png\nquit\n'  > /tmp/g83bcl.txt
printf 'tick 200\ncam 51.5 51.5\nzoom min\nshrouddarktest\nshot shots/g83b_shr_none.png\nquit\n' > /tmp/g83bsn.txt
printf 'tick 200\ncam 51.5 51.5\nzoom min\nshrouddarktest\nshot shots/g83b_shr_lit.png\nquit\n'  > /tmp/g83bsl.txt
grun - --scen SCG01EC --pack SCG01EA.pack $BASE --noshroud --nosmudge --script /tmp/g83bcn.txt
grun - --scen SCG01EC --pack SCG01EA.pack $BASE --noshroud --script /tmp/g83bcl.txt
grun - --scen SCG01EC --pack SCG01EA.pack $BASE --nosmudge --script /tmp/g83bsn.txt
grun - --scen SCG01EC --pack SCG01EA.pack $BASE --script /tmp/g83bsl.txt
gshots shots/g83b_clr_none.png shots/g83b_clr_lit.png shots/g83b_shr_none.png shots/g83b_shr_lit.png
if [ "$GRC" != "0" ]; then
  bad "G83b decal light under the shroud: the run failed or wrote no shot (GRC=$GRC)"
else
  BQL=$(python3 - <<'PY'
import os, sys
import numpy as np
from PIL import Image, ImageChops, ImageFilter
F = ('shots/g83b_clr_none.png', 'shots/g83b_clr_lit.png',
     'shots/g83b_shr_none.png', 'shots/g83b_shr_lit.png')
for f in F:
    if not os.path.exists(f) or os.path.getsize(f) == 0:
        print('G83BMISSING|' + f); sys.exit(1)
cn, cl, sn, sl = (Image.open(f).convert('RGB') for f in F)
def lum(im):
    a = np.asarray(im).astype(float)
    return 0.299 * a[:, :, 0] + 0.587 * a[:, :, 1] + 0.114 * a[:, :, 2]
# The decal footprint, eroded 2 px so half-covered edge pixels stay out of the average.
mask = ImageChops.difference(cn, cl).convert('L').point(lambda v: 255 if v > 12 else 0)
core = np.asarray(mask.filter(ImageFilter.MinFilter(5))) > 0
Lcn, Lcl, Lsn, Lsl = lum(cn), lum(cl), lum(sn), lum(sl)
H, W = Lcn.shape
reg = np.zeros((H, W), bool)
reg[:, :1040] = True          # left of the sidebar only
reg[:40, :320] = False        # the CAM/FOV HUD plate
ok = core & reg & (Lcn > 24) & (Lcl > 24)     # bright enough to divide by
qg = Lsn / np.maximum(Lcn, 1e-6)
qd = Lsl / np.maximum(Lcl, 1e-6)
band = ok & (qg > 0.05) & (qg < 0.95)         # partly covered, neither clear nor black
n = int(band.sum())
if n == 0:
    print('G83BTHIN|core=%d' % int(ok.sum())); sys.exit(0)
g = float(qg[band].mean()); d = float(qd[band].mean())
print('G83B|corepx=%d|bandpx=%d|ground=%.4f|decal=%.4f|ratio=%.4f'
      '|g1000=%d|d1000=%d|r1000=%d'
      % (int(ok.sum()), n, g, d, d / g,
         round(g * 1000), round(d * 1000), round(1000.0 * d / g)))
PY
)
  echo "$BQL" >> "$OUT"
  BQN=$(echo "$BQL" | sed -n 's/.*|bandpx=\([0-9]*\).*/\1/p')
  BQG=$(echo "$BQL" | sed -n 's/.*|g1000=\([0-9]*\).*/\1/p')
  BQD=$(echo "$BQL" | sed -n 's/.*|d1000=\([0-9]*\).*/\1/p')
  BQR=$(echo "$BQL" | sed -n 's/.*|r1000=\([0-9]*\).*/\1/p')
  if [ -z "$BQR" ]; then
    bad "G83b decal light under the shroud: nothing measurable came back ($BQL)"
  elif [ "${BQN:-0}" -lt 4000 ]; then
    bad "G83b decal light under the shroud: only $BQN decal pixels are partly covered (measured 13497); the frame no longer puts the shroud over a decal, so this gate cannot fail"
  elif [ "${BQG:-0}" -lt 150 ] || [ "${BQG:-9999}" -gt 600 ]; then
    bad "G83b decal light under the shroud: the ground keeps ${BQG}/1000 of its light under the cover (measured 308); that is not a partial cover, so the ratio below means nothing"
  elif [ "${BQR:-9999}" -le 1150 ] && [ "${BQR:-0}" -ge 850 ]; then
    ok "G83b decal light under the shroud: over $BQN partly covered decal pixels the decal keeps ${BQD}/1000 of its unshrouded brightness and the ground beside it ${BQG}/1000, a ratio of ${BQR}/1000 (was 1779, i.e. the decal ignored the shroud the ground had already taken)"
  else
    bad "G83b decal ignores the shroud the ground takes: ratio=$BQR(want 850..1150, was 1779) ground=$BQG decal=$BQD over $BQN px -- $BQL"
  fi
fi

# G84 THE SHROUD MUST DARKEN THE GROUND, NOT JUST BE PAINTED OVER IT. Reports of terrain
# showing through the fog of war. The blanket is translucent by design (a mapped-but-
# unseen cell is covered at 0.45, and the feather band is capped at the ROM's 0.784
# ceiling), so whatever it lies on decides how much the player sees. The console never
# has this problem because the shroud byte is part of the TERRAIN's own vertex light
# (shade * shroudByte / 255, ROM 0x1965C0..0x1965E4): shrouded ground is drawn dark and
# the translucent layer lands on ground that is already dim. Our ground used to be drawn
# at FULL light under the blanket.
#
# The measurement, and why G17 cannot be it: G17 builds its mask from the HARD shroud
# path and both paths were equally lit underneath, so this defect was common mode and
# excluded from that mask by construction. It scored 505 lit pixels here while a third
# of the covered ground was showing. This gate compares against a --noshroud shot of the
# same frame instead, which is the ground at full light with nothing hidden, and counts
# how much of it the shroud still leaves lit.
#
# THE MASK IS FIXED TO ONE ARM ON PURPOSE. A darker arm differs from the lit reference
# over MORE pixels, so a mask rebuilt per arm grows exactly as the fix works and the
# count moves the wrong way for a reason that has nothing to do with the leak. Both arms
# are scored on the mask the `shroudlight 0` arm defines.
#
# `shroudlight 0` reproduces the older draw inside the same binary, so the gate carries
# its own trap: if the arms ever stop disagreeing, the fix is gone.
# Delete first: shots/ persists between runs and a gate that scores the previous run's
# PNGs passes green after a startup crash.
rm -f shots/g84_lit.png shots/g84_off.png shots/g84_on.png
printf 'tick 8\nshot shots/g84_lit.png\nquit\n' > /tmp/g84a.txt
printf 'tick 8\nshroudsoft 1\nshroudlight 0\nshot shots/g84_off.png\nshroudlight 1\nshot shots/g84_on.png\nquit\n' > /tmp/g84b.txt
./cnc_eyes --scen SCG01EA --pack SCG01EA.pack $BASE --noshroud --script /tmp/g84a.txt >> "$OUT" 2>&1
./cnc_eyes --scen SCG01EA --pack SCG01EA.pack $BASE --script /tmp/g84b.txt >> "$OUT" 2>&1
SHL=$(python3 - <<'PY'
import os, sys
from PIL import Image
import numpy as np
for f in ('shots/g84_lit.png', 'shots/g84_off.png', 'shots/g84_on.png'):
    if not os.path.exists(f) or os.path.getsize(f) == 0:
        print('G84MISSING|' + f); sys.exit(1)
lit = np.asarray(Image.open('shots/g84_lit.png').convert('RGB')).astype(int)
off = np.asarray(Image.open('shots/g84_off.png').convert('RGB')).astype(int)
on  = np.asarray(Image.open('shots/g84_on.png').convert('RGB')).astype(int)
H, W, _ = lit.shape
reg = np.zeros((H, W), bool)
reg[:, :1040] = True          # left of the sidebar only
reg[:40, :320] = False        # the CAM/FOV HUD plate
sl = lit.sum(2)
mask = reg & (lit != off).any(2) & (sl > 120)   # covered, and lit underneath
n = int(mask.sum())
if n < 100000:
    print('G84THIN|%d' % n); sys.exit(0)
base = sl[mask].mean()
lo = int((mask & (off.sum(2) > 120)).sum())
ln = int((mask & (on.sum(2)  > 120)).sum())
print('G84|mask=%d|leakoff=%d|leakon=%d|residoff=%d|residon=%d'
      % (n, lo, ln, round(1000.0 * off.sum(2)[mask].mean() / base),
         round(1000.0 * on.sum(2)[mask].mean() / base)))
PY
)
echo "$SHL" >> "$OUT"
SHMASK=$(echo "$SHL" | sed -n 's/^G84|mask=\([0-9]*\).*/\1/p')
SHOFF=$(echo "$SHL" | sed -n 's/.*|leakoff=\([0-9]*\).*/\1/p')
SHON=$(echo "$SHL" | sed -n 's/.*|leakon=\([0-9]*\).*/\1/p')
SHRO=$(echo "$SHL" | sed -n 's/.*|residoff=\([0-9]*\).*/\1/p')
SHRN=$(echo "$SHL" | sed -n 's/.*|residon=\([0-9]*\).*/\1/p')
if [ -z "$SHMASK" ]; then
  bad "G84 shroud light: nothing measurable came back ($SHL)"
elif [ "${SHOFF:-0}" -lt 95000 ]; then
  # The trap. `shroudlight 0` must still reproduce the old draw, or the two arms are
  # measuring the same thing and a green light here means nothing.
  bad "G84 shroud light: the shroudlight 0 arm leaked only $SHOFF pixels (measured 104926); the arms no longer disagree, so this gate cannot fail"
elif [ "${SHON:-999999}" -lt 75000 ] && [ "${SHRN:-9999}" -le 320 ]; then
  ok "G84 shroud light: $SHON of $SHMASK covered lit pixels still read as lit, down from $SHOFF, and the covered ground keeps ${SHRN}/1000 of its unshrouded brightness, down from ${SHRO}/1000"
else
  bad "G84 shroud leaks lit ground: leak=$SHON(want <75000, was $SHOFF) residual=${SHRN}/1000(want <=320, was ${SHRO}/1000)"
fi

# =====================================================================================
# G85 THE ION CANNON: the targeting cursor, and the beam that the cartridge does not have.
#
# TWO INDEPENDENT THINGS, one gate, because they are the two halves of one symptom, an
# Ion Cannon with neither a targeting selector nor a beam, and neither half is
# meaningful without the other.
#
# HALF ONE, THE SELECTOR, IS A FIX. Arming a special used to set an index and nothing
# else, and the cursor's armed-superweapon branch sat BELOW the `no selection` early
# return. Composed, that showed the ion cursor only when something was selected, which is
# the one state the 1995 engine can never be in while targeting (sidebar.cpp:2306 calls
# Unselect_All on the way in), and showed the plain arrow in the state the engine is
# always in. Clicking the cameo with an empty selection, the normal case, changed nothing
# on screen at all while the weapon really was armed and the next map click really did
# fire it. PROVEN TO FAIL: against a binary with only those two changes reverted, arm A
# reads shape=normal and the armed-versus-rest picture differs by ZERO pixels.
#
# HALF TWO, THE BEAM, IS A REGISTERED GAP AND THIS GATE MEASURES IT RATHER THAN FIXING
# IT. The cartridge's own recipe for ANIM_ION_CANNON is empty (10 slots, no sources), no
# anim in the ROM references the Lightning_1..7 or Ion_Spark templates, and the code site
# that fires them was never decoded. So an Ion Cannon strike currently paints NOTHING on
# the tactical map, and arm E asserts exactly that: firing and not firing differ by zero
# map pixels and differ only inside the sidebar, where the cameo goes from ready to
# recharging. THAT ZERO IS THE EVIDENCE THE REGISTER CARRIES. When the beam is decoded
# and built, this arm MUST be rewritten rather than deleted, because at that point a zero
# is the regression.
#
# LOCKED TO SCG74EA, 1280x800, cam 18 47 and the pointer at 500,400. It is the one
# shipped mission whose player owns an Advanced Comm Center at mission start, so SW_Ion
# is in the strip from tick 0; the `superrecharge` verb (the brain's own
# DEBUG_REQUEST_SUPERWEAPON_RECHARGE, gate only) is what makes it READY, because no
# shipped scenario starts with the Ion Cannon charged. Change the scenario, the camera,
# the resolution or the pointer and every number below moves.
IONB="--scen SCG74EA --pack SCG01EA.pack $BASE --noshroud --nosound --w 1280 --h 800"
cat > /tmp/g85_arm.txt <<'EOF'
tick 40
cam 18 47
superrecharge
tick 5
sbdump
sbclickitem SW_Ion
sbdump
cursor 500 400
shot shots/g85_armed.png
EOF
cat > /tmp/g85_rest.txt <<'EOF'
tick 40
cam 18 47
superrecharge
tick 5
tick 1
sbdump
cursor 500 400
shot shots/g85_rest.png
EOF
cat > /tmp/g85_rest2.txt <<'EOF'
tick 40
cam 18 47
superrecharge
tick 5
tick 1
cursor 500 400
shot shots/g85_rest2.png
EOF
# Arm C: a special armed while units are selected must TAKE the selection, which is what
# lets the cursor branch live above the no-selection return at all.
cat > /tmp/g85_sel.txt <<'EOF'
tick 40
cam 18 47
superrecharge
tick 5
lclickobj E1 0
expectsel 1
sbclickitem SW_Ion
cursor 500 400
EOF
cat > /tmp/g85_fire.txt <<'EOF'
tick 40
cam 18 47
superrecharge
tick 5
sbclickitem SW_Ion
actclick 500 400
tick 30
sbdump
efxdump
shot shots/g85_fire.png
EOF
cat > /tmp/g85_nofire.txt <<'EOF'
tick 40
cam 18 47
superrecharge
tick 5
sbclickitem SW_Ion
cancel
tick 30
sbdump
shot shots/g85_nofire.png
EOF
gbegin shots/g85_armed.png shots/g85_rest.png shots/g85_rest2.png \
       shots/g85_fire.png shots/g85_nofire.png
grun /tmp/g85_arm.log    $IONB --script /tmp/g85_arm.txt
grun /tmp/g85_rest.log   $IONB --script /tmp/g85_rest.txt
grun -                   $IONB --script /tmp/g85_rest2.txt
grun /tmp/g85_sel.log    $IONB --script /tmp/g85_sel.txt
grun /tmp/g85_fire.log   $IONB --script /tmp/g85_fire.txt
grun -                   $IONB --script /tmp/g85_nofire.txt
gshots shots/g85_armed.png shots/g85_rest.png shots/g85_rest2.png \
       shots/g85_fire.png shots/g85_nofire.png
# Arm A: the cameo is READY only because the debug charge reached the engine, and arming
# it with an empty selection latches the special BY NAME.
IONRDY=$(sed -n 's/^SBENT|.*name=SW_Ion|.*|constructing=0|completed=1|progress=1.00|.*/1/p' \
             /tmp/g85_arm.log | head -1)
IONARM=$(grep -c '^SBDUMP|armed=1|armedid=1|armedname=SW_Ion' /tmp/g85_arm.log)
IONUNS=$(grep -c '^SUPER|unselect|was=0|now=0' /tmp/g85_arm.log)
IONSHP=$(sed -n 's/^CURSOR|at=500.0,400.0|shape=\([a-z-]*\)|.*/\1/p' /tmp/g85_arm.log | tail -1)
IONREST=$(sed -n 's/^CURSOR|at=500.0,400.0|shape=\([a-z-]*\)|.*/\1/p' /tmp/g85_rest.log | tail -1)
# Arm C: arming took the selection away, and the cursor is the ion one afterwards.
IONTOOK=$(grep -c '^SUPER|unselect|was=1|now=0' /tmp/g85_sel.log)
IONSSHP=$(sed -n 's/^CURSOR|at=500.0,400.0|shape=\([a-z-]*\)|.*/\1/p' /tmp/g85_sel.log | tail -1)
# Arm D: the ENGINE spent the weapon, and it really did create ANIM_ION_CANNON (type 59)
# at the target cell.
#
# THIS ARM WAS REWRITTEN on 25 Aug, exactly as its own failure text asked. It used to
# require `firemap=0` -- the strike painting ZERO map pixels -- and said in as many words
# that a non-zero value "MEANS SOMETHING NOW DRAWS FOR THE STRIKE, which would be the beam
# arriving and this arm must then be rewritten". The beam has arrived. What was true and
# is still true is that the ion cannon spawns no PARTICLES: recipe index 52 is 160 bytes
# of zero in the ROM, so EFXEMPTY is still asserted. What was never true, and was only
# ever a description of missing work, is that nothing should appear: the whole effect is
# geometry, and it is now baked (ION0..ION9) and drawn.
IONSPENT=$(grep -c '^SBENT|.*name=SW_Ion|.*|constructing=1|completed=0|progress=0.00|' /tmp/g85_fire.log)
IONANIM=$(grep -c '^EFXDUMP|ANIM|IONSFX|id=[0-9]*|type=59|' /tmp/g85_fire.log)
IONEMPTY=$(grep -c '^EFXEMPTY|IONSFX|empty-by-design|' /tmp/g85_fire.log)
# THE MESH REACHED draw_mesh. Emitted from inside the ion draw, so a pixel difference
# alone cannot fake it -- and a pixel difference alone is exactly what this arm used to
# forbid, back when the strike drew nothing at all.
IONMESH=$(grep -c '^EFXION|id=[0-9]*|mesh=[0-9]*|' /tmp/g85_fire.log)
IONPIX=$(python3 - <<'PY'
from PIL import Image
import numpy as np
def rd(p): return np.asarray(Image.open(p).convert('RGB')).astype(int)
a, r, r2 = rd('shots/g85_armed.png'), rd('shots/g85_rest.png'), rd('shots/g85_rest2.png')
f, n = rd('shots/g85_fire.png'), rd('shots/g85_nofire.png')
d = (np.abs(a - r).sum(2) > 0)
ys, xs = np.nonzero(d)
# The cursor's own box: the pointer is at 500,400 with a 15,12 hotspot, magnified.
box = 0
if d.sum():
    box = int(xs.min() >= 420 and xs.max() <= 620 and ys.min() >= 320 and ys.max() <= 500)
df = (np.abs(f - n).sum(2) > 0)
print('G85|cursor=%d|inbox=%d|steady=%d|firemap=%d|firebar=%d'
      % (d.sum(), box, (np.abs(r - r2).sum(2) > 0).sum(),
         df[:, :1040].sum(), df[:, 1040:].sum()))
PY
)
echo "$IONPIX" >> "$OUT"
IONCUR=$(echo "$IONPIX"  | sed -n 's/^G85|cursor=\([0-9]*\).*/\1/p')
IONBOX=$(echo "$IONPIX"  | sed -n 's/.*|inbox=\([0-9]*\).*/\1/p')
IONSTDY=$(echo "$IONPIX" | sed -n 's/.*|steady=\([0-9]*\).*/\1/p')
IONFMAP=$(echo "$IONPIX" | sed -n 's/.*|firemap=\([0-9]*\).*/\1/p')
IONFBAR=$(echo "$IONPIX" | sed -n 's/.*|firebar=\([0-9]*\).*/\1/p')
if [ "$GRC" != "0" ]; then
  bad "G85 ion cannon: a run failed (exit $GRC)"
elif [ -z "$IONCUR" ]; then
  bad "G85 ion cannon: nothing measurable came back ($IONPIX)"
elif [ "${IONRDY:-0}" != "1" ] || [ "${IONARM:-0}" -lt 1 ]; then
  bad "G85 ion cannon: the weapon never came up ready-and-armed (ready=$IONRDY want 1, armed-by-name=$IONARM want >=1); superrecharge or the sidebar entry moved, and nothing below means anything"
elif [ "${IONSTDY:-1}" != "0" ]; then
  bad "G85 ion cannon: two identical unarmed runs differ by $IONSTDY pixels, so the picture comparison below cannot mean anything"
elif [ "$IONSHP" = "ion" ] && [ "$IONREST" = "normal" ] && [ "${IONUNS:-0}" -ge 1 ] \
     && [ "${IONCUR:-0}" -gt 800 ] && [ "${IONBOX:-0}" = "1" ] \
     && [ "$IONSSHP" = "ion" ] && [ "${IONTOOK:-0}" -ge 1 ] \
     && [ "${IONSPENT:-0}" -ge 1 ] && [ "${IONANIM:-0}" -ge 1 ] \
     && [ "${IONEMPTY:-0}" -ge 1 ] \
     && [ "${IONFMAP:-0}" -gt 0 ] && [ "${IONMESH:-0}" -ge 1 ] \
     && [ "${IONFBAR:-0}" -gt 2000 ]; then
  ok "G85 ion cannon: arming with an EMPTY selection shows the ion cursor (shape=ion, $IONCUR pixels change inside the pointer's own box, unarmed reads normal and two unarmed runs differ by 0), arming WITH a selection takes it away first, the engine spends the weapon and creates ANIM_ION_CANNON, its PARTICLE recipe is still empty by design, and the strike now paints $IONFMAP map pixels because the beam and its shock rings are real geometry ($IONMESH ION snapshots reached draw_mesh)"
else
  bad "G85 ion cannon: armed-shape=$IONSHP(want ion) unarmed-shape=$IONREST(want normal) unselect-on-arm=$IONUNS(want >=1) cursor-pixels=$IONCUR(want >800; 0 means arming changed nothing on screen) in-cursor-box=$IONBOX(want 1) with-selection-shape=$IONSSHP(want ion) selection-taken=$IONTOOK(want >=1) engine-spent=$IONSPENT(want >=1) anim-created=$IONANIM(want >=1) recipe-empty=$IONEMPTY(want >=1; this is the PARTICLE recipe and it is still legitimately 160 bytes of zero -- the ion cannon spawns no particles on the console either) fire-map-pixels=$IONFMAP(want >0; ZERO now means the beam STOPPED drawing, the regression this arm was rewritten to catch) ion-snapshots-drawn=$IONMESH(want >=1; zero with non-zero map pixels would mean something else is painting there) fire-bar-pixels=$IONFBAR(want >2000, the cameo going to recharging)"
fi

# G85b AN ARMED SUPERWEAPON DOES NOT SURVIVE MISSION TEARDOWN.
#
# THE DEFECT. sb_free() resets every latch that describes a MISSION rather than the
# program, and the armed-special latch was not among them. While that latch was an INDEX
# into g_sbState.entry a leftover was harmless: both readers bailed on
# `index >= entry.size()` and a fresh mission starts with an empty entry list. Making the
# latch an IDENTITY (G85's own fix) removed that accidental guard, so a special left armed
# at the end of one mission arrived in the next one still armed. The application shell
# boots mission after mission in one process, so this is on the played path, not a
# theoretical one.
#
# WHAT IT LOOKED LIKE. Mission two came up with the ion cursor painted over the whole map
# from its first frame, and its first left click was eaten and sent to the engine as a
# SuperWeaponReq carrying MISSION ONE's building type and id.
#
# WHY THIS GATE NEEDS TWO MISSIONS IN ONE PROCESS, and why no earlier gate could see it:
# the standalone renderer boots one mission and exits, so a latch that only misbehaves on
# the SECOND boot is invisible to every gate in this file. The `remission` verb (gate only,
# like superrecharge) runs game_shutdown() then game_boot(), which is the pair the shell
# runs between missions, in that order and with nothing else between them.
#
# PROVEN TO FAIL: against a binary with only the three reset lines in sb_free removed, the
# second mission reads armed=1 armedname=SW_Ion, its cursor reads shape=ion, and clicking
# a rifleman prints SUPER|fire|SW_Ion|id=1 with the selection still empty.
#
# LOCKED TO SCG74EA, the one shipped mission whose player owns an Advanced Comm Center at
# mission start, so SW_Ion is in the strip from tick 0. Same scenario, camera, resolution
# and pointer as G85; change any of them and the numbers move.
SUPB="--scen SCG74EA --pack SCG01EA.pack $BASE --noshroud --nosound --w 1280 --h 800"
cat > /tmp/g85b.txt <<'EOF'
tick 40
cam 18 47
superrecharge
tick 5
sbdump
sbclickitem SW_Ion
sbdump
cursor 500 400
remission
tick 40
cam 18 47
tick 5
sbdump
cursor 500 400
actclickobj E1 0
expectsel 1
EOF
gbegin
grun /tmp/g85b.log $SUPB --script /tmp/g85b.txt
# Mission one: the arm has to have happened, or the rest of this gate proves nothing.
SWARM=$(grep -c '^SBDUMP|armed=1|armedid=1|armedname=SW_Ion' /tmp/g85b.log)
SWCUR1=$(sed -n 's/^CURSOR|at=500.0,400.0|shape=\([a-z-]*\)|.*/\1/p' /tmp/g85b.log | sed -n 1p)
# Two missions really did run in this one process.
SWDOWN=$(grep -c '^REMISSION|shutdown|SCG74EA' /tmp/g85b.log)
SWBOOT=$(grep -c '^REMISSION|booted|SCG74EA|' /tmp/g85b.log)
# Mission two: the last sbdump, the last cursor reading, the first click.
SWARM2=$(grep '^SBDUMP|armed=' /tmp/g85b.log | tail -1)
SWCUR2=$(sed -n 's/^CURSOR|at=500.0,400.0|shape=\([a-z-]*\)|.*/\1/p' /tmp/g85b.log | tail -1)
SWFIRE=$(grep -c '^SUPER|fire|' /tmp/g85b.log)
SWSEL=$(grep -c '^EXPECTSEL|want=1|have=1|PASS' /tmp/g85b.log)
if [ "$GRC" != "0" ]; then
  bad "G85b armed superweapon does not survive teardown: the run failed (exit $GRC)"
elif [ "${SWARM:-0}" -lt 1 ] || [ "$SWCUR1" != "ion" ]; then
  bad "G85b armed superweapon does not survive teardown: mission one never got armed (armed-by-name=$SWARM want >=1, cursor=$SWCUR1 want ion), so there was nothing to carry over and nothing below means anything"
elif [ "${SWDOWN:-0}" != "1" ] || [ "${SWBOOT:-0}" != "1" ]; then
  bad "G85b armed superweapon does not survive teardown: the second mission did not boot in this process (shutdown=$SWDOWN booted=$SWBOOT, both want 1)"
elif [ "$SWARM2" = "SBDUMP|armed=0|armedid=0|armedname=-" ] && [ "$SWCUR2" = "normal" ] \
     && [ "${SWFIRE:-1}" = "0" ] && [ "${SWSEL:-0}" = "1" ]; then
  ok "G85b armed superweapon does not survive teardown: a special armed in mission one is gone by mission two ($SWARM2), the pointer is the plain arrow rather than the ion cursor, and the first left click SELECTS a rifleman instead of launching mission one's weapon"
else
  bad "G85b armed superweapon does not survive teardown: mission two reads [$SWARM2] (want armed=0) cursor=$SWCUR2(want normal) super-fires=$SWFIRE(want 0; 1 means the first click was eaten and mission one's building id was fired) first-click-selected=$SWSEL(want 1)"
fi

# ---- SKIRMISH ------------------------------------------------------------------------
# The computer opponent is the engine's own expert system and every arm of it is gated on
# the game NOT being a normal single player game, so these gates are the only thing in the
# suite that exercises that half of the engine at all.
SKBASE="--cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack --dylib TiberianDawn.dylib --dir missions/ --content content/"
SKMAP=SCM01EA

if [ ! -s "$SKMAP.pack" ] || [ ! -s "missions/$SKMAP.INI" ]; then
  bad "G71..G78 skirmish: $SKMAP.pack or missions/$SKMAP.INI is not installed, so no skirmish gate could run. Run tools/stage-skirmish-maps.sh first."
else

# G71 A SKIRMISH STARTS AND THE TWO SIDES ARE TOLD APART.
# Both houses are called MultiN, so the only thing that can answer "which side is this"
# is the act= field. If it is missing or wrong the player's own army draws in the enemy's
# livery, which is what happened before the field existed.
rm -f shots/gate_skirmish.png
SKLOG=$(./cnc_eyes --scen $SKMAP --pack $SKMAP.pack $SKBASE --skirmish --side gdi \
        --script gate_skirmish.txt 2>&1)
SKRC=$?
echo "$SKLOG" >> "$OUT"
SKHUMAN=$(echo "$SKLOG" | grep -c '^HOUSE|Multi[0-9]|.*|human=1|player=1|')
SKAI=$(echo "$SKLOG" | grep -c '^HOUSE|Multi[0-9]|.*|human=0|player=0|.*|buildings=[1-9]')
SKGDI=$(echo "$SKLOG" | grep '^OBJ|' | grep -c '|Multi[0-9]|.*|act=0$')
SKNOD=$(echo "$SKLOG" | grep '^OBJ|' | grep -c '|Multi[0-9]|.*|act=1$')
if [ "$SKRC" != "0" ]; then
  bad "G71 skirmish start: the run itself failed (exit $SKRC)"
elif [ "${SKHUMAN:-0}" -ge 1 ] && [ "${SKGDI:-0}" -ge 1 ] && [ "${SKNOD:-0}" -ge 1 ]; then
  ok "G71 skirmish starts: one human MultiN house, and both sides are distinguishable ($SKGDI objects report act=0 GDI, $SKNOD report act=1 Nod)"
else
  bad "G71 skirmish start: human-houses=$SKHUMAN(want 1) gdi-objects=$SKGDI(want >=1) nod-objects=$SKNOD(want >=1; either being 0 means act= is missing and every object would draw the same side)"
fi

# G72 THE COMPUTER BUILDS A BASE. This is the one gate that proves the opponent is alive.
# Every other skirmish gate passes just as happily against a computer house that sits
# still, which is exactly what happens if the multiplayer flag is not set.
if [ "${SKAI:-0}" -ge 1 ]; then
  SKBLD=$(echo "$SKLOG" | grep '^HOUSE|Multi' | grep 'human=0' | sed -n 's/.*|buildings=\([0-9]*\)|.*/\1/p' | tail -1)
  ok "G72 the computer built a base with no help: $SKBLD buildings by tick 1500, from a start of none"
else
  bad "G72 the computer built NOTHING. Either the game is not running as multiplayer (the expert system is gated on that) or the computer house was never flagged as one"
fi

# G73 THE OPENING CLICK. On the retail maps the starting escort can stand on the 3x3
# Construction Yard pad, and the engine then answers ACTION_NO_DEPLOY and discards the
# click. That is the player's FIRST action, so a skirmish that fails it is unplayable.
SKCUR=$(echo "$SKLOG" | grep -c '^CURSOROBJ|MCV:@me#0|.*|shape=deploy$')
SKFACT=$(echo "$SKLOG" | grep '^OBJ|BUILDING|FACT|' | grep -c '|act=0$')
if [ "${SKCUR:-0}" -ge 1 ] && [ "${SKFACT:-0}" -ge 1 ]; then
  ok "G73 the opening click works: the cursor over the player's own MCV reads deploy, and the click puts a Construction Yard on the map"
else
  bad "G73 the opening click: cursor-reads-deploy=$SKCUR(want 1; 0 means the pad is blocked and the first click is dead) player-conyard=$SKFACT(want >=1)"
fi

# G74 THE SIDE REACHES THE ART. Same map, same seed, the two sides swapped. If the side
# were not driving the house textures the two frames would be identical.
rm -f shots/sk_gdi.png shots/sk_nod.png
./cnc_eyes --scen $SKMAP --pack $SKMAP.pack $SKBASE --skirmish --side gdi \
   --shot shots/sk_gdi.png --ticks 400 >>"$OUT" 2>&1
SKRG=$?
./cnc_eyes --scen $SKMAP --pack $SKMAP.pack $SKBASE --skirmish --side nod \
   --shot shots/sk_nod.png --ticks 400 >>"$OUT" 2>&1
SKRN=$?
if [ "$SKRG" != "0" ] || [ "$SKRN" != "0" ]; then
  bad "G74 side reaches the art: a run failed (exit $SKRG / $SKRN)"
elif [ ! -s shots/sk_gdi.png ] || [ ! -s shots/sk_nod.png ]; then
  bad "G74 side reaches the art: a shot was not written"
else
  SG=$(shasum shots/sk_gdi.png | cut -d' ' -f1); SN=$(shasum shots/sk_nod.png | cut -d' ' -f1)
  if [ "$SG" != "$SN" ]; then
    ok "G74 the chosen side reaches the picture: playing GDI and playing Nod render differently on the same map (${SG:0:12} vs ${SN:0:12})"
  else
    bad "G74 the chosen side does NOT reach the picture: both sides rendered the identical frame ${SG:0:12}, so the side is being ignored"
  fi
fi

# G75 A SKIRMISH IS REPRODUCIBLE, and it is worth asserting rather than assuming: the
# engine's own scenario seeding is dead code on this path, which is WHY two runs match,
# and that is an accident that could be undone by a change elsewhere.
rm -f shots/sk_r1.png shots/sk_r2.png
./cnc_eyes --scen $SKMAP --pack $SKMAP.pack $SKBASE --skirmish --side gdi \
   --shot shots/sk_r1.png --ticks 400 >>"$OUT" 2>&1
./cnc_eyes --scen $SKMAP --pack $SKMAP.pack $SKBASE --skirmish --side gdi \
   --shot shots/sk_r2.png --ticks 400 >>"$OUT" 2>&1
if [ ! -s shots/sk_r1.png ] || [ ! -s shots/sk_r2.png ]; then
  bad "G75 skirmish reproducibility: no shot was written"
else
  R1=$(shasum shots/sk_r1.png | cut -d' ' -f1); R2=$(shasum shots/sk_r2.png | cut -d' ' -f1)
  if [ "$R1" = "$R2" ]; then ok "G75 two skirmish runs are identical ${R1:0:12}"
  else bad "G75 skirmish reproducibility $R1 vs $R2"; fi
fi

# G76 THE OPENING CLICK, ON EVERY SHIPPED MAP AND BOTH STARTS. G73 proves one map. This
# proves the DEFAULT escort size is still the right one, which is a measured property and
# not a taste: the engine scatters the escort within about four cells of the MCV and any
# unit landing inside the 3x3 Construction Yard pad kills the player's first click. Sweeping
# every map at both starts, an escort of 0 leaves 17 of 18 starts deployable, 1 leaves 10,
# 5 leaves 4 and 6 or more leaves none. If a change makes this number fall, the first thing
# a player does stops working and nothing else in the suite would notice.
SKOK=0; SKTOT=0; SKDEAD=""
for skm in missions/SCM0*.INI; do
  skn=$(basename "$skm" .INI)
  [ -s "$skn.pack" ] || continue
  for skst in 0,1 1,0; do
    SKTOT=$((SKTOT+1))
    if ./cnc_eyes --scen $skn --pack $skn.pack $SKBASE --skirmish --side gdi \
         --starts $skst --script gate_deploy.txt 2>>"$OUT" | grep -q 'shape=deploy'; then
      SKOK=$((SKOK+1))
    else
      SKDEAD="$SKDEAD $skn/$skst"
    fi
  done
done
if [ "$SKTOT" -lt 18 ]; then
  bad "G76 opening click sweep: only $SKTOT of 18 starts could be tested, so maps or packs are missing"
elif [ "$SKOK" -ge 17 ]; then
  ok "G76 the opening click works on $SKOK of $SKTOT shipped starts at the default escort size (the measured best; dead:${SKDEAD:- none})"
else
  bad "G76 the opening click works on only $SKOK of $SKTOT shipped starts, which is worse than the measured best of 17. Dead:$SKDEAD"
fi

# G77 THE CHEAT MENU. Five testing switches on the star key. Four of them are the
# engine's own testing interface and one is ours, and the whole point of them is that a
# tester can turn a rule off in one click, so a broken one is worse than none: it looks
# like it worked and quietly did nothing.
#
# The switches are asserted by their EFFECT, not by their checkbox. Money must actually
# arrive, the build list must actually grow, the shroud must actually lift, and a build
# must actually finish. The one that is not the engine's, fog of war, is the one most
# likely to rot, because what the player sees hidden is decided on our side.
rm -f shots/gate_cheats.png
CHLOG=$(./cnc_eyes --scen $SKMAP --pack $SKMAP.pack $SKBASE --skirmish --side gdi \
        --script gate_cheats.txt 2>&1)
CHRC=$?
echo "$CHLOG" >> "$OUT"
# MATCH THE SWITCH LINE, not just the prefix: the dump prints a second line reporting
# what was last SENT to the engine, and a bare prefix match with tail -1 lands on that
# one, which carries neither the credits nor the shroud. That is how the first draft of
# this gate reported "the money switch did nothing" against a build where it worked.
CHBEFORE=$(echo "$CHLOG" | grep -m1 '^CHEATDUMP|money=')
CHAFTER=$(echo "$CHLOG" | grep '^CHEATDUMP|money=' | tail -1)
# The shipped defaults, which are the ordinary game: every cheat off, fog of war on.
CHDEF=$(echo "$CHBEFORE" | grep -c '|money=0|instant=0|tech=0|fog=1|invuln=0|shroudon=1|')
# Money arrived, the shroud lifted.
CHMONEY=$(echo "$CHAFTER" | sed -n 's/.*|credits=\([0-9]*\).*/\1/p')
CHFOG=$(echo "$CHAFTER" | grep -c '|shroudon=0|')
# The build list grew, and a build finished inside a second.
CHENT0=$(echo "$CHLOG" | grep -c '^SBDUMP|entries=4$')
CHDONE=$(echo "$CHLOG" | grep -c '^SBENT|0|name=NUKE|.*|completed=1|')
CHPAGE=$(echo "$CHLOG" | grep -c '^CHEATS|open|page=5|')
if [ "$CHRC" != "0" ]; then
  bad "G77 cheat menu: the run itself failed (exit $CHRC)"
elif [ ! -s shots/gate_cheats.png ]; then
  bad "G77 cheat menu: no shot was written, so the page did not draw"
elif [ "${CHDEF:-0}" = "1" ] && [ "${CHMONEY:-0}" -ge 20000 ] && [ "${CHFOG:-0}" = "1" ] \
     && [ "${CHDONE:-0}" -ge 1 ] && [ "${CHPAGE:-0}" = "1" ]; then
  ok "G77 cheat menu: the defaults are the ordinary game (every cheat off, fog of war on), and every switch ACTS -- money reached $CHMONEY, the shroud lifted, a power plant finished inside a second, and the page draws"
else
  bad "G77 cheat menu: defaults-correct=$CHDEF(want 1) credits=$CHMONEY(want >=20000; the money switch did nothing) shroud-lifted=$CHFOG(want 1) build-finished=$CHDONE(want >=1; instant build did nothing) page-drew=$CHPAGE(want 1)"
fi

# G78 THE BUILD LIST SWITCH, on its own, because it is the one with a trap in it: the
# engine's request FLIPS the flag rather than setting it, so a client that sends it every
# tick toggles it every tick, and a client that starts from "unknown" unlocks a build list
# nobody asked to unlock. Both of those were live bugs. The assertion is that the list is
# SHORT before and LONG after, which neither bug can satisfy.
CHSHORT=$(echo "$CHLOG" | grep -m1 '^SBDUMP|entries=' | sed -n 's/.*entries=\([0-9]*\).*/\1/p')
CHLONG=$(echo "$CHLOG" | grep '^SBDUMP|entries=' | tail -1 | sed -n 's/.*entries=\([0-9]*\).*/\1/p')
if [ -n "$CHSHORT" ] && [ -n "$CHLONG" ] && [ "$CHLONG" -gt "$CHSHORT" ]; then
  ok "G78 the tech tree switch: the build list grows from $CHSHORT entries to $CHLONG, and is NOT unlocked before it is asked for"
else
  bad "G78 the tech tree switch: entries before=$CHSHORT after=$CHLONG. Equal means either the switch does nothing, or the list was already unlocked at tick 1 (the request toggles rather than sets)"
fi

fi

# ---- THE SKIRMISH LOBBY ---------------------------------------------------------------
# G86 and G87 start at 86 on purpose: G79 to G85 are allocated to the player bug board.
#
# G86 THE SCREEN. Opens the lobby and works every control on it with synthetic events,
# one frame per step. The assertion that matters is the string fit: db_print clips to the
# SURFACE and never to a caller's box, so a label wider than its button prints straight
# out across the battlefield, and this screen has shipped that defect twice already. The
# check measures every string it draws against the box it draws into.
rm -rf shots/lobby && mkdir -p shots/lobby
# Run to a file under a watchdog rather than through a pipe. `X=$(cmd | tee)` reports
# TEE's exit status, which is always 0, so the "run itself failed" branch below could
# never fire and a crash scored as a frame shortage. And with no maps installed the
# lobby refuses to open and the app parks at the menu forever, which wedges the suite.
./cnc3d --lobbyshot shots/lobby --dylib ./TiberianDawn.dylib --dir ./missions/ \
        --content ./content/ --cameos cameos.pack --dospack dossidebar.pack \
        --dosinf dosinfantry.pack > shots/lobby86.log 2>&1 &
LBP=$!; LBW=0
while kill -0 $LBP 2>/dev/null && [ $LBW -lt 180 ]; do sleep 1; LBW=$((LBW+1)); done
if kill -0 $LBP 2>/dev/null; then kill $LBP 2>/dev/null; wait $LBP 2>/dev/null; LBRC=timeout
else wait $LBP; LBRC=$?; fi
LBLOG=$(cat shots/lobby86.log); echo "$LBLOG" >> "$OUT"
LBFIT=$(echo "$LBLOG" | sed -n 's/^LOBBY|fit|\([0-9]*\) strings measured|\([0-9]*\) overflow/\1 \2/p')
LBMEAS=$(echo "$LBFIT" | cut -d' ' -f1); LBOVER=$(echo "$LBFIT" | cut -d' ' -f2)
LBSHOTS=$(ls shots/lobby/*.png 2>/dev/null | wc -l | tr -d ' ')
# The map count the app scanned. Asserted, not assumed: packs are gitignored build
# products, so a clean checkout has none, and a lobby with an empty map list must fail
# this gate rather than quietly pass it having proved nothing.
LBMAPS=$(echo "$LBLOG" | sed -n 's/^APP|skirmish|\([0-9]*\) maps$/\1/p' | tail -1)
if [ "$LBRC" != "0" ]; then
  bad "G86 lobby screen: the run itself failed (exit $LBRC)"
elif [ "${LBMAPS:-0}" -lt 1 ]; then
  bad "G86 lobby screen: this tree has ${LBMAPS:-0} skirmish maps installed, so the lobby never opened and the gate proves nothing. Run tools/stage-skirmish-maps.sh first."
elif [ "${LBSHOTS:-0}" -lt 8 ]; then
  bad "G86 lobby screen: only $LBSHOTS frames were written, so the controls were not driven"
elif [ "${LBMEAS:-0}" -ge 30 ] && [ "${LBOVER:-1}" = "0" ]; then
  ok "G86 lobby screen: every control drives over $LBMAPS maps, $LBSHOTS frames rendered, and all $LBMEAS strings it draws fit inside their own boxes"
else
  bad "G86 lobby screen: strings measured=$LBMEAS(want >=30; a low count means the check ran on an empty screen and proved nothing) overflowing=$LBOVER(want 0)"
fi

# G87 THE ROUTE. The screen drawing correctly is not the feature; the feature is that what
# you click is the match you get. This goes the real way -- a click on Skirmish, the
# lobby, Play -- and then reads the ENGINE'S OWN report of the lobby it was handed and
# checks it against what the app said it sent. A control that moves on screen and changes
# nothing is the exact failure this screen exists to avoid. Watchdogged and map-counted
# for the same two reasons as G86.
./cnc3d --lobbyplay 30 --dylib ./TiberianDawn.dylib --dir ./missions/ \
        --content ./content/ --cameos cameos.pack --dospack dossidebar.pack \
        --dosinf dosinfantry.pack > shots/lobby87.log 2>&1 &
LPP=$!; LPW=0
while kill -0 $LPP 2>/dev/null && [ $LPW -lt 180 ]; do sleep 1; LPW=$((LPW+1)); done
if kill -0 $LPP 2>/dev/null; then kill $LPP 2>/dev/null; wait $LPP 2>/dev/null; LPRC=timeout
else wait $LPP; LPRC=$?; fi
LPLOG=$(cat shots/lobby87.log); echo "$LPLOG" >> "$OUT"
LPMAPS=$(echo "$LPLOG" | sed -n 's/^APP|skirmish|\([0-9]*\) maps$/\1/p' | tail -1)
LPSENT=$(echo "$LPLOG" | grep -m1 '^APP|lobby|')
LPSIDE=$(echo "$LPSENT" | sed -n 's/.*|side=\([A-Za-z]*\).*/\1/p')
LPAI=$(echo "$LPSENT" | sed -n 's/.*|ai=\([0-9]*\).*/\1/p')
LPCR=$(echo "$LPSENT" | sed -n 's/.*|credits=\([0-9]*\).*/\1/p')
# The engine's own line, which is written by the brain and not by the screen.
LPGOT=$(echo "$LPLOG" | grep -m1 '^skirmish: ')
LPPLAYERS=$(echo "$LPGOT" | sed -n 's/^skirmish: \([0-9]*\) player.*/\1/p')
LPGOTSIDE=$(echo "$LPGOT" | sed -n 's/.*human plays \([A-Za-z]*\).*/\1/p')
LPGOTCR=$(echo "$LPGOT" | sed -n 's/.*, \([0-9]*\) credits.*/\1/p')
LPDONE=$(echo "$LPLOG" | grep -c '^LOBBYPLAY|complete')
if [ "${LPMAPS:-0}" -lt 1 ]; then
  bad "G87 lobby route: this tree has ${LPMAPS:-0} skirmish maps installed, so there is no match to start and the gate proves nothing. Run tools/stage-skirmish-maps.sh first."
elif [ "$LPRC" != "0" ] || [ "${LPDONE:-0}" != "1" ]; then
  bad "G87 lobby route: the run did not complete (exit $LPRC, complete=$LPDONE)"
elif [ -n "$LPSIDE" ] && [ "$LPSIDE" = "$LPGOTSIDE" ] \
     && [ -n "$LPCR" ] && [ "$LPCR" = "$LPGOTCR" ] \
     && [ -n "$LPAI" ] && [ "$LPPLAYERS" = "$((LPAI + 1))" ]; then
  ok "G87 lobby route: clicking through the lobby and pressing Play starts exactly that match -- the screen asked for $LPSIDE, $LPAI opponents and $LPCR credits, and the engine reports $LPPLAYERS players, human plays $LPGOTSIDE, $LPGOTCR credits"
else
  bad "G87 lobby route: what the screen sent and what the engine got disagree. screen side=$LPSIDE ai=$LPAI credits=$LPCR; engine players=$LPPLAYERS side=$LPGOTSIDE credits=$LPGOTCR"
fi

# G88 CONTROL GROUPS GO THROUGH THE ENGINE, AND COME BACK.
#
# The renderer keeps no group list: the number keys call
# CNC_Handle_ControlGroup_Request, which is Handle_Team, and the selection is read back
# from CNC3D_Dump_Selection. So this gate is really asking whether that round trip holds.
#
# Four legs, and the last two are the ones worth having. Leg 2 asserts that assigning a
# unit to a second group EMPTIES the first, which is the engine's rule
# (conquer.cpp:3088-3095) and is exactly what a private shadow list would get wrong. Leg 3
# asserts that the additive action selects the GROUP ALONGSIDE what is held rather than
# putting the held units into the group; the first version of this code called that "add"
# and had it backwards, and only a measurement said so.
#
# The camera leg differences the two CAM|state lines either side of the ALT recall. The
# centring is ours, deliberately: Handle_Team centres the ENGINE's tactical rectangle,
# which this renderer does not use.
#
# No tee. The pipeline's exit status is tee's, not the game's, which is the defect eight
# gates in this file carried until the pass that fixed them.
rm -f shots/g88.log
GRPLOG=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud \
             --script gate_group.txt 2>&1)
GRPRC=$?
echo "$GRPLOG" >> "$OUT"
GRPPASS=$(echo "$GRPLOG" | grep -c '^EXPECTSEL|.*|PASS$')
GRPFAIL=$(echo "$GRPLOG" | grep -c '^EXPECTSEL|.*|FAIL$')
GRPADD=$(echo "$GRPLOG" | grep -c '^GROUP|addselect|key=2|slot=1|selection=2$')
GRPDROP=$(echo "$GRPLOG" | grep -c '^GROUP|recall|key=1|slot=0|selection=0$')
GRPSCRIPT=$(echo "$GRPLOG" | grep -c 'SCRIPT|end gate_group.txt: .*, 0 failures')
# the camera must MOVE on the ALT recall: two CAM|state lines, different x
GRPCAM=$(echo "$GRPLOG" | grep '^CAM|state|' | sed -n 's/^CAM|state|x=\([0-9.]*\).*/\1/p' \
         | uniq | wc -l | tr -d ' ')
if [ "$GRPRC" != "0" ]; then
  bad "G88 control groups: the run itself failed (exit $GRPRC)"
elif [ "$GRPPASS" = "8" ] && [ "$GRPFAIL" = "0" ] && [ "$GRPADD" = "1" ] \
     && [ "$GRPDROP" = "1" ] && [ "$GRPSCRIPT" = "1" ] && [ "$GRPCAM" = "2" ]; then
  ok "G88 control groups: assign/recall round trips through the engine ($GRPPASS selection assertions), re-assigning to another group empties the first, SHIFT selects the group ALONGSIDE what is held (2 selected from a group of 1), and ALT moves the camera onto it"
else
  bad "G88 control groups: selection-asserts=$GRPPASS/8 fails=$GRPFAIL addselect=$GRPADD(want 1) old-group-emptied=$GRPDROP(want 1) script-clean=$GRPSCRIPT(want 1) camera-moved=$GRPCAM(want 2 distinct x)"
fi

# G89 ABORT ASKS FIRST, AND THE THREE ANSWERS ARE DISTINCT.
#
# The mission used to end on the Abort click. 1995 raises a message box and only queues
# the exit if the answer is Abort (goptions.cpp:421-447). Read off the act= field optclick
# already prints, so no diagnostic line changed and no anchored pattern is disarmed.
# DOPT_ACT_NONE 0, DOPT_ACT_ABORT 2, DOPT_ACT_RESTART 6, DOPT_PAGE_CONFIRM 6.
#
# The leg that matters most is the last: Abort and Restart differ by one enum value and
# land in adjacent arms, which is the pair that gets crossed. A Restart wired to abort's
# arm would send the player to the main menu instead of back into the mission, and every
# other leg here would still pass.
CFLOG=$(./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE --noshroud \
            --script gate_confirm.txt 2>&1)
CFRC=$?
echo "$CFLOG" >> "$OUT"
CFOPEN=$(echo "$CFLOG" | grep -c '^OPTIONS|script|click|Abort Mission|.*|act=0|page=6$')
CFCANCEL=$(echo "$CFLOG" | grep -c '^OPTIONS|script|click|Cancel|.*|act=0|page=0$')
CFABORT=$(echo "$CFLOG" | grep -c '^OPTIONS|script|click|Abort|.*|act=2|page=6$')
CFRESTART=$(echo "$CFLOG" | grep -c '^OPTIONS|script|click|Restart|.*|act=6|page=6$')
CFCLEAN=$(echo "$CFLOG" | grep -c 'SCRIPT|end gate_confirm.txt: .*, 0 failures')
if [ "$CFRC" != "0" ]; then
  bad "G89 abort confirmation: the run itself failed (exit $CFRC)"
elif [ "$CFOPEN" = "3" ] && [ "$CFCANCEL" = "1" ] && [ "$CFABORT" = "1" ] \
     && [ "$CFRESTART" = "1" ] && [ "$CFCLEAN" = "1" ]; then
  ok "G89 abort confirmation: the Abort button raises the box instead of ending the mission ($CFOPEN times), Cancel goes back to the pause page, Abort still ends it (act=2) and Restart returns its own action (act=6) rather than abort's"
else
  bad "G89 abort confirmation: box-opened=$CFOPEN(want 3) cancel-returns=$CFCANCEL(want 1) abort=$CFABORT(want 1) restart=$CFRESTART(want 1) script-clean=$CFCLEAN(want 1)"
fi

# G90 A DESTROYED BUILDING SHEDS ITS SECTIONS INSTEAD OF STANDING INTACT.
#
# Two runs of the same script, once plainly and once with --nodeathshed, differenced on
# the LAST frame of the eight-tick death window the engine's own CountDown = 8 gives a
# killed structure (building.cpp:1562). The A/B is the assertion: during an explosion the
# picture changes constantly, so "it changed" proves nothing, and only the shed can account
# for a difference between two runs that are otherwise identical and deterministic.
#
# The anti-vacuity leg is the DEATHSHED|begin line. Without it a script that failed to kill
# anything would produce two identical runs, zero difference, and a confident green.
rm -f shots/g90_*.png shots/g90off_*.png
DSLOG=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud \
            --script gate_deathshed.txt 2>&1)
DSRC=$?
echo "$DSLOG" >> "$OUT"
DSDIED=$(echo "$DSLOG" | grep -c '^DEATHSHED|GTWR|.*|begin|frame=')
for n in 0 1 2 3 4 5 6 7; do cp -f shots/g90_$n.png shots/g90on_$n.png 2>/dev/null; done
DSOFFLOG=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud --nodeathshed \
               --script gate_deathshed.txt 2>&1)
DSRC2=$?
echo "$DSOFFLOG" >> "$OUT"
DSDIED2=$(echo "$DSOFFLOG" | grep -c '^DEATHSHED|GTWR|.*|begin|frame=')
DSPIX=$(python3 - <<'PY3'
from PIL import Image
import numpy as np
try:
    a = np.asarray(Image.open('shots/g90on_7.png').convert('RGB'), int)
    b = np.asarray(Image.open('shots/g90_7.png').convert('RGB'), int)
    print(int((np.abs(a - b).sum(2) > 0).sum()))
except Exception:
    print(-1)
PY3
)
if [ "$DSRC" != "0" ] || [ "$DSRC2" != "0" ]; then
  bad "G90 death shed: a run failed (exit $DSRC / $DSRC2)"
elif [ "${DSDIED:-0}" -ge 1 ] && [ "${DSDIED2:-0}" -ge 1 ] && [ "${DSPIX:--1}" -ge 200 ]; then
  ok "G90 death shed: a Guard Tower really died in both runs ($DSDIED / $DSDIED2 DEATHSHED begins), and on the last tick of its eight-tick window the shed accounts for $DSPIX changed pixels against --nodeathshed, where the roof is still a solid intact pyramid inside its own explosion"
else
  bad "G90 death shed: died-on=$DSDIED(want >=1) died-off=$DSDIED2(want >=1) shed-vs-noshed=$DSPIX px(want >=200). Zero deaths means the script did not kill anything and the pixel leg proves nothing."
fi

# ==================================================================================
# G91. THE BUILDING SHATTER. A killed structure comes apart into its own pieces, which
# fly, land on the terrain and settle.
#
# The subject is the player's own POWER PLANT, force-fired with a Medium Tank, because it
# has NINE pieces where the Guard Tower G90 kills has three -- enough to exercise the weld
# rule, the mass proxy and the flat-piece settle. CTRL-click is force fire and works on
# your own buildings, which is the only way a script can stage a death on demand.
#
# Every number below is read out of the renderer's own state, not out of pixels: the piece
# count comes from the PACK's section table, and the rest positions come from the physics.
rm -f /tmp/g91_*.png
SHLOG=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud \
            --script gate_shatter.txt 2>&1)
SHRC=$?
echo "$SHLOG" >> "$OUT"
# 1. THE PIECE COUNT, against the pack. Catches a broken weld rule, a shadow plate leaking
#    into the piece list, and a pack that lost its sections -- all in one line.
SHNUKE=$(echo "$SHLOG" | grep -c '^SHATTER|NUKE|id=[0-9]*|pieces=9|tris=132|')
# 2. THE ANTI-VACUITY LEG. No death, no gate.
SHDIED=$(echo "$SHLOG" | grep -c '^SHATTER|NUKE|')
# 3. EVERY SETTLED PIECE SITS ON THE TERRAIN, at ground + its own underside, within a
#    hundredth of a cell. This is the leg that fails if a piece sinks through the
#    heightfield or floats above it, and it is why the resting height is the piece's
#    UNDERSIDE and not its bounding sphere -- the sphere floated the widest panel 1.48
#    cells into the air.
SHGROUND=$(echo "$SHLOG" | python3 -c '
import sys, re
bad = 0; n = 0
for ln in sys.stdin:
    if not ln.startswith("SHATTER|rest|"): continue
    m = re.search(r"\|at=([-0-9.]+),([-0-9.]+),([-0-9.]+)\|ground=([-0-9.]+)\|low=([-0-9.]+)", ln)
    if not m: continue
    n += 1
    y = float(m.group(2)); g = float(m.group(4)); low = float(m.group(5))
    if abs(y - (g + low)) > 0.01: bad += 1
print("%d %d" % (n, bad))
')
SHRESTN=$(echo "$SHGROUND" | cut -d" " -f1)
SHRESTBAD=$(echo "$SHGROUND" | cut -d" " -f2)
# 4. THE PHYSICS, NOT THE BACKSTOP, IS WHAT SETTLES PIECES -- and at least one piece
#    actually ROLLED. These two together are the real assertion, and the first draft of
#    this gate got it wrong by demanding ZERO backstop stops. That is not what the design
#    promises: a piece on ground that runs downhill for a long way never satisfies any
#    rest threshold, because the slope feeds it as fast as friction drains it, and two of
#    the Power Plant's nine were still rolling after ten seconds when measured. The
#    backstop is supposed to catch those. What must NOT happen is the backstop carrying
#    the feature, which is exactly what a bad elasticity looks like: at the chunk pool's
#    own 0.7 every single piece bounced its whole window away, every one stopped on the
#    backstop, and rolled= was 0.000 across the board because the roll arm was reached
#    literally never. So: a clear majority settle by physics, and rolling happens.
SHTIMEOUT=$(echo "$SHLOG" | grep -c '^SHATTER|rest|.*|to=1$')
SHROLLED=$(echo "$SHLOG" | grep -c '^SHATTER|rest|.*|rolled=0\.[1-9]')
# 5. THE POOL EMPTIES. A piece that never rests never fades and never leaves.
SHLIVE=$(echo "$SHLOG" | grep -c '^SHATTERDUMP|live=0|')
if [ "$SHRC" != "0" ]; then
  bad "G91 building shatter: the run failed (exit $SHRC)"
elif [ "${SHDIED:-0}" -ge 1 ] && [ "${SHNUKE:-0}" -ge 1 ] && [ "${SHRESTN:-0}" -ge 9 ] \
     && [ "${SHRESTBAD:-1}" -eq 0 ] && [ "${SHTIMEOUT:-99}" -le 4 ] && [ "${SHROLLED:-0}" -ge 1 ] \
     && [ "${SHLIVE:-0}" -ge 1 ]; then
  ok "G91 building shatter: a Power Plant came apart into 9 pieces / 132 triangles off the pack's own section table, $SHRESTN pieces settled with only $SHTIMEOUT on the backstop, $SHROLLED of them rolled a visible distance first, every one rests on the terrain at ground+underside within 0.01 cells, and the pool emptied"
else
  bad "G91 building shatter: died=$SHDIED(want >=1) pieces9=$SHNUKE(want 1) rested=$SHRESTN(want >=9) off-ground=$SHRESTBAD(want 0) on-backstop=$SHTIMEOUT(want <=4; all of them means the elasticity regressed and nothing ever reaches the roll arm) rolled=$SHROLLED(want >=1) pool-emptied=$SHLIVE(want 1). Zero deaths means the script killed nothing and every other leg is vacuous."
fi

# ==================================================================================
# G92. A DESTROYED VEHICLE COMES APART, AND NOTHING ELSE DOES.
#
# See the long header in gate_unitshatter.txt for why the second half is the hard half.
# Short version: a building announces its death (str<=0 for eight ticks), a VEHICLE DOES
# NOT -- measured, a Jeep goes from str=12/150 straight to MISSING -- so the only
# observable is that it left the dump, and leaving the dump also means docked, boarded,
# flew off the map, or deployed. A strength test scores 100% false negatives on one-shot
# kills (two measured deaths at FULL health). The discriminator is the victim's own
# explosion anim, born that tick, within 0.75 cells, and each anim authorises ONE victim.
rm -f /tmp/g92_*.png
USLOG=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud \
            --script gate_unitshatter.txt 2>&1)
USRC=$?
echo "$USLOG" >> "$OUT"
# 1. ANTI-VACUITY. A tank really died and really shattered. Without this every leg below
#    passes on a run where the force-fire missed and nothing was ever killed.
USDIED=$(echo "$USLOG" | grep -c '^UNITDEATH|MTNK|.*|verdict=shatter$')
# 2. THE DECOMPOSITION AGAINST THE PACK. 91 triangles is the Medium Tank's whole mesh, so
#    this catches a piece silently dropped, a shadow section leaking in, a contiguity bug
#    in the component split, and a pack that lost its section table.
USPIECES=$(echo "$USLOG" | grep -c '^SHATTER|MTNK|id=[0-9]*|pieces=6|tris=91|')
# 3. THE TURRET FLEW WHOLE. This is the leg that fails if the role weld regresses to plain
#    sections, which would give five turret shards instead of one 41-triangle turret. It
#    is the shot the whole unit rule exists for.
USTURRET=$(echo "$USLOG" | grep -c '^SHATTER|piece|id=[0-9]*|k=5|sec=5\.\.5|tris=41|')
# 4. EVERY SETTLED PIECE SITS ON THE TERRAIN, same arithmetic as G91.
USGROUND=$(echo "$USLOG" | python3 -c '
import sys, re
bad = 0; n = 0
for ln in sys.stdin:
    if not ln.startswith("SHATTER|rest|"): continue
    m = re.search(r"\|at=([-0-9.]+),([-0-9.]+),([-0-9.]+)\|ground=([-0-9.]+)\|low=([-0-9.]+)", ln)
    if not m: continue
    n += 1
    if abs(float(m.group(2)) - (float(m.group(4)) + float(m.group(5)))) > 0.01: bad += 1
print("%d %d" % (n, bad))
')
USRESTN=$(echo "$USGROUND" | cut -d" " -f1)
USRESTBAD=$(echo "$USGROUND" | cut -d" " -f2)
# 5. THE POOL IS NOT STARVED. Note what this does NOT assert, and why: G91 asserts the
#    pool EMPTIES, and that assertion cannot be made here. SCG90EA has both sides in
#    contact, so vehicles keep dying for the whole run and the pool is legitimately
#    non-empty whenever the dump is taken -- the first version of this leg demanded
#    live=0 and went red on a build where everything worked. What is assertable is that
#    nothing was DROPPED: no piece was refused a slot or refused by the triangle ceiling.
#    The "pieces eventually leave" claim is G91's, on a scenario that can hold still.
USDROP=$(echo "$USLOG" | grep -c '^SHATTERDUMP|live=[0-9]*|tris=[0-9]*|dropped=0|')
#
# RUN B: THE GUARDS. A long ordinary game on SCG01EA -- the MCV deploys, harvesters run
# tiberium to the refinery for two thousand ticks, landing craft come and go.
UGLOG=$(./cnc_eyes --scen SCG01EA --pack SCG01EA.pack $BASE \
            --script gate_unitshatter_guard.txt 2>&1)
UGRC=$?
echo "$UGLOG" >> "$OUT"
# 6. THE MCV DEPLOY IS NOT A DEATH -- and it was CONSIDERED and REFUSED, not merely absent.
#    Asserting only "no MCV shattered" would pass on a build where the sweep never ran.
UGMCVNO=$(echo "$UGLOG" | grep -c '^UNITDEATH|MCV|.*|verdict=shatter$')
UGMCVSEEN=$(echo "$UGLOG" | grep -cE '^UNITDEATH\|MCV\|.*\|verdict=skip-(bldcell|nobang)$')
# 7. NO HARVESTER EVER SHATTERS. This is the regression that turns every tiberium delivery
#    in the game into an exploding harvester, and it is one line away at all times: a
#    docking harvester goes limbo=1 and STAYS IN THE DUMP, so the sweep is safe only
#    because refresh_objects pushes limboed objects into g_objects. Anyone who "tidies"
#    the sweep by skipping limboed objects fires this leg.
UGHARV=$(echo "$UGLOG" | grep -c '^UNITDEATH|HARV|.*|verdict=shatter$')
if [ "$USRC" != "0" ] || [ "$UGRC" != "0" ]; then
  bad "G92 vehicle shatter: a run failed (exit $USRC / $UGRC)"
elif [ "${USDIED:-0}" -ge 1 ] && [ "${USPIECES:-0}" -ge 1 ] && [ "${USTURRET:-0}" -ge 1 ] \
     && [ "${USRESTN:-0}" -ge 6 ] && [ "${USRESTBAD:-1}" -eq 0 ] && [ "${USDROP:-0}" -ge 1 ] \
     && [ "${UGMCVNO:-1}" -eq 0 ] && [ "${UGMCVSEEN:-0}" -ge 1 ] && [ "${UGHARV:-1}" -eq 0 ]; then
  ok "G92 vehicle shatter: a Medium Tank came apart into 6 pieces / 91 triangles with its 41-triangle turret whole, $USRESTN pieces rest on the terrain at ground+underside within 0.01 cells and no piece was ever refused a slot; and over an ordinary game the MCV deploy was considered and REFUSED ($UGMCVSEEN skip verdict) while no harvester ever shattered"
else
  bad "G92 vehicle shatter: died=$USDIED(want >=1; zero means the script killed nothing and every leg is vacuous) pieces6tris91=$USPIECES(want >=1) turret41=$USTURRET(want >=1; zero means the role weld regressed to plain sections) rested=$USRESTN(want >=6) off-ground=$USRESTBAD(want 0) nothing-dropped=$USDROP(want >=1) mcv-shattered=$UGMCVNO(want 0) mcv-refused=$UGMCVSEEN(want >=1; zero means the sweep never even saw it, so the guard is vacuous) harv-shattered=$UGHARV(want 0; nonzero means somebody added a limbo filter to the sweep MEMBERSHIP and every tiberium delivery now explodes)"
fi

# ==================================================================================
# G93. THE CARTRIDGE'S DAMAGED-BUILDING ART.
#
# See gate_damage.txt for the decode. Short version: Health_Ratio() < 0x80 sets draw-flag
# bit 3 and the model handler APPENDS an overlay display list over the intact building.
# Four of the five legs are read out of the renderer's own state, not out of pixels.
rm -f shots/g93_*.png shots/g93off_*.png
DMLOG=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud \
            --script gate_damage.txt 2>&1)
DMRC=$?
echo "$DMLOG" >> "$OUT"
for n in 0 1 2 3 4 5 6; do cp -f shots/g93_$n.png shots/g93on_$n.png 2>/dev/null; done
DMOFFLOG=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud --nodamageart \
               --script gate_damage.txt 2>&1)
DMRC2=$?
echo "$DMOFFLOG" >> "$OUT"
# 1. ANTI-VACUITY. Buildings were observed at all, in both runs.
DMSEEN=$(echo "$DMLOG" | grep -c '^DMGART|NUKE|')
# 2. THE PACK. Read out of the PACK, exactly as G91's piece count is. This is the leg that
#    catches a binary running against packs that were never re-baked -- a failure that
#    otherwise looks identical to "the feature does not work". 21 codes and not 22 because
#    the OBELISK has a damage record with NO display list (it gets damage smoke and no
#    art, the cartridge's own choice), so there is no OBLIDMG mesh to point a code at.
DMPACK=$(echo "$DMLOG" | grep -c '^DMGPACK|codes=21|meshes=20|tris=345$')
# 3. THE RULE ITSELF, ON EVERY SAMPLE, not at one hand-picked tick. drawn must equal
#    (ratio < 0x80 AND a mesh resolved). This is what catches <= instead of <, an inverted
#    test, a stale per-type cache, and an overlay that draws unconditionally -- none of
#    which a screenshot would show.
DMRULE=$(echo "$DMLOG" | python3 -c '
import sys, re
bad = tot = on = off = 0
for ln in sys.stdin:
    if not ln.startswith("DMGART|"): continue
    m = re.search(r"\|ratio=0x([0-9A-F]+)\|mesh=(-?\d+)\|drawn=(\d)", ln)
    if not m: continue
    tot += 1
    want = 1 if (int(m.group(1), 16) < 0x80 and int(m.group(2)) >= 0) else 0
    got  = int(m.group(3))
    if want != got: bad += 1
    on += got; off += (1 - got)
print("%d %d %d %d" % (tot, bad, on, off))
')
# 7. AND THE SUPPRESSION IS REPORTED. `drawn` is the cartridge's rule; `sup` is why we
#    declined to draw it anyway. Assert every sample carries a sup= field and that its
#    value is one of the four known ones -- a typo or a new arm that forgets to name itself
#    would otherwise be invisible, and this is the only leg that can ever see the
#    dying-building and shatter-owned suppressions at all.
DMSUP=$(echo "$DMLOG" | grep -c '^DMGART|.*|sup=\(none\|dead\|construct\|shatter\)|str=')
DMTOT=$(echo "$DMRULE" | cut -d" " -f1)
DMBAD=$(echo "$DMRULE" | cut -d" " -f2)
DMON=$(echo "$DMRULE" | cut -d" " -f3)
DMOFF=$(echo "$DMRULE" | cut -d" " -f4)
# 4/5. THE A/B, BOTH DIRECTIONS. While damaged the picture must CHANGE against
#      --nodamageart; while healthy it must be pixel-IDENTICAL. The second half is only
#      meaningful because two runs of one script are deterministic (G4), so one differing
#      pixel is a real finding. 40 rather than 200 because the smallest damage list is 2
#      triangles.
# EXPORTED, not just assigned. "DMLOGF=x DMPIX=$(...)" is TWO ASSIGNMENTS, not an
# environment prefix on a command, so the variable never reaches the python subprocess and
# every sample reads as "no log" -- which showed up as first-damaged-sample=-1 while the
# rule leg was green on all 102 observations.
echo "$DMLOG" > /tmp/g93_on.log
export DMLOGF=/tmp/g93_on.log
DMPIX=$(python3 - <<'PY3'
from PIL import Image
import numpy as np, re, sys, os
def diff(a, b):
    try:
        x = np.asarray(Image.open(a).convert('RGB'), int)
        y = np.asarray(Image.open(b).convert('RGB'), int)
        return int((np.abs(x - y).sum(2) > 0).sum())
    except Exception:
        return -1
log = open(os.environ.get('DMLOGF', '/dev/null')).read()
# which samples were damaged, in order
drawn = [int(m) for m in re.findall(r'^DMGART\|NUKE\|id=1\|.*\|drawn=(\d)$', log, re.M)]
firstOn = next((k for k, d in enumerate(drawn) if d), -1)
worst = 0
for k, d in enumerate(drawn):
    if d: continue
    n = diff('shots/g93on_%d.png' % k, 'shots/g93_%d.png' % k)
    if n > worst: worst = n
onpix = diff('shots/g93on_%d.png' % firstOn, 'shots/g93_%d.png' % firstOn) if firstOn >= 0 else -1
print("%d %d %d" % (firstOn, onpix, worst))
PY3
)
# 6. THE COLOUR OF WHAT IT DREW. This is the leg that would have caught the bug the first
#    version of this gate shipped green: nine of the twenty overlays inherit G_LIGHTING, so
#    their vertex slot holds a packed unit NORMAL rather than a colour, and an unwhitened
#    bake makes the renderer modulate the damage texture by a direction vector. The result
#    is not subtly wrong, it is saturated magenta and green -- measured at 70% green / 23%
#    magenta on a damaged Construction Yard. Damage art is scorch, rust, fire and torn
#    metal, so the pixels it adds are overwhelmingly WARM. A green-or-magenta majority
#    means the whitener missed a mesh, whatever the pixel COUNT says.
DMHUE=$(python3 - <<'PY3'
from PIL import Image
import numpy as np
def hue_split(a, b):
    try:
        x = np.asarray(Image.open(a).convert('RGB'), int)
        y = np.asarray(Image.open(b).convert('RGB'), int)
    except Exception:
        return -1, -1
    m = (np.abs(x - y).sum(2) > 30)
    if m.sum() < 50:
        return -1, -1
    px = y[m].astype(int)
    r, g, bl = px[:, 0], px[:, 1], px[:, 2]
    mx = px.max(1); mn = px.min(1)
    sat = (mx - mn) > 40
    if sat.sum() < 20:
        return 0, 0
    warm = ((r >= g) & (r >= bl))[sat].sum()
    bad  = (((g > r) & (g > bl)) | ((bl > g) & (r > g)))[sat].sum()
    return int(100 * warm / sat.sum()), int(100 * bad / sat.sum())
w6, b6 = hue_split('shots/g93on_6.png', 'shots/g93_6.png')
print("%d %d" % (w6, b6))
PY3
)
DMWARM=$(echo "$DMHUE" | cut -d" " -f1)
DMBADHUE=$(echo "$DMHUE" | cut -d" " -f2)
DMFIRST=$(echo "$DMPIX" | cut -d" " -f1)
DMONPIX=$(echo "$DMPIX" | cut -d" " -f2)
DMHEALTHYPIX=$(echo "$DMPIX" | cut -d" " -f3)
if [ "$DMRC" != "0" ] || [ "$DMRC2" != "0" ]; then
  bad "G93 damaged-building art: a run failed (exit $DMRC / $DMRC2)"
elif [ "${DMSEEN:-0}" -ge 1 ] && [ "${DMPACK:-0}" -ge 1 ] && [ "${DMTOT:-0}" -ge 6 ] \
     && [ "${DMBAD:-1}" -eq 0 ] && [ "${DMON:-0}" -ge 1 ] && [ "${DMOFF:-0}" -ge 1 ] \
     && [ "${DMONPIX:--1}" -ge 40 ] && [ "${DMHEALTHYPIX:--1}" -eq 0 ] \
     && [ "${DMWARM:--1}" -ge 50 ] && [ "${DMBADHUE:-100}" -le 25 ] \
     && [ "${DMSUP:-0}" -eq "${DMTOT:--1}" ]; then
  ok "G93 damaged-building art: the pack carries 21 codes / 20 meshes / 345 triangles, the cartridge's own rule (ratio < 0x80) held on all $DMTOT observations with $DMON damaged and $DMOFF healthy, the overlay put $DMONPIX pixels on screen the first sample it was due, and every healthy sample is pixel-identical to --nodamageart; a second structure (the Construction Yard) drew ${DMWARM}% warm saturated pixels against ${DMBADHUE}% green-or-magenta, so no overlay is being modulated by a packed normal"
else
  bad "G93 damaged-building art: seen=$DMSEEN(want >=1; zero means nothing was observed and every leg is vacuous) pack=$DMPACK(want 1; zero means these packs were never re-baked with bake5.py) samples=$DMTOT(want >=6) rule-violations=$DMBAD(want 0; nonzero means drawn disagrees with ratio<0x80, the <= off-by-one) damaged=$DMON(want >=1) healthy=$DMOFF(want >=1) first-damaged-sample=$DMFIRST changed-px-when-damaged=$DMONPIX(want >=40; zero means it resolved a mesh and drew nothing) sup-fields=$DMSUP(want == samples; a shortfall means a DMGART line carries no sup= or an unknown one) warm-pct=$DMWARM(want >=50) green-or-magenta-pct=$DMBADHUE(want <=25; a high value means an overlay inherits G_LIGHTING and the bake did not whiten it, so the texture is modulated by a direction vector and draws rainbow) changed-px-when-healthy=$DMHEALTHYPIX(want 0; nonzero means the overlay leaks onto full-health buildings or --nodamageart does not reach the draw)"
fi

# ==================================================================================
# G94. THE SIDEBAR TOOLTIP. See gate_tooltip.txt for why this is a restoration rather
# than a deviation, and why it is asserted on strings rather than pixels.
rm -f shots/g94_*.png
TTLOG=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud \
            --script gate_tooltip.txt 2>&1)
TTRC=$?
echo "$TTLOG" >> "$OUT"
# The SAME script against the 640 HUD, because the two HUDs have different geometry, row
# counts and scroll state, and gates.sh already runs both elsewhere. A tooltip that works
# on one and not the other is the likely failure, not a total absence.
TTLOG6=$(CNC3D_HUD=new ./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud \
            --script gate_tooltip.txt 2>&1)
TTRC6=$?
echo "$TTLOG6" >> "$OUT"
# 1. a cameo gives BOTH strings, and they are the game's own words and the game's own price
TTNAME=$(echo "$TTLOG" | grep -c '^SBTIP|hover=1|.*|name=Med\. Tank|cost=800$')
# 2. a chrome button gives a name and NO price. cost 0 is not a free unit, it is a
#    SPECIAL (Fetch_Techno_Type yields NULL for RTTI_SPECIAL), and 1995 omits the line
#    rather than printing $0.
TTCHROME=$(echo "$TTLOG" | grep -c '^SBTIP|hover=[0-9]*|col=-1|slot=-1|name=Sell Structure|cost=-1$')
# 3. off the panel, nothing. This is the leg that fails if the hover is never cleared.
TTNONE=$(echo "$TTLOG" | grep -c '^SBTIP|hover=0|col=-1|slot=-1|name=-|cost=-1$')
# 4. THE WHOLE TABLE, not one lucky lookup. 118 rows, and -- the real assertion -- not one
#    of them fell back to its own ident. "MTNK" as a title would mean the bake's join
#    silently failed for that row and nothing else would show it.
TTCOUNT=$(echo "$TTLOG" | grep -c '^SBTIPNAMES|count=118$')
TTIDENT=$(echo "$TTLOG" | python3 -c '
import sys
# THREE NAMES ARE LEGITIMATELY THEIR OWN IDENT and this list is why the leg is worth
# having rather than being relaxed away. The game itself calls these three by their
# acronym -- verified against CONQUER.ENG inside LOCAL.MIX: TXT_A10 index 96 is "A10",
# TXT_C17 index 97 is "C17", TXT_APC index 110 is "APC". The first version of this leg
# flagged them as failed joins, which was the ASSERTION being wrong rather than the data.
# Naming them rather than raising a threshold keeps the leg sharp: a FOURTH ident-equals-
# title row still fails, and so does one of these three going missing.
OK = {"A10", "C17", "APC"}
bad, seen = 0, set()
for ln in sys.stdin:
    if not ln.startswith("SBTIPNAME|"): continue
    p = ln.rstrip("\n").split("|")
    if len(p) < 3 or p[1] != p[2]: continue
    if p[1] in OK: seen.add(p[1])
    else: bad += 1
# a missing acronym counts as a failure too, so the table cannot quietly lose one
print(bad + len(OK - seen))
')
# 5. the 640 HUD gets it too, at ITS geometry
TT6=$(echo "$TTLOG6" | grep -c '^SBTIP|hover=1|.*|name=Med\. Tank|cost=800$')
if [ "$TTRC" != "0" ] || [ "$TTRC6" != "0" ]; then
  bad "G94 sidebar tooltip: a run failed (exit $TTRC / $TTRC6)"
elif [ "${TTNAME:-0}" -ge 1 ] && [ "${TTCHROME:-0}" -ge 1 ] && [ "${TTNONE:-0}" -ge 1 ] \
     && [ "${TTCOUNT:-0}" -ge 1 ] && [ "${TTIDENT:-1}" -eq 0 ] && [ "${TT6:-0}" -ge 1 ]; then
  ok "G94 sidebar tooltip: a cameo reads 'Med. Tank' and '\$800' from the game's own CONQUER.ENG and its own export, a chrome button reads 'Sell Structure' with no price line, off the panel reads nothing, all 118 baked names resolved with none falling back to its ident except the three the game itself calls A10, C17 and APC, and the 640 HUD gets the same at its own geometry"
else
  bad "G94 sidebar tooltip: cameo=$TTNAME(want >=1) chrome=$TTCHROME(want >=1) offpanel=$TTNONE(want >=1; zero means the hover is never cleared) table=$TTCOUNT(want 1) idents-not-joined=$TTIDENT(want 0; counts rows whose title is its own ident EXCLUDING the three the game really does call A10/C17/APC, plus any of those three that went missing) hud640=$TT6(want >=1; zero means it works on the DOS bar only)"
fi

# ==================================================================================
# G95. BUILD QUEUEING. See gate_queue.txt for why the count lives in the eyes and why
# CREDITS rather than a unit count is the assertion.
QLOG=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud \
           --script gate_queue.txt 2>&1)
QRC=$?
echo "$QLOG" >> "$OUT"
QOFF=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud --noqueue \
           --script gate_queue.txt 2>&1)
QRC2=$?
echo "$QOFF" >> "$OUT"
# 1. the queue took the clicks
QADD=$(echo "$QLOG" | grep -c '^SBQUEUE|add|E1|btype=2|bid=0|n=2$')
# 2. the pump fired TWICE and emptied. This is the leg that fails if the pump never runs,
#    runs more than once per frame, or loops.
QPUMP=$(echo "$QLOG" | grep -c '^SBQUEUE|pump|E1|')
QEMPTY=$(echo "$QLOG" | grep -c '^SBQUEUEDUMP|entries=0$')
# 3. THE MONEY. Three Minigunners at 100 apiece is exactly 300, and credits cannot be
#    killed by a Nod buggy the way the units themselves can.
QSPENT=$(echo "$QLOG" | python3 -c '
import sys, re
v = [int(m) for m in re.findall(r"^CHEATDUMP\|.*\|credits=(\d+)$", sys.stdin.read(), re.M)]
print((v[0] - v[-1]) if len(v) >= 2 else -1)
')
# 4. THE A/B: with the feature off, no pump at all.
QOFFPUMP=$(echo "$QOFF" | grep -c '^SBQUEUE|pump|')
if [ "$QRC" != "0" ] || [ "$QRC2" != "0" ]; then
  bad "G95 build queue: a run failed (exit $QRC / $QRC2)"
elif [ "${QADD:-0}" -ge 1 ] && [ "${QPUMP:-0}" -eq 2 ] && [ "${QEMPTY:-0}" -ge 1 ] \
     && [ "${QSPENT:-0}" -eq 300 ] && [ "${QOFFPUMP:-1}" -eq 0 ]; then
  ok "G95 build queue: two extra clicks queued two more Minigunners, the pump fired exactly twice as the barracks freed itself, the queue emptied, and the bank is 300 credits lighter -- exactly three at 100 apiece; with --noqueue the pump never fires"
else
  bad "G95 build queue: queued=$QADD(want >=1) pumps=$QPUMP(want exactly 2; 0 means the pump never ran, >2 means it is looping and will announce Building fifteen times a second) emptied=$QEMPTY(want >=1) credits-spent=$QSPENT(want exactly 300 = 3 x 100; a smaller number means the queued items were never actually built) pumps-with-noqueue=$QOFFPUMP(want 0)"
fi

# ==================================================================================
# G96. RALLY POINTS. See gate_rally.txt for the inertness argument and for the FOURTH
# gate (Can_Player_Move) that the first attempt missed.
RLOG=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud \
           --script gate_rally.txt 2>&1)
RRC=$?
echo "$RLOG" >> "$OUT"
# the SAME script with no rally set: the control arm of the A/B
cat > /tmp/g96_ctl.txt <<'CTL'
tick 30
cam 57 52
zoom max
count E1
build E1
tick 140
obj E1 10
CTL
RCTL=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud \
           --script /tmp/g96_ctl.txt 2>&1)
RRC2=$?
echo "$RCTL" >> "$OUT"
# 1. THE LEFT CLICK set it, and the brain stored it. Left and not right: the request was for
#    right first, tried it, and changed his mind on 25 Aug. The gesture needs no rung of
#    its own -- once BuildingClass::What_Action answers ACTION_MOVE for a factory the
#    ordinary order path carries it.
RSET=$(echo "$RLOG" | grep -c '^ORDER|.*|cell=57,55|.*|MOVE$')
RGOT=$(echo "$RLOG" | grep -c '^RALLY|PYLE|id=[0-9]*|cell=57,55$')
# 2. AND RIGHT-CLICK DOES NOT. It deselects, which is its one meaning in this build, and
#    the rally set on the left button SURVIVES it unchanged. This leg is the regression
#    guard for the gesture asked to be taken off the right button: if anyone puts
#    it back, the selection stops clearing and this goes red.
RDESEL=$(echo "$RLOG" | grep -c '^EXPECTSEL|want=0|have=0|PASS$')
RNORIGHT=$(echo "$RLOG" | grep -c '^RALLY|rightclick|')
RSURVIVE=$(echo "$RLOG" | grep -c '^RALLY|PYLE|id=[0-9]*|cell=57,55$')
# 3. THE PRODUCED UNIT WENT THERE -- and the control proves it went somewhere ELSE without
#    a rally point. Asserting only that the rally was stored would pass on a build where
#    the unit ignored it.
RWENT=$(echo "$RLOG" | grep -c '^OBJ|E1#10|INFANTRY|GoodGuy|cell=57,55|')
RCTLCELL=$(echo "$RCTL" | sed -n 's/^OBJ|E1#10|INFANTRY|GoodGuy|cell=\([0-9,]*\)|.*/\1/p' | tail -1)
if [ "$RRC" != "0" ] || [ "$RRC2" != "0" ]; then
  bad "G96 rally points: a run failed (exit $RRC / $RRC2)"
elif [ "${RSET:-0}" -ge 1 ] && [ "${RGOT:-0}" -ge 1 ] && [ "${RDESEL:-0}" -ge 1 ] \
     && [ "${RNORIGHT:-1}" -eq 0 ] && [ "${RSURVIVE:-0}" -ge 2 ] \
     && [ "${RWENT:-0}" -ge 1 ] && [ "${RCTLCELL:-57,55}" != "57,55" ]; then
  ok "G96 rally points: a LEFT click on the ground set the barracks' rally to 57,55 and the brain stored it, a right click still deselects and leaves the rally untouched, and the Minigunner it built walked to 57,55 -- where the same build with no rally point ends at $RCTLCELL instead"
else
  bad "G96 rally points: leftclick-order=$RSET(want >=1; zero means What_Action stopped answering MOVE for a factory and all four brain gates need checking together) brain-stored=$RGOT(want >=1) right-deselects=$RDESEL(want >=1; zero means right-click stopped cancelling, which is what putting the rally back on it would do) rally-on-right=$RNORIGHT(want 0; nonzero means the right-button rung came back) rally-survived-right-click=$RSURVIVE(want >=2) unit-at-rally=$RWENT(want >=1; zero means the rally was stored and the produced unit ignored it, the four-gates-must-move-together failure) control-cell=$RCTLCELL(want anything BUT 57,55)"
fi

# G97. THE NUCLEAR STRIKE'S MUSHROOM. See gate_nuke.txt for why the script ticks 356
# times before anything happens and why the assertion is EFXNUKE rather than a pixel count.
NKB="--scen SCB70EA --pack SCB01EA.pack $BASE --noshroud --nosound --w 1280 --h 800"
NKLOG=$(./cnc_eyes $NKB --script gate_nuke.txt 2>&1)
NKRC=$?
echo "$NKLOG" | grep -E '^(EFXNUKE|EFXDUMP\|ANIM\|ATOMSFX)' >> "$OUT"
# A SECOND IDENTICAL RUN, because this whole clip is driven off the engine's own stage
# counter and the claim that it carries no wallclock is worth testing rather than
# asserting. Two runs must produce the same EFXNUKE sequence byte for byte.
NKLOG2=$(./cnc_eyes $NKB --script gate_nuke.txt 2>&1)
NKRC2=$?
# 1. The engine really made ANIM_ATOM_BLAST, and it is type 60 -- our brain's own
#    AnimType ordinal (defines.h:1486), NOT the cartridge's 53. The two enums are
#    different and efx_recipes.h is the remap; a gate that asserted 53 here would be
#    testing the wrong one.
NKANIM=$(echo "$NKLOG" | grep -c '^EFXDUMP|ANIM|ATOMSFX|id=[0-9]*|type=60|')
# 2. THE MESH REACHED draw_mesh. Emitted from inside the draw, so nothing else can fake
#    it: zero here means the anim fired and the mushroom was never drawn, which is the
#    exact state this gate exists to prevent returning to.
NKDRAW=$(echo "$NKLOG" | grep -c '^EFXNUKE|id=[0-9]*|mesh=[0-9]*|')
# 3. IT GREW. The clip frame must ADVANCE across the strike -- a mushroom stuck on frame 0
#    would still emit one line and still change pixels. First and last t, converted to
#    integer thousandths by awk so this needs no float arithmetic in sh.
#
#    THE PARSE USED TO DROP THE ONE FRAME THAT MATTERS. It was
#      sed -n 's/^EFXNUKE|.*|t=0*\.\([0-9]*\)$/\1/p'
#    which requires the value to start "0.", so the FULL-GROWTH frame -- t=1.000, printed
#    the moment stage reaches biggest, which every completed strike reaches -- matched
#    nothing and vanished. With the current script's timing the window happens to close at
#    t=0.684 and the gate passes, so this was invisible; shift the capture a few ticks
#    later and every captured line reads 1.000, both variables come back EMPTY, the
#    defaults make the test 0 -gt 0, and a perfectly working build goes red. Caught by
#    feeding the real printf output back through the old expression rather than by reading
#    it. Now anchored on the whole value, so 0.053, 0.684 and 1.000 all parse.
NKT0=$(echo "$NKLOG" | sed -n 's/^EFXNUKE|.*|frame=\(.*\)$/\1/p' | head -1 \
       | awk '{printf "%d", $1 * 1000 + 0.5}')
NKT1=$(echo "$NKLOG" | sed -n 's/^EFXNUKE|.*|frame=\(.*\)$/\1/p' | tail -1 \
       | awk '{printf "%d", $1 * 1000 + 0.5}')
# THE CLOCK ITSELF, not just that the clip advanced. The cartridge's kind-12 nuke arm
# multiplies the engine stage by the float at RAM 0x800049B4 -- read back from the ROM as
# 1.1111112 (10/9) at ROM 0x0055B4, mul.s at ROM 0x04EDE8. This build first shipped with
# stage/biggest * (animFrames-1) instead, which is stage * 30/19 = 1.5789 and ran the
# whole mushroom 42% FAST. Both clocks pass a "the frame went up" test and both overshoot
# frame 30 at high stages, so neither growth nor a final-frame floor can tell them apart.
# The RATIO can: it is 1111 per 1000 for the cartridge and 1579 for the renormalisation.
# Measured off every line rather than one, so a single anomalous sample cannot carry it.
NKRATIO=$(echo "$NKLOG" \
    | sed -n 's/^EFXNUKE|id=[0-9]*|mesh=[0-9]*|stage=\([0-9]*\)\/[0-9]*|frame=\(.*\)$/\1 \2/p' \
    | awk '$1 > 0 { n++; s += $2 / $1 } END { if (n) printf "%d", s / n * 1000 + 0.5 }')
# 4. The mesh id is a real pack mesh. -1 is the "pack from before the nuke bake" path,
#    which draws nothing and must not read as a pass.
NKMESH=$(echo "$NKLOG" | sed -n 's/^EFXNUKE|id=[0-9]*|mesh=\([0-9-]*\)|.*/\1/p' | head -1)
# 5. Determinism.
NKSEQ1=$(echo "$NKLOG"  | grep '^EFXNUKE|' | shasum | cut -c1-12)
NKSEQ2=$(echo "$NKLOG2" | grep '^EFXNUKE|' | shasum | cut -c1-12)
if [ "$NKRC" != "0" ] || [ "$NKRC2" != "0" ]; then
  bad "G97 nuclear strike: a run failed (exit $NKRC / $NKRC2)"
elif [ "${NKANIM:-0}" -lt 1 ]; then
  bad "G97 nuclear strike: the engine never created ANIM_ATOM_BLAST (type=60 sightings=$NKANIM, want >=1). Nothing below means anything: either the superweapon cheat stopped granting SW_Nuke, or SCB70EA stopped having a player-owned Temple, or Place_Special_Blast took its no-launch-site return-false path"
elif [ "${NKDRAW:-0}" -ge 2 ] && [ "${NKMESH:--1}" -ge 0 ] \
     && [ "${NKT1:-0}" -gt "${NKT0:-0}" ] \
     && [ "${NKRATIO:-0}" -ge 1100 ] && [ "${NKRATIO:-0}" -le 1122 ] \
     && [ "$NKSEQ1" = "$NKSEQ2" ]; then
  ok "G97 nuclear strike: the blast created ANIM_ATOM_BLAST type=60 ($NKANIM sightings) and NUKEFX (pack mesh $NKMESH) reached draw_mesh on $NKDRAW distinct clip frames, growing from frame $NKT0/1000 to $NKT1/1000 on the cartridge's own clock (measured frame/stage = $NKRATIO/1000 against the ROM's 1111), identically across two runs (EFXNUKE digest $NKSEQ1)"
else
  bad "G97 nuclear strike: draws=$NKDRAW(want >=2; zero means the anim fired and the mesh never drew -- the pre-mushroom state) mesh=$NKMESH(want >=0; -1 is a pack with no NUKEFX, so re-bake) t0=${NKT0:-?}/1000 t1=${NKT1:-?}/1000(want t1 > t0; equal means the clip is stuck on one frame) frame-per-stage=${NKRATIO:-?}/1000(want 1100..1122, the ROM's 1.1111112 at RAM 0x800049B4; 1579 is the stage/biggest renormalisation that ran the mushroom 42% fast and that this gate used to be blind to) digest=$NKSEQ1 vs $NKSEQ2(want equal; a difference means something in the mushroom reads a clock that is not the engine's)"
fi

# G98. THE ION CANNON'S GEOMETRY. See gate_ion.txt for the stage doubling and for why
# the assertion is EFXION rather than a pixel count.
IOLOG=$(./cnc_eyes $IONB --script gate_ion.txt 2>&1)
IORC=$?
echo "$IOLOG" | grep -E '^(EFXION|EFXDUMP\|ANIM\|IONSFX)' >> "$OUT"
IOLOG2=$(./cnc_eyes $IONB --script gate_ion.txt 2>&1)
IORC2=$?
# 1. the snapshots drew at all, and more than one of them
IODRAW=$(echo "$IOLOG" | grep -c '^EFXION|id=[0-9]*|mesh=[0-9]*|')
# 2. THE STRIKE ADVANCED. First and last snapshot index: a beam frozen on ION0 would still
#    emit a line and still paint pixels.
IOS0=$(echo "$IOLOG" | sed -n 's/^EFXION|.*|snap=\([0-9]*\)$/\1/p' | head -1)
IOS1=$(echo "$IOLOG" | sed -n 's/^EFXION|.*|snap=\([0-9]*\)$/\1/p' | tail -1)
# 3. every mesh id is real; -1 is a pack baked before the ion snapshots existed.
#    THIS ARM USED TO BE UNARMABLE: the draw loop did `if (mi < 0) continue;` BEFORE the
#    printf, so mesh=-1 could never be printed and this count was structurally always 0
#    while its failure text advertised cover it did not provide. The report now happens
#    first, so a pack missing a snapshot really does say so.
IOBAD=$(echo "$IOLOG" | grep -c '^EFXION|id=[0-9]*|mesh=-1|')
# 4. determinism, for the same reason G97 checks it: the index comes from the engine's
#    stage and nothing here may read a clock of its own.
IOSEQ1=$(echo "$IOLOG"  | grep '^EFXION|' | shasum | cut -c1-12)
IOSEQ2=$(echo "$IOLOG2" | grep '^EFXION|' | shasum | cut -c1-12)
if [ "$IORC" != "0" ] || [ "$IORC2" != "0" ]; then
  bad "G98 ion cannon geometry: a run failed (exit $IORC / $IORC2)"
elif [ "${IODRAW:-0}" -ge 2 ] && [ "${IOBAD:-1}" -eq 0 ] \
     && [ "${IOS1:-0}" -gt "${IOS0:-0}" ] && [ "$IOSEQ1" = "$IOSEQ2" ]; then
  ok "G98 ion cannon geometry: the beam and its shock rings drew on $IODRAW distinct snapshots, advancing ION$IOS0 -> ION$IOS1 off the engine's own doubled stage, identically across two runs (EFXION digest $IOSEQ1)"
else
  bad "G98 ion cannon geometry: draws=$IODRAW(want >=2; zero means the anim fired and no ION snapshot reached draw_mesh, the pre-beam state G85 used to enshrine) missing-meshes=$IOBAD(want 0; nonzero means the pack predates the ION bake and must be re-baked) snap0=${IOS0:-?} snap1=${IOS1:-?}(want snap1 > snap0; equal means the strike is stuck on one snapshot) digest=$IOSEQ1 vs $IOSEQ2(want equal)"
fi

# =====================================================================================
# G99..G102. THE MISSION EDITOR.
#
# The editor grew a whole scripting layer and none of it was in this suite, which meant a
# change to the renderer could break the editor and the suite would stay green. These
# four call the editor's own gate scripts, which live in tools/ because they need the
# repo layout rather than the run folder.
#
# G99  the panel's layout, at eight window sizes and two backing scales
# G100 every mission's script survives a save, and the mission check's totals
# G101 Enhanced scripting end to end, in a real running mission
# G102 the whole authoring loop: New Map, place, save, play
# =====================================================================================
EDREPO="$(cd "$GATEDIR/.." && pwd)"

UIT=$(./cnc_eyes --uitest 2>&1 | grep 'UITEST|TOTAL' || true)
UIN=$(printf '%s' "$UIT" | sed -n 's/.*|\([0-9]*\) failures/\1/p')
if [ "${UIN:-1}" = "0" ]; then
  ok "G99 editor layout: no control collides with another at any tested window size or backing scale"
else
  bad "G99 editor layout: ${UIN:-no} failures from --uitest (a control the hit test answers for twice, or one that falls off the panel)"
fi

if [ -x "$EDREPO/tools/gate-scripttest.sh" ]; then
  SCG=$(sh "$EDREPO/tools/gate-scripttest.sh" 2>&1 | grep '^SCRIPTGATE' || true)
  SCP=$(printf '%s' "$SCG" | sed -n 's/SCRIPTGATE: \([0-9]*\) passed.*/\1/p' | head -1)
  SCF=$(printf '%s' "$SCG" | sed -n 's/SCRIPTGATE: [0-9]* passed, \([0-9]*\) failed.*/\1/p' | head -1)
  if [ "${SCF:-1}" = "0" ] && [ "${SCP:-0}" -ge 90 ]; then
    ok "G100 mission scripts: $SCP missions round-trip their triggers, teams and zones through a save unchanged ($(printf '%s' "$SCG" | tail -1 | sed 's/SCRIPTGATE: //'))"
  else
    bad "G100 mission scripts: ${SCP:-0} passed, ${SCF:-?} failed -- a mission's script does not survive being written back out"
  fi
else
  bad "G100 mission scripts: tools/gate-scripttest.sh is missing"
fi

if [ -x "$EDREPO/tools/gate-enhanced.sh" ]; then
  ENG=$(sh "$EDREPO/tools/gate-enhanced.sh" 2>&1 | grep '^ENHGATE' || true)
  if printf '%s' "$ENG" | grep -q 'ENHGATE: PASSED'; then
    ok "G101 Enhanced scripting: a two-clause condition fires at the right tick, the engine runs the carrier's action, and the editor's TRACE survives both reboots"
  else
    bad "G101 Enhanced scripting: $(printf '%s' "$ENG" | grep FAIL | head -3 | tr '\n' ' ')"
  fi
else
  bad "G101 Enhanced scripting: tools/gate-enhanced.sh is missing"
fi

if [ -x "$EDREPO/tools/gate-authoring.sh" ]; then
  AUG=$(sh "$EDREPO/tools/gate-authoring.sh" 2>&1 | grep '^AUTHGATE' || true)
  if printf '%s' "$AUG" | grep -q 'AUTHGATE: PASSED'; then
    ok "G102 authoring loop: a map made from nothing in this editor -- New Map, six objects, a rule -- saves and plays, and its rule fires"
  else
    bad "G102 authoring loop: $(printf '%s' "$AUG" | grep FAIL | head -3 | tr '\n' ' ')"
  fi
else
  bad "G102 authoring loop: tools/gate-authoring.sh is missing"
fi

# G103. THE RALLY POINT FLAG. See gate_rallyflag.txt for why the test counts GOLD pixels.
RFB="--scen SCG90EA --pack SCG01EA.pack $BASE --noshroud --nosound --w 1280 --h 800"
gbegin shots/g103_sel.png shots/g103_desel.png
grun /tmp/g99.log $RFB --script gate_rallyflag.txt
gshots shots/g103_sel.png shots/g103_desel.png
RFSET=$(grep -c '^RALLY|PYLE|id=[0-9]*|cell=57,54$' /tmp/g99.log)
RFPIX=$(python3 - <<'PY'
from PIL import Image
import numpy as np
def gold(p):
    a = np.asarray(Image.open(p).convert('RGB')).astype(int)
    r = a[500:600, 660:790]      # the patch of ground the flag stands on
    return int(((r[:,:,0] > 140) & (r[:,:,1] > 110) & (r[:,:,2] < 100)).sum())
s, d = gold('shots/g103_sel.png'), gold('shots/g103_desel.png')
print('G103|sel=%d|desel=%d|flag=%d' % (s, d, s - d))
PY
)
echo "$RFPIX" >> "$OUT"
RFSEL=$(echo "$RFPIX"  | sed -n 's/^G103|sel=\([0-9]*\).*/\1/p')
RFDES=$(echo "$RFPIX"  | sed -n 's/.*|desel=\([0-9]*\).*/\1/p')
RFDIFF=$(echo "$RFPIX" | sed -n 's/.*|flag=\(-*[0-9]*\)$/\1/p')
if [ "$GRC" != "0" ]; then
  bad "G103 rally flag: the run failed (exit $GRC)"
elif [ "${RFSET:-0}" -lt 1 ]; then
  bad "G103 rally flag: the rally point was never set (RALLY|PYLE|cell=57,54 seen $RFSET times, want >=1), so nothing below means anything"
elif [ "${RFDIFF:-0}" -ge 300 ] && [ "${RFDES:-99999}" -lt "${RFSEL:-0}" ]; then
  ok "G103 rally flag: with the Barracks selected and a rally point at 57,54 the flag stands there in the player's GOLD ($RFSEL gold pixels against $RFDES with nothing selected, so $RFDIFF of flag), and it goes away with the selection"
else
  bad "G103 rally flag: selected=$RFSEL deselected=$RFDES flag=$RFDIFF(want >=300). Zero or near-zero means either the flag is not being drawn at all, or it is drawn in the cartridge's own RED instead of the house colour -- the texture is a solid (132,16,8) and this test counts gold, so a red flag scores nothing. A NEGATIVE number means it is drawn when nothing is selected, which is the other half of what was asked for"
fi

# G104. A NEW MISSION STARTS FRESH. See gate_missionreset.txt for the two defects.
MRLOG=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud --nosound \
            --script gate_missionreset.txt 2>&1)
MRRC=$?
echo "$MRLOG" | grep -E '^(SHATTERDUMP|ECHO\|===)' >> "$OUT"
# the kill really happened, or the two dumps below are trivially equal
MRKILL=$(echo "$MRLOG" | sed -n 's/^SHATTERDUMP|live=\([0-9]*\).*/\1/p' | head -1)
# and nothing survived into the next mission
MRAFTER=$(echo "$MRLOG" | sed -n 's/^SHATTERDUMP|live=\([0-9]*\).*/\1/p' | sed -n '2p')
# THE CLOCK. A second run asks the brain's own dump what frame the new mission starts on.
MRF=$(./cnc_eyes --scen SCG74EA --pack SCG01EA.pack $BASE --noshroud --nosound \
          --script gate_missionframe.txt 2>&1 \
      | sed -n 's/^OBJDUMP-END total=[0-9]* frame=\([0-9]*\)$/\1/p')
MRF1=$(echo "$MRF" | sed -n '1p')
MRF2=$(echo "$MRF" | sed -n '2p')
if [ "$MRRC" != "0" ]; then
  bad "G104 mission reset: the run failed (exit $MRRC)"
elif [ "${MRKILL:-0}" -lt 1 ]; then
  bad "G104 mission reset: nothing was destroyed in mission one (live=$MRKILL after the kill, want >=1), so nothing below can mean anything -- the force-fire order or the camera moved"
elif [ "${MRAFTER:-99}" -eq 0 ] && [ "${MRF1:-0}" -gt 100 ] && [ "${MRF2:-99}" -eq 0 ]; then
  ok "G104 mission reset: $MRKILL pieces of rubble in mission one, ZERO in mission two before it has ticked, and the engine clock restarts (mission one ended at frame $MRF1, mission two begins at $MRF2)"
else
  bad "G104 mission reset: rubble-after-the-kill=$MRKILL rubble-in-mission-two=$MRAFTER(want 0; anything else is the last map's wreckage standing on the new one) mission-one-end-frame=$MRF1(want >100) mission-two-start-frame=$MRF2(want 0; a non-zero value means Frame survived Clear_Scenario again and every build timer, AI interval and recharge in the new mission starts part-run)"
fi

# ---------------------------------------------------------------------------
# G105  THE NOD CAMPAIGN FLOW, and specifically its MAP SCREEN.
#
# WHY THIS GATE EXISTS. G14 above drives the same flow for GDI and was green
# throughout the whole period Nod had no map screen at all. Reported: 
# "There are no NOD map screens between any NOD campaign missions currently...
# They still dont work." The second report came AFTER the code was fixed, and the
# cause was not code: campaign.pack in the play directory was twelve days old and
# did not carry EARTH_A / AFRICA / CLICK_A. camp_mapsel looks those up through
# camp_entry, and a missing entry makes it `return 0` -- no globe, no map, no
# error the player ever sees. A GDI-only flow gate cannot see any of that, because
# the GDI reels were in the old pack.
#
# So this gate asserts the NOD side end to end, on the SHIPPED pack, and it fails
# on stale DATA exactly as loudly as on broken code.
NODFLOW=shots/flow105.log
./cnc3d --flowtest 20 --flowside nod --dylib ./TiberianDawn.dylib --dir ./missions/ \
      --content ./content/ --cameos cameos.pack --dospack dossidebar.pack \
      --dosinf dosinfantry.pack > "$NODFLOW" 2>>"$OUT" &
NP=$!; NW=0
while kill -0 $NP 2>/dev/null && [ $NW -lt 300 ]; do sleep 1; NW=$((NW+1)); done
kill $NP 2>/dev/null; wait $NP 2>/dev/null
NL=$(cat "$NODFLOW")
echo "$NL" >> "$OUT"
NMAP=$(echo "$NL" | grep -c "CAMPAIGN|mapsel|picked")
NSCORE=$(echo "$NL" | grep -c "CAMPAIGN|score|")
# The three reels the old pack was missing. camp_entry prints this line and then
# camp_mapsel returns 0, so its ABSENCE is the whole point of the check.
NMISS=$(echo "$NL" | grep -cE "pack has no entry (EARTH_A|AFRICA|CLICK_A)")
NGLOBE=0; [ -s flow_globe.png ] && NGLOBE=1
NMAPPNG=0; [ -s flow_map.png ] && NMAPPNG=1
if [ "$NMAP" = "1" ] && [ "$NSCORE" = "1" ] && [ "$NMISS" = "0" ] \
   && [ "$NGLOBE" = "1" ] && [ "$NMAPPNG" = "1" ]; then
  ok "G105 Nod campaign flow: score screen, grey globe and the Africa map all ran, and the pack carries EARTH_A/AFRICA/CLICK_A"
else
  bad "G105 Nod campaign flow: mapsel=$NMAP(want 1) score=$NSCORE(want 1) missing-reels=$NMISS(want 0; non-zero means campaign.pack is STALE -- re-run game/bake_campaign.py and copy it next to the binary) globe.png=$NGLOBE map.png=$NMAPPNG"
fi

# ---------------------------------------------------------------------------
# G106  A SUPERWEAPON FIRES INTO THE SHROUD. See gate_fogsuper.txt for the defect,
# for why G97 and G98 could not catch it, and for why the SHROUDAT line is part of
# the assertion rather than context.
FSB="--scen SCG74EA --pack SCG01EA.pack $BASE --nosound --w 1280 --h 800"
FSLOG=$(./cnc_eyes $FSB --script "$GATEDIR/gate_fogsuper.txt" 2>&1)
FSRC=$?
echo "$FSLOG" | grep -E '^(EFXION|SHROUDAT|SUPER\|)' >> "$OUT"
FSHID=$(echo "$FSLOG" | grep -c '^SHROUDAT|30,47|HIDDEN')
FSFIRE=$(echo "$FSLOG" | grep -c '^SUPER|fire|SW_Ion')
FSDRAW=$(echo "$FSLOG" | grep -c '^EFXION')
if [ "$FSRC" != "0" ]; then
  bad "G106 superweapon into shroud: the run failed (exit $FSRC)"
elif [ "$FSHID" != "1" ]; then
  bad "G106 superweapon into shroud: cell 30,47 is NOT hidden at this point in SCG74EA (SHROUDAT said $FSHID), so this gate is no longer testing the fog at all -- pick a cell that is still shrouded rather than deleting the check"
elif [ "$FSFIRE" != "1" ]; then
  bad "G106 superweapon into shroud: the ion cannon was never dispatched (SUPER|fire lines=$FSFIRE), so nothing below can mean anything -- the arm or the map click broke, not the shroud rule"
elif [ "$FSDRAW" -ge 1 ]; then
  ok "G106 superweapon into shroud: cell 30,47 is under shroud, the strike dispatched, and it DREW ($FSDRAW EFXION frames)"
else
  bad "G106 superweapon into shroud: the strike was dispatched at a hidden cell and drew NOTHING (EFXION=$FSDRAW). The charge is spent and the player sees no strike, which reads as a dead button. The shroud cull is back in efx_draw_ion_meshes."
fi

# ---------------------------------------------------------------------------
# G107  BUILD ANYWHERE. See gate_buildany.txt for why this reads the placement OVERLAY
# rather than performing a placement, and for the gate that went green over a broken
# cheat before it.
BAB="--scen SCG01EC --pack SCG01EA.pack $BASE --noshroud --nosound --w 1280 --h 800"
BALOG=$(./cnc_eyes $BAB --script "$GATEDIR/gate_buildany.txt" 2>&1)
BARC=$?
echo "$BALOG" | grep -E '^(PLACEMAP\|origin|CHEAT\|buildanywhere)' >> "$OUT"
BA_HAVE=$(echo "$BALOG" | grep -c '^CHEAT|buildanywhere|1')
# two PLACEMAP header lines, in order: switch off, then switch on
BA_OFF=$(echo "$BALOG" | grep '^PLACEMAP|origin' | sed -n 1p)
BA_ON=$(echo "$BALOG" | grep '^PLACEMAP|origin' | sed -n 2p)
# clear is compared as "must not RISE" rather than "must not change": units move between
# the two dumps and a cell a harvester has driven onto stops being buildable, so exact
# equality is not a stable property (measured 330 -> 329 on a clean run). What must never
# happen is buildable ground GAINING cells, which is what lifting the terrain rule would do.
ba_field() { printf '%s' "$1" | sed -n "s/.*|$2=\([0-9]*\).*/\1/p"; }
BA_P0=$(ba_field "$BA_OFF" prox);  BA_P1=$(ba_field "$BA_ON" prox)
BA_C0=$(ba_field "$BA_OFF" clear); BA_C1=$(ba_field "$BA_ON" clear)
BA_L0=$(ba_field "$BA_OFF" legal); BA_L1=$(ba_field "$BA_ON" legal)
if [ "$BARC" != "0" ]; then
  bad "G107 build anywhere: the run failed (exit $BARC)"
elif [ -z "$BA_P0" ] || [ -z "$BA_P1" ]; then
  bad "G107 build anywhere: placement mode was never entered (need two PLACEMAP|origin lines, got $(echo "$BALOG" | grep -c '^PLACEMAP|origin')) -- SCG01EC is the scenario because it is the only one that hands the player a Construction Yard and a build list, so check that first"
elif [ "$BA_HAVE" != "1" ]; then
  bad "G107 build anywhere: the switch never reached the brain (no CHEAT|buildanywhere|1 line) -- this TiberianDawn build may have no CNC3D_Set_Build_Anywhere export"
elif [ "${BA_C1:-99999}" -gt "${BA_C0:-0}" ]; then
  bad "G107 build anywhere: the switch OPENED UP buildable ground (clear $BA_C0 -> $BA_C1). The cheat is only allowed to lift base adjacency; ground that will not take a foundation must still refuse one, which is the whole of what was asked for."
elif [ "${BA_P1:-0}" -ge $(( ${BA_P0:-0} * 2 )) ] && [ "${BA_L1:-0}" -gt "${BA_L0:-0}" ]; then
  ok "G107 build anywhere: the switch opens the map up -- proximity cells $BA_P0 -> $BA_P1 and legal origins $BA_L0 -> $BA_L1 -- while buildable ground does not gain a single cell ($BA_C0 -> $BA_C1)"
else
  bad "G107 build anywhere: the switch changed nothing the player can see (prox $BA_P0 -> $BA_P1, legal $BA_L0 -> $BA_L1, both want a rise). The hook is not on the routine that fills PlacementCellInfo -- check DLLExportClass::Passes_Proximity_Check and DisplayClass::Passes_Proximity_Check, the FOUR argument overload."
fi

# G108  INSTANT WIN / INSTANT LOSE, and the cheat page's layout. See gate_verdict.txt
# for why this clicks the button rather than calling the export.
VDB="--scen SCG74EA --pack SCG01EA.pack $BASE --noshroud --nosound --w 1280 --h 800"
VWLOG=$(./cnc_eyes $VDB --script "$GATEDIR/gate_verdict.txt" 2>&1)
VWRC=$?
VLLOG=$(./cnc_eyes $VDB --script "$GATEDIR/gate_verdict_lose.txt" 2>&1)
VLRC=$?
echo "$VWLOG" | grep -E '^(CHEAT\|verdict|GAMEOVER|CHEATRECT\|)' >> "$OUT"
echo "$VLLOG" | grep -E '^(CHEAT\|verdict|GAMEOVER)' >> "$OUT"
VWIN=$(echo "$VWLOG" | grep -c '^GAMEOVER|WIN')
VLOSE=$(echo "$VLLOG" | grep -c '^GAMEOVER|LOSE')
# No control on the page may share a pixel with another. The page went from six rows to
# seven plus a second button row, and the geometry comment was the only thing checking.
VOVER=$(echo "$VWLOG" | awk -F'|' '/^CHEATRECT\|/ {
    split($4,p,","); split($5,d,"x");
    n++; X[n]=p[1]; Y[n]=p[2]; W[n]=d[1]; H[n]=d[2];
  }
  END { c=0;
    for (i=1;i<=n;i++) for (j=i+1;j<=n;j++)
      if (X[i] < X[j]+W[j] && X[j] < X[i]+W[i] && Y[i] < Y[j]+H[j] && Y[j] < Y[i]+H[i]) c++;
    print c }')
if [ "$VWRC" != "0" ] || [ "$VLRC" != "0" ]; then
  bad "G108 instant verdict: a run failed (win exit $VWRC, lose exit $VLRC)"
elif [ "${VOVER:-99}" != "0" ]; then
  bad "G108 instant verdict: $VOVER pairs of controls overlap on the cheat page -- something was added without re-checking DOPT_CH_STEP and DOPT_CH_BTN_Y against the box height"
elif [ "$VWIN" = "1" ] && [ "$VLOSE" = "1" ]; then
  ok "G108 instant verdict: both buttons end the mission through the engine's own chain (GAMEOVER|WIN and GAMEOVER|LOSE), and no two controls on the page overlap"
else
  bad "G108 instant verdict: win=$VWIN lose=$VLOSE (want 1 each). A CHEAT|verdict line with flagged=1 and no GAMEOVER means the flag was raised and HouseClass::AI never acted on it; no CHEAT|verdict line at all means the button did not route to DOPT_ACT_CHEAT_WIN/LOSE."
fi

# ---------------------------------------------------------------------------
# G109  THE SAM SITE'S FIRING SOUND. See gate_samsound.txt for the three things about
# the script that are load-bearing.
#
# --audiowav IS PART OF THE ASSERTION, not a convenience. A headless run with no audio
# sink never calls mixer_render, so mixer voices never retire, the 32-voice pool
# saturates for good, and after about 32 sounds --dumpsound starts printing SILENT for
# every low-priority sound. ROCKET2 is in the lowest priority tier, so it is the first
# thing that would lie. With a sink the voices retire and the report is honest.
SSB="--scen SCG13EA --pack SCG13EA.pack $BASE --noshroud --dumpsound --musicvol 0"
SSLOG=$(./cnc_eyes $SSB --audiowav shots/g109.wav --w 1280 --h 800 \
        --script "$GATEDIR/gate_samsound.txt" 2>&1)
SSRC=$?
echo "$SSLOG" | grep -E '^(SOUND\|sfx\|59\|ROCKET2|SOUND\|end)' >> "$OUT"
SS_PLAYED=$(echo "$SSLOG" | grep -c '^SOUND|sfx|59|ROCKET2|.*|played$')
SS_SILENT=$(echo "$SSLOG" | grep -c '^SOUND|sfx|59|ROCKET2|.*|SILENT$')
# Every ROCKET2 must come from a BUILDING: px is the firer's coord at 24 px per cell, so
# both components are exact multiples of 24. A unit or a bullet would not be.
SS_BLDG=$(echo "$SSLOG" | sed -n 's/^SOUND|sfx|59|ROCKET2|var=[0-9]*|px=\([0-9]*\),\([0-9]*\)|.*/\1 \2/p' \
          | awk '{ if ($1 % 24 == 0 && $2 % 24 == 0) n++ } END { print n+0 }')
if [ "$SSRC" != "0" ]; then
  bad "G109 SAM firing sound: the run failed (exit $SSRC)"
elif [ "$SS_PLAYED" -lt 2 ]; then
  bad "G109 SAM firing sound: $SS_PLAYED ROCKET2 events played (want 2 or more), $SS_SILENT silent. If the count is ZERO the SAMs almost certainly never engaged rather than fired silently -- check the air strike delivered its A-10s (airdump) and that the script still ticks ~350 past the click, because they take about 250 ticks to arrive."
elif [ "$SS_SILENT" != "0" ]; then
  bad "G109 SAM firing sound: $SS_SILENT of $((SS_PLAYED + SS_SILENT)) ROCKET2 events came back SILENT. With --audiowav the voice pool retires normally, so this is a real drop rather than the saturation artifact."
elif [ "$SS_BLDG" != "$SS_PLAYED" ]; then
  bad "G109 SAM firing sound: only $SS_BLDG of $SS_PLAYED ROCKET2 events came from a building coordinate (px a multiple of 24 on both axes). Something other than the SAM is raising ROCKET2 and this gate is no longer watching the SAM."
else
  ok "G109 SAM firing sound: the SAMs engage the air strike and raise $SS_PLAYED ROCKET2 events, every one played and every one at a building coordinate"
fi

echo "----- $LABEL: $PASS pass, $FAIL fail -----" | tee -a "$OUT"
exit $FAIL
