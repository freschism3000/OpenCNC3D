/* infdeath_mod.h -- what Enhanced's 3-D soldiers (einf/, model3d_mod.h) leave behind and
   throw out: bodies that stay where they fell, darkened, at most a set number of them;
   a ragdoll that takes over from the death clip part way through the fall and that
   vehicles and soldiers shove; blood pooled under each body; and on each shot of a weapon
   that has them, a flash at the model's own muzzle and spent casings thrown from its
   ejection port, bouncing on the ground and fading.

   Included by cnc_eyes.cpp after the corpse list (M3Corpse, g_m3Corpses) and before the
   soldier's draw. Models are found by m3inf_model_any, which answers on a tick with no
   frame drawn (m3inf_find answers only while one is). Everything here steps once an engine tick from refresh_objects and reads
   no clock, so two runs of the same script are the same picture. Drawing is the host's:
   infdeath_draw_blood with the ground decals, infdeath_draw_casings with the soldiers.

   THE RAGDOLL is joints as points with fixed distances between them (Verlet integration,
   the distances satisfied by repeated projection), the way a character's skeleton is
   usually done when a full rigid-body solver is more than it needs: 22 points, the bones
   between them and a few braces that keep the hips, shoulders and head one rigid torso.
   It starts from two samples of the death clip one tick apart, so it carries on with the
   clip's own velocity and does not jump. Each bone of the model is then posed from the
   points: a limb by the turn that carries its direction at the hand-over to its direction
   now (keeping the clip's twist), the hips, chest and head by the turn of a two-axis
   frame, so their breadth turns with them.

   UNITS: the points are in WORLD cells (x east, y up, z south). A model point p maps to
   the world as  anchor + Ry(d.yaw) * MODEL_SCALE * scale * p, which is m3_matrix with no
   wobble; infdeath_to_model is its inverse. */
#ifndef OPENCNC3D_INFDEATH_MOD_H
#define OPENCNC3D_INFDEATH_MOD_H

#include <map>
#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>

static bool visible(const SimObject& o);

/* ---- the points -------------------------------------------------------------------- */
enum {
    IR_PELVIS, IR_CHEST, IR_HEAD, IR_HEADTIP,
    IR_HIP_L, IR_KNEE_L, IR_ANKLE_L, IR_TOE_L,
    IR_HIP_R, IR_KNEE_R, IR_ANKLE_R, IR_TOE_R,
    IR_SH_L, IR_EL_L, IR_WR_L, IR_FIN_L,
    IR_SH_R, IR_EL_R, IR_WR_R, IR_FIN_R,
    /* THE FAR SIDE OF EACH JOINT A BODY CAN COME APART AT. Each sits on its partner,
       held there by a link of length zero, until the joint is cut (see IR_JOINT_P):
       the chest's joint as the hips carry it, the neck as the head carries it with a
       point beside the head to turn it by, and the hips and shoulders as the limbs
       carry them. */
    IR_WAIST, IR_NECKX, IR_HEADSIDE,
    IR_HIPX_L, IR_HIPX_R, IR_SHX_L, IR_SHX_R,
    IR_GRIP, IR_TIP,
    IR_N
};
/* each point's radius against the ground, as a share of his standing height */
static const float IR_RADIUS[IR_N] = {
    0.085f, 0.090f, 0.075f, 0.030f,
    0.070f, 0.055f, 0.050f, 0.030f,
    0.070f, 0.055f, 0.050f, 0.030f,
    0.060f, 0.045f, 0.040f, 0.025f,
    0.060f, 0.045f, 0.040f, 0.025f,
    0.080f, 0.065f, 0.050f,
    0.065f, 0.065f, 0.055f, 0.055f,
    0.025f, 0.020f };

/* THE JOINTS A BODY COMES APART AT, and the two pieces of him each one joins: the hips
   (with the legs) are the root, the chest hangs from them at the waist, the head and the
   arms from the chest, the legs from the hips. */
enum { IJ_WAIST, IJ_NECK, IJ_HIP_L, IJ_HIP_R, IJ_SH_L, IJ_SH_R, IJ_N };
enum { IG_LOWER, IG_UPPER, IG_HEAD, IG_ARM_L, IG_ARM_R, IG_LEG_L, IG_LEG_R, IG_N };
static const unsigned char IR_JOINT_G[IJ_N][2] = {
    { IG_LOWER, IG_UPPER }, { IG_UPPER, IG_HEAD }, { IG_LOWER, IG_LEG_L }, { IG_LOWER, IG_LEG_R },
    { IG_UPPER, IG_ARM_L }, { IG_UPPER, IG_ARM_R } };
/* the joint's point on the parent's side and on the child's */
static const unsigned char IR_JOINT_P[IJ_N][2] = {
    { IR_WAIST, IR_CHEST }, { IR_HEAD, IR_NECKX }, { IR_HIP_L, IR_HIPX_L }, { IR_HIP_R, IR_HIPX_R },
    { IR_SH_L, IR_SHX_L }, { IR_SH_R, IR_SHX_R } };

/* the bones (pairs held at their length) and the braces that make each piece one rigid
   thing; the third number is the joint whose cut removes the link (0 none, else 1 + IJ_) */
#define IRJ(j) (1 + (j))
static const unsigned char IR_LINKS[][3] = {
    /* the hips: one rigid piece with the waist's point */
    { IR_PELVIS, IR_WAIST, 0 }, { IR_PELVIS, IR_HIP_L, 0 }, { IR_PELVIS, IR_HIP_R, 0 }, { IR_HIP_L, IR_HIP_R, 0 },
    { IR_HIP_L, IR_WAIST, 0 }, { IR_HIP_R, IR_WAIST, 0 },
    /* the chest and shoulders */
    { IR_CHEST, IR_HEAD, 0 }, { IR_CHEST, IR_SH_L, 0 }, { IR_CHEST, IR_SH_R, 0 }, { IR_SH_L, IR_SH_R, 0 },
    { IR_HEAD, IR_SH_L, 0 }, { IR_HEAD, IR_SH_R, 0 },
    /* the head */
    { IR_NECKX, IR_HEADTIP, 0 }, { IR_NECKX, IR_HEADSIDE, 0 }, { IR_HEADTIP, IR_HEADSIDE, 0 },
    /* the legs and arms */
    { IR_HIPX_L, IR_KNEE_L, 0 }, { IR_KNEE_L, IR_ANKLE_L, 0 }, { IR_ANKLE_L, IR_TOE_L, 0 }, { IR_KNEE_L, IR_TOE_L, 0 },
    { IR_HIPX_R, IR_KNEE_R, 0 }, { IR_KNEE_R, IR_ANKLE_R, 0 }, { IR_ANKLE_R, IR_TOE_R, 0 }, { IR_KNEE_R, IR_TOE_R, 0 },
    { IR_SHX_L, IR_EL_L, 0 }, { IR_EL_L, IR_WR_L, 0 }, { IR_WR_L, IR_FIN_L, 0 }, { IR_EL_L, IR_FIN_L, 0 },
    { IR_SHX_R, IR_EL_R, 0 }, { IR_EL_R, IR_WR_R, 0 }, { IR_WR_R, IR_FIN_R, 0 }, { IR_EL_R, IR_FIN_R, 0 },
    /* THE JOINTS: each a weld of its two sides, and the braces across it */
    { IR_WAIST, IR_CHEST, IRJ(IJ_WAIST) },
    { IR_PELVIS, IR_SH_L, IRJ(IJ_WAIST) }, { IR_PELVIS, IR_SH_R, IRJ(IJ_WAIST) },
    { IR_HIP_L, IR_CHEST, IRJ(IJ_WAIST) }, { IR_HIP_R, IR_CHEST, IRJ(IJ_WAIST) },
    { IR_HIP_L, IR_SH_L, IRJ(IJ_WAIST) }, { IR_HIP_R, IR_SH_R, IRJ(IJ_WAIST) }, { IR_PELVIS, IR_HEAD, IRJ(IJ_WAIST) },
    { IR_HEAD, IR_NECKX, IRJ(IJ_NECK) }, { IR_HEADTIP, IR_CHEST, IRJ(IJ_NECK) },
    { IR_HEADSIDE, IR_CHEST, IRJ(IJ_NECK) }, { IR_HEADSIDE, IR_SH_L, IRJ(IJ_NECK) }, { IR_HEADSIDE, IR_SH_R, IRJ(IJ_NECK) },
    { IR_HIP_L, IR_HIPX_L, IRJ(IJ_HIP_L) }, { IR_HIP_R, IR_HIPX_R, IRJ(IJ_HIP_R) },
    { IR_SH_L, IR_SHX_L, IRJ(IJ_SH_L) }, { IR_SH_R, IR_SHX_R, IRJ(IJ_SH_R) },
    /* the weapon, when it falls on its own */
    { IR_GRIP, IR_TIP, 0 } };
static const int IR_NLINK = (int)(sizeof IR_LINKS / sizeof IR_LINKS[0]);
/* knees and elbows may bend but not fold flat: the hip-to-ankle and shoulder-to-wrist
   distances held above a share of the limb's length */
static const unsigned char IR_REACH[][2] = {
    { IR_HIPX_L, IR_ANKLE_L }, { IR_HIPX_R, IR_ANKLE_R }, { IR_SHX_L, IR_WR_L }, { IR_SHX_R, IR_WR_R } };
static const float IR_REACH_MIN[4] = { 0.55f, 0.55f, 0.35f, 0.35f };
static const unsigned char IR_REACH_MID[4] = { IR_KNEE_L, IR_KNEE_R, IR_EL_L, IR_EL_R };

/* ---- what a model gives us, found by bone name once ------------------------------- */
struct InfRig {
    bool ok;
    int pelvis, chest, head, root, thigh[2], shin[2], foot[2], uarm[2], farm[2], hand[2];
    int rifle, muzzle, eject, ejectdir;
    float heightU;                  /* his standing height in model units */
    float headTip[3], headSide[3], toe[2][3], fin[2][3];   /* rest places of the points no joint marks */
    int shootN, proneN;             /* the engine's firing stages, standing and prone */
    unsigned shootAt, proneAt;      /* which of them a shot leaves on (bit per stage) */
    /* which piece of him each bone belongs to (IG_), -1 for the weapon and its marks */
    signed char grp[M3_MAX_BONES];
    /* THE CAPS: for each joint, the torn end on the parent's side [0] and on the child's
       [1], as draw ranges over triangles appended to the model (ir_build_caps). Drawn only
       for a body cut at that joint. */
    bool capOk;
    M3Sub cap[IJ_N][2];
    /* THE TORN DRAW (ir_build_caps): every vertex's home piece, and the model's triangles
       again with each one's vertices on its own side of every joint, a vertex shared
       across a joint being copied for the far side, so one skinning can fold each side
       of a cut onto its own end of it */
    std::vector<signed char> vhome;
    std::vector<M3Sub> tornSub;
};
static std::map<int, InfRig> g_irRig;

static int ir_bone(const M3Model& m, const char* name)
{
    for (size_t i = 0; i < m.bone.size(); i++)
        if (!strcmp(m.bone[i].name, name)) return (int)i;
    return -1;
}

static int ir_bone_prefix(const M3Model& m, const char* prefix)
{
    const size_t n = strlen(prefix);
    for (size_t i = 0; i < m.bone.size(); i++)
        if (!strncmp(m.bone[i].name, prefix, n)) return (int)i;
    return -1;
}

/* a bone's rest joint, model space: its bone space is the joint, so the inverse bind is
   a translation by minus it */
static void ir_joint(const M3Model& m, int b, float* j)
{
    const float* I = m.bone[b].ib;
    j[0] = -I[3]; j[1] = -I[7]; j[2] = -I[11];
}

static unsigned ir_stage_mask(const char* s)
{
    unsigned m = 0;
    while (*s && *s != '/') {
        if (*s >= '0' && *s <= '9') {
            int v = 0;
            while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
            if (v < 32) m |= 1u << v;
        } else s++;
    }
    return m;
}

static void ir_build_caps(int mi);
static const InfRig& ir_rig(int mi)
{
    std::map<int, InfRig>::iterator it = g_irRig.find(mi);
    if (it != g_irRig.end()) return it->second;
    InfRig r;
    memset(&r, 0, sizeof r);
    const M3Model& m = g_m3[mi];
    const char* side[2] = { "L", "R" };
    char nm[40];
    r.pelvis = ir_bone(m, "pelvis"); r.chest = ir_bone(m, "chest"); r.head = ir_bone(m, "head");
    r.root = ir_bone(m, "root"); r.rifle = ir_bone(m, "rifle");
    for (int s = 0; s < 2; s++) {
        snprintf(nm, sizeof nm, "thigh.%s", side[s]);    r.thigh[s] = ir_bone(m, nm);
        snprintf(nm, sizeof nm, "shin.%s", side[s]);     r.shin[s] = ir_bone(m, nm);
        snprintf(nm, sizeof nm, "foot.%s", side[s]);     r.foot[s] = ir_bone(m, nm);
        snprintf(nm, sizeof nm, "upperarm.%s", side[s]); r.uarm[s] = ir_bone(m, nm);
        snprintf(nm, sizeof nm, "forearm.%s", side[s]);  r.farm[s] = ir_bone(m, nm);
        snprintf(nm, sizeof nm, "hand.%s", side[s]);     r.hand[s] = ir_bone(m, nm);
    }
    r.muzzle = ir_bone_prefix(m, "muzzle/");
    r.eject = ir_bone(m, "eject");
    r.ejectdir = ir_bone(m, "ejectdir");
    r.ok = r.pelvis >= 0 && r.chest >= 0 && r.head >= 0;
    for (int s = 0; s < 2 && r.ok; s++)
        r.ok = r.thigh[s] >= 0 && r.shin[s] >= 0 && r.foot[s] >= 0 && r.uarm[s] >= 0
            && r.farm[s] >= 0 && r.hand[s] >= 0;
    if (r.ok) {
        float h[3], a[3], w[3], e[3];
        ir_joint(m, r.head, h);
        r.heightU = h[1] / 0.815f;                 /* the neck sits at 0.815 of his height */
        r.headTip[0] = h[0]; r.headTip[1] = h[1] + 0.14f * r.heightU; r.headTip[2] = h[2];
        r.headSide[0] = h[0] + 0.08f * r.heightU; r.headSide[1] = h[1] + 0.07f * r.heightU; r.headSide[2] = h[2];
        for (int s = 0; s < 2; s++) {
            ir_joint(m, r.foot[s], a);
            /* ahead is -z in the model (the pack's axes) */
            r.toe[s][0] = a[0]; r.toe[s][1] = a[1] - 0.05f * r.heightU; r.toe[s][2] = a[2] - 0.10f * r.heightU;
            ir_joint(m, r.hand[s], w);
            ir_joint(m, r.farm[s], e);
            float d[3] = { w[0] - e[0], w[1] - e[1], w[2] - e[2] };
            const float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            for (int k = 0; k < 3; k++) r.fin[s][k] = w[k] + (l > 1e-6f ? d[k] / l : 0.0f) * 0.08f * r.heightU;
        }
    }
    r.shootN = r.proneN = 0;
    if (r.muzzle >= 0) {
        /* "muzzle/8/2.4.6/6/2.4" */
        const char* p = m.bone[r.muzzle].name + 7;
        r.shootN = atoi(p);
        p = strchr(p, '/'); if (p) { p++; r.shootAt = ir_stage_mask(p); p = strchr(p, '/'); }
        if (p) { p++; r.proneN = atoi(p); p = strchr(p, '/'); }
        if (p) { p++; r.proneAt = ir_stage_mask(p); }
    }
    for (size_t b = 0; b < m.bone.size() && b < (size_t)M3_MAX_BONES; b++) {
        const char* n = m.bone[b].name;
        const int s = strstr(n, ".R") ? 1 : 0;
        if (!strncmp(n, "upperarm.", 9) || !strncmp(n, "forearm.", 8) || !strncmp(n, "hand.", 5))
            r.grp[b] = (signed char)(IG_ARM_L + s);
        else if (!strncmp(n, "thigh.", 6) || !strncmp(n, "shin.", 5) || !strncmp(n, "foot.", 5))
            r.grp[b] = (signed char)(IG_LEG_L + s);
        else if (!strcmp(n, "head")) r.grp[b] = IG_HEAD;
        else if (!strcmp(n, "pelvis") || !strcmp(n, "root")) r.grp[b] = IG_LOWER;
        else if (!strcmp(n, "rifle") || !strcmp(n, "eject") || !strcmp(n, "ejectdir")
                 || !strncmp(n, "muzzle/", 7)) r.grp[b] = -1;
        else r.grp[b] = IG_UPPER;
    }
    r.capOk = false;
    g_irRig[mi] = r;
    if (r.ok) ir_build_caps(mi);
    return g_irRig[mi];
}

/* ---- the torn ends ---------------------------------------------------------------------
   A BODY CUT AT A JOINT IS DRAWN AS SEVERAL DRAWS OF THE ONE MODEL, a piece each: every
   bone outside the piece is folded to nothing at the joint the piece was cut from (its
   scale 0, its place that joint), so the skin that hung from it closes into the joint and
   the piece shows no hole and no second copy of anything. What closes it is drawn here:
   a torn end for each side of each joint, a shallow dome of meat with bone at its heart
   and a short wall of it back into the stump, sized to the cross-section the skin
   actually has there in the bind pose. The caps are appended to the model's own vertex
   and index arrays, weighted whole to the bone on their side, and listed in no draw list:
   a piece's draw adds the caps of its own cuts for that draw only, so a whole soldier
   draws exactly what he did before and his coarser levels of detail are untouched. */
static unsigned ir_cap_hash(unsigned a, unsigned b)
{
    unsigned h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}

/* the meat: bone at the centre with marrow in it, dark wet red with lighter fibres
   running out from it, and a darker rim where the skin was torn */
static int ir_cap_textures(M3Model& m)
{
    const int T = 64;
    M3Tex base, nrm, orm;
    base.w = nrm.w = orm.w = T; base.h = nrm.h = orm.h = T;
    base.gl = nrm.gl = orm.gl = 0;
    base.px.resize((size_t)T * T * 4); nrm.px.resize((size_t)T * T * 4); orm.px.resize((size_t)T * T * 4);
    for (int y = 0; y < T; y++)
        for (int x = 0; x < T; x++) {
            const float u = (x + 0.5f) / T - 0.5f, v = (y + 0.5f) / T - 0.5f;
            const float r = sqrtf(u * u + v * v) * 2.0f;              /* 0 centre, 1 rim */
            const float ang = atan2f(v, u);
            const float fib = 0.5f + 0.5f * sinf(ang * 23.0f + r * 9.0f
                              + 3.0f * (float)(ir_cap_hash((unsigned)(ang * 40.0f + 200.0f), 7u) & 255) / 255.0f);
            const float n = (float)(ir_cap_hash((unsigned)(x * 977 + y), 0xCA9u) & 255) / 255.0f;
            float c[3], rough;
            if (r < 0.07f) {                       /* marrow */
                c[0] = 0.38f; c[1] = 0.10f; c[2] = 0.07f; rough = 0.45f;
            } else if (r < 0.19f) {                /* bone */
                const float k = 0.92f + 0.08f * n;
                c[0] = 0.86f * k; c[1] = 0.80f * k; c[2] = 0.68f * k; rough = 0.55f;
            } else if (r < 0.86f) {                /* meat */
                const float k = 0.70f + 0.30f * fib * (0.75f + 0.25f * n);
                c[0] = 0.56f * k; c[1] = 0.07f * k; c[2] = 0.055f * k; rough = 0.28f;
            } else {                               /* the torn rim */
                c[0] = 0.32f; c[1] = 0.05f; c[2] = 0.04f; rough = 0.35f;
            }
            unsigned char* b = &base.px[((size_t)y * T + x) * 4];
            b[0] = (unsigned char)(c[0] * 255.0f); b[1] = (unsigned char)(c[1] * 255.0f);
            b[2] = (unsigned char)(c[2] * 255.0f); b[3] = 0;             /* no house colour */
            unsigned char* q = &nrm.px[((size_t)y * T + x) * 4];
            q[0] = (unsigned char)(128 + (int)((fib - 0.5f) * 40.0f)); q[1] = 128; q[2] = 255; q[3] = 0;
            unsigned char* o = &orm.px[((size_t)y * T + x) * 4];
            o[0] = 255; o[1] = (unsigned char)(rough * 255.0f); o[2] = 0; o[3] = 0;
        }
    const int t0 = (int)m.tex.size();
    m.tex.push_back(base); m.tex.push_back(nrm); m.tex.push_back(orm);
    M3Mat mt;
    mt.base = t0; mt.norm = t0 + 1; mt.orm = t0 + 2;
    mt.flags = M3_MAT_NO_PLAYER;
    m.mat.push_back(mt);
    return (int)m.mat.size() - 1;
}

static void ir_build_caps(int mi)
{
    InfRig& r = g_irRig[mi];
    M3Model& m = g_m3[mi];
    const size_t nv0 = m.vert.size() / 8;
    if (r.capOk || m.skin.size() != nv0 * 8 || m.sub.empty()) return;
    const float H = r.heightU;
    /* each vertex's home: the piece its heaviest bone belongs to (the weapon: -1) */
    r.vhome.assign(nv0, (signed char)IG_UPPER);
    for (size_t v = 0; v < nv0; v++) {
        const unsigned char* sk = &m.skin[v * 8];
        int best = 0;
        for (int k = 1; k < 4; k++) if (sk[4 + k] > sk[4 + best]) best = k;
        r.vhome[v] = sk[best] < M3_MAX_BONES ? r.grp[sk[best]] : (signed char)IG_UPPER;
    }
    /* the triangles again, each on the side most of its corners are on, its corners
       from the far side copied over */
    {
        std::map<std::pair<unsigned, int>, unsigned> dup;
        const size_t nsub = m.sub.size();
        for (size_t si = 0; si < nsub; si++) {
            const M3Sub sb0 = m.sub[si];
            const size_t i0 = m.idx.size();
            for (unsigned t = 0; t + 2 < sb0.nidx; t += 3) {
                unsigned iv[3];
                int g[3];
                for (int k = 0; k < 3; k++) { iv[k] = m.idx[sb0.idx0 + t + k]; g[k] = r.vhome[iv[k]]; }
                const int T = (g[1] == g[2]) ? g[1] : g[0];
                for (int k = 0; k < 3; k++) {
                    if (g[k] == T) { m.idx.push_back(iv[k]); continue; }
                    std::map<std::pair<unsigned, int>, unsigned>::iterator f = dup.find(std::make_pair(iv[k], T));
                    if (f == dup.end()) {
                        const unsigned nvi = (unsigned)(m.vert.size() / 8);
                        for (int q = 0; q < 8; q++) m.vert.push_back(m.vert[(size_t)iv[k] * 8 + q]);
                        for (int q = 0; q < 8; q++) m.skin.push_back(m.skin[(size_t)iv[k] * 8 + q]);
                        r.vhome.push_back((signed char)T);
                        f = dup.insert(std::make_pair(std::make_pair(iv[k], T), nvi)).first;
                    }
                    m.idx.push_back(f->second);
                }
            }
            M3Sub ns = sb0;
            ns.idx0 = (unsigned)i0; ns.nidx = (unsigned)(m.idx.size() - i0);
            r.tornSub.push_back(ns);
        }
    }
    const int mat = ir_cap_textures(m);
    for (int j = 0; j < IJ_N; j++) {
        int parent, child, toward;
        float J[3], A[3], T[3];
        const int s = (j == IJ_HIP_R || j == IJ_SH_R) ? 1 : 0;
        switch (j) {
        case IJ_WAIST: parent = r.pelvis; child = r.chest; toward = r.head; break;
        case IJ_NECK:  parent = r.chest; child = r.head; toward = -1; break;
        case IJ_HIP_L: case IJ_HIP_R: parent = r.pelvis; child = r.thigh[s]; toward = r.shin[s]; break;
        default:       parent = r.chest; child = r.uarm[s]; toward = r.farm[s]; break;
        }
        ir_joint(m, child, J);
        if (toward >= 0) ir_joint(m, toward, T);
        else { T[0] = r.headTip[0]; T[1] = r.headTip[1]; T[2] = r.headTip[2]; }
        for (int k = 0; k < 3; k++) A[k] = T[k] - J[k];
        {
            const float l = sqrtf(A[0] * A[0] + A[1] * A[1] + A[2] * A[2]);
            for (int k = 0; k < 3; k++) A[k] = l > 1e-9f ? A[k] / l : (k == 1 ? 1.0f : 0.0f);
        }
        /* the plane across the joint */
        float e1[3], e2[3];
        {
            const float up[3] = { 0.0f, 0.0f, 1.0f }, sd[3] = { 1.0f, 0.0f, 0.0f };
            const float* ref = fabsf(A[2]) < 0.9f ? up : sd;
            e1[0] = A[1] * ref[2] - A[2] * ref[1]; e1[1] = A[2] * ref[0] - A[0] * ref[2]; e1[2] = A[0] * ref[1] - A[1] * ref[0];
            const float l = sqrtf(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]);
            for (int k = 0; k < 3; k++) e1[k] /= l;
            e2[0] = A[1] * e1[2] - A[2] * e1[1]; e2[1] = A[2] * e1[0] - A[0] * e1[2]; e2[2] = A[0] * e1[1] - A[1] * e1[0];
        }
        /* THE CROSS-SECTION THE SKIN HAS THERE: the child's own vertices in a thin slab
           at the joint, their middle and their reach along the plane's two axes */
        std::vector<float> xs, ys;
        float cx = 0.0f, cy = 0.0f;
        const float lim = (j == IJ_WAIST) ? 0.22f * H : 0.15f * H;
        for (size_t v = 0; v < nv0; v++) {
            const unsigned char* sk = &m.skin[v * 8];
            float w = 0.0f;
            for (int k = 0; k < 4; k++) if (sk[k] == child) w += sk[4 + k] / 255.0f;
            if (w < 0.3f) continue;
            const float* p = &m.vert[v * 8];
            const float d[3] = { p[0] - J[0], p[1] - J[1], p[2] - J[2] };
            const float ax = d[0] * A[0] + d[1] * A[1] + d[2] * A[2];
            if (ax < -0.02f * H || ax > 0.06f * H) continue;
            const float px = d[0] * e1[0] + d[1] * e1[1] + d[2] * e1[2];
            const float py = d[0] * e2[0] + d[1] * e2[1] + d[2] * e2[2];
            if (px * px + py * py > lim * lim) continue;
            xs.push_back(px); ys.push_back(py); cx += px; cy += py;
        }
        float ex = 0.045f * H, ey = 0.045f * H;
        if (xs.size() >= 6) {
            cx /= (float)xs.size(); cy /= (float)ys.size();
            for (size_t k = 0; k < xs.size(); k++) { xs[k] = fabsf(xs[k] - cx); ys[k] = fabsf(ys[k] - cy); }
            std::sort(xs.begin(), xs.end()); std::sort(ys.begin(), ys.end());
            ex = xs[xs.size() * 8 / 10] * 0.95f; ey = ys[ys.size() * 8 / 10] * 0.95f;
            if (ex < 0.02f * H) ex = 0.02f * H;
            if (ey < 0.02f * H) ey = 0.02f * H;
        } else {
            cx = cy = 0.0f;
        }
        const float hgt = 0.20f * (ex < ey ? ex : ey);
        for (int side = 0; side < 2; side++) {
            const float sg = side == 0 ? 1.0f : -1.0f;               /* which way it faces */
            const int bone = side == 0 ? parent : child;
            const size_t v0 = m.vert.size() / 8;
            r.vhome.resize(v0 + 1 + 3 * 12, (signed char)IR_JOINT_G[j][side]);
            const int SEG = 12;
            /* centre, then rings: the dome's inner ring, its rim, and the wall's foot */
            const float RF[3] = { 0.55f, 1.0f, 1.0f };
            const float RZ[3] = { 0.75f, 0.0f, -1.6f };
            const float RU[3] = { 0.22f, 0.40f, 0.47f };
            #define IR_CAP_VERT(f, z, ux, uy, nx_, ny_, nz_) do { \
                float P_[3]; for (int k = 0; k < 3; k++) P_[k] = J[k] + e1[k] * (cx + (f) * ex * (ux)) \
                    + e2[k] * (cy + (f) * ey * (uy)) + A[k] * sg * (z) * hgt; \
                m.vert.push_back(P_[0]); m.vert.push_back(P_[1]); m.vert.push_back(P_[2]); \
                m.vert.push_back(nx_); m.vert.push_back(ny_); m.vert.push_back(nz_); \
                const unsigned char sk_[8] = { (unsigned char)bone, 0, 0, 0, 255, 0, 0, 0 }; \
                m.skin.insert(m.skin.end(), sk_, sk_ + 8); } while (0)
            IR_CAP_VERT(0.0f, 1.0f, 0.0f, 0.0f, A[0] * sg, A[1] * sg, A[2] * sg);
            m.vert.push_back(0.5f); m.vert.push_back(0.5f);
            for (int ring = 0; ring < 3; ring++)
                for (int k = 0; k < SEG; k++) {
                    const float ang = 6.2831853f * (float)k / SEG;
                    const float ux = cosf(ang), uy = sinf(ang);
                    float n[3];
                    const float out = ring == 0 ? 0.45f : (ring == 1 ? 0.8f : 1.0f);
                    const float up = ring == 0 ? 0.9f : (ring == 1 ? 0.55f : 0.0f);
                    for (int q = 0; q < 3; q++) n[q] = (e1[q] * ux + e2[q] * uy) * out + A[q] * sg * up;
                    const float nl = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                    IR_CAP_VERT(RF[ring], RZ[ring], ux, uy, n[0] / nl, n[1] / nl, n[2] / nl);
                    m.vert.push_back(0.5f + RU[ring] * ux); m.vert.push_back(0.5f + RU[ring] * uy);
                }
            #undef IR_CAP_VERT
            const size_t i0 = m.idx.size();
            #define IR_CAP_TRI(a_, b_, c_) do { \
                const unsigned ia_ = (unsigned)(v0 + (a_)), ib_ = (unsigned)(v0 + (b_)), ic_ = (unsigned)(v0 + (c_)); \
                const float* pa = &m.vert[ia_ * 8]; const float* pb = &m.vert[ib_ * 8]; const float* pc = &m.vert[ic_ * 8]; \
                const float u_[3] = { pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2] }; \
                const float w_[3] = { pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2] }; \
                const float fn[3] = { u_[1] * w_[2] - u_[2] * w_[1], u_[2] * w_[0] - u_[0] * w_[2], u_[0] * w_[1] - u_[1] * w_[0] }; \
                const float nn = (pa[3] + pb[3] + pc[3]) * fn[0] + (pa[4] + pb[4] + pc[4]) * fn[1] + (pa[5] + pb[5] + pc[5]) * fn[2]; \
                m.idx.push_back(ia_); \
                if (nn >= 0.0f) { m.idx.push_back(ib_); m.idx.push_back(ic_); } \
                else { m.idx.push_back(ic_); m.idx.push_back(ib_); } } while (0)
            for (int k = 0; k < SEG; k++) {
                const int k1 = (k + 1) % SEG;
                IR_CAP_TRI(0, 1 + k, 1 + k1);
                for (int ring = 0; ring < 2; ring++) {
                    const int a0 = 1 + ring * SEG + k, a1 = 1 + ring * SEG + k1;
                    const int b0 = a0 + SEG, b1 = a1 + SEG;
                    IR_CAP_TRI(a0, b0, b1);
                    IR_CAP_TRI(a0, b1, a1);
                }
            }
            #undef IR_CAP_TRI
            M3Sub sb;
            sb.part = m.sub[0].part; sb.mat = mat;
            sb.idx0 = (unsigned)i0; sb.nidx = (unsigned)(m.idx.size() - i0);
            r.cap[j][side] = sb;
        }
    }
    m3_pose_model_changed(m);
    r.capOk = true;
}

/* ---- small vector and rotation helpers ------------------------------------------- */
static void ir_qrot(const float* q, const float* v, float* o)
{
    /* v' = v + 2w(q x v) + 2 q x (q x v) */
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float tx = 2.0f * (y * v[2] - z * v[1]);
    const float ty = 2.0f * (z * v[0] - x * v[2]);
    const float tz = 2.0f * (x * v[1] - y * v[0]);
    o[0] = v[0] + w * tx + (y * tz - z * ty);
    o[1] = v[1] + w * ty + (z * tx - x * tz);
    o[2] = v[2] + w * tz + (x * ty - y * tx);
}

static void ir_qmul(const float* a, const float* b, float* o)
{
    const float x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    const float y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    const float z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    const float w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    o[0] = x; o[1] = y; o[2] = z; o[3] = w;
}

static void ir_norm3(float* v)
{
    const float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-12f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

/* the shortest turn carrying unit a onto unit b */
static void ir_qarc(const float* a, const float* b, float* q)
{
    const float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    if (d < -0.9999f) {
        float ax[3] = { 0.0f, -a[2], a[1] };
        if (ax[1] * ax[1] + ax[2] * ax[2] < 1e-8f) { ax[0] = a[2]; ax[1] = 0.0f; ax[2] = -a[0]; }
        ir_norm3(ax);
        q[0] = ax[0]; q[1] = ax[1]; q[2] = ax[2]; q[3] = 0.0f;
        return;
    }
    q[0] = a[1] * b[2] - a[2] * b[1];
    q[1] = a[2] * b[0] - a[0] * b[2];
    q[2] = a[0] * b[1] - a[1] * b[0];
    q[3] = 1.0f + d;
    const float l = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int k = 0; k < 4; k++) q[k] /= l;
}

/* a rotation (quaternion) from a 3x3 matrix in columns c0 c1 c2 */
static void ir_qmat(const float* c0, const float* c1, const float* c2, float* q)
{
    const float m00 = c0[0], m10 = c0[1], m20 = c0[2];
    const float m01 = c1[0], m11 = c1[1], m21 = c1[2];
    const float m02 = c2[0], m12 = c2[1], m22 = c2[2];
    const float tr = m00 + m11 + m22;
    if (tr > 0.0f) {
        const float s = sqrtf(tr + 1.0f) * 2.0f;
        q[3] = 0.25f * s; q[0] = (m21 - m12) / s; q[1] = (m02 - m20) / s; q[2] = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
        q[3] = (m21 - m12) / s; q[0] = 0.25f * s; q[1] = (m01 + m10) / s; q[2] = (m02 + m20) / s;
    } else if (m11 > m22) {
        const float s = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
        q[3] = (m02 - m20) / s; q[0] = (m01 + m10) / s; q[1] = 0.25f * s; q[2] = (m12 + m21) / s;
    } else {
        const float s = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
        q[3] = (m10 - m01) / s; q[0] = (m02 + m20) / s; q[1] = (m12 + m21) / s; q[2] = 0.25f * s;
    }
}

/* the turn carrying the frame (up a, side b) onto (up c, side d) */
static void ir_qframe(const float* a, const float* b, const float* c, const float* d, float* q)
{
    float u0[3] = { a[0], a[1], a[2] }, u1[3] = { c[0], c[1], c[2] };
    ir_norm3(u0); ir_norm3(u1);
    float s0[3], s1[3], f0[3], f1[3];
    float k = b[0] * u0[0] + b[1] * u0[1] + b[2] * u0[2];
    for (int i = 0; i < 3; i++) s0[i] = b[i] - u0[i] * k;
    k = d[0] * u1[0] + d[1] * u1[1] + d[2] * u1[2];
    for (int i = 0; i < 3; i++) s1[i] = d[i] - u1[i] * k;
    ir_norm3(s0); ir_norm3(s1);
    f0[0] = u0[1] * s0[2] - u0[2] * s0[1]; f0[1] = u0[2] * s0[0] - u0[0] * s0[2]; f0[2] = u0[0] * s0[1] - u0[1] * s0[0];
    f1[0] = u1[1] * s1[2] - u1[2] * s1[1]; f1[1] = u1[2] * s1[0] - u1[0] * s1[2]; f1[2] = u1[0] * s1[1] - u1[1] * s1[0];
    /* R = B1 * B0^T, B = [u s f] as columns */
    float c0[3], c1[3], c2[3];
    for (int r = 0; r < 3; r++) {
        c0[r] = u1[r] * u0[0] + s1[r] * s0[0] + f1[r] * f0[0];
        c1[r] = u1[r] * u0[1] + s1[r] * s0[1] + f1[r] * f0[1];
        c2[r] = u1[r] * u0[2] + s1[r] * s0[2] + f1[r] * f0[2];
    }
    ir_qmat(c0, c1, c2, q);
}

/* ---- a body -------------------------------------------------------------------------- */
enum { IRS_ANIM, IRS_RAG, IRS_ASLEEP };
struct InfRag {
    int id, since, mi, variant;
    int state;                       /* IRS_*: the clip still plays, the ragdoll has it, at rest */
    int handoff;                     /* the engine frame the ragdoll took over */
    int still;                       /* ticks at rest in a row */
    float ax, ay, az, yawDeg, S;     /* the anchor m3inf_draw_as places him by */
    float p[IR_N][3], pp[IR_N][3];   /* points now and a substep ago, world cells */
    float len[IR_NLINK];             /* the links' lengths, from the hand-over */
    float reach[4];                  /* the limbs' full lengths */
    bool  freeGun, handGun;          /* the weapon falls on its own / stays in his right hand */
    /* at the hand-over, per bone: its pose (8 floats) and the points that turn it */
    std::vector<float> pose0;
    float hoP[IR_N][3];              /* the points at the hand-over, MODEL space */
    /* blood */
    int bloodBorn;                   /* engine frame it began to spread, -1 none yet */
    float bx, bz, bsize, brot;
    int bvar;
    /* coming apart */
    int cause;                       /* IRC_: what killed him, which decides how he comes apart */
    unsigned cut;                    /* the joints cut, a bit per IJ_ */
    int cutAt[IJ_N];                 /* the engine frame each was cut, for its spray */
    long long overBy;                /* the tracked vehicle over him last tick, 0 none */
    int splats;                      /* drops of his spray that have landed */
    float crushX, crushZ, crushSX, crushSZ;   /* the vehicle that ran him over, and its step */
};
enum { IRC_NONE, IRC_BLAST, IRC_CRUSH };
/* which piece of him each point belongs to (IG_), -1 the weapon */
static const signed char IR_PGRP[IR_N] = {
    IG_LOWER, IG_UPPER, IG_UPPER, IG_HEAD,
    IG_LOWER, IG_LEG_L, IG_LEG_L, IG_LEG_L,
    IG_LOWER, IG_LEG_R, IG_LEG_R, IG_LEG_R,
    IG_UPPER, IG_ARM_L, IG_ARM_L, IG_ARM_L,
    IG_UPPER, IG_ARM_R, IG_ARM_R, IG_ARM_R,
    IG_LOWER, IG_HEAD, IG_HEAD,
    IG_LEG_L, IG_LEG_R, IG_ARM_L, IG_ARM_R,
    -1, -1 };

/* THE PIECES A BODY IS IN: the groups joined by the joints not cut, numbered in the order
   of their first group, so the piece holding the hips is always piece 0. */
static int ir_components(unsigned cut, int comp[IG_N])
{
    int par[IG_N];
    for (int g = 0; g < IG_N; g++) par[g] = g;
    for (int j = 0; j < IJ_N; j++) {
        if (cut & (1u << j)) continue;
        int a = IR_JOINT_G[j][0], b = IR_JOINT_G[j][1];
        while (par[a] != a) a = par[a];
        while (par[b] != b) b = par[b];
        if (a != b) par[a > b ? a : b] = a < b ? a : b;
    }
    int n = 0, id[IG_N];
    for (int g = 0; g < IG_N; g++) id[g] = -1;
    for (int g = 0; g < IG_N; g++) {
        int r = g;
        while (par[r] != r) r = par[r];
        if (id[r] < 0) id[r] = n++;
        comp[g] = id[r];
    }
    return n;
}
static std::map<long long, InfRag> g_irBody;
static int g_irLastFrame = -1;
static int g_irDrawnBlood = 0, g_irRagLive = 0, g_irPushes = 0;

static long long ir_key(int id, int since) { return ((long long)id << 32) ^ (unsigned)since; }

static unsigned ir_hash(unsigned a, unsigned b)
{
    unsigned h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}
static float ir_r01(unsigned h) { return (float)(h & 0xFFFFu) / 65535.0f; }

static void ir_to_world(const InfRag& b, const float* m, float* w)
{
    const float ry = -b.yawDeg * (float)M_PI / 180.0f;
    const float cy = cosf(ry), sy = sinf(ry);
    w[0] = b.ax + b.S * (cy * m[0] + sy * m[2]);
    w[1] = b.ay + b.S * m[1];
    w[2] = b.az + b.S * (-sy * m[0] + cy * m[2]);
}

static void ir_to_model(const InfRag& b, const float* w, float* m)
{
    const float ry = -b.yawDeg * (float)M_PI / 180.0f;
    const float cy = cosf(ry), sy = sinf(ry);
    const float dx = w[0] - b.ax, dy = w[1] - b.ay, dz = w[2] - b.az;
    m[0] = (cy * dx - sy * dz) / b.S;
    m[1] = dy / b.S;
    m[2] = (sy * dx + cy * dz) / b.S;
}

/* a point a bone carries: its rest place, model space, through the bone's pose */
static void ir_carry(const M3Model& m, const float* pose, int bone, const float* rest, float* out)
{
    float j[3], v[3], r[3];
    ir_joint(m, bone, j);
    const float* p = pose + bone * 8;
    const float s = p[7];
    for (int k = 0; k < 3; k++) v[k] = (rest[k] - j[k]) * s;
    float q[4] = { p[0], p[1], p[2], p[3] };
    const float l = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int k = 0; k < 4; k++) q[k] = l > 1e-12f ? q[k] / l : (k == 3 ? 1.0f : 0.0f);
    ir_qrot(q, v, r);
    for (int k = 0; k < 3; k++) out[k] = r[k] + p[4 + k];
}

/* the points of a pose, MODEL space */
static void ir_points(const M3Model& m, const InfRig& r, const float* pose, float P[IR_N][3])
{
    float j[3];
    #define IR_JOINT(ix, bone) do { ir_joint(m, bone, j); ir_carry(m, pose, bone, j, P[ix]); } while (0)
    IR_JOINT(IR_PELVIS, r.pelvis); IR_JOINT(IR_CHEST, r.chest); IR_JOINT(IR_HEAD, r.head);
    ir_carry(m, pose, r.head, r.headTip, P[IR_HEADTIP]);
    for (int s = 0; s < 2; s++) {
        const int o = s * 4;
        IR_JOINT(IR_HIP_L + o, r.thigh[s]); IR_JOINT(IR_KNEE_L + o, r.shin[s]);
        IR_JOINT(IR_ANKLE_L + o, r.foot[s]); ir_carry(m, pose, r.foot[s], r.toe[s], P[IR_TOE_L + o]);
        IR_JOINT(IR_SH_L + o, r.uarm[s]); IR_JOINT(IR_EL_L + o, r.farm[s]);
        IR_JOINT(IR_WR_L + o, r.hand[s]); ir_carry(m, pose, r.hand[s], r.fin[s], P[IR_FIN_L + o]);
    }
    ir_carry(m, pose, r.head, r.headSide, P[IR_HEADSIDE]);
    for (int k = 0; k < 3; k++) {
        P[IR_WAIST][k] = P[IR_CHEST][k]; P[IR_NECKX][k] = P[IR_HEAD][k];
        P[IR_HIPX_L][k] = P[IR_HIP_L][k]; P[IR_HIPX_R][k] = P[IR_HIP_R][k];
        P[IR_SHX_L][k] = P[IR_SH_L][k]; P[IR_SHX_R][k] = P[IR_SH_R][k];
    }
    if (r.rifle >= 0) {
        IR_JOINT(IR_GRIP, r.rifle);
        if (r.muzzle >= 0) { ir_joint(m, r.muzzle, j); ir_carry(m, pose, r.rifle, j, P[IR_TIP]); }
        else { for (int k = 0; k < 3; k++) P[IR_TIP][k] = P[IR_GRIP][k]; P[IR_TIP][2] -= 0.3f * r.heightU; }
    } else {
        for (int k = 0; k < 3; k++) { P[IR_GRIP][k] = P[IR_WR_R][k]; P[IR_TIP][k] = P[IR_FIN_R][k]; }
    }
    #undef IR_JOINT
}

/* where in its death clip each death hands over to the ragdoll: mid-fall, after the
   clip's own hit and stagger (die1..die5, then lying down) */
static float ir_handoff_share(int variant)
{
    static const float s[5] = { 0.50f, 0.42f, 0.45f, 0.40f, 0.72f };
    if (variant >= 100) return 0.25f;
    return s[(variant % 5 + 5) % 5];
}

static bool ir_ragdoll_on(void) { return g_fx.enabled && g_fx.einf_ragdoll; }

/* ---- the hand-over -------------------------------------------------------------------- */
static void ir_start(InfRag& b, const SimObject& o, const AnimState& a, int ci, float frame)
{
    const M3Model& m = g_m3[b.mi];
    const InfRig& r = ir_rig(b.mi);
    const size_t nb = m.bone.size();
    std::vector<float> now(nb * 8), was(nb * 8);
    const M3Clip& c = m.clip[ci];
    const float tick = c.fps / (float)M3INF_TICKS_PER_SECOND;
    m3_sample(m, ci, frame, &now[0]);
    m3_sample(m, ci, frame - tick, &was[0]);
    float Pn[IR_N][3], Pw[IR_N][3];
    ir_points(m, r, &now[0], Pn);
    ir_points(m, r, &was[0], Pw);
    b.ax = o.wx; b.az = o.wz; b.ay = terrain_y(o.wx, o.wz) + o.ylift;
    b.yawDeg = a.yaw;
    b.S = MODEL_SCALE * m3inf_scale(m);
    b.pose0 = now;
    for (int i = 0; i < IR_N; i++) {
        float wn[3], ww[3];
        ir_to_world(b, Pn[i], wn);
        ir_to_world(b, Pw[i], ww);
        memcpy(b.hoP[i], Pn[i], sizeof Pn[i]);
        /* a substep's worth of the clip's own last tick of motion */
        for (int k = 0; k < 3; k++) { b.p[i][k] = wn[k]; b.pp[i][k] = wn[k] - (wn[k] - ww[k]) * 0.25f; }
    }
    for (int l = 0; l < IR_NLINK; l++) {
        const float* A = b.p[IR_LINKS[l][0]];
        const float* B = b.p[IR_LINKS[l][1]];
        b.len[l] = sqrtf((A[0] - B[0]) * (A[0] - B[0]) + (A[1] - B[1]) * (A[1] - B[1]) + (A[2] - B[2]) * (A[2] - B[2]));
    }
    for (int k = 0; k < 4; k++) {
        const float* A = b.p[IR_REACH[k][0]];
        const float* M = b.p[IR_REACH_MID[k]];
        const float* B = b.p[IR_REACH[k][1]];
        const float l1 = sqrtf((A[0] - M[0]) * (A[0] - M[0]) + (A[1] - M[1]) * (A[1] - M[1]) + (A[2] - M[2]) * (A[2] - M[2]));
        const float l2 = sqrtf((B[0] - M[0]) * (B[0] - M[0]) + (B[1] - M[1]) * (B[1] - M[1]) + (B[2] - M[2]) * (B[2] - M[2]));
        b.reach[k] = l1 + l2;
    }
    /* the weapon: kept in the right hand when it rides on it (a pistol, a grenade, a
       toolbox), dropped as a free stick when it is held in both (a rifle, a launcher) */
    b.handGun = false; b.freeGun = false;
    if (r.rifle >= 0) {
        const float* g = &now[r.rifle * 8];
        const float* h = &now[r.hand[1] * 8];
        float dq = fabsf(g[0] * h[0] + g[1] * h[1] + g[2] * h[2] + g[3] * h[3]);
        float dt = fabsf(g[4] - h[4]) + fabsf(g[5] - h[5]) + fabsf(g[6] - h[6]);
        b.handGun = dq > 0.999f && dt < 0.5f;
        b.freeGun = !b.handGun && r.muzzle >= 0;
    }
    b.state = IRS_RAG;
    b.handoff = g_engineFrame;
    b.still = 0;
}

/* ---- the step ---------------------------------------------------------------------- */
static float ir_ground(float x, float z) { return terrain_y(x, z); }

static void ir_integrate(InfRag& b, const InfRig& r)
{
    const float hc = r.heightU * b.S;                    /* his height in cells */
    const float dt = 1.0f / ((float)M3INF_TICKS_PER_SECOND * 4.0f);
    const float g = 4.5f;                                /* cells a second squared */
    for (int sub = 0; sub < 4; sub++) {
        for (int i = 0; i < IR_N; i++) {
            if (i >= IR_GRIP && !b.freeGun) continue;
            float* p = b.p[i];
            float* q = b.pp[i];
            const float vx = (p[0] - q[0]) * 0.995f, vy = (p[1] - q[1]) * 0.995f, vz = (p[2] - q[2]) * 0.995f;
            q[0] = p[0]; q[1] = p[1]; q[2] = p[2];
            p[0] += vx; p[1] += vy - g * dt * dt; p[2] += vz;
        }
        for (int it = 0; it < 8; it++) {
            for (int l = 0; l < IR_NLINK; l++) {
                const int ia = IR_LINKS[l][0], ib = IR_LINKS[l][1];
                if ((ia >= IR_GRIP || ib >= IR_GRIP) && !b.freeGun) continue;
                if (IR_LINKS[l][2] && (b.cut & (1u << (IR_LINKS[l][2] - 1)))) continue;
                float* A = b.p[ia];
                float* B = b.p[ib];
                float d[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] };
                const float L = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                if (L < 1e-9f) continue;
                const float k = 0.5f * (L - b.len[l]) / L;
                for (int c = 0; c < 3; c++) { A[c] += d[c] * k; B[c] -= d[c] * k; }
            }
            for (int k = 0; k < 4; k++) {
                float* A = b.p[IR_REACH[k][0]];
                float* B = b.p[IR_REACH[k][1]];
                float d[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] };
                const float L = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                const float lo = IR_REACH_MIN[k] * b.reach[k];
                if (L < 1e-9f || L >= lo) continue;
                const float f = 0.5f * (L - lo) / L;
                for (int c = 0; c < 3; c++) { A[c] += d[c] * f; B[c] -= d[c] * f; }
            }
            /* the ground: each point kept its radius above it, and what lies on it drags */
            for (int i = 0; i < IR_N; i++) {
                if (i >= IR_GRIP && !b.freeGun) continue;
                float* p = b.p[i];
                const float floorY = ir_ground(p[0], p[2]) + IR_RADIUS[i] * hc;
                if (p[1] < floorY) {
                    p[1] = floorY;
                    float* q = b.pp[i];
                    q[0] = p[0] - (p[0] - q[0]) * 0.55f;
                    q[2] = p[2] - (p[2] - q[2]) * 0.55f;
                    if (q[1] < p[1]) q[1] = p[1] - (p[1] - q[1]) * 0.2f;
                }
            }
        }
    }
}

/* ---- coming apart ---------------------------------------------------------------------- */
static bool ir_gibs_on(void) { return g_fx.enabled && g_fx.einf_gibs && ir_ragdoll_on(); }
static int g_irCuts = 0, g_irBlasts = 0, g_irCrushes = 0, g_irRunOver = 0;

/* THE VEHICLES THAT CRUSH, by the 1995 rules' own flag (udata.cpp IsCrusher): the tracked
   ones, the harvester, the MCV and the dinosaurs. Wheels and hovercraft roll over a body
   without tearing it. */
static bool ir_crusher(const SimObject& o)
{
    static const char* const T[] = { "HTNK", "MTNK", "LTNK", "STNK", "FTNK", "APC", "HARV", "MCV",
                                     "MHQ", "VICE", "TRIC", "TREX", "STEG" };
    if (o.kind != K_UNIT) return false;
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++)
        if (!strcmp(o.type, T[i])) return true;
    return false;
}

static void ir_cut(InfRag& b, int j)
{
    if (b.cut & (1u << j)) return;
    b.cut |= 1u << j;
    b.cutAt[j] = g_engineFrame;
    g_irCuts++;
}

/* a velocity, cells a second, given to every point of one piece about its middle, with a
   tumble: Verlet keeps velocity as the step from the last place, so the last place is
   set behind */
static void ir_throw(InfRag& b, int piece, const int comp[IG_N], const float* v, const float* w)
{
    const float dt = 1.0f / ((float)M3INF_TICKS_PER_SECOND * 4.0f);
    float c[3] = { 0, 0, 0 };
    int n = 0;
    for (int i = 0; i < IR_N; i++) {
        if (IR_PGRP[i] < 0 || comp[IR_PGRP[i]] != piece) continue;
        for (int k = 0; k < 3; k++) c[k] += b.p[i][k];
        n++;
    }
    if (!n) return;
    for (int k = 0; k < 3; k++) c[k] /= (float)n;
    for (int i = 0; i < IR_N; i++) {
        const bool gun = IR_PGRP[i] < 0;
        if (gun ? !(b.freeGun && piece == 0) : comp[IR_PGRP[i]] != piece) continue;
        const float d[3] = { b.p[i][0] - c[0], b.p[i][1] - c[1], b.p[i][2] - c[2] };
        const float vi[3] = { v[0] + w[1] * d[2] - w[2] * d[1], v[1] + w[2] * d[0] - w[0] * d[2],
                              v[2] + w[0] * d[1] - w[1] * d[0] };
        for (int k = 0; k < 3; k++) b.pp[i][k] = b.p[i][k] - vi[k] * dt;
    }
}

static void ir_piece_middle(const InfRag& b, int piece, const int comp[IG_N], float* c)
{
    int n = 0;
    c[0] = c[1] = c[2] = 0.0f;
    for (int i = 0; i < IR_N; i++) {
        if (IR_PGRP[i] < 0 || comp[IR_PGRP[i]] != piece) continue;
        for (int k = 0; k < 3; k++) c[k] += b.p[i][k];
        n++;
    }
    if (n) for (int k = 0; k < 3; k++) c[k] /= (float)n;
}

/* BLOWN APART: every limb and the head off, the waist one time in three, and each piece
   thrown out from one point a little below his middle (the burst came from the ground
   or struck him), so they go every way at once, upward more than down. */
static void ir_blast(InfRag& b, const InfRig& r)
{
    const unsigned h = ir_hash((unsigned)b.id * 7u + 1u, (unsigned)b.since);
    for (int j = IJ_NECK; j < IJ_N; j++) ir_cut(b, j);
    if (ir_r01(h >> 3) < 0.34f) ir_cut(b, IJ_WAIST);
    const float hc = r.heightU * b.S;
    float o[3];
    for (int k = 0; k < 3; k++) o[k] = 0.5f * (b.p[IR_PELVIS][k] + b.p[IR_CHEST][k]);
    o[0] += (ir_r01(h >> 7) - 0.5f) * 0.25f * hc;
    o[2] += (ir_r01(h >> 11) - 0.5f) * 0.25f * hc;
    o[1] -= 0.30f * hc;
    int comp[IG_N];
    const int n = ir_components(b.cut, comp);
    for (int pc = 0; pc < n; pc++) {
        const unsigned hp = ir_hash(h, (unsigned)pc + 17u);
        float c[3], d[3];
        ir_piece_middle(b, pc, comp, c);
        for (int k = 0; k < 3; k++) d[k] = c[k] - o[k] + (ir_r01(hp >> (k * 4)) - 0.5f) * 0.6f * hc;
        ir_norm3(d);
        const float sp = 0.8f + 0.7f * ir_r01(hp >> 13);
        float v[3] = { d[0] * sp, d[1] * sp + 0.8f + 0.6f * ir_r01(hp >> 17), d[2] * sp };
        float w[3];
        for (int k = 0; k < 3; k++) w[k] = (ir_r01(hp >> (k * 5 + 2)) - 0.5f) * (pc == 0 ? 10.0f : 26.0f);
        ir_throw(b, pc, comp, v, w);
    }
    g_irBlasts++;
}

/* RUN OVER: torn where the tracks went, two to four joints, and the pieces squeezed out
   from under the hull and carried along with it */
static void ir_crush(InfRag& b, const InfRig& r)
{
    const unsigned h = ir_hash((unsigned)b.id * 13u + 5u, (unsigned)b.since);
    int cuts = 0;
    for (int j = 0; j < IJ_N; j++)
        if (ir_r01(h >> (j * 3)) < 0.5f) { ir_cut(b, j); cuts++; }
    for (int j = IJ_HIP_L; cuts < 2 && j < IJ_N; j++)
        if (!(b.cut & (1u << j))) { ir_cut(b, j); cuts++; }
    const float hc = r.heightU * b.S;
    int comp[IG_N];
    const int n = ir_components(b.cut, comp);
    for (int pc = 0; pc < n; pc++) {
        const unsigned hp = ir_hash(h, (unsigned)pc + 31u);
        float c[3];
        ir_piece_middle(b, pc, comp, c);
        float d[2] = { c[0] - b.crushX + (ir_r01(hp) - 0.5f) * 0.3f * hc,
                       c[2] - b.crushZ + (ir_r01(hp >> 8) - 0.5f) * 0.3f * hc };
        const float l = sqrtf(d[0] * d[0] + d[1] * d[1]);
        if (l > 1e-5f) { d[0] /= l; d[1] /= l; } else { d[0] = 1.0f; d[1] = 0.0f; }
        const float sp = 0.22f + 0.3f * ir_r01(hp >> 4);
        const float cv = (float)M3INF_TICKS_PER_SECOND * 0.5f;
        float v[3] = { d[0] * sp + b.crushSX * cv, 0.2f + 0.25f * ir_r01(hp >> 12), d[1] * sp + b.crushSZ * cv };
        float w[3];
        for (int k = 0; k < 3; k++) w[k] = (ir_r01(hp >> (k * 5 + 1)) - 0.5f) * 14.0f;
        ir_throw(b, pc, comp, v, w);
    }
    g_irCrushes++;
}

/* is a ground point under a vehicle's hull: its own measured box (crush_hull_for_mesh),
   turned to the engine's facing (0 north, clockwise; north is -z), shrunk by a share */
static bool ir_under_hull(const SimObject& o, float x, float z, float keep)
{
    const CrushHull hl = crush_hull_for_mesh(mesh_for(o));
    const float a = (float)(o.face >= 0 ? o.face : 0) * (6.2831853f / 256.0f);
    const float dx = x - o.ex, dz = z - o.ez;
    const float lf = dx * sinf(a) - dz * cosf(a), lr = dx * cosf(a) + dz * sinf(a);
    return fabsf(lf) <= hl.len * 0.5f * keep && fabsf(lr) <= hl.beam * 0.5f * keep;
}

/* VEHICLES AND SOLDIERS SHOVE A BODY: a point inside a vehicle's round footprint (a
   soldier's, smaller) is moved out of it and given the vehicle's own step, so a tank
   rolling over a body carries it along and rolls it aside; it wakes a body at rest. */
static std::map<long long, std::pair<float, float> > g_irPrevPos;

/* Broad phase rebuilt from the immutable object snapshot for this physics step.
   Cell lists retain object indices. Re-query after a shove so chained contacts
   still follow the original full-scan order, including a departed overBy hull. */
static std::map<std::pair<int,int>,std::vector<size_t> > g_irPushGrid;
static std::map<long long,size_t> g_irPushIds;
static unsigned long g_irPushCandidates=0;
static void ir_push_grid() {
    g_irPushGrid.clear();g_irPushIds.clear();
    for(size_t i=0;i<g_objects.size();i++) {
        const SimObject& o=g_objects[i];
        if(o.limbo||o.dying||(o.kind!=K_UNIT&&o.kind!=K_INFANTRY)) continue;
        float radius=o.kind==K_UNIT?0.42f:0.10f;
        if(o.kind==K_UNIT&&ir_crusher(o)&&ir_gibs_on()) {
            const CrushHull h=crush_hull_for_mesh(mesh_for(o));
            radius=std::max(radius,0.5f*sqrtf(h.len*h.len+h.beam*h.beam));
        }
        radius+=0.002f;
        const int x0=(int)floorf((o.ex-radius)*0.25f),x1=(int)floorf((o.ex+radius)*0.25f);
        const int z0=(int)floorf((o.ez-radius)*0.25f),z1=(int)floorf((o.ez+radius)*0.25f);
        for(int z=z0;z<=z1;z++) for(int x=x0;x<=x1;x++) g_irPushGrid[{x,z}].push_back(i);
        g_irPushIds[(long long)o.kind*1000000+o.id]=i;
    }
}
static std::vector<size_t> ir_push_candidates(const InfRag& b) {
    std::vector<size_t> indices;
    float x0=b.p[0][0],x1=x0,z0=b.p[0][2],z1=z0;
    for(int j=1;j<IR_N;j++) {
        x0=std::min(x0,b.p[j][0]);x1=std::max(x1,b.p[j][0]);
        z0=std::min(z0,b.p[j][2]);z1=std::max(z1,b.p[j][2]);
    }
    /* Check diagnostic poses before float-to-int conversion. */
    if(!std::isfinite(x0+x1+z0+z1)||fabsf(x0)>1e6f||fabsf(x1)>1e6f||fabsf(z0)>1e6f||fabsf(z1)>1e6f) {
        for(size_t i=0;i<g_objects.size();i++) indices.push_back(i);return indices;
    }
    const int ix0=(int)floorf((x0-0.002f)*0.25f),ix1=(int)floorf((x1+0.002f)*0.25f);
    const int iz0=(int)floorf((z0-0.002f)*0.25f),iz1=(int)floorf((z1+0.002f)*0.25f);
    if((int64_t)(ix1-ix0+1)*(iz1-iz0+1)>4096) {
        for(size_t i=0;i<g_objects.size();i++) indices.push_back(i);return indices;
    }
    for(int z=iz0;z<=iz1;z++) for(int x=ix0;x<=ix1;x++) {
        auto f=g_irPushGrid.find({x,z});
        if(f!=g_irPushGrid.end()) indices.insert(indices.end(),f->second.begin(),f->second.end());
    }
    auto old=g_irPushIds.find(b.overBy);
    if(old!=g_irPushIds.end()) indices.push_back(old->second);
    std::sort(indices.begin(),indices.end());indices.erase(std::unique(indices.begin(),indices.end()),indices.end());
    return indices;
}
static void ir_push(InfRag& b, const InfRig& r)
{
    const float hc = r.heightU * b.S;
    const bool indexed=mesh_perf_switch("OPENCNC3D_CORPSE_GRID");
    std::vector<size_t> candidates;
    if(indexed) candidates=ir_push_candidates(b);
    else for(size_t i=0;i<g_objects.size();i++) candidates.push_back(i);
    size_t at=0;
    while(at<candidates.size()) {
        const size_t i=candidates[at++];g_irPushCandidates++;
        const SimObject& o = g_objects[i];
        if (o.limbo || o.dying) continue;
        float rad;
        if (o.kind == K_UNIT) rad = 0.42f;
        else if (o.kind == K_INFANTRY) {
            if (o.id == b.id) continue;
            rad = 0.10f;
        } else continue;
        const long long k = (long long)o.kind * 1000000 + o.id;
        float sx = 0.0f, sz = 0.0f;
        std::map<long long, std::pair<float, float> >::iterator pv = g_irPrevPos.find(k);
        if (pv != g_irPrevPos.end()) { sx = o.ex - pv->second.first; sz = o.ez - pv->second.second; }
        bool hit = false;
        const unsigned beforeCut=b.cut;
        for (int j = 0; j < IR_N; j++) {
            if (j >= IR_GRIP && !b.freeGun) continue;
            float* p = b.p[j];
            const float dx = p[0] - o.ex, dz = p[2] - o.ez;
            const float d2 = dx * dx + dz * dz;
            if (d2 >= rad * rad) continue;
            if (p[1] > ir_ground(p[0], p[2]) + (o.kind == K_UNIT ? 0.5f : 0.25f) * 1.0f) continue;
            const float d = sqrtf(d2);
            const float nx = d > 1e-5f ? dx / d : 1.0f, nz = d > 1e-5f ? dz / d : 0.0f;
            const float out = (rad - d) * (o.kind == K_UNIT ? 0.6f : 0.25f);
            p[0] += nx * out; p[2] += nz * out;
            /* the shover's step, a substep's share, and a little lift to roll it */
            float* q = b.pp[j];
            q[0] = p[0] - sx * (o.kind == K_UNIT ? 0.35f : 0.15f) - nx * out * 0.25f;
            q[2] = p[2] - sz * (o.kind == K_UNIT ? 0.35f : 0.15f) - nz * out * 0.25f;
            q[1] = p[1] - (o.kind == K_UNIT ? 0.02f : 0.005f) * hc * 4.0f;
            hit = true;
        }
        if (hit) { b.state = IRS_RAG; b.still = 0; g_irPushes++; }
        /* A TRACKED VEHICLE DRIVING OVER A BODY TEARS IT where its hull passes: each
           joint inside the hull's round, three times in four, at least one; once a pass,
           so a tank parked on a body does not keep tearing it */
        if (o.kind == K_UNIT && ir_crusher(o) && ir_gibs_on()) {
            const bool over = ir_under_hull(o, b.p[IR_PELVIS][0], b.p[IR_PELVIS][2], 0.9f)
                           || ir_under_hull(o, b.p[IR_CHEST][0], b.p[IR_CHEST][2], 0.9f);
            const bool moving = sx * sx + sz * sz > 0.0004f;
            if (over && moving && b.overBy != k) {
                const unsigned hh = ir_hash((unsigned)b.id * 3u + (unsigned)o.id, (unsigned)g_engineFrame);
                int cuts = 0, cand = -1;
                for (int j = 0; j < IJ_N; j++) {
                    if (b.cut & (1u << j)) continue;
                    const float* q = b.p[IR_JOINT_P[j][0]];
                    if (!ir_under_hull(o, q[0], q[2], 1.0f)) continue;
                    cand = j;
                    if (ir_r01(hh >> (j * 3)) < 0.75f) { ir_cut(b, j); cuts++; }
                }
                if (!cuts && cand >= 0) ir_cut(b, cand);
                if (cand >= 0) g_irRunOver++;
                b.overBy = k;
            } else if (!over && b.overBy == k) {
                b.overBy = 0;
            }
        }
        if(indexed&&(hit||beforeCut!=b.cut)) {
            candidates=ir_push_candidates(b);
            at=(size_t)(std::upper_bound(candidates.begin(),candidates.end(),i)-candidates.begin());
        }
    }
}

/* ---- blood ------------------------------------------------------------------------- */
static void ir_blood_start(InfRag& b, const InfRig& r)
{
    if (b.bloodBorn >= 0) return;
    const float hc = r.heightU * b.S;
    b.bx = (b.p[IR_PELVIS][0] + b.p[IR_CHEST][0]) * 0.5f;
    b.bz = (b.p[IR_PELVIS][2] + b.p[IR_CHEST][2]) * 0.5f;
    const unsigned h = ir_hash((unsigned)b.id, (unsigned)b.since);
    b.bsize = hc * (0.55f + 0.35f * ir_r01(h));
    b.brot = ir_r01(h >> 16) * 6.2831853f;
    b.bvar = (int)((h >> 8) & 3);
    b.bloodBorn = g_engineFrame;
}

/* ---- the corpse list's side: one body per corpse, and the cap ------------------------- */
/* the soldiers the engine had last tick, and the ones a tracked vehicle has just crushed */
struct IrSeen { SimObject o; AnimState a; int frame; };
struct IrCrush { float x, z, sx, sz; };
static std::map<int, IrSeen> g_irSeen;
static std::map<long long, IrCrush> g_irCrushed;

static InfRag* ir_body_for(const M3Corpse& c)
{
    const long long k = ir_key(c.o.id, c.a.since);
    std::map<long long, InfRag>::iterator it = g_irBody.find(k);
    if (it != g_irBody.end()) return &it->second;
    const int mi = m3inf_model_any(c.o);
    if (mi < 0 || !ir_rig(mi).ok) return NULL;
    InfRag b = InfRag();             /* value-initialised: every number zero */
    b.id = c.o.id; b.since = c.a.since; b.mi = mi; b.variant = c.a.variant;
    b.state = IRS_ANIM; b.handoff = -1; b.bloodBorn = -1;
    /* HOW HE DIED, read at the killing blow: the engine's own kind of death (23 and 24
       high explosive, 25 armour piercing: shells, rockets, grenades), or a crush this
       module saw (g_irCrushed) */
    std::map<long long, IrCrush>::iterator cr = g_irCrushed.find(k);
    if (cr != g_irCrushed.end()) {
        b.cause = IRC_CRUSH;
        b.crushX = cr->second.x; b.crushZ = cr->second.z;
        b.crushSX = cr->second.sx; b.crushSZ = cr->second.sz;
        g_irCrushed.erase(cr);
    } else if (c.o.doing >= 23 && c.o.doing <= 25) {
        b.cause = IRC_BLAST;
    }
    g_irBody[k] = b;
    return &g_irBody[k];
}

/* A NEW BODY BEYOND THE CAP takes the oldest one's place: the oldest corpse goes, and its
   blood with it. Called as each new body is listed. */
static void infdeath_cap(void)
{
    if (!g_fx.enabled) return;
    const int cap = g_fx.einf_corpse_max > 0.0f ? (int)(g_fx.einf_corpse_max + 0.5f) : 200;
    while ((int)g_m3Corpses.size() > cap) {
        size_t old = 0;
        for (size_t i = 1; i < g_m3Corpses.size(); i++)
            if (g_m3Corpses[i].a.since < g_m3Corpses[old].a.since) old = i;
        g_irBody.erase(ir_key(g_m3Corpses[old].o.id, g_m3Corpses[old].a.since));
        g_m3Corpses.erase(g_m3Corpses.begin() + old);
    }
}

/* ---- the spray -------------------------------------------------------------------------
   A TORN JOINT SPRAYS FOR 3 SECONDS (IR_SPRAY_TICKS), from both its sides: drops thrown out
   of the torn end, fewer and slower as it goes, carrying some of the piece's own motion.
   A drop is a point under gravity. Where it lands it leaves a small splat: on the ground
   a decal laid on the terrain (ir_splat), on a vehicle or a building a decal projected
   onto whatever surface the depth buffer has there (IrObjSplat), which stays on a
   vehicle as it drives. The drops of one body leave at most IR_SPLATS_PER_BODY splats
   on the ground, and the splats go with the body when the cap takes it. */
struct IrDrop { float p[3], v[3]; int born; long long body; };
struct IrSplat { float x, z, size, rot, stretch; int var, born; long long body; };
struct IrObjSplat { int kind, id; float l[3], d[3]; float size, rot; int var, born; };
static std::vector<IrDrop> g_irDrops;
static std::vector<IrSplat> g_irSplats;
static std::vector<IrObjSplat> g_irObjSplats;
static int g_irDropsMade = 0, g_irSplatGround = 0, g_irSplatObj = 0;
static const int IR_SPRAY_TICKS = 45;              /* 3 s at the engine's 15 ticks */
static const size_t IR_DROP_MAX = 3000, IR_SPLAT_MAX = 12000, IR_OBJSPLAT_MAX = 1500;
static const int IR_SPLATS_PER_BODY = 90, IR_OBJSPLATS_PER_OBJ = 40;
/* the point inside each side of a joint the torn end faces away from */
static const unsigned char IR_SPRAY_IN[IJ_N][2] = {
    { IR_PELVIS, IR_HEAD }, { IR_CHEST, IR_HEADTIP }, { IR_WAIST, IR_KNEE_L }, { IR_WAIST, IR_KNEE_R },
    { IR_CHEST, IR_EL_L }, { IR_CHEST, IR_EL_R } };

static void ir_spray_emit(const InfRag& b, long long key)
{
    if (!g_fx.einf_spray || !b.cut) return;
    const float dt = 1.0f / ((float)M3INF_TICKS_PER_SECOND * 4.0f);
    for (int j = 0; j < IJ_N; j++) {
        if (!(b.cut & (1u << j))) continue;
        const int age = g_engineFrame - b.cutAt[j];
        if (age < 0 || age >= IR_SPRAY_TICKS) continue;
        const float f = 1.0f - (float)age / (float)IR_SPRAY_TICKS;
        const float rate = 2.2f * f * f;
        for (int side = 0; side < 2; side++) {
            const unsigned h = ir_hash((unsigned)b.id * 41u + (unsigned)(j * 2 + side), (unsigned)g_engineFrame);
            const int n = (int)rate + (ir_r01(h) < rate - floorf(rate) ? 1 : 0);
            const float* P = b.p[IR_JOINT_P[j][side]];
            const float* Pp = b.pp[IR_JOINT_P[j][side]];
            const float* Q = b.p[IR_SPRAY_IN[j][side]];
            float dir[3] = { P[0] - Q[0], P[1] - Q[1], P[2] - Q[2] };
            ir_norm3(dir);
            for (int e = 0; e < n && g_irDrops.size() < IR_DROP_MAX; e++) {
                const unsigned hh = ir_hash(h, (unsigned)e + 3u);
                const float sp = (0.45f + 0.65f * ir_r01(hh)) * (0.6f + 0.4f * f);
                IrDrop d;
                for (int k = 0; k < 3; k++) {
                    d.v[k] = dir[k] * sp + (ir_r01(hh >> (k * 5 + 4)) - 0.5f) * 0.7f + (P[k] - Pp[k]) / dt * 0.6f;
                    d.p[k] = P[k] + (ir_r01(hh >> (k * 4 + 19)) - 0.5f) * 0.01f;
                }
                d.born = g_engineFrame;
                d.body = key;
                g_irDrops.push_back(d);
                g_irDropsMade++;
            }
        }
    }
}

/* a vehicle's heading as two ground axes, ahead and to its right, from the engine's
   PrimaryFacing (0 north, turning clockwise; north is -z) */
static void ir_axes_of(const SimObject& o, float* fwd, float* right)
{
    const float a = (float)(o.face >= 0 ? o.face : 0) * (6.2831853f / 256.0f);
    fwd[0] = sinf(a); fwd[1] = -cosf(a);
    right[0] = cosf(a); right[1] = sinf(a);
}

/* is a world point inside the rough volume of a vehicle or a building: the hull's own
   measured box, or the footprint, up to a height either is unlikely to exceed */
static bool ir_in_object(const SimObject& o, const float* p, float* local)
{
    const float gy = terrain_y(o.ex, o.ez);
    const float up = p[1] - gy;
    if (o.kind == K_UNIT) {
        if (o.alt > 0 || up > 0.42f || up < -0.05f) return false;
        const CrushHull hl = crush_hull_for_mesh(mesh_for(o));
        float f[2], r[2];
        ir_axes_of(o, f, r);
        const float dx = p[0] - o.ex, dz = p[2] - o.ez;
        const float lf = dx * f[0] + dz * f[1], lr = dx * r[0] + dz * r[1];
        if (fabsf(lf) > hl.len * 0.5f || fabsf(lr) > hl.beam * 0.5f) return false;
        local[0] = lr; local[1] = up; local[2] = lf;
        return true;
    }
    if (o.kind == K_BUILDING) {
        if (up > 0.75f || up < -0.05f) return false;
        if (fabsf(p[0] - o.ex) > o.fw * 0.5f || fabsf(p[2] - o.ez) > o.fh * 0.5f) return false;
        local[0] = p[0]; local[1] = p[1]; local[2] = p[2];
        return true;
    }
    return false;
}

static void ir_drops_step(void)
{
    if (g_irDrops.empty()) return;
    const float dt = 1.0f / ((float)M3INF_TICKS_PER_SECOND * 4.0f);
    /* the vehicles and buildings near any drop: a box around all of them, widened */
    float lo[2] = { 1e9f, 1e9f }, hi[2] = { -1e9f, -1e9f };
    for (size_t i = 0; i < g_irDrops.size(); i++) {
        const float* p = g_irDrops[i].p;
        if (p[0] < lo[0]) lo[0] = p[0]; if (p[0] > hi[0]) hi[0] = p[0];
        if (p[2] < lo[1]) lo[1] = p[2]; if (p[2] > hi[1]) hi[1] = p[2];
    }
    std::vector<size_t> nearby;
    for (size_t i = 0; i < g_objects.size(); i++) {
        const SimObject& o = g_objects[i];
        if (o.limbo || (o.kind != K_UNIT && o.kind != K_BUILDING)) continue;
        const float rr = o.kind == K_BUILDING ? 0.5f * (float)(o.fw > o.fh ? o.fw : o.fh) + 0.5f : 1.2f;
        if (o.ex < lo[0] - rr || o.ex > hi[0] + rr || o.ez < lo[1] - rr || o.ez > hi[1] + rr) continue;
        nearby.push_back(i);
    }
    for (size_t i = 0; i < g_irDrops.size(); ) {
        IrDrop& d = g_irDrops[i];
        bool gone = g_engineFrame - d.born > IR_SPRAY_TICKS || g_engineFrame < d.born;
        for (int s = 0; s < 4 && !gone; s++) {
            d.v[1] -= 4.5f * dt;
            for (int k = 0; k < 3; k++) d.p[k] += d.v[k] * dt;
            float loc[3];
            for (size_t n = 0; n < nearby.size() && !gone; n++) {
                const SimObject& o = g_objects[nearby[n]];
                if (!ir_in_object(o, d.p, loc)) continue;
                gone = true;
                int on = 0;
                for (size_t q = 0; q < g_irObjSplats.size(); q++)
                    on += g_irObjSplats[q].kind == (int)o.kind && g_irObjSplats[q].id == o.id;
                if (on >= IR_OBJSPLATS_PER_OBJ) break;
                IrObjSplat sp;
                sp.kind = (int)o.kind; sp.id = o.id;
                memcpy(sp.l, loc, sizeof loc);
                float v[3] = { d.v[0], d.v[1], d.v[2] };
                ir_norm3(v);
                if (o.kind == K_UNIT) {
                    float f[2], r[2];
                    ir_axes_of(o, f, r);
                    sp.d[0] = v[0] * r[0] + v[2] * r[1]; sp.d[1] = v[1]; sp.d[2] = v[0] * f[0] + v[2] * f[1];
                } else {
                    memcpy(sp.d, v, sizeof v);
                }
                const unsigned h = ir_hash((unsigned)g_irSplatObj * 7u + 1u, (unsigned)d.born);
                sp.size = 0.035f + 0.045f * ir_r01(h);
                sp.rot = ir_r01(h >> 8) * 6.2831853f;
                sp.var = (int)((h >> 16) & 3);
                sp.born = g_engineFrame;
                if (g_irObjSplats.size() >= IR_OBJSPLAT_MAX) g_irObjSplats.erase(g_irObjSplats.begin());
                g_irObjSplats.push_back(sp);
                g_irSplatObj++;
            }
            if (gone) break;
            const float gy = terrain_y(d.p[0], d.p[2]);
            if (d.p[1] <= gy) {
                gone = true;
                std::map<long long, InfRag>::iterator bo = g_irBody.find(d.body);
                if (bo == g_irBody.end() || bo->second.splats >= IR_SPLATS_PER_BODY) break;
                bo->second.splats++;
                IrSplat sp;
                sp.x = d.p[0]; sp.z = d.p[2];
                const unsigned h = ir_hash((unsigned)g_irSplatGround * 3u + 7u, (unsigned)d.born);
                const float hs = sqrtf(d.v[0] * d.v[0] + d.v[2] * d.v[2]);
                sp.size = 0.022f + 0.03f * ir_r01(h);
                sp.rot = hs > 0.05f ? atan2f(d.v[2], d.v[0]) : ir_r01(h >> 8) * 6.2831853f;
                sp.stretch = 1.0f + (hs * 0.9f > 1.4f ? 1.4f : hs * 0.9f);
                sp.var = (int)((h >> 16) & 3);
                sp.born = g_engineFrame;
                sp.body = d.body;
                if (g_irSplats.size() >= IR_SPLAT_MAX) g_irSplats.erase(g_irSplats.begin());
                g_irSplats.push_back(sp);
                g_irSplatGround++;
            }
        }
        if (gone) g_irDrops.erase(g_irDrops.begin() + i);
        else i++;
    }
}

/* a splat on a vehicle or building outlives neither */
static void ir_objsplats_sweep(void)
{
    if (g_irObjSplats.empty()) return;
    std::map<long long, int> live;
    for (size_t i = 0; i < g_objects.size(); i++)
        if (!g_objects[i].limbo) live[(long long)g_objects[i].kind * 1000000 + g_objects[i].id] = 1;
    for (size_t i = 0; i < g_irObjSplats.size(); ) {
        const IrObjSplat& sp = g_irObjSplats[i];
        if (!live.count((long long)sp.kind * 1000000 + sp.id) || g_engineFrame < sp.born)
            g_irObjSplats.erase(g_irObjSplats.begin() + i);
        else i++;
    }
}

/* IS THERE BLOOD UNDER A VEHICLE HERE: a body's point, a torn piece or a pool of blood
   within a radius, for the tracks it lays (treadmark_mod.h) */
static bool infdeath_blood_at(float x, float z, float rad)
{
    if (!g_fx.enabled) return false;
    for (std::map<long long, InfRag>::const_iterator it = g_irBody.begin(); it != g_irBody.end(); ++it) {
        const InfRag& b = it->second;
        if (b.state == IRS_ANIM && b.bloodBorn < 0) continue;
        if (b.bloodBorn >= 0) {
            const float dx = b.bx - x, dz = b.bz - z, rr = rad + b.bsize * 0.35f;
            if (dx * dx + dz * dz < rr * rr) return true;
        }
        if (b.state == IRS_ANIM) continue;
        for (int i = 0; i < IR_GRIP; i++) {
            const float dx = b.p[i][0] - x, dz = b.p[i][2] - z;
            if (dx * dx + dz * dz < rad * rad) return true;
        }
    }
    return false;
}

/* ---- the tick ------------------------------------------------------------------------ */
/* RUN OVER. DriveClass::Overrun_Square deletes a crushed soldier outright, with no death
   of his own: he is simply gone from the next tick's list. A soldier the engine had last
   tick and has no longer, who was not dying and not taken off the map (boarding a
   transport only limbos him), with a tracked vehicle on top of him, was crushed: he is
   given a body here, where he stood, that comes apart under the hull. */
static void ir_detect_crushes(void)
{
    for (std::map<int, IrSeen>::iterator it = g_irSeen.begin(); it != g_irSeen.end(); ) {
        if (it->second.frame >= g_engineFrame) { ++it; continue; }
        const IrSeen sv = it->second;
        g_irSeen.erase(it++);
        if (sv.frame != g_engineFrame - 1 || !ir_gibs_on()) continue;
        if (sv.o.dying || sv.o.limbo || sv.a.slot == M3INF_DIE) continue;
        const SimObject* by = NULL;
        float best = 0.75f * 0.75f;
        for (size_t i = 0; i < g_objects.size(); i++) {
            const SimObject& o = g_objects[i];
            if (o.limbo || !ir_crusher(o)) continue;
            const float dx = o.ex - sv.o.ex, dz = o.ez - sv.o.ez;
            if (dx * dx + dz * dz < best) { best = dx * dx + dz * dz; by = &o; }
        }
        if (!by) continue;
        const int mi = m3inf_model_any(sv.o);
        if (mi < 0 || !ir_rig(mi).ok || m3inf_slot_ticks(sv.o, M3INF_DIE, 0) <= 0) continue;
        M3Corpse c;
        c.o = sv.o; c.o.dying = 1;
        c.a = sv.a;
        c.a.slot = M3INF_DIE; c.a.variant = 0; c.a.since = g_engineFrame;
        for (int k = 0; k < M3INF_SLOTS; k++) c.a.w[k] = c.a.wPrev[k] = (k == M3INF_DIE) ? 1.0f : 0.0f;
        c.lastSeen = g_engineFrame - 1;
        IrCrush cr;
        cr.x = by->ex; cr.z = by->ez; cr.sx = cr.sz = 0.0f;
        std::map<long long, std::pair<float, float> >::iterator pv =
            g_irPrevPos.find((long long)by->kind * 1000000 + by->id);
        if (pv != g_irPrevPos.end()) { cr.sx = by->ex - pv->second.first; cr.sz = by->ez - pv->second.second; }
        g_irCrushed[ir_key(c.o.id, c.a.since)] = cr;
        g_m3Corpses.push_back(c);
        infdeath_cap();
    }
}

static void infdeath_step(void)
{
    if (g_irLastFrame >= 0 && g_engineFrame < g_irLastFrame) {
        g_irBody.clear();                     /* the clock ran backwards: a new game */
        g_irPrevPos.clear();
        g_irSeen.clear(); g_irCrushed.clear();
        g_irDrops.clear(); g_irSplats.clear(); g_irObjSplats.clear();
    }
    g_irLastFrame = g_engineFrame;
    g_irRagLive = 0;
    ir_detect_crushes();
    if(mesh_perf_switch("OPENCNC3D_CORPSE_GRID")) ir_push_grid();
    for (size_t i = 0; i < g_m3Corpses.size(); i++) {
        const M3Corpse& c = g_m3Corpses[i];
        InfRag* b = ir_body_for(c);
        if (!b) continue;
        const M3Model& m = g_m3[b->mi];
        const InfRig& r = ir_rig(b->mi);
        const int ci = m3inf_slot_clip(m, M3INF_DIE, c.a.variant);
        if (ci < 0) continue;
        const M3Clip& clip = m.clip[ci];
        const float frame = (float)(g_engineFrame - c.a.since) / (float)M3INF_TICKS_PER_SECOND * clip.fps;
        if (b->state == IRS_ANIM) {
            if (b->cause != IRC_NONE && ir_gibs_on()) {
                /* TORN APART AT THE KILLING BLOW: the ragdoll takes him at once */
                const float tick = clip.fps / (float)M3INF_TICKS_PER_SECOND;
                ir_start(*b, c.o, c.a, ci, frame > tick ? frame : tick);
                if (b->cause == IRC_BLAST) ir_blast(*b, r);
                else ir_crush(*b, r);
                if (g_fx.einf_blood && b->cause == IRC_CRUSH) ir_blood_start(*b, r);
            } else if (ir_ragdoll_on() && frame >= ir_handoff_share(c.a.variant) * (float)(clip.nframe - 1))
                ir_start(*b, c.o, c.a, ci, frame);
            else if (!ir_ragdoll_on() && frame >= (float)(clip.nframe - 1) && b->bloodBorn < 0) {
                /* no ragdoll: the blood goes under where the clip leaves him */
                std::vector<float> pose(m.bone.size() * 8);
                m3_sample(m, ci, (float)(clip.nframe - 1), &pose[0]);
                float P[IR_N][3];
                ir_points(m, r, &pose[0], P);
                b->ax = c.o.wx; b->az = c.o.wz; b->ay = terrain_y(c.o.wx, c.o.wz) + c.o.ylift;
                b->yawDeg = c.a.yaw; b->S = MODEL_SCALE * m3inf_scale(m);
                ir_to_world(*b, P[IR_PELVIS], b->p[IR_PELVIS]);
                ir_to_world(*b, P[IR_CHEST], b->p[IR_CHEST]);
                if (g_fx.einf_blood) ir_blood_start(*b, r);
            }
            if (b->state == IRS_ANIM) continue;
        }
        const unsigned cutBefore = b->cut;
        ir_push(*b, r);
        if (b->cut != cutBefore) { b->state = IRS_RAG; b->still = 0; }
        ir_spray_emit(*b, ir_key(c.o.id, c.a.since));
        if (b->state == IRS_RAG) {
            float before[IR_N][3];
            memcpy(before, b->p, sizeof before);
            ir_integrate(*b, r);
            float mv = 0.0f;
            for (int k = 0; k < IR_N; k++) {
                const float d = fabsf(b->p[k][0] - before[k][0]) + fabsf(b->p[k][1] - before[k][1])
                              + fabsf(b->p[k][2] - before[k][2]);
                if (d > mv) mv = d;
            }
            b->still = mv < 0.0015f ? b->still + 1 : 0;
            if (b->still >= 12) b->state = IRS_ASLEEP;
            g_irRagLive++;
        }
        /* blood once he is down: lying on the ground, or a second after the hand-over */
        if (g_fx.einf_blood && b->bloodBorn < 0
            && (b->state == IRS_ASLEEP || g_engineFrame - b->handoff > 15))
            ir_blood_start(*b, r);
    }
    /* drop the bodies whose corpse is gone (a cap, a scenario reload), and their spray */
    bool dropped = false;
    for (std::map<long long, InfRag>::iterator it = g_irBody.begin(); it != g_irBody.end(); ) {
        bool live = false;
        for (size_t i = 0; i < g_m3Corpses.size() && !live; i++)
            live = ir_key(g_m3Corpses[i].o.id, g_m3Corpses[i].a.since) == it->first;
        if (live) ++it; else { g_irBody.erase(it++); dropped = true; }
    }
    if (dropped) {
        for (size_t i = 0; i < g_irSplats.size(); )
            if (!g_irBody.count(g_irSplats[i].body)) g_irSplats.erase(g_irSplats.begin() + i); else i++;
        for (size_t i = 0; i < g_irDrops.size(); )
            if (!g_irBody.count(g_irDrops[i].body)) g_irDrops.erase(g_irDrops.begin() + i); else i++;
    }
    ir_drops_step();
    ir_objsplats_sweep();
    /* where every shover is this tick, for its step next tick */
    for (size_t i = 0; i < g_objects.size(); i++) {
        const SimObject& o = g_objects[i];
        if (o.kind != K_UNIT && o.kind != K_INFANTRY) continue;
        g_irPrevPos[(long long)o.kind * 1000000 + o.id] = std::make_pair(o.ex, o.ez);
    }
}

/* ---- the draw's side: the pose and the shade ------------------------------------------ */
/* The ragdoll's pose for a dying man or a corpse, model space, 8 floats a bone; false
   while the clip still has him (or the ragdoll is off). */
static bool infdeath_pose(int mi, const SimObject& o, const AnimState& a, float* out)
{
    std::map<long long, InfRag>::const_iterator it = g_irBody.find(ir_key(o.id, a.since));
    if (it == g_irBody.end() || it->second.state == IRS_ANIM || it->second.mi != mi)
        return false;
    const InfRag& b = it->second;
    const M3Model& m = g_m3[mi];
    const InfRig& r = ir_rig(mi);
    const size_t nb = m.bone.size();
    float P[IR_N][3];
    for (int i = 0; i < IR_N; i++) ir_to_model(b, b.p[i], P[i]);
    memcpy(out, &b.pose0[0], nb * 8 * sizeof(float));

    /* one bone: the turn from its hand-over direction(s) to now, applied to its hand-over
       pose; its joint goes to the point that marks it */
    #define IR_SUB(a_, b_, o_) do { o_[0] = a_[0] - b_[0]; o_[1] = a_[1] - b_[1]; o_[2] = a_[2] - b_[2]; } while (0)
    float d0[3], d1[3], s0[3], s1[3], q[4];
    #define IR_APPLY(bone, ref) do { \
        float* po = out + (bone) * 8; float nq[4]; ir_qmul(q, po, nq); \
        po[0] = nq[0]; po[1] = nq[1]; po[2] = nq[2]; po[3] = nq[3]; \
        float off[3] = { b.pose0[(bone) * 8 + 4] - b.hoP[ref][0], b.pose0[(bone) * 8 + 5] - b.hoP[ref][1], \
                         b.pose0[(bone) * 8 + 6] - b.hoP[ref][2] }; float ro[3]; ir_qrot(q, off, ro); \
        po[4] = P[ref][0] + ro[0]; po[5] = P[ref][1] + ro[1]; po[6] = P[ref][2] + ro[2]; } while (0)
    #define IR_LIMB(bone, ia, ib_) do { IR_SUB(b.hoP[ib_], b.hoP[ia], d0); IR_SUB(P[ib_], P[ia], d1); \
        ir_norm3(d0); ir_norm3(d1); ir_qarc(d0, d1, q); IR_APPLY(bone, ia); } while (0)
    #define IR_FRAME(bone, at, up, sl, sr) do { IR_SUB(b.hoP[up], b.hoP[at], d0); IR_SUB(P[up], P[at], d1); \
        IR_SUB(b.hoP[sl], b.hoP[sr], s0); IR_SUB(P[sl], P[sr], s1); ir_qframe(d0, s0, d1, s1, q); IR_APPLY(bone, at); } while (0)

    IR_FRAME(r.pelvis, IR_PELVIS, IR_WAIST, IR_HIP_L, IR_HIP_R);
    float qp[4] = { q[0], q[1], q[2], q[3] };
    if (r.root >= 0) { q[0] = qp[0]; q[1] = qp[1]; q[2] = qp[2]; q[3] = qp[3]; IR_APPLY(r.root, IR_PELVIS); }
    IR_FRAME(r.chest, IR_CHEST, IR_HEAD, IR_SH_L, IR_SH_R);
    /* any other bone of the upper body (a collarbone, carrying the arm) turns with the
       chest; left at its hand-over pose, the skin round a shoulder stayed where the man
       died while the body fell */
    for (size_t bi = 0; bi < nb && bi < (size_t)M3_MAX_BONES; bi++)
        if ((int)bi != r.chest && r.grp[bi] == IG_UPPER) IR_APPLY((int)bi, IR_CHEST);
    IR_FRAME(r.head, IR_NECKX, IR_HEADTIP, IR_HEADSIDE, IR_NECKX);
    float qh[4] = { 0, 0, 0, 1 };
    for (int s = 0; s < 2; s++) {
        const int o4 = s * 4;
        IR_LIMB(r.thigh[s], IR_HIPX_L + s, IR_KNEE_L + o4);
        IR_LIMB(r.shin[s], IR_KNEE_L + o4, IR_ANKLE_L + o4);
        IR_LIMB(r.foot[s], IR_ANKLE_L + o4, IR_TOE_L + o4);
        IR_LIMB(r.uarm[s], IR_SHX_L + s, IR_EL_L + o4);
        /* and any other bone of this arm (a shoulder's half-turn helper) with the upper arm */
        for (size_t bi = 0; bi < nb && bi < (size_t)M3_MAX_BONES; bi++)
            if (r.grp[bi] == IG_ARM_L + s && (int)bi != r.uarm[s] && (int)bi != r.farm[s]
                && (int)bi != r.hand[s]) IR_APPLY((int)bi, IR_SHX_L + s);
        IR_LIMB(r.farm[s], IR_EL_L + o4, IR_WR_L + o4);
        IR_LIMB(r.hand[s], IR_WR_L + o4, IR_FIN_L + o4);
        if (s == 1) { qh[0] = q[0]; qh[1] = q[1]; qh[2] = q[2]; qh[3] = q[3]; }
    }
    /* the weapon and the marks on it: dropped on its own, or with the hand that holds it */
    const int marks[4] = { r.rifle, r.muzzle, r.eject, r.ejectdir };
    if (b.freeGun) {
        IR_SUB(b.hoP[IR_TIP], b.hoP[IR_GRIP], d0); IR_SUB(P[IR_TIP], P[IR_GRIP], d1);
        ir_norm3(d0); ir_norm3(d1); ir_qarc(d0, d1, q);
        for (int k = 0; k < 4; k++) if (marks[k] >= 0) IR_APPLY(marks[k], IR_GRIP);
    } else if (b.handGun) {
        q[0] = qh[0]; q[1] = qh[1]; q[2] = qh[2]; q[3] = qh[3];
        for (int k = 0; k < 4; k++) if (marks[k] >= 0) IR_APPLY(marks[k], IR_WR_R);
    } else {
        q[0] = qp[0]; q[1] = qp[1]; q[2] = qp[2]; q[3] = qp[3];
        for (int k = 0; k < 4; k++) if (marks[k] >= 0) IR_APPLY(marks[k], IR_PELVIS);
    }
    #undef IR_SUB
    #undef IR_APPLY
    #undef IR_LIMB
    #undef IR_FRAME
    (void)nb;
    return true;
}

/* ---- the pieces ------------------------------------------------------------------------ */
static const InfRag* ir_body_drawn(int mi, const SimObject& o, const AnimState& a)
{
    std::map<long long, InfRag>::const_iterator it = g_irBody.find(ir_key(o.id, a.since));
    if (it == g_irBody.end() || it->second.state == IRS_ANIM || it->second.mi != mi) return NULL;
    return &it->second;
}

/* How many draws the body takes: 1 while he is whole. */
static int infdeath_npieces(int mi, const SimObject& o, const AnimState& a)
{
    const InfRag* b = ir_body_drawn(mi, o, a);
    if (!b || !b->cut || !ir_rig(mi).capOk) return 1;
    int comp[IG_N];
    return ir_components(b->cut, comp);
}

static int g_irTornDraws = 0;
static std::vector<M3Sub> g_irSubSave;

/* A TORN BODY, SKINNED ONCE AND DRAWN ONCE. Every vertex is posed as the piece it belongs
   to sees the body: its own piece's bones as the ragdoll has them, and a bone across a
   cut joint folded to that joint's point on the vertex's own side (scale 0). A vertex
   shared by triangles on both sides of a joint was copied for the far side when the
   model was prepared (ir_build_caps), so each side closes onto its own end of the cut,
   where its cap is. The model's draw list is swapped for the torn one, with the caps of
   the cut joints, until infdeath_torn_end. */
static void infdeath_torn_begin(int mi, const SimObject& o, const AnimState& a, const float* pose)
{
    const InfRag* b = ir_body_drawn(mi, o, a);
    M3Model& m = g_m3[mi];
    const InfRig& r = ir_rig(mi);
    if (!b || !r.capOk) { m3_skin(m, pose); return; }
    const size_t nb = m.bone.size();
    const size_t nv = m.vert.size() / 8;
    if (m.posed.size() != m.vert.size()) m.posed = m.vert;
    int comp[IG_N];
    ir_components(b->cut, comp);
    float P[IR_N][3];
    for (int i = 0; i < IR_N; i++) ir_to_model(*b, b->p[i], P[i]);
    /* for a vertex at home in group h, where a bone of group g folds to: -1 not folded,
       else the point (IR_) of the joint leaving h's piece toward g, on h's side */
    int fold[IG_N][IG_N];
    for (int h = 0; h < IG_N; h++) {
        int entry[IG_N];
        bool seen[IG_N];
        for (int g = 0; g < IG_N; g++) { seen[g] = comp[g] == comp[h]; entry[g] = -1; }
        for (int pass = 0; pass < IG_N; pass++)
            for (int j = 0; j < IJ_N; j++)
                for (int sd = 0; sd < 2; sd++) {
                    const int from = IR_JOINT_G[j][sd], to = IR_JOINT_G[j][1 - sd];
                    if (!seen[from] || seen[to]) continue;
                    seen[to] = true;
                    entry[to] = comp[from] == comp[h] ? (j * 2 + sd) : entry[from];
                }
        for (int g = 0; g < IG_N; g++)
            fold[h][g] = comp[g] == comp[h] || entry[g] < 0 ? -1 : IR_JOINT_P[entry[g] / 2][entry[g] % 2];
    }
    const int gunGrp = b->handGun ? IG_ARM_R : IG_LOWER;
    /* THROUGH THE FRAME'S POSE CACHE, like a body in one piece (m3_pose_find): the passes
       after the first read this skin back instead of making it again at full detail, and
       draw it from the card (m3_pose_gpu). The key is the skin's whole input: the bone
       poses, the cut joints' points, which joints are cut and which piece holds the gun.
       Everything else it reads is fixed with the model. */
    static float key[M3_MAX_BONES * 8 + IR_N * 3 + 2];
    float* cached = 0;
    int got = -1;
    if (nb <= (size_t)M3_MAX_BONES) {
        size_t nk = nb * 8;
        memcpy(key, pose, nk * sizeof(float));
        for (int i = 0; i < IR_N; i++) {
            key[nk++] = P[i][0]; key[nk++] = P[i][1]; key[nk++] = P[i][2];
        }
        const unsigned cut = b->cut;
        memcpy(&key[nk++], &cut, sizeof cut);
        key[nk++] = (float)gunGrp;
        got = m3_pose_find(m, nv, key, nk, &cached);
    } else {
        m.drawPosed = 0;
    }
    /* a hit is the skin already made, unless it is to be checked */
    if (got <= 0 || m3_pose_check()) {
        /* the bones' matrices, as m3_skin builds them */
        static float M[M3_MAX_BONES][12];
        static signed char G[M3_MAX_BONES];
        for (size_t bi = 0; bi < nb && bi < (size_t)M3_MAX_BONES; bi++) {
            const float* p = pose + bi * 8;
            float q[4] = { p[0], p[1], p[2], p[3] };
            float ql = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
            ql = ql > 1e-12f ? 1.0f / sqrtf(ql) : 0.0f;
            for (int k = 0; k < 4; k++) q[k] *= ql;
            const float sc = p[7];
            const float x = q[0], y = q[1], z = q[2], w = q[3];
            const float Hm[12] = {
                (1 - 2 * (y * y + z * z)) * sc, (2 * (x * y - z * w)) * sc, (2 * (x * z + y * w)) * sc, p[4],
                (2 * (x * y + z * w)) * sc, (1 - 2 * (x * x + z * z)) * sc, (2 * (y * z - x * w)) * sc, p[5],
                (2 * (x * z - y * w)) * sc, (2 * (y * z + x * w)) * sc, (1 - 2 * (x * x + y * y)) * sc, p[6] };
            const float* I = m.bone[bi].ib;
            float* o2 = M[bi];
            for (int rr = 0; rr < 3; rr++) {
                for (int col = 0; col < 3; col++)
                    o2[rr * 4 + col] = Hm[rr * 4] * I[col] + Hm[rr * 4 + 1] * I[4 + col] + Hm[rr * 4 + 2] * I[8 + col];
                o2[rr * 4 + 3] = Hm[rr * 4] * I[3] + Hm[rr * 4 + 1] * I[7] + Hm[rr * 4 + 2] * I[11] + Hm[rr * 4 + 3];
            }
            G[bi] = r.grp[bi] < 0 ? (signed char)gunGrp : r.grp[bi];
        }
        /* into the cache entry, m.posed when the cache is full, or for a hit being checked
           (OPENCNC3D_POSECHECK) a scratch array compared with the entry below */
        static std::vector<float> check;
        if (got > 0)
            check.resize(nv * 6);
        const float* src = &m.vert[0];
        float* dst = got > 0 ? &check[0] : (cached ? cached : &m.posed[0]);
        const size_t ds = cached ? 6 : 8;
        const unsigned char* sk = &m.skin[0];
        for (size_t v = 0; v < nv; v++, src += 8, dst += ds, sk += 8) {
            int h = v < r.vhome.size() ? r.vhome[v] : IG_UPPER;
            if (h < 0) h = gunGrp;
            float px = 0, py = 0, pz = 0, nx = 0, ny = 0, nz = 0;
            for (int k = 0; k < 4; k++) {
                if (!sk[4 + k]) continue;
                const float w = (float)sk[4 + k] * (1.0f / 255.0f);
                const int bi = sk[k];
                const int f = fold[h][G[bi]];
                if (f >= 0) {                 /* across a cut: folded to the joint */
                    px += w * P[f][0]; py += w * P[f][1]; pz += w * P[f][2];
                    continue;
                }
                const float* o2 = M[bi];
                px += w * (o2[0] * src[0] + o2[1] * src[1] + o2[2] * src[2] + o2[3]);
                py += w * (o2[4] * src[0] + o2[5] * src[1] + o2[6] * src[2] + o2[7]);
                pz += w * (o2[8] * src[0] + o2[9] * src[1] + o2[10] * src[2] + o2[11]);
                nx += w * (o2[0] * src[3] + o2[1] * src[4] + o2[2] * src[5]);
                ny += w * (o2[4] * src[3] + o2[5] * src[4] + o2[6] * src[5]);
                nz += w * (o2[8] * src[3] + o2[9] * src[4] + o2[10] * src[5]);
            }
            float nl = nx * nx + ny * ny + nz * nz;
            nl = nl > 1e-20f ? 1.0f / sqrtf(nl) : 0.0f;
            dst[0] = px; dst[1] = py; dst[2] = pz;
            dst[3] = nx * nl; dst[4] = ny * nl; dst[5] = nz * nl;
        }
        if (got > 0 && memcmp(&check[0], cached, nv * 6 * sizeof(float)) != 0)
            g_m3PoseBad++;
    }
    /* the torn draw list: the model again, side by side, and the cut joints' ends */
    g_irSubSave.swap(m.sub);
    m.sub = r.tornSub;
    for (int j = 0; j < IJ_N; j++)
        if (b->cut & (1u << j)) { m.sub.push_back(r.cap[j][0]); m.sub.push_back(r.cap[j][1]); }
    {
        const M3Mat& mt = m.mat[r.cap[0][0].mat];
        m3_upload_tex(m.tex[mt.base]); m3_upload_tex(m.tex[mt.norm]); m3_upload_tex(m.tex[mt.orm]);
    }
    g_irTornDraws++;
}

static void infdeath_torn_end(int mi)
{
    M3Model& m = g_m3[mi];
    if (!g_irSubSave.empty()) { m.sub.swap(g_irSubSave); g_irSubSave.clear(); }
}

/* How much of his own colour a dying man keeps: all of it as the clip begins, falling to
   the panel's share (half, by default) over the second half of his fall; a corpse the
   engine has let go of keeps the share. Bodies left on the ground then read as part of it
   rather than as men still in the fight. */
static float infdeath_tint(const SimObject& o, const AnimState& a, bool corpse)
{
    if (!g_fx.enabled) return 1.0f;
    const float keep = g_fx.einf_corpse_dark;
    if (corpse) return keep;
    const int ticks = m3inf_slot_ticks(o, M3INF_DIE, a.variant);
    if (ticks <= 0) return 1.0f;
    float t = ((float)(g_engineFrame - a.since) + g_tickAlpha) / (float)ticks;
    t = (t - 0.5f) * 2.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    t = t * t * (3.0f - 2.0f * t);
    return 1.0f - (1.0f - keep) * t;
}

/* ---- shots: the flash at the muzzle and the casings ---------------------------------------- */
struct IrCasing { float p[3], v[3], a[3], w[3]; int born; int rest; };
static std::vector<IrCasing> g_irCasings;
static std::map<int, int> g_irLastStage;          /* heap id -> last firing stage seen */
static int g_irFlashes = 0, g_irCasingsMade = 0;
static const int IR_CASING_LIFE = 45;             /* 3 s at the engine's 15 ticks */
static const size_t IR_CASING_MAX = 600;

static void infdeath_shot_tick(const SimObject& o, const AnimState& a)
{
    if (!g_fx.enabled || o.kind != K_INFANTRY) return;
    {
        IrSeen& sv = g_irSeen[o.id];
        sv.o = o; sv.a = a; sv.frame = g_engineFrame;
    }
    if (o.dying) return;
    const bool standing = a.slot == M3INF_SHOOT && o.doing == 4;
    const bool prone = a.slot == M3INF_FIREPRONE && o.doing == 8;
    if (!standing && !prone) { g_irLastStage.erase(o.id); return; }
    const int mi = m3inf_model_any(o);
    if (mi < 0) return;
    const InfRig& r = ir_rig(mi);
    if (r.muzzle < 0) return;
    const int n = standing ? r.shootN : r.proneN;
    const unsigned at = standing ? r.shootAt : r.proneAt;
    if (n <= 0 || !at) return;
    const int stage = (o.dostage > 0 ? o.dostage : 0) % n;
    std::map<int, int>::iterator ls = g_irLastStage.find(o.id);
    const int last = ls != g_irLastStage.end() ? ls->second : -1;
    g_irLastStage[o.id] = stage;
    if (stage == last || !(at & (1u << stage))) return;

    /* the weapon where the clip has it on this stage */
    const M3Model& m = g_m3[mi];
    const int ci = m3inf_slot_clip(m, standing ? M3INF_SHOOT : M3INF_FIREPRONE, 0);
    if (ci < 0) return;
    std::vector<float> pose(m.bone.size() * 8);
    m3_sample(m, ci, (float)stage / (float)n * (float)m.clip[ci].nframe, &pose[0]);
    InfRag b = InfRag();
    b.ax = o.wx; b.az = o.wz; b.ay = terrain_y(o.wx, o.wz) + o.ylift;
    b.yawDeg = a.yaw; b.S = MODEL_SCALE * m3inf_scale(m);
    float j[3], pm[3], mw[3];
    ir_joint(m, r.muzzle, j);
    ir_carry(m, &pose[0], r.muzzle, j, pm);
    ir_to_world(b, pm, mw);
    if (g_efxEnhOn && g_fx.einf_flash) {
        efx_emit(EFX_ENH_EXTRA[0].src, mw[0], mw[1], mw[2], 1,
                 ir_hash((unsigned)o.id, 0xF1A5u), g_engineFrame, 0x71, stage);
        g_irFlashes++;
    }
    if (r.eject >= 0 && r.ejectdir >= 0 && g_fx.einf_casings) {
        float pe[3], pd[3], we[3], wd[3];
        ir_joint(m, r.eject, j);    ir_carry(m, &pose[0], r.eject, j, pe);
        ir_joint(m, r.ejectdir, j); ir_carry(m, &pose[0], r.ejectdir, j, pd);
        ir_to_world(b, pe, we);
        ir_to_world(b, pd, wd);
        float dir[3] = { wd[0] - we[0], wd[1] - we[1], wd[2] - we[2] };
        ir_norm3(dir);
        const unsigned h = ir_hash((unsigned)o.id * 31u + (unsigned)stage, (unsigned)g_engineFrame);
        IrCasing c;
        memcpy(c.p, we, sizeof we);
        const float sp = 0.9f + 0.4f * ir_r01(h);                 /* cells a second */
        c.v[0] = dir[0] * sp + (ir_r01(h >> 4) - 0.5f) * 0.25f;
        c.v[1] = dir[1] * sp + 0.55f + 0.3f * ir_r01(h >> 8);
        c.v[2] = dir[2] * sp + (ir_r01(h >> 12) - 0.5f) * 0.25f;
        for (int k = 0; k < 3; k++) {
            c.a[k] = ir_r01(h >> (k * 5 + 1)) * 6.28f;
            c.w[k] = (ir_r01(h >> (k * 3 + 16)) - 0.5f) * 40.0f;   /* radians a second */
        }
        c.born = g_engineFrame;
        c.rest = 0;
        if (g_irCasings.size() >= IR_CASING_MAX) g_irCasings.erase(g_irCasings.begin());
        g_irCasings.push_back(c);
        g_irCasingsMade++;
    }
}

static void infdeath_casings_step(void)
{
    const float dt = 1.0f / ((float)M3INF_TICKS_PER_SECOND * 4.0f);
    for (size_t i = 0; i < g_irCasings.size(); ) {
        IrCasing& c = g_irCasings[i];
        if (g_engineFrame - c.born > IR_CASING_LIFE || g_engineFrame < c.born) {
            g_irCasings.erase(g_irCasings.begin() + i);
            continue;
        }
        for (int s = 0; s < 4 && !c.rest; s++) {
            c.v[1] -= 4.5f * dt;
            for (int k = 0; k < 3; k++) { c.p[k] += c.v[k] * dt; c.a[k] += c.w[k] * dt; }
            const float gy = terrain_y(c.p[0], c.p[2]) + 0.003f;
            if (c.p[1] < gy) {
                c.p[1] = gy;
                if (c.v[1] < 0.0f) c.v[1] = -c.v[1] * 0.35f;
                c.v[0] *= 0.55f; c.v[2] *= 0.55f;
                for (int k = 0; k < 3; k++) c.w[k] *= 0.5f;
                if (c.v[1] < 0.08f && c.v[0] * c.v[0] + c.v[2] * c.v[2] < 0.004f) {
                    c.rest = 1;
                    c.a[0] = 1.5708f; c.a[2] = 0.0f;        /* lying on its side */
                }
            }
        }
        i++;
    }
}

/* ---- drawing ------------------------------------------------------------------------- */
/* THE BLOOD, dark red PAINTED over the ground through its soft-edged coverage (alpha
   blend), with the ground's own light under it: a multiply toward dark red (the tracks'
   way) came out near black on the dark temperate grass and read as a shadow, not blood.
   The splatter is generated, four variants: a pool of overlapping blots and a spray of
   drops, a little darker and glossier at its heart. It spreads over the first two
   seconds. 128 texels a variant, mipmapped: at 64 the pool's edge and drops were soft
   blurs at the near camera. */
static GLuint g_irBloodTex = 0;
static const int IR_BT = 128;

static void ir_build_blood(void)
{
    if (g_irBloodTex) return;
    std::vector<unsigned char> px((size_t)IR_BT * 4 * IR_BT * 4, 0);
    for (int v = 0; v < 4; v++) {
        std::vector<float> a((size_t)IR_BT * IR_BT, 0.0f);
        /* blots: discs of varying size near the centre */
        for (int k = 0; k < 9; k++) {
            const unsigned h = ir_hash(0xB100Du + (unsigned)v, (unsigned)k);
            const float cx = 0.5f + (ir_r01(h) - 0.5f) * 0.36f, cz = 0.5f + (ir_r01(h >> 16) - 0.5f) * 0.36f;
            const float rr = 0.10f + 0.14f * ir_r01(h >> 8);
            for (int y = 0; y < IR_BT; y++)
                for (int x = 0; x < IR_BT; x++) {
                    const float dx = (x + 0.5f) / IR_BT - cx, dz = (y + 0.5f) / IR_BT - cz;
                    const float d = sqrtf(dx * dx + dz * dz) / rr;
                    const float e = d < 0.75f ? 1.0f : (d < 1.0f ? (1.0f - d) / 0.25f : 0.0f);
                    float& A = a[(size_t)y * IR_BT + x];
                    if (e > A) A = e;
                }
        }
        /* drops: small discs flung outward */
        for (int k = 0; k < 18; k++) {
            const unsigned h = ir_hash(0xD20Bu + (unsigned)v * 97u, (unsigned)k);
            const float ang = ir_r01(h) * 6.2831853f, rad = 0.28f + 0.18f * ir_r01(h >> 8);
            const float cx = 0.5f + cosf(ang) * rad, cz = 0.5f + sinf(ang) * rad;
            const float rr = 0.012f + 0.022f * ir_r01(h >> 16);
            for (int y = 0; y < IR_BT; y++)
                for (int x = 0; x < IR_BT; x++) {
                    const float dx = (x + 0.5f) / IR_BT - cx, dz = (y + 0.5f) / IR_BT - cz;
                    const float d = sqrtf(dx * dx + dz * dz) / rr;
                    if (d < 1.0f) {
                        float& A = a[(size_t)y * IR_BT + x];
                        const float e = d < 0.6f ? 0.9f : 0.9f * (1.0f - d) / 0.4f;
                        if (e > A) A = e;
                    }
                }
        }
        /* a little grain, darker at the heart of the pool, transparent at the border */
        for (int y = 0; y < IR_BT; y++)
            for (int x = 0; x < IR_BT; x++) {
                float A = a[(size_t)y * IR_BT + x];
                const float dx = (x + 0.5f) / IR_BT - 0.5f, dz = (y + 0.5f) / IR_BT - 0.5f;
                if (dx * dx + dz * dz > 0.24f) A = 0.0f;
                A *= 0.78f + 0.22f * ir_r01(ir_hash((unsigned)(x * 131 + y), (unsigned)v + 7u));
                /* dark red, darker where it pools deepest; coverage in alpha */
                unsigned char* q = &px[((size_t)y * IR_BT * 4 + (size_t)v * IR_BT + x) * 4];
                const float deep = A > 0.8f ? (A - 0.8f) / 0.2f : 0.0f;
                q[0] = (unsigned char)(255.0f * (0.46f - 0.16f * deep));
                q[1] = (unsigned char)(255.0f * (0.035f - 0.015f * deep));
                q[2] = (unsigned char)(255.0f * (0.03f - 0.012f * deep));
                q[3] = (unsigned char)(255.0f * (A > 1.0f ? 1.0f : A) * 0.88f);
            }
    }
    glGenTextures(1, &g_irBloodTex);
    glBindTexture(GL_TEXTURE_2D, g_irBloodTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, 0x8191 /* GL_GENERATE_MIPMAP */, GL_TRUE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, IR_BT * 4, IR_BT, 0, GL_RGBA, GL_UNSIGNED_BYTE, &px[0]);
    glTexParameteri(GL_TEXTURE_2D, 0x8191 /* GL_GENERATE_MIPMAP */, GL_FALSE);
}

static void infdeath_draw_blood(void)
{
    g_irDrawnBlood = 0;
    if (!g_fx.enabled || (!g_fx.einf_blood && g_irSplats.empty()) || (g_irBody.empty() && g_irSplats.empty())) return;
    ir_build_blood();
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_irBloodTex);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -1.0f);
    glDepthMask(GL_FALSE);
    const float now = engine_time();
    const int N = 5;                                  /* a 5 x 5 grid laid on the ground */
    for (std::map<long long, InfRag>::const_iterator it = g_irBody.begin(); it != g_irBody.end(); ++it) {
        const InfRag& b = it->second;
        if (b.bloodBorn < 0 || !g_fx.einf_blood) continue;
        float grow = (now - (float)b.bloodBorn) / 30.0f;
        grow = grow < 0.0f ? 0.0f : (grow > 1.0f ? 1.0f : grow);
        grow = 0.35f + 0.65f * (1.0f - (1.0f - grow) * (1.0f - grow));
        const float half = b.bsize * 0.5f * grow;
        const float cr = cosf(b.brot), sr = sinf(b.brot);
        const float u0 = (float)b.bvar * 0.25f;
        /* Shared grid points keep exactly the original arithmetic, once rather than
           four times per tile. Its complete posed bounds are valid for this pass. */
        float points[6][6][3],lo[3]={1e30f,1e30f,1e30f},hi[3]={-1e30f,-1e30f,-1e30f};
        for(int j=0;j<=N;j++)for(int i=0;i<=N;i++) {
            const float s=(float)i/N*2.0f-1.0f,t=(float)j/N*2.0f-1.0f;
            float* p=points[j][i];p[0]=b.bx+(cr*s-sr*t)*half;p[2]=b.bz+(sr*s+cr*t)*half;
            p[1]=terrain_y(p[0],p[2])+0.002f;
            for(int q=0;q<3;q++){lo[q]=std::min(lo[q],p[q]);hi[q]=std::max(hi[q],p[q]);}
        }
        const bool outside=mesh_perf_switch("OPENCNC3D_EFFECT_CULL")&&mesh_box_outside(lo,hi);
        /* the ground's own light and fog of war on it, as the scorch marks take theirs */
        {
            const float sh = terrain_shade((int)floorf(b.bx + 0.5f), (int)floorf(b.bz + 0.5f))
                           * (g_objShroud ? 1.0f - shroud_env_at(b.bx, b.bz) : 1.0f);
            glColor4f(sh, sh, sh, 1.0f);
        }
        if(outside) {
            const float s=(float)(N-1)/N*2.0f-1.0f;
            glTexCoord2f(u0+(s*0.5f+0.5f)*0.25f,1.0f);continue;
        }
        glBegin(GL_TRIANGLES);
        for (int j = 0; j < N; j++)
            for (int i = 0; i < N; i++) {
                float X[4], Y[4], Z[4], U[4], Vv[4];
                const int ci[4] = { i, i + 1, i + 1, i }, cj[4] = { j, j, j + 1, j + 1 };
                for (int k = 0; k < 4; k++) {
                    const float s = (float)ci[k] / N * 2.0f - 1.0f, t = (float)cj[k] / N * 2.0f - 1.0f;
                    const float* p=points[cj[k]][ci[k]];
                    X[k]=p[0];Y[k]=p[1];Z[k]=p[2];
                    U[k] = u0 + (s * 0.5f + 0.5f) * 0.25f;
                    Vv[k] = t * 0.5f + 0.5f;
                }
                const int tri[6] = { 0, 1, 2, 0, 2, 3 };
                for (int k = 0; k < 6; k++) {
                    glTexCoord2f(U[tri[k]], Vv[tri[k]]);
                    glVertex3f(X[tri[k]], Y[tri[k]], Z[tri[k]]);
                }
            }
        glEnd();
        g_irDrawnBlood++;
    }
    /* THE SPRAY'S DROPS ON THE GROUND: one small stretched quad each, the same blood,
       grown to full size over their first ticks */
    if (!g_irSplats.empty()) {
        glBegin(GL_TRIANGLES);
        for (size_t i = 0; i < g_irSplats.size(); i++) {
            const IrSplat& sp = g_irSplats[i];
            if (g_viewValid && (sp.x < g_viewX0 - 1.0f || sp.x > g_viewX1 + 1.0f
                                || sp.z < g_viewZ0 - 1.0f || sp.z > g_viewZ1 + 1.0f)) continue;
            float grow = (now - (float)sp.born) / 3.0f;
            grow = grow < 0.0f ? 0.0f : (grow > 1.0f ? 1.0f : grow);
            const float hw = sp.size * 0.5f * (0.5f + 0.5f * grow);
            const float hl = hw * sp.stretch;
            const float cr = cosf(sp.rot), sr = sinf(sp.rot);
            const float sh = terrain_shade((int)floorf(sp.x + 0.5f), (int)floorf(sp.z + 0.5f))
                           * (g_objShroud ? 1.0f - shroud_env_at(sp.x, sp.z) : 1.0f);
            glColor4f(sh, sh, sh, 1.0f);
            float X[4], Y[4], Z[4];
            const float cs[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
            for (int k = 0; k < 4; k++) {
                const float a = cs[k][0] * hl, c = cs[k][1] * hw;
                X[k] = sp.x + cr * a - sr * c;
                Z[k] = sp.z + sr * a + cr * c;
                Y[k] = terrain_y(X[k], Z[k]) + 0.003f;
            }
            const float u0 = (float)sp.var * 0.25f;
            const float U[4] = { u0 + 0.03f, u0 + 0.22f, u0 + 0.22f, u0 + 0.03f };
            const float V[4] = { 0.12f, 0.12f, 0.88f, 0.88f };
            const int tri[6] = { 0, 1, 2, 0, 2, 3 };
            for (int k = 0; k < 6; k++) {
                glTexCoord2f(U[tri[k]], V[tri[k]]);
                glVertex3f(X[tri[k]], Y[tri[k]], Z[tri[k]]);
            }
        }
        glEnd();
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
}

/* THE CASINGS: small brass cylinders drawn as boxes, each face shaded by which way it
   faces, faded out over their last half second. */
static void infdeath_draw_casings(void)
{
    if (!g_fx.enabled || g_irCasings.empty()) return;
    const float now = engine_time();
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const float L = 0.022f, R = 0.0055f;             /* cells: legible at the closest zoom */
    glBegin(GL_QUADS);
    for (size_t i = 0; i < g_irCasings.size(); i++) {
        const IrCasing& c = g_irCasings[i];
        const float age = now - (float)c.born;
        float al = (float)IR_CASING_LIFE - age;
        al = al / 7.5f;                                /* the last half second */
        al = al < 0.0f ? 0.0f : (al > 1.0f ? 1.0f : al);
        if (al <= 0.0f) continue;
        /* the casing's axes from its three tumble angles */
        const float ca = cosf(c.a[0]), sa = sinf(c.a[0]), cb = cosf(c.a[1]), sb = sinf(c.a[1]);
        const float cc = cosf(c.a[2]), sc = sinf(c.a[2]);
        float ax[3][3] = {
            { cb * cc, cb * sc, -sb },
            { sa * sb * cc - ca * sc, sa * sb * sc + ca * cc, sa * cb },
            { ca * sb * cc + sa * sc, ca * sb * sc - sa * cc, ca * cb } };
        const float hx[3] = { L * 0.5f, R, R };
        static const int F[6][4] = { {0,1,3,2}, {4,6,7,5}, {0,4,5,1}, {2,3,7,6}, {0,2,6,4}, {1,5,7,3} };
        float corner[8][3];
        for (int k = 0; k < 8; k++)
            for (int d = 0; d < 3; d++)
                corner[k][d] = c.p[d] + ax[0][d] * hx[0] * ((k & 1) ? 1 : -1)
                                      + ax[1][d] * hx[1] * ((k & 2) ? 1 : -1)
                                      + ax[2][d] * hx[2] * ((k & 4) ? 1 : -1);
        for (int f = 0; f < 6; f++) {
            float e1[3], e2[3], n[3];
            for (int d = 0; d < 3; d++) {
                e1[d] = corner[F[f][1]][d] - corner[F[f][0]][d];
                e2[d] = corner[F[f][3]][d] - corner[F[f][0]][d];
            }
            n[0] = e1[1] * e2[2] - e1[2] * e2[1]; n[1] = e1[2] * e2[0] - e1[0] * e2[2]; n[2] = e1[0] * e2[1] - e1[1] * e2[0];
            ir_norm3(n);
            const float lit = 0.55f + 0.45f * (n[1] > 0.0f ? n[1] : -0.3f * n[1]);
            glColor4f(0.86f * lit, 0.66f * lit, 0.26f * lit, al);
            for (int k = 0; k < 4; k++) glVertex3fv(corner[F[f][k]]);
        }
    }
    glEnd();
    glDisable(GL_BLEND);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

/* THE SPRAY IN THE AIR: each drop a short streak along its own motion. */
static void ir_draw_drops(void)
{
    if (g_irDrops.empty()) return;
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    for (size_t i = 0; i < g_irDrops.size(); i++) {
        const IrDrop& d = g_irDrops[i];
        const float sh = terrain_shade((int)floorf(d.p[0] + 0.5f), (int)floorf(d.p[2] + 0.5f))
                       * (g_objShroud ? 1.0f - shroud_env_at(d.p[0], d.p[2]) : 1.0f);
        glColor4f(0.50f * sh, 0.03f * sh, 0.03f * sh, 0.9f);
        glVertex3f(d.p[0], d.p[1], d.p[2]);
        glVertex3f(d.p[0] - d.v[0] * 0.035f, d.p[1] - d.v[1] * 0.035f, d.p[2] - d.v[2] * 0.035f);
    }
    glEnd();
    glLineWidth(1.0f);
    glDisable(GL_BLEND);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

/* ---- splats on vehicles and buildings --------------------------------------------------
   PROJECTED THROUGH THE DEPTH BUFFER. A drop that lands on a hull is not laid on a mesh
   this module knows the triangles of: every vehicle and building is drawn by its own
   system. So the splat is a small box at the place the drop struck, long along the drop's
   path, and a fragment shader reads the scene's depth under each pixel of the box, turns
   it back into a point in the world, and paints the blood where that point falls inside
   the box: on whatever surface the hull really has there, at any angle. The depth is
   copied once a frame, only when there is a splat to draw. Enhanced only, like
   everything in this file; the shader is GLSL 1.20. */
static GLuint g_irDecalProg = 0, g_irDepthTex = 0;
static int g_irDecalTried = 0, g_irDepthW = 0, g_irDepthH = 0, g_irObjDrawn = 0;
static const char* IR_DECAL_VS =
    "#version 120\n"
    "void main() { gl_Position = ftransform(); }\n";
static const char* IR_DECAL_FS =
    "#version 120\n"
    "uniform sampler2D uDepth;\n"
    "uniform sampler2D uTex;\n"
    "uniform mat4 uInvVP;\n"
    "uniform vec4 uVP;\n"
    "uniform vec3 uC, uX, uY, uZ;\n"      /* the box: centre and three axes over their half-length squared */
    "uniform vec2 uUV;\n"                 /* the atlas: first u, width */
    "uniform float uShade;\n"
    "void main() {\n"
    "    vec2 s = (gl_FragCoord.xy - uVP.xy) / uVP.zw;\n"
    "    float d = texture2D(uDepth, s).r;\n"
    "    vec4 w = uInvVP * vec4(s * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);\n"
    "    vec3 p = w.xyz / w.w - uC;\n"
    "    vec3 l = vec3(dot(p, uX), dot(p, uY), dot(p, uZ));\n"
    "    if (abs(l.x) > 1.0 || abs(l.y) > 1.0 || abs(l.z) > 1.0) discard;\n"
    "    vec4 t = texture2D(uTex, vec2(uUV.x + (l.x * 0.5 + 0.5) * uUV.y, l.y * 0.5 + 0.5));\n"
    "    float edge = 1.0 - smoothstep(0.6, 1.0, abs(l.z));\n"
    "    gl_FragColor = vec4(t.rgb * uShade, t.a * edge);\n"
    "}\n";

static GLuint ir_decal_prog(void)
{
    if (g_irDecalTried) return g_irDecalProg;
    g_irDecalTried = 1;
    if (!fx_gl_ready) return 0;
    GLuint vs = fx_compile(GL_VERTEX_SHADER, IR_DECAL_VS, "blood splat");
    if (!vs) return 0;
    GLuint fs = fx_compile(GL_FRAGMENT_SHADER, IR_DECAL_FS, "blood splat");
    if (!fs) { fx_glDeleteShader(vs); return 0; }
    GLuint p = fx_glCreateProgram();
    fx_glAttachShader(p, vs);
    fx_glAttachShader(p, fs);
    fx_glLinkProgram(p);
    GLint ok = 0;
    fx_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    fx_glDeleteShader(vs);
    fx_glDeleteShader(fs);
    if (!ok) {
        fprintf(stderr, "INFDEATH|program|blood splat FAILED TO LINK\n");
        fx_glDeleteProgram(p);
        return 0;
    }
    g_irDecalProg = p;
    return p;
}

/* a 4x4 inverse (column major), by cofactors */
static bool ir_inv4(const float* m, float* o)
{
    float inv[16];
    inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (fabsf(det) < 1e-20f) return false;
    det = 1.0f / det;
    for (int i = 0; i < 16; i++) o[i] = inv[i] * det;
    return true;
}

static void ir_draw_objsplats(void)
{
    g_irObjDrawn = 0;
    if (g_irObjSplats.empty()) return;
    const GLuint prog = ir_decal_prog();
    if (!prog) return;
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    if (vp[2] <= 0 || vp[3] <= 0) return;
    struct IrSplatDraw {size_t index;float c[3],d[3],ax[3],ay[3],C[3],V[8][3];};
    std::vector<IrSplatDraw> prepared;
    std::map<long long, size_t> at;
    for (size_t i = 0; i < g_objects.size(); i++)
        at[(long long)g_objects[i].kind * 1000000 + g_objects[i].id] = i;
    for (size_t i = 0; i < g_irObjSplats.size(); i++) {
        const IrObjSplat& sp = g_irObjSplats[i];
        std::map<long long, size_t>::const_iterator f = at.find((long long)sp.kind * 1000000 + sp.id);
        if (f == at.end()) continue;
        const SimObject& o = g_objects[f->second];
        if (!visible(o)) continue;
        IrSplatDraw draw;draw.index=i;
        float* c=draw.c;float* d=draw.d;
        if (sp.kind == K_UNIT) {
            float fw[2], rt[2];
            ir_axes_of(o, fw, rt);
            const float gy = terrain_y(o.wx, o.wz);
            c[0] = o.wx + rt[0] * sp.l[0] + fw[0] * sp.l[2];
            c[2] = o.wz + rt[1] * sp.l[0] + fw[1] * sp.l[2];
            c[1] = gy + sp.l[1];
            d[0] = rt[0] * sp.d[0] + fw[0] * sp.d[2];
            d[2] = rt[1] * sp.d[0] + fw[1] * sp.d[2];
            d[1] = sp.d[1];
        } else {
            memcpy(c, sp.l, 3*sizeof(float)); memcpy(d, sp.d, 3*sizeof(float));
        }
        ir_norm3(d);
        /* the box: across the drop's path the splat's size, along it reaching in */
        float* ax=draw.ax;float* ay=draw.ay;
        {
            const float ref[3] = { fabsf(d[1]) < 0.9f ? 0.0f : 1.0f, fabsf(d[1]) < 0.9f ? 1.0f : 0.0f, 0.0f };
            ax[0] = d[1] * ref[2] - d[2] * ref[1]; ax[1] = d[2] * ref[0] - d[0] * ref[2]; ax[2] = d[0] * ref[1] - d[1] * ref[0];
            ir_norm3(ax);
            const float cr = cosf(sp.rot), sr = sinf(sp.rot);
            float by[3] = { d[1] * ax[2] - d[2] * ax[1], d[2] * ax[0] - d[0] * ax[2], d[0] * ax[1] - d[1] * ax[0] };
            for (int k = 0; k < 3; k++) { const float x = ax[k]; ax[k] = cr * x + sr * by[k]; ay[k] = -sr * x + cr * by[k]; }
        }
        const float hx = sp.size * 0.5f, hz = 0.16f;
        float* C=draw.C;
        for (int k = 0; k < 3; k++) C[k] = c[k] + d[k] * 0.08f;
        auto& V=draw.V;
        for (int k = 0; k < 8; k++)
            for (int q = 0; q < 3; q++)
                V[k][q] = C[q] + ax[q] * hx * ((k & 1) ? 1 : -1) + ay[q] * hx * ((k & 2) ? 1 : -1)
                               + d[q] * hz * ((k & 4) ? 1 : -1);
        float lo[3],hi[3];memcpy(lo,V[0],sizeof lo);memcpy(hi,V[0],sizeof hi);
        for(int k=1;k<8;k++)for(int q=0;q<3;q++){lo[q]=std::min(lo[q],V[k][q]);hi[q]=std::max(hi[q],V[k][q]);}
        if(!mesh_perf_switch("OPENCNC3D_EFFECT_CULL")||!mesh_box_outside(lo,hi))prepared.push_back(draw);
    }
    if(prepared.empty()) {
        /* Match the original empty submission's inherited state without copying depth. */
        glCullFace(GL_BACK);glDisable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);glDisable(GL_BLEND);fx_glUseProgram(0);
        glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,0);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,0);return;
    }
    /* the scene's depth as it stands, copied */
    if (!g_irDepthTex || g_irDepthW != vp[2] || g_irDepthH != vp[3]) {
        if (!g_irDepthTex) glGenTextures(1, &g_irDepthTex);
        glBindTexture(GL_TEXTURE_2D, g_irDepthTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, vp[2], vp[3], 0,
                     GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
        g_irDepthW = vp[2]; g_irDepthH = vp[3];
    }
    glBindTexture(GL_TEXTURE_2D, g_irDepthTex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], vp[2], vp[3]);
    float P[16], MV[16], VPm[16], inv[16];
    glGetFloatv(GL_PROJECTION_MATRIX, P);
    glGetFloatv(GL_MODELVIEW_MATRIX, MV);
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) sum += P[k * 4 + r] * MV[c * 4 + k];
            VPm[c * 4 + r] = sum;
        }
    if (!ir_inv4(VPm, inv)) return;
    ir_build_blood();
    fx_glUseProgram(prog);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_irDepthTex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_irBloodTex);
    fx_set1i(prog, "uDepth", 1);
    fx_set1i(prog, "uTex", 0);
    fx_setmat(prog, "uInvVP", inv);
    {
        GLint l = fx_glGetUniformLocation(prog, "uVP");
        if (l >= 0) fx_glUniform4f(l, (float)vp[0], (float)vp[1], (float)vp[2], (float)vp[3]);
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);                 /* the box's far faces: each pixel once */
    for(const auto& draw:prepared) {
        const IrObjSplat& sp=g_irObjSplats[draw.index];
        const float* c=draw.c;const float* d=draw.d;const float* C=draw.C;
        const float* ax=draw.ax;const float* ay=draw.ay;
        const float hx=sp.size*0.5f,hz=0.16f;
        const auto& V=draw.V;
        fx_set3f(prog, "uC", C[0], C[1], C[2]);
        fx_set3f(prog, "uX", ax[0] / hx, ax[1] / hx, ax[2] / hx);
        fx_set3f(prog, "uY", ay[0] / hx, ay[1] / hx, ay[2] / hx);
        fx_set3f(prog, "uZ", d[0] / hz, d[1] / hz, d[2] / hz);
        {
            GLint l = fx_glGetUniformLocation(prog, "uUV");
            if (l >= 0) fx_glUniform2f(l, (float)sp.var * 0.25f, 0.25f);
        }
        const float sh = terrain_shade((int)floorf(c[0] + 0.5f), (int)floorf(c[2] + 0.5f))
                       * (g_objShroud ? 1.0f - shroud_env_at(c[0], c[2]) : 1.0f);
        fx_set1f(prog, "uShade", sh);
        static const int F[6][4] = { {0,2,3,1}, {4,5,7,6}, {0,1,5,4}, {2,6,7,3}, {0,4,6,2}, {1,3,7,5} };
        glBegin(GL_QUADS);
        for (int f2 = 0; f2 < 6; f2++)
            for (int k = 0; k < 4; k++) glVertex3fv(V[F[f2][k]]);
        glEnd();
        g_irObjDrawn++;
    }
    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    fx_glUseProgram(0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
}

/* the spray in the air and its splats on hulls, after every opaque thing is drawn */
static void infdeath_draw_spray(void)
{
    if (!g_fx.enabled || g_m3Mirrored) return;
    ir_draw_drops();
    ir_draw_objsplats();
}

/* The engine clock does not restart reliably between missions. Explicit teardown
   owns heap-id maps and blood/physics queues instead of a backwards-clock guess. */
static void infdeath_reset(bool closeModels=true)
{
    g_irBody.clear(); g_irPrevPos.clear(); g_irSeen.clear(); g_irCrushed.clear();
    g_irDrops.clear(); g_irSplats.clear(); g_irObjSplats.clear(); g_irCasings.clear();
    g_irLastStage.clear(); g_irSubSave.clear();
    /* Rig construction extends model geometry; keep its indices across a save
       load of the same pack, and retire them only with the models themselves. */
    if(closeModels)g_irRig.clear();
    g_irPushGrid.clear();g_irPushIds.clear();
    g_irLastFrame=-1; g_irDrawnBlood=g_irRagLive=g_irPushes=0;
    g_irCuts=g_irBlasts=g_irCrushes=g_irRunOver=g_irTornDraws=0;
    g_irDropsMade=g_irSplatGround=g_irSplatObj=g_irFlashes=g_irCasingsMade=g_irObjDrawn=0;
    if (g_irBloodTex) glDeleteTextures(1,&g_irBloodTex);
    if (g_irDepthTex) glDeleteTextures(1,&g_irDepthTex);
    if (g_irDecalProg && fx_glDeleteProgram) fx_glDeleteProgram(g_irDecalProg);
    g_irBloodTex=g_irDepthTex=g_irDecalProg=0;
    g_irDepthW=g_irDepthH=g_irDecalTried=0;
}

static void infdeath_log(void)
{
    int rag = 0, sleep = 0, blood = 0;
    /* the deepest any ragdoll point sits below its own radius off the ground, cells */
    float lowest = 0.0f;
    for (std::map<long long, InfRag>::const_iterator it = g_irBody.begin(); it != g_irBody.end(); ++it) {
        const InfRag& b = it->second;
        rag += b.state == IRS_RAG;
        sleep += b.state == IRS_ASLEEP;
        blood += b.bloodBorn >= 0;
        if (b.state == IRS_ANIM) continue;
        const float hc = ir_rig(b.mi).heightU * b.S;
        for (int i = 0; i < IR_GRIP; i++) {
            const float d = b.p[i][1] - (ir_ground(b.p[i][0], b.p[i][2]) + IR_RADIUS[i] * hc);
            if (d < lowest) lowest = d;
        }
    }
    int torn = 0, pieces = 0;
    for (std::map<long long, InfRag>::const_iterator it = g_irBody.begin(); it != g_irBody.end(); ++it) {
        if (!it->second.cut) continue;
        int comp[IG_N];
        torn++;
        pieces += ir_components(it->second.cut, comp);
    }
    printf("INFDEATH|corpses=%d|bodies=%d|ragdoll=%d|asleep=%d|blood=%d|pushes=%d|flashes=%d|casings_made=%d|casings_live=%d|lowest=%.4f\n",
           (int)g_m3Corpses.size(), (int)g_irBody.size(), rag, sleep, blood, g_irPushes, g_irFlashes,
           g_irCasingsMade, (int)g_irCasings.size(), lowest);
    printf("INFGIBS|blasts=%d|crushes=%d|runover=%d|cuts=%d|torn=%d|pieces=%d|drops_made=%d|drops_live=%d|splats_ground=%d|splats_obj=%d|objsplats=%d|objdrawn=%d|torndraws=%d\n",
           g_irBlasts, g_irCrushes, g_irRunOver, g_irCuts, torn, pieces, g_irDropsMade, (int)g_irDrops.size(),
           (int)g_irSplats.size(), g_irSplatObj, (int)g_irObjSplats.size(), g_irObjDrawn, g_irTornDraws);
}

#endif
