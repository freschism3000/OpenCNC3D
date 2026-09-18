/*
 * gate_beacon.c -- LAN discovery, proven in one process with no second machine.
 *
 * WHY THIS EXISTS BEFORE ANY UI. A server browser that shows nothing is
 * indistinguishable from a quiet network, so a discovery bug does not look like a bug: it
 * looks like nobody is playing. That is the worst failure mode a feature can have, and it
 * is why the wire is proven here, headless, before a pixel is drawn on top of it.
 *
 * IT HAS ALREADY EARNED ITS KEEP. Writing it turned up two defects that the browser would
 * have inherited as "the list is always empty":
 *   1. A send to 255.255.255.255 FAILS on macOS, returning -1. The interface's own subnet
 *      broadcast works and is what is used now.
 *   2. NetAddr is an opaque buffer plus a LEN, not a sockaddr_in. An address built by
 *      casting the struct leaves len at 0, which every function in net_udp.c reads as
 *      "unset", so every datagram was silently dropped before it reached the socket.
 */
#include "netbeacon.h"
#include "net_udp.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
  #include <windows.h>
  static void gb_nap(int ms) { Sleep((DWORD)ms); }
#else
  #include <unistd.h>
  static void gb_nap(int ms) { usleep((useconds_t)ms * 1000); }
#endif

static int fails = 0;
static int checks = 0;

static void check(int cond, const char* what)
{
    checks++;
    if (!cond) {
        printf("FAIL  %s\n", what);
        fails++;
    }
}

/* Poll for up to `ms`, stopping as soon as `want` rows are listed. Polling on a clock
   rather than a fixed sleep keeps the gate fast when it passes and honest when it does
   not: a beacon that arrives late still counts, and one that never arrives still fails. */
static int settle(int want, int ms)
{
    int waited = 0;
    for (;;) {
        nb_browse_poll();
        if (nb_browse_count() >= want) return nb_browse_count();
        if (waited >= ms) return nb_browse_count();
        gb_nap(25);
        waited += 25;
    }
}

int main(void)
{
    const NbGame* g;
    int n;

    printf("CNC3D LAN beacon gate\n\n");

    if (!nb_browse_open()) {
        printf("FAILED: could not open the discovery port %d. Something else is on it,\n"
               "        or this platform refused a shared bind.\n", NB_PORT);
        return 1;
    }
    if (!nb_announce_open()) {
        printf("FAILED: could not open a broadcasting socket.\n");
        nb_browse_close();
        return 1;
    }

    /* ---- leg 1: a game announces and is listed, with every field intact ---- */
    nb_announce("HOSTS GAME", "SCM01EA", "GREEN ACRES", 17421, 3, 8, 1, 0xC6D08568u, 0xE4DC23ADu);
    n = settle(1, 2000);
    check(n == 1, "one announcement produces exactly one row");
    g = nb_browse_get(0);
    if (!g) {
        printf("FAILED: nothing was listed at all; the rest cannot be checked.\n");
        nb_announce_close();
        nb_browse_close();
        return 1;
    }
    check(strcmp(g->name, "HOSTS GAME") == 0, "the game's name survives the wire");
    check(strcmp(g->scenario, "SCM01EA") == 0, "the scenario code survives the wire");
    check(strcmp(g->mapname, "GREEN ACRES") == 0, "the map's display name survives the wire");
    check(g->port == 17421, "the MATCH port is listed, not the beacon's sending port");
    check(g->players_now == 3 && g->players_max == 8, "the player counts survive");
    check(g->has_password == 1, "the padlock survives");
    check(g->abi == 0xC6D08568u, "the order-wire hash survives");
    check(g->scen_hash == 0xE4DC23ADu, "the scenario hash survives");
    /* THE ADDRESS IS THE ONE TO JOIN. It comes from the datagram rather than from inside
       it, so a host cannot send joiners somewhere else, and it must not be loopback on a
       machine that has a real address or the row is useless to another machine. */
    check(g->addr[0] != '\0', "the row carries the address the beacon came from");
    printf("  listed: '%s' on %s at %s:%u, %d/%d players, padlock=%d\n",
           g->name, g->scenario, g->addr, (unsigned)g->port,
           g->players_now, g->players_max, g->has_password);

    /* ---- leg 2: the same game re-announcing does not become a second row ---- */
    gb_nap(NB_PERIOD_MS + 100);
    nb_announce("HOSTS GAME", "SCM01EA", "GREEN ACRES", 17421, 4, 8, 1, 0xC6D08568u, 0xE4DC23ADu);
    settle(2, 1200);   /* asks for 2 so it waits the full time rather than stopping at 1 */
    check(nb_browse_count() == 1, "a host re-announcing stays ONE row");
    g = nb_browse_get(0);
    check(g && g->players_now == 4, "and the row is UPDATED, not merely kept");

    /* ---- leg 3: a second game on another port is a second row ---- */
    gb_nap(NB_PERIOD_MS + 100);
    nb_announce("OTHER GAME", "SCM02EA", "SAND TRAP", 17431, 1, 2, 0, 0xC6D08568u, 0x12345678u);
    n = settle(2, 2000);
    check(n == 2, "a game on a different port is a different row");

    /* ---- leg 4: silence removes a row, which is the whole liveness story ---- */
    printf("  waiting %d ms for the rows to go stale...\n", NB_STALE_MS + 500);
    gb_nap(NB_STALE_MS + 500);
    nb_browse_poll();
    check(nb_browse_count() == 0,
          "a game that stops announcing leaves the list without saying goodbye");

    /* ---- leg 5: REFRESH empties the list at once ---- */
    gb_nap(NB_PERIOD_MS + 100);
    nb_announce("HOSTS GAME", "SCM01EA", "GREEN ACRES", 17421, 3, 8, 1, 0xC6D08568u, 0xE4DC23ADu);
    settle(1, 2000);
    check(nb_browse_count() >= 1, "a game announcing again comes back");
    nb_browse_clear();
    check(nb_browse_count() == 0, "REFRESH empties the list immediately");

    nb_announce_close();
    nb_browse_close();

    printf("\n");
    if (fails == 0) {
        printf("  PASSED. %d checks. A game announces itself on the LAN, is listed once\n"
               "          with every field intact and at the address another machine\n"
               "          would type, updates in place, and leaves the list by going\n"
               "          quiet rather than by saying goodbye.\n", checks);
        return 0;
    }
    printf("  FAILED: %d of %d checks.\n", fails, checks);
    return 1;
}
