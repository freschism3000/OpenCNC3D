"""Minimal MEG V3 reader, written against the spec and validated on real bytes."""
import struct

class Meg:
    def __init__(self, path):
        self.path = path
        f = open(path, 'rb')
        self.f = f
        hdr = f.read(24)
        (self.magic, self.version, self.data_start,
         self.cnt_a, self.cnt_b, self.names_size) = struct.unpack('<6I', hdr)
        if self.magic != 0xFFFFFFFF:
            raise SystemExit('%s: magic %#x (encrypted or not a MEG)' % (path, self.magic))
        if self.version != 0x3F7D70A4:
            raise SystemExit('%s: version %#x' % (path, self.version))
        # filename table: u16 len then that many raw ASCII bytes, at +0x02
        blob = f.read(self.names_size)
        names, o = [], 0
        while o < len(blob):
            (n,) = struct.unpack_from('<H', blob, o); o += 2
            names.append(blob[o:o+n].decode('ascii', 'replace')); o += n
        self.names = names
        # record table: 20 bytes each, 2-byte packed
        nrec = self.cnt_a
        rec = f.read(20*nrec)
        self.records = []
        for i in range(nrec):
            flags, crc, idx, size, start, nameidx = struct.unpack_from('<HIiIIH', rec, 20*i)
            self.records.append(dict(flags=flags, crc=crc, index=idx, size=size,
                                     start=start, nameidx=nameidx))
        self.by_name = {}
        for r in self.records:
            if 0 <= r['nameidx'] < len(names):
                self.by_name[names[r['nameidx']].upper()] = r

    def read(self, name):
        r = self.by_name.get(name.upper().replace('/', '\\'))
        if r is None: return None
        self.f.seek(r['start']); return self.f.read(r['size'])
