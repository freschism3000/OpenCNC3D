/*
 * lockstep.h -- the turn scheduler, and nothing else.
 *
 * WHAT LOCKSTEP IS HERE. Every peer runs its own copy of the engine and they stay
 * identical because they execute the same orders on the same frames. So the only thing
 * that crosses the wire is orders, and the only job of this file is to answer one
 * question for the host's main loop: MAY FRAME N EXECUTE YET. It may, once every peer
 * has said what it wants done on frame N, including saying nothing.
 *
 * WHY THIS FILE HAS NO SOCKETS IN IT. The scheduler is where the correctness lives and
 * the socket is where the platform lives, so they are separated: everything below is
 * portable C89 with no I/O at all, driven by ls_local_order / ls_pack / ls_on_packet,
 * and net_udp.h owns the file descriptor. That is the same discipline the renderer uses
 * to keep scene assembly away from the graphics API, and it buys the same two things: a
 * different transport can be dropped underneath without touching the rules, and the
 * rules can be tested by feeding two schedulers each other's packets in one process,
 * with no network at all.
 *
 * THE ORDERS ARE OPAQUE. An order is a blob of bytes to this file. It happens to be the
 * engine's EventClass, 22 bytes with its payload at offset 6, but nothing here reads
 * inside one: the scheduler decides WHEN a blob executes and in WHAT ORDER relative to
 * other peers' blobs, and the engine decides what it means. That keeps the wire honest
 * if the engine's event ever grows.
 *
 * RELIABILITY WITHOUT RETRANSMISSION. Every packet carries the last LS_REDUNDANCY turns
 * of this peer's orders, not just the newest, so a dropped packet is covered by the next
 * one and there is no ack, no retransmit timer and no head of line stall. The cost is
 * bandwidth we do not have a shortage of: an order is 22 bytes and a busy turn is a few
 * dozen of them. This is the standard lockstep trick and it is why plain UDP is enough.
 *
 * THE ORDER WITHIN A TURN IS FIXED BY SEAT. Two peers must not merely execute the same
 * orders on the same frame, they must execute them in the same sequence, or a contested
 * action resolves differently on each. Orders run seat 0 first, then seat 1, and within
 * one seat in the order that peer queued them. Nothing here ever sorts by arrival.
 */
#ifndef CNC3D_LOCKSTEP_H
#define CNC3D_LOCKSTEP_H


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

/* The roster this build can seat. The engine is eight houses today and the Enhanced
   ladder goes to sixty four; this is the one place the scheduler's own limit moves, and
   it is deliberately independent of the engine's so that raising one does not silently
   claim the other moved too. */
#define LS_MAX_SEATS 8

/* How far ahead of the executing frame an order is stamped. The order a player gives on
   frame N executes on frame N + LS_MAX_AHEAD, which is what buys the network the time to
   deliver it before that frame arrives. It is felt as input lag: at the engine's 15 Hz,
   three turns is 200 ms. The 1995 engine defaulted to the same three and raised it when
   the link was slow; raising it here is a lobby decision, and a peer may not change it
   mid match because every peer stamps against it. */
#define LS_MAX_AHEAD 3

/* How many past turns ride along in every packet. Must exceed the number of consecutive
   packets that can be lost before the turn they cover comes due, which is why it is
   larger than LS_MAX_AHEAD rather than equal to it. */
#define LS_REDUNDANCY 6

/* The ring of turns held in memory, indexed by turn number modulo this. Must be
   comfortably larger than LS_MAX_AHEAD + LS_REDUNDANCY or a turn still needed for
   redundancy would be overwritten by a turn being filled. A power of two so the modulo
   is a mask. */
#define LS_HISTORY 64

/* Bytes of orders one seat may issue on one turn. An engine order is 22 bytes and the
   engine emits ONE PER SELECTED OBJECT, so a move order over a large selection is the
   realistic worst case rather than a pathological one: this holds about ninety of them.
   Overflow is reported, never silently dropped, because an order that vanishes on one
   peer and not another is a desync rather than a lost click. */
#define LS_TURN_BYTES 2048

/* The largest single order accepted, as a sanity bound on anything arriving from the
   wire. The engine's event is 22 bytes; this leaves room for it to grow without letting
   a malformed length field name a buffer. */
#define LS_ORDER_MAX 64

/* Wire constants. The magic makes a stray packet on the port obvious rather than being
   parsed as a turn, and the version refuses a peer built against a different wire
   instead of desyncing ten minutes in. Bump the version whenever the packet layout or
   the meaning of a field changes. */
#define LS_MAGIC   0x33434E50u   /* 'C3NP' little endian on the wire, written by hand */
#define LS_VERSION 1

/* The largest packet this file will build or accept. LS_REDUNDANCY turns of a full
   LS_TURN_BYTES each, plus headers, is the true worst case; it is well inside any
   sensible path MTU only because a full turn is rare, so the packer stops adding older
   turns once it reaches LS_PACKET_MAX rather than emitting something that will fragment.
   Losing redundancy is a recoverable degradation; fragmenting every packet is not. */
#define LS_PACKET_MAX 1200

/* Why a call failed. Returned rather than logged so the caller decides whether a
   condition is fatal, and so the tests can assert on it. */
enum {
    LS_OK = 0,
    LS_ERR_FULL,        /* this turn's order buffer is full for this seat */
    LS_ERR_RANGE,       /* a seat or turn outside what this scheduler holds */
    LS_ERR_BAD_PACKET,  /* magic, version, length or count did not survive validation */
    LS_ERR_TOO_BIG      /* a single order longer than LS_ORDER_MAX */
};

typedef struct {
    unsigned char bytes[LS_TURN_BYTES];
    int           used;      /* bytes filled */
    int           count;     /* orders held */
    int           reported;  /* this seat has SPOKEN for this turn, even to say nothing */
} LsTurn;

typedef struct {
    int    seats;            /* how many peers are in this match */
    int    me;               /* which seat this peer is */
    int    max_ahead;        /* copy of LS_MAX_AHEAD, agreed in the lobby */
    unsigned turn_send;      /* the turn local orders are currently being stamped for */
    unsigned turn_exec;      /* the next turn the engine will be allowed to run */
    LsTurn ring[LS_HISTORY][LS_MAX_SEATS];
    /* Diagnostics, for the waiting indicator and for a desync report. Never used to
       make a scheduling decision, so that reading them cannot change behaviour. */
    unsigned long packets_in;
    unsigned long packets_bad;
    unsigned long orders_in;
} LsState;

/* Set up a match. `seats` peers, this one sitting at `me`, starting at turn 0. Returns
   LS_OK or LS_ERR_RANGE. Every peer must call this with the same `seats`. */
int ls_init(LsState* s, int seats, int me);

/* Queue one local order for the turn currently being stamped. The caller does not choose
   the turn: that is the whole point of the scheduler, and letting a caller pick would let
   two peers stamp the same click differently. Returns LS_OK, LS_ERR_FULL or
   LS_ERR_TOO_BIG. */
int ls_local_order(LsState* s, const void* bytes, int len);

/* Build the outgoing packet for this peer: the turn being stamped plus the last
   LS_REDUNDANCY turns of already-sent orders. Writes at most LS_PACKET_MAX bytes into
   `out` and returns the length, or a negative LS_ERR_*. Calling this MARKS the stamped
   turn as spoken for locally and advances to the next one, so it is called exactly once
   per turn, by the same code that decides a turn has been reached. */
int ls_pack(LsState* s, void* out, int outmax);

/* Take a packet off the wire. Validates it fully before it changes any state, so a
   malformed or hostile packet cannot leave the ring half written. The sender's seat is
   read from the packet and checked against the roster; a packet claiming this peer's own
   seat is refused, because a peer's own orders come from ls_local_order and accepting
   them twice would double every action. Returns LS_OK or an LS_ERR_*. */
int ls_on_packet(LsState* s, const void* bytes, int len);

/* May the engine execute turn s->turn_exec yet? True once every seat has reported that
   turn. This is the barrier: the host's main loop asks before every simulation tick and
   keeps rendering, without advancing, while the answer is false. */
int ls_turn_ready(const LsState* s);

/* Which seats are holding the match up, as a bitmask, for the waiting indicator. Zero
   when the turn is ready. */
unsigned ls_waiting_mask(const LsState* s);

/* Hand every order for the executing turn to `sink`, in seat order and then in queue
   order, then advance to the next turn. Called only when ls_turn_ready is true. The sink
   is where the host injects each order into its own engine. */
typedef void (*LsOrderSink)(void* user, int seat, const void* bytes, int len);
void ls_run_turn(LsState* s, LsOrderSink sink, void* user);

/* The turn the engine is about to run, and the turn local input is being stamped for.
   Exposed for the host's status line and for tests; nothing here is settable. */
unsigned ls_exec_turn(const LsState* s);
unsigned ls_send_turn(const LsState* s);

#ifdef __cplusplus
}   /* extern "C" */
#endif

#endif /* CNC3D_LOCKSTEP_H */
