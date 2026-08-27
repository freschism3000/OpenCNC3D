"""Westwood TD MIX archive reader.

Header: u16 count, u32 datasize, then count * 12 bytes of (u32 id, u32 offset, u32 size).
Data begins immediately after the table; offsets are relative to that point.
Filenames are not stored, only the hash produced by mix_id().

TWO HEADER LAYOUTS, and how they are told apart. Everything on the original 1995 discs is
the plain layout above -- CONQUER.MIX opens `0a 01`, i.e. count 266. The later C&C95 /
Command & Conquer Gold builds (the ones the freeware re-release is made of) introduced an
EXTENDED layout that begins with a u16 ZERO where a count would be, followed by a u32
flags word; the real count/datasize header then follows, after an optional 20-byte
checksum-key block. Since a real archive can never have zero files, a leading zero is an
unambiguous marker rather than a guess.

    flags & 0x00010000   a SHA digest is appended after the data (harmless, ignored)
    flags & 0x00020000   the index is Blowfish-encrypted with an 80-byte RSA-wrapped key

The encrypted case is NOT supported and says so out loud rather than returning an empty
archive: decrypting it needs Westwood's Blowfish key schedule, which is not in this repo.
Nothing we ship is encrypted; this exists so an archive from an unfamiliar release either
works or explains itself, instead of silently reading zero records and letting a caller
report "0 movies found" as though the disc were empty.
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
        self.extended = False
        self.flags = 0
        head = 0
        if len(self.data) >= 6 and struct.unpack_from('<H', self.data, 0)[0] == 0:
            # Extended layout: a real archive cannot hold zero files, so this is the
            # marker and not a count. See the module docstring.
            self.extended = True
            self.flags = struct.unpack_from('<I', self.data, 2)[0]
            if self.flags & 0x00020000:
                raise ValueError(
                    '%s has a Blowfish-ENCRYPTED index (flags 0x%08X). This reader '
                    'cannot open it; use a release whose MIX is unencrypted, or add a '
                    'Blowfish key schedule.' % (os.path.basename(path), self.flags))
            head = 6
        count, datasize = struct.unpack_from('<HI', self.data, head)
        self.count = count
        self.datasize = datasize
        self.entries = {}   # id -> (offset, size)
        self.order = []     # ids in table order
        table = head + 6
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
