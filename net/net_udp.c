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
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  #define CLOSESOCK close
  #define SOCKBAD   (-1)
  typedef int sock_t;
#endif

struct NetSock {
    sock_t         fd;
    unsigned short port;
};

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

NetSock* net_open(unsigned short port)
{
    static NetSock pool[4];
    static int used = 0;
    NetSock* s;
    struct sockaddr_in a;
    socklen_t alen;

    /* A fixed pool rather than malloc: this file is linked into a host that already
       avoids allocation in its frame path, and a match needs one socket. Four is room for
       a match, a lobby probe and a spare without ever asking the allocator. */
    if (used >= (int)(sizeof pool / sizeof pool[0])) return 0;
    s = &pool[used];

    s->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (s->fd == SOCKBAD) return 0;

    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(s->fd, (struct sockaddr*)&a, (socklen_t)sizeof a) != 0) {
        CLOSESOCK(s->fd);
        return 0;
    }
    if (set_nonblocking(s->fd) != 0) {
        CLOSESOCK(s->fd);
        return 0;
    }

    /* Ask the socket what it actually got, which is the point of passing 0. */
    alen = (socklen_t)sizeof a;
    if (getsockname(s->fd, (struct sockaddr*)&a, &alen) == 0) {
        s->port = ntohs(a.sin_port);
    } else {
        s->port = port;
    }

    used++;
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

/* ---------------------------------------------------------------------------- traffic */

int net_send(NetSock* s, const NetAddr* to, const void* buf, int len)
{
    int n;
    if (!s || s->fd == SOCKBAD || !to || to->len == 0 || !buf || len <= 0) return -1;
    n = (int)sendto(s->fd, (const char*)buf, (size_t)len, 0,
                    (const struct sockaddr*)(const void*)to->opaque, (socklen_t)to->len);
    return n;
}

int net_recv(NetSock* s, NetAddr* from, void* buf, int max)
{
    socklen_t alen;
    int n;

    if (!s || s->fd == SOCKBAD || !from || !buf || max <= 0) return -1;
    memset(from, 0, sizeof *from);
    alen = (socklen_t)sizeof from->opaque;
    n = (int)recvfrom(s->fd, (char*)buf, (size_t)max, 0,
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
    return n;
}
