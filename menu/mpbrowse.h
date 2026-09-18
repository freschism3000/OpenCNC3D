/*
 * mpbrowse.h -- the internet game list, as the screen sees it.
 *
 * THE LAN BROWSER LISTENS; THIS ONE ASKS. A beacon cannot cross a router, so a game
 * played across the internet has to be announced somewhere both ends can reach. That
 * somewhere answers two methods on one address: a host posts a row to say its game is
 * open and keeps saying so, and a browsing player reads the list back.
 *
 * NOTHING HERE EVER BLOCKS THE FRAME. Every request runs on a worker; the screen asks
 * for a refresh, carries on drawing, and picks up rows when they arrive. That is not a
 * refinement, it is the whole reason this file exists rather than a call to the HTTP
 * client at the draw site: a lost packet to a machine on the other side of the world
 * would otherwise stop the game for as long as the network took to give up.
 *
 * A SCREEN THAT CLOSES NEVER WAITS EITHER. Leaving the browser abandons whatever is in
 * flight instead of joining it, so a reply that arrives afterwards is dropped rather
 * than written into a screen that has gone.
 *
 * THE TWO KINDS OF ROW ARE NOT COSMETIC. A relayed row carries a six-character code and
 * no address at all, and says nothing about where its host lives. A direct row carries
 * an address, which is that player's own home connection, published to everyone who
 * opens the browser. Both are listed, deliberately, and the host is told which one they
 * are choosing before they choose it.
 */

#ifndef MPBROWSE_H
#define MPBROWSE_H

#ifdef __cplusplus
extern "C" {
#endif

#define MB_MAX_ROWS   32
#define MB_NAME_MAX   32
#define MB_MAP_MAX    32
#define MB_SCEN_MAX   16
#define MB_ADDR_MAX   64
#define MB_ROOM_MAX   12   /* "#K7M-3QX" and room to grow */
#define MB_ID_MAX     33   /* 32 hex characters and a terminator */

/* One open game. The same shape the LAN beacon hands over, plus the room code, which is
   the one thing a relayed game has instead of an address. */
typedef struct MB_Row
{
    /* THE ROW'S PUBLIC ID, as the list serves it: 32 lowercase hex characters, drawn by
       the host for each game it lists. Empty when the list sent none or sent something
       that is not one. The browser holds its selection by this rather than by position,
       because two direct games behind one router share an address and a port. */
    char id[MB_ID_MAX];
    char name[MB_NAME_MAX];
    char map[MB_MAP_MAX];        /* the map's NAME when the host sent one */
    char scenario[MB_SCEN_MAX];  /* the scenario CODE */
    char addr[MB_ADDR_MAX];      /* EMPTY on a relayed row */
    char room[MB_ROOM_MAX];      /* EMPTY on a direct row */
    unsigned short port;
    int players_now;
    int players_max;
    int locked;                  /* draw the padlock and ask for the passcode */
    int relay;                   /* which of the two kinds this is */
    unsigned abi;                /* the order wire this game speaks */
    unsigned scen;               /* the scenario's own bytes */
} MB_Row;

enum { MB_IDLE = 0, MB_FETCHING, MB_READY, MB_FAILED };

/* WHERE THE LIST LIVES. There is a built-in address so the game works out of the box,
   and an override so it can be pointed at a stand-in while it is being worked on. The
   override refuses a plain-HTTP address unless it is this machine: on the public
   internet that would put a host's own address on the wire in clear, and the whole
   point of telling a direct host their address becomes public is that it goes exactly
   where they were told and nowhere else. Returns 0 and changes nothing if refused. */
int         mb_set_url(const char *url);
const char *mb_url(void);

/* Ask for the list. Returns at once; the answer arrives at mb_phase(). A second ask
   while one is in flight is ignored rather than queued. */
void        mb_refresh(void);
int         mb_phase(void);
/* Copy up to `max` rows out. Returns how many were written. Safe at any phase; gives
   the last list that arrived, which is what a screen wants while a refresh runs. */
int         mb_rows(MB_Row *out, int max);
const char *mb_error(void);
/* Leaving the browser: forget any reply still in flight, so it cannot land later. */
void        mb_forget(void);

/* ---- the host's half ----------------------------------------------------------
   Announce a game, keep it listed, and take it down. mb_publish is called once to
   announce and then again to keep the row alive and to carry a changed player count;
   it is the same request either way, so there is no separate heartbeat to forget.
   Every call returns at once.

   THE ROW IS WITHDRAWN ON EVERY WAY OUT OF A ROOM, and it still cannot be relied on: a
   host whose machine loses power withdraws nothing. The list expires a row that stops
   being heard from, and that timeout is the only cleanup that always works. */
int         mb_publish(const MB_Row *row);
void        mb_unpublish(void);
int         mb_published(void);
const char *mb_publish_error(void);
/* How often a listed game should call mb_publish again, in milliseconds. */
#define MB_BEAT_MS 15000

#ifdef __cplusplus
}
#endif

#endif /* MPBROWSE_H */
