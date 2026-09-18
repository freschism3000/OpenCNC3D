/*
 * gate_lobby.c -- the LOBBY state machine, driven end to end in one process.
 *
 * WHY THIS AND NOT THE SCREEN. menu/dosmp.c and the lobby screen own no socket: they
 * answer what the player asked for and the shell calls the functions below. So the
 * question "does hosting, joining, readying and starting actually work" is a question
 * about THIS layer, and it can be asked with no SDL, no window and no second machine.
 * A gate that drove the pixels would be slower, flakier, and would still be testing this.
 *
 * WHAT IT COVERS, and each leg is a thing that was written and never run:
 *   1. a host opens a room and reports itself waiting
 *   2. a joiner finds it, is seated, and BOTH agree on the seat number
 *   3. the host's START is REFUSED while the joiner has not readied
 *   4. the joiner readies, the host sees it, and START is then allowed
 *   5. both ends reach STARTED, with the same scenario and the same seat count
 *   6. a joiner with the wrong passcode is refused BY NAME rather than timing out
 *   7. a player who never readies can be REMOVED by the host, and is told so by name
 *   9. THE CHAT: a joiner's line reaches the host attributed to the JOINER'S seat (the
 *      packet's, never the sender's address), sanitised, and the host's own line is in
 *      its own ring -- the speaker sees what everybody else sees
 *  13. THE RELAY ARMING is one shot and CANNOT LEAK into the next room, including from a
 *      door that refused its own arguments before opening anything; a relayed room must
 *      carry a passcode; and the two blocking command-line doors refuse to be relayed
 *  12. A CHANGED OFFER (map or rules) UNLIGHTS EVERY READY, on BOTH ends, so nobody is
 *      committed to a match they were never shown
 *  11. THE MAP CHECK is asked when the WELCOME NAMES the host's map -- the first moment
 *      a joiner can answer it -- so "I have not got that map" and "my copy differs" are
 *      two different refusals in words, and the matching copy still gets in
 *  10. AN UNREADABLE ENGINE (order-wire hash 0) is refused in BOTH directions, so two
 *      builds that each failed to read their own brain can no longer match each other
 *      and switch the compatibility check off between them
 *   8. THE SEATS ARE A MENU (v5): in a room of four with seat 1 a BOT, seat 2 EMPTY and
 *      seat 3 BLOCKED, a joiner walks past the computer into seat 2, the room wants
 *      exactly two people, START is allowed once that joiner readies, and the started
 *      match's lockstep waits on nobody but the person -- a BOT and a BLOCK never speak
 *  14. A START PRESSED BEFORE THE ROUND TRIP IS MEASURED IS HELD FOR IT, and goes out on
 *      the lookahead the measurement gives (leg 1 checks the same thing when its own
 *      quick press happens to beat the first ping)
 *  15. a seat that never answers a ping holds it for a bounded 3 s, then gets the widest
 *      lookahead the band allows
 *  16. in a room of three the lookahead is the SLOWEST seat's, not the first to answer
 *  17. a held START is dropped by a goodbye, a READY going out or a changed map, and an
 *      identical push of the setup does not drop it
 *  18. nobody is seated while a pressed START is being measured, so a knock cannot
 *      cancel the press, and the empty seats close when it commits
 *  19. once START is on the wire, a seat request cannot change the room between one copy
 *      of it and the next
 *  20. a START that cannot finish while anybody still seated may have it FAILS the room
 *      with a goodbye to everyone; with nobody left who can have it, the room waits again
 *      and the next start is a whole new one; and a seat removed mid-start is sent a
 *      goodbye too, in case the START already put it in the match
 *  21. A LOST READY DOES NOT SHUT THE ROOM. The joiner's ready tick is swallowed on its
 *      way to the host, the person presses nothing else, and the room still fills, still
 *      lights up and still starts -- because the tick is re-sent until the host echoes it
 *  22. the request for a turn, on the wire: the host answers the seat that asked, passes
 *      the question to the other joiner it names, does both at most once per beat and at
 *      sixteen bytes however long the question was, and ignores one signed with another
 *      seat's number or naming seats this match has not got
 *  23. THE PROBE: a host answers a stranger's probe with no HELLO, no seat and no passcode,
 *      echoes the asker's nonce and stamp in sixteen bytes however long the probe was,
 *      answers nothing shorter, answers one sender eight times a second at most, counts
 *      two sockets on one address as one sender, and logs a flood in one line a second; the
 *      browser's own prober gets four spaced answers from that room and seats nobody, and
 *      a host that does not know the word and a port with nobody on it both read as no
 *      answer only once the schedule has run; bad keys, cancel and clear
 *  24. a RELAYED room is probed through a relay stand-in that forwards only to ids it has
 *      seen: the room answers and seats nobody, a code nobody holds reads as no answer,
 *      and every relayed room is asked through ONE registration; a second relay id is a
 *      second sender, twenty senders keeping the room's table of senders full silence
 *      neither the browser's prober nor each other, and two spellings of one room code are
 *      one target sending one round
 */
#include "netmatch.h"
#include "net_udp.h"
#include "lockstep.h"
#include "roomcode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
  #include <sys/types.h>
  #include <sys/wait.h>
#endif

#if defined(_WIN32)
  #include <windows.h>
  static void gl_nap(int ms) { Sleep((DWORD)ms); }
#else
  #include <unistd.h>
  #include <sys/time.h>
  static void gl_nap(int ms) { usleep((useconds_t)ms * 1000); }
#endif

static int fails = 0, checks = 0;

/* THE MAP HASHER THE JOINER CHILD INSTALLS, for leg 11. netmatch cannot open a file, so
   the real game hands it one of these over the scenario's .INI and .BIN bytes; here the
   three answers a machine can give are simply named. 0 is "I have not got that map". */
static unsigned gl_map_hash_none (const char* scen, void* u) { (void)scen; (void)u; return 0u; }
static unsigned gl_map_hash_wrong(const char* scen, void* u) { (void)scen; (void)u; return 0xDEADBEEFu; }
static unsigned gl_map_hash_right(const char* scen, void* u) { (void)scen; (void)u; return 0x12345678u; }

static void check(int cond, const char *what)
{
    checks++;
    if (!cond) { printf("FAIL  %s\n", what); fails++; }
}

/* THE TWO PEERS ARE TWO PROCESSES IN REAL LIFE AND ONE HERE, so they cannot both be
 * "the" lobby: netmatch keeps one match in file statics, which is right for a game and
 * impossible to drive both sides of at once. So the host runs in THIS process and the
 * joiner is a child, and the two talk over loopback exactly as separate computers would.
 *
 * That is not a compromise for the test's convenience: it is the only shape that
 * exercises the real single-match code rather than a second copy of it built for testing.
 */
static void fill_setup(NmSetup *s, const char *scen)
{
    int i;
    memset(s, 0, sizeof *s);
    snprintf(s->scenario, sizeof s->scenario, "%s", scen);
    s->credits = 5000;
    s->tiberium = 1;
    s->crates = 0;
    s->superweapons = 1;
    s->bases = 1;
    s->unit_count = 0;
    s->speed = 3;
    s->humans = 2;
    s->seats = 2;
    for (i = 0; i < NM_MAX_SEATS; i++) {
        s->house[i] = (unsigned char)(i & 1);
        s->colour[i] = (unsigned char)i;
        s->team[i] = (unsigned char)i;
        s->start[i] = (unsigned char)i;
        s->is_ai[i] = (unsigned char)(i >= 2);
        s->mode[i] = (unsigned char)(i < 2 ? NM_SEAT_HUMAN : NM_SEAT_BLOCK);   /* v5 */
    }
}

/* ---- JOINERS WRITTEN BY HAND, for the legs about the START itself (14 to 20) ----
 *
 * A child running the real netmatch answers every ping at once and acknowledges every
 * START the moment it lands, so it can never be the slow link, the seat that never
 * answers, or the seat whose acknowledgement comes late. Those are exactly the cases a
 * START has to survive. So these joiners are plain sockets in this process speaking the
 * wire's bytes, with the answers the legs need withheld or delayed, while the host is
 * still the real code. Writing the bytes out here rather than calling the code under test
 * is also what makes these the other end of the protocol rather than a second copy of it:
 * if the wire moves, these legs fail and say so. */
#define GL_HELLO    0x4D4C4548u
#define GL_WELCOME  0x4D434C57u
#define GL_READY    0x4D594452u
#define GL_TICK     0x4B434954u
#define GL_BYE      0x4D455942u
#define GL_PING     0x474E4950u
#define GL_PONG     0x474E4F50u
#define GL_START    0x4D545253u
#define GL_SACK     0x4B434153u
#define GL_SEATPREF 0x54414553u
#define GL_NEED     0x4445454Eu
#define GL_LS       0x33434E50u   /* a turn packet, which is what an answer looks like */
#define GL_VERSION  10
#define GL_SETUP_BYTES 192
#define GL_START_BYTES (16 + GL_SETUP_BYTES)
#define GL_SETUP_AHEAD 38        /* offsets inside the setup, after the 16 byte header */
#define GL_SETUP_COLOUR 56

static unsigned gl_now_ms(void)
{
#if defined(_WIN32)
    return (unsigned)GetTickCount();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned)(tv.tv_sec * 1000u + tv.tv_usec / 1000u);
#endif
}
static void gl_put(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v & 0xFF); p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF); p[3] = (unsigned char)((v >> 24) & 0xFF);
}
static unsigned gl_get(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16)
         | ((unsigned)p[3] << 24);
}

typedef struct {
    NetSock *sock;
    NetAddr host;
    int seat;                 /* from the WELCOME, -1 before it */
    int pong_ms;              /* how long a ping waits for its answer; -1 never answers */
    int auto_sack;            /* acknowledge a START the moment it arrives */
    unsigned char held[8];
    unsigned held_at;
    int holding;
    int starts;               /* START packets received, re-sends included */
    unsigned char first_start[GL_START_BYTES];
    unsigned char last_start[GL_START_BYTES];
    int host_bye;             /* a goodbye naming seat 0, the host, arrived */
    int turn_pkts;            /* turn packets received, which is what an answer is   */
    int needs;                /* NM_NEED questions the host forwarded to this seat   */
    int need_len;             /* and how long the last of them was                   */
} GlFake;

/* Opened answering no ping at all; a leg sets pong_ms when it wants the answers to flow. */
static int gl_fake_open(GlFake *f, unsigned short port, int auto_sack)
{
    memset(f, 0, sizeof *f);
    f->seat = -1;
    f->pong_ms = -1;
    f->auto_sack = auto_sack;
    f->sock = net_open(0);
    return f->sock && net_resolve("127.0.0.1", port, &f->host) == 0;
}
static void gl_fake_close(GlFake *f)
{
    if (f->sock) net_close(f->sock);
    f->sock = NULL;
}
static void gl_fake_send(GlFake *f, const unsigned char *b, int n)
{
    if (f->sock) net_send(f->sock, &f->host, b, n);
}
static void gl_fake_hello(GlFake *f)
{
    unsigned char h[20 + NM_PLAYER_NAME_MAX];
    memset(h, 0, sizeof h);
    gl_put(h, GL_HELLO);
    gl_put(h + 4, GL_VERSION);
    gl_put(h + 8, 0xABCDEF01u);
    gl_put(h + 12, 0x12345678u);
    gl_put(h + 16, 0u);
    memcpy(h + 20, "HANDMADE", 8);
    gl_fake_send(f, h, (int)sizeof h);
}
static void gl_fake_tick(GlFake *f, int on)
{
    unsigned char t[12];
    gl_put(t, GL_TICK); gl_put(t + 4, (unsigned)f->seat); gl_put(t + 8, on ? 1u : 0u);
    gl_fake_send(f, t, 12);
}
static void gl_fake_bye(GlFake *f)
{
    unsigned char t[12];
    int k;
    gl_put(t, GL_BYE); gl_put(t + 4, (unsigned)f->seat); gl_put(t + 8, 0xFFFFFFFFu);
    for (k = 0; k < 3; k++) gl_fake_send(f, t, 12);
}
static void gl_fake_sack(GlFake *f)
{
    unsigned char t[8];
    int k;
    gl_put(t, GL_SACK); gl_put(t + 4, (unsigned)f->seat);
    for (k = 0; k < 3; k++) gl_fake_send(f, t, 8);
}
static void gl_fake_seatpref(GlFake *f, int colour)
{
    unsigned char p[24];
    gl_put(p, GL_SEATPREF); gl_put(p + 4, (unsigned)f->seat);
    gl_put(p + 8, 0u); gl_put(p + 12, (unsigned)f->seat); gl_put(p + 16, (unsigned)colour);
    gl_put(p + 20, 0xFFFFFFFFu);
    gl_fake_send(f, p, 24);
}
static void gl_fake_pump(GlFake *f)
{
    unsigned char in[1024];
    NetAddr from;
    int n;
    if (!f->sock) return;
    while ((n = net_recv(f->sock, &from, in, (int)sizeof in)) > 0) {
        const unsigned m = n >= 4 ? gl_get(in) : 0u;
        if (m == GL_WELCOME && n >= 16) {
            unsigned char rd[8];
            f->seat = (int)gl_get(in + 12);
            gl_put(rd, GL_READY); gl_put(rd + 4, (unsigned)f->seat);
            gl_fake_send(f, rd, 8);
        } else if (m == GL_START && n >= GL_START_BYTES) {
            if (f->starts == 0) memcpy(f->first_start, in, GL_START_BYTES);
            memcpy(f->last_start, in, GL_START_BYTES);
            f->starts++;
            if (f->auto_sack) gl_fake_sack(f);
        } else if (m == GL_PING && n >= 8 && f->pong_ms >= 0 && !f->holding) {
            gl_put(f->held, GL_PONG);
            memcpy(f->held + 4, in + 4, 4);
            f->held_at = gl_now_ms();
            f->holding = 1;
        } else if (m == GL_BYE && n >= 8 && gl_get(in + 4) == 0u) {
            f->host_bye = 1;
        } else if (m == GL_LS) {
            f->turn_pkts++;
        } else if (m == GL_NEED) {
            f->needs++;
            f->need_len = n;
        }
    }
    if (f->holding && f->pong_ms >= 0 && gl_now_ms() - f->held_at >= (unsigned)f->pong_ms) {
        gl_fake_send(f, f->held, 8);
        f->holding = 0;
    }
}
/* The host polls and every joiner is serviced, for `ms`. */
static void gl_run(GlFake *f, int nf, int ms)
{
    const unsigned t0 = gl_now_ms();
    int i;
    while (gl_now_ms() - t0 < (unsigned)ms) {
        nm_lobby_poll();
        for (i = 0; i < nf; i++) gl_fake_pump(&f[i]);
        gl_nap(5);
    }
}
/* A room `seats` wide on `port` with the first `nf` joiners seated and readied. The
   joiners answer no ping until the leg says so, so nothing is measured before the leg
   decides it should be. */
static int gl_room(unsigned short port, int seats, GlFake *f, int nf)
{
    NmSetup s;
    unsigned t0;
    int i, all;
    fill_setup(&s, "SCM01EA");
    s.seats = seats;
    s.humans = seats;
    for (i = 0; i < NM_MAX_SEATS; i++) {
        s.is_ai[i] = 0;
        s.mode[i] = (unsigned char)(i == 0 ? NM_SEAT_HUMAN
                                           : i < seats ? NM_SEAT_OPEN : NM_SEAT_BLOCK);
    }
    if (!nm_lobby_host(port, &s, 0xABCDEF01u, 0x12345678u, seats, "HANDMADE", NULL)) return 0;
    t0 = gl_now_ms();
    for (;;) {
        for (i = 0; i < nf; i++) if (f[i].seat < 0) gl_fake_hello(&f[i]);
        gl_run(f, nf, 60);
        all = 1;
        for (i = 0; i < nf; i++) if (f[i].seat < 0) all = 0;
        if (all || gl_now_ms() - t0 > 3000u) break;
    }
    for (i = 0; i < nf; i++) if (f[i].seat > 0) gl_fake_tick(&f[i], 1);
    gl_run(f, nf, 150);
    return all && nm_lobby_all_ready();
}
static void gl_room_close(GlFake *f, int nf)
{
    int i;
    nm_shutdown();
    nm_lobby_cancel();
    for (i = 0; i < nf; i++) gl_fake_close(&f[i]);
    gl_nap(100);
}
/* Poll until the host's START has been committed, or `ms` pass. */
static int gl_until_starting(GlFake *f, int nf, int ms)
{
    const unsigned t0 = gl_now_ms();
    while (!nm_lobby_starting() && gl_now_ms() - t0 < (unsigned)ms) gl_run(f, nf, 10);
    return nm_lobby_starting();
}

/* ---- THE PROBE, for legs 23 and 24 ------------------------------------------------
 *
 * The askers here are plain sockets writing the bytes out, for the reason the joiners
 * above are: the host is the real code, and a second writer of the wire is what notices
 * the wire moving. The browser's own prober, which is what the game list calls, is then
 * driven against the same real host. */
#define GL_PROBE    0x424F5250u
#define GL_PROBEACK 0x4B425250u

typedef struct {
    int acks;                 /* PROBEACK words received                               */
    int other;                /* anything else at all: a WELCOME or a REFUSE lands here */
    int last_len;
    unsigned last_nonce, last_stamp, last_word;
} GlProbeSeen;

static void gl_probe_send(GlFake *f, unsigned nonce, unsigned stamp, int len)
{
    unsigned char p[600];
    if (len > (int)sizeof p) len = (int)sizeof p;
    memset(p, 0xEE, sizeof p);
    gl_put(p, GL_PROBE);
    gl_put(p + 4, nonce);
    gl_put(p + 8, stamp);
    gl_put(p + 12, 0u);
    gl_fake_send(f, p, len);
}
static void gl_probe_collect(GlFake *f, GlProbeSeen *s)
{
    unsigned char in[1024];
    NetAddr from;
    int n;
    if (!f->sock) return;
    while ((n = net_recv(f->sock, &from, in, (int)sizeof in)) > 0) {
        if (n >= 4 && gl_get(in) == GL_PROBEACK) {
            s->acks++;
            s->last_len = n;
            if (n >= 16) {
                s->last_nonce = gl_get(in + 4);
                s->last_stamp = gl_get(in + 8);
                s->last_word = gl_get(in + 12);
            }
        } else {
            s->other++;
        }
    }
}
/* The host polls and up to two askers read what came back, for `ms`. */
static void gl_probe_run(GlFake *a, GlProbeSeen *sa, GlFake *b, GlProbeSeen *sb, int ms)
{
    const unsigned t0 = gl_now_ms();
    while (gl_now_ms() - t0 < (unsigned)ms) {
        nm_lobby_poll();
        if (a) gl_probe_collect(a, sa);
        if (b) gl_probe_collect(b, sb);
        gl_nap(2);
    }
}

/* A RELAY STAND-IN THAT ENFORCES WHAT A RELAYED PROBE DEPENDS ON: an id is known only
 * once it has sent something, and a datagram for an id nobody has registered goes
 * nowhere. That second rule is why a browsing player needs a registration of its own at
 * all, so it is counted rather than assumed, and a leg can tell "the relay had nowhere to
 * send it" from "the host did not answer". The ping and the id rules are the public
 * servers' own; the header is little endian through the same helper as every other
 * word here. */
typedef struct {
    NetSock *sock;
    unsigned id[40];
    NetAddr at[40];
    int n;                    /* distinct sender ids ever seen                  */
    int nowhere;              /* datagrams addressed to an id nobody registered */
    unsigned watch;           /* an id whose incoming probes are counted, or 0  */
    int watched;              /* probes forwarded to it                         */
} GlRelay;

static void gl_relay_pump(GlRelay *r)
{
    unsigned char buf[1100];
    NetAddr from;
    int n, i, k;
    if (!r->sock) return;
    while ((n = net_recv(r->sock, &from, buf, (int)sizeof buf)) > 0) {
        unsigned snd, rcv;
        if (n < 8) continue;
        snd = gl_get(buf);
        rcv = gl_get(buf + 4);
        if (snd == 0u && rcv == 0u) {
            if (n == 50) net_send(r->sock, &from, buf, 12);
            continue;
        }
        if (snd == 0u || snd == 0xFFFFFFFFu || snd == rcv) continue;
        k = -1;
        for (i = 0; i < r->n; i++) if (r->id[i] == snd) k = i;
        if (k < 0) {
            if (r->n >= 40) continue;
            k = r->n++;
            r->id[k] = snd;
        }
        r->at[k] = from;
        if (rcv == 0u) continue;          /* a registration: forward nothing */
        k = -1;
        for (i = 0; i < r->n; i++) if (r->id[i] == rcv) k = i;
        if (k < 0) { r->nowhere++; continue; }
        if (net_addr_equal(&r->at[k], &from)) continue;
        if (r->watch && rcv == r->watch && n >= 24 && gl_get(buf + 8) == GL_PROBE) r->watched++;
        net_send(r->sock, &r->at[k], buf, n);
    }
}

/* MANY RELAY IDS FROM ONE PLAIN SOCKET. The transport keeps a small fixed pool of sockets,
   so twenty tunnel sockets cannot be open in one process; but a relay knows a client only
   by the id in the eight byte header and the address it last came from, so one socket that
   writes the header by hand, under a different id each time, is twenty clients to the relay
   and twenty senders to the room. Base id + k is asker k. */
#define GL_FAKE_BASE 0x00A00001u
static void gl_id_probe(NetSock *s, const NetAddr *relay, unsigned id, unsigned room, unsigned nonce)
{
    unsigned char p[24];
    gl_put(p, id);
    gl_put(p + 4, room);
    gl_put(p + 8, GL_PROBE);
    gl_put(p + 12, nonce);
    gl_put(p + 16, gl_now_ms());
    gl_put(p + 20, 0u);
    if (s) net_send(s, relay, p, 24);
}

/* The relay, the room and the many-id socket, for at least `ms`; each answer is counted
   against the id it was addressed to, into got[0..n). */
static void gl_fk_pump(GlRelay *rel, NetSock *fake, int *got, int n, int ms)
{
    const unsigned u0 = gl_now_ms();
    unsigned char in[64];
    NetAddr from;
    int len;
    do {
        gl_relay_pump(rel);
        nm_lobby_poll();
        gl_relay_pump(rel);
        while (fake && (len = net_recv(fake, &from, in, (int)sizeof in)) > 0) {
            const unsigned k = gl_get(in + 4) - GL_FAKE_BASE;
            if (len >= 24 && gl_get(in + 8) == GL_PROBEACK && k < (unsigned)n) got[k]++;
        }
        gl_nap(1);
    } while (gl_now_ms() - u0 < (unsigned)ms);
}

/* ---- A LOSSY WIRE, for leg 21 ------------------------------------------------------
 *
 * The joiners above are hand written, which is right for the legs about the HOST: the
 * bytes the host must survive are written out and withheld one by one. It is exactly
 * wrong for a leg about the JOINER, because the behaviour under test would then be the
 * harness's own.
 *
 * So this leg keeps BOTH ends real -- the host in this process, the joiner in a child --
 * and puts a socket between them that forwards everything unchanged except the copies it
 * is told to swallow. Loopback loses nothing on its own, and a lobby that only works on a
 * wire that loses nothing is a lobby that only works here.
 *
 * The child dials this socket, so the host sees the wire's address as the joiner's seat
 * and answers to it; nothing on either end knows it is not a direct link.
 */
typedef struct {
    NetSock *sock;
    NetAddr host;        /* where the real host is listening       */
    NetAddr peer;        /* the child, learned from its first word  */
    int have_peer;
    unsigned swallow;    /* the message word to swallow             */
    int swallow_left;    /* how many more copies of it to swallow   */
    int swallowed;       /* how many were swallowed                 */
    int passed;          /* how many of that word got through       */
} GlWire;

static int gl_wire_open(GlWire *w, unsigned short listen_port, unsigned short host_port,
                        unsigned swallow, int copies)
{
    memset(w, 0, sizeof *w);
    w->swallow = swallow;
    w->swallow_left = copies;
    w->sock = net_open(listen_port);
    return w->sock && net_resolve("127.0.0.1", host_port, &w->host) == 0;
}
static void gl_wire_close(GlWire *w)
{
    if (w->sock) net_close(w->sock);
    w->sock = NULL;
}
static void gl_wire_pump(GlWire *w)
{
    unsigned char in[LS_PACKET_MAX];
    NetAddr from;
    int n;
    if (!w->sock) return;
    while ((n = net_recv(w->sock, &from, in, (int)sizeof in)) > 0) {
        if (n < 4) continue;
        if (net_addr_equal(&from, &w->host)) {
            if (w->have_peer) net_send(w->sock, &w->peer, in, n);
            continue;
        }
        w->peer = from;
        w->have_peer = 1;
        if (gl_get(in) == w->swallow) {
            if (w->swallow_left > 0) {
                w->swallow_left--;
                w->swallowed++;
                continue;        /* the packet a real network drops */
            }
            w->passed++;
        }
        net_send(w->sock, &w->host, in, n);
    }
}

/* The joiner half, run as a child. Returns 0 when it reached a started match. */
/* A joiner that never readies, so the host has to remove it. This is the AFK player the
   override exists for, and it is the one shape the ready gate cannot resolve on its own. */
static int run_idle_joiner(unsigned short port)
{
    int i, state = NM_LOBBY_IDLE;
    if (!nm_lobby_join("127.0.0.1", port, 0xABCDEF01u, 0x12345678u, NULL)) return 1;
    for (i = 0; i < 400; i++) {
        state = nm_lobby_poll();
        if (state == NM_LOBBY_REFUSED || state == NM_LOBBY_FAILED) break;
        gl_nap(50);   /* and never presses READY */
    }
    printf("IDLE|state=%d|kicked=%d|err=%s\n", state, nm_lobby_was_kicked(),
           nm_lobby_error());
    /* The point of the leg: it must learn that it was REMOVED, by name, rather than
       being left to work out that a connection stopped. */
    return (state == NM_LOBBY_REFUSED && nm_lobby_was_kicked()) ? 0 : 1;
}

static int run_joiner(unsigned short port, const char *pass, int expect_refusal,
                      unsigned abi, const char *maps, int ready_once)
{
    NmSetup got;
    int i, state = NM_LOBBY_IDLE;
    /* Leg 12: ready ONCE and then leave the light alone, so that when the host changes
       the map underneath it the clear can be observed instead of being immediately undone
       by this loop pressing READY again. saw_cleared is this half of the assertion. */
    int readied = 0, saw_cleared = 0;
    memset(&got, 0, sizeof got);
    /* Leg 11 installs one of the three; every other leg passes none, and with no hasher
       installed netmatch behaves exactly as it did before the hook existed. */
    if (maps && !strcmp(maps, "none"))  nm_set_map_hasher(gl_map_hash_none,  NULL);
    if (maps && !strcmp(maps, "wrong")) nm_set_map_hasher(gl_map_hash_wrong, NULL);
    if (maps && !strcmp(maps, "right")) nm_set_map_hasher(gl_map_hash_right, NULL);
    /* THE HELLO'S MAP FINGERPRINT IS 0 ON THIS PATH, which is what the GUI sends and is
       correct: a joiner does not know the host's map until the WELCOME names it. */
    if (!nm_lobby_join("127.0.0.1", port, abi, maps ? 0u : 0x12345678u, pass)) {
        printf("JOIN|open-failed|%s\n", nm_lobby_error());
        return expect_refusal ? 0 : 1;
    }
    for (i = 0; i < 600; i++) {          /* up to 30 s */
        state = nm_lobby_poll();
        if (state == NM_LOBBY_STARTED) break;
        if (state == NM_LOBBY_REFUSED || state == NM_LOBBY_FAILED) break;
        /* READY as soon as this peer has a seat, which is what a person does -- and a
           word first, with a byte the menu font cannot draw in it, for leg 9. */
        if (ready_once && readied && !nm_lobby_my_ready()) saw_cleared = 1;
        if (nm_lobby_seat() >= 1 && !nm_lobby_my_ready() && !(ready_once && readied)) {
            nm_chat_say("HELLO\001FROM THE JOINER");   /* octal: \x01F would be one byte */
            nm_lobby_set_ready(1);
            readied = 1;
        }
        if (ready_once && saw_cleared) break;
        gl_nap(50);
    }
    if (ready_once) {
        printf("JOIN|ready-once|readied=%d|cleared=%d\n", readied, saw_cleared);
        nm_shutdown();
        return (readied && saw_cleared) ? 0 : 1;
    }
    if (expect_refusal) {
        printf("JOIN|state=%d|err=%s\n", state, nm_lobby_error());
        return (state == NM_LOBBY_REFUSED) ? 0 : 1;
    }
    if (state != NM_LOBBY_STARTED) {
        printf("JOIN|never-started|state=%d|err=%s\n", state, nm_lobby_error());
        return 1;
    }
    printf("JOIN|started|seat=%d|scenario=%s|humans=%d\n",
           nm_lobby_seat(), nm_lobby_setup()->scenario, nm_lobby_wanted());
    /* LINGER, because a real joiner does. It goes on to play, so its acknowledgement is
       the last thing the host hears from the lobby rather than being immediately followed
       by a goodbye. Leaving at once was how this gate found the abort bug above, and it
       is not the behaviour being tested here. */
    gl_nap(600);
    nm_shutdown();
    return 0;
}

int main(int argc, char **argv)
{
    NmSetup setup;
    unsigned short port = 17451;
    int i, started = 0, saw_unready_refusal = 0, saw_ready = 0;
    int first_press_worst = -2, first_press_pending = -1, first_press_starting = -1;
    int commit_worst = -2;
    pid_t child;

    /* The child re-execs this same binary with a role, which keeps one file and one
       build rule and means the two halves cannot drift. */
    if (argc > 1 && !strcmp(argv[1], "--joiner")) {
        const unsigned short p = (unsigned short)atoi(argv[2]);
        /* argv[5], when present, is the order-wire hash this joiner announces, in hex.
           Leg 10 uses it to announce 0 -- "I could not read my own brain" -- which is the
           one value that must never be treated as agreement. */
        /* THE STRING, NOT THE COUNT. This read `argc > 4`, so adding a fifth and sixth
           argument for legs 10 and 11 silently told every one of those joiners to EXPECT
           a refusal -- and a joiner expecting one reports success when the match starts
           without it. A gate that passes for the wrong reason is worse than one that
           fails, and this one was one edit away from it. */
        return run_joiner(p, (argc > 3 && argv[3][0]) ? argv[3] : NULL,
                          (argc > 4 && argv[4][0]) ? 1 : 0,
                          (argc > 5 && argv[5][0]) ? (unsigned)strtoul(argv[5], NULL, 16)
                                                   : 0xABCDEF01u,
                          (argc > 6 && argv[6][0]) ? argv[6] : NULL,
                          (argc > 7 && argv[7][0]) ? 1 : 0);
    }
    if (argc > 1 && !strcmp(argv[1], "--idle")) {
        return run_idle_joiner((unsigned short)atoi(argv[2]));
    }

    printf("CNC3D lobby gate\n\n");

    /* ---- legs 1 to 5: a room, a joiner, a ready, a start ---- */
    fill_setup(&setup, "SCM01EA");
    check(nm_lobby_host(port, &setup, 0xABCDEF01u, 0x12345678u, 2, "GATE GAME", NULL) == 1,
          "a host opens a room");
    check(nm_lobby_poll() == NM_LOBBY_WAITING, "and reports itself waiting for a joiner");
    check(nm_lobby_seat() == 0, "the host is seat 0");
    check(nm_lobby_seat_ready(0) == 1, "and the host counts as ready with no toggle");

    child = fork();
    if (child == 0) {
        char ps[16];
        snprintf(ps, sizeof ps, "%u", (unsigned)port);
        execl(argv[0], argv[0], "--joiner", ps, "", (char *)NULL);
        _exit(127);
    }

    for (i = 0; i < 600; i++) {
        const int st = nm_lobby_poll();
        /* The round trip AS THE START WAS COMMITTED: pings answered after that, while the
           acknowledgements come in, can still raise the worst one. */
        if (commit_worst == -2 && nm_lobby_starting()) commit_worst = nm_lobby_worst_rtt();
        if (nm_lobby_filled() >= 2 && !nm_lobby_seat_ready(1)) {
            /* THE MOMENT THAT MATTERS: somebody is in the room and has not readied, so
               the host's START must be refused. Checked here rather than after, because
               once they ready the window is gone. */
            if (!saw_unready_refusal && !nm_lobby_all_ready()) saw_unready_refusal = 1;
        }
        if (nm_lobby_filled() >= 2 && nm_lobby_seat_ready(1)) saw_ready = 1;
        if (saw_ready && nm_lobby_all_ready()) {
            /* THE FIRST PRESS, as it lands. This loop presses the moment the joiner has
               readied, which is the quickest hand there is and so the one most likely to
               beat the first round trip. */
            if (first_press_worst == -2) first_press_worst = nm_lobby_worst_rtt();
            nm_lobby_start();
            if (first_press_pending < 0) {
                first_press_pending = nm_lobby_start_pending();
                first_press_starting = nm_lobby_starting();
            }
            if (commit_worst == -2 && nm_lobby_starting()) commit_worst = nm_lobby_worst_rtt();
        }
        if (st == NM_LOBBY_STARTED) { started = 1; break; }
        if (st == NM_LOBBY_REFUSED || st == NM_LOBBY_FAILED) break;
        gl_nap(50);
    }

    check(saw_unready_refusal, "START is refused while a seated joiner has not readied");
    check(saw_ready, "the host sees the joiner's ready tick arrive");
    check(started, "both ends reach a started match");
    /* AND IT STARTED ON A MEASURED LINK. A press quicker than the first ping used to go
       straight out on the built-in LAN lookahead with nothing measured, which on a relay
       is a fraction of the cover the link needs. */
    if (first_press_worst < 0) {
        check(first_press_pending == 1 && first_press_starting == 0,
              "a START pressed before any round trip was measured is held, not sent");
    } else {
        printf("  note: the first press landed after a round trip was already in (%d ms), "
               "so this leg did not see the hold; legs 14 to 16 force it\n",
               first_press_worst);
    }
    if (started) {
        check(commit_worst >= 0 && nm_lobby_setup()->ahead == nm_ahead_for_rtt(commit_worst),
              "the started room's lookahead is the one its measured round trip gives");
        printf("  lookahead %d from a worst round trip of %d ms at the commit\n",
               nm_lobby_setup()->ahead, commit_worst);
    }
    /* ---- leg 9: the chat ---- */
    {
        int seat = -1, k, joiner_line = -1;
        const char *t;
        nm_chat_say("WELCOME");
        for (k = 0; k < nm_chat_count(); k++) {
            t = nm_chat_line(k, &seat);
            if (t && seat == 1) joiner_line = k;
        }
        check(nm_chat_count() >= 2, "the ring holds the joiner's line and the host's own");
        check(joiner_line >= 0, "the joiner's line is attributed to seat 1, the packet's seat");
        if (joiner_line >= 0) {
            t = nm_chat_line(joiner_line, &seat);
            check(strcmp(t, "HELLO?FROM THE JOINER") == 0,
                  "and the byte the font cannot draw arrived as '?'");
            printf("  chat: seat=%d text=[%s]\n", seat, t);
        }
        t = nm_chat_line(nm_chat_count() - 1, &seat);
        check(t && seat == 0 && strcmp(t, "WELCOME") == 0,
              "the host's own line is in its own ring: a speaker sees what everybody sees");
    }
    if (started) {
        check(nm_lobby_wanted() == 2, "the host started the seat count it asked for");
        check(strcmp(nm_lobby_setup()->scenario, "SCM01EA") == 0,
              "and the scenario the room was opened on");
        printf("  host started: seat=%d humans=%d scenario=%s\n",
               nm_lobby_seat(), nm_lobby_wanted(), nm_lobby_setup()->scenario);
    }
    {
        int status = 0;
        waitpid(child, &status, 0);
        check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "the joiner reached the same started match");
    }
    nm_shutdown();

    /* ---- leg 6: the wrong passcode is refused BY NAME ---- */
    gl_nap(250);
    port = 17452;
    fill_setup(&setup, "SCM01EA");
    check(nm_lobby_host(port, &setup, 0xABCDEF01u, 0x12345678u, 2, "LOCKED", "1234") == 1,
          "a host opens a room with a passcode");
    child = fork();
    if (child == 0) {
        char ps[16];
        snprintf(ps, sizeof ps, "%u", (unsigned)port);
        execl(argv[0], argv[0], "--joiner", ps, "9999", "expect-refusal", (char *)NULL);
        _exit(127);
    }
    for (i = 0; i < 200; i++) { nm_lobby_poll(); gl_nap(25); }
    {
        int status = 0;
        waitpid(child, &status, 0);
        check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "a joiner with the wrong passcode is refused by name, not left to time out");
    }
    nm_shutdown();

    /* ---- leg 10: AN UNREADABLE ENGINE IS REFUSED, AND IT IS REFUSED BOTH WAYS ----
       0 is what the order-wire probe returns when it could not read the brain at all, and
       it used to be allowed to mean "fine". Two builds that had each failed to read their
       own engine compared 0 against 0, matched, and played a match with the compatibility
       check silently switched off -- which is exactly the state every Windows build was in
      , because the lobby's copy of the brain search only knew macOS
       filenames. Both directions are asserted here because the fix is a rule about the
       VALUE, not about the joiner: a host that cannot read its own engine has no business
       seating anybody either. */
    gl_nap(250);
    port = 17456;
    fill_setup(&setup, "SCM01EA");
    check(nm_lobby_host(port, &setup, 0xABCDEF01u, 0x12345678u, 2, "ABIROOM", NULL) == 1,
          "a host with a readable engine opens a room");
    child = fork();
    if (child == 0) {
        char ps[16];
        snprintf(ps, sizeof ps, "%u", (unsigned)port);
        execl(argv[0], argv[0], "--joiner", ps, "", "expect-refusal", "0", (char *)NULL);
        _exit(127);
    }
    for (i = 0; i < 200; i++) { nm_lobby_poll(); gl_nap(25); }
    {
        int status = 0;
        waitpid(child, &status, 0);
        check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "a joiner that could not read its own engine is refused, not seated");
    }
    nm_shutdown();

    gl_nap(250);
    port = 17457;
    fill_setup(&setup, "SCM01EA");
    check(nm_lobby_host(port, &setup, 0u, 0x12345678u, 2, "ABIROOM2", NULL) == 1,
          "a host that could not read its own engine still opens a room");
    child = fork();
    if (child == 0) {
        char ps[16];
        snprintf(ps, sizeof ps, "%u", (unsigned)port);
        execl(argv[0], argv[0], "--joiner", ps, "", "expect-refusal", "ABCDEF01",
              (char *)NULL);
        _exit(127);
    }
    for (i = 0; i < 200; i++) { nm_lobby_poll(); gl_nap(25); }
    {
        int status = 0;
        waitpid(child, &status, 0);
        check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "a host that could not read its own engine refuses a good joiner rather "
              "than seating it");
    }
    nm_shutdown();

    /* ---- leg 11: THE MAP CHECK, ASKED WHERE IT IS ANSWERABLE ----
       Three joiners against one room. The first has not got the host's map at all, the
       second has a different copy of it, the third has the host's. Only the third is
       seated, and the first two are told which of the two it was.

       WHY THIS COULD NOT BE TESTED BEFORE. The check used to sit on the HELLO, comparing
       the host's fingerprint against the hash of whatever map the joiner's own screen was
       showing -- two unrelated maps -- and through the GUI it compared against a literal
       zero, which switches the test off. So the door players use had no map check on it,
       and two people with different copies of the same map started a match and parted on
       the first frame that read a cell. The question is now asked at the moment the
       WELCOME NAMES the host's map, which is the first moment a joiner could answer it. */
    gl_nap(250);
    port = 17458;
    fill_setup(&setup, "SCM01EA");
    check(nm_lobby_host(port, &setup, 0xABCDEF01u, 0x12345678u, 2, "MAPROOM", NULL) == 1,
          "a host opens a room on a known map");
    child = fork();
    if (child == 0) {
        char ps[16];
        snprintf(ps, sizeof ps, "%u", (unsigned)port);
        execl(argv[0], argv[0], "--joiner", ps, "", "expect-refusal", "", "none",
              (char *)NULL);
        _exit(127);
    }
    for (i = 0; i < 200; i++) { nm_lobby_poll(); gl_nap(25); }
    {
        int status = 0;
        waitpid(child, &status, 0);
        check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "a joiner that has not got the host's map is refused, not seated");
    }
    check(nm_lobby_filled() == 1, "and that refusal left the room empty");
    nm_shutdown();

    gl_nap(250);
    port = 17459;
    fill_setup(&setup, "SCM01EA");
    check(nm_lobby_host(port, &setup, 0xABCDEF01u, 0x12345678u, 2, "MAPROOM2", NULL) == 1,
          "a host opens a room for the different-copy leg");
    child = fork();
    if (child == 0) {
        char ps[16];
        snprintf(ps, sizeof ps, "%u", (unsigned)port);
        execl(argv[0], argv[0], "--joiner", ps, "", "expect-refusal", "", "wrong",
              (char *)NULL);
        _exit(127);
    }
    for (i = 0; i < 200; i++) { nm_lobby_poll(); gl_nap(25); }
    {
        int status = 0;
        waitpid(child, &status, 0);
        check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "a joiner holding a DIFFERENT copy of the host's map is refused");
    }
    nm_shutdown();

    /* AND THE ANTI-VACUITY HALF. Two refusals prove nothing if this hasher refuses
       everybody: the matching copy must still get in, through the same code path, with
       the HELLO still carrying the zero the GUI sends. */
    gl_nap(250);
    port = 17460;
    fill_setup(&setup, "SCM01EA");
    check(nm_lobby_host(port, &setup, 0xABCDEF01u, 0x12345678u, 2, "MAPROOM3", NULL) == 1,
          "a host opens a room for the matching-copy leg");
    child = fork();
    if (child == 0) {
        char ps[16];
        snprintf(ps, sizeof ps, "%u", (unsigned)port);
        execl(argv[0], argv[0], "--joiner", ps, "", "", "", "right", (char *)NULL);
        _exit(127);
    }
    started = 0;
    for (i = 0; i < 400; i++) {
        nm_lobby_poll();
        if (!started && nm_lobby_all_ready() && nm_lobby_filled() == 2) {
            started = nm_lobby_start();
        }
        gl_nap(25);
    }
    check(started == 1, "a joiner holding the host's own copy of the map is seated and "
                        "the match starts");
    {
        int status = 0;
        waitpid(child, &status, 0);
        check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "and that joiner reached the started match");
    }
    nm_shutdown();

    /* ---- leg 12: A CHANGED OFFER UNLIGHTS EVERY READY ----
       READY means "I accept these settings". The host could swap the map, the tech level
       or the credits out from under a room that had already agreed to the old ones, and
       every light stayed on: START remained available the whole time and the joiner was
       committed to a match it had never been shown.

       BOTH ENDS ARE ASSERTED because clearing only one is the same as clearing neither. A
       joiner's NM_TICK carries its own ready flag and the host takes the joiner's word for
       it, so a host-side clear alone is overwritten about fifty milliseconds later by the
       next tick from that seat. The child reports its own light going out; the parent
       requires START to be refused. */
    gl_nap(250);
    port = 17461;
    fill_setup(&setup, "SCM01EA");
    check(nm_lobby_host(port, &setup, 0xABCDEF01u, 0x12345678u, 2, "OFFERROOM", NULL) == 1,
          "a host opens a room for the changed-offer leg");
    child = fork();
    if (child == 0) {
        char ps[16];
        snprintf(ps, sizeof ps, "%u", (unsigned)port);
        execl(argv[0], argv[0], "--joiner", ps, "", "", "", "", "readyonce",
              (char *)NULL);
        _exit(127);
    }
    {
        int sawready = 0, refused_after = -1;
        for (i = 0; i < 400; i++) {
            nm_lobby_poll();
            if (!sawready && nm_lobby_all_ready() && nm_lobby_filled() == 2) {
                sawready = 1;
                /* THE HOST CHANGES THE MAP under a room that has already said yes. */
                {
                    NmSetup changed = *nm_lobby_setup();
                    snprintf(changed.scenario, sizeof changed.scenario, "%s", "SCG01EA");
                    nm_lobby_set_setup(&changed);
                }
            } else if (sawready && refused_after < 0) {
                /* Give the clear a moment to reach the wire, then require START to be
                   refused. all_ready going false IS the refusal: nm_lobby_start checks it. */
                if (i > 40 && !nm_lobby_all_ready()) refused_after = i;
            }
            gl_nap(25);
        }
        check(sawready == 1, "the room reached all-ready before the map was changed");
        check(refused_after >= 0,
              "changing the map unlights the room, so START is refused again");
        check(nm_lobby_start() == 0,
              "and START really is refused, not merely reported unready");
    }
    {
        int status = 0;
        waitpid(child, &status, 0);
        check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "and the joiner's own light went out, so its next tick cannot put it back");
    }
    nm_shutdown();

    /* ---- leg 13: THE RELAY ARMING, AND THAT IT CANNOT LEAK ----
       Relayed play differs from LAN play in exactly one place: which kind of socket
       nm_open opens. The arming that selects it is a one-shot, and the dangerous failure
       is not that it fails to apply -- it is that it applies to the WRONG room. A door
       that checks its own arguments and returns before opening anything must still have
       disarmed, or the next LAN game this process hosts goes silently through a relay
       nobody asked for, and nothing on screen says so.

       No relay server is needed for any of this: net_open_tunnel resolves, opens an
       ephemeral socket and sends one unacknowledged registration, so it succeeds against
       an address with nothing listening. What is under test here is which socket got
       opened, which is exactly what nm_is_relayed answers. */
    gl_nap(250);
    fill_setup(&setup, "SCM01EA");
    nm_relay_next("127.0.0.1", 51999, 0x1234ul, 0ul);
    check(nm_lobby_host(17462, &setup, 0xABCDEF01u, 0x12345678u, 2, "RELAYED", "4321") == 1,
          "a relayed room opens");
    check(nm_is_relayed() == 1, "and it is running on a relayed socket");
    nm_shutdown();

    gl_nap(150);
    fill_setup(&setup, "SCM01EA");
    check(nm_lobby_host(17463, &setup, 0xABCDEF01u, 0x12345678u, 2, "PLAIN", NULL) == 1,
          "the next room opens with no arming");
    check(nm_is_relayed() == 0,
          "and it is NOT relayed -- the arming was one shot, and the socket pool did not "
          "hand back a slot still carrying the last one's tunnel state");
    nm_shutdown();

    /* A RELAYED ROOM MAY BE OPEN. It was briefly required to carry a passcode, on the
       reasoning that a room code is a routing address rather than a secret. That holds
       only in a world with no game list: once public games are LISTED for strangers to
       find, a compulsory passcode on one is a contradiction. The passcode makes a game
       private, on a relay exactly as on a LAN. */
    gl_nap(150);
    fill_setup(&setup, "SCM01EA");
    nm_relay_next("127.0.0.1", 51999, 0x2345ul, 0ul);
    check(nm_lobby_host(17464, &setup, 0xABCDEF01u, 0x12345678u, 2, "OPENROOM", NULL) == 1,
          "a relayed room with NO passcode opens: a public game is meant to be joined");
    check(nm_is_relayed() == 1, "and it is relayed");
    nm_shutdown();

    gl_nap(150);
    fill_setup(&setup, "SCM01EA");
    check(nm_lobby_host(17465, &setup, 0xABCDEF01u, 0x12345678u, 2, "AFTER", NULL) == 1,
          "a plain room opens after a relayed one");
    check(nm_is_relayed() == 0, "and it is NOT relayed: the arming was one shot");
    nm_shutdown();

    /* A relayed JOIN needs a room code, not an address. */
    gl_nap(150);
    nm_relay_next("127.0.0.1", 51999, 0x3456ul, 0ul);   /* host_id 0 = no room named */
    check(nm_lobby_join(NULL, 0, 0xABCDEF01u, 0u, NULL) == 0,
          "a relayed join with no room code is refused");
    check(nm_lobby_join("127.0.0.1", 17466, 0xABCDEF01u, 0u, NULL) == 1,
          "and the arming did not survive it: a plain join by address still works");
    check(nm_is_relayed() == 0, "on a plain socket");
    nm_shutdown();

    /* The two BLOCKING doors refuse to be relayed at all. The blocking host reads
       version, abi and scenario out of a HELLO and seats the sender; it has no passcode
       check anywhere, so relaying it would put a door-less room on the internet. */
    gl_nap(150);
    fill_setup(&setup, "SCM01EA");
    nm_relay_next("127.0.0.1", 51999, 0x4567ul, 0ul);
    check(nm_host(17467, &setup, 0xABCDEF01u, 0x12345678u, 2, 1) == -1,
          "the command-line host refuses to be relayed: it has no passcode check");
    nm_relay_next("127.0.0.1", 51999, 0x5678ul, 0x9999ul);
    check(nm_join("127.0.0.1", 17468, &setup, 0xABCDEF01u, 0x12345678u, 1) == -1,
          "and the command-line join refuses for symmetry with it");
    nm_shutdown();

    /* ---- leg 7: the host removes a player who never readies ---- */
    gl_nap(250);
    port = 17453;
    fill_setup(&setup, "SCM01EA");
    check(nm_lobby_host(port, &setup, 0xABCDEF01u, 0x12345678u, 2, "KICKROOM", NULL) == 1,
          "a host opens a room for the removal leg");
    child = fork();
    if (child == 0) {
        char ps[16];
        snprintf(ps, sizeof ps, "%u", (unsigned)port);
        execl(argv[0], argv[0], "--idle", ps, (char *)NULL);
        _exit(127);
    }
    {
        int seated = 0, kicked = 0, refused_while_unready = 0;
        for (i = 0; i < 400; i++) {
            nm_lobby_poll();
            if (nm_lobby_filled() >= 2) {
                seated = 1;
                /* The room is full and the start is still refused, because that seat has
                   not readied and never will. That is the deadlock the override exists
                   for, and it is checked before the override is used. */
                if (!nm_lobby_all_ready()) refused_while_unready = 1;
                /* nm_lobby_kick REFUSES a seat that has not yet acknowledged its
                   welcome, so this simply keeps asking until it takes. That is the leg
                   proving the invariant as well as the removal: a kick that landed
                   before the peer knew its seat number would be read by that peer as
                   somebody else leaving, and it would sit in a room it had been thrown
                   out of. */
                if (refused_while_unready && !kicked) {
                    kicked = nm_lobby_kick(1);
                }
                if (kicked && nm_lobby_filled() == 1) break;
            }
            gl_nap(50);
        }
        check(seated, "the idle player joins");
        check(refused_while_unready,
              "the start stays refused while that player never readies");
        check(kicked, "the host removes the seat");
        check(nm_lobby_filled() == 1, "and the seat is open again");
    }
    {
        int status = 0;
        waitpid(child, &status, 0);
        check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "the removed player is told it was REMOVED, not merely disconnected");
    }
    nm_shutdown();

    /* ---- leg 8: the seats are a menu ---- */
    gl_nap(250);
    port = 17454;
    fill_setup(&setup, "SCM01EA");
    setup.seats = 4;
    setup.mode[1] = NM_SEAT_BOT;
    setup.mode[2] = NM_SEAT_OPEN;
    setup.mode[3] = NM_SEAT_BLOCK;
    check(nm_lobby_host(port, &setup, 0xABCDEF01u, 0x12345678u, 4, "SEATROOM", NULL) == 1,
          "a host opens a room of four: a computer, an empty seat and a blocked one");
    check(nm_lobby_wanted() == 2, "the room wants exactly two people: the host and the empty seat");
    check(nm_lobby_seat_mode(1) == NM_SEAT_BOT && nm_lobby_seat_mode(3) == NM_SEAT_BLOCK,
          "the setup carries the modes the host set");
    check(nm_lobby_setup()->humans == 2 && nm_lobby_setup()->seats == 4,
          "and counts two humans in four seats");
    check(nm_lobby_setup()->is_ai[1] == 1 && nm_lobby_setup()->is_ai[2] == 0,
          "is_ai is read off the modes: the computer is seat 1 and seat 2 is not one");
    child = fork();
    if (child == 0) {
        char ps[16];
        snprintf(ps, sizeof ps, "%u", (unsigned)port);
        execl(argv[0], argv[0], "--joiner", ps, "", (char *)NULL);
        _exit(127);
    }
    {
        int seated2 = 0, ready2 = 0, started2 = 0, bot_never_taken = 1;
        for (i = 0; i < 600; i++) {
            const int st = nm_lobby_poll();
            if (nm_lobby_seat_taken(1)) bot_never_taken = 0;
            if (nm_lobby_seat_taken(2)) seated2 = 1;
            if (seated2 && nm_lobby_seat_ready(2)) ready2 = 1;
            if (ready2 && nm_lobby_all_ready()) nm_lobby_start();
            if (st == NM_LOBBY_STARTED) { started2 = 1; break; }
            if (st == NM_LOBBY_REFUSED || st == NM_LOBBY_FAILED) break;
            gl_nap(50);
        }
        check(seated2, "the joiner is seated at 2, walking past the computer at 1");
        check(bot_never_taken, "and the computer's seat is never handed to a person");
        check(ready2 && started2, "the room starts once that one person has readied");
        if (started2) {
            const unsigned waiting = nm_waiting_mask();
            check(nm_lobby_setup()->mode[2] == NM_SEAT_HUMAN,
                  "the started setup stamps the joiner's seat HUMAN");
            check((waiting & ((1u << 1) | (1u << 3))) == 0,
                  "the lockstep waits on neither the computer nor the blocked seat");
            printf("  seat room started: modes=%d%d%d%d humans=%d seats=%d waiting=%02X\n",
                   nm_lobby_setup()->mode[0], nm_lobby_setup()->mode[1],
                   nm_lobby_setup()->mode[2], nm_lobby_setup()->mode[3],
                   nm_lobby_setup()->humans, nm_lobby_setup()->seats, waiting);
        }
    }
    {
        int status = 0;
        waitpid(child, &status, 0);
        check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "the joiner reached the started match from seat 2");
    }
    nm_shutdown();

    /* ---- leg 14: A START PRESSED BEFORE THE LINK IS MEASURED IS HELD FOR IT ----
       The lookahead a START carries is chosen from the room's worst round trip, and a
       joiner readies the instant it is seated, long before the host's once-a-second ping
       has come back. A press in that gap used to go out at once on the built-in LAN
       lookahead with nothing measured, which through a relay is a fraction of the cover
       the link needs. This joiner is 300 ms away and its first answer is withheld until
       after the press, so the gap is certain rather than likely. */
    {
        GlFake f[1];
        const unsigned short p = 17470;
        int worst, ahead;
        gl_nap(150);
        check(gl_fake_open(&f[0], p, 1), "14: a hand-written joiner opens a socket");
        check(gl_room(p, 2, f, 1), "14: a room of two is full and ready");
        check(nm_lobby_worst_rtt() < 0, "14: and nothing has been measured when START is pressed");
        check(nm_lobby_start() == 1, "14: START is accepted");
        check(nm_lobby_start_pending() == 1 && nm_lobby_starting() == 0,
              "14: but held, not sent, while the round trip is unmeasured");
        f[0].pong_ms = 300;
        gl_run(f, 1, 120);
        check(f[0].starts == 0 && nm_lobby_start_pending() == 1,
              "14: no START reaches the joiner before its round trip is in");
        check(gl_until_starting(f, 1, 3000), "14: the START is committed once it is");
        worst = nm_lobby_worst_rtt();
        ahead = nm_lobby_setup()->ahead;
        check(worst >= 300 && ahead == nm_ahead_for_rtt(worst) && ahead > 3,
              "14: on the lookahead the measured round trip gives, above the LAN three");
        printf("  leg 14: committed on %d ms, lookahead %d\n", worst, ahead);
        gl_run(f, 1, 300);
        check(f[0].starts > 0 && f[0].first_start[16 + GL_SETUP_AHEAD] == (unsigned char)ahead,
              "14: and the START the joiner received carries that same lookahead");
        {
            const unsigned t0 = gl_now_ms();
            while (nm_lobby_poll() != NM_LOBBY_STARTED && gl_now_ms() - t0 < 2000u)
                gl_run(f, 1, 10);
        }
        check(nm_lobby_state() == NM_LOBBY_STARTED, "14: and the room starts");
        gl_room_close(f, 1);
    }

    /* ---- leg 15: A SEAT THAT NEVER ANSWERS DOES NOT HOLD THE ROOM FOR EVER ----
       and is given the widest cover the band allows, because a dozen pings unanswered is
       a link the LAN number is certain to be wrong for. */
    {
        GlFake f[1];
        const unsigned short p = 17471;
        gl_nap(150);
        check(gl_fake_open(&f[0], p, 1), "15: a joiner that will answer no ping opens");
        check(gl_room(p, 2, f, 1), "15: the room is full and ready");
        check(nm_lobby_start() == 1 && nm_lobby_start_pending() == 1, "15: START is held");
        check(nm_lobby_start() == 1 && nm_lobby_start_pending() == 1 && !nm_lobby_starting(),
              "15: a second press is accepted and starts nothing twice");
        gl_run(f, 1, 2500);
        check(nm_lobby_start_pending() == 1 && !nm_lobby_starting() && f[0].starts == 0,
              "15: still held at 2.5 s, with no START on the wire");
        check(gl_until_starting(f, 1, 1500), "15: committed at the bound all the same");
        check(nm_lobby_setup()->ahead == LS_AHEAD_MAX,
              "15: with the widest lookahead the band allows");
        gl_room_close(f, 1);
    }

    /* ---- leg 16: THE SLOWEST SEAT, NOT THE FIRST ONE TO ANSWER ----
       The worst round trip is a maximum over the seats that have answered, so in a room of
       three it is only a lower bound until both have. */
    {
        GlFake f[2];
        const unsigned short p = 17472;
        int saw_partial = 0;
        gl_nap(150);
        check(gl_fake_open(&f[0], p, 1) && gl_fake_open(&f[1], p, 1),
              "16: two joiners open, one near and one 700 ms away");
        check(gl_room(p, 3, f, 2), "16: a room of three is full and ready");
        f[0].pong_ms = 0;
        f[1].pong_ms = 700;
        nm_lobby_start();
        {
            const unsigned t0 = gl_now_ms();
            while (!nm_lobby_starting() && gl_now_ms() - t0 < 3500u) {
                gl_run(f, 2, 10);
                if (!nm_lobby_starting() && nm_lobby_worst_rtt() >= 0) saw_partial = 1;
            }
        }
        check(saw_partial, "16: there was a moment with the near seat measured and START still held");
        check(nm_lobby_starting() && nm_lobby_worst_rtt() >= 700
              && nm_lobby_setup()->ahead == nm_ahead_for_rtt(nm_lobby_worst_rtt()),
              "16: and the room's lookahead is the far seat's");
        gl_room_close(f, 2);
    }

    /* ---- leg 17: A HELD START IS DROPPED WHEN THE ROOM CHANGES UNDER IT ----
       The host agreed to start the room that was there. A seat that walks out, a READY
       that goes out and a changed map each make it a different room, and none of them may
       leave a press behind that fires later on its own. An identical push of the setup,
       which the lobby screen makes on every frame that changed anything, is not a change. */
    {
        GlFake f[1];
        unsigned short p = 17473;
        gl_nap(150);
        check(gl_fake_open(&f[0], p, 1) && gl_room(p, 2, f, 1), "17: a ready room for the goodbye");
        nm_lobby_start();
        gl_run(f, 1, 300);
        gl_fake_bye(&f[0]);
        gl_run(f, 1, 300);
        check(!nm_lobby_start_pending() && !nm_lobby_starting(),
              "17: a goodbye drops the held start");
        gl_run(f, 1, 3000);
        check(!nm_lobby_starting(), "17: and nothing starts once the bound has passed either");
        gl_room_close(f, 1);

        p = 17474;
        gl_nap(150);
        check(gl_fake_open(&f[0], p, 1) && gl_room(p, 2, f, 1), "17: a ready room for the unready");
        nm_lobby_start();
        gl_run(f, 1, 300);
        gl_fake_tick(&f[0], 0);
        gl_run(f, 1, 300);
        check(!nm_lobby_start_pending() && !nm_lobby_starting(),
              "17: a READY going out drops the held start");
        gl_fake_tick(&f[0], 1);
        gl_run(f, 1, 3000);
        check(nm_lobby_all_ready() && !nm_lobby_starting(),
              "17: and readying again does not bring the old press back");
        gl_room_close(f, 1);

        p = 17475;
        gl_nap(150);
        check(gl_fake_open(&f[0], p, 1) && gl_room(p, 2, f, 1), "17: a ready room for the map change");
        nm_lobby_start();
        {
            NmSetup same = *nm_lobby_setup();
            nm_lobby_set_setup(&same);
        }
        check(nm_lobby_start_pending() == 1, "17: an identical push of the setup leaves it held");
        {
            NmSetup changed = *nm_lobby_setup();
            snprintf(changed.scenario, sizeof changed.scenario, "%s", "SCG01EA");
            nm_lobby_set_setup(&changed);
        }
        check(!nm_lobby_start_pending() && !nm_lobby_starting(),
              "17: a changed map drops it");
        gl_room_close(f, 1);
    }

    /* ---- leg 18: NOBODY IS SEATED WHILE A PRESSED START IS BEING MEASURED ----
       Pressing START is what closes a room. While the press was a single instant it closed
       the empty seats on the spot; now that it waits for the round trips, the seats close
       when it commits, and a stranger knocking in the wait took a chair and cancelled the
       host's press. A listed room can be knocked on again and again. */
    {
        GlFake f[2];
        const unsigned short p = 17476;
        int held_throughout = 1;
        gl_nap(150);
        check(gl_fake_open(&f[0], p, 1) && gl_fake_open(&f[1], p, 1),
              "18: a joiner and a stranger open");
        check(gl_room(p, 3, f, 1), "18: a room of three with two in it is ready");
        nm_lobby_start();
        f[0].pong_ms = 400;
        check(nm_lobby_start_pending() == 1, "18: START is held for the round trip");
        {
            const unsigned t0 = gl_now_ms();
            unsigned last_knock = 0;
            while (!nm_lobby_starting() && gl_now_ms() - t0 < 3500u) {
                if (last_knock == 0 || gl_now_ms() - last_knock > 100u) {
                    gl_fake_hello(&f[1]);
                    last_knock = gl_now_ms();
                }
                gl_run(f, 2, 10);
                if (!nm_lobby_starting() && !nm_lobby_start_pending()) held_throughout = 0;
            }
        }
        check(f[1].seat < 0 && nm_lobby_filled() == 2,
              "18: the stranger knocking during the wait is not seated");
        check(held_throughout && nm_lobby_starting(),
              "18: and the host's press survives the knocking and commits");
        gl_fake_hello(&f[1]);
        gl_run(f, 2, 300);
        check(f[1].seat < 0 && nm_lobby_seat_mode(2) == NM_SEAT_BLOCK,
              "18: after which the empty seat is closed, as a press always closed it");
        gl_room_close(f, 2);
    }

    /* ---- leg 19: NOTHING EDITS THE ROOM ONCE THE START IS ON THE WIRE ----
       START is re-sent to every seat that has not acknowledged it, packed from the room as
       it is at that moment. A joiner asking for a new colour between two copies used to be
       granted, so the seat that acknowledged the first copy and the seat that acknowledged
       a later one booted different rooms and parted on the first frame. */
    {
        GlFake f[2];
        const unsigned short p = 17477;
        gl_nap(150);
        check(gl_fake_open(&f[0], p, 1) && gl_fake_open(&f[1], p, 0),
              "19: one joiner that acknowledges at once and one that is slow to");
        check(gl_room(p, 3, f, 2), "19: a room of three is full and ready");
        f[0].pong_ms = 0;
        f[1].pong_ms = 0;
        nm_lobby_start();
        {
            const unsigned t0 = gl_now_ms();
            while ((f[0].starts == 0 || f[1].starts == 0) && gl_now_ms() - t0 < 3000u)
                gl_run(f, 2, 10);
        }
        check(f[0].starts > 0 && f[1].starts > 0, "19: both joiners have the START");
        gl_fake_seatpref(&f[1], 6);
        gl_run(f, 2, 600);
        check(f[1].starts >= 2, "19: the slow seat was sent the START again after asking");
        gl_fake_sack(&f[1]);
        {
            const unsigned t0 = gl_now_ms();
            while (nm_lobby_poll() != NM_LOBBY_STARTED && gl_now_ms() - t0 < 2000u)
                gl_run(f, 2, 10);
        }
        check(nm_lobby_state() == NM_LOBBY_STARTED, "19: the room starts");
        check(memcmp(f[1].first_start, f[1].last_start, GL_START_BYTES) == 0,
              "19: every copy of the START the slow seat received describes the same room");
        check(memcmp(f[0].first_start + 16, f[1].last_start + 16, GL_SETUP_BYTES) == 0,
              "19: and it is the room the quick seat booted");
        check(f[1].seat > 0 && f[1].seat < NM_MAX_SEATS
              && f[1].last_start[16 + GL_SETUP_COLOUR + f[1].seat] != 6
              && nm_lobby_setup()->colour[f[1].seat] != 6,
              "19: the colour asked for after START was not applied, on the wire or on the host");
        gl_room_close(f, 2);
    }

    /* ---- leg 20: A START THAT REACHED ANYONE CANNOT BE TAKEN BACK ----
       A joiner that receives START is in the match on the roster it carried. When the
       start then cannot finish, the room used to go back to waiting as though nothing had
       gone out: the seat that had acknowledged still counted as acknowledged, so the next
       press started the host at once on a different room and sent that seat nothing. */
    {
        GlFake f[2];
        unsigned short p = 17478;
        gl_nap(150);
        check(gl_fake_open(&f[0], p, 1) && gl_fake_open(&f[1], p, 0),
              "20: one joiner that acknowledges and one that will leave instead");
        check(gl_room(p, 3, f, 2), "20: a room of three is full and ready");
        f[0].pong_ms = 0;
        f[1].pong_ms = 0;
        nm_lobby_start();
        {
            const unsigned t0 = gl_now_ms();
            while ((f[0].starts == 0 || f[1].starts == 0) && gl_now_ms() - t0 < 3000u)
                gl_run(f, 2, 10);
        }
        gl_run(f, 2, 200);
        check(f[0].starts > 0 && f[1].starts > 0 && nm_lobby_starting(),
              "20: the START is out and the quick seat has acknowledged it");
        gl_fake_bye(&f[1]);
        gl_run(f, 2, 300);
        check(nm_lobby_state() == NM_LOBBY_FAILED,
              "20: the other seat leaving before it answered FAILS the room");
        check(strstr(nm_lobby_error(), "could not start") != NULL,
              "20: and says why in words");
        printf("  leg 20: %s\n", nm_lobby_error());
        check(f[0].host_bye, "20: the seat already in the match is told the host has gone");
        check(nm_lobby_start() == 0, "20: and the failed room refuses another START");
        gl_room_close(f, 2);

        /* THE OTHER HALF: with nobody left seated who can have the START, the room is
           simply the room again, and the next start is a whole new one. */
        p = 17479;
        gl_nap(150);
        check(gl_fake_open(&f[0], p, 0) && gl_fake_open(&f[1], p, 1),
              "20: a joiner that leaves before answering, and one who arrives later");
        check(gl_room(p, 2, f, 1), "20: a room of two is full and ready");
        f[0].pong_ms = 0;
        nm_lobby_start();
        {
            const unsigned t0 = gl_now_ms();
            while (f[0].starts == 0 && gl_now_ms() - t0 < 3000u) gl_run(f, 1, 10);
        }
        gl_fake_bye(&f[0]);
        gl_run(f, 1, 300);
        check(nm_lobby_state() == NM_LOBBY_WAITING && !nm_lobby_starting(),
              "20: the only joiner leaving mid-start puts the room back to waiting");
        f[1].pong_ms = 0;
        {
            const unsigned t0 = gl_now_ms();
            while (f[1].seat < 0 && gl_now_ms() - t0 < 2000u) {
                gl_fake_hello(&f[1]);
                gl_run(f, 2, 60);
            }
        }
        gl_fake_tick(&f[1], 1);
        gl_run(f, 2, 200);
        check(nm_lobby_start() == 1, "20: a newcomer takes the seat and readies");
        {
            const unsigned t0 = gl_now_ms();
            while (nm_lobby_poll() != NM_LOBBY_STARTED && gl_now_ms() - t0 < 3000u)
                gl_run(f, 2, 10);
        }
        check(nm_lobby_state() == NM_LOBBY_STARTED && f[1].starts > 0,
              "20: and the next start waits for, and reaches, the newcomer");
        gl_room_close(f, 2);

        /* AND A SEAT REMOVED MID-START is told the host has gone as well as that it was
           removed, because if the START reached it, it is in a match that ignores a KICK. */
        p = 17480;
        gl_nap(150);
        check(gl_fake_open(&f[0], p, 0) && gl_room(p, 2, f, 1),
              "20: a ready room for a removal while the START is out");
        f[0].pong_ms = 0;
        nm_lobby_start();
        {
            const unsigned t0 = gl_now_ms();
            while (f[0].starts == 0 && gl_now_ms() - t0 < 3000u) gl_run(f, 1, 10);
        }
        check(f[0].starts > 0 && nm_lobby_kick(f[0].seat) == 1, "20: the host removes the seat");
        gl_run(f, 1, 300);
        check(f[0].host_bye && nm_lobby_state() == NM_LOBBY_WAITING && !nm_lobby_starting(),
              "20: which gets a goodbye from the host, and the room waits again");
        gl_room_close(f, 1);
    }

    /* ---- leg 21: A LOST READY DOES NOT SHUT THE ROOM ----
       The ready tick was sent once and answered by nothing, so a single dropped datagram
       left the host holding a seat that was not ready while the joiner's own screen said
       it was. START was then refused for ever, with nothing on either screen to say why,
       and the only way out was for the player to guess and toggle READY off and on again.
       A relayed pair failed to start exactly this way: the joiner logged its ready and the
       host never saw one.

       BOTH ENDS ARE REAL HERE and the loss is in the wire between them, which is the only
       arrangement that can tell the fix from a harness that presses twice: the child
       presses READY once and never again. */
    {
        GlWire w;
        const unsigned short p = 17481, wp = 17482;
        int started21 = 0, saw_ready21 = 0, status = 0;
        gl_nap(150);
        fill_setup(&setup, "SCM01EA");
        check(nm_lobby_host(p, &setup, 0xABCDEF01u, 0x12345678u, 2, "LOSSY", NULL) == 1,
              "21: a host opens a room");
        check(gl_wire_open(&w, wp, p, GL_TICK, 1) == 1,
              "21: and a wire that will swallow the first ready tick sits in front of it");
        child = fork();
        if (child == 0) {
            char ps[16];
            snprintf(ps, sizeof ps, "%u", (unsigned)wp);
            gl_wire_close(&w);
            execl(argv[0], argv[0], "--joiner", ps, "", (char *)NULL);
            _exit(127);
        }
        for (i = 0; i < 1200; i++) {          /* up to 12 s */
            const int st = nm_lobby_poll();
            gl_wire_pump(&w);
            if (nm_lobby_seat_ready(1)) saw_ready21 = 1;
            if (nm_lobby_all_ready()) nm_lobby_start();
            if (st == NM_LOBBY_STARTED) { started21 = 1; break; }
            if (st == NM_LOBBY_REFUSED || st == NM_LOBBY_FAILED) break;
            gl_nap(10);
        }
        check(w.swallowed == 1, "21: the joiner's first ready tick never reached the host");
        check(saw_ready21, "21: the host learns the seat is ready anyway");
        check(w.passed >= 1, "21: because the tick was re-sent, not pressed again");
        /* AND THE REPEAT STOPS. A joiner that says it for ever would be a lobby that
           talks over itself once the room is quiet, which is the other way to get this
           wrong: one gets through, the host echoes it, and that is the end of it. */
        check(w.passed <= 2, "21: and it stops the moment the host echoes it back");
        check(started21, "21: and the room starts");
        printf("  leg 21: swallowed=%d passed=%d\n", w.swallowed, w.passed);
        nm_shutdown();
        nm_lobby_cancel();
        /* The goodbye has to get through the wire for the child to stop waiting. */
        for (i = 0; i < 40; i++) { gl_wire_pump(&w); gl_nap(10); }
        waitpid(child, &status, 0);
        check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "21: and the joiner reached the same started match");
        gl_wire_close(&w);
        gl_nap(100);
    }

    /* ---- leg 22: THE TURN REQUEST ON THE WIRE, AND WHO THE HOST SENDS IT TO ----
       A peer that has been deaf longer than the redundancy window asks for the turn it is
       stuck on by number, and whoever holds that turn answers with it. The scheduler gate
       proves the repair works; this is the only thing that drives the MESSAGE: the seat
       check, the answer, the rate it may be answered at, and the host's forward to the
       other joiners the question names.

       IT WAS THE FORWARD THAT WAS NEVER RUN AT ALL. The forward exists for a joiner asking
       for another joiner's turn, which cannot happen in a room of two, and every run this
       had ever had was a room of two: the mask always named seat 0 and the loop that
       starts at seat 1 never matched. It also used to repeat the datagram at whatever
       length it arrived, while reading nothing past the sixteenth byte, and to answer as
       often as it was asked however often that was. */
    {
        GlFake f[2];
        const unsigned short p = 17483;
        unsigned char need[600];
        int i, seat_a, seat_b, started22 = 0;
        gl_nap(150);
        check(gl_fake_open(&f[0], p, 1) && gl_fake_open(&f[1], p, 1),
              "22: two joiners open");
        check(gl_room(p, 3, f, 2), "22: a room of three is full and ready");
        f[0].pong_ms = 0;
        f[1].pong_ms = 0;
        nm_lobby_start();
        check(gl_until_starting(f, 2, 4000), "22: the START goes out");
        for (i = 0; i < 400 && !started22; i++) {
            if (nm_lobby_poll() == NM_LOBBY_STARTED) started22 = 1;
            gl_fake_pump(&f[0]);
            gl_fake_pump(&f[1]);
            gl_nap(5);
        }
        check(started22, "22: and the match begins");
        seat_a = f[0].seat;
        seat_b = f[1].seat;
        check(seat_a == 1 && seat_b == 2, "22: the two joiners hold seats 1 and 2");

        /* The host stamps a few turns, so it has something to answer with. */
        for (i = 0; i < 6; i++) {
            nm_begin_turn();
            nm_service();
            gl_fake_pump(&f[0]);
            gl_fake_pump(&f[1]);
            gl_nap(5);
        }

        /* ONE QUESTION, ASKED TWENTY TIMES. The asker's own beat allows one every hundred
           milliseconds; nothing on this side enforced that, so twenty questions bought
           twenty answers of up to a kilobyte each, and twenty forwards. */
        gl_nap(150);
        f[0].turn_pkts = 0;
        f[1].needs = 0;
        f[1].need_len = 0;
        memset(need, 0xEE, sizeof need);
        gl_put(need, GL_NEED);
        gl_put(need + 4, (unsigned)seat_a);
        gl_put(need + 8, 1u);
        /* waiting on the host and on the other joiner, which is the case the forward is
           the only route for */
        gl_put(need + 12, 1u | (1u << (unsigned)seat_b));
        for (i = 0; i < 20; i++) gl_fake_send(&f[0], need, 16);
        for (i = 0; i < 12; i++) {
            nm_service();
            gl_fake_pump(&f[0]);
            gl_fake_pump(&f[1]);
            gl_nap(5);
        }
        check(f[0].turn_pkts == 1, "22: the host answers the asking seat, once");
        check(f[1].needs == 1, "22: and passes the question to the other seat it names, once");
        check(f[1].need_len == 16, "22: as sixteen bytes, which is the length of a question");

        /* A LONG ONE IS STILL FORWARDED SHORT. The handler admits anything at or above
           sixteen and reads nothing past it, so re-emitting the datagram as it arrived let
           one seat make the host repeat a kilobyte to every other joiner. */
        gl_nap(150);
        f[1].needs = 0;
        f[1].need_len = 0;
        gl_fake_send(&f[0], need, (int)sizeof need);
        for (i = 0; i < 12; i++) {
            nm_service();
            gl_fake_pump(&f[0]);
            gl_fake_pump(&f[1]);
            gl_nap(5);
        }
        check(f[1].needs == 1 && f[1].need_len == 16,
              "22: a six hundred byte question is forwarded as sixteen bytes");

        /* SPEAKING AS SOMEBODY ELSE BUYS NOTHING, as for a goodbye or a surrender. */
        gl_nap(150);
        f[0].turn_pkts = 0;
        f[1].needs = 0;
        gl_put(need + 4, (unsigned)seat_b);          /* seat 1's socket, seat 2's name */
        gl_fake_send(&f[0], need, 16);
        for (i = 0; i < 12; i++) {
            nm_service();
            gl_fake_pump(&f[0]);
            gl_fake_pump(&f[1]);
            gl_nap(5);
        }
        check(f[0].turn_pkts == 0 && f[1].needs == 0,
              "22: a question signed with another seat's number is ignored");

        /* AND A MASK NAMING SEATS THIS MATCH HAS NOT GOT IS CUT DOWN TO NOTHING. Every bit
           set in it costs an answer and, in a star, a relay of that answer to everybody
           else, so it is the one field here that multiplies and the one that is trimmed. */
        gl_nap(150);
        f[0].turn_pkts = 0;
        f[1].needs = 0;
        gl_put(need + 4, (unsigned)seat_a);
        gl_put(need + 12, 0xFFFFFF80u);              /* seats 7 and up: none of them exist */
        gl_fake_send(&f[0], need, 16);
        for (i = 0; i < 12; i++) {
            nm_service();
            gl_fake_pump(&f[0]);
            gl_fake_pump(&f[1]);
            gl_nap(5);
        }
        check(f[0].turn_pkts == 0 && f[1].needs == 0,
              "22: a question naming only seats that are not in the match is dropped");

        gl_room_close(f, 2);
    }

    /* ---- leg 23: A PROBE IS ANSWERED FROM ANYBODY, AND SEATS NOBODY ----
       A browsing player has not joined, has no seat and may not know the passcode, and the
       game list's PING column is the round trip to exactly that host. So a probe is
       answered ahead of every check the lobby makes. Because it is answered for anybody,
       what a stranger can make a host do with it is the thing proved here: an answer no
       larger than the question, a limit on how often one sender is answered, and a log
       that says so without a line per probe. Then the browser's own prober, which is the
       code the game list calls, against the same room, a host that does not know the word
       and a port with nobody on it. */
    {
        const unsigned short p = 17487, oldp = 17488, deadp = 17489;
        GlFake s1, s2;
        GlProbeSeen a, b;
        NmSetup ps;
        NmProbeResult r, ro, rd;
        NetSock *old = NULL;
        char key[40], okey[40], dkey[40];
        unsigned arrive[16];
        unsigned char in[1024];
        NetAddr from;
        int i, n, narrive = 0, spaced = 1, lines = 0, ignored = 0, limited_lines = 0;
        unsigned t0, el;

        gl_nap(150);
        fill_setup(&ps, "SCM01EA");
        check(nm_lobby_host(p, &ps, 0xABCDEF01u, 0x12345678u, 2, "PROBED", "4321") == 1
              && nm_lobby_locked(), "23: a host opens a room that carries a passcode");
        check(gl_fake_open(&s1, p, 0) && gl_fake_open(&s2, p, 0),
              "23: two strangers open sockets");

        memset(&a, 0, sizeof a);
        gl_probe_send(&s1, 0x11223344u, 0x55667788u, 16);
        gl_probe_run(&s1, &a, NULL, NULL, 150);
        check(a.acks == 1, "23: a stranger's probe is answered: no HELLO, no seat, no passcode");
        check(a.last_len == 16, "23: in sixteen bytes");
        check(a.last_nonce == 0x11223344u && a.last_stamp == 0x55667788u,
              "23: carrying the asker's nonce and stamp back untouched");
        check(a.last_word == (unsigned)GL_VERSION,
              "23: with the host's wire version in the last word");
        check(a.other == 0, "23: and nothing else: no WELCOME, no REFUSE");
        check(nm_lobby_filled() == 1, "23: the stranger holds no seat");

        memset(&a, 0, sizeof a);
        gl_probe_send(&s1, 0x0A01u, 0x0B01u, 600);
        gl_probe_run(&s1, &a, NULL, NULL, 150);
        check(a.acks == 1 && a.last_len == 16,
              "23: a 600 byte probe is answered in sixteen bytes, never at its own length");
        memset(&a, 0, sizeof a);
        gl_probe_send(&s1, 0x0A02u, 0x0B02u, 12);
        gl_probe_send(&s1, 0x0A03u, 0x0B03u, 8);
        gl_probe_run(&s1, &a, NULL, NULL, 150);
        check(a.acks == 0, "23: a probe shorter than sixteen bytes is not answered at all");

        /* ONE SENDER, FORTY PROBES INSIDE ONE SECOND. Its earlier second is let run out
           first, so what is counted is one whole second's answers. */
        gl_probe_run(&s1, &a, NULL, NULL, 1100);
        memset(&a, 0, sizeof a);
        memset(&b, 0, sizeof b);
        for (i = 0; i < 40; i++) gl_probe_send(&s1, 0x1000u + (unsigned)i, (unsigned)i, 16);
        gl_probe_run(&s1, &a, NULL, NULL, 200);
        check(a.acks == 8, "23: forty probes inside one second from one sender draw eight answers");
        gl_probe_send(&s2, 0x2000u, 1u, 16);
        gl_probe_run(&s1, &a, &s2, &b, 150);
        check(a.acks == 8 && b.acks == 0,
              "23: and a second socket on the same address, inside that same second, shares those "
              "eight: a sender is its address, not its port (leg 24 shows a different sender is answered)");
        {
            NetAddr x, y, z, t1, t2;
            check(net_resolve("127.0.0.1", 17000, &x) == 0 && net_resolve("127.0.0.1", 17001, &y) == 0
                  && net_resolve("127.0.0.2", 17000, &z) == 0,
                  "23: three numeric addresses resolve");
            net_addr_tunnel(&t1, 0x1234ul);
            net_addr_tunnel(&t2, 0x1235ul);
            check(net_addr_same_host(&x, &y) && !net_addr_equal(&x, &y),
                  "23: one IP on two ports is one sender and two peers");
            check(!net_addr_same_host(&x, &z), "23: two IPs are two senders");
            check(net_addr_same_host(&t1, &t1) && !net_addr_same_host(&t1, &t2)
                  && !net_addr_same_host(&t1, &x),
                  "23: and through a relay a sender is its tunnel id");
        }
        gl_probe_run(&s1, &a, &s2, &b, 1000);
        memset(&a, 0, sizeof a);
        gl_probe_send(&s1, 0x2100u, 2u, 16);
        gl_probe_run(&s1, &a, NULL, NULL, 150);
        check(a.acks == 1, "23: and the first sender is answered again once its second is up");

        /* THE LOG UNDER A FLOOD: two senders at fifty probes a second each, for a little
           over three seconds. The host prints to this process's own stdout, so stdout is
           pointed at a file for the length of it and read back. Whatever line the probes
           above were owed is let out first. */
        gl_probe_run(&s1, &a, &s2, &b, 1100);
        {
            FILE *cap = tmpfile();
            int saved = -1;
            char line[256];
            fflush(stdout);
            if (cap) {
                saved = dup(1);
                if (saved >= 0) dup2(fileno(cap), 1);
            }
            t0 = gl_now_ms();
            while (gl_now_ms() - t0 < 3200u) {
                for (i = 0; i < 5; i++) {
                    gl_probe_send(&s1, 0x3000u + (unsigned)i, (unsigned)i, 16);
                    gl_probe_send(&s2, 0x4000u + (unsigned)i, (unsigned)i, 16);
                }
                gl_probe_run(&s1, &a, &s2, &b, 100);
            }
            fflush(stdout);
            if (saved >= 0) {
                dup2(saved, 1);
                close(saved);
            }
            if (cap) {
                rewind(cap);
                while (fgets(line, sizeof line, cap)) {
                    if (!strncmp(line, "NET|probes|", 11)) {
                        lines++;
                        if (!strstr(line, "|limited=0|")) limited_lines++;
                        printf("  leg 23 flood: %s", line);
                    } else if (!strncmp(line, "NET|ignored|", 12)) {
                        ignored++;
                    }
                }
                fclose(cap);
            }
            check(cap != NULL && saved >= 0, "23: the host's log is read back after the flood");
            check(lines >= 2 && lines <= 3,
                  "23: 3.2 s of flood wrote one line a second, not one line a probe");
            check(limited_lines == lines, "23: and every one of those lines counts refusals");
            check(ignored == 0, "23: and no probe is written off as an unrecognised datagram");
        }

        /* THE BROWSER'S OWN PROBER, against the same room. */
        gl_probe_run(&s1, &a, &s2, &b, 1100);
        snprintf(key, sizeof key, "127.0.0.1:%u", (unsigned)p);
        t0 = gl_now_ms();
        check(nm_probe_start(key) == 1, "23: the browser starts probing the room");
        while (nm_probe_poll(key, &r) == NM_PROBE_PROBING && gl_now_ms() - t0 < 4000u) {
            nm_lobby_poll();
            gl_nap(2);
        }
        el = gl_now_ms() - t0;
        printf("  leg 23: browser probe of %s: state=%d rtt=%dms answers=%d/%d in %u ms\n",
               key, r.state, r.rtt_ms, r.answers, r.sent, el);
        check(r.state == NM_PROBE_ANSWERED && r.answers == NM_PROBE_COUNT
              && r.sent == NM_PROBE_COUNT, "23: every probe the browser sent is answered");
        check(r.rtt_ms >= 0 && r.rtt_ms < 100,
              "23: and the minimum round trip on loopback is under 100 ms");
        check(el >= (unsigned)NM_PROBE_SPAN_MS,
              "23: the probes went out spaced, not in one burst");
        check(r.version == (unsigned)GL_VERSION, "23: the result names the host's wire version");
        check(nm_lobby_filled() == 1, "23: and probing the room seated nobody in it");
        check(nm_probe_start(key) == 1 && nm_probe_poll(key, &r) == NM_PROBE_ANSWERED
              && r.sent == NM_PROBE_COUNT,
              "23: starting a key that already has its answer does not probe it again");

        /* A HOST THAT DOES NOT KNOW THE WORD, which is every host built before it: a socket
           that reads the probes and answers none, which is all that dispatch does with
           them. And a port with nobody on it at all. Both must read as silence, and only
           once the whole schedule has run. */
        old = net_open(oldp);
        check(old != NULL, "23: a host that does not know the word opens its port");
        snprintf(okey, sizeof okey, "127.0.0.1:%u", (unsigned)oldp);
        snprintf(dkey, sizeof dkey, "127.0.0.1:%u", (unsigned)deadp);
        t0 = gl_now_ms();
        check(nm_probe_start(okey) == 1 && nm_probe_start(dkey) == 1,
              "23: the browser probes both");
        for (;;) {
            const int so = nm_probe_poll(okey, &ro);
            const int sd = nm_probe_poll(dkey, &rd);
            while (old && (n = net_recv(old, &from, in, (int)sizeof in)) > 0) {
                if (n >= 16 && gl_get(in) == GL_PROBE && narrive < 16)
                    arrive[narrive++] = gl_now_ms();
            }
            if ((so != NM_PROBE_PROBING && sd != NM_PROBE_PROBING)
                || gl_now_ms() - t0 > 5000u) break;
            gl_nap(2);
        }
        el = gl_now_ms() - t0;
        for (i = 1; i < narrive; i++) {
            const unsigned gap = arrive[i] - arrive[i - 1];
            const unsigned want = (unsigned)NM_PROBE_GAP_MS(i - 1);
            if (gap + 10u < want || gap > want + 100u) spaced = 0;
        }
        printf("  leg 23: silent host state=%d sent=%d answers=%d arrivals=%d; empty port "
               "state=%d; both settled in %u ms\n",
               ro.state, ro.sent, ro.answers, narrive, rd.state, el);
        check(narrive == NM_PROBE_COUNT, "23: the silent host receives exactly four probes");
        check(spaced, "23: each one at its own gap after the last: 175, 180 and 190 ms");
        check(ro.state == NM_PROBE_SILENT && ro.answers == 0 && ro.sent == NM_PROBE_COUNT,
              "23: a host that does not know the word reads as no answer");
        check(rd.state == NM_PROBE_SILENT && rd.answers == 0,
              "23: and so does a port with nobody on it");
        check(el >= (unsigned)(NM_PROBE_SPAN_MS + NM_PROBE_WAIT_MS)
              && el < (unsigned)(NM_PROBE_SPAN_MS + NM_PROBE_WAIT_MS + 600),
              "23: once the wait after the last probe is up, not before it and not long after");

        check(nm_probe_start("localhost:17421") == 0
              && nm_probe_poll("localhost:17421", &r) == NM_PROBE_INVALID,
              "23: a host NAME is refused as a key rather than looked up");
        check(nm_probe_start("#K7M-3Q") == 0 && nm_probe_poll("#K7M-3Q", &r) == NM_PROBE_INVALID,
              "23: so is a room code one symbol short");
        check(nm_probe_start("127.0.0.1") == 0 && nm_probe_start("127.0.0.1:0") == 0
              && nm_probe_start("300.1.1.1:17421") == 0,
              "23: and an address with no port, with port 0, or with an octet past 255");

        /* A CALLER THAT READS THE PROBER ONCE A FRAME. An answer on loopback lands a moment
           after its probe leaves and then sits in the socket until the next poll, so timed
           at that poll every probe reads the gap between polls, and the least of four equal
           gaps is the gap. Timed at its arrival it reads the round trip. The host here is
           polled every millisecond, and the prober only every sixteen. */
        {
            unsigned lastp;
            int st = NM_PROBE_PROBING, stamps = 0;
            NetSock *probe_check = net_open(0);
            if (probe_check) {
                stamps = net_set_rx_stamps(probe_check, 1);
                net_close(probe_check);
            }
            check(stamps == 1, "23: this system stamps a datagram's arrival");
            nm_probe_clear_all();
            check(nm_probe_start(key) == 1,
                  "23: the browser probes the room again, polled once every 16 ms");
            t0 = lastp = gl_now_ms();
            while (st == NM_PROBE_PROBING && gl_now_ms() - t0 < 4000u) {
                nm_lobby_poll();
                if (gl_now_ms() - lastp >= 16u) {
                    st = nm_probe_poll(key, &r);
                    lastp = gl_now_ms();
                }
                gl_nap(1);
            }
            printf("  leg 23: prober polled every 16 ms: state=%d rtt=%dms answers=%d/%d\n",
                   r.state, r.rtt_ms, r.answers, r.sent);
            check(r.state == NM_PROBE_ANSWERED && r.answers == NM_PROBE_COUNT,
                  "23: and every probe is answered");
            check(r.rtt_ms >= 0 && r.rtt_ms <= 5,
                  "23: reading the loopback round trip, not the 16 ms between its polls");
        }

        nm_probe_cancel(okey);
        check(nm_probe_poll(okey, &r) == NM_PROBE_UNKNOWN, "23: a cancelled key is forgotten");
        check(nm_probe_start(okey) == 1, "23: and can be probed afresh");
        t0 = gl_now_ms();
        while (gl_now_ms() - t0 < 200u) {
            nm_probe_pump();
            while (old && net_recv(old, &from, in, (int)sizeof in) > 0) { /* in flight */ }
            gl_nap(2);
        }
        nm_probe_cancel(okey);
        gl_nap(20);
        while (old && net_recv(old, &from, in, (int)sizeof in) > 0) { /* sent before it */ }
        narrive = 0;
        t0 = gl_now_ms();
        while (gl_now_ms() - t0 < 500u) {
            nm_probe_pump();
            while (old && (n = net_recv(old, &from, in, (int)sizeof in)) > 0)
                if (n >= 16 && gl_get(in) == GL_PROBE) narrive++;
            gl_nap(2);
        }
        check(narrive == 0, "23: a cancelled probe sends nothing more");
        nm_probe_clear_all();
        check(nm_probe_poll(key, &r) == NM_PROBE_UNKNOWN
              && nm_probe_poll(dkey, &r) == NM_PROBE_UNKNOWN,
              "23: and clearing forgets every key");

        nm_probe_shutdown();
        if (old) net_close(old);
        gl_fake_close(&s1);
        gl_fake_close(&s2);
        nm_lobby_cancel();
    }

    /* ---- leg 24: A RELAYED ROOM IS PROBED THROUGH THE RELAY, WITHOUT JOINING IT ----
       A browsing player addresses a room by its code, which is the host's tunnel id, and
       a relay forwards only to ids it has already seen, because the answer is addressed to
       the asker. So the asker registers an id of its own, and every relayed room it asks
       about shares that ONE registration: each registration is one of the eight clients a
       public relay allows a single address, and a join made later from the same address
       needs one of them too. */
    {
        const unsigned short rp = 17491;
        const unsigned long room_id = 0x01234567ul & RC_HOST_ID_MAX;
        const unsigned long ghost_id = 0x00765432ul & RC_HOST_ID_MAX;
        GlRelay rel;
        NmSetup rs;
        NmProbeResult r, rg;
        char code[RC_TEXT_MAX], ghost[RC_TEXT_MAX];
        unsigned t0;
        int opened;

        gl_nap(150);
        memset(&rel, 0, sizeof rel);
        rel.sock = net_open(rp);
        check(rel.sock != NULL, "24: a stand-in relay opens");
        fill_setup(&rs, "SCM01EA");
        nm_relay_next("127.0.0.1", rp, room_id, 0ul);
        opened = nm_lobby_host(0, &rs, 0xABCDEF01u, 0x12345678u, 2, "RELAYED", "4321");
        check(opened == 1 && nm_is_relayed(),
              "24: a relayed room with a passcode opens through it");
        check(rc_encode(room_id, code) && rc_encode(ghost_id, ghost),
              "24: the room's id and a second id both encode as room codes");
        t0 = gl_now_ms();
        while (gl_now_ms() - t0 < 200u) {
            gl_relay_pump(&rel);
            nm_lobby_poll();
            gl_nap(2);
        }
        nm_probe_set_relay("127.0.0.1", rp);
        check(nm_probe_start(code) == 1 && nm_probe_start(ghost) == 1,
              "24: the browser probes the room's code and a code nobody holds");
        t0 = gl_now_ms();
        for (;;) {
            int sr, sg;
            gl_relay_pump(&rel);
            nm_lobby_poll();
            gl_relay_pump(&rel);
            sr = nm_probe_poll(code, &r);
            sg = nm_probe_poll(ghost, &rg);
            if ((sr != NM_PROBE_PROBING && sg != NM_PROBE_PROBING)
                || gl_now_ms() - t0 > 5000u) break;
            gl_nap(2);
        }
        printf("  leg 24: %s state=%d rtt=%dms answers=%d/%d; %s state=%d sent=%d; the relay "
               "saw %d ids and %d datagrams for nobody\n", code, r.state, r.rtt_ms,
               r.answers, r.sent, ghost, rg.state, rg.sent, rel.n, rel.nowhere);
        check(r.state == NM_PROBE_ANSWERED && r.answers == NM_PROBE_COUNT,
              "24: the relayed room answers every probe");
        check(nm_lobby_filled() == 1, "24: and seats nobody");
        check(rg.state == NM_PROBE_SILENT && rg.answers == 0 && rg.sent == NM_PROBE_COUNT,
              "24: a room code nobody holds reads as no answer");
        check(rel.nowhere >= NM_PROBE_COUNT,
              "24: because the relay had nowhere to send those probes");
        check(rel.n == 2,
              "24: and both rooms were asked through ONE registration beside the host's own");

        /* A SENDER THROUGH A RELAY IS ITS TUNNEL ID, AND A FULL TABLE OF SENDERS MAKES ROOM
           RATHER THAN REFUSING. Twenty relay ids stand in for twenty machines, which one
           loopback address cannot: to the room every one is a different sender. They keep
           the room's table of senders full, one probe a second each, while the browser's
           prober asks the same room. */
        {
            enum { NFK = 20 };
            NetSock *fake = net_open(0);
            NetAddr relay_at;
            int got[NFK], sent[NFK], k, every = 1, started = 0, sp = NM_PROBE_PROBING, ids0;
            unsigned next[NFK];
            char alias[RC_TEXT_MAX];
            NmProbeResult ra, rb;
            const unsigned room = (unsigned)room_id;

            for (k = 0; k < NFK; k++) { got[k] = 0; sent[k] = 0; }
            ids0 = rel.n;
            check(fake != NULL && net_resolve("127.0.0.1", rp, &relay_at) == 0,
                  "24: one plain socket opens to speak for twenty relay ids");

            for (k = 0; k < 40; k++) gl_id_probe(fake, &relay_at, GL_FAKE_BASE, room, 0x5000u + (unsigned)k);
            gl_fk_pump(&rel, fake, got, NFK, 200);
            gl_id_probe(fake, &relay_at, GL_FAKE_BASE + 1u, room, 0x5100u);
            gl_fk_pump(&rel, fake, got, NFK, 150);
            printf("  leg 24: forty probes from one id drew %d answers; one from another id drew %d\n",
                   got[0], got[1]);
            check(got[0] == 8, "24: forty probes inside one second from one relay id draw eight answers");
            check(got[1] == 1, "24: and a different id inside that same second is still answered");

            gl_fk_pump(&rel, fake, got, NFK, 1100);
            memset(got, 0, sizeof got);
            nm_probe_clear_all();
            t0 = gl_now_ms();
            for (k = 0; k < NFK; k++) next[k] = t0 + (unsigned)(k * 51);
            while (gl_now_ms() - t0 < 3600u || (started && sp == NM_PROBE_PROBING
                                                 && gl_now_ms() - t0 < 7000u)) {
                const unsigned now = gl_now_ms();
                if (now - t0 < 3600u) {
                    for (k = 0; k < NFK; k++) {
                        if (now - next[k] < 0x80000000u) {
                            gl_id_probe(fake, &relay_at, GL_FAKE_BASE + (unsigned)k, room,
                                        0x6000u + (unsigned)(k * 16 + sent[k]));
                            sent[k]++;
                            next[k] += 1020u;
                        }
                    }
                }
                if (!started && now - t0 >= 1100u) started = nm_probe_start(code);
                gl_fk_pump(&rel, fake, got, NFK, 2);
                if (started) sp = nm_probe_poll(code, &ra);
            }
            gl_fk_pump(&rel, fake, got, NFK, 300);
            for (k = 0; k < NFK; k++) if (got[k] != sent[k] || sent[k] < 3) every = 0;
            printf("  leg 24: twenty ids at a probe a second each (%d ids new to the relay): prober "
                   "state=%d answers=%d/%d; asker 0 answered %d/%d, asker 19 %d/%d\n", rel.n - ids0, sp,
                   started ? ra.answers : -1, started ? ra.sent : -1, got[0], sent[0], got[NFK - 1],
                   sent[NFK - 1]);
            check(rel.n - ids0 == NFK, "24: the relay saw twenty new ids, so the room saw twenty senders");
            check(started && sp == NM_PROBE_ANSWERED && ra.answers == NM_PROBE_COUNT,
                  "24: with twenty senders keeping the room's table full, the browser's prober is "
                  "answered every probe");
            check(every, "24: and so is every probe from all twenty: a full table makes room rather than refusing");

            /* ONE ROOM UNDER TWO SPELLINGS OF ITS CODE IS ONE TARGET. */
            gl_fk_pump(&rel, fake, got, NFK, 1100);
            nm_probe_clear_all();
            snprintf(alias, sizeof alias, "%s", code);
            for (k = 1; alias[k]; k++) {
                if (alias[k] >= 'A' && alias[k] <= 'Z') alias[k] = (char)(alias[k] - 'A' + 'a');
                else if (alias[k] == '0') alias[k] = 'o';
                else if (alias[k] == '1') alias[k] = 'l';
            }
            rel.watch = (unsigned)room_id;
            rel.watched = 0;
            check(strcmp(alias, code) != 0 && nm_probe_start(code) == 1 && nm_probe_start(alias) == 1,
                  "24: the room's code and another spelling of it are both taken");
            t0 = gl_now_ms();
            for (;;) {
                const int sa = nm_probe_poll(code, &ra);
                const int sb = nm_probe_poll(alias, &rb);
                gl_fk_pump(&rel, fake, got, NFK, 2);
                if ((sa != NM_PROBE_PROBING && sb != NM_PROBE_PROBING) || gl_now_ms() - t0 > 5000u) break;
            }
            gl_fk_pump(&rel, fake, got, NFK, 200);
            printf("  leg 24: %s and %s: states %d/%d, rtt %d/%d ms, sent %d/%d; the room was sent %d probes\n",
                   code, alias, ra.state, rb.state, ra.rtt_ms, rb.rtt_ms, ra.sent, rb.sent, rel.watched);
            check(ra.state == NM_PROBE_ANSWERED && rb.state == NM_PROBE_ANSWERED
                  && ra.sent == rb.sent && ra.rtt_ms == rb.rtt_ms,
                  "24: and they are one target with one answer");
            check(rel.watched == NM_PROBE_COUNT,
                  "24: so the room was sent one round of probes, not one per spelling");
            rel.watch = 0u;
            if (fake) net_close(fake);
        }
        nm_probe_shutdown();
        nm_lobby_cancel();
        if (rel.sock) net_close(rel.sock);
    }

    printf("\n");
    if (fails == 0) {
        printf("  PASSED. %d checks. A host opens a room, a joiner is seated, the host's\n"
               "          START is refused until that joiner readies, and both ends then\n"
               "          reach the same match on the same scenario. A wrong passcode is\n"
               "          refused by name. And a player who never readies can be REMOVED,\n"
               "          is told so by name, and leaves an open seat behind -- without\n"
               "          which one idle keyboard holds the room shut for everybody.\n"
               "          And the seats are a menu: a joiner walks past a BOT seat into\n"
               "          the EMPTY one, a BLOCKed seat is nobody's, and the started\n"
               "          match waits on the person alone. An engine nobody could read\n"
               "          (order-wire hash 0) is refused in BOTH directions rather than\n"
               "          matching another zero and switching the check off. And the map\n"
               "          is judged when the WELCOME NAMES it: not having it and having a\n"
               "          different copy of it are two refusals in words, the matching\n"
               "          copy still gets in, and a peer that refuses itself out of a\n"
               "          seat it was already given says goodbye instead of leaving a\n"
               "          phantom the host waits on for ever. And a host that changes the\n"
               "          map or the rules unlights every READY on both ends, so nobody\n"
               "          is committed to a match they were never shown. And the RELAY\n"
               "          arming is one shot that cannot leak into the next room, even\n"
               "          from a door that refused its own arguments before opening\n"
               "          anything: a relayed room must carry a passcode, and the two\n"
               "          blocking command-line doors refuse to be relayed at all. And a\n"
               "          START waits for every seat's round trip (for 3 s at most) and\n"
               "          goes out on the lookahead the slowest one gives; a held START is\n"
               "          dropped when the room changes and never seats a knock; nothing\n"
               "          edits the room once it is on the wire; and one that cannot\n"
               "          finish fails the room rather than leaving a seat in a match\n"
               "          nobody else is playing. And a READY that the wire eats is said\n"
               "          again until the host echoes it, so one lost datagram no longer\n"
               "          leaves a room that can never start and a player with nothing to\n"
               "          go on but pressing the button twice. And a peer stuck on a turn\n"
               "          can ask for it by number: the host answers the seat that asked,\n"
               "          passes the question to the other joiner it names, does each at\n"
               "          most once a beat and at sixteen bytes however long the question\n"
               "          was, and ignores one signed with somebody else's seat. And a\n"
               "          PROBE is answered from anybody, seating nobody, in sixteen bytes,\n"
               "          eight times a second per sender and one log line a second at\n"
               "          most; the browser's prober reads four spaced round trips from a\n"
               "          direct room and from a relayed one on one registration, and a\n"
               "          host that does not know the word, an empty port and a room code\n"
               "          nobody holds all read as no answer once the schedule has run.\n",
               checks);
        return 0;
    }
    printf("  FAILED: %d of %d checks.\n", fails, checks);
    return 1;
}
