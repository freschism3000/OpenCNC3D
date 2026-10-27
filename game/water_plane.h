/* Enhanced water meets the native shoreline at the native shared corner heights.
   Submerged terrain is simulated by the surface shader.  This helper is included
   by water_mod.h after its source declarations; it does not own a draw pass. */
#ifndef OPENCNC3D_WATER_PLANE_H
#define OPENCNC3D_WATER_PLANE_H

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

struct WaterPlaneState {
    bool ready = false, ra = false;
    unsigned groundGen = 0, holesGen = 0, epoch = 1;
    int gridW = 0, gridH = 0;
    int rect[4] = {0,0,0,0};
    float base = 0.0f, emittedNativeError = 0.0f;
    std::string scene;
    std::vector<int> body;             // world cell -> connected body, -1: native
    std::vector<int> shoreBody;        // nearest body for dry swash-band cells
    std::vector<float> shoreDistance;
    std::vector<float> level;          // source body mode (connectivity diagnostics only)
    std::vector<unsigned char> wet;    // exact wet texels; support never floods dry shore
    std::vector<unsigned char> open;   // world cell -> 1 when every texel of it is wet
    std::vector<float> surf;           // covered cell x 4 corners (NW NE SW SE): surface Y
    std::vector<float> floor;          // map corner -> the floor's Y, shared by its cells
    int wetW = 0, wetH = 0, wetRes = 0;
    bool audit = false, auditReported = false, flatBed = true;
    std::vector<float> emittedMin, emittedMax;
    std::vector<size_t> emittedCount;
    size_t emittedLowered = 0;
};
static WaterPlaneState g_waterPlane;

/* Only the native fallback floor uses this gap. Enhanced water has no rendered
   seabed; the surface shader supplies its virtual submerged bank. */
static const float WATER_PLANE_BED_GAP = 0.04f;

static void water_plane_forget(void)
{
    WaterPlaneState& P = g_waterPlane;
    P.ready = false;
    P.body.clear(); P.shoreBody.clear(); P.shoreDistance.clear(); P.level.clear();
    P.wet.clear(); P.open.clear(); P.surf.clear(); P.floor.clear(); P.wetW = P.wetH = P.wetRes = 0;
    P.emittedMin.clear(); P.emittedMax.clear(); P.emittedCount.clear();
    ++P.epoch;
}

static bool water_plane_valid(void)
{
    const WaterPlaneState& P = g_waterPlane;
    return P.ready && P.groundGen == g_groundGen && P.holesGen == g_cellHolesGen && P.ra == water_src_ra()
        && P.gridW == g_gridW && P.gridH == g_gridH && P.base == g_terrainBase
        && P.rect[0] == g_mapX && P.rect[1] == g_mapY && P.rect[2] == g_mapW && P.rect[3] == g_mapH
        && P.scene == g_pack.scen;
}

static bool water_plane_on(void)
{
    return g_fxActive && g_fx.water_fx && water_plane_valid();
}

/* Cache keys must include this value: changing water_fx must not leave a lowered
   floor cached when the native water pass is selected, or vice versa. */
static unsigned water_plane_epoch(void)
{
    return water_plane_on() ? g_waterPlane.epoch : 0;
}

static int water_plane_root(std::vector<int>& parent, int i)
{
    int root = i;
    while (parent[root] != root) root = parent[root];
    while (parent[i] != i) { const int next = parent[i]; parent[i] = root; i = next; }
    return root;
}

static void water_plane_join(std::vector<int>& parent, int a, int b)
{
    a = water_plane_root(parent, a); b = water_plane_root(parent, b);
    if (a != b) parent[std::max(a, b)] = std::min(a, b);
}

/* FALLS1/2 are authored drops, not still water.  Keep their original triangles and
   do not use them to join the upper and lower body into a single elevation. */
static void water_plane_falls(std::vector<unsigned char>& falls)
{
    if (water_src_ra()) return;
    int count = 0;
    const TtSlot* table = tt_table(g_pack.theater, &count);
    std::map<int, bool> slots;
    for (int i = 0; i < count; ++i)
        if (table[i].tmpl == 163 || table[i].tmpl == 164) slots[table[i].slot] = true;
    if (slots.empty()) return;
    const std::vector<PackCell>& cells = water_cells();
    for (size_t i = 0; i < cells.size(); ++i) {
        const PackCell& c = cells[i];
        if (c.x < 0 || c.y < 0 || c.x >= g_gridW || c.y >= g_gridH) continue;
        if (slots.find(ts_slot_of_cell(c, count)) != slots.end())
            falls[(size_t)c.y * g_gridW + c.x] = 1;
    }
}

static int water_plane_raw_corner(int x, int z)
{
    const size_t i = (size_t)z * (g_gridW + 1) + x;
    return i < g_pack.corner.size() ? g_pack.corner[i] : 0;
}

/* The opaque road deck hides the water cut under a TD bridge; that is an
   occluder, not a dam. Join existing wet bodies touching one connected native
   bridge footprint. Dry deck cells never become water cells or gain support Y.
   A neighbouring FALLS stamp prevents a shortcut across an authored drop. */
static int water_plane_bridge_hydrology(std::vector<int>& parent,
                                       const std::vector<unsigned char>& falls,
                                       std::vector<int>& bridge)
{
    bridge.assign(parent.size(), 0);
    if (water_src_ra()) return 0;
    int count = 0;
    const TtSlot* table = tt_table(g_pack.theater, &count);
    std::map<int, int> slots;
    for (int i = 0; i < count; ++i)
        if (table[i].tmpl >= 165 && table[i].tmpl <= 172) slots[table[i].slot] = table[i].tmpl;
    if (slots.empty()) return 0;
    const std::vector<PackCell>& cells = water_cells();
    for (size_t i = 0; i < cells.size(); ++i) {
        const PackCell& c = cells[i];
        if (c.x < 0 || c.y < 0 || c.x >= g_gridW || c.y >= g_gridH) continue;
        const auto found = slots.find(ts_slot_of_cell(c, count));
        if (found != slots.end()) bridge[(size_t)c.y * g_gridW + c.x] = found->second;
    }
    std::vector<unsigned char> seen(parent.size(), 0);
    std::vector<int> stack, water;
    int links = 0;
    for (size_t seed = 0; seed < bridge.size(); ++seed) {
        if (!bridge[seed] || seen[seed]) continue;
        const int type = bridge[seed]; bool hasFalls = false;
        stack.clear(); water.clear(); stack.push_back((int)seed); seen[seed] = 1;
        while (!stack.empty()) {
            const int c = stack.back(); stack.pop_back();
            if (parent[c] >= 0) water.push_back(c);
            const int x = c % g_gridW, z = c / g_gridW;
            const int nb[4] = {x > 0 ? c - 1 : -1, x + 1 < g_gridW ? c + 1 : -1,
                               z > 0 ? c - g_gridW : -1, z + 1 < g_gridH ? c + g_gridW : -1};
            for (int k = 0; k < 4; ++k) {
                const int n = nb[k]; if (n < 0) continue;
                if (falls[n]) { hasFalls = true; continue; }
                if (bridge[n] == type) {
                    if (!seen[n]) { seen[n] = 1; stack.push_back(n); }
                } else if (parent[n] >= 0) water.push_back(n);
            }
        }
        if (hasFalls || water.empty()) continue;
        for (size_t i = 1; i < water.size(); ++i)
            if (water_plane_root(parent, water[0]) != water_plane_root(parent, water[i])) {
                water_plane_join(parent, water[0], water[i]); ++links;
            }
    }
    return links;
}

/* Match the native water's shared corners and triangle diagonal. The old modal
   body elevation lowered many shores and needed dense closure meshes. Native
   contact heights close those edges themselves; depth is now only a shader
   illusion. Open water retains the source's flat areas, without changing the
   authored falls or bridging disconnected wet regions. */
static int water_plane_lay_heights(void)
{
    WaterPlaneState& P = g_waterPlane;
    const int W=g_gridW,H=g_gridH,CW=W+1;
    P.surf.assign((size_t)W*H*4,0.0f);
    P.floor.assign((size_t)CW*(H+1),1.0e9f);
    for(int z=0;z<=H;z++)for(int x=0;x<=W;x++) {
        const float native=terrain_corner_y(x,z);
        for(int dz=-1;dz<=0;dz++)for(int dx=-1;dx<=0;dx++) {
            const int cx=x+dx,cz=z+dz;
            if(cx<0||cz<0||cx>=W||cz>=H)continue;
            const size_t cell=(size_t)cz*W+cx;
            if(P.shoreBody[cell]<0)continue;
            P.surf[cell*4+(-dx)+2*(-dz)]=native;
            P.floor[(size_t)z*CW+x]=native-WATER_PLANE_BED_GAP;
        }
    }
    return 0;
}

/* Build from precisely the same wet mask the shared shore field uses.  A cell's
   water is rendered by one quad, so all wet fragments in that cell share its body;
   neighbouring cells join only where wet texels actually touch across their edge.
   This avoids joining two lakes merely because their shore tiles are adjacent. */
static void water_plane_build(const std::vector<unsigned char>& wet, int w, int h, int res)
{
    water_plane_forget();
    if (res <= 0 || w != g_gridW * res || h != g_gridH * res
        || wet.size() != (size_t)w * h || g_gridW <= 0 || g_gridH <= 0) return;
    WaterPlaneState& P = g_waterPlane;
    const size_t cells = (size_t)g_gridW * g_gridH;
    std::vector<unsigned char> falls(cells, 0);
    water_plane_falls(falls);
    std::vector<int> parent(cells, -1), wetCount(cells, 0);
    int fallsN = 0;
    for (int z = 0; z < g_gridH; ++z) for (int x = 0; x < g_gridW; ++x) {
        const int c = z * g_gridW + x;
        if (falls[c]) { ++fallsN; continue; }
        int n = 0;
        for (int j = 0; j < res; ++j) for (int k = 0; k < res; ++k)
            n += wet[(size_t)(z * res + j) * w + x * res + k] != 0;
        if (n) { parent[c] = c; wetCount[c] = n; }
    }
    for (int z = 0; z < g_gridH; ++z) for (int x = 0; x < g_gridW; ++x) {
        const int c = z * g_gridW + x;
        if (parent[c] < 0) continue;
        if (x + 1 < g_gridW && parent[c + 1] >= 0) {
            for (int j = 0; j < res; ++j) {
                const size_t a = (size_t)(z * res + j) * w + (x + 1) * res - 1;
                if (wet[a] && (wet[a + 1] || (j > 0 && wet[a + 1 - w])
                    || (j + 1 < res && wet[a + 1 + w]))) {
                    water_plane_join(parent, c, c + 1); break;
                }
            }
        }
        if (z + 1 < g_gridH && parent[c + g_gridW] >= 0) {
            for (int k = 0; k < res; ++k) {
                const size_t a = (size_t)((z + 1) * res - 1) * w + x * res + k;
                if (wet[a] && (wet[a + w] || (k > 0 && wet[a + w - 1])
                    || (k + 1 < res && wet[a + w + 1]))) {
                    water_plane_join(parent, c, c + g_gridW); break;
                }
            }
        }
        /* Eight-connected wet texels also meet at a cell corner.  An authored
           falls cell on either orthogonal side remains a barrier, so this cannot
           bypass the drop by joining the upper/lower body around its corner. */
        if (z + 1 < g_gridH) for (int dx = -1; dx <= 1; dx += 2) {
            const int nx = x + dx, next = (z + 1) * g_gridW + nx;
            if (nx < 0 || nx >= g_gridW || parent[next] < 0
                || falls[(size_t)z * g_gridW + nx] || falls[(size_t)(z + 1) * g_gridW + x]) continue;
            const int ax = dx > 0 ? (x + 1) * res - 1 : x * res;
            const int bx = dx > 0 ? nx * res : (nx + 1) * res - 1;
            if (wet[(size_t)((z + 1) * res - 1) * w + ax]
                && wet[(size_t)((z + 1) * res) * w + bx]) water_plane_join(parent, c, next);
        }
    }
    std::vector<int> bridge;
    const int bridgeLinks = water_plane_bridge_hydrology(parent, falls, bridge);
    P.body.assign(cells, -1);
    std::map<int, int> bodyOf;
    for (size_t i = 0; i < cells; ++i) if (parent[i] >= 0) {
        const int root = water_plane_root(parent, (int)i);
        std::map<int, int>::iterator found = bodyOf.find(root);
        if (found == bodyOf.end()) found = bodyOf.insert(std::make_pair(root, (int)bodyOf.size())).first;
        P.body[i] = found->second;
    }
    typedef std::map<int, size_t> Histogram;
    std::vector<Histogram> pure(bodyOf.size()), fallback(bodyOf.size()), bridgeFallback(bodyOf.size());
    int pureN = 0;
    /* A native corner votes only if water touches it from every in-map quadrant.
       No pad heights and no physical rock relief enter the elevation authority.
       Native bridge paint can reveal new water without changing the river level;
       corners touching its footprint are deck/approach evidence, not water height.
       Count each unique corner once, rather than overweighting interior quads. */
    for (int z = 0; z <= g_gridH; ++z) for (int x = 0; x <= g_gridW; ++x) {
        if (x < g_mapX || z < g_mapY || x > g_mapX + g_mapW || z > g_mapY + g_mapH) continue;
        int id = -1; bool allWater = true;
        for (int dz = -1; dz <= 0 && allWater; ++dz) for (int dx = -1; dx <= 0; ++dx) {
            const int cx = x + dx, cz = z + dz;
            if (cx < 0 || cz < 0 || cx >= g_gridW || cz >= g_gridH) continue;
            const size_t at = (size_t)cz * g_gridW + cx;
            if (bridge[at]) { allWater = false; break; }
            const int body = P.body[at];
            const int tx = x * res + (dx < 0 ? -1 : 0), tz = z * res + (dz < 0 ? -1 : 0);
            if (body < 0 || !wet[(size_t)tz * w + tx] || (id >= 0 && id != body)) {
                allWater = false; break;
            }
            id = body;
        }
        if (allWater && id >= 0) { ++pure[id][water_plane_raw_corner(x, z)]; ++pureN; }
    }
    /* A very narrow channel may contain no pure-water corner.  Its connected body
       then takes the modal lower native corner of its cells, weighted by water
       area. Natural wet cells take priority over native bridge paint. A body
       wholly within a bridge keeps bridge-only evidence as a final fallback.
       The result is still one shared plane, never per-cell low plates. */
    for (int z = 0; z < g_gridH; ++z) for (int x = 0; x < g_gridW; ++x) {
        if (x < g_mapX || z < g_mapY || x >= g_mapX + g_mapW || z >= g_mapY + g_mapH) continue;
        const int c = z * g_gridW + x, id = P.body[c];
        if (id < 0 || !pure[id].empty()) continue;
        const int lo = std::min(std::min(water_plane_raw_corner(x, z), water_plane_raw_corner(x + 1, z)),
                                std::min(water_plane_raw_corner(x, z + 1), water_plane_raw_corner(x + 1, z + 1)));
        (bridge[c] ? bridgeFallback[id] : fallback[id])[lo] += (size_t)wetCount[c];
    }
    const bool flat = g_pack.corner.empty() || ra3d_flat_ground();
    P.level.assign(bodyOf.size(), 0.0f);
    int fallbackN = 0, unsupportedN = 0, levelN = 0;
    float minLevel = 0.0f, maxLevel = 0.0f;
    for (size_t i = 0; i < P.level.size(); ++i) {
        const Histogram& votes = !pure[i].empty() ? pure[i]
            : !fallback[i].empty() ? fallback[i] : bridgeFallback[i];
        if (votes.empty()) { ++unsupportedN; continue; }
        fallbackN += pure[i].empty();
        int mode = 0; size_t most = 0;
        for (Histogram::const_iterator it = votes.begin(); it != votes.end(); ++it)
            if (it->second > most) { mode = it->first; most = it->second; }
        P.level[i] = flat ? 0.0f : (float)mode * (1.0f / 64.0f) - g_terrainBase;
        if (!levelN || P.level[i] < minLevel) minLevel = P.level[i];
        if (!levelN || P.level[i] > maxLevel) maxLevel = P.level[i];
        ++levelN;
    }
    /* Border heights are not elevation evidence. A body connected to playable
       water carries that body's authority through the border; a wholly outside
       body with no playable evidence retains its native geometry. */
    for (size_t i = 0; i < P.body.size(); ++i) if (P.body[i] >= 0
        && pure[P.body[i]].empty() && fallback[P.body[i]].empty()
        && bridgeFallback[P.body[i]].empty()) P.body[i] = -1;
    /* The shore overlay visits dry cells as well.  Extend the elevation authority
       to their nearest body, without extending the rendered water mask.  Eight
       neighbours retain diagonal beach coverage; four cells exceed the shader's
       3.10-cell land band.  Falls remain a native-height barrier. */
    P.shoreBody = P.body;
    P.shoreDistance.assign(cells, 1.0e9f);
    typedef std::pair<float, int> ShoreStep;
    std::priority_queue<ShoreStep, std::vector<ShoreStep>, std::greater<ShoreStep> > todo;
    for (size_t i = 0; i < cells; ++i) if (P.body[i] >= 0) {
        P.shoreDistance[i] = 0.0f; todo.push(ShoreStep(0.0f, (int)i));
    }
    while (!todo.empty()) {
        const ShoreStep at = todo.top(); todo.pop();
        if (at.first != P.shoreDistance[at.second] || at.first >= 4.0f) continue;
        const int x = at.second % g_gridW, z = at.second / g_gridW;
        for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dz) continue;
            const int nx = x + dx, nz = z + dz;
            if (nx < 0 || nz < 0 || nx >= g_gridW || nz >= g_gridH) continue;
            const int next = nz * g_gridW + nx;
            const float dist = at.first + (dx && dz ? 1.41421356237f : 1.0f);
            if (falls[next] || dist > 4.0f || dist >= P.shoreDistance[next]) continue;
            P.shoreDistance[next] = dist; P.shoreBody[next] = P.shoreBody[at.second];
            todo.push(ShoreStep(dist, next));
        }
    }
    P.ra = water_src_ra(); P.groundGen = g_groundGen; P.holesGen = g_cellHolesGen;
    P.gridW = g_gridW; P.gridH = g_gridH; P.base = g_terrainBase; P.scene = g_pack.scen;
    P.rect[0] = g_mapX; P.rect[1] = g_mapY; P.rect[2] = g_mapW; P.rect[3] = g_mapH;
    P.wet = wet; P.wetW = w; P.wetH = h; P.wetRes = res;
    P.open.assign(cells, 0);
    for (size_t i = 0; i < cells; ++i) P.open[i] = wetCount[i] == res * res;
    P.ready = true;
    P.audit = getenv("OPENCNC3D_WATER_PLANE_AUDIT") != NULL; P.auditReported = false;
    const char* flatBed = getenv("OPENCNC3D_WATER_FLAT_BED");
    P.flatBed = !flatBed || !*flatBed || strcmp(flatBed, "0");
    water_plane_lay_heights();
    if (P.audit) {
        P.emittedMin.assign(P.level.size(), 1.0e9f); P.emittedMax.assign(P.level.size(), -1.0e9f);
        P.emittedCount.assign(P.level.size(), 0); P.emittedLowered = 0; P.emittedNativeError=0;
    }
    fprintf(stderr, "WATER|plane|scen=%s|bodies=%lu|pure_corners=%d|fallback_bodies=%d|native_border_bodies=%d|bridge_links=%d|y=%.4f..%.4f|native_falls_cells=%d|bed_gap=%.3f|height=native-contact|virtual-bed=shader\n",
            g_pack.scen, (unsigned long)P.level.size(), pureN, fallbackN, unsupportedN, bridgeLinks, minLevel, maxLevel, fallsN, WATER_PLANE_BED_GAP);
}

static int water_plane_body(int cx, int cz)
{
    if (!water_plane_on() || cx < 0 || cz < 0 || cx >= g_gridW || cz >= g_gridH) return -1;
    return g_waterPlane.body[(size_t)cz * g_gridW + cx];
}

/* The flat sheet must continue underneath the raised bank, otherwise the alpha
   cut's projected lip can expose clear colour behind a finite wet-cell quad.
   Ground still draws last and supplies coverage; this extension changes no wet
   mask, swash classification or visible support on dry land. */
static int water_plane_render_body(int cx, int cz)
{
    if (!water_plane_on() || cx < 0 || cz < 0 || cx >= g_gridW || cz >= g_gridH) return -1;
    return g_waterPlane.shoreBody[(size_t)cz * g_gridW + cx];
}

static bool water_plane_covers(const PackCell& c)
{
    return water_plane_render_body(c.x, c.y) >= 0;
}

static float water_plane_cell_y(const PackCell& c, int cx, int cz)
{
    const int body = water_plane_render_body(c.x, c.y);
    if (body < 0) return terrain_corner_y(cx, cz);
    WaterPlaneState& P = g_waterPlane;
    const int k = (cx - c.x) + 2 * (cz - c.y);
    const float y = k >= 0 && k < 4 ? P.surf[((size_t)c.y * g_gridW + c.x) * 4 + k] : P.level[body];
    if (P.audit && !P.auditReported) {
        P.emittedNativeError=std::max(P.emittedNativeError,fabsf(y-terrain_corner_y(cx,cz)));
        P.emittedMin[body]=std::min(P.emittedMin[body],y);
        P.emittedMax[body]=std::max(P.emittedMax[body],y);
        ++P.emittedCount[body];
    }
    return y;
}

/* Report submitted geometry, including the independent native-height error.
   Source contact heights may vary within a body; they must agree exactly with
   native corners and neighbouring cells, rather than with a modal flat level. */
static void water_plane_audit_report(void)
{
    WaterPlaneState& P = g_waterPlane;
    if (!P.audit || P.auditReported || !water_plane_on()) return;
    size_t vertices = 0, bodies = 0; float maxSpan = 0.0f;
    for (size_t i = 0; i < P.emittedCount.size(); ++i) if (P.emittedCount[i]) {
        ++bodies; vertices += P.emittedCount[i];
        maxSpan = std::max(maxSpan, P.emittedMax[i] - P.emittedMin[i]);
    }
    fprintf(stderr, "WATER|plane-audit|scen=%s|emitted_bodies=%lu|vertices=%lu|max_body_y_span=%.9f|native_height_max_error=%.9f|generated_bank_vertices=0\n",
            g_pack.scen, (unsigned long)bodies, (unsigned long)vertices, maxSpan, P.emittedNativeError);
    P.auditReported = true;
}

static float water_plane_floor_y(const PackCell& c, int cx, int cz)
{
    if (water_plane_render_body(c.x, c.y) < 0 || cx < 0 || cz < 0 || cx > g_gridW || cz > g_gridH)
        return terrain_corner_y(cx, cz);
    return g_waterPlane.floor[(size_t)cz * (g_gridW + 1) + cx];
}

/* The surface's height inside a covered cell, on the two triangles it is drawn as
   (split NE to SW, as the ground is). */
static float water_plane_surface_at(int cx, int cz, float wx, float wz)
{
    const float* s = &g_waterPlane.surf[((size_t)cz * g_gridW + cx) * 4];
    const float fx = wx - (float)cx, fz = wz - (float)cz;
    if (fx + fz <= 1.0f) return s[0] + (s[1] - s[0]) * fx + (s[2] - s[0]) * fz;
    return s[3] + (s[2] - s[3]) * (1.0f - fx) + (s[1] - s[3]) * (1.0f - fz);
}

/* The shore's lift gate needs the nearest water's height even on the dry half of a
   coast cell.  This query changes no coverage; beyond the swash band and on the
   authored falls it retains the caller's native-height fallback. */
static float water_plane_height(float wx, float wz, float fallback)
{
    if (wx < 0.0f || wz < 0.0f || wx >= g_gridW || wz >= g_gridH) return fallback;
    if (!water_plane_on()) return fallback;
    const int cx = (int)wx, cz = (int)wz;
    if (g_waterPlane.shoreBody[(size_t)cz * g_gridW + cx] < 0) return fallback;
    return water_plane_surface_at(cx, cz, wx, wz);
}

/* The visible film lies on the surface over water, the exact plane wherever the bank is
   not below it, even where native base heights were raised by the scene's autoheight.
   Dry bank samples and authored falls keep their ground height, independently of the
   under-bank draw extent. */
static float water_plane_surface_height(float wx, float wz, float fallback)
{
    if (wx < 0.0f || wz < 0.0f || wx >= g_gridW || wz >= g_gridH) return fallback;
    const int cx = (int)wx, cz = (int)wz;
    const int body = water_plane_body(cx, cz);
    if (body < 0) return fallback;
    const WaterPlaneState& P = g_waterPlane;
    const int tx = (int)(wx * P.wetRes), tz = (int)(wz * P.wetRes);
    if (tx < 0 || tz < 0 || tx >= P.wetW || tz >= P.wetH || !P.wet[(size_t)tz * P.wetW + tx]) return fallback;
    return water_plane_surface_at(cx, cz, wx, wz);
}

/* Objects and shroud follow the visible surface on actual wet texels. Native
   autoheight is not a second water elevation, even where it lies above the plane.
   Dry land, opaque bridge pixels and authored falls retain their fallback. */
static float water_plane_support_height(float wx, float wz, float fallback)
{
    return water_plane_surface_height(wx, wz, fallback);
}

#endif
