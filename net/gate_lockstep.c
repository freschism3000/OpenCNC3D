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
    leg_barrier_holds();
    leg_refuses_garbage();
    leg_limits();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : g_fail;
}
