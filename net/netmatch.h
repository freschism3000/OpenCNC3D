/*
 * netmatch.h -- one lockstep match between two peers, for the host's main loop.
 *
 * WHERE THIS SITS. lockstep.c decides WHEN a turn may run and net_udp.c owns the socket;
 * neither knows what a match is. This file does: who the peer is, what the host decided
 * about the match and the joiner has to adopt, how this peer's orders leave the engine and
 * the other peer's arrive in it, and whether the two worlds still agree. The renderer calls
 * five functions a tick and never sees a socket, which is the seam the design asks for.
 *
 * THE ENGINE SIDE IS TWO CALLBACKS. The host hands over "drain my pending orders" and
 * "post this order", which are CNC3D_Drain_Events and CNC3D_Post_Event behind a typedef,
 * so this file links into the game without the brain's header and the two-brain gate could
 * drive it with its own copies.
 *
 * TWO SEATS TODAY. The scheduler seats eight and the packets name their seat, so nothing
 * here narrows the wire; what is two-only is the handshake, which knows one peer address.
 * A star with a host relaying is Phase 5's work and changes this file, not lockstep.c.
 */
#ifndef CNC3D_NETMATCH_H
#define CNC3D_NETMATCH_H

#ifdef __cplusplus
extern "C" {
#endif

#define NM_PORT_DEFAULT 17421
#define NM_MAX_SEATS 8

/* What the host decided and the joiner adopts before either reads the scenario. Every
   field here changes the simulation, so every field must agree, and the joiner's own
   command line loses to it. */
typedef struct {
    char scenario[16];
    int credits;
    int tiberium;
    int crates;
    int superweapons;
    int bases;
    int unit_count;
    int speed;    /* the game speed slider index, 0..6: THE MATCH'S TICK RATE */
    int seats;    /* humans plus computers */
    int humans;   /* 2 */
    unsigned char house[NM_MAX_SEATS];
    unsigned char colour[NM_MAX_SEATS];
    unsigned char team[NM_MAX_SEATS];
    unsigned char start[NM_MAX_SEATS];
    unsigned char is_ai[NM_MAX_SEATS];
} NmSetup;

/* Drain up to `max` pending local orders into `out`, each `event_size` bytes, stamped
   `frame_delay` frames ahead. Returns the count, or negative on a refusal. */
typedef int (*NmDrainFn)(void* user, void* out, int max, int frame_delay);
/* Post one received order. Returns 1 on success. */
typedef int (*NmPostFn)(void* user, const void* event);

/* Host a match: bind `port`, wait up to `timeout_s` for a joiner, hand it `setup`.
   Returns this peer's seat (0) or -1. `abi_hash` is the brain's order-wire layout hash;
   a joiner with a different one is refused, because the two could not exchange an order. */
int nm_host(unsigned short port, const NmSetup* setup, unsigned abi_hash, int timeout_s);
/* Join a match at `addr:port`; fills `setup` with what the host decided. Returns this
   peer's seat (1) or -1. */
int nm_join(const char* addr, unsigned short port, NmSetup* setup, unsigned abi_hash, int timeout_s);

int nm_active(void);
int nm_seat(void);
const NmSetup* nm_setup(void);
void nm_set_engine(NmDrainFn drain, NmPostFn post, void* user, int event_size);

/* THE TURN, in the order the host's loop calls them:
   nm_begin_turn  drain local orders, pack this turn, send it. 0 means the match is over
                  (a queue overflowed or the peer is gone).
   nm_turn_ready  pump the socket; may the executing turn run yet? Non blocking.
   nm_wait_turn   the same, blocking up to timeout_ms, for the scripted paths.
   nm_run_turn    deliver the executing turn's orders to the engine, both seats in seat order.
   Then the host advances the engine ONE tick. */
int  nm_begin_turn(void);
int  nm_turn_ready(void);
int  nm_wait_turn(int timeout_ms);
int  nm_run_turn(void);

/* The desync alarm. The host hashes the world after every tick and reports it here; every
   NM_SYNC_EVERY frames the hash goes to the peer, and a peer hash for a frame this side has
   a different hash for is a desync. */
#define NM_SYNC_EVERY 15
void nm_note_hash(unsigned frame, unsigned hash);
int  nm_desynced(void);
unsigned nm_desync_frame(void);

/* The peer said goodbye, or has been silent past the limit. A scripted joiner reads this to
   end cleanly rather than counting the host's exit as a refusal. */
int nm_peer_left(void);

unsigned nm_turns_run(void);
void nm_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif
