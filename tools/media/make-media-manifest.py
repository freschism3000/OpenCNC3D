#!/usr/bin/env python3
"""Make the movies-and-music download manifest from the freeware disc images.

    python3 tools/media/make-media-manifest.py [--cache DIR] [--dry-run] [--verbose]

WHAT IT MAKES. The packages carry neither the 1995 movies nor the score: the launcher
downloads them, when the player asks for them, straight out of the disc images on public
mirrors. It needs to know, for every file it will write into the install folder, which
disc holds it, at which byte, how big it is and what its SHA-256 must be. This tool works
that out and writes it down twice:

    tools/media/media-manifest.txt   the manifest as text, for people and for
                                     tools/media/probe-sources.py
    launcher/lmedia_manifest.c       the same text compiled into the launcher

Both are committed. Regenerating them needs the network, and the result is a pinned
decision, not a build product: a mirror that changes its bytes must make the download
FAIL its hash check rather than quietly hand the player something else.

HOW IT WORKS, all by HTTP byte range (nothing reads a whole disc image):

  1. Each candidate in tools/media/media-sources.txt is opened. A zip is kept only if the
     image inside it is stored, because only then do zip offsets map onto image offsets.
  2. The ISO9660 primary volume descriptor (byte 32768) and the directory tree give the
     extent of every file on the disc. Mirrors of one disc must agree on all of them.
  3. Each MIX file's index gives each entry's offset. A movie or a Covert Operations
     track is one contiguous entry inside a MIX, so it is one byte range in the image.
  4. Every file is checked against the development copy in data/dosdata: size, the first
     and last 64 KB on EVERY mirror, and the whole file where the development copy did
     not come out of these images. SCORES.MIX and TRANSIT.MIX are fetched whole, because
     the player gets them as whole files and their hash has to be the disc's own.
  5. The score is played through the game's own lookup rules (loose files first, then the
     archives in the game's order, .AUD before .VAR) for every theme in the game's theme
     table, and each track it would open must be the shape the game's music reader takes:
     mono, 22050 Hz, SOS ADPCM (compression 99). That prints themes=N/37.
  6. RED ALERT, from its two 1996 discs, for the Red Alert module's folder (ra3d/). Its
     movies and its score are entries of archives inside MAIN.MIX, whose indexes are
     encrypted; the bodies are not, so each movie is still one byte range of its disc.
     The score is the exception: the game's audio reader takes plain MIX headers only, so
     the player's ra3d/audio/SCORES.MIX is the disc's archive with its index rewritten
     plain (tools/ra3d/stage-audio.py). Its manifest line carries that rewritten header,
     and the launcher writes it ahead of the disc's data block. Every file is checked
     against the development copy in data/radata, and the lines are fetched only where
     the module is installed (the manifest's `module ra3d` line).

It refuses to write anything if a file has no good mirror, unless --allow-gaps.
"""
import argparse
import hashlib
import os
import re
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import medialib as ml  # noqa: E402

REPO = ml.REPO
WINDOW = 65536

# Tiberian Dawn's discs, and Red Alert's. The movies and the score of each game come
# from its own discs only.
TD_DISCS = ("GDI", "NOD", "COVERT")
RA_DISCS = ("RA_ALLIED", "RA_SOVIET")
RA_MODULE = "ra3d"
# The Red Alert movies' archive on each disc, inside MAIN.MIX.
RA_MOVIE_ARCHIVES = (("RA_ALLIED", "MOVIES1.MIX"), ("RA_SOVIET", "MOVIES2.MIX"))
# The Allied ending is the disc's entry C1FB1EE5, which is the MIX ID of ALLYEND.VQA,
# while the engine asks for ALLIEND.VQA; tools/ra3d/stage-frontend.py stages the same
# alias in the development folder.
RA_ALIASES = (("ALLIEND.VQA", 0xC1FB1EE5, "_C1FB1EE5.BIN"),)

# The movie set in data/dosdata/movies was mostly extracted from these very images, so for
# those files a matching size, head and tail is all the evidence needed. These eight were
# in the set before that, from another pressing of the game, so they are checked whole.
WHOLE_CHECK_MOVIES = ("LOGO", "INTRO2", "CONSYARD", "FLAG", "GAMEOVER", "GDI1", "GDI2",
                      "LANDING")

# The legacy top-level copy of the two menu movies. Older packages shipped it; nothing in
# the game reads it. It belongs to the movies and goes when they go.
LEGACY_TOPLEVEL_MOVIES = ("movies/LOGO.VQA", "movies/INTRO2.VQA")

# The archives the game opens, in the order audio/cncaudio.c (cnc_audio_create) and
# audio/audioboot.c add them. Loose files in dosdata/music and dosdata come before all of
# them. The three Red Alert archives are absent from a Tiberian Dawn install.
GAME_MIX_ORDER = ("SOUNDS.MIX", "SPEECH.MIX", "SCORES.MIX", "RA_LOCAL.MIX", "ALLIES.MIX",
                  "RUSSIAN.MIX", "ZOUNDS.MIX", "AUD.MIX", "TRANSIT.MIX",
                  "transit/TRANSIT.MIX")
BANK_MAX_MIX = 8  # audio/sndbank.c: the bank holds at most this many archives

MUSIC_RATE = 22050
MUSIC_COMP_SOS = 99


def say(*a):
    print(*a, flush=True)


def fmt(n):
    return "{:,}".format(n)


class Mirror:
    def __init__(self, disc, kind, url):
        self.disc, self.kind, self.url = disc, kind, url
        self.base = None
        self.img = None
        self.ok = False
        self.reason = ""
        self.note = ""

    def __str__(self):
        return "%s %s" % (self.disc, self.url)


def load_sources(path):
    out = []
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            p = line.split()
            if len(p) != 4 or p[0] != "source" or p[1] not in ml.DISCS or \
                    p[2] not in ("iso", "zip"):
                raise SystemExit("%s:%d: expected 'source <%s> iso|zip <url>'"
                                 % (path, n, "|".join(ml.DISCS)))
            out.append(Mirror(p[1], p[2], p[3]))
    return out


def open_mirror(fetch, m):
    try:
        if m.kind == "zip":
            m.base, size, name, crc = ml.zip_stored_image(fetch, m.url)
            m.note = "stored %s, %s bytes, CRC-32 %08x, image starts at byte %d" % (
                name, fmt(size), crc, m.base)
        else:
            m.base, size = 0, fetch.total(m.url)
            m.note = "%s bytes" % fmt(size)
        m.img = ml.IsoImage(fetch, m.url, m.base)
        if m.img.blocks * ml.ISO_BLOCK > size:
            raise ml.FetchError("the volume claims %s bytes but the image is %s"
                                % (fmt(m.img.blocks * ml.ISO_BLOCK), fmt(size)))
        m.img.walk()
        m.ok = True
    except ml.FetchError as e:
        m.reason = str(e)


def same_layout(a, b):
    return (a.img.volume == b.img.volume and a.img.blocks == b.img.blocks
            and a.img.files == b.img.files)


def drop(m, why):
    if m.ok:
        m.ok = False
        m.reason = why
        say("  DROP %s: %s" % (m, why))


def one_file(img, name):
    hits = img.files.get(name, [])
    return hits[0] if len(hits) == 1 else (None if not hits else "many")


class Item:
    """One file the player can be given."""

    def __init__(self, component, relpath, disc, offset, size):
        self.component, self.relpath = component, relpath
        self.disc, self.offset, self.size = disc, offset, size
        self.sha256 = None
        self.local = None          # the development copy, if there is one
        self.local_sha = None
        self.disc_path = None      # cached whole copy from the reference mirror
        self.differs = False       # the disc's bytes are not the development copy's
        self.head = self.tail = None
        self.prefix = b""          # bytes the launcher writes ahead of the disc's range

    def name(self):
        return self.relpath.rsplit("/", 1)[-1]


def disc_window(fetch, m, item):
    n = min(WINDOW, item.size)
    head = fetch.get(m.url, m.base + item.offset, n)
    tail = fetch.get(m.url, m.base + item.offset + item.size - n, n)
    return head, tail


def settle(fetch, item, mirrors, whole=False):
    """Decide what bytes the player gets for item, and check every mirror serves them."""
    ref = mirrors[0]
    if item.local and os.path.getsize(item.local) == item.size:
        item.local_sha = ml.sha256_file(item.local)
        lh, lt = ml.head_tail(item.local, item.size, WINDOW)
        rh, rt = disc_window(fetch, ref, item)
        same = (lh == rh and lt == rt)
    else:
        same = False
    if same and not whole:
        item.sha256 = item.local_sha
        item.head, item.tail = lh, lt
    else:
        item.disc_path = fetch.get_file(ref.url, ref.base + item.offset, item.size)
        item.sha256 = ml.sha256_file(item.disc_path)
        item.head, item.tail = ml.head_tail(item.disc_path, item.size, WINDOW)
        item.differs = item.sha256 != item.local_sha
    for m in mirrors[1:]:
        if not m.ok:
            continue
        try:
            h, t = disc_window(fetch, m, item)
        except ml.FetchError as e:
            drop(m, "could not read %s: %s" % (item.relpath, e))
            continue
        if h != item.head or t != item.tail:
            drop(m, "its bytes for %s differ from the reference mirror's" % item.relpath)


def parse_themes():
    src = open(os.path.join(REPO, "audio", "sfxtable.c"), encoding="latin-1").read()
    hdr = open(os.path.join(REPO, "audio", "sfxtable.h"), encoding="latin-1").read()
    count = int(re.search(r"#define\s+SFX_THEME_COUNT\s+(\d+)", hdr).group(1))
    block = re.search(r"const\s+ThemeEntry\s+sfx_theme\[SFX_THEME_COUNT\]\s*=\s*\{(.*?)\n\};",
                      src, re.S).group(1)
    names = re.findall(r'\{"([^"]+)",\s*-?\d+,\s*\d+,\s*\d+,\s*\d+,\s*\d+,', block)
    if len(names) != count:
        raise SystemExit("audio/sfxtable.c lists %d themes, sfxtable.h says %d"
                         % (len(names), count))
    return names


class Bank:
    """The game's audio bank, rebuilt from files: loose folders first, then archives."""

    def __init__(self):
        self.loose = []  # (label, {NAME: path})
        self.mixes = []  # (label, mixlib.Mix)

    def add_loose(self, label, files):
        self.loose.append((label, {k.upper(): v for k, v in files.items()}))

    def add_mix(self, label, path):
        with open(path, "rb") as f:
            head = f.read(2)
        if ml.u16(head, 0) == 0:
            raise SystemExit("%s has the extended MIX layout, which audio/mixfile.c does "
                             "not read: the game would ignore the whole archive" % label)
        self.mixes.append((label, ml.mixlib.Mix(path)))

    def lookup(self, name):
        name = name.upper()
        for label, files in self.loose:
            if name in files:
                with open(files[name], "rb") as f:
                    return label, f.read(12)
        for label, mix in self.mixes:
            if mix.has(name):
                return label, mix.get(name)[:12]
        return None, None


def aud_shape(h):
    rate = ml.u16(h, 0)
    flags, comp = h[10], h[11]
    return rate, ("stereo" if flags & 1 else "mono"), comp


def theme_report(bank, themes, verbose=False):
    """themes=N/total the way audio/audioboot.c counts them, plus a shape check of
    every track the game could open for each theme."""
    have, bad = 0, []
    for t in themes:
        found = False
        for ext in (".AUD", ".VAR"):
            label, h = bank.lookup(t + ext)
            if not label:
                continue
            found = True
            rate, ch, comp = aud_shape(h)
            ok = rate == MUSIC_RATE and ch == "mono" and comp == MUSIC_COMP_SOS
            if not ok:
                bad.append("%s%s from %s is %d Hz %s compression %d" % (t, ext, label,
                                                                       rate, ch, comp))
            if verbose:
                say("    %-12s %-14s %d Hz %s comp %d%s" % (t + ext, label, rate, ch, comp,
                                                            "" if ok else "  WRONG SHAPE"))
        if found:
            have += 1
        elif verbose:
            say("    %-12s (not on the disc)" % t)
    return have, bad


def ra_theme_names():
    """The theme names of the game's Red Alert table (audio/ra_theme_table.h)."""
    src = open(os.path.join(REPO, "audio", "ra_theme_table.h"), encoding="latin-1").read()
    return re.findall(r'\{\{"([^"]+)",', src)


def red_alert(fetch, by_disc, radata, problems, verbose):
    """Red Alert's movies and score, for the module folder ra3d/. Returns the Items.

    Each disc's MAIN.MIX is an archive of archives with encrypted indexes: MOVIES1.MIX
    (Allied) or MOVIES2.MIX (Soviet), and SCORES.MIX, are entries of it. Only indexes are
    encrypted, so an entry of an entry is still one contiguous run of the disc image."""
    say("\nRED ALERT (the module folder %s/, fetched only where it is installed)" % RA_MODULE)
    if not all(by_disc.get(d) for d in RA_DISCS):
        problems.append("Red Alert: a disc has no usable mirror")
        return []
    sys.path.insert(0, os.path.join(REPO, "tools", "ra3d"))
    from importlib import import_module
    stage_audio = import_module("stage-audio")

    main = {}
    for d in RA_DISCS:
        ref = by_disc[d][0]
        hit = one_file(ref.img, "MAIN.MIX")
        if not isinstance(hit, tuple):
            problems.append("Red Alert: %s has %s MAIN.MIX" % (d, "no" if hit is None else
                                                                 "more than one"))
            continue
        path, lba, size = hit
        idx = ml.RaMixIndex(ref.img, lba * ml.ISO_BLOCK, size, "%s %s" % (d, path))
        main[d] = idx
        say("  %-9s %-10s extent %6d, %11s bytes, %2d entries, index %s" % (
            d, path, lba, fmt(size), idx.count, "encrypted" if idx.encrypted else "plain"))
    if len(main) != len(RA_DISCS):
        return []

    def inner(d, name):
        hit = main[d].find(name)
        if not hit:
            return None
        return ml.RaMixIndex(by_disc[d][0].img, hit[0], hit[1], "%s MAIN.MIX:%s" % (d, name))

    archives = []
    for d, name in RA_MOVIE_ARCHIVES:
        idx = inner(d, name)
        if idx is None:
            problems.append("Red Alert: %s's MAIN.MIX holds no %s" % (d, name))
            continue
        archives.append((d, name, idx))
        say("  %-9s MAIN.MIX:%-11s %11s bytes, %2d entries" % (d, name, fmt(idx.fsize),
                                                              idx.count))
    items = []

    # THE MOVIES: the set tools/ra3d/stage-frontend.py stages, every .VQA the disc
    # extraction in data/radata/movies named, and the Allied ending under the name the
    # engine asks for. The Allied disc is searched first.
    movdir = os.path.join(radata, "movies")
    want = [(f.upper(), ml.mix_id(f.upper()), f) for f in sorted(os.listdir(movdir))
            if f.upper().endswith(".VQA")]
    want += [(n, eid, src) for n, eid, src in RA_ALIASES]
    on = dict((d, 0) for d in RA_DISCS)
    for name, eid, src in want:
        where = [(d, a, idx.find_id(eid)) for d, a, idx in archives if idx.find_id(eid)]
        if not where:
            problems.append("Red Alert movie %s is on neither disc" % name)
            continue
        d, a, (off, size) = where[0]
        on[d] += 1
        it = Item("movies", "%s/movies/%s" % (RA_MODULE, name), d, off, size)
        it.local = os.path.join(movdir, src)
        settle(fetch, it, by_disc[d])
        if it.disc_path or it.differs:
            problems.append("Red Alert movie %s: the disc's bytes are not data/radata's" % name)
        items.append(it)
        if verbose:
            say("    %-14s %-9s %s %11s bytes at %11s" % (name, d, a, fmt(size), fmt(off)))
    say("  movies: %d, %d from the Allied disc and %d from the Soviet disc, %s bytes"
        % (sum(on.values()), on["RA_ALLIED"], on["RA_SOVIET"],
           fmt(sum(it.size for it in items))))

    # THE SCORE, as the game's audio reads it: the disc's SCORES.MIX with its encrypted
    # index rewritten plain, then its data block as it is on the disc. The development
    # copy is made the same way, by stage-audio.py from data/radata/SCORES.MIX, so the
    # player's file is that file byte for byte or the tool stops.
    tmp = tempfile.mkdtemp(prefix="opencnc3d-ra-audio-")
    plain_path = os.path.join(tmp, "SCORES.MIX")
    stage_audio.stage_one(__import__("pathlib").Path(os.path.join(radata, "SCORES.MIX")),
                          __import__("pathlib").Path(plain_path))
    with open(plain_path, "rb") as f:
        plain = f.read()
    for d in RA_DISCS:
        sc = inner(d, "SCORES.MIX")
        if sc is None:
            continue
        prefix = sc.plain_header()
        body = sc.start + sc.data_start
        if plain[:len(prefix)] != prefix or len(plain) != len(prefix) + sc.datasize:
            problems.append("Red Alert's score: the plain header made from %s's index is not "
                            "stage-audio.py's" % d)
            break
        it = Item("music", "%s/audio/SCORES.MIX" % RA_MODULE, d, body, len(plain))
        it.prefix = prefix
        it.sha256 = hashlib.sha256(plain).hexdigest()
        rng = plain[len(prefix):]
        it.head, it.tail = ml.head_tail(rng, len(rng), WINDOW)
        for m in by_disc[d]:
            n = min(WINDOW, len(rng))
            try:
                h = fetch.get(m.url, m.base + body, n)
                t = fetch.get(m.url, m.base + body + len(rng) - n, n)
            except ml.FetchError as e:
                drop(m, "could not read Red Alert's score: %s" % e)
                continue
            if (h, t) != (it.head, it.tail):
                drop(m, "its bytes for Red Alert's score are not data/radata's")
        if not any(m.ok for m in by_disc[d]):
            problems.append("Red Alert's score has no good mirror")
            break
        items.append(it)
        say("  score: %s MAIN.MIX:SCORES.MIX, %d entries, a %d-byte plain header and %s "
            "bytes of the disc, sha256 %s" % (d, sc.count, len(prefix), fmt(len(rng)),
                                               it.sha256))
        break
    else:
        problems.append("Red Alert's score is on neither disc")

    # The themes of the game's Red Alert table, from the player's score or from
    # RA_LOCAL.MIX, which the package carries (stage-audio.py's LOCAL.MIX). The table also
    # lists the expansion discs' themes, which neither 1996 disc carries, so the count is
    # not expected to be whole; what the score must do is add every theme it holds.
    local_path = os.path.join(tmp, "RA_LOCAL.MIX")
    stage_audio.stage_one(__import__("pathlib").Path(os.path.join(radata, "LOCAL.MIX")),
                          __import__("pathlib").Path(local_path))
    themes = ra_theme_names()
    score = ml.mixlib.Mix(plain_path)
    local = ml.mixlib.Mix(local_path)
    with_music = sum(1 for t in themes if score.has(t + ".AUD") or local.has(t + ".AUD"))
    without = sum(1 for t in themes if local.has(t + ".AUD"))
    in_score = sum(1 for t in themes if score.has(t + ".AUD"))
    say("  Red Alert themes: %d/%d with the music ticked, %d/%d without (RA_LOCAL.MIX's); "
        "the other %d are the expansion discs'" % (with_music, len(themes), without,
                                                   len(themes), len(themes) - with_music))
    say("  the score holds %d entries, %d of them themes of that table" % (score.count,
                                                                         in_score))
    if with_music <= without:
        problems.append("Red Alert's score adds no theme of the game's table")
    for f in os.listdir(tmp):
        os.remove(os.path.join(tmp, f))
    os.rmdir(tmp)
    return items


def c_escape(line):
    s = line.replace("\\", "\\\\").replace('"', '\\"').replace("??", "?\\?")
    return '    "%s\\n"' % s


def write_atomic(path, text):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    os.replace(tmp, path)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--sources", default=os.path.join(HERE, "media-sources.txt"))
    ap.add_argument("--dosdata", default=os.path.join(REPO, "data", "dosdata"))
    ap.add_argument("--radata", default=os.path.join(REPO, "data", "radata"))
    ap.add_argument("--cache", default=os.path.join(tempfile.gettempdir(),
                                                    "opencnc3d-media-cache"),
                    help="where ranged reads and whole files are kept between runs")
    ap.add_argument("--out", default=os.path.join(HERE, "media-manifest.txt"))
    ap.add_argument("--out-c", default=os.path.join(REPO, "launcher", "lmedia_manifest.c"))
    ap.add_argument("--windows", default=os.path.join(HERE, "media-windows.txt"))
    ap.add_argument("--dry-run", action="store_true", help="check everything, write nothing")
    ap.add_argument("--allow-gaps", action="store_true",
                    help="write the manifest even if some development file has no mirror")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    fetch = ml.Fetcher(args.cache, verbose=args.verbose)
    dosdata = args.dosdata
    problems = []

    # ---------------------------------------------------------------- 1-2. mirrors
    say("MIRRORS")
    mirrors = load_sources(args.sources)
    for m in mirrors:
        open_mirror(fetch, m)
        if m.ok and m.disc in RA_DISCS and m.img.files.get("MAIN.MIX#MULTIEXTENT"):
            drop(m, "its MAIN.MIX is split over several extents")
        if m.ok:
            say("  open %s\n       %s; volume %s, %s blocks, %d directories, %d files"
                % (m, m.note, m.img.volume, fmt(m.img.blocks), m.img.dirs,
                   sum(len(v) for v in m.img.files.values())))
        else:
            say("  DROP %s: %s" % (m, m.reason))
    by_disc = {d: [m for m in mirrors if m.disc == d and m.ok] for d in ml.DISCS}
    for d, ms in by_disc.items():
        for m in ms[1:]:
            if not same_layout(ms[0], m):
                drop(m, "its directory tree is not the same disc as %s" % ms[0].url)
    by_disc = {d: [m for m in ms if m.ok] for d, ms in by_disc.items()}
    for d in ml.DISCS:
        if not by_disc[d]:
            raise SystemExit("no usable mirror for the %s disc" % d)

    # ---------------------------------------------------------------- the archives
    say("\nDISC EXTENTS (reference mirror of each disc)")
    mix = {}  # (disc, NAME) -> MixIndex
    for d in TD_DISCS:
        ref = by_disc[d][0]
        for name in ("MOVIES.MIX", "SCORES.MIX", "TRANSIT.MIX"):
            hit = one_file(ref.img, name)
            if hit is None:
                say("  %-6s %-24s not on this disc" % (d, name))
                continue
            if hit == "many":
                say("  %-6s %-24s on this disc more than once: %s" % (
                    d, name, ", ".join(p for p, _, _ in ref.img.files[name])))
                problems.append("%s carries %s more than once" % (d, name))
                continue
            path, lba, size = hit
            idx = ml.MixIndex(ref.img, lba, size, "%s %s" % (d, path))
            idx.path = path
            mix[(d, name)] = idx
            say("  %-6s %-24s extent %6d (byte %11s) %11s bytes, %3d entries, %s layout"
                % (d, path, lba, fmt(lba * ml.ISO_BLOCK), fmt(size), idx.count,
                   "plain" if idx.plain else "extended (flags 0x%08X)" % idx.flags))
            for m in by_disc[d][1:]:
                if m.ok and m.img.read(idx.start, len(idx.header)) != idx.header:
                    drop(m, "its %s index differs from the reference mirror's" % name)

    items = []

    # ---------------------------------------------------------------- 3-4. movies
    say("\nMOVIES")
    movdir = os.path.join(dosdata, "movies")
    local_movies = sorted(f for f in os.listdir(movdir) if f.upper().endswith(".VQA"))
    for f in local_movies:
        if f != f.upper():
            problems.append("dosdata/movies/%s is not upper case; the game opens "
                            "upper-case names" % f)
    seen_on = dict((d, 0) for d in TD_DISCS)
    both, missing = [], []
    for f in local_movies:
        name = f.upper()
        where = []
        for d in TD_DISCS:  # GDI first, then Nod, the Covert Operations disc last
            idx = mix.get((d, "MOVIES.MIX"))
            hit = idx.find(name) if idx else None
            if hit:
                where.append((d, hit))
        if not where:
            missing.append(name)
            continue
        if len(where) > 1:
            both.append("%s (%s)" % (name, "+".join(w[0] for w in where)))
        d, (off, size) = where[0]
        seen_on[d] += 1
        it = Item("movies", "dosdata/movies/" + name, d, off, size)
        it.local = os.path.join(movdir, f)
        whole = name[:-4] in WHOLE_CHECK_MOVIES
        settle(fetch, it, by_disc[d], whole=whole)
        items.append(it)
        if args.verbose or it.differs or whole:
            say("  %-14s %-6s %11s bytes at %11s%s%s" % (
                name, d, fmt(size), fmt(off), "  checked whole" if whole else "",
                "  DIFFERS from the development copy" if it.differs else ""))
    say("  %d development movies: %d from the GDI disc, %d from the Nod disc, %d from the "
        "Covert Operations disc, %d on none" % (len(local_movies), seen_on["GDI"],
                                               seen_on["NOD"], seen_on["COVERT"],
                                               len(missing)))
    if both:
        say("  on more than one disc (the first is used): %s" % ", ".join(both))
    for n in missing:
        say("  NOT ON ANY DISC: %s" % n)
        problems.append("movie %s is on no disc" % n)
    differs = [it.name() for it in items if it.differs]
    if differs:
        say("  the player gets the DISC's bytes, not the development copy's, for: %s"
            % ", ".join(differs))

    # ---------------------------------------------------------------- 5. music
    #
    # THE TARGET is the score the development folder plays, byte for byte: every entry of
    # its SCORES.MIX and transit/TRANSIT.MIX, and every loose track in dosdata/music.
    # For each archive the first disc whose copy holds ALL of those tracks is used,
    # fetched whole; a track no chosen archive holds becomes a loose file of its own.
    say("\nMUSIC")
    themes = parse_themes()
    names = {}
    for t in themes:
        for ext in (".AUD", ".VAR"):
            names[ml.mix_id(t + ext)] = t + ext
    loose_dir = os.path.join(dosdata, "music")
    loose = sorted(f.upper() for f in os.listdir(loose_dir) if f.upper().endswith(".AUD"))
    for f in loose:
        names[ml.mix_id(f)] = f

    def label(eid):
        return names.get(eid, "#%08X" % eid)

    dev = {}  # id -> sha256 of the bytes the development folder plays for it
    for f in loose:
        dev[ml.mix_id(f)] = ml.sha256_file(os.path.join(loose_dir, f))
    local_mix = {}
    for name, rel in (("SCORES.MIX", "SCORES.MIX"), ("TRANSIT.MIX", "transit/TRANSIT.MIX")):
        lm = ml.mixlib.Mix(os.path.join(dosdata, rel))
        local_mix[name] = lm
        for eid in lm.order:
            dev.setdefault(eid, hashlib.sha256(lm.get_by_id(eid)).hexdigest())
        say("  development %-20s %11s bytes, %2d entries" % (
            "dosdata/" + rel, fmt(os.path.getsize(os.path.join(dosdata, rel))), lm.count))
    loose_only = set(ml.mix_id(f) for f in loose) - set(local_mix["SCORES.MIX"].entries) \
        - set(local_mix["TRANSIT.MIX"].entries)
    say("  development dosdata/music: %d loose tracks, %d of them in neither archive (%s)"
        % (len(loose), len(loose_only), " ".join(sorted(label(e) for e in loose_only))))

    def candidates(archive):
        return [(d, mix[(d, archive)]) for d in TD_DISCS if (d, archive) in mix]

    def lacks(idx, ids):
        return sorted(label(e) for e in ids if e not in idx.entries)

    chosen = {}
    for archive, rel, want_extra in (("SCORES.MIX", "dosdata/SCORES.MIX", loose_only),
                                     ("TRANSIT.MIX", "dosdata/transit/TRANSIT.MIX", set())):
        base_ids = set(local_mix[archive].entries)
        say("  %s: what each disc offers" % rel)
        full, partial = None, None
        for d in TD_DISCS:
            if (d, archive) not in mix:
                say("    %-6s has no %s" % (d, archive))
                continue
            idx = mix[(d, archive)]
            missing_all = lacks(idx, base_ids | want_extra)
            say("    %-6s %-20s %11s bytes, %2d entries; lacks %d of the %d development "
                "tracks%s" % (d, idx.path, fmt(idx.fsize), idx.count, len(missing_all),
                              len(base_ids | want_extra),
                              (": " + " ".join(missing_all)) if missing_all else ""))
            if not idx.plain:
                say("           extended layout: the game's MIX reader refuses it")
                continue
            if not missing_all and full is None:
                full = (d, idx, True)
            if not lacks(idx, base_ids) and partial is None:
                partial = (d, idx, False)
        pick = full or partial
        if pick is None:
            problems.append("no disc carries a %s holding every development track" % archive)
            continue
        d, idx, covers_extra = pick
        it = Item("music", rel, d, idx.start, idx.fsize)
        it.local = os.path.join(dosdata, rel[len("dosdata/"):])
        settle(fetch, it, by_disc[d], whole=True)
        items.append(it)
        chosen[archive] = (it, ml.mixlib.Mix(it.disc_path), covers_extra)
        say("    USE    %s %s, sha256 %s%s" % (d, idx.path, it.sha256,
                                               "" if it.differs else
                                               "  (the development copy's exact bytes)"))

    # Every development track must come out of the player's layout as the same bytes.
    music_files = {it.relpath: it for it in items if it.component == "music"}
    loose_items = []
    if "SCORES.MIX" in chosen and not chosen["SCORES.MIX"][2]:
        # The chosen score lacks the loose-only tracks: each is one MIX entry somewhere,
        # so it is fetched by range as a loose file, the way the development folder has it.
        for eid in sorted(loose_only, key=label):
            name = label(eid)
            for d, idx in candidates("SCORES.MIX"):
                hit = idx.find(name)
                if hit:
                    it = Item("music", "dosdata/music/" + name, d, hit[0], hit[1])
                    it.local = os.path.join(loose_dir, name)
                    settle(fetch, it, by_disc[d], whole=True)
                    items.append(it)
                    loose_items.append(it)
                    music_files[it.relpath] = it
                    break
            else:
                problems.append("music %s is on no disc" % name)
    same, wrong = 0, []
    player = [(it.name(), None, it) for it in loose_items] + \
        [(a, chosen[a][1], chosen[a][0]) for a in ("SCORES.MIX", "TRANSIT.MIX")
         if a in chosen]
    for eid, sha in sorted(dev.items(), key=lambda x: label(x[0])):
        got = None
        for nm, m, it in player:
            if m is None and ml.mix_id(nm) == eid:
                got = it.sha256
                break
            if m is not None and eid in m.entries:
                got = hashlib.sha256(m.get_by_id(eid)).hexdigest()
                break
        if got == sha:
            same += 1
        else:
            wrong.append("%s (%s)" % (label(eid), "absent" if got is None else "different"))
    say("  development tracks the player's music reproduces byte for byte: %d/%d"
        % (same, len(dev)))
    for w in wrong:
        say("    NOT REPRODUCED: %s" % w)
        problems.append("the player's music does not reproduce %s" % w)

    # ---------------------------------------------------------------- legacy
    say("\nLEGACY (removed with their component, never fetched)")
    legacy = [("movies", p) for p in LEGACY_TOPLEVEL_MOVIES]
    for p in LEGACY_TOPLEVEL_MOVIES:
        say("  movies %s" % p)
    for f in loose:
        rel = "dosdata/music/" + f
        if rel in music_files:
            continue
        legacy.append(("music", rel))
        where = [a for a in ("SCORES.MIX", "TRANSIT.MIX")
                 if a in chosen and ml.mix_id(f) in chosen[a][1].entries]
        say("  music  %-28s the same bytes are in the player's %s" % (
            rel, where[0] if where else "(NOWHERE: check)"))

    # ---------------------------------------------------------------- themes
    say("\nTHEMES (the game's lookup rules over the theme table in audio/sfxtable.c)")
    package = {}
    for name in ("SOUNDS.MIX", "SPEECH.MIX", "ZOUNDS.MIX", "AUD.MIX"):
        p = os.path.join(dosdata, name)
        if os.path.isfile(p):
            package[name] = p

    def build_bank(with_music):
        b = Bank()
        b.add_loose("dosdata/music", {it.name(): it.disc_path for it in loose_items}
                    if with_music else {})
        opened = 0
        for name in GAME_MIX_ORDER:
            rel = "dosdata/" + name
            if name in package:
                b.add_mix(rel, package[name])
                opened += 1
            elif with_music and rel in music_files:
                b.add_mix(rel, music_files[rel].disc_path)
                opened += 1
        if opened > BANK_MAX_MIX:
            raise SystemExit("the install would open %d archives; the bank holds %d"
                             % (opened, BANK_MAX_MIX))
        return b

    have, bad = theme_report(build_bank(True), themes, verbose=args.verbose)
    say("  with music ticked:   themes=%d/%d" % (have, len(themes)))
    for b in bad:
        say("    WRONG SHAPE: %s" % b)
        problems.append(b)
    if have != len(themes):
        problems.append("only %d of %d themes resolve" % (have, len(themes)))
    h0, _ = theme_report(build_bank(False), themes)
    say("  with music unticked: themes=%d/%d" % (h0, len(themes)))
    if h0 != 0:
        problems.append("%d themes still resolve with the music left out" % h0)
    bank = build_bank(True)
    for name, what in (("MAP1", "the menu theme"), ("WIN1", "the win theme")):
        where, _ = bank.lookup(name + ".AUD")
        say("  %s.AUD, %s, comes from %s" % (name, what, where))
    gold = mix.get(("GDI", "SCORES.MIX"))
    if gold:
        n = sum(1 for t in themes if ml.mix_id(t + ".AUD") in gold.entries
                or ml.mix_id(t + ".VAR") in gold.entries)
        say("  for comparison, the GDI disc's own SCORES.MIX alone holds %d/%d themes"
            % (n, len(themes)))

    # ---------------------------------------------------------------- 6. Red Alert
    ra_items = red_alert(fetch, by_disc, args.radata, problems, args.verbose)
    items += ra_items

    # ---------------------------------------------------------------- totals
    say("\nTOTALS")
    totals = {}
    for c in ("movies", "music"):
        sel = [it for it in items if it.component == c]
        totals[c] = (len(sel), sum(it.size for it in sel))
        ra = [it for it in sel if it in ra_items]
        say("  %-7s %3d files %14s bytes, %d of them %s bytes Red Alert's" % (
            c, totals[c][0], fmt(totals[c][1]), len(ra), fmt(sum(it.size for it in ra))))
    say("  network: %s bytes in %d requests (the cache answered the rest)"
        % (fmt(fetch.net_bytes), fetch.requests))

    if problems:
        say("\nPROBLEMS")
        for p in problems:
            say("  " + p)
        if not args.allow_gaps:
            say("\nnothing written (--allow-gaps writes a manifest without them)")
            return 1

    # ---------------------------------------------------------------- write
    kept = [m for m in mirrors if m.ok]
    lines = ["OCMEDIA 1",
             "# Generated by tools/media/make-media-manifest.py from "
             "tools/media/media-sources.txt. Do not edit by hand.",
             "# movies: %d files, %s bytes. music: %d files, %s bytes." % (
                 totals["movies"][0], fmt(totals["movies"][1]), totals["music"][0],
                 fmt(totals["music"][1])),
             "# A file is the byte range [base+offset, base+offset+size) of any mirror of "
             "its disc.",
             "# A file with a head is the head's bytes, then that range for the rest of its "
             "size.",
             "# A module's files are fetched only where that module is installed."]
    for m in kept:
        lines.append("mirror %s %s %d" % (m.disc, m.url, m.base))
    if ra_items:
        lines.append("module %s" % RA_MODULE)
    for it in sorted(items, key=lambda i: (i.component != "movies", i.relpath)):
        lines.append("file %s %s %d %s %s %d%s" % (it.component, it.relpath, it.size,
                                                    it.sha256, it.disc, it.offset,
                                                    " " + it.prefix.hex() if it.prefix else ""))
    for c, p in sorted(legacy, key=lambda x: (x[0] != "movies", x[1])):
        lines.append("legacy %s %s" % (c, p))
    text = "\n".join(lines) + "\n"
    text.encode("ascii")
    ml.Manifest(text)  # the reader must accept what the writer wrote
    digest = hashlib.sha256(text.encode()).hexdigest()

    ctext = [
        "/* GENERATED by tools/media/make-media-manifest.py. Do not edit: rerun the tool.",
        " *",
        " * The movies and music the launcher can download, and where each file lives",
        " * inside the freeware disc images on the public mirrors. The same text is",
        " * committed as tools/media/media-manifest.txt, where its format is described",
        " * (tools/media/README.md), and tools/media/probe-sources.py checks every mirror",
        " * against it before a release.",
        " *",
        " * movies: %d files, %s bytes" % (totals["movies"][0], fmt(totals["movies"][1])),
        " * music:  %d files, %s bytes" % (totals["music"][0], fmt(totals["music"][1])),
        " * SHA-256 of the text: %s" % digest,
        " */",
        "",
        "const char LMEDIA_MANIFEST[] =",
    ] + [c_escape(ln) for ln in text.rstrip("\n").split("\n")]
    ctext[-1] += ";"
    csrc = "\n".join(ctext) + "\n"

    wlines = ["# Generated by tools/media/make-media-manifest.py beside media-manifest.txt.",
              "# The SHA-256 of the first and last 64 KB of every file, so",
              "# tools/media/probe-sources.py can check a mirror without the whole file.",
              "window %d" % WINDOW]
    for it in sorted(items, key=lambda i: (i.component != "movies", i.relpath)):
        wlines.append("%s %s %s" % (it.relpath, hashlib.sha256(it.head).hexdigest(),
                                    hashlib.sha256(it.tail).hexdigest()))
    wtext = "\n".join(wlines) + "\n"

    if args.dry_run:
        say("\n--dry-run: would write %s (%d lines) and %s" % (args.out, len(lines),
                                                             args.out_c))
        return 0
    write_atomic(args.out, text)
    write_atomic(args.out_c, csrc)
    write_atomic(args.windows, wtext)
    say("\nwrote %s (%d lines, sha256 %s)\nwrote %s\nwrote %s" % (
        os.path.relpath(args.out, REPO), len(lines), digest,
        os.path.relpath(args.out_c, REPO), os.path.relpath(args.windows, REPO)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
