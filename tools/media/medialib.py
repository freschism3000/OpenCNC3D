"""Shared pieces of the media tools: ranged HTTP reads, the zip and ISO9660 walks, the
MIX index, and the manifest reader.

Everything here works on BYTE RANGES of a disc image that sits on somebody else's web
server. Nothing downloads a whole image: a 609 MB ISO is opened by reading its volume
descriptor (2 KB), its directories (a few KB), each MIX file's index (a few KB) and then
only the bytes of the files that are wanted.

Standard library only, so the tools run on any machine with Python 3 and no setup.
"""
import hashlib
import http.client
import os
import ssl
import struct
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "menu", "tools"))
import mixlib  # noqa: E402  (the TD filename hash, the same one the game uses)
sys.path.insert(0, os.path.join(REPO, "tools", "ra3d"))
import ramix  # noqa: E402  (Red Alert's encrypted MIX headers, keyed from the engine source)

USER_AGENT = "OpenCNC3D-media-tools/1"
ISO_BLOCK = 2048
ISO_PVD = 16 * ISO_BLOCK  # the primary volume descriptor is sector 16
DISCS = ("GDI", "NOD", "COVERT", "RA_ALLIED", "RA_SOVIET")
# The folders whose files belong to a module rather than to the game: the launcher fetches
# them only where that module is installed (`module` lines in the manifest).
MODULES = ("ra3d",)


def mix_id(name):
    return mixlib.mix_id(name)


def _ssl_context():
    # Some Python builds ship without a certificate store of their own. The system's
    # bundle is added when it exists; adding a CA bundle never weakens verification.
    ctx = ssl.create_default_context()
    for p in ("/etc/ssl/cert.pem", "/etc/ssl/certs/ca-certificates.crt",
              "/etc/pki/tls/certs/ca-bundle.crt"):
        if os.path.isfile(p):
            try:
                ctx.load_verify_locations(p)
            except (ssl.SSLError, OSError):
                pass
    return ctx


class FetchError(Exception):
    pass


class Fetcher:
    """Ranged GETs with strict checking, retries, and an on-disk cache.

    STRICT: a range read must come back as HTTP 206 with a Content-Range that starts and
    ends exactly where it was asked to. A server that ignores Range answers 200 with the
    whole object, and silently accepting that would stream a 609 MB image to read 64 KB.

    The cache exists so that a rerun of the generator, or the probe straight after it,
    does not fetch the same 20 MB of heads and tails again. It is keyed by URL, offset
    and length, so it can never hand back bytes for a different question.
    """

    def __init__(self, cache_dir=None, use_cache=True, timeout=60, verbose=False,
                 attempts=5):
        self.attempts = attempts
        self.cache_dir = cache_dir
        self.use_cache = use_cache and bool(cache_dir)
        self.timeout = timeout
        self.verbose = verbose
        self.ctx = _ssl_context()
        self.totals = {}
        self.net_bytes = 0
        self.requests = 0
        if self.cache_dir:
            os.makedirs(self.cache_dir, exist_ok=True)

    def _cache_path(self, url, start, length):
        key = hashlib.sha256(url.encode()).hexdigest()[:20]
        return os.path.join(self.cache_dir, "%s_%d_%d.bin" % (key, start, length))

    def _open(self, url, start, end):
        req = urllib.request.Request(url, headers={
            "Range": "bytes=%d-%d" % (start, end),
            "User-Agent": USER_AGENT,
        })
        return urllib.request.urlopen(req, timeout=self.timeout, context=self.ctx)

    @staticmethod
    def _content_range(resp):
        cr = resp.headers.get("Content-Range", "")
        # "bytes 100-199/609021742"
        try:
            unit, rest = cr.split(" ", 1)
            span, total = rest.split("/", 1)
            a, b = span.split("-", 1)
            return int(a), int(b), (int(total) if total != "*" else -1)
        except ValueError:
            return None

    RETRY_CODES = (408, 425, 429, 500, 502, 503, 504)

    def _read_into(self, url, start, length, sink, have=0):
        """Write bytes [start+have, start+length) of url into sink, resuming on retry."""
        attempts = 0
        while have < length:
            attempts += 1
            try:
                self.requests += 1
                with self._open(url, start + have, start + length - 1) as resp:
                    if resp.status != 206:
                        raise FetchError("%s answered HTTP %d to a range request; it does "
                                         "not serve byte ranges" % (url, resp.status))
                    cr = self._content_range(resp)
                    if not cr or cr[0] != start + have or cr[1] != start + length - 1:
                        raise FetchError("%s answered range %d-%d with Content-Range %r"
                                         % (url, start + have, start + length - 1,
                                            resp.headers.get("Content-Range")))
                    if cr[2] >= 0:
                        self.totals[url] = cr[2]
                    while have < length:
                        chunk = resp.read(min(1 << 20, length - have))
                        if not chunk:
                            break
                        sink(chunk)
                        have += len(chunk)
                        self.net_bytes += len(chunk)
                if have < length:
                    raise ConnectionError("the connection closed at byte %d of %d"
                                          % (have, length))
            except urllib.error.HTTPError as e:
                if e.code not in self.RETRY_CODES or attempts >= self.attempts:
                    raise FetchError("%s: HTTP %d" % (url, e.code))
                self._pause(url, attempts, e)
            except (urllib.error.URLError, OSError, http.client.HTTPException) as e:
                if attempts >= self.attempts:
                    raise FetchError("%s: %s (after %d attempts)" % (url, e, attempts))
                self._pause(url, attempts, e)
        return have

    def _pause(self, url, attempts, why):
        if self.verbose:
            print("    retry %d for %s: %s" % (attempts, url, why))
        time.sleep(3 * attempts)

    def get(self, url, start, length):
        """Bytes [start, start+length) of url, as bytes. For reads up to a few MB."""
        if length <= 0:
            return b""
        cp = self._cache_path(url, start, length) if self.use_cache else None
        if cp and os.path.isfile(cp) and os.path.getsize(cp) == length:
            with open(cp, "rb") as f:
                return f.read()
        parts = []
        self._read_into(url, start, length, parts.append)
        data = b"".join(parts)
        if cp:
            tmp = cp + ".tmp"
            with open(tmp, "wb") as f:
                f.write(data)
            os.replace(tmp, cp)
        return data

    def get_file(self, url, start, length):
        """Bytes [start, start+length) of url, streamed into a cache file. Returns its path.

        For whole archives and whole movies. A partial file from an interrupted run is
        resumed rather than started again."""
        if not self.cache_dir:
            raise FetchError("get_file needs a cache directory")
        cp = self._cache_path(url, start, length)
        if os.path.isfile(cp) and os.path.getsize(cp) == length:
            return cp
        part = cp + ".part"
        have = os.path.getsize(part) if os.path.isfile(part) else 0
        if have > length:
            os.remove(part)
            have = 0
        with open(part, "ab") as f:
            self._read_into(url, start, length, f.write, have=have)
        os.replace(part, cp)
        return cp

    def total(self, url):
        """The full size of the object at url, from a one-byte range read."""
        if url not in self.totals:
            self._probe_total(url)
        if url not in self.totals:
            raise FetchError("%s gave no total size in its Content-Range" % url)
        return self.totals[url]

    def _probe_total(self, url):
        # Never answered from the cache: the total is the live object's size.
        self._read_into(url, 0, 1, lambda b: None)


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


# ------------------------------------------------------------------ zip containers

def zip_stored_image(fetch, url):
    """Find the one disc image inside a zip and return (base, size, entry name).

    A byte range into the zip is a byte range into the image ONLY when the image is
    STORED (method 0): then its bytes sit in the zip verbatim, starting right after the
    entry's local header. Anything else raises, with the reason, and the mirror is not
    used. Zip64 is not needed for images under 4 GB and is refused rather than guessed.
    """
    total = fetch.total(url)
    tail_len = min(total, 65536 + 22)
    tail = fetch.get(url, total - tail_len, tail_len)
    i = tail.rfind(b"PK\x05\x06")
    if i < 0:
        raise FetchError("no end-of-central-directory record: not a zip")
    (_sig, _disk, _cdisk, _nthis, count, cd_size, cd_off, _clen) = \
        struct.unpack_from("<IHHHHIIH", tail, i)
    if cd_off == 0xFFFFFFFF or count == 0xFFFF:
        raise FetchError("Zip64 archive; not supported")
    cd = fetch.get(url, cd_off, cd_size)
    p = 0
    images = []
    while p + 46 <= len(cd) and cd[p:p + 4] == b"PK\x01\x02":
        (_s, _vm, _vn, flag, method, _mt, _md, crc, csize, usize, nl, el, cl, _dn, _ia,
         _ea, lho) = struct.unpack_from("<IHHHHHHIIIHHHHHII", cd, p)
        name = cd[p + 46:p + 46 + nl].decode("latin-1")
        if name.lower().endswith(".iso"):
            images.append((name, flag, method, crc, csize, usize, lho))
        p += 46 + nl + el + cl
    if len(images) != 1:
        raise FetchError("expected one .iso entry, found %d" % len(images))
    name, flag, method, crc, csize, usize, lho = images[0]
    if method != 0:
        raise FetchError("%s is compressed (zip method %d, %d bytes packed into %d), so "
                         "byte ranges in the zip do not map onto the image"
                         % (name, method, usize, csize))
    if flag & 1:
        raise FetchError("%s is encrypted" % name)
    if csize != usize:
        raise FetchError("%s is stored but its sizes disagree" % name)
    lh = fetch.get(url, lho, 30)
    if lh[:4] != b"PK\x03\x04" or u16(lh, 8) != 0:
        raise FetchError("%s: local header does not match the central directory" % name)
    base = lho + 30 + u16(lh, 26) + u16(lh, 28)
    return base, usize, name, crc


# ------------------------------------------------------------------ ISO9660

class IsoImage:
    """One disc image on one mirror: where it starts in the URL's bytes, and its tree."""

    def __init__(self, fetch, url, base):
        self.fetch = fetch
        self.url = url
        self.base = base
        pvd = fetch.get(url, base + ISO_PVD, ISO_BLOCK)
        if pvd[0] != 1 or pvd[1:6] != b"CD001":
            raise FetchError("no ISO9660 primary volume descriptor at byte %d" % ISO_PVD)
        self.volume = pvd[40:72].decode("latin-1").strip()
        self.blocks = u32(pvd, 80)
        self.block = u16(pvd, 128)
        if self.block != ISO_BLOCK:
            raise FetchError("logical block size %d, expected 2048" % self.block)
        self.root = pvd[156:156 + 34]
        self.files = {}  # upper-case name -> list of (path, lba, size)
        self.dirs = 0

    def read(self, offset, length):
        return self.fetch.get(self.url, self.base + offset, length)

    @staticmethod
    def _records(buf):
        i = 0
        while i < len(buf):
            n = buf[i]
            if n == 0:
                i = (i // ISO_BLOCK + 1) * ISO_BLOCK
                continue
            rec = buf[i:i + n]
            if len(rec) < 34:
                break
            lba = u32(rec, 2)
            size = u32(rec, 10)
            flags = rec[25]
            nl = rec[32]
            name = rec[33:33 + nl]
            yield name, lba, size, flags
            i += n

    def walk(self, max_dirs=256, max_depth=8):
        """Every file on the disc, by name. Directories are a few KB each."""
        stack = [("", u32(self.root, 2), u32(self.root, 10), 0)]
        seen = set()
        while stack:
            path, lba, size, depth = stack.pop()
            if lba in seen or len(seen) >= max_dirs:
                continue
            seen.add(lba)
            buf = self.read(lba * ISO_BLOCK, size)
            for raw, flba, fsize, flags in self._records(buf):
                if raw in (b"\x00", b"\x01"):
                    continue
                name = raw.decode("latin-1").split(";")[0].upper()
                if name.endswith("."):
                    name = name[:-1]
                full = path + "/" + name
                if flags & 0x02:
                    if depth + 1 <= max_depth:
                        stack.append((full, flba, fsize, depth + 1))
                    continue
                if flags & 0x80:
                    # A file split over several extents is not one byte range.
                    name += "#MULTIEXTENT"
                self.files.setdefault(name, []).append((full, flba, fsize))
        self.dirs = len(seen)
        return self.files


# ------------------------------------------------------------------ MIX index

class MixIndex:
    """A TD MIX file's index, read by range out of a disc image.

    `plain` is what the game's own reader (audio/mixfile.c) accepts: a u16 count first.
    The later Gold archives can begin with a u16 zero and a flags word instead; those are
    readable here but NOT by the game, so a caller that means to hand a whole MIX to the
    player has to check `plain`.
    """

    def __init__(self, img, lba, fsize, label):
        self.label = label
        self.lba = lba
        self.fsize = fsize
        self.start = lba * ISO_BLOCK  # offset of the MIX inside the disc image
        h = img.read(self.start, 12)
        head = 0
        self.flags = 0
        self.plain = True
        if u16(h, 0) == 0:
            self.plain = False
            self.flags = u32(h, 2)
            if self.flags & 0x00020000:
                raise FetchError("%s has an encrypted index (flags 0x%08X)"
                                 % (label, self.flags))
            head = 6
        self.count = u16(h, head)
        self.datasize = u32(h, head + 2)
        if self.count <= 0 or self.count > 20000:
            raise FetchError("%s: implausible entry count %d" % (label, self.count))
        table = img.read(self.start + head + 6, 12 * self.count)
        self.header = h[:head + 6] + table
        self.data_start = head + 6 + 12 * self.count
        self.entries = {}
        for k in range(self.count):
            eid, off, size = struct.unpack_from("<III", table, 12 * k)
            if self.data_start + off + size > fsize:
                raise FetchError("%s: entry 0x%08X runs past the end of the file"
                                 % (label, eid))
            self.entries[eid] = (off, size)

    def find(self, name):
        """(offset inside the disc image, size) of a named entry, or None."""
        e = self.entries.get(mix_id(name))
        if not e:
            return None
        return self.start + self.data_start + e[0], e[1]


class RaMixIndex:
    """A Red Alert MIX file's index, read by range: out of a disc image, or out of another
    MIX the way MAIN.MIX holds MOVIES1.MIX and SCORES.MIX.

    A Red Alert archive may begin with a u16 zero and a flag word, 0x0002 meaning its
    index is encrypted: an 80-byte RSA block holding a Blowfish key, then the
    Blowfish-encrypted count, size and index in whole 8-byte blocks. The file BODIES are
    never encrypted, so every entry is still one contiguous run of the image's bytes, and
    only the index has to be decrypted to find it (tools/ra3d/ramix.py, keyed from the
    engine's own source). `order` keeps the entries in the order the index lists them,
    which is the order a plain header rewritten from it must keep.
    """

    def __init__(self, img, start, fsize, label):
        self.label = label
        self.start = start  # offset of the MIX inside the disc image
        self.fsize = fsize
        h = img.read(start, 4)
        first, flags = struct.unpack_from("<hH", h, 0)
        self.encrypted = False
        if first == 0:
            p = 4
            if flags & 2:
                self.encrypted = True
                key, keylen = ramix._rsa_key(img.read(start + p, 80))
                p += keylen
                bf = ramix.Blowfish(key)
                head = bf.decrypt(img.read(start + p, 8))
                count, size = struct.unpack_from("<HI", head, 0)
                hlen = 6 + 12 * count
                padded = (hlen + 7) // 8 * 8
                head = bf.decrypt(img.read(start + p, padded))
                p += padded
                index = head[6:hlen]
            else:
                count, size = struct.unpack_from("<HI", img.read(start + p, 6), 0)
                index = img.read(start + p + 6, 12 * count)
                p += 6 + 12 * count
        else:
            count, size = struct.unpack_from("<HI", img.read(start, 6), 0)
            index = img.read(start + 6, 12 * count)
            p = 6 + 12 * count
        if count <= 0 or count > 20000 or p + size > fsize:
            raise FetchError("%s: implausible index (%d entries, %d bytes of data in a "
                             "%d-byte file)" % (label, count, size, fsize))
        self.count, self.datasize, self.data_start = count, size, p
        self.order = []
        self.entries = {}
        for k in range(count):
            eid, off, sz = struct.unpack_from("<III", index, 12 * k)
            if off + sz > size:
                raise FetchError("%s: entry 0x%08X runs past the data" % (label, eid))
            self.order.append(eid)
            self.entries[eid] = (off, sz)

    def find_id(self, eid):
        """(offset inside the disc image, size) of an entry by its MIX ID, or None."""
        e = self.entries.get(eid)
        if not e:
            return None
        return self.start + self.data_start + e[0], e[1]

    def find(self, name):
        return self.find_id(mix_id(name))

    def plain_header(self):
        """The Tiberian Dawn layout of this index, the one audio/mixfile.c reads: u16
        count, u32 size, then id, offset, size for each entry in the index's order.
        Followed by the data block unchanged, it is the archive the game's audio reads
        (tools/ra3d/stage-audio.py writes the same bytes from the disc's file)."""
        out = bytearray(struct.pack("<HI", self.count, self.datasize))
        for eid in self.order:
            off, sz = self.entries[eid]
            out += struct.pack("<III", eid, off, sz)
        return bytes(out)


# ------------------------------------------------------------------ local files

def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(1 << 20)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def head_tail(path_or_bytes, size, window=65536):
    """The first and last `window` bytes of a file (or bytes), clipped to its size."""
    n = min(window, size)
    if isinstance(path_or_bytes, (bytes, bytearray)):
        return bytes(path_or_bytes[:n]), bytes(path_or_bytes[size - n:size])
    with open(path_or_bytes, "rb") as f:
        head = f.read(n)
        f.seek(size - n)
        tail = f.read(n)
    return head, tail


# ------------------------------------------------------------------ the manifest

class Manifest:
    """The OCMEDIA 1 text: mirrors, modules, files and legacy paths. See
    tools/media/README.md."""

    def __init__(self, text):
        self.mirrors = []   # (disc, url, base) in preference order
        self.modules = []   # folders whose files only a module's install fetches
        self.files = []     # dicts: component relpath size sha256 disc offset head
        self.legacy = []    # (component, relpath)
        lines = [ln.strip() for ln in text.splitlines()]
        body = [ln for ln in lines if ln and not ln.startswith("#")]
        if not body or body[0] != "OCMEDIA 1":
            raise ValueError("not an OCMEDIA 1 manifest")
        for ln in body[1:]:
            f = ln.split()
            if f[0] == "mirror" and len(f) == 4:
                self.mirrors.append((f[1], f[2], int(f[3])))
            elif f[0] == "module" and len(f) == 2 and f[1] in MODULES:
                self.modules.append(f[1])
            elif f[0] == "file" and len(f) in (7, 8):
                head = bytes.fromhex(f[7]) if len(f) == 8 else b""
                if len(head) >= int(f[3]):
                    raise ValueError("a file's head is not shorter than the file: %s" % ln)
                self.files.append({"component": f[1], "relpath": f[2], "size": int(f[3]),
                                   "sha256": f[4], "disc": f[5], "offset": int(f[6]),
                                   "head": head})
            elif f[0] == "legacy" and len(f) == 3:
                self.legacy.append((f[1], f[2]))
            else:
                raise ValueError("unreadable manifest line: %s" % ln)

    def module_of(self, relpath):
        """The module folder a file belongs to, or None for the game's own."""
        for m in self.modules:
            if relpath.lower().startswith(m.lower() + "/"):
                return m
        return None

    @staticmethod
    def load(path):
        with open(path, "r", encoding="utf-8") as f:
            return Manifest(f.read())
