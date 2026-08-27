#!/usr/bin/env python3
"""
CNC3D — F3DEX2 display-list -> OBJ mesh exporter.

Geometry in the N64 build is a display list plus a block of 16-byte Vtx structs.
The DL references vertices by *RAM* address, so we first recover the overlay delta
(D = RAM - ROM) by locating the vertex block (see f3d.py / find_vtx_block), then walk
the DL maintaining the RSP's 32-slot vertex buffer.

Commands used:
    G_VTX  (0x01)  w0 = 01 | numv<<12 | end<<1 ; w1 = RAM address of Vtx array
                   loads numv vertices into slots [end-numv, end)
    G_TRI1 (0x05)  w0 = 05 aa bb cc          -> tri (aa/2, bb/2, cc/2)
    G_TRI2 (0x06)  w0 = 06 aa bb cc, w1 = 00 dd ee ff  -> two tris
    G_ENDDL(0xDF)  end

Vtx (16 bytes, big-endian):
    s16 x, y, z ; u16 flag ; s16 u, v ; u8 r, g, b, a
"""
import sys, struct, json, bisect, collections

G_VTX, G_TRI1, G_TRI2, G_ENDDL, G_SETTIMG = 0x01, 0x05, 0x06, 0xDF, 0xFD

def read_vtx(d, p):
    x, y, z, fl, u, v = struct.unpack_from(">hhhHhh", d, p)
    r, g, b, a = d[p+12:p+16]
    return dict(x=x, y=y, z=z, u=u, v=v, r=r, g=g, b=b, a=a)

def vtx_block_ok(d, p, n):
    if p < 0 or p + n*16 > len(d): return False
    seen = set(); span = 0
    for i in range(n):
        x, y, z, fl, u, v = struct.unpack_from(">hhhHhh", d, p + i*16)
        if fl != 0: return False
        if max(abs(x), abs(y), abs(z)) > 20000: return False
        span = max(span, abs(x), abs(y), abs(z)); seen.add((x, y, z))
    return span >= 8 and len(seen) >= max(3, n // 3)

def walk(d, dl_off, delta, max_cmds=4000):
    """Walk a DL, returning (vertices, faces) with faces indexing `vertices`.

    `delta` is either an int (single overlay: rom = ram - delta) or a callable
    resolve(ram, numv) -> rom offset or None. The callable form is required for
    overlays that are co-resident with others (a movie display list may point into
    ScriptModels / GameModelsTextures / MovieTextures as well as its own segment),
    where one delta cannot resolve every G_VTX.

    A G_VTX whose address cannot be resolved is SKIPPED, not fatal: dl2.json's scan
    contains a handful of misdetected display-list starts whose first word is a bogus
    G_VTX (ram 0x00008100 / 0xFFFFFF00). Aborting there silently produced empty meshes.
    Triangles referencing an unfilled slot are dropped by the bounds check below.
    """
    resolve = delta if callable(delta) else (lambda a, n: a - delta)
    verts, faces = [], []
    # RSP vertex buffer -> index into verts. F3DEX2 nominally has 32 slots, but some
    # streams here index higher, so allow 64 and bounds-check every access.
    NSLOT = 64
    slots = [None]*NSLOT
    def ok(t): return all(0 <= i < NSLOT and slots[i] is not None for i in t)
    o = dl_off
    for _ in range(max_cmds):
        w0, w1 = struct.unpack_from(">II", d, o); o += 8
        op = w0 >> 24
        if op == G_VTX:
            numv = (w0 >> 12) & 0xFF
            end = (w0 >> 1) & 0x7F
            start = end - numv
            p = resolve(w1, numv)
            if p is None or p < 0 or p + numv*16 > len(d): continue
            for i in range(numv):
                verts.append(read_vtx(d, p + i*16))
                if 0 <= start + i < NSLOT:
                    slots[start + i] = len(verts) - 1
        elif op == G_TRI1:
            t = [(w0 >> 17) & 0x7F, (w0 >> 9) & 0x7F, (w0 >> 1) & 0x7F]
            if ok(t):
                faces.append([slots[i] for i in t])
        elif op == G_TRI2:
            for w in (w0, w1):
                t = [(w >> 17) & 0x7F, (w >> 9) & 0x7F, (w >> 1) & 0x7F]
                if ok(t):
                    faces.append([slots[i] for i in t])
        elif op == G_ENDDL:
            break
    return verts, faces

def write_obj(path, verts, faces, name="model", tile=None):
    """Write an OBJ. `tile` = (tex_w, tex_h) of the bound texture.

    UV normalization is (u/32)/tex_w -- the /32 is S10.5, the /tex_w is the tile size.
    An earlier version wrote `u/1024.0`, which silently hardcodes a 32x32 tile
    (32 texels * 32). That is right for the 110 32x32 groups and WRONG for every other
    size: 16x16 came out at 0.5, 2x2 at 0.0625, 64x16 at 2.0. Pass the real tile size.
    With tile=None the UVs are left in raw texels (u/32) so nothing is silently scaled.
    """
    tw, th = tile if tile else (1, 1)
    with open(path, "w") as f:
        f.write(f"# CNC3D - extracted from Command & Conquer (N64)\no {name}\n")
        for v in verts:
            f.write(f"v {v['x']} {v['y']} {v['z']}\n")
        for v in verts:
            f.write(f"vt {(v['u']/32.0)/tw:.6f} {(v['v']/32.0)/th:.6f}\n")
        for a, b, c in faces:
            f.write(f"f {a+1}/{a+1} {b+1}/{b+1} {c+1}/{c+1}\n")

def find_vtx_block(d, vcmds, limit=None, near=None, window=0x80000):
    """Locate the ROM position of a DL's vertex block from its relative layout.

    Every G_VTX in a list points into one contiguous block, so the *relative* spacing
    of the arrays is known even though the absolute base is not. Slide that pattern
    over the ROM and accept the position where every array parses as valid Vtx.

    `near` (the DL's own ROM offset) restricts the search to +/-`window` around it.
    Measured vertex blocks sit within ~0x11000 of their display list, so a local
    window is both far faster and much less prone to a false match elsewhere in the
    ROM -- blind whole-ROM matching produced garbled meshes.
    """
    A0 = vcmds[0][2]
    rel = [(a - A0, n) for _, n, a in vcmds]
    base = min(r for r, _ in rel)
    rel = [(r - base, n) for r, n in rel]
    n0 = rel[0][1]
    lo, hi = 0, (limit or len(d))
    if near is not None:
        lo = max(0, near - window); hi = min(hi, near + window)
    for P in range(lo - (lo % 16), hi, 16):
        if not vtx_block_ok(d, P, n0): continue
        if all(vtx_block_ok(d, P + r, n) for r, n in rel):
            return P, A0 + base - P      # (rom_pos, delta)
    return None, None

if __name__ == "__main__":
    rom = open(sys.argv[1], "rb").read()
    cache = json.load(open(sys.argv[2]))
    outdir = sys.argv[3]
    import os; os.makedirs(outdir, exist_ok=True)
    starts = sorted(cache["starts"])
    owner = collections.defaultdict(list)
    for off, n, a in cache["vtx"]:
        i = bisect.bisect_right(starts, off) - 1
        if i >= 0: owner[starts[i]].append((off, n, a))
    ranked = sorted(owner.items(), key=lambda kv: -sum(n for _, n, _ in kv[1]))
    want = int(sys.argv[4]) if len(sys.argv) > 4 else 12
    done = 0
    for s, vs in ranked:
        if done >= want: break
        P, D = find_vtx_block(rom, vs)
        if P is None: continue
        verts, faces = walk(rom, s, D)
        if len(faces) < 4: continue
        name = f"dl_{s:07X}"
        write_obj(os.path.join(outdir, name + ".obj"), verts, faces, name)
        print(f"{name}: vtx_block=0x{P:07X} delta=0x{D:08X} verts={len(verts)} tris={len(faces)}")
        done += 1
