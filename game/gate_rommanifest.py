#!/usr/bin/env python3
"""G180 -- the ROM manifest is the archive parser's own output, at the base the parser
computes for itself.

Why this gate exists. docs/rom-manifest.csv was written by an early build of the archive
parser that carried the guessed data base 0x470000. The real base is the byte right after
the last directory record, dir_start + 24*nrecs = 0x0460540 + 24*1422 = 0x468A90, so every
address the manifest published was 0x7570 too high: 1538 rows in the base and rom_addr
columns, and the 86 trailing-table rows had no base at all. A wrong base does not look
wrong. Files tile contiguously, so it lands mid-stream in a NEIGHBOURING FILE OF THE SAME
TYPE and still emits plausible text and plausible pictures. The only oracle that catches it
is exact length, which is what leg 1 uses.

The four legs:
  1. the parser's own self-test on the cartridge: every record tiles and every codec
     decodes to its recorded length, at the base the parser uses;
  2. the base constant still equals the RULE, recomputed live off the ROM, rather than
     having drifted back to a literal somebody typed;
  3. docs/rom-manifest.csv is byte-identical to a fresh regeneration, so the published
     manifest can never again be a stale snapshot of a superseded parser;
  4. no raw extraction mirror in the tree holds bytes cut at the wrong base. This leg does
     not care which remedy was chosen for such a mirror -- deleted, or re-cut correctly --
     only that no wrong-base bytes are sitting in the tree pretending to be assets.

Prints one machine-readable line and exits non-zero on any failure.
Read-only on the ROM and on the tree.
"""
import contextlib
import io
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def find_root(argv):
    """Repo root: argv[1] if given, else the directory above this script."""
    cands = []
    if len(argv) > 1:
        cands.append(os.path.abspath(argv[1]))
    cands.append(os.path.dirname(HERE))
    for c in cands:
        if os.path.isfile(os.path.join(c, "tools", "romdump", "archive.py")):
            return c
    return None


def main():
    root = find_root(sys.argv)
    if root is None:
        print("ROMMANIFEST|verdict=NOROOT|looked=%s" % ";".join(
            [os.path.abspath(sys.argv[1])] if len(sys.argv) > 1 else [] or [os.path.dirname(HERE)]))
        return 1
    rd = os.path.join(root, "tools", "romdump")
    rom = os.path.join(root, "data", "rom", "cnc_eu.z64")
    man = os.path.join(root, "docs", "rom-manifest.csv")
    for p in (rom, man):
        if not os.path.isfile(p):
            print("ROMMANIFEST|verdict=MISSING|path=%s" % os.path.relpath(p, root))
            return 1

    sys.path.insert(0, rd)
    import archive  # the one implementation

    d = archive.load(rom)

    # ---- leg 1: the parser's own self-test, at the base the parser uses ----------------
    chk = subprocess.run([sys.executable, os.path.join(rd, "archive.py"), rom, "check"],
                         capture_output=True, text=True)
    selftest = "OK" if (chk.returncode == 0 and "OK, every record tiles and decodes"
                        in chk.stdout) else "PROBLEMS"

    # ---- leg 2: the base constant still equals the rule, recomputed off the ROM --------
    MAIN = 0x0460540
    nrecs = len(archive.parse_dir(d, MAIN))
    rule = MAIN + 24 * nrecs
    base_ok = (rule == archive.GROUP_BASE == 0x468A90)
    # the two trailing tables answer to the same rule
    trail_ok = all(archive.TRAILING_BASES[s] == s + 24 * len(archive.parse_dir(d, s))
                   for s in archive.TRAILING_BASES)

    # ---- leg 3: the published manifest is a fresh regeneration -------------------------
    tmp = tempfile.NamedTemporaryFile(suffix=".csv", delete=False)
    tmp.close()
    try:
        quiet = io.StringIO()          # cmd_manifest chatters; this gate prints one line
        with contextlib.redirect_stdout(quiet):
            archive.cmd_manifest(d, tmp.name)
        fresh = open(tmp.name, "rb").read()
    finally:
        os.unlink(tmp.name)
    published = open(man, "rb").read()
    man_ok = (fresh == published)
    man_rows = published.count(b"\n") - 1

    # ---- leg 4: no raw mirror in the tree holds wrong-base bytes -----------------------
    WRONG = 0x470000
    recs_by_dir = {}
    for a in archive.all_archives(d):
        recs_by_dir[a["dir"]] = (a["base"], {r["name"]: r for r in a["recs"]})
    # Every table shares one data base, so a sub-range table resolves to the same base;
    # a mirror folder is named dir_<hex of the directory table it was cut from>.
    stale = 0
    stale_first = ""
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [x for x in dirnames if x not in (".git", "node_modules", "build")]
        head = os.path.basename(dirpath)
        if not head.startswith("dir_"):
            continue
        try:
            start = int(head[4:], 16)
        except ValueError:
            continue
        recs = archive.parse_dir(d, start)
        if not recs:
            continue
        base = archive.TRAILING_BASES.get(start, start + 24 * len(recs))
        byname = {r["name"]: r for r in recs}
        for fn in filenames:
            r = byname.get(fn)
            if r is None:
                continue
            size = r["csize"] if r["method"] else r["usize"]
            if size == 0:
                continue          # a zero-length stub is the same at any base
            blob = open(os.path.join(dirpath, fn), "rb").read()
            if blob == d[base + r["off"]: base + r["off"] + size]:
                continue          # cut at the true base
            if blob == d[WRONG + r["off"]: WRONG + r["off"] + size]:
                stale += 1
                if not stale_first:
                    stale_first = os.path.relpath(os.path.join(dirpath, fn), root)

    print("ROMMANIFEST|selftest=%s|nrecs=%d|rule=0x%07X|const=0x%07X|trailing=%s"
          "|manifest=%s|rows=%d|wrongbase_files=%d|first=%s"
          % (selftest, nrecs, rule, archive.GROUP_BASE, "OK" if trail_ok else "DRIFTED",
             "MATCHES" if man_ok else "STALE", man_rows, stale, stale_first or "-"))
    return 0 if (selftest == "OK" and base_ok and trail_ok and man_ok and stale == 0) else 1


if __name__ == "__main__":
    sys.exit(main())
