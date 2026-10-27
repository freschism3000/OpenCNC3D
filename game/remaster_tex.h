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
 *  WHY THE BASE ATLAS COMES OUT AT 120, which is the one number here that looks
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
 *  The Experimental temperate set uses original 256px generated source cells.
 *  Its atlas uses k=11 and 264px slots on GPUs supporting 16384px textures,
 *  preserving the same normalized coordinates. Base Remastered stays at k=5.
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
#ifndef OPENCNC3D_REMASTER_TEX_H
#define OPENCNC3D_REMASTER_TEX_H

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

#define RT_K_BASE   5                      /* 120px base Remastered cells */
#define RT_K_DETAIL 11                     /* 264px cells carry all 256px generated detail */
#define RT_SRC      128                    /* what the Remaster actually ships */
#define RT_OVERRIDE_SRC 256                /* the generated temperate PNGs */
#define RT_GRASS_ICONS 16
#define RT_GRASS_BYTES ((size_t)RT_GRASS_ICONS * RT_OVERRIDE_SRC * RT_OVERRIDE_SRC * 3)
#define RT_ROAD_COUNT 43
#define RT_ROAD_SHAPES 12
#define RT_ROAD_HEADER_BYTES (12 + RT_ROAD_COUNT * RT_ROAD_SHAPES * 4)
#define RT_CLIFF_COUNT 38
#define RT_CLIFF_SHAPES 6
#define RT_CLIFF_CELL_BYTES ((size_t)RT_OVERRIDE_SRC * RT_OVERRIDE_SRC * 3)
#define RT_CLIFF_HEADER_BYTES (12 + RT_CLIFF_COUNT * RT_CLIFF_SHAPES * 4)
#define RT_SHORE_HEADER_BYTES (12 + TT_TEMPERAT_SLOTS * 4)
/* Set by the Experimental F5 controls before the Remastered atlas is built. */
static int rt_generated_temperate_on = 0;
/* Chosen against GL_MAX_TEXTURE_SIZE before building. 11 needs a 16384-wide
   color atlas; older GPUs can use 9 or 5 without rejecting the experiment. */
static int rt_detail_scale = RT_K_DETAIL;

/* The environment variables remain useful for art iteration. In a repo run,
   locate the approved packs automatically; in a packaged game they can live
   beside the other .pack files in the working directory. */
static int rt_override_exists(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static const char* rt_override_path(const char* env_name,
                                    const char* packaged,
                                    const char* repo_root,
                                    const char* repo_run)
{
    const char* path = getenv(env_name);
    static char parent_packaged[512];
    if (path && (!strcmp(path, "off") || !strcmp(path, "none"))) return NULL;
    if (path && *path) return path;
    if (rt_override_exists(packaged)) return packaged;
    /* RA3D runs from playable/ra3d but shares OpenCNC3D's authored terrain packs,
       which game/make-build.sh places in the parent playable/ folder. */
    if (packaged && snprintf(parent_packaged, sizeof parent_packaged, "../%s", packaged)
        < (int)sizeof parent_packaged && rt_override_exists(parent_packaged))
        return parent_packaged;
    if (rt_override_exists(repo_root)) return repo_root;
    if (rt_override_exists(repo_run)) return repo_run;
    return NULL;
}

/* A complete RGB pack of the sixteen CLEAR1 icons, used for local art iteration.
   The source PNGs stay editable; prepare_tiles.py writes their exact RGB bytes in
   icon order. A missing or truncated pack leaves the Remastered art intact. */
static unsigned char* rt_grass_override_read(void)
{
    if (!rt_generated_temperate_on && !getenv("OPENCNC3D_GRASS_OVERRIDE")) return NULL;
    const char* path = rt_override_path(
        "OPENCNC3D_GRASS_OVERRIDE", "temperate_grass_rgb.bin",
        "art/generated/temperate_grass/v3_color_matched/grass_rgb.bin",
        "../art/generated/temperate_grass/v3_color_matched/grass_rgb.bin");
    FILE* f;
    unsigned char* rgb;
    int extra;
    if (!path || !*path) return NULL;
    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "remaster: cannot open grass override %s\n", path);
        return NULL;
    }
    rgb = (unsigned char*)malloc(RT_GRASS_BYTES);
    if (!rgb) { fclose(f); return NULL; }
    extra = fread(rgb, 1, RT_GRASS_BYTES, f) == RT_GRASS_BYTES ? fgetc(f) : 0;
    fclose(f);
    if (extra != EOF) {
        fprintf(stderr, "remaster: grass override must be exactly %zu bytes: %s\n",
                RT_GRASS_BYTES, path);
        free(rgb);
        return NULL;
    }
    fprintf(stderr, "remaster: loaded 16 CLEAR1 grass replacements from %s\n", path);
    return rgb;
}

/* Generated temperate cliff cells are indexed by template S01..S38 and shape
   0..5. An absent offset means that shape has no replacement. The single pack
   keeps 157 small files out of the runtime path. */
typedef struct RtCliffOverride {
    unsigned char* data;
    size_t len;
} RtCliffOverride;

static unsigned int rt_le32(const unsigned char* p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8)
         | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static RtCliffOverride rt_cliff_override_read(void)
{
    RtCliffOverride pack = {0};
    if (!rt_generated_temperate_on && !getenv("OPENCNC3D_CLIFF_OVERRIDE")) return pack;
    const char* path = rt_override_path(
        "OPENCNC3D_CLIFF_OVERRIDE", "temperate_cliffs_rgb.pack",
        "art/generated/temperate_cliffs/approved_style_v1/cliffs_rgb_color_matched.pack",
        "../art/generated/temperate_cliffs/approved_style_v1/cliffs_rgb_color_matched.pack");
    FILE* f;
    long size;
    int i, count = 0;
    if (!path || !*path) return pack;
    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "remaster: cannot open cliff override %s\n", path);
        return pack;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < RT_CLIFF_HEADER_BYTES
        || size > 64L * 1024 * 1024 || fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "remaster: invalid cliff override size: %s\n", path);
        fclose(f);
        return pack;
    }
    pack.len = (size_t)size;
    pack.data = (unsigned char*)malloc(pack.len);
    if (!pack.data || fread(pack.data, 1, pack.len, f) != pack.len) {
        free(pack.data); pack.data = NULL; pack.len = 0;
        fclose(f);
        return pack;
    }
    fclose(f);
    if (memcmp(pack.data, "C3DCLIFF", 8) != 0 || rt_le32(pack.data + 8) != 2) {
        fprintf(stderr, "remaster: invalid cliff override header: %s\n", path);
        free(pack.data); pack.data = NULL; pack.len = 0;
        return pack;
    }
    for (i = 0; i < RT_CLIFF_COUNT * RT_CLIFF_SHAPES; i++) {
        unsigned int off = rt_le32(pack.data + 12 + i * 4);
        if (!off) continue;
        if (off < RT_CLIFF_HEADER_BYTES || (size_t)off > pack.len
            || RT_CLIFF_CELL_BYTES > pack.len - (size_t)off) {
            fprintf(stderr, "remaster: invalid cliff cell offset: %s\n", path);
            free(pack.data); pack.data = NULL; pack.len = 0;
            return pack;
        }
        count++;
    }
    fprintf(stderr, "remaster: loaded %d temperate cliff replacements from %s\n",
            count, path);
    return pack;
}

static const unsigned char* rt_cliff_override_cell(const RtCliffOverride* pack,
                                                   const char* name, int shape)
{
    int template_number;
    unsigned int off;
    if (!pack->data || shape < 0 || shape >= RT_CLIFF_SHAPES
        || name[0] != 'S' || name[1] < '0' || name[1] > '9'
        || name[2] < '0' || name[2] > '9' || name[3]) return NULL;
    template_number = (name[1] - '0') * 10 + name[2] - '0';
    if (template_number < 1 || template_number > RT_CLIFF_COUNT) return NULL;
    off = rt_le32(pack->data + 12
                  + ((template_number - 1) * RT_CLIFF_SHAPES + shape) * 4);
    return off ? pack->data + off : NULL;
}

/* All forty used D01..D43 templates live in one sparse pack. Three XML-only
   templates have no temperate atlas slot and therefore no replacement cell. */
static RtCliffOverride rt_road_override_read(void)
{
    RtCliffOverride pack = {0};
    if (!rt_generated_temperate_on) return pack;
    const char* path = rt_override_path(
        "OPENCNC3D_ROAD_OVERRIDE", "temperate_roads_rgb.pack",
        "art/generated/temperate_roads/approved_style_v1/roads_rgb.pack",
        "../art/generated/temperate_roads/approved_style_v1/roads_rgb.pack");
    if (!path) return pack;
    FILE* f = fopen(path, "rb");
    if (!f) return pack;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return pack; }
    long size = ftell(f);
    if (size < RT_ROAD_HEADER_BYTES || size > 64L*1024*1024
        || fseek(f, 0, SEEK_SET)) { fclose(f); return pack; }
    pack.len = (size_t)size;
    pack.data = (unsigned char*)malloc(pack.len);
    if (!pack.data || fread(pack.data, 1, pack.len, f) != pack.len) {
        fclose(f); free(pack.data); pack.data = NULL; pack.len = 0; return pack;
    }
    fclose(f);
    if (memcmp(pack.data, "C3DROAD0", 8) || rt_le32(pack.data+8) != 2) {
        free(pack.data); pack.data = NULL; pack.len = 0; return pack;
    }
    int count = 0;
    for (int i = 0; i < RT_ROAD_COUNT*RT_ROAD_SHAPES; i++) {
        unsigned int off = rt_le32(pack.data+12+i*4);
        if (!off) continue;
        if (off < RT_ROAD_HEADER_BYTES || (size_t)off > pack.len
            || RT_CLIFF_CELL_BYTES > pack.len-(size_t)off) {
            fprintf(stderr, "remaster: invalid road cell offset: %s\n", path);
            free(pack.data); pack.data = NULL; pack.len = 0; return pack;
        }
        count++;
    }
    fprintf(stderr, "remaster: loaded %d temperate gravel road replacements from %s\n",
            count, path);
    return pack;
}

static const unsigned char* rt_road_override_cell(const RtCliffOverride* pack,
                                                  const char* name, int icon)
{
    if (!pack->data || icon < 0 || icon >= RT_ROAD_SHAPES
        || name[0] != 'D' || name[1] < '0' || name[1] > '9'
        || name[2] < '0' || name[2] > '9' || name[3]) return NULL;
    int number = (name[1]-'0')*10+name[2]-'0';
    if (number < 1 || number > RT_ROAD_COUNT) return NULL;
    unsigned int off = rt_le32(pack->data+12
                               +((number-1)*RT_ROAD_SHAPES+icon)*4);
    return off ? pack->data+off : NULL;
}

/* The shore pack is indexed by Temperate atlas slot. The source sheets follow
   the Remastered templates; only the cartridge's alpha cutout stays intact. */
static RtCliffOverride rt_shore_override_read(void)
{
    RtCliffOverride pack = {0};
    if (!rt_generated_temperate_on) return pack;
    const char* path = rt_override_path(
        "OPENCNC3D_SHORE_OVERRIDE", "temperate_shores_rgb.pack",
        "art/generated/temperate_shores/remastered_style_v1/shores_rgb.pack",
        "../art/generated/temperate_shores/remastered_style_v1/shores_rgb.pack");
    if (!path) return pack;
    FILE* f = fopen(path, "rb");
    if (!f) return pack;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return pack; }
    long size = ftell(f);
    if (size < RT_SHORE_HEADER_BYTES || size > 64L*1024*1024
        || fseek(f, 0, SEEK_SET)) { fclose(f); return pack; }
    pack.len = (size_t)size;
    pack.data = (unsigned char*)malloc(pack.len);
    if (!pack.data || fread(pack.data, 1, pack.len, f) != pack.len) {
        fclose(f); free(pack.data); pack.data = NULL; pack.len = 0; return pack;
    }
    fclose(f);
    if (memcmp(pack.data, "C3DSHOR0", 8) || rt_le32(pack.data+8) != 2) {
        free(pack.data); pack.data = NULL; pack.len = 0; return pack;
    }
    int count = 0;
    for (int i = 0; i < TT_TEMPERAT_SLOTS; i++) {
        unsigned int off = rt_le32(pack.data+12+i*4);
        if (!off) continue;
        if (off < RT_SHORE_HEADER_BYTES || (size_t)off > pack.len
            || RT_CLIFF_CELL_BYTES > pack.len-(size_t)off) {
            fprintf(stderr, "remaster: invalid shore cell offset: %s\n", path);
            free(pack.data); pack.data = NULL; pack.len = 0; return pack;
        }
        count++;
    }
    fprintf(stderr, "remaster: loaded %d temperate shore replacements from %s\n",
            count, path);
    return pack;
}

static const unsigned char* rt_shore_override_cell(const RtCliffOverride* pack, int slot)
{
    if (!pack->data || slot < 0 || slot >= TT_TEMPERAT_SLOTS) return NULL;
    unsigned int off = rt_le32(pack->data+12+slot*4);
    return off ? pack->data+off : NULL;
}

/* Bridge art is indexed by the same Temperate atlas slots as shore art, so
   the runtime keeps the cartridge's water alpha and map placement intact. */
static RtCliffOverride rt_bridge_override_read(void)
{
    RtCliffOverride pack = {0};
    if (!rt_generated_temperate_on) return pack;
    const char* path = rt_override_path(
        "OPENCNC3D_BRIDGE_OVERRIDE", "temperate_bridges_rgb.pack",
        "art/generated/temperate_bridges/remastered_style_v1/bridges_rgb.pack",
        "../art/generated/temperate_bridges/remastered_style_v1/bridges_rgb.pack");
    if (!path) return pack;
    FILE* f = fopen(path, "rb");
    if (!f) return pack;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return pack; }
    long size = ftell(f);
    if (size < RT_SHORE_HEADER_BYTES || size > 64L*1024*1024
        || fseek(f, 0, SEEK_SET)) { fclose(f); return pack; }
    pack.len = (size_t)size;
    pack.data = (unsigned char*)malloc(pack.len);
    if (!pack.data || fread(pack.data, 1, pack.len, f) != pack.len) {
        fclose(f); free(pack.data); pack.data = NULL; pack.len = 0; return pack;
    }
    fclose(f);
    if (memcmp(pack.data, "C3DBRID0", 8) || rt_le32(pack.data+8) != 2) {
        free(pack.data); pack.data = NULL; pack.len = 0; return pack;
    }
    int count = 0;
    for (int i = 0; i < TT_TEMPERAT_SLOTS; i++) {
        unsigned int off = rt_le32(pack.data+12+i*4);
        if (!off) continue;
        if (off < RT_SHORE_HEADER_BYTES || (size_t)off > pack.len
            || RT_CLIFF_CELL_BYTES > pack.len-(size_t)off) {
            fprintf(stderr, "remaster: invalid bridge cell offset: %s\n", path);
            free(pack.data); pack.data = NULL; pack.len = 0; return pack;
        }
        count++;
    }
    fprintf(stderr, "remaster: loaded %d temperate bridge replacements from %s\n",
            count, path);
    return pack;
}

/* Remastered-led river colour is indexed by atlas slot. The cartridge's
   per-pixel water cutout remains the placement and alpha authority. */
static RtCliffOverride rt_river_override_read(void)
{
    RtCliffOverride pack = {0};
    if (!rt_generated_temperate_on) return pack;
    const char* path = rt_override_path(
        "OPENCNC3D_RIVER_OVERRIDE", "temperate_rivers_rgb.pack",
        "art/generated/temperate_rivers/remastered_style_v1/rivers_rgb.pack",
        "../art/generated/temperate_rivers/remastered_style_v1/rivers_rgb.pack");
    if (!path) return pack;
    FILE* f = fopen(path, "rb");
    if (!f) return pack;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return pack; }
    long size = ftell(f);
    if (size < RT_SHORE_HEADER_BYTES || size > 64L*1024*1024
        || fseek(f, 0, SEEK_SET)) { fclose(f); return pack; }
    pack.len = (size_t)size;
    pack.data = (unsigned char*)malloc(pack.len);
    if (!pack.data || fread(pack.data, 1, pack.len, f) != pack.len) {
        fclose(f); free(pack.data); pack.data = NULL; pack.len = 0; return pack;
    }
    fclose(f);
    if (memcmp(pack.data, "C3DRIVR0", 8) || rt_le32(pack.data+8) != 2) {
        free(pack.data); pack.data = NULL; pack.len = 0; return pack;
    }
    int count = 0;
    for (int i = 0; i < TT_TEMPERAT_SLOTS; i++) {
        unsigned int off = rt_le32(pack.data+12+i*4);
        if (!off) continue;
        if (off < RT_SHORE_HEADER_BYTES || (size_t)off > pack.len
            || RT_CLIFF_CELL_BYTES > pack.len-(size_t)off) {
            fprintf(stderr, "remaster: invalid river cell offset: %s\n", path);
            free(pack.data); pack.data = NULL; pack.len = 0; return pack;
        }
        count++;
    }
    fprintf(stderr, "remaster: loaded %d temperate river replacements from %s\n",
            count, path);
    return pack;
}

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
    /* The Enhanced sheet's VARIANTS (rt_build_atlas_enh): RT_VAR_INTS ints each, the bank
       slot, the variant (1 or 2), the template's width and height in cells, and the slot
       index the variant was placed at, in rows below the used height (uh). */
    int nvar;
    int* var;
} RtAtlas;
#define RT_VAR_INTS 5

RT_MAYBE_UNUSED static void rt_atlas_free(RtAtlas* a)
{
    if (!a) return;
    free(a->rgba);
    free(a->var);
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
    unsigned char* grass_override = NULL;
    RtCliffOverride road_override = {0};
    RtCliffOverride cliff_override = {0};
    RtCliffOverride shore_override = {0};
    RtCliffOverride bridge_override = {0};
    RtCliffOverride river_override = {0};
    const int k = rt_generated_temperate_on && !strcmp(theater, "TEMPERAT")
                ? rt_detail_scale : RT_K_BASE;
    const int ts = TT_TS * k, gutter = TT_GUTTER * k, pitch = TT_PITCH * k;

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

    out->uw = TT_COLS * pitch;
    out->uh = ((nslots + TT_COLS - 1) / TT_COLS) * pitch;
    out->w = rt_pot(out->uw); out->h = rt_pot(out->uh);
    out->rgba = (unsigned char*)calloc((size_t)out->w * out->h, 4);
    src   = (unsigned char*)malloc(RT_CLIFF_CELL_BYTES);
    small = (unsigned char*)malloc((size_t)ts * ts * 3);
    alpha = (unsigned char*)malloc((size_t)ts * ts);
    if (!out->rgba || !src || !small || !alpha) goto done;
    if (!strcmp(theater, "TEMPERAT")) {
        grass_override = rt_grass_override_read();
        cliff_override = rt_cliff_override_read();
        road_override = rt_road_override_read();
        shore_override = rt_shore_override_read();
        bridge_override = rt_bridge_override_read();
        river_override = rt_river_override_read();
    }

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
            const unsigned char* cliff_cell;

            if (!rt_tag(&p, end, "Shape", shape, sizeof shape)) break;
            if (!rt_tag(&p, end, "Frame", frame, sizeof frame)) { p = q; continue; }
            if (!frame[0]) continue;             /* an empty Frame is a real blank cell */
            icon = atoi(shape);
            tid = rt_template_id(name);
            if (tid < 0) continue;               /* a template our engine does not have */
            slot = tt_slot_of(theater, tid, icon, &sea);
            if (slot < 0 || slot >= nslots) continue;   /* not in the cartridge's bank */

            cliff_cell = rt_cliff_override_cell(&cliff_override, name, icon);
            if (cliff_cell) {
                memcpy(src, cliff_cell, RT_CLIFF_CELL_BYTES);
                sw = sh = RT_OVERRIDE_SRC;
            } else if (const unsigned char* shore_cell =
                           rt_shore_override_cell(&shore_override, slot)) {
                memcpy(src, shore_cell, RT_CLIFF_CELL_BYTES);
                sw = sh = RT_OVERRIDE_SRC;
            } else if (const unsigned char* bridge_cell =
                           rt_shore_override_cell(&bridge_override, slot)) {
                memcpy(src, bridge_cell, RT_CLIFF_CELL_BYTES);
                sw = sh = RT_OVERRIDE_SRC;
            } else if (const unsigned char* river_cell =
                           rt_shore_override_cell(&river_override, slot)) {
                memcpy(src, river_cell, RT_CLIFF_CELL_BYTES);
                sw = sh = RT_OVERRIDE_SRC;
            } else if (const unsigned char* road_cell =
                           rt_road_override_cell(&road_override, name, icon)) {
                memcpy(src, road_cell, RT_CLIFF_CELL_BYTES);
                sw = sh = RT_OVERRIDE_SRC;
            } else if (grass_override && !strcasecmp(name, "CLEAR1")
                && icon >= 0 && icon < RT_GRASS_ICONS) {
                memcpy(src, grass_override + (size_t)icon * RT_CLIFF_CELL_BYTES,
                       RT_CLIFF_CELL_BYTES);
                sw = sh = RT_OVERRIDE_SRC;
            } else {
                snprintf(path, sizeof path, "DATA\\ART\\TEXTURES\\SRGB\\%s\\%s",
                         rootpath, frame);
                {   /* the XML names .tga; what ships is .DDS */
                    size_t l = strlen(path);
                    if (l > 4 && !strcasecmp(path + l - 4, ".TGA"))
                        memcpy(path + l - 4, ".DDS", 4);
                }
                for (i = 0; i < ntex && !rec; i++) rec = rt_meg_find(&tex[i], path);
                if (!rec) {
                    if (getenv("OPENCNC3D_RT_DEBUG") && out->missing < 8)
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
            }
            rt_box_rgb(src, sw, sh, small, ts, ts);

            col = slot % TT_COLS; row = slot / TT_COLS;
            gx = col * TT_PITCH + TT_GUTTER;                /* the CARTRIDGE rect, for alpha */
            gy = row * TT_PITCH + TT_GUTTER;
            if (gx + TT_TS > caw || gy + TT_TS > cah) { out->missing++; continue; }
            rt_alpha_up(cart, caw, gx, gy, alpha, ts);

            x0 = col * pitch + gutter;
            y0 = row * pitch + gutter;
            for (yy = 0; yy < ts; yy++) {
                unsigned char* d = out->rgba + ((size_t)(y0 + yy) * out->w + x0) * 4;
                const unsigned char* s = small + (size_t)yy * ts * 3;
                const unsigned char* a = alpha + (size_t)yy * ts;
                int xx;
                for (xx = 0; xx < ts; xx++) {
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
            for (yy = 0; yy < gutter; yy++) {
                memcpy(out->rgba + ((size_t)(y0 - 1 - yy) * out->w + x0) * 4,
                       out->rgba + ((size_t)y0 * out->w + x0) * 4, (size_t)ts * 4);
                memcpy(out->rgba + ((size_t)(y0 + ts + yy) * out->w + x0) * 4,
                       out->rgba + ((size_t)(y0 + ts - 1) * out->w + x0) * 4,
                       (size_t)ts * 4);
            }
            for (yy = -gutter; yy < ts + gutter; yy++) {
                unsigned char* rowp = out->rgba + ((size_t)(y0 + yy) * out->w) * 4;
                int g;
                for (g = 1; g <= gutter; g++) {
                    memcpy(rowp + (size_t)(x0 - g) * 4, rowp + (size_t)x0 * 4, 4);
                    memcpy(rowp + (size_t)(x0 + ts - 1 + g) * 4,
                           rowp + (size_t)(x0 + ts - 1) * 4, 4);
                }
            }
            out->placed++;
        }
    }
    ok = (out->placed > 0);
    fprintf(stderr, "remaster: %s atlas %dx%d (used %dx%d), %d tiles placed, %d missing\n",
            theater, out->w, out->h, out->uw, out->uh, out->placed, out->missing);

done:
    free(src); free(small); free(alpha); free(grass_override);
    free(road_override.data); free(cliff_override.data); free(shore_override.data);
    free(bridge_override.data); free(river_override.data); free(xml);
    rt_meg_close(&cfg);
    for (i = 0; i < ntex; i++) rt_meg_close(&tex[i]);
    if (!ok) rt_atlas_free(out);
    return ok;
}

/* ---- THE ENHANCED TEXTURES ----------------------------------------------------------
 *
 * The 1995 tiles remastered at 30 texels a cell (tools/art/remaster/textures), shipped as
 * one pack per theater (tools/bakery/pack_remaster_terrain.py) and built into the same
 * sheet the Remastered art uses: tile 24k on a 26k pitch at k = 5, so every cell's UVs
 * address it unchanged. 30 is a quarter of 120, and the cell goes in by bilinear
 * interpolation, which is how the 30-texel texture would look sampled bilinearly itself.
 *
 * A bank slot the pack has no cell for (a template with no art in that theater's 1995
 * MIX, or one left blank) takes the 1995 cell from the DOS atlas, interpolated the same
 * way, so the sheet has no holes. Alpha is the cartridge's, as for the Remastered art:
 * the renderer cuts open water out of the ground and draws its own sea. */

#define RT_ENH_MAGIC "C3DT30\0\0"
/* The pack says its own cell size. A whole multiple of the 24-texel cell (144 = 24 x 6)
   goes into a sheet of that k one for one; any other size (the shipped 30) is
   interpolated into the k = 5 sheet. RT_ENH_KMAX caps the sheet: k = 10 is 240 a cell. */
#define RT_ENH_KMAX  10

/* Texel centres to texel centres, clamped at the edges: a cell sampled on its own, as
   the gutter makes the GPU sample it. */
static void rt_bilinear_rgb(const unsigned char* src, int sw, int sh, int sstride,
                            unsigned char* dst, int dw, int dh)
{
    int y, x, c;
    for (y = 0; y < dh; y++) {
        const float fy = ((float)y + 0.5f) * (float)sh / (float)dh - 0.5f;
        const int y0 = fy < 0 ? 0 : (int)fy, y1 = y0 + 1 < sh ? y0 + 1 : sh - 1;
        const float wy = fy < 0 ? 0.0f : fy - (float)y0;
        for (x = 0; x < dw; x++) {
            const float fx = ((float)x + 0.5f) * (float)sw / (float)dw - 0.5f;
            const int x0 = fx < 0 ? 0 : (int)fx, x1 = x0 + 1 < sw ? x0 + 1 : sw - 1;
            const float wx = fx < 0 ? 0.0f : fx - (float)x0;
            for (c = 0; c < 3; c++) {
                const float a = src[(size_t)y0 * sstride + x0 * 3 + c], b = src[(size_t)y0 * sstride + x1 * 3 + c];
                const float d = src[(size_t)y1 * sstride + x0 * 3 + c], e = src[(size_t)y1 * sstride + x1 * 3 + c];
                const float top = a + (b - a) * wx, bot = d + (e - d) * wx;
                dst[((size_t)y * dw + x) * 3 + c] = (unsigned char)(top + (bot - top) * wy + 0.5f);
            }
        }
    }
}

/* One slot's picture and alpha into the sheet, with its gutter replicated outward. */
static void rt_place(RtAtlas* out, int slot, int ts, int gutter, int pitch,
                     const unsigned char* rgb, const unsigned char* alpha)
{
    const int col = slot % TT_COLS, row = slot / TT_COLS;
    const int x0 = col * pitch + gutter, y0 = row * pitch + gutter;
    int yy, xx, g;
    for (yy = 0; yy < ts; yy++) {
        unsigned char* d = out->rgba + ((size_t)(y0 + yy) * out->w + x0) * 4;
        for (xx = 0; xx < ts; xx++) {
            d[xx * 4 + 0] = rgb[((size_t)yy * ts + xx) * 3 + 0];
            d[xx * 4 + 1] = rgb[((size_t)yy * ts + xx) * 3 + 1];
            d[xx * 4 + 2] = rgb[((size_t)yy * ts + xx) * 3 + 2];
            d[xx * 4 + 3] = alpha[(size_t)yy * ts + xx];
        }
    }
    for (yy = 0; yy < gutter; yy++) {
        memcpy(out->rgba + ((size_t)(y0 - 1 - yy) * out->w + x0) * 4,
               out->rgba + ((size_t)y0 * out->w + x0) * 4, (size_t)ts * 4);
        memcpy(out->rgba + ((size_t)(y0 + ts + yy) * out->w + x0) * 4,
               out->rgba + ((size_t)(y0 + ts - 1) * out->w + x0) * 4, (size_t)ts * 4);
    }
    for (yy = -gutter; yy < ts + gutter; yy++) {
        unsigned char* rowp = out->rgba + ((size_t)(y0 + yy) * out->w) * 4;
        for (g = 1; g <= gutter; g++) {
            memcpy(rowp + (size_t)(x0 - g) * 4, rowp + (size_t)x0 * 4, 4);
            memcpy(rowp + (size_t)(x0 + ts - 1 + g) * 4, rowp + (size_t)(x0 + ts - 1) * 4, 4);
        }
    }
}

/* Where the theater's pack is, written into buf, or NULL. OPENCNC3D_TERRAINENH (or the older
   OPENCNC3D_TERRAIN30) names one outright, or turns it "off"; otherwise terrainenh_<TH>.pack,
   the set the game ships, beside the other packs in the working directory, else in the
   repository (a repo run), else in the Red Alert module's folder (ra3d/); and only then
   terrain30_<TH>.pack, the first set at 30 texels a cell, for a folder that has not been
   rebuilt since. */
static const char* rt_enh_path(const char* theater, char* buf, size_t n)
{
    static const char* const names[2] = { "terrainenh", "terrain30" };
    const char* env = getenv("OPENCNC3D_TERRAINENH");
    int k, w;
    if (!env || !*env) env = getenv("OPENCNC3D_TERRAIN30");
    if (env && (!strcmp(env, "off") || !strcmp(env, "none"))) return NULL;
    if (env && *env) return env;
    for (k = 0; k < 2; k++)
        for (w = 0; w < 4; w++) {
            if (w == 0) snprintf(buf, n, "%s_%s.pack", names[k], theater);
            /* a Red Alert theater's set ships inside the Red Alert module's own folder */
            else if (w == 3) snprintf(buf, n, "ra3d/%s_%s.pack", names[k], theater);
            else snprintf(buf, n, "%sart/remaster/textures/%s/%s_%s.pack",
                          w == 2 ? "../" : "", theater, names[k], theater);
            if (rt_override_exists(buf)) return buf;
        }
    return NULL;
}

/* The theater's pack, whole, or NULL (rt_enh_path finds it). */
static unsigned char* rt_enh_read(const char* theater, size_t* len, int* cell)
{
    char where[192];
    const char* path;
    FILE* f;
    long size;
    unsigned char* data;
    path = rt_enh_path(theater, where, sizeof where);
    *len = 0;
    if (!path) return NULL;
    f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 20 || size > 160L * 1024 * 1024
        || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    data = (unsigned char*)malloc((size_t)size);
    if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) { free(data); fclose(f); return NULL; }
    fclose(f);
    *cell = (int)rt_le32(data + 12);
    if (memcmp(data, RT_ENH_MAGIC, 8) != 0 || rt_le32(data + 8) != 1 || *cell < TT_TS || *cell > TT_TS * RT_ENH_KMAX) {
        fprintf(stderr, "enhanced terrain: %s is not a version 1 pack of 24 to %d texels a cell\n",
                path, TT_TS * RT_ENH_KMAX);
        free(data);
        return NULL;
    }
    {
        const size_t n = rt_le32(data + 16);
        if ((size_t)size != 20 + n * 4 + n * (size_t)*cell * *cell * 3) {
            fprintf(stderr, "enhanced terrain: %s is truncated or padded\n", path);
            free(data);
            return NULL;
        }
    }
    fprintf(stderr, "enhanced terrain: %s, %d texels a cell\n", path, *cell);
    *len = (size_t)size;
    return data;
}

/* Whether a pack exists for the theater: the Visuals dialog greys the entry otherwise. */
RT_MAYBE_UNUSED static int rt_enh_available(const char* theater)
{
    char where[192];
    if (!theater || !theater[0]) return 0;
    return rt_enh_path(theater, where, sizeof where) != NULL;
}

/* Build the theater's Enhanced sheet. cart and dos are the cartridge and DOS atlases as
   the pack carries them (same packing): alpha from the first, the fallback cells from the
   second. Returns 1 on success. */
RT_MAYBE_UNUSED static int rt_build_atlas_enh(const char* theater,
                                              const unsigned char* cart, int caw, int cah,
                                              const unsigned char* dos, int daw, int dah,
                                              RtAtlas* out)
{
    size_t len = 0, n, i;
    int cell = 0;
    unsigned char* pack = rt_enh_read(theater, &len, &cell);
    const int k = (cell % TT_TS == 0) ? cell / TT_TS : RT_K_BASE;
    const int ts = TT_TS * k, gutter = TT_GUTTER * k, pitch = TT_PITCH * k;
    unsigned char *rgb = NULL, *alpha = NULL, *dosrgb = NULL;
    const TtSlot* tab;
    int nslots = 0, s, fallback = 0;
    const unsigned char** bycell = NULL;
    memset(out, 0, sizeof *out);
    if (!pack) return 0;
    tab = tt_table(theater, &nslots);
    n = rt_le32(pack + 16);
    if (nslots <= 0) { free(pack); return 0; }

    out->uw = TT_COLS * pitch;
    out->uh = ((nslots + TT_COLS - 1) / TT_COLS) * pitch;
    /* THE VARIANTS: a cell whose icon field carries a variant number above its own eight
       bits (tools/bakery/pack_remaster_terrain.py) is another painting of the same piece.
       They go in rows of their own BELOW the used height, which stays the bank's: every
       cell's texture coordinates still address the first painting unchanged, and the game
       moves a cell to a variant by the variant's offset in the sheet. */
    for (i = 0; i < n; i++) {
        const unsigned char* key = pack + 20 + i * 4;
        const int icon = key[2] | (key[3] << 8);
        if (icon >= 256 && tt_slot_of(theater, key[0] | (key[1] << 8), icon & 255, NULL) >= 0) out->nvar++;
    }
    if (out->nvar > 0) {
        out->var = (int*)calloc((size_t)out->nvar, sizeof(int) * RT_VAR_INTS);
        if (!out->var) goto fail;
    }
    /* THE SHEET AT ITS OWN SIZE, not padded to a power of two. At 144 texels a cell the
       Desert's and Winter's sheets are 4992 x 4836 and 4992 x 4212; padded, both are
       8192 x 8192, 256 MB in memory twice over (built, then uploaded), against 97 MB.
       Non-power-of-two textures are core since GL 2.0, and this sheet is Tier 2 only: it
       has no mipmaps, and every texture coordinate is already scaled by uw / w. */
    out->w = out->uw;
    out->h = out->uh + ((out->nvar + TT_COLS - 1) / TT_COLS) * pitch;
    out->rgba = (unsigned char*)calloc((size_t)out->w * out->h, 4);
    rgb = (unsigned char*)malloc((size_t)ts * ts * 3);
    alpha = (unsigned char*)malloc((size_t)ts * ts);
    dosrgb = (unsigned char*)malloc((size_t)TT_TS * TT_TS * 3);
    bycell = (const unsigned char**)calloc((size_t)nslots, sizeof *bycell);
    if (!out->rgba || !rgb || !alpha || !dosrgb || !bycell) goto fail;

    /* each pack cell to its bank slot, and each variant to a slot of its own below */
    {
        const int base = (out->uh / pitch) * TT_COLS;
        int e = 0;
        for (i = 0; i < n; i++) {
            const unsigned char* key = pack + 20 + i * 4;
            const int tmpl = key[0] | (key[1] << 8), icon = key[2] | (key[3] << 8);
            const int slot = tt_slot_of(theater, tmpl, icon & 255, NULL);
            const unsigned char* px = pack + 20 + n * 4 + i * (size_t)cell * cell * 3;
            if (slot < 0 || slot >= nslots) continue;
            if (icon < 256) { bycell[slot] = px; continue; }
            if (e >= out->nvar) continue;
            {
                int* v = out->var + (size_t)e * RT_VAR_INTS;
                const int gx = (slot % TT_COLS) * TT_PITCH + TT_GUTTER, gy = (slot / TT_COLS) * TT_PITCH + TT_GUTTER;
                v[0] = slot; v[1] = (icon >> 8) & 3; v[2] = ((icon >> 10) & 7) + 1; v[3] = ((icon >> 13) & 7) + 1;
                v[4] = base + e;
                e++;
                if (gx + TT_TS > caw || gy + TT_TS > cah) { v[1] = 0; continue; }
                rt_bilinear_rgb(px, cell, cell, cell * 3, rgb, ts, ts);
                rt_alpha_up(cart, caw, gx, gy, alpha, ts);   /* the first painting's water cut */
                rt_place(out, v[4], ts, gutter, pitch, rgb, alpha);
            }
        }
    }
    for (s = 0; s < nslots; s++) {
        const int slot = tab[s].slot;
        const int gx = (slot % TT_COLS) * TT_PITCH + TT_GUTTER, gy = (slot / TT_COLS) * TT_PITCH + TT_GUTTER;
        if (slot < 0 || slot >= nslots || gx + TT_TS > caw || gy + TT_TS > cah) continue;
        if (bycell[slot]) {
            rt_bilinear_rgb(bycell[slot], cell, cell, cell * 3, rgb, ts, ts);   /* 1:1 when cell == ts */
        } else if (dos && gx + TT_TS <= daw && gy + TT_TS <= dah) {
            int y, x;
            for (y = 0; y < TT_TS; y++)
                for (x = 0; x < TT_TS; x++)
                    memcpy(dosrgb + ((size_t)y * TT_TS + x) * 3,
                           dos + ((size_t)(gy + y) * daw + gx + x) * 4, 3);
            rt_bilinear_rgb(dosrgb, TT_TS, TT_TS, TT_TS * 3, rgb, ts, ts);
            fallback++;
        } else {
            out->missing++;
            continue;
        }
        rt_alpha_up(cart, caw, gx, gy, alpha, ts);
        rt_place(out, slot, ts, gutter, pitch, rgb, alpha);
        out->placed++;
    }
    fprintf(stderr, "enhanced terrain: %s atlas %dx%d (used %dx%d), %d slots, %d from the pack, "
                    "%d from the 1995 art, %d missing, %d variant cells\n", theater, out->w, out->h,
            out->uw, out->uh, nslots, out->placed - fallback, fallback, out->missing, out->nvar);
    free(pack); free(rgb); free(alpha); free(dosrgb); free(bycell);
    if (out->placed > 0) return 1;
    rt_atlas_free(out);
    return 0;
fail:
    free(pack); free(rgb); free(alpha); free(dosrgb); free(bycell);
    rt_atlas_free(out);
    return 0;
}

#endif /* OPENCNC3D_REMASTER_TEX_H */
