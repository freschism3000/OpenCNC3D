"""Westwood TD MIX archive reader.

Header: u16 count, u32 datasize, then count * 12 bytes of (u32 id, u32 offset, u32 size).
Data begins immediately after the table; offsets are relative to that point.
Filenames are not stored, only the hash produced by mix_id().
"""
import struct
import os


def mix_id(name):
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


class Mix:
    def __init__(self, path):
        self.path = path
        with open(path, 'rb') as f:
            self.data = f.read()
        count, datasize = struct.unpack_from('<HI', self.data, 0)
        self.count = count
        self.datasize = datasize
        self.entries = {}   # id -> (offset, size)
        self.order = []     # ids in table order
        table = 6
        for i in range(count):
            eid, off, size = struct.unpack_from('<III', self.data, table + i * 12)
            self.entries[eid] = (off, size)
            self.order.append(eid)
        self.data_start = table + count * 12

    def has(self, name):
        return mix_id(name) in self.entries

    def get(self, name):
        eid = mix_id(name)
        if eid not in self.entries:
            raise KeyError('%s (id 0x%08X) not in %s' % (name, eid, os.path.basename(self.path)))
        off, size = self.entries[eid]
        s = self.data_start + off
        return self.data[s:s + size]

    def get_by_id(self, eid):
        off, size = self.entries[eid]
        s = self.data_start + off
        return self.data[s:s + size]

    def size_of(self, name):
        return self.entries[mix_id(name)][1]
