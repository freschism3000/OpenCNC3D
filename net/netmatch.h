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
 * EIGHT SEATS, AS A STAR WITH THE HOST RELAYING. It was two for a while and
 * the two-ness was never in the scheduler, which has always seated LS_MAX_SEATS and named
 * the seat in every packet: it was in this file's handshake, which knew one peer address.
 *
 * WHY A STAR AND NOT A MESH, because the choice has a consequence the operator will feel.
 * In a mesh every peer sends its turn packet straight to every other, which is one hop and
 * the lowest latency there is, and it requires every pair of machines to be able to reach
 * each other. In a star the joiners speak only to the host and the host forwards each
 * joiner's packet to the others, which costs one extra hop, on a LAN well under a
 * millisecond, and requires only ONE machine to be reachable.
 *
 * The star wins here on evidence rather than taste: every attempt this project has made to
 * get two peers talking across a real network has run into one that would not accept an
 * inbound connection. Needing ONE reachable host, instead of every peer reachable by every
 * other, is the difference between "forward one port" and "forward a port everywhere, and
 * hope no two players are behind the same NAT". It is also what Phase 5's relay and hole
 * punching build on, so nothing here is thrown away.
 *
 * WHAT THE RELAY DOES NOT DO: it does not read, reorder, merge or drop anything. A
 * forwarded packet is the sender's bytes, unchanged, and the scheduler on the far side
 * cannot tell a relayed packet from a direct one. The host is a wire, not a referee, which
 * is what keeps the lockstep argument the same one it was with two peers.
 */
#ifndef CNC3D_NETMATCH_H
#define CNC3D_NETMATCH_H

#ifdef __cplusplus
extern "C" {
#endif

#define NM_PORT_DEFAULT 17421

/* THE RELAY, for now: one address compiled in. CnCNet's Tunnel V3 servers are public,
   advertise a 250-client pool, and were verified answering with `netcheck relay`. A room
   code names an ID and not a SERVER, so both ends must resolve it against the same relay;
   until there is a list to choose from, that means one constant and a code from a host on
   any other relay simply does not answer.
   THIS IS A PLACEHOLDER WITH A SHELF LIFE. It is somebody else's donated infrastructure
   and it can move, fill up or go away, so the next step after a first real trial is to
   read the master list rather than to trust this line. */
#define NM_RELAY_HOST "49.13.152.109"
#define NM_RELAY_PORT 50001
#define NM_MAX_SEATS 8
/* The game's name as the host types it and the browser shows it. Matches the beacon's
   NB_NAME_MAX on purpose: a name that fits one and not the other would be truncated in
   the list and whole in the lobby, which reads as two different games. */
#define NM_NAME_MAX 32
/* A password is at most this long. It is never sent: only a hash of it crosses. */
#define NM_PASS_MAX 32
/* A PLAYER'S OWN HANDLE. Twelve, because that is the engine's own MPLAYER_NAME_MAX and
   CNC_Set_Multiplayer_Data truncates there: a wire carrying more would carry characters
   the simulation throws away. 1995 calls this machine's copy MPlayerName and the table
   MPlayerNames[]. */
#define NM_PLAYER_NAME_MAX 12

/* THE SPEED A ROOM OPENS AT, and the ONE place that number is written on this side of
   the network seam. Three callers build an NmSetup from scratch -- the skirmish answer
   packed for the wire, the host screen's push every dirty frame, and the renderer's
   own idea of the match speed before a setup has arrived -- and each of them used to
   carry its own literal. Three copies of one decision is three chances to move two.
   4 is the same index the single-player Game Controls page ships (dopt_settings_init),
   so a player meets one pace in a campaign mission, a skirmish and a network game. The
   two are deliberately equal and deliberately SEPARATE constants: this one is a
   property of the match, travels in the handshake, and must not make the lobby depend
   on the options page. A compile-time check where both headers are in scope refuses to
   build if they ever stop agreeing.
   IT IS ONLY A STARTING VALUE. What a match actually runs at is setup.speed as it
   crossed the wire: the host decides and every joiner adopts, so a peer whose default
   is different still runs the host's number and the two worlds keep the same clock. */
#define NM_DEFAULT_SPEED 4

/* What the host decided and the joiner adopts before either reads the scenario. Every
   field here changes the simulation, so every field must agree, and the joiner's own
   command line loses to it. */
/* TAGGED, not anonymous, so a header that does not include this one can still say
   `struct NmSetup*` in a prototype. Without the tag, `struct NmSetup` is a DIFFERENT and
   incomplete type, and the mismatch only shows up at the one call site that dereferences
   it. The menu shell's multiplayer screen is that call site. */
typedef struct NmSetup {
    char scenario[16];
    int credits;
    int tiberium;
    int crates;
    int aitake;      /* v6 */
    int shortgame;   /* v6 */
    int superweapons;
    int bases;
    int unit_count;
    /* TECH LEVEL, and it is on the wire because leaving it off desynced the first match
       ever started from the lobby: each side booted with its own default, the build lists
       differed, and the two worlds parted on the first production order. Anything the
       lobby lets a player set has to travel or it is not a shared match. */
    int build;
    int speed;    /* the game speed slider index, 0..6: THE MATCH'S TICK RATE */
    /* HOW FAR AHEAD ORDERS ARE STAMPED, chosen by the host from the worst round trip it
       measured while the room was open, and carried here because every peer stamps
       against it. Three turns is 200 ms of cover, which is right on a LAN and not enough
       through a relay; see LS_AHEAD_MIN in lockstep.h for the band and what it costs.
       Zero from an older peer means "not said", and the default stands. */
    int ahead;
    /* THE ROOM'S WIDTH: how many seats there are, blocked ones included, and the size
       of the lockstep roster. On the lobby path it is the number on the host tab. */
    int seats;
    /* How many of those a PERSON sits in or will sit in (mode HUMAN or OPEN). Never
       decided by a caller on the lobby path: netmatch counts it off the modes. */
    int humans;
    unsigned char house[NM_MAX_SEATS];
    unsigned char colour[NM_MAX_SEATS];
    unsigned char team[NM_MAX_SEATS];
    unsigned char start[NM_MAX_SEATS];
    unsigned char is_ai[NM_MAX_SEATS];   /* DERIVED: mode[i] == NM_SEAT_BOT. Kept because four
                                            writers and one reader already speak it. */
    /* WHAT EACH SEAT IS FOR, decided by the host and carried on the wire since version 5.
       A bool could not say it: OPEN and BLOCK differ only in whether a joiner may be
       seated, and that is the whole feature -- a room that starts empty and fills with
       people, computers, or nothing, seat by seat, at the host's say. */
    unsigned char mode[NM_MAX_SEATS];
    /* WHO IS IN EACH SEAT (v7). Host-authored like every other field here: a joiner
       sends its handle in the HELLO and the host writes it in, exactly as 1995 fills
       MPlayerNames[] from the join packets. A BOT seat reads COMPUTER; an OPEN or
       BLOCKED seat is empty. */
    char name[NM_MAX_SEATS][NM_PLAYER_NAME_MAX];
} NmSetup;

enum {
    NM_SEAT_HUMAN = 0,   /* a person sits here (or will: the host's own seat is HUMAN) */
    NM_SEAT_BOT   = 1,   /* a computer plays this seat                                */
    NM_SEAT_OPEN  = 2,   /* empty, and a joiner may take it                           */
    NM_SEAT_BLOCK = 3    /* empty, and nobody may take it                             */
};

/* Drain up to `max` pending local orders into `out`, each `event_size` bytes, stamped
   `frame_delay` frames ahead. Returns the count, or negative on a refusal. */
typedef int (*NmDrainFn)(void* user, void* out, int max, int frame_delay);
/* Post one received order. Returns 1 on success. */
typedef int (*NmPostFn)(void* user, const void* event);

/* Host a match: bind `port`, wait up to `timeout_s` for `humans - 1` joiners, hand them
   `setup`, then start everyone together.
   Returns this peer's seat (0) or -1.

   TWO FINGERPRINTS, AND THEY CATCH DIFFERENT MISTAKES.

   `abi_hash` is the brain's order-wire layout hash. A joiner with a different one is
   refused because the two could not exchange an order at all: the bytes would land in
   different fields.

   `scen_hash` is a hash of the SCENARIO'S OWN BYTES, the .INI and .BIN the simulation
   starts from. Two peers can agree on every word of the setup, including the scenario
   NAME, and still begin from different worlds: an edited map, a different mission set, a
   half-copied user map. Nothing in the setup can see that, the handshake would succeed,
   and the match would desync on the first tick with the alarm reporting a mismatch and no
   cause. Hashing the bytes turns that into a refusal by name before a tick is run, which
   is the difference between a bug report and a sentence telling the player what to fix.

   Pass 0 for `scen_hash` to mean "I cannot compute one", and the check is skipped rather
   than failed: a zero on either side is not evidence of disagreement. Both ends check, so
   the joiner still catches a mismatch when it was started with no scenario of its own and
   adopted the host's name from the welcome. */
int nm_host(unsigned short port, const NmSetup* setup, unsigned abi_hash, unsigned scen_hash,
            int humans, int timeout_s);
/* Join a match at `addr:port`; fills `setup` with what the host decided. Returns this
   peer's seat (1) or -1. See nm_host for what the two hashes are and why there are two. */
int nm_join(const char* addr, unsigned short port, NmSetup* setup, unsigned abi_hash, unsigned scen_hash, int timeout_s);

/* ------------------------------------------------------------------ THE LOBBY ------ *
 *  The handshake as a STATE MACHINE you poll, rather than a function that blocks until
 *  the match starts.
 *
 *  WHY IT HAD TO CHANGE. nm_host used to sit in a loop until every joiner had arrived and
 *  acknowledged, which is exactly right for a command line and impossible for a screen: a
 *  lobby has to draw the players as they arrive, let them tick READY, and start when the
 *  HOST presses a button rather than when the last acknowledgement lands. So the loop is
 *  turned inside out. nm_host and nm_join still exist and still block, because the gates
 *  and the command line want them, but they are now thin loops over these same calls --
 *  ONE implementation, so the screen and the command line cannot drift apart.
 *
 *  THE SHAPE: call nm_lobby_host or nm_lobby_join once, then nm_lobby_poll every frame
 *  until it stops returning NM_LOBBY_WAITING.
 * ---------------------------------------------------------------------------------- */
enum {
    NM_LOBBY_IDLE = 0,  /* nothing has been opened                                     */
    NM_LOBBY_WAITING,   /* host: seats to fill. joiner: not seated, or not started yet */
    NM_LOBBY_FULL,      /* every seat is taken. The host may press START               */
    NM_LOBBY_STARTED,   /* the match is armed: leave the lobby and play                */
    NM_LOBBY_REFUSED,   /* refused, and nm_lobby_error says why in a printable sentence */
    NM_LOBBY_FAILED     /* the socket died or nobody came                              */
};

/* Open as the host. `name` is what the browser shows, `password` may be NULL or empty for
   an open game. Returns 1, or 0 with nm_lobby_error set. Does not block. */
int nm_lobby_host(unsigned short port, const NmSetup* setup, unsigned abi_hash,
                  unsigned scen_hash, int humans, const char* name, const char* password);
/* Open as a joiner. `password` must match the host's or the join is refused by name. */
int nm_lobby_join(const char* addr, unsigned short port, unsigned abi_hash,
                  unsigned scen_hash, const char* password);
/* Pump. Returns one of NM_LOBBY_*. Cheap; call it every frame. */
int nm_lobby_poll(void);
int nm_lobby_state(void);
int nm_lobby_is_host(void);
int nm_lobby_seat(void);            /* this peer's seat, or -1 before it is seated   */
int nm_lobby_filled(void);          /* how many humans are in, host included         */
int nm_lobby_wanted(void);          /* how many the host asked for                   */
int nm_lobby_seat_taken(int seat);
int nm_lobby_seat_ready(int seat);  /* the host counts as ready always               */
int nm_lobby_seat_mode(int seat);   /* NM_SEAT_*; BLOCK for any seat past the room     */
/* THE LOBBY CHAT. A line said here goes to every seat: a joiner sends it to
   the host, which relays it to the rest, the same star every other lobby message rides.
   Lines are kept in a ring of the last NM_CHAT_LOG; the count only ever rises, so a
   caller that remembers how many it has read can pick up exactly the new ones.
   nm_chat_line answers NULL for a line that has scrolled out of the ring. The text is a
   REMOTE peer's bytes, already reduced to printable ASCII on receipt, and `seat` is the
   number carried IN the packet, never the sender's address: on a joiner every relayed
   line arrives from the host. The ring serves BOTH the lobby and the match: since 6 Sep
   2026 nm_pump handles NM_CHAT too, so a caller that wants only the match's lines should
   snapshot nm_chat_count() when the match arms and read forward from there, because the
   room's conversation is still sitting in the ring behind it. */
#define NM_CHAT_MAX 96
#define NM_CHAT_LOG 16
void nm_chat_say(const char* text);
int nm_chat_count(void);
const char* nm_chat_line(int i, int* seat);
int nm_lobby_all_ready(void);       /* every seat taken AND every joiner ready       */
/* A joiner's own toggle; ignored on the host. The tick it sends is REPEATED until the
   host echoes it back, because the button is state rather than an event and one lost
   datagram used to leave a room that could never start and no way to find out but
   pressing again. */
void nm_lobby_set_ready(int on);
int nm_lobby_my_ready(void);
/* THE WORST ROUND TRIP THE HOST HAS MEASURED, in milliseconds, or -1 before any seat has
   answered a ping. Host only: a joiner never measures anything, because the number it
   would arrive at is not the one the room runs on. Exposed so the lobby can show it.
   It is the worst over the seats that HAVE answered, so until every seat has, it is a
   lower bound; START waits for all of them for exactly that reason. */
int nm_lobby_worst_rtt(void);
/* What that round trip makes the lookahead, without applying it: the same arithmetic the
   host uses at START, so a screen can say what the room will run at. */
int nm_ahead_for_rtt(int rtt_ms);

/* THIS PEER'S OWN SEAT: side, team, colour and start. On the host it is applied here; on
   a joiner it is sent to the host, which applies what the rules allow and re-welcomes.
   start is -1 for unpicked. Once the host's START is on the wire the host applies nothing,
   from anybody, because every copy of the START must describe the same room. */
void nm_lobby_set_my_seat(int house, int team, int colour, int start);
/* HOST ONLY: remove a seat. The player is told why and sent back to the browser; the
   seat opens again and somebody else may take it.

   WHY THIS IS NOT OPTIONAL. START is gated on every seated player having readied, so one
   person who walks away from the keyboard holds the room shut for everybody else, with no
   way out but the host quitting and everyone starting again. A gate with no override is a
   deadlock with good manners. */
int nm_lobby_kick(int seat);
/* Set when THIS peer was removed by the host, so the screen can say so rather than
   reporting a connection that merely stopped. */
int nm_lobby_was_kicked(void);

/* HOST ONLY: replace the setup the match will start with. The host may change the map and
   the rules WHILE people are sitting in the lobby, and START sends whatever is held here,
   so this has to be called whenever the screen changes or the joiners would be started on
   the settings the room was opened with rather than the ones they were reading. */
void nm_lobby_set_setup(const NmSetup* setup);
/* HOST ONLY: start everyone. Refuses (0) unless all_ready; otherwise answers 1.
   THE START MAY NOT GO OUT ON THIS CALL. The lookahead the START carries is chosen from
   every seated joiner's round trip, and a joiner who has just readied may not have
   answered a ping yet, so the press is held PENDING while nm_lobby_poll measures, and the
   final roster goes out from the poll the moment every seat has a round trip, or after a
   bounded wait. The caller simply keeps polling, as it already does. A pending start is
   dropped if the room changes under it (a seat leaves or is removed, a READY goes out,
   the host changes the room or leaves); nobody new is seated while it waits, just as
   nobody was once START closed the room; and pressing again while pending or starting
   starts nothing twice. Every door goes through here, so no door can skip the
   measurement.
   ONCE THE START HAS GONE OUT IT CANNOT BE TAKEN BACK. If it cannot finish (a seat leaves
   or is removed first, or not everyone answers within 15 s) while anybody still seated
   may have received it, the room FAILS, with a goodbye to everyone and the reason in
   nm_lobby_error; and a failed room refuses START (0). */
int nm_lobby_start(void);
/* START has been pressed and is waiting on a round trip: the screens say so. */
int nm_lobby_start_pending(void);
/* The name the host gave the game, for the joiner's lobby caption. */
const char* nm_lobby_name(void);
/* THIS MACHINE'S OWN HANDLE, the mirror of 1995's MPlayerName. Set it BEFORE hosting or
   joining: it is what the HELLO carries and what the host writes into seat 0. */
void nm_set_player_name(const char* name);
const char* nm_player_name(void);
const char* nm_seat_name(int seat);   /* the handle in that seat, never NULL */
const NmSetup* nm_lobby_setup(void);
/* A printable sentence for the screen. Never NULL. */
const char* nm_lobby_error(void);
int nm_lobby_locked(void);           /* the room carries a passcode: the advert's lock */
int nm_lobby_starting(void);         /* START has gone out; the roster is settled       */
/* Leave without starting. Safe to call in any state. */
void nm_lobby_cancel(void);

int nm_active(void);
int nm_seat(void);
/* How many seats the match ended up with, and how many of them are people. Both are
   decided by the HOST and adopted by every joiner, so every peer answers the same. */
int nm_seats(void);
/* Which seats the current turn is still waiting on, as a bit per seat, or 0 when the turn
   is ready or no match is live. For a message that names who is late rather than only
   that somebody is. */
unsigned nm_waiting_mask(void);
int nm_humans(void);
/* How many humans have actually joined so far, for a host that is still waiting. */
int nm_joined(void);
const NmSetup* nm_setup(void);
/* ---- THE MAP CHECK, AND WHY IT NEEDS A HOOK -------------------------------------
   netmatch knows nothing about files. It cannot open a scenario, so it cannot answer
   "do I have this map, and are my bytes the host's bytes" on its own -- and until
   7 Sep 2026 it did not try: the joiner compared the hash of whatever map its OWN
   screen happened to be showing against the host's, which is a comparison between two
   unrelated things. Through the GUI it never even did that, because that path passed a
   literal zero and zero disables the test. So two players with different copies of the
   same map started a match and parted on the first frame that read a cell.

   The caller installs a hasher: given a scenario name, return the fingerprint of the
   local copy, or 0 if this machine does not have it. netmatch then asks the question at
   the only moment it is answerable -- when the WELCOME has just NAMED the host's map --
   and refuses in words. The host installs the same hook and it keeps the room's
   advertised fingerprint following the map the host picks. */
typedef unsigned (*NmMapHashFn)(const char* scenario, void* user);
void nm_set_map_hasher(NmMapHashFn fn, void* user);
/* What this peer announced about itself. The browser greys a row it would be refused
   from, and it can only do that if it can ask what "refused" would compare against. */
/* ---- RELAYED PLAY -------------------------------------------------------------------
   Arm the NEXT room opened or joined on this machine to run through a relay instead of a
   plain socket. One shot: whichever door runs next takes the request and disarms it, so a
   door that fails its own checks cannot leave the next one armed.

   `my_id` is this peer's own tunnel id -- a host draws one that fits a room code
   (rc_draw_host_id), a joiner draws any (rc_draw_peer_id) because it is never written
   down. `host_id` is the id decoded from the room code, and is 0 on a host, which IS the
   host. Nothing above this changes: net_recv rewrites an arriving datagram's sender to
   its tunnel id and the rest of the match layer never learns there is a relay.

   The two BLOCKING doors (nm_host, nm_join) refuse to be relayed. The blocking host has
   no passcode check at all, so relaying it would put a door-less room on the internet. */
void nm_relay_next(const char* tunnel_host, unsigned short tunnel_port,
                   unsigned long my_id, unsigned long host_id);
/* Is the socket this match is running on a relayed one? For the screens, which say
   different things about a relayed failure, and for gates. */
int nm_is_relayed(void);
/* THE ROOM'S OWN CODE, for a HOST to read out: "#K7M-3QX", or "" when this room is not
   relayed. It is this peer's own tunnel id rendered by rc_encode, kept here so the screen
   asks the network what the room is rather than keeping a second copy that could drift. */
const char* nm_room_code(void);
unsigned long nm_room_id(void);

/* ---- HOW FAR AWAY A ROOM IS, ASKED BEFORE JOINING IT ------------------------------
   A PROBE is the one message a lobby host answers from anybody at all. It is handled
   ahead of every seat, version and passcode check, it seats nobody and changes nothing,
   and its answer is never larger than the question, so a forged sender cannot use a
   host to multiply traffic at somebody else. The asker stamps its own clock into the
   probe and the host sends those bytes back, so a round trip is one machine's clock read
   twice and the host keeps nothing per probe.

   THE WIRE, sixteen bytes each way, little endian like every other word in netmatch:
     PROBE     asker -> host   NM_PROBE_WORD     nonce  asker's ms stamp  0
     PROBEACK  host -> asker   NM_PROBEACK_WORD  nonce  asker's ms stamp  host's NM_VERSION
   A probe shorter than NM_PROBE_BYTES is not answered, and a longer one is answered in
   NM_PROBE_BYTES. A host answers one sender (one IP whatever its port, or one tunnel id)
   at most NM_PROBE_PER_SOURCE times in each second it counts for that sender (so up to
   twice that across the edge of two), and
   everybody together a few dozen times, and writes what it answered in one log line a
   second at most (NET|probes|answered=|limited=|short=).

   NO VERSION BUMP, the rule NM_NEED follows. A host from before this word files it with
   every other datagram from a stranger it does not recognise: no answer, no state, one
   log line a second at most. A match already running drops anything from outside the
   match, and the blocking command-line host reads nothing but a HELLO. Refusing joins
   between two builds that play each other perfectly well, for a message nobody has to
   understand, is not what the version is for. What that costs is that silence cannot be
   told apart from an old host, a lost datagram or a room that has gone, so all three
   read the same.

   WHAT THE NUMBER MEANS. A lobby screen reads its socket about ten times a second, so an
   answer can wait up to one of those polls before it is sent. Several probes that land
   at different points of that poll, and the MINIMUM of their round trips, remove most of
   that wait. It is a floor on how a match will feel, not a forecast: a match runs at its
   slowest seat, and this measures one link.

   WHY THE GAPS ARE UNEVEN. The poll is not every 100 ms. A screen polls on the first
   frame after 100 ms have passed, so its period is anything from about 100 to 140 ms
   depending on how long a frame takes, and the asker cannot know which. Any one fixed gap
   lines up with some period in that range and then every probe waits the same long
   stretch. Measured on loopback, with the host polled on a fixed period, the least of
   four probes 130 ms apart waited up to 127 ms against a 130 ms poll and up to 78 ms
   against 117 ms; with the gaps below it waited at most 56 ms against either, and against
   a frame loop of 20 to 45 ms frames at most 55 where 130 ms apart waited up to 96. The
   price is time, and a little where 130 ms already suits the poll: an answered row shows
   "..." for about 545 ms plus its round trip rather than 390, a silent one for about two
   seconds rather than 1.9, and against a poll of exactly 100 ms the wait averaged 17 ms
   (at most 44) where 130 ms apart averaged 14 (at most 31).
   ---------------------------------------------------------------------------------- */
#define NM_PROBE_WORD       0x424F5250u   /* 'PROB' asker -> host */
#define NM_PROBEACK_WORD    0x4B425250u   /* 'PRBK' host -> asker */
#define NM_PROBE_BYTES      16
#define NM_PROBE_PER_SOURCE 8     /* answers to one sender per second it counts, host side */
#define NM_PROBE_COUNT      4     /* probes per target                                   */
#define NM_PROBE_GAP1_MS    175   /* from the first probe to the second                  */
#define NM_PROBE_GAP2_MS    180   /* second to third                                     */
#define NM_PROBE_GAP3_MS    190   /* third to fourth                                     */
/* The gap after probe i (0, 1 or 2), and the whole span from the first to the last. */
#define NM_PROBE_GAP_MS(i)  ((i) <= 0 ? NM_PROBE_GAP1_MS : (i) == 1 ? NM_PROBE_GAP2_MS \
                                                               : NM_PROBE_GAP3_MS)
#define NM_PROBE_SPAN_MS    (NM_PROBE_GAP1_MS + NM_PROBE_GAP2_MS + NM_PROBE_GAP3_MS)
#define NM_PROBE_WAIT_MS    1500  /* after the last one, before a target reads as silent */
#define NM_PROBE_KEY_MAX    40

/* THE BROWSER'S HALF. Nothing here blocks: start a target once, then poll it every
   frame; the poll does the sending and the receiving. A target is named by a KEY:
     "#K7M-3QX"           a relayed room, asked through the relay (nm_probe_set_relay)
     "203.0.113.7:17421"  a direct host. NUMERIC ONLY, so a frame never waits on DNS.

   A DIRECT PROBE SENDS THIS PLAYER'S ADDRESS TO THAT HOST. A relayed one does not: the
   host sees a random tunnel id. Nothing in this file probes an address it was not handed,
   so WHEN a direct row is probed is entirely the caller's decision.

   A RELAYED PROBE SPENDS ONE CLIENT SLOT ON THE RELAY. The relay forwards only to ids it
   has already seen and the answer is addressed to the asker, so the asker must register
   an id of its own. It registers ONE, on one socket, and every relayed target shares it.
   CnCNet's servers allow eight clients per public address, and a join made later from
   the same address counts against the same eight. The slot is refreshed only while a
   relayed target is being probed, and the relay lets it go its own timeout after that
   (30 or 60 s, plus up to a minute more before its sweep). The socket and its id are kept
   across nm_probe_clear_all for exactly this reason: a new registration on every REFRESH
   would spend a new slot each time, and a player who refreshed eight times inside a
   minute could lock their own join out of the relay.

   HOW LONG. A target's first probe goes out on the poll that starts it (at most
   NM_PROBE_RUNNING_MAX targets run at once; the rest wait their turn and read as
   PROBING), the rest follow at the three gaps above, and a target is finished when every
   probe has been answered or NM_PROBE_WAIT_MS after the last one went out: about two
   seconds (NM_PROBE_SPAN_MS + NM_PROBE_WAIT_MS) before a silent target reads as silent.

   A ROUND TRIP ENDS WHEN THE ANSWER ARRIVED, NOT WHEN THE POLL READ IT. On macOS and
   Linux the system stamps each answer's arrival, so polling once a frame costs the
   number nothing. Windows has no such stamp here, so there an answer is timed when the
   poll reads it and every reading includes up to one of the caller's poll intervals.
   Measured on loopback without the stamp: a prober polled every 8 ms read 8 ms and one
   polled every 16 ms read 16, whatever the round trip was, and the minimum of four could
   not remove it because all four carried the same gap. The schedule and the round trip
   are timed on a clock that does not step when the system sets its time (the performance
   counter on Windows, the monotonic clock elsewhere), and a reading no schedule could
   produce, below zero or past the last wait, is dropped rather than shown. */
#define NM_PROBE_RUNNING_MAX 8

enum {
    NM_PROBE_UNKNOWN = 0,  /* never started, cancelled or cleared                          */
    NM_PROBE_PROBING,      /* waiting its turn or asking; rtt_ms is the best so far, or -1 */
    NM_PROBE_ANSWERED,     /* finished, answered at least once; rtt_ms is the minimum      */
    NM_PROBE_SILENT,       /* finished with no answer at all                               */
    NM_PROBE_INVALID       /* the key is neither a room code nor a numeric address:port   */
};

typedef struct NmProbeResult {
    int      state;     /* NM_PROBE_*                                                  */
    int      rtt_ms;    /* minimum round trip over the answers so far, -1 before one   */
    int      answers;   /* how many probes were answered                               */
    int      sent;      /* how many went out, 0..NM_PROBE_COUNT                        */
    unsigned version;   /* the host's NM_VERSION from its answer, 0 before one         */
} NmProbeResult;

/* The relay a "#CODE" key is asked through. NM_RELAY_HOST:NM_RELAY_PORT until set; a
   port of 0 means NM_RELAY_PORT. A different relay drops every relayed target still in
   flight and the registration on the old one. A numeric host keeps the frame off DNS. */
void nm_probe_set_relay(const char* host, unsigned short port);
/* Start probing `key`. Returns 1 when it is probing or already has a result (a key that
   has one is NOT probed again: cancel or clear it first), 0 when the key is invalid (it
   then polls as NM_PROBE_INVALID, except a key of NM_PROBE_KEY_MAX bytes or more, or with
   a byte outside printable ASCII, which is not kept at all and polls as UNKNOWN) or every
   slot is busy with a target still probing. Every spelling of one room code (either case,
   I or L for 1, O for 0) is the same target, here and in poll and cancel. */
int  nm_probe_start(const char* key);
/* Service every target and report `key`. Returns the state, and fills `out` when given.
   Cheap enough to call for every row every frame: the sockets are serviced once per
   millisecond however many rows ask. */
int  nm_probe_poll(const char* key, NmProbeResult* out);
/* Service every target without asking about one. nm_probe_poll does this for you. */
void nm_probe_pump(void);
/* Forget one target: it stops sending and polls as NM_PROBE_UNKNOWN. */
void nm_probe_cancel(const char* key);
/* Forget every target, for a REFRESH. The sockets and the relay registration stay. */
void nm_probe_clear_all(void);
/* Forget every target and close the sockets, for leaving the game list. */
void nm_probe_shutdown(void);

/* The host has left and this peer is not the host: the relay every other player was
   reached through has gone, so the match is over rather than merely short-handed. */
int nm_host_gone(void);
unsigned nm_abi(void);
unsigned nm_scen(void);

void nm_set_engine(NmDrainFn drain, NmPostFn post, void* user, int event_size);

/* THE TURN, in the order the host's loop calls them:
   nm_begin_turn  drain local orders, pack this turn, send it. 0 means the match is over
                  (a queue overflowed or the peer is gone).
   nm_turn_ready  pump the socket; may the executing turn run yet? Non blocking.
   nm_wait_turn   the same, blocking up to timeout_ms, for the scripted paths.
   nm_run_turn    deliver the executing turn's orders to the engine, both seats in seat order.
   Then the host advances the engine ONE tick. */
int  nm_begin_turn(void);
void nm_service(void);   /* drain the socket without advancing a turn (chat, goodbyes) */
int  nm_surrender(void); /* resign: the host names the turn, everybody applies it there */
int  nm_surr_due(void);  /* which seat's surrender lands on this turn, or -1 */
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
/* A SEAT WHOSE DEPARTURE IS DUE, or -1: true straight after the turn the host named for it
   has run, and never before. Every peer answers with the same seat on the same turn, which
   is what lets the game destroy that army on one frame everywhere instead of parting the
   simulation. Answered once per seat. */
int nm_left_due(void);

unsigned nm_turns_run(void);
void nm_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif
