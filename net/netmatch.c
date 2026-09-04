/*
 * netmatch.c -- one lockstep match between two peers. See netmatch.h.
 *
 * gnu89 rather than c89 because it sleeps, and sleeping is a platform call.
 */
#include "netmatch.h"
#include "lockstep.h"
#include "net_udp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
static void nm_nap(int ms) { Sleep((DWORD)ms); }
static unsigned nm_now_ms(void) { return (unsigned)GetTickCount(); }
#else
#include <unistd.h>
#include <sys/time.h>
static void nm_nap(int ms) { usleep((useconds_t)ms * 1000); }
static unsigned nm_now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned)(tv.tv_sec * 1000u + tv.tv_usec / 1000u);
}
#endif

/* Wire words. Distinct from LS_MAGIC so the pump can route by the first four bytes, and
   from netcheck's, so a stray netcheck on the same port is refused rather than obeyed. */
#define NM_HELLO   0x4D4C4548u /* 'HELM' joiner -> host: version, abi hash */
#define NM_WELCOME 0x4D434C57u /* 'WLCM' host -> joiner: the setup */
#define NM_REFUSE  0x4D455052u /* 'RPEM' host -> joiner: why not */
#define NM_READY   0x4D594452u /* 'RDYM' joiner -> host: setup adopted */
#define NM_SYNC    0x4D434E53u /* 'SNCM' either way: frame, world hash */
#define NM_BYE     0x4D455942u /* 'BYEM' either way: leaving */
#define NM_VERSION 1

#define NM_HASH_RING 128
#define NM_RESEND_MS 100      /* re-send the last turn packet while waiting on the peer */
#define NM_SILENCE_MS 30000   /* a peer silent this long has left */

static NetSock* s_sock = NULL;
static NetAddr s_peer;
static int s_active = 0;
static int s_seat = -1;
static NmSetup s_setup;
static unsigned s_abi = 0;

/* THE SCHEDULER STATE IS STATIC, NOT A LOCAL, and that is not style: it is a megabyte,
   and Windows gives a program 2 MB of stack. netcheck.exe learned that on launch. */
static LsState s_ls;

static NmDrainFn s_drain = NULL;
static NmPostFn s_post = NULL;
static void* s_user = NULL;
static int s_event_size = 22;

static unsigned char s_last_pkt[LS_PACKET_MAX];
static int s_last_pkt_len = 0;
static unsigned s_last_send_ms = 0;
static unsigned s_last_heard_ms = 0;
static int s_peer_left = 0;
static int s_fatal = 0;

static unsigned s_my_hash[NM_HASH_RING];
static unsigned s_my_hash_frame[NM_HASH_RING];
static unsigned s_peer_hash[NM_HASH_RING];
static unsigned s_peer_hash_frame[NM_HASH_RING];
static int s_desynced = 0;
static unsigned s_desync_frame = 0;
static unsigned s_synced_frames = 0;

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

/* ---------------------------------------------------------------- setup on the wire -- */
/* Fixed layout, little endian, one byte per roster field. 16 + 8*4 + 5*8 = 88 bytes. */
#define NM_SETUP_BYTES 88

static int setup_pack(const NmSetup* s, unsigned char* p)
{
    int i;
    memset(p, 0, NM_SETUP_BYTES);
    memcpy(p, s->scenario, 15);
    put_u32(p + 16, (unsigned)s->credits);
    p[20] = (unsigned char)s->tiberium;
    p[21] = (unsigned char)s->crates;
    p[22] = (unsigned char)s->superweapons;
    p[23] = (unsigned char)s->bases;
    put_u32(p + 24, (unsigned)s->unit_count);
    p[28] = (unsigned char)s->speed;
    p[29] = (unsigned char)s->seats;
    p[30] = (unsigned char)s->humans;
    for (i = 0; i < NM_MAX_SEATS; i++) {
        p[48 + i] = s->house[i];
        p[56 + i] = s->colour[i];
        p[64 + i] = s->team[i];
        p[72 + i] = s->start[i];
        p[80 + i] = s->is_ai[i];
    }
    return NM_SETUP_BYTES;
}

static void setup_unpack(NmSetup* s, const unsigned char* p)
{
    int i;
    memset(s, 0, sizeof(*s));
    memcpy(s->scenario, p, 15);
    s->scenario[15] = '\0';
    s->credits = (int)get_u32(p + 16);
    s->tiberium = p[20];
    s->crates = p[21];
    s->superweapons = p[22];
    s->bases = p[23];
    s->unit_count = (int)get_u32(p + 24);
    s->speed = p[28];
    s->seats = p[29];
    s->humans = p[30];
    for (i = 0; i < NM_MAX_SEATS; i++) {
        s->house[i] = p[48 + i];
        s->colour[i] = p[56 + i];
        s->team[i] = p[64 + i];
        s->start[i] = p[72 + i];
        s->is_ai[i] = p[80 + i];
    }
}

/* ------------------------------------------------------------------- the handshake -- */

static int nm_open(unsigned short port)
{
    if (net_startup() != 0) {
        printf("NET|error=networking would not start\n");
        return 0;
    }
    s_sock = net_open(port);
    if (!s_sock) {
        printf("NET|error=could not open UDP port %u%s\n", (unsigned)port,
               port ? " (something else is using it?)" : "");
        net_shutdown();
        return 0;
    }
    return 1;
}

static void nm_reset_match(int seat)
{
    s_seat = seat;
    s_active = 1;
    s_peer_left = 0;
    s_fatal = 0;
    s_desynced = 0;
    s_desync_frame = 0;
    s_synced_frames = 0;
    s_last_pkt_len = 0;
    s_last_send_ms = 0;
    s_last_heard_ms = nm_now_ms();
    memset(s_my_hash_frame, 0xFF, sizeof(s_my_hash_frame));
    memset(s_peer_hash_frame, 0xFF, sizeof(s_peer_hash_frame));
    ls_init(&s_ls, 2, seat);
}

int nm_host(unsigned short port, const NmSetup* setup, unsigned abi_hash, int timeout_s)
{
    unsigned char in[LS_PACKET_MAX];
    unsigned char out[4 + 4 + NM_SETUP_BYTES];
    NetAddr from;
    time_t deadline;
    int have_peer = 0, ready = 0, n;
    unsigned last_welcome = 0;
    char txt[64];

    if (!setup || !nm_open(port)) return -1;
    s_setup = *setup;
    s_abi = abi_hash;
    printf("NET|hosting|port=%u|scenario=%s|seats=%d|waiting=%ds\n",
           (unsigned)net_local_port(s_sock), s_setup.scenario, s_setup.seats, timeout_s);
    fflush(stdout);
    deadline = time(0) + timeout_s;
    while (!ready && time(0) < deadline) {
        n = net_recv(s_sock, &from, in, (int)sizeof in);
        if (n >= 12 && get_u32(in) == NM_HELLO) {
            unsigned ver = get_u32(in + 4);
            unsigned abi = get_u32(in + 8);
            if (ver != NM_VERSION || abi != abi_hash) {
                unsigned char no[12];
                put_u32(no, NM_REFUSE);
                put_u32(no + 4, ver != NM_VERSION ? 1u : 2u);
                put_u32(no + 8, abi_hash);
                net_send(s_sock, &from, no, 12);
                net_addr_text(&from, txt, (int)sizeof txt);
                printf("NET|refused|peer=%s|reason=%s|theirs=%08X|mine=%08X\n", txt,
                       ver != NM_VERSION ? "version" : "order-wire layout", abi, abi_hash);
                fflush(stdout);
                continue;
            }
            if (!have_peer) {
                s_peer = from;
                have_peer = 1;
                net_addr_text(&s_peer, txt, (int)sizeof txt);
                printf("NET|joiner|peer=%s\n", txt);
                fflush(stdout);
            }
        }
        if (have_peer && n >= 4 && get_u32(in) == NM_READY && net_addr_equal(&from, &s_peer)) {
            ready = 1;
            break;
        }
        /* The welcome carries the setup and is what the joiner is waiting on, so it is
           repeated until the joiner says it has it. */
        if (have_peer && (last_welcome == 0 || nm_now_ms() - last_welcome > 300)) {
            put_u32(out, NM_WELCOME);
            put_u32(out + 4, abi_hash);
            setup_pack(&s_setup, out + 8);
            net_send(s_sock, &s_peer, out, (int)sizeof out);
            last_welcome = nm_now_ms();
        }
        if (n <= 0) nm_nap(5);
    }
    if (!ready) {
        printf("NET|error=%s within %d seconds\n",
               have_peer ? "the joiner never confirmed the setup" : "nobody joined", timeout_s);
        fflush(stdout);
        net_close(s_sock);
        s_sock = NULL;
        net_shutdown();
        return -1;
    }
    nm_reset_match(0);
    printf("NET|match|seat=0|peer=%s|scenario=%s|speed=%d|abi=%08X\n", txt, s_setup.scenario,
           s_setup.speed, abi_hash);
    fflush(stdout);
    return 0;
}

int nm_join(const char* addr, unsigned short port, NmSetup* setup, unsigned abi_hash, int timeout_s)
{
    unsigned char in[LS_PACKET_MAX];
    unsigned char hello[12];
    NetAddr from;
    time_t deadline;
    int have_setup = 0, n, k;
    unsigned last_hello = 0;
    char txt[64];

    if (!setup || !nm_open(0)) return -1;
    if (net_resolve(addr, port, &s_peer) != 0) {
        printf("NET|error=could not resolve '%s'\n", addr);
        net_close(s_sock);
        s_sock = NULL;
        net_shutdown();
        return -1;
    }
    s_abi = abi_hash;
    net_addr_text(&s_peer, txt, (int)sizeof txt);
    printf("NET|joining|peer=%s|waiting=%ds\n", txt, timeout_s);
    fflush(stdout);
    put_u32(hello, NM_HELLO);
    put_u32(hello + 4, NM_VERSION);
    put_u32(hello + 8, abi_hash);
    deadline = time(0) + timeout_s;
    while (!have_setup && time(0) < deadline) {
        if (last_hello == 0 || nm_now_ms() - last_hello > 250) {
            net_send(s_sock, &s_peer, hello, 12);
            last_hello = nm_now_ms();
        }
        n = net_recv(s_sock, &from, in, (int)sizeof in);
        if (n >= 4 && net_addr_equal(&from, &s_peer)) {
            if (n >= 8 + NM_SETUP_BYTES && get_u32(in) == NM_WELCOME) {
                setup_unpack(setup, in + 8);
                s_setup = *setup;
                have_setup = 1;
            } else if (n >= 12 && get_u32(in) == NM_REFUSE) {
                printf("NET|refused-by-host|reason=%s|host-abi=%08X|mine=%08X\n",
                       get_u32(in + 4) == 1u ? "version" : "order-wire layout", get_u32(in + 8), abi_hash);
                fflush(stdout);
                net_close(s_sock);
                s_sock = NULL;
                net_shutdown();
                return -1;
            }
        }
        if (!have_setup) nm_nap(10);
    }
    if (!have_setup) {
        printf("NET|error=the host never answered within %d seconds (not running, or UDP %u does not reach it)\n",
               timeout_s, (unsigned)port);
        fflush(stdout);
        net_close(s_sock);
        s_sock = NULL;
        net_shutdown();
        return -1;
    }
    /* Several times: it is the one datagram whose loss strands the host in its wait. The
       host keeps re-sending WELCOME until it hears this, and any repeat is ignored. */
    for (k = 0; k < 3; k++) {
        unsigned char rd[4];
        put_u32(rd, NM_READY);
        net_send(s_sock, &s_peer, rd, 4);
        nm_nap(20);
    }
    nm_reset_match(1);
    printf("NET|match|seat=1|peer=%s|scenario=%s|speed=%d|abi=%08X\n", txt, s_setup.scenario,
           s_setup.speed, abi_hash);
    fflush(stdout);
    return 1;
}

int nm_active(void) { return s_active; }
int nm_seat(void) { return s_seat; }
const NmSetup* nm_setup(void) { return &s_setup; }

void nm_set_engine(NmDrainFn drain, NmPostFn post, void* user, int event_size)
{
    s_drain = drain;
    s_post = post;
    s_user = user;
    s_event_size = event_size > 0 ? event_size : 22;
}

/* ---------------------------------------------------------------------- the turn --- */

static void nm_pump(void)
{
    unsigned char in[LS_PACKET_MAX];
    NetAddr from;
    int n;
    if (!s_sock) return;
    while ((n = net_recv(s_sock, &from, in, (int)sizeof in)) > 0) {
        unsigned magic;
        if (!net_addr_equal(&from, &s_peer)) continue;
        if (n < 4) continue;
        s_last_heard_ms = nm_now_ms();
        magic = get_u32(in);
        if (magic == LS_MAGIC) {
            ls_on_packet(&s_ls, in, n);
        } else if (magic == NM_SYNC && n >= 12) {
            unsigned frame = get_u32(in + 4);
            unsigned hash = get_u32(in + 8);
            unsigned slot = frame % NM_HASH_RING;
            s_peer_hash[slot] = hash;
            s_peer_hash_frame[slot] = frame;
            if (s_my_hash_frame[slot] == frame) {
                if (s_my_hash[slot] != hash && !s_desynced) {
                    s_desynced = 1;
                    s_desync_frame = frame;
                    printf("NETDESYNC|frame=%u|mine=%08X|peer=%08X\n", frame, s_my_hash[slot], hash);
                    fflush(stdout);
                } else if (s_my_hash[slot] == hash) {
                    s_synced_frames++;
                }
            }
        } else if (magic == NM_BYE) {
            if (!s_peer_left) {
                printf("NET|peer-left|turn=%u\n", ls_exec_turn(&s_ls));
                fflush(stdout);
            }
            s_peer_left = 1;
        }
        /* Late HELLO, WELCOME and READY repeats share this socket and mean nothing now. */
    }
    if (!s_peer_left && nm_now_ms() - s_last_heard_ms > NM_SILENCE_MS) {
        printf("NET|peer-silent|seconds=%d|turn=%u\n", NM_SILENCE_MS / 1000, ls_exec_turn(&s_ls));
        fflush(stdout);
        s_peer_left = 1;
    }
}

int nm_begin_turn(void)
{
    unsigned char events[64 * 64];
    int n, i, len;
    if (!s_active || s_fatal) return 0;
    if (s_peer_left) return 0;
    if (s_drain) {
        n = s_drain(s_user, events, 64, LS_MAX_AHEAD);
        if (n < 0) {
            printf("NET|error=the brain refused to drain its orders (%d): is lockstep on?\n", n);
            fflush(stdout);
            s_fatal = 1;
            return 0;
        }
        for (i = 0; i < n; i++) {
            int rc = ls_local_order(&s_ls, events + (size_t)i * (size_t)s_event_size, s_event_size);
            if (rc != LS_OK) {
                printf("NET|error=order queue overflow on turn %u (%d orders this turn); the match cannot continue\n",
                       ls_send_turn(&s_ls), n);
                fflush(stdout);
                s_fatal = 1;
                return 0;
            }
        }
    }
    len = ls_pack(&s_ls, s_last_pkt, (int)sizeof s_last_pkt);
    if (len <= 0) {
        s_fatal = 1;
        return 0;
    }
    s_last_pkt_len = len;
    net_send(s_sock, &s_peer, s_last_pkt, len);
    s_last_send_ms = nm_now_ms();
    return 1;
}

int nm_turn_ready(void)
{
    if (!s_active || s_fatal) return 0;
    nm_pump();
    if (s_peer_left) return 0;
    if (ls_turn_ready(&s_ls)) return 1;
    /* Both peers' packets for one turn can be lost together, and then each waits for the
       other for ever, because a peer only speaks when it advances. The last packet is
       repeated while waiting, which the receiver's idempotent store makes free. */
    if (s_last_pkt_len > 0 && nm_now_ms() - s_last_send_ms > NM_RESEND_MS) {
        net_send(s_sock, &s_peer, s_last_pkt, s_last_pkt_len);
        s_last_send_ms = nm_now_ms();
    }
    return 0;
}

int nm_wait_turn(int timeout_ms)
{
    unsigned start = nm_now_ms();
    for (;;) {
        if (nm_turn_ready()) return 1;
        if (s_peer_left || s_fatal) return 0;
        if (nm_now_ms() - start > (unsigned)timeout_ms) return 0;
        nm_nap(1);
    }
}

static void nm_sink(void* user, int seat, const void* bytes, int len)
{
    (void)user;
    (void)seat;
    if (len != s_event_size) {
        printf("NET|error=an order of %d bytes arrived where the wire unit is %d\n", len, s_event_size);
        fflush(stdout);
        s_fatal = 1;
        return;
    }
    if (s_post && s_post(s_user, bytes) != 1) {
        printf("NET|error=the brain refused a posted order on turn %u from seat %d\n", ls_exec_turn(&s_ls), seat);
        fflush(stdout);
        s_fatal = 1;
    }
}

int nm_run_turn(void)
{
    if (!s_active || s_fatal) return 0;
    ls_run_turn(&s_ls, nm_sink, NULL);
    return s_fatal ? 0 : 1;
}

/* ------------------------------------------------------------------- the alarm ----- */

void nm_note_hash(unsigned frame, unsigned hash)
{
    unsigned slot = frame % NM_HASH_RING;
    if (!s_active) return;
    s_my_hash[slot] = hash;
    s_my_hash_frame[slot] = frame;
    if (s_peer_hash_frame[slot] == frame) {
        if (s_peer_hash[slot] != hash && !s_desynced) {
            s_desynced = 1;
            s_desync_frame = frame;
            printf("NETDESYNC|frame=%u|mine=%08X|peer=%08X\n", frame, hash, s_peer_hash[slot]);
            fflush(stdout);
        } else if (s_peer_hash[slot] == hash) {
            s_synced_frames++;
        }
    }
    if ((frame % NM_SYNC_EVERY) == 0) {
        unsigned char p[12];
        put_u32(p, NM_SYNC);
        put_u32(p + 4, frame);
        put_u32(p + 8, hash);
        if (s_sock) net_send(s_sock, &s_peer, p, 12);
        printf("NETSYNC|frame=%u|hash=%08X\n", frame, hash);
        fflush(stdout);
    }
}

int nm_desynced(void) { return s_desynced; }
unsigned nm_desync_frame(void) { return s_desync_frame; }
int nm_peer_left(void) { return s_peer_left; }
unsigned nm_turns_run(void) { return ls_exec_turn(&s_ls); }

void nm_shutdown(void)
{
    if (!s_active) return;
    if (s_sock) {
        unsigned char bye[4];
        int k;
        put_u32(bye, NM_BYE);
        for (k = 0; k < 3; k++) net_send(s_sock, &s_peer, bye, 4);
        printf("NET|leaving|turns=%u|synced-frames=%u|packets-in=%lu|bad=%lu|orders-in=%lu\n",
               ls_exec_turn(&s_ls), s_synced_frames, s_ls.packets_in, s_ls.packets_bad, s_ls.orders_in);
        fflush(stdout);
        net_close(s_sock);
        s_sock = NULL;
        net_shutdown();
    }
    s_active = 0;
    s_seat = -1;
}
