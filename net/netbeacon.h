/*
 * netbeacon.h -- how a game on a LAN is found, and nothing else.
 *
 * A HOST ANNOUNCES, A BROWSER LISTENS. The host broadcasts a short datagram to
 * 255.255.255.255 on a fixed port about once a second for as long as it is sitting in
 * the lobby with room in it. A browser binds that port and lists what it hears. There is
 * no service, nothing to configure, and it works on a network with no internet at all.
 *
 * WHY A BEACON AND NOT A QUERY. A browser that asked "who is out there" would need to
 * know where to ask, which is the problem it is trying to solve. Broadcasting costs one
 * small datagram a second per host and removes the question.
 *
 * LIVENESS IS THE BEACON STOPPING. A game not heard from for NB_STALE_MS leaves the
 * list. Nothing has to announce that it has gone, so a host that crashes disappears
 * correctly rather than lingering as a row that cannot be joined.
 *
 * WHAT A BEACON MUST NOT CARRY, and this is a rule rather than an observation: it is
 * broadcast in clear to an entire network, so it carries nothing that matters. No
 * password, not even a hash of one; no player names; no addresses other than the one the
 * datagram already came from. It carries what a browser needs to draw one row and decide
 * whether joining could work, and the password is checked at the join.
 *
 * BEST EFFORT, ALWAYS. A network that drops broadcast traffic, a firewall, or a platform
 * that refuses the socket option all produce an EMPTY LIST rather than an error. A caller
 * must never report "no games found" as "the network is broken", because on a quiet LAN
 * those look identical and only one of them is true.
 */
#ifndef CNC3D_NETBEACON_H
#define CNC3D_NETBEACON_H

#ifdef __cplusplus
extern "C" {
#endif

/* One below the default match port, deliberately adjacent so the pair is memorable and
   a firewall rule that opens one is obviously next to the other. */
#define NB_PORT 17420
/* Bumped whenever the beacon's layout changes. A browser lists only versions it knows,
   because a row it cannot parse is a row it cannot join, and showing it would be a
   promise the JOIN button could not keep. */
/* 2: the advert carries the map's DISPLAY NAME beside its scenario code. A
   browser could only ever print the code, so a row read SCM01EA where the map is called
   Green Acres, and a joiner who does not HAVE the host's map cannot be told its name by
   anyone but the host. */
#define NB_VERSION 2

#define NB_NAME_MAX  32   /* the game's name, as typed by the host   */
#define NB_SCEN_MAX  16   /* the scenario CODE: SCM01EA, USER07      */
/* THE MAP'S DISPLAY NAME. It travels because the code is a file name and the row wants a
   map's name, and because the joiner may not HAVE this map: a name resolved locally is
   exactly the name a joiner who needs it cannot look up. The code stays beside it as the
   fallback and as what a later "you do not have this map" check would compare. */
#define NB_MAPNAME_MAX 32
#define NB_ADDR_MAX  64   /* dotted quad, from the datagram itself   */
#define NB_MAX_GAMES 32   /* rows a browser will hold at once        */

#define NB_PERIOD_MS 1000 /* how often a host re-announces           */
#define NB_STALE_MS  4000 /* silence after which a row is dropped    */

/* One row of the browser. `addr` is where the datagram CAME FROM and is never taken from
   inside it: a host that lied about its own address would send joiners somewhere else. */
typedef struct NbGame
{
    char name[NB_NAME_MAX];
    char scenario[NB_SCEN_MAX];
    char mapname[NB_MAPNAME_MAX];   /* empty when the host advertised none */
    char addr[NB_ADDR_MAX];
    unsigned short port;      /* the MATCH port, which is not the beacon port */
    int players_now;
    int players_max;
    int has_password;         /* draw the padlock, and prompt before joining  */
    unsigned abi;             /* the brain's order-wire layout hash           */
    unsigned scen_hash;       /* the scenario's own bytes                     */
    unsigned last_heard_ms;   /* for the staleness sweep                      */
} NbGame;

/* ---- the host side ---- */
/* Open the socket a host announces from. Returns 1, or 0 if broadcasting is not
   available, which is not fatal: the game still runs and simply cannot be found. */
int  nb_announce_open(void);
/* Send one beacon now, if NB_PERIOD_MS has passed since the last. Cheap to call every
   frame; it does its own rate limiting so no caller has to keep a timer. */
void nb_announce(const char* name, const char* scenario, const char* mapname,
                 unsigned short match_port,
                 int players_now, int players_max, int has_password,
                 unsigned abi, unsigned scen_hash);
void nb_announce_close(void);

/* ---- the browser side ---- */
int  nb_browse_open(void);
/* Drain everything waiting and drop anything gone stale. Call it often; it never blocks.
   Returns the number of rows now listed. */
int  nb_browse_poll(void);
int  nb_browse_count(void);
const NbGame* nb_browse_get(int i);
void nb_browse_close(void);
/* Forget every row, for a REFRESH that should not show what is already gone. The list
   refills within NB_PERIOD_MS from every host still announcing. */
void nb_browse_clear(void);

#ifdef __cplusplus
}
#endif
#endif
