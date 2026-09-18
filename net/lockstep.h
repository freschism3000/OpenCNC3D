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
 * For a SHORT loss. A loss longer than that window is repaired by the one exception, a
 * request naming the turn a stuck peer needs, answered with ls_pack_from; see
 * LS_REDUNDANCY for why no window, however wide, could do it alone.
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

/* THE BAND A ROOM MAY CHOOSE FROM, and why there is a band at all.
 *
 * Three turns is 200 ms of cover, which is generous on a LAN and not enough across an
 * ocean or through a relay: an order that arrives after the turn it was stamped for has
 * gone by is not late, it is a stalled match, because every peer waits for it. So the
 * room picks the number from the worst round trip it can measure before anyone presses
 * START, exactly as the 1995 engine raised its own when the link was slow.
 *
 * THE COST IS INPUT LAG AND IT IS PAID BY EVERYONE. At 15 Hz each turn of cover is 66.7
 * ms between the click and the unit moving, so the ceiling is not arbitrary: twelve turns
 * is 800 ms, which is about as much as a real-time game can ask for before it stops
 * feeling like one. A link worse than that wants a smaller world, not a bigger number.
 *
 * NOT A MID-MATCH DIAL. Every peer stamps against this, so it is agreed once in the lobby,
 * travels in the setup with the tech level and the map, and never moves again. Changing it
 * while turns are in flight would have two peers stamping the same order for two different
 * turns, which is a desync with extra steps. */
#define LS_AHEAD_MIN 3
#define LS_AHEAD_MAX 12

/* How many past turns ride along in every packet. It covers a SHORT loss on its own: a
   dropped packet is repaired by the next one, with no request and no delay.

   IT CANNOT COVER A LONG ONE, AND NO NUMBER HERE COULD. A peer that is still receiving
   keeps executing, and every turn it executes moves the window it sends past the turn a
   peer that is not receiving is stuck on. How far past depends on the lookahead: with a
   lookahead of A, the newest turn a peer sends can be 2A + 1 beyond the one its partner
   is waiting for, and from A = 3 up, which is the whole band, that is outside a window of
   six. A two-way outage does the same thing more slowly. So a loss longer
   than the window is repaired by asking, not by carrying more: the stuck peer names the
   turn it needs and the peer holding it answers with ls_pack_from. The window stays what
   it was, and it stays the wire's limit on blocks per packet. */
#define LS_REDUNDANCY 6

/* The ring of turns held in memory, indexed by turn number modulo this. Must be
   comfortably larger than LS_AHEAD_MAX + LS_REDUNDANCY or a turn still needed for
   redundancy would be overwritten by a turn being filled, and larger than the 2A + 2
   turns back a stuck partner can be asking for (26 at the top of the band), or
   ls_pack_from would no longer hold the turn it is asked for. A power of two so the
   modulo is a mask. */
#define LS_HISTORY 64

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
   Losing redundancy is a recoverable degradation; fragmenting every packet is not.

   1016 AND NOT AN MTU, because the tightest limit on this path is not the network's.
   An internet match is carried by a CnCNet tunnel, which puts an eight byte header in
   front of whatever it forwards and whose older server reads into a 1024 byte buffer.
   1016 + 8 is exactly that buffer, so a full packet arrives instead of being dropped by
   a relay that never explains why. The number is ONE number for every path rather than
   a limit per transport: it is also both peers' ACCEPT bound, so two peers that
   disagreed about it would have the larger one's packets silently refused by the
   smaller, which is a stall that looks like packet loss.

   What it costs is redundancy per packet, on a link where a full turn is already rare,
   and the packer degrades exactly as the paragraph above says it does. */
#define LS_PACKET_MAX 1016

/* BYTES OF ORDERS ONE SEAT MAY ISSUE ON ONE TURN, AND IT IS THE WIRE THAT SETS IT.
 *
 * One turn's orders are ONE BLOCK and a block is never split across datagrams, so the most
 * a turn can hold is what is left of a packet once the packet header (12 bytes) and the
 * block header (2 for the length, 1 for the count) are paid for. That is the 15 below, and
 * it is written as a subtraction rather than as a number because the two limits MUST move
 * together.
 *
 * THEY USED NOT TO, AND THAT WAS A HANG. This was an independent 2048, so a turn holding 44
 * orders or more (22 bytes and a length byte each, 1012 bytes) was accepted by
 * ls_local_order, marked as spoken for by ls_pack, and then dropped from the packet by the
 * very first size test in the redundancy walk. What went out was a 12 byte header carrying
 * no turns at all, which the far peer accepted as valid and learned nothing from. Re-sending
 * it sent the same empty packet, and asking for the turn by number hit the same test inside
 * ls_pack_from and got nothing back, so both peers waited on that turn for ever. It was
 * reachable from the game rather than pathological: the engine emits one order PER SELECTED
 * OBJECT, so one right click with 44 units selected did it.
 *
 * Overflow is reported, never silently dropped, because an order that vanishes on one peer
 * and not another is a desync rather than a lost click. A caller holding more orders than
 * this should ask ls_turn_room how many fit and keep the rest for the next turn, which
 * costs one turn of lag instead of the match. */
#define LS_TURN_BYTES (LS_PACKET_MAX - 15)

/* Why a call failed. Returned rather than logged so the caller decides whether a
   condition is fatal, and so the tests can assert on it.
   THESE ARE POSITIVE AND SMALL. The calls that return a STATUS return them as they are;
   the two that return a LENGTH, ls_pack and ls_pack_from, return them NEGATED, so a caller
   reading a length can test the sign and cannot mistake LS_ERR_RANGE for a two byte
   datagram it is being asked to send. */
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
    /* A SEAT THAT WILL NEVER SPEAK: a computer, or a blocked slot. The barrier skips it.
       Set by ls_set_absent on EVERY peer with the SAME set, straight after ls_init and
       before any turn, or the peers wait on different seats and the match never starts.
       Kept as a flag rather than by shrinking `seats`, so seat numbers stay the roster's
       and a person at seat 5 of 8 is seat 5 on every machine. */
    int    absent[LS_MAX_SEATS];
    /* AND THE TURN THE ABSENCE STARTS ON, which is what makes a seat leaving MID MATCH
       mean the same thing on every peer. A seat set absent by ls_set_absent goes quiet
       from turn 0 (a computer never spoke at all); a seat that WALKS OUT goes quiet at the
       turn the host names, and every peer is told that number.
       WITHOUT IT THE ORDER STREAMS FORK. Absence used to take effect the instant the
       goodbye was read, or the instant a local silence timer fired, and that is a
       different turn on every machine because it is each machine's own clock. A peer
       that had the departing seat's last turn executed those orders; a peer that had not
       stopped waiting and executed none. Both then played on, one army's last move having
       happened in one world and not the other. */
    unsigned absent_from[LS_MAX_SEATS];
    /* Diagnostics, for the waiting indicator and for a desync report. Never used to
       make a scheduling decision, so that reading them cannot change behaviour. */
    unsigned long packets_in;
    unsigned long packets_bad;
    unsigned long orders_in;
} LsState;

/* Set up a match. `seats` peers, this one sitting at `me`, starting at turn 0. Returns
   LS_OK or LS_ERR_RANGE. Every peer must call this with the same `seats`. */
int ls_init(LsState* s, int seats, int me);
/* The same, with the room's agreed lookahead instead of the built-in default. Clamped to
   LS_AHEAD_MIN..LS_AHEAD_MAX, because a number off the wire is a number a peer chose.
   ls_init is this with LS_MAX_AHEAD, so every existing caller is unchanged.
   IT CANNOT BE SET LATER: the opening turns are pre-agreed empty up to the lookahead, so
   the value has to be known while the ring is being seeded. */
int ls_init_ahead(LsState* s, int seats, int me, int ahead);
/* Mark `seat` as one that never sends: a computer or a blocked slot. See LsState::absent.
   Absent from turn 0, because such a seat never spoke at all.
   Returns LS_OK, or LS_ERR_RANGE for a seat outside the match or for this peer's own. */
int ls_set_absent(LsState* s, int seat);

/* THE SAME, BUT STARTING AT A NAMED TURN: for a seat that WALKS OUT of a running match.
   Up to `turn` the seat is still a seat, so the barrier waits for it and its orders
   execute; from `turn` on it is skipped and any orders it may still have in the ring are
   NOT executed. Every peer must be given the SAME turn, which is why the host names it and
   sends it with the goodbye. Returns LS_OK or LS_ERR_RANGE.
   A turn at or below the executing turn is an immediate absence, which is what a seat that
   was already holding the match up gets. */
int ls_set_absent_at(LsState* s, int seat, unsigned turn);

/* THE FIRST TURN `seat` HAS NOT REPORTED, counting forward from the executing turn. It is
   the answer to "how far can this match still go without that seat speaking again", and it
   is what the host names as the turn a departure lands on: every turn below it is one this
   peer can already run, so naming it can never ask the match to wait for a turn that is
   never coming. Returns the executing turn when the seat is already holding it up. */
unsigned ls_seat_first_missing(const LsState* s, int seat);

/* Queue one local order for the turn currently being stamped. The caller does not choose
   the turn: that is the whole point of the scheduler, and letting a caller pick would let
   two peers stamp the same click differently. Returns LS_OK, LS_ERR_FULL or
   LS_ERR_TOO_BIG. */
int ls_local_order(LsState* s, const void* bytes, int len);

/* HOW MANY MORE ORDERS OF `len` BYTES THE TURN BEING STAMPED CAN STILL TAKE. A caller with
   a burst in hand asks this first and keeps the rest for the next turn, rather than
   offering them all and having ls_local_order refuse the overflow: a turn is one block on
   the wire, the block cannot be split, and LS_TURN_BYTES says what a block holds. Spilling
   costs the overflow one turn of lag; not spilling costs the match. Zero when nothing more
   fits, and zero for a bad argument. */
int ls_turn_room(const LsState* s, int len);

/* Build the outgoing packet for this peer: the turn being stamped plus the last
   LS_REDUNDANCY turns of already-sent orders. Writes at most LS_PACKET_MAX bytes into
   `out` and returns the length, or a negative LS_ERR_*. Calling this MARKS the stamped
   turn as spoken for locally and advances to the next one, so it is called exactly once
   per turn, by the same code that decides a turn has been reached.
   THE WINDOW IS ALWAYS THE FULL LS_REDUNDANCY, whatever the lookahead. It used to be
   shorter at a small lookahead, as an accident: executing a turn wiped this peer's own
   copy of it, so the walk ran out of reported turns at the executing turn rather than at
   the end of the window. A peer's own turns are kept now, because ls_pack_from can only
   answer with what this peer still holds. Measured on two peers with no loss, driven one
   turn per pass the way a match drives them: at a lookahead of 3 the mean packet grew from
   70 bytes and four blocks to 99 bytes and six, which is about 0.4 kB/s per peer at the
   engine's 15 Hz; from a lookahead of 6 up, which is every relayed room, it was six blocks
   and 99 bytes before and is six blocks and 99 bytes now. The content of each turn is
   unchanged either way. The newest block always fits (see LS_TURN_BYTES); it is the OLDEST
   redundant copies that are dropped when a packet fills. */
int ls_pack(LsState* s, void* out, int outmax);

/* THE SAME PACKET AGAIN, STARTING WHERE SOMEBODY IS STUCK. Builds a packet of this peer's
   own orders for up to LS_REDUNDANCY turns, beginning at `from` and going forward, and
   changes nothing: no turn is marked, nothing advances. It exists for the one loss the
   redundancy window cannot repair, a peer that has been deaf for longer than the window
   while this one kept executing, and it is sent in answer to that peer naming the turn it
   is waiting on. The packet is an ordinary one (ls_on_packet cannot tell the difference),
   so the store's first-copy-wins rule makes it harmless when it arrives twice or late.
   Returns the length, 0 when there is nothing to send (that turn has not been stamped
   yet, or is too old for the ring to hold), or a NEGATIVE LS_ERR_*: -LS_ERR_RANGE for a
   buffer too small to hold a header, and -LS_ERR_TOO_BIG when the turn asked for is itself
   larger than the buffer offered, which is the one case a caller must not read as "there
   is nothing to send". */
int ls_pack_from(const LsState* s, unsigned from, void* out, int outmax);

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

/* HOW FAR AHEAD THIS SESSION STAMPS, which is the room's agreed number and NOT the
   compile-time default once a lobby has measured its link.
 *
 * THIS EXISTS BECAUSE THE TWO HALVES CAME APART. The scheduler holds an order for this
 * many turns; the ENGINE stamps that same order with the frame it expects to execute on,
 * and it is told the number separately. When the room agreed on anything other than the
 * default, the engine stamped for three turns' time and the scheduler delivered on five,
 * so the order arrived describing a frame that had already gone and the engine refused
 * every one of them. Ask here rather than reaching for the constant, and the two cannot
 * disagree again. */
int ls_ahead(const LsState* s);

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
