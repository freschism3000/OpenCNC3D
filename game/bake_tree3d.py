#!/usr/bin/env python3
"""Turn a photogrammetry-grade glTF tree into a game tree of about three thousand
triangles that still reads as a tree.

WHY THE SOURCE CANNOT SIMPLY BE DECIMATED. island_tree_01 is 1,599,403 triangles:
34,787 of trunk, 504,584 of fine branching and 1,060,032 of leaves, and the leaves are
solid modelled geometry, one leaf per pair of triangles, rather than planes with a cut
out. A Tiberian Dawn map carries dozens of trees. Collapsing a million solid leaves onto
a budget gives a green lump, because the detail IS the leaves and there is no smaller
surface hiding inside them.

SO THE TREE IS REBUILT, the way a game tree is built, and this file is that rebuild.

  the WOOD  is the trunk and its limbs, vertex-clustered to a few hundred triangles and
            then RE-UNWRAPPED cylindrically onto its own tiling bark texture. The fine
            twig primitive is dropped: cluster it and you get a lump where the branching
            was, not a thinner branch.

  the LEAVES are thrown away and rebuilt as alpha-cut CARDS, one or two quads standing
            where each clump of foliage stood.

  the SPRAYS on those cards are COMPOSED, not cropped. This is the correction that
            matters most, and the first build got it wrong: a Poly Haven leaf atlas holds
            about TEN SEPARATE LEAVES at roughly 200x400 texels each, because the source
            mesh uses it one leaf per quad. Cropping a window of it puts ONE enormous
            leaf on a card, which is exactly why the first build read as confetti. The
            leaves are segmented out of the atlas and dozens of them are scattered,
            rotated, scaled and shaded into each spray, with real gaps between them.

  the LIGHT is baked. See ao_bake below for the measurement showing why the sun shadow
            map cannot shadow a canopy against itself at this scale, and why the inside
            of a canopy therefore has to be told that it is inside.

Nothing here is decoded from the cartridge. The console draws a tree as its own low-poly
model, that model is what CLASSIC and Win98 keep drawing, and there is no higher-detail
original in the ROM for this to be faithful to. The source is CC0.
"""
import argparse, json, os, struct, sys, zlib
import numpy as np
from PIL import Image


# ---- glTF 2.0, enough of it to read a Poly Haven model ---------------------------
COMP = {5120: ('b', 1), 5121: ('B', 1), 5122: ('h', 2), 5123: ('H', 2),
        5125: ('I', 4), 5126: ('f', 4)}
NCOMP = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}


def read_accessor(g, buf, idx):
    a = g['accessors'][idx]
    bv = g['bufferViews'][a['bufferView']]
    fmt, sz = COMP[a['componentType']]
    n = NCOMP[a['type']]
    off = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
    stride = bv.get('byteStride') or (sz * n)
    count = a['count']
    raw = np.frombuffer(buf, dtype=np.uint8, count=stride * (count - 1) + sz * n, offset=off)
    view = np.lib.stride_tricks.as_strided(raw, shape=(count, sz * n), strides=(stride, 1))
    return view.copy().view(np.dtype(fmt)).reshape(count, n)


def node_matrix(node):
    if 'matrix' in node:
        return np.array(node['matrix'], dtype=np.float64).reshape(4, 4).T
    m = np.eye(4)
    if 'scale' in node:
        m = np.diag(list(node['scale']) + [1.0]) @ m
    if 'rotation' in node:
        x, y, z, w = node['rotation']
        r = np.array([
            [1-2*(y*y+z*z), 2*(x*y-z*w),   2*(x*z+y*w),   0],
            [2*(x*y+z*w),   1-2*(x*x+z*z), 2*(y*z-x*w),   0],
            [2*(x*z-y*w),   2*(y*z+x*w),   1-2*(x*x+y*y), 0],
            [0, 0, 0, 1]], dtype=np.float64)
        m = r @ m
    if 'translation' in node:
        t = np.eye(4); t[:3, 3] = node['translation']
        m = t @ m
    return m


def load_gltf(path, aux=()):
    """[(pos, nrm, uv, material, indices)] in world space, one per primitive.

    A material carries every map the bake needs, resolved by NAME against the files
    sitting beside the model: Poly Haven ships glTF with JPG colour, which cannot hold an
    alpha channel, so a cut-out material's alpha arrives as its own PNG and the glTF says
    only alphaMode BLEND. Reading the colour without that alpha samples the atlas's black
    backing on three texels out of four."""
    g = json.load(open(path))
    base = os.path.dirname(path)
    buf = open(os.path.join(base, g['buffers'][0]['uri']), 'rb').read()

    def beside(stem, kind):
        for f in aux:
            b = os.path.basename(f)
            if b.startswith(stem) and kind in b:
                return f
        return None

    mats = {}
    for mi, mat in enumerate(g.get('materials', [])):
        pbr = mat.get('pbrMetallicRoughness', {})
        bct = pbr.get('baseColorTexture', {})
        ti = bct.get('index')
        if ti is None:
            continue
        img = g['images'][g['textures'][ti]['source']].get('uri')
        if not img:
            continue
        stem = os.path.basename(img).split('_diff')[0]
        xf = bct.get('extensions', {}).get('KHR_texture_transform', {})
        nt = mat.get('normalTexture', {}).get('index')
        nrm_uri = g['images'][g['textures'][nt]['source']].get('uri') if nt is not None else None
        mt = pbr.get('metallicRoughnessTexture', {}).get('index')
        arm_uri = g['images'][g['textures'][mt]['source']].get('uri') if mt is not None else None
        mats[mi] = dict(
            name=mat.get('name', ''), stem=stem,
            tex=os.path.join(base, img),
            nrm=os.path.join(base, nrm_uri) if nrm_uri else None,
            arm=os.path.join(base, arm_uri) if arm_uri else None,
            alpha=(beside(stem, 'alpha') if mat.get('alphaMode') in ('BLEND', 'MASK') else None),
            uvoff=xf.get('offset', [0.0, 0.0]), uvscale=xf.get('scale', [1.0, 1.0]))

    prims = []

    def walk(ni, parent, top):
        node = g['nodes'][ni]
        m = parent @ node_matrix(node)
        if 'mesh' in node:
            for p in g['meshes'][node['mesh']]['primitives']:
                pos = read_accessor(g, buf, p['attributes']['POSITION']).astype(np.float64)
                nrm = (read_accessor(g, buf, p['attributes']['NORMAL']).astype(np.float64)
                       if 'NORMAL' in p['attributes'] else np.zeros_like(pos))
                uv = (read_accessor(g, buf, p['attributes']['TEXCOORD_0']).astype(np.float64)
                      if 'TEXCOORD_0' in p['attributes'] else np.zeros((len(pos), 2)))
                idx = (read_accessor(g, buf, p['indices']).astype(np.int64).ravel()
                       if 'indices' in p else np.arange(len(pos)))
                pos = (m[:3, :3] @ pos.T).T + m[:3, 3]
                nrm = (m[:3, :3] @ nrm.T).T
                prims.append((pos, nrm, uv, mats.get(p.get('material')), idx, top))
        for c in node.get('children', []):
            walk(c, m, top)

    # THE TOP-LEVEL NODE IS THE TREE. fir_tree_01 is three separate firs in one file,
    # 18.9, 14.1 and 14.5 metres, standing side by side; concatenating them the way a
    # single-tree asset can be concatenated would weld them into one 26 metre blob. Each
    # top-level node is baked as its own model and the pack carries all three.
    for t, ni in enumerate(g['scenes'][g.get('scene', 0)]['nodes']):
        walk(ni, np.eye(4), t)
    return prims


# ---------------------------------------------------------------------------------
#  FBX, which is what a tree AUTHORED for a game arrives as.
#
#  Poly Haven ships glTF; the Unity Asset Store and its neighbours ship FBX, and there is
#  no FBX library on this machine and no Blender to convert with. fbx_read.py reads the
#  binary container directly and this turns what comes back into the same primitive
#  tuples load_gltf returns, so everything below neither knows nor cares which it got.
#
#  THE ALPHA USUALLY LIVES IN THE DIFFUSE. A scan ships its cut-out as a separate map
#  because its colour is a JPEG and JPEG has no alpha. A game asset ships a PNG or TGA
#  whose own alpha IS the cut-out, which is the whole convention alpha-cut foliage is
#  built on, so a material whose diffuse carries real alpha variation is foliage even
#  when no separate mask exists anywhere.
# ---------------------------------------------------------------------------------
def load_fbx(path, aux=()):
    import fbx_read
    ms, root = fbx_read.meshes(path)
    slots = fbx_read.textures(root)
    base = os.path.dirname(path) or '.'

    def resolve(fn):
        if not fn:
            return None
        for cand in aux:
            if os.path.basename(cand).lower() == fn.lower():
                return cand
        for d in (base, os.path.join(base, 'textures'), os.path.join(base, 'Textures')):
            f = os.path.join(d, fn)
            if os.path.exists(f):
                return f
        return None

    def diffuse_carries_alpha(f):
        """Real variation, not a fully opaque alpha channel that happens to exist."""
        if not f:
            return False
        try:
            im = Image.open(f)
            if im.mode not in ('RGBA', 'LA', 'PA'):
                return False
            a = np.asarray(im.convert('RGBA'))[..., 3]
            return bool((a < 200).mean() > 0.02)
        except Exception:
            return False

    mats = {}
    for name, sl in slots.items():
        tex = resolve(sl.get('DiffuseColor'))
        alpha = resolve(sl.get('TransparencyFactor') or sl.get('TransparentColor'))
        if alpha is None and diffuse_carries_alpha(tex):
            alpha = tex
        mats[name] = dict(name=name, stem=name, tex=tex, alpha=alpha,
                          nrm=resolve(sl.get('NormalMap') or sl.get('Bump')),
                          arm=resolve(sl.get('ShininessExponent')
                                      or sl.get('SpecularFactor')),
                          uvoff=[0.0, 0.0], uvscale=[1.0, 1.0])
    ordered = list(mats.values())

    prims = []
    for mi, m in enumerate(ms):
        if not len(m['T']):
            continue
        P, N, UV, T = m['P'], m['N'], m['UV'], m['T']
        if N is None:
            N = np.zeros_like(P)
        if UV is None:
            UV = np.zeros((len(P), 2), dtype=np.float32)
        mpp = m['mat_per_poly']
        groups = ([(k, T[m['poly_of_pv'][T[:, 0]] == k]) for k in np.unique(mpp)]
                  if mpp is not None else [(0, T)])
        for k, tt in groups:
            if not len(tt):
                continue
            used = np.unique(tt)
            remap = -np.ones(len(P), dtype=np.int64)
            remap[used] = np.arange(len(used))
            mat = ordered[int(k)] if int(k) < len(ordered) else (ordered[0] if ordered else None)
            prims.append((P[used].astype(np.float64), N[used].astype(np.float64),
                          UV[used].astype(np.float64), mat,
                          remap[tt].ravel(), mi))
    return prims


def load_model(path, aux=()):
    """Whichever of the two formats this is."""
    return (load_fbx(path, aux) if path.lower().endswith('.fbx')
            else load_gltf(path, aux))


# ---------------------------------------------------------------------------------
#  THE WOOD: vertex clustering, which is the decimation that cannot fold a tree flat.
#
#  Quadric edge collapse gives a better surface for the same count, but it needs a
#  half-edge structure over half a million triangles and it happily welds two branches
#  that pass close by. Clustering on a grid cannot: two branches in different cells stay
#  different, and the grid is the only parameter.
#
#  THE SOURCE UVs ARE NOT CARRIED THROUGH, and that was a real bug. Averaging them over a
#  cluster mixes texels from opposite sides of the trunk's unwrap, and squeezing the
#  result into half of a shared sheet put grey rock on the trunk: in game it read as a
#  boulder under a bush rather than as bark. The bark is re-unwrapped cylindrically
#  below, onto its own tiling texture, which is what bark wants anyway.
# ---------------------------------------------------------------------------------
def cluster_decimate(pos, nrm, tri, target_tris):
    lo, hi = pos.min(0), pos.max(0)
    ext = np.maximum(hi - lo, 1e-6)
    g = 8
    best = None
    for _ in range(24):
        cell = np.floor((pos - lo) / ext * (g - 1e-6)).astype(np.int64)
        key = (cell[:, 0] * g + cell[:, 1]) * g + cell[:, 2]
        uniq, inv = np.unique(key, return_inverse=True)
        t = inv[tri]
        ok = (t[:, 0] != t[:, 1]) & (t[:, 1] != t[:, 2]) & (t[:, 0] != t[:, 2])
        t = np.unique(np.sort(t[ok], axis=1), axis=0)
        best = (uniq, inv, t)
        if len(t) >= target_tris:
            break
        g += max(2, g // 3)
    uniq, inv, t = best

    n = len(uniq)
    P = np.zeros((n, 3)); N = np.zeros((n, 3)); C = np.zeros(n)
    np.add.at(P, inv, pos); np.add.at(N, inv, nrm); np.add.at(C, inv, 1.0)
    C = np.maximum(C, 1.0)[:, None]
    P /= C; N /= C
    N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-9)

    used = np.unique(t)
    remap = -np.ones(n, dtype=np.int64)
    remap[used] = np.arange(len(used))
    return P[used], N[used], remap[t]


# ---------------------------------------------------------------------------------
#  THE ARMATURE: real limbs, rebuilt from the primitive that was being thrown away.
#
#  A tree reads as a tree because dark limbs push out through the foliage and give the
#  canopy something to hang on. The scan's fine branching is 504,584 triangles of exactly
#  that, and the first build simply dropped it, because vertex clustering turns a spray
#  of twigs into a lump where the branching was. The answer is not to decimate it: it is
#  to read its SHAPE and build new limbs along it.
#
#  The shape is its medial axis, and a voxel graph is enough to find it. Occupied voxels
#  become nodes, nodes are joined to their neighbours, a spanning tree is grown from the
#  lowest node so every limb hangs off the trunk, and each surviving edge is swept as a
#  tapered tube whose radius falls with how much branch is still above it. That last part
#  is what makes it look grown rather than plumbed: a limb carrying half the canopy is
#  thick, and a limb carrying one twig is thin.
# ---------------------------------------------------------------------------------
def branch_skeleton(points, res=22, min_pts=40, prune=0.035):
    """Nodes and parent links along the medial axis of a branch point cloud."""
    lo, hi = points.min(0), points.max(0)
    ext = np.maximum(hi - lo, 1e-6)
    c = np.clip(np.floor((points - lo) / ext * (res - 1e-6)).astype(np.int64), 0, res - 1)
    key = (c[:, 0] * res + c[:, 1]) * res + c[:, 2]
    uniq, inv, cnt = np.unique(key, return_inverse=True, return_counts=True)
    n = len(uniq)
    P = np.zeros((n, 3)); np.add.at(P, inv, points); P /= cnt[:, None]
    keep = cnt >= min_pts
    if keep.sum() < 4:
        return None
    P, W = P[keep], cnt[keep].astype(np.float64)

    # THE SPANNING TREE, grown outward from the lowest node, which is the trunk's foot.
    # Prim's algorithm on the full distance matrix: a few hundred nodes at most, so the
    # matrix is small and the exactness is worth more than the speed of a heuristic.
    m = len(P)
    d = np.linalg.norm(P[:, None, :] - P[None, :, :], axis=2)
    root = int(np.argmin(P[:, 1]))
    parent = -np.ones(m, dtype=np.int64)
    inset = np.zeros(m, dtype=bool)
    inset[root] = True
    best = d[root].copy()
    src = np.full(m, root)
    for _ in range(m - 1):
        best_masked = np.where(inset, np.inf, best)
        j = int(np.argmin(best_masked))
        if not np.isfinite(best_masked[j]):
            break
        inset[j] = True
        parent[j] = src[j]
        closer = d[j] < best
        best = np.where(closer, d[j], best)
        src = np.where(closer, j, src)

    # HOW MUCH HANGS OFF EACH NODE, which becomes the limb's thickness. Walk the tree
    # from the leaves inward and accumulate, so a node's load is its own mass plus all
    # of its children's.
    load = W.copy()
    order = np.argsort(-np.linalg.norm(P - P[root], axis=1))
    for i in order:
        if parent[i] >= 0:
            load[parent[i]] += load[i]

    # PRUNE THE STUBS. A node whose whole subtree reaches less than `prune` of a tree
    # height away is a twig the cards already cover, and drawing it costs a tube.
    reach = np.zeros(m)
    for i in order:
        if parent[i] >= 0:
            reach[parent[i]] = max(reach[parent[i]],
                                   reach[i] + float(np.linalg.norm(P[i] - P[parent[i]])))
    alive = (reach >= prune) | (parent < 0)
    segs = []
    for i in range(m):
        pj = parent[i]
        if pj < 0 or not alive[i]:
            continue
        segs.append((pj, i))
    return dict(P=P, load=load, segs=segs, root=root)


def tubes_from_skeleton(sk, radius_scale=0.030, sides=5, taper=0.42):
    """Sweep each skeleton edge as a tapered tube, with cylindrical bark UVs."""
    P, load, segs = sk['P'], sk['load'], sk['segs']
    lmax = float(load.max()) if len(load) else 1.0
    verts, norms, uvs, tris = [], [], [], []
    for (aI, bI) in segs:
        a, b = P[aI], P[bI]
        ax = b - a
        L = float(np.linalg.norm(ax))
        if L < 1e-5:
            continue
        ax = ax / L
        up = np.array([0.0, 1.0, 0.0])
        if abs(float(np.dot(ax, up))) > 0.95:
            up = np.array([1.0, 0.0, 0.0])
        e1 = np.cross(ax, up); e1 /= np.linalg.norm(e1)
        e2 = np.cross(ax, e1)
        ra = radius_scale * (load[aI] / lmax) ** taper
        rb = radius_scale * (load[bI] / lmax) ** taper
        base = len(verts)
        for k in range(sides):
            th = 2.0 * np.pi * k / sides
            rad = np.cos(th) * e1 + np.sin(th) * e2
            verts.append(a + rad * ra); norms.append(rad); uvs.append((k / sides * 1.6, 0.0))
            verts.append(b + rad * rb); norms.append(rad)
            uvs.append((k / sides * 1.6, L * 2.4))
        for k in range(sides):
            k2 = (k + 1) % sides
            i0, i1 = base + k * 2, base + k * 2 + 1
            j0, j1 = base + k2 * 2, base + k2 * 2 + 1
            tris.append((i0, i1, j1)); tris.append((i0, j1, j0))
    if not tris:
        return None
    return (np.array(verts), np.array(norms), np.array(uvs),
            np.array(tris, dtype=np.int64))


def bark_unwrap(P, N, tri, repeats_u=3.0, repeats_v=2.4):
    """Cylindrical bark UVs, with the seam split so it does not smear.

    A trunk is a cylinder and bark is a tiling texture, so the honest unwrap is the angle
    round the trunk against the height up it. The one hazard is the seam where the angle
    wraps from one back to zero: a triangle straddling it spans the whole texture and
    draws a smeared band down the trunk. Those corners are duplicated and pushed past the
    wrap, which is the standard fix and costs a handful of vertices."""
    ang = np.arctan2(P[:, 2], P[:, 0]) / (2.0 * np.pi) + 0.5
    U = np.stack([ang * repeats_u, P[:, 1] * repeats_v], axis=1)
    P = list(P); N = list(N); U = list(U)
    tri = tri.copy()
    span = repeats_u * 0.5
    added = 0
    for k in range(len(tri)):
        u = np.array([U[tri[k, j]][0] for j in range(3)])
        if u.max() - u.min() <= span:
            continue
        for j in range(3):
            if u[j] < u.min() + span:
                P.append(P[tri[k, j]]); N.append(N[tri[k, j]])
                U.append(np.array([U[tri[k, j]][0] + repeats_u, U[tri[k, j]][1]]))
                tri[k, j] = len(P) - 1
                added += 1
    return np.array(P), np.array(N), np.array(U), tri, added


# ---------------------------------------------------------------------------------
#  THE LEAVES: segment the atlas, then compose sprays out of what comes back.
# ---------------------------------------------------------------------------------
def components(mask, max_iter=4000):
    """Connected opaque blobs, 4-connected, exact. There is no scipy on this machine,
    and label propagation converges in a few hundred numpy passes on a 1k sheet."""
    h, w = mask.shape
    lab = np.where(mask, np.arange(h * w, dtype=np.int64).reshape(h, w), -1)
    for _ in range(max_iter):
        prev = lab
        m = lab.copy()
        m[:-1, :] = np.maximum(m[:-1, :], np.where(mask[1:, :], lab[1:, :], -1))
        m[1:, :] = np.maximum(m[1:, :], np.where(mask[:-1, :], lab[:-1, :], -1))
        m[:, :-1] = np.maximum(m[:, :-1], np.where(mask[:, 1:], lab[:, 1:], -1))
        m[:, 1:] = np.maximum(m[:, 1:], np.where(mask[:, :-1], lab[:, :-1], -1))
        lab = np.where(mask, m, -1)
        if np.array_equal(lab, prev):
            break
    return lab


def cut_leaves(alpha, layers, min_px=1200, pad=2, dark=0.10, maxfill=0.80):
    """One entry per leaf: every layer cropped to that leaf, plus its own alpha.

    A COMPONENT IS NOT A LEAF JUST BECAUSE THE ALPHA MAP SAYS OPAQUE. island_tree_01's
    leaf alpha marks a 52 by 49 patch of the atlas's own black backing, where the colour
    map is (0.9, 0.9, 0.9) out of 255. Scattered into a spray at the size a leaf gets,
    that patch became a solid black square, and sixteen sprays each carried two or three
    of them. Anything whose colour under its own mask is near black is backing, not
    foliage, and is dropped by measurement rather than by a hand-written exclusion."""
    mask = alpha > 0.5
    lab = components(mask)
    ids, counts = np.unique(lab[lab >= 0], return_counts=True)
    key = 'd' if 'd' in layers else sorted(layers)[0]
    out = []
    for i, c in zip(ids, counts):
        if c < min_px:
            continue
        m = (lab == i)
        if float(layers[key][m].mean()) < dark:
            continue
        ys0, xs0 = np.nonzero(m)
        box = (float(ys0.max() - ys0.min() + 1) * float(xs0.max() - xs0.min() + 1))
        # A SOLID RECTANGLE IS UV PADDING, NOT FOLIAGE. fir_tree_01's twig atlas carries
        # brown packing bars between its needle sprays, and they came through the first
        # bake as brown planks lying across the canopy. Nothing that grows fills its own
        # bounding box: a single leaf measures 0.59 of it and a needle spray 0.18.
        if box > 0 and float(c) / box > maxfill:
            continue
        ys, xs = np.nonzero(lab == i)
        y0, y1 = max(int(ys.min()) - pad, 0), min(int(ys.max()) + pad + 1, lab.shape[0])
        x0, x1 = max(int(xs.min()) - pad, 0), min(int(xs.max()) + pad + 1, lab.shape[1])
        e = {'a': (lab[y0:y1, x0:x1] == i).astype(np.float32), 'px': int(c)}
        for k, img in layers.items():
            e[k] = img[y0:y1, x0:x1].astype(np.float32)
        e['gr'] = float(layers[key][m][..., 1].mean() - layers[key][m][..., 0].mean()) \
            if layers[key].ndim == 3 else 0.0
        out.append(e)

    # AND THE LAST TEST IS THAT FOLIAGE IS GREENER THAN IT IS RED, judged against the
    # atlas's OWN median rather than against an absolute. fir_tree_01's twig atlas holds
    # a 1024 by 212 brown packing bar and a bare-twig island among its needle sprays,
    # and both came through the first bake as brown planks lying across the canopy. The
    # nine real sprays measure green minus red between +13 and +15 out of 255; the bar
    # measures -12 and the bare twigs -11. A fixed threshold would fail on an autumn or
    # a dead tree, so the test is relative: a piece far below what this atlas's own
    # foliage does is not foliage.
    if len(out) >= 4:
        med = float(np.median([e['gr'] for e in out]))
        if med > 0.01:
            kept = [e for e in out if e['gr'] >= med * 0.25]
            if len(kept) >= 3:
                out = kept
    out.sort(key=lambda d: -d['px'])
    return out


def _place(leaf, key, deg, scale):
    a = Image.fromarray((leaf['a'] * 255).astype(np.uint8), 'L')
    w = max(4, int(a.width * scale)); h = max(4, int(a.height * scale))
    a = a.resize((w, h), Image.BILINEAR).rotate(deg, resample=Image.BILINEAR, expand=True)
    im = Image.fromarray(np.clip(leaf[key] * 255.0, 0, 255).astype(np.uint8), 'RGB')
    im = im.resize((w, h), Image.BILINEAR).rotate(deg, resample=Image.BILINEAR, expand=True)
    return (np.asarray(im).astype(np.float32) / 255.0,
            np.asarray(a).astype(np.float32) / 255.0)


def spray_plan(leaves, side, rng, count, leaf_frac=0.34):
    """Decide the placements ONCE, so the colour, the normal and the roughness sprays are
    three views of the same spray rather than three different sprays."""
    plan = []
    for _ in range(count):
        li = rng.randint(len(leaves))
        base = leaf_frac * side / max(leaves[li]['a'].shape)
        plan.append(dict(leaf=li, deg=rng.rand() * 360.0,
                         scale=base * (0.58 + 0.90 * rng.rand()),
                         shade=0.68 + 0.46 * rng.rand(),
                         x=rng.rand(), y=rng.rand()))
    return plan


def render_spray(leaves, plan, side, key, rotate_normals=False, shade=True,
                 cover_target=1.0):
    out = np.zeros((side, side, 3), dtype=np.float32)
    cov = np.zeros((side, side), dtype=np.float32)
    for p in plan:
        lf, a = _place(leaves[p['leaf']], key, p['deg'], p['scale'])
        lh, lw = a.shape
        if lh >= side or lw >= side:
            continue
        y = int(p['y'] * (side - lh)); x = int(p['x'] * (side - lw))
        v = lf
        if rotate_normals:
            # A ROTATED NORMAL MAP IS NOT A ROTATED PICTURE. Turning the leaf turns its
            # tangent frame with it, so the stored normal's x and y have to turn by the
            # same angle, or every leaf in the spray is lit as though it never moved.
            t = np.radians(p['deg'])
            nx = v[..., 0] * 2.0 - 1.0
            ny = v[..., 1] * 2.0 - 1.0
            v = np.stack([(nx * np.cos(t) - ny * np.sin(t)) * 0.5 + 0.5,
                          (nx * np.sin(t) + ny * np.cos(t)) * 0.5 + 0.5,
                          v[..., 2]], axis=2)
        elif shade:
            # a leaf deeper in the spray is darker, and that is the only shading a flat
            # card carries on its own; without it a patch reads as one flat colour
            v = np.clip(v * p['shade'], 0.0, 1.0)
        aa = a[..., None]
        dst = out[y:y + lh, x:x + lw]
        dst[...] = v * aa + dst * (1.0 - aa)
        cov[y:y + lh, x:x + lw] = np.maximum(cov[y:y + lh, x:x + lw], a)
        if (cov > 0.5).mean() > cover_target:
            break
    return out, cov


# ---------------------------------------------------------------------------------
#  THE LIGHT, baked from the dense model before it is thrown away.
#
#  WHY THIS AND NOT THE SUN SHADOW MAP. The Enhanced chain casts a 4096-texel shadow map
#  fitted to the view rectangle and the trees are in its caster list, so a tree does
#  darken the ground under it. It does NOT darken itself. Measured on SCG01EA at
#  dist 2400: dropping the receiver's normal offset from the shipped 1.5 texels to 0
#  moved 3,296 canopy pixels out of 88,400 and left the canopy's contrast at 15.4
#  against 15.3. Between the depth bias a thin card needs and the 0.08-cell penumbra,
#  leaf-on-leaf shadowing is filtered away before it reaches the screen, and no dial
#  recovers it. So it is measured here once, from the model that still has a million
#  leaves in it, and costs nothing at draw time.
#
#  ao         how much sky a vertex can see. 0.29 at this canopy's centre against 0.87
#             at its outer edge, measured on a probe line out from the middle.
#  thickness  how much foliage the sun must pass through to reach it from behind, which
#             is what makes a lit canopy edge glow like leaves rather than like paper.
# ---------------------------------------------------------------------------------
def _sphere_dirs(n, up_bias=0.55):
    """n directions on a sphere, pulled toward the sky: a tree is lit from above, and an
    even sphere spends half its rays under the ground where the answer means nothing."""
    i = np.arange(n, dtype=np.float64) + 0.5
    phi = np.arccos(1.0 - 2.0 * i / n)
    theta = np.pi * (1.0 + 5.0 ** 0.5) * i
    d = np.stack([np.cos(theta) * np.sin(phi), np.cos(phi), np.sin(theta) * np.sin(phi)], 1)
    d[:, 1] = d[:, 1] * (1.0 - up_bias) + up_bias
    return d / np.linalg.norm(d, axis=1, keepdims=True)


def ao_bake(verts, normals, dense, res=96, ndirs=32, steps=22, reach=0.55, k=3.4):
    lo, hi = dense.min(0), dense.max(0)
    ext = np.maximum(hi - lo, 1e-6)
    c = np.clip(np.floor((dense - lo) / ext * (res - 1e-6)).astype(np.int64), 0, res - 1)
    grid = np.bincount((c[:, 0] * res + c[:, 1]) * res + c[:, 2],
                       minlength=res ** 3).astype(np.float32)
    nz = grid[grid > 0]
    # A VOXEL IS FULL LONG BEFORE IT HOLDS EVERY POINT. One voxel at the trunk's base
    # holds thousands, and normalising by the maximum would flatten every leaf voxel to
    # nothing, so a high percentile is the divisor instead.
    grid /= max(float(np.percentile(nz, 75)) if len(nz) else 1.0, 1.0)
    grid = np.clip(grid, 0.0, 1.0).reshape(res, res, res)


    ts = np.linspace(reach / steps, reach, steps)
    gain = k * (reach / steps) * res / 8.0

    def march(origin, direction):
        depth = np.zeros(len(origin))
        for t in ts:
            p = origin + direction * t
            cc = np.floor((p - lo) / ext * (res - 1e-6)).astype(np.int64)
            inside = np.all((cc >= 0) & (cc < res), axis=1)
            np.clip(cc, 0, res - 1, out=cc)
            depth += np.where(inside, grid[cc[:, 0], cc[:, 1], cc[:, 2]], 0.0)
        return depth

    occ = np.zeros(len(verts))
    for d in _sphere_dirs(ndirs):
        occ += np.exp(-march(verts, d) * gain)
    ao = occ / ndirs

    n = normals / np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-9)
    thick = 1.0 - np.exp(-march(verts, -n) * gain * 0.62)
    return np.clip(ao, 0, 1), np.clip(thick, 0, 1)


# ---------------------------------------------------------------------------------
def load_map(path, mode='RGB', fallback=None, size=None):
    if path and os.path.exists(path):
        im = Image.open(path).convert(mode)
        if size:
            im = im.resize(size, Image.BILINEAR)
        a = np.asarray(im).astype(np.float32) / 255.0
        return a
    if fallback is None or size is None:
        return None
    a = np.zeros((size[1], size[0], 3), dtype=np.float32)
    a[...] = fallback
    return a if mode == 'RGB' else a[..., 0]



def mip_chain(sheet, cutoff=0.35, preserve_coverage=False):
    """Box-filtered mip levels, with the alpha-tested COVERAGE held constant.

    An alpha-cut canopy that is mipped naively dissolves as it recedes: each level's
    average alpha is lower than the last, so fewer texels clear the cut and the tree goes
    bald at distance. Every level's alpha is therefore rescaled until the fraction of it
    above the cut matches level 0. This is the standard fix and it is the difference
    between a canopy that thins with distance and one that does not.

    Without a mip chain at all the leaf edges alias and crawl on every camera move, which
    is half of what reads as 'low poly': tree3d uploaded level 0 alone with GL_LINEAR."""
    levels = [sheet]
    if preserve_coverage:
        target = float((sheet[..., 3].astype(np.float32) / 255.0 > cutoff).mean())
    cur = sheet
    while min(cur.shape[0], cur.shape[1]) > 1:
        h, w = cur.shape[0] // 2, cur.shape[1] // 2
        nx = cur.reshape(h, 2, w, 2, 4).astype(np.float32).mean(axis=(1, 3))
        if preserve_coverage and target > 0.0:
            a = nx[..., 3] / 255.0
            loS, hiS = 0.05, 20.0
            for _ in range(24):
                mid = 0.5 * (loS + hiS)
                if float((a * mid > cutoff).mean()) > target:
                    hiS = mid
                else:
                    loS = mid
            nx[..., 3] = np.clip(a * (0.5 * (loS + hiS)) * 255.0, 0, 255)
        cur = np.clip(nx, 0, 255).astype(np.uint8)
        levels.append(cur)
    return levels


# ---------------------------------------------------------------------------------
#  THE DIRECT IMPORT: a tree that was already a game tree.
#
#  Everything above this line rebuilds a game tree out of something that was never one.
#  It exists because Poly Haven ships photogrammetry: a million solid leaves and an atlas
#  of ten individual leaves laid out flat for texturing. None of it is needed when the
#  asset was AUTHORED for a game, and running it anyway would be worse than useless: it
#  would decimate a mesh an artist already budgeted and recompose an atlas an artist
#  already drew.
#
#  A game tree arrives with the three things the reconstruction was manufacturing:
#    - a triangle budget already spent, usually with LODs authored beside it
#    - foliage as alpha-cut cards, UV'd to a real foliage atlas of branch sprays
#    - bark on its own tiling texture with a normal map
#
#  So this path keeps the geometry exactly as authored and adds only what the renderer
#  needs and the asset has no way to carry: the baked occlusion and thickness, and the
#  wind's sway weight. That is the whole job.
# ---------------------------------------------------------------------------------
LOD_SKIP = ('collider', 'billboard', 'lod3')


def pick_lod(meshes, budget):
    """The most detailed LOD that fits the budget, ignoring colliders and billboards.

    An artist's own LOD chain is better than anything decimation here would produce, so
    the budget is spent by CHOOSING rather than by cutting."""
    cand = []
    for m in meshes:
        low = m['name'].lower()
        if any(k in low for k in LOD_SKIP) or not len(m['T']) or not m.get('slots'):
            continue
        cand.append(m)
    if not cand:
        return None
    fits = [m for m in cand if len(m['T']) <= budget]
    return max(fits, key=lambda m: len(m['T'])) if fits else min(cand, key=lambda m: len(m['T']))


def unity_textures(matname, aux):
    """Unity's convention: <material>_color.png and <material>_normal.png beside it."""
    want = {'tex': matname + '_color', 'nrm': matname + '_normal'}
    out = {'name': matname, 'stem': matname, 'tex': None, 'nrm': None, 'arm': None,
           'alpha': None, 'uvoff': [0.0, 0.0], 'uvscale': [1.0, 1.0]}
    for key, stem in want.items():
        for f in aux:
            b = os.path.splitext(os.path.basename(f))[0].lower()
            if b == stem.lower():
                out[key] = f
                break
    return out


def import_asis(paths, a):
    """One entry per file, each tagged with the species whose texture set it needs."""
    import fbx_read
    entries = []
    for path in paths:
        ms, root = fbx_read.meshes(path)
        mesh = pick_lod(ms, a.budget)
        if mesh is None:
            print('  %s: no usable LOD mesh, skipped' % os.path.basename(path))
            continue
        base = os.path.dirname(path) or '.'
        aux = []
        for d in (base, os.path.join(base, '..'), os.path.join(base, '..', '..')):
            tex = os.path.join(d, 'Textures')
            if os.path.isdir(tex):
                aux = [os.path.join(tex, f) for f in os.listdir(tex)]
                break
        mats = [unity_textures(n, aux) for n in mesh['slots']]
        foliage, wood = [], []
        for m in mats:
            # FOLIAGE IS THE MATERIAL WHOSE COLOUR CARRIES A REAL CUT-OUT, not the one
            # whose name says leaves. Names differ per pack; an alpha channel does not.
            if m['tex'] and _has_alpha(m['tex']):
                m['alpha'] = m['tex']
                foliage.append(m)
            elif m['tex']:
                wood.append(m)
        # THE BARK IS THE WOOD MATERIAL NAMED FOR IT, and taking the first one instead is
        # a real trap: pine_2 lists 'stump' before 'pine_bark_1', so the first pass gave
        # that model a texture set of its own called stump and baked an eighth set for
        # one tree. A stump beside a pine is not a species.
        bark = next((m for m in wood if 'bark' in m['name'].lower()), None) or (wood[0] if wood else None)
        # THE SPECIES IS THE BARK'S OWN NAME, less the word bark: oak_bark and
        # beech_bark are what tell four pines apart from two oaks, and it is the bark
        # that cannot be shared, so it is the bark that names the set.
        sp = (bark['name'].replace('_bark', '').rstrip('_0123456789')
              if bark else os.path.basename(path).rstrip('_0123456789.fbx'))
        skin, frames = read_anim(path, mesh, root)
        if skin is not None:
            print('  %s: %d bones, %d frames of authored sway'
                  % (os.path.basename(path), frames.shape[1], frames.shape[0]))
        entries.append(dict(path=path, mesh=mesh, mats=mats, foliage=foliage,
                            bark=bark, species=sp, skin=skin, frames=frames,
                            name=os.path.splitext(os.path.basename(path))[0]))
    return entries


ANIM_FRAMES = 20
ANIM_MAXBONES = 64


def _geom_node(root, gid):
    for n in root:
        if n.name == 'Objects':
            for g in n.findall('Geometry'):
                if g.props[0] == gid:
                    return g
    return None


def read_anim(path, mesh, root):
    """(skin[nverts,3] int16, frames[nf, nbones, 3, 4] float32) in the MODEL's own space.

    TWENTY FRAMES, and that is measured rather than chosen. Reconstructing all hundred
    authored frames by lerping a decimated set lands at 0.093% of the tree's height on
    oak_1, 0.050% on pine_3: about a twentieth of a screen pixel at this camera. Sampling
    finer does not improve it, because past twenty the grid stops landing on the authored
    keys and the error is the lerp's own.

    Bone indices ride as SHORTS. glTexCoordPointer rejects GL_UNSIGNED_BYTE outright with
    GL_INVALID_ENUM and says nothing, so a byte index would arrive as silence."""
    import fbx_read
    skel, skinmap = fbx_read.read_skin(path)
    if skel is None or mesh['id'] not in skinmap:
        return None, None
    geom = _geom_node(root, mesh['id'])
    if geom is None:
        return None, None
    b0, b1, w0 = skinmap[mesh['id']]
    pvi = fbx_read.polygon_vertex_index(geom)
    nb = skel.frames.shape[1]
    if nb > ANIM_MAXBONES:
        print('  %s: %d bones is past the %d the shader carries, animation dropped'
              % (os.path.basename(path), nb, ANIM_MAXBONES))
        return None, None
    skin = np.zeros((len(pvi), 3), dtype='<i2')
    skin[:, 0] = b0[pvi]
    skin[:, 1] = b1[pvi]
    skin[:, 2] = np.clip(np.round(w0[pvi] * 32767.0), 0, 32767)
    nf = skel.frames.shape[0]
    sel = (np.arange(ANIM_FRAMES) * nf) // ANIM_FRAMES
    return skin, skel.frames[sel]


def anim_to_unit(frames, centre, height):
    """The bone matrices moved into the space build_asis normalises the mesh into.

    build_asis divides the mesh by its own height and centres it, so a bone matrix read
    out of the file operates on coordinates the pack no longer stores. Rather than
    un-normalising in the shader, the matrix is conjugated once here: go back to model
    space, apply the bone, come back. Row-vector convention throughout, because that is
    what the file uses and what Skeleton.gl43 already assumes."""
    H = float(height)
    c = np.asarray(centre, dtype=np.float64)
    S = np.eye(4); S[0, 0] = S[1, 1] = S[2, 2] = H; S[3, :3] = c
    T = np.eye(4); T[0, 0] = T[1, 1] = T[2, 2] = 1.0 / H; T[3, :3] = -c / H
    A = S @ frames.astype(np.float64) @ T          # (nf, nb, 4, 4)
    # the shader reads three rows and does dot(row, vec4(p,1)) per component, so it wants
    # the first three COLUMNS of A, transposed
    return np.ascontiguousarray(np.transpose(A[:, :, :, :3], (0, 1, 3, 2)).astype('<f4'))


def _has_alpha(f):
    try:
        im = Image.open(f)
        if im.mode not in ('RGBA', 'LA', 'PA'):
            return False
        return bool((np.asarray(im.convert('RGBA'))[..., 3] < 200).mean() > 0.02)
    except Exception:
        return False


def sheet_set(foliage_mats, bark_mat, leaf_px, bark_px):
    """One species' four sheets: foliage colour and normal, bark colour and normal.

    ONE SET PER SPECIES, NOT ONE FOR THE PACK. Foliage UVs sit inside nought to one and
    could be atlased across species, but BARK CANNOT BE: a trunk's UVs run from about
    minus nine to ten because bark tiles up it, measured on these very models, and a
    tiling texture in an atlas cell reads its neighbours. A birch's white bark against an
    oak's is also exactly the kind of difference the eye picks up first. So each species
    keeps its own set and a model says which one it uses; sets are shared between the
    models that share a species, which is what keeps the pack from being sixteen copies
    of seven textures."""
    n = max(len(foliage_mats), 1)
    cols = int(np.ceil(np.sqrt(n)))
    rows = int(np.ceil(n / cols))
    AW, AH = cols * leaf_px, rows * leaf_px
    atlas = np.zeros((AH, AW, 4), dtype=np.uint8)
    atlas_n = np.zeros((AH, AW, 4), dtype=np.uint8)
    rect = {}
    for i, m in enumerate(foliage_mats):
        x, y = (i % cols) * leaf_px, (i // cols) * leaf_px
        c = Image.open(m['tex']).convert('RGBA').resize((leaf_px, leaf_px), Image.LANCZOS)
        atlas[y:y + leaf_px, x:x + leaf_px] = np.asarray(c)
        if m['nrm']:
            nn = Image.open(m['nrm']).convert('RGB').resize((leaf_px, leaf_px), Image.LANCZOS)
            atlas_n[y:y + leaf_px, x:x + leaf_px, :3] = np.asarray(nn)
        else:
            atlas_n[y:y + leaf_px, x:x + leaf_px, :3] = (128, 128, 255)
        # NO ROUGHNESS MAP SHIPS WITH THESE, so the alpha carries a constant: a leaf is
        # waxy and a good deal smoother than bark, and the shader reads it from here.
        atlas_n[y:y + leaf_px, x:x + leaf_px, 3] = 110
        rect[m['name']] = (x / AW, y / AH, leaf_px / AW, leaf_px / AH)

    bp = bark_px
    bark = np.zeros((bp, bp, 4), dtype=np.uint8)
    bark_n = np.zeros((bp, bp, 4), dtype=np.uint8)
    bd = bn = None
    if bark_mat and bark_mat['tex']:
        bd = np.asarray(Image.open(bark_mat['tex']).convert('RGB').resize((bp, bp), Image.LANCZOS))
    if bark_mat and bark_mat['nrm']:
        bn = np.asarray(Image.open(bark_mat['nrm']).convert('RGB').resize((bp, bp), Image.LANCZOS))
    bark[..., :3] = bd if bd is not None else (90, 74, 58)
    bark[..., 3] = 255
    bark_n[..., :3] = bn if bn is not None else (128, 128, 255)
    bark_n[..., 3] = 205
    return dict(atlas=atlas, atlas_n=atlas_n, bark=bark, bark_n=bark_n, rect=rect,
                aw=AW, ah=AH, bp=bp)


def surface_cloud(P, T, target=180000, seed=7):
    """Points scattered over the mesh's own surface, for the occlusion to march through.

    THE OUTPUT MESH IS NOT ITS OWN DENSITY FIELD. On a scan that distinction did not
    arise, because the thing being marched through had 1.3 million points in it. An
    authored tree has a few thousand vertices, and marching a ray through five thousand
    points finds nothing: the first bake of the oaks came back with occlusion running
    from 0.82 to 1.00, which is a tree that never shades itself anywhere. The surface is
    therefore SAMPLED, area-weighted so a big leaf card contributes as much canopy as its
    size deserves, and that sample is what the rays cross."""
    rng = np.random.RandomState(seed)
    a, b, c = P[T[:, 0]], P[T[:, 1]], P[T[:, 2]]
    area = 0.5 * np.linalg.norm(np.cross(b - a, c - a), axis=1)
    tot = float(area.sum())
    if tot <= 0:
        return P
    n = np.maximum((area / tot * target).astype(np.int64), 1)
    idx = np.repeat(np.arange(len(T)), n)
    u = rng.rand(len(idx), 1)
    v = rng.rand(len(idx), 1)
    over = (u + v) > 1.0
    u[over] = 1.0 - u[over]
    v[over] = 1.0 - v[over]
    return a[idx] + (b[idx] - a[idx]) * u + (c[idx] - a[idx]) * v


def build_asis(entry, rect, a):
    """One authored tree, converted. No decimation, no recomposition, no armature."""
    mesh, mats = entry['mesh'], entry['mats']
    P = mesh['P'].astype(np.float64)
    N = (mesh['N'].astype(np.float64) if mesh['N'] is not None else np.zeros_like(P))
    UV = (mesh['UV'].astype(np.float64) if mesh['UV'] is not None
          else np.zeros((len(P), 2)))
    T = mesh['T']
    mpp = mesh['mat_per_poly']
    poly = mesh['poly_of_pv'][T[:, 0]]

    lo, hi = P.min(0), P.max(0)
    height = hi[1] - lo[1]
    centre = np.array([(lo[0] + hi[0]) * 0.5, lo[1], (lo[2] + hi[2]) * 0.5])
    Q = (P - centre) / height

    MAT = np.zeros(len(P))
    U = UV.copy()
    for k, m in enumerate(mats):
        # THE MATERIAL OF THE POLYGON, not the polygon's own index. Comparing poly == k
        # picked out polygon number k and nothing else, so every vertex but three stayed
        # flagged as wood and the whole canopy drew with the bark texture and no cut-out:
        # three grey boulders where the oaks should have been.
        vs = (np.unique(T[mpp[poly] == k]) if mpp is not None else np.arange(len(P)))
        if not len(vs):
            continue
        if m['alpha'] is not None:
            MAT[vs] = 1.0
            x0, y0, w, h = rect.get(m['name'], (0.0, 0.0, 1.0, 1.0))
            # the authored UV is inside nought to one, so the atlas cell is a scale and
            # an offset and nothing is resampled
            U[vs, 0] = x0 + np.clip(UV[vs, 0], 0.0, 1.0) * w
            U[vs, 1] = y0 + np.clip(UV[vs, 1], 0.0, 1.0) * h

    AO, TH = ao_bake(Q, N, surface_cloud(Q, T))
    y = Q[:, 1]
    sway = np.clip(y / max(y.max(), 1e-6), 0.0, 1.0) ** 3
    sway[MAT > 0.5] = np.maximum(sway[MAT > 0.5], 0.30)
    # A LEAF CARD'S NORMAL IS ITS CARD'S, AND THAT IS FINE. The shader turns it toward
    # the viewer and wraps the sun round it, so an authored two-sided card lights
    # correctly without the normals being touched here.
    skin, frames = entry.get('skin'), entry.get('frames')
    if skin is not None and len(skin) != len(Q):
        print('  %s: skin is %d rows against %d vertices, animation dropped'
              % (entry['name'], len(skin), len(Q)))
        skin, frames = None, None
    if frames is not None:
        frames = anim_to_unit(frames, centre, height)
    return dict(P=Q, N=N, U=U, T=T, MAT=MAT, AO=AO, TH=TH, sway=sway,
                skin=skin, frames=frames,
                height=float(height),
                radius=float(np.abs(Q[:, [0, 2]]).max()),
                name=os.path.splitext(os.path.basename(entry['path']))[0])


def build_sheets(a, rng, lmat, bmat):
    """The two texture pairs, built ONCE and shared by every tree in the file.

    HOW BIG A PIECE GOES ON A CARD IS MEASURED, NOT ASSUMED, because scans differ in
    kind. island_tree_01's foliage atlas is eight enormous single leaves, so a card has
    to be COMPOSED out of dozens of them or it shows one leaf the size of a branch.
    fir_tree_01's is seven finished needle sprays, and scattering thirty of those would
    give a solid green brick. So the median piece's share of the atlas decides: a big
    piece is already a spray and a few go on a card, a small one is a single leaf and
    many do."""
    ldiff = load_map(lmat['tex'])
    lsz = (ldiff.shape[1], ldiff.shape[0])
    lalpha = load_map(lmat['alpha'], 'L', size=lsz)
    if lalpha.ndim == 3:
        lalpha = lalpha[..., 0]
    lnrm = load_map(lmat['nrm'], 'RGB', fallback=(0.5, 0.5, 1.0), size=lsz)
    larm = load_map(lmat['arm'], 'RGB', fallback=(1.0, 0.55, 0.0), size=lsz)
    cut = cut_leaves(lalpha, {'d': ldiff, 'n': lnrm, 'r': larm})
    if not cut:
        sys.exit('could not segment a single piece of foliage out of %s' % lmat['alpha'])
    # HOW FULL A PIECE'S OWN BOUNDING BOX IS, which separates the two kinds cleanly and
    # by measurement. A single leaf is a solid blade and fills most of its box:
    # island_tree_01's eight leaves measure 0.59. A needle or frond spray is feathery and
    # mostly gaps: fir_tree_01's eleven pieces measure 0.18. Area alone does not
    # discriminate, and it got this exact call wrong, putting island_tree_01 at seven
    # enormous leaves a card.
    fill = float(np.median([(c['a'] > 0.5).mean() for c in cut]))
    if a.leaves_per_spray > 0:
        per, frac = a.leaves_per_spray, a.leaf_frac
    elif fill < 0.35:
        per, frac = 8, 0.66
    else:
        per, frac = 30, 0.34
    print('foliage atlas: %d piece(s) segmented, median box fill %.2f, so %d per card '
          'at %.2f of a card across' % (len(cut), fill, per, frac))

    side = a.spray_px
    cols = int(np.ceil(np.sqrt(a.sprays)))
    rows = int(np.ceil(a.sprays / cols))
    AW, AH = cols * side, rows * side
    atlas = np.zeros((AH, AW, 4), dtype=np.uint8)
    atlas_n = np.zeros((AH, AW, 4), dtype=np.uint8)
    spray_uv = []
    for i in range(a.sprays):
        plan = spray_plan(cut, side, rng, per, frac)
        col, cov = render_spray(cut, plan, side, 'd', cover_target=a.cover)
        nor, _ = render_spray(cut, plan, side, 'n', rotate_normals=True)
        rgh, _ = render_spray(cut, plan, side, 'r', shade=False)
        x, y = (i % cols) * side, (i // cols) * side
        atlas[y:y + side, x:x + side, :3] = np.clip(col * 255, 0, 255).astype(np.uint8)
        atlas[y:y + side, x:x + side, 3] = np.clip(cov * 255, 0, 255).astype(np.uint8)
        atlas_n[y:y + side, x:x + side, :3] = np.clip(nor * 255, 0, 255).astype(np.uint8)
        # the ARM convention is ambient occlusion, ROUGHNESS, metallic; the green channel
        # is the one worth carrying and it rides in the normal sheet's alpha
        atlas_n[y:y + side, x:x + side, 3] = np.clip(rgh[..., 1] * 255, 0, 255).astype(np.uint8)
        spray_uv.append((x / AW, y / AH, (x + side) / AW, (y + side) / AH))

    bp = a.bark_px
    bark = np.zeros((bp, bp, 4), dtype=np.uint8)
    bark_n = np.zeros((bp, bp, 4), dtype=np.uint8)
    bd = load_map(bmat['tex'], 'RGB', size=(bp, bp)) if bmat else None
    bn = load_map(bmat['nrm'], 'RGB', fallback=(0.5, 0.5, 1.0), size=(bp, bp)) if bmat else None
    ba = load_map(bmat['arm'], 'RGB', fallback=(1.0, 0.8, 0.0), size=(bp, bp)) if bmat else None
    if bd is None:
        bd = np.full((bp, bp, 3), 0.35, dtype=np.float32)
    if bn is None:
        bn = np.zeros((bp, bp, 3), dtype=np.float32); bn[...] = (0.5, 0.5, 1.0)
    if ba is None:
        ba = np.zeros((bp, bp, 3), dtype=np.float32); ba[...] = (1.0, 0.8, 0.0)
    # THE BARK IS GRADED DOWN. A scan is photographed in daylight and the Tiberian Dawn
    # temperate ground is dark; island_tree_01's pale buttressed trunk came out of the
    # first build as a white blob that read as a mushroom under the canopy.
    bd = np.clip(bd * a.bark_gain, 0.0, 1.0)
    bark[..., :3] = np.clip(bd * 255, 0, 255).astype(np.uint8)
    bark[..., 3] = 255
    bark_n[..., :3] = np.clip(bn * 255, 0, 255).astype(np.uint8)
    bark_n[..., 3] = np.clip(ba[..., 1] * 255, 0, 255).astype(np.uint8)
    return atlas, atlas_n, bark, bark_n, spray_uv, AW, AH, bp


def build_model(prims, a, rng, spray_uv, label):
    """One tree: trunk, limbs, cards and the baked light, in tree-height units."""
    wood = [p for p in prims if not (p[3] and p[3].get('alpha'))]
    leaves = [p for p in prims if (p[3] and p[3].get('alpha'))]
    if not leaves:
        return None

    allpos = np.concatenate([p[0] for p in prims])
    lo, hi = allpos.min(0), allpos.max(0)
    height = hi[1] - lo[1]
    centre = np.array([(lo[0] + hi[0]) * 0.5, lo[1], (lo[2] + hi[2]) * 0.5])
    scale = 1.0 / height

    def mean_edge(pr):
        pos, _, _, _, idx = pr[:5]
        t = idx.reshape(-1, 3)[:20000]
        return float(np.linalg.norm(pos[t[:, 0]] - pos[t[:, 1]], axis=1).mean()) * scale

    # THE TWIGS ARE NOT DECIMATED, THEY ARE READ. A primitive whose triangles average
    # under about a six-hundredth of a tree height is fine branching, and clustering it
    # gives a lump where the branching was rather than a thinner branch. It is set aside
    # and rebuilt as an armature of real tapered limbs, which is what a canopy hangs on
    # and what the first build simply threw away.
    twigs = []
    if len(wood) > 1:
        keep = [i for i, w in enumerate(wood) if mean_edge(w) >= 0.0016]
        if keep and len(keep) < len(wood):
            twigs = [wood[i] for i in range(len(wood)) if i not in keep]
            wood = [wood[i] for i in keep]

    WP = np.zeros((0, 3)); WN = np.zeros((0, 3)); WU = np.zeros((0, 2))
    WT = np.zeros((0, 3), dtype=np.int64)
    seams = 0
    share = max(1, a.wood_tris // max(len(wood), 1))
    for pos, nrm, uv, mat, idx, _nd in wood:
        p_, n_, t_ = cluster_decimate((pos - centre) * scale, nrm, idx.reshape(-1, 3), share)
        p_, n_, u_, t_, ns = bark_unwrap(p_, n_, t_)
        WT = np.concatenate([WT, t_ + len(WP)])
        WP = np.concatenate([WP, p_]); WN = np.concatenate([WN, n_])
        WU = np.concatenate([WU, u_])
        seams += ns

    nlimb = 0
    if twigs and a.limbs > 0:
        bpts = np.concatenate([(w[0] - centre) * scale for w in twigs])
        # a thinning, because Prim's on the full distance matrix wants hundreds of nodes
        # and not thousands, and the medial axis of half a million points is not sharper
        # for having read every one of them
        if len(bpts) > 200000:
            bpts = bpts[::len(bpts) // 200000]
        sk = branch_skeleton(bpts, res=a.limb_res, min_pts=a.limb_min)
        tb = tubes_from_skeleton(sk, radius_scale=a.limb_radius) if sk else None
        if tb:
            lp_, ln_, lu_, lt_ = tb
            if len(lt_) > a.limbs:
                lt_ = lt_[:a.limbs]
            WT = np.concatenate([WT, lt_ + len(WP)])
            WP = np.concatenate([WP, lp_]); WN = np.concatenate([WN, ln_])
            WU = np.concatenate([WU, lu_])
            nlimb = len(lt_)

    lp = np.concatenate([(p[0] - centre) * scale for p in leaves])
    ln = np.concatenate([p[1] for p in leaves])
    glo, ghi = lp.min(0), lp.max(0)
    ext = np.maximum(ghi - glo, 1e-6)
    g = 4
    while g < 200:
        cell = np.floor((lp - glo) / ext * (g - 1e-6)).astype(np.int64)
        key = (cell[:, 0] * g + cell[:, 1]) * g + cell[:, 2]
        uniq, inv, cnt = np.unique(key, return_inverse=True, return_counts=True)
        if len(uniq) >= a.cards:
            break
        g += max(1, g // 3)
    nC = len(uniq)
    C = np.zeros((nC, 3)); NN = np.zeros((nC, 3)); K = np.zeros(nC)
    np.add.at(C, inv, lp); np.add.at(NN, inv, ln); np.add.at(K, inv, 1.0)
    K = np.maximum(K, 1.0)
    C /= K[:, None]; NN /= K[:, None]
    S = np.zeros(nC); np.add.at(S, inv, np.linalg.norm(lp - C[inv], axis=1)); S /= K
    # WHERE THE CARDS GO IS A SILHOUETTE DECISION, NOT A POPULATION ONE. Taking the
    # fullest cells spends the whole budget inside the canopy, where every card is hidden
    # behind another one, and leaves the outer shell to a scatter of isolated specks that
    # read as detached confetti. The outline is what the eye judges a tree by.
    rad = np.linalg.norm(C - C.mean(axis=0), axis=1)
    shell = rad / max(float(rad.max()), 1e-6)
    score = K * (0.25 + 0.75 * shell ** 1.6)
    order = np.argsort(-score)[:a.cards]
    C, NN, S, K, shell = C[order], NN[order], S[order], K[order], shell[order]

    canopy = C.mean(axis=0)
    kmax = max(float(K.max()), 1.0)
    verts, norms, uvs, tris = [], [], [], []
    ncross = 0
    cross_cut = np.quantile(K, 1.0 - a.cross) if a.cross > 0 else np.inf
    for i in range(len(C)):
        c = C[i]
        out = c - canopy
        if np.linalg.norm(out) < 1e-4:
            out = np.array([1.0, 0.0, 0.0])
        out = out / np.linalg.norm(out)
        # A CARD SEEN EDGE-ON IS NOT THERE. Facing every card straight out of the canopy
        # centre puts a quarter of them side-on to any camera and the canopy reads as a
        # spoked wheel, so each is tilted by its own draw from a seeded generator.
        out = out + rng.normal(scale=0.50, size=3)
        out = out / np.linalg.norm(out)
        # HOW BIG A CARD IS, and it is not the grid cell's spread. Driving it from the
        # mean spread of a UNIFORM grid cell gives every cell nearly the same answer, so
        # every card came out at the floor: measured on the shipped fir pack, 854 cards
        # with a min, a tenth, a median AND a ninetieth of 0.060, a 1.5 to 1 range and a
        # spread over mean of 0.019. A canopy of identical cards has no near and no far
        # and reads as one flat layer.
        #
        # Mass and position decide it instead. Radius goes as the CUBE ROOT of the
        # clump's mass, because mass is a volume, and it is pushed out toward the hull
        # because the outline is what the eye judges a tree by and a big card there
        # closes it. That gives a real range of about four to one.
        mass = (float(K[i]) / kmax) ** (1.0 / 3.0)
        r = a.card_min + (a.card_max - a.card_min) * (0.62 * mass + 0.38 * shell[i])
        sid = rng.randint(len(spray_uv))
        # A CARD'S SPRAY IS ALSO TURNED AND FLIPPED. Sixteen sprays over eight hundred
        # cards is one picture every fifty; with four quarter turns and a mirror it is
        # one every four hundred, and it costs nothing but the order of four UV pairs.
        rot = rng.randint(4)
        flip = rng.randint(2)
        quads = [out]
        if K[i] >= cross_cut:
            cq = np.cross(out, [0.0, 1.0, 0.0])
            if np.linalg.norm(cq) > 1e-4:
                quads.append(cq / np.linalg.norm(cq))
                ncross += 1
        for q in quads:
            right = np.cross([0.0, 1.0, 0.0], q)
            if np.linalg.norm(right) < 1e-4:
                right = np.array([1.0, 0.0, 0.0])
            right = right / np.linalg.norm(right)
            upc = np.cross(q, right)
            rr, uu = right * r, upc * r
            x0, y0, x1, y1 = spray_uv[sid]
            b = len(verts)
            corner = [(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)]
            corner = corner[rot:] + corner[:rot]
            if flip:
                corner = [(1.0 - u, v) for (u, v) in corner]
            for k, (sx, sy) in enumerate(((-1, -1), (1, -1), (1, 1), (-1, 1))):
                u0, v0 = corner[k]
                verts.append(c + rr * sx + uu * sy)
                norms.append(q)
                uvs.append((x0 + u0 * (x1 - x0), y0 + v0 * (y1 - y0)))
            tris.append((b, b + 1, b + 2)); tris.append((b, b + 2, b + 3))

    LP = np.array(verts); LN = np.array(norms); LU = np.array(uvs)
    LT = np.array(tris, dtype=np.int64)
    P = np.concatenate([WP, LP]); N = np.concatenate([WN, LN]); U = np.concatenate([WU, LU])
    T = np.concatenate([WT, LT + len(WP)])
    MAT = np.concatenate([np.zeros(len(WP)), np.ones(len(LP))])

    dense = np.concatenate([(p[0] - centre) * scale for p in prims])
    AO, TH = ao_bake(P, N, dense)

    y = P[:, 1]
    sway = np.clip(y / max(y.max(), 1e-6), 0.0, 1.0) ** 3
    sway[len(WP):] = np.maximum(sway[len(WP):], 0.30)

    print('%s: %d trunk + %d limb + %d card tris = %d, %d clumps (%d crossed), '
          'ao %.2f..%.2f mean %.2f, %.1fm tall'
          % (label, len(WT) - nlimb, nlimb, len(LT), len(T), len(C), ncross,
             AO.min(), AO.max(), AO.mean(), height))
    return dict(P=P, N=N, U=U, T=T, MAT=MAT, AO=AO, TH=TH, sway=sway,
                radius=float(np.abs(P[:, [0, 2]]).max()))


def _sheet_blob(sheet, cov):
    out = [struct.pack('<I', 0)]
    levels = mip_chain(sheet, preserve_coverage=cov)
    out[0] = struct.pack('<I', len(levels))
    for lv in levels:
        raw = lv.tobytes()
        comp = zlib.compress(raw, 9)
        out.append(struct.pack('<4I', lv.shape[1], lv.shape[0], len(raw), len(comp)))
        out.append(comp)
    return b''.join(out)


def write_pack3(out, models, sets, mapping):
    """TREE3D4: models, the texture SETS they share, the map from a cartridge code, and
    the authored sway each model was rigged with.

    The map is the point of the format. The renderer used to pick a model by hashing the
    cell, which is right when every tree is the same species and wrong the moment T18 has
    to be an acacia on a desert map and T05 a fir on a temperate one. The pack now says
    which model each of the cartridge's own names draws, because that is a decision about
    the game and not about the renderer.

    Version 4 adds the skin. Every model may carry a bone index pair and a weight per
    vertex, and a short palette of bone matrices per frame, so the tree plays the sway its
    artist rigged instead of the one the shader invents. A model with no rig writes
    nought bones and the loader and the shader both fall back to the global sway."""
    with open(out, 'wb') as f:
        f.write(b'TREE3D4\0')
        f.write(struct.pack('<3I', 4, len(models), len(sets)))
        for m in models:
            P, T = m['P'], m['T']
            f.write(struct.pack('<3I', len(P), len(T), m['set']))
            f.write(struct.pack('<2f', 1.0, m['radius']))
            rec = np.zeros((len(P), 12), dtype='<f4')
            rec[:, 0:3] = P; rec[:, 3:6] = m['N']; rec[:, 6:8] = m['U']
            rec[:, 8] = m['sway']; rec[:, 9] = m['AO']
            rec[:, 10] = m['TH']; rec[:, 11] = m['MAT']
            f.write(rec.tobytes())
            f.write(T.astype('<u4').tobytes())
            skin, frames = m.get('skin'), m.get('frames')
            if skin is None or frames is None:
                f.write(struct.pack('<2I', 0, 0))
            else:
                f.write(struct.pack('<2I', frames.shape[1], frames.shape[0]))
                f.write(np.ascontiguousarray(skin, dtype='<i2').tobytes())
                f.write(np.ascontiguousarray(frames, dtype='<f4').tobytes())
        f.write(struct.pack('<I', len(mapping)))
        for code, mi in sorted(mapping.items()):
            f.write(code.encode('ascii')[:8].ljust(8, b'\0'))
            f.write(struct.pack('<I', mi))
        for st in sets:
            f.write(struct.pack('<4I', st['aw'], st['ah'], st['bp'], st['bp']))
            f.write(_sheet_blob(st['atlas'], True))
            f.write(_sheet_blob(st['atlas_n'], False))
            f.write(_sheet_blob(st['bark'], False))
            f.write(_sheet_blob(st['bark_n'], False))
    rigged = sum(1 for m in models if m.get('frames') is not None)
    print('wrote %s (%.2f MB, %d models, %d texture sets, %d codes mapped, %d rigged)'
          % (out, os.path.getsize(out) / 1e6, len(models), len(sets), len(mapping),
             rigged))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('gltf', nargs='+', help='one or more .gltf or .fbx files')
    ap.add_argument('-o', '--out', default='tree3d.pack')
    ap.add_argument('--wood-tris', type=int, default=500)
    ap.add_argument('--cards', type=int, default=700)
    ap.add_argument('--cross', type=float, default=0.22,
                    help='the fraction of clumps that earn a SECOND quad across the '
                         'first. A single card seen edge-on is not there; a cross always '
                         'shows something. Two triangles each, so only the biggest '
                         'clumps get one.')
    ap.add_argument('--sprays', type=int, default=16)
    ap.add_argument('--spray-px', type=int, default=256)
    ap.add_argument('--bark-px', type=int, default=256)
    ap.add_argument('--bark-gain', type=float, default=0.70,
                    help='the bark is graded down by this. A scan is photographed in '
                         'daylight and the temperate ground is dark, so a pale trunk '
                         'comes out as a white blob under the canopy.')
    ap.add_argument('--leaves-per-spray', type=int, default=0,
                    help='0 measures the atlas and decides: a piece that is already a '
                         'spray gets a few per card, a single leaf gets many.')
    ap.add_argument('--leaf-frac', type=float, default=0.34)
    ap.add_argument('--cover', type=float, default=0.42,
                    help='how much of a spray the foliage covers. Higher and the card is '
                         'a green rectangle with a cut edge; lower and the canopy is '
                         'see-through.')
    ap.add_argument('--card-min', type=float, default=0.020,
                    help='the smallest card, as a fraction of tree height')
    ap.add_argument('--card-max', type=float, default=0.062,
                    help='the biggest. Three to one against the minimum: a canopy of '
                         'identically sized cards has no near and no far in it, and the '
                         'first build had a range of 1.5 to 1 with the tenth percentile '
                         'sitting on the floor. Judged by eye against the leaf scale the '
                         'director signed off on.')
    ap.add_argument('--limbs', type=int, default=760,
                    help='triangle budget for the rebuilt limb armature. 0 drops the '
                         'branching entirely, which is what the first build did and why '
                         'the foliage floated with nothing to hang on.')
    ap.add_argument('--limb-res', type=int, default=22)
    ap.add_argument('--limb-min', type=int, default=40)
    ap.add_argument('--limb-radius', type=float, default=0.030)
    ap.add_argument('--max-models', type=int, default=8)
    ap.add_argument('--asis', action='store_true',
                    help='the input is ALREADY a game tree: keep its geometry and its '
                         'atlas exactly as authored and add only the baked light and the '
                         'sway weight. Assumed automatically for a file that carries a '
                         'LOD chain, because nothing but a game asset does.')
    ap.add_argument('--budget', type=int, default=3400,
                    help='triangles per tree in --asis mode. The most detailed authored '
                         'LOD that fits is chosen; nothing is decimated.')
    ap.add_argument('--leaf-px', type=int, default=512)
    ap.add_argument('--map', nargs='*', default=None, metavar='CODE=model',
                    help="which cartridge name draws which model, e.g. T18=acacia_4. "
                         "Anything left unmapped falls back to the cell hash.")
    ap.add_argument('--preview', default=None)
    a = ap.parse_args()

    rng = np.random.RandomState(20260908)

    # A FILE THAT CARRIES A LOD CHAIN WAS AUTHORED FOR A GAME, and nothing else is. That
    # is the whole test, and it means the right path is chosen without a flag.
    asis = a.asis
    if not asis and a.gltf[0].lower().endswith('.fbx'):
        import fbx_read
        names = [m['name'].lower() for m in fbx_read.meshes(a.gltf[0])[0]]
        asis = any('lod' in n for n in names)

    if asis:
        print('reading %d authored tree(s); geometry kept as-is' % len(a.gltf))
        entries = import_asis(a.gltf, a)
        if not entries:
            sys.exit('nothing in those files looked like a tree')

        # ONE TEXTURE SET PER SPECIES, built once and shared by that species' models.
        sets, set_of = [], {}
        for e in entries:
            sp = e['species']
            if sp not in set_of:
                if not e['foliage']:
                    sys.exit('%s has no cut-out material: none of it is foliage' % e['name'])
                set_of[sp] = len(sets)
                sets.append(sheet_set(e['foliage'], e['bark'], a.leaf_px, a.bark_px))
            e['set'] = set_of[sp]

        models = []
        for e in entries[:a.max_models]:
            m = build_asis(e, sets[e['set']]['rect'], a)
            m['set'] = e['set']
            models.append(m)
            print('  %-12s %-9s %5d tris, %.1fm tall, ao %.2f..%.2f mean %.2f'
                  % (m['name'], e['species'], len(m['T']), m['height'],
                     m['AO'].min(), m['AO'].max(), m['AO'].mean()))

        # THE MAP FROM THE CARTRIDGE'S OWN NAMES, which is the decision the director made
        # and not one this tool is entitled to invent. Anything unmapped keeps hashing.
        mapping = {}
        by_name = {m['name']: i for i, m in enumerate(models)}
        for pair in (a.map or []):
            if '=' not in pair:
                sys.exit('--map wants CODE=model, got %r' % pair)
            code, mdl = pair.split('=', 1)
            if mdl not in by_name:
                sys.exit('--map %s: no model called %r in this bake' % (code, mdl))
            mapping[code.strip().upper()] = by_name[mdl]

        total = sum(len(m['T']) for m in models)
        print('TOTAL %d model(s), %d tris (%d each on average), %d texture set(s): %s'
              % (len(models), total, total // len(models), len(sets),
                 ', '.join(sorted(set_of))))
        if a.preview:
            Image.fromarray(sets[0]['atlas'], 'RGBA').save(a.preview)
        write_pack3(a.out, models, sets, mapping)
        return

    base = os.path.dirname(a.gltf[0]) or '.'
    texdir = os.path.join(base, 'textures')
    aux = ([os.path.join(texdir, f) for f in os.listdir(texdir)]
           if os.path.isdir(texdir) else [])
    prims = load_model(a.gltf[0], aux)

    lmats = [p[3] for p in prims if p[3] and p[3].get('alpha')]
    wmats = [p[3] for p in prims if p[3] and not p[3].get('alpha')]
    if not lmats:
        sys.exit('no cut-out material in %s: nothing here is foliage' % a.gltf[0])
    # THE BARK COMES FROM THE MATERIAL NAMED FOR IT WHERE THERE IS ONE. fir_tree_01 ships
    # a clean tiling bark beside three photogrammetry trunk islands that carry moss and
    # ground colour baked into them; the tiling one is the one a cylindrical unwrap wants.
    bmat = None
    for m in wmats:
        if 'bark' in (m.get('name') or '').lower():
            bmat = m
            break
    if bmat is None and wmats:
        bmat = wmats[0]

    atlas, atlas_n, bark, bark_n, spray_uv, AW, AH, bp = build_sheets(a, rng, lmats[0], bmat)

    nodes = sorted(set(p[5] for p in prims))[:a.max_models]
    models = []
    for nd in nodes:
        sub = [p for p in prims if p[5] == nd]
        m = build_model(sub, a, rng, spray_uv, 'tree %d' % (nd + 1))
        if m:
            models.append(m)
    if not models:
        sys.exit('nothing in %s baked into a tree' % a.gltf[0])

    total = sum(len(m['T']) for m in models)
    print('TOTAL %d model(s), %d tris (%d each on average) | foliage %dx%d, bark %dx%d, '
          'both with normal and roughness'
          % (len(models), total, total // len(models), AW, AH, bp, bp))

    if a.preview:
        Image.fromarray(atlas, 'RGBA').save(a.preview)

    for m in models:
        m['set'] = 0
    write_pack3(a.out, models,
                [dict(atlas=atlas, atlas_n=atlas_n, bark=bark, bark_n=bark_n,
                      aw=AW, ah=AH, bp=bp)], {})


if __name__ == '__main__':
    main()
