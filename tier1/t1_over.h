/*
 * t1_over.h -- the overlay cells and the things in flight.
 *
 * Walls and bullets are not techno objects: they live in none of the heaps the object
 * dump walks, so the engine reports them on their own lines and w98_brain.c parses them
 * off the same capture. This draws them, out of the cartridge's own meshes, with the
 * cartridge's own tables.
 */

#ifndef T1_OVER_H
#define T1_OVER_H

#include "softras.h"
#include "t1_cam.h"
#include "t1_mesh.h"
#include "t1_terrain.h"
#include "w98_brain.h"

/* Resolve every wall and bullet mesh once. Safe to call again; it caches. */
void t1_over_init(T1_MeshBank *b);

/* How many of the wall pieces the pack actually carries, for the report. A pack from
 * before the wall bake aliases these names onto the BULLET meshes, and sandbags then draw
 * as missiles standing on end, which is a wrong answer convincing enough to ship. */
int  t1_over_wall_pieces(void);

/* seen, shroud-culled, no mesh, submitted -- from the last wall pass. */
void t1_over_stats(int *out5);

/* The wall SHADOW sets: CYCLS0..10 and WOODS0..10, eleven unrotated pieces each. Call it
 * inside the caller's shadow state, beside the object shadow pass. */
long t1_over_draw_wall_shadows(T1_MeshBank *b, const T1_Cam *cam, const T1_Screen *scr,
                               const T1_Terrain *terr, const W98_Overlays *ov,
                               int (*shown)(int cx, int cz));

/* How many of the five wall types resolved a complete eleven-piece shadow set. */
int  t1_over_shadow_sets(void);

/* `shown` is called per cell and decides shroud culling; pass NULL to draw everything.
 * Walls have HEIGHT, so they are culled like objects rather than hidden under the veil. */
long t1_over_draw_walls(T1_MeshBank *b, const T1_Cam *cam, const T1_Screen *scr,
                        const T1_Terrain *terr, const W98_Overlays *ov,
                        int (*shown)(int cx, int cz));

long t1_over_draw_bullets(T1_MeshBank *b, const T1_Cam *cam, const T1_Screen *scr,
                          const T1_Terrain *terr, const W98_Overlays *ov,
                          int (*shown)(int cx, int cz));

#endif /* T1_OVER_H */
