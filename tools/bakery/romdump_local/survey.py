#!/usr/bin/env python3
"""
CNC3D — N64 ROM structural survey tool.

Read-only reconnaissance of the Command & Conquer (N64) ROM. Prints:
  - header identity (magic, internal name, cart id, region)
  - trailing zero-padding boundary
  - a coarse entropy/zero map (finds compressed asset blobs)
  - filename-like strings (by extension)
  - INI-style section headers
  - source file paths (../src/*.c) and function-name tokens left in the ROM

Usage:
    python3 survey.py /path/to/cnc_eu.z64 [section]
      section = header | pad | entropy | names | ini | modules | all (default)

The ROM is NEVER modified. Nothing is written to disk.
"""
import sys, re, struct, math, collections

def load(path):
    data = open(path, "rb").read()
    # Normalise to big-endian z64 if given a .n64/.v64
    magic = data[:4]
    if magic == b"\x80\x37\x12\x40":
        pass  # z64, native big-endian
    elif magic == b"\x37\x80\x40\x12":  # v64 (byteswapped halfwords)
        b = bytearray(data)
        b[0::2], b[1::2] = data[1::2], data[0::2]
        data = bytes(b)
    elif magic == b"\x40\x12\x37\x80":  # n64 (little-endian words)
        data = b"".join(data[i:i+4][::-1] for i in range(0, len(data), 4))
    return data

def section_header(data):
    name = data[0x20:0x34].split(b"\x00")[0].decode("latin1").strip()
    cart = data[0x3B:0x3F].decode("latin1")
    region = {"E":"USA","P":"Europe/PAL","J":"Japan"}.get(cart[3], f"?({cart[3]})")
    print(f"[header] internal='{name}'  cart_id={cart}  region={region}  size={len(data)} bytes")

def section_pad(data):
    end = len(data)
    while end > 0 and data[end-1] == 0:
        end -= 1
    print(f"[pad] last non-zero byte @0x{end-1:07X}; trailing zeros = {len(data)-end} bytes "
          f"({(len(data)-end)/1048576:.2f} MB); real data ~ {end/1048576:.2f} MB")
    return end

def section_entropy(data, bs=64*1024):
    print(f"[entropy] {bs//1024}KB blocks — class transitions only:")
    prev = None
    for i in range(0, len(data), bs):
        blk = data[i:i+bs]
        if not blk: break
        z = blk.count(0)/len(blk)
        cnt = collections.Counter(blk)
        ent = -sum((c/len(blk))*math.log2(c/len(blk)) for c in cnt.values())
        cls = ("PAD" if z > 0.95 else "COMPRESSED" if ent > 7.5
               else "CODE/TEXT" if ent < 5.0 else "mixed")
        if cls != prev:
            print(f"    0x{i:07X}  z={z:4.2f} ent={ent:4.2f}  {cls}")
            prev = cls

def section_names(data):
    exts = (b".INI",b".BRF",b".MAP",b".IMG",b".AUD",b".BIN",b".bin",b".ini",
            b".brf",b".map",b".img",b".SHP",b".PAL",b".WSA",b".VQA",b".MIX",b".PA4",b".PA8")
    rx = re.compile(rb'[\x20-\x7e]{4,40}')
    print("[names] filename-like strings:")
    for m in rx.finditer(data):
        s = m.group()
        if any(e in s for e in exts):
            print(f"    0x{m.start():07X}  {s.decode('latin1')}")

def section_ini(data):
    rx = re.compile(rb'\[[A-Za-z][A-Za-z0-9 _]{2,20}\]')
    secs = collections.defaultdict(list)
    for m in re.finditer(rb'[\x20-\x7e]{4,}', data):
        for mm in rx.finditer(m.group()):
            secs[mm.group()].append(m.start())
    print("[ini] section headers (>=1 hit):")
    for k in sorted(secs):
        v = secs[k]
        print(f"    {k.decode('latin1'):22s} x{len(v):<3d} first@0x{v[0]:07X}")

def section_modules(data):
    paths = sorted(set(m.group().decode('latin1')
        for m in re.finditer(rb'\.\./src/[\w./-]+\.(?:c|h|cpp|s)', data)))
    print(f"[modules] {len(paths)} source paths:")
    for p in paths: print("   ", p)
    funcs = sorted(set(m.group().decode('latin1')
        for m in re.finditer(rb'[A-Za-z_][A-Za-z0-9_]{2,40}\(\)', data)))
    print(f"[modules] {len(funcs)} Ident() tokens:")
    for f in funcs: print("   ", f)

def main():
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    data = load(sys.argv[1])
    what = sys.argv[2] if len(sys.argv) > 2 else "all"
    if what in ("header","all"):   section_header(data)
    if what in ("pad","all"):      section_pad(data)
    if what in ("entropy","all"):  section_entropy(data)
    if what in ("names","all"):    section_names(data)
    if what in ("ini","all"):      section_ini(data)
    if what in ("modules","all"):  section_modules(data)

if __name__ == "__main__":
    main()
