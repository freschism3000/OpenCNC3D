/*
 * dosmp.h -- the MULTIPLAYER screen: host a game, browse for one, and the waiting room.
 *
 * WHAT THIS IS MODELLED ON. The C&C Remastered Collection's flow, researched rather than
 * inspected: LAN Play is its own main-menu entry there, the online path carries HOST and
 * JOIN as TABS, there are two distinct screens (a listing of games and the lobby you are
 * in), the READY control sits bottom right and shows its state, and the host's START is
 * greyed until every joining player has readied. `docs/design-multiplayer-lobby.md`
 * carries the sources and marks the details nobody could establish, which are choices
 * made here rather than facts about that game.
 *
 * WHAT IT DELIBERATELY DOES DIFFERENTLY, and why:
 *   - ONE main-menu slot, not two. The 1995 menu has a fixed six items and Multiplayer is
 *     the slot TXT_MULTIPLAYER_GAME already occupies. LAN and INTERNET are filters inside
 *     the browser instead of separate entries, which is also what was asked for.
 *   - THE MATCH OPTIONS ARE THE SKIRMISH LOBBY, reached by a button, rather than redrawn
 *     here. That screen already picks a map, a side, colours, teams, credits, tech level
 *     and the four toggles, and it already has a layout self-check. A second copy of it
 *     would be a second thing to keep in step, and the first divergence would be silent.
 *
 * THE SCREEN OWNS NO SOCKET. It answers WHAT THE PLAYER ASKED FOR and the shell does it,
 * exactly as the skirmish lobby answers a match setup and never starts a mission. That is
 * what lets the whole screen be walked by a script with no network in the process.
 */
#ifndef CNC3D_DOSMP_H
#define CNC3D_DOSMP_H

#include "dosbar.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MP_NAME_MAX 32
/* FOUR DIGITS, not a free-text password, and that is research rather than laziness: the
   Remastered Collection's private games are a PRIVATE GAME checkbox plus a four digit
   passcode (patch 735514, the first patch after launch). Four digits is also the whole of
   what a lobby password needs to be, it types on any keyboard, and it means this screen
   needs a numeric field rather than a general text editor. */
#define MP_PASS_MAX 5   /* four digits and a terminator */
/* THE PLAYER'S OWN HANDLE, eleven characters and a terminator. Not MP_NAME_MAX: the
   ROOM's name may be 32 because the advert carries 32, but a PLAYER's name is truncated
   by the engine at eleven, so a longer field would let somebody type characters the
   match will never show. */
#define MP_HANDLE_MAX 12
/* A HOST ADDRESS TYPED BY HAND, "host" or "host:port". Sixty-four to match MP_Row.addr
   and NB_ADDR_MAX: a dynamic-DNS name plus a port passes forty characters without trying,
   and a field that stops accepting a name half way through is worse than no field. */
#define MP_ADDR_MAX 64
/* The widest field mp_field has to render. It sizes one stack buffer; every field on the
   screen is drawn through it, so this is the ceiling and not a per-field guess. */
#define MP_FIELD_MAX MP_ADDR_MAX
#define MP_MAX_ROWS 32
/* How many rows the browser shows at once, on BOTH sub-tabs. The two used to differ, the
   internet list being shorter to keep the ROOM CODE field under it, and every place that
   turned a pixel into a row had to ask which sub-tab it was on. The field now shares one
   line with PASSCODE under a list of one height, so there is one number and nothing to
   ask. The driver needs it to clamp its scroll, which is why it is here rather than
   private to the drawing code. */
#define MP_LIST_ROWS_VISIBLE 10
/* A ROW'S IDENTITY, as text: the list's own id, a room code, or "address:port". Sized for
   the longest of those, a 63 character address and a five digit port. */
#define MP_KEY_MAX 72
#define MP_MAX_SEATS 8

/* Which half of the screen is showing. */
enum { MP_TAB_HOST = 0, MP_TAB_JOIN = 1 };
/* The browser's filter. INTERNET is drawn and selectable and says it has no server. */
enum { MP_NET_LAN = 0, MP_NET_INTERNET = 1 };
/* The screen has two pages: the tabs, and the waiting room once a game exists. */
enum { MP_PAGE_PICK = 0, MP_PAGE_WAIT = 1 };

/* What a press asked for. The shell does the work; this screen only answers. */
enum {
    MP_ACT_NONE = 0,
    MP_ACT_HOST,     /* start hosting with name/pass/players below    */
    MP_ACT_JOIN,     /* join the selected row                         */
    MP_ACT_REFRESH,  /* empty the list and listen again               */
    MP_ACT_READY,    /* a joiner toggled its own ready                */
    MP_ACT_START,    /* the host pressed START GAME                   */
    MP_ACT_LEAVE,    /* leave the waiting room, back to the tabs      */
    MP_ACT_CANCEL    /* back to the main menu                         */
};

/* WHY A ROW CANNOT BE JOINED, which is also what the status line says about it. OK is 0,
   so a zeroed row is an ordinary joinable one. A row from another build is judged before
   a full one: it could not be played even with a seat free. */
enum { MP_WHY_OK = 0, MP_WHY_FULL, MP_WHY_BUILD };

/* WHAT IS KNOWN ABOUT A ROW'S ROUND TRIP. UNKNOWN is 0 and is what every row carries
   until something measures it; the PING cell is then blank rather than a guess. */
enum { MP_PING_UNKNOWN = 0, MP_PING_PROBING, MP_PING_MS, MP_PING_LOST };

/* The column the list is sorted by. GAME is 0, so a fresh screen sorts by name. */
enum { MP_SORT_GAME = 0, MP_SORT_MAP, MP_SORT_PING, MP_SORT_PLAYERS };

/* Why mp_view is being asked: the rows were just refilled from the network, or the
   player pressed a header or a filter. The two keep the selection in view differently. */
enum { MP_VIEW_REFILL = 0, MP_VIEW_PLAYER };

/* One row of the browser. Filled by the shell from the beacon. */
typedef struct MP_Row
{
    /* WHICH GAME THIS IS, whatever position it arrives in: see mp_row_key. The selection
       is held by this, because the list is sorted and refilled ten times a second and a
       position means a different game after either. */
    char key[MP_KEY_MAX];
    char name[MP_NAME_MAX];
    char map[32];   /* the map's NAME when the host sent one, else its code */
    char addr[64];
    /* A RELAYED GAME HAS A CODE WHERE A DIRECT ONE HAS AN ADDRESS, and the two are not
       interchangeable: a code is a random id on a relay and names no machine, so it is
       joined by arming the relay first rather than by resolving anything. `relay` says
       which of the two this row is, and the join has to ask, because a row that is joined
       the wrong way fails in a way that looks like the host having gone. */
    char room[12];
    int relay;
    unsigned short port;
    int players_now;
    int players_max;
    int locked;
    /* 0 when this build could not play it: a different order wire, or full. The row is
       still SHOWN, greyed, because a game you cannot join is information and hiding it
       makes the browser look empty for a reason nobody can see. Always equal to
       `why == MP_WHY_OK`; `why` says which of the reasons it was. */
    int joinable;
    int why;        /* MP_WHY_* */
    /* 1 for a row heard on this network. Its PING cell is empty, it has no round trip to
       sort by, and its status line gives the address it answered from. */
    int lan;
    /* THE ROUND TRIP, as far as anything has measured it. Read only through
       mp_row_ping_ms and mp_row_ping_text. The shell fills these from the prober on the
       internet list, and decides which rows are asked at all; a row nobody asked about
       stays MP_PING_UNKNOWN and draws a blank cell. */
    int ping_state; /* MP_PING_* */
    int ping_ms;    /* meaningful only while ping_state is MP_PING_MS */
} MP_Row;

typedef struct MP_State
{
    int page;      /* MP_PAGE_*  */
    int tab;       /* MP_TAB_*   */
    int net;       /* MP_NET_*   */

    /* ---- the HOST tab ---- */
    char name[MP_NAME_MAX];
    char pass[MP_PASS_MAX];    /* up to four digits; empty when the game is open */
    char handle[MP_HANDLE_MAX];/* THIS PLAYER'S OWN NAME, asked for once and remembered */
    /* WHICH ACTION THE PROMPT STANDS IN FRONT OF: 0 when it is shut, else MP_ACT_HOST or
       MP_ACT_JOIN, which is what OK answers with. While it is set every other control is
       invisible, so it is modal for free: the hit test walks the same predicate. */
    int prompt;
    /* WHAT JOIN MEANT WHEN IT OPENED THE PROMPT: the selected game's key, or empty for a
       typed code. The list goes on refilling behind the box, so OK asks again and joins
       only if JOIN still means the same thing. */
    char promptkey[MP_KEY_MAX];
    int  private_game;         /* the PRIVATE GAME box. Without it the passcode is ignored */
    /* PLAY OVER THE INTERNET: host through a relay instead of on this network. Others
       then join with a ROOM CODE rather than an address, and nobody forwards a port,
       because both ends are talking outbound to the relay. It forces PRIVATE GAME on and
       keeps it on: over a public relay a room code is a routing address rather than a
       secret, so the passcode is the only door the room has. */
    int  relay_game;
    /* PUT THIS GAME ON THE PUBLIC LIST BY ADDRESS. Only meaningful, and only shown, while
       INTERNET GAME is off: a relayed room is listed as a code that names no machine and
       needs no permission for it, but a room on this network can only be listed by the
       address it answers on, which is the player's own connection. That is not something
       to do to somebody because they left a box unticked, so it is a box they tick, with
       the consequence written under it. */
    int  list_game;
    /* THERE IS NO PLAYER COUNT ON THIS SCREEN (5 Sep 2026, by request): the room is as
       wide as the chosen map has starts, and the lobby re-sizes it when the host picks
       another map. What the setup chose. Not drawn on this screen any more (the map is
       named once, in the match setup, and a second caption here was a second place for
       the two to disagree) -- but the beacon still announces it as the scenario fallback
       before a room has a setup, so it stays. */
    char mapname[16];

    /* ---- the JOIN tab ---- */
    /* THE ADDRESS TYPED ON THE INTERNET SUB-TAB. A game across the internet cannot be
       found by a broadcast, so somebody has to be told where it is. Empty means "use the
       selected row", which is what the LAN sub-tab always does. */
    char addr[MP_ADDR_MAX];
    /* THE ROWS, SHOWN FIRST. The shown rows, sorted, are rows[0..rowcount); the rows a
       HIDE box is holding back follow them, rows[rowcount..rowcount+rowhidden). A refill
       writes every row it has at the front with rowhidden 0 and calls mp_view, which puts
       them back in this shape. rows[rowsel] is always the selected game. */
    MP_Row rows[MP_MAX_ROWS];
    int rowcount;
    int rowhidden;
    int rowsel;
    int rowtop;
    /* THE SELECTED GAME BY IDENTITY. Set by a row click or a list key and cleared by
       REFRESH or a sub-tab change. It outlives the row: a game filtered out, or missing
       from one fetch, leaves rowsel at -1 and JOIN grey, and is selected again the moment
       it is back. */
    char selkey[MP_KEY_MAX];
    int sortcol;    /* MP_SORT_* */
    int sortdesc;   /* 1 = descending: Z to A, most players first, slowest first */
    int hide_locked;/* HIDE LOCKED: rows that ask for a passcode */
    int hide_grey;  /* HIDE GREYED: every row drawn grey, whatever made it so */
    /* THE SCROLL THUMB WHILE IT IS HELD, and where on the thumb it was taken. */
    int drag;
    int dragdiff;

    /* ---- the waiting room ---- */
    /* WHERE OTHER PLAYERS SHOULD KNOCK, as text, filled by the shell when hosting starts.
       Typing an address is only half a feature: somebody has to be able to read one out,
       and until now the host's own addresses went to stdout and nowhere a player looks.
       Empty when it could not be determined, which is a thing the screen says rather than
       guesses at. */
    char myaddr[80];
    int wait_ishost;
    int wait_humans;
    int wait_myseat;
    int wait_taken[MP_MAX_SEATS];
    int wait_ready[MP_MAX_SEATS];
    char status[96];           /* one line the shell can write anything into */

    /* ---- input ---- */
    int hover;                 /* item under the pointer, or -1           */
    int pressed;               /* item held down, or -1                   */
    int focus;                 /* the text field with the caret, or -1    */
    int caret_on;              /* blink state, the shell toggles it       */
} MP_State;

/* Every clickable thing, in one enum, exactly as the skirmish lobby does it: one list
   means hit testing, drawing and the keyboard walk cannot disagree about what exists. */
enum {
    MP_I_TAB_HOST = 0,
    MP_I_TAB_JOIN,
    MP_I_NAME,        /* text field */
    MP_I_PRIVATE,     /* the PRIVATE GAME checkbox */
    MP_I_RELAY,       /* PLAY OVER THE INTERNET: host through a relay */
    /* LIST PUBLICLY: put a NON-relayed room on the public list, at the address it
       answers on. Drawn only while MP_I_RELAY is off, because a relayed room is listed
       anyway and as a code rather than an address. */
    MP_I_PUBLIC,
    MP_I_PASS,        /* four digit passcode, live only while PRIVATE is ticked */
    MP_I_HOST,
    MP_I_NET_LAN,
    MP_I_NET_NET,
    MP_I_REFRESH,
    MP_I_ADDR,        /* text field: a host to join directly, on the INTERNET sub-tab */
    MP_I_JOIN,
    MP_I_READY,
    MP_I_START,
    MP_I_LEAVE,
    MP_I_CANCEL,
    /* THE GAME LIST'S HEADER. Each cell sorts by its column: pressed once it takes that
       column's natural order, pressed again it reverses. */
    MP_I_COL_GAME,
    MP_I_COL_MAP,
    MP_I_COL_PING,
    MP_I_COL_PLAYERS,
    /* The scroll bar beside the rows: its track pages, its thumb drags. */
    MP_I_LIST_BAR,
    /* The two HIDE boxes on the sub-tab row. The rect covers the box and its word. */
    MP_I_HIDE_LOCKED,
    MP_I_HIDE_GREY,
    /* THE NAME PROMPT'S OWN CONTROLS, last because they exist only while the box is up. */
    MP_I_HANDLE,
    MP_I_HANDLE_OK,
    MP_ITEM_COUNT
};

void mp_init(MP_State *st);
/* Is this item drawn on the page and tab now showing? Hit testing and the keyboard walk
   both ask, so an item that is not visible can never be pressed. */
int  mp_item_visible(const MP_State *st, int item);
/* Is it visible but refused? A greyed START says WHY in the status line. */
int  mp_item_disabled(const MP_State *st, int item);
int  mp_item_rect(const MP_State *st, int item, int *x, int *y, int *w, int *h);
int  mp_hit_test(const MP_State *st, int mx, int my);
/* The browser row under the pointer, or -1. Only the rows band answers: the header above
   it and the scroll bar beside it never select a row. */
int  mp_row_at(const MP_State *st, int mx, int my);

int  mp_press(MP_State *st, int mx, int my);    /* returns MP_ACT_* */
void mp_motion(MP_State *st, int mx, int my);
void mp_release(MP_State *st);
void mp_scroll(MP_State *st, int delta);
/* A printable character typed into the focused field. Ignored when nothing has focus. */
void mp_text(MP_State *st, char ch);
/* Backspace, tab, escape, return, the arrows, and the four keys that page the list.
   `key` is one of MP_K_*. */
enum { MP_K_BACK = 1, MP_K_TAB, MP_K_ESC, MP_K_ENTER, MP_K_UP, MP_K_DOWN,
       MP_K_PGUP, MP_K_PGDN, MP_K_HOME, MP_K_END };
int  mp_key(MP_State *st, int key);             /* returns MP_ACT_* */
/* SHUT THE YOUR NAME BOX WITHOUT ITS OK: ESC does this, and so does leaving the screen.
   The prompt only opens for a player with no name, and a name nobody confirmed is not a
   name, so what was typed into it goes too. Does nothing while the box is shut. */
void mp_prompt_close(MP_State *st);

/* ---- the game list: identity, order, filters, scroll ---------------------------- */

/* A ROW'S KEY, from what the row already carries. The list's own id when there is one,
   because two direct games behind one router share an address and a port. Without one, a
   relayed row's room code, and otherwise "address:port", which is also what a LAN row is
   always keyed by. Call it after the row's other fields are filled. */
void mp_row_key(MP_Row *r, const char *id);

/* PUT THE ROWS IN ORDER, AND THE SELECTION BACK ON ITS GAME. A pure function of the state.
   Takes every row in rows[0..rowcount+rowhidden), moves the ones a HIDE box holds back to
   the end, sorts the rest, finds `selkey` among them, and scrolls:
     MP_VIEW_REFILL  the selected row keeps the screen line it had, if it was on screen;
                     a row that was scrolled out of view is not chased.
     MP_VIEW_PLAYER  the same, and a row that was out of view is scrolled the least
                     distance that brings it back.
   The order is total, so the result never depends on the order rows arrived in: joinable
   rows before rows that cannot be joined (never reversed), then the sort column in its
   direction, then name A to Z ignoring case, then key. */
void mp_view(MP_State *st, int reason);

/* THE STATUS LINE'S EXPLANATION when the shell has nothing of its own to say: what the
   selected row is and why it can or cannot be joined, how many games a HIDE box is holding
   back, or what to type while the ROOM CODE field has the caret. Empty when there is
   nothing to explain. The shell's own `status` wins over this when it is set. */
void mp_status_line(const MP_State *st, char *buf, int len);

/* THE PING CELL, from one place. mp_row_ping_ms is the round trip in milliseconds, or -1
   when it is not known or the row is a LAN row, which is also how the PING sort sees it:
   unknown values sort last in both directions, and a LAN list sorted by PING is all ties,
   ordered by name. mp_row_ping_text is what the cell prints: blank for a row this build
   cannot join and for a LAN row, "..." while it is being measured, "NN MS" once it is,
   "--" when nothing answered, and blank while nothing has been measured. */
int  mp_row_ping_ms(const MP_Row *r);
void mp_row_ping_text(const MP_Row *r, char *buf, int len);

/* Where the scroll thumb is, in menu pixels: its top and its height. A height of zero
   means there is no thumb: every shown row fits, so the track is drawn empty and a press
   on it does nothing. */
void mp_bar_thumb(const MP_State *st, int *ty, int *th);

/* ---- remembered across launches -------------------------------------------------- */

/* WHAT THE SCREEN REMEMBERS: the sort, both HIDE boxes and the player's own name. */
typedef struct MP_Prefs
{
    int sortcol;
    int sortdesc;
    int hide_locked;
    int hide_grey;
    char handle[MP_HANDLE_MAX];
} MP_Prefs;
void mp_prefs_get(const MP_State *st, MP_Prefs *out);
/* WHAT A SETTINGS WRITE SHOULD HOLD NOW: every value as the screen has it, except the name
   while the YOUR NAME box is still open, which stays what `saved` holds. So the sort and
   the HIDE boxes are written whenever they change, and a half typed name never is. */
void mp_prefs_to_save(const MP_State *st, const MP_Prefs *saved, MP_Prefs *out);
/* Applies them and re-sorts, so the list is in the remembered order at once. */
void mp_prefs_put(MP_State *st, const MP_Prefs *in);
int  mp_prefs_equal(const MP_Prefs *a, const MP_Prefs *b);
/* Reads a settings file over `io`, which the caller fills with the defaults first. Every
   value is checked on the way in and a value that is not a legal one leaves that default
   standing, so a damaged file changes nothing it cannot vouch for. Returns 1 when a file
   was opened, 0 when there is none. Says nothing either way. */
int  mp_prefs_read(const char *path, MP_Prefs *io);
/* Writes the file. Returns 0 when it could not be written. */
int  mp_prefs_write(const char *path, const MP_Prefs *p);

void mp_draw(DB_Surface *s, const DB_Pack *p, const MP_State *st);
/* Does every string this screen can print fit the box it prints into? The same question
   sk_check_layout answers for the skirmish lobby, and the same reason: it is the one
   thing about a screen made of labels that arithmetic answers exactly and a screenshot
   answers badly. Returns the number of overflows, 0 being the good answer. */
int  mp_check_layout(const DB_Pack *p, const MP_State *st);

#ifdef __cplusplus
}
#endif
#endif
