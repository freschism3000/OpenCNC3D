#!/usr/bin/env python3
"""The sizes Setup prints beside its Movies and Music boxes, read from the media manifest.

    media-sizes.py <manifest> [--exe OpenCNC3D.exe] [--module ra3d]

<manifest> is either launcher/lmedia_manifest.c, the generated C file that compiles the
manifest into the launcher, or the manifest as plain text. Prints shell assignments
(MOVIES_BYTES, MOVIES_KB, MOVIES_SIZE, MOVIES_FILES and the same four for MUSIC) that
tools/win/make-installer-win.sh evaluates and hands to makensis.

THE NUMBERS ON THE COMPONENTS PAGE ARE THE LAUNCHER'S OWN. Setup only draws the boxes;
the launcher it installs does the downloading, from the manifest compiled into it. A size
typed into the installer script would be a second copy of that manifest, and the first
time the movie list changed the page would promise one figure and the bar would count to
another. So the totals are summed here from the same text, and with --exe the whole
manifest text is required to appear, byte for byte, inside the launcher being packaged:
proof that the boxes and the downloads come from one manifest.

The size format is the launcher's (lm_format_size): binary megabytes and gigabytes with
one decimal, labelled MB and GB, which is also how NSIS itself counts the disk space it
reports on the directory page, so the three figures agree.

A MODULE'S MOVIES AND SCORE ARE COUNTED ONLY WITH --module. The launcher fetches the
files under a `module` line's folder only where that module is installed, so the figure
beside each box is the game's own files plus those of every module the package Setup
installs carries, which tools/win/make-installer-win.sh knows from the staged package and
passes as --module.

Manifest records, one per line, '#' starting a comment:
    OCMEDIA 1
    mirror <DISC> <url> <base>
    module <folder>
    file <component> <relpath> <size> <sha256hex> <DISC> <offset> [<head hex>]
    legacy <component> <relpath>
"""
import ast
import re
import sys

COMPONENTS = ("movies", "music")
DISCS = ("GDI", "NOD", "COVERT", "RA_ALLIED", "RA_SOVIET")
MODULES = ("ra3d",)
USAGE = "usage: media-sizes.py <manifest .c or .txt> [--exe OpenCNC3D.exe] [--module ra3d]"


def die(msg):
    sys.stderr.write("media-sizes: %s\n" % msg)
    sys.exit(1)


def manifest_from_c(src):
    """The text of LMEDIA_MANIFEST, from the C file that defines it.

    Adjacent string literals are one string in C, so every literal in the initializer is
    joined in order. The escapes a generator writes (\\n, \\", \\\\, \\t) mean the same in
    Python, which is what decodes them.
    """
    m = re.search(r"LMEDIA_MANIFEST\s*\[\s*\]\s*=", src)
    if not m:
        die("no 'LMEDIA_MANIFEST[] =' definition in the C file")
    rest = src[m.end():]
    # One left-to-right scan, because comments and literals can each contain what starts
    # the other: every mirror URL has a "//" in it, and a comment may quote a string.
    tok = re.compile(r'"((?:[^"\\\n]|\\.)*)"|/\*.*?\*/|//[^\n]*|;', re.S)
    parts = []
    for t in tok.finditer(rest):
        if t.group(0) == ";":
            break
        if t.group(0).startswith('"'):
            parts.append(t.group(1))
    else:
        die("the LMEDIA_MANIFEST definition never ends")
    if not parts:
        die("LMEDIA_MANIFEST has no string literal in it")
    try:
        return "".join(ast.literal_eval('"%s"' % p) for p in parts)
    except (ValueError, SyntaxError) as e:
        die("a string literal in LMEDIA_MANIFEST does not decode: %s" % e)


def fmt_size(n):
    if n >= 1024 * 1024 * 1024:
        return "%.1f GB" % (n / (1024.0 * 1024 * 1024))
    return "%.1f MB" % (n / (1024.0 * 1024))


def main(argv):
    if len(argv) < 2:
        die(USAGE)
    path = argv[1]
    exe = None
    with_modules = set()
    rest = argv[2:]
    while rest:
        if len(rest) >= 2 and rest[0] == "--exe":
            exe = rest[1]
        elif len(rest) >= 2 and rest[0] == "--module" and rest[1] in MODULES:
            with_modules.add(rest[1])
        else:
            die(USAGE)
        rest = rest[2:]

    try:
        with open(path, "rb") as f:
            raw = f.read()
    except OSError as e:
        die("cannot read %s: %s" % (path, e))
    try:
        src = raw.decode("utf-8")
    except UnicodeDecodeError:
        die("%s is not UTF-8" % path)
    text = manifest_from_c(src) if path.endswith(".c") else src

    lines = [ln.split("#", 1)[0].strip() for ln in text.splitlines()]
    lines = [ln for ln in lines if ln]
    if not lines or lines[0].split() != ["OCMEDIA", "1"]:
        die("the manifest does not start with 'OCMEDIA 1'")

    mirrors = set()
    total = dict((c, 0) for c in COMPONENTS)
    count = dict((c, 0) for c in COMPONENTS)
    seen = set()
    used_discs = set()
    modules = set()
    files = []
    for ln in lines[1:]:
        f = ln.split()
        kind = f[0]
        if kind == "mirror":
            if len(f) != 4 or f[1] not in DISCS or not f[3].isdigit():
                die("bad mirror record: %s" % ln)
            mirrors.add(f[1])
        elif kind == "module":
            if len(f) != 2 or f[1] not in MODULES:
                die("bad module record: %s" % ln)
            modules.add(f[1])
        elif kind == "file":
            if len(f) not in (7, 8):
                die("a file record needs 7 fields, or 8 with a head: %s" % ln)
            _, comp, rel, size, sha, disc, off = f[:7]
            if len(f) == 8 and not re.fullmatch(r"(?:[0-9a-f]{2})+", f[7]):
                die("bad head: %s" % ln)
            if comp not in COMPONENTS:
                die("unknown component '%s': %s" % (comp, ln))
            if not size.isdigit() or int(size) <= 0 or not off.isdigit():
                die("bad size or offset: %s" % ln)
            if not re.fullmatch(r"[0-9a-fA-F]{64}", sha):
                die("bad sha256: %s" % ln)
            if disc not in DISCS:
                die("unknown disc '%s': %s" % (disc, ln))
            if rel.lower() in seen:
                die("%s is listed twice" % rel)
            seen.add(rel.lower())
            used_discs.add(disc)
            files.append((comp, rel, int(size)))
        elif kind == "legacy":
            if len(f) != 3 or f[1] not in COMPONENTS:
                die("bad legacy record: %s" % ln)
        else:
            die("unknown record '%s'" % kind)

    for m in with_modules - modules:
        die("--module %s, and the manifest has no module line for it" % m)
    for comp, rel, size in files:
        top = rel.split("/", 1)[0] if "/" in rel else ""
        if top in MODULES and top not in modules:
            die("%s is in the %s folder and the manifest names no module line for it"
                % (rel, top))
        if top in modules and top not in with_modules:
            continue
        total[comp] += size
        count[comp] += 1
    for c in COMPONENTS:
        if count[c] == 0:
            die("the manifest lists no %s files" % c)
    for d in sorted(used_discs - mirrors):
        die("files come from the %s disc and no mirror serves it" % d)

    if exe:
        try:
            with open(exe, "rb") as f:
                blob = f.read()
        except OSError as e:
            die("cannot read %s: %s" % (exe, e))
        if text.encode("utf-8") not in blob:
            die("%s does not carry this manifest: the launcher was built from a "
                "different one, so the sizes on Setup's page would not be what it "
                "downloads" % exe)

    for c in COMPONENTS:
        up = c.upper()
        print("%s_BYTES=%d" % (up, total[c]))
        print("%s_KB=%d" % (up, (total[c] + 1023) // 1024))
        print("%s_SIZE='%s'" % (up, fmt_size(total[c])))
        print("%s_FILES=%d" % (up, count[c]))


if __name__ == "__main__":
    main(sys.argv)
