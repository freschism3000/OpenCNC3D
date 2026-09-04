/* ==================================================================================== *
 *  remaster_tex.h -- build a terrain atlas from the player's own Command & Conquer
 *  Remastered Collection.
 *
 *  WE SHIP NONE OF THIS ART AND WE NEVER WILL. It is Electronic Arts' work in a product
 *  they sell. Nothing here is baked, cached into the repository, or carried in a pack:
 *  the archives are opened in the player's installation, at runtime, on the frame they
 *  choose "Remastered Textures", and the atlas that comes out lives only in memory.
 *  game/remaster.h is what finds the installation; this is what reads it.
 *
 *  WHY THE ATLAS COMES OUT AT 120 AND NOT 128, which is the one number here that looks
 *  arbitrary and is not. Every cell in a pack carries four NORMALISED texture
 *  coordinates, baked against the cartridge atlas's 24-texel tile on a 26 pitch. A
 *  second atlas addresses those same coordinates exactly -- not nearly, exactly -- when
 *  it is the first one scaled by a whole number in both axes: tile 24k, gutter k, pitch
 *  26k, same 32 columns. At k=5 that is a 120-texel cell, and
 *      (col*130 + 5) / (32*130)  ==  (col*26 + 1) / (32*26)
 *  for every column, algebraically. So the renderer binds a different texture and
 *  changes nothing else: no second coordinate table, no paging, no pack format change.
 *  The cost is a 128 -> 120 downscale, about 6%, against a cell that occupies roughly
 *  129 screen pixels at 1080p (measured in engine with probecell, two adjacent cells).
 *  Native 128 is possible and needs a paged atlas with its own rectangle table; it buys
 *  very little for a great deal more machinery. k=5 is the chosen scale.
 *
 *  THE ALPHA IS THE CARTRIDGE'S, NEVER THE REMASTER'S. A transparent texel is not a
 *  colour here, it is a mechanism: it is where draw_water and draw_seabed show through,
 *  and PackCell.holes -- baked from that same cartridge mask -- independently gates the
 *  sea, the radar's water colour and the shroud's ground height. Taking the DDS's own
 *  alpha would desynchronise the atlas from every one of those. So the cartridge's 24x24
 *  mask is upscaled bilinearly to the tile and the existing alpha test cuts it.
 *
 *  Formats, all verified against a real install rather than documentation
 *  (tools/remaster/ carries the Python these numbers were checked with):
 *    MEG V3, unencrypted   magic 0xFFFFFFFF, version 0x3F7D70A4, nothing compressed
 *    tiles                 128x128 DXT1, 8 mip levels in each file (we take level 0)
 *    the mapping           DATA\XML\TILESETS\TD_TERRAIN_<THEATER>.XML keys art by
 *                          <Name>/<Shape>, which IS the (template, icon) pair the
 *                          cartridge's own TL4/TL8 tables give for every bank slot
 * ==================================================================================== */
#ifndef CNC3D_REMASTER_TEX_H
#define CNC3D_REMASTER_TEX_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <strings.h>          /* strcasecmp */
#endif

#include "remaster.h"
#include "terrain_tiles.h"
#include "edit_tables.h"

#if defined(__GNUC__) || defined(__clang__)
#define RT_MAYBE_UNUSED __attribute__((unused))
#else
#define RT_MAYBE_UNUSED
#endif

#define RT_K        5                      /* the whole-number scale; see the note above */
#define RT_TS       (TT_TS * RT_K)         /* 120 */
#define RT_GUTTER   (TT_GUTTER * RT_K)     /*   5 */
#define RT_PITCH    (TT_PITCH * RT_K)      /* 130 */
#define RT_SRC      128                    /* what the Remaster actually ships */

/* ---- MEG V3 ----------------------------------------------------------------------- */

typedef struct RtRec { unsigned int size, start; unsigned short nameidx; } RtRec;

typedef struct RtMeg {
    FILE*  f;
    char*  namepool;        /* every name, NUL separated, uppercased */
    unsigned int* nameoff;  /* namepool offset per name index */
    RtRec* rec;
    int    n, nnames;
} RtMeg;

static void rt_meg_close(RtMeg* m)
{
    if (!m) return;
    if (m->f) fclose(m->f);
    free(m->namepool); free(m->nameoff); free(m->rec);
    memset(m, 0, sizeof *m);
}

static int rt_meg_open(RtMeg* m, const char* path)
{
    unsigned int hdr[6];
    unsigned char* names = NULL;
    unsigned char* recs = NULL;
    unsigned int o = 0;
    int i;

    memset(m, 0, sizeof *m);
    m->f = fopen(path, "rb");
    if (!m->f) return 0;
    if (fread(hdr, 4, 6, m->f) != 6) { rt_meg_close(m); return 0; }
    if (hdr[0] != 0xFFFFFFFFu) {
        fprintf(stderr, "remaster: %s has magic %#x -- encrypted MEGs are not supported\n",
                path, hdr[0]);
        rt_meg_close(m); return 0;
    }
    if (hdr[1] != 0x3F7D70A4u) {
        fprintf(stderr, "remaster: %s is MEG version %#x, not the V3 this reads\n",
                path, hdr[1]);
        rt_meg_close(m); return 0;
    }
    /* countA and countB are equal in every file anyone has seen, and the two published
       accounts of which is which disagree. Assert rather than pick a side. */
    if (hdr[3] != hdr[4]) {
        fprintf(stderr, "remaster: %s has %u records against %u names -- the two counts "
                        "have always been equal; this file is not understood\n",
                path, hdr[3], hdr[4]);
        rt_meg_close(m); return 0;
    }
    m->n = m->nnames = (int)hdr[3];
    if (m->n <= 0 || m->n > 4000000) { rt_meg_close(m); return 0; }

    names = (unsigned char*)malloc(hdr[5] ? hdr[5] : 1);
    if (!names || fread(names, 1, hdr[5], m->f) != hdr[5]) {
        free(names); rt_meg_close(m); return 0;
    }
    m->namepool = (char*)malloc(hdr[5] + 1);
    m->nameoff = (unsigned int*)malloc(sizeof(unsigned int) * (size_t)m->nnames);
    if (!m->namepool || !m->nameoff) { free(names); rt_meg_close(m); return 0; }
    {
        unsigned int po = 0;
        for (i = 0; i < m->nnames && o + 2 <= hdr[5]; i++) {
            unsigned int len = (unsigned int)names[o] | ((unsigned int)names[o + 1] << 8);
            unsigned int k;
            o += 2;
            if (o + len > hdr[5]) break;
            m->nameoff[i] = po;
            for (k = 0; k < len; k++) {
                char c = (char)names[o + k];
                if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
                if (c == '/') c = '\\';
                m->namepool[po++] = c;
            }
            m->namepool[po++] = 0;
            o += len;
        }
        if (i != m->nnames) {
            fprintf(stderr, "remaster: %s: filename table ran short (%d of %d)\n",
                    path, i, m->nnames);
            free(names); rt_meg_close(m); return 0;
        }
    }
    free(names);

    recs = (unsigned char*)malloc((size_t)m->n * 20);
    if (!recs || fread(recs, 20, (size_t)m->n, m->f) != (size_t)m->n) {
        free(recs); rt_meg_close(m); return 0;
    }
    m->rec = (RtRec*)malloc(sizeof(RtRec) * (size_t)m->n);
    if (!m->rec) { free(recs); rt_meg_close(m); return 0; }
    for (i = 0; i < m->n; i++) {
        const unsigned char* r = recs + (size_t)i * 20;
        memcpy(&m->rec[i].size,  r + 0x0A, 4);
        memcpy(&m->rec[i].start, r + 0x0E, 4);
        memcpy(&m->rec[i].nameidx, r + 0x12, 2);
    }
    free(recs);
    return 1;
}

/* Names are stored uppercase with backslashes; the query is normalised to match. */
static const RtRec* rt_meg_find(const RtMeg* m, const char* name)
{
    char want[512];
    size_t i = 0;
    int k;
    for (; name[i] && i + 1 < sizeof want; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c == '/') c = '\\';
        want[i] = c;
    }
    want[i] = 0;
    for (k = 0; k < m->n; k++) {
        const unsigned short ni = m->rec[k].nameidx;
        if (ni < m->nnames && !strcmp(m->namepool + m->nameoff[ni], want))
            return &m->rec[k];
    }
    return NULL;
}

static unsigned char* rt_meg_read(RtMeg* m, const RtRec* r, unsigned int* len)
{
    unsigned char* buf;
    if (!r) return NULL;
    buf = (unsigned char*)malloc(r->size ? r->size : 1);
    if (!buf) return NULL;
    if (fseek(m->f, (long)r->start, SEEK_SET) != 0
        || fread(buf, 1, r->size, m->f) != r->size) { free(buf); return NULL; }
    if (len) *len = r->size;
    return buf;
}

/* ---- DXT1 ------------------------------------------------------------------------- */

static void rt_565(unsigned short c, unsigned char* o)
{
    const int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    o[0] = (unsigned char)((r << 3) | (r >> 2));
    o[1] = (unsigned char)((g << 2) | (g >> 4));
    o[2] = (unsigned char)((b << 3) | (b >> 2));
}

/* Level 0 only, RGB, into a w*h*3 buffer.
   BOTH DXT1 AND DXT5, because the terrain is a mix of the two and it is nearly an even
   one: 409 of TEMPERAT's 758 tiles are DXT1 and 349 are DXT5. Published guidance says
   terrain is DXT1 and that DXT5 is for decorations; on a real install that is simply not
   so, and a DXT1-only reader silently drops 46% of the theater.
   The two share a colour block -- 16-bit endpoints then two bits per texel -- and differ
   only in that DXT5 puts 8 bytes of alpha in front of it and always uses the four-colour
   interpolation, where DXT1 switches to three-plus-transparent when c0 <= c1. We take no
   alpha from either (it comes from the cartridge), so DXT5 is DXT1 at an offset. */
static int rt_dxt(const unsigned char* dds, unsigned int len, int* wOut, int* hOut,
                  unsigned char* out, int outcap)
{
    unsigned int hsize, h, w, mips;
    const unsigned char* p;
    int bx, by, bw, bh, bpb, coff;

    if (len < 128 || memcmp(dds, "DDS ", 4) != 0) return 0;
    memcpy(&hsize, dds + 4, 4);
    memcpy(&h, dds + 12, 4);
    memcpy(&w, dds + 16, 4);
    memcpy(&mips, dds + 28, 4);
    if (!memcmp(dds + 84, "DXT1", 4))                          { bpb = 8;  coff = 0; }
    else if (!memcmp(dds + 84, "DXT5", 4)
             || !memcmp(dds + 84, "DXT3", 4))                  { bpb = 16; coff = 8; }
    else {
        fprintf(stderr, "remaster: unsupported texture format '%.4s'\n", dds + 84);
        return 0;
    }
    if (w == 0 || h == 0 || w > 4096 || h > 4096) return 0;
    if ((int)(w * h * 3) > outcap) return 0;
    p = dds + 4 + hsize;
    bw = (int)((w + 3) / 4); bh = (int)((h + 3) / 4);
    if ((unsigned)(p - dds) + (unsigned)(bw * bh * bpb) > len) return 0;

    for (by = 0; by < bh; by++) {
        for (bx = 0; bx < bw; bx++) {
            const unsigned char* b = p + ((size_t)by * bw + bx) * bpb + coff;
            unsigned short c0, c1;
            unsigned int bits;
            unsigned char pal[4][3];
            int py, px, i;
            memcpy(&c0, b, 2); memcpy(&c1, b + 2, 2); memcpy(&bits, b + 4, 4);
            rt_565(c0, pal[0]); rt_565(c1, pal[1]);
            if (c0 > c1 || coff) {          /* DXT5 is always the four-colour form */
                for (i = 0; i < 3; i++) {
                    pal[2][i] = (unsigned char)((2 * pal[0][i] + pal[1][i]) / 3);
                    pal[3][i] = (unsigned char)((pal[0][i] + 2 * pal[1][i]) / 3);
                }
            } else {
                for (i = 0; i < 3; i++) {
                    pal[2][i] = (unsigned char)((pal[0][i] + pal[1][i]) / 2);
                    pal[3][i] = 0;
                }
            }
            for (py = 0; py < 4; py++) {
                for (px = 0; px < 4; px++) {
                    const int y = by * 4 + py, x = bx * 4 + px;
                    unsigned char* o;
                    if (y >= (int)h || x >= (int)w) continue;
                    o = out + ((size_t)y * w + x) * 3;
                    i = (int)((bits >> (2 * (py * 4 + px))) & 3);
                    o[0] = pal[i][0]; o[1] = pal[i][1]; o[2] = pal[i][2];
                }
            }
        }
    }
    *wOut = (int)w; *hOut = (int)h;
    (void)mips;                 /* the file carries 8; level 0 is all we place */
    return 1;
}

/* ---- the tileset XML ---------------------------------------------------------------
 *
 * A targeted scanner rather than a parser, and the file's shape is why that is safe: it
 * is machine generated, every Tile is
 *     <Tile><Key><Name>X</Name><Shape>N</Shape></Key>
 *           <Value><Frames><Frame>rel\path.tga</Frame>...</Frames>...</Value></Tile>
 * and we want exactly three fields out of it. cnc_eyes carries no XML parser and is not
 * getting one for this. Anything the scanner cannot make sense of is SKIPPED and counted,
 * never guessed at, and the count is reported.
 *
 * NEVER CONSTRUCT A TERRAIN FILENAME. The frame text is the authority. The obvious rule
 * (template + extension + a four-digit index) is wrong for a large minority of tiles --
 * SH1 and friends carry a second suffix -- and a constructed name silently misses them.
 */

/* The text between <tag> and </tag>, starting the search at *p and advancing it. */
static int rt_tag(const char** p, const char* end, const char* tag,
                  char* out, int cap)
{
    char open[64], close[64];
    const char* a;
    const char* b;
    snprintf(open, sizeof open, "<%s>", tag);
    snprintf(close, sizeof close, "</%s>", tag);
    a = strstr(*p, open);
    if (!a || a >= end) return 0;
    a += strlen(open);
    b = strstr(a, close);
    if (!b || b >= end) return 0;
    {
        int n = (int)(b - a);
        int i = 0, j = 0;
        while (i < n && (a[i] == ' ' || a[i] == '\t' || a[i] == '\r' || a[i] == '\n')) i++;
        while (n > i && (a[n-1] == ' ' || a[n-1] == '\t' || a[n-1] == '\r' || a[n-1] == '\n')) n--;
        for (; i < n && j + 1 < cap; i++) out[j++] = a[i];
        out[j] = 0;
    }
    *p = b + strlen(close);
    return 1;
}

/* TEMPERAT/DESERT/WINTER -> the tileset file. SNOW and SAND have no remaster at all. */
static const char* rt_xml_for(const char* theater)
{
    if (!strcmp(theater, "TEMPERAT")) return "TD_TERRAIN_TEMPERATE.XML";
    if (!strcmp(theater, "DESERT"))   return "TD_TERRAIN_DESERT.XML";
    if (!strcmp(theater, "WINTER"))   return "TD_TERRAIN_WINTER.XML";
    return NULL;
}

static int rt_template_id(const char* name)
{
    int i;
    for (i = 0; i < EDIT_TEMPLATE_COUNT; i++)
        if (EDIT_TEMPLATES[i].name && !strcasecmp(EDIT_TEMPLATES[i].name, name))
            return i;
    return -1;
}

/* ---- resampling -------------------------------------------------------------------- */

/* 128 -> 120 by area average. The ratio is 0.9375, so every destination texel covers a
   little over one source texel and a box filter is indistinguishable from anything
   fancier at this scale while costing nothing. */
static void rt_box_rgb(const unsigned char* src, int sw, int sh,
                       unsigned char* dst, int dw, int dh)
{
    int y, x, c;
    for (y = 0; y < dh; y++) {
        const int y0 = y * sh / dh, y1 = ((y + 1) * sh + dh - 1) / dh;
        for (x = 0; x < dw; x++) {
            const int x0 = x * sw / dw, x1 = ((x + 1) * sw + dw - 1) / dw;
            int sy, sx, n = 0, acc[3] = {0, 0, 0};
            for (sy = y0; sy < y1 && sy < sh; sy++)
                for (sx = x0; sx < x1 && sx < sw; sx++) {
                    const unsigned char* q = src + ((size_t)sy * sw + sx) * 3;
                    acc[0] += q[0]; acc[1] += q[1]; acc[2] += q[2]; n++;
                }
            if (!n) n = 1;
            for (c = 0; c < 3; c++) dst[((size_t)y * dw + x) * 3 + c] = (unsigned char)(acc[c] / n);
        }
    }
}

/* The cartridge's 24x24 alpha for one slot, bilinearly up to the tile size. Unthresholded
   on purpose: the existing alpha test cuts it per fragment, which lands the shoreline
   contour sub-texel instead of on the 24-grid it was baked at. */
static void rt_alpha_up(const unsigned char* cart, int caw, int sx, int sy,
                        unsigned char* dst, int n)
{
    int y, x;
    for (y = 0; y < n; y++) {
        const float fy = ((float)y + 0.5f) * (float)TT_TS / (float)n - 0.5f;
        int y0 = (int)(fy < 0 ? 0 : fy), y1 = y0 + 1 < TT_TS ? y0 + 1 : TT_TS - 1;
        const float wy = fy - (float)y0 < 0 ? 0.0f : fy - (float)y0;
        for (x = 0; x < n; x++) {
            const float fx = ((float)x + 0.5f) * (float)TT_TS / (float)n - 0.5f;
            int x0 = (int)(fx < 0 ? 0 : fx), x1 = x0 + 1 < TT_TS ? x0 + 1 : TT_TS - 1;
            const float wx = fx - (float)x0 < 0 ? 0.0f : fx - (float)x0;
            const float a00 = cart[((size_t)(sy + y0) * caw + sx + x0) * 4 + 3];
            const float a01 = cart[((size_t)(sy + y0) * caw + sx + x1) * 4 + 3];
            const float a10 = cart[((size_t)(sy + y1) * caw + sx + x0) * 4 + 3];
            const float a11 = cart[((size_t)(sy + y1) * caw + sx + x1) * 4 + 3];
            const float top = a00 + (a01 - a00) * wx, bot = a10 + (a11 - a10) * wx;
            dst[(size_t)y * n + x] = (unsigned char)(top + (bot - top) * wy + 0.5f);
        }
    }
}

/* ---- the atlas --------------------------------------------------------------------- */

typedef struct RtAtlas {
    unsigned char* rgba;    /* w*h*4, padded to powers of two for upload */
    int w, h;               /* padded */
    int uw, uh;             /* used, which is what the uv scale divides by */
    int placed, missing;
} RtAtlas;

RT_MAYBE_UNUSED static void rt_atlas_free(RtAtlas* a)
{
    if (!a) return;
    free(a->rgba);
    memset(a, 0, sizeof *a);
}

static int rt_pot(int v) { int p = 1; while (p < v) p *= 2; return p; }

/* Build the whole theater's atlas. cart/caw/cah are the CARTRIDGE atlas as uploaded,
   which is where every tile's alpha comes from. Returns 1 on success. */
RT_MAYBE_UNUSED static int rt_build_atlas(const char* install, const char* theater,
                                          const unsigned char* cart, int caw, int cah,
                                          RtAtlas* out)
{
    char path[RM_PATH_MAX], data[RM_PATH_MAX], real[256];
    const char* xmlname = rt_xml_for(theater);
    RtMeg cfg, tex[8];
    int ntex = 0, i, ok = 0;
    unsigned char* xml = NULL;
    unsigned int xmllen = 0;
    char rootpath[256] = {0};
    int nslots = 0;
    unsigned char* src = NULL;
    unsigned char* small = NULL;
    unsigned char* alpha = NULL;

    memset(out, 0, sizeof *out);
    memset(&cfg, 0, sizeof cfg);
    memset(tex, 0, sizeof tex);
    if (!xmlname) return 0;                      /* SNOW and SAND have no remaster */
    if (!rm_data_dir(install, data, sizeof data)) return 0;

    if (!rm_find_entry(data, "CONFIG.MEG", real, sizeof real)) return 0;
    rm_join(path, sizeof path, data, real);
    if (!rt_meg_open(&cfg, path)) return 0;

    /* MOUNT EVERY TEXTURE ARCHIVE PRESENT and resolve by path. Data/MEGAFILES.XML lists
       archives that are not on disk, so the set is discovered rather than named: on this
       install the terrain is in TEXTURES_TD_SRGB.MEG, but nothing here depends on that. */
    {
#if !defined(_WIN32)
        DIR* d = opendir(data);
        struct dirent* e;
        if (d) {
            while ((e = readdir(d)) != NULL && ntex < 8) {
                const size_t l = strlen(e->d_name);
                if (l < 5 || strncasecmp(e->d_name, "TEXTURES", 8) != 0) continue;
                if (strcasecmp(e->d_name + l - 4, ".MEG") != 0) continue;
                rm_join(path, sizeof path, data, e->d_name);
                if (rt_meg_open(&tex[ntex], path)) ntex++;
            }
            closedir(d);
        }
#else
        char pat[RM_PATH_MAX];
        WIN32_FIND_DATAA fd;
        HANDLE h;
        snprintf(pat, sizeof pat, "%s\\TEXTURES*.MEG", data);
        h = FindFirstFileA(pat, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                rm_join(path, sizeof path, data, fd.cFileName);
                if (rt_meg_open(&tex[ntex], path)) ntex++;
            } while (ntex < 8 && FindNextFileA(h, &fd));
            FindClose(h);
        }
#endif
    }
    if (!ntex) { fprintf(stderr, "remaster: no TEXTURES*.MEG under %s\n", data); goto done; }

    snprintf(path, sizeof path, "DATA\\XML\\TILESETS\\%s", xmlname);
    xml = rt_meg_read(&cfg, rt_meg_find(&cfg, path), &xmllen);
    if (!xml) { fprintf(stderr, "remaster: %s is not in CONFIG.MEG\n", path); goto done; }
    {   /* NUL terminate so the scanner can use strstr */
        unsigned char* z = (unsigned char*)realloc(xml, xmllen + 1);
        if (!z) goto done;
        xml = z; xml[xmllen] = 0;
    }
    {
        const char* p = (const char*)xml;
        if (!rt_tag(&p, (const char*)xml + xmllen, "RootTexturePath", rootpath, sizeof rootpath)) {
            fprintf(stderr, "remaster: %s has no RootTexturePath\n", xmlname);
            goto done;
        }
    }

    tt_table(theater, &nslots);
    if (nslots <= 0) goto done;

    out->uw = TT_COLS * RT_PITCH;
    out->uh = ((nslots + TT_COLS - 1) / TT_COLS) * RT_PITCH;
    out->w = rt_pot(out->uw); out->h = rt_pot(out->uh);
    out->rgba = (unsigned char*)calloc((size_t)out->w * out->h, 4);
    src   = (unsigned char*)malloc((size_t)RT_SRC * RT_SRC * 3);
    small = (unsigned char*)malloc((size_t)RT_TS * RT_TS * 3);
    alpha = (unsigned char*)malloc((size_t)RT_TS * RT_TS);
    if (!out->rgba || !src || !small || !alpha) goto done;

    {
        const char* p = (const char*)xml;
        const char* end = (const char*)xml + xmllen;
        char name[64], shape[32], frame[256];
        while (rt_tag(&p, end, "Name", name, sizeof name)) {
            const char* q = p;
            int tid, icon, slot, sea = 0, sw, sh, gy, gx;
            const RtRec* rec = NULL;
            unsigned char* dds;
            unsigned int ddslen = 0;
            int col, row, x0, y0, yy;

            if (!rt_tag(&p, end, "Shape", shape, sizeof shape)) break;
            if (!rt_tag(&p, end, "Frame", frame, sizeof frame)) { p = q; continue; }
            if (!frame[0]) continue;             /* an empty Frame is a real blank cell */
            icon = atoi(shape);
            tid = rt_template_id(name);
            if (tid < 0) continue;               /* a template our engine does not have */
            slot = tt_slot_of(theater, tid, icon, &sea);
            if (slot < 0 || slot >= nslots) continue;   /* not in the cartridge's bank */

            snprintf(path, sizeof path, "DATA\\ART\\TEXTURES\\SRGB\\%s\\%s",
                     rootpath, frame);
            {   /* the XML names .tga; what ships is .DDS */
                size_t l = strlen(path);
                if (l > 4 && !strcasecmp(path + l - 4, ".TGA"))
                    memcpy(path + l - 4, ".DDS", 4);
            }
            for (i = 0; i < ntex && !rec; i++) rec = rt_meg_find(&tex[i], path);
            if (!rec) {
                if (getenv("CNC3D_RT_DEBUG") && out->missing < 8)
                    fprintf(stderr, "remaster: MISS %s|%d -> %s\n", name, icon, path);
                out->missing++; continue;
            }
            dds = rt_meg_read(&tex[i - 1], rec, &ddslen);
            if (!dds) { out->missing++; continue; }
            if (!rt_dxt(dds, ddslen, &sw, &sh, src, RT_SRC * RT_SRC * 3)
                || sw != RT_SRC || sh != RT_SRC) {
                free(dds); out->missing++; continue;
            }
            free(dds);
            rt_box_rgb(src, sw, sh, small, RT_TS, RT_TS);

            col = slot % TT_COLS; row = slot / TT_COLS;
            gx = col * TT_PITCH + TT_GUTTER;                /* the CARTRIDGE rect, for alpha */
            gy = row * TT_PITCH + TT_GUTTER;
            if (gx + TT_TS > caw || gy + TT_TS > cah) { out->missing++; continue; }
            rt_alpha_up(cart, caw, gx, gy, alpha, RT_TS);

            x0 = col * RT_PITCH + RT_GUTTER;
            y0 = row * RT_PITCH + RT_GUTTER;
            for (yy = 0; yy < RT_TS; yy++) {
                unsigned char* d = out->rgba + ((size_t)(y0 + yy) * out->w + x0) * 4;
                const unsigned char* s = small + (size_t)yy * RT_TS * 3;
                const unsigned char* a = alpha + (size_t)yy * RT_TS;
                int xx;
                for (xx = 0; xx < RT_TS; xx++) {
                    d[xx * 4 + 0] = s[xx * 3 + 0];
                    d[xx * 4 + 1] = s[xx * 3 + 1];
                    d[xx * 4 + 2] = s[xx * 3 + 2];
                    d[xx * 4 + 3] = a[xx];
                }
            }
            /* THE GUTTER, and it is the same trick the 24-texel atlas plays: replicate
               each tile's own edge outward, so a bilinear sample at a cell boundary
               blends the edge with a copy of itself -- which is the clamp the console's
               RDP performs -- instead of with whatever tile the packer put next door. */
            for (yy = 0; yy < RT_GUTTER; yy++) {
                memcpy(out->rgba + ((size_t)(y0 - 1 - yy) * out->w + x0) * 4,
                       out->rgba + ((size_t)y0 * out->w + x0) * 4, (size_t)RT_TS * 4);
                memcpy(out->rgba + ((size_t)(y0 + RT_TS + yy) * out->w + x0) * 4,
                       out->rgba + ((size_t)(y0 + RT_TS - 1) * out->w + x0) * 4,
                       (size_t)RT_TS * 4);
            }
            for (yy = -RT_GUTTER; yy < RT_TS + RT_GUTTER; yy++) {
                unsigned char* rowp = out->rgba + ((size_t)(y0 + yy) * out->w) * 4;
                int g;
                for (g = 1; g <= RT_GUTTER; g++) {
                    memcpy(rowp + (size_t)(x0 - g) * 4, rowp + (size_t)x0 * 4, 4);
                    memcpy(rowp + (size_t)(x0 + RT_TS - 1 + g) * 4,
                           rowp + (size_t)(x0 + RT_TS - 1) * 4, 4);
                }
            }
            out->placed++;
        }
    }
    ok = (out->placed > 0);
    fprintf(stderr, "remaster: %s atlas %dx%d (used %dx%d), %d tiles placed, %d missing\n",
            theater, out->w, out->h, out->uw, out->uh, out->placed, out->missing);

done:
    free(src); free(small); free(alpha); free(xml);
    rt_meg_close(&cfg);
    for (i = 0; i < ntex; i++) rt_meg_close(&tex[i]);
    if (!ok) rt_atlas_free(out);
    return ok;
}

#endif /* CNC3D_REMASTER_TEX_H */
