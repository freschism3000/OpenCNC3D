/* ================================================================================
 * SHATTER -- a killed structure, vehicle or aircraft comes apart into its own pieces,
 * which fly, bounce, roll down the terrain, settle, and sink into the ground as they fade.
 *
 * TWO CUTTING RULES SHARE ONE PHYSICS. shatter_mesh cuts a BUILDING (display-list
 * sections, with a weld that keeps a closed sub-body whole); shatter_mesh_unit cuts a
 * VEHICLE OR AIRCRAFT (sections again, but a TURRET or ROTOR part stays whole and the
 * rest split along their own connected components). Everything after the cut -- launch,
 * gravity, bounce, roll, settle, linger, sink-fade -- is one code path.
 *
 * WHAT IS THE CARTRIDGE'S AND WHAT IS OURS. Stated first, because this is the one
 * feature in the renderer with no cartridge original to copy: the N64 has no authored
 * building collapse at all. It swaps to FRAG3 debris and that is the whole event.
 *
 *   THE CARTRIDGE'S: the pieces themselves (the PK7 section tables, one entry per
 *   G_VTX batch, already in every shipped pack -- these are the display list's own
 *   seams, not a decomposition we invented); the gravity (-2.0 u/tick^2), all decoded from the
 *   chunk visual record at RAM 0x801C95F0; the launch SPEED law (SOURCE_Debris2's
 *   20.0 + rand8/256 * 5.0 u/tick); the tumble law (0.05 * (byte - 127) per euler
 *   axis, the same three-byte extraction efx_step already uses); the 8-tick death
 *   window (BuildingClass::Take_Damage's CountDown = 8); and the pulse's art and
 *   every number in it (SOURCE_Shock, EFX_SRC[29]).
 *
 *   OURS, and every one of them is marked OURS at its site below: the decision to fly
 *   the sections at all -- and for VEHICLES that decision is ours ENTIRELY, because
 *   neither original ever breaks a vehicle into its own geometry: 1995 plays one
 *   AnimClass and calls Delete_This (unit.cpp:966, aircraft.cpp:1641), and the cartridge
 *   fires seven GENERIC debris display lists that are not the victim's mesh. Those chunks
 *   keep firing alongside these pieces rather than being replaced -- the gravel is the
 *   cartridge's, the wreck is ours; the WELD rule that keeps a closed sub-body whole; the launch
 *   ELEVATION (45 degrees, where the cartridge's chunk cone is near-horizontal); the
 *   mass proxy; the ROLL and the settle, which the cartridge's chunks do not have; the
 *   decision to fire SOURCE_Shock, which the cartridge's own 66-recipe table never
 *   instantiates; and the fade timing.
 *
 * THE CLOCK. Everything here is driven by the ENGINE TICK, never by frames and never
 * by a wallclock, and shatter_step is called from the deduplicated tick edge and never
 * from a draw. That is the same lesson the death shed's comment records: under
 * --script a plain `tick` advances the sim without drawing, so a frame-clocked pool
 * would not survive two --shot runs. Sub-tick smoothness is a lerp at DRAW time only.
 *
 * TIER 1. This is TIER 2 ONLY, and the degradation is honest rather than broken: Win98
 * gets the section SHED alone, so the building still visibly disassembles over its
 * eight ticks in the display list's own order and is gone. What it does not get is the
 * pieces continuing to exist after the building does.
 * THE BLOCKER IS NOT THE PER-VERTEX MATRIX, and an earlier version of this note said it
 * was. The extra 3x4 is 12 multiplies and 9 adds on a vertex that already pays a
 * bilinear shroud lookup -- the smallest cost in the feature. The real blocker is
 * COUNT x DURATION x DRAW CALLS: a full pool is 128 pieces, about twelve buildings'
 * worth, and each piece is a separate draw_mesh that opens with curtex = -2 and so
 * re-binds its texture. That is up to 384 draw calls and 128-plus binds per frame, for
 * five to six seconds after the buildings are already gone, on a Pentium II.
 * The PULSE is the exception and ships on both tiers: it is one particle in the existing
 * sprite pool, and it is fired ABOVE the shatter's early-outs so that switching the
 * shatter off does not take it with them. See docs/tier1-gap.md.
 * ================================================================================ */
#ifndef SHATTER_MOD_H
#define SHATTER_MOD_H

#define SHATTER_MAX_PIECES 128      /* about ten buildings' worth */
#define SHATTER_MAX_TRIS   2048     /* hard triangle ceiling, checked at launch */
#define SHATTER_FADE_TICKS 30       /* ticks from full to gone once the linger ends */
/* HOW LONG A PIECE MAY STAY IN CONTACT BEFORE IT IS MADE TO STOP. This is not only an
   anti-hang device, it is a LOOK: a piece on ground that runs downhill for a long way
   never satisfies any rest threshold, because the slope keeps feeding it exactly as fast
   as friction drains it. MEASURED on SCG90EA: two of the Power Plant's nine pieces were
   still rolling after 150 ticks, ten seconds, and would have kept going. Four seconds is
   "it rolled around a bit and settled", which is the event; ten is a piece of scenery
   wandering off. So the cap is deliberate and the gate expects it to fire sometimes. */
#define SHATTER_ROLL_MAX   60

/* ---- the decomposition, computed once per mesh and cached ---------------------- */
struct ShatterPiece {                /* a piece of a MESH; no world state here */
    int   tri0, tri1;                /* [tri0, tri1) into the mesh's triangle list */
    int   sec0, sec1;                /* the section indices it came from, for the log */
    float cx, cy, cz;                /* centroid, MESH units */
    float r;                         /* bounding-sphere radius, MESH units: the ROLL radius */
    float low;                       /* centroid.y - lowest vertex: how high it RESTS */
    float high;                      /* highest vertex - centroid.y: how deep it SINKS */
    float diag;                      /* bbox diagonal, MESH units */
    /* HOW HEAVY THIS PIECE FLIES, 0..1, and the two rules choose it DIFFERENTLY because
       the same proxy is right for one and useless for the other.
       A BUILDING uses the bbox-diagonal share, which spreads its pieces 0.11 to 0.70 and
       makes a floor slab barely leave the ground.
       A UNIT uses the TRIANGLE share, because a vehicle's every piece is a thin panel
       spanning most of the model's box: measured on the Medium Tank, the diagonal proxy
       gives 0.724, 0.818 and 0.845 for a 6-triangle panel, the 41-triangle turret and a
       16-triangle panel -- all heavy, all within a fifth of each other, so every piece
       launches at the same speed and lands 0.12 to 0.17 cells away on a tank that is a
       full cell long. That is a slump, not a shatter. The triangle share gives 0.066,
       0.451 and 0.176 for the same three, ranges of 0.65, 0.33 and 0.55 cells, and it
       correctly makes the turret the heaviest thing on the tank. */
    float mass;
    bool  flat;                      /* a panel, not a lump: slides, does not roll */
};
struct ShatterMesh { std::vector<ShatterPiece> pieces; float diag; bool done; };
static std::map<int, ShatterMesh> g_shatMesh;
/* UNITS GET THEIR OWN CACHE AND THEIR OWN RULE, and the separation is not tidiness.
   The building rule applied to a unit leaves a Mammoth's turret in five pieces; the unit
   rule applied to a building is a REGRESSION -- measured, it takes HAND from 11 pieces to
   14 and in doing so loses the ball, which is the one thing this whole feature was asked
   for by name. Buildings must come out bit-identical or G91 goes red. */
static std::map<int, ShatterMesh> g_shatMeshUnit;

/* ---- a piece in flight --------------------------------------------------------- */
struct ShatterLive {
    int   mi, tri0, tri1, house, face;
    int   id, k, serial;             /* owning object, piece index, death serial */
    int   launchTick;                /* it hangs in place until this tick */
    float px, py, pz, vx, vy, vz;    /* world, cells and cells/tick */
    float ppx, ppy, ppz;             /* previous tick, for the sub-tick lerp */
    float ex, ey, ez, pex, pey, pez; /* euler, radians */
    /* TWO ROTATIONS, AND THEY MUST NOT SHARE STORAGE. The tumble is a per-tick euler
       RATE, the cartridge's 0.05 * (byte - 127) law, which at the shipped
       shatter_spin of 0.295 (fx_state.h:432) is bounded by 0.033 rad/tick, about
       1.9 degrees per axis. It reaches the dial's 0.11 rad/tick only at spin = 1.0,
       which is the cartridge's own chunk rate and not what anybody plays.
       The roll is a UNIT-LENGTH AXIS that the angle accumulated in rolled turns about
       at draw time. One triple used to hold whichever of the two had been written
       last, and the cost was this: a piece that had rolled once and then left the
       ground added a unit vector to its euler every tick, about a radian, some thirty
       times the intended tumble and independent of the spin dial entirely. Downhill
       that is nearly every tick, because py does not move on the tick after a roll
       while the ground under the new px/pz has already dropped, so a rounded piece
       rolling off a slope spun itself into a blur and kept it up until the backstop
       caught it. */
    float tx, ty, tz;                /* the euler tumble RATE, radians per tick */
    float rx, ry, rz;                /* the rolling AXIS, unit length once it rolls */
    float cmx, cmy, cmz;             /* the piece's centroid in MESH units */
    float r, low, high, mass;
    bool  flat, resting, launched;
    int   restTick, bounces, groundTick;
    float rolled;
};
static std::vector<ShatterLive> g_shatLive;
static bool g_shatter      = true;      /* --noshatter is the A/B */
static int  g_shatLastTick = -1;
static int  g_shatDropped  = 0;
/* Heap ids are recycled, and a slot can die twice inside one linger window, which
   would put two piece sets in the pool under the same (id, k). The serial makes the
   diagnostic lines unambiguous and gives the pool a real key. */
static int  g_shatSerial   = 0;

/* Build the piece list for a mesh. Cached: this runs once per model per session. */
static const ShatterMesh& shatter_mesh(int mi)
{
    std::map<int, ShatterMesh>::iterator it = g_shatMesh.find(mi);
    if (it != g_shatMesh.end()) return it->second;
    ShatterMesh sm; sm.done = true; sm.diag = 1.0f;
    const PackMesh& mesh = g_pack.mesh[mi];
    const std::vector<PackTri>& tris = mesh.tris;
    const int ns = (int)mesh.sections.size();
    const int nt = (int)tris.size();
    if (ns < 1 || nt < 1) { g_shatMesh[mi] = sm; return g_shatMesh[mi]; }

    /* section k spans [sections[k], sections[k+1]), exactly build_tri_limit's arithmetic */
    std::vector<int> lo(ns), hi(ns);
    for (int k = 0; k < ns; k++) {
        lo[k] = mesh.sections[k];
        hi[k] = (k + 1 < ns) ? mesh.sections[k + 1] : nt;
        if (lo[k] < 0) lo[k] = 0;
        if (hi[k] > nt) hi[k] = nt;
    }
    /* (b) THE GROUND SHADOW PLATE IS NOT A PIECE. 19 of the 21 building meshes carry
       exactly one all-MODE_SHADOW section. Once the building is gone its shadow should
       simply not be drawn -- a flying shadow decal is the tell of a naive implementation. */
    std::vector<bool> drop(ns, false);
    for (int k = 0; k < ns; k++) {
        bool allShadow = (hi[k] > lo[k]);
        for (int i = lo[k]; i < hi[k]; i++)
            if (tris[i].mode != MODE_SHADOW) { allShadow = false; break; }
        if (allShadow || hi[k] <= lo[k]) drop[k] = true;
    }
    /* (c) union-find the survivors by shared vertex POSITION, rounded to 0.01 mesh units */
    std::vector<int> uf(ns);
    for (int k = 0; k < ns; k++) uf[k] = k;
    {
        std::map<long long, int> seen;
        for (int k = 0; k < ns; k++) {
            if (drop[k]) continue;
            for (int i = lo[k]; i < hi[k]; i++)
                for (int c = 0; c < 3; c++) {
                    const PackVert& v = tris[i].v[c];
                    const long long qx = (long long)floorf(v.x * 100.0f + 0.5f);
                    const long long qy = (long long)floorf(v.y * 100.0f + 0.5f);
                    const long long qz = (long long)floorf(v.z * 100.0f + 0.5f);
                    const long long key = (qx * 73856093LL) ^ (qy * 19349663LL) ^ (qz * 83492791LL);
                    std::map<long long, int>::iterator f = seen.find(key);
                    if (f == seen.end()) { seen[key] = k; continue; }
                    int a = f->second, b = k;
                    while (uf[a] != a) a = uf[a];
                    while (uf[b] != b) b = uf[b];
                    if (a != b) uf[a > b ? a : b] = (a < b ? a : b);
                }
        }
        for (int k = 0; k < ns; k++) { int a = k; while (uf[a] != a) a = uf[a]; uf[k] = a; }
    }
    /* (d) OURS: a welded group stays WHOLE only if it is contiguous in section index,
       watertight (every edge shared by exactly two triangles) and at most 35% of the
       mesh. Without this rule the Hand of Nod's ball is four separate shards; with it
       the ball is one body that falls and rolls, which is the whole point. Without the
       three qualifiers the rule overreaches and flies NUKE as a single lump (welding
       alone collapses it to 124 of its 134 triangles).
       HOW OFTEN IT ACTUALLY FIRES: ONCE, on HAND's ball (dl_0150630 k=10, sec 11..14,
       64 triangles), across every mesh in all 87 shipped scenario packs. An earlier
       version of this comment claimed six -- HAND's ball plus SAM's three tubes and
       FACT's two chimneys -- which was carried over from the design note and never
       checked against the binary. The renderer's own shatterdump says one. SAM and FACT
       have no welded piece at all. The rule is still worth its weight for the single
       case it serves, because that case is the one the owner asked for by name. */
    std::vector<bool> used(ns, false);
    for (int k = 0; k < ns; k++) {
        if (drop[k] || used[k]) continue;
        int g0 = k, g1 = k;
        for (int j = k + 1; j < ns; j++) {
            if (drop[j]) break;
            if (uf[j] != uf[k]) break;
            g1 = j;
        }
        bool weld = (g1 > g0);
        if (weld) {                       /* contiguity is implicit in the scan above */
            int tn = 0;
            for (int j = g0; j <= g1; j++) tn += hi[j] - lo[j];
            if ((float)tn > 0.35f * (float)nt) weld = false;
            if (weld) {                   /* watertight: every edge used exactly twice */
                std::map<long long, int> edge;
                for (int j = g0; j <= g1 && weld; j++)
                    for (int i = lo[j]; i < hi[j]; i++)
                        for (int c = 0; c < 3; c++) {
                            const PackVert& a = tris[i].v[c];
                            const PackVert& b = tris[i].v[(c + 1) % 3];
                            long long ka = ((long long)floorf(a.x*100.f+.5f)*73856093LL)
                                         ^ ((long long)floorf(a.y*100.f+.5f)*19349663LL)
                                         ^ ((long long)floorf(a.z*100.f+.5f)*83492791LL);
                            long long kb = ((long long)floorf(b.x*100.f+.5f)*73856093LL)
                                         ^ ((long long)floorf(b.y*100.f+.5f)*19349663LL)
                                         ^ ((long long)floorf(b.z*100.f+.5f)*83492791LL);
                            edge[(ka < kb ? ka : kb) * 31LL + (ka < kb ? kb : ka)]++;
                        }
                for (std::map<long long, int>::iterator e = edge.begin(); e != edge.end(); ++e)
                    if (e->second != 2) { weld = false; break; }
            }
        }
        const int end = weld ? g1 : g0;
        ShatterPiece pc;
        pc.sec0 = g0; pc.sec1 = end;
        pc.tri0 = lo[g0]; pc.tri1 = hi[end];
        for (int j = g0; j <= end; j++) used[j] = true;
        if (pc.tri1 <= pc.tri0) continue;
        /* (e) centroid, radius, bbox */
        double sx = 0, sy = 0, sz = 0; int n = 0;
        float x0=1e30f,y0=1e30f,z0=1e30f,x1=-1e30f,y1=-1e30f,z1=-1e30f;
        for (int i = pc.tri0; i < pc.tri1; i++)
            for (int c = 0; c < 3; c++) {
                const PackVert& v = tris[i].v[c];
                sx += v.x; sy += v.y; sz += v.z; n++;
                if (v.x<x0)x0=v.x; if (v.x>x1)x1=v.x;
                if (v.y<y0)y0=v.y; if (v.y>y1)y1=v.y;
                if (v.z<z0)z0=v.z; if (v.z>z1)z1=v.z;
            }
        if (!n) continue;
        pc.cx = (float)(sx/n); pc.cy = (float)(sy/n); pc.cz = (float)(sz/n);
        pc.r = 0.0f;
        for (int i = pc.tri0; i < pc.tri1; i++)
            for (int c = 0; c < 3; c++) {
                const PackVert& v = tris[i].v[c];
                const float dx=v.x-pc.cx, dy=v.y-pc.cy, dz=v.z-pc.cz;
                const float d = sqrtf(dx*dx+dy*dy+dz*dz);
                if (d > pc.r) pc.r = d;
            }
        /* HOW HIGH THE PIECE RESTS is its own underside, not its bounding sphere. A
           bounding-sphere contact test floats a wide flat panel a whole cell above the
           ground -- measured on the Power Plant, whose widest section has r = 1.48
           cells -- while for a genuinely round piece like the Hand of Nod's ball the
           two are the same number by definition. So this is right in both cases and
           the sphere radius is kept only for the ROLLING rate. */
        pc.low = pc.cy - y0;
        pc.high = y1 - pc.cy;
        const float ex=x1-x0, ey=y1-y0, ez=z1-z0;
        pc.diag = sqrtf(ex*ex+ey*ey+ez*ez);
        /* OURS: a nearly flat piece is a wall panel, not a marble. Data-driven off the
           bbox, so there is no per-model table to keep in step with the art. */
        pc.flat = (pc.diag > 0.0f && ey < 0.25f * pc.diag);
        pc.mass = 0.0f;                 /* filled below, once sm.diag is known */
        sm.pieces.push_back(pc);
    }
    /* whole-mesh diagonal, for the mass proxy */
    {
        float x0=1e30f,y0=1e30f,z0=1e30f,x1=-1e30f,y1=-1e30f,z1=-1e30f;
        for (int i = 0; i < nt; i++)
            for (int c = 0; c < 3; c++) {
                const PackVert& v = tris[i].v[c];
                if (v.x<x0)x0=v.x; if (v.x>x1)x1=v.x;
                if (v.y<y0)y0=v.y; if (v.y>y1)y1=v.y;
                if (v.z<z0)z0=v.z; if (v.z>z1)z1=v.z;
            }
        const float ex=x1-x0, ey=y1-y0, ez=z1-z0;
        sm.diag = sqrtf(ex*ex+ey*ey+ez*ez);
        if (sm.diag <= 0.0f) sm.diag = 1.0f;
    }
    for (size_t q = 0; q < sm.pieces.size(); q++) {
        float m = sm.pieces[q].diag / sm.diag;      /* the building proxy, unchanged */
        if (m < 0.0f) m = 0.0f;
        if (m > 1.0f) m = 1.0f;
        sm.pieces[q].mass = m;
    }
    g_shatMesh[mi] = sm;
    return g_shatMesh[mi];
}

/* ---- 3x3 helpers, local and tiny ------------------------------------------------ */
static void shat_euler(float ax, float ay, float az, float* m)
{
    const float sx=sinf(ax),cx=cosf(ax),sy=sinf(ay),cy=cosf(ay),sz=sinf(az),cz=cosf(az);
    m[0]= cy*cz;              m[1]= -cy*sz;             m[2]=  sy;
    m[3]= sx*sy*cz + cx*sz;   m[4]= -sx*sy*sz + cx*cz;  m[5]= -sx*cy;
    m[6]=-cx*sy*cz + sx*sz;   m[7]=  cx*sy*sz + sx*cz;  m[8]=  cx*cy;
}
static void shat_axis(float x, float y, float z, float a, float* m)
{
    const float L = sqrtf(x*x+y*y+z*z);
    if (L <= 1e-6f) { m[0]=m[4]=m[8]=1.f; m[1]=m[2]=m[3]=m[5]=m[6]=m[7]=0.f; return; }
    x/=L; y/=L; z/=L;
    const float c=cosf(a), s=sinf(a), t=1.0f-c;
    m[0]=t*x*x+c;   m[1]=t*x*y-s*z; m[2]=t*x*z+s*y;
    m[3]=t*x*y+s*z; m[4]=t*y*y+c;   m[5]=t*y*z-s*x;
    m[6]=t*x*z-s*y; m[7]=t*y*z+s*x; m[8]=t*z*z+c;
}
static void shat_mul(const float* a, const float* b, float* o)
{
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            o[r*3+c] = a[r*3]*b[c] + a[r*3+1]*b[3+c] + a[r*3+2]*b[6+c];
}


/* ================================================================================
 * THE UNIT RULE. Vehicles, aircraft and boats.
 *
 * EVERY PART OF IT IS THE CARTRIDGE'S OWN DATA -- there is no spatial cut of ours
 * anywhere in it. Sections are the PK7 display-list seams; parts and their TURRET /
 * ROTOR roles are the PKD part table, which bake5.py derived from engine truth; and the
 * component split cuts only along the cartridge's own emission order, never across it.
 *
 * WHY IT IS NOT THE BUILDING RULE. Two measured reasons.
 *   The turret. A part whose role is ROLE_TURRET or ROLE_ROTOR is kept WHOLE, because a
 *   turret coming off a tank in one piece is the shot this feature exists for. That is
 *   legal because part boundaries land exactly on section starts on all 20 unit meshes
 *   (verified against the pack: HTNK parts (0,62,STATIC),(62,76,TURRET) against sections
 *   [0,16,31,46,62,78,94,110,124], and likewise MTNK, LTNK, JEEP, BGGY, BOAT, TRAN,
 *   HELI), so a role piece is always a contiguous triangle range and nothing downstream
 *   has to change.
 *   The weld does not run. Verified across every mesh in all 87 shipped scenario packs:
 *   the ONLY welded piece anywhere is the Hand of Nod's ball. On units it never fires,
 *   so carrying it here would be dead code pretending to be a rule.
 *
 * AND THE PREMISE THAT STARTED THIS WAS WRONG, which is worth recording so nobody
 * re-derives it. The claim was "many vehicles are a single display-list section, so they
 * would fly as one lump". They are not. Every one of the 20 unit and aircraft meshes
 * already yields 3 to 9 pieces under the BUILDING rule. The 78 one-piece meshes in the
 * pack are scenery. This rule is about the QUALITY of the cut -- an intact turret, an
 * even piece size, no slivers -- not about rescuing a lump that never existed.
 * ============================================================================== */
static const ShatterMesh& shatter_mesh_unit(int mi)
{
    std::map<int, ShatterMesh>::iterator it = g_shatMeshUnit.find(mi);
    if (it != g_shatMeshUnit.end()) return it->second;
    ShatterMesh sm; sm.done = true; sm.diag = 1.0f;
    const PackMesh& mesh = g_pack.mesh[mi];
    const std::vector<PackTri>& tris = mesh.tris;
    const int ns = (int)mesh.sections.size();
    const int nt = (int)tris.size();
    if (ns < 1 || nt < 1) { g_shatMeshUnit[mi] = sm; return g_shatMeshUnit[mi]; }

    std::vector<int> lo(ns), hi(ns);
    for (int k = 0; k < ns; k++) {
        lo[k] = mesh.sections[k];
        hi[k] = (k + 1 < ns) ? mesh.sections[k + 1] : nt;
        if (lo[k] < 0) lo[k] = 0;
        if (hi[k] > nt) hi[k] = nt;
    }
    /* (1) the ground shadow plate is not a piece. Fires once across the 20 unit meshes,
       on the SAM launcher's 2-triangle section. */
    std::vector<bool> drop(ns, false);
    for (int k = 0; k < ns; k++) {
        bool allShadow = (hi[k] > lo[k]);
        for (int i = lo[k]; i < hi[k]; i++)
            if (tris[i].mode != MODE_SHADOW) { allShadow = false; break; }
        if (allShadow || hi[k] <= lo[k]) drop[k] = true;
    }
    /* (2) a TURRET or ROTOR part is one whole piece. Marked by the section it starts at. */
    std::vector<int> roleEnd(ns, -1);          /* section -> last section of a role run */
    for (size_t pi = 0; pi < mesh.parts.size(); pi++) {
        const PackPart& pt = mesh.parts[pi];
        if (pt.role != ROLE_TURRET && pt.role != ROLE_ROTOR) continue;
        const int p0 = pt.tri0, p1 = pt.tri0 + pt.ntris;
        int s0 = -1, s1 = -1;
        for (int k = 0; k < ns; k++) {
            if (drop[k]) continue;
            if (lo[k] >= p0 && hi[k] <= p1) { if (s0 < 0) s0 = k; s1 = k; }
        }
        if (s0 >= 0) roleEnd[s0] = s1;
    }

    std::vector<std::pair<int,int> > runs;      /* the pieces, as [tri0, tri1) */
    for (int k = 0; k < ns; k++) {
        if (drop[k]) continue;
        if (roleEnd[k] >= k) {                  /* a role part: whole, and skip its run */
            runs.push_back(std::make_pair(lo[k], hi[roleEnd[k]]));
            k = roleEnd[k];
            continue;
        }
        /* (3) split into connected components, but ONLY if every component is already a
           CONTIGUOUS run of the display list. Cutting a non-contiguous component would
           mean reordering the cartridge's own triangles, which this renderer does not do
           anywhere and which draw_mesh's single-range walk could not express. Measured:
           279 of 391 in-section components are contiguous and are cut; the other 112
           sections are simply left whole. */
        std::vector<int> uf(hi[k] - lo[k]);
        for (size_t q = 0; q < uf.size(); q++) uf[q] = (int)q;
        {
            std::map<long long, int> seen;
            for (int i = lo[k]; i < hi[k]; i++)
                for (int c = 0; c < 3; c++) {
                    const PackVert& v = tris[i].v[c];
                    const long long q = ((long long)floorf(v.x*100.f+.5f)*73856093LL)
                                      ^ ((long long)floorf(v.y*100.f+.5f)*19349663LL)
                                      ^ ((long long)floorf(v.z*100.f+.5f)*83492791LL);
                    std::map<long long,int>::iterator f = seen.find(q);
                    if (f == seen.end()) { seen[q] = i - lo[k]; continue; }
                    int a = f->second, b = i - lo[k];
                    while (uf[a] != a) a = uf[a];
                    while (uf[b] != b) b = uf[b];
                    if (a != b) uf[a > b ? a : b] = (a < b ? a : b);
                }
            for (size_t q = 0; q < uf.size(); q++) { int a=(int)q; while (uf[a]!=a) a=uf[a]; uf[q]=a; }
        }
        bool contiguous = true;
        {
            std::map<int, std::pair<int,int> > span;   /* root -> [first,last] */
            for (size_t q = 0; q < uf.size(); q++) {
                std::map<int,std::pair<int,int> >::iterator f = span.find(uf[q]);
                if (f == span.end()) span[uf[q]] = std::make_pair((int)q,(int)q);
                else f->second.second = (int)q;
            }
            int covered = 0;
            for (std::map<int,std::pair<int,int> >::iterator f = span.begin(); f != span.end(); ++f)
                covered += f->second.second - f->second.first + 1;
            if (covered != (int)uf.size()) contiguous = false;
            for (size_t q = 0; q < uf.size() && contiguous; q++) {
                const std::pair<int,int>& sp = span[uf[q]];
                if ((int)q < sp.first || (int)q > sp.second) contiguous = false;
            }
        }
        if (!contiguous) { runs.push_back(std::make_pair(lo[k], hi[k])); continue; }
        /* (4) and merge any run under six triangles into the one before it, which keeps
           contiguity by construction and is what stops a wheel arch becoming confetti. */
        int start = 0;
        std::vector<std::pair<int,int> > sub;
        for (size_t q = 1; q <= uf.size(); q++) {
            if (q < uf.size() && uf[q] == uf[start]) continue;
            const int a = lo[k] + start, b = lo[k] + (int)q;
            if (!sub.empty() && (b - a) < 6) sub.back().second = b;
            else sub.push_back(std::make_pair(a, b));
            start = (int)q;
        }
        for (size_t q = 0; q < sub.size(); q++) runs.push_back(sub[q]);
    }
    /* (5) anything still under three triangles joins the piece it physically touches in
       the triangle list, so a two-triangle sliver never flies on its own. */
    for (size_t q = 0; q < runs.size(); ) {
        if (runs[q].second - runs[q].first >= 3 || runs.size() == 1) { q++; continue; }
        bool merged = false;
        if (q > 0 && runs[q-1].second == runs[q].first) {
            runs[q-1].second = runs[q].second; merged = true;
        } else if (q + 1 < runs.size() && runs[q].second == runs[q+1].first) {
            runs[q+1].first = runs[q].first; merged = true;
        }
        if (!merged) { q++; continue; }
        runs.erase(runs.begin() + q);
    }

    for (size_t q = 0; q < runs.size(); q++) {
        ShatterPiece pc;
        pc.tri0 = runs[q].first; pc.tri1 = runs[q].second;
        pc.sec0 = (int)q; pc.sec1 = (int)q;      /* the launch order is piece order here */
        if (pc.tri1 <= pc.tri0) continue;
        double sx=0, sy=0, sz=0; int n=0;
        float x0=1e30f,y0=1e30f,z0=1e30f,x1=-1e30f,y1=-1e30f,z1=-1e30f;
        for (int i = pc.tri0; i < pc.tri1; i++)
            for (int c = 0; c < 3; c++) {
                const PackVert& v = tris[i].v[c];
                sx+=v.x; sy+=v.y; sz+=v.z; n++;
                if (v.x<x0)x0=v.x; if (v.x>x1)x1=v.x;
                if (v.y<y0)y0=v.y; if (v.y>y1)y1=v.y;
                if (v.z<z0)z0=v.z; if (v.z>z1)z1=v.z;
            }
        if (!n) continue;
        pc.cx=(float)(sx/n); pc.cy=(float)(sy/n); pc.cz=(float)(sz/n);
        pc.r = 0.0f;
        for (int i = pc.tri0; i < pc.tri1; i++)
            for (int c = 0; c < 3; c++) {
                const PackVert& v = tris[i].v[c];
                const float dx=v.x-pc.cx, dy=v.y-pc.cy, dz=v.z-pc.cz;
                const float d = sqrtf(dx*dx+dy*dy+dz*dz);
                if (d > pc.r) pc.r = d;
            }
        pc.low = pc.cy - y0; pc.high = y1 - pc.cy;
        const float ex=x1-x0, ey=y1-y0, ez=z1-z0;
        pc.diag = sqrtf(ex*ex+ey*ey+ez*ez);
        pc.flat = (pc.diag > 0.0f && ey < 0.25f * pc.diag);
        pc.mass = (float)(pc.tri1 - pc.tri0) / (float)nt;   /* the TRIANGLE share */
        if (pc.mass > 1.0f) pc.mass = 1.0f;
        sm.pieces.push_back(pc);
    }
    {
        float x0=1e30f,y0=1e30f,z0=1e30f,x1=-1e30f,y1=-1e30f,z1=-1e30f;
        for (int i = 0; i < nt; i++)
            for (int c = 0; c < 3; c++) {
                const PackVert& v = tris[i].v[c];
                if (v.x<x0)x0=v.x; if (v.x>x1)x1=v.x;
                if (v.y<y0)y0=v.y; if (v.y>y1)y1=v.y;
                if (v.z<z0)z0=v.z; if (v.z>z1)z1=v.z;
            }
        const float ex=x1-x0, ey=y1-y0, ez=z1-z0;
        sm.diag = sqrtf(ex*ex+ey*ey+ez*ez);
        if (sm.diag <= 0.0f) sm.diag = 1.0f;
    }
    g_shatMeshUnit[mi] = sm;
    return g_shatMeshUnit[mi];
}

/* ================================================================================
 * SPAWN. Called from the ONE site that already observes a building's death and
 * already prints DEATHSHED| -- the per-tick parse, never a draw.
 * ============================================================================== */
static void shatter_note_death(int id, int mi, int face, int house,
                               float wx, float wz, int fw, int fh, int frame,
                               const char* type,
                               bool isUnit, float invx, float invz, float lift)
{
    /* THE PULSE FIRES FIRST AND INDEPENDENTLY, and that ordering is a fix rather than a
       preference. It used to sit below the early-outs, which made the header's own claim
       that "the pulse ships on both tiers" untrue: with the shatter switched off -- which
       is every Win98 build and any Tier 2 player who unticks it -- the pulse was
       unreachable, because the return above it had already fired. It is one particle in
       the existing sprite pool and has nothing to do with the piece physics. */
    const int fwv = fw > 0 ? fw : 1, fhv = fh > 0 ? fh : 1;
    const float fcx = wx, fcz = wz;
    if (g_fx.shatter_pulse) {
        EfxSourceRec sh = EFX_SRC[29];
        const float fscale = (float)(fwv > fhv ? fwv : fhv);
        sh.size *= fscale; sh.sizeR *= fscale; sh.grow *= fscale;
        efx_emit(sh, fcx, efx_ground(fcx, fcz) + lift + 0.10f, fcz,
                 1, ((unsigned)id << 8) ^ 0x53484f43u, frame, 0, 0);
    }

    if (!g_shatter || !g_fx.shatter_on) return;   /* the F5 switch, and --noshatter */
    if (mi < 0 || mi >= (int)g_pack.mesh.size()) return;
    const ShatterMesh& sm = isUnit ? shatter_mesh_unit(mi) : shatter_mesh(mi);
    if (sm.pieces.empty()) return;
    const PackMesh& mesh = g_pack.mesh[mi];
    const int ns = (int)mesh.sections.size();
    if (ns < 1) return;

    /* The pulse itself is fired above, before the early-outs. OURS to fire; the
       cartridge's in every other respect. SOURCE_Shock is EFX_SRC[29], particle system 4,
       and a grep of all 66 entries of EFX_RECIPE shows nothing instantiates it: the
       console carries this emitter and its art and never fires it once. The pieces fly
       RADIALLY from its centre, so the blast and the debris are visibly one event, and
       the FOOTPRINT SCALING is ours too -- a 3x3 Construction Yard earns a ring three
       times the width of a 1x1 Guard Tower. */
    float rs, rc; facing_rot(face, &rs, &rc);
    const float ground = terrain_y(fcx, fcz);
    int made = 0, tris = 0;
    const int serial = ++g_shatSerial;
    /* DESCENDING, and that is a priority order rather than a style choice. Section index
       ascends with k, the shed drops the HIGHEST section first, so the highest k is the
       piece that launches FIRST and is on screen longest -- the Hand of Nod's ball is
       k=10 of 11. Walking ascending meant that when the pool filled, the survivors were
       the low sections (the floor slab, which barely moves) and the roof was dropped.
       This way a starved pool loses the least interesting pieces. */
    int liveTris = 0;
    for (size_t q = 0; q < g_shatLive.size(); q++)
        liveTris += g_shatLive[q].tri1 - g_shatLive[q].tri0;
    for (int k = (int)sm.pieces.size() - 1; k >= 0; k--) {
        const ShatterPiece& pc = sm.pieces[k];
        if ((int)g_shatLive.size() >= SHATTER_MAX_PIECES) { g_shatDropped++; continue; }
        /* AND THE TRIANGLE CEILING IS ACTUALLY CHECKED. It was declared with the comment
           "hard triangle ceiling, checked at launch" and then checked nowhere, so the real
           worst case ran over half again past the declared budget. */
        if (liveTris + (pc.tri1 - pc.tri0) > SHATTER_MAX_TRIS) { g_shatDropped++; continue; }
        liveTris += pc.tri1 - pc.tri0;
        ShatterLive L;
        L.mi = mi; L.tri0 = pc.tri0; L.tri1 = pc.tri1; L.house = house; L.face = face;
        L.id = id; L.k = k; L.serial = serial;
        L.cmx = pc.cx; L.cmy = pc.cy; L.cmz = pc.cz;
        L.r = pc.r * MODEL_SCALE;
        L.low = pc.low * MODEL_SCALE;
        L.high = pc.high * MODEL_SCALE;
        L.flat = pc.flat;
        L.mass = pc.mass;      /* chosen by the rule that cut the piece; see ShatterPiece */

        /* THE PIECE STARTS EXACTLY WHERE IT STOOD. Its centroid goes through the same
           transform draw_mesh uses for the standing building -- facing rotation, then
           MODEL_SCALE, then the terrain height under the anchor -- so on the tick it
           launches it is in the whole building's own position and nothing jumps. */
        L.px = fcx + (pc.cx * rc + pc.cz * rs) * MODEL_SCALE;
        L.pz = fcz + (-pc.cx * rs + pc.cz * rc) * MODEL_SCALE;
        /* THE LIFT IS WHY AN ORCA'S WRECK DOES NOT SPAWN IN THE DIRT. Flight level is
           alt = 24, which alt_lift turns into 0.9375 cells -- nearly three times an
           Orca's own model height, so spawning on the ground would be wrong by three
           body heights. It is 0 for every ground vehicle and every landed aircraft, so
           it is one term rather than a branch. */
        L.py = ground + lift + pc.cy * MODEL_SCALE;
        L.ppx = L.px; L.ppy = L.py; L.ppz = L.pz;

        /* Launch order IS the shed order, so the two are one event and cannot disagree:
           section k departs at death phase 1 - k/ns, highest section first. On the Hand
           of Nod that makes the BALL -- sections 11..14, welded, the last 64 triangles of
           the display list -- the very first thing to leave the building. */
        /* A UNIT HAS NO DEATH WINDOW, so every piece leaves on the same tick. That is
           not a simplification, it is the measurement: a building sits in the heap for
           eight ticks with str <= 0 (CountDown = 8), and a vehicle goes from str=12/150
           in one dump straight to MISSING in the next -- str <= 0 is never observed at
           one-tick resolution. There is no window to stagger across. */
        if (isUnit) {
            L.launchTick = frame;
        } else {
            float d = 1.0f - (float)pc.sec1 / (float)ns;
            if (d < 0.0f) d = 0.0f; if (d > 1.0f) d = 1.0f;
            L.launchTick = frame + (int)(d * (float)(BUILDING_DEATH_TICKS - 1) + 0.5f);
        }

        /* THE IMPULSE, radial from the pulse centre. Never rand(), never a wallclock:
           the seed is the brain's own heap id and the tick strength hit zero, so the
           same input log gives the same debris field to the byte. */
        const unsigned key = ((unsigned)id << 8) ^ 0x53484154u;   /* 'SHAT' */
        const unsigned h1 = efx_hash(key, (unsigned)frame, (unsigned)k, 0u);
        const unsigned h2 = efx_hash(h1, 0x9E3779B9u, (unsigned)k, key);
        float dx = L.px - fcx, dz = L.pz - fcz;
        float dl = sqrtf(dx*dx + dz*dz);
        float az;
        if (dl < 1e-4f) az = efx_r8(h1) * 6.2831853f;      /* dead centre: pick one */
        else            az = atan2f(dz, dx);
        az += (efx_r8(h1 >> 8) - 0.5f) * 0.70f;            /* OURS: spread */
        const float el = 0.611f + (efx_r8(h1 >> 16) - 0.5f) * 0.52f;  /* OURS: 45 +- 15 deg */
        /* the CARTRIDGE'S speed law, SOURCE_Debris2's 20.0 + rand8/256 * 5.0 u/tick.
           Sanity, because a made-up speed is a bug with a nicer interface: at a = 2.0
           u/tick^2 and 45 degrees the range is s^2/a, so the mean 22.5 gives 253 units
           = 0.99 cells over 15.9 ticks = about a second at 15 Hz. Heavy pieces keep
           less of it, so FACT's floor slab barely leaves the ground. */
        const float sp = (20.0f + efx_r8(h1 >> 24) * 5.0f)
                       * (1.0f - 0.7f * L.mass) * g_fx.shatter_force * EFX_U;
        L.vx = cosf(az) * sp * cosf(el);
        L.vz = sinf(az) * sp * cosf(el);
        L.vy = sp * sinf(el);
        /* AND THE WRECK KEEPS GOING. Inheriting the victim's own motion is the difference
           between a tank that explodes and a tank that WAS MOVING and explodes, and for
           aircraft it is not a garnish: an Orca cruises at 0.15625 cells/tick, which is
           1.88x the strongest launch this feature can produce and 3.2x the mean, so
           without this an Orca's debris would stop dead in mid-air while the explosion
           carried on. No vertical term: the smoothing window carries x and z only, so a
           descending helicopter's sink rate is simply not recoverable. */
        L.vx += invx; L.vz += invz;
        /* the CARTRIDGE'S tumble law and its three-byte extraction */
        L.ex = L.ey = L.ez = 0.0f;
        L.pex = L.pey = L.pez = 0.0f;
        L.tx = 0.05f * ((float)((h2 >> 8) & 0xFF) - 127.0f) * g_fx.shatter_spin * 0.01745f;
        L.ty = 0.05f * ((float)((h2 >> 4) & 0xFF) - 127.0f) * g_fx.shatter_spin * 0.01745f;
        L.tz = 0.05f * ((float)( h2       & 0xFF) - 127.0f) * g_fx.shatter_spin * 0.01745f;
        L.rx = L.ry = L.rz = 0.0f;      /* no roll axis until the roll arm sets one */
        L.resting = false; L.launched = false; L.restTick = 0; L.bounces = 0; L.groundTick = 0; L.rolled = 0.0f;
        g_shatLive.push_back(L);
        made++; tris += pc.tri1 - pc.tri0;
        printf("SHATTER|piece|id=%d|k=%d|sec=%d..%d|tris=%d|r=%.3f|mass=%.3f|v=%.4f,%.4f,%.4f\n",
               id, k, pc.sec0, pc.sec1, pc.tri1 - pc.tri0, L.r, L.mass, L.vx, L.vy, L.vz);
    }
    printf("SHATTER|%s|id=%d|pieces=%d|tris=%d|frame=%d\n",
           type, id, made, tris, frame);
    fflush(stdout);
}

/* ================================================================================
 * STEP. From the deduplicated tick edge beside efx_step, never from a draw.
 * ============================================================================== */
static void shatter_step(int frame)
{
    if (frame == g_shatLastTick) return;
    if (frame < g_shatLastTick) {                /* new mission, or a restarted clock */
        g_shatLive.clear(); g_shatDropped = 0;
    }
    g_shatLastTick = frame;
    const int linger = (int)(g_fx.shatter_linger * 15.0f + 0.5f);

    for (size_t i = 0; i < g_shatLive.size(); ) {
        ShatterLive& L = g_shatLive[i];
        if (frame < L.launchTick) { i++; continue; }   /* still standing in the building */
        L.launched = true;
        L.ppx = L.px; L.ppy = L.py; L.ppz = L.pz;
        L.pex = L.ex; L.pey = L.ey; L.pez = L.ez;

        if (L.resting) {
            if (frame - L.restTick > linger + SHATTER_FADE_TICKS) {
                g_shatLive.erase(g_shatLive.begin() + i);
                continue;
            }
            i++; continue;
        }
        /* integrate, same shape as efx_integrate_once */
        L.px += L.vx; L.py += L.vy; L.pz += L.vz;
        L.vy += EFX_CHUNK_YACC * EFX_U;

        /* CONTACT at the piece's own resting height, not its bare centre. The chunk
           pool tests its centre and gets away with it at 0.25 cells across; a building
           piece is up to 0.5 and would sink half-through the ground. The +r is OURS. */
        const float gy = efx_ground(L.px, L.pz) + L.low;
        if (L.py <= gy) {
            L.py = gy;
            /* THE CONTACT CLOCK STARTS AT THE FIRST TOUCH OF ANY KIND. A piece bouncing
               DOWN A SLOPE is not a decaying system: the ground recedes under it every
               tick, so each fall is taller than the last and the hillside feeds the
               bounce faster than elasticity drains it. Measured before this was written:
               a Power Plant panel was still bouncing 400 ticks later with the ground
               0.04 cells lower each cycle, so nothing rested, nothing faded, and the
               pool never emptied. */
            if (!L.groundTick) L.groundTick = frame;

            /* FRICTION ON EVERY CONTACT, and it is COULOMB rather than a per-tick
               multiplier. A multiplier only ASYMPTOTES to zero, so against any downhill
               pull it finds a terminal velocity and creeps for ever; subtracting a fixed
               amount actually stops. It is also the honest physics -- a piece rests on
               any slope shallower than its friction angle and slides down anything
               steeper. Applying it here rather than in the roll arm alone is what makes
               a bouncing piece lose its horizontal speed too. */
            {
                /* The friction ANGLE this implies is the number that matters: a piece
                   rests on any slope where mu exceeds the downhill pull, which here is
                   2.0 * EFX_U * gradient = 0.0078 * gradient. At 0.0040 that balanced at
                   a gradient of 0.51, about 27 degrees, and MEASURED on SCG90EA that let
                   two of the Power Plant's nine pieces creep downhill until the
                   four-second backstop caught them. Rubble does not do that. At 0.0055 the
                   balance moves to 0.70, about 35 degrees, which is where loose rubble
                   actually sits, and the pieces still roll a visible distance first. */
                const float mu = (L.flat ? 0.0075f : 0.0055f)
                               * (1.20f - 0.40f * g_fx.shatter_roll);
                const float sp = sqrtf(L.vx*L.vx + L.vz*L.vz);
                if (sp > 1e-6f) {
                    const float ns = sp - mu;
                    const float kk = (ns > 0.0f ? ns : 0.0f) / sp;
                    L.vx *= kk; L.vz *= kk;
                }
            }

            /* BOUNCE OR ROLL, and the test is on the REBOUND rather than the impact,
               measured against gravity. One tick of gravity is 0.0078 cells/tick, so a
               rebound under about three of those cannot leave the ground in any visible
               way and is a piece settling, not a piece bouncing. Testing the impact
               instead -- which is what this did first -- let a piece on a slope bounce
               its whole four-second window away and reach the roll arm literally never:
               every piece in the first working build rested on the timeout with
               rolled=0.000, so the rolling this feature exists for never once ran. */
            /* ELASTICITY IS OURS, AND IT IS NOT THE CHUNK POOL'S 0.7. That number was
               decoded for the cartridge's DEBRIS GRAVEL -- small, light, bouncy. A
               building piece is a slab of concrete or a steel panel weighing tons: it
               thuds, it does not bounce, and the heavier it is the less it bounces at
               all. Keeping 0.7 here was measured to be visibly wrong AND functionally
               wrong: on sloping ground every Power Plant piece bounced for its whole
               four-second window without once reaching the roll arm, because the
               hillside dropping away under it refilled the bounce each cycle. */
            const float el  = 0.35f * (1.0f - 0.5f * L.mass);
            const float reb = -L.vy * el;
            if (reb > 0.025f) {
                if (!L.bounces)
                    printf("SHATTER|land|id=%d|k=%d|frame=%d|at=%.3f,%.3f,%.3f|ground=%.3f\n",
                           L.id, L.k, frame, L.px, L.py, L.pz, gy - L.low);
                L.vy = reb;
                L.vx *= el; L.vz *= el;
                L.tx *= el; L.ty *= el; L.tz *= el;
                L.bounces++;
            } else {
                /* ROLL. OURS -- the cartridge's chunks do not have it, and it is the part
                   that makes a ball read as a ball. The terrain accelerates the piece
                   downhill using the gradient the heightfield gives for free. */
                L.vy = 0.0f;
                const float e = 0.25f;
                const float gx = (efx_ground(L.px+e, L.pz) - efx_ground(L.px-e, L.pz)) / (2*e);
                const float gz = (efx_ground(L.px, L.pz+e) - efx_ground(L.px, L.pz-e)) / (2*e);
                L.vx -= 2.0f * EFX_U * gx; L.vz -= 2.0f * EFX_U * gz;
                /* A FLAT PIECE LIES DOWN. OURS. A panel is only stable face-up or
                   face-down, so as it settles its pitch and roll ease to the nearest
                   multiple of pi -- either face -- while its yaw stays wherever the
                   tumble left it. Without this a wall panel comes to rest balanced on
                   its edge like a headstone, which was the single most obviously wrong
                   thing in the first working build: the Power Plant's biggest panel
                   stood upright in the crater for the whole linger. */
                if (L.flat) {
                    const float px2 = floorf(L.ex / 3.14159265f + 0.5f) * 3.14159265f;
                    const float pz2 = floorf(L.ez / 3.14159265f + 0.5f) * 3.14159265f;
                    L.ex += (px2 - L.ex) * 0.22f;
                    L.ez += (pz2 - L.ez) * 0.22f;
                }
                const float sped = sqrtf(L.vx*L.vx + L.vz*L.vz);
                if (!L.flat && L.r > 1e-4f && sped > 1e-5f) {
                    /* ROLLING, not sliding: one rotation about the horizontal axis
                       perpendicular to travel, advancing by |v|/r radians per tick. That
                       one line is the difference between the Hand of Nod's ball rolling
                       down a slope and skidding down it, and it is why the piece's
                       bounding-sphere radius is worth precomputing. */
                    L.rx = -L.vz / sped; L.ry = 0.0f; L.rz = L.vx / sped;
                    L.rolled += sped / L.r;
                }
                /* ON THE GROUND THE FREE TUMBLE ENDS, rolled or not: what turns the
                   piece from here is the roll, or nothing. That is the behaviour this
                   arm always had, because both of its branches used to overwrite the
                   tumble; it is now said once and on purpose. The ROLL AXIS is
                   deliberately NOT cleared here. Clearing it is what the shared triple
                   did, and since shat_axis falls back to identity on a zero axis while
                   rolled is still positive, a piece that had rolled snapped back to its
                   launch orientation on the very tick it settled -- friction subtracts a
                   fixed amount and so takes the horizontal speed to exactly zero, which
                   lands in this branch and rested on the same tick. */
                L.tx = L.ty = L.tz = 0.0f;
            }

            /* REST: the chunk pool's own 0.002 cells/tick threshold, or the backstop.
               THE BACKSTOP SITS ON THE CONTACT AND NOT INSIDE THE ROLL ARM, because the
               runaway case never reaches the roll arm -- a backstop parked there is
               unreachable exactly when it is needed. */
            if (L.vx*L.vx + L.vy*L.vy + L.vz*L.vz < 4.0e-6f || frame - L.groundTick > SHATTER_ROLL_MAX) {
                const int to = (frame - L.groundTick > 60) ? 1 : 0;
                L.resting = true; L.restTick = frame;
                L.vx = L.vy = L.vz = 0.0f;
                /* THE EULER GOES IN BEFORE to=, NEVER AFTER IT. The backstop leg of the
                   shatter gate anchors to=1 to the END of this line, so a field appended
                   past it stops matching in silence. It is printed at all because the
                   rotation is otherwise unobservable from outside the renderer, and
                   every existing leg of that gate reads positions: a piece whose
                   rotation has run away rests with euler terms in the tens of radians,
                   where flight alone can only put a few there. */
                printf("SHATTER|rest|id=%d|k=%d|frame=%d|at=%.3f,%.3f,%.3f|ground=%.3f|low=%.3f|bounces=%d|rolled=%.3f|euler=%.2f,%.2f,%.2f|to=%d\n",
                       L.id, L.k, frame, L.px, L.py, L.pz,
                       efx_ground(L.px, L.pz), L.low, L.bounces, L.rolled,
                       L.ex, L.ey, L.ez, to);
            }
        }
        if (!L.resting && L.vy != 0.0f) { L.ex += L.tx; L.ey += L.ty; L.ez += L.tz; }
        i++;
    }
    fflush(stdout);
}

/* Does the shatter pool own this object's geometry? While it does, draw_object_mesh must
   not also draw the building: the pieces ARE the building. The empty-pool early-out keeps
   this free in every frame where nothing has died, which is nearly all of them. */
static bool shatter_owns(int id)
{
    if (g_shatLive.empty() || id < 0) return false;
    for (size_t i = 0; i < g_shatLive.size(); i++)
        if (g_shatLive[i].id == id) return true;
    return false;
}

/* Mission boundary and shutdown. The mesh cache is keyed by INDEX into g_pack, so it must
   not outlive the pack it was built from. */
static void shatter_reset(void)
{
    g_shatMesh.clear();
    g_shatMeshUnit.clear();
    g_shatLive.clear();
    g_shatLastTick = -1;
    g_shatDropped  = 0;
    g_shatSerial   = 0;
}

/* ================================================================================
 * DRAW. One draw_mesh per piece, through the SAME path everything else uses.
 * ============================================================================== */
static void shatter_draw(int pass, bool fadingPass)
{
    if (g_shatLive.empty()) return;
    const int linger = (int)(g_fx.shatter_linger * 15.0f + 0.5f);
    const float a = g_tickAlpha;                 /* 0 under --shot and --script */
    for (size_t i = 0; i < g_shatLive.size(); i++) {
        const ShatterLive& L = g_shatLive[i];
        /* A PIECE THAT HAS NOT LAUNCHED IS STILL DRAWN, and that is the whole fix for the
           worst bug this feature had. The original design let the death SHED keep drawing
           the standing shell while the pool drew only the launched pieces, on the claim
           that "the shed keeps the LOW sections and the shatter launches the HIGH ones, so
           they never overlap". That claim was FALSE. The launch tick ROUNDS
           (1 - sec/ns) * 7 while build_tri_limit CEILS it on one path and FLOORS it on the
           other, and ceil >= round always, so a section was drawn TWICE -- a ghost welded
           in place while its own copy flew away -- on 3 of the 6 death ticks of a Power
           Plant. With smooth animations on, the player's default, the floor branch could
           also shed PAST a piece boundary and leave a HOLE of triangles drawn by nobody.
           Both were measured off instrumented draw ranges.
           Now the pool owns every triangle of a dying building from the tick it dies, and
           draw_object_mesh suppresses the building itself (see shatter_owns). An unlaunched
           piece sits at its spawn pose, which is exactly where it stood, so the handover is
           invisible -- and the two systems cannot disagree, because there is only one. */
        int alpha = 255;
        float sink = 0.0f;
        if (L.resting) {
            const int age = g_shatLastTick - L.restTick;
            if (age > linger) {
                const float t = (float)(age - linger) / (float)SHATTER_FADE_TICKS;
                const float tc = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
                alpha = (int)(255.0f * (1.0f - tc));
                /* IT SINKS, IT DOES NOT SHRINK. OURS, and a deliberate departure from the
                   cartridge, whose own disappearance law for this class of object is the
                   chunk pool's EFX_CHUNK_SHRINK = -0.005 scale/tick. A shrinking slab
                   reads as a slab being deleted; a settling one reads as rubble the
                   ground is taking back, and the terrain's own depth buffer does the
                   work -- the piece is progressively occluded by the hill it is lying
                   on rather than dissolving in mid-air. the project owner asked for this by name on
                   24 Aug 2026 after seeing the shrink.
                   The distance is the piece's own full height, so it is exactly buried
                   at the end whatever its size, plus a hair so nothing pokes through. */
                sink = tc * (L.low + L.high) * 1.05f;
                if (alpha < 1) continue;
            }
        }
        const bool fading = (alpha < 255);
        if (fading != fadingPass) continue;      /* opaque pieces never touch the blended pass */

        const float px = L.ppx + (L.px - L.ppx) * a;
        const float py = L.ppy + (L.py - L.ppy) * a - sink;
        const float pz = L.ppz + (L.pz - L.ppz) * a;
        const float ex = L.pex + (L.ex - L.pex) * a;
        const float ey = L.pey + (L.ey - L.pey) * a;
        const float ez = L.pez + (L.ez - L.pez) * a;

        float R[9], Rb[9], Rr[9];
        shat_euler(ex, ey, ez, Rb);
        if (L.rolled > 0.0f) { shat_axis(L.rx, L.ry, L.rz, L.rolled, Rr); shat_mul(Rr, Rb, R); }
        else                 { for (int j = 0; j < 9; j++) R[j] = Rb[j]; }

        /* The piece matrix, in MESH units: bring the centroid to the model origin, spin
           it, shrink it. draw_mesh then anchors it at (px,pz) and lifts it to py, so the
           centroid lands exactly on the physics position with no new arithmetic. */
        float M[12];
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) M[r*4+c] = R[r*3+c];
            M[r*4+3] = -(R[r*3+0]*L.cmx + R[r*3+1]*L.cmy + R[r*3+2]*L.cmz);
        }
        g_meshAlphaScale = (unsigned char)alpha;
        draw_mesh(L.mi, px, pz, L.face, pass, 0, 0.0f, L.house, 1.0f,
                  py - terrain_y(px, pz), false, WOBBLE_NONE, -1.0f, M,
                  (size_t)L.tri0, (size_t)L.tri1);
        g_meshAlphaScale = 255;
    }
}
/* shatterdump -- the decomposition of every mesh the pack holds, plus the live pool.
   This is how the piece counts in gate_shatter.txt are checked against the PACK rather
   than against a screenshot: a bad weld rule, a shadow section leaking into the piece
   list, or a pack that lost its section tables all show up as a changed number here. */
static void shatter_dump(void)
{
    for (size_t mi = 0; mi < g_pack.mesh.size(); mi++) {
        const PackMesh& m = g_pack.mesh[mi];
        const ShatterMesh& sm = shatter_mesh((int)mi);
        if (sm.pieces.empty()) continue;
        int tn = 0;
        for (size_t k = 0; k < sm.pieces.size(); k++) tn += sm.pieces[k].tri1 - sm.pieces[k].tri0;
        printf("SHATTERMESH|%s|mi=%d|pieces=%d|tris=%d|sections=%d\n",
               m.name, (int)mi, (int)sm.pieces.size(), tn, (int)m.sections.size());
        for (size_t k = 0; k < sm.pieces.size(); k++) {
            const ShatterPiece& pc = sm.pieces[k];
            int nsh=0,nop=0,ncu=0,nxl=0;
            for (int t=pc.tri0;t<pc.tri1;t++){int md=m.tris[t].mode;
                if(md==MODE_SHADOW)nsh++;else if(md==MODE_OPAQUE)nop++;
                else if(md==MODE_CUTOUT)ncu++;else nxl++;}
            printf("SHATTERMESH|%s|k=%d|sec=%d..%d|tris=%d|op=%d|cut=%d|xlu=%d|shadow=%d|r=%.1f|low=%.1f|diag=%.1f|flat=%d\n",
                   m.name, (int)k, pc.sec0, pc.sec1, pc.tri1 - pc.tri0,
                   nop,ncu,nxl,nsh, pc.r, pc.low, pc.diag, pc.flat ? 1 : 0);
        }
    }
    int lt = 0;
    for (size_t i = 0; i < g_shatLive.size(); i++)
        lt += g_shatLive[i].tri1 - g_shatLive[i].tri0;
    printf("SHATTERDUMP|live=%d|tris=%d|dropped=%d|pool=%d\n",
           (int)g_shatLive.size(), lt, g_shatDropped, SHATTER_MAX_PIECES);
    fflush(stdout);
}
#endif
