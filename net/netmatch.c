/*
 * netmatch.c -- one lockstep match between two peers. See netmatch.h.
 *
 * gnu89 rather than c89 because it sleeps, and sleeping is a platform call.
 */
#include "netmatch.h"
#include "roomcode.h"
#include "lockstep.h"
#include "net_udp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
static void nm_nap(int ms) { Sleep((DWORD)ms); }
static unsigned nm_now_ms(void) { return (unsigned)GetTickCount(); }
#else
#include <unistd.h>
#include <sys/time.h>
static void nm_nap(int ms) { usleep((useconds_t)ms * 1000); }
static unsigned nm_now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned)(tv.tv_sec * 1000u + tv.tv_usec / 1000u);
}
#endif

/* Wire words. Distinct from LS_MAGIC so the pump can route by the first four bytes, and
   from netcheck's, so a stray netcheck on the same port is refused rather than obeyed. */
#define NM_HELLO   0x4D4C4548u /* 'HELM' joiner -> host: version, abi hash, scenario hash */
#define NM_WELCOME 0x4D434C57u /* 'WLCM' host -> joiner: abi hash, scenario hash, the setup */
#define NM_REFUSE  0x4D455052u /* 'RPEM' host -> joiner: why not */
#define NM_READY   0x4D594452u /* 'RDYM' joiner -> host: setup adopted, my seat */
#define NM_START   0x4D545253u /* 'STRM' host -> all: the FINAL setup, play now */
#define NM_SACK    0x4B434153u /* 'SACK' joiner -> host: start adopted */
/* A PLAYER'S OWN READY TICK, which is a different thing from NM_READY and the two must
   not be confused. NM_READY is protocol: "I received the setup". NM_TICK is a PERSON
   pressing the READY button, and it is what the host's START waits on. They were nearly
   given one name, which would have made a lobby that starts as soon as everybody has
   merely connected. */
#define NM_TICK    0x4B434954u /* 'TICK' any -> all: seat, ready flag (relayed) */
#define NM_KICK    0x4B43494Bu /* 'KICK' host -> all: this seat has been removed */
#define NM_SYNC    0x4D434E53u /* 'SNCM' any -> all: seat, frame, world hash (relayed) */
#define NM_BYE     0x4D455942u /* 'BYEM' any -> all: the seat that is leaving (relayed) */
#define NM_SURR    0x52525553u /* 'SURR' any -> all: the seat that has resigned (relayed) */
/* 'NEED' any -> the seats named: "I am stuck on this turn, waiting for you". Seat, the turn
   this peer is waiting to execute, and the scheduler's waiting mask. Every seat named in
   the mask answers with its own orders from that turn on (ls_pack_from), and the host
   passes the question to the joiners it names, exactly as it passes their turn packets
   on. Match only. A peer that does not know this word ignores it, which leaves it exactly
   as stuck as it was before the word existed, so it needs no version of its own. */
#define NM_NEED    0x4445454Eu
/* 'CHAT' any -> all: seat, length, bytes (relayed by the host). THE FIRST VARIABLE-LENGTH
   MESSAGE IN THIS FILE: every other handler guards on a fixed `n >= size`; this one reads
   a length out of the payload and checks it against BOTH the packet and NM_CHAT_MAX
   before a byte is copied. Lobby only. */
#define NM_CHAT    0x54414843u
/* 'SEAT' joiner -> host: the four things about a seat that belong to the person in it,
   which until now never left their machine. The host is the only writer of the setup, so
   a joiner ASKS and the host applies what the room's rules allow. */
#define NM_SEATPREF 0x54414553u
/* THE ROUND TRIP, measured the only way that needs no agreed clock: the host stamps its
   own millisecond counter into a ping, the joiner echoes those bytes back untouched, and
   the host subtracts. Nothing compares multiple machines' clocks, so nothing has to sync them.
   Lobby only -- once the match is running the scheduler's own waiting IS the measurement,
   and the number can no longer be acted on anyway. */
#define NM_PING    0x474E4950u   /* 'PING' host -> joiner: echo these bytes back */
#define NM_PONG    0x474E4F50u   /* 'PONG' joiner -> host: the same bytes, unchanged */
/* 2 rather than 1 since the scenario hash joined the handshake: HELLO grew four bytes
   and WELCOME's setup moved four along, so a v1 peer would read the setup off by a word
   and adopt garbage. Nothing has shipped a match, so this costs nobody an upgrade, and
   refusing by version is exactly what the field is for.

   3 since the match became a star of up to eight: WELCOME now names the seat the host
   assigned, START carries the final roster once everyone is in, and SYNC names the seat
   it came from so a hash can be attributed. A v2 peer would take a seat nobody gave it. */
#define NM_VERSION 10  /* 4: the tech level travels; 5: a per-seat mode does too;
                          6: AI takeover and short game; 7: player names, so the
                          HELLO carries a handle and the setup carries eight;
                          8: the start waits for every seat's round trip.

                          9 BECAUSE THE ROOM CHANGED, NOT BECAUSE THE MESSAGE DID.
                          NM_NEED is safe for an older peer to receive: it does not
                          know the word and ignores it. What is NOT safe is the room
                          it is now part of. A seat whose round trip could not be
                          measured now seats the room at the top of the lookahead
                          band, and a two-way outage at that lookahead deadlocks
                          unless somebody asks for the turn by number. An older peer
                          never asks and never answers, so it would sit in a room
                          whose settings assume it does: deaf for ever when the loss
                          is its way, and leaving the new peer stuck for ever when it
                          is the one holding a turn. A goodbye also carries the turn
                          it lands on now, and an older peer sends one without it.
                          Refusing by version is what the field is for, and it refuses
                          by name in the handshake.

                          10 BECAUSE A GOODBYE'S TURN NOW MEANS SOMETHING ELSE, not
                          because a byte moved. A departure is applied straight after
                          the named turn runs, where a 9 applies it after the turn before,
                          so a 9 and a 10 in one room would destroy the same house one
                          tick apart on every departure. A 10 also waits on a departed
                          seat until the host says so and asks the host for that seat's
                          missing turns, which a 9 host never answers. */
#define NM_JOIN_TIMEOUT_MS 10000u   /* HELLOs unanswered this long is failed, by name */
/* HOW LONG A PRESSED START WAITS FOR A SEAT THAT HAS NOT ANSWERED A PING, and how often
   that seat is asked while it does. About a dozen asks, fewer when the caller polls more
   slowly than the interval: one lost datagram cannot cost a measurement, and a seat that
   answers none of them is not a quick seat that was unlucky. */
#define NM_START_MEASURE_MS 3000u
#define NM_MEASURE_PING_MS 250u
/* HOW OFTEN A READY THE HOST HAS NOT ECHOED IS SAID AGAIN. Every other piece of lobby
   state already repeats until the far end answers -- the HELLO until a WELCOME, the
   WELCOME until its NM_READY, the START until its NM_SACK -- and the READY TICK was the
   one that did not: it went out once, nothing acknowledged it, and a single dropped
   datagram left the host holding a seat that was not ready while the person's own screen
   said it was. The room could then never start and neither screen could say why; the only
   way out was to guess and toggle the button off and on. Measured on a relayed pair that
   failed to start exactly so: the joiner logged its ready and the host never saw one.
   Four a second, the rate the room is already pinged at. */
#define NM_READY_RESEND_MS 250u

#define NM_HASH_RING 128
#define NM_RESEND_MS 100      /* re-send the last turn packet while waiting on the peer */
#define NM_SILENCE_MS 30000   /* a peer silent this long has left */

static NetSock* s_sock = NULL;
/* THE SEAT TABLE. s_peers[i] is the address of the peer sitting at seat i, and s_have[i]
   says whether that entry means anything. The HOST fills every entry as joiners arrive
   and is the only peer that needs them all. A JOINER fills exactly one, seat 0, the host,
   because in a star a joiner never sends anywhere else. This is the whole difference
   between two peers and eight. */
static NetAddr s_peers[NM_MAX_SEATS];
static int s_have[NM_MAX_SEATS];
static int s_active = 0;
static int s_seat = -1;
static int s_seats = 0;    /* every seat, humans and computers alike */
static int s_humans = 0;   /* the ones with a person behind them */
static int s_is_host = 0;
static NmSetup s_setup;
/* The two fingerprints this peer agreed the match on. They are kept past the handshake
   for one reason: they are the first question anyone asks a desync report. A divergence
   between peers that agreed on BOTH is the engine disagreeing with itself and is a bug in
   here; a divergence where scen reads 00000000 was never checked at all, because one side
   could not hash its scenario, and the map is then the first thing to rule out rather than
   the last. Printing them beside the alarm is the difference between a report that can be
   triaged from the log and one that needs the reporter back at the keyboard. */
static unsigned s_abi = 0;
static unsigned s_scen = 0;

/* ---- the lobby ---- */
static int s_lobby_state = NM_LOBBY_IDLE;
static char s_lobby_name[NM_NAME_MAX];
static unsigned s_pass = 0;              /* 0 = open. See nm_pass_hash. */
static int s_ack[NM_MAX_SEATS];          /* protocol: this seat has the setup   */
/* THE WORST ROUND TRIP SEEN PER SEAT, in ms, or -1 for never answered. The WORST and not
   an average on purpose: the room runs at the speed of its slowest link, so a seat that
   is usually quick and occasionally terrible is a terrible seat. */
static int s_rtt[NM_MAX_SEATS];
static unsigned s_last_ping_ms = 0;
static int s_ready[NM_MAX_SEATS];        /* a PERSON pressed READY              */
/* A JOINER'S OWN READY AS THE HOST HAS CONFIRMED IT, and when this peer last said so.
   The host echoes a seat's tick back to that seat, and the echo is the acknowledgement:
   while these disagree with s_ready[s_seat] the tick is still in the air or was lost, and
   the poll says it again. Host side both are unused: seat 0 is ready by construction. */
static int s_ready_wire = 0;
static unsigned s_ready_sent_ms = 0;
/* HOW MANY TIMES THE CURRENT PRESS HAS HAD TO BE SAID AGAIN. Kept for the log rather than
   for the protocol: the first repeat and the agreement that ends it are one line each, so
   a room that would not start can be read afterwards from what each end wrote down. That
   is how the lost ready was found in the first place, from a joiner's log saying it had
   readied beside a host's that never mentioned it. */
static int s_ready_again = 0;
static int s_sack[NM_MAX_SEATS];         /* this seat acknowledged the START    */
static char s_lobby_err[192];
static int s_lobby_humans = 0;
static unsigned s_last_hello_ms = 0;
static unsigned s_last_welcome_ms = 0;
static unsigned s_last_start_ms = 0;
static int s_starting = 0;               /* host: START sent, waiting on acks    */
static unsigned s_start_began_ms = 0;    /* so a start cannot wait for ever      */
/* START PRESSED, NOT YET SENT: the host is waiting for every seated joiner's round trip,
   because the lookahead the START carries is chosen from them. See lobby_try_start. */
static int s_start_pending = 0;
static unsigned s_start_pressed_ms = 0;
static int s_kicked = 0;                 /* this peer was removed by the host    */
static unsigned s_lobby_open_ms = 0;
/* DID THE RELAY ITSELF ANSWER? A joiner that hears nothing for ten seconds has two very
   different problems -- the relay is unreachable from this computer, or the relay is fine
   and no room wears that code -- and they look identical from the inside. The relay's own
   ping costs fifty bytes, needs no registration and no state, and separates them. Asked
   once when the join opens; the answer is in hand before the timeout has to say anything.
   0 not asked, 1 asked, 2 answered. */
static int s_relayPinged = 0;
static int s_relayHeard = 0;
static unsigned char s_relayNonce[4];

/* ---- the lobby chat ---- */
static char s_chat[NM_CHAT_LOG][NM_CHAT_MAX];
static int s_chat_seat[NM_CHAT_LOG];
static int s_chat_total = 0;   /* lines ever pushed; the ring holds the last NM_CHAT_LOG */

/* SANITISED ON RECEIPT. A remote peer is not bound by the local field's rule, so every
   byte outside printable ASCII becomes '?' here, once, before anything draws it. */
/* ONE WRITER FOR EVERY NAME that enters this file, local or remote: clipped, terminated,
   and every byte outside printable ASCII turned into '?'. Same rule as a chat line, for
   the same reason: it is drawn. */
static void nm_name_copy(char* dst, const char* src)
{
    int i = 0;
    if (!dst) return;
    if (src) {
        for (; i < NM_PLAYER_NAME_MAX - 1 && src[i]; i++) {
            const unsigned char c = (unsigned char)src[i];
            dst[i] = (c < 32 || c >= 127) ? '?' : (char)c;
        }
    }
    dst[i] = '\0';
}

/* THIS MACHINE'S OWN HANDLE: 1995's MPlayerName, one global, entered once. */
static char s_my_name[NM_PLAYER_NAME_MAX] = "PLAYER";
void nm_set_player_name(const char* name)
{
    nm_name_copy(s_my_name, (name && *name) ? name : "PLAYER");
    if (!s_my_name[0]) nm_name_copy(s_my_name, "PLAYER");
}
const char* nm_player_name(void) { return s_my_name; }

static void nm_chat_push(int seat, const char* text, int len)
{
    char* dst = s_chat[s_chat_total % NM_CHAT_LOG];
    int i;
    if (len < 0) len = 0;
    if (len > NM_CHAT_MAX - 1) len = NM_CHAT_MAX - 1;
    for (i = 0; i < len; i++) {
        const unsigned char c = (unsigned char)text[i];
        dst[i] = (c < 32 || c >= 127) ? '?' : (char)c;
    }
    dst[len] = '\0';
    s_chat_seat[s_chat_total % NM_CHAT_LOG] = seat;
    s_chat_total++;
}

/* ---- THE SEAT TABLE, AS THE HOST'S SETUP DESCRIBES IT (version 5, 5 Sep 2026) ----
   A room is `seats` wide and every seat has a MODE. The host's screen sets OPEN, BOT or
   BLOCK per seat; who is actually sitting in one is the host's s_have[] table. The two
   are folded together HERE and nowhere else, so that what leaves this machine in a
   WELCOME or a START is one description of the room: a seat with a person in it is
   HUMAN, a HUMAN seat with nobody in it is OPEN, and everything past the room is BLOCK.
   `humans` is then simply the count of seats a person sits in or will sit in, and
   is_ai[] is the count of BOTs said one seat at a time. Nothing reads s_lobby_humans
   to decide anything any more: it is the size the room opened at, and it is printed. */
static int lobby_room(void)
{
    int r = s_setup.seats;
    if (r < 2) r = 2;
    if (r > NM_MAX_SEATS) r = NM_MAX_SEATS;
    return r;
}

static int lobby_seat_wants_person(int i)
{
    if (i < 0 || i >= lobby_room()) return 0;
    if (i == 0) return 1;
    return s_setup.mode[i] == NM_SEAT_HUMAN || s_setup.mode[i] == NM_SEAT_OPEN;
}

static void lobby_normalise(void)
{
    int i, humans = 0, room;
    if (!s_is_host) return;
    room = lobby_room();
    s_setup.seats = room;
    for (i = 0; i < NM_MAX_SEATS; i++) {
        unsigned char m = s_setup.mode[i];
        if (i >= room) m = NM_SEAT_BLOCK;
        else if (i == 0 || s_have[i]) m = NM_SEAT_HUMAN;
        else if (m == NM_SEAT_HUMAN) m = NM_SEAT_OPEN;
        else if (m != NM_SEAT_BOT && m != NM_SEAT_BLOCK) m = NM_SEAT_OPEN;
        s_setup.mode[i] = m;
        /* THE NAME FOLLOWS THE MODE, in the one place the room is folded together. A
           computer is called COMPUTER, which is the word 1995 uses for a non-human
           house; an empty seat has no name. Seat 0 is always HUMAN and keeps its own. */
        if (m == NM_SEAT_BOT) nm_name_copy(s_setup.name[i], "COMPUTER");
        else if (m != NM_SEAT_HUMAN) s_setup.name[i][0] = '\0';
        s_setup.is_ai[i] = (unsigned char)(m == NM_SEAT_BOT);
        if (m == NM_SEAT_HUMAN || m == NM_SEAT_OPEN) humans++;
    }
    s_setup.humans = humans;
}

/* The lowest seat a joiner may be given: OPEN and empty. -1 when the room has none,
   which is what "full" means now -- not "as many people as the host asked for" but
   "no seat left that is anybody's to take". */
static int lobby_open_seat(void)
{
    int i;
    for (i = 1; i < lobby_room(); i++)
        if (s_setup.mode[i] == NM_SEAT_OPEN && !s_have[i]) return i;
    return -1;
}

/* THE SCHEDULER STATE IS STATIC, NOT A LOCAL, and that is not style: it is a megabyte,
   and Windows gives a program 2 MB of stack. netcheck.exe learned that on launch. */
static LsState s_ls;

static NmDrainFn s_drain = NULL;
static NmPostFn s_post = NULL;
static void* s_user = NULL;
static int s_event_size = 22;

static unsigned char s_last_pkt[LS_PACKET_MAX];
static int s_last_pkt_len = 0;
static unsigned s_last_send_ms = 0;
/* WHEN EACH SEAT WAS LAST HEARD FROM, one clock per seat rather than one for the match.
   With two peers "the peer went quiet" and "the match went quiet" were the same sentence;
   with eight they are not, and a single clock would keep a match alive on the strength of
   seven peers while the eighth was gone. */
static unsigned s_last_heard_ms[NM_MAX_SEATS];
static int s_left[NM_MAX_SEATS];
/* THE TURN A DEPARTURE TAKES EFFECT ON, which every peer must agree about or two
   machines destroy the same army on different frames and the match parts. The host
   stamps it far enough ahead that every peer has the goodbye before it arrives; a peer
   with no stamp yet holds LS_TURN_NONE. */
#define LS_TURN_NONE 0xFFFFFFFFu
static unsigned s_left_turn[NM_MAX_SEATS];
static int s_left_done[NM_MAX_SEATS];

/* A SURRENDER IS NOT A GOODBYE, and it needs its own table for three reasons, each on its
   own sufficient. The seat that resigns applies it to ITSELF, so it cannot ride the
   departure path, which deliberately skips this peer's own seat. The player is still
   there, watching, and goes on sending turn packets, so the seat must NOT be marked
   absent: doing so would tell every other peer to stop waiting for turns that are still
   arriving, and there is no way back. And a departure is what a scripted run reads as the
   match ending, which a surrender is not.
   
   What it DOES share with a goodbye is the only part that matters: the host names the
   turn it takes effect on and relays that, so every machine kills or converts the same
   house on the same frame and no two worlds part. */
static unsigned s_surr_turn[NM_MAX_SEATS];
static int s_surr_done[NM_MAX_SEATS];
static int s_fatal = 0;

static unsigned s_my_hash[NM_HASH_RING];
static unsigned s_my_hash_frame[NM_HASH_RING];
/* One hash ring per seat: every peer checks every other peer it hears from, so a
   disagreement is attributed to the pair that disagreed rather than to "the match". */
static unsigned s_peer_hash[NM_MAX_SEATS][NM_HASH_RING];
static unsigned s_peer_hash_frame[NM_MAX_SEATS][NM_HASH_RING];
static int s_desynced = 0;
/* THE HOST HAS LEFT AND THIS IS NOT THE HOST. In a star every link is a link to the
   host, so when it goes nobody can reach anybody: the remaining players are not a match
   with a gap in it, they are several people alone. Without this they sat in a frozen
   world until the thirty-second stall gave up, which is a long time to look at a
   picture that has stopped. */
static int s_host_gone = 0;
static unsigned s_desync_frame = 0;
static unsigned s_synced_frames = 0;
/* HOW OFTEN THIS MATCH NEEDED A TURN ASKED FOR (nm_ask_for_turn) and how often this peer
   answered somebody else's request. Printed when the match ends, because a repair that
   works is otherwise invisible: the match simply does not stop. */
static unsigned s_turns_asked = 0;
static unsigned s_turns_answered = 0;
/* THE ANSWERING SIDE'S OWN BEAT. The question is throttled where it is asked; the answer
   was not throttled anywhere, and an answer is up to a kilobyte where the question is
   sixteen bytes. One answer, and one forward, per asking seat per NM_RESEND_MS, so the
   cost of asking is bounded by the rate the asker is allowed to ask at rather than by the
   rate it chooses to. */
static unsigned s_need_ans_ms[NM_MAX_SEATS];
static unsigned s_need_fwd_ms[NM_MAX_SEATS];
static unsigned s_last_answer_said_ms = 0;
/* THE TURN EACH SEAT WENT QUIET ON, as this peer has been told it, so the line saying so
   is printed once rather than on every repeat of a goodbye. */
static unsigned s_absent_at[NM_MAX_SEATS];
/* WHAT THE HOST HAS RECEIVED FROM EACH SEAT, KEPT SO IT CAN ANSWER FOR A SEAT THAT HAS
   LEFT. A departing seat is waited for up to the turn the host names, and a survivor that
   never received some of the turns below it cannot ask the seat that sent them, because
   that seat is gone. In a star every one of those turns came in through the host, and the
   host names the first turn it does NOT hold, so the host holds every turn a survivor can
   still be missing. It sends them again exactly as they first arrived.
   This replaces a two second timer that let the turn through instead. That timer was the
   one place a peer acted on its own evidence about a departure, and a peer that skipped
   the gap executed fewer of that seat's orders than a peer that had them, which is a
   desync by design.
   The newest LS_HISTORY packets per seat, one per distinct top turn, so that the repeats a
   waiting seat sends of one packet cannot push the older ones out. Written only on the
   host, and only while the seat is still in the match. */
#define NM_KEEP_PKTS LS_HISTORY
static unsigned char s_keep_pkt[NM_MAX_SEATS][NM_KEEP_PKTS][LS_PACKET_MAX];
static int s_keep_len[NM_MAX_SEATS][NM_KEEP_PKTS];
static unsigned s_keep_top[NM_MAX_SEATS][NM_KEEP_PKTS];
static int s_keep_next[NM_MAX_SEATS];
/* ONE ANSWER FOR DEPARTED SEATS PER ASKING SEAT PER NM_RESEND_MS, on its own clock so that
   it never takes the slot the host answers for its own turns in. */
static unsigned s_need_gone_ms[NM_MAX_SEATS];
static unsigned s_gone_answer_said_ms = 0;

/* A TEST INSTRUMENT, OFF UNLESS THE ENVIRONMENT ASKS FOR IT. CNC3D_NETTEST_HOLD_BYE set to
   "<seat>:<ms>" makes the HOST hold every goodbye it sends to that seat for that many
   milliseconds, while turns go on flowing. It forces the one arrival order that a loopback
   match otherwise reaches only by chance: a survivor that already holds every turn the
   departing seat sent, and runs them, before it is told which turn the departure lands
   on. Nothing in play sets it. */
#define NM_TEST_HOLD_MAX 64
static int s_test_hold_seat = -1;
static unsigned s_test_hold_ms = 0;
static unsigned char s_test_held[NM_TEST_HOLD_MAX][12];
static unsigned s_test_held_due[NM_TEST_HOLD_MAX];
static int s_test_held_n = 0;
/* THE SAME KIND OF INSTRUMENT FOR THE OTHER ORDER A LOOPBACK MATCH NEVER REACHES: a
   survivor that hears the goodbye but never receives the departing seat's last turns.
   CNC3D_NETTEST_CUT_RELAY set to "<from>:<to>:<turn>" makes the HOST stop passing seat
   <from>'s turn packets on to seat <to> from the first one whose newest turn is <turn> or
   later. The host still takes its own copy of each. Nothing in play sets it. */
static int s_test_cut_from = -1;
static int s_test_cut_to = -1;
static unsigned s_test_cut_turn = 0;
/* AND ONE FOR THE ORDER IN WHICH THE HOST IS THE LATE PEER: a host that has already run the
   departing seat's last turn when it reads the goodbye, while a survivor has not yet run
   it. The host reaches that order on a real link whenever the goodbye is delayed more than
   the turns in front of it; on loopback it only reaches it by chance.
   CNC3D_NETTEST_LATE_BYE set to "<from>:<to>:<turn>:<ms>" does two things on the HOST.
   It holds seat <from>'s turn packets for seat <to> that many milliseconds, from the first
   one whose newest turn is <turn> or later, so that <to> runs behind the host. Every other
   seat gets them at once and the host takes its own copy at once. And it reads a goodbye
   from seat <from> only once it has run every turn that seat sent, so the host names the
   departure turn while it is already standing on it. A backstop reads it anyway after
   NM_TEST_LATE_BACKSTOP_MS. Nothing in play sets it. */
#define NM_TEST_DELAY_MAX 128
#define NM_TEST_LATE_BACKSTOP_MS 5000u
static int s_test_late_from = -1;
static int s_test_late_to = -1;
static unsigned s_test_late_turn = 0;
static unsigned s_test_late_ms = 0;
static unsigned char s_test_delay_pkt[NM_TEST_DELAY_MAX][LS_PACKET_MAX];
static int s_test_delay_len[NM_TEST_DELAY_MAX];
static unsigned s_test_delay_due[NM_TEST_DELAY_MAX];
static int s_test_delay_head = 0;
static int s_test_delay_n = 0;
static unsigned char s_test_late_bye[12];
static int s_test_late_bye_len = 0;
static NetAddr s_test_late_bye_from;
static unsigned s_test_late_bye_since = 0;
static int s_test_late_bye_state = 0;   /* 0 nothing held, 1 held, 2 read */

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

/* ---------------------------------------------------------------- setup on the wire -- */
/* Fixed layout, little endian, one byte per roster field. 16 + 8*4 + 5*8 = 88 bytes, plus
   the tech level at 32 since version 4, plus the seat modes at 88..95 since version 5.
   The roster still starts at 48. */
#define NM_SETUP_BYTES 192   /* 96 of rules and roster, then eight names of twelve */

static int setup_pack(const NmSetup* s, unsigned char* p)
{
    int i;
    memset(p, 0, NM_SETUP_BYTES);
    memcpy(p, s->scenario, 15);
    put_u32(p + 16, (unsigned)s->credits);
    p[20] = (unsigned char)s->tiberium;
    p[21] = (unsigned char)s->crates;
    p[22] = (unsigned char)s->superweapons;
    p[23] = (unsigned char)s->bases;
    /* v6, in the reserved hole between the counts and the roster. */
    p[36] = (unsigned char)s->aitake;
    p[37] = (unsigned char)s->shortgame;
    p[38] = (unsigned char)s->ahead;   /* v8 */
    put_u32(p + 24, (unsigned)s->unit_count);
    p[28] = (unsigned char)s->speed;
    p[29] = (unsigned char)s->seats;
    p[30] = (unsigned char)s->humans;
    put_u32(p + 32, (unsigned)s->build);   /* v4; the gap at 32..47 was already reserved */
    for (i = 0; i < NM_MAX_SEATS; i++) {
        p[48 + i] = s->house[i];
        p[56 + i] = s->colour[i];
        p[64 + i] = s->team[i];
        p[72 + i] = s->start[i];
        p[80 + i] = s->is_ai[i];
        p[88 + i] = s->mode[i];   /* v5 */
        memcpy(p + 96 + i * NM_PLAYER_NAME_MAX, s->name[i], NM_PLAYER_NAME_MAX);  /* v7 */
    }
    return NM_SETUP_BYTES;
}

static void setup_unpack(NmSetup* s, const unsigned char* p)
{
    int i;
    memset(s, 0, sizeof(*s));
    memcpy(s->scenario, p, 15);
    s->scenario[15] = '\0';
    s->credits = (int)get_u32(p + 16);
    s->tiberium = p[20];
    s->crates = p[21];
    s->superweapons = p[22];
    s->bases = p[23];
    s->aitake = p[36];
    s->shortgame = p[37];
    s->ahead = p[38];   /* v8; 0 from a peer that does not send it */
    s->unit_count = (int)get_u32(p + 24);
    s->speed = p[28];
    s->seats = p[29];
    s->humans = p[30];
    s->build = (int)get_u32(p + 32);   /* v4 */
    for (i = 0; i < NM_MAX_SEATS; i++) {
        s->house[i] = p[48 + i];
        s->colour[i] = p[56 + i];
        s->team[i] = p[64 + i];
        s->start[i] = p[72 + i];
        s->is_ai[i] = p[80 + i];
        s->mode[i] = p[88 + i];   /* v5 */
        /* SANITISED ON THE WAY IN, once: these are a remote peer's bytes and they are
           drawn on a screen, which is the rule the chat line already follows. */
        nm_name_copy(s->name[i], (const char*)p + 96 + i * NM_PLAYER_NAME_MAX);   /* v7 */
    }
}

/* ------------------------------------------------------------------- the handshake -- */

/* One table, so the two ends cannot describe the same refusal differently. A player who
   is told "order-wire layout" when the truth is "map bytes" goes and reinstalls the wrong
   thing, and the number beside it is no help unless the word is right. */
static const char* nm_refuse_text(unsigned reason)
{
    switch (reason) {
    case 1u:
        return "version";
    case 2u:
        return "order-wire layout";
    case 3u:
        return "map bytes";
    case 4u:
        return "passcode";
    case 5u:
        /* Reads after "the host refused: " on the joiner's screen, so it is a phrase
           rather than a noun. Deliberately says ONE OF US: the joiner cannot tell from
           here which end failed to read its brain, and guessing would send half of
           these players to look at the wrong machine. */
        return "one of us could not read its own engine";
    default:
        return "unknown";
    }
}

/* WHAT A PLAYER IS SAYING YES TO WHEN THEY PRESS READY: the map and the rules. Not the
   per-seat arrays -- another player choosing a different colour is not a change to the
   offer, and folding those in here would unready the whole room every time anybody moved
   a slider on their own row. Not `ahead` either: the host measures that off the round
   trips it is taking while the room sits open, so it moves on its own with nobody having
   decided anything. `seats` and `humans` are left out for the same reason, being derived
   from the map and from who has walked in; a map change already shows up in `scenario`. */
static int lobby_offer_changed(const NmSetup* a, const NmSetup* b)
{
    return strcmp(a->scenario, b->scenario) != 0
        || a->credits      != b->credits
        || a->tiberium     != b->tiberium
        || a->crates       != b->crates
        || a->aitake       != b->aitake
        || a->shortgame    != b->shortgame
        || a->superweapons != b->superweapons
        || a->bases        != b->bases
        || a->unit_count   != b->unit_count
        || a->build        != b->build
        || a->speed        != b->speed;
}

static NmMapHashFn s_maphash_fn = NULL;
static void*       s_maphash_user = NULL;

void nm_set_map_hasher(NmMapHashFn fn, void* user)
{
    s_maphash_fn = fn;
    s_maphash_user = user;
}

int nm_host_gone(void) { return s_host_gone; }
unsigned nm_abi(void)  { return s_abi; }
unsigned nm_scen(void) { return s_scen; }

/* The fingerprint of a named map on THIS machine, or 0 when there is no hasher installed
   or this machine does not have that map. 0 is the same "cannot answer" it has always
   been everywhere else on this wire, so a peer whose caller never installed a hasher
   behaves exactly as it did before the hook existed. */
static unsigned nm_local_map_hash(const char* scenario)
{
    if (!s_maphash_fn || !scenario || !*scenario) return 0u;
    return s_maphash_fn(scenario, s_maphash_user);
}

/* ---- ARMING THE NEXT DOOR FOR A RELAY -----------------------------------------------
 *
 * A relayed match differs from a LAN one in exactly one place: which kind of socket gets
 * opened. Everything above that -- the handshake, the roster, the lockstep, the star --
 * is unchanged, because net_recv rewrites an arriving datagram's `from` to the sender's
 * tunnel id and nm_seat_of matches on whatever NetAddr it is handed. That is why this is
 * an arming flag and not a parallel code path.
 *
 * ONE SHOT, AND EACH DOOR DISARMS IT ITSELF. The request is taken into a local as the
 * first statement of whichever door runs next, and cleared there. It is deliberately NOT
 * cleared inside nm_open: a door that fails its own checks and returns before opening
 * anything would then leave the arming standing, and the next LAN room this process
 * opened would silently go through a relay nobody asked for.
 */
typedef struct NmRelayReq {
    int          armed;
    char         host[128];
    unsigned short port;
    unsigned long my_id;
    unsigned long host_id;
} NmRelayReq;

static NmRelayReq s_relayReq;      /* the pending request */
static NmRelayReq s_relayNow;      /* what the door that is running took */

void nm_relay_next(const char* tunnel_host, unsigned short tunnel_port,
                   unsigned long my_id, unsigned long host_id)
{
    memset(&s_relayReq, 0, sizeof s_relayReq);
    if (!tunnel_host || !*tunnel_host || my_id == 0ul) return;
    s_relayReq.armed = 1;
    snprintf(s_relayReq.host, sizeof s_relayReq.host, "%s", tunnel_host);
    s_relayReq.port = tunnel_port;
    s_relayReq.my_id = my_id;
    s_relayReq.host_id = host_id;     /* 0 on a host: it IS the host */
}

/* Called first by every door. Takes the request and disarms it in one step. */
static void nm_relay_take(void)
{
    s_relayNow = s_relayReq;
    memset(&s_relayReq, 0, sizeof s_relayReq);
}

int nm_is_relayed(void) { return s_sock ? net_is_tunnel(s_sock) : 0; }

/* Rendered once, when the room opens, from the id this peer actually registered. Empty
   whenever the room is not relayed, which is what the screen tests. */
static char s_roomCode[RC_TEXT_MAX];
static unsigned long s_roomId = 0ul;
const char* nm_room_code(void) { return s_roomCode; }
unsigned long nm_room_id(void) { return s_roomId; }

static int nm_open(unsigned short port)
{
    /* CLOSE WHAT IS ALREADY OPEN. This used to overwrite s_sock, and the socket it
       dropped was never closed: the layer below hands out a fixed pool of six, so five
       abandoned joins exhausted it and the sixth said "could not open a socket to join
       with" on a machine whose networking was fine. Found when the LAN finally worked
       and a player joined repeatedly. */
    if (s_sock) {
        net_close(s_sock);
        s_sock = NULL;
        net_shutdown();
    }
    if (net_startup() != 0) {
        printf("NET|error=networking would not start\n");
        return 0;
    }
    s_roomCode[0] = '\0';
    s_roomId = 0ul;
    if (s_relayNow.armed) {
        /* THE PORT IS IGNORED ON A RELAYED SOCKET, and that is correct rather than
           sloppy: a relayed peer is never dialled directly, so a fixed local port buys
           nothing and costs a collision when two copies run on one machine. The relay
           knows us by our id, not by where we are. */
        s_sock = net_open_tunnel(s_relayNow.host, s_relayNow.port, s_relayNow.my_id);
        if (!s_sock) {
            printf("NET|error=could not reach the relay at %s:%u\n",
                   s_relayNow.host, (unsigned)s_relayNow.port);
            net_shutdown();
            return 0;
        }
        /* THE CODE IS THIS PEER'S OWN ID, and it is only a room code on a HOST -- a
           joiner's id is never written down, and rc_encode refuses it anyway because a
           joiner draws from the full 32 bits rather than the thirty a code can carry. */
        s_roomId = s_relayNow.my_id;
        if (!rc_encode(s_roomId, s_roomCode)) s_roomCode[0] = '\0';
        printf("NET|relay|%s:%u|id=%lu%s%s\n", s_relayNow.host, (unsigned)s_relayNow.port,
               s_relayNow.my_id, s_roomCode[0] ? "|code=" : "", s_roomCode);
        fflush(stdout);
        return 1;
    }
    s_sock = net_open(port);
    if (!s_sock) {
        printf("NET|error=could not open UDP port %u%s\n", (unsigned)port,
               port ? " (something else is using it?)" : "");
        net_shutdown();
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ the star ------ *
 *  Three ways bytes leave this peer, and keeping them apart is what stops the relay
 *  turning into a loop or a broadcast storm.
 *
 *  nm_send_seat   one named seat. The joiner's only outward direction is seat 0.
 *  nm_send_others everyone this peer knows about except itself. On a joiner that is just
 *                 the host; on the host it is every joiner.
 *  nm_relay       the HOST only, and only for bytes that arrived from a joiner: send them
 *                 on to every other joiner, unchanged, skipping the one they came from.
 *                 A joiner never relays, so nothing can bounce.
 * ---------------------------------------------------------------------------------- */
static void nm_send_seat(int seat, const void* buf, int len)
{
    if (seat >= 0 && seat < NM_MAX_SEATS && s_have[seat] && s_sock) {
        net_send(s_sock, &s_peers[seat], buf, len);
    }
}

static void nm_send_others(const void* buf, int len)
{
    int i;
    for (i = 0; i < NM_MAX_SEATS; i++) {
        if (i != s_seat) nm_send_seat(i, buf, len);
    }
}

static void nm_relay(int from_seat, const void* buf, int len)
{
    int i;
    if (!s_is_host) return;
    for (i = 0; i < NM_MAX_SEATS; i++) {
        if (i != s_seat && i != from_seat) nm_send_seat(i, buf, len);
    }
}

/* Which seat is at this address, or -1. Linear over eight is not worth a map. */
static int nm_seat_of(const NetAddr* a)
{
    int i;
    for (i = 0; i < NM_MAX_SEATS; i++) {
        if (s_have[i] && net_addr_equal(a, &s_peers[i])) return i;
    }
    return (-1);
}

static void nm_reset_match(int seat, int seats)
{
    int i;
    unsigned now = nm_now_ms();
    s_seat = seat;
    s_seats = seats;
    s_active = 1;
    s_fatal = 0;
    s_desynced = 0;
    s_host_gone = 0;
    s_desync_frame = 0;
    s_synced_frames = 0;
    s_turns_asked = 0;
    s_turns_answered = 0;
    s_last_answer_said_ms = 0;
    s_last_pkt_len = 0;
    s_last_send_ms = 0;
    s_gone_answer_said_ms = 0;
    memset(s_keep_len, 0, sizeof s_keep_len);
    memset(s_keep_next, 0, sizeof s_keep_next);
    s_test_held_n = 0;
    s_test_hold_seat = -1;
    s_test_hold_ms = 0;
    {
        const char* hold = getenv("CNC3D_NETTEST_HOLD_BYE");
        int hs;
        unsigned hms;
        if (hold && sscanf(hold, "%d:%u", &hs, &hms) == 2 && hs > 0 && hs < NM_MAX_SEATS) {
            s_test_hold_seat = hs;
            s_test_hold_ms = hms;
            printf("NET|test|hold-goodbye|seat=%d|ms=%u\n", hs, hms);
            fflush(stdout);
        }
    }
    s_test_cut_from = -1;
    s_test_cut_to = -1;
    s_test_cut_turn = 0;
    {
        const char* cut = getenv("CNC3D_NETTEST_CUT_RELAY");
        int cf, ct;
        unsigned cturn;
        if (cut && sscanf(cut, "%d:%d:%u", &cf, &ct, &cturn) == 3 && cf > 0 && ct > 0
            && cf < NM_MAX_SEATS && ct < NM_MAX_SEATS && cf != ct) {
            s_test_cut_from = cf;
            s_test_cut_to = ct;
            s_test_cut_turn = cturn;
            printf("NET|test|cut-relay|from=%d|to=%d|turn=%u\n", cf, ct, cturn);
            fflush(stdout);
        }
    }
    s_test_late_from = -1;
    s_test_late_to = -1;
    s_test_late_turn = 0;
    s_test_late_ms = 0;
    s_test_delay_head = 0;
    s_test_delay_n = 0;
    s_test_late_bye_len = 0;
    s_test_late_bye_since = 0;
    s_test_late_bye_state = 0;
    {
        const char* late = getenv("CNC3D_NETTEST_LATE_BYE");
        int lf, lt;
        unsigned lturn, lms;
        if (late && sscanf(late, "%d:%d:%u:%u", &lf, &lt, &lturn, &lms) == 4 && lf > 0
            && lt > 0 && lf < NM_MAX_SEATS && lt < NM_MAX_SEATS && lf != lt) {
            s_test_late_from = lf;
            s_test_late_to = lt;
            s_test_late_turn = lturn;
            s_test_late_ms = lms;
            printf("NET|test|late-goodbye|from=%d|to=%d|turn=%u|ms=%u\n", lf, lt, lturn, lms);
            fflush(stdout);
        }
    }
    for (i = 0; i < NM_MAX_SEATS; i++) {
        s_need_ans_ms[i] = 0;
        s_need_fwd_ms[i] = 0;
        s_need_gone_ms[i] = 0;
        s_absent_at[i] = LS_TURN_NONE;
        s_last_heard_ms[i] = now;
        s_left[i] = 0;
        s_left_turn[i] = LS_TURN_NONE;
        s_left_done[i] = 0;
        s_surr_turn[i] = LS_TURN_NONE;
        s_surr_done[i] = 0;
    }
    memset(s_my_hash_frame, 0xFF, sizeof(s_my_hash_frame));
    memset(s_peer_hash_frame, 0xFF, sizeof(s_peer_hash_frame));
    /* THE SCHEDULER IS TOLD THE WHOLE ROSTER and then which seats never speak. It used
       to be told the HUMAN COUNT, which worked only while people were a prefix of the
       roster: the moment a computer sits at seat 1 and a person at seat 2, ls_init
       refuses `me >= seats` for the person and the match cannot start. Seat numbers are
       the roster's on every machine now, and the barrier skips the seats the setup says
       are computers or blocked. A setup with no seat modes (an older CLI handshake, or a
       test that never filled them) falls back to the old prefix rule so nothing that
       passed before this changes shape. */
    {
        int roster = (s_setup.seats > 0 && s_setup.seats <= NM_MAX_SEATS) ? s_setup.seats : seats;
        int any_mode = 0;
        for (i = 0; i < NM_MAX_SEATS; i++) if (s_setup.mode[i]) any_mode = 1;
        if (roster < seats) roster = seats;
        if (roster <= seat) roster = seat + 1;
        /* SEEDED WITH WHAT THE ROOM AGREED. Zero means the setup came from a peer or a
           path that never said, and the built-in default is right for those: it is the
           LAN number, and the LAN is where they all are. */
        ls_init_ahead(&s_ls, roster, seat,
                      s_setup.ahead > 0 ? s_setup.ahead : LS_MAX_AHEAD);
        for (i = 0; i < roster; i++) {
            const int silent = any_mode
                ? (s_setup.mode[i] == NM_SEAT_BOT || s_setup.mode[i] == NM_SEAT_BLOCK)
                : (i >= seats);
            if (silent && i != seat) ls_set_absent(&s_ls, i);
        }
    }
}

/* ==================================================================== THE LOBBY ====== *
 *  The handshake as a state machine. See netmatch.h for why it is shaped this way.
 * ==================================================================== */

/* A PASSWORD IS HASHED, NEVER SENT. FNV-1a, the same one the renderer hashes the scenario
 * with, so there is one hash in this project rather than two.
 *
 * SAY WHAT THIS IS AND IS NOT. It is not security. A hash replayed is exactly as good as
 * the password, so anyone who can read the datagram can join. What it buys is that the
 * password does not sit in clear in a packet capture, in a log, or on the screen of
 * somebody watching the wire on a shared network, which is the realistic threat for a
 * game on a LAN. Anyone who needs more than that should not be relying on a lobby
 * password. Zero means the game is open, and a password that happens to hash to zero is
 * nudged to 1 so that "open" cannot be forged by choosing the right word. */
static unsigned nm_pass_hash(const char* pw)
{
    unsigned h = 2166136261u;
    int i;
    if (!pw || !*pw) return 0u;
    for (i = 0; pw[i] && i < NM_PASS_MAX; i++) {
        h ^= (unsigned char)pw[i];
        h *= 16777619u;
    }
    return h ? h : 1u;
}

int nm_lobby_locked(void) { return s_pass ? 1 : 0; }

static void nm_lobby_fail(int state, const char* msg)
{
    s_lobby_state = state;
    snprintf(s_lobby_err, sizeof s_lobby_err, "%s", msg ? msg : "");
    printf("NET|lobby|%s|%s\n",
           state == NM_LOBBY_REFUSED ? "refused" : "failed", s_lobby_err);
    fflush(stdout);
    /* AND IF WE WERE ALREADY IN A CHAIR, GET OUT OF IT. Some refusals are the host's --
       it never seated us and there is nothing to give back. Others are our OWN, decided
       after the WELCOME arrived, which is to say after the host had already written us
       into the room and told everybody: the map is one we have not got, or our copy of it
       is not theirs. Those used to leave a PHANTOM behind. The host went on holding the
       seat for somebody who had walked away without a word, START stayed blocked on a
       ready that was never coming, and the chair could not be reused.

       Found by leg 11's own "and that refusal left the room empty", which is there
       because a refusal that only half happens is worse than one that does not happen at
       all. Three copies because this is UDP and the goodbye has no acknowledgement; it is
       the same thing every other departure on this wire does. */
    s_start_pending = 0;   /* a room that has failed has nothing left to start */
    if (state == NM_LOBBY_REFUSED && !s_is_host && s_seat >= 1 && s_sock) {
        unsigned char bye[12];
        int k;
        put_u32(bye, NM_BYE);
        put_u32(bye + 4, (unsigned)s_seat);
        put_u32(bye + 8, LS_TURN_NONE);
        for (k = 0; k < 3; k++) net_send(s_sock, &s_peers[0], bye, 12);
    }
}

static void lobby_probe_reset(void);   /* the probe's host half, beside the host's poll */

int nm_lobby_host(unsigned short port, const NmSetup* setup, unsigned abi_hash,
                  unsigned scen_hash, int humans, const char* name, const char* password)
{
    int i;
    nm_relay_take();          /* FIRST, so a failure below cannot leave the next door armed */
    s_lobby_state = NM_LOBBY_IDLE;
    s_lobby_err[0] = '\0';
    if (!setup) return 0;
    if (humans < 1) humans = 1;
    if (humans > NM_MAX_SEATS) humans = NM_MAX_SEATS;
    /* A RELAYED ROOM MAY BE OPEN, and briefly it was not allowed to be. That rule was
       written for a world with no game list, where a room code was a weak secret and the
       only people who could knock were the ones told it. With a public browser the room
       is meant to be found by strangers -- that is the entire point of listing it -- so a
       compulsory passcode on a public game is a contradiction rather than a safeguard.
       The passcode is what makes a game PRIVATE, on a relay exactly as on a LAN. */
    if (!nm_open(port)) {
        nm_lobby_fail(NM_LOBBY_FAILED, s_relayNow.armed
                      ? "could not reach the relay"
                      : "could not open the port to host on");
        return 0;
    }
    s_setup = *setup;
    /* THE HOST'S OWN SEAT IS THE HOST'S OWN HANDLE; every other seat's arrives
       with its HELLO, and seat 0 never sends one. */
    nm_name_copy(s_setup.name[0], s_my_name);
    /* `humans` IS THE ROOM'S WIDTH, the number on the host tab: how many seats there are,
       not how many people are wanted. Which seats want a person is the mode table's
       answer, normalised below once the seat table is cleared. */
    if (s_setup.seats < humans) s_setup.seats = humans;
    s_abi = abi_hash;
    s_scen = scen_hash;
    s_is_host = 1;
    s_humans = humans;
    s_lobby_humans = humans;
    s_pass = nm_pass_hash(password);
    snprintf(s_lobby_name, sizeof s_lobby_name, "%s", (name && *name) ? name : "OpenCNC 3D GAME");
    memset(s_have, 0, sizeof s_have);
    memset(s_ack, 0, sizeof s_ack);
    memset(s_ready, 0, sizeof s_ready);
    memset(s_sack, 0, sizeof s_sack);
    s_ready_wire = 0;
    s_ready_sent_ms = 0;
    s_ready_again = 0;
    /* THE HOST IS SEAT 0 AND IS ALWAYS READY. It is the peer pressing START, so a ready
       state for it would be a control that gates itself. */
    s_seat = 0;
    s_ack[0] = 1;
    s_ready[0] = 1;
    s_sack[0] = 1;
    lobby_normalise();
    s_chat_total = 0;
    s_starting = 0;
    s_start_pending = 0;
    s_last_welcome_ms = 0;
    s_last_start_ms = 0;
    s_lobby_open_ms = nm_now_ms();
    /* NOTHING MEASURED YET. A round trip left over from the last room describes a link
       that is not in this one. */
    { int r; for (r = 0; r < NM_MAX_SEATS; r++) s_rtt[r] = -1; }
    s_last_ping_ms = 0;
    lobby_probe_reset();      /* a new room owes no stranger anything from the last one */
    s_lobby_state = NM_LOBBY_WAITING;
    printf("NET|hosting|port=%u|name=%s|scenario=%s|humans=%d|seats=%d|locked=%d\n",
           (unsigned)net_local_port(s_sock), s_lobby_name, s_setup.scenario, s_setup.humans,
           s_setup.seats, s_pass ? 1 : 0);
    {
        char addrs[8][64];
        int na = net_local_addrs(addrs, 8);
        for (i = 0; i < na; i++) {
            printf("NET|join-me-at|%s:%u\n", addrs[i], (unsigned)net_local_port(s_sock));
        }
        if (na <= 0) printf("NET|join-me-at|unknown\n");
    }
    fflush(stdout);
    return 1;
}

int nm_lobby_join(const char* addr, unsigned short port, unsigned abi_hash,
                  unsigned scen_hash, const char* password)
{
    nm_relay_take();          /* FIRST; see nm_relay_next */
    s_lobby_state = NM_LOBBY_IDLE;
    s_lobby_err[0] = '\0';
    /* A RELAYED JOIN NAMES A ROOM, NOT A MACHINE, so `addr` is unused on that path and
       must not be required. What identifies the far end is the host id the caller
       decoded from the room code. */
    if (!s_relayNow.armed && (!addr || !*addr)) {
        nm_lobby_fail(NM_LOBBY_FAILED, "no address to join");
        return 0;
    }
    if (s_relayNow.armed && s_relayNow.host_id == 0ul) {
        nm_lobby_fail(NM_LOBBY_FAILED, "no room code to join");
        return 0;
    }
    if (!nm_open(0)) {
        nm_lobby_fail(NM_LOBBY_FAILED, s_relayNow.armed
                      ? "could not reach the relay"
                      : "could not open a socket to join with");
        return 0;
    }
    memset(s_have, 0, sizeof s_have);
    memset(s_ack, 0, sizeof s_ack);
    memset(s_ready, 0, sizeof s_ready);
    memset(s_sack, 0, sizeof s_sack);
    /* AND NOTHING IS OUTSTANDING: a ready still waiting for its echo belonged to the
       room this peer has just left. */
    s_ready_wire = 0;
    s_ready_sent_ms = 0;
    s_ready_again = 0;
    memset(&s_setup, 0, sizeof s_setup);   /* a second join must not draw the last room */
    s_relayPinged = 0;
    s_relayHeard = 0;
    if (s_relayNow.armed) {
        /* THE HOST IS AN ID, AND THERE IS NOTHING TO RESOLVE. The relay's own address was
           already looked up inside net_open_tunnel; seat 0 is simply the host's tunnel id,
           which the player typed as a room code. Sending net_resolve at a room code would
           be asking DNS about a name that was never a name. */
        net_addr_tunnel(&s_peers[0], s_relayNow.host_id);
        printf("NET|joining|room=%lu|via=%s:%u\n", s_relayNow.host_id,
               s_relayNow.host, (unsigned)s_relayNow.port);
        fflush(stdout);
        /* ASK THE RELAY WHETHER IT IS THERE, once, at the same moment the first HELLO
           goes out. It answers on the same socket and costs nothing on either side. */
        {
            int k;
            for (k = 0; k < 4; k++) s_relayNonce[k] = (unsigned char)(0x5A ^ (k * 41 + 11));
            if (net_tunnel_ping_send(s_sock, s_relayNow.host, s_relayNow.port,
                                     s_relayNonce) > 0)
                s_relayPinged = 1;
        }
    } else if (net_resolve(addr, port, &s_peers[0]) != 0) {
        char msg[160];
        snprintf(msg, sizeof msg, "could not find '%s'", addr);
        net_close(s_sock);
        s_sock = NULL;
        net_shutdown();
        nm_lobby_fail(NM_LOBBY_FAILED, msg);
        return 0;
    }
    s_have[0] = 1;
    /* THE HOST IS ALWAYS READY, on the joiner's copy of the table as well as its own;
       a joiner never heard it, so its all_ready answer disagreed with the host's. */
    s_ready[0] = 1;
    s_is_host = 0;
    s_abi = abi_hash;
    s_scen = scen_hash;
    s_pass = nm_pass_hash(password);
    s_seat = -1;
    s_starting = 0;
    s_start_pending = 0;
    s_kicked = 0;
    s_last_hello_ms = 0;
    s_lobby_open_ms = nm_now_ms();
    /* NOTHING MEASURED YET. A round trip left over from the last room describes a link
       that is not in this one. */
    { int r; for (r = 0; r < NM_MAX_SEATS; r++) s_rtt[r] = -1; }
    s_last_ping_ms = 0;
    s_lobby_name[0] = '\0';
    s_chat_total = 0;
    s_lobby_state = NM_LOBBY_WAITING;
    {
        char txt[64];
        net_addr_text(&s_peers[0], txt, (int)sizeof txt);
        printf("NET|joining|peer=%s\n", txt);
        fflush(stdout);
    }
    return 1;
}

/* Send my ready tick to everybody. On a joiner that is the host, which relays it. */
static void nm_send_tick(void)
{
    unsigned char p[12];
    if (s_seat < 0) return;
    put_u32(p, NM_TICK);
    put_u32(p + 4, (unsigned)s_seat);
    put_u32(p + 8, s_ready[s_seat] ? 1u : 0u);
    nm_send_others(p, 12);
    s_ready_sent_ms = nm_now_ms();
}

void nm_lobby_set_ready(int on)
{
    if (s_is_host) return;              /* the host is always ready; see nm_lobby_host */
    if (s_seat < 0) return;
    s_ready[s_seat] = on ? 1 : 0;
    s_ready_again = 0;      /* a new press is a new thing to get acknowledged */
    nm_send_tick();
    printf("NET|ready|seat=%d|%d\n", s_seat, s_ready[s_seat]);
    fflush(stdout);
}

int nm_lobby_my_ready(void) { return (s_seat >= 0) ? s_ready[s_seat] : 0; }

/* WHAT A SEAT'S OWN PERSON MAY DECIDE, applied by the host and only by the host: it is
   the one writer of the room, so a joiner asks and the host answers with the room the
   next WELCOME carries. The two uniqueness rules are the screen's own and are enforced
   here as well, because a joiner cannot see what another joiner just took. */
static void lobby_apply_seat(int seat, int house, int team, int colour, int start)
{
    int i;
    if (seat < 0 || seat >= NM_MAX_SEATS) return;
    /* NOT ONCE THE START IS ON THE WIRE, for the reason nm_lobby_set_setup gives and with
       a sharper consequence. START is re-sent to every seat that has not acknowledged it,
       and each copy is packed from the room as it is at that moment. A joiner's seat
       request landing between two copies changed the room under the second one, so a seat
       that acknowledged the first START booted one set of colours and starts while a seat
       that acknowledged a later one booted another, and the match was different on
       different machines from its first frame. The request is not queued for later: there
       is no later, because the next thing that room does is play. */
    if (s_starting) return;
    if (house == 0 || house == 1) s_setup.house[seat] = (unsigned char)house;
    if (team >= 0 && team < NM_MAX_SEATS) s_setup.team[seat] = (unsigned char)team;
    if (colour >= 0 && colour < NM_MAX_SEATS) {
        /* TAKING A COLOUR MOVES WHOEVER HAD IT, which is what the screen has always
           promised and what the wire refused to do.

           The lobby's own picker leaves every square live on a seat's own row in a
           match, on the stated rule that "seat 0 is just another player and taking a
           colour off them is exactly the negotiation the popup is for", and
           sk_set_row_colour swaps the two seats. The host refused the same request
           outright. So a joiner asking for a taken colour saw its screen swap the two
           rows, saw the status line say who had moved, and then watched the whole thing
           snap back a moment later when the host's answer arrived. Two rules, one room.

           A swap, not "the lowest free colour": the seats hold a permutation of 0..7 --
           the lobby maintains that and checks it -- so the colour this seat is vacating
           IS the lowest free one, and swapping keeps the permutation with one write
           each. A displaced seat is displaced once; there is no chain. */
        int other = -1;
        for (i = 0; i < NM_MAX_SEATS; i++)
            if (i != seat && s_setup.mode[i] != NM_SEAT_BLOCK
                && s_setup.colour[i] == (unsigned char)colour) { other = i; break; }
        if (other >= 0) s_setup.colour[other] = s_setup.colour[seat];
        s_setup.colour[seat] = (unsigned char)colour;
    }
    if (start < 0) {
        /* SK_START_RANDOM on the wire is "unpicked": the engine deals it from the
           synchronised RNG, which is what the lobby already sends for an empty pick. */
        s_setup.start[seat] = (unsigned char)0x7f;   /* SK_START_RANDOM: the engine deals it */
    } else if (start < NM_MAX_SEATS) {
        int taken = 0;
        for (i = 0; i < NM_MAX_SEATS; i++)
            if (i != seat && s_setup.mode[i] != NM_SEAT_BLOCK
                && s_setup.start[i] == (unsigned char)start) taken = 1;
        if (!taken) s_setup.start[seat] = (unsigned char)start;
    }
    lobby_normalise();
    for (i = 1; i < NM_MAX_SEATS; i++) s_ack[i] = 0;   /* re-welcome: the room changed */
}

void nm_lobby_set_my_seat(int house, int team, int colour, int start)
{
    if (s_seat < 0) return;
    if (s_is_host) { lobby_apply_seat(s_seat, house, team, colour, start); return; }
    {
        unsigned char p[24];
        put_u32(p, NM_SEATPREF);
        put_u32(p + 4, (unsigned)s_seat);
        put_u32(p + 8, (unsigned)(house ? 1 : 0));
        put_u32(p + 12, (unsigned)(team < 0 ? 0 : team));
        put_u32(p + 16, (unsigned)(colour < 0 ? 0 : colour));
        put_u32(p + 20, (unsigned)(start < 0 ? 0xFFFFFFFFu : (unsigned)start));
        nm_send_seat(0, p, (int)sizeof p);
    }
}

/* WHAT A ROUND TRIP MAKES THE LOOKAHEAD.
 *
 * An order stamped N turns ahead has N tick-times to arrive. At the engine's 15 Hz that is
 * 66.7 ms a turn, so the cover a room needs is the round trip plus room for one late
 * packet, and the arithmetic is that and nothing cleverer: turns = rtt / 66.7, rounded up,
 * plus one. The +1 is not padding for its own sake -- with cover exactly equal to the
 * round trip, the average packet arrives exactly as its turn comes due and half of them
 * are late.
 *
 * MEASURED IN THE LOBBY, WHICH IS A QUIET ROOM, and a match is not: the same link carries
 * eight seats of orders once play starts. That is a second reason the number leans high.
 *
 * Clamped to the band lockstep will accept anyway, so the two cannot disagree about what
 * is legal. */
int nm_ahead_for_rtt(int rtt_ms)
{
    int turns;
    if (rtt_ms < 0) return LS_MAX_AHEAD;      /* nothing measured: the built-in default */
    turns = (rtt_ms * 3 + 199) / 200;         /* ceil(rtt / 66.7), in integers */
    turns += 1;
    if (turns < LS_AHEAD_MIN) turns = LS_AHEAD_MIN;
    if (turns > LS_AHEAD_MAX) turns = LS_AHEAD_MAX;
    return turns;
}

int nm_lobby_worst_rtt(void)
{
    int i, worst = -1;
    if (!s_is_host) return -1;
    for (i = 1; i < NM_MAX_SEATS; i++) {
        if (!s_have[i] || s_rtt[i] < 0) continue;
        if (s_rtt[i] > worst) worst = s_rtt[i];
    }
    return worst;
}

/* HOW MANY SEATED JOINERS HAVE NOT ANSWERED A PING YET. The worst round trip above is a
   maximum over the seats that HAVE answered, so in a room of three or more it is only a
   lower bound until this reaches zero: the one seat that has not replied may well be the
   slowest link in the room, and it is the slowest link the lookahead exists for. */
static int lobby_unmeasured(void)
{
    int i, n = 0;
    for (i = 1; i < NM_MAX_SEATS; i++)
        if (s_have[i] && s_rtt[i] < 0) n++;
    return n;
}

/* A PRESSED START THAT HAS NOT GONE OUT IS DROPPED THE MOMENT THE ROOM IT WAS PRESSED
   FOR CHANGES. The host agreed to start THAT room: a seat that walks out takes its
   measurement with it, a READY that goes out is a person who is no longer agreeing, and a
   changed map or rule is an offer nobody has said yes to. The host sees START GAME again
   and presses it again for the room that is actually there. (A seat that would walk IN is
   not seated during the wait at all; see the HELLO arm.) */
static void lobby_cancel_start(const char* why, int seat)
{
    if (!s_start_pending) return;
    s_start_pending = 0;
    if (seat >= 0) printf("NET|start-cancelled|seat=%d|%s\n", seat, why);
    else printf("NET|start-cancelled|%s\n", why);
    fflush(stdout);
}

static void lobby_close_open_seats(void);

/* THE ONE PLACE A START IS COMMITTED, and every door into it -- the lobby screen's START
 * GAME, the multiplayer screen's START, the headless host, the gates -- arrives through
 * nm_lobby_start and then here, so none of them can skip the measurement.
 *
 * WHY START WAITS AT ALL. The host pings only SEATED joiners, and a joiner readies the
 * instant it is seated, so a quick hand presses START before any reply can have landed.
 * The worst round trip then reads -1 and the room agrees the built-in LAN lookahead
 * whatever the link is: three turns, 200 ms of cover, chosen for a relay whose round trip
 * can be twice that without anybody having looked. The headless host hid this by holding
 * a ready room for a moment; the screens pressed straight through it, and a relayed room
 * started from the lobby screen's own button read -1 on every run that checked.
 *
 * SO A PRESS IS A PROMISE, KEPT BY THE POLL. nm_lobby_start marks the start pending and
 * asks for a ping at once; every poll after that comes here, and the START goes out when
 * EVERY seated joiner has a round trip, not merely one of them. Nothing blocks: the screen
 * keeps drawing, playing music and polling while it waits, which is about one round trip
 * plus however long the caller takes between polls.
 *
 * A SEAT THAT NEVER ANSWERS DOES NOT HOLD THE ROOM FOR EVER. After NM_START_MEASURE_MS
 * the start goes out anyway, and a seat still unmeasured then is given the largest cover
 * the band allows rather than the smallest: a dozen pings unanswered is a link that is
 * either slower than the wait or dropping most of what it is sent, and the LAN number is
 * the one choice certain to be wrong for it. It costs input lag, and buys the turns a
 * lossy link repairs (nm_ask_for_turn) the most time to land before they come due. It no
 * longer risks the match: a lookahead above the six-turn redundancy window used to turn
 * a two-way outage of a few hundred milliseconds into a deadlock, and the turn request is
 * what removed that. */
static void lobby_try_start(void)
{
    int unmeasured, worst;
    unsigned waited;
    if (!s_start_pending || s_starting) return;
    if (!nm_lobby_all_ready()) {
        lobby_cancel_start("somebody in the room is no longer ready", -1);
        return;
    }
    unmeasured = lobby_unmeasured();
    waited = nm_now_ms() - s_start_pressed_ms;
    if (unmeasured > 0 && waited < NM_START_MEASURE_MS) return;
    s_start_pending = 0;
    lobby_close_open_seats();
    lobby_normalise();
    /* THE ROOM'S LOOKAHEAD, DECIDED HERE AND NEVER AGAIN. It goes out with the START, so
       every peer seeds its scheduler with the same number; a joiner that worked it out
       for itself would get a different one, because each measures a different link.
       Chosen from the WORST seat, because a lockstep room runs at the speed of its
       slowest link and there is no such thing as one peer being late on its own. */
    worst = nm_lobby_worst_rtt();
    s_setup.ahead = (unmeasured > 0) ? LS_AHEAD_MAX : nm_ahead_for_rtt(worst);
    printf("NET|ahead|turns=%d|worst-rtt=%dms|cover=%dms|unmeasured=%d|waited=%ums\n",
           s_setup.ahead, worst, s_setup.ahead * 200 / 3, unmeasured, waited);
    s_start_began_ms = nm_now_ms();
    s_starting = 1;
    s_last_start_ms = 0;
    printf("NET|starting|humans=%d|seats=%d\n", s_setup.humans, s_setup.seats);
    fflush(stdout);
}

/* A START THAT WAS ON THE WIRE AND CANNOT FINISH: somebody left or was removed before
 * every seat acknowledged it, or nobody acknowledged it in time.
 *
 * IT CANNOT SIMPLY BE TAKEN BACK. A joiner that receives START goes straight into the
 * match on the roster it carried, and nothing in a running match listens for a second
 * START. This used to put the host back in the room as though nothing had gone out, and
 * two things followed. A seat that had already acknowledged went on counting as
 * acknowledged, so the host's next press started at once and sent that seat nothing, and
 * the two ran different rosters. And the seat itself sat in a match that never got a
 * turn, kept alive for ever by the room's own pings.
 *
 * WHETHER A SEAT ACKNOWLEDGED IS NOT THE TEST, because an acknowledgement can be lost
 * after the START was not: the host cannot tell a joiner that never heard START from one
 * whose three replies all went missing. So the test is whether a START went out while
 * anybody is still seated to have received it. If one did, the room is over, and it says
 * so to everyone: a goodbye from seat 0 ends the match for a seat already in one and
 * closes the room, by name, for a seat still in the lobby. If none did (the only joiner
 * left, say), the room goes back to waiting with nothing left over from the attempt, and
 * the next press is a new start. */
static void lobby_abort_start(const char* why)
{
    int i, reached = 0;
    if (!s_starting) return;
    s_starting = 0;
    s_start_began_ms = 0;
    printf("NET|start-aborted|%s\n", why);
    fflush(stdout);
    if (s_last_start_ms != 0) {
        for (i = 1; i < NM_MAX_SEATS; i++)
            if (s_have[i]) reached++;
    }
    s_last_start_ms = 0;
    for (i = 1; i < NM_MAX_SEATS; i++) s_sack[i] = 0;
    if (reached > 0) {
        unsigned char bye[8];
        char msg[192];
        int k;
        put_u32(bye, NM_BYE);
        put_u32(bye + 4, 0u);
        for (k = 0; k < 3; k++) nm_send_others(bye, 8);
        snprintf(msg, sizeof msg, "the game could not start: %s", why);
        nm_lobby_fail(NM_LOBBY_FAILED, msg);
    }
}

/* ---- THE PROBE, THE HOST'S HALF (the wire and the reasons are in netmatch.h) ----

   ANSWERED BEFORE ANYTHING ELSE IS ASKED OF THE SENDER. A browsing player has not
   joined, has no seat, and may not know the passcode; the probe is the question "are you
   there, and how far away", and every one of those checks would turn the answer into
   silence for exactly the people who need it.

   WHAT A STRANGER CAN MAKE A HOST DO WITH IT is therefore the thing to bound, and it is
   bounded three ways. The answer is NM_PROBE_BYTES whatever arrived, so a forged source
   address buys an attacker no more bytes at the victim than it sent here. One sender is
   answered at most NM_PROBE_PER_SOURCE times in each second the table counts for it, and
   everybody together at most NM_PROBE_ALL_PER_S in each of the host's own seconds. Those
   are counted seconds, not a sliding window, so across the edge of two a sender can
   collect twice its share inside one real second (measured: 26 answers in one log line of
   a two-sender flood); a flood from one address or from many still costs the host at
   most a couple of kilobytes a second of upload. And nothing is printed per probe: a flood would
   otherwise bury the one log this project debugs a room from.

   A SENDER IS AN ADDRESS, NOT A SOCKET. Counted by address and port, one machine with
   sixteen sockets was sixteen senders, which filled the table below and had every other
   asker refused: measured, sixteen sockets on one machine sending one probe a second
   each held a real prober at no answers for three rounds running. So a sender is its IP
   whatever the port, and through a relay its tunnel id, which is all a relayed probe
   says about where it came from. What that costs falls on players who share one public
   address: to a host they are one sender, and past eight probes inside a second from all
   of them together the rest go unanswered. A browsing player sends four a round, so two
   of them can ask one room at the same moment and lose nothing.

   A FULL TABLE MAKES ROOM RATHER THAN REFUSING. When all NM_PROBE_SRC_SLOTS senders are
   in the table, a new one takes the slot whose second began longest ago, so filling the
   table is no way to silence a room. What still can is the budget for everybody
   together: senders that forge their addresses, NM_PROBE_ALL_PER_S of them a second
   between them, use it up, and every other asker reads as no answer for as long as they
   keep it up. That is the price of a hard ceiling on what a flood can make a host
   upload, and nothing this side of the wire can tell a forged sender from a real one. */
#define NM_PROBE_SRC_SLOTS 16
#define NM_PROBE_ALL_PER_S 64
static struct {
    NetAddr  addr;
    unsigned since_ms;    /* when this sender's current second began; 0 = slot unused */
    int      count;       /* answers given in it */
} s_probe_src[NM_PROBE_SRC_SLOTS];
static unsigned s_probe_all_ms = 0;
static int s_probe_all_count = 0;
/* What the one line a second will say, and when the second it covers began. */
static unsigned s_probe_said_ms = 0;
static unsigned s_probe_answered = 0, s_probe_limited = 0, s_probe_short = 0;

static void lobby_probe_reset(void)
{
    memset(s_probe_src, 0, sizeof s_probe_src);
    s_probe_all_ms = 0;
    s_probe_all_count = 0;
    s_probe_said_ms = 0;
    s_probe_answered = s_probe_limited = s_probe_short = 0;
}

static int lobby_probe_allowed(const NetAddr* from, unsigned now)
{
    int i, slot = -1;
    const unsigned stamp = now ? now : 1u;    /* 0 means "unused" in the table */
    if (s_probe_all_ms == 0 || now - s_probe_all_ms >= 1000u) {
        s_probe_all_ms = stamp;
        s_probe_all_count = 0;
    }
    for (i = 0; i < NM_PROBE_SRC_SLOTS; i++) {
        if (s_probe_src[i].since_ms != 0 && net_addr_same_host(&s_probe_src[i].addr, from)) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        /* A free slot if there is one, else the one whose second began longest ago. */
        unsigned oldest = 0u;
        for (i = 0; i < NM_PROBE_SRC_SLOTS; i++) {
            const unsigned age = now - s_probe_src[i].since_ms;
            if (s_probe_src[i].since_ms == 0) { slot = i; break; }
            if (slot < 0 || age > oldest) { slot = i; oldest = age; }
        }
        s_probe_src[slot].addr = *from;
        s_probe_src[slot].since_ms = stamp;
        s_probe_src[slot].count = 0;
    } else if (now - s_probe_src[slot].since_ms >= 1000u) {
        s_probe_src[slot].since_ms = stamp;    /* this sender's second is up */
        s_probe_src[slot].count = 0;
    }
    if (s_probe_src[slot].count >= NM_PROBE_PER_SOURCE) return 0;
    if (s_probe_all_count >= NM_PROBE_ALL_PER_S) return 0;
    s_probe_src[slot].count++;
    s_probe_all_count++;
    return 1;
}

static void lobby_answer_probe(const NetAddr* from, const unsigned char* in, int n)
{
    unsigned char ack[NM_PROBE_BYTES];
    const unsigned now = nm_now_ms();
    if (s_probe_said_ms == 0) s_probe_said_ms = now ? now : 1u;
    if (n < NM_PROBE_BYTES) {
        s_probe_short++;
        return;
    }
    if (!lobby_probe_allowed(from, now)) {
        s_probe_limited++;
        return;
    }
    /* The asker's nonce and stamp, byte for byte, and this host's wire version where the
       asker sent a zero. Sixteen bytes whatever length arrived. */
    put_u32(ack, NM_PROBEACK_WORD);
    memcpy(ack + 4, in + 4, 8);
    put_u32(ack + 12, (unsigned)NM_VERSION);
    net_send(s_sock, from, ack, NM_PROBE_BYTES);
    s_probe_answered++;
}

/* ONE LINE, AT MOST ONCE A SECOND, and only when something arrived. The counts cover the
   second since the first probe the last line did not report, so a player's handful of
   probes reads as one line rather than four, and a flood reads as one line a second with
   the refusals counted in it. No sender's address is printed: the line says what the
   room did, and a browsing player's address is theirs. */
static void lobby_say_probes(void)
{
    if (s_probe_said_ms == 0 || nm_now_ms() - s_probe_said_ms < 1000u) return;
    printf("NET|probes|answered=%u|limited=%u|short=%u\n",
           s_probe_answered, s_probe_limited, s_probe_short);
    fflush(stdout);
    s_probe_said_ms = 0;
    s_probe_answered = s_probe_limited = s_probe_short = 0;
}

/* ---- the host's half of the poll ---- */
static void nm_lobby_poll_host(void)
{
    unsigned char in[LS_PACKET_MAX];
    unsigned char out[4 + 4 + 4 + 4 + NM_SETUP_BYTES];
    NetAddr from;
    int n, i;
    int seated = nm_lobby_filled();
    char txt[64];

    while ((n = net_recv(s_sock, &from, in, (int)sizeof in)) > 0) {
        unsigned magic;
        int seat;
        if (n < 4) continue;
        magic = get_u32(in);
        /* THE PROBE, FIRST, and before the sender is even looked up in the seat table:
           the answer must not depend on who is asking. */
        if (magic == NM_PROBE_WORD) {
            lobby_answer_probe(&from, in, n);
            continue;
        }
        seat = nm_seat_of(&from);

        if (magic == NM_HELLO && n >= 20 + NM_PLAYER_NAME_MAX) {
            unsigned ver = get_u32(in + 4);
            unsigned abi = get_u32(in + 8);
            unsigned scn = get_u32(in + 12);
            unsigned pw  = get_u32(in + 16);
            int scen_bad = (scn != 0 && s_scen != 0 && scn != s_scen);
            int pass_bad = (s_pass != 0 && pw != s_pass);
            /* A ZERO IS NOT AN AGREEMENT. 0 means "I could not read my own brain", and
               it was allowed to sail through this test in the one case
               where it did the most damage: BOTH sides 0 compared equal, so two builds
               that had each failed the check passed it together, and the whole point of
               the test -- that two different engines never share a match -- was switched
               off exactly where nothing could notice. (The other case, 0 against a real
               value, refused correctly but for the wrong reason and with a message that
               sent the player looking for a version mismatch that was not there.)
               Refused on its own reason now, ahead of the mismatch arm so that a peer
               which cannot read its engine is never described as having a different one. */
            int abi_unknown = (abi == 0u || s_abi == 0u);
            if (ver != NM_VERSION || abi_unknown || abi != s_abi || scen_bad || pass_bad) {
                unsigned reason = (ver != NM_VERSION) ? 1u
                                : (abi_unknown ? 5u
                                : (abi != s_abi ? 2u : (scen_bad ? 3u : 4u)));
                unsigned char no[12];
                put_u32(no, NM_REFUSE);
                put_u32(no + 4, reason);
                put_u32(no + 8, reason == 3u ? s_scen : s_abi);
                net_send(s_sock, &from, no, 12);
                net_addr_text(&from, txt, (int)sizeof txt);
                /* Both values on the line, as the blocking path's line has always had
                   them: a refusal with no numbers cannot be read off a log. Never the
                   passcode hashes. */
                printf("NET|refused|peer=%s|reason=%s|theirs=%08X|mine=%08X\n", txt,
                       nm_refuse_text(reason),
                       reason == 1u ? ver : reason == 3u ? scn : reason == 4u ? 0u : abi,
                       reason == 1u ? (unsigned)NM_VERSION : reason == 3u ? s_scen
                                    : reason == 4u ? 0u : s_abi);
                fflush(stdout);
                continue;
            }
            if (seat < 0) {
                /* THE LOWEST OPEN SEAT, so seats fill in order and a player who leaves and
                   comes back takes the hole rather than pushing the roster along. A seat
                   the host gave to a computer or blocked is not a hole: a joiner walks
                   past it to the next one, and a room with no open seat left ignores the
                   knock, which is what full means now. */
                /* AND NOBODY IS SEATED WHILE A PRESSED START IS BEING MEASURED. Pressing
                   START is what closes a room: before the press was held for its round
                   trips, the seats nobody had taken were closed on the press itself, and a
                   knock after it found no chair. The seats now close when the start is
                   committed, so without this a stranger knocking in the wait took a chair
                   and the arrival cancelled the host's press, which on a listed room can
                   happen again every time somebody new finds it. The knock is not
                   answered rather than refused: a joiner keeps knocking for its ten
                   seconds, so if this start is cancelled for some other reason it is
                   seated then, and if the start goes ahead the seats close under it,
                   exactly as they always did. */
                i = s_start_pending ? -1 : lobby_open_seat();
                if (i < 0 && s_start_pending) {
                    static unsigned lastHeldMs = 0;
                    if (lastHeldMs == 0 || nm_now_ms() - lastHeldMs > 1000u) {
                        lastHeldMs = nm_now_ms();
                        net_addr_text(&from, txt, (int)sizeof txt);
                        printf("NET|knock-held|peer=%s|the host pressed START\n", txt);
                        fflush(stdout);
                    }
                }
                if (i > 0) {
                    s_peers[i] = from;
                    s_have[i] = 1;
                    /* THE NAME AT THE MOMENT THE SEAT IS TAKEN; lobby_normalise
                       publishes it to everyone. This is where it is learned. */
                    nm_name_copy(s_setup.name[i], (const char*)in + 20);
                    if (!s_setup.name[i][0]) nm_name_copy(s_setup.name[i], "PLAYER");
                    s_ack[i] = 0;
                    s_ready[i] = 0;
                    s_sack[i] = 0;
                    /* A NEW PERSON IN AN OLD CHAIR IS AN UNMEASURED LINK: the round trip
                       this seat held belonged to whoever sat here before. */
                    s_rtt[i] = -1;
                    seated++;
                    lobby_normalise();
                    net_addr_text(&from, txt, (int)sizeof txt);
                    printf("NET|joiner|seat=%d|peer=%s|seated=%d/%d\n", i, txt, seated,
                           nm_lobby_wanted());
                    fflush(stdout);
                }
            }
        } else if (magic == NM_PONG && n >= 8 && seat > 0) {
            /* THE STAMP THIS HOST PUT IN, COME BACK. Subtracting gives the round trip
               with no clock agreed between the multiple machines, because both readings are
               this machine's own. A pong from a ping older than the room is discarded
               rather than believed: GetTickCount wraps, and a wrapped subtraction reads
               as a link forty days slow. */
            const unsigned sent = get_u32(in + 4);
            const unsigned now = nm_now_ms();
            if (now >= sent && now - sent < 60000u) {
                const int rtt = (int)(now - sent);
                if (rtt > s_rtt[seat]) s_rtt[seat] = rtt;   /* the worst, not the last */
            }
        } else if (magic == NM_READY && n >= 8 && seat > 0) {
            s_ack[seat] = 1;
        } else if (magic == NM_TICK && n >= 12 && seat > 0) {
            /* THE SEAT IS THE SENDER'S, NOT THE NUMBER IN THE PACKET, which is the rule
               every arm around this one already followed and this one did not. Without
               the two tests it grew here, a datagram from an address holding no seat at
               all could set any seat's ready flag and be relayed to the room as though a
               player had sent it -- and a seated joiner could ready somebody else up.
               The unseated case also reached nm_relay with a seat of -1, which skips
               nobody, so the room was told by a stranger. */
            unsigned who = get_u32(in + 4);
            if (who < (unsigned)NM_MAX_SEATS && (int)who == seat) {
                s_ready[who] = get_u32(in + 8) ? 1 : 0;
                nm_relay(seat, in, n);
                /* AND BACK TO THE SEAT IT CAME FROM, which is what makes the tick
                   answerable. Unchanged bytes, so what that joiner reads is the flag
                   this host has just written for it: agreement, or a reason to say it
                   again. Nothing else needs it -- the relay above is what the rest of
                   the room reads -- so this is one extra twelve byte datagram per
                   press. */
                nm_send_seat(seat, in, n);
            }
        } else if (magic == NM_SACK && n >= 8 && seat > 0) {
            s_sack[seat] = 1;
        } else if (magic == NM_SEATPREF && n >= 24 && seat > 0) {
            /* THE PERSON IN THAT SEAT ASKING FOR THEIR OWN ROW. The seat is the
               PACKET'S SENDER, never the number in the packet, so nobody can move
               somebody else. */
            const unsigned st_raw = get_u32(in + 20);
            lobby_apply_seat(seat, (int)get_u32(in + 8), (int)get_u32(in + 12),
                             (int)get_u32(in + 16),
                             (st_raw == 0xFFFFFFFFu) ? -1 : (int)st_raw);
        } else if (magic == NM_CHAT && n >= 12 && seat > 0) {
            /* THE SEAT COMES FROM THE PACKET AND MUST MATCH THE ADDRESS IT CAME FROM:
               the host knows both, so a joiner cannot speak as somebody else. */
            const unsigned who = get_u32(in + 4);
            const unsigned len = get_u32(in + 8);
            if ((int)who == seat && len < (unsigned)NM_CHAT_MAX && (int)len <= n - 12) {
                nm_chat_push(seat, (const char*)in + 12, (int)len);
                nm_relay(seat, in, n);
            }
        } else if (magic == NM_BYE && seat > 0) {
            printf("NET|lobby-left|seat=%d\n", seat);
            fflush(stdout);
            s_have[seat] = 0;
            s_ack[seat] = 0;
            s_ready[seat] = 0;
            s_sack[seat] = 0;
            s_rtt[seat] = -1;
            seated--;
            lobby_cancel_start("a player left", seat);
            lobby_normalise();
            /* A GOODBYE DURING THE START ABORTS THE START, and this is not tidiness.
               A joiner acknowledges the start and may say goodbye a fraction later, and
               both arrive in the SAME drain: the ack set the flag and the goodbye then
               unseated the player and cleared it, so the host went on waiting for an
               acknowledgement from somebody who had already gone, for ever, with no
               timeout. What becomes of the room then depends on whether anybody still
               seated can have the START already; see lobby_abort_start. */
            if (s_starting) {
                char why[96];
                snprintf(why, sizeof why, "seat %d left while the match was starting", seat);
                lobby_abort_start(why);
                if (s_lobby_state == NM_LOBBY_FAILED) return;
            }
        } else if (seat < 0) {
            /* A DATAGRAM FROM A STRANGER THAT MATCHED NOTHING: an older build's HELLO, a
               short one, a scan. One line a second at most, so "nothing arrived" and
               "something arrived that I did not understand" stop looking the same in
               the log; before this, both were silence. */
            static unsigned lastIgnoredMs = 0;
            if (lastIgnoredMs == 0 || nm_now_ms() - lastIgnoredMs > 1000u) {
                lastIgnoredMs = nm_now_ms();
                net_addr_text(&from, txt, (int)sizeof txt);
                printf("NET|ignored|peer=%s|magic=%08X|n=%d\n", txt, magic, n);
                fflush(stdout);
            }
        }
    }

    lobby_say_probes();

    /* PING every seated joiner, once a second. Cheap, and it is the only thing that tells
       the room how far ahead to stamp: a lobby that never measures has to guess, and the
       guess that was there is a LAN number. The host's own clock goes out and comes back
       untouched, so this needs no agreed time between host and joiner.
       FOUR TIMES A SECOND WHILE A PRESSED START IS WAITING ON THE ANSWER, because then
       the ping is the only thing between the button and the match. Same message, same
       bytes: only how often it goes out changes. */
    if (s_last_ping_ms == 0
        || nm_now_ms() - s_last_ping_ms > (s_start_pending ? NM_MEASURE_PING_MS : 1000u)) {
        unsigned char png[8];
        put_u32(png, NM_PING);
        put_u32(png + 4, nm_now_ms());
        for (i = 1; i < NM_MAX_SEATS; i++) {
            if (!s_have[i]) continue;
            nm_send_seat(i, png, (int)sizeof png);
        }
        s_last_ping_ms = nm_now_ms();
    }

    /* WELCOME every seated joiner that has not acknowledged one. Per seat, because it
       carries that joiner's OWN seat number. */
    if (s_last_welcome_ms == 0 || nm_now_ms() - s_last_welcome_ms > 300) {
        for (i = 1; i < NM_MAX_SEATS; i++) {
            if (!s_have[i] || s_ack[i]) continue;
            put_u32(out, NM_WELCOME);
            put_u32(out + 4, s_abi);
            put_u32(out + 8, s_scen);
            put_u32(out + 12, (unsigned)i);
            setup_pack(&s_setup, out + 16);
            nm_send_seat(i, out, (int)sizeof out);
        }
        s_last_welcome_ms = nm_now_ms();
    }

    /* A PRESSED START, KEPT: after this drain's pongs, so an answer that has just landed
       is counted, and before the START block, so a start committed here goes out on this
       same poll. */
    lobby_try_start();

    if (s_starting) {
        /* START, repeated to anyone who has not acknowledged it, until they all have. */
        int all = 1;
        /* AND NOT FOR EVER. A peer that goes silent between readying and acknowledging
           would otherwise hold the room open with no way out but the host quitting. */
        if (s_start_began_ms != 0 && nm_now_ms() - s_start_began_ms > 15000u) {
            lobby_abort_start("not every player answered the start within 15 seconds");
            if (s_lobby_state != NM_LOBBY_FAILED)
                s_lobby_state = (nm_lobby_filled() >= nm_lobby_wanted()) ? NM_LOBBY_FULL
                                                                      : NM_LOBBY_WAITING;
            return;
        }
        for (i = 1; i < NM_MAX_SEATS; i++) {
            if (s_have[i] && !s_sack[i]) all = 0;   /* only PEOPLE acknowledge */
        }
        if (all) {
            nm_reset_match(0, s_setup.seats);
            s_lobby_state = NM_LOBBY_STARTED;
            printf("NET|match|seat=0|humans=%d|seats=%d|scenario=%s|speed=%d|abi=%08X|scen=%08X\n",
                   s_setup.humans, s_setup.seats, s_setup.scenario, s_setup.speed, s_abi, s_scen);
            fflush(stdout);
            return;
        }
        if (s_last_start_ms == 0 || nm_now_ms() - s_last_start_ms > 200) {
            put_u32(out, NM_START);
            put_u32(out + 4, s_abi);
            put_u32(out + 8, s_scen);
            put_u32(out + 12, (unsigned)s_setup.humans);
            setup_pack(&s_setup, out + 16);
            for (i = 1; i < NM_MAX_SEATS; i++) {
                if (s_have[i] && !s_sack[i]) nm_send_seat(i, out, (int)sizeof out);
            }
            s_last_start_ms = nm_now_ms();
        }
        return;
    }

    s_lobby_state = (nm_lobby_filled() >= nm_lobby_wanted()) ? NM_LOBBY_FULL : NM_LOBBY_WAITING;
}

/* THE SEATS NOBODY TOOK ARE NOT PLAYERS, and this is not tidiness. START no longer waits
   for every seat to fill, so the seats still OPEN when the host presses it have to be
   closed before the roster leaves this machine: the engine makes a PLAYER of every seat
   that is not BLOCK and gives it a house with nobody driving it, and the lockstep barrier
   skips only BOT and BLOCK, so an OPEN seat left in the roster is a seat the turn waits
   on for ever. 1995 says the same by construction: MPlayerCount is Players.Count() + 1. */
static void lobby_close_open_seats(void)
{
    int i;
    for (i = 1; i < NM_MAX_SEATS; i++)
        if (!s_have[i] && s_setup.mode[i] == NM_SEAT_OPEN)
            s_setup.mode[i] = NM_SEAT_BLOCK;
}

/* ---- the joiner's half ---- */
static void nm_lobby_poll_join(void)
{
    unsigned char in[LS_PACKET_MAX];
    NetAddr from;
    int n, k;

    /* NO ANSWER IS A SENTENCE, NOT A WAIT. The HELLO is unsolicited inbound UDP at the
       host (the beacon it saw left from another socket), which is exactly the packet a
       firewall on the host's machine drops, and a joiner cannot tell a dropped packet
       from a closed port (net_recv swallows both). Ten seconds of HELLOs with nothing
       back is failed, by name, with the address, so the browser can say it. Every gate
       seats its joiner within milliseconds on loopback. The first real-network test
       sat in this state for minutes with a zero room on the screen. */
    /* REFUSED BY THIS MACHINE is not the same as unanswered, and waiting ten seconds to
       say so leaves the player looking at a room that was never there. The moment a send
       is refused outright, say which machine refused it and where the switch is. */
    /* THESE TWO SENTENCES ARE ABOUT A LAN, and over a relay both of them are false. The
       macOS Local Network permission has no say over a datagram to a public address, and
       a host's firewall is not in the path at all when both ends dial out to a relay. A
       player told to open a setting that cannot be their problem is worse off than one
       told nothing, because they will go and do it. */
    if (s_seat < 0 && net_send_blocked()) {
        nm_lobby_fail(NM_LOBBY_FAILED, nm_is_relayed()
                      ? "this computer refused to send to the relay: check that nothing "
                        "is blocking outbound UDP"
                      : "this computer is not allowed to use the local network: "
                        "System Settings > Privacy & Security > Local Network");
        return;
    }
    if (s_seat < 0 && s_lobby_open_ms != 0 && nm_now_ms() - s_lobby_open_ms > NM_JOIN_TIMEOUT_MS) {
        char txt[64], msg[192];
        if (nm_is_relayed()) {
            /* THE RELAY ANSWERED AND THE ROOM DID NOT, or nothing answered at all: two
               different problems that look like the same ten seconds of silence, and the
               player can only act on one of them. The ping is asked BEFORE the room is
               given up on, so this sentence knows which it was. */
            if (s_relayPinged && s_sock && net_tunnel_ping_seen(s_sock)) s_relayHeard = 1;
            snprintf(msg, sizeof msg, s_relayHeard
                     ? "the relay is working, but nothing answers that room code. Check "
                       "the code, and that the host is still hosting."
                     : "no answer from the relay in %u s. This computer may not be able "
                       "to reach it.",
                     NM_JOIN_TIMEOUT_MS / 1000u);
        } else {
            net_addr_text(&s_peers[0], txt, (int)sizeof txt);
            snprintf(msg, sizeof msg,
                     "no answer from %s in %u s: host firewall, or Local Network permission?",
                     txt, NM_JOIN_TIMEOUT_MS / 1000u);
        }
        nm_lobby_fail(NM_LOBBY_FAILED, msg);
        return;
    }
    if (s_seat < 0 && (s_last_hello_ms == 0 || nm_now_ms() - s_last_hello_ms > 250)) {
        /* THE HANDLE RIDES THE JOIN REQUEST, which is where 1995 puts it too. */
        unsigned char hello[20 + NM_PLAYER_NAME_MAX];
        memset(hello, 0, sizeof hello);
        put_u32(hello, NM_HELLO);
        put_u32(hello + 4, NM_VERSION);
        put_u32(hello + 8, s_abi);
        put_u32(hello + 12, s_scen);
        put_u32(hello + 16, s_pass);
        memcpy(hello + 20, s_my_name, NM_PLAYER_NAME_MAX);
        nm_send_seat(0, hello, (int)sizeof hello);
        s_last_hello_ms = nm_now_ms();
    }

    /* A READY THE HOST HAS NOT ECHOED IS SAID AGAIN, until it has. The button is STATE,
       not an event: what matters is that the host ends up holding what the person chose,
       and one datagram is not a promise of that. This is the same shape as every other
       thing in this lobby -- say it until the far end proves it heard -- and it is the
       shape the tick was missing. It costs nothing in the normal case: the echo of a tick
       that arrived comes back in a round trip, long before the first repeat is due, so a
       room on a wire that loses nothing sends exactly what it always sent.
       IT IS NOT A SECOND PRESS. The flag repeated is the one the person last chose, so a
       repeat can only ever make the host agree with the screen. A host that cleared this
       seat's ready because the room changed is not argued with either: the changed
       WELCOME clears this peer's own flag as it arrives, and what gets repeated after
       that is the clear.
       AND A HOST THAT NEVER ECHOES is not a failure, only a room that keeps hearing the
       tick: twelve bytes four times a second while it sits open, applied the same way
       every time. Measured against a host without the echo, a room held open for five
       seconds carried 22 of them and started; with the echo it carried one. */
    if (s_seat >= 1 && s_ready[s_seat] != s_ready_wire
        && (s_ready_sent_ms == 0 || nm_now_ms() - s_ready_sent_ms > NM_READY_RESEND_MS)) {
        nm_send_tick();
        if (++s_ready_again == 1) {
            printf("NET|ready-again|seat=%d|%d|no echo from the host yet\n",
                   s_seat, s_ready[s_seat]);
            fflush(stdout);
        }
    }

    while ((n = net_recv(s_sock, &from, in, (int)sizeof in)) > 0) {
        unsigned magic;
        if (n < 4) continue;
        magic = get_u32(in);
        if (!net_addr_equal(&from, &s_peers[0])) {
            /* THE HOST MAY ANSWER FROM ANOTHER OF ITS ADDRESSES: the beacon left one
               socket and the WELCOME leaves another, and on a machine with two ways onto
               the LAN (cable and WiFi, a VPN, a virtual adapter) the two source addresses
               are two routing decisions. Before seating, a WELCOME or a REFUSE of the
               right magic and size is the host by definition (the port is fixed and the
               payload is checked below), so the peer address follows it, with a line
               saying so. After seating the strict match stands; before this, the drop
               was silent. */
            const int isAnswer = (magic == NM_WELCOME && n >= 16 + NM_SETUP_BYTES)
                              || (magic == NM_REFUSE && n >= 12);
            char a[64], b[64];
            if (s_seat >= 0 || !isAnswer) continue;
            net_addr_text(&s_peers[0], a, (int)sizeof a);
            net_addr_text(&from, b, (int)sizeof b);
            printf("NET|host-answered-from|%s|expected=%s\n", b, a);
            fflush(stdout);
            s_peers[0] = from;
        }
        if (magic == NM_WELCOME && n >= 16 + NM_SETUP_BYTES) {
            unsigned host_scen = get_u32(in + 8);
            NmSetup offered;
            int seat = (int)get_u32(in + 12);
            if (seat < 1 || seat >= NM_MAX_SEATS) {
                nm_lobby_fail(NM_LOBBY_REFUSED, "the host gave me no seat");
                return;
            }
            /* THE SEAT IS TAKEN BEFORE THE MAP IS JUDGED, and the order is load-bearing.
               By the time this WELCOME arrived the host had already written this peer
               into the room and told everybody else; refusing below is this machine
               walking out of a chair it is sitting in, and nm_lobby_fail can only say
               goodbye for a chair it knows the number of. With the map checks above this
               line the refusal was silent, the host went on holding the seat for somebody
               who had gone, and START stayed blocked on a ready that was never coming.
               Leg 11's "and that refusal left the room empty" is the check that found it. */
            if (s_seat != seat) {
                s_seat = seat;
                printf("NET|seated|seat=%d\n", seat);
                fflush(stdout);
            }
            /* THE MAP QUESTION IS ASKED HERE, and it could not have been asked any
               earlier. The WELCOME is the first thing that NAMES the host's map; before
               it a joiner knows an address and nothing else. The old test sat above the
               unpack and compared the host's fingerprint against the hash of whichever
               map this joiner's own screen happened to be showing -- not a map either
               machine was going to play. Through the GUI it did not even do that: that
               path passes 0, and 0 turns the test off, so the one door players actually
               use had no map check on it at all.

               Two questions, in the order a person would ask them: have I got this map,
               and is my copy the same as theirs. */
            memset(&offered, 0, sizeof offered);
            setup_unpack(&offered, in + 16);
            if (s_maphash_fn && offered.scenario[0]) {
                const unsigned mine = nm_local_map_hash(offered.scenario);
                if (mine == 0u) {
                    char why[160];
                    snprintf(why, sizeof why,
                             "you do not have that game's map (%s)", offered.scenario);
                    nm_lobby_fail(NM_LOBBY_REFUSED, why);
                    return;
                }
                /* Adopt it before the comparison: from here on this peer's fingerprint is
                   the one for the map it is actually going to play, which is what the
                   handshake and the beacon should both have been carrying all along. */
                s_scen = mine;
            }
            if (host_scen != 0 && s_scen != 0 && host_scen != s_scen) {
                nm_lobby_fail(NM_LOBBY_REFUSED,
                              "that game is on a different copy of the map");
                return;
            }
            /* A CHANGED OFFER UNLIGHTS THIS PEER'S OWN READY. The host clears the room's
               lights when it changes the map or the rules; this is where the person in
               the seat finds out. Without it the joiner goes on believing it is ready,
               keeps saying so in every NM_TICK, and puts the light straight back on the
               host's screen -- so the two would disagree about a match that had changed
               under both of them. */
            if (s_ready[s_seat] && lobby_offer_changed(&s_setup, &offered)) {
                s_ready[s_seat] = 0;
                printf("NET|offer-changed|my-ready-cleared|seat=%d\n", s_seat);
                fflush(stdout);
            }
            s_setup = offered;
            for (k = 0; k < 3; k++) {
                unsigned char rd[8];
                put_u32(rd, NM_READY);
                put_u32(rd + 4, (unsigned)seat);
                nm_send_seat(0, rd, 8);
            }
        } else if (magic == NM_PING && n >= 8) {
            /* ECHOED UNTOUCHED, which is the whole of this end: the joiner never reads
               the stamp, never compares it against its own clock and never measures
               anything. The number belongs to the host, because in a star every link is
               a link to the host, and the host is the one that has to choose. */
            unsigned char png[8];
            put_u32(png, NM_PONG);
            memcpy(png + 4, in + 4, 4);
            net_send(s_sock, &s_peers[0], png, (int)sizeof png);
        } else if (magic == NM_TICK && n >= 12) {
            unsigned who = get_u32(in + 4);
            if (who >= (unsigned)NM_MAX_SEATS) {
                /* not a seat */
            } else if ((int)who == s_seat) {
                /* MY OWN TICK, COME BACK: the host holds this, whatever this peer
                   believes. If it is what the person chose the repeat above stops; if
                   it is stale -- an older press whose echo overtook a newer one -- the
                   repeat carries on until the two agree. Never written into s_ready:
                   the person in the seat owns the button, and adopting the echo would
                   let a late one flip the screen back under their hand. */
                s_ready_wire = get_u32(in + 8) ? 1 : 0;
                if (s_ready_again > 0 && s_ready_wire == s_ready[s_seat]) {
                    printf("NET|ready-agreed|seat=%d|%d|after %d repeat(s)\n",
                           s_seat, s_ready_wire, s_ready_again);
                    fflush(stdout);
                    s_ready_again = 0;
                }
            } else {
                s_ready[who] = get_u32(in + 8) ? 1 : 0;
            }
        } else if (magic == NM_START && n >= 16 + NM_SETUP_BYTES && s_seat >= 1) {
            unsigned char ak[8];
            int humans = (int)get_u32(in + 12);
            setup_unpack(&s_setup, in + 16);
            if (humans < 1 || humans > NM_MAX_SEATS) humans = s_setup.humans;
            if (humans < 1 || humans > NM_MAX_SEATS) humans = 2;
            s_humans = humans;
            s_lobby_humans = humans;
            put_u32(ak, NM_SACK);
            put_u32(ak + 4, (unsigned)s_seat);
            for (k = 0; k < 3; k++) nm_send_seat(0, ak, 8);
            nm_reset_match(s_seat, humans);
            s_lobby_state = NM_LOBBY_STARTED;
            printf("NET|match|seat=%d|humans=%d|seats=%d|scenario=%s|speed=%d|abi=%08X|scen=%08X\n",
                   s_seat, humans, s_setup.seats, s_setup.scenario, s_setup.speed, s_abi, s_scen);
            fflush(stdout);
            return;
        } else if (magic == NM_CHAT && n >= 12) {
            /* From the host's address always (a star), so the seat is the packet's. */
            const unsigned who = get_u32(in + 4);
            const unsigned len = get_u32(in + 8);
            if (who < (unsigned)NM_MAX_SEATS && (int)who != s_seat &&
                len < (unsigned)NM_CHAT_MAX && (int)len <= n - 12) {
                nm_chat_push((int)who, (const char*)in + 12, (int)len);
            }
        } else if (magic == NM_KICK && n >= 8) {
            const int who = (int)get_u32(in + 4);
            if (who == s_seat) {
                s_kicked = 1;
                nm_lobby_fail(NM_LOBBY_REFUSED, "the host removed you from the game");
                return;
            }
            /* Somebody else went. Their seat opens on this roster too, which is the half
               a peer cannot work out for itself in a star. */
            if (who > 0 && who < NM_MAX_SEATS) {
                s_ready[who] = 0;
                printf("NET|lobby-left|seat=%d|removed by the host\n", who);
                fflush(stdout);
            }
        } else if (magic == NM_BYE && n >= 8) {
            /* THE ROOM CLOSED. The host's CANCEL sends this and nothing here
               listened, so a joiner sat in a room that no longer existed. 1995 does the
               same: a sign-off from the game's owner puts the joiner back on the game
               list. The seat is the packet's, so a relayed goodbye from another player
               opens that seat instead of closing the room. */
            const int who = (int)get_u32(in + 4);
            if (who == 0) {
                nm_lobby_fail(NM_LOBBY_REFUSED, "the host closed the room");
                return;
            }
            if (who > 0 && who < NM_MAX_SEATS && who != s_seat) {
                s_ready[who] = 0;
                printf("NET|lobby-left|seat=%d|left the room\n", who);
                fflush(stdout);
            }
        } else if (magic == NM_REFUSE && n >= 12) {
            unsigned reason = get_u32(in + 4);
            char msg[160];
            snprintf(msg, sizeof msg, "the host refused: %s", nm_refuse_text(reason));
            nm_lobby_fail(NM_LOBBY_REFUSED, msg);
            return;
        }
    }
    s_lobby_state = (s_seat >= 1) ? NM_LOBBY_FULL : NM_LOBBY_WAITING;
}

int nm_lobby_poll(void)
{
    if (s_lobby_state == NM_LOBBY_IDLE || s_lobby_state == NM_LOBBY_STARTED
        || s_lobby_state == NM_LOBBY_REFUSED || s_lobby_state == NM_LOBBY_FAILED) {
        return s_lobby_state;
    }
    if (!s_sock) {
        nm_lobby_fail(NM_LOBBY_FAILED, "the connection went away");
        return s_lobby_state;
    }
    if (s_is_host) nm_lobby_poll_host();
    else nm_lobby_poll_join();
    return s_lobby_state;
}

int nm_lobby_state(void) { return s_lobby_state; }
int nm_lobby_is_host(void) { return s_is_host; }
int nm_lobby_seat(void) { return s_seat; }
/* HOW MANY PEOPLE THE ROOM IS WAITING TO SEAT: every seat that is a person's, host
   included. Counted off the setup on both ends, because a joiner holds the host's stamped
   copy and the answer has to be the same on every machine. */
int nm_lobby_wanted(void)
{
    int i, n = 0;
    for (i = 0; i < NM_MAX_SEATS; i++)
        if (lobby_seat_wants_person(i)) n++;
    return n;
}
int nm_lobby_seat_mode(int seat)
{
    if (seat < 0 || seat >= NM_MAX_SEATS) return NM_SEAT_BLOCK;
    if (seat >= lobby_room()) return NM_SEAT_BLOCK;
    return s_setup.mode[seat];
}
int nm_lobby_seat_taken(int seat)
{
    if (seat < 0 || seat >= NM_MAX_SEATS) return 0;
    if (seat == s_seat) return 1;
    if (s_is_host) return s_have[seat];
    /* A JOINER SEES WHO ELSE IS IN THROUGH THE SETUP. In a star it only ever hears the
       host, but the host stamps every occupied seat HUMAN before a WELCOME leaves, and
       re-welcomes the room whenever the table changes, so this is the host's own answer
       rather than the guess it used to be ("seats up to humans are taken"). */
    return s_setup.mode[seat] == NM_SEAT_HUMAN;
}
int nm_lobby_seat_ready(int seat)
{
    if (seat < 0 || seat >= NM_MAX_SEATS) return 0;
    return s_ready[seat];
}
int nm_lobby_filled(void)
{
    int i, n = 0;
    for (i = 0; i < NM_MAX_SEATS; i++) {
        if (i == s_seat || s_have[i]) n++;
    }
    return n;
}
int nm_lobby_all_ready(void)
{
    int i, playing = 0;
    const int room = lobby_room();
    /* THE PEOPLE IN THE ROOM, NOT THE SEATS THE MAP DREW. A room is as wide
       as the map has starts and nobody has to fill it: two players on an eight start map
       were told "Waiting for 6 more players to join" and START was dead, because this
       required filled() to reach wanted(), which counts every OPEN seat as a person yet
       to arrive. netdlg.cpp:3442 is the 1995 rule and it is this one: whoever is here,
       plus the computers, is the match. A seat nobody took is not a late player, and
       nm_lobby_start closes it. */
    for (i = 0; i < room; i++) {
        if (s_setup.mode[i] == NM_SEAT_BOT) { playing++; continue; }
        if (s_setup.mode[i] != NM_SEAT_HUMAN) continue;   /* OPEN or BLOCK: nobody is in it */
        playing++;
        if (!s_ready[i]) return 0;
    }
    /* ONE SEAT IS NOT A MATCH, whoever is in it. Two people, or a person and a computer:
       the host tab cannot open a room narrower than two, and a room of one person and
       seven blocked seats would otherwise start a game with nobody to play. */
    return playing >= 2;
}
void nm_chat_say(const char* text)
{
    unsigned char pkt[12 + NM_CHAT_MAX];
    int len;
    if (!text || s_seat < 0 || !s_sock) return;
    len = (int)strlen(text);
    if (len > NM_CHAT_MAX - 1) len = NM_CHAT_MAX - 1;
    if (len <= 0) return;
    /* LOCALLY FIRST: nm_send_others never loops back, and a speaker who saw every line
       but their own would be the one bug this pane could ship with. */
    nm_chat_push(s_seat, text, len);
    put_u32(pkt, NM_CHAT);
    put_u32(pkt + 4, (unsigned)s_seat);
    put_u32(pkt + 8, (unsigned)len);
    memcpy(pkt + 12, text, (size_t)len);
    if (s_is_host) nm_send_others(pkt, 12 + len);
    else nm_send_seat(0, pkt, 12 + len);
    printf("NET|chat|seat=%d|%s\n", s_seat, s_chat[(s_chat_total - 1) % NM_CHAT_LOG]);
    fflush(stdout);
}

int nm_chat_count(void) { return s_chat_total; }

const char* nm_chat_line(int i, int* seat)
{
    if (i < 0 || i >= s_chat_total || i < s_chat_total - NM_CHAT_LOG) return NULL;
    if (seat) *seat = s_chat_seat[i % NM_CHAT_LOG];
    return s_chat[i % NM_CHAT_LOG];
}

const char* nm_lobby_name(void) { return s_lobby_name; }
const NmSetup* nm_lobby_setup(void) { return &s_setup; }
const char* nm_lobby_error(void) { return s_lobby_err; }

int nm_lobby_kick(int seat)
{
    unsigned char pkt[8];
    int k;
    if (!s_is_host) return 0;
    if (seat <= 0 || seat >= NM_MAX_SEATS) return 0;   /* seat 0 is the host itself */
    if (!s_have[seat]) return 0;
    /* NOT UNTIL THAT PEER KNOWS WHICH SEAT IT IS IN. A KICK is addressed by seat number,
       and a joiner learns its own from the WELCOME; between being seated and receiving
       that WELCOME there is a window in which a removal names a seat the target cannot
       recognise as itself, so it reads the packet as somebody ELSE leaving and sits there
       waiting in a room it has been thrown out of. s_ack is the joiner's own confirmation
       that it has the setup, which is the same message that carried its seat, so it is
       exactly the receipt this needs. Refusing here is a delay of one round trip and it
       is bounded: the host re-sends the welcome every 300 ms until it is acknowledged. */
    if (!s_ack[seat]) return 0;
    put_u32(pkt, NM_KICK);
    put_u32(pkt + 4, (unsigned)seat);
    /* TO EVERYONE, not only to the one being removed. The others have that seat drawn on
       their own roster, and a removal only they cannot see is a seat that stays occupied
       by a player who is gone. Sent several times because it is a one-shot with no ack:
       the peer being removed will not be answering afterwards. */
    for (k = 0; k < 3; k++) {
        int i;
        for (i = 0; i < NM_MAX_SEATS; i++) {
            if (i != s_seat && s_have[i]) nm_send_seat(i, pkt, 8);
        }
    }
    /* A SEAT REMOVED WHILE THE START IS ON THE WIRE MAY ALREADY BE IN THE MATCH, where a
       KICK means nothing. A goodbye from seat 0 ends that match for it at once, instead of
       leaving it to wait thirty seconds of silence for turns nobody will send. A seat still
       in the lobby has the KICK first, which has already said why and closed its room. */
    if (s_starting && s_last_start_ms != 0) {
        unsigned char bye[8];
        put_u32(bye, NM_BYE);
        put_u32(bye + 4, 0u);
        for (k = 0; k < 3; k++) nm_send_seat(seat, bye, 8);
    }
    printf("NET|kicked|seat=%d\n", seat);
    fflush(stdout);
    s_have[seat] = 0;
    s_ack[seat] = 0;
    s_ready[seat] = 0;
    s_sack[seat] = 0;
    s_rtt[seat] = -1;
    lobby_cancel_start("a player was removed", seat);
    lobby_normalise();
    /* A REMOVAL DURING A START ABORTS IT, for the same reason a goodbye does: the start
       is waiting on an acknowledgement from a seat that will never send one. */
    if (s_starting) {
        char why[96];
        snprintf(why, sizeof why, "seat %d was removed while the match was starting", seat);
        lobby_abort_start(why);
    }
    return 1;
}

int nm_lobby_was_kicked(void) { return s_kicked; }

void nm_lobby_set_setup(const NmSetup* setup)
{
    NmSetup was;
    int i;
    /* NOT ONCE THE MATCH IS STARTING. START closes the seats nobody took,
       and the screen pushed its own copy back one frame later, re-opening all six: the
       turn barrier then waited for ever on chairs nobody was in, the match ran the three
       turns lockstep pre-marks and froze, and the renderer went on drawing a world that
       had stopped. That is what "I can see my MCV and nothing responds" is. The room is
       settled the moment START is pressed; nothing may edit it after that. */
    if (!s_is_host || !setup || s_starting) return;
    was = s_setup;
    s_setup = *setup;
    /* THE ROOM CANNOT SHRINK UNDER A SEATED PLAYER. The width follows the map the host
       picks (its start count), so it moves; but a seat with a person in it stays in the
       room whatever the map says, and the host's START is refused by the screen until the
       map seats everybody or somebody is removed. Who sits where is s_have[]'s, not the
       caller's, and lobby_normalise folds the two together. */
    for (i = NM_MAX_SEATS - 1; i > 0; i--) {
        if (s_have[i] && s_setup.seats < i + 1) s_setup.seats = i + 1;
    }
    lobby_normalise();
    /* A CHANGED SETUP IS RE-WELCOMED. Until 5 Sep 2026 a joiner learned the host's
       settings once, in its WELCOME, and then sat looking at a picture of the room as it
       was when it walked in: every seat mode, map and rule the host changed after that
       reached it only in the START. Clearing the acknowledgement makes the host's
       300 ms welcome loop send the setup again to everyone who is seated, and the joiner
       answers with the same NM_READY it answered the first one with. */
    if (memcmp(&was, &s_setup, sizeof was) != 0) {
        /* A START PRESSED FOR THE ROOM AS IT WAS is not a start for this one. */
        lobby_cancel_start("the host changed the room", -1);
        for (i = 1; i < NM_MAX_SEATS; i++)
            if (s_have[i]) s_ack[i] = 0;
        /* AND A CHANGED OFFER UNLIGHTS EVERY READY. Clearing the acknowledgement above
           re-sends the room; it never touched the ready lights, so a host could swap the
           map, the tech level or the credits out from under people who had already
           agreed to the old ones and START stayed available the whole time. What the
           joiner accepted is not what it would be playing.

           BOTH ENDS HAVE TO DO THIS AND THAT IS NOT BELT AND BRACES. A joiner's NM_TICK
           carries its own ready flag and the host takes the joiner's word for it, so a
           clear made only here is overwritten by the next tick from that seat, about
           fifty milliseconds later. The joiner clears its own when the re-WELCOME
           arrives; this half is what stops START being pressable in the gap. */
        /* AND THE ROOM'S ADVERTISED MAP FOLLOWS THE MAP. s_scen was written once, at
           nm_lobby_host, and never again -- so the moment the host changed map the room
           went on announcing, and refusing joiners against, the fingerprint of the map it
           had started on. Every joiner was then measured against the wrong yardstick: the
           right bytes were refused and the wrong ones let through. */
        if (strcmp(was.scenario, s_setup.scenario) != 0) {
            const unsigned h = nm_local_map_hash(s_setup.scenario);
            if (h != 0u && h != s_scen) {
                s_scen = h;
                printf("NET|room-map|%s|%08X\n", s_setup.scenario, s_scen);
                fflush(stdout);
            }
        }
        if (lobby_offer_changed(&was, &s_setup)) {
            for (i = 1; i < NM_MAX_SEATS; i++)
                if (s_have[i]) s_ready[i] = 0;
            printf("NET|offer-changed|ready-cleared\n");
        }
        printf("NET|setup|seats=%d|humans=%d|modes=%d%d%d%d%d%d%d%d\n", s_setup.seats,
               s_setup.humans, s_setup.mode[0], s_setup.mode[1], s_setup.mode[2],
               s_setup.mode[3], s_setup.mode[4], s_setup.mode[5], s_setup.mode[6],
               s_setup.mode[7]);
        fflush(stdout);
    }
}

int nm_lobby_start(void)
{
    if (!s_is_host) return 0;
    /* A SECOND PRESS IS NOT A SECOND START. Once the START is on the wire the lookahead it
       carries is settled, and re-deciding it would seed two joiners differently; while it
       is still waiting on a round trip, the wait already under way is the answer. */
    if (s_starting) return 1;
    /* A ROOM THAT HAS FAILED STARTS NOTHING, including one that failed because a start
       could not be taken back: its seats may still read as ready. */
    if (s_lobby_state != NM_LOBBY_WAITING && s_lobby_state != NM_LOBBY_FULL) return 0;
    if (!nm_lobby_all_ready()) return 0;
    if (!s_start_pending) {
        s_start_pending = 1;
        s_start_pressed_ms = nm_now_ms();
        s_last_ping_ms = 0;   /* ask now, not on the next one-second beat */
        printf("NET|start-pressed|unmeasured=%d\n", lobby_unmeasured());
        fflush(stdout);
    }
    /* A ROOM THAT IS ALREADY MEASURED STARTS ON THIS CALL, exactly as it always did. */
    lobby_try_start();
    return 1;
}

int nm_lobby_start_pending(void) { return s_start_pending; }

/* WHAT THIS MATCH COST AND WHAT REPAIRED IT, said once on the way out and from every door
   a match can leave by. A repair that works leaves no other trace, because the match
   simply does not stop, so turns-answered is the only evidence the answering half of it
   ever ran. Silent when no match ever started, and silent the second time because whoever
   printed it first has already cleared the socket. */
static void nm_say_leaving(void)
{
    if (!s_active) return;
    printf("NET|leaving|turns=%u|synced-frames=%u|packets-in=%lu|bad=%lu|orders-in=%lu"
           "|turns-asked=%u|turns-answered=%u\n",
           ls_exec_turn(&s_ls), s_synced_frames, s_ls.packets_in, s_ls.packets_bad,
           s_ls.orders_in, s_turns_asked, s_turns_answered);
    fflush(stdout);
}

void nm_lobby_cancel(void)
{
    if (s_sock) {
        unsigned char bye[8];
        int k;
        put_u32(bye, NM_BYE);
        put_u32(bye + 4, (unsigned)(s_seat < 0 ? 0 : s_seat));
        for (k = 0; k < 3; k++) nm_send_others(bye, 8);
        /* AND THE MATCH'S OWN COUNTERS, IF THERE WAS A MATCH. This is the door every
           game started from a lobby leaves by, and it used to close the socket without a
           word: nm_shutdown runs afterwards, finds the socket already gone, and prints
           nothing. So the one line that says whether a stall was repaired, and by whom,
           was printed only on the command-line paths and never on the one real players
           use. */
        nm_say_leaving();
        net_close(s_sock);
        s_sock = NULL;
        net_shutdown();
    }
    s_lobby_state = NM_LOBBY_IDLE;
    s_seat = -1;
    s_is_host = 0;
    s_starting = 0;
    s_start_pending = 0;
    memset(s_have, 0, sizeof s_have);
}

int nm_host(unsigned short port, const NmSetup* setup, unsigned abi_hash, unsigned scen_hash,
            int humans, int timeout_s)
{
    unsigned char in[LS_PACKET_MAX];
    unsigned char out[4 + 4 + 4 + 4 + NM_SETUP_BYTES];
    NetAddr from;
    time_t deadline;
    int n, i, seated = 1, started = 0;
    int ready[NM_MAX_SEATS];
    int acked[NM_MAX_SEATS];
    unsigned last_welcome = 0, last_start = 0;
    char txt[64];

    nm_relay_take();
    /* THE BLOCKING HOST DOOR IS NOT RELAYABLE, and this is not a limitation to lift
       later without work. It reads ver, abi and scn out of a HELLO and seats the sender;
       it has NO PASSCODE CHECK AT ALL, because on a LAN it never needed one. Relaying it
       would put a room with no door on the public internet. The lobby path
       (nm_lobby_host) is the one that checks a passcode, and it is the one the screens
       use; this door is the command line's, for gates and for a direct cable. */
    if (s_relayNow.armed) {
        printf("NET|error=the command-line host cannot be relayed: it has no passcode "
               "check. Host from the multiplayer screen instead.\n");
        fflush(stdout);
        return -1;
    }
    if (!setup || !nm_open(port)) return -1;
    if (humans < 1) humans = 1;
    if (humans > NM_MAX_SEATS) humans = NM_MAX_SEATS;
    s_setup = *setup;
    s_abi = abi_hash;
    s_scen = scen_hash;
    s_is_host = 1;
    s_humans = humans;
    memset(s_have, 0, sizeof s_have);
    memset(ready, 0, sizeof ready);
    memset(acked, 0, sizeof acked);
    /* The host is its own seat 0 and does not need an address for itself. */
    printf("NET|hosting|port=%u|scenario=%s|humans=%d|seats=%d|waiting=%ds\n",
           (unsigned)net_local_port(s_sock), s_setup.scenario, humans, s_setup.seats, timeout_s);
    /* SAY WHICH ADDRESS TO JOIN, because the commonest way a LAN match fails to happen is
       nobody being sure. The socket is bound to INADDR_ANY, so every one of these reaches
       it; which one a given machine should use is a question about their network and not
       about this program, so all of them are printed rather than one being guessed at. */
    {
        char addrs[8][64];
        int na = net_local_addrs(addrs, 8);
        int ai2;
        if (na <= 0) {
            printf("NET|join-me-at|unknown (no non-loopback IPv4 address was readable; "
                   "the other machines still join by whatever address reaches this one)\n");
        }
        for (ai2 = 0; ai2 < na; ai2++) {
            printf("NET|join-me-at|%s:%u\n", addrs[ai2], (unsigned)net_local_port(s_sock));
        }
    }
    fflush(stdout);

    /* ---- PHASE 1: seat the joiners -------------------------------------------------
       A HELLO from an address already seated is a repeat, not a second player: the
       joiner re-sends every 250 ms until it hears a WELCOME, so most HELLOs after the
       first are repeats and giving each one a new seat would fill the match with one
       person. The address is the identity here, which is exactly as strong as the star
       is, and no stronger; Phase 5's lobby is where identity stops being an address. */
    /* THE LOOP RUNS UNTIL EVERY SEAT IS FILLED *AND* EVERY JOINER HAS ITS SEAT, which are
       two different things and conflating them was a real bug. Exiting as soon as the last
       HELLO arrived left the joiners that had just been seated with no WELCOME, because
       the WELCOME is sent from inside this loop: they went on to accept the START, which
       carries no seat, and reported `seat=-1`. Filling the last chair is not the same as
       everybody knowing which chair they are in. */
    deadline = time(0) + timeout_s;
    while (time(0) < deadline) {
        int all_ready;
        n = net_recv(s_sock, &from, in, (int)sizeof in);
        if (n >= 16 && get_u32(in) == NM_HELLO) {
            unsigned ver = get_u32(in + 4);
            unsigned abi = get_u32(in + 8);
            unsigned scn = get_u32(in + 12);
            int seat = nm_seat_of(&from);
            int scen_bad = (scn != 0 && scen_hash != 0 && scn != scen_hash);
            /* The same rule as the lobby path above, and here for the same reason: this
               is the door the gates and the .bat launchers come through, so a rule that
               held on only one of the two doors would be proved green by a suite that
               never used the other one. */
            int abi_unknown = (abi == 0u || abi_hash == 0u);
            if (ver != NM_VERSION || abi_unknown || abi != abi_hash || scen_bad) {
                unsigned reason = (ver != NM_VERSION) ? 1u
                                : (abi_unknown ? 5u : (abi != abi_hash ? 2u : 3u));
                unsigned char no[12];
                put_u32(no, NM_REFUSE);
                put_u32(no + 4, reason);
                put_u32(no + 8, reason == 3u ? scen_hash : abi_hash);
                net_send(s_sock, &from, no, 12);
                net_addr_text(&from, txt, (int)sizeof txt);
                printf("NET|refused|peer=%s|reason=%s|theirs=%08X|mine=%08X\n", txt,
                       nm_refuse_text(reason), reason == 3u ? scn : abi,
                       reason == 3u ? scen_hash : abi_hash);
                fflush(stdout);
                continue;
            }
            if (seat < 0) {
                seat = seated++;
                s_peers[seat] = from;
                s_have[seat] = 1;
                net_addr_text(&from, txt, (int)sizeof txt);
                printf("NET|joiner|seat=%d|peer=%s|seated=%d/%d\n", seat, txt, seated, humans);
                fflush(stdout);
            }
        }
        if (n >= 8 && get_u32(in) == NM_READY) {
            int seat = nm_seat_of(&from);
            if (seat > 0) ready[seat] = 1;
        }
        /* The welcome carries a joiner's OWN seat, so it is sent per seat rather than
           broadcast, and repeated until that joiner says it has it. */
        if (last_welcome == 0 || nm_now_ms() - last_welcome > 300) {
            for (i = 1; i < NM_MAX_SEATS; i++) {
                if (!s_have[i] || ready[i]) continue;
                put_u32(out, NM_WELCOME);
                put_u32(out + 4, abi_hash);
                put_u32(out + 8, scen_hash);
                put_u32(out + 12, (unsigned)i);
                setup_pack(&s_setup, out + 16);
                nm_send_seat(i, out, (int)sizeof out);
            }
            last_welcome = nm_now_ms();
        }
        all_ready = (seated >= humans);
        for (i = 1; i < humans; i++) {
            if (!ready[i]) all_ready = 0;
        }
        if (all_ready) break;
        if (n <= 0) nm_nap(5);
    }
    {
        int all_ready = (seated >= humans);
        for (i = 1; i < humans; i++) {
            if (!ready[i]) all_ready = 0;
        }
        if (!all_ready) {
            if (seated < humans) {
                printf("NET|error=only %d of %d players joined within %d seconds\n",
                       seated, humans, timeout_s);
            } else {
                printf("NET|error=all %d players joined but not all of them took their seat\n",
                       humans);
            }
            fflush(stdout);
            net_close(s_sock);
            s_sock = NULL;
            net_shutdown();
            return -1;
        }
    }
    /* ---- PHASE 2: start them together ----------------------------------------------
       WHY A SECOND ROUND AT ALL, when every joiner already has the setup. Because the
       setup a joiner was welcomed with was PROVISIONAL: seat 3 was welcomed before seats
       4 to 7 existed, and the roster's human count is what the scheduler is initialised
       with. Starting on the provisional copy would give the early joiners a scheduler
       waiting on fewer peers than the match has, and it would wait for ever the moment a
       late seat spoke. So the host fixes the roster once everyone is in and sends it to
       all of them, and nobody plays until everybody has acknowledged it. */
    s_setup.humans = humans;
    if (s_setup.seats < humans) s_setup.seats = humans;
    deadline = time(0) + (timeout_s < 30 ? timeout_s : 30);
    while (!started && time(0) < deadline) {
        n = net_recv(s_sock, &from, in, (int)sizeof in);
        if (n >= 8 && get_u32(in) == NM_SACK) {
            int seat = nm_seat_of(&from);
            if (seat > 0) acked[seat] = 1;
        }
        if (last_start == 0 || nm_now_ms() - last_start > 200) {
            put_u32(out, NM_START);
            put_u32(out + 4, abi_hash);
            put_u32(out + 8, scen_hash);
            put_u32(out + 12, (unsigned)humans);
            setup_pack(&s_setup, out + 16);
            for (i = 1; i < NM_MAX_SEATS; i++) {
                if (s_have[i] && !acked[i]) nm_send_seat(i, out, (int)sizeof out);
            }
            last_start = nm_now_ms();
        }
        started = 1;
        for (i = 1; i < humans; i++) {
            if (!acked[i]) started = 0;
        }
        if (!started && n <= 0) nm_nap(5);
    }
    if (!started) {
        printf("NET|error=not every player acknowledged the start\n");
        fflush(stdout);
        net_close(s_sock);
        s_sock = NULL;
        net_shutdown();
        return -1;
    }
    /* One last START, unasked. The ack that satisfied the loop above may itself be the
       last packet a joiner sends before it starts ticking, and a joiner that acked while
       an earlier START was in flight has nothing further to wait for; this costs one
       datagram per joiner and removes a start that hangs on a lost ack. */
    for (i = 1; i < NM_MAX_SEATS; i++) {
        if (s_have[i]) nm_send_seat(i, out, (int)sizeof out);
    }

    nm_reset_match(0, humans);
    printf("NET|match|seat=0|humans=%d|seats=%d|scenario=%s|speed=%d|abi=%08X|scen=%08X\n",
           humans, s_setup.seats, s_setup.scenario, s_setup.speed, abi_hash, scen_hash);
    fflush(stdout);
    return 0;
}

int nm_join(const char* addr, unsigned short port, NmSetup* setup, unsigned abi_hash,
            unsigned scen_hash, int timeout_s)
{
    unsigned char in[LS_PACKET_MAX];
    unsigned char hello[16];
    NetAddr from;
    time_t deadline;
    int have_setup = 0, have_start = 0, n, k, my_seat = -1, humans = 0;
    unsigned last_hello = 0;
    char txt[64];

    nm_relay_take();
    /* Refused for symmetry with the host door above: the two are a matched pair used by
       the same callers, and a joiner that could relay while the host could not would only
       ever produce a confusing failure. */
    if (s_relayNow.armed) {
        printf("NET|error=the command-line join cannot be relayed. Join from the "
               "multiplayer screen instead.\n");
        fflush(stdout);
        return -1;
    }
    if (!setup || !nm_open(0)) return -1;
    memset(s_have, 0, sizeof s_have);
    if (net_resolve(addr, port, &s_peers[0]) != 0) {
        printf("NET|error=could not resolve '%s'\n", addr);
        net_close(s_sock);
        s_sock = NULL;
        net_shutdown();
        return -1;
    }
    /* SEAT 0 IS THE HOST AND IS THE ONLY ADDRESS A JOINER EVER NEEDS. In a star it is
       also the only one it is ever given: the host does not publish the other joiners,
       because nothing here would send to them. */
    s_have[0] = 1;
    s_is_host = 0;
    s_abi = abi_hash;
    s_scen = scen_hash;
    net_addr_text(&s_peers[0], txt, (int)sizeof txt);
    printf("NET|joining|peer=%s|waiting=%ds\n", txt, timeout_s);
    fflush(stdout);
    put_u32(hello, NM_HELLO);
    put_u32(hello + 4, NM_VERSION);
    put_u32(hello + 8, abi_hash);
    put_u32(hello + 12, scen_hash);
    deadline = time(0) + timeout_s;
    while (!have_start && time(0) < deadline) {
        if (!have_setup && (last_hello == 0 || nm_now_ms() - last_hello > 250)) {
            nm_send_seat(0, hello, 16);
            last_hello = nm_now_ms();
        }
        n = net_recv(s_sock, &from, in, (int)sizeof in);
        if (n >= 4 && net_addr_equal(&from, &s_peers[0])) {
            unsigned magic = get_u32(in);
            if (magic == NM_WELCOME && n >= 16 + NM_SETUP_BYTES) {
                /* CHECKED AT BOTH ENDS ON PURPOSE. The host checks the HELLO, which
                   catches a joiner that knows its own map; this catches a joiner that was
                   started with no --scen at all and sent 0, and it is also the end that
                   can name the file the player has to replace. */
                unsigned host_scen = get_u32(in + 8);
                if (host_scen != 0 && scen_hash != 0 && host_scen != scen_hash) {
                    printf("NET|refused-by-me|reason=map bytes|host-scen=%08X|mine=%08X\n",
                           host_scen, scen_hash);
                    fflush(stdout);
                    net_close(s_sock);
                    s_sock = NULL;
                    net_shutdown();
                    return -1;
                }
                my_seat = (int)get_u32(in + 12);
                if (my_seat < 1 || my_seat >= NM_MAX_SEATS) {
                    printf("NET|error=the host seated me at %d, which is not a seat\n", my_seat);
                    fflush(stdout);
                    net_close(s_sock);
                    s_sock = NULL;
                    net_shutdown();
                    return -1;
                }
                setup_unpack(setup, in + 16);
                s_setup = *setup;
                if (!have_setup) {
                    printf("NET|seated|seat=%d\n", my_seat);
                    fflush(stdout);
                }
                have_setup = 1;
                /* Several times: it is the one datagram whose loss strands the host. */
                for (k = 0; k < 3; k++) {
                    unsigned char rd[8];
                    put_u32(rd, NM_READY);
                    put_u32(rd + 4, (unsigned)my_seat);
                    nm_send_seat(0, rd, 8);
                }
            } else if (magic == NM_START && n >= 16 + NM_SETUP_BYTES && my_seat >= 1) {
                /* `my_seat >= 1` IS THE GUARD, and it is not belt and braces. A START that
                   arrives before this joiner has been welcomed carries no seat of its own,
                   and taking it would start a peer that does not know which seat it is:
                   it would send its orders stamped -1 and every hash it reported would be
                   attributed to nobody. Ignoring it is right because the host re-sends
                   both, so the WELCOME that is still owed will arrive. */
                /* THE FINAL ROSTER, and the one the scheduler is built from. The setup
                   that came with the welcome was provisional: seats after this one had
                   not arrived yet. */
                unsigned char ak[8];
                humans = (int)get_u32(in + 12);
                setup_unpack(setup, in + 16);
                s_setup = *setup;
                put_u32(ak, NM_SACK);
                put_u32(ak + 4, (unsigned)my_seat);
                for (k = 0; k < 3; k++) nm_send_seat(0, ak, 8);
                have_start = 1;
            } else if (magic == NM_REFUSE && n >= 12) {
                unsigned reason = get_u32(in + 4);
                printf("NET|refused-by-host|reason=%s|host-%s=%08X|mine=%08X\n",
                       nm_refuse_text(reason), reason == 3u ? "scen" : "abi", get_u32(in + 8),
                       reason == 3u ? scen_hash : abi_hash);
                fflush(stdout);
                net_close(s_sock);
                s_sock = NULL;
                net_shutdown();
                return -1;
            }
        }
        if (!have_start) nm_nap(5);
    }
    if (!have_start) {
        printf("NET|error=%s within %d seconds (host not running, or UDP %u does not reach it)\n",
               have_setup ? "the host seated me but never started the match"
                          : "the host never answered",
               timeout_s, (unsigned)port);
        fflush(stdout);
        net_close(s_sock);
        s_sock = NULL;
        net_shutdown();
        return -1;
    }
    if (humans < 1 || humans > NM_MAX_SEATS) humans = s_setup.humans;
    if (humans < 1 || humans > NM_MAX_SEATS) humans = 2;
    s_humans = humans;
    nm_reset_match(my_seat, humans);
    printf("NET|match|seat=%d|humans=%d|seats=%d|scenario=%s|speed=%d|abi=%08X|scen=%08X\n",
           my_seat, humans, s_setup.seats, s_setup.scenario, s_setup.speed, abi_hash, scen_hash);
    fflush(stdout);
    return my_seat;
}

int nm_active(void) { return s_active; }
int nm_seat(void) { return s_seat; }
int nm_seats(void) { return s_setup.seats > 0 ? s_setup.seats : s_seats; }
unsigned nm_waiting_mask(void) { return s_active ? ls_waiting_mask(&s_ls) : 0u; }
int nm_humans(void) { return s_humans > 0 ? s_humans : s_seats; }
int nm_joined(void)
{
    int i, n = 0;
    for (i = 0; i < NM_MAX_SEATS; i++) {
        if (s_have[i]) n++;
    }
    return s_is_host ? n + 1 : n;   /* the host does not hold an address for itself */
}
const NmSetup* nm_setup(void) { return &s_setup; }
const char* nm_seat_name(int seat)
{
    if (seat < 0 || seat >= NM_MAX_SEATS) return "";
    return s_setup.name[seat];
}
/* The room is settled: START has gone out and the roster must not be edited again. */
int nm_lobby_starting(void) { return s_starting; }

void nm_set_engine(NmDrainFn drain, NmPostFn post, void* user, int event_size)
{
    s_drain = drain;
    s_post = post;
    s_user = user;
    s_event_size = event_size > 0 ? event_size : 22;
}

/* ---------------------------------------------------------------------- the turn --- */

/* THE SEATS THIS MATCH HAS, as a bitmask, minus the ones that have gone. It is what a
   waiting mask off the wire is cut down to before anything acts on it. A joiner knows the
   addresses of nobody but the host, so this counts the ROSTER rather than the address
   table, which is the one thing both ends agree on. */
static unsigned nm_seated_bits(void)
{
    unsigned m = 0;
    int i;
    for (i = 0; i < s_seats && i < NM_MAX_SEATS; i++) {
        if (!s_left[i]) m |= (1u << (unsigned)i);
    }
    return m;
}

/* MARK A SEAT GONE FROM A NAMED TURN, and write down which turn that was. A match that
   ends up disagreeing about a departure is a desync, and the one thing that makes it
   diagnosable afterwards is each peer having said out loud which turn it used. */
static void nm_seat_absent_at(int who, unsigned turn)
{
    if (who < 0 || who >= NM_MAX_SEATS || who == s_seat) return;
    if (ls_set_absent_at(&s_ls, who, turn) != LS_OK) return;
    if (s_absent_at[who] != turn) {
        s_absent_at[who] = turn;
        printf("NET|seat-quiet|seat=%d|from-turn=%u|now=%u\n", who, turn,
               ls_exec_turn(&s_ls));
        fflush(stdout);
    }
}

/* MAY THIS SEAT'S REQUEST BE ANSWERED YET, and stamp it if so. Both of these record the
   moment they say yes, so calling one is taking the slot, not asking about it. */
static int nm_need_may_answer(int who)
{
    const unsigned now = nm_now_ms();
    if (who < 0 || who >= NM_MAX_SEATS) return 0;
    if (s_need_ans_ms[who] != 0 && now - s_need_ans_ms[who] < (unsigned)NM_RESEND_MS) return 0;
    s_need_ans_ms[who] = now ? now : 1u;
    return 1;
}

static int nm_need_may_forward(int who)
{
    const unsigned now = nm_now_ms();
    if (who < 0 || who >= NM_MAX_SEATS) return 0;
    if (s_need_fwd_ms[who] != 0 && now - s_need_fwd_ms[who] < (unsigned)NM_RESEND_MS) return 0;
    s_need_fwd_ms[who] = now ? now : 1u;
    return 1;
}

static int nm_need_gone_may_answer(int who)
{
    const unsigned now = nm_now_ms();
    if (who < 0 || who >= NM_MAX_SEATS) return 0;
    if (s_need_gone_ms[who] != 0 && now - s_need_gone_ms[who] < (unsigned)NM_RESEND_MS) return 0;
    s_need_gone_ms[who] = now ? now : 1u;
    return 1;
}

/* THE HOST'S GOODBYE FOR A SEAT, WITH THE TURN IT LANDS ON, to one seat. Every goodbye the
   host sends in a match goes through here, so the test instrument above has one place to
   hold them. */
static void nm_bye_to(int seat, int who, unsigned turn)
{
    unsigned char bye[12];
    put_u32(bye, NM_BYE);
    put_u32(bye + 4, (unsigned)who);
    put_u32(bye + 8, turn);
    if (seat == s_test_hold_seat && s_test_hold_ms > 0) {
        if (s_test_held_n < NM_TEST_HOLD_MAX) {
            memcpy(s_test_held[s_test_held_n], bye, sizeof bye);
            s_test_held_due[s_test_held_n] = nm_now_ms() + s_test_hold_ms;
            s_test_held_n++;
        }
        return;
    }
    nm_send_seat(seat, bye, (int)sizeof bye);
}

static void nm_test_release_held(void)
{
    int k = 0, j;
    const unsigned now = nm_now_ms();
    while (k < s_test_held_n) {
        if ((int)(now - s_test_held_due[k]) >= 0) {
            nm_send_seat(s_test_hold_seat, s_test_held[k], 12);
            for (j = k + 1; j < s_test_held_n; j++) {
                memcpy(s_test_held[j - 1], s_test_held[j], 12);
                s_test_held_due[j - 1] = s_test_held_due[j];
            }
            s_test_held_n--;
        } else {
            k++;
        }
    }
}

/* CNC3D_NETTEST_LATE_BYE's delayed relay: one packet in, sent to `seat` once it is due. A
   full queue sends at once rather than dropping, and the gate that uses the switch checks
   from the logs that the order it wanted really happened. */
static void nm_test_delay_push(int seat, const unsigned char* p, int n)
{
    int slot;
    if (n <= 0 || n > LS_PACKET_MAX) return;
    if (s_test_delay_n >= NM_TEST_DELAY_MAX) {
        nm_send_seat(seat, p, n);
        return;
    }
    slot = (s_test_delay_head + s_test_delay_n) % NM_TEST_DELAY_MAX;
    memcpy(s_test_delay_pkt[slot], p, (size_t)n);
    s_test_delay_len[slot] = n;
    s_test_delay_due[slot] = nm_now_ms() + s_test_late_ms;
    s_test_delay_n++;
}

static void nm_test_release_delayed(void)
{
    const unsigned now = nm_now_ms();
    while (s_test_delay_n > 0 && (int)(now - s_test_delay_due[s_test_delay_head]) >= 0) {
        nm_send_seat(s_test_late_to, s_test_delay_pkt[s_test_delay_head],
                     s_test_delay_len[s_test_delay_head]);
        s_test_delay_head = (s_test_delay_head + 1) % NM_TEST_DELAY_MAX;
        s_test_delay_n--;
    }
}

/* CNC3D_NETTEST_LATE_BYE's held goodbye, handed back to the receive loop as though it had
   just arrived, once this host has run every turn the departing seat sent. Returns its
   length the one time it is due, and 0 otherwise. */
static int nm_test_late_bye_due(unsigned char* in, NetAddr* from)
{
    unsigned first, now_turn, waited;
    if (s_test_late_bye_state != 1) return 0;
    first = ls_seat_first_missing(&s_ls, s_test_late_from);
    now_turn = ls_exec_turn(&s_ls);
    waited = nm_now_ms() - s_test_late_bye_since;
    if (now_turn < first && waited < NM_TEST_LATE_BACKSTOP_MS) return 0;
    memcpy(in, s_test_late_bye, (size_t)s_test_late_bye_len);
    *from = s_test_late_bye_from;
    s_test_late_bye_state = 2;
    printf("NET|test|late-goodbye-read|seat=%d|now=%u|first-missing=%u|waited=%ums\n",
           s_test_late_from, now_turn, first, waited);
    fflush(stdout);
    return s_test_late_bye_len;
}

/* KEEP A TURN PACKET THE HOST HAS ACCEPTED FROM A SEAT. See s_keep_pkt. A packet with the
   same top as one already kept is a repeat, or an answer covering the same turns, and only
   the longer of the two is kept. */
static void nm_keep_packet(int seat, const unsigned char* p, int n)
{
    const unsigned top = get_u32(p + 8);
    int k, slot;
    if (seat <= 0 || seat >= NM_MAX_SEATS || n < 12 || n > LS_PACKET_MAX) return;
    for (k = 0; k < NM_KEEP_PKTS; k++) {
        if (s_keep_len[seat][k] > 0 && s_keep_top[seat][k] == top) {
            if (n > s_keep_len[seat][k]) {
                memcpy(s_keep_pkt[seat][k], p, (size_t)n);
                s_keep_len[seat][k] = n;
            }
            return;
        }
    }
    slot = s_keep_next[seat];
    memcpy(s_keep_pkt[seat][slot], p, (size_t)n);
    s_keep_len[seat][slot] = n;
    s_keep_top[seat][slot] = top;
    s_keep_next[seat] = (slot + 1) % NM_KEEP_PKTS;
}

/* THE KEPT PACKET THAT CARRIES `turn` FOR A SEAT THAT HAS LEFT, or -1. Only packets whose
   newest turn is below the departure turn qualify, so an answer can never hand a survivor
   one of that seat's turns at or after the one the host named. Nothing is lost by that
   rule: a packet holding a turn below the named one and a turn at or above it would hold
   the named turn too, since a packet's turns run unbroken down from its top, and the named
   turn is by definition one the host never received. Of the packets that qualify the one
   with the lowest top is taken, because if any of them carries `turn` that one does. */
static int nm_kept_for(int seat, unsigned turn, unsigned left_turn)
{
    int k, best = -1;
    for (k = 0; k < NM_KEEP_PKTS; k++) {
        const unsigned top = s_keep_top[seat][k];
        if (s_keep_len[seat][k] <= 0 || top < turn || top >= left_turn) continue;
        if (best < 0 || top < s_keep_top[seat][best]) best = k;
    }
    if (best >= 0) {
        /* The packet's oldest turn is top - (blocks - 1). Compared the other way round so
           that a packet from the first few turns of a match cannot wrap below zero. */
        const unsigned blocks = s_keep_pkt[seat][best][7];
        if (blocks == 0 || s_keep_top[seat][best] > turn + (blocks - 1u)) best = -1;
    }
    return best;
}

static void nm_pump(void)
{
    unsigned char in[LS_PACKET_MAX];
    NetAddr from;
    int n;
    if (!s_sock) return;
    if (s_test_held_n > 0) nm_test_release_held();
    if (s_test_delay_n > 0) nm_test_release_delayed();
    /* A goodbye CNC3D_NETTEST_LATE_BYE held is read first, once it is due. Outside that
       test the first call returns 0 and this is the plain receive loop. */
    while ((n = nm_test_late_bye_due(in, &from)) > 0
           || (n = net_recv(s_sock, &from, in, (int)sizeof in)) > 0) {
        unsigned magic;
        int src = nm_seat_of(&from);
        if (src < 0) continue;          /* not a peer of this match */
        if (n < 4) continue;
        s_last_heard_ms[src] = nm_now_ms();
        magic = get_u32(in);
        if (magic == LS_MAGIC) {
            /* NOTHING MORE FROM A SEAT THE HOST HAS DECLARED GONE, neither stored nor
               relayed. The host named the departure turn as the first turn of that seat's
               it did not hold, so every turn below it is already here and has already been
               passed on. A packet that turns up afterwards, late or out of order, can only
               carry turns at or after the named one, and relaying it would let a survivor
               that has not yet heard the goodbye find that seat's named turn reported and
               run its orders there. */
            if (s_is_host && src > 0 && s_left[src]) continue;
            /* THE RELAY. The host passes a joiner's orders to the other joiners exactly
               as they arrived. ls_on_packet has already taken its own copy, and the
               scheduler's store is idempotent, so a packet that arrives twice by two
               routes costs nothing. The host also keeps a copy, so it can answer for this
               seat after it has gone; see s_keep_pkt. */
            if (ls_on_packet(&s_ls, in, n) == LS_OK && s_is_host && src > 0 && n >= 12
                && in[6] == (unsigned char)src) {
                nm_keep_packet(src, in, n);
            }
            if (s_is_host && src == s_test_cut_from && n >= 12
                && get_u32(in + 8) >= s_test_cut_turn) {
                int k;
                for (k = 0; k < NM_MAX_SEATS; k++)
                    if (k != s_seat && k != src && k != s_test_cut_to) nm_send_seat(k, in, n);
            } else if (s_is_host && src == s_test_late_from && n >= 12
                       && get_u32(in + 8) >= s_test_late_turn) {
                int k;
                for (k = 0; k < NM_MAX_SEATS; k++)
                    if (k != s_seat && k != src && k != s_test_late_to) nm_send_seat(k, in, n);
                nm_test_delay_push(s_test_late_to, in, n);
            } else {
                nm_relay(src, in, n);
            }
        } else if (magic == NM_SYNC && n >= 16) {
            unsigned seat = get_u32(in + 4);
            unsigned frame = get_u32(in + 8);
            unsigned hash = get_u32(in + 12);
            unsigned slot = frame % NM_HASH_RING;
            if (seat < (unsigned)NM_MAX_SEATS && (int)seat != s_seat) {
                s_peer_hash[seat][slot] = hash;
                s_peer_hash_frame[seat][slot] = frame;
                if (s_my_hash_frame[slot] == frame) {
                    if (s_my_hash[slot] != hash && !s_desynced) {
                        s_desynced = 1;
                        s_desync_frame = frame;
                        printf("NETDESYNC|frame=%u|seat=%u|mine=%08X|peer=%08X|abi=%08X|scen=%08X\n",
                               frame, seat, s_my_hash[slot], hash, s_abi, s_scen);
                        fflush(stdout);
                    } else if (s_my_hash[slot] == hash) {
                        s_synced_frames++;
                    }
                }
            }
            nm_relay(src, in, n);
        } else if (magic == NM_BYE) {
            /* THE SEAT COMES FROM THE PACKET, NOT FROM THE ADDRESS, and that distinction
               is the whole reason a relayed goodbye works. On a joiner every relayed
               packet arrives from the HOST's address, so `src` is 0 for all of them.
               Marking s_left[src] would have recorded the host as gone the moment any
               other joiner quit, ending six matches because one player pressed escape. */
            int who = (n >= 8) ? (int)get_u32(in + 4) : src;
            if (who < 0 || who >= NM_MAX_SEATS) who = src;
            /* AND ON THE HOST, THE SEAT MUST BE THE SENDER'S OWN. NM_CHAT below has
               made this check since it was written; these two never did, so any seated
               peer could name ANY seat here and the host would act on it and relay the
               decision to everybody as its own. That is one packet to make another
               player resign, or to walk them out of the match. The test is host-only and
               src-positive for the reason the comment above gives: on a joiner every
               relayed packet wears the host's address, so there the packet's seat is the
               only truth there is. */
            if (s_is_host && src > 0 && who != src) continue;
            /* CNC3D_NETTEST_LATE_BYE: the first goodbye from that seat is held, and repeats
               of it are dropped, until the receive loop hands the held copy back. */
            if (s_is_host && src > 0 && src == s_test_late_from && s_test_late_bye_state != 2) {
                if (s_test_late_bye_state == 0) {
                    const int keep = n < (int)sizeof s_test_late_bye ? n : (int)sizeof s_test_late_bye;
                    memcpy(s_test_late_bye, in, (size_t)keep);
                    s_test_late_bye_len = keep;
                    s_test_late_bye_from = from;
                    s_test_late_bye_since = nm_now_ms();
                    s_test_late_bye_state = 1;
                }
                continue;
            }
            if (!s_left[who]) {
                printf("NET|peer-left|seat=%d|turn=%u\n", who, ls_exec_turn(&s_ls));
                fflush(stdout);
            }
            s_left[who] = 1;
            /* See s_host_gone. Seat 0 is the host by construction on every peer. */
            if (who == 0 && !s_is_host) s_host_gone = 1;
            /* THE TURN IT TAKES EFFECT ON. The HOST decides it and everyone obeys, which
               is what keeps the armies dying on the same frame everywhere. On the host the
               number is always its own: a turn written into a joiner's goodbye is ignored,
               because no joiner is in a position to know it. On a joiner the only number
               there is comes from the host.
               THE NUMBER IS THE FIRST TURN THAT SEAT HAS NOT SPOKEN FOR, not a guess from
               the host's own send counter. It used to be send turn plus the lookahead plus
               one, which is a turn BEYOND the last one the departing seat ever sent, so a
               match that waited for the seat until then would wait for a turn that was
               never coming. Asking the scheduler gives a number every turn below which is
               already in hand, so the wait always ends. */
            if (s_is_host) {
                int k, c;
                if (s_left_turn[who] == LS_TURN_NONE)
                    s_left_turn[who] = ls_seat_first_missing(&s_ls, who);
                for (c = 0; c < 3; c++)
                    for (k = 0; k < NM_MAX_SEATS; k++)
                        if (k != s_seat) nm_bye_to(k, who, s_left_turn[who]);
            } else if (n >= 12) {
                const unsigned t = get_u32(in + 8);
                if (t != LS_TURN_NONE) s_left_turn[who] = t;
            }
            /* AND THE SCHEDULER IS TOLD WHEN, NOT JUST WHO, AND ONLY IN THE HOST'S WORDS.
               Marking the seat absent the instant the goodbye was read is the same
               decision taken at a different turn on every machine, because it is taken on
               each machine's own clock: a peer holding the departing seat's last turn
               executed those orders while a peer that never received them executed none,
               and both played on from different worlds.
               A JOINER THAT HAS NO NUMBER YET DOES NOTHING, and waiting is what makes it
               safe. Until the host's goodbye arrives the seat is still a seat, so this
               peer cannot run the named turn: nobody holds that seat's report for it. That
               is the whole guarantee nm_left_due rests on. The one exception is the host
               itself leaving, which ends the match here whatever turn it lands on. */
            if (who != s_seat) {
                if (s_left_turn[who] != LS_TURN_NONE)
                    nm_seat_absent_at(who, s_left_turn[who]);
                else if (who == 0)
                    nm_seat_absent_at(who, ls_seat_first_missing(&s_ls, who));
            }
        } else if (magic == NM_SURR && n >= 8) {
            /* A SEAT HAS RESIGNED. Same shape as the goodbye above and for the same
               reason: the HOST decides which turn it lands on and everybody obeys, so
               the house dies on one frame everywhere. The seat comes from the PACKET,
               not the address, because on a joiner every relayed packet wears the
               host's.

               Nothing here marks the seat absent or left: that player is still in the
               match, watching, and still sending turns. Marking them absent would tell
               every other peer to stop waiting for turns that are still arriving, and
               there is no way back from it. */
            int who = (int)get_u32(in + 4);
            /* AND ON THE HOST, THE SEAT MUST BE THE SENDER'S OWN. NM_CHAT below has
               made this check since it was written; these two never did, so any seated
               peer could name ANY seat here and the host would act on it and relay the
               decision to everybody as its own. That is one packet to make another
               player resign, or to walk them out of the match. The test is host-only and
               src-positive for the reason the comment above gives: on a joiner every
               relayed packet wears the host's address, so there the packet's seat is the
               only truth there is. */
            if (s_is_host && src > 0 && who != src) continue;
            if (who >= 0 && who < NM_MAX_SEATS) {
                if (n >= 12) {
                    const unsigned t = get_u32(in + 8);
                    if (t != LS_TURN_NONE) s_surr_turn[who] = t;
                } else if (s_is_host && s_surr_turn[who] == LS_TURN_NONE) {
                    s_surr_turn[who] = ls_send_turn(&s_ls) + ls_ahead(&s_ls) + 1;
                }
                if (s_is_host && s_surr_turn[who] != LS_TURN_NONE) {
                    unsigned char pkt[12];
                    int k;
                    put_u32(pkt, NM_SURR);
                    put_u32(pkt + 4, (unsigned)who);
                    put_u32(pkt + 8, s_surr_turn[who]);
                    for (k = 0; k < 3; k++) nm_send_others(pkt, 12);
                } else {
                    nm_relay(src, in, n);
                }
            }
        } else if (magic == NM_NEED && n >= 16) {
            /* A PEER IS STUCK ON A TURN THE REDUNDANCY WINDOW NO LONGER CARRIES. See
               nm_ask_for_turn for how it gets stuck. The answer is this peer's own orders
               from that turn on, and it goes where this peer's turn packets always go:
               from the host straight to the seat that asked, from a joiner to the host,
               which relays it to the other joiners like any other turn packet. */
            const int who = (int)get_u32(in + 4);
            const unsigned turn = get_u32(in + 8);
            unsigned mask = get_u32(in + 12);
            /* The seat must be the sender's own on the host, as for a goodbye; on a joiner
               every relayed packet wears the host's address, so the packet's is all there
               is. An answer costs one packet, but it is still not one a stranger chooses
               the destination of. */
            if (s_is_host && src > 0 && who != src) continue;
            if (who < 0 || who >= NM_MAX_SEATS || who == s_seat) continue;
            /* THE HOST ANSWERS FOR A SEAT THAT HAS LEFT, because nobody else can. The asker
               is waiting on that seat for one of two reasons and both get their answer
               here: it has not heard the goodbye, so the goodbye goes again, with the turn;
               or it has, and is missing one of that seat's turns below the named one, which
               the host holds (see s_keep_pkt) and sends again as it first arrived. Either
               way the asker goes on waiting until the host's word reaches it, rather than
               letting the turn through on its own evidence. */
            if (s_is_host && src > 0) {
                unsigned gone = 0;
                int g;
                for (g = 0; g < NM_MAX_SEATS && g < s_seats; g++) {
                    if (g == who || g == s_seat || !(mask & (1u << (unsigned)g))) continue;
                    if (s_left[g] && s_left_turn[g] != LS_TURN_NONE) gone |= 1u << (unsigned)g;
                }
                if (gone && nm_need_gone_may_answer(who)) {
                    for (g = 0; g < NM_MAX_SEATS; g++) {
                        int kp = -1;
                        if (!(gone & (1u << (unsigned)g))) continue;
                        nm_bye_to(who, g, s_left_turn[g]);
                        if (turn < s_left_turn[g]) {
                            kp = nm_kept_for(g, turn, s_left_turn[g]);
                            if (kp >= 0) nm_send_seat(who, s_keep_pkt[g][kp], s_keep_len[g][kp]);
                        }
                        if (s_gone_answer_said_ms == 0
                            || nm_now_ms() - s_gone_answer_said_ms > 1000u) {
                            s_gone_answer_said_ms = nm_now_ms();
                            printf("NET|answered-for-departed|seat=%d|for=%d|turn=%u|left-turn=%u|%s\n",
                                   g, who, turn, s_left_turn[g],
                                   turn >= s_left_turn[g] ? "goodbye"
                                   : (kp >= 0 ? "goodbye+turns" : "goodbye, turn not kept"));
                            fflush(stdout);
                        }
                    }
                }
            }
            /* THE MASK IS THE ONE FIELD HERE THAT MULTIPLIES, so it is the one field that
               is cut down to what it is allowed to name. It arrived off the wire, and every
               bit set in it is a seat that answers with up to a kilobyte and, in a star,
               a host that relays each of those answers on to everyone else. Unchecked, one
               sixteen byte datagram repeated four times a second turns into tens of
               kilobytes a second through the relay that was already the lossy leg. Kept to
               the seats actually in this match, never the asker's own, never this peer's
               idea of who is not holding anything up. */
            mask &= nm_seated_bits();
            mask &= ~(1u << (unsigned)who);
            if (!mask) continue;
            /* AND IT IS ANSWERED NO FASTER THAN IT IS ASKED. The asking beat is throttled
               to NM_RESEND_MS at the asker; nothing throttled the reply, so a seat that
               chose to ask twenty times a beat was answered twenty times, with up to a
               kilobyte each. One answer per asking seat per beat now, which is the rate
               the asker is allowed to ask at. Not keyed on the turn as well, on purpose:
               an answer can be lost, and a peer that has to ask again for the same turn
               must be able to get it again. */
            if (s_seat >= 0 && s_seat < NM_MAX_SEATS && (mask & (1u << s_seat))
                && nm_need_may_answer(who)) {
                unsigned char rep[LS_PACKET_MAX];
                const int len = ls_pack_from(&s_ls, turn, rep, (int)sizeof rep);
                if (len > 12) {
                    nm_send_seat(s_is_host ? who : 0, rep, len);
                    s_turns_answered++;
                    /* SAID, AT MOST ONCE A SECOND, and for the same reason the question is:
                       until now the asking half left a line in the log and the answering
                       half left none, so a stall that repaired itself could not be told
                       from a stall nobody answered. */
                    if (s_last_answer_said_ms == 0
                        || nm_now_ms() - s_last_answer_said_ms > 1000u) {
                        s_last_answer_said_ms = nm_now_ms();
                        printf("NET|turn-answered|seat=%d|turn=%u|answered=%u\n",
                               who, turn, s_turns_answered);
                        fflush(stdout);
                    }
                } else if (len < 0) {
                    printf("NET|turn-answer-failed|seat=%d|turn=%u|rc=%d\n", who, turn, len);
                    fflush(stdout);
                }
            }
            /* THE HOST PASSES IT ON, to the joiners it names and no others: the one that
               asked already knows, and a joiner that is not holding anybody up has nothing
               to send. SIXTEEN BYTES, freshly built, not the datagram as it arrived: the
               handler admits anything at or above sixteen and reads nothing past it, so
               forwarding `n` let a seated peer make the host repeat a kilobyte to every
               other joiner for a question that is sixteen bytes long. The same throttle
               applies, so one peer cannot drive the other seven. */
            if (s_is_host && nm_need_may_forward(who)) {
                unsigned char fwd[16];
                int j;
                put_u32(fwd, NM_NEED);
                put_u32(fwd + 4, (unsigned)who);
                put_u32(fwd + 8, turn);
                put_u32(fwd + 12, mask);
                for (j = 1; j < NM_MAX_SEATS; j++)
                    if (j != src && (mask & (1u << j))) nm_send_seat(j, fwd, (int)sizeof fwd);
            }
        } else if (magic == NM_CHAT && n >= 12) {
            /* CHAT DURING THE MATCH, which this socket carried in the lobby and dropped
               the moment play began: the two NM_CHAT handlers live in the lobby drains,
               and nm_lobby_poll returns as soon as the room has started, so a line sent
               in game fell off this chain and was discarded in silence.

               It is NOT an order and must never become one. It carries no turn, it is
               not in the world hash, and it cannot stall the barrier: two peers whose
               chat differs are still playing the same game. That is why it is handled
               here, beside the goodbye, rather than anywhere near ls_local_order.

               The length is checked twice before a byte is copied, against the ring and
               against the datagram, exactly as both lobby handlers do. This is the only
               variable length message on the socket. */
            const unsigned who = get_u32(in + 4);
            const unsigned len = get_u32(in + 8);
            if (len < (unsigned)NM_CHAT_MAX && (int)len <= n - 12) {
                if (s_is_host) {
                    /* THE SEAT MUST MATCH THE ADDRESS IT CAME FROM. The host knows both,
                       so nobody can speak as somebody else; then it relays, because in a
                       star the joiners hear each other only through here. */
                    if ((int)who == src && src > 0) {
                        nm_chat_push(src, (const char*)in + 12, (int)len);
                        nm_relay(src, in, n);
                    }
                } else if (who < (unsigned)NM_MAX_SEATS && (int)who != s_seat) {
                    /* On a joiner every relayed packet wears the HOST's address, so the
                       seat is the packet's and not the sender's. A peer's own line was
                       already pushed by nm_chat_say and must not appear twice. */
                    nm_chat_push((int)who, (const char*)in + 12, (int)len);
                }
            }
        }
        /* Late HELLO, WELCOME, READY and SACK repeats share this socket. A late HELLO is
           answered nowhere: the match has started and a newcomer is Phase 5's problem. */
    }
    {
        int i;
        unsigned now = nm_now_ms();
        for (i = 0; i < NM_MAX_SEATS; i++) {
            if (i == s_seat || !s_have[i] || s_left[i]) continue;
            if (now - s_last_heard_ms[i] > NM_SILENCE_MS) {
                printf("NET|peer-silent|seat=%d|seconds=%d|turn=%u\n", i,
                       NM_SILENCE_MS / 1000, ls_exec_turn(&s_ls));
                fflush(stdout);
                s_left[i] = 1;
                if (s_is_host && s_left_turn[i] == LS_TURN_NONE)
                    s_left_turn[i] = ls_seat_first_missing(&s_ls, i);
                nm_seat_absent_at(i, s_left_turn[i] != LS_TURN_NONE
                                     ? s_left_turn[i]
                                     : ls_seat_first_missing(&s_ls, i));
                /* AND THE HOST HAS TO SAY SO, because in a star it is the only peer that
                   can tell. A joiner hears every other joiner only through the relay, so
                   a joiner that dies without a goodbye simply stops being forwarded, and
                   the others would wait on a seat that will never report again: the
                   scheduler needs every seat to speak before a turn may run, so they
                   would hang rather than fail, which is the worst way to end. The host
                   sends the goodbye that peer did not get to send.
                   WITH THE TURN ON IT, twelve bytes and not eight. The host worked the
                   number out on the line above and then sent a goodbye that did not carry
                   it, so every joiner fell back to a number of its own and the seats went
                   quiet on different turns in different worlds. */
                if (s_is_host) {
                    int k;
                    for (k = 0; k < NM_MAX_SEATS; k++) {
                        if (k != s_seat && k != i) nm_bye_to(k, i, s_left_turn[i]);
                    }
                }
            }
        }
    }
}

/* I RESIGN. On the host that stamps its own turn directly; on a joiner it asks the host
   to name one, which comes back relayed to everybody, including back to here. */
int nm_surrender(void)
{
    unsigned char pkt[12];
    int k;
    if (!s_active || s_seat < 0) return 0;
    if (s_surr_turn[s_seat] != LS_TURN_NONE) return 1;   /* already asked */
    if (s_is_host) {
        s_surr_turn[s_seat] = ls_send_turn(&s_ls) + ls_ahead(&s_ls) + 1;
        put_u32(pkt, NM_SURR);
        put_u32(pkt + 4, (unsigned)s_seat);
        put_u32(pkt + 8, s_surr_turn[s_seat]);
        for (k = 0; k < 3; k++) nm_send_others(pkt, 12);
    } else {
        put_u32(pkt, NM_SURR);
        put_u32(pkt + 4, (unsigned)s_seat);
        for (k = 0; k < 3; k++) nm_send_seat(0, pkt, 8);
    }
    printf("NET|surrender|seat=%d|turn=%u\n", s_seat, s_surr_turn[s_seat]);
    fflush(stdout);
    return 1;
}

/* WHOSE SURRENDER LANDS ON THIS TURN? Unlike a departure this DOES answer for our own
   seat: the player who resigned watches their own base go up on the same frame everybody
   else does. Answers once per seat. */
int nm_surr_due(void)
{
    int i;
    const unsigned now = ls_exec_turn(&s_ls);
    for (i = 0; i < NM_MAX_SEATS; i++) {
        if (s_surr_done[i]) continue;
        if (s_surr_turn[i] == LS_TURN_NONE || now < s_surr_turn[i]) continue;
        s_surr_done[i] = 1;
        printf("NET|surrendered|seat=%d|turn=%u\n", i, now);
        fflush(stdout);
        return i;
    }
    return -1;
}

/* HAS A SEAT'S DEPARTURE COME DUE? Only once the turn the host named has RUN, never on
   the turn before it, and that one turn is the whole difference between a departure that
   lands everywhere at once and one that does not.
   This is asked once, straight after each turn runs. Asked after turn N-1 about a
   departure named for turn N, it depended on the goodbye having arrived before turn N-1
   ran, and nothing made that so: N-1 needs only turns the departing seat did send, so a
   survivor whose goodbye came in a moment late ran N-1, found nothing due, advanced the
   world, and applied the departure one tick after everybody else. Two G210 runs in twenty
   on an idle machine did exactly that, the host applying it on turn 208 and the survivor
   on 209, and the hashes parted two frames later.
   Asked after turn N instead, the goodbye is guaranteed to be here. Turn N cannot run
   without that seat's report for it or the host's goodbye, and nobody holds the report:
   the host named N because it never received one, and it stops relaying the seat once
   it has named it. So every peer that gets far enough to ask already knows. */
int nm_left_due(void)
{
    int i;
    const unsigned now = ls_exec_turn(&s_ls);
    for (i = 0; i < NM_MAX_SEATS; i++) {
        if (i == s_seat || !s_left[i] || s_left_done[i]) continue;
        if (s_left_turn[i] == LS_TURN_NONE || now <= s_left_turn[i]) continue;
        s_left_done[i] = 1;
        printf("NET|peer-gone|seat=%d|turn=%u\n", i, now);
        fflush(stdout);
        return i;
    }
    return -1;
}

static int nm_any_left(void)
{
    int i;
    for (i = 0; i < NM_MAX_SEATS; i++) {
        if (i != s_seat && s_have[i] && s_left[i]) return 1;
    }
    return 0;
}

int nm_begin_turn(void)
{
    unsigned char events[64 * 64];
    int n, i, len;
    if (!s_active || s_fatal) return 0;
    n = 0;
    if (s_drain) {
        /* ONLY AS MANY ORDERS AS ONE TURN CAN PUT ON THE WIRE, and the rest stay in the
           brain's queue for the next turn. A turn is one block in one datagram and cannot
           be split, so about forty three orders of twenty two bytes is all a turn holds;
           the engine emits one order PER SELECTED OBJECT, so a single right click with a
           big selection goes straight past that. Taking them all and letting the scheduler
           refuse the overflow would end the match on a click any player can make. Spilling
           costs the overflow one turn, which is 67 ms at the engine's rate and invisible
           beside the lookahead it is already waiting out. The brain stamps each order as
           it hands it over, so the ones that wait are stamped for the later turn and
           execute on every peer alike. */
        int room = ls_turn_room(&s_ls, s_event_size);
        if (room > 64) room = 64;   /* and no more than `events` holds */
        /* THE NUMBER THE SCHEDULER IS ACTUALLY USING, not the one this file was
           compiled with. The engine stamps each order with the frame it expects to run
           on, computed from what it is told here, and the scheduler then holds that order
           for ls_ahead turns. Hand it the constant while the room has agreed on more and
           every order arrives describing a frame that has already passed, which the
           engine refuses -- so on any link slow enough to raise the lookahead, which is
           every relayed one, no order a player gives can ever execute. */
        n = room > 0 ? s_drain(s_user, events, room, ls_ahead(&s_ls)) : 0;
        if (n < 0) {
            printf("NET|error=the brain refused to drain its orders (%d): is lockstep on?\n", n);
            fflush(stdout);
            s_fatal = 1;
            return 0;
        }
        if (room > 0 && n == room) {
            /* SAID WHEN THE TURN IS FILLED TO ITS WIRE LIMIT, so a burst that arrives a
               beat late is a line in the log rather than a mystery. Not an error: whatever
               is still queued goes out on the next turn. */
            printf("NET|orders-capped|turn=%u|sent=%d\n", ls_send_turn(&s_ls), n);
            fflush(stdout);
        }
        for (i = 0; i < n; i++) {
            int rc = ls_local_order(&s_ls, events + (size_t)i * (size_t)s_event_size, s_event_size);
            if (rc != LS_OK) {
                printf("NET|error=order queue overflow on turn %u (%d orders this turn); the match cannot continue\n",
                       ls_send_turn(&s_ls), n);
                fflush(stdout);
                s_fatal = 1;
                return 0;
            }
        }
    }
    len = ls_pack(&s_ls, s_last_pkt, (int)sizeof s_last_pkt);
    if (len <= 12) {
        /* A PACKET WITHOUT THE TURN IN IT IS NOT A PACKET. A negative is an error code and
           a bare twelve byte header is a packet carrying no turns at all, which every peer
           accepts as valid and learns nothing from; either way the match cannot go on and
           says so rather than stalling until the thirty second timer. */
        printf("NET|error=the turn could not be packed (%d) on turn %u; the match cannot continue\n",
               len, ls_send_turn(&s_ls));
        fflush(stdout);
        s_fatal = 1;
        return 0;
    }
    s_last_pkt_len = len;
    /* TO EVERY OTHER SEAT. On a joiner that is the host alone, which then relays it;
       on the host it is every joiner directly. */
    nm_send_others(s_last_pkt, len);
    s_last_send_ms = nm_now_ms();
    return 1;
}

/* THE SOCKET, SERVICED WITHOUT ADVANCING A TURN. nm_pump is otherwise reachable only
   through nm_turn_ready, which the live loop calls inside its own pause gate, so a peer
   sitting in the options dialog drains nothing: a line sent to it waits in the kernel
   buffer and that player looks silent to everybody else until play resumes. Safe at
   frame rate, because nothing in the pump runs a turn. */
void nm_service(void)
{
    if (s_active && s_sock) nm_pump();
}

/* ASK FOR THE TURN THIS PEER IS STUCK ON, BY NUMBER.
 *
 * Re-sending the last packet, below, repairs a short loss and nothing longer. A peer that
 * is still hearing its partner goes on executing while the partner hears nothing, and
 * every turn it executes moves the window of turns it sends further past the one the
 * partner is waiting for: with a lookahead of A it can get 2A + 1 turns ahead of it,
 * which is outside the six-turn window at every lookahead a room may choose. Once all six
 * packets that carried a turn are lost, nothing it sends afterwards carries that turn
 * again, and both peers wait on each other for ever. A turn cannot be recovered by
 * carrying more turns per packet: the lead grows with the lookahead, and the packet is
 * bounded by the relay.
 *
 * So the stuck peer names the turn, and whoever it is waiting for answers from their own
 * copy of it, which the scheduler now keeps after executing it.
 *
 * WHAT IS ACTUALLY TESTED, which is less than this used to claim. The scheduler gate runs
 * a forty-step outage between two peers at lookaheads 3, 8 and 12, one way and both ways,
 * and asserts both that the request recovers it and that the same outage stalls without
 * the request; and it runs a star of three where everything to and from one seat is cut,
 * routed through a stand-in for the host's relay, which is the shape this file implements.
 * There is no timing model in any of it: delivery is instant and counted in steps, so no
 * leg of it corresponds to a number of milliseconds. What no gate covers is the message on
 * a real socket.
 *
 * Sent on the same beat as the re-send, so only while actually stuck. */
static void nm_ask_for_turn(void)
{
    unsigned char p[16];
    const unsigned mask = ls_waiting_mask(&s_ls);
    if (!mask || s_seat < 0) return;
    put_u32(p, NM_NEED);
    put_u32(p + 4, (unsigned)s_seat);
    put_u32(p + 8, ls_exec_turn(&s_ls));
    put_u32(p + 12, mask);
    nm_send_others(p, (int)sizeof p);
    s_turns_asked++;
    /* SAID, AT MOST ONCE A SECOND: a stall that recovers leaves no other trace, and the
       leaving line is not printed on every way out of a match. */
    {
        static unsigned lastSaidMs = 0;
        if (lastSaidMs == 0 || nm_now_ms() - lastSaidMs > 1000u) {
            lastSaidMs = nm_now_ms();
            printf("NET|turn-asked|turn=%u|waiting=%02X|asked=%u\n", ls_exec_turn(&s_ls),
                   mask, s_turns_asked);
            fflush(stdout);
        }
    }
}

int nm_turn_ready(void)
{
    if (!s_active || s_fatal) return 0;
    nm_pump();
    if (ls_turn_ready(&s_ls)) return 1;
    /* Both peers' packets for one turn can be lost together, and then each waits for the
       other for ever, because a peer only speaks when it advances. The last packet is
       repeated while waiting, which the receiver's idempotent store makes free, and the
       turn this peer is stuck on is asked for by number, which is what repairs a loss
       longer than the packet's window.
       A TURN HELD UP BY A SEAT THAT HAS LEFT IS ASKED FOR THE SAME WAY, and it is never
       let through on a timer. The host answers for that seat (see the NM_NEED handler),
       because the host holds every turn below the one it named. A peer that let the turn
       through instead would run fewer of that seat's orders than a peer that had them. */
    if (s_last_pkt_len > 0 && nm_now_ms() - s_last_send_ms > NM_RESEND_MS) {
        nm_send_others(s_last_pkt, s_last_pkt_len);
        nm_ask_for_turn();
        s_last_send_ms = nm_now_ms();
    }
    return 0;
}

int nm_wait_turn(int timeout_ms)
{
    unsigned start = nm_now_ms();
    for (;;) {
        if (nm_turn_ready()) return 1;
        if (s_fatal) return 0;
        if (nm_now_ms() - start > (unsigned)timeout_ms) return 0;
        nm_nap(1);
    }
}

static void nm_sink(void* user, int seat, const void* bytes, int len)
{
    (void)user;
    (void)seat;
    if (len != s_event_size) {
        printf("NET|error=an order of %d bytes arrived where the wire unit is %d\n", len, s_event_size);
        fflush(stdout);
        s_fatal = 1;
        return;
    }
    if (s_post) {
        const int rc = s_post(s_user, bytes);
        if (rc != 1) {
            /* THE CODE IS THE DIAGNOSIS AND IT USED TO BE THROWN AWAY. The engine says
               WHY it refused -- a stamp for a frame that has already gone reads -3, a
               house index it cannot resolve reads -4 -- and printing only "refused" cost
               a whole internet match to work out from first principles. */
            printf("NET|error=the brain refused a posted order on turn %u from seat %d "
                   "(code %d%s)\n", ls_exec_turn(&s_ls), seat, rc,
                   rc == -3 ? ": the frame it was stamped for has already passed" : "");
            fflush(stdout);
            s_fatal = 1;
        }
    }
}

int nm_run_turn(void)
{
    if (!s_active || s_fatal) return 0;
    ls_run_turn(&s_ls, nm_sink, NULL);
    return s_fatal ? 0 : 1;
}

/* ------------------------------------------------------------------- the alarm ----- */

void nm_note_hash(unsigned frame, unsigned hash)
{
    unsigned slot = frame % NM_HASH_RING;
    if (!s_active) return;
    s_my_hash[slot] = hash;
    s_my_hash_frame[slot] = frame;
    {
        int i;
        for (i = 0; i < NM_MAX_SEATS; i++) {
            if (i == s_seat) continue;
            if (s_peer_hash_frame[i][slot] != frame) continue;
            if (s_peer_hash[i][slot] != hash && !s_desynced) {
                s_desynced = 1;
                s_desync_frame = frame;
                printf("NETDESYNC|frame=%u|seat=%d|mine=%08X|peer=%08X|abi=%08X|scen=%08X\n",
                       frame, i, hash, s_peer_hash[i][slot], s_abi, s_scen);
                fflush(stdout);
            } else if (s_peer_hash[i][slot] == hash) {
                s_synced_frames++;
            }
        }
    }
    if ((frame % NM_SYNC_EVERY) == 0) {
        unsigned char p[16];
        put_u32(p, NM_SYNC);
        put_u32(p + 4, (unsigned)s_seat);
        put_u32(p + 8, frame);
        put_u32(p + 12, hash);
        nm_send_others(p, 16);
        printf("NETSYNC|frame=%u|hash=%08X\n", frame, hash);
        fflush(stdout);
    }
}

int nm_desynced(void) { return s_desynced; }
unsigned nm_desync_frame(void) { return s_desync_frame; }
int nm_peer_left(void) { return nm_any_left(); }
unsigned nm_turns_run(void) { return ls_exec_turn(&s_ls); }

void nm_shutdown(void)
{
    if (!s_active) return;
    if (s_sock) {
        unsigned char bye[8];
        int k;
        put_u32(bye, NM_BYE);
        put_u32(bye + 4, (unsigned)s_seat);
        for (k = 0; k < 3; k++) nm_send_others(bye, 8);
        nm_say_leaving();
        net_close(s_sock);
        s_sock = NULL;
        net_shutdown();
    }
    s_active = 0;
    s_seat = -1;
    s_seats = 0;
    s_humans = 0;
    s_is_host = 0;
    memset(s_have, 0, sizeof s_have);
}

/* ========================================================== THE PROBE, BROWSING ====== *
 *  The asker's half. netmatch.h has the wire, the two key forms, what a relayed probe
 *  costs the relay, and why a direct one is only ever the caller's decision.
 *
 *  NONE OF THIS TOUCHES THE MATCH. It keeps its own targets and its own sockets, so a
 *  browser can probe while this machine is hosting, joining or doing neither, and a
 *  probe that goes wrong cannot take a room down with it.
 * ==================================================================================== */

/* THE PROBE'S OWN CLOCK. A round trip to a room behind a nearby relay is a few dozen
   milliseconds, and GetTickCount, which the rest of this file reads on Windows, moves in
   steps of 10 to 16 ms, so every probe would be rounded to whichever step it straddled.
   The performance counter has no such step. Only the asker ever reads these stamps (the
   host sends them back untouched), so which clock makes them is this machine's business
   alone. */
#ifdef _WIN32
static unsigned nm_probe_ms(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (freq.QuadPart == 0 && !QueryPerformanceFrequency(&freq)) freq.QuadPart = 0;
    if (freq.QuadPart == 0 || !QueryPerformanceCounter(&now)) return (unsigned)GetTickCount();
    return (unsigned)((now.QuadPart / freq.QuadPart) * 1000
                      + ((now.QuadPart % freq.QuadPart) * 1000) / freq.QuadPart);
}
#else
/* ON macOS AND LINUX, THE MONOTONIC CLOCK, not the wall clock the rest of this file reads.
   The wall clock steps whenever the system sets it, on a time sync or a wake from sleep.
   Timed on it, a step of 200 ms back inside a 40 ms round trip works out as minus 160 ms,
   one of three seconds forward as 999 MS, and a step back holds the next probe until the
   clock has made the time up again. The monotonic clock does not step. */
static unsigned nm_probe_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return nm_now_ms();
    return (unsigned)((unsigned long)ts.tv_sec * 1000ul + (unsigned long)ts.tv_nsec / 1000000ul);
}
#endif

/* WHEN THE ANSWER JUST READ ARRIVED, on nm_probe_ms's clock. The system stamps a datagram's
   arrival on the WALL clock, so the stamp is turned into an age (the wall clock now, less
   the stamp) and the age is taken off the monotonic clock now. Only the moments between
   the arrival and this read are on the wall clock, and an age below zero or over a second
   says the clock stepped inside them; the answer is then timed as read, as it is wherever
   there is no stamp (Windows, here), and the reading carries the time since the last poll. */
static unsigned probe_arrival(const NetSock* s)
{
#ifndef _WIN32
    long sec, usec;
    if (net_last_rx_time(s, &sec, &usec)) {
        struct timeval tv;
        long age_us;
        unsigned mono;
        gettimeofday(&tv, NULL);          /* first, so the age can only come out short */
        mono = nm_probe_ms();
        age_us = ((long)tv.tv_sec - sec) * 1000000L + ((long)tv.tv_usec - usec);
        if (age_us >= 0L && age_us <= 1000000L) return mono - (unsigned)(age_us / 1000L);
        return mono;
    }
#else
    (void)s;
#endif
    return nm_probe_ms();
}

/* The game list's thirty-two rows with room to spare, so a REFRESH that brings back a
   different set of rooms never has to wait for a slot. */
#define NM_PROBE_TARGETS 48

typedef struct {
    char     key[NM_PROBE_KEY_MAX];
    int      state;                     /* NM_PROBE_*; UNKNOWN is a free slot           */
    int      relayed;
    int      launched;                  /* has had its turn to start sending            */
    NetAddr  addr;
    unsigned nonce;                     /* probe i carries nonce + i                    */
    unsigned sent_at[NM_PROBE_COUNT];   /* this machine's stamp, as each probe carried it */
    unsigned mask;                      /* which probes have been answered              */
    unsigned next_ms;                   /* when the next probe is due                   */
    unsigned seq;                       /* start order: who launches first, who is evicted */
    int      sent, answers, best;
    unsigned version;
} NmProbeTarget;

static NmProbeTarget s_probe[NM_PROBE_TARGETS];
static NetSock* s_probe_tun = NULL;     /* the ONE relay registration, for every "#CODE" */
static NetSock* s_probe_udp = NULL;     /* every direct target                           */
static int s_probe_net = 0;             /* this half holds a net_startup of its own      */
/* A RELAY THAT WOULD NOT OPEN IS NOT RETRIED EVERY FRAME: opening one resolves its name,
   and a name lookup can hold a frame. Tried again after a clear or a new relay. */
static int s_probe_tun_failed = 0;
static char s_probe_relay[128] = NM_RELAY_HOST;
static unsigned short s_probe_relay_port = NM_RELAY_PORT;
static unsigned s_probe_seq = 0;
static unsigned s_probe_pumped_ms = 0;
static int s_probe_pumped = 0;

/* THE KEY A TARGET IS FILED UNDER. A room code is read loosely (either case, I and L for 1,
   O for 0), so one room can be named by several spellings of its code, and each would
   otherwise be a target of its own, asking the same room once per spelling. Every
   spelling is filed as the one the room itself prints. Anything else is filed as given. */
static const char* probe_canon(const char* key, char* buf)
{
    unsigned long id;
    if (key && key[0] == '#' && rc_decode(key, &id) && rc_encode(id, buf)) return buf;
    return key;
}

static NmProbeTarget* probe_find(const char* key)
{
    int i;
    if (!key || !*key) return NULL;
    for (i = 0; i < NM_PROBE_TARGETS; i++) {
        if (s_probe[i].state != NM_PROBE_UNKNOWN && !strcmp(s_probe[i].key, key))
            return &s_probe[i];
    }
    return NULL;
}

/* A free slot, or else the oldest target that has finished. A target still probing is
   never thrown out: some row on the screen is waiting for it. */
static NmProbeTarget* probe_slot(void)
{
    NmProbeTarget* oldest = NULL;
    int i;
    for (i = 0; i < NM_PROBE_TARGETS; i++) {
        NmProbeTarget* t = &s_probe[i];
        if (t->state == NM_PROBE_UNKNOWN) return t;
        if (t->state != NM_PROBE_PROBING && (!oldest || t->seq < oldest->seq)) oldest = t;
    }
    return oldest;
}

/* "a.b.c.d:port" and nothing else: four decimal octets and a port of 1..65535. A NAME IS
   REFUSED RATHER THAN RESOLVED, because resolving one blocks, and this is called from a
   screen that is drawing. The game list only ever serves numeric addresses. */
static int probe_parse_direct(const char* key, char* ip, int ipmax, unsigned short* port)
{
    const char* colon = strrchr(key, ':');
    int i, parts = 0, digits = 0, val = 0;
    long p = 0;
    if (!colon || colon == key || !colon[1]) return 0;
    if ((int)(colon - key) >= ipmax) return 0;
    for (i = 0; key + i < colon; i++) {
        const char c = key[i];
        if (c >= '0' && c <= '9') {
            val = val * 10 + (c - '0');
            if (++digits > 3 || val > 255) return 0;
        } else if (c == '.' && digits > 0) {
            parts++;
            digits = 0;
            val = 0;
        } else {
            return 0;
        }
    }
    if (parts != 3 || digits == 0) return 0;
    for (i = 1; colon[i]; i++) {
        if (colon[i] < '0' || colon[i] > '9' || i > 5) return 0;
        p = p * 10 + (colon[i] - '0');
    }
    if (p < 1 || p > 65535) return 0;
    memcpy(ip, key, (size_t)(colon - key));
    ip[colon - key] = '\0';
    *port = (unsigned short)p;
    return 1;
}

/* Keys are written into the log, so a key with a byte the log cannot show is refused. */
static int probe_key_printable(const char* key)
{
    for (; *key; key++) {
        if ((unsigned char)*key < 33 || (unsigned char)*key > 126) return 0;
    }
    return 1;
}

static NetSock* probe_sock(int relayed)
{
    if (!s_probe_net) {
        if (net_startup() != 0) return NULL;
        s_probe_net = 1;
    }
    if (!relayed) {
        if (!s_probe_udp) {
            s_probe_udp = net_open(0);
            if (s_probe_udp) net_set_rx_stamps(s_probe_udp, 1);   /* see probe_drain */
        }
        return s_probe_udp;
    }
    if (!s_probe_tun && !s_probe_tun_failed) {
        /* ANY id, never written down: it only has to differ from the rooms' and be
           something the relay accepts. Drawn once per socket, because the relay ties an id
           to the address it first came from and refuses it from anywhere else. */
        const unsigned long id = rc_draw_peer_id();
        s_probe_tun = id ? net_open_tunnel(s_probe_relay, s_probe_relay_port, id) : NULL;
        if (s_probe_tun) {
            net_set_rx_stamps(s_probe_tun, 1);                        /* see probe_drain */
            printf("NET|probe-relay|open|via=%s:%u\n", s_probe_relay,
                   (unsigned)s_probe_relay_port);
        } else {
            s_probe_tun_failed = 1;
            printf("NET|probe-relay|could not open|via=%s:%u\n", s_probe_relay,
                   (unsigned)s_probe_relay_port);
        }
        fflush(stdout);
    }
    return s_probe_tun;
}

/* One line per finished target. A browse is at most a few dozen of them, on a REFRESH a
   person pressed, and "why does that row say --" is answered by exactly this line. */
static void probe_finish(NmProbeTarget* t, int state)
{
    t->state = state;
    if (state == NM_PROBE_ANSWERED)
        printf("NET|probe|key=%s|rtt=%dms|answers=%d/%d|version=%u\n", t->key, t->best,
               t->answers, t->sent, t->version);
    else
        printf("NET|probe|key=%s|no-answer|sent=%d\n", t->key, t->sent);
    fflush(stdout);
}

static void probe_drain(NetSock* s, int relayed)
{
    unsigned char in[64];
    NetAddr from;
    int n, i;
    while ((n = net_recv(s, &from, in, (int)sizeof in)) > 0) {
        const unsigned now = probe_arrival(s);
        if (n < NM_PROBE_BYTES || get_u32(in) != NM_PROBEACK_WORD) continue;
        for (i = 0; i < NM_PROBE_TARGETS; i++) {
            NmProbeTarget* t = &s_probe[i];
            unsigned idx;
            int rtt;
            if (t->state != NM_PROBE_PROBING || !t->launched || t->relayed != relayed) continue;
            if (!net_addr_equal(&from, &t->addr)) continue;
            /* THE NONCE SAYS WHICH PROBE, AND THE STAMP MUST BE THE ONE IT CARRIED. An
               answer that fails either is somebody else's, a copy, or a round that is
               over, and none of those is a round trip. Two keys can still name one address
               (an octet written with leading zeros), which is why a mismatch moves on to
               the next target instead of dropping the datagram. */
            idx = get_u32(in + 4) - t->nonce;
            if (idx >= (unsigned)t->sent || (t->mask & (1u << idx))) continue;
            if (get_u32(in + 8) != t->sent_at[idx]) continue;
            rtt = (int)(now - t->sent_at[idx]);
            /* A ROUND TRIP THE SCHEDULE COULD NOT HAVE PRODUCED IS NOT ONE, and is dropped
               rather than shown. Two milliseconds under zero is the rounding of three
               millisecond readings on loopback, and reads as zero. */
            if (rtt < 0 && rtt >= -2) rtt = 0;
            if (rtt < 0 || rtt > NM_PROBE_SPAN_MS + NM_PROBE_WAIT_MS) break;
            t->mask |= 1u << idx;
            t->answers++;
            if (t->best < 0 || rtt < t->best) t->best = rtt;
            t->version = get_u32(in + 12);
            if (t->answers >= NM_PROBE_COUNT) probe_finish(t, NM_PROBE_ANSWERED);
            break;
        }
    }
}

void nm_probe_pump(void)
{
    unsigned now = nm_probe_ms();
    int i, busy_tun = 0, busy_udp = 0;
    s_probe_pumped = 1;
    s_probe_pumped_ms = now;

    for (i = 0; i < NM_PROBE_TARGETS; i++) {
        const NmProbeTarget* t = &s_probe[i];
        if (t->state != NM_PROBE_PROBING || !t->launched) continue;
        if (t->relayed) busy_tun = 1;
        else busy_udp = 1;
    }
    /* A SOCKET IS READ ONLY WHILE SOMETHING ON IT IS BEING ASKED. Reading a relayed socket
       is what keeps its registration alive (net_recv sends the keepalive), so a browser
       that has its answers stops holding a relay slot it no longer needs. The next probe
       registers again from the same socket, which the relay accepts from an address it
       already knows. */
    if (busy_tun && s_probe_tun) probe_drain(s_probe_tun, 1);
    if (busy_udp && s_probe_udp) probe_drain(s_probe_udp, 0);
    now = nm_probe_ms();

    /* FINISHED: every probe is out and the wait after the last one is up. */
    for (i = 0; i < NM_PROBE_TARGETS; i++) {
        NmProbeTarget* t = &s_probe[i];
        if (t->state != NM_PROBE_PROBING || !t->launched || t->sent < NM_PROBE_COUNT) continue;
        if (now - t->sent_at[NM_PROBE_COUNT - 1] >= (unsigned)NM_PROBE_WAIT_MS)
            probe_finish(t, t->answers ? NM_PROBE_ANSWERED : NM_PROBE_SILENT);
    }

    /* LAUNCHED IN THE ORDER THEY WERE STARTED, a few at a time. A REFRESH starts every row
       at once, and thirty-two rooms asked in the same instant is a burst on this player's
       own uplink that would inflate the very numbers it is measuring. */
    for (;;) {
        NmProbeTarget* next = NULL;
        int running = 0;
        for (i = 0; i < NM_PROBE_TARGETS; i++) {
            NmProbeTarget* t = &s_probe[i];
            if (t->state != NM_PROBE_PROBING) continue;
            if (t->launched) running++;
            else if (!next || t->seq < next->seq) next = t;
        }
        if (!next || running >= NM_PROBE_RUNNING_MAX) break;
        next->launched = 1;
        next->next_ms = now;
        if (!probe_sock(next->relayed)) probe_finish(next, NM_PROBE_SILENT);  /* sent reads 0 */
    }

    /* SENT: one probe per target per pump at most, so a caller that stalls for a moment
       resumes the spacing rather than catching up in a burst. */
    for (i = 0; i < NM_PROBE_TARGETS; i++) {
        NmProbeTarget* t = &s_probe[i];
        NetSock* sock;
        unsigned char p[NM_PROBE_BYTES];
        unsigned stamp;
        if (t->state != NM_PROBE_PROBING || !t->launched || t->sent >= NM_PROBE_COUNT) continue;
        if (nm_probe_ms() - t->next_ms >= 0x80000000u) continue;          /* not due yet */
        sock = t->relayed ? s_probe_tun : s_probe_udp;
        if (!sock) {
            probe_finish(t, NM_PROBE_SILENT);
            continue;
        }
        stamp = nm_probe_ms();
        put_u32(p, NM_PROBE_WORD);
        put_u32(p + 4, t->nonce + (unsigned)t->sent);
        put_u32(p + 8, stamp);
        put_u32(p + 12, 0u);
        net_send(sock, &t->addr, p, NM_PROBE_BYTES);
        t->sent_at[t->sent++] = stamp;
        t->next_ms = stamp + (unsigned)NM_PROBE_GAP_MS(t->sent - 1);
    }
}

int nm_probe_start(const char* key)
{
    NmProbeTarget* t;
    NetAddr a;
    char ip[32];
    unsigned short port = 0;
    unsigned long id = 0ul;
    int relayed = 0, valid = 0;
    char canon[RC_TEXT_MAX];
    if (!key || !*key || strlen(key) >= (size_t)NM_PROBE_KEY_MAX || !probe_key_printable(key))
        return 0;
    key = probe_canon(key, canon);
    t = probe_find(key);
    if (t) return t->state == NM_PROBE_INVALID ? 0 : 1;
    memset(&a, 0, sizeof a);
    if (key[0] == '#') {
        if (rc_decode(key, &id)) {
            net_addr_tunnel(&a, id);
            relayed = 1;
            valid = 1;
        }
    } else if (probe_parse_direct(key, ip, (int)sizeof ip, &port)
               && net_resolve(ip, port, &a) == 0) {
        valid = 1;
    }
    t = probe_slot();
    if (!t) return 0;
    memset(t, 0, sizeof *t);
    snprintf(t->key, sizeof t->key, "%s", key);
    t->seq = ++s_probe_seq;
    t->best = -1;
    if (!valid) {
        t->state = NM_PROBE_INVALID;
        printf("NET|probe|key=%s|not a room code or a numeric address:port\n", t->key);
        fflush(stdout);
        return 0;
    }
    t->state = NM_PROBE_PROBING;
    t->relayed = relayed;
    t->addr = a;
    t->nonce = (unsigned)rc_draw_peer_id();
    nm_probe_pump();        /* the first probe goes out now, not on the next frame */
    return 1;
}

int nm_probe_poll(const char* key, NmProbeResult* out)
{
    const NmProbeTarget* t;
    char canon[RC_TEXT_MAX];
    if (!s_probe_pumped || nm_probe_ms() != s_probe_pumped_ms) nm_probe_pump();
    t = probe_find(probe_canon(key, canon));
    if (out) {
        memset(out, 0, sizeof *out);
        out->rtt_ms = -1;
        if (t) {
            out->state = t->state;
            out->rtt_ms = t->best;
            out->answers = t->answers;
            out->sent = t->sent;
            out->version = t->version;
        }
    }
    return t ? t->state : NM_PROBE_UNKNOWN;
}

void nm_probe_cancel(const char* key)
{
    char canon[RC_TEXT_MAX];
    NmProbeTarget* t = probe_find(probe_canon(key, canon));
    if (t) memset(t, 0, sizeof *t);
}

void nm_probe_clear_all(void)
{
    memset(s_probe, 0, sizeof s_probe);
    s_probe_tun_failed = 0;       /* a REFRESH may try an unreachable relay again */
}

void nm_probe_shutdown(void)
{
    nm_probe_clear_all();
    if (s_probe_tun) {
        net_close(s_probe_tun);
        s_probe_tun = NULL;
        printf("NET|probe-relay|closed\n");
        fflush(stdout);
    }
    if (s_probe_udp) {
        net_close(s_probe_udp);
        s_probe_udp = NULL;
    }
    if (s_probe_net) {
        net_shutdown();
        s_probe_net = 0;
    }
}

void nm_probe_set_relay(const char* host, unsigned short port)
{
    int i;
    if (!host || !*host) host = NM_RELAY_HOST;
    if (port == 0) port = NM_RELAY_PORT;
    if (!strcmp(host, s_probe_relay) && port == s_probe_relay_port) return;
    /* A ROOM CODE MEANS NOTHING ON ANOTHER RELAY, so anything still being asked on the old
       one is dropped rather than left to time out into a false "no answer". */
    for (i = 0; i < NM_PROBE_TARGETS; i++) {
        if (s_probe[i].relayed && s_probe[i].state == NM_PROBE_PROBING)
            memset(&s_probe[i], 0, sizeof s_probe[i]);
    }
    if (s_probe_tun) {
        net_close(s_probe_tun);
        s_probe_tun = NULL;
    }
    s_probe_tun_failed = 0;
    snprintf(s_probe_relay, sizeof s_probe_relay, "%s", host);
    s_probe_relay_port = port;
}
