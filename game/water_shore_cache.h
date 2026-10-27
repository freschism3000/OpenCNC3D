#ifndef CNC3D_WATER_SHORE_CACHE_H
#define CNC3D_WATER_SHORE_CACHE_H

#include <array>
#include <algorithm>
#include <cassert>
#include <map>
#include <string>
#include <utility>

/* Static shoreline geometry only. The caller still supplies this frame's light
   and shroud at the four cell corners. Eight subdivisions match the terrain's
   relief lattice; shared triangle vertices reuse the same 81 height samples. */
struct WaterShoreNode {
    float wx, wz, y, lift, coast, contactLift;
};

struct WaterShoreCacheKey {
    unsigned ground, pad, surface, water;
    int width, height;
    float base;
    bool flatGround;
    std::string scene;

    bool operator==(const WaterShoreCacheKey& other) const
    {
        return ground == other.ground && pad == other.pad
            && surface == other.surface && water == other.water && width == other.width
            && height == other.height && base == other.base
            && flatGround == other.flatGround && scene == other.scene;
    }
};

struct WaterShoreCellNodes {
    std::array<WaterShoreNode,81> nodes;
    bool coarse=false;
    WaterShoreNode& operator[](size_t i){return nodes[i];}
    const WaterShoreNode& operator[](size_t i)const{return nodes[i];}
};

/* Only an exact native two-triangle surface with constant scope can lose its
   subdivisions. Relief and river mouths keep their full shared lattice. */
static bool water_shore_flat_film(const WaterShoreCellNodes& cell)
{
    const float a=cell[0].y,b=cell[8].y,c=cell[72].y,d=cell[80].y;
    const float la=cell[0].contactLift,lb=cell[8].contactLift,lc=cell[72].contactLift,ld=cell[80].contactLift;
    for(int z=0;z<=8;z++)for(int x=0;x<=8;x++) {
        const auto& n=cell[z*9+x];const float fx=x*.125f,fz=z*.125f;
        const float y=fx+fz<=1?a+(b-a)*fx+(c-a)*fz:d+(c-d)*(1-fx)+(b-d)*(1-fz);
        const float lift=fx+fz<=1?la+(lb-la)*fx+(lc-la)*fz:ld+(lc-ld)*(1-fx)+(lb-ld)*(1-fz);
        if(fabsf(n.contactLift-lift)>1e-6f || n.lift!=0 || n.coast!=cell[0].coast || fabsf(n.y-y)>1e-6f)return false;
    }
    return true;
}
static bool water_shore_linear_weights(const float values[4])
{ return fabsf(values[0]-values[1]-values[2]+values[3])<1e-7f; }

static std::map<std::pair<int, int>, WaterShoreCellNodes> g_waterShoreNodes;
static WaterShoreCacheKey g_waterShoreCacheKey;
static bool g_waterShoreCacheValid = false;
static WaterShoreCellNodes* g_waterShoreLastCell = NULL;
static int g_waterShoreLastX = 0, g_waterShoreLastZ = 0;

/* Once before a shore draw, after terrain_surface_prepare_scene. The surface
   accessor returns zero when relief is off or unavailable, so a mode toggle
   cannot reuse raised nodes for the native ground (or the reverse). */
static void water_shore_cache_prepare(void)
{
    WaterShoreCacheKey key;
    key.ground = g_groundGen;
    key.pad = g_padEpoch;
    key.surface = terrain_surface_generation();
    key.water = water_plane_epoch();
    key.width = g_gridW;
    key.height = g_gridH;
    key.base = g_terrainBase;
    key.flatGround = ra3d_flat_ground();
    key.scene = g_pack.scen;
    if (g_waterShoreCacheValid && key == g_waterShoreCacheKey) return;
    g_waterShoreNodes.clear();
    g_waterShoreLastCell = NULL;
    g_waterShoreCacheKey = key;
    g_waterShoreCacheValid = true;
}

/* nx and nz are lattice indices in [0,8]. References remain valid until the
   next prepare call that observes a changed geometry key. The last-cell path
   avoids a map lookup for every emitted triangle vertex. */
static const WaterShoreNode& water_shore_node(int cx, int cz, int nx, int nz)
{
    assert(nx >= 0 && nx <= 8 && nz >= 0 && nz <= 8);
    if (!g_waterShoreCacheValid) water_shore_cache_prepare();
    if (!g_waterShoreLastCell || cx != g_waterShoreLastX || cz != g_waterShoreLastZ) {
        const std::pair<int, int> key(cx, cz);
        auto found = g_waterShoreNodes.find(key);
        if (found == g_waterShoreNodes.end()) {
            found = g_waterShoreNodes.insert(std::make_pair(key, WaterShoreCellNodes())).first;
            for (int z = 0; z <= 8; ++z) for (int x = 0; x <= 8; ++x) {
                WaterShoreNode& node = found->second[z * 9 + x];
                node.wx = (float)cx + (float)x * 0.125f;
                node.wz = (float)cz + (float)z * 0.125f;
                node.coast = water_swash_coast_at(node.wx, node.wz);
                node.lift = terrain_surface_height(node.wx, node.wz);
                const float base=terrain_y_base(node.wx,node.wz);
                node.y = base + node.lift;
                node.contactLift=std::max(0.0f,node.y-water_plane_height(node.wx,node.wz,base));
            }
            found->second.coarse=water_shore_flat_film(found->second);
        }
        g_waterShoreLastCell = &found->second;
        g_waterShoreLastX = cx;
        g_waterShoreLastZ = cz;
    }
    return (*g_waterShoreLastCell)[nz * 9 + nx];
}

#endif
