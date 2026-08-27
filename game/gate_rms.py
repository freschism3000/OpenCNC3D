#!/usr/bin/env python3
"""Is there still SOUND at the end of a recorded run?

    python3 gate_rms.py FILE.wav SECONDS   ->   RMS|<int>|SECS|<float>|FRAMES|<n>

RMS over the last SECONDS of the WAV, rounded to an integer, plus the length so a caller
can tell "quiet" from "the recording stopped early". A missing or empty file prints
RMS|-1, which every caller must treat as a failure rather than as silence.

WHY A RECORDING AND NOT A LOG LINE. The jukebox gate (G48b) needs to know that the score
is still coming out of the mixer a minute after a manually picked track ended, and the
MUSIC|play lines alone cannot say that: they are printed when a track is STARTED. Worse,
measured, WITHOUT --audiowav the control arm shows a single track too --
nothing drains the mixer ring when there is no device and no tap, so the run does not
reach the end of the track at all and a gate written without the tap is red on a fixed
build. --audiowav is therefore load-bearing in every caller of this helper.

Measured that day on SCG90EA, 900 ticks, --theme TARGET --soundvol 0: the Play arm reads
0.0 on the shipped v0.5.7 binary (the mixer is dead from second 39) and 5763.8 after the
fix, which is equal to the no-jukebox control. The Stop arm reads 0.0 on both, and that
is correct: Stop is meant to stop.

--soundvol 0 in those runs is load-bearing the other way. With effects on, battle noise
holds the tail at RMS 2000-4000 with the score already dead, and this helper cannot tell
the two apart -- it measures the mix, which is what a player hears.
"""
import sys, os, wave

p = sys.argv[1]
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0
if not os.path.exists(p) or os.path.getsize(p) == 0:
    print("RMS|-1|SECS|0|FRAMES|0")
    sys.exit(0)
w = wave.open(p)
sr, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
import numpy as np
a = np.frombuffer(w.readframes(n), dtype="<i2").astype(float)
want = int(secs * sr * ch)
tail = a[-want:] if len(a) > want else a
rms = float((tail ** 2).mean() ** 0.5) if len(tail) else 0.0
print("RMS|%d|SECS|%.1f|FRAMES|%d" % (int(round(rms)), n / float(sr) if sr else 0, n))
