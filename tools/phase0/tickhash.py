#!/usr/bin/env python3
"""Turn a CNC3D_TICKDUMP capture into one SHA-256 per tick.

LINE ENDINGS ARE NORMALISED, and that is not tidiness. Windows opens stdout in TEXT mode,
so a capture redirected to a file there has every \n written as \r\n while the same
capture on macOS has \n. Hashing the bytes as they arrive therefore makes the two sides
disagree on every tick even when their simulations are identical, which is exactly the
false negative this gate exists to avoid reporting. Measured 3 Sep 2026: the committed
Windows capture is CRLF and the Mac one is LF. Every \r before a newline is dropped
before hashing, so the comparison is about the simulation and nothing else.

    tickhash.py capture.log > ticks.sha256

Each output line is "<tick> <sha256>", where the hash covers every byte from that
tick's "TICK|n" line up to and including the engine's own "OBJDUMP-END" receipt. Nothing
after it is hashed, and that bound is load bearing too: the host prints its own chatter
after the first and last dump (a state readout whose buffer size carries the platform's
pointer width, 4080118 bytes on arm64 against 4014574 on i686), and with the block bounded
by the next TICK| line those two ticks "differed" on both captures of 3 Sep 2026 while the
19,998 between them agreed and the engine lines of tick 1 were byte identical by hand. The
engine's receipt is the end of the engine's state; the host's opinion of itself is not.

ONLY ENGINE LINES ARE HASHED, even inside a block. On Windows the host and the DLL do not
share one stdout buffer, so the host's chatter can surface anywhere in the stream, and it
did: the state readout after tick 1 appeared inside the blocks of ticks 2 to 10 on the
Windows capture. An engine line starts with an upper case tag followed by "|" (OBJ|,
HOUSE|, TIB|, WALL|, VIEW|, TICK|, ...) or with "OBJDUMP-"; everything else is the host
talking about itself and is dropped. Bytes before the first TICK| line are not hashed. A final line
"ALL <sha256>" hashes the concatenation of all per-tick hashes, so each platform's
capture reduces to one number to compare first, and only then the first differing tick.
"""
import hashlib, re, sys

ENGINE_LINE = re.compile(rb"^[A-Z][A-Z0-9_]*(\||-)")

def main(path):
    ticks = []
    cur = None
    buf = []
    with open(path, 'rb') as f:
        for line in f:
            line = line.replace(b"\r\n", b"\n")
            if line.startswith(b"TICK|"):
                if cur is not None:
                    ticks.append((cur, hashlib.sha256(b"".join(buf)).hexdigest()))
                cur = int(line[5:].strip() or 0)
                buf = [line]
            elif cur is not None:
                if not ENGINE_LINE.match(line):
                    continue
                buf.append(line)
                if line.startswith(b"OBJDUMP-END"):
                    ticks.append((cur, hashlib.sha256(b"".join(buf)).hexdigest()))
                    cur = None
    if cur is not None:
        ticks.append((cur, hashlib.sha256(b"".join(buf)).hexdigest()))
    allh = hashlib.sha256()
    for t, h in ticks:
        sys.stdout.write("%d %s\n" % (t, h))
        allh.update(h.encode())
    sys.stdout.write("ALL %s ticks=%d\n" % (allh.hexdigest(), len(ticks)))

if __name__ == "__main__":
    main(sys.argv[1])
