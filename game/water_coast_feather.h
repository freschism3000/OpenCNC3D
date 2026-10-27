#ifndef OPENCNC3D_WATER_COAST_FEATHER_H
#define OPENCNC3D_WATER_COAST_FEATHER_H

#include <algorithm>
#include <cstring>
#include <vector>

/* Include after water_edt_2d. A coast colour feather is the dry bank becoming
   transparent over live water, not the static water painting becoming opaque.
   Assemble the COMPLETE authored template before extending its material; atlas
   slots and their replicated gutters are not neighbours in that space.

   Only 0 < alpha < 255 RGB is changed. Opaque bank RGB, all alpha, fully clear
   sea/floor RGB and missing icons stay byte exact. Missing icons cannot donate.
   Nearest opaque material seeds the extension, then a short harmonic solve
   softens donor boundaries without ever sampling the static wet painting. */
static size_t water_coast_feather_rgb(unsigned char* pixels, int w, int h,
        size_t row, int pixel, const unsigned char* alpha, size_t alphaRow,
        int alphaPixel, const unsigned char* occupied = NULL)
{
    if (!pixels || !alpha || w <= 0 || h <= 0 || pixel < 3 || alphaPixel < 1
        || row < (size_t)w * pixel || alphaRow < (size_t)w * alphaPixel) return 0;
    const size_t size = (size_t)w * h;
    std::vector<unsigned char> donor(size, 0), feather(size, 0);
    std::vector<int> targets;
    bool hasDonor = false;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const size_t i = (size_t)y * w + x;
        if (occupied && !occupied[i]) continue;
        const unsigned char a = alpha[(size_t)y * alphaRow + (size_t)x * alphaPixel];
        donor[i] = a == 255 ? 1 : 0;
        hasDonor |= donor[i] != 0;
        if (a > 0 && a < 255) { feather[i] = 1; targets.push_back((int)i); }
    }
    if (!hasDonor || targets.empty()) return 0;
    std::vector<float> distance(size), colour(size * 3, 0.0f);
    std::vector<int> nearest(size);
    water_edt_2d(donor.data(), distance.data(), nearest.data(), w, h);
    for (size_t i = 0; i < size; ++i) if (donor[i] || feather[i]) {
        const int src = donor[i] ? (int)i : nearest[i];
        if (src < 0) continue;
        const unsigned char* rgb = pixels + (size_t)(src / w) * row + (size_t)(src % w) * pixel;
        for (int c = 0; c < 3; ++c) colour[i * 3 + c] = rgb[c];
    }
    for (int pass = 0; pass < 24; ++pass) for (size_t q = 0; q < targets.size(); ++q) {
        const int i = targets[pass & 1 ? targets.size() - 1 - q : q], x = i % w, y = i / w;
        const int neighbours[4] = {x ? i - 1 : -1, x + 1 < w ? i + 1 : -1,
                                  y ? i - w : -1, y + 1 < h ? i + w : -1};
        float sum[3] = {0.0f, 0.0f, 0.0f}; int count = 0;
        for (int j : neighbours) if (j >= 0 && (donor[j] || feather[j])) {
            for (int c = 0; c < 3; ++c) sum[c] += colour[(size_t)j * 3 + c];
            ++count;
        }
        if (count) for (int c = 0; c < 3; ++c) colour[(size_t)i * 3 + c] = sum[c] / count;
    }
    for (int i : targets) {
        unsigned char* rgb = pixels + (size_t)(i / w) * row + (size_t)(i % w) * pixel;
        for (int c = 0; c < 3; ++c)
            rgb[c] = (unsigned char)std::max(0.0f, std::min(255.0f, colour[(size_t)i * 3 + c] + 0.5f));
    }
    return targets.size();
}

/* An atlas region's actual cell pixels and their location in the logical
   authored template. Row/pixel strides may address an RGB tile or an RGBA
   atlas. Call once with every available icon of ONE template and variant.
   Refresh each destination slot's own gutters after this function returns. */
struct WaterCoastFeatherRegion {
    unsigned char* pixels;
    const unsigned char* alpha;
    size_t row, alphaRow;
    int pixel, alphaPixel, x, y, w, h;
};

static size_t water_coast_feather_template(const std::vector<WaterCoastFeatherRegion>& regions,
                                          int w, int h)
{
    if (w <= 0 || h <= 0 || regions.empty()) return 0;
    const size_t size = (size_t)w * h;
    std::vector<unsigned char> rgb(size * 3, 0), alpha(size, 0), occupied(size, 0);
    for (const auto& r : regions) {
        if (!r.pixels || !r.alpha || r.pixel < 3 || r.alphaPixel < 1 || r.w <= 0 || r.h <= 0
            || r.x < 0 || r.y < 0 || r.x + r.w > w || r.y + r.h > h
            || r.row < (size_t)r.w * r.pixel || r.alphaRow < (size_t)r.w * r.alphaPixel) return 0;
        for (int y = 0; y < r.h; ++y) for (int x = 0; x < r.w; ++x) {
            const size_t i = (size_t)(r.y + y) * w + r.x + x;
            occupied[i] = 1;
            alpha[i] = r.alpha[(size_t)y * r.alphaRow + (size_t)x * r.alphaPixel];
            memcpy(&rgb[i * 3], r.pixels + (size_t)y * r.row + (size_t)x * r.pixel, 3);
        }
    }
    const size_t count = water_coast_feather_rgb(rgb.data(), w, h, (size_t)w * 3, 3,
                                                alpha.data(), (size_t)w, 1, occupied.data());
    if (!count) return 0;
    for (const auto& r : regions) for (int y = 0; y < r.h; ++y) for (int x = 0; x < r.w; ++x) {
        const size_t i = (size_t)(r.y + y) * w + r.x + x;
        if (alpha[i] > 0 && alpha[i] < 255)
            memcpy(r.pixels + (size_t)y * r.row + (size_t)x * r.pixel, &rgb[i * 3], 3);
    }
    return count;
}

#endif
