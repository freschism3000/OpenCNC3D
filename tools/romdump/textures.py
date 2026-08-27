#!/usr/bin/env python3
"""
CNC3D — RDP texture state machine: per-display-list texture extraction + UV mapping.

Walks a display list tracking RDP tile state and returns, for every triangle, which
texture was bound. Textures are decoded to RGBA using the overlay-correct resolver in
segments.py (see the shadowing bug documented there -- without it every movie texture
decodes into display-list opcode bytes and comes out as coloured noise).

Commands consumed:
    G_SETTIMG     0xFD  w0 = FD | fmt<<21 | siz<<19 | (width-1) ; w1 = image RAM address
    G_SETTILE     0xF5  fmt<<21 | siz<<19 | line<<9 | tmem ; w1 = tile<<24 | palette<<20 | ...
    G_SETTILESIZE 0xF2  uls<<12|ult ; w1 = tile<<24 | lrs<<12|lrt   (all 10.2 fixed)
                        width = ((lrs-uls)>>2)+1, height = ((lrt-ult)>>2)+1
    G_LOADTLUT    0xF0  w1 = tile<<24 | count<<14   -> palette = most recent G_SETTIMG
    G_TEXTURE     0xD7  w1 = scaleS<<16 | scaleT

*** UV RULE (verified empirically, and the reasoning matters) ***
texel = u / 32, i.e. plain S10.5. But NOT because "scale is 1.0":
every tile in this ROM has shiftS=shiftT=15, which the RDP interprets as coord<<1 (x2),
and every G_TEXTURE is scaleS=scaleT=0x8000 (x0.5). The two cancel exactly. Proven
visually on a vehicle wheel: /32 puts one tire centred on the quad, /64 shows a cropped
quadrant, /16 tiles a 2x2 grid of tires.

Formats seen in this ROM: CI8+TLUT (most common), I8, RGBA5551, CI4, IA.
"""
import struct

G_VTX, G_TRI1, G_TRI2, G_ENDDL = 0x01, 0x05, 0x06, 0xDF
G_SETTIMG, G_SETTILE, G_SETTILESIZE, G_LOADTLUT, G_TEXTURE = 0xFD, 0xF5, 0xF2, 0xF0, 0xD7

FMT = {0: "RGBA", 1: "YUV", 2: "CI", 3: "IA", 4: "I"}

# The shared 256-entry RGBA5551 palette in resident .data. Most display lists never issue
# a G_LOADTLUT of their own -- the palette is loaded once at startup and left in TMEM --
# so CI textures must fall back to this. Verified byte-identical to the palettes the ROM
# genuinely does LOADTLUT at 0x028D470 / 0x032DB90 / 0x04022F0.
GLOBAL_TLUT = 0x99130

# Texture data is plain linear row-major in ROM; the N64's odd-row 64-bit interleave is a
# TMEM-side concern and must NOT be undone here (verified by A/B decoding).

def rgba5551(v):
    r = (v >> 11) & 0x1F; g = (v >> 6) & 0x1F; b = (v >> 1) & 0x1F; a = v & 1
    return ((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2), 255 if a else 0)

def decode_texture(rom, off, fmt, siz, w, h, pal_off=None):
    """Decode an N64 texture to a list of h rows of RGBA bytes. None if unsupported."""
    if w <= 0 or h <= 0 or w > 512 or h > 512: return None
    px = []
    try:
        if fmt == 0 and siz == 2:                      # RGBA5551
            need = w * h * 2
            if off + need > len(rom): return None
            for y in range(h):
                row = bytearray()
                for x in range(w):
                    v = struct.unpack_from(">H", rom, off + (y * w + x) * 2)[0]
                    row += bytes(rgba5551(v))
                px.append(bytes(row))
        elif fmt == 2 and siz == 1:                    # CI8 + TLUT
            if pal_off is None: pal_off = GLOBAL_TLUT
            if off + w * h > len(rom): return None
            pal = [rgba5551(struct.unpack_from(">H", rom, pal_off + i * 2)[0]) for i in range(256)]
            for y in range(h):
                row = bytearray()
                for x in range(w):
                    row += bytes(pal[rom[off + y * w + x]])
                px.append(bytes(row))
        elif fmt == 2 and siz == 0:                    # CI4 + TLUT
            if pal_off is None: pal_off = GLOBAL_TLUT
            pal = [rgba5551(struct.unpack_from(">H", rom, pal_off + i * 2)[0]) for i in range(16)]
            for y in range(h):
                row = bytearray()
                for x in range(w):
                    b = rom[off + (y * w + x) // 2]
                    row += bytes(pal[(b >> 4) if (x & 1) == 0 else (b & 0xF)])
                px.append(bytes(row))
        elif fmt == 4 and siz == 1:                    # I8
            # The RDP replicates I into R,G,B *and A* for format I. Forcing alpha to 255
            # here is what made every building's ground shadow a solid black slab: the
            # shadow spans use combiner PRIM(black) with A = TEXEL0_A off a 16x16 I8 blob
            # (e.g. the shared shadow at ROM 0x00E8670 used by 30+ village models), and with
            # alpha pinned at 255 the blob covers the whole quad instead of fading out.
            if off + w * h > len(rom): return None
            for y in range(h):
                row = bytearray()
                for x in range(w):
                    i = rom[off + y * w + x]
                    row += bytes((i, i, i, i))
                px.append(bytes(row))
        elif fmt == 4 and siz == 0:                    # I4
            if off + (w * h + 1) // 2 > len(rom): return None
            for y in range(h):
                row = bytearray()
                for x in range(w):
                    b = rom[off + (y * w + x) // 2]
                    i = ((b >> 4) if (x & 1) == 0 else (b & 0xF)) * 17
                    row += bytes((i, i, i, i))
                px.append(bytes(row))
        elif fmt == 3 and siz == 1:                    # IA8 (4-bit intensity, 4-bit alpha)
            if off + w * h > len(rom): return None
            for y in range(h):
                row = bytearray()
                for x in range(w):
                    b = rom[off + y * w + x]
                    i = (b >> 4) * 17; a = (b & 0xF) * 17
                    row += bytes((i, i, i, a))
                px.append(bytes(row))
        else:
            return None
    except Exception:
        return None
    return px

G_MODIFYVTX, G_LOADBLOCK, G_LOADTILE = 0x02, 0xF3, 0xF4
G_SETPRIMCOLOR, G_SETCOMBINE, G_GEOMETRYMODE = 0xFA, 0xFC, 0xD9

# *** G_TEXTURE FIELD LAYOUT (this was decoded wrong for a long time) ***
# F3DEX2 gsSPTexture(s,t,level,tile,on):
#     w0 = G_TEXTURE<<24 | level<<11 | tile<<8 | on<<1     w1 = s<<16 | t
# so tile = (w0>>8)&7 and on = (w0>>1)&0x7F. Reading tile as (w0&7) makes every
# "D7 00 0k 02" look like tile=2/on=1 and hides the fact that the ROM preloads several
# tiles and then switches the RENDER TILE between draw calls. Quoted from V02
# (ROM 0x011DE00), which loads four textures and then draws four groups:
#     011DF00: D7000002 -> tile 0 on   011DF58: D7000102 -> tile 1 on
#     011DFC8: D7000202 -> tile 2 on   011E008: D7000302 -> tile 3 on
# and from WOOD (ROM 0x0100250): 0100270: D7000000 -> on = 0, texturing DISABLED.
def _texture_fields(w0):
    return (w0 >> 8) & 7, (w0 >> 1) & 0x7F      # (render tile, on)

# Combiner classes actually present in the model display lists (only six distinct
# G_SETCOMBINE words exist across all 113 named models). Decoded cycle-1 output:
COMBINERS = {
    (0xFCFFFFFF, 0xFFFCF279): "tex",        # RGB = TEXEL0            A = TEXEL0_A
    (0xFC127E24, 0xFFFFF3F9): "tex_shade",  # RGB = TEXEL0 * SHADE    A = TEXEL0_A
    (0xFCFFFFFF, 0xFFFDF2F9): "prim_texa",  # RGB = PRIM              A = TEXEL0_A  <- shadows
    (0xFC327E64, 0xFFFFF7FB): "prim_shade", # RGB = PRIM * SHADE      A = PRIM_A
    (0xFCFFFFFF, 0xFFFDF6FB): "prim",       # RGB = PRIM              A = PRIM_A
    (0xFCFF97FF, 0xFF2CFE7F): "tex_primalpha",  # RGB = TEXEL0            A = TEXEL0_A*PRIM_A
}

# G_MODIFYVTX writes the ST value the RSP would have produced AFTER the G_TEXTURE scale,
# i.e. half of what the vertex buffer holds. Everything else in this file works in raw
# vertex-buffer units (texel = u/32), so a modified ST must be doubled to live in the same
# space: texel = val*2/32 = val/16. Proven on V02 (ROM 0x011DF30 "02140000 01000080"):
# tile 0 there is 16x8 and the intended corner is (1,1); 0x100/16 = 16 texels = 1.0 and
# 0x080/16 = 8 texels = 1.0, whereas /32 would put every corner at exactly 0.5.
MODIFY_ST_MUL = 2

# Only three render modes appear across all 113 models (G_SETOTHERMODE_L, shift 3 len 29):
#   00552078  AA_ZB_OPA_SURF   x142  opaque
#   00553078  AA_ZB_TEX_EDGE   x139  CVG_X_ALPHA -> alpha punch-through (tree leaves)
#   005049D8  AA_ZB_XLU_SURF   x80   ZMODE_XLU + FORCE_BL -> real alpha blend (shadows)
# Which one is in force decides whether a texture's alpha is ignored, cut, or blended, so
# it has to travel with the face or shadows and foliage cannot both be right.
RENDERMODE = {0x00552078: "opa", 0x00553078: "edge", 0x005049D8: "xlu"}
G_SETOTHERMODE_L = 0xE2


def walk_rdp(rom, dl_off, resolve, max_cmds=4000, state_in=None):
    """Walk a display list as an RDP state machine.

    Returns (verts, faces, state_of_face, textures, state_out) where each entry of
    state_of_face is a hashable tuple (texKey|None, primRGBA, combinerClass, cull).
    """
    verts, faces, sof = [], [], []
    textures = {}
    NSLOT = 64
    slots = [None] * NSLOT
    drawn = set()                 # vertex indices already referenced by an emitted face
    st = dict(state_in) if state_in else dict(
        cur_img=None, pal=None, tiles=[None]*8, timg=[None]*8, tsize=[None]*8,
        tpal=[None]*8, key=[None]*8, rtile=0, on=0, prim=(255, 255, 255, 255),
        comb="tex_shade", cull=0, rm="opa")
    for k in ("tiles", "timg", "tsize", "tpal", "key"):
        st[k] = list(st[k])

    def face_state():
        tk = st["key"][st["rtile"]] if st["on"] else None
        return (tk, st["prim"], st["comb"], st["cull"], st["rm"])

    o = dl_off
    for _ in range(max_cmds):
        if o + 8 > len(rom): break
        w0, w1 = struct.unpack_from(">II", rom, o); o += 8
        op = w0 >> 24
        if op == G_VTX:
            numv = (w0 >> 12) & 0xFF
            start = ((w0 >> 1) & 0x7F) - numv
            p = resolve(w1, numv)
            if p is None or p < 0 or p + numv * 16 > len(rom): continue
            for i in range(numv):
                x, y, z, fl, u, v = struct.unpack_from(">hhhHhh", rom, p + i * 16)
                r, g, b, a = rom[p + i*16 + 12: p + i*16 + 16]
                verts.append(dict(x=x, y=y, z=z, u=u, v=v, r=r, g=g, b=b, a=a))
                if 0 <= start + i < NSLOT:
                    slots[start + i] = len(verts) - 1
        elif op == G_MODIFYVTX:
            where = (w0 >> 16) & 0xFF
            si = (w0 & 0xFFFF) // 2
            if where == 0x14 and 0 <= si < NSLOT and slots[si] is not None:
                vi = slots[si]
                if vi in drawn:            # already used -- fork it, never rewrite history
                    verts.append(dict(verts[vi])); vi = len(verts) - 1; slots[si] = vi
                s = struct.unpack(">h", struct.pack(">H", w1 >> 16))[0]
                t = struct.unpack(">h", struct.pack(">H", w1 & 0xFFFF))[0]
                verts[vi]["u"] = s * MODIFY_ST_MUL
                verts[vi]["v"] = t * MODIFY_ST_MUL
        elif op in (G_TRI1, G_TRI2):
            for w in ((w0,) if op == G_TRI1 else (w0, w1)):
                t = [(w >> 17) & 0x7F, (w >> 9) & 0x7F, (w >> 1) & 0x7F]
                if all(0 <= i < NSLOT and slots[i] is not None for i in t):
                    f = [slots[i] for i in t]
                    faces.append(f); sof.append(face_state()); drawn.update(f)
        elif op == G_TEXTURE:
            st["rtile"], st["on"] = _texture_fields(w0)
        elif op == G_SETPRIMCOLOR:
            st["prim"] = ((w1 >> 24) & 0xFF, (w1 >> 16) & 0xFF, (w1 >> 8) & 0xFF, w1 & 0xFF)
        elif op == G_SETOTHERMODE_L:
            st["rm"] = RENDERMODE.get(w1, st["rm"])
        elif op == G_SETCOMBINE:
            st["comb"] = COMBINERS.get((w0, w1), "tex_shade")
        elif op == G_GEOMETRYMODE:
            clr = (~w0) & 0xFFFFFF
            st["cull"] = (st["cull"] & ~clr) | (w1 & 0x600)
        elif op == G_SETTILE:
            ti = (w1 >> 24) & 7
            # w1 = tile<<24 | palette<<20 | cmT<<18 | maskT<<14 | shiftT<<10
            #                             | cmS<<8  | maskS<<4  | shiftS
            st["tiles"][ti] = ((w0 >> 21) & 7, (w0 >> 19) & 3, (w1 >> 20) & 0xF,
                               (w1 >> 8) & 3, (w1 >> 4) & 0xF,      # cmS, maskS
                               (w1 >> 18) & 3, (w1 >> 14) & 0xF)    # cmT, maskT
        elif op == G_SETTIMG:
            st["cur_img"] = w1
        elif op in (G_LOADBLOCK, G_LOADTILE):
            ti = (w1 >> 24) & 7
            st["timg"][ti] = st["cur_img"]
            st["tpal"][ti] = st["pal"]
        elif op == G_LOADTLUT:
            if st["cur_img"] is not None:
                st["pal"] = resolve(st["cur_img"])
        elif op == G_SETTILESIZE:
            ti = (w1 >> 24) & 7
            uls = (w0 >> 12) & 0xFFF; ult = w0 & 0xFFF
            lrs = (w1 >> 12) & 0xFFF; lrt = w1 & 0xFFF
            st["tsize"][ti] = (((lrs - uls) >> 2) + 1, ((lrt - ult) >> 2) + 1)
            ram = st["timg"][ti]
            desc = st["tiles"][ti]
            if ram is not None and desc is not None:
                p = resolve(ram)
                if p is not None:
                    w, h = st["tsize"][ti]
                    pal = st["tpal"][ti] if st["tpal"][ti] is not None else st["pal"]
                    cms, masks, cmt, maskt = desc[3], desc[4], desc[5], desc[6]
                    key = (p, desc[0], desc[1], w, h, pal, cms, masks, cmt, maskt)
                    textures[key] = dict(rom=p, fmt=desc[0], siz=desc[1], w=w, h=h, pal=pal,
                                         cms=cms, masks=masks, cmt=cmt, maskt=maskt)
                    st["key"][ti] = key
        elif op == G_ENDDL:
            break
    return verts, faces, sof, textures, st


def walk_textured(rom, dl_off, resolve, max_cmds=4000, state_in=None):
    """Backwards-compatible view of walk_rdp: tex_of_face carries only the texture key."""
    v, f, sof, tx, st = walk_rdp(rom, dl_off, resolve, max_cmds, state_in)
    return v, f, [s[0] for s in sof], tx, st

def uv_norm(u, v, w, h):
    """S10.5 texel coords -> normalized UV (see the UV RULE note above)."""
    return (u / 32.0) / max(w, 1), (v / 32.0) / max(h, 1)


# *** WRAP / MIRROR / CLAMP (this was ignored entirely and it is not cosmetic) ***
# G_SETTILE carries cmS/cmT (bit0 = MIRROR, bit1 = CLAMP) and maskS/maskT (wrap period is
# 2^mask texels; mask 0 disables wrapping and forces a clamp). Across the 113 named models
# the ROM issues 880 G_SETTILE commands and 261 of them ask for CLAMP, 50-odd for MIRROR;
# quoted counts from the survey, e.g. V01 (ROM 0x011DB40) "F5 ... 00 02 02 0F ..." -> cmS =
# cmT = 2 = G_TX_CLAMP. Sampling everything with a plain modulo turns every clamped decal
# into a tiled one: TMPL's 16x16 stripe runs u = -12.9..127.1 and comes out as eight
# repeats of a texture the hardware would have smeared once.
CM_MIRROR, CM_CLAMP = 1, 2


def wrap_texel(t, size, cm, mask):
    """Apply one axis of the RDP tile addressing rule to an integer texel coordinate."""
    n = (1 << mask) if mask else 0
    if cm & CM_CLAMP or not n:
        return 0 if t < 0 else (size - 1 if t > size - 1 else t)
    if cm & CM_MIRROR:
        p = t % (2 * n)
        t = p if p < n else (2 * n - 1 - p)
    else:
        t = t % n
    return t if t < size else size - 1
