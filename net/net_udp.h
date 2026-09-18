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

/* ---- the same socket, through a relay ---------------------------------------------
 *
 * WHY THIS IS HERE AND NOT IN A FILE OF ITS OWN. Everything above this seam addresses a
 * peer by NetAddr and asks nothing about what one contains -- so if a NetAddr can name a
 * peer THROUGH a relay as easily as it names one on a LAN, the match layer, the lobby and
 * the scheduler need no relay code at all. That is what these four calls buy, and it is
 * why the identity problem a relay usually creates does not arise here: the host still
 * tells joiners apart by the address a packet came from, and that address is now the
 * peer's tunnel id rather than its IP.
 *
 * THE PROTOCOL IS CnCNet's TUNNEL V3, and docs/cncnet-tunnel-protocol.md is the wire
 * written down from the server source rather than from recollection. The whole of it:
 * eight bytes in front of every datagram, a sender id and a receiver id, both little
 * endian; a peer registers itself by sending its own id with a receiver of zero, and
 * keeps its slot by repeating that every fifteen seconds; the relay holds nothing but
 * "this id was last seen at this address" and forwards the rest untouched.
 *
 * IDS ARE CHOSEN, NOT ISSUED. The relay never allocates one. The host draws an id for
 * every seat and hands the table out over the lobby, which it can do because the lobby
 * already carries the roster.
 *
 * ONE SOCKET FOR EVERYTHING, and that is a requirement rather than a tidiness: the relay
 * remembers the address a registration arrived from, so game traffic leaving a different
 * socket comes from an address it does not know and is dropped until the old mapping
 * expires -- thirty to sixty seconds of one way silence that retrying cannot shorten. */

/* Open a socket that speaks to peers through a relay. `tunnel_host` is the relay's
 * address and `tunnel_port` its port; `local_id` is this peer's chosen id, which must not
 * be 0 or 0xFFFFFFFF and must differ from every other peer's in the match.
 * Registers immediately and keeps registering for as long as the socket is open. */
NetSock* net_open_tunnel(const char* tunnel_host, unsigned short tunnel_port,
                         unsigned long local_id);

/* Address a peer by its tunnel id. The result is a NetAddr like any other: it compares,
 * prints and travels through net_send exactly as an IP one does, and only this file ever
 * knows the difference. */
void net_addr_tunnel(NetAddr* out, unsigned long peer_id);

/* Non zero when this socket carries its traffic through a relay. */
int net_is_tunnel(const NetSock* s);

/* Send the keepalive if one is due. Called from net_recv, so a caller that polls its
 * socket keeps its slot without knowing a relay exists; exposed for a caller that goes
 * quiet for a while and wants to be explicit about it. */
void net_tunnel_keepalive(NetSock* s);

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
/* Whether two addresses are the same SENDER as far as this end can tell: the same IP
   whatever the port, or, through a relay, the same tunnel id, which is all a relayed
   datagram says about where it came from. For counting a sender that can open any number
   of sockets as one. */
int net_addr_same_host(const NetAddr* a, const NetAddr* b);
/* Non-zero when the last send was refused because this machine is not allowed to reach
   the local network (macOS answers EHOSTUNREACH to an app without that permission). */
int net_send_blocked(void);

/* Send one datagram. Returns the bytes sent, or negative on failure. A UDP send that
   fails is not fatal to a lockstep match: the next turn's packet carries this turn's
   orders again, which is the same property that covers loss in the network. */
/* THIS MACHINE'S OWN ADDRESSES, so a host can print the command the other players type.
   Writes up to `max` dotted-quad strings into `out`, each at most 63 characters plus a
   terminator, skipping loopback and anything that is not IPv4, and returns how many it
   wrote. It exists because the single most common way a LAN match fails to happen is
   nobody being sure which address the host is on. Best effort by design: a machine with
   no address, or a platform where the list cannot be read, returns 0 and the caller says
   so rather than guessing. */
int net_local_addrs(char out[][64], int max);

/* ---- the relay's own ping ----------------------------------------------------------
   A 50-byte datagram addressed to the relay itself rather than through it; the server
   echoes the first 12 bytes. It is the only way to test this client against a REAL
   server: the loopback stand-in in gate_tunnel.c cannot disagree with us about the
   protocol, only agree with our reading of it. Put a nonce and a timestamp in the twelve
   and the echo is an RTT measurement with no state kept anywhere.
   The socket may be an ordinary one -- no registration is involved. Their rate limit is
   twenty pings per address per minute.
   The nonce is FOUR bytes, not twelve, and that is the protocol's doing: the echo is the
   first twelve bytes of the request, and the first eight of those are the two id fields,
   which must both be zero or the server does not treat it as a ping at all. So bytes
   8..11 are the only part of the answer we get to choose. */
int net_tunnel_ping_send(NetSock* s, const char* host, unsigned short port,
                         const unsigned char nonce[4]);
/* 1 and fills `echo` when the 12-byte answer has arrived, 0 when nothing is waiting. For
   a PLAIN socket doing nothing else -- netcheck's relay mode. */
int net_tunnel_ping_recv(NetSock* s, unsigned char echo[12]);
/* Has the relay answered a ping on THIS socket? For a match socket, where the echo
   arrives among the match traffic and net_recv is the only thing reading it: a caller
   with its own recvfrom would be racing the match for datagrams. Latched, so it can be
   asked long after the answer came. */
int net_tunnel_ping_seen(const NetSock* s);

/* ---------------------------------------------------------------- LAN discovery ----- *
 *  A host announces itself by broadcasting; a browser listens. Three primitives, and
 *  they are separate from net_open because each one needs a socket option that would be
 *  wrong on a match socket.
 *
 *  net_open_shared   bind `port` with the address reusable, so SEVERAL processes on one
 *                    machine can all listen for beacons. Without it the second browser on
 *                    a machine cannot open the discovery port at all, and testing this
 *                    feature means two copies of the game on one desk.
 *  net_set_broadcast let a socket send to the broadcast address. Off by default on every
 *                    platform, and a send to 255.255.255.255 without it simply fails.
 *  net_broadcast_addr fill `out` with 255.255.255.255:port.
 *
 *  ALL THREE ARE BEST EFFORT AND SAY SO: a network that drops broadcast traffic, or a
 *  platform that refuses the option, leaves the browser with an empty list rather than an
 *  error, so the caller must never report "no games" as "the network is broken".
 * ------------------------------------------------------------------------------------ */
NetSock* net_open_shared(unsigned short port);
int net_set_broadcast(NetSock* s, int on);
/* EVERY BROADCAST ADDRESS THIS MACHINE HAS, one per interface that can broadcast, and
   NOT 255.255.255.255.

   That distinction was measured rather than assumed. On macOS a send to 255.255.255.255
   fails outright, returning -1, while a send to the interface's own subnet broadcast
   (192.168.1.255) is delivered and arrives at a listener on the same machine FROM that
   machine's LAN address. So the per-interface form is both the one that works and the one
   that makes a browser on the host's own machine list the host at the address a joiner
   should actually use.

   Returns how many it wrote. If a machine has no broadcast-capable interface at all this
   falls back to 127.0.0.1, so hosting and browsing on one disconnected laptop still
   finds itself. */
int net_broadcast_addrs(unsigned short port, NetAddr* out, int max);

int net_send(NetSock* s, const NetAddr* to, const void* buf, int len);

/* Take one datagram if one is waiting. Returns its length, 0 when nothing is queued, or
   negative on a real error. Never blocks, so the frame loop can drain it with a while
   and carry on rendering. */
int net_recv(NetSock* s, NetAddr* from, void* buf, int max);

/* ---- WHEN A DATAGRAM ARRIVED, not when somebody got round to reading it ------------
   A caller that reads its socket once a frame learns about a datagram up to a frame after
   it landed, and anything it times from that moment carries the frame in it. Measured
   with a room on loopback: a prober read once every 8 ms timed every answer at 8 ms and
   one read every 16 ms at 16, whatever the real round trip was, and the least of several
   readings could not remove it because every reading carried the same gap.

   So a socket can ask the system to stamp each datagram as it arrives. OFF BY DEFAULT and
   off on every match socket: switching it on changes how this file reads that socket,
   and nothing that reads a match's traffic needs a stamp.

   net_set_rx_stamps  1 when this socket's datagrams will now be stamped, 0 when this
                      platform cannot (Windows, here), and the caller times on its own.
   net_last_rx_time   the arrival of the datagram the last net_recv on this socket
                      returned, as the system clock's seconds and microseconds (the clock
                      gettimeofday reads). 0 when there is no stamp for it. */
int net_set_rx_stamps(NetSock* s, int on);
int net_last_rx_time(const NetSock* s, long* sec, long* usec);

#ifdef __cplusplus
}   /* extern "C" */
#endif

#endif /* CNC3D_NET_UDP_H */
