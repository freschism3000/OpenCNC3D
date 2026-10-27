/* 320x200 MS-DOS Classic scene. The terrain and object art are both flat pixel
 * surfaces. Object poses and draw order come from the 1995 engine's layer export. */
#ifndef OPENCNC3D_DOSCLASSIC_MOD_H
#define OPENCNC3D_DOSCLASSIC_MOD_H

#include <unordered_map>

struct Dos2DShape {
    char name[16];
    int w, h, frames, cols, tw, th;
    std::vector<unsigned char> zipped;
    std::vector<unsigned char> raw;  /* the palette indices, unpacked on first use by dos2d_shimmer */
    GLuint tex[144]; /* normal/ghost/flash/shadow/translucent variants with player remaps */
    Dos2DShape() : w(0), h(0), frames(0), cols(0), tw(0), th(0) { name[0] = 0; memset(tex,0,sizeof tex); }
};

static std::vector<Dos2DShape> g_dos2dShapes;
static int g_dos2dLoad = 0;
static int g_dos2dMissing = 0;

/* Every aircraft layer the last Classic frame drew (body, shadow, rotor), as the
 * rectangle of its SHP in framebuffer pixels from the top left, with the engine's draw
 * flags and whether it was drawn as a shadow. Script verb dosairprobe prints it;
 * dosshadows 0 leaves out every layer the ENGINE flags SHAPE_FADING | SHAPE_PREDATOR,
 * whatever this renderer made of it, which is a gate's control leg. */
struct Dos2DAirRec { char asset[16]; int id, sub, flags, shadow, drawn; float x0, y0, x1, y1; };
static std::vector<Dos2DAirRec> g_dos2dAirRecs;
static int g_dos2dShadowsOff = 0;
/* Every animation the last Classic frame drew, with the table it went through (ghost:
 * UnitShadow or MOVEFLSH's white table; trans: TranslucentTable). dosanimprobe prints it. */
struct Dos2DAnimRec { char name[16]; int frame, ghost, trans; float x0, y0, x1, y1; };
static std::vector<Dos2DAnimRec> g_dos2dAnimRecs;

/* Every vehicle and vessel layer the last Classic frame met inside the view, with the
 * engine's cloak state and draw flags, the 1995 visual character worked out from them
 * (dos2d_visual) and what this renderer drew for it: the sprite, the darkening, the
 * shimmer, or nothing (hidden, invisible to this player, or left out by the control).
 * Script verb doscloakprobe prints it; dosomit TYPE leaves every layer of that type out
 * of the frame, so a gate finds what was drawn for it by difference. */
struct Dos2DCloakRec {
    char type[16];
    int id, sub, owner, ours, cloak, cstage, visual, flags;
    const char* as;
    float x0, y0, x1, y1;
};
static std::vector<Dos2DCloakRec> g_dos2dCloakRecs;
static char g_dos2dOmit[16] = "";
static GLuint g_dos2dShimmerTex = 0;
static unsigned char g_dos2dRemap[8][16];
static unsigned char g_dos2dBase[16];
/* Red Alert: WINTER, TEMPERAT, INTERIOR. Tiberian Dawn: TEMPERAT, DESERT, and Temperate
   again for WINTER, whose palette is byte-identical to it. */
static unsigned char g_dos2dPalette[3][768];
static int g_dos2dTdPal = 0;          /* Tiberian Dawn's pack carried its theater palettes */
static const unsigned char* g_dos2dTexPal = NULL; /* the palette the cached textures used */
static char g_dos2dTexShade[16] = "";             /* and the shadow blend's row */

struct RaClassicTile {
    char theater;
    char name[16];
    unsigned short icon;
    unsigned char pixels[576];
    GLuint texture;
    RaClassicTile() : theater(0), icon(0), texture(0) { name[0] = 0; }
};
static std::vector<RaClassicTile> g_raTiles;
static std::unordered_map<std::string, size_t> g_raTileIndex;
static unsigned char g_raTilePal[3][768];
static CNCMapDataStruct g_raStaticMap;
static std::string g_raStaticScene;
static int g_raTerrainLoad = 0;
static int g_raStaticReady = 0;
static int g_raLayersReported = 0;

/* The shared engine ABI has no Interior enum, so the Red Alert brain exports
   that third original theater as NONE. It is unambiguous for RA missions. */
static int ra2d_pal_index(void)
{
    return g_raStaticMap.Theater == CNC_THEATER_WINTER ? 0
         : g_raStaticMap.Theater == CNC_THEATER_NONE ? 2 : 1;
}

static char ra2d_theater_letter(void)
{
    return ra2d_pal_index() == 0 ? 'S' : ra2d_pal_index() == 2 ? 'I' : 'T';
}

/* The palette every Classic sprite is decoded through. In 1995 it is GamePalette, which
 * DisplayClass::Init_Theater loads from the theater's own <THEATER>.PAL, and 97 entries
 * differ between Tiberian Dawn's Temperate and Desert (48..111, 171, 172, 224..254):
 * Temperate's 224..239 are blues and lavenders, Desert's are browns, so a Desert building
 * decoded through Temperate's palette comes out speckled blue. The sidebar's palette is
 * the fallback for a pack older than the theater palettes. */
static const unsigned char* dos2d_palette(void)
{
    if (g_dos2dRa) return g_dos2dPalette[ra2d_pal_index()];
    if (g_dos2dTdPal) return g_dos2dPalette[!strcmp(g_pack.theater, "DESERT") ? 1 : 0];
    return g_dbPack ? g_dbPack->pal8 : g_dosPal8;
}

static void dos2d_quad(float x0, float y0, float x1, float y1,
                       float u0, float v0, float u1, float v1);

static std::string ra2d_key(char theater, const char* name, int icon)
{
    return std::string(1, theater) + "/" + name + "/" + std::to_string(icon);
}

static bool ra2d_load_terrain(void)
{
    if (g_raTerrainLoad) return g_raTerrainLoad > 0;
    g_raTerrainLoad = -1;
    FILE* file = fopen("ra3d/terrain.pack", "rb");
    if (!file) { fprintf(stderr, "RA2D|missing ra3d/terrain.pack\n"); return false; }
    char magic[8]; uint32_t count;
    if (fread(magic, 1, 8, file) != 8 || memcmp(magic, "RA2DT002", 8) ||
        fread(&count, 4, 1, file) != 1 || count > 10000 ||
        fread(g_raTilePal, 1, sizeof g_raTilePal, file) != sizeof g_raTilePal) {
        fclose(file); return false;
    }
    g_raTiles.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        RaClassicTile tile;
        if (fread(&tile.theater, 1, 1, file) != 1 ||
            fread(tile.name, 1, 16, file) != 16 ||
            fread(&tile.icon, 2, 1, file) != 1 ||
            fread(tile.pixels, 1, 576, file) != 576) break;
        tile.name[15] = 0;
        g_raTileIndex[ra2d_key(tile.theater, tile.name, tile.icon)] = g_raTiles.size();
        g_raTiles.push_back(tile);
    }
    fclose(file);
    if (g_raTiles.size() != count) {
        fprintf(stderr, "RA2D|truncated terrain pack\n");
        g_raTiles.clear(); g_raTileIndex.clear(); return false;
    }
    g_raTerrainLoad = 1;
    fprintf(stderr, "RA2D|loaded %u original terrain icons\n", count);
    return true;
}

static bool ra2d_fetch_map(void)
{
    if (g_raStaticReady && g_raStaticScene == g_bootScen) return true;
    if (!BrainGetState) return false;
    memset(&g_raStaticMap, 0, sizeof g_raStaticMap);
    if (!BrainGetState(GAME_STATE_STATIC_MAP, 0,
                       (unsigned char*)&g_raStaticMap, sizeof g_raStaticMap)) return false;
    if (g_raStaticMap.MapCellWidth <= 0 || g_raStaticMap.MapCellHeight <= 0 ||
        g_raStaticMap.MapCellWidth * g_raStaticMap.MapCellHeight > MAX_EXPORT_CELLS)
        return false;
    g_raStaticScene = g_bootScen;
    g_raStaticReady = 1;
    fprintf(stderr, "RA2D|map %s theater=%d bounds=%d,%d %dx%d\n",
            g_bootScen, (int)g_raStaticMap.Theater,
            g_raStaticMap.MapCellX, g_raStaticMap.MapCellY,
            g_raStaticMap.MapCellWidth, g_raStaticMap.MapCellHeight);
    return true;
}

static RaClassicTile* ra2d_find_tile(char theater, const char* exported, int icon)
{
    char name[17];
    const char* suffix = strstr(exported, "_i");
    size_t len = suffix ? (size_t)(suffix - exported) : strcspn(exported, ".");
    if (len > 16) len = 16;
    memcpy(name, exported, len); name[len] = 0;
    std::unordered_map<std::string, size_t>::const_iterator it =
        g_raTileIndex.find(ra2d_key(theater, name, icon));
    return it == g_raTileIndex.end() ? NULL : &g_raTiles[it->second];
}

static bool ra2d_upload_tile(RaClassicTile* tile)
{
    if (tile->texture) return true;
    unsigned char rgba[32 * 32 * 4] = {0};
    const unsigned char* pal = g_raTilePal[tile->theater == 'S' ? 0
                                       : tile->theater == 'I' ? 2 : 1];
    for (int y = 0; y < 24; y++)
        for (int x = 0; x < 24; x++) {
            const unsigned char ix = tile->pixels[y*24+x];
            unsigned char* out = &rgba[(y*32+x)*4];
            out[0] = pal[ix*3]; out[1] = pal[ix*3+1]; out[2] = pal[ix*3+2];
            out[3] = 255;
        }
    glGenTextures(1, &tile->texture);
    glBindTexture(GL_TEXTURE_2D, tile->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba);
    return true;
}

static void ra2d_draw_terrain(int viewX, int viewY, float ox, float oy,
                             int scale, int viewW, int viewH)
{
    if (!ra2d_load_terrain() || !ra2d_fetch_map()) return;
    const int width = g_raStaticMap.MapCellWidth;
    const int height = g_raStaticMap.MapCellHeight;
    const char theater = ra2d_theater_letter();
    glColor4f(1,1,1,1);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++) {
            const int px = (g_raStaticMap.MapCellX+x)*24-viewX;
            const int py = (g_raStaticMap.MapCellY+y)*24-viewY;
            if (px < -24 || px >= viewW || py < -24 || py >= viewH) continue;
            const CNCStaticCellStruct& cell = g_raStaticMap.StaticCells[y*width+x];
            RaClassicTile* tile = ra2d_find_tile(theater, cell.TemplateTypeName,
                                                cell.IconNumber);
            if (!tile) tile = ra2d_find_tile(theater, "CLEAR1", 0);
            if (!tile || !ra2d_upload_tile(tile)) continue;
            glBindTexture(GL_TEXTURE_2D, tile->texture);
            glBegin(GL_QUADS);
            dos2d_quad(ox+px*scale, oy+py*scale, ox+(px+24)*scale, oy+(py+24)*scale,
                       0, 0, 24.0f/32.0f, 24.0f/32.0f);
            glEnd();
        }
}

static int dos2d_pow2(int n)
{
    int v = 1;
    while (v < n) v <<= 1;
    return v;
}

static bool dos2d_load(void)
{
    if (g_dos2dLoad) return g_dos2dLoad > 0;
    g_dos2dLoad = -1;
    const char* path = g_dos2dRa ? "ra3d/dosclassic.pack" : "dosclassic.pack";
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "DOS2D|missing %s\n", path); return false; }
    char magic[8]; uint32_t count = 0;
    const bool got = fread(magic, 1, 8, f) == 8;
    /* Tiberian Dawn's DOS2D003 is DOS2D002 with the theater palettes after the remaps. */
    const bool tdPal = got && !g_dos2dRa && !memcmp(magic, "DOS2D003", 8);
    if (!got || (memcmp(magic, g_dos2dRa ? "DOS2D005" : "DOS2D002", 8) && !tdPal)
        || fread(&count, 4, 1, f) != 1 || count > 1024
        || (g_dos2dRa && fread(g_dos2dBase, 1, 16, f) != 16)
        || fread(g_dos2dRemap, 1, sizeof g_dos2dRemap, f) != sizeof g_dos2dRemap) { fclose(f); return false; }
    if (g_dos2dRa && fread(g_dos2dPalette, 1, sizeof g_dos2dPalette, f)
                      != sizeof g_dos2dPalette) { fclose(f); return false; }
    if (tdPal) {
        /* TEMPERAT.PAL and DESERT.PAL as the 1995 VGA palettes hold them, six bits a
           channel, widened as the sidebar's palette is (v << 2). */
        unsigned char pal6[2][768];
        if (fread(pal6, 1, sizeof pal6, f) != sizeof pal6) { fclose(f); return false; }
        for (int t = 0; t < 2; t++)
            for (int i = 0; i < 768; i++) g_dos2dPalette[t][i] = (unsigned char)(pal6[t][i] << 2);
        memcpy(g_dos2dPalette[2], g_dos2dPalette[0], 768);
        g_dos2dTdPal = 1;
    }
    if (!g_dos2dRa)
        for (int i = 0; i < 16; i++) g_dos2dBase[i] = (unsigned char)(176+i);
    for (uint32_t i = 0; i < count; i++) {
        Dos2DShape s;
        uint16_t wh[2]; uint32_t nf, bytes;
        if (fread(s.name, 1, 16, f) != 16 || fread(wh, 2, 2, f) != 2
            || fread(&nf, 4, 1, f) != 1 || fread(&bytes, 4, 1, f) != 1
            || !wh[0] || !wh[1] || nf > 1024 || bytes > 20000000) break;
        s.name[15] = 0; s.w = wh[0]; s.h = wh[1]; s.frames = (int)nf;
        s.zipped.resize(bytes);
        if (fread(s.zipped.data(), 1, bytes, f) != bytes) break;
        g_dos2dShapes.push_back(s);
    }
    fclose(f);
    if (g_dos2dShapes.size() != count) {
        fprintf(stderr, "DOS2D|truncated sprite pack\n"); g_dos2dShapes.clear(); return false;
    }
    g_dos2dLoad = 1;
    /* Tiberian Dawn keeps the line its Classic gates read; Red Alert names itself. */
    fprintf(stderr, "DOS2D|loaded %u original %sSHP shapes\n", count,
            g_dos2dRa ? "Red Alert " : "");
    return true;
}

static Dos2DShape* dos2d_shape(const char* name)
{
    for (size_t i = 0; i < g_dos2dShapes.size(); i++)
        if (!strcmp(g_dos2dShapes[i].name, name)) return &g_dos2dShapes[i];
    return NULL;
}

static ScreenPoly dos2d_object_screen_poly(const SimObject& o, int fbw, int fbh)
{
    sb_layout(fbw, fbh);
    ScreenPoly poly;
    dop_reset(&poly);
    char theater_name[20];
    snprintf(theater_name, sizeof theater_name, "%c/%s",
             g_dos2dRa ? ra2d_theater_letter()
                       : g_pack.theater[0], o.type);
    const Dos2DShape* shape = dos2d_shape(theater_name);
    if (!shape) shape = dos2d_shape(o.type);
    const float worldScale = g_dos2dRa ? (float)g_ra95WorldScale : (float)g_dbScale;
    const float width = (float)(shape ? shape->w :
                                o.kind == K_BUILDING ? o.fw*24 : 24) * worldScale;
    const float height = (float)(shape ? shape->h :
                                 o.kind == K_BUILDING ? o.fh*24 : 24) * worldScale;
    float col, row;
    world_to_screen(o.ex, 0.0f, o.ez, fbw, fbh, &col, &row);
    /* DLL_Draw_Intercept and dos2d_draw_shape both centre the SHP at the
       object's render coordinate. Use that rectangle for click and script aims. */
    dop_add(&poly, col-width*0.5f, row-height*0.5f);
    dop_add(&poly, col+width*0.5f, row-height*0.5f);
    dop_add(&poly, col+width*0.5f, row+height*0.5f);
    dop_add(&poly, col-width*0.5f, row+height*0.5f);
    dop_finish(&poly);
    return poly;
}

/* The aircraft shadow. AircraftClass::Draw_It draws the aircraft's own frame again at
 * its ground position with SHAPE_PREDATOR | SHAPE_FADING, and a flying helicopter's rotor
 * takes the same pair. CC_Draw_Shape (conquer.cpp) turns that pair into a ghost draw
 * through Map.SpecialGhost: every pixel under the silhouette becomes the ground behind
 * it, faded 100/256 toward BLACK and matched to the nearest of the palette's last 16
 * entries (Conquer_Build_Fading_Table), which is an olive shade ramp in Temperate and a
 * brown one in Desert. One blend cannot repeat a 16-colour remap, so each row is the
 * blend  ground*(1-a) + colour*a  fitted to that table over the theater's clear ground
 * by tools/dos_aircraft_shadow.py, which also prints the residuals:
 *   TEMPERAT  3.5 levels rms from the table (a plain black fade: 10.3), ground x0.68
 *   DESERT    4.3 levels rms (black: 7.7), ground x0.64
 * Winter's palette and clear ground are byte-identical to Temperate's. Any other
 * theater gets the table's fade before the ramp match, black at 100/256. Red Alert
 * keeps the black 130/255 its Classic was tuned with. */
struct Dos2DShadowBlend { const char* theater; unsigned char r, g, b, a; };

static Dos2DShadowBlend dos2d_shadow_blend(void)
{
    static const Dos2DShadowBlend rows[] = {
        { "TEMPERAT", 36, 24,  0, 164 },
        { "WINTER",   36, 24,  0, 164 },
        { "DESERT",   46, 38,  6, 141 },
    };
    if (g_dos2dRa) { const Dos2DShadowBlend ra = { "RA", 0, 0, 0, 130 }; return ra; }
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++)
        if (!strcmp(g_pack.theater, rows[i].theater)) return rows[i];
    const Dos2DShadowBlend fade = { "", 0, 0, 0, 100 };
    return fade;
}

/* DisplayClass::TranslucentTable, built in Init_Theater from MagicCols and the same in
 * both games: palette 32..39 lay their own colour over the ground at 110/256, while
 * BLACK, WHITE, LTGREY and DKGREY are not colours at all but shades of the ground below,
 * toward black by 200, 40, 80 and 140. Returns the alpha (255 for a pixel the table
 * leaves alone) and sets *shade for the four that draw as black. */
static int dos2d_magic_alpha(unsigned char ix, bool* shade)
{
    *shade = ix == 12 || ix == 15 || ix == 14 || ix == 13;
    if (ix >= 32 && ix <= 39) return 110;
    return ix == 12 ? 200 : ix == 15 ? 40 : ix == 14 ? 80 : ix == 13 ? 140 : 255;
}

static int dos2d_tex_slot(int remap, bool ghost, bool flash, bool shadow, bool trans)
{
    return (remap >= 0 && remap < 8 ? remap+1 : 0) + (ghost ? 9 : 0) + (flash ? 18 : 0)
         + (shadow ? 36 : 0) + (trans ? 72 : 0);
}

/* Drop every cached sprite texture, so the next draw decodes through the palette and
 * shadow colour in force now; a campaign that moves from a Temperate map to a Desert
 * one changes both. */
static void dos2d_flush_textures(void)
{
    for (size_t i = 0; i < g_dos2dShapes.size(); i++) {
        Dos2DShape& s = g_dos2dShapes[i];
        glDeleteTextures((GLsizei)(sizeof s.tex / sizeof s.tex[0]), s.tex);
        memset(s.tex, 0, sizeof s.tex);
    }
    g_dos2dTexPal = NULL;
    g_dos2dTexShade[0] = 0;
}

static bool dos2d_upload(Dos2DShape* s, int remap, bool ghost, bool flash, bool shadow,
                         bool trans)
{
    const int slot = remap >= 0 && remap < 8 ? remap+1 : 0;
    const int texSlot = dos2d_tex_slot(remap, ghost, flash, shadow, trans);
    if (g_dos2dTexPal != dos2d_palette()
        || strcmp(g_dos2dTexShade, dos2d_shadow_blend().theater)) {
        dos2d_flush_textures();
        g_dos2dTexPal = dos2d_palette();
        snprintf(g_dos2dTexShade, sizeof g_dos2dTexShade, "%s", dos2d_shadow_blend().theater);
    }
    if (s->tex[texSlot]) return true;
    const size_t rawbytes = (size_t)s->w * s->h * s->frames;
    std::vector<unsigned char> indices(rawbytes);
    uLongf outbytes = (uLongf)rawbytes;
    if (uncompress(indices.data(), &outbytes, s->zipped.data(), (uLong)s->zipped.size()) != Z_OK
        || outbytes != rawbytes) return false;
    s->cols = 1024 / s->w;
    if (s->cols < 1) s->cols = 1;
    if (s->cols > s->frames) s->cols = s->frames;
    s->tw = dos2d_pow2(s->cols * s->w);
    s->th = dos2d_pow2(((s->frames + s->cols - 1) / s->cols) * s->h);
    std::vector<unsigned char> rgba((size_t)s->tw * s->th * 4, 0);
    const unsigned char* pal = dos2d_palette();
    for (int fr = 0; fr < s->frames; fr++) {
        const int sx = (fr % s->cols) * s->w, sy = (fr / s->cols) * s->h;
        for (int y = 0; y < s->h; y++)
            for (int x = 0; x < s->w; x++) {
                const unsigned char ix = indices[((size_t)fr * s->h + y) * s->w + x];
                unsigned char colour = ix;
                if (slot)
                    for (int band = 0; band < 16; band++)
                        if (ix == g_dos2dBase[band]) {
                            colour = g_dos2dRemap[slot-1][band];
                            break;
                        }
                unsigned char* p = &rgba[((size_t)(sy+y) * s->tw + sx+x) * 4];
                if (shadow && ix) {
                    /* SHAPE_FADING | SHAPE_PREDATOR darkens the ground under the
                       whole silhouette rather than drawing a second aircraft, in the
                       theater's shadow colour (dos2d_shadow_blend). */
                    const Dos2DShadowBlend sb = dos2d_shadow_blend();
                    p[0] = sb.r; p[1] = sb.g; p[2] = sb.b; p[3] = sb.a;
                } else if ((trans || (ghost && g_dos2dRa && strcmp(s->name, "MOVEFLSH")))
                           && ix) {
                    /* TranslucentTable (dos2d_magic_alpha). Drawing those source
                       colours raw gives a muzzle flash a white disc and makes corpses
                       green. Red Alert's ghost draws also shade LTGREEN, as
                       Map.UnitShadow does, at 130. */
                    bool shaded;
                    int alpha = dos2d_magic_alpha(ix, &shaded);
                    if (!trans && ix == 4) { alpha = 130; shaded = true; }
                    p[0] = shaded ? 0 : pal[colour*3];
                    p[1] = shaded ? 0 : pal[colour*3+1];
                    p[2] = shaded ? 0 : pal[colour*3+2];
                    p[3] = (unsigned char)alpha;
                } else if (ghost && ix == 4) {
                    p[0] = p[1] = p[2] = 0; p[3] = 130;
                } else if (ghost && !strcmp(s->name, "MOVEFLSH") && ix == 1) {
                    /* AnimClass uses Map.WhiteTranslucentTable for MOVEFLSH.
                     * Its only drawn index is 1, faded 80/256 toward white. */
                    p[0] = p[1] = p[2] = 255; p[3] = 80;
                } else if (!strcmp(s->name, "SHADOW")) {
                    /* DisplayClass::Init_Theater: BLACK is solid; palette indices
                     * 16,15,14,13 fade toward black by 130,170,250,250/255. */
                    p[0] = p[1] = p[2] = 0;
                    p[3] = ix == 0 ? 0 : ix == 16 ? 130 : ix == 15 ? 170
                         : ix == 14 || ix == 13 ? 250 : 255;
                } else {
                    p[0] = pal[colour*3]; p[1] = pal[colour*3+1]; p[2] = pal[colour*3+2];
                    if (flash && ix) {
                        /* Map.FadingLight fades the base palette 85/255 toward white. */
                        p[0] = (unsigned char)((p[0]*170 + 255*85) / 255);
                        p[1] = (unsigned char)((p[1]*170 + 255*85) / 255);
                        p[2] = (unsigned char)((p[2]*170 + 255*85) / 255);
                    }
                    p[3] = ix == 0 ? 0 : 255;
                }
            }
    }
    glGenTextures(1, &s->tex[texSlot]);
    glBindTexture(GL_TEXTURE_2D, s->tex[texSlot]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s->tw, s->th, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba.data());
    return true;
}

static void dos2d_quad(float x0, float y0, float x1, float y1,
                       float u0, float v0, float u1, float v1)
{
    glTexCoord2f(u0,v0); glVertex2f(x0,y0);
    glTexCoord2f(u1,v0); glVertex2f(x1,y0);
    glTexCoord2f(u1,v1); glVertex2f(x1,y1);
    glTexCoord2f(u0,v1); glVertex2f(x0,y1);
}

static void dos2d_draw_shape(const char* name, int frame, int px, int py,
                             bool centered, float ox, float oy, int scale, int remap = -1,
                             bool ghost = false, bool flash = false, bool shadow = false,
                             bool trans = false)
{
    Dos2DShape* s = dos2d_shape(name);
    if (!s) {
        static std::set<std::string> missing;
        g_dos2dMissing++;
        if (missing.insert(name).second) fprintf(stderr, "DOS2D|missing original SHP %s\n", name);
        return;
    }
    if (!dos2d_upload(s,remap,ghost,flash,shadow,trans)) return;
    const int fr = frame < s->frames && frame >= 0 ? frame : 0;
    const int sx = (fr % s->cols)*s->w, sy = (fr/s->cols)*s->h;
    const float x0 = ox + (px-(centered ? s->w/2 : 0))*scale;
    const float y0 = oy + (py-(centered ? s->h/2 : 0))*scale;
    const int texSlot = dos2d_tex_slot(remap, ghost, flash, shadow, trans);
    if (ghost || shadow || trans) {
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glAlphaFunc(GL_GREATER,0.0f); /* white move flash has alpha below 0.5 */
    }
    glBindTexture(GL_TEXTURE_2D,s->tex[texSlot]);
    glBegin(GL_QUADS);
    dos2d_quad(x0,y0,x0+s->w*scale,y0+s->h*scale,
               (float)sx/s->tw,(float)sy/s->th,
               (float)(sx+s->w)/s->tw,(float)(sy+s->h)/s->th);
    glEnd();
    if (ghost || shadow || trans) { glDisable(GL_BLEND); glAlphaFunc(GL_GREATER,0.5f); }
}

/* CLOAKING. TechnoClass::Visual_Character (techno.cpp; the same ladder in both games)
 * says how the 1995 engine draws a cloaking unit, and Techno_Draw_Object turns that into
 * these draws, in this order:
 *
 *   NORMAL      the unit                                FADING|GHOST
 *   INDISTINCT  the unit, then the shimmer              FADING|GHOST, PREDATOR
 *   DARKEN      the unit, darkened, then the shimmer    FADING|GHOST, FADING|PREDATOR, PREDATOR
 *   SHADOWY     a dark silhouette, then the shimmer     FADING|PREDATOR, PREDATOR
 *   RIPPLE      the shimmer alone                       PREDATOR
 *   HIDDEN      nothing
 *
 * FADING|PREDATOR is Map.SpecialGhost, the darkening an aircraft's shadow is drawn with
 * (dos2d_shadow_blend); PREDATOR alone is the shimmer (dos2d_shimmer), which draws none
 * of the unit's own colours. Somebody else's fully cloaked unit is HIDDEN; the player's
 * own is SHADOWY, as is anybody's between 19 and 28 of the 38 stages either way.
 *
 * THE LAYER EXPORT DRAWS HIDDEN AS SHADOWY. Techno_Draw_Object gives the export's virtual
 * window a hidden unit's layers exactly as if it were shadowy, so nothing in the layers
 * says it is hidden. Classic works the ladder out again from the unit's Cloak, its owner
 * and its cloaking device's stage (the object dump's cstage) and draws no layer of a
 * hidden unit. Without the stage the one frame an uncloaking unit spends hidden draws as
 * shadowy; nothing else depends on it. */
enum { D2V_NORMAL, D2V_INDISTINCT, D2V_DARKEN, D2V_SHADOWY, D2V_RIPPLE, D2V_HIDDEN };
static const char* const D2V_NAME[] = {
    "normal", "indistinct", "darken", "shadowy", "ripple", "hidden"
};

static int dos2d_visual(int cloak, int stage, bool ours)
{
    enum { UNCLOAKED, CLOAKING, CLOAKED, UNCLOAKING };
    const int MAX_UNCLOAK_STAGE = 38;
    if (cloak == UNCLOAKED) return D2V_NORMAL;
    if (cloak == CLOAKED) return ours ? D2V_SHADOWY : D2V_HIDDEN;
    if (stage < 0) return D2V_SHADOWY;          /* stage unknown: see above */
    if (cloak == UNCLOAKING) stage = MAX_UNCLOAK_STAGE - stage;
    if (stage <= 0) return D2V_NORMAL;
    stage = (stage << 8) / MAX_UNCLOAK_STAGE;   /* Cardinal_To_Fixed */
    if (stage < 0x40) return D2V_INDISTINCT;
    if (stage < 0x80) return D2V_DARKEN;
    if (stage < 0xC0) return D2V_SHADOWY;
    if (ours) return D2V_SHADOWY;
    if (stage < 0xFF) return D2V_RIPPLE;
    return D2V_HIDDEN;
}

/* The cloaking device's stage for a layer's unit, from the object dump, or -1 when the
   dump does not carry this unit or its cloak. A dump without the two cloak fields (Red
   Alert's) reads UNCLOAKED with stage 0 for everything, so the stage is taken only where
   the dump's cloak state is the layer's own. */
static int dos2d_cloak_stage(const CNCObjectStruct& o)
{
    const int kind = o.Type == UNIT ? K_UNIT : o.Type == INFANTRY ? K_INFANTRY
                   : o.Type == AIRCRAFT ? K_AIRCRAFT : o.Type == BUILDING ? K_BUILDING : -1;
    if (kind < 0) return -1;
    for (size_t i = 0; i < g_objects.size(); i++)
        if (g_objects[i].id == o.ID && g_objects[i].kind == kind)
            return g_objects[i].cloak == o.Cloak ? g_objects[i].cstage : -1;
    return -1;
}

/* SHAPE_PREDATOR, the cloaking shimmer: BF_Predator_Trans (common/keybuff.cpp) as
 * CC_Draw_Shape (conquer.cpp) sets it up. Every pixel under the shape takes the colour
 * already on the screen a few pixels away: 1 3 2 5 2 3 4 1 to the right along its own
 * row, the step moving on one place per pixel drawn and starting at the game frame modulo
 * 8; when the shape's centre is right of the middle of the tactical view, the same steps
 * to the left along the row below. A step that would leave the view keeps the pixel.
 * Done on the 1995 pixel grid: the picture under the shape is read back, one sample per
 * 1995 pixel, shifted, and drawn over itself. */
static void dos2d_shimmer(const char* name, int frame, int px, int py, float ox, float oy,
                          int scale, int viewW, int viewH, int fbh)
{
    Dos2DShape* s = dos2d_shape(name);
    if (!s || scale < 1) return;
    if (s->raw.empty()) {
        const size_t rawbytes = (size_t)s->w * s->h * s->frames;
        s->raw.resize(rawbytes);
        uLongf outbytes = (uLongf)rawbytes;
        if (uncompress(s->raw.data(), &outbytes, s->zipped.data(), (uLong)s->zipped.size()) != Z_OK
            || outbytes != rawbytes) { s->raw.clear(); return; }
    }
    const int fr = frame < s->frames && frame >= 0 ? frame : 0;
    const unsigned char* idx = &s->raw[(size_t)fr * s->w * s->h];
    const int sx0 = px - s->w/2, sy0 = py - s->h/2;
    /* the 1995 pixels the steps can read: five either side, one row below */
    const int rx0 = std::max(0, sx0 - 5), rx1 = std::min(viewW, sx0 + s->w + 5);
    const int ry0 = std::max(0, sy0),     ry1 = std::min(viewH, sy0 + s->h + 1);
    if (rx1 <= rx0 || ry1 <= ry0) return;
    const int rw = (rx1 - rx0) * scale, rh = (ry1 - ry0) * scale;
    std::vector<unsigned char> under((size_t)rw * rh * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels((int)lroundf(ox) + rx0*scale, fbh - ((int)lroundf(oy) + ry1*scale), rw, rh,
                 GL_RGBA, GL_UNSIGNED_BYTE, under.data());
    /* glReadPixels rows run bottom up; sample the middle of each 1995 pixel */
    auto at = [&](int x, int y) -> const unsigned char* {
        const int col = (x - rx0)*scale + scale/2, row = (ry1 - 1 - y)*scale + scale/2;
        return &under[((size_t)row * rw + col) * 4];
    };
    static const int steps[8] = { 1, 3, 2, 5, 2, 3, 4, 1 };
    const int engineFrame = g_engineFrame > 0 ? g_engineFrame : 0;
    const bool left = px > viewW / 2;
    unsigned step = left ? (unsigned)(-engineFrame) % 8u : (unsigned)engineFrame % 8u;
    const int tw = dos2d_pow2(s->w), th = dos2d_pow2(s->h);
    std::vector<unsigned char> rgba((size_t)tw * th * 4, 0);
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            const int vx = sx0 + x, vy = sy0 + y;
            if (!idx[y * s->w + x] || vx < 0 || vx >= viewW || vy < 0 || vy >= viewH) continue;
            const int fx = left ? vx - steps[step] : vx + steps[step];
            const int fy = left ? vy + 1 : vy;
            step = (step + 1) % 8;
            const bool inside = fx >= rx0 && fx < rx1 && fy >= ry0 && fy < ry1;
            const unsigned char* c = inside ? at(fx, fy) : at(vx, vy);
            unsigned char* p = &rgba[((size_t)y * tw + x) * 4];
            p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; p[3] = 255;
        }
    if (!g_dos2dShimmerTex) glGenTextures(1, &g_dos2dShimmerTex);
    glBindTexture(GL_TEXTURE_2D, g_dos2dShimmerTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    const float x0 = ox + sx0*scale, y0 = oy + sy0*scale;
    glColor4f(1,1,1,1);
    glBegin(GL_QUADS);
    dos2d_quad(x0, y0, x0 + s->w*scale, y0 + s->h*scale,
               0.0f, 0.0f, (float)s->w/tw, (float)s->h/th);
    glEnd();
}

/* TechnoClass::Draw_It in the 1995 source uses WHITE one-pixel corners sized
 * from Class_Of().Dimensions, and a four-row black-outlined health bar. */
static void dos2d_solid(float x, float y, float w, float h, unsigned char colour)
{
    const unsigned char* pal = dos2d_palette();
    glColor3ub(pal[colour*3],pal[colour*3+1],pal[colour*3+2]);
    glBegin(GL_QUADS);
    glVertex2f(x,y); glVertex2f(x+w,y);
    glVertex2f(x+w,y+h); glVertex2f(x,y+h);
    glEnd();
}

static unsigned int dos2d_player_mask(void)
{
    /* Selection and target flashing both use HousesType bit positions, the order of
       each game's house table (Tiberian Dawn's hdata.cpp: GoodGuy, BadGuy, Neutral,
       Special, then the eight multiplayer houses). A skirmish player is one of those
       Multi houses, so a mask that knew only GoodGuy and BadGuy hid every selection
       bracket and target flash of a skirmish. */
    static const char* const ra_houses[] = {
        "Spain", "Greece", "USSR", "England", "Ukraine", "Germany",
        "France", "Turkey", "GoodGuy", "BadGuy", "Neutral", "JP",
        "Multi1", "Multi2", "Multi3", "Multi4", "Multi5", "Multi6",
        "Multi7", "Multi8"
    };
    static const char* const td_houses[] = {
        "GoodGuy", "BadGuy", "Neutral", "Special",
        "Multi1", "Multi2", "Multi3", "Multi4", "Multi5", "Multi6",
        "Multi7", "Multi8"
    };
    const char* const* houses = g_dos2dRa ? ra_houses : td_houses;
    const unsigned int count = g_dos2dRa ? sizeof ra_houses / sizeof ra_houses[0]
                                         : sizeof td_houses / sizeof td_houses[0];
    for (unsigned int i = 0; i < count; i++)
        if (!strcmp(g_playerHouse, houses[i])) return 1u << i;
    return 1u;
}

static void dos2d_selection(const CNCObjectStruct& o, int px, int py,
                            float ox, float oy, int scale)
{
    if (!(o.IsSelectedMask & dos2d_player_mask())) return;
    const int w = std::max(4,std::min(192,(int)o.DimensionX));
    const int h = std::max(4,std::min(192,(int)o.DimensionY));
    if (o.Type == INFANTRY) py -= 6;
    if (o.Type == BUILDING && !strcmp(o.TypeName,"PYLE")) py -= 5;
    const int lx = w/2, ly = h/2, dx = std::max(1,w/5), dy = std::max(1,h/5);
    const bool health = o.Strength > 0 && o.MaxStrength > 0;
    const int fudge = health ? 4 : 0;
    const float x0=ox+(px-lx)*scale, x1=ox+(px+lx)*scale;
    const float yt=oy+(py-ly+fudge)*scale, yb=oy+(py+ly)*scale;
    glDisable(GL_TEXTURE_2D);
    if (health) {
        const float hy=oy+(py-ly)*scale;
        dos2d_solid(x0,hy,w*scale,4*scale,12); /* BLACK */
        const float ratio=std::max(0.0f,std::min(1.0f,(float)o.Strength/o.MaxStrength));
        const int filled=std::max(1,std::min(w-2,(int)((w-2)*ratio)));
        dos2d_solid(x0+scale,hy+scale,filled*scale,2*scale,
                    ratio<0.25f ? 8 : ratio<0.5f ? 5 : 4);
    }
    const float t=(float)scale, hx=dx*scale, vy=dy*scale;
    dos2d_solid(x0,yt,hx,t,15); dos2d_solid(x0,yt,t,vy,15);
    dos2d_solid(x1-hx,yt,hx,t,15); dos2d_solid(x1-t,yt,t,vy,15);
    dos2d_solid(x1-hx,yb-t,hx,t,15); dos2d_solid(x1-t,yb-vy,t,vy,15);
    dos2d_solid(x0,yb-t,hx,t,15); dos2d_solid(x0,yb-vy,t,vy,15);
    glColor4f(1,1,1,1);
    glEnable(GL_TEXTURE_2D);
}

/* THE OBELISK'S BEAM, as TechnoClass::Draw_It drew it: the firer's three lines, each in
 * its own palette colour (two outer 0x7D, the core 0x7F, drawn in that order so the core
 * is on top), from the source pixel moved by the line's own x offset to the target pixel,
 * one 1995 pixel wide. Drawn straight after the firer's own sprite, which is where Draw_It
 * drew them, so whatever is drawn later covers them as it did then; a firer that is not
 * drawn draws no beam. The beams and their five-frame clock are laser_mod.h's. The ends are
 * the engine's coordinates in 1995 pixels, by the same lepton-to-pixel rule the anims use
 * here; Bresenham, both ends included, as the 1995 line routine. */
static void dos2d_line(int x0, int y0, int x1, int y1, unsigned char colour,
                       float ox, float oy, int scale, int* rgb)
{
    const unsigned char* pal = g_dos2dRa ? g_dos2dPalette[ra2d_pal_index()]
                             : g_dbPack ? g_dbPack->pal8 : g_dosPal8;
    rgb[0] = pal[colour * 3]; rgb[1] = pal[colour * 3 + 1]; rgb[2] = pal[colour * 3 + 2];
    const int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (int guard = 0; guard < 4096; guard++) {
        dos2d_solid(ox + x0 * scale, oy + y0 * scale, (float)scale, (float)scale, colour);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static int dos2d_draw_lasers(const CNCObjectStruct& o, int viewX, int viewY,
                             float ox, float oy, int scale)
{
    /* the layer export's object types against the engine's RTTI numbers */
    const int rtti = o.Type == INFANTRY ? 1 : o.Type == UNIT ? 3 : o.Type == AIRCRAFT ? 5
                   : o.Type == BUILDING ? 7 : -1;
    if (rtti < 0 || g_lasers.empty()) return 0;
    int drawn = 0;
    for (size_t i = 0; i < g_lasers.size(); i++) {
        const LaserBeam& b = g_lasers[i];
        if (b.l.kind != rtti || b.l.id != (int)o.ID || !laser_live(b, NULL)) continue;
        const int sx = (int)floorf(b.l.sx * 24.0f) - viewX;
        const int sy = (int)floorf(b.l.sz * 24.0f) - viewY;
        const int tx = (int)floorf(b.l.tx * 24.0f) - viewX;
        const int ty = (int)floorf(b.l.tz * 24.0f) - viewY;
        glDisable(GL_TEXTURE_2D);
        for (int k = 0; k < b.l.count && k < 3; k++)
            if (b.l.colour[k] >= 0)
                dos2d_line(sx + b.l.off[k], sy, tx, ty, (unsigned char)b.l.colour[k],
                           ox, oy, scale, g_laserClassicRGB[k]);
        glColor4f(1, 1, 1, 1);
        glEnable(GL_TEXTURE_2D);
        drawn++;
    }
    g_laserDrawn += drawn;
    if (drawn) g_laserDrawnMode = FX_MODE_CLASSIC;
    return drawn;
}

/* The engine's EFX|ANIM export carries the original AnimType, shape name, Start and
 * live Stage. WINDOW_VIRTUAL replaces several legacy animations with 3D variants,
 * so GAME_STATE_LAYERS cannot supply their DOS sprites. Use the original records,
 * omitting only the five *_VIRTUAL types at the end of AnimType. */
static int dos2d_draw_anims(bool ground, int viewX, int viewY,
                            float ox, float oy, int scale, int viewW, int viewH)
{
    int drawn=0;
    for (size_t i=0;i<g_efxAnims.size();i++) {
        const EfxAnim& a=g_efxAnims[i];
        char theater_name[16];
        snprintf(theater_name,sizeof theater_name,"%c/%s",
                 ra2d_theater_letter(),a.name);
        const char* art = g_dos2dRa && dos2d_shape(theater_name) ? theater_name : a.name;
        const Dos2DShape* shape=dos2d_shape(art);
        /* (the engine's own number, etype: under Red Alert `type` is Tiberian Dawn's
           counterpart for the 3D effects, ra3d_effects_mod.h, and -2 for none) */
        if (a.etype<0 || (!g_dos2dRa && a.etype>=80)
            || (a.ground!=0)!=ground || a.stages<=0
            || a.stage<0 || a.stage>=a.stages || !shape
            || a.start+a.stage<0 || a.start+a.stage>=shape->frames) continue;
        const int px=(int)floorf(a.wx*24.0f)-viewX;
        const int py=(int)floorf(a.wz*24.0f)-viewY;
        if (px<-192 || px>viewW+192 || py<-192 || py>viewH+192
            || shroud_cell_hidden((int)floorf(a.wx),(int)floorf(a.wz))) continue;
        bool translucent, trans = false;
        if (g_dos2dRa) {
            translucent = !strcmp(a.name,"MOVEFLSH") || !strcmp(a.name,"GUNFIRE") ||
                          !strcmp(a.name,"CORPSE1") || !strcmp(a.name,"CORPSE2") ||
                          !strcmp(a.name,"CORPSE3");
        } else {
            /* Tiberian Dawn's AnimClass::Draw_It (anim.cpp): the dinosaur deaths, the
               atom blast and the flag go through Map.UnitShadow, MOVEFLSH (IsWhiteTrans)
               through WhiteTranslucentTable, and the two types adata.cpp flags
               IsTranslucent, the muzzle flash GUNFIRE and the smoke puff SMOKEY, through
               TranslucentTable. Without it GUNFIRE's white rim draws as a solid disc. */
            /* The ghost draws: dos2d_upload gives MOVEFLSH its white table and the
               rest UnitShadow's shaded LTGREEN. */
            static const char* const ghosted[] = {
                "TRIC", "TREX", "STEG", "RAPT", "ATOMSFX", "FLAGFLY", "MOVEFLSH"
            };
            translucent = false;
            for (size_t k = 0; k < sizeof ghosted / sizeof ghosted[0]; k++)
                if (!strcmp(a.name, ghosted[k])) translucent = true;
            trans = !strcmp(a.name,"GUNFIRE") || !strcmp(a.name,"SMOKEY");
        }
        dos2d_draw_shape(art,a.start+a.stage,px,py,true,ox,oy,scale,-1,
                         translucent,false,false,trans);
        if (g_dos2dAnimRecs.size() < 256) {
            Dos2DAnimRec r;
            memset(&r, 0, sizeof r);
            snprintf(r.name, sizeof r.name, "%s", a.name);
            r.frame = a.start+a.stage; r.ghost = translucent; r.trans = trans;
            r.x0 = ox + (px - shape->w/2)*scale; r.y0 = oy + (py - shape->h/2)*scale;
            r.x1 = r.x0 + shape->w*scale;      r.y1 = r.y0 + shape->h*scale;
            g_dos2dAnimRecs.push_back(r);
        }
        drawn++;
    }
    return drawn;
}

/* CellClass::Draw_It stamps TransIconset frame 0 or 2 into every occupied
 * foundation cell. The proximity verdict belongs to the cursor origin, while
 * clearance belongs to each cell. Draw these before units and buildings, as
 * the original tactical renderer does.
 *
 * BOTH GAMES, and Tiberian Dawn's is the same routine (cell.cpp, IsCursorHere:
 * frame 0 when Map.ProximityCheck && Is_Generally_Clear(), else 2). This was Red
 * Alert's only, and Classic is the one look that never reaches sb_draw_placement
 * (draw_frame hands the whole picture to dos2d_scene), so a Tiberian Dawn building
 * held over the map in Classic showed no footprint at all. No guard on the shape:
 * a pack without TRANS says so through dos2d_draw_shape's missing line rather than
 * drawing nothing in silence. */
static void dos2d_place_cursor(int viewX, int viewY,
                               float ox, float oy, int scale, int viewW, int viewH)
{
    if (!g_sbPlacing || g_sbPlaceIdx < 0
        || g_sbPlaceIdx >= (int)g_sbState.entry.size()
        || g_sbHoverX < 0 || g_sbHoverY < 0) return;
    const int oi = sb_grid_index(g_sbHoverX, g_sbHoverY);
    const bool prox = oi >= 0 && g_sbPlaceProx[oi];
    const SbEntry& entry = g_sbState.entry[g_sbPlaceIdx];
    const int origin = g_sbHoverY * SB_STRIDE + g_sbHoverX;
    glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER, 0.5f);
    for (size_t k = 0; k < entry.occupy.size(); k++) {
        const int c = origin + entry.occupy[k];
        const int cx = c % SB_STRIDE, cy = c / SB_STRIDE;
        const int px = cx * 24 - viewX, py = cy * 24 - viewY;
        if (px < -24 || px >= viewW || py < -24 || py >= viewH
            || shroud_cell_hidden(cx, cy)) continue;
        const int ci = sb_grid_index(cx, cy);
        const bool clear = prox && ci >= 0 && g_sbPlaceClear[ci]
                           && sb_on_play_map(cx, cy);
        dos2d_draw_shape("TRANS", clear ? 0 : 2, px, py, false,
                         ox, oy, scale);
    }
}

static void dos2d_scene(int fbw, int fbh)
{
    g_fbW = fbw; g_fbH = fbh;
    g_laserDrawn = 0;      /* the beams this picture draws, counted by dos2d_draw_lasers */
    sb_layout(fbw, fbh);
    const int scale = g_dos2dRa ? g_ra95WorldScale : g_dbScale;
    const int viewW = g_dos2dRa ? g_ra95ViewW : 240;
    const int viewH = g_dos2dRa ? g_ra95ViewH : 200;
    const float ox = g_dbX0 - (float)viewW * scale, oy = g_dbY0;
    const int viewX = (int)floorf(g_camX * 24.0f - viewW * 0.5f);
    const int viewY = (int)floorf(g_camZ * 24.0f - viewH * 0.5f);
    const PackTex& atlas = g_pack.tex[g_pack.terrainTexDos >= 0 ? g_pack.terrainTexDos : g_pack.terrainTex];
    glViewport(0,0,fbw,fbh);
    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    begin_overlay(fbw,fbh);
    glEnable(GL_SCISSOR_TEST);
    glScissor((int)ox, fbh - (int)(oy + viewH*scale), viewW*scale, viewH*scale);
    if (g_dos2dRa && g_raStaticReady) {
        /* MapCell includes a one-cell engine sampling collar. Keep terrain,
           objects and fog inside OriginalMapCell, the playable rectangle used
           by the camera clamp, even at a high-resolution viewport. */
        const int mapX = g_raStaticMap.OriginalMapCellWidth > 0
                       ? g_raStaticMap.OriginalMapCellX : g_raStaticMap.MapCellX;
        const int mapY = g_raStaticMap.OriginalMapCellHeight > 0
                       ? g_raStaticMap.OriginalMapCellY : g_raStaticMap.MapCellY;
        const int mapW = g_raStaticMap.OriginalMapCellWidth > 0
                       ? g_raStaticMap.OriginalMapCellWidth : g_raStaticMap.MapCellWidth;
        const int mapH = g_raStaticMap.OriginalMapCellHeight > 0
                       ? g_raStaticMap.OriginalMapCellHeight : g_raStaticMap.MapCellHeight;
        const int x0 = std::max(0, (mapX*24-viewX)*scale);
        const int y0 = std::max(0, (mapY*24-viewY)*scale);
        const int x1 = std::min(viewW*scale,
            ((mapX+mapW)*24-viewX)*scale);
        const int y1 = std::min(viewH*scale,
            ((mapY+mapH)*24-viewY)*scale);
        glScissor((int)ox+x0, fbh-(int)oy-y1,
                  std::max(0,x1-x0), std::max(0,y1-y0));
    }
    glEnable(GL_TEXTURE_2D);
    if (g_dos2dRa) {
        ra2d_draw_terrain(viewX, viewY, ox, oy, scale, viewW, viewH);
    } else {
        glBindTexture(GL_TEXTURE_2D, g_pack.terrainClassicGL ? g_pack.terrainClassicGL : atlas.gl);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glColor4f(1,1,1,1);
        glBegin(GL_QUADS);
        for (size_t i = 0; i < g_pack.cell.size(); i++) {
            const PackCell& c = g_pack.cell[i];
            const int px = c.x*24-viewX, py = c.y*24-viewY;
            if (px < -24 || px >= viewW || py < -24 || py >= viewH || !cell_shown(c.x,c.y)) continue;
            dos2d_quad(ox+px*scale, oy+py*scale, ox+(px+24)*scale, oy+(py+24)*scale,
                       c.u0*(float)atlas.uw/atlas.w, c.v0*(float)atlas.uh/atlas.h,
                       c.u1*(float)atlas.uw/atlas.w, c.v1*(float)atlas.uh/atlas.h);
        }
        glEnd();
    }

    if (dos2d_load() && BrainGetState && BrainGetView) {
        static std::vector<unsigned char> bytes(sizeof(CNCObjectListStruct) + 8192*sizeof(CNCObjectStruct));
        int view[11]; BrainGetView(view);
        static std::vector<unsigned char> dynbytes(sizeof(CNCDynamicMapStruct) + 8192*sizeof(CNCDynamicMapEntryStruct));
        glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER, 0.5f);
        const bool dynok = BrainGetState(GAME_STATE_DYNAMIC_MAP, 0, dynbytes.data(), (unsigned)dynbytes.size());
        static bool dynreported = false;
        if (!dynreported) { fprintf(stderr, "DOS2D|dynamic ok=%d count=%d\n", dynok ? 1 : 0,
                                     dynok ? ((const CNCDynamicMapStruct*)dynbytes.data())->Count : 0);
                            dynreported = true; }
        if (dynok) {
            const CNCDynamicMapStruct* dyn = (const CNCDynamicMapStruct*)dynbytes.data();
            const int count = dyn->Count < 8192 ? dyn->Count : 8192;
            for (int i = 0; i < count; i++) {
                const CNCDynamicMapEntryStruct& d = dyn->Entries[i];
                if (shroud_cell_hidden(d.CellX,d.CellY)) continue;
                const int px = d.PositionX + (view[4]*24)/256 - viewX;
                const int py = d.PositionY + (view[5]*24)/256 - viewY;
                if (px < -192 || px > viewW+192 || py < -192 || py > viewH+192) continue;
                char key[16];
                snprintf(key,sizeof key,"%c/%s",g_dos2dRa ?
                         ra2d_theater_letter() :
                         g_pack.theater[0],d.AssetName);
                dos2d_draw_shape(dos2d_shape(key) ? key : d.AssetName,d.ShapeIndex,
                                 px,py,d.IsOverlay || d.IsFlag,ox,oy,scale,-1,
                                 (d.DrawFlags & 0x1000) != 0 /* SHAPE_GHOST */);
            }
        }
        dos2d_place_cursor(viewX,viewY,ox,oy,scale,viewW,viewH);
        g_dos2dAnimRecs.clear();
        dos2d_draw_anims(true,viewX,viewY,ox,oy,scale,viewW,viewH);
        const bool layers_ok = BrainGetState(GAME_STATE_LAYERS, 0, bytes.data(),
                                             (unsigned)bytes.size());
        if (g_dos2dRa && !g_raLayersReported) {
            const CNCObjectListStruct* probe = (const CNCObjectListStruct*)bytes.data();
            int infantry = 0, vehicles = 0, buildings = 0, terrain = 0;
            if (layers_ok)
                for (int i = 0; i < probe->Count && i < 8192; i++) {
                    const CNCObjectStruct& item = probe->Objects[i];
                    infantry += item.Type == INFANTRY;
                    vehicles += item.Type == UNIT || item.Type == VESSEL || item.Type == AIRCRAFT;
                    buildings += item.Type == BUILDING;
                    terrain += item.Type == TERRAIN;
                }
            fprintf(stderr, "RA2D|sprites|ok=%d|records=%d|infantry=%d|vehicles=%d|buildings=%d|terrain=%d\n",
                    layers_ok ? 1 : 0, layers_ok ? probe->Count : 0,
                    infantry, vehicles, buildings, terrain);
            g_raLayersReported = 1;
        }
        g_dos2dAirRecs.clear();
        if (layers_ok) {
            const CNCObjectListStruct* list = (const CNCObjectListStruct*)bytes.data();
            std::vector<int> order;
            const int count = list->Count < 8192 ? list->Count : 8192;
            for (int i = 0; i < count; i++) order.push_back(i);
            std::stable_sort(order.begin(), order.end(), [list](int a,int b) {
                return list->Objects[a].SortOrder < list->Objects[b].SortOrder;
            });
            /* The export moves a FADING|PREDATOR sub-object under its unit, which is right
               for an aircraft's shadow. A darkening unit's darkening is drawn over the unit
               (Techno_Draw_Object), so it goes back above it. */
            for (size_t j = 0; j < order.size(); j++) {
                const CNCObjectStruct& d = list->Objects[order[j]];
                if (!d.SubObject || (d.DrawFlags & 0x0300) != 0x0300 || d.Type == AIRCRAFT
                    || d.Cloak == 0) continue;
                for (size_t k = j + 1; k < order.size(); k++) {
                    const CNCObjectStruct& u = list->Objects[order[k]];
                    if (u.SubObject || u.CNCInternalObjectPointer != d.CNCInternalObjectPointer)
                        continue;
                    std::rotate(order.begin() + j, order.begin() + j + 1, order.begin() + k + 1);
                    j--;
                    break;
                }
            }
            g_dos2dCloakRecs.clear();
            glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER, 0.5f);
            for (size_t j = 0; j < order.size(); j++) {
                const CNCObjectStruct& o = list->Objects[order[j]];
                if (o.Type == ANIM) continue; /* original EFX|ANIM records above/below */
                const int wx = o.PositionX + (view[4]*24)/256;
                const int wy = o.PositionY + (view[5]*24)/256;
                const int px = wx-viewX, py = wy-viewY;
                if (px < -192 || px > viewW+192 || py < -192 || py > viewH+192) continue;
                /* A building or terrain sprite can extend into mapped cells even
                   when its anchor is still shrouded. The fog pass below clips its
                   hidden pixels; culling the whole shape at the anchor leaves a
                   visible hole. Mobile objects still need the centre-cell test. */
                if (shroud_cell_hidden(o.CellX,o.CellY) &&
                    (!g_dos2dRa || (o.Type != BUILDING && o.Type != TERRAIN))) continue;
                char key[16];
                snprintf(key,sizeof key,"%c/%s",g_dos2dRa ?
                         ra2d_theater_letter() :
                         g_pack.theater[0],o.AssetName);
                const unsigned int playerMask = dos2d_player_mask();
                const bool flash = o.IsBlushing && (o.FlashingFlags & playerMask);
                /* HouseClass::Remap_Table gives single-player Nod structures the
                 * red table; BadGuy's stored house colour is blue-grey for units. */
                const bool rotor = g_dos2dRa && (!strcmp(o.AssetName,"RROTOR") ||
                                                   !strcmp(o.AssetName,"LROTOR"));
                /* SHAPE_FADING | SHAPE_PREDATOR is what the engine asks for an aircraft's
                   shadow, a flying rotor, a cloaking unit's darkening and a shadowy unit
                   itself, and both games draw it as the shadow blend (dos2d_shadow_blend),
                   as CC_Draw_Shape turns every one of them into Map.SpecialGhost. The
                   control toggle below reads the engine's flags, not `shadow`, so a gate
                   still finds those layers by difference on a build where `shadow` misses
                   some of them. SHAPE_PREDATOR alone is the cloaking shimmer. */
                const bool fadepred = (o.DrawFlags & 0x0300) == 0x0300;
                const bool shadow = fadepred;
                const bool shimmer = (o.DrawFlags & 0x0300) == 0x0200;
                const int remap = flash || rotor || shadow ? -1 : (o.Type == BUILDING && o.Owner == 1
                                   ? 2 /* REMAP_RED */ : o.RemapColor);
                const char* art = o.IsTheaterSpecific && dos2d_shape(key) ? key : o.AssetName;
                /* A unit the 1995 engine would not draw at all: hidden by its cloak
                   (dos2d_visual), or one the export marks invisible to this player (Red
                   Alert's mines, IsInvisible). */
                const bool techno = o.Type == UNIT || o.Type == INFANTRY || o.Type == AIRCRAFT
                                 || o.Type == BUILDING || o.Type == VESSEL;
                const bool ours = o.Owner >= 0 && o.Owner < 32
                               && ((1u << o.Owner) & playerMask) != 0;
                const int cstage = techno && (o.Cloak == 1 || o.Cloak == 3)
                                 ? dos2d_cloak_stage(o) : -1;
                const int visual = techno ? dos2d_visual(o.Cloak, cstage, ours) : D2V_NORMAL;
                const bool invisible = !(o.VisibleFlags & playerMask);
                const bool omitted = g_dos2dOmit[0] && !strcmp(o.TypeName, g_dos2dOmit);
                if ((o.Type == UNIT || o.Type == VESSEL) && g_dos2dCloakRecs.size() < 256) {
                    const Dos2DShape* s = dos2d_shape(art);
                    Dos2DCloakRec r;
                    memset(&r, 0, sizeof r);
                    snprintf(r.type, sizeof r.type, "%s", o.TypeName);
                    r.id = o.ID; r.sub = o.SubObject; r.owner = o.Owner; r.ours = ours;
                    r.cloak = o.Cloak; r.cstage = cstage; r.visual = visual;
                    r.flags = o.DrawFlags;
                    r.as = omitted ? "omitted" : visual == D2V_HIDDEN ? "hidden"
                         : invisible ? "invisible" : shimmer ? "shimmer"
                         : shadow ? "darken" : "sprite";
                    if (s) {
                        r.x0 = ox + (px - s->w/2)*scale; r.y0 = oy + (py - s->h/2)*scale;
                        r.x1 = r.x0 + s->w*scale;      r.y1 = r.y0 + s->h*scale;
                    }
                    g_dos2dCloakRecs.push_back(r);
                }
                if (omitted || visual == D2V_HIDDEN || invisible) continue;
                if (o.Type == AIRCRAFT && g_dos2dAirRecs.size() < 256) {
                    const Dos2DShape* s = dos2d_shape(art);
                    Dos2DAirRec r;
                    memset(&r, 0, sizeof r);
                    snprintf(r.asset, sizeof r.asset, "%s", o.AssetName);
                    r.id = o.ID; r.sub = o.SubObject; r.flags = o.DrawFlags;
                    r.shadow = shadow; r.drawn = !(g_dos2dShadowsOff && fadepred);
                    if (s) {
                        r.x0 = ox + (px - s->w/2)*scale; r.y0 = oy + (py - s->h/2)*scale;
                        r.x1 = r.x0 + s->w*scale;      r.y1 = r.y0 + s->h*scale;
                    }
                    g_dos2dAirRecs.push_back(r);
                }
                if (g_dos2dShadowsOff && fadepred) continue;
                if (shimmer)
                    dos2d_shimmer(art,o.ShapeIndex,px,py,ox,oy,scale,viewW,viewH,fbh);
                else
                    dos2d_draw_shape(art,
                                     o.ShapeIndex,px,py,(o.DrawFlags & 0x0020) != 0,ox,oy,scale,remap,
                                     (o.DrawFlags & 0x1000) != 0,flash,shadow);
                /* Draw_It draws the lines before the selection box */
                if (!o.SubObject) dos2d_draw_lasers(o,viewX,viewY,ox,oy,scale);
                if (!o.SubObject) dos2d_selection(o,px,py,ox,oy,scale);
            }
        }
        dos2d_draw_anims(false,viewX,viewY,ox,oy,scale,viewW,viewH);
        glDisable(GL_ALPHA_TEST);
    }
    /* Deep DOS shroud is opaque black; draw the partial SHADOW.SHP rims after it. */
    glDisable(GL_TEXTURE_2D);
    glColor3f(0,0,0);
    glBegin(GL_QUADS);
    const bool ra_fog_cells = g_dos2dRa && g_raStaticReady;
    const size_t fog_cells = ra_fog_cells ?
        (size_t)g_raStaticMap.MapCellWidth * g_raStaticMap.MapCellHeight : g_pack.cell.size();
    for (size_t i = 0; i < fog_cells; i++) {
        const int cx = ra_fog_cells ? g_raStaticMap.MapCellX + (int)(i % g_raStaticMap.MapCellWidth)
                                 : g_pack.cell[i].x;
        const int cy = ra_fog_cells ? g_raStaticMap.MapCellY + (int)(i / g_raStaticMap.MapCellWidth)
                                 : g_pack.cell[i].y;
        const int px = cx*24-viewX, py = cy*24-viewY;
        if (px < -24 || px >= viewW || py < -24 || py >= viewH || !shroud_cell_hidden(cx,cy)) continue;
        const float x0=ox+px*scale,y0=oy+py*scale,x1=x0+24*scale,y1=y0+24*scale;
        glVertex2f(x0,y0); glVertex2f(x1,y0); glVertex2f(x1,y1); glVertex2f(x0,y1);
    }
    glEnd();
    /* Redraw_Shadow in the DOS source overlays SHADOW.SHP at the engine's own
     * Cell_Shadow index on visible-but-unmapped edge cells. */
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER,0.0f);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1,1,1,1);
    for (size_t i=0;i<fog_cells;i++) {
        const int cx = ra_fog_cells ? g_raStaticMap.MapCellX + (int)(i % g_raStaticMap.MapCellWidth)
                                 : g_pack.cell[i].x;
        const int cy = ra_fog_cells ? g_raStaticMap.MapCellY + (int)(i / g_raStaticMap.MapCellWidth)
                                 : g_pack.cell[i].y;
        const int shadow=shroud_shadow_at(cx,cy);
        /* Red Alert's Cell_Shadow table uses frames 0..46. The old 12-frame
           limit dropped most edge combinations and left square gaps. */
        if (shadow<0 || shadow>=48) continue;
        const int px=cx*24-viewX, py=cy*24-viewY;
        if (px<-24 || px>=viewW || py<-24 || py>=viewH) continue;
        dos2d_draw_shape("SHADOW",shadow,px,py,false,ox,oy,scale);
    }
    glDisable(GL_BLEND); glDisable(GL_ALPHA_TEST);
    glColor4f(1,1,1,1);
    if (g_bandOn) {
        /* The rubber band, as DisplayClass::Draw_It lays it over the tactical page in
           both games: Draw_Rect in WHITE, one 1995 pixel wide, both corners held inside
           the tactical view (Mouse_Left_Held bounds them), so the box sits on the same
           pixel grid as the sprites it is drawn round. */
        const int bx0 = std::max(0, std::min(viewW-1, (int)floorf((std::min(g_bandC0,g_bandC1)-ox)/scale)));
        const int bx1 = std::max(0, std::min(viewW-1, (int)floorf((std::max(g_bandC0,g_bandC1)-ox)/scale)));
        const int by0 = std::max(0, std::min(viewH-1, (int)floorf((std::min(g_bandR0,g_bandR1)-oy)/scale)));
        const int by1 = std::max(0, std::min(viewH-1, (int)floorf((std::max(g_bandR0,g_bandR1)-oy)/scale)));
        const float x0 = ox + bx0*scale, y0 = oy + by0*scale;
        const float w = (float)(bx1-bx0+1)*scale, h = (float)(by1-by0+1)*scale;
        glDisable(GL_TEXTURE_2D);
        dos2d_solid(x0, y0, w, (float)scale, 15);              /* WHITE */
        dos2d_solid(x0, y0+h-scale, w, (float)scale, 15);
        dos2d_solid(x0, y0, (float)scale, h, 15);
        dos2d_solid(x0+w-scale, y0, (float)scale, h, 15);
        glColor4f(1,1,1,1);
    }
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_TEXTURE_2D);
    end_overlay();
    if (sb_enabled()) {
        begin_overlay(fbw,fbh);
        sb_draw_panel(fbw,fbh);
        sb_draw_queue_digits(fbw,fbh);
        sb_draw_tooltip(fbw,fbh);
        end_overlay();
    }
    if (g_dos2dRa && g_gameOver.valid) {
        /* Red Alert's Win95 Do_Win/Do_Lose prints over the tactical scene;
           the N64 black verdict plate is not part of this mode. */
        const char* message = g_gameOver.win ? "MISSION ACCOMPLISHED" : "MISSION FAILED";
        const float ink_scale = 1.5f * g_ra95WorldScale;
        const float x = g_dbX0 * 0.5f - sb_text_w(message, ink_scale) * 0.5f;
        const float y = 90.0f * g_ra95WorldScale;
        begin_overlay(fbw,fbh);
        /* The map pass leaves blending disabled. FONT.SHP has transparent cells;
           drawing it without alpha rejection paints an opaque box per glyph. */
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 0.5f);
        sb_text(x + 2.0f,y + 2.0f,message,ink_scale,0.0f,0.0f,0.0f);
        sb_text(x,y,message,ink_scale,1.0f,0.1f,0.1f);
        glDisable(GL_ALPHA_TEST);
        end_overlay();
    }
    if (!g_optOpen && !g_gameOver.valid) draw_cursor_top(fbw,fbh,false,0.0f,true);
    gmw_frame(fbw,fbh);
    fxp_draw_stats(fbw,fbh);
    fxp_draw(fbw,fbh);
}

static void dos2d_free(void)
{
    dos2d_flush_textures();
    g_dos2dShapes.clear();
    g_dos2dTdPal = 0;
    if (g_dos2dShimmerTex) { glDeleteTextures(1, &g_dos2dShimmerTex); g_dos2dShimmerTex = 0; }
    for (size_t i = 0; i < g_raTiles.size(); i++)
        if (g_raTiles[i].texture) glDeleteTextures(1, &g_raTiles[i].texture);
    g_raTiles.clear();
    g_raTileIndex.clear();
    g_raTerrainLoad = 0;
    g_raStaticReady = 0;
    g_raStaticScene.clear();
    g_raLayersReported = 0;
    g_dos2dLoad = 0;
    g_dos2dMissing = 0;
}

#endif
