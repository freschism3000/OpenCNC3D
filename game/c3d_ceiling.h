/* THE RENDERER'S MAP CEILING, AND THE MAP FORMAT'S STRIDE, WHICH ARE NOT THE SAME NUMBER.
 *
 * They were one number for as long as there was one map size, and separating them is the
 * whole of what lets the ceiling move.
 *
 *   C3D_MAP_MAX     how wide a world this build's STORAGE can hold. Every grid-shaped
 *                   static in the renderer and the editor is sized from it once, and
 *                   every INDEX into those grids strides by the LIVE dims (g_gridW,
 *                   g_gridH) rather than by this. Raising it costs bytes and nothing
 *                   else.
 *
 *   C3D_INI_STRIDE  the stride a big map's own FILES are written in: a flat cell number
 *                   in a mission INI, or the key of a sparse .BIN record, is
 *                   y * C3D_INI_STRIDE + x. This is frozen by every map that already
 *                   exists and by the engine that reads them, and it must NOT follow the
 *                   ceiling. If it did, raising C3D_MAP_MAX would silently rewrite the
 *                   cell numbering of every big map ever saved: the same file would mean
 *                   different ground.
 *
 * The distinction is the same one the engine had to learn the hard way on its own side of
 * the seam, where a build's cell width had been used as a file's cell width and a map
 * loaded as scattered rows. A number that describes a FILE and a number that describes
 * THIS BUILD have to be able to move independently, and they can only do that if they are
 * spelled differently.
 *
 * A legacy (non-big) map's stride is 64 and is spelled inline where it is used, because 64
 * is that format's own constant and shares nothing with either of these.
 *
 * Kept in its own header because shroud_mod.h needs the ceiling and is included well above
 * the renderer's grid seam; before this, the shroud sized itself from the ENGINE's map
 * constant instead, which is a different number again and left the shroud behind whenever
 * the renderer's ceiling moved.
 */
#ifndef C3D_CEILING_H
#define C3D_CEILING_H

/* 256 as of 27 Aug 2026, from 128. The cost is bytes of BSS: everything sized from it is
 * static at the ceiling, so the whole renderer-plus-editor grid set grows about fourfold,
 * from roughly one megabyte to roughly four. Nothing here is per-frame work: the loops all
 * run over the live grid, not the ceiling.
 *
 * It is NOT the engine's limit. The classic engine walks a 128 grid and the XL engine a
 * 1024 one; this says only how much room the renderer keeps. It has to be at least as
 * large as anything a loaded engine can hand back, which cnc_eyes.cpp asserts. */
#define C3D_MAP_MAX  256
#define C3D_CORN_MAX (C3D_MAP_MAX + 1)

/* The big-map file format's own stride, and the reason it is a separate name. 128 is what
 * every existing Version=1 map is written in and what the engine's own reader assumes for
 * that format. It moves only when a NEW format is introduced that says which stride it is
 * in, never as a side effect of giving this build more room. */
#define C3D_INI_STRIDE 128
#define C3D_INI_CELLS  (C3D_INI_STRIDE * C3D_INI_STRIDE)

#endif /* C3D_CEILING_H */
