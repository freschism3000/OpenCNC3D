#!/usr/bin/env python3
"""Check that every movie and music file in the manifest can still be downloaded.

    python3 tools/media/probe-sources.py [--manifest FILE] [--full] [--jobs N]

FOR RELEASES. The manifest pins, for every file the launcher can download, a byte range
inside a disc image on somebody else's server and the SHA-256 those bytes must have.
Servers move, go down and occasionally change what they serve. This asks every mirror
for the first and last 64 KB of every file it is supposed to serve and compares them
with the windows recorded when the manifest was made (tools/media/media-windows.txt).

It also checks that launcher/lmedia_manifest.c carries exactly the text of
tools/media/media-manifest.txt, so the launcher cannot ship with a different list from
the one that was probed.

Exit status:
  0  every file has at least one mirror serving the right bytes
  1  at least one file has NO good mirror: a player ticking that box would get a failure
  2  the manifest, its windows file or the compiled copy are missing or disagree
  3  nothing was reachable at all (no network?)

A mirror that fails while another serves the file is reported but does not fail the
run: the launcher falls through to the next mirror by itself. --full fetches every file
WHOLE from every mirror and checks its SHA-256; that is about 2 GB and is for a
deliberate audit, not for every release.
"""
import argparse
import concurrent.futures
import hashlib
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import medialib as ml  # noqa: E402


def c_manifest_text(path):
    """The string LMEDIA_MANIFEST holds, decoded from the generated C source."""
    src = open(path, encoding="utf-8").read()
    body = src.split("LMEDIA_MANIFEST[] =", 1)[1]
    out = []
    for lit in re.findall(r'^\s*"((?:[^"\\]|\\.)*)"', body, re.M):
        out.append(lit.replace("\\?", "?").encode("ascii").decode("unicode_escape"))
    return "".join(out)


def load_windows(path):
    win = {}
    size = None
    for line in open(path, encoding="utf-8"):
        p = line.split()
        if not p or p[0].startswith("#"):
            continue
        if p[0] == "window":
            size = int(p[1])
        elif len(p) == 3:
            win[p[0]] = (p[1], p[2])
    return size, win


def check_one(cache, url, base, f, window, want, full):
    """A file's range on one mirror. A file with a head (Red Alert's score) serves only the
    bytes after its head from the disc, and its recorded windows are of that range."""
    fetch = ml.Fetcher(cache, use_cache=bool(cache))
    rng = f["size"] - len(f["head"])
    n = min(window, rng)
    start = base + f["offset"]
    try:
        if full:
            h = hashlib.sha256(f["head"])
            fetch._read_into(url, start, rng, h.update)
            ok = h.hexdigest() == f["sha256"]
            return ok, "" if ok else "whole-file SHA-256 differs"
        head = fetch.get(url, start, n)
        tail = fetch.get(url, start + rng - n, n)
    except ml.FetchError as e:
        return None, str(e)
    hs, ts = hashlib.sha256(head).hexdigest(), hashlib.sha256(tail).hexdigest()
    if (hs, ts) == want:
        return True, ""
    return False, "%s differ" % ("head and tail" if hs != want[0] and ts != want[1]
                                 else "head" if hs != want[0] else "tail")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--manifest", default=os.path.join(HERE, "media-manifest.txt"))
    ap.add_argument("--windows", default=os.path.join(HERE, "media-windows.txt"))
    ap.add_argument("--compiled", default=os.path.join(ml.REPO, "launcher",
                                                       "lmedia_manifest.c"),
                    help="the generated C copy to compare; an empty value skips it")
    ap.add_argument("--cache", default=None,
                    help="reuse earlier reads (off by default: a probe asks the live servers)")
    ap.add_argument("--full", action="store_true", help="fetch every file whole (slow)")
    ap.add_argument("--jobs", type=int, default=6)
    args = ap.parse_args()

    try:
        text = open(args.manifest, encoding="utf-8").read()
        man = ml.Manifest(text)
        window, win = load_windows(args.windows)
    except (OSError, ValueError) as e:
        print("PROBE|error|%s" % e)
        return 2
    if args.compiled:
        if not os.path.isfile(args.compiled):
            print("PROBE|error|%s is missing" % args.compiled)
            return 2
        if c_manifest_text(args.compiled) != text:
            print("PROBE|error|%s does not carry the text of %s; rerun "
                  "tools/media/make-media-manifest.py" % (args.compiled, args.manifest))
            return 2
        print("PROBE|compiled|%s matches" % os.path.relpath(args.compiled, ml.REPO))
    for f in man.files:
        if f["relpath"] not in win:
            print("PROBE|error|no recorded windows for %s" % f["relpath"])
            return 2

    # Mirrors first: one that cannot be opened at all is reported once, not per file.
    live = []
    for disc, url, base in man.mirrors:
        try:
            fetch = ml.Fetcher(None, attempts=3)
            pvd = fetch.get(url, base + ml.ISO_PVD, 6)
            if pvd != b"\x01CD001":
                raise ml.FetchError("no ISO9660 volume descriptor where the manifest "
                                    "says the image starts")
            live.append((disc, url, base))
            print("PROBE|mirror|%s|%s|open" % (disc, url))
        except ml.FetchError as e:
            print("PROBE|mirror|%s|%s|DOWN|%s" % (disc, url, e))
    if not live:
        print("PROBE|result|nothing reachable")
        return 3

    jobs = []
    for f in man.files:
        for disc, url, base in live:
            if disc == f["disc"]:
                jobs.append((f, disc, url, base))
    results = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as ex:
        futs = {ex.submit(check_one, args.cache, url, base, f, window, win[f["relpath"]],
                          args.full): (f["relpath"], url) for f, disc, url, base in jobs}
        for fut in concurrent.futures.as_completed(futs):
            results[futs[fut]] = fut.result()

    bad_files = []
    per_mirror = {}
    for f in man.files:
        good = 0
        for disc, url, base in live:
            if disc != f["disc"]:
                continue
            ok, why = results[(f["relpath"], url)]
            st = per_mirror.setdefault(url, [0, 0])
            st[1] += 1
            if ok:
                good += 1
                st[0] += 1
            else:
                print("PROBE|file|%s|%s|%s" % (f["relpath"], url,
                                               why if ok is False else "unreachable: " + why))
        if good == 0:
            bad_files.append(f["relpath"])
    for disc, url, base in live:
        ok, n = per_mirror.get(url, [0, 0])
        print("PROBE|mirror|%s|%s|%d/%d files good" % (disc, url, ok, n))
    for c in ("movies", "music"):
        sel = [f for f in man.files if f["component"] == c]
        nbad = sum(1 for f in sel if f["relpath"] in bad_files)
        print("PROBE|component|%s|%d files|%d bytes|%d without a good mirror"
              % (c, len(sel), sum(f["size"] for f in sel), nbad))
    if bad_files:
        for p in bad_files:
            print("PROBE|NO GOOD MIRROR|%s" % p)
        print("PROBE|result|FAIL|%d of %d files have no good mirror"
              % (len(bad_files), len(man.files)))
        return 1
    print("PROBE|result|ok|%d files, every one served by at least one mirror"
          % len(man.files))
    return 0


if __name__ == "__main__":
    sys.exit(main())
