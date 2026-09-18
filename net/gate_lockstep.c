/*
 * gate_lockstep.c -- proves the turn scheduler, with no engine and no sockets.
 *
 * WHY THIS EXISTS AND WHAT IT IS WORTH. The scheduler's job is to make every peer
 * execute the same orders on the same frame in the same sequence, and that property can
 * be checked completely without a game: run two or more schedulers in one process, hand
 * each other's packets across, and assert the order streams they produce are identical.
 * It cannot tell you the ENGINE stays in step, which needs two real brains and is a
 * separate gate; it can tell you the scheduler is not the reason it did not.
 *
 * The interesting legs are the unkind ones. A scheduler that works when every packet
 * arrives on time is not evidence of anything, so the drop legs below delete packets on
 * purpose and assert the redundancy window covers them, and the malformed leg feeds it
 * garbage and asserts nothing reaches the ring.
 *
 * WHAT THE TRANSCRIPT COMPARISON DOES AND DOES NOT PROVE, because the difference is easy
 * to misread later. It proves the peers AGREE. It does not prove the sequence they agree
 * on is the intended one: reversing the seat loop on every peer keeps them identical and
 * these legs stay green, correctly, because a globally consistent order is not a desync.
 * What it does catch is a peer ordering differently from its neighbours, which is the
 * real failure. Measured by breaking it on purpose: making each peer run its OWN orders
 * first fails the clean leg and the four peer leg immediately, while a whole-loop reversal
 * does not. If a future change needs the specific seat sequence pinned as well, that is a
 * separate assertion against a recorded expected transcript, and it is not this file.
 *
 *   cc -std=c89 -o gate_lockstep gate_lockstep.c lockstep.c && ./gate_lockstep
 *
 * Exit status is zero when every leg passes, and the count of failures otherwise, so a
 * suite can read the status and a person can read the log.
 */
#include "lockstep.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0;
static int g_fail = 0;

static void ok(const char* what)
{
    g_pass++;
    printf("OK   %s\n", what);
}

static void bad(const char* what)
{
    g_fail++;
    printf("FAIL %s\n", what);
}

static void check(int cond, const char* what)
{
    if (cond) ok(what); else bad(what);
}

/* ------------------------------------------------------------- the recording sink */

/* Every order a scheduler executes is appended here as "seat:first_byte ", so two peers'
   transcripts can be compared as plain strings. The first byte of each order is enough
   to tell orders apart because the tests choose distinct ones. */
typedef struct {
    char  text[8192];
    int   len;
    int   orders;
} Transcript;

static void sink(void* user, int seat, const void* bytes, int len)
{
    Transcript* tr = (Transcript*)user;
    const unsigned char* b = (const unsigned char*)bytes;
    char line[64];
    int n;
    n = sprintf(line, "%d:%02X:%d ", seat, b[0], len);
    if (tr->len + n < (int)sizeof(tr->text)) {
        memcpy(tr->text + tr->len, line, (size_t)n);
        tr->len += n;
        tr->text[tr->len] = 0;
    }
    tr->orders++;
}

/* ------------------------------------------------------------------------ the legs */

/* Two peers, every packet delivered, both queue orders on most turns. The transcripts
   must match exactly, and both must have executed the same number of turns. */
static void leg_two_peers_clean(void)
{
    LsState a, b;
    Transcript ta, tb;
    unsigned char pa[LS_PACKET_MAX], pb[LS_PACKET_MAX];
    int la, lb, turn;

    memset(&ta, 0, sizeof ta);
    memset(&tb, 0, sizeof tb);
    check(ls_init(&a, 2, 0) == LS_OK, "clean: seat 0 inits");
    check(ls_init(&b, 2, 1) == LS_OK, "clean: seat 1 inits");

    for (turn = 0; turn < 40; turn++) {
        unsigned char oa[22], ob[22];
        memset(oa, 0, sizeof oa);
        memset(ob, 0, sizeof ob);
        oa[0] = (unsigned char)(0x10 + (turn % 7));
        ob[0] = (unsigned char)(0x80 + (turn % 5));

        /* Seat 0 gives an order every turn, seat 1 every third, so the empty-turn path
           is exercised as well as the busy one. */
        ls_local_order(&a, oa, (int)sizeof oa);
        if (turn % 3 == 0) ls_local_order(&b, ob, (int)sizeof ob);

        la = ls_pack(&a, pa, (int)sizeof pa);
        lb = ls_pack(&b, pb, (int)sizeof pb);
        if (la < 0 || lb < 0) { bad("clean: pack failed"); return; }

        if (ls_on_packet(&a, pb, lb) != LS_OK) { bad("clean: seat 0 refused a good packet"); return; }
        if (ls_on_packet(&b, pa, la) != LS_OK) { bad("clean: seat 1 refused a good packet"); return; }

        while (ls_turn_ready(&a)) ls_run_turn(&a, sink, &ta);
        while (ls_turn_ready(&b)) ls_run_turn(&b, sink, &tb);
    }

    check(ta.orders > 0, "clean: orders actually executed");
    check(ta.orders == tb.orders, "clean: both peers executed the same number of orders");
    check(strcmp(ta.text, tb.text) == 0, "clean: the two transcripts are identical");
    check(ls_exec_turn(&a) == ls_exec_turn(&b), "clean: both peers reached the same turn");
}

/* The same, but every Nth packet in each direction is thrown away. The redundancy window
   must cover the gap, so the transcripts must still match and no turn may be lost. */
static void leg_packet_loss(int drop_every)
{
    LsState a, b;
    Transcript ta, tb;
    unsigned char pa[LS_PACKET_MAX], pb[LS_PACKET_MAX];
    int la, lb, turn;
    char what[96];

    memset(&ta, 0, sizeof ta);
    memset(&tb, 0, sizeof tb);
    ls_init(&a, 2, 0);
    ls_init(&b, 2, 1);

    for (turn = 0; turn < 60; turn++) {
        unsigned char oa[22];
        memset(oa, 0, sizeof oa);
        oa[0] = (unsigned char)(0x20 + (turn % 11));
        ls_local_order(&a, oa, (int)sizeof oa);

        la = ls_pack(&a, pa, (int)sizeof pa);
        lb = ls_pack(&b, pb, (int)sizeof pb);

        /* Drop in BOTH directions on the same turns, which is the harder case: each peer
           is missing a report and neither can advance until a later packet carries the
           redundant copy. */
        if (turn % drop_every != 0) {
            ls_on_packet(&a, pb, lb);
            ls_on_packet(&b, pa, la);
        }

        while (ls_turn_ready(&a)) ls_run_turn(&a, sink, &ta);
        while (ls_turn_ready(&b)) ls_run_turn(&b, sink, &tb);
    }

    sprintf(what, "loss 1 in %d: transcripts still identical", drop_every);
    check(strcmp(ta.text, tb.text) == 0, what);
    sprintf(what, "loss 1 in %d: orders still delivered (%d)", drop_every, ta.orders);
    check(ta.orders > 0, what);
    sprintf(what, "loss 1 in %d: both peers reached the same turn", drop_every);
    check(ls_exec_turn(&a) == ls_exec_turn(&b), what);
}

/* Four peers, all talking to each other, with the seat-order rule under real pressure:
   every peer issues an order on the same turn, and every transcript must agree. */
static void leg_four_peers(void)
{
    LsState p[4];
    Transcript tr[4];
    unsigned char pkt[4][LS_PACKET_MAX];
    int len[4];
    int i, j, turn;

    for (i = 0; i < 4; i++) {
        memset(&tr[i], 0, sizeof tr[i]);
        ls_init(&p[i], 4, i);
    }

    for (turn = 0; turn < 30; turn++) {
        for (i = 0; i < 4; i++) {
            unsigned char o[22];
            memset(o, 0, sizeof o);
            o[0] = (unsigned char)(0x40 + i);
            ls_local_order(&p[i], o, (int)sizeof o);
            len[i] = ls_pack(&p[i], pkt[i], (int)sizeof pkt[i]);
        }
        /* Deliver in a DIFFERENT order to each peer, so that any dependence on arrival
           order shows up as a mismatched transcript rather than passing by luck. */
        for (i = 0; i < 4; i++) {
            for (j = 3; j >= 0; j--) {
                if (i != j) ls_on_packet(&p[i], pkt[j], len[j]);
            }
        }
        for (i = 0; i < 4; i++) {
            while (ls_turn_ready(&p[i])) ls_run_turn(&p[i], sink, &tr[i]);
        }
    }

    check(tr[0].orders > 0, "four peers: orders executed");
    check(strcmp(tr[0].text, tr[1].text) == 0 &&
          strcmp(tr[0].text, tr[2].text) == 0 &&
          strcmp(tr[0].text, tr[3].text) == 0,
          "four peers: all four transcripts identical despite different arrival order");
}

/* AN OUTAGE LONGER THAN THE REDUNDANCY WINDOW, repaired by asking for the turn.
 *
 * Each step, each peer does what the game loop does once it is due: stamp and send its
 * turn if it has not, run the turn if it may, and otherwise, being stuck, send its last
 * packet again and (when `ask` is set) name the turn it is stuck on, which the other peer
 * answers with ls_pack_from. Delivery is instant; the outage throws away everything one
 * peer (or both) sends for a stretch of steps.
 *
 * WHY THIS WAS A DEADLOCK. The peer that can still hear keeps running until it has used
 * every turn its partner told it about, which is up to 2A + 1 turns past the one the deaf
 * partner is stuck on, so when the outage ends the window of six turns it keeps re-sending
 * no longer holds that turn. Nothing sent afterwards ever carries it again. The control
 * run, with the request switched off, is there to show that this leg is testing the
 * request and not a lucky schedule: the same outage must stall without it. */
static int outage_run(int ahead, int both, int ask, int steps, Transcript* ta, Transcript* tb,
                      unsigned* exec_a, unsigned* exec_b)
{
    static LsState p[2];
    unsigned char last[2][LS_PACKET_MAX], rep[LS_PACKET_MAX];
    int lastlen[2] = { 0, 0 }, begun[2] = { 0, 0 };
    const int cut_from = 20, cut_to = 60;
    int step, i;
    Transcript* tr[2];

    tr[0] = ta; tr[1] = tb;
    ls_init_ahead(&p[0], 2, 0, ahead);
    ls_init_ahead(&p[1], 2, 1, ahead);
    for (step = 0; step < steps; step++) {
        for (i = 0; i < 2; i++) {
            const int other = 1 - i;
            /* seat 1's packets to seat 0 are lost in the cut; with `both`, seat 0's too */
            const int lost = step >= cut_from && step < cut_to && (i == 1 || both);
            if (!begun[i]) {
                unsigned char o[22];
                memset(o, 0, sizeof o);
                o[0] = (unsigned char)(0x30 + i * 0x40 + (step % 13));
                if (step % 2 == i) ls_local_order(&p[i], o, (int)sizeof o);
                lastlen[i] = ls_pack(&p[i], last[i], (int)sizeof last[i]);
                begun[i] = 1;
                if (!lost) ls_on_packet(&p[other], last[i], lastlen[i]);
            }
            if (ls_turn_ready(&p[i])) {
                ls_run_turn(&p[i], sink, tr[i]);
                begun[i] = 0;
                continue;
            }
            if (!lost) ls_on_packet(&p[other], last[i], lastlen[i]);
            if (ask && (ls_waiting_mask(&p[i]) & (1u << other))) {
                /* The request itself can be lost too; its answer travels the other way. */
                const int answer_lost = step >= cut_from && step < cut_to && (other == 1 || both);
                int n;
                if (!lost) {
                    n = ls_pack_from(&p[other], ls_exec_turn(&p[i]), rep, (int)sizeof rep);
                    if (n > 0 && !answer_lost) ls_on_packet(&p[i], rep, n);
                }
            }
        }
    }
    *exec_a = ls_exec_turn(&p[0]);
    *exec_b = ls_exec_turn(&p[1]);
    return 0;
}

static void leg_long_outage(int ahead, int both)
{
    Transcript ta, tb;
    unsigned ea, eb;
    char what[160];
    const int steps = 400;

    memset(&ta, 0, sizeof ta);
    memset(&tb, 0, sizeof tb);
    outage_run(ahead, both, 1, steps, &ta, &tb, &ea, &eb);
    sprintf(what, "outage %s, lookahead %d: recovered with the request (turns %u and %u)",
            both ? "both ways" : "one way", ahead, ea, eb);
    check(ea > 300 && eb > 300, what);
    sprintf(what, "outage %s, lookahead %d: the two transcripts are identical",
            both ? "both ways" : "one way", ahead);
    /* compared over the turns both ran: the shorter transcript is a prefix of the longer */
    check(ta.orders > 0 && tb.orders > 0
          && strncmp(ta.text, tb.text, (size_t)(ta.len < tb.len ? ta.len : tb.len)) == 0, what);

    memset(&ta, 0, sizeof ta);
    memset(&tb, 0, sizeof tb);
    outage_run(ahead, both, 0, steps, &ta, &tb, &ea, &eb);
    sprintf(what, "outage %s, lookahead %d: and WITHOUT the request the same outage stalls "
            "(turns %u and %u), so the request is what this leg tests",
            both ? "both ways" : "one way", ahead, ea, eb);
    check(ea < 100 || eb < 100, what);
}

/* A STAR OF THREE, which is the shape the request is actually sent in.
 *
 * Two peers can only ever ask each other. Three cannot: a joiner reaches another joiner
 * only through the host, so a request from one joiner for another joiner's turn is
 * FORWARDED, and the answer comes back the same way. That forward is the one piece of the
 * repair that no test drove, in a topology no test built, so this builds it: seat 0 is the
 * host and seats 1 and 2 are joiners, every joiner packet goes to the host and is relayed
 * on, and everything to and from one seat is cut for a stretch of steps.
 *
 * IT IS STILL NOT THE MESSAGE ON A SOCKET. This routes what netmatch routes and calls what
 * netmatch calls, and it proves the scheduler can be repaired through a relay; it does not
 * parse a sixteen byte datagram or check who a host will send one to. */
static int star_down(int step, int x, int y, int cut_from, int cut_to, int cut_seat)
{
    if (step < cut_from || step >= cut_to) return 0;
    return x == cut_seat || y == cut_seat;
}

/* One seat's packet, routed the way the star routes it: straight out from the host, and
   via the host with a relay on the far side from a joiner. */
static void star_deliver(LsState* p, const unsigned char* pkt, int len, int from,
                         int step, int cf, int ct, int cs)
{
    int j;
    if (len <= 0) return;
    if (from == 0) {
        for (j = 1; j < 3; j++)
            if (!star_down(step, 0, j, cf, ct, cs)) ls_on_packet(&p[j], pkt, len);
        return;
    }
    if (star_down(step, from, 0, cf, ct, cs)) return;   /* it never reached the host */
    ls_on_packet(&p[0], pkt, len);
    for (j = 1; j < 3; j++) {
        if (j == from) continue;
        if (!star_down(step, 0, j, cf, ct, cs)) ls_on_packet(&p[j], pkt, len);
    }
}

/* `who` is stuck; it names the turn and the seats it is waiting for, and each of those
   answers from its own retained copy. `fwd` counts the answers that went joiner to joiner,
   which is the leg that exists only in a star. */
static void star_ask(LsState* p, int who, int step, int cf, int ct, int cs, int* fwd)
{
    const unsigned mask = ls_waiting_mask(&p[who]);
    const unsigned turn = ls_exec_turn(&p[who]);
    unsigned char rep[LS_PACKET_MAX];
    int j, n;

    if (!mask) return;
    for (j = 0; j < 3; j++) {
        if (j == who || !(mask & (1u << j))) continue;
        /* the question out */
        if (who > 0 && star_down(step, who, 0, cf, ct, cs)) continue;
        if (j > 0 && who > 0 && star_down(step, 0, j, cf, ct, cs)) continue;
        if (j > 0 && who == 0 && star_down(step, 0, j, cf, ct, cs)) continue;
        n = ls_pack_from(&p[j], turn, rep, (int)sizeof rep);
        if (n <= 0) continue;
        /* the answer back */
        if (j > 0 && star_down(step, j, 0, cf, ct, cs)) continue;
        if (who > 0 && star_down(step, 0, who, cf, ct, cs)) continue;
        ls_on_packet(&p[who], rep, n);
        if (j > 0 && who > 0) (*fwd)++;
    }
}

static void leg_star_of_three(int ahead, int cut_seat)
{
    static LsState p[3];
    static Transcript tr[3];
    unsigned char last[3][LS_PACKET_MAX];
    int lastlen[3], begun[3];
    const int cut_from = 20, cut_to = 60, steps = 400;
    int step, i, fwd = 0;
    char what[176];

    for (i = 0; i < 3; i++) {
        memset(&tr[i], 0, sizeof tr[i]);
        lastlen[i] = 0;
        begun[i] = 0;
        ls_init_ahead(&p[i], 3, i, ahead);
    }
    for (step = 0; step < steps; step++) {
        for (i = 0; i < 3; i++) {
            if (!begun[i]) {
                unsigned char o[22];
                memset(o, 0, sizeof o);
                o[0] = (unsigned char)(0x50 + i * 0x10 + (step % 7));
                if (step % 3 == i) ls_local_order(&p[i], o, (int)sizeof o);
                lastlen[i] = ls_pack(&p[i], last[i], (int)sizeof last[i]);
                begun[i] = 1;
                star_deliver(p, last[i], lastlen[i], i, step, cut_from, cut_to, cut_seat);
            }
            if (ls_turn_ready(&p[i])) {
                ls_run_turn(&p[i], sink, &tr[i]);
                begun[i] = 0;
                continue;
            }
            star_deliver(p, last[i], lastlen[i], i, step, cut_from, cut_to, cut_seat);
            star_ask(p, i, step, cut_from, cut_to, cut_seat, &fwd);
        }
    }

    sprintf(what, "star of three, lookahead %d, seat %d cut off: all three recovered "
            "(turns %u, %u, %u)", ahead, cut_seat, ls_exec_turn(&p[0]),
            ls_exec_turn(&p[1]), ls_exec_turn(&p[2]));
    check(ls_exec_turn(&p[0]) > 300 && ls_exec_turn(&p[1]) > 300
          && ls_exec_turn(&p[2]) > 300, what);

    {
        int shortest = tr[0].len;
        for (i = 1; i < 3; i++) if (tr[i].len < shortest) shortest = tr[i].len;
        sprintf(what, "star of three, lookahead %d, seat %d cut off: all three transcripts "
                "are identical", ahead, cut_seat);
        check(shortest > 0
              && strncmp(tr[0].text, tr[1].text, (size_t)shortest) == 0
              && strncmp(tr[0].text, tr[2].text, (size_t)shortest) == 0, what);
    }
    sprintf(what, "star of three, lookahead %d, seat %d cut off: the repair went joiner to "
            "joiner through the host %d times", ahead, cut_seat, fwd);
    check(fwd > 0, what);
}

/* A TURN FILLED TO WHAT THE WIRE HOLDS MUST STILL TRAVEL, and the turn above that must be
 * refused rather than accepted and then dropped.
 *
 * This is the regression leg for a hang that a player could cause with one click. A turn
 * is ONE block in ONE datagram, and the buffer a turn was allowed to fill used to be twice
 * what a datagram could carry: forty four orders were accepted, the turn was marked as
 * spoken for, and then the packer's first size test threw the block away and sent a header
 * with no turns in it. The far peer accepted that as valid and learned nothing, re-sending
 * it sent the same nothing, and asking for the turn by number hit the same test. The
 * engine emits one order per selected object, so a right click on forty four units did it.
 *
 * So: every count from one up to the limit must arrive whole, the limit must be what
 * ls_turn_room says it is, and one more than the limit must be refused out loud. */
static void leg_full_turn_travels(void)
{
    LsState a;
    unsigned char o[22], pkt[LS_PACKET_MAX];
    int room, i, len, worst_blocks = 99, all_arrived = 1;

    ls_init(&a, 2, 0);
    memset(o, 0x5A, sizeof o);
    room = ls_turn_room(&a, (int)sizeof o);
    check(room > 0 && (room + 1) * ((int)sizeof o + 1) > LS_PACKET_MAX - 15,
          "a full turn: ls_turn_room names the most orders one datagram can carry");

    for (i = 0; i < room; i++) {
        if (ls_local_order(&a, o, (int)sizeof o) != LS_OK) break;
    }
    check(i == room, "a full turn: the turn takes exactly that many");
    check(ls_local_order(&a, o, (int)sizeof o) == LS_ERR_FULL,
          "a full turn: and refuses the next one loudly rather than dropping it");

    for (i = 1; i <= room; i++) {
        LsState s, r;
        int k;
        ls_init(&s, 2, 0);
        ls_init(&r, 2, 1);
        for (k = 0; k < i; k++) ls_local_order(&s, o, (int)sizeof o);
        len = ls_pack(&s, pkt, (int)sizeof pkt);
        if (len <= 12 || pkt[7] < 1) { worst_blocks = i; all_arrived = 0; break; }
        if (ls_on_packet(&r, pkt, len) != LS_OK || r.orders_in != (unsigned long)i) {
            worst_blocks = i; all_arrived = 0; break;
        }
        /* and the request path can answer with the same turn */
        if (ls_pack_from(&s, ls_send_turn(&s) - 1u + (unsigned)ls_ahead(&s),
                         pkt, (int)sizeof pkt) <= 12) {
            worst_blocks = i; all_arrived = 0; break;
        }
    }
    if (all_arrived) {
        check(1, "a full turn: every count from 1 to the limit is packed, received whole "
                 "and can be asked for again");
    } else {
        char what[128];
        sprintf(what, "a full turn: a turn of %d orders did not survive the wire",
                worst_blocks);
        check(0, what);
    }

    /* And a buffer too small for the turn is an error, not an empty packet. */
    {
        LsState s;
        unsigned char small[64];
        ls_init(&s, 2, 0);
        for (i = 0; i < 10; i++) ls_local_order(&s, o, (int)sizeof o);
        check(ls_pack(&s, small, (int)sizeof small) < 0,
              "a full turn: a buffer too small to hold it is an error, not a short packet");
    }
}

/* A SEAT THAT WALKS OUT GOES QUIET ON THE SAME TURN EVERYWHERE.
 *
 * Absence used to take effect the moment each peer noticed, which is a different turn on
 * every machine because each one notices on its own clock. A peer holding the departing
 * seat's last turn executed those orders and a peer that did not executed none, and both
 * played on from worlds that had parted. The turn is named once and obeyed by everybody
 * here: the same set of orders runs on both, and neither runs the ones stamped at or after
 * that turn even when they are sitting in the ring. */
static void leg_departure_lands_on_one_turn(void)
{
    static LsState p[3];
    static Transcript tr[3];
    unsigned char pkt[LS_PACKET_MAX], o[22];
    int i, turn, len, j;
    unsigned quit_at;

    for (i = 0; i < 3; i++) {
        memset(&tr[i], 0, sizeof tr[i]);
        ls_init(&p[i], 3, i);
    }
    memset(o, 0, sizeof o);

    for (turn = 0; turn < 12; turn++) {
        for (i = 0; i < 3; i++) {
            o[0] = (unsigned char)(0xC0 + i * 0x10 + (turn % 12));
            ls_local_order(&p[i], o, (int)sizeof o);
            len = ls_pack(&p[i], pkt, (int)sizeof pkt);
            for (j = 0; j < 3; j++) if (j != i) ls_on_packet(&p[j], pkt, len);
        }
        for (i = 0; i < 3; i++) {
            while (ls_turn_ready(&p[i])) ls_run_turn(&p[i], sink, &tr[i]);
        }
    }

    /* SEAT 1'S LAST WORD REACHES ONE SURVIVOR AND NOT THE OTHER, which is the whole case:
       the two peers now hold different things for the same turn, and what stops them
       parting is that they agree on the turn from which that seat no longer counts. */
    o[0] = 0xFA;
    ls_local_order(&p[1], o, (int)sizeof o);
    len = ls_pack(&p[1], pkt, (int)sizeof pkt);
    ls_on_packet(&p[2], pkt, len);

    /* The host names the turn, from what it holds rather than from its own send counter:
       every turn below it is one the host can already run. */
    quit_at = ls_seat_first_missing(&p[0], 1);
    check(quit_at >= ls_exec_turn(&p[0]),
          "a departure: the named turn is never one the match has already run");
    check(p[2].ring[quit_at & (LS_HISTORY - 1)][1].reported
          && !p[0].ring[quit_at & (LS_HISTORY - 1)][1].reported,
          "a departure: one survivor holds the leaver's last turn and the other does not");
    ls_set_absent_at(&p[0], 1, quit_at);
    ls_set_absent_at(&p[2], 1, quit_at);

    for (turn = 0; turn < 40; turn++) {
        for (i = 0; i < 3; i += 2) {
            o[0] = (unsigned char)(0xE0 + i);
            ls_local_order(&p[i], o, (int)sizeof o);
            len = ls_pack(&p[i], pkt, (int)sizeof pkt);
            ls_on_packet(&p[i == 0 ? 2 : 0], pkt, len);
        }
        for (i = 0; i < 3; i += 2) {
            while (ls_turn_ready(&p[i])) ls_run_turn(&p[i], sink, &tr[i]);
        }
    }

    check(ls_exec_turn(&p[0]) > 30 && ls_exec_turn(&p[2]) > 30,
          "a departure: both survivors kept running");
    check(tr[0].len == tr[2].len && strcmp(tr[0].text, tr[2].text) == 0,
          "a departure: and neither ran the leaver's orders from the named turn on, so the "
          "two order streams stayed identical");
}

/* The barrier itself: with a peer that never speaks, the turn must never become ready,
   and the waiting mask must name exactly that peer. */
static void leg_barrier_holds(void)
{
    LsState a;
    unsigned char pa[LS_PACKET_MAX];
    int turn, ready_seen = 0;

    ls_init(&a, 2, 0);
    for (turn = 0; turn < 20; turn++) {
        ls_pack(&a, pa, (int)sizeof pa);
        /* seat 1 says nothing, ever */
        if (ls_turn_ready(&a)) ready_seen++;
    }
    /* The pre-agreed opening turns are ready by construction; past those the barrier must
       hold. Running every ready turn first drains the opening, then nothing more may
       become ready. */
    while (ls_turn_ready(&a)) ls_run_turn(&a, sink, NULL);
    check(!ls_turn_ready(&a), "barrier: a silent peer stops the match");
    check(ls_waiting_mask(&a) == 0x2u, "barrier: the waiting mask names exactly the silent seat");
}

/* Malformed input must be refused without touching the ring. */
static void leg_refuses_garbage(void)
{
    LsState a;
    unsigned char good[LS_PACKET_MAX], junk[64];
    LsState b;
    int len, i;

    ls_init(&a, 2, 0);
    ls_init(&b, 2, 1);
    ls_local_order(&b, "\x01\x02\x03", 3);
    len = ls_pack(&b, good, (int)sizeof good);

    check(ls_on_packet(&a, good, 8) == LS_ERR_BAD_PACKET, "garbage: a truncated packet is refused");

    memcpy(junk, good, 16);
    junk[0] ^= 0xFF;
    check(ls_on_packet(&a, junk, 16) == LS_ERR_BAD_PACKET, "garbage: a wrong magic is refused");

    memcpy(junk, good, 16);
    junk[4] = 0x7F;
    check(ls_on_packet(&a, junk, 16) == LS_ERR_BAD_PACKET, "garbage: a wrong version is refused");

    /* A packet claiming to be from this peer's own seat. Accepting it would execute every
       local order twice. */
    {
        unsigned char self[LS_PACKET_MAX];
        memcpy(self, good, (size_t)len);
        self[6] = 0;
        check(ls_on_packet(&a, self, len) == LS_ERR_BAD_PACKET, "garbage: a packet forging our own seat is refused");
    }

    /* A block whose declared length runs past the packet. */
    {
        unsigned char over[LS_PACKET_MAX];
        memcpy(over, good, (size_t)len);
        over[12] = 0xFF; over[13] = 0xFF;
        check(ls_on_packet(&a, over, len) == LS_ERR_BAD_PACKET, "garbage: an overlong block is refused");
    }

    /* Every refusal above must have left the scheduler able to accept the real thing. */
    check(ls_on_packet(&a, good, len) == LS_OK, "garbage: a good packet still works afterwards");
    for (i = 0; i < 3; i++) {
        /* And accepting it twice must be harmless. */
        check(ls_on_packet(&a, good, len) == LS_OK, "garbage: duplicate delivery is harmless");
        break;
    }
}

/* An order larger than the wire allows, and a turn filled past its buffer, must both be
   refused rather than truncated. */
static void leg_limits(void)
{
    LsState a;
    unsigned char big[LS_ORDER_MAX + 8];
    unsigned char ord[22];
    int i, hit_full = 0;

    ls_init(&a, 2, 0);
    memset(big, 0xAB, sizeof big);
    check(ls_local_order(&a, big, (int)sizeof big) == LS_ERR_TOO_BIG,
          "limits: an oversized order is refused");

    memset(ord, 0, sizeof ord);
    for (i = 0; i < 1000; i++) {
        if (ls_local_order(&a, ord, (int)sizeof ord) == LS_ERR_FULL) { hit_full = 1; break; }
    }
    check(hit_full, "limits: a full turn reports LS_ERR_FULL rather than dropping silently");
}

int main(void)
{
    printf("lockstep scheduler gate\n");
    leg_two_peers_clean();
    leg_packet_loss(3);
    leg_packet_loss(2);
    leg_four_peers();
    leg_long_outage(3, 0);
    leg_long_outage(12, 0);
    leg_long_outage(8, 1);
    leg_long_outage(12, 1);
    leg_star_of_three(3, 2);
    leg_star_of_three(12, 2);
    leg_star_of_three(12, 1);
    leg_full_turn_travels();
    leg_departure_lands_on_one_turn();
    leg_barrier_holds();
    leg_refuses_garbage();
    leg_limits();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : g_fail;
}
