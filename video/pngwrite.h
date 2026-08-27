/* pngwrite.h -- minimal RGBA PNG writer, stored deflate, no zlib dependency.
 * Used by the preview tools to dump frames so they can be looked at. */
#ifndef PNGWRITE_H
#define PNGWRITE_H

int png_write_rgba(const char *path, const unsigned char *rgba, int w, int h);

/* Nearest-neighbour integer upscale, because these are DOS pixels and any
 * filtering is a lie about what the renderer produced. */
void png_nearest_scale(const unsigned char *src, int sw, int sh, unsigned char *dst, int scale);

#endif /* PNGWRITE_H */
