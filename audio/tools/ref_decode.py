"""A literal, independent transcription of common/auduncmp.cpp Audio_Unzap and the
AUD chunk walk, in Python. Its only job is to disagree with the C if the C is wrong.

    python3 tools/ref_decode.py SOUNDS.MIX NUYELL1.AUD out.raw
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mixlib import Mix

ZapTabTwo = [-2, -1, 0, 1]
ZapTabFour = [-9, -8, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 8]

def clamp(x, lo, hi):
    return hi if x > hi else (lo if x < lo else x)

def audio_unzap(src, size):
    """size = declared uncompressed byte count for this chunk."""
    dst = bytearray()
    sample = 0x80
    si = 0
    remaining = size
    while remaining > 0 and si < len(src):
        shifted = (src[si] << 2) & 0xFFFF
        si += 1
        code = (shifted & 0xFF00) >> 8
        count = (shifted & 0x00FF) >> 2       # 0..63, then treated as SIGNED char
        if code == 2:
            if count & 0x20:
                # signed char arithmetic: count <<= 3 then sample += count >> 3
                c8 = (count << 3) & 0xFF
                if c8 >= 0x80:
                    c8 -= 0x100                # signed char
                sample += c8 >> 3              # arithmetic shift of a negative
                dst.append(clamp(sample, 0, 255))
                remaining -= 1
            else:
                n = count + 1
                for _ in range(n):
                    if si >= len(src):
                        break
                    dst.append(src[si]); si += 1; remaining -= 1
                sample = src[si - 1]
        elif code == 1:
            for _ in range(count + 1):
                if si >= len(src):
                    break
                c = src[si]; si += 1
                sample += ZapTabFour[c & 0x0F]
                dst.append(clamp(sample, 0, 255))
                sample += ZapTabFour[c >> 4]
                dst.append(clamp(sample, 0, 255))
                remaining -= 2
        elif code == 0:
            for _ in range(count + 1):
                if si >= len(src):
                    break
                c = src[si]; si += 1
                for sh in (0, 2, 4, 6):
                    sample += ZapTabTwo[(c >> sh) & 0x03]
                    dst.append(clamp(sample, 0, 255))
                remaining -= 4
        else:
            n = count + 1
            dst.extend(bytes([clamp(sample, 0, 255)]) * n)
            remaining -= n
    return bytes(dst)

def decode_aud(data):
    rate, size, uncomp, flags, comp = struct.unpack_from("<HIIBB", data, 0)
    p = 12
    out = bytearray()
    while p + 8 <= len(data):
        csize, usize, magic = struct.unpack_from("<HHI", data, p)
        if magic != 0x0000DEAF:
            break
        p += 8
        chunk = data[p:p + csize]; p += csize
        if csize == usize:
            out.extend(chunk)
        else:
            out.extend(audio_unzap(chunk, usize))
    return rate, flags, comp, bytes(out)

if __name__ == "__main__":
    mixpath, name, outpath = sys.argv[1], sys.argv[2], sys.argv[3]
    data = Mix(mixpath).get(name)
    rate, flags, comp, pcm = decode_aud(data)
    print("%s: %d Hz flags=%d comp=%d -> %d bytes (%.3f s)" % (name, rate, flags, comp, len(pcm), len(pcm)/rate))
    open(outpath, "wb").write(pcm)

# ---------------------------------------------------------------- SOS ADPCM
# Independent transcription of common/soscodec.cpp, used to cross check the C.
_idx = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]
_step = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,
         107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,
         724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,
         3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,
         15289,16818,18500,20350,22385,24623,27086,29794,32767]

def sos_decode_chunk(state, src):
    out = []
    index, sample = state
    for b in src:
        for ny in (b & 0x0F, (b >> 4) & 0x0F):
            st = _step[index]
            diff = st >> 3
            if ny & 4: diff += st
            if ny & 2: diff += st >> 1
            if ny & 1: diff += st >> 2
            if ny & 8: diff = -diff
            sample = clamp(sample + diff, -32768, 32767)
            index = clamp(index + _idx[ny & 7], 0, 88)
            out.append(sample)
    return (index, sample), out

def decode_aud_sos(data):
    rate, size, uncomp, flags, comp = struct.unpack_from("<HIIBB", data, 0)
    assert comp == 99, "not SOS"
    p = 12
    state = (0, 0)
    out = []
    while p + 8 <= len(data):
        csize, usize, magic = struct.unpack_from("<HHI", data, p)
        if magic != 0x0000DEAF:
            break
        p += 8
        chunk = data[p:p + csize]; p += csize
        if csize == usize:
            out.extend(struct.unpack("<%dh" % (usize // 2), chunk))
        else:
            state, s = sos_decode_chunk(state, chunk)
            out.extend(s[: usize // 2] if usize else s)
    return rate, flags, out
