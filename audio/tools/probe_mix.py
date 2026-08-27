"""What is actually inside the DOS audio MIXes, and what do the AUD headers say."""
import json, struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mixlib import Mix, mix_id

import os as _os
# Repo root, resolved at runtime. CNC3D_ROOT overrides it for an out-of-tree checkout.
_ROOT = _os.environ.get("CNC3D_ROOT") or _os.path.abspath(
    _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))

DOS = _os.path.join(_ROOT, "data", "dosdata")
MIXES = ["SOUNDS.MIX", "SPEECH.MIX", "SCORES.MIX", "AUD.MIX", "ZOUNDS.MIX"]
tables = json.load(open(os.path.dirname(os.path.abspath(__file__)) + "/tables.json"))

EXTS = [".AUD", ".V00", ".V01", ".V02", ".V03", ".JUV", ".VAR"]

def audhdr(b):
    if len(b) < 12: return None
    rate, size, uncomp, flags, comp = struct.unpack_from("<HIIBB", b, 0)
    return dict(rate=rate, size=size, uncomp=uncomp, flags=flags, comp=comp,
                ch=2 if flags & 1 else 1, bits=16 if flags & 2 else 8, filesize=len(b))

mixes = {}
for m in MIXES:
    p = os.path.join(DOS, m)
    if not os.path.exists(p):
        print("MISSING", m); continue
    mx = Mix(p)
    mixes[m] = mx
    print("%-12s %5d entries  data %d" % (m, mx.count, mx.datasize))

# how many entries resolve at all
resolved = {m: set() for m in mixes}
found = {}   # name -> (mix, hdr)
def probe(name):
    for m, mx in mixes.items():
        if mx.has(name):
            b = mx.get(name)
            resolved[m].add(mix_id(name))
            return m, b
    return None, None

print("\n--- VOC (sound effects) ---")
comp_hist = {}
miss = []
for e in tables["voc"]:
    hit = False
    for ext in ([".AUD"] if e["where"] == "IN_NOVAR" else [".AUD", ".V00", ".V01", ".V02", ".V03", ".JUV"]):
        m, b = probe(e["name"] + ext)
        if b:
            h = audhdr(b)
            key = (m, h["rate"], h["ch"], h["bits"], h["comp"])
            comp_hist[key] = comp_hist.get(key, 0) + 1
            found[e["name"] + ext] = (m, h)
            hit = True
    if not hit:
        miss.append(e["name"])
print("resolved %d file variants; missing base names: %d %s" % (len(found), len(miss), miss))
for k, v in sorted(comp_hist.items()):
    print("   %-12s %5d Hz %dch %2dbit comp=%-3d  x%d" % (k[0], k[1], k[2], k[3], k[4], v))

print("\n--- VOX (speech) ---")
comp_hist = {}; miss = []
for n in tables["vox"]:
    m, b = probe(n + ".AUD")
    if b:
        h = audhdr(b)
        key = (m, h["rate"], h["ch"], h["bits"], h["comp"])
        comp_hist[key] = comp_hist.get(key, 0) + 1
    else:
        miss.append(n)
print("missing: %d %s" % (len(miss), miss))
for k, v in sorted(comp_hist.items()):
    print("   %-12s %5d Hz %dch %2dbit comp=%-3d  x%d" % (k[0], k[1], k[2], k[3], k[4], v))

print("\n--- THEMES (music) ---")
comp_hist = {}; miss = []
for t in tables["themes"]:
    hit = False
    for ext in (".AUD", ".VAR"):
        m, b = probe(t["name"] + ext)
        if b:
            h = audhdr(b)
            key = (m, h["rate"], h["ch"], h["bits"], h["comp"])
            comp_hist[key] = comp_hist.get(key, 0) + 1
            hit = True
    if not hit: miss.append(t["name"])
print("missing: %d %s" % (len(miss), miss))
for k, v in sorted(comp_hist.items()):
    print("   %-12s %5d Hz %dch %2dbit comp=%-3d  x%d" % (k[0], k[1], k[2], k[3], k[4], v))

print("\n--- unresolved entries per mix ---")
for m, mx in mixes.items():
    print("%-12s %d/%d ids resolved by name" % (m, len(resolved[m]), mx.count))
