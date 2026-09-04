# Phase 0: the cross architecture determinism gate

`docs/design-multiplayer.md` section 10, Phase 0. Lockstep multiplayer needs the macOS
arm64 build and the 32 bit Windows build of the SAME commit to simulate bit for bit alike.
This folder holds the tool that turns a per tick engine dump into one hash per tick, and
the captured hashes from each machine, named by platform, commit, scenario and tick count.

## The recipe, identical on either platform

```
CNC3D_TICKDUMP=1 <cncbrain> content/ missions/ SCB01EA 20000 > capture.log
python3 tools/phase0/tickhash.py capture.log > <platform>-<commit>-SCB01EA-20000.sha256
```

`cncbrain` is `brain/host/cnc_host.c`: `game/build.sh` builds it on the Mac and
`tools/win/build-win.sh` builds `cncbrain.exe` on Windows. `content/` is the two theatre
MIX stubs the shipped game carries (TEMPERAT.MIX sha1 44ffd85aaade, DESERT.MIX
441f8bdbf2b6), `missions/` is `game/missions/`. Default build level 7. The brain must be
built from the commit named in the file, or the comparison is meaningless.

## What the tool hashes, and the two things it deliberately ignores

Each output line is `<tick> <sha256>` over that tick's ENGINE lines only: from `TICK|n` to
the engine's own `OBJDUMP-END` receipt, keeping lines that start with an upper case tag
and `|` (`OBJ|`, `HOUSE|`, `TIB|`, `WALL|`, `VIEW|`, ...) or with `OBJDUMP-`. The last line
`ALL <sha256> ticks=N` chains every per tick hash into one number. Compare `ALL` first; if
it differs, `diff` the two files and the first differing line is the first divergent tick.

It ignores carriage returns, because Windows writes a redirected stdout as CRLF. And it
ignores everything the HOST prints, because the host's state readout after the first and
last tick carries the platform's pointer width in a buffer size, and its `[event]` lines
land wherever the host's stdout buffer happens to flush on Windows, which is not where they
land on the Mac. Both were found the expensive way on 3 Sep 2026: the first comparison had
all 20,000 ticks differing, and the engine was identical underneath.

## The result, 3 Sep 2026: PHASE 0 PASSES

    ALL eace4a6ee574b329f54a56e1824977a2c462fcfbf3bb7fbd541b217c231c3307 ticks=20000

Both files above carry that line and are byte identical, so `diff` closes the gate with no
argument. macOS arm64 and 32 bit Windows, built from commit 0372033, simulate 20,000 ticks
of SCB01EA identically at every single tick.

**Lockstep is viable.** The assumption every later phase rests on is now measured rather
than hoped, and the design's own words for it were that this is the one test that can kill
the recommendation. It did not.

### Three false alarms on the way, all worth knowing

The first comparison had every tick differing from tick 1, which is the shape of a
systematic difference rather than a drifting simulation. None of the three causes was the
engine.

1. **Line endings in the capture.** Windows opens stdout in text mode, so a redirected
   capture is CRLF and the macOS one is LF. Hashing raw bytes made every tick disagree.
2. **The host's chatter inside the block.** The host and the DLL do not share one stdout
   buffer on Windows, so the host's own state readout surfaced inside the blocks of ticks
   2 to 10. Only engine lines are hashed now.
3. **A buffer size that carries the platform's pointer width.** The host prints a state
   readout whose buffer is 4080118 bytes on arm64 and 4014574 on i686. Bounding each block
   at the engine's own `OBJDUMP-END` receipt keeps the host's opinion of itself out.

And one ruled out cheaply rather than assumed: **char signedness**, unsigned by default on
arm64 and signed on x86, which is the classic cross platform divergence. A Mac brain
rebuilt with `-fsigned-char` produces a byte identical capture.

Line endings bite twice: these two `.sha256` files are themselves written by Python on each
platform, so the Windows one arrived CRLF and a plain `diff` called all 20,001 lines
different while every hash matched. Both are stored LF.

## What is captured

| File | Built from | Self identical? | Hashed with |
|---|---|---|---|
| `windows-i686-0372033-SCB01EA-20000.sha256` | 32 bit Windows, MSYS2 i686 gcc 16.2, 3 Sep 2026 | yes, two independent runs | the tool as committed now |
| `mac-arm64-0372033-SCB01EA-20000.sha256` | macOS arm64, Apple clang, 3 Sep 2026 | yes, two independent runs | the tool as committed now |
| `mac-arm64-0372033-SCB01EA-tick1.txt` | the Mac's tick 1 block, raw | | (byte identical to the Windows tick 1 engine lines, checked by hand) |

The two `.sha256` files are byte identical, which is the gate. To re-run it after a brain
change, capture on both platforms from the same commit with the recipe above and `diff`.
