#ifndef OPENCNC3D_HEADING_SMOOTH_H
#define OPENCNC3D_HEADING_SMOOTH_H
#include <math.h>
/* DirType is a circle of 256 units. Keep fractional headings through rasterisation. */
static inline float heading_wrap(float a)
{
    a = fmodf(a, 256.0f);
    return a < 0.0f ? a + 256.0f : a;
}
static inline float heading_interpolate(int previous, int current, float alpha)
{
    if (previous < 0 || current < 0) return (float)current;
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    const int delta = ((current - previous + 128) & 255) - 128;
    return heading_wrap((float)previous + (float)delta * alpha);
}
#endif
