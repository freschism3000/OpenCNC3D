/*
 * net_udp.c -- one non blocking UDP socket, on Windows and on everything else.
 *
 * The whole platform difference lives in this file, and it is smaller than its reputation:
 * winsock needs a startup call, spells close and ioctl differently, and reports errors
 * through its own function rather than errno. Everything else is the same BSD API that
 * has been stable since before the game this is a port of.
 *
 * The Tier 1 (Windows 98) build does not compile this file, because it does not compile
 * the host at all. Winsock 2 is present on 98 SE and plain UDP would work there, so this
 * file is not what stands in the way; the Tier 1 answer is recorded in the gap contract
 * rather than guessed at here.
 */
#include "net_udp.h"

/* TIER 1 MUST NOT COMPILE THIS FILE, and until this guard the only thing stopping it was
   an accident. The Win98 build sets -D_WIN32_WINNT=0x0400, under which mingw's ws2tcpip.h
   does not declare getaddrinfo, which net_resolve calls; so the file happened to fail to
   build there rather than being refused. An accident is not a contract. Networking is
   declared Tier 2 only in the gap register, and this turns that declaration into a build
   failure that says which rule was broken instead of an undeclared identifier. */
#if defined(WIN98)
#error "net/ is Tier 2 only. The Win98 build must not compile this file; multiplayer is declared absent there."
#endif

#include <string.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#if defined(_WIN32)
  /* Ask for winsock2 before windows.h, or windows.h drags in winsock 1 and the two
     collide in a way whose error message names neither. */
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef int socklen_t;
  #define CLOSESOCK closesocket
  #define SOCKBAD   INVALID_SOCKET
  typedef SOCKET sock_t;
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <ifaddrs.h>
  #include <net/if.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  #include <sys/time.h>
  #include <sys/uio.h>
  #define CLOSESOCK close
  #define SOCKBAD   (-1)
  typedef int sock_t;
#endif

struct NetSock {
    sock_t         fd;
    unsigned short port;
    /* ---- relay mode, all zero on an ordinary socket ---- */
    int            tunnel;          /* non zero: every datagram carries a V3 header  */
    unsigned long  local_id;        /* this peer's id, as the relay knows it         */
    NetAddr        relay;           /* the relay itself, an ordinary IP address      */
    long           last_reg;        /* seconds, when the registration last went out  */
    /* THE RELAY ANSWERED ITS OWN PING. Noticed here rather than by the caller, because
       the echo arrives on this socket among the match traffic and net_recv is the only
       thing reading it -- a caller trying to pick the reply out with its own recvfrom
       would be racing the match for datagrams. */
    int            ping_seen;
    /* ---- arrival stamps, all zero unless net_set_rx_stamps asked for them ---- */
    int            rx_stamps;       /* read with recvmsg and keep the system's stamp */
    int            rx_have;         /* the datagram last returned carried one        */
    long           rx_sec, rx_usec;
};

/* THE STAMPED READ, used only by a socket that asked for it, so every other socket keeps
   the plain recvfrom it has always had. Same contract as that recvfrom: the length, or
   negative with errno set. The stamp belongs to the datagram this call read, and a read
   that finds nothing, or a datagram with no stamp, leaves none. */
#if !defined(_WIN32) && defined(SO_TIMESTAMP) && defined(SCM_TIMESTAMP)
#define NET_HAVE_RX_STAMPS 1
static int recv_stamped(NetSock* s, void* dst, int cap, NetAddr* from, socklen_t* alen)
{
    struct msghdr msg;
    struct iovec iov;
    char ctrl[128];
    struct cmsghdr* c;
    int n;
    memset(&msg, 0, sizeof msg);
    iov.iov_base = dst;
    iov.iov_len = (size_t)cap;
    msg.msg_name = from->opaque;
    msg.msg_namelen = *alen;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = ctrl;
    msg.msg_controllen = sizeof ctrl;
    s->rx_have = 0;
    n = (int)recvmsg(s->fd, &msg, 0);
    if (n < 0) return n;
    *alen = msg.msg_namelen;
    c = CMSG_FIRSTHDR(&msg);
    while (c != NULL) {
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_TIMESTAMP) {
            struct timeval tv;
            memcpy(&tv, CMSG_DATA(c), sizeof tv);
            s->rx_sec = (long)tv.tv_sec;
            s->rx_usec = (long)tv.tv_usec;
            s->rx_have = 1;
        }
        c = CMSG_NXTHDR(&msg, c);   /* POSIX spells it NXTHDR; NEXTHDR is not declared */
    }
    return n;
}
#endif

int net_set_rx_stamps(NetSock* s, int on)
{
    if (!s || s->fd == SOCKBAD) return 0;
#if defined(NET_HAVE_RX_STAMPS)
    {
        const int v = on ? 1 : 0;
        if (setsockopt(s->fd, SOL_SOCKET, SO_TIMESTAMP, &v, (socklen_t)sizeof v) != 0) {
            s->rx_stamps = 0;
            return 0;
        }
        s->rx_stamps = v;
        s->rx_have = 0;
        return v;
    }
#else
    /* WINDOWS HAS NO STAMP HERE. Its own form needs a recent Windows 10, an ioctl and a
       different read call, none of which can be exercised from the machine this is built
       on, so the caller is told no and times the datagram itself. */
    (void)on;
    return 0;
#endif
}

int net_last_rx_time(const NetSock* s, long* sec, long* usec)
{
    if (!s || !s->rx_have) return 0;
    if (sec) *sec = s->rx_sec;
    if (usec) *usec = s->rx_usec;
    return 1;
}

/* The tunnel's eight byte header, and the two ids the protocol reserves. */
#define TUN_HDR      8
/* THE RELAY'S OWN CEILING FOR A WHOLE DATAGRAM, header included. The legacy V3 server
   reads into a buffer this size inside a bare loop with no exception handling, so an
   oversized datagram does not merely get dropped: by its own code it faults the task and
   takes the tunnel down for everyone using it. Named once because both directions need
   the same number -- the send side must not exceed it and the receive side must have
   room for all of it. */
#define TUN_DGRAM_MAX 1024
#define TUN_ID_NONE  0ul
#define TUN_ID_BCAST 0xFFFFFFFFul
/* Seconds between registrations. The relay forgets a peer after thirty (the older
   server) and the timeout cannot be shortened by retrying, because every retry arrives
   from an address the relay has already stopped believing. Fifteen leaves a whole
   missed registration of margin. */
#define TUN_REG_EVERY 15

/* HOW A TUNNEL ADDRESS IS TOLD FROM AN IP ONE. A NetAddr is opaque bytes plus a length,
   and every IP address written into one is a sockaddr, whose first field is a family
   that is never this. So a tunnel address is eight bytes: a tag that cannot be a
   sockaddr, then the peer's id. `len` being 8 is itself distinctive -- a sockaddr_in is
   16 and a sockaddr_in6 is 28 -- but the tag is what is actually tested, because a
   length is the kind of thing that gets rounded. */
#define TUN_TAG 0x4C4E5401ul   /* 'TNL' and a version, little endian on the wire */

static void tun_put_u32(unsigned char* p, unsigned long v)
{
    p[0] = (unsigned char)(v & 0xFFul);
    p[1] = (unsigned char)((v >> 8) & 0xFFul);
    p[2] = (unsigned char)((v >> 16) & 0xFFul);
    p[3] = (unsigned char)((v >> 24) & 0xFFul);
}

static unsigned long tun_get_u32(const unsigned char* p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8)
         | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static int addr_is_tunnel(const NetAddr* a)
{
    return a && a->len == TUN_HDR && tun_get_u32(a->opaque) == TUN_TAG;
}

void net_addr_tunnel(NetAddr* out, unsigned long peer_id)
{
    if (!out) return;
    memset(out, 0, sizeof *out);
    tun_put_u32(out->opaque, TUN_TAG);
    tun_put_u32(out->opaque + 4, peer_id);
    out->len = TUN_HDR;
}

int net_is_tunnel(const NetSock* s) { return s && s->tunnel; }

/* The registration, which is also the keepalive: this peer's id, addressed to nobody.
   Receiver zero is never a real peer, so the relay records the sender and forwards
   nothing. */
static void tun_register(NetSock* s)
{
    unsigned char p[TUN_HDR];
    if (!s || !s->tunnel) return;
    tun_put_u32(p, s->local_id);
    tun_put_u32(p + 4, TUN_ID_NONE);
    sendto(s->fd, (const char*)p, (size_t)TUN_HDR, 0,
           (const struct sockaddr*)(const void*)s->relay.opaque, (socklen_t)s->relay.len);
    s->last_reg = (long)time(NULL);
}

void net_tunnel_keepalive(NetSock* s)
{
    if (!s || !s->tunnel) return;
    if ((long)time(NULL) - s->last_reg >= TUN_REG_EVERY) tun_register(s);
}

/* ------------------------------------------------------------------------- lifecycle */

int net_startup(void)
{
#if defined(_WIN32)
    WSADATA wsa;
    /* 2.2 is what every Windows since 98 SE supplies. */
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0 ? 0 : -1;
#else
    return 0;
#endif
}

void net_shutdown(void)
{
#if defined(_WIN32)
    WSACleanup();
#endif
}

static int set_nonblocking(sock_t fd)
{
#if defined(_WIN32)
    u_long on = 1;
    return ioctlsocket(fd, FIONBIO, &on) == 0 ? 0 : -1;
#else
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) return -1;
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0 ? 0 : -1;
#endif
}

/* THE ONE IMPLEMENTATION. `reuse` asks for SO_REUSEADDR (and SO_REUSEPORT where the
   platform has it) so that several processes on one machine can listen to the same
   broadcast port. It is a parameter rather than always-on because a MATCH socket must
   NOT be reusable: two matches quietly binding one port would each receive a share of
   the other's packets, which is a fault nobody would think to look for. */
/* Defined beside net_send, where the flag it clears is set. Declared here because
   net_open_ex is the one place that knows a fresh socket is a fresh start. */
static void net_send_unblock(void);

static NetSock* net_open_ex(unsigned short port, int reuse)
{
    static NetSock pool[6];
    static int used = 0;
    NetSock* s;
    struct sockaddr_in a;
    socklen_t alen;

    /* A fixed pool rather than malloc: this file is linked into a host that already
       avoids allocation in its frame path, and a match needs one socket. Six is room for
       a match, a discovery beacon, a browser probe and a spare without ever asking the
       allocator.

       AND A CLOSED SLOT IS REUSED, which it was not. `used` only ever counted UP, so the
       pool was a budget of four sockets for the PROCESS'S WHOLE LIFE rather than four at
       once: a server browser that opens a discovery socket, closes it and refreshes would
       get four refreshes and then silently fail to open a fifth, with net_open returning
       0 and the caller reporting "networking would not start" on a machine whose
       networking was fine. Nothing had reopened a socket before the browser, which is why
       it had never bitten. */
    {
        int i;
        s = 0;
        for (i = 0; i < (int)(sizeof pool / sizeof pool[0]); i++) {
            if (i >= used) { s = &pool[i]; used = i + 1; break; }
            if (pool[i].fd == SOCKBAD) { s = &pool[i]; break; }
        }
        if (!s) return 0;
    }

    /* A REUSED SLOT IS WIPED, and this is not tidiness. A pool entry keeps every field it
       had when its socket was closed, and once a socket can be a RELAYED one those fields
       include its tunnel state. Without this, the first ordinary socket to reuse a slot
       that a tunnelled socket had finished with would be silently still tunnelled: every
       datagram wrapped in a relay header and posted to a relay, on a LAN game that never
       asked for one. It would present as "the LAN broke when internet play shipped", it
       would depend on the order the player happened to open things in, and nothing here
       would look wrong. The relay has no caller yet, so this is free to fix today and
       expensive to find later.
       fd is set explicitly rather than left as the memset's 0, because 0 is a perfectly
       valid descriptor and every reuse test in this file reads fd == SOCKBAD. */
    memset(s, 0, sizeof *s);
    s->fd = SOCKBAD;

    s->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (s->fd == SOCKBAD) return 0;

    /* A NEW SOCKET HAS NOT HAD A SEND REFUSED YET. See net_send_unblock: the flag was
       process-wide and never cleared, so one refusal on a LAN attempt condemned every
       later join, including one to an address that permission has no say over. */
    net_send_unblock();

    if (reuse) {
        int one = 1;
#if defined(_WIN32)
        setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);
#else
        setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &one, (socklen_t)sizeof one);
#if defined(SO_REUSEPORT)
        /* macOS and the BSDs need this one as well before a second process may bind the
           same UDP port; SO_REUSEADDR alone is not enough there. Not fatal if refused. */
        setsockopt(s->fd, SOL_SOCKET, SO_REUSEPORT, &one, (socklen_t)sizeof one);
#endif
#endif
    }

    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    /* SOCKBAD ON THE WAY OUT OF EVERY FAILURE, or the slot is lost. The pool reuses an
       entry by testing fd == SOCKBAD; closing the descriptor without saying so left the
       slot holding a stale, closed fd, so it was never handed out again -- and the pool
       is six deep, so six failed binds are enough to make net_open start refusing on a
       machine whose networking is fine. */
    if (bind(s->fd, (struct sockaddr*)&a, (socklen_t)sizeof a) != 0) {
        CLOSESOCK(s->fd);
        s->fd = SOCKBAD;
        return 0;
    }
    if (set_nonblocking(s->fd) != 0) {
        CLOSESOCK(s->fd);
        s->fd = SOCKBAD;
        return 0;
    }

    /* Ask the socket what it actually got, which is the point of passing 0. */
    alen = (socklen_t)sizeof a;
    if (getsockname(s->fd, (struct sockaddr*)&a, &alen) == 0) {
        s->port = ntohs(a.sin_port);
    } else {
        s->port = port;
    }

    return s;
}

void net_close(NetSock* s)
{
    if (!s || s->fd == SOCKBAD) return;
    CLOSESOCK(s->fd);
    s->fd = SOCKBAD;
}

unsigned short net_local_port(const NetSock* s)
{
    return s ? s->port : 0;
}

/* ------------------------------------------------------------------ LAN discovery ---- */

NetSock* net_open(unsigned short port) { return net_open_ex(port, 0); }
NetSock* net_open_shared(unsigned short port) { return net_open_ex(port, 1); }

int net_set_broadcast(NetSock* s, int on)
{
    int v = on ? 1 : 0;
    if (!s || s->fd == SOCKBAD) return -1;
#if defined(_WIN32)
    return setsockopt(s->fd, SOL_SOCKET, SO_BROADCAST, (const char*)&v, sizeof v) == 0 ? 0 : -1;
#else
    return setsockopt(s->fd, SOL_SOCKET, SO_BROADCAST, &v, (socklen_t)sizeof v) == 0 ? 0 : -1;
#endif
}

/* FILL A NetAddr PROPERLY. It is not a sockaddr_in: it is an opaque buffer plus a LEN,
   and a NetAddr whose len is 0 reads as "unset" everywhere in this file. Casting the
   struct straight to sockaddr_in* writes the bytes and leaves len at zero, so every
   address built that way is silently discarded by net_send and prints as "(unset)".
   That cost a debugging round; this helper exists so it cannot happen a second time. */
static void net_addr_set4(NetAddr* out, unsigned long host_order_ip, unsigned short port)
{
    struct sockaddr_in a;
    memset(out, 0, sizeof(*out));
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(host_order_ip);
    a.sin_port = htons(port);
    memcpy(out->opaque, &a, sizeof a);
    out->len = (int)sizeof a;
}

int net_broadcast_addrs(unsigned short port, NetAddr* out, int max)
{
    int n = 0;
    if (!out || max <= 0) return 0;
#if defined(_WIN32)
    /* UNVERIFIED ON WINDOWS. 255.255.255.255 is refused on macOS, which is why the POSIX
       arm below reads the interface list, but Windows is generally happy with it and the
       machine to check on was not to hand. If LAN discovery ever fails on Windows and
       works on the Mac, this line is the first place to look. */
    net_addr_set4(&out[n], INADDR_BROADCAST, port);
    n++;
#else
    {
        struct ifaddrs* list = NULL;
        struct ifaddrs* it;
        if (getifaddrs(&list) == 0) {
            for (it = list; it != NULL && n < max; it = it->ifa_next) {
                struct sockaddr_in* b;
                if (it->ifa_addr == NULL) continue;
                if (it->ifa_addr->sa_family != AF_INET) continue;
                if ((it->ifa_flags & IFF_UP) == 0) continue;
                if ((it->ifa_flags & IFF_BROADCAST) == 0) continue;
                if (it->ifa_broadaddr == NULL) continue;
                b = (struct sockaddr_in*)it->ifa_broadaddr;
                if (b->sin_addr.s_addr == 0) continue;
                net_addr_set4(&out[n], (unsigned long)ntohl(b->sin_addr.s_addr), port);
                n++;
            }
            freeifaddrs(list);
        }
    }
#endif
    if (n == 0 && max > 0) {
        /* No broadcast interface: a laptop with the network off. Loopback so that hosting
           and browsing on one machine still works, which is how this gets tested. */
        net_addr_set4(&out[0], INADDR_LOOPBACK, port);
        n = 1;
    }
    return n;
}

/* --------------------------------------------------------------------------- addresses */

int net_resolve(const char* host, unsigned short port, NetAddr* out)
{
    struct addrinfo hints, *res = 0;
    char portstr[16];

    if (!host || !out) return -1;
    memset(out, 0, sizeof *out);
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;          /* IPv4 for now; the struct holds v6 for later */
    hints.ai_socktype = SOCK_DGRAM;
    sprintf(portstr, "%u", (unsigned)port);

    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) return -1;
    if (res->ai_addrlen > sizeof out->opaque) { freeaddrinfo(res); return -1; }
    memcpy(out->opaque, res->ai_addr, res->ai_addrlen);
    out->len = (int)res->ai_addrlen;
    freeaddrinfo(res);
    return 0;
}

void net_addr_text(const NetAddr* a, char* out, int outmax)
{
    /* A PEER THROUGH A RELAY HAS NO ADDRESS TO PRINT, so it prints as what it is. Every
       diagnostic in the match layer names peers with this, and "0.0.0.0:0" for all of
       them would make a relayed room unreadable in exactly the logs that matter. */
    if (addr_is_tunnel(a) && out && outmax > 0) {
        sprintf(out, "tunnel:%lu", tun_get_u32(a->opaque + 4));
        return;
    }
    const struct sockaddr_in* in;
    if (!out || outmax < 8) return;
    out[0] = 0;
    if (!a || a->len == 0) { sprintf(out, "(unset)"); return; }
    in = (const struct sockaddr_in*)(const void*)a->opaque;
    if (in->sin_family == AF_INET) {
        unsigned long h = ntohl(in->sin_addr.s_addr);
        sprintf(out, "%lu.%lu.%lu.%lu:%u",
                (h >> 24) & 0xFF, (h >> 16) & 0xFF, (h >> 8) & 0xFF, h & 0xFF,
                (unsigned)ntohs(in->sin_port));
    } else {
        sprintf(out, "(non ipv4)");
    }
}

int net_addr_equal(const NetAddr* a, const NetAddr* b)
{
    /* TWO PEERS ARE THE SAME PEER WHEN THEIR IDS MATCH. This is what the host's
       seat lookup ends up asking through a relay, where every peer shares one IP and
       comparing sockaddrs would make them all one player. */
    if (addr_is_tunnel(a) || addr_is_tunnel(b)) {
        if (!addr_is_tunnel(a) || !addr_is_tunnel(b)) return 0;
        return tun_get_u32(a->opaque + 4) == tun_get_u32(b->opaque + 4);
    }
    if (!a || !b) return 0;
    if (a->len != b->len || a->len == 0) return 0;
    /* Compared field by field rather than with memcmp over the whole struct, because
       sockaddr_in carries eight bytes of padding that nothing initialises consistently,
       and a memcmp over those would call the same peer two different peers. */
    {
        const struct sockaddr_in* x = (const struct sockaddr_in*)(const void*)a->opaque;
        const struct sockaddr_in* y = (const struct sockaddr_in*)(const void*)b->opaque;
        if (x->sin_family != y->sin_family) return 0;
        if (x->sin_family != AF_INET) return 0;
        return x->sin_port == y->sin_port &&
               x->sin_addr.s_addr == y->sin_addr.s_addr;
    }
}

int net_addr_same_host(const NetAddr* a, const NetAddr* b)
{
    if (!a || !b) return 0;
    if (addr_is_tunnel(a) || addr_is_tunnel(b)) return net_addr_equal(a, b);
    if (a->len != b->len || a->len == 0) return 0;
    {
        const struct sockaddr_in* x = (const struct sockaddr_in*)(const void*)a->opaque;
        const struct sockaddr_in* y = (const struct sockaddr_in*)(const void*)b->opaque;
        if (x->sin_family != AF_INET || y->sin_family != AF_INET) return 0;
        return x->sin_addr.s_addr == y->sin_addr.s_addr;
    }
}

/* ---------------------------------------------------------------------------- traffic */

/* THIS MACHINE'S OWN IPv4 ADDRESSES. See the note in net_udp.h for why this is here.

   POSIX reads the interface list directly, which is the whole answer. WIN32 asks the
   resolver for its own hostname instead, which is a WEAKER answer and is chosen anyway:
   the full form needs iphlpapi and a second library on the link line, and this is a
   convenience print rather than anything the match depends on. It can return fewer
   addresses than the machine has, and the caller must not treat an empty list as "this
   machine is not on a network". */
int net_local_addrs(char out[][64], int max)
{
    int n = 0;
    if (max <= 0) return 0;
#if defined(_WIN32)
    {
        char host[256];
        struct addrinfo hints, *res = NULL, *it;
        if (gethostname(host, (int)sizeof host) != 0) return 0;
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(host, NULL, &hints, &res) != 0) return 0;
        for (it = res; it != NULL && n < max; it = it->ai_next) {
            struct sockaddr_in* a = (struct sockaddr_in*)it->ai_addr;
            unsigned long h = ntohl(a->sin_addr.s_addr);
            if ((h >> 24) == 127) continue;
            sprintf(out[n], "%lu.%lu.%lu.%lu", (h >> 24) & 255ul, (h >> 16) & 255ul,
                    (h >> 8) & 255ul, h & 255ul);
            n++;
        }
        freeaddrinfo(res);
    }
#else
    {
        struct ifaddrs* list = NULL;
        struct ifaddrs* it;
        if (getifaddrs(&list) != 0) return 0;
        for (it = list; it != NULL && n < max; it = it->ifa_next) {
            struct sockaddr_in* a;
            unsigned long h;
            if (it->ifa_addr == NULL) continue;
            if (it->ifa_addr->sa_family != AF_INET) continue;
            if ((it->ifa_flags & IFF_UP) == 0) continue;
            a = (struct sockaddr_in*)it->ifa_addr;
            h = (unsigned long)ntohl(a->sin_addr.s_addr);
            if ((h >> 24) == 127) continue;   /* loopback is not what a joiner types */
            sprintf(out[n], "%lu.%lu.%lu.%lu", (h >> 24) & 255ul, (h >> 16) & 255ul,
                    (h >> 8) & 255ul, h & 255ul);
            n++;
        }
        freeifaddrs(list);
    }
#endif
    return n;
}

/* THE LAST SEND WAS REFUSED BY THE MACHINE ITSELF, not lost on the way. macOS answers
   EHOSTUNREACH to an app that has not been granted Local Network permission, while the
   host's broadcast advert still arrives, so the game could see a room it could not
   reach and had no way to say why (two days of it, 5-6 Sep 2026). */
/* ONE REFUSED SEND USED TO CONDEMN THE WHOLE PROCESS. This is set when the system
   refuses an outbound datagram (macOS withholds Local Network permission that way) and it
   was never cleared anywhere, while the joiner's poll reads it before doing anything else
   and fails the join with "this computer is not allowed to use the local network".

   So: a player without that permission tries a LAN game and is told the truth; then types
   a friend's INTERNET address, and that join dies instantly on the same sentence -- which
   is now a false diagnosis, because an outbound send to a public address is not what the
   permission gates. They are told to change a setting that has nothing to do with their
   problem, and nothing but restarting the program clears it.

   Cleared when a socket is opened: a socket that has just been created has not had a send
   refused yet, and that is the honest state to start a new attempt from. */
static int s_sendBlocked = 0;
int net_send_blocked(void) { return s_sendBlocked; }
static void net_send_unblock(void) { s_sendBlocked = 0; }

NetSock* net_open_tunnel(const char* tunnel_host, unsigned short tunnel_port,
                         unsigned long local_id)
{
    NetSock* s;
    NetAddr relay;

    /* THE TWO IDS THE PROTOCOL KEEPS. Zero means "addressed to nobody", which is what a
       registration uses, and all ones is the relay's own broadcast. A peer wearing either
       would be talking to the relay rather than through it. */
    if (local_id == TUN_ID_NONE || local_id == TUN_ID_BCAST) return NULL;
    if (!tunnel_host || !*tunnel_host) return NULL;
    memset(&relay, 0, sizeof relay);
    if (net_resolve(tunnel_host, tunnel_port, &relay) != 0) return NULL;

    /* AN EPHEMERAL LOCAL PORT, ALWAYS. A relayed peer is never dialled directly, so it
       has nothing to gain from a fixed port and something to lose: two copies on one
       machine would collide. */
    s = net_open(0);
    if (!s) return NULL;
    s->tunnel = 1;
    s->local_id = local_id;
    s->relay = relay;
    s->last_reg = 0;
    tun_register(s);
    return s;
}

/* THE RELAY'S OWN PING, which is the only way to test our client against a REAL server.
   sender_id 0, receiver_id 0, the datagram exactly 50 bytes: the server replies with the
   first 12 bytes verbatim. It costs the server almost nothing, needs no registration and
   no state, and proves UDP reachability end to end without depending on ICMP.

   WHY THIS EXISTS AT ALL. Our V3 client is complete and its only test is a loopback
   stand-in we wrote ourselves, which cannot disagree with us about the protocol -- it can
   only agree with our reading of it. Everything else planned on top of this transport
   rests on that reading being right. Fifty bytes to a public server settles it.

   The caller supplies the 12 bytes that come back, so a nonce and a timestamp in them
   turn the echo into an RTT measurement with nothing kept on either side. Their rate
   limit is 20 pings per address per minute; this sends one. */
int net_tunnel_ping_send(NetSock* s, const char* host, unsigned short port,
                         const unsigned char nonce[4])
{
    unsigned char pkt[50];
    NetAddr relay;
    int n;
    if (!s || s->fd == SOCKBAD || !host || !*host || !nonce) return -1;
    memset(&relay, 0, sizeof relay);
    if (net_resolve(host, port, &relay) != 0) return -1;
    memset(pkt, 0, sizeof pkt);
    /* BOTH IDS MUST BE ZERO and they must STAY zero. That is what makes this datagram
       addressed to the relay itself rather than through it, and it is the whole test the
       server applies before echoing. The nonce therefore starts at byte 8: the first
       written version put twelve bytes of nonce at offset 0, which overwrote both id
       fields, and a real server said nothing at all. The loopback stand-in in
       gate_tunnel.c had agreed with it, because it was written from the same reading. */
    tun_put_u32(pkt, TUN_ID_NONE);
    tun_put_u32(pkt + 4, TUN_ID_NONE);
    memcpy(pkt + 8, nonce, 4);     /* the only part of the echo we choose */
    n = (int)sendto(s->fd, (const char*)pkt, sizeof pkt, 0,
                    (const struct sockaddr*)(const void*)relay.opaque,
                    (socklen_t)relay.len);
    return n < 0 ? -1 : (int)sizeof pkt;
}

/* The matching read. Returns 1 and fills `echo` when a 12-byte answer arrives, 0 when
   nothing is waiting. The socket is non-blocking, so the caller polls. */
int net_tunnel_ping_seen(const NetSock* s) { return s && s->ping_seen; }

int net_tunnel_ping_recv(NetSock* s, unsigned char echo[12])
{
    unsigned char buf[64];
    NetAddr from;
    int n;
    if (!s || s->fd == SOCKBAD || !echo) return 0;
    memset(&from, 0, sizeof from);
    from.len = (int)sizeof from.opaque;
    n = (int)recvfrom(s->fd, (char*)buf, sizeof buf, 0,
                      (struct sockaddr*)(void*)from.opaque, (socklen_t*)&from.len);
    if (n != 12) return 0;
    memcpy(echo, buf, 12);
    return 1;
}

int net_send(NetSock* s, const NetAddr* to, const void* buf, int len)
{
    int n;
    if (!s || s->fd == SOCKBAD || !to || to->len == 0 || !buf || len <= 0) return -1;
    /* THROUGH THE RELAY, WITH THE PEER'S ID IN FRONT. The caller handed us a peer, not a
       machine; the datagram goes to the relay and the relay decides where that peer is.
       A tunnel socket refuses a plain IP address and the other way about, because the two
       kinds of NetAddr are not interchangeable and a silent mismatch here would be a
       packet that vanishes. */
    if (s->tunnel) {
        /* 1024 BYTES ON THE WIRE, HEADER INCLUDED -- not 1024 of payload. This buffer was
           TUN_HDR + 1024, which puts 1032 bytes on the wire, and the ceiling in the
           protocol is the TOTAL. It matters more than an off-by-eight usually would:
           the legacy V3 server reads into a 1024-byte buffer inside a bare while(true)
           with no try/catch, so on Windows an oversized datagram throws WSAEMSGSIZE,
           faults that task, and by the code takes the whole tunnel down -- for everyone
           using it, not just for us. These are other people's public servers. Payload is
           therefore 1016, which is nowhere near what a turn packet needs. */
        unsigned char pkt[TUN_DGRAM_MAX];
        if (!addr_is_tunnel(to)) return -1;
        if (len > (int)sizeof pkt - TUN_HDR) return -1;
        tun_put_u32(pkt, s->local_id);
        tun_put_u32(pkt + 4, tun_get_u32(to->opaque + 4));
        memcpy(pkt + TUN_HDR, buf, (size_t)len);
        n = (int)sendto(s->fd, (const char*)pkt, (size_t)(len + TUN_HDR), 0,
                        (const struct sockaddr*)(const void*)s->relay.opaque,
                        (socklen_t)s->relay.len);
        return n < 0 ? -1 : len;
    }
    if (addr_is_tunnel(to)) return -1;
    n = (int)sendto(s->fd, (const char*)buf, (size_t)len, 0,
                    (const struct sockaddr*)(const void*)to->opaque, (socklen_t)to->len);
    if (n < 0) {
        /* A SEND THAT FAILS IS A LINE, once a second at most. On a Mac a game that was
           never granted Local Network permission has its unicast to the LAN refused right
           here, silently, while the broadcast advert it RECEIVES still arrives: the first
           real-network test looked exactly like that from both ends. */
        static time_t lastSaid = 0;
        const time_t now = time(NULL);
#if !defined(_WIN32)
        if (errno == EHOSTUNREACH || errno == EPERM || errno == EACCES) s_sendBlocked = 1;
#endif
        if (now != lastSaid) {
            char txt[64];
#if defined(_WIN32)
            const int e = WSAGetLastError();
            const char* es = "";
#else
            const int e = errno;
            const char* es = strerror(e);
#endif
            lastSaid = now;
            net_addr_text(to, txt, (int)sizeof txt);
            printf("NET|send-failed|to=%s|errno=%d|%s\n", txt, e, es);
            fflush(stdout);
        }
    }
    return n;
}

int net_recv(NetSock* s, NetAddr* from, void* buf, int max)
{
    socklen_t alen;
    int n;
    NetAddr wire;

    if (!s || s->fd == SOCKBAD || !from || !buf || max <= 0) return -1;
    /* THE SLOT IS KEPT BY THE POLL, so a caller that reads its socket never has to know a
       relay is under it. A match polls many times a second and the lobby ten. */
    if (s->tunnel) net_tunnel_keepalive(s);

    /* ZERO MEANS "THE QUEUE IS EMPTY", AND ONLY THAT. It used to mean two things: an
       empty queue, and "that datagram was not for you" -- and every caller in the tree
       drains with `while ((n = net_recv(...)) > 0)`. So on a relayed socket ONE stray
       packet ended the drain and left real traffic sitting in the kernel buffer until the
       next frame. On a public relay a stray is not a rare event; a stranger sweeping the
       id space looks exactly like this from the inside, and the symptom would be a match
       that stutters when somebody else's port scanner runs.

       The refusals now CONTINUE to the next datagram instead of returning. The loop is
       here rather than inside the tunnel branch because the defect was never in the
       branch -- it was one return value carrying two different facts. */
    for (;;) {
        /* THE HEADER NEEDS ROOM OF ITS OWN, AND ASKING THE CALLER FOR IT WOULD BE A LIE.
           A caller offers a buffer the size of the largest packet it will ever accept, and
           through a relay the datagram on the wire is that plus eight bytes of routing it
           is never going to see. Handing recvfrom the caller's size means a full packet
           does not fit.
           WHAT THAT COSTS DEPENDS ON THE PLATFORM, WHICH IS WHY TWO MACS COULD NOT FIND
           IT. BSD truncates the datagram and reports the truncated length, so the packet
           arrives corrupt and is dropped later as malformed. Winsock refuses the call
           outright with WSAEMSGSIZE, which is neither of the two codes forgiven below, so
           the receive reports a hard error. Either way the packet is lost every time it is
           sent, and a turn that never arrives is a match that stops rather than a frame
           that stutters.
           The staging buffer is the RELAY's own ceiling, not the caller's: no legitimate
           relayed datagram can exceed it, so nothing is truncated here either. */
        unsigned char stage[TUN_DGRAM_MAX];
        void* dst = s->tunnel ? (void*)stage : buf;
        int cap = s->tunnel ? (int)sizeof stage : max;
        memset(from, 0, sizeof *from);
        alen = (socklen_t)sizeof from->opaque;
#if defined(NET_HAVE_RX_STAMPS)
        if (s->rx_stamps)
            n = recv_stamped(s, dst, cap, from, &alen);
        else
#endif
        n = (int)recvfrom(s->fd, (char*)dst, (size_t)cap, 0,
                          (struct sockaddr*)(void*)from->opaque, &alen);
        if (n < 0) {
    #if defined(_WIN32)
            int e = WSAGetLastError();
            /* WSAECONNRESET on a UDP socket means an earlier send drew an ICMP port
               unreachable, which happens routinely while a peer is still starting up. It is
               not an error in this socket and must not be reported as one. */
            if (e == WSAEWOULDBLOCK || e == WSAECONNRESET) return 0;
    #else
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
            if (errno == ECONNREFUSED) return 0;   /* same ICMP case as above */
    #endif
            return -1;
    }
        from->len = (int)alen;

        /* ---- through a relay, unwrap and say WHO rather than WHERE --------------------
           Three refusals, and each is a real thing that arrives on this socket:

           anything not from the relay      the relay is the only correspondent a tunnelled
                                            peer has, so a datagram from elsewhere is either
                                            a stray or somebody probing the port;
           anything shorter than a header   the relay's own reachability reply is twelve
                                            bytes and is not a peer speaking;
           anything addressed elsewhere     the relay does not check that a sender is
                                            entitled to reach a receiver, so a packet whose
                                            receiver is not us is not ours to act on.

           What survives is handed up with `from` naming the SENDER'S ID. Everything above
           this file then goes on identifying peers by address exactly as it does on a LAN --
           which is the whole reason the relay needs no code up there. */
        if (s->tunnel) {
            unsigned long to_id;
            wire = *from;
            if (!net_addr_equal(&wire, &s->relay)) continue;
            if (n < TUN_HDR) continue;
            to_id = tun_get_u32(stage + 4);
            /* THE RELAY'S OWN ANSWER, which is not a peer speaking: exactly twelve bytes
               echoing our ping's first twelve, so both id fields read zero. Noted and
               swallowed. Without this arm it is dropped one line below as "addressed to
               somebody else" and nothing ever learns the relay is alive -- which is the
               difference between telling a player their room code is wrong and telling
               them this computer cannot reach the relay at all. */
            if (n == 12 && to_id == TUN_ID_NONE
                && tun_get_u32(stage) == TUN_ID_NONE) {
                s->ping_seen = 1;
                continue;
            }
            if (to_id != s->local_id) continue;
            n -= TUN_HDR;
            net_addr_tunnel(from, tun_get_u32(stage));
            /* BOUNDED BY WHAT THE CALLER OFFERED, not by what arrived. A relay forwards
               whatever it is given, so a peer speaking a wire this build does not know
               could hand us more payload than the caller has room for; that is a packet
               to drop, not a buffer to overrun. */
            if (n > max) continue;
            if (n > 0) memcpy(buf, stage + TUN_HDR, (size_t)n);
        }
        return n;
    }
}
