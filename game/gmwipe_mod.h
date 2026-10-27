/* ====================================================================================
 *  gmwipe_mod.h -- SPACE cycles the graphics mode, live, by tearing the picture up.
 *
 *  WHAT A PLAYER SEES. SPACE takes the game to the next graphics mode (Classic, then
 *  N64, then Enhanced, then Classic again; where N64 is not offered, as in Red Alert,
 *  Classic and Enhanced in turn). The old picture is torn up cell by cell from left to
 *  right, each piece turning over and flying off to the right like a carpet of tiles
 *  ripped up from one end, with the new mode already there beneath; the ground shakes
 *  and a low rumble goes with it (THE TILE RIP, below). The new mode's name comes up
 *  large at the top of the screen, holds, and fades.
 *
 *  HOW. The old mode's last finished frame is copied into a texture (a snapshot), the
 *  mode is switched through the same apply the Visuals screen uses (so the choice is
 *  saved like any other), and while the rip runs each new frame is drawn normally with
 *  the snapshot's pieces drawn back over it, flat where the front has not reached them.
 *  Nothing is rendered twice: the pieces are a still picture for the second and a half
 *  the rip takes, which is what makes the switch free of cost while it happens.
 *
 *  WHERE IN THE FRAME. After fx_present, when the finished picture (post chain, sidebar
 *  and all) is on the real framebuffer, and before the pointer: while a snapshot is due
 *  or on screen the pointer is drawn after this, as it is while the F5 panel is up, so
 *  it is never frozen into the snapshot and never covered by it.
 *
 *  THE CLOCK. Presentation only, so it runs on the wall clock, but a frame counts for at
 *  most a thirtieth of a second: the first frame of a newly chosen mode can be slow (the
 *  bushes, rocks and grass build their batches on first sight), and a line that jumped
 *  forward by that frame's length would read as a stutter. A slow first frame therefore
 *  holds the line still for a moment instead. A scripted run can stand the clock still
 *  and place it by hand (`gfxwipe`), which is how a gate photographs the middle of it.
 *
 *  THE TITLE'S LETTERS use the original C&C GRAD6FNT from the DOS UI pack, with whole
 *  pixel enlargement and nearest filtering. Green lettering has a crisp dark drop shadow.
 *  No font data is added to the build.
 *
 *  TIER 1: NONE, TIER 2 ONLY. The draw is fixed function (a copied texture, quads, lines
 *  and additive blending), but the thing it switches between is not: Windows 98 has the
 *  Classic picture only, so there is nothing for SPACE to cycle there. Listed in
 *  docs/tier1-gap.md.
 * ==================================================================================== */
#ifndef OPENCNC3D_GMWIPE_MOD_H
#define OPENCNC3D_GMWIPE_MOD_H

#include "../audio/wavio.h"

#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP 0x8191
#endif

static const float GMW_TITLE_IN   = 0.15f;  /* the title's fade in                     */
static const float GMW_TITLE_HOLD = 3.00f;  /* how long it stands before fading        */
static const float GMW_TITLE_FADE = 0.60f;  /* its fade out                            */

/* THE TILE RIP (its own section below): the front's crossing and one tile's own time. */
static const float GMW_RIP_S  = 0.95f;   /* the tear front's crossing, eased in and out    */
static const float GMW_TILE_S = 0.50f;   /* one tile, first lift to gone                   */
static const float GMW_RIP_SHAKE = 0.07f; /* the camera's jolt while it runs, in cells    */
/* The whole wipe: the crossing, and the last tile's own time after it. */
static float gmw_wipe_total(void) { return GMW_RIP_S + GMW_TILE_S; }
static void gmw_rumble_build(short** out, int* outN);

enum { GMW_IDLE = 0, GMW_CAPTURE = 1, GMW_WIPE = 2 };

static int    g_gmwState   = GMW_IDLE;
static int    g_gmwQueued  = 0;       /* a press during a wipe, taken when it ends      */
static int    g_gmwTarget  = 0;       /* the mode being switched to                     */
static int    g_gmwFrom    = 0;       /* and the one it came from                       */
static float  g_gmwT       = 0.0f;    /* seconds since the line set off                 */
static float  g_gmwTitleT  = -1.0f;   /* seconds since the title came up; <0 none       */
static int    g_gmwTitleMode = 0;
static Uint32 g_gmwLastMs  = 0;
static int    g_gmwFrozen  = 0;       /* a script stands the clock still               */
static unsigned g_gmwSeed  = 1;       /* one per wipe, so no two look alike             */
static Uint64 g_gmwSwitchAt = 0;
/* THE HIDDEN WARM-UP (gmw_warm): a mode about to be drawn for one frame under the
   snapshot, and the mode to come back to afterwards. */
static int    g_gmwWarmDue  = 0;      /* capture this frame and switch to g_gmwWarmMode   */
static int    g_gmwWarmMode = 0;
static int    g_gmwWarmBack = 0;
static unsigned g_gmwWarmed = 0;      /* bit per mode drawn at least once this mission    */
static char   g_gmwWarmScen[20] = "";      /* when the switch was made, to time the next frame */

static GLuint g_gmwSnap = 0;
static int    g_gmwSnapW = 0, g_gmwSnapH = 0;

/* ---- the colours: one per mode, the line takes the colour of the mode arriving ------ */
static void gmw_mode_rgb(int mode, float* r, float* g, float* b)
{
    if (mode == FX_MODE_CLASSIC)    { *r = 0.36f; *g = 1.00f; *b = 0.40f; }  /* the dialog's green */
    else if (mode == FX_MODE_N64) { *r = 0.30f; *g = 0.82f; *b = 1.00f; }  /* electric cyan      */
    else                            { *r = 1.00f; *g = 0.74f; *b = 0.26f; }  /* amber              */
}

static const char* gmw_mode_title(int mode)
{
    return mode == FX_MODE_CLASSIC ? "CLASSIC" : mode == FX_MODE_N64 ? "N64"
                                                                       : "ENHANCED";
}

/* A small deterministic hash, so the arcs and sparks are the same for the same wipe and
   the same instant: two scripted shots of one moment are one picture. */
static float gmw_rand(unsigned a, unsigned b)
{
    unsigned h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ g_gmwSeed * 0xC2B2AE3Du;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return (float)(h & 0xFFFFFF) / 16777215.0f;
}

static float gmw_smooth(float e0, float e1, float x)
{
    float t = (x - e0) / (e1 - e0);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

static float gmw_ease(float p)       /* cubic, in and out */
{
    if (p <= 0.0f) return 0.0f;
    if (p >= 1.0f) return 1.0f;
    return p < 0.5f ? 4.0f * p * p * p : 1.0f - powf(-2.0f * p + 2.0f, 3.0f) * 0.5f;
}

/* ================================================================================== *
 *  THE TITLE'S LETTERS
 * ================================================================================== */
enum { GMW_UP = 8, GMW_PAD = 1 };          /* growth factor and transparent atlas gutter */
static GLuint g_gmwFontTex = 0;
static int    g_gmwFontW = 0, g_gmwFontH = 0;
static int    g_gmwFontTried = 0;
static int    g_gmwCapH = 6;
struct GmwGlyph { int x, y, w, h; float xo, yo, adv; int have; };
static GmwGlyph g_gmwGlyph[96];

static GLuint gmw_upload_alpha(const unsigned char* px, int w, int h)
{
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, w, h, 0, GL_ALPHA, GL_UNSIGNED_BYTE, px);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
    return t;
}

/* Only the characters a title can use: capitals, digits, the hyphen and the space. */
static int gmw_title_char(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == ' ';
}

static void gmw_font_build(void)
{
    if (g_gmwFontTried) return;
    g_gmwFontTried = 1;
    const DB_Font* f = g_dbPack ? db_font(g_dbPack, "GRAD6FNT") : NULL;
    if (!f) {
        fprintf(stderr, "GFXWIPE|title unavailable: DOS pack has no GRAD6FNT\n");
        return;
    }
    /* Measure the capitals in the shipped glyph cells, excluding their shadow. */
    int capTop = f->maxh, capBottom = -1;
    for (int c = 'A'; c <= 'Z' && c < f->nchars; c++)
        for (int j = 0; j < f->maxh; j++)
            for (int i = 0; i < f->width[c] && i < f->maxw; i++)
                if ((f->cells[((size_t)c * f->maxh + j) * f->maxw + i] == 1 ||
                    f->cells[((size_t)c * f->maxh + j) * f->maxw + i] >= 4)) {
                    if (j < capTop) capTop = j;
                    if (j > capBottom) capBottom = j;
                }
    if (capBottom < capTop) return;
    g_gmwCapH = capBottom - capTop + 1;
    const int AW = 2048;
    int x = 0, y = 0, shelf = 0;
    memset(g_gmwGlyph, 0, sizeof g_gmwGlyph);
    /* Pack first, to know the height. */
    for (int c = 32; c < 127; c++) {
        if (!gmw_title_char(c)) continue;
        if (c >= f->nchars) continue;
        GmwGlyph* o = &g_gmwGlyph[c - 32];
        o->adv = (f->width[c] + DB_FONT6_XSPACING) * GMW_UP;
        o->xo = -GMW_PAD;
        o->yo = -capTop * GMW_UP - GMW_PAD;
        if (!f->width[c] || c == ' ') continue;
        const int w = f->width[c] * GMW_UP + 2 * GMW_PAD, h = f->maxh * GMW_UP + 2 * GMW_PAD;
        if (x + w > AW) { x = 0; y += shelf; shelf = 0; }
        o->x = x; o->y = y; o->w = w; o->h = h; o->have = 1;
        x += w;
        if (h > shelf) shelf = h;
    }
    int AH = 1;
    while (AH < y + shelf) AH <<= 1;
    std::vector<unsigned char> sharp((size_t)AW * AH, 0);
    /* Preserve the original pixel silhouettes, including the forked C&C lettering. */
    for (int c = 32; c < 127; c++) {
        const GmwGlyph* o = &g_gmwGlyph[c - 32];
        if (!o->have) continue;
        for (int j = 0; j < f->maxh * GMW_UP; j++)
            for (int i = 0; i < f->width[c] * GMW_UP; i++) {
                const size_t cell = ((size_t)c * f->maxh + j / GMW_UP) * f->maxw + i / GMW_UP;
                sharp[(size_t)(o->y + GMW_PAD + j) * AW + o->x + GMW_PAD + i] =
                    (f->cells[cell] == 1 || f->cells[cell] >= 4) ? 255 : 0;
            }
    }
    g_gmwFontW = AW; g_gmwFontH = AH;
    g_gmwFontTex = gmw_upload_alpha(sharp.data(), AW, AH);
    fprintf(stderr, "GFXWIPE|title font %dx%d from C&C GRAD6FNT, cap height %d\n",
            AW, AH, g_gmwCapH);
}

/* Width of a string at `scale` (1 = the grown face), with `track` extra between letters. */
static float gmw_text_w(const char* s, float scale, float track)
{
    float w = 0.0f;
    int n = 0;
    for (const char* p = s; *p; p++) {
        const int c = (unsigned char)*p;
        if (c < 32 || c > 126) continue;
        w += g_gmwGlyph[c - 32].adv * scale;
        n++;
    }
    return w + (n > 1 ? (n - 1) * track : 0.0f);
}

/* One pass of a string with one texture; the caller sets the blend and the colour. */
static void gmw_text_pass(GLuint tex, const char* s, float x, float top, float scale,
                          float track)
{
    const float us = 1.0f / (float)g_gmwFontW, vs = 1.0f / (float)g_gmwFontH;
    const float lineTop = floorf(top + 0.5f);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glBegin(GL_QUADS);
    for (const char* p = s; *p; p++) {
        const int c = (unsigned char)*p;
        if (c < 32 || c > 126) continue;
        const GmwGlyph* g = &g_gmwGlyph[c - 32];
        if (g->have) {
            const float gx = x + g->xo * scale, gy = lineTop + g->yo * scale;
            const float gw = g->w * scale, gh = g->h * scale;
            const float u0 = g->x * us, v0 = g->y * vs;
            const float u1 = (g->x + g->w) * us, v1 = (g->y + g->h) * vs;
            glTexCoord2f(u0, v0); glVertex2f(gx, gy);
            glTexCoord2f(u1, v0); glVertex2f(gx + gw, gy);
            glTexCoord2f(u1, v1); glVertex2f(gx + gw, gy + gh);
            glTexCoord2f(u0, v1); glVertex2f(gx, gy + gh);
        }
        x += g->adv * scale + track;
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

/* A string with a dark drop shadow, centred on cx, capitals starting at `top`, `caps`
   pixels tall. */
static void gmw_text(const char* s, float cx, float top, float caps, float trackFrac,
                     float r, float g, float b, float a)
{
    const float scale = fmaxf(1.0f, floorf(caps / g_gmwCapH + 0.5f)) / GMW_UP;
    const float track = floorf(caps * trackFrac + 0.5f);
    const float x = floorf(cx - gmw_text_w(s, scale, track) * 0.5f + 0.5f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const int rim = (int)fmaxf(1.0f, floorf(caps * 0.06f + 0.5f));
    const int shadow = (int)fmaxf(2.0f, floorf(caps * 0.12f + 0.5f));
    glColor4f(0.0f, 0.0f, 0.0f, a);
    /* A solid surround keeps the ink readable on terrain; fill the longer drop shadow
       continuously so it stays attached to each letter. */
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (dx || dy)
                gmw_text_pass(g_gmwFontTex, s, x + dx * rim, top + dy * rim, scale, track);
    for (int d = 1; d <= shadow; d++)
        gmw_text_pass(g_gmwFontTex, s, x + d, top + d, scale, track);
    glColor4f(r, g, b, a);
    gmw_text_pass(g_gmwFontTex, s, x, top, scale, track);
}

/* ================================================================================== *
 *  THE SOUND
 *
 *  The rumble of the rip (built in THE RUMBLE, below), SYNTHESISED once at boot like the
 *  rain (rain_audio.h): no clip on either disc is this, so there is no asset to license
 *  and the same bytes on every machine. It is played on the effects bus, so the Sound
 *  volume slider is its volume, and panned left to right with the tear front while it
 *  plays (gmw_frame). Nothing about the game's own sound changes.
 * ================================================================================== */
static short* g_gmwRumble = NULL;
static int    g_gmwRumbleN = 0;
static int    g_gmwRumbleVoice = -1;

static float gmw_speed(float p)            /* the ease's slope, 0..1, peak at p = 0.5 */
{
    if (p <= 0.0f || p >= 1.0f) return 0.0f;
    const float d = p < 0.5f ? 12.0f * p * p : 3.0f * (2.0f - 2.0f * p) * (2.0f - 2.0f * p);
    return d / 3.0f;
}

/* Where the sound sits, -1000 left to +1000 right, when the front is `p` of the way
   across: the front's own eased position, kept a little in from the edges. One function
   for play and for the WAV, so the gate hears what the player hears. */
static int gmw_rumble_pan(float p)
{
    return (int)(-700.0f + 1400.0f * gmw_ease(p));
}

static void gmw_rumble_build(void)
{
    if (g_gmwRumble) return;
    gmw_rumble_build(&g_gmwRumble, &g_gmwRumbleN);
}

static void gmw_rumble_play(void)
{
    if (!g_au) return;
    gmw_rumble_build();
    if (!g_gmwRumble) return;
    Mixer* m = cnc_audio_mixer(g_au);
    if (g_gmwRumbleVoice >= 0 && mixer_voice_active(m, g_gmwRumbleVoice))
        mixer_voice_stop(m, g_gmwRumbleVoice);
    /* priority 200: above the battle's ordinary effects, so a busy fight cannot steal
       the one sound that says the picture just changed */
    g_gmwRumbleVoice = mixer_play(m, MIX_BUS_FX, g_gmwRumble, g_gmwRumbleN, 800, gmw_rumble_pan(0.0f), 200);
    printf("GFXWIPE|rumble|%s\n", g_gmwRumbleVoice >= 0 ? "playing" : "REFUSED");
    fflush(stdout);
}

/* THE RUMBLE ON ITS OWN, rendered through a fresh copy of the game's mixer with the same
   left-to-right pan steps gmw_rumble_follow gives it at 60 frames a second, and written
   as a stereo WAV. For a gate, and for a listener who is not at the machine: the headless
   recorder only advances with engine ticks, so a switch between ticks never reaches it. */
static int gmw_rumble_wav(const char* path)
{
    gmw_rumble_build();
    if (!g_gmwRumble) return 0;
    Mixer* m = mixer_create();
    WavOut* w = m ? wav_open(path, MIX_RATE, 2) : NULL;
    if (!w) { if (m) mixer_destroy(m); return 0; }
    const int v = mixer_play(m, MIX_BUS_FX, g_gmwRumble, g_gmwRumbleN, 800, gmw_rumble_pan(0.0f), 200);
    const int frame = MIX_RATE / 60;                 /* one display frame of samples */
    std::vector<short> buf((size_t)frame * 2);
    const long total = (long)g_gmwRumbleN + MIX_RATE / 4;
    for (long at = 0; at < total; at += frame) {
        const float p = (float)at / (float)MIX_RATE / GMW_RIP_S;
        if (v >= 0 && mixer_voice_active(m, v))
            mixer_voice_gain(m, v, 800, gmw_rumble_pan(p));
        mixer_render(m, &buf[0], frame);
        wav_write(w, &buf[0], (long)frame * 2);
    }
    const int pk = wav_peak(w);
    const double rms = wav_rms(w);
    wav_close(w);
    mixer_destroy(m);
    printf("GFXWIPE|rumble wav|%s|peak=%d|rms=%.0f\n", path, pk, rms);
    fflush(stdout);
    return 1;
}

/* Left to right with the front, while it plays. */
static void gmw_rumble_follow(float p)
{
    if (!g_au || g_gmwRumbleVoice < 0) return;
    Mixer* m = cnc_audio_mixer(g_au);
    if (!mixer_voice_active(m, g_gmwRumbleVoice)) { g_gmwRumbleVoice = -1; return; }
    mixer_voice_gain(m, g_gmwRumbleVoice, 800, gmw_rumble_pan(p));
}

/* ================================================================================== *
 *  THE TILE RIP
 *
 *  WHAT A PLAYER SEES. The old picture is not wiped by a line: it is torn up, cell by
 *  cell, from left to right. A ragged front crosses the screen; as it passes a map cell
 *  the cell's piece of the old picture comes up on its left side, turns over towards the
 *  right and flies off that way, as a carpet of tiles would if it were ripped up from
 *  the left, and the new mode is already there beneath it. The ground shakes while it
 *  runs, and a low rumble goes with it. Units, buildings and shroud go up with the ground they stood on, like a
 *  carpet lifted with everything on it. Rows start a little apart so the front tears
 *  rather than marches. The sidebar comes up last, in one piece.
 *
 *  HOW. At the snapshot the four corners of every map cell are projected through the old
 *  mode's own camera (Classic's flat window or the console camera with the heightmap),
 *  which gives each cell a quadrilateral on the screen and the same quadrilateral in the
 *  snapshot. A tile at rest is that quadrilateral drawn flat, which is the old picture
 *  exactly; a lifted one is the same four corners rotated about its hinge in a small 3D
 *  overlay whose z = 0 plane is the screen in pixels. The depth buffer sorts the pieces.
 *  Cells that would stray under the sidebar are held in by a scissor to the map's
 *  rectangle. Fixed function throughout: one copied texture, quads, blending.
 *
 *  THE CLOCK. The front's crossing takes GMW_RIP_S, eased in and out, and a tile sets
 *  off at the moment the front reaches it (the ease inverted, so a frozen
 *  clock placed by hand gives the same picture every run); each tile then takes
 *  GMW_TILE_S from its first lift to gone. Everything is a function of the one clock and
 *  the per-wipe seed: no state is kept between frames.
 * ================================================================================== */

struct GmwTile {
    float x[4], y[4];      /* rest corners on the screen, pixel space, y down             */
    float u[4], v[4];      /* the same corners in the snapshot                            */
    float trig;            /* the screen x at which the front sets it off                 */
    float size;            /* its width on the screen, the reach of its lift              */
    int   hud;             /* 1: a piece of the HUD, outside the map's scissor            */
    unsigned id;           /* its own number, for its own scatter and axis                */
};
static std::vector<GmwTile> g_gmwTiles;
static float g_gmwMapRect[4] = { 0, 0, 0, 0 };   /* x0 y0 x1 y1 of the map at the snapshot */
static int   g_gmwMapRectOn = 0;

/* The cubic ease run backwards: at what fraction of the crossing is the front at `y`. */
static float gmw_ease_inv(float y)
{
    if (y <= 0.0f) return 0.0f;
    if (y >= 1.0f) return 1.0f;
    return y < 0.5f ? cbrtf(y * 0.25f) : 1.0f - 0.5f * cbrtf(2.0f * (1.0f - y));
}

static void gmw_tile_push(const float* x, const float* y, int fbw, int fbh, int hud, float jitter)
{
    GmwTile t;
    float minx = 1e9f, maxx = -1e9f;
    for (int i = 0; i < 4; i++) {
        t.x[i] = x[i]; t.y[i] = y[i];
        t.u[i] = x[i] / (float)fbw;
        t.v[i] = 1.0f - y[i] / (float)fbh;
        if (x[i] < minx) minx = x[i];
        if (x[i] > maxx) maxx = x[i];
    }
    t.size = maxx - minx;
    t.trig = minx + jitter * t.size;
    t.hud  = hud;
    t.id   = (unsigned)g_gmwTiles.size() * 7919u + 17u;
    g_gmwTiles.push_back(t);
}

static void gmw_rect_push(float x0, float y0, float x1, float y1, int fbw, int fbh, int hud,
                          float jitter)
{
    if (x1 - x0 < 0.5f || y1 - y0 < 0.5f) return;
    const float x[4] = { x0, x1, x1, x0 }, y[4] = { y0, y0, y1, y1 };
    gmw_tile_push(x, y, fbw, fbh, hud, jitter);
}

/* A strip of the screen outside the map (the bar above a letterboxed Classic view, say),
   cut into pieces a cell wide so it tears with the ground rather than lifting whole. */
static void gmw_strip_push(float x0, float y0, float x1, float y1, float step, int fbw, int fbh,
                           unsigned salt)
{
    if (y1 - y0 < 0.5f) return;
    if (step < 8.0f) step = 8.0f;
    int k = 0;
    for (float x = x0; x < x1 - 0.5f; x += step, k++)
        gmw_rect_push(x, y0, fminf(x + step, x1), y1, fbw, fbh, 1,
                      (gmw_rand(salt, (unsigned)k) - 0.5f) * 0.9f);
}

/* Called at the snapshot, while the OLD mode's camera and layout are still the ones on
   screen: every cell's corners through that camera. */
static void gmw_rip_capture(int fbw, int fbh)
{
    g_gmwTiles.clear();
    g_gmwMapRectOn = 0;
    const float W = (float)fbw, H = (float)fbh;
    sb_layout(fbw, fbh);
    const bool flat = g_fx.dos_classic;
    float mx0 = 0.0f, my0 = 0.0f, mx1 = W, my1 = H;     /* the map's screen rectangle */
    float cellPx = H / 8.0f;
    float panelX = g_dbW > 0.0f ? sb_panel_x(fbw) : W;
    if (flat) {
        const float scale = g_dos2dRa ? (float)g_ra95WorldScale : (float)g_dbScale;
        const int viewW = g_dos2dRa ? g_ra95ViewW : 240;
        const int viewH = g_dos2dRa ? g_ra95ViewH : 200;
        mx0 = g_dbX0 - (float)viewW * scale; mx1 = g_dbX0;
        my0 = g_dbY0;                        my1 = g_dbY0 + (float)viewH * scale;
        cellPx = 24.0f * scale;
        panelX = g_dbX0;
    } else {
        mx1 = panelX;
    }
    if (mx0 < 0.0f) mx0 = 0.0f;
    if (my0 < 0.0f) my0 = 0.0f;
    if (mx1 > W) mx1 = W;
    if (my1 > H) my1 = H;
    g_gmwMapRect[0] = mx0; g_gmwMapRect[1] = my0; g_gmwMapRect[2] = mx1; g_gmwMapRect[3] = my1;
    g_gmwMapRectOn = 1;

    static const int OX[4] = { 0, 1, 1, 0 }, OZ[4] = { 0, 0, 1, 1 };
    int cells = 0;
    for (int cz = 0; cz < g_gridH; cz++) {
        for (int cx = 0; cx < g_gridW; cx++) {
            float x[4], y[4];
            bool ok = true;
            for (int i = 0; i < 4 && ok; i++) {
                const float wx = (float)(cx + OX[i]), wz = (float)(cz + OZ[i]);
                const float wy = flat ? 0.0f : terrain_corner_y(cx + OX[i], cz + OZ[i]);
                ok = project_point(wx, wy, wz, fbw, fbh, &x[i], &y[i]);
            }
            if (!ok) continue;                        /* a corner behind the camera */
            float minx = x[0], maxx = x[0], miny = y[0], maxy = y[0];
            for (int i = 1; i < 4; i++) {
                if (x[i] < minx) minx = x[i]; if (x[i] > maxx) maxx = x[i];
                if (y[i] < miny) miny = y[i]; if (y[i] > maxy) maxy = y[i];
            }
            if (maxx <= mx0 || minx >= mx1 || maxy <= my0 || miny >= my1) continue;
            if (maxx - minx > W || maxy - miny > H) continue;   /* a cell on the eye's own plane */
            if (maxy - miny < 1.5f && maxx - minx < 1.5f) continue;   /* a sliver at the horizon */
            if (flat) {
                /* Classic's cells are rectangles: clip the ones at the window's edge, so the
                   piece is the part that was on screen and nothing of the sidebar. */
                for (int i = 0; i < 4; i++) {
                    x[i] = x[i] < mx0 ? mx0 : x[i] > mx1 ? mx1 : x[i];
                    y[i] = y[i] < my0 ? my0 : y[i] > my1 ? my1 : y[i];
                }
            }
            gmw_tile_push(x, y, fbw, fbh, 0, (gmw_rand((unsigned)cx, (unsigned)cz) - 0.5f) * 1.6f);
            cells++;
        }
    }
    /* What is left of the screen: strips round the map, and the sidebar in one piece. */
    gmw_strip_push(0.0f, 0.0f, panelX, my0, cellPx, fbw, fbh, 901u);
    gmw_strip_push(0.0f, my1, panelX, H, cellPx, fbw, fbh, 902u);
    gmw_strip_push(0.0f, my0, mx0, my1, cellPx, fbw, fbh, 903u);
    gmw_rect_push(panelX, 0.0f, W, H, fbw, fbh, 1, 0.0f);
    fprintf(stderr, "GFXWIPE|rip: %d cells, %d pieces, map %.0f,%.0f-%.0f,%.0f, panel at %.0f\n",
            cells, (int)g_gmwTiles.size(), mx0, my0, mx1, my1, panelX);
}

/* One tile at `q` of its own time (0 at rest, 1 gone): where its corners are in the
   overlay's space, and which way it faces. */
static void gmw_tile_place(const GmwTile* t, float q, float W, float H, float D,
                           float* px, float* py, float* pz, int* front, float* shade)
{
    /* THE HINGE is through the middle of the tile's RIGHTMOST edge on the screen: the
       carpet is ripped up from the left, so each tile's left side is what comes up, and
       it turns over towards the right. Each tile's axis is tilted a little from that
       edge, its own way, so a column of tiles does not come up as one slat. */
    int ha = 1, hb = 2;
    {
        float best = -1e9f;
        for (int e = 0; e < 4; e++) {
            const int a = e, b = (e + 1) & 3;
            const float mid = 0.5f * (t->x[a] + t->x[b]);
            if (mid > best) { best = mid; ha = a; hb = b; }
        }
    }
    const float ax = 0.5f * (t->x[ha] + t->x[hb]), ay = 0.5f * (t->y[ha] + t->y[hb]);
    float dx = t->x[hb] - t->x[ha], dy = t->y[hb] - t->y[ha];
    {
        const float l = sqrtf(dx * dx + dy * dy);
        if (l > 1e-4f) { dx /= l; dy /= l; } else { dx = 0.0f; dy = 1.0f; }
        const float tilt = (gmw_rand(t->id, 3u) - 0.5f) * 0.42f;    /* about +-12 degrees */
        const float ct = cosf(tilt), st = sinf(tilt);
        const float rx = dx * ct - dy * st, ry = dx * st + dy * ct;
        dx = rx; dy = ry;
    }
    /* e: the in-plane direction from the hinge towards the tile's lifting side. */
    float ex = -dy, ey = dx;
    {
        float side = 0.0f;
        for (int i = 0; i < 4; i++) side += (t->x[i] - ax) * ex + (t->y[i] - ay) * ey;
        if (side < 0.0f) { ex = -ex; ey = -ey; }
    }
    /* THE MOTION. The turn: the left side comes up towards the eye and over to the
       right, past upright, in the first six tenths, as a carpet's edge does when it is
       pulled up and thrown over. The flight: from a third in the tile lets go, keeps
       turning, and flies to the right and a little up before it drops, fading in its
       last four tenths. Its own number gives it its own spin, reach and height. */
    const float turn  = gmw_smooth(0.0f, 0.60f, q);
    const float fly   = gmw_smooth(0.33f, 1.0f, q);
    const float theta = 2.6f * turn + (0.8f + 0.8f * gmw_rand(t->id, 5u)) * fly;
    const float hx = ax + (3.0f + 2.5f * gmw_rand(t->id, 6u)) * t->size * powf(fly, 1.5f);
    const float hy = ay - (0.8f + 0.8f * gmw_rand(t->id, 7u)) * t->size * fly
                   + 1.6f * t->size * fly * fly;
    const float hz = 0.45f * t->size * turn + 0.5f * t->size * fly;
    const float c = cosf(theta), sn = sinf(theta);
    float cx = 0.0f, cy = 0.0f, cz = 0.0f;
    for (int i = 0; i < 4; i++) {
        const float vx = t->x[i] - ax, vy = t->y[i] - ay;
        const float along = vx * dx + vy * dy;
        const float out   = vx * ex + vy * ey;
        px[i] = hx + along * dx + out * c * ex;
        py[i] = hy + along * dy + out * c * ey;
        pz[i] = hz + out * sn;
        cx += px[i] * 0.25f; cy += py[i] * 0.25f; cz += pz[i] * 0.25f;
    }
    /* The face the eye sees: the tile's normal against the line to the eye. */
    const float nx = -sn * ex, ny = -sn * ey, nz = c;
    const float vx = W * 0.5f - cx, vy = H * 0.5f - cy, vz = D - cz;
    *front = (nx * vx + ny * vy + nz * vz) > 0.0f ? 1 : 0;
    /* Lit the same on either face: full as it lies, a little darker as it tilts, so the
       peel has a shadow side; the underside shows the same picture as the top. */
    *shade = 0.65f + 0.35f * fabsf(c);
}

static void gmw_draw_rip(int fbw, int fbh)
{
    const float W = (float)fbw, H = (float)fbh;
    const float t = g_gmwT;
    const float p = t / GMW_RIP_S;
    const float u = H / 800.0f;
    const float front = W * gmw_ease(p);
    float r, g, b;
    gmw_mode_rgb(g_gmwTarget, &r, &g, &b);

    /* THE OVERLAY'S SPACE: a perspective whose z = 0 plane is the screen in pixels, y
       down, the eye at (W/2, H/2, D) looking straight at it. A tile at rest is drawn
       exactly where the snapshot had it; a lifted one comes towards the eye. */
    const double D = 1.5 * (double)fbh, n = D * 0.05, f = D * 8.0;
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glFrustum(-0.5 * W * n / D, 0.5 * W * n / D, 0.5 * H * n / D, -0.5 * H * n / D, n, f);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glTranslated(-0.5 * W, -0.5 * H, -D);
    {   /* The pieces jolt with the ground beneath them: the camera shake the rip holds
           (gmw_frame), turned into pixels at the look-at depth, so the old picture and
           the new one shake as one surface. */
        float sx, sz;
        shake_offset(&sx, &sz);
        const float ppc = g_fx.dos_classic ? 24.0f * (float)g_dbScale : W / 24.6f;
        glTranslatef(-sx * ppc, -sz * ppc * 0.7f, 0.0f);
    }

    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_gmwSnap);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const int N = (int)g_gmwTiles.size();
    for (int pass = 0; pass < 2; pass++) {
        /* Pass 0: the map's pieces, held inside the map's rectangle so a lifting cell
           never shows from under the sidebar. Pass 1: the HUD's pieces, unclipped. */
        if (pass == 0 && g_gmwMapRectOn) {
            const float x0 = g_gmwMapRect[0], y0 = g_gmwMapRect[1];
            const float x1 = g_gmwMapRect[2], y1 = g_gmwMapRect[3];
            glScissor(g_vpOffX + (int)x0, g_vpOffY + (int)(H - y1), (int)(x1 - x0), (int)(y1 - y0));
            glEnable(GL_SCISSOR_TEST);
        } else {
            cnc_screen_viewport(fbw, fbh);   /* its own scissor, or none */
        }
        /* Every piece in one textured batch: both faces carry the picture. */
        glEnable(GL_TEXTURE_2D);
        glBegin(GL_QUADS);
        for (int i = 0; i < N; i++) {
            const GmwTile* tl = &g_gmwTiles[i];
            if (tl->hud != pass) continue;
            const float t0 = GMW_RIP_S * gmw_ease_inv(tl->trig / W);
            const float q = (t - t0) / GMW_TILE_S;
            if (q >= 1.0f) continue;
            if (q <= 0.0f) {
                glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
                for (int k = 0; k < 4; k++) {
                    glTexCoord2f(tl->u[k], tl->v[k]);
                    glVertex3f(tl->x[k], tl->y[k], 0.0f);
                }
                continue;
            }
            float px[4], py[4], pz[4], shade;
            int fr;
            gmw_tile_place(tl, q, W, H, (float)D, px, py, pz, &fr, &shade);
            const float a = 1.0f - gmw_smooth(0.60f, 1.0f, q);
            (void)fr;
            /* A touch of the arriving mode's colour on the lit face as it swings, the way
               the line's wash coloured what it uncovered. */
            const float warm = 0.18f * gmw_smooth(0.0f, 0.5f, q) * (1.0f - gmw_smooth(0.5f, 1.0f, q));
            glColor4f(shade + warm * r, shade + warm * g, shade + warm * b, a);
            for (int k = 0; k < 4; k++) {
                glTexCoord2f(tl->u[k], tl->v[k]);
                glVertex3f(px[k], py[k], pz[k]);
            }
        }
        glEnd();
    }
    cnc_screen_viewport(fbw, fbh);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();

    /* THE FRONT ITSELF, in the flat overlay: the old picture darkens where it is about to
       be torn, as a carpet does where it is pulled, and the new picture behind the front
       carries a faint wash of the arriving mode's colour (gmw_mode_rgb). */
    if (front > -200.0f * u && front < W + 200.0f * u) {
        glShadeModel(GL_SMOOTH);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBegin(GL_QUADS);
        glColor4f(0.0f, 0.0f, 0.0f, 0.30f); glVertex2f(front, 0); glVertex2f(front, H);
        glColor4f(0.0f, 0.0f, 0.0f, 0.0f);  glVertex2f(front + 90.0f * u, H); glVertex2f(front + 90.0f * u, 0);
        glEnd();
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glBegin(GL_QUADS);
        glColor4f(r, g, b, 0.0f);   glVertex2f(front - 220.0f * u, 0); glVertex2f(front - 220.0f * u, H);
        glColor4f(r, g, b, 0.10f);  glVertex2f(front, H); glVertex2f(front, 0);
        glEnd();
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
}

/* ================================================================================== *
 *  THE RUMBLE
 *
 *  The rip's sound, SYNTHESISED once at boot like the rain (rain_audio.h), to
 *  the front's own motion: a low rumble, the ground coming up.
 *
 *    THE BODY    noise through a resonant low pass near 65 Hz: the weight. (A random
 *                walk was tried first and put nearly all the energy below 40 Hz, where
 *                most speakers have nothing to say.)
 *    THE ROLL    noise through a second resonance near 95 Hz: the movement in it.
 *    THE GRIND   a band in the low mids, for the pieces against each other.
 *
 *  Measured on the rendered clip: about a third of the energy below 40 Hz, two fifths
 *  between 40 and 80, a fifth between 80 and 160, centroid near 110 Hz.
 *
 *  The loudness follows the front's speed, rising quickly and letting go slowly, and it
 *  runs on past the crossing for the last pieces' flight before it fades.
 * ================================================================================== */
static void gmw_rumble_build(short** out, int* outN)
{
    const float total = GMW_RIP_S + GMW_TILE_S + 0.45f;
    const int N = (int)(MIX_RATE * total);
    float* b = (float*)malloc(sizeof(float) * (size_t)N);
    short* pcm = (short*)malloc(sizeof(short) * (size_t)N);
    if (!b || !pcm) { free(b); free(pcm); return; }
    unsigned lcg = 0x13579BDFu;               /* a fixed generator: the same bytes every run */
    const float fs = (float)MIX_RATE;
    const float f1 = 2.0f * sinf(3.14159265f * 65.0f / fs);
    const float f2 = 2.0f * sinf(3.14159265f * 95.0f / fs);
    const float fg = 2.0f * sinf(3.14159265f * 230.0f / fs);
    float l1 = 0.0f, b1 = 0.0f, l2 = 0.0f, b2 = 0.0f, lg = 0.0f, bg = 0.0f;
    float env = 0.0f, peak = 0.0f;
    for (int i = 0; i < N; i++) {
        const float t = (float)i / fs;
        const float p = t / GMW_RIP_S;
        const float sp = gmw_speed(p);                        /* the front's speed, 0..1 */
        lcg = lcg * 1664525u + 1013904223u;
        const float x = (float)(int)(lcg >> 8) * (1.0f / 8388608.0f) - 1.0f;
        /* the loudness: with the front, quick to rise, slow to let go; after the crossing
           it holds for the last pieces, then goes */
        const float target = p <= 1.0f ? 0.30f + 0.70f * powf(sp, 0.6f)
                           : (t < GMW_RIP_S + GMW_TILE_S * 0.6f ? 0.30f : 0.0f);
        env += (target - env) * (target > env ? 0.0009f : 0.00010f) * (44100.0f / fs);
        l1 += f1 * b1; { const float h = x - l1 - 0.50f * b1; b1 += f1 * h; }
        l2 += f2 * b2; { const float h = x - l2 - 0.45f * b2; b2 += f2 * h; }
        lg += fg * bg; { const float h = x - lg - 0.70f * bg; bg += fg * h; }
        const float v = env * (l1 * 1.00f + l2 * 0.60f + bg * 0.30f);
        b[i] = v;
        if (fabsf(v) > peak) peak = fabsf(v);
    }
    const float sc = peak > 1e-6f ? 21000.0f / peak : 0.0f;
    const int ramp = (int)(fs * 0.010f);
    for (int i = 0; i < N; i++) {
        float v = b[i] * sc;
        if (i < ramp) v *= (float)i / ramp;
        if (i > N - 1 - ramp) v *= (float)(N - 1 - i) / ramp;
        pcm[i] = (short)(v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : (int)v);
    }
    free(b);
    *out = pcm;
    *outN = N;
    fprintf(stderr, "GFXWIPE|rumble built: %d samples at %d Hz\n", N, MIX_RATE);
}

/* ================================================================================== *
 *  THE SWITCH, AND THE FRAME HOOK
 * ================================================================================== */

/* The mode in use becomes `mode`, through the Visuals screen's own apply, so every mode
   keeps its own settings and the choice is written to the preset like a dialog click. */
static void gmw_switch_quiet(int mode)
{
    DOPT_Visuals v;
    memset(&v, 0, sizeof v);
    vis_from_fx(&v);
    v.mode = mode;
    opt_apply_visuals(NULL, &v);
    dopt_set_visuals(&g_optState, &v);
}

static void gmw_switch(int mode)
{
    gmw_switch_quiet(mode);
    printf("GFXMODE|%s|space\n", FX_MODE_NAME[mode]);
    fflush(stdout);
}

/* SPACE. A press while the line is crossing is kept, one deep, and taken when it lands,
   so a quick double press goes two modes on rather than being lost. */
static void gmw_request(void)
{
    if (g_gmwState != GMW_IDLE || g_gmwWarming) { g_gmwQueued = 1; return; }
    g_gmwFrom = fx_mode_of(&g_fx);
    g_gmwTarget = (g_gmwFrom + 1) % FX_MODE_COUNT;
    /* WHERE N64 IS NOT OFFERED (Red Alert, which has only Classic and Enhanced: the
       cartridge never had its art), SPACE steps over it, as the Visuals screen does */
    if (g_gmwTarget == FX_MODE_N64 && g_optState.n64_unavailable)
        g_gmwTarget = (g_gmwTarget + 1) % FX_MODE_COUNT;
    g_gmwState = GMW_CAPTURE;
}

/* While a snapshot is due or on screen the pointer is drawn after this hook. */
static bool gmw_holds_pointer(void)
{
    return g_gmwState != GMW_IDLE || g_gmwWarming || g_gmwWarmDue;
}

static void gmw_capture(int fbw, int fbh)
{
    if (!g_gmwSnap) glGenTextures(1, &g_gmwSnap);
    glBindTexture(GL_TEXTURE_2D, g_gmwSnap);
    if (g_gmwSnapW != fbw || g_gmwSnapH != fbh) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, fbw, fbh, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        g_gmwSnapW = fbw; g_gmwSnapH = fbh;
    }
    glReadBuffer(GL_BACK);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, fbw, fbh);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/* ================================================================================== *
 *  AHEAD OF THE PRESS
 *
 *  Every mode builds things the first time it is drawn, and a SPACE into a mode that has
 *  never been drawn this mission used to pay for all of them in one frame. Measured on
 *  SCG01EA with the Remastered Collection installed, before any of this: 4.1 s for the
 *  first Enhanced frame, 1.6 s for the first Enhanced one after starting in Classic,
 *  0.55 to 0.68 s for every return to either afterwards.
 *
 *  The returns are gone at their source (the water keeps what it built per atlas). The
 *  first visits are paid here instead, while another mode is on screen, one task a frame
 *  in the first frames of the mission, where the map has only just appeared and a short
 *  pause reads as part of loading rather than as a stutter in play. The Remastered sheet,
 *  the one piece measured in seconds, is built on a thread (terrain_remaster_prebuild_tick
 *  in cnc_eyes.cpp) and reaches the GPU a band at a time.
 *
 *  Each task runs only when the mode on screen does not already need it (that mode
 *  builds it itself, on its own first frame, as it always has), and only in a player's
 *  run: an automated one keeps the old order unless a script asks (`gfxprebuild`).
 * ================================================================================== */
static void gmw_prewarm(void)
{
    static char scen[20] = "";
    static int done = 0;
    if (g_rtpAllow != 1) return;
    if (strcmp(scen, g_pack.scen)) {
        snprintf(scen, sizeof scen, "%s", g_pack.scen);
        done = 0;
        g_gmwWarmed = 0;
    }
    const int mode = fx_mode_of(&g_fx);
    const int f = g_rtpFrames;
    const bool chainWanted = true;
    const bool waterWanted = g_fx.mode_row[FX_MODE_ENHANCED][FXR_WATER] >= 0.5f;
    Uint64 t0;
    const char* what = NULL;

    t0 = SDL_GetPerformanceCounter();
    if (f >= 2 && !(done & 1)) {
        done |= 1;
        /* THE CHAIN'S SHADERS, the cloud mask and the framebuffers: only Classic never
           boots them. */
        if (mode != FX_MODE_ENHANCED && chainWanted) { fx_boot(); what = "post chain"; }
    } else if (f >= 3 && !(done & 2)) {
        done |= 2;
        /* THE WATER FIELD, read from the cartridge's own tiles, so it serves every mode;
           only Classic never builds it. */
        if (mode != FX_MODE_ENHANCED && waterWanted) { water_field_sync(); what = "water field"; }
    } else if (f >= 4 && !(done & 4)) {
        done |= 4;
        /* THE GRASS: its mask and blades come from the cartridge's tiles too; only the
           colour step is redone for Enhanced's own ground when it arrives. */
        if (mode != FX_MODE_ENHANCED && g_fx.mode_row[FX_MODE_ENHANCED][FXR_GRASS] >= 0.5f
            && !strcmp(g_pack.theater, "TEMPERAT")) {
            grass_bake_sync(grass_bake_cfg_default());
            what = "grass";
        }
    } else if (f >= 5 && !(done & 8)) {
        done |= 8;
        /* ENHANCED'S SOFT COAST, the sheet its sea draws the shore through, for the atlas
           Enhanced's own settings pick. */
        if (mode != FX_MODE_ENHANCED && g_fx.mode_row[FX_MODE_ENHANCED][FXR_WATER] >= 0.5f) {
            const int want = (int)(g_fx.mode_row[FX_MODE_ENHANCED][FXR_TEXSET] + 0.5f);
            const int ai = (want == FX_TEX_ENH && g_pack.terrainTexEnh >= 0) ? g_pack.terrainTexEnh
                         : (want != FX_TEX_N64 && want != FX_TEX_REMASTER && g_pack.terrainTexDos >= 0)
                               ? g_pack.terrainTexDos
                         : (want == FX_TEX_REMASTER && g_pack.terrainTexRemaster >= 0)
                               ? g_pack.terrainTexRemaster
                         : g_pack.terrainTex;
            water_soft_prepare(ai, g_fx.mode_row[FX_MODE_ENHANCED][FXR_BILINEAR] >= 0.5f ? 1 : 0);
            what = "Enhanced's soft coast";
        }
    }
    /* THEN THE REST, whatever it is: each mode not yet drawn this mission is drawn once,
       for one frame, under a snapshot of the frame before (gmw_frame), which is the only
       way to be sure every shader, target and upload it will ask for exists before a
       player asks for it. Enhanced waits for its ground to be ready, or the frame
       would build the ground itself, synchronously, for seconds. */
    if (!what && f >= 8 && g_gmwState == GMW_IDLE && !g_gmwWarmDue && !g_gmwWarming) {
        g_gmwWarmed |= 1u << mode;
        for (int m = 0; m < FX_MODE_COUNT; m++) {
            if (g_gmwWarmed & (1u << m)) continue;
            if (m == FX_MODE_N64 && g_optState.n64_unavailable) continue;   /* never shown */
            g_gmwWarmDue = 1;
            g_gmwWarmMode = m;
            break;
        }
    }
    if (what)
        fprintf(stderr, "GFXWIPE|%s ready ahead of time in %.1f ms\n", what,
                (double)(SDL_GetPerformanceCounter() - t0) * 1000.0
                    / (double)SDL_GetPerformanceFrequency());
}

/* Called once a frame, after fx_present. */
static void gmw_frame(int fbw, int fbh)
{
    gmw_font_build();   /* once, on the first frame, so the first press has no build in it */
    if (g_au) gmw_rumble_build();
    /* And everything the other two modes build the first time they are drawn. */
    terrain_remaster_prebuild_tick();
    gmw_prewarm();

    {
        const Uint32 now = SDL_GetTicks();
        float dt = g_gmwLastMs ? (float)(now - g_gmwLastMs) / 1000.0f : 0.0f;
        g_gmwLastMs = now;
        if (dt > 1.0f / 30.0f) dt = 1.0f / 30.0f;
        if (g_gmwFrozen) dt = 0.0f;
        if (g_gmwState == GMW_WIPE) g_gmwT += dt;
        if (g_gmwTitleT >= 0.0f) g_gmwTitleT += dt;
    }

    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    cnc_screen_viewport(fbw, fbh);

    /* THE HIDDEN WARM-UP. First frame: this finished picture is kept and the other mode
       switched in, quietly. Second frame: that mode has just drawn everything it needs,
       under a snapshot that covers the whole screen, and the player's own mode comes
       straight back. Nothing is written to the preset on the way (g_gmwWarming). */
    if (g_gmwWarming) {
        const Uint64 a = SDL_GetPerformanceCounter();
        begin_overlay(fbw, fbh);
        glDisable(GL_BLEND);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, g_gmwSnap);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, 0.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex2f((float)fbw, 0.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex2f((float)fbw, (float)fbh);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, (float)fbh);
        glEnd();
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
        end_overlay();
        gmw_switch_quiet(g_gmwWarmBack);
        g_gmwWarming = 0;
        fprintf(stderr, "GFXWIPE|%s drawn once, hidden, to warm it up (%.1f ms since the switch)\n",
                FX_MODE_NAME[g_gmwWarmMode],
                (double)(SDL_GetPerformanceCounter() - g_gmwSwitchAt) * 1000.0
                    / (double)SDL_GetPerformanceFrequency());
        g_gmwSwitchAt = 0;
        (void)a;
    } else if (g_gmwWarmDue && g_gmwState == GMW_IDLE) {
        g_gmwWarmDue = 0;
        g_gmwWarmBack = fx_mode_of(&g_fx);
        if (g_gmwWarmMode != g_gmwWarmBack) {
            gmw_capture(fbw, fbh);
            g_gmwWarming = 1;
            g_gmwWarmed |= 1u << g_gmwWarmMode;
            gmw_switch_quiet(g_gmwWarmMode);
            g_gmwSwitchAt = SDL_GetPerformanceCounter();
        }
    }

    if (g_gmwState == GMW_CAPTURE) {
        /* This frame is the old mode's last, already on the glass: keep it, switch, and
           the line sets off on the next one. */
        gmw_capture(fbw, fbh);
        gmw_rip_capture(fbw, fbh);
        {
            const Uint64 a = SDL_GetPerformanceCounter();
            gmw_switch(g_gmwTarget);
            g_gmwSwitchAt = SDL_GetPerformanceCounter();
            fprintf(stderr, "GFXWIPE|switch took %.1f ms\n",
                    (double)(g_gmwSwitchAt - a) * 1000.0 / (double)SDL_GetPerformanceFrequency());
        }
        g_gmwState = GMW_WIPE;
        g_gmwT = 0.0f;
        g_gmwTitleT = 0.0f;
        gmw_rumble_play();
        printf("GFXWIPE|rip|pieces=%d|shake=%.3f|shake_on=%d\n", (int)g_gmwTiles.size(),
               GMW_RIP_SHAKE, g_fx.shake_on ? 1 : 0);
        g_gmwTitleMode = g_gmwTarget;
        g_gmwSeed = g_gmwSeed * 1664525u + 1013904223u;
    } else if (g_gmwState == GMW_WIPE && g_gmwT >= gmw_wipe_total() && !g_gmwFrozen) {
        g_gmwState = GMW_IDLE;
        if (g_gmwQueued) { g_gmwQueued = 0; gmw_request(); }
    }

    /* THE FIRST FRAME OF THE NEW MODE, timed from the switch to here: the cost a player
       would feel as a hitch, and the reason the clock is capped. */
    if (g_gmwSwitchAt && g_gmwState == GMW_WIPE && g_gmwT > 0.0f) {
        fprintf(stderr, "GFXWIPE|first frame of %s took %.1f ms\n", FX_MODE_NAME[g_gmwTarget],
                (double)(SDL_GetPerformanceCounter() - g_gmwSwitchAt) * 1000.0
                    / (double)SDL_GetPerformanceFrequency());
        g_gmwSwitchAt = 0;
    }
    const bool wiping = (g_gmwState == GMW_WIPE);
    if (wiping) gmw_rumble_follow(g_gmwT / GMW_RIP_S);
    /* THE SHAKE: held at one strength for as long as the rip runs (a note of equal
       strength restarts the jolt every frame, so its decay never gets going), then it
       dies away by the game's own decay once the last piece is gone. Engine-tick driven
       like every other shake, so a scripted shot is the same picture every run. */
    if (wiping) shake_note(GMW_RIP_SHAKE);
    const bool title = g_gmwTitleT >= 0.0f && g_gmwTitleT < GMW_TITLE_HOLD + GMW_TITLE_FADE;
    if (!wiping && !title) {
        if (g_gmwTitleT >= GMW_TITLE_HOLD + GMW_TITLE_FADE) g_gmwTitleT = -1.0f;
        glViewport(vp[0], vp[1], vp[2], vp[3]);
        return;
    }

    begin_overlay(fbw, fbh);
    glDisable(GL_CULL_FACE);
    if (wiping) gmw_draw_rip(fbw, fbh);
    if (title && g_gmwFontTex) {
        const float tt = g_gmwTitleT;
        const float a = gmw_smooth(0.0f, GMW_TITLE_IN, tt)
                      * (1.0f - gmw_smooth(GMW_TITLE_HOLD, GMW_TITLE_HOLD + GMW_TITLE_FADE, tt));
        /* It settles into place: a touch large and a touch low as it arrives. */
        const float settle = 1.0f - gmw_ease(gmw_smooth(0.0f, 0.35f, tt));
        const float caps = (float)fbh * 0.036f * (1.0f + 0.06f * settle);
        const float top = (float)fbh * 0.085f + caps * 0.25f * settle;
        const float cx = (float)fbw * 0.5f;
        const unsigned char* green = g_dbPack->pal8 + DB_GREEN * 3;
        const float r = green[0] / 255.0f, g = green[1] / 255.0f, b = green[2] / 255.0f;
        gmw_text("GRAPHICS MODE", cx, top - caps * 0.70f, caps * 0.40f, 0.45f,
                 r, g, b, a * 0.75f);
        gmw_text(gmw_mode_title(g_gmwTitleMode), cx, top, caps, 0.12f, r, g, b, a);
        /* A rule under it that draws itself out from the centre. */
        {
            const float scale = fmaxf(1.0f, floorf(caps / g_gmwCapH + 0.5f)) / GMW_UP;
            const float full = gmw_text_w(gmw_mode_title(g_gmwTitleMode), scale, floorf(caps * 0.12f + 0.5f));
            const float half = 0.5f * full * gmw_ease(gmw_smooth(0.05f, 0.45f, tt)) + caps * 0.4f;
            const float ry = top + caps * 1.28f, th = (float)fbh / 400.0f + 0.5f;
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glBegin(GL_QUADS);
            glColor4f(r, g, b, 0.0f);    glVertex2f(cx - half, ry);      glVertex2f(cx - half, ry + th);
            glColor4f(r, g, b, 0.9f * a); glVertex2f(cx, ry + th);        glVertex2f(cx, ry);
            glColor4f(r, g, b, 0.9f * a); glVertex2f(cx, ry);             glVertex2f(cx, ry + th);
            glColor4f(r, g, b, 0.0f);    glVertex2f(cx + half, ry + th); glVertex2f(cx + half, ry);
            glEnd();
        }
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    end_overlay();
    glViewport(vp[0], vp[1], vp[2], vp[3]);
}

#endif /* OPENCNC3D_GMWIPE_MOD_H */
