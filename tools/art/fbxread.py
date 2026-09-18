"""Minimal binary FBX 7.x reader: enough to pull geometry, materials and connections.

The counterpart to fbxout.py, which writes this format for an artist. This reads what
comes back. It is deliberately small: it understands the record tree, the property
types and the array encodings, and nothing else. No transforms are resolved and no
scene semantics are applied here; fbxin.py does that, where the choices are visible.

Both FBX 7.4 (32-bit record headers) and 7.5 (64-bit) are read, because a DCC
application picks the version and the artist does not choose it.
"""
import struct, zlib

ARRAY = {"f": ("f", 4), "d": ("d", 8), "l": ("q", 8), "i": ("i", 4), "b": ("b", 1)}
SCALAR = {"C": ("?", 1), "B": ("b", 1), "Y": ("h", 2), "I": ("i", 4),
          "L": ("q", 8), "F": ("f", 4), "D": ("d", 8)}


class Node(object):
    def __init__(self, name, props, kids):
        self.name, self.props, self.kids = name, props, kids

    def find(self, name):
        return [k for k in self.kids if k.name == name]

    def first(self, name):
        f = self.find(name)
        return f[0] if f else None

    def walk(self):
        yield self
        for k in self.kids:
            for n in k.walk():
                yield n


def _read_node(d, pos, ver):
    if ver >= 7500:
        end, nprop, plen = struct.unpack_from("<QQQ", d, pos); p = pos + 24
    else:
        end, nprop, plen = struct.unpack_from("<III", d, pos); p = pos + 12
    nlen = d[p]; p += 1
    name = d[p:p + nlen].decode("ascii", "replace"); p += nlen
    if end == 0:
        return None, pos
    props = []
    for _ in range(nprop):
        t = chr(d[p]); p += 1
        if t in SCALAR:
            f, s = SCALAR[t]
            props.append(struct.unpack_from("<" + f, d, p)[0]); p += s
        elif t in ("S", "R"):
            ln = struct.unpack_from("<I", d, p)[0]; p += 4
            v = d[p:p + ln]; p += ln
            props.append(v.decode("utf-8", "replace") if t == "S" else v)
        elif t in ARRAY:
            f, s = ARRAY[t]
            n, enc, cl = struct.unpack_from("<III", d, p); p += 12
            raw = d[p:p + cl]; p += cl
            if enc:
                raw = zlib.decompress(raw)
            props.append(list(struct.unpack("<%d%s" % (n, f), raw[:n * s])))
        else:
            raise ValueError("unknown property type %r" % t)
    kids = []
    while p < end:
        k, _ = _read_node(d, p, ver)
        if k is None:
            p += 13 if ver < 7500 else 25
            break
        kids.append(k)
        p = k.end
    node = Node(name, props, kids)
    node.end = end
    return node, end


def load(path):
    d = open(path, "rb").read()
    ver = struct.unpack_from("<I", d, 23)[0]
    roots, p = [], 27
    while p < len(d) - 160:
        n, _ = _read_node(d, p, ver)
        if n is None:
            break
        roots.append(n)
        p = n.end
    root = Node("__root__", [], roots)
    root.end = len(d)
    return ver, root


def name_of(node):
    """FBX object names are 'Name\\x00\\x01Class'."""
    for p in node.props:
        if isinstance(p, str) and "\x00\x01" in p:
            return p.split("\x00\x01")[0]
    return None


def objects(root):
    """{id: node} for everything under Objects, plus the connection list."""
    objs, conns = {}, []
    for n in root.walk():
        if n.name == "Objects":
            for k in n.kids:
                ids = [p for p in k.props if isinstance(p, int)]
                if ids:
                    objs[ids[0]] = k
        elif n.name == "Connections":
            for c in n.kids:
                p = c.props
                if len(p) >= 3:
                    conns.append((p[0], p[1], p[2], p[3] if len(p) > 3 else None))
    return objs, conns


def polygons(geom):
    """[(vertex indices...)] from PolygonVertexIndex, where a negative index
    (~i) marks the last corner of a polygon."""
    idx = geom.first("PolygonVertexIndex").props[0]
    out, cur = [], []
    for i in idx:
        if i < 0:
            cur.append(~i)
            out.append(tuple(cur))
            cur = []
        else:
            cur.append(i)
    return out
