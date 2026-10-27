#!/usr/bin/env python3
"""Make the language packs' download manifest from the localised retail disc images.

    python3 tools/media/make-language-manifest.py [--cache DIR] [--only LANG] [--verbose]

WHAT IT MAKES. A language other than English is a pack the game downloads when the player
picks it in Settings: the language's own string table, briefings, announcer, unit voices
and dubbed movies, taken straight out of that language's 1995 or 1996 disc images by HTTP
byte range, as the movies and the score are (make-media-manifest.py). This tool works out,
for every file of every pack, which disc holds it, at which byte, how big it is and what
its SHA-256 must be, and writes it down twice:

    tools/media/language-manifest.txt   the manifest as text, for people
    game/lang_manifest.h                the same text compiled into the game

WHAT A PACK HOLDS, and what it leaves English (the choices the packs were made to):

  - the string table (CONQUER.ENG, which every language's disc still names .ENG) and the
    briefings (MISSION.INI), as loose files;
  - the announcer's archive (SPEECH.MIX) and the unit voices' archive (SOUNDS.MIX) whole.
    The German SOUNDS.MIX also carries the death sounds its censored release replaced;
    the game takes only the voices from it (lang_sound_wanted in game/lang_mod.h), so the
    soldiers still die with the 1995 English screams;
  - every dubbed movie: a movie whose length matches the English one is the same picture
    with the language's soundtrack. A movie of another length was cut or edited for that
    release, and is left out, so the game plays the English one there;
  - never the sprites: the German infantry are the censored cyborgs, and the English
    soldiers stay.

THE SOURCES are retail discs uploaded to archive.org, not EA's freeware: each disc is
listed with every mirror found to be byte-identical (same MD5), and mirrors of one disc
must agree on every file's place.
"""
import argparse
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import medialib as ml  # noqa: E402

REPO = ml.REPO
ARCHIVE = "https://archive.org/download/"

# The discs: name, game, then every byte-identical copy (URL, base).
DISCS = {
    "DE_GDI": ("td", [
        ARCHIVE + "command-conquer-der-tiberiumkonflikt-incl-addon-der-ausnahmezustand/"
                  "Image/Der%20Tiberiumkonflikt%20DOS/GDI.iso",
        ARCHIVE + "Command_And_Conquer_Teil_1_-_Der_Tiberiumkonflikt_1995_Virgin_DE/"
                  "Command%20%26%20Conquer%20Teil%201%20-%20Der%20Tiberiumkonflikt%20%281995%29"
                  "%28Virgin%29%28DE%29%28Disc%201%20of%202%29.iso",
    ]),
    "DE_NOD": ("td", [
        ARCHIVE + "command-conquer-der-tiberiumkonflikt-incl-addon-der-ausnahmezustand/"
                  "Image/Der%20Tiberiumkonflikt%20DOS/NOD.iso",
        ARCHIVE + "Command_And_Conquer_Teil_1_-_Der_Tiberiumkonflikt_1995_Virgin_DE/"
                  "Command%20%26%20Conquer%20Teil%201%20-%20Der%20Tiberiumkonflikt%20%281995%29"
                  "%28Virgin%29%28DE%29%28Disc%202%20of%202%29.iso",
    ]),
    "DE_RA_ALLIED": ("ra", [
        ARCHIVE + "command-conquer-alarmstufe-rot-incl-addon_202202/"
                  "Image/Alarmstufe%20Rot%20%28Virgin%20Interactive%29/Alliierten.iso",
        ARCHIVE + "Command_And_Conquer_Teil_2_-_Alarmstufe_Rot_1996_Virgin_DE/"
                  "Command%20%26%20Conquer%20Teil%202%20-%20Alarmstufe%20Rot%20%281996%29"
                  "%28Virgin%29%28DE%29%28Disc%201%20of%202%29.iso",
    ]),
    "DE_RA_SOVIET": ("ra", [
        ARCHIVE + "command-conquer-alarmstufe-rot-incl-addon_202202/"
                  "Image/Alarmstufe%20Rot%20%28Virgin%20Interactive%29/Sowjetunion.iso",
        ARCHIVE + "Command_And_Conquer_Teil_2_-_Alarmstufe_Rot_1996_Virgin_DE/"
                  "Command%20%26%20Conquer%20Teil%202%20-%20Alarmstufe%20Rot%20%281996%29"
                  "%28Virgin%29%28DE%29%28Disc%202%20of%202%29.iso",
    ]),
    # (the only French Red Alert image found that can be read by range is the Soviet disc)
    "FR_RA_SOVIET": ("ra", [
        ARCHIVE + "Westwood_CommandAndConquer_Alerte_Rouge_French_Version_MSDOS_Win95_1996/"
                  "Westwood-CommandAndConquer-AlerteRouge-FrenchVersion-MSDOSWin95.iso",
    ]),
}
# The packs: language code, game, the name the dropdown shows, its discs (the first holds
# the text and voices; every one is searched for movies).
PACKS = [
    ("de", "td", "Deutsch", ("DE_GDI", "DE_NOD")),
    ("de", "ra", "Deutsch", ("DE_RA_ALLIED", "DE_RA_SOVIET")),
    ("fr", "ra", "Francais", ("FR_RA_SOVIET",)),
]
RA_DATA = os.path.join(REPO, "data", "radata")
# Tiberian Dawn's English development copy, against which a movie is judged dubbed (same
# length) or cut (any other length).
TD_DATA = os.path.join(REPO, "data", "dosdata")


def say(*a):
    print(*a, flush=True)


def open_disc(fetch, disc):
    """Every mirror of a disc, opened and walked; they must agree on every file."""
    imgs = []
    for url in DISCS[disc][1]:
        img = ml.IsoImage(fetch, url, 0)
        img.walk()
        imgs.append(img)
    ref = imgs[0]
    for img in imgs[1:]:
        if img.files != ref.files:
            raise SystemExit("%s: the mirrors disagree on the disc's layout (%s, %s)"
                             % (disc, ref.url, img.url))
    return imgs


def one_file(img, name):
    hits = img.files.get(name.upper(), [])
    if len(hits) != 1:
        raise SystemExit("%s: %d files named %s on %s" % (img.url, len(hits), name, img.volume))
    return hits[0]


def hashed(fetch, img, offset, size):
    """The SHA-256 of bytes [offset, offset+size) of a disc, streamed and not kept."""
    h = hashlib.sha256()
    step = 4 << 20
    at = 0
    while at < size:
        n = min(step, size - at)
        h.update(img.read(offset + at, n))
        at += n
    return h.hexdigest()


def td_pack(fetch, code, discs, lines, verbose):
    imgs = {d: open_disc(fetch, d) for d in discs}
    first = imgs[discs[0]][0]
    root = "dosdata/lang/%s/" % code
    files = []                      # (relpath, size, disc, offset)
    # the string table and the briefings, loose, out of their archives
    for archive, entry in (("LOCAL.MIX", "CONQUER.ENG"), ("GENERAL.MIX", "MISSION.INI")):
        path, lba, fsize = one_file(first, archive)
        mix = ml.MixIndex(first, lba, fsize, archive)
        hit = mix.find(entry)
        if not hit:
            raise SystemExit("%s has no %s" % (archive, entry))
        files.append((root + entry, hit[1], discs[0], hit[0]))
    # the announcer and the unit voices, whole: the game's own reader must take them
    for archive in ("SPEECH.MIX", "SOUNDS.MIX"):
        path, lba, fsize = one_file(first, archive)
        mix = ml.MixIndex(first, lba, fsize, archive)
        if not mix.plain:
            raise SystemExit("%s on %s is not a plain MIX the game can read" % (archive, discs[0]))
        files.append((root + archive, fsize, discs[0], lba * ml.ISO_BLOCK))
    # the dubbed movies
    english = sorted(n[:-4] for n in os.listdir(os.path.join(TD_DATA, "movies"))
                     if n.upper().endswith(".VQA"))
    kept, cut, missing = [], [], []
    for name in english:
        want = os.path.getsize(os.path.join(TD_DATA, "movies", name + ".VQA"))
        found = None
        for d in discs:
            img = imgs[d][0]
            if "MOVIES.MIX" not in img.files:
                continue
            path, lba, fsize = one_file(img, "MOVIES.MIX")
            hit = ml.MixIndex(img, lba, fsize, d + " MOVIES.MIX").find(name + ".VQA")
            if hit:
                found = (d, hit)
                break
        if not found:
            missing.append(name)
        elif found[1][1] != want:
            cut.append("%s (%d against %d)" % (name, found[1][1], want))
        else:
            kept.append(name)
            files.append((root + "movies/%s.VQA" % name, found[1][1], found[0], found[1][0]))
    say("%s td: %d dubbed movies, %d cut and left English: %s" % (code, len(kept), len(cut),
                                                                 ", ".join(cut)))
    if missing:
        say("%s td: not on these discs, left English: %s" % (code, ", ".join(missing)))
    total = 0
    for rel, size, disc, off in files:
        img = imgs[disc][0]
        digest = hashed(fetch, img, off, size)
        # every other mirror must hold the same bytes at the same place
        for other in imgs[disc][1:]:
            if other.read(off, min(size, 65536)) != img.read(off, min(size, 65536)):
                raise SystemExit("%s: mirror %s differs at %s" % (disc, other.url, rel))
        lines.append("file %s td %s %d %s %s %d" % (code, rel, size, digest, disc, off))
        total += size
        if verbose:
            say("  %s %d" % (rel, size))
    return total, len(files)


def ra_pack(fetch, code, discs, lines, verbose):
    """Red Alert's pack. Its archives are entries of MAIN.MIX and of INSTALL/REDALERT.MIX,
    with encrypted indexes; the game's audio reader takes plain ones, so an archive is its
    index rewritten plain (the line's head) followed by its data block as it is on the disc,
    the way the Red Alert score is fetched (make-media-manifest.py)."""
    imgs = {d: open_disc(fetch, d) for d in discs}
    root = "ra3d/lang/%s/" % code

    def tops(img):
        out = []
        for name in ("MAIN.MIX", "REDALERT.MIX"):
            for path, lba, fsize in img.files.get(name, []):
                out.append(ml.RaMixIndex(img, lba * ml.ISO_BLOCK, fsize, path))
        return out

    def archive(name):
        for d in discs:
            img = imgs[d][0]
            for top in tops(img):
                hit = top.find(name)
                if hit:
                    return d, img, ml.RaMixIndex(img, hit[0], hit[1], "%s:%s" % (top.label, name))
        return None

    files = []                      # (relpath, size, disc, offset, head bytes)
    for arch, entry in (("LOCAL.MIX", "CONQUER.ENG"), ("GENERAL.MIX", "MISSION.INI")):
        a = archive(arch)
        hit = a and a[2].find(entry)
        if not hit:
            raise SystemExit("%s %s: no %s:%s" % (code, discs, arch, entry))
        files.append((root + entry, hit[1], a[0], hit[0], b""))
    for arch in ("SPEECH.MIX", "SOUNDS.MIX", "ALLIES.MIX", "RUSSIAN.MIX"):
        a = archive(arch)
        if not a:
            say("%s ra: no %s on these discs, left English" % (code, arch))
            continue
        d, img, idx = a
        head = idx.plain_header()
        files.append((root + arch, len(head) + idx.datasize, d, idx.start + idx.data_start, head))
    english = sorted(n[:-4] for n in os.listdir(os.path.join(RA_DATA, "movies"))
                     if n.upper().endswith(".VQA"))
    kept, cut, missing = [], [], []
    for name in english:
        want = os.path.getsize(os.path.join(RA_DATA, "movies", name + ".VQA"))
        found = None
        for mov in ("MOVIES1.MIX", "MOVIES2.MIX"):
            a = archive(mov)
            hit = a and a[2].find(name + ".VQA")
            if hit:
                found = (a[0], hit)
                break
        if not found:
            missing.append(name)
        elif found[1][1] != want:
            cut.append(name)
        else:
            kept.append(name)
            files.append((root + "movies/%s.VQA" % name, found[1][1], found[0], found[1][0], b""))
    say("%s ra: %d dubbed movies, %d cut or changed and left English (%s), %d not on these discs"
        % (code, len(kept), len(cut), ", ".join(cut), len(missing)))
    total = 0
    for rel, size, disc, off, head in files:
        img = imgs[disc][0]
        h = hashlib.sha256(head)
        body = size - len(head)
        at = 0
        while at < body:
            n = min(4 << 20, body - at)
            h.update(img.read(off + at, n))
            at += n
        for other in imgs[disc][1:]:
            if other.read(off, min(body, 65536)) != img.read(off, min(body, 65536)):
                raise SystemExit("%s: mirror %s differs at %s" % (disc, other.url, rel))
        line = "file %s ra %s %d %s %s %d" % (code, rel, size, h.hexdigest(), disc, off)
        if head:
            line += " " + head.hex()
        lines.append(line)
        total += size
    return total, len(files)


def c_escape(line):
    return line.replace("\\", "\\\\").replace("\"", "\\\"")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", default=None)
    ap.add_argument("--verbose", action="store_true")
    # --reuse CODE:GAME (repeatable): keep that pack's lines from the manifest as it stands
    # instead of fetching and hashing its files again
    ap.add_argument("--reuse", action="append", default=[])
    a = ap.parse_args()
    fetch = ml.Fetcher(cache_dir=a.cache, use_cache=bool(a.cache), verbose=a.verbose)
    lines = ["OCLANG 1",
             "# Generated by tools/media/make-language-manifest.py. Do not edit by hand.",
             "# mirror <DISC> <url> <base>",
             "# pack <code> <game> <name>",
             "# file <code> <game> <relpath> <size> <sha256> <DISC> <offset> [<head>]",
             "# A file with a head is the head's bytes (hex), then the disc's bytes from offset."]
    used = sorted({d for p in PACKS for d in p[3]})
    for d in used:
        for url in DISCS[d][1]:
            lines.append("mirror %s %s 0" % (d, url))
    old = {}
    try:
        for l in open(os.path.join(HERE, "language-manifest.txt")):
            f = l.split()
            if len(f) > 3 and f[0] in ("file", "#") and (f[0] == "file" or len(f) > 2):
                if f[0] == "file":
                    old.setdefault("%s:%s" % (f[1], f[2]), []).append(l.rstrip("\n"))
    except OSError:
        pass
    for code, game, name, discs in PACKS:
        lines.append("pack %s %s %s" % (code, game, name))
        key = "%s:%s" % (code, game)
        if key in a.reuse and old.get(key):
            lines += old[key]
            say("%s: %d lines kept from the manifest as it stands" % (key, len(old[key])))
            continue
        total, n = (td_pack if game == "td" else ra_pack)(fetch, code, discs, lines, a.verbose)
        lines.append("# %s %s: %d files, %d bytes" % (code, game, n, total))
        say("%s %s: %d files, %.1f MB" % (code, game, n, total / 1e6))
    text = "\n".join(lines) + "\n"
    with open(os.path.join(HERE, "language-manifest.txt"), "w") as f:
        f.write(text)
    body = "".join("    \"%s\\n\"\n" % c_escape(l) for l in lines)
    with open(os.path.join(REPO, "game", "lang_manifest.h"), "w") as f:
        f.write("/* GENERATED by tools/media/make-language-manifest.py from the localised disc images:\n"
                "   the language packs, file by file. Do not edit by hand. */\n"
                "#ifndef LANG_MANIFEST_H\n#define LANG_MANIFEST_H\n"
                "static const char LANG_MANIFEST[] =\n" + body + "    ;\n#endif\n")
    say("wrote tools/media/language-manifest.txt and game/lang_manifest.h")


if __name__ == "__main__":
    main()
