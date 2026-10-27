/* ra3d_terrain.h -- RED ALERT'S GROUND under Enhanced.
 *
 * Under Enhanced the Red Alert module draws its world in the shared 3D path, and that
 * path's ground used to be a placeholder: the Tiberian Dawn pack the module loads so the
 * renderer has a pack at all, 64 cells wide on Red Alert's 128-cell maps. This replaces
 * the placeholder's ground (its sea, its terrain, its coast, its ground detail) with the
 * Red Alert mission's own map, and nothing else: everything the mode draws on the ground
 * (the infantry, the buildings and vehicles, the effects, the shroud) still draws, on it.
 *
 * THE MAP is the one the engine exports (GAME_STATE_STATIC_MAP: per cell the template's
 * INI name and the icon within it, the mission rectangle and the theater), laid flat on
 * the world grid at Red Alert's own cell coordinates.
 *
 * THE SHEET. terrainenh_RA_<TH>.pack (in the module's ra3d/ folder) carries one 144-texel
 * RGB painting per (template name, icon, variant), with the template's size in cells.
 * Every key in the pack gets a slot of its own in one sheet, laid out like Tiberian
 * Dawn's Enhanced sheet (each cell with a gutter of its own edge, one texel for each of
 * the 1995 cell's 24, the sheet at its own size rather than a power of two). Red Alert
 * has no cartridge bank, so the slots come from the pack's keys rather than a bank table.
 * A cell the map uses and the pack lacks takes the 1995 tile out of ra3d/terrain.pack,
 * enlarged the way Tiberian Dawn's sheet enlarges its own 1995 fallback, so there are no
 * holes. The 1995 set (the DOS texset, and Interior, which has no Enhanced set) is the
 * same sheet built from those tiles alone at 24 texels a cell.
 *
 * THE BUFFERS. What is drawn from the sheet (the ground, the sea floor, the join strips,
 * the normal pass) is kept on the GPU per sheet, and each cell's light is sent again only
 * when it changes: see "THE GROUND KEPT ON THE GPU" below.
 *
 * A variant is chosen per PLACEMENT by the same rule as Tiberian Dawn's
 * (enh_variant_pick): the placement's corner counted in template widths and heights.
 *
 * THE JOIN BLEND. terrainseam_RA_<TH>.pack carries the strips, drawn by the same code as
 * Tiberian Dawn's (terrain_seam.h): the right or lower piece's left or top strip over its
 * neighbour, one direction per join, a feature's strip never over a cliff, per variant.
 * The clear ground (CLEAR1) takes no strip, as Tiberian Dawn's grass takes none.
 *
 * WATER is Tiberian Dawn's Enhanced sea (water_mod.h), on Red Alert's map. Red Alert's
 * templates carry no alpha, so the cut is read off the 1995 tiles: a texel of a template
 * that can hold water (the open sea W1 and W2, the shores SH, the sea cliffs WC, the
 * rivers RV and river cliffs RC, the bridges, the fords, the falls and F01..F06) is water
 * where its palette index is one of the sea's (RA3D_WATER_INDEX below), cleaned of single
 * stray texels. Each slot the map draws carries that cut in its alpha, bilinear up to the
 * cell, the land solid and the painted water fading out into the sea over the width of
 * Tiberian Dawn's soft coast. When the sea is drawn the ground is cut and blended there
 * over it, with the same painted ground under it as the sea floor (ra3d_draw_floor); the
 * sea's base texture is Red Alert's own open-water painting (W2, else W1) from the same
 * set. When it is not (water_fx off, Interior, the chain off) the ground draws opaque as
 * it always did and the painted water shows. Surface ships leave Tiberian Dawn's boats'
 * wake (water_is_boat). The cut is the 1995 tile's: where a 144-texel painting puts its
 * coast somewhere else, its painted water stays opaque on the land side of the cut, and
 * on the water side the sea yields to the painting wherever the painting is not blue
 * (water_mod.h). The sea's hue is the painting's own: C&C's water_natural is not
 * applied to it.
 *
 * THE HEIGHTS are the mission's own scene's (ra3d/3d/<SCEN>.pack, its auto heightmap on
 * Red Alert's 128-cell grid, read through terrain_corner_y), and the ground is lit as
 * Tiberian Dawn's Enhanced ground is: the shroud's corner light times the level ground's
 * 160/255, with the per-pixel sun of the post chain over it from the scene's corner
 * normal (one sun, ra3d_one_sun); the console's own corner shade off those heights
 * (g_shadeGrid) only where the post chain does not shade the ground. A mission
 * with no scene of its own draws flat at 0 (ra3d_flat_ground in cnc_eyes.cpp).
 *
 * Tier 2 only, like the Tiberian Dawn Enhanced terrain it follows: Win98 has no Red Alert
 * module at all.
 */
#ifndef OPENCNC3D_RA3D_TERRAIN_H
#define OPENCNC3D_RA3D_TERRAIN_H

#define RA3D_ENH_MAGIC  "C3DRA144"
/* GL 1.4's automatic mipmaps, which the Windows headers (GL 1.1) do not name */
#define RA3D_GENERATE_MIPMAP 0x8191
#define RA3D_TEXTURE_LOD_BIAS 0x8501
#define RA3D_SEAM_MAGIC "C3DRASEM"

struct Ra3dSlot { int name, icon, var, w, h; };

struct Ra3dGround {
    bool built = false, ok = false;
    std::string key;                 /* theater, set and scenario the sheet was built for */
    char theater = 0;                /* 'S' snow, 'T' temperate, 'I' interior          */
    int set = -1;                    /* FX_TEX_ENH or FX_TEX_DOS, as built             */
    GLuint tex = 0, dryTex = 0, drySeamL = 0, drySeamT = 0;
    std::map<int,std::vector<unsigned char> > cliffRepair;
    int w = 0, h = 0, ts = 0, gutter = 0, pitch = 0, cols = 1;
    std::vector<Ra3dSlot> slot;
    std::unordered_map<std::string, int> slotOf;   /* "name/icon/var" -> slot           */
    std::vector<std::string> names;
    std::unordered_map<std::string, int> nameOf;
    int clearName = -1;
    std::vector<int> cellBase;       /* per map cell: its first painting's slot          */
    std::vector<unsigned char> varHas;   /* per slot: bit v set if variant v exists      */
    std::vector<int> varSlot;        /* slot * TS_VARS + v -> the variant's slot, or -1  */
    std::vector<unsigned char> avg;  /* per slot: its painting's mean colour, for the radar */
    std::vector<unsigned char> wet;  /* per slot: 1 when its cut holds water          */
    int fromPack = 0, fromTiles = 0, missing = 0, variants = 0, coast = 0;
    int wetSlots = 0;
};
static Ra3dGround g_ra3d;
static TerrainSeams g_ra3dSeams;     /* indexed by Red Alert slot and template name      */
static bool g_ra3dSeamsTried = false;
/* Moves every time a sheet is built: the ground's buffers (below) are laid out from the
   sheet's slots, its water cells and its strips, and are stale the moment it is rebuilt. */
static unsigned g_ra3dSheetGen = 0;
static void ra3d_cache_free(void);

/* 'S' snow, 'T' temperate, 'I' interior (exported as NONE, see ra2d_pal_index) */
static char ra3d_theater(void) { return ra2d_theater_letter(); }
static const char* ra3d_theater_name(char th)
{
    return th == 'S' ? "SNOW" : th == 'I' ? "INTERIOR" : "TEMPERAT";
}

/* The template's INI name out of the engine's export ("D01_i3.tga" -> "D01"). */
static void ra3d_template_name(const char* exported, char out[17])
{
    const char* suffix = strstr(exported, "_i");
    size_t len = suffix ? (size_t)(suffix - exported) : strcspn(exported, ".");
    if (len > 16) len = 16;
    memcpy(out, exported, len);
    out[len] = 0;
}

static std::string ra3d_key(const char* name, int icon, int var)
{
    return std::string(name) + "/" + std::to_string(icon) + "/" + std::to_string(var);
}

static int ra3d_name_id(const char* name)
{
    std::unordered_map<std::string, int>::const_iterator it = g_ra3d.nameOf.find(name);
    if (it != g_ra3d.nameOf.end()) return it->second;
    const int id = (int)g_ra3d.names.size();
    g_ra3d.names.push_back(name);
    g_ra3d.nameOf[name] = id;
    return id;
}

/* A pack beside the module, in the repository's art folder for a repository run, or
   named outright by an environment variable ("off" turns it off). */
static const char* ra3d_pack_path(const char* env, const char* stem, char th,
                                  char* buf, size_t n)
{
    const char* e = getenv(env);
    if (e && (!strcmp(e, "off") || !strcmp(e, "none"))) return NULL;
    if (e && *e) return e;
    const char* TH = ra3d_theater_name(th);
    for (int w = 0; w < 3; w++) {
        if (w == 0) snprintf(buf, n, "ra3d/%s_RA_%s.pack", stem, TH);
        else snprintf(buf, n, "%sart/remaster/textures/RA_%s/%s_RA_%s.pack",
                      w == 2 ? "../" : "", TH, stem, TH);
        if (rt_override_exists(buf)) return buf;
    }
    return NULL;
}

/* Whether the Enhanced set exists for the mission's theater (the Visuals dialog greys
   the entry otherwise). */
static int ra3d_enh_available(void)
{
    char buf[256];
    const char th = ra3d_theater();
    if (!th || th == 'I') return 0;
    return ra3d_pack_path("OPENCNC3D_RATERRAINENH", "terrainenh", th, buf, sizeof buf) != NULL;
}

static std::vector<unsigned char> ra3d_read_file(const char* path)
{
    std::vector<unsigned char> d;
    FILE* f = fopen(path, "rb");
    if (!f) return d;
    if (fseek(f, 0, SEEK_END) == 0) {
        const long n = ftell(f);
        if (n > 0 && n < 400L * 1024 * 1024 && fseek(f, 0, SEEK_SET) == 0) {
            d.resize((size_t)n);
            if (fread(&d[0], 1, d.size(), f) != d.size()) d.clear();
        }
    }
    fclose(f);
    return d;
}

/* One slot's RGB and alpha into the RGBA sheet, its gutter replicated outward
   (rt_place's layout, and the slot's column out of this sheet's own width). A NULL alpha
   is solid ground. */
static void ra3d_place(std::vector<unsigned char>& sheet, int slot, const unsigned char* rgb,
                       const unsigned char* alpha)
{
    const Ra3dGround& G = g_ra3d;
    const int x0 = (slot % G.cols) * G.pitch + G.gutter, y0 = (slot / G.cols) * G.pitch + G.gutter;
    const int ts = G.ts, g = G.gutter;
    for (int y = -g; y < ts + g; y++) {
        const int sy = y < 0 ? 0 : y >= ts ? ts - 1 : y;
        unsigned char* d = &sheet[((size_t)(y0 + y) * G.w + x0 - g) * 4];
        for (int x = -g; x < ts + g; x++) {
            const int sx = x < 0 ? 0 : x >= ts ? ts - 1 : x;
            memcpy(d, rgb + ((size_t)sy * ts + sx) * 3, 3);
            d[3] = alpha ? alpha[(size_t)sy * ts + sx] : 255;
            d += 4;
        }
    }
}

/* The 1995 tile for a template cell, RGB at 24 texels, or false. */
static bool ra3d_tile_rgb(char th, const char* name, int icon, unsigned char out[24 * 24 * 3])
{
    char exported[40];
    snprintf(exported, sizeof exported, "%s_i%d.tga", name, icon);
    const RaClassicTile* t = ra2d_find_tile(th, exported, icon);
    if (!t) return false;
    const unsigned char* pal = g_raTilePal[th == 'S' ? 0 : th == 'I' ? 2 : 1];
    for (int i = 0; i < 24 * 24; i++) memcpy(out + i * 3, pal + t->pixels[i] * 3, 3);
    return true;
}

/* A painting's mean colour, every eighth texel each way. */
static void ra3d_mean(const unsigned char* rgb, int ts, unsigned char out[3])
{
    long sum[3] = { 0, 0, 0 }, n = 0;
    for (int y = 0; y < ts; y += 8)
        for (int x = 0; x < ts; x += 8, n++)
            for (int c = 0; c < 3; c++) sum[c] += rgb[((size_t)y * ts + x) * 3 + c];
    for (int c = 0; c < 3; c++) out[c] = (unsigned char)(n ? sum[c] / n : 0);
}

/* ---- THE WATER CUT ------------------------------------------------------------------ */

/* The templates that can hold water at all. The palette's water colours also turn up,
   a texel here and there, in snow shadows on cliffs and in road dust; gating by template
   keeps every one of those land. Interior has none of these. */
static bool ra3d_water_family(const char* n)
{
    static const char* const pre[] = { "W1", "W2", "SH", "WC", "RV", "RC", "BR", "F0",
                                       "FALLS", "FORD", NULL };
    for (int i = 0; pre[i]; i++)
        if (!strncmp(n, pre[i], strlen(pre[i]))) return true;
    return false;
}

/* THE SEA'S PALETTE INDICES, the same in the snow, temperate and interior palettes:
   46, 47 and 62..67 and 72 are the open water's blues and greys, 96..102 the seven
   colours the 1996 game cycles for the glint, 167 and 176..180 the shallow blue-greys a
   shore and a river lay along the bank. Measured over every template of both theaters:
   with the template gate above, no road, slope, cliff or clear piece is cut. */
static bool ra3d_water_index(unsigned char i)
{
    return i == 46 || i == 47 || (i >= 62 && i <= 67) || i == 72 || (i >= 96 && i <= 102)
        || i == 167 || (i >= 176 && i <= 180);
}

/* A 3x3 majority (the tile's edge replicated outward): what the shore art dithers into
   the sand and the sea flecks into the surf goes to whichever side surrounds it. */
static void ra3d_cut_majority(unsigned char m[576])
{
    unsigned char o[576];
    for (int y = 0; y < 24; y++)
        for (int x = 0; x < 24; x++) {
            int n = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    const int yy = y + dy < 0 ? 0 : y + dy > 23 ? 23 : y + dy;
                    const int xx = x + dx < 0 ? 0 : x + dx > 23 ? 23 : x + dx;
                    n += m[yy * 24 + xx];
                }
            o[y * 24 + x] = n >= 5 ? 1 : 0;
        }
    memcpy(m, o, 576);
}

/* Islands of `val` smaller than eight texels that do not reach the tile's edge become
   the other side: a speck of sea in the snow, a fleck of foam out at sea. One that
   reaches the edge may continue into the next cell, so it stays. */
static void ra3d_cut_specks(unsigned char m[576], unsigned char val)
{
    unsigned char seen[576];
    memset(seen, 0, sizeof seen);
    int stack[576], comp[576];
    for (int s0 = 0; s0 < 576; s0++) {
        if (m[s0] != val || seen[s0]) continue;
        int sp = 0, nc = 0;
        bool edge = false;
        stack[sp++] = s0; seen[s0] = 1;
        while (sp) {
            const int c = stack[--sp];
            comp[nc++] = c;
            const int x = c % 24, y = c / 24;
            if (x == 0 || y == 0 || x == 23 || y == 23) edge = true;
            const int nb[4] = { x > 0 ? c - 1 : -1, x < 23 ? c + 1 : -1,
                                y > 0 ? c - 24 : -1, y < 23 ? c + 24 : -1 };
            for (int k = 0; k < 4; k++)
                if (nb[k] >= 0 && !seen[nb[k]] && m[nb[k]] == val) { seen[nb[k]] = 1; stack[sp++] = nb[k]; }
        }
        if (!edge && nc < 8)
            for (int k = 0; k < nc; k++) m[comp[k]] = val ? 0 : 1;
    }
}

/* The water in one template cell's 1995 tile: 1 water, 0 land; the count of water texels. */
static int ra3d_water_cut(char th, const char* name, int icon, unsigned char out[576])
{
    memset(out, 0, 576);
    if (th == 'I' || !ra3d_water_family(name)) return 0;
    char exported[40];
    snprintf(exported, sizeof exported, "%s_i%d.tga", name, icon);
    const RaClassicTile* t = ra2d_find_tile(th, exported, icon);
    if (!t) return 0;
    for (int i = 0; i < 576; i++) out[i] = ra3d_water_index(t->pixels[i]) ? 1 : 0;
    ra3d_cut_majority(out);
    ra3d_cut_majority(out);
    ra3d_cut_specks(out, 1);
    ra3d_cut_specks(out, 0);
    int n = 0;
    for (int i = 0; i < 576; i++) n += out[i];
    return n;
}

/* A slot's alpha at the sheet's cell size: the cut bilinear up to it (rt_alpha_up's
   sampling), thresholded at the half, and ramped from that line out over the water by
   WATER_SOFT_RADIUS texels of the 1995 cell, scaled to this one: the width of Tiberian
   Dawn's soft coast, laid on the water side, where Red Alert's texels are its painted
   water (water_soft_ramp says why). */
static void ra3d_water_alpha(const unsigned char cut[576], int ts, unsigned char* alpha)
{
    std::vector<unsigned char> solid((size_t)ts * ts), hole((size_t)ts * ts);
    for (int y = 0; y < ts; y++) {
        const float fy = ((float)y + 0.5f) * 24.0f / (float)ts - 0.5f;
        const int y0 = (int)(fy < 0 ? 0 : fy), y1 = y0 + 1 < 24 ? y0 + 1 : 23;
        const float wy = fy - (float)y0 < 0 ? 0.0f : fy - (float)y0;
        for (int x = 0; x < ts; x++) {
            const float fx = ((float)x + 0.5f) * 24.0f / (float)ts - 0.5f;
            const int x0 = (int)(fx < 0 ? 0 : fx), x1 = x0 + 1 < 24 ? x0 + 1 : 23;
            const float wx = fx - (float)x0 < 0 ? 0.0f : fx - (float)x0;
            const float a00 = cut[y0 * 24 + x0] ? 0.0f : 255.0f, a01 = cut[y0 * 24 + x1] ? 0.0f : 255.0f;
            const float a10 = cut[y1 * 24 + x0] ? 0.0f : 255.0f, a11 = cut[y1 * 24 + x1] ? 0.0f : 255.0f;
            const float top = a00 + (a01 - a00) * wx, bot = a10 + (a11 - a10) * wx;
            const unsigned char a = (unsigned char)(top + (bot - top) * wy + 0.5f);
            solid[(size_t)y * ts + x] = a >= 128 ? 1 : 0;
            hole[(size_t)y * ts + x] = a >= 128 ? 0 : 1;
        }
    }
    water_soft_ramp(&solid[0], &hole[0], ts, ts, WATER_SOFT_RADIUS * (float)ts / 24.0f,
                    alpha, (size_t)ts, 1, true);
}


/* Snow's 144px shore paintings move the water edge relative to the 24px native
   cut. Keep that original cut authoritative, then remove only painted blue/foam
   connected to its water. Work across the complete template, including icon
   boundaries, so a cell edge cannot terminate a mask or its feather. The saved
   Snow rock matte is deliberately NOT a land mask: it includes painted water. */
static void ra3d_snow_coast_alpha(const Ra3dGround& G,
        const std::vector<const unsigned char*>& pixels,
        const std::vector<unsigned char>& used,
        std::vector<std::vector<unsigned char> >& result,
        std::vector<std::vector<unsigned char> >* featherRGB = NULL)
{
    std::map<std::string,std::vector<int> > groups;
    for(size_t s=0;s<G.slot.size();s++) {
        const Ra3dSlot& sl=G.slot[s];const char* name=G.names[sl.name].c_str();
        if(pixels[s] && ra3d_water_family(name))groups[std::string(name)+"/"+std::to_string(sl.var)].push_back((int)s);
    }
    long extra=0,nativeCount=0,extendedRGB=0;int authored=0;
    for(const auto& group:groups) {
        bool needed=false;int cw=1,ch=1;
        for(int s:group.second){needed|=used[s]!=0;cw=std::max(cw,G.slot[s].w);ch=std::max(ch,G.slot[s].h);}
        if(!needed || cw>16 || ch>16)continue;
        const int w=cw*G.ts,h=ch*G.ts;const size_t size=(size_t)w*h;
        std::vector<unsigned char> candidate(size,0),wet(size,0),solid(size,1),alpha(size,255),occupied(size,0);
        std::vector<int> queue;queue.reserve(size/2);
        for(int slot:group.second) {
            const Ra3dSlot& sl=G.slot[slot];if(sl.icon<0 || sl.icon>=cw*ch)continue;
            unsigned char cut[576];ra3d_water_cut('S',G.names[sl.name].c_str(),sl.icon,cut);
            const int x0=(sl.icon%cw)*G.ts,y0=(sl.icon/cw)*G.ts;
            for(int y=0;y<G.ts;y++)for(int x=0;x<G.ts;x++) {
                const size_t at=(size_t)(y0+y)*w+x0+x,src=((size_t)y*G.ts+x)*3;
                occupied[at]=1;
                const unsigned char* rgb=pixels[slot]+src;
                const bool native=cut[(y*24/G.ts)*24+x*24/G.ts]!=0;
                // The template/native seed owns topology; chroma only extends a
                // connected water component, never classifies arbitrary ground.
                const int r=rgb[0],g=rgb[1],b=rgb[2];
                const bool blue=b-r>=8 && b-g>=-6;
                const bool foam=b-r>=2 && b-g>=0 && r+g+b>=540;
                candidate[at]=(native || blue || foam)?1:0;
                if(native){wet[at]=1;queue.push_back((int)at);nativeCount++;}
            }
        }
        for(size_t q=0;q<queue.size();q++) {
            const int at=queue[q],x=at%w,y=at/w;
            const int nb[4]={x?at-1:-1,x+1<w?at+1:-1,y?at-w:-1,y+1<h?at+w:-1};
            for(int k=0;k<4;k++)if(nb[k]>=0 && candidate[nb[k]] && !wet[nb[k]]){
                wet[nb[k]]=1;queue.push_back(nb[k]);extra++;
            }
        }
        // Missing native icons are open continuation, not an invented solid wall
        // at the edge of a used water icon. Apply after the connected colour flood.
        for(size_t j=0;j<size;j++){if(!occupied[j])wet[j]=1;solid[j]=wet[j]?0:1;}
        water_soft_ramp(solid.data(),wet.data(),w,h,WATER_SOFT_RADIUS*(float)G.ts/24.0f,
                        alpha.data(),(size_t)w,1,true);
        /* The wet-side colour feather is terrain showing through translucent
           alpha. Its RGB must extend the adjacent land, not a static blue water
           painting on the bank's raised geometry. Work across the same complete
           template as the alpha, preserving opaque land and fully clear water
           pixels (the latter remain the original painted sea floor). */
        std::vector<int> nearLand, slotForIcon;
        if (featherRGB) {
            bool hasLand=false;for(size_t j=0;j<size;j++)hasLand|=solid[j]&&occupied[j];
            if (hasLand) {
                std::vector<float> distance(size);nearLand.resize(size);
                water_edt_2d(solid.data(),distance.data(),nearLand.data(),w,h);
                slotForIcon.assign((size_t)cw*ch,-1);
                for(int slot:group.second)if(G.slot[slot].icon>=0&&G.slot[slot].icon<cw*ch)
                    slotForIcon[G.slot[slot].icon]=slot;
            }
        }
        for(int slot:group.second)if(used[slot]) {
            const Ra3dSlot& sl=G.slot[slot];if(sl.icon<0 || sl.icon>=cw*ch)continue;
            const int x0=(sl.icon%cw)*G.ts,y0=(sl.icon/cw)*G.ts;
            result[slot].resize((size_t)G.ts*G.ts);
            for(int y=0;y<G.ts;y++)memcpy(result[slot].data()+(size_t)y*G.ts,
                alpha.data()+(size_t)(y0+y)*w+x0,G.ts);
            if(featherRGB && !nearLand.empty())for(int y=0;y<G.ts;y++)for(int x=0;x<G.ts;x++) {
                const size_t at=(size_t)(y0+y)*w+x0+x;
                if(!alpha[at] || alpha[at]>=128)continue;
                const int nearest=nearLand[at];if(nearest<0)continue;
                const int lx=nearest%w,ly=nearest/w,landSlot=slotForIcon[(ly/G.ts)*cw+lx/G.ts];
                if(landSlot<0)continue;
                std::vector<unsigned char>& rgb=(*featherRGB)[slot];
                if(rgb.empty())rgb.assign(pixels[slot],pixels[slot]+(size_t)G.ts*G.ts*3);
                memcpy(rgb.data()+((size_t)y*G.ts+x)*3,
                       pixels[landSlot]+((size_t)(ly%G.ts)*G.ts+lx%G.ts)*3,3);
                extendedRGB++;
            }
            authored++;
        }
    }
    fprintf(stderr,"RA3D|snow-coast|native-water=%ld|connected-painted-water=%ld|cells=%d|whole-template=1|feather-rgb-pixels=%ld\n",nativeCount,extra,authored,extendedRGB);
}

#include "ra3d_cliff_material.h"

/* Repair each complete shore-cliff template, including cell/variant boundaries.
   The original sheet remains available for water-FX-off and Classic rendering. */
static void ra3d_cliff_material_sheet(Ra3dGround& G,std::vector<unsigned char>& sheet)
{
    std::map<std::string,std::vector<int> > groups;
    for(size_t i=0;i<G.slot.size();i++){const auto& sl=G.slot[i];const char* n=G.names[sl.name].c_str();
        if(!strncmp(n,"WC",2)||!strncmp(n,"RC",2))groups[std::string(n)+"/"+std::to_string(sl.var)].push_back((int)i);}
    size_t changed=0;int repairedTemplates=0;
    for(const auto& g:groups){int cw=1,ch=1;for(int slot:g.second){cw=std::max(cw,G.slot[slot].w);ch=std::max(ch,G.slot[slot].h);}if(cw>16||ch>16)continue;
        const int w=cw*G.ts,h=ch*G.ts;std::vector<unsigned char> rgb((size_t)w*h*3,0),wet((size_t)w*h,1);
        for(int slot:g.second){const auto& sl=G.slot[slot];if(sl.icon<0||sl.icon>=cw*ch)continue;unsigned char cut[576];ra3d_water_cut(G.theater,G.names[sl.name].c_str(),sl.icon,cut);
            const int sx=(slot%G.cols)*G.pitch+G.gutter,sy=(slot/G.cols)*G.pitch+G.gutter,ox=(sl.icon%cw)*G.ts,oy=(sl.icon/cw)*G.ts;
            for(int y=0;y<G.ts;y++)for(int x=0;x<G.ts;x++){const size_t at=(size_t)(oy+y)*w+ox+x;memcpy(&rgb[at*3],&sheet[((size_t)(sy+y)*G.w+sx+x)*4],3);wet[at]=cut[(y*24/G.ts)*24+x*24/G.ts];}}
        std::vector<unsigned char> mask;const size_t n=ra3d_cliff_dry_material(rgb,wet,w,h,G.ts/24,&mask);if(!n)continue;changed+=n;repairedTemplates++;
        for(int slot:g.second){const auto& sl=G.slot[slot];if(sl.icon<0||sl.icon>=cw*ch)continue;
            const int sx=(slot%G.cols)*G.pitch+G.gutter,sy=(slot/G.cols)*G.pitch+G.gutter,ox=(sl.icon%cw)*G.ts,oy=(sl.icon/cw)*G.ts;
            std::vector<unsigned char>& repaired=G.cliffRepair[slot];repaired.resize((size_t)G.ts*G.ts);
            for(int y=0;y<G.ts;y++)for(int x=0;x<G.ts;x++)repaired[(size_t)y*G.ts+x]=mask[(size_t)(oy+y)*w+ox+x];
            for(int y=-G.gutter;y<G.ts+G.gutter;y++)for(int x=-G.gutter;x<G.ts+G.gutter;x++){const int px=std::max(0,std::min(G.ts-1,x)),py=std::max(0,std::min(G.ts-1,y));
                memcpy(&sheet[((size_t)(sy+y)*G.w+sx+x)*4],&rgb[((size_t)(oy+py)*w+ox+px)*3],3);}}
    }
    fprintf(stderr,"RA3D|cliff-material|theater=%c|templates=%d|repaired-rgb=%zu|alpha-unchanged=1|whole-template=1\n",G.theater,repairedTemplates,changed);
}

/* Extend actual opaque bank material through the complete native colour feather.
   Unused slots deliberately have no prepared coast alpha, so their painted water
   must not masquerade as an opaque donor. Their missing logical icons stay absent;
   available icons on either side of a cell join still share one template solve. */
static void ra3d_coast_feather_sheet(const Ra3dGround& G,
        const std::vector<unsigned char>& used,std::vector<unsigned char>& sheet)
{
    std::map<std::string,std::vector<int> > groups;
    for(size_t s=0;s<G.slot.size();++s) {
        const Ra3dSlot& sl=G.slot[s];
        if(used[s] && ra3d_water_family(G.names[sl.name].c_str()))
            groups[std::to_string(sl.name)+"/"+std::to_string(sl.var)].push_back((int)s);
    }
    size_t changed=0,targets=0;int templates=0;
    for(const auto& group:groups) {
        int cw=1,ch=1;
        for(int s:group.second){cw=std::max(cw,G.slot[s].w);ch=std::max(ch,G.slot[s].h);}
        if(cw>16 || ch>16)continue;
        std::vector<WaterCoastFeatherRegion> regions;
        for(int s:group.second) {
            const Ra3dSlot& sl=G.slot[s];if(sl.icon<0 || sl.icon>=cw*ch)continue;
            const int x=(s%G.cols)*G.pitch+G.gutter,y=(s/G.cols)*G.pitch+G.gutter;
            unsigned char* px=&sheet[((size_t)y*G.w+x)*4];
            regions.push_back({px,px+3,(size_t)G.w*4,(size_t)G.w*4,4,4,
                               (sl.icon%cw)*G.ts,(sl.icon/cw)*G.ts,G.ts,G.ts});
            for(int yy=0;yy<G.ts;++yy)for(int xx=0;xx<G.ts;++xx) {
                const unsigned char a=px[((size_t)yy*G.w+xx)*4+3];targets+=a>0&&a<255;
            }
        }
        const size_t count=water_coast_feather_template(regions,cw*G.ts,ch*G.ts);
        if(!count)continue;
        changed+=count;++templates;
        for(int s:group.second)water_coast_gutter(sheet,G.w,G.h,
            (s%G.cols)*G.pitch+G.gutter,(s/G.cols)*G.pitch+G.gutter,G.ts,G.ts);
    }
    fprintf(stderr,"RA3D|coast-feather|theater=%c|templates=%d|rgb-pixels=%zu|unresolved=%zu|whole-template=1|alpha-and-opaque-unchanged=1\n",
            G.theater,templates,changed,targets-changed);
}

static void ra3d_free(void)
{
    ra3d_cache_free();
    if (g_ra3d.tex) glDeleteTextures(1, &g_ra3d.tex);
    if (g_ra3d.dryTex) glDeleteTextures(1,&g_ra3d.dryTex);
    if (g_ra3d.drySeamL) glDeleteTextures(1,&g_ra3d.drySeamL);
    if (g_ra3d.drySeamT) glDeleteTextures(1,&g_ra3d.drySeamT);
    g_ra3d = Ra3dGround();
    /* the sea's source goes with the sheet; its one white texel is kept for the next */
    if (g_waterRa.base) glDeleteTextures(1, &g_waterRa.base);
    const GLuint white = g_waterRa.white;
    g_waterRa = WaterRaSource();
    g_waterRa.white = white;
    if (g_ra3dSeams.texL) glDeleteTextures(1, &g_ra3dSeams.texL);
    if (g_ra3dSeams.texT) glDeleteTextures(1, &g_ra3dSeams.texT);
    g_ra3dSeams = TerrainSeams();
    g_ra3dSeamsTried = false;
}

/* The set this frame asks for: Enhanced's own when its pack is there, the 1995 tiles
   otherwise (Red Alert has no cartridge art and no Remastered terrain). */
static int ra3d_set_wanted(void)
{
    return (fx_texset_get() == FX_TEX_ENH && ra3d_theater() != 'I' && ra3d_enh_available())
           ? FX_TEX_ENH : FX_TEX_DOS;
}

static void ra3d_seams_load(const std::vector<unsigned char>* cleanSheet=NULL);

static int ra3d_cell_slot(int i, int x, int y);
static void ra3d_slot_uv(int slot, float* u0, float* v0, float* u1, float* v1);

/* THE SEA'S BASE: the open water Red Alert's ground paints, as one repeating texture.
   W2 is the 2x2 sea (icons 0..3 left to right, top to bottom), so its four paintings
   make one 2-cell tile; a set without it takes W1, one cell. Each painting is the set's
   own (the pack's at its size, else the 1995 tile enlarged), as the ground draws it. */
static bool ra3d_water_base(const std::vector<const unsigned char*>& pixels,
                            std::vector<unsigned char>& out, int* side, const char** what)
{
    const Ra3dGround& G = g_ra3d;
    const int ts = G.ts;
    std::vector<unsigned char> tile(24 * 24 * 3), rgb((size_t)ts * ts * 3);
    auto painting = [&](const char* name, int icon, int* fromPack) -> bool {
        std::unordered_map<std::string, int>::const_iterator it = G.slotOf.find(ra3d_key(name, icon, 0));
        if (it != G.slotOf.end() && pixels[it->second]) {
            memcpy(&rgb[0], pixels[it->second], rgb.size());
            (*fromPack)++;
            return true;
        }
        if (!ra3d_tile_rgb(G.theater, name, icon, &tile[0])) return false;
        if (ts == 24) memcpy(&rgb[0], &tile[0], tile.size());
        else rt_bilinear_rgb(&tile[0], 24, 24, 24 * 3, &rgb[0], ts, ts);
        return true;
    };
    int fromPack = 0;
    out.assign((size_t)4 * ts * ts * 3, 0);
    bool w2 = true;
    for (int i = 0; i < 4 && w2; i++) {
        if (!painting("W2", i, &fromPack)) { w2 = false; break; }
        const int ox = (i & 1) * ts, oy = (i >> 1) * ts;
        for (int y = 0; y < ts; y++)
            memcpy(&out[((size_t)(oy + y) * 2 * ts + ox) * 3], &rgb[(size_t)y * ts * 3], (size_t)ts * 3);
    }
    if (w2) {
        *side = 2 * ts;
        *what = fromPack == 4 ? "W2 painted" : "W2 1995";
        return true;
    }
    fromPack = 0;
    if (!painting("W1", 0, &fromPack)) return false;
    out.assign(rgb.begin(), rgb.end());
    *side = ts;
    *what = fromPack ? "W1 painted" : "W1 1995";
    return true;
}

/* HOW MUCH OF THE THEATER CAN HOLD WATER: every template and icon of its 1995 set run
   through the cut, once per theater (the gate's and the log reader's count). */
static void ra3d_water_census(char th)
{
    static std::string done;
    if (done.find(th) != std::string::npos) return;
    done += th;
    std::set<std::string> names;
    int icons = 0, total = 0;
    unsigned char cut[576];
    for (size_t i = 0; i < g_raTiles.size(); i++) {
        const RaClassicTile& t = g_raTiles[i];
        if (t.theater != th) continue;
        total++;
        if (ra3d_water_cut(th, t.name, t.icon, cut) > 0) { icons++; names.insert(t.name); }
    }
    fprintf(stderr, "WATER|ra|cut|theater=%s|templates=%d|icons=%d|of=%d\n",
            ra3d_theater_name(th), (int)names.size(), icons, total);
}

/* THE SEA'S SOURCE (water_mod.h's g_waterRa), filled from the sheet just built: the
   map's cells with their rectangles and holes, the sheet, the base and its grain. The
   cut atlas was laid with the sheet. No sea on Interior, on a map with no water, or on a
   mission drawn flat on the placeholder pack (its grid is not this map's). */
static void ra3d_water_source(const std::vector<const unsigned char*>& pixels)
{
    const Ra3dGround& G = g_ra3d;
    WaterRaSource& WR = g_waterRa;
    WR.ok = false;
    ra3d_water_census(G.theater);
    if (G.theater == 'I') { fprintf(stderr, "WATER|ra|none|theater=INTERIOR\n"); return; }
    if (ra3d_flat_ground()) { fprintf(stderr, "WATER|ra|none|no scene of its own\n"); return; }
    const int W = g_raStaticMap.MapCellWidth, H = g_raStaticMap.MapCellHeight;
    WR.cell.clear();WR.grassMasks.clear();WR.swashRivers.clear();WR.bedCliffs.clear();WR.bedMasks.clear();
    WR.cell.reserve((size_t)W * H);
    WR.wetCells = 0;
    for (int i = 0; i < W * H; i++) {
        const int cx = g_raStaticMap.MapCellX + i % W, cy = g_raStaticMap.MapCellY + i / W;
        const int base = G.cellBase[i];
        if (base < 0 || cx < 0 || cy < 0 || cx >= g_gridW || cy >= g_gridH) continue;
        PackCell c;
        memset(&c, 0, sizeof c);
        c.x = (short)cx; c.y = (short)cy;
        c.holes = G.wet[base];
        ra3d_slot_uv(ra3d_cell_slot(i, cx, cy), &c.u0, &c.v0, &c.u1, &c.v1);
        WR.cell.push_back(c);
        const int picked=ra3d_cell_slot(i,cx,cy);int grassMask=-1;
        if(picked>=0 && G.theater=='T' && G.set==FX_TEX_ENH) {
            const Ra3dSlot& sl=G.slot[picked];const char* name=G.names[sl.name].c_str();
            grassMask=!strcmp(name,"CLEAR1")?0:terrain_grass_mask_ra(name,sl.icon,sl.var);
        }
        WR.grassMasks.push_back(grassMask);
        const char* waterName=picked>=0?G.names[G.slot[picked].name].c_str():"";
        WR.swashRivers.push_back(!strncmp(waterName,"RV",2) || !strncmp(waterName,"FORD",4) ||
                                !strncmp(waterName,"FALLS",5) || !strncmp(waterName,"BRIDGE",6) ||
                                !strncmp(waterName,"BR1",3) || !strncmp(waterName,"BR2",3) || !strncmp(waterName,"BR3",3));
        WR.bedCliffs.push_back(!strncmp(waterName,"WC",2) || !strncmp(waterName,"RC",2) ||
                               (waterName[0]=='S' && waterName[1]>='0' && waterName[1]<='9'));
        WR.bedMasks.push_back(picked>=0 && WR.bedCliffs.back()?terrain_grass_mask_ra(waterName,G.slot[picked].icon,G.slot[picked].var):-1);
        WR.wetCells += c.holes;
    }
    if (!WR.wetCells) { fprintf(stderr, "WATER|ra|none|%s has no water\n", g_raStaticScene.c_str()); return; }
    std::vector<unsigned char> base;
    int side = 0;
    const char* what = "";
    if (!ra3d_water_base(pixels, base, &side, &what)) {
        fprintf(stderr, "WATER|ra|none|no open-water painting in this set\n");
        return;
    }
    /* its luminance's mean and spread, as water_grain_measure reads WATER1's */
    double sum = 0.0, sum2 = 0.0;
    const size_t np = (size_t)side * side;
    for (size_t i = 0; i < np; i++) {
        const unsigned char* q = &base[i * 3];
        const double l = (0.299 * q[0] + 0.587 * q[1] + 0.114 * q[2]) / 255.0;
        sum += l; sum2 += l * l;
    }
    const double mean = sum / (double)np, var = sum2 / (double)np - mean * mean;
    const double sd = var > 1.0e-6 ? sqrt(var) : 0.001;
    WR.grainMean = (float)mean;
    WR.grainInv = (float)(1.0 / (2.5 * sd));
    WR.baseRepeats = (float)G.ts / (float)side;     /* a painting is a cell */
    if (WR.base) glDeleteTextures(1, &WR.base);
    glGenTextures(1, &WR.base);
    glBindTexture(GL_TEXTURE_2D, WR.base);
    /* MIPMAPPED: a cell is a few dozen pixels on screen and the painting 144 texels, so
       the slide would shimmer through a plain linear filter */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, RA3D_GENERATE_MIPMAP, GL_TRUE);
    /* a level sharper than the footprint asks for: the painting's ripples are what the
       sea is made of, and the plain choice blurred them to a wash */
    glTexParameterf(GL_TEXTURE_2D, RA3D_TEXTURE_LOD_BIAS, -1.0f);
    GLint align = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &align);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, side, side, 0, GL_RGB, GL_UNSIGNED_BYTE, &base[0]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, align);
    if (!WR.white) {
        const unsigned char px[4] = { 255, 255, 255, 255 };
        glGenTextures(1, &WR.white);
        glBindTexture(GL_TEXTURE_2D, WR.white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    WR.sheet = PackTex();
    WR.sheet.w = WR.sheet.uw = G.w;
    WR.sheet.h = WR.sheet.uh = G.h;
    WR.sheet.gl = G.dryTex?G.dryTex:G.tex;
    WR.sheetKey = 1000 + G.set;
    WR.ok = true;
    fprintf(stderr, "WATER|ra|source|%s|%s|%s|wet_slots=%d|wet_cells=%d|base=%s|%dx%d|repeats=%.2f|"
                    "mean=%.3f|spread=%.3f\n",
            g_raStaticScene.c_str(), ra3d_theater_name(G.theater),
            G.set == FX_TEX_ENH ? "enhanced" : "1995", G.wetSlots, WR.wetCells, what, side, side,
            (double)WR.baseRepeats, mean, sd);
}

/* BUILD THE SHEET for the mission's theater and the set asked for. Once per mission and
   set: a theater's Enhanced pack is about 95 MB, read and laid out in well under a
   second. */
static bool ra3d_ground_ensure(void)
{
    if (!ra2d_load_terrain() || !ra2d_fetch_map()) return false;
    const char th = ra3d_theater();
    const int set = ra3d_set_wanted();
    const std::string key = std::string(1, th) + "/" + std::to_string(set) + "/" + g_raStaticScene;
    if (g_ra3d.built && g_ra3d.key == key) return g_ra3d.ok;
    const Uint64 t0 = SDL_GetPerformanceCounter();
    ra3d_free();
    g_ra3dSheetGen++;
    Ra3dGround& G = g_ra3d;
    G.built = true;
    G.key = key;
    G.theater = th;
    G.set = set;
    G.clearName = ra3d_name_id("CLEAR1");

    /* the pack's keys, each a slot */
    std::vector<unsigned char> pack;
    int cell = 24;
    size_t count = 0;
    const char* packPath = NULL;
    char pbuf[256];
    if (set == FX_TEX_ENH) {
        packPath = ra3d_pack_path("OPENCNC3D_RATERRAINENH", "terrainenh", th, pbuf, sizeof pbuf);
        if (packPath) pack = ra3d_read_file(packPath);
        bool good = pack.size() >= 20 && !memcmp(&pack[0], RA3D_ENH_MAGIC, 8)
                    && rt_le32(&pack[8]) == 1;
        if (good) {
            cell = (int)rt_le32(&pack[12]);
            count = rt_le32(&pack[16]);
            good = cell % 24 == 0 && cell >= 24 && cell <= 24 * RT_ENH_KMAX
                   && pack.size() == 20 + count * 20 + count * (size_t)cell * cell * 3;
        }
        if (!good) {
            fprintf(stderr, "RA3D|enh|%s is not a version 1 Red Alert terrain pack; the 1995 tiles draw\n",
                    packPath ? packPath : "(no pack)");
            pack.clear(); count = 0; cell = 24; G.set = FX_TEX_DOS;
        }
    }
    const int k = cell / 24;
    G.ts = cell; G.gutter = k; G.pitch = cell + 2 * k;
    std::vector<const unsigned char*> pixels;       /* per slot: the pack's RGB, or NULL */
    for (size_t i = 0; i < count; i++) {
        const unsigned char* e = &pack[20 + i * 20];
        char name[17];
        memcpy(name, e, 16); name[16] = 0;
        const int icon = e[16] | (e[17] << 8), var = e[18], sz = e[19];
        if (var >= TS_VARS) continue;
        const std::string kk = ra3d_key(name, icon, var);
        if (G.slotOf.count(kk)) continue;
        Ra3dSlot s;
        s.name = ra3d_name_id(name); s.icon = icon; s.var = var;
        s.w = (sz & 15) + 1; s.h = ((sz >> 4) & 15) + 1;
        G.slotOf[kk] = (int)G.slot.size();
        G.slot.push_back(s);
        pixels.push_back(&pack[20 + count * 20 + i * (size_t)cell * cell * 3]);
        if (var) G.variants++;
    }
    /* the map's cells: each to its first painting's slot, a 1995 tile where the pack has none */
    const int W = g_raStaticMap.MapCellWidth, H = g_raStaticMap.MapCellHeight;
    G.cellBase.assign((size_t)W * H, -1);
    for (int i = 0; i < W * H; i++) {
        const CNCStaticCellStruct& c = g_raStaticMap.StaticCells[i];
        char name[17];
        ra3d_template_name(c.TemplateTypeName, name);
        const std::string kk = ra3d_key(name, c.IconNumber, 0);
        std::unordered_map<std::string, int>::const_iterator it = G.slotOf.find(kk);
        if (it == G.slotOf.end()) {
            Ra3dSlot s;
            s.name = ra3d_name_id(name); s.icon = c.IconNumber; s.var = 0; s.w = s.h = 1;
            G.slotOf[kk] = (int)G.slot.size();
            it = G.slotOf.find(kk);
            G.slot.push_back(s);
            pixels.push_back(NULL);
        }
        G.cellBase[i] = it->second;
        /* OPENCNC3D_RA3DDUMP=1: every cell's template and icon, for checking a picture
           against the map the engine exported */
        if (getenv("OPENCNC3D_RA3DDUMP"))
            fprintf(stderr, "RA3D|cell|%d,%d|%s|%d|%s\n", g_raStaticMap.MapCellX + i % W,
                    g_raStaticMap.MapCellY + i / W, name, c.IconNumber,
                    pixels[it->second] ? "pack" : "tile");
    }
    /* each first painting's variants */
    const int n = (int)G.slot.size();
    G.varHas.assign((size_t)n, 0);
    G.varSlot.assign((size_t)n * TS_VARS, -1);
    for (int s = 0; s < n; s++) {
        if (!G.slot[s].var) continue;
        const std::string base = ra3d_key(G.names[G.slot[s].name].c_str(), G.slot[s].icon, 0);
        std::unordered_map<std::string, int>::const_iterator it = G.slotOf.find(base);
        if (it == G.slotOf.end()) continue;
        G.varHas[it->second] |= (unsigned char)(1 << G.slot[s].var);
        G.varSlot[(size_t)it->second * TS_VARS + G.slot[s].var] = s;
    }

    GLint maxTex = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    G.cols = (int)ceil(sqrt((double)n));
    if (G.cols < 1) G.cols = 1;
    G.w = G.cols * G.pitch;
    G.h = ((n + G.cols - 1) / G.cols) * G.pitch;
    if (maxTex > 0 && (G.w > maxTex || G.h > maxTex)) {
        fprintf(stderr, "RA3D|enh|a %dx%d sheet is over this GL's %d limit; nothing drawn\n",
                G.w, G.h, (int)maxTex);
        return false;
    }
    std::vector<unsigned char> sheet((size_t)G.w * G.h * 4, 0);
    std::vector<unsigned char> rgb((size_t)G.ts * G.ts * 3), tile(24 * 24 * 3);
    std::vector<unsigned char> alpha((size_t)G.ts * G.ts);
    G.avg.assign((size_t)n * 3, 0);
    G.wet.assign((size_t)n, 0);
    /* THE SLOTS THIS MAP DRAWS: every cell's first painting and that painting's variants.
       Only these are given the sea's soft coast in their alpha: the ramp (two distance
       transforms a slot) on every wet slot of the pack was most of the sheet's time, which
       is part of a mission's first frame, and the alpha of a slot no cell draws is never
       read. Every slot still gets its painting: the coast's shader reads the sheet's
       colour a screen pixel either side of the point it shades, and far from the camera
       a pixel spans more than a slot's gutter, so the neighbouring slot's colour reaches
       the picture there. Every slot's cut is still read, for the sea's atlas and counts. */
    std::vector<unsigned char> used((size_t)n, 0);
    for (size_t i = 0; i < G.cellBase.size(); i++) {
        const int b = G.cellBase[i];
        if (b < 0) continue;
        used[b] = 1;
        for (int v = 1; v < TS_VARS; v++) {
            const int vs = G.varSlot[(size_t)b * TS_VARS + v];
            if (vs >= 0) used[vs] = 1;
        }
    }
    /* THE CUT ATLAS for the sea's field (water_mod.h): every slot's 1995 cut at 24 texels,
       in the sheet's own layout at 1/k of its size, 255 land and 0 water */
    WaterRaSource& WR = g_waterRa;
    WR.cutW = G.w / k; WR.cutH = G.h / k;
    WR.cut.assign((size_t)WR.cutW * WR.cutH, 255);
    unsigned char cut[576];
    std::vector<std::vector<unsigned char> > snowAlpha((size_t)n),snowFeatherRGB((size_t)n);
    if(th=='S' && G.set==FX_TEX_ENH)ra3d_snow_coast_alpha(G,pixels,used,snowAlpha,&snowFeatherRGB);
    for (int s = 0; s < n; s++) {
        const Ra3dSlot& sl = G.slot[s];
        const char* name = G.names[sl.name].c_str();
        const unsigned char* a = NULL;
        if (ra3d_water_cut(th, name, sl.icon, cut) > 0) {
            G.wet[s] = 1;
            G.wetSlots++;
            if (used[s]) {
                ra3d_water_alpha(cut, G.ts, &alpha[0]);
                a = &alpha[0];
                G.coast++;
            }
            const int cx0 = (s % G.cols) * (G.pitch / k) + 1, cy0 = (s / G.cols) * (G.pitch / k) + 1;
            for (int y = -1; y <= 24; y++)
                for (int x = -1; x <= 24; x++) {
                    const int sy = y < 0 ? 0 : y > 23 ? 23 : y, sx = x < 0 ? 0 : x > 23 ? 23 : x;
                    WR.cut[(size_t)(cy0 + y) * WR.cutW + cx0 + x] = cut[sy * 24 + sx] ? 0 : 255;
                }
        }
        if(!snowAlpha[s].empty()) {
            memcpy(alpha.data(),snowAlpha[s].data(),alpha.size());a=alpha.data();
            bool wet=false;for(unsigned char v:alpha)if(v<128){wet=true;break;}
            if(wet && !G.wet[s]){G.wet[s]=1;G.wetSlots++;G.coast++;}
            const int cx0=(s%G.cols)*(G.pitch/k)+1,cy0=(s/G.cols)*(G.pitch/k)+1;
            for(int y=-1;y<=24;y++)for(int x=-1;x<=24;x++) {
                const int sx=std::min(G.ts-1,std::max(0,x)*k+k/2),sy=std::min(G.ts-1,std::max(0,y)*k+k/2);
                WR.cut[(size_t)(cy0+y)*WR.cutW+cx0+x]=alpha[(size_t)sy*G.ts+sx];
            }
        }
        if(pixels[s] && G.set==FX_TEX_ENH && th=='T' && ra3d_water_family(name))G.wet[s]=1;
        if (pixels[s]) {
            if(used[s] && G.wet[s] && G.set==FX_TEX_ENH && th=='T') {
                std::vector<unsigned char> coast((size_t)G.ts*G.ts*4);
                for(size_t j=0;j<(size_t)G.ts*G.ts;j++) {
                    memcpy(&coast[j*4],pixels[s]+j*3,3);coast[j*4+3]=a?a[j]:255;
                }
                coast_mask_apply(&coast[0],G.ts,G.ts,(size_t)G.ts*4,0,name,sl.icon,sl.var);
                for(size_t j=0;j<(size_t)G.ts*G.ts;j++) {
                    memcpy(&rgb[j*3],&coast[j*4],3);alpha[j]=coast[j*4+3];
                }
                const int cx0=(s%G.cols)*(G.pitch/k)+1,cy0=(s/G.cols)*(G.pitch/k)+1;
                for(int y=-1;y<=24;y++)for(int x=-1;x<=24;x++) {
                    const int sx=std::min(G.ts-1,std::max(0,x)*k+k/2),sy=std::min(G.ts-1,std::max(0,y)*k+k/2);
                    WR.cut[(size_t)(cy0+y)*WR.cutW+cx0+x]=alpha[(size_t)sy*G.ts+sx];
                }
                ra3d_place(sheet,s,pixels[s],&alpha[0]); // preserve raw painted RGB for water-FX-off and the floor
            }else ra3d_place(sheet, s, snowFeatherRGB[s].empty()?pixels[s]:snowFeatherRGB[s].data(), a);
            ra3d_mean(pixels[s], G.ts, &G.avg[(size_t)s * 3]);
            G.fromPack++;
            continue;
        }
        if (!ra3d_tile_rgb(th, name, sl.icon, &tile[0])
            && !ra3d_tile_rgb(th, "CLEAR1", 0, &tile[0])) {   /* Classic's own stand-in */
            G.missing++;
            continue;
        }
        if (G.ts == 24) memcpy(&rgb[0], &tile[0], tile.size());
        else rt_bilinear_rgb(&tile[0], 24, 24, 24 * 3, &rgb[0], G.ts, G.ts);
        ra3d_place(sheet, s, &rgb[0], a);
        ra3d_mean(&rgb[0], G.ts, &G.avg[(size_t)s * 3]);
        G.fromTiles++;
    }
    glGenTextures(1, &G.tex);
    glBindTexture(GL_TEXTURE_2D, G.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, fx_filter_mode(GL_NEAREST));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, fx_filter_mode(GL_NEAREST));
    fx_filter_note(G.tex, GL_NEAREST);   /* the bilinear toggle has to reach it too */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    /* RGBA: the alpha is the water cut, solid everywhere but the sea's slots. Drawn
       without the sea the alpha is never read and the picture is the RGB it always was. */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, G.w, G.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, &sheet[0]);
    glBindTexture(GL_TEXTURE_2D, 0);
    std::vector<unsigned char> drySheet;
    if(G.set==FX_TEX_ENH) {
        ra3d_cliff_material_sheet(G,sheet);
        drySheet=sheet;
        ra3d_coast_feather_sheet(G,used,drySheet);
        glGenTextures(1,&G.dryTex);glBindTexture(GL_TEXTURE_2D,G.dryTex);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,fx_filter_mode(GL_NEAREST));glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,fx_filter_mode(GL_NEAREST));fx_filter_note(G.dryTex,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,G.w,G.h,0,GL_RGBA,GL_UNSIGNED_BYTE,drySheet.data());glBindTexture(GL_TEXTURE_2D,0);
    }
    if(G.set==FX_TEX_ENH && th!='I') {
        std::vector<unsigned char> base;int side=0;const char* what=NULL;
        if(ra3d_water_base(pixels,base,&side,&what)) {
            const auto tile=water_static_tile(base,side,G.ts);int painted=0;
            for(int slot=0;slot<n;slot++)if(used[slot]&&G.wet[slot]) {
                const int x=(slot%G.cols)*G.pitch+G.gutter,y=(slot/G.cols)*G.pitch+G.gutter;
                unsigned char* rgba=&sheet[((size_t)y*G.w+x)*4];
                water_static_composite(rgba,G.ts,(size_t)G.w*4,tile,rgba+3,(size_t)G.w*4,4);
                water_coast_gutter(sheet,G.w,G.h,x,y,G.ts,G.ts);painted++;
            }
            // The live effect retains the dry source uploaded above. Only the
            // effects-off atlas receives the coherent, opaque water painting.
            glBindTexture(GL_TEXTURE_2D,G.tex);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,G.w,G.h,0,GL_RGBA,GL_UNSIGNED_BYTE,sheet.data());glBindTexture(GL_TEXTURE_2D,0);
            fprintf(stderr,"WATER|static-material|ra|theater=%c|cells=%d|same-bank=1|land-alpha-unchanged=1\n",th,painted);
        }
    }
    G.ok = true;
    ra3d_water_source(pixels);
    /* coast= the wet slots the map draws, the ones given the soft coast (of wet_slots in
       the sea's source line) */
    fprintf(stderr, "RA3D|enh|sheet|%s|%s|%s|%dx%d|cell=%d|slots=%d|pack=%d|variants=%d|"
                    "tiles=%d|missing=%d|coast=%d|%.0f ms\n",
            g_raStaticScene.c_str(), ra3d_theater_name(th),
            G.set == FX_TEX_ENH ? "enhanced" : "1995", G.w, G.h, G.ts, n, G.fromPack,
            G.variants, G.fromTiles, G.missing, G.coast,
            (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency());
    if (G.set == FX_TEX_ENH) ra3d_seams_load(&drySheet);
    return true;
}

/* The painting map cell i draws: its first painting, or a variant by placement. */
static int ra3d_cell_slot(int i, int x, int y)
{
    const int base = g_ra3d.cellBase[i];
    if (base < 0 || !g_ra3d.varHas[base] || enh_variants_off()) return base;
    const Ra3dSlot& s = g_ra3d.slot[base];
    const int v = enh_variant_pick(x, y, s.icon, s.w, s.h, g_ra3d.varHas[base]);
    const int vs = v ? g_ra3d.varSlot[(size_t)base * TS_VARS + v] : -1;
    return vs >= 0 ? vs : base;
}

static void ra3d_slot_uv(int slot, float* u0, float* v0, float* u1, float* v1)
{
    const Ra3dGround& G = g_ra3d;
    const int x0 = (slot % G.cols) * G.pitch + G.gutter, y0 = (slot / G.cols) * G.pitch + G.gutter;
    *u0 = (float)x0 / (float)G.w;        *v0 = (float)y0 / (float)G.h;
    *u1 = (float)(x0 + G.ts) / (float)G.w; *v1 = (float)(y0 + G.ts) / (float)G.h;
}

/* THE JOIN BLEND'S STRIPS for the theater: the same arrangement terrain_seams_ensure
   makes for Tiberian Dawn, keyed by Red Alert slot (first painting) times TS_VARS plus
   the variant, and by template name for "is this a cliff". OPENCNC3D_TERRAINSEAM=off turns
   both games' strips off; OPENCNC3D_RATERRAINSEAM names Red Alert's pack outright. */
static void ra3d_seams_load(const std::vector<unsigned char>* cleanSheet)
{
    const Ra3dGround& G=g_ra3d;
    g_ra3dSeamsTried = true;
    const char* td = getenv("OPENCNC3D_TERRAINSEAM");
    if (td && (!strcmp(td, "off") || !strcmp(td, "none"))) {
        fprintf(stderr, "RA3D|seams|off\n");
        return;
    }
    char buf[256];
    const char* path = ra3d_pack_path("OPENCNC3D_RATERRAINSEAM", "terrainseam", g_ra3d.theater, buf, sizeof buf);
    if (!path) { fprintf(stderr, "RA3D|seams|no pack for %s\n", ra3d_theater_name(g_ra3d.theater)); return; }
    std::vector<unsigned char> d = ra3d_read_file(path);
    if (d.size() < 24 || memcmp(&d[0], RA3D_SEAM_MAGIC, 8) != 0 || rt_le32(&d[8]) != 1) {
        fprintf(stderr, "RA3D|seams|%s is not a version 1 Red Alert seam pack\n", path);
        return;
    }
    const unsigned C = rt_le32(&d[12]), M = rt_le32(&d[16]), cnt = rt_le32(&d[20]);
    const size_t keys = 24, body = keys + (size_t)cnt * 24;
    if (!C || !M || M >= C || d.size() != body + (size_t)cnt * C * M * 4) {
        fprintf(stderr, "RA3D|seams|%s has the wrong size\n", path);
        return;
    }
    TerrainSeams& S = g_ra3dSeams;
    const size_t nv = g_ra3d.slot.size() * TS_VARS;
    S.slotL.assign(nv, -1); S.slotT.assign(nv, -1);
    S.featL.assign(nv, 0); S.featT.assign(nv, 0);
    S.cliffTmpl.assign(65536, 0);
    std::vector<const unsigned char*> L, T, dryL, dryT;
    std::vector<unsigned char> dry(d);size_t repairedRGB=0;
    int unused = 0;
    for (unsigned k = 0; k < cnt; k++) {
        const unsigned char* e = &d[keys + (size_t)k * 24];
        char name[17];
        memcpy(name, e, 16); name[16] = 0;
        const int icon = e[16] | (e[17] << 8), side = e[18], feat = e[19] == 1, var = e[20];
        const unsigned char* px = &d[body + (size_t)k * C * M * 4];
        if (!feat) {
            const int id = ra3d_name_id(name);
            if (id < 65536) S.cliffTmpl[id] = 1;
        }
        std::unordered_map<std::string, int>::const_iterator it = g_ra3d.slotOf.find(ra3d_key(name, icon, 0));
        if (it == g_ra3d.slotOf.end() || var >= TS_VARS) { unused++; continue; }
        const size_t slot = (size_t)it->second * TS_VARS + var;
        unsigned char* dryPx=&dry[body+(size_t)k*C*M*4];
        auto variant=g_ra3d.slotOf.find(ra3d_key(name,icon,var));
        if(cleanSheet && variant!=g_ra3d.slotOf.end()) {
            auto repair=g_ra3d.cliffRepair.find(variant->second);
            if(repair!=g_ra3d.cliffRepair.end()) {
                const int ts=g_ra3d.ts,sx=(variant->second%g_ra3d.cols)*g_ra3d.pitch+g_ra3d.gutter,sy=(variant->second/g_ra3d.cols)*g_ra3d.pitch+g_ra3d.gutter;
                std::vector<unsigned char> edge(C*3),mask(C);
                for(unsigned j=0;j<C;j++){const int a=(int)(j*ts/C),x=side?a:0,y=side?0:a;mask[j]=repair->second[(size_t)y*ts+x];memcpy(&edge[j*3],&(*cleanSheet)[((size_t)(sy+y)*g_ra3d.w+sx+x)*4],3);}
                repairedRGB+=ra3d_cliff_strip_material(dryPx,(int)C,(int)M,side,edge,mask);
            }
        }
        // Neither fallback strips nor live-coast strips may restore painted
        // water across a neighbour. The source sheet's alpha owns that edge.
        if(cleanSheet && G.set==FX_TEX_ENH && variant!=g_ra3d.slotOf.end() && G.wet[variant->second]) {
            const int source=variant->second,ts=G.ts,sx=(source%G.cols)*G.pitch+G.gutter,sy=(source/G.cols)*G.pitch+G.gutter;
            for(unsigned y=0;y<(side?M:C);y++)for(unsigned x=0;x<(side?C:M);x++) {
                const int ex=side?(int)(x*ts/C):0,ey=side?0:(int)(y*ts/C);
                const int coverage=(*cleanSheet)[((size_t)(sy+ey)*G.w+sx+ex)*4+3];const size_t at=((size_t)y*(side?C:M)+x)*4+3;
                d[body+(size_t)k*C*M*4+at]=(unsigned char)((d[body+(size_t)k*C*M*4+at]*coverage+127)/255);
                dryPx[at]=(unsigned char)((dryPx[at]*coverage+127)/255);
            }
        }
        if (side == 0) { S.slotL[slot] = (int)L.size(); S.featL[slot] = feat; L.push_back(px); dryL.push_back(dryPx); }
        else           { S.slotT[slot] = (int)T.size(); S.featT[slot] = feat; T.push_back(px); dryT.push_back(dryPx); }
    }
    S.cell = (int)C; S.margin = (int)M;
    if (!L.empty()) S.texL = ts_sheet(L, (int)M, (int)C, &S.wL, &S.hL, &S.colsL);
    if (!T.empty()) S.texT = ts_sheet(T, (int)C, (int)M, &S.wT, &S.hT, &S.colsT);
    if(repairedRGB){int w,h,cols;
        if(!dryL.empty())g_ra3d.drySeamL=ts_sheet(dryL,(int)M,(int)C,&w,&h,&cols);
        if(!dryT.empty())g_ra3d.drySeamT=ts_sheet(dryT,(int)C,(int)M,&w,&h,&cols);
        fprintf(stderr,"RA3D|cliff-material|join-repaired-rgb=%zu|alpha-unchanged=1\n",repairedRGB);
    }
    S.ok = S.texL || S.texT;
    S.tried = true;
    fprintf(stderr, "RA3D|seams|%s|%zu left|%zu top|%d unmatched|margin %u at %u a cell\n",
            path, L.size(), T.size(), unused, M, C);
}

/* ONE SUN, as Tiberian Dawn's Enhanced ground has it (terrain_draw and its passes in
   cnc_eyes.cpp): while the post chain shades the ground per pixel from its normal
   (sun_lambert with terrain_normals), every corner is drawn at the level ground's 160/255
   and the console's baked shade is NOT also applied. That shade is the console's own
   fixed light, up to 255/160 = 1.59 times level on a slope turned
   to it; the post chain's sun then adds up to 1.35 times more on any slope that faces
   the F5 sun, so a slope facing both was lit twice, near 2.15 times level ground, and
   pale snow under the terrain brightness and the grade's gain went to pure white.
   Lowering the sun or the brightness could not reach it, since half of it was not the
   F5 sun at all. */
static bool ra3d_one_sun(void)
{
    return fx_unified_lighting() || (g_fxActive && g_fx.sun_lambert && g_fx.terrain_normals);
}

/* A corner's light before the shroud: the console's shade off the scene's heights, or
   the level ground's 160/255 where the ground is flat or the post chain's one sun
   shades it instead (ra3d_one_sun). */
static float ra3d_corner_light(int x, int z)
{
    if (ra3d_flat_ground() || g_pack.corner.empty() || !g_shadeReady || ra3d_one_sun())
        return 160.0f / 255.0f;
    if (x < 0) x = 0; else if (x > g_gridW) x = g_gridW;
    if (z < 0) z = 0; else if (z > g_gridH) z = g_gridH;
    return (float)g_shadeGrid[z * (g_gridW + 1) + x] * (1.0f / 255.0f);
}

/* A cell's four corners' light, NW NE SE SW: the corner shade times the shroud's. */
static inline void ra3d_cell_light(int cx, int cy, float s[4])
{
    s[0] = ra3d_corner_light(cx, cy) * shroud_corner_vis(cx, cy);
    s[1] = ra3d_corner_light(cx + 1, cy) * shroud_corner_vis(cx + 1, cy);
    s[2] = ra3d_corner_light(cx + 1, cy + 1) * shroud_corner_vis(cx + 1, cy + 1);
    s[3] = ra3d_corner_light(cx, cy + 1) * shroud_corner_vis(cx, cy + 1);
}

#include "ra3d_road_join.h"

/* ---- THE GROUND KEPT ON THE GPU ------------------------------------------------------
 *
 * The ground, the sea floor, the join strips and the ground's normals used to be sent
 * corner by corner, every cell in view, every time a pass drew them: the heights, the
 * texture coordinates and the light of each corner computed again and the lot sent in
 * immediate mode. On Red Alert's 128-cell maps that was the largest section of the
 * frame. None of it changes from one frame to the next except the light, and the light
 * only where the shroud moved.
 *
 * So it is Tiberian Dawn's arrangement (terrain_cache.h) on Red Alert's map: the shape is
 * built once per sheet into buffers on the GPU, in the order the loops visited it. The
 * records are one per map cell for the ground, then one per wet cell for the sea floor
 * (which carries its own texture coordinates, the ones the sea's source was built with),
 * six corners each; every strip of both passes as its TS_LAT corners and an index list;
 * and the normal pass's colours beside the ground's corners. Each cell's light is
 * recomputed at most once a frame, for the cells a pass draws, and only a cell whose
 * light differs from the one remembered has its colours rebuilt and sent again: its
 * ground, its floor and the strips laid over its neighbours. Then the records the pass
 * admits go down in runs of consecutive records, so the triangles, their corners and their
 * order are those of the loops they replace, and the pictures are the same.
 *
 * Rebuilt when the sheet is (a mission, a set), and when anything the shape is computed
 * from moves: the heights (a pad, a pack), the variants switch, the map's rectangle. Not
 * on a driver without buffers, where the loops below still run; OPENCNC3D_TERRAIN_CACHE=0
 * turns this and Tiberian Dawn's cache off for an A/B.
 * ------------------------------------------------------------------------------------- */

/* Everything the shape is computed from. Compared whole. */
struct Ra3dCacheKey {
    unsigned sheetGen, groundGen, padEpoch, flat, waterPlaneEpoch;
    int varOff, gridW, gridH, mapX, mapY, mapW, mapH, pad;
    float base;
    const void* floorCells;
    size_t nFloor;
    GLuint seamL, seamT;
    int seamCell, seamMargin;
    size_t seamSlots;
};

struct Ra3dCache {
    bool built, failed;
    Ra3dCacheKey key;
    int nCells;                       /* the map's cells, W x H                           */
    int nGround;                      /* ground records; the floor's follow them          */
    std::vector<int> recCell;         /* record -> map cell, ground then floor            */
    std::vector<int> groundOf, floorOf;   /* map cell -> its record, or -1               */
    TcStage A;                        /* per corner x y z u v, and its colour             */
    GLuint nrmVbo;                    /* per ground corner the normal pass's RGB          */
    bool nrmBuilt;
    unsigned nrmShade;
    int nrmLevel;
    /* the strips, pass 0 (left) then pass 1 (top): TS_LAT corners each, x y z u0 v0 u1 v1 */
    std::vector<int> stripCell[2];    /* strip -> map cell a                              */
    std::vector<int> stripBx[2], stripBy[2];   /* the world cell it is laid over        */
    std::vector<int> stripOf[2];      /* map cell -> its strip in that pass, or -1        */
    std::vector<int> stripK[2];       /* strip -> its strip in the pass's sheet           */
    std::vector<int> floorSrc;        /* floor record - nGround -> the sea source's cell  */
    std::vector<float> fxz;           /* per strip corner: its fraction of the cell       */
    std::vector<unsigned> idx;        /* TS_IDX per strip, absolute                       */
    size_t stripFirst[2];
    TcStage B;
    std::vector<float> light;         /* per map cell: NW NE SE SW, as last coloured      */
    std::vector<unsigned char> lit;   /* per map cell: 1 once it has been coloured        */
    std::vector<unsigned> lightAt;    /* per map cell: the stamp it was last computed for */
    unsigned stamp;                   /* moved once per ground pass (ra3d_cache_frame)    */
    std::vector<int> sig;             /* the light's inputs at that stamp (ra3d_cache_pass) */
    /* what the frame used, for the log */
    long uploads, recoloured;
    unsigned revSeq;                  /* g_groundSeq the shape was last brought up to date at */
};
static Ra3dCache g_ra3dc;

static void ra3d_cache_free(void)
{
    Ra3dCache& C = g_ra3dc;
    tcache_stage_free(C.A);
    tcache_stage_free(C.B);
    if (C.nrmVbo && g_grassBufReady) gr_glDeleteBuffers(1, &C.nrmVbo);
    C.nrmVbo = 0;
    C.built = C.failed = C.nrmBuilt = false;
    C.nCells = C.nGround = 0;
    C.recCell.clear(); C.groundOf.clear(); C.floorOf.clear(); C.floorSrc.clear();
    for (int p = 0; p < 2; p++) {
        C.stripCell[p].clear(); C.stripBx[p].clear(); C.stripBy[p].clear(); C.stripOf[p].clear();
        C.stripK[p].clear();
    }
    C.fxz.clear(); C.idx.clear();
    C.light.clear(); C.lit.clear(); C.lightAt.clear();
    C.uploads = C.recoloured = 0;
}

static bool ra3d_cache_usable(void)
{
    return !g_editOn && !g_ra3dc.failed && tcache_switch_on() && grass_buf_load();
}

/* One buffer sent and its size read back, as tcache_store does for Tiberian Dawn's: a
   driver short of memory may make an empty store and say nothing. */
static bool ra3d_cache_store(GLuint* vbo, GLenum target, const void* data, size_t bytes, GLenum usage)
{
    if (!*vbo) gr_glGenBuffers(1, vbo);
    gr_glBindBuffer(target, *vbo);
    gr_glBufferData(target, (FxGLsizeiptr)bytes, bytes ? data : NULL, usage);
    GLint got = -1;
    gr_glGetBufferParameteriv(target, GL_BUFFER_SIZE, &got);
    gr_glBindBuffer(target, 0);
    if (bytes && (got < 0 || (size_t)got != bytes)) {
        fprintf(stderr, "GROUND|ra|cache refused|the driver made %ld B of a %lu B ground store; "
                        "Red Alert's ground draws per frame until the next mission\n",
                (long)got, (unsigned long)bytes);
        g_ra3dc.failed = true;
        return false;
    }
    return true;
}

static void ra3d_cache_key_now(Ra3dCacheKey* k)
{
    memset(k, 0, sizeof *k);
    k->sheetGen = g_ra3dSheetGen;
    k->groundGen = g_groundGen; k->padEpoch = g_padEpoch;
    k->flat = ra3d_flat_ground() ? 1u : 0u;
    k->waterPlaneEpoch = water_plane_epoch();
    k->varOff = enh_variants_off() ? 1 : 0;
    k->gridW = g_gridW; k->gridH = g_gridH;
    k->mapX = g_raStaticMap.MapCellX; k->mapY = g_raStaticMap.MapCellY;
    k->mapW = g_raStaticMap.MapCellWidth; k->mapH = g_raStaticMap.MapCellHeight;
    k->base = g_terrainBase;
    k->floorCells = g_waterRa.cell.empty() ? NULL : &g_waterRa.cell[0];
    k->nFloor = g_waterRa.cell.size();
    const TerrainSeams& S = g_ra3dSeams;
    k->seamL = S.texL; k->seamT = S.texT; k->seamCell = S.cell; k->seamMargin = S.margin;
    k->seamSlots = S.slotL.size() + S.slotT.size();
}

/* A cell's six corners, NW SW NE NE SW SE (the console's diagonal), x y z u v. */
static void ra3d_cache_put_cell(std::vector<float>& g, int cx, int cy, float u0, float v0, float u1, float v1,
                                const PackCell* floor = NULL)
{
    const float x = (float)cx, z = (float)cy;
    const float y00 = floor ? water_plane_floor_y(*floor, cx, cy) : terrain_corner_y(cx, cy);
    const float y10 = floor ? water_plane_floor_y(*floor, cx + 1, cy) : terrain_corner_y(cx + 1, cy);
    const float y11 = floor ? water_plane_floor_y(*floor, cx + 1, cy + 1) : terrain_corner_y(cx + 1, cy + 1);
    const float y01 = floor ? water_plane_floor_y(*floor, cx, cy + 1) : terrain_corner_y(cx, cy + 1);
    const float V[30] = { x,        y00, z,        u0, v0,
                          x,        y01, z + 1.0f, u0, v1,
                          x + 1.0f, y10, z,        u1, v0,
                          x + 1.0f, y10, z,        u1, v0,
                          x,        y01, z + 1.0f, u0, v1,
                          x + 1.0f, y11, z + 1.0f, u1, v1 };
    g.insert(g.end(), V, V + 30);
}

/* THE SHAPE: the ground's cells in map order, the floor's wet cells in the sea source's
   order, and both passes' strips in the order ra3d_draw_seams visits them, with every
   condition that cannot change during a mission already applied. */
static bool ra3d_cache_build(const Ra3dCacheKey& k)
{
    Ra3dCache& C = g_ra3dc;
    const Uint64 t0 = SDL_GetPerformanceCounter();
    const bool wasFailed = C.failed;
    ra3d_cache_free();
    C.failed = wasFailed;
    if (C.failed) return false;
    C.key = k;
    const Ra3dGround& G = g_ra3d;
    const int W = g_raStaticMap.MapCellWidth, H = g_raStaticMap.MapCellHeight;
    const int X0 = g_raStaticMap.MapCellX, Y0 = g_raStaticMap.MapCellY;
    C.nCells = W * H;
    C.groundOf.assign((size_t)C.nCells, -1);
    C.floorOf.assign((size_t)C.nCells, -1);
    std::vector<float>& g = C.A.geo;
    g.reserve((size_t)C.nCells * 30);
    for (int i = 0; i < C.nCells; i++) {
        const int cx = X0 + i % W, cy = Y0 + i / W;
        const int s = ra3d_cell_slot(i, cx, cy);
        if (s < 0) continue;
        float u0, v0, u1, v1;
        ra3d_slot_uv(s, &u0, &v0, &u1, &v1);
        C.groundOf[i] = (int)C.recCell.size();
        C.recCell.push_back(i);
        ra3d_cache_put_cell(g, cx, cy, u0, v0, u1, v1);
    }
    C.nGround = (int)C.recCell.size();
    const std::vector<PackCell>& wet = g_waterRa.cell;
    for (size_t f = 0; f < wet.size(); f++) {
        const PackCell& c = wet[f];
        if (!c.holes && !water_plane_covers(c)) continue;
        const int ax = c.x - X0, ay = c.y - Y0;
        if (ax < 0 || ay < 0 || ax >= W || ay >= H) continue;
        const int i = ay * W + ax;
        C.floorOf[i] = (int)C.recCell.size();
        C.recCell.push_back(i);
        C.floorSrc.push_back((int)f);
        ra3d_cache_put_cell(g, c.x, c.y, c.u0, c.v0, c.u1, c.v1, &c);
    }
    C.A.col.assign(C.recCell.size() * 6 * 4, 0.0f);

    /* the strips */
    const TerrainSeams& S = g_ra3dSeams;
    std::vector<float>& sg = C.B.geo;
    int nstrips = 0;
    for (int pass = 0; pass < 2; pass++) {
        C.stripOf[pass].assign((size_t)C.nCells, -1);
        C.stripFirst[pass] = (size_t)nstrips;
        TsPass P;
        if (!S.ok || G.set != FX_TEX_ENH || !ts_pass(S, pass, &P)) continue;
        for (int i = 0; i < C.nCells; i++) {
            const int ax = i % W, ay = i / W, cx = X0 + ax, cy = Y0 + ay;
            const int sa = G.cellBase[i];
            if (sa < 0 || G.slot[sa].name == G.clearName) continue;
            const int bx = ax + (pass ? 0 : 1), by = ay + (pass ? 1 : 0);
            if (bx >= W || by >= H) continue;
            const int bi = by * W + bx;
            const int sb = G.cellBase[bi];
            if (sb < 0 || G.slot[sb].name == G.clearName) continue;
            if (ra3d_road_slot(sa) && ra3d_road_slot(sb) && ra3d_roads_ready()) continue;
            const int drawn = ra3d_cell_slot(bi, X0 + bx, Y0 + by);
            const int sbv = sb * TS_VARS + (drawn == sb ? 0 : G.slot[drawn].var);
            const int kk = (*P.idx)[sbv];
            if (kk < 0) continue;
            if ((*P.feat)[sbv] && S.cliffTmpl[G.slot[sa].name & 0xFFFF]) continue;
            float u0, v0, u1, v1;
            ra3d_slot_uv(ra3d_cell_slot(i, cx, cy), &u0, &v0, &u1, &v1);
            C.stripOf[pass][i] = nstrips;
            C.stripCell[pass].push_back(i);
            C.stripBx[pass].push_back(X0 + bx);
            C.stripBy[pass].push_back(Y0 + by);
            C.stripK[pass].push_back(kk);
            const unsigned v0i = (unsigned)nstrips * TS_LAT;
            for (int ai = 0; ai <= TS_NA; ai++)
                for (int xi = 0; xi <= TS_NX; xi++) {
                    float fx, fz, out[7];
                    ts_corner_f(S, pass, ai, xi, &fx, &fz);
                    ts_corner_geo(S, pass, P, kk, cx, cy, u0, u1, v0, v1, 1.0f, 1.0f, fx, fz, out);
                    sg.insert(sg.end(), out, out + 7);
                    C.fxz.push_back(fx); C.fxz.push_back(fz);
                }
            for (int ia = 0; ia < TS_NA; ia++)
                for (int ix = 0; ix < TS_NX; ix++) {
                    int q[6];
                    ts_quad_corners(pass, ia, ix, q);
                    for (int j = 0; j < 6; j++) C.idx.push_back(v0i + (unsigned)q[j]);
                }
            nstrips++;
        }
    }
    C.B.col.assign((size_t)nstrips * TS_LAT * 4, 0.0f);

    if (!ra3d_cache_store(&C.A.geoVbo, GL_ARRAY_BUFFER, g.empty() ? NULL : &g[0], g.size() * sizeof(float), GL_STATIC_DRAW)
        || !ra3d_cache_store(&C.A.colVbo, GL_ARRAY_BUFFER, NULL, C.A.col.size() * sizeof(float), GL_DYNAMIC_DRAW))
        return false;
    if (nstrips
        && (!ra3d_cache_store(&C.B.geoVbo, GL_ARRAY_BUFFER, &sg[0], sg.size() * sizeof(float), GL_STATIC_DRAW)
            || !ra3d_cache_store(&C.B.colVbo, GL_ARRAY_BUFFER, NULL, C.B.col.size() * sizeof(float), GL_DYNAMIC_DRAW)
            || !ra3d_cache_store(&C.B.idxVbo, GL_ELEMENT_ARRAY_BUFFER, &C.idx[0],
                                 C.idx.size() * sizeof(unsigned), GL_STATIC_DRAW)))
        return false;
    C.A.dirtyLo = C.A.dirtyHi = 0;
    C.B.dirtyLo = C.B.dirtyHi = 0;
    C.light.assign((size_t)C.nCells * 4, 0.0f);
    C.lit.assign((size_t)C.nCells, 0);
    C.lightAt.assign((size_t)C.nCells, 0u);
    C.built = true;
    C.revSeq = ground_rev_current() ? g_groundSeq : 0;
    fprintf(stderr, "GROUND|ra|cache|%s|cells=%d|floor=%d|strips=%d+%d|%.1f MB|%.1f ms\n",
            g_raStaticScene.c_str(), C.nGround, (int)C.recCell.size() - C.nGround,
            (int)C.stripCell[0].size(), (int)C.stripCell[1].size(),
            (double)((g.size() + C.A.col.size() + sg.size() + C.B.col.size()) * sizeof(float)
                     + C.idx.size() * sizeof(unsigned)) / (1024.0 * 1024.0),
            (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency());
    return true;
}

/* A BUILDING MOVES A FEW CELLS, NOT THE MAP (terrain_cache.h, tcache_patch, for the same
   arrangement on Tiberian Dawn's ground). When the key differs in nothing but the pad
   epoch, the ground, floor and strip records of the cells stamped since the last refresh
   are laid again with the build's own functions and sent over their old bytes. The
   lights, colours and the normal pass's buffer read no height and stay. */
static bool ra3d_cache_patch(const Ra3dCacheKey& k)
{
    Ra3dCache& C = g_ra3dc;
    if (!C.built || !ground_patch_on() || !ground_rev_current() || g_groundWholeSeq > C.revSeq) return false;
    Ra3dCacheKey a = k, b = C.key;
    a.padEpoch = b.padEpoch = 0;
    if (memcmp(&a, &b, sizeof a) != 0) return false;
    const Uint64 t0 = SDL_GetPerformanceCounter();
    const unsigned since = C.revSeq;
    const int W = g_raStaticMap.MapCellWidth;
    const int X0 = g_raStaticMap.MapCellX, Y0 = g_raStaticMap.MapCellY;
    long cells = 0, strips = 0;
    size_t lo = 0, hi = 0;
    std::vector<float> fresh;
    for (size_t r = 0; r < C.recCell.size(); r++) {
        const int i = C.recCell[r];
        const int cx = X0 + i % W, cy = Y0 + i / W;
        if (!ground_rev_moved(since, cx, cy, cx, cy)) continue;
        fresh.clear();
        if ((int)r < C.nGround) {
            float u0, v0, u1, v1;
            ra3d_slot_uv(ra3d_cell_slot(i, cx, cy), &u0, &v0, &u1, &v1);
            ra3d_cache_put_cell(fresh, cx, cy, u0, v0, u1, v1);
        } else {
            const PackCell& c = g_waterRa.cell[C.floorSrc[r - C.nGround]];
            ra3d_cache_put_cell(fresh, c.x, c.y, c.u0, c.v0, c.u1, c.v1, &c);
        }
        ground_patch_record(C.A.geoVbo, C.A.geo, 5, 6, r, &fresh[0], lo, hi, cells);
    }
    ground_send_range(C.A.geoVbo, C.A.geo, 5, lo, hi);
    const TerrainSeams& S = g_ra3dSeams;
    lo = hi = 0;
    for (int pass = 0; pass < 2; pass++) {
        TsPass P;
        if (C.stripCell[pass].empty() || !ts_pass(S, pass, &P)) continue;
        for (size_t r = 0; r < C.stripCell[pass].size(); r++) {
            const int i = C.stripCell[pass][r];
            const int cx = X0 + i % W, cy = Y0 + i / W;
            const int bx = C.stripBx[pass][r], by = C.stripBy[pass][r];
            if (!ground_rev_moved(since, cx, cy, cx, cy) && !ground_rev_moved(since, bx, by, bx, by)) continue;
            float u0, v0, u1, v1;
            ra3d_slot_uv(ra3d_cell_slot(i, cx, cy), &u0, &v0, &u1, &v1);
            float out[TS_LAT * 7];
            int j = 0;
            for (int ai = 0; ai <= TS_NA; ai++)
                for (int xi = 0; xi <= TS_NX; xi++, j++) {
                    float fx, fz;
                    ts_corner_f(S, pass, ai, xi, &fx, &fz);
                    ts_corner_geo(S, pass, P, C.stripK[pass][r], cx, cy, u0, u1, v0, v1, 1.0f, 1.0f, fx, fz, &out[j * 7]);
                }
            ground_patch_record(C.B.geoVbo, C.B.geo, 7, TS_LAT, C.stripFirst[pass] + r, out, lo, hi, strips);
        }
    }
    ground_send_range(C.B.geoVbo, C.B.geo, 7, lo, hi);
    C.key = k;
    C.revSeq = g_groundSeq;
    const double ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
    if (ground_patch_verify()) {
        /* The GPU must hold the arrays and the arrays must be what a whole rebuild lays
           now; the rebuild is then kept, which is the old path. */
        std::vector<float> gpuA(C.A.geo.size()), gpuB(C.B.geo.size());
        const bool read = ground_read_buffer(GL_ARRAY_BUFFER, C.A.geoVbo, gpuA.size() * sizeof(float), gpuA.data())
            && (C.B.geo.empty() || ground_read_buffer(GL_ARRAY_BUFFER, C.B.geoVbo, gpuB.size() * sizeof(float), gpuB.data()));
        const bool gpuOk = !read || (gpuA == C.A.geo && gpuB == C.B.geo);
        const std::vector<float> wantA = C.A.geo, wantB = C.B.geo;
        const std::vector<int> recA = C.recCell, recB0 = C.stripCell[0], recB1 = C.stripCell[1];
        const bool ok = ra3d_cache_build(k);
        const bool same = ok && C.A.geo == wantA && C.B.geo == wantB && C.recCell == recA
            && C.stripCell[0] == recB0 && C.stripCell[1] == recB1;
        fprintf(stderr, "GROUNDPATCH|verify|ra cache|%s|gpu=%s|cells=%ld|strips=%ld\n",
                same && gpuOk ? "ok" : "MISMATCH", read ? (gpuOk ? "ok" : "MISMATCH") : "unread", cells, strips);
        return ok;
    }
    fprintf(stderr, "GROUNDPATCH|ra cache|cells=%ld|strips=%ld|%.2f ms\n", cells, strips, ms);
    return true;
}

/* Ready to draw from: the shape current (built now if not), or false when the loops run. */
static bool ra3d_cache_ready(void)
{
    if (!g_ra3d.ok || !ra3d_cache_usable()) return false;
    Ra3dCacheKey k;
    ra3d_cache_key_now(&k);
    if (!g_ra3dc.built || memcmp(&k, &g_ra3dc.key, sizeof k) != 0) {
        const GLuint bound = gls_array_binding();
        const bool ok = ra3d_cache_patch(k) || ra3d_cache_build(k);
        gr_glBindBuffer(GL_ARRAY_BUFFER, bound);
        if (!ok) return false;
    }
    return true;
}

/* A new frame's worth of light: every cell is computed afresh the first time a pass after
   this asks for it. Each ground pass of a frame calls it once, before its first draw. */
static void ra3d_cache_frame(void)
{
    Ra3dCache& C = g_ra3dc;
    if (++C.stamp == 0) {               /* wrapped: no cell may look computed */
        std::fill(C.lightAt.begin(), C.lightAt.end(), 0u);
        C.stamp = 1;
    }
    C.sig.clear();
}

/* WHAT A CELL'S LIGHT IS COMPUTED FROM, besides the shroud's corner field (which only the
   engine's refresh moves, between frames). The shade is built lazily by whichever pass
   asks first, and the sea's surface asks for it between the floor and the ground on a
   mission's first frame, so the light a frame remembered can go stale inside the frame;
   each pass compares this before it draws and starts the frame's light again if it moved. */
static void ra3d_cache_pass(void)
{
    Ra3dCache& C = g_ra3dc;
    const int sig[11] = { g_shadeReady ? 1 : 0, (int)g_shadeGen, ra3d_flat_ground() ? 1 : 0,
                          g_shroudTerrainLight, g_shroudSoft, g_shroudRimOn, g_shroudOn,
                          g_shroudValid, g_shroudRevealed ? 1 : 0, g_shroudEditorOff ? 1 : 0,
                          ra3d_one_sun() ? 1 : 0 };
    if (C.sig.size() == 11 && !memcmp(&C.sig[0], sig, sizeof sig)) return;
    if (!C.sig.empty()) ra3d_cache_frame();     /* moved since the frame's first pass */
    C.sig.assign(sig, sig + 11);
}

static void ra3d_cache_colour_rec(int rec, const float s[4])
{
    Ra3dCache& C = g_ra3dc;
    float* c = &C.A.col[(size_t)rec * 6 * 4];
    /* corners in drawing order: NW, SW, NE, NE, SW, SE = light index 0, 3, 1, 1, 3, 2 */
    static const int order[6] = { 0, 3, 1, 1, 3, 2 };
    for (int j = 0; j < 6; j++) {
        c[0] = c[1] = c[2] = s[order[j]];
        c[3] = 1.0f;                    /* glColor3f's alpha */
        c += 4;
    }
    tcache_dirty(C.A, (size_t)rec * 6, (size_t)rec * 6 + 6);
}

/* Map cell i's light this frame; when it is not the one remembered, its ground, its floor
   and the strips it lays are coloured again. */
static void ra3d_cache_light(int i)
{
    Ra3dCache& C = g_ra3dc;
    if (C.stamp && C.lightAt[i] == C.stamp) return;
    C.lightAt[i] = C.stamp;
    const int W = g_raStaticMap.MapCellWidth;
    float s[4];
    ra3d_cell_light(g_raStaticMap.MapCellX + i % W, g_raStaticMap.MapCellY + i / W, s);
    float* was = &C.light[(size_t)i * 4];
    if (C.lit[i] && !memcmp(was, s, sizeof s)) return;
    memcpy(was, s, sizeof s);
    C.lit[i] = 1;
    C.recoloured++;
    if (C.groundOf[i] >= 0) ra3d_cache_colour_rec(C.groundOf[i], s);
    if (C.floorOf[i] >= 0) ra3d_cache_colour_rec(C.floorOf[i], s);
    static const unsigned char notint[4][3] = { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} };
    for (int p = 0; p < 2; p++) {
        const int k = C.stripOf[p].empty() ? -1 : C.stripOf[p][i];
        if (k < 0) continue;
        const size_t v0 = (size_t)k * TS_LAT;
        for (int j = 0; j < TS_LAT; j++)
            ts_corner_rgba(s, notint, false, C.fxz[(v0 + j) * 2], C.fxz[(v0 + j) * 2 + 1],
                           &C.B.col[(v0 + j) * 4]);
        tcache_dirty(C.B, v0, v0 + TS_LAT);
    }
}

static void ra3d_cache_upload(TcStage& s)
{
    if (s.dirtyLo != s.dirtyHi) g_ra3dc.uploads++;
    tcache_upload_colours(s);
}

/* OPENCNC3D_RA3DCACHE_VERIFY=1: A MEASURING INSTRUMENT, not a debug leftover. A cache that
   keeps a stale colour draws a picture that looks right until the shroud moves, so two
   matching screenshots say nothing about the invalidation. With this set, every record a
   pass draws from the buffers is computed again exactly as the per-frame loop computes it
   (its corners, its coordinates, its light this frame) and compared bit for bit with what
   the GPU holds, read back from the stores. Slow; for gates and checks only. */
typedef void (APIENTRY *Ra3dGetBufferSubData)(GLenum, FxGLsizeiptr, FxGLsizeiptr, void*);
static Ra3dGetBufferSubData g_ra3dGetSub = NULL;
static int  g_ra3dVerify = -1;
static long g_ra3dVerRecs = 0, g_ra3dVerBad = 0;

static bool ra3d_verify_on(void)
{
    if (g_ra3dVerify < 0) {
        const char* e = getenv("OPENCNC3D_RA3DCACHE_VERIFY");
        g_ra3dVerify = (e && strcmp(e, "0")) ? 1 : 0;
        if (g_ra3dVerify) {
            g_ra3dGetSub = (Ra3dGetBufferSubData)SDL_GL_GetProcAddress("glGetBufferSubData");
            if (!g_ra3dGetSub) g_ra3dGetSub = (Ra3dGetBufferSubData)SDL_GL_GetProcAddress("glGetBufferSubDataARB");
            if (!g_ra3dGetSub) {
                fprintf(stderr, "GROUND|ra|verify|unavailable|no glGetBufferSubData\n");
                g_ra3dVerify = 0;
            }
        }
    }
    return g_ra3dVerify == 1;
}

static void ra3d_verify_read(GLenum target, GLuint vbo, size_t bytes, void* out)
{
    if (!bytes) return;
    const GLuint bound = gls_array_binding();
    gr_glBindBuffer(target, vbo);
    g_ra3dGetSub(target, 0, (FxGLsizeiptr)bytes, out);
    gr_glBindBuffer(target, 0);
    gr_glBindBuffer(GL_ARRAY_BUFFER, bound);
}

static void ra3d_verify_bad(const char* what, int rec)
{
    if (g_ra3dVerBad < 8)
        fprintf(stderr, "GROUND|ra|verify|MISMATCH|%s|record=%d\n", what, rec);
    g_ra3dVerBad++;
}

/* The ground's or the floor's records the pass just drew, or the normal pass's colours. */
static void ra3d_verify_cells(const std::vector<GLint>& first, const std::vector<GLsizei>& count, bool nrm,
                              bool level)
{
    if (!ra3d_verify_on() || first.empty()) return;
    Ra3dCache& C = g_ra3dc;
    const int W = g_raStaticMap.MapCellWidth;
    std::vector<float> geo(C.A.geo.size()), col(nrm ? (size_t)C.nGround * 18 : C.A.col.size());
    ra3d_verify_read(GL_ARRAY_BUFFER, C.A.geoVbo, geo.size() * sizeof(float), geo.empty() ? NULL : &geo[0]);
    ra3d_verify_read(GL_ARRAY_BUFFER, nrm ? C.nrmVbo : C.A.colVbo, col.size() * sizeof(float),
                     col.empty() ? NULL : &col[0]);
    static const unsigned char up[3] = { 128, 255, 128 };
    static const int dx[6] = { 0, 0, 1, 1, 0, 1 }, dz[6] = { 0, 1, 0, 0, 1, 1 };
    static const int order[6] = { 0, 3, 1, 1, 3, 2 };
    for (size_t j = 0; j < first.size(); j++)
        for (int r = first[j] / 6; r < (first[j] + count[j]) / 6; r++) {
            g_ra3dVerRecs++;
            const int i = C.recCell[r];
            const int cx = g_raStaticMap.MapCellX + i % W, cy = g_raStaticMap.MapCellY + i / W;
            float u0, v0, u1, v1;
            if (r < C.nGround) ra3d_slot_uv(ra3d_cell_slot(i, cx, cy), &u0, &v0, &u1, &v1);
            else {
                const PackCell& c = g_waterRa.cell[C.floorSrc[r - C.nGround]];
                if (c.x != cx || c.y != cy) ra3d_verify_bad("floor cell", r);
                u0 = c.u0; v0 = c.v0; u1 = c.u1; v1 = c.v1;
            }
            std::vector<float> want;
            const PackCell* floor = r < C.nGround ? NULL : &g_waterRa.cell[C.floorSrc[r - C.nGround]];
            ra3d_cache_put_cell(want, cx, cy, u0, v0, u1, v1, floor);
            if (memcmp(&want[0], &geo[(size_t)r * 30], 30 * sizeof(float))) ra3d_verify_bad("corners", r);
            if (nrm) {
                bool bad = false;
                for (int k = 0; k < 6 && !bad; k++) {
                    int x = cx + dx[k], z = cy + dz[k];
                    x = x < 0 ? 0 : x > g_gridW ? g_gridW : x;
                    z = z < 0 ? 0 : z > g_gridH ? g_gridH : z;
                    const unsigned char* n = level ? up : g_normGrid[z * (g_gridW + 1) + x];
                    for (int q = 0; q < 3 && !bad; q++)
                        if ((float)n[q] * (1.0f / 255.0f) != col[((size_t)r * 6 + k) * 3 + q]) bad = true;
                }
                if (bad) ra3d_verify_bad("normal", r);
                continue;
            }
            float L[4];
            ra3d_cell_light(cx, cy, L);
            for (int k = 0; k < 6; k++) {
                const float* c = &col[((size_t)r * 6 + k) * 4];
                const float sv = L[order[k]];
                if (memcmp(&c[0], &sv, 4) || memcmp(&c[1], &sv, 4) || memcmp(&c[2], &sv, 4) || c[3] != 1.0f) {
                    ra3d_verify_bad("light", r);
                    break;
                }
            }
        }
}

/* The strips a pass just drew: their corners, coordinates, indices and light. */
static void ra3d_verify_strips(int pass, const std::vector<size_t>& first, const std::vector<GLsizei>& count)
{
    if (!ra3d_verify_on() || first.empty()) return;
    Ra3dCache& C = g_ra3dc;
    const TerrainSeams& S = g_ra3dSeams;
    TsPass P;
    if (!ts_pass(S, pass, &P)) return;
    const int W = g_raStaticMap.MapCellWidth;
    std::vector<float> geo(C.B.geo.size()), col(C.B.col.size());
    std::vector<unsigned> idx(C.idx.size());
    ra3d_verify_read(GL_ARRAY_BUFFER, C.B.geoVbo, geo.size() * sizeof(float), &geo[0]);
    ra3d_verify_read(GL_ARRAY_BUFFER, C.B.colVbo, col.size() * sizeof(float), &col[0]);
    ra3d_verify_read(GL_ELEMENT_ARRAY_BUFFER, C.B.idxVbo, idx.size() * sizeof(unsigned), &idx[0]);
    static const unsigned char notint[4][3] = { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} };
    for (size_t j = 0; j < first.size(); j++)
        for (size_t f = first[j]; f < first[j] + (size_t)count[j]; f += TS_IDX) {
            g_ra3dVerRecs++;
            const int st = (int)(f / TS_IDX), r = st - (int)C.stripFirst[pass];
            const int i = C.stripCell[pass][r];
            const int cx = g_raStaticMap.MapCellX + i % W, cy = g_raStaticMap.MapCellY + i / W;
            float u0, v0, u1, v1, L[4];
            ra3d_slot_uv(ra3d_cell_slot(i, cx, cy), &u0, &v0, &u1, &v1);
            ra3d_cell_light(cx, cy, L);
            bool bad = false;
            for (int ai = 0; ai <= TS_NA && !bad; ai++)
                for (int xi = 0; xi <= TS_NX && !bad; xi++) {
                    const size_t v = (size_t)st * TS_LAT + ai * (TS_NX + 1) + xi;
                    float fx, fz, out[7], rgba[4];
                    ts_corner_f(S, pass, ai, xi, &fx, &fz);
                    ts_corner_geo(S, pass, P, C.stripK[pass][r], cx, cy, u0, u1, v0, v1, 1.0f, 1.0f, fx, fz, out);
                    ts_corner_rgba(L, notint, false, fx, fz, rgba);
                    if (memcmp(out, &geo[v * 7], sizeof out)) { ra3d_verify_bad("strip corners", st); bad = true; }
                    else if (memcmp(rgba, &col[v * 4], sizeof rgba)) { ra3d_verify_bad("strip light", st); bad = true; }
                }
            for (int ia = 0; ia < TS_NA && !bad; ia++)
                for (int ix = 0; ix < TS_NX && !bad; ix++) {
                    int q[6];
                    ts_quad_corners(pass, ia, ix, q);
                    for (int k = 0; k < 6; k++)
                        if (idx[f + (size_t)(ia * TS_NX + ix) * 6 + k] != (unsigned)st * TS_LAT + (unsigned)q[k]) {
                            ra3d_verify_bad("strip index", st);
                            bad = true;
                            break;
                        }
                }
        }
}

/* Records [first, first + 6 n) of the ground stage as triangles: the colour array the
   stage's, or with `nrm` the normal pass's and no texture coordinates. Then the
   current colour and coordinate the last corner left, as the per-corner calls did. */
static void ra3d_cache_draw_runs(const std::vector<GLint>& first, const std::vector<GLsizei>& count, bool nrm)
{
    Ra3dCache& C = g_ra3dc;
    const GLuint bound = gls_array_binding();
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    tcache_arrays_begin(C.A, 5 * sizeof(float), false);
    if (nrm) {
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        gr_glBindBuffer(GL_ARRAY_BUFFER, C.nrmVbo);
        glColorPointer(3, GL_FLOAT, 3 * sizeof(float), (const void*)0);
    }
    for (size_t j = 0; j < first.size(); j++) glDrawArrays(GL_TRIANGLES, first[j], count[j]);
    glPopClientAttrib();
    gr_glBindBuffer(GL_ARRAY_BUFFER, bound);
    if (!nrm) {
        const size_t last = (size_t)(first.back() + count.back() - 1);
        const float* geo = &C.A.geo[last * 5];
        glTexCoord2f(geo[3], geo[4]);
    }
}

static inline void ra3d_run_add(std::vector<GLint>& first, std::vector<GLsizei>& count, int rec)
{
    if (!count.empty() && first.back() + count.back() == rec * 6) count.back() += 6;
    else { first.push_back(rec * 6); count.push_back(6); }
}

/* THE GROUND: one textured cell per map cell, two triangles split SW-NE as Tiberian
   Dawn's ground is, on the scene's heights, lit with the corner shade times the shroud.
   With `sea` the sea is drawn under it: the sheet's alpha (the water cut with its soft
   ramp) is blended over it, as Tiberian Dawn's terrain is over its sea; without, the
   alpha is not read and the ground is opaque. */
/* Optional three-material clear-ground finish. Eligibility comes from the engine's
   original template names, never a painted colour or a changed map template. Kept
   outside the ground cache: its existing geometry/light verification remains exact,
   and the same function below is called for the sea reflection as for the world. */
static void ra3d_draw_clear_blend(void)
{
    const Ra3dGround& G = g_ra3d;
    if (!g_fxActive || !g_fx.enabled || g_fx.dos_classic || !ra3d_own_ground()
        || !G.ok || (G.theater != 'T' && G.theater != 'S') || G.set != FX_TEX_ENH
        || !clear_blend_requested()) return;
    const int W = g_raStaticMap.MapCellWidth, H = g_raStaticMap.MapCellHeight;
    const int X0 = g_raStaticMap.MapCellX, Y0 = g_raStaticMap.MapCellY;
    if (W <= 0 || H <= 0 || g_gridW <= 0 || g_gridH <= 0) return;

    static std::vector<unsigned char> eligible;
    static std::vector<int> grassMasks;
    static unsigned sheet = ~0u;
    static int width = 0, height = 0, count = 0;
    if (sheet != g_ra3dSheetGen || width != g_gridW || height != g_gridH) {
        eligible.assign((size_t)g_gridW * g_gridH, 0);
        grassMasks.assign(eligible.size(),-1);
        count = 0;
        for (int i = 0; i < W * H; i++) {
            const int x = X0 + i % W, z = Y0 + i / W;
            if (x < 0 || z < 0 || x >= g_gridW || z >= g_gridH) continue;
            char name[17];
            ra3d_template_name(g_raStaticMap.StaticCells[i].TemplateTypeName, name);
            int mask=0;
            if(strcmp(name,"CLEAR1")) {
                const int slot=ra3d_cell_slot(i,x,z);
                if(slot<0)continue;
                const Ra3dSlot& material=G.slot[slot];
                mask=terrain_grass_mask_ra(name,material.icon,material.var);
            }
            if(mask<0)continue;
            const size_t ci=(size_t)z*g_gridW+x;
            eligible[ci]=1;grassMasks[ci]=mask;count++;
        }
        sheet = g_ra3dSheetGen; width = g_gridW; height = g_gridH;
    }
    if (!count || !clear_blend_prepare(g_gridW, g_gridH, eligible,
                                       clear_blend_seed(g_raStaticScene.c_str()), G.theater == 'S' ? 1.0f : g_fx.clear_saturation)) return;

    static std::vector<ClearBlendCell> visible;
    visible.clear();
    for (int i = 0; i < W * H; i++) {
        const int x = X0 + i % W, z = Y0 + i / W;
        if (x < 0 || z < 0 || x >= g_gridW || z >= g_gridH
            || !eligible[(size_t)z * g_gridW + x]
            || !cell_shown(x, z) || !cell_in_view(x, z)) continue;
        ClearBlendCell c;
        memset(&c, 0, sizeof c);
        c.x = x; c.z = z;c.grassMask=grassMasks[(size_t)z*g_gridW+x];
        if (G.theater == 'S' && c.grassMask > 0) {
            /* A continuing rock face keeps its authored matte. Only a neighbour's
               explicit exposed snow can soften the otherwise rectangular cell cut. */
            const int nx[4]={x-1,x+1,x,x},nz[4]={z,z,z-1,z+1};
            for(int e=0;e<4;e++) if(nx[e]>=0 && nz[e]>=0 && nx[e]<g_gridW && nz[e]<g_gridH)
                c.groundEdges[e]=(float)(grassMasks[(size_t)nz[e]*g_gridW+nx[e]]+1);
        }
        c.height[0] = terrain_corner_y(x, z);
        c.height[1] = terrain_corner_y(x + 1, z);
        c.height[2] = terrain_corner_y(x + 1, z + 1);
        c.height[3] = terrain_corner_y(x, z + 1);
        ra3d_cell_light(x, z, c.light);
        visible.push_back(c);
    }
    if (visible.empty()) return;
    clear_blend_draw(visible, false); /* Red Alert's ground has light, no TD vertex tint. */
    static std::string reported;
    if (reported != G.key) {
        fprintf(stderr, "CLEARBLEND|ra|scene=%s|theater=%s|original=CLEAR1+outlined-features|eligible=%d|visible=%lu\n",
                g_raStaticScene.c_str(), G.theater == 'S' ? "RA_SNOW" : "TEMPERAT", count, (unsigned long)visible.size());
        reported = G.key;
    }
}

static int g_ra3dCellsDrawn = 0;
static void ra3d_draw_ground(bool sea, bool deferSnow = false, bool opaqueDepth = false,
                             bool writeDepth = true, bool depthPrepared = false)
{
    GLint priorDepthFunc = GL_LESS;
    if (sea) glGetIntegerv(GL_DEPTH_FUNC, &priorDepthFunc);
    /* A feathered terrain pixel is a colour mixture, not an opaque height owner.
       Writing its raised native/physical height into the scene depth made a
       translucent blue fringe become a solid plate in every later post pass.
       Own depth only with the opaque core, then blend the colour without writes.
       Road joins and clear-blend mattes never participate in this depth prepass. */
    if (sea && !opaqueDepth && !depthPrepared) {
        glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_CURRENT_BIT);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_TRUE);
        ra3d_draw_ground(true, true, true);
        glPopAttrib();
    }
    g_ra3dCellsDrawn = 0;
    const Ra3dGround& G = g_ra3d;
    const int W = g_raStaticMap.MapCellWidth, H = g_raStaticMap.MapCellHeight;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D,sea&&G.dryTex?G.dryTex:G.tex);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    if (sea) {
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, opaqueDepth ? 0.999f : 0.02f);
        glDepthFunc(GL_LEQUAL); // colour must pass the opaque core's equal depth
        glDepthMask(opaqueDepth && writeDepth ? GL_TRUE : GL_FALSE);
        if (opaqueDepth) glDisable(GL_BLEND);
        else { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
    } else {
        glDisable(GL_ALPHA_TEST);
        glDisable(GL_BLEND);
    }
    if (ra3d_cache_ready()) {
        ra3d_cache_pass();
        Ra3dCache& C = g_ra3dc;
        static std::vector<GLint> first;
        static std::vector<GLsizei> count;
        first.clear(); count.clear();
        for (int r = 0; r < C.nGround; r++) {
            const int i = C.recCell[r];
            const int cx = g_raStaticMap.MapCellX + i % W, cy = g_raStaticMap.MapCellY + i / W;
            if (!cell_shown(cx, cy) || !cell_in_view(cx, cy) || terrain_surface_has_cell(cx,cy)) continue;
            ra3d_cache_light(i);
            g_ra3dCellsDrawn++;
            ra3d_run_add(first, count, r);
        }
        if (!first.empty()) {
            ra3d_cache_upload(C.A);
            ra3d_cache_draw_runs(first, count, false);
            ra3d_verify_cells(first, count, false, false);
        }
    } else {
    glBegin(GL_TRIANGLES);
    for (int i = 0; i < W * H; i++) {
        const int cx = g_raStaticMap.MapCellX + i % W, cy = g_raStaticMap.MapCellY + i / W;
        if (!cell_shown(cx, cy) || !cell_in_view(cx, cy) || terrain_surface_has_cell(cx,cy)) continue;
        const int s = ra3d_cell_slot(i, cx, cy);
        if (s < 0) continue;
        float u0, v0, u1, v1;
        ra3d_slot_uv(s, &u0, &v0, &u1, &v1);
        g_ra3dCellsDrawn++;
        const float x = (float)cx, z = (float)cy;
        float L[4];
        ra3d_cell_light(cx, cy, L);
        const float s00 = L[0], s10 = L[1], s11 = L[2], s01 = L[3];
        const float y00 = terrain_corner_y(cx, cy), y10 = terrain_corner_y(cx + 1, cy);
        const float y11 = terrain_corner_y(cx + 1, cy + 1), y01 = terrain_corner_y(cx, cy + 1);
        glColor3f(s00, s00, s00); glTexCoord2f(u0, v0); glVertex3f(x,        y00, z);
        glColor3f(s01, s01, s01); glTexCoord2f(u0, v1); glVertex3f(x,        y01, z + 1.0f);
        glColor3f(s10, s10, s10); glTexCoord2f(u1, v0); glVertex3f(x + 1.0f, y10, z);
        glColor3f(s10, s10, s10); glTexCoord2f(u1, v0); glVertex3f(x + 1.0f, y10, z);
        glColor3f(s01, s01, s01); glTexCoord2f(u0, v1); glVertex3f(x,        y01, z + 1.0f);
        glColor3f(s11, s11, s11); glTexCoord2f(u1, v1); glVertex3f(x + 1.0f, y11, z + 1.0f);
    }
    glEnd();
    }
    terrain_surface_draw(0,sea&&G.dryTex?G.dryTex:G.tex);
    if (!opaqueDepth && !deferSnow) ra3d_draw_road_joins();
    /* Reflections have no seam pass. In the main snow pass, apply the native
       ground matte after old strips so they cannot paint clipped cliff fragments back. */
    if (!opaqueDepth && (!deferSnow || G.theater != 'S')) ra3d_draw_clear_blend();
    if (sea) {
        glDisable(GL_BLEND);
        glDisable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 0.5f);
        glDepthMask(writeDepth ? GL_TRUE : GL_FALSE);
        glDepthFunc((GLenum)priorDepthFunc);
    }
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

/* THE SEA FLOOR: the holed cells drawn opaque from the same sheet, before the sea, so
   what the surface lets through and bends is Red Alert's own painted water (and the
   painted shore under the shallows). Tiberian Dawn's floor is the cartridge's BOTTOM
   tile, a sandy bed that turned Red Alert's blue to a milky green under the wash. Same
   XY corners and diagonal as the ground.  Enhanced keeps the opaque floor below
   its body's shared water plane, so autoheight cannot poke blue plates through it. */
static void ra3d_draw_floor(void)
{
    if (water_bed_draw()) return;
    const Ra3dGround& G = g_ra3d;
    const std::vector<PackCell>& cells = g_waterRa.cell;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, G.tex);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
    if (ra3d_cache_ready()) {
        ra3d_cache_pass();
        Ra3dCache& C = g_ra3dc;
        const int W = g_raStaticMap.MapCellWidth;
        static std::vector<GLint> first;
        static std::vector<GLsizei> count;
        first.clear(); count.clear();
        const int n = (int)C.recCell.size();
        for (int r = C.nGround; r < n; r++) {
            const int i = C.recCell[r];
            const int cx = g_raStaticMap.MapCellX + i % W, cy = g_raStaticMap.MapCellY + i / W;
            if (!cell_shown(cx, cy) || !cell_in_view(cx, cy)) continue;
            ra3d_cache_light(i);
            ra3d_run_add(first, count, r);
        }
        if (!first.empty()) {
            ra3d_cache_upload(C.A);
            ra3d_cache_draw_runs(first, count, false);
            ra3d_verify_cells(first, count, false, false);
        }
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        return;
    }
    glBegin(GL_TRIANGLES);
    for (size_t i = 0; i < cells.size(); i++) {
        const PackCell& c = cells[i];
        if ((!c.holes && !water_plane_covers(c)) || !cell_shown(c.x, c.y) || !cell_in_view(c.x, c.y)) continue;
        const int cx = c.x, cy = c.y;
        const float x = (float)cx, z = (float)cy;
        float L[4];
        ra3d_cell_light(cx, cy, L);
        const float s00 = L[0], s10 = L[1], s11 = L[2], s01 = L[3];
        const float y00 = water_plane_floor_y(c, cx, cy), y10 = water_plane_floor_y(c, cx + 1, cy);
        const float y11 = water_plane_floor_y(c, cx + 1, cy + 1), y01 = water_plane_floor_y(c, cx, cy + 1);
        glColor3f(s00, s00, s00); glTexCoord2f(c.u0, c.v0); glVertex3f(x,        y00, z);
        glColor3f(s01, s01, s01); glTexCoord2f(c.u0, c.v1); glVertex3f(x,        y01, z + 1.0f);
        glColor3f(s10, s10, s10); glTexCoord2f(c.u1, c.v0); glVertex3f(x + 1.0f, y10, z);
        glColor3f(s10, s10, s10); glTexCoord2f(c.u1, c.v0); glVertex3f(x + 1.0f, y10, z);
        glColor3f(s01, s01, s01); glTexCoord2f(c.u0, c.v1); glVertex3f(x,        y01, z + 1.0f);
        glColor3f(s11, s11, s11); glTexCoord2f(c.u1, c.v1); glVertex3f(x + 1.0f, y11, z + 1.0f);
    }
    glEnd();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

/* THE GROUND IN THE SEA'S MIRROR (water_mod.h's reflection, and the casters' reflection
   pass): cut where the sea is, so the mirror shows the sky there, as Tiberian Dawn's
   terrain does in its own. A pass of its own, so its cells' light is its own frame's. */
static void ra3d_reflect_ground(void)
{
    if (!g_ra3d.ok) return;
    ra3d_cache_frame();
    ra3d_draw_ground(water_fx_wanted());
}

// The physical S cliffs and their saved ground mattes replace the old colour-only
// skirts in both games. SH shore families and road joins keep their own strips.
static bool ra3d_unified_cliff_slot(int slot)
{
    const auto& G=g_ra3d;
    if(!fx_unified_lighting() || G.set!=FX_TEX_ENH || slot<0 || slot>=(int)G.slot.size())return false;
    const int id=G.slot[slot].name;if(id<0 || id>=(int)G.names.size())return false;
    const std::string& n=G.names[id];
    return n.size()>=2 && n[0]=='S' && n[1]>='0' && n[1]<='9';
}

/* THE JOIN BLEND over the ground, by Tiberian Dawn's rules and drawing code. */
static int g_ra3dStripsDrawn = 0;
static void ra3d_draw_seams(void)
{
    g_ra3dStripsDrawn = 0;
    const TerrainSeams& S = g_ra3dSeams;
    const Ra3dGround& G = g_ra3d;
    if (!S.ok || G.set != FX_TEX_ENH) return;
    const int W = g_raStaticMap.MapCellWidth, H = g_raStaticMap.MapCellHeight;
    const int X0 = g_raStaticMap.MapCellX, Y0 = g_raStaticMap.MapCellY;
    static const unsigned char notint[4][3] = { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} };
    if (ra3d_cache_ready()) {
        ra3d_cache_pass();
        /* both passes' light first: a strip of either pass is coloured from its cell's
           light, and the colours go down once, before the first strip is drawn */
        Ra3dCache& C = g_ra3dc;
        static std::vector<GLsizei> count[2];
        static std::vector<size_t> first[2];
        for (int pass = 0; pass < 2; pass++) {
            first[pass].clear(); count[pass].clear();
            const std::vector<int>& cellOf = C.stripCell[pass];
            const size_t base = C.stripFirst[pass];
            for (int r = 0; r < (int)cellOf.size(); r++) {
                const int i = cellOf[r];
                const int cx = X0 + i % W, cy = Y0 + i / W;
                if (!cell_shown(cx, cy) || !cell_in_view(cx, cy)) continue;
                if (!cell_shown(C.stripBx[pass][r], C.stripBy[pass][r])) continue;
                const int bi=(C.stripBy[pass][r]-Y0)*W+C.stripBx[pass][r]-X0;
                if(ra3d_unified_cliff_slot(G.cellBase[bi]))continue;
                ra3d_cache_light(i);
                g_ra3dStripsDrawn++;
                const size_t f = (base + (size_t)r) * TS_IDX;
                if (!count[pass].empty() && first[pass].back() + (size_t)count[pass].back() == f)
                    count[pass].back() += TS_IDX;
                else { first[pass].push_back(f); count[pass].push_back(TS_IDX); }
            }
        }
        ra3d_cache_upload(C.B);
        ts_strips_begin(false, G.tex);
        for (int pass = 0; pass < 2; pass++) {
            TsPass P;
            if (!ts_pass(S, pass, &P)) continue;
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D,water_fx_wanted()&&(pass?G.drySeamT:G.drySeamL)?(pass?G.drySeamT:G.drySeamL):P.tex);
            if (first[pass].empty()) continue;
            const GLuint bound = gls_array_binding();
            glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
            tcache_arrays_begin(C.B, 7 * sizeof(float), true);
            gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, C.B.idxVbo);
            for (size_t j = 0; j < first[pass].size(); j++)
                glDrawElements(GL_TRIANGLES, count[pass][j], GL_UNSIGNED_INT,
                               (const void*)(first[pass][j] * sizeof(unsigned)));
            gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            glPopClientAttrib();
            gr_glBindBuffer(GL_ARRAY_BUFFER, bound);
            ra3d_verify_strips(pass, first[pass], count[pass]);
            /* the current colour and coordinates the last corner left, as the batch did */
            const size_t last = C.idx[first[pass].back() + (size_t)count[pass].back() - 1];
            const float* col = &C.B.col[last * 4];
            const float* geo = &C.B.geo[last * 7];
            glColor4f(col[0], col[1], col[2], col[3]);
            glTexCoord2f(geo[3], geo[4]);
            glMultiTexCoord2f(GL_TEXTURE1, geo[5], geo[6]);
        }
        ts_strips_end(false, false, 0, G.tex);
        glBindTexture(GL_TEXTURE_2D, 0);
        return;
    }
    ts_strips_begin(false, G.tex);
    for (int pass = 0; pass < 2; pass++) {
        TsPass P;
        if (!ts_pass(S, pass, &P)) continue;
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,water_fx_wanted()&&(pass?G.drySeamT:G.drySeamL)?(pass?G.drySeamT:G.drySeamL):P.tex);
        g_tsBatch.clear();
        for (int i = 0; i < W * H; i++) {
            const int ax = i % W, ay = i / W, cx = X0 + ax, cy = Y0 + ay;
            if (!cell_shown(cx, cy) || !cell_in_view(cx, cy)) continue;
            const int sa = G.cellBase[i];
            if (sa < 0 || G.slot[sa].name == G.clearName) continue;          /* clear: nothing */
            const int bx = ax + (pass ? 0 : 1), by = ay + (pass ? 1 : 0);
            if (bx >= W || by >= H) continue;
            const int bi = by * W + bx;
            if (!cell_shown(X0 + bx, Y0 + by)) continue;
            const int sb = G.cellBase[bi];
            if (sb < 0 || G.slot[sb].name == G.clearName || ra3d_unified_cliff_slot(sb)) continue;
            if (ra3d_road_slot(sa) && ra3d_road_slot(sb) && ra3d_roads_ready()) continue;
            /* the strips of the painting that cell draws */
            const int drawn = ra3d_cell_slot(bi, X0 + bx, Y0 + by);
            const int sbv = sb * TS_VARS + (drawn == sb ? 0 : G.slot[drawn].var);
            const int k = (*P.idx)[sbv];
            if (k < 0) continue;
            if ((*P.feat)[sbv] && S.cliffTmpl[G.slot[sa].name & 0xFFFF]) continue;   /* never over a cliff */
            float s[4];
            ra3d_cell_light(cx, cy, s);
            float u0, v0, u1, v1;
            ra3d_slot_uv(ra3d_cell_slot(i, cx, cy), &u0, &v0, &u1, &v1);
            ts_strip_emit(S, pass, P, k, cx, cy, u0, u1, v0, v1, 1.0f, 1.0f, s, notint, false);
            g_ra3dStripsDrawn++;
        }
        imm_draw_triangles(g_tsBatch, true);
    }
    ts_strips_end(false, false, 0, G.tex);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/* The normal pass's colours for the ground's corners: the scene's corner normal
   (g_normGrid, terrain_shade_build's encoding), straight up where the ground is flat.
   Static until the shade is rebuilt or the shape is. FLOATS, byte times 1/255, for the
   reason ground_normals_static gives: the immediate glColor3ubv they replace produced
   exactly that on macOS, where a byte array is converted as byte / 255 correctly rounded
   and differs in the last bit for some bytes. Three of them, so alpha is glColor3ub's 1. */
static bool ra3d_cache_normals_ready(bool level)
{
    Ra3dCache& C = g_ra3dc;
    if (!ra3d_cache_ready() || C.nGround != C.nCells) return false;
    if (C.nrmBuilt && C.nrmLevel == (level ? 1 : 0) && C.nrmShade == g_shadeGen) return true;
    const int W = g_raStaticMap.MapCellWidth;
    std::vector<float> rgb((size_t)C.nGround * 6 * 3);
    static const int dx[6] = { 0, 0, 1, 1, 0, 1 }, dz[6] = { 0, 1, 0, 0, 1, 1 };
    static const unsigned char up[3] = { 128, 255, 128 };
    for (int r = 0; r < C.nGround; r++) {
        const int i = C.recCell[r];
        const int cx = g_raStaticMap.MapCellX + i % W, cy = g_raStaticMap.MapCellY + i / W;
        for (int j = 0; j < 6; j++) {
            const unsigned char* n = up;
            if (!level) {
                int x = cx + dx[j], z = cy + dz[j];
                x = x < 0 ? 0 : x > g_gridW ? g_gridW : x;
                z = z < 0 ? 0 : z > g_gridH ? g_gridH : z;
                n = g_normGrid[z * (g_gridW + 1) + x];
            }
            float* q = &rgb[((size_t)r * 6 + j) * 3];
            for (int k = 0; k < 3; k++) q[k] = (float)n[k] * (1.0f / 255.0f);
        }
    }
    const GLuint bound = gls_array_binding();
    const bool ok = ra3d_cache_store(&C.nrmVbo, GL_ARRAY_BUFFER, &rgb[0], rgb.size() * sizeof(float),
                                     GL_STATIC_DRAW);
    gr_glBindBuffer(GL_ARRAY_BUFFER, bound);
    if (!ok) return false;
    C.nrmBuilt = true;
    C.nrmLevel = level ? 1 : 0;
    C.nrmShade = g_shadeGen;
    return true;
}

/* The ground's normal for the post chain's sun: the scene's corner normal (g_normGrid,
   terrain_shade_build's encoding), straight up where the ground is flat, over the same
   cells the ground draws. */
static void ra3d_ground_normals(void)
{
    if (!g_ra3d.ok) return;
    const int W = g_raStaticMap.MapCellWidth, H = g_raStaticMap.MapCellHeight;
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    /* THE CORNER NORMALS ARE BUILT HERE, before the first frame's normal pass reads them,
       as Tiberian Dawn's branch of fx_draw_terrain_normals does. The grid is otherwise
       built lazily by the first caller of terrain_shade, and under one sun nothing on Red
       Alert's ground asks for the shade: the first caller was whatever drew a building's
       apron, often a few seconds into the mission. Until then this pass drew the ground
       straight up while the relief surface (terrain_surface.h) drew its own true slope,
       so every relief cell on a slope was lit as a hard-edged patch with a dark seam,
       and all of them changed at once on the frame the grid appeared. */
    const bool wasReady = g_shadeReady;
    const Uint64 t0 = SDL_GetPerformanceCounter();
    if (!ra3d_flat_ground() && !g_pack.corner.empty() && !g_shadeReady) terrain_shade_build();
    const bool level = ra3d_flat_ground() || g_pack.corner.empty() || !g_shadeReady;
    /* Once a mission, what the first normal pass drew the ground with: its corner normals
       ("built" here, or "ready" from an earlier caller), or "level" on a flat ground. */
    static std::string said;
    if (said != g_ra3d.key) {
        fprintf(stderr, "RA3D|normals|first pass|scene=%s|grid=%s|engine_frame=%d|drawn=%u|%.2f ms\n",
                g_raStaticScene.c_str(), level ? "level" : wasReady ? "ready" : "built",
                g_engineFrame, g_drawFrameSerial,
                (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency());
        said = g_ra3d.key;
    }
    if (ra3d_cache_normals_ready(level)) {
        Ra3dCache& C = g_ra3dc;
        static std::vector<GLint> first;
        static std::vector<GLsizei> count;
        first.clear(); count.clear();
        for (int r = 0; r < C.nGround; r++) {
            const int i = C.recCell[r];
            const int cx = g_raStaticMap.MapCellX + i % W, cy = g_raStaticMap.MapCellY + i / W;
            if (!cell_shown(cx, cy) || !cell_in_view(cx, cy)) continue;
            ra3d_run_add(first, count, r);
        }
        if (!first.empty()) {
            ra3d_cache_draw_runs(first, count, true);
            ra3d_verify_cells(first, count, true, level);
        }
        terrain_surface_draw(1,0);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        return;
    }
    glColor3ub(128, 255, 128);
    auto corner = [&](int cx, int cz) {
        if (!level) {
            const int x = cx < 0 ? 0 : cx > g_gridW ? g_gridW : cx;
            const int z = cz < 0 ? 0 : cz > g_gridH ? g_gridH : cz;
            glColor3ubv(g_normGrid[z * (g_gridW + 1) + x]);
        }
        glVertex3f((float)cx, terrain_corner_y(cx, cz), (float)cz);
    };
    glBegin(GL_TRIANGLES);
    for (int i = 0; i < W * H; i++) {
        const int cx = g_raStaticMap.MapCellX + i % W, cy = g_raStaticMap.MapCellY + i / W;
        if (!cell_shown(cx, cy) || !cell_in_view(cx, cy)) continue;
        corner(cx, cy);     corner(cx, cy + 1); corner(cx + 1, cy);
        corner(cx + 1, cy); corner(cx, cy + 1); corner(cx + 1, cy + 1);
    }
    glEnd();
    terrain_surface_draw(1,0);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

/* THE GROUND PASS, in draw_frame's place for the sea, the terrain and the coast: the
   sheet built (or found built), the cells, then the strips. Returns false with nothing
   drawn when the map or the 1995 tiles cannot be read. */
static bool ra3d_world_ground(void)
{
    if (!ra3d_ground_ensure()) return false;
    /* The floor is submitted before water_fx_draw, which used to be the first
       field synchronizer.  Its cached geometry now needs the body's plane too,
       including when physical terrain relief is disabled. */
    if (water_fx_wanted()) water_field_sync();
    static std::string reported;
    if (reported != g_ra3d.key) {
        /* heights=scene: laid on the mission's own scene's heightmap; flat: at 0 */
        fprintf(stderr, "RA3D|scene|ground=red alert|theater=%s|rect=%d,%d %dx%d|set=%s|heights=%s\n",
                ra3d_theater_name(g_ra3d.theater), g_raStaticMap.MapCellX, g_raStaticMap.MapCellY,
                g_raStaticMap.MapCellWidth, g_raStaticMap.MapCellHeight,
                g_ra3d.set == FX_TEX_ENH ? "enhanced" : "1995",
                ra3d_flat_ground() ? "flat" : "scene");
        reported = g_ra3d.key;
    }
    /* THE SEA (water_mod.h, on this map's source): capture the painted floor, draw
       the ground and its colour feathers, then resolve the live surface before foam.
       If the surface cannot draw, the ground stays opaque and the painted water shows.
       The frame profile's sections are Tiberian Dawn's names: the floor is the seabed,
       the surface the water, and the ground, its strips and the coast the terrain. */
    ra3d_cache_frame();
    bool sea = false;
    if (water_fx_wanted()) {
        ra3d_draw_floor();
        fprof_mark("seabed");
        sea = water_fx_draw();
        fprof_mark("water");
    }
    const bool coastColourDeferred = g_waterSurfaceDeferred;
    if(coastColourDeferred) {
        glPushAttrib(GL_ENABLE_BIT|GL_DEPTH_BUFFER_BIT|GL_COLOR_BUFFER_BIT);
        glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
        ra3d_draw_ground(true,true,true);
        glPopAttrib();
        water_fx_draw_after_terrain();
    }
    ra3d_draw_ground(sea, true, false, true, coastColourDeferred);
    ra3d_draw_seams();
    if (g_ra3d.theater == 'S') ra3d_draw_clear_blend();
    // Strips and the Snow matte must finish before the shared road colour.
    // Otherwise an old contact cap can paint a straight line over the join.
    ra3d_draw_road_joins();
    if (sea) water_shore_draw();
    {
        static int lastSea = -1;
        if ((int)sea != lastSea) {
            fprintf(stderr, "WATER|ra|frame|%s|sea=%d|field=%d|wet_texels=%d\n",
                    g_raStaticScene.c_str(), (int)sea, g_waterFieldTex ? 1 : 0, g_waterFieldWet);
            lastSea = (int)sea;
        }
    }
    /* OPENCNC3D_RA3DFRAME=1: what the frame laid, each time that changes (the gate's evidence
       that the ground and the strips are drawn, not only loaded) */
    static int lastCells = -1, lastStrips = -1;
    if ((g_ra3dCellsDrawn != lastCells || g_ra3dStripsDrawn != lastStrips)
        && getenv("OPENCNC3D_RA3DFRAME")) {
        fprintf(stderr, "RA3D|frame|%s|cells=%d|strips=%d|cam=%.1f,%.1f\n",
                g_raStaticScene.c_str(), g_ra3dCellsDrawn, g_ra3dStripsDrawn, g_camX, g_camZ);
        lastCells = g_ra3dCellsDrawn; lastStrips = g_ra3dStripsDrawn;
    }
    /* OPENCNC3D_RA3DCACHE_VERIFY=1: every frame, what the instrument checked and found,
       and how much light the frame had to send again (cells recoloured, stores sent) */
    if (ra3d_verify_on()) {
        fprintf(stderr, "GROUND|ra|verify|%s|cache=%d|records=%ld|bad=%ld|recoloured=%ld|uploads=%ld\n",
                g_raStaticScene.c_str(), g_ra3dc.built ? 1 : 0, g_ra3dVerRecs, g_ra3dVerBad,
                g_ra3dc.recoloured, g_ra3dc.uploads);
        g_ra3dc.recoloured = g_ra3dc.uploads = 0;
    }
    return true;
}

/* THE RADAR'S GROUND, from the same map and sheet: one entry per map cell, its painting's
   mean colour (radar_plot_into asks while this ground is the one drawn). */
static bool ra3d_radar_on(void) { return ra3d_own_ground() && g_ra3d.ok && !g_ra3d.cellBase.empty(); }
static int ra3d_radar_count(void) { return (int)g_ra3d.cellBase.size(); }
static void ra3d_radar_cell(int i, int* x, int* y, unsigned char rgb[3])
{
    const int W = g_raStaticMap.MapCellWidth > 0 ? g_raStaticMap.MapCellWidth : 1;
    *x = g_raStaticMap.MapCellX + i % W;
    *y = g_raStaticMap.MapCellY + i / W;
    const int s = g_ra3d.cellBase[i];
    if (s < 0) { rgb[0] = rgb[1] = rgb[2] = 0; return; }
    memcpy(rgb, &g_ra3d.avg[(size_t)s * 3], 3);
}

#endif /* OPENCNC3D_RA3D_TERRAIN_H */
