/*
 * t1_shroud.h -- the fog of war, draped over the heightfield.
 *
 * The engine already knows exactly which cells the player has seen and which are in
 * sight right now, and hands it over through GAME_STATE_SHROUD; wb_shroud reads it. So
 * the shroud here is a PROJECTION of the truth and never a second, guessed copy of it,
 * which is the same rule the desktop build follows.
 *
 * The recipe is the cartridge's: unmapped cells are opaque black, cells that have been
 * seen but are not currently watched are a partial veil, and the edges are SOFT because
 * the alpha is carried per CORNER, so two neighbouring cells in different states blend
 * across the boundary instead of showing a staircase. The desktop build's own note calls
 * this "edge cells translucent, alpha ceiling 200/255", and 200 is used here too.
 *
 * IT IS DRAWN ON THE TERRAIN'S OWN CORNERS, with the terrain's own SW-NE diagonal, so it
 * is watertight against the ground by construction rather than by a bias: a shroud on its
 * own flat plane would float over hills and sink into valleys.
 *
 * GLIDE ONLY. This pass needs per-vertex alpha and a blend, which the software rasteriser
 * has neither of; the software build is registered in docs/tier1-gap.md as having no
 * shroud rather than being given a fake one.
 */

#ifndef T1_SHROUD_H
#define T1_SHROUD_H

#include "softras.h"
#include "t1_cam.h"
#include "t1_terrain.h"

/* `vis` is 64*64 as wb_shroud fills it: 0 unmapped, 1 mapped, 2 visible.
 * `white` is any fully opaque white texel block; the shroud is that block modulated to
 * black, because this backend has no untextured path.
 * Returns the number of cells that actually drew. */
long t1_shroud_draw(const T1_Terrain *t, const T1_Cam *cam, const T1_Screen *scr,
                    const unsigned char *vis, const SR_Texture *white,
                    float wu, float wv,
                    int x0, int z0, int w, int h);

#endif /* T1_SHROUD_H */
