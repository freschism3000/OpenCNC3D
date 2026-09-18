/* ====================================================================================
 *  rain_glsl.h -- the one piece of shader text the rain shares between passes.
 *
 *  TIER 2 ONLY, AND OURS. Nothing here is decoded from the cartridge; the console has
 *  no weather. This header exists because the same drop-impact ripple is wanted in
 *  programs that live in different files: the light pass (fx_post.h), which bends the
 *  normal of every upward-facing wet surface with it, the sea's surface (water_mod.h),
 *  which adds it on top of the wave field, and the wave field's own step, which uses
 *  the hashes to scatter drops. One function, spelled once.
 *
 *  THE RIPPLE IS PROCEDURAL, NOT A TEXTURE. A ring texture would need an animated
 *  atlas (frames of an expanding ring) and a seam-free tiling of it; a lattice of cells
 *  each holding one jittered ring on its own clock needs neither, costs nine cell
 *  evaluations per pixel, and never repeats visibly because every cell's phase is its
 *  own. The function returns the SLOPE of the ring field (d height / d xz), which is
 *  what a normal wants, so the caller adds it to n.xz and renormalises.
 *
 *  DETERMINISM, AND THE FOLD. Time is whatever the caller passes, and every caller
 *  passes the engine clock folded to this function's own period: two --shot runs of
 *  the same script ring the same rings, and a long session cannot coarsen the phase.
 *  The fold works because each cell's ring rate is one of THREE multiples of the base
 *  rate (1, 1.25, 1.5), never a continuous value: over a period of 4/rate seconds
 *  every cell completes a whole number of rings (4, 5 or 6), so fmodf(t, 4/rate) on
 *  the host leaves every phase exactly where it was. Variety comes from each cell's
 *  own phase offset, which folds for free. A continuous per-cell rate would have no
 *  common period and no fold, which is the trap the cloud drift recorded.
 * ==================================================================================== */
#ifndef CNC3D_RAIN_GLSL_H
#define CNC3D_RAIN_GLSL_H

#include <stdlib.h>
#include <string.h>

/* A hash without sin(): the sine hash loses its randomness on a GPU once its argument
   passes a few thousand, and a lattice over a whole map at several rings a cell gets
   there. This one is the fract/dot construction that is stable at any magnitude. */
static const char* RAIN_GLSL_RIPPLE =
    "float rn_h1(vec2 p) {\n"
    "    vec3 p3 = fract(vec3(p.xyx) * 0.1031);\n"
    "    p3 += dot(p3, p3.yzx + 33.33);\n"
    "    return fract((p3.x + p3.y) * p3.z);\n"
    "}\n"
    "vec2 rn_h2(vec2 p) {\n"
    "    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));\n"
    "    p3 += dot(p3, p3.yzx + 33.33);\n"
    "    return fract((p3.xx + p3.yz) * p3.zy);\n"
    "}\n"
    /* p: world xz in cells. t: seconds, already folded to 4/rate. scale: lattice cells
       per world cell. rate: base rings a second per lattice cell. density: the fraction
       of lattice cells that ring at all (a light rain has fewer rings, not fainter
       ones). Returns the slope of the ring field at p, in the lattice's own units. */
    "vec2 rn_ripple(vec2 p, float t, float scale, float rate, float density) {\n"
    "    vec2 q = p * scale;\n"
    "    vec2 cell = floor(q);\n"
    "    vec2 acc = vec2(0.0);\n"
    "    for (int j = -1; j <= 1; j++) {\n"
    "        for (int i = -1; i <= 1; i++) {\n"
    "            vec2 c = cell + vec2(float(i), float(j));\n"
    "            vec2 h = rn_h2(c);\n"
    "            if (h.x > density) continue;\n"
    /* each cell rings on its own clock: one of three rates, and its own phase */
    "            float mult = 1.0 + 0.25 * floor(h.y * 3.0);\n"
    "            float ph = fract(t * rate * mult + rn_h1(c + 17.0));\n"
    "            vec2 centre = c + 0.5 + (rn_h2(c + 5.0) - 0.5) * 0.7;\n"
    "            vec2 d = q - centre;\n"
    "            float dist = length(d);\n"
    "            if (dist < 1e-4) continue;\n"
    /* the ring: one crest at radius r, a wavelength wide, dying as it spreads. The
       amplitude falls LINEARLY with age: a square law was tried first and killed the
       ring while it was still a speck, so nothing on screen ever read as a circle. */
    "            float r = ph * 0.9;\n"
    "            float x = dist - r;\n"
    "            float env = exp(-x * x * 40.0);\n"
    "            float amp = 1.0 - ph;\n"
    /* d/dx of  cos(k x) * exp(-40 x^2)  with k = 12 */
    "            float dh = (-12.0 * sin(12.0 * x) - 80.0 * x * cos(12.0 * x)) * env * amp;\n"
    "            acc += (d / dist) * dh;\n"
    "        }\n"
    "    }\n"
    "    return acc;\n"
    "}\n";

static const char* RAIN_GLSL_PUDDLE =
/* WHERE A PUDDLE IS, spelled once. Two consumers ask this and they must agree to the
   pixel or splashes land where there is no water: the light pass, which shades the
   puddle, and the splash placer, which may only throw a crown inside one. The noise is
   the cloud deck's own tiling field at a finer scale, two octaves, histogram equalised
   at boot so a threshold on it is a coverage fraction rather than an arbitrary level.

   THE FETCH ITSELF IS NOT IN HERE, and that is the one thing not shared. A fragment
   shader may call texture2D and take the derivative it wants; a vertex shader may not,
   and has to name the level. So each caller does its own two fetches at the two UVs
   below and hands the results here. The weights, the threshold and the edge, which are
   the parts that decide the SHAPE, have one spelling. */
"vec2 rn_puddle_uv1(vec2 p, float inv) { return p * inv + vec2(0.31, 0.77); }\n"
"vec2 rn_puddle_uv2(vec2 p, float inv) { return p * inv * 2.7 + vec2(0.66, 0.12); }\n"
"float rn_puddle_mask(float n1, float n2) { return n1 * 0.62 + n2 * 0.38; }\n"
/* THE RAMP IS CENTRED ON THE THRESHOLD, not grown upward from it. Widening it upward
   is not a soft edge, it is a smaller puddle: the 50% contour walks inward and the
   whole shape shrinks and pales together, which is what the first attempt at fading
   these edges actually did. Centred, the contour stays where the coverage dial put it
   and the band feathers to either side of it, which is the thing that was wanted. */
"float rn_puddle_cover(float m, float cover, float edge) {\n"
"    float t = 1.0 - cover;\n"
"    return smoothstep(t - edge * 0.5, t + edge * 0.5, m);\n"
"}\n";

/* One shader source assembled from the version line, which GLSL wants first, every
   shared function above, and the pass's own body. The caller frees it.
   GLSL 1.20 has no #include, and glShaderSource with several strings is harder to
   read than one buffer; the light program is built the same way in fx_post.h. */
static char* rain_glsl_source(const char* body)
{
    static const char* head = "#version 120\n";
    const size_t n = strlen(head) + strlen(RAIN_GLSL_RIPPLE)
                  + strlen(RAIN_GLSL_PUDDLE) + strlen(body) + 1;
    char* out = (char*)malloc(n);
    if (!out) return 0;
    strcpy(out, head);
    strcat(out, RAIN_GLSL_RIPPLE);
    strcat(out, RAIN_GLSL_PUDDLE);
    strcat(out, body);
    return out;
}

#endif /* CNC3D_RAIN_GLSL_H */
