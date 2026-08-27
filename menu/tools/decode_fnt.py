"""Westwood .FNT decoder, written straight off common/font.cpp (Set_Font + Buffer_Print).

Header (20 bytes, little endian):
  0x00 u16 FontLength       size of file
  0x02 u8  FontCompress     0 = uncompressed
  0x03 u8  FontDataBlocks   5
  0x04 u16 InfoBlockOffset  0x000E in every 1995 DOS font (NOT 0x0010 as the
                            GPL source comment claims); Set_Font reads
                            MaxHeight at info+4 and MaxWidth at info+5,
                            i.e. absolute 18 and 19.
  0x06 u16 OffsetBlockOffset  -> u16[nchars] offset of each glyph's bitmap
  0x08 u16 WidthBlockOffset   -> u8[nchars]  advance/blit width of each glyph
  0x0A u16 DataBlockOffset    -> start of packed bitmap data
  0x0C u16 HeightOffset       -> u16[nchars]; low byte = blank rows above the
                                 glyph, high byte = number of stored rows
  0x0E u16 (0x1012)         "UnknownConst"; also the start of the info block
  0x10 u8  pad (0)
  0x11 u8  CharCount        number of glyphs MINUS one
  0x12 u8  MaxHeight        cell height, the value Set_Font puts in FontHeight
  0x13 u8  MaxWidth         widest glyph

Glyph bitmap: 4 bits per pixel, 2 pixels per byte, LOW nibble is the LEFT
pixel (Buffer_Print consumes `color_packed & 0x0F` first, then `>> 4`).
Row stride = ceil(width / 2) bytes. Nibble value 0 is background/transparent
(Buffer_Print only writes when ColorXlat[0][value] is non-zero); values 1..15
index the 16-entry font palette.
"""
import struct


class Font:
    def __init__(self, data, name='?'):
        self.name = name
        self.data = data
        (self.file_length, self.compress, self.datablocks) = struct.unpack_from('<HBB', data, 0)
        (self.info_off, self.offsets_off, self.widths_off,
         self.data_off, self.heights_off, self.unknown_const) = struct.unpack_from('<HHHHHH', data, 4)
        self.pad = data[16]
        self.char_count_field = data[17]
        self.nchars = self.char_count_field + 1
        self.max_height = data[18]
        self.max_width = data[19]

        self.offsets = list(struct.unpack_from('<%dH' % self.nchars, data, self.offsets_off))
        self.widths = list(data[self.widths_off:self.widths_off + self.nchars])
        hraw = struct.unpack_from('<%dH' % self.nchars, data, self.heights_off)
        self.ytop = [h & 0xFF for h in hraw]
        self.rows = [h >> 8 for h in hraw]

    def glyph(self, c):
        """Return (width, height, [ [nibble,...] per row ]) padded to the full cell height."""
        w = self.widths[c]
        h = self.max_height
        y0 = self.ytop[c]
        nrows = self.rows[c]
        stride = (w + 1) // 2
        base = self.offsets[c]
        cell = [[0] * w for _ in range(h)]
        for r in range(nrows):
            y = y0 + r
            if y >= h:
                break
            row = self.data[base + r * stride: base + (r + 1) * stride]
            x = 0
            for b in row:
                if x < w:
                    cell[y][x] = b & 0x0F
                    x += 1
                if x < w:
                    cell[y][x] = b >> 4
                    x += 1
        return w, h, cell

    def bytes_used(self, c):
        return ((self.widths[c] + 1) // 2) * self.rows[c]
