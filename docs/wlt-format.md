# .WLT, the cartridge's wavelet image container

`.WLT` holds the N64 briefing artwork: portraits, mission maps, faction emblems and the
short flipbook animations that play behind a briefing. There are **336 distinct names in
the cartridge, 337 archive records** (`MOEBIUS.WLT` is listed twice in the main table),
in **nine frame sizes**.

It is the last of the cartridge's image containers to open. `.IMG` and `.JIM` were
reversed by reading their headers; `.WLT` could not be, because the payload is a
wavelet bitstream with no published grammar.

## What it is not

It is not a reimplementation. The decoder in `tools/bakery/wlt.py` **runs the
cartridge's own decoder** under the MIPS III interpreter in `tools/bakery/n64emu.py`:
load the two overlays the routine needs into a flat 8 MB RDRAM image, call the entry
points in the order the mission briefing loader calls them, read the pixels back out. What comes
back is what the console produces, byte for byte, with no reverse-engineered
approximation in the middle.

The codec is a wavelet library, and the cartridge was never stripped of its debug
strings, so it says so itself. `../src/n64wavelet.c` is at ROM 0x1BC100, and the
attribution block just after it names the Phantom Works group at McDonnell Douglas
Corporation and the IRAD programme the work came out of. Its own strings also name its
filter bank (CUSTOM / TWO-SIX / FIVE-THREE / HAAR / REVERSIBLE) and its coefficient
models (SPLIT FIELD / ORIENTATION / SCALE & DIAGONAL / SCALE / GLOBAL / LINE SEGMENTS /
ZERO RUNS). None of that was needed to decode a file, and it is recorded because it is
the map for anyone who later wants to decode one without an interpreter.

## Container header

Big-endian. The routine that parses it is `F_PLANESIZE` in the table below; it returns
the size of the output plane and rejects anything whose first byte is not `'W'`.

| Offset | Type | Meaning |
|---|---|---|
| +0x00 | u8 | `'W'` (0x57) magic |
| +0x01 | u8 | flags |
| +0x02 | u32 | control word; the top two bits select the colour model |
| +0x06 | u16 | width |
| +0x08 | u16 | height |
| +0x0A | u8 | coefficient depth (>= 9 means a 2-byte intermediate plane) |

Everything after that is the bitstream, which is why sibling frames of one animation
differ in length: each frame codes only the coefficients it needs. All sixteen
`WRENCH*.WLT` frames are 80 x 54 and range from 902 to 1727 bytes.

## The pixels have no alpha

The codec emits **RGB888**, `w*h*3` bytes. The loader then calls `F_FIXUP`, which packs
that buffer in place to N64 **RGBA5551 with the alpha bit forced to 1**. So a `.WLT`
carries no transparency at all: a frame that looks like a cut-out portrait is a subject
on a black field, and anything that composites one has to key the black itself.

## Addresses, and how to convert one

**Every address below is a RAM address, and a ROM offset is only meaningful together
with the overlay it belongs to.** The cartridge DMAs overlays, so `rom = ram - delta`
and the delta is that overlay's own. Two segments carry this decode path:

| segment | ROM start | ROM end | RAM start | delta |
|---|---|---|---|---|
| resident | 0x0001000 | 0x00C6EA0 | 0x80000400 | 0x7FFFF400 |
| BriefingCode | 0x01B67D0 | 0x020E400 | 0x801C2600 | 0x8000BE30 |

The resident base is not a guess: the ROM header's entry point at +0x08 reads
`80000400`. Both segment rows come from the segment table that
`tools/bakery/romdump_local/segments.py` recovers from the ROM's own overlay loader.

| name | RAM | segment | ROM | first instruction | what it is |
|---|---|---|---|---|---|
| `F_LOADER` | 0x801D9E00 | BriefingCode | 0x01CDFD0 | `addiu sp,sp,-112` | the mission briefing image loader; the call order below is its own |
| `F_SETMODE` | 0x801DE2E4 | BriefingCode | 0x01D24B4 | `addiu v0,zero,-1` | codec mode select |
| `F_SCRATCHSIZE` | 0x801DD3EC | BriefingCode | 0x01D15BC | `addiu sp,sp,-40` | scratch bytes this stream needs |
| `F_GIVESCRATCH` | 0x801E99E0 | BriefingCode | 0x01DDBB0 | `addiu sp,sp,-24` | hand the codec its scratch |
| `F_PLANESIZE` | 0x801DD790 | BriefingCode | 0x01D1960 | `addiu sp,sp,-32` | parse the container header, return plane bytes |
| `F_DECODE` | 0x801DBEE0 | BriefingCode | 0x01D00B0 | `addiu sp,sp,-112` | `decode(src, len, info, extra)`; `info[+8]`=width, `info[+12]`=height |
| `F_FIXUP` | 0x800508AC | resident | 0x00514AC | `addu a3,a0,zero` | RGB888 -> RGBA5551, alpha forced to 1 |
| `S_MODULENAME` | 0x801C7F30 | BriefingCode | 0x01BC100 | | the string `../src/n64wavelet.c` |

Five resident allocator entry points are hooked so the routine does not need a booted
OS: 0x80073478, 0x800732C4, 0x80073420, 0x80073548, 0x8007358C (ROM 0x0074078,
0x0073EC4, 0x0074020, 0x0074148, 0x007418C). **None of the five actually fires**,
measured by instrumenting the hook table across the cartridge's files: `decode()` hands
the codec every buffer it needs up front, so the game heap is never entered. They stay
as insurance, because a stream that did allocate would otherwise run the real allocator
against a heap that was never initialised and fail as plausible garbage rather than as
an error.

### One offset in an earlier write-up was wrong, and the way it was wrong is the lesson

`F_PLANESIZE` was published as "RAM 0x801DD790 (ROM 0x1D6960)". 0x801DD790 is in
BriefingCode, whose delta is `0x801C2600 - 0x1B67D0 = 0x8000BE30`, so the ROM offset is
**0x1D1960**. 0x1D6960 is 0x5000 away, still inside the same overlay, and still
disassembles into instructions: nothing about it looks wrong until you call it.

That is why `wlt.py offsets <ROM>` exists. It recomputes the whole table from the two
segment constants, checks every published address lands inside a segment, disassembles
the first instruction at each derived offset, and reads the module-name string back out
of the ROM at the offset the arithmetic gives. Read the numbers out of that command, not
out of prose.

`offsets` does **not** catch a wrong address that still lands in the segment: pointed at
0x801E2790 (the RAM address the wrong ROM offset implies) it prints a clean table. What
catches that is `verify`, which then fails all nine smoke frames with
`MemoryError: bad address FFFFA734`. The two commands are one gate together, not either
alone.

## The archive base

The main directory table is at ROM 0x0460540 with 1422 records, and the data base for a
table is the byte immediately after its last record:

    0x0460540 + 24 * 1422 = 0x468A90

A base of 0x470000 is off by 0x7570 and yields streams whose first byte is not `'W'`.
That is the first thing to check when a `.WLT` will not decode: with the wrong base,
all 336 fail the magic test rather than some of them.

**Three of the 336 are also LZ77-compressed inside the archive** (method 0x2200):
`RIVG0901.WLT`, `RIVG0903.WLT` and `RIVG0904.WLT`. The other 333 are stored. That path is
small enough to leave untested by accident, so `RIVG0901.WLT` is in the smoke set;
leaving the decompression unapplied makes exactly those three lose their `'W'` magic.
(`RIVG0901` and `RIVG0903` are 839 bytes each and decode to the same digest: the
cartridge holds that frame twice.)

## Verifying it

    python3 tools/bakery/wlt.py offsets <ROM>              # the address table, re-derived
    python3 tools/bakery/wlt.py verify  <ROM> -j9 -s       # the ten-file smoke set
    python3 tools/bakery/wlt.py verify  <ROM> -j12 out.txt # all 336, and write a manifest

`verify` decodes and takes the sha256 of the RGBA5551 output of each frame. Ten of
those digests are stored in `REF_SMOKE` in the tool, one per distinct frame size plus one
of the three archive-compressed streams; the sha256 of the sorted `NAME|WxH|sha256` lines
of all 336 is stored as `REF_ALL`.

**The digests are the gate, not the fact that a file decoded.** Two mutations were run
to establish that. Removing the branch-likely nullification from the `BEQL` arm of the
interpreter, and skipping the `F_FIXUP` call, each leave all ten smoke frames decoding
at exactly the right size with no error raised and every pixel different. A check on
size and success alone passes both. The stored digests fail both, 10 of 10.

Measured runs on the EU cartridge (header id `NCCP`, CRC `AE5B9465 C54D6576`):

    WLT|total|files=336|decoded=336|failed=0|hdrmismatch=0|refok=10|refbad=0
        |all=match|distinct_sizes=9
        |88x61:203,80x54:90,172x103:26,172x104:6,160x240:4,172x84:3,120x78:2,117x78:1,236x142:1

    WLTOFF|segments=2|addrs=13|inseg=13|module=ok|names=336|nonw=0|bad=0

336 of 336, exit 0, 2m27s on twelve workers. The ten-file smoke set is 25 seconds on nine
workers and about a minute on one. Two independent full runs produced 336 byte-identical
lines, and the smoke run's digests match the same files' digests in the full run, so
neither the worker count nor the run changes a pixel.

Content was also looked at rather than only hashed: the Kane and Seth portraits, the
Moebius and Nikoomba portraits, the GDI gold eagle and the Nod scorpion on their stone
plates, the Nod hex badge and the wrench icon all come out as the pictures they are
named after.

`G183` in the gate suite runs both commands with the smoke set and reads
`refok`/`refbad`, not merely "decoded". Six mutations were put against it and each turned
it red with its own message: the wrong archive base, the wrong-delta `F_PLANESIZE`
address, the dead `BEQL` nullification, the skipped `F_FIXUP`, an address outside every
segment, and the archive LZ77 left unapplied.

## What is not done

- **Nothing in the game consumes a `.WLT` yet.** The decoder is a bakery tool; no pack
  carries a briefing image and no renderer draws one. Briefing screens are a separate
  piece of work and this only removes the blocker.
- **The bitstream itself is still unreversed.** Decoding needs the cartridge and the
  interpreter. That is fine for a bakery step, which has both, and it means there is no
  decoder that could run inside the game at runtime.
- **The interpreter covers what this path executes and no more**: integer MIPS III plus
  the FPU ops these routines use, KSEG0 only, no MMU, no COP0, no exceptions,
  no NaN-ordered compares. Another routine may well need instructions it does not have;
  it raises rather than guessing, so that failure is loud.
- **One frame size has a single example** (236x142, `GDIEVA_C.WLT`) and another has one
  (117x78, `FINL0449.WLT`), so those two shapes are each proved by exactly one file.
