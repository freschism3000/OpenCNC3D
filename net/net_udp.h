/*
 * net_udp.h -- the transport seam. One UDP socket, and the smallest surface that lets
 * everything above it stay portable.
 *
 * WHY THERE IS A SEAM AT ALL. This mirrors the discipline the renderer already keeps
 * between scene assembly and the graphics API: the rules live above the seam and the
 * platform lives below it. lockstep.c has no socket in it and can therefore be tested
 * in one process with no network; this file has no scheduling in it and can therefore be
 * replaced without anything above it noticing. The replacement is not hypothetical: a
 * relayed connection and a Windows 98 build both want a different thing underneath, and
 * declaring the seam now is what keeps that from becoming a rewrite.
 *
 * WHY UDP AND NOT TCP. Lockstep sends a small fixed packet every turn and covers loss by
 * repeating recent turns inside the next packet, so it needs no ordering and no
 * retransmission from the transport. TCP would supply both, and would also supply head of
 * line blocking, which converts one lost packet into a stall for every turn behind it.
 * The redundancy this design already carries makes that trade strictly bad.
 *
 * THERE IS NO ENCRYPTION HERE, deliberately. A 22 byte order carries no secret, and both
 * of the transports that mandate encryption cost a TLS stack that the Tier 1 target
 * cannot host. What is exposed by not having it is each peer's address, which a relay is
 * the answer to rather than a cipher.
 */
#ifndef CNC3D_NET_UDP_H
#define CNC3D_NET_UDP_H


/* THE RENDERER IS C++ AND THIS FILE IS C, so the declarations below have to be marked as
   C linkage or the C++ compiler mangles every name in them and the link fails on symbols
   that are plainly present in the object file. It is written now rather than on the day
   the host first calls one of these, because that day the failure appears in a binary
   nobody was editing: the standalone renderer already links these objects and would keep
   building, while the app, which does not link them, breaks. Two lines here cost nothing
   and remove that entirely. */
#ifdef __cplusplus
extern "C" {
#endif

/* An opaque peer address. Held as raw bytes so that no caller has to include a platform
   sockets header, which is what lets the host and the tests stay clean of winsock. Big
   enough for a sockaddr_in6 so the same struct survives an IPv6 answer later. */
typedef struct {
    unsigned char opaque[32];
    int           len;      /* 0 when this address is unset */
} NetAddr;

typedef struct NetSock NetSock;

/* Bring the platform's networking up and take it down again. On Windows this is
   WSAStartup and WSACleanup and it MUST bracket everything else; everywhere else it is a
   no op that exists so the caller does not need to know that. Returns 0 on success. */
int  net_startup(void);
void net_shutdown(void);

/* Open a non blocking UDP socket. `port` is the local port to bind, or 0 to let the
   system choose, which is what a joining peer wants. Returns NULL on failure. */
NetSock* net_open(unsigned short port);
void     net_close(NetSock* s);

/* The port actually bound, which is the useful answer after asking for 0. */
unsigned short net_local_port(const NetSock* s);

/* Resolve a host and port into an address. `host` may be a name or a dotted address.
   Returns 0 on success. This is the ONLY blocking call in this file, because a name
   lookup can take a moment; it is called from the lobby and never from the frame loop. */
int net_resolve(const char* host, unsigned short port, NetAddr* out);

/* Format an address back into text, for the lobby and for logs. `out` needs 64 bytes. */
void net_addr_text(const NetAddr* a, char* out, int outmax);

/* Whether two addresses name the same peer. Used to map an arriving packet to a seat. */
int net_addr_equal(const NetAddr* a, const NetAddr* b);

/* Send one datagram. Returns the bytes sent, or negative on failure. A UDP send that
   fails is not fatal to a lockstep match: the next turn's packet carries this turn's
   orders again, which is the same property that covers loss in the network. */
int net_send(NetSock* s, const NetAddr* to, const void* buf, int len);

/* Take one datagram if one is waiting. Returns its length, 0 when nothing is queued, or
   negative on a real error. Never blocks, so the frame loop can drain it with a while
   and carry on rendering. */
int net_recv(NetSock* s, NetAddr* from, void* buf, int max);

#ifdef __cplusplus
}   /* extern "C" */
#endif

#endif /* CNC3D_NET_UDP_H */
