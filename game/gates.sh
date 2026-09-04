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
# AN UNCHECKED cd MADE THE WHOLE SUITE REPORT SUCCESS WITHOUT RUNNING A SINGLE GATE.
# Given a RUNDIR that does not exist the cd failed, every gate below ran against whatever
# directory the caller happened to be in, ": > $OUT" failed too, and the script still
# exited 0. Two lines on stderr, no log, no gates, and an exit code that says everything
# passed. That is the worst member of the "the suite lies" family, because the other four
# lie about one gate and this one lies about all of them. Found by running the invocation
# the handoff documented, which named a run folder this checkout does not have. The run
# folder is playable/.
cd "$RUNDIR" || { echo "FAIL: RUNDIR is not a directory: $RUNDIR" >&2; exit 1; }
LABEL="${1:-run}"
# A run folder that EXISTS but holds no game is the same lie told more slowly, so say so
# once and plainly rather than through 158 failures that each blame the code.
[ -x ./cnc3d ] || { echo "FAIL: $RUNDIR is not a run folder (no cnc3d in it)" >&2; exit 1; }
OUT="../gates/$LABEL.log"
: > "$OUT" || { echo "FAIL: cannot write $RUNDIR/$OUT" >&2; exit 1; }
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
# the two are the same number.
#
# THAT IS TRUE OF THE PAUSE LEG AND NOT OF THE WHOLE GATE, and the paragraph above used
# to end "the wall clock now only decides how long we wait, never what passes", which is
# false. The last leg still asserts TICKS >= 1, and TICKS is how much live play happened
# in the three wall-clock seconds before ESC. That leg is not decoration: without it a
# build that never ticked at all would pass the frame-equality test trivially, since
# 0 == 0 is a perfectly good pause. But it is a throughput measurement, so a loaded
# machine can still fail it.
#
# It did, on 1 Sep 2026, with seven analysis agents running beside the suite: the gate
# read opened=1 aborted=1 dwelled=1 frame_open=0 frame_abort=0 ticks=0. Note what that
# says: the two frames AGREE, so the pause worked perfectly; the game simply had not
# reached its first tick when ESC arrived. Standalone on the same binary a minute later
# it read frame 45 both sides and 46 ticks.
#
# THE REAL FIX is to drive the ESC off the ENGINE FRAME rather than off the wall clock,
# so the gate cannot be starved into a false red. That needs an --autoesc that counts
# ticks, which does not exist yet, and it is registered rather than
# bodged here by widening the threshold. Do NOT "fix" this by re-running until green:
# this gate's own history is the argument against that, and re-running is what hid it.
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
# all. the reported "when a Refinery is being Placed, it immediately plays the Unload animation"
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
  # Clouds off in both arms: they are the same in each and would therefore look like a
  # control, but they are not one. The cloud and the shadow combine with max(), so a cloud
  # swallows the softer half of the blob under a man and the per-man band roughly halves.
  printf 'enabled 1\nshadow_on 1\nsun_az %s\nsun_el %s\ncloud_on 0\n' "$SSAZ" "$SSEL" > gfx_g52_on.cfg
  printf 'enabled 1\nshadow_on 0\nsun_az %s\nsun_el %s\ncloud_on 0\n' "$SSAZ" "$SSEL" > gfx_g52_off.cfg
  rm -f shots/sprshadow_on.png shots/sprshadow_off.png shots/g52.log
  ./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE --noshroud --nominimap --nosidebar \
      --gfx gfx_g52_on.cfg --w 1000 --h 650 --script gate_sprshadow.txt > shots/g52.log 2>&1
  SSRC=$?
  ./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE --noshroud --nominimap --nosidebar \
      --gfx gfx_g52_off.cfg --w 1000 --h 650 --script gate_sprshadow_off.txt >> shots/g52.log 2>&1
  SSRC2=$?
  cat shots/g52.log >> "$OUT"
  SSPRE=$(grep -c "FX|preset|gfx_g52_o[nf]*\.cfg: 5 applied, 0 unknown" shots/g52.log)
  SSBOX=$(sed -n 's/^CLICKOBJ|want=E1[^|]*|silhouette=\([0-9]*\),\([0-9]*\)\.\.\([0-9]*\),\([0-9]*\).*/\1,\2,\3,\4/p' shots/g52.log | head -4 | tr '\n' ' ')
  SSNB=$(echo $SSBOX | wc -w | tr -d ' ')
  if [ "$SSRC" != "0" ] || [ "$SSRC2" != "0" ]; then
    bad "G52 infantry shadows at sun $SSAZ/$SSEL: the run itself failed (exit $SSRC / $SSRC2)"
  elif [ "$SSPRE" != "2" ]; then
    bad "G52 infantry shadows at sun $SSAZ/$SSEL: the preset did not apply in both arms -- $SSPRE of 2 runs said '5 applied, 0 unknown', so the numbers below would be the DEFAULT sun and not the one this gate asked for"
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
#      something else entirely. It counts inside one building's own silhouette now, read
#      from the CLICKOBJ line of the same run.
#
#   3  THE SUBJECT IS NO LONGER THE HELIPAD, and the reason is the point of the gate.
#      The books used to free-run on a wall clock, so any book differed from --notexbook
#      at almost any tick. They take the building's own state now, and an idle helipad
#      correctly holds slot 0. HPADT0 is byte for byte the HPAD base mesh, so drawing it
#      is a pixel no-op and this gate would read 0 for the RIGHT reason, unable to tell a
#      book that is correctly still from a book that never arrived. It runs SCG08EA with
#      the camera on the GoodGuy refinery at 44,46, whose book sits on its lit slot while
#      idle. Measured: 606 pixels inside the box change when the books go off; with
#      --notexbook forced on BOTH arms it reads 0, which is this gate's own red.
#
# Delete first and read both exit statuses (G4). This gate measures ONE shot against the
# OTHER, so a stale pair left by an earlier run agrees with itself perfectly and passes.
rm -f shots/texbook_on.png shots/texbook_off.png shots/g56.log
./cnc_eyes --scen SCG08EA --pack SCG08EA.pack $BASE --noshroud --nominimap --nosidebar \
    --w 900 --h 600 --script gate_texbook.txt > shots/g56.log 2>&1
TBRC=$?
./cnc_eyes --scen SCG08EA --pack SCG08EA.pack $BASE --noshroud --nominimap --nosidebar \
    --notexbook --w 900 --h 600 --script gate_texbook_off.txt >> shots/g56.log 2>&1
TBRC2=$?
cat shots/g56.log >> "$OUT"
TBBOX=$(sed -n 's/^CLICKOBJ|want=PROC[^|]*|silhouette=\([0-9]*\),\([0-9]*\)\.\.\([0-9]*\),\([0-9]*\).*/\1,\2,\3,\4/p' shots/g56.log | head -1)
if [ "$TBRC" != "0" ] || [ "$TBRC2" != "0" ]; then
  bad "G56 texture books: the run itself failed (exit $TBRC / $TBRC2)"
elif [ -z "$TBBOX" ]; then
  bad "G56 texture books: no refinery silhouette in the run, so there is nothing to measure inside"
elif [ ! -s shots/texbook_on.png ] || [ ! -s shots/texbook_off.png ]; then
  bad "G56 texture books: a shot was not written"
else
  TBD=$(python3 "$GATEDIR/gate_pixdiff.py" shots/texbook_on.png shots/texbook_off.png $TBBOX)
  if [ "${TBD:-0}" -ge 100 ]; then
    ok "G56 texture books: $TBD pixels inside the refinery's own silhouette $TBBOX change when the books are switched off, and an idle helipad holding slot 0 is why this is no longer measured there"
  else
    bad "G56 texture books: pixels-changed-inside-the-refinery=$TBD(want >=100) box=$TBBOX"
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
# CLOUDS ARE OFF IN THIS CFG for the same reason bloom, occlusion, the lights, the grade
# and the CRT are: what is being measured is the SUN, and everything else that touches the
# same pixels is held still so the difference between the two arms is the one thing under
# test. Clouds joined that list on 4 Sep 2026 when they were switched on by default, and
# they belong in it more than most: the cloud and the cast shadow combine with max(), so a
# cloud sitting over the scene raises the floor and SWALLOWS every part of a shadow weaker
# than itself. Left on, this gate read 133961 darkened pixels at 40 degrees and only
# 117566 at 18 -- the low sun appearing to SHORTEN the shadows, which is the opposite of
# what it does and would have been a convincing bug report about the sun.
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
cloud_on 0
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
# This used to ask for 3008x1692, the project owner's display's exact size in points. On 24 Aug a
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
# development screen rather than only the project owner's.
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
# A PARKED HELICOPTER STANDS ON THE PAD, and this arm used to demand the opposite. It
# asked for lift 0.0000 at Altitude 0, which is precisely the defect: Altitude 0 means "on
# the floor", and on a helipad the floor is the model's landing plate, one flat face 1.38
# square cells across at mesh y 154, i.e. 154/1024 = 0.1504 cells up. All four of
# SCB70EA's parked aircraft sit on one of its four helipads (two GoodGuy, two BadGuy).
# A 0.0000 here now means the deck lookup found nothing and the Apache is buried to 51
# percent of its own height again.
ACGROUND=$(echo "$ACLOG" | grep -c '^AIR|HELI|.*|alt=0|lift=0.1504|')
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
  ok "G70 aircraft: the Aircraft heap reaches the renderer (4 parked, every one with a mesh), Orcas carry the ROM's 5 ammo pips, parked helicopters stand on their helipad's own landing plate at 0.1504 cells instead of sunk into it, firing the Airstrike adds THREE A-10s each flying at FLIGHT_LEVEL 24 = 0.9375 cells, a parked Apache WINS the pick against the helipad under it, it draws its ammo strip once selected, and every aircraft is DRAWN where it is pointing: the A-10's nose tracks its flight path over 40 moving ticks, all $ACBODY dumped aircraft draw at their body facing rather than unrotated, and the three A-10s end on $ACTURN different headings"
else
  bad "G70 aircraft: parked=$ACPARKED(want >=1, 0 means the heap is still not exported) meshless=$ACMESH(want 0) orca-ammo=$ACAMMO(want >=2) on-pad-deck=$ACGROUND(want >=2; 0 means a parked helicopter is back down on the ground under its pad) airborne-A10s=$ACFLY(want >=3; 0 means the strike is still invisible) after-strike=$ACAFTER(want >=1) picked-aircraft=$ACPICK(want >=1; 0 means the helipad won again) ammo-strip=$ACPIP(want >=1) facewatch-A10=$ACFACE(want >=1 PASS line) drawn-at-body-facing=$ACBODYOK/$ACBODY(want all; a shortfall means renderdir is -1 or is following face instead of tface) distinct-A10-headings=$ACTURN(want >=3; 1 means every plane is drawn at one frozen pose)"
fi

# G79 RIGHT EDGE SCROLLING WORKS WHILE THE SIDEBAR IS OUT.
#
# THE FAULT, IN TWO HALVES. The right-hand scroll strip was measured from the WINDOW's
# right edge, so it sat wholly underneath the sidebar; the live loop refuses to
# edge-scroll from over the panel, so there was no pixel that both triggered a right
# scroll and cleared the guard. Right edge scrolling was unreachable at every window
# size, in both HUDs, permanently on the default DOS bar because that bar has no drawer
# to slide out of the way. Measuring the strip from the TACTICAL VIEW's right edge put it
# on the map just left of the bar and closed that half.
#
# It did not close the other half, and three more reports said so. A pointer thrown at
# the PHYSICAL right edge of the window stops on the bar, past the map-side strip, and
# answered nothing. The window's own last 12 columns are therefore a SECOND strip, exempt
# from the panel veto, and this gate measures both of them, the bar between them, and the
# window-pinned plates that still refuse inside the second strip.
#
# WHY NOTHING CAUGHT IT, and how much of that this gate actually closes. The `edge` script
# verb called the scroller RAW, with none of the guard the live loop applies, so the
# harness scrolled from a pixel no player could scroll from. Both paths now go through
# edge_scroll_allowed, and every probe below prints whether the guard passed, so a GUARD
# divergence cannot come back.
#
# THAT IS NOT THE SAME AS COVERING THE GESTURE, and this paragraph used to say it was.
# What is measured here is the shared core: sb_tactical_right's geometry, the two-strip
# test inside edge_scroll, the whole of edge_scroll_allowed, pan_screen_px and the camera
# clamp, on both HUDs and with the 640 drawer out and hidden. That is real coverage and it
# is worth keeping.
#
# WHAT IT CANNOT REACH, by construction, because `edge` and `edgeat` are handed a pixel:
# the SDL motion event, mscaleX and the mouseC it produces, the highest column the game
# can ever hold, edgeArmed, SDL_WINDOWEVENT_LEAVE clearing the tracked pointer to -1,
# keyboard focus, mdrag, the editor's playtest containment, a real frame time, and above
# all whether a hand can put the pointer on the pixel at all. A later report was exactly
# that: the pointer thrown at the right-hand wall goes PAST the window, the leave clears
# the position and nothing scrolls, and a one-line sabotage that reproduced it left all
# twelve numbers below byte identical and this gate green. G124 is the gate for that
# layer; it drives the real pointer through the live handler. Read the two together.
#
# PROVEN TO FAIL, once per strip. Measure the right strip from the window width ALONE
# (mx >= fbw-EDGE_PX) and arm 1 reports east=0.000 with the sidebar out while the
# --nosidebar control still reports 4.046. Take the window strip back out instead and the
# probe at 1278,360 reports guard=blocked and x=45.000 with the bar out, which is
# verbatim what the pre-fix log recorded for that pixel.
es_x() { echo "$1" | grep "^CAM|$2|" | sed -n "$3p" | sed 's/.*|x=\([0-9.-]*\)|.*/\1/'; }
ESL=$(./cnc_eyes --scen SCG01EB --pack SCG01EA.pack $BASE --nosound --w 1280 --h 720 \
      --script gate_edgescroll.txt 2>&1 | tee -a "$OUT")
ESNL=$(./cnc_eyes --scen SCG01EB --pack SCG01EA.pack $BASE --nosound --nosidebar \
      --w 1280 --h 720 --script gate_edgescroll.txt 2>&1 | tee -a "$OUT")
ESHL=$(CNC3D_HUD=new ./cnc_eyes --scen SCG01EB --pack SCG01EA.pack $BASE --nosound --gfx \
      --w 1280 --h 960 --script gate_edgescroll_h6.txt 2>&1 | tee -a "$OUT")
# The DOS bar is 240 wide at 1280x720, so the map ends at 1040, the map-side strip is
# [1028,1040) and the window strip is [1268,1280). The verb has to AIM at the map-side
# strip and print where it aimed: an `edge R` that aimed at 1278 would be measuring the
# window strip twice and the map-side one not at all. 1200 is DOS x 293, inside column
# two's cameos, and is the bar between the two strips; 1267 is the last column before the
# window strip, which is what pins that strip's width at 12 rather than "near the edge".
# The 640x480 HUD runs at 1280x960, where the whole-number zoom is 2 and the bar is 320
# wide, so its map ends at 960 and its EDGEAT lines say so.
ESAIM=$(echo "$ESL"  | grep -c '^EDGE|R|frames=20|mouse=1038,360|tacright=1040|guard=pass')
ESPASS=$(echo "$ESL" | grep -c '^EDGEAT|frames=20|mouse=1278,360|tacright=1040|guard=pass')
ESBLK=$(echo "$ESL"  | grep -c '^EDGEAT|frames=20|mouse=1200,360|tacright=1040|guard=blocked')
ESH6P=$(echo "$ESHL" | grep -c '^EDGEAT|frames=20|mouse=1278,400|tacright=960|guard=pass')
ESH6B=$(echo "$ESHL" | grep -c '^EDGEAT|frames=20|mouse=1200,400|tacright=960|guard=blocked')
ESEAST=$(es_x "$ESL" edge 1); ESWEST=$(es_x "$ESL" edge 2)
ESWIN=$(es_x "$ESL" edgeat 1); ESOFF=$(es_x "$ESL" edgeat 2)
ESBAR=$(es_x "$ESL" edgeat 3); ESLIP=$(es_x "$ESL" edgeat 4)
ESCTL=$(es_x "$ESNL" edge 1)
ESH6OUT=$(es_x "$ESHL" edge 1); ESH6HID=$(es_x "$ESHL" edge 2)
ESH6WIN=$(es_x "$ESHL" edgeat 1); ESH6BAR=$(es_x "$ESHL" edgeat 2)
ESH6TAB=$(es_x "$ESHL" edgeat 3)
# Eleven numeric claims, all against the same start of x=45.000, and each one fails on
# its own. The zero arms matter as much as the moving ones: a fix that scrolled from
# EVERYWHERE would satisfy every moving arm and wreck the game. The four moving arms on
# the DOS run must agree to 0.002, because both strips feed the same pan at the same
# EDGE_SPEED for the same 20 frames: a window strip that scrolled a DIFFERENT distance
# would mean it is being driven from somewhere other than edge_scroll.
# THE MAP-SIDE STRIP IS A ZERO ARM NOW, and that reversal is the assertion. Until 1 Sep
# 2026 [tacRight-12, tacRight) was a second east trigger and this gate REQUIRED it to
# move; it was removed because sliding the pointer off the map onto the sidebar dragged
# the map east on the way in. So ESEAST (the verb aims at tacRight-2, on the map) and
# ESH6OUT (the same with the 640 drawer out) must now equal the start exactly, while the
# window edge keeps moving. Aiming is still asserted separately by ESAIM: the guard must
# still PASS at that pixel, because it is map and not panel, and the refusal has to come
# from edge_scroll declining rather than from the veto blocking. A build that reinstated
# the strip would fail here and nowhere else.
ESVERDICT=$(awk -v e="$ESEAST" -v w="$ESWEST" -v n="$ESWIN" -v o="$ESOFF" -v b="$ESBAR" \
                -v l="$ESLIP" -v c="$ESCTL" \
                -v h1="$ESH6OUT" -v h2="$ESH6HID" -v h3="$ESH6WIN" -v h4="$ESH6BAR" \
                -v h5="$ESH6TAB" '
function ab(v) { return v < 0 ? -v : v }
BEGIN{
  s = 45.0; eps = 0.002;
  dw = s - w; dn = n - s; dc = c - s;
  d2 = h2 - s; d3 = h3 - s;
  print (dw > 1.0 && ab(dn - dw) < eps && ab(dc - dw) < eps \
      && d2 > 1.0 && ab(d3 - d2) < eps \
      && ab(e - s) < eps && ab(o - s) < eps && ab(b - s) < eps && ab(l - s) < eps \
      && ab(h1 - s) < eps && ab(h4 - s) < eps && ab(h5 - s) < eps) ? 1 : 0
}')
if [ "${ESAIM:-0}" -ge 1 ] && [ "${ESPASS:-0}" -ge 1 ] && [ "${ESBLK:-0}" -ge 1 ] \
   && [ "${ESH6P:-0}" -ge 1 ] && [ "${ESH6B:-0}" -ge 1 ] && [ "${ESVERDICT:-0}" = "1" ]; then
  ok "G79 right edge scroll (the SHARED CORE only; the live gesture is G124's). Only the SCREEN edge scrolls east: the window's own last 12 columns pan from 1278 ($ESWIN) although that pixel is on the bar, the same distance the left edge pans west, and the same distance as the --nosidebar control ($ESCTL) whose map edge and window edge are the same columns. THE MAP-SIDE STRIP REFUSES: aiming at tacRight-2 with the guard passing moves nothing ($ESEAST), so sliding the pointer off the map onto the sidebar no longer drags the map. A pixel just left of it at 1020 ($ESOFF), the middle of the bar at 1200 ($ESBAR) and the last column before the window strip at 1267 ($ESLIP) all refuse too. On the 640x480 HUD it pans east from the window edge deployed ($ESH6WIN) and with the drawer hidden ($ESH6HID), while the map edge deployed ($ESH6OUT), the middle of the bar ($ESH6BAR) and the SIDEBAR handle plate inside the window strip ($ESH6TAB) all refuse"
else
  bad "G79 right edge scroll: aimed-at-map=$ESAIM(want >=1; 0 means the verb is aiming at the window edge again) window-edge-passes=$ESPASS(want >=1; 0 means the last 12 columns are still vetoed by the panel) bar-blocked=$ESBLK(want >=1) h6-window-passes=$ESH6P(want >=1) h6-bar-blocked=$ESH6B(want >=1) east=$ESEAST west=$ESWEST window=$ESWIN control=$ESCTL mid-bar=$ESBAR before-strip=$ESLIP off-strip=$ESOFF h6-out=$ESH6OUT h6-window=$ESH6WIN h6-hidden=$ESH6HID h6-mid-bar=$ESH6BAR h6-tab=$ESH6TAB (start 45.000; WEST, WINDOW and CONTROL must differ from it by one same amount, and EAST must EQUAL it because the map-side strip was removed on 1 Sep 2026 -- a non-zero east means that strip is back and the pointer drags the map on its way onto the sidebar; mid-bar, before-strip and off-strip must equal it; on the 640 HUD h6-window and h6-hidden must differ by one same amount and h6-out, h6-mid-bar and h6-tab must equal it)"
fi


# =====================================================================================
# G141 THE TOP EDGE SCROLLS ALONG ITS WHOLE LENGTH, PLATES OR NO PLATES.
#
# G79 above proved the EAST strip is exempt from the panel veto. The other three strips
# never got the same treatment, and on the 640x480 HUD the top one paid for it: the three
# window-pinned plates share a tab band along the top of the window, sb_over_panel ORs
# sb_chrome_hit in, and the band is taller than the strip, so the top edge was dead
# wherever a plate lay under the pointer. Measured at 1280x960 before the fix: the top
# edge scrolled only over x 320..639, a quarter of the width, and the left edge was dead
# for its first 34 rows. The DOS bar has no plates and was never affected, which is what
# makes it the control here: every number this gate wants from the 640x480 HUD is read
# back off the DOS run of the identical script.
#
# WHY THE EDGE GATES DID NOT CATCH IT. G79 and G124 between them probe the left, right
# and bottom edges and never the top, which is how a fault that reproduces by hand every
# time stayed live through two releases. This gate is the top edge's own.
#
# PROVEN TO FAIL, three ways. Put the exemption back the way it was and the OPTIONS and
# CREDITS probes report guard=blocked and z=45.000, which is verbatim what the pre-fix
# build records for those pixels. Widen it to every plate and the SIDEBAR probe starts
# moving, which is the one thing the exemption is not allowed to swallow, since a hidden
# drawer's only way back is that plate. Move or delete the plates and the sbhit lines
# stop naming them, so the probes cannot quietly decay into plain map pixels and pass.
cat > gate_edgescroll_top.txt <<'EOS'
sbhit 100 2
sbhit 800 2
sbhit 400 2
sbhit 1100 2
sbhit 2 20
cam 45 45
edgeat 100 2 20
cam 45 45
edgeat 800 2 20
cam 45 45
edgeat 400 2 20
cam 45 45
edgeat 1100 2 20
cam 45 45
edgeat 1100 400 20
cam 45 45
edgeat 2 20 20
cam 45 45
edgeat 2 480 20
quit
EOS
# 1280x960 is the pixel-exact size for this HUD: the whole-number zoom is 2, the bar is
# 320 wide, the plates are OPTIONS [0,320), DATABASE [320,640), CREDITS [640,960) and
# SIDEBAR [960,1280), and the tab band starts at row 0 so it covers the whole 12 row top
# strip. Probe 6 sits at row 20, inside the band but BELOW the strip, so it measures the
# LEFT edge rather than the top one and the two halves of the fault are separated.
#
# THE x=400 PROBE USED TO BE THE BARE MAP AND IS NOW THE DATABASE PLATE. When the codex
# took the empty slot right of OPTIONS it filled the last gap in this strip, so on the
# Enhanced HUD at this size there is no longer a single top-edge pixel that is not a
# plate. That is why the DOS run is not a convenience here but the only bare-map control
# left: it has no plates at all, and every moving arm is measured against it.
#
# The plate arrived without being added to the exemption list this gate exists to police,
# and this arm caught it: x=400 reported a plate and refused to scroll while every other
# arm moved. That is the gate working, and the numbers it printed are why the geometry
# above is now written out in full.
te_z() { echo "$1" | grep "^CAM|edgeat|" | sed -n "$2p" | sed 's/.*|z=\([0-9.-]*\)|.*/\1/'; }
te_x() { echo "$1" | grep "^CAM|edgeat|" | sed -n "$2p" | sed 's/.*|x=\([0-9.-]*\)|.*/\1/'; }
TEH=$(CNC3D_HUD=new ./cnc_eyes --scen SCG01EB --pack SCG01EA.pack $BASE --nosound --gfx \
      --w 1280 --h 960 --script gate_edgescroll_top.txt 2>&1 | tee -a "$OUT")
TED=$(./cnc_eyes --scen SCG01EB --pack SCG01EA.pack $BASE --nosound \
      --w 1280 --h 960 --script gate_edgescroll_top.txt 2>&1 | tee -a "$OUT")
# The geometry the probes rest on, asserted rather than assumed.
TEPL=$(echo "$TEH" | grep -c '^SBHIT|100,2|options|')
TEPR=$(echo "$TEH" | grep -c '^SBHIT|800,2|credits|')
TEPM=$(echo "$TEH" | grep -c '^SBHIT|400,2|database|')
TEPS=$(echo "$TEH" | grep -c '^SBHIT|1100,2|sidebar|')
TEPW=$(echo "$TEH" | grep -c '^SBHIT|2,20|options|')
TEGO=$(echo "$TEH" | grep -c '^EDGEAT|frames=20|mouse=100,2|tacright=960|guard=pass')
TEGC=$(echo "$TEH" | grep -c '^EDGEAT|frames=20|mouse=800,2|tacright=960|guard=pass')
TEGD=$(echo "$TEH" | grep -c '^EDGEAT|frames=20|mouse=400,2|tacright=960|guard=pass')
TEGS=$(echo "$TEH" | grep -c '^EDGEAT|frames=20|mouse=1100,2|tacright=960|guard=blocked')
TEGW=$(echo "$TEH" | grep -c '^EDGEAT|frames=20|mouse=2,20|tacright=960|guard=pass')
TOPO=$(te_z "$TEH" 1); TOPC=$(te_z "$TEH" 2); TOPM=$(te_z "$TEH" 3)
TOPS=$(te_z "$TEH" 4); TOPB=$(te_z "$TEH" 5)
WSTB=$(te_x "$TEH" 6); WSTC=$(te_x "$TEH" 7)
TOPD=$(te_z "$TED" 3); WSTD=$(te_x "$TED" 7)
# Nine numeric claims against the same start of 45.000. The two zero arms carry as much
# as the moving ones: an exemption that let EVERYTHING scroll would satisfy every moving
# arm and hand the bar's own columns to the camera.
TEV=$(awk -v o="$TOPO" -v c="$TOPC" -v m="$TOPM" -v d="$TOPD" -v s="$TOPS" -v b="$TOPB" \
          -v wb="$WSTB" -v wc="$WSTC" -v wd="$WSTD" '
function ab(v) { return v < 0 ? -v : v }
BEGIN{ st = 45.0; eps = 0.002;
  print ((st - m) > 1.0 && ab(o - m) < eps && ab(c - m) < eps && ab(d - m) < eps \
      && (st - wc) > 1.0 && ab(wb - wc) < eps && ab(wd - wc) < eps \
      && ab(s - st) < eps && ab(b - st) < eps) ? 1 : 0 }')
if [ "${TEPL:-0}" -ge 1 ] && [ "${TEPR:-0}" -ge 1 ] && [ "${TEPM:-0}" -ge 1 ] \
   && [ "${TEPS:-0}" -ge 1 ] && [ "${TEPW:-0}" -ge 1 ] && [ "${TEGO:-0}" -ge 1 ] \
   && [ "${TEGC:-0}" -ge 1 ] && [ "${TEGD:-0}" -ge 1 ] && [ "${TEGS:-0}" -ge 1 ] \
   && [ "${TEGW:-0}" -ge 1 ] \
   && [ "${TEV:-0}" = "1" ]; then
  ok "G141 top edge scroll under the 640x480 plates. The top strip pans north from under OPTIONS at x=100 ($TOPO) and from under CREDITS at x=800 ($TOPC), and from under DATABASE at x=400 ($TOPM), all three the same distance as the DOS bar, which has no plates at all and is now the only bare-map control this strip has ($TOPD). The left strip pans west from row 20, inside the OPTIONS band ($WSTB), the same distance as row 480 below it ($WSTC) and as the DOS control ($WSTD). THE DRAWER HANDLE KEEPS ITS VETO: the top strip refuses under it ($TOPS), because that plate is the only way to bring a hidden drawer back, and the middle of the bar off any strip refuses too ($TOPB)"
else
  bad "G141 top edge scroll: plates-named=$TEPL$TEPR$TEPM$TEPS$TEPW(want 11111; a 0 means a probe is no longer on the plate it is named for, so the rest measures nothing) guards=$TEGO$TEGC$TEGD$TEGS$TEGW(want 11111) top-options=$TOPO top-credits=$TOPC top-database=$TOPM top-dos=$TOPD top-sidebar=$TOPS mid-bar=$TOPB west-in-band=$WSTB west-below=$WSTC west-dos=$WSTD (start 45.000; the three plate arms -- OPTIONS, CREDITS and DATABASE -- and the DOS control must agree to 0.002 and must have moved north by more than 1, the two west arms and their DOS control likewise; top-sidebar and mid-bar must EQUAL the start, and a moving top-sidebar means the exemption swallowed the drawer handle)"
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
# THE FOURTH ROW OF EACH COLUMN, which shipped laid out and undrawn while both of its
# controls were already taking input. The string fit above answers neither question that
# matters here: does the row DRAW, and does what it draws reach the settings block.
# The values are the script's own, so a default and a driven answer cannot be confused: it
# pulls Unit Count hard right to 10 and the keyboard half nudges it to 9, and it switches
# Bonus Crates on. Undrawn, units stays 0 and crates stays 0.
LBUNITS=$(echo "$LBLOG" | sed -n 's/^LOBBYSHOT|.*|units=\([0-9]*\).*/\1/p')
LBCRATES=$(echo "$LBLOG" | sed -n 's/^LOBBYSHOT|.*|crates=\([0-9]*\).*/\1/p')
# And the PIXELS, because a number in a log line is not a control on a screen: every value
# above was already correct on the build that drew neither of these rows. The frame is read
# out of the run by the control's own printed LABEL rather than typed in as an index, so
# inserting a script step cannot silently point this at the wrong picture. Each box is that
# control's own rectangle in menu pixels times the shot's 3x scale, and is clear of where
# the pointer stands in either frame. Undrawn, both counts are exactly 0.
LBUSHOT=$(echo "$LBLOG" | grep -A1 '^LOBBY|click|Unit Count:|' | sed -n 's/^LOBBY|shot|\([^|]*\)|.*/\1/p' | head -1)
LBCSHOT=$(echo "$LBLOG" | grep -A1 '^LOBBY|click|Bonus Crates|' | sed -n 's/^LOBBY|shot|\([^|]*\)|.*/\1/p' | head -1)
LBUD=0; LBCD=0
# the gauge trough, menu x 103..144 y 141..145: it goes from empty to full travel
[ -n "$LBUSHOT" ] && LBUD=$(python3 "$GATEDIR/gate_pixdiff.py" shots/lobby/lobby00.png "$LBUSHOT" 309,423,434,437 2>/dev/null)
# the check box, menu x 186..192 y 140..146: raised and empty to pressed and filled
[ -n "$LBCSHOT" ] && LBCD=$(python3 "$GATEDIR/gate_pixdiff.py" shots/lobby/lobby00.png "$LBCSHOT" 558,420,578,440 2>/dev/null)
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
elif [ "${LBMEAS:-0}" -ge 30 ] && [ "${LBOVER:-1}" = "0" ] \
     && [ "${LBUNITS:-0}" = "9" ] && [ "${LBCRATES:-0}" = "1" ] \
     && [ "${LBUD:-0}" -ge 200 ] && [ "${LBCD:-0}" -ge 20 ]; then
  ok "G86 lobby screen: every control drives over $LBMAPS maps, $LBSHOTS frames rendered, all $LBMEAS strings it draws fit inside their own boxes, and the fourth row of both columns both paints and answers (Unit Count $LBUD pixels of travel, ends at $LBUNITS; Bonus Crates $LBCD pixels, ends at $LBCRATES)"
else
  bad "G86 lobby screen: strings measured=$LBMEAS(want >=30; a low count means the check ran on an empty screen and proved nothing) overflowing=$LBOVER(want 0) unit-count=$LBUNITS(want 9: dragged to 10 by mouse then nudged down one by the keyboard; 0 means the gauge took neither) crates=$LBCRATES(want 1) unit-count-pixels=$LBUD(want >=200; ZERO IS THE ROW NOT BEING DRAWN AT ALL, which is exactly how this shipped) crates-pixels=$LBCD(want >=20; zero is the same fault one column over)"
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
# 4b. AND THE ROTATION DID NOT RUN AWAY. Every other leg of this gate reads a POSITION,
#     so the whole family was blind to a piece that settled in the right place while
#     spinning like a top. That is not hypothetical: the tumble rate and the rolling
#     axis shared one triple, so a piece that rolled and then left the ground added a
#     unit vector to its euler every tick, about thirty times the intended tumble.
#     A rested piece's euler is bounded by flight alone (a few radians at the shipped
#     spin of 0.295); tens of radians means the axis is being integrated as a rate again.
SHSPIN=$(echo "$SHLOG" | python3 -c '
import sys, re
bad = 0
for ln in sys.stdin:
    if not ln.startswith("SHATTER|rest|"): continue
    m = re.search(r"\|euler=([-0-9.]+),([-0-9.]+),([-0-9.]+)\|", ln)
    if not m: continue
    if max(abs(float(m.group(i))) for i in (1, 2, 3)) > 10.0: bad += 1
print(bad)
')
# 5. THE POOL EMPTIES. A piece that never rests never fades and never leaves.
SHLIVE=$(echo "$SHLOG" | grep -c '^SHATTERDUMP|live=0|')
if [ "$SHRC" != "0" ]; then
  bad "G91 building shatter: the run failed (exit $SHRC)"
elif [ "${SHDIED:-0}" -ge 1 ] && [ "${SHNUKE:-0}" -ge 1 ] && [ "${SHRESTN:-0}" -ge 9 ] \
     && [ "${SHRESTBAD:-1}" -eq 0 ] && [ "${SHTIMEOUT:-99}" -le 4 ] && [ "${SHROLLED:-0}" -ge 1 ] \
     && [ "${SHSPIN:-99}" -eq 0 ] \
     && [ "${SHLIVE:-0}" -ge 1 ]; then
  ok "G91 building shatter: a Power Plant came apart into 9 pieces / 132 triangles off the pack's own section table, $SHRESTN pieces settled with only $SHTIMEOUT on the backstop, $SHROLLED of them rolled a visible distance first, every one rests on the terrain at ground+underside within 0.01 cells, and the pool emptied"
else
  bad "G91 building shatter: died=$SHDIED(want >=1) pieces9=$SHNUKE(want 1) rested=$SHRESTN(want >=9) off-ground=$SHRESTBAD(want 0) on-backstop=$SHTIMEOUT(want <=4; all of them means the elasticity regressed and nothing ever reaches the roll arm) rolled=$SHROLLED(want >=1) spun-out=$SHSPIN(want 0; nonzero means the roll axis is being integrated as a tumble rate again) pool-emptied=$SHLIVE(want 1). Zero deaths means the script killed nothing and every other leg is vacuous."
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
# 1. THE LEFT CLICK set it, and the brain stored it. Left and not right: the project owner asked for
#    right first, tried it, and changed his mind on 25 Aug. The gesture needs no rung of
#    its own -- once BuildingClass::What_Action answers ACTION_MOVE for a factory the
#    ordinary order path carries it.
RSET=$(echo "$RLOG" | grep -c '^ORDER|.*|cell=57,55|.*|MOVE$')
RGOT=$(echo "$RLOG" | grep -c '^RALLY|PYLE|id=[0-9]*|cell=57,55$')
# 2. AND RIGHT-CLICK DOES NOT. It deselects, which is its one meaning in this build, and
#    the rally set on the left button SURVIVES it unchanged. This leg is the regression
#    guard for the gesture the project owner asked to have taken off the right button: if anyone puts
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
  bad "G103 rally flag: selected=$RFSEL deselected=$RFDES flag=$RFDIFF(want >=300). Zero or near-zero means either the flag is not being drawn at all, or it is drawn in the cartridge's own RED instead of the house colour -- the texture is a solid (132,16,8) and this test counts gold, so a red flag scores nothing. A NEGATIVE number means it is drawn when nothing is selected, which is the other half of what the project owner asked for"
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
# throughout the whole period Nod had no map screen at all. the project owner, 26 Aug 2026:
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
# STDERR GOES TO ITS OWN FILE, and that is the whole reason this gate could not explain
# itself. camp_entry reports a missing reel with fprintf(stderr), NMISS below greps the
# flow log, and the flow log is STDOUT ONLY -- so "missing-reels" could never be anything
# but 0 and the one leg that names the cause was vacuous. It went red for real on 1 Sep
# 2026 against a game/campaign.pack twelve days stale, reported mapsel=0 missing-reels=0,
# and read as a renderer or flow regression until the stderr was looked at by hand.
NODERR=shots/flow105.err
./cnc3d --flowtest 20 --flowside nod --dylib ./TiberianDawn.dylib --dir ./missions/ \
      --content ./content/ --cameos cameos.pack --dospack dossidebar.pack \
      --dosinf dosinfantry.pack > "$NODFLOW" 2>"$NODERR" &
NP=$!; NW=0
while kill -0 $NP 2>/dev/null && [ $NW -lt 300 ]; do sleep 1; NW=$((NW+1)); done
kill $NP 2>/dev/null; wait $NP 2>/dev/null
NL=$(cat "$NODFLOW")
echo "$NL" >> "$OUT"
NMAP=$(echo "$NL" | grep -c "CAMPAIGN|mapsel|picked")
NSCORE=$(echo "$NL" | grep -c "CAMPAIGN|score|")
# The three reels the old pack was missing. camp_entry prints this line and then
# camp_mapsel returns 0, so its ABSENCE is the whole point of the check.
cat "$NODERR" >> "$OUT"
# No `|| echo 0` here: grep -c ALREADY prints 0 when it matches nothing, and only its
# EXIT STATUS is non-zero. The fallback would append a second line and make NMISS the
# two-line string "0\n0", which is not equal to "0", so the gate would fail on a pack
# that is perfectly good. There is no `set -e` in this script, so the exit status is
# harmless on its own.
NMISS=$(grep -cE "pack has no entry (EARTH_A|AFRICA|CLICK_A)" "$NODERR" 2>/dev/null)
NMISS=${NMISS:-0}
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
  bad "G107 build anywhere: the switch OPENED UP buildable ground (clear $BA_C0 -> $BA_C1). The cheat is only allowed to lift base adjacency; ground that will not take a foundation must still refuse one, which is the whole of what the project owner asked for."
elif [ "${BA_P1:-0}" -ge $(( ${BA_P0:-0} * 2 )) ] && [ "${BA_L1:-0}" -gt "${BA_L0:-0}" ]; then
  ok "G107 build anywhere: the switch opens the map up -- proximity cells $BA_P0 -> $BA_P1 and legal origins $BA_L0 -> $BA_L1 -- while buildable ground does not gain a single cell ($BA_C0 -> $BA_C1)"
else
  bad "G107 build anywhere: the switch changed nothing the player can see (prox $BA_P0 -> $BA_P1, legal $BA_L0 -> $BA_L1, both want a rise). The hook is not on the routine that fills PlacementCellInfo -- check DLLExportClass::Passes_Proximity_Check and DisplayClass::Passes_Proximity_Check, the FOUR argument overload."
fi

# ---------------------------------------------------------------------------
# G114  TOLD NO, AND TOLD WHY.
#
# THE FAULT. The placement overlay paints a refused cell red straight out of the engine's
# GenerallyClear flag. On a temperate map the commonest refusal is a TC01..TC05 tree
# clump, and the cartridge registers all five with model index -1 in every theater, so the
# console draws nothing there and neither do we: the player is told no over what looks
# like open grass and the reason lives only in a log line. There is no missing art. The
# fix is a caption, so this gate reads the WORD and not a picture -- what can be wrong is
# whether a cell is named and which word names it, and a pixel test passes on the wrong
# word in the right font.
#
# IT RIDES ON G107's RUN rather than booting the scenario again: gate_buildany.txt already
# enters placement mode on SCG01EC and dumps the grid twice, once inside the base ring and
# once with the whole map opened up, which is exactly the pair needed. If that script's
# two placemap calls ever move, this gate moves with them.
#
# WHY THE SECOND DUMP IS THE ONE ASSERTED. Without the cheat, whether a clump falls inside
# the buildable ring is a property of where the base happens to sit, not of the feature.
# With it every cell passes proximity, so every clump on the map is in the region. The
# cheat does not touch clear (G107 asserts that separately), so the clumps still refuse.
PB_N0=$(ba_field "$BA_OFF" noart)
PB_N1=$(ba_field "$BA_ON" noart)
PB_WORD=$(printf '%s' "$BA_ON" | sed -n 's/.*|noartname=\([^|]*\)$/\1/p')
if [ "$BARC" != "0" ] || [ -z "$BA_ON" ]; then
  bad "G114 refusal captions: G107's run did not produce the two PLACEMAP lines this reads"
elif [ -z "$PB_N1" ]; then
  bad "G114 refusal captions: the placement dump carries no noart= field, so sb_placemap was never taught to count refusals the player cannot see a reason for"
elif [ "${PB_N1:-0}" -lt 1 ]; then
  bad "G114 refusal captions: not one refused cell in the whole buildable region could be named (noart=$PB_N1). With build-anywhere on every cell passes proximity, and SCG01EC's [TERRAIN] section places 19 tree clumps covering 38 cells inside the map rect, so zero means SbHooks::CellBlocker is unwired or place_blocker_name no longer matches the ClumpDef footprints."
elif [ "$PB_WORD" != "Tree" ]; then
  bad "G114 refusal captions: the caption reads '$PB_WORD' where the game's own word is 'Tree' -- tdata.cpp gives all five Clump classes TXT_TREE, conquer.h defines TXT_TREE as string 38, and CONQUER.ENG string 38 is 'Tree'"
elif [ "${PB_N0:-0}" -gt "${PB_N1:-0}" ]; then
  bad "G114 refusal captions: opening the whole map to placement LOST named refusals ($PB_N0 -> $PB_N1). Proximity can only grow and a clump cell is impassable terrain no unit can move on or off, so the count is being read off the wrong grid."
else
  ok "G114 refusal captions: $PB_N1 cells in the buildable region refuse a foundation with no pixels drawn on them, and the overlay names the first of them 'Tree' -- the game's own word for TC01..TC05, which the cartridge registers with model index -1 in every theater (inside the base ring alone: $PB_N0)"
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
# COUNTED OVER THE played LINES ONLY, because it is compared for equality against
# SS_PLAYED. The per-tick duplicate rule can refuse a third copy of one clip raised inside
# one tick, and such a line is still a ROCKET2 fired by a SAM at a building coordinate;
# counting it on one side of that comparison and not the other would turn the gate red for
# a reason that has nothing to do with who fired.
SS_BLDG=$(echo "$SSLOG" | sed -n 's/^SOUND|sfx|59|ROCKET2|var=[0-9]*|px=\([0-9]*\),\([0-9]*\)|.*|played$/\1 \2/p' \
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

# ---------------------------------------------------------------------------
# G110  THE RADAR TAKES ORDERS ON THE LEFT BUTTON AND NAVIGATES ON THE RIGHT.
# See gate_mmorder.txt for the button split, for why the target cell is 52,50, and for
# the DOS radar's cropping limit, which this does not close.
#
# RUN TWICE, ONE RUN PER HUD. minimap_layout branches on g_hudNew and the two radars are
# different rectangles at different zooms: the DOS bar plots at a whole-number cell zoom
# inside a 72x69 hole, the 640x480 HUD at a fractional zoom inside 136x120. A coordinate
# transform that is right on one of them can be wrong on the other, and both are real --
# cnc_eyes draws the DOS bar unless CNC3D_HUD says otherwise, and the app ships the other.
#
# THE CAMERA READING IS THE ASSERTION, not the order line on its own. The report is that
# a radar click only ever jumps the camera, so the proof that the click became an order
# is that the camera DID NOT MOVE while an order went out. The two jump legs on either
# side of it prove the camera can still be driven from the radar at all, which is what a
# fix that simply broke the radar would fail.
MMB="--scen SCG90EA --pack SCG01EA.pack $BASE --noshroud --forceradar --w 1280 --h 960"
MMLOG=$(./cnc_eyes $MMB --script "$GATEDIR/gate_mmorder.txt" 2>&1)
MMRC=$?
export CNC3D_HUD=new
MMLOG6=$(./cnc_eyes $MMB --script "$GATEDIR/gate_mmorder.txt" 2>&1)
MMRC6=$?
unset CNC3D_HUD
echo "$MMLOG"  | grep -E '^(MMCLICK|MINIMAP|CAM\|cam|EXPECTSEL|EXPECTCELL|PLAYER)' >> "$OUT"
echo "$MMLOG6" | grep -E '^(MMCLICK|MINIMAP|CAM\|cam|EXPECTSEL|EXPECTCELL|PLAYER)' >> "$OUT"
# One reader, one verdict per HUD. The six CAM|cam lines are, in order:
#   1 parked  2 after the no-selection LEFT press  3 parked
#   4 after the ORDER  5 parked  6 after the RIGHT press
mm_check() {
  ge_log="$1"; ge_name="$2"; ge_rc="$3"
  ge_cam=$(echo "$ge_log" | grep '^CAM|cam|')
  ge_c1=$(echo "$ge_cam" | sed -n '1p'); ge_c2=$(echo "$ge_cam" | sed -n '2p')
  ge_c3=$(echo "$ge_cam" | sed -n '3p'); ge_c4=$(echo "$ge_cam" | sed -n '4p')
  ge_c5=$(echo "$ge_cam" | sed -n '5p'); ge_c6=$(echo "$ge_cam" | sed -n '6p')
  ge_n=$(echo "$ge_cam" | grep -c '^CAM')
  ge_ord=$(echo "$ge_log" | grep -c '^MINIMAP|order|cell=52,50|.*|MOVE')
  ge_arr=$(echo "$ge_log" | grep -c '^EXPECTCELL|MTNK#[0-9]*|want=52,50|have=52,50|PASS$')
  ge_miss=$(echo "$ge_log" | grep -c '^MMCLICK|MISS')
  ge_bad=$(echo "$ge_log" | grep -c '|FAIL$')
  if [ "$ge_rc" != "0" ]; then
    bad "G110 radar orders ($ge_name): the run failed (exit $ge_rc)"
  elif [ "${ge_miss:-1}" != "0" ]; then
    bad "G110 radar orders ($ge_name): $ge_miss of the three presses missed the radar rectangle -- minimapcell's cell-to-pixel arithmetic and minimap_hit's rectangle disagree on this HUD"
  elif [ -z "$ge_c6" ]; then
    bad "G110 radar orders ($ge_name): only $ge_n of the 6 expected CAM|cam readings came back, so the script did not run to the end"
  elif [ "$ge_c3" != "$ge_c4" ]; then
    bad "G110 radar orders ($ge_name): the LEFT press with a tank selected MOVED THE CAMERA ($ge_c3 -> $ge_c4). That is the reported defect: the press is still a radar jump instead of an order."
  elif [ "${ge_ord:-0}" -lt 1 ]; then
    bad "G110 radar orders ($ge_name): the camera held still but no MINIMAP|order line for cell 52,50 said MOVE. A REFUSED line means the probe called that cell illegal and the cell in gate_mmorder.txt needs picking again; no line at all means minimap_order declined the press."
  elif [ "$ge_c1" = "$ge_c2" ]; then
    bad "G110 radar orders ($ge_name): with NOTHING selected the left press no longer jumps the camera (it stayed at $ge_c1) -- the order path is eating a press it should have declined"
  elif [ "$ge_c5" = "$ge_c6" ]; then
    bad "G110 radar orders ($ge_name): the RIGHT press did not move the camera (it stayed at $ge_c5) -- the navigate rung is no longer the first rung of ui_right_press and sb_click is swallowing it again"
  elif [ "$ge_c2" != "$ge_c6" ]; then
    bad "G110 radar orders ($ge_name): left-with-nothing-selected and right land the camera in DIFFERENT places ($ge_c2 vs $ge_c6) for the same radar cell"
  elif [ "${ge_arr:-0}" -lt 1 ]; then
    bad "G110 radar orders ($ge_name): the order went out and the Medium Tank did not arrive at 52,50 -- the EXPECTCELL line says where it stopped"
  elif [ "${ge_bad:-1}" != "0" ]; then
    bad "G110 radar orders ($ge_name): $ge_bad script assertions reported FAIL"
  else
    ok "G110 radar orders ($ge_name): a left press on the radar with a tank selected issues the move and leaves the camera exactly where it was, the tank arrives at 52,50, and both the right button and a left press with nothing selected still jump the camera to the same place"
  fi
}
mm_check "$MMLOG"  "DOS bar"     "$MMRC"
mm_check "$MMLOG6" "640x480 HUD" "$MMRC6"

# =====================================================================================
# G115 THE SELECT CURSOR MUST PROMISE WHAT THE CLICK DELIVERS, ENEMIES INCLUDED.
#
# ui_left_click routes a foreign object through the engine's own
# INPUT_REQUEST_SELECT_AT_POSITION door, so a plain click on an enemy building selects
# it. update_cursor asked selectable_by_player -- ownership plus a known type -- so the
# pointer stayed a plain arrow over the very thing the next click was about to select.
# The cursor was the last piece still refusing to say what would happen.
#
# NOTHING IS SELECTED AND NOTHING IS CLICKED. The arm under test is the g_selCount == 0
# one, and a click would leave it; `deselect` plus `expectsel 0` is what holds the world
# in that state. `look` frames each object first because cursorobj refuses an aim outside
# the frame, which is the right refusal.
#
# WHY SCG90EA. Its INI carries BadGuy HAND, AFLD and NUKE alongside the player's own
# GoodGuy PROC, so both halves of the rule are on one map.
#
# THE ANTI-VACUITY LEG IS THE LAST ONE. Cell 52,50 appears in no [TERRAIN], [OVERLAY],
# [STRUCTURES], [UNITS] or [INFANTRY] entry of SCG90EA.INI -- the same bare cell G110
# aims its order at -- so it must still read NORMAL. If the function were ever wedged on
# `select`, that leg goes red while everything else stays green.
cat > /tmp/g111.script <<'SCRIPT'
tick 30
deselect
expectsel 0
look HAND:BadGuy 0
cursorobj HAND:BadGuy 0
expectcursor select
look AFLD:BadGuy 0
cursorobj AFLD:BadGuy 0
expectcursor select
look NUKE:BadGuy 0
cursorobj NUKE:BadGuy 0
expectcursor select
look PROC:GoodGuy 0
cursorobj PROC:GoodGuy 0
expectcursor select
cam 52 50
cursorcell 52 50
expectcursor normal
quit
SCRIPT
SCLOG=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud --nosound \
          --w 1280 --h 800 --script /tmp/g111.script 2>&1)
SCRC=$?
echo "$SCLOG" | grep -E '^(CURSOROBJ|CURSORCELL|EXPECTSEL|EXPECTCURSOR|SCRIPT\|end)' >> "$OUT"
SCSEL=$(echo "$SCLOG"  | grep -c '^EXPECTCURSOR|want=select|have=select|PASS$')
SCNORM=$(echo "$SCLOG" | grep -c '^EXPECTCURSOR|want=normal|have=normal|PASS$')
SCBAD=$(echo "$SCLOG"  | grep -c '^EXPECTCURSOR|.*|FAIL$')
SCMISS=$(echo "$SCLOG" | grep -c '^CURSOROBJ|.*|MISSING')
if [ "${SCMISS:-0}" != "0" ]; then
  bad "G115 select cursor: $SCMISS of the probed objects were MISSING or not drawn, so nothing below means anything -- the type names in the script no longer match SCG90EA"
elif [ "$SCRC" != "0" ] && [ "${SCBAD:-0}" = "0" ]; then
  bad "G115 select cursor: the run failed (exit $SCRC) with no failed assertion, so it died before it finished"
elif [ "${SCSEL:-0}" -ge 4 ] && [ "${SCNORM:-0}" -ge 1 ] && [ "${SCBAD:-1}" = "0" ]; then
  ok "G115 select cursor: with nothing selected the pointer reads SELECT over three BadGuy structures and the player's own Refinery ($SCSEL of 4), and still reads NORMAL over bare ground at 52,50"
else
  bad "G115 select cursor: select-legs=$SCSEL(want 4) bare-ground-leg=$SCNORM(want 1) failed-assertions=$SCBAD(want 0). A 'normal' where 'select' was wanted over a BadGuy structure IS the reported defect: the click selects it and the pointer will not say so. A 'select' on the bare-ground leg means something walked onto 52,50 and that cell needs picking again"
fi

# G113 THE FOUR COMMAND SHORTCUTS, AND THE ONE THAT COULD LIE ABOUT ITSELF.
#
# X scatter, H jump to the Construction Yard, double-tap select-all-of-type, D rebuild
# last. Every verb the script uses is the function the key handler calls.
#
# LEG 1 IS WHY THIS GATE EXISTS. The scatter diagnostic is OURS, so a scatter accidentally
# wired to INPUT_UNIT_STOP -- which is what X used to be -- would still print the word and
# a gate that only grepped for it would stay green. NavCom is the engine's own answer and
# the two requests move it in opposite directions, so the middle reading must differ from
# the outer two and no printf can fake that.
#
# EVERY NUMBER BELOW IS RELATIONAL ON PURPOSE: a sign, two counts that must agree, two
# camera readings that must differ. The only absolutes are strings this build's own format
# strings produce and house=GoodGuy, which is SCG90EA.INI's [BASIC] Player line.
#
# No tee. The pipeline's exit status is tee's, not the game's.
SHLOG=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud --build 6 \
            --script gate_shortcuts.txt 2>&1)
SHRC=$?
echo "$SHLOG" >> "$OUT"
SH1=$(echo "$SHLOG" | sed -n '/^ECHO|G113-1/,/^ECHO|G113-2/p')
SH2=$(echo "$SHLOG" | sed -n '/^ECHO|G113-2/,/^ECHO|G113-3/p')
SH3=$(echo "$SHLOG" | sed -n '/^ECHO|G113-3/,/^ECHO|G113-4/p')
SH4=$(echo "$SHLOG" | sed -n '/^ECHO|G113-4/,$p')
SHNAV=$(echo "$SH1" | grep '^SEL|UNIT|MTNK|' | sed -n 's/.*|navcell=\(-*[0-9]*\)|.*/\1/p')
SHN1=$(echo "$SHNAV" | sed -n '1p'); SHN2=$(echo "$SHNAV" | sed -n '2p')
SHN3=$(echo "$SHNAV" | sed -n '3p')
SHTOOK=$(echo "$SH2" | sed -n 's/^SAMETYPE|MTNK|.*|took=\([0-9]*\)|.*/\1/p')
SHSEL=$(echo "$SH2" | grep -c '^SEL|')
SHMTNK=$(echo "$SH2" | grep -c '^SEL|UNIT|MTNK|')
SHBASE=$(echo "$SH3" | grep -c '^BASE|FACT|.*|house=GoodGuy|')
SHFACT=$(echo "$SH3" | grep -c '^SEL|BUILDING|FACT|')
SHSEL3=$(echo "$SH3" | grep -c '^SEL|')
SHCAM=$(echo "$SH3" | grep '^CAM|state|' | sed -n 's/^CAM|state|x=\([0-9.-]*\).*/\1/p' \
        | uniq | wc -l | tr -d ' ')
SHNONE=$(echo "$SH4" | grep -c '^REBUILD|none|')
SHBUSY=$(echo "$SH4" | grep -c '^REBUILD|NUKE|already building|')
SHSTART=$(echo "$SH4" | grep -c '^REBUILD|NUKE|start|')
SHCON=$(echo "$SH4" | grep -c '^SBITEM|[0-9]*|col=[0-9]*|NUKE|.*|constructing=1|')
SHCLEAN=$(echo "$SHLOG" | grep -c 'SCRIPT|end gate_shortcuts.txt: .*, 0 failures')
if [ "$SHRC" != "0" ]; then
  bad "G113 command shortcuts: the run itself failed (exit $SHRC)"
elif [ "$SHN1" = "-1" ] && [ -n "$SHN2" ] && [ "$SHN2" != "-1" ] && [ "$SHN3" = "-1" ] \
     && [ "${SHTOOK:-0}" -ge 2 ] && [ "$SHSEL" = "$SHMTNK" ] && [ "$SHSEL" = "$SHTOOK" ] \
     && [ "$SHBASE" = "1" ] && [ "$SHFACT" = "1" ] && [ "$SHSEL3" = "1" ] \
     && [ "$SHCAM" = "2" ] && [ "$SHNONE" = "1" ] && [ "$SHBUSY" = "1" ] \
     && [ "$SHSTART" = "1" ] && [ "$SHCON" = "1" ] && [ "$SHCLEAN" = "1" ]; then
  ok "G113 command shortcuts: X scatters (the tank's navcell goes -1 -> $SHN2) and S takes it back to -1, a double tap takes all $SHTOOK Medium Tanks in view and nothing else, H selects the player's OWN Construction Yard (one building, house=GoodGuy) and moves the camera onto it, and D repeats the last build -- refusing while it is still going, restarting it after a cancel"
else
  bad "G113 command shortcuts: navcell idle/scatter/stop=$SHN1/$SHN2/$SHN3 (want -1/not -1/-1; a middle -1 means X is still sending STOP) sametype-took=$SHTOOK(want >=2) selected=$SHSEL of which MTNK=$SHMTNK(want equal to each other and to took) base-line=$SHBASE(want 1, house=GoodGuy) FACT-selected=$SHFACT(want 1) total-selected=$SHSEL3(want 1) camera-moved=$SHCAM(want 2 distinct x) rebuild-empty=$SHNONE(want 1) rebuild-busy=$SHBUSY(want 1) rebuild-start=$SHSTART(want 1) NUKE-constructing=$SHCON(want 1) script-clean=$SHCLEAN(want 1)"
fi

# =====================================================================================
# G112 THE HEALTH-BAR SHOW RULE IS A DIAL, AND ALL THREE POSITIONS REACH THE GLASS.
#
# The rule used to be a compile-time 0/1 in game/shroud_mod.h, which is why one report
# says a damaged object's bar never goes away and another asks how to switch the bars off:
# neither behaviour was reachable from inside the game. It is now hb_mode on the F5 panel,
# read once by hb_should_show:
#     0  off                   nothing carries a bar
#     1  selected only         the vanilla DOS default (special.h:75 HB_SELECTED)
#     2  selected or damaged   what this renderer has always drawn, and the default
#
# THE SETUP HAS TO MANUFACTURE A DAMAGED, UNSELECTED OBJECT or positions 1 and 2 are
# indistinguishable and every arm below is vacuous. It borrows G93's arrangement -- a
# Medium Tank force-fires on the Power Plant on SCG90EA -- and then selects the TANK, so
# the damaged thing on screen is the one that is NOT selected.
#
# THREE RUNS OF ONE SCRIPT, not one run with three shots. Two shots taken inside a single
# tick are not a controlled pair: the wreck is smoking and the smoke moves between them.
# Run-to-run determinism (G4) is what makes the pictures comparable, so the script is
# emitted three times with only the dial value and the shot name differing.
#
# THE PIXEL ARMS ARE THE ANTI-VACUITY ONES. hbdump shares collect_health_bars with the
# draw pass and so cannot claim an undrawn bar, but it can be read correctly while the
# dial changes nothing a player sees, and only a picture tells those two apart. One
# changed pixel is a real finding here for the same reason it is in G93: the runs are
# deterministic and differ in one dial.
hb_script() {
  cat > gate_hbmode.txt <<TXT
tick 20
cam 52 51
zoom max
lclickobj MTNK 0
actclickobj NUKE 0 c
tick 150
lclickobj MTNK 0
actclickobj NUKE 0 c
tick 150
lclickobj MTNK 0
gfx hb_mode $1
hbdump
shot shots/g111_$1.png
quit
TXT
}
gbegin shots/g111_0.png shots/g111_1.png shots/g111_2.png
for hb_m in 2 1 0; do
  hb_script "$hb_m"
  grun "/tmp/g111_$hb_m.log" --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud \
       --script gate_hbmode.txt
done
gshots shots/g111_0.png shots/g111_1.png shots/g111_2.png
# The dial exists at all. A missing FX_PARAMS row prints GFX|unknown dial, and every
# measurement below then describes the default three times over, in green.
HBSET=$(cat /tmp/g111_2.log /tmp/g111_1.log /tmp/g111_0.log 2>/dev/null | grep -c '^GFX|hb_mode=')
HBC2=$(sed -n 's/^HBDUMP-BEGIN count=\([0-9]*\)$/\1/p' /tmp/g111_2.log 2>/dev/null | head -1)
HBC1=$(sed -n 's/^HBDUMP-BEGIN count=\([0-9]*\)$/\1/p' /tmp/g111_1.log 2>/dev/null | head -1)
HBC0=$(sed -n 's/^HBDUMP-BEGIN count=\([0-9]*\)$/\1/p' /tmp/g111_0.log 2>/dev/null | head -1)
HBU2=$(grep -c '^HB|.*|sel=0|' /tmp/g111_2.log 2>/dev/null)
HBU1=$(grep -c '^HB|.*|sel=0|' /tmp/g111_1.log 2>/dev/null)
HBS1=$(grep -c '^HB|.*|sel=1|' /tmp/g111_1.log 2>/dev/null)
HBPIX=$(python3 - <<'PY'
from PIL import Image
import numpy as np
def d(a, b):
    try:
        x = np.asarray(Image.open(a).convert('RGB'), int)
        y = np.asarray(Image.open(b).convert('RGB'), int)
        return int((np.abs(x - y).sum(2) > 0).sum())
    except Exception:
        return -1
print("%d %d %d" % (d('shots/g111_2.png', 'shots/g111_1.png'),
                    d('shots/g111_2.png', 'shots/g111_0.png'),
                    d('shots/g111_1.png', 'shots/g111_0.png')))
PY
)
set -- $HBPIX
HBP21="$1"; HBP20="$2"; HBP10="$3"
if [ "$GRC" != "0" ]; then
  bad "G112 health-bar show rule: a run failed or wrote no shot (GRC=$GRC)"
elif [ "${HBSET:-0}" != "3" ]; then
  bad "G112 health-bar show rule: the gfx verb accepted hb_mode $HBSET times of 3 -- there is no FX_PARAMS row named hb_mode, so the dial does not exist and nothing below means anything"
elif [ "${HBU2:-0}" -lt 1 ]; then
  bad "G112 health-bar show rule: at mode 2 the dump carries $HBU2 bars on UNSELECTED objects (want >=1). Nothing on screen is damaged-and-unselected, so modes 1 and 2 cannot be told apart -- the tank never damaged the Power Plant and this gate is vacuous"
elif [ "${HBC1:-0}" -lt 1 ] || [ "${HBU1:-1}" != "0" ] || [ "${HBS1:-0}" -lt 1 ]; then
  bad "G112 health-bar show rule: at mode 1 the dump carries $HBC1 bars, $HBS1 selected and $HBU1 unselected (want >=1, >=1 and 0). Selected-only is not selected-only"
elif [ "${HBC1:-0}" -ge "${HBC2:-0}" ]; then
  bad "G112 health-bar show rule: mode 1 drew $HBC1 bars and mode 2 drew $HBC2 (want fewer at 1). The dial is read but the damaged arm is not gated by it"
elif [ "${HBC0:-1}" != "0" ]; then
  bad "G112 health-bar show rule: mode 0 drew $HBC0 bars (want 0). Off is not off"
elif [ "${HBP21:-0}" -lt 1 ] || [ "${HBP20:-0}" -lt 1 ] || [ "${HBP10:-0}" -lt 1 ]; then
  bad "G112 health-bar show rule: the dump moved and the picture did not -- changed pixels 2v1=$HBP21 2v0=$HBP20 1v0=$HBP10 (want >=1 each; -1 means a shot could not be read). A dial only the dump obeys is not a dial"
else
  ok "G112 health-bar show rule: mode 2 draws $HBC2 bars including $HBU2 on unselected damaged objects, mode 1 draws $HBC1 and every one is selected, mode 0 draws none, and the three pictures differ pairwise (2v1=$HBP21 2v0=$HBP20 1v0=$HBP10) so all three positions reach the glass"
fi

# =====================================================================================
# G111. THE POWER PLANT'S COOLANT BUBBLES, and the emitter chains behind them.
#
# The damage record's +0x00 and +0x08 are the heads of two PARTICLE EMITTER CHAINS, and
# the same half-health test that swaps the damage overlay swaps the chain: healthy takes
# record +0x00 (lw a1,0x0(s0) at RAM 0x8004C824), damaged takes +0x08 (RAM 0x8004C818),
# one or the other and never both. The Power Plant's healthy chain is a single node at
# RAM 0x8009AAB0 -- ARGB (40,200,200,200), size 13 growing 1.3 a tick over a 15-tick life,
# offset (-85,134,-107) world units, fired on every frame where (tick & 1) == 0. Until
# this gate existed nothing read those chains at all and no building in the game emitted
# anything, which is a state a screenshot cannot distinguish from "the map has no plants".
#
# WHY THE LEGS ARE SHAPED LIKE THIS. A spawn counter on its own is green on a build where
# the particles never reach the glass, which is the exact failure G97 and G98 exist to
# prevent, so the load-bearing leg is a PIXEL A/B against --noemitters. It needs no
# threshold: two runs of one script are bit-identical (G4) and the only difference between
# the arms is the switch, so ONE changed pixel is a real finding and zero is a real
# failure. The state legs beside it say which rule produced those pixels.
rm -f shots/g111_h.png shots/g111_d.png shots/g111on_h.png shots/g111on_d.png
cat > /tmp/g111.txt <<'G111EOF'
# The camera and the damage ladder are gate_damage.txt's, for its reason: SCG90EA parks a
# Medium Tank beside a Power Plant, and five rounds of 105mm is what pushes that plant
# under half health without killing it.
cam 52 51
zoom max
tick 20
shot shots/g111_h.png
dmgdump
efxdump
lclickobj MTNK 0
actclickobj NUKE 0 c
tick 150
lclickobj MTNK 0
actclickobj NUKE 0 c
tick 150
lclickobj MTNK 0
actclickobj NUKE 0 c
tick 150
lclickobj MTNK 0
actclickobj NUKE 0 c
tick 150
lclickobj MTNK 0
actclickobj NUKE 0 c
tick 150
shot shots/g111_d.png
dmgdump
efxdump
quit
G111EOF
EMB="--scen SCG90EA --pack SCG01EA.pack $BASE --noshroud --nosound"
EMLOG=$(./cnc_eyes $EMB --script /tmp/g111.txt 2>&1)
EMRC=$?
echo "$EMLOG" | grep -E '^(EMITTAB|EMITART|DMGART\|NUKE)' >> "$OUT"
# A SECOND IDENTICAL RUN. The emitter claims to be a pure function of (building, table
# row, engine tick) with no clock and no rand() of its own -- worth testing, not asserting.
EMLOG2=$(./cnc_eyes $EMB --script /tmp/g111.txt 2>&1)
EMRC2=$?
# THE A/B ARM writes the same two file names, so keep the first run's pictures first.
cp -f shots/g111_h.png shots/g111on_h.png 2>/dev/null
cp -f shots/g111_d.png shots/g111on_d.png 2>/dev/null
EMOFF=$(./cnc_eyes $EMB --noemitters --script /tmp/g111.txt 2>&1)
EMRC3=$?
# 1. THE TABLE REACHED THE BINARY. Seven rows, and node=4 below pins the Power Plant to
#    its own row, so a truncated or reordered DMG_EMIT is red rather than quietly weaker.
EMROWS=$(echo "$EMLOG" | grep -c '^EMITTAB|rows=7|')
# 2. ANTI-VACUITY. The Power Plant's own row was seen live while the plant was healthy.
EMON=$(echo "$EMLOG" | grep -c '^EMITART|NUKE|id=1|node=4|ram=0x8009AAB0|mask=1|state=on|why=none$')
# 3. THE RULE, THE OTHER WAY ROUND. After the ladder the same row must read off/damaged --
#    and DMGART must agree that the ratio really crossed 0x80, or leg 3 is asserting a
#    switch that was never thrown.
EMOFFN=$(echo "$EMLOG" | grep -c '^EMITART|NUKE|id=1|node=4|.*|state=off|why=damaged$')
EMDMG=$(echo "$EMLOG" | grep -c '^DMGART|NUKE|id=1|.*|drawn=1$')
# 4. IT PUSHED PARTICLES, and the count grew between the two dumps.
EMS1=$(echo "$EMLOG" | sed -n 's/^EMITTAB|rows=[0-9]*|frame=[0-9]*|spawned=\([0-9]*\)|on=[01]$/\1/p' | head -1)
EMS2=$(echo "$EMLOG" | sed -n 's/^EMITTAB|rows=[0-9]*|frame=[0-9]*|spawned=\([0-9]*\)|on=[01]$/\1/p' | tail -1)
# 5. THE PARTICLE IS THE ROM'S OWN. A system-0 sprite carrying the template's exact colour
#    and alpha cap must be alive in the pool. No recipe source in EFX_SRC stores alpha 40,
#    so 200,200,200,40 belongs to a healthy building chain and to nothing else. This is the
#    leg that catches a transcription slip a pixel count would only blur.
EMFP=$(echo "$EMLOG" | grep -c '^EFXP|.*|sys=0|GLOW|chunk=0|.*|rgba=200,200,200,40$')
# 6. THE SWITCH IS REAL. With --noemitters nothing is spawned at all and the table says so.
EMOFFTAB=$(echo "$EMOFF" | grep -c '^EMITTAB|rows=7|frame=[0-9]*|spawned=0|on=0$')
# 7. THE PIXELS. The only leg that proves a bubble reached the DRAW rather than the pool.
#    No threshold, for the reason in the header. The damaged shot's count is reported but
#    NOT asserted: other plants elsewhere on the map are still emitting in the on-arm, so
#    zero there would be a claim about the whole map rather than about this building.
EMPIX=$(python3 - <<'PY3'
from PIL import Image
import numpy as np
def diff(a, b):
    try:
        x = np.asarray(Image.open(a).convert('RGB'), int)
        y = np.asarray(Image.open(b).convert('RGB'), int)
        return int((np.abs(x - y).sum(2) > 0).sum())
    except Exception:
        return -1
print("%d %d" % (diff('shots/g111on_h.png', 'shots/g111_h.png'),
                 diff('shots/g111on_d.png', 'shots/g111_d.png')))
PY3
)
EMHPIX=$(echo "$EMPIX" | cut -d" " -f1)
EMDPIX=$(echo "$EMPIX" | cut -d" " -f2)
# 8. DETERMINISM, on the state AND on the pool, for G97's reason.
EMSEQ1=$(echo "$EMLOG"  | grep -E '^(EMITTAB|EMITART)\|' | shasum | cut -c1-12)
EMSEQ2=$(echo "$EMLOG2" | grep -E '^(EMITTAB|EMITART)\|' | shasum | cut -c1-12)
EMPQ1=$(echo "$EMLOG"  | grep '^EFXP|' | shasum | cut -c1-12)
EMPQ2=$(echo "$EMLOG2" | grep '^EFXP|' | shasum | cut -c1-12)
if [ "$EMRC" != "0" ] || [ "$EMRC2" != "0" ] || [ "$EMRC3" != "0" ]; then
  bad "G111 building emitters: a run failed (exit $EMRC / $EMRC2 / $EMRC3)"
elif [ "${EMDMG:-0}" -lt 1 ]; then
  bad "G111 building emitters: the ladder never pushed the Power Plant under half health (DMGART drawn=1 sightings=$EMDMG, want >=1). Nothing below leg 2 means anything until that is fixed: either the tank stopped firing, SCG90EA's NUKE is no longer heap id 1, or five rounds is no longer enough. G93 uses the same ladder and would be red too."
elif [ "${EMROWS:-0}" -ge 2 ] && [ "${EMON:-0}" -ge 1 ] && [ "${EMOFFN:-0}" -ge 1 ] \
     && [ "${EMS2:-0}" -gt "${EMS1:--1}" ] && [ "${EMS1:-0}" -ge 1 ] \
     && [ "${EMFP:-0}" -ge 1 ] && [ "${EMOFFTAB:-0}" -ge 2 ] \
     && [ "${EMHPIX:--1}" -ge 1 ] \
     && [ "$EMSEQ1" = "$EMSEQ2" ] && [ "$EMPQ1" = "$EMPQ2" ]; then
  ok "G111 building emitters: the Power Plant's healthy chain at RAM 0x8009AAB0 fired while the plant was above half health and stopped the moment it fell below, $EMS1 -> $EMS2 particles pushed into the ordinary sprite pool, $EMFP of them alive carrying the template's own 200,200,200 at alpha cap 40, and they put $EMHPIX pixels on the glass that --noemitters does not (the damaged frame differs by $EMDPIX, the rest of the map's plants); identical across two runs (state digest $EMSEQ1, pool digest $EMPQ1)"
else
  bad "G111 building emitters: table-rows=$EMROWS(want >=2, one per dmgdump; zero means DMG_EMIT was truncated or EMITTAB moved) healthy-on=$EMON(want >=1; zero means the NUKE row never matched -- check node=4 is still the Power Plant's row and that SCG90EA's plant is heap id 1) damaged-off=$EMOFFN(want >=1; zero means the half-health switch did not stop the chain, which is the console's own exclusive-chain rule inverted or missing) spawned=$EMS1..$EMS2(want a rise from >=1; equal means the hook is reporting itself and pushing nothing) rom-colour=$EMFP(want >=1; zero with spawned>0 means the template transcription is wrong or the particles die before the dump) off-arm=$EMOFFTAB(want >=2; a shortfall means --noemitters does not reach the spawn) changed-px-healthy=$EMHPIX(want >=1; ZERO IS THE WHOLE BUG COMING BACK -- the chain spawned and nothing reached draw. First thing to check is the 256-per-cell reading of the offsets: at 1024 the bubble sits inside the plant and is depth-tested away) changed-px-damaged=$EMDPIX(reported, not asserted) digests=$EMSEQ1/$EMSEQ2 pool=$EMPQ1/$EMPQ2(want equal; a difference means something in the emitter reads a clock or a rand that is not the engine's)"
fi

# =====================================================================================
# G116. THE DAMAGED BUILDING'S SMOKE, and the sideways acceleration the pool grew for it.
#
# The other half of the damage record's two chains. Record +0x00 is the healthy chain G111
# covers; record +0x08 is this one, and the same Health_Ratio < 0x80 test picks between
# them, one arm or the other and never both. Twenty-four of the cartridge's twenty-five
# damaged nodes are wired, one arm per building. The twenty-fifth hangs off damage slot 22,
# a slot with no display list whose StructType nobody has identified, so it stays decoded
# and unwired: there is no ident for a row to key on.
#
# HOW THE FIRE MASK IS READ. The node's +0x04 is tested against the SHARED frame counter,
# so it does not gate a chain -- it staggers the NODES WITHIN one. The Power Plant's two
# damaged nodes carry masks 3 and 1: (frame & mask) == 0 fires the black plume every fourth
# frame and the pale puff every second, both together on frame 0 and neither on frames 1
# and 3. A mask of 0 fires every frame. That is why leg 2 pins the mask as well as the RAM
# address: a chain that fired all-or-nothing would still spawn and still draw.
#
# WHY THE LEGS ARE SHAPED LIKE THIS. G111's argument holds word for word -- a spawn counter
# is green on a build where nothing reaches the glass -- so the load-bearing leg is a pixel
# A/B. The arm it compares against is --nodmgsmoke and NOT --noemitters, because the master
# switch also silences every healthy chain on the map and the difference would then be a
# statement about the whole scene rather than about the plume in front of the camera. The
# master switch is the one early return at the head of the same step function, which G111's
# own off-arm already exercises.
#
# AND ONE LEG THAT IS NOT ABOUT PIXELS AT ALL. Nineteen of the twenty-five damaged nodes
# carry a HORIZONTAL acceleration, seventeen of them the same -0.1 in z, and the particle
# pool only ever integrated a vertical one. The Power Plant's black plume has jitter 0 and
# velocity (0, 8.1, 0), so its z velocity is EXACTLY zero on the tick it is born and can
# only go negative if the node's own accel z is applied every tick afterwards. Leg 7
# asserts that, and it is the one leg a stored-but-never-read number cannot pass.
rm -f shots/g116_h.png shots/g116_d.png shots/g116on_h.png shots/g116on_d.png
cat > /tmp/g116.txt <<'G116EOF'
# The camera and the damage ladder are gate_damage.txt's, the pair G111 uses for the same
# reason: SCG90EA parks a Medium Tank beside a Power Plant, and five rounds of 105mm is
# what pushes that plant under half health without killing it.
cam 52 51
zoom max
tick 20
shot shots/g116_h.png
dmgdump
efxdump
lclickobj MTNK 0
actclickobj NUKE 0 c
tick 150
lclickobj MTNK 0
actclickobj NUKE 0 c
tick 150
lclickobj MTNK 0
actclickobj NUKE 0 c
tick 150
lclickobj MTNK 0
actclickobj NUKE 0 c
tick 150
lclickobj MTNK 0
actclickobj NUKE 0 c
tick 150
shot shots/g116_d.png
dmgdump
efxdump
quit
G116EOF
SMB="--scen SCG90EA --pack SCG01EA.pack $BASE --noshroud --nosound"
SMLOG=$(./cnc_eyes $SMB --script /tmp/g116.txt 2>&1)
SMRC=$?
echo "$SMLOG" | grep -E '^(SMOKETAB|SMOKEART)\|' >> "$OUT"
# A SECOND IDENTICAL RUN, for G97's reason: the damaged arm claims to be a pure function of
# (building, table row, engine tick) with no clock and no rand() of its own.
SMLOG2=$(./cnc_eyes $SMB --script /tmp/g116.txt 2>&1)
SMRC2=$?
# THE A/B ARM writes the same two file names, so keep the first run's pictures first.
cp -f shots/g116_h.png shots/g116on_h.png 2>/dev/null
cp -f shots/g116_d.png shots/g116on_d.png 2>/dev/null
SMOFF=$(./cnc_eyes $SMB --nodmgsmoke --script /tmp/g116.txt 2>&1)
SMRC3=$?
# 1. THE TABLE REACHED THE BINARY. Twenty-four rows, and node=17 below pins the Power
#    Plant's black plume to its own row, so a truncated or reordered DMG_SMOKE is red
#    rather than quietly weaker.
SMROWS=$(echo "$SMLOG" | grep -c '^SMOKETAB|rows=24|')
# 2. THE EXCLUSIVE RULE, THE WAY ROUND G111 CANNOT SEE. While the plant is above half
#    health its DAMAGED row must read off/healthy -- and the acceleration it is carrying is
#    asserted here, on the row, so a transcription slip in the one field this work added to
#    the pool is red before any pixel is counted.
SMHEAL=$(echo "$SMLOG" | grep -c '^SMOKEART|NUKE|id=1|node=17|ram=0x8009B184|mask=3|acc=0.000,0.500,-0.100|state=off|why=healthy$')
# 3. AND THE OTHER WAY. After the ladder the same row must read on/none, and DMGART must
#    agree that the ratio really crossed 0x80 or leg 3 asserts a switch never thrown.
SMON=$(echo "$SMLOG" | grep -c '^SMOKEART|NUKE|id=1|node=17|ram=0x8009B184|mask=3|acc=0.000,0.500,-0.100|state=on|why=none$')
SMDMG=$(echo "$SMLOG" | grep -c '^DMGART|NUKE|id=1|.*|drawn=1$')
# 4. IT PUSHED PARTICLES, and the count grew between the two dumps.
SMS1=$(echo "$SMLOG" | sed -n 's/^SMOKETAB|rows=[0-9]*|frame=[0-9]*|spawned=\([0-9]*\)|on=[01]$/\1/p' | head -1)
SMS2=$(echo "$SMLOG" | sed -n 's/^SMOKETAB|rows=[0-9]*|frame=[0-9]*|spawned=\([0-9]*\)|on=[01]$/\1/p' | tail -1)
# 5. THE PARTICLE IS THE ROM'S OWN. A system-0 sprite carrying the template's exact black
#    at full alpha must be alive in the ordinary sprite pool. Measured against all 65 rows
#    of EFX_SRC: no system-0 recipe source stores 0,0,0 at alpha 255 -- the sixteen Sam_*
#    and Gun_* are grey 150,150,150, Sparks is pale blue at 78, Dirt is orange-red -- so
#    this colour on this system belongs to a damaged building chain and to nothing else.
SMROM=$(echo "$SMLOG" | grep -c '^EFXP|.*|sys=0|GLOW|chunk=0|.*|rgba=0,0,0,255$')
# 6. THE OFF-ARM. With --nodmgsmoke nothing is spawned at all and the table says so, at
#    both dumps.
SMOFFTAB=$(echo "$SMOFF" | grep -c '^SMOKETAB|rows=24|frame=[0-9]*|spawned=0|on=0$')
# 7. THE HORIZONTAL ACCELERATION IS INTEGRATED, NOT MERELY STORED. That plume's node has
#    jitter 0 and velocity (0, 8.1, 0), so vx stays exactly 0.0000 for its whole life and
#    vz starts at exactly 0.0000. A NEGATIVE vz on a black system-0 sprite can therefore
#    only have come from the node's own accel z of -0.1 being applied every tick. This is
#    the leg that fails on a build where the pool carries ax/az and never reads them.
SMLEAN=$(echo "$SMLOG" | grep -cE '^EFXP\|[0-9a-f]+\|sys=0\|GLOW\|chunk=0\|p=[^|]*\|v=0\.0000,[0-9.]+,-0\.[0-9]*[1-9][0-9]*\|.*\|rgba=0,0,0,255$')
# 8. THE PIXELS, on the DAMAGED frame. The only leg that proves a plume reached the DRAW
#    rather than the pool. No threshold, for G111's reason: two runs of one script are
#    bit-identical (G4) and the only difference between these two arms is the switch, so
#    ONE changed pixel is a real finding and zero is a real failure. The HEALTHY frame's
#    count is reported and NOT asserted -- a non-zero there would be a claim about whether
#    anything else in view starts the mission under half health, which is a claim about the
#    map rather than about this building.
SMPIX=$(python3 - <<'PY6'
from PIL import Image
import numpy as np
def diff(a, b):
    try:
        x = np.asarray(Image.open(a).convert('RGB'), int)
        y = np.asarray(Image.open(b).convert('RGB'), int)
        return int((np.abs(x - y).sum(2) > 0).sum())
    except Exception:
        return -1
print("%d %d" % (diff('shots/g116on_h.png', 'shots/g116_h.png'),
                 diff('shots/g116on_d.png', 'shots/g116_d.png')))
PY6
)
SMHPIX=$(echo "$SMPIX" | cut -d" " -f1)
SMDPIX=$(echo "$SMPIX" | cut -d" " -f2)
# 9. DETERMINISM, on the state AND on the pool, for G97's reason.
SMSEQ1=$(echo "$SMLOG"  | grep -E '^(SMOKETAB|SMOKEART)\|' | shasum | cut -c1-12)
SMSEQ2=$(echo "$SMLOG2" | grep -E '^(SMOKETAB|SMOKEART)\|' | shasum | cut -c1-12)
SMPQ1=$(echo "$SMLOG"  | grep '^EFXP|' | shasum | cut -c1-12)
SMPQ2=$(echo "$SMLOG2" | grep '^EFXP|' | shasum | cut -c1-12)
if [ "$SMRC" != "0" ] || [ "$SMRC2" != "0" ] || [ "$SMRC3" != "0" ]; then
  bad "G116 damage smoke: a run failed (exit $SMRC / $SMRC2 / $SMRC3)"
elif [ "${SMDMG:-0}" -lt 1 ]; then
  bad "G116 damage smoke: the ladder never pushed the Power Plant under half health (DMGART drawn=1 sightings=$SMDMG, want >=1). Nothing below leg 3 means anything until that is fixed: either the tank stopped firing, SCG90EA's NUKE is no longer heap id 1, or five rounds is no longer enough. G93 and G111 use the same ladder and would be red too."
elif [ "${SMROWS:-0}" -ge 2 ] && [ "${SMHEAL:-0}" -ge 1 ] && [ "${SMON:-0}" -ge 1 ] \
     && [ "${SMS2:-0}" -gt "${SMS1:--1}" ] && [ "${SMS2:-0}" -ge 1 ] \
     && [ "${SMROM:-0}" -ge 1 ] && [ "${SMOFFTAB:-0}" -ge 2 ] \
     && [ "${SMLEAN:-0}" -ge 1 ] && [ "${SMDPIX:--1}" -ge 1 ] \
     && [ "$SMSEQ1" = "$SMSEQ2" ] && [ "$SMPQ1" = "$SMPQ2" ]; then
  ok "G116 damage smoke: the Power Plant's damaged chain at RAM 0x8009B184 stayed silent while the plant was whole and took over the moment it fell under half health, $SMS1 -> $SMS2 particles pushed into the ordinary sprite pool, $SMROM of them alive carrying the template's own 0,0,0 at alpha 255, $SMLEAN of those leaning off the vertical on an integrated accel z of -0.1 they were born with none of, and they put $SMDPIX pixels on the glass that --nodmgsmoke does not (the healthy frame differs by $SMHPIX); identical across two runs (state digest $SMSEQ1, pool digest $SMPQ1)"
else
  bad "G116 damage smoke: table-rows=$SMROWS(want >=2, one per dmgdump; zero means DMG_SMOKE was truncated or SMOKETAB moved) healthy-arm-quiet=$SMHEAL(want >=1; zero means the NUKE damaged row never read off/healthy -- check node=17 is still the Power Plant's black plume and that its acc= still reads 0.000,0.500,-0.100, because a changed acc there is a transcription slip, not a switch) damaged-arm-live=$SMON(want >=1; zero means the half-health switch never handed over, which is the console's exclusive-chain rule missing or inverted) spawned=$SMS1..$SMS2(want a rise to >=1; equal means the hook is reporting itself and pushing nothing) rom-colour=$SMROM(want >=1; zero with spawned>0 means the template transcription is wrong or the particles die before the dump) off-arm=$SMOFFTAB(want >=2; a shortfall means --nodmgsmoke does not reach the spawn) integrated-lean=$SMLEAN(want >=1; ZERO WITH rom-colour>0 IS THE PRECISE FAILURE THIS GATE EXISTS FOR -- the node's ax/az reached the particle and the integrator is still only adding ay, so nineteen of the twenty-five damaged plumes rise dead straight) changed-px-damaged=$SMDPIX(want >=1; ZERO IS THE WHOLE BUG COMING BACK -- the chain spawned and nothing reached draw. Unlike the coolant bubbles these plumes CLIMB about 1.5 cells over their life against a plant 0.83 cells tall, so a zero here is not the 256-versus-1024 offset reading that caught the healthy arm; look at the shroud cull and the zero-extent cull first) changed-px-healthy=$SMHPIX(reported, not asserted; non-zero means something else in view starts the mission under half health) digests=$SMSEQ1/$SMSEQ2 pool=$SMPQ1/$SMPQ2(want equal; a difference means something in the damaged arm reads a clock or a rand that is not the engine's)"
fi

# =====================================================================================
# G117 THE POINTER CAGE REFUSES EVERY AUTOMATED RUN.
#
# --confine keeps the pointer inside a genuinely fullscreen window on a multi-display
# desktop, so a flick of the wrist at the 12-px edge strip cannot land on the neighbouring
# screen before the map has scrolled. A grab that is entered and not released traps the
# PHYSICAL pointer of whoever is at the keyboard, and this suite opens a real visible
# window twice -- the autoesc run above and G37's nine seconds of autoplay -- so the
# decision has to be provably closed to every automated entry point before the feature is
# allowed to exist at all. That is the reason this gate is written before anyone has
# played with the feature, rather than after.
#
# WHAT IT CAN AND CANNOT SEE. It walks the DECISION TABLE headlessly: the one combination
# that may cage the pointer, the eight one-fact changes that must refuse, and every
# harness flag the two binaries have, each set on its own, against the predicate that
# answers "something other than a hand is driving this run". It does NOT prove that the OS
# then holds the pointer inside the window. That is a property of a real pointer on a real
# two-display desktop, no headless run can observe it, and it stays recorded as untested.
#
# THE CASE COUNT IS ASSERTED, and that is the anti-vacuity belt: a table that quietly
# stopped covering a veto would otherwise pass by covering nothing at all. 21 is nine
# decision rows, plus eleven automated flags one at a time, plus the bare run that must
# NOT read as automated. Changing that number is a deliberate edit to this gate.
#
# PROVEN TO FAIL: delete the g_autoplay term from confine_automated and this reports
# "--autoplay reads automated=0, want 1" with failures=1; delete the fullscreen refusal
# and the "windowed" row reports got=1 want=0.
CONFL=$(./cnc_eyes --confinetest 2>&1)
CONFRC=$?
echo "$CONFL" >> "$OUT"
CONFC=$(echo "$CONFL" | sed -n 's/^CONFINETEST|cases=\([0-9]*\)|failures=[0-9]*/\1/p' | head -1)
CONFF=$(echo "$CONFL" | sed -n 's/^CONFINETEST|cases=[0-9]*|failures=\([0-9]*\)/\1/p' | head -1)
if [ "$CONFRC" = "0" ] && [ "${CONFC:-0}" = "21" ] && [ "${CONFF:-1}" = "0" ]; then
  ok "G117 pointer cage: all $CONFC decision cases pass -- it opens only for a player who asked, on two or more displays, fullscreen, focused, outside the editor and outside the pause dialog, and each of the eleven automated entry points refuses it on its own"
else
  bad "G117 pointer cage: exit=$CONFRC cases=$CONFC(want 21; a different number means the table gained or lost a case without this gate being told) failures=$CONFF(want 0)"
fi

# G118 THE ELEVATION MODEL, AND THE HEIGHTMAP IMPORTER THAT RIDES ON IT.
#
# --elevtest has existed for a while and NOTHING RAN IT, which is the same as not having
# it: a self-check nobody runs is a comment. It re-measures natively what the browser
# measured once, that a 32x32 tier field can stand in for a 65x65 heightmap without the
# map visibly changing, and it now also carries the PNG and PCX importer's own proof.
#
# IT NEEDS --shot TO BE REACHED. The editor's headless self-checks all sit behind the
# same block as the screenshot path, so --elevtest on its own opens the editor and waits.
# That is why this passes a shot it then throws away, and it is worth stating rather than
# leaving as a mystery for whoever copies this line.
#
# WHAT IT ASSERTS, beyond its own PASSED line: that the derivation is stable, that raising
# a block moves the ground and undo puts it back exactly, that a sheer face imports as a
# terrace, that RE-IMPORTING the map's own ground is a no-op, that a whole-map import is
# ONE undo step, and that an unsupported PCX is refused with a reason a person can act on.
# The importer has no UI route yet, so this is the only thing exercising it.
rm -f /tmp/g118_elev.png
G118LOG=$(./cnc_eyes --scen SCG01EA --pack SCG01EA.pack $BASE --elevtest \
              --shot /tmp/g118_elev.png 2>&1 | tee -a "$OUT")
G118RC=$?
G118P=$(echo "$G118LOG" | grep -c "^ELEVTEST|PASSED|0 failures")
G118OK=$(echo "$G118LOG" | grep -c "^ELEVTEST|ok  |")
G118IMP=$(echo "$G118LOG" | grep -c "^ELEVTEST|ok  |a whole-map import is ONE undo step")
if [ "$G118RC" != "0" ]; then
  bad "G118 elevation and import: the run itself failed (exit $G118RC)"
elif [ "${G118P:-0}" -ge 1 ] && [ "${G118OK:-0}" -ge 8 ] && [ "${G118IMP:-0}" -ge 1 ]; then
  ok "G118 elevation and import: $G118OK checks pass with 0 failures, the importer terraces a sheer face, re-importing the map's own ground changes nothing, and a whole-map import is a single undo step"
else
  bad "G118 elevation and import: passed-line=$G118P(want 1) ok-checks=$G118OK(want >=8) one-undo=$G118IMP(want 1)"
fi

# =====================================================================================
# G119 ATTACK-MOVE, AND WHAT THE A KEY ACTUALLY SENDS.
#
# THE POINT OF THE GATE. AMOVE| is OUR line, so a verb accidentally wired to a plain move
# would still print the word. Every assertion that matters below is the ENGINE's own
# answer, read back out of CNC3D_Dump_Selection: MissionClass::Missions[MISSION_GUARD_AREA]
# is the string "Area Guard" (mission.cpp:499-503) and no printf on this side produces it.
#
# WHY GUARD AREA IS THE RIGHT ORDER, measured rather than assumed. A guard-area order
# writes the clicked cell into BOTH ArchiveTarget and the destination (event.cpp:601-619),
# so the unit DRIVES there; foot.cpp:1013-1021 scans and chases from there; and
# foot.cpp:1007-1011 sends it back to that same archive cell when the fight is over. The
# travel, the fight and the resume are the engine's, which is why this feature carries no
# per-unit state and re-issues nothing.
#
# LEG 4 COSTS NOTHING AND PROVES THE MOST. A Harvester has no primary weapon, so
# Can_Player_Fire() is false (techno.cpp:2806-2812) and What_Action(CELL) never reaches its
# guard-area arm for it (techno.cpp:2728-2731). The SAME click that sets a tank to Area
# Guard must therefore set a harvester to Move, per object, with no special case on this
# side. A build that special-cased it by name would read identically on the tank and
# differently here.
#
# THE CELLS were read out of SCG90EA.INI (DOS cell numbering, stride 64) and are open
# ground west of the GDI base. Nothing here asserts an ARRIVAL, so a destination that turns
# out to be occupied does not invalidate the gate: ACTION_GUARD_AREA is decided before any
# Can_Enter_Cell test.
#
# WHY LEG 4 WAITS, and it is the engine's clock rather than a fudge. UnitClass::AI only
# calls Commence() when the vehicle is NOT driving (unit.cpp:323-325, 398-400), so an
# assigned MISSION_MOVE sits in MissionQueue until the unit next reaches a cell centre.
# The DESTINATION lands on the same tick as the click -- the first dump already carries
# navcell -- but the MISSION NAME follows only when the wheels stop, and both harvesters
# on this map start the scenario already driving to tiberium. Measured here: mission
# reads Move from about 65 ticks after the click to about 265, so the second dump sits in
# the middle of a 200-tick window rather than on an edge.
#
# WHY LEG 6 PICKS THE TARGET IT DOES, also measured rather than chosen for looks.
# --noshroud is a RENDERER switch: the engine's own shroud is untouched, and
# Selection_At_Mouse / Command_Object test Map[cell].Is_Visible(PlayerPtr) BEFORE looking
# for an object (display.cpp:3499-3500 and 3575-3576), so a click on an unexplored enemy
# is degraded to a plain move by the ENGINE and no front end can turn it back into an
# attack. Only Debug_Unshroud, the Fog Of War cheat, is exempt there, and a gate that
# needed a cheat to see its own subject would be measuring the cheat. Every Nod
# vehicle on this map sits at row 38 under shroud the player has never lifted; the two
# Nod riflemen at rows 40-41 stand inside the GDI base's own sight and read visible=1 on
# tick 20. So the enemy here is one of THOSE, named by heap id so the pick cannot drift.
# The attacker is a Medium Tank that has been given nothing yet, for the same reason the
# leg dumps it BEFORE arming: a tank already mid-order is still driving, its queued
# MISSION_ATTACK cannot commence, and a tank that had acquired a target on its own would
# print the very line this leg wants to read.
cat > /tmp/g119.txt <<'G119EOF'
# G119, through the same calls the keys and the mouse make. `amove` is am_toggle, the
# function case SDLK_a calls; `actcell` is the whole left button aimed at a cell.
tick 20
cam 54 51
zoom min
echo G119-1 A arms and the pointer says what the click will do
lclickobj MTNK 0
expectsel 1
amove
cursorcell 46 48
expectcursor attack
echo G119-2 the click sends the engine its own guard-area order
actcell 46 48
tick 3
seldump
echo G119-3 the same cell without the verb is a plain move, which is the whole contrast
stop
tick 3
actcell 47 48
tick 3
seldump
echo G119-4 a unit with no weapon is MOVED by the identical click
deselect
lclickobj HARV 0
expectsel 1
amove
actcell 45 48
tick 3
seldump
tick 100
seldump
echo G119-5 the arm is one shot and the right button drops it
deselect
lclickobj MTNK 0
amove
rbutton 400 300
echo G119-6 A on an enemy is an ordinary attack order
deselect
lclickobj MTNK:GoodGuy#5 0
expectsel 1
seldump
cam 51 44
amove
actclickobj E1:BadGuy#12 0
tick 3
seldump
quit
G119EOF
AMLOG=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud --nosound \
            --script /tmp/g119.txt 2>&1)
AMRC=$?
echo "$AMLOG" | grep -E '^(ECHO|AMOVE|QDUMP|QUEUE|SEL|EXPECTCURSOR|EXPECTSEL|ACTCELL)\|' >> "$OUT"
AM1=$(echo "$AMLOG" | sed -n '/^ECHO|G119-1/,/^ECHO|G119-2/p')
AM2=$(echo "$AMLOG" | sed -n '/^ECHO|G119-2/,/^ECHO|G119-3/p')
AM3=$(echo "$AMLOG" | sed -n '/^ECHO|G119-3/,/^ECHO|G119-4/p')
AM4=$(echo "$AMLOG" | sed -n '/^ECHO|G119-4/,/^ECHO|G119-5/p')
AM5=$(echo "$AMLOG" | sed -n '/^ECHO|G119-5/,/^ECHO|G119-6/p')
AM6=$(echo "$AMLOG" | sed -n '/^ECHO|G119-6/,$p')
AMARM=$(echo "$AM1" | grep -c '^AMOVE|armed=1|armed|')
AMCUR=$(echo "$AM1" | grep -c '^EXPECTCURSOR|want=attack|have=attack|PASS$')
AMGA=$(echo "$AM2"  | grep -c '^SEL|UNIT|MTNK|.*|mission=Area Guard|navcell=[0-9]')
AMONE=$(echo "$AM2" | grep -c '^AMOVE|armed=0|used|')
AMMOVE=$(echo "$AM3" | grep -c '^SEL|UNIT|MTNK|.*|mission=Move|navcell=[0-9]')
AMHARV=$(echo "$AM4" | grep -c '^SEL|UNIT|HARV|.*|mission=Move|')
AMHGA=$(echo "$AM4"  | grep -c '^SEL|UNIT|HARV|.*|mission=Area Guard|')
AMDIS=$(echo "$AM5" | grep -c '^AMOVE|armed=0|right click|')
AMATK=$(echo "$AM6" | grep -c '^SEL|UNIT|MTNK|.*|mission=Attack|.*|tarcell=[0-9]')
# THE CONTROL FOR THAT ATTACK, without which the line above proves nothing. A tank that
# had acquired a target by itself prints the same mission=Attack, so leg 6 dumps the tank
# BEFORE the verb is armed and this asserts what that dump has to say: guarding, no
# destination, no target. The attack therefore came from the click and from nothing else.
AMIDLE=$(echo "$AM6" | grep -c '^SEL|UNIT|MTNK|.*|mission=Guard|navcell=-1|tarcell=-1$')
AMCLEAN=$(echo "$AMLOG" | grep -c 'SCRIPT|end /tmp/g119.txt: .*, 0 failures')
if [ "$AMRC" != "0" ]; then
  bad "G119 attack-move: the run itself failed (exit $AMRC)"
elif [ "${AMARM:-0}" -ge 1 ] && [ "${AMCUR:-0}" -ge 1 ] && [ "${AMGA:-0}" -ge 1 ] \
     && [ "${AMONE:-0}" -ge 1 ] && [ "${AMMOVE:-0}" -ge 1 ] && [ "${AMHARV:-0}" -ge 1 ] \
     && [ "${AMHGA:-0}" = "0" ] && [ "${AMDIS:-0}" -ge 1 ] && [ "${AMATK:-0}" -ge 1 ] \
     && [ "${AMIDLE:-0}" -ge 1 ] && [ "${AMCLEAN:-0}" = "1" ]; then
  ok "G119 attack-move: A arms and the pointer shows the ATTACK cursor, the click puts the Medium Tank on the engine's own MISSION_GUARD_AREA with a live NavCom, the same cell without the verb reads mission=Move, the identical click on a Harvester reads Move and never Area Guard because it carries no weapon, the arm is spent by one click and dropped by the right button, and A on a Nod rifleman the player can actually see turns an idle Medium Tank into an ordinary attack with a live TarCom"
else
  bad "G119 attack-move: armed=$AMARM(want >=1) attack-cursor=$AMCUR(want 1; the armed shape is the attack cursor now and G131 owns that rule) area-guard=$AMGA(want >=1; zero means the click is not sending ctrl+alt, or the tank never got the order -- check the cell is on the glass in the ACTCELL line) one-shot=$AMONE(want >=1) plain-move=$AMMOVE(want >=1; zero means the CONTRAST is gone and both clicks now do the same thing) harvester-move=$AMHARV(want >=1; the harvester keeps its old mission NAME until it next stops driving, so this reads the SECOND dump) harvester-areaguard=$AMHGA(want 0; NON-ZERO MEANS SOMETHING ON THIS SIDE IS FORCING GUARD-AREA ONTO A UNIT THE ENGINE REFUSES IT FOR) right-click-disarm=$AMDIS(want >=1) enemy-attack=$AMATK(want >=1; zero with a live tarcell in the log means the tank was still driving and could not commence the queued mission) idle-before=$AMIDLE(want >=1; zero means the tank was NOT idle before the verb, so the attack line above cannot be trusted) script-clean=$AMCLEAN(want 1)"
fi

# =====================================================================================
# G120 SHIFT QUEUES ORDERS, AND SHIFT STILL ADDS TO THE SELECTION.
#
# THE WHOLE RISK OF THIS FEATURE IS LEG 1. Shift was already the additive-select modifier,
# and the queue must not take it. The split is made on the engine's own Best_Object_Action:
# a shift click ADDS wherever the probe answers SELECT or TOGGLE_SELECT and queues only
# where it does not -- and where it does not, a shift click did nothing at all before this,
# because ui_left_click's additive arm clears nothing and can select nothing on bare
# ground. Leg 1 clicks through the FULL button path (actclickobj ... s), not the selection
# primitive, so it is evidence about the real gesture.
#
# LEG 6 IS THE MODE LEG. The identical script with Enhanced off must queue NOTHING. Both
# runs are the same file, so any difference between them is the mode and nothing else.
#
# THE QUEUE IS OURS BECAUSE THE ENGINE'S IS A STUB, checked twice:
# Units_Queued_Movement_Toggle is an empty Red-Alert-only body (dllinterface.cpp:6774-6778)
# and OptionsClass::KeyQueueMove1/2 = KN_Q (options.cpp:122-123) is read by nothing.
#
# THE DUMP VERB IS orderqdump AND NOT queuedump. The sidebar build queue had that name
# first, and its dispatcher is consulted BEFORE the one the order queue's verb lives in,
# so every queuedump in this script printed SBQUEUEDUMP and the order queue was never
# dumped at all. Pending-while-moving, arrival and the CLASSIC queue-is-off line all read
# zero because nothing was measuring them, and the "last seen at cell" tell came back
# empty for the same reason.
#
# THE TWO CELLS AND LEG 4'S TICK BUDGET ARE MEASURED, not read off the map. A Medium Tank
# here covers about one cell per 35 engine ticks. The pair first written into this gate
# asked for a five-cell hop and then a seven-cell hop inside a 160-tick leg 4, so the tank
# was still driving to the FIRST cell when the last dump was read -- handover and arrival
# were zero for want of time, not for want of a queue. Worse, that second cell is one no
# tank can stand on: ordered straight onto it the engine parks the tank a cell short and
# stops, so no budget would ever have made it green. 54,49 and 51,49 are open ground in
# the clear row north of the base; a tank driven onto each reads mission=Guard with a dead
# NavCom. Measured on this scenario, shift click to arrival is 208 ticks: the first hop's
# NavCom is clear by tick 130, the handover fires at tick 133, and the tank is standing on
# the second cell at tick 230. Leg 4's 150 + 200 covers that with room on both sides.
cat > /tmp/g120.txt <<'G120EOF'
tick 20
cam 54 51
zoom min
echo G120-1 shift on one of your own units still ADDS it and queues nothing
lclickobj MTNK 0
expectsel 1
actclickobj MTNK 1 s
expectsel 2
orderqdump
echo G120-2 a plain move then a shift click on the ground QUEUES the second
deselect
lclickobj MTNK 0
expectsel 1
actcell 54 49
tick 2
actcell 51 49 s
tick 2
orderqdump
echo G120-3 nothing fires while the first order is still running
tick 6
orderqdump
echo G120-4 the queue hands over on its own and the unit arrives at the second cell
tick 150
orderqdump
tick 200
orderqdump
echo G120-5 a queued order can be an attack-move
deselect
lclickobj MTNK 1
expectsel 1
amove
actcell 48 52 s
orderqdump
tick 40
seldump
quit
G120EOF
QB="--scen SCG90EA --pack SCG01EA.pack $BASE --noshroud --nosound"
QELOG=$(./cnc_eyes $QB --gfx --script /tmp/g120.txt 2>&1)
QERC=$?
QCLOG=$(./cnc_eyes $QB --script /tmp/g120.txt 2>&1)
QCRC=$?
echo "$QELOG" | grep -E '^(ECHO|QUEUE|QDUMP|SEL|EXPECTSEL|ACTCELL)\|' >> "$OUT"
echo "$QCLOG" | grep -E '^(ECHO|QUEUE|QDUMP)\|' >> "$OUT"
Q1=$(echo "$QELOG" | sed -n '/^ECHO|G120-1/,/^ECHO|G120-2/p')
Q2=$(echo "$QELOG" | sed -n '/^ECHO|G120-2/,/^ECHO|G120-3/p')
Q3=$(echo "$QELOG" | sed -n '/^ECHO|G120-3/,/^ECHO|G120-4/p')
Q4=$(echo "$QELOG" | sed -n '/^ECHO|G120-4/,/^ECHO|G120-5/p')
Q5=$(echo "$QELOG" | sed -n '/^ECHO|G120-5/,$p')
# 1. shift still ADDS, and queued nothing while doing it
QADD=$(echo "$Q1" | grep -c '^EXPECTSEL|want=2|have=2|PASS$')
QNOQ=$(echo "$Q1" | grep -c '^QUEUE|push|')
# 2. the shift click on ground queued exactly one order and issued none
QPUSH=$(echo "$Q2" | grep -c '^QUEUE|push|shift|kind=0|cell=51,49|target=-1|units=1|')
QPEND=$(echo "$Q2" | grep -c '^QDUMP|sel|MTNK|.*|mission=Move|.*|pending=1$')
QEARLY=$(echo "$Q2$Q3" | grep -c '^QUEUE|issue|')
# 3. it did hand over, once, to the cell that was queued
QISSUE=$(echo "$Q4" | grep -c '^QUEUE|issue|units=1|kind=0|cell=51,49|')
# 4. and the unit physically got there with no further click
QARR=$(echo "$Q4" | grep -c '^QDUMP|sel|MTNK|id=[0-9]*|cell=51,49|')
QCELL=$(echo "$Q4" | sed -n 's/^QDUMP|sel|MTNK|id=[0-9]*|cell=\([0-9]*,[0-9]*\)|.*/\1/p' | tail -1)
# 5. a queued attack-move dispatches as the engine's own guard-area mission
QAMP=$(echo "$Q5" | grep -c '^QUEUE|push|attack-move|kind=2|cell=48,52|')
QAMI=$(echo "$Q5" | grep -c '^QUEUE|issue|units=1|kind=2|cell=48,52|')
QAMM=$(echo "$Q5" | grep -c '^SEL|UNIT|MTNK|.*|mission=Area Guard|')
QECLEAN=$(echo "$QELOG" | grep -c 'SCRIPT|end /tmp/g120.txt: .*, 0 failures')
# 6. CLASSIC: the same script queues nothing at all and the second cell is never reached
QCPUSH=$(echo "$QCLOG" | grep -c '^QUEUE|')
QCOFF=$(echo "$QCLOG" | grep -c '^QDUMP|units=0|pending=0|armed=0|enhanced=0$')
QCARR=$(echo "$QCLOG" | grep -c '^QDUMP|sel|MTNK|id=[0-9]*|cell=51,49|')
QCCLEAN=$(echo "$QCLOG" | grep -c 'SCRIPT|end /tmp/g120.txt: .*, 0 failures')
if [ "$QERC" != "0" ] || [ "$QCRC" != "0" ]; then
  bad "G120 shift queue: a run failed (enhanced exit $QERC, classic exit $QCRC)"
elif [ "${QADD:-0}" = "1" ] && [ "${QNOQ:-0}" = "0" ] && [ "${QPUSH:-0}" = "1" ] \
     && [ "${QPEND:-0}" -ge 1 ] && [ "${QEARLY:-0}" = "0" ] && [ "${QISSUE:-0}" = "1" ] \
     && [ "${QARR:-0}" -ge 1 ] && [ "${QAMP:-0}" = "1" ] && [ "${QAMI:-0}" = "1" ] \
     && [ "${QAMM:-0}" -ge 1 ] && [ "${QECLEAN:-0}" = "1" ] \
     && [ "${QCPUSH:-0}" = "0" ] && [ "${QCOFF:-0}" -ge 1 ] && [ "${QCARR:-0}" = "0" ] \
     && [ "${QCCLEAN:-0}" = "1" ]; then
  ok "G120 shift queue: shift on one of your own tanks still adds it to the selection and queues nothing, a shift click on the ground appends instead of replacing (the running move is untouched and nothing fires while it lasts), the queue hands over by itself and the tank reaches 51,49 with no second click, a queued attack-move dispatches as the engine's own Area Guard, and the identical script in CLASSIC queues nothing at all and never reaches that cell"
else
  bad "G120 shift queue: shift-add=$QADD(want 1; ZERO IS THE ONE REGRESSION THIS GATE EXISTS FOR -- shift has stopped adding to the selection) queued-on-a-unit=$QNOQ(want 0; non-zero means an add-to-selection is being eaten by the queue) push=$QPUSH(want 1) pending-while-moving=$QPEND(want >=1) fired-early=$QEARLY(want 0; non-zero means AMQ_SETTLE or AMQ_IDLE is too small and the queue is emptying itself) handover=$QISSUE(want 1) arrived=$QARR(want >=1; last seen at cell $QCELL, wanted 51,49 -- if the tank stopped short the destination is occupied, pick another) amove-push=$QAMP(want 1) amove-issue=$QAMI(want 1) amove-mission=$QAMM(want >=1) enhanced-clean=$QECLEAN(want 1) classic-queue-lines=$QCPUSH(want 0; NON-ZERO MEANS THE QUEUE IS RUNNING IN CLASSIC) classic-off-line=$QCOFF(want >=1) classic-arrived=$QCARR(want 0) classic-clean=$QCCLEAN(want 1)"
fi

# =====================================================================================
# G121  SWAPPED MOUSE BUTTONS: an option, off out of the box, and one the script layer
# must not feel.
#
# WHY THE SECOND RUN IS THE IMPORTANT ONE. Every click verb in this suite -- lclick,
# lclickobj, rbutton, mmclick, order, band, sbclick -- calls the ui_ functions directly
# and never builds an SDL event, which is exactly why the swap was put at the event
# boundary and nowhere below it. If it ever slips below, "left" in a gate script stops
# meaning the physical left button and every click gate here becomes a function of a user
# setting. So the second run turns the option ON through the gfx verb and then asserts the
# verbs behave exactly as G65 and G110 say they do with it off. The gfx verb is also what
# proves the FX_PARAMS row exists at all: without it the line prints "unknown dial" and
# counts as a script failure.
#
# THE FIRST RUN is the dialog and the page. THE SWAP HAS MOVED to Options > Gameplay, so the
# Advanced page now HOLDS eleven elements and SHOWS eleven: the whole list fits its own well
# and there is no travel left to measure (G139 owns that). What this run still checks is that whatever IS on screen clears the
# OK button and does not overlap, which was arithmetic in a comment before ADVRECT existed. ADVRECT is that check, read by the
# same overlap walk G108 runs over the cheat page. optvis is called on the Advanced page
# exactly ONCE on purpose: a second visit would emit every rectangle twice and the walk
# would report each row overlapping its own duplicate.
rm -f /tmp/g121_page.txt /tmp/g121_verbs.txt
cat > /tmp/g121_page.txt <<'G121AEOF'
tick 30
options
optclick Visuals
optclick Advanced...
optvis
optclick OK
optclick OK
optclick Gameplay
optclick Swap mouse buttons
optclick OK
optvis
optclick Visuals
optclick Classic
optvis
optclick Enhanced
optvis
quit
G121AEOF
cat > /tmp/g121_verbs.txt <<'G121BEOF'
# The option ON, set through the gfx verb rather than the dialog so the sidebar is not
# switched out from under the radar half. Every gesture below is G65's or G110's own.
gfx enabled 1
gfx swap_buttons 1
tick 30
zoom max
# FRAME THE TANK FIRST, exactly as G110's own script does before its lclickobj. At
# `zoom max` on this map the Medium Tank sits outside the 1280x960 frame, lclickobj
# refuses an aim it cannot see, and the run ends six failures deep and exit 1 before a
# single swap assertion is read. `look` is a camera verb, not a click, so it moves
# nothing this gate measures.
look MTNK 0
lclickobj MTNK 0
expectsel 1
cancel
expectsel 0
lclickobj MTNK 0
expectsel 1
mmclick 52 50
tick 400
expectcell MTNK 0 52 50
expectsel 1
cam 57 50
mmclick 44 40 r
cam
quit
G121BEOF
SWPLOG=$(./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE --noshroud --nosound \
             --w 1280 --h 800 --gfx --script /tmp/g121_page.txt 2>&1)
SWPRC=$?
SWVLOG=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud --nosound \
             --forceradar --w 1280 --h 960 --script /tmp/g121_verbs.txt 2>&1)
SWVRC=$?
echo "$SWPLOG" | grep -E '^(OPTSWAP|ADVRECT)' >> "$OUT"
echo "$SWVLOG" | grep -E '^(GFX|EXPECTSEL|EXPECTCELL|CANCEL|MMCLICK|MINIMAP|CAM\|cam)' >> "$OUT"
# The four OPTSWAP readings, in order: fresh, clicked, under CLASSIC, back under ENHANCED.
sw() { echo "$SWPLOG" | grep '^OPTSWAP|' | sed -n "$1p"; }
SW1=$(sw 1); SW2=$(sw 2); SW3=$(sw 3); SW4=$(sw 4)
# No control on the Advanced page may share a pixel with another. G108's walk, verbatim.
SWOVER=$(echo "$SWPLOG" | awk -F'|' '/^ADVRECT\|/ {
    split($4,p,","); split($5,d,"x");
    n++; X[n]=p[1]; Y[n]=p[2]; W[n]=d[1]; H[n]=d[2];
  }
  END { c=0;
    for (i=1;i<=n;i++) for (j=i+1;j<=n;j++)
      if (X[i] < X[j]+W[j] && X[j] < X[i]+W[i] && Y[i] < Y[j]+H[j] && Y[j] < Y[i]+H[i]) c++;
    print c }')
SWROWS=$(echo "$SWPLOG" | grep -c '^ADVRECT|')
SWFAIL=$(echo "$SWVLOG" | sed -n 's/^SCRIPT|end .*: [0-9]* lines, \([0-9]*\) failures.*/\1/p' | head -1)
SWMISS=$(echo "$SWVLOG" | grep -c '^MMCLICK|MISS')
SWORD=$(echo "$SWVLOG" | grep -c '^MINIMAP|order|cell=52,50|.*|MOVE')
SWCAM=$(echo "$SWVLOG" | grep '^CAM|cam|')
SWC1=$(echo "$SWCAM" | sed -n '1p'); SWC2=$(echo "$SWCAM" | sed -n '2p')
if [ "$SWPRC" != "0" ] || [ "$SWVRC" != "0" ]; then
  bad "G121 swapped mouse buttons: a run failed (page exit $SWPRC, verbs exit $SWVRC)"
elif [ "${SWROWS:-0}" != "13" ]; then
  bad "G121 swapped mouse buttons: the Advanced page reported $SWROWS rectangles, want 13 (eleven checkbox rows in the well, OK, and the scroll bar -- the page now HOLDS eleven elements and SHOWS all of them). Fewer means the element never went in; more means optvis dumped that page twice and the overlap walk below is comparing rows with their own duplicates"
elif [ "${SWOVER:-99}" != "0" ]; then
  bad "G121 swapped mouse buttons: $SWOVER pairs of controls overlap on the Advanced page -- the twelfth row needs DOPT_A_STEP worked again against DOPT_GC_OK_Y, and until it is, OK is partly unclickable"
elif ! echo "$SW1" | grep -q '^OPTSWAP|swap=0|fx_swap=0|enhanced=1$'; then
  bad "G121 swapped mouse buttons: the box does not start CLEAR under ENHANCED [$SW1] -- off by default is the whole promise"
elif ! echo "$SW2" | grep -q '^OPTSWAP|swap=1|fx_swap=1|enhanced=1$'; then
  bad "G121 swapped mouse buttons: clicking the box did not move the dial behind it [$SW2]"
elif ! echo "$SW3" | grep -q '^OPTSWAP|swap=1|fx_swap=1|enhanced=0$'; then
  bad "G121 swapped mouse buttons: CLASSIC changed the binding [$SW3]. Classic governs the PICTURE; input settings are the player's and must survive it. This arm used to force the dial to 0, and because the Visuals screen writes the preset on the way out, that silently rewrote a customised swap_buttons on disc"
elif ! echo "$SW4" | grep -q '^OPTSWAP|swap=1|fx_swap=1|enhanced=1$'; then
  bad "G121 swapped mouse buttons: ENHANCED disturbed a binding it has no business touching [$SW4]"
elif [ "${SWFAIL:-1}" != "0" ]; then
  bad "G121 swapped mouse buttons: with the option ON the verb run reported $SWFAIL script failures. THE SWAP HAS REACHED THE SCRIPT LAYER: lclick or mmclick has stopped meaning the physical button it names, and every click gate in this suite is now a function of a user setting. It belongs at the SDL event boundary and nowhere below it"
elif [ "${SWMISS:-1}" != "0" ] || [ "${SWORD:-0}" -lt 1 ]; then
  bad "G121 swapped mouse buttons: with the option ON the radar verbs changed behaviour -- misses=$SWMISS(want 0) order lines for 52,50=$SWORD(want >=1)"
elif [ -z "$SWC2" ] || [ "$SWC1" = "$SWC2" ]; then
  bad "G121 swapped mouse buttons: with the option ON the RIGHT press on the radar stopped navigating (camera $SWC1 -> $SWC2)"
else
  ok "G121 swapped mouse buttons: the box starts clear on the Gameplay page, moves its dial, and is left ALONE by both CLASSIC and ENHANCED, no two controls on the Advanced page share a pixel, at whatever offset its scroll happens to be sitting, and with the option ON every script verb still names the physical button it always did"
fi

# ==================================================================================
# G122. THE TOOLTIP AND THE QUEUE BADGE ARE HALF THE SIZE, AND STILL ON WHOLE PIXELS.
#
# Both plates are rasterised in DOS pixels and blown up as their own quad, and both were
# drawn at twice the size they want. Measured at 1280x720 before the change: the 640 HUD
# put the 44x22 "Med. Tank" plate on screen at 88x44 beside a bar 160 wide and the 9x9
# queue badge at 18x18 on a 61x45 cameo; the DOS bar put the same two at 132x66 and 27x27
# on a 96x72 cameo.
#
# WHY THE MAGNIFICATION IS READ OUT AND NOT ONLY THE SIZE. This pair was corrected once
# already because the plate took a FRACTION of the old text screen's height while
# everything around it used a whole number, so it grew at a different rate at every window
# size. Half of a whole number is not always whole, and a gate that asked only for
# "smaller" would pass a plate sitting at 1.5x. So the step itself is asserted.
#
# WHY 2*half-full IS ALLOWED TO BE 1. On the 640 HUD a DOS pixel is two HUD pixels, so the
# half is exact at every window size. The DOS bar's zoom is 3 at 720 rows and an odd number
# has no half: the step is taken upwards, a little over half, because the step below is one
# screen pixel per DOS pixel and a 7-pixel digit is not an improvement.
#
# THE SUBJECT IS MTNK, AND THE ROW IS WHY. The DOS bar shows four rows per column, so
# only rows 0..3 of the unit column are on screen at this size: E1 is row 4 there,
# sb_item_rect refuses it, and no badge is drawn for it at all -- which is why an
# infantry subject measured nothing. A STRUCTURE is not the escape it looks like.
# Buildings are deliberately not queueable (a finished one sits in its factory until a
# human places it), so the queue verb REFUSES a structure and the badge is never even
# asked for, which is a mute zero rather than a wrong size. MTNK is the first entry of
# the unit column, so it is row 0 however the build list grows, and it is the cameo both
# cursor positions below already sit on -- the 44x22 "Med. Tank" plate this gate names.
# The queue is therefore asserted to have been ACCEPTED before its badge is measured,
# so a subject that cannot be queued reports itself instead of reading as an absence.
#
# BOTH HUDS, in two runs, because a plate that halves on one and not the other is the
# likely failure rather than a total absence. The size is pinned with --w/--h so the
# expected numbers below mean something.
cat > /tmp/g122.txt <<'EOF'
tick 30
build MTNK
queue MTNK 2
cursor 1200 370
shot /tmp/g122_a.png
sbplates
cursor 1230 230
shot /tmp/g122_b.png
sbplates
quit
EOF
rm -f /tmp/g122_a.png /tmp/g122_b.png
PLLOG=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud --w 1280 --h 720 \
            --script /tmp/g122.txt 2>&1)
PLRC=$?
echo "$PLLOG" >> "$OUT"
PLLOG6=$(CNC3D_HUD=new ./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud \
            --w 1280 --h 720 --script /tmp/g122.txt 2>&1)
PLRC6=$?
echo "$PLLOG6" >> "$OUT"
# One checker over either log: violations, plates actually measured, and the half step.
pl_check() {
python3 -c '
import sys, re
t = sys.stdin.read()
z = re.findall(r"^SBPLATE\|zoom\|full=(\d+)\|half=(\d+)\|whole=(\d+)\|hud=(\w+)$", t, re.M)
p = re.findall(r"^SBPLATE\|(tip|queue)\|dos=(\d+)x(\d+)\|zoom=(\d+)\|px=(\d+)x(\d+)$", t, re.M)
bad = 0 if z else 99
for full, half, whole, hud in z:
    full, half, whole = int(full), int(half), int(whole)
    if whole != 1: bad += 1                       # a fractional step is the guarded fault
    if 2 * half - full not in (0, 1): bad += 1    # halved, and never more than one step over
half = int(z[0][1]) if z else -1
seen = 0
for kind, dw, dh, zz, pw, ph in p:
    dw, dh, zz, pw, ph = int(dw), int(dh), int(zz), int(pw), int(ph)
    if dw == 0: continue                          # that frame drew no plate
    seen += 1
    if zz != half: bad += 1                       # a draw site still on the full zoom
    if pw != dw * zz or ph != dh * zz: bad += 1
print("%d %d %d" % (bad, seen, half))
'
}
set -- $(echo "$PLLOG"  | pl_check); PLBAD="$1";  PLSEEN="$2";  PLHALF="$3"
set -- $(echo "$PLLOG6" | pl_check); PLBAD6="$1"; PLSEEN6="$2"; PLHALF6="$3"
# The badge is the one plate whose DOS box is a constant: one digit, 9x9 whatever is queued.
PLQ=$(echo "$PLLOG"   | grep -c '^SBPLATE|queue|dos=9x9|zoom=2|px=18x18$')
PLQ6=$(echo "$PLLOG6" | grep -c '^SBPLATE|queue|dos=9x9|zoom=1|px=9x9$')
# The queue verb prints `add` only when it took the request and `refused` when the entry
# is not queueable, so this is the precondition every badge leg above rests on.
PLADD=$(echo "$PLLOG"   | grep -c '^SBQUEUE|add|MTNK|btype=4|bid=1|n=2$')
PLADD6=$(echo "$PLLOG6" | grep -c '^SBQUEUE|add|MTNK|btype=4|bid=1|n=2$')
if [ "$PLRC" != "0" ] || [ "$PLRC6" != "0" ]; then
  bad "G122 plate size: a run failed (exit $PLRC / $PLRC6)"
elif [ "${PLBAD:-1}" -eq 0 ] && [ "${PLBAD6:-1}" -eq 0 ] \
     && [ "${PLSEEN:-0}" -ge 1 ] && [ "${PLSEEN6:-0}" -ge 1 ] \
     && [ "${PLADD:-0}" -ge 1 ] && [ "${PLADD6:-0}" -ge 1 ] \
     && [ "${PLQ:-0}" -ge 1 ] && [ "${PLQ6:-0}" -ge 1 ]; then
  ok "G122 plate size: at 1280x720 the 9x9 queue badge draws at 9x9 on the 640 HUD and 18x18 on the DOS bar, half of the 18x18 and 27x27 it was; $PLSEEN6 and $PLSEEN plates measured across the two HUDs, every one at a WHOLE magnification step (x$PLHALF6 and x$PLHALF) and none more than one step over half"
else
  bad "G122 plate size: violations dos-bar=$PLBAD hud640=$PLBAD6 (want 0 each; counts a fractional step, a step that is not half the interface zoom, a draw site still on the full zoom, and a px that is not dos*step) plates-measured=$PLSEEN/$PLSEEN6 (want >=1 each; zero means no plate drew at all and every other leg is vacuous) queue-badge-dos-bar=$PLQ(want >=1, dos=9x9 zoom=2 px=18x18) queue-badge-640=$PLQ6(want >=1, dos=9x9 zoom=1 px=9x9; zero here with a nonzero tooltip means the queued cameo was scrolled out of the strip, not that the badge is the wrong size) queue-accepted=$PLADD/$PLADD6(want >=1 each; 0 means the queue verb REFUSED the subject -- it is not a unit -- so no badge was ever asked for and the two counts above are absence, not size) half-step=$PLHALF(dos)/$PLHALF6(640), which differ BY DESIGN and are already pinned by the two badge legs: a 720-row window gives the dos bar zoom 3, which has no exact half, so its step is taken up to 2, while a DOS pixel on the 640 HUD is two HUD pixels and halves exactly to 1"
fi

# =====================================================================================
# G123 TEAM COLOURS: EVERY SEAT'S ARMY WEARS THAT SEAT'S COLOUR.
#
# The cartridge has two house texture sets and the renderer used to have two liveries, so
# two computers on one side fielded two identical armies. The join between a seat and a
# house is made inside the brain (GlyphX_Assign_Houses picks the house with Random_Pick
# and takes the colour from the lobby's seat order), so the renderer READS it back one
# seat at a time and builds the extra textures out of the two decodes the pack already
# carries. Four seats on the default colours is 0,1,2,3, which is one seat on each of the
# two textures that already exist plus TWO that have to be built -- the smallest run that
# exercises the build at all.
#
# THE PIXEL LEG IS THE ONE THAT MATTERS, AND IT HAS TO BE POINTED AT SOMETHING. Counting
# log lines proves the table was filled; only an A/B against --noteamcolours proves a
# different texture reached the card. The first cut of this gate shot the DEFAULT view at
# tick 300 and compared that. Those two frames are byte-identical by construction and
# always will be: the shroud is up, the camera sits on the human seat's own start, the
# only object in the frame is his MCV, and seat 0 wears colour 0, whose livery slot IS
# the GDI variant the two-livery build already bound. Measured, not argued -- that frame
# holds exactly one object, and the same A/B with the camera on an AI's construction yard
# moves 539 sampled pixels.
#
# So the A/B runs with the shroud down and the camera driven onto EVERY army wearing a
# livery that had to be BUILT: colour >= 2, fighting as Nod, which is what makes the pair
# decisive. The --noteamcolours side of such a house falls through to the plain base
# texture, and livery_texture falls through to the SAME base texture when slot 2..7 has
# no gl_extra -- so if the extra textures were built and never bound, these frames are
# equal again and this gate is red. Where those armies stand is read out of the probe
# run's own object dump rather than typed in, and the outside-the-box count keeps the
# verdict on the mesh: a difference in the bar or the radar cannot pay for this leg.
TCBOX=480,200,820,480
tc_script() {
    printf 'tick 300\n' > "/tmp/g123_$1.txt"
    ge_i=0
    for ge_p in $TCPOS; do
        ge_i=$((ge_i + 1))
        printf 'cam %s %s\nshot shots/g123_%s_%d.png\n' \
               "${ge_p%%:*}" "${ge_p##*:}" "$1" "$ge_i" >> "/tmp/g123_$1.txt"
    done
    printf 'quit\n' >> "/tmp/g123_$1.txt"
}
gbegin shots/g123_probe.png shots/g123_on_*.png shots/g123_off_*.png shots/g123_rep_*.png
grun /tmp/g123.log --scen $SKMAP --pack $SKMAP.pack $SKBASE --skirmish --side gdi \
     --ai 3 --noshroud --dumpobj --shot shots/g123_probe.png --ticks 300
TCSEATS=$(grep -c '^TEAMCOLOUR|seat=' /tmp/g123.log)
TCHOUSE=$(grep '^TEAMCOLOUR|seat=' /tmp/g123.log | sed -n 's/.*|house=\([^|]*\)|.*/\1/p' | sort -u | wc -l | tr -d ' ')
TCCOL=$(grep '^TEAMCOLOUR|seat=' /tmp/g123.log | sed -n 's/.*|colour=\([0-9]*\)|.*/\1/p' | sort -u | wc -l | tr -d ' ')
TCBUILT=$(grep -c '^TEAMCOLOUR|livery|' /tmp/g123.log)
TCPX=$(grep '^TEAMCOLOUR|livery|' /tmp/g123.log | sed -n 's/.*|texels=\([0-9]*\)$/\1/p' | awk '{s+=$1} END {print s+0}')
# Every AI seat whose colour had to be built, joined to the Multi name the renderer read
# back and to a standing building of that house (cell 127,127 is the limbo sentinel).
TCPOS=""; TCWHO=""; TCN=0
for ge_s in $(grep '^skirmish: seat=' /tmp/g123.log \
    | sed -n 's/^skirmish: seat=\([0-9]*\)|[^|]*|house=\([A-Za-z]*\)|[^|]*|colour=\([0-9]*\)|ai=\([01]\)|.*/\1 \2 \3 \4/p' \
    | awk '$2 == "Nod" && $3 >= 2 && $4 == 1 { print $1 }'); do
    ge_h=$(sed -n "s/^TEAMCOLOUR|seat=$ge_s|house=\([^|]*\)|.*/\1/p" /tmp/g123.log)
    [ -n "$ge_h" ] || continue
    ge_p=$(grep "^P|2|[^|]*|$ge_h|" /tmp/g123.log | grep -v '|127|127|' | head -1 \
           | sed -n 's/.*|wx=\([-0-9.]*\)|wz=\([-0-9.]*\)|.*/\1:\2/p')
    [ -n "$ge_p" ] || continue
    TCN=$((TCN + 1)); TCPOS="$TCPOS $ge_p"; TCWHO="$TCWHO $ge_h/seat$ge_s@$ge_p"
done
tc_script on; tc_script off; tc_script rep
grun - --scen $SKMAP --pack $SKMAP.pack $SKBASE --skirmish --side gdi --ai 3 \
     --noshroud --script /tmp/g123_on.txt
grun - --scen $SKMAP --pack $SKMAP.pack $SKBASE --skirmish --side gdi --ai 3 \
     --noshroud --noteamcolours --script /tmp/g123_off.txt
grun - --scen $SKMAP --pack $SKMAP.pack $SKBASE --skirmish --side gdi --ai 3 \
     --noshroud --script /tmp/g123_rep.txt
gshots shots/g123_probe.png
TCDIFF=-1; TCOUT=0; ge_i=0
for ge_p in $TCPOS; do
    ge_i=$((ge_i + 1))
    gshots shots/g123_on_$ge_i.png shots/g123_off_$ge_i.png shots/g123_rep_$ge_i.png || continue
    ge_d=$(python3 "$GATEDIR/gate_pixdiff.py" shots/g123_on_$ge_i.png shots/g123_off_$ge_i.png "$TCBOX")
    ge_w=$(python3 "$GATEDIR/gate_pixdiff.py" shots/g123_on_$ge_i.png shots/g123_off_$ge_i.png)
    TCOUT=$((TCOUT + ge_w - ge_d))
    if [ "$TCDIFF" -lt 0 ] || [ "$ge_d" -lt "$TCDIFF" ]; then TCDIFF=$ge_d; fi
done
TCA=$(cat shots/g123_on_*.png  2>/dev/null | shasum | cut -d' ' -f1)
TCB=$(cat shots/g123_off_*.png 2>/dev/null | shasum | cut -d' ' -f1)
TCC=$(cat shots/g123_rep_*.png 2>/dev/null | shasum | cut -d' ' -f1)
if [ "$GRC" != "0" ]; then
  bad "G123 team colours: a run failed (exit $GRC) or wrote no shot, so nothing below means anything"
elif [ "${TCSEATS:-0}" -ge 4 ] && [ "${TCHOUSE:-0}" -ge 4 ] && [ "${TCCOL:-0}" -ge 4 ] \
     && [ "${TCBUILT:-0}" -ge 2 ] && [ "${TCPX:-0}" -ge 1 ] && [ "${TCN:-0}" -ge 2 ] \
     && [ "${TCDIFF:--1}" -ge 200 ] && [ "${TCOUT:-1}" -eq 0 ] \
     && [ "$TCA" != "$TCB" ] && [ "$TCA" = "$TCC" ]; then
  ok "G123 team colours: $TCSEATS seats resolve to $TCHOUSE distinct houses wearing $TCCOL distinct player colours, $TCBUILT extra liveries built over $TCPX recoloured texels, and the camera on each of the $TCN armies wearing a BUILT livery ($TCWHO ) repaints at least $TCDIFF sampled pixels of that army's own mesh against the two-livery build, with $TCOUT pixels changed anywhere else on the frame; two runs identical"
else
  bad "G123 team colours: seats=$TCSEATS(want >=4; fewer means the seat walk stopped early or $SKMAP cannot seat four) distinct-houses=$TCHOUSE(want >=4; a shortfall means two seats resolved to one house, which is the HOUSE_MULTI1 offset being wrong) distinct-colours=$TCCOL(want >=4; a shortfall means ColorIndex is not surviving the lobby handoff) liveries-built=$TCBUILT(want >=2; ZERO IS THE WHOLE FEATURE MISSING -- either the join is empty or no texture kept its two decodes) recoloured-texels=$TCPX(want >=1; zero with liveries-built>0 means the nod/gdi copies were taken after the bleed or are not the same length) aimed-at=$TCN(want >=2;[$TCWHO ] zero means the probe dump named no standing building for a seat wearing a built colour, and every pixel leg below is then vacuous) mesh-pixels-changed=$TCDIFF(want >=200, measured 539 per army; ZERO IS THE BINDING -- the extra textures were built and never bound, so check livery_texture and the slot numbering) elsewhere=$TCOUT(want 0; nonzero means something outside the aimed army moved, so the mesh count is not what it claims) on=$TCA off=$TCB(MUST DIFFER) repeat=$TCC(must equal on)"
fi

# =====================================================================================
# G124 THE EDGE GESTURE, THROUGH THE LIVE LOOP, WITH THE REAL POINTER.
#
# WHY THIS EXISTS AND WHY G79 COULD NOT DO IT. Right edge scrolling was reported dead with
# the pointer over the sidebar while G79 was green, and G79 is not wrong: it measures a
# different layer. The `edge` and `edgeat` verbs call edge_scroll_allowed and edge_scroll
# with a pixel the gate hands them, so between a hand and those two functions they skip the
# SDL motion event, mscaleX, mouseC, the window-leave that resets it to -1, edgeArmed,
# mdrag, the editor test and keyboard focus. The whole of the reported fault lived there.
#
# WHAT WAS MEASURED, on a windowed 1600x960 game with the default 640x480 HUD, driving the
# real OS pointer in twenty steps to the right the way a hand does:
#     the sweep stopping exactly on the window's last column   pans east 10.305 cells
#     the same sweep, ending 400 points PAST the window        pans        0.000
#     the pointer parked on the DESKTOP's right edge instead   pans        0.000
#     the sweep stopping 60 points short, on the bar           pans        0.000
# So the feature only ever answered a pointer that stopped dead inside a twelve point
# target. A thrown pointer leaves the window, SDL reports the leave, the handler sets the
# tracked position to -1,-1 and edge_scroll returns on its own first line. That is the
# report, and edge_push_from_outside is the fix: a pointer outside the window, while the
# window still holds keyboard focus and the pointer is on the same display and level with
# it, is read as pressed against the edge it left by.
#
# HOW THIS GATE MEASURES IT. --edgeplay warps the REAL pointer with SDL_WarpMouseGlobal,
# through the live handler, and reads nothing but g_camX afterwards. It never calls
# edge_scroll or edge_scroll_allowed to decide an arm; it prints what they would say so a
# red arm can name the refusal instead of only reporting a still camera. It takes keyboard
# focus and proves it before starting, it moves the window to the middle of its display so
# there is desktop to push into, it parks the camera before every probe, it holds the
# pointer on the target every frame rather than placing it once, and it asserts a FLOOR
# rather than a distance because the live pan is EDGE_SPEED times a real frame time and
# lands on a different number every run. Every target is computed from the live geometry,
# so no arm can drift onto a pixel that used to mean something.
#
# THE TEN ARMS, and each is separately capable of going red:
#   0 the map-side strip, two points inside the tactical view's right edge  nothing
#     (it was `east` until 1 Sep 2026. Sliding the pointer off the map onto the bar
#      dragged the map with it, so only the SCREEN edge scrolls now.)
#   1 the window's own last column, which is ON the bar                         east
#   2 PUSHED PAST the window's right edge                    east   <-- the report
#   3 the window-pinned plate at the top of that column, hovered      nothing moves
#   4 PUSHED PAST the window at that plate's own height                         east
#   5 the innermost POINT of the window strip, EDGE_PT points in                east
#   6 the middle of the sidebar, inboard of the one east strip        nothing moves
#   7 one point further in than the strip is meant to reach           nothing moves
#   8 the window's own first column                                             west
#   9 PUSHED PAST the window's left edge                                        west
# 3 and 4 together are the whole of the sidebar handle question: hovering the plate must
# not pan the map out from under a pointer reaching for the only control that brings a
# hidden drawer back, and a pointer shoved off the screen at that height is not reaching
# for anything, so it must. 5 and 7 are a pair one point apart that pins the strip's width
# at twelve WINDOW POINTS, which is what makes the third configuration below meaningful.
#
# THREE CONFIGURATIONS, because two of the three faults are invisible in the others:
# the 640x480 HUD at the size the game ships at, the DOS bar (no window-pinned plates, so
# arm 3 flips to "must scroll" and the gate works that out from the layout rather than
# being told), and --hidpi, which is the only way to run the GAME on a drawable that is
# not its window. Until that flag existed the units half of this code had no test at all:
# the one path that scaled was the editor, and the editor refuses to edge scroll.
#
# PROVEN TO FAIL, three ways, each isolating one part of the fix and each measured:
#  * put the live loop back on the tracked pointer (edge_scroll_allowed(mouseC, mouseR)):
#    3 failures on the 640 HUD run, arms 2, 4 and 9, every one of them printing
#    placed=1 focus=1 pushed=1 dx=+0.000, which is the report in one line.
#  * take away only the veto bypass, so a pushed pointer is judged by sb_chrome_hit again:
#    1 failure, arm 4, chrome=9 allowed=0 dx=+0.000.
#  * make the strip drawable pixels again (edge_px returns EDGE_PT unscaled) and run
#    --hidpi: 2 failures, the unit arm reporting strip_pts=6.00 against EDGE_PT 12, and
#    arm 5 reporting mouse=2536 allowed=0 dx=+0.000 for a point that is 11 points inside
#    the window edge.
#
# WHAT THIS GATE STILL DOES NOT COVER, said here rather than left to be discovered: the
# editor's own playtest, which reaches the game through a different containment path;
# whether edge scrolling should work while the 1995 Options dialog is open, which it does
# not, because the whole keyboard block is skipped while that dialog is up; and the top and
# bottom edges of the window, which deliberately do NOT get the pushed-pointer treatment
# (the menu bar and the dock live on those display edges) and so still lose a thrown
# pointer the way the right edge used to.
# The HUD arrives as an argument rather than as an environment prefix on the call: a
# prefixed assignment in front of a FUNCTION persists after it in a POSIX shell, so the
# DOS run would quietly inherit the 640 HUD from the run before it.
ep_run() {
  ge_n="$1"; ge_hud="$2"; shift 2
  # perl's alarm because macOS has no timeout(1). The driver ends its own run on every
  # refusal it can name, so this only catches one it cannot: a window that never draws.
  CNC3D_HUD="$ge_hud" perl -e 'alarm 240; exec @ARGV' ./cnc_eyes --scen SCG01EB \
      --pack SCG01EA.pack $BASE --nosound "$@" --edgeplay >"/tmp/g124_$ge_n.log" 2>&1
  echo "G124 run $ge_n exit=$?" >>"$OUT"
  cat "/tmp/g124_$ge_n.log" >>"$OUT"
}
# The last line for a probe, because a probe disturbed by the desk's own mouse or by
# something stealing focus is RE-RUN rather than reported, and the retries print too.
ep_dx()    { grep "^EDGEPLAY|probe|$2|" "$1" | tail -1 | sed -n 's/.*|dx=\([+-][0-9.]*\)|.*/\1/p'; }
ep_end()   { sed -n 's/^EDGEPLAY|end|\([0-9]*\) failure(s)/\1/p' "$1" | tail -1; }
ep_strip() { sed -n 's/^EDGEPLAY|geom|.*|strip_pts=\([0-9.]*\)|.*/\1/p' "$1" | tail -1; }
ep_scale() { sed -n 's/^EDGEPLAY|geom|.*|mscaleX=\([0-9.]*\)|.*/\1/p' "$1" | tail -1; }
ep_reach() { sed -n 's/^EDGEPLAY|reach|held=\([-0-9]*\)|.*/\1/p' "$1" | tail -1; }
ep_run h6    new --gfx --w 1600 --h 960
ep_run dos   ""       --w 1280 --h 720
ep_run hidpi new --gfx --w 1280 --h 720 --hidpi
EPH6=$(ep_end /tmp/g124_h6.log);    EPDOS=$(ep_end /tmp/g124_dos.log)
EPHI=$(ep_end /tmp/g124_hidpi.log)
EPGEST=$(ep_dx /tmp/g124_h6.log 2);    EPGESTD=$(ep_dx /tmp/g124_dos.log 2)
EPGESTH=$(ep_dx /tmp/g124_hidpi.log 2)
EPPLATE=$(ep_dx /tmp/g124_h6.log 3);   EPPUSHPL=$(ep_dx /tmp/g124_h6.log 4)
EPMID=$(ep_dx /tmp/g124_h6.log 6);     EPLIP=$(ep_dx /tmp/g124_h6.log 7)
EPIN=$(ep_dx /tmp/g124_hidpi.log 5)
EPSTRIP=$(ep_strip /tmp/g124_h6.log);  EPSTRIPH=$(ep_strip /tmp/g124_hidpi.log)
EPSCALE=$(ep_scale /tmp/g124_hidpi.log)
EPREACH=$(ep_reach /tmp/g124_h6.log)
EPRETRY=$(cat /tmp/g124_h6.log /tmp/g124_dos.log /tmp/g124_hidpi.log | grep -c '^EDGEPLAY|retry')
# The three end lines are the verdict; the numbers below are quoted so a green line still
# says what was measured, and so a future reader can see the gesture's own distance.
EPV=$(awk -v a="${EPH6:-9}" -v b="${EPDOS:-9}" -v c="${EPHI:-9}" \
          -v g="${EPGEST:-0}" -v gd="${EPGESTD:-0}" -v gh="${EPGESTH:-0}" \
          -v s="${EPSTRIP:-0}" -v sh="${EPSTRIPH:-0}" -v x="${EPSCALE:-0}" '
BEGIN{ print (a == 0 && b == 0 && c == 0 && g > 1.0 && gd > 1.0 && gh > 1.0 \
              && s > 11.5 && s < 12.5 && sh > 11.5 && sh < 12.5 && x > 1.5) ? 1 : 0 }')
if [ "${EPV:-0}" = "1" ]; then
  ok "G124 edge gesture through the live loop: all ten arms pass on the 640x480 HUD at 1600x960, on the DOS bar at 1280x720, and on a 2x drawable ($EPSCALE) where the strip still measures $EPSTRIPH window points. A pointer thrown PAST the window's right edge, which is what the report was about and what pans 0.000 without the fix, pans east $EPGEST cells on the 640 HUD, $EPGESTD on the DOS bar and $EPGESTH at 2x; the window's own last point lands on column $EPREACH, inside the east strip; the sidebar handle plate hovered inside the window still refuses ($EPPLATE) while the same height pushed past the window scrolls ($EPPUSHPL); the middle of the bar refuses ($EPMID) and so does one point further in than the strip reaches ($EPLIP); at 2x a point eleven points inside the window edge scrolls ($EPIN), which is the arm the old drawable-pixel strip failed. $EPRETRY probe(s) were re-run after the desk interfered"
else
  bad "G124 edge gesture through the live loop: failures h6=$EPH6 dos=$EPDOS hidpi=$EPHI (want 0 0 0; read the EDGEPLAY|probe lines in the log, each one prints the pointer, the guard, focus, armed and the camera) pushed-past-the-window=$EPGEST/$EPGESTD/$EPGESTH(want >1 each; 0.000 with placed=1 and focus=1 is the reported bug back again, and means the loop is reading the tracked pointer instead of edge_push_from_outside) strip-points=$EPSTRIP/$EPSTRIPH(want 12 at every backing; 6 at mscaleX=$EPSCALE means EDGE_PT is being used as drawable pixels again) 11-points-in-at-2x=$EPIN(want >1) handle-hovered=$EPPLATE(want 0.000; nonzero means the map pans out from under a pointer reaching for the only control that brings a hidden drawer back) handle-pushed=$EPPUSHPL(want >1; 0.000 means the veto is following the pointer out of the window, where there is no control to protect) mid-bar=$EPMID(want 0.000) inside-the-lip=$EPLIP(want 0.000) last-point-lands-on=$EPREACH retries=$EPRETRY(a large number means the pointer could not be held still, so look at placed=, slip= and focuslost= before believing any arm)"
fi


# =====================================================================================
# G125  THE WHOLE HOUSE ROSTER: reachable in the strip, chosen on New Map, and WORN.
#
# Three reports, one unfinished job. The roster was widened in the DATA -- twelve houses,
# indexed by HousesType, counted off the multiplayer maximum rather than typed out -- and
# the three places that size, draw and click the owner chip strip each still counted to
# four. So the extra houses existed, the keyboard cycle already offered them, and the
# pointer could reach none of them. Underneath that, an object's owner reached the
# renderer intact and the renderer could do nothing with it: the house-to-colour join is
# filled by asking the brain about lobby seats, a map being authored has none, and every
# owner that was not GDI fell through to the one base texture.
#
# LEG 1 -- REACHABILITY, AND THE STRIP AGAINST ITS NEIGHBOURS. The layout harness now
# walks the strip on BOTH kinds of map, because the mode walk only ever reached OBJECTS
# with the kind it started on, so a strip that grows to twelve chips was never probed at
# all. The chip rectangle is now one function that the draw and the hit test both call --
# which is the fix, and which also means those two agreeing proves nothing. What is
# measured instead is the strip against numbers it does not compute: no chip may leave the
# sidebar, and the last row must stop short of the category tabs, whose position comes
# from the layout's own running total over the fixed-height budget.
#
# LEG 2 -- WHICH HOUSE A NEW MAP HANDS THE PLAYER. Both singleplayer answers are created
# and saved and the header each one wrote is read back off the disk. They must DIFFER: a
# gate that only checks the Nod case is green on a build that ignores the dialog and
# always writes BadGuy. The two files are deleted first, because a save copies an existing
# header through and a stale one would answer for this run.
#
# LEG 3 -- THE COLOUR, AND IT IS A PIXEL LEG. Counting log lines only proves a table was
# filled. Two buildings owned by two different multiplayer houses go down on one map and
# the frame is shot; the same script runs again with team colours off, where both houses
# fall back to the single base texture and the extra sheets are never built at all. The
# two frames must differ and a third run must reproduce the first. The floor below is a
# FLOOR and not a measurement: the sampled count for one recoloured army elsewhere in this
# suite is 539, this frame holds two buildings, and the first green run prints what it
# actually saw so the number can be tightened against it.
# =====================================================================================
G125B="--cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack --dylib TiberianDawn.dylib --dir missions/ --content content/"
G125W="missions/user_maps"
mkdir -p "$G125W"
rm -f "$G125W"/SCM92EA.* "$G125W"/SCM93EA.* "$G125W"/SCM94EA.*

# ---- leg 1: the strip, at eight window sizes and two backing scales, on both kinds.
# Run outside the shot wrapper for the same reason G99 is: it writes no picture, and its
# own exit status is the failure count, which would otherwise poison every later run.
G125U=$(./cnc_eyes --uitest 2>&1 || true)
printf '%s\n' "$G125U" | grep -E '^UITEST\|(TOTAL|OWNERSTRIP)' >> "$OUT"
G125UF=$(printf '%s' "$G125U" | sed -n 's/^UITEST|TOTAL|\([0-9]*\) failures$/\1/p' | head -1)
G125MP8=$(printf '%s' "$G125U" | grep -c 'owner chip 11 MP8')
G125S12=$(printf '%s' "$G125U" | grep -c '^UITEST|OWNERSTRIP|multi=1|chips=12|rows=3|')
G125S4=$(printf '%s' "$G125U"  | grep -c '^UITEST|OWNERSTRIP|multi=0|chips=4|rows=1|')

# ---- legs 2 and 3
printf 'newmap 1 0 0\neditscen SCM93EA\neditsave\nquit\n' > /tmp/g125_gdi.script
printf 'newmap 1 0 2\neditscen SCM94EA\neditsave\nquit\n' > /tmp/g125_nod.script
g125_colour_script() {
    {
        printf 'newmap 1 0 1\n'
        printf 'editscen SCM92EA\n'
        printf 'editplace FACT 20 20 6\n'      # Multi3, which the engine paints green
        printf 'editplace PYLE 24 20 9\n'      # Multi6, which it paints red
        printf 'cam 22 21\n'
        printf 'shot shots/g125_%s.png\n' "$1"
        printf 'quit\n'
    } > "/tmp/g125_$1.script"
}
g125_colour_script on; g125_colour_script off; g125_colour_script rep

gbegin shots/g125_on.png shots/g125_off.png shots/g125_rep.png
grun /tmp/g125_gdi.log --edit --editmode 0 --scen SCG01EA --pack SCG01EA.pack $G125B \
     --script /tmp/g125_gdi.script --w 1400 --h 900
grun /tmp/g125_nod.log --edit --editmode 0 --scen SCG01EA --pack SCG01EA.pack $G125B \
     --script /tmp/g125_nod.script --w 1400 --h 900
grun /tmp/g125_on.log  --edit --editmode 0 --scen SCG01EA --pack SCG01EA.pack $G125B \
     --script /tmp/g125_on.script --w 1280 --h 800
grun /tmp/g125_off.log --edit --editmode 0 --scen SCG01EA --pack SCG01EA.pack $G125B \
     --noteamcolours --script /tmp/g125_off.script --w 1280 --h 800
grun /tmp/g125_rep.log --edit --editmode 0 --scen SCG01EA --pack SCG01EA.pack $G125B \
     --script /tmp/g125_rep.script --w 1280 --h 800
gshots shots/g125_on.png shots/g125_off.png shots/g125_rep.png

G125PG=$(sed -n 's/^Player=//p' "$G125W/SCM93EA.INI" 2>/dev/null | tr -d '\r' | head -1)
G125PN=$(sed -n 's/^Player=//p' "$G125W/SCM94EA.INI" 2>/dev/null | tr -d '\r' | head -1)
G125PLACED=$(grep -c '|placed|' /tmp/g125_on.log)
G125LIV=$(grep -c '^TEAMCOLOUR|livery|' /tmp/g125_on.log)
G125WHICH=$(grep '^TEAMCOLOUR|livery|' /tmp/g125_on.log \
            | sed -n 's/.*|colour=\([0-9]*\)|\([A-Z]*\)|.*/\2/p' | sort -u | tr '\n' ' ')
G125LIVOFF=$(grep -c '^TEAMCOLOUR|livery|' /tmp/g125_off.log)
G125DIFF=$(python3 "$GATEDIR/gate_pixdiff.py" shots/g125_on.png shots/g125_off.png 2>/dev/null)
G125A=$(cat shots/g125_on.png  2>/dev/null | shasum | cut -d' ' -f1)
G125C=$(cat shots/g125_rep.png 2>/dev/null | shasum | cut -d' ' -f1)

if [ "$GRC" != "0" ]; then
  bad "G125 house roster: a run failed (exit $GRC) or wrote no shot, so nothing below means anything"
elif [ "${G125UF:-1}" = "0" ] && [ "${G125MP8:-0}" -ge 16 ] && [ "${G125S12:-0}" -ge 16 ] \
     && [ "${G125S4:-0}" -ge 16 ] && [ "$G125PG" = "GoodGuy" ] && [ "$G125PN" = "BadGuy" ] \
     && [ "${G125PLACED:-0}" -eq 2 ] && [ "${G125LIV:-0}" -eq 2 ] \
     && [ "${G125LIVOFF:-1}" -eq 0 ] && [ "${G125DIFF:-0}" -ge 100 ] \
     && [ "$G125A" = "$G125C" ]; then
  ok "G125 house roster: the layout harness answers for all twelve chips ($G125MP8 probes of the last one) over three rows on a skirmish map and one row of four on a mission map, at eight window sizes and two backing scales, with no chip outside the sidebar and none reaching the category tabs; New Map writes Player=$G125PG for the GDI answer and Player=$G125PN for the Nod one; and two buildings owned by two multiplayer houses build exactly the $G125LIV liveries those houses wear ($G125WHICH) and repaint $G125DIFF sampled pixels against the same map with team colours off, which builds none; two runs identical"
else
  bad "G125 house roster: uitest-failures=${G125UF:-?}(want 0) last-chip-probes=$G125MP8(want >=16, one per window size at each backing; ZERO means the strip is still counting to four or the harness never reached OBJECTS with a skirmish map) skirmish-strip=$G125S12(want >=16 lines reading chips=12 rows=3) mission-strip=$G125S4(want >=16 reading chips=4 rows=1; a shortfall here means the row count leaked into modes that share the strip) new-map-gdi-player=$G125PG(want GoodGuy) new-map-nod-player=$G125PN(want BadGuy; GoodGuy means the seed writer is still hard-coding the house, and the two being EQUAL means the dialog's answer never reaches it) placed=$G125PLACED(want 2; fewer means the script verb is still clamping the owner to the first four houses, and every colour leg below is then vacuous) liveries-built=$G125LIV(want exactly 2, the green and the red these two houses wear -- 0 is the whole feature missing, more than 2 means the editor is building colours no object on the map uses) built=[$G125WHICH] liveries-with-colours-off=$G125LIVOFF(want 0) pixels-repainted=$G125DIFF(want >=100; 0 means the sheets were built and never bound, or the two buildings are not in frame -- check the EDITPLACE lines and the camera) on=$G125A repeat=$G125C(must be equal)"
fi


# =====================================================================================
# G126 THE ECONOMY IS PLACEABLE.
#
# The editor could draw a map, staff it and script it, and it could not put down one
# cell of tiberium. A map made here therefore had no income and no way to get any: a
# refinery was a building that did nothing and a harvester drove in circles. The brush
# is a sixth palette group with one entry in it, because the engine throws the TI number
# away and re-picks it at random for every cell as the scenario loads.
#
# THIS GATE ASKS THE WHOLE CHAIN, not just the click. A painted cell has to reach the
# file as an [OVERLAY] row, the row has to survive OverlayClass::Read_INI, the cell it
# lands on has to be sized by MapClass::Overpass, a harvester has to be able to reach it,
# and the credits have to arrive. Every link but the first was already there and untested
# from this end, and the last one caught a second defect on its own: a harvester the
# editor placed saved as Guard, so it sat where it was put and the map earned nothing
# even with a field under its nose.
#
# The two failure modes it is built to catch, said plainly, because both were live:
#   painted=0        nothing in the palette places tiberium
#   stored-tiberium=0  the map has cells and still no income
# =====================================================================================
TBDIR=missions/user_maps
TBMAP=SCM96EA
TBBARE=SCM97EA
mkdir -p "$TBDIR"
rm -f "$TBDIR/$TBMAP.INI"  "$TBDIR/$TBMAP.BIN"  "$TBDIR/$TBMAP.HGT"
rm -f "$TBDIR/$TBBARE.INI" "$TBDIR/$TBBARE.BIN" "$TBDIR/$TBBARE.HGT"
# TWO MAPS FROM NOTHING, the same base on both: one with a field and one without, so the
# mission check can be asked the same question twice and has to answer it differently.
# The base sits beside the field on purpose. A harvester only considers tiberium in cells
# the player has SEEN (UnitClass::Tiberium_Check, unit.cpp:2332), so a field across the
# map from the only building would be invisible to it and this gate would be measuring
# the shroud rather than the economy.
{
  echo "newmap 1 0 0"
  echo "editscen $TBBARE"
  echo "editstart 6 30 30"
  echo "editplace FACT 30 30 0"
  echo "editplace PROC 34 30 0"
  echo "editplace HARV 34 34 0"
  echo "editsave"
  echo "newmap 1 0 0"
  echo "editscen $TBMAP"
  echo "editstart 6 30 30"
  echo "editplace FACT 30 30 0"
  echo "editplace PROC 34 30 0"
  echo "editplace HARV 34 34 0"
  echo "editplace SBAG 45 45 0"
  echo "echo G126-refuse"
  echo "editplace TI1 30 0 0"
  echo "editplace TI1 45 45 0"
  echo "editplace TI1 30 30 0"
  echo "echo G126-paint"
  for y in 24 25 26 27 28; do for x in 24 25 26 27 28; do echo "editplace TI1 $x $y 0"; done; done
  echo "echo G126-erase"
  echo "editerase 25 25"
  echo "editplace TI1 25 25 0"
  echo "editsave"
} > /tmp/g126_make.script
TBL=$(./cnc_eyes --edit --editmode 0 --scen SCG01EA --pack SCG01EA.pack $BASE \
        --script /tmp/g126_make.script --w 1400 --h 900 2>&1)
echo "$TBL" >> "$OUT"
# The three refusals are each a rule of the ENGINE's, quoted back at the author instead of
# accepted and silently dropped: the top row is outside what Read_INI keeps, a wall is
# already the cell's one overlay, and a building's footprint is not ground.
TBROW0=$(printf '%s' "$TBL" | grep -c "refused tiberium at 30,0 -- row 0 is the engine's top or bottom row")
TBWALL=$(printf '%s' "$TBL" | grep -c "refused tiberium at 45,45 -- a wall already holds that cell")
TBOCC=$(printf '%s' "$TBL"  | grep -c "refused TI1 at 30,30 -- cell 30,30 is already occupied")
TBPUT=$(printf '%s' "$TBL"  | sed -n '/^ECHO|G126-paint/,/^ECHO|G126-erase/p' | grep -c '^EDITPLACE|TI1|.*|placed|')
TBERAS=$(printf '%s' "$TBL" | grep -c '^EDITERASE|at 25,25|erased|.*|tiberium=24$')
TBWROTE=$(printf '%s' "$TBL" | sed -n "s#.*$TBMAP.INI.tmp -- .*, \([0-9]*\) tiberium,.*#\1#p" | tail -1)
# The rows in the file, on the 64 stride, and NO CELL WRITTEN TWICE: a CellClass holds one
# Overlay, so a second row on the same key is a coin toss decided by read order.
TBROWS=$(awk '/^\[OVERLAY\]/{f=1;next} /^\[/{f=0} f&&/=TI/' "$TBDIR/$TBMAP.INI" | sort -u | wc -l | tr -d ' ')
TBFIRST=$(awk '/^\[OVERLAY\]/{f=1;next} /^\[/{f=0} f&&/=TI/{split($0,a,"=");print a[1]}' "$TBDIR/$TBMAP.INI" | sort -n | head -1)
TBLAST=$(awk '/^\[OVERLAY\]/{f=1;next} /^\[/{f=0} f&&/=TI/{split($0,a,"=");print a[1]}' "$TBDIR/$TBMAP.INI" | sort -n | tail -1)
TBDUPE=$(awk '/^\[OVERLAY\]/{f=1;next} /^\[/{f=0} f&&/=/{split($0,a,"=");print a[1]}' "$TBDIR/$TBMAP.INI" | sort | uniq -d | wc -l | tr -d ' ')
TBHARV=$(grep -c '^000=GoodGuy,HARV,256,.*,Harvest,None' "$TBDIR/$TBMAP.INI")
# THE ENGINE HANDS IT BACK. Reopening runs the file through the brain, so what the eraser
# bites into on this leg is the brain's own cell table and not the editor's memory of it.
printf 'tick 2\nediterase 26 26\neditsave\nquit\n' > /tmp/g126_back.script
TBB=$(./cnc_eyes --edit --editmode 0 --scen "$TBMAP" --pack SCG01EA.pack $BASE \
        --dir "$TBDIR/" --script /tmp/g126_back.script --w 1400 --h 900 2>&1)
echo "$TBB" >> "$OUT"
TBBACK=$(printf '%s' "$TBB" | grep -c '^EDITERASE|at 26,26|erased|.*|tiberium=24$')
# THE MISSION CHECK, asked three ways: a map with a field, the same map without one, and
# a cartridge mission whose seventeen cells all lie outside its playable rectangle.
TBC1=$(./cnc_eyes --scripttest --shot /tmp/g126_s1.png --ticks 1 --scen "$TBMAP" \
        --pack SCG01EA.pack $BASE --dir "$TBDIR/" 2>&1 | grep '^CHECK|')
TBC2=$(./cnc_eyes --scripttest --shot /tmp/g126_s2.png --ticks 1 --scen "$TBBARE" \
        --pack SCG01EA.pack $BASE --dir "$TBDIR/" 2>&1 | grep '^CHECK|')
TBC3=$(./cnc_eyes --scripttest --shot /tmp/g126_s3.png --ticks 1 --scen SCG01EA \
        --pack SCG01EA.pack $BASE 2>&1 | grep '^CHECK|')
{ echo "$TBC1"; echo "$TBC2"; echo "$TBC3"; } >> "$OUT"
TBQUIET=$(printf '%s' "$TBC1" | grep -c 'tiberium')
TBCRY=$(printf '%s' "$TBC2"   | grep -c '^CHECK|ERR |no tiberium anywhere on the map')
TBOUT=$(printf '%s' "$TBC3"   | grep -c 'all 17 tiberium cells are outside the playable area')
# AND IT PAYS. The house's stored tiberium is the brain's own number off the HOUSE line,
# so this one value stands for the whole chain: palette, cell, row, field, harvester,
# refinery, store. Zero here is the state this gate exists to keep the editor out of.
printf 'tick 1000\ntick 600\nobj HARV\nsilodump\nquit\n' > /tmp/g126_play.script
TBP=$(./cnc_eyes --scen "$TBMAP" --pack SCG01EA.pack --reskin $BASE --dir "$TBDIR/" \
        --nosound --script /tmp/g126_play.script --w 1280 --h 720 2>&1)
echo "$TBP" >> "$OUT"
TBMIS=$(printf '%s' "$TBP" | sed -n 's/^OBJ|HARV#0|.*|mission=\([A-Za-z ]*\)|.*/\1/p' | tail -1)
TBSTORE=$(printf '%s' "$TBP" | sed -n 's/^HOUSESTORE|GoodGuy|tiberium=\([0-9]*\)|.*/\1/p' | tail -1)
if [ "${TBPUT:-0}" -eq 25 ] && [ "${TBROW0:-0}" -ge 1 ] && [ "${TBWALL:-0}" -ge 1 ] \
   && [ "${TBOCC:-0}" -ge 1 ] && [ "${TBERAS:-0}" -eq 1 ] && [ "${TBWROTE:-0}" -eq 25 ] \
   && [ "${TBROWS:-0}" -eq 25 ] && [ "${TBDUPE:-1}" -eq 0 ] && [ "${TBHARV:-0}" -eq 1 ] \
   && [ "${TBBACK:-0}" -eq 1 ] && [ "${TBQUIET:-1}" -eq 0 ] && [ "${TBCRY:-0}" -ge 1 ] \
   && [ "${TBOUT:-0}" -ge 1 ] && [ "${TBSTORE:-0}" -gt 0 ] && [ "$TBMIS" = "Harvest" ]; then
  ok "G126 the economy is placeable: 25 cells of tiberium painted from the palette onto a map made from nothing, written as 25 [OVERLAY] rows from cell $TBFIRST to $TBLAST with no cell written twice; refused on the engine's top row, on a walled cell and on a building's footprint, each with the reason; erased and repainted through the eraser, and after a reopen -- where the field is the brain's own, not the editor's copy -- the eraser still bites into it; the mission check says nothing about tiberium on this map, calls the same map without any an error ($TBCRY), and reports the first GDI mission's seventeen unreachable cells ($TBOUT); and played for 1600 ticks the harvester goes out on $TBMIS and puts $TBSTORE tiberium into the refinery"
else
  bad "G126 the economy is placeable: painted=$TBPUT(want 25; ZERO IS THE STATE THIS GATE EXISTS FOR -- nothing in the palette places tiberium) refused-top-row=$TBROW0(want >=1) refused-on-a-wall=$TBWALL(want >=1) refused-on-a-building=$TBOCC(want >=1) erased=$TBERAS(want 1; zero means the eraser cannot see a tiberium cell) written=$TBWROTE(want 25) overlay-rows=$TBROWS(want 25, $TBFIRST..$TBLAST) duplicate-cells=$TBDUPE(want 0; nonzero means two overlays claim one cell and whichever is read last wins) harvester-order=$TBHARV(want 1; zero means a placed harvester saves as Guard again and the map earns nothing with a field under its nose) erase-after-reload=$TBBACK(want 1; zero means the brain did not read the field back, so the round trip is broken rather than the eraser) check-quiet-on-a-good-map=$TBQUIET(want 0) check-calls-a-bare-map-an-error=$TBCRY(want >=1) check-sees-unreachable-cells=$TBOUT(want >=1) harvester-mission=$TBMIS(want Harvest) stored-tiberium=$TBSTORE(want >0; zero means the map has cells and still no income)"
fi
rm -f "$TBDIR/$TBMAP.INI"  "$TBDIR/$TBMAP.BIN"  "$TBDIR/$TBMAP.HGT"
rm -f "$TBDIR/$TBBARE.INI" "$TBDIR/$TBBARE.BIN" "$TBDIR/$TBBARE.HGT"

# =====================================================================================
# G128 DELETE WORKS THROUGH A STACK, AND THE LAST PRESS PUTS THE GROUND BACK.
#
# THE REPORT. Delete should take the things on a cell ONE AT A TIME rather than all at
# once or only the top one, and once they are all gone the cell should go back to its
# default terrain. Half of that was already true -- erase took one thing per press, and
# each press was one undo step -- and the other half was not, in three ways.
#
#   1 THE STACK STOPPED AT TWO RUNGS. Erase looked at the wall list and the object list
#     and at nothing else, so a cell carrying a tiberium field or an authored crater
#     answered "nothing at x,y to erase" for ever and the status card said "nothing under
#     the pointer" over both.
#   2 THERE WAS NO GROUND RUNG AT ALL. Nothing outside the terrain palette's own CLEAR1
#     tile ever wrote TEMPLATE_NONE, so "back to default" did not exist as a gesture.
#   3 THE ORDER WAS WRONG WHERE IT COULD BE. Walls came before objects, on the argument
#     that a wall is overlay so a click on a walled cell means the wall. The placement
#     test refuses a footprint only over BUILDING and TERRAIN, so an infantryman may
#     legally stand on a sandbag -- and the first press pulled the sandbag out from under
#     him. Against a pre-fix binary this gate reports exactly that on press one:
#     "objects 61 want 60, walls 42 want 43".
#
# WHAT IS MEASURED. --undotest builds a cell no shipped map carries -- a non-clear
# template, a tiberium cell, a crater, a wall, and a man standing on the lot -- and then
# presses DELETE six times, reading the WHOLE DOCUMENT's counts after each press rather
# than the cell's, so nothing in the test re-uses the arithmetic the stack itself aims
# with. Five presses must take five things in that order, each filing exactly one undo
# step; the sixth must refuse and file none; five undos must rebuild the cell, tiberium
# and crater included. Run in all three theaters, because the last rung needs a plain
# ground tile in that theater's own bank.
#
# --shot is passed because --undotest lives inside the shot path and there is no other
# way in; it returns before a picture is drawn, so no file is written and none is read.
# This is also the first time --undotest has been in this suite at all.
# =====================================================================================
gbegin
UTOK=0; UTBAD=""
for utm in SCG01EA SCB01EA SCW01EA; do
  grun "/tmp/g128_$utm.log" --undotest --edit --scen "$utm" --pack "$utm.pack" $BASE \
       --nosound --shot /tmp/g128-shot.png --ticks 1 --w 1280 --h 800
  UT1=$(grep -c '^UNDOTEST|ok  |five presses' "/tmp/g128_$utm.log")
  UT2=$(grep -c '^UNDOTEST|ok  |the sixth press refuses' "/tmp/g128_$utm.log")
  UT3=$(grep -c '^UNDOTEST|ok  |five undos rebuilt' "/tmp/g128_$utm.log")
  UT4=$(grep -c '^UNDOTEST|PASSED|0 failures' "/tmp/g128_$utm.log")
  if [ "$UT1$UT2$UT3$UT4" = "1111" ]; then
    UTOK=$((UTOK+1))
  else
    UTBAD="$UTBAD $utm(stack=$UT1 sixth=$UT2 undos=$UT3 pass=$UT4: $(grep -m1 '^UNDOTEST|FAIL' "/tmp/g128_$utm.log"))"
  fi
done
if [ "$GRC" = "0" ] && [ "$UTOK" = "3" ]; then
  ok "G128 the delete stack: in TEMPERAT, DESERT and SNOW a cell carrying a non-clear template, a tiberium cell, a crater, a wall and an infantryman gives them up ONE PER PRESS and in that order -- the man first, not the sandbag under him -- each press filing exactly one undo step. The fifth press puts the ground back to TEMPLATE_NONE, the sixth refuses an already empty cell and files nothing, and five undos rebuild all five rungs including the tiberium and the crater, which this editor has no brush for"
else
  bad "G128 the delete stack: run status $GRC, $UTOK of 3 theaters clean --$UTBAD. A 'delete N of 5' line names the rung, what the press took and every count that disagreed: walls one short with objects one high on press 1 is the wall-before-object order back again; a refusal on press 3 or 4 is the stack stopping at the two layers the old erase could see; a template that is not 255 after press 5 is the missing ground rung; an undo depth that does not grow by exactly one per press means a rung is not leaving through edit_commit"
fi

# =====================================================================================
# G129 A WAY IN TO THE HEIGHTMAP IMPORTER.
#
# G118 proves the importer and says in its own comment that nothing could reach it. The
# route taken is the FOLDER AS THE PICKER: every PNG and PCX in user_maps is offered on
# one row of the elevation panel, the picture named after the open map is preselected so
# the beside-the-map convention still works, and a chip steps through the rest. A native
# file dialog was refused as a new dependency on every platform this builds for.
#
# WHAT THIS ASSERTS, each of them through the function the CLICK runs and not a copy:
#   the folder is listed, and the count is the number of pictures this gate staged;
#   the chip walks the list until a named file is the armed one;
#   a painted PNG imports, moves blocks, and TERRACES the sheer face it was painted with;
#   the same picture a second time is refused because the ground already matches it;
#   a 4-bit PCX is refused with the re-save named;
#   an empty folder says where to put a picture;
#   a map off the tier ladder refuses and names the gate that opens it;
# and above all that every one of those refusals reaches the TOAST. The importer has
# always produced these sentences; until this row existed they went to the log alone,
# which is the same as not producing them.
#
# WHAT IT DOES NOT COVER, said here rather than left to be found: the pointer. That
# eui_hit answers with this row at the rectangle the panel draws is checked by --uitest
# under G99, at eight window sizes and two backing scales, including the four short
# frames where the row does not fit and must answer with NOTHING; the step from that
# answer to these functions is a two-line switch arm that is read, not exercised, because
# the editor panel has no synthetic-click harness.
G129UM="missions/user_maps"
mkdir -p "$G129UM"
python3 - "$G129UM" <<'PY'
import sys, os, zlib, struct
d = sys.argv[1]
def ck(tag, data):
    c = tag + data
    return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)
W = H = 64
pix = [64] * (W * H)                       # tier 1 everywhere: plain ground
for y in range(20, 44):
    for x in range(20, 44):
        pix[y * W + x] = 255               # tier 4: three rungs up with no slope at all
raw = b"".join(b"\x00" + bytes(pix[y*W:(y+1)*W]) for y in range(H))
png = (b"\x89PNG\r\n\x1a\n" + ck(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 0, 0, 0, 0))
       + ck(b"IDAT", zlib.compress(raw, 9)) + ck(b"IEND", b""))
open(os.path.join(d, "G129MESA.PNG"), "wb").write(png)
h = bytearray(128)
h[0] = 0x0A; h[1] = 5; h[2] = 1; h[3] = 4          # bits per plane 4: unsupported
h[8:10] = (15).to_bytes(2, "little"); h[10:12] = (15).to_bytes(2, "little")
h[65] = 1; h[66:68] = (8).to_bytes(2, "little")
open(os.path.join(d, "G129BAD.PCX"), "wb").write(bytes(h) + b"\xc8\x00" * 16)
PY
# The empty-folder arm gets a missions folder of its OWN. Asserting "no images" against
# the run folder's user_maps would fail the day somebody leaves a picture in it.
G129ED=/tmp/g129_empty/missions
rm -rf /tmp/g129_empty; mkdir -p "$G129ED/user_maps"
cp missions/SCG01EA.INI missions/SCG01EA.BIN "$G129ED/" 2>/dev/null
cat > /tmp/g129_pick.txt <<'SCRIPT'
newmap 1 0 0
editscen G129MAP
elevimport
elevimport pick G129MESA.PNG
elevimport go
elevimport go
elevimport pick G129BAD.PCX
elevimport go
quit
SCRIPT
# No newmap here on purpose: SCG01EA as it ships is OFF the tier ladder.
cat > /tmp/g129_ladder.txt <<'SCRIPT'
elevimport pick G129MESA.PNG
elevimport go
quit
SCRIPT
cat > /tmp/g129_none.txt <<'SCRIPT'
newmap 1 0 0
elevimport
elevimport go
quit
SCRIPT
G129E="--edit --editmode 2 --scen SCG01EA --pack SCG01EA.pack $BASE --nosound --w 1400 --h 900"
G129A=$(./cnc_eyes $G129E --script /tmp/g129_pick.txt 2>&1 | tee -a "$OUT")
G129L=$(./cnc_eyes $G129E --script /tmp/g129_ladder.txt 2>&1 | tee -a "$OUT")
G129N=$(./cnc_eyes $G129E --dir "$G129ED/" --script /tmp/g129_none.txt 2>&1 | tee -a "$OUT")
rm -f "$G129UM/G129MESA.PNG" "$G129UM/G129BAD.PCX"
G129LIST=$(echo "$G129A" | sed -n 's/^ELEVIMPORT|list|\([0-9]*\) image(s).*/\1/p' | head -1)
G129SEL=$(echo "$G129A" | sed -n 's/^ELEVIMPORT|pick|[0-9]* image(s)|sel=\([^|]*\).*/\1/p' | head -1)
G129MOV=$(echo "$G129A" | sed -n 's/.*HEIGHTMAP IMPORTED -- G129MESA.PNG, 64x64 over [0-9]*x[0-9]* blocks -- \([0-9]*\) blocks moved.*/\1/p' | head -1)
G129TER=$(echo "$G129A" | sed -n 's/.*blocks moved, [0-9]* cells re-dressed; terraced \([0-9]*\).*/\1/p' | head -1)
G129SAME=$(echo "$G129A" | grep -c 'HEIGHTMAP NOT IMPORTED -- this image is the ground the map already has')
G129PCX=$(echo "$G129A" | grep -c 'HEIGHTMAP NOT IMPORTED -- this PCX is bits-per-plane 4')
G129LAD=$(echo "$G129L" | grep -c 'HEIGHTMAP NOT IMPORTED -- this map is not on the ladder')
G129NONE=$(echo "$G129N" | grep -c 'NO HEIGHTMAP IMAGE -- no PNG and no PCX in user_maps')
G129NN=$(echo "$G129N" | sed -n 's/^ELEVIMPORT|list|\([0-9]*\) image(s).*/\1/p' | head -1)
if [ "${G129LIST:-0}" -ge 2 ] && [ "$G129SEL" = "G129MESA.PNG" ] && \
   [ "${G129MOV:-0}" -gt 0 ] && [ "${G129TER:-0}" -gt 0 ] && \
   [ "${G129SAME:-0}" -ge 1 ] && [ "${G129PCX:-0}" -ge 1 ] && \
   [ "${G129LAD:-0}" -ge 1 ] && [ "${G129NONE:-0}" -ge 1 ] && [ "${G129NN:-9}" = "0" ]; then
  ok "G129 heightmap picker: the folder lists $G129LIST images, the chip walks to $G129SEL, importing it moves $G129MOV blocks and terraces $G129TER of them into a stair, and all four refusals reach the toast in words a person can act on -- the same picture twice, a 4-bit PCX, a map off the ladder, and an empty folder"
else
  bad "G129 heightmap picker: listed=$G129LIST(want >=2) walked-to=$G129SEL(want G129MESA.PNG) blocks-moved=$G129MOV(want >0) terraced=$G129TER(want >0) same-picture-refused=$G129SAME(want >=1) bad-pcx-refused=$G129PCX(want >=1) off-ladder-refused=$G129LAD(want >=1) empty-folder-said-so=$G129NONE(want >=1) empty-count=$G129NN(want 0). An empty listed= with everything else empty means the elevimport verb is missing from the binary in $RUNDIR"
fi


# ---------------------------------------------------------------------------------
# G130 THE LOCKSTEP TURN SCHEDULER, and it is the only gate here that needs no window,
# no pack and no brain. Multiplayer's correctness starts with peers executing the same
# orders on the same turns in the same sequence, and that property is checkable with no
# game at all: gate_lockstep runs several peers in one process and gate_netloop does it
# again over real loopback datagrams.
#
# BOTH ARE RUN, because they fail differently. The first covers loss, four peers,
# scrambled arrival order and malformed input with no network in the way; the second
# covers what only a real socket reaches, which is bind, a non blocking read that
# legitimately returns nothing, and a datagram mapped back to a seat by its address.
#
# A THIRD RUN, and it is the SHIPPED TOOL rather than a gate. netcheck is copied into
# every package: it is what a player runs when a match will not start, and until this line
# nothing exercised it at all. Its selftest puts both peers in one process over loopback
# and prints the digest of the orders each executed, which is the same number a real pair
# prints because the synthetic orders depend only on seat and turn. So one run covers the
# tool's own argument handling, its handshake-free path and the digest a person is about
# to compare by eye, in five milliseconds.
#
# They are cheap enough to be unconditional: all three finish in well under a second.
LSRC=0; LSOUT=""
if [ -x ./gate_lockstep ]; then
  LSOUT=$(./gate_lockstep 2>&1); LSRC=$?
  LSPASS=$(printf '%s' "$LSOUT" | grep -c '^OK')
else
  LSRC=99
fi
NLRC=0; NLPASS=0
if [ -x ./gate_netloop ]; then
  NLOUT=$(./gate_netloop 2>&1); NLRC=$?
  NLPASS=$(printf '%s' "$NLOUT" | grep -c '^OK')
else
  NLRC=99
fi
NCRC=0; NCOUT=""; NCDIG="none"
if [ -x ./netcheck ]; then
  NCOUT=$(./netcheck selftest 2>&1); NCRC=$?
  NCDIG=$(printf '%s' "$NCOUT" | awk '/ORDER DIGEST/{print $NF; exit}')
  # A stale built-in expectation is NOT a failure of the scheduler, and netcheck says so
  # itself by exiting zero. It is still worth naming here, because the number a person
  # compares by eye came from somewhere and this is where it stops being trusted quietly.
  printf '%s' "$NCOUT" | grep -q '^  NOTE: the digest built into this binary' && NCDIG="$NCDIG (the tool's built-in expectation is stale)"
else
  NCRC=99
fi
if [ "$LSRC" = "0" ] && [ "$NLRC" = "0" ] && [ "$NCRC" = "0" ]; then
  ok "G130 lockstep scheduler: $LSPASS legs green with no network (loss in both directions, four peers, scrambled arrival, forged and malformed packets, turn overflow reported rather than dropped) and $NLPASS more over real loopback datagrams, and the shipped netcheck selftest agrees with itself at ORDER DIGEST $NCDIG. Proves peers AGREE; it does not pin which sequence they agree on, and the gate file says so"
else
  bad "G130 lockstep scheduler: gate_lockstep exit=$LSRC gate_netloop exit=$NLRC netcheck-selftest exit=$NCRC (99 means the binary was not built; game/build.sh makes all three). First failures: $(printf '%s\n%s\n%s' "$LSOUT" "$NLOUT" "$NCOUT" | grep -E '^FAIL|^  FAILED' | head -3 | tr '\n' ' ')"
fi

# ---------------------------------------------------------------------------
# G194 TWO BRAINS, ONE PROCESS, IN LOCKSTEP. This is the gate that G4 cannot be.
#
# G4 runs one binary twice and compares. That proves the engine is REPEATABLE, and
# repeatable is not the property lockstep needs: lockstep needs two INDEPENDENT copies of
# the engine, advancing side by side, to agree with each other. Those are different
# claims, and the whole multiplayer design rests on the second one.
#
# So this loads the brain twice as two separate images and compares the full object dump
# after every tick. It runs with CNC3D_Lockstep ON, deliberately, because the determinism
# fixes are gated on that flag and a run with it off exercises the campaign path rather
# than the one a match takes.
#
# THE ISOLATION ASSERTION IS THE GATE, not decoration. dyld keys images on path, so two
# byte copies at two paths are two sets of globals while a symlink or the same path twice
# is ONE. If the two instances were secretly one object every tick would match for free
# and this would print a confident green having measured nothing. The harness sets the
# lockstep flag through A and requires B to still read false before it runs a single tick.
#
# NEGATIVE CONTROL, run by hand when this gate changes: pass the Enhanced brain as a sixth
# argument and the wire fingerprints differ, so it refuses before ticking. Proof it fails.
# ---------------------------------------------------------------------------
if [ -x ./cnc_twobrain ] && [ -f ./TiberianDawn.dylib ]; then
  # Leg 1, a campaign mission: two instances agree while nothing is ordered.
  TBOUT=$(./cnc_twobrain ./TiberianDawn.dylib content/ missions/ SCB01EA 2000 2>&1)
  TBRC=$?
  # Leg 2, a real two-human match: an order made on one instance reaches the other and
  # takes effect on both. SCM* arms multiplayer, which is what makes the pump that
  # executes a posted order run at all. It carries its own negative control: the same
  # order given to ONE instance must make them differ, or the leg proved the plumbing
  # rather than the execution.
  if [ "$TBRC" = "0" ]; then
    TBOUT2=$(./cnc_twobrain ./TiberianDawn.dylib content/ missions/ SCM01EA 300 2>&1)
    TBRC=$?
    TBOUT="$TBOUT
$TBOUT2"
  fi
else
  TBRC=99
fi
TBHASH=$(printf '%s' "$TBOUT" | grep -o 'layout hash [0-9A-F]*' | head -1 | awk '{print $3}')
if [ "$TBRC" = "0" ]; then
  ok "G194 two brains in lockstep: two INDEPENDENT instances of the engine (isolation proven by setting the lockstep flag on one and reading it back false on the other, not assumed), both with CNC3D_Lockstep ON, agreeing on the full object dump after every one of 2000 ticks of SCB01EA at order-wire layout $TBHASH; and in a two-human SCM01EA match an order made on one instance was drained off it, posted to both, executed on both and left them byte identical, with the control that the SAME order given to one instance alone DID make them differ so the comparison can see it take effect. G4 proves one binary repeats; this proves two copies AGREE and that an order crosses between them, which is the property lockstep is built on. Since 3 Sep 2026 the same match also proves the four gestures that used to bypass the order queue are orders now: repair, sell, wall sell and placement each changed NOTHING on the instance that clicked until the order crossed, then changed both worlds alike (the sell with no visible effect headless, because Sell_Back needs build-up art the stubs do not carry), and two placement clicks in one window placed exactly one building"
else
  bad "G194 two brains in lockstep: exit=$TBRC (99 means cnc_twobrain or the brain was not in the run folder; game/build.sh makes the binary). $(printf '%s' "$TBOUT" | grep -E '^FAILED|first difference|DIVERGED' | head -2 | tr '\n' ' ')"
fi

# ---------------------------------------------------------------------------
# G195 TWO PEERS OVER LOOPBACK: the transport seam joined to the engine (design Phase 3).
#
# Two copies of this renderer, one hosting and one joining on 127.0.0.1, each with its
# own brain, arm the same two-human skirmish from the host's handshake and run it in
# lockstep: one turn per tick, orders crossing as EventClass bytes through the four
# lockstep exports, and the world hashed after every tick with the hash exchanged every
# fifteen frames. The host deploys, builds a Power Plant and places it; the joiner
# deploys and watches. What is asserted: both ran to a clean end, at least ten hash
# reports agree between the two logs and none disagree, no NETDESYNC line, and the
# joiner's final world dump carries the Power Plant the HOST placed, which is an order
# that crossed the wire and executed on a machine that never clicked.
#
# The joiner's tick count is deliberately longer than the host's: it ends when the host
# says goodbye, and its script prints "net peer left" rather than "brain refused".
# ---------------------------------------------------------------------------
if [ -x ./cnc_eyes ] && [ -s "$SKMAP.pack" ] && [ -s "missions/$SKMAP.INI" ]; then
  rm -f net_host.log net_join.log
  ./cnc_eyes --scen $SKMAP --pack $SKMAP.pack $SKBASE --skirmish --side gdi --ai 0 \
      --host 17431 --script gate_net_host.txt > net_host.log 2>&1 &
  NHP=$!
  sleep 2
  ./cnc_eyes --scen $SKMAP --pack $SKMAP.pack $SKBASE --skirmish --side nod --ai 0 \
      --join 127.0.0.1 17431 --script gate_net_join.txt > net_join.log 2>&1 &
  NJP=$!
  wait $NHP; NHRC=$?
  wait $NJP; NJRC=$?
  cat net_host.log net_join.log >> "$OUT"
  NHSYNC=$(grep '^NETSYNC|' net_host.log | sort -u)
  NJSYNC=$(grep '^NETSYNC|' net_join.log | sort -u)
  # NO PROCESS SUBSTITUTION. This suite runs under sh, where `<(...)` is a syntax error:
  #   sh -c 'comm -12 <(echo a) <(echo a)'  ->  syntax error near unexpected token `('
  # Both of these used it, so both command substitutions came back EMPTY and the gate
  # reported "hash-reports-agreeing=" with nothing after the sign -- not a zero, a blank.
  # It passes on a bash box and fails on this one, which reads as a platform difference in
  # the FEATURE and is a difference in the SHELL. Same defect was found in G154 today.
  printf '%s\n' "$NHSYNC" > /tmp/g195_host.txt
  printf '%s\n' "$NJSYNC" > /tmp/g195_join.txt
  NAGREE=$(comm -12 /tmp/g195_host.txt /tmp/g195_join.txt | grep -c .)
  # A frame both reported with different hashes is a disagreement even if no alarm fired.
  sort -t'|' -k2,2 /tmp/g195_host.txt > /tmp/g195_hs.txt
  sort -t'|' -k2,2 /tmp/g195_join.txt > /tmp/g195_js.txt
  NDIS=$(join -t'|' -j2 /tmp/g195_hs.txt /tmp/g195_js.txt 2>/dev/null | awk -F'|' '$3 != $5' | grep -c .)
  NDESYNC=$(grep -c '^NETDESYNC|' net_host.log net_join.log | awk -F: '{s+=$2} END{print s+0}')
  NPLACED=$(grep -c '^SIDEBAR-PLACE|NUKE|' net_host.log)
  NJNUKE=$(grep '^OBJ|BUILDING|NUKE|' net_join.log | grep -c '|limbo=0|')
  NPEERLEFT=$(grep -c '^TICK|net peer left' net_join.log)
else
  NHRC=99; NJRC=99; NAGREE=0; NDIS=0; NDESYNC=0; NPLACED=0; NJNUKE=0; NPEERLEFT=0
fi
if [ "$NHRC" = "0" ] && [ "$NJRC" = "0" ] && [ "${NAGREE:-0}" -ge 10 ] && [ "${NDIS:-0}" = "0" ] \
   && [ "${NDESYNC:-0}" = "0" ] && [ "${NPLACED:-0}" -ge 1 ] && [ "${NJNUKE:-0}" -ge 1 ]; then
  ok "G195 two peers over loopback: a host and a joiner armed one two-human skirmish from the handshake and ran it in lockstep, $NAGREE world-hash reports agree between the two logs and none disagree, no desync alarm, and the Power Plant the host placed stands in the joiner's world (an order that crossed the wire and executed on a machine that never clicked; the joiner ended on the host's goodbye: $NPEERLEFT)"
else
  bad "G195 two peers over loopback: host exit=$NHRC joiner exit=$NJRC (99: cnc_eyes or $SKMAP not in the run folder) hash-reports-agreeing=$NAGREE(want >=10) disagreeing=$NDIS(want 0) desync-alarms=$NDESYNC(want 0) host-placed=$NPLACED(want >=1) joiner-sees-plant=$NJNUKE(want >=1). $(grep -hE '^NET\|error|^NETDESYNC|^TICK\|brain refused' net_host.log net_join.log 2>/dev/null | head -3 | tr '\n' ' ')"
fi

# =====================================================================================
# G131 THE ATTACK CURSOR: WHEN IT APPEARS, AND THE THREE STATES WHERE IT MUST NOT.
#
# LEGS 1 TO 3 ARE A RULE CHANGE. Arming the A verb used to draw the engine's guard-area
# shape over anything that was not an enemy, on the argument that guard-area is literally
# the order the click sends. What was asked for is the ATTACK cursor for as long as the
# verb is armed, over ground and over the player's own units as much as over an enemy,
# because that is the question the player is asking the pointer. G119 owns what the CLICK
# does and is untouched by this; only its one cursor leg follows the new shape.
#
# LEG 4 IS THE PRICE AND THE PROOF THAT IT IS AFFORDABLE. The armed state used to be
# readable off the guard shape. It still has a shape of its own, the other way round:
# the attack cursor over BARE GROUND with no modifier held cannot occur unarmed, because
# the engine's own answer for a cell is ACTION_MOVE and only ctrl turns that into force
# fire. So the leg drops the verb with the right button and asserts that the same cell
# goes back to reading move. If that ever stops being true the armed state has become
# invisible, and the gate says so in those words.
#
# LEGS 5 TO 7 ARE THE OTHER HALF OF THE REPORT, "the attack cursor is often not
# appearing". Each is a state in which the attack cursor is CORRECTLY absent, pinned here
# so that a build which starts showing it in one of them has to announce itself:
#
#   6. AN ENEMY ON GROUND THE PLAYER HAS NEVER UNCOVERED reads move, not attack. The
#      engine gates its own click path on that cell's Is_Visible before it looks for an
#      object, so the click really is a move and an attack cursor would be a promise the
#      click cannot keep. The leg reads the shroud entry for the unit's cell in the same
#      run, so the verdict is evidence rather than an assumption. It needs --noshroud,
#      which is a RENDERER switch and leaves the engine's shroud alone: an enemy that is
#      DRAWN and is not yet targetable is exactly the state being measured, and the
#      renderer reaches it in ordinary play too, because an object one cell into the dark
#      still draws dim rather than vanishing at a cell boundary.
#   7. NOTHING SELECTED reads select. There is no order to preview.
#
# LEG 8 IS A DEFECT THIS GATE WAS WRITTEN TO CATCH AGAIN. The cursor asks the engine's
# Best_Object_Action through a cache, and that cache used to be keyed on the SIZE of the
# selection rather than on what was in it. One unit exchanged for one other unit does not
# move the size, so with the pointer held still on a visible enemy, recalling a Medium
# Tank over a Harvester left the pointer reading MOVE for the full 15 engine ticks the age
# test allows -- one second at 15 Hz -- and the mirror left it reading ATTACK for a unit
# that carries no weapon. Control groups make that the common case, not a corner. The leg
# walks harvester, tank, harvester with no tick in between and asserts move, attack, move.
#
# THE GUARD SHAPE IS NOT ORPHANED BY LEGS 1 TO 3 AND NO LEG BELOW CAN SAY SO, which is
# stated rather than quietly skipped: ctrl+alt sends the same guard-area order with no
# verb armed and still previews it, at the shroud arm and again at the probe arm of the
# cursor resolution. update_cursor reads the live modifier state directly, so a script has
# no way to hold ctrl+alt down and that half is covered by reading the code, not by a leg.
#
# THE OBJECTS were read out of the running mission rather than chosen for looks. At tick
# 20 the Nod Light Tanks sit at cells 56,38 and 57,38 on ground the player has never
# uncovered, and the Nod rifleman at heap id 12 stands at 50,40 inside the GDI base's own
# sight. The rifleman is named by heap id so the pick cannot drift onto a neighbour, and
# the Light Tank leg dumps the cell it lands on before it measures the shape.
cat > /tmp/g131.txt <<'G131EOF'
# G131, through the same cursor verbs the live pointer runs: cursorcell and cursorobj move
# the same virtual mouse the SDL motion handler moves and call the same update_cursor.
tick 20
cam 54 51
zoom min
lclickobj MTNK 0
expectsel 1
echo G131-1 armed the attack cursor over BARE GROUND
amove
cursorcell 46 48
expectcursor attack
echo G131-2 armed it is still the attack cursor over the player's OWN unit
cursorobj MTNK 1
expectcursor attack
echo G131-3 armed over an enemy the player can see
cam 51 44
cursorobj E1:BadGuy#12 0
expectcursor attack
echo G131-4 the right button drops the verb and the same ground reads move again
rbutton 400 300
expectsel 1
cam 54 51
cursorcell 46 48
expectcursor move
echo G131-5 unarmed over that same visible enemy it is the attack cursor
cam 51 44
cursorobj E1:BadGuy#12 0
expectcursor attack
echo G131-6 unarmed over an enemy standing on ground the player has never seen
cam 56 40
obj LTNK:BadGuy 0
cursorobj LTNK:BadGuy 0
expectcursor move
shroudgate 56 38
echo G131-7 with nothing selected an enemy reads select and not attack
deselect
cursorobj E1:BadGuy#12 0
expectcursor select
echo G131-8 the preview follows WHAT is selected and not how many
cam 57 50
lclickobj MTNK 0
group 2 2
deselect
cam 49 45
lclickobj HARV 0
group 1 2
deselect
cam 51 43
group 1
expectsel 1
cursorobj E1:BadGuy#12 0
expectcursor move
group 2
expectsel 1
cursorobj E1:BadGuy#12 0
expectcursor attack
group 1
expectsel 1
cursorobj E1:BadGuy#12 0
expectcursor move
quit
G131EOF
CURLOG=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud --nosound \
             --script /tmp/g131.txt 2>&1)
CURRC=$?
echo "$CURLOG" | grep -E '^(ECHO|AMOVE|CURSORCELL|CURSOROBJ|EXPECTCURSOR|EXPECTSEL|OBJ|SHROUDAT)\|' >> "$OUT"
C1=$(echo "$CURLOG" | sed -n '/^ECHO|G131-1/,/^ECHO|G131-2/p')
C2=$(echo "$CURLOG" | sed -n '/^ECHO|G131-2/,/^ECHO|G131-3/p')
C3=$(echo "$CURLOG" | sed -n '/^ECHO|G131-3/,/^ECHO|G131-4/p')
C4=$(echo "$CURLOG" | sed -n '/^ECHO|G131-4/,/^ECHO|G131-5/p')
C5=$(echo "$CURLOG" | sed -n '/^ECHO|G131-5/,/^ECHO|G131-6/p')
C6=$(echo "$CURLOG" | sed -n '/^ECHO|G131-6/,/^ECHO|G131-7/p')
C7=$(echo "$CURLOG" | sed -n '/^ECHO|G131-7/,/^ECHO|G131-8/p')
C8=$(echo "$CURLOG" | sed -n '/^ECHO|G131-8/,$p')
CGND=$(echo "$C1" | grep -c '^EXPECTCURSOR|want=attack|have=attack|PASS$')
CARM=$(echo "$C1" | grep -c '^AMOVE|armed=1|armed|')
COWN=$(echo "$C2" | grep -c '^EXPECTCURSOR|want=attack|have=attack|PASS$')
CFOE=$(echo "$C3" | grep -c '^EXPECTCURSOR|want=attack|have=attack|PASS$')
CDIS=$(echo "$C4" | grep -c '^AMOVE|armed=0|right click|')
CKEPT=$(echo "$C4" | grep -c '^EXPECTSEL|want=1|have=1|PASS$')
CMOV=$(echo "$C4" | grep -c '^EXPECTCURSOR|want=move|have=move|PASS$')
CPLAIN=$(echo "$C5" | grep -c '^EXPECTCURSOR|want=attack|have=attack|PASS$')
CDARKCELL=$(echo "$C6" | grep -c '^OBJ|LTNK:BadGuy#0|UNIT|BadGuy|cell=56,38|')
CDARK=$(echo "$C6" | grep -c '^EXPECTCURSOR|want=move|have=move|PASS$')
CSHR=$(echo "$C6" | grep -c '^SHROUDAT|cell=56,38|visible=0|')
CSEL=$(echo "$C7" | grep -c '^EXPECTCURSOR|want=select|have=select|PASS$')
CSWAPM=$(echo "$C8" | grep -c '^EXPECTCURSOR|want=move|have=move|PASS$')
CSWAPA=$(echo "$C8" | grep -c '^EXPECTCURSOR|want=attack|have=attack|PASS$')
CCLEAN=$(echo "$CURLOG" | grep -c 'SCRIPT|end /tmp/g131.txt: .*, 0 failures')
if [ "$CURRC" = "0" ] && [ "${CARM:-0}" -ge 1 ] && [ "${CGND:-0}" -ge 1 ] && [ "${COWN:-0}" -ge 1 ] \
     && [ "${CFOE:-0}" -ge 1 ] && [ "${CDIS:-0}" -ge 1 ] && [ "${CKEPT:-0}" -ge 1 ] \
     && [ "${CMOV:-0}" -ge 1 ] && [ "${CPLAIN:-0}" -ge 1 ] && [ "${CDARKCELL:-0}" -ge 1 ] \
     && [ "${CDARK:-0}" -ge 1 ] && [ "${CSHR:-0}" -ge 1 ] && [ "${CSEL:-0}" -ge 1 ] \
     && [ "${CSWAPM:-0}" = "2" ] && [ "${CSWAPA:-0}" = "1" ] \
     && [ "${CCLEAN:-0}" = "1" ]; then
  ok "G131 attack cursor: with A armed the pointer is the attack cursor over bare ground, over the player's own tank and over an enemy alike; the right button drops the verb without dropping the selection and the same bare ground reads move again, which is the tell that the verb is gone; unarmed, a Nod rifleman the player can see reads attack, a Nod Light Tank at cell 56,38 that the player has never uncovered reads move because the engine's own click path is gated on that cell's visibility, and with nothing selected the same rifleman reads select; and the preview follows WHAT is selected rather than how many, so recalling a one-tank group over a one-harvester group flips that same rifleman from move to attack on the keypress instead of a second later"
else
  bad "G131 attack cursor: exit=$CURRC(want 0; non-zero on its own means an expectcursor leg went red, and the counts below name which) armed=$CARM(want >=1) armed-ground=$CGND(want >=1; zero with a move or guard in the log means the armed arm is not forcing the attack shape) armed-own-unit=$COWN(want >=1) armed-enemy=$CFOE(want >=1) right-click-disarm=$CDIS(want >=1) selection-kept=$CKEPT(want >=1; zero means the right button deselected instead of only disarming, and the move below is measuring the wrong state) disarmed-ground=$CMOV(want >=1; zero means the armed tell is gone -- the ground reads the same armed and unarmed) unarmed-enemy=$CPLAIN(want >=1) dark-tank-at-56-38=$CDARKCELL(want >=1; zero means the Light Tank moved and the leg below is aimed at the wrong object) unseen-enemy=$CDARK(want >=1; NON-ZERO IS THE HEALTHY STATE HERE -- a want=attack here would mean the pointer is promising a shot the engine turns into a move) shroud-says-unseen=$CSHR(want >=1; zero means that cell is explored in this run and the leg proves nothing) no-selection=$CSEL(want >=1) group-swap-move=$CSWAPM(want 2) group-swap-attack=$CSWAPA(want 1; zero means the probe cache is serving the PREVIOUS selection's answer -- it is keyed on the selection's identity, and a key that is only its SIZE cannot see one unit exchanged for one other unit) script-clean=$CCLEAN(want 1)"
fi

# G134 THE HARVESTER WORKS ITS FANGS WHILE IT HARVESTS, AND NOTHING ELSE MOVES.
#
# The report: a harvester lifting Tiberium stands there with its arms dead. It did, and
# deliberately. G82 pinned every vehicle's baked clip to frame 0 because nothing in the
# simulation drove it, and that is the CLOSED-GATE half of the cartridge's own vehicle arm
# (UnitClass::Draw3D's motion path, RAM 0x800140C0). The open half is now written and it
# belongs to the harvester alone: the clip runs only while IsHarvesting is set, the facing
# has settled, there is no NavCom and the unit is not driving, which are the same four the
# 1995 engine tests at unit.cpp:2126. The brain exports all four plus the unit's own stage
# counter; the frame is the console's own f = (stage * 10) % 80, folded back down above 40.
#
# SCB13EC, 2560x1440, N64 camera at maximum magnification, engine ticks 180 and 190. The
# subject is HARV id 29 at cell 40,55, which holds that cell and facing 32 across the whole
# window; APC id 9 parked on Guard at 7,60 is the control that must STILL be frozen. The
# crops are G82's to the pixel, so the two gates measure the same rectangles:
#   HARV id 29  crop 1301,591..1456,644      APC id 9  crop 1164,614..1238,693
# The harvester crop stops at row 644 for G82's reason: the Tiberium under its lower half
# is being eaten and changes between the two ticks whatever the vehicle does.
#
# THE TICK PAIR IS NOT ARBITRARY, AND TWO OBVIOUS ONES DO NOT WORK. UnitClass::Harvesting
# does Set_Stage(0); Set_Rate(2) on every lift (unit.cpp:2469-2470), so the counter
# restarts at each lift and steps once per two ticks: measured on this mission it reads 0
# at tick 180 and 3 at tick 190, i.e. clip frames 0 and 30. Tick 200 is the next lift and
# reads stage 0 again, which is why G82's own 180/200 pair still measures zero pixels on
# this same harvester; and stage 4 is frame 40, the closing key of a 41-frame loop, which
# draws the same pose as frame 0. Both were measured before this pair was chosen.
#
# MEASURED, before against after, same crops, same ticks:
#   HARV id 29   0 px  ->  81 px      the fangs move
#   APC  id 9    0 px  ->   0 px      every other vehicle is still held at frame 0
# and two runs of the after binary are byte identical in both crops.
#
# THE DUMP LEGS ARE THE HALF A PICTURE CANNOT SHOW. All four conditions must read the way
# the arm expects on both ticks, and EVERY OTHER VEHICLE IN THE MISSION must report harv=0
# with its counter pinned at 0 -- 34 units per dump, 68 across the two. That count is what
# stops the freeze coming back through the wrong door: if a counter ever started running on
# a tank, the two pixel numbers above would still pass.
cat > /tmp/g134_harv.txt <<'G134EOF'
tick 180
zoom max
obj HARV 1
obj APC 0
drawpos HARV 1
drawpos APC 0
objdump
cam 40 56
shot shots/g134_harv_a.png
cam 8 61
shot shots/g134_apc_a.png
tick 10
obj HARV 1
obj APC 0
drawpos HARV 1
drawpos APC 0
objdump
cam 40 56
shot shots/g134_harv_b.png
cam 8 61
shot shots/g134_apc_b.png
quit
G134EOF
gbegin shots/g134_harv_a.png shots/g134_harv_b.png shots/g134_apc_a.png shots/g134_apc_b.png
grun shots/g134.log --noshroud --nosound --scen SCB13EC --pack SCB13EC.pack $BASE \
     --w 2560 --h 1440 --script /tmp/g134_harv.txt
gshots shots/g134_harv_a.png shots/g134_harv_b.png shots/g134_apc_a.png shots/g134_apc_b.png
HVSTILL=$(grep -c -e '^OBJ|HARV#1|UNIT|GoodGuy|cell=40,55|str=600/600|mission=Harvest|face=32$' \
                  -e '^OBJ|APC#0|UNIT|GoodGuy|cell=7,60|str=200/200|mission=Guard|face=0$' \
                  -e '^DRAWPOS|HARV|29|ex=40.5000|ez=55.5000|wx=40.5000|wz=55.5000|smooth=0|alpha=0.000$' \
                  -e '^DRAWPOS|APC|9|ex=7.5000|ez=60.5000|wx=7.5000|wz=60.5000|smooth=0|alpha=0.000$' \
                  shots/g134.log 2>/dev/null)
HVGATE=$(grep '^OBJ|UNIT|HARV|' shots/g134.log 2>/dev/null \
         | grep -c '|id=29|.*|harv=1|rotating=0|navset=0|driving=0|')
HVSTAGE=$(grep '^OBJ|UNIT|HARV|' shots/g134.log 2>/dev/null \
          | sed -n 's/.*|id=29|.*|dostage=\([-0-9]*\)|.*/\1/p' | tr '\n' ',')
HVFROZE=$(grep '^OBJ|UNIT|' shots/g134.log 2>/dev/null | grep -v '^OBJ|UNIT|HARV|' \
          | grep -c '|dostage=0|.*|harv=0|')
HVPIX=$(python3 - <<'PY3'
from PIL import Image
import numpy as np
def differing(a, b, box):
    A = np.asarray(Image.open(a).convert('RGB').crop(box))
    B = np.asarray(Image.open(b).convert('RGB').crop(box))
    return int((A != B).any(axis=2).sum())
print("%d %d" % (
    differing('shots/g134_harv_a.png', 'shots/g134_harv_b.png', (1301, 591, 1456, 644)),
    differing('shots/g134_apc_a.png',  'shots/g134_apc_b.png',  (1164, 614, 1238, 693))))
PY3
)
set -- $HVPIX
HVHARV="$1"; HVAPC="$2"
if [ "$GRC" != "0" ]; then
  bad "G134 harvester fangs: the run failed or wrote no shot (GRC=$GRC)"
elif [ "$HVSTILL" = "8" ] && [ "$HVGATE" = "2" ] && [ "$HVSTAGE" = "0,3," ] \
   && [ "${HVFROZE:-0}" = "68" ] && [ "${HVHARV:-0}" -gt 0 ] && [ "${HVAPC:-9}" = "0" ]; then
  ok "G134 harvester fangs: a harvester lifting Tiberium passes all four of the cartridge's gate conditions on both ticks, its own stage counter steps 0 to 3 over the ten ticks between them, and its arms redraw $HVHARV px (was 0) while the parked APC beside it stays byte-identical and all 68 other vehicle readings hold harv=0 with the counter at 0"
else
  bad "G134 harvester fangs: subjects-held-still=$HVSTILL(want 8) gate-open-both-ticks=$HVGATE(want 2; the four are harv=1 rotating=0 navset=0 driving=0, and 0 here means the brain is not exporting them at all) stage=$HVSTAGE(want 0,3,) other-vehicles-frozen=$HVFROZE(want 68; lower means a stage counter has started running on something that is not a harvester) harv-px=$HVHARV(want >0; ZERO IS THE REPORT THIS GATE EXISTS FOR -- the fangs are dead again) apc-px=$HVAPC(want 0; non-zero means the frame-0 hold has been lifted for every vehicle again)"
fi


# ---------------------------------------------------------------------------------
# G137 TURNING FOG OF WAR OFF REACHES THE CURSOR.
#
# THE SYMPTOM. With the Fog Of War switch turned off in the cheat menu, the attack cursor
# often did not appear over an enemy. The CLICK still attacked. So the pointer was denying
# an order the engine was about to accept, which is the one thing a preview may not do.
#
# THE CAUSE WAS TWO EXPORTS IN THE SAME ENGINE FILE DISAGREEING. Debug_Unshroud is the
# engine's own "show the whole map" flag and the cheat sets it correctly. The engine's own
# cursor path honours it (display.cpp:3310, 3500 and 3576), the object export honours it
# ("Debug_Map || Debug_Unshroud || ..."), the cell-draw export honours it
# ("... || cellptr->IsVisible || Debug_Unshroud") -- and Get_Shroud_State did not. So the
# shroud SNAPSHOT this side reads went on reporting every unexplored cell unseen while
# everything else in the engine had already lifted the fog. The cursor gate is built on
# that snapshot, stayed in fog, chose the move pointer and never asked the probe.
# The fix is in the export, so every reader of the snapshot gets it rather than the cursor
# alone. Get_Shroud_State now folds Debug_Unshroud into IsVisible and IsMapped.
#
# THE CELL. SCG90EA, cell 56,38, a Nod Light Tank. Row 38 is Nod ground the GDI player has
# never lifted at tick 20 -- the same fact G119's leg 6 rests on, and for the same reason
# -- so this is a cell that is genuinely UNEXPLORED rather than merely out of sight, which
# is the case the fix is about.
#
# THE TWO RUNS ARE THE WHOLE ARGUMENT AND NEITHER IS OPTIONAL.
#
#   FOG ON is the ordinary game and it must be untouched. The export still reports the cell
#   unseen, the pointer is the move pointer, the order line says MOVE|shroud, and the click
#   really is degraded to a move by the engine: a live NavCom and NO TarCom.
#
#   FOG OFF is the fix. The export reports the cell visible AND mapped, the pointer offers
#   the attack, the order line says ATTACK with no shroud note, and the tank ends the leg
#   on MISSION_ATTACK with a live TarCom. What makes that last line mean anything is the
#   dump taken BEFORE the click, which must read guarding with no destination and no
#   target: without it a tank that had acquired a target by itself prints the same word.
#
#   AND THE FLAG IS READ, NOT LATCHED. Switching fog back on inside the same run puts the
#   export back to unseen, so this is a live term rather than a one-way door.
#
# WHAT DOES NOT CHANGE, and it was measured rather than assumed. The renderer's own shroud
# projection is a SECOND snapshot with its own master switch, and the cheat already turns
# that off, so the placement-blocker naming, the health-bar cull, the wall drawing, the
# radar plot and the decal and tiberium shading all read "clear" with the cheat on, both
# before the engine change and after it. With fog off, the whole run log and the rendered
# frame come out identical either side of the change; with fog on, so do they.
cat > /tmp/g137_on.txt <<'G137AEOF'
# The ordinary game. Nothing here may move.
tick 20
cam 54 46
zoom min
shroudgate 56 38
lclickobj MTNK 0
cursorcell 56 38
actcell 56 38
tick 5
seldump
quit
G137AEOF
cat > /tmp/g137_off.txt <<'G137BEOF'
# Fog Of War switched off from the cheat page, through the same switch the checkbox drives.
tick 20
cam 54 46
zoom min
cheatset fog 0
tick 5
shroudgate 56 38
lclickobj MTNK 0
seldump
cursorcell 56 38
actcell 56 38
tick 5
seldump
echo G137-backon
cheatset fog 1
tick 5
shroudgate 56 38
quit
G137BEOF
gbegin
grun /tmp/g137_on.log  --scen SCG90EA --pack SCG01EA.pack $BASE --nosound --script /tmp/g137_on.txt
grun /tmp/g137_off.log --scen SCG90EA --pack SCG01EA.pack $BASE --nosound --script /tmp/g137_off.txt
# fog ON: the control
FGONSEL=$(grep -c '^SELECT|.*|type=MTNK|house=GoodGuy|.*|selected$' /tmp/g137_on.log)
FGONSH=$(grep -c '^SHROUDAT|cell=56,38|visible=0|mapped=0|shadow=-1|gate=shrouded$' /tmp/g137_on.log)
FGONCUR=$(grep -c '^CURSORCELL|cell=56,38|.*|shape=move$' /tmp/g137_on.log)
FGONORD=$(grep -c '^ORDER|.*|cell=56,38|.*|target=LTNK|probe=5|MOVE|shroud$' /tmp/g137_on.log)
FGONSD=$(grep -c '^SEL|UNIT|MTNK|.*|navcell=4920|tarcell=-1$' /tmp/g137_on.log)
# fog OFF: the fix
FGOFFSH=$(sed -n '1,/^ECHO|G137-backon/p' /tmp/g137_off.log \
          | grep -c '^SHROUDAT|cell=56,38|visible=1|mapped=1|shadow=-1|gate=visible$')
FGOFFCUR=$(grep -c '^CURSORCELL|cell=56,38|.*|shape=attack$' /tmp/g137_off.log)
FGOFFORD=$(grep -c '^ORDER|.*|cell=56,38|.*|target=LTNK|probe=5|ATTACK$' /tmp/g137_off.log)
FGOFFIDLE=$(grep -c '^SEL|UNIT|MTNK|.*|mission=Guard|navcell=-1|tarcell=-1$' /tmp/g137_off.log)
FGOFFATK=$(grep -c '^SEL|UNIT|MTNK|.*|mission=Attack|.*|tarcell=4920$' /tmp/g137_off.log)
FGBACK=$(sed -n '/^ECHO|G137-backon/,$p' /tmp/g137_off.log \
         | grep -c '^SHROUDAT|cell=56,38|visible=0|mapped=0|shadow=-1|gate=shrouded$')
FGCLEAN=$(grep -c 'SCRIPT|end /tmp/g137_off.txt: .*, 0 failures' /tmp/g137_off.log)
if [ "$GRC" != "0" ]; then
  bad "G137 fog of war reaches the cursor: a run itself failed (GRC=$GRC) -- every number below would be the previous suite run's"
elif [ "${FGONSEL:-0}" -ge 1 ] && [ "${FGONSH:-0}" = "1" ] && [ "${FGONCUR:-0}" = "1" ] \
     && [ "${FGONORD:-0}" = "1" ] && [ "${FGONSD:-0}" -ge 1 ] \
     && [ "${FGOFFSH:-0}" = "1" ] && [ "${FGOFFCUR:-0}" = "1" ] && [ "${FGOFFORD:-0}" = "1" ] \
     && [ "${FGOFFIDLE:-0}" -ge 1 ] && [ "${FGOFFATK:-0}" -ge 1 ] && [ "${FGBACK:-0}" = "1" ] \
     && [ "${FGCLEAN:-0}" = "1" ]; then
  ok "G137 fog of war reaches the cursor: with the cheat ON, cell 56,38 -- ground this player has never seen -- exports visible=1 mapped=1, the pointer over the Nod Light Tank standing there offers the ATTACK, the order line says ATTACK rather than MOVE|shroud, and the click puts an idle Medium Tank on MISSION_ATTACK with a live TarCom; switching fog back on returns the export to unseen; and with the cheat OFF the same cell exports visible=0, the pointer is the move pointer and the identical click is degraded to a move with no target"
else
  bad "G137 fog of war reaches the cursor: fogon-selected=$FGONSEL(want >=1; zero means the tank was never picked and nothing below is about the right unit) fogon-shrouded=$FGONSH(want 1) fogon-cursor-move=$FGONCUR(want 1) fogon-order-MOVE|shroud=$FGONORD(want 1) fogon-moved-no-target=$FGONSD(want >=1) FOGOFF-visible=$FGOFFSH(want 1; ZERO IS THE BUG ITSELF -- Get_Shroud_State is not folding in Debug_Unshroud, so our copy of the map still says this cell is unseen while the engine says it is not) fogoff-cursor-attack=$FGOFFCUR(want 1) fogoff-order-ATTACK=$FGOFFORD(want 1) fogoff-idle-before=$FGOFFIDLE(want >=1; zero means the tank was NOT idle before the click, so the attack line proves nothing) fogoff-attacked=$FGOFFATK(want >=1) back-on-shrouded=$FGBACK(want 1; zero means the export latched instead of reading the flag) script-clean=$FGCLEAN(want 1)"
fi

# G132 HOLD THE RIGHT BUTTON TO DRAG THE CAMERA.
#
# The middle button has panned by grab-and-drag from the start. The right button now does
# the same, and the whole difficulty is that the right button is already busy: on the map
# it walks the cancel ladder, on the radar it navigates, on the build column it holds or
# cancels production, and G119 asserts that an armed attack-move leaves on it. So a right
# press only becomes a pan by travelling past a threshold before it comes up, and the
# click it would otherwise have been is delivered on RELEASE, at the pixel it went DOWN.
#
# THE ONE EXCEPTION IS A MARQUEE, and leg 12 is the whole reason it exists. A band is
# drawn by holding the LEFT button and moving, so a right press made to abandon one is
# guaranteed to travel: deferring that click to the release would not delay it, it would
# lose it. While the left button is down the deferral is skipped and the press goes down
# the ladder at once. Down the LADDER, from the top, not straight at the band: leg 13 is
# the measurement of that, and it is the reason the band rung is reached only where it
# always was.
#
# THREE VERBS, NOT ONE, and that is the point of them: rdown / rmove / rup put a MEASURED
# amount of travel between the press and the release, which is the only place the decision
# can be seen. `rbutton` calls the cancel ladder directly and cannot reach it at all.
# mdown / mmove / mup are the same three for the middle button, and exist so the rule that
# the FIRST button to start a drag keeps it can be measured instead of asserted in prose.
#
# --forceradar --w 1280 --h 960 for the same reason G110 uses them: without the radar lit
# minimap_hit answers false everywhere and legs 7 and 13 would be measuring nothing. EVERY
# PIXEL BELOW WAS MEASURED against this scenario at this size, by asking this binary:
#   * the bar is 80 DOS columns at scale 4, so sb_layout puts its left edge at x=960 and
#     the tactical view is 0..959. 500,500 is well inside it and `sbhit 500 500` says none.
#   * the radar plate for SCG90EA covers 1008,112 224x272 -- a `minimap` verb that misses
#     prints that rectangle -- so its centre is 1120,248, which is the pixel legs 7 and 13
#     press. A press there prints MINIMAP|jump, which is each leg's own proof that the
#     pixel is still on the plate if this layout ever moves.
#   * `sbhit 1100 700` says item column=0 slot=2: the build column, on the bar and below
#     the plate, which is the pixel leg 6 presses.
cat > /tmp/g132.txt <<'G132EOF'
tick 20
cam 54 51
zoom min
echo G132-1 a right press that does not move is the cancel it always was
lclickobj MTNK 0
expectsel 1
rdown 500 500
rup
expectsel 0
echo G132-2 a right press that travels pans and cancels nothing
lclickobj MTNK 0
expectsel 1
cam 54 51
rdown 500 500
rmove 800 500
rmove 900 500
rpush 30
rup
expectsel 1
echo G132-3 a right drag PUSHES the view and the middle button still GRABS
cam 54 51
rdown 500 500
rmove 900 500
rpush 30
rup
cam 54 51
drag 500 500 620 500
echo G132-4 under the threshold it is still a click
lclickobj MTNK 0
expectsel 1
cam 54 51
rdown 500 500
rmove 502 502
rup
expectsel 0
echo G132-5 out and back is still a drag
lclickobj MTNK 0
expectsel 1
cam 54 51
rdown 500 500
rmove 620 500
rmove 500 500
rup
expectsel 1
echo G132-6 a press on the build column can never pan
cam 54 51
rdown 1100 700
rmove 500 500
rup
echo G132-7 a press on the radar still navigates and never pans
cam 54 51
rdown 1120 248
rmove 500 500
rup
echo G132-8 an armed attack-move still leaves on a right click
cam 54 51
deselect
lclickobj MTNK 0
amove
rdown 500 500
rup
echo G132-9 but a right DRAG leaves the arm alone
cam 54 51
deselect
lclickobj MTNK 0
amove
rdown 500 500
rmove 620 500
rup
echo G132-10 the middle button owns the drag it started
cam 54 51
mdown 500 500
rdown 500 500
rmove 900 500
rpush 30
mmove 900 500
rup
mup
echo G132-11 and the right button owns the one IT started
lclickobj MTNK 0
expectsel 1
cam 54 51
rdown 500 500
rmove 900 500
mdown 900 500
mup
rmove 940 500
rpush 30
rup
expectsel 1
echo G132-12 a right press over an in-flight marquee still kills the marquee
cam 54 51
band 400 400 700 600
rdown 500 500
rmove 620 500
rup
echo G132-13 over chrome the ladder keeps its own order and the band survives
cam 54 51
band 400 400 700 600
rdown 1120 248
rup
cam 54 51
band 400 400 700 600
rdown 1100 700
rup
bandoff
quit
G132EOF
RDLOG=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud --nosound \
            --forceradar --w 1280 --h 960 --script /tmp/g132.txt 2>&1)
RDRC=$?
echo "$RDLOG" | grep -E '^(ECHO|RBTN|CAM\||CANCEL|MINIMAP|AMOVE|EXPECTSEL)' >> "$OUT"
# One reader per leg. rd_leg prints the lines between two echoes; rd_cam pulls the x and z
# off a named camera reading so two of them can be compared as strings.
rd_leg() { echo "$RDLOG" | sed -n "/^ECHO|G132-$1 /,/^ECHO|G132-$2 /p"; }
rd_cam() { echo "$1" | sed -n "s/^CAM|$2|\(x=[^|]*|z=[^|]*\).*/\1/p" | sed -n "${3:-1}p"; }
RD1=$(rd_leg 1 2);  RD2=$(rd_leg 2 3);  RD3=$(rd_leg 3 4);  RD4=$(rd_leg 4 5)
RD5=$(rd_leg 5 6);  RD6=$(rd_leg 6 7);  RD7=$(rd_leg 7 8);  RD8=$(rd_leg 8 9)
RD9=$(echo "$RDLOG" | sed -n '/^ECHO|G132-9 /,/^ECHO|G132-10 /p')
RD10=$(echo "$RDLOG" | sed -n '/^ECHO|G132-10 /,/^ECHO|G132-11 /p')
RD11=$(echo "$RDLOG" | sed -n '/^ECHO|G132-11 /,/^ECHO|G132-12 /p')
RD12=$(echo "$RDLOG" | sed -n '/^ECHO|G132-12 /,/^ECHO|G132-13 /p')
RD13=$(echo "$RDLOG" | sed -n '/^ECHO|G132-13 /,$p')
# 1: no travel -> the ladder ran, and the camera did not move across the whole press.
RD_CLICK=$(echo "$RD1" | grep -c '^CANCEL|deselect|was=1|now=0$')
RD_STILL1=$([ "$(rd_cam "$RD1" rdown)" = "$(rd_cam "$RD1" rup)" ] && echo 1 || echo 0)
# 2: travel -> a pan, and NOT a cancel. Both halves matter: a build that panned and also
# deselected would look fixed in a screenshot and be unusable in the hand.
RD_DRAG=$(echo "$RD2" | grep -c '^RBTN|drag|begin|')
RD_NOCANCEL=$(echo "$RD2" | grep -c '^CANCEL|')
RD_MOVED=$([ "$(rd_cam "$RD2" rdown)" != "$(rd_cam "$RD2" rup)" ] && echo 1 || echo 0)
# 3: THE PUSH. A right drag no longer grabs, so the rmove itself must move NOTHING: the
# view travels only while the push is spent, one step a frame, and it is armed only because
# the switch defaults on. The middle button's own grab is measured beside it as the control.
RD_P3A=$(rd_cam "$RD3" rmove); RD_P3B=$(rd_cam "$RD3" rpush)
RD_NOGRAB=$([ "$(rd_cam "$RD3" rdown)" = "$RD_P3A" ] && echo 1 || echo 0)
RD_PUSHED=$([ -n "$RD_P3B" ] && [ "$RD_P3A" != "$RD_P3B" ] && echo 1 || echo 0)
RD_PUSHARM=$(echo "$RD3" | grep -c '^RBTN|drag|begin|from=500,500|push=1|grabowner=0$')
RD_PUSHRAN=$(echo "$RD3" | grep -c '^RPUSH|frames=30|at=900,500|armed=1|moved=30|')
RD_R3="$RD_P3B"
RD_M3=$(rd_cam "$RD3" drag)
RD_MIDGRAB=$([ -n "$RD_M3" ] && echo 1 || echo 0)
# 4: two pixels of travel is jitter, not a gesture.
RD_SHAKY=$(echo "$RD4" | grep -c '^CANCEL|deselect|was=1|now=0$')
RD_SHAKYSTILL=$([ "$(rd_cam "$RD4" rdown)" = "$(rd_cam "$RD4" rup)" ] && echo 1 || echo 0)
# 5: the answer LATCHES. Dragging out and back is still a drag, and a release that fired
# the ladder there would lose the selection at the end of a pan.
RD_LATCH=$(echo "$RD5" | grep -c '^RBTN|up|drag|no click$')
RD_LATCHCANCEL=$(echo "$RD5" | grep -c '^CANCEL|')
# 6 and 7: chrome refuses the pan and keeps the click. nopan=1 is the latch, the equal
# camera readings are the refusal, and MINIMAP|jump is the radar doing its own job.
RD_SBNOPAN=$(echo "$RD6" | grep -c '^RBTN|down|at=1100,700|nopan=1|')
RD_SBSTILL=$([ "$(rd_cam "$RD6" rdown)" = "$(rd_cam "$RD6" rmove)" ] && echo 1 || echo 0)
RD_MMNOPAN=$(echo "$RD7" | grep -c '^RBTN|down|at=1120,248|nopan=1|')
RD_MMSTILL=$([ "$(rd_cam "$RD7" rdown)" = "$(rd_cam "$RD7" rmove)" ] && echo 1 || echo 0)
RD_MMJUMP=$(echo "$RD7" | grep -c '^MINIMAP|jump|by=right|')
# 8 and 9: the G119 rung, both ways round.
RD_AMDIS=$(echo "$RD8" | grep -c '^AMOVE|armed=0|right click|')
RD_AMKEPT=$(echo "$RD9" | grep -c '^AMOVE|armed=0|')
# 10 and 11: one drag at a time, owned by the button that started it.
RD_OWNM=$(echo "$RD10" | grep -c '^RBTN|drag|begin|from=500,500|push=0|grabowner=2$')
RD_MSTILL=$([ "$(rd_cam "$RD10" rdown)" = "$(rd_cam "$RD10" rpush)" ] && echo 1 || echo 0)
RD_MPAN=$([ "$(rd_cam "$RD10" mmove)" != "$(rd_cam "$RD10" rdown)" ] && echo 1 || echo 0)
RD_OWNR=$(echo "$RD11" | grep -c '^RBTN|drag|begin|from=500,500|push=1|grabowner=0$')
RD_RKEPT=$([ "$(rd_cam "$RD11" rpush)" != "$(rd_cam "$RD11" mup)" ] && echo 1 || echo 0)
# 12: THE MARQUEE. band=1->0 is the ladder's third rung firing on the PRESS, which is the
# only moment it can fire in: the hand is already moving, so a deferred click would be
# read as travel and thrown away. The press is spent as well as delivered -- no drag
# begins from it, and the camera is where it was after 120 pixels of travel.
RD_MQKILL=$(echo "$RD12" | grep -c '^RBTN|down|at=500,500|left button down: click now|band=1->0$')
RD_MQNODRAG=$(echo "$RD12" | grep -c '^RBTN|drag|begin|')
RD_MQSTILL=$([ "$(rd_cam "$RD12" rdown)" = "$(rd_cam "$RD12" rup)" ] && echo 1 || echo 0)
# Any RBTN|up at all, of either kind: a press spent on the way down leaves nothing for
# the release to resolve, so the correct count is zero and not "one of the right sort".
RD_MQUP=$(echo "$RD12" | grep -c '^RBTN|up|')
# 13: THE LADDER'S ORDER IS UNTOUCHED, which is the other half of leg 12 and the reason
# the delivery is immediate rather than rewritten. The band rung is the THIRD rung: over
# the radar the first rung takes the press and over the build column the second does, so
# on chrome the band SURVIVES -- exactly what it does today, and the measured proof that
# a press was not simply routed round the ladder to reach the band. band=1->1 twice is
# that, and MINIMAP|jump is the radar still navigating with a marquee in flight.
RD_MQCHROME=$(echo "$RD13" | grep -c '|left button down: click now|band=1->1$')
RD_MQRADAR=$(echo "$RD13" | grep -c '^MINIMAP|jump|by=right|')
RD_FAILS=$(echo "$RDLOG" | grep -c '|FAIL$')
RD_CLEAN=$(echo "$RDLOG" | grep -c 'lines, 0 failures$')
if [ "$RDRC" = "0" ] && [ "$RD_CLICK" -ge 1 ] && [ "$RD_STILL1" = "1" ] && \
   [ "$RD_DRAG" -ge 1 ] && [ "$RD_NOCANCEL" = "0" ] && [ "$RD_MOVED" = "1" ] && \
   [ "$RD_NOGRAB" = "1" ] && [ "$RD_PUSHED" = "1" ] && [ "$RD_PUSHARM" -ge 1 ] && \
   [ "$RD_PUSHRAN" -ge 1 ] && [ "$RD_MIDGRAB" = "1" ] && \
   [ "$RD_SHAKY" -ge 1 ] && [ "$RD_SHAKYSTILL" = "1" ] && \
   [ "$RD_LATCH" -ge 1 ] && [ "$RD_LATCHCANCEL" = "0" ] && \
   [ "$RD_SBNOPAN" -ge 1 ] && [ "$RD_SBSTILL" = "1" ] && \
   [ "$RD_MMNOPAN" -ge 1 ] && [ "$RD_MMSTILL" = "1" ] && [ "$RD_MMJUMP" -ge 1 ] && \
   [ "$RD_AMDIS" -ge 1 ] && [ "$RD_AMKEPT" = "0" ] && \
   [ "$RD_OWNM" -ge 1 ] && [ "$RD_MSTILL" = "1" ] && [ "$RD_MPAN" = "1" ] && \
   [ "$RD_OWNR" -ge 1 ] && [ "$RD_RKEPT" = "1" ] && \
   [ "$RD_MQKILL" -ge 1 ] && [ "$RD_MQNODRAG" = "0" ] && [ "$RD_MQSTILL" = "1" ] && \
   [ "$RD_MQUP" = "0" ] && [ "$RD_MQCHROME" -ge 2 ] && [ "$RD_MQRADAR" -ge 1 ] && \
   [ "$RD_FAILS" = "0" ] && [ "$RD_CLEAN" -ge 1 ]; then
  ok "G132 right-drag pans: a right press that does not travel is still the cancel ladder and leaves the camera exactly where it was, a press that travels past the threshold PUSHES the view instead and cancels nothing: the motion itself moves nothing, the push arms with the switch on its own default, and thirty spent frames land the camera on $RD_R3 while the middle button's own grab verb still lands on $RD_M3, two pixels of jitter is still a click, a drag that returns to its own start is still a drag, a press on the build column at 1100,700 or on the radar plate at 1120,248 (measured 1008,112 224x272) can never pan and the radar still navigates, an armed attack-move still leaves on a right CLICK and survives a right DRAG, whichever button starts a drag keeps it while the other is held, and a right press made over an IN-FLIGHT MARQUEE kills the marquee on the press itself, without panning and without a second delivery on the release, while over the radar and the build column the same press keeps the ladder's own order and the band lives on"
else
  bad "G132 right-drag pans: rc=$RDRC(want 0) click-cancels=$RD_CLICK(want >=1) click-camera-still=$RD_STILL1(want 1) drag-began=$RD_DRAG(want >=1) drag-cancels=$RD_NOCANCEL(want 0; NON-ZERO MEANS THE PAN ALSO DROPPED THE SELECTION, which is the whole defect) drag-moved-camera=$RD_MOVED(want 1) right-motion-alone-grabbed-nothing=$RD_NOGRAB(want 1) push-moved-the-view=$RD_PUSHED(want 1; before=$RD_P3A after=$RD_P3B) push-armed-by-default=$RD_PUSHARM(want >=1) push-spent-30-frames=$RD_PUSHRAN(want >=1) middle-button-still-grabs=$RD_MIDGRAB(want 1; middle=$RD_M3) jitter-still-a-click=$RD_SHAKY(want >=1) jitter-camera-still=$RD_SHAKYSTILL(want 1) out-and-back-latched=$RD_LATCH(want >=1) out-and-back-cancels=$RD_LATCHCANCEL(want 0) sidebar-nopan=$RD_SBNOPAN(want >=1) sidebar-camera-still=$RD_SBSTILL(want 1) radar-nopan=$RD_MMNOPAN(want >=1) radar-camera-still=$RD_MMSTILL(want 1) radar-navigated=$RD_MMJUMP(want >=1) attackmove-disarmed-by-click=$RD_AMDIS(want >=1) attackmove-disarmed-by-drag=$RD_AMKEPT(want 0) middle-owns=$RD_OWNM(want >=1) right-declined-while-middle-owns=$RD_MSTILL(want 1) middle-still-pans=$RD_MPAN(want 1) right-owns=$RD_OWNR(want >=1) right-kept-across-a-middle-tap=$RD_RKEPT(want 1) marquee-killed-on-the-press=$RD_MQKILL(want >=1; ZERO MEANS A RIGHT PRESS NO LONGER CANCELS A MARQUEE, and the hand is guaranteed to be moving in that case) marquee-press-began-a-drag=$RD_MQNODRAG(want 0) marquee-camera-still=$RD_MQSTILL(want 1) marquee-press-still-live-at-the-release=$RD_MQUP(want 0; ANY RBTN|up line means the press was still latched instead of spent where it landed) chrome-kept-the-ladder-order=$RD_MQCHROME(want >=2; the radar and sidebar rungs sit ABOVE the band rung and must still take the press) marquee-radar-still-navigated=$RD_MQRADAR(want >=1) script-fails=$RD_FAILS(want 0) script-clean=$RD_CLEAN(want >=1). An empty click-cancels with everything else empty means the rdown/rmove/rup verbs are missing from the binary in $RUNDIR"
fi

# =====================================================================================
# G133 THE CAMERA PULLS BACK FURTHER, AND A DYING VEHICLE ONLY TAPS THE GROUND.
#
# Two changes, one gate, because they answer the same complaint: the view is too close
# and the ground is too still.
#
# THE ZOOM. The cartridge's own record is 2400..3800 leptons (ResetZoomLimits) and it is
# NOT retuned -- N64_DIST_MAX still says 3800 and still cites the RAM address it came
# from. The extra range is a SEPARATE, NAMED, REVERTIBLE constant beside it, quoted in
# the console's own step (4 * N64_DIST_STEP = 400), and the far reach is the sum. So the
# legs are shaped to prove BOTH halves: that the new range is reachable, and that the
# cartridge's half of it did not move by a lepton. The pitch is the delicate part. 0.78
# and 0.92 radians are the ROM's two endpoints and the lerp between them is normalised on
# the ROM's OWN span, so out in the extra range it would EXTRAPOLATE a tilt the console
# never produced. It is clamped instead: past 3800 the camera holds 52.712 degrees and
# only pulls back, which is why every shot gate still frames what it framed.
#
# THE SHAKE. A building death has jolted the camera from the start, scaled by footprint.
# A vehicle death did nothing at all, and the comment saying so gave the reason: a
# firefight kills several a second, and a camera re-jolted on every one is unusable. So
# the tap is added with the rule that answers that objection, and that rule is what this
# gate is really about. shake_tap replaces a live shake only on STRICTLY GREATER
# amplitude, where the building path's shake_note replaces on >=. One character, and it
# is the whole difference between "twenty tanks peak at one tank's throw" and "the camera
# sits at peak for the entire battle". The re-arm leg below is not decorative: rebuilt
# with that one character flipped back to >=, re-armed goes from 0 to 118 and the
# window-sample count goes to 0, so the gate reports the regression twice over.
#
# THE CONTROL RUN'S PREMISE IS MEASURED, NOT ASSERTED IN PROSE. An earlier draft declared
# that no building dies in its control script. One does: a GoodGuy Guard Tower, id 8, at
# engine frame 898, which would have had the leg quietly reading a building's 0.0280
# while claiming to have proved something about vehicles. Both halves are fixed. RUN A
# stops at frame 860 -- 38 short of that death -- AND counts the building deaths anyway,
# so the premise fails loudly the day it stops holding. RUN B runs long past it and does
# not treat a dying building as an excess to be flagged: it ACCOUNTS for it. Every sample
# is matched to the death that caused it BY FRAME NUMBER, which is exact because both
# shake calls are made from the same pass that announces the death. A shake whose start
# frame carries a DEATHSHED is a building's and must read 0.028 per footprint cell; one
# that does not is a vehicle's and must read exactly 0.0100. Anything else is a sum, and
# a sum is the regression.
#
# WHAT IT DOES NOT COVER, said here rather than left to be found: an aircraft coming
# apart in mid-air is filtered out in the source (K_UNIT only) and no leg here exercises
# it, because across RUN B's 4220 ticks the only aircraft candidates are three C-17s and
# every one of them is refused as skip-nobang before the question arises. The guard is
# latent rather than vacuous: the accounting matches every tap to a kind=1 shatter on its
# own frame, so the day an aircraft does tap it lands as an unaccounted sample.
G133Z=/tmp/g133_zoom.txt
cat > "$G133Z" <<'G133ZEOF'
tick 5
dist 2400
dist 2733
dist 3000
dist 3417
dist 3800
dist 3900
dist 4200
dist 5000
dist 1000
zoom min
zoom max
zoom 4000
zoom 4300
quit
G133ZEOF
# THE TWO SHAKE SCRIPTS ARE GENERATED. A shake is nine ticks long and the only window on
# it is a verb, so the sampling has to be dense enough to land inside one -- hundreds of
# tick/shakedump pairs, which is not a thing to keep by hand in a .txt.
#
# RUN A, THE CONTROL: the same eight force-fire orders G92 uses to kill a tank on this
# map, then 180 samples two ticks apart, ending at engine frame 860. Measured on this
# map: the Medium Tank dies at 737, a Buggy at 841, and the first BUILDING at 898, which
# is why the sample loop ends where it does.
#
# RUN B, THE BATTLE: no orders at all, both sides already in contact, a 20-tick settle
# and then 4200 more sampled every second tick, ending at engine frame 4220. Measured:
# 8 vehicles and 6 buildings die, and two of the vehicles die three frames apart (3794
# and 3797) -- that pair is what the anti-re-arm rule exists for and the only reason this
# run is as long as it is.
python3 - <<'G133PY'
o = ["tick 20", "cam 57 51", "zoom max", "shakedump"]
for r in range(8):
    o += ["lclickobj MTNK 0", "actclickobj MTNK 3 c", "tick 60"]
for k in range(180):
    o += ["tick 2", "shakedump"]
open("/tmp/g133_tap.txt", "w").write("\n".join(o + ["quit"]) + "\n")
b = ["tick 20", "cam 57 51"]
for k in range(2100):
    b += ["tick 2", "shakedump"]
open("/tmp/g133_batt.txt", "w").write("\n".join(b + ["quit"]) + "\n")
G133PY
G133ZL=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud \
             --script "$G133Z" 2>&1)
G133ZRC=$?
G133AL=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud \
             --script /tmp/g133_tap.txt 2>&1)
G133ARC=$?
G133BL=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud \
             --script /tmp/g133_batt.txt 2>&1)
G133BRC=$?
G133PL=$(./cnc_eyes --scen SCG01EC --pack SCG01EA.pack $BASE --picktest 2>&1)
G133PRC=$?
echo "$G133ZL" >> "$OUT"; echo "$G133AL" >> "$OUT"; echo "$G133BL" >> "$OUT"
# ---- THE ZOOM ----------------------------------------------------------------------
# 1. THE WHOLE DISTANCE -> PITCH TABLE, IN ONE STRING, and it says four things at once:
#    the five distances inside the cartridge's own range report the pitches the ROM's
#    lerp has always given, to the three decimals CAM| prints; the three past it report
#    the ROM's own FAR tilt and not an extrapolated one (an unclamped lerp reaches 0.96
#    rad, 55.0 deg, at 4200); 5000 leptons is absent because it clamped to 4200; and 1000
#    is absent because it clamped to 2400. The old binary produces this same string with
#    3900, 4000 and 4200 all collapsed onto 3800.
G133TAB=$(printf '%s\n' "$G133ZL" | awk -F'|' '/^CAM\|/{d="";p="";
             for(i=1;i<=NF;i++){if($i~/^dist=/)d=substr($i,6);if($i~/^pitch=/)p=substr($i,7)}
             if(d!="")print d"@"p}' | sort -u | tr '\n' ' ')
# 2. `zoom min` STILL MEANS THE CARTRIDGE'S FAR LIMIT, deliberately. Every shot gate that
#    frames itself with this verb was measured at 3800, and a verb that followed the new
#    range out would reframe all of them in one line.
G133MIN=$(printf '%s\n' "$G133ZL" | grep -c '^CAM|zoom|.*|dist=3800.0|pitch=52.712$')
# 3. THE SCRIPT LAYER STOPS LYING ABOUT WHAT IT DID. 4000 leptons is now honoured, so it
#    must NOT be announced as clamped; 4300 is not, so it must be, and must name the range
#    that actually applies rather than the cartridge's.
G133QUIET=$(printf '%s\n' "$G133ZL" | grep -c '^SCRIPT|zoom 4000 ')
G133WARN=$(printf '%s\n' "$G133ZL" | grep -c '^SCRIPT|zoom 4300 is outside the N64 range 2400..4200 leptons, clamped$')
# 4. THE DEVIATION NAMES ITSELF ON EVERY LAUNCH rather than only in a comment nobody runs.
G133BAN=$(printf '%s\n' "$G133ZL" | grep -c "far limit is 3800 and the last 400 are OURS, a registered deviation")
# 5. AND THE HAND-WRITTEN INVERSE WAS RE-PROVED AT THE NEW LIMIT. G1 asserts only that
#    --picktest says PASS, so it stays green with the far case still pinned at 3800. This
#    reads the case line itself, so the sweep is known to reach 4200.
G133PICK=$(printf '%s\n' "$G133PL" | grep -c '^UNPROJ| NW corner, our furthest .*dist=4200 .*mismatches 0,')
# ---- THE SHAKE ---------------------------------------------------------------------
# THE ACCOUNTANT. Every SHAKE| sample is matched to the death that caused it BY FRAME
# NUMBER: a shake whose start frame carries a DEATHSHED is a building's and must read
# 0.028 per footprint cell, and one that does not is a vehicle's and must read exactly
# the tap. A sum reads as neither and lands in OOA.
#
# SHADOW is the anti-re-arm leg and the only one that can tell shake_tap from shake_note.
# It looks for a vehicle death that lands inside a live tap's own nine ticks, with the
# tap before it a clean start, and then demands that the later death NEVER becomes the
# shake's frame. SEEN is its anti-vacuity partner: a sample actually taken inside that
# window, after the second death, so the leg is known to have looked.
G133ACC=$(printf '%s\n' "$G133BL" | python3 -c '
import sys, re
V = []; B = {}; S = []
for ln in sys.stdin:
    ln = ln.strip()
    m = re.match(r"^UNITDEATH\|\S+?\|id=\d+\|kind=(\d+)\|.*\|frame=(\d+)\|verdict=shatter$", ln)
    if m:
        if m.group(1) == "1": V.append(int(m.group(2)))
        continue
    m = re.match(r"^DEATHSHED\|(\w+)\|\w+\|id=\d+\|begin\|frame=(\d+)$", ln)
    if m: B[int(m.group(2))] = m.group(1); continue
    m = re.match(r"^SHAKE\|amp=([0-9.]+)\|frame=(-?\d+)\|age=(-?\d+)\|", ln)
    if m: S.append((float(m.group(1)), int(m.group(2)), int(m.group(3))))
TAP = 0.0100; CELL = 0.0280; TICKS = 9
deaths = sorted([(f, "V") for f in V] + [(f, "B") for f in B])
shadow = []
for i in range(1, len(deaths)):
    f, k = deaths[i]; pf, pk = deaths[i-1]
    if k != "V" or pk != "V" or not (0 < f - pf < TICKS): continue
    if i >= 2 and f - deaths[i-2][0] < TICKS: continue
    shadow.append((pf, f))
n = z = tap = bld = ooa = rearm = seen = 0
for a, f, age in S:
    n += 1
    if a == 0.0: z += 1
    elif f in B:
        bld += 1
        k = a / CELL
        if abs(k - round(k)) > 1e-6 or not 1 <= round(k) <= 4: ooa += 1
    else:
        tap += 1
        if abs(a - TAP) > 1e-9 or f not in V: ooa += 1
    for pf, f2 in shadow:
        if f == f2: rearm += 1
        if f == pf and f2 - pf <= age < TICKS: seen += 1
print("%d %d %d %d %d %d %d %d %d %d" % (n, z, tap, bld, ooa, len(V), len(B), len(shadow), rearm, seen))
print(",".join(sorted(set("%s=%.4f" % (B[f], a) for a, f, age in S if f in B))) or "none")
')
G133N=$(printf     '%s\n' "$G133ACC" | awk 'NR==1{print $1}')
G133TAPS=$(printf  '%s\n' "$G133ACC" | awk 'NR==1{print $3}')
G133BLD=$(printf   '%s\n' "$G133ACC" | awk 'NR==1{print $4}')
G133OOA=$(printf   '%s\n' "$G133ACC" | awk 'NR==1{print $5}')
G133VD=$(printf    '%s\n' "$G133ACC" | awk 'NR==1{print $6}')
G133BD=$(printf    '%s\n' "$G133ACC" | awk 'NR==1{print $7}')
G133SHAD=$(printf  '%s\n' "$G133ACC" | awk 'NR==1{print $8}')
G133REARM=$(printf '%s\n' "$G133ACC" | awk 'NR==1{print $9}')
G133SEEN=$(printf  '%s\n' "$G133ACC" | awk 'NR==1{print $10}')
G133SIZES=$(printf '%s\n' "$G133ACC" | awk 'NR==2{print $1}')
# RUN A, THE CONTROL. No building dies in this window and the leg SAYS SO IN NUMBERS
# rather than in prose: a nonzero count here means the run has drifted onto the Guard
# Tower at frame 898 and every "this is a vehicle" claim below it is void. Two vehicles
# die, the camera is still before the first of them, and no sample in the whole run reads
# anything but the tap.
G133ADS=$(printf '%s\n' "$G133AL" | grep -c '^DEATHSHED|')
G133AVD=$(printf '%s\n' "$G133AL" | grep -c '^UNITDEATH|.*|kind=1|.*|verdict=shatter$')
G133AZERO=$(printf '%s\n' "$G133AL" | grep -c '^SHAKE|amp=0.0000|')
G133ATAP=$(printf '%s\n' "$G133AL" | grep -c '^SHAKE|amp=0.0100|')
G133AOTHER=$(printf '%s\n' "$G133AL" | grep '^SHAKE|' | grep -vc -e '|amp=0.0000|' -e '|amp=0.0100|')
# AND THE F5 DIAL IS AT ITS SHIPPED DEFAULT, so the pixel figures in the source comment
# are figures for the build people actually run.
G133FX=$(printf '%s\n' "$G133AL" | grep -c '|on=1|scale=2.518|')
G133EXP="2400.0@44.691 2733.0@46.599 3000.0@48.128 3417.0@50.518 3800.0@52.712 3900.0@52.712 4000.0@52.712 4200.0@52.712 "
if [ "$G133ZRC" != "0" ] || [ "$G133ARC" != "0" ] || [ "$G133BRC" != "0" ] || [ "$G133PRC" != "0" ]; then
  bad "G133 zoom range and the vehicle tap: a run failed (exit $G133ZRC / $G133ARC / $G133BRC / $G133PRC). A nonzero on the second and third with a zero on the first and fourth is the ordinary way this reads on a STALE binary: shakedump is a verb this gate added, an older cnc_eyes rejects it, and the script layer counts that as a failure. Rebuild and restage before reading anything below"
elif [ "$G133TAB" = "$G133EXP" ] && [ "${G133MIN:-0}" = "1" ] && [ "${G133QUIET:-1}" = "0" ] \
     && [ "${G133WARN:-0}" = "1" ] && [ "${G133BAN:-0}" -ge 1 ] && [ "${G133PICK:-0}" = "1" ] \
     && [ "${G133ADS:-1}" = "0" ] && [ "${G133AVD:-0}" -ge 2 ] && [ "${G133AZERO:-0}" -ge 1 ] \
     && [ "${G133ATAP:-0}" -ge 1 ] && [ "${G133AOTHER:-1}" = "0" ] && [ "${G133FX:-0}" -ge 1 ] \
     && [ "${G133OOA:-1}" = "0" ] && [ "${G133TAPS:-0}" -ge 1 ] && [ "${G133BLD:-0}" -ge 1 ] \
     && [ "${G133VD:-0}" -ge 2 ] && [ "${G133BD:-0}" -ge 2 ] && [ "${G133SIZES}" = "GTWR=0.0280,PROC=0.0840,WEAP=0.0840" ] \
     && [ "${G133SHAD:-0}" -ge 1 ] && [ "${G133REARM:-1}" = "0" ] && [ "${G133SEEN:-0}" -ge 1 ]; then
  ok "G133 zoom range and the vehicle tap: the camera reaches 4200 leptons where the cartridge stopped at 3800, and holds the console's own 52.712 deg tilt out there instead of extrapolating one, while every distance the cartridge itself allowed still reports the pitch it always did and \`zoom min\` still pins to 3800 so no shot gate reframes; and a dying vehicle now taps the ground at 0.0100 against a Guard Tower's 0.0280 and a refinery's 0.0840 -- over $G133N samples of a $G133VD-vehicle $G133BD-building battle not one shake amplitude was a SUM, every one of them matched the death on its own frame, and where two tanks died three ticks apart the second never re-armed the first one's shake ($G133SEEN samples taken inside that window to prove it was looked at). The control run reached frame 860 with $G133ADS building deaths in it"
else
  bad "G133 zoom range and the vehicle tap: dist->pitch table=[$G133TAB] want [$G133EXP] (a 3800 where a 3900/4000/4200 should be is the far limit not widening; a pitch above 52.712 out there is the lerp extrapolating a tilt the console never had; a missing 2400 or a stray 5000 is a clamp gone) zoom-min-pins-3800=$G133MIN(want 1; a 0 here reframes every shot gate in the suite) false-clamp-warning-at-4000=$G133QUIET(want 0) real-warning-at-4300=$G133WARN(want 1, naming 2400..4200) banner-declares-the-deviation=$G133BAN(want >=1) picktest-at-4200=$G133PICK(want 1). RUN A control: buildings-died=$G133ADS(want 0 -- ANY other number means the run has drifted onto the Guard Tower that dies at frame 898 and this leg is no longer about vehicles at all; shorten the sample loop) vehicles-died=$G133AVD(want >=2) still-before-the-first=$G133AZERO(want >=1) tap-samples=$G133ATAP(want >=1; zero means the tap is not being noted) anything-else=$G133AOTHER(want 0) shake-dial-at-default=$G133FX(want >=1). RUN B battle: samples=$G133N unaccounted=$G133OOA(want 0; an amplitude that is neither the 0.0100 tap on a vehicle's own frame nor 0.028-per-cell on a frame that announced a building is a SUM) taps=$G133TAPS(want >=1) building-samples=$G133BLD(want >=1) vehicles=$G133VD(want >=2) buildings=$G133BD(want >=2) footprint-scale=[$G133SIZES](want GTWR=0.0280,PROC=0.0840,WEAP=0.0840 -- one cell against three) close-pairs=$G133SHAD(want >=1; a 0 means this battle no longer kills two vehicles inside one shake window and the re-arm leg is vacuous, so lengthen the run) re-armed=$G133REARM(want 0; nonzero is shake_tap testing >= instead of > and a screen of dying tanks holding the camera at peak) window-samples=$G133SEEN(want >=1)"
fi

# =====================================================================================
# G135 THE MAP BUTTON'S THIRD STATE: WHO IS PLAYING, AND WHAT THEY HAVE KILLED.
#
# 1995's Map button is a three-way cycle and only two of its states are a map.
# sidebar.cpp:2626-2650 Zoom_Mode_Control: zoomed -> unzoomed -> player status -> zoomed
# with a radar, and with NO radar it still toggles the player status as long as the game
# is not GAME_NORMAL. radar.cpp:1849-1958 Draw_Names is what that state paints: a
# "Name:"/"Kills:" header in LTGREY, a rule under it, then one row per multiplayer house,
# the name in MPlayerTColors[RemapColor] and the kill total right-aligned, GREY when the
# house is defeated. There is no zoom in this renderer, so the three states here are the
# plot, the list, and the empty hole this build's Map button already had.
#
# THE GROUND THE LIST STANDS ON IS THE LEG THAT WAS WRONG AND IS NOW MEASURED. Draw_Names
# opens with CC_Draw_Shape(RadarAnim, RADAR_ACTIVATED_FRAME, ...) and a Fill_Rect of the
# interior in BLACK, and neither has a test in front of it (radar.cpp:1871-1873): the list
# takes the LIT bezel and a black hole whether or not this player has a radar. A radarless
# skirmish that printed the roster over the faction emblem would still pass every text
# assertion below, so the pixels are asserted directly: the list frame WITH a radar and
# the list frame WITHOUT one must be the same frame, to the pixel.
#
# THE PIXEL COUNTS ARE RECTANGLE-LIMITED, and that is not decoration. Every verb that
# presses the Map button advances the world one tick, so a whole-frame diff across a press
# mixes the radar hole with whatever moved on the battlefield. The rectangle is the DOS
# radar hole in screen pixels at the default 1280x720: the bar is 240 wide at scale 3 so
# g_dbX0 is 1040, DB_RAD_X + DB_RAD_OFF_X is DOS x 244 -> 1052 and DB_RAD_I_WIDTH 72 -> 216
# wide; g_dbY0 is (720-600)/2 = 60, DB_RAD_Y + DB_RAD_OFF_Y is DOS y 8 -> 84 and
# DB_RAD_I_HEIGHT 69 -> 207 tall. The art HUD's box is measured the same way at 1280x960,
# where g_h6Scale is 2: H6_RADAR_X/Y 11,31 -> 982,62 and H6_RADAR_W/H 136x120 -> 272x240.
#
# WHAT THIS GATE DOES NOT CARRY. The campaign leg asserts that a campaign mission reaches
# no roster at all, that the Map button prints the two lines it always printed, and that
# its radar hole still swaps to the faction emblem. It does NOT compare those frames
# against a build made from the unchanged sources, because the suite holds no stored pixel
# reference. That comparison was made by hand instead: all three campaign frames and the
# first skirmish frame came out byte-identical against a binary built from the sources
# before this change, and it is recorded here as evidence the gate cannot re-run.
if [ ! -s "$SKMAP.pack" ] || [ ! -s "missions/$SKMAP.INI" ]; then
  bad "G135 the Map button's player list: $SKMAP is not installed, so this gate could not run at all. Run tools/stage-skirmish-maps.sh first."
else
G135S="--scen $SKMAP --pack $SKMAP.pack $SKBASE --skirmish --side gdi"
G135HOLE_R="1052 84 1268 291"
G135H6_R="982 62 1254 302"
cat > /tmp/g135_cycle.txt <<'SCRIPT'
tick 40
sbroster
shot shots/g135_radar.png
sbmap
sbroster
shot shots/g135_list.png
sbmap
sbroster
shot shots/g135_none.png
sbmap
sbroster
quit
SCRIPT
cat > /tmp/g135_nr.txt <<'SCRIPT'
tick 40
sbroster
shot shots/g135_nr_off.png
sbmap
sbroster
shot shots/g135_nr_list.png
sbmap
sbroster
quit
SCRIPT
cat > /tmp/g135_press.txt <<'SCRIPT'
tick 30
deselect
cam 20 20
mmclick 50 50
cam
cam 20 20
sbmap
sbroster
mmclick 50 50
cam
quit
SCRIPT
cat > /tmp/g135_kills.txt <<'SCRIPT'
tick 5
sbmap
sbroster
tick 8000
sbroster
quit
SCRIPT
cat > /tmp/g135_camp.txt <<'SCRIPT'
tick 120
sbroster
shot shots/g135_c1.png
sbmap
sbroster
shot shots/g135_c2.png
sbmap
sbroster
shot shots/g135_c3.png
quit
SCRIPT
cat > /tmp/g135_h6.txt <<'SCRIPT'
tick 40
shot shots/g135_h6_off.png
sbmap
sbroster
shot shots/g135_h6_list.png
quit
SCRIPT

gbegin shots/g135_radar.png shots/g135_list.png shots/g135_none.png \
       shots/g135_nr_off.png shots/g135_nr_list.png \
       shots/g135_c1.png shots/g135_c2.png shots/g135_c3.png \
       shots/g135_h6_off.png shots/g135_h6_list.png
grun /tmp/g135_a.log $G135S --ai 3 --forceradar --script /tmp/g135_cycle.txt
grun /tmp/g135_b.log $G135S --ai 3 --script /tmp/g135_nr.txt
# NOT through grun: mmclick counts a press outside the plotted radar as a script
# failure, and that refusal IS this leg's assertion, so the run is EXPECTED to exit 1.
# Any other status is a real fault and G135CRC says which it was.
./cnc_eyes $G135S --ai 3 --forceradar --noshroud --script /tmp/g135_press.txt \
    >/tmp/g135_c.log 2>&1; G135CRC=$?
cat /tmp/g135_c.log >> "$OUT"
grun /tmp/g135_d.log $G135S --ai 1 --forceradar --script /tmp/g135_kills.txt
grun /tmp/g135_e.log --scen SCG01EA --pack SCG01EA.pack $BASE --forceradar \
     --script /tmp/g135_camp.txt
# NOT through grun either: this is the 640x480 art HUD, and grun cannot carry the
# environment variable that selects it. The status is checked as G135FRC below.
CNC3D_HUD=new ./cnc_eyes $G135S --ai 3 --w 1280 --h 960 --script /tmp/g135_h6.txt \
    >/tmp/g135_f.log 2>&1; G135FRC=$?
cat /tmp/g135_f.log >> "$OUT"
gshots shots/g135_radar.png shots/g135_list.png shots/g135_none.png \
       shots/g135_nr_off.png shots/g135_nr_list.png \
       shots/g135_c1.png shots/g135_c2.png shots/g135_c3.png \
       shots/g135_h6_off.png shots/g135_h6_list.png

# The cycle, in the order the three presses printed it.
G135CYC=$(grep '^SIDEBAR-INPUT|map|' /tmp/g135_a.log | sed 's/^SIDEBAR-INPUT|map|//' | tr '\n' ',')
# The list as the SECOND sbroster saw it: shown, and the plot no longer clickable.
G135SHOWN=$(grep -c '^SBROSTER|available=1|shown=1|radar=0|rows=4' /tmp/g135_a.log)
G135OFF=$(grep -c '^SBROSTER|available=1|shown=0|radar=1|rows=4' /tmp/g135_a.log)
# One row per seat, exactly one of them the human, and every ink distinct.
G135ME=$(grep '^SBROW|' /tmp/g135_a.log | grep -c 'name=PLAYER|')
G135AI=$(grep '^SBROW|' /tmp/g135_a.log | grep -c 'name=COMPUTER|')
G135INK=$(grep '^SBROW|' /tmp/g135_a.log | sed -n 's/.*|ink=\([0-9]*\)|.*/\1/p' | sort -u | wc -l | tr -d ' ')
# Draw_Names walks HOUSES, so the rows must come out in ascending house order.
G135ORD=$(grep '^SBROW|' /tmp/g135_a.log | sed -n '1,4s/.*|house=\([0-9]*\)|.*/\1/p' \
          | awk 'NR>1 && $1<=p {bad=1} {p=$1} END{print bad?0:1}')
# The pixels. G135HOLE is what changed INSIDE the radar hole when the names replaced the
# plot; G135ALL is what changed in the whole 1280x720 frame. They must be EQUAL, which is
# how this gate says "the list stayed inside the hole" without a second rectangle.
G135HOLE=$(python3 "$GATEDIR/gate_gfx.py" rect shots/g135_radar.png shots/g135_list.png $G135HOLE_R | cut -d'|' -f2)
G135ALL=$(python3 "$GATEDIR/gate_gfx.py" diff shots/g135_radar.png shots/g135_list.png | cut -d'|' -f2)
# Third press: the names go, and the hole reverts to the house-logo plate (dosbar.c:641),
# which is the DEACTIVATED radar this build has always drawn. Not black.
G135PLATE=$(python3 "$GATEDIR/gate_gfx.py" rect shots/g135_list.png shots/g135_none.png $G135HOLE_R | cut -d'|' -f2)
# THE BLOCKING ASSERTION. radar.cpp:1871-1873 lights the bezel and blacks the hole with no
# test in front of it, so the list frame must not depend on having a radar at all. Both
# runs are the same seed at the same tick, so anything but 0 is the roster sitting on
# different ground -- the faction emblem, if the ACTIVATED frame was not forced.
G135BEZEL=$(python3 "$GATEDIR/gate_gfx.py" diff shots/g135_list.png shots/g135_nr_list.png | cut -d'|' -f2)
G135EMPTY=$(python3 "$GATEDIR/gate_gfx.py" diff shots/g135_none.png shots/g135_nr_off.png | cut -d'|' -f2)
# sidebar.cpp:2645-2648: no radar, not a campaign -> the list toggles anyway.
G135NR=$(grep -c '^SIDEBAR-INPUT|map|players on|no radar$' /tmp/g135_b.log)
G135NR2=$(grep -c '^SIDEBAR-INPUT|map|players off|no radar$' /tmp/g135_b.log)
# radar.cpp:1390-1393: while the names are up the radar swallows the press.
G135CAM=$(grep '^CAM|cam|' /tmp/g135_c.log | sed -n 's/^CAM|cam|x=\([0-9.]*\)|z=\([0-9.]*\).*/\1,\2/p' | tr '\n' ' ')
G135MISS=$(grep -c '^MMCLICK|MISS' /tmp/g135_c.log)
# The kill column is the engine's own number, so it must MOVE when something dies.
G135K0=$(grep '^SBROW|' /tmp/g135_d.log | sed -n '1,2s/.*|kills=\([0-9]*\)|.*/\1/p' | sort -rn | head -1)
G135K1=$(grep '^SBROW|' /tmp/g135_d.log | sed -n '3,4s/.*|kills=\([0-9]*\)|.*/\1/p' | sort -rn | head -1)
G135DEF=$(grep '^SBROW|' /tmp/g135_d.log | sed -n '3,4p' | grep -c 'defeated=1')
# The campaign. No hook, no rows, and the button says exactly what it always said.
G135CAV=$(grep -c '^SBROSTER|available=0|shown=0|radar=[01]|rows=0' /tmp/g135_e.log)
G135CL=$(grep '^SIDEBAR-INPUT|map|' /tmp/g135_e.log | sed 's/^SIDEBAR-INPUT|map|//' | tr '\n' ',')
G135CHOLE=$(python3 "$GATEDIR/gate_gfx.py" rect shots/g135_c1.png shots/g135_c2.png $G135HOLE_R | cut -d'|' -f2)
G135CBACK=$(python3 "$GATEDIR/gate_gfx.py" rect shots/g135_c1.png shots/g135_c3.png $G135HOLE_R | cut -d'|' -f2)
# The art HUD. Its radar box is 136x120 at scale 2, so 65280 pixels, and the list must
# replace EVERY one of them: that box is where the faction emblem is drawn, and a roster
# printed on top of the emblem would leave most of it standing.
G135H6=$(python3 "$GATEDIR/gate_gfx.py" rect shots/g135_h6_off.png shots/g135_h6_list.png $G135H6_R | cut -d'|' -f2)
G135H6ALL=$(python3 "$GATEDIR/gate_gfx.py" diff shots/g135_h6_off.png shots/g135_h6_list.png | cut -d'|' -f2)
G135H6R=$(grep -c '^SBROSTER|available=1|shown=1|radar=0|rows=4' /tmp/g135_f.log)

if [ "$GRC" != "0" ]; then
  bad "G135 the Map button's player list: a run failed or wrote no shot (GRC=$GRC)"
elif [ "$G135CYC" = "players,off,radar," ] && [ "${G135SHOWN:-0}" -ge 1 ] \
     && [ "${G135OFF:-0}" -ge 1 ] && [ "${G135ME:-0}" = "4" ] && [ "${G135AI:-0}" = "12" ] \
     && [ "${G135INK:-0}" = "4" ] && [ "${G135ORD:-0}" = "1" ] \
     && [ "${G135HOLE:-0}" -ge 2000 ] && [ "${G135ALL:-1}" = "${G135HOLE:-0}" ] \
     && [ "${G135PLATE:-0}" -ge 20000 ] \
     && [ "${G135BEZEL:-1}" = "0" ] && [ "${G135EMPTY:-1}" = "0" ] \
     && [ "${G135NR:-0}" -ge 1 ] && [ "${G135NR2:-0}" -ge 1 ] \
     && [ "${G135MISS:-0}" = "1" ] && [ "${G135CRC:-0}" = "1" ] \
     && [ "$G135CAM" = "20.000,20.000 50.500,50.500 20.000,20.000 20.000,20.000 " ] \
     && [ "${G135K0:-9}" = "0" ] && [ "${G135K1:-0}" -ge 1 ] && [ "${G135DEF:-0}" -ge 1 ] \
     && [ "${G135CAV:-0}" = "3" ] && [ "$G135CL" = "radar off,radar on," ] \
     && [ "${G135CHOLE:-0}" -ge 20000 ] && [ "${G135CBACK:-99999}" -le 2000 ] \
     && [ "${G135FRC:-1}" = "0" ] && [ "${G135H6R:-0}" -ge 1 ] \
     && [ "${G135H6:-0}" = "65280" ] && [ "${G135H6ALL:-0}" = "65280" ]; then
  ok "G135 the Map button's player list: in a skirmish the button cycles plot -> list -> house-logo plate -> plot, the list carries one row per seat in ascending house order with $G135INK distinct inks off the 1995 text ramp and exactly one PLAYER among three COMPUTERs, $G135HOLE pixels of names appear inside the 216x207 radar hole and not one pixel outside it, $G135PLATE of that hole go back to the faction plate when the names are dropped, THE LIST FRAME IS PIXEL-IDENTICAL WITH AND WITHOUT A RADAR ($G135BEZEL pixels apart) so the ACTIVATED bezel and the black fill are as unconditional here as they are in Draw_Names, the radar stops taking camera jumps while the list covers it (one refused press, camera held at 20,20 where the same press had jumped it to 50.5,50.5), the kill column starts at $G135K0 and reaches $G135K1 with the defeated row greyed, the 640x480 art HUD replaces all $G135H6 pixels of its faction emblem with the same list and changes nothing else, and a campaign mission reaches no roster at all (available=0 three times), says exactly the two lines it always said, and its own Map button still swaps $G135CHOLE pixels of radar for the emblem and back"
else
  bad "G135 the Map button's player list: cycle=[$G135CYC](want players,off,radar,) list-shown=$G135SHOWN(want >=1) list-off=$G135OFF(want >=1) player-rows=$G135ME(want 4, one per sbroster) computer-rows=$G135AI(want 12) distinct-inks=$G135INK(want 4; fewer means two seats print the same colour and MPlayerTColors is not being indexed by PlayerColorType) house-order=$G135ORD(want 1) names-in-hole=$G135HOLE(want >=2000; 0 means the list draws nothing) whole-frame=$G135ALL(want the same number as names-in-hole; larger means the list painted outside the radar hole) hole-reverted=$G135PLATE(want >=20000) list-frame-with-vs-without-radar=$G135BEZEL(want 0; anything else means the list is standing on different ground when the player has no radar -- the faction emblem, because the ACTIVATED bezel and the BLACK fill of radar.cpp:1871-1873 were not forced) empty-frame-with-vs-without-radar=$G135EMPTY(want 0) no-radar-on=$G135NR(want >=1) no-radar-off=$G135NR2(want >=1; zero for either means the button no longer follows Zoom_Mode_Control's else arm and a radarless skirmish cannot see the list) refused-presses=$G135MISS(want exactly 1) refusal-run-exit=$G135CRC(want 1, the refused press and nothing else) cameras=[$G135CAM](want the second reading moved and the last two did not) kills-at-start=$G135K0(want 0) kills-after=$G135K1(want >=1; 0 means the column is frozen, not that nothing died -- check the defeated flag beside it) defeated-rows=$G135DEF(want >=1) art-hud-exit=$G135FRC(want 0) art-hud-rows=$G135H6R(want >=1) art-hud-box=$G135H6(want 65280, the whole 136x120 emblem box at scale 2; less means part of the emblem is still showing through the list) art-hud-frame=$G135H6ALL(want 65280, the same number: more means the list changed something outside its box) campaign-roster-absent=$G135CAV(want 3) campaign-button=[$G135CL](want radar off,radar on,) campaign-hole=$G135CHOLE(want >=20000) campaign-radar-back=$G135CBACK(want <=2000)"
fi
fi

# =====================================================================================
# G138 THE MAP CAN BE MADE TO LOOK FOUGHT OVER.
#
# The editor could place tiberium (G126) and could not place a crater or a scorch mark,
# so a map built from nothing was always pristine ground. The brush is a seventh palette
# group of SEVEN rows and not fifteen, and the reason is the engine's, not ours: for an
# IsCrater type SmudgeClass::Mark stores SMUDGE_CRATER1 + Spot_Index(Coord)
# (smudge.cpp:229), Read_INI hands it the cell CENTRE, and Spot_Index answers 0 there, so
# every crater in every INI loads as CR1 whatever it was called. Measured over the 100
# shipped missions: 221 authored rows, 47 of them CR1 and not one CR2..CR6.
#
# WHAT THIS ASKS, and each leg fails on its own:
#   placed        the brush puts a crater and a scorch down at all
#   deepened      clicking a crater again DEEPENS it, and stops at 4 (Mark's own clamp,
#                 smudge.cpp:218-219, matching the crater art's five frames)
#   flat          clicking a SCORCH again does not stack it: it is not IsCrater
#   rows          each cell reaches the file once, as cell=NAME,cell,data
#   refused-tib   a smudge is refused on tiberium, WITH its reason
#   refused-wall  and on a wall, WITH its reason
#   erased        the eraser takes one back, and the count drops by exactly one
#
# The refusals are asserted WITH their reason text because a silent refusal and a correct
# refusal are the same line otherwise, and this editor has shipped a silent one before.
# =====================================================================================
SMDIR=missions/user_maps
SMMAP=SCM93EA
mkdir -p "$SMDIR"
rm -f "$SMDIR/$SMMAP.INI" "$SMDIR/$SMMAP.BIN" "$SMDIR/$SMMAP.HGT"
{
  echo "newmap 1 0 0"
  echo "editscen $SMMAP"
  echo "editstart 6 30 30"
  # six clicks on one cell: the clamp has to hold at 4, not run to 5
  for i in 1 2 3 4 5 6; do echo "editplace CR1 20 20 0"; done
  # two on another: a scorch does not stack
  echo "editplace SC1 22 20 0"
  echo "editplace SC1 22 20 0"
  echo "editplace SC4 24 20 0"
  # the two refusals, each set up by placing the thing that blocks it
  echo "editplace TI1 30 30 0"
  echo "editplace CR1 30 30 0"
  echo "editplace SBAG 32 20 0"
  echo "editplace CR1 32 20 0"
  # and the eraser takes one back
  echo "editplace CR1 26 20 0"
  echo "editerase 26 20"
  echo "editsave"
  echo "quit"
} > /tmp/g138_make.script
SML=$(./cnc_eyes --edit --editmode 0 --scen SCG01EA --pack SCG01EA.pack $BASE \
        --script /tmp/g138_make.script --w 1400 --h 900 2>&1)
echo "$SML" >> "$OUT"
SMPLACED=$(printf '%s' "$SML" | grep -c '^EDITPLACE|CR1|at 20,20|placed|')
SMSC=$(printf '%s' "$SML"     | grep -c '^EDITPLACE|SC[14]|at 2[24],20|placed|')
SMRTIB=$(printf '%s' "$SML"   | grep -c 'refused CR1 at 30,30 -- tiberium covers that cell')
SMRWALL=$(printf '%s' "$SML"  | grep -c 'refused CR1 at 32,20 -- a wall holds that cell')
SMRN=$(printf '%s' "$SML"     | grep -c '^EDITPLACE|CR1|at 3[02],[23]0|refused|')
# smudges= is read from the ERASE line, which prints the count AFTER the erase.
SMERAS=$(printf '%s' "$SML" | sed -n 's/^EDITERASE|at 26,20|erased|.*|smudges=\([0-9]*\)|.*/\1/p')
SMINI="$SMDIR/$SMMAP.INI"
# THE FILE IS CRLF, because a scenario INI is a DOS file and is written as one. Every
# read of it here strips the carriage return first: without that a `$`-anchored pattern
# matches nothing and every one of these legs reports 0 against a file that is perfectly
# correct, which is a green-looking gate reporting a fault that is not there.
SMBODY=$(sed -n '/^\[SMUDGE\]/,/^\[/p' "$SMINI" 2>/dev/null | tr -d '\r')
SMSEC=$(printf '%s' "$SMBODY"  | grep -cE '^[0-9]+=')
SMDEEP=$(printf '%s' "$SMBODY" | grep -cE '^1300=CR1,1300,4$')
SMFLAT=$(printf '%s' "$SMBODY" | grep -cE '^1302=SC1,1302,0$')
SMDUP=$(printf '%s' "$SMBODY"  | grep -oE '^[0-9]+' | sort | uniq -d | wc -l | tr -d ' ')
if [ "${SMPLACED:-0}" -eq 6 ] && [ "${SMSC:-0}" -eq 3 ] \
   && [ "${SMRTIB:-0}" -ge 1 ] && [ "${SMRWALL:-0}" -ge 1 ] && [ "${SMRN:-0}" -eq 2 ] \
   && [ "${SMSEC:-0}" -eq 3 ] && [ "${SMDEEP:-0}" -eq 1 ] && [ "${SMFLAT:-0}" -eq 1 ] \
   && [ "${SMDUP:-1}" -eq 0 ] && [ "${SMERAS:-0}" -eq 3 ]; then
  ok "G138 the map can be made to look fought over: six clicks on one cell deepen a crater to data 4 and the clamp holds it there, a scorch clicked twice stays flat at 0, three cells reach [SMUDGE] as cell=NAME,cell,data with no cell written twice, a smudge is refused on tiberium and on a wall and each refusal says which, and the eraser takes one back leaving $SMERAS"
else
  bad "G138 the map can be made to look fought over: crater-clicks-placed=$SMPLACED(want 6) scorch-placed=$SMSC(want 3) refused-on-tiberium=$SMRTIB(want >=1, WITH its reason) refused-on-wall=$SMRWALL(want >=1, WITH its reason) refusals-seen=$SMRN(want 2) rows-written=$SMSEC(want 3) crater-at-data-4=$SMDEEP(want 1; 0 means Mark's clamp at 4 is not being reproduced, so a seventh click would run off the crater art's five frames) scorch-at-data-0=$SMFLAT(want 1; a scorch is not IsCrater and must not stack) duplicate-cells=$SMDUP(want 0) smudges-after-erase=$SMERAS(want 3; the eraser must take exactly one)"
fi

# ==================================================================================
# G139  THE ADVANCED PAGE SCROLLS, AND ITS GEOMETRY IS MEASURED AT ALL.
#
# Two jobs, and the second one is why the first went unnoticed for so long.
#
#   1. gate_optlayout has been compiled by game/build.sh and shipped in every package
#      since it was written, and NOTHING IN THIS SUITE HAS EVER RUN IT. So the only
#      thing in the project that can measure this dialog's rectangles has never
#      measured them here, and "the layout gate is green" has cost nothing to say. It
#      runs below. It reads the Advanced page twice, at the top and at the bottom of
#      travel, so a scroll offset cannot put a row through the OK button unseen.
#
#   2. The scroll itself, through the SHIPPED path. optscroll calls dopt_scroll, which
#      is the function both dialog loops hand SDL_MOUSEWHEEL to, so this exercises the
#      wheel and not a setter beside it. What is asserted is behaviour and not a
#      literal row count, because the whole point of the bar is that the number of
#      elements is free to grow: the window must show the same number of rows wherever
#      it sits, must reach rows it could not reach before, must stop showing the first
#      row once it has moved off it, and must come back to exactly where it started.
#
#      THOSE ARE THE LEGS AGAIN, and they were briefly something else. When the mouse
#      swap moved to the Gameplay page the count fell to eleven elements in an eleven row
#      well, nothing scrolled, and the assertions were tightened to say so: every element
#      visible at every offset, all three dumps identical. That is a statement about one
#      transient count and not about the page, and the paragraph above -- left untouched
#      through that change -- already said the opposite. The terrain art row makes twelve,
#      the well scrolls again, and the legs are the behavioural ones once more. Adding a
#      thirteenth must not require touching this gate a third time.
rm -f /tmp/g139_scroll.txt
cat > /tmp/g139_scroll.txt <<'G139EOF'
tick 30
options
optclick Visuals
optclick Advanced...
optscroll 0
optscroll 99
optscroll -99
quit
G139EOF
OLOUT=$(./gate_optlayout dossidebar.pack 2>&1); OLRC=$?
echo "$OLOUT" >> "$OUT"
OLFAIL=$(printf '%s' "$OLOUT" | sed -n 's/^OPTLAYOUT|checked=[0-9]*|failures=\([0-9]*\)$/\1/p')
OLTOP=$(printf '%s' "$OLOUT" | grep -c '^OPTLAYOUT|page=ADVANCED|checked=')
OLBOT=$(printf '%s' "$OLOUT" | grep -c '^OPTLAYOUT|page=ADVANCED-BOTTOM|checked=')
SCLOG=$(# --gfx IS LOAD-BEARING and not decoration: without it `optclick Advanced...` silently
# leaves the page on VISUALS and reports no failure at all, so the gate would measure the
# wrong page and pass. --nosound because nothing automated makes a noise. gate_optlayout
# must sit beside gates.sh in RUNDIR; make-build.sh stages it.
./cnc_eyes --nosound --noshroud --scen SCG01EC --pack SCG01EA.pack $BASE --w 1280 --h 800 \
          --script /tmp/g139_scroll.txt --gfx 2>&1)
SCRC=$?
echo "$SCLOG" | grep -E '^(OPTIONS\|script\|scroll|ADVROW\|)' >> "$OUT"
# The three dumps, split on the scroll line that introduces each one.
adv_dump() { printf '%s\n' "$SCLOG" | awk -v want="$1" '/^OPTIONS\|script\|scroll\|/ {k++} k==want && /^ADVROW\|/ {print}'; }
D1=$(adv_dump 1); D2=$(adv_dump 2); D3=$(adv_dump 3)
D1N=$(printf '%s\n' "$D1" | grep -c '^ADVROW|')
D2N=$(printf '%s\n' "$D2" | grep -c '^ADVROW|')
D1HAS0=$(printf '%s\n' "$D1" | grep -c '^ADVROW|0|')
D2HAS0=$(printf '%s\n' "$D2" | grep -c '^ADVROW|0|')
# THE MAX LEG COUNTS ELEMENT ROWS ONLY. OK and the scroll bar are ADVROW lines too, and
# both are page chrome that does not move when the well scrolls, so leaving them in makes
# the highest row identical at the top and the bottom of travel and the "did it move" leg
# fails on a CORRECT implementation. D2OVER below deliberately keeps ALL the rows, because
# the overlap walk has to see the chrome as well.
D1MAX=$(printf '%s\n' "$D1" | awk -F'|' '/^ADVROW\|/ && $3!="OK" && $3!="Scroll" {print $2}' | sort -n | tail -1)
D2MAX=$(printf '%s\n' "$D2" | awk -F'|' '/^ADVROW\|/ && $3!="OK" && $3!="Scroll" {print $2}' | sort -n | tail -1)
# No two controls may share a pixel at the BOTTOM of travel either. G108's walk, verbatim.
D2OVER=$(printf '%s\n' "$D2" | awk -F'|' '/^ADVROW\|/ {
    split($4,p,","); split($5,d,"x");
    n++; X[n]=p[1]; Y[n]=p[2]; W[n]=d[1]; H[n]=d[2];
  }
  END { c=0;
    for (i=1;i<=n;i++) for (j=i+1;j<=n;j++)
      if (X[i] < X[j]+W[j] && X[j] < X[i]+W[i] && Y[i] < Y[j]+H[j] && Y[j] < Y[i]+H[i]) c++;
    print c }')
if [ "$OLRC" != "0" ] || [ "${OLFAIL:-99}" != "0" ]; then
  bad "G139 the Advanced page scrolls: gate_optlayout exited $OLRC with ${OLFAIL:-?} failures. First: $(printf '%s' "$OLOUT" | grep '  FAIL' | head -1)"
elif [ "${OLTOP:-0}" != "1" ] || [ "${OLBOT:-0}" != "1" ]; then
  bad "G139 the Advanced page scrolls: gate_optlayout read the page top=$OLTOP bottom=$OLBOT times (want 1 each). Without the second pass the scrolled layout is never measured at all"
elif [ "$SCRC" != "0" ]; then
  bad "G139 the Advanced page scrolls: the scroll run failed (exit $SCRC)"
elif [ "${D1N:-0}" -lt 2 ] || [ "$D1N" != "$D2N" ]; then
  bad "G139 the Advanced page scrolls: the well showed $D1N rows unscrolled and $D2N at the bottom of travel. The window has to be the same size wherever it sits; a different count means rows are being clipped rather than scrolled"
elif [ "${D1HAS0:-0}" != "1" ] || [ "${D2HAS0:-1}" != "0" ]; then
  bad "G139 the Advanced page scrolls: unscrolled the well must start at element 0 and at the bottom of travel it must have moved off it. first-row-present unscrolled=$D1HAS0 scrolled=$D2HAS0 (want 1 and 0), rows=$D1N/$D2N"
elif [ "${D2MAX:-0}" -le "${D1MAX:-0}" ]; then
  bad "G139 the Advanced page scrolls: the bottom of travel reached element row $D2MAX, no further than the top's $D1MAX. The wheel is not reaching rows the unscrolled well cannot show, so the bar moves nothing"
elif [ "${D2OVER:-99}" != "0" ]; then
  bad "G139 the Advanced page scrolls: $D2OVER pairs of controls overlap at the bottom of travel -- the scroll offset is putting a row through the OK button or through the bar"
elif [ "$D1" != "$D3" ]; then
  bad "G139 the Advanced page scrolls: scrolling to the end and back did not restore the page. The offset is not clamping symmetrically, so the well can be left somewhere it cannot be driven back from"
else
  ok "G139 the Advanced page scrolls: gate_optlayout runs and reads every rectangle on both the unscrolled and the wheeled page with no overlaps, and with the swap moved off to the Gameplay page the well of $D1N rows now holds the whole list, so the offset clamps to zero and all three dumps are identical (highest element row $D1MAX unscrolled, $D2MAX wheeled)"
fi

# G140. THE PRIMARY FACTORY'S LABEL. See gate_primary.txt for why three legs are needed.
PRB="--scen SCG90EA --pack SCG01EA.pack $BASE --noshroud --nosound --w 1280 --h 800"
gbegin shots/g140_sel.png shots/g140_desel.png
grun /tmp/g140.log $PRB --script gate_primary.txt
gshots shots/g140_sel.png shots/g140_desel.png
# WHICH barracks the script clicked, out of the run's own selection receipt rather than a
# hard-coded heap id that moves the day the scenario or the build order changes.
PRID=$(sed -n 's/^SELECT|.*|type=PYLE|house=GoodGuy|id=\([0-9]*\)|selected$/\1/p' /tmp/g140.log | head -1)
# Two objdumps run, one selected and one not, so 2 is "set in both" -- which is also the
# proof that deselecting takes the word away and NOT the flag.
PRON=$(grep -c "^OBJ|BUILDING|PYLE|GoodGuy|.*|id=$PRID|.*|primary=1|" /tmp/g140.log)
PRANY=$(grep -c '^OBJ|BUILDING|PYLE|GoodGuy|.*|primary=1|' /tmp/g140.log)
# The drawing half, from the collector the draw pass uses.
PRSEL=$(sed -n 's/^PRIMARYDUMP-BEGIN .*count=\([0-9]*\)$/\1/p' /tmp/g140.log | sed -n '1p')
PRDES=$(sed -n 's/^PRIMARYDUMP-BEGIN .*count=\([0-9]*\)$/\1/p' /tmp/g140.log | sed -n '2p')
PRNAME=$(grep -c "^PRIMARY|PYLE|id=$PRID|" /tmp/g140.log)
# THE CLASS INCLUDES '-' ON PURPOSE. collect_primary_labels culls only a label that is
# ENTIRELY off screen, so one straddling the left or top edge reports a negative x0 or y0.
# Without the minus this sed comes back EMPTY, RECT falls to the 0,0,1,1 default, the crop
# is one pixel and the gate fails blaming the font.
PRRECT=$(sed -n 's/^PRIMARY|.*|rect=\([0-9,-]*\)$/\1/p' /tmp/g140.log | sed -n '1p')
PRZOOM=$(sed -n 's/^PRIMARY|.*|zoom=\([0-9.]*\)|.*/\1/p' /tmp/g140.log | sed -n '1p')
PRPIX=$(RECT="${PRRECT:-0,0,1,1}" python3 - <<'PY'
from PIL import Image
import numpy as np, os
x0, y0, x1, y1 = [int(v) for v in os.environ['RECT'].split(',')]
def white(p):
    a = np.asarray(Image.open(p).convert('RGB')).astype(int)
    r = a[max(0,y0):max(0,y1), max(0,x0):max(0,x1)]
    if r.size == 0:
        return -1
    # The font's DB_WHITE is (252,252,252) in the pack's own palette, and the label is
    # drawn BELOW the post-process seam, so it is neither bloomed nor graded and the value
    # reaches the framebuffer exactly. Terrain does not hold all three channels this high.
    return int(((r[:,:,0] >= 250) & (r[:,:,1] >= 250) & (r[:,:,2] >= 250)).sum())
s, d = white('shots/g140_sel.png'), white('shots/g140_desel.png')
print('G140|white_sel=%d|white_desel=%d|word=%d' % (s, d, s - d))
PY
)
echo "$PRPIX" >> "$OUT"
PRWS=$(echo "$PRPIX" | sed -n 's/^G140|white_sel=\(-*[0-9]*\).*/\1/p')
PRWD=$(echo "$PRPIX" | sed -n 's/.*|white_desel=\(-*[0-9]*\)|.*/\1/p')
PRWORD=$(echo "$PRPIX" | sed -n 's/.*|word=\(-*[0-9]*\)$/\1/p')
if [ "$GRC" != "0" ]; then
  bad "G140 primary label: the run failed (exit $GRC)"
elif [ -z "$PRID" ]; then
  bad "G140 primary label: no PYLE was ever selected, so the gesture never happened and nothing below means anything. Either the second barracks was not placed or the camera is not on it"
elif [ "${PRANY:-0}" != "2" ] || [ "${PRON:-0}" != "2" ]; then
  bad "G140 primary label: across the two dumps, $PRANY GoodGuy PYLE lines carry primary=1 and $PRON of them are barracks id=$PRID (want 2 and 2). Anything else means the click did not move the flag, moved it to the wrong building, or the flag was cleared when the selection went away -- which would make the word's disappearance mean the wrong thing"
elif [ "${PRSEL:-0}" != "1" ] || [ "${PRNAME:-0}" != "1" ]; then
  bad "G140 primary label: with the barracks selected the frame would draw $PRSEL labels and $PRNAME of them name id=$PRID (want 1 and 1). Zero means the flag reached the brain and not the screen"
elif [ "${PRDES:-9}" != "0" ]; then
  bad "G140 primary label: $PRDES labels would still be drawn with nothing selected (want 0). The word is supposed to be gated on the selection, and the flag is still set at this point, so a non-zero count means the is_selected test is missing"
# THE FLOOR FOLLOWS THE ZOOM THE FRAME REPORTED. A fixed 120 was justified by a wrong
# minimum: sb_dos_px_half() clamps at 1, not 2, and the 640x480 HUD hits 1 at both
# 1280x720 and 1280x800, which is what a player using the shipped HUD gets. At half=1 the
# word is 41x11 real pixels and its ink is 85, under a 120 floor, so a fixed floor goes red
# on a CORRECT build. 60*z*z is 60 at zoom 1 and 240 at zoom 2, a floor with margin at both.
PRFLOOR=$(awk -v z="${PRZOOM:-1}" 'BEGIN{ printf "%d", 60*z*z }')
elif [ "${PRWORD:-0}" -ge "${PRFLOOR:-60}" ] && [ "${PRWD:-99999}" -lt "${PRWS:-0}" ]; then
  ok "G140 primary label: the second click made barracks id=$PRID primary, the word stands in its own rectangle $PRRECT at zoom $PRZOOM ($PRWS white pixels against $PRWD with nothing selected, so $PRWORD of ink), and deselecting takes the word away while the engine keeps the flag"
else
  bad "G140 primary label: rect=$PRRECT zoom=$PRZOOM selected=$PRWS deselected=$PRWD word=$PRWORD (want >=120). The glyph ink of \"Primary\" is 85 DOS pixels, and the floor is 60*zoom*zoom off the zoom the frame itself reported, so 60 at half-zoom 1 (the 640x480 HUD at 1280x720 and 1280x800) and 240 at half-zoom 2 (the DOS bar there). sb_dos_px_half clamps at 1, not 2. Also note the selection brackets put about 18 white pixels inside this rectangle before a glyph is drawn. Zero means the rectangle is being reported but nothing is drawn in it -- most likely the 8POINT font failed to resolve, which prints its own line, or the alpha test is discarding every texel. A NEGATIVE number means the word is drawn when nothing is selected"
fi

# =====================================================================================
# G142 A NEW MAP COMES UP FLAT, IN EVERY THEATER AND ON BOTH GRIDS.
#
# THE REPORT. A brand-new Desert map arrived with raised ground already on it, and the
# elevation tools would not move it.
#
# WHAT WAS WRONG. New Map clears the donor mission's objects and flattens every corner to
# rung 1, and it never cleared the BUILDING PAD OVERLAY. Every height the renderer and the
# editor read comes through corner_raw, which prefers the pad to the map's own corner, and
# the pad is rebuilt only from a brain dump, which a blank map never gets. So the donor's
# pads stood over ground the elevation tools were writing underneath, and a raise or a
# lower wrote its corners and nothing appeared to move.
#
# WHY IT SWEEPS EVERY CELL RATHER THAN PROBING ONE. Where the plateau lands is not a
# constant: it is wherever the last dump's buildings stood, it differs per theater, and on
# the 128 grid a pad written at stride 65 and read back at stride 129 lands somewhere else
# again. The only claim that holds for all of them is that nothing anywhere is off the
# flat. Against a pre-fix binary this gate reports theater0=27 theater1=103 128-grid=123
# fast-path=61.
#
# NO tick IN THESE SCRIPTS, deliberately. A tick runs refresh_objects, which rebuilds the
# pad and would cure the defect behind the gate's back. The editor does not tick on its
# own, which is why this reaches a player at all, and drawing frames does not cure it
# either, so the scripts go from newmap straight to height.
#
# THE LAST TWO LEGS EXIST SO THIS CANNOT BE PASSED BY REMOVING THE PAD. The overlay must
# still level the first GDI mission's three buildings, and both ways into a blank map must
# be taken: a theater CHANGE reboots on to the donor pack, while a new map in the theater
# already loaded takes edit_request_new's fast path and never reboots, so it inherits the
# pads of whatever map was open. Only the second is beyond the reach of a reboot, and the
# three theaters whose donors carry no buildings look clean either way, so a gate that
# tested the reboot path alone would be testing luck.
# =====================================================================================
gbegin /tmp/g142_t0.log /tmp/g142_t1.log /tmp/g142_t2.log /tmp/g142_t3.log \
       /tmp/g142_t4.log /tmp/g142_big.log /tmp/g142_fast.log
G142E="--edit --editmode 0 $BASE --nosound --w 1400 --h 900"
# The sweep script: every cell of the world, one height line each. Written by awk because
# the 128 grid is 16384 of them.
g142_sweep() {                              # g142_sweep FILE FIRST-VERB GRID
    { echo "$2"
      awk -v n="$3" 'BEGIN{for(y=0;y<n;y++)for(x=0;x<n;x++)printf "height %d %d\n",x,y}'
      echo quit
    } > "$1"
}
# The logs are NOT poured into the gate log: these seven runs are 40960 height lines
# between them. The counts go in, and the first offending cells with them, which is what a
# reader of a red gate needs.
g142_run() {                                # g142_run LOG SCEN PACK SCRIPT
    ./cnc_eyes $G142E --scen "$2" --pack "$3" --script "$4" > "$1" 2>&1
    ge_rc=$?
    [ "$ge_rc" = "0" ] || GRC="$ge_rc"
}
# A blank map is flattened to rung 1 AND its datum moved with it, so every sample must read
# 0.0000 at the centre and at all four corners. Anything else is ground nobody asked for.
g142_bumps() {
    grep -a '^HEIGHT|' "$1" \
      | grep -avc 'centre=0.0000|nw=0.0000|ne=0.0000|sw=0.0000|se=0.0000'
}
g142_seen()  { grep -ac '^HEIGHT|' "$1"; }
# READ THE LOG, NOT A VARIABLE. This leg counted `$ge_out`, which g142_run never sets: the
# run sends all of its output to the log FILE and keeps only the exit code. So the grep ran
# on an empty string, returned 0 for all seven runs, and the leg whose whole job is to catch
# a gate that measured nothing was itself measuring nothing. Found 2 Sep 2026, when it went
# red on a build where all seven maps were made correctly.
g142_new()   { grep -ac '^edit: NEW MAP' "$1"; }
g142_where() {
    grep -a '^HEIGHT|' "$1" \
      | grep -av 'centre=0.0000|nw=0.0000|ne=0.0000|sw=0.0000|se=0.0000' \
      | sed -n 's/^HEIGHT|\([0-9]*,[0-9]*\)|.*/\1/p' | tr '\n' ' ' | cut -c1-70
}
G142SUM=0; G142SEEN=0; G142RPT=""
# THE BLANK MAP ACTUALLY HAPPENED, and this leg is not optional. terrain_y answers 0.0f for
# a world with no corners, so "every cell reads 0.0000" is ALSO exactly what a leg that never
# MADE a map reports: a donor mission that cannot be opened leaves edit_blank_map uncalled and
# the run still prints its 4096 height lines, all zero, and exits 0. The sample count above
# does not close that, because the sweep still ran. Counting the editor's own NEW MAP receipt
# does. This suite's oldest failure mode is a gate that measures silence and calls it success.
G142NEW=0
for ge_th in 0 1 2 3 4; do
    g142_sweep "/tmp/g142_t$ge_th.script" "newmap 1 $ge_th 0" 64
    g142_run "/tmp/g142_t$ge_th.log" SCG01EA SCG01EA.pack "/tmp/g142_t$ge_th.script"
    ge_b=$(g142_bumps "/tmp/g142_t$ge_th.log")
    G142SUM=$((G142SUM + ge_b))
    G142SEEN=$((G142SEEN + $(g142_seen "/tmp/g142_t$ge_th.log")))
    G142NEW=$((G142NEW + $(g142_new "/tmp/g142_t$ge_th.log")))
    G142RPT="$G142RPT theater$ge_th=$ge_b"
    [ "$ge_b" = "0" ] || echo "G142 theater $ge_th: $ge_b cells off the flat, first $(g142_where "/tmp/g142_t$ge_th.log")" >> "$OUT"
done
g142_sweep /tmp/g142_big.script "newmap 4 1 0" 128
g142_run /tmp/g142_big.log SCG01EA SCG01EA.pack /tmp/g142_big.script
G142BIG=$(g142_bumps /tmp/g142_big.log)
G142BSEEN=$(g142_seen /tmp/g142_big.log)
G142NEW=$((G142NEW + $(g142_new /tmp/g142_big.log)))
[ "$G142BIG" = "0" ] || echo "G142 128 grid: $G142BIG cells off the flat, first $(g142_where /tmp/g142_big.log)" >> "$OUT"
# The fast path: a DESERT map made in a session already on a desert mission, so the blank
# happens in place and no reboot intervenes to rebuild the pad by accident.
g142_sweep /tmp/g142_fast.script "newmap 1 1 0" 64
g142_run /tmp/g142_fast.log SCB05EA SCB05EA.pack /tmp/g142_fast.script
G142FAST=$(g142_bumps /tmp/g142_fast.log)
G142SEEN=$((G142SEEN + $(g142_seen /tmp/g142_fast.log)))
G142NEW=$((G142NEW + $(g142_new /tmp/g142_fast.log)))
[ "$G142FAST" = "0" ] || echo "G142 fast path: $G142FAST cells off the flat, first $(g142_where /tmp/g142_fast.log)" >> "$OUT"
# The pad's own report, from the FIRST line each run prints, which is the loaded mission
# before anything is blanked.
G142PADB=$(sed -n 's/^PAD|buildings=\([0-9]*\)|.*/\1/p' /tmp/g142_t0.log | head -1)
G142PADR=$(sed -n 's/^PAD|buildings=[0-9]*|corners-raised=\([0-9]*\)|.*/\1/p' /tmp/g142_t0.log | head -1)
G142REB=$(grep -ac '^NEWMAP|rebooted|' /tmp/g142_t1.log)
G142FR=$(grep -ac '^NEWMAP|rebooted|' /tmp/g142_fast.log)
if [ "$GRC" != "0" ]; then
  bad "G142 a new map is flat: a run failed (exit $GRC), so nothing measured here means anything"
elif [ "${G142NEW:-0}" != "7" ]; then
  bad "G142 a new map is flat: $G142NEW of the 7 runs printed a NEW MAP receipt (want 7). A run whose donor could not be opened still sweeps every cell and reads 0.0000 everywhere, because terrain_y answers 0 for a world with no corners, so the flatness legs below would have gone green on a gate that measured nothing"
elif [ "${G142SEEN:-0}" != "24576" ] || [ "${G142BSEEN:-0}" != "16384" ]; then
  bad "G142 a new map is flat: the sweep did not happen. $G142SEEN height samples over the six 64-grid legs (want 24576) and $G142BSEEN on the 128 one (want 16384). A silent height verb would make every count below a zero that means nothing, which is the one way this gate could go green on a broken build"
elif [ "${G142PADB:-0}" != "3" ] || [ "${G142PADR:-0}" -lt 1 ]; then
  bad "G142 a new map is flat: the pad control leg failed before anything was blanked. On the first GDI mission as it ships the overlay reports buildings=$G142PADB corners-raised=$G142PADR (want 3 and >=1). This leg is here so that switching the pad off, or deleting it, cannot turn the rest of this gate green"
elif [ "${G142REB:-0}" -lt 1 ] || [ "${G142FR:-1}" != "0" ]; then
  bad "G142 a new map is flat: the two ways into a blank map were not both taken. reboot leg rebooted=$G142REB (want >=1), fast-path leg rebooted=$G142FR (want 0). A reboot on the fast-path leg means the run measured the donor path twice and the case a reboot cannot cure went untested"
elif [ "${G142SUM:-1}" = "0" ] && [ "${G142BIG:-1}" = "0" ] && [ "${G142FAST:-1}" = "0" ]; then
  ok "G142 a new map is flat: all 4096 cells of a medium map read 0.0000 in every one of the five theaters, all 16384 cells of a 128 map do, and so do the 4096 of a map made without a reboot; the pad still levels the first GDI mission's three buildings ($G142PADR corners)"
else
  bad "G142 a new map is flat: cells off the flat$G142RPT (each want 0 of 4096) 128-grid=$G142BIG (want 0 of 16384) fast-path=$G142FAST (want 0 of 4096). Non-zero means New Map is leaving the previous dump's building pads on the world: corner_raw prefers the pad to the map's own corner, so that ground reads high and every elevation edit under it is written and then ignored"
fi

# =====================================================================================
# G196. EVERY NEW MAP PRESET NAMES THE RECTANGLE IT MAKES, AND LEAVES ITS BORDER.
#
# The size list is read top to bottom and the header over it says PLAYABLE AREA, so every
# row has to mean the same thing. One row named its GRID instead: it read "128 x 128" while
# building a 120x120 rect inside a 128 world, promising a size no map of that format can
# carry, because the played rectangle sits one blank cell inside the cell array on every
# side and the widest a Version=1 map allows is therefore 126.
#
# TWO HALVES, because either alone is vacuous. The LABEL comes out of the layout harness,
# which already walks this dialog. The RECT comes out of a real New Map through the script
# verb. Neither side is allowed to be empty, or two silences would agree with each other.
# The world-cell counts pin which grid each rect was measured in, so shrinking the big
# preset onto the legacy grid could not pass by making its two numbers meet in the middle.
# And the left edge is checked, so widening a row to fill its grid edge to edge fails here
# rather than shipping a map a unit cannot walk off.
gbegin
G141S=/tmp/g141.script
: > "$G141S"
for G141I in 0 1 2 3 4; do echo "newmap $G141I 0 0" >> "$G141S"; done
echo quit >> "$G141S"
grun /tmp/g141.log --edit --editmode 0 --scen SCG01EA --pack SCG01EA.pack $BASE \
     --nosound --script "$G141S" --w 1280 --h 800
# The probe's own buffer is 40 bytes and clips the closing bracket off the longest row, so
# read the two numbers out of the label rather than matching the whole string. CUSTOM is
# row 5 and is left out on purpose: it does not name a rectangle, it takes one.
G141U=$(./cnc_eyes --uitest 2>&1 || true)
G141BAD=""; G141SAY=""
for G141I in 0 1 2 3 4; do
  G141L=$(printf '%s\n' "$G141U" \
          | sed -n "s/^UITEST|[^|]*|new-map size row $G141I ([^0-9]*\([0-9]*\) x \([0-9]*\).*/\1x\2/p" \
          | sort -u | tr '\n' '/')
  G141R=$(sed -n "s/^NEWMAP|size=$G141I|.*|rect=[0-9]*,[0-9]* \([0-9]*x[0-9]*\)|.*/\1/p" \
          /tmp/g141.log | sort -u | tr '\n' '/')
  G141O=$(sed -n "s/^NEWMAP|size=$G141I|.*|rect=\([0-9]*\),[0-9]* .*/\1/p" /tmp/g141.log | head -1)
  G141SAY="$G141SAY [$G141I lbl=$G141L rect=$G141R x0=$G141O]"
  if [ -z "$G141L" ] || [ -z "$G141R" ]; then
    G141BAD="$G141BAD row $G141I measured nothing;"
  elif [ "$G141L" != "$G141R" ]; then
    G141BAD="$G141BAD row $G141I is labelled $G141L and builds $G141R;"
  elif [ "${G141O:-0}" -lt 1 ]; then
    G141BAD="$G141BAD row $G141I starts at column $G141O, edge to edge with no blank border;"
  fi
done
G141BIG=$(grep -c 'blanked 16384 of 16384 cells' /tmp/g141.log)
G141SML=$(grep -c 'blanked 4096 of 4096 cells' /tmp/g141.log)
if [ "$GRC" != "0" ]; then
  bad "G196 new map presets: the run failed (exit $GRC), so nothing below means anything"
elif [ "${G141SML:-0}" != "4" ] || [ "${G141BIG:-0}" != "1" ]; then
  bad "G196 new map presets: the five rows built $G141SML worlds of 64x64 and $G141BIG of 128x128 (want 4 and 1). Until that holds, a label agreeing with a rect says nothing about which grid the rect is in, and the big row could pass by having been shrunk onto the legacy grid"
elif [ -n "$G141BAD" ]; then
  bad "G196 new map presets:$G141BAD measured$G141SAY. Every row names its PLAYABLE rectangle, which is what the header over the list says and what the other rows do; a row naming its grid instead promises a size the format cannot give, since the widest Version=1 rect is 126"
else
  ok "G196 new map presets: all five rows name the rectangle they build and each leaves a blank border,$G141SAY, four on the 64 grid and one on the 128"
fi


# =====================================================================================
# G143 THE GAMEPLAY PAGE EXISTS, IS REACHED FROM THE PAUSE MENU, AND FITS.
#
# Input is not part of the picture, so the two mouse switches live on a page of their own
# reached from the pause menu as Options > Gameplay. Its heading is "Enhanced Mode" and
# NOTHING on it is gated on the CLASSIC / ENHANCED master switch.
#
# THE PAGE NUMBER IS 7 and that is deliberate: DOPT_PAGE_GAMEPLAY is APPENDED after
# DOPT_PAGE_CONFIRM rather than inserted beside VISUALS, because four gates in this file
# match page numbers as literals (ADVANCED 3, SOUND 4, CHEATS 5, CONFIRM 6 and OPTIONS 0)
# and inserting would renumber three of them silently.
# =====================================================================================
rm -f /tmp/g143_gp.txt /tmp/g143_adv.txt
cat > /tmp/g143_gp.txt <<'G143AEOF'
tick 30
options
optclick Gameplay
optgp
optclick Swap mouse buttons
optgp
optclick Right button scrolls
optgp
optclick OK
optgp
quit
G143AEOF
cat > /tmp/g143_adv.txt <<'G143BEOF'
tick 30
options
optclick Visuals
optclick Advanced...
optvis
quit
G143BEOF
GPLOG=$(./cnc_eyes --nosound --noshroud --scen SCG01EC --pack SCG01EA.pack $BASE \
          --w 1280 --h 800 --gfx --script /tmp/g143_gp.txt 2>&1); GPRC=$?
ADLOG=$(./cnc_eyes --nosound --noshroud --scen SCG01EC --pack SCG01EA.pack $BASE \
          --w 1280 --h 800 --gfx --script /tmp/g143_adv.txt 2>&1); ADRC=$?
echo "$GPLOG" | grep -E '^(OPTGP\||GPRECT\||OPTIONS\|script\|click)' >> "$OUT"
echo "$ADLOG" | grep -E '^ADVRECT\|' >> "$OUT"
gp_dump() { printf '%s\n' "$GPLOG" | grep '^OPTGP|' | sed -n "$1p"; }
GP1=$(gp_dump 1); GP2=$(gp_dump 2); GP3=$(gp_dump 3); GP4=$(gp_dump 4)
GPPAGE=$(echo "$GP1" | grep -c '^OPTGP|page=7|')
GPRECTS=$(echo "$GPLOG" | grep '^GPRECT|' | sort -u | wc -l | tr -d ' ')
# G108's overlap walk, verbatim, over one dump's worth of rectangles.
GPOVER=$(echo "$GPLOG" | grep '^GPRECT|' | sort -u | awk -F'|' '{
    split($4,p,","); split($5,d,"x");
    n++; X[n]=p[1]; Y[n]=p[2]; W[n]=d[1]; H[n]=d[2];
  }
  END { c=0;
    for (i=1;i<=n;i++) for (j=i+1;j<=n;j++)
      if (X[i] < X[j]+W[j] && X[j] < X[i]+W[i] && Y[i] < Y[j]+H[j] && Y[j] < Y[i]+H[i]) c++;
    print c }')
# Every rectangle inside the box the same OPTGP line reports.
GPBOX=$(echo "$GP1" | sed -n 's/.*|box=\([0-9]*\)\.\.\([0-9]*\),\([0-9]*\)\.\.\([0-9]*\)$/\1 \2 \3 \4/p')
GPOUT=$(echo "$GPLOG" | grep '^GPRECT|' | sort -u | awk -F'|' -v b="$GPBOX" '
  BEGIN { split(b,B," ") }
  { split($4,p,","); split($5,d,"x");
    if (p[1] < B[1] || p[1]+d[1]-1 > B[2] || p[2] < B[3] || p[2]+d[2]-1 > B[4]) c++ }
  END { print c+0 }')
GPHEAD=$(echo "$GP1" | grep -c '|head=1|')
GPHR=$(echo "$GP1" | sed -n 's/.*|headrect=\([0-9]*\),\([0-9]*\) \([0-9]*\)x\([0-9]*\)|.*/\1 \3/p')
GPHOK=$(awk -v v="$GPHR" -v b="$GPBOX" 'BEGIN{ split(v,V," "); split(b,B," ");
  print (V[1] > B[1] && V[1]+V[2]-1 < B[2]) ? 1 : 0 }')
GPDEF=$(echo "$GP1" | grep -c '|swap=0|fx_swap=0|push=1|fx_push=1|')
GPSWAP=$(echo "$GP2" | grep -c '|swap=1|fx_swap=1|push=1|fx_push=1|')
GPPUSH=$(echo "$GP3" | grep -c '|swap=1|fx_swap=1|push=0|fx_push=0|')
GPBACK=$(echo "$GPLOG" | grep -c '^OPTIONS|script|click|OK|.*|page=0$')
GPADVN=$(echo "$ADLOG" | grep -c '^ADVRECT|')
GPADVSWAP=$(echo "$ADLOG" | grep -c '^ADVRECT|.*|Swap mouse buttons|')
GPFAIL=$(echo "$GPLOG" | grep -c '|FAIL$')
if [ "$GPRC" != "0" ] || [ "$ADRC" != "0" ]; then
  bad "G143 the Gameplay page: a run failed (page exit $GPRC, advanced exit $ADRC)"
elif [ "${GPPAGE:-0}" != "1" ]; then
  bad "G143 the Gameplay page: clicking Gameplay on the pause menu did not land on page 7 [$GP1]. 7 is DOPT_PAGE_GAMEPLAY, APPENDED after CONFIRM so that ADVANCED, SOUND, CHEATS and CONFIRM keep 3, 4, 5 and 6, which four gates in this file match as literals"
elif [ "${GPRECTS:-0}" != "3" ]; then
  bad "G143 the Gameplay page: the page answered with $GPRECTS distinct rectangles, want 3 (two checkbox rows and OK). Fewer means a control has no rectangle and no click can reach it"
elif [ "${GPOVER:-99}" != "0" ]; then
  bad "G143 the Gameplay page: $GPOVER pairs of controls share a pixel. A row through the OK button makes OK partly unclickable"
elif [ "${GPOUT:-99}" != "0" ]; then
  bad "G143 the Gameplay page: $GPOUT rectangles fall outside the dialog box $GPBOX the page itself reports. A control drawn off its own plate is a control the player cannot see"
elif [ "${GPHEAD:-0}" != "1" ] || [ "${GPHOK:-0}" != "1" ]; then
  bad "G143 the Gameplay page: the category heading measured=$GPHEAD (want 1) and inside-the-box=$GPHOK (want 1). Its rectangle is $GPHR against box $GPBOX. The heading is not a control, so one shared function measures it for both the draw and this dump; if it cannot be measured the font did not resolve"
elif [ "${GPDEF:-0}" != "1" ]; then
  bad "G143 the Gameplay page: the switches do not start at their defaults [$GP1]. Swapped buttons is OFF out of the box and the right button push is ON out of the box, and both the checkbox and the dial behind it must say so"
elif [ "${GPSWAP:-0}" != "1" ] || [ "${GPPUSH:-0}" != "1" ]; then
  bad "G143 the Gameplay page: clicking a box did not move the dial behind it. after-swap-click=[$GP2] after-push-click=[$GP3]"
elif [ "${GPBACK:-0}" -lt 1 ]; then
  bad "G143 the Gameplay page: OK did not step back to the pause menu (no click line reporting page=0)"
elif [ "${GPADVN:-0}" != "13" ]; then
  bad "G143 the Gameplay page: the Advanced page reported $GPADVN rectangles, want 13 (eleven element rows, OK and the scroll bar). With the swap moved off it holds eleven elements and shows all eleven"
elif [ "${GPADVSWAP:-1}" != "0" ]; then
  bad "G143 the Gameplay page: the Advanced page still carries a row labelled Swap mouse buttons. Either the element was not deleted, or dosopt.h's enum and dopt_vis_elem[] disagree and a row below it is now mislabelled"
elif [ "${GPFAIL:-1}" != "0" ]; then
  bad "G143 the Gameplay page: the run reported $GPFAIL script failures"
else
  ok "G143 the Gameplay page: Options > Gameplay opens page 7 with three rectangles that fit inside its own box $GPBOX and share no pixel, its heading is measured at $GPHR and lands inside the plate, the switches start OFF and ON and both boxes move the dial behind them, OK steps back to the pause menu, and the Advanced page is back to thirteen rectangles with no Swap mouse buttons among them"
fi

# =====================================================================================
# G144 INPUT IS THE PLAYER'S, AND IT SURVIVES A QUIT.
#
# Two claims. FIRST, CLASSIC governs the PICTURE and not the hand: choosing Classic must
# leave a customised binding alone in memory AND on disc. Measured before this round: with
# swap_buttons 1 on disc, walking Visuals then Classic rewrote cnc3d-fx.cfg with
# swap_buttons 0, so the setting could not survive being looked at.
# SECOND, the pause dialog now WRITES. fx_save had exactly three callers and none of them
# was the in-mission dialog, so anything set while paused was lost on quit.
# THE WRITE COUNT IS MEASURED, not assumed: the F5 panel writes straight into g_fx on every
# motion event, so a naive "write when g_fx changed" would rewrite the file once a frame
# and make the panel's own SAVE button decoration.
# =====================================================================================
VISDIR=/tmp/g144_vis
rm -rf "$VISDIR"; mkdir -p "$VISDIR"
VISCFG="$VISDIR/cnc3d-fx.cfg"
# RUN 1 GETS ITS OWN FILE. It ends on an ODD number of clicks on purpose (so the write
# count is 1 and not 0), which means it leaves swap_buttons 1 behind. Runs 2..5 measure
# absolute values on a file that starts from the compiled defaults, so sharing one file
# across all five makes run 2 toggle 1 -> 0 and demand 1. Measured: that is a red on a
# CORRECT build. Two files, and each run's starting state is stated rather than inherited.
VISCFG1="$VISDIR/run1-fx.cfg"
rm -f /tmp/g144_1.txt /tmp/g144_2.txt /tmp/g144_3.txt /tmp/g144_4.txt /tmp/g144_5.txt
cat > /tmp/g144_1.txt <<'G144AEOF'
tick 30
gfxpanel 1
gfx bloom_intensity 0.10
gfx bloom_intensity 0.20
gfx bloom_intensity 0.30
gfx bloom_intensity 0.40
gfx bloom_intensity 0.50
gfx bloom_intensity 0.60
gfx bloom_intensity 0.70
gfx bloom_intensity 0.80
gfx bloom_intensity 0.90
gfx bloom_intensity 1.00
gfxpanel 0
options
optclick Gameplay
optclick Swap mouse buttons
optclick Swap mouse buttons
optclick Swap mouse buttons
optclick OK
optclick Resume Mission
quit
G144AEOF
cat > /tmp/g144_2.txt <<'G144BEOF'
tick 30
options
optclick Gameplay
optclick Swap mouse buttons
optclick OK
optclick Resume Mission
quit
G144BEOF
cat > /tmp/g144_3.txt <<'G144CEOF'
tick 30
options
optclick Gameplay
optgp
quit
G144CEOF
cat > /tmp/g144_4.txt <<'G144DEOF'
tick 30
options
optclick Visuals
optclick Classic
optvis
optclick OK
optclick Resume Mission
quit
G144DEOF
cat > /tmp/g144_5.txt <<'G144EEOF'
tick 30
options
optclick Resume Mission
quit
G144EEOF
v_run() {
  ./cnc_eyes --nosound --noshroud --scen SCG01EC --pack SCG01EA.pack $BASE \
             --w 1280 --h 800 --gfx --visualscfg "$VISCFG" --script "$1" 2>&1
}
V1=$(VISCFG="$VISCFG1" v_run /tmp/g144_1.txt); V1RC=$?
# The three counts run 1 asks for, split on the panel and the dialog.
# THE PANEL VERB ALREADY EXISTED and prints GFX|panel=open / GFX|panel=closed, NOT a new
# GFXPANEL| line. Keying this window on a string the binary never prints makes the leg
# report 0 whatever the code does, which is the wanted answer arrived at by accident.
V1PANEL=$(printf '%s\n' "$V1" | awk '/^GFX\|panel=open$/{p=1} /^GFX\|panel=closed$/{p=0} p&&/^VISCFG\|wrote\|/{c++} END{print c+0}')
V1PANELSEEN=$(printf '%s\n' "$V1" | grep -c '^GFX|panel=open$')
V1DLG=$(printf '%s\n' "$V1" | awk '/^OPTIONS\|open\|/{p=1} /\|click\|Resume Mission\|/{p=0} p&&/^VISCFG\|wrote\|/{c++} END{print c+0}')
V1ALL=$(printf '%s\n' "$V1" | grep -c '^VISCFG|wrote|')
V2=$(v_run /tmp/g144_2.txt); V2RC=$?
V2DISK=$(sed -n 's/^swap_buttons *\([0-9]*\).*/\1/p' "$VISCFG" | head -1)
V2PUSH=$(sed -n 's/^right_drag_scroll *\([0-9]*\).*/\1/p' "$VISCFG" | head -1)
V3=$(v_run /tmp/g144_3.txt); V3RC=$?
V3READ=$(printf '%s\n' "$V3" | grep -c '^OPTGP|page=7|swap=1|fx_swap=1|')
V4=$(v_run /tmp/g144_4.txt); V4RC=$?
V4SWAP=$(printf '%s\n' "$V4" | grep -c '^OPTSWAP|swap=1|fx_swap=1|enhanced=0$')
V4DISK=$(sed -n 's/^swap_buttons *\([0-9]*\).*/\1/p' "$VISCFG" | head -1)
V5=$(v_run /tmp/g144_5.txt); V5RC=$?
V5W=$(printf '%s\n' "$V5" | grep -c '^VISCFG|wrote|')
printf 'G144|panel=%s|dialog=%s|total=%s|disk_swap=%s|disk_push=%s|relaunch=%s|classic_swap=%s|classic_disk=%s|noop=%s\n' \
  "$V1PANEL" "$V1DLG" "$V1ALL" "$V2DISK" "$V2PUSH" "$V3READ" "$V4SWAP" "$V4DISK" "$V5W" >> "$OUT"
if [ "$V1RC" != "0" ] || [ "$V2RC" != "0" ] || [ "$V3RC" != "0" ] || [ "$V4RC" != "0" ] || [ "$V5RC" != "0" ]; then
  bad "G144 input is the player's: a run failed (1=$V1RC 2=$V2RC 3=$V3RC 4=$V4RC 5=$V5RC)"
elif [ "${V1PANELSEEN:-0}" -lt 1 ]; then
  bad "G144 input is the player's: the tuning panel never opened (no GFX|panel=open line), so the write-count leg below measured a window that does not exist. ANTI-VACUITY: a leg whose window never opens reports 0 writes on any build at all"
elif [ "${V1PANEL:-99}" != "0" ]; then
  bad "G144 input is the player's: ten dial moves with the F5 panel open produced $V1PANEL file writes, want 0. The panel writes straight into g_fx on every motion event, so a sync that does not stand down while it is open rewrites the preset once a frame and makes the panel's own SAVE button decoration"
elif [ "${V1DLG:-99}" != "0" ]; then
  bad "G144 input is the player's: clicking boxes with the pause dialog still OPEN produced $V1DLG file writes, want 0. The write belongs on the way OUT of the dialog, not on every click inside it"
elif [ "${V1ALL:-0}" != "1" ]; then
  bad "G144 input is the player's: the whole of run 1 produced $V1ALL file writes, want exactly 1. Three clicks on one box leave it moved, so closing the dialog must write once and once only"
elif [ "${V2DISK:-x}" != "1" ] || [ "${V2PUSH:-x}" != "1" ]; then
  bad "G144 input is the player's: after setting the box from the PAUSE dialog and leaving, the file on disc reads swap_buttons=${V2DISK:-absent} right_drag_scroll=${V2PUSH:-absent}, want 1 and 1. Until this round fx_save had three callers and the in-mission dialog was not one of them, so a mouse setting made while paused was lost on quit"
elif [ "${V3READ:-0}" -lt 1 ]; then
  bad "G144 input is the player's: a FRESH process reading the same file did not come up with the binding set (want one OPTGP line with swap=1 and fx_swap=1, having clicked nothing). A setting written and never read back is not persistence"
elif [ "${V4SWAP:-0}" -lt 1 ]; then
  bad "G144 input is the player's: choosing CLASSIC changed the binding in memory. Classic governs the PICTURE; how the game is driven stays the player's"
elif [ "${V4DISK:-x}" != "1" ]; then
  bad "G144 input is the player's: choosing CLASSIC rewrote the file, which now reads swap_buttons=${V4DISK:-absent} over a customised 1. This is the measurement that opened this round: the Visuals screen writes the preset on the way out, so a CLASSIC arm that touches the binding destroys it on disc"
elif [ "${V5W:-99}" != "0" ]; then
  bad "G144 input is the player's: opening the dialog, changing nothing and leaving produced $V5W file writes, want 0"
else
  ok "G144 input is the player's: ten dial moves with the tuning panel open write nothing and three clicks inside the dialog write nothing, closing it on a change writes exactly once, the pause dialog's own setting reaches the file, a fresh process reads it back, CLASSIC leaves it alone in memory AND on disc, and a look-and-leave writes nothing at all"
fi

# =====================================================================================
# G145 THE RIGHT BUTTON PUSHES THE VIEW.
#
# Holding the right button past the click threshold PUSHES the camera: the further the
# pointer stands from the middle of the screen the faster it travels, with a small flat
# spot at the centre. It is ON by default, it is NOT gated on the CLASSIC / ENHANCED master
# switch, and with it OFF the right button never moves the camera at all -- off does not
# restore the old grab-drag.
#
# At --w 1280 --h 960 the flat spot is 5 percent of the screen each way, so 64 columns and
# 48 rows either side of centre 640,480, and the sidebar's left edge is 960. Every RPUSH
# line prints its own dead=64,48 so those two numbers are read back rather than assumed.
# =====================================================================================
rm -f /tmp/g145.txt
cat > /tmp/g145.txt <<'G145EOF'
tick 20
cam 54 51
echo G145-1 the flat spot around the press point
rdown 500 500
rmove 540 500
rpush 60
rup
echo G145-2 the ramp rises with the offset
cam 54 51
rdown 500 500
rmove 900 500
rpush 12
rup
cam 54 51
rdown 500 500
rmove 1250 500
rpush 12
rup
echo G145-3 the dialog drops the latch
cam 54 51
rdown 500 500
rmove 900 500
options
rpush 60
optclick Resume Mission
rpush 60
rup
echo G145-4 the edge strip stands down while the push is live
cam 54 51
rdown 500 500
rmove 900 500
edgeat 2 500 30
rup
edgeat 2 500 30
echo G145-5 the chrome veto arms nothing
cam 54 51
rdown 1100 700
rmove 900 500
rpush 60
rup
echo G145-6 the click survives
lclickobj MTNK 0
expectsel 1
cam 54 51
rdown 500 500
rmove 503 502
rpush 30
rup
expectsel 0
echo G145-7 the switch off means no camera motion at all
gfx right_drag_scroll 0
cam 54 51
rdown 500 500
rmove 900 500
rpush 60
rup
quit
G145EOF
PULOG=$(./cnc_eyes --scen SCG90EA --pack SCG01EA.pack $BASE --noshroud --nosound \
            --forceradar --w 1280 --h 960 --script /tmp/g145.txt 2>&1)
PURC=$?
echo "$PULOG" | grep -E '^(ECHO|RBTN|RPUSH|EDGEAT|CAM\||EXPECTSEL|CANCEL)' >> "$OUT"
pu_leg() { printf '%s\n' "$PULOG" | sed -n "/^ECHO|G145-$1 /,/^ECHO|G145-$2 /p"; }
pu_cam() { printf '%s\n' "$1" | sed -n "s/^CAM|$2|\(x=[^|]*|z=[^|]*\).*/\1/p" | sed -n "${3:-1}p"; }
PU1=$(pu_leg 1 2); PU2=$(pu_leg 2 3); PU3=$(pu_leg 3 4); PU4=$(pu_leg 4 5)
PU5=$(pu_leg 5 6); PU6=$(pu_leg 6 7)
PU7=$(printf '%s\n' "$PULOG" | sed -n '/^ECHO|G145-7 /,$p')
# 0: ON out of the box. The first arm of the run has had nothing set on it.
PU_DEFON=$(printf '%s\n' "$PULOG" | grep -m1 '^RBTN|drag|begin|' | grep -c '|push=1|')
# 1: inside the flat spot nothing moves, and the line states the dead zone it used.
# THE FLAT SPOT TRAVELS WITH THE HAND. This pressed at 500,500 and moved to 700,500, which
# was inside the dead zone only because the zone used to sit at the SCREEN CENTRE and 700 is
# 60 px from 640. The origin is the press point now, so that is a 200 px shove and it moves,
# correctly. 540 is 40 px from the press, inside the same 64 px zone, and it is a stronger
# test than the old one: it cannot pass by accident on a gesture that happens to start near
# the middle of the glass.
PU_DEAD=$(printf '%s\n' "$PU1" | grep -c '^RPUSH|frames=60|at=540,500|armed=1|moved=0|dead=64,48|')
PU_DEADSTILL=$([ "$(pu_cam "$PU1" rmove)" = "$(pu_cam "$PU1" rpush)" ] && echo 1 || echo 0)
# 2: the same frames must cover strictly more ground further out.
# TWELVE FRAMES, NOT THIRTY, AND THE REASON IS THE MAP EDGE. At the doubled push speed
# thirty frames pan further than SCG01EA has room for, so both arms clamped at x=63.000
# and the ramp leg compared two identical numbers -- green if the ramp worked, green if it
# did not. Measured at 12: near 58.249, far 61.283, against a clamp at 63.000 and a start
# at 54, so both arms are strictly inside the map and strictly apart. Anyone raising the
# speed again must re-check this: a saturated ramp leg is a leg that cannot fail.
PU_NEAR=$(printf '%s\n' "$PU2" | sed -n 's/^CAM|rpush|x=\([0-9.-]*\)|.*/\1/p' | sed -n 1p)
PU_FAR=$(printf '%s\n' "$PU2" | sed -n 's/^CAM|rpush|x=\([0-9.-]*\)|.*/\1/p' | sed -n 2p)
PU_RAMP=$(awk -v a="$PU_NEAR" -v b="$PU_FAR" 'BEGIN{ print (a>54 && b>a) ? 1 : 0 }')
# 3: the dialog CLEARS the latch rather than skipping it, and it stays cleared afterwards.
PU_DROP=$(printf '%s\n' "$PU3" | grep -c '^RPUSH|dropped|dialog$')
PU_DROPARM=$(printf '%s\n' "$PU3" | grep -c '^RPUSH|frames=60|at=900,500|armed=0|moved=0|')
PU_DROPSTILL=$([ "$(pu_cam "$PU3" rmove)" = "$(pu_cam "$PU3" rpush 2)" ] && echo 1 || echo 0)
# 4: the edge strip must never add to a push, and must come back when the push ends.
PU_EBLOCK=$(printf '%s\n' "$PU4" | grep -c '^EDGEAT|frames=30|mouse=2,500|.*|guard=blocked|busy=1$')
PU_EPASS=$(printf '%s\n' "$PU4" | grep -c '^EDGEAT|frames=30|mouse=2,500|.*|guard=pass|busy=0$')
PU_ESTILL=$([ "$(pu_cam "$PU4" rmove)" = "$(pu_cam "$PU4" edgeat 1)" ] && echo 1 || echo 0)
PU_EMOVED=$([ "$(pu_cam "$PU4" edgeat 1)" != "$(pu_cam "$PU4" edgeat 2)" ] && echo 1 || echo 0)
# 5: a press that landed on the build column can never arm, however far it travels.
PU_NOPAN=$(printf '%s\n' "$PU5" | grep -c '^RBTN|down|at=1100,700|nopan=1|')
PU_NOPANARM=$(printf '%s\n' "$PU5" | grep -c '^RPUSH|frames=60|.*|armed=0|moved=0|')
PU_NOPANSTILL=$([ "$(pu_cam "$PU5" rdown)" = "$(pu_cam "$PU5" rpush)" ] && echo 1 || echo 0)
# 6: three pixels of travel is jitter, and the ladder still runs on the release.
PU_CLICK=$(printf '%s\n' "$PU6" | grep -c '^RBTN|up|click|at=500,500$')
PU_CLICKCANCEL=$(printf '%s\n' "$PU6" | grep -c '^CANCEL|deselect|was=1|now=0$')
PU_CLICKARM=$(printf '%s\n' "$PU6" | grep -c '^RPUSH|frames=30|.*|armed=0|moved=0|')
# 7: OFF is not the old grab-drag coming back. It is no camera motion at all.
PU_OFFARM=$(printf '%s\n' "$PU7" | grep -c '^RBTN|drag|begin|from=500,500|push=0|')
PU_OFFMOVE=$(printf '%s\n' "$PU7" | grep -c '^RPUSH|frames=60|at=900,500|armed=0|moved=0|')
PU_OFFSTILL=$([ "$(pu_cam "$PU7" rdown)" = "$(pu_cam "$PU7" rup)" ] && echo 1 || echo 0)
PU_OFFDRAG=$(printf '%s\n' "$PU7" | grep -c '^RBTN|up|drag|no click$')
PU_FAILS=$(printf '%s\n' "$PULOG" | grep -c '|FAIL$')
if [ "$PURC" != "0" ] || [ "${PU_FAILS:-1}" != "0" ]; then
  bad "G145 the right button pushes the view: the run failed (exit $PURC, $PU_FAILS script failures). An empty set of numbers below means the rpush verb is missing from the binary in $RUNDIR"
elif [ "${PU_DEFON:-0}" != "1" ]; then
  bad "G145 the right button pushes the view: the very first right drag of the run reports push=0, so the gesture is not ON out of the box. It was asked for, and a player should not have to go looking for it"
elif [ "${PU_DEAD:-0}" -lt 1 ] || [ "${PU_DEADSTILL:-0}" != "1" ]; then
  bad "G145 the right button pushes the view: inside the flat spot the view moved. armed-and-spent-nothing=$PU_DEAD(want >=1) camera-still=$PU_DEADSTILL(want 1). the press was at 500,500 and the hand moved to 540,500, 40 px, inside the 64 px flat spot the RPUSH line printed. The origin is the PRESS POINT, not the screen centre: a press in a corner must start from rest like any other"
elif [ "${PU_RAMP:-0}" != "1" ]; then
  bad "G145 the right button pushes the view: thirty frames from 900,500 landed x=$PU_NEAR and the same thirty from 1250,500 landed x=$PU_FAR. The second must be strictly further than the first or the speed does not rise with the offset, and both must be past the starting 54"
elif [ "${PU_DROP:-0}" != "1" ] || [ "${PU_DROPARM:-0}" -lt 2 ] || [ "${PU_DROPSTILL:-0}" != "1" ]; then
  bad "G145 the right button pushes the view: opening the pause dialog did not drop the latch. dropped-once=$PU_DROP(want 1) disarmed-both-times=$PU_DROPARM(want >=2) camera-still=$PU_DROPSTILL(want 1). A dialog sets a flag and never touches the button, so a latch left standing scrolls the map with nothing held down"
elif [ "${PU_EBLOCK:-0}" -lt 1 ] || [ "${PU_ESTILL:-0}" != "1" ] || [ "${PU_EPASS:-0}" -lt 1 ] || [ "${PU_EMOVED:-0}" != "1" ]; then
  bad "G145 the right button pushes the view: the edge strip did not stand down while a push was live. blocked-while-pushing=$PU_EBLOCK(want >=1) camera-still-then=$PU_ESTILL(want 1) passes-after-the-release=$PU_EPASS(want >=1) and-moves=$PU_EMOVED(want 1). The suppression used to be free because the right button set g_camDrag; it does not any more"
elif [ "${PU_NOPAN:-0}" -lt 1 ] || [ "${PU_NOPANARM:-0}" -lt 1 ] || [ "${PU_NOPANSTILL:-0}" != "1" ]; then
  bad "G145 the right button pushes the view: a press on the build column armed a push. nopan-latched=$PU_NOPAN(want >=1) never-armed=$PU_NOPANARM(want >=1) camera-still=$PU_NOPANSTILL(want 1). Under the sidebar there is no ground to travel over and the map would leap"
elif [ "${PU_CLICK:-0}" -lt 1 ] || [ "${PU_CLICKCANCEL:-0}" -lt 1 ] || [ "${PU_CLICKARM:-0}" -lt 1 ]; then
  bad "G145 the right button pushes the view: a right CLICK stopped being a click. delivered=$PU_CLICK(want >=1) ran-the-ladder=$PU_CLICKCANCEL(want >=1) armed-nothing=$PU_CLICKARM(want >=1). The push arms only past the existing six point threshold and no new one was invented"
elif [ "${PU_OFFARM:-0}" -lt 1 ] || [ "${PU_OFFMOVE:-0}" -lt 1 ] || [ "${PU_OFFSTILL:-0}" != "1" ] || [ "${PU_OFFDRAG:-0}" -lt 1 ]; then
  bad "G145 the right button pushes the view: with the switch OFF the right button still moved the camera. armed=0-reported=$PU_OFFARM(want >=1) spent-nothing=$PU_OFFMOVE(want >=1) camera-still-across-the-gesture=$PU_OFFSTILL(want 1) release-was-a-drag=$PU_OFFDRAG(want >=1). OFF does not restore the old grab-drag: it means the right button moves the camera by nothing at all, and only cancels and issues orders"
else
  ok "G145 the right button pushes the view: it is armed by default, spends nothing inside the flat spot the line itself reports, covers more ground from 1250 (x=$PU_FAR) than from 900 (x=$PU_NEAR), drops its latch the moment a dialog opens and does not pick it up again, stands the edge strip down while it is live and hands it back on the release, arms nothing at all from the build column, leaves a three pixel right click a right click, and with the switch off moves the camera by nothing whatsoever"
fi

# =====================================================================================
# G146 THE PUSH POINTER, MEASURED IN PIXELS.
#
# While the push is travelling the cursor becomes FOUR arrows pointing out of the picked
# ground point. This gate does not assert a branch or an offset: it counts white connected
# components in the picture, so a spread that renders as one diamond blob goes red on the
# count AND on the ink at the centre.
#
# THE CROP STOPS AT THE SIDEBAR. The bar's left edge is 960 at --w 1280 and it is full of
# near-white pixels, so a crop that runs into it counts the bar's own buttons as cursor
# components. Measured: without the clip the push shot reads 5 components and both control
# shots read 3.
# =====================================================================================
rm -f /tmp/g146.txt
rm -f shots/g146_plain.png shots/g146_push.png shots/g146_off.png shots/g146_noswitch.png
cat > /tmp/g146.txt <<'G146EOF'
tick 20
cam 48 52
cursor3d 1
cursor 660 500
shot shots/g146_plain.png
edgearrow 45.500 45.500
edgearrow 35.030 45.500
edgearrow 40.500 38.030
rdown 660 500
rmove 900 500
cursor 900 500
shot shots/g146_push.png
rup
cursor 900 500
shot shots/g146_off.png
gfx right_drag_scroll 0
rdown 660 500
rmove 900 500
cursor 900 500
shot shots/g146_noswitch.png
rup
edgearrow 45.500 45.500
edgearrow 35.030 45.500
edgearrow 40.500 38.030
quit
G146EOF
CULOG=$(./cnc_eyes --scen SCG01EA --pack SCG01EA.pack $BASE --noshroud --nosound \
            --w 1280 --h 960 --script /tmp/g146.txt 2>&1)
CURC=$?
echo "$CULOG" | grep -E '^(SHOT\||EDGEARROW\|)' >> "$OUT"
CUSHOTS=$(printf '%s\n' "$CULOG" | grep -c '^SHOT|shots/g146_.*|ok$')
CUEA_BEFORE=$(printf '%s\n' "$CULOG" | grep '^EDGEARROW|' | sed -n '1,3p' | tr '\n' ' ')
CUEA_AFTER=$(printf '%s\n' "$CULOG" | grep '^EDGEARROW|' | sed -n '4,6p' | tr '\n' ' ')
CUEA_WEST=$(printf '%s\n' "$CULOG" | grep -c '^EDGEARROW|world=35.030,45.500|deg=90.0|face=192$')
CUPIX=$(python3 - <<'G146PY'
from PIL import Image
BAR = 960          # the sidebar's left edge at --w 1280; everything from here is chrome
S   = 160
def label(mask, W, H, minpx=12):
    seen = [[0]*W for _ in range(H)]; out = []
    for y in range(H):
        for x in range(W):
            if mask[y][x] and not seen[y][x]:
                st = [(x, y)]; seen[y][x] = 1; px = []
                while st:
                    cx, cy = st.pop(); px.append((cx, cy))
                    for dy in (-1, 0, 1):
                        for dx in (-1, 0, 1):
                            nx, ny = cx+dx, cy+dy
                            if 0 <= nx < W and 0 <= ny < H and mask[ny][nx] and not seen[ny][nx]:
                                seen[ny][nx] = 1; st.append((nx, ny))
                if len(px) >= minpx: out.append(px)
    return out
for name, cx, cy in (('plain', 660, 500), ('push', 900, 500),
                     ('off', 900, 500), ('noswitch', 900, 500)):
    try:
        im = Image.open('shots/g146_%s.png' % name).convert('RGB')
    except Exception as e:
        print('G146PIX|%s|ERROR|%s' % (name, e)); continue
    x0, y0 = cx - S//2, cy - S//2
    cr = im.crop((x0, y0, x0+S, y0+S)); p = cr.load()
    barx = BAR - x0
    mask = [[1 if (x < barx and p[x, y][0] > 190 and p[x, y][1] > 190
                   and p[x, y][2] > 190) else 0 for x in range(S)] for y in range(S)]
    comps = label(mask, S, S)
    tot = sum(len(c) for c in comps)
    if tot:
        gx = sum(x for c in comps for x, _ in c) / tot
        gy = sum(y for c in comps for _, y in c) / tot
        ink = sum(1 for c in comps for x, y in c if (x-gx)**2 + (y-gy)**2 <= 36)
    else:
        gx = gy = -1.0; ink = -1
    print('G146PIX|%s|comps=%d|ink=%d' % (name, len(comps), ink))
G146PY
)
echo "$CUPIX" >> "$OUT"
CU_PUSH=$(printf '%s\n' "$CUPIX" | grep -c '^G146PIX|push|comps=4|ink=0$')
CU_PLAIN=$(printf '%s\n' "$CUPIX" | grep -c '^G146PIX|plain|comps=1|')
CU_OFF=$(printf '%s\n' "$CUPIX" | grep -c '^G146PIX|off|comps=1|')
CU_NOSW=$(printf '%s\n' "$CUPIX" | grep -c '^G146PIX|noswitch|comps=1|')
if [ "$CURC" != "0" ]; then
  bad "G146 the push pointer: the run failed (exit $CURC). Any measurement below would be of stale shots"
elif [ "${CUSHOTS:-0}" != "4" ]; then
  bad "G146 the push pointer: $CUSHOTS of the four screenshots were written (want 4), so at least one number below is read off a stale file"
elif [ "${CU_PUSH:-0}" != "1" ]; then
  bad "G146 the push pointer: while the push is live the crop must hold exactly FOUR separated white components with an ink-free disc of radius 6 at their common centroid. It reads [$(printf '%s\n' "$CUPIX" | grep '|push|')]. One component with a solid centre is the diamond blob a too-small spread renders; C3D_PUSH_SPREAD is the dial and its two rejected values are written down beside it"
elif [ "${CU_PLAIN:-0}" != "1" ] || [ "${CU_OFF:-0}" != "1" ] || [ "${CU_NOSW:-0}" != "1" ]; then
  bad "G146 the push pointer: the three control shots must each hold exactly ONE component, which is this gate's own proof that its counter can fail. plain=$CU_PLAIN off=$CU_OFF noswitch=$CU_NOSW (want 1 each). off is also the proof the latch clears on the release, and noswitch that nothing new is drawn with the switch off"
elif [ "$CUEA_BEFORE" != "$CUEA_AFTER" ] || [ "${CUEA_WEST:-0}" -lt 2 ]; then
  bad "G146 the push pointer: the MAP EDGE arrow was disturbed by the push. before=[$CUEA_BEFORE] after=[$CUEA_AFTER] west-readings=$CUEA_WEST(want 2). The push branch returns before the edge arrow's own block, which must be left exactly as it was"
else
  ok "G146 the push pointer: with the push live the cursor draws four separated white components with an ink-free centre, and with it released, never armed, or switched off it draws exactly one, while the map edge arrow reads deg=-1/90.0/0.0 identically before and after the whole gesture"
fi



# =====================================================================================
# G147 THE DATABASE TAB
#
# Four claims, and the first is the one that makes the other three mean anything:
#
#   1. IT IS ABSENT IN CLASSIC. A run with no --gfx must report avail=0 and must NOT
#      draw a plate in the 160-pixel slot right of OPTIONS. Without this leg the gate
#      would pass just as happily on a build that showed the tab to everybody.
#   2. It opens, and the roster it opens on is the table's roster: the per-category
#      counts and the row it lands on are checked against codex_table.h itself, so a
#      regenerated table and a stale screen cannot agree by accident.
#   3. The numbers on the page are the 1995 engine's. Spot-checked against
#      brain/vanilla/tiberiandawn, not against a previous run of this gate.
#   4. The music ducks while it is open and is on its way back afterwards.
#
# The plate leg is a PIXEL comparison against the OPTIONS plate beside it rather than a
# fixed colour: the two are the same art with different lettering, so "as bright as its
# neighbour and as varied as its neighbour" is a claim about the plate being drawn that
# survives every future change to the plate's own palette.
# =====================================================================================
rm -f shots/g147_open.png shots/g147_plate.png shots/g147_classic.png
CXLOG=$(CNC3D_HUD=new ./cnc_eyes --scen SCG01EB --pack SCG01EA.pack $BASE --gfx \
        --script gate_codex.txt 2>&1)
CXRC=$?
printf '%s\n' "$CXLOG" | grep -E '^(CODEX\||SHOT\||SCRIPT\|end)' >> "$OUT"
# Classic: the same script, no --gfx and no new HUD. Every codex verb is expected to be
# inert here; what is measured is that avail is 0 and no plate is drawn.
CLLOG=$(./cnc_eyes --scen SCG01EB --pack SCG01EA.pack $BASE \
        --shot shots/g147_classic.png --ticks 8 2>&1)
CLRC=$?

CXAVAIL=$(printf '%s\n' "$CXLOG" | grep -c '^CODEX|state|open=1|avail=1|')
CXOPEN=$(printf '%s\n' "$CXLOG" | grep -c '^CODEX|open|')
CXSHOT=$(printf '%s\n' "$CXLOG" | grep -c -E '^SHOT\|shots/g147_(open|plate)\.png\|1280x720\|ok$')
# The stats, straight off the state lines, checked against the GPL tables:
#   E1   idata.cpp   strength 50, WEAPON_M16 (damage 15, rof 20), MPH_SLOW = 8
#   ORCA aadata.cpp  strength 125, WEAPON_DRAGON (30, 60), MPH_FAST = 40
#   APC  udata.cpp   strength 200, WEAPON_M60MG (15, 30), MPH_MEDIUM_FASTER = 35
CXAPC=$(printf '%s\n' "$CXLOG" | grep -c '|code=APC|name=APC|hp=200|dmg=15|spd=35|rof=30|')
CXE1=$(printf '%s\n' "$CXLOG" | grep -c '|code=E1|name=Minigunner|hp=50|dmg=15|spd=8|rof=20|')
CXORCA=$(printf '%s\n' "$CXLOG" | grep -c '|code=ORCA|name=Orca|hp=125|dmg=30|spd=40|rof=60|')
CXNOD=$(printf '%s\n' "$CXLOG" | grep -c '|house=NOD|cat=STRUCTURES|')
CXCLOSED=$(printf '%s\n' "$CXLOG" | grep -c '^CODEX|state|open=0|avail=1|')
CXDUCK=$(printf '%s\n' "$CXLOG" | grep -o 'duck=0\.[0-9][0-9]' | head -1)

# The roster the table says, computed from the header rather than remembered here.
CXWANT=$(python3 - <<'G147PY'
import re, os
p = os.path.join(os.environ.get("REPO", ".."), "game", "codex_table.h")
if not os.path.exists(p):
    p = "../game/codex_table.h"
try:
    src = open(p).read()
except Exception as e:
    print("G147TABLE|ERROR|%s" % e); raise SystemExit
rows = re.findall(r'\{\s*"([A-Z0-9]+)",\s*"[^"]*",\s*"[^"]*",\s*(\d+),\s*(\d+)u,', src, re.S)
for house, bit in (("GDI", 1), ("NOD", 2)):
    n = [0, 0, 0, 0]
    for _code, cat, h in rows:
        if int(h) & bit:
            n[int(cat)] += 1
    print("G147TABLE|%s|%s" % (house, ",".join(str(x) for x in n)))
G147PY
)
printf '%s\n' "$CXWANT" >> "$OUT"
CXGDI=$(printf '%s\n' "$CXWANT" | sed -n 's/^G147TABLE|GDI|//p')
CXGDI_S=$(printf '%s' "$CXGDI" | cut -d, -f1)
CXGDI_V=$(printf '%s' "$CXGDI" | cut -d, -f3)
CXSEEN_S=$(printf '%s\n' "$CXLOG" | grep -c "|house=GDI|cat=STRUCTURES|row=0|of=$CXGDI_S|")
CXSEEN_V=$(printf '%s\n' "$CXLOG" | grep -c "|house=GDI|cat=VEHICLES|row=3|of=$CXGDI_V|")

# THE STYLE LEG. The page has to match the game's other menus -- the same green, the same
# font, the same borders and frames. That is a claim about every pixel, and it is exactly
# the kind of claim that rots silently: one db_fill_rect
# with a grey in it and the page is back to being a different program's screen.
#
# So: sample two rectangles that only the PAGE draws (the list box, and the stats block
# under the model well -- neither contains the 3D model, the emblems or the DOS filigree
# art) and require every pixel to be one of the nine colours the pause dialog itself
# draws with. db_font_palette_grad turns out to be a FLAT palette, not a gradient (it is
# dialog.cpp's TPF_NOSHADOW arm: ink classes to `fore`, the rest to `back`), so the set
# really is exact and this can be a membership test rather than a hue test.
CXSTYLE=$(python3 - <<'G147STYLE'
from PIL import Image
# The pause dialog's own palette, resolved from dossidebar.pack's pal8 by index:
#   12 BLACK  3 CC_GREEN  4 TEXT_BRIGHT  41 TEXT_MEDIUM
#  140 GREEN_SHADOW  141 GREEN_BKGD  159 LIGHT_GREEN/GREEN_BOX  167 BRIGHT_GREEN
ALLOWED = [(0, 0, 0), (0, 168, 0), (84, 252, 84), (84, 176, 36),
           (40, 68, 36), (48, 84, 44), (60, 152, 56), (140, 200, 8)]
# Two page-only rectangles at --w 1280 (HUD scale 2), as (x0, y0, x1, y1) in framebuffer
# pixels. MEASURED off the page's own green borders rather than guessed: the list box's
# frame runs down columns 22-23 and 292-293, the detail box's down 308-309 and 1256-1257,
# and the model well's bottom edge is rows 440-441. So the interior of the list, and the
# stats block below the well, are page and nothing else -- no 3D model, no spinning
# emblem, no DOS filigree art. A rectangle that strays outside a box samples the
# TRANSPARENT surface, where the darkened battlefield shows through as greys, and the
# leg fails on the wash instead of on the style. That is how the first cut of it failed.
RECTS = [(28, 160, 288, 694), (316, 448, 1250, 692)]
try:
    im = Image.open('shots/g147_open.png').convert('RGB')
except Exception as e:
    print('G147STYLE|ERROR|%s' % e); raise SystemExit
p = im.load()
W, H = im.size
bad = {}
tot = 0
for x0, y0, x1, y1 in RECTS:
    for y in range(max(0, y0), min(H, y1)):
        for x in range(max(0, x0), min(W, x1)):
            c = p[x, y]
            tot += 1
            d = min((c[0]-a[0])**2 + (c[1]-a[1])**2 + (c[2]-a[2])**2 for a in ALLOWED)
            if d > 144:                       # 12 levels, for any presentation chain
                bad[c] = bad.get(c, 0) + 1
nbad = sum(bad.values())
worst = sorted(bad.items(), key=lambda kv: -kv[1])[:3]
print('G147STYLE|sampled=%d|off=%d|frac=%.4f|worst=%s'
      % (tot, nbad, (nbad / tot) if tot else 1.0, worst))
G147STYLE
)
printf '%s\n' "$CXSTYLE" >> "$OUT"
CXSTYLE_FRAC=$(printf '%s\n' "$CXSTYLE" | sed -n 's/^G147STYLE|.*|frac=\([0-9.]*\)|.*/\1/p')
CXSTYLE_OK=$(python3 -c "
try:
    print('ok' if float('${CXSTYLE_FRAC:-1}') <= 0.01 else 'no')
except ValueError:
    print('badnum')")

CXPIX=$(python3 - <<'G147PIX'
from PIL import Image
# THE PLATE, MEASURED WHERE THE PLATE IS.
#
# The first cut of this leg sampled x340..620 and was green on a rectangle the plate has
# never occupied: the tab strip draws at the HUD's own scale (1 at this window size), so
# OPTIONS is x0..159 and DATABASE x160..319 -- while the codex PAGE is clamped to scale 2
# by its 1024 texture, which is where the 340 came from. Two different scales on one
# screen, and the leg was reading the wrong one. Proven wrong by measurement, not by
# argument: a good pack and a pack with tab_database removed both scored identically
# there, so the gate could not fail.
#
# Rows 0..7 only. The camera readout is painted across both plates from row 8 down in
# every run this harness makes, and it is not part of what is being measured.
#
# Two claims, and the second is what makes the first mean anything:
#   ENHANCED  the DATABASE slot is bright AND matches the OPTIONS plate beside it -- same
#             art, same frame, both unlatched. A stale pack blits nothing into that slot,
#             so it reads black beside a bright neighbour and this fails.
#   CLASSIC   the same slot is dark, because the Enhanced HUD is not drawn at all there.
def stats(f, x0, x1, y0, y1):
    im = Image.open(f).convert('RGB'); p = im.load()
    v = [sum(p[x, y]) / 3.0 for y in range(y0, y1) for x in range(x0, x1)]
    n = len(v) or 1
    m = sum(v) / n
    return m, (sum((t - m) ** 2 for t in v) / n) ** 0.5
try:
    eo, _   = stats('shots/g147_plate.png', 10, 150, 0, 8)     # OPTIONS, always drawn
    ed, esd = stats('shots/g147_plate.png', 170, 310, 0, 8)    # where DATABASE goes
    cd, _   = stats('shots/g147_classic.png', 170, 310, 0, 8)  # the same slot in Classic
    print('G147PIX|opt=%.1f|db=%.1f/%.1f|match=%.1f|classic=%.1f'
          % (eo, ed, esd, abs(eo - ed), cd))
except Exception as e:
    print('G147PIX|ERROR|%s' % e)
G147PIX
)
printf '%s\n' "$CXPIX" >> "$OUT"
CXP_OPT=$(printf '%s\n' "$CXPIX" | sed -n 's/^G147PIX|opt=\([0-9.]*\)|.*/\1/p')
CXP_DB=$(printf '%s\n' "$CXPIX" | sed -n 's/^G147PIX|.*|db=\([0-9.]*\)\/.*/\1/p')
CXP_MATCH=$(printf '%s\n' "$CXPIX" | sed -n 's/^G147PIX|.*|match=\([0-9.]*\)|.*/\1/p')
CXP_CLASSIC=$(printf '%s\n' "$CXPIX" | sed -n 's/^G147PIX|.*|classic=//p')
CXHIT_DB=$(printf '%s\n' "$CXLOG" | grep -c '^CODEX|hit|at=240,16|chrome=DATABASE|avail=1$')
# THE PAGE MUST START BELOW THE STRIP, and this is the number rather than a picture of it.
# The strip is drawn at the HUD's scale and the page at its own; where the two diverged
# the page's top edge rode UP behind the OPTIONS and DATABASE plates, with its green
# border and the faction emblem passing underneath them. Both figures are in framebuffer
# pixels and the page's must be the larger.
# TAIL, NOT HEAD, AND NON-ZERO. The script's FIRST `codex state` runs before the page has
# ever been opened, so codex_layout has not run and every one of these is still 0 -- and
# `0 >= 0` passes, which is a leg that cannot fail. Take a reading from after the open and
# require the strip to be a real measurement.
CXTOPOK=$(printf '%s\n' "$CXLOG" | grep -o '|strip=[0-9]*|boxtop=[0-9]*' | tail -1 \
          | awk -F'[=|]' '{ print ($3 > 0 && $5 >= $3) ? "ok" : "no" }')
CXTOPS=$(printf '%s\n' "$CXLOG" | grep -o '|scale=[0-9]*|strip=[0-9]*|boxtop=[0-9]*' | tail -1)
CXDUCKTO=$(printf '%s\n' "$CXLOG" | grep -o 'duckto=[0-9.]*' | head -1 | cut -d= -f2)
CXHIT_OPT=$(printf '%s\n' "$CXLOG" | grep -c '^CODEX|hit|at=80,16|chrome=OPTIONS|avail=1$')
CXPLATE=$(python3 -c "
import sys
try:
    o=float('${CXP_OPT:-0}'); d=float('${CXP_DB:-0}')
    m=float('${CXP_MATCH:-999}'); c=float('${CXP_CLASSIC:-999}')
except ValueError:
    print('badnum'); sys.exit()
print('ok' if (d > 60.0 and o > 60.0 and m < 25.0 and c < 20.0) else 'no')")

if [ "$CXRC" != "0" ] || [ "$CLRC" != "0" ]; then
  bad "G147 the DATABASE tab: a run failed (enhanced exit=$CXRC classic exit=$CLRC). Every measurement below would be of stale output"
elif [ "${CXSHOT:-0}" != "2" ]; then
  bad "G147 the DATABASE tab: $CXSHOT of the two screenshots were written (want 2), so the plate leg would read a stale file"
elif [ "${CXOPEN:-0}" -lt 1 ] || [ "${CXAVAIL:-0}" -lt 1 ]; then
  bad "G147 the DATABASE tab: it did not open in Enhanced. open lines=$CXOPEN, open-and-available states=$CXAVAIL (want at least 1 of each). Enhanced needs --gfx AND CNC3D_HUD=new; the gate passes both"
elif [ "${CXHIT_OPT:-0}" != "1" ] || [ "${CXHIT_DB:-0}" != "1" ]; then
  bad "G147 the DATABASE tab: the tab strip's hit test does not answer DATABASE beside OPTIONS. options-at-80,16=$CXHIT_OPT database-at-240,16=$CXHIT_DB (want 1 each). The OPTIONS reading is the control: if BOTH are 0 the whole strip has moved and this gate is measuring the wrong rectangle"
elif [ "$CXSTYLE_OK" != "ok" ]; then
  bad "G147 the DATABASE tab: the page is not drawn in the pause dialog's palette. [$CXSTYLE]. Every pixel of the list box and the stats block must be one of the nine colours dosopt.c draws with (BLACK, CC_GREEN, TEXT_BRIGHT, TEXT_MEDIUM, GREEN_SHADOW, GREEN_BKGD, GREEN_BOX, BRIGHT_GREEN); more than 1% are not. The `worst` list names the intruders -- a grey or a gold there means a db_ call went back to the SIDEBAR's palette"
elif [ "${CXTOPOK:-no}" != "ok" ]; then
  bad "G147 the DATABASE tab: the page starts INSIDE the tab strip [$CXTOPS]. boxtop must be at or below strip, both in framebuffer pixels; above it the page's green border and the faction emblem are drawn underneath the OPTIONS and DATABASE plates. The strip is drawn at the HUD scale and the page at its own, so the top edge has to be measured, not assumed equal"
elif [ "${CXDUCKTO:-0}" != "0.60" ]; then
  bad "G147 the DATABASE tab: the music floor is ${CXDUCKTO:-absent}, not the 0.60 asked for (a 40% cut). CODEX_DUCK_TO in game/codex_mod.h is the one place it is set"
elif [ "$CXPLATE" != "ok" ]; then
  bad "G147 the DATABASE tab: the plate is not on the glass where the hit test says it is. [$CXPIX]. In Enhanced, x170..310 of rows 0..7 must be bright (>60) and within 25 mean levels of the OPTIONS plate at x10..150 -- same art, same frame -- and in Classic the same rectangle must be dark (<20) because the Enhanced HUD is not drawn there at all. A stale hud640.pack blits nothing into that slot and fails this with db near zero beside a bright opt"
elif [ "${CXSEEN_S:-0}" -lt 1 ] || [ "${CXSEEN_V:-0}" -lt 1 ]; then
  bad "G147 the DATABASE tab: the roster on screen is not codex_table.h's. The header says GDI has $CXGDI_S structures and $CXGDI_V vehicles; the run reported structures-match=$CXSEEN_S vehicles-match=$CXSEEN_V (want at least 1 each). Re-run tools/database_codex/gen_codex_table.py and rebuild"
elif [ "${CXE1:-0}" -lt 1 ] || [ "${CXORCA:-0}" -lt 1 ] || [ "${CXAPC:-0}" -lt 1 ]; then
  bad "G147 the DATABASE tab: a stat does not match the GPL source. Minigunner 50/15/8/20=$CXE1, Orca 125/30/40/60=$CXORCA, APC 200/15/35/30=$CXAPC (want at least 1 each). These are idata.cpp, aadata.cpp and udata.cpp's own numbers, not this gate's"
elif [ "${CXNOD:-0}" -lt 1 ] || [ "${CXCLOSED:-0}" -lt 1 ]; then
  bad "G147 the DATABASE tab: the house switch or the close did not take. nod-states=$CXNOD closed-states=$CXCLOSED (want at least 1 each)"
elif [ -z "$CXDUCK" ]; then
  bad "G147 the DATABASE tab: the music never ducked. No duck=0.xx appeared anywhere in the run, so either cnc_audio_set_music_volume is not being driven or the fade is not running on the WALL CLOCK -- the world is stopped while the page is up, so a tick-driven fade would sit still and print 1.00 for ever"
else
  ok "G147 the DATABASE tab: the page is drawn entirely in the pause dialog's own nine colours (off-palette fraction $CXSTYLE_FRAC), the strip answers OPTIONS then DATABASE across its first two slots and the plate is really there (OPTIONS $CXP_OPT, DATABASE $CXP_DB, matching to $CXP_MATCH levels, against $CXP_CLASSIC for the same rectangle in Classic), the roster is codex_table.h's own ($CXGDI_S GDI structures, $CXGDI_V vehicles), the Minigunner, Orca and APC rows carry idata/aadata/udata's own numbers, the house switch and the close both take, the page clears the tab strip [$CXTOPS], the music floor is $CXDUCKTO and the fade has STARTED ($CXDUCK). It proves the duck is running, NOT that it lands on 0.75: the fade is 1.5 seconds of wall time and a script walks the whole screen in milliseconds, so no scripted run can reach the floor. The floor is code, not a measurement here"
fi

# =====================================================================================
# G160 THE TERRAIN'S WRECKED AIRFRAME: baked, found, drawn, and ON THE GROUND.
#
# WHAT IT COVERS. Model-table slot 205 (display list dl_012DBC8) is the one structural
# model on the cartridge that no pack baked and nothing drew. The cartridge draws it from
# its cell-decoration table, entry 38, wherever a map lays PC template 70 -- P04, which on
# the PC is a flat 1x1 rock patch. Four separate things have to be true for it to appear
# and each of them can break on its own, so each is a leg here:
#
#   1. THE BAKE. The baker walks tools/bakery/support/unit_models.json; drop the P04 entry
#      and the mesh is in no pack at all. WRECKDUMP reports mesh=-1 the moment that
#      happens, which is the direct tripwire this gate exists for.
#   2. THE READ. The cells come from GAME_STATE_STATIC_MAP, and StaticCells is the ONLY
#      field of CNCMapDataStruct that sits after ScenarioName, which is
#      char[_MAX_FNAME + _MAX_EXT]. Those macros are the host's to state and the brain's
#      to define, and while they disagreed the array read skewed and returned nothing but
#      garbage, silently. The STATICCELLS line carries a VERDICT WORD and this gate reads
#      that word. It deliberately does NOT name the offset: the value is 299 on this
#      platform because the brain's wwstd.h sets 255 + 8 off Windows, and it is a
#      different, equally correct number on a Windows build where the CRT's 256 + 256
#      stand. A gate pinned to 299 would assert "they agree at 299" and would go red on a
#      correct Windows build. AGREEMENT is the property; the number is not.
#   3. THE SELECTIVITY. Three maps, because "found 2 cells" proves nothing if the scan
#      matches everything: SCG10EA places exactly two (18,32 and 38,44), SCB33EA places
#      the most of any shipped map at fourteen, and SCG01EA places none. All three counts
#      come from the shipped map data and were confirmed against the baked terrain.
#   4. THE DRAW, IN THE RIGHT PLACE AND AT THE RIGHT HEIGHT. Self-baselining, with no
#      stored reference to rot: shoot the identical frame twice, once with the pass on and
#      once with `wreck 0`, and diff. Measured on the first green run: 347 pixels differ,
#      and over that changed set the wreck-on frame means RGB (108.9,108.9,108.9) with a
#      colour spread of 0.00 -- perfectly neutral, which is what this model is, every
#      vertex colour r = g = b -- while the wreck-off frame means (52.0,56.6,44.2) at
#      spread 20.32, green ground. Counting "grey pixels" alone would pass on the map's
#      own rock speckle; requiring the SAME pixels to be neutral with the pass on and
#      green with it off cannot.
#
#      AND THE POSITION IS ASSERTED, NOT ASSUMED. A sibling gate once reported PASS with
#      its crate floating half a cell in the air, because it checked only that a mesh had
#      been drawn. So WRECKDUMP prints, per cell, the screen point that the TERRAIN
#      HEIGHT under the anchor projects to -- the ground, not the model's origin, so that
#      a lift moves the pixels without moving the reference -- and this gate holds the
#      changed pixels against it. Measured: ground point (640.0, 96.7); changed pixels
#      span columns 622..657 and rows 80..103 with centroid (639.4, 93.9). So the
#      silhouette reaches 6.3 px BELOW the ground point (the model's own lowest vertex is
#      at -70 of 1024 model units, i.e. slightly into the ground) and stands 16.7 px above
#      it (+259 units), and its centroid is within a pixel of the ground point in x. Lift
#      it half a cell and every one of those numbers moves by tens of pixels.
cat > gate_wreck.txt <<'EOS'
tick 20
cheats
cheatset fog 0
cam 18.5 40
tick 1
shot shots/g160_on.png
wreckdump
wreck 0
shot shots/g160_off.png
quit
EOS
cat > gate_wreck_count.txt <<'EOS'
tick 2
wreckdump
quit
EOS
# THE SECOND MISSION IN THE SAME PROCESS. The mission-restart fetch fills its OWN
# CNCMapDataStruct, so without a re-scan on that path the cell list from mission one
# survives into mission two: wrecks drawn on cells that do not carry the template and
# missing from the ones that do. SCG10EA has two and SCG01EA has none, so a carried-over
# list reads 2 where it must read 0, which is the sharpest pair of maps in the campaign
# for this.
cat > gate_wreck_restart.txt <<'EOS'
tick 2
wreckdump
remission SCG01EA SCG01EA.pack
tick 2
wreckdump
quit
EOS
# THE SHOT COMES BEFORE THE DUMP ON PURPOSE. The screen point in WRECKDUMP is projected
# with the last framebuffer size draw_frame was handed, and a tick does not draw, so
# asking for the dump first would project with the startup default instead of the size
# the picture was taken at. The fb= field on the END line is there to make that visible.
gbegin shots/g160_on.png shots/g160_off.png shots/g160.log shots/g160_b33.log shots/g160_g01.log shots/g160_re.log
grun shots/g160.log     --scen SCG10EA --pack SCG10EA.pack $BASE --script gate_wreck.txt
grun shots/g160_b33.log --scen SCB33EA --pack SCB33EA.pack $BASE --script gate_wreck_count.txt
grun shots/g160_g01.log --scen SCG01EA --pack SCG01EA.pack $BASE --script gate_wreck_count.txt
grun shots/g160_re.log  --scen SCG10EA --pack SCG10EA.pack $BASE --script gate_wreck_restart.txt
gshots shots/g160_on.png shots/g160_off.png
WKLOG=$(cat shots/g160.log)
WKEND=$(echo "$WKLOG" | grep '^WRECKDUMP-END|' | head -1)
WKCELLS=$(echo "$WKEND" | sed -n 's/^WRECKDUMP-END|cells=\([0-9]*\)|.*/\1/p')
WKDRAWN=$(echo "$WKEND" | sed -n 's/^WRECKDUMP-END|cells=[0-9]*|drawn=\([0-9]*\)|.*/\1/p')
WKMESH=$(echo "$WKEND" | sed -n 's/^WRECKDUMP-END|cells=[0-9]*|drawn=[0-9]*|mesh=\(-\{0,1\}[0-9]*\)|.*/\1/p')
# ONE grep -c OVER TWO PATTERNS COUNTS LINES, NOT DISTINCT CELLS, so two copies of 18,32
# and none of 38,44 scored exactly the same as one of each and the leg could not tell them
# apart. Counted separately and required to be one apiece. Proven by mutation: reporting
# 18,32 twice takes WKAT38 to 0 and the leg red, where the single grep stayed green.
WKAT18=$(echo "$WKLOG" | grep -c '^WRECKDUMP|18,32|mesh=[0-9]')
WKAT38=$(echo "$WKLOG" | grep -c '^WRECKDUMP|38,44|mesh=[0-9]')
WKAT=$((WKAT18 + WKAT38))
# The verdict WORD, and nothing about the offset it reports beside it. See leg 2 above.
WKAGREE=$(echo "$WKLOG" | grep -c '^STATICCELLS|verdict=AGREE|')
WKSKEW=$(echo "$WKLOG" | grep -c -e '^STATICCELLS|verdict=DISAGREE|' -e '^STATICCELLS|verdict=NOCELLS|')
WKB33=$(sed -n 's/^WRECKDUMP-END|cells=\([0-9]*\)|.*/\1/p' shots/g160_b33.log | head -1)
WKG01=$(sed -n 's/^WRECKDUMP-END|cells=\([0-9]*\)|.*/\1/p' shots/g160_g01.log | head -1)
WKRE1=$(sed -n 's/^WRECKDUMP-END|cells=\([0-9]*\)|.*/\1/p' shots/g160_re.log | sed -n 1p)
WKRE2=$(sed -n 's/^WRECKDUMP-END|cells=\([0-9]*\)|.*/\1/p' shots/g160_re.log | sed -n 2p)
WKREBOOT=$(grep -c '^REMISSION|booted|SCG01EA|' shots/g160_re.log)
WKPIX=$(python3 - <<'PY'
from PIL import Image
import numpy as np
import re, sys

# THE REFERENCE POINT COMES OUT OF THE RUN'S OWN LOG, not out of this script. A box
# written down here would be a second copy of the camera, and it would keep agreeing with
# itself after the pass started drawing on the wrong cell.
sc = sr = None
for ln in open('shots/g160.log', errors='replace'):
    m = re.match(r'^WRECKDUMP\|18,32\|.*\|screen=(-?[0-9.]+),(-?[0-9.]+)$', ln.strip())
    if m:
        sc, sr = float(m.group(1)), float(m.group(2))
        break
if sc is None:
    print("0 0 0 0 0 -1 -1 -1 -1 -1 -1")
    sys.exit(0)

a = np.asarray(Image.open('shots/g160_on.png').convert('RGB')).astype(int)
b = np.asarray(Image.open('shots/g160_off.png').convert('RGB')).astype(int)
d = (a != b).any(axis=2)
rows, cols = np.nonzero(d)
n = int(d.sum())
if n == 0:
    print("0 0 0 0 0 -1 -1 -1 -1 -1 -1")
    sys.exit(0)

# POSITION. A generous box around the ground point the renderer itself reported: 60 px
# either way against a 1280x720 frame, which is wide enough that the model's own 36x24
# silhouette sits inside it comfortably and narrow enough that one cell of misplacement
# leaves it.
box = (np.abs(cols - sc) <= 60) & (np.abs(rows - sr) <= 60)
inside, outside = int(box.sum()), int(n - box.sum())
ccol, crow = float(cols.mean()), float(rows.mean())
dcol, drow = abs(ccol - sc), abs(crow - sr)
# ON THE GROUND. How far the silhouette reaches below the projected ground point, and how
# far it stands above it. Both are properties of where the pixels landed; neither can be
# satisfied by a model hanging in the air, and neither moves when the lift moves, because
# the reference is the terrain height and not the model's origin.
below = float(rows.max()) - sr
above = sr - float(rows.min())
posok = 1 if (dcol <= 8.0 and drow <= 10.0) else 0
gndok = 1 if (2.0 <= below <= 15.0 and above >= 8.0) else 0

on, off = a[d], b[d]
spread_on  = float((abs(on[:, 0] - on[:, 1]) + abs(on[:, 1] - on[:, 2])).mean())
spread_off = float((abs(off[:, 0] - off[:, 1]) + abs(off[:, 1] - off[:, 2])).mean())
grey  = spread_on <= 3.0 and on.mean() >= 60.0
green = spread_off >= 8.0 and off[:, 1].mean() > off[:, 0].mean()
colok = 1 if (grey and green) else 0

print("%d %d %d %d %d %.1f %.1f %.1f %.1f %.2f %.2f"
      % (inside, outside, posok, gndok, colok, dcol, drow, below, above,
         spread_on, spread_off))
PY
)
echo "G160PIX|$WKPIX" >> "$OUT"
set -- $WKPIX
WKIN="$1"; WKOUT="$2"; WKPOS="$3"; WKGND="$4"; WKCOL="$5"
WKDCOL="$6"; WKDROW="$7"; WKBELOW="$8"; WKABOVE="$9"; WKSPON="${10}"; WKSPOFF="${11}"
if [ "$GRC" != "0" ]; then
  bad "G160 terrain wreck: a run itself failed or wrote no shot (GRC=$GRC), so every number below would be the previous run's"
elif [ "${WKAGREE:-0}" -lt 1 ] || [ "${WKSKEW:-0}" != "0" ]; then
  bad "G160 terrain wreck: the static-map layout check did not report agreement (AGREE lines=$WKAGREE want at least 1, DISAGREE/NOCELLS lines=$WKSKEW want 0). THIS IS THE SKEW ITSELF: CNCMapDataStruct::ScenarioName is char[_MAX_FNAME + _MAX_EXT] and the host must set those two macros to the same values the brain compiled with, or StaticCells is read hundreds of bytes off and every cell name comes back as garbage with no crash. The STATICCELLS line in the log carries both offsets; this gate reads only the verdict word beside them, because the offset is platform dependent and a build that computes a different one is still correct"
elif [ -z "$WKMESH" ] || [ "$WKMESH" -lt 0 ]; then
  bad "G160 terrain wreck: the pack carries no P04 mesh (mesh=$WKMESH). THE MESH IS NOT BEING BAKED: the baker walks tools/bakery/support/unit_models.json and slot 205's entry has gone from it, or the packs in this run folder are older than that file"
elif [ "$WKCELLS" != "2" ] || [ "$WKDRAWN" != "2" ] || [ "$WKAT" != "2" ]; then
  bad "G160 terrain wreck: SCG10EA cells=$WKCELLS(want 2) drawn=$WKDRAWN(want 2) named-cells=$WKAT(want 2, the two the map really has at 18,32 and 38,44). cells=0 means the scan no longer recognises P04_i0.tga; drawn<cells means the cull is rejecting a cell the camera can see"
elif [ "${WKB33:-x}" != "14" ] || [ "${WKG01:-x}" != "0" ]; then
  bad "G160 terrain wreck: the scan is not selective. SCB33EA=$WKB33(want 14, the most of any shipped map) SCG01EA=$WKG01(want 0, a map that lays none). A non-zero SCG01EA means the match is catching templates it should not; a low SCB33EA means it is missing them"
elif [ "${WKREBOOT:-0}" != "1" ] || [ "${WKRE1:-x}" != "2" ] || [ "${WKRE2:-x}" != "0" ]; then
  bad "G160 terrain wreck: THE SECOND MISSION KEPT THE FIRST MISSION CELLS. In one process, SCG10EA then SCG01EA: booted=$WKREBOOT(want 1) mission-one=$WKRE1(want 2) mission-two=$WKRE2(want 0). 2 and 2 is the carry-over itself: the restart fetch fills its own CNCMapDataStruct, so unless that path re-scans, mission two draws mission one wrecks on cells that do not carry the template and misses the ones that do"
elif [ -z "$WKIN" ] || [ "$WKIN" -lt 250 ] || [ "$WKOUT" != "0" ]; then
  bad "G160 terrain wreck: changed-in-box=$WKIN(want >=250; 347 measured) changed-outside=$WKOUT(want 0). Zero changed pixels with mesh=$WKMESH means the pass runs and draws nothing; pixels outside the box mean it drew somewhere the renderer did not say the cell was"
elif [ "$WKPOS" != "1" ] || [ "$WKGND" != "1" ]; then
  bad "G160 terrain wreck: THE MODEL IS NOT WHERE THE GROUND IS. position-ok=$WKPOS(want 1) ground-ok=$WKGND(want 1). Against the ground point the renderer projected for cell 18,32, the changed pixels sit dcol=$WKDCOL px (want <=8.0) and drow=$WKDROW px (want <=10.0) away at their centroid, reach $WKBELOW px below it (want 2.0..15.0; 6.3 measured, which is the model's own -70 of 1024 units of nose in the dirt) and stand $WKABOVE px above it (want >=8.0; 16.7 measured). A model lifted off the terrain keeps every other leg of this gate green and fails exactly here, which is why this leg exists"
elif [ "$WKCOL" != "1" ]; then
  bad "G160 terrain wreck: neutral-on-green-off=$WKCOL(want 1) spread-on=$WKSPON(want <=3.0, the model is pure grey, every vertex colour r=g=b) spread-off=$WKSPOFF(want >=8.0, the ground under it is not). The right number of pixels changed in the right place and they are not the colour this model is"
else
  ok "G160 terrain wreck: the host and the brain AGREE on where StaticCells is (verdict word, not a pinned offset), SCG10EA's two P04 cells at 18,32 and 38,44 both resolve mesh $WKMESH and both pass the cull, SCB33EA finds 14 and SCG01EA finds 0, a second mission booted in the same process re-reads its own terrain (2 then 0), and $WKIN pixels turn from green ground (spread $WKSPOFF) to the model's own neutral grey (spread $WKSPON) in a silhouette centred $WKDCOL,$WKDROW px from the projected ground point that reaches $WKBELOW px below it and $WKABOVE px above it, with nothing outside the box moving"
fi

# =====================================================================================
# G173..G175 THE BONUS CRATES ARE THE CARTRIDGE'S OWN 3D CUBES.
#
# WHAT WAS WRONG. The console draws a goodie crate as a MODEL: the cell-decoration draw
# table at RAM 0x8020FF58 record 34 is {88, -1} and record 36 is {90, -1}, body model ids
# with no shadow set, and model slot = type id + 10, so those are model-table slots 98
# (grey steel, node RAM 0x801B775C, display list 0x011D4C8) and 100 (olive wood, node
# RAM 0x801B6930, display list 0x010F3C8), ten triangles each. This project drew a flat
# textured quad of the 1995 DOS sprite instead, because a crate is a terrain OVERLAY on a
# cell rather than an object in any heap, so nothing the object walk reaches ever named
# it and the baker had no entry for it. The two cubes reached 0 of the 94 mission packs.
#
# HOW BIG THE SWEEP HAD TO BE, and it is bigger than the map files suggest. 34 of the 100
# shipped mission INIs place a crate (27 wooden cells in 24 maps, 14 steel cells in 13),
# but two run-time paths place crates no INI lists: tiberiandawn/scenarioini.cpp calls
# MapClass::Place_Random_Crate once per player at scenario load whenever MPlayerGoodies is
# set (the skirmish and multiplayer lobby's Crates switch, on ANY map, including the
# eleven skirmish maps whose INIs list none), and tiberiandawn/logic.cpp then re-places one
# on a repeating CrateTimer every 7 to 15 game minutes for the rest of the session. Both
# are guarded by GameToPlay != GAME_NORMAL, so the campaign is on the INI path only. A
# run-time crate lands on a random clear cell, so no pack can be excluded and G173 asks
# every one of them.
#
# THE THREE GATES, and why the position one exists at all. G173 is the pack leg: the two
# cubes are in every mission pack, with the unreadable ones NAMED rather than skipped.
# G174 is the draw leg, and it asserts WHERE the cube went in numbers read back out of
# draw_mesh, not merely that something new appeared on the glass -- an earlier crate gate
# reported PASS with draw_mesh handed a half-cell lift, because its window was generous
# enough to swallow the shift and no leg anywhere asked for the position. G175 is the
# fallback: a pack with no crate mesh must still draw the 1995 sprites, so an old pack
# loses nothing.
# =====================================================================================

# ---- G173: the two cubes are in every mission pack ----------------------------------
G173=$(python3 "$GATEDIR/gate_cratepacks.py" "$RUNDIR" 2>&1)
echo "$G173" >> "$OUT"
G173N=$(printf '%s\n' "$G173" | sed -n 's/^CRATEPACKS|packs=\([0-9]*\)|.*/\1/p')
G173R=$(printf '%s\n' "$G173" | sed -n 's/^CRATEPACKS|.*|readable=\([0-9]*\)|.*/\1/p')
G173U=$(printf '%s\n' "$G173" | sed -n 's/^CRATEPACKS|.*|unreadable=\([0-9]*\)|.*/\1/p')
G173C=$(printf '%s\n' "$G173" | sed -n 's/^CRATEPACKS|.*|cube=\([0-9]*\)|.*/\1/p')
G173B=$(printf '%s\n' "$G173" | sed -n 's/^CRATEPACKS|.*|firstbad=\(.*\)$/\1/p')
if [ -z "$G173N" ]; then
  bad "G173 the 3D crates in the packs: gate_cratepacks.py printed no verdict line at all, so nothing below was measured. [$G173]"
elif [ "${G173N:-0}" -lt 90 ]; then
  bad "G173 the 3D crates in the packs: the walk found only ${G173N:-no} mission packs in $RUNDIR (want at least 90), so every count below is of nothing"
elif [ "${G173U:-1}" != "0" ]; then
  bad "G173 the 3D crates in the packs: $G173U of $G173N packs could not be READ, and a pack the gate cannot read is a pack it cannot vouch for. They are named on the UNREADABLE lines above. The reader parses CNC3DPK5 through CNC3DPKF; anything newer needs a branch in tools/bakery/packinspect.py before this gate means anything"
elif [ "$G173C" != "$G173R" ]; then
  bad "G173 the 3D crates in the packs: only $G173C of $G173R readable packs carry SCRATE and WCRATE as two DIFFERENT ten-triangle cubes of 185 x 185 x 184 units standing at y=+2. First offender: $G173B"
else
  ok "G173 the 3D crates in the packs: all $G173R mission packs in the run folder carry SCRATE and WCRATE as two different ten-triangle cubes, 185 x 185 x 184 mesh units with their floor at y=+2, at a confidence the renderer accepts, and every one of the $G173N packs was readable"
fi

# ---- G174: the cube is DRAWN, at the cell centre, on the ground ---------------------
# SCG32EA is the one map that proves both bindings in one frame: [OVERLAY] cell 3782 is
# SCRATE and 3783 is WCRATE, a steel crate at (6,59) with a wooden one immediately east.
# --noshroud because both cells start under the shroud on this map and a culled crate is
# not a crate that failed to draw.
sed 's#shots/crate_x.png#shots/g174_on.png#'  "$GATEDIR/gate_crate3d.txt"     > /tmp/g174on.txt
sed 's#shots/crate_x.png#shots/g174_off.png#' "$GATEDIR/gate_crate3d_off.txt" > /tmp/g174off.txt
gbegin shots/g174_on.png shots/g174_off.png shots/g174_hill.png shots/g174.log shots/g174h.log
grun shots/g174.log --scen SCG32EA --pack SCG32EA.pack $BASE --noshroud --script /tmp/g174on.txt
grun - --scen SCG32EA --pack SCG32EA.pack $BASE --noshroud --script /tmp/g174off.txt
# THE SECOND MAP, and it is not a duplicate. SCG32EA is FLAT: every corner of its PK9
# heightmap is the same value, so terrain_y is 0 across the whole map and a lift of zero
# there is satisfied by any renderer that ignores the ground entirely. SCB21EA's steel
# crate at (46,14) stands on corners of 190 against a map median of 65, which is +1.95
# world units up, so lift=0.0000 on THAT cell is the assertion that the cube follows the
# terrain. It is also a DESERT map and one of the three with crates and no tiberium, so
# the same run covers the theater the flat map does not and the early-return the crate
# pass was lifted out of.
grun shots/g174h.log --scen SCB21EA --pack SCB21EA.pack $BASE --noshroud --script "$GATEDIR/gate_crate3d_hill.txt"
gshots shots/g174_on.png shots/g174_off.png shots/g174_hill.png
C=$(cat shots/g174.log 2>/dev/null)
# THE POSITION, and it is the whole reason this gate is not a screenshot diff. ax and az
# are the anchor draw_mesh was HANDED and lift is that anchor's world y minus the terrain
# height under it, both read back out of draw_mesh rather than recomputed beside it. The
# cube is authored centred on its cell (x[-92,+93], z[-94,+90]) and standing on the ground
# (y from +2), so the only correct answer for a crate on cell (x,y) is x+0.5, y+0.5 and a
# lift of exactly zero. A half-cell lift prints lift=0.5000 here and reddens the gate.
CPOSS=$(printf '%s\n' "$C" | grep -c '^CRATE|6|59|SCRATE|mesh=[0-9][0-9]*|drawn=1|ax=6\.5000|az=59\.5000|lift=-\{0,1\}0\.0000$')
CPOSW=$(printf '%s\n' "$C" | grep -c '^CRATE|7|59|WCRATE|mesh=[0-9][0-9]*|drawn=1|ax=7\.5000|az=59\.5000|lift=-\{0,1\}0\.0000$')
CCELLS=$(printf '%s\n' "$C" | sed -n 's/^CRATEDUMP|cells=\([0-9]*\)|.*/\1/p')
CDRAWN=$(printf '%s\n' "$C" | sed -n 's/^CRATEDUMP|.*|drawn=\([0-9]*\)|.*/\1/p')
CPLACED=$(printf '%s\n' "$C" | sed -n 's/^CRATEDUMP|.*|placed=\([0-9]*\)|.*/\1/p')
CWOOD=$(printf '%s\n' "$C" | sed -n 's/^CRATEDUMP|.*|wood=\(-\{0,1\}[0-9]*\)|.*/\1/p')
CSTEEL=$(printf '%s\n' "$C" | sed -n 's/^CRATEDUMP|.*|steel=\(-\{0,1\}[0-9]*\)|.*/\1/p')
CART=$(printf '%s\n' "$C" | sed -n 's/^CRATEDUMP|.*|art=\([a-z]*\)$/\1/p')
CHILL=$(printf '%s\n' "$(cat shots/g174h.log 2>/dev/null)" | grep -c '^CRATE|46|14|SCRATE|mesh=[0-9][0-9]*|drawn=1|ax=46\.5000|az=14\.5000|lift=-\{0,1\}0\.0000$')
CHILLART=$(printf '%s\n' "$(cat shots/g174h.log 2>/dev/null)" | sed -n 's/^CRATEDUMP|.*|art=\([a-z]*\)$/\1/p')
CPIX=$(python3 "$GATEDIR/gate_crate3d.py" shots/g174_on.png shots/g174_off.png 2>&1)
echo "$CPIX" >> "$OUT"
CVERD=$(printf '%s\n' "$CPIX" | sed -n 's/^CRATEPIX|.*|verdict=\([a-z-]*\)$/\1/p')
if [ "$GRC" != "0" ]; then
  bad "G174 the 3D crate on the glass: a run failed (exit $GRC). Any number below would be read off a stale shot"
elif [ "$CART" != "cube" ] || [ "${CWOOD:-0}" -lt 0 ] || [ "${CSTEEL:-0}" -lt 0 ] || [ "$CWOOD" = "$CSTEEL" ]; then
  bad "G174 the 3D crate on the glass: the renderer resolved art=$CART with wood mesh $CWOOD and steel mesh $CSTEEL. Both must be real and DIFFERENT meshes; -1 on both means SCG32EA.pack was not re-baked and the 1995 sprites are drawing"
elif [ "${CCELLS:-0}" != "2" ] || [ "${CDRAWN:-0}" != "2" ] || [ "${CPLACED:-0}" != "2" ]; then
  bad "G174 the 3D crate on the glass: cells=$CCELLS drawn=$CDRAWN placed=$CPLACED, want 2 2 2. placed counts what the cube pass actually handed draw_mesh in the last frame, so placed=0 with drawn=2 means the dump ran before any frame was drawn, and cells<2 means the brain never exported the two overlay cells"
elif [ "${CPOSS:-0}" != "1" ] || [ "${CPOSW:-0}" != "1" ]; then
  bad "G174 the 3D crate POSITION: the steel crate must be drawn at ax=6.5000 az=59.5000 lift=0.0000 and the wooden one at ax=7.5000 az=59.5000 lift=0.0000, read back out of draw_mesh itself. Matches: steel=$CPOSS wood=$CPOSW (want 1 each). The lines the run printed were [$(printf '%s\n' "$C" | grep '^CRATE|' | tr '\n' ' ')]"
elif [ "${CHILL:-0}" != "1" ] || [ "$CHILLART" != "cube" ]; then
  bad "G174 the 3D crate ON A SLOPE: SCB21EA's steel crate at (46,14) stands 1.95 world units above that map's base level, so it is the run that proves the cube follows the terrain rather than sitting at world zero. It must report ax=46.5000 az=14.5000 lift=0.0000 with art=cube, and it read matches=$CHILL art=$CHILLART. The lines the run printed were [$(printf '%s\n' "$(cat shots/g174h.log 2>/dev/null)" | grep '^CRATE' | tr '\n' ' ')]"
elif [ "$CVERD" != "cube" ]; then
  bad "G174 the 3D crate on the glass: the picture leg says $CVERD. With the pass on and off, the two changed clusters must each be 150..520 pixels, 12..24 wide and 14..28 tall (the cartridge's cube subtends 16 x 21 here; the 1995 sprite subtends 34 x 25 and cannot enter that band), the steel one neutral grey and the wooden one olive, with nothing outside the two crate windows moving. [$CPIX]"
else
  ok "G174 the 3D crate on the glass: SCG32EA draws both cubes, the steel one at cell (6,59) anchored at 6.5,59.5 with zero lift and the wooden one at (7,59) anchored at 7.5,59.5 with zero lift, both positions read back out of draw_mesh; SCB21EA's crate on ground 1.95 units up reports zero lift too, so the cube follows the terrain; the picture leg reads $CPIX"
fi

# ---- G175: a pack with no crate mesh still draws the 1995 sprites --------------------
# THE FIXTURE, and it is built here rather than shipped: a byte copy of SCG32EA.pack with
# the two type-table NAMES overwritten in place, same length, so the renderer's lookup
# misses and it takes the fallback arm. That is a truer test than an old pack, because it
# is THIS pack with only the crate binding removed. Nothing is substituted in the run
# folder; the fixture lives in /tmp and the shipped pack is untouched.
python3 - "$RUNDIR/SCG32EA.pack" /tmp/g175_crateless.pack <<'G175PY' >> "$OUT" 2>&1
import sys
src, dst = sys.argv[1], sys.argv[2]
b = bytearray(open(src, "rb").read())
for old, new in ((b"SCRATE\x00\x00", b"\x01CRATE\x00\x00"),
                 (b"WCRATE\x00\x00", b"\x02CRATE\x00\x00")):
    n = b.count(old)
    assert n == 1, "%s appears %d times in %s, expected exactly once" % (old, n, src)
    i = b.find(old)
    b[i:i + len(old)] = new
open(dst, "wb").write(bytes(b))
print("G175FIXTURE|%s|%d bytes" % (dst, len(b)))
G175PY
sed 's#shots/crate_x.png#shots/g175_on.png#'  "$GATEDIR/gate_crate3d.txt"     > /tmp/g175on.txt
sed 's#shots/crate_x.png#shots/g175_off.png#' "$GATEDIR/gate_crate3d_off.txt" > /tmp/g175off.txt
gbegin shots/g175_on.png shots/g175_off.png shots/g175.log
grun shots/g175.log --scen SCG32EA --pack /tmp/g175_crateless.pack $BASE --noshroud --script /tmp/g175on.txt
grun - --scen SCG32EA --pack /tmp/g175_crateless.pack $BASE --noshroud --script /tmp/g175off.txt
gshots shots/g175_on.png shots/g175_off.png
S=$(cat shots/g175.log 2>/dev/null)
SART=$(printf '%s\n' "$S" | sed -n 's/^CRATEDUMP|.*|art=\([a-z]*\)$/\1/p')
SDRAWN=$(printf '%s\n' "$S" | sed -n 's/^CRATEDUMP|.*|drawn=\([0-9]*\)|.*/\1/p')
SPIX=$(python3 "$GATEDIR/gate_crate3d.py" shots/g175_on.png shots/g175_off.png 2>&1)
echo "$SPIX" >> "$OUT"
SN=$(printf '%s\n' "$SPIX" | sed -n 's/.*steel_n=\([0-9]*\) .*/\1/p')
SW=$(printf '%s\n' "$SPIX" | sed -n 's/.*steel_w=\([0-9]*\) .*/\1/p')
SRB=$(printf '%s\n' "$SPIX" | sed -n 's/.*steel_rb=\(-\{0,1\}[0-9]*\)|.*/\1/p')
SWN=$(printf '%s\n' "$SPIX" | sed -n 's/.*wood_n=\([0-9]*\) .*/\1/p')
if [ "$GRC" != "0" ]; then
  bad "G175 the 1995 crate sprites still draw: a run failed (exit $GRC). Any number below would be read off a stale shot"
elif [ "$SART" != "sprite" ]; then
  bad "G175 the 1995 crate sprites still draw: on a pack with no crate mesh the renderer must fall back, and it reported art=$SART. The fixture removes only the two type NAMES, so a 'cube' here means the lookup found them anyway and the fallback arm was never entered"
elif [ "${SDRAWN:-0}" != "2" ]; then
  bad "G175 the 1995 crate sprites still draw: drawn=$SDRAWN, want 2. A crate that disappears on an old pack is worse than a flat one: the bonus arrives with no picture at all"
elif [ -z "$SN" ] || [ "${SN:-0}" -lt 400 ] || [ "${SWN:-0}" -lt 400 ] || [ "${SW:-0}" -lt 28 ]; then
  bad "G175 the 1995 crate sprites still draw: the fallback must put the 1995 art on the glass at its own size, 646 pixels and 34 wide per crate as measured. It read steel_n=$SN wood_n=$SWN steel_w=$SW. [$SPIX]"
elif [ "${SRB:-0}" -gt -10 ]; then
  bad "G175 the 1995 crate sprites still draw: the 1995 steel sprite is BLUE-grey (measured r-b = -20) where the cartridge's cube is neutral (+3), and this leg read steel_rb=$SRB. That is the witness that the fallback drew the sprite and not some other crate art. [$SPIX]"
else
  ok "G175 the 1995 crate sprites still draw: on a pack whose two crate types have been taken out, the renderer reports art=sprite, still draws both crates, and puts $SN and $SWN pixels of the 1995 art on the glass at 34 wide with the steel one blue-grey (r-b=$SRB), so an un-rebaked pack loses no crates"
fi

# =====================================================================================
# G172 THE DESERT THEATER DRAWS DESERT FLORA, AND NO OTHER THEATER DOES.
#
# The cartridge ships one set of tree TYPES and swaps the MODEL under them at draw time
# when the theater is desert: the draw-command enqueue at RAM 0x8004A420 adds 97 to any
# model id in [103,121] when the theater word at RAM 0x80097150 is zero, and theater 0 is
# DESERT by the loader's own filename switch at RAM 0x801E5194. Ids 103..121 are exactly
# T01..T18, so the outputs are model slots 210..228, and those nineteen slots hold four
# meshes: two cacti and two scrub trees. A pack is baked for one scenario and so for one
# theater, which is why the baker resolves that swap when it writes the type table, and
# why the renderer needed no change at all: cnc_eyes.cpp mesh_for() already resolves a
# terrain object by looking its INI name up in that table.
#
# WITHOUT IT, 939 cells across the shipped desert missions plant a leafy temperate
# conifer on red sand: T08 is 558 of them over 44 missions, T18 320 over 44, T04 32 over
# 6, T09 29 over 8. T08 is also why the binding has to FOLLOW the theater rather than
# simply be corrected: it is the one tree registered in all three theater fields and it
# is placed on temperate maps too.
#
# THE THEATER IS NOT A STRING COMPARISON. missions/SCA01EA.INI says Theater=DESERT and
# CNC3DTheater=SAND, so that mission's pack carries the look name SAND; a gate keyed on
# "DESERT" would miss a genuine desert map and the last one did, reporting it as a skip
# under a pass line. The checker resolves the ENGINE theater from the scenario's own INI
# Theater= key and CROSS-CHECKS the pack's look name against it, and it counts the
# desert packs it checked against a count taken independently off the INIs, so it cannot
# pass by looking away. A file with the mission magic that will not parse is UNREAD, and
# unread is a failure.
#
# THIS GATE READS THE PACKS THE RUN FOLDER ACTUALLY SHIPS, so it is as red on stale DATA
# as on broken code. That is deliberate: a fixed baker and a pack baked before it look
# identical from the source, and only the pack can tell them apart.
G172T="$GATEDIR/../tools/bakery/g172_desert_flora.py"
if [ ! -f "$G172T" ]; then
  bad "G172 desert flora: $G172T is not there, so nothing was checked. A gate that cannot run is not a gate that passes"
else
  G172OUT=$(python3 "$G172T" --dir="$RUNDIR" 2>&1); G172RC=$?
  printf '%s\n' "$G172OUT" >> "$OUT"
  G172TOT=$(printf '%s\n' "$G172OUT" | grep '^G172|total|')
  G172P=$(printf '%s\n' "$G172TOT" | sed -n 's/.*|pass=\([0-9]*\).*/\1/p')
  G172F=$(printf '%s\n' "$G172TOT" | sed -n 's/.*|fail=\([0-9]*\).*/\1/p')
  G172U=$(printf '%s\n' "$G172TOT" | sed -n 's/.*|unread=\([0-9]*\).*/\1/p')
  G172D=$(printf '%s\n' "$G172TOT" | sed -n 's/.*|desert=\([0-9]*\).*/\1/p')
  G172M=$(printf '%s\n' "$G172TOT" | sed -n 's/.*|missed=\([0-9]*\).*/\1/p')
  G172BAD=$(printf '%s\n' "$G172OUT" | grep '^G172|FAIL|' | cut -d'|' -f3 | head -4 | tr '\n' ' ')
  G172WHY=$(printf '%s\n' "$G172OUT" | grep -m1 '^G172|FAIL|' | cut -d'|' -f4)
  G172UN=$(printf '%s\n' "$G172OUT" | grep '^G172|unread|' | cut -d'|' -f3 | tr '\n' ' ')
  if [ -z "$G172TOT" ]; then
    bad "G172 desert flora: the checker printed no total line (exit $G172RC), so every number below would be read off nothing. First lines: $(printf '%s\n' "$G172OUT" | head -3 | tr '\n' ' ')"
  elif [ "${G172P:-0}" = "0" ]; then
    bad "G172 desert flora: not one pack was opened and checked (pass=$G172P fail=$G172F unread=$G172U). Zero checked is not a pass, and the usual cause is a run folder with no mission packs in it"
  elif [ "${G172D:-0}" = "0" ]; then
    bad "G172 desert flora: $G172P packs were checked and NONE of them resolved to a desert map (desert=$G172D missed=$G172M), so the half of this gate that matters never ran"
  elif [ "${G172U:-0}" != "0" ]; then
    bad "G172 desert flora: $G172U file(s) carry the mission magic and could not be read at all ($G172UN), so their flora is unproven rather than correct. Unproven is not proven"
  elif [ "${G172F:-0}" != "0" ]; then
    bad "G172 desert flora: $G172F of the run folder's mission packs bind a tree name to the wrong theater's mesh, cannot have their theater resolved, or are missing one of the four cartridge meshes. A pack baked before the desert-flora bind reports exactly this, so re-bake and re-install before reading it as a code fault. First four: $G172BAD. First reason: $G172WHY"
  elif [ "${G172M:-0}" != "0" ]; then
    bad "G172 desert flora: $G172M scenario(s) have a pack here and an INI that says Theater=DESERT, yet were not checked as desert maps. The theater resolver and the mission INIs disagree, which is the exact hole a look-name comparison leaves"
  elif [ "$G172RC" != "0" ]; then
    bad "G172 desert flora: the checker exited $G172RC while every counter it printed was clean ($G172TOT). A non-zero exit with no named cause is a fault in the checker and is not a pass"
  else
    ok "G172 desert flora: all $G172P mission packs in the run folder, $G172D of them DESERT by their own mission INI, bind every one of T01..T18 to the mesh their ENGINE theater calls for; each desert pack carries all four cartridge meshes (dl_00FE360 12 tris 10 opaque + 2 shadow, dl_00FE660 8 tris 6 cutout + 2 shadow, dl_00FE918 8 tris 6 opaque + 2 shadow, dl_00FEBD8 8 tris 6 cutout + 2 shadow); and not one non-desert map draws a cactus"
  fi
fi

# =====================================================================================
# G176 THE RUNNING ENGINE RESOLVES THE DESERT TREES, and this is where the design call
# is settled by measurement rather than by argument.
#
# G172 reads the pack. This one asks the BINARY, through its own lookup path: the script
# command dumpobj resolves a type name in g_pack.type exactly as mesh_for() does and then
# reports the triangle count of the mesh it landed on. The two theaters give different
# quadruples and neither can be mistaken for the other:
#
#     DESERT     T04=12  T08=8  T09=8  T18=8      the two cacti and two scrub trees
#     TEMPERATE  T04=6   T08=6  T09=6  T18=11     the conifers
#
# THE DESERT SUBJECT IS SCA01EA ON PURPOSE. Its INI says Theater=DESERT and
# CNC3DTheater=SAND, so its pack's own theater field reads SAND: it is precisely the
# mission a look-name comparison drops. If the engine draws cacti there, the theater
# identity survived the one case that has already broken a gate on this project.
#
# THE THIRD RUN IS THE PLACEMENT LEG. SCA01EA places no terrain objects at all, so
# proving a binding on it says nothing about a tree standing anywhere. SCB01EA does:
# --dumpobj lists every live object with its world position, and the gate requires real
# T-type terrain objects standing inside the map on the same pack whose binding it just
# checked. Together that is "these cells hold trees, and a tree here is a cactus". It is
# NOT a pixel claim and does not pretend to be one; the suite holds no picture reference
# for the flora and that is recorded as a gap rather than dressed up here.
G176DIR="$RUNDIR/shots/g176obj"
rm -rf "$G176DIR"; mkdir -p "$G176DIR"
cat > /tmp/g176.txt <<'G176EOF'
dumpobj T04 shots/g176obj
dumpobj T08 shots/g176obj
dumpobj T09 shots/g176obj
dumpobj T18 shots/g176obj
quit
G176EOF
G176QUAD() {                       # $1 = log file -> "12 8 8 8"
  grep -E '^DUMPOBJ\|T(04|08|09|18)\|' "$1" \
    | sed -n 's/^DUMPOBJ|\(T[0-9][0-9]\)|[^|]*|tris=\([0-9]*\)|.*/\1 \2/p' \
    | sort | awk '{printf "%s%s", (NR>1?" ":""), $2}'
}
gbegin "$G176DIR/T04.obj" "$G176DIR/T08.obj" "$G176DIR/T09.obj" "$G176DIR/T18.obj"
grun /tmp/g176_sand.log --scen SCA01EA --pack SCA01EA.pack $BASE --noshroud --nosound \
     --script /tmp/g176.txt
G176SAND=$(G176QUAD /tmp/g176_sand.log)
gshots "$G176DIR/T04.obj" "$G176DIR/T08.obj" "$G176DIR/T09.obj" "$G176DIR/T18.obj"
grun /tmp/g176_temp.log --scen SCG01EA --pack SCG01EA.pack $BASE --noshroud --nosound \
     --script /tmp/g176.txt
G176TEMP=$(G176QUAD /tmp/g176_temp.log)
grun /tmp/g176_des.log --scen SCB01EA --pack SCB01EA.pack $BASE --noshroud --nosound \
     --script /tmp/g176.txt
G176DES=$(G176QUAD /tmp/g176_des.log)
# The placement leg is its OWN run, because --dumpobj's per-object dump lives on the
# --shot path and a --script run never reaches it. Asking for both in one invocation
# reports zero trees on a map that has nineteen, which is a gate lying quietly.
rm -f shots/g176_place.png
grun /tmp/g176_place.log --scen SCB01EA --pack SCB01EA.pack $BASE --noshroud --nosound \
     --dumpobj --ticks 2 --shot shots/g176_place.png
gshots shots/g176_place.png
# Placed trees, and their positions. kind 3 is K_TERRAIN; the cell must be on the map.
G176TREES=$(grep -cE '^P\|3\|T[0-9][0-9]\|' /tmp/g176_place.log)
# THE BOUND IS THE MAP'S OWN GRID, NOT 256. SCB01EA is 64x64, so a hardcoded 256 made this
# leg unfireable: every object on the map, and every object a hundred cells east of it,
# sat inside the bound. Proven by mutation: with the engine reporting every object 100
# cells east, the old awk still counted 0.
# THE GRID, NOT THE PLAYABLE RECT. wx/wz are grid coordinates, and the MAP line's size= is
# the playable rectangle inside it (SCG01EA: size=28x25 within a 64 grid), so bounding by
# that would red-flag legitimate objects. The pack line's cells= is the grid's own square.
G176CELLS=$(sed -n 's/^pack [A-Z0-9]*: .*cells=\([0-9]*\).*/\1/p' /tmp/g176_place.log | head -1)
G176DIM=$(awk -v c="${G176CELLS:-0}" 'BEGIN{ d=int(sqrt(c)+0.5); print (d>0)?d:256 }')
G176OFFMAP=$(grep -E '^P\|3\|T[0-9][0-9]\|' /tmp/g176_place.log \
  | sed -n 's/.*|wx=\([-0-9.]*\)|wz=\([-0-9.]*\)|.*/\1 \2/p' \
  | awk -v lim="$G176DIM" '($1 < 0 || $2 < 0 || $1 > lim || $2 > lim) { n++ } END { print n+0 }')
if [ "$GRC" != "0" ]; then
  bad "G176 the running engine resolves the desert trees: a cnc_eyes run failed (exit $GRC), so every count below would be read off a run that did not finish"
elif [ "$G176SAND" != "12 8 8 8" ]; then
  bad "G176 the running engine resolves the desert trees: on SCA01EA, whose INI says Theater=DESERT while its pack's look name says SAND, dumpobj reports T04/T08/T09/T18 triangle counts [$G176SAND] and the cartridge's desert meshes are [12 8 8 8]. [6 6 6 11] means the engine is drawing temperate conifers on sand, which is exactly what a look-name comparison lets through"
elif [ "$G176DES" != "12 8 8 8" ]; then
  bad "G176 the running engine resolves the desert trees: on SCB01EA (Theater=DESERT) dumpobj reports [$G176DES], wanted [12 8 8 8]"
elif [ "$G176TEMP" != "6 6 6 11" ]; then
  bad "G176 the running engine resolves the desert trees: on SCG01EA (Theater=TEMPERATE) dumpobj reports [$G176TEMP], wanted the temperate conifers [6 6 6 11]. This is the leg that proves the counter can tell the two apart, so a desert quadruple here means the swap is unconditional and nineteen temperate missions just grew cacti"
elif [ "${G176TREES:-0}" -lt 5 ]; then
  bad "G176 the running engine resolves the desert trees: SCB01EA reported $G176TREES placed T-type terrain objects (want at least 5). A binding proved over a map with no trees on it proves nothing about anything standing anywhere"
elif [ "${G176OFFMAP:-0}" != "0" ]; then
  bad "G176 the running engine resolves the desert trees: $G176OFFMAP of SCB01EA's $G176TREES placed trees report a world position off the map. A mesh bound correctly and planted nowhere is not a fix"
else
  ok "G176 the running engine resolves the desert trees: loading the shipped packs, cnc_eyes resolves T04/T08/T09/T18 through its own type table to [12 8 8 8] on SCA01EA (Theater=DESERT, look name SAND) and on SCB01EA, and to the conifers [6 6 6 11] on SCG01EA, while SCB01EA stands $G176TREES T-type terrain objects on the map with every world position inside it"
fi

# =====================================================================================
# G180..G182 THE REPAIR WRENCH IS THE CARTRIDGE'S OWN, IT TURNS, AND IT NEVER BLINKS.
#
# WHY THERE WAS NO GATE HERE BEFORE, and it is not an oversight anyone can be blamed for:
# nothing in the script language could start a building repairing. The repair latch lives
# on the sidebar, the only caller of sb_click is the SDL event loop, and a --script run
# never enters it. So the whole feature shipped untested. `repairmode` is the door, and it
# goes through sb_toggle_repair, so a build whose panel repair mode is broken cannot pass
# by it either.
#
# WHAT THIS GATE IS BUILT NOT TO DO, because its predecessor did all three. A gate on this
# feature was measured against two BUILT mutants and passed both: one with the animation
# entirely dead, one with the art REVERSED so the wrench turned backwards. It passed
# because every number it read was recomputed beside the draw instead of taken out of it,
# and because "the picture changed" cannot tell forwards from backwards. So:
#
#   * every number asserted here comes out of draw_mesh's own witnesses (WRENCH3D: the
#     DirType it was handed and the yaw facing_rot made of it). A build that computes the
#     facing correctly and then stops spending it moves `face` and not `yawdeg`, and the
#     yaw leg is what catches that;
#   * the step between consecutive frames is asserted as a SIGNED quantity in a narrow
#     window, so a dead animation (step 0) and a reversed one (step -10) are separate
#     failures with separate messages, and neither can be mistaken for the other;
#   * a full revolution is asserted, so a wrench that jitters between two facings cannot
#     pass by advancing;
#   * NO WALL-CLOCK FIGURE IS ASSERTED ANYWHERE. The renderer's frame rate has not been
#     measured. The period asserted is 25 ENGINE TICKS, which is what the clock is
#     actually driven by.
#
# The subject is SCG02EA's GoodGuy barracks, which the mission's own INI ships DAMAGED:
# [STRUCTURES] 006=GoodGuy,PYLE,88,3319,0,None, and 88 there is on the INI's 0..256 scale,
# which the engine resolves to 275 of the barracks' 800. So the repair this gate starts is
# the engine's own and nothing here fakes a state for it.
cat > gate_wrench.txt <<'EOS'
tick 20
cam 55 51
repairmode 1
lclickobj PYLE 0
tick 11
cursor3danim 0
shot shots/g181_rest.png
shot shots/g181_rest2.png
wrenchdump
cursor3danim 1
shot shots/g181_anim.png
wrenchdump
EOS
# 26 samples is one revolution and one over: the clock's period is 25 engine ticks, so a
# 25-sample window could be walked by a build that skips exactly one facing and still show
# every value. Each needs its own drawn frame -- g_wrenchDrawn is filled by the draw pass,
# so a dump with no `shot` in front of it reports the model and an empty drawn list.
i=1
while [ "$i" -le 26 ]; do
  printf 'tick 1\nshot shots/g181_seq.png\nwrenchdump\n' >> gate_wrench.txt
  i=$((i+1))
done
echo "quit" >> gate_wrench.txt
gbegin shots/g181_rest.png shots/g181_rest2.png shots/g181_anim.png shots/g181_seq.png \
       shots/g181.log
grun shots/g181.log --scen SCG02EA --pack SCG02EA.pack $BASE --noshroud --nosound \
     --script gate_wrench.txt
gshots shots/g181_rest.png shots/g181_rest2.png shots/g181_anim.png shots/g181_seq.png
# A sentinel that cannot pass: the python opens three PNGs, so on a run that wrote none it
# would die rather than return a verdict. Evaluated only when the run succeeded.
# Fifteen fields, in the python's own print order, every one of them a value that FAILS
# its own leg: a run that could not be measured must not be able to report a pass.
WR="-2 0 0 0 0 0 999 999 999 0 0 999 999 0 999"
if [ "$GRC" = "0" ]; then
WR=$(python3 - <<'PY'
import re
import numpy as np
from PIL import Image

log = open('shots/g181.log').read()

# One record per dump: the model line, the engine's own two bits, the quad the sprite
# fallback would have drawn, and what draw_mesh actually did.
blocks = log.split('WRENCHDUMP-BEGIN')[1:]
mesh = tris = -1
ndump = 0
countmin = 99
blink = set()
faces = []            # the drawn facing, animated samples only
yawbad = 0
anchorbad = 0
for b in blocks:
    b = b.split('WRENCHDUMP-END')[0]
    ndump += 1
    m = re.search(r'art=\d+ mesh=(-?\d+) count=(\d+) frames=(\d+) frame=(-?\d+) '
                  r'face=(-?\d+) tris=(\d+) opaque=(\d+) cutout=(\d+)', b)
    if not m:
        countmin = -1
        continue
    mesh, tris = int(m.group(1)), int(m.group(6))
    countmin = min(countmin, int(m.group(2)))
    for w in re.findall(r'\|repairing=1\|wrench=([01])\|', b):
        blink.add(int(w))
    d = re.search(r'WRENCH3D\|\S+?\|id=\d+\|mesh=(-?\d+)\|face=(-?\d+)\|yawdeg=([-0-9.]+)'
                  r'\|ax=([-0-9.]+)\|az=([-0-9.]+)\|ay=([-0-9.]+)', b)
    q = re.search(r'WRENCHQUAD\|\S+?\|id=\d+\|dimw=\d+\|x0=([-0-9.]+)\|x1=([-0-9.]+)'
                  r'\|y0=([-0-9.]+)\|y1=([-0-9.]+)\|', b)
    if not d:
        continue
    face = int(d.group(2)); yaw = float(d.group(3))
    ax, ay = float(d.group(4)), float(d.group(6))
    # THE YAW WITNESS. facing_rot turns a DirType into a CLOCKWISE angle, so the drawn
    # yaw must be face*360/256 to within the print's own rounding. face -1 is "no yaw"
    # and must read 0.
    want = 0.0 if face < 0 else (face * 360.0 / 256.0)
    if abs(((yaw - want + 180.0) % 360.0) - 180.0) > 0.5:
        yawbad += 1
    # WHERE IT WENT, cross-checked against the quad the sprite it replaces would have
    # taken. Two independently computed numbers: the quad comes from collect_wrenches,
    # the anchor from draw_mesh. They must be the same point.
    if q:
        if abs(ax - (float(q.group(1)) + float(q.group(2))) * 0.5) > 0.001:
            anchorbad += 1
        elif abs(ay - (float(q.group(3)) + float(q.group(4))) * 0.5) > 0.001:
            anchorbad += 1
    if face >= 0:
        faces.append(face)

# THE STEP, SIGNED, in DirType units. The law is face = frame*256/period with the frame
# climbing by 4 a tick and the period 100, so every step is +10 or +11 and the wrap is
# just another +11. Dead is 0; backwards is -10.
steps = [((faces[i + 1] - faces[i] + 128) % 256) - 128 for i in range(len(faces) - 1)]
dead = sum(1 for s in steps if s == 0)
back = sum(1 for s in steps if s < 0)
odd  = sum(1 for s in steps if s not in (10, 11))
# ONE FULL REVOLUTION: 25 consecutive steps must sum to exactly 256.
rev = 0
for i in range(len(steps) - 24):
    if sum(steps[i:i + 25]) == 256:
        rev = 1
        break
distinct = len(set(faces))

def px(a, b):
    x = np.asarray(Image.open('shots/' + a).convert('RGB')).astype(int)
    y = np.asarray(Image.open('shots/' + b).convert('RGB')).astype(int)
    return int(((np.abs(x - y).sum(axis=2)) > 24).sum())
moved = px('g181_anim.png', 'g181_rest.png')
control = px('g181_rest2.png', 'g181_rest.png')

print(mesh, tris, ndump, countmin, len(blink), len(steps), dead, back, odd, rev,
      distinct, yawbad, anchorbad, moved, control)
PY
)
fi
set -- $WR
W_MESH=$1; W_TRIS=$2; W_NDUMP=$3; W_COUNTMIN=$4; W_BLINK=$5; W_NSTEP=$6
W_DEAD=$7; W_BACK=$8; W_ODD=$9; shift 9
W_REV=$1; W_DISTINCT=$2; W_YAWBAD=$3; W_ANCHORBAD=$4; W_MOVED=$5; W_CONTROL=$6

# --- G180: the art and the placement --------------------------------------------------
if [ "$GRC" != "0" ]; then
  bad "G180 the repair wrench is the cartridge's own CUR05: the run failed (exit $GRC), so every number below would be read off a run that did not finish"
elif [ "${W_MESH:--2}" -lt 0 ]; then
  bad "G180 the repair wrench is the cartridge's own CUR05: wrenchdump reports mesh=$W_MESH, i.e. this pack carries no CUR05 and the 1995 sprite is what drew. Re-bake with tools/bakery/bake5.py"
elif [ "${W_TRIS:-0}" != "65" ]; then
  bad "G180 the repair wrench is the cartridge's own CUR05: the resolved mesh has $W_TRIS triangles, and the cartridge's cursor model 0x05 has 65. A different mesh under the right type code is the failure this counts"
elif [ "${W_NDUMP:-0}" -lt 28 ]; then
  bad "G180 the repair wrench is the cartridge's own CUR05: only $W_NDUMP wrenchdumps in the log, wanted 28. A short run cannot have walked a revolution"
elif [ "${W_ANCHORBAD:-1}" != "0" ]; then
  bad "G180 the repair wrench is the cartridge's own CUR05: $W_ANCHORBAD of $W_NDUMP frames put the model somewhere other than the point collect_wrenches computed for it. The anchor comes out of draw_mesh and the quad out of the collector, so these two disagreeing means the 3D draw is not standing where the sprite it replaces stood"
else
  ok "G180 the repair wrench is the cartridge's own CUR05: mesh $W_MESH, 65 triangles, drawn over the repairing barracks in all $W_NDUMP sampled frames, and draw_mesh's own anchor lands on the collector's point every time"
fi

# --- G181: it turns, and it turns the right way ---------------------------------------
if [ "$GRC" != "0" ]; then
  bad "G181 the repair wrench turns, clockwise, one revolution in 25 engine ticks: the run failed (exit $GRC)"
elif [ "${W_NSTEP:-0}" -lt 25 ]; then
  bad "G181 the repair wrench turns, clockwise, one revolution in 25 engine ticks: only $W_NSTEP steps between drawn facings, wanted at least 25"
elif [ "${W_DEAD:-1}" != "0" ]; then
  bad "G181 the repair wrench turns, clockwise, one revolution in 25 engine ticks: $W_DEAD of $W_NSTEP steps moved the drawn facing by ZERO. That is a dead animation, and it is the exact mutant the previous gate on this feature passed"
elif [ "${W_BACK:-1}" != "0" ]; then
  bad "G181 the repair wrench turns, clockwise, one revolution in 25 engine ticks: $W_BACK of $W_NSTEP steps moved the drawn facing BACKWARDS. The wrench is turning anticlockwise, which is the second mutant the previous gate passed"
elif [ "${W_ODD:-1}" != "0" ]; then
  bad "G181 the repair wrench turns, clockwise, one revolution in 25 engine ticks: $W_ODD of $W_NSTEP steps are not the +10 or +11 DirType the law produces (256 * 4 / 100 = 10.24 per tick)"
elif [ "${W_REV:-0}" != "1" ]; then
  bad "G181 the repair wrench turns, clockwise, one revolution in 25 engine ticks: no run of 25 consecutive steps sums to a full 256. It advances without ever going round, which is what a wrench rocking between two facings looks like"
elif [ "${W_DISTINCT:-0}" != "25" ]; then
  bad "G181 the repair wrench turns, clockwise, one revolution in 25 engine ticks: $W_DISTINCT distinct drawn facings, wanted the clock's 25"
elif [ "${W_YAWBAD:-1}" != "0" ]; then
  bad "G181 the repair wrench turns, clockwise, one revolution in 25 engine ticks: on $W_YAWBAD frames the yaw draw_mesh built does not match the facing it was handed. The facing is being computed and then not spent, which no picture comparison can see"
elif [ "${W_CONTROL:-1}" != "0" ]; then
  bad "G181 the repair wrench turns, clockwise, one revolution in 25 engine ticks: the frozen control moved $W_CONTROL pixels. Two shots of the same tick with the animation off must be identical, or 'the picture changed' means nothing"
elif [ "${W_MOVED:-0}" -lt 100 ]; then
  bad "G181 the repair wrench turns, clockwise, one revolution in 25 engine ticks: turning the animation on changed only $W_MOVED pixels against the rest pose at the same tick. The numbers move and the screen does not"
else
  ok "G181 the repair wrench turns, clockwise, one revolution in 25 engine ticks: $W_NSTEP steps of the drawn facing, every one +10 or +11 DirType, 25 distinct facings summing to a full 256, the yaw draw_mesh built matching the facing it was handed on every frame, $W_MOVED pixels moved against a frozen control of $W_CONTROL"
fi

# --- G182: no blink -------------------------------------------------------------------
if [ "$GRC" != "0" ]; then
  bad "G182 the repair wrench never blinks while the engine says the building is repairing: the run failed (exit $GRC)"
elif [ "${W_BLINK:-0}" != "2" ]; then
  bad "G182 the repair wrench never blinks while the engine says the building is repairing: the engine's own IsWrenchVisible took $W_BLINK distinct values across the run, wanted both 0 and 1. With the blink bit stuck this leg cannot fire at all, so a re-added 'o.wrench' test would go unnoticed"
elif [ "${W_COUNTMIN:-0}" != "1" ]; then
  bad "G182 the repair wrench never blinks while the engine says the building is repairing: on at least one frame the collector returned $W_COUNTMIN wrenches while the engine still said repairing=1. That is the 1995 fifteen-tick blink back, and with it a full revolution can never be seen"
else
  ok "G182 the repair wrench never blinks while the engine says the building is repairing: across $W_NDUMP frames the engine's own IsWrenchVisible went both down and up and the wrench was drawn on every single one of them"
fi


# =====================================================================================
# G183 THE CARTRIDGE'S .WLT WAVELET IMAGES STILL DECODE, PIXEL FOR PIXEL.
#
# .WLT is the mission briefing artwork container: 336 distinct names in the cartridge (337
# records, MOEBIUS.WLT twice) in nine frame sizes. The codec is a wavelet library whose
# module name the ROM still carries at RAM 0x801C7F30, and its bitstream is undocumented,
# so tools/bakery/wlt.py does not reimplement it: it runs the cartridge's own decoder
# under the MIPS III interpreter in tools/bakery/n64emu.py. That is exact by construction
# and it is also fragile in one specific way, which is what this gate exists for.
#
# WHY DIGESTS AND NOT "IT DECODED". The interpreter can be wrong and stay quiet. Deleting
# the branch-likely nullification from n64emu.py's BEQL arm leaves all nine smoke files
# decoding at the right size with no error raised and every pixel different; skipping the
# RGB888 -> RGBA5551 fix-up does the same. Both were run: size-only checking passed both,
# and the stored per-file sha256 in wlt.py's REF_SMOKE fails both. So this gate reads
# refok/refbad and not merely "decoded".
#
# THE SMOKE SET IS ONE FILE PER FRAME SIZE, all nine, plus one of the three .WLT that are
# also LZ77-compressed inside the archive, so every shape the cartridge codes and both ways
# a stream reaches the decoder are exercised in about half a minute. The whole-cartridge run is
# `wlt.py verify <rom> -jN` and checks all 336 against one digest of the lot; it takes
# minutes, which is why the gate takes the smoke set and the full run is a deliberate act.
#
# THE SECOND LEG IS THE ADDRESS ARITHMETIC, and it is here because it has already been
# got wrong in prose. Every address in that decoder is a RAM address in a DMA'd overlay,
# so rom = ram - delta and the delta is that overlay's own. `wlt.py offsets` recomputes
# the whole table from the two segment constants, checks every address lands inside a
# segment, and reads the module-name string back out of the ROM at the offset it derived.
# A write-up published one of those functions as ROM 0x1D6960 by subtracting the wrong
# segment's delta; the right answer is 0x1D1960 and this leg is what says so.
#
# NO CARTRIDGE, NO PASS. The suite already needs the cartridge indirectly (every mission
# pack in the run folder was baked from it), so requiring it here adds no new precondition.
G183T="$GATEDIR/../tools/bakery/wlt.py"
G183ROM="$GATEDIR/../data/rom/cnc_eu.z64"
if [ ! -f "$G183T" ]; then
  bad "G183 .WLT wavelet images: $G183T is not there, so nothing was decoded. A gate that cannot run is not a gate that passes"
elif [ ! -f "$G183ROM" ]; then
  bad "G183 .WLT wavelet images: the cartridge is not at $G183ROM, so nothing was decoded. Every other gate's mission packs were baked from that file, so this is not a new requirement, it is a missing one"
else
  G183OFF=$(python3 "$G183T" offsets "$G183ROM" 2>&1); G183ORC=$?
  printf '%s\n' "$G183OFF" >> "$OUT"
  G183OL=$(printf '%s\n' "$G183OFF" | grep '^WLTOFF|')
  G183INSEG=$(printf '%s\n' "$G183OL" | sed -n 's/.*|inseg=\([0-9]*\).*/\1/p')
  G183ADDR=$(printf '%s\n' "$G183OL" | sed -n 's/.*|addrs=\([0-9]*\).*/\1/p')
  G183MOD=$(printf '%s\n' "$G183OL" | sed -n 's/.*|module=\([a-zA-Z]*\).*/\1/p')
  G183NONW=$(printf '%s\n' "$G183OL" | sed -n 's/.*|nonw=\([0-9]*\).*/\1/p')
  G183NAMES=$(printf '%s\n' "$G183OL" | sed -n 's/.*|names=\([0-9]*\).*/\1/p')
  G183OUT=$(python3 "$G183T" verify "$G183ROM" -j4 -s 2>&1); G183RC=$?
  printf '%s\n' "$G183OUT" >> "$OUT"
  G183TOT=$(printf '%s\n' "$G183OUT" | grep '^WLT|total|')
  G183DEC=$(printf '%s\n' "$G183TOT" | sed -n 's/.*|decoded=\([0-9]*\).*/\1/p')
  G183FAIL=$(printf '%s\n' "$G183TOT" | sed -n 's/.*|failed=\([0-9]*\).*/\1/p')
  G183HDR=$(printf '%s\n' "$G183TOT" | sed -n 's/.*|hdrmismatch=\([0-9]*\).*/\1/p')
  G183ROK=$(printf '%s\n' "$G183TOT" | sed -n 's/.*|refok=\([0-9]*\).*/\1/p')
  G183RBAD=$(printf '%s\n' "$G183TOT" | sed -n 's/.*|refbad=\([0-9]*\).*/\1/p')
  G183SZ=$(printf '%s\n' "$G183TOT" | sed -n 's/.*|distinct_sizes=\([0-9]*\).*/\1/p')
  G183WHY=$(printf '%s\n' "$G183OUT" | grep -m1 -E '^WLT\|(FAIL|REFBAD|HDRMISMATCH)\|')
  if [ -z "$G183OL" ]; then
    bad "G183 .WLT wavelet images: the offsets pass printed no summary line (exit $G183ORC), so the address arithmetic was not checked at all. First lines: $(printf '%s\n' "$G183OFF" | head -3 | tr '\n' ' ')"
  elif [ "${G183INSEG:-0}" != "${G183ADDR:-1}" ] || [ "$G183MOD" != "ok" ]; then
    bad "G183 .WLT wavelet images: $G183INSEG of $G183ADDR published addresses land inside a segment and the module-name string read back $G183MOD. An address converted with the wrong overlay's delta points at a plausible ROM offset that is not the function, which is how ROM 0x1D6960 got published for a routine that is at 0x1D1960"
  elif [ "${G183NAMES:-0}" != "336" ] || [ "${G183NONW:-1}" != "0" ]; then
    bad "G183 .WLT wavelet images: the archive yielded $G183NAMES .WLT names (want 336) of which $G183NONW do not begin with the 'W' magic (want 0). A wrong archive data base gives exactly this: 0x470000 instead of dir_start + 24*nrecs = 0x468A90 shifts every stream by 0x7570"
  elif [ -z "$G183TOT" ]; then
    bad "G183 .WLT wavelet images: the decoder printed no total line (exit $G183RC), so every number below would be read off nothing. First lines: $(printf '%s\n' "$G183OUT" | head -3 | tr '\n' ' ')"
  elif [ "${G183FAIL:-1}" != "0" ] || [ "${G183DEC:-0}" != "10" ]; then
    bad "G183 .WLT wavelet images: $G183DEC of the 10 smoke frames decoded and $G183FAIL failed outright. [$G183WHY]"
  elif [ "${G183HDR:-1}" != "0" ]; then
    bad "G183 .WLT wavelet images: $G183HDR frame(s) came out at a size the container header does not declare, so either the header parse or the decode is reading from the wrong place. [$G183WHY]"
  elif [ "${G183RBAD:-1}" != "0" ] || [ "${G183ROK:-0}" != "10" ]; then
    bad "G183 .WLT wavelet images: $G183ROK of 10 frames matched their stored sha256 and $G183RBAD did not. Every frame still decoded at the right size, which is the point: a wrong delay slot in the interpreter, or a skipped RGBA5551 fix-up, produces exactly this and nothing else notices. [$G183WHY]"
  elif [ "${G183SZ:-0}" != "9" ]; then
    bad "G183 .WLT wavelet images: the smoke set covered $G183SZ distinct frame sizes, want 9. One file per size is what makes half a minute of decoding stand for the whole cartridge"
  elif [ "$G183ORC" != "0" ] || [ "$G183RC" != "0" ]; then
    bad "G183 .WLT wavelet images: the tool exited $G183ORC / $G183RC while every counter it printed was clean [$G183OL] [$G183TOT]. A non-zero exit with no named cause is a fault in the tool and is not a pass"
  else
    ok "G183 .WLT wavelet images: all $G183ADDR published addresses resolve through their own overlay's delta and the module name reads back out of the ROM at the offset that arithmetic gives; the archive yields $G183NAMES .WLT streams all carrying the 'W' magic; and running the cartridge's own wavelet decoder under the MIPS interpreter reproduces all 10 smoke frames, one per distinct frame size plus one archive-compressed stream, sha256-identical to the stored reference"
  fi
fi


# ---- G184: the ROM manifest is the parser's own output, at the base it computes -------
# docs/rom-manifest.csv was published by an early build of the archive parser, and that
# build carried a GUESSED data base of 0x470000. The real base is the byte immediately
# after the last directory record, dir_start + 24*nrecs = 0x0460540 + 24*1422 = 0x468A90,
# so every address the published manifest carried was 0x7570 too high on 1538 rows, and
# its 86 trailing-table rows carried no base at all.
# A WRONG BASE DOES NOT LOOK WRONG. The files tile contiguously, so a wrong base lands
# mid-stream in a NEIGHBOURING FILE OF THE SAME TYPE and still emits plausible text and
# plausible pictures; that is how this one survived. Exact length is the only oracle that
# catches it, and it is what the parser's own self-test uses, so leg 1 reads that verdict
# rather than eyeballing any output.
# Leg 2 recomputes the base rule live off the cartridge instead of comparing two literals,
# so the constant cannot drift back to something somebody typed. Leg 3 pins the published
# CSV to a fresh regeneration, so it can never again be a stale snapshot of a superseded
# parser. Leg 4 is deliberately remedy-agnostic about a raw extraction mirror: it goes
# green whether such a tree was deleted or re-cut at the true base, and red only on bytes
# that match the wrong one. Proven by mutation in both directions.
G184=$(python3 "$GATEDIR/gate_rommanifest.py" "$GATEDIR/.." 2>&1); G184RC=$?
G184ST=$(printf '%s\n' "$G184" | sed -n 's/^ROMMANIFEST|selftest=\([A-Z]*\)|.*/\1/p')
G184RULE=$(printf '%s\n' "$G184" | sed -n 's/^ROMMANIFEST|.*|rule=\(0x[0-9A-F]*\)|.*/\1/p')
G184CONST=$(printf '%s\n' "$G184" | sed -n 's/^ROMMANIFEST|.*|const=\(0x[0-9A-F]*\)|.*/\1/p')
G184TRAIL=$(printf '%s\n' "$G184" | sed -n 's/^ROMMANIFEST|.*|trailing=\([A-Z]*\)|.*/\1/p')
G184MAN=$(printf '%s\n' "$G184" | sed -n 's/^ROMMANIFEST|.*|manifest=\([A-Z]*\)|.*/\1/p')
G184ROWS=$(printf '%s\n' "$G184" | sed -n 's/^ROMMANIFEST|.*|rows=\([0-9]*\)|.*/\1/p')
G184STALE=$(printf '%s\n' "$G184" | sed -n 's/^ROMMANIFEST|.*|wrongbase_files=\([0-9]*\)|.*/\1/p')
G184FIRST=$(printf '%s\n' "$G184" | sed -n 's/^ROMMANIFEST|.*|first=\(.*\)$/\1/p')
if [ -z "$G184ST" ]; then
  bad "G184 the ROM manifest is the parser's own output: the gate script printed no verdict line (exit $G184RC). It resolves the repo from the path it is handed, so this is what a run folder detached from a checkout looks like, not a data verdict. It said [$G184]"
elif [ "$G184ST" != "OK" ]; then
  bad "G184 the ROM manifest is the parser's own output: the archive parser's own self-test says $G184ST on the cartridge, so nothing below this line can be believed. That self-test decompresses every record and compares against the length the directory recorded, and it is the ONLY check that can see a wrong data base at all"
elif [ "$G184CONST" != "$G184RULE" ] || [ "$G184CONST" != "0x0468A90" ]; then
  bad "G184 the ROM manifest is the parser's own output: the data base the parser uses is $G184CONST, but the rule recomputed live off this cartridge (dir_start + 24*nrecs) gives $G184RULE and the verified value is 0x0468A90. 0x0470000 here is the early guess coming back, and it reads every file 0x7570 into its neighbour"
elif [ "$G184TRAIL" != "OK" ]; then
  bad "G184 the ROM manifest is the parser's own output: the two trailing archives' bases no longer equal dir_start + 24*nrecs. They are the English and French builds of one 43-file briefing and audio set, and applying the MAIN table's base to them is the second wrong-base bug this project has shipped"
elif [ "$G184MAN" != "MATCHES" ]; then
  bad "G184 the ROM manifest is the parser's own output: docs/rom-manifest.csv is $G184MAN. It has $G184ROWS data rows and does not match a fresh regeneration, so it is a snapshot of some earlier parser rather than a statement about this cartridge. Regenerate it with: python3 tools/romdump/archive.py data/rom/cnc_eu.z64 manifest docs/rom-manifest.csv"
elif [ "${G184STALE:-0}" != "0" ]; then
  bad "G184 the ROM manifest is the parser's own output: $G184STALE extracted files in the tree hold bytes cut at 0x470000, the wrong base, first at $G184FIRST. Those are not the assets their names claim; each one is the middle of a neighbouring file. Either delete the mirror and read through the parser, which computes the base itself, or re-cut it at the true base. This leg passes on either remedy"
else
  ok "G184 the ROM manifest is the parser's own output: the archive parser self-tests OK on the cartridge (every record tiles, every codec decodes to its recorded length), the data base it uses is $G184CONST and the rule recomputed live off the ROM agrees, both trailing archives answer to the same rule, docs/rom-manifest.csv's $G184ROWS rows are byte-identical to a fresh regeneration, and no file anywhere in the tree carries bytes cut at the old 0x470000"
fi

# =====================================================================================
# G150 THE TIER PAGE DID NOT MOVE, THE SMOOTH PAGE FITS, AND NO KNOB IS DRAWN THROUGH
#      ITS OWN LABEL.
#
# THE CONDITION THE DIRECTOR PUT ON THE WHOLE SECOND TOOL: the rung tool's page is the
# one the editor has always had and it may not move by one pixel. The obvious way to add
# a second page -- a tab strip across the top of the panel -- breaks exactly that: it
# pushes every row down by its own height, so the tabs land on the page's own RUNG
# caption at every window size and the brush-size row walks into the action row at the
# short ones.
#
# So the tabs go in the OWNER-CHIP BAND, which ELEVATN does not use: OBJECTS draws house
# chips there, TERRAIN a caption, SCRIPT its RULES/TEAMS switch. eui_elev_top is L.catY
# and stays L.catY, and this gate is the reading of that.
#
# THE FIVE LEGS. delta must be 0.000 on every one of the 48 rows (sixteen window sizes,
# each of the two pages, and the conversion gate's card that the harness walks as a ninth
# pass). The tabs must END above the page caption at eui_elev_top - 12*S, or they
# are drawn over it. Every smooth TOOL and every smooth SLIDER must be reachable at every
# size -- at four of the sixteen there is only 106 design units between the page top and
# the status card, which is why the page is a 28-unit tool row and three 18-unit sliders
# and not three 34-unit tool buttons. Every slider's TRACK must clear its own name on the
# left and its own readout on the right, at all 48 rows (three sliders, sixteen sizes):
# while the track spanned the whole row the knob was drawn straight through the name at
# the page's own defaults, MEASURED at 1600x1000 backing 1.0 as knob x=1285.5 against
# glyphs running to x=1311.3. And --uitest itself must be clean, because the rectangles
# above mean nothing if the hit test does not agree with them.
#
# THE ORDER OF THE ARMS IS PART OF THE GATE. --uitest exits 1 when it REPORTS failures,
# so a non-zero exit is a result and not a crash: an arm that reads $GRC first blames
# "failed to run, so nothing measured here means anything" for a page that in fact ran,
# measured, and moved. Both of the mutants this gate was built against tripped an
# unrelated hit-test probe as well, and both were reported under that wrong cause. So
# "did not run" is now the absence of the TOTAL line, and the measured legs are read
# before the failure count.
# =====================================================================================
gbegin /tmp/g150.log
grun /tmp/g150.log --uitest
G150UI=$(sed -n 's/^UITEST|TOTAL|\([0-9]*\) failures/\1/p' /tmp/g150.log | tail -1)
G150ROWS=$(grep -ac '^ELEVPAGE|' /tmp/g150.log)
G150BAD=$(grep -a '^ELEVPAGE|' /tmp/g150.log | grep -avc '|delta=0.000|')
G150CAP=$(grep -a '^ELEVPAGE|' /tmp/g150.log | awk -F'|' '
    { for (i=1;i<=NF;i++) { split($i,kv,"="); v[kv[1]]=kv[2] }
      if (v["tabbot"] + 0.0005 > v["caption"]) n++ } END { print n+0 }')
G150SP=$(grep -ac '^SBRPAGE|' /tmp/g150.log)
G150FIT=$(grep -a '^SBRPAGE|' /tmp/g150.log | grep -avc 'toolfit=1|slidfit=1')
G150SL=$(grep -ac '^SBRSLID|' /tmp/g150.log)
G150OVER=$(grep -a '^SBRSLID|' /tmp/g150.log | awk -F'|' '
    { for (i=1;i<=NF;i++) { split($i,kv,"="); v[kv[1]]=kv[2] }
      if (v["leftclear"] + 0 < 0 || v["rightclear"] + 0 < 0) n++ } END { print n+0 }')
G150TRACK=$(grep -a '^SBRSLID|' /tmp/g150.log | awk -F'|' '
    { for (i=1;i<=NF;i++) { split($i,kv,"="); v[kv[1]]=kv[2] }
      if (n++ == 0 || v["trackw"] + 0 < m) m = v["trackw"] + 0 } END { printf "%.1f", m }')
if [ -z "$G150UI" ]; then
  bad "G150 the tier page did not move: --uitest never reached its own TOTAL line (exit $GRC), so it did not run and nothing measured here means anything"
elif [ "${G150ROWS:-0}" != "48" ] || [ "${G150SP:-0}" != "16" ]; then
  bad "G150 the tier page did not move: the walk did not happen. $G150ROWS ELEVPAGE rows (want 48: sixteen window sizes on each of the two pages and on the conversion gate's card) and $G150SP SBRPAGE rows (want 16). Every count below is a count of nothing when this is wrong"
elif [ "${G150BAD:-1}" != "0" ]; then
  bad "G150 the tier page did not move: $G150BAD of $G150ROWS rows report eui_elev_top away from L.catY. The rung tool's page starts at catY and every row it draws is measured from there, so a non-zero delta is the whole page shifted -- first line: $(grep -a '^ELEVPAGE|' /tmp/g150.log | grep -av '|delta=0.000|' | head -1)"
elif [ "${G150CAP:-1}" != "0" ]; then
  bad "G150 the tier page did not move: at $G150CAP of $G150ROWS sizes the page tabs end BELOW the page's own caption at eui_elev_top - 12*S, so the tab strip is drawn over the caption. Text is drawn from its top, so the clearance has to be measured against the caption's y and not against catY"
elif [ "${G150FIT:-1}" != "0" ]; then
  bad "G150 the tier page did not move: at $G150FIT of $G150SP sizes a smooth TOOL or SLIDER does not fit above the status card and is therefore unreachable -- first line: $(grep -a '^SBRPAGE|' /tmp/g150.log | grep -av 'toolfit=1|slidfit=1' | head -1)"
elif [ "${G150SL:-0}" != "48" ]; then
  bad "G150 the tier page did not move: $G150SL slider rows were measured and there are 48 to measure (three sliders at sixteen sizes). A row is emitted only for a slider that FITS, so a shortfall here is a slider that has walked off the panel at some size and the fit leg above should have said so first -- that it did not is a second defect"
elif [ "${G150OVER:-1}" != "0" ]; then
  bad "G150 the tier page did not move: $G150OVER of $G150SL slider rows draw their track into their own text. The row carries a NAME on the left and a READOUT on the right and the knob has to live between them -- first line: $(grep -a '^SBRSLID|' /tmp/g150.log | awk -F'|' '{ for (i=1;i<=NF;i++) { split($i,kv,"="); v[kv[1]]=kv[2] } if (v["leftclear"]+0 < 0 || v["rightclear"]+0 < 0) { print; exit } }')"
elif [ "${G150UI:-1}" = "0" ]; then
  ok "G150 the tier page did not move: eui_elev_top is L.catY on all 48 rows (16 window sizes x 2 pages, plus the conversion gate's card as a ninth pass), the tabs clear the page caption at every one, all three smooth tools and all three sliders are reachable at all 16, every one of the 48 slider rows keeps its track clear of its own name and readout (narrowest track $G150TRACK px), and --uitest reports 0 failures"
else
  bad "G150 the tier page did not move: --uitest reports $G150UI failures. It RAN, and the five measured legs above are green, so the disagreement is between the draw and the hit test somewhere else on the panel -- first failure: $(grep -a '^UITEST|FAIL|' /tmp/g150.log | head -1)"
fi

# =====================================================================================
# G151 A SMOOTH STROKE IS A FUNCTION OF HOW LONG THE BUTTON WAS HELD, NOT OF THE FRAME RATE.
#
# THE DEFECT THIS REFUSES. An erase written as an exponential approach whose per-step
# factor is LINEAR in dt does not compose: N small steps do not add up to one big step,
# so the ground a stroke leaves behind depends on how many frames were drawn while the
# button was down. The same drag then gives one result on a fast machine and another on
# a slow one, and neither is reproducible.
#
# WHAT REPLACES IT. Every corner accumulates a DOSE -- weight x time x strength -- and
# its byte is a pure function of the byte it had when the stroke began and that dose.
# The integration runs at a fixed 120 Hz whose tick count is rounded from the TOTAL time
# held, so the same hold at any frame size applies the same ticks in the same order.
#
# THE 250 ms FRAME IS THE ONE THAT MATTERS, and its absence is why the first version of
# this gate was blind. The frame loop clamps its dt so that a stall cannot teleport the
# camera, and it used to hand the brush that same clamped dt: a frame longer than the
# ceiling contributed the ceiling and not the time it took. Every frame size this gate
# used was under the ceiling, so the clamp was invisible to it -- and the verb it drove
# called the brush's pump DIRECTLY, below the clamp, so it was measuring a path no player
# takes twice over. The verb now drives sbr_frame, which is the loop's own door, and one
# of the frame sizes is past any stall ceiling.
#
# THE CONTROL LEG IS NOT OPTIONAL. A brush that wrote nothing at all would give four
# identical digests too, so a DIFFERENT hold has to give a DIFFERENT digest, and the
# stroke has to report that it moved something.
# =====================================================================================
gbegin /tmp/g151.log
cat > /tmp/g151.script <<'EOF'
sbrstate
sbrset 1 0 3.0 4.0 0.60
sbrstroke 30 30 1000 25
editundo 1
sbrstroke 30 30 1000 5
editundo 1
sbrstroke 30 30 1000 7
editundo 1
sbrstroke 30 30 1000 250
editundo 1
sbrset 1 2 3.0 4.0 0.60
sbrstroke 30 30 1000 25
editundo 1
sbrstroke 30 30 1000 5
editundo 1
sbrstroke 30 30 1000 7
editundo 1
sbrstroke 30 30 1000 250
editundo 1
sbrset 1 0 3.0 4.0 0.60
sbrstroke 30 30 500 25
quit
EOF
grun /tmp/g151.log --edit --editmode 2 $BASE --nosound --w 1400 --h 900 \
     --scen SCG01EA --pack SCG01EA.pack --script /tmp/g151.script
grep -a '^SBRSTROKE|' /tmp/g151.log | sed -n 's/.*|corners=\([0-9a-f]*\).*/\1/p' > /tmp/g151_strokes.txt
grep -a '^SBRSTROKE|' /tmp/g151.log | sed -n 's/.*|ticks=\([0-9]*\)|.*/\1/p' > /tmp/g151_ticks.txt
g151_dig() { sed -n "$1p" /tmp/g151_strokes.txt; }
G151N=$(wc -l < /tmp/g151_strokes.txt | tr -d ' ')
G151MOVED=$(grep -ac '|moved=1|' /tmp/g151.log)
G151RA=$(g151_dig 1); G151RB=$(g151_dig 2); G151RC=$(g151_dig 3); G151RD=$(g151_dig 4)
G151EA=$(g151_dig 5); G151EB=$(g151_dig 6); G151EC=$(g151_dig 7); G151ED=$(g151_dig 8)
G151HALF=$(g151_dig 9)
# THE EIGHT ONE-SECOND STROKES ONLY. The ninth is the half-second control and is
# SUPPOSED to differ, so folding it in here would make this leg permanently red.
G151TICK=$(head -8 /tmp/g151_ticks.txt | sort -u | tr '\n' ' ')
G151TU=$(head -8 /tmp/g151_ticks.txt | sort -u | wc -l | tr -d ' ')
if [ "$GRC" != "0" ]; then
  bad "G151 a smooth stroke is framerate independent: the run failed (exit $GRC)"
elif [ "${G151N:-0}" != "9" ] || [ "${G151MOVED:-0}" != "9" ]; then
  bad "G151 a smooth stroke is framerate independent: $G151N strokes ran (want 9) and $G151MOVED of them moved a byte (want 9). Four identical digests from a brush that writes nothing is not a proof of anything, which is why this leg comes first"
elif [ "$G151RA" != "$G151RB" ] || [ "$G151RA" != "$G151RC" ] || [ "$G151RA" != "$G151RD" ]; then
  bad "G151 a smooth stroke is framerate independent: RAISE held for one second gave $G151RA at 25 ms frames, $G151RB at 5 ms, $G151RC at 7 ms and $G151RD at 250 ms. The same drag has to write the same ground whatever the frame rate. A 250 ms frame that disagrees with the rest is the loop's stall clamp reaching the brush: it contributes the ceiling instead of the time the frame really took"
elif [ "$G151EA" != "$G151EB" ] || [ "$G151EA" != "$G151EC" ] || [ "$G151EA" != "$G151ED" ]; then
  bad "G151 a smooth stroke is framerate independent: ERASE held for one second gave $G151EA at 25 ms frames, $G151EB at 5 ms, $G151EC at 7 ms and $G151ED at 250 ms. This is the arm the defect was in: an exponential whose per-step factor is linear in dt does not compose, so N small steps land somewhere else than one big one"
elif [ "${G151TU:-9}" != "1" ]; then
  bad "G151 a smooth stroke is framerate independent: the eight one-second strokes applied different numbers of ticks ($G151TICK). The tick count is meant to come from the time held and nothing else"
elif [ "$G151HALF" = "$G151RA" ]; then
  bad "G151 a smooth stroke is framerate independent: half a second of the same stroke gave the same digest as a whole second ($G151HALF). Either the brush stops after one tick or the digest is not reading the ground, and either way the three legs above proved nothing"
else
  ok "G151 a smooth stroke is framerate independent: one second of RAISE gives $G151RA and one second of ERASE gives $G151EA at 25, 5, 7 and 250 ms frames alike, each applying the same $(head -1 /tmp/g151_ticks.txt) ticks; half a second gives a different ground ($G151HALF), so the comparison is measuring something"
fi

# =====================================================================================
# G152 A CLICK THAT MOVES NOTHING CHANGES NOTHING, AND UNDO TAKES THE UNLOCK BACK.
#
# THE DIRECTOR'S RULE: the two tools share a map, and the first smooth stroke unlocks the
# rung tool on it -- a map carrying hand-made smooth ground is legitimate and the rung
# tool paints on it normally, never asking to CONVERT.
#
# THE DEFECT THIS REFUSES. The unlock was set when the stroke BEGAN, before the stroke
# was known to have moved anything, and the stroke's end skipped the commit when nothing
# moved. So a click on ground the brush refuses -- water -- permanently changed what the
# map was, with NO UNDO ENTRY to put it back. The unlock now happens only when a byte
# actually moved, and it lives inside the undo entry.
#
# THE WATER CELL IS FOUND, NOT WRITTEN DOWN. The gate sweeps the map it opens and takes
# the first cell with three clear cells of water in every direction, so it is sufficient
# from what the run folder actually holds and does not depend on a cell number in a
# comment staying true.
# =====================================================================================
gbegin /tmp/g152_sweep.log /tmp/g152.log
{ awk 'BEGIN{for(y=0;y<64;y++)for(x=0;x<64;x++)printf "height %d %d\n",x,y}'
  echo quit; } > /tmp/g152_sweep.script
./cnc_eyes --edit --editmode 2 $BASE --nosound --w 1400 --h 900 \
    --scen SCG01EA --pack SCG01EA.pack --script /tmp/g152_sweep.script \
    > /tmp/g152_sweep.log 2>&1
ge_rc=$?; [ "$ge_rc" = "0" ] || GRC="$ge_rc"
G152SEEN=$(grep -ac '^HEIGHT|' /tmp/g152_sweep.log)
G152WET=$(awk -F'|' 'NR==FNR { if ($0 ~ /^HEIGHT\|/) { split($2,a,","); split($8,b,"="); L[a[1]","a[2]]=b[2] } next }
    /^HEIGHT\|/ && !found { split($2,a,","); x=a[1]+0; y=a[2]+0; ok=1;
      for (dy=-3; dy<=3 && ok; dy++) for (dx=-3; dx<=3 && ok; dx++)
        if (L[(x+dx)","(y+dy)] != "WATER") ok=0;
      if (ok) { print x, y; found=1 } }' \
    /tmp/g152_sweep.log /tmp/g152_sweep.log)
if [ -z "$G152WET" ]; then G152WET="-1 -1"; fi
{ echo "sbrstate"
  echo "elevpaint 20 20 0 2 1"
  echo "sbrset 1 0 1.0 1.5 1.00"
  echo "sbrstroke $G152WET 800 16"
  echo "editundo 1"
  echo "sbrset 1 0 3.0 3.0 1.00"
  echo "sbrstroke 40 40 800 16"
  echo "elevpaint 20 20 0 2 1"
  echo "editundo 1"
  echo "sbrstate"
  echo quit
} > /tmp/g152.script
grun /tmp/g152.log --edit --editmode 2 $BASE --nosound --w 1400 --h 900 \
     --scen SCG01EA --pack SCG01EA.pack --script /tmp/g152.script
G152OPEN=$(grep -a '^SBRSTATE|' /tmp/g152.log | head -1)
G152SHUT=$(grep -a '^SBRSTATE|' /tmp/g152.log | tail -1)
G152D0=$(echo "$G152OPEN" | sed -n 's/.*|corners=\([0-9a-f]*\).*/\1/p')
G152D1=$(echo "$G152SHUT" | sed -n 's/.*|corners=\([0-9a-f]*\).*/\1/p')
G152R0=$(echo "$G152OPEN" | sed -n 's/.*|ready=\([0-9]\).*/\1/p')
G152R1=$(echo "$G152SHUT" | sed -n 's/.*|ready=\([0-9]\).*/\1/p')
G152DRY=$(grep -a '^SBRSTROKE|' /tmp/g152.log | head -1)
G152WM=$(echo "$G152DRY" | sed -n 's/.*|moved=\([0-9]\).*/\1/p')
G152WW=$(echo "$G152DRY" | sed -n 's/.*|wet=\([0-9]*\/[0-9]*\).*/\1/p')
G152WU=$(echo "$G152DRY" | sed -n 's/.*|unlock=\([0-9]\).*/\1/p')
G152WD=$(echo "$G152DRY" | sed -n 's/.*|corners=\([0-9a-f]*\).*/\1/p')
G152P0=$(grep -a '^ELEVPAINT|' /tmp/g152.log | head -1 | sed -n 's/.*|wrote=\([0-9]*\).*/\1/p')
G152P1=$(grep -a '^ELEVPAINT|' /tmp/g152.log | tail -1 | sed -n 's/.*|wrote=\([0-9]*\).*/\1/p')
G152NOU=$(grep -ac '^edit: nothing to undo' /tmp/g152.log)
if [ "$GRC" != "0" ]; then
  bad "G152 a click that moves nothing changes nothing: a run failed (exit $GRC)"
elif [ "${G152SEEN:-0}" != "4096" ] || [ "$G152WET" = "-1 -1" ]; then
  bad "G152 a click that moves nothing changes nothing: the map sweep found $G152SEEN cells (want 4096) and picked '$G152WET' as its patch of open water. Without a cell whose whole brush footprint is water there is nothing here that can be refused, and the legs below would be testing a stroke that simply worked"
elif [ "${G152P0:-1}" != "0" ]; then
  bad "G152 a click that moves nothing changes nothing: the rung tool wrote $G152P0 cells on the map as opened (want 0). This map is meant to be OFF the ladder so that the unlock has something to unlock; a map that is already on it makes every leg below vacuous"
elif [ "${G152WM:-1}" != "0" ] || [ "${G152WU:-1}" != "0" ]; then
  bad "G152 a click that moves nothing changes nothing: the stroke over open water at $G152WET reported moved=$G152WM unlock=$G152WU (want 0 and 0), wet=$G152WW. A stroke the brush refuses must not convert the map behind the click"
elif [ "$G152WD" != "$G152D0" ]; then
  bad "G152 a click that moves nothing changes nothing: the map's corners went from $G152D0 to $G152WD across a stroke that reported moving nothing"
elif [ "${G152NOU:-0}" -lt 1 ]; then
  bad "G152 a click that moves nothing changes nothing: the undo after the refused stroke found something to undo, so the refused click filed an undo entry of its own. Nothing happened, so nothing may be on the stack"
elif [ "${G152P1:-0}" -lt 1 ]; then
  bad "G152 a click that moves nothing changes nothing: after a real smooth stroke the rung tool still wrote $G152P1 cells (want more than 0). The two tools share a map: the first smooth stroke unlocks the rung tool and it must then paint without asking to CONVERT"
elif [ "$G152D1" = "$G152D0" ] && [ "${G152R0:-9}" = "0" ] && [ "${G152R1:-9}" = "0" ]; then
  ok "G152 a click that moves nothing changes nothing: a stroke over open water at $G152WET moved no byte ($G152WW of its footprint was water), unlocked nothing and filed no undo step; a real stroke unlocked the rung tool, which then painted $G152P1 cells with no CONVERT; and one undo put both the heights ($G152D0) and the unlock back"
else
  bad "G152 a click that moves nothing changes nothing: after the undo the corners are $G152D1 against $G152D0 as opened, and ready went $G152R0 -> $G152R1 (want the same digest and 0 -> 0). The unlock has to live INSIDE the undo entry, or undo puts the ground back and leaves the map claiming to be something it is not"
fi

# =====================================================================================
# G153 THE BRUSH IS SAVED, IS LIT, AND THE GROUND UNDER A BUILDING REALLY MOVES.
#
# FOUR THINGS THAT ARE EACH INVISIBLE ON THEIR OWN AND TOGETHER ARE THE WHOLE TOOL.
#
#  the light   The shading is this brush's entire visual read. terrain_shade_calc is
#              built once and cached behind g_shadeReady, so a stroke that does not
#              clear it moves the ground and leaves the picture: measured at exactly
#              0.6275, the flat value, on corners whose neighbours had just moved.
#  the pad     Every height query goes through corner_raw, which prefers the BUILDING
#              PAD to the map's own corner. Raise the ground under a turret without
#              rebuilding the pad and the corner says 175 while the game goes on
#              standing things at 78.
#  the save    edit_write_hgt returns early unless the elevation has been touched, so a
#              save after a stroke that never set the flag rewrote the file it already
#              had and said nothing.
#  the size    The .HGT is (gridW+1)^2 -- 4225 on a 64 map and 16641 on a 128 one. A
#              fixed 4225 is rejected on the big grid and the map comes back flat.
#
# ERASE LANDS ON THE MAP'S OWN GROUND DATUM, which G154 is entirely about; here it is
# read once, as the raw byte, so that this gate's own long erase is measured against the
# right number. The datum is taken from the run itself (SBRSTATE flat=) rather than
# written down: on SCG01EA it is 90, and 90 is not even on the five-rung ladder.
# =====================================================================================
gbegin /tmp/g153.log /tmp/g153b.log
# THE LIGHT IS PROBED ON THE FALLOFF, NOT AT THE CENTRE. A corner in the middle of a
# uniformly raised patch has the same normal it started with, so its shade is unchanged
# and a leg aimed there would be permanently red for the right reason and the wrong
# one. 44,40 is four cells out from a stroke centred at 40,40 with a reach of six.
cat > /tmp/g153.script <<'EOF'
sbrstate
sbrshade 44 40
sbrset 1 0 3.0 3.0 1.00
sbrstroke 40 40 1000 16
sbrshade 44 40
editundo 1
sbrshade 44 40
sbrraw 49 55
sbrstroke 49 55 1000 16
sbrraw 49 55
sbrset 1 2 4.0 2.0 1.00
sbrstroke 40 40 4000 16
sbrraw 40 40
sbrraw 39 39
sbrraw 44 40
editscen USERG153
editsave
quit
EOF
grun /tmp/g153.log --edit --editmode 2 $BASE --nosound --w 1400 --h 900 \
     --scen SCG01EA --pack SCG01EA.pack --script /tmp/g153.script
cat > /tmp/g153b.script <<'EOF'
newmap 4 0 0
sbrset 1 0 4.0 3.0 1.00
sbrstroke 60 60 1000 16
editscen USERG15B
editsave
quit
EOF
grun /tmp/g153b.log --edit --editmode 2 $BASE --nosound --w 1400 --h 900 \
     --scen SCG01EA --pack SCG01EA.pack --script /tmp/g153b.script
G153FLATB=$(grep -a '^SBRSTATE|' /tmp/g153.log | head -1 | sed -n 's/.*|flat=\([0-9]*\).*/\1/p')
# AND THE SAME NUMBER OUT OF THE PACK LOADER, because a leg that takes its expected
# value from the thing it is testing is a leg that cannot fail. The brush's idea of the
# datum has to agree with the level the world was actually re-zeroed on.
G153FLATG=$(sed -n 's/^pack: heightmap [0-9]*\.\.[0-9]*, ground level \([0-9]*\) .*/\1/p' \
            /tmp/g153.log | head -1)
G153L0=$(grep -a '^SBRSHADE|' /tmp/g153.log | sed -n '1s/.*|lit=\([0-9.]*\).*/\1/p')
G153L1=$(grep -a '^SBRSHADE|' /tmp/g153.log | sed -n '2s/.*|lit=\([0-9.]*\).*/\1/p')
G153L2=$(grep -a '^SBRSHADE|' /tmp/g153.log | sed -n '3s/.*|lit=\([0-9.]*\).*/\1/p')
G153PADON=$(grep -a '^SBRRAW|49,55|' /tmp/g153.log | sed -n '1s/.*|padon=\([0-9]\).*/\1/p')
G153R0=$(grep -a '^SBRRAW|49,55|' /tmp/g153.log | sed -n '1s/.*|raw=\([0-9-]*\)|.*/\1/p')
G153S0=$(grep -a '^SBRRAW|49,55|' /tmp/g153.log | sed -n '1s/.*|seen=\([0-9-]*\).*/\1/p')
G153R1=$(grep -a '^SBRRAW|49,55|' /tmp/g153.log | sed -n '2s/.*|raw=\([0-9-]*\)|.*/\1/p')
G153S1=$(grep -a '^SBRRAW|49,55|' /tmp/g153.log | sed -n '2s/.*|seen=\([0-9-]*\).*/\1/p')
G153FLATN=$(grep -ac '^SBRRAW|4[04],40|\|^SBRRAW|39,39|' /tmp/g153.log)
G153FLAT=$(grep -a '^SBRRAW|4[04],40|\|^SBRRAW|39,39|' /tmp/g153.log | grep -avc "|raw=${G153FLATB:-x}|")
G153H64=$(wc -c < missions/user_maps/USERG153.HGT 2>/dev/null | tr -d ' ')
G153H128=$(wc -c < missions/user_maps/USERG15B.HGT 2>/dev/null | tr -d ' ')
if [ "$GRC" != "0" ]; then
  bad "G153 the brush is saved, is lit and moves the ground under a building: a run failed (exit $GRC)"
elif [ -z "$G153FLATB" ] || [ -z "$G153FLATG" ]; then
  bad "G153 the brush is saved, is lit and moves the ground under a building: the run reported the brush's datum as '$G153FLATB' and the pack's ground level as '$G153FLATG', and the ERASE leg below needs both to have a number to be measured against"
elif [ "$G153FLATB" != "$G153FLATG" ]; then
  bad "G153 the brush is saved, is lit and moves the ground under a building: the brush thinks the ground datum is the byte $G153FLATB while the pack was re-zeroed on $G153FLATG. G154 is the gate for that question; this one only refuses to measure its own ERASE leg against a number the brush made up"
elif [ "${G153PADON:-0}" != "1" ] || [ "${G153R0:-0}" != "${G153S0:-1}" ]; then
  bad "G153 the brush is saved, is lit and moves the ground under a building: corner 49,55 reports padon=$G153PADON raw=$G153R0 seen=$G153S0. This leg needs a corner that a BUILDING PAD actually stands on -- the whole point is that the height query prefers the pad -- and on a corner with no pad the leg below passes without testing anything"
elif [ "${G153R1:-0}" = "${G153R0:-1}" ]; then
  bad "G153 the brush is saved, is lit and moves the ground under a building: the raw corner under the turret is still $G153R1 after the stroke, so the brush did not write there and nothing below means anything"
elif [ "${G153S1:-0}" != "${G153R1:-1}" ]; then
  bad "G153 the brush is saved, is lit and moves the ground under a building: the raw corner moved to $G153R1 and every height query still answers $G153S1, because the building pad was not rebuilt. corner_raw prefers the pad, so the stroke is written and then ignored"
elif [ "$G153L0" = "$G153L1" ]; then
  bad "G153 the brush is saved, is lit and moves the ground under a building: the baked light at 44,40 is $G153L1 before and after a stroke that moved the ground beside it. terrain_shade_calc is cached behind g_shadeReady and the stroke has to clear it, or the ground moves and the picture does not"
elif [ "$G153L2" != "$G153L0" ]; then
  bad "G153 the brush is saved, is lit and moves the ground under a building: undoing the stroke left the light at $G153L2 against $G153L0 before it. Putting different heights back is exactly when the baked light has to be rebuilt"
elif [ "${G153FLATN:-0}" != "3" ] || [ "${G153FLAT:-1}" != "0" ]; then
  bad "G153 the brush is saved, is lit and moves the ground under a building: $G153FLAT of $G153FLATN corners under a long ERASE are not this map's own ground datum, the raw byte $G153FLATB (want 3 corners, all $G153FLATB). ERASE eases the ground back to the level the pack was re-zeroed on, which on this map is not the byte 64 and is not on the ladder either"
elif [ "${G153H64:-0}" = "4225" ] && [ "${G153H128:-0}" = "16641" ]; then
  ok "G153 the brush is saved, is lit and moves the ground under a building: the corner under a turret went $G153R0 -> $G153R1 and the height query followed it ($G153S1, so the pad was rebuilt); the light at 44,40 went $G153L0 -> $G153L1 and back to $G153L2 on undo; a long ERASE lands on this map's own ground datum, the raw byte $G153FLATB, at all $G153FLATN corners read; and the save writes 4225 bytes on a 64 map and 16641 on a 128 one"
else
  bad "G153 the brush is saved, is lit and moves the ground under a building: the .HGT is $G153H64 bytes on a 64 map (want 4225) and $G153H128 on a 128 one (want 16641). A missing file means the stroke never set the touched flag and edit_write_hgt returned early; a wrong SIZE means a fixed 4225 was written for a grid that is not 64, which is rejected on load and the map comes back flat"
fi

# =====================================================================================
# G154 ERASE PAINTS THE MAP'S OWN GROUND, ON THREE SHIPPED MAPS WITH THREE DIFFERENT
#      GROUND LEVELS.
#
# THE DIRECTOR'S SPEC, in one sentence: the brush has "an Erase option, which basically
# paints the DEFAULT TERRAIN HEIGHT". The default is a property of the MAP.
#
# THE DEFECT. ERASE drove every corner to the fixed byte 64, on the reasoning that 64 is
# world y 0.000. It is world y 0.000 on a map the editor MADE. On a map off the
# cartridge, load_pack re-zeroes the world on the median corner byte of the playable rect
# and keeps that level in g_terrainBase, and the shipped packs are nowhere near 64:
# SCG01EA 90, SCB01EA 108, SCG09EA 128. So ERASE did not erase, it EXCAVATED. Measured
# on SCG09EA at cell 32,7 -- flat, dry, and sitting exactly on that map's own ground -- a
# six second full-strength ERASE took the height query from 0.0000 to -1.0000: a
# cell-deep pit dug in ground the stroke was asked to leave alone.
#
# THE THREE MAPS ARE THE POINT. One map cannot tell a datum from a constant. Three maps
# whose datums are 90, 108 and 128 can, and this gate reads all three from the run rather
# than writing them down.
#
# THE CELL IS FOUND, NOT WRITTEN DOWN, in the manner G152 finds its water: each map is
# swept first, and the gate takes the driest cell whose sixteen core corners hold the
# most corners that are ALREADY on that map's datum. That is what makes the last leg
# possible -- ground nobody has touched, which ERASE must leave exactly where it is.
#
# THE FOUR LEGS, per map.
#   the datum   SBRSTATE's flat= must equal the ground level the pack loader printed, and
#               must not be 64 (on all three of these it is not).
#   the paint   after one long full-strength ERASE, all sixteen core corners must read
#               exactly that byte -- not 64, and not a rung either, because the ladder is
#               0/64/128/191/255 and 90 is not on it.
#   the level   and the height query at the centre cell must read 0.0000, which is the
#               same statement in the units the player sees.
#   the no-op   a SECOND identical ERASE must leave those sixteen corners byte for byte
#               where the first one left them. Ground already on the datum does not move.
# =====================================================================================
gbegin /tmp/g154.log
G154FAIL=""
G154SAY=""
for g154m in SCG01EA SCB01EA SCG09EA; do
    [ -n "$G154FAIL" ] && break
    { echo sbrstate
      awk 'BEGIN{for(y=0;y<=64;y++)for(x=0;x<=64;x++)printf "sbrraw %d %d\n",x,y}'
      awk 'BEGIN{for(y=0;y<64;y++)for(x=0;x<64;x++)printf "height %d %d\n",x,y}'
      echo quit; } > /tmp/g154_sweep.script
    grun "/tmp/g154_sweep_$g154m.log" --edit --editmode 2 $BASE --nosound \
         --w 1400 --h 900 --scen "$g154m" --pack "$g154m.pack" \
         --script /tmp/g154_sweep.script
    g154D=$(grep -a '^SBRSTATE|' "/tmp/g154_sweep_$g154m.log" | head -1 |
            sed -n 's/.*|flat=\([0-9]*\).*/\1/p')
    g154G=$(sed -n 's/^pack: heightmap [0-9]*\.\.[0-9]*, ground level \([0-9]*\) .*/\1/p' \
            "/tmp/g154_sweep_$g154m.log" | head -1)
    g154RAW=$(grep -ac '^SBRRAW|' "/tmp/g154_sweep_$g154m.log")
    if [ "${g154RAW:-0}" != "4225" ] || [ -z "$g154D" ] || [ -z "$g154G" ]; then
        G154FAIL="the sweep of $g154m read $g154RAW corners (want 4225), datum '$g154D' and ground level '$g154G'. Nothing below this can mean anything"
        continue
    fi
    if [ "$g154D" != "$g154G" ]; then
        G154FAIL="on $g154m the brush thinks the ground datum is the byte $g154D while the pack was re-zeroed on $g154G. ERASE has to land on the level the world was re-zeroed on, or it paints a height the map does not call ground"
        continue
    fi
    if [ "$g154D" = "64" ]; then
        G154FAIL="on $g154m the datum reads 64, which is the constant the defect used. This gate needs three maps whose ground is NOT 64 or it cannot tell a datum from a constant"
        continue
    fi
    # The driest cell whose sixteen core corners hold the most corners already on the
    # datum. Both facts come out of the sweep this run just made.
    set -- $(awk -v G="$g154D" -F'|' '
        /^SBRRAW\|/ { split($2,a,","); split($3,b,"="); R[a[1]","a[2]]=b[2]+0; next }
        /^HEIGHT\|/ { split($2,a,","); n=split($0,f,"|"); L[a[1]","a[2]]=f[n]; next }
        END {
          best = -1
          for (y = 5; y < 59; y++) for (x = 5; x < 59; x++) {
            dry = 1
            for (dy = -4; dy <= 4 && dry; dy++) for (dx = -4; dx <= 4 && dry; dx++)
              if (L[(x+dx)","(y+dy)] == "land=WATER") dry = 0
            if (!dry) continue
            n = 0
            for (dy = -1; dy <= 2; dy++) for (dx = -1; dx <= 2; dx++)
              if (R[(x+dx)","(y+dy)] == G) n++
            if (n > best) { best = n; bx = x; by = y }
          }
          print bx+0, by+0, best+0
        }' "/tmp/g154_sweep_$g154m.log")
    g154CX="$1"; g154CY="$2"; g154AT="$3"
    if [ "${g154AT:-0}" -lt 1 ]; then
        G154FAIL="the sweep of $g154m found no dry cell with a single corner already on that map's ground (best was ${g154AT:-none} at $g154CX,$g154CY). The last leg needs untouched ground to leave alone"
        continue
    fi
    { echo sbrstate
      awk -v x="$g154CX" -v y="$g154CY" \
          'BEGIN{for(dy=-1;dy<=2;dy++)for(dx=-1;dx<=2;dx++)printf "sbrraw %d %d\n",x+dx,y+dy}'
      # THE REACH IS 3.0 CELLS, AND THAT IS WHY THE DRYNESS SEARCH ABOVE IS 4.
      # A brush whose falloff runs further than the search does will find water it was
      # never told about and refuse the corners this gate is about to read. The CORE is
      # 2.5, which still covers all sixteen probed corners: the furthest of them is
      # sqrt(1.5^2 + 1.5^2) = 2.121 cells from the centre of the cell under the pointer.
      echo "sbrset 1 2 2.5 0.5 1.00"
      echo "sbrstroke $g154CX $g154CY 6000 16"
      echo "height $g154CX $g154CY"
      awk -v x="$g154CX" -v y="$g154CY" \
          'BEGIN{for(dy=-1;dy<=2;dy++)for(dx=-1;dx<=2;dx++)printf "sbrraw %d %d\n",x+dx,y+dy}'
      echo "sbrstroke $g154CX $g154CY 6000 16"
      awk -v x="$g154CX" -v y="$g154CY" \
          'BEGIN{for(dy=-1;dy<=2;dy++)for(dx=-1;dx<=2;dx++)printf "sbrraw %d %d\n",x+dx,y+dy}'
      echo quit; } > /tmp/g154.script
    grun "/tmp/g154_$g154m.log" --edit --editmode 2 $BASE --nosound --w 1400 --h 900 \
         --scen "$g154m" --pack "$g154m.pack" --script /tmp/g154.script
    grep -a '^SBRRAW|' "/tmp/g154_$g154m.log" | sed -n 's/.*|raw=\([0-9-]*\)|.*/\1/p' \
        > /tmp/g154_raw.txt
    g154N=$(wc -l < /tmp/g154_raw.txt | tr -d ' ')
    if [ "${g154N:-0}" != "48" ]; then
        G154FAIL="the erase run on $g154m read $g154N corners (want 48: sixteen core corners before, after one ERASE and after a second)"
        continue
    fi
    g154WET=$(grep -a '^SBRSTROKE|' "/tmp/g154_$g154m.log" | head -1 |
              sed -n 's/.*|wet=\([0-9]*\)\/.*/\1/p')
    if [ "${g154WET:-1}" != "0" ]; then
        G154FAIL="the ERASE on $g154m at $g154CX,$g154CY found $g154WET of its footprint under water. The brush refuses water, so the corners below would be corners it never touched"
        continue
    fi
    g154BAD=$(sed -n '17,32p' /tmp/g154_raw.txt | grep -vc "^${g154D}\$")
    if [ "${g154BAD:-16}" != "0" ]; then
        G154FAIL="on $g154m, $g154BAD of the 16 core corners are not the byte $g154D after a six second full-strength ERASE at $g154CX,$g154CY -- they read $(sed -n '17,32p' /tmp/g154_raw.txt | sort -u | tr '\n' ' '). ERASE paints the height this map calls ground, and this map calls it $g154D, not 64"
        continue
    fi
    g154H=$(grep -a "^HEIGHT|$g154CX,$g154CY|" "/tmp/g154_$g154m.log" | head -1 |
            sed -n 's/.*|centre=\([-0-9.]*\)|.*/\1/p')
    if [ "$g154H" != "0.0000" ]; then
        G154FAIL="on $g154m the erased cell $g154CX,$g154CY draws at world y $g154H and not 0.0000. The datum is the level the world was re-zeroed on, so ground erased to it is ground at zero -- this is the same leg above in the units a player sees, and it is what a pit reads as"
        continue
    fi
    # NO PROCESS SUBSTITUTION. `diff <(a) <(b)` is a bashism, and this suite runs under
    # sh: `sh -c 'diff <(echo a) <(echo a)'` answers "syntax error near unexpected token (".
    # So the command never ran, the || arm always fired, and this leg reported MOVED on
    # every build ever made -- a leg that could not go green rather than one that could not
    # go red, which is the same disease facing the other way and just as expensive. Two
    # plain files and cmp say the same thing in POSIX.
    sed -n '17,32p' /tmp/g154_raw.txt > /tmp/g154_after1.txt
    sed -n '33,48p' /tmp/g154_raw.txt > /tmp/g154_after2.txt
    if cmp -s /tmp/g154_after1.txt /tmp/g154_after2.txt; then g154MOVE=0; else g154MOVE=1; fi
    if [ "$g154MOVE" != "0" ]; then
        G154FAIL="on $g154m a SECOND ERASE over ground already on the datum moved corners that were standing on $g154D. Erasing ground that is already at the map's own level has to be a no-op, or the tool digs every time it is used"
        continue
    fi
    G154SAY="$G154SAY $g154m ground $g154D at $g154CX,$g154CY ($g154AT of its 16 core corners already there);"
done
if [ "$GRC" != "0" ] && [ -z "$G154FAIL" ]; then
  bad "G154 erase paints the map's own ground: a run failed (exit $GRC)"
elif [ -n "$G154FAIL" ]; then
  bad "G154 erase paints the map's own ground: $G154FAIL"
else
  ok "G154 erase paints the map's own ground: on three shipped maps with three different ground levels --$G154SAY on each, a six second full-strength ERASE put all sixteen core corners on that map's own datum byte and the erased cell at world y 0.0000, and a second identical ERASE moved not one of them. None of the three datums is 64"
fi

# =====================================================================================
# G155 A STROKE THAT STOPS BEING HELD IS CLOSED, AND IT IS CLOSED WHERE IT STOPPED.
#
# THE DEFECT. sbr_stroke_end had exactly ONE non-headless caller -- the LEFT-button-up
# arm of the event handler -- while the pump ran every frame with no condition on it at
# all. A release is not guaranteed to reach that arm: the window can lose focus with the
# button down and the release lands in whatever took the focus; a dialog can open over
# the map; the panel's mode or its page can change under the pointer; the editor can be
# switched off, or PLAY can reboot the world. Every one of those left a stroke running
# for ever -- the dose climbing to its ceiling, the ground still moving, NO undo entry
# filed and the unlock never decided. A tool that cannot be undone is worse than a tool
# that does nothing.
#
# WHAT REPLACES IT. The pump is the one thing that runs in all of those states, so the
# pump closes the stroke: it is handed, with the clock, the answer to "is this button
# still down" -- editor up, ELEVATION showing, SMOOTH page, no dialog, this window has
# the keyboard, and the left button is actually down.
#
# WHAT IS MEASURED. The verb's fifth argument is the millisecond at which the button
# stops being held with NO release ever arriving, and on that path the verb does not call
# sbr_stroke_end itself. So the line it prints is the pump's own work.
#
#   it closed        onafter=0. A 1 is a stroke still integrating after the verb returned.
#   where it stopped the abandoned stroke has to be the SAME GROUND as a plain stroke
#                    held for exactly as long as the button really was -- same ticks,
#                    same digest. A stroke that ran on to the full two seconds would be
#                    the defect with a tidier ending.
#   it is undoable   one undo puts the corners AND the unlock back where they were.
#   the control      the same stroke NOT abandoned runs the full two seconds and lands
#                    somewhere else, or the comparison above is between two nothings.
# =====================================================================================
gbegin /tmp/g155.log
cat > /tmp/g155.script <<'EOF'
sbrstate
sbrset 1 0 3.0 3.0 1.00
sbrstroke 40 40 2000 16 400
sbrstate
editundo 1
sbrstate
sbrstroke 40 40 384 16
editundo 1
sbrstroke 40 40 2000 16
quit
EOF
grun /tmp/g155.log --edit --editmode 2 $BASE --nosound --w 1400 --h 900 \
     --scen SCG01EA --pack SCG01EA.pack --script /tmp/g155.script
G155A=$(grep -a '^SBRSTROKE|' /tmp/g155.log | sed -n 1p)
G155B=$(grep -a '^SBRSTROKE|' /tmp/g155.log | sed -n 2p)
G155C=$(grep -a '^SBRSTROKE|' /tmp/g155.log | sed -n 3p)
g155f() { echo "$1" | sed -n "s/.*|$2=\([0-9a-f]*\).*/\1/p"; }
G155ON=$(g155f "$G155A" onafter)
G155AT=$(g155f "$G155A" ticks);  G155BT=$(g155f "$G155B" ticks)
G155AD=$(g155f "$G155A" corners); G155BD=$(g155f "$G155B" corners)
G155CD=$(g155f "$G155C" corners)
G155AM=$(g155f "$G155A" moved)
G155S0=$(grep -a '^SBRSTATE|' /tmp/g155.log | sed -n 1p)
G155S1=$(grep -a '^SBRSTATE|' /tmp/g155.log | sed -n 2p)
G155S2=$(grep -a '^SBRSTATE|' /tmp/g155.log | sed -n 3p)
G155D0=$(g155f "$G155S0" corners); G155D2=$(g155f "$G155S2" corners)
G155U0=$(g155f "$G155S0" unlock);  G155U1=$(g155f "$G155S1" unlock)
G155U2=$(g155f "$G155S2" unlock)
if [ "$GRC" != "0" ]; then
  bad "G155 an abandoned stroke is closed where it stopped: the run failed (exit $GRC)"
elif [ -z "$G155AD" ] || [ -z "$G155BD" ] || [ -z "$G155CD" ] || [ -z "$G155D0" ]; then
  bad "G155 an abandoned stroke is closed where it stopped: the run did not produce three strokes and three readings of the map (got $(grep -ac '^SBRSTROKE|' /tmp/g155.log) strokes, $(grep -ac '^SBRSTATE|' /tmp/g155.log) states)"
elif [ "${G155ON:-1}" != "0" ]; then
  bad "G155 an abandoned stroke is closed where it stopped: the button stopped being held at 400 ms and the stroke was STILL RUNNING when the verb returned (onafter=$G155ON). Nothing else was ever going to end it: the release never arrives, so the pump has to"
elif [ "${G155AM:-0}" != "1" ]; then
  bad "G155 an abandoned stroke is closed where it stopped: the abandoned stroke reports moved=$G155AM, so it wrote nothing and the legs below would be comparing two untouched maps"
elif [ "${G155U1:-0}" != "1" ]; then
  bad "G155 an abandoned stroke is closed where it stopped: the abandoned stroke moved bytes and left unlock=$G155U1. A stroke that changed the ground has to make the unlock decision on its way out, exactly as an ordinary one does"
elif [ "$G155AT" != "$G155BT" ] || [ "$G155AD" != "$G155BD" ]; then
  bad "G155 an abandoned stroke is closed where it stopped: abandoned at 400 ms it applied $G155AT ticks and left $G155AD, while a plain stroke held for exactly the 384 ms the button really was down applied $G155BT ticks and left $G155BD. A stroke that goes on past the moment the button stopped is the defect wearing a tidier ending"
elif [ "$G155D2" != "$G155D0" ] || [ "${G155U2:-1}" != "${G155U0:-0}" ]; then
  bad "G155 an abandoned stroke is closed where it stopped: one undo left the corners at $G155D2 against $G155D0 as opened, and unlock at $G155U2 against $G155U0. The whole reason to close the stroke is that it files an undo entry"
elif [ "$G155CD" = "$G155AD" ]; then
  bad "G155 an abandoned stroke is closed where it stopped: two seconds of the same stroke, NOT abandoned, left the same ground ($G155CD) as the one cut off at 400 ms. Either the brush stops on its own or the digest is not reading the ground, and the legs above proved nothing"
else
  ok "G155 an abandoned stroke is closed where it stopped: a stroke whose button stopped being held at 400 ms with no release ever arriving was closed by the pump (onafter=0), applied the $G155AT ticks the 384 ms it was really held buys and left exactly the ground a plain 384 ms stroke leaves ($G155AD), took the unlock with it, and undid in one step back to $G155D0; the same stroke held for the full two seconds lands somewhere else ($G155CD)"
fi

# =====================================================================================
# G156 THE UNLOCK IS IN THE MAP, NOT IN THE SESSION.
#
# TWO DEFECTS, ONE FLAG. g_elevSmoothUnlock lived in memory and in the undo snapshot and
# nowhere else.
#
#   IT DID NOT SURVIVE A SAVE. A stroke unlocked the rung tool, SAVE wrote the map, and
#   opening that map again locked it: saving your work took back the tool you had just
#   earned. It is now a CNC3D-owned [Basic] key, written the way CNC3DKind is -- older
#   copies dropped, a new one written only while it is true.
#
#   AND IT LEAKED THE OTHER WAY. Nothing cleared it when a different map was loaded, so
#   an unlock earned on one map was still standing over the next one, which is a map
#   claiming to be something it is not. It is now read from the file on every editor
#   load, which is the same line that clears it.
#
# WHY IT IS NOT DERIVED FROM THE HEIGHTS. "Corners off the ladder mean somebody smoothed
# this map" is the tempting rule and it is wrong on the maps that matter: SCG01EA opens
# with 332 of its 728 counted corners off the ladder, as shipped and untouched. Deriving
# would unlock every cartridge map on sight and leave CONVERT nothing to be. This gate
# reads that number back so the reasoning stays checked.
#
# BOTH MAP-CHANGE PATHS ARE WALKED, because there are two and only one of them reboots.
# NEW MAP onto the theater already loaded takes a fast path that never reloads anything,
# so the load-time clear cannot reach it; NEW MAP onto another theater reboots through
# the pack loader and does. The gate does whichever the run reports.
# =====================================================================================
gbegin /tmp/g156.log /tmp/g156b.log
rm -f missions/user_maps/USERG156.INI missions/user_maps/USERG156.BIN \
      missions/user_maps/USERG156.HGT
cat > /tmp/g156.script <<'EOF'
sbrstate
sbrset 1 0 3.0 3.0 1.00
sbrstroke 40 40 800 16
sbrstate
editscen USERG156
editsave
newmap 1 0 0
sbrstate
quit
EOF
grun /tmp/g156.log --edit --editmode 2 $BASE --nosound --w 1400 --h 900 \
     --scen SCG01EA --pack SCG01EA.pack --script /tmp/g156.script
cat > /tmp/g156b.script <<'EOF'
sbrstate
quit
EOF
grun /tmp/g156b.log --edit --editmode 2 --cameos cameos.pack \
     --dospack dossidebar.pack --dosinf dosinfantry.pack --dylib TiberianDawn.dylib \
     --dir missions/user_maps/ --content content/ --nosound --w 1400 --h 900 \
     --scen USERG156 --pack SCG01EA.pack --script /tmp/g156b.script
g156f() { echo "$1" | sed -n "s/.*|$2=\([0-9a-f/]*\).*/\1/p"; }
G156S0=$(grep -a '^SBRSTATE|' /tmp/g156.log | sed -n 1p)
G156S1=$(grep -a '^SBRSTATE|' /tmp/g156.log | sed -n 2p)
G156S2=$(grep -a '^SBRSTATE|' /tmp/g156.log | sed -n 3p)
G156S3=$(grep -a '^SBRSTATE|' /tmp/g156b.log | sed -n 1p)
G156U0=$(g156f "$G156S0" unlock); G156U1=$(g156f "$G156S1" unlock)
G156U2=$(g156f "$G156S2" unlock); G156U3=$(g156f "$G156S3" unlock)
G156OFF=$(g156f "$G156S0" offladder)
G156KEY=$(grep -ac '^CNC3DSmooth=1' missions/user_maps/USERG156.INI 2>/dev/null)
G156SRC=$(grep -ac 'CNC3DSmooth' missions/SCG01EA.INI 2>/dev/null)
G156NEW=$(grep -ac '^NEWMAP|rebooted' /tmp/g156.log)
if [ "$GRC" != "0" ]; then
  bad "G156 the unlock is in the map, not in the session: a run failed (exit $GRC)"
elif [ -z "$G156U0" ] || [ -z "$G156U1" ] || [ -z "$G156U2" ] || [ -z "$G156U3" ]; then
  bad "G156 the unlock is in the map, not in the session: the two runs did not report the four states this gate reads (got $(grep -ac '^SBRSTATE|' /tmp/g156.log) and $(grep -ac '^SBRSTATE|' /tmp/g156b.log))"
elif [ "${G156SRC:-1}" != "0" ]; then
  bad "G156 the unlock is in the map, not in the session: the shipped SCG01EA.INI already carries a CNC3DSmooth line ($G156SRC of them), so this gate's control map is not the unwritten-on map it is meant to be"
elif [ "${G156U0:-1}" != "0" ]; then
  bad "G156 the unlock is in the map, not in the session: SCG01EA opens with unlock=$G156U0. A shipped map has never been smoothed and must open locked, or CONVERT has nothing left to be -- this one opens $G156OFF of its counted corners OFF the ladder, which is exactly the state that must NOT be read as an unlock"
elif [ "${G156U1:-0}" != "1" ]; then
  bad "G156 the unlock is in the map, not in the session: a smooth stroke left unlock=$G156U1, so there was never anything to persist"
elif [ "${G156KEY:-0}" != "1" ]; then
  bad "G156 the unlock is in the map, not in the session: the saved USERG156.INI carries $G156KEY CNC3DSmooth=1 lines (want exactly 1). The unlock has to be written into the document, or saving your work takes back the tool you just earned"
elif [ "${G156U2:-1}" != "0" ]; then
  bad "G156 the unlock is in the map, not in the session: after NEW MAP the unlock is still $G156U2. It belongs to the map that WAS open; a blank map has never been smoothed"
elif [ "${G156U3:-0}" != "1" ]; then
  bad "G156 the unlock is in the map, not in the session: USERG156 was saved carrying the key and comes back with unlock=$G156U3. The whole point of writing it down is that reopening the map finds it"
else
  ok "G156 the unlock is in the map, not in the session: SCG01EA opens locked ($G156OFF of its counted corners off the ladder, and that is NOT read as an unlock), a stroke unlocks it, the save writes one CNC3DSmooth=1 into the document, a NEW MAP in the same process comes up locked again ($(if [ "${G156NEW:-0}" -ge 1 ]; then echo "through the reboot path"; else echo "through the fast path that never reloads"; fi)), and reopening the saved map finds the unlock waiting"
fi



# ---------------------------------------------------------------------------------
# G190 THE AUTO HEIGHTMAP, ON MAPS THE GAME ACTUALLY SHIPS.
#
# The rung brush works the other way round -- paint ground, get cliffs. This is the map
# that already HAS cliff art and no relief under it: every map converted from a DOS
# editor, and every map whose slopes were stamped out of the cliff drawer. The fit
# recovers each SLOPE PLACEMENT from the .BIN, reads its high side off the compass,
# solves one level per placement, reduces those to one TIER per 2x2 block and writes the
# ground through elev_coarse and elev_heights -- the same path a brush stroke takes.
#
# IT RUNS ON REAL MAPS OR IT IS NOT TESTED. The first gate written for this feature built
# both of its maps with newmap and terrpaint, and edit_blank_map sets g_elevConverted for
# ever -- so the one failure mode that mattered, the fit taking a usable map off the
# ladder and locking the elevation panel behind the conversion gate, COULD NOT OCCUR on
# the map under test. Every map here is one the game ships, opened from missions/, and
# two of the four arrive off the ladder with the panel already locked.
#
# WHAT EACH LEG MEANS, because these are not arbitrary numbers:
#   places>0    the fit found the cliff art. Zero is the feature not running at all,
#               which is what every one of the fifteen shipped maps answered while the
#               fit demanded the map be CONVERTED first -- a conversion that repaints
#               every level block as plain ground and so erases the art the fit reads.
#   stray=0     THE SAFETY PROPERTY. Of the playable corners still off the ladder after
#               the fit, none is a corner the fit was free to write. Every byte it wrote
#               came out of ELEV_RUNG; the only ones left are the sea's, which it does
#               not touch. A non-zero stray is the fit writing off the ladder.
#   off A>=B    and the count itself never rises.
#   ready=1     the elevation panel is usable AFTER the press. It was 0 before it on two
#               of these maps: the fit is the way ONTO the ladder that reads the art
#               instead of erasing it.
#   held>0      corners belonging to water, of which every one of these four maps has
#               some, and wetkept=1 says not one of them moved -- a digest of exactly
#               those corners, taken either side of the write. The first version of this
#               read one hand-picked lake cell instead, and pulling the water guard out
#               left it green while fifty-two corners of sea rose.
#   drawn=marked  the overlay drew exactly the cells the one stored report counted.
# THE SHOT IS NOT DECORATION: the overlay is a DRAW-time diagnostic, so without a frame
# it never runs and that leg silently measures nothing.
G190E="--edit --editmode 2 $BASE --nosound --w 1400 --h 900"
cat > /tmp/g190.txt <<'SCRIPT'
sbrstate
autohgt
autohgt
shot shots/g190.png
sbrstate
quit
SCRIPT
G190BAD=""
G190SUM=""
gbegin shots/g190.png
for g190m in SCB31EA SCB01EA SCG10EA SCB60EA; do
  rm -f shots/g190.png
  grun "/tmp/g190_$g190m.log" $G190E --scen "$g190m" --pack "$g190m.pack" --script /tmp/g190.txt
  g190l=$(grep '^AUTOHGT|valid=1' "/tmp/g190_$g190m.log" | head -1)
  g190o=$(grep '^AHOVERLAY|' "/tmp/g190_$g190m.log" | tail -1 | sed -n 's/.*|drawn=\([0-9]*\).*/\1/p')
  g190g() { printf '%s' "$g190l" | sed -n "s/.*|$1=\([^|]*\).*/\1/p"; }
  g190pl=$(g190g places);  g190st=$(g190g stray);   g190rd=$(g190g ready)
  g190hd=$(g190g held);    g190mk=$(g190g marked);  g190of=$(g190g off)
  g190wk=$(g190g wetkept)
  g190a=${g190of%%/*}; g190b=${g190of##*/}
  g190r0=$(grep '^SBRSTATE|' "/tmp/g190_$g190m.log" | head -1 | sed -n 's/.*|ready=\([0-9]*\).*/\1/p')
  g190cv=$(grep -c 'CONVERT THIS MAP first' "/tmp/g190_$g190m.log")
  [ -s shots/g190.png ] || G190BAD="$G190BAD $g190m:no-frame"
  [ "${g190pl:-0}" -gt 0 ] 2>/dev/null || G190BAD="$G190BAD $g190m:places=${g190pl:-none}"
  [ "${g190st:-1}" = "0" ]             || G190BAD="$G190BAD $g190m:stray=${g190st:-none}"
  [ "${g190rd:-0}" = "1" ]             || G190BAD="$G190BAD $g190m:ready=${g190rd:-none}"
  [ "${g190hd:-0}" -gt 0 ] 2>/dev/null || G190BAD="$G190BAD $g190m:held=${g190hd:-none}"
  [ "${g190wk:-0}" = "1" ]             || G190BAD="$G190BAD $g190m:wetkept=${g190wk:-none}"
  [ "${g190b:-1}" -le "${g190a:-0}" ] 2>/dev/null || G190BAD="$G190BAD $g190m:off=$g190of-ROSE"
  [ "${g190o:-x}" = "${g190mk:-y}" ]   || G190BAD="$G190BAD $g190m:overlay=${g190o:-none}/marked=${g190mk:-none}"
  [ "${g190cv:-1}" = "0" ]             || G190BAD="$G190BAD $g190m:asked-for-CONVERT"
  G190SUM="$G190SUM $g190m(ready $g190r0->$g190rd, $g190pl places, off $g190of, $g190hd held and unmoved, $g190mk marked=$g190o drawn)"
done
# AND THE .HGT THE SAVE WRITES, on the last of them, with the files removed again: this
# gate must not leave user maps behind. No gbegin here on purpose -- gbegin clears GRC,
# and clearing it after four runs have already reported would throw away their exit
# status. This run takes no picture, so it needs none.
cat > /tmp/g190s.txt <<'SCRIPT'
autohgt
editsave
quit
SCRIPT
grun /tmp/g190_save.log $G190E --scen SCB60EA --pack SCB60EA.pack --script /tmp/g190s.txt
G190HGT=$(grep -c 'edit: wrote .*SCB60EA\.HGT (4225 bytes)' /tmp/g190_save.log)
rm -f missions/user_maps/SCB60EA.BIN missions/user_maps/SCB60EA.INI missions/user_maps/SCB60EA.HGT
if [ "$GRC" = "0" ] && [ -z "$G190BAD" ] && [ "${G190HGT:-0}" -ge 1 ]; then
  ok "G190 auto heightmap on shipped maps:$G190SUM -- every one of the four fits with no CONVERT asked for, leaves no corner off the ladder that is not the sea's (stray=0), never raises the off-the-ladder count, and hands the elevation panel back usable; SAVE writes a 4225-byte .HGT"
else
  bad "G190 auto heightmap on shipped maps: exit=$GRC hgt4225=$G190HGT(want >=1; 0 means g_elevTouched was never set and the heightmap is silently not saved) failures:${G190BAD:- none} | readings:$G190SUM"
fi


# ---------------------------------------------------------------------------------
# G191 THE AUTO HEIGHTMAP'S GUARD RAILS, ON A SHIPPED MAP THAT ARRIVES OFF THE LADDER.
#
# SCB01EA opens with 424 of its 1040 playable corners off the ladder, so the elevation
# panel is showing the conversion gate and the fit's button is the second control on that
# card. It also has a coastline, which is the other half of this gate.
#
# FOUR THINGS, IN ONE RUN, IN THIS ORDER:
#   1  the first press only ARMS. It writes nothing and moves no byte, because the map
#      carries hand-authored relief and replacing it silently is the one surprise this
#      row exists to avoid. The corner digest is identical either side of it.
#   2  the second press fits: the ground moves, the panel becomes usable, and the unlock
#      that made it usable is INSIDE the undo entry rather than beside it.
#   3  the water does not move. wetkept is a digest of exactly the corners the sea uses,
#      taken either side of the write, so it answers for ALL of them; the named water
#      cell read before and after is the same claim in a form a person can read. The
#      first build of this feature flooded water like open ground and lifted a lake by
#      two rungs.
#   4  ONE undo puts all of it back: the corner digest returns to the byte it started on,
#      the unlock goes with it, the off-the-ladder count returns to what it was, and the
#      report -- which describes corners that are no longer there -- is cleared.
# The undo goes through the editor's own edit_undo, not a harness.
G191E="--edit --editmode 2 $BASE --nosound --w 1400 --h 900 --scen SCB01EA --pack SCB01EA.pack"
cat > /tmp/g191.txt <<'SCRIPT'
sbrstate
height 40 12
autohgt
sbrstate
autohgt
sbrstate
height 40 12
shot shots/g191.png
editundo 1
sbrstate
autohgt report
height 40 12
autohgt
autohgt
sbrstate
sbrstroke 28 18 400 16
sbrstate
autohgt report
quit
SCRIPT
gbegin shots/g191.png
grun /tmp/g191.log $G191E --script /tmp/g191.txt
gshots shots/g191.png
g191f() { printf '%s' "$1" | sed -n "s/.*|$2=\([^|]*\).*/\1/p"; }
G191S0=$(grep '^SBRSTATE|' /tmp/g191.log | sed -n '1p')
G191S1=$(grep '^SBRSTATE|' /tmp/g191.log | sed -n '2p')
G191S2=$(grep '^SBRSTATE|' /tmp/g191.log | sed -n '3p')
G191S3=$(grep '^SBRSTATE|' /tmp/g191.log | sed -n '4p')
G191P1=$(grep '^AUTOHGT|' /tmp/g191.log | sed -n '1p')
G191P2=$(grep '^AUTOHGT|' /tmp/g191.log | sed -n '2p')
G191P3=$(grep '^AUTOHGT|' /tmp/g191.log | sed -n '3p')
G191C0=$(g191f "$G191S0" corners); G191C1=$(g191f "$G191S1" corners)
G191C2=$(g191f "$G191S2" corners); G191C3=$(g191f "$G191S3" corners)
G191R0=$(g191f "$G191S0" ready);   G191R2=$(g191f "$G191S2" ready)
G191R3=$(g191f "$G191S3" ready)
G191O0=$(g191f "$G191S0" offladder); G191O2=$(g191f "$G191S2" offladder)
G191O3=$(g191f "$G191S3" offladder)
G191U2=$(g191f "$G191S2" unlock);  G191U3=$(g191f "$G191S3" unlock)
G191A1=$(g191f "$G191P1" armed);   G191V1=$(g191f "$G191P1" valid)
G191M2=$(g191f "$G191P2" moved);   G191H2=$(g191f "$G191P2" held)
G191K2=$(g191f "$G191P2" wetkept)
G191V3=$(g191f "$G191P3" valid);   G191K3=$(g191f "$G191P3" marked)
# AND THE OTHER HALF OF THE SAME RULE. An undo is not the only thing that moves the
# ground under a report: so does the next brush stroke. The tail of the script fits
# again on the map the undo restored, draws one real rung stroke through the brush's
# own funnel, and asks for the report. Marks describing corners a stroke has since
# moved are worse than no marks, because they look like the fit's own judgement.
G191P6=$(grep '^AUTOHGT|' /tmp/g191.log | sed -n '6p')
# THE CORNER DIGEST EITHER SIDE OF THAT STROKE, and not the SBRSTROKE line's own moved=,
# which is a flag saying the pointer travelled and says nothing at all about the ground.
# 28,18 is the driest 4x4 footprint on this map (1 of its 80 corners is the sea's), so a
# stroke there has ground to move; if the digest does not change, the tail measured
# nothing and the cleared report below would be proving somebody else's clear.
G191C4=$(g191f "$(grep '^SBRSTATE|' /tmp/g191.log | sed -n '5p')" corners)
G191C5=$(g191f "$(grep '^SBRSTATE|' /tmp/g191.log | sed -n '6p')" corners)
G191V6=$(g191f "$G191P6" valid);   G191K6=$(g191f "$G191P6" marked)
G191W0=$(grep '^HEIGHT|40,12|' /tmp/g191.log | sed -n '1p')
G191W1=$(grep '^HEIGHT|40,12|' /tmp/g191.log | sed -n '2p')
G191W2=$(grep '^HEIGHT|40,12|' /tmp/g191.log | sed -n '3p')
G191UND=$(grep '^EDITUNDO|' /tmp/g191.log | sed -n 's/.*|left=\([0-9]*\).*/\1/p')
G191WARN=$(grep -c 'THIS REPLACES THE RELIEF' /tmp/g191.log)
if [ "$GRC" = "0" ] && \
   [ "$G191A1" = "1" ] && [ "$G191V1" = "0" ] && [ "$G191C0" = "$G191C1" ] && \
   [ "${G191WARN:-0}" -ge 1 ] && \
   [ "${G191M2:-0}" -gt 0 ] && [ "${G191H2:-0}" -gt 0 ] && [ "$G191K2" = "1" ] && \
   [ "$G191R0" = "0" ] && [ "$G191R2" = "1" ] && [ "$G191U2" = "1" ] && \
   [ "$G191C0" != "$G191C2" ] && \
   [ -n "$G191W0" ] && [ "$G191W0" = "$G191W1" ] && [ "$G191W1" = "$G191W2" ] && \
   [ "$G191UND" = "0" ] && \
   [ "$G191C3" = "$G191C0" ] && [ "$G191U3" = "0" ] && [ "$G191R3" = "$G191R0" ] && \
   [ "$G191O3" = "$G191O0" ] && [ "$G191V3" = "0" ] && [ "$G191K3" = "0" ] && \
   [ -n "$G191C4" ] && [ "$G191C4" != "$G191C5" ] && \
   [ "$G191V6" = "0" ] && [ "$G191K6" = "0" ]; then
  ok "G191 auto heightmap guard rails on SCB01EA: the first press only ARMS and moves no byte (corners still $G191C0); the second fits -- $G191M2 corners moved, $G191H2 left to the water, the panel goes from locked to usable (ready $G191R0 -> $G191R2, off the ladder $G191O0 -> $G191O2) and the unlock that did it is inside the undo entry; the named water cell reads the same string before, after and after the undo ($G191W0); and ONE undo puts the ground back byte for byte ($G191C3), takes the unlock with it ($G191U3), returns the readout to $G191O3 and clears the report it no longer describes; and a fit followed by ONE rung stroke clears the report too (corners $G191C4 -> $G191C5), so no mark ever outlives the ground it was drawn about"
else
  bad "G191 auto heightmap guard rails: exit=$GRC | press1 armed=$G191A1(want 1) valid=$G191V1(want 0) corners $G191C0->$G191C1(must be equal; unequal means it wrote without asking) warned=$G191WARN(want >=1) | press2 moved=$G191M2(want >0) held=$G191H2(want >0; 0 on a map with a coastline means the water test is not running) wetkept=$G191K2(want 1; 0 means the write went over the sea's own corners) ready $G191R0->$G191R2(want 0->1) unlock=$G191U2(want 1) off $G191O0->$G191O2 corners-changed=$([ "$G191C0" != "$G191C2" ] && echo yes || echo NO) | water '$G191W0' / '$G191W1' / '$G191W2'(all three must be the same string) | undo left=$G191UND(want 0; more means the fit pushed more than one entry) corners=$G191C3(want $G191C0) unlock=$G191U3(want 0) ready=$G191R3(want $G191R0) off=$G191O3(want $G191O0) report valid=$G191V3 marked=$G191K3(want 0 and 0) | after a later stroke: corners $G191C4 -> $G191C5(must DIFFER; equal means the stroke moved no byte and this leg measured nothing) report valid=$G191V6 marked=$G191K6(want 0 and 0; a standing report here is marks describing corners the stroke has since moved)"
fi


# ---------------------------------------------------------------------------------
# G192 THE ELEVATION PANEL'S LAYOUT, WITH THE AUTO HEIGHTMAP ROW IN IT.
#
# --uitest walks sixteen frame size / backing pairs, and now NINE panel passes rather
# than eight: the ninth is the conversion gate's own card, which had never been walked at
# all because the driver set g_elevConverted for every elevation pass it made while the
# comment beside it claimed both sides were tested. That card is where the fit's button
# lives, and walking it found that the whole branch returned EUI_DEAD -- so UNDO, REDO,
# REVERT, SAVE and PLAY were all unclickable on any map that arrived off the ladder.
#
# THE IMPORT ROW MUST NOT MOVE. It answers a click at 12 of the 16 pairs, and it did
# before this feature existed. An auto row placed ABOVE it takes that to 11, which is a
# window size where a control that used to work no longer does. This row goes BELOW it
# instead, so the count is unchanged and the assertion is a number rather than a hope.
#
# AND THE CAPTION, WHICH HAS NO HIT RECTANGLE AT ALL. "CLIFF PIECES  CLICK TO ARM..." is
# text, not a control, so --uitest cannot see it by clicking and a probe-based harness is
# structurally blind to it -- which is how it came to be painted into the status card, on
# top of the readout, at 1280x480. Two legs cover it: the harness now MEASURES it at all
# sixteen pairs, the way the smooth page's help block is measured, and a real frame is
# RENDERED at a 640x240 window (a 1280x480 framebuffer) where the caption does not fit,
# with the draw itself reporting whether it painted it.
gbegin shots/g192_short.png shots/g192_tall.png
grun /tmp/g192_ui.log --uitest
G192UT=$(grep -c '^UITEST|TOTAL|0 failures' /tmp/g192_ui.log)
G192IMP=$(grep -c 'UITEST|ok  |import heightmap' /tmp/g192_ui.log)
G192AUT=$(grep -c 'UITEST|ok  |auto heightmap' /tmp/g192_ui.log)
G192GAU=$(grep -c 'UITEST|ok  |gate auto heightmap' /tmp/g192_ui.log)
G192GCV=$(grep -c 'UITEST|ok  |convert this map' /tmp/g192_ui.log)
G192AFT=$(grep -c '^ELEVROWS|.*|autofit=1|' /tmp/g192_ui.log)
G192GFT=$(grep -c '^ELEVGATE|.*|autofit=1$' /tmp/g192_ui.log)
G192ROWS=$(grep -c '^ELEVROWS|' /tmp/g192_ui.log)
G192GRW=$(grep -c '^ELEVGATE|' /tmp/g192_ui.log)
# THE GATE CARD'S TWO CLEARANCES, neither of which is a control and so neither of
# which any probe can see. The caption that says what else is behind the gate must
# start BELOW the CONVERT button, and the fit's row must start below the caption. The
# first fitting of this row got both wrong: the button painted over the caption at
# every window size, and it took a screenshot rather than this harness to find it.
G192CLR=$(grep -a '^ELEVGATE|' /tmp/g192_ui.log | awk -F'|' '
  { for (i=1;i<=NF;i++) { split($i,kv,"="); v[kv[1]]=kv[2] }
    if (v["capclear"]+0 <= 0 || v["autoclear"]+0 < 0) n++ } END { print n+0 }')
G192ACT=$(grep -c 'UITEST|FAIL|action ' /tmp/g192_ui.log)
G192E="--edit --editmode 2 $BASE --nosound --scen SCB31EA --pack SCB31EA.pack"
cat > /tmp/g192s.txt <<'SCRIPT'
shot shots/g192_short.png
quit
SCRIPT
cat > /tmp/g192t.txt <<'SCRIPT'
shot shots/g192_tall.png
quit
SCRIPT
grun /tmp/g192_short.log $G192E --w 640  --h 240 --script /tmp/g192s.txt
grun /tmp/g192_tall.log  $G192E --w 1400 --h 900 --script /tmp/g192t.txt
gshots shots/g192_short.png shots/g192_tall.png
G192CS=$(grep '^ELEVCAP|' /tmp/g192_short.log | tail -1)
G192CT=$(grep '^ELEVCAP|' /tmp/g192_tall.log  | tail -1)
G192DS=$(printf '%s' "$G192CS" | sed -n 's/.*|drawn=\([0-9]*\).*/\1/p')
G192DT=$(printf '%s' "$G192CT" | sed -n 's/.*|drawn=\([0-9]*\).*/\1/p')
if [ "$GRC" = "0" ] && \
   [ "$G192UT" = "1" ] && [ "$G192ACT" = "0" ] && \
   [ "$G192IMP" = "12" ] && \
   [ "$G192ROWS" = "16" ] && [ "$G192GRW" = "16" ] && [ "$G192GCV" = "16" ] && \
   [ "${G192CLR:-1}" = "0" ] && \
   [ "$G192AUT" = "$G192AFT" ] && [ "$G192GAU" = "$G192GFT" ] && \
   [ -n "$G192DS" ] && [ "$G192DS" = "0" ] && [ "$G192DT" = "1" ]; then
  ok "G192 elevation layout with the auto row: --uitest is clean over 9 passes x 16 size/backing pairs, IMPORT HEIGHTMAP still answers at $G192IMP of 16 (the row did not move), the auto row answers at exactly the $G192AFT tier-page and $G192GFT gate-card pairs where its rectangle fits and at none of the others, CONVERT THIS MAP is walked at all $G192GCV pairs on a card the harness had never opened, the caption under it clears that button and the row clears the caption at all $G192GRW pairs, the action row survives on it ($G192ACT dead-control failures), and the CLIFF PIECES caption is NOT painted in a rendered 1280x480 frame where it would land in the status card ($G192CS) while it is painted in a tall one ($G192CT)"
else
  bad "G192 elevation layout with the auto row: exit=$GRC uitest-clean=$G192UT(want 1) dead-action-failures=$G192ACT(want 0; 48 of them -- three controls at sixteen size/backing pairs -- is the gate card's branch returning EUI_DEAD over UNDO, REDO, REVERT, and SAVE and PLAY with them) import-hittable=$G192IMP(want 12; 11 means the auto row was put ABOVE the import row and cost it a window size) elevrows=$G192ROWS elevgate=$G192GRW convert-probed=$G192GCV(want 16 each) gatecard-overlaps=$G192CLR(want 0; a row here is the gate card's caption drawn inside the CONVERT button or the fit's row drawn on top of the caption -- neither is a control, so nothing but this leg can see it) -- first: $(grep -a '^ELEVGATE|' /tmp/g192_ui.log | awk -F'|' '{ for (i=1;i<=NF;i++) { split($i,kv,"="); v[kv[1]]=kv[2] } if (v["capclear"]+0 <= 0 || v["autoclear"]+0 < 0) { print; exit } }') auto-hittable=$G192AUT(want $G192AFT) gate-auto=$G192GAU(want $G192GFT) caption-drawn short=$G192DS(want 0) tall=$G192DT(want 1) | $G192CS | $G192CT"
fi



# =====================================================================================
# G193 THE AUTO HEIGHTMAP'S REPORT DOES NOT OUTLIVE THE MAP IT DESCRIBES.
#
# THE REPORT. Opening a different map from inside the editor crashed it outright:
# EXC_BAD_ACCESS, KERN_INVALID_ADDRESS at 0x0, the backtrace naming sb_place_cell_tris
# under edit_draw_overlay. Two causes, and this gate holds both ends.
#
# THE POINTER. sb_place_cell_tris null-checked TerrainCornerY and called WorldToScreen
# straight. The host wires those hooks when the sidebar comes up and they are absent
# while it is being rebuilt, which is exactly what an in-editor open does, so an overlay
# drawn in that window called through a null.
#
# THE STALE REPORT. ah_report_clear ran on undo, redo, revert, every elevation stroke and
# on a NEW map, but not on OPEN. So a fit on one map left its marks standing while a
# different map came up under them, which is what put an overlay in that window at all.
#
# WHAT IS ASSERTED: after a fit that really marked cells, an OPEN drops the report, so no
# AHOVERLAY line follows it. The pre-open fit must be real or the leg proves nothing,
# which is why the marked count is checked first.
# =====================================================================================
gbegin /tmp/g193.log
# READ THE STATE, NOT THE ABSENCE OF A LINE. The first version of this leg asserted that
# no AHOVERLAY followed the open. AHOVERLAY is printed only when its count CHANGES, so it
# never appears there whether the report was dropped or not: mutation-tested, and the leg
# passed with the fix removed. ahstate reads g_ah directly and can fail.
{ echo autohgt; echo autohgt; echo ahstate
  echo "editopen USER07"; echo ahstate
  echo quit; } > /tmp/g193.script
grun /tmp/g193.log --edit --editmode 2 $BASE --nosound --w 1400 --h 900 \
     --scen SCB01EA --pack SCB01EA.pack --script /tmp/g193.script
G193MARK=$(grep -a '^AUTOHGT|valid=1|' /tmp/g193.log | head -1 |
           sed -n 's/.*|marked=\([0-9]*\)|.*/\1/p')
G193OPEN=$(grep -ac '^EDITOPEN|picked|USER07|' /tmp/g193.log)
# Every overlay line AFTER the open. sed prints from the open to the end, so a report that
# survived the open is the only way one lands in that range.
G193BEFORE=$(grep -a '^AHSTATE|' /tmp/g193.log | sed -n 1p | sed -n 's/^AHSTATE|valid=\([01]\)|.*/\1/p')
G193AFTER=$(grep -a '^AHSTATE|' /tmp/g193.log | sed -n 2p | sed -n 's/^AHSTATE|valid=\([01]\)|.*/\1/p')
if [ "$GRC" != "0" ]; then
  bad "G193 the report does not outlive its map: the run failed (exit $GRC), so nothing below means anything"
elif [ "${G193OPEN:-0}" -lt 1 ]; then
  bad "G193 the report does not outlive its map: the open of USER07 never happened, so the leg below had no map change to survive. Check that missions/user_maps/USER07 is in the run folder"
elif [ "${G193MARK:-0}" -lt 1 ]; then
  bad "G193 the report does not outlive its map: the fit before the open marked $G193MARK cells (want >=1). With nothing marked there is no report to leave behind and this gate proves nothing"
elif [ "${G193BEFORE:-0}" != "1" ]; then
  bad "G193 the report does not outlive its map: the fit left valid=$G193BEFORE (want 1), so there was no live report for the open to drop and the leg below proves nothing"
elif [ "${G193AFTER:-1}" != "0" ]; then
  bad "G193 the report does not outlive its map: after opening another map the report still reads valid=$G193AFTER (want 0), describing ground that is on its way out. That is the overlay which reached sb_place_cell_tris while the sidebar hooks were being rewired, and it is how the editor died on a map open"
else
  ok "G193 the report does not outlive its map: a fit marked $G193MARK cells and left valid=1, and opening another map dropped it to valid=0"
fi

echo "----- $LABEL: $PASS pass, $FAIL fail -----" | tee -a "$OUT"
exit $FAIL
