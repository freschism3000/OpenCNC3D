/*
 * mpbrowse.c -- fetching and publishing the internet game list, off the frame.
 *
 * THE STATE HERE IS NEVER FREED, AND THAT IS THE DESIGN. A worker that is still waiting
 * on a dead network when the player leaves the screen has to be able to finish writing
 * somewhere harmless. Joining it instead would freeze the game for as long as the
 * network took to give up, which is the one thing this file exists to prevent. So the
 * two little states below live for the length of the process, workers are detached, and
 * a reply that arrives after the screen has gone is dropped by its generation number
 * rather than by anybody waiting for it.
 *
 * ONE REQUEST AT A TIME, PER HALF. Browsing and publishing never happen together in
 * practice -- a host is not reading the list it is on -- but they are kept apart anyway
 * so neither can be made to wait behind the other.
 */

#include "mpbrowse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "../launcher/lnet.h"
#include "../launcher/ljson.h"
#include "../net/roomcode.h"

/* THE ADDRESS THE GAME ASKS BY DEFAULT. A compiled-in address with no way to change it
   is the mistake the relay already carries, so this one is overridable. */
#ifndef MB_DEFAULT_URL
#define MB_DEFAULT_URL "https://cnc3dgame.com/api/games"
#endif

/* A list of thirty-two rows is a few kilobytes. The cap is what stops a reply that is
   not a list from being read into memory, and it is deliberately close to the real
   size rather than generous. */
#define MB_GET_CAP  65536
#define MB_POST_CAP 4096

#define MB_ROW_VERSION 1

/* ------------------------------------------------------------------ shared */

static char        s_url[512];
static SDL_mutex  *s_url_lock;

static void mb_once(void)
{
    /* The menu is single threaded until the first request starts a worker, so the first
       call cannot race. */
    if (!s_url_lock)
        s_url_lock = SDL_CreateMutex();
    if (!s_url[0])
        snprintf(s_url, sizeof s_url, "%s", MB_DEFAULT_URL);
}

/* Is this address one that may be spoken to in clear? Only this machine. A direct row
   carries the host's own address, and a host who was told it becomes public was told it
   goes to one place. */
static int mb_local_url(const char *url)
{
    return url
        && (!strncmp(url, "http://127.0.0.1", 16)
            || !strncmp(url, "http://localhost", 16)
            || !strncmp(url, "http://[::1]", 12));
}

int mb_set_url(const char *url)
{
    mb_once();
    if (!url || !*url) {
        SDL_LockMutex(s_url_lock);
        snprintf(s_url, sizeof s_url, "%s", MB_DEFAULT_URL);
        SDL_UnlockMutex(s_url_lock);
        return 1;
    }
    if (strncmp(url, "https://", 8) != 0 && !mb_local_url(url))
        return 0;
    SDL_LockMutex(s_url_lock);
    snprintf(s_url, sizeof s_url, "%s", url);
    SDL_UnlockMutex(s_url_lock);
    return 1;
}

const char *mb_url(void)
{
    mb_once();
    return s_url;
}

static void mb_url_copy(char *out, int outlen)
{
    mb_once();
    SDL_LockMutex(s_url_lock);
    snprintf(out, (size_t)outlen, "%s", s_url);
    SDL_UnlockMutex(s_url_lock);
}

/* ------------------------------------------------------------------ browsing */

typedef struct MB_Browse
{
    SDL_mutex *lock;
    int        phase;
    int        generation;   /* bumped by every ask and by every leaving */
    int        rows;
    MB_Row     row[MB_MAX_ROWS];
    char       error[256];
} MB_Browse;

static MB_Browse s_b;

static void mb_browse_once(void)
{
    if (!s_b.lock) {
        s_b.lock = SDL_CreateMutex();
        s_b.phase = MB_IDLE;
    }
}

static void mb_str(char *out, int outlen, const LJ_Value *obj, const char *key)
{
    const char *v = lj_str(obj, key);
    snprintf(out, (size_t)outlen, "%s", v ? v : "");
}

/* Read one row out of the document. Returns 0 for a row this build will not show at all,
   which is only ever a row missing something it cannot be drawn or joined without. */
static int mb_row_from_json(const LJ_Value *it, MB_Row *r)
{
    memset(r, 0, sizeof *r);
    mb_str(r->id, sizeof r->id, it, "id");
    /* AN ID THAT IS NOT ONE IS DROPPED, not kept half-right. The row is still shown and
       still joinable; the screen then holds it by its room code or its address instead. */
    {
        int i, good = (strlen(r->id) == 32);
        for (i = 0; good && i < 32; i++) {
            const char c = r->id[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) good = 0;
        }
        if (!good) r->id[0] = '\0';
    }
    mb_str(r->name, sizeof r->name, it, "name");
    mb_str(r->map, sizeof r->map, it, "map");
    mb_str(r->scenario, sizeof r->scenario, it, "scenario");
    mb_str(r->addr, sizeof r->addr, it, "addr");
    mb_str(r->room, sizeof r->room, it, "room");
    r->port        = (unsigned short)lj_num(it, "port", 0);
    r->players_now = (int)lj_num(it, "players", 0);
    r->players_max = (int)lj_num(it, "max", 0);
    r->locked      = lj_bool(it, "locked", 0);
    r->relay       = lj_bool(it, "relay", 0);
    r->abi         = (unsigned)lj_num(it, "abi", 0);
    r->scen        = (unsigned)lj_num(it, "scen", 0);

    if (!r->name[0] || r->players_max <= 0)
        return 0;
    /* THE TWO KINDS ARE CHECKED, NOT TRUSTED. A relayed row with no code cannot be
       joined and a direct row with no address cannot either, so neither is shown: a row
       that cannot be clicked is worse than an empty list, because it looks like the game
       is broken rather than like nobody is playing. */
    if (r->relay) {
        if (!r->room[0]) return 0;
        r->addr[0] = '\0';
        r->port = 0;
    } else {
        if (!r->addr[0] || r->port == 0) return 0;
        r->room[0] = '\0';
    }
    return 1;
}

static int mb_fetch_thread(void *unused)
{
    char url[512], err[256];
    char *body;
    int mine;
    (void)unused;

    SDL_LockMutex(s_b.lock);
    mine = s_b.generation;
    SDL_UnlockMutex(s_b.lock);

    mb_url_copy(url, (int)sizeof url);
    err[0] = '\0';
    body = ln_get_text(url, NULL, MB_GET_CAP, err, (int)sizeof err);

    if (!body) {
        /* WHAT THE PLAYER READS AND WHAT THE LOG KEEPS ARE NOT THE SAME SENTENCE. The
           HTTP client is shared with the updater and says so in its own words, which on a
           game browser would name the wrong thing entirely; and one of the two backends
           reports transport failures in whatever words its library chose, which is not a
           set anything here can match on. So the screen gets one sentence this file owns,
           and the exact reason goes to the log where it is worth having. */
        printf("MPLIST|fetch-failed|%s\n", err[0] ? err : "no reason given");
        fflush(stdout);
        SDL_LockMutex(s_b.lock);
        if (mine == s_b.generation) {
            snprintf(s_b.error, sizeof s_b.error, "COULD NOT REACH THE GAME LIST.");
            s_b.phase = MB_FAILED;
        }
        SDL_UnlockMutex(s_b.lock);
        return 0;
    }

    /* A REPLY THAT FILLS THE CAP EXACTLY WAS CUT OFF. The two backends disagree about
       what to do with an over-long answer, so the length is the only test that means the
       same thing on both, and a truncated document must not be reported as bad JSON from
       a service that is behaving. */
    if (strlen(body) >= (size_t)MB_GET_CAP) {
        SDL_LockMutex(s_b.lock);
        if (mine == s_b.generation) {
            snprintf(s_b.error, sizeof s_b.error,
                     "THE GAME LIST IS TOO LONG TO READ.");
            s_b.phase = MB_FAILED;
        }
        SDL_UnlockMutex(s_b.lock);
        free(body);
        return 0;
    }

    {
        LJ_Value *doc = lj_parse(body, err, (int)sizeof err);
        free(body);
        if (!doc) {
            SDL_LockMutex(s_b.lock);
            if (mine == s_b.generation) {
                printf("MPLIST|parse-failed|%s\n", err);
                fflush(stdout);
                snprintf(s_b.error, sizeof s_b.error, "THE GAME LIST SENT SOMETHING ELSE.");
                s_b.phase = MB_FAILED;
            }
            SDL_UnlockMutex(s_b.lock);
            return 0;
        }
        {
            const LJ_Value *games = lj_get(doc, "games");
            const LJ_Value *it;
            MB_Row rows[MB_MAX_ROWS];
            int n = 0;
            for (it = lj_first(games); it && n < MB_MAX_ROWS; it = lj_next(it)) {
                if (mb_row_from_json(it, &rows[n]))
                    n++;
            }
            SDL_LockMutex(s_b.lock);
            if (mine == s_b.generation) {
                memcpy(s_b.row, rows, sizeof(MB_Row) * (size_t)n);
                s_b.rows = n;
                s_b.error[0] = '\0';
                s_b.phase = MB_READY;
            }
            SDL_UnlockMutex(s_b.lock);
        }
        lj_free(doc);
    }
    return 0;
}

void mb_refresh(void)
{
    SDL_Thread *t;
    mb_browse_once();

    SDL_LockMutex(s_b.lock);
    if (s_b.phase == MB_FETCHING) {   /* one at a time; a second press is not a second ask */
        SDL_UnlockMutex(s_b.lock);
        return;
    }
    s_b.generation++;
    s_b.phase = MB_FETCHING;
    SDL_UnlockMutex(s_b.lock);

    t = SDL_CreateThread(mb_fetch_thread, "cnc3d-gamelist", NULL);
    if (!t) {
        SDL_LockMutex(s_b.lock);
        snprintf(s_b.error, sizeof s_b.error, "COULD NOT LOOK FOR GAMES.");
        s_b.phase = MB_FAILED;
        SDL_UnlockMutex(s_b.lock);
        return;
    }
    /* DETACHED, BECAUSE NOTHING IS EVER GOING TO WAIT FOR IT. */
    SDL_DetachThread(t);
}

int mb_phase(void)
{
    int p;
    mb_browse_once();
    SDL_LockMutex(s_b.lock);
    p = s_b.phase;
    SDL_UnlockMutex(s_b.lock);
    return p;
}

int mb_rows(MB_Row *out, int max)
{
    int n;
    mb_browse_once();
    if (!out || max <= 0) return 0;
    SDL_LockMutex(s_b.lock);
    n = s_b.rows < max ? s_b.rows : max;
    if (n > 0)
        memcpy(out, s_b.row, sizeof(MB_Row) * (size_t)n);
    SDL_UnlockMutex(s_b.lock);
    return n;
}

const char *mb_error(void)
{
    /* The pointer is stable and only ever rewritten by a worker under the lock; a torn
       read here would show a half-written sentence for one frame and never anything
       worse, which is not worth a copy on every draw. */
    mb_browse_once();
    return s_b.error;
}

void mb_forget(void)
{
    mb_browse_once();
    SDL_LockMutex(s_b.lock);
    /* THE GENERATION IS BUMPED HERE TOO, not only by an ask. Without this a reply that
       arrives after the player has walked away lands in the rows of a screen that is
       gone, and the next visit opens on a list nobody asked for. */
    s_b.generation++;
    s_b.rows = 0;
    s_b.phase = MB_IDLE;
    s_b.error[0] = '\0';
    SDL_UnlockMutex(s_b.lock);
}

/* ------------------------------------------------------------------ publishing */

typedef struct MB_Publish
{
    SDL_mutex *lock;
    int        busy;
    int        listed;
    char       id[33];
    char       token[33];
    char       body[1024];   /* the row the worker is to send */
    char       error[256];
} MB_Publish;

static MB_Publish s_p;

static void mb_publish_once(void)
{
    if (!s_p.lock)
        s_p.lock = SDL_CreateMutex();
}

/* Sixteen random bytes as hex. Four draws of the room code's own generator, which is the
   only entropy this project has and is already the thing standing between a room and a
   stranger. Returns 0 if the machine would not give a random number. */
static int mb_make_key(char out[33])
{
    int i;
    out[0] = '\0';
    for (i = 0; i < 4; i++) {
        const unsigned long v = rc_draw_peer_id();
        char part[9];
        if (v == 0ul) return 0;
        snprintf(part, sizeof part, "%08lx", v);
        memcpy(out + i * 8, part, 8);
    }
    out[32] = '\0';
    return 1;
}

/* JSON HAS TO BE WRITTEN BY HAND HERE: the reader in this tree has no writer beside it,
   and a game's name is typed by a player, so it is the one field that can carry a quote
   or a backslash into the document. */
static void mb_json_escape(char *out, int outlen, const char *in)
{
    int o = 0;
    int i;
    if (outlen <= 0) return;
    for (i = 0; in && in[i] && o < outlen - 2; i++) {
        const unsigned char c = (unsigned char)in[i];
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = (char)c;
        } else if (c >= 0x20 && c < 0x7F) {
            out[o++] = (char)c;
        }
        /* Anything else is dropped rather than escaped: the screens that draw this
           cannot show it, so carrying it would only make the row longer. */
    }
    out[o] = '\0';
}

static int mb_post_thread(void *unused)
{
    char url[512], err[256], body[1024];
    char *reply;
    (void)unused;

    SDL_LockMutex(s_p.lock);
    snprintf(body, sizeof body, "%s", s_p.body);
    SDL_UnlockMutex(s_p.lock);

    mb_url_copy(url, (int)sizeof url);
    err[0] = '\0';
    reply = ln_post_text(url, NULL, body, NULL, MB_POST_CAP, err, (int)sizeof err);

    SDL_LockMutex(s_p.lock);
    if (reply) {
        s_p.error[0] = '\0';
    } else {
        printf("MPLIST|publish-failed|%s\n", err[0] ? err : "no reason given");
        fflush(stdout);
        snprintf(s_p.error, sizeof s_p.error, "COULD NOT LIST THIS GAME.");
        /* A FAILED HEARTBEAT DOES NOT UNLIST THE GAME HERE. The row may well still be
           standing; the next beat will say so, and the list drops it on its own if it
           does not. Marking it gone locally would only make the screen lie in the other
           direction. */
    }
    s_p.busy = 0;
    SDL_UnlockMutex(s_p.lock);
    if (reply) free(reply);
    return 0;
}

static void mb_post_now(void)
{
    SDL_Thread *t = SDL_CreateThread(mb_post_thread, "cnc3d-gamelist-post", NULL);
    if (!t) {
        SDL_LockMutex(s_p.lock);
        snprintf(s_p.error, sizeof s_p.error, "COULD NOT LIST THIS GAME.");
        s_p.busy = 0;
        SDL_UnlockMutex(s_p.lock);
        return;
    }
    SDL_DetachThread(t);
}

int mb_publish(const MB_Row *row)
{
    char name[128], map[128], scen[64], room[32];
    mb_publish_once();
    if (!row) return 0;

    SDL_LockMutex(s_p.lock);
    if (s_p.busy) {      /* a beat is still going out; this one is simply skipped */
        SDL_UnlockMutex(s_p.lock);
        return 1;
    }
    if (!s_p.id[0]) {
        if (!mb_make_key(s_p.id) || !mb_make_key(s_p.token)) {
            snprintf(s_p.error, sizeof s_p.error,
                     "THIS COMPUTER WOULD NOT GIVE A RANDOM NUMBER.");
            s_p.id[0] = '\0';
            SDL_UnlockMutex(s_p.lock);
            return 0;
        }
    }
    mb_json_escape(name, (int)sizeof name, row->name);
    mb_json_escape(map, (int)sizeof map, row->map);
    mb_json_escape(scen, (int)sizeof scen, row->scenario);
    mb_json_escape(room, (int)sizeof room, row->room);

    /* THE ONE FIELD THAT DIFFERS BETWEEN THE TWO KINDS, built first so the row itself
       stays one statement. A relayed game is found by its code; a direct one by a port,
       at an address the list takes from the connection. */
    {
        char target[64];
        if (row->relay)
            snprintf(target, sizeof target, "\"room\":\"%s\",", room);
        else
            snprintf(target, sizeof target, "\"port\":%u,", (unsigned)row->port);

        /* THE ADDRESS IS NOT SENT. A direct row's address is taken by the list from the
           connection it arrived on, so a host can only ever list itself and cannot point
           the list at somebody else's machine. */
        snprintf(s_p.body, sizeof s_p.body,
                 "{\"v\":%d,\"id\":\"%s\",\"token\":\"%s\","
                 "\"name\":\"%s\",\"map\":\"%s\",\"scenario\":\"%s\","
                 "\"players\":%d,\"max\":%d,\"locked\":%s,"
                 "\"abi\":%u,\"scen\":%u,\"relay\":%s,%s"
                 "\"open\":true}",
                 MB_ROW_VERSION, s_p.id, s_p.token,
                 name, map, scen,
                 row->players_now, row->players_max, row->locked ? "true" : "false",
                 row->abi, row->scen, row->relay ? "true" : "false",
                 target);
    }
    s_p.busy = 1;
    s_p.listed = 1;
    SDL_UnlockMutex(s_p.lock);
    mb_post_now();
    return 1;
}

void mb_unpublish(void)
{
    mb_publish_once();
    SDL_LockMutex(s_p.lock);
    if (!s_p.id[0] || !s_p.listed) {
        SDL_UnlockMutex(s_p.lock);
        return;
    }
    /* THE WITHDRAWAL OVERWRITES WHATEVER BEAT WAS PENDING. Going out matters more than
       any heartbeat still queued behind it. */
    snprintf(s_p.body, sizeof s_p.body,
             "{\"v\":%d,\"id\":\"%s\",\"token\":\"%s\",\"open\":false}",
             MB_ROW_VERSION, s_p.id, s_p.token);
    s_p.listed = 0;
    s_p.busy = 1;
    /* THE KEYS ARE DROPPED ONLY AFTER THE WITHDRAWAL HAS BEEN WRITTEN, and they are
       dropped rather than kept: the next room this player opens is a different game and
       must not inherit a row somebody may still be looking at. A fresh pair is drawn for
       it. The message above already carries the old pair, so clearing them here costs
       the worker nothing. */
    s_p.id[0] = '\0';
    s_p.token[0] = '\0';
    SDL_UnlockMutex(s_p.lock);
    mb_post_now();
}

int mb_published(void)
{
    int v;
    mb_publish_once();
    SDL_LockMutex(s_p.lock);
    v = s_p.listed;
    SDL_UnlockMutex(s_p.lock);
    return v;
}

const char *mb_publish_error(void)
{
    mb_publish_once();
    return s_p.error;
}
