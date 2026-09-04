/*
 * t1_place.h -- the building placement preview.
 *
 * the project owner: "Placing buildings have no preview of the structure and where its being placed,
 * when moving the mouse around." They did not. Placement mode was a boolean and one
 * click: the player picked a cameo, the pointer changed nothing, and wherever the next
 * click landed a building either appeared or silently did not.
 *
 * WHAT IS DRAWN, and it is three things, because the engine answers three questions:
 *
 *   1. The legal REGION, faint. Every cell whose origin would satisfy the proximity rule
 *      -- the "you must build next to something you own" rule -- shaded so the player can
 *      see where the base can grow at all, rather than hunting for it a click at a time.
 *   2. The FOOTPRINT under the pointer, cell by cell, green where that cell is clear and
 *      red where it is not. This is the building's own occupy list out of the engine, so
 *      it is the right shape for every structure without a table of our own.
 *   3. The BUILDING ITSELF, its real mesh, translucent, standing where it would stand.
 *      That is the part the project owner asked for and the part a cell grid cannot give: a Weapons
 *      Factory and a Barracks both occupy a 3x3 and look nothing alike.
 *
 * The legality is the ENGINE's verdict, never ours: GAME_STATE_PLACEMENT reports, per
 * cell, whether the proximity check passes and whether the cell is generally clear, and
 * a cell is a legal origin when proximity passes there and every cell of the occupy list
 * is clear. Nothing here re-implements a placement rule.
 */

#ifndef T1_PLACE_H
#define T1_PLACE_H

#include "w98_brain.h"
#include "t1_mesh.h"
#include "t1_terrain.h"
#include "t1_cam.h"

typedef struct
{
    int           active;          /* placement mode is on */
    W98_Build     item;            /* what is being placed, with its occupy list */
    unsigned char prox[64 * 64];   /* the engine's per-cell proximity verdict */
    unsigned char clear[64 * 64];  /* the engine's per-cell obstruction verdict */
    int           gridx, gridy;    /* the placement grid's origin, in world cells */
    int           hoverx, hovery;  /* the cell under the pointer, -1 when off the map */
    int           legal;           /* is the hovered origin a legal one */
    int           mesh;            /* the model to ghost, or -1 */
    long          cellsdrawn;      /* for the report */
} T1_Place;

/* Enter placement mode with this sidebar item. Returns 0 if the engine will not have it. */
int  t1_place_begin(T1_Place *p, const T1_MeshBank *b, const W98_Build *item);
void t1_place_end(T1_Place *p);

/* Once a frame while active: re-read the engine's verdict and resolve the hovered cell.
 * `wx`, `wz` are the world position under the pointer, from t1_screen_to_plane. */
void t1_place_update(T1_Place *p, float wx, float wz);

/* Is the hovered origin legal? The commit uses this so a doomed click is not sent. */
int  t1_place_legal(const T1_Place *p, int cellx, int celly);

/* The cell to hand SIDEBAR_REQUEST_PLACE: grid coordinates, not world cells. */
void t1_place_grid(const T1_Place *p, int *gx, int *gy);

/* The first legal origin, scanning in reading order, or 0 if there is none. A TEST
 * affordance: a scripted run has no way to know which cell the engine will accept, and a
 * placement path that has never actually placed anything is not a tested one. */
int  t1_place_first_legal(const T1_Place *p, int *cellx, int *celly);

/* Draw it. Call after the object pass, so the ghost sits over the world rather than
 * under it. `white` is the HUD's solid block and wu/wv one texel inside it, exactly as
 * the shroud takes them. */
long t1_place_draw(T1_Place *p, T1_MeshBank *b, const T1_Terrain *t,
                   const T1_Cam *cam, const T1_Screen *scr,
                   const SR_Texture *white, float wu, float wv);

#endif /* T1_PLACE_H */
