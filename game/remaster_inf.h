/* ==================================================================================== *
 *  remaster_inf.h -- infantry sprites from the player's own Command & Conquer
 *  Remastered Collection.
 *
 *  WE SHIP NONE OF THIS ART. It is Electronic Arts' work in a product they sell. The
 *  archives are opened in the player's installation, at runtime, on the frame they choose
 *  "Remastered" infantry; the strips that come out live in memory and nowhere else.
 *  remaster.h finds the install; remaster_tex.h's RtMeg reads the container.
 *
 *  THIS IS A SECOND BAKER FOR dosinf_mod.h's OWN FORMAT, not a new sprite system, and
 *  that is the decision the whole file rests on. It emits DosStrip records with identical
 *  semantics -- a uniform grid cell, the cell symmetric about Draw_It's anchor, the
 *  bottom row on the ground -- and installs them in the same table the DOS art uses. So
 *  dosinf_draw_sprite, infantry_quad_size, the shadow emitter, picking, health bars and
 *  the codex page all keep working untouched, and the only thing that changes is which
 *  pixels are in the strip.
 *
 *  WHAT THE ART ACTUALLY IS, measured off a real install and not from documentation:
 *
 *    DATA\ART\TEXTURES\SRGB\TIBERIAN_DAWN\UNITS\<NAME>.ZIP   one per type, stored
 *      uncompressed inside the MEG, holding <name>-NNNN.tga and <name>-NNNN.meta.
 *    the .meta   JSON: {"size":[267,208],"crop":[101,44,166,114]} -- the logical frame
 *      and the crop box of the non-empty region. The TGA is exactly the crop.
 *    the .tga    uncompressed 32-bit BGRA, bottom-left origin (descriptor 0x00), soft
 *      alpha. One signature across every infantry frame.
 *    the index   DATA\XML\TILESETS\TD_UNITS.XML maps (Name, Shape) -> one frame, and
 *      Shape IS the SHP frame index. Counts match our own art exactly: E1 532, E2/E4/E5
 *      660, E3 548, E6 248, RMBO 468, C1..C10 and DELPHI 375, MOEBIUS and CHAN 258.
 *
 *  THREE THINGS THAT LOOK EASY AND ARE NOT:
 *
 *  1. OUR FRAME NUMBER IS STRIP-LOCAL. dosinf_draw_sprite computes
 *     facenum * stages + stage against a strip, while the Remaster is indexed by the
 *     SHP frame. The (Frame, Count, Jump) triples that bridge them live only in the
 *     baker, so they are GENERATED into dosinf_dotable.h from the brain's own idata.cpp
 *     rather than transcribed. shp = Frame + facenum*Jump + stage.
 *
 *  2. THE CROP BOTTOM IS THE SHADOW'S BOTTOM, NOT THE FEET. Anchoring a frame on its
 *     crop box floats every man off the ground by the length of his drop shadow. The
 *     anchor is computed from the LOGICAL frame, which is constant per type.
 *
 *  3. ZIP MEMBERS ARE RAW DEFLATE. The tree's existing zlib calls (edit_mod.h's
 *     uncompress, cnc_eyes.cpp's compress2) are the RFC1950 wrapped form and fail on
 *     every one of these. inflateInit2 with a negative window bit count is the one that
 *     works; copying the existing call sites will not.
 *
 *  HOUSE COLOUR IS OURS, NOT EA'S, and this is deliberate. The Remaster ships one green
 *  sprite per type and recolours it from CNCTDTEAMCOLORS.XML, which knows GOOD, BAD_UNIT,
 *  BAD_STRUCTURE, NEUTRAL and MULTI1..6. That table cannot express what this engine
 *  already does: eight PlayerColorType seats, of which GOLD/LTBLUE are baked rows and the
 *  other six are built at runtime by dosinf_livery_build recognising KEY_BAND and
 *  rewriting it to LIVERY_BAND. EA's set collapses several of those onto one shift and
 *  has nothing for GREY or BROWN, which are default skirmish seats; and its GDI gold sits
 *  closer to our ORANGE than to our GOLD, which would put a rifleman a different colour
 *  from his own radar blip and his own tank. So the green uniform is recognised here and
 *  rewritten to the SAME LIVERY_BAND the DOS art uses, and all three art sets agree.
 * ==================================================================================== */
#ifndef CNC3D_REMASTER_INF_H
#define CNC3D_REMASTER_INF_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "remaster.h"
#include "remaster_tex.h"       /* RtMeg: the MEG container reader */

/* How big a remastered man draws against the DOS one whose footprint he inherits. 1.0 is
   the exact arithmetic match; 0.8 is what looks right beside the 1995 sprites. */
#define RI_SIZE 0.8f

#if defined(__GNUC__) || defined(__clang__)
#define RI_MAYBE_UNUSED __attribute__((unused))
#else
#define RI_MAYBE_UNUSED
#endif

/* ---- ZIP ---------------------------------------------------------------------------
 *
 * Enough of the format to walk one archive held wholly in memory: find the end-of-central
 * -directory record, walk the central directory, and inflate a member by name. No ZIP64
 * (none of these archives needs it) and no encryption. Anything unexpected is refused
 * rather than guessed at.
 */

typedef struct RiZipEnt {
    char         name[64];
    unsigned int comp, uncomp, lho, method, crc;
} RiZipEnt;

typedef struct RiZip {
    const unsigned char* b;
    unsigned int         n;
    RiZipEnt*            e;
    int                  ne;
} RiZip;

static unsigned int ri_u32(const unsigned char* p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8)
         | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}
static unsigned int ri_u16(const unsigned char* p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static void ri_zip_close(RiZip* z) { if (z) { free(z->e); memset(z, 0, sizeof *z); } }

static int ri_zip_open(RiZip* z, const unsigned char* buf, unsigned int len)
{
    long i;
    unsigned int cdoff, cdn, p;
    memset(z, 0, sizeof *z);
    z->b = buf; z->n = len;
    if (len < 22) return 0;
    /* EOCD, scanned back from the end; the comment field is empty in these archives but
       the scan costs nothing and does not assume that. */
    for (i = (long)len - 22; i >= 0; i--)
        if (buf[i] == 0x50 && buf[i+1] == 0x4B && buf[i+2] == 0x05 && buf[i+3] == 0x06)
            break;
    if (i < 0) return 0;
    cdn   = ri_u16(buf + i + 10);
    cdoff = ri_u32(buf + i + 16);
    if (cdn == 0xFFFF || cdoff == 0xFFFFFFFFu) {
        fprintf(stderr, "remaster: ZIP64 archives are not supported\n");
        return 0;
    }
    z->e = (RiZipEnt*)calloc(cdn ? cdn : 1, sizeof(RiZipEnt));
    if (!z->e) return 0;
    p = cdoff;
    for (z->ne = 0; z->ne < (int)cdn && p + 46 <= len; ) {
        unsigned int nl, el, cl;
        if (ri_u32(buf + p) != 0x02014B50u) break;
        nl = ri_u16(buf + p + 28); el = ri_u16(buf + p + 30); cl = ri_u16(buf + p + 32);
        {
            RiZipEnt* e = &z->e[z->ne];
            unsigned int c = nl < sizeof e->name - 1 ? nl : sizeof e->name - 1;
            memcpy(e->name, buf + p + 46, c); e->name[c] = 0;
            e->method = ri_u16(buf + p + 10);
            e->crc    = ri_u32(buf + p + 16);
            e->comp   = ri_u32(buf + p + 20);
            e->uncomp = ri_u32(buf + p + 24);
            e->lho    = ri_u32(buf + p + 42);
            z->ne++;
        }
        p += 46 + nl + el + cl;
    }
    return z->ne > 0;
}

/* Inflate one member into `out`, which must be exactly its uncompressed size. */
static int ri_zip_read(const RiZip* z, const RiZipEnt* e, unsigned char* out)
{
    unsigned int lh, nl, el, off;
    if (!e || e->lho + 30 > z->n) return 0;
    lh = e->lho;
    if (ri_u32(z->b + lh) != 0x04034B50u) return 0;
    nl = ri_u16(z->b + lh + 26); el = ri_u16(z->b + lh + 28);
    off = lh + 30 + nl + el;
    if (off + e->comp > z->n) return 0;
    if (e->method == 0) {                       /* stored */
        if (e->comp != e->uncomp) return 0;
        memcpy(out, z->b + off, e->uncomp);
        return 1;
    }
    if (e->method != 8) {
        fprintf(stderr, "remaster: ZIP method %u is not deflate\n", e->method);
        return 0;
    }
    {
        /* RAW deflate: the negative window size is what selects it. The tree's other
           zlib callers use uncompress(), which expects the RFC1950 header a ZIP member
           does not have, and fails on every one of these. */
        z_stream s;
        int rc;
        memset(&s, 0, sizeof s);
        if (inflateInit2(&s, -MAX_WBITS) != Z_OK) return 0;
        s.next_in   = (Bytef*)(z->b + off);
        s.avail_in  = e->comp;
        s.next_out  = out;
        s.avail_out = e->uncomp;
        rc = inflate(&s, Z_FINISH);
        inflateEnd(&s);
        if (rc != Z_STREAM_END || s.total_out != e->uncomp) return 0;
    }
    return 1;
}

static const RiZipEnt* ri_zip_find(const RiZip* z, const char* name)
{
    int i;
    for (i = 0; i < z->ne; i++)
        if (!strcasecmp(z->e[i].name, name)) return &z->e[i];
    return NULL;
}

/* ---- the sprite's own two files ---------------------------------------------------- */

typedef struct RiFrame {
    int  sw, sh;            /* the LOGICAL frame, constant per type */
    int  cx0, cy0, cx1, cy1;/* the crop box inside it */
    int  w, h;              /* == cx1-cx0, cy1-cy0 */
    unsigned char* rgba;    /* w*h*4, top-left origin, straight RGBA */
} RiFrame;

static void ri_frame_free(RiFrame* f) { if (f) { free(f->rgba); memset(f, 0, sizeof *f); } }

/* {"size":[267,208],"crop":[101,44,166,114]} -- scanned rather than parsed; the file is
   machine written and two integer lists is the whole of it. */
static int ri_meta(const char* s, RiFrame* f)
{
    const char* p = strstr(s, "\"size\"");
    if (!p || sscanf(p, "\"size\"%*[^0-9-]%d%*[^0-9-]%d", &f->sw, &f->sh) != 2) return 0;
    p = strstr(s, "\"crop\"");
    if (!p || sscanf(p, "\"crop\"%*[^0-9-]%d%*[^0-9-]%d%*[^0-9-]%d%*[^0-9-]%d",
                     &f->cx0, &f->cy0, &f->cx1, &f->cy1) != 4) return 0;
    return f->sw > 0 && f->sh > 0 && f->cx1 > f->cx0 && f->cy1 > f->cy0;
}

/* Uncompressed 32-bit BGRA, bottom-left origin. Refused loudly if it is anything else,
   the way rt_dxt refuses an unknown FourCC: every infantry frame on a real install has
   exactly this header, so a different one means the assumption has moved. */
static int ri_tga(const unsigned char* t, unsigned int len, RiFrame* f)
{
    unsigned int need;
    int w, h;
    if (len < 18) return 0;
    if (t[1] != 0 || t[2] != 2 || t[16] != 32) {
        fprintf(stderr, "remaster: sprite TGA is cmap=%u type=%u bpp=%u, not the "
                        "uncompressed 32-bit form every infantry frame uses\n",
                t[1], t[2], t[16]);
        return 0;
    }
    w = (int)ri_u16(t + 12); h = (int)ri_u16(t + 14);
    need = 18u + (unsigned)w * (unsigned)h * 4u + t[0];
    if (w <= 0 || h <= 0 || len < need) return 0;
    f->w = w; f->h = h;
    f->rgba = (unsigned char*)malloc((size_t)w * h * 4);
    if (!f->rgba) return 0;
    {
        const unsigned char* src = t + 18 + t[0];
        const int flip = (t[17] & 0x20) == 0;   /* bottom-left origin -> flip */
        int y, x;
        for (y = 0; y < h; y++) {
            const unsigned char* r = src + (size_t)(flip ? (h - 1 - y) : y) * w * 4;
            unsigned char* d = f->rgba + (size_t)y * w * 4;
            for (x = 0; x < w; x++) {          /* BGRA -> RGBA */
                d[x*4+0] = r[x*4+2];
                d[x*4+1] = r[x*4+1];
                d[x*4+2] = r[x*4+0];
                d[x*4+3] = r[x*4+3];
            }
        }
    }
    return 1;
}

/* ---- the uniform, recoloured to OUR livery -----------------------------------------
 *
 * The Remaster ships one sprite per type wearing a GREEN uniform and derives every house
 * from it. We do the same thing, but to OUR eight PlayerColorType seats rather than EA's
 * ten team colours, for the reason in the header: EA's set cannot express eight, and its
 * GDI gold is nearer our ORANGE than our GOLD, which would leave a rifleman a different
 * colour from his own radar blip and his own tank.
 *
 * FINDING THE UNIFORM. The green is a single hue band. CNCTDTEAMCOLORS.XML brackets it
 * with two colours whose hues are 104.4 and 133.2 degrees, and that band is what every
 * house shift in that file operates on, so it is EA's own answer to "which texels are the
 * uniform" and is used here as such -- read once from the install rather than written
 * down, so a patch that moves the uniform moves this with it.
 *
 * That band is also the civilian test. Measured across a real install it selects 15..52%
 * of the body texels of E1..E6 and RMBO and EXACTLY NONE of C1..C10, DELPHI, MOEBIUS and
 * CHAN -- which reproduces, with no table at all, the same "does this type wear a
 * uniform" answer the DOS pack derives from its two house rows disagreeing.
 *
 * MAPPING IT. A livery is a sixteen-entry ramp and entries 0..7 are its main light-to-dark
 * run. A uniform texel's own brightness, normalised over the range the uniform actually
 * occupies in that frame, indexes that run and is INTERPOLATED between neighbours. A hard
 * eight-step lookup would quantise the Remaster's smooth shading back to 1995 banding,
 * which is the whole thing this art was chosen for; interpolating keeps the gradient and
 * still lands on our exact colours at both ends.
 */

typedef struct RiBand { float h0, h1; } RiBand;      /* the uniform hue band, in turns */

static void ri_rgb2hsv(float r, float g, float b, float* h, float* s, float* v)
{
    const float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    const float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    const float d = mx - mn;
    *v = mx;
    *s = mx > 0.0f ? d / mx : 0.0f;
    if (d <= 0.0f) { *h = 0.0f; return; }
    if (mx == r)      *h = ((g - b) / d);
    else if (mx == g) *h = ((b - r) / d) + 2.0f;
    else              *h = ((r - g) / d) + 4.0f;
    *h /= 6.0f;
    if (*h < 0.0f) *h += 1.0f;
}

/* Is this texel part of the uniform? Hue inside the band and enough saturation to have a
   hue worth trusting -- a near-grey texel has a meaningless one and must not be caught,
   or boots and rifles get painted too. */
RI_MAYBE_UNUSED static int ri_is_uniform(const unsigned char* p, const RiBand* b)
{
    float h, s, v;
    if (p[3] < 8) return 0;
    ri_rgb2hsv(p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, &h, &s, &v);
    return s >= 0.20f && v >= 0.05f && h >= b->h0 && h <= b->h1;
}

/* Recolour one frame in place. `ramp` is LIVERY_BAND[colour]; only entries 0..7 are used,
   which is the main light-to-dark run. Texels outside the band are untouched, so skin,
   boots, webbing, weapons and the drop shadow all survive exactly as authored. */
RI_MAYBE_UNUSED static void ri_livery(unsigned char* rgba, int w, int h,
                                      const RiBand* band,
                                      const unsigned char ramp[16][3])
{
    long i, n = (long)w * h;
    float vmin = 1.0f, vmax = 0.0f;
    int any = 0;

    for (i = 0; i < n; i++) {
        const unsigned char* p = rgba + i * 4;
        float hh, ss, vv;
        if (!ri_is_uniform(p, band)) continue;
        ri_rgb2hsv(p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, &hh, &ss, &vv);
        if (vv < vmin) vmin = vv;
        if (vv > vmax) vmax = vv;
        any = 1;
    }
    if (!any) return;                       /* a civilian: nothing to recolour */
    if (vmax - vmin < 1e-4f) vmax = vmin + 1e-4f;

    for (i = 0; i < n; i++) {
        unsigned char* p = rgba + i * 4;
        float hh, ss, vv, t;
        int k0, k1, c;
        if (!ri_is_uniform(p, band)) continue;
        ri_rgb2hsv(p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, &hh, &ss, &vv);
        /* brightest uniform texel -> ramp[0], darkest -> ramp[7] */
        t = (1.0f - (vv - vmin) / (vmax - vmin)) * 7.0f;
        if (t < 0.0f) t = 0.0f;
        if (t > 7.0f) t = 7.0f;
        k0 = (int)t; k1 = k0 < 7 ? k0 + 1 : 7;
        {
            const float f = t - (float)k0;
            for (c = 0; c < 3; c++) {
                const float a = (float)ramp[k0][c], b2 = (float)ramp[k1][c];
                float o = a + (b2 - a) * f;
                p[c] = (unsigned char)(o < 0.0f ? 0.0f : (o > 255.0f ? 255.0f : o) + 0.5f);
            }
        }
    }
}

/* The uniform band, read from the install's own team-colour table rather than written
   down here: LowerBounds and UpperBounds are two colours whose hues bracket the green. */
RI_MAYBE_UNUSED static int ri_read_band(RtMeg* cfg, RiBand* out)
{
    unsigned char* xml;
    unsigned int len = 0;
    int ok = 0;
    xml = rt_meg_read(cfg, rt_meg_find(cfg, "DATA\\XML\\CNCTDTEAMCOLORS.XML"), &len);
    if (!xml) return 0;
    {
        unsigned char* z = (unsigned char*)realloc(xml, len + 1);
        if (!z) { free(xml); return 0; }
        xml = z; xml[len] = 0;
    }
    {
        const char* p = (const char*)xml;
        char lo[64], up[64];
        float lr, lg, lb, ur, ug, ub, hs, vs;
        /* the first TeamColorTypeClass carrying bounds defines the band; every house in
           the file shares it, which is what makes it the uniform's band and not a
           house's own colour. */
        if (rt_tag(&p, (const char*)xml + len, "LowerBounds", lo, sizeof lo)) { /* nested */ }
        p = (const char*)xml;
        {
            const char* a = strstr(p, "<LowerBounds>");
            const char* b = a ? strstr(a, "<UpperBounds>") : NULL;
            if (a && b
                && sscanf(a, "<LowerBounds>%*[^<]<R>%f</R>%*[^<]<G>%f</G>%*[^<]<B>%f</B>",
                          &lr, &lg, &lb) == 3
                && sscanf(b, "<UpperBounds>%*[^<]<R>%f</R>%*[^<]<G>%f</G>%*[^<]<B>%f</B>",
                          &ur, &ug, &ub) == 3) {
                float h0, h1;
                ri_rgb2hsv(lr, lg, lb, &h0, &hs, &vs);
                ri_rgb2hsv(ur, ug, ub, &h1, &hs, &vs);
                out->h0 = h0 < h1 ? h0 : h1;
                out->h1 = h0 < h1 ? h1 : h0;
                ok = 1;
            }
        }
    }
    free(xml);
    if (!ok) fprintf(stderr, "remaster: no usable colour bounds in CNCTDTEAMCOLORS.XML\n");
    return ok;
}

/* ---- one strip -----------------------------------------------------------------------
 *
 * A DosStrip is a GRID: `frames` cells of fw x fh across `cols` columns of one texture,
 * addressed facing-major as facenum*stages + stage. Everything downstream -- the draw,
 * picking, health bars, the shadow, the codex page -- reads that geometry, so the
 * Remastered art is built into the same shape rather than a new one.
 *
 * THE CELL IS THE UNION OF EVERY FRAME'S CROP, IN LOGICAL COORDINATES. Each Remastered
 * frame is a tight crop of a logical frame that is constant per type, and the crops
 * differ frame to frame -- E1 alone has 85 distinct crop widths. Aligning them by their
 * own crop boxes would make the man jitter as he animates. Aligning them inside the
 * logical frame is exact, and taking the union of the crops rather than the whole logical
 * frame throws away only margin that is empty in every frame of the strip.
 *
 * THE WORLD SIZE IS THE DOS STRIP'S, NOT THE ART'S. infantry_quad_size sizes the man from
 * fw / sprite_texels_per_unit(), and that divisor is a global 24. A remastered frame is
 * about five times the DOS one, so left alone it would draw a man five times too big.
 * The strip therefore carries its OWN texels-per-unit, derived from the DOS strip it
 * replaces: tpu = rm_fw * 24 / dos_fw. Same footprint on the ground, five times the
 * texels inside it. A DOS strip leaves the field zero and the global constant stands.
 */

typedef struct RiSheet {
    unsigned char* rgba;
    int   texw, texh;                 /* power of two */
    int   frames, facings, stages;
    int   fw, fh, cols;
    float tpu;                        /* texels per world unit for THIS strip */
} RiSheet;

RI_MAYBE_UNUSED static void ri_sheet_free(RiSheet* s)
{
    if (!s) return;
    free(s->rgba);
    memset(s, 0, sizeof *s);
}

static int ri_pot(int v) { int p = 1; while (p < v) p *= 2; return p; }

/* Read one frame of a type by SHP index. Returns 0 and leaves *f zeroed if absent. */
static int ri_load_frame(const RiZip* z, const char* lowty, int shp, RiFrame* f)
{
    char tn[80], mn[80];
    const RiZipEnt *te, *me;
    unsigned char *tb, *mb;
    int ok = 0;
    memset(f, 0, sizeof *f);
    snprintf(tn, sizeof tn, "%s-%04d.tga", lowty, shp);
    snprintf(mn, sizeof mn, "%s-%04d.meta", lowty, shp);
    te = ri_zip_find(z, tn);
    me = ri_zip_find(z, mn);
    if (!te || !me) return 0;
    mb = (unsigned char*)calloc(me->uncomp + 1, 1);
    tb = (unsigned char*)malloc(te->uncomp ? te->uncomp : 1);
    if (mb && tb && ri_zip_read(z, me, mb) && ri_zip_read(z, te, tb)
        && ri_meta((const char*)mb, f) && ri_tga(tb, te->uncomp, f))
        ok = 1;
    free(mb); free(tb);
    if (!ok) ri_frame_free(f);
    return ok;
}

/* THE UNION BOX OF ONE ACTION, from the .meta files alone -- no TGA is decoded, so this
   is cheap enough to run for a reference action before building anything.
   It exists because the SCALE MUST BE ONE NUMBER PER TYPE, not per action. Deriving it
   per action matched each remastered union box to the DOS cell for the same action, and
   those two boxes are built differently enough that the ratio wobbles: measured on E1,
   STAND gives 4.76 and FIRE 4.69 but WALK gives 3.72, so a walking man drew about a fifth
   larger than a standing one. In the 1995 art every strip shares one texels-per-unit and
   the relative sizes come from the art itself; this restores that. */
static int ri_union_box(const RiZip* z, const char* lowty, const short row[3],
                        int* w, int* h)
{
    const int Frame = row[0], Count = row[1], Jump = row[2];
    const int facings = Jump > 0 ? 8 : 1;
    const int stages = Count > 0 ? Count : 1;
    const int n = facings * stages;
    int x0 = 1 << 28, y0 = 1 << 28, x1 = -(1 << 28), y1 = -(1 << 28);
    int i, got = 0;
    if (Count == 0 && Jump == 0 && Frame == 0) return 0;
    for (i = 0; i < n; i++) {
        char mn[80];
        const RiZipEnt* me;
        unsigned char* mb;
        RiFrame f;
        const int shp = Frame + (i / stages) * Jump + (i % stages);
        snprintf(mn, sizeof mn, "%s-%04d.meta", lowty, shp);
        me = ri_zip_find(z, mn);
        if (!me) continue;
        mb = (unsigned char*)calloc(me->uncomp + 1, 1);
        if (!mb) continue;
        memset(&f, 0, sizeof f);
        if (ri_zip_read(z, me, mb) && ri_meta((const char*)mb, &f)) {
            if (f.cx0 < x0) x0 = f.cx0;
            if (f.cy0 < y0) y0 = f.cy0;
            if (f.cx1 > x1) x1 = f.cx1;
            if (f.cy1 > y1) y1 = f.cy1;
            got = 1;
        }
        free(mb);
    }
    if (!got || x1 <= x0 || y1 <= y0) return 0;
    *w = x1 - x0; *h = y1 - y0;
    return 1;
}

/* Build one (type, action) strip. `row` is the {Frame, Count, Jump} triple from
   dosinf_dotable.h; dos_fw is the DOS strip's own frame width, which sets the scale.
   Returns 0 when the type has no art for this action, which is a normal answer. */
RI_MAYBE_UNUSED static int ri_build_strip(const RiZip* z, const char* lowty,
                                          const short row[3], int dos_fw,
                                          const RiBand* band,
                                          const unsigned char ramp[16][3],
                                          float tpu, RiSheet* out)
{
    const int Frame = row[0], Count = row[1], Jump = row[2];
    const int facings = Jump > 0 ? 8 : 1;
    const int stages  = Count > 0 ? Count : 1;
    const int nframes = facings * stages;
    int x0 = 1 << 28, y0 = 1 << 28, x1 = -(1 << 28), y1 = -(1 << 28);
    RiFrame* fr;
    int i, ok = 0;

    memset(out, 0, sizeof *out);
    if (Count == 0 && Jump == 0 && Frame == 0) return 0;      /* no art for this action */
    if (nframes <= 0 || nframes > 4096) return 0;

    fr = (RiFrame*)calloc((size_t)nframes, sizeof(RiFrame));
    if (!fr) return 0;

    /* Load every frame and take the union of the crops, in logical coordinates. */
    for (i = 0; i < nframes; i++) {
        const int f_ = i / stages, s_ = i % stages;
        const int shp = Frame + f_ * Jump + s_;
        if (!ri_load_frame(z, lowty, shp, &fr[i])) continue;
        if (fr[i].cx0 < x0) x0 = fr[i].cx0;
        if (fr[i].cy0 < y0) y0 = fr[i].cy0;
        if (fr[i].cx1 > x1) x1 = fr[i].cx1;
        if (fr[i].cy1 > y1) y1 = fr[i].cy1;
    }
    if (x1 <= x0 || y1 <= y0) goto done;

    out->fw = x1 - x0;
    out->fh = y1 - y0;
    out->frames = nframes;
    out->facings = facings;
    out->stages = stages;
    /* A squarish grid keeps the sheet close to square, which wastes least after the
       power-of-two round up. */
    out->cols = (int)(0.5 + 1.0 * (nframes > 0 ? 1 : 1));
    {
        int c = 1;
        while (c * c < nframes) c++;
        out->cols = c;
    }
    out->texw = ri_pot(out->cols * out->fw);
    {
        const int rows = (nframes + out->cols - 1) / out->cols;
        out->texh = ri_pot(rows * out->fh);
    }
    /* ONE SCALE FOR THE WHOLE TYPE, handed in. See ri_union_box for why it cannot be
       derived here from this action's own box. */
    out->tpu = tpu;
    (void)dos_fw;
    out->rgba = (unsigned char*)calloc((size_t)out->texw * out->texh, 4);
    if (!out->rgba) goto done;

    for (i = 0; i < nframes; i++) {
        const int cx = (i % out->cols) * out->fw;
        const int cy = (i / out->cols) * out->fh;
        int y;
        if (!fr[i].rgba) continue;
        ri_livery(fr[i].rgba, fr[i].w, fr[i].h, band, ramp);
        for (y = 0; y < fr[i].h; y++) {
            const int dy = cy + (fr[i].cy0 - y0) + y;
            const int dx = cx + (fr[i].cx0 - x0);
            if (dy < 0 || dy >= out->texh) continue;
            if (dx < 0 || dx + fr[i].w > out->texw) continue;
            memcpy(out->rgba + ((size_t)dy * out->texw + dx) * 4,
                   fr[i].rgba + (size_t)y * fr[i].w * 4,
                   (size_t)fr[i].w * 4);
        }
    }
    ok = 1;

done:
    for (i = 0; i < nframes; i++) ri_frame_free(&fr[i]);
    free(fr);
    if (!ok) ri_sheet_free(out);
    return ok;
}

#endif /* CNC3D_REMASTER_INF_H */
