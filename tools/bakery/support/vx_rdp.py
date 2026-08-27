#!/usr/bin/env python3
"""VERIFY-ONLY: independent F3DEX2 interpreter with full RDP tile state.

Written from scratch for adversarial verification. Does NOT import the decode
agent's ti_*.py helpers. Only segments.SEGMENTS (the overlay table, which was
independently validated by geometry) is reused, plus a locally re-derived
co-residency resolver.
"""
import struct, sys, os, json

SEGMENTS = [
    ("ScriptModels",       0x00C6EA0, 0x00DA420, 0x80122B60),
    ("GameModelsTextures", 0x00DA420, 0x00FE210, 0x801369B0),
    ("GameModelsGeometry", 0x00FE210, 0x01643F0, 0x8015A7A0),
    ("CodeOverlay",        0x01643F0, 0x01B67D0, 0x801C2600),
    ("BriefingCode",       0x01B67D0, 0x020E400, 0x801C2600),
    ("Compress",           0x020E400, 0x021BB80, 0x80122B60),
    ("ReplayMission",      0x021BB80, 0x0224980, 0x80130830),
    ("FactionSel",         0x0224980, 0x0226360, 0x80130830),
    ("MapSel",             0x0226360, 0x0243B90, 0x80122B60),
    ("Score",              0x0243B90, 0x0250BF0, 0x80122B60),
    ("IntroSequence",      0x0250BF0, 0x0266700, 0x80122B60),
    ("JoyConfig",          0x0266700, 0x0267A50, 0x80122B60),
    ("DialogMenu",         0x0267A50, 0x026B350, 0x80130830),
    ("MovieTextures",      0x026B350, 0x028CCA0, 0x8015A7A0),
    ("Movie1",             0x028CCA0, 0x02D64A0, 0x8017C100),
    ("Movie2",             0x02D64A0, 0x030C250, 0x8017C100),
    ("Movie3",             0x030C250, 0x032D520, 0x8017C100),
    ("Movie4_1",           0x032D520, 0x03A1470, 0x8017C100),
    ("Movie5_1",           0x03A1470, 0x0442DA0, 0x8017C100),
    ("Movie6_1",           0x0442DA0, 0x04578A0, 0x8017C100),
]
BYNAME = {s[0]: s for s in SEGMENTS}

# Which overlays are simultaneously resident. A movie overlay group is
# {ScriptModels, GameModelsTextures, MovieTextures, MovieN}; the in-game group is
# {ScriptModels, GameModelsTextures, GameModelsGeometry, CodeOverlay}.
MOVIES = ["Movie1", "Movie2", "Movie3", "Movie4_1", "Movie5_1", "Movie6_1"]


def seg_for_rom(off):
    for s in SEGMENTS:
        if s[1] <= off < s[2]:
            return s
    return None


def resident_group(dl_off):
    """The overlay set live when this DL runs (my own reconstruction)."""
    own = seg_for_rom(dl_off)
    if own is None:
        return list(SEGMENTS)
    n = own[0]
    if n in MOVIES:
        return [BYNAME[x] for x in ("ScriptModels", "GameModelsTextures",
                                    "MovieTextures", n)]
    if n == "MovieTextures":
        return [BYNAME[x] for x in ("ScriptModels", "GameModelsTextures",
                                    "MovieTextures")]
    if n in ("GameModelsGeometry", "GameModelsTextures", "CodeOverlay"):
        return [BYNAME[x] for x in ("ScriptModels", "GameModelsTextures",
                                    "GameModelsGeometry", "CodeOverlay")]
    # standalone menu/intro overlays: themselves + the always-resident pools
    return [own] + [BYNAME[x] for x in ("ScriptModels", "GameModelsTextures")
                    if x != n]


def make_resolve(dl_off):
    grp = resident_group(dl_off)
    own = seg_for_rom(dl_off)
    order = ([own] if own in grp else []) + [s for s in grp if s is not own]

    def r(ram):
        for _, rs, re_, ra in order:
            if ra <= ram < ra + (re_ - rs):
                return ram - ra + rs
        return None
    return r


# ---------------------------------------------------------------- interpreter
FMT = {0: "RGBA", 1: "YUV", 2: "CI", 3: "IA", 4: "I"}
SIZ = {0: 4, 1: 8, 2: 16, 3: 32}


class Tile:
    __slots__ = ("fmt", "siz", "line", "tmem", "pal", "cmT", "maskT", "shiftT",
                 "cmS", "maskS", "shiftS", "uls", "ult", "lrs", "lrt")

    def __init__(self):
        self.fmt = self.siz = self.line = self.tmem = self.pal = 0
        self.cmT = self.maskT = self.shiftT = 0
        self.cmS = self.maskS = self.shiftS = 0
        self.uls = self.ult = self.lrs = self.lrt = 0

    def copy(self):
        t = Tile()
        for k in self.__slots__:
            setattr(t, k, getattr(self, k))
        return t


def read_vtx(d, p):
    x, y, z, fl, u, v = struct.unpack_from(">hhhHhh", d, p)
    r, g, b, a = d[p + 12:p + 16]
    return (x, y, z, u, v, r, g, b, a)


def walk(rom, dl_off, resolve, max_cmds=8000, depth=0):
    """Returns (verts, tris) where tris = [(i0,i1,i2, state)] and state is a dict
    describing the RDP texture binding in force for that triangle."""
    verts = []
    tris = []
    slots = [None] * 64
    tiles = [Tile() for _ in range(8)]
    timg = {"addr": None, "fmt": 0, "siz": 0, "width": 0}
    tlut_addr = None
    tlut_count = 0
    texscale = (0xFFFF, 0xFFFF)
    texon = True
    # The COLOUR COMBINER in force. Tracked because an untextured triangle's colour
    # depends entirely on it: some fold SHADE into PRIM, some take PRIM alone. Dropping
    # it meant every untextured PRIM triangle got the same treatment, which is how the
    # ATTACK cursor's red arrowheads baked to near-black. Additive: nothing that already
    # reads this dict is affected.
    combine = None
    # THE BLENDER / RENDER MODE in force, which this walker used to throw away. It is the
    # only thing that says a triangle is TRANSLUCENT: the cartridge marks those with
    # G_RM_AA_ZB_XLU_SURF (0x005049D8) through G_SETOTHERMODE_L, and the texture format
    # alone -- which is all the baker had -- cannot tell a canopy from a solid panel.
    # F3DEX2 packs (32 - shift - len) at bits 8..15 and (len - 1) at bits 0..7, so the
    # rendermode setter is exactly 0xE200001C: shift 3, length 29. That shift leaves bits
    # 0..2 alone, which is why no model display list can touch the alpha-compare bits.
    rendermode = None
    rendertile = 0
    loadmode = {}          # tile index -> "BLOCK"/"TILE"
    o = dl_off
    for _ in range(max_cmds):
        if o + 8 > len(rom):
            break
        w0, w1 = struct.unpack_from(">II", rom, o)
        o += 8
        op = w0 >> 24
        if op == 0x01:                                    # G_VTX
            numv = (w0 >> 12) & 0xFF
            end = (w0 >> 1) & 0x7F
            start = end - numv
            p = resolve(w1)
            if p is None or p < 0 or p + numv * 16 > len(rom):
                continue
            for i in range(numv):
                verts.append(read_vtx(rom, p + i * 16))
                if 0 <= start + i < 64:
                    slots[start + i] = len(verts) - 1
        elif op in (0x05, 0x06):                          # G_TRI1 / G_TRI2
            ws = (w0,) if op == 0x05 else (w0, w1)
            st = None
            for w in ws:
                t = [(w >> 17) & 0x7F, (w >> 9) & 0x7F, (w >> 1) & 0x7F]
                if all(0 <= i < 64 and slots[i] is not None for i in t):
                    if st is None:
                        tl = tiles[rendertile]
                        st = dict(fmt=tl.fmt, siz=tl.siz, line=tl.line,
                                  tmem=tl.tmem, pal=tl.pal,
                                  cmS=tl.cmS, cmT=tl.cmT,
                                  maskS=tl.maskS, maskT=tl.maskT,
                                  uls=tl.uls, ult=tl.ult, lrs=tl.lrs, lrt=tl.lrt,
                                  timg=timg["addr"], timg_fmt=timg["fmt"],
                                  timg_siz=timg["siz"], timg_w=timg["width"],
                                  tlut=tlut_addr, tlut_n=tlut_count,
                                  scaleS=texscale[0], scaleT=texscale[1],
                                  texon=texon,
                                  combine=combine,
                                  rendermode=rendermode,
                                  load=loadmode.get(rendertile))
                    tris.append((slots[t[0]], slots[t[1]], slots[t[2]], st))
        elif op == 0xFC:                                  # G_SETCOMBINE
            combine = (w0, w1)
        elif op == 0xE2:                                  # G_SETOTHERMODE_L
            # 0xE200001C is gsDPSetRenderMode; anything else with this opcode would be a
            # different field and must not be mistaken for the blender. There are 1006 of
            # the former in the ROM and no alpha-compare setters at all.
            if w0 == 0xE200001C:
                rendermode = w1
        elif op == 0xFD:                                  # G_SETTIMG
            timg = {"addr": w1, "fmt": (w0 >> 21) & 7, "siz": (w0 >> 19) & 3,
                    "width": (w0 & 0xFFF) + 1}
        elif op == 0xF5:                                  # G_SETTILE
            ti = (w1 >> 24) & 7
            t = tiles[ti]
            t.fmt = (w0 >> 21) & 7
            t.siz = (w0 >> 19) & 3
            t.line = (w0 >> 9) & 0x1FF
            t.tmem = w0 & 0x1FF
            t.pal = (w1 >> 20) & 0xF
            t.cmT = (w1 >> 18) & 3
            t.maskT = (w1 >> 14) & 0xF
            t.shiftT = (w1 >> 10) & 0xF
            t.cmS = (w1 >> 8) & 3
            t.maskS = (w1 >> 4) & 0xF
            t.shiftS = w1 & 0xF
        elif op == 0xF2:                                  # G_SETTILESIZE
            ti = (w1 >> 24) & 7
            t = tiles[ti]
            t.uls = (w0 >> 12) & 0xFFF
            t.ult = w0 & 0xFFF
            t.lrs = (w1 >> 12) & 0xFFF
            t.lrt = w1 & 0xFFF
        elif op == 0xF3:                                  # G_LOADBLOCK
            loadmode[(w1 >> 24) & 7] = "BLOCK"
        elif op == 0xF4:                                  # G_LOADTILE
            ti = (w1 >> 24) & 7
            loadmode[ti] = "TILE"
            t = tiles[ti]
            t.uls = (w0 >> 12) & 0xFFF
            t.ult = w0 & 0xFFF
            t.lrs = (w1 >> 12) & 0xFFF
            t.lrt = w1 & 0xFFF
        elif op == 0xF0:                                  # G_LOADTLUT
            tlut_addr = timg["addr"]
            tlut_count = ((w1 >> 14) & 0x3FF) + 1
        elif op == 0xD7:                                  # G_TEXTURE
            texscale = ((w1 >> 16) & 0xFFFF, w1 & 0xFFFF)
            rendertile = (w0 >> 8) & 7
            texon = ((w0 >> 1) & 0x7F) != 0 or True
        elif op == 0xDE:                                  # G_DL
            p = resolve(w1)
            if p is not None and depth < 4:
                sv, st = walk(rom, p, resolve, max_cmds, depth + 1)
                base = len(verts)
                verts.extend(sv)
                for a, b, c, s in st:
                    tris.append((a + base, b + base, c + base, s))
            if (w0 >> 16) & 0xFF == 1:                    # G_DL branch (no return)
                break
        elif op == 0xDF:                                  # G_ENDDL
            break
    return verts, tris


# ---------------------------------------------------------------- decoding
def rgba5551(v):
    r = (v >> 11) & 31
    g = (v >> 6) & 31
    b = (v >> 1) & 31
    a = 255 if (v & 1) else 0
    return ((r * 255 + 15) // 31, (g * 255 + 15) // 31, (b * 255 + 15) // 31, a)


# Textures we have already refused to read at the TMEM line stride, so the
# warning below fires once per (base, w, h) rather than once per baked triangle.
_STRIDE_WARNED = set()


def decode(rom, base, w, h, fmt, siz, tlut=None, pal=0, stride_texels=None):
    """base = ROM offset, returns list of rows of (r,g,b,a). Plain linear.

    stride_texels is IGNORED when it disagrees with w, and that is deliberate.
    The only caller (bake_pk4.pyc decode_tex) hands us the tile's TMEM `line`,
    not the DRAM image width:

        t = st['line'] * 64 // BPP[siz]
        if t >= w: stride = t

    but `line` is the TMEM DESTINATION stride and is rounded up to whole 64-bit
    words, while the RDP reads DRAM rows at the G_SETTIMG image width. For any
    tile narrower than one TMEM word (CI8 w<8, CI4 w<16, RGBA16 w<4) the two
    differ and the line stride walks off into unrelated texture memory.

    Proven on the missile body texture (DRAGON/MISSILE/BOMBLET, display list
    ROM 0x01051A0): G_SETTIMG at ROM 0x0105200 is FD480001 = CI/8b with width
    (w0 & 0xFFF) + 1 = 2, and G_SETTILE at ROM 0x0105208 is F5480200 with
    line = (w0 >> 9) & 0x1FF = 1, i.e. a line stride of 8 texels for a 2-texel
    image. At the image width the texture is a black nosecone, a white body and
    one house-coloured band (index 1: 841908 through the Nod TLUT at 0x99130,
    DEBD6B through the GDI one at 0x98F30); at the line stride it is a grey
    striped smear that never contains index 1 at all.
    """
    sw = w
    if stride_texels and stride_texels != w:
        key = (base, w, h)
        if key not in _STRIDE_WARNED:
            _STRIDE_WARNED.add(key)
            sys.stderr.write(
                "vx_rdp: ignoring TMEM line stride %d for the %dx%d tile at "
                "ROM %#x (DRAM rows are read at the G_SETTIMG image width)\n"
                % (stride_texels, w, h, base))
    out = []
    if fmt == 0 and siz == 2:            # RGBA5551
        rb = sw * 2
        for y in range(h):
            p = base + y * rb
            row = []
            for x in range(w):
                if p + x * 2 + 2 > len(rom):
                    row.append((0, 0, 0, 0)); continue
                row.append(rgba5551(struct.unpack_from(">H", rom, p + x * 2)[0]))
            out.append(row)
    elif fmt == 0 and siz == 3:          # RGBA8888
        rb = sw * 4
        for y in range(h):
            p = base + y * rb
            out.append([tuple(rom[p + x * 4:p + x * 4 + 4]) for x in range(w)])
    elif fmt == 2 and siz == 1:          # CI8
        rb = sw
        for y in range(h):
            p = base + y * rb
            row = []
            for x in range(w):
                i = rom[p + x] if p + x < len(rom) else 0
                row.append(tlut[i] if tlut else (i, i, i, 255))
            out.append(row)
    elif fmt == 2 and siz == 0:          # CI4
        rb = sw // 2
        for y in range(h):
            p = base + y * rb
            row = []
            for x in range(w):
                b = rom[p + x // 2] if p + x // 2 < len(rom) else 0
                i = (b >> 4) if x % 2 == 0 else (b & 15)
                i |= pal << 4
                row.append(tlut[i] if tlut else (i * 16, i * 16, i * 16, 255))
            out.append(row)
    elif fmt == 4 and siz == 1:          # I8
        rb = sw
        for y in range(h):
            p = base + y * rb
            out.append([(rom[p + x],) * 3 + (rom[p + x],) for x in range(w)])
    elif fmt == 4 and siz == 0:          # I4
        rb = sw // 2
        for y in range(h):
            p = base + y * rb
            row = []
            for x in range(w):
                b = rom[p + x // 2]
                i = (b >> 4) if x % 2 == 0 else (b & 15)
                i = i * 17
                row.append((i, i, i, i))
            out.append(row)
    elif fmt == 3 and siz == 1:          # IA8 (4/4)
        rb = sw
        for y in range(h):
            p = base + y * rb
            row = []
            for x in range(w):
                b = rom[p + x]
                i = (b >> 4) * 17; a = (b & 15) * 17
                row.append((i, i, i, a))
            out.append(row)
    elif fmt == 3 and siz == 2:          # IA16 (8/8)
        rb = sw * 2
        for y in range(h):
            p = base + y * rb
            row = []
            for x in range(w):
                i = rom[p + x * 2]; a = rom[p + x * 2 + 1]
                row.append((i, i, i, a))
            out.append(row)
    elif fmt == 3 and siz == 0:          # IA4 (3/1)
        rb = sw // 2
        for y in range(h):
            p = base + y * rb
            row = []
            for x in range(w):
                b = rom[p + x // 2]
                n = (b >> 4) if x % 2 == 0 else (b & 15)
                i = ((n >> 1) * 255) // 7; a = 255 if (n & 1) else 0
                row.append((i, i, i, a))
            out.append(row)
    else:
        return None
    return out


def read_tlut(rom, off, n=256):
    return [rgba5551(struct.unpack_from(">H", rom, off + i * 2)[0]) for i in range(n)]


def load_rom(p=None):
    # The cartridge is not in the repo. CNC3D_ROM names it; otherwise data/rom/.
    if p is None:
        _root = os.environ.get("CNC3D_ROOT") or os.path.abspath(
            os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
        p = os.environ.get("CNC3D_ROM") or os.path.join(_root, "data", "rom", "cnc_eu.z64")
    return open(p, "rb").read()
