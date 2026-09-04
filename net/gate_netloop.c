/*
 * gate_netloop.c -- two peers, one machine, REAL UDP sockets, real loopback.
 *
 * gate_lockstep.c proves the scheduler by handing packets across in memory. This proves
 * the same property with the operating system in the middle: two sockets, two schedulers,
 * datagrams that genuinely leave the process and come back. What it adds over the memory
 * test is everything the seam touches, which is the part a memory test cannot reach:
 * bind, non blocking reads that legitimately return nothing, address comparison mapping a
 * datagram back to a seat, and the byte order of the packet surviving a real send.
 *
 * It runs both peers in ONE process on two sockets rather than forking, because the point
 * is the socket layer rather than process isolation, and one process can assert on both
 * transcripts at the end.
 *
 *   cc -std=gnu89 -o gate_netloop gate_netloop.c lockstep.c net_udp.c && ./gate_netloop
 *
 * Exit status is the failure count.
 */
#include "lockstep.h"
#include "net_udp.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static int g_pass = 0, g_fail = 0;

static void check(int cond, const char* what)
{
    if (cond) { g_pass++; printf("OK   %s\n", what); }
    else      { g_fail++; printf("FAIL %s\n", what); }
}

typedef struct {
    char text[16384];
    int  len;
    int  orders;
} Transcript;

static void sink(void* user, int seat, const void* bytes, int len)
{
    Transcript* tr = (Transcript*)user;
    const unsigned char* b = (const unsigned char*)bytes;
    char line[64];
    int n = sprintf(line, "%d:%02X:%d ", seat, b[0], len);
    if (tr->len + n < (int)sizeof tr->text) {
        memcpy(tr->text + tr->len, line, (size_t)n);
        tr->len += n;
        tr->text[tr->len] = 0;
    }
    tr->orders++;
}

int main(void)
{
    NetSock *sa, *sb;
    NetAddr addr_a, addr_b, from;
    LsState a, b;
    Transcript ta, tb;
    unsigned char pkt[LS_PACKET_MAX], in[LS_PACKET_MAX];
    char txt[64];
    int turn, n, len;
    int attribution_ok = 1;
    time_t settle;

    printf("lockstep over real UDP loopback\n");

    check(net_startup() == 0, "platform networking starts");

    sa = net_open(0);
    sb = net_open(0);
    check(sa != 0 && sb != 0, "two sockets open on system chosen ports");
    if (!sa || !sb) { printf("\n%d passed, %d failed\n", g_pass, g_fail); return g_fail ? g_fail : 1; }

    check(net_local_port(sa) != 0 && net_local_port(sb) != 0,
          "both sockets report the port they actually got");
    check(net_local_port(sa) != net_local_port(sb), "the two sockets got different ports");

    check(net_resolve("127.0.0.1", net_local_port(sa), &addr_a) == 0, "loopback resolves for seat 0");
    check(net_resolve("127.0.0.1", net_local_port(sb), &addr_b) == 0, "loopback resolves for seat 1");

    net_addr_text(&addr_a, txt, (int)sizeof txt);
    printf("     seat 0 at %s\n", txt);
    net_addr_text(&addr_b, txt, (int)sizeof txt);
    printf("     seat 1 at %s\n", txt);

    check(net_addr_equal(&addr_a, &addr_a) == 1, "an address equals itself");
    check(net_addr_equal(&addr_a, &addr_b) == 0, "two different ports are different peers");

    /* Nothing has been sent, so a read must report nothing rather than blocking or
       failing. This is the property the frame loop depends on. */
    check(net_recv(sa, &from, in, (int)sizeof in) == 0, "an idle socket reads as empty, not as an error");

    memset(&ta, 0, sizeof ta);
    memset(&tb, 0, sizeof tb);
    ls_init(&a, 2, 0);
    ls_init(&b, 2, 1);

    for (turn = 0; turn < 50; turn++) {
        unsigned char oa[22], ob[22];

        memset(oa, 0, sizeof oa);
        memset(ob, 0, sizeof ob);
        oa[0] = (unsigned char)(0x10 + (turn % 13));
        ob[0] = (unsigned char)(0x90 + (turn % 7));

        ls_local_order(&a, oa, (int)sizeof oa);
        if (turn % 2 == 0) ls_local_order(&b, ob, (int)sizeof ob);

        len = ls_pack(&a, pkt, (int)sizeof pkt);
        if (len > 0) net_send(sa, &addr_b, pkt, len);
        len = ls_pack(&b, pkt, (int)sizeof pkt);
        if (len > 0) net_send(sb, &addr_a, pkt, len);

        /* Drain both sockets. Loopback is prompt but nothing here assumes it: a datagram
           that has not arrived this turn is covered by the redundancy in the next one,
           which is the same property the drop legs of the memory gate assert. */
        while ((n = net_recv(sa, &from, in, (int)sizeof in)) > 0) {
            /* Checked on EVERY datagram, reported once. A per datagram check() would
               print fifty identical lines and bury the legs that matter. */
            if (!net_addr_equal(&from, &addr_b)) attribution_ok = 0;
            ls_on_packet(&a, in, n);
        }
        while ((n = net_recv(sb, &from, in, (int)sizeof in)) > 0) {
            ls_on_packet(&b, in, n);
        }

        while (ls_turn_ready(&a)) ls_run_turn(&a, sink, &ta);
        while (ls_turn_ready(&b)) ls_run_turn(&b, sink, &tb);
    }

    /* SETTLE BEFORE ASSERTING, and this leg is the one this gate was missing.
       Every turn above sends and drains in the same breath, so a datagram still in flight
       when the drain runs is covered by the redundancy in the NEXT turn's packet. On the
       last turn there is no next packet. One datagram arriving a moment late therefore left
       the two peers one turn apart and failed three legs at once: the order counts, the
       transcripts and the executed turn. That is a defect in this test rather than in the
       scheduler, and it is invisible except under load, which is why it passed standalone
       and went red inside the suite.

       Measured: skipping seat 0's drain on the final turn alone reproduces it exactly, at
       13 passed and 3 failed, with seat 0 on 74 orders against seat 1's 75.

       Bounded in real seconds rather than in iterations, for the reason the connectivity
       tool already learned the hard way: a spin count is not a duration, and at -O2 it
       expires in a fraction of the time it looks like it should. Two seconds is thousands
       of times what loopback needs, and a peer that is genuinely stuck still reaches the
       assertions below and is reported rather than hanging. The loop is tight rather than
       sleeping so this file keeps its property of having no platform header in it at all;
       it costs a busy core only on the failure path, and only for those two seconds. */
    settle = time(0) + 2;
    for (;;) {
        int moved = 0;
        while ((n = net_recv(sa, &from, in, (int)sizeof in)) > 0) {
            if (!net_addr_equal(&from, &addr_b)) attribution_ok = 0;
            ls_on_packet(&a, in, n);
            moved = 1;
        }
        while ((n = net_recv(sb, &from, in, (int)sizeof in)) > 0) {
            ls_on_packet(&b, in, n);
            moved = 1;
        }
        while (ls_turn_ready(&a)) { ls_run_turn(&a, sink, &ta); moved = 1; }
        while (ls_turn_ready(&b)) { ls_run_turn(&b, sink, &tb); moved = 1; }
        if (moved) continue;                                  /* progress, go again */
        if (ls_exec_turn(&a) == ls_exec_turn(&b)) break;      /* settled and level */
        if (time(0) >= settle) break;                         /* say so below instead */
    }

    check(attribution_ok, "every datagram was attributed to the right peer by its address");
    check(ls_exec_turn(&a) == ls_exec_turn(&b),
          "the two peers settled on the same turn once the last datagrams had landed");

    printf("     seat 0 executed %d orders, seat 1 executed %d\n", ta.orders, tb.orders);
    printf("     packets in: seat 0 %lu (%lu bad), seat 1 %lu (%lu bad)\n",
           a.packets_in, a.packets_bad, b.packets_in, b.packets_bad);

    check(ta.orders > 0, "orders crossed a real socket and executed");
    check(a.packets_in > 0 && b.packets_in > 0, "both peers received real datagrams");
    check(a.packets_bad == 0 && b.packets_bad == 0, "no packet was malformed after a real send");
    check(ta.orders == tb.orders, "both peers executed the same number of orders");
    check(strcmp(ta.text, tb.text) == 0, "the two transcripts are identical over real UDP");
    check(ls_exec_turn(&a) == ls_exec_turn(&b), "both peers reached the same turn");

    net_close(sa);
    net_close(sb);
    net_shutdown();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : g_fail;
}
