/* ================================================================================
 * THE PLACEMENT (eplc) -- a remastered structure going up part by part, under Enhanced,
 * as the remaster viewer's Place button plays it.
 *
 * WHAT IT IS. A structure put down in Enhanced, of a type with a remastered model
 * (ebld_mod.h) and a baked eplc/<TYPE>.eplc (tools/art/remaster/bake_eplc.py), does not
 * show the cartridge's section-by-section reveal. Instead:
 *
 *   1. the slab rises out of the ground on its own, with the ground breaking round its
 *      foot: a burst of dust and a rolling cloud along its outline, clods thrown up and
 *      falling back, and a ring of broken earth that stays a while after;
 *   2. the parts of the building come up through the slab in overlapping waves, each as
 *      its purpose suggests: walls and halls rise, roofs and plates slide home and latch,
 *      stacks telescope while turning slowly, ducts and panels hinge open, pipes and braces
 *      grow along their routes, doors close, lamps come last. Every part rising through
 *      the slab streams dust from its outline there for as long as it moves through it;
 *   3. the camera gives the game's one placement jolt as the building is put down, stays
 *      still while the slab comes up, then rumbles as the main structure rises, strongest
 *      as it arrives, dying away a third of a second after (the player's own buildings
 *      only, as the jolt always was);
 *   4. at the end every part is at rest, which is exactly the model the remastered
 *      building draws from then on: its turning parts carry on from where the placement
 *      left them (a fan spun up to its idle speed meets the idle turn without a jump), its
 *      flipbooks from the frame they were held at, and its idle effects start then, not
 *      before.
 *
 * THE DATA. The baked file carries the lab's ACTORS: sets of triangles that move as one,
 * each riding a parent, with their motions (slide, hinge, extend, spin up, shake, scale,
 * and from version 2 morph: every vertex eased from an offset pose back to rest, a hand
 * closing round what it catches), the planes they are cut by while they play (the ground,
 * the slab's top, the surface they come out of), the outline they raise dust from (from
 * version 2 at another surface of the slab than its top where a part rises through one,
 * and with more dust than its outline gives where it says so) and the shared preset (place.json:
 * the slab's dust, the ring, the camera). It names NO triangle of the building's own
 * file: it carries, per triangle of the lab's model, the actor and where the triangle
 * lies. At every pack load the remastered building's intact pack mesh is walked and each
 * of its triangles is found again by its centre and facing (eplc_attach), so the
 * placement draws whatever that file holds, at whatever texture size or lighting, as long
 * as it is the same geometry. A file whose triangles stop matching is refused for the
 * type, and the cartridge's reveal draws it, rather than anything being moved wrongly.
 *
 * THE CLOCK. The placement plays in the lab's own seconds, fifteen engine ticks to the
 * second, from the tick the engine first reports the building in its build-up state
 * (BSTATE_CONSTRUCTION). The engine's build-up lasts three ticks a frame of the building's
 * MAKE animation, which for most structures is within a few ticks of the placement's own
 * length (the Power Plant's is 58 ticks against 58.2). The approved timings are kept rather
 * than stretched to the engine's: a placement longer than the build-up carries on past it
 * (the building already works in the engine), and one shorter holds at rest for what is
 * left. A hit while it plays does not stop it: the 1995 engine's build-up runs on when the
 * building is damaged (BuildingClass::Take_Damage never touches the build state), so the
 * placement plays to its end and the damaged stage shows after it. Only the building's
 * destruction ends a placement early.
 *
 * SELLING plays the placement backwards on the engine's own deconstruction clock (the
 * build-up fraction of a building being sold runs from 1 to 0), so the parts fold away and
 * sink through the slab, the slab last, and the building is gone exactly when the engine
 * removes it; the emergence dust streams as the parts pass back down through the slab, and
 * nothing else: no camera, no clods, no ring. The Construction Yard's placement is its
 * deploy out of the MCV, and it too plays backwards: the yard folds back into the MCV,
 * which backs off the slab and is gone with the building, as the cartridge's own MCV deploy
 * rig is played backwards over a sold yard (the engine gives no MCV back for it).
 *
 * THE WALLS. A wall cell is one of sixteen connection pieces (ebld_mod.h); the file holds
 * sixteen rigs, one per piece, each the lab's own placement of a cell of that shape (the
 * sandbags' courses rising out of one another, the concrete wall's footing, pillar and
 * wall, the fence's kerb, posts, mesh and rail growing along the run), started per cell at
 * the moment the wall line's stagger (cnc_eyes.cpp, wall_rise_note_tick) gives it.
 *
 * THE DRAW is fixed function: a draw_mesh call per run of triangles, in the model's own
 * order, and user clip planes (glClipPlane, planes 1 to 5; plane 0 is the water
 * reflection's) for the cuts. A run whose motion is rigid is drawn at rest, kept on the card
 * as a standing building's pieces are, with its motion on the modelview stack
 * (eplc_draw_run below); every other run has its motion applied on the CPU through
 * draw_mesh's 3x4 extra matrix, as ebld_mod.h draws its pieces. A part that grows along a
 * path, or bends (morph), has its vertices moved on the CPU in a scratch copy of its
 * triangles.
 *
 * TIER 1. Enhanced does not exist on the Win98 build, so neither does this. Every motion
 * is a matrix or a vertex move and the dust ordinary particles; the clip planes have no
 * Voodoo 2 answer, and docs/tier1-gap.md lists what that build would lose.
 * ================================================================================ */
#ifndef EBLD_PLACE_H
#define EBLD_PLACE_H

#ifndef GL_CURRENT_PROGRAM
#define GL_CURRENT_PROGRAM 0x8B8D
#endif

/* ---- the file's vocabulary (tools/art/remaster/bake_eplc.py) ---------------------- */
enum { EPM_SLIDE = 0, EPM_HINGE, EPM_EXTEND, EPM_SPINUP, EPM_SHAKE, EPM_SCALE,
       EPM_MORPH /* version 2 */ };
enum { EPE_POWER = 0, EPE_INOUT, EPE_IN, EPE_LINEAR };
enum { EPMF_HOLD = 1, EPMF_OPEN = 2 };
enum { EPA_EMERGE = 1, EPA_PUFF = 2, EPA_SPLIT = 4, EPA_COMP = 8,
       /* version 2: an emergence through another surface than the slab's top, and more
          dust than an outline's span gives, on an emergence and on a puff */
       EPA_THROUGH = 16, EPA_EBOOST = 32, EPA_PBOOST = 64 };
/* the versions this reads: 1, and 2, which is 1 with the morph, `through` and `boost` */
enum { EPLC_VERSION_MIN = 1, EPLC_VERSION = 2 };
/* how near (pack units) a vertex of a morphed part must lie to a position of its morph to
   take that position's offset (the baker's MORPH_TOL; the bake's own check proves every
   vertex finds one at 0 error) */
static const float EPLC_MORPH_TOL = 0.25f;
/* SPLASH: the placement's impact, and its sale's, is Red Alert's water splash (VOC_SPLASH)
   in place of the cinematic placement impact: a building that stands in the sea */
enum { EPX_WALL = 1, EPX_COMP = 2, EPX_SPLASH = 4 };
/* the sheet a template's particles wear: the smoke sheet (every file before the field), the
   steam sheet, the glow specks */
enum { EPS_SMOKE = 0, EPS_STEAM, EPS_GLOW };
enum { EPT_SLAB = 0, EPT_EMERGE, EPT_PUFF };
enum { EPTF_BURST = 1, EPTF_GRAVITY = 2, EPTF_UNTIL = 4, EPTF_RAMP = 8, EPTF_DELAY = 16 };
/* an emitter template's fields, in the file's order */
enum { EPF_RATE = 0, EPF_LIFE0, EPF_LIFE1, EPF_SIZE0, EPF_SIZE1, EPF_OUT, EPF_UP, EPF_SPEED,
       EPF_VX, EPF_VY, EPF_VZ, EPF_JITTER, EPF_DRAG, EPF_RISE, EPF_GRAVITY, EPF_R, EPF_G,
       EPF_B, EPF_ALPHA, EPF_FADE, EPF_EDGE, EPF_SPACING, EPF_Z, EPF_DELAY, EPF_RAMP,
       EPF_UNTIL, EPF_UNTILFADE, EPF_EVERY0, EPF_EVERY1, EPF_COUNT0, EPF_COUNT1, EPF_N };

/* THE PUFFS. The lab gives a heavy part a short puff of settling dust where it lands
   (place_rm.py's `puff`), timed as it times them and sized as it sizes them: by the span of
   the puff's outline (its points' larger extent, worked out here as they are read, since the
   bake carries none), as an emerging part's dust is by its own. */
static const bool EPLC_PUFFS = true;

struct EplcMotion {
    int type, ease, flags;
    float delay, len, power, over, settle, a[12];
    std::vector<std::vector<float> > path;    /* EXTEND: polylines, xyz pack units */
    std::vector<float> morph;                 /* MORPH: per position, (x, y, z) at rest and
                                                 its offset at the start, pack units */
};
struct EplcClip { int in; float p[3], n[3]; };
struct EplcOutline {
    float y, edge, span, at, len;
    float through;                            /* EMERGE with THROUGH: the surface it rises
                                                 through, pack y in the slab's frame */
    float boost;                              /* EBOOST / PBOOST: the dust taken this many
                                                 times; 1 without */
    std::vector<float> pt;                    /* npt x (x, z, nx, nz), pack units */
};
struct EplcActor {
    char name[25];
    int parent, flags;
    float zmin, zmax;                         /* pack y, its extent at rest */
    std::vector<EplcMotion> mo;
    std::vector<EplcClip> clip;
    EplcOutline emerge, puff;
    int spinUp, extend, morph;                /* the motion of that type, -1 none */
};
struct EplcTriRec { int actor; float c[3], n[3]; };
struct EplcRig {
    float length, slabLen, slabTop, r0, r1;
    int slab;
    std::vector<EplcActor> actor;
    std::vector<EplcTriRec> tri;
};
struct EplcTmpl { int role, flags, sheet; float f[EPF_N]; };
/* ONE DRAW RUN: triangles [a, b) of the remastered building's intact mesh (grow < 0) or of
   this model's grow mesh, all of one actor, one moving part and one flipbook. */
struct EplcRun { int grow, a, b, actor, move, flip; };
/* A PART THAT GROWS along its paths (extend): its triangles copied into the grow mesh at
   [t0, t1), their rest positions, and where each vertex lies along its nearest path.
   A PART THAT BENDS (morph, version 2) is copied there the same way, and carries instead
   each vertex's offset at the motion's start (`off`). */
enum { EPG_EXTEND = 0, EPG_MORPH };
struct EplcSeg { float a[3], d[3], L, cum; };
struct EplcGrow {
    int kind, actor, motion, t0, t1;
    std::vector<float> rest;                  /* 9 floats a triangle */
    std::vector<float> at;                    /* per vertex: distance along its path */
    std::vector<int> path, seg;               /* per vertex */
    std::vector<std::vector<EplcSeg> > P;     /* per path, its segments */
    std::vector<float> plen;                  /* per path, its length */
    std::vector<float> off;                   /* MORPH: 9 floats a triangle */
    float k;                                  /* the growth written last, -1 none */
};
struct EplcModel {
    std::string type, path;
    int flags;
    float jolt, cam[5], emerge[3];
    std::vector<EplcTmpl> tmpl;
    float foot[4];                            /* band, scale, edge (cells), top */
    std::vector<float> hull;                  /* x, z pack, counter-clockwise from above */
    float ring[6], ringBox[4];
    int ringW, ringH;
    std::vector<unsigned char> ringPx;
    std::vector<EplcRig> rig;
    bool comp;
    std::string compModel;
    float compYaw, compShift[2];
    int compFlags;
    std::vector<EplcTriRec> compTri;
    /* THE ATTACHMENT, per pack load (eplc_attach) */
    int ebld;                                 /* the remastered model it moves, -1 */
    int evhc;                                 /* the companion's vehicle model, -1 */
    bool attached;
    int matched, unmatched;
    int morphed, unmorphed;                   /* vertices of morphed parts that found their
                                                 offset, and that did not */
    std::vector<std::vector<EplcRun> > run;   /* per rig */
    std::vector<std::vector<EplcGrow> > grow; /* per rig */
    std::vector<std::vector<int> > moveRoot;  /* per rig: per moving part, its root actor */
    std::vector<EplcRun> compRun;
    int growMesh, ringTex, ringMesh;
    bool ringUp;
    long drawn;                               /* placement draws since the last dump */
    long normalDrawn, companionNormalDrawn;     /* actual surface-lighting pass coverage */
};
static std::vector<EplcModel> g_eplc;
static bool g_eplcLoaded = false;
/* --noeplc clears it: the remastered buildings then go up by the cartridge's reveal. */
static bool g_eplcOn = true;
static int g_eplcSerial = -1;

/* ---- one building going up or being sold ------------------------------------------ */
enum { EPS_PLACE = 0, EPS_SELL };
struct EplcBld {
    int model, mode, id, cx, cy, start, str0;
    char type[16];
    float wx, wz;
    int fw, fh;
    bool mine;                                /* the player's own: the camera feels it */
    bool cut;                                 /* the building's destruction ended it */
    bool struck;                              /* hit while it plays (it plays on) */
    bool seen, jolted, ended;
    int endTick;
    std::vector<float> acc;                   /* per (actor or footprint) emitter */
    std::vector<float> next;                  /* a bursting emitter's next burst, seconds */
    float lastT;                              /* the sell's clock last tick */
};
static std::map<long long, EplcBld> g_eplcBld;
static int g_eplcLastTick = -1;

/* ---- reading a file --------------------------------------------------------------- */
struct EplcIn {
    const std::vector<unsigned char>& b;
    size_t o;
    bool bad;
    EplcIn(const std::vector<unsigned char>& bb) : b(bb), o(0), bad(false) {}
    bool need(size_t n) { if (bad || o + n > b.size()) { bad = true; return false; } return true; }
    unsigned u32() { if (!need(4)) return 0; unsigned v; memcpy(&v, &b[o], 4); o += 4; return v; }
    int i32() { return (int)u32(); }
    float f32() { if (!need(4)) return 0.0f; float v; memcpy(&v, &b[o], 4); o += 4; return ebld_finite(v) ? v : (bad = true, 0.0f); }
    unsigned u16() { if (!need(2)) return 0; unsigned short v; memcpy(&v, &b[o], 2); o += 2; return v; }
    unsigned u8() { if (!need(1)) return 0; return b[o++]; }
    void bytes(void* d, size_t n) { if (!need(n)) return; memcpy(d, &b[o], n); o += n; }
};

static void eplc_read_outline(EplcIn& in, EplcOutline& O, bool puff, int aflags)
{
    O.through = 0.0f;
    O.boost = 1.0f;
    if (puff) {
        O.at = in.f32(); O.len = in.f32(); O.y = in.f32(); O.edge = in.f32(); O.span = 0.0f;
        if (aflags & EPA_PBOOST) O.boost = in.f32();
    } else {
        O.y = in.f32(); O.edge = in.f32(); O.span = in.f32(); O.at = O.len = 0.0f;
        if (aflags & EPA_THROUGH) O.through = in.f32();
        if (aflags & EPA_EBOOST) O.boost = in.f32();
    }
    if (!(O.boost > 0.0f) || O.boost > 16.0f) { in.bad = true; return; }
    const unsigned n = in.u32();
    if (n > 100000) { in.bad = true; return; }
    O.pt.resize((size_t)n * 4);
    for (size_t i = 0; i < O.pt.size(); i++) O.pt[i] = in.f32();
    if (puff && n) {
        /* (a puff's span: its outline points' larger extent, pack units to cells) */
        float x0 = O.pt[0], x1 = O.pt[0], z0 = O.pt[1], z1 = O.pt[1];
        for (size_t i = 0; i < (size_t)n; i++) {
            x0 = fminf(x0, O.pt[i * 4]); x1 = fmaxf(x1, O.pt[i * 4]);
            z0 = fminf(z0, O.pt[i * 4 + 1]); z1 = fmaxf(z1, O.pt[i * 4 + 1]);
        }
        O.span = fmaxf(x1 - x0, z1 - z0) / 1024.0f;
    }
}

static bool eplc_read_tris(EplcIn& in, std::vector<EplcTriRec>& out, int nactor)
{
    const unsigned n = in.u32();
    if (n > 2000000) return false;
    out.resize(n);
    for (unsigned i = 0; i < n && !in.bad; i++) {
        EplcTriRec& t = out[i];
        t.actor = (int)in.u16();
        in.u16();
        for (int k = 0; k < 3; k++) t.c[k] = in.f32();
        for (int k = 0; k < 3; k++) t.n[k] = in.f32();
        if (t.actor >= nactor) return false;
    }
    return !in.bad;
}

/* The whole file, every count and index checked; a file that does not add up is refused
   whole, with the reason, and the type goes up by the cartridge's reveal. */
static bool eplc_read_one(const char* path, EplcModel& m)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    std::vector<unsigned char> b;
    fseek(f, 0, SEEK_END);
    const long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 64L * 1048576L) { fclose(f); return false; }
    b.resize((size_t)sz);
    const bool ok = fread(&b[0], 1, b.size(), f) == b.size();
    fclose(f);
    if (!ok) return false;
    EplcIn in(b);
    char magic[4];
    in.bytes(magic, 4);
    const unsigned ver = in.u32();
    char ty[9] = { 0 };
    in.bytes(ty, 8);
    if (memcmp(magic, "EPLC", 4) || (int)ver < EPLC_VERSION_MIN || (int)ver > EPLC_VERSION) {
        fprintf(stderr, "eplc: %s is not an EPLC version %d to %d file -- refused\n", path,
                EPLC_VERSION_MIN, EPLC_VERSION);
        return false;
    }
    m.type = ty;
    m.path = path;
    m.flags = (int)in.u32();
    const unsigned nrig = in.u32();
    m.jolt = in.f32();
    for (int i = 0; i < 5; i++) m.cam[i] = in.f32();
    for (int i = 0; i < 3; i++) m.emerge[i] = in.f32();
    const unsigned nt = in.u32();
    if (nt > 64) { fprintf(stderr, "eplc: %s: %u templates -- refused\n", path, nt); return false; }
    m.tmpl.resize(nt);
    for (unsigned i = 0; i < nt; i++) {
        m.tmpl[i].role = (int)in.u8();
        m.tmpl[i].flags = (int)in.u8();
        m.tmpl[i].sheet = (int)in.u16();
        for (int k = 0; k < EPF_N; k++) m.tmpl[i].f[k] = in.f32();
        if (m.tmpl[i].role > EPT_PUFF || m.tmpl[i].sheet > EPS_GLOW) in.bad = true;
    }
    for (int i = 0; i < 4; i++) m.foot[i] = in.f32();
    const unsigned nh = in.u32();
    if (nh > 4096) in.bad = true;
    m.hull.resize((size_t)nh * 2);
    for (size_t i = 0; i < m.hull.size(); i++) m.hull[i] = in.f32();
    for (int i = 0; i < 6; i++) m.ring[i] = in.f32();
    for (int i = 0; i < 4; i++) m.ringBox[i] = in.f32();
    m.ringW = (int)in.u16();
    m.ringH = (int)in.u16();
    if (m.ringW > 1024 || m.ringH > 1024 || (m.ringW & (m.ringW - 1)) || (m.ringH & (m.ringH - 1))
        || ((m.ringW == 0) != (m.ringH == 0)))
        in.bad = true;
    if (!in.bad && m.ringW) {
        m.ringPx.resize((size_t)m.ringW * m.ringH * 4);
        in.bytes(&m.ringPx[0], m.ringPx.size());
    }
    if (nrig != ((m.flags & EPX_WALL) ? 16u : 1u)) in.bad = true;
    m.rig.resize(in.bad ? 0 : nrig);
    for (unsigned r = 0; r < m.rig.size() && !in.bad; r++) {
        EplcRig& R = m.rig[r];
        R.length = in.f32(); R.slabLen = in.f32(); R.slabTop = in.f32();
        R.r0 = in.f32(); R.r1 = in.f32();
        R.slab = in.i32();
        const unsigned na = in.u32();
        if (na == 0 || na > 1000 || R.slab < 0 || R.slab >= (int)na || !(R.length > 0.0f)
            || R.length > 8.0f) { in.bad = true; break; }
        R.actor.resize(na);
        for (unsigned i = 0; i < na && !in.bad; i++) {
            EplcActor& A = R.actor[i];
            memset(A.name, 0, sizeof A.name);
            in.bytes(A.name, 24);
            A.parent = in.i32();
            A.flags = (int)in.u32();
            A.zmin = in.f32(); A.zmax = in.f32();
            if (A.parent < -1 || A.parent >= (int)i) { in.bad = true; break; }
            /* (version 2's flags in a version 1 file, or an option with no outline) */
            if ((A.flags & (EPA_THROUGH | EPA_EBOOST | EPA_PBOOST)) && ver < 2) { in.bad = true; break; }
            if ((A.flags & (EPA_THROUGH | EPA_EBOOST)) && !(A.flags & EPA_EMERGE)) { in.bad = true; break; }
            if ((A.flags & EPA_PBOOST) && !(A.flags & EPA_PUFF)) { in.bad = true; break; }
            const unsigned nm = in.u32();
            if (nm > 64) { in.bad = true; break; }
            A.mo.resize(nm);
            A.spinUp = A.extend = A.morph = -1;
            for (unsigned k = 0; k < nm && !in.bad; k++) {
                EplcMotion& M = A.mo[k];
                M.type = (int)in.u8(); M.ease = (int)in.u8(); M.flags = (int)in.u8(); in.u8();
                M.delay = in.f32(); M.len = in.f32(); M.power = in.f32();
                M.over = in.f32(); M.settle = in.f32();
                for (int q = 0; q < 12; q++) M.a[q] = in.f32();
                if (M.type > (ver >= 2 ? EPM_MORPH : EPM_SCALE) || M.ease > EPE_LINEAR) { in.bad = true; break; }
                if (M.type == EPM_SPINUP && A.spinUp < 0) A.spinUp = (int)k;
                if (M.type == EPM_MORPH) {
                    /* one morph an actor, and never with an extend: both bend its copy */
                    if (A.morph >= 0) { in.bad = true; break; }
                    A.morph = (int)k;
                    const unsigned nv = in.u32();
                    if (nv < 3 || nv > 200000) { in.bad = true; break; }
                    M.morph.resize((size_t)nv * 6);
                    for (size_t q = 0; q < M.morph.size(); q++) M.morph[q] = in.f32();
                }
                if (M.type == EPM_EXTEND) {
                    if (A.extend < 0) A.extend = (int)k;
                    const unsigned np = in.u32();
                    if (np == 0 || np > 256) { in.bad = true; break; }   /* (a grown net of ribbons: up to 256) */
                    M.path.resize(np);
                    for (unsigned p = 0; p < np && !in.bad; p++) {
                        const unsigned npt = in.u32();
                        if (npt < 2 || npt > 256) { in.bad = true; break; }
                        M.path[p].resize((size_t)npt * 3);
                        for (size_t q = 0; q < M.path[p].size(); q++) M.path[p][q] = in.f32();
                    }
                }
            }
            const unsigned nc = in.u32();
            if (nc > 4) { in.bad = true; break; }      /* the ground and four: planes 1..5 */
            A.clip.resize(nc);
            for (unsigned k = 0; k < nc && !in.bad; k++) {
                EplcClip& C = A.clip[k];
                C.in = in.i32();
                for (int q = 0; q < 3; q++) C.p[q] = in.f32();
                for (int q = 0; q < 3; q++) C.n[q] = in.f32();
                if (C.in < -1 || C.in >= (int)na) in.bad = true;
            }
            if (A.morph >= 0 && A.extend >= 0) { in.bad = true; break; }
            A.emerge.boost = A.puff.boost = 1.0f;
            A.emerge.through = A.puff.through = 0.0f;
            if (A.flags & EPA_EMERGE) eplc_read_outline(in, A.emerge, false, A.flags);
            if (A.flags & EPA_PUFF) eplc_read_outline(in, A.puff, true, A.flags);
        }
        if (!in.bad && !eplc_read_tris(in, R.tri, (int)na)) in.bad = true;
    }
    m.comp = false;
    if (!in.bad && (m.flags & EPX_COMP)) {
        char cm[9] = { 0 };
        in.bytes(cm, 8);
        m.compModel = cm;
        m.compYaw = in.f32();
        m.compShift[0] = in.f32(); m.compShift[1] = in.f32();
        m.compFlags = (int)in.u32();
        if (!eplc_read_tris(in, m.compTri, (int)m.rig[0].actor.size())) in.bad = true;
        m.comp = !in.bad;
    }
    if (in.bad || in.o != b.size()) {
        fprintf(stderr, "eplc: %s does not add up (at byte %lu of %lu) -- refused\n", path,
                (unsigned long)in.o, (unsigned long)b.size());
        return false;
    }
    m.ebld = m.evhc = -1;
    m.attached = false;
    m.growMesh = m.ringTex = m.ringMesh = -1;
    m.ringUp = false;
    m.matched = m.unmatched = 0;
    m.morphed = m.unmorphed = 0;
    m.drawn = m.normalDrawn = m.companionNormalDrawn = 0;
    return true;
}

/* Once per process, beside the remastered buildings' own files: OpenCNC3D's from eplc/, and
   under Red Alert's module its own from ra3d/eplc/ (tools/ra3d/bake-placements.py, staged by
   tools/ra3d/stage-structures.py), named by Red Alert's types, never OpenCNC3D's files: a
   type both games name (the Construction Yard) is a different building in each. */
static void eplc_load(const char* dir)
{
    if (g_eplcLoaded) return;
    g_eplcLoaded = true;
    static const char* const TYPES_TD[] = {
        "AFLD", "ATWR", "BIO", "EYE", "FACT", "FIX", "GTWR", "GUN", "HAND", "HOSP",
        "HPAD", "HQ", "MISS", "NUK2", "NUKE", "OBLI", "PROC", "PYLE", "SAM", "SILO",
        "TMPL", "WEAP", "SBAG", "CYCL", "BRIK", NULL
    };
    static const char* const TYPES_RA[] = {
        "FACT", "POWR", "APWR", "BARR", "TENT", "DOME", "ATEK", "STEK", "PBOX", "HBOX",
        "KENN", "GAP", "IRON", "PDOX", "MSLO", "FCOM", "SYRD", "SPEN", "TSLA", "FTUR",
        "AGUN", "PROC", "SILO", "HPAD", "AFLD", "FIX", "HOSP", "WEAP", "SAM", "GUN",
        "MISS", "BIO", "SBAG", "CYCL", "BRIK", "BARB",
        /* (Red Alert's own barbed-wire fence, built by the Soviets: tools/ra3d/bake-props.py) */
        "FENC", NULL
    };
    const char* const* TYPES = g_raModule ? TYPES_RA : TYPES_TD;
    std::string names;
    for (size_t i = 0; TYPES[i]; i++) {
        char path[512];
        snprintf(path, sizeof path, "%s%s/%s.eplc", dir && *dir ? dir : "",
                 g_raModule ? "ra3d/eplc" : "eplc", TYPES[i]);
        EplcModel m;
        if (!eplc_read_one(path, m)) continue;
        if (m.type != TYPES[i]) {
            fprintf(stderr, "eplc: %s says it is %s -- refused\n", path, m.type.c_str());
            continue;
        }
        if (((m.flags & EPX_WALL) != 0) != (ebld_wall_kind(TYPES[i]) >= 0)) {
            fprintf(stderr, "eplc: %s is %s a wall -- refused\n", path,
                    (m.flags & EPX_WALL) ? "" : "not");
            continue;
        }
        names += (names.empty() ? "" : " ") + m.type;
        g_eplc.push_back(m);
    }
    fprintf(stderr, "eplc: %d placement%s for Enhanced%s%s\n", (int)g_eplc.size(),
            g_eplc.size() == 1 ? "" : "s", names.empty() ? "" : ": ", names.c_str());
}

/* ---- finding the building's own triangles again ----------------------------------- */
/* A grid over the lab's triangle centres, 8 pack units a cell, for the nearest-centre
   search the attachment runs once per pack and type. */
struct EplcGrid {
    float cell;
    std::map<long long, std::vector<int> > g;
    static long long key(int x, int y, int z)
    {
        return ((long long)(x + 1048576) << 42) ^ ((long long)(y + 1048576) << 21)
               ^ (long long)(z + 1048576);
    }
    void build(const std::vector<EplcTriRec>& T)
    {
        cell = 8.0f;
        g.clear();
        for (size_t i = 0; i < T.size(); i++)
            g[key((int)floorf(T[i].c[0] / cell), (int)floorf(T[i].c[1] / cell),
                  (int)floorf(T[i].c[2] / cell))].push_back((int)i);
    }
    /* the record lying at c facing n (within 4 pack units and 25 degrees); a triangle of no
       area matches the nearest centre whatever its facing. -1: none */
    int find(const std::vector<EplcTriRec>& T, const float* c, const float* n, bool flat) const
    {
        const int x = (int)floorf(c[0] / cell), y = (int)floorf(c[1] / cell),
                  z = (int)floorf(c[2] / cell);
        int best = -1;
        float bd = 16.0f;
        for (int dz = -1; dz <= 1; dz++)
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    std::map<long long, std::vector<int> >::const_iterator it =
                        g.find(key(x + dx, y + dy, z + dz));
                    if (it == g.end()) continue;
                    for (size_t q = 0; q < it->second.size(); q++) {
                        const EplcTriRec& R = T[it->second[q]];
                        const float ex = R.c[0] - c[0], ey = R.c[1] - c[1], ez = R.c[2] - c[2];
                        const float d = ex * ex + ey * ey + ez * ez;
                        if (d >= bd) continue;
                        const float rn = R.n[0] * R.n[0] + R.n[1] * R.n[1] + R.n[2] * R.n[2];
                        const float dot = R.n[0] * n[0] + R.n[1] * n[1] + R.n[2] * n[2];
                        if (!flat && rn > 0.25f && dot <= 0.9f) continue;
                        bd = d;
                        best = it->second[q];
                    }
                }
        return best;
    }
};

static void eplc_tri_key(const PackTri& t, float* c, float* n, bool* flat)
{
    c[0] = (t.v[0].x + t.v[1].x + t.v[2].x) / 3.0f;
    c[1] = (t.v[0].y + t.v[1].y + t.v[2].y) / 3.0f;
    c[2] = (t.v[0].z + t.v[1].z + t.v[2].z) / 3.0f;
    const float ax = t.v[1].x - t.v[0].x, ay = t.v[1].y - t.v[0].y, az = t.v[1].z - t.v[0].z;
    const float bx = t.v[2].x - t.v[0].x, by = t.v[2].y - t.v[0].y, bz = t.v[2].z - t.v[0].z;
    n[0] = ay * bz - az * by; n[1] = az * bx - ax * bz; n[2] = ax * by - ay * bx;
    const float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    *flat = !(l > 1e-6f);
    if (!*flat) { n[0] /= l; n[1] /= l; n[2] /= l; }
}

/* The actor of every triangle in [a, b) of a pack mesh, -1 where none is found. */
static int eplc_match(const PackMesh& pm, int a, int b, const std::vector<EplcTriRec>& T,
                      const EplcGrid& G, std::vector<int>& out)
{
    int miss = 0;
    out.assign((size_t)(b - a), -1);
    for (int i = a; i < b; i++) {
        float c[3], n[3];
        bool flat;
        eplc_tri_key(pm.tris[(size_t)i], c, n, &flat);
        const int r = G.find(T, c, n, flat);
        if (r < 0) miss++;
        else out[(size_t)(i - a)] = T[(size_t)r].actor;
    }
    return miss;
}

/* A grown part's paths as segments, and every vertex's place along its nearest path, as
   the viewer's rigGrowPrep. */
static void eplc_grow_prep(EplcGrow& G, const EplcMotion& M, const PackMesh& gm)
{
    G.P.clear(); G.plen.clear();
    for (size_t p = 0; p < M.path.size(); p++) {
        std::vector<EplcSeg> segs;
        float cum = 0.0f;
        const std::vector<float>& q = M.path[p];
        for (size_t i = 0; i + 1 < q.size() / 3; i++) {
            EplcSeg s;
            float L = 0.0f;
            for (int k = 0; k < 3; k++) { s.a[k] = q[i * 3 + k]; s.d[k] = q[i * 3 + 3 + k] - q[i * 3 + k]; L += s.d[k] * s.d[k]; }
            L = sqrtf(L);
            for (int k = 0; k < 3; k++) s.d[k] /= (L > 0.0f ? L : 1.0f);
            s.L = L; s.cum = cum;
            cum += L;
            segs.push_back(s);
        }
        G.P.push_back(segs);
        G.plen.push_back(cum);
    }
    const int nv = (G.t1 - G.t0) * 3;
    G.rest.resize((size_t)nv * 3);
    G.at.assign((size_t)nv, 0.0f);
    G.path.assign((size_t)nv, 0);
    G.seg.assign((size_t)nv, 0);
    for (int t = G.t0; t < G.t1; t++)
        for (int k = 0; k < 3; k++) {
            const int vi = (t - G.t0) * 3 + k;
            const PackVert& v = gm.tris[(size_t)t].v[k];
            G.rest[(size_t)vi * 3] = v.x; G.rest[(size_t)vi * 3 + 1] = v.y; G.rest[(size_t)vi * 3 + 2] = v.z;
            float best = 1e30f;
            for (size_t p = 0; p < G.P.size(); p++)
                for (size_t j = 0; j < G.P[p].size(); j++) {
                    const EplcSeg& s = G.P[p][j];
                    const float wx = v.x - s.a[0], wy = v.y - s.a[1], wz = v.z - s.a[2];
                    float tt = wx * s.d[0] + wy * s.d[1] + wz * s.d[2];
                    if (tt < 0.0f) tt = 0.0f;
                    if (tt > s.L) tt = s.L;
                    const float ex = wx - s.d[0] * tt, ey = wy - s.d[1] * tt, ez = wz - s.d[2] * tt;
                    const float d = ex * ex + ey * ey + ez * ez;
                    if (d < best) { best = d; G.at[(size_t)vi] = s.cum + tt; G.path[(size_t)vi] = (int)p; G.seg[(size_t)vi] = (int)j; }
                }
        }
    G.k = -1.0f;
}

/* A bent part's vertices (morph): where each rests, and its offset at the motion's start,
   found by its position among the motion's (the nearest within EPLC_MORPH_TOL; the lab's
   offsets are a function of the position, so the skin stays closed). A vertex with none
   keeps no offset, and is counted in *miss. */
static void eplc_morph_prep(EplcGrow& G, const EplcMotion& M, const PackMesh& gm, int* found,
                            int* miss)
{
    const float cell = 2.0f;
    std::map<long long, std::vector<int> > grid;
    const int nm = (int)(M.morph.size() / 6);
    for (int i = 0; i < nm; i++) {
        const float* p = &M.morph[(size_t)i * 6];
        grid[EplcGrid::key((int)floorf(p[0] / cell), (int)floorf(p[1] / cell),
                           (int)floorf(p[2] / cell))].push_back(i);
    }
    const int nv = (G.t1 - G.t0) * 3;
    G.rest.resize((size_t)nv * 3);
    G.off.assign((size_t)nv * 3, 0.0f);
    for (int t = G.t0; t < G.t1; t++)
        for (int k = 0; k < 3; k++) {
            const int vi = (t - G.t0) * 3 + k;
            const PackVert& v = gm.tris[(size_t)t].v[k];
            G.rest[(size_t)vi * 3] = v.x; G.rest[(size_t)vi * 3 + 1] = v.y; G.rest[(size_t)vi * 3 + 2] = v.z;
            const int x = (int)floorf(v.x / cell), y = (int)floorf(v.y / cell), z = (int)floorf(v.z / cell);
            int best = -1;
            float bd = EPLC_MORPH_TOL * EPLC_MORPH_TOL;
            for (int dz = -1; dz <= 1; dz++)
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        std::map<long long, std::vector<int> >::const_iterator it =
                            grid.find(EplcGrid::key(x + dx, y + dy, z + dz));
                        if (it == grid.end()) continue;
                        for (size_t q = 0; q < it->second.size(); q++) {
                            const float* p = &M.morph[(size_t)it->second[q] * 6];
                            const float ex = p[0] - v.x, ey = p[1] - v.y, ez = p[2] - v.z;
                            const float d = ex * ex + ey * ey + ez * ez;
                            if (d <= bd) { bd = d; best = it->second[q]; }
                        }
                    }
            if (best < 0) { (*miss)++; continue; }
            (*found)++;
            for (int c = 0; c < 3; c++) G.off[(size_t)vi * 3 + c] = M.morph[(size_t)best * 6 + 3 + c];
        }
    G.k = -1.0f;
}

/* A copied triangle keeps whatever the pack mesh carries per triangle beside it: a pack
   mesh that holds a normal per corner and a class byte per triangle (`nrm`, 9 floats, and
   `tfl`) has them copied with the triangle, so a grown part lights as it does standing.
   Chosen at compile time, so this reads the same against a pack mesh without them. */
template <class PM>
static auto eplc_copy_extra(PM& d, const PM& s, size_t i, int)
    -> decltype(d.nrm.push_back(0.0f), d.tfl.push_back(s.tfl[0]), void())
{
    if (s.nrm.size() < (i + 1) * 9 || s.tfl.size() <= i
        || d.nrm.size() != (d.tris.size() - 1) * 9 || d.tfl.size() != d.tris.size() - 1)
        return;
    d.nrm.insert(d.nrm.end(), s.nrm.begin() + (long)(i * 9), s.nrm.begin() + (long)(i * 9 + 9));
    d.tfl.push_back(s.tfl[i]);
}
template <class PM>
static void eplc_copy_extra(PM&, const PM&, size_t, long) {}

/* The piece of a wall's model for connection mask k, by its name "w<k>", -1 none. */
static int eplc_wall_piece(const EbldSet& S, int k)
{
    char nm[8];
    snprintf(nm, sizeof nm, "w%d", k);
    for (size_t i = 0; i < S.piece.size(); i++)
        if (!strcmp(S.piece[i].name, nm)) return (int)i;
    return -1;
}

/* After every pack load, after ebld_attach and evhc_attach: each placement finds its
   building's intact mesh, matches its triangles, makes its runs, its grow mesh and its
   ring. A type whose building did not attach, or whose triangles do not match, is left
   unattached and goes up by the cartridge's reveal. `fresh` false: only the placements not
   attached yet are tried again, the others kept as they are (a building whose file is read at
   first sight, eplc_attach_late). */
static void eplc_attach(bool fresh = true)
{
    g_eplcSerial = g_ebldPackSerial;
    if (g_eplc.empty()) return;
    int attached = 0;
    for (size_t e = 0; e < g_eplc.size(); e++) {
        EplcModel& P = g_eplc[e];
        if (!fresh && P.attached) { attached++; continue; }
        P.attached = false;
        P.ebld = P.evhc = -1;
        P.run.clear(); P.grow.clear(); P.moveRoot.clear(); P.compRun.clear();
        P.growMesh = P.ringTex = P.ringMesh = -1;
        P.ringUp = false;
        P.matched = P.unmatched = 0;
        P.morphed = P.unmorphed = 0;
        int m = -1;
        for (size_t i = 0; i < g_ebld.size(); i++)
            if (g_ebld[i].attached && g_ebld[i].type == P.type) { m = (int)i; break; }
        if (m < 0 || !ebld_meshes_ok(g_ebld[m])) continue;
        const EbldModel& M = g_ebld[m];
        const EbldSet& S = M.set[EBS_INTACT];
        const bool wall = (P.flags & EPX_WALL) != 0;
        if (wall != ((M.flags & EBX_WALL) != 0)) continue;
        bool havePieces = true;
        for (int k = 0; wall && k < 16; k++) if (eplc_wall_piece(S, k) < 0) havePieces = false;
        if (!havePieces) continue;
        const int imi = M.mesh[EBM_INTACT];
        const std::vector<int>& rg = M.rng[EBM_INTACT];
        P.run.resize(P.rig.size());
        P.grow.resize(P.rig.size());
        P.moveRoot.resize(P.rig.size());
        std::vector<std::vector<int> > actOf(P.rig.size());     /* per rig, per triangle */
        std::vector<int> base(P.rig.size(), 0);
        bool refuse = false;
        for (size_t r = 0; r < P.rig.size() && !refuse; r++) {
            const EplcRig& R = P.rig[r];
            EplcGrid G;
            G.build(R.tri);
            int a = 0, b = M.shadow0;
            if (wall) {
                const int k = eplc_wall_piece(S, (int)r);
                a = rg[(size_t)k * EBR + EBR_A]; b = rg[(size_t)k * EBR + EBR_B];
            }
            base[r] = a;
            int miss = eplc_match(g_pack.mesh[(size_t)imi], a, b, R.tri, G, actOf[r]);
            /* A WAVING CLOTH'S TRIANGLES (a flag) are posed in the mesh itself by every draw
               (ebld_wave), so a placement attached after the building was first drawn (Red
               Alert's, read at first sight) finds them blown out of their rest: they go with
               the nearest triangle found, as a sliver does, and are not counted against it */
            if (miss && !wall && !M.waveMesh[EBM_INTACT].empty()) {
                for (size_t e = 0; e < M.waveMesh[EBM_INTACT].size(); e++) {
                    const int t = M.waveMesh[EBM_INTACT][e].first;
                    if (t >= a && t < b && actOf[r][(size_t)(t - a)] < 0) {
                        actOf[r][(size_t)(t - a)] = -2;
                        miss--;
                    }
                }
                for (size_t i = 0; i < actOf[r].size(); i++)
                    if (actOf[r][i] == -2) actOf[r][i] = -1;
            }
            P.unmatched += miss;
            P.matched += (b - a) - miss;
            /* A FEW MISSING TRIANGLES (a sliver the lab rounded differently) go with the
               nearest one found; more than one in two hundred is a different model. */
            if (miss * 200 > (b - a)) refuse = true;
            for (int i = 0; i < b - a && !refuse; i++) {
                if (actOf[r][(size_t)i] >= 0) continue;
                for (int d = 1; d < b - a; d++) {
                    if (i - d >= 0 && actOf[r][(size_t)(i - d)] >= 0) { actOf[r][(size_t)i] = actOf[r][(size_t)(i - d)]; break; }
                    if (i + d < b - a && actOf[r][(size_t)(i + d)] >= 0) { actOf[r][(size_t)i] = actOf[r][(size_t)(i + d)]; break; }
                }
            }
        }
        if (refuse) {
            fprintf(stderr, "eplc: %s: %d of %d of the building's triangles are not in its "
                            "placement -- it goes up by the cartridge's reveal (rebake the "
                            "placement for this model: tools/art/remaster/bake_eplc.py %s)\n",
                    P.type.c_str(), P.unmatched, P.matched + P.unmatched, P.type.c_str());
            P.run.clear(); P.grow.clear(); P.moveRoot.clear();
            continue;
        }
        /* THE RUNS, in the intact mesh's own order (so the translucent parts keep their
           draw order), split wherever the actor, the moving part or the flipbook changes.
           A growing actor's triangles go to the grow mesh instead, copied. */
        PackMesh gm;
        gm.gpuDeformed = true;  /* grow/morph vertices are rewritten at each pose */
        memset(gm.name, 0, sizeof gm.name);
        snprintf(gm.name, sizeof gm.name, "eplc:%s:grow", P.type.c_str());
        const PackMesh& im = g_pack.mesh[(size_t)imi];
        for (size_t r = 0; r < P.rig.size(); r++) {
            const EplcRig& R = P.rig[r];
            P.moveRoot[r].assign(M.move.size(), -1);
            std::vector<EplcRun>& out = P.run[r];
            const size_t k0 = wall ? (size_t)eplc_wall_piece(S, (int)r) : 0,
                         k1 = wall ? k0 + 1 : S.piece.size();
            std::map<int, std::vector<std::pair<int, int> > > growTris;   /* actor -> (tri, piece) */
            for (size_t k = k0; k < k1; k++) {
                const EbldPiece& Pc = S.piece[k];
                if (Pc.flags & EBF_WATERFALL) continue;
                const int* rr = &rg[k * EBR];
                const int mv = (k < M.moveOf.size()) ? M.moveOf[k] : -1;
                for (int i = rr[EBR_A]; i < rr[EBR_B]; i++) {
                    const int act = actOf[r][(size_t)(i - base[r])];
                    if (act < 0) continue;
                    if (mv >= 0) {
                        int& root = P.moveRoot[r][(size_t)mv];
                        if (R.actor[(size_t)act].flags & EPA_SPLIT) root = act;
                        else if (root < 0) root = act;
                    }
                    if (R.actor[(size_t)act].extend >= 0 || R.actor[(size_t)act].morph >= 0) {
                        growTris[act].push_back(std::make_pair(i, (int)k));
                        continue;
                    }
                    const int mt = im.tris[(size_t)i].tex - M.tex0;
                    const int flip = ebld_flipbook(M, mt) ? mt : -1;
                    if (!out.empty() && out.back().grow < 0 && out.back().b == i
                        && out.back().actor == act && out.back().move == mv && out.back().flip == flip) {
                        out.back().b = i + 1;
                    } else {
                        EplcRun q = { -1, i, i + 1, act, mv, flip };
                        out.push_back(q);
                    }
                }
            }
            for (std::map<int, std::vector<std::pair<int, int> > >::iterator it = growTris.begin();
                 it != growTris.end(); ++it) {
                EplcGrow Gr;
                Gr.actor = it->first;
                const EplcActor& GA = R.actor[(size_t)it->first];
                Gr.kind = GA.morph >= 0 ? EPG_MORPH : EPG_EXTEND;
                Gr.motion = GA.morph >= 0 ? GA.morph : GA.extend;
                Gr.t0 = (int)gm.tris.size();
                for (size_t q = 0; q < it->second.size(); q++) {
                    const int i = it->second[q].first, k = it->second[q].second;
                    const int mv = ((size_t)k < M.moveOf.size()) ? M.moveOf[(size_t)k] : -1;
                    const int mt = im.tris[(size_t)i].tex - M.tex0;
                    const int flip = ebld_flipbook(M, mt) ? mt : -1;
                    const int at = (int)gm.tris.size();
                    gm.tris.push_back(im.tris[(size_t)i]);
                    eplc_copy_extra(gm, im, (size_t)i, 0);
                    std::vector<EplcRun>& o2 = out;
                    if (!o2.empty() && o2.back().grow == (int)P.grow[r].size() && o2.back().b == at
                        && o2.back().move == mv && o2.back().flip == flip) {
                        o2.back().b = at + 1;
                    } else {
                        EplcRun q2 = { (int)P.grow[r].size(), at, at + 1, it->first, mv, flip };
                        o2.push_back(q2);
                    }
                }
                Gr.t1 = (int)gm.tris.size();
                P.grow[r].push_back(Gr);
            }
        }
        if (!gm.tris.empty()) {
            PackPart part;
            part.tri0 = 0; part.ntris = (int)gm.tris.size(); part.role = ROLE_STATIC;
            part.px = part.py = part.pz = 0.0f;
            gm.parts.assign(1, part);
            g_pack.mesh.push_back(gm);
            P.growMesh = (int)g_pack.mesh.size() - 1;
            for (size_t r = 0; r < P.grow.size(); r++)
                for (size_t g = 0; g < P.grow[r].size(); g++) {
                    EplcGrow& Gr = P.grow[r][g];
                    const EplcMotion& Mo = P.rig[r].actor[(size_t)Gr.actor].mo[(size_t)Gr.motion];
                    if (Gr.kind == EPG_MORPH)
                        eplc_morph_prep(Gr, Mo, g_pack.mesh[(size_t)P.growMesh], &P.morphed, &P.unmorphed);
                    else
                        eplc_grow_prep(Gr, Mo, g_pack.mesh[(size_t)P.growMesh]);
                }
            /* A BENT PART WHOSE VERTICES ARE NOT ITS MORPH'S is a different model, as a
               triangle not found is: more than one in two hundred refuses the placement */
            if (P.unmorphed * 200 > P.morphed + P.unmorphed) {
                fprintf(stderr, "eplc: %s: %d of %d vertices of its bent parts are not in its "
                                "placement -- it goes up by the cartridge's reveal (rebake the "
                                "placement for this model: tools/art/remaster/bake_eplc.py %s)\n",
                        P.type.c_str(), P.unmorphed, P.morphed + P.unmorphed, P.type.c_str());
                g_pack.mesh.pop_back();
                P.growMesh = -1;
                P.run.clear(); P.grow.clear(); P.moveRoot.clear();
                continue;
            }
        }
        /* THE RING OF BROKEN EARTH, a grid over its box lying on the terrain (as the
           Power Plant's scorch), the image uploaded at its first draw */
        if (P.ringW > 0) {
            PackTex pt;
            pt.w = pt.uw = P.ringW;
            pt.h = pt.uh = P.ringH;
            pt.gl = pt.gl_gdi = 0;
            for (int q = 0; q < 7; q++) pt.gl_extra[q] = 0;
            g_pack.tex.push_back(pt);
            P.ringTex = (int)g_pack.tex.size() - 1;
            PackMesh rm;
            memset(rm.name, 0, sizeof rm.name);
            snprintf(rm.name, sizeof rm.name, "eplc:%s:ring", P.type.c_str());
            const int N = 8;
            for (int j = 0; j < N; j++)
                for (int i = 0; i < N; i++) {
                    float x[4], z[4], u[4], v[4];
                    const int ci[4] = { i, i + 1, i + 1, i }, cj[4] = { j, j, j + 1, j + 1 };
                    for (int c = 0; c < 4; c++) {
                        u[c] = (float)ci[c] / (float)N;
                        v[c] = (float)cj[c] / (float)N;
                        x[c] = P.ringBox[0] + (P.ringBox[2] - P.ringBox[0]) * u[c];
                        z[c] = P.ringBox[1] + (P.ringBox[3] - P.ringBox[1]) * v[c];
                    }
                    static const int TRI[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
                    for (int h = 0; h < 2; h++) {
                        PackTri t;
                        t.tex = P.ringTex;
                        t.mode = MODE_XLU;
                        t.wrap = 0;
                        for (int k = 0; k < 3; k++) {
                            const int c = TRI[h][k];
                            PackVert& d = t.v[k];
                            d.x = x[c]; d.y = 0.0f; d.z = z[c];
                            d.u = u[c]; d.v = v[c];
                            d.r = d.g = d.b = d.a = 255;
                        }
                        rm.tris.push_back(t);
                    }
                }
            PackPart part;
            part.tri0 = 0; part.ntris = (int)rm.tris.size(); part.role = ROLE_STATIC;
            part.px = part.py = part.pz = 0.0f;
            rm.parts.assign(1, part);
            g_pack.mesh.push_back(rm);
            P.ringMesh = (int)g_pack.mesh.size() - 1;
        }
        /* THE COMPANION: the vehicle it deploys from, its own remastered model, found the
           same way in that model's intact mesh */
        if (P.comp) {
            const int v = evhc_model_of(P.compModel.c_str());
            if (v >= 0) {
                const EvhcModel& V = g_evhc[(size_t)v];
                const EbldSet& VS = V.m.set[EBS_INTACT];
                const int vmi = V.m.mesh[EBM_INTACT];
                const int n = (int)VS.tri.size();
                if (vmi >= 0 && vmi < (int)g_pack.mesh.size() && (int)g_pack.mesh[(size_t)vmi].tris.size() >= n) {
                    EplcGrid G;
                    G.build(P.compTri);
                    std::vector<int> ca;
                    const int miss = eplc_match(g_pack.mesh[(size_t)vmi], 0, n, P.compTri, G, ca);
                    if (miss * 200 <= n) {
                        for (int i = 0; i < n; i++) {
                            const int act = ca[(size_t)i];
                            if (act < 0) continue;
                            if (!P.compRun.empty() && P.compRun.back().b == i && P.compRun.back().actor == act)
                                P.compRun.back().b = i + 1;
                            else { EplcRun q = { -1, i, i + 1, act, -1, -1 }; P.compRun.push_back(q); }
                        }
                        P.evhc = v;
                    } else {
                        fprintf(stderr, "eplc: %s: the %s it deploys from does not match its "
                                "placement (%d of %d triangles) -- placed without it\n",
                                P.type.c_str(), P.compModel.c_str(), miss, n);
                    }
                }
            }
        }
        P.ebld = m;
        P.attached = true;
        attached++;
        if (P.unmatched)
            fprintf(stderr, "eplc: %s: %d triangles matched, %d by their neighbours\n",
                    P.type.c_str(), P.matched, P.unmatched);
        if (P.morphed || P.unmorphed)
            fprintf(stderr, "eplc: %s: %d vertices of its bent parts found their offsets, %d "
                            "did not\n", P.type.c_str(), P.morphed, P.unmorphed);
    }
    fprintf(stderr, "eplc: %d of %d placements attached to %s\n", attached, (int)g_eplc.size(),
            g_pack.scen);
}

static bool eplc_rule_now(void) { return g_eplcOn && ebld_rule_now(); }

/* Whether the play folder has a placement for this type, attached yet or not (Red Alert's are
   attached the first time a building of the type goes up or is sold, eplc_attach_late). */
static bool eplc_has_type(const char* type)
{
    if (!eplc_rule_now()) return false;
    for (size_t i = 0; i < g_eplc.size(); i++)
        if (g_eplc[i].type == type) return true;
    return false;
}
static int eplc_model_of(const char* type)
{
    if (!eplc_rule_now()) return -1;
    for (size_t i = 0; i < g_eplc.size(); i++) {
        const EplcModel& P = g_eplc[i];
        if (!P.attached || P.type != type) continue;
        if (P.ebld < 0 || P.ebld >= (int)g_ebld.size() || !g_ebld[(size_t)P.ebld].attached) return -1;
        return (int)i;
    }
    return -1;
}

/* ---- the motions, as the viewer's rigProgress, rigEase and rigMatrix --------------- */
static float eplc_ease(const EplcMotion& m, float u)
{
    if (m.ease == EPE_INOUT) return u * u * (3.0f - 2.0f * u);
    if (m.ease == EPE_IN) return u * u;
    if (m.ease == EPE_LINEAR) return u;
    return 1.0f - powf(1.0f - u, m.power);
}
/* how far a motion is at t: k from 0 to 1 (exactly 1 once over) and the overshoot's share,
   which rises and falls back over the settle */
static void eplc_progress(const EplcMotion& m, float t, float* k, float* b)
{
    const float u = (t - m.delay) / (m.len > 1e-6f ? m.len : 1e-6f);
    *b = 0.0f;
    if (u <= 0.0f) { *k = 0.0f; return; }
    if (u >= 1.0f) { *k = 1.0f; return; }
    if (!(m.over != 0.0f)) { *k = eplc_ease(m, u); return; }
    const float k1 = 1.0f - m.settle;
    if (u < k1) { *k = eplc_ease(m, u / k1); return; }
    const float s = (u - k1) / (1.0f - k1);
    *k = 1.0f;
    *b = sinf(EBLD_PI * s) * (1.0f - s) / 0.58f;
}
static EbXf eplc_trans(float x, float y, float z)
{
    EbXf X;
    ebxf_id(X);
    X.t[0] = x; X.t[1] = y; X.t[2] = z;
    return X;
}
/* An actor's pose at t relative to its parent: its turns and scales about their pivots in
   order, then the sum of its slides and shakes. The identity once every motion is over,
   but for a turn marked HOLD, which stays at its end. */
static EbXf eplc_local(const EplcActor& A, float t)
{
    EbXf M;
    ebxf_id(M);
    for (size_t i = 0; i < A.mo.size(); i++) {
        const EplcMotion& m = A.mo[i];
        if (m.type != EPM_HINGE && m.type != EPM_SCALE) continue;
        float k, b;
        eplc_progress(m, t, &k, &b);
        if (k >= 1.0f && b == 0.0f && !(m.flags & EPMF_HOLD)) continue;
        EbXf R;
        if (m.type == EPM_HINGE) {
            const float d = m.a[7] - m.a[6];
            const float ang = m.a[6] + d * k + (d > 0.0f ? 1.0f : d < 0.0f ? -1.0f : 0.0f) * m.over * b;
            ebxf_turn(R, m.a, m.a + 3, ang * (EBLD_PI / 180.0f));
        } else {
            ebxf_id(R);
            for (int j = 0; j < 3; j++) {
                const float s = m.a[3 + j] + (1.0f - m.a[3 + j]) * k;
                R.R[j * 4] = s;
                R.t[j] = m.a[j] - s * m.a[j];
            }
        }
        M = ebxf_mul(R, M);
    }
    float off[3] = { 0.0f, 0.0f, 0.0f };
    for (size_t i = 0; i < A.mo.size(); i++) {
        const EplcMotion& m = A.mo[i];
        if (m.type == EPM_SLIDE) {
            float k, b;
            eplc_progress(m, t, &k, &b);
            for (int j = 0; j < 3; j++) off[j] += (k >= 1.0f) ? m.a[3 + j] : m.a[j] + (m.a[3 + j] - m.a[j]) * k;
            if (b != 0.0f && m.over != 0.0f) {
                float d[3], l = 0.0f;
                for (int j = 0; j < 3; j++) { d[j] = m.a[3 + j] - m.a[j]; l += d[j] * d[j]; }
                l = sqrtf(l);
                if (l > 0.0f) for (int j = 0; j < 3; j++) off[j] += d[j] / l * m.over * b;
            }
        } else if (m.type == EPM_SHAKE) {
            const float u = (t - m.delay) / (m.len > 1e-6f ? m.len : 1e-6f);
            if (u > 0.0f && u < 1.0f) {
                const float A_ = m.a[0] * sinf(EBLD_PI * u);
                off[0] += A_ * sinf(2.0f * EBLD_PI * m.a[1] * t);
                off[2] -= A_ * sinf(2.0f * EBLD_PI * m.a[2] * t + 1.1f);
            }
        }
    }
    if (off[0] != 0.0f || off[1] != 0.0f || off[2] != 0.0f)
        M = ebxf_mul(eplc_trans(off[0], off[1], off[2]), M);
    return M;
}

/* ---- one placement's pose at one moment -------------------------------------------- */
struct EplcPose {
    float t;                                  /* seconds into the placement */
    float spinTick;                           /* the turning parts' clock, engine ticks */
    std::vector<EbXf> W;                      /* per actor, in the building's frame */
    std::vector<EbXf> spin;                   /* per moving part, its own turn now */
    std::vector<int> spinOn;                  /* per moving part: 1 turns, 0 fill, -1 none */
};

/* The idle turn's clock while the placement plays: held at the moment it will end, or, for
   a part that spins up, run at the ramp's share of real time so it reaches that moment
   exactly at the end (the viewer's placeT, the integral of a smoothstep). Engine ticks. */
static float eplc_spin_clock(const EplcRig& R, int root, float t, float endTick)
{
    if (t >= R.length) return endTick + (t - R.length) * 15.0f;
    if (root < 0 || R.actor[(size_t)root].spinUp < 0) return endTick;
    const EplcMotion& m = R.actor[(size_t)root].mo[(size_t)R.actor[(size_t)root].spinUp];
    const float d0 = m.delay, d1 = (m.flags & EPMF_OPEN) || !(m.len > 0.0f) ? R.length : m.delay + m.len;
    const float L = (d1 - d0) > 1e-3f ? d1 - d0 : 1e-3f;
    struct I_ {
        static float f(float x, float d0_, float d1_, float L_)
        {
            if (x <= d0_) return 0.0f;
            if (x >= d1_) return L_ / 2.0f + (x - d1_);
            const float u = (x - d0_) / L_;
            return L_ * (u * u * u - u * u * u * u / 2.0f);
        }
    };
    const float tt = t < R.length ? t : R.length;
    return endTick - 15.0f * (I_::f(R.length, d0, d1, L) - I_::f(tt, d0, d1, L));
}

static void eplc_pose(const EplcModel& P, int ri, const SimObject* o, float t, float endTick,
                      EplcPose& out)
{
    const EplcRig& R = P.rig[(size_t)ri];
    const EbldModel& M = g_ebld[(size_t)P.ebld];
    out.t = t;
    out.spinTick = endTick;
    const size_t nm = M.move.size();
    out.spin.resize(nm);
    out.spinOn.assign(nm, -1);
    for (size_t v = 0; v < nm; v++) {
        const EbldMove& mv = M.move[v];
        ebxf_id(out.spin[v]);
        if (mv.kind == EBV_FILL) { out.spinOn[v] = 0; continue; }
        /* A PART OF A CLIP (the Hand of Nod's gate) stands at rest while the placement
           plays, as the lab splits it as any other part; its clip takes over at the end */
        if (mv.kind == EBV_CLIP) continue;
        const int root = (ri < (int)P.moveRoot.size() && v < P.moveRoot[(size_t)ri].size())
                       ? P.moveRoot[(size_t)ri][v] : -1;
        const float clk = eplc_spin_clock(R, root, t, endTick);
        const float ang = ebld_move_angle(mv, o ? o->face : -1, clk);
        ebxf_turn(out.spin[v], mv.a, mv.a + 3, ang);
        out.spinOn[v] = 1;
    }
    out.W.resize(R.actor.size());
    for (size_t i = 0; i < R.actor.size(); i++) {
        const EplcActor& A = R.actor[i];
        const EbXf L = eplc_local(A, t);
        if (A.parent < 0) { out.W[i] = L; continue; }
        EbXf Pw = out.W[(size_t)A.parent];
        /* the riders of a turning part split between actors turn with it */
        if (R.actor[(size_t)A.parent].flags & EPA_SPLIT) {
            for (size_t v = 0; v < nm; v++)
                if (P.moveRoot[(size_t)ri][v] == A.parent && out.spinOn[v] == 1) {
                    Pw = ebxf_mul(Pw, out.spin[v]);
                    break;
                }
        }
        out.W[i] = ebxf_mul(Pw, L);
    }
}

/* Whether actor a's frame already carries the turn of moving part v (it rides the split
   part's root): its own share then needs no second turn. */
static bool eplc_rides_turn(const EplcModel& P, int ri, int a, int v)
{
    const EplcRig& R = P.rig[(size_t)ri];
    for (int p = R.actor[(size_t)a].parent; p >= 0; p = R.actor[(size_t)p].parent)
        if ((R.actor[(size_t)p].flags & EPA_SPLIT) && P.moveRoot[(size_t)ri][(size_t)v] == p) return true;
    return false;
}

/* ---- the clip planes ----------------------------------------------------------------- */
struct EplcPlanes { int n; GLdouble eq[5][4]; };
static void eplc_planes(const EplcModel& P, int ri, const EplcPose& Q, int a, float cx,
                        float cz, float base, EplcPlanes& out)
{
    const EplcRig& R = P.rig[(size_t)ri];
    const EplcActor& A = R.actor[(size_t)a];
    out.n = 0;
    /* the ground, under everything */
    out.eq[0][0] = 0.0; out.eq[0][1] = 1.0; out.eq[0][2] = 0.0; out.eq[0][3] = -(double)base;
    out.n = 1;
    for (size_t c = 0; c < A.clip.size() && out.n < 5; c++) {
        const EplcClip& C = A.clip[c];
        EbXf F;
        ebxf_id(F);
        if (C.in >= 0) {
            F = Q.W[(size_t)C.in];
            if (R.actor[(size_t)C.in].flags & EPA_SPLIT)
                for (size_t v = 0; v < Q.spin.size(); v++)
                    if (P.moveRoot[(size_t)ri][v] == C.in && Q.spinOn[v] == 1) { F = ebxf_mul(F, Q.spin[v]); break; }
        }
        float p[3], n[3];
        for (int i = 0; i < 3; i++) {
            p[i] = F.R[i * 3] * C.p[0] + F.R[i * 3 + 1] * C.p[1] + F.R[i * 3 + 2] * C.p[2] + F.t[i];
            n[i] = F.R[i * 3] * C.n[0] + F.R[i * 3 + 1] * C.n[1] + F.R[i * 3 + 2] * C.n[2];
        }
        const double wx = cx + p[0] * MODEL_SCALE, wy = base + p[1] * MODEL_SCALE,
                     wz = cz + p[2] * MODEL_SCALE;
        GLdouble* e = out.eq[out.n++];
        e[0] = n[0]; e[1] = n[1]; e[2] = n[2];
        e[3] = -(n[0] * wx + n[1] * wy + n[2] * wz);
    }
}
static int g_eplcPlanesOn = 0;
static void eplc_planes_set(const EplcPlanes* pl)
{
    const int want = pl ? pl->n : 0;
    for (int i = 0; i < want; i++) {
        glClipPlane(GL_CLIP_PLANE1 + i, pl->eq[i]);
        if (i >= g_eplcPlanesOn) glEnable(GL_CLIP_PLANE1 + i);
    }
    for (int i = want; i < g_eplcPlanesOn; i++) glDisable(GL_CLIP_PLANE1 + i);
    g_eplcPlanesOn = want;
}

/* A flipbook's frame at a given engine time (ebld_uv_set's, on a clock of our own). */
static void eplc_uv_at(const EbldModel& M, int texIdx, float tick)
{
    float su = 1.0f, sv = 1.0f, ou = 0.0f, ov = 0.0f;
    if (texIdx >= 0 && texIdx < (int)M.tex.size()) {
        const EbldTex& T = M.tex[(size_t)texIdx];
        if (T.grid > 1) {
            int f = 0;
            if (T.frames > 1 && T.fpsf > 0.0f) {
                f = (int)floorf(tick / 15.0f * T.fpsf);
                f %= T.frames;
                if (f < 0) f += T.frames;
            }
            su = sv = 1.0f / (float)T.grid;
            ou = (float)(f % T.grid) / (float)T.grid;
            ov = (float)(f / T.grid) / (float)T.grid;
        }
    }
    g_meshUVScaleU = su; g_meshUVScaleV = sv;
    g_meshUVOffU = ou;   g_meshUVOffV = ov;
}

/* A grown part at growth k: every vertex past the tip is carried, in its own cross
   section, to the tip (turned from its segment's direction to the tip's), as rigGrow. */
static void eplc_grow_write(EplcModel& P, EplcGrow& G, float k)
{
    if (P.growMesh < 0 || G.k == k) return;
    G.k = k;
    PackMesh& gm = g_pack.mesh[(size_t)P.growMesh];
    gm.gpuPoseRevision++;
    gm.xzReach = -1.0f;
    struct Tip { float s, at[3]; int j; };
    std::vector<Tip> tip(G.P.size());
    for (size_t p = 0; p < G.P.size(); p++) {
        const std::vector<EplcSeg>& sg = G.P[p];
        Tip& T = tip[p];
        T.s = k * G.plen[p];
        T.j = 0;
        while (T.j < (int)sg.size() - 1 && sg[(size_t)T.j + 1].cum < T.s) T.j++;
        const EplcSeg& s = sg[(size_t)T.j];
        for (int c = 0; c < 3; c++) T.at[c] = s.a[c] + s.d[c] * (T.s - s.cum);
    }
    for (int t = G.t0; t < G.t1; t++)
        for (int q = 0; q < 3; q++) {
            const size_t vi = (size_t)((t - G.t0) * 3 + q);
            PackVert& v = gm.tris[(size_t)t].v[q];
            const float* rest = &G.rest[vi * 3];
            const Tip& T = tip[(size_t)G.path[vi]];
            if (k >= 1.0f || G.at[vi] <= T.s) { v.x = rest[0]; v.y = rest[1]; v.z = rest[2]; continue; }
            const EplcSeg& s = G.P[(size_t)G.path[vi]][(size_t)G.seg[vi]];
            const EplcSeg& st = G.P[(size_t)G.path[vi]][(size_t)T.j];
            float on[3], r[3];
            for (int c = 0; c < 3; c++) {
                on[c] = s.a[c] + s.d[c] * (G.at[vi] - s.cum);
                r[c] = rest[c] - on[c];
            }
            /* the turn taking the vertex's segment direction to the tip's */
            const float* u = s.d;
            const float* w = st.d;
            const float cs = u[0] * w[0] + u[1] * w[1] + u[2] * w[2];
            float out[3] = { r[0], r[1], r[2] };
            if (cs < 0.999999f) {
                float ax[3] = { u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0] };
                float sn = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
                if (sn < 1e-6f) {
                    /* opposite: half a turn about any axis square to it */
                    ax[0] = fabsf(u[0]) < 0.9f ? 0.0f : -u[1];
                    ax[1] = fabsf(u[0]) < 0.9f ? -u[2] : u[0];
                    ax[2] = fabsf(u[0]) < 0.9f ? u[1] : 0.0f;
                    sn = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
                }
                EbXf X;
                const float zero[3] = { 0.0f, 0.0f, 0.0f };
                ax[0] /= sn; ax[1] /= sn; ax[2] /= sn;
                ebxf_turn(X, zero, ax, atan2f(sqrtf(fmaxf(0.0f, 1.0f - cs * cs)), cs));
                for (int c = 0; c < 3; c++) out[c] = X.R[c * 3] * r[0] + X.R[c * 3 + 1] * r[1] + X.R[c * 3 + 2] * r[2];
            }
            v.x = T.at[0] + out[0]; v.y = T.at[1] + out[1]; v.z = T.at[2] + out[2];
        }
}

/* A bent part at progress k (the morph's eased share, 1 once over): every vertex at its
   rest plus its offset times 1 - k, as the lab's rigPose. */
static void eplc_morph_write(EplcModel& P, EplcGrow& G, float k)
{
    if (P.growMesh < 0 || G.k == k) return;
    G.k = k;
    PackMesh& gm = g_pack.mesh[(size_t)P.growMesh];
    gm.gpuPoseRevision++;
    gm.xzReach = -1.0f;
    const float w = k >= 1.0f ? 0.0f : 1.0f - k;
    for (int t = G.t0; t < G.t1; t++)
        for (int q = 0; q < 3; q++) {
            const size_t vi = (size_t)((t - G.t0) * 3 + q);
            PackVert& v = gm.tris[(size_t)t].v[q];
            v.x = G.rest[vi * 3] + G.off[vi * 3] * w;
            v.y = G.rest[vi * 3 + 1] + G.off[vi * 3 + 1] * w;
            v.z = G.rest[vi * 3 + 2] + G.off[vi * 3 + 2] * w;
        }
}

/* ---- where a building stands in its placement --------------------------------------- */
static const EplcBld* eplc_find(const SimObject& o)
{
    if (g_eplcBld.empty()) return NULL;
    std::map<long long, EplcBld>::const_iterator it = g_eplcBld.find((long long)smooth_key(o));
    if (it == g_eplcBld.end()) return NULL;
    const EplcBld& E = it->second;
    if (strcmp(E.type, o.type) || E.cx != o.cx || E.cy != o.cy) return NULL;
    return &E;
}
static bool eplc_selling(const SimObject& o)
{
    return building_constructing(o) && !strcmp(o.mission, "Selling");
}

/* The seconds into its placement a building is drawn at now, -1 when the placement does
   not own it this frame. *sell: it is being sold (the time runs back). */
static float eplc_time_of(const EplcModel& P, const SimObject& o, const EplcBld* E, bool* sell)
{
    const EplcRig& R = P.rig[0];
    *sell = false;
    const bool cons = building_constructing(o);
    if ((E && E->mode == EPS_SELL) || eplc_selling(o)) {
        if (!cons) return -1.0f;
        *sell = true;
        float f = construction_frac(o);
        if (f < 0.0f) f = 0.0f;
        if (f > 1.0f) f = 1.0f;
        return R.length * f;
    }
    if (!E) return cons ? 0.0f : -1.0f;
    if (E->cut) return cons ? R.length : -1.0f;
    float t = (engine_time() - (float)E->start) / 15.0f;
    if (t < 0.0f) t = 0.0f;
    if (t >= R.length) { if (!cons) return -1.0f; t = R.length; }
    return t;
}

/* Whether the placement still holds this building (going up or being sold, or its
   placement still playing after the engine's build-up ended): the remastered building's
   own idle effects and the cartridge's wait for it. */
static bool eplc_holds(const SimObject& o)
{
    if (o.kind != K_BUILDING || g_eplcBld.empty() || !eplc_rule_now()) return false;
    const EplcBld* E = eplc_find(o);
    if (!E || E->cut) return false;
    if (E->mode == EPS_SELL) return true;
    const int m = E->model;
    if (m < 0 || m >= (int)g_eplc.size()) return false;
    return (float)(g_engineFrame - E->start) < g_eplc[(size_t)m].rig[0].length * 15.0f;
}

/* Whether a type's one placement jolt is the placement's own (it plays later than the
   press: the Construction Yard's, as its MCV's box lands on the slab), so the game's jolt
   at the press stands down for it. */
static bool eplc_owns_jolt(const char* type)
{
    const int m = eplc_model_of(type);
    return m >= 0 && g_eplc[(size_t)m].jolt > 0.0f;
}

/* Whether a type's placement lands with a splash (EPX_SPLASH) rather than the cinematic
   impact. */
static bool eplc_splashes(const char* type)
{
    const int m = eplc_model_of(type);
    return m >= 0 && (g_eplc[(size_t)m].flags & EPX_SPLASH) != 0;
}

/* ---- the draw -------------------------------------------------------------------------- */
/* THE CARD MOVES THE RIGID PIECES. draw_mesh rebuilds a run on the CPU and uploads it again
   whenever the extra matrix it is handed changes, in every pass that draws it, and while a
   placement plays every part's matrix changes every frame: hundreds of uploads a frame for
   as long as a structure rose. A run whose motion is RIGID (a turn and a shift, no scale) is
   drawn AT REST instead, which the batch keeps on the card after its first frame exactly as
   it keeps a standing building's still pieces, and its motion goes on the modelview stack:
   the world-space matrix that carries it from where it stands at rest to where the
   placement has it now. The cuts are set before the push, in eye space, so they stay where
   the lab put them; the cull reads the pushed matrices; and the normal and specular
   targets, which read the normal attribute as the batch built it (at rest), turn it by the
   same motion in their vertex shader (uPose, ebld_mod.h).
   THE RUNS KEEP THE MODEL'S OWN ORDER. Parts of a model meet in faces that lie in one
   plane (the Temple of Nod's spine sliding along its ridge over the roof blades): where two
   such faces overlap, the one drawn last shows, so drawing the parts in another order
   changes the picture.
   EVERYTHING ELSE KEEPS THE CPU PATH, which is the arithmetic the approved placements were
   judged with: the vehicle a yard deploys from (its parts fold and are squashed flat onto
   one another, the Red Alert yard's gantry onto the MCV's bed, where the GPU's rounding
   decides a different face than the CPU's did), a part a motion scales, the grown and bent
   parts, whose vertices move every frame anyway, a fill, and a run collected into a
   fragment scope (a wreck's pieces), whose draws are replayed after the matrix would have
   been popped. OPENCNC3D_EPLC_CARD=0 puts every run on the CPU path, the A/B for this. */
struct EplcCard { float W[16]; };
static void eplc_card_matrix(const EbXf& X, float cx, float cz, float baseY, int face,
                             EplcCard& C)
{
    /* draw_mesh places a corner v at anchor + S F (X v): F the facing, S the model scale.
       At rest it stands at anchor + S F v, so the motion in world space is the turn
       F R F' about the anchor and the shift S F t. */
    float rs, rc;
    facing_rot((float)face, &rs, &rc);
    const float F[9] = { rc, 0.0f, rs,  0.0f, 1.0f, 0.0f,  -rs, 0.0f, rc };
    float FR[9], T[9];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            float s = 0.0f;
            for (int k = 0; k < 3; k++) s += F[i * 3 + k] * X.R[k * 3 + j];
            FR[i * 3 + j] = s;
        }
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            float s = 0.0f;
            for (int k = 0; k < 3; k++) s += FR[i * 3 + k] * F[j * 3 + k];
            T[i * 3 + j] = s;
        }
    const float anchor[3] = { cx, baseY, cz };
    for (int col = 0; col < 3; col++) {
        for (int row = 0; row < 3; row++) C.W[col * 4 + row] = T[row * 3 + col];
        C.W[col * 4 + 3] = 0.0f;
    }
    for (int i = 0; i < 3; i++) {
        const float ft = F[i * 3] * X.t[0] + F[i * 3 + 1] * X.t[1] + F[i * 3 + 2] * X.t[2];
        C.W[12 + i] = anchor[i] + ft * MODEL_SCALE
                    - (T[i * 3] * anchor[0] + T[i * 3 + 1] * anchor[1] + T[i * 3 + 2] * anchor[2]);
    }
    C.W[15] = 1.0f;
}
/* Whether a motion is a turn and a shift and nothing else: its 3x3 orthonormal, not a mirror. */
static bool eplc_rigid(const EbXf& X)
{
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            float s = 0.0f;
            for (int k = 0; k < 3; k++) s += X.R[i * 3 + k] * X.R[j * 3 + k];
            if (fabsf(s - (i == j ? 1.0f : 0.0f)) > 1.0e-4f) return false;
        }
    const float* r = X.R;
    return r[0] * (r[4] * r[8] - r[5] * r[7]) - r[1] * (r[3] * r[8] - r[5] * r[6])
         + r[2] * (r[3] * r[7] - r[4] * r[6]) > 0.0f;
}
/* The turn for the normals, on the program drawing the normal or specular target. */
static void eplc_pose_uniform(GLuint prog, const float* m)
{
    static GLuint forProg = 0;
    static GLint loc = -1;
    if (prog != forProg) { forProg = prog; loc = fx_glGetUniformLocation(prog, "uPose"); }
    if (loc >= 0) fx_glUniformMatrix4fv(loc, 1, GL_FALSE, m);
}
/* Runs drawn each way since the last dump (eplcdump): the card's and the CPU's. */
static long g_eplcCardRuns = 0, g_eplcCpuRuns = 0;
/* One run: at rest on the card with its motion pushed (card), or with its motion applied on
   the CPU through draw_mesh's extra matrix. */
static void eplc_draw_run(int mesh, const EbXf& X, bool card, float cx, float cz, float base,
                          int face, int pass, int house, float lift, int a, int b, GLint prog)
{
    if (!card || g_meshFragmentCollect || !mesh_perf_switch("OPENCNC3D_EPLC_CARD")) {
        float Mx[12];
        ebxf_mat(X, Mx);
        draw_mesh(mesh, cx, cz, face, pass, 0, 0.0f, house, 1.0f, lift, false,
                  WOBBLE_NONE, -1.0f, Mx, (size_t)a, (size_t)b);
        g_eplcCpuRuns++;
        return;
    }
    EplcCard C;
    eplc_card_matrix(X, cx, cz, base, face, C);
    const GlsClipRestore clipWas;
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glMultMatrixf(C.W);
    if (g_meshNrmOut && prog) eplc_pose_uniform((GLuint)prog, C.W);
    draw_mesh(mesh, cx, cz, face, pass, 0, 0.0f, house, 1.0f, lift, false,
              WOBBLE_NONE, -1.0f, NULL, (size_t)a, (size_t)b);
    if (g_meshNrmOut && prog) eplc_pose_uniform((GLuint)prog, EBLD_POSE_IDENTITY);
    glPopMatrix();
    clipWas.restored();
    g_eplcCardRuns++;
}
static long g_eplcDrawRuns = 0;
/* Whether two runs are cut by the same planes, to the bit. */
static bool eplc_planes_same(const EplcPlanes& a, const EplcPlanes& b)
{
    return a.n == b.n && !memcmp(a.eq, b.eq, sizeof(a.eq[0]) * (size_t)a.n);
}
/* THE RUNS, ONE DRAW FOR A STRETCH THAT MOVES AS ONE. In the model's own order a part's
   triangles lie in many short runs between other parts' (the Power Plant's fourteen parts are
   284 runs), and each run is a draw in every pass. Consecutive runs of the model drawn the same
   way (both on the card or both on the CPU), with transforms equal to the bit and the same
   cuts, are drawn as one stretch: the same triangles in the same order through the same
   matrix, so the same picture, in one draw. Parts that have arrived at rest are the common
   case. A flipbook's run, a fill's and a grown or bent part's are drawn alone, as before. */
static void eplc_draw_runs(EplcModel& P, int ri, const EplcPose& Q, const SimObject* o,
                           int pass, float cx, float cz, int face, int house, bool clip,
                           float base, float lift, GLint prog)
{
    const EbldModel& M = g_ebld[(size_t)P.ebld];
    const EplcRig& R = P.rig[(size_t)ri];
    const int imi = M.mesh[EBM_INTACT];
    int planesFor = -2;
    EplcPlanes pl;
    pl.n = 0;
    /* the stretch waiting to be drawn */
    struct Pend { bool on, card; int a, b; EbXf X; EplcPlanes pl; } pend;
    pend.on = false;
    const auto flush = [&]() {
        if (!pend.on) return;
        if (clip) eplc_planes_set(&pend.pl);
        eplc_draw_run(imi, pend.X, pend.card, cx, cz, base, face, pass, house, lift, pend.a, pend.b, prog);
        pend.on = false;
    };
    for (size_t i = 0; i < P.run[(size_t)ri].size(); i++) {
        const EplcRun& q = P.run[(size_t)ri][i];
        int mesh = imi;
        if (q.grow >= 0) flush();             /* (a grown part's write comes after what precedes it) */
        if (q.grow >= 0 && P.grow[(size_t)ri][(size_t)q.grow].kind == EPG_MORPH) {
            /* a bent part: drawn from its copy at the morph's progress, start to end */
            EplcGrow& G = P.grow[(size_t)ri][(size_t)q.grow];
            float k, b;
            eplc_progress(R.actor[(size_t)G.actor].mo[(size_t)G.motion], Q.t, &k, &b);
            eplc_morph_write(P, G, k);
            mesh = P.growMesh;
        } else if (q.grow >= 0) {
            EplcGrow& G = P.grow[(size_t)ri][(size_t)q.grow];
            const EplcMotion& m = R.actor[(size_t)G.actor].mo[(size_t)G.motion];
            float k, b;
            eplc_progress(m, Q.t, &k, &b);
            if (k < 1.0f) {
                float u = (Q.t - m.delay) / (m.len > 1e-6f ? m.len : 1e-6f);
                u = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
                k = eplc_ease(m, u);
            }
            /* not started: nothing of it is drawn (gathered at its anchor it would hang
               in the air before what it grows out of has arrived) */
            if (!(k > 0.0f)) continue;
            eplc_grow_write(P, G, k);
            mesh = P.growMesh;
        }
        EbXf X = Q.W[(size_t)q.actor];
        float fillY = 3.0e38f;
        if (q.move >= 0 && q.move < (int)Q.spinOn.size()) {
            if (Q.spinOn[(size_t)q.move] == 1) {
                if (!eplc_rides_turn(P, ri, q.actor, q.move)) X = ebxf_mul(X, Q.spin[(size_t)q.move]);
            } else if (Q.spinOn[(size_t)q.move] == 0 && o) {
                float dummy[12], seen = 0.0f;
                if (!ebld_move_pose(M.move[(size_t)q.move], *o, dummy, &fillY, &seen)) continue;
            }
        }
        if (clip && planesFor != q.actor) {
            eplc_planes(P, ri, Q, q.actor, cx, cz, base, pl);
            planesFor = q.actor;
        }
        if (g_meshNrmOut) P.normalDrawn++;
        g_eplcDrawRuns++;
        /* (the fill level clamps a corner before its motion: a fill keeps the CPU path) */
        const bool card = q.grow < 0 && fillY >= 1.0e38f && eplc_rigid(X);
        if (q.grow < 0 && q.flip < 0 && fillY >= 1.0e38f) {
            if (pend.on && q.a == pend.b && card == pend.card && !memcmp(&X, &pend.X, sizeof X)
                && (!clip || eplc_planes_same(pl, pend.pl))) {
                pend.b = q.b;
                continue;
            }
            flush();
            pend.on = true; pend.card = card; pend.a = q.a; pend.b = q.b; pend.X = X; pend.pl = pl;
            continue;
        }
        flush();
        if (clip) eplc_planes_set(&pl);
        if (q.flip >= 0) eplc_uv_at(M, q.flip, Q.t >= R.length ? engine_time() : Q.spinTick);
        g_meshFillY = fillY;
        eplc_draw_run(mesh, X, card, cx, cz, base, face, pass, house, lift, q.a, q.b, prog);
        g_meshFillY = 3.0e38f;
        if (q.flip >= 0) ebld_uv_clear();
    }
    flush();
}

/* Whether the placement draws this building this frame (going up, being sold, or its
   placement still playing): the cartridge's MCV deploy rig, which the main passes and the
   sun's ask for first, stands down for it. */
static bool eplc_owns(const SimObject& o)
{
    if (o.kind != K_BUILDING || o.id < 0 || g_placeGhostPass || g_eplc.empty()) return false;
    const int pm = eplc_model_of(o.type);
    if (pm < 0 || (g_eplc[(size_t)pm].flags & EPX_WALL) || object_model3d(o) >= 0) return false;
    bool sell = false;
    return eplc_time_of(g_eplc[(size_t)pm], o, eplc_find(o), &sell) >= 0.0f;
}

/* Asked by draw_object_mesh, for a remastered building, before anything else:
   true when the placement (or the sell) owns the building this frame and has drawn the
   pass. */
static bool eplc_draw_object(int mi, const SimObject& o, int pass)
{
    (void)mi;
    if (o.kind != K_BUILDING || o.id < 0 || g_placeGhostPass || g_eplc.empty()) return false;
    const int pm = eplc_model_of(o.type);
    if (pm < 0) return false;
    EplcModel& P = g_eplc[(size_t)pm];
    if (P.flags & EPX_WALL) return false;
    /* the authored model wins where one is installed and on, as over the remaster */
    if (object_model3d(o) >= 0) return false;
    const EplcBld* E = eplc_find(o);
    bool sell = false;
    const float t = eplc_time_of(P, o, E, &sell);
    if (t < 0.0f) return false;
    EbldModel& M = g_ebld[(size_t)P.ebld];
    if (!ebld_resident(M)) return false;
    const EplcRig& R = P.rig[0];
    /* the moment t reaches the end: a sell starts there and runs back, so its turning parts
       start from the angle they stood at and spin back down */
    const float endTick = sell ? (E ? (float)E->start : engine_time())
                        : E ? (float)E->start + R.length * 15.0f
                            : engine_time() + (R.length - t) * 15.0f;
    EplcPose Q;
    eplc_pose(P, 0, &o, t, endTick, Q);
    const float cx = o.wx, cz = o.wz;
    const float base = terrain_y(cx, cz) + alt_lift(o);
    const int face = draw_facing(o), house = mesh_house(o);
    /* the cuts while it moves; at rest it is the model as it stands, uncut */
    const bool atRest = !sell && t >= R.length;
    GLint prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    const bool clip = !atRest && (prog == 0 || g_meshNrmOut);
    if (pass == MODE_OPAQUE && !ebld_aux_pass()) P.drawn++;
    g_meshFlash = g_flashOn && o.blush != 0;
    MeshArrayScope arrays;    /* the runs share the client arrays, as a standing building's pieces do */
    if (pass == MODE_SHADOW) {
        /* the cartridge's ground-shadow plate, with the slab */
        if (clip) { EplcPlanes pl; eplc_planes(P, 0, Q, R.slab, cx, cz, base, pl); eplc_planes_set(&pl); }
        const EbXf& S = Q.W[(size_t)R.slab];
        eplc_draw_run(M.mesh[EBM_INTACT], S, eplc_rigid(S), cx, cz, base, face, pass, house,
                      alt_lift(o), M.shadow0, M.shadow1, prog);
    } else {
        eplc_draw_runs(P, 0, Q, &o, pass, cx, cz, face, house, clip, base, alt_lift(o), prog);
        /* the vehicle it deploys from, while it plays */
        if (!atRest && P.evhc >= 0 && P.evhc < (int)g_evhc.size()) {
            EvhcModel& V = g_evhc[(size_t)P.evhc];
            if (evhc_resident(V, false)) {
                if (g_meshNrmOut) {
                    fx_set1f((GLuint)prog, "uClass", fx_vehicle_active(&g_fx) ? 0.34f : EVHC_NORMAL_CLASS);
                    fx_set1f((GLuint)prog, "uEmitClass", fx_vehicle_active(&g_fx) ? 0.22f : EBLD_SELFLIT_CLASS);
                }
                EbXf St;
                const float zero[3] = { 0.0f, 0.0f, 0.0f }, up[3] = { 0.0f, 1.0f, 0.0f };
                ebxf_turn(St, zero, up, P.compYaw * (EBLD_PI / 180.0f));
                St.t[0] = P.compShift[0]; St.t[2] = P.compShift[1];
                int planesFor = -2;
                EplcPlanes pl;
                for (size_t i = 0; i < P.compRun.size(); i++) {
                    const EplcRun& q = P.compRun[i];
                    if (clip && planesFor != q.actor) {
                        eplc_planes(P, 0, Q, q.actor, cx, cz, base, pl);
                        eplc_planes_set(&pl);
                        planesFor = q.actor;
                    }
                    if (g_meshNrmOut) P.companionNormalDrawn++;
                    /* (always the CPU path: THE CARD MOVES THE RIGID PIECES says why) */
                    eplc_draw_run(V.m.mesh[EBM_INTACT], ebxf_mul(Q.W[(size_t)q.actor], St), false,
                                  cx, cz, base, face, pass, house, alt_lift(o), q.a, q.b, prog);
                }
            }
        }
    }
    eplc_planes_set(NULL);
    if (g_meshNrmOut) {
        fx_set1f((GLuint)prog, "uClass", EBLD_NORMAL_CLASS);
        fx_set1f((GLuint)prog, "uEmitClass", EBLD_SELFLIT_CLASS);
    }
    g_meshFlash = false;
    return true;
}

/* ---- the walls ------------------------------------------------------------------------- */
/* Asked by ebld_wall_draw for a wall cell rising t seconds after its start: draws the
   piece's own rig at t and answers true while that plays. */
static bool eplc_wall_draw(int kind, int icon, float cx, float cz, int pass, float t)
{
    /* (the three walls a player builds; under Red Alert its fence too, kind 5, which the
       Soviets build cell by cell) */
    if (t < 0.0f || kind < 0 || (kind > 2 && !(g_raModule && kind == 5)) || icon < 0 || icon > 15
        || g_eplc.empty()) return false;
    const int pm = eplc_model_of(EBLD_WALLS[kind]);
    if (pm < 0) return false;
    EplcModel& P = g_eplc[(size_t)pm];
    if (!(P.flags & EPX_WALL) || icon >= (int)P.rig.size()) return false;
    const EplcRig& R = P.rig[(size_t)icon];
    if (t >= R.length) return false;
    EbldModel& M = g_ebld[(size_t)P.ebld];
    if (!ebld_resident(M)) return false;
    EplcPose Q;
    eplc_pose(P, icon, NULL, t, engine_time(), Q);
    GLint prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (pass == MODE_OPAQUE && !ebld_aux_pass()) P.drawn++;
    /* (draw_walls draws its pieces with the ground per vertex: so does this, and the cuts
       are at the cell centre's ground) */
    MeshArrayScope arrays;
    const EbldWallCull cs = ebld_wall_cull_begin();   /* one-sided, as the whole wall */
    eplc_draw_runs(P, icon, Q, NULL, pass, cx, cz, -1, 0, (prog == 0 || g_meshNrmOut), terrain_y(cx, cz), 0.0f, prog);
    ebld_wall_cull_end(cs);
    eplc_planes_set(NULL);
    return true;
}

/* The longest a wall cell's placement plays, seconds (0 when none is attached): the wall
   line keeps a cell's rise this long. */
static float eplc_wall_seconds(void)
{
    float s = 0.0f;
    if (!eplc_rule_now()) return 0.0f;
    for (size_t i = 0; i < g_eplc.size(); i++)
        if (g_eplc[i].attached && (g_eplc[i].flags & EPX_WALL))
            for (size_t r = 0; r < g_eplc[i].rig.size(); r++)
                if (g_eplc[i].rig[r].length > s) s = g_eplc[i].rig[r].length;
    return s;
}

/* ---- the ring of broken earth ---------------------------------------------------------- */
/* Every placement's ring, in the translucent pass, lying on the terrain: grows in with the
   slab, stays `hold` seconds after the placement ends, fades over `fade`. */
static void eplc_draw_rings(void)
{
    if (g_eplcBld.empty() || !eplc_rule_now() || ebld_aux_pass()) return;
    for (std::map<long long, EplcBld>::const_iterator it = g_eplcBld.begin(); it != g_eplcBld.end(); ++it) {
        const EplcBld& E = it->second;
        if (E.mode != EPS_PLACE || E.model < 0 || E.model >= (int)g_eplc.size()) continue;
        EplcModel& P = g_eplc[(size_t)E.model];
        if (P.ringMesh < 0 || P.ringTex < 0 || P.ringTex >= (int)g_pack.tex.size()) continue;
        const EplcRig& R = P.rig[0];
        const float t = (engine_time() - (float)E.start) / 15.0f;
        const float grow = P.ring[2] * (R.slabLen > 0.0f ? R.slabLen : R.length);
        const float end = R.length + P.ring[3], fade = P.ring[4] > 0.0f ? P.ring[4] : 3.0f;
        float a = P.ring[1] * fminf(1.0f, t / fmaxf(grow, 0.01f));
        float f = (t - end) / fade;
        f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
        a *= 1.0f - f;
        if (!(a > 0.0f)) continue;
        if (!P.ringUp) {
            P.ringUp = true;
            GLint bound = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
            g_pack.tex[(size_t)P.ringTex].gl = n64fix_upload(P.ringPx, P.ringW, P.ringH);
            glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
        }
        const unsigned char was = g_meshAlphaScale;
        g_meshAlphaScale = (unsigned char)(255.0f * (a > 1.0f ? 1.0f : a) + 0.5f);
        const bool gpv = g_meshGroundPerVertex;
        g_meshGroundPerVertex = true;
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -2.0f);
        glDepthMask(GL_FALSE);
        draw_mesh(P.ringMesh, E.wx, E.wz, -1, MODE_XLU, 0, 0.0f, 0, 1.0f, 0.002f);
        glDisable(GL_POLYGON_OFFSET_FILL);
        g_meshGroundPerVertex = gpv;
        g_meshAlphaScale = was;
    }
}

/* ---- the dust ---------------------------------------------------------------------- */
/* One particle of a template, born at world (x, z) at height y, thrown along the unit
   direction (dx, dy, dz) at the template's speed, sized and sped by `scale`: the lab's
   emitter number for number, turned into the particle pool's per-tick terms as the wall
   dust and the remastered buildings' emitters are (15 ticks a second, the drag folded into
   a mean speed and the rise). */
static void eplc_particle(const EplcTmpl& T, float scale, float x, float y, float z,
                          const float* dir, unsigned h)
{
    const float* f = T.f;
    const float sp = f[EPF_SPEED] * scale;
    const float j0 = efx_r8(h) * 2.0f - 1.0f, j1 = efx_r8(h >> 8) * 2.0f - 1.0f,
                j2 = efx_r8(h >> 16) * 2.0f - 1.0f;
    const float v[3] = { f[EPF_VX] + dir[0] * sp + j0 * f[EPF_JITTER],
                         f[EPF_VY] + dir[1] * sp + j1 * f[EPF_JITTER],
                         f[EPF_VZ] + dir[2] * sp + j2 * f[EPF_JITTER] };
    const float life = f[EPF_LIFE0] + efx_r8(h >> 24) * (f[EPF_LIFE1] - f[EPF_LIFE0]);
    int lt = (int)(life * 15.0f + 0.5f);
    if (lt < 1) lt = 1;
    float vm = 1.0f, rise = f[EPF_RISE] - f[EPF_GRAVITY];
    const float kL = f[EPF_DRAG] * life;
    if (f[EPF_DRAG] > 0.0f && kL > 1e-4f) {
        const float ex = expf(-kL);
        const float r0 = rise;
        vm = (1.0f - ex) / kL;
        rise = 2.0f * (r0 / f[EPF_DRAG]) * (life - (1.0f - ex) / f[EPF_DRAG]) / (life * life);
    }
    EfxPart p;
    memset(&p, 0, sizeof p);
    p.x = x; p.z = z; p.y = y;
    p.sx = p.x; p.sy = p.y; p.sz = p.z;
    p.vx = v[0] * vm / 15.0f;
    p.vy = v[1] * vm / 15.0f;
    p.vz = v[2] * vm / 15.0f;
    p.ay = rise / 225.0f;
    p.hw = 0.5f * f[EPF_SIZE0] * scale;
    p.grow = 0.5f * (f[EPF_SIZE1] - f[EPF_SIZE0]) * scale / (float)lt;
    p.life = (short)lt;
    p.sys = 6;                                   /* the smoke system: the SMOKE sheet */
    if (T.sheet == EPS_STEAM) {
        /* the steam sheet, drawn as the landing-zone smoke is; without it (Enhanced effects
           off) the smoke sheet in the template's own colour */
        const unsigned char sh = efx_src_sheet("LandingSmoke");
        if (sh) { p.sys = 1; p.sheet = sh; }
    } else if (T.sheet == EPS_GLOW) {
        p.sys = 0;                               /* the glow system's own sheet */
    }
    const float al = f[EPF_ALPHA] < 0.0f ? 0.0f : (f[EPF_ALPHA] > 1.0f ? 1.0f : f[EPF_ALPHA]);
    const unsigned char a = (unsigned char)(al * 255.0f + 0.5f);
    const float fd = f[EPF_FADE] > 0.01f ? f[EPF_FADE] : 0.2f;
    int ar = (int)((float)a / (fd * (float)lt) + 0.5f);
    if (ar < 1) ar = 1;
    if (ar > 255) ar = 255;
    p.arate = (unsigned char)ar;
    p.r = (unsigned char)f[EPF_R]; p.g = (unsigned char)f[EPF_G]; p.b = (unsigned char)f[EPF_B];
    p.a = a;
    p.seed = h;
    p.bounce = f[EPF_GRAVITY] > 0.0f ? 1 : 0;   /* clods and grit skip off the ground */
    efx_push(p);
}

/* How much an emitter emits now (the lab's fxLive): 1, easing to nothing over until_fade
   before until. */
static float eplc_live(float T, float until, float fade, bool has)
{
    if (!has) return 1.0f;
    if (T >= until) return 0.0f;
    return fade > 0.0f ? fminf(1.0f, (until - T) / fade) : 1.0f;
}

/* n particles of one template from points along an outline (x, z, nx, nz per point, in
   the frame F, at height y in that frame), each thrown outward from its own side. */
static void eplc_emit_outline(const EplcTmpl& T, float scale, const std::vector<float>& pts,
                              float y, const EbXf& F, float wx, float wz, float base, int n,
                              unsigned hk)
{
    const size_t np = pts.size() / 4;
    if (!np || n <= 0) return;
    for (int i = 0; i < n; i++) {
        const unsigned h = efx_hash(hk, 0x9E3779B9u, (unsigned)i, 0x45504C43u);
        const size_t k = (size_t)(efx_hash(h, 0x7F4A7C15u, 3u, 0u) % (unsigned)np);
        const float px = pts[k * 4], pz = pts[k * 4 + 1], nx = pts[k * 4 + 2], nz = pts[k * 4 + 3];
        float p[3], d[3];
        const float lp[3] = { px, y, pz };
        float ld[3] = { nx * T.f[EPF_OUT], 0.0f, nz * T.f[EPF_OUT] };
        for (int c = 0; c < 3; c++) {
            p[c] = F.R[c * 3] * lp[0] + F.R[c * 3 + 1] * lp[1] + F.R[c * 3 + 2] * lp[2] + F.t[c];
            d[c] = F.R[c * 3] * ld[0] + F.R[c * 3 + 1] * ld[1] + F.R[c * 3 + 2] * ld[2];
        }
        d[1] += T.f[EPF_UP];
        const float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (l > 0.0f) { d[0] /= l; d[1] /= l; d[2] /= l; }
        eplc_particle(T, scale, wx + p[0] * MODEL_SCALE, base + p[1] * MODEL_SCALE,
                      wz + p[2] * MODEL_SCALE, d, h);
    }
}

/* This tick's count from an accumulator (the lab's `acc`): a whole particle each time it
   passes one. */
static int eplc_take(float& acc, float add)
{
    acc += add;
    const int n = (int)floorf(acc);
    if (n > 0) acc -= (float)n;
    return n;
}

/* The outline points of the footprint for one slab template: a point every `spacing`
   along each side, `edge` outside it, each with its side's outward normal. */
static void eplc_foot_points(const EplcModel& P, const EplcTmpl& T, std::vector<float>& out)
{
    out.clear();
    const size_t n = P.hull.size() / 2;
    const float sp = T.f[EPF_SPACING] > 0.0f ? T.f[EPF_SPACING] : 0.1f;
    for (size_t j = 0; j < n; j++) {
        const float ax = P.hull[j * 2] * MODEL_SCALE, az = -P.hull[j * 2 + 1] * MODEL_SCALE;
        const float bx = P.hull[((j + 1) % n) * 2] * MODEL_SCALE, bz = -P.hull[((j + 1) % n) * 2 + 1] * MODEL_SCALE;
        /* (in the lab's x, y: y north) */
        const float len = hypotf(bx - ax, bz - az);
        if (len < 1e-6f) continue;
        const float nx = (bz - az) / len, ny = -(bx - ax) / len;
        const int k = (int)fmaxf(1.0f, floorf(len / sp + 0.5f));
        for (int i = 0; i < k; i++) {
            const float u = ((float)i + 0.5f) / (float)k;
            const float x = ax + (bx - ax) * u + nx * T.f[EPF_EDGE];
            const float y = az + (bz - az) * u + ny * T.f[EPF_EDGE];
            out.push_back(x / MODEL_SCALE);
            out.push_back(-y / MODEL_SCALE);
            out.push_back(nx);
            out.push_back(-ny);
        }
    }
}

/* Whether an actor is passing through the slab's top at t, and how fast (cells a second):
   its height against the slab's, now and a tick ago. An actor rising through another
   surface of the slab (THROUGH: a statue out of its rock's stump) is judged against that
   surface instead, as the lab's rigPose. */
static float eplc_emerging(const EplcModel& P, int ri, const EplcPose& Q, const EplcPose& Q0,
                           int a)
{
    const EplcRig& R = P.rig[(size_t)ri];
    const EplcActor& A = R.actor[(size_t)a];
    const EbXf& S = Q.W[(size_t)R.slab];
    const EbXf& S0 = Q0.W[(size_t)R.slab];
    /* (the slab only slides: its height is its translation) */
    const float dy = (Q.W[(size_t)a].t[1] - S.t[1]) * MODEL_SCALE;
    const float dy0 = (Q0.W[(size_t)a].t[1] - S0.t[1]) * MODEL_SCALE;
    const float dt = Q.t - Q0.t;
    const float v = fabsf(dt) > 1e-6f ? fabsf(dy - dy0) / fabsf(dt) : 0.0f;
    const float zmin = A.zmin * MODEL_SCALE, zmax = A.zmax * MODEL_SCALE,
                top = ((A.flags & EPA_THROUGH) ? A.emerge.through : R.slabTop) * MODEL_SCALE;
    const bool spans = zmin + dy < top - 1e-4f && zmax + dy > top;
    if (!spans) return 0.0f;
    const float vref = P.emerge[2] > 0.0f ? P.emerge[2] : 0.6f;
    return fminf(1.0f, v / vref);
}

/* One rig's dust for one tick: the emergence dust of every part passing through the slab,
   the puffs (EPLC_PUFFS), and, placing, the slab's own ground-breaking dust along the
   footprint. acc and next are the building's accumulators. */
static void eplc_dust(EplcModel& P, int ri, const SimObject* o, float t, float t0, bool sell,
                      float wx, float wz, float base, std::vector<float>& acc,
                      std::vector<float>& next, unsigned key, int frame)
{
    const EplcRig& R = P.rig[(size_t)ri];
    EplcPose Q, Q0;
    eplc_pose(P, ri, o, t, 0.0f, Q);
    eplc_pose(P, ri, o, t0, 0.0f, Q0);
    const size_t nt = P.tmpl.size();
    const size_t need = (R.actor.size() + 1) * nt;
    if (acc.size() != need) acc.assign(need, 0.0f);
    if (next.size() != nt) next.assign(nt, -1.0f);
    const float ref = P.emerge[0] > 0.0f ? P.emerge[0] : 1.2f, kmin = P.emerge[1];
    for (size_t a = 0; a < R.actor.size(); a++) {
        const EplcActor& A = R.actor[a];
        if (!(A.flags & (EPA_EMERGE | EPA_PUFF))) continue;
        const float act = (A.flags & EPA_EMERGE) ? eplc_emerging(P, ri, Q, Q0, (int)a) : 0.0f;
        for (size_t k = 0; k < nt; k++) {
            const EplcTmpl& T = P.tmpl[k];
            float rate = 0.0f, scale = 1.0f;
            const EplcOutline* O = NULL;
            /* (`boost`, version 2: the size, speed and rate taken that many times, the
               lab's rigEmitters; 1 without) */
            if (T.role == EPT_EMERGE && (A.flags & EPA_EMERGE)) {
                O = &A.emerge;
                scale = fminf(1.0f, fmaxf(kmin, O->span / ref)) * O->boost;
                rate = T.f[EPF_RATE] * O->edge * O->boost * act;
            } else if (T.role == EPT_PUFF && (A.flags & EPA_PUFF) && EPLC_PUFFS && !sell) {
                O = &A.puff;
                if (t < O->at || t >= O->at + O->len) continue;
                scale = fminf(1.0f, fmaxf(kmin, O->span / ref)) * O->boost;
                rate = T.f[EPF_RATE] * O->edge * O->boost
                     * eplc_live(t, O->at + O->len, O->len / 2.0f, true);
            }
            if (!O || !(rate > 0.0f)) continue;
            const int n = eplc_take(acc[a * nt + k], rate / 15.0f);
            eplc_emit_outline(T, scale, O->pt, O->y, Q.W[(size_t)R.slab], wx, wz, base, n,
                              efx_hash(key, (unsigned)frame, (unsigned)(a * 64 + k), 0x44555354u));
        }
    }
    /* the slab's phase: the ground breaking along the footprint (placing only) */
    if (sell || P.hull.empty()) return;
    const float dur = R.slabLen > 0.0f ? R.slabLen : R.length;
    const float edge = P.foot[2], scale = P.foot[1];
    std::vector<float> pts;
    EbXf ground;
    ebxf_id(ground);
    for (size_t k = 0; k < nt; k++) {
        const EplcTmpl& T = P.tmpl[k];
        if (T.role != EPT_SLAB) continue;
        const float delay = (T.flags & EPTF_DELAY) ? T.f[EPF_DELAY] * dur : 0.0f;
        if (t < delay) continue;
        const float ramp = (T.flags & EPTF_RAMP) && T.f[EPF_RAMP] > 0.0f
                         ? fminf(1.0f, (t - delay) / (T.f[EPF_RAMP] * dur)) : 1.0f;
        const float live = eplc_live(t, T.f[EPF_UNTIL] * dur, T.f[EPF_UNTILFADE] * dur,
                                     (T.flags & EPTF_UNTIL) != 0);
        int n = 0;
        float& ac = acc[R.actor.size() * nt + k];
        if (T.flags & EPTF_BURST) {
            /* bursts of clods: count per cell of outline, then a wait of `every` seconds */
            if (next[k] < 0.0f)
                next[k] = delay + efx_r8(efx_hash(key, 0x434C4F44u, (unsigned)k, 0u)) * T.f[EPF_EVERY0];
            int b = 0;
            while (t >= next[k] && b < 16) {
                const unsigned hb = efx_hash(key, 0x42555253u, (unsigned)k, (unsigned)(next[k] * 1000.0f));
                const float c = (T.f[EPF_COUNT0] + efx_r8(hb) * (T.f[EPF_COUNT1] - T.f[EPF_COUNT0])) * edge;
                int m = (int)fmaxf(1.0f, floorf(c * ramp + 0.5f));
                if (live < 1.0f) m = (int)floorf((float)m * live + 0.5f);
                n += m;
                next[k] += fmaxf(T.f[EPF_EVERY0] + efx_r8(hb >> 8) * (T.f[EPF_EVERY1] - T.f[EPF_EVERY0]), 0.02f);
                b++;
            }
        } else {
            n = eplc_take(ac, T.f[EPF_RATE] * edge * ramp * live / 15.0f);
        }
        if (n <= 0) continue;
        eplc_foot_points(P, T, pts);
        eplc_emit_outline(T, scale, pts, T.f[EPF_Z] / MODEL_SCALE, ground, wx, wz, base, n,
                          efx_hash(key, (unsigned)frame, (unsigned)k, 0x534C4142u));
    }
}

/* The dust of a wall cell rising t seconds after its start, for one tick. Answers false
   when the cell's wall has no placement (its old dust then plays). */
static std::map<int, std::vector<float> > g_eplcWallAcc, g_eplcWallNext;
static bool eplc_wall_dust(int kind, int icon, int cellKey, float cx, float cz,
                           float t, int frame)
{
    if (kind < 0 || (kind > 2 && !(g_raModule && kind == 5))) return false;
    const int pm = eplc_model_of(EBLD_WALLS[kind]);
    if (!g_efxPoolLive) return pm >= 0;
    if (pm < 0 || icon < 0 || icon > 15) return false;
    EplcModel& P = g_eplc[(size_t)pm];
    if (!(P.flags & EPX_WALL)) return false;
    if (t < 0.0f) return true;
    if (t > P.rig[(size_t)icon].length + 0.1f) {
        g_eplcWallAcc.erase(cellKey); g_eplcWallNext.erase(cellKey);
        return true;
    }
    eplc_dust(P, icon, NULL, t, t - 1.0f / 15.0f, false, cx, cz, terrain_y(cx, cz),
              g_eplcWallAcc[cellKey], g_eplcWallNext[cellKey], (unsigned)cellKey * 2654435761u, frame);
    return true;
}

/* ---- the tick ---------------------------------------------------------------------------- */
/* On the tick edge, just before ebld_step: note every building going up or being sold of a
   type with a placement, end the ones that are over or hit, fire the late jolt, and raise
   the dust. */
/* A BUILDING READ AT FIRST SIGHT (Red Alert's: ebld_type_lazy) has no model when the pack is
   loaded, so its placement cannot attach then. The tick it first goes up, its file is read
   (ebld_model_of reads it) and the placement tried again, before its start is looked for,
   so the placement owns it from its first frame. -1: none for the type. */
static int eplc_attach_late(const char* type)
{
    bool waiting = false;
    for (size_t i = 0; i < g_eplc.size() && !waiting; i++)
        waiting = !g_eplc[i].attached && g_eplc[i].type == type;
    if (!waiting || ebld_model_of(type) < 0) return -1;
    eplc_attach(false);
    return eplc_model_of(type);
}

/* RED ALERT'S WALLS are read when its map first shows them (ebld_wall_models), after the pack
   was loaded: a wall placement waiting for its wall's model attaches once that model is read,
   tried once per pack. */
static void eplc_attach_walls_ra(void)
{
    static int serial = -1;
    static std::set<std::string> tried;
    if (serial != g_ebldPackSerial) { serial = g_ebldPackSerial; tried.clear(); }
    bool need = false;
    for (size_t i = 0; i < g_eplc.size(); i++) {
        const EplcModel& P = g_eplc[i];
        if (P.attached || !(P.flags & EPX_WALL) || tried.count(P.type)) continue;
        for (size_t m = 0; m < g_ebld.size() && !need; m++)
            if (g_ebld[m].attached && g_ebld[m].type == P.type && (g_ebld[m].flags & EBX_WALL)) {
                need = true;
                tried.insert(P.type);
            }
    }
    if (need) eplc_attach(false);
}

static void eplc_step(int frame)
{
    if (g_eplc.empty()) return;
    if (frame == g_eplcLastTick) return;
    if (g_raModule && eplc_rule_now()) eplc_attach_walls_ra();
    if (frame < g_eplcLastTick) g_eplcBld.clear();
    g_eplcLastTick = frame;
    for (std::map<long long, EplcBld>::iterator it = g_eplcBld.begin(); it != g_eplcBld.end(); ++it)
        it->second.seen = false;
    const bool rule = eplc_rule_now();
    for (size_t i = 0; i < g_objects.size(); i++) {
        const SimObject& o = g_objects[i];
        if (o.kind != K_BUILDING || o.id < 0 || o.limbo) continue;
        const long long key = (long long)smooth_key(o);
        std::map<long long, EplcBld>::iterator it = g_eplcBld.find(key);
        if (it != g_eplcBld.end()) {
            const EplcBld& E = it->second;
            if (strcmp(E.type, o.type) || E.cx != o.cx || E.cy != o.cy) { g_eplcBld.erase(it); it = g_eplcBld.end(); }
        }
        int pm = rule ? eplc_model_of(o.type) : -1;
        if (pm < 0 && rule && g_raModule && building_constructing(o) && it == g_eplcBld.end())
            pm = eplc_attach_late(o.type);
        if (pm < 0 || (g_eplc[(size_t)pm].flags & EPX_WALL) || o.str <= 0) {
            if (it != g_eplcBld.end()) g_eplcBld.erase(it);
            continue;
        }
        EplcModel& P = g_eplc[(size_t)pm];
        const EplcRig& R = P.rig[0];
        const bool cons = building_constructing(o);
        if (it == g_eplcBld.end()) {
            if (!cons) continue;
            EplcBld E = EplcBld();
            E.model = pm;
            E.mode = eplc_selling(o) ? EPS_SELL : EPS_PLACE;
            E.id = o.id; E.cx = o.cx; E.cy = o.cy;
            snprintf(E.type, sizeof E.type, "%s", o.type);
            E.start = frame;
            E.str0 = o.str;
            E.lastT = -1.0f;
            it = g_eplcBld.insert(std::make_pair(key, E)).first;
            printf("EPLC|start|key=%lld|type=%s|mode=%s|frame=%d|seconds=%.2f\n", key, o.type,
                   E.mode == EPS_SELL ? "sell" : "place", frame, R.length);
        }
        EplcBld& E = it->second;
        E.seen = true;
        E.wx = o.wx; E.wz = o.wz;
        E.fw = o.fw > 0 ? o.fw : 1;
        E.fh = o.fh > 0 ? o.fh : 1;
        E.mine = g_playerHouse[0] && !strcmp(o.house, g_playerHouse);
        if (E.mode == EPS_PLACE && eplc_selling(o)) {
            E.mode = EPS_SELL;
            E.start = frame;
            E.lastT = -1.0f;
            printf("EPLC|end|key=%lld|why=sold|frame=%d\n", key, frame);
        }
        /* A SALE OF A BUILDING IN THE SEA splashes as its placement did (the player's own,
           once, as it starts going down); every other sale is silent here as before */
        if (E.mode == EPS_SELL && !E.jolted && E.mine && (P.flags & EPX_SPLASH)) {
            E.jolted = true;
            cinematic_audio_note_as(CIN_PLACE, E.wx, E.wz, (float)(E.fw > E.fh ? E.fw : E.fh), true);
        }
        const float t = (float)(frame - E.start) / 15.0f;
        if (E.mode == EPS_PLACE && !E.cut && !E.struck && o.str < E.str0) {
            E.struck = true;
            printf("EPLC|hit|key=%lld|frame=%d|t=%.2f|str=%d/%d\n", key, frame, t, o.str, E.str0);
        }
        if (E.mode == EPS_PLACE && !E.cut && o.str <= 0) {
            E.cut = true;
            E.endTick = frame;
            printf("EPLC|end|key=%lld|why=destroyed|frame=%d|t=%.2f\n", key, frame, t);
        }
        if (E.mode == EPS_PLACE && !E.cut && !E.ended && t >= R.length) {
            E.ended = true;
            E.endTick = frame;
            printf("EPLC|end|key=%lld|why=done|frame=%d\n", key, frame);
        }
        /* the late jolt (the Construction Yard's), the game's placement jolt at its moment */
        if (E.mode == EPS_PLACE && E.mine && !E.cut && !E.jolted && P.jolt > 0.0f
            && t >= P.jolt) {
            E.jolted = true;
            shake_tap(P.cam[0] * (float)(E.fw > E.fh ? E.fw : E.fh));
            cinematic_audio_note_as(CIN_PLACE, E.wx, E.wz, (float)(E.fw > E.fh ? E.fw : E.fh),
                                    (P.flags & EPX_SPLASH) != 0);
        }
        /* the dust, while it plays */
        if (!g_efxPoolLive) continue;
        const float base = terrain_y(o.wx, o.wz);
        if (E.mode == EPS_PLACE && !E.cut && t <= R.length + 0.1f)
            eplc_dust(P, 0, &o, t, t - 1.0f / 15.0f, false, o.wx, o.wz, base, E.acc, E.next,
                      (unsigned)key, frame);
        else if (E.mode == EPS_SELL && cons) {
            float f = construction_frac(o);
            f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
            const float ts = R.length * f;
            if (E.lastT >= 0.0f)
                eplc_dust(P, 0, &o, ts, E.lastT, true, o.wx, o.wz, base, E.acc, E.next,
                          (unsigned)key, frame);
            E.lastT = ts;
        }
    }
    /* a building that has left the heap, or whose placement and ring are long over */
    for (std::map<long long, EplcBld>::iterator it = g_eplcBld.begin(); it != g_eplcBld.end();) {
        const EplcBld& E = it->second;
        bool drop = !E.seen;
        if (!drop && E.mode == EPS_PLACE && E.model >= 0 && E.model < (int)g_eplc.size()) {
            const EplcModel& P = g_eplc[(size_t)E.model];
            const float t = (float)(frame - E.start) / 15.0f;
            const float ringEnd = P.rig[0].length + P.ring[3] + P.ring[4];
            if (t > ringEnd + 0.5f && (E.cut || E.ended)) drop = true;
        }
        if (drop) g_eplcBld.erase(it++);
        else ++it;
    }
    fflush(stdout);
}

/* eplcplay: play a standing building's placement again from now, as the lab's Place button
   does, so every type can be looked at and checked from a test mission's own buildings
   (the sidebar of one side builds only some of them). The draw, the dust, the camera and
   the hand-over are the ones a building put down gets; only what starts it differs, and
   the game's jolt at the press is given here as the placement would get it. */
static bool eplc_force(const SimObject& o)
{
    int pm = eplc_model_of(o.type);
    /* (a Red Alert type's placement attaches the first time one goes up: a standing one asked
       to play it attaches it here) */
    if (pm < 0 && g_raModule && o.kind == K_BUILDING) pm = eplc_attach_late(o.type);
    if (pm < 0 || (g_eplc[(size_t)pm].flags & EPX_WALL) || o.kind != K_BUILDING || o.id < 0)
        return false;
    const long long key = (long long)smooth_key(o);
    EplcBld E = EplcBld();
    E.model = pm;
    E.mode = EPS_PLACE;
    E.id = o.id; E.cx = o.cx; E.cy = o.cy;
    snprintf(E.type, sizeof E.type, "%s", o.type);
    E.start = g_engineFrame;
    E.str0 = o.str;
    E.lastT = -1.0f;
    E.wx = o.wx; E.wz = o.wz;
    E.fw = o.fw > 0 ? o.fw : 1;
    E.fh = o.fh > 0 ? o.fh : 1;
    E.mine = g_playerHouse[0] && !strcmp(o.house, g_playerHouse);
    E.seen = true;
    g_eplcBld[key] = E;
    if (E.mine && !(g_eplc[(size_t)pm].jolt > 0.0f)) {
        shake_tap(g_eplc[(size_t)pm].cam[0] * (float)(E.fw > E.fh ? E.fw : E.fh));
        cinematic_audio_note_as(CIN_PLACE, E.wx, E.wz, (float)(E.fw > E.fh ? E.fw : E.fh),
                                (g_eplc[(size_t)pm].flags & EPX_SPLASH) != 0);
    }
    printf("EPLC|start|key=%lld|type=%s|mode=replay|frame=%d|seconds=%.2f\n", key, o.type,
           g_engineFrame, g_eplc[(size_t)pm].rig[0].length);
    return true;
}

/* THE RUMBLE, added to the camera's jolt (shake_offset): as the main structure of one of
   the player's own buildings rises through the slab it swells, strongest as the part
   arrives, and dies away over rumble_out seconds after; a hit cuts it at once. The
   strongest one of several wins, as the jolts do. Cells. */
static void eplc_rumble(float* sx, float* sz)
{
    *sx = *sz = 0.0f;
    if (g_eplcBld.empty() || !eplc_rule_now()) return;
    float best = 0.0f;
    for (std::map<long long, EplcBld>::const_iterator it = g_eplcBld.begin(); it != g_eplcBld.end(); ++it) {
        const EplcBld& E = it->second;
        if (E.mode != EPS_PLACE || !E.mine || E.cut) continue;
        if (E.model < 0 || E.model >= (int)g_eplc.size()) continue;
        const EplcModel& P = g_eplc[(size_t)E.model];
        const EplcRig& R = P.rig[0];
        if (R.r0 < 0.0f || R.r1 <= R.r0) continue;
        const float t = (engine_time() - (float)E.start) / 15.0f;
        const float out = P.cam[2] > 0.0f ? P.cam[2] : 0.35f;
        float env = 0.0f;
        if (t < R.r1) {
            float k = (t - R.r0) / (R.r1 - R.r0);
            k = k < 0.0f ? 0.0f : k;
            env = k * k * (3.0f - 2.0f * k);
        } else {
            env = fmaxf(0.0f, 1.0f - (t - R.r1) / out);
        }
        const float r = P.cam[1] * (float)(E.fw > E.fh ? E.fw : E.fh) * env;
        if (r <= best) continue;
        best = r;
        *sx = r * sinf(2.0f * EBLD_PI * P.cam[3] * t);
        *sz = r * sinf(2.0f * EBLD_PI * P.cam[4] * t + 1.1f);
    }
}

/* Mission boundary and shutdown. */
static void eplc_reset(void)
{
    g_eplcBld.clear();
    g_eplcWallAcc.clear();
    g_eplcWallNext.clear();
    g_eplcLastTick = -1;
}

/* eplcdump: the files, their attachment, every building in its placement, and the pose
   of each: how many of its actors are still moving, the slab's height and the main
   part's, its turning parts' clock. */
static void eplc_dump(void)
{
    int att = 0;
    for (size_t i = 0; i < g_eplc.size(); i++) if (g_eplc[i].attached) att++;
    printf("EPLC|loaded=%d|attached=%d|on=%d|enhanced=%d\n", (int)g_eplc.size(), att,
           eplc_rule_now() ? 1 : 0, g_fx.enabled ? 1 : 0);
    /* (the draws since the last dump, each way: eplc_draw_run) */
    printf("EPLCRUNS|card=%ld|cpu=%ld\n", g_eplcCardRuns, g_eplcCpuRuns);
    g_eplcCardRuns = g_eplcCpuRuns = 0;
    for (size_t i = 0; i < g_eplc.size(); i++) {
        const EplcModel& P = g_eplc[i];
        int na = 0, nr = 0, ng = 0;
        for (size_t r = 0; r < P.rig.size(); r++) na += (int)P.rig[r].actor.size();
        for (size_t r = 0; r < P.run.size(); r++) nr += (int)P.run[r].size();
        for (size_t r = 0; r < P.grow.size(); r++) ng += (int)P.grow[r].size();
        float len = 0.0f;
        for (size_t r = 0; r < P.rig.size(); r++) len = fmaxf(len, P.rig[r].length);
        printf("EPLCMODEL|%s|attached=%d|rigs=%d|actors=%d|matched=%d|unmatched=%d|runs=%d|"
               "grow=%d|companion=%d|seconds=%.3f|drawn=%ld|normal_runs=%ld|companion_normal_runs=%ld\n", P.type.c_str(),
               P.attached ? 1 : 0, (int)P.rig.size(), na, P.matched, P.unmatched, nr, ng,
               (int)P.compRun.size(), len, P.drawn, P.normalDrawn, P.companionNormalDrawn);
        g_eplc[i].drawn = g_eplc[i].normalDrawn = g_eplc[i].companionNormalDrawn = 0;
        /* (version 2: a bent part's vertices found in its morph) */
        if (P.morphed || P.unmorphed)
            printf("EPLCMORPH|%s|found=%d|missing=%d\n", P.type.c_str(), P.morphed, P.unmorphed);
    }
    for (std::map<long long, EplcBld>::const_iterator it = g_eplcBld.begin(); it != g_eplcBld.end(); ++it) {
        const EplcBld& E = it->second;
        if (E.model < 0 || E.model >= (int)g_eplc.size()) continue;
        const EplcModel& P = g_eplc[(size_t)E.model];
        if (!P.attached) continue;
        const EplcRig& R = P.rig[0];
        /* the pose of the frame drawn: the object, if it is still in the heap */
        const SimObject* o = NULL;
        for (size_t i = 0; i < g_objects.size(); i++)
            if (g_objects[i].kind == K_BUILDING && (long long)smooth_key(g_objects[i]) == it->first) { o = &g_objects[i]; break; }
        float t = -1.0f;
        bool sell = false;
        if (o) t = eplc_time_of(P, *o, &E, &sell);
        int moving = 0;
        float slabY = 0.0f, mainY = 0.0f, spinDeg = -999.0f;
        int spinPiece = -1;
        if (t >= 0.0f) {
            EplcPose Q;
            eplc_pose(P, 0, o, t, (float)E.start + R.length * 15.0f, Q);
            for (size_t a = 0; a < Q.W.size(); a++) {
                const EbXf& X = Q.W[a];
                bool id = true;
                for (int q = 0; q < 9; q++) if (fabsf(X.R[q] - ((q % 4 == 0) ? 1.0f : 0.0f)) > 1e-5f) id = false;
                for (int q = 0; q < 3; q++) if (fabsf(X.t[q]) > 0.01f) id = false;
                if (!id) moving++;
            }
            slabY = Q.W[(size_t)R.slab].t[1] * MODEL_SCALE;
            int main = -1;
            for (size_t a = 0; a < R.actor.size(); a++)
                if ((R.actor[a].flags & EPA_EMERGE) && (main < 0 || R.actor[a].emerge.span > R.actor[(size_t)main].emerge.span))
                    main = (int)a;
            if (main >= 0) mainY = Q.W[(size_t)main].t[1] * MODEL_SCALE;
            /* the first turning part's angle as this frame draws it (the hand-over check:
               it meets the idle turn the remastered building draws after) */
            const EbldModel& M = g_ebld[(size_t)P.ebld];
            for (size_t v = 0; v < M.move.size(); v++)
                if (M.move[v].kind == EBV_SPIN) {
                    const float clk = eplc_spin_clock(R, P.moveRoot[0][v], t,
                                                      (float)E.start + (sell ? 0.0f : R.length * 15.0f));
                    spinDeg = ebld_move_angle(M.move[v], o->face, clk) * (180.0f / EBLD_PI);
                    spinPiece = M.move[v].piece;
                    break;
                }
        }
        /* each part rising through the slab: how much it is moving there now (0 to 1, the
           share of its dust), and for a bent part how far its morph has come */
        if (t >= 0.0f && E.mode == EPS_PLACE && t < R.length) {
            EplcPose Q, Q0;
            eplc_pose(P, 0, o, t, 0.0f, Q);
            eplc_pose(P, 0, o, t - 1.0f / 15.0f, 0.0f, Q0);
            for (size_t a = 0; a < R.actor.size(); a++) {
                const EplcActor& A = R.actor[a];
                if (A.flags & EPA_EMERGE)
                    printf("EPLCEMERGE|%s|key=%lld|actor=%s|act=%.3f|through=%d\n", E.type,
                           it->first, A.name, eplc_emerging(P, 0, Q, Q0, (int)a),
                           (A.flags & EPA_THROUGH) ? 1 : 0);
                if (A.morph >= 0) {
                    float k, b;
                    eplc_progress(A.mo[(size_t)A.morph], t, &k, &b);
                    printf("EPLCBEND|%s|key=%lld|actor=%s|k=%.3f\n", E.type, it->first, A.name, k);
                }
            }
        }
        float rx, rz;
        eplc_rumble(&rx, &rz);
        printf("EPLCOBJ|%s|key=%lld|id=%d|mode=%s|start=%d|t=%.3f|len=%.3f|moving=%d|"
               "slab_y=%.4f|main_y=%.4f|cut=%d|mine=%d|str=%d|str0=%d|bstate=%d|spin_deg=%.2f|spin_piece=%d\n", E.type,
               it->first, E.id,
               E.mode == EPS_SELL ? "SELL" : (t >= R.length || t < 0.0f ? "REST" : "PLACE"),
               E.start, t, R.length, moving, slabY, mainY, E.cut ? 1 : 0, E.mine ? 1 : 0,
               o ? o->str : -1, E.str0, o ? o->doing : -1, spinDeg, spinPiece);
    }
    float rx, rz;
    eplc_rumble(&rx, &rz);
    printf("EPLCSHAKE|rumble_x=%.5f|rumble_z=%.5f\n", rx, rz);
}

#endif /* EBLD_PLACE_H */
