#!/usr/bin/env python3
"""Cross-brain parity comparator (docs/design-xl-brain.md, determinism (a)).

Two captures of the SAME scripted session -- one on the classic dylib, one on
TiberianDawnXL -- must tell the same story line for line. The only honest
differences are the flat cell NUMBERS, which encode (x, y) against each
brain's stride (classic 128, XL 1024). This tool extracts the simulation-
bearing lines from each capture, decodes every stride-derived field back to
per-axis form using that capture's stride, and diffs the result. First
divergence wins: it reports the line index, the last tick context seen, and
for OBJ| lines the exact field.

    compare.py A.log STRIDE_A B.log STRIDE_B

Exit 0 on parity, 1 on divergence, 2 on a malformed capture.

Lines compared (everything else in the capture is host chatter and ignored):
  OBJ|    per-object rows; newcell/centercell/nav/tar decoded via stride,
          inicell dropped outright (its formula is convention, not geometry,
          and is currently in flight on the vanilla side)
  CARGO|, TIB|, WALL|, SMUDGE|, HOUSE|   already per-axis or axis-free
  TICK|   the script's own tick receipts (objects=, frame=, selection=)
  SOUND|  brain-emitted audio events when the run used --dumpsound
  OBJDUMP-END|  the dump's own frame/total receipt
"""
import sys

PREFIXES = ("OBJ|", "CARGO|", "TIB|", "WALL|", "SMUDGE|", "HOUSE|",
            "TICK|", "SOUND|", "OBJDUMP-END")
CELL_FIELDS = ("newcell", "centercell", "nav", "tar")
DROP_FIELDS = ("inicell",)


def decode_cell(val, stride):
    try:
        c = int(val)
    except ValueError:
        return val
    if c < 0:
        return str(c)
    return "%d,%d" % (c % stride, c // stride)


SENTINEL_POS_FIELDS = ("newcell", "centercell", "x", "y", "lx", "ly",
                       "clx", "cly", "tcx", "tcy")


def normalize(line, stride):
    if not line.startswith("OBJ|"):
        return line
    parts = line.split("|")
    fields = dict(p.split("=", 1) for p in parts if "=" in p)
    # The never-placed object's ctor coordinate is a bogus all-ones sentinel
    # (object.cpp: "Some bogus illegal value") whose bit width follows the
    # axis width, so it prints 65535/65535 on classic and -1/-1 on XL. The
    # dump's own commentary declares a limbo object's position meaningless;
    # collapse the sentinel to one spelling so it cannot masquerade as a
    # position divergence. Real positions (limbo'd passengers included)
    # still compare exactly.
    sentinel = (fields.get("limbo") == "1"
                and fields.get("lx") in ("-1", "65535")
                and fields.get("ly") in ("-1", "65535")
                and fields.get("mission") in ("None", "-"))
    out = []
    for p in parts:
        if "=" in p:
            k, v = p.split("=", 1)
            if k in DROP_FIELDS:
                continue
            if sentinel and k in SENTINEL_POS_FIELDS:
                p = "%s=SENTINEL" % k
            elif k in CELL_FIELDS:
                p = "%s=%s" % (k, decode_cell(v, stride))
        out.append(p)
    return "|".join(out)


def load(path, stride):
    rows = []
    with open(path, errors="replace") as f:
        for raw in f:
            line = raw.rstrip("\r\n")
            if line.startswith(PREFIXES):
                rows.append(normalize(line, stride))
    return rows


def main():
    if len(sys.argv) != 5:
        sys.stderr.write(__doc__)
        return 2
    a_path, a_stride, b_path, b_stride = (
        sys.argv[1], int(sys.argv[2]), sys.argv[3], int(sys.argv[4]))
    a = load(a_path, a_stride)
    b = load(b_path, b_stride)
    if not a or not b:
        print("PARITY|MALFORMED|%s has %d sim lines, %s has %d"
              % (a_path, len(a), b_path, len(b)))
        return 2

    tick_ctx = "before first TICK|"
    for i in range(min(len(a), len(b))):
        if a[i].startswith(("TICK|", "OBJDUMP-END")) and a[i] == b[i]:
            tick_ctx = a[i]
        if a[i] != b[i]:
            print("PARITY|DIVERGED|line %d of the sim stream|after [%s]" % (i, tick_ctx))
            print("  A(%s): %s" % (a_path, a[i]))
            print("  B(%s): %s" % (b_path, b[i]))
            if a[i].startswith("OBJ|") and b[i].startswith("OBJ|"):
                fa = dict(p.split("=", 1) for p in a[i].split("|") if "=" in p)
                fb = dict(p.split("=", 1) for p in b[i].split("|") if "=" in p)
                for k in sorted(set(fa) | set(fb)):
                    if fa.get(k) != fb.get(k):
                        print("  field %s: A=%s B=%s" % (k, fa.get(k), fb.get(k)))
            return 1
    if len(a) != len(b):
        print("PARITY|DIVERGED|stream lengths differ: A=%d B=%d|after [%s]"
              % (len(a), len(b), tick_ctx))
        for j in range(min(len(a), len(b)), min(len(a), len(b)) + 3):
            if j < len(a):
                print("  A extra: %s" % a[j])
            if j < len(b):
                print("  B extra: %s" % b[j])
        return 1
    print("PARITY|OK|%d sim lines identical after stride normalization" % len(a))
    return 0


if __name__ == "__main__":
    sys.exit(main())
