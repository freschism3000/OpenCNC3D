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

/* WHO OWNS A RING SLOT, AND WHEN IT IS WIPED. A slot is shared by every turn with the same
 * residue modulo LS_HISTORY, so each one must be emptied before the next turn that owns it
 * can be written, and never while the turn that owns it now may still be asked for. The
 * two halves of a slot are written by different things, so they are wiped at different
 * moments:
 *
 *   ANOTHER SEAT'S HALF is written only by ls_on_packet, which accepts a turn only while
 *   it is below turn_exec + LS_HISTORY. That is exactly the moment the previous owner of
 *   the slot has executed, so wiping it on EXECUTION is both early enough and sufficient.
 *   It used to be wiped a second time when this peer began stamping that turn, which
 *   threw away whatever a peer running ahead had already delivered for it; with enough of
 *   a lead that was more turns than the redundancy window could send again.
 *
 *   THIS PEER'S OWN HALF is written only by ls_local_order and ls_pack, both at
 *   turn_send + max_ahead, so it is wiped when stamping begins there and NOT when the turn
 *   executes. Executing a turn does not end anybody else's need for it: a peer that lost
 *   every packet carrying it is still waiting, and ls_pack_from can only send again what
 *   this peer still holds. */
static void ring_claim_others(LsState* s, unsigned turn)
{
    int i;
    for (i = 0; i < LS_MAX_SEATS; i++) {
        if (i != s->me) turn_clear(&s->ring[RING(turn)][i]);
    }
}

/* --------------------------------------------------------------------------- lifetime */

int ls_init(LsState* s, int seats, int me)
{
    return ls_init_ahead(s, seats, me, LS_MAX_AHEAD);
}

int ls_init_ahead(LsState* s, int seats, int me, int ahead)
{
    unsigned t;

    if (!s || seats < 1 || seats > LS_MAX_SEATS || me < 0 || me >= seats) {
        return LS_ERR_RANGE;
    }
    /* CLAMPED, NOT TRUSTED: this arrives from the wire, and a peer that asked for zero
       would pre-agree no turns at all and stamp orders into the turn already executing. */
    if (ahead < LS_AHEAD_MIN) ahead = LS_AHEAD_MIN;
    if (ahead > LS_AHEAD_MAX) ahead = LS_AHEAD_MAX;
    memset(s, 0, sizeof(*s));
    s->seats = seats;
    s->me = me;
    s->max_ahead = ahead;
    s->turn_exec = 0;
    s->turn_send = 0;

    /* THE OPENING TURNS ARE PRE-AGREED EMPTY, and without this the match never starts.
       Orders queued on turn 0 are stamped to execute on turn max_ahead, so nobody has
       anything to say about turns 0 through max_ahead-1; if the scheduler still waited
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

int ls_turn_room(const LsState* s, int len)
{
    const LsTurn* t;
    int room;

    if (!s || len <= 0 || len > LS_ORDER_MAX) return 0;
    t = &s->ring[RING(s->turn_send + (unsigned)s->max_ahead)][s->me];
    room = (LS_TURN_BYTES - t->used) / (len + 1);
    if (room < 0) room = 0;
    /* The count is one byte on the wire. */
    if (room > 255 - t->count) room = 255 - t->count;
    return room > 0 ? room : 0;
}

/* ---------------------------------------------------------------------------- packing */

int ls_pack(LsState* s, void* out, int outmax)
{
    unsigned char* p = (unsigned char*)out;
    unsigned top;
    int off, blocks, k;

    if (!s || !p || outmax < 12) return -LS_ERR_RANGE;

    top = s->turn_send + (unsigned)s->max_ahead;
    /* THE NEWEST TURN MUST FIT, and this refuses rather than sends a packet without it.
       The walk below stops on size, which is right for the older redundant copies and
       catastrophic for the newest one: this peer has just marked that turn as spoken for,
       so dropping it puts an empty packet on the wire and leaves every other peer waiting
       on a turn that will never be sent again. LS_TURN_BYTES makes it impossible for the
       caller's own orders to overflow a full-sized buffer; what this catches is a buffer
       smaller than LS_PACKET_MAX. */
    if (12 + 3 + s->ring[RING(top)][s->me].used > outmax) return -LS_ERR_TOO_BIG;

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
       while emitting an oversized datagram fragments every packet in the match. The
       newest block is never what is dropped here; it was checked above.
       THE WINDOW IS THE FULL LS_REDUNDANCY AT EVERY LOOKAHEAD, because this peer's own
       turns are kept after they execute so that ls_pack_from can answer with them. It was
       shorter at a small lookahead while executing wiped them. See ls_pack's contract for
       what that costs. */
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
    /* The slot this peer is about to stamp into is wiped, including its reported flag,
       which is correct: it has not spoken for that turn yet. Only this peer's own half;
       see ring_claim_others for why the rest of the slot is left alone. */
    turn_clear(&s->ring[RING(s->turn_send + (unsigned)s->max_ahead)][s->me]);

    return off;
}

int ls_pack_from(const LsState* s, unsigned from, void* out, int outmax)
{
    unsigned char* p = (unsigned char*)out;
    unsigned newest, top, turn;
    int off, n, k;

    if (!s || !p || outmax < 12) return -LS_ERR_RANGE;
    if (s->turn_send == 0) return 0;                        /* nothing spoken for yet */
    newest = s->turn_send - 1u + (unsigned)s->max_ahead;    /* the last turn ls_pack sent */
    if (from > newest) return 0;                            /* not stamped yet: nothing to send */
    /* The slot for `from` has been handed to a later turn once stamping has reached
       from + LS_HISTORY, and what it holds then is not that turn's orders. */
    if (from + (unsigned)(LS_HISTORY - 1) < s->turn_send + (unsigned)s->max_ahead) return 0;

    /* OLDEST FIRST when deciding what fits, the reverse of ls_pack. The turn asked for is
       the one that matters, so it is the one that must not be the block left out. */
    off = 12;
    n = 0;
    for (turn = from; turn <= newest && n < LS_REDUNDANCY; turn++) {
        const LsTurn* t = &s->ring[RING(turn)][s->me];
        if (!t->reported) break;
        if (off + 3 + t->used > outmax || off + 3 + t->used > LS_PACKET_MAX) {
            /* SAID PLAINLY WHEN IT IS THE ASKED-FOR TURN THAT WILL NOT FIT, because a
               caller that read that as 0 would take "your buffer is too small" for
               "there is nothing to send" and leave the asking peer stuck for ever with
               nothing written down. LS_TURN_BYTES makes it unreachable with a full-sized
               buffer; it is here so a smaller one fails loudly instead. */
            if (n == 0) return -LS_ERR_TOO_BIG;
            break;
        }
        off += 3 + t->used;
        n++;
    }
    if (n == 0) return 0;
    top = from + (unsigned)n - 1u;

    /* THE SAME PACKET ls_pack WRITES, so the receiver needs no second parser: a top, and
       the blocks newest first beneath it. */
    put_u32(p + 0, LS_MAGIC);
    put_u16(p + 4, LS_VERSION);
    p[6] = (unsigned char)s->me;
    p[7] = (unsigned char)n;
    put_u32(p + 8, top);
    off = 12;
    for (k = 0; k < n; k++) {
        const LsTurn* t = &s->ring[RING(top - (unsigned)k)][s->me];
        put_u16(p + off, (unsigned)t->used); off += 2;
        p[off++] = (unsigned char)t->count;
        if (t->used > 0) {
            memcpy(p + off, t->bytes, (size_t)t->used);
            off += t->used;
        }
    }
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

int ls_set_absent(LsState* s, int seat)
{
    return ls_set_absent_at(s, seat, 0u);
}

int ls_set_absent_at(LsState* s, int seat, unsigned turn)
{
    if (!s || seat < 0 || seat >= s->seats || seat == s->me) return LS_ERR_RANGE;
    /* A turn already gone by is an absence that starts now. Clamping rather than refusing,
       because the caller that gets here is a peer catching up on a goodbye it heard late,
       and the alternative is a seat that is never skipped at all. */
    if (turn < s->turn_exec) turn = s->turn_exec;
    /* THE LAST CALLER WINS, because the caller is the one that knows whose number this is:
       a seat can be named locally as a guess and then again by the host, whose number is
       the one every peer must end up on, and a scheduler that kept the first would keep
       the guess. Whoever calls this is responsible for not overwriting the host with a
       guess; see how the goodbye is handled. */
    s->absent[seat] = 1;
    s->absent_from[seat] = turn;
    return LS_OK;
}

/* Has `seat` stopped counting by the time turn `turn` runs? */
static int seat_gone_by(const LsState* s, int seat, unsigned turn)
{
    return s->absent[seat] && turn >= s->absent_from[seat];
}

unsigned ls_seat_first_missing(const LsState* s, int seat)
{
    unsigned t;
    if (!s || seat < 0 || seat >= s->seats) return 0;
    for (t = s->turn_exec; t < s->turn_exec + (unsigned)(LS_HISTORY - 1); t++) {
        if (!s->ring[RING(t)][seat].reported) return t;
    }
    return s->turn_exec + (unsigned)(LS_HISTORY - 1);
}

int ls_turn_ready(const LsState* s)
{
    int i;
    if (!s) return 0;
    for (i = 0; i < s->seats; i++) {
        /* A computer has nothing to say, and neither has a seat that has walked out, from
           the turn it walked out on. Below that turn it is still a seat and is waited for,
           because its last orders execute on every peer or on none. */
        if (seat_gone_by(s, i, s->turn_exec)) continue;
        if (!s->ring[RING(s->turn_exec)][i].reported) return 0;
    }
    return 1;
}

int ls_ahead(const LsState* s)
{
    return s ? s->max_ahead : LS_MAX_AHEAD;
}

unsigned ls_waiting_mask(const LsState* s)
{
    unsigned m = 0;
    int i;
    if (!s) return 0;
    for (i = 0; i < s->seats; i++) {
        if (seat_gone_by(s, i, s->turn_exec)) continue;
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
        /* A SEAT THAT HAS GONE CONTRIBUTES NOTHING FROM ITS OWN TURN ON, whether or not
           its orders happen to be sitting in this peer's ring. That is the other half of
           ls_set_absent_at: skipping it in the barrier alone would let a peer that
           received the departing seat's last packet execute those orders while a peer
           that did not executed none, and the two worlds would part on the spot. */
        if (seat_gone_by(s, seat, turn)) continue;
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
    /* The other seats' half of the slot this turn occupied is now free for a future turn.
       Claiming it here rather than lazily means a peer that sends a turn far ahead cannot
       find a stale reported flag waiting for it. This peer's own half is kept until it
       stamps into the slot again; see ring_claim_others. */
    ring_claim_others(s, turn);
}

unsigned ls_exec_turn(const LsState* s) { return s ? s->turn_exec : 0; }
unsigned ls_send_turn(const LsState* s) { return s ? s->turn_send : 0; }
