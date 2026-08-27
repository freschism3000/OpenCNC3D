"""Work out which .V0x files the engine can ACTUALLY ask for.

Sound_Effect() picks the extension from the sign of `variation`:
  variation > 0  ->  .V01 / .V03      (InfantryClass passes  +(ID+1))
  variation < 0  ->  .V00 / .V02      (UnitClass and AircraftClass pass -(ID+1))

So an IN_VAR sound used only by infantry never needs .V00/.V02, and a sound used
only by vehicles never needs .V01/.V03. Auditing all four for every entry invents
missing files that the 1995 game never looked for.
"""
import re, os, sys, json

import os as _os
# Repo root, resolved at runtime. CNC3D_ROOT overrides it for an out-of-tree checkout.
_ROOT = _os.environ.get("CNC3D_ROOT") or _os.path.abspath(
    _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))
SRC = _os.path.join(_ROOT, "brain", "vanilla", "tiberiandawn")

def enum_order():
    txt = open(SRC + "/defines.h", encoding="latin-1").read()
    i = txt.index("VOC_NONE = -1"); j = txt.index("VOC_FIRST = 0", i)
    body = txt[i:j]
    body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
    body = re.sub(r'//[^\n]*', '', body)
    names, seen = [], set()
    for m in re.finditer(r'\b(VOC_[A-Z0-9_]+)\b', body):
        n = m.group(1)
        if n in ("VOC_NONE", "VOC_COUNT", "VOC_BUILD_SELECT", "VOC_FIRST"):
            continue
        if n in seen:
            continue
        seen.add(n); names.append(n)
    return {n: i for i, n in enumerate(names)}

def responses():
    """(class, function) -> list of VOC_ names, plus the sign of the variation."""
    out = {}
    for f, sign in (("infantry.cpp", +1), ("unit.cpp", -1), ("aircraft.cpp", -1)):
        txt = open(os.path.join(SRC, f), encoding="latin-1").read()
        for fn in ("Response_Select", "Response_Move", "Response_Attack"):
            m = re.search(r'^void\s+\w+Class::%s\(void\)\s*\{' % fn, txt, re.M)
            if not m:
                continue
            # take the function body up to the closing brace at column 0
            end = txt.index("\n}", m.end())
            body = txt[m.end():end]
            body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
            body = re.sub(r'//[^\n]*', '', body)
            vocs = set(re.findall(r'\b(VOC_[A-Z0-9_]+)\b', body))
            out.setdefault(sign, set()).update(vocs)
    return out

if __name__ == "__main__":
    order = enum_order()
    r = responses()
    use = {}
    for sign, vocs in r.items():
        for v in vocs:
            if v not in order:
                continue
            use.setdefault(order[v], 0)
            use[order[v]] |= (1 if sign > 0 else 2)
    print(json.dumps(use))
    sys.stderr.write("infantry(+): %d  vehicle(-): %d\n" % (len(r.get(1, [])), len(r.get(-1, []))))
