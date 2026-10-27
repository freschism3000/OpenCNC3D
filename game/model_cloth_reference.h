#ifndef OPENCNC3D_MODEL_CLOTH_REFERENCE_H
#define OPENCNC3D_MODEL_CLOTH_REFERENCE_H
#include <stddef.h>
#include <math.h>

/* The explicit ORM alpha is linear coverage, not a colour classification. Only its
   paired albedo participates. Keep every shade and pattern relative to this material's
   measured mean instead of borrowing the luminance of an older soldier's texture. */
static float m3_cloth_reference_rgba(const unsigned char* base,
                                   const unsigned char* orm, size_t pixels)
{
    if (!base || !orm) return 0.0f;
    double sum = 0.0, weight = 0.0;
    const double luma[3] = { 0.2126, 0.7152, 0.0722 };
    for (size_t p = 0; p < pixels; ++p) {
        const double mask = (double)orm[p * 4 + 3] / 255.0;
        if (mask <= 0.0) continue;
        double lum = 0.0;
        for (int c = 0; c < 3; ++c) {
            const double s = (double)base[p * 4 + c] / 255.0;
            const double linear = s <= 0.04045 ? s / 12.92 : pow((s + 0.055) / 1.055, 2.4);
            lum += linear * luma[c];
        }
        sum += lum * mask;
        weight += mask;
    }
    return weight > 0.0 ? (float)(sum / weight) : 0.0f;
}
#endif
