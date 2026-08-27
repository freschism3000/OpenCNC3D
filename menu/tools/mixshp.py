#!/usr/bin/env python3
"""
mixshp.py -- Westwood MIX archive reader + Tiberian Dawn SHP (ShapeSet / "keyframe")
decoder for the 1995 MS-DOS release of Command & Conquer.

Everything here is a direct port of the GPL engine sources, not a guess:

  MIX container      : header u16 count, u32 datasize, count * (u32 id, u32 off, u32 size).
                       Data starts right after the table; offsets are relative to that.
                       Names are not stored, only the hash produced by mix_id().
  LCW ("format80")   : brain/vanilla/common/lcw.cpp        -> LCW_Uncompress()
  XOR delta ("f40")  : brain/vanilla/common/xordelta.cpp   -> Apply_XOR_Delta()
  SHP frame assembly : brain/vanilla/common/keyframe.cpp   -> Build_Frame()
                       header brain/vanilla/common/keyframe.h -> KeyFrameType flags

SHP layout (KeyFrameHeaderType, 14 bytes, little endian, packed):
    u16 frames, u16 x, u16 y, u16 width, u16 height, u16 largest_frame_size, s16 flags
  followed by `frames` entries of 2 * u32:
    u32[0] = (frame flags << 24) | offset-of-this-frame's-data
    u32[1] = meaning depends on the frame flags (see build_frame())
  If (flags & 1) the shape carries an embedded 768 byte VGA palette, and every
  data offset in the table must be biased by +768.

Usage:
    python3 mixshp.py            # decode the sidebar set, write PNGs + contact sheet
    python3 mixshp.py --list     # dump the MIX indexes with resolved names
"""

import argparse
import json
import os
import struct
import sys

MIX_PATHS = [
    "dosart/cd1/CONQUER.MIX",
    "dosart/LOCAL.MIX",
    "dosart/cd1/TEMPERAT.MIX",
    "dosart/cd1/DESERT.MIX",
    "dosart/cd1/WINTER.MIX",
    "dosart/cd1/GENERAL.MIX",
    "dosart/cd1/SETUP.MIX",
]

SCRATCH = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT_ROOT = os.path.join(SCRATCH, "sidebar")
PNG_DIR = os.path.join(OUT_ROOT, "png")


# ---------------------------------------------------------------------------
# MIX
# ---------------------------------------------------------------------------

def mix_id(name):
    """Westwood's TD filename hash. Confirmed against 100+ known entries."""
    b = name.upper().encode()
    l = len(b)
    i = 0
    v = 0
    while i < l:
        a = 0
        for j in range(4):
            a >>= 8
            if i + j < l:
                a |= (b[i + j] << 24)
        v = ((v << 1) & 0xFFFFFFFF) | (v >> 31)
        v = (v + a) & 0xFFFFFFFF
        i += 4
    return v


class MixFile(object):
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as fh:
            self.data = fh.read()
        count, datasize = struct.unpack_from("<HI", self.data, 0)
        self.count = count
        self.datasize = datasize
        self.base = 6 + count * 12
        self.index = {}
        for i in range(count):
            fid, off, size = struct.unpack_from("<IiI", self.data, 6 + i * 12)
            self.index[fid & 0xFFFFFFFF] = (off, size)
        # Sanity: the archive should be exactly table + payload.
        self.trailing = len(self.data) - (self.base + datasize)

    def has(self, name):
        return mix_id(name) in self.index

    def read(self, name):
        return self.read_id(mix_id(name))

    def read_id(self, fid):
        off, size = self.index[fid]
        start = self.base + off
        return self.data[start:start + size]

    def size_of(self, name):
        return self.index[mix_id(name)][1]


class MixSet(object):
    """Several MIX archives searched in order, like the engine's CCFileClass."""

    def __init__(self, root, paths):
        self.mixes = []
        for rel in paths:
            p = os.path.join(root, rel)
            if os.path.exists(p):
                self.mixes.append((rel, MixFile(p)))

    def find(self, name):
        fid = mix_id(name)
        for rel, mix in self.mixes:
            if fid in mix.index:
                return rel, mix.read_id(fid), mix.index[fid][1]
        return None, None, None

    def read(self, name):
        rel, data, size = self.find(name)
        if data is None:
            raise KeyError(name)
        return data


# ---------------------------------------------------------------------------
# LCW  (lcw.cpp :: LCW_Uncompress)
# ---------------------------------------------------------------------------

def lcw_uncompress(src, src_pos, length):
    """Decompress `length` bytes. Returns (bytearray dest, bytes_written).

    Faithful port including the clamp-to-destination-end guards.
    """
    dest = bytearray(length)
    sp = src_pos
    dp = 0
    n = len(src)
    while dp < length:
        if sp >= n:
            break
        op = src[sp]
        sp += 1
        if not (op & 0x80):
            # 0xxxyyyy yyyyyyyy : short copy from dest, back y, run x+3
            count = (op >> 4) + 3
            if sp >= n:
                break
            back = src[sp] + ((op & 0x0F) << 8)
            sp += 1
            cp = dp - back
            if count > length - dp:
                count = length - dp
            for _ in range(count):
                dest[dp] = dest[cp] if cp >= 0 else 0
                dp += 1
                cp += 1
        else:
            if not (op & 0x40):
                if op == 0x80:
                    return dest, dp          # end of data
                # 10xxxxxx : copy x bytes straight from source
                count = op & 0x3F
                if count > length - dp:
                    count = length - dp
                dest[dp:dp + count] = src[sp:sp + count]
                sp += (op & 0x3F)
                dp += count
            else:
                if op == 0xFE:
                    # long run of one byte
                    count = src[sp] | (src[sp + 1] << 8)
                    sp += 2
                    val = src[sp]
                    sp += 1
                    if count > length - dp:
                        count = length - dp
                    for _ in range(count):
                        dest[dp] = val
                        dp += 1
                elif op == 0xFF:
                    # long copy from dest, absolute offset
                    count = src[sp] | (src[sp + 1] << 8)
                    sp += 2
                    cp = src[sp] | (src[sp + 1] << 8)
                    sp += 2
                    if count > length - dp:
                        count = length - dp
                    for _ in range(count):
                        dest[dp] = dest[cp]
                        dp += 1
                        cp += 1
                else:
                    # 11xxxxxx : medium copy from dest, absolute offset, x+3 bytes
                    count = (op & 0x3F) + 3
                    cp = src[sp] | (src[sp + 1] << 8)
                    sp += 2
                    if count > length - dp:
                        count = length - dp
                    for _ in range(count):
                        dest[dp] = dest[cp]
                        dp += 1
                        cp += 1
    return dest, dp


# ---------------------------------------------------------------------------
# XOR delta  (xordelta.cpp :: Apply_XOR_Delta)
# ---------------------------------------------------------------------------

def apply_xor_delta(dst, src, src_pos):
    """In-place XOR delta over `dst` (a bytearray). Returns the source cursor."""
    gp = src_pos
    dp = 0
    n = len(src)
    dlen = len(dst)
    while True:
        if gp >= n:
            return gp
        cmd = src[gp]
        gp += 1
        count = cmd
        xorval = False
        value = 0

        if not (cmd & 0x80):
            if cmd == 0:
                # 0b00000000 : run of a single xor value
                count = src[gp] & 0xFF
                gp += 1
                value = src[gp]
                gp += 1
                xorval = True
            # else 0b0??????? : xor the next `count` source bytes
        else:
            count &= 0x7F
            if count != 0:
                # 0b1??????? : skip `count` bytes
                dp += count
                continue
            count = (src[gp] & 0xFF) | (src[gp + 1] << 8)
            gp += 2
            if count == 0:
                return gp                    # end of delta
            if (count & 0x8000) == 0:
                dp += count                  # long skip
                continue
            if count & 0x4000:
                count &= 0x3FFF
                value = src[gp]
                gp += 1
                xorval = True
            else:
                count &= 0x3FFF

        if xorval:
            for _ in range(count):
                if dp < dlen:
                    dst[dp] ^= value
                dp += 1
        else:
            for _ in range(count):
                if dp < dlen:
                    dst[dp] ^= src[gp]
                dp += 1
                gp += 1


# ---------------------------------------------------------------------------
# SHP  (keyframe.cpp :: Build_Frame)
# ---------------------------------------------------------------------------

KF_NUMBER = 0x08
KF_UNCOMP = 0x10
KF_DELTA = 0x20
KF_KEYDELTA = 0x40
KF_KEYFRAME = 0x80

SHP_HEADER_SIZE = 14
SUBFRAMEOFFS = 7


class ShpError(Exception):
    pass


class Shape(object):
    """A decoded ShapeSet: uniform width/height, `frames` 8-bit index buffers."""

    def __init__(self, data):
        if len(data) < SHP_HEADER_SIZE:
            raise ShpError("too short to be a SHP (%d bytes)" % len(data))
        (self.frames, self.x, self.y, self.width, self.height,
         self.largest_frame_size, self.flags) = struct.unpack_from("<HHHHHHh", data, 0)
        self.data = data
        self.buffsize = self.width * self.height
        if self.frames == 0 or self.width == 0 or self.height == 0:
            raise ShpError("implausible header frames=%d %dx%d"
                           % (self.frames, self.width, self.height))
        need = SHP_HEADER_SIZE + self.frames * 8
        if need > len(data):
            raise ShpError("offset table (%d bytes) runs past end of file (%d)"
                           % (need, len(data)))
        self.has_palette = bool(self.flags & 1)
        self.pal_bias = 768 if self.has_palette else 0
        self._cache = {}

    def embedded_palette(self):
        """768 raw bytes if flags&1, else None. Location per Get_Build_Frame_Palette."""
        if not self.has_palette:
            return None
        off = 8 * self.frames + 16 + SHP_HEADER_SIZE
        return self.data[off:off + 768]

    def frame_flags(self, n):
        u0 = struct.unpack_from("<I", self.data, SHP_HEADER_SIZE + n * 8)[0]
        return (u0 >> 24) & 0xFF

    def _offsets(self, frame, howmany):
        """Read `howmany` u32s starting at the offset-table slot for `frame`."""
        base = SHP_HEADER_SIZE + (frame << 3)
        out = []
        for i in range(howmany):
            p = base + i * 4
            if p + 4 <= len(self.data):
                out.append(struct.unpack_from("<I", self.data, p)[0])
            else:
                out.append(0)
        return out

    def frame(self, framenumber):
        """Port of Build_Frame(). Returns a bytearray of width*height palette indices."""
        if framenumber in self._cache:
            return self._cache[framenumber]
        if framenumber >= self.frames:
            raise ShpError("frame %d out of range (%d)" % (framenumber, self.frames))

        data = self.data
        buffsize = self.buffsize
        offset = self._offsets(framenumber, 3)
        frameflags = (offset[0] >> 24) & 0xFF

        if frameflags & KF_KEYFRAME:
            ptr = (offset[0] & 0x00FFFFFF) + self.pal_bias
            buf, _ = lcw_uncompress(data, ptr, buffsize)
        else:
            if frameflags & KF_DELTA:
                currframe = offset[1] & 0xFFFF
                offset = self._offsets(currframe, SUBFRAMEOFFS)

            offcurr = offset[1] & 0x00FFFFFF                 # keyframe LCW data
            offdiff = (offset[0] & 0x00FFFFFF) - offcurr     # delta relative to it
            ptr = offcurr + self.pal_bias

            buf, wrote = lcw_uncompress(data, ptr, buffsize)
            if wrote > buffsize:
                raise ShpError("frame %d overran its buffer" % framenumber)

            apply_xor_delta(buf, data, ptr + offdiff)

            if frameflags & KF_DELTA:
                currframe += 1
                subframe = 2
                while currframe <= framenumber:
                    offdiff = (offset[subframe] & 0x00FFFFFF) - offcurr
                    apply_xor_delta(buf, data, ptr + offdiff)
                    currframe += 1
                    subframe += 2
                    if subframe >= (SUBFRAMEOFFS - 1) and currframe <= framenumber:
                        offset = self._offsets(currframe, SUBFRAMEOFFS)
                        subframe = 0

        self._cache[framenumber] = buf
        return buf

    def describe(self):
        kinds = {}
        for i in range(self.frames):
            f = self.frame_flags(i)
            name = []
            if f & KF_KEYFRAME:
                name.append("KEYFRAME")
            if f & KF_KEYDELTA:
                name.append("KEYDELTA")
            if f & KF_DELTA:
                name.append("DELTA")
            if f & KF_UNCOMP:
                name.append("UNCOMP")
            if f & KF_NUMBER:
                name.append("NUMBER")
            k = "+".join(name) if name else "0x%02X" % f
            kinds[k] = kinds.get(k, 0) + 1
        return kinds


# ---------------------------------------------------------------------------
# ShapeBlock  (getshape.cpp :: Extract_Shape, shape.h :: Shape_Type)
#
# This is the OLDER Westwood shape container, not the keyframe one. MOUSE.SHP
# uses it: init.cpp/mouse.cpp reach it through Extract_Shape(), never
# Build_Frame(). Layout:
#
#   u16 NumShapes
#   i32 Offsets[NumShapes]      offsets are relative to buffer+2
#   (then one extra i32 end-of-data offset, then padding to the first shape)
#
# Each shape then carries its own 10 byte header:
#   u16 ShapeType   0 = 256-colour LCW, 1 = 16-colour + colour table,
#                   2 = uncompressed, 4 = fewer than 16 colours
#   u8  Height
#   u16 Width
#   u8  OriginalHeight
#   u16 ShapeSize   total bytes of this shape, header included
#   u16 DataLength  size after LCW but BEFORE the RLE-zero pass
#   u8  Colortable[16]   present only when ShapeType & 1
#
# Pixel data is LCW, then a second RLE-zero ("format2") pass in which a 0x00
# byte is followed by a run length of transparent pixels.
# ---------------------------------------------------------------------------

MAKESHAPE_NORMAL = 0x0000
MAKESHAPE_COMPACT = 0x0001
MAKESHAPE_NOCOMP = 0x0002
MAKESHAPE_VARIABLE = 0x0004

SHAPEBLOCK_HDR = 10


def rle_zero_expand(src, length):
    """Format2: literal bytes, except 0x00 which introduces a run of zeros."""
    out = bytearray(length)
    dp = 0
    sp = 0
    n = len(src)
    while dp < length and sp < n:
        v = src[sp]
        sp += 1
        if v != 0:
            out[dp] = v
            dp += 1
        else:
            if sp >= n:
                break
            count = src[sp]
            sp += 1
            dp += count
    return out, dp


class ShapeBlock(object):
    def __init__(self, data):
        self.data = data
        self.count = struct.unpack_from("<H", data, 0)[0]
        self.offsets = [struct.unpack_from("<i", data, 2 + i * 4)[0]
                        for i in range(self.count)]

    def header(self, i):
        p = 2 + self.offsets[i]
        stype, height, width, origh, ssize, dlen = struct.unpack_from("<HBHBHH", self.data, p)
        return dict(pos=p, type=stype, width=width, height=height,
                    original_height=origh, shape_size=ssize, data_length=dlen)

    def frame(self, i):
        h = self.header(i)
        p = h["pos"] + SHAPEBLOCK_HDR
        if h["type"] & MAKESHAPE_COMPACT:
            p += 16                                   # skip the 16 entry colour table
        payload_end = h["pos"] + h["shape_size"]
        payload = self.data[p:payload_end]
        w, ht = h["width"], h["height"]
        if h["type"] & MAKESHAPE_NOCOMP:
            stage1 = bytearray(payload[:h["data_length"]])
        else:
            stage1, _ = lcw_uncompress(payload, 0, h["data_length"])
        buf, wrote = rle_zero_expand(stage1, w * ht)
        h["rle_wrote"] = wrote
        return buf, h


# ---------------------------------------------------------------------------
# Palette + PNG
# ---------------------------------------------------------------------------

def load_pal(raw):
    """768 byte 6-bit VGA palette -> list of 256 (r,g,b) 8-bit tuples."""
    pal = []
    for i in range(256):
        r, g, b = raw[i * 3], raw[i * 3 + 1], raw[i * 3 + 2]
        # VGA DAC is 6 bit; widen to 8 by replicating the top 2 bits.
        pal.append((((r & 0x3F) << 2) | (r >> 4),
                    ((g & 0x3F) << 2) | (g >> 4),
                    ((b & 0x3F) << 2) | (b >> 4)))
    return pal


def contact_sheet(items, path, scale=2, cols=8, bg=(48, 48, 56), tilebg=(0, 0, 0)):
    """items: list of (label, RGBA Image), all the same size."""
    from PIL import Image, ImageDraw
    cw = max(i.width for _, i in items) * scale
    ch = max(i.height for _, i in items) * scale
    pad, labelh = 4, 10
    rows = (len(items) + cols - 1) // cols
    W = cols * (cw + pad) + pad
    H = rows * (ch + pad + labelh) + pad
    sheet = Image.new("RGB", (W, H), bg)
    draw = ImageDraw.Draw(sheet)
    for i, (label, img) in enumerate(items):
        r, c = divmod(i, cols)
        x = pad + c * (cw + pad)
        y = pad + r * (ch + pad + labelh)
        big = img.convert("RGBA").resize((img.width * scale, img.height * scale),
                                         Image.NEAREST)
        tile = Image.new("RGB", (cw, ch), tilebg)
        # centre so an odd-sized frame still sits inside its own cell
        tile.paste(big, ((cw - big.width) // 2, (ch - big.height) // 2), big)
        sheet.paste(tile, (x, y))
        draw.text((x + 1, y + ch + 1), label, fill=(230, 230, 230))
    sheet.save(path)
    return sheet.size


def frame_to_image(buf, w, h, pal, transparent_index=0):
    from PIL import Image
    img = Image.new("RGBA", (w, h))
    px = img.load()
    i = 0
    for y in range(h):
        for x in range(w):
            v = buf[i]
            i += 1
            if v == transparent_index:
                px[x, y] = (0, 0, 0, 0)
            else:
                r, g, b = pal[v]
                px[x, y] = (r, g, b, 255)
    return img


# ---------------------------------------------------------------------------
# The sidebar working set
# ---------------------------------------------------------------------------

# Every <NAME>ICON.SHP present in CONQUER.MIX. Recovered by brute-forcing the
# MIX hash over [A-Z0-9]{1,4} + "ICON.SHP", which is exhaustive because the 8.3
# base name leaves at most 4 characters before the "ICON" suffix.
#
# The brute force also produced two FALSE POSITIVES. The MIX hash is weak and
# stores no filename, so unrelated names can land on the same id. "ATRNICON.SHP"
# and "OLNICON.SHP" are not real assets, they collide with ATOMICUP.SHP (48x48,
# 4 frames, the nuke launch animation) and MINIGUN.SHP (18x17, 48 frames, the
# turret animation). Both are excluded below and decoded under their real names
# in COLLISIONS instead. Neither "ATRN" nor "OLN" is a C&C object.
CAMEOS = ("A10 AFLD APC ARCO ARTY ATOM ATWR BARB BGGY BIKE BIO BOAT BOMB BRIK C17 CYCL "
          "E1 E2 E3 E4 E5 E6 EYE FACT FIX FTNK GTWR GUN HAND HARV HELI HOSP HPAD HQ "
          "HTNK ION JEEP LST LTNK MCV MHQ MLRS MSAM MTNK NUK2 NUKE OBLI ORCA PROC "
          "PUMP PYLE RMBO ROAD SAM SBAG SILO STNK TMPL TRAN WEAP WOOD").split()

COLLISIONS = ["ATOMICUP.SHP", "MINIGUN.SHP"]

CHROME = ["CLOCK.SHP", "STRIP.SHP", "STRIPUP.SHP", "STRIPDN.SHP", "TABS.SHP",
          "OPTIONS.SHP", "POWER.SHP",
          # sidebar.cpp draws PIP_READY / PIP_HOLDING over a building cameo
          # from ObjectTypeClass::PipShapes, which is PIPS.SHP.
          "PIPS.SHP"]

# Shapes that use the older ShapeBlock container instead of the keyframe one.
BLOCK_SHAPES = ["MOUSE.SHP"]

# C&C95-only additions. Absent from the 1995 DOS build ON PURPOSE: Repair/Sell/Map
# are TextButtonClass in 6-point font there (sidebar.cpp Init_IO, lo-res branch).
NOT_IN_DOS = ["SIDE1.SHP", "SIDE2.SHP", "REPAIR.SHP", "SELL.SHP", "MAP.SHP"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.path.join(SCRATCH, "dosart", ".."))
    ap.add_argument("--list", action="store_true", help="dump MIX indexes")
    ap.add_argument("--out", default=PNG_DIR)
    args = ap.parse_args()

    root = SCRATCH
    mixes = MixSet(root, MIX_PATHS)
    if not mixes.mixes:
        print("no MIX archives found under %s" % root, file=sys.stderr)
        return 2

    print("=== archives ===")
    for rel, mix in mixes.mixes:
        print("  %-28s %4d entries, payload %d bytes, trailing %d"
              % (rel, mix.count, mix.datasize, mix.trailing))

    if args.list:
        return 0

    # Palette: the theater palettes are the only ones in these archives.
    pal_raw = mixes.read("TEMPERAT.PAL")
    pal = load_pal(pal_raw)

    os.makedirs(args.out, exist_ok=True)

    wanted = [("%sICON.SHP" % c, "cameo", c) for c in CAMEOS]
    wanted += [(n, "chrome", n[:-4]) for n in CHROME]
    wanted += [(n, "collide", n[:-4]) for n in COLLISIONS]

    report = {"palette": "TEMPERAT.PAL", "shapes": {}, "missing": [], "failed": {}}
    cameo_imgs = []

    print("\n=== shapes ===")
    print("%-14s %-8s %6s %5s %5s %6s %6s  %s"
          % ("name", "kind", "bytes", "frms", "w", "h", "flags", "frame kinds"))
    for name, kind, label in wanted:
        rel, raw, size = mixes.find(name)
        if raw is None:
            report["missing"].append(name)
            print("%-14s %-8s   MISSING" % (name, kind))
            continue
        try:
            shp = Shape(raw)
        except ShpError as exc:
            report["failed"][name] = str(exc)
            print("%-14s %-8s   HEADER FAIL: %s" % (name, kind, exc))
            continue

        kinds = shp.describe()
        print("%-14s %-8s %6d %5d %5d %6d %6d  %s"
              % (name, kind, size, shp.frames, shp.width, shp.height, shp.flags,
                 ", ".join("%s x%d" % (k, v) for k, v in sorted(kinds.items()))))

        entry = {
            "container": "keyframe",
            "mix": rel, "bytes": size, "frames": shp.frames,
            "width": shp.width, "height": shp.height,
            "hdr_x": shp.x, "hdr_y": shp.y,
            "largest_frame_size": shp.largest_frame_size,
            "flags": shp.flags, "embedded_palette": shp.has_palette,
            "frame_kinds": kinds, "decoded_frames": 0,
        }

        sub = os.path.join(args.out, label.lower())
        os.makedirs(sub, exist_ok=True)
        bad = []
        for i in range(shp.frames):
            try:
                buf = shp.frame(i)
            except Exception as exc:               # noqa: BLE001
                bad.append("%d: %s" % (i, exc))
                continue
            img = frame_to_image(buf, shp.width, shp.height, pal)
            img.save(os.path.join(sub, "%s_%03d.png" % (label.lower(), i)))
            entry["decoded_frames"] += 1
            if kind == "cameo" and i == 0:
                cameo_imgs.append((label, img))
        if bad:
            entry["frame_errors"] = bad
            report["failed"][name] = bad
        report["shapes"][name] = entry

    # ---- ShapeBlock-format shapes ----
    print("\n=== ShapeBlock (Extract_Shape) shapes ===")
    for name in BLOCK_SHAPES:
        rel, raw, size = mixes.find(name)
        if raw is None:
            report["missing"].append(name)
            print("  %-12s MISSING" % name)
            continue
        blk = ShapeBlock(raw)
        label = name[:-4].lower()
        sub = os.path.join(args.out, label)
        os.makedirs(sub, exist_ok=True)
        sizes = {}
        types = {}
        ok = 0
        bad = []
        for i in range(blk.count):
            try:
                buf, h = blk.frame(i)
            except Exception as exc:               # noqa: BLE001
                bad.append("%d: %s" % (i, exc))
                continue
            sizes["%dx%d" % (h["width"], h["height"])] = \
                sizes.get("%dx%d" % (h["width"], h["height"]), 0) + 1
            types[h["type"]] = types.get(h["type"], 0) + 1
            img = frame_to_image(buf, h["width"], h["height"], pal)
            img.save(os.path.join(sub, "%s_%03d.png" % (label, i)))
            ok += 1
        print("  %-12s %s  %d shapes, %d decoded, sizes %s, ShapeType %s"
              % (name, rel, blk.count, ok, sizes, types))
        report["shapes"][name] = {
            "container": "ShapeBlock", "mix": rel, "bytes": size,
            "frames": blk.count, "decoded_frames": ok,
            "sizes": sizes, "shape_types": {str(k): v for k, v in types.items()},
        }
        if bad:
            report["failed"][name] = bad

    print("\n=== deliberately absent in the 1995 DOS build ===")
    for name in NOT_IN_DOS:
        rel, raw, size = mixes.find(name)
        state = "PRESENT in %s (%d b)" % (rel, size) if raw else "absent (as expected)"
        print("  %-12s %s" % (name, state))
        report.setdefault("cc95_only", {})[name] = state

    # ---- contact sheets ----
    if cameo_imgs:
        # Keep the sheet cell-tight: the two odd-sized icons (ATRN 48x48,
        # OLN 18x17) go on their own sheet so every main cell is exactly 32x24
        # and no label can be misread against a neighbouring row.
        odd = [(l, i) for l, i in cameo_imgs if (i.width, i.height) != (32, 24)]
        if odd:
            raise SystemExit("unexpected non-32x24 cameo: %s" % [l for l, _ in odd])
        path = os.path.join(OUT_ROOT, "cameos_contact_sheet.png")
        contact_sheet(cameo_imgs, path, scale=3, cols=8)
        print("\ncontact sheet: %s  (%d cameos at 32x24)" % (path, len(cameo_imgs)))

    for label, scale, cols in (("clock", 2, 16), ("options", 1, 8),
                               ("mouse", 2, 16), ("tabs", 2, 4),
                               ("strip", 2, 4), ("stripup", 3, 4),
                               ("stripdn", 3, 4), ("power", 6, 4),
                               ("minigun", 3, 12), ("atomicup", 2, 4),
                               ("pips", 6, 8)):
        sub = os.path.join(args.out, label)
        if not os.path.isdir(sub):
            continue
        from PIL import Image
        files = sorted(f for f in os.listdir(sub) if f.endswith(".png"))
        imgs = [(f.split("_")[-1][:-4], Image.open(os.path.join(sub, f))) for f in files]
        if not imgs:
            continue
        path = os.path.join(OUT_ROOT, "%s_frames.png" % label)
        contact_sheet(imgs, path, scale=scale, cols=cols)
        print("frame sheet:   %s  (%d frames)" % (path, len(imgs)))

    with open(os.path.join(OUT_ROOT, "shapes.json"), "w") as fh:
        json.dump(report, fh, indent=2, sort_keys=True)
    print("report: %s" % os.path.join(OUT_ROOT, "shapes.json"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
