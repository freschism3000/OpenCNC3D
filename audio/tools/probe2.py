import json, struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mixlib import Mix, mix_id

import os as _os
# Repo root, resolved at runtime. CNC3D_ROOT overrides it for an out-of-tree checkout.
_ROOT = _os.environ.get("CNC3D_ROOT") or _os.path.abspath(
    _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
DOS = _os.path.join(_ROOT, "data", "dosdata")
tables = json.load(open(os.path.dirname(os.path.abspath(__file__)) + "/tables.json"))
mixes = {m: Mix(os.path.join(DOS, m)) for m in ["SOUNDS.MIX","SPEECH.MIX","SCORES.MIX","AUD.MIX","ZOUNDS.MIX"]}
def audhdr(b):
    rate,size,uncomp,flags,comp = struct.unpack_from("<HIIBB", b, 0)
    return rate,size,uncomp,flags,comp
def probe(name):
    for m,mx in mixes.items():
        if mx.has(name): return m, mx.get(name)
    return None,None

print("=== compression type 1 / non-22050 / 8-bit files ===")
odd=[]
for e in tables["voc"]:
    exts = [".AUD"] if e["where"]=="IN_NOVAR" else [".AUD",".V00",".V01",".V02",".V03",".JUV"]
    for ext in exts:
        m,b = probe(e["name"]+ext)
        if not b: continue
        rate,size,unc,flags,comp = audhdr(b)
        if comp!=99 or rate!=22050 or (flags&2)==0 or (flags&1):
            odd.append((e["name"]+ext,m,rate,flags,comp,len(b),unc))
for o in odd: print("  %-14s %-11s %5dHz flags=%d comp=%-3d file=%7d uncomp=%8d" % o)

print("\n=== per-name missing detail ===")
for e in tables["voc"]:
    exts = [".AUD"] if e["where"]=="IN_NOVAR" else [".AUD",".V00",".V01",".V02",".V03",".JUV"]
    hits=[ext for ext in exts if probe(e["name"]+ext)[1]]
    if not hits: print("  MISS %-10s where=%s" % (e["name"], e["where"]))

print("\n=== ZOUNDS-only names ===")
z=[]
for e in tables["voc"]:
    for ext in [".AUD",".V00",".V01",".V02",".V03",".JUV"]:
        m,b=probe(e["name"]+ext)
        if m=="ZOUNDS.MIX": z.append(e["name"]+ext)
print(" ", len(z), z[:20])

print("\n=== AUD.MIX: what's inside (headers of raw entries) ===")
mx=mixes["AUD.MIX"]
for i,eid in enumerate(mx.order):
    b=mx.get_by_id(eid)
    rate,size,unc,flags,comp = audhdr(b)
    plaus = (rate in (11025,22050,22222,44100)) and comp in (1,99) and size+12<=len(b)+4
    print("  %2d id=0x%08X size=%8d  hdr rate=%6d comp=%3d flags=%d uncomp=%9d %s" % (i,eid,len(b),rate,comp,flags,unc,"AUD" if plaus else "?"))

print("\n=== SCORES.MIX unresolved ids ===")
mx=mixes["SCORES.MIX"]
known=set()
for t in tables["themes"]:
    for ext in (".AUD",".VAR"): known.add(mix_id(t["name"]+ext))
for eid in mx.order:
    if eid not in known:
        b=mx.get_by_id(eid); rate,size,unc,flags,comp=audhdr(b)
        print("  id=0x%08X size=%d rate=%d comp=%d flags=%d" % (eid,len(b),rate,comp,flags))

print("\n=== SOUNDS/ZOUNDS unresolved ids ===")
for mn in ("SOUNDS.MIX","ZOUNDS.MIX"):
    mx=mixes[mn]; known=set()
    for e in tables["voc"]:
        for ext in [".AUD",".V00",".V01",".V02",".V03",".JUV",".VAR"]: known.add(mix_id(e["name"]+ext))
    for n in tables["vox"]: known.add(mix_id(n+".AUD"))
    un=[eid for eid in mx.order if eid not in known]
    print(" %s: %d unresolved" % (mn,len(un)))
    for eid in un:
        b=mx.get_by_id(eid); rate,size,unc,flags,comp=audhdr(b)
        print("   id=0x%08X size=%7d rate=%6d comp=%3d flags=%d" % (eid,len(b),rate,comp,flags))
