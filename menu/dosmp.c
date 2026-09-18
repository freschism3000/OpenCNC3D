/*
 * dosmp.c -- the MULTIPLAYER screen. See dosmp.h for the shape and what it is modelled on.
 */
#include "dosmp.h"
#include "dosmenu.h"
#include "doslobby.h"

#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------- the layout ----- *
 *  The same dialog box the skirmish lobby uses, so the two screens are plainly the same
 *  program. Everything below is in MENU space, 320x200, and nothing is computed from a
 *  window size: the shell scales the finished surface.
 * ------------------------------------------------------------------------------------ */
#define MPX SK_DLG_X
#define MPY SK_DLG_Y
#define MPW SK_DLG_W
#define MPH SK_DLG_H

#define MP_CAP_Y (MPY + 5)
#define MP_TAB_Y (MPY + 18)
#define MP_TAB_H 10
#define MP_TAB_W 70
#define MP_TAB0_X (MPX + 12)
#define MP_TAB1_X (MP_TAB0_X + MP_TAB_W + 4)

#define MP_BODY_Y (MP_TAB_Y + MP_TAB_H + 4)

/* HOST tab */
#define MP_LBL_X (MPX + 14)
#define MP_FLD_X (MPX + 84)
#define MP_FLD_W 150
#define MP_FLD_H 11
#define MP_NAME_Y (MP_BODY_Y + 8)
#define MP_PRIV_Y (MP_NAME_Y + 16)
#define MP_PRIV_BOX 9
/* THE CHECKBOX SITS A LITTLE RIGHT OF THE FIELD COLUMN, on purpose. "PRIVATE GAME" is the
   longest label on this tab and it ends exactly where the column begins, so a box aligned
   with the fields above and below touches the M. Eight pixels is the smallest gap that
   reads as a gap. */
#define MP_PRIV_X (MP_FLD_X + 8)
#define MP_PASS_Y (MP_PRIV_Y + 15)
#define MP_PASS_W 44
/* The relay checkbox, a row under the passcode. The host tab has eighty free pixels below
   it before the status line, so nothing moves to make room. */
#define MP_RELAY_Y (MP_PASS_Y + 17)
/* The two sentences under the relay box, named so mp_check_layout can measure them. The
   room's right edge is MPX + MPW, and the line starts at MP_LBL_X. */
/* The three lines of an empty INTERNET list, named so mp_check_layout measures them. The
   third is also what the status line says while the ROOM CODE field has the caret. */
#define MP_JOIN_L1 "LOOKING FOR GAMES ON THE INTERNET..."
#define MP_JOIN_L2 "A GAME APPEARS A FEW SECONDS AFTER IT OPENS."
#define MP_JOIN_L3 "OR TYPE A CODE. #K7M-3QX NEEDS NO PORTS."
/* The two lines of an empty LAN list. */
#define MP_LAN_L1 "LOOKING FOR GAMES ON YOUR NETWORK..."
#define MP_LAN_L2 "A GAME APPEARS A SECOND AFTER IT IS HOSTED."
/* The second line of a list the HIDE boxes emptied; the first carries the count. */
#define MP_HIDDEN_L2 "UNTICK HIDE TO SEE THEM."
/* The fixed sentences mp_status_line can say about a selected row. */
#define MP_SAY_BUILD  "ANOTHER BUILD HOSTS THIS GAME."
#define MP_SAY_LOCKED "LOCKED: TYPE THE HOST'S 4 DIGIT PASSCODE."
/* THE LONGEST ADDRESS THE STATUS LINE PRINTS WHOLE, in characters. No glyph in the menu
   font is wider than six pixels, so twenty-eight of them and the rest of
   "DIRECT TO ...:65535." fit the line, and mp_check_layout measures that widest case
   rather than trusting this sum. A longer address is cut with "..". */
#define MP_SAY_ADDR_MAX 28
/* WHAT TICKING THE BOX ACTUALLY DOES, said in the caption rather than left to be found
   out. A relayed room goes on the public list, and what stands in that list is a code
   that names no machine, so being listed costs the host nothing they would mind. The
   OFF case is not listed at all, which is why it does not have to warn about anything. */
#define MP_RELAY_ON_TEXT  "IN THE PUBLIC LIST, AS A CODE. NO PORTS."
/* THE OFF LINE NO LONGER SAYS "THIS NETWORK ONLY", because with the box below it ticked
   that is not true any more: an unrelayed room can be listed by address. It says how
   others reach it, which is true either way, and leaves being LISTED to the box that
   owns that question. */
#define MP_RELAY_OFF_TEXT "OFF: ON THIS NETWORK, OR BY ADDRESS."
/* LIST PUBLICLY, one row under the relay box, and only drawn while the relay box is off.
   The ON line is a warning and has to read as one: what goes on the list for a room that
   is not relayed is the address it answers on, which is the player's own connection, and
   it is read by everybody who opens the browser rather than only by whoever joins. */
#define MP_PUBLIC_Y (MP_RELAY_Y + 26)
#define MP_PUBLIC_ON_TEXT  "YOUR ADDRESS GOES ON THE PUBLIC LIST."
#define MP_PUBLIC_OFF_TEXT "OFF: NOT LISTED. TELL PEOPLE YOURSELF."
/* NOTHING SITS UNDER THE PASSCODE ANY MORE. The PLAYERS gauge went when the
   room became as wide as its map; the MAP caption went with the map to the lobby; and
   SET UP MATCH went last: HOST opens the room and takes the player straight to the
   multiplayer game screen, where the map and every rule are set. */

/* JOIN tab, the same geometry on both sub-tabs:
 *
 *   y  36   [LAN][INTERNET]                 HIDE [ ]LOCKED [ ]GREYED
 *   y  49   | GAME         | MAP     | PING  | PLAYERS ^ |          header, 9 px
 *   y  58   | * ROOM NAME    MAP NAME  12 MS   1/8       |#|        ten rows, 9 px pitch
 *   y 152   ROOM CODE [#K7M-3QX     ]    PASSCODE [    ]            the field line
 *   y 166   the status line, which explains the selected row
 *   y 178   [REFRESH] [CANCEL]                         [JOIN]
 */
#define MP_NET_Y (MP_BODY_Y + 2)
#define MP_NET_H 9
#define MP_NET_W 56
#define MP_NET0_X (MPX + 14)
#define MP_NET1_X (MP_NET0_X + MP_NET_W + 4)
/* The list's whole rectangle: header, rows and scroll bar. */
#define MP_LIST_X (MPX + 12)
#define MP_LIST_Y (MP_NET_Y + MP_NET_H + 4)
#define MP_LIST_W (MPW - 24)
#define MP_HEAD_H 9
#define MP_LIST_ROWS MP_LIST_ROWS_VISIBLE
#define MP_LIST_RH 9
#define MP_ROWS_Y (MP_LIST_Y + MP_HEAD_H)
#define MP_ROWS_H (MP_LIST_ROWS * MP_LIST_RH)
/* THE SCROLL BAR, on the list's right edge and only beside the rows. Five wide, a two
   pixel gap, a four pixel minimum thumb: the proportions of the Visuals Advanced page's
   bar, which have already been looked at on this surface. */
#define MP_BAR_W 5
#define MP_BAR_GAP 2
#define MP_BAR_MIN 4
#define MP_BAR_X (MP_LIST_X + MP_LIST_W - MP_BAR_W)
/* THE ROWS BAND STOPS SHORT OF THE BAR, so a press on the bar, or in the gap beside it,
   can never select the row it happens to be level with. */
#define MP_ROWS_W (MP_BAR_X - MP_BAR_GAP - MP_LIST_X)
/* THE COLUMNS. Each header cell is its column's clickable rectangle, and the four cells
   tile the rows band exactly. Text in a cell starts MP_CELL_IN in, except the room's
   name, which keeps the offset past the padlock the rows have always had. */
#define MP_COL_LOCK 3      /* the padlock '*', from the list's left edge */
#define MP_COL_NAME 12     /* the room's name, from the list's left edge */
#define MP_CELL_IN 3
#define MP_HEAD_GAME_W 98
#define MP_HEAD_MAP_W  67
#define MP_HEAD_PING_W 38
#define MP_HEAD_MAP_X  (MP_LIST_X + MP_HEAD_GAME_W)
#define MP_HEAD_PING_X (MP_HEAD_MAP_X + MP_HEAD_MAP_W)
#define MP_HEAD_PLR_X  (MP_HEAD_PING_X + MP_HEAD_PING_W)
#define MP_HEAD_PLR_W  (MP_LIST_X + MP_ROWS_W - MP_HEAD_PLR_X)
/* TEXT BUDGETS, in pixels. Fourteen glyphs of a room's name. Ten glyphs of a map's name,
   which is what the PING column's thirty-eight pixels left of it: a longer map name is
   cut with "..", exactly as a room's name is. "999 MS" for the round trip. Each is
   measured by mp_check_layout against the worst case and against every row drawn. */
#define MP_NAME_TW 84
#define MP_MAP_TW  62
#define MP_PING_TW 34
#define MP_PLR_TW  (MP_HEAD_PLR_W - MP_CELL_IN)
/* THE TWO HIDE BOXES, right-aligned on the sub-tab row so they end where the list ends.
   Each item's rect is the nine pixel box AND its word, so the word is clickable too. */
#define MP_HIDE_W 47
#define MP_HIDE_BOX 9
#define MP_HIDE_TXT 11     /* the word, from the box's left edge */
#define MP_HIDE1_X (MP_LIST_X + MP_LIST_W - MP_HIDE_W)
#define MP_HIDE0_X (MP_HIDE1_X - MP_HIDE_W - 6)
#define MP_HIDE_CAP_X (MP_HIDE0_X - 28)   /* "HIDE" is 24 px, then a 4 px gap */
/* THE FIELD LINE, under the rows. ROOM CODE on the left on the INTERNET sub-tab, PASSCODE
   on the right on both, so PASSCODE stays put when the sub-tab changes. */
#define MP_FIELDS_Y (MP_ROWS_Y + MP_ROWS_H + 4)
#define MP_ADDR_X (MP_LBL_X + 56)          /* "ROOM CODE" is 52 px, then a 4 px gap */
#define MP_ADDR_W 104                      /* sixteen glyphs show; it keeps the tail */
#define MP_JPASS_X (MP_LIST_X + MP_LIST_W - MP_PASS_W)
#define MP_JPASS_LBL_X (MP_JPASS_X - 52)   /* "PASSCODE" is 48 px, then a 4 px gap */

/* the waiting room */
#define MP_SEAT_X (MPX + 14)
#define MP_SEAT_Y (MP_BODY_Y + 6)
#define MP_SEAT_W (MPW - 28)
#define MP_SEAT_RH 10
#define MP_SEAT_READY 200

/* the button row, shared by every page */
#define MP_BTN_H 12
#define MP_BTN_Y (MPY + MPH - MP_BTN_H - 8)
#define MP_BTN_W 78
#define MP_BTN0_X (MPX + 12)
#define MP_BTN1_X (MPX + MPW - MP_BTN_W - 12)
#define MP_STATUS_Y (MP_BTN_Y - 12)
#define MP_STATUS_X (MPX + 12)
#define MP_STATUS_W (MPW - 24)

void mp_init(MP_State *st)
{
    memset(st, 0, sizeof *st);
    st->page = MP_PAGE_PICK;
    st->tab = MP_TAB_HOST;
    st->net = MP_NET_LAN;
    st->rowsel = -1;
    st->hover = -1;
    st->pressed = -1;
    st->focus = -1;
    st->wait_myseat = -1;
    /* A DEFAULT SO THE FIELD IS NEVER EMPTY. A game with no name is a blank row in
       somebody else's browser, which reads as a broken host rather than a shy one. */
    snprintf(st->name, sizeof st->name, "%s", "CNC3D GAME");
    snprintf(st->mapname, sizeof st->mapname, "%s", "SCM01EA");
    /* The list sorts by game name, A to Z, which memset already said. Names do not change
       between heartbeats, so it is the order that moves least while a player reads it. */
}

int mp_item_visible(const MP_State *st, int item)
{
    /* MODAL. While the prompt is up nothing under it is visible, so nothing under it can
       be clicked and nothing under it takes the keyboard. */
    if (st->prompt) return item == MP_I_HANDLE || item == MP_I_HANDLE_OK;
    if (st->page == MP_PAGE_WAIT) {
        switch (item) {
        case MP_I_READY:  return !st->wait_ishost;
        case MP_I_START:  return st->wait_ishost;
        case MP_I_LEAVE:  return 1;
        default:          return 0;
        }
    }
    switch (item) {
    case MP_I_TAB_HOST:
    case MP_I_TAB_JOIN:
    case MP_I_CANCEL:
        return 1;
    case MP_I_NAME:
    case MP_I_PRIVATE:
    case MP_I_RELAY:
    case MP_I_HOST:
        return st->tab == MP_TAB_HOST;
    /* NOT DRAWN AT ALL WHILE THE ROOM IS RELAYED, rather than drawn and greyed. A
       relayed room is on the list already and as a code, so the question this box asks
       does not arise, and a dead control that cannot say why is the thing every other
       disabled control on this screen is written to avoid. */
    case MP_I_PUBLIC:
        return st->tab == MP_TAB_HOST && !st->relay_game;
    /* THE PASSCODE IS ON BOTH TABS: the host types the code that locks
       the room, and a joiner types the code that opens it. One field, two jobs, and
       no second control to keep in step. */
    case MP_I_PASS:
        return 1;
    case MP_I_NET_LAN:
    case MP_I_NET_NET:
    case MP_I_REFRESH:
    case MP_I_JOIN:
    case MP_I_COL_GAME:
    case MP_I_COL_MAP:
    case MP_I_COL_PING:
    case MP_I_COL_PLAYERS:
    case MP_I_LIST_BAR:
    case MP_I_HIDE_LOCKED:
    case MP_I_HIDE_GREY:
        return st->tab == MP_TAB_JOIN;
    /* ONLY ON THE INTERNET SUB-TAB. A game across the internet cannot be found by a
       broadcast, so this is where somebody types the code or the address they were given.
       It shares the line under the list with PASSCODE, which is why a typed target only
       wins while no row is selected (the shell's JOIN says so). */
    case MP_I_ADDR:
        return st->tab == MP_TAB_JOIN && st->net == MP_NET_INTERNET;
    default:
        return 0;
    }
}

int mp_item_disabled(const MP_State *st, int item)
{
    if (!mp_item_visible(st, item)) return 1;
    switch (item) {
    case MP_I_PASS:
        /* On the host tab it belongs to PRIVATE GAME; on the join tab it is live only
           while the selected room carries the padlock, so an open game does not offer
           a box nobody needs. */
        if (st->tab == MP_TAB_HOST) return !st->private_game;
        /* A TYPED ADDRESS CANNOT CARRY A PADLOCK. There is no row to read `locked` off,
           so the box has to be offered on the chance -- a direct joiner types the code
           speculatively. Without this it is permanently dead on a typed join and a locked
           host is simply unreachable: they would be refused for the passcode, with no box
           on screen to answer the refusal in. */
        if (st->addr[0] && st->net == MP_NET_INTERNET) return 0;
        if (st->rowsel < 0 || st->rowsel >= st->rowcount) return 1;
        return !st->rows[st->rowsel].locked;
    case MP_I_ADDR:
        return 0;

    case MP_I_JOIN:
        /* A typed address is a destination in its own right and needs no row. */
        if (st->addr[0] && st->net == MP_NET_INTERNET) return 0;
        /* Nothing selected, or a row this build could not play. */
        if (st->rowsel < 0 || st->rowsel >= st->rowcount) return 1;
        return !st->rows[st->rowsel].joinable;
    case MP_I_START: {
        /* THE HOST'S START IS GREY UNTIL EVERY JOINER IS READY, which is the rule this
           screen exists to enforce and is what Remastered does. */
        int i;
        for (i = 0; i < st->wait_humans && i < MP_MAX_SEATS; i++) {
            if (!st->wait_taken[i]) return 1;
            if (!st->wait_ready[i]) return 1;
        }
        return st->wait_humans < 2;
    }
    default:
        return 0;
    }
}

int mp_item_rect(const MP_State *st, int item, int *x, int *y, int *w, int *h)
{
    int rx = 0, ry = 0, rw = 0, rh = 0;
    if (!mp_item_visible(st, item)) return 0;
    switch (item) {
    case MP_I_TAB_HOST: rx = MP_TAB0_X; ry = MP_TAB_Y; rw = MP_TAB_W; rh = MP_TAB_H; break;
    case MP_I_TAB_JOIN: rx = MP_TAB1_X; ry = MP_TAB_Y; rw = MP_TAB_W; rh = MP_TAB_H; break;
    case MP_I_NAME:     rx = MP_FLD_X; ry = MP_NAME_Y; rw = MP_FLD_W; rh = MP_FLD_H; break;
    /* THE PROMPT, centred in the dialog: a field and an OK under it. */
    case MP_I_HANDLE:    rx = MPX + 60; ry = MPY + 86; rw = MPW - 120; rh = MP_FLD_H; break;
    case MP_I_HANDLE_OK: rx = MPX + (MPW - MP_BTN_W) / 2; ry = MPY + 108;
                         rw = MP_BTN_W; rh = MP_BTN_H; break;
    case MP_I_PRIVATE:  rx = MP_PRIV_X; ry = MP_PRIV_Y; rw = MP_PRIV_BOX; rh = MP_PRIV_BOX; break;
    case MP_I_RELAY:    rx = MP_PRIV_X; ry = MP_RELAY_Y; rw = MP_PRIV_BOX; rh = MP_PRIV_BOX; break;
    case MP_I_PUBLIC:   rx = MP_PRIV_X; ry = MP_PUBLIC_Y; rw = MP_PRIV_BOX; rh = MP_PRIV_BOX; break;
    case MP_I_PASS:
        /* On the join tab, at the right end of the line under the list; on the host tab,
           under PRIVATE GAME. */
        rw = MP_PASS_W; rh = MP_FLD_H;
        if (st->tab == MP_TAB_JOIN) { rx = MP_JPASS_X; ry = MP_FIELDS_Y; }
        else                        { rx = MP_FLD_X;   ry = MP_PASS_Y; }
        break;
    case MP_I_HOST:     rx = MP_BTN1_X; ry = MP_BTN_Y; rw = MP_BTN_W; rh = MP_BTN_H; break;
    case MP_I_NET_LAN:  rx = MP_NET0_X; ry = MP_NET_Y; rw = MP_NET_W; rh = MP_NET_H; break;
    case MP_I_NET_NET:  rx = MP_NET1_X; ry = MP_NET_Y; rw = MP_NET_W; rh = MP_NET_H; break;
    case MP_I_REFRESH:  rx = MP_LBL_X; ry = MP_BTN_Y; rw = MP_BTN_W; rh = MP_BTN_H; break;
    /* UNDER THE ROWS, four pixels below the last one a click can select. */
    case MP_I_ADDR:     rx = MP_ADDR_X; ry = MP_FIELDS_Y; rw = MP_ADDR_W; rh = MP_FLD_H; break;
    case MP_I_JOIN:     rx = MP_BTN1_X; ry = MP_BTN_Y; rw = MP_BTN_W; rh = MP_BTN_H; break;
    case MP_I_READY:    rx = MP_BTN1_X; ry = MP_BTN_Y; rw = MP_BTN_W; rh = MP_BTN_H; break;
    case MP_I_START:    rx = MP_BTN1_X; ry = MP_BTN_Y; rw = MP_BTN_W; rh = MP_BTN_H; break;
    case MP_I_LEAVE:    rx = MP_BTN0_X; ry = MP_BTN_Y; rw = MP_BTN_W; rh = MP_BTN_H; break;
    case MP_I_CANCEL:
        /* On the JOIN tab REFRESH already owns the left slot, so CANCEL sits beside it. */
        rx = (st->tab == MP_TAB_JOIN) ? (MP_BTN0_X + MP_BTN_W + 6) : MP_BTN0_X;
        ry = MP_BTN_Y; rw = MP_BTN_W; rh = MP_BTN_H;
        break;
    case MP_I_COL_GAME:    rx = MP_LIST_X;      ry = MP_LIST_Y; rw = MP_HEAD_GAME_W; rh = MP_HEAD_H; break;
    case MP_I_COL_MAP:     rx = MP_HEAD_MAP_X;  ry = MP_LIST_Y; rw = MP_HEAD_MAP_W;  rh = MP_HEAD_H; break;
    case MP_I_COL_PING:    rx = MP_HEAD_PING_X; ry = MP_LIST_Y; rw = MP_HEAD_PING_W; rh = MP_HEAD_H; break;
    case MP_I_COL_PLAYERS: rx = MP_HEAD_PLR_X;  ry = MP_LIST_Y; rw = MP_HEAD_PLR_W;  rh = MP_HEAD_H; break;
    case MP_I_LIST_BAR:    rx = MP_BAR_X; ry = MP_ROWS_Y; rw = MP_BAR_W; rh = MP_ROWS_H; break;
    case MP_I_HIDE_LOCKED: rx = MP_HIDE0_X; ry = MP_NET_Y; rw = MP_HIDE_W; rh = MP_NET_H; break;
    case MP_I_HIDE_GREY:   rx = MP_HIDE1_X; ry = MP_NET_Y; rw = MP_HIDE_W; rh = MP_NET_H; break;
    default: return 0;
    }
    if (x) *x = rx;
    if (y) *y = ry;
    if (w) *w = rw;
    if (h) *h = rh;
    return 1;
}

static int mp_in(int mx, int my, int x, int y, int w, int h)
{
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

int mp_hit_test(const MP_State *st, int mx, int my)
{
    int i, x, y, w, h;
    for (i = 0; i < MP_ITEM_COUNT; i++) {
        if (!mp_item_visible(st, i)) continue;
        if (!mp_item_rect(st, i, &x, &y, &w, &h)) continue;
        if (mp_in(mx, my, x, y, w, h)) return i;
    }
    return -1;
}

/* The rows band is the one place a pixel turns into a row, and it is asked by the drawing,
   the hit test and the harness alike, so the three cannot disagree about where the list
   is. The prompt is modal, so no row is under it. */
int mp_row_at(const MP_State *st, int mx, int my)
{
    int r;
    if (st->page != MP_PAGE_PICK || st->tab != MP_TAB_JOIN || st->prompt) return -1;
    if (mx < MP_LIST_X || mx >= MP_LIST_X + MP_ROWS_W) return -1;
    if (my < MP_ROWS_Y || my >= MP_ROWS_Y + MP_ROWS_H) return -1;
    r = st->rowtop + (my - MP_ROWS_Y) / MP_LIST_RH;
    return (r >= 0 && r < st->rowcount) ? r : -1;
}

/* ------------------------------------------------------------- the game list ------- */

static int mp_clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* The top row can never be past the point where the last row sits at the bottom. */
static void mp_clamp_top(MP_State *st)
{
    int last = st->rowcount - MP_LIST_ROWS;
    if (last < 0) last = 0;
    st->rowtop = mp_clampi(st->rowtop, 0, last);
}

/* Scroll the least distance that puts the selected row on screen. */
static void mp_show_sel(MP_State *st)
{
    if (st->rowsel >= 0) {
        if (st->rowsel < st->rowtop) st->rowtop = st->rowsel;
        if (st->rowsel >= st->rowtop + MP_LIST_ROWS) st->rowtop = st->rowsel - MP_LIST_ROWS + 1;
    }
    mp_clamp_top(st);
}

/* A SELECTION BY HAND. It says what it selected by key, and it clears the status line,
   because anything written there, a refusal most often, described the previous one. */
static void mp_select(MP_State *st, int row)
{
    st->rowsel = row;
    snprintf(st->selkey, sizeof st->selkey, "%s", st->rows[row].key);
    st->status[0] = '\0';
}

static void mp_forget_selection(MP_State *st)
{
    st->rowsel = -1;
    st->selkey[0] = '\0';
}

void mp_row_key(MP_Row *r, const char *id)
{
    if (id && id[0])
        snprintf(r->key, sizeof r->key, "%s", id);
    else if (r->relay && r->room[0])
        snprintf(r->key, sizeof r->key, "%s", r->room);
    else
        snprintf(r->key, sizeof r->key, "%s:%u", r->addr, (unsigned)r->port);
}

int mp_row_ping_ms(const MP_Row *r)
{
    /* A LAN ROW HAS NO ROUND TRIP TO SORT BY, whatever its state fields hold, so a PING
       sort on the LAN list leaves every row tied and the name decides the order. */
    if (!r || r->lan) return -1;
    return (r->ping_state == MP_PING_MS && r->ping_ms >= 0) ? r->ping_ms : -1;
}

void mp_row_ping_text(const MP_Row *r, char *buf, int len)
{
    if (!buf || len <= 0) return;
    buf[0] = '\0';
    /* A ROW THIS BUILD WOULD BE REFUSED FROM SHOWS NOTHING: a round trip to a door that
       will not open is not a number anybody should choose by. */
    if (!r || !r->joinable) return;
    /* NOR DOES A LAN ROW. Every row on that list is on this network, so a word repeated
       down the whole column says nothing; the cell is left empty and the header stays. */
    if (r->lan) return;
    switch (r->ping_state) {
    case MP_PING_PROBING: snprintf(buf, (size_t)len, "%s", "..."); break;
    /* THREE DIGITS AT MOST, because the cell is sized for "999 MS": a round trip of a
       second or more reads 999, and is unplayable either way. */
    case MP_PING_MS:      snprintf(buf, (size_t)len, "%d MS", mp_clampi(r->ping_ms, 0, 999)); break;
    case MP_PING_LOST:    snprintf(buf, (size_t)len, "%s", "--"); break;
    default:              break;
    }
}

/* Case-insensitive ASCII, which is all the menu font can draw. */
static int mp_ci_cmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'a' && ca <= 'z') ca -= 'a' - 'A';
        if (cb >= 'a' && cb <= 'z') cb -= 'a' - 'A';
        if (ca != cb) return ca < cb ? -1 : 1;
        if (!ca) return 0;
    }
}

static int mp_cmp_int(int a, int b) { return a < b ? -1 : (a > b ? 1 : 0); }

/* THE ORDER, AS A TOTAL ORDER. Negative when a goes before b.
 *   1. joinable rows before rows that cannot be joined, in either direction, so a game a
 *      player can join is never buried under ones they cannot;
 *   2. the sort column in the chosen direction. A PING that is not known goes last in
 *      both directions, because "not measured" is not slower or faster than anything;
 *   3. name A to Z ignoring case, then key, always ascending. Keys are unique, so nothing
 *      is left to the order the rows arrived in. */
static int mp_row_order(const MP_State *st, const MP_Row *a, const MP_Row *b)
{
    const int ga = !a->joinable, gb = !b->joinable;
    int d = 0;
    if (ga != gb) return ga - gb;
    switch (st->sortcol) {
    case MP_SORT_MAP:
        d = mp_ci_cmp(a->map, b->map);
        break;
    case MP_SORT_PLAYERS:
        d = mp_cmp_int(a->players_now, b->players_now);
        if (!d) d = mp_cmp_int(a->players_max, b->players_max);
        break;
    case MP_SORT_PING: {
        const int pa = mp_row_ping_ms(a), pb = mp_row_ping_ms(b);
        if ((pa < 0) != (pb < 0)) return (pa < 0) ? 1 : -1;
        d = mp_cmp_int(pa, pb);
        break;
    }
    default:
        d = mp_ci_cmp(a->name, b->name);
        break;
    }
    if (st->sortdesc) d = -d;
    if (d) return d;
    d = mp_ci_cmp(a->name, b->name);
    if (d) return d;
    return strcmp(a->key, b->key);
}

/* What a HIDE box holds back. GREYED is named after what the player sees, so it goes on
   meaning "every grey row" whatever reasons for grey are added later. */
static int mp_row_held(const MP_State *st, const MP_Row *r)
{
    return (st->hide_locked && r->locked) || (st->hide_grey && !r->joinable);
}

void mp_view(MP_State *st, int reason)
{
    MP_Row sorted[MP_MAX_ROWS];
    int idx[MP_MAX_ROWS];
    int total = st->rowcount + st->rowhidden;
    int shown = 0, held = 0, i, j, line = -1;

    if (total > MP_MAX_ROWS) total = MP_MAX_ROWS;
    if (total < 0) total = 0;

    /* 1. The screen line the selected row was on, if it was on screen at all. */
    if (st->rowsel >= 0 && st->rowsel >= st->rowtop && st->rowsel < st->rowtop + MP_LIST_ROWS)
        line = st->rowsel - st->rowtop;

    /* 2. Shown rows first, the held ones after them, then the shown ones sorted. An
       insertion sort, because there are at most thirty-two rows and it needs no global
       to carry the sort column into a comparator. */
    for (i = 0; i < total; i++)
        if (!mp_row_held(st, &st->rows[i])) idx[shown++] = i;
    for (i = 0; i < total; i++)
        if (mp_row_held(st, &st->rows[i])) idx[shown + held++] = i;
    for (i = 1; i < shown; i++) {
        const int v = idx[i];
        for (j = i; j > 0 && mp_row_order(st, &st->rows[v], &st->rows[idx[j - 1]]) < 0; j--)
            idx[j] = idx[j - 1];
        idx[j] = v;
    }
    for (i = 0; i < total; i++) sorted[i] = st->rows[idx[i]];
    if (total > 0) memcpy(st->rows, sorted, sizeof(MP_Row) * (size_t)total);
    st->rowcount = shown;
    st->rowhidden = held;

    /* 3. The selection is wherever its game now is, or nowhere. It is never invented: a
       state with no key has no selection, whatever rowsel said. */
    st->rowsel = -1;
    if (st->selkey[0]) {
        for (i = 0; i < shown; i++) {
            if (!strcmp(st->rows[i].key, st->selkey)) { st->rowsel = i; break; }
        }
    }

    /* 4 and 5. Keep its line if it had one; a press also brings back a row that was out of
       view, and a refill does not chase one. */
    if (st->rowsel >= 0 && line >= 0) st->rowtop = st->rowsel - line;
    if (reason == MP_VIEW_PLAYER) mp_show_sel(st);

    /* 6. */
    mp_clamp_top(st);
}

void mp_bar_thumb(const MP_State *st, int *ty, int *th)
{
    const int total = st->rowcount > MP_LIST_ROWS ? st->rowcount : MP_LIST_ROWS;
    const int h = mp_clampi(MP_ROWS_H * MP_LIST_ROWS / total, MP_BAR_MIN, MP_ROWS_H);
    int off;
    /* NOTHING TO SCROLL, NO THUMB. When every shown game fits in the rows, including when
       none is shown at all, a thumb the length of the track reads as a second border round
       the well rather than as something to hold. The answer is then a height of zero at
       the top of the track, which the drawing takes as "no plate" and a press takes as a
       track with nothing to page or hold. */
    if (st->rowcount <= MP_LIST_ROWS) {
        if (ty) *ty = MP_ROWS_Y;
        if (th) *th = 0;
        return;
    }
    /* PINNED TO THE BOTTOM AT THE LAST PAGE. Plain floor division leaves the thumb a pixel
       short of the end of the track there, which reads as "there is more below". */
    if (st->rowtop >= st->rowcount - MP_LIST_ROWS) {
        off = MP_ROWS_H - h;
    } else {
        off = MP_ROWS_H * st->rowtop / total;
        if (off > MP_ROWS_H - h) off = MP_ROWS_H - h;
    }
    if (off < 0) off = 0;
    if (ty) *ty = MP_ROWS_Y + off;
    if (th) *th = h;
}

void mp_status_line(const MP_State *st, char *buf, int len)
{
    if (!buf || len <= 0) return;
    buf[0] = '\0';
    if (st->page != MP_PAGE_PICK || st->tab != MP_TAB_JOIN || st->prompt) return;
    if (st->focus == MP_I_ADDR) {
        snprintf(buf, (size_t)len, "%s", MP_JOIN_L3);
        return;
    }
    if (st->rowsel >= 0 && st->rowsel < st->rowcount) {
        const MP_Row *r = &st->rows[st->rowsel];
        char where[MP_SAY_ADDR_MAX + 1];
        const char *src = (r->lan || !r->relay) ? r->addr : r->room;
        int n = (int)strlen(src);
        /* The address or code, cut with ".." if it is longer than the line can hold. */
        if (n > MP_SAY_ADDR_MAX) {
            memcpy(where, src, (size_t)(MP_SAY_ADDR_MAX - 2));
            where[MP_SAY_ADDR_MAX - 2] = '.';
            where[MP_SAY_ADDR_MAX - 1] = '.';
            where[MP_SAY_ADDR_MAX] = '\0';
        } else {
            memcpy(where, src, (size_t)n + 1);
        }
        if (r->why == MP_WHY_BUILD)
            snprintf(buf, (size_t)len, "%s", MP_SAY_BUILD);
        else if (r->why == MP_WHY_FULL)
            snprintf(buf, (size_t)len, "FULL: %d OF %d PLAYERS.", r->players_now, r->players_max);
        else if (r->locked)
            snprintf(buf, (size_t)len, "%s", MP_SAY_LOCKED);
        else if (r->lan)
            snprintf(buf, (size_t)len, "ON THIS NETWORK, %s.", where);
        else if (r->relay)
            snprintf(buf, (size_t)len, "RELAYED. ROOM %s. NO PORTS NEEDED.", where);
        else
            snprintf(buf, (size_t)len, "DIRECT TO %s:%u.", where, (unsigned)r->port);
        return;
    }
    if (st->rowhidden == 1)
        snprintf(buf, (size_t)len, "%s", "1 GAME HIDDEN.");
    else if (st->rowhidden > 1)
        snprintf(buf, (size_t)len, "%d GAMES HIDDEN.", st->rowhidden);
}

/* ---------------------------------------------------------- remembered settings ---- */

static const char *const MP_SORT_WORD[4] = { "game", "map", "ping", "players" };

void mp_prefs_get(const MP_State *st, MP_Prefs *out)
{
    memset(out, 0, sizeof *out);
    out->sortcol = st->sortcol;
    out->sortdesc = st->sortdesc ? 1 : 0;
    out->hide_locked = st->hide_locked ? 1 : 0;
    out->hide_grey = st->hide_grey ? 1 : 0;
    snprintf(out->handle, sizeof out->handle, "%s", st->handle);
}

void mp_prefs_to_save(const MP_State *st, const MP_Prefs *saved, MP_Prefs *out)
{
    mp_prefs_get(st, out);
    if (st->prompt && saved)
        snprintf(out->handle, sizeof out->handle, "%s", saved->handle);
}

void mp_prefs_put(MP_State *st, const MP_Prefs *in)
{
    st->sortcol = mp_clampi(in->sortcol, MP_SORT_GAME, MP_SORT_PLAYERS);
    st->sortdesc = in->sortdesc ? 1 : 0;
    st->hide_locked = in->hide_locked ? 1 : 0;
    st->hide_grey = in->hide_grey ? 1 : 0;
    snprintf(st->handle, sizeof st->handle, "%s", in->handle);
    mp_view(st, MP_VIEW_PLAYER);
}

int mp_prefs_equal(const MP_Prefs *a, const MP_Prefs *b)
{
    return a->sortcol == b->sortcol && a->sortdesc == b->sortdesc
        && a->hide_locked == b->hide_locked && a->hide_grey == b->hide_grey
        && strcmp(a->handle, b->handle) == 0;
}

/* A HAND-EDITABLE FILE IS CHECKED, NOT TRUSTED. Each line is "key value". A value that is
   not a legal one for its key is dropped and the default it would have replaced stands;
   an unknown key, a line with no value, and bytes that are not text are passed over. So a
   truncated or damaged file can only ever put back values it spells correctly, and a
   file that is all damage leaves every default where it was. Nothing is printed: a
   missing or damaged settings file is not something a player needs to be told about. */
int mp_prefs_read(const char *path, MP_Prefs *io)
{
    FILE *f;
    char line[256];
    if (!path || !*path || !io) return 0;
    f = fopen(path, "r");
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        char *k = line, *v, *end;
        int i;
        line[sizeof line - 1] = '\0';
        end = k + strlen(k);
        while (end > k && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t'))
            *--end = '\0';
        while (*k == ' ' || *k == '\t') k++;
        if (*k == '#' || !*k) continue;
        v = k;
        while (*v && *v != ' ' && *v != '\t') v++;
        if (!*v) continue;
        *v++ = '\0';
        while (*v == ' ' || *v == '\t') v++;
        if (!*v) continue;
        if (!strcmp(k, "sort")) {
            for (i = 0; i < 4; i++)
                if (!strcmp(v, MP_SORT_WORD[i])) io->sortcol = i;
        } else if (!strcmp(k, "order")) {
            if (!strcmp(v, "up")) io->sortdesc = 0;
            else if (!strcmp(v, "down")) io->sortdesc = 1;
        } else if (!strcmp(k, "hide_locked") || !strcmp(k, "hide_greyed")) {
            int *dst = !strcmp(k, "hide_locked") ? &io->hide_locked : &io->hide_grey;
            if (!strcmp(v, "0") || !strcmp(v, "1")) *dst = (v[0] == '1');
        } else if (!strcmp(k, "handle")) {
            /* The same rule the YOUR NAME field types by: printable ASCII, and no longer
               than the field holds. */
            int good = (int)strlen(v) < MP_HANDLE_MAX;
            for (i = 0; good && v[i]; i++)
                if ((unsigned char)v[i] < 32 || (unsigned char)v[i] >= 127) good = 0;
            if (good) snprintf(io->handle, sizeof io->handle, "%s", v);
        }
    }
    fclose(f);
    return 1;
}

int mp_prefs_write(const char *path, const MP_Prefs *p)
{
    FILE *f;
    int bad;
    if (!path || !*path || !p) return 0;
    f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "# CNC3D multiplayer: the game list's order and filters, and the player's name.\n"
               "#\n"
               "# Written when one of these changes on the MULTIPLAYER screen. sort is game,\n"
               "# map, ping or players. order is up (A to Z, fewest players, fastest first)\n"
               "# or down. hide_locked and hide_greyed are 0 or 1. handle is the name other\n"
               "# players see, at most %d characters. A value this build cannot read is\n"
               "# ignored. Delete this file to go back to the defaults.\n"
               "#\n", MP_HANDLE_MAX - 1);
    fprintf(f, "sort         %s\n", MP_SORT_WORD[mp_clampi(p->sortcol, 0, 3)]);
    fprintf(f, "order        %s\n", p->sortdesc ? "down" : "up");
    fprintf(f, "hide_locked  %d\n", p->hide_locked ? 1 : 0);
    fprintf(f, "hide_greyed  %d\n", p->hide_grey ? 1 : 0);
    if (p->handle[0]) fprintf(f, "handle       %s\n", p->handle);
    bad = ferror(f);
    if (fclose(f) != 0) bad = 1;
    return !bad;
}

/* ------------------------------------------------------------------- input ---------- */

void mp_motion(MP_State *st, int mx, int my)
{
    st->hover = mp_hit_test(st, mx, my);
    /* A HELD THUMB FOLLOWS THE POINTER, from wherever on the thumb it was taken, so it
       does not jump to put its top under the pointer on the first move. ONLY WHILE ITS
       LIST IS THE THING ON SCREEN: a thumb still marked held on another tab, or behind the
       YOUR NAME box, lost its release somewhere, and is let go rather than left to scroll a
       list nobody is holding. So is one held while a refill shrinks the list until every
       game fits: that list has no thumb left to hold. */
    if (st->drag && (st->page != MP_PAGE_PICK || st->tab != MP_TAB_JOIN || st->prompt
                     || st->rowcount <= MP_LIST_ROWS))
        st->drag = 0;
    if (st->drag) {
        const int total = st->rowcount > MP_LIST_ROWS ? st->rowcount : MP_LIST_ROWS;
        st->rowtop = (my - st->dragdiff - MP_ROWS_Y) * total / MP_ROWS_H;
        mp_clamp_top(st);
    }
}

void mp_release(MP_State *st)
{
    st->pressed = -1;
    st->drag = 0;
}

void mp_scroll(MP_State *st, int delta)
{
    if (st->page != MP_PAGE_PICK || st->tab != MP_TAB_JOIN) return;
    st->rowtop += delta;
    mp_clamp_top(st);
}

/* WHICH FIELD THE CARET IS IN, answered in ONE place. There used to be two of these,
   written out as parallel if-chains -- one inside mp_text and one inside the backspace
   arm of mp_key -- and they had already drifted: MP_I_HANDLE was in the first and not in
   the second, so a player could type their name into the YOUR NAME prompt and had no way
   to correct a typo. Two lists of the same thing is one list too many, and adding a third
   field to both of them by hand is how that bug gets made a second time. */
static char *mp_focused_field(MP_State *st, int *max)
{
    switch (st->focus) {
    case MP_I_HANDLE: *max = MP_HANDLE_MAX; return st->handle;
    case MP_I_NAME:   *max = MP_NAME_MAX;   return st->name;
    case MP_I_PASS:   *max = MP_PASS_MAX;   return st->pass;
    case MP_I_ADDR:   *max = MP_ADDR_MAX;   return st->addr;
    default:          *max = 0;             return NULL;
    }
}

void mp_text(MP_State *st, char ch)
{
    char *f;
    int max, n;
    f = mp_focused_field(st, &max);
    if (!f) return;
    /* DIGITS ONLY IN THE PASSCODE. It is a four digit code, so a letter here is a
       keystroke the other player could never reproduce on the numeric row of their
       keyboard. The rule belongs to the field rather than to the lookup, which is why it
       stays here and not in the helper. */
    if (st->focus == MP_I_PASS && (ch < '0' || ch > '9')) return;
    /* PRINTABLE ASCII ONLY. The menu font has one glyph per code and no notion of UTF-8,
       so a character it cannot draw would be a hole in the field and a byte on the wire
       that the other end renders differently. */
    if (ch < 32 || ch >= 127) return;
    n = (int)strlen(f);
    if (n >= max - 1) return;
    f[n] = ch;
    f[n + 1] = '\0';
}

/* What JOIN is aimed at right now, as mp_press records it into promptkey: the selected
   game's key, or empty when a typed code is the destination. */
static void mp_join_target(const MP_State *st, char *out, int len)
{
    const int row = st->rowsel >= 0 && st->rowsel < st->rowcount;
    snprintf(out, (size_t)len, "%s", row ? st->rows[st->rowsel].key : "");
}

void mp_prompt_close(MP_State *st)
{
    if (!st->prompt) return;
    st->prompt = 0;
    st->promptkey[0] = '\0';
    st->handle[0] = '\0';
    if (st->focus == MP_I_HANDLE) st->focus = -1;
}

/* THE PROMPT'S OK, pressed or typed as Enter. It answers the action the box stood in front
   of, and for JOIN it asks first whether JOIN still means what it meant when the box
   opened: the list refills behind the box ten times a second, so the game may have left
   the list, been held back by a HIDE box, or come back and taken the place of a typed
   code. If it no longer means the same thing, the box shuts, the name is kept, and nothing
   is joined: the player sees the list as it now is and presses JOIN again. */
static int mp_prompt_ok(MP_State *st)
{
    const int act = st->prompt;
    char now[MP_KEY_MAX];
    int same;
    if (!act || !st->handle[0]) return MP_ACT_NONE;   /* OK is drawn dead until it has one */
    mp_join_target(st, now, (int)sizeof now);
    same = !strcmp(now, st->promptkey);
    st->prompt = 0;
    st->promptkey[0] = '\0';
    st->focus = -1;
    if (act == MP_ACT_JOIN && (!same || mp_item_disabled(st, MP_I_JOIN))) return MP_ACT_NONE;
    return act;
}

int mp_key(MP_State *st, int key)
{
    switch (key) {
    case MP_K_BACK: {
        int max;
        char *f = mp_focused_field(st, &max);
        if (f) {
            int n = (int)strlen(f);
            if (n > 0) f[n - 1] = '\0';
        }
        return MP_ACT_NONE;
    }
    case MP_K_TAB:
        /* Walk the text fields, which is what a person expects of Tab on a form. This
           used to test `tab != MP_TAB_HOST` and give up, which was fair while the join
           tab had one field and is not once it has two: Tab would simply do nothing for
           the player typing an address, which is the one player most likely to reach for
           it. Each arm walks only fields that are live, so the caret can never park in a
           box that refuses keystrokes. */
        if (st->page != MP_PAGE_PICK) return MP_ACT_NONE;
        if (st->tab == MP_TAB_HOST) {
            st->focus = (st->focus == MP_I_NAME && st->private_game) ? MP_I_PASS : MP_I_NAME;
            return MP_ACT_NONE;
        }
        if (!mp_item_disabled(st, MP_I_ADDR) && !mp_item_disabled(st, MP_I_PASS))
            st->focus = (st->focus == MP_I_ADDR) ? MP_I_PASS : MP_I_ADDR;
        else if (!mp_item_disabled(st, MP_I_ADDR)) st->focus = MP_I_ADDR;
        else if (!mp_item_disabled(st, MP_I_PASS)) st->focus = MP_I_PASS;
        return MP_ACT_NONE;
    case MP_K_ESC:
        /* THE BOX FIRST. It is modal, so ESC is an answer to it, not to the screen behind
           it: a player who changes their mind about typing a name is not asking to leave. */
        if (st->prompt) { mp_prompt_close(st); return MP_ACT_NONE; }
        if (st->focus >= 0) { st->focus = -1; return MP_ACT_NONE; }
        return (st->page == MP_PAGE_WAIT) ? MP_ACT_LEAVE : MP_ACT_CANCEL;
    case MP_K_ENTER:
        /* Enter in the box is its OK, and never the HOST or JOIN behind it, which would
           otherwise go ahead with no name while the box still asked for one. */
        if (st->prompt) return mp_prompt_ok(st);
        if (st->page == MP_PAGE_WAIT) {
            if (st->wait_ishost) return mp_item_disabled(st, MP_I_START) ? MP_ACT_NONE : MP_ACT_START;
            return MP_ACT_READY;
        }
        if (st->tab == MP_TAB_HOST) return MP_ACT_HOST;
        return mp_item_disabled(st, MP_I_JOIN) ? MP_ACT_NONE : MP_ACT_JOIN;
    /* THE LIST KEYS. Every one of them selects a row by its key, clears the status line
       and scrolls the least distance that shows the row, so the selection a key makes is
       always one the player can see.

       NOT WHILE ANYTHING ELSE HAS THE KEYBOARD. Behind the YOUR NAME box they would change
       which game the box's OK joins while the box hides the list; in a text field, Home and
       End belong to the field. Either way a key would select a game the player never
       looked at, and on the internet list a selected direct game is one this machine then
       sends its address to. ESC, or a click on a row, gives the keys back to the list. */
    case MP_K_UP:
    case MP_K_DOWN:
    case MP_K_PGUP:
    case MP_K_PGDN:
    case MP_K_HOME:
    case MP_K_END:
        if (st->prompt || st->focus >= 0) return MP_ACT_NONE;
        if (st->page == MP_PAGE_PICK && st->tab == MP_TAB_JOIN && st->rowcount > 0) {
            int r = st->rowsel;
            switch (key) {
            case MP_K_UP:   r = (r < 0) ? 0 : r - 1; break;
            case MP_K_DOWN: r = (r < 0) ? 0 : r + 1; break;
            case MP_K_PGUP: r = (r < 0) ? 0 : r - MP_LIST_ROWS; break;
            case MP_K_PGDN: r = (r < 0) ? 0 : r + MP_LIST_ROWS; break;
            case MP_K_HOME: r = 0; break;
            default:        r = st->rowcount - 1; break;
            }
            mp_select(st, mp_clampi(r, 0, st->rowcount - 1));
            mp_show_sel(st);
        }
        return MP_ACT_NONE;
    default:
        return MP_ACT_NONE;
    }
}

static int mp_item_sortcol(int item)
{
    switch (item) {
    case MP_I_COL_MAP:     return MP_SORT_MAP;
    case MP_I_COL_PING:    return MP_SORT_PING;
    case MP_I_COL_PLAYERS: return MP_SORT_PLAYERS;
    default:               return MP_SORT_GAME;
    }
}

int mp_press(MP_State *st, int mx, int my)
{
    const int it = mp_hit_test(st, mx, my);
    const int row = mp_row_at(st, mx, my);

    /* A PRESS WHILE THE THUMB IS STILL HELD means its release never arrived: the button
       went up outside this screen's loop. Whatever was held is let go first, and a press
       on the thumb takes hold of it again below. */
    st->drag = 0;

    if (row >= 0) {
        mp_select(st, row);
        st->focus = -1;
        return MP_ACT_NONE;
    }
    if (it < 0) { st->focus = -1; return MP_ACT_NONE; }
    st->pressed = it;
    if (mp_item_disabled(st, it)) return MP_ACT_NONE;

    switch (it) {
    case MP_I_TAB_HOST: st->tab = MP_TAB_HOST; st->focus = -1; return MP_ACT_NONE;
    case MP_I_TAB_JOIN:
        st->tab = MP_TAB_JOIN;
        st->focus = -1;
        mp_forget_selection(st);
        return MP_ACT_REFRESH;
    case MP_I_NAME:     st->focus = MP_I_NAME; return MP_ACT_NONE;
    case MP_I_PRIVATE:
        st->private_game = !st->private_game;
        if (!st->private_game) st->pass[0] = '\0';
        st->focus = st->private_game ? MP_I_PASS : -1;
        return MP_ACT_NONE;
    case MP_I_PASS:     st->focus = MP_I_PASS; return MP_ACT_NONE;
    case MP_I_ADDR:     st->focus = MP_I_ADDR; return MP_ACT_NONE;
    case MP_I_RELAY:
        /* AN INTERNET GAME MAY BE OPEN. PRIVATE GAME is left exactly where the player
           put it: a game meant to be found by strangers is the ordinary case, and the
           passcode is for the other one. */
        st->relay_game = !st->relay_game;
        /* AND THE ADDRESS IS NEVER LISTED BY INHERITANCE. Ticking the relay hides the
           box below, so a tick left in it would be a decision the player made about a
           different kind of room and can no longer see. It is dropped rather than
           remembered, which costs one more click and cannot publish an address by
           accident. */
        if (st->relay_game) st->list_game = 0;
        return MP_ACT_NONE;
    case MP_I_PUBLIC:
        st->list_game = !st->list_game;
        return MP_ACT_NONE;
    case MP_I_HOST:
        st->focus = -1;
        /* ASKED ONCE. A handle remembered from last time goes straight through; a first
           run stops here and asks, which is the whole of the request. */
        if (!st->handle[0]) {
            st->prompt = MP_ACT_HOST;
            st->promptkey[0] = '\0';
            st->focus = MP_I_HANDLE;
            return MP_ACT_NONE;
        }
        return MP_ACT_HOST;
    case MP_I_HANDLE:  st->focus = MP_I_HANDLE; return MP_ACT_NONE;
    case MP_I_HANDLE_OK: return mp_prompt_ok(st);
    /* REFRESH, not NONE, and the difference is a live button that joins something the
       player cannot see. MP_ACT_REFRESH is what empties the row list in the shell;
       without it, switching to INTERNET replaced the LAN list on screen and left rowsel
       and rowcount exactly as they were. JOIN then tested a row that was no longer drawn,
       drew itself live, and joined a LAN game the player was not looking at -- and a
       frozen one, because the browser stops being refilled on this sub-tab. The key goes
       too, or the first refill of the other list would select a game by it. */
    case MP_I_NET_LAN: st->net = MP_NET_LAN;      mp_forget_selection(st); return MP_ACT_REFRESH;
    case MP_I_NET_NET: st->net = MP_NET_INTERNET; mp_forget_selection(st); return MP_ACT_REFRESH;
    case MP_I_REFRESH: mp_forget_selection(st); return MP_ACT_REFRESH;
    case MP_I_JOIN:
        if (!st->handle[0]) {
            st->prompt = MP_ACT_JOIN;
            mp_join_target(st, st->promptkey, (int)sizeof st->promptkey);
            st->focus = MP_I_HANDLE;
            return MP_ACT_NONE;
        }
        return MP_ACT_JOIN;
    case MP_I_READY:   return MP_ACT_READY;
    case MP_I_START:   return MP_ACT_START;
    case MP_I_LEAVE:   return MP_ACT_LEAVE;
    case MP_I_CANCEL:  return MP_ACT_CANCEL;
    /* A HEADER: its column in its natural order, or the other way if it already is the
       sort. Natural is A to Z for names and most players first for PLAYERS, which is the
       order a player scanning for a busy game wants; PING starts fastest first. */
    case MP_I_COL_GAME:
    case MP_I_COL_MAP:
    case MP_I_COL_PING:
    case MP_I_COL_PLAYERS: {
        const int col = mp_item_sortcol(it);
        if (st->sortcol == col) {
            st->sortdesc = !st->sortdesc;
        } else {
            st->sortcol = col;
            st->sortdesc = (col == MP_SORT_PLAYERS);
        }
        mp_view(st, MP_VIEW_PLAYER);
        return MP_ACT_NONE;
    }
    /* THE TRACK PAGES AND THE THUMB DRAGS. Neither changes the selection or the caret:
       scrolling a list is looking at it, not choosing from it. With nothing to scroll there
       is no thumb, and the track does nothing at all: no page and nothing taken hold of. */
    case MP_I_LIST_BAR: {
        int ty, th;
        mp_bar_thumb(st, &ty, &th);
        if (th <= 0) return MP_ACT_NONE;
        if (my < ty) {
            st->rowtop -= MP_LIST_ROWS;
        } else if (my >= ty + th) {
            st->rowtop += MP_LIST_ROWS;
        } else {
            st->drag = 1;
            st->dragdiff = my - ty;
        }
        mp_clamp_top(st);
        return MP_ACT_NONE;
    }
    case MP_I_HIDE_LOCKED:
        st->hide_locked = !st->hide_locked;
        mp_view(st, MP_VIEW_PLAYER);
        return MP_ACT_NONE;
    case MP_I_HIDE_GREY:
        st->hide_grey = !st->hide_grey;
        mp_view(st, MP_VIEW_PLAYER);
        return MP_ACT_NONE;
    default:           return MP_ACT_NONE;
    }
}

/* ------------------------------------------------------------------- drawing -------- */

/* db_print wants a FONT PALETTE, not a colour: the 1995 text routine maps each nibble
   class through sixteen entries, which is how one glyph gets its body, its shadow and its
   outline. db_font_palette builds the plain one, and this wraps it so every call site
   below reads as a colour and there is one place that knows the difference. */
static const unsigned char *mp_pal(int colour)
{
    static unsigned char fp[16];
    /* THE GRAD VARIANT AND A TRANSPARENT BACK, which is what the rest of this menu uses
       and is not interchangeable with the plain one. GRAD6FNT spreads a glyph across
       several nibble classes, so a palette built by db_font_palette leaves most of those
       classes on the background colour and the text draws as nothing at all: the boxes
       appear, correctly placed, and every label is invisible. That is exactly what the
       first render of this screen looked like. */
    db_font_palette_grad(fp, (unsigned char)colour, DB_TBLACK);
    return fp;
}

static const char *mp_label(const MP_State *st, int item)
{
    switch (item) {
    case MP_I_TAB_HOST: return "HOST GAME";
    case MP_I_TAB_JOIN: return "JOIN GAME";
    case MP_I_PRIVATE:  return "PRIVATE GAME";
    case MP_I_HOST:     return "HOST";
    case MP_I_NET_LAN:  return "LAN";
    case MP_I_NET_NET:  return "INTERNET";
    case MP_I_RELAY:    return "";
    case MP_I_PUBLIC:   return "";
    case MP_I_ADDR:     return "";
    case MP_I_REFRESH:  return "REFRESH";
    case MP_I_JOIN:     return "JOIN";
    case MP_I_READY:    return st->wait_ready[st->wait_myseat < 0 ? 0 : st->wait_myseat]
                                   ? "NOT READY" : "READY";
    case MP_I_START:    return "START GAME";
    case MP_I_LEAVE:    return "LEAVE";
    case MP_I_CANCEL:   return "CANCEL";
    default:            return "";
    }
}

/* The header's words, and where each starts. GAME lines up with the room names under it,
   which start past the padlock; the others start MP_CELL_IN into their cell. */
static const char *mp_head_label(int item)
{
    switch (item) {
    case MP_I_COL_MAP:     return "MAP";
    case MP_I_COL_PING:    return "PING";
    case MP_I_COL_PLAYERS: return "PLAYERS";
    default:               return "GAME";
    }
}

static int mp_head_text_x(int item)
{
    switch (item) {
    case MP_I_COL_MAP:     return MP_HEAD_MAP_X + MP_CELL_IN;
    case MP_I_COL_PING:    return MP_HEAD_PING_X + MP_CELL_IN;
    case MP_I_COL_PLAYERS: return MP_HEAD_PLR_X + MP_CELL_IN;
    default:               return MP_LIST_X + MP_COL_NAME;
    }
}

/* THE SORT DIRECTION beside the active header: a triangle five pixels wide and three
   tall, pointing up for ascending and down for descending, drawn in the label's colour.
   It is drawn rather than printed because the menu font's own ^ is three pixels of ink
   at this size, which reads as a speck beside five-row capitals. Horizontal lines on the
   same 8-bit surface as the text, so it costs the fixed-function build nothing. */
#define MP_MARK_W 5
static void mp_sort_mark(DB_Surface *s, int x, int y, int desc, unsigned char colour)
{
    int i;
    for (i = 0; i < 3; i++) {
        const int half = desc ? 2 - i : i;    /* rows of 1, 3, 5 or of 5, 3, 1 */
        db_line_h(s, x + 2 - half, x + 2 + half, y + i, colour);
    }
}

static void mp_button(DB_Surface *s, const DB_Font *f, const char *label,
                      int x, int y, int w, int h, int pressed, int disabled)
{
    int tw;
    dm_green_button(s, x, y, w, h, pressed, disabled);
    tw = db_string_width(f, label, DB_FONT6_XSPACING);
    db_print(s, f, label, x + (w - tw) / 2 + (pressed ? 1 : 0),
             y + (h - 6) / 2 + (pressed ? 1 : 0),
             mp_pal(disabled ? DM_TEXT_DISABLED : DM_TEXT_BRIGHT), DB_FONT6_XSPACING);
}

/* A text field: a sunken box, the text, and a caret when it has focus. `hide` prints
   asterisks, which is the whole of what a password field is on a screen like this. */
static void mp_field(DB_Surface *s, const DB_Font *f, const char *text,
                     int x, int y, int w, int h, int focused, int caret, int hide)
{
    char shown[MP_FIELD_MAX + 1];
    int i, n, tw;
    db_fill_rect(s, x, y, x + w - 1, y + h - 1, DM_GREEN_SHADOW);
    db_line_h(s, x, x + w - 1, y, DM_DIS_SHADOW);
    db_line_v(s, x, y, y + h - 1, DM_DIS_SHADOW);
    db_line_h(s, x, x + w - 1, y + h - 1, DM_LIGHT_GREEN);
    db_line_v(s, x + w - 1, y, y + h - 1, DM_LIGHT_GREEN);

    n = (int)strlen(text);
    /* KEEP THE TAIL, NOT THE HEAD. This clamped to the FIRST MP_NAME_MAX characters, so
       past thirty-two the scroll below had nothing left to scroll to and the field clipped
       the end again -- the exact failure the comment under it exists to prevent, arriving
       silently at a length nothing on the screen used to reach. An address does reach it.
       The buffer is sized by the widest field on the screen, so no caller can overrun it
       by being longer than the one this was written for. */
    if (n > MP_FIELD_MAX) { text += n - MP_FIELD_MAX; n = MP_FIELD_MAX; }
    for (i = 0; i < n; i++) shown[i] = hide ? '*' : text[i];
    shown[n] = '\0';
    /* SCROLL FROM THE RIGHT once the text is longer than the box: a field that clips the
       END hides exactly the character just typed, which makes it look like typing stopped
       working. */
    tw = db_string_width(f, shown, DB_FONT6_XSPACING);
    while (tw > w - 8 && shown[0]) {
        memmove(shown, shown + 1, strlen(shown));
        tw = db_string_width(f, shown, DB_FONT6_XSPACING);
    }
    db_print(s, f, shown, x + 3, y + (h - 6) / 2, mp_pal(DM_TEXT_BRIGHT), DB_FONT6_XSPACING);
    if (focused && caret) {
        db_fill_rect(s, x + 3 + tw + 1, y + 2, x + 3 + tw + 1, y + h - 3, DM_TEXT_BRIGHT);
    }
}

static void mp_tab(DB_Surface *s, const DB_Font *f, const char *label,
                   int x, int y, int w, int h, int on)
{
    db_fill_rect(s, x, y, x + w - 1, y + h - 1, on ? DM_GREEN_BKGD : DM_GREEN_SHADOW);
    db_line_h(s, x, x + w - 1, y, on ? DM_LIGHT_GREEN : DM_GREEN_BKGD);
    db_line_v(s, x, y, y + h - 1, on ? DM_LIGHT_GREEN : DM_GREEN_BKGD);
    db_line_v(s, x + w - 1, y, y + h - 1, DM_DIS_SHADOW);
    {
        const int tw = db_string_width(f, label, DB_FONT6_XSPACING);
        db_print(s, f, label, x + (w - tw) / 2, y + (h - 6) / 2,
                 mp_pal(on ? DM_TEXT_BRIGHT : DM_TEXT_MEDIUM), DB_FONT6_XSPACING);
    }
}

/* A HEADER CELL, in the sub-tabs' own two styles: the sort column is drawn "on" and the
   rest "off", so the header reads as the same kind of control as LAN and INTERNET. The
   label is left-aligned over its column rather than centred, and the active one carries
   the direction marker two pixels after it. */
static void mp_head(DB_Surface *s, const DB_Font *f, const MP_State *st, int item)
{
    int x, y, w, h;
    const int on = (st->sortcol == mp_item_sortcol(item));
    const char *label = mp_head_label(item);
    const int lx = mp_head_text_x(item);
    const unsigned char *fp = mp_pal(on ? DM_TEXT_BRIGHT : DM_TEXT_MEDIUM);
    if (!mp_item_rect(st, item, &x, &y, &w, &h)) return;
    db_fill_rect(s, x, y, x + w - 1, y + h - 1, on ? DM_GREEN_BKGD : DM_GREEN_SHADOW);
    db_line_h(s, x, x + w - 1, y, on ? DM_LIGHT_GREEN : DM_GREEN_BKGD);
    db_line_v(s, x, y, y + h - 1, on ? DM_LIGHT_GREEN : DM_GREEN_BKGD);
    db_line_v(s, x + w - 1, y, y + h - 1, DM_DIS_SHADOW);
    db_print(s, f, label, lx, y + (h - 6) / 2, fp, DB_FONT6_XSPACING);
    /* Two pixels after the label, and on the middle three of the capitals' five rows. */
    if (on) {
        mp_sort_mark(s, lx + db_string_width(f, label, DB_FONT6_XSPACING) + 2, y + 3,
                     st->sortdesc, DM_TEXT_BRIGHT);
    }
}

/* A checkbox exactly as PRIVATE GAME draws one, with its word beside it. */
static void mp_check(DB_Surface *s, const DB_Font *f, const char *word, int x, int y, int on)
{
    db_fill_rect(s, x, y, x + MP_HIDE_BOX - 1, y + MP_HIDE_BOX - 1, DM_GREEN_SHADOW);
    db_line_h(s, x, x + MP_HIDE_BOX - 1, y, DM_DIS_SHADOW);
    db_line_v(s, x, y, y + MP_HIDE_BOX - 1, DM_DIS_SHADOW);
    if (on) db_fill_rect(s, x + 2, y + 2, x + MP_HIDE_BOX - 3, y + MP_HIDE_BOX - 3, DM_LIGHT_GREEN);
    db_print(s, f, word, x + MP_HIDE_TXT, y + 1, mp_pal(DM_TEXT_MEDIUM), DB_FONT6_XSPACING);
}

/* THE ROWS, the empty-list sentences, and the scroll bar. */
static void mp_draw_list(DB_Surface *s, const DB_Font *f, const MP_State *st)
{
    char line[128], cut[MP_NAME_MAX + 1];
    int i;

    db_fill_rect(s, MP_LIST_X, MP_LIST_Y, MP_LIST_X + MP_LIST_W - 1,
                 MP_ROWS_Y + MP_ROWS_H - 1, DM_GREEN_SHADOW);
    for (i = MP_I_COL_GAME; i <= MP_I_COL_PLAYERS; i++)
        mp_head(s, f, st, i);

    if (st->rowcount == 0) {
        const int x = MP_LIST_X + 6;
        if (st->rowhidden > 0) {
            /* THE HIDE BOXES EMPTIED IT, and a list that looks empty for a reason nobody
               can see is the failure every greyed row on this screen exists to avoid. */
            snprintf(line, sizeof line, "NO GAMES TO SHOW: %d HIDDEN.", st->rowhidden);
            db_print(s, f, line, x, MP_ROWS_Y + 6, mp_pal(DM_TEXT_MEDIUM), DB_FONT6_XSPACING);
            db_print(s, f, MP_HIDDEN_L2, x, MP_ROWS_Y + 16, mp_pal(DM_TEXT_DISABLED), DB_FONT6_XSPACING);
        } else if (st->net == MP_NET_INTERNET) {
            db_print(s, f, MP_JOIN_L1, x, MP_ROWS_Y + 6, mp_pal(DM_TEXT_MEDIUM), DB_FONT6_XSPACING);
            db_print(s, f, MP_JOIN_L2, x, MP_ROWS_Y + 16, mp_pal(DM_TEXT_DISABLED), DB_FONT6_XSPACING);
            /* Both ways in are real and neither replaces the other: the list finds a game
               among strangers, and the field takes a code somebody read out. */
            db_print(s, f, MP_JOIN_L3, x, MP_ROWS_Y + 30, mp_pal(DM_TEXT_DISABLED), DB_FONT6_XSPACING);
        } else {
            db_print(s, f, MP_LAN_L1, x, MP_ROWS_Y + 6, mp_pal(DM_TEXT_MEDIUM), DB_FONT6_XSPACING);
            db_print(s, f, MP_LAN_L2, x, MP_ROWS_Y + 16, mp_pal(DM_TEXT_DISABLED), DB_FONT6_XSPACING);
        }
    }

    for (i = 0; i < MP_LIST_ROWS; i++) {
        const int r = st->rowtop + i;
        const int ry = MP_ROWS_Y + i * MP_LIST_RH;
        const MP_Row *g;
        const unsigned char *fp;
        if (r < 0 || r >= st->rowcount) break;
        g = &st->rows[r];
        if (r == st->rowsel) {
            db_fill_rect(s, MP_LIST_X, ry, MP_LIST_X + MP_ROWS_W - 1,
                         ry + MP_LIST_RH - 1, DM_GREEN_BKGD);
        }
        fp = mp_pal(g->joinable ? DM_TEXT_BRIGHT : DM_TEXT_DISABLED);
        if (g->locked)
            db_print(s, f, "*", MP_LIST_X + MP_COL_LOCK, ry + 1, fp, DB_FONT6_XSPACING);
        /* NAMES ARE CUT BY THE LOBBY'S RULE, which says a name was cut rather than ending
           it mid-word where it would read as the real one. */
        sk_cut_to_width(f, g->name, MP_NAME_TW, cut, (int)sizeof cut);
        db_print(s, f, cut, MP_LIST_X + MP_COL_NAME, ry + 1, fp, DB_FONT6_XSPACING);
        sk_cut_to_width(f, g->map, MP_MAP_TW, cut, (int)sizeof cut);
        db_print(s, f, cut, MP_HEAD_MAP_X + MP_CELL_IN, ry + 1, fp, DB_FONT6_XSPACING);
        mp_row_ping_text(g, line, (int)sizeof line);
        db_print(s, f, line, MP_HEAD_PING_X + MP_CELL_IN, ry + 1, fp, DB_FONT6_XSPACING);
        snprintf(line, sizeof line, "%d/%d", g->players_now, g->players_max);
        db_print(s, f, line, MP_HEAD_PLR_X + MP_CELL_IN, ry + 1, fp, DB_FONT6_XSPACING);
    }

    /* THE BAR: a sunken well, lit on the bottom and right, and, only while there are more
       shown games than rows, a thumb that is a plate of the screen's lighter green, the one
       its buttons and the active header are edged with. Not the screen's button plate: that
       is filled with the well's own green, so only a one pixel edge told thumb from well and
       the thumb was hard to find. The plate keeps a bevel in the darkest green, on its
       bottom and right while it rests and on its top and left while it is held. With every
       shown game in view the well is drawn empty: a thumb that filled it read as a border,
       not as a control. */
    {
        const int bx = MP_BAR_X, by = MP_ROWS_Y;
        int ty, th;
        db_fill_rect(s, bx, by, bx + MP_BAR_W - 1, by + MP_ROWS_H - 1, DM_GREEN_BKGD);
        db_line_h(s, bx, bx + MP_BAR_W - 1, by, DM_GREEN_SHADOW);
        db_line_v(s, bx, by, by + MP_ROWS_H - 1, DM_GREEN_SHADOW);
        db_line_h(s, bx, bx + MP_BAR_W - 1, by + MP_ROWS_H - 1, DM_LIGHT_GREEN);
        db_line_v(s, bx + MP_BAR_W - 1, by, by + MP_ROWS_H - 1, DM_LIGHT_GREEN);
        mp_bar_thumb(st, &ty, &th);
        if (th > 0) {
            db_fill_rect(s, bx, ty, bx + MP_BAR_W - 1, ty + th - 1, DM_LIGHT_GREEN);
            if (st->drag) {
                db_line_h(s, bx, bx + MP_BAR_W - 1, ty, DM_GREEN_SHADOW);
                db_line_v(s, bx, ty, ty + th - 1, DM_GREEN_SHADOW);
            } else {
                db_line_h(s, bx, bx + MP_BAR_W - 1, ty + th - 1, DM_GREEN_SHADOW);
                db_line_v(s, bx + MP_BAR_W - 1, ty, ty + th - 1, DM_GREEN_SHADOW);
            }
        }
    }
}

void mp_draw(DB_Surface *s, const DB_Pack *p, const MP_State *st)
{
    const DB_Font *f = db_font(p, "GRAD6FNT");
    int i, x, y, w, h;
    char line[128];

    if (!f) f = db_font(p, "6POINT");
    dm_green_dialog(s, MPX, MPY, MPW, MPH);

    /* caption */
    {
        const char *cap = (st->page == MP_PAGE_WAIT) ? st->name : "MULTIPLAYER";
        const int tw = db_string_width(f, cap, DB_FONT6_XSPACING);
        db_print(s, f, cap, MPX + (MPW - tw) / 2, MP_CAP_Y, mp_pal(DM_TEXT_BRIGHT), DB_FONT6_XSPACING);
    }

    if (st->page == MP_PAGE_PICK) {
        mp_tab(s, f, mp_label(st, MP_I_TAB_HOST), MP_TAB0_X, MP_TAB_Y, MP_TAB_W,
               MP_TAB_H, st->tab == MP_TAB_HOST);
        mp_tab(s, f, mp_label(st, MP_I_TAB_JOIN), MP_TAB1_X, MP_TAB_Y, MP_TAB_W,
               MP_TAB_H, st->tab == MP_TAB_JOIN);
    }

    if (st->page == MP_PAGE_PICK && st->tab == MP_TAB_HOST) {
        db_print(s, f, "GAME NAME", MP_LBL_X, MP_NAME_Y + 2, mp_pal(DM_TEXT_MEDIUM), DB_FONT6_XSPACING);
        mp_item_rect(st, MP_I_NAME, &x, &y, &w, &h);
        mp_field(s, f, st->name, x, y, w, h, st->focus == MP_I_NAME, st->caret_on, 0);

        db_print(s, f, "PRIVATE GAME", MP_LBL_X, MP_PRIV_Y + 1, mp_pal(DM_TEXT_MEDIUM), DB_FONT6_XSPACING);
        mp_item_rect(st, MP_I_PRIVATE, &x, &y, &w, &h);
        db_fill_rect(s, x, y, x + w - 1, y + h - 1, DM_GREEN_SHADOW);
        db_line_h(s, x, x + w - 1, y, DM_DIS_SHADOW);
        db_line_v(s, x, y, y + h - 1, DM_DIS_SHADOW);
        if (st->private_game) {
            db_fill_rect(s, x + 2, y + 2, x + w - 3, y + h - 3, DM_LIGHT_GREEN);
        }

        {
            /* "INTERNET GAME" and not "PLAY OVER THE INTERNET", because the checkbox
               column is shared with PRIVATE GAME above it and a longer label runs UNDER
               the box rather than stopping short of it. Thirteen characters is what fits
               between MP_LBL_X and MP_PRIV_X at six pixels a glyph, and it reads as the
               pair it is: PRIVATE GAME, INTERNET GAME. */
            const char *cap = "INTERNET GAME";
            db_print(s, f, cap, MP_LBL_X, MP_RELAY_Y + 1,
                     mp_pal(DM_TEXT_MEDIUM), DB_FONT6_XSPACING);
            mp_item_rect(st, MP_I_RELAY, &x, &y, &w, &h);
            db_fill_rect(s, x, y, x + w - 1, y + h - 1, DM_GREEN_SHADOW);
            db_line_h(s, x, x + w - 1, y, DM_DIS_SHADOW);
            db_line_v(s, x, y, y + h - 1, DM_DIS_SHADOW);
            if (st->relay_game)
                db_fill_rect(s, x + 2, y + 2, x + w - 3, y + h - 3, DM_LIGHT_GREEN);
            /* Both sentences are measured by mp_check_layout, which free text on this
               screen was not until one of them ran off the plate. */
            db_print(s, f,
                     st->relay_game ? MP_RELAY_ON_TEXT : MP_RELAY_OFF_TEXT,
                     MP_LBL_X, MP_RELAY_Y + 12, mp_pal(DM_TEXT_DISABLED), DB_FONT6_XSPACING);
        }

        /* LIST PUBLICLY, and the whole reason it is a separate box rather than something
           HOST decides: publishing an unrelayed room means publishing the address it
           answers on, to everybody who opens the browser and not only to whoever joins.
           Thirteen characters, which is what fits between the label column and the
           checkbox column, and it reads as the third of a set: PRIVATE GAME, INTERNET
           GAME, LIST PUBLICLY. */
        if (mp_item_visible(st, MP_I_PUBLIC)) {
            db_print(s, f, "LIST PUBLICLY", MP_LBL_X, MP_PUBLIC_Y + 1,
                     mp_pal(DM_TEXT_MEDIUM), DB_FONT6_XSPACING);
            mp_item_rect(st, MP_I_PUBLIC, &x, &y, &w, &h);
            db_fill_rect(s, x, y, x + w - 1, y + h - 1, DM_GREEN_SHADOW);
            db_line_h(s, x, x + w - 1, y, DM_DIS_SHADOW);
            db_line_v(s, x, y, y + h - 1, DM_DIS_SHADOW);
            if (st->list_game)
                db_fill_rect(s, x + 2, y + 2, x + w - 3, y + h - 3, DM_LIGHT_GREEN);
            db_print(s, f,
                     st->list_game ? MP_PUBLIC_ON_TEXT : MP_PUBLIC_OFF_TEXT,
                     MP_LBL_X, MP_PUBLIC_Y + 12, mp_pal(DM_TEXT_DISABLED),
                     DB_FONT6_XSPACING);
        }

        db_print(s, f, "PASSCODE", MP_LBL_X, MP_PASS_Y + 2, mp_pal(st->private_game ? DM_TEXT_MEDIUM : DM_TEXT_DISABLED), DB_FONT6_XSPACING);
        mp_item_rect(st, MP_I_PASS, &x, &y, &w, &h);
        if (st->private_game) {
            mp_field(s, f, st->pass, x, y, w, h, st->focus == MP_I_PASS, st->caret_on, 0);
            db_print(s, f, "4 DIGITS", x + w + 6, y + 2, mp_pal(DM_TEXT_DISABLED), DB_FONT6_XSPACING);
        } else {
            db_fill_rect(s, x, y, x + w - 1, y + h - 1, DM_DIS_FILL);
            db_print(s, f, "ANYONE MAY JOIN", x + w + 6, y + 2, mp_pal(DM_TEXT_DISABLED), DB_FONT6_XSPACING);
        }

    }

    if (st->page == MP_PAGE_PICK && st->tab == MP_TAB_JOIN) {
        mp_tab(s, f, mp_label(st, MP_I_NET_LAN), MP_NET0_X, MP_NET_Y, MP_NET_W,
               MP_NET_H, st->net == MP_NET_LAN);
        mp_tab(s, f, mp_label(st, MP_I_NET_NET), MP_NET1_X, MP_NET_Y, MP_NET_W,
               MP_NET_H, st->net == MP_NET_INTERNET);

        /* HIDE [ ]LOCKED [ ]GREYED, on both sub-tabs: a ticked box is in view beside the
           list it is thinning. */
        db_print(s, f, "HIDE", MP_HIDE_CAP_X, MP_NET_Y + 1, mp_pal(DM_TEXT_MEDIUM), DB_FONT6_XSPACING);
        mp_check(s, f, "LOCKED", MP_HIDE0_X, MP_NET_Y, st->hide_locked);
        mp_check(s, f, "GREYED", MP_HIDE1_X, MP_NET_Y, st->hide_grey);

        mp_draw_list(s, f, st);

        /* THE FIELD LINE. ROOM CODE on the internet sub-tab, where a player handed a code
           must still be able to type it, list or no list. */
        if (mp_item_rect(st, MP_I_ADDR, &x, &y, &w, &h)) {
            db_print(s, f, "ROOM CODE", MP_LBL_X, y + 2, mp_pal(DM_TEXT_MEDIUM), DB_FONT6_XSPACING);
            mp_field(s, f, st->addr, x, y, w, h, st->focus == MP_I_ADDR, st->caret_on, 0);
        }
        /* THE PASSCODE, the same item as the host tab's, so the screen has one passcode
           field and not two that can disagree. An open row greys the box rather than
           hiding it, and a locked row's status line says what to type in it. */
        if (mp_item_rect(st, MP_I_PASS, &x, &y, &w, &h)) {
            const int shut = mp_item_disabled(st, MP_I_PASS);
            db_print(s, f, "PASSCODE", MP_JPASS_LBL_X, y + 2,
                     mp_pal(shut ? DM_TEXT_DISABLED : DM_TEXT_MEDIUM), DB_FONT6_XSPACING);
            if (!shut)
                mp_field(s, f, st->pass, x, y, w, h, st->focus == MP_I_PASS, st->caret_on, 0);
            else
                db_fill_rect(s, x, y, x + w - 1, y + h - 1, DM_DIS_FILL);
        }
    }

    if (st->page == MP_PAGE_WAIT) {
        /* THE HOST READS THIS OUT. The other half of the address field on the join tab:
           one player types an address and one has to be able to say what it is. On a LAN
           this is the whole answer; from the internet it is the machine the router has to
           be pointed at, which is why the port is spelled out beside it rather than left
           implied. Only the host sees it -- a joiner's own address is of no use to
           anybody. */
        if (st->wait_ishost) {
            /* BELOW EVERY SEAT, not above the list: the room's name is printed across the
               top of the plate and anything put up there lands on top of it. Measured
               from the LAST possible seat rather than from the number of players in this
               room, so an eight-seat room cannot grow down into it. */
            const int ay = MP_SEAT_Y + MP_MAX_SEATS * MP_SEAT_RH + 10;
            if (st->myaddr[0]) {
                db_print(s, f, "OTHERS TYPE THIS ADDRESS:", MP_LBL_X, ay,
                         mp_pal(DM_TEXT_DISABLED), DB_FONT6_XSPACING);
                db_print(s, f, st->myaddr, MP_LBL_X, ay + 10,
                         mp_pal(DM_TEXT_BRIGHT), DB_FONT6_XSPACING);
                db_print(s, f, "FROM OUTSIDE, FORWARD THIS UDP PORT.",
                         MP_LBL_X, ay + 20, mp_pal(DM_TEXT_DISABLED), DB_FONT6_XSPACING);
            } else {
                db_print(s, f, "COULD NOT READ THIS MACHINE'S ADDRESS.", MP_LBL_X,
                         ay, mp_pal(DM_TEXT_DISABLED), DB_FONT6_XSPACING);
            }
        }
        db_print(s, f, "PLAYER", MP_SEAT_X, MP_SEAT_Y - 10, mp_pal(DM_TEXT_MEDIUM), DB_FONT6_XSPACING);
        db_print(s, f, "READY", MP_SEAT_X + MP_SEAT_READY, MP_SEAT_Y - 10, mp_pal(DM_TEXT_MEDIUM), DB_FONT6_XSPACING);
        for (i = 0; i < st->wait_humans && i < MP_MAX_SEATS; i++) {
            const int ry = MP_SEAT_Y + i * MP_SEAT_RH;
            const int me = (i == st->wait_myseat);
            if (me) {
                db_fill_rect(s, MP_SEAT_X - 3, ry - 1, MP_SEAT_X + MP_SEAT_W - 1,
                             ry + MP_SEAT_RH - 2, DM_GREEN_BKGD);
            }
            if (!st->wait_taken[i]) {
                /* "OPEN SLOT" is the Remastered wording and it is better than a dash: it
                   says the game is waiting for somebody rather than that a row is broken. */
                sprintf(line, "%d.  OPEN SLOT", i + 1);
                db_print(s, f, line, MP_SEAT_X, ry, mp_pal(DM_TEXT_DISABLED), DB_FONT6_XSPACING);
                continue;
            }
            sprintf(line, "%d.  %s%s", i + 1, i == 0 ? "HOST" : "PLAYER", me ? "  (YOU)" : "");
            db_print(s, f, line, MP_SEAT_X, ry, mp_pal(DM_TEXT_BRIGHT), DB_FONT6_XSPACING);
            db_print(s, f, st->wait_ready[i] ? "YES" : "-",
                     MP_SEAT_X + MP_SEAT_READY, ry, mp_pal(st->wait_ready[i] ? DM_TEXT_BRIGHT : DM_TEXT_DISABLED), DB_FONT6_XSPACING);
        }
    }

    /* THE STATUS LINE, on every page. It is where a greyed button says why it is grey,
       which is the difference between a control that is refusing and one that is dead.
       On the join tab, when the shell has nothing of its own to say, it explains the
       selected row. */
    if (st->status[0]) {
        db_print(s, f, st->status, MP_STATUS_X, MP_STATUS_Y, mp_pal(DM_TEXT_MEDIUM), DB_FONT6_XSPACING);
    } else {
        mp_status_line(st, line, (int)sizeof line);
        if (line[0])
            db_print(s, f, line, MP_STATUS_X, MP_STATUS_Y, mp_pal(DM_TEXT_MEDIUM), DB_FONT6_XSPACING);
    }

    for (i = 0; i < MP_ITEM_COUNT; i++) {
        if (!mp_item_visible(st, i)) continue;
        if (i == MP_I_TAB_HOST || i == MP_I_TAB_JOIN) continue;
        if (i == MP_I_NET_LAN || i == MP_I_NET_NET) continue;
        if (i == MP_I_NAME || i == MP_I_PASS || i == MP_I_ADDR) continue;
        if (i == MP_I_PRIVATE || i == MP_I_RELAY || i == MP_I_PUBLIC) continue;
        if (i == MP_I_HANDLE || i == MP_I_HANDLE_OK) continue;
        if (i >= MP_I_COL_GAME && i <= MP_I_HIDE_GREY) continue;   /* the list draws these */
        if (!mp_item_rect(st, i, &x, &y, &w, &h)) continue;
        mp_button(s, f, mp_label(st, i), x, y, w, h, st->pressed == i,
                  mp_item_disabled(st, i));
    }

    /* THE NAME PROMPT, LAST AND OVER EVERYTHING, which is what modal means in a renderer
       with one pass and no depth: drawn last, hit first (mp_item_visible does the
       second half). */
    if (st->prompt) {
        if (mp_item_rect(st, MP_I_HANDLE, &x, &y, &w, &h)) {
            dm_green_dialog(s, x - 10, y - 30, w + 20, 62);
            db_print(s, f, "YOUR NAME", x, y - 14, mp_pal(DM_TEXT_MEDIUM), DB_FONT6_XSPACING);
            mp_field(s, f, st->handle, x, y, w, h, st->focus == MP_I_HANDLE, st->caret_on, 0);
        }
        if (mp_item_rect(st, MP_I_HANDLE_OK, &x, &y, &w, &h))
            mp_button(s, f, "OK", x, y, w, h, st->pressed == MP_I_HANDLE_OK, !st->handle[0]);
    }
}

/* One measurement, named on stdout when it fails. */
static int mp_fits(const DB_Font *f, const char *what, const char *text, int box)
{
    const int w = db_string_width(f, text, DB_FONT6_XSPACING);
    if (w <= box) return 0;
    printf("MPLAYOUT|overflow|%s=%s|w=%d|box=%d\n", what, text, w, box);
    return 1;
}

static int mp_rects_meet(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

int mp_check_layout(const DB_Pack *p, const MP_State *st)
{
    const DB_Font *f = db_font(p, "GRAD6FNT");
    int bad = 0, i, j, x, y, w, h;
    char cut[MP_NAME_MAX + 1];
    if (!f) f = db_font(p, "6POINT");
    if (!f) return 0;
    for (i = 0; i < MP_ITEM_COUNT; i++) {
        const char *l;
        if (!mp_item_rect(st, i, &x, &y, &w, &h)) continue;
        /* THE CHECKBOX'S LABEL IS NOT INSIDE THE CHECKBOX. Its rect is the nine pixel
           box you click and the words sit to the left of it, so measuring one against the
           other reports an overflow that is not one. Skipped by name rather than by a
           width fudge, because a fudge would also hide a real overflow. */
        if (i == MP_I_PRIVATE) continue;
        l = mp_label(st, i);
        if (!l[0]) continue;
        if (db_string_width(f, l, DB_FONT6_XSPACING) > w - 4) {
            printf("MPLAYOUT|overflow|item=%d|label=%s|w=%d|box=%d\n", i, l,
                   db_string_width(f, l, DB_FONT6_XSPACING), w);
            bad++;
        }
    }

    /* EVERY CONTROL ON THE PLATE, AND NONE ON ANOTHER. A rect that leaves the dialog, runs
       into the button row's margin, or lands on a neighbour is a press that goes to the
       wrong control, and no screenshot says which one got it. The rows band counts as a
       control here, because mp_press asks it before any item. */
    for (i = 0; i < MP_ITEM_COUNT; i++) {
        int ax, ay, aw, ah;
        if (!mp_item_rect(st, i, &ax, &ay, &aw, &ah)) continue;
        if (ax < MPX + 1 || ax + aw - 1 > MPX + MPW - 2 || ay + ah - 1 > MPY + MPH - 4) {
            printf("MPLAYOUT|outside|item=%d|x=%d..%d|y=%d..%d\n", i, ax, ax + aw - 1, ay, ay + ah - 1);
            bad++;
        }
        if (st->page == MP_PAGE_PICK && st->tab == MP_TAB_JOIN && !st->prompt
            && mp_rects_meet(ax, ay, aw, ah, MP_LIST_X, MP_ROWS_Y, MP_ROWS_W, MP_ROWS_H)) {
            printf("MPLAYOUT|overlap|item=%d|rows band\n", i);
            bad++;
        }
        for (j = i + 1; j < MP_ITEM_COUNT; j++) {
            int bx, by, bw, bh;
            if (!mp_item_rect(st, j, &bx, &by, &bw, &bh)) continue;
            if (mp_rects_meet(ax, ay, aw, ah, bx, by, bw, bh)) {
                printf("MPLAYOUT|overlap|item=%d|item=%d\n", i, j);
                bad++;
            }
        }
    }

    /* THE LIST'S OWN BOXES, whatever page this is: they are constants, and a constant
       that stops fitting should say so on the next run of any page. */
    {
        static const char *const W31 = "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW";
        const int mark = MP_MARK_W;
        MP_State t;
        char say[128];
        /* A header's label, two pixels, and the marker, clear of the cell's right edge
           line. */
        for (i = MP_I_COL_GAME; i <= MP_I_COL_PLAYERS; i++) {
            const int cx[4] = { MP_LIST_X, MP_HEAD_MAP_X, MP_HEAD_PING_X, MP_HEAD_PLR_X };
            const int cw[4] = { MP_HEAD_GAME_W, MP_HEAD_MAP_W, MP_HEAD_PING_W, MP_HEAD_PLR_W };
            const int k = i - MP_I_COL_GAME;
            const int lx = mp_head_text_x(i);
            const int end = lx + db_string_width(f, mp_head_label(i), DB_FONT6_XSPACING) + 2 + mark;
            if (end > cx[k] + cw[k] - 1) {
                printf("MPLAYOUT|overflow|header=%s|ends=%d|cell ends=%d\n",
                       mp_head_label(i), end, cx[k] + cw[k] - 1);
                bad++;
            }
        }
        /* The worst a cell can be asked to print. */
        bad += mp_fits(f, "players", "8/8", MP_PLR_TW);
        bad += mp_fits(f, "ping", "999 MS", MP_PING_TW);
        bad += mp_fits(f, "ping", "...", MP_PING_TW);
        bad += mp_fits(f, "ping", "--", MP_PING_TW);
        /* EACH TEXT BUDGET ENDS BEFORE THE NEXT COLUMN BEGINS. A budget is only a promise
           about the cut; it is the gap to the next column that keeps a cut name off the
           next column's text, and nothing else measures that. The text of one column must
           end at least MP_CELL_IN pixels before the text of the next starts, which is the
           same inset every cell already gives its own text, and the last column must end
           inside the rows band. */
        {
            const int text0[4] = { MP_LIST_X + MP_COL_NAME, MP_HEAD_MAP_X + MP_CELL_IN,
                                   MP_HEAD_PING_X + MP_CELL_IN, MP_HEAD_PLR_X + MP_CELL_IN };
            const int budget[4] = { MP_NAME_TW, MP_MAP_TW, MP_PING_TW, MP_PLR_TW };
            static const char *const col[4] = { "GAME", "MAP", "PING", "PLAYERS" };
            for (i = 0; i < 4; i++) {
                const int end = text0[i] + budget[i];
                const int limit = (i < 3) ? text0[i + 1] - MP_CELL_IN : MP_LIST_X + MP_ROWS_W;
                if (end > limit) {
                    printf("MPLAYOUT|overflow|column=%s|text ends=%d|next column's text=%d\n",
                           col[i], end, (i < 3) ? text0[i + 1] : limit);
                    bad++;
                }
            }
        }
        /* The cut rule itself, on the worst name, measured here rather than taken from the
           width the cut reports about itself. */
        sk_cut_to_width(f, W31, MP_NAME_TW, cut, (int)sizeof cut);
        bad += mp_fits(f, "name cut", cut, MP_NAME_TW);
        sk_cut_to_width(f, W31, MP_MAP_TW, cut, (int)sizeof cut);
        bad += mp_fits(f, "map cut", cut, MP_MAP_TW);
        /* The HIDE caption before its first box, and each word inside its item. */
        bad += mp_fits(f, "hide", "HIDE", MP_HIDE0_X - 1 - MP_HIDE_CAP_X);
        bad += mp_fits(f, "hide", "LOCKED", MP_HIDE_W - MP_HIDE_TXT);
        bad += mp_fits(f, "hide", "GREYED", MP_HIDE_W - MP_HIDE_TXT);
        /* The field line's two labels before their fields. */
        bad += mp_fits(f, "field", "ROOM CODE", MP_ADDR_X - 1 - MP_LBL_X);
        bad += mp_fits(f, "field", "PASSCODE", MP_JPASS_X - 1 - MP_JPASS_LBL_X);

        /* EVERY SENTENCE mp_status_line CAN SAY, at its widest, produced by the function
           itself on a state built to be the worst case, so what is measured is what is
           printed. */
        mp_init(&t);
        t.tab = MP_TAB_JOIN;
        t.net = MP_NET_INTERNET;
        t.rowcount = 1;
        t.rowsel = 0;
        t.rows[0].joinable = 1;
        t.rows[0].port = 65535;
        snprintf(t.rows[0].addr, sizeof t.rows[0].addr, "%s", W31);   /* cut to fit */
        snprintf(t.rows[0].room, sizeof t.rows[0].room, "%s", "#WWW-WWW");
        for (i = 0; i < 7; i++) {
            t.rows[0].why = MP_WHY_OK;
            t.rows[0].locked = t.rows[0].relay = t.rows[0].lan = 0;
            t.focus = -1;
            t.rowsel = 0;
            t.rowhidden = 0;
            switch (i) {
            case 0: t.rows[0].why = MP_WHY_BUILD; break;
            case 1: t.rows[0].why = MP_WHY_FULL; t.rows[0].players_now = t.rows[0].players_max = 8; break;
            case 2: t.rows[0].locked = 1; break;
            case 3: t.rows[0].relay = 1; break;
            case 4: break;                                        /* direct */
            case 5: t.rows[0].lan = 1; break;
            default: t.rowsel = -1; t.rowhidden = MP_MAX_ROWS; break;
            }
            mp_status_line(&t, say, (int)sizeof say);
            bad += mp_fits(f, "status", say, MP_STATUS_W);
        }
        t.focus = MP_I_ADDR;
        mp_status_line(&t, say, (int)sizeof say);
        bad += mp_fits(f, "status", say, MP_STATUS_W);
        if (st->status[0])
            bad += mp_fits(f, "status", st->status, MP_STATUS_W);

        /* AND EVERY ROW THIS STATE WOULD DRAW, cut by the rule the draw cuts by. */
        for (i = 0; i < st->rowcount && i < MP_MAX_ROWS; i++) {
            const MP_Row *g = &st->rows[i];
            sk_cut_to_width(f, g->name, MP_NAME_TW, cut, (int)sizeof cut);
            bad += mp_fits(f, "row name", cut, MP_NAME_TW);
            sk_cut_to_width(f, g->map, MP_MAP_TW, cut, (int)sizeof cut);
            bad += mp_fits(f, "row map", cut, MP_MAP_TW);
            mp_row_ping_text(g, say, (int)sizeof say);
            bad += mp_fits(f, "row ping", say, MP_PING_TW);
            snprintf(say, sizeof say, "%d/%d", g->players_now, g->players_max);
            bad += mp_fits(f, "row players", say, MP_PLR_TW);
        }
    }

    /* The two longest sentences the browser can print, which are the ones most likely to
       run off the panel and the ones a screenshot of a working LAN never shows. */
    {
        /* EVERY SENTENCE THIS SCREEN CAN PRINT, not just the browser's three. The list was
           short by four and the gap cost a real overflow: the relay checkbox's own line
           ran off the right edge of the plate and the audit said nothing, because a string
           that is not in this array is a string nobody measures. Anything added to a draw
           site belongs here in the same edit. */
        static const char *const MSGS_LIST[] = {
            MP_JOIN_L1, MP_JOIN_L2, MP_JOIN_L3,
            MP_LAN_L1, MP_LAN_L2,
            MP_HIDDEN_L2,
            "NO GAMES TO SHOW: 32 HIDDEN.",
            /* The browser's own failures, which the shell puts on the status line. They are
               held to the narrower list width anyway, so either place fits them. */
            "COULD NOT REACH THE GAME LIST.",
            "THE GAME LIST SENT SOMETHING ELSE.",
            "THE GAME LIST IS TOO LONG TO READ.",
            "COULD NOT LOOK FOR GAMES."
        };
        /* These start at MP_LBL_X and run to the dialog's right edge instead. */
        static const char *const MSGS_BODY[] = {
            MP_RELAY_ON_TEXT,
            MP_RELAY_OFF_TEXT,
            MP_PUBLIC_ON_TEXT,
            MP_PUBLIC_OFF_TEXT,
            "OTHERS TYPE THIS ADDRESS:",
            "FROM OUTSIDE, FORWARD THIS UDP PORT.",
            "COULD NOT READ THIS MACHINE'S ADDRESS.",
            "WAITING FOR EVERYONE TO PRESS READY."
        };
        const int bodyw = (MPX + MPW) - MP_LBL_X - 4;
        for (i = 0; i < (int)(sizeof MSGS_LIST / sizeof MSGS_LIST[0]); i++) {
            if (db_string_width(f, MSGS_LIST[i], DB_FONT6_XSPACING) > MP_LIST_W - 12) {
                printf("MPLAYOUT|overflow|msg=%s|w=%d|box=%d\n", MSGS_LIST[i],
                       db_string_width(f, MSGS_LIST[i], DB_FONT6_XSPACING), MP_LIST_W - 12);
                bad++;
            }
        }
        for (i = 0; i < (int)(sizeof MSGS_BODY / sizeof MSGS_BODY[0]); i++) {
            if (db_string_width(f, MSGS_BODY[i], DB_FONT6_XSPACING) > bodyw) {
                printf("MPLAYOUT|overflow|msg=%s|w=%d|box=%d\n", MSGS_BODY[i],
                       db_string_width(f, MSGS_BODY[i], DB_FONT6_XSPACING), bodyw);
                bad++;
            }
        }
    }
    return bad;
}
