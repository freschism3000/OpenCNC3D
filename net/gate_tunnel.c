/* gate_tunnel: the relayed transport, against a relay small enough to read.
 *
 * WHY THIS GATE EXISTS. An internet match is carried by a CnCNet tunnel, and every
 * property the match layer depends on has to survive that: a peer must still be told WHO
 * a packet came from when every packet arrives from one relay address, the payload must
 * come out the size it went in, and the two kinds of address must not be silently
 * interchangeable. None of that is visible from a LAN test, and none of it can be tested
 * against a public relay in a suite that has to run offline.
 *
 * THE RELAY BELOW IS THE WHOLE PROTOCOL. Read the eight byte header, remember that this
 * sender is at this address, forward to the receiver if we have seen it. The real server
 * does that and nothing else, so a peer that works here works there for every reason
 * except reachability, which no gate can answer anyway.
 */
#include "net_udp.h"
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET rsock_t;
  #define RBAD INVALID_SOCKET
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <unistd.h>
  #include <fcntl.h>
  typedef int rsock_t;
  #define RBAD (-1)
#endif

static int fails = 0;
static void check(int ok, const char* what)
{
    printf("%s %s\n", ok ? "OK  " : "FAIL", what);
    if (!ok) fails++;
}

static unsigned long ru32(const unsigned char* p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8)
         | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

/* THE MIRROR OF ru32, and it exists because writing one by hand got it backwards. The
   tunnel header is LITTLE-endian (docs/cncnet-tunnel-protocol.md 1.2, and net_udp.c's
   tun_put_u32); a big-endian header hand-written here byte-swaps the receiver id, the
   real code correctly refuses the packet, and the gate then reports a fault in the code
   rather than in itself. One writer beside the one reader. */
static void gt_nap(int ms)
{
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)ms * 1000);
#endif
}

static void wu32(unsigned char* p, unsigned long v)
{
    p[0] = (unsigned char)(v & 0xFFul);
    p[1] = (unsigned char)((v >> 8) & 0xFFul);
    p[2] = (unsigned char)((v >> 16) & 0xFFul);
    p[3] = (unsigned char)((v >> 24) & 0xFFul);
}

/* The relay's whole memory: an id and where it was last seen. */
#define MAXPEER 8
static unsigned long r_id[MAXPEER];
static struct sockaddr_in r_at[MAXPEER];
static int r_n = 0;

/* PUT A DATAGRAM STRAIGHT INTO A PEER'S QUEUE, addressed to whoever we like. This is what
   a stray looks like from inside a relayed socket: the relay does not check that a sender
   is entitled to reach a receiver, so a peer sees traffic meant for other people, and on a
   PUBLIC relay a stranger sweeping the id space produces exactly this. Returns 1 if the
   peer's address was known. */
static int relay_inject(rsock_t rs, unsigned long to_peer, unsigned long claim_rcv,
                        const char* payload, int len)
{
    unsigned char pkt[256];
    int i, k = -1;
    for (i = 0; i < r_n; i++) if (r_id[i] == to_peer) k = i;
    if (k < 0 || len < 0 || len > (int)sizeof pkt - 8) return 0;
    wu32(pkt, 0xDEADBEEFul);
    wu32(pkt + 4, claim_rcv);
    if (len) memcpy(pkt + 8, payload, (size_t)len);
    sendto(rs, (const char*)pkt, (size_t)(len + 8), 0,
           (const struct sockaddr*)&r_at[k], (socklen_t)sizeof r_at[k]);
    return 1;
}

static void relay_pump(rsock_t rs)
{
    unsigned char buf[2048];
    struct sockaddr_in from;
    socklen_t flen = sizeof from;
    int n, i, k;
    for (;;) {
        n = (int)recvfrom(rs, (char*)buf, sizeof buf, 0, (struct sockaddr*)&from, &flen);
        if (n < 8) return;
        {
            const unsigned long snd = ru32(buf);
            const unsigned long rcv = ru32(buf + 4);
            /* THE PING, AND WHY THIS STAND-IN NOW ENFORCES IT RATHER THAN ASSUMING IT.
               A real V3 server treats a datagram of EXACTLY 50 bytes with BOTH ids zero
               as a ping and answers with the request's first twelve bytes. This harness
               used to know nothing about that, and the cost of it was concrete: the first
               ping this project ever sent put its nonce at offset 0, which overwrote both
               id fields, and every public server ignored it in silence. Nothing here
               objected, because a stand-in written from the same reading of the protocol
               as the client can only ever agree with the client.
               So the ids are CHECKED. A 50-byte datagram whose ids are not both zero is
               dropped rather than forwarded, exactly as a real server drops it, and the
               gate below can therefore fail on a malformed ping instead of shrugging. */
            if (n == 50) {
                if (snd == 0ul && rcv == 0ul) {
                    sendto(rs, (const char*)buf, 12, 0,
                           (const struct sockaddr*)&from, (socklen_t)sizeof from);
                }
                continue;
            }
            k = -1;
            for (i = 0; i < r_n; i++) if (r_id[i] == snd) k = i;
            if (k < 0 && r_n < MAXPEER) { k = r_n++; r_id[k] = snd; }
            if (k >= 0) r_at[k] = from;              /* last seen here */
            if (rcv == 0) continue;                   /* a registration, forward nothing */
            for (i = 0; i < r_n; i++)
                if (r_id[i] == rcv)
                    sendto(rs, (const char*)buf, (size_t)n, 0,
                           (const struct sockaddr*)&r_at[i], (socklen_t)sizeof r_at[i]);
        }
    }
}

int main(void)
{
    rsock_t rs;
    struct sockaddr_in ra;
    NetSock *a, *b;
    NetAddr aid, bid, from;
    unsigned char got[256];
    int n, i;
    u_long nb = 1;

    if (net_startup() != 0) { printf("no networking\n"); return 1; }

    rs = socket(AF_INET, SOCK_DGRAM, 0);
    if (rs == RBAD) { printf("no relay socket\n"); return 1; }
    memset(&ra, 0, sizeof ra);
    ra.sin_family = AF_INET;
    ra.sin_addr.s_addr = htonl(0x7F000001ul);
    ra.sin_port = htons(51999);
    if (bind(rs, (struct sockaddr*)&ra, sizeof ra) != 0) { printf("no bind\n"); return 1; }
    /* THE RELAY SOCKET GETS A RECEIVE TIMEOUT, ON BOTH PLATFORMS, and that one choice
       answers two faults at once.

       relay_pump reads until recvfrom answers with less than a header, and treats that
       as "nothing left to forward". Only the Windows non-blocking call was set here, so
       on macOS and Linux the socket blocked: the first drained queue parked the process
       in recvfrom for ever, with no output and no timeout. It stopped a release suite
       dead at ten minutes and would have stopped every one after it.

       Making it merely NON-blocking fixes the hang and leaves a race in its place: the
       pump is called immediately after a peer sends, and an empty queue at that instant
       means the packet is still in flight rather than absent. Four checks then failed
       depending on how the two ends were scheduled.

       A timeout is what the pump actually wants: wait a short while for a packet, and
       give up when none comes. Two hundred milliseconds is far longer than loopback
       needs and far shorter than a person would notice. */
    {
#if defined(_WIN32)
        DWORD tv = 200;
#else
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
#endif
        if (setsockopt(rs, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) != 0) {
            printf("no relay receive timeout\n");
            return 1;
        }
    }
    (void)nb;

    a = net_open_tunnel("127.0.0.1", 51999, 0x1111u);
    b = net_open_tunnel("127.0.0.1", 51999, 0x2222u);
    check(a != NULL && b != NULL, "two peers open a relayed socket");
    check(net_is_tunnel(a) && net_is_tunnel(b), "and both report they are relayed");

    /* The relay learns both peers from the registration net_open_tunnel already sent. */
    relay_pump(rs);
    check(r_n == 2, "the relay learned both peers from their registrations alone");

    net_addr_tunnel(&aid, 0x1111u);
    net_addr_tunnel(&bid, 0x2222u);
    check(!net_addr_equal(&aid, &bid), "two peer ids are two different addresses");
    {
        NetAddr again;
        net_addr_tunnel(&again, 0x1111u);
        check(net_addr_equal(&aid, &again), "and the same id is the same address");
    }

    /* A sends an order to B. */
    n = net_send(a, &bid, "ORDER-22-BYTES-PAYLOAD", 22);
    check(n == 22, "a peer sends through the relay");
    relay_pump(rs);

    memset(got, 0, sizeof got);
    n = 0;
    for (i = 0; i < 50 && n <= 0; i++) n = net_recv(b, &from, got, sizeof got);
    check(n == 22, "the other peer receives exactly the payload, header stripped");
    check(memcmp(got, "ORDER-22-BYTES-PAYLOAD", 22) == 0, "and the bytes are unchanged");
    check(net_addr_equal(&from, &aid), "and it is told WHO sent it, not where from");
    check(!net_addr_equal(&from, &bid), "which is not itself");

    {
        char txt[64];
        net_addr_text(&from, txt, sizeof txt);
        check(strcmp(txt, "tunnel:4369") == 0, "a relayed peer prints as its id");
    }

    /* A FULL PACKET, WHICH IS THE ONE SIZE THAT MATTERS AND THE ONE NOBODY SENT.
     *
     * Every leg above moves twenty-two bytes, and at that size the header has spare room
     * whatever the receive path does with it. A real match's largest turn does not: the
     * scheduler's ceiling is the relay's own 1024 minus the eight byte header, so a full
     * turn is EXACTLY the datagram that has no slack left.
     *
     * That is where the two platforms stop agreeing. Handed a buffer eight bytes short,
     * BSD truncates and reports the short length, so the packet arrives corrupt; Winsock
     * refuses the call outright with a code the receive path does not forgive, so the
     * packet is lost every time it is sent. A match whose big turns never arrive stops
     * dead, and nothing smaller than a full turn would ever show it.
     *
     * The payload is patterned rather than zeroed so a truncation cannot pass as a
     * success: the last byte is the one a short read loses first. */
    {
        static unsigned char big[1016];
        static unsigned char back[1016];
        int k;
        for (k = 0; k < (int)sizeof big; k++) big[k] = (unsigned char)(k * 7 + 3);

        n = net_send(a, &bid, big, (int)sizeof big);
        check(n == (int)sizeof big, "a peer sends a FULL packet, 1016 plus the header");
        relay_pump(rs);

        memset(back, 0, sizeof back);
        n = 0;
        for (i = 0; i < 50 && n <= 0; i++) n = net_recv(b, &from, back, (int)sizeof back);
        check(n == (int)sizeof big, "a full packet arrives whole, not truncated or refused");
        check(memcmp(back, big, sizeof big) == 0, "and every byte of it survived");
    }

    /* A plain IP address must not be sendable on a relayed socket, and the other way. */
    {
        NetAddr ip;
        NetSock* plain = net_open(0);
        memset(&ip, 0, sizeof ip);
        net_resolve("127.0.0.1", 51999, &ip);
        check(net_send(a, &ip, "x", 1) < 0, "a relayed socket refuses a plain address");
        check(plain && net_send(plain, &bid, "x", 1) < 0,
              "and a plain socket refuses a peer id");
        if (plain) net_close(plain);
    }

    /* THE PING, ROUND TRIP, THROUGH A RELAY THAT CHECKS IT. The relay above answers a
       50-byte datagram only when both ids are zero, which is the rule a real server
       applies -- so this leg fails on a malformed ping rather than echoing it back
       politely. That is the whole point: the first ping this project sent was malformed
       and two public servers ignored it, while a stand-in that did not check would have
       called it green.
       The echo is the request's first twelve bytes, so the eight zero id bytes come back
       too and only bytes 8..11 are ours. Asserting the zeros matters as much as asserting
       the nonce: they are what made it a ping. */
    {
        NetSock* p = net_open(0);
        unsigned char nonce[4], echo[12], want[12];
        int got = 0, i;
        for (i = 0; i < 4; i++) nonce[i] = (unsigned char)(0x5A ^ (i * 17 + 3));
        memset(want, 0, sizeof want);
        memcpy(want + 8, nonce, 4);
        check(p != NULL, "a plain socket opens for the ping");
        if (p) {
            check(net_tunnel_ping_send(p, "127.0.0.1", 51999, nonce) == 50,
                  "the relay ping is exactly 50 bytes on the wire");
            for (i = 0; i < 200 && !got; i++) {
                relay_pump(rs);
                got = net_tunnel_ping_recv(p, echo);
            }
            check(got == 1, "the relay answers a well formed ping");
            check(got && memcmp(echo, want, 12) == 0,
                  "and echoes the first twelve bytes verbatim, zero ids included");
            net_close(p);
        }
    }

    /* A STRAY MUST NOT END THE DRAIN. Every caller in the tree reads its socket with
       `while ((n = net_recv(...)) > 0)`, and net_recv used to answer 0 for two different
       facts: an empty queue, and "that datagram was not for you". So one packet addressed
       to somebody else stopped the drain and left real traffic sitting in the kernel
       buffer until the next frame. On a public relay that is not a rare event -- it is
       what a stranger sweeping the id space looks like from in here -- and the symptom
       would have been a match that stutters when somebody else's scanner runs.

       Two datagrams, in this order: one for a stranger, then one for us. The good one has
       to arrive from a drain written the way every real caller writes it. */
    {
        unsigned char rx[64];
        NetAddr rfrom;
        int drained = 0, got_good = 0, m;
        check(relay_inject(rs, 0x2222u, 0xBADBAD01ul, "NOTYOURS", 8) == 1,
              "a datagram for a stranger can be put in front of the peer");
        relay_inject(rs, 0x2222u, 0x2222u, "MINE", 4);
        gt_nap(150);          /* both are in the kernel's queue before the drain starts */
        /* EXACTLY ONE DRAIN, because that is what a real caller does: once per frame,
           with the standard `while (> 0)` idiom, and then it goes off to render. Looping
           until the packet turns up would pass against the old code too -- the defect was
           never that traffic was LOST, it was that it sat in the buffer for a frame every
           time somebody else's packet arrived first. A retry loop hides exactly that, and
           this leg was written with one at first and proved nothing. */
        while ((m = net_recv(b, &rfrom, rx, (int)sizeof rx)) > 0) {
            drained++;
            if (m == 4 && memcmp(rx, "MINE", 4) == 0) got_good = 1;
        }
        check(got_good == 1,
              "and in ONE drain the real one behind it still arrives: a refused datagram "
              "continues the drain instead of reporting the queue empty");
        check(drained == 1, "while the stranger's is not handed up at all");
    }

    /* THE RELAY'S ANSWER IS NOTICED ON A MATCH SOCKET TOO, not only on a bare one. The
       echo arrives among the match traffic, and net_recv is the only thing reading that
       socket -- a caller with its own recvfrom would be racing the match for datagrams.
       So the transport latches it and the match layer just asks. This is what lets a
       joiner tell "this computer cannot reach the relay" from "nothing answers that room
       code", which are otherwise the same ten seconds of silence. */
    {
        unsigned char nonce2[4], rx2[64];
        NetAddr f2;
        int guard;
        for (guard = 0; guard < 4; guard++) nonce2[guard] = (unsigned char)(0x33 + guard);
        check(net_tunnel_ping_seen(a) == 0, "a socket has not heard from the relay yet");
        check(net_tunnel_ping_send(a, "127.0.0.1", 51999, nonce2) == 50,
              "a match socket can ask the relay whether it is there");
        for (guard = 0; guard < 60 && !net_tunnel_ping_seen(a); guard++) {
            relay_pump(rs);
            while (net_recv(a, &f2, rx2, (int)sizeof rx2) > 0) { /* drain as a match does */ }
        }
        check(net_tunnel_ping_seen(a) == 1,
              "and the answer is noticed by the ordinary drain, not by a second reader");
    }

    net_close(a);
    net_close(b);
    net_shutdown();
    printf("%s: %d failure(s)\n", fails ? "TUNNEL FAILED" : "TUNNEL OK", fails);
    return fails ? 1 : 0;
}
