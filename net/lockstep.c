/*
 * lockstep.c -- the turn scheduler. See lockstep.h for what it is and why it has no
 * sockets in it.
 *
 * THE PACKET, byte for byte. Everything is little endian and written a byte at a time
 * rather than by casting a struct over the buffer, because the two builds this has to
 * agree between are a 64 bit Mac and a 32 bit Windows, and a struct's padding is the
 * compiler's business while a wire format is ours.
 *
 *   offset  size  field
 *   0       4     magic       LS_MAGIC
 *   4       2     version     LS_VERSION
 *   6       1     seat        who sent this
 *   7       1     turn_count  how many turns of orders follow
 *   8       4     turn_top    the newest turn carried; the rest descend from it
 *   12      ...   turn_count blocks, newest first
 *
 *   each block:
 *   0       2     bytes       length of the order data that follows
 *   2       1     count       how many orders are in it
 *   3       ...   the orders, each: 1 byte length, then that many bytes
 *
 * A block with bytes == 0 and count == 0 is a peer saying "nothing on that turn", which
 * is not the same as saying nothing at all: the scheduler needs to hear from every peer
 * about every turn, and silence is what it waits for. That is the same heartbeat the
 * 1995 engine sent as FRAMEINFO.
 */
#include "lockstep.h"

#include <string.h>

#define RING(t) ((unsigned)(t) & (LS_HISTORY - 1))

/* ------------------------------------------------------------------ little endian io */

static void put_u16(unsigned char* p, unsigned v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void put_u32(unsigned char* p, unsigned v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static unsigned get_u16(const unsigned char* p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned get_u32(const unsigned char* p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

/* ---------------------------------------------------------------------------- helpers */

static void turn_clear(LsTurn* t)
{
    t->used = 0;
    t->count = 0;
    t->reported = 0;
}

/* Clear one turn slot across every seat. Called when a turn number first becomes current
   for a ring index that an older turn used to own, so a stale turn's orders can never be
   mistaken for the new one's. */
static void ring_claim(LsState* s, unsigned turn)
{
    int i;
    for (i = 0; i < LS_MAX_SEATS; i++) {
        turn_clear(&s->ring[RING(turn)][i]);
    }
}

/* --------------------------------------------------------------------------- lifetime */

int ls_init(LsState* s, int seats, int me)
{
    unsigned t;

    if (!s || seats < 1 || seats > LS_MAX_SEATS || me < 0 || me >= seats) {
        return LS_ERR_RANGE;
    }
    memset(s, 0, sizeof(*s));
    s->seats = seats;
    s->me = me;
    s->max_ahead = LS_MAX_AHEAD;
    s->turn_exec = 0;
    s->turn_send = 0;

    /* THE OPENING TURNS ARE PRE-AGREED EMPTY, and without this the match never starts.
       Orders queued on turn 0 are stamped to execute on turn LS_MAX_AHEAD, so nobody has
       anything to say about turns 0 through LS_MAX_AHEAD-1; if the scheduler still waited
       to be told about them it would wait forever. Marking them reported for every seat
       is the same thing the 1995 engine did by starting its send counter ahead of its
       execute counter. */
    for (t = 0; t < (unsigned)s->max_ahead; t++) {
        int i;
        for (i = 0; i < s->seats; i++) {
            s->ring[RING(t)][i].reported = 1;
        }
    }
    return LS_OK;
}

/* ----------------------------------------------------------------------- local orders */

int ls_local_order(LsState* s, const void* bytes, int len)
{
    LsTurn* t;
    unsigned stamp;

    if (!s || !bytes || len <= 0) return LS_ERR_RANGE;
    if (len > LS_ORDER_MAX) return LS_ERR_TOO_BIG;

    /* Stamped to execute max_ahead turns after the one being sent, which is what gives
       the packet time to arrive. */
    stamp = s->turn_send + (unsigned)s->max_ahead;
    t = &s->ring[RING(stamp)][s->me];

    /* +1 for the per order length byte. */
    if (t->used + len + 1 > LS_TURN_BYTES || t->count >= 255) {
        /* LOUD, NOT SILENT. The engine's own DoList drops overflow with an empty
           statement, and that is exactly how an action happens on one peer and not
           another. The caller is expected to treat this as fatal to the match rather
           than as a lost click. */
        return LS_ERR_FULL;
    }
    t->bytes[t->used++] = (unsigned char)len;
    memcpy(t->bytes + t->used, bytes, (size_t)len);
    t->used += len;
    t->count++;
    return LS_OK;
}

/* ---------------------------------------------------------------------------- packing */

int ls_pack(LsState* s, void* out, int outmax)
{
    unsigned char* p = (unsigned char*)out;
    unsigned top;
    int off, blocks, k;

    if (!s || !p || outmax < 12) return LS_ERR_RANGE;

    top = s->turn_send + (unsigned)s->max_ahead;

    /* This peer has now spoken for the turn it was stamping, even if it queued nothing.
       Done before the walk below so the newest block is always marked reported. */
    s->ring[RING(top)][s->me].reported = 1;

    put_u32(p + 0, LS_MAGIC);
    put_u16(p + 4, LS_VERSION);
    p[6] = (unsigned char)s->me;
    p[7] = 0;                      /* turn_count, filled once known */
    put_u32(p + 8, top);
    off = 12;
    blocks = 0;

    /* Newest turn first, then back through the redundancy window. Stop early rather than
       overrun the packet: losing the OLDEST redundant copy is a recoverable degradation,
       while emitting an oversized datagram fragments every packet in the match. */
    for (k = 0; k < LS_REDUNDANCY; k++) {
        const LsTurn* t;
        unsigned turn;

        if ((unsigned)k > top) break;      /* no turns before zero */
        turn = top - (unsigned)k;
        t = &s->ring[RING(turn)][s->me];

        /* Only turns this peer has actually spoken for may be sent. Without this a
           redundancy walk that reached back past the start of the match would claim a
           turn was reported when it never was. */
        if (!t->reported) break;
        if (off + 3 + t->used > outmax || off + 3 + t->used > LS_PACKET_MAX) break;

        put_u16(p + off, (unsigned)t->used); off += 2;
        p[off++] = (unsigned char)t->count;
        if (t->used > 0) {
            memcpy(p + off, t->bytes, (size_t)t->used);
            off += t->used;
        }
        blocks++;
    }
    p[7] = (unsigned char)blocks;

    /* Move on to stamping the next turn. */
    s->turn_send++;
    ring_claim(s, s->turn_send + (unsigned)s->max_ahead);
    /* ring_claim wiped the slot this peer is about to stamp into, including its reported
       flag, which is correct: it has not spoken for that turn yet. */

    return off;
}

/* -------------------------------------------------------------------------- unpacking */

int ls_on_packet(LsState* s, const void* bytes, int len)
{
    const unsigned char* p = (const unsigned char*)bytes;
    unsigned top;
    int seat, blocks, off, k;

    if (!s || !p) return LS_ERR_RANGE;
    s->packets_in++;

    /* VALIDATE EVERYTHING BEFORE TOUCHING THE RING. A packet arrives from another
       machine and is therefore not to be trusted: a half applied malformed packet would
       leave orders in the ring that no peer agrees on, which is a desync arriving by
       post. The whole packet is walked once to check it, and only then walked again to
       store it. */
    if (len < 12) { s->packets_bad++; return LS_ERR_BAD_PACKET; }
    if (get_u32(p + 0) != LS_MAGIC) { s->packets_bad++; return LS_ERR_BAD_PACKET; }
    if (get_u16(p + 4) != LS_VERSION) { s->packets_bad++; return LS_ERR_BAD_PACKET; }

    seat = p[6];
    blocks = p[7];
    top = get_u32(p + 8);

    if (seat < 0 || seat >= s->seats) { s->packets_bad++; return LS_ERR_BAD_PACKET; }
    /* A peer's own orders come from ls_local_order. Accepting them from the wire as well
       would execute every local action twice. */
    if (seat == s->me) { s->packets_bad++; return LS_ERR_BAD_PACKET; }
    if (blocks < 0 || blocks > LS_REDUNDANCY) { s->packets_bad++; return LS_ERR_BAD_PACKET; }

    off = 12;
    for (k = 0; k < blocks; k++) {
        unsigned blen;
        int count, walk, n;

        if (off + 3 > len) { s->packets_bad++; return LS_ERR_BAD_PACKET; }
        blen = get_u16(p + off);
        count = p[off + 2];
        if (blen > LS_TURN_BYTES) { s->packets_bad++; return LS_ERR_BAD_PACKET; }
        if (off + 3 + (int)blen > len) { s->packets_bad++; return LS_ERR_BAD_PACKET; }

        /* The order lengths inside the block must tile it exactly. A block whose lengths
           run past its own end, or stop short of it, is malformed. */
        walk = off + 3;
        for (n = 0; n < count; n++) {
            int olen;
            if (walk + 1 > off + 3 + (int)blen) { s->packets_bad++; return LS_ERR_BAD_PACKET; }
            olen = p[walk];
            if (olen <= 0 || olen > LS_ORDER_MAX) { s->packets_bad++; return LS_ERR_BAD_PACKET; }
            walk += 1 + olen;
            if (walk > off + 3 + (int)blen) { s->packets_bad++; return LS_ERR_BAD_PACKET; }
        }
        if (walk != off + 3 + (int)blen) { s->packets_bad++; return LS_ERR_BAD_PACKET; }

        off += 3 + (int)blen;
    }

    /* Second walk: store. Nothing below can fail. */
    off = 12;
    for (k = 0; k < blocks; k++) {
        unsigned blen = get_u16(p + off);
        int count = p[off + 2];
        unsigned turn;
        LsTurn* t;

        if ((unsigned)k > top) break;
        turn = top - (unsigned)k;

        /* A turn already executed is history: its redundant copies are noise now, and
           writing them would be writing into a ring slot a FUTURE turn already owns. */
        if (turn < s->turn_exec) { off += 3 + (int)blen; continue; }
        /* Equally, a turn further ahead than the ring holds cannot be stored. It should
           not happen, since a peer only ever sends max_ahead ahead of its own send turn,
           but a peer running far ahead of this one is exactly the case worth refusing
           rather than aliasing onto a live slot. */
        if (turn >= s->turn_exec + LS_HISTORY) { off += 3 + (int)blen; continue; }

        t = &s->ring[RING(turn)][seat];
        /* First copy wins. The redundant copies that follow are byte identical by
           construction, so re-storing them would be wasted work, and skipping them makes
           the store idempotent, which is what makes duplicate delivery harmless. */
        if (!t->reported) {
            if (blen > 0) memcpy(t->bytes, p + off + 3, (size_t)blen);
            t->used = (int)blen;
            t->count = count;
            t->reported = 1;
            s->orders_in += (unsigned long)count;
        }
        off += 3 + (int)blen;
    }
    return LS_OK;
}

/* --------------------------------------------------------------------------- the gate */

int ls_turn_ready(const LsState* s)
{
    int i;
    if (!s) return 0;
    for (i = 0; i < s->seats; i++) {
        if (!s->ring[RING(s->turn_exec)][i].reported) return 0;
    }
    return 1;
}

unsigned ls_waiting_mask(const LsState* s)
{
    unsigned m = 0;
    int i;
    if (!s) return 0;
    for (i = 0; i < s->seats; i++) {
        if (!s->ring[RING(s->turn_exec)][i].reported) m |= (1u << i);
    }
    return m;
}

void ls_run_turn(LsState* s, LsOrderSink sink, void* user)
{
    unsigned turn;
    int seat;

    if (!s) return;
    turn = s->turn_exec;

    /* SEAT ORDER, ALWAYS, NEVER ARRIVAL ORDER. Two peers that execute the same orders in
       a different sequence resolve a contested action differently, which is a desync that
       looks like a gameplay bug. */
    for (seat = 0; seat < s->seats; seat++) {
        const LsTurn* t = &s->ring[RING(turn)][seat];
        int off = 0, n;
        for (n = 0; n < t->count; n++) {
            int olen;
            if (off + 1 > t->used) break;
            olen = t->bytes[off];
            if (olen <= 0 || off + 1 + olen > t->used) break;
            if (sink) sink(user, seat, t->bytes + off + 1, olen);
            off += 1 + olen;
        }
    }

    s->turn_exec++;
    /* The slot this turn occupied is now free for a future turn. Claiming it here rather
       than lazily means a peer that sends a turn far ahead cannot find a stale reported
       flag waiting for it. */
    ring_claim(s, turn);
}

unsigned ls_exec_turn(const LsState* s) { return s ? s->turn_exec : 0; }
unsigned ls_send_turn(const LsState* s) { return s ? s->turn_send : 0; }
