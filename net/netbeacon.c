/*
 * netbeacon.c -- LAN discovery. See netbeacon.h for what this is and what it must not do.
 */
#include "netbeacon.h"
#include "net_udp.h"

#include <string.h>
#include <stdio.h>

#if defined(_WIN32)
  #include <windows.h>
  static unsigned nb_now_ms(void) { return (unsigned)GetTickCount(); }
#else
  #include <sys/time.h>
  static unsigned nb_now_ms(void)
  {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      return (unsigned)(tv.tv_sec * 1000u + tv.tv_usec / 1000u);
  }
#endif

/* 'CNCB'. Distinct from the match magics so a beacon that reaches a match socket, or the
   other way round, is dropped rather than half read. */
#define NB_MAGIC 0x42434E43u

/* 4 magic + 4 version + 4 port + 4 now + 4 max + 4 flags + 4 abi + 4 scen = 32,
   then the two strings. Fixed offsets, little endian, exactly as netmatch packs. */
#define NB_HDR   32
#define NB_BYTES (NB_HDR + NB_NAME_MAX + NB_SCEN_MAX + NB_MAPNAME_MAX)

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

/* A string field is fixed width and always terminated on the way OUT of the packet, never
   trusted to be terminated on the way in: a hostile or truncated beacon must not be able
   to run a printf off the end of a row. */
static void put_str(unsigned char* p, const char* s, int max)
{
    int i = 0;
    memset(p, 0, (size_t)max);
    if (!s) return;
    for (i = 0; i < max - 1 && s[i]; i++) p[i] = (unsigned char)s[i];
}
static void get_str(char* out, const unsigned char* p, int max)
{
    int i;
    for (i = 0; i < max - 1; i++) {
        unsigned char c = p[i];
        /* Printable ASCII only. A beacon is drawn straight into a menu row, and a control
           character there is at best a hole in the text and at worst a terminal escape in
           somebody's log. Anything else becomes a space. */
        out[i] = (c >= 32 && c < 127) ? (char)c : (c ? ' ' : '\0');
        if (!c) break;
    }
    out[max - 1] = '\0';
}

/* ------------------------------------------------------------------ the host side --- */

static NetSock* s_ann = NULL;
static unsigned s_last_ann = 0;

int nb_announce_open(void)
{
    if (s_ann) return 1;
    if (net_startup() != 0) return 0;
    /* Port 0: the beacon is SENT from an ephemeral port and only ever RECEIVED on
       NB_PORT, so a host does not need to hold the discovery port and two games on one
       machine cannot fight over it. */
    s_ann = net_open(0);
    if (!s_ann) { net_shutdown(); return 0; }
    if (net_set_broadcast(s_ann, 1) != 0) {
        /* Not fatal and deliberately quiet at this level: the caller decides whether a
           game nobody can find is worth mentioning, and it is the caller that has a
           screen to say it on. */
        net_close(s_ann);
        s_ann = NULL;
        net_shutdown();
        return 0;
    }
    s_last_ann = 0;
    return 1;
}

void nb_announce(const char* name, const char* scenario, const char* mapname,
                 unsigned short match_port,
                 int players_now, int players_max, int has_password,
                 unsigned abi, unsigned scen_hash)
{
    unsigned char pkt[NB_BYTES];
    unsigned now = nb_now_ms();
    if (!s_ann) return;
    if (s_last_ann != 0 && now - s_last_ann < NB_PERIOD_MS) return;
    s_last_ann = now;

    memset(pkt, 0, sizeof pkt);
    put_u32(pkt, NB_MAGIC);
    put_u32(pkt + 4, NB_VERSION);
    put_u32(pkt + 8, (unsigned)match_port);
    put_u32(pkt + 12, (unsigned)(players_now < 0 ? 0 : players_now));
    put_u32(pkt + 16, (unsigned)(players_max < 0 ? 0 : players_max));
    put_u32(pkt + 20, has_password ? 1u : 0u);
    put_u32(pkt + 24, abi);
    put_u32(pkt + 28, scen_hash);
    put_str(pkt + NB_HDR, name, NB_NAME_MAX);
    put_str(pkt + NB_HDR + NB_NAME_MAX, scenario, NB_SCEN_MAX);
    put_str(pkt + NB_HDR + NB_NAME_MAX + NB_SCEN_MAX, mapname, NB_MAPNAME_MAX);

    /* One datagram per broadcast-capable interface. A machine on two networks announces
       on both, which is what somebody with wifi and ethernet expects. */
    {
        NetAddr addrs[8];
        int na = net_broadcast_addrs(NB_PORT, addrs, 8);
        int i;
        for (i = 0; i < na; i++) net_send(s_ann, &addrs[i], pkt, (int)sizeof pkt);
    }
}

void nb_announce_close(void)
{
    if (!s_ann) return;
    net_close(s_ann);
    s_ann = NULL;
    net_shutdown();
}

/* --------------------------------------------------------------- the browser side --- */

static NetSock* s_browse = NULL;
static NbGame s_games[NB_MAX_GAMES];
static int s_count = 0;

int nb_browse_open(void)
{
    if (s_browse) return 1;
    if (net_startup() != 0) return 0;
    /* SHARED, so a machine can host and browse at once, and so two copies of the game on
       one desk can both watch. Without it the second one silently sees nothing. */
    s_browse = net_open_shared(NB_PORT);
    if (!s_browse) { net_shutdown(); return 0; }
    s_count = 0;
    /* ONE DATAGRAM OUT, the moment the browser opens: on a Mac this is what makes the
       system ask whether the game may talk to the local network. A browser only ever
       LISTENED, so the question was first asked by a JOIN, fullscreen, and a dialog
       nobody saw meant every packet to the host was dropped in silence (the first two
       real-network tests). Six bytes, wrong size and magic for every browser, so nobody
       lists it. The launcher does the same before any game; this covers a copy started
       without it. */
    {
        NetAddr bc[8];
        int i, n = net_broadcast_addrs(NB_PORT, bc, 8);
        if (net_set_broadcast(s_browse, 1) == 0)
            for (i = 0; i < n; i++) net_send(s_browse, &bc[i], "CNC3D?", 6);
    }
    return 1;
}

void nb_browse_clear(void) { s_count = 0; }

/* Same game, or a new row? The pair (address, match port) is the identity: one machine
   can legitimately host two games on two ports, and separate hosts can offer games with
   the same NAME, which is why the name is not part of this. */
static int nb_find(const char* addr, unsigned short port)
{
    int i;
    for (i = 0; i < s_count; i++) {
        if (s_games[i].port == port && strcmp(s_games[i].addr, addr) == 0) return i;
    }
    return (-1);
}

int nb_browse_poll(void)
{
    unsigned char in[512];
    NetAddr from;
    int n;
    unsigned now = nb_now_ms();
    if (!s_browse) return 0;

    while ((n = net_recv(s_browse, &from, in, (int)sizeof in)) > 0) {
        char addr[NB_ADDR_MAX];
        char text[NB_ADDR_MAX];
        unsigned short mport;
        int idx;
        char* colon;
        if (n < (int)NB_BYTES) continue;
        if (get_u32(in) != NB_MAGIC) continue;
        if (get_u32(in + 4) != NB_VERSION) continue;

        /* THE ADDRESS COMES FROM THE DATAGRAM, NOT FROM INSIDE IT. net_addr_text gives
           "1.2.3.4:56789" and the port on it is the sender's ephemeral one, which is not
           where the match is; the match port is a field. So the text is cut at the colon
           and the field supplies the rest. */
        net_addr_text(&from, text, (int)sizeof text);
        snprintf(addr, sizeof addr, "%s", text);
        colon = strrchr(addr, ':');
        if (colon) *colon = '\0';

        mport = (unsigned short)get_u32(in + 8);
        if (mport == 0) continue;

        idx = nb_find(addr, mport);
        if (idx < 0) {
            if (s_count >= NB_MAX_GAMES) continue;   /* full: drop, never overwrite */
            idx = s_count++;
            memset(&s_games[idx], 0, sizeof s_games[idx]);
            snprintf(s_games[idx].addr, sizeof s_games[idx].addr, "%s", addr);
            s_games[idx].port = mport;
        }
        s_games[idx].players_now = (int)get_u32(in + 12);
        s_games[idx].players_max = (int)get_u32(in + 16);
        s_games[idx].has_password = get_u32(in + 20) ? 1 : 0;
        s_games[idx].abi = get_u32(in + 24);
        s_games[idx].scen_hash = get_u32(in + 28);
        get_str(s_games[idx].name, in + NB_HDR, NB_NAME_MAX);
        get_str(s_games[idx].scenario, in + NB_HDR + NB_NAME_MAX, NB_SCEN_MAX);
        get_str(s_games[idx].mapname, in + NB_HDR + NB_NAME_MAX + NB_SCEN_MAX,
                NB_MAPNAME_MAX);
        s_games[idx].last_heard_ms = now;
    }

    /* Sweep the stale. Order is not preserved on removal, and that is deliberate: a
       stable ORDER would be nice but a row that jumps is better than a row that lingers,
       and the caller sorts for display anyway. */
    {
        int i = 0;
        while (i < s_count) {
            if (now - s_games[i].last_heard_ms > NB_STALE_MS) {
                s_games[i] = s_games[s_count - 1];
                s_count--;
            } else {
                i++;
            }
        }
    }
    return s_count;
}

int nb_browse_count(void) { return s_count; }

const NbGame* nb_browse_get(int i)
{
    if (i < 0 || i >= s_count) return NULL;
    return &s_games[i];
}

void nb_browse_close(void)
{
    if (!s_browse) return;
    net_close(s_browse);
    s_browse = NULL;
    s_count = 0;
    net_shutdown();
}
