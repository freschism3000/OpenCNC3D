/* ====================================================================================
 *  rain_mod.h -- RAIN UNDER ENHANCED: streaks in the air, wet ground and hulls, drops
 *                on the water.
 *
 *  TIER 2 ONLY, AND OURS. The cartridge has no weather of any kind; nothing here is
 *  decoded from the ROM. It is registered with the known gaps as an authored addition,
 *  the same standing as the sun, the clouds and the sea's surface. CLASSIC, Win98 and
 *  any frame the post chain cannot run draw exactly what they drew before this file
 *  existed: every entry point below returns at once, or sets every rain uniform to
 *  zero, unless the chain is live and the rain switch is on.
 *
 *  THREE THINGS, IN THREE PLACES, ONE SWITCH:
 *
 *   1. THE STREAKS (this file, rain_streaks_draw). A fixed lattice of thin quads,
 *      each one drop's motion blur, drawn once per frame with a vertex shader that
 *      places every quad from the engine clock alone. The CPU uploads nothing per
 *      frame: the vertex array is built once and holds only SEEDS; the shader turns a
 *      seed and the time into a world position. A streak lives on a periodic lattice
 *      and the shader picks the copy nearest the camera's view, so panning the camera
 *      does not drag the rain with it: a drop is where it is in the world, and the
 *      lattice only re-tiles a whole period away, outside the view. Density on screen
 *      is constant at every zoom because the period follows the visible rectangle.
 *      Depth-tested against the world, depth writes OFF: a hill hides the rain behind
 *      it and the shroud blanket, drawn later, covers rain over ground nobody has seen.
 *      Drawn INSIDE the world pass, so the light pass shades a streak's pixels as the
 *      ground behind them (a streak in a cast shadow is darker): accepted, because the
 *      paint is thin and the alternative is a second depth-attached target.
 *
 *   2. THE WET LOOK (fx_post.h, the light pass; the uniforms are set here). Every
 *      surface the chain shades gets darker and richer, a Fresnel sheen of the sky, and
 *      a sun glint off a normal bent by drop-impact rings wherever it faces up. Flat
 *      ground gathers puddles that mirror the sky. Distance haze and an overcast tint
 *      finish it. The light pass already reconstructs a position and a normal for every
 *      pixel, which is why this costs no new pass and no new draw call. The sea is
 *      masked out of it through its own shore field (bound on unit 5 here): the
 *      surface below draws its own rain, and the post chain would otherwise wet the
 *      seabed it sees under the water a second time.
 *
 *   3. THE WATER (water_mod.h; the uniforms are set here). Drops land in the wave
 *      field as random presses each step, so the sea rings the way it rings for a
 *      hull, and the surface shader adds the same impact ripple the ground has and
 *      mirrors the overcast instead of a clear sky.
 *
 *  DETERMINISM. The only clock is g_fxTime (engine ticks), FOLDED to each consumer's
 *  own period before it is handed over (rain_time_streaks, rain_time_ripple), so a
 *  long session cannot coarsen a phase; the only random numbers are a fixed LCG at
 *  boot and hashes of cell coordinates and step counters in the shaders. Two --shot
 *  runs of one script are one picture.
 *
 *  EVERY NUMBER IS A DIAL in fx_state.h ("3d  RAIN  (ours)"). rain_fx 0 draws nothing
 *  and sets every rain uniform to zero, which is the gate's trap.
 * ==================================================================================== */
#ifndef CNC3D_RAIN_MOD_H
#define CNC3D_RAIN_MOD_H

#include "rain_glsl.h"

/* The lattice. 6000 quads is 24000 vertices in one client array, drawn in one call;
   the dial scales how many of them are drawn, never the array. */
#define RAIN_MAX_STREAKS 6000

static GLuint g_rainProg = 0;
static int    g_rainProgTried = 0;
static std::vector<float> g_rainVerts;    /* 4 floats a vertex: three seeds and one more */
static std::vector<float> g_rainCorners;  /* 2 floats a vertex: across (-1..1), along (0..1) */
static int    g_rainLogged = -1;          /* last state written to stderr, so it is once */

/* ---- is the rain on at all ------------------------------------------------------- */

static int rain_live(void)
{
    /* the chain must be running this frame: no chain, no rain, whatever the dial says */
    return g_fx.enabled && g_fx.rain_fx && g_fxActive && fx_gl_ready && g_fx.rain_amount > 0.0f;
}

/* The wetness the surfaces see, 0..1. Amount is the master; wet is how much of it
   reaches the ground. Kept as one function so every consumer agrees. */
static float rain_wetness(void)
{
    if (!rain_live()) return 0.0f;
    float w = g_fx.rain_amount * g_fx.rain_wet;
    return w < 0.0f ? 0.0f : (w > 1.0f ? 1.0f : w);
}

/* How overcast the world is, 0..1: the sea's reflection and the light pass both ask. */
static float rain_overcast(void)
{
    if (!rain_live()) return 0.0f;
    float o = g_fx.rain_overcast * g_fx.rain_amount;
    return o < 0.0f ? 0.0f : (o > 1.0f ? 1.0f : o);
}

/* ---- the clocks, folded ---------------------------------------------------------------
   Each consumer gets the engine clock folded to ITS period, so every phase built from
   it is exactly the phase an unfolded clock would give and never coarsens. The streaks
   fall at one of four speeds (0.8, 1.0, 1.2, 1.4 times the dial), so over 5H/speed
   seconds each completes 4, 5, 6 or 7 whole falls; the rings' period is in
   rain_glsl.h. fmodf of a negative clock (before the first engine dump) is negative
   and the shaders' fract() is happy with that. */
static float rain_time_streaks(void)
{
    const float h = g_fx.rain_height > 0.5f ? g_fx.rain_height : 0.5f;
    const float v = g_fx.rain_speed > 0.1f ? g_fx.rain_speed : 0.1f;
    return fmodf(g_fxTime, 5.0f * h / v);
}
static float rain_time_ripple(void)
{
    const float r = g_fx.rain_ripple_rate > 0.05f ? g_fx.rain_ripple_rate : 0.05f;
    return fmodf(g_fxTime, 4.0f / r);
}

/* ---- the streaks ----------------------------------------------------------------- */

/* THE VERTEX SHADER PLACES THE RAIN. gl_Vertex carries three seeds in [0,1) and a
   fourth in w; gl_MultiTexCoord0 is the corner: x across the streak (-1 or 1), y along
   it (0 at the drop, 1 at the tail). uFall is the unit direction a drop travels,
   already carrying the wind's slant, and uRight is the camera's own right vector so the
   quad faces the viewer across its width. The drop's ground point sits on the lattice;
   its height is a phase of the fall that the clock advances, so it falls, lands and is
   back at the top with the same seed. */
static const char* RAIN_VS =
    "#version 120\n"
    "uniform vec3 uRight; uniform vec3 uFall; uniform vec2 uCentre; uniform float uPeriod;\n"
    "uniform float uHeight; uniform float uTime; uniform float uSpeed; uniform float uLen; uniform float uWidth;\n"
    "uniform vec3 uCamPos; uniform float uNear; uniform vec4 uRect;\n"
    "varying vec2 vUV; varying float vFade;\n"
    "void main() {\n"
    "    vec4 s = gl_Vertex;\n"
    "    vec2 c = gl_MultiTexCoord0.xy;\n"
    /* the copy of this streak nearest the view's centre: floor(...+0.5) is a round */
    "    vec2 base = s.xz * uPeriod;\n"
    "    vec2 gp = base + floor((uCentre - base) / uPeriod + 0.5) * uPeriod;\n"
    /* THE MAP'S EDGE. Beyond the play rectangle there is no ground, only the clear
       colour, and the outermost ring of cells is shroud that nothing lifts: rain
       drawn out there hangs over a black void. So the rain fades out over the last
       cell and a half inside the rectangle, the way every object stops at its edge. */
    "    float edge = min(min(gp.x - uRect.x, uRect.z - gp.x), min(gp.y - uRect.y, uRect.w - gp.y));\n"
    "    float inMap = smoothstep(0.0, 1.5, edge);\n"
    /* one of four speeds, so the sheet does not march and the clock can fold */
    "    float sp = uSpeed * (0.8 + 0.2 * floor(s.w * 4.0));\n"
    "    float ph = fract(s.y + uTime * sp / uHeight);\n"
    "    float y = uHeight * (1.0 - ph);\n"
    /* back up the fall line from the ground point to that height */
    "    vec3 head = vec3(gp.x, 0.0, gp.y) - uFall * (y / max(-uFall.y, 0.2));\n"
    "    vec3 pos = head - uFall * (c.y * uLen) + uRight * (c.x * uWidth);\n"
    "    vUV = c;\n"
    /* fade in over the top of the volume (the respawn) and out close to the eye, where
       one quad would otherwise cross half the screen */
    "    float d = length(head - uCamPos);\n"
    "    vFade = smoothstep(0.0, 0.10, ph) * smoothstep(uNear, uNear * 2.5, d) * inMap;\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * vec4(pos, 1.0);\n"
    "}\n";

/* Soft across, brightest at the drop and fading down the tail. */
static const char* RAIN_FS =
    "#version 120\n"
    "varying vec2 vUV; varying float vFade; uniform vec4 uColor;\n"
    "void main() {\n"
    "    float w = 1.0 - vUV.x * vUV.x;\n"
    "    float l = 1.0 - vUV.y;\n"
    "    gl_FragColor = vec4(uColor.rgb, uColor.a * w * w * l * vFade);\n"
    "}\n";

static GLuint rain_link(const char* vs_src, const char* fs_src, const char* label)
{
    if (!fx_gl_ready) return 0;
    GLuint vs = fx_compile(GL_VERTEX_SHADER, vs_src, label);
    if (!vs) return 0;
    GLuint fs = fx_compile(GL_FRAGMENT_SHADER, fs_src, label);
    if (!fs) { fx_glDeleteShader(vs); return 0; }
    GLuint p = fx_glCreateProgram();
    fx_glAttachShader(p, vs);
    fx_glAttachShader(p, fs);
    fx_glLinkProgram(p);
    GLint ok = 0;
    fx_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048]; GLsizei n = 0;
        if (fx_glGetProgramInfoLog) fx_glGetProgramInfoLog(p, (GLsizei)sizeof log, &n, log);
        log[n < (GLsizei)sizeof log ? n : (GLsizei)sizeof log - 1] = 0;
        fprintf(stderr, "RAIN|program|%s FAILED TO LINK\n%s\n", label, log);
        fx_glDeleteProgram(p);
        p = 0;
    }
    fx_glDeleteShader(vs);
    fx_glDeleteShader(fs);
    return p;
}

/* The lattice, built once. A fixed LCG rather than rand(): the seeds must be the same
   on every machine and every run, and rand() promises neither. */
static void rain_boot(void)
{
    if (!g_rainVerts.empty()) return;
    g_rainVerts.resize((size_t)RAIN_MAX_STREAKS * 4 * 4);
    g_rainCorners.resize((size_t)RAIN_MAX_STREAKS * 4 * 2);
    unsigned lcg = 0x2545F491u;
    static const float cx[4] = { -1.0f, 1.0f, 1.0f, -1.0f };
    static const float cy[4] = {  0.0f, 0.0f, 1.0f,  1.0f };
    for (int i = 0; i < RAIN_MAX_STREAKS; i++) {
        float s[4];
        for (int k = 0; k < 4; k++) {
            lcg = lcg * 1664525u + 1013904223u;
            s[k] = (float)(lcg >> 8) * (1.0f / 16777216.0f);
        }
        for (int v = 0; v < 4; v++) {
            float* p = &g_rainVerts[((size_t)i * 4 + v) * 4];
            p[0] = s[0]; p[1] = s[1]; p[2] = s[2]; p[3] = s[3];
            float* c = &g_rainCorners[((size_t)i * 4 + v) * 2];
            c[0] = cx[v]; c[1] = cy[v];
        }
    }
}

/* Called from draw_frame after the combat effects and before the shroud blanket, in
   the world's own projection. Draws nothing unless the rain is live. */
static void rain_streaks_draw(void)
{
    const int live = rain_live() && g_fx.rain_streaks > 0.0f;
    if (g_rainLogged != live) {
        g_rainLogged = live;
        fprintf(stderr, "RAIN|%s|amount=%.2f|streaks=%.2f|wet=%.2f\n",
                live ? "on" : "off", g_fx.rain_amount, g_fx.rain_streaks, rain_wetness());
    }
    if (!live) return;
    if (!g_rainProg && !g_rainProgTried) {
        g_rainProgTried = 1;
        g_rainProg = rain_link(RAIN_VS, RAIN_FS, "rain");
    }
    if (!g_rainProg) return;
    rain_boot();

    /* how many of the lattice: the amount times the streak dial, on a square law so
       the bottom of the dial is a drizzle rather than a thin downpour */
    float dens = g_fx.rain_amount * g_fx.rain_streaks;
    if (dens > 1.0f) dens = 1.0f;
    int n = (int)((float)RAIN_MAX_STREAKS * dens * dens + 0.5f);
    if (n < 1) return;
    if (n > RAIN_MAX_STREAKS) n = RAIN_MAX_STREAKS;

    /* the volume: the visible ground rectangle plus the slant's reach, and never so
       small that the lattice period shows */
    const float vw = g_viewX1 - g_viewX0, vh = g_viewZ1 - g_viewZ0;
    const float height = g_fx.rain_height > 0.5f ? g_fx.rain_height : 0.5f;
    const float slant = g_fx.rain_slant;
    float period = (vw > vh ? vw : vh) * 1.30f + 2.0f * height * slant + 2.0f;
    if (period < 8.0f) period = 8.0f;
    const float cx = 0.5f * (g_viewX0 + g_viewX1), cz = 0.5f * (g_viewZ0 + g_viewZ1);
    /* the wind: a compass bearing, read the same way the clouds and the current are */
    const float rad = g_fx.rain_wind * (3.14159265358979f / 180.0f);
    float fx_ = sinf(rad) * slant, fy = -1.0f, fz = -cosf(rad) * slant;
    const float fl = sqrtf(fx_ * fx_ + fy * fy + fz * fz);
    fx_ /= fl; fy /= fl; fz /= fl;

    fx_glUseProgram(g_rainProg);
    fx_set3f(g_rainProg, "uRight", g_bbRight[0], g_bbRight[1], g_bbRight[2]);
    fx_set3f(g_rainProg, "uFall", fx_, fy, fz);
    fx_set2f(g_rainProg, "uCentre", cx, cz);
    fx_set1f(g_rainProg, "uPeriod", period);
    fx_set1f(g_rainProg, "uHeight", height);
    fx_set1f(g_rainProg, "uTime", rain_time_streaks());
    fx_set1f(g_rainProg, "uSpeed", g_fx.rain_speed);
    fx_set1f(g_rainProg, "uLen", g_fx.rain_len);
    fx_set1f(g_rainProg, "uWidth", g_fx.rain_width);
    fx_set3f(g_rainProg, "uCamPos", g_fxCamPos[0], g_fxCamPos[1], g_fxCamPos[2]);
    fx_set1f(g_rainProg, "uNear", 1.5f);
    {
        /* the play rectangle inset by its rim ring, which is always shroud */
        GLint l = fx_glGetUniformLocation(g_rainProg, "uRect");
        if (l >= 0) fx_glUniform4f(l, (float)(g_mapX + 1), (float)(g_mapY + 1),
                                   (float)(g_mapX + g_mapW - 1), (float)(g_mapY + g_mapH - 1));
    }
    {
        /* a cool, pale grey: rain against this camera's ground reads as light, not
           white; the alpha is the streak dial's, capped so a heavy fall thickens by
           count rather than by paint */
        float a = 0.30f * g_fx.rain_streaks;
        if (a > 0.45f) a = 0.45f;
        GLint l = fx_glGetUniformLocation(g_rainProg, "uColor");
        if (l >= 0) fx_glUniform4f(l, 0.78f, 0.83f, 0.92f, a);
    }

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(4, GL_FLOAT, 0, &g_rainVerts[0]);
    glTexCoordPointer(2, GL_FLOAT, 0, &g_rainCorners[0]);
    glDrawArrays(GL_QUADS, 0, n * 4);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);

    fx_glUseProgram(0);
    /* exactly the state the effects pass left: depth writes on, blend off, white */
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

/* ---- the splashes ---------------------------------------------------------------- *
 *
 *  A CROWN WHERE A DROP LANDS IN STANDING WATER, and nowhere else. Rain on dry ground
 *  throws a ring and no crown; rain into a puddle throws both. So a crown is placed only
 *  where the light pass would shade a puddle, which is why the rule for where a puddle
 *  IS lives in rain_glsl.h and is asked by both: if the two ever disagreed, crowns would
 *  stand on dry sand and no pixel count could tell, because a crown in the wrong place
 *  is exactly as many pixels as a crown in the right one. That is what G225 measures.
 *
 *  THE PLACING IS ON THE CPU AND THE CULLING IS ON THE GPU, which is the opposite way
 *  round from the streaks and there is a reason. A streak hangs in the air, so the whole
 *  lattice can live in the vertex shader off a flat y = 0 plane and be depth tested
 *  against whatever it passes in front of. A crown stands ON THE GROUND, so it needs the
 *  terrain's height at its own point, and that is a heightfield lookup no vertex shader
 *  has. The CPU therefore walks a world lattice, asks terrain_y for the height and the
 *  terrain's own corner normals for the slope, and hands up a few hundred points; the
 *  shader then asks the shared puddle rule at each and collapses the quad where there is
 *  no water. The expensive question is answered once a site, the cheap one costs two
 *  vertex fetches.
 *
 *  A SITE IS WORLD LOCKED, by the same nearest-copy round the streaks use, because a
 *  puddle is world locked: a crown that slid with the camera would be standing where
 *  there is no water a moment later.
 *
 *  DETERMINISM. The seeds are a hash of the site index and nothing else, so site 400 is
 *  in the same place whatever was drawn before it, and the clock is folded to exactly
 *  one splash period before it is handed over. Two --shot runs are one picture.
 *
 *  VERTEX TEXTURE FETCH is required and is asked for once. A GL that reports no vertex
 *  texture units draws no splashes and says so. Tier 2 only, like the rest of the rain. */

#define RAIN_MAX_SPLASH 1500

static GLuint g_rainSplashProg = 0;
static int    g_rainSplashTried = 0;
static int    g_rainSplashVTex = -1;      /* vertex texture units, asked once          */
static std::vector<float> g_rainSplashV;  /* 4 floats a vertex: world xyz and a seed   */
static std::vector<float> g_rainSplashC;  /* 2 floats a vertex: the corner, built once */

/* The crown's own clock. One rate for every site, so the period is exactly one splash
   and each site's own phase offset folds with it. */
static float rain_time_splash(void)
{
    const float r = g_fx.rain_splash_rate > 0.05f ? g_fx.rain_splash_rate : 0.05f;
    return fmodf(g_fxTime, 1.0f / r);
}

/* A hash of the site index, not a running generator: the sites must not depend on the
   order they were asked for, and every build must place them identically. */
static float rain_splash_hash(unsigned i, unsigned k)
{
    unsigned h = i * 0x9E3779B1u + k * 0x85EBCA77u;
    h ^= h >> 15; h *= 0x2C1B3C6Du;
    h ^= h >> 12; h *= 0x297A2D39u;
    h ^= h >> 15;
    return (float)(h >> 8) * (1.0f / 16777216.0f);
}

static const char* SPLASH_VS_BODY =
    "uniform vec3 uRight; uniform vec3 uUp; uniform sampler2D uCloud;\n"
    "uniform float uPuddleInv; uniform float uPuddle; uniform float uPuddleEdge;\n"
    "uniform float uPuddleIn;\n"
    "uniform sampler2D uWater; uniform int uWaterOn; uniform vec2 uWaterScale;\n"
    "uniform float uTime; uniform float uRate; uniform float uSize;\n"
    "uniform vec3 uCamPos; uniform float uNear;\n"
    "varying vec2 vUV; varying float vFade;\n"
    "void main() {\n"
    "    vec3 base = gl_Vertex.xyz;\n"
    "    vec2 c = gl_MultiTexCoord0.xy;\n"
    "    vUV = c;\n"
    /* THE OPEN SEA IS NOT A PUDDLE. The slope test passes over water, because the seabed
       under it is flat, and the noise does not care what it lies over: the first cut
       stood crowns all across the bay. The sea has its own rain, drops pressed into the
       wave field and rings on the surface, so a land crown out there is the same weather
       drawn twice. The field that says where the water is is the one the light pass
       masks the wet term with, asked here at the same point. */
    "    if (uWaterOn == 1 &&\n"
    "        texture2DLod(uWater, base.xz * uWaterScale, 0.0).a > 0.35)\n"
    "        { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); vFade = 0.0; return; }\n"
    /* the shared rule, at level 0 because a vertex fetch has no derivative to pick one */
    "    float n1 = texture2DLod(uCloud, rn_puddle_uv1(base.xz, uPuddleInv), 0.0).r;\n"
    "    float n2 = texture2DLod(uCloud, rn_puddle_uv2(base.xz, uPuddleInv), 0.0).r;\n"
    /* A CROWN GOES WELL INSIDE A PUDDLE, NOT ON ITS RIM. The light pass draws water
       wherever the mask clears its threshold, and that threshold is nearly a step, so a
       crown keyed on the same number stands as often on the rim as in the middle, and a
       rim pixel is half dry ground. Asking for a coverage a margin STRICTER than the
       shading uses keeps the crowns in water the eye can see. The rule itself is still
       the shared one; only the threshold handed to it moves. */
    "    float cover = rn_puddle_cover(rn_puddle_mask(n1, n2),\n"
    "                                  max(uPuddle - uPuddleIn, 0.0), uPuddleEdge);\n"
    /* the crown rises fast and settles back: a square root in, linear out */
    "    float ph = fract(gl_Vertex.w + uTime * uRate);\n"
    "    float d = length(base - uCamPos);\n"
    "    vFade = (1.0 - ph) * smoothstep(0.45, 0.90, cover) * smoothstep(uNear, uNear * 2.0, d);\n"
    "    if (vFade <= 0.002) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }\n"
    "    float s = uSize * sqrt(ph);\n"
    "    vec3 pos = base + uRight * (c.x * s * 0.62) + uUp * (c.y * s);\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * vec4(pos, 1.0);\n"
    "}\n";

/* The crown itself: a wall that leans outward as it rises, and a bright bead where it
   meets the water. The first cut drew both as hairlines and read as nothing at all at
   this camera, so both are wider than the physics wants. */
static const char* SPLASH_FS =
    "#version 120\n"
    "varying vec2 vUV; varying float vFade; uniform vec4 uColor;\n"
    "void main() {\n"
    "    float x = abs(vUV.x), y = clamp(vUV.y, 0.0, 1.0);\n"
    "    float w = (x - y * 0.85) * 3.2;\n"
    "    float arm = exp(-w * w) * (1.0 - y * 0.70);\n"
    "    float bead = exp(-x * x * 6.0) * (1.0 - smoothstep(0.0, 0.42, y));\n"
    "    float a = clamp(arm * 0.85 + bead, 0.0, 1.0) * vFade;\n"
    "    gl_FragColor = vec4(uColor.rgb, uColor.a * a);\n"
    "}\n";

static void rain_splash_boot(void)
{
    if (!g_rainSplashC.empty()) return;
    g_rainSplashC.resize((size_t)RAIN_MAX_SPLASH * 4 * 2);
    g_rainSplashV.resize((size_t)RAIN_MAX_SPLASH * 4 * 4);
    static const float cx[4] = { -1.0f, 1.0f, 1.0f, -1.0f };
    static const float cy[4] = {  0.0f, 0.0f, 1.0f,  1.0f };
    for (int i = 0; i < RAIN_MAX_SPLASH; i++)
        for (int v = 0; v < 4; v++) {
            float* c = &g_rainSplashC[((size_t)i * 4 + v) * 2];
            c[0] = cx[v]; c[1] = cy[v];
        }
}

/* Called from draw_frame straight after the streaks, in the world's own projection. */
static void rain_splash_draw(void)
{
    if (!rain_live() || g_fx.rain_splash <= 0.0f || g_fx.rain_puddle <= 0.0f) return;
    if (!g_fxCloudTex) return;               /* the puddle noise IS the cloud deck's   */
    if (g_rainSplashVTex < 0) {
        GLint u = 0;
        glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &u);
        g_rainSplashVTex = (int)u;
        if (g_rainSplashVTex <= 0)
            fprintf(stderr, "RAIN|splash|this GL has no vertex texture units; no splashes\n");
    }
    if (g_rainSplashVTex <= 0) return;
    if (!g_rainSplashProg && !g_rainSplashTried) {
        g_rainSplashTried = 1;
        char* vs = rain_glsl_source(SPLASH_VS_BODY);
        if (vs) { g_rainSplashProg = rain_link(vs, SPLASH_FS, "splash"); free(vs); }
    }
    if (!g_rainSplashProg) return;
    rain_splash_boot();

    float dens = g_fx.rain_amount * g_fx.rain_splash;
    if (dens > 1.0f) dens = 1.0f;
    int want = (int)((float)RAIN_MAX_SPLASH * dens * dens + 0.5f);
    if (want < 1) return;
    if (want > RAIN_MAX_SPLASH) want = RAIN_MAX_SPLASH;

    /* the same lattice the streaks stand on, so the two agree about where the rain is */
    const float vw = g_viewX1 - g_viewX0, vh = g_viewZ1 - g_viewZ0;
    float period = (vw > vh ? vw : vh) * 1.20f + 2.0f;
    if (period < 8.0f) period = 8.0f;
    const float ccx = 0.5f * (g_viewX0 + g_viewX1), ccz = 0.5f * (g_viewZ0 + g_viewZ1);
    const float rx0 = (float)(g_mapX + 1), rz0 = (float)(g_mapY + 1);
    const float rx1 = (float)(g_mapX + g_mapW - 1), rz1 = (float)(g_mapY + g_mapH - 1);
    const float flat = g_fx.rain_puddle_flat < 0.998f ? g_fx.rain_puddle_flat : 0.998f;

    int n = 0;
    for (int i = 0; i < want; i++) {
        const float bx = rain_splash_hash((unsigned)i, 1u) * period;
        const float bz = rain_splash_hash((unsigned)i, 2u) * period;
        const float gx = bx + floorf((ccx - bx) / period + 0.5f) * period;
        const float gz = bz + floorf((ccz - bz) / period + 0.5f) * period;
        if (gx < rx0 || gx > rx1 || gz < rz0 || gz > rz1) continue;
        /* THE SAME NORMAL THE LIGHT PASS SEES, not a plausible one. A central difference
           of terrain_y was tried and straddles cell boundaries, so it answered a
           different question from the one the shader asks. terrain_corner_normal is what
           the ground-normal target rasterises, the target is Gouraud, so the four corners
           are blended the same way and the result normalised the same way. */
        const int   cx = (int)gx, cz = (int)gz;
        const float fx = gx - (float)cx, fz = gz - (float)cz;
        float n00[3], n10[3], n01[3], n11[3], nn[3];
        terrain_corner_normal(cx,     cz,     n00);
        terrain_corner_normal(cx + 1, cz,     n10);
        terrain_corner_normal(cx,     cz + 1, n01);
        terrain_corner_normal(cx + 1, cz + 1, n11);
        for (int k = 0; k < 3; k++)
            nn[k] = (n00[k] * (1.0f - fx) + n10[k] * fx) * (1.0f - fz)
                  + (n01[k] * (1.0f - fx) + n11[k] * fx) * fz;
        const float nl = sqrtf(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
        if (nl < 1e-4f || nn[1] / nl < flat) continue;
        const float y = terrain_y(gx, gz);
        const float seed = rain_splash_hash((unsigned)i, 3u);
        for (int v = 0; v < 4; v++) {
            float* p = &g_rainSplashV[((size_t)n * 4 + v) * 4];
            p[0] = gx; p[1] = y; p[2] = gz; p[3] = seed;
        }
        n++;
    }
    if (n < 1) return;

    fx_glUseProgram(g_rainSplashProg);
    fx_set3f(g_rainSplashProg, "uRight", g_bbRight[0], g_bbRight[1], g_bbRight[2]);
    fx_set3f(g_rainSplashProg, "uUp", g_bbUp[0], g_bbUp[1], g_bbUp[2]);
    fx_set3f(g_rainSplashProg, "uCamPos", g_fxCamPos[0], g_fxCamPos[1], g_fxCamPos[2]);
    fx_set1f(g_rainSplashProg, "uNear", 1.5f);
    fx_set1f(g_rainSplashProg, "uTime", rain_time_splash());
    fx_set1f(g_rainSplashProg, "uRate", g_fx.rain_splash_rate);
    fx_set1f(g_rainSplashProg, "uSize", g_fx.rain_splash_size);
    /* the same clamps the light pass puts on the same dials, so the two cannot part
       company at the bottom of a slider */
    fx_set1f(g_rainSplashProg, "uPuddleInv",
             1.0f / (g_fx.rain_puddle_size > 1.0f ? g_fx.rain_puddle_size : 1.0f));
    fx_set1f(g_rainSplashProg, "uPuddle", g_fx.rain_puddle);
    fx_set1f(g_rainSplashProg, "uPuddleEdge",
             g_fx.rain_puddle_edge > 0.002f ? g_fx.rain_puddle_edge : 0.002f);
    fx_set1f(g_rainSplashProg, "uPuddleIn", 0.10f);
    fx_set1i(g_rainSplashProg, "uCloud", 0);
    {
        const int wOn = (g_waterFieldTex && g_gridW > 0 && g_gridH > 0 && water_fx_wanted()
                         && !strcmp(g_waterFieldScen, g_pack.scen)) ? 1 : 0;
        fx_set1i(g_rainSplashProg, "uWater", 1);
        fx_set1i(g_rainSplashProg, "uWaterOn", wOn);
        fx_set2f(g_rainSplashProg, "uWaterScale", g_gridW > 0 ? 1.0f / (float)g_gridW : 0.0f,
                                                  g_gridH > 0 ? 1.0f / (float)g_gridH : 0.0f);
        glActiveTexture(GL_TEXTURE1);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, wOn ? g_waterFieldTex : 0);
        glActiveTexture(GL_TEXTURE0);
    }
    {
        /* the streaks' pale cool grey, brighter: a crown catches more of the sky than a
           falling drop does, and it has to read at a quarter of a cell across */
        float a = 0.85f * g_fx.rain_splash;
        if (a > 0.95f) a = 0.95f;
        GLint l = fx_glGetUniformLocation(g_rainSplashProg, "uColor");
        if (l >= 0) fx_glUniform4f(l, 0.86f, 0.90f, 0.96f, a);
    }

    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_fxCloudTex);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(4, GL_FLOAT, 0, &g_rainSplashV[0]);
    glTexCoordPointer(2, GL_FLOAT, 0, &g_rainSplashC[0]);
    glDrawArrays(GL_QUADS, 0, n * 4);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);

    fx_glUseProgram(0);
    /* EXACTLY THE STATE THIS FOUND, and texturing is part of the state. The streaks run
       immediately before and leave unit 0 with GL_TEXTURE_2D DISABLED; this pass enables
       it to read the puddle noise and has to put it back, or every untextured draw after
       it -- the shroud, the bar, the HUD plates -- samples whatever is bound instead of
       taking its vertex colour. Leaving an enable behind is the kind of fault that shows
       up somewhere else entirely and gets blamed on whatever drew there. */
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

/* ---- the wet look: what the light pass is told --------------------------------------
   Forward-declared in fx_post.h and called with the light program bound, after its own
   uniforms. Zero everywhere when the rain is off, so the shader's rain block is a
   uniform branch that costs nothing. */
static void rain_light_uniforms(GLuint prog)
{
    const float wet = rain_wetness();
    fx_set1f(prog, "uRainWet", wet);
    fx_set1f(prog, "uRainDark", wet > 0.0f ? g_fx.rain_dark : 0.0f);
    fx_set1f(prog, "uRainGloss", g_fx.rain_gloss > 1.0f ? g_fx.rain_gloss : 1.0f);
    fx_set1f(prog, "uRainSpec", wet > 0.0f ? g_fx.rain_spec : 0.0f);
    fx_set1f(prog, "uRainRipple", wet > 0.0f ? g_fx.rain_ripple : 0.0f);
    fx_set1f(prog, "uRainRippleScale", g_fx.rain_ripple_scale > 0.5f ? g_fx.rain_ripple_scale : 0.5f);
    fx_set1f(prog, "uRainRippleRate", g_fx.rain_ripple_rate);
    /* the puddle mask reads the cloud deck's noise; no deck, no puddles */
    fx_set1f(prog, "uRainPuddle", (wet > 0.0f && g_fxCloudTex) ? g_fx.rain_puddle * wet : 0.0f);
    fx_set1f(prog, "uRainHaze", rain_live() ? g_fx.rain_haze * g_fx.rain_amount : 0.0f);
    {
        /* THE HAZE STARTS AT THE GROUND THE CAMERA LOOKS AT. A fog from the eye out
           washed the whole frame one even grey, because from this rig the nearest
           ground and the farthest are within a few cells of each other; measured
           from a little short of the look-at point it is clear in the middle of the
           view and greys toward the far edge, which is what distance looks like. */
        const float ty = terrain_y(g_camX, g_camZ);
        const float dx = g_fxCamPos[0] - g_camX, dy = g_fxCamPos[1] - ty, dz = g_fxCamPos[2] - g_camZ;
        fx_set1f(prog, "uRainHazeNear", 0.85f * sqrtf(dx * dx + dy * dy + dz * dz));
    }
    fx_set1f(prog, "uRainOvercast", rain_overcast());
    fx_set1f(prog, "uRainTime", rain_time_ripple());
    /* the sky a wet surface mirrors: an overcast grey-blue, not the clear sky's cobalt */
    fx_set3f(prog, "uRainSky", 0.62f, 0.66f, 0.72f);
    /* the rings' count and contrast, the sheen, and the puddles' pattern scale */
    fx_set1f(prog, "uRainRippleDensity", g_fx.rain_ripple_density);
    fx_set1f(prog, "uRainRippleShade", g_fx.rain_ripple_shade);
    fx_set1f(prog, "uRainSheen", wet > 0.0f ? g_fx.rain_sheen : 0.0f);
    fx_set1f(prog, "uRainPuddleInv", 1.0f / (g_fx.rain_puddle_size > 1.0f ? g_fx.rain_puddle_size : 1.0f));
    /* THE DERIVED RELIEF. There is no normal map anywhere in this game's data, so the
       light pass reads the art's own painted shading back as a height field; these two
       say how hard, on the ground and on everything standing on it. */
    fx_set1f(prog, "uRainRelief", wet > 0.0f ? g_fx.rain_relief : 0.0f);
    fx_set1f(prog, "uRainReliefBody", wet > 0.0f ? g_fx.rain_relief_body : 0.0f);
    fx_set1f(prog, "uRainPuddleMirror", g_fx.rain_puddle_mirror);
    fx_set1f(prog, "uRainPuddleEdge", g_fx.rain_puddle_edge > 0.002f ? g_fx.rain_puddle_edge : 0.002f);
    fx_set1f(prog, "uRainPuddleFlat", g_fx.rain_puddle_flat < 0.998f ? g_fx.rain_puddle_flat : 0.998f);
    /* the reflection march runs only where there is a puddle to put it in */
    fx_set1f(prog, "uRainPuddleSSR", (wet > 0.0f && g_fx.rain_puddle > 0.0f) ? g_fx.rain_puddle_ssr : 0.0f);
    /* THE GROUND FOG. An eight-step march through the cloud deck's own noise, read as
       a two-octave field drifting on the wind. Each octave's drift is a phase in tile
       periods folded to [0,1) host-side, the cloud drift's own fold, so a long session
       cannot coarsen it; the shader slides the pattern along the wind frame's x axis,
       where a whole period is exactly one repeat of the mask. The fog has its base at
       the map's mainland level (the terrain's zero) and thins upward, so hills stand
       out of it and the sea sits deeper in it. */
    {
        const int live = rain_live();
        const float fog = live ? g_fx.rain_fog * g_fx.rain_amount : 0.0f;
        const float scale = g_fx.rain_fog_scale > 1.0f ? g_fx.rain_fog_scale : 1.0f;
        const float rad = g_fx.rain_wind * (3.14159265358979f / 180.0f);
        fx_set1f(prog, "uRainFog", fog);
        fx_set1f(prog, "uRainFogHeight", g_fx.rain_fog_height > 0.1f ? g_fx.rain_fog_height : 0.1f);
        fx_set1f(prog, "uRainFogBase", 0.0f);
        fx_set1f(prog, "uRainFogInvScale", 1.0f / scale);
        fx_set1f(prog, "uRainFogDetail", g_fx.rain_fog_detail);
        fx_set2f(prog, "uRainFogDir", sinf(rad), -cosf(rad));
        fx_set1f(prog, "uRainFogDrift1", fmodf(g_fx.rain_fog_speed * g_fxTime / scale, 1.0f));
        fx_set1f(prog, "uRainFogDrift2", fmodf(g_fx.rain_fog_speed * 1.6f * 2.7f * g_fxTime / scale, 1.0f));
    }
}

/* THE SEA'S OWN MASK for the light pass, on unit 5 (free there: 0 colour, 1 depth,
   2 shadow, 3 cloud, 4 ground normal). The shore field's alpha is 1 over water, and the
   light pass zeroes the wet term where it reads 1. The field only exists once the
   Enhanced sea has built it for this map; with no field the pass is told so and the
   sea, if any, is wetted like ground, which is the pre-existing gap and not a new one. */
static void rain_light_bind(GLuint prog)
{
    /* AND IT MUST BE THIS MAP'S FIELD. Nothing rebuilds or frees the shore field
       except the sea's own draw, which does not run when the Water Shader row is off
       or when the pack carries no water tiles, so the texture left standing is the
       previous mission's coastline. Bound unchecked it stamps that coastline across
       the new map, switching the wet term off along a shore that is not there. */
    const int on = (rain_wetness() > 0.0f && g_waterFieldTex && g_gridW > 0 && g_gridH > 0
                    && water_fx_wanted()
                    && !strcmp(g_waterFieldScen, g_pack.scen)) ? 1 : 0;
    fx_set1i(prog, "uRainWater", 5);
    fx_set1i(prog, "uRainWaterOn", on);
    fx_set2f(prog, "uRainWaterScale", g_gridW > 0 ? 1.0f / (float)g_gridW : 0.0f,
                                       g_gridH > 0 ? 1.0f / (float)g_gridH : 0.0f);
    glActiveTexture(GL_TEXTURE5);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, on ? g_waterFieldTex : 0);
    glActiveTexture(GL_TEXTURE0);
}
static void rain_light_unbind(void)
{
    glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0);
}

/* ---- the water: what the wave field and the surface are told ------------------------
   Forward-declared in water_mod.h. The step counter is the seed: a different scatter
   of drops every step, the same scatter on every run. */
static void rain_wave_uniforms(GLuint prog, int step)
{
    const int live = rain_live();
    /* per-texel probability a drop lands this step; the field is 12 texels a cell */
    const float p = live ? g_fx.rain_drops * g_fx.rain_amount * 0.004f : 0.0f;
    fx_set1f(prog, "uRainP", p);
    fx_set1f(prog, "uRainF", live ? g_fx.rain_drop_force : 0.0f);
    fx_set1f(prog, "uRainSeed", (float)(step % 4096));
}

static void rain_water_uniforms(GLuint prog)
{
    const int live = rain_live();
    const float amount = live ? g_fx.rain_amount : 0.0f;
    fx_set1f(prog, "uRainRipple", live ? g_fx.rain_ripple * amount * g_fx.rain_water_ripple : 0.0f);
    fx_set1f(prog, "uRainRippleScale", g_fx.rain_ripple_scale > 0.5f ? g_fx.rain_ripple_scale : 0.5f);
    fx_set1f(prog, "uRainRippleRate", g_fx.rain_ripple_rate);
    fx_set1f(prog, "uRainRippleDensity", g_fx.rain_ripple_density);
    fx_set1f(prog, "uRainTime", rain_time_ripple());
    /* the wave field's amplitude window: a drop's ring is far smaller than a hull's
       trough, so under rain the window opens (1 is the dry window, 0.3 lets rings in) */
    fx_set1f(prog, "uRainLive", live && g_fx.rain_drops > 0.0f ? 1.0f - 0.5f * amount : 1.0f);
    fx_set1f(prog, "uRainOvercast", rain_overcast());
    fx_set1f(prog, "uRainSea", live ? g_fx.rain_sea : 0.0f);
    fx_set3f(prog, "uRainSky", 0.62f, 0.66f, 0.72f);
}

#endif /* CNC3D_RAIN_MOD_H */
