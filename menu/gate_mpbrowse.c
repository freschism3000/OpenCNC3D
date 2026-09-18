/*
 * gate_mpbrowse.c -- the game's own half of the internet game list, against a real one.
 *
 * WHAT IT IS FOR. This gate does not test that a list can be fetched; it tests the things
 * that would be quietly wrong if nobody looked. That a relayed row carries NO address, so
 * a host who chose the safe kind of room stays unfindable. That a plain-HTTP address is
 * REFUSED, so a direct host's own address cannot be talked out of this build in clear.
 * That a game name full of quotes does not break a document this file writes by hand,
 * because there is no JSON writer in the tree and a player types that name. And that a
 * list which is not there FAILS rather than hanging, because everything here runs on a
 * worker the frame is never allowed to wait for.
 *
 * IT NEEDS THE SERVICE, and takes its address as the one argument, so the suite starts a
 * stand-in and points this at it. No relay and no second machine: this is the half that
 * talks to the list, and the half that talks to another player is gated elsewhere.
 *
 * WHAT A GREEN RUN DOES NOT PROVE: anything about the screen that draws these rows, and
 * anything about a real service on the far side of a real network. Both are exercised by
 * hand and neither is what this file is for.
 */

#include <stdio.h>
#include <string.h>

#include <SDL.h>

#include "mpbrowse.h"

static int fails = 0;
static int checks = 0;

static void check(const char *what, int ok, const char *detail)
{
    checks++;
    if (ok) {
        printf("  ok    %s\n", what);
    } else {
        printf("  FAILED %s   %s\n", what, detail ? detail : "");
        fails++;
    }
}

/* The worker answers when it answers. Everything here is bounded, because a gate that
   waits for ever on a network is a gate that reports nothing at all. */
static int wait_phase(int want, int ms)
{
    int t = 0;
    while (t < ms) {
        if (mb_phase() == want) return 1;
        SDL_Delay(25);
        t += 25;
    }
    return 0;
}

static void fill(MB_Row *r, const char *name, const char *room)
{
    memset(r, 0, sizeof *r);
    snprintf(r->name, sizeof r->name, "%s", name);
    snprintf(r->map, sizeof r->map, "%s", "Green Acres");
    snprintf(r->scenario, sizeof r->scenario, "%s", "SCM01EA");
    snprintf(r->room, sizeof r->room, "%s", room);
    r->relay = 1;
    r->players_now = 1;
    r->players_max = 4;
    r->locked = 1;
    r->abi = 0xABCDEF01u;
    r->scen = 0x12345678u;
}

int main(int argc, char **argv)
{
    MB_Row rows[MB_MAX_ROWS];
    MB_Row mine;
    const char *url;
    int n, i, found;

    if (argc < 2) {
        printf("usage: %s <game list url>\n", argv[0]);
        return 2;
    }
    url = argv[1];
    SDL_Init(SDL_INIT_EVENTS);

    printf("gate_mpbrowse: against %s\n", url);

    /* THE REFUSAL FIRST, because it is the one that protects somebody's address. */
    check("a plain-http address out on the internet is refused",
          mb_set_url("http://example.com/api/games") == 0, mb_url());
    check("and the refusal changed nothing",
          strstr(mb_url(), "example.com") == NULL, mb_url());
    check("an https address is accepted",
          mb_set_url("https://example.com/api/games") == 1, NULL);
    check("this machine may be spoken to in clear, so a stand-in can be used",
          mb_set_url(url) == 1, mb_url());

    fill(&mine, "GATE GAME", "#K7M-3QX");
    check("a game can be listed", mb_publish(&mine) == 1, mb_publish_error());
    SDL_Delay(1500);
    check("listing it raised no error", mb_publish_error()[0] == '\0', mb_publish_error());
    check("and the game says it is listed", mb_published() == 1, NULL);

    mb_refresh();
    check("the list comes back", wait_phase(MB_READY, 15000), mb_error());
    n = mb_rows(rows, MB_MAX_ROWS);
    found = 0;
    for (i = 0; i < n; i++) {
        if (strcmp(rows[i].name, "GATE GAME") != 0) continue;
        found = 1;
        check("the row is relayed", rows[i].relay == 1, NULL);
        /* THE ID IS WHAT THE BROWSER HOLDS A SELECTION BY, so it has to arrive whole. */
        {
            int k, hex = (strlen(rows[i].id) == 32);
            for (k = 0; hex && k < 32; k++)
                if (!((rows[i].id[k] >= '0' && rows[i].id[k] <= '9')
                      || (rows[i].id[k] >= 'a' && rows[i].id[k] <= 'f')))
                    hex = 0;
            check("its id survived: 32 lowercase hex", hex, rows[i].id);
        }
        check("it carries its room code", !strcmp(rows[i].room, "#K7M-3QX"), rows[i].room);
        check("A RELAYED ROW CARRIES NO ADDRESS", rows[i].addr[0] == '\0', rows[i].addr);
        check("its map name survived", !strcmp(rows[i].map, "Green Acres"), rows[i].map);
        check("its seats survived",
              rows[i].players_now == 1 && rows[i].players_max == 4, NULL);
        check("its padlock survived", rows[i].locked == 1, NULL);
        check("and the two numbers a joiner is refused against",
              rows[i].abi == 0xABCDEF01u && rows[i].scen == 0x12345678u, NULL);
    }
    check("the listed game is in the list", found, NULL);

    /* A NAME IS TYPED BY A PLAYER and goes into a document written here by hand. */
    {
        MB_Row odd;
        fill(&odd, "A \"QUOTED\" \\ NAME", "#Z9Q-2WX");
        mb_unpublish();
        SDL_Delay(1200);
        check("a second game lists", mb_publish(&odd) == 1, mb_publish_error());
        SDL_Delay(1500);
        check("a name full of quotes did not break the row",
              mb_publish_error()[0] == '\0', mb_publish_error());
        mb_forget();
        mb_refresh();
        check("and the list still reads", wait_phase(MB_READY, 15000), mb_error());
        n = mb_rows(rows, MB_MAX_ROWS);
        found = 0;
        for (i = 0; i < n; i++)
            if (strstr(rows[i].name, "QUOTED")) found = 1;
        check("with the quoted game in it", found, NULL);
    }

    mb_unpublish();
    SDL_Delay(1500);
    check("a game can be taken off the list", mb_published() == 0, NULL);
    mb_forget();
    mb_refresh();
    wait_phase(MB_READY, 15000);
    n = mb_rows(rows, MB_MAX_ROWS);
    found = 0;
    for (i = 0; i < n; i++)
        if (strstr(rows[i].name, "QUOTED") || !strcmp(rows[i].name, "GATE GAME"))
            found = 1;
    check("and it is gone from the list", !found, NULL);

    /* A LIST THAT IS NOT THERE MUST FAIL, and must do it in a bounded time: everything
       above runs on a worker precisely so a screen never waits on one. */
    mb_set_url("http://127.0.0.1:1/");
    mb_forget();
    mb_refresh();
    check("a list that is not there fails instead of hanging",
          wait_phase(MB_FAILED, 40000), "still waiting after 40 s");
    check("and says so in words the screen can draw",
          mb_error()[0] != '\0', NULL);

    printf("gate_mpbrowse: %d checks, %d FAILED\n", checks, fails);
    SDL_Quit();
    return fails ? 1 : 0;
}
