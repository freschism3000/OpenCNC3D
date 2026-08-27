/*
 * w98_braintest.c -- prove the tier1 brain bridge on the real Windows 98 machine.
 *
 * Loads the brain, starts a mission, ticks it, and prints the live object list. It is a
 * console program on purpose: it answers "is the world actually there" with no renderer
 * in the way, so a failure here can only be the brain or the bridge.
 *
 *   w98braintest <content_dir> <mission_dir> <scenario> [ticks]
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "w98_brain.h"

int main(int argc, char **argv)
{
    char err[512];
    W98_MapInfo map;
    static W98_Object objs[WB_MAX_OBJECTS];
    int n, i, t, ticks;
    const char *content, *missions, *scen;

    if (argc < 4)
    {
        printf("usage: %s <content_dir> <mission_dir> <scenario> [ticks]\n", argv[0]);
        return 2;
    }
    content  = argv[1];
    missions = argv[2];
    scen     = argv[3];
    ticks    = (argc > 4) ? atoi(argv[4]) : 0;

    if (!wb_open(NULL, err, sizeof err)) { printf("BRAIN OPEN FAILED: %s\n", err); return 1; }
    printf("brain loaded\n");

    if (!wb_start(content, missions, scen, 1, err, sizeof err))
    { printf("START FAILED: %s\n", err); return 1; }
    printf("scenario '%s' started\n", scen);

    if (wb_map(&map))
        printf("map theater=%d origin=%d,%d size=%dx%d scenario='%s'\n",
               map.theater, map.cellx, map.celly, map.cellw, map.cellh, map.scenario);
    else
        printf("map read FAILED\n");

    for (t = 0; t < ticks; ++t) wb_tick();
    printf("advanced %d tick(s)\n", ticks);

    n = wb_objects(objs, WB_MAX_OBJECTS);
    printf("OBJECTS: %d\n", n);
    for (i = 0; i < n && i < 40; ++i)
    {
        W98_Object *o = &objs[i];
        printf("  %-9s %-6s %-8s cell=%4d (%2d,%2d) lept=(%5d,%5d) fw=%d fh=%d "
               "face=%3d tface=%3d hp=%d/%d %s\n",
               o->kind, o->name, o->house, o->cell, o->cx, o->cy, o->lx, o->ly,
               o->fw, o->fh, o->face, o->tface, o->strength, o->maxstrength, o->mission);
    }
    if (n > 40) printf("  ... %d more\n", n - 40);

    wb_close();
    printf("OK\n");
    return 0;
}
