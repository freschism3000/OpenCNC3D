"""Pull the engine's own sound tables out of the vanilla source.

VocType names + priority + context come from tiberiandawn/audio.cpp SoundEffectName[].
VoxType names come from the same file's Speech[].
Theme names come from theme.cpp _themes[].
"""
import re, sys, json

import os as _os
# Repo root, resolved at runtime. CNC3D_ROOT overrides it for an out-of-tree checkout.
_ROOT = _os.environ.get("CNC3D_ROOT") or _os.path.abspath(
    _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", ".."))

SRC = _os.path.join(_ROOT, "brain", "vanilla", "tiberiandawn")

def strip_comments(s):
    s = re.sub(r'/\*.*?\*/', '', s, flags=re.S)
    s = re.sub(r'//[^\n]*', '', s)
    return s

def voc_table():
    txt = open(SRC + "/audio.cpp", encoding="latin-1").read()
    i = txt.index("SoundEffectName[VOC_COUNT] = {")
    j = txt.index("\n};", i)
    body = strip_comments(txt[i:j])
    out = []
    for m in re.finditer(r'\{\s*"([^"]+)"\s*,\s*(\d+)\s*,\s*(IN_\w+)\s*\}', body):
        out.append({"name": m.group(1), "priority": int(m.group(2)), "where": m.group(3)})
    return out

def vox_table():
    txt = open(SRC + "/audio.cpp", encoding="latin-1").read()
    i = txt.index("char const* Speech[VOX_COUNT] = {")
    j = txt.index("\n};", i)
    body = strip_comments(txt[i:j])
    return [m.group(1) for m in re.finditer(r'"([^"]+)"', body)]

def theme_table():
    txt = open(SRC + "/theme.cpp", encoding="latin-1").read()
    m = re.search(r'ThemeClass::ThemeControl\s+ThemeClass::_themes\[\s*THEME_COUNT\s*\]\s*=\s*\{', txt)
    if not m:
        m = re.search(r'_themes\[[^\]]*\]\s*=\s*\{', txt)
    i = m.end()
    j = txt.index("\n};", i)
    body = strip_comments(txt[i:j])
    out = []
    for e in re.finditer(r'\{\s*"([^"]+)"\s*,([^{}]*)\}', body):
        fields = [f.strip() for f in e.group(2).split(",")]
        out.append({"name": e.group(1), "fields": fields})
    return out

if __name__ == "__main__":
    d = {"voc": voc_table(), "vox": vox_table(), "themes": theme_table()}
    print(json.dumps(d, indent=1))
    sys.stderr.write("voc=%d vox=%d themes=%d\n" % (len(d["voc"]), len(d["vox"]), len(d["themes"])))
