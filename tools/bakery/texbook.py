#!/usr/bin/env python3
"""
texbook.py -- the cartridge's THIRD animation channel: a time-indexed TEXTURE SWAP
hanging off a scene-graph node's +0x10 word.

WHY THIS EXISTS, and it is the second wrong answer about the Construction Yard's fans.

It was reported twice that the fans do not turn. The first attempt decided they were
scene-graph rotation, found none, and called the ground burnt. The second decided the
console spins the TEXTURE's UV, picked the target by "the first up-facing textured
triangle", and span the barrel-vault roof band instead -- which is what was reported the
second time, with video. Neither attempt looked at node+0x10.

THE CHANNEL, read out of the ROM rather than inferred. A scene-graph node is
{Gfx* dl, N64 Mtx* rest, Handle* h, Node** children, TexBookPayload* book, ...} and the
FIFTH word, node+0x10, is non-zero on eleven nodes across the model table. It points at

    payload:  { TexAnim* ta, Gfx* src, Gfx* dst, u32 maxSplits }
    TexAnim:  { u32* times, u32* images, u32 settimgTemplate, u32 matchAddr,
                Gfx* tail, u16 count, u16 index }

with the applier at RAM 0x80080FAC and a LOAD-TIME SPLITTER at RAM 0x80080E28 which
copies the node's display list, deletes the G_SETTIMG whose w1 equals matchAddr, and
hands back the head and tail. That splitter is why `tail` is 0 in the ROM and why the
channel looks dead to anything that only reads static data: the console builds the two
halves at load and then, per frame, emits head + the chosen G_SETTIMG + tail. The
geometry is drawn ONCE with a substituted image.

WHAT THAT MEANS FOR US: a per-frame texture swap is exactly a MESH VARIANT, which is what
the cursor flipbooks already bake (bake_cursor_flipbooks). No pack format changes.

THE MEASUREMENT FOR THE FANS, which is the whole point:

    FACT node 0x801359F8 -> payload 0x801359E8 -> TexAnim 0x801359D0
    count 3, times [0, 160, 320, 480], match 0x80122D40,
    images 0x80122D40 / 0x80122E40 / 0x80122F40

Three 16x16 CI8 images, 256 bytes apart, and dumped they are a four-blade fan at three
rotations: upright cross, about 15 degrees, about 30 degrees. That is the fan, and the
period of 480 at a step of 160 is three frames.
"""
import importlib.util
import os
import struct

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.normpath(os.path.join(_HERE, "..", ".."))

_spec = importlib.util.spec_from_file_location(
    "segments", os.path.join(_ROOT, "tools", "romdump", "segments.py"))
SEG = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(SEG)


def ram_to_rom(ram):
    """RAM address -> ROM offset, through the OVERLAY table. A flat offset silently
    returns another segment's bytes, which is this project's most repeated mistake."""
    for name, romStart, romEnd, ramStart in SEG.SEGMENTS:
        if ramStart <= ram < ramStart + (romEnd - romStart):
            return romStart + (ram - ramStart)
    return None


def _u32(rom, off):
    return struct.unpack_from(">I", rom, off)[0]


def texbook(rom, node_ram):
    """-> dict for the node's texture book, or None if it carries none.

    Every field is asserted rather than trusted: a book whose times are not ascending,
    or whose first image is not the one the display list actually draws, is not a book
    we have understood, and baking it would substitute the wrong texture silently."""
    noff = ram_to_rom(node_ram)
    if noff is None:
        return None
    payload_ram = _u32(rom, noff + 0x10)
    if payload_ram == 0:
        return None
    poff = ram_to_rom(payload_ram)
    if poff is None:
        return None
    ta_ram = _u32(rom, poff + 0x00)
    src_ram = _u32(rom, poff + 0x04)
    dst_ram = _u32(rom, poff + 0x08)
    max_splits = _u32(rom, poff + 0x0C)
    toff = ram_to_rom(ta_ram)
    if toff is None:
        return None
    times_ram = _u32(rom, toff + 0x00)
    imgs_ram = _u32(rom, toff + 0x04)
    settimg = _u32(rom, toff + 0x08)
    match = _u32(rom, toff + 0x0C)
    tail = _u32(rom, toff + 0x10)
    cw = _u32(rom, toff + 0x14)
    count, index = cw >> 16, cw & 0xFFFF

    assert 1 <= count <= 16, "node %08X: texture book count %d is not plausible" % (
        node_ram, count)
    assert tail == 0, (
        "node %08X: the ROM's tail display list is %08X, not 0. The load-time splitter "
        "at RAM 0x80080E28 is supposed to fill this in, so a non-zero value here means "
        "the struct is not what we think it is." % (node_ram, tail))

    to = ram_to_rom(times_ram)
    io = ram_to_rom(imgs_ram)
    assert to is not None and io is not None, \
        "node %08X: the book's times or images are in no known segment" % node_ram
    times = [_u32(rom, to + i * 4) for i in range(count + 1)]
    images = [_u32(rom, io + i * 4) for i in range(count)]

    assert times[0] == 0, "node %08X: times do not start at 0: %s" % (node_ram, times)
    assert all(times[i] < times[i + 1] for i in range(count)), \
        "node %08X: times are not ascending: %s" % (node_ram, times)
    assert images[0] == match, (
        "node %08X: the book's first image %08X is not the one the display list draws "
        "(%08X), so we would be substituting for a texture that is not there."
        % (node_ram, images[0], match))
    assert len(set(images)) == count, \
        "node %08X: the book repeats an image: %s" % (node_ram, [hex(i) for i in images])

    # ---- EVERY SPLIT, not just the first --------------------------------------------
    # The payload's maxSplits says how many TexAnim records follow, 0x18 apart, and this
    # reader used to take record 0 and stop. For a one-record book that is the same thing
    # and nothing was wrong. For the ION CANNON's beam it is NOT: maxSplits is 7, records
    # 0..5 all match 0x8013BDF0 (the I8 glow book) and record 6 matches 0x80137DF0 (the
    # RGBA16 core). Taking only record 0 returned the glow and DROPPED THE CORE ENTIRELY
    # -- silently, because a book that reads fine is indistinguishable from a complete
    # one. Measured 25 Aug 2026 by walking all seven.
    #
    # All seven carry IDENTICAL times -- 8 frames at 160, period 1280 -- so the effective
    # schedule is a single 8-frame cycle and each split just names a different image to
    # substitute in its own G_SETTIMG. That is asserted below rather than assumed, because
    # if a future book ever disagreed, one schedule could not express it.
    splits = []
    for si in range(max_splits if max_splits else 1):
        so = toff + si * 0x18
        s_times_ram = _u32(rom, so + 0x00)
        s_imgs_ram = _u32(rom, so + 0x04)
        s_settimg = _u32(rom, so + 0x08)
        s_match = _u32(rom, so + 0x0C)
        s_cw = _u32(rom, so + 0x14)
        s_count = s_cw >> 16
        sto, sio = ram_to_rom(s_times_ram), ram_to_rom(s_imgs_ram)
        if sto is None or sio is None or not (1 <= s_count <= 16):
            continue
        s_t = [_u32(rom, sto + i * 4) for i in range(s_count + 1)]
        s_i = [_u32(rom, sio + i * 4) for i in range(s_count)]
        splits.append({"match": s_match, "settimg": s_settimg,
                       "count": s_count, "times": s_t, "images": s_i})
    assert splits, "node %08X: maxSplits=%d but no split resolved" % (node_ram, max_splits)
    assert all(sp["times"] == splits[0]["times"] for sp in splits), (
        "node %08X: the splits disagree about their times, so one schedule cannot "
        "express this book: %s" % (node_ram, [sp["times"] for sp in splits]))
    assert splits[0]["match"] == match, (
        "node %08X: split 0 disagrees with the record read above (%08X vs %08X)"
        % (node_ram, splits[0]["match"], match))

    return {
        "node": node_ram, "count": count, "index": index,
        "times": times, "images": images, "match": match,
        "settimg": settimg, "period": times[count],
        "src": src_ram, "dst": dst_ram, "max_splits": max_splits,
        # one entry per SPLIT; distinct_books is what a caller must bake
        "splits": splits,
        "distinct_books": sorted(set(sp["match"] for sp in splits)),
    }


# The eleven nodes that carry the channel, found by walking the model table. Named here
# so a re-walk that returns a different set is a loud failure rather than a quiet one.
KNOWN_NODES = {
    "FACT": [0x801359F8],
    # Found by walking every model-table slot for a non-zero node+0x10 and
    # resolving the slots back through unit_models.json. Eleven nodes carry the channel;
    # six belong to structures and these are the other five. The other five payload nodes
    # are slots 1..5, which have no geometry at all.
    "PROC": [0x801B6E18, 0x801B6DC8],   # TWO books: a warning triangle and a strip
    "SILO": [0x801B7700],
    "HPAD": [0x801BEE48],               # the landing markers
    "FIX":  [0x801B74D8],               # the repair bay's lamp
}


def main():
    import sys
    rom = open(os.path.join(_ROOT, "data", "rom", "cnc_eu.z64"), "rb").read()
    for name, nodes in sorted(KNOWN_NODES.items()):
      for node in nodes:
        b = texbook(rom, node)
        if not b:
            print("%-6s node %08X: no texture book" % (name, node))
            continue
        print("%-6s node %08X: %d images, period %d, times %s" % (
            name, node, b["count"], b["period"], b["times"]))
        print("        images %s (match %08X)" % (
            [hex(i) for i in b["images"]], b["match"]))


if __name__ == "__main__":
    main()
