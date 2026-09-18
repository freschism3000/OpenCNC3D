/*
 * netcheck.c -- can these two peers lockstep at all.
 *
 * WHAT THIS IS FOR. Wiring lockstep into the game touches the engine, the host's frame
 * loop and the lobby, and when a match then fails to start there are four places to look
 * and no way to tell them apart. This tool removes one of them permanently: it runs the
 * real scheduler over the real socket layer between two real machines, with no engine and
 * no renderer, and says plainly whether the peers stayed in step. If this passes and a
 * match still fails, the network is not the reason.
 *
 * It is also the cheapest possible answer to the questions a first multiplayer session
 * actually runs into: is the port reachable, how far apart are the two ends in
 * milliseconds, and is anything dropping packets.
 *
 *   on one machine:     netcheck host [port]
 *   on the other:       netcheck join <address> [port]
 *   on either, alone:   netcheck selftest
 *
 * The default port is 17421. The host prints the port it bound and then waits; the joiner
 * is given the host's address. Both then run a fixed number of lockstep turns against
 * each other with synthetic orders and print a verdict.
 *
 * WHY THERE IS A SELFTEST. Without one, a joiner that will not connect leaves two
 * possibilities and no way to separate them: the network is in the way, or this binary is
 * broken on this platform. selftest answers the second on its own, in under a second, by
 * running both peers in one process over the loopback address, which exercises the whole
 * of the socket layer and the whole of the scheduler and none of the network. If it prints
 * the expected digest, the tool works here and the fault is between the two ends.
 *
 * The digest it prints is THE SAME NUMBER a successful pair prints, because the synthetic
 * orders depend only on the seat and the turn. So the expected answer is known before any
 * connection is attempted, and a pair whose digests match each other but not this one is
 * a different fault from a pair whose digests differ.
 *
 * Exit status is zero when the peers finished in step, and non zero otherwise, so this
 * can be scripted as well as read.
 */
#include "lockstep.h"
#include "net_udp.h"
#include "roomcode.h"
/* For the probe's wire constants only. This tool does not link netmatch, and nothing it
   uses from that header is more than a #define. */
#include "netmatch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <unistd.h>
  #include <sys/time.h>
#endif

#define NC_PORT_DEFAULT 17421
#define NC_TURNS        300      /* 300 turns is 20 seconds of match at the engine's 15 Hz */

/* The handshake is deliberately tiny and separate from the lockstep packet, so that a
   stray lockstep packet cannot be read as a handshake or the reverse. A joiner repeats
   HELLO until it is answered, because the first datagram of a session is the one most
   likely to be lost while the other end is still starting up. */
#define NC_HELLO  0x4F4C4548u   /* 'HELO' */
#define NC_WELCOME 0x434C4557u  /* 'WELC' */

/* THE LATENCY PROBE, and it is deliberately NOT part of the lockstep packet. Putting a
   timestamp on the wire the game will use would mean the diagnostic and the match share a
   format, so a change to one is a change to the other, and the wire would carry a field
   the engine has no use for. This rides beside it on the same socket instead, exactly as
   the handshake does, and the run loop skips it by magic for the same reason.

   Eight bytes: the magic, then the sender's own millisecond stamp, echoed back
   unmodified. Only the sender ever reads the stamp, so the two ends do not need clocks
   that agree, only clocks that run. */
#define NC_PING   0x474E4950u   /* 'PING' */
#define NC_PONG   0x474E4F50u   /* 'PONG' */

/* Turns between probes. Thirty at 15 Hz is one every two seconds of match, which over a
   three hundred turn run is ten samples: enough to separate a link from a hiccup, few
   enough that the probe cannot be mistaken for load. */
#define NC_PING_EVERY 30

static void put_u32(unsigned char* p, unsigned v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static unsigned get_u32(const unsigned char* p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

/* Sleep a few milliseconds so a wait loop does not spin a core flat.

   IT IS A CLOCK THAT BOUNDS THE WAITS, NOT A SPIN COUNT. The first version of this file
   counted iterations of a volatile loop, which is not a duration: at -O2 the whole
   handshake window came and went in well under two seconds, so a joiner started a moment
   late met a host that had already given up, and the tool then blamed the firewall. A
   diagnostic that misreports the fault is worse than no diagnostic, so every wait below
   is bounded in real seconds read from time(). */
static void nap_ms(int ms)
{
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)ms * 1000);
#endif
}

/* Milliseconds from an arbitrary origin. Used only for differences, and only for
   differences taken on the machine that took the first reading, so an origin that means
   nothing and a clock that wraps are both fine over a run this short. time() cannot do
   this job: its granularity is one second, and one second is five times the whole budget
   a turn has. */
static unsigned now_ms(void)
{
#if defined(_WIN32)
    /* THE PERFORMANCE COUNTER, NOT GetTickCount. The tick count moves in steps of 10 to
       16 ms, which is most of a round trip to a nearby relay, so every reading this tool
       prints would be rounded to whichever step it straddled. */
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (freq.QuadPart == 0 && !QueryPerformanceFrequency(&freq)) freq.QuadPart = 0;
    if (freq.QuadPart == 0 || !QueryPerformanceCounter(&now)) return (unsigned)GetTickCount();
    return (unsigned)((now.QuadPart / freq.QuadPart) * 1000
                      + ((now.QuadPart % freq.QuadPart) * 1000) / freq.QuadPart);
#else
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (unsigned)(tv.tv_sec * 1000u + (unsigned)(tv.tv_usec / 1000));
#endif
}

/* Round trip samples, kept as the three numbers that answer different questions: the best
   case the link can do, the typical one, and the worst, because a link whose worst is ten
   times its best is a different problem from one that is simply far away. */
typedef struct {
    int      samples;
    unsigned min;
    unsigned max;
    unsigned long total;
} Rtt;

static void rtt_add(Rtt* r, unsigned ms)
{
    if (r->samples == 0 || ms < r->min) r->min = ms;
    if (r->samples == 0 || ms > r->max) r->max = ms;
    r->total += ms;
    r->samples++;
}

/* WHAT A ROUND TRIP MEANS FOR THIS SCHEDULER, said once and printed wherever a number is.
   An order given on turn N is stamped for turn N + LS_MAX_AHEAD, so it has three turns to
   cross the link, which at the engine's 15 Hz is 200 ms of ONE WAY cover. A round trip is
   two crossings, so the link keeps up while the round trip stays under about 400 ms, and
   the order simply arrives in time. Past that the receiving peer reaches the turn before
   the order does and every peer waits, which is felt as the whole match stuttering rather
   than as one player lagging: that is what lockstep costs and it is why this number is
   worth knowing before a match rather than during one. */
static void rtt_report(const Rtt* r, const char* who)
{
    if (r->samples == 0) {
        printf("          %s round trip: no samples, the probe never came back.\n", who);
        printf("          The likeliest reason by far is that the two ends are not the\n");
        printf("          same build: a peer without the probe does not answer it, and\n");
        printf("          counts it as a malformed packet on its own side. Check the\n");
        printf("          other end's malformed count before reading anything into this.\n");
        return;
    }
    printf("          %s round trip: min %u ms, avg %lu ms, max %u ms over %d samples\n",
           who, r->min, r->total / (unsigned long)r->samples, r->max, r->samples);
    if (r->max < 400) {
        printf("          Inside the 400 ms an order has to cross the link and be waited\n");
        printf("          for, so the link is not what would hold a match up.\n");
    } else {
        printf("          The worst sample is past the 400 ms an order has to cross the\n");
        printf("          link, so a match on this link would stall for every player at\n");
        printf("          once, not just the far one. Raising the lobby's turns-ahead is\n");
        printf("          the lever, and it is paid for in input lag.\n");
    }
}

/* Seconds a peer waits for the other end to appear. Generous on purpose: the two ends are
   started by hand, one after the other, by a person crossing a room. */
#define NC_HANDSHAKE_SECS 60
/* Seconds a peer waits for one turn before declaring the match stalled. */
#define NC_TURN_SECS 10

/* THE DIGEST NC_TURNS TURNS OF THE SYNTHETIC ORDERS PRODUCE. Measured, not derived: the
   orders are a pure function of seat and turn, so this number is fixed by make_order and
   NC_TURNS together and by nothing else. It is the same on a 64 bit build and a 32 bit
   one, which was checked rather than assumed, because that is the pairing this project
   ships and the digest width has been wrong here before.

   It is a CROSS CHECK AND NOT THE PASS CONDITION. Two peers agreeing with each other is
   what lockstep has to guarantee, and that is what decides the exit status. Disagreeing
   with this constant while agreeing with each other means only that the order shape or
   the turn count moved and this line did not follow; the note below says so rather than
   failing, because a diagnostic that cries wolf after an ordinary edit is a diagnostic
   people stop running. */
#define NC_EXPECT_DIGEST 0xE2668540u

typedef struct {
    unsigned long orders;
    unsigned      digest;   /* order of execution folded into one number */
} Tally;

/* The digest is what makes the verdict meaningful. Counting orders proves only that both
   peers were busy; folding seat, first byte and length in sequence proves they executed
   the SAME orders in the SAME order, which is the property lockstep exists to provide.

   IT IS 32 BITS ON PURPOSE, AND MASKED, and this is not fussiness. It was written as an
   unsigned long, which is 64 bits on a 64 bit build and 32 on a 32 bit one, so the two
   ends printed digests of different widths from an identical order stream: measured, a
   32 bit peer said E2668540 while a 64 bit peer said F749997BE2668540, the same low half
   with the top half missing. A person comparing those two lines would conclude lockstep
   had failed. The project ships a 64 bit build on one platform and a 32 bit build on the
   other, so that is not a corner case, it is the exact pairing this tool exists to
   check, and a diagnostic that misreports the thing it was written to measure is worse
   than not having one. Anything compared BETWEEN peers is fixed width from here on. */
static void sink(void* user, int seat, const void* bytes, int len)
{
    Tally* t = (Tally*)user;
    const unsigned char* b = (const unsigned char*)bytes;
    t->orders++;
    t->digest = (unsigned)((t->digest * 1315423911u
                            + (unsigned)((seat << 16) | (b[0] << 8) | len)) & 0xFFFFFFFFu);
}

/* CnCNet's V3 tunnels all listen here; the master list carries the port per server but
   every official one advertises this. */
#define NC_RELAY_PORT 50001
/* One of CnCNet's public V3 tunnels, verified answering. Overridable on the command line
   because the point of this tool is to try things by hand. */
#define NC_RELAY_DEFAULT "49.13.152.109"

/* PING ONE RELAY AND SAY WHAT CAME BACK. Twelve bytes of nonce go out inside a 50-byte
   datagram and the server returns them verbatim, so a wrong echo is as informative as no
   echo: it means something answered that is not speaking this protocol. */
static int relay_ping(const char* host, unsigned short port)
{
    NetSock* s;
    unsigned char nonce[4], echo[12], want[12];
    int i, got = 0;
    unsigned waited = 0;

    /* The echo is the request's first twelve bytes, and its first EIGHT are the two id
       fields, which a ping must leave at zero. So four bytes are ours to choose and the
       expected answer is eight zeros followed by them. */
    for (i = 0; i < 4; i++) nonce[i] = (unsigned char)(0xA5 ^ (i * 31 + 7));
    memset(want, 0, sizeof want);
    memcpy(want + 8, nonce, 4);
    s = net_open(0);
    if (!s) {
        printf("RELAY|%s:%u|no socket\n", host, (unsigned)port);
        return 3;
    }
    if (net_tunnel_ping_send(s, host, port, nonce) < 0) {
        printf("RELAY|%s:%u|could not send (name did not resolve, or the send was "
               "refused)\n", host, (unsigned)port);
        net_close(s);
        return 4;
    }
    /* Two seconds, in tenths. A tunnel that has not answered by then is not going to. */
    for (waited = 0; waited < 2000 && !got; waited += 100) {
        got = net_tunnel_ping_recv(s, echo);
        if (!got) nap_ms(100);
    }
    if (!got) {
        printf("RELAY|%s:%u|NO ANSWER in 2 s. UDP %u may be blocked leaving this "
               "network, or that server is down.\n", host, (unsigned)port, (unsigned)port);
        net_close(s);
        return 5;
    }
    if (memcmp(echo, want, 12) != 0) {
        printf("RELAY|%s:%u|ANSWERED, BUT NOT WITH OUR BYTES. Something is listening "
               "there that does not speak Tunnel V3.\n", host, (unsigned)port);
        net_close(s);
        return 6;
    }
    printf("RELAY|%s:%u|ok|echo=12 bytes, verbatim|round trip under %u ms\n",
           host, (unsigned)port, waited ? waited : 100u);
    net_close(s);
    return 0;
}

static void usage(const char* me)
{
    printf("usage: %s host [port]\n", me);
    printf("       %s join <address> [port]\n", me);
    printf("       %s selftest\n", me);
    printf("       %s relay <host> [port]\n", me);
    printf("       %s relayhost [relay] \n", me);
    printf("       %s relayjoin <#CODE> [relay]\n", me);
    printf("\n");
    printf("  Runs the real lockstep scheduler over the real socket layer between two\n");
    printf("  ends, with no engine. Start the host first, then the joiner.\n");
    printf("  Default port %d, UDP. The host must be reachable on it.\n", NC_PORT_DEFAULT);
    printf("\n");
    printf("  relay pings a CnCNet Tunnel V3 server: a 50-byte datagram it echoes the\n");
    printf("  first 12 bytes of. It proves this build's understanding of the protocol\n");
    printf("  against a REAL server, which a loopback test structurally cannot -- and it\n");
    printf("  needs no registration, no session and no state on either side.\n");
    printf("  Default relay port %d. Their limit is 20 pings per address per minute.\n",
           NC_RELAY_PORT);
    printf("\n");
    printf("  relayhost and relayjoin run the SAME session through a CnCNet relay\n");
    printf("  instead of directly. The host prints a room code; the joiner types it.\n");
    printf("  Neither machine forwards a port, because both dial outward. This is the\n");
    printf("  only way to prove a relay actually carries our traffic: every gate in the\n");
    printf("  tree talks to a stand-in written from our own reading of the protocol.\n");
    printf("\n");
    printf("  selftest needs nothing else running: it puts both peers in one process on\n");
    printf("  the loopback address, so it says whether this binary works on this machine\n");
    printf("  before anything is blamed on the network. Run it first.\n");
    printf("\n");
    printf("       %s probe [relay] <#CODE> [rounds]\n", me);
    printf("       %s probe <address:port> [rounds]\n", me);
    printf("\n");
    printf("  probe asks a game's LOBBY how far away it is, the way the game list does:\n");
    printf("  %d probes per round, %d, %d and %d ms apart, each round trip printed, and\n"
           "  the minimum.\n",
           NM_PROBE_COUNT, NM_PROBE_GAP1_MS, NM_PROBE_GAP2_MS, NM_PROBE_GAP3_MS);
    printf("  It takes no seat and needs no passcode. A relayed room is asked through the\n");
    printf("  relay (default %s), which costs one relay client slot for the whole run,\n",
           NC_RELAY_DEFAULT);
    printf("  so ask for several rounds rather than running it several times. A direct\n");
    printf("  probe sends this machine's address to that host.\n");
}

/* ---- THE PROBE, BY HAND ------------------------------------------------------------
   WHAT IT IS FOR. The game list's PING column is the minimum of NM_PROBE_COUNT probes,
   and a number on a screen is only worth showing if something that prints its working
   can reproduce it. This prints every round trip, each round's minimum, and for a
   relayed room the relay's own echo from this machine, which is the other half of the
   path and the first thing to compare a surprising number against.

   IT WRITES THE BYTES ITSELF, from the constants in netmatch.h, rather than calling the
   game's prober. This tool links only the transport; and a second writer of the wire is
   also a second reading of it, which is what notices when the two drift. The schedule
   is the game's: NM_PROBE_COUNT probes at the gaps NM_PROBE_GAP_MS gives, a round finished
   NM_PROBE_WAIT_MS after its last probe.

   ONE RELAY REGISTRATION FOR THE WHOLE RUN, however many rounds, because every id a
   process registers is one of the eight clients a relay allows one public address, and
   it stays counted for up to two minutes after the process exits. */
#define NC_PROBE_ROUNDS     3
#define NC_PROBE_ROUNDS_MAX 10

/* HOST or HOST:PORT. 1 with a port, 0 with none, -1 when it is neither. */
static int split_hostport(const char* text, char* host, int hostmax, unsigned short* port)
{
    const char* c = strrchr(text, ':');
    size_t n;
    if (!c) {
        snprintf(host, (size_t)hostmax, "%s", text);
        return host[0] ? 0 : -1;
    }
    n = (size_t)(c - text);
    if (n == 0 || n >= (size_t)hostmax || !c[1]) return -1;
    memcpy(host, text, n);
    host[n] = '\0';
    *port = (unsigned short)atoi(c + 1);
    return *port ? 1 : -1;
}

/* The relay's own echo, timed: this machine to the relay and back. On a plain socket of
   its own, because a tunnel socket swallows the echo. THE LEAST OF THREE, because one
   echo on a fresh socket was measured reading 123 ms from a relay that a full relayed
   round trip crossed twice in 42: the first datagram of a new flow can wait on the
   router in front of this machine, and a single sample cannot tell that from distance.
   Three of the twenty pings a minute a relay allows an address. -1 when none came back
   within two seconds of being sent. */
static int relay_echo_ms(const char* host, unsigned short port)
{
    NetSock* ps = net_open(0);
    unsigned char nonce[4], echo[12];
    int k, i, best = -1;
    if (!ps) return -1;
    for (k = 0; k < 3; k++) {
        const unsigned seed = now_ms() ^ (unsigned)(k * 0x9E3779B9u);
        unsigned t0;
        for (i = 0; i < 4; i++) nonce[i] = (unsigned char)((seed >> (i * 8)) & 0xFFu);
        t0 = now_ms();
        if (net_tunnel_ping_send(ps, host, port, nonce) < 0) break;
        while (now_ms() - t0 < 2000u) {
            if (net_tunnel_ping_recv(ps, echo) && memcmp(echo + 8, nonce, 4) == 0) {
                const int ms = (int)(now_ms() - t0);
                if (best < 0 || ms < best) best = ms;
                break;
            }
            nap_ms(1);
        }
        nap_ms(130);
    }
    net_close(ps);
    return best;
}

static int probe_cmd(int argc, char** argv)
{
    char relay[128], host[128], target[64], mins[160];
    unsigned short rport = NC_RELAY_PORT, dport = 0;
    unsigned long room = 0ul;
    const char* code = NULL;
    NetSock* s;
    NetAddr peer, from;
    unsigned char in[64], pkt[NM_PROBE_BYTES];
    int relayed = 0, rounds = NC_PROBE_ROUNDS, argi, round;
    int best_all = -1, answered_all = 0, sent_all = 0, echo = -1;
    unsigned version = 0;

    snprintf(relay, sizeof relay, "%s", NC_RELAY_DEFAULT);
    if (argc < 3) { usage(argv[0]); return 2; }
    if (argc >= 4 && argv[3][0] == '#') {
        relayed = 1;
        code = argv[3];
        argi = 4;
        if (split_hostport(argv[2], relay, (int)sizeof relay, &rport) < 0) {
            printf("netcheck: '%s' is not a relay address.\n", argv[2]);
            return 2;
        }
    } else if (argv[2][0] == '#') {
        relayed = 1;
        code = argv[2];
        argi = 3;
    } else {
        argi = 3;
        if (split_hostport(argv[2], host, (int)sizeof host, &dport) != 1) {
            printf("netcheck: '%s' is neither a room code (#K7M-3QX) nor address:port.\n",
                   argv[2]);
            return 2;
        }
    }
    if (argc > argi) {
        rounds = atoi(argv[argi]);
        if (rounds < 1) rounds = 1;
        if (rounds > NC_PROBE_ROUNDS_MAX) rounds = NC_PROBE_ROUNDS_MAX;
    }
    if (relayed && !rc_decode(code, &room)) {
        printf("netcheck: '%s' is not a room code. They look like #K7M-3QX.\n", code);
        return 2;
    }
    if (net_startup() != 0) {
        printf("netcheck: this platform's networking would not start.\n");
        return 3;
    }
    memset(&peer, 0, sizeof peer);
    if (relayed) {
        const unsigned long my_id = rc_draw_peer_id();
        if (!rc_encode(room, target)) snprintf(target, sizeof target, "%lu", room);
        echo = relay_echo_ms(relay, rport);
        s = my_id ? net_open_tunnel(relay, rport, my_id) : NULL;
        if (!s) {
            printf("PROBE|target=%s|could not open a socket through the relay at %s:%u\n",
                   target, relay, (unsigned)rport);
            net_shutdown();
            return 3;
        }
        net_addr_tunnel(&peer, room);
        if (echo >= 0)
            printf("PROBE|target=%s|via=%s:%u|relay-echo=%dms\n", target, relay,
                   (unsigned)rport, echo);
        else
            printf("PROBE|target=%s|via=%s:%u|relay-echo=none in 2 s\n", target, relay,
                   (unsigned)rport);
    } else {
        if (net_resolve(host, dport, &peer) != 0) {
            printf("netcheck: could not resolve '%s'.\n", host);
            net_shutdown();
            return 2;
        }
        s = net_open(0);
        if (!s) {
            printf("netcheck: could not open a UDP socket.\n");
            net_shutdown();
            return 3;
        }
        net_addr_text(&peer, target, (int)sizeof target);
        printf("PROBE|target=%s|direct\n", target);
    }
    fflush(stdout);

    mins[0] = '\0';
    for (round = 1; round <= rounds; round++) {
        const unsigned nonce = (unsigned)rc_draw_peer_id() ^ now_ms();
        unsigned sent_at[NM_PROBE_COUNT];
        unsigned next = now_ms(), last = 0, mask = 0;
        int sent = 0, got = 0, best = -1, n;
        size_t used;
        for (;;) {
            if (sent < NM_PROBE_COUNT && now_ms() - next < 0x80000000u) {
                const unsigned stamp = now_ms();
                put_u32(pkt, NM_PROBE_WORD);
                put_u32(pkt + 4, nonce + (unsigned)sent);
                put_u32(pkt + 8, stamp);
                put_u32(pkt + 12, 0u);
                net_send(s, &peer, pkt, NM_PROBE_BYTES);
                sent_at[sent++] = stamp;
                last = stamp;
                next = stamp + (unsigned)NM_PROBE_GAP_MS(sent - 1);
            }
            while ((n = net_recv(s, &from, in, (int)sizeof in)) > 0) {
                unsigned idx;
                int rtt;
                if (n < NM_PROBE_BYTES || get_u32(in) != NM_PROBEACK_WORD) continue;
                if (!net_addr_equal(&from, &peer)) continue;
                idx = get_u32(in + 4) - nonce;
                if (idx >= (unsigned)sent || (mask & (1u << idx))) continue;
                if (get_u32(in + 8) != sent_at[idx]) continue;
                rtt = (int)(now_ms() - sent_at[idx]);
                mask |= 1u << idx;
                got++;
                if (best < 0 || rtt < best) best = rtt;
                version = get_u32(in + 12);
                printf("PROBE|round=%d|probe=%u|rtt=%dms\n", round, idx + 1u, rtt);
            }
            if (got == NM_PROBE_COUNT) break;
            if (sent == NM_PROBE_COUNT && now_ms() - last >= (unsigned)NM_PROBE_WAIT_MS) break;
            nap_ms(1);
        }
        sent_all += sent;
        answered_all += got;
        used = strlen(mins);
        if (best >= 0) {
            if (best_all < 0 || best < best_all) best_all = best;
            printf("PROBE|round=%d|min=%dms|answers=%d/%d\n", round, best, got, sent);
            snprintf(mins + used, sizeof mins - used, "%s%d", used ? "," : "", best);
        } else {
            printf("PROBE|round=%d|min=--|answers=0/%d\n", round, sent);
            snprintf(mins + used, sizeof mins - used, "%s--", used ? "," : "");
        }
        fflush(stdout);
        /* A SECOND BETWEEN ROUNDS, so no round can run into the host's limit on answers to
           one sender. Drained meanwhile: that keeps the relay registration fresh, and an
           answer arriving late belongs to a round that is over. */
        if (round < rounds) {
            const unsigned t = now_ms();
            while (now_ms() - t < 1000u) {
                while (net_recv(s, &from, in, (int)sizeof in) > 0) { /* late: dropped */ }
                nap_ms(5);
            }
        }
    }

    if (best_all >= 0) {
        printf("PROBE|done|target=%s|min=%dms|rounds=%d|answered=%d/%d|round-mins=%s"
               "|host-version=%u\n", target, best_all, rounds, answered_all, sent_all,
               mins, version);
    } else {
        printf("PROBE|done|target=%s|NO ANSWER|rounds=%d|answered=0/%d\n", target, rounds,
               sent_all);
        printf("          Silence has several causes that look the same from here: no such\n");
        printf("          room, a match that has already started, a host built before the\n");
        printf("          probe existed, a firewall in front of a direct host, or loss.\n");
    }
    fflush(stdout);
    net_close(s);
    net_shutdown();
    return best_all >= 0 ? 0 : 5;
}

/* THE SYNTHETIC ORDER, in one place because two places would break the claim that the
   selftest prints the same digest as a real pair. Twenty two bytes, the size of the
   engine's own order, with a first byte that varies with both seat and turn so that the
   digest is sensitive to sequence rather than merely to volume. It is a pure function of
   (seat, turn), which is exactly why the expected digest can be known in advance. */
static void make_order(unsigned char* out, int seat, int turn)
{
    memset(out, 0, 22);
    out[0] = (unsigned char)(0x10 + (seat * 0x40) + (turn % 23));
}

/* BOTH PEERS IN ONE PROCESS, over real loopback datagrams. This is not a simulation of
   the socket layer: it is the socket layer, two sockets, real sends and real receives.
   What it removes is the network between the two ends, and that is the point, because
   removing it is what separates "this binary does not work here" from "these two ends
   cannot reach each other".

   The turn structure below matches the match loop exactly, one iteration per turn per
   seat, because the digest is only a useful expectation if the two paths execute the same
   number of turns. */
static int selftest(void)
{
    NetSock *sk[2];
    NetAddr addr[2], from;
    LsState ls[2];
    Tally tally[2];
    Rtt rtt[2];
    unsigned char pkt[LS_PACKET_MAX], in[LS_PACKET_MAX];
    int turn, s, other, n, len;
    time_t tdeadline;

    printf("netcheck: selftest, both peers in one process over 127.0.0.1.\n");

    sk[0] = net_open(0);
    sk[1] = net_open(0);
    if (!sk[0] || !sk[1]) {
        printf("netcheck: could not open two UDP sockets on this machine.\n");
        printf("          Nothing else has to be running for that to work, so this is\n");
        printf("          the platform refusing rather than the network.\n");
        return 3;
    }
    for (s = 0; s < 2; s++) {
        if (net_resolve("127.0.0.1", net_local_port(sk[s]), &addr[s]) != 0) {
            printf("netcheck: 127.0.0.1 would not resolve, which means the loopback\n");
            printf("          interface is not usable on this machine.\n");
            return 3;
        }
        if (ls_init(&ls[s], 2, s) != LS_OK) {
            printf("netcheck: the scheduler refused a two seat match, which should be impossible.\n");
            return 3;
        }
        memset(&tally[s], 0, sizeof tally[s]);
        memset(&rtt[s], 0, sizeof rtt[s]);
    }
    printf("          seat 0 on port %u, seat 1 on port %u, %d turns.\n",
           (unsigned)net_local_port(sk[0]), (unsigned)net_local_port(sk[1]), NC_TURNS);

    for (turn = 0; turn < NC_TURNS; turn++) {
        for (s = 0; s < 2; s++) {
            unsigned char order[22];
            other = 1 - s;
            make_order(order, s, turn);
            ls_local_order(&ls[s], order, (int)sizeof order);
            len = ls_pack(&ls[s], pkt, (int)sizeof pkt);
            if (len > 0) net_send(sk[s], &addr[other], pkt, len);
            /* The probe runs here too. Loopback will report zero or one millisecond,
               which is not an interesting number in itself; what it proves is that the
               probe path works on this platform, so a zero from a real pair is a real
               reading rather than a probe that never came back. */
            if ((turn % NC_PING_EVERY) == 0) {
                unsigned char ping[8];
                put_u32(ping, NC_PING);
                put_u32(ping + 4, now_ms());
                net_send(sk[s], &addr[other], ping, 8);
            }
        }

        /* Bounded exactly as the match loop is. Loopback is prompt and nothing here
           assumes it: a datagram that has not arrived is covered by the redundancy in the
           next turn's packet, and a peer that has genuinely stopped becomes a report. */
        tdeadline = time(0) + NC_TURN_SECS;
        while (time(0) < tdeadline) {
            for (s = 0; s < 2; s++) {
                other = 1 - s;
                while ((n = net_recv(sk[s], &from, in, (int)sizeof in)) > 0) {
                    if (!net_addr_equal(&from, &addr[other])) continue;
                    if (n == 8 && get_u32(in) == NC_PING) {
                        unsigned char pong[8];
                        put_u32(pong, NC_PONG);
                        put_u32(pong + 4, get_u32(in + 4));
                        net_send(sk[s], &addr[other], pong, 8);
                        continue;
                    }
                    if (n == 8 && get_u32(in) == NC_PONG) {
                        rtt_add(&rtt[s], now_ms() - get_u32(in + 4));
                        continue;
                    }
                    ls_on_packet(&ls[s], in, n);
                }
            }
            if (ls_turn_ready(&ls[0]) && ls_turn_ready(&ls[1])) break;
            nap_ms(1);
        }
        if (!ls_turn_ready(&ls[0]) || !ls_turn_ready(&ls[1])) {
            printf("netcheck: STALLED on turn %u with nothing but loopback in the way.\n",
                   ls_exec_turn(&ls[0]));
            printf("          That is this binary or this platform, not the network.\n");
            return 1;
        }
        for (s = 0; s < 2; s++) {
            while (ls_turn_ready(&ls[s])) ls_run_turn(&ls[s], sink, &tally[s]);
        }
    }

    printf("\n");
    printf("netcheck: seat 0 finished turn %u, orders %lu, DIGEST %08X\n",
           ls_exec_turn(&ls[0]), tally[0].orders, tally[0].digest);
    printf("          seat 1 finished turn %u, orders %lu, DIGEST %08X\n",
           ls_exec_turn(&ls[1]), tally[1].orders, tally[1].digest);
    printf("          malformed packets: seat 0 %lu, seat 1 %lu\n",
           ls[0].packets_bad, ls[1].packets_bad);
    rtt_report(&rtt[0], "loopback");

    net_close(sk[0]);
    net_close(sk[1]);

    if (tally[0].digest != tally[1].digest || tally[0].orders != tally[1].orders) {
        printf("\n");
        printf("  FAILED. The two peers did not execute the same orders, with no network\n");
        printf("  between them at all. Do not go looking at firewalls: this build of the\n");
        printf("  scheduler is wrong on this machine.\n");
        return 1;
    }
    if (ls[0].packets_bad || ls[1].packets_bad) {
        printf("\n");
        printf("  FAILED. Packets were malformed over loopback, which cannot be loss and\n");
        printf("  cannot be the network. The wire format and this build disagree.\n");
        return 1;
    }

    printf("\n");
    printf("  PASSED. This binary can lockstep on this machine, and\n");
    printf("          ORDER DIGEST %08X\n", tally[0].digest);
    printf("  is what a host and a joiner should BOTH print when they finish %d turns.\n",
           NC_TURNS);
    printf("  Write it down before running the pair: two ends that agree with each other\n");
    printf("  but not with this number are a different fault from two that disagree.\n");
    if (tally[0].digest != NC_EXPECT_DIGEST) {
        printf("\n");
        printf("  NOTE: the digest built into this binary is %08X, so the order shape or\n",
               NC_EXPECT_DIGEST);
        printf("  the turn count has changed and that constant was not updated with it.\n");
        printf("  The two peers still agree, which is the property that matters, so this\n");
        printf("  is a stale expectation rather than a fault. Both ends must be the same\n");
        printf("  build for the comparison in the next paragraph to mean anything.\n");
    }
    return 0;
}

int main(int argc, char** argv)
{
    NetSock* sock;
    NetAddr peer, from;
    LsState ls;
    Tally tally;
    Rtt rtt;
    unsigned char pkt[LS_PACKET_MAX], in[LS_PACKET_MAX];
    char txt[64];
    unsigned short port = NC_PORT_DEFAULT;
    int is_host, seat, turn, n, len;
    time_t deadline, tdeadline;
    int have_peer = 0;
    unsigned long sent = 0, got = 0;
    /* Relay mode: the same session, carried by a CnCNet tunnel instead of going direct.
       Neither machine forwards a port, because both dial outward. */
    int is_relay = 0;
    unsigned long my_id = 0ul, peer_id = 0ul;
    char relay_host[128];
    snprintf(relay_host, sizeof relay_host, "%s", NC_RELAY_DEFAULT);

    if (argc < 2) { usage(argv[0]); return 2; }

    /* Handled before anything else binds a socket, because it is the thing to run first
       and it needs no argument, no port and no second end. */
    if (!strcmp(argv[1], "selftest")) {
        int rc;
        if (net_startup() != 0) {
            printf("netcheck: this platform's networking would not start.\n");
            return 3;
        }
        rc = selftest();
        net_shutdown();
        return rc;
    }

    /* Handled on its own too: it opens its own socket, speaks no lockstep and needs no
       second copy of this tool at the far end, only a game lobby. */
    if (!strcmp(argv[1], "probe")) return probe_cmd(argc, argv);

    if (!strcmp(argv[1], "host")) {
        is_host = 1;
        if (argc >= 3) port = (unsigned short)atoi(argv[2]);
    } else if (!strcmp(argv[1], "join")) {
        is_host = 0;
        if (argc < 3) { usage(argv[0]); return 2; }
        if (argc >= 4) port = (unsigned short)atoi(argv[3]);
    } else if (!strcmp(argv[1], "relayhost")) {
        is_host = 1;
        is_relay = 1;
        if (argc >= 3) snprintf(relay_host, sizeof relay_host, "%s", argv[2]);
    } else if (!strcmp(argv[1], "relayjoin")) {
        is_host = 0;
        is_relay = 1;
        if (argc < 3) { usage(argv[0]); return 2; }
        if (!rc_decode(argv[2], &peer_id)) {
            printf("netcheck: '%s' is not a room code. They look like #K7M-3QX.\n", argv[2]);
            return 2;
        }
        if (argc >= 4) snprintf(relay_host, sizeof relay_host, "%s", argv[3]);
    } else if (!strcmp(argv[1], "relay")) {
        int rc;
        unsigned short rport = NC_RELAY_PORT;
        if (argc < 3) { usage(argv[0]); return 2; }
        if (argc >= 4) rport = (unsigned short)atoi(argv[3]);
        if (net_startup() != 0) {
            printf("netcheck: this platform's networking would not start.\n");
            return 3;
        }
        rc = relay_ping(argv[2], rport);
        net_shutdown();
        return rc;
    } else {
        usage(argv[0]);
        return 2;
    }

    if (net_startup() != 0) {
        printf("netcheck: this platform's networking would not start.\n");
        return 3;
    }

    /* The host binds the agreed port because the joiner has to be able to name it. The
       joiner takes any port, which is what lets several joiners share one machine. */
    if (is_relay) {
        /* A HOST'S ID HAS TO FIT A ROOM CODE, which is thirty bits; a joiner's is never
           written down and uses the full width. */
        my_id = is_host ? rc_draw_host_id() : rc_draw_peer_id();
        if (my_id == 0ul) {
            printf("netcheck: this computer would not give a random number.\n");
            net_shutdown();
            return 3;
        }
        sock = net_open_tunnel(relay_host, NC_RELAY_PORT, my_id);
        if (!sock) {
            printf("netcheck: could not reach the relay at %s:%u.\n",
                   relay_host, (unsigned)NC_RELAY_PORT);
            net_shutdown();
            return 3;
        }
    } else
    sock = net_open(is_host ? port : 0);
    if (!sock) {
        printf("netcheck: could not open a UDP socket on port %u.\n", (unsigned)port);
        if (is_host) printf("          Something else is probably already using it.\n");
        net_shutdown();
        return 3;
    }

    memset(&peer, 0, sizeof peer);

    if (is_host) {
        seat = 0;
        if (is_relay) {
            char code[RC_TEXT_MAX];
            if (!rc_encode(my_id, code)) { printf("netcheck: the drawn id will not encode.\n");
                                           net_close(sock); net_shutdown(); return 3; }
            printf("netcheck: hosting THROUGH THE RELAY at %s. Waiting for a joiner.\n",
                   relay_host);
            printf("          ROOM CODE  %s\n", code);
            printf("          On the other machine run:  netcheck relayjoin %s\n", code);
            /* FLUSHED, because this line exists to be READ and acted on while the process
               keeps running. Redirected to a file, stdout is block buffered, so without
               this the one thing the other machine needs sits in a buffer until the
               session ends -- by which time there is nothing to join. */
            fflush(stdout);
        } else {
        printf("netcheck: hosting on UDP port %u. Waiting for a joiner.\n",
               (unsigned)net_local_port(sock));
        printf("          On the other machine run:  netcheck join <this machine> %u\n",
               (unsigned)net_local_port(sock));
        }

        deadline = time(0) + NC_HANDSHAKE_SECS;
        while (!have_peer && time(0) < deadline) {
            n = net_recv(sock, &from, in, (int)sizeof in);
            if (n >= 4 && get_u32(in) == NC_HELLO) {
                unsigned char reply[4];
                put_u32(reply, NC_WELCOME);
                peer = from;
                have_peer = 1;
                /* Answered several times: the joiner is waiting on this and it is the one
                   datagram whose loss would strand the whole session. */
                net_send(sock, &peer, reply, 4);
                net_send(sock, &peer, reply, 4);
                net_send(sock, &peer, reply, 4);
                net_addr_text(&peer, txt, (int)sizeof txt);
                printf("netcheck: joiner at %s\n", txt);
            }
            if (!have_peer) nap_ms(5);
        }
        if (!have_peer) {
            printf("netcheck: nobody joined within %d seconds.\n", NC_HANDSHAKE_SECS);
            printf("          If the joiner says it is sending, the port is not reaching\n");
            printf("          this machine: check the firewall, and if the two are not on\n");
            printf("          the same network, that UDP %u is forwarded here.\n",
                   (unsigned)net_local_port(sock));
            net_close(sock); net_shutdown();
            return 1;
        }
    } else {
        seat = 1;
        if (is_relay) {
            /* THE HOST IS AN ID. Nothing to resolve: the relay's address was looked up
               when the socket opened, and the room code names who to ask for. */
            net_addr_tunnel(&peer, peer_id);
        } else if (net_resolve(argv[2], port, &peer) != 0) {
            printf("netcheck: could not resolve '%s'.\n", argv[2]);
            net_close(sock); net_shutdown();
            return 3;
        }
        if (is_relay) {
            char code[RC_TEXT_MAX];
            if (rc_encode(peer_id, code)) printf("netcheck: joining room %s\n", code);
            else                          printf("netcheck: joining room %lu\n", peer_id);
        } else {
            net_addr_text(&peer, txt, (int)sizeof txt);
            printf("netcheck: joining %s\n", txt);
        }

        deadline = time(0) + NC_HANDSHAKE_SECS;
        while (!have_peer && time(0) < deadline) {
            unsigned char hello[4];
            put_u32(hello, NC_HELLO);
            net_send(sock, &peer, hello, 4);
            n = net_recv(sock, &from, in, (int)sizeof in);
            if (n >= 4 && get_u32(in) == NC_WELCOME) {
                have_peer = 1;
                printf("netcheck: host answered.\n");
            }
            if (!have_peer) nap_ms(20);
        }
        if (!have_peer) {
            printf("netcheck: the host never answered within %d seconds.\n", NC_HANDSHAKE_SECS);
            printf("          Either it is not running, or UDP %u does not reach it.\n",
                   (unsigned)port);
            net_close(sock); net_shutdown();
            return 1;
        }
    }

    /* Both ends agree on two seats and who is who, which is the only state a lockstep
       match needs before its first turn beyond the scenario itself. */
    if (ls_init(&ls, 2, seat) != LS_OK) {
        printf("netcheck: the scheduler refused a two seat match, which should be impossible.\n");
        net_close(sock); net_shutdown();
        return 3;
    }
    memset(&tally, 0, sizeof tally);
    memset(&rtt, 0, sizeof rtt);

    printf("netcheck: running %d turns as seat %d.\n", NC_TURNS, seat);

    for (turn = 0; turn < NC_TURNS; turn++) {
        unsigned char order[22];
        make_order(order, seat, turn);
        ls_local_order(&ls, order, (int)sizeof order);

        len = ls_pack(&ls, pkt, (int)sizeof pkt);
        if (len > 0 && net_send(sock, &peer, pkt, len) > 0) sent++;

        /* One probe every so often, carrying this machine's own clock reading. */
        if ((turn % NC_PING_EVERY) == 0) {
            unsigned char ping[8];
            put_u32(ping, NC_PING);
            put_u32(ping + 4, now_ms());
            net_send(sock, &peer, ping, 8);
        }

        /* Wait for the turn to become runnable, exactly as the game's frame loop will.
           The bound is what turns a dead peer into a report rather than a hang. */
        tdeadline = time(0) + NC_TURN_SECS;
        while (time(0) < tdeadline) {
            while ((n = net_recv(sock, &from, in, (int)sizeof in)) > 0) {
                if (!net_addr_equal(&from, &peer)) continue;
                /* THE HANDSHAKE SHARES THIS SOCKET, so a late HELLO or one of the
                   repeated WELCOMEs can still be in the queue once the match has started.
                   They are not lockstep packets and must not be offered to the scheduler:
                   it would refuse them, correctly, and the refusal would be counted as a
                   malformed packet, which is the one number in the verdict a person is
                   going to read as trouble. Skipped explicitly instead, so packets_bad
                   means only what it says. */
                if (n == 4 && (get_u32(in) == NC_HELLO || get_u32(in) == NC_WELCOME)) continue;
                /* The probe shares the socket too, and is skipped for the same reason.
                   A PING is answered with the sender's own stamp untouched; a PONG is
                   this peer's stamp coming home, so the difference is the round trip and
                   no clock on the other machine is involved in it. */
                if (n == 8 && get_u32(in) == NC_PING) {
                    unsigned char pong[8];
                    put_u32(pong, NC_PONG);
                    put_u32(pong + 4, get_u32(in + 4));
                    net_send(sock, &peer, pong, 8);
                    continue;
                }
                if (n == 8 && get_u32(in) == NC_PONG) {
                    rtt_add(&rtt, now_ms() - get_u32(in + 4));
                    continue;
                }
                if (ls_on_packet(&ls, in, n) == LS_OK) got++;
            }
            if (ls_turn_ready(&ls)) break;
            nap_ms(1);
        }
        if (!ls_turn_ready(&ls)) {
            printf("netcheck: STALLED on turn %u waiting for seat mask 0x%X.\n",
                   ls_exec_turn(&ls), ls_waiting_mask(&ls));
            printf("          The peer stopped sending, or its packets stopped arriving.\n");
            net_close(sock); net_shutdown();
            return 1;
        }
        while (ls_turn_ready(&ls)) ls_run_turn(&ls, sink, &tally);
    }

    printf("\n");
    printf("netcheck: finished %u turns as seat %d\n", ls_exec_turn(&ls), seat);
    printf("          packets sent %lu, accepted %lu, malformed %lu\n",
           sent, got, ls.packets_bad);
    printf("          orders executed %lu\n", tally.orders);
    printf("          ORDER DIGEST %08X\n", tally.digest);
    rtt_report(&rtt, "peer");
    printf("          Packets accepted against what the OTHER end says it sent is the\n");
    printf("          loss figure; anything the redundancy covered never shows up here.\n");
    if (ls.packets_bad > 0) {
        printf("\n");
        printf("  %lu packet(s) were malformed, and LOSS DOES NOT PRODUCE THAT: a lost\n",
               ls.packets_bad);
        printf("  packet simply never arrives. A packet that arrives and cannot be read\n");
        printf("  is either a peer running a different build of this tool, or something\n");
        printf("  else on this port. Check the other end's version first.\n");
    }
    printf("\n");
    printf("  Compare the digest with the other machine's. Identical digests mean the two\n");
    printf("  peers executed the same orders in the same sequence, which is the whole of\n");
    printf("  what lockstep has to guarantee. Different digests mean the scheduler is not\n");
    printf("  ready for the engine to be put on top of it, and the engine is not the cause.\n");

    net_close(sock);
    net_shutdown();
    return (ls.packets_bad == 0 && tally.orders > 0) ? 0 : 1;
}
