#!/usr/bin/env python3
"""A reader for binary FBX 7.x, enough of it to import a game-ready tree.

WHY THIS EXISTS. bake_tree3d.py reads glTF, which is what Poly Haven ships. Trees that
were AUTHORED for a game rather than scanned come from the Unity Asset Store and its
neighbours, and those ship FBX. There is no FBX library on this machine and no Blender to
convert with, so the format is read directly. It is a documented container and the part a
mesh lives in is small: node records with typed properties, arrays optionally deflated.

WHAT IT DELIBERATELY DOES NOT DO. No animation, no skinning, no cameras, no lights, no
NURBS, no ASCII FBX. A tree needs geometry, its material assignment and the file names of
the textures, and everything else in a 300 megabyte package is weight. Where an animated
tree carries its wind in vertex COLOURS, which is the usual Unity convention, those are
read, because the renderer's own wind can use them directly.

VALIDATION. Poly Haven publishes the same asset as both FBX and glTF, so the reader is
checked against a file whose answer is already known: island_tree_01 is 1,599,403
triangles in three primitives of 34,787, 1,060,032 and 504,584. A reader that agrees with
that on a 48 megabyte file is reading the format and not guessing at it.
"""
import struct, sys, zlib
import numpy as np


class Node(object):
    __slots__ = ('name', 'props', 'children')

    def __init__(self, name, props, children):
        self.name = name
        self.props = props
        self.children = children

    def find(self, name):
        for c in self.children:
            if c.name == name:
                return c
        return None

    def findall(self, name):
        return [c for c in self.children if c.name == name]

    def __repr__(self):
        return 'Node(%s, %d props, %d children)' % (self.name, len(self.props),
                                                    len(self.children))


_ARRAY = {'f': ('<f4', 4), 'd': ('<f8', 8), 'l': ('<i8', 8), 'i': ('<i4', 4),
          'b': ('|i1', 1)}
_SCALAR = {'Y': ('<h', 2), 'C': ('<?', 1), 'I': ('<i', 4), 'F': ('<f', 4),
           'D': ('<d', 8), 'L': ('<q', 8)}


def _prop(buf, o):
    t = chr(buf[o]); o += 1
    if t in _SCALAR:
        fmt, n = _SCALAR[t]
        return struct.unpack_from(fmt, buf, o)[0], o + n
    if t in _ARRAY:
        dt, _ = _ARRAY[t]
        length, enc, comp = struct.unpack_from('<3I', buf, o)
        o += 12
        raw = buf[o:o + comp]
        o += comp
        if enc == 1:
            raw = zlib.decompress(raw)
        return np.frombuffer(raw, dtype=np.dtype(dt), count=length), o
    if t in ('S', 'R'):
        n = struct.unpack_from('<I', buf, o)[0]
        o += 4
        v = bytes(buf[o:o + n])
        return (v.decode('utf-8', 'replace') if t == 'S' else v), o + n
    raise ValueError('unknown FBX property type %r at %d' % (t, o - 1))


def parse(path):
    """The whole file as a tree of Nodes. Returns (root_children, version)."""
    buf = memoryview(open(path, 'rb').read())
    if bytes(buf[:20]) != b'Kaydara FBX Binary  ':
        raise ValueError('%s is not a binary FBX (ASCII FBX is not supported)' % path)
    version = struct.unpack_from('<I', buf, 23)[0]
    # 7.5 widened every record offset from 32 to 64 bits, which is the one structural
    # difference between the versions this has to read.
    wide = version >= 7500
    hdr = '<QQQB' if wide else '<IIIB'
    hdrlen = 25 if wide else 13

    def records(o, end):
        out = []
        while o < end:
            endoff, nprops, plen, namelen = struct.unpack_from(hdr, buf, o)
            o += hdrlen
            if endoff == 0:
                break
            name = bytes(buf[o:o + namelen]).decode('utf-8', 'replace')
            o += namelen
            po = o
            props = []
            for _ in range(nprops):
                v, po = _prop(buf, po)
                props.append(v)
            o += plen
            children = records(o, endoff - hdrlen) if o < endoff - hdrlen else []
            out.append(Node(name, props, children))
            o = endoff
        return out

    return records(27, len(buf)), version


def _layer(geom, kind, key, idxkey):
    """One layer element as (values, index_or_None, mapping, reference)."""
    n = geom.find(kind)
    if n is None:
        return None, None, None, None
    vals = n.find(key)
    mapping = n.find('MappingInformationType')
    ref = n.find('ReferenceInformationType')
    idx = n.find(idxkey) if idxkey else None
    return (vals.props[0] if vals else None,
            idx.props[0] if idx else None,
            mapping.props[0] if mapping else '',
            ref.props[0] if ref else '')


def _expand(vals, idx, mapping, ref, ncomp, npv, polyvert_to_vert, poly_of_pv):
    """Bring a layer element up to one value per POLYGON VERTEX, whatever it was."""
    if vals is None:
        return None
    v = np.asarray(vals, dtype=np.float32).reshape(-1, ncomp)
    if ref == 'IndexToDirect' and idx is not None:
        i = np.asarray(idx, dtype=np.int64)
    elif mapping == 'ByVertice' or mapping == 'ByVertex':
        i = polyvert_to_vert
    elif mapping == 'ByPolygon':
        i = poly_of_pv
    elif mapping == 'AllSame':
        i = np.zeros(npv, dtype=np.int64)
    else:
        i = np.arange(npv, dtype=np.int64)
    if mapping in ('ByVertice', 'ByVertex') and ref == 'IndexToDirect':
        i = np.asarray(idx, dtype=np.int64)[polyvert_to_vert]
    if len(i) != npv:
        if len(v) == npv:
            return v
        i = np.resize(i, npv)
    np.clip(i, 0, len(v) - 1, out=i)
    return v[i]


def meshes(path):
    """[(pos, nrm, uv, col, tri, geom_id, name)] with everything per polygon vertex.

    Triangulated by fan, which is what FBX's own PolygonVertexIndex convention wants:
    a polygon runs until an index arrives negative, and that last one is stored as its
    bitwise complement."""
    root, version = parse(path)
    objects = None
    for n in root:
        if n.name == 'Objects':
            objects = n
            break
    if objects is None:
        raise ValueError('%s has no Objects node' % path)

    matmap = mesh_materials(root)
    out = []
    for geom in objects.findall('Geometry'):
        verts = geom.find('Vertices')
        pvi = geom.find('PolygonVertexIndex')
        if verts is None or pvi is None:
            continue
        P = np.asarray(verts.props[0], dtype=np.float64).reshape(-1, 3)
        raw = np.asarray(pvi.props[0], dtype=np.int64)
        last = raw < 0
        vidx = np.where(last, ~raw, raw)
        npv = len(vidx)

        # which polygon each polygon-vertex belongs to, and where each polygon starts
        poly_of_pv = np.cumsum(np.concatenate([[0], last[:-1].astype(np.int64)]))
        ends = np.nonzero(last)[0]
        starts = np.concatenate([[0], ends[:-1] + 1])
        counts = ends - starts + 1

        # fan triangulation, vectorised over every polygon at once
        tri = []
        for k in range(3, int(counts.max()) + 1 if len(counts) else 3):
            sel = counts >= k
            if not sel.any():
                continue
            s = starts[sel]
            tri.append(np.stack([s, s + (k - 2), s + (k - 1)], axis=1))
        T = np.concatenate(tri) if tri else np.zeros((0, 3), dtype=np.int64)

        nrm = _expand(*_layer(geom, 'LayerElementNormal', 'Normals', 'NormalsIndex'),
                      ncomp=3, npv=npv, polyvert_to_vert=vidx, poly_of_pv=poly_of_pv)
        uv = _expand(*_layer(geom, 'LayerElementUV', 'UV', 'UVIndex'),
                     ncomp=2, npv=npv, polyvert_to_vert=vidx, poly_of_pv=poly_of_pv)
        col = _expand(*_layer(geom, 'LayerElementColor', 'Colors', 'ColorIndex'),
                      ncomp=4, npv=npv, polyvert_to_vert=vidx, poly_of_pv=poly_of_pv)

        mat = None
        me = geom.find('LayerElementMaterial')
        if me is not None:
            mv = me.find('Materials')
            mm = me.find('MappingInformationType')
            if mv is not None:
                m = np.asarray(mv.props[0], dtype=np.int64)
                mat = (np.zeros(len(counts), dtype=np.int64) + m[0]
                       if (mm is not None and mm.props[0] == 'AllSame') or len(m) == 1
                       else np.resize(m, len(counts)))

        gid = geom.props[0] if geom.props else 0
        name = ''
        for p in geom.props:
            if isinstance(p, str) and p:
                name = p.split('\x00')[0]
                break
        slots = matmap.get(gid, [])
        out.append(dict(P=P[vidx].astype(np.float32), N=nrm, UV=uv, COL=col,
                        T=T, mat_per_poly=mat, poly_of_pv=poly_of_pv,
                        id=gid, name=name, slots=slots,
                        nverts=len(P), npolys=len(counts)))
    return out, root


def _clean(v):
    """FBX object names arrive as "name\x00\x01Class"."""
    return v.split('\x00')[0] if isinstance(v, str) else ''


def mesh_materials(root):
    """{geometry id: [material name, ...]} in the slot order the polygons index.

    A mesh's materials are not attached to the Geometry: the Geometry hangs off a Model
    and the Materials hang off the same Model, so the slot order a polygon's material
    index refers to is the order those Material connections appear. Walking Connections
    is the only way to recover it, and getting it wrong paints bark with leaves."""
    objects = conns = None
    for n in root:
        if n.name == 'Objects':
            objects = n
        elif n.name == 'Connections':
            conns = n
    if objects is None or conns is None:
        return {}
    matname, modelname, geomids = {}, {}, set()
    for m in objects.findall('Material'):
        if m.props:
            matname[m.props[0]] = _clean(m.props[1] if len(m.props) > 1 else '')
    for m in objects.findall('Model'):
        if m.props:
            modelname[m.props[0]] = _clean(m.props[1] if len(m.props) > 1 else '')
    for g in objects.findall('Geometry'):
        if g.props:
            geomids.add(g.props[0])

    geom_of_model, mats_of_model = {}, {}
    for c in conns.findall('C'):
        p = c.props
        if len(p) < 3:
            continue
        child, parent = p[1], p[2]
        if child in geomids and parent in modelname:
            geom_of_model[parent] = child
        elif child in matname and parent in modelname:
            mats_of_model.setdefault(parent, []).append(matname[child])
    return {geom_of_model[m]: mats for m, mats in mats_of_model.items() if m in geom_of_model}


def textures(root):
    """{material name: {slot: relative filename}} from Objects and Connections."""
    objects = None
    conns = None
    for n in root:
        if n.name == 'Objects':
            objects = n
        elif n.name == 'Connections':
            conns = n
    if objects is None:
        return {}
    names, files = {}, {}
    for m in objects.findall('Material'):
        if m.props:
            names[m.props[0]] = (m.props[1].split('\x00')[0]
                                 if len(m.props) > 1 and isinstance(m.props[1], str) else '')
    for t in objects.findall('Texture'):
        rel = t.find('RelativeFilename')
        if t.props and rel is not None:
            files[t.props[0]] = rel.props[0].replace('\\', '/').split('/')[-1]
    out = {}
    if conns is not None:
        for c in conns.findall('C'):
            p = c.props
            if len(p) >= 3 and p[1] in files and p[2] in names:
                slot = p[3] if len(p) > 3 else 'DiffuseColor'
                out.setdefault(names[p[2]], {})[slot] = files[p[1]]
    return out


if __name__ == '__main__':
    ms, root = meshes(sys.argv[1])
    total = 0
    for m in ms:
        total += len(m['T'])
        print('%-24s %8d verts %8d tris | n %s uv %s col %s | %s'
              % (m['name'][:24], m['nverts'], len(m['T']),
                 'y' if m['N'] is not None else 'n',
                 'y' if m['UV'] is not None else 'n',
                 'Y' if m['COL'] is not None else 'n',
                 ', '.join(m['slots']) or '(no slots)'))
    print('TOTAL %d triangles over %d mesh(es)' % (total, len(ms)))
    tx = textures(root)
    for mat, slots in tx.items():
        print('material %-24s %s' % (mat[:24], slots))


# =====================================================================================
#  SKINNING AND ANIMATION
#
#  Enough of the FBX deformer and animation model to play an authored tree. Every rule
#  below was measured against the asset rather than taken from a specification, because
#  the specification has two readings on the one equation that matters and exporters
#  disagree about which they wrote.
#
#  THE FACTS THIS RESTS ON, all measured on the shipped pack:
#    a C record is (kind, CHILD, PARENT), and the BONE is the CHILD of its Cluster
#    Transform and TransformLink are 16 doubles in ROW-VECTOR order: reshape(4,4) and
#      do not transpose; the translation is row 3
#    TransformLink is the bone's GLOBAL transform at bind time, proved by comparing it
#      against the independent BindPose node: they agree exactly
#    only LOD0 and LOD1 carry a Skin at all; LOD2, LOD3 and the collider carry none, in
#      every one of the thirty files, so a triangle budget that reaches for LOD2 gets a
#      static tree and no error
#    curves are LINEAR with no tangents (KeyAttrFlags bit 4), 100 keys one 24th of a
#      second apart, so sampling on that same grid reproduces the artist's keys exactly
#    the rotation order is the default eEulerXYZ. This was not assumed: skinning the rest
#      pose under all six orders and measuring displacement from the authored mesh gives
#      XYZ 0.073 mean against 0.737 for the next best, on a tree 17.8 units tall
#    there is no PreRotation, no rotation or scaling pivot, and no scale animation
#      anywhere in the pack; the pivot chain is implemented anyway, because a rig that
#      does use PreRotation and is read without it is wrong by ninety degrees
# =====================================================================================
FBX_TICKS_PER_SECOND = 46186158000.0


def _clean_name(v):
    return v.split('\x00')[0] if isinstance(v, str) else ''


def _obj_class(n):
    ss = [p for p in n.props if isinstance(p, str)]
    return ss[-1] if ss else ''


def connections(root):
    """(edges, labelled) where edges[parent] = [child...] and labelled[(child,parent)] = name."""
    conns = None
    for n in root:
        if n.name == 'Connections':
            conns = n
    edges, labelled = {}, {}
    if conns is None:
        return edges, labelled
    for c in conns.findall('C'):
        p = c.props
        if len(p) < 3:
            continue
        edges.setdefault(p[2], []).append(p[1])
        if len(p) > 3 and isinstance(p[3], str):
            labelled[(p[1], p[2])] = p[3]
    return edges, labelled


def _prop70(node):
    """Properties70 as {name: [values...]}."""
    out = {}
    p70 = node.find('Properties70') if node else None
    if p70 is None:
        return out
    for P in p70.findall('P'):
        if P.props:
            out[str(P.props[0])] = list(P.props[1:])
    return out


def _euler_xyz(rx, ry, rz):
    """Row-vector XYZ euler, degrees in, 4x4 out. Row vector means v * M, so the
    composition reads left to right and R = Rx @ Ry @ Rz."""
    x, y, z = np.radians([rx, ry, rz])
    cx, sx, cy, sy, cz, sz = np.cos(x), np.sin(x), np.cos(y), np.sin(y), np.cos(z), np.sin(z)
    Rx = np.array([[1, 0, 0, 0], [0, cx, sx, 0], [0, -sx, cx, 0], [0, 0, 0, 1]], dtype=np.float64)
    Ry = np.array([[cy, 0, -sy, 0], [0, 1, 0, 0], [sy, 0, cy, 0], [0, 0, 0, 1]], dtype=np.float64)
    Rz = np.array([[cz, sz, 0, 0], [-sz, cz, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]], dtype=np.float64)
    return Rx @ Ry @ Rz


def _local_matrix(t, r, s, pre=None):
    """The node's local transform, row-vector: S then R then T."""
    M = np.eye(4)
    M[0, 0], M[1, 1], M[2, 2] = s
    R = _euler_xyz(*r)
    if pre is not None:
        R = _euler_xyz(*pre) @ R
    M = M @ R
    M[3, :3] = t
    return M


class Skeleton(object):
    """Bones, their bind inverses, and the animation sampled onto its own key grid."""

    def __init__(self, names, parent, invbind, frames, period):
        self.names = names
        self.parent = parent
        self.invbind = invbind          # (nbones, 4, 4)
        self.frames = frames            # (nframes, nbones, 4, 4) skinning matrices
        self.period = period            # seconds for one loop

    def gl43(self):
        """(nframes, nbones, 3, 4) ready for glUniform4fv, no transpose at draw time."""
        f = self.frames
        out = np.zeros((f.shape[0], f.shape[1], 3, 4), dtype=np.float32)
        # row-vector M means a point is v*M; the shader does dot(row, vec4(p,1)) per
        # component, so it wants the COLUMNS of the 4x3 part, which is M[:, :3] transposed
        out[:] = np.transpose(f[:, :, :, :3], (0, 1, 3, 2))
        return out


def read_skin(path, want_geom=None):
    """(Skeleton, {geom_id: (bone_index[cp], bone_index2[cp], weight[cp])}) or (None, {}).

    Bone indices are per CONTROL POINT. meshes() returns everything per POLYGON VERTEX,
    which is the same gather it applies to positions, so the caller finishes the job with
    idx[polygon_vertex_index(geom)]."""
    root, _ = parse(path)
    objects = None
    for n in root:
        if n.name == 'Objects':
            objects = n
    if objects is None:
        return None, {}
    edges, labelled = connections(root)

    models = {m.props[0]: m for m in objects.findall('Model')}
    limbs = {i: m for i, m in models.items() if 'LimbNode' in _obj_class(m)}
    skins = {d.props[0]: d for d in objects.findall('Deformer') if _obj_class(d) == 'Skin'}
    clusters = {d.props[0]: d for d in objects.findall('Deformer')
                if _obj_class(d) == 'Cluster'}
    geoms = {g.props[0]: g for g in objects.findall('Geometry')}
    if not limbs or not skins:
        return None, {}

    # THE CHAIN RUNS OVER EVERY MODEL, NOT JUST THE BONES, and that is not a nicety.
    # Above Root sits a Null called <tree>_skeleton carrying Lcl Rotation (-90, 0, 0),
    # which is Blender's Z-up to Y-up conversion. Walking only LimbNodes drops it and
    # rotates the entire skeleton a quarter turn: measured, that moved the mesh 12.7
    # units on a tree 17.8 units tall, while the bind pose still reproduced the authored
    # vertices perfectly, which is exactly the shape of bug that looks like bad weights
    # and is not.
    node_ids = sorted(models)
    nindex = {n: i for i, n in enumerate(node_ids)}
    nparent = [-1] * len(node_ids)
    for pid, kids in edges.items():
        for k in kids:
            if k in nindex and pid in nindex:
                nparent[nindex[k]] = nindex[pid]
    bone_ids = sorted(limbs)
    bindex = {b: i for i, b in enumerate(bone_ids)}
    parent = [-1] * len(bone_ids)
    for b, bid in enumerate(bone_ids):
        p = nparent[nindex[bid]]
        while p >= 0 and node_ids[p] not in bindex:
            p = nparent[p]
        parent[b] = bindex[node_ids[p]] if p >= 0 else -1

    # THE SKIN BELONGS TO WHICHEVER GEOMETRY IT IS CONNECTED TO, and to nothing else. Both
    # skins in a file carry the SAME name, so the name is no help; the Skin -> Geometry
    # edge is the only thing that assigns them.
    skin_of_geom = {}
    for gid in geoms:
        for ch in edges.get(gid, []):
            if ch in skins:
                skin_of_geom[gid] = ch

    invbind = np.tile(np.eye(4), (len(bone_ids), 1, 1))
    weights = {}
    seen_link = {}
    for gid, sid in skin_of_geom.items():
        if want_geom is not None and gid != want_geom:
            continue
        ncp = len(np.asarray(geoms[gid].find('Vertices').props[0])) // 3
        b0 = np.zeros(ncp, dtype=np.int32)
        b1 = np.zeros(ncp, dtype=np.int32)
        w0 = np.ones(ncp, dtype=np.float32)
        best = np.zeros(ncp, dtype=np.float32)
        second = np.zeros(ncp, dtype=np.float32)
        for cid in edges.get(sid, []):
            if cid not in clusters:
                continue
            cl = clusters[cid]
            bone = None
            for ch in edges.get(cid, []):
                if ch in bindex:
                    bone = bindex[ch]
                    break
            if bone is None:
                continue
            tr = cl.find('Transform')
            tl = cl.find('TransformLink')
            if tr is not None and tl is not None:
                T = np.asarray(tr.props[0], dtype=np.float64).reshape(4, 4)
                L = np.asarray(tl.props[0], dtype=np.float64).reshape(4, 4)
                seen_link[bone] = L
                # WHICH READING OF "Transform" THIS EXPORTER USED, decided by measurement
                # rather than by trusting a name. The FBX SDK writes the mesh's global
                # bind there, so the inverse bind is Transform @ inv(TransformLink);
                # Blender writes the inverse bind itself. The discriminator is that under
                # Blender's reading Transform @ TransformLink is the identity, and it is
                # here to 1.8e-6, which is float32 residue: a 1e-6 tolerance is too tight.
                invbind[bone] = (T if np.allclose(T @ L, np.eye(4), atol=1e-4)
                                 else T @ np.linalg.inv(L))
            ix, wt = cl.find('Indexes'), cl.find('Weights')
            # a cluster can carry no Indexes node AT ALL rather than an empty one
            if ix is None or wt is None:
                continue
            I = np.asarray(ix.props[0], dtype=np.int64)
            W = np.asarray(wt.props[0], dtype=np.float64)
            ok = (I >= 0) & (I < ncp)
            I, W = I[ok], W[ok].astype(np.float32)
            take = W > best[I]
            demote = (~take) & (W > second[I])
            ti = I[take]
            b1[ti] = b0[ti]; second[ti] = best[ti]
            b0[ti] = bone;   best[ti] = W[take]
            di = I[demote]
            b1[di] = bone;   second[di] = W[demote]
        tot = np.maximum(best + second, 1e-8)
        w0 = (best / tot).astype(np.float32)
        weights[gid] = (b0, b1, w0)

    # ---- the animation ---------------------------------------------------------------
    curve_nodes = {c.props[0]: c for c in objects.findall('AnimationCurveNode')}
    curves = {c.props[0]: c for c in objects.findall('AnimationCurve')}
    # curve -> curve node is labelled d|X / d|Y / d|Z; curve node -> model is labelled
    # Lcl Translation / Lcl Rotation / Lcl Scaling. The curve node's own name is 'T', 'R'
    # or 'S' and means nothing.
    chan = {}
    for (child, par), lab in labelled.items():
        if child in curve_nodes and par in nindex and lab.startswith('Lcl '):
            chan.setdefault(nindex[par], {})[lab] = child
    axis_curves = {}
    for (child, par), lab in labelled.items():
        if child in curves and par in curve_nodes and lab.startswith('d|'):
            axis_curves.setdefault(par, {})[lab[2:]] = child

    nframes, period = 0, 0.0
    for cid, c in curves.items():
        kt = c.find('KeyTime')
        if kt is None:
            continue
        arr = np.asarray(kt.props[0], dtype=np.int64)
        if len(arr) > nframes:
            nframes = len(arr)
            # THE PERIOD IS KEYS TIMES THE STEP, NOT FIRST KEY TO LAST. The span between
            # the first and last key is one step short of the loop, and baking that makes
            # every tree hitch once a cycle.
            step = (arr[1] - arr[0]) if len(arr) > 1 else 0
            period = float(len(arr) * step) / FBX_TICKS_PER_SECOND
    if nframes == 0:
        return None, weights

    def sample(bone, prop, default):
        got = list(default)
        node = chan.get(bone, {}).get(prop)
        if node is None:
            return np.tile(np.asarray(got, dtype=np.float64), (nframes, 1))
        p = _prop70(curve_nodes[node])
        for k, ax in (('d|X', 0), ('d|Y', 1), ('d|Z', 2)):
            if k in p and p[k]:
                try: got[ax] = float(p[k][-1])
                except Exception: pass
        out = np.tile(np.asarray(got, dtype=np.float64), (nframes, 1))
        for ax, k in enumerate('XYZ'):
            cid = axis_curves.get(node, {}).get(k)
            if cid is None:
                continue
            v = np.asarray(curves[cid].find('KeyValueFloat').props[0], dtype=np.float64)
            out[:, ax] = v[:nframes] if len(v) >= nframes else np.resize(v, nframes)
        return out

    T = np.zeros((len(node_ids), nframes, 3))
    R = np.zeros((len(node_ids), nframes, 3))
    S = np.ones((len(node_ids), nframes, 3))
    pre = {}
    for n, nid in enumerate(node_ids):
        st = _prop70(models[nid])
        def stat(name, dflt):
            v = st.get(name)
            try: return [float(v[-3]), float(v[-2]), float(v[-1])] if v else list(dflt)
            except Exception: return list(dflt)
        T[n] = sample(n, 'Lcl Translation', stat('Lcl Translation', (0, 0, 0)))
        R[n] = sample(n, 'Lcl Rotation', stat('Lcl Rotation', (0, 0, 0)))
        S[n] = sample(n, 'Lcl Scaling', stat('Lcl Scaling', (1, 1, 1)))
        pr = st.get('PreRotation')
        if pr:
            try: pre[n] = [float(pr[-3]), float(pr[-2]), float(pr[-1])]
            except Exception: pass

    # PARENTS FIRST, and the file does not store them that way. Sorting the bones by
    # depth once is the whole fix; walking them in id order asks a child for a parent
    # global that does not exist yet.
    depth = [0] * len(node_ids)
    for n in range(len(node_ids)):
        d, p = 0, nparent[n]
        while p >= 0 and d < len(node_ids):
            d += 1
            p = nparent[p]
        depth[n] = d
    order = sorted(range(len(node_ids)), key=lambda n: depth[n])

    frames = np.zeros((nframes, len(bone_ids), 4, 4))
    for f in range(nframes):
        glob = [None] * len(node_ids)
        for n in order:
            L = _local_matrix(T[n, f], R[n, f], S[n, f], pre.get(n))
            p = nparent[n]
            glob[n] = L if (p < 0 or glob[p] is None) else (L @ glob[p])
        for b, bid in enumerate(bone_ids):
            frames[f, b] = invbind[b] @ glob[nindex[bid]]

    names = [_clean_name(limbs[b].props[1] if len(limbs[b].props) > 1 else '')
             for b in bone_ids]
    return Skeleton(names, parent, invbind, frames, period), weights


def polygon_vertex_index(geom):
    """The same gather meshes() applies to positions, so a per-control-point array can be
    brought up to per polygon vertex with one index."""
    raw = np.asarray(geom.find('PolygonVertexIndex').props[0], dtype=np.int64)
    return np.where(raw < 0, ~raw, raw)
