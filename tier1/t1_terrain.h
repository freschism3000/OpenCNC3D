/*
 * t1_terrain.h -- the console's own heightfield terrain, drawn by the CPU.
 *
 * Reads a .t1terr file (made by tools/win98/mkterrain.py from a baked scenario pack) and
 * draws it through softras under the recovered N64 camera. The file is laid out so that
 * loading is one read and a handful of pointers into the blob, the same zero-decode
 * arrangement game/dosbar.c uses for the sidebar pack.
 */

#ifndef T1_TERRAIN_H
#define T1_TERRAIN_H

#include "softras.h"
#include "t1_cam.h"

typedef struct
{
    unsigned char *blob;
    long           blobsize;

    /* The atlas is cut into pages, because the Voodoo 2's maximum texture is 256x256 and
     * the source atlas is 1024x1024. Every cell is a 24x24 tile at an integer origin, so
     * a page holds a 10x10 grid of them and a cell record is a page plus a tile
     * coordinate. The software renderer is happy with either: both are powers of two. */
    int  npages, page_sz, tile, per;
    const unsigned char *pal;        /* 768, 256 RGB triples; index 0 is a water hole */
    const unsigned char *pages;      /* npages * page_sz * page_sz palette indices */

    int  ncells;                     /* 4096, a dense 64x64 grid stored y*64+x */
    const unsigned char *cells;      /* ncells x 4 bytes: page, tilex, tiley, holes */

    const unsigned char *heights;    /* 65 x 65 per CORNER, one unit = 1/64 of a cell */
    const unsigned char *cmtint;     /* 65 x 65 u16 RGBA5551, currently unused */

    float base_y;                    /* the median corner height, subtracted from all */
    /* The lowest and highest corner on the map, in cells and already relative to base_y.
     * The view cull brackets its ray against these two planes, so they have to be the
     * real extremes rather than a guess: a hill above yhi would poke into the frame from
     * outside the culled box. */
    float ylo, yhi;
    unsigned char shade[65 * 65];    /* precomputed per corner, 0..255 */
    SR_Texture tex[8];               /* one per page; 3 for a typical mission */
} T1_Terrain;

int  t1_terrain_load(T1_Terrain *t, const char *path, char *err, int errlen);
void t1_terrain_free(T1_Terrain *t);

/* Height of a CORNER in cells, already relative to base_y. */
float t1_terrain_corner_y(const T1_Terrain *t, int cx, int cz);

/* Draws the cells of the rectangle [x0,x0+w) x [z0,z0+h). Returns pixels written. */
long t1_terrain_draw(T1_Terrain *t, SR_Target *tg, const T1_Cam *cam,
                     const T1_Screen *scr, int x0, int z0, int w, int h);

#endif /* T1_TERRAIN_H */
