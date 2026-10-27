/* ================================================================================== *
 *  model3d_mod.h -- authored vehicle models, lit by a real light, from .c3m files.
 *
 *  WHAT THIS IS
 *
 *  A SECOND ART SET for vehicles, drawn instead of the cartridge's model when one is
 *  installed and Enhanced Visuals is on. The console's own model stays in the mission
 *  pack, untouched, and is what Classic draws and what Win98 draws. Nothing here
 *  replaces a decode: every triangle and every texel is authored, so this module can
 *  be wrong about a tank but it cannot be unfaithful about the cartridge.
 *
 *  WHY IT IS NOT IN THE PACK. A pack vehicle is 38 to 139 triangles with 16x16
 *  textures and the console's lighting baked into its vertex colours, because the
 *  mesh path has no lights: it multiplies texture by vertex colour and that is the
 *  whole shading model. An authored vehicle is the other shape -- thousands of
 *  triangles, one atlas, and a base colour that means nothing until a light has been
 *  applied to it. Putting the second inside the first would throw away the part worth
 *  having. It gets a file and a draw of its own, the way the solid tiberium crystals
 *  already do.
 *
 *  THE LIGHT, AND WHAT IS OURS ABOUT IT
 *
 *  ALL OF IT. The cartridge has no lights, so a physically based evaluation here is
 *  an addition and is named as one. It is a single directional sun plus a two-colour
 *  hemisphere ambient:
 *
 *    - the SUN is the one the rest of the Enhanced chain already uses, the azimuth
 *      and elevation on the Visuals page. It is read from the same g_fxSunDir the
 *      ground's own term and the shadow matrix read, so turning the dial turns the
 *      light on the tank and the shadow it casts together. Using a second light here
 *      would have given a hull lit from one bearing and a shadow lying on another.
 *    - the AMBIENT is a sky colour from above and a bounce colour from below, and
 *      exists because a single sun leaves everything facing away from it pure black,
 *      which no photograph of a tank has ever looked like.
 *    - the SPECULAR is Cook-Torrance with GGX, Smith height-correlated visibility and
 *      a Schlick Fresnel: the standard metal/rough parameterisation, so the maps the
 *      artist authored against a standard viewer mean here what they meant there.
 *    - the AMBIENT SPECULAR is the usual split-sum stand-in against a flat
 *      environment. Without it every metal part reads as black, because metal has no
 *      diffuse term at all and a lone sun only ever lights the sliver of it that
 *      happens to be at the mirror angle.
 *
 *  The post chain does NOT light this a second time. Its sun term is applied to
 *  ground pixels only (the light pass tests the ground class in the normal buffer),
 *  so shadows, occlusion, bloom and the grade still reach the model while the
 *  directional light reaches it exactly once.
 *
 *  THE PLAYER COLOUR is a mask in the base map's alpha, not a second texture set. The
 *  cartridge recolours by swapping a palette, which an RGBA image cannot carry; the
 *  mask says which texels are paint and the house tint multiplies them, so a model
 *  ships once and wears every one of the eight player colours.
 *
 *  TIER 1 (Win98, Voodoo 2): ABSENT BY DESIGN, and that is a complete answer rather
 *  than a gap. Glide is fixed function with no vertex or fragment stage at all, so
 *  none of the above can be drawn there under any budget. The Win98 build keeps the
 *  cartridge's model, which is the picture it has always had and the one the charter
 *  promises. See the row this feature owns in docs/tier1-gap.md.
 *
 *  DETERMINISM. Nothing here reads a clock. The only value that advances is the belt
 *  scroll, and it is handed in from the per-tick object parse (the same g_treadNow the
 *  pack path uses) rather than sampled in the draw, so two screenshot runs of one
 *  script stay byte identical.
 * ================================================================================== */
#ifndef OPENCNC3D_MODEL3D_MOD_H
#define OPENCNC3D_MODEL3D_MOD_H
#include "model_cloth_reference.h"

/* ---- the file ---------------------------------------------------------------------
   Written by tools/art/blender_c3m.py, which carries the format's other half. Little
   endian throughout, and every count is up front so the whole file is one pass.

       char     magic[8]     "C3DMODL1"
       u32      version, nvert, nidx, npart, nsub, nmat, ntex, reserved
       char     type[8]      the engine's type name, e.g. HTNK
       f32      bbox[6]      min xyz then max xyz, model units
       vert[]   8 floats     position, normal, uv
       idx[]    u32
       part[]   u32 role, 3f pivot, char name[16]
       sub[]    u32 part, mat, idx0, nidx
       mat[]    i32 base, norm, orm, u32 flags
       tex[]    u32 w, h, nbytes, then nbytes of RGBA8

   VERSION 2 appends a tail, and puts the bone count in the header's eighth word (which
   version 1 wrote as zero):

       u32      nclip, reserved[3]
       -- present when nbone > 0 --
       skin[]   per vertex: u8 bone[4], u8 weight[4], the weights summing to 255
       bone[]   char name[32], f32 inverse bind[12] (row-major 3x4: model -> bone space)
       clip[]   char name[16], u32 nframe, f32 fps, f32 stride, u32 flags,
                then nframe * nbone * 8 floats: the bone's MODEL-space pose as
                quaternion xyzw, translation xyz and a uniform scale

   tools/art/fbx_c3m.py writes it from an FBX. A skinned model's vertices are its BIND
   pose, which is what every pass draws until something poses it (m3_pose).

   A SUBMESH is one part's triangles in one material. Splitting at bake time is what
   lets the draw be a bind and a glDrawElements per material rather than a test per
   triangle, which is the shape the pack path has and the reason it is slow. */

#define M3_MAGIC   "C3DMODL1"
#define M3_VERSION 3u          /* the newest this build reads; 1 and 2 are read too  */
#define M3_MAX_BONES 64        /* the pose palette, and the converter's own limit   */

enum { M3_ROLE_STATIC = 0, M3_ROLE_TURRET = 1 };
enum { M3_MAT_TREAD = 1 << 0, M3_MAT_NO_PLAYER = 1 << 1, M3_MAT_CLOTH = 1 << 2,
       M3_MAT_EMBLEM = 1 << 3 };

/* THE SHOULDER EMBLEM'S PAINT. A material flagged M3_MAT_EMBLEM carries two stencils, the
   GDI eagle in its surface map's alpha and the Nod emblem in its normal map's alpha, over
   a colour map with the eagle painted out; the soldier's side picks one and it is painted
   in this colour. Measured: the mean of the eagle's own texels on the Tempest Rising
   rifleman's symbol sheet (T_GDI_Symbol_D), linear, so the GDI soldier's shoulder looks as
   it was painted and the Nod one's emblem is the same paint. */
#define M3_LOGO_R 0.4869f
#define M3_LOGO_G 0.4822f
#define M3_LOGO_B 0.4760f

/* THE CLOTH COLOUR'S REFERENCE BRIGHTNESS. A side's cloth colour replaces the fabric's
   paint with that colour scaled by the paint's own brightness over this number, so the
   camouflage pattern, the seams and the shading all survive as light and dark of the new
   colour, and fabric of average brightness comes out as exactly the colour chosen.
   Measured: the mean linear luminance of the Tempest Rising rifleman's colour map under
   his cloth mask (T_GDI_Rifleman_D under T_GDI_Rifleman_Cloth), which is the only sheet
   that carries one. A second soldier with fabric of a different brightness needs its own
   number, carried per material, before this constant is shared with it. */
#define M3_CLOTH_REF 0.0820f

struct M3Part { int role; float px, py, pz; char name[17]; };
struct M3Sub  { int part, mat; unsigned idx0, nidx; };
struct M3Mat  { int base, norm, orm; unsigned flags; float clothRef=0.0f; };
struct M3Tex  { int w, h; std::vector<unsigned char> px; GLuint gl; };

enum { M3_CLIP_LOOP = 1 << 0, M3_CLIP_LOCOMOTION = 1 << 1 };
struct M3Bone { char name[33]; float ib[12]; };
struct M3Clip {
    std::string name;
    int nframe;
    float fps;
    float stride;                   /* model units of ground per loop; 0 if not a gait */
    unsigned flags;
    std::vector<float> pose;        /* nframe * nbone * 8 */
};

/* VERSION 3: A COARSER LEVEL OF DETAIL. Its own index list and submeshes over the SAME
   vertex array, using only the first nvert vertices (the exporter puts a coarser level's
   vertices first), so drawing it skins only that prefix. screen is the switch in Unreal's
   own ScreenSize measure -- the bind pose's bounding sphere, its projected diameter over
   the screen's height -- below which the level is drawn. */
struct M3Lod {
    float screen;
    unsigned nvert;
    std::vector<unsigned int> idx;
    std::vector<M3Sub> sub;
};

/* A bind/weight edit must get a fresh revision, even when it fits the existing
   vector capacity. Pointer/size stamps below also catch appended corpse caps. */
static unsigned long long g_m3PoseRevision = 0;
struct M3Model {
    std::string type;
    std::vector<float>        vert;   /* 8 floats each: x y z nx ny nz u v */
    std::vector<unsigned int> idx;
    std::vector<M3Part>       part;
    std::vector<M3Sub>        sub;
    std::vector<M3Mat>        mat;
    std::vector<M3Tex>        tex;
    /* version 2, a skinned model; all three empty for a rigid one */
    std::vector<unsigned char> skin;  /* 8 bytes a vertex: bone[4], weight[4] */
    std::vector<M3Bone>       bone;
    std::vector<M3Clip>       clip;
    /* THE POSED COPY OF vert, for a pose the frame's pose cache had no room for (m3_skin
       writes a pose into the cache when it can, and draws point at it through drawPosed).
       One buffer per model rather than per object, because a pose written here is drawn
       straight after and nothing reads it between two objects. */
    std::vector<float>        posed;
    /* THE BONE POSES the last m3_skin used (8 floats a bone, model space), kept so a
       point held by a bone -- the muzzle of the rifle in his hands -- can be placed on
       the pose that was just drawn. Like posed, one per model and rewritten per object. */
    std::vector<float>        pose;
    /* version 3, finest first; empty for every model without levels */
    std::vector<M3Lod>        lod;
    /* Bind/skin content identity for exact pose-cache lookups. */
    unsigned long long poseRevision;
    /* WHICH LEVEL THE NEXT POSE AND DRAW USE: -1 the full mesh, else an index into lod.
       Set by the caller for one object and handed back to -1 after it. */
    int   drawLod;
    float bb[6];
    bool  uploaded;
    /* A REMASTERED SOLDIER (einf/<TYPE>.c3m): Enhanced's own infantry, drawn with the
       experimental set OFF. The experimental set (models3d/) is a different look and
       keeps its own models; the two never stand in for each other (m3inf_set_now). */
    bool  enh;
    /* A RIGID MODEL IN GPU MEMORY: vert in one vertex buffer, and idx followed by each
       level's list in one index buffer, level l starting at lodBase[l]. Both 0 for a
       skinned model, whose vertices change with every pose, and wherever the driver has
       no buffer objects or would not make the store; the draw then reads vert and idx
       from system memory, as it always did, so both stay loaded. */
    GLuint vbo, ibo;
    std::vector<unsigned> lodBase;
    /* WHERE THE NEXT DRAW OF A SKINNED MODEL READS ITS POSE: this object's entry in the
       frame's pose cache (m3_pose_find), position and normal at 6 floats a vertex. Set by
       m3_skin and taken back to 0 by the draw that reads it; 0 draws posed. */
    const float* drawPosed;
    /* ...and which entry that is, so the draw can find the entry's GPU copy
       (m3_pose_gpu). Read only while drawPosed is set, and checked against it. */
    int drawPosedEnt;
    /* A SKINNED MODEL'S FIXED HALF IN GPU MEMORY (m3_skin_buffers): the texture
       coordinates, which no pose changes, in one buffer, and idx followed by each level's
       list in another, level l starting at skinLodBase[l]. Built from vert and idx as they
       stand, and built again if either has grown since: a torn body's ends and split seams
       are appended to both (infdeath_mod.h ir_build_caps). skinTried stops a driver that
       would not make the store from being asked again on every draw. */
    GLuint skinUv, skinIbo;
    size_t skinUvN, skinIdxN;
    bool   skinTried;
    std::vector<unsigned> skinLodBase;
    M3Model() : poseRevision(++g_m3PoseRevision), drawLod(-1), uploaded(false), enh(false), vbo(0), ibo(0), drawPosed(0),
                drawPosedEnt(-1), skinUv(0), skinIbo(0), skinUvN(0), skinIdxN(0),
                skinTried(false)
    { memset(bb, 0, sizeof bb); }
};

static void m3_pose_model_changed(M3Model& m)
{
    m.poseRevision = ++g_m3PoseRevision;
    m.drawPosed = 0;
    m.drawPosedEnt = -1;
}

static std::vector<M3Model> g_m3;
static bool g_m3Loaded = false;

/* IS THIS ART SWITCHED ON. Enhanced, the switch, a model installed, and a shader stage
   to draw it with. The last of those four is not paranoia: fx_gl.h resolves the GLSL
   entry points at run time and reports a driver that does not have them, and on such a
   machine the right answer is the cartridge's model rather than an untextured
   silhouette. --nomodel3d clears the switch, which is the A/B every judgement about
   this art has to be made against. */
static bool m3_available(void)
{
    /* UNDER THE EXPERIMENTAL SWITCH, with the generated terrain. The authored models and
       that art are one look rather than two features that happen to be on at once, and
       the A/B the look is judged by has to move both together: a high detail hull on the
       cartridge's own ground compares nothing to anything. m3_on stays its own row so a
       single piece can still be taken out while the rest of the experiment is up. */
    return g_fx.enabled && g_fx.experimental_textures && g_fx.m3_on && fx_gl_ready
           && !g_m3.empty();
}

/* THE 3D SOLDIERS' OWN GATE: the same experimental look, but their own row in place of
   m3_on. The vehicle row taking the soldiers out with it would be a switch with a
   side effect nobody can see from the panel. */
/* WHICH SET OF SOLDIERS THIS PICTURE USES, whether or not it is drawing them: the
   remastered soldiers (einf/) when their row is on and experimental art is off, else the
   experimental set's (models3d/). The soldier's state is stepped with the model of the
   set that would draw him, so its clips' lengths are the ones he is shown with. With
   the remastered row off the experimental set is still the one stepped, as it was
   before the remastered soldiers existed; it is drawn only under experimental art. */
static bool m3inf_set_now(void)
{
    return !g_fx.experimental_textures && g_fx.einf_on;
}

static bool m3inf_available(void)
{
    if (!g_fx.enabled || !fx_gl_ready || g_m3.empty())
        return false;
    const bool enh = m3inf_set_now();
    if (!enh && !(g_fx.experimental_textures && g_fx.m3inf_on))
        return false;
    for (size_t i = 0; i < g_m3.size(); i++)
        if (g_m3[i].enh == enh && !g_m3[i].bone.empty())
            return true;
    return false;
}

/* THE OBSERVER, and it counts SUBMITTED geometry rather than finished pixels.
   A gate that only photographs this feature cannot tell "Classic correctly drew the
   cartridge model" from "the authored model was built and then quietly discarded", and
   those are different bugs with the same picture. Zeroed at the top of every frame by
   the renderer, so a dump reads one frame and not a session. */
static int g_m3Drawn = 0;     /* objects answered by an authored model this frame */
static int g_m3Tris  = 0;     /* triangles submitted for them                     */
static int g_m3Turn  = 0;     /* of those objects, how many had a turned turret   */
static int g_m3InfDrawn = 0;  /* of those, infantry, posed from a clip            */
static int g_m3InfFall  = 0;  /* infantry with a model but no clip for what they
                                 are doing, handed back to their sprite           */

/* SET FOR THE SUN'S CASTER PASS ONLY. That pass re-draws the world into a depth-only
   target from the sun's box: it wants this model's SHAPE and has no use for its
   shading, and running a fragment shader that samples three atlases to write nothing
   but depth would be the most expensive way to produce the same texture. */
static int g_m3CasterDepth = 0;
/* Set while the 3D soldiers' own sun map is being drawn: the one caster pass in which a
   soldier draws and the shared map's pass leaves him out. */
static int g_m3InfOwnPass = 0;

/* SET FOR THE WATER'S REFLECTION PASS, which is the caster's other caller. That pass
   draws the world with the modelview MIRRORED about the sea, and a mirror reverses
   winding: culling back faces under it culls the front ones and turns a solid hull
   inside out. The cheap and correct answer for a pass that is already drawing the
   scene twice is to stop culling for it. */
static int g_m3Mirrored = 0;

static int m3_find(const char* type)
{
    if (!type || !m3_available())
        return -1;
    for (size_t i = 0; i < g_m3.size(); i++)
        if (g_m3[i].type == type && !g_m3[i].enh)
            return (int)i;
    return -1;
}

static int m3inf_find(const char* type)
{
    if (!type || !m3inf_available())
        return -1;
    const bool enh = m3inf_set_now();
    for (size_t i = 0; i < g_m3.size(); i++)
        if (g_m3[i].type == type && !g_m3[i].bone.empty() && g_m3[i].enh == enh)
            return (int)i;
    return -1;
}

/* ---- load -------------------------------------------------------------------------
   One directory, scanned by NAME rather than by listing it: this program has to
   compile for a freestanding mingw where dirent.h is not worth spending the effort
   on, which is the same reason the mission scan probes instead of reading a folder.
   The names come from the type table, so a model is installed by dropping
   models3d/<TYPE>.c3m beside the missions and nothing has to be registered. */
static bool m3_read_one(const char* path, M3Model& m)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return false;
    m3_pose_model_changed(m);
    char magic[8];
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, M3_MAGIC, 8) != 0) {
        fprintf(stderr, "model3d: %s is not a c3m model\n", path);
        fclose(f);
        return false;
    }
    unsigned h[8];
    char type[8];
    if (fread(h, 4, 8, f) != 8 || fread(type, 1, 8, f) != 8) { fclose(f); return false; }
    if (h[0] < 1u || h[0] > M3_VERSION) {
        fprintf(stderr, "model3d: %s is format %u and this build reads 1 to %u -- re-run "
                        "the exporter that made it\n", path, h[0], M3_VERSION);
        fclose(f);
        return false;
    }
    const unsigned version = h[0];
    const unsigned nbone = (version >= 2u) ? h[7] : 0u;
    if (nbone > M3_MAX_BONES) {
        fprintf(stderr, "model3d: %s has %u bones and the palette holds %d\n",
                path, nbone, M3_MAX_BONES);
        fclose(f);
        return false;
    }
    char tn[9];
    memcpy(tn, type, 8);
    tn[8] = 0;
    m.type = tn;
    if (fread(m.bb, 4, 6, f) != 6) { fclose(f); return false; }

    const unsigned nvert = h[1], nidx = h[2], npart = h[3];
    const unsigned nsub = h[4], nmat = h[5], ntex = h[6];
    if (!nvert || !nidx || !npart || !nsub || !nmat) { fclose(f); return false; }
    m.vert.resize((size_t)nvert * 8);
    if (fread(&m.vert[0], 4, m.vert.size(), f) != m.vert.size()) { fclose(f); return false; }
    m.idx.resize(nidx);
    if (fread(&m.idx[0], 4, nidx, f) != nidx) { fclose(f); return false; }
    for (unsigned i = 0; i < npart; i++) {
        unsigned role; float p[3]; char nm[16];
        if (fread(&role, 4, 1, f) != 1 || fread(p, 4, 3, f) != 3
            || fread(nm, 1, 16, f) != 16) { fclose(f); return false; }
        M3Part pt;
        pt.role = (int)role; pt.px = p[0]; pt.py = p[1]; pt.pz = p[2];
        memcpy(pt.name, nm, 16); pt.name[16] = 0;
        m.part.push_back(pt);
    }
    for (unsigned i = 0; i < nsub; i++) {
        unsigned s[4];
        if (fread(s, 4, 4, f) != 4) { fclose(f); return false; }
        M3Sub sb; sb.part = (int)s[0]; sb.mat = (int)s[1]; sb.idx0 = s[2]; sb.nidx = s[3];
        m.sub.push_back(sb);
    }
    for (unsigned i = 0; i < nmat; i++) {
        int t[3]; unsigned fl;
        if (fread(t, 4, 3, f) != 3 || fread(&fl, 4, 1, f) != 1) { fclose(f); return false; }
        M3Mat mt; mt.base = t[0]; mt.norm = t[1]; mt.orm = t[2]; mt.flags = fl;
        m.mat.push_back(mt);
    }
    for (unsigned i = 0; i < ntex; i++) {
        unsigned t[3];
        if (fread(t, 4, 3, f) != 3) { fclose(f); return false; }
        M3Tex tx; tx.w = (int)t[0]; tx.h = (int)t[1]; tx.gl = 0;
        tx.px.resize(t[2]);
        if (t[2] && fread(&tx.px[0], 1, t[2], f) != t[2]) { fclose(f); return false; }
        m.tex.push_back(tx);
    }
    /* Cache each material's explicit masked fabric luminance once, while CPU pixels
       are present. Only the guarded RA Allied draw opts into this reference. */
    for (size_t i=0;i<m.mat.size();i++) {
        M3Mat& mt=m.mat[i];
        if (!(mt.flags&M3_MAT_CLOTH) || mt.base<0 || mt.orm<0 ||
            mt.base>=(int)m.tex.size() || mt.orm>=(int)m.tex.size()) continue;
        const M3Tex& base=m.tex[mt.base]; const M3Tex& orm=m.tex[mt.orm];
        if (base.w<=0 || base.h<=0 || base.w!=orm.w || base.h!=orm.h) continue;
        const size_t pixels=(size_t)base.w*(size_t)base.h;
        if (base.px.size()/4!=pixels || orm.px.size()/4!=pixels ||
            base.px.size()%4 || orm.px.size()%4) continue;
        mt.clothRef=m3_cloth_reference_rgba(base.px.data(),orm.px.data(),pixels);
    }
    if (version >= 2u) {
        unsigned tail[4];
        if (fread(tail, 4, 4, f) != 4) { fclose(f); return false; }
        const unsigned nclip = tail[0];
        if (nbone) {
            m.skin.resize((size_t)nvert * 8);
            if (fread(&m.skin[0], 1, m.skin.size(), f) != m.skin.size()) { fclose(f); return false; }
            for (unsigned b = 0; b < nbone; b++) {
                M3Bone bn;
                char nm[32];
                if (fread(nm, 1, 32, f) != 32 || fread(bn.ib, 4, 12, f) != 12) {
                    fclose(f); return false;
                }
                memcpy(bn.name, nm, 32); bn.name[32] = 0;
                m.bone.push_back(bn);
            }
            for (unsigned c = 0; c < nclip; c++) {
                char nm[17];
                unsigned nf, fl;
                float hd[2];
                if (fread(nm, 1, 16, f) != 16 || fread(&nf, 4, 1, f) != 1
                    || fread(hd, 4, 2, f) != 2 || fread(&fl, 4, 1, f) != 1) {
                    fclose(f); return false;
                }
                nm[16] = 0;
                if (nf == 0 || nf > 100000u) { fclose(f); return false; }
                M3Clip cl;
                cl.name = nm; cl.nframe = (int)nf; cl.fps = hd[0]; cl.stride = hd[1];
                cl.flags = fl;
                cl.pose.resize((size_t)nf * nbone * 8);
                if (fread(&cl.pose[0], 4, cl.pose.size(), f) != cl.pose.size()) {
                    fclose(f); return false;
                }
                if (!(cl.fps > 0.0f)) cl.fps = 30.0f;
                m.clip.push_back(cl);
            }
        }
    }
    if (version >= 3u) {
        unsigned nlod = 0;
        if (fread(&nlod, 4, 1, f) != 1 || nlod > 8u) { fclose(f); return false; }
        for (unsigned l = 0; l < nlod; l++) {
            float hf; unsigned hd[3];
            if (fread(&hf, 4, 1, f) != 1 || fread(hd, 4, 3, f) != 3) { fclose(f); return false; }
            M3Lod lv;
            lv.screen = hf; lv.nvert = hd[0];
            if (!hd[1] || !hd[2] || hd[0] > nvert) { fclose(f); return false; }
            for (unsigned i = 0; i < hd[2]; i++) {
                unsigned sb4[4];
                if (fread(sb4, 4, 4, f) != 4) { fclose(f); return false; }
                M3Sub sb; sb.part = (int)sb4[0]; sb.mat = (int)sb4[1];
                sb.idx0 = sb4[2]; sb.nidx = sb4[3];
                lv.sub.push_back(sb);
            }
            lv.idx.resize(hd[1]);
            if (fread(&lv.idx[0], 4, hd[1], f) != hd[1]) { fclose(f); return false; }
            m.lod.push_back(lv);
        }
    }
    fclose(f);
    /* A LEVEL IS CHECKED LIKE THE MESH: every index inside its own vertex prefix (which is
       what lets the pose stop there), every submesh inside its index list. */
    for (size_t l = 0; l < m.lod.size(); l++) {
        const M3Lod& lv = m.lod[l];
        for (size_t i = 0; i < lv.idx.size(); i++)
            if (lv.idx[i] >= lv.nvert) {
                fprintf(stderr, "model3d: %s level %d index %u is past its %u vertices\n",
                        path, (int)l + 1, lv.idx[i], lv.nvert);
                return false;
            }
        for (size_t i = 0; i < lv.sub.size(); i++) {
            const M3Sub& sb = lv.sub[i];
            if (sb.part < 0 || sb.part >= (int)m.part.size()
                || sb.mat < 0 || sb.mat >= (int)m.mat.size()
                || (size_t)sb.idx0 + sb.nidx > lv.idx.size()) {
                fprintf(stderr, "model3d: %s level %d submesh %d is out of range\n",
                        path, (int)l + 1, (int)i);
                return false;
            }
        }
    }
    /* EVERY INDEX IS CHECKED ONCE, HERE. A bad one is a driver crash rather than a
       wrong picture, and this file is authored art that arrives from outside the
       build, so it is exactly the input that should not be trusted by shape alone. */
    for (size_t i = 0; i < m.idx.size(); i++)
        if (m.idx[i] >= nvert) {
            fprintf(stderr, "model3d: %s index %u is past %u vertices\n",
                    path, m.idx[i], nvert);
            return false;
        }
    for (size_t i = 0; i < m.skin.size(); i += 8) {
        int wsum = 0;
        for (int k = 0; k < 4; k++) {
            if (m.skin[i + 4 + k] && m.skin[i + k] >= m.bone.size()) {
                fprintf(stderr, "model3d: %s vertex %u names bone %u of %u\n", path,
                        (unsigned)(i / 8), (unsigned)m.skin[i + k], (unsigned)m.bone.size());
                return false;
            }
            wsum += m.skin[i + 4 + k];
        }
        if (wsum == 0) {
            fprintf(stderr, "model3d: %s vertex %u is bound to no bone\n", path,
                    (unsigned)(i / 8));
            return false;
        }
    }
    for (size_t i = 0; i < m.sub.size(); i++) {
        const M3Sub& s = m.sub[i];
        if (s.part < 0 || s.part >= (int)m.part.size()
            || s.mat < 0 || s.mat >= (int)m.mat.size()
            || (size_t)s.idx0 + s.nidx > m.idx.size()) {
            fprintf(stderr, "model3d: %s submesh %d is out of range\n", path, (int)i);
            return false;
        }
    }
    return true;
}

static void m3_upload(M3Model& m);   /* with the rest of the GL, further down */

static void m3_load(const char* dir)
{
    if (g_m3Loaded)
        return;
    g_m3Loaded = true;
    /* FACT is a completed-building preview. The object-level selector keeps its
       cartridge construction, selling and damaged states until those are authored. */
    static const char* const TYPES[] = {
        "HTNK", "MTNK", "LTNK", "STNK", "FTNK", "APC", "ARTY", "BGGY", "BIKE",
        "JEEP", "HARV", "MCV", "MLRS", "MSAM", "TRAN", "HELI", "ORCA", "BOAT", "FACT",
        /* INFANTRY, which only a SKINNED model can answer: a soldier is his animation,
           and a rigid one would glide about in a single pose. The draw that takes these
           is m3inf_draw in the renderer, not the vehicle path. */
        "E1", "E2", "E3", "E4", "E5", "E6", "RMBO",
        /* E1TR, the brain's test infantry: the minigunner in every rule and every piece
           of art EXCEPT this model, so it can stand beside the minigunner and be told
           apart. The renderer reads it as E1 everywhere else (type_art_alias). */
        "E1TR"
    };
    int found = 0;
    for (size_t i = 0; i < sizeof TYPES / sizeof TYPES[0]; i++) {
        char path[512];
        snprintf(path, sizeof path, "%smodels3d/%s.c3m",
                 dir && *dir ? dir : "", TYPES[i]);
        M3Model m;
        if (!m3_read_one(path, m))
            continue;
        if (m.type != TYPES[i]) {
            fprintf(stderr, "model3d: %s says it is %s -- skipped\n",
                    path, m.type.c_str());
            continue;
        }
        size_t bytes = 0;
        for (size_t t = 0; t < m.tex.size(); t++) bytes += m.tex[t].px.size();
        printf("M3|%s|tris %d|verts %d|parts %d|subs %d|tex %d|%.1f MB|bones %d|clips %d\n",
               m.type.c_str(), (int)(m.idx.size() / 3), (int)(m.vert.size() / 8),
               (int)m.part.size(), (int)m.sub.size(), (int)m.tex.size(),
               (double)bytes / (1024.0 * 1024.0), (int)m.bone.size(), (int)m.clip.size());
        for (size_t c = 0; c < m.clip.size(); c++)
            printf("M3|%s|clip %s|%d frames|%.0f fps|stride %.0f|%s\n", m.type.c_str(),
                   m.clip[c].name.c_str(), m.clip[c].nframe, (double)m.clip[c].fps,
                   (double)m.clip[c].stride,
                   (m.clip[c].flags & M3_CLIP_LOOP) ? "loop" : "once");
        for (size_t l = 0; l < m.lod.size(); l++)
            printf("M3|%s|lod %d|tris %d|verts %u|below screen size %.3f\n",
                   m.type.c_str(), (int)l + 1, (int)(m.lod[l].idx.size() / 3),
                   m.lod[l].nvert, (double)m.lod[l].screen);
        g_m3.push_back(m);
        found++;
    }
    /* THE REMASTERED SOLDIERS, einf/<TYPE>.c3m (tools/art/remaster/infantry): Enhanced's
       own infantry. Reported as M3E| so a gate reading the experimental set's M3| lines
       reads that set alone. The soldiers, then the civilians and the three named
       characters; a type whose file is absent is simply left to its sprite.
       RED ALERT'S SET (g_raModule): its own types. Eighteen of them draw a Tiberian Dawn
       sprite frame for frame (E1 to E4, E6, the ten civilians, Delphi, Chan, and Einstein,
       who is Moebius's sprite) and wear that model; the Medic, Spy, Thief, Tanya, General
       Stavros and the Attack Dog have models of their own. A type is matched by the
       engine's own name, so Einstein reads Moebius's file under his. */
    static const char* const EINF[][2] = {
        { "E1", "E1" }, { "E2", "E2" }, { "E3", "E3" }, { "E4", "E4" }, { "E5", "E5" }, { "E6", "E6" },
        { "RMBO", "RMBO" },
        { "C1", "C1" }, { "C2", "C2" }, { "C3", "C3" }, { "C4", "C4" }, { "C5", "C5" }, { "C6", "C6" },
        { "C7", "C7" }, { "C8", "C8" }, { "C9", "C9" }, { "C10", "C10" },
        { "MOEBIUS", "MOEBIUS" }, { "CHAN", "CHAN" }, { "DELPHI", "DELPHI" } };
    static const char* const EINF_RA[][2] = {
        { "E1", "E1" }, { "E2", "E2" }, { "E3", "E3" }, { "E4", "E4" }, { "E6", "E6" },
        { "E7", "E7" }, { "SPY", "SPY" }, { "THF", "THF" }, { "MEDI", "MEDI" }, { "GNRL", "GNRL" },
        { "DOG", "DOG" },
        { "C1", "C1" }, { "C2", "C2" }, { "C3", "C3" }, { "C4", "C4" }, { "C5", "C5" }, { "C6", "C6" },
        { "C7", "C7" }, { "C8", "C8" }, { "C9", "C9" }, { "C10", "C10" },
        { "EINSTEIN", "MOEBIUS" }, { "CHAN", "CHAN" }, { "DELPHI", "DELPHI" } };
    const char* const (*set)[2] = g_raModule ? EINF_RA : EINF;
    const size_t setN = g_raModule ? sizeof EINF_RA / sizeof EINF_RA[0] : sizeof EINF / sizeof EINF[0];
    int einf = 0;
    for (size_t i = 0; i < setN; i++) {
        char path[512];
        M3Model m;
        /* Red Alert's own models are staged in its module folder (ra3d/einf/, from
           game/einf_ra); the ones it shares are the C&C set beside the game */
        bool got = false;
        if (g_raModule) {
            snprintf(path, sizeof path, "%sra3d/einf/%s.c3m", dir && *dir ? dir : "", set[i][1]);
            got = m3_read_one(path, m);
        }
        if (!got) {
            snprintf(path, sizeof path, "%seinf/%s.c3m", dir && *dir ? dir : "", set[i][1]);
            got = m3_read_one(path, m);
        }
        if (!got)
            continue;
        if (m.type != set[i][1] || m.bone.empty()) {
            fprintf(stderr, "model3d: %s is not a skinned %s -- skipped\n", path, set[i][1]);
            continue;
        }
        m.type = set[i][0];                     /* the engine's name for whoever wears it */
        m.enh = true;
        printf("M3E|%s|tris %d|verts %d|tex %d|bones %d|clips %d\n", m.type.c_str(),
               (int)(m.idx.size() / 3), (int)(m.vert.size() / 8), (int)m.tex.size(),
               (int)m.bone.size(), (int)m.clip.size());
        for (size_t c = 0; c < m.clip.size(); c++)
            printf("M3E|%s|clip %s|%d frames|%.0f fps|stride %.0f|%s\n", m.type.c_str(),
                   m.clip[c].name.c_str(), m.clip[c].nframe, (double)m.clip[c].fps,
                   (double)m.clip[c].stride, (m.clip[c].flags & M3_CLIP_LOOP) ? "loop" : "once");
        g_m3.push_back(m);
        einf++;
    }
    if (found || einf) {
        printf("M3|loaded|%d model%s|remastered soldiers %d\n", found, found == 1 ? "" : "s", einf);
        fflush(stdout);
    }
    /* UPLOADED HERE AND NOT ON FIRST DRAW, which is the reason this is called with a
       live GL context. Deferring it puts a 1.3 second stall on the frame a Mammoth
       first comes into view -- measured, and it is 12.8 MB of atlas plus its mipmap
       chain -- and the middle of a battle is a worse place for that than the second
       the loading screen is already taking. */
    for (size_t i = 0; i < g_m3.size(); i++)
        m3_upload(g_m3[i]);
}

/* ---- the shader ------------------------------------------------------------------ */

static const char* M3_VS =
    "#version 120\n"
    "uniform mat4  uModel;\n"      /* object -> world, the same transform on the stack */
    "uniform vec2  uUVOffset;\n"
    "varying vec3  vWorld;\n"
    "varying vec3  vNormal;\n"
    "varying vec2  vUV;\n"
    "void main() {\n"
    /* The world position and normal are computed from uModel rather than read back off
       the modelview, because the modelview also carries the camera and the light is in
       world space. Both describe the same placement; the host builds it once and sends
       it twice. Scale is uniform, so the normal needs no inverse transpose. */
    "    vec4 wp = uModel * gl_Vertex;\n"
    "    vWorld  = wp.xyz;\n"
    "    vNormal = mat3(uModel) * gl_Normal;\n"
    "    vUV     = gl_MultiTexCoord0.xy + uUVOffset;\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
    "}\n";

static const char* M3_FS =
    "#version 120\n"
    /* GLSL 1.20 HAS NO FRAGMENT-STAGE CUBE FETCH AT AN EXPLICIT LEVEL, which the
       roughness chain needs: the whole point is to choose the blur by roughness rather
       than by screen-space derivative. The extension that adds it is ancient and
       universal on anything that can run this chain at all, and the #else keeps a
       driver without it drawing rather than failing to compile -- the bias form lands
       on roughly the right level for a surface that is not edge-on, which is a
       degradation and not a break. */
    "#extension GL_ARB_shader_texture_lod : enable\n"
    "#ifdef GL_ARB_shader_texture_lod\n"
    "#define M3_ENVLOD(s, d, l) textureCubeLod(s, d, l)\n"
    "#else\n"
    "#define M3_ENVLOD(s, d, l) textureCube(s, d, l)\n"
    "#endif\n"
    "varying vec3 vWorld;\n"
    "varying vec3 vNormal;\n"
    "varying vec2 vUV;\n"
    "uniform sampler2D uBase;\n"
    "uniform sampler2D uNorm;\n"
    "uniform sampler2D uORM;\n"
    "uniform vec3  uSunDir;\n"      /* the direction light TRAVELS, world space */
    "uniform vec3  uSunCol;\n"
    "uniform vec3  uSkyCol;\n"
    "uniform vec3  uGndCol;\n"
    /* THE SKY, when one is installed. uEnvOn 0 keeps the two-colour hemisphere above,
       character for character, which is what a build with no sky pack draws and is why
       this change is additive rather than a replacement. */
    "uniform samplerCube uEnv;\n"
    "uniform int   uEnvOn;\n"
    "uniform float uEnvMaxLod;\n"
    "uniform float uEnvAmb;\n"
    /* vec4 AND NOT vec3, because fx_gl.h resolves glUniform4fv and not glUniform3fv,
       and a whole new entry point to save nine floats of uniform space would be a bad
       trade. The baker writes the padding. */
    "uniform vec4  uSH[9];\n"
    "uniform vec3  uCamPos;\n"
    "uniform vec3  uPlayer;\n"
    "uniform float uPlayerOn;\n"
    "uniform vec3  uCloth;\n"
    "uniform float uClothOn;\n"      /* the cloth amount, 0 on a material without the mask */
    "uniform float uClothRef;\n"
    "uniform float uEmblemOn, uSide;\n"
    "uniform vec3  uLogo;\n"
    "uniform float uPlayerGain;\n"  /* the team plates' brightness; 1 on every vehicle */
    "uniform float uShroud;\n"      /* 0 lit, 1 fully in the shroud */
    "uniform int uLightSplit;\n"
    "uniform float uFlash;\n"       /* the damage blush: 1 paints the whole hull white */
    "uniform float uNormalStr, uAoStr, uRoughMin, uSpecAmb, uGamma;\n"
    "uniform vec3  uTint;\n"        /* the shatter/wreck tint, 1,1,1 when unused */
    "uniform float uAlpha;\n"
    FX_OBJECT_GRADE_GLSL
    "\n"
    /* THE TANGENT FRAME IS DERIVED, NOT DELIVERED. Screen-space derivatives of the
       world position and of the UV give a basis that matches the triangle's own
       parameterisation exactly, including where an island is mirrored -- which the two
       sides of a tank are. Carrying tangents in the vertex instead would cost a third
       texture coordinate set and a bake-time decision about handedness that this gets
       right for free. */
    "mat3 m3_frame(vec3 N, vec3 p, vec2 uv) {\n"
    "    vec3 dp1 = dFdx(p),  dp2 = dFdy(p);\n"
    "    vec2 du1 = dFdx(uv), du2 = dFdy(uv);\n"
    "    vec3 dp2p = cross(dp2, N), dp1p = cross(N, dp1);\n"
    "    vec3 T = dp2p * du1.x + dp1p * du2.x;\n"
    "    vec3 B = dp2p * du1.y + dp1p * du2.y;\n"
    "    float inv = inversesqrt(max(dot(T, T), dot(B, B)) + 1e-12);\n"
    "    return mat3(T * inv, B * inv, N);\n"
    "}\n"
    "\n"
    "float m3_ggx(float NoH, float a) {\n"
    "    float a2 = a * a;\n"
    "    float d = NoH * NoH * (a2 - 1.0) + 1.0;\n"
    "    return a2 / max(3.14159265 * d * d, 1e-7);\n"
    "}\n"
    /* Smith, height correlated, in the form that cancels the 4*NoL*NoV of the
       microfacet denominator -- so the specular term below multiplies D, V and F and
       nothing else. */
    "float m3_vis(float NoV, float NoL, float a) {\n"
    "    float a2 = a * a;\n"
    "    float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);\n"
    "    float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);\n"
    "    return 0.5 / max(gv + gl, 1e-6);\n"
    "}\n"
    "vec3 m3_fresnel(vec3 f0, float u) {\n"
    "    float f = pow(1.0 - u, 5.0);\n"
    "    return f0 + (vec3(1.0) - f0) * f;\n"
    "}\n"
    /* THE IRRADIANCE, reconstructed from nine coefficients. The cosine convolution is
       already inside them (the baker folds it in), so this is the irradiance arriving
       at a surface facing n and nothing further has to be done to it. */
    "vec3 m3_sh(vec3 n) {\n"
    "    return uSH[0].rgb * 0.282095\n"
    "         + uSH[1].rgb * (0.488603 * n.y)\n"
    "         + uSH[2].rgb * (0.488603 * n.z)\n"
    "         + uSH[3].rgb * (0.488603 * n.x)\n"
    "         + uSH[4].rgb * (1.092548 * n.x * n.y)\n"
    "         + uSH[5].rgb * (1.092548 * n.y * n.z)\n"
    "         + uSH[6].rgb * (0.315392 * (3.0 * n.z * n.z - 1.0))\n"
    "         + uSH[7].rgb * (1.092548 * n.x * n.z)\n"
    "         + uSH[8].rgb * (0.546274 * (n.x * n.x - n.y * n.y));\n"
    "}\n"
    /* THE OTHER HALF OF THE SPLIT SUM: how much of the prefiltered environment a
       surface of this roughness actually reflects, at this angle. Normally a lookup
       table; this is Karis's analytic fit of the same two numbers, which is four
       instructions and saves shipping and binding a texture for a function that is
       smooth in both its arguments. */
    "vec2 m3_env_brdf(float rough, float NoV) {\n"
    "    vec4 c0 = vec4(-1.0, -0.0275, -0.572,  0.022);\n"
    "    vec4 c1 = vec4( 1.0,  0.0425,  1.040, -0.040);\n"
    "    vec4 r  = rough * c0 + c1;\n"
    "    float a = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;\n"
    "    return vec2(-1.04, 1.04) * a + r.zw;\n"
    "}\n"
    "vec3 m3_srgb_to_linear(vec3 c) {\n"
    "    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)),\n"
    "               step(vec3(0.04045), c));\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    vec4 base = texture2D(uBase, vUV);\n"
    "    vec3 alb  = m3_srgb_to_linear(base.rgb);\n"
    /* THE PLAYER COLOUR. A multiply and not a replace, so the paint keeps every scratch
       and every streak the artist put in it and only its hue moves. The mask is the
       base map's alpha; where it is zero -- dirt, rubber, bare metal, the rocket tips --
       nothing happens at all. */
    "    alb = mix(alb, alb * uPlayer * uPlayerGain, base.a * uPlayerOn);\n"
    "    vec4 ormA = texture2D(uORM, vUV);\n"
    /* THE CLOTH COLOUR, on the fabric the surface map's alpha marks. A replace, unlike
       the player colour, because the fabric is one faction's camouflage already and a
       multiply over tan cannot make black; the paint's brightness is kept, so the
       pattern stays (see M3_CLOTH_REF). The two masks do not overlap. */
    "    float clum = dot(alb, vec3(0.2126, 0.7152, 0.0722));\n"
    "    alb = mix(alb, uCloth * (clum / uClothRef), ormA.a * uClothOn);\n"
    "    float logo = mix(ormA.a, texture2D(uNorm, vUV).a, uSide) * uEmblemOn;\n"
    "    alb = mix(alb, uLogo, logo);\n"
    "    alb = mix(alb, vec3(1.0), uFlash);\n"
    "\n"
    "    vec3 orm = ormA.rgb;\n"
    "    float ao    = mix(1.0, orm.r, uAoStr);\n"
    "    float rough = clamp(orm.g, uRoughMin, 1.0);\n"
    "    float metal = orm.b;\n"
    "\n"
    "    vec3 N = normalize(vNormal);\n"
    "    if (!gl_FrontFacing) N = -N;\n"
    "    vec3 V = normalize(uCamPos - vWorld);\n"
    "    vec3 tn = texture2D(uNorm, vUV).rgb * 2.0 - 1.0;\n"
    "    tn.xy *= uNormalStr;\n"
    "    N = normalize(m3_frame(N, vWorld, vUV) * normalize(tn));\n"
    "\n"
    "    vec3 L = -uSunDir;\n"
    "    vec3 H = normalize(L + V);\n"
    "    float NoL = max(dot(N, L), 0.0);\n"
    "    float NoV = max(dot(N, V), 1e-4);\n"
    "    float NoH = max(dot(N, H), 0.0);\n"
    "    float VoH = max(dot(V, H), 0.0);\n"
    "\n"
    "    vec3 f0 = mix(vec3(0.04), alb, metal);\n"
    "    vec3 diff = alb * (1.0 - metal);\n"
    "    float a = rough * rough;\n"
    "    vec3 spec = m3_fresnel(f0, VoH) * (m3_ggx(NoH, a) * m3_vis(NoV, NoL, a));\n"
    /* EVERY LIGHT HERE IS AN IRRADIANCE and every diffuse response divides by pi.
       Worth being exact about, because getting it wrong is invisible on one surface
       and wrong on every other: drop the 1/pi from the fill but keep it on the sun
       and a hull reads correctly in the light and glows in the shade. */
    "    const float INV_PI = 0.31830989;\n"
    "    vec3 lit = (diff * INV_PI + spec) * uSunCol * NoL;\n"
    "    vec3 directLight = lit;\n"
    "\n"
    /* THE HEMISPHERE. Sky above, ground bounce below, the blend on the normal's own y.
       It is not an environment probe and does not pretend to be one; it exists because
       a lone sun leaves every surface turned away from it at zero, and the occlusion
       map is what stops it filling the creases it should not reach. */
    "    vec3 amb = mix(uGndCol, uSkyCol, N.y * 0.5 + 0.5);\n"
    "    if (uEnvOn == 1) amb = max(m3_sh(N) * uEnvAmb, vec3(0.0));\n"
    "    lit += diff * INV_PI * amb * ao;\n"
    /* AND THE AMBIENT'S SPECULAR, without which metal is black: metal has no diffuse
       term, so the only light it would otherwise carry is the sliver at the mirror
       angle. The split-sum stand-in against a flat environment, with the horizon
       occlusion the usual approximation carries. */
    "    if (uEnvOn == 1) {\n"
    /* THE REFLECTION. The prefiltered chain holds RADIANCE, not irradiance, so there is
       no 1/pi here and its absence is deliberate: putting one in would darken every
       metal surface by a factor of three and look like the sky had failed to load. */
    "        vec3 R = reflect(-V, N);\n"
    "        vec3 pre = m3_srgb_to_linear(M3_ENVLOD(uEnv, R, rough * uEnvMaxLod).rgb)\n"
    "                 * uEnvAmb;\n"
    "        vec2 ab = m3_env_brdf(rough, NoV);\n"
    "        lit += pre * (f0 * ab.x + ab.y) * uSpecAmb * ao * mix(ao, 1.0, metal);\n"
    "    } else {\n"
    "        vec3 fa = f0 + (max(vec3(1.0 - rough), f0) - f0) * pow(1.0 - NoV, 5.0);\n"
    "        lit += fa * amb * INV_PI * uSpecAmb * ao * mix(ao, 1.0, metal);\n"
    "    }\n"
    "\n"
    "    if (uLightSplit == 1) { gl_FragColor = vec4(clamp(directLight/max(lit,vec3(0.00001)),0.0,1.0),0.25); return; }\n"
    "    lit *= uTint;\n"
    /* THE SHROUD IS TAKEN AFTER THE ENCODE, NOT BEFORE IT. Everything else under the
       shroud darkens in display values: the blanket blends black over the frame, the
       ground and draw_mesh subtract the byte from a display shade, the sprite soldiers
       draw at glColor (1 - e). Multiplied in here, ahead of the gamma, the same byte
       came out as (1 - e)^(1/2.2): at the 0.784 ceiling a soldier stood at half his
       light on ground and blanket at a fifth of theirs, so soldiers at the reveal edge
       read as standing on top of the shroud. */
    "    vec3 outc = objectGrade(pow(max(lit, vec3(0.0)), vec3(1.0 / uGamma))) * (1.0 - uShroud);\n"
    "    gl_FragColor = vec4(outc, uAlpha);\n"
    "}\n";

/* ---- the normals pass -------------------------------------------------------------
   THE SAME SURFACE NORMAL THE LIT PASS USES, handed to the post chain so its occlusion
   and its wet-surface response see the hull the art actually describes instead of the
   flat plane the depth buffer reconstructs. Until now nothing but the terrain and the
   canopies wrote into that buffer, because nothing else in the scene HAD a normal: a
   cartridge model stores none at all.

   The alpha is the class, and it is 0.35 rather than the foliage 0.5 on purpose. A
   hull wants the normal used and then wants to be treated as rolled steel, which is
   neither what a field wants nor what a canopy does. */
static const char* M3_NRM_FS =
    "#version 120\n"
    "varying vec3 vWorld;\n"
    "varying vec3 vNormal;\n"
    "varying vec2 vUV;\n"
    "uniform sampler2D uNorm;\n"
    "uniform float uNormalStr, uClass;\n"
    "mat3 m3_frame(vec3 N, vec3 p, vec2 uv) {\n"
    "    vec3 dp1 = dFdx(p),  dp2 = dFdy(p);\n"
    "    vec2 du1 = dFdx(uv), du2 = dFdy(uv);\n"
    "    vec3 dp2p = cross(dp2, N), dp1p = cross(N, dp1);\n"
    "    vec3 T = dp2p * du1.x + dp1p * du2.x;\n"
    "    vec3 B = dp2p * du1.y + dp1p * du2.y;\n"
    "    float inv = inversesqrt(max(dot(T, T), dot(B, B)) + 1e-12);\n"
    "    return mat3(T * inv, B * inv, N);\n"
    "}\n"
    "void main() {\n"
    "    vec3 N = normalize(vNormal);\n"
    "    if (!gl_FrontFacing) N = -N;\n"
    "    vec3 tn = texture2D(uNorm, vUV).rgb * 2.0 - 1.0;\n"
    "    tn.xy *= uNormalStr;\n"
    "    N = normalize(m3_frame(N, vWorld, vUV) * normalize(tn));\n"
    "    gl_FragColor = vec4(N * 0.5 + 0.5, uClass);\n"
    "}\n";

/* WHAT THE CHAIN CALLS AN AUTHORED HULL. Read by the light pass as the middle of three
   bands; see the class note in fx_post.h. */
#define M3_NORMAL_CLASS 0.35f
/* A 3D SOLDIER, still inside the light pass's object band (above 0.25, below the 0.40
   of foliage) so everything that treats him as a hull still does. The contact march is
   the one reader that tells him apart: it takes a depth sample as a solid 0.6 of a cell
   deep, which is a tank's thickness, and a man a seventh of a cell deep swept that far
   drew a hard-edged dark slab on the ground beside him. */
#define M3_SOLDIER_CLASS 0.30f

static GLuint g_m3NrmProg = 0;
static int    g_m3NrmTried = 0;
/* Set for the duration of the host's normal pass and cleared with it. */
static int    g_m3LightSplit = 0;
static int    g_m3NormalPass = 0;

static GLuint g_m3Prog = 0;
static int    g_m3ProgTried = 0;

/* EVERY SHADER PARAMETER'S LOCATION, looked up once when its program links. m3_draw sets
   about forty parameters per object in each lit pass (the main view and the water's
   reflection) and eight or more in the normals pass, for every tank, yard and 3D
   soldier on the map, and asking the driver for each one by name on every draw is a
   string search that always gives the same answer. A location is fixed at link time,
   and these two programs are linked once per process and never deleted or relinked, so
   a table filled then stays right. One table per program, and not one shared, because
   the submesh parameters are set on whichever of the two is drawing and each program
   numbers them its own way. A name a program does not use holds -1 and is skipped,
   exactly as a lookup by name would be. */
struct M3Loc {
    GLint uBase, uNorm, uORM, uSunDir, uSunCol, uSkyCol, uGndCol;
    GLint uEnvOn, uEnv, uEnvMaxLod, uEnvAmb, uSH, uCamPos;
    GLint uPlayer, uCloth, uClothRef, uSide, uLogo, uPlayerGain;
    GLint uTint, uShroud, uFlash, uAlpha, uLightSplit;
    GLint uObjectGrade0, uObjectGrade1, uObjectGradeRGB;
    GLint uNormalStr, uAoStr, uRoughMin, uSpecAmb, uGamma, uClass;
    GLint uModel, uUVOffset, uPlayerOn, uClothOn, uEmblemOn;
};
static M3Loc g_m3Loc, g_m3NrmLoc;

static void m3_loc_fill(GLuint p, M3Loc& L)
{
#define M3_LOC(n) L.n = fx_glGetUniformLocation(p, #n)
    M3_LOC(uBase); M3_LOC(uNorm); M3_LOC(uORM); M3_LOC(uSunDir); M3_LOC(uSunCol);
    M3_LOC(uSkyCol); M3_LOC(uGndCol); M3_LOC(uEnvOn); M3_LOC(uEnv); M3_LOC(uEnvMaxLod);
    M3_LOC(uEnvAmb); M3_LOC(uSH); M3_LOC(uCamPos); M3_LOC(uPlayer); M3_LOC(uCloth);
    M3_LOC(uClothRef); M3_LOC(uSide); M3_LOC(uLogo); M3_LOC(uPlayerGain); M3_LOC(uTint);
    M3_LOC(uLightSplit); M3_LOC(uShroud); M3_LOC(uFlash); M3_LOC(uAlpha); M3_LOC(uNormalStr); M3_LOC(uAoStr);
    M3_LOC(uRoughMin); M3_LOC(uSpecAmb); M3_LOC(uGamma); M3_LOC(uClass); M3_LOC(uModel);
    M3_LOC(uUVOffset); M3_LOC(uPlayerOn); M3_LOC(uClothOn); M3_LOC(uEmblemOn);
    M3_LOC(uObjectGrade0); M3_LOC(uObjectGrade1); M3_LOC(uObjectGradeRGB);
#undef M3_LOC
}

/* The setters, by location: a negative one is a parameter the program does not have. */
static inline void m3_u1i(GLint l, int v) { if (l >= 0) fx_glUniform1i(l, v); }
static inline void m3_u1f(GLint l, float v) { if (l >= 0) fx_glUniform1f(l, v); }
static inline void m3_u2f(GLint l, float a, float b) { if (l >= 0) fx_glUniform2f(l, a, b); }
static inline void m3_u3f(GLint l, float a, float b, float c)
{ if (l >= 0) fx_glUniform3f(l, a, b, c); }
static inline void m3_u4fv(GLint l, int n, const float* v) { if (l >= 0) fx_glUniform4fv(l, n, v); }
static inline void m3_umat(GLint l, const float* m)
{ if (l >= 0) fx_glUniformMatrix4fv(l, 1, GL_FALSE, m); }

static GLuint m3_prog(void)
{
    if (!g_m3Prog && !g_m3ProgTried) {
        g_m3ProgTried = 1;
        if (!fx_gl_ready)
            return 0;
        GLuint vs = fx_compile(GL_VERTEX_SHADER, M3_VS, "model3d");
        if (!vs) return 0;
        GLuint fs = fx_compile(GL_FRAGMENT_SHADER, M3_FS, "model3d");
        if (!fs) { fx_glDeleteShader(vs); return 0; }
        GLuint p = fx_glCreateProgram();
        fx_glAttachShader(p, vs);
        fx_glAttachShader(p, fs);
        fx_glLinkProgram(p);
        GLint ok = 0;
        fx_glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[4096]; GLsizei n = 0;
            if (fx_glGetProgramInfoLog) fx_glGetProgramInfoLog(p, (GLsizei)sizeof log, &n, log);
            log[n < (GLsizei)sizeof log ? n : (GLsizei)sizeof log - 1] = 0;
            fprintf(stderr, "M3|program|FAILED TO LINK\n%s\n", log);
            fx_glDeleteProgram(p);
            p = 0;
        }
        fx_glDeleteShader(vs);
        fx_glDeleteShader(fs);
        if (p) m3_loc_fill(p, g_m3Loc);
        g_m3Prog = p;
    }
    return g_m3Prog;
}

static GLuint m3_nrm_prog(void)
{
    if (!g_m3NrmProg && !g_m3NrmTried) {
        g_m3NrmTried = 1;
        if (!fx_gl_ready)
            return 0;
        GLuint vs = fx_compile(GL_VERTEX_SHADER, M3_VS, "model3d-normals");
        if (!vs) return 0;
        GLuint fs = fx_compile(GL_FRAGMENT_SHADER, M3_NRM_FS, "model3d-normals");
        if (!fs) { fx_glDeleteShader(vs); return 0; }
        GLuint p = fx_glCreateProgram();
        fx_glAttachShader(p, vs);
        fx_glAttachShader(p, fs);
        fx_glLinkProgram(p);
        GLint ok = 0;
        fx_glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[4096]; GLsizei n = 0;
            if (fx_glGetProgramInfoLog) fx_glGetProgramInfoLog(p, (GLsizei)sizeof log, &n, log);
            log[n < (GLsizei)sizeof log ? n : (GLsizei)sizeof log - 1] = 0;
            fprintf(stderr, "M3|normals program|FAILED TO LINK\n%s\n", log);
            fx_glDeleteProgram(p);
            p = 0;
        }
        fx_glDeleteShader(vs);
        fx_glDeleteShader(fs);
        if (p) m3_loc_fill(p, g_m3NrmLoc);
        g_m3NrmProg = p;
    }
    return g_m3NrmProg;
}

/* ---- upload ------------------------------------------------------------------------
   Mipmapped and anisotropic-free: GL_LINEAR_MIPMAP_LINEAR on an atlas that a vehicle
   forty pixels tall samples at about a sixteenth of its resolution is the difference
   between a hull and a field of sparkle. The chain is built by hand with a box
   reduction rather than through gluBuild2DMipmaps, which is in GLU and not in GL and
   is therefore one more library to carry to Windows for something this small. */
static void m3_upload_tex(M3Tex& t)
{
    if (t.gl || t.px.empty())
        return;
    glGenTextures(1, &t.gl);
    glBindTexture(GL_TEXTURE_2D, t.gl);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    std::vector<unsigned char> cur = t.px;
    int w = t.w, h = t.h, level = 0;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, &cur[0]);
    while (w > 1 || h > 1) {
        const int nw = w > 1 ? w / 2 : 1;
        const int nh = h > 1 ? h / 2 : 1;
        std::vector<unsigned char> next((size_t)nw * nh * 4);
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++) {
                const int sx = x * 2, sy = y * 2;
                const int sx1 = (w > 1 ? sx + 1 : sx), sy1 = (h > 1 ? sy + 1 : sy);
                for (int c = 0; c < 4; c++) {
                    const int a = cur[((size_t)sy  * w + sx ) * 4 + c];
                    const int b = cur[((size_t)sy  * w + sx1) * 4 + c];
                    const int d = cur[((size_t)sy1 * w + sx ) * 4 + c];
                    const int e = cur[((size_t)sy1 * w + sx1) * 4 + c];
                    next[((size_t)y * nw + x) * 4 + c] = (unsigned char)((a + b + d + e + 2) / 4);
                }
            }
        cur.swap(next);
        w = nw; h = nh; level++;
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, w, h, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, &cur[0]);
    }
    /* THE PIXELS ARE DROPPED once they are on the card. The atlas set is 12 MB of RGBA
       per model and keeping a second copy in system memory buys nothing: nothing here
       ever re-uploads, and a lost context takes the whole window with it. */
    std::vector<unsigned char>().swap(t.px);
}

/* A RIGID MODEL'S GEOMETRY INTO GPU MEMORY, once. Drawn from system memory, every draw
   of a Construction Yard or a Mammoth, in each of the four passes that draw it (the sun
   map, the water's reflection, the main view and the normals), had the driver copy the
   whole model to the card again: 15 to 17 thousand vertices and 30 thousand indices,
   the same client-array cost the trees were measured paying before they moved into
   buffers. The same floats and the same indices reach the same pipeline from here, so
   the picture cannot change; only where the driver reads them from does.

   The entry points come through grass_buf_load, never by name: the 32-bit Windows build
   links against a GL 1.1 library that has none of them. The size of each store is read
   back, because a driver short of memory may make a store of size zero without raising
   an error, and a model drawn from that would silently vanish; on any failure the
   buffers are dropped and the model keeps its system-memory draw. The buffers live as
   long as the process, like the atlases: the models load once and never change. */
static void m3_upload_buffers(M3Model& m)
{
    if (!m.bone.empty() || m.vert.empty() || m.idx.empty() || !grass_buf_load())
        return;
    std::vector<unsigned int> all(m.idx);
    std::vector<unsigned> base;
    for (size_t l = 0; l < m.lod.size(); l++) {
        base.push_back((unsigned)all.size());
        all.insert(all.end(), m.lod[l].idx.begin(), m.lod[l].idx.end());
    }
    const unsigned long vbytes = (unsigned long)(m.vert.size() * sizeof(float));
    const unsigned long ibytes = (unsigned long)(all.size() * sizeof(unsigned int));
    GLuint b[2] = { 0, 0 };
    gr_glGenBuffers(2, b);
    GLint gotV = 0, gotI = 0;
    gr_glBindBuffer(GL_ARRAY_BUFFER, b[0]);
    gr_glBufferData(GL_ARRAY_BUFFER, (FxGLsizeiptr)vbytes, &m.vert[0], GL_STATIC_DRAW);
    gr_glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &gotV);
    gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b[1]);
    gr_glBufferData(GL_ELEMENT_ARRAY_BUFFER, (FxGLsizeiptr)ibytes, &all[0], GL_STATIC_DRAW);
    gr_glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &gotI);
    /* Unbound on every path: a bound array buffer turns every later client-array
       pointer in the program into a byte offset. */
    gr_glBindBuffer(GL_ARRAY_BUFFER, 0);
    gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    if (gotV < 0 || (unsigned long)gotV != vbytes || gotI < 0 || (unsigned long)gotI != ibytes) {
        fprintf(stderr, "M3|%s|the driver made %ld of %lu vertex and %ld of %lu index bytes; "
                        "drawn from system memory\n", m.type.c_str(), (long)gotV, vbytes,
                (long)gotI, ibytes);
        gr_glDeleteBuffers(2, b);
        return;
    }
    m.vbo = b[0];
    m.ibo = b[1];
    m.lodBase.swap(base);
    printf("M3|%s|gpu|%lu vertex bytes|%lu index bytes\n", m.type.c_str(), vbytes, ibytes);
}

static void m3_upload(M3Model& m)
{
    if (m.uploaded)
        return;
    m.uploaded = true;
    for (size_t i = 0; i < m.tex.size(); i++)
        m3_upload_tex(m.tex[i]);
    m3_upload_buffers(m);
}

/* ---- pose ---------------------------------------------------------------------------
   A SKINNED MODEL'S VERTICES FOR ONE MOMENT OF ONE CLIP, into the frame's pose cache,
   or into m.posed when the cache is full.

   `frame` is a position in the clip in its own frames, fractional, and any value: a
   looping clip wraps it and a once clip holds its ends. Two stored frames are blended
   per bone -- the rotation by a normalised lerp, which the converter made safe by
   keeping each bone's quaternions in one hemisphere across the clip, and the
   translation and scale linearly -- and each bone's pose is multiplied by its inverse
   bind to give the matrix that moves a bind-pose vertex. Every vertex then takes the
   weighted sum of up to four of those.

   ON THE CPU, NOT IN A VERTEX SHADER, and the reason is the Win98 contract as much as
   speed. A soldier is 5 to 13 thousand vertices by the level drawn (E1: 13,356 at full
   detail, 8,349 and 5,705 at the two coarser levels), and each soldier is skinned once a
   frame however many passes draw him (the frame's pose cache, m3_pose_find); a shader
   palette would have been a second vertex path for every pass that draws the model
   (lit, normals, the sun's depth, the reflection), and a CPU pose is a vertex array that
   anything, including a fixed-function pipeline, can draw. The model is still Tier 2
   only for its lighting, but the skinning is not what keeps it there.

   DETERMINISM: the caller supplies the frame, from engine state; nothing here reads a
   clock, and the arithmetic is the same on every run. */
/* One clip's bone poses at `frame`, as 8 floats a bone (quaternion xyzw, translation,
   scale), into out[nb * 8]. Loops wrap and once clips hold their ends. */
static void m3_sample(const M3Model& m, int ci, float frame, float* out)
{
    const size_t nb = m.bone.size();
    const M3Clip& c = m.clip[ci];
    const int n = c.nframe;
    int i0, i1;
    float t;
    if (c.flags & M3_CLIP_LOOP) {
        float f = fmodf(frame, (float)n);
        if (f < 0.0f) f += (float)n;
        i0 = (int)f;
        if (i0 >= n) i0 = n - 1;
        t = f - (float)i0;
        i1 = (i0 + 1 < n) ? i0 + 1 : 0;
    } else {
        float f = frame < 0.0f ? 0.0f : (frame > (float)(n - 1) ? (float)(n - 1) : frame);
        i0 = (int)f;
        t = f - (float)i0;
        i1 = (i0 + 1 < n) ? i0 + 1 : i0;
    }
    for (size_t b = 0; b < nb; b++) {
        const float* p0 = &c.pose[((size_t)i0 * nb + b) * 8];
        const float* p1 = &c.pose[((size_t)i1 * nb + b) * 8];
        float* o = out + b * 8;
        const float sgn = (p0[0] * p1[0] + p0[1] * p1[1] + p0[2] * p1[2] + p0[3] * p1[3]) < 0.0f
                          ? -1.0f : 1.0f;
        for (int k = 0; k < 4; k++) o[k] = p0[k] + (sgn * p1[k] - p0[k]) * t;
        for (int k = 4; k < 8; k++) o[k] = p0[k] + (p1[k] - p0[k]) * t;
    }
}

/* ---- the frame's poses ---------------------------------------------------------------
   ONE SKIN PER POSE PER FRAME. A 3D soldier is drawn by up to four passes a frame (his
   own sun map, the water's reflection, the main view and the normals), and each of them
   used to skin him again into the one m.posed his model shares with every other soldier
   of his type, because the next soldier's pose overwrote it between passes. The skin is
   the costly part: every vertex of the level drawn, times up to four bones.

   So the first skin of a pose in a frame is written into this cache, and every later
   draw of the same pose in that frame reads it back. THE KEY IS THE SKIN'S WHOLE INPUT:
   the model, how many vertices are posed, and the blended bone poses m3_skin is handed,
   compared to the last bit. Everything else the skin reads (the bind pose, the weights,
   the inverse binds) is fixed at load. Equal keys therefore run the same arithmetic on
   the same numbers, and the vertices read back are the bytes a fresh skin would write,
   whichever soldier or pass asks: a hit cannot be wrong, and anything that moves the
   pose (a clip, a frame, a weight, a level, the sub-tick fraction) is a different key and
   a fresh skin. Which soldier it is plays no part, so a recycled heap id, a corpse, a
   rider and a paused frame need no rule of their own.

   Only position and normal are kept, 6 floats a vertex; the draw takes the texture
   coordinates from the bind pose, which the skin never changes.

   Identical complete keys observed in consecutive frames may also retain their
   finished skin and vertex buffer in the bounded cross-frame cache (m3_rest.h).
   A model revision and its bind/skin/bone storage stamps accompany both caches.
   Edits that fit an existing vector must call m3_pose_model_changed; load and
   corpse-cap construction do so. Changed input always takes the original skin.

   The frame memory is M3_POSE_BLOCKS fixed blocks, allocated as a frame first needs them,
   refilled from the start every frame and never moved, so an entry's address holds for
   the frame. The cap is for the 32-bit Windows build's address space: past it a pose is
   skinned into m.posed per pass exactly as before, and only the saving is lost. The
   blocks go back at the end of each mission.

   OPENCNC3D_POSECHECK=1 re-skins every hit into a scratch array and compares it with the
   cached bytes; m3dump reports the count that differed, which must be 0. */
#define M3_POSE_BLOCK_FLOATS (1u << 20)      /* 4 MB a block                        */
#define M3_POSE_BLOCKS       8               /* 32 MB at most: about 240 soldiers at
                                                the coarsest level, 100 at full detail */
struct M3RestPose;
struct M3PoseModelStamp {
    unsigned long long revision;
    const void *vert, *skin, *bone;
    size_t vertN, skinN, boneN;
    bool operator==(const M3PoseModelStamp& b) const {
        return revision == b.revision && vert == b.vert && skin == b.skin && bone == b.bone
               && vertN == b.vertN && skinN == b.skinN && boneN == b.boneN;
    }
};
static M3PoseModelStamp m3_pose_model_stamp(const M3Model& m)
{
    M3PoseModelStamp s;
    s.revision = m.poseRevision;
    s.vert = m.vert.empty() ? 0 : &m.vert[0]; s.vertN = m.vert.size();
    s.skin = m.skin.empty() ? 0 : &m.skin[0]; s.skinN = m.skin.size();
    s.bone = m.bone.empty() ? 0 : &m.bone[0]; s.boneN = m.bone.size();
    return s;
}
struct M3PoseEnt {
    const M3Model* m;
    M3PoseModelStamp stamp;
    unsigned nv, nkey, hash;
    const float* key;        /* the bone poses the skin was handed: nkey floats  */
    float* out;              /* position and normal, 6 floats a vertex, nv of them */
    int gpu;                 /* out in g_m3PoseVbo[this entry]: 0 not yet, 1 yes, -1 failed */
    M3RestPose* rest;         /* stable until the next m3_pose_frame, else 0 */
};
static float* g_m3PoseBlk[M3_POSE_BLOCKS];
static int    g_m3PoseCur = 0;               /* the block being filled               */
static size_t g_m3PoseUsed = 0;              /* floats used in it                    */
static std::vector<M3PoseEnt> g_m3PoseEnt;   /* this frame's entries                 */
static std::vector<int>       g_m3PoseTab;   /* open addressing into them; -1 empty  */
/* This frame's tally, zeroed with the rest of the observer: skins written into the
   cache, draws served from it, skins that found it full, and hits that re-skinned to
   something else under OPENCNC3D_POSECHECK. */
static int g_m3PoseSkins = 0, g_m3PoseHits = 0, g_m3PoseFull = 0, g_m3PoseBad = 0;
/* ...and of the GPU copies (m3_pose_gpu): poses sent to the card, and draws that read one
   from there rather than from system memory. */
static int g_m3PoseUploads = 0, g_m3PoseGpuDraws = 0;
/* One vertex buffer per entry, entry i's pose in g_m3PoseVbo[i], kept from frame to frame
   and given each frame's pose afresh. */
static std::vector<GLuint> g_m3PoseVbo;

#include "m3_rest.h"

static int m3_pose_check(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("OPENCNC3D_POSECHECK");
        on = (e && *e && strcmp(e, "0") != 0) ? 1 : 0;
    }
    return on;
}

/* Called at the top of every drawn frame: release the previous frame's references. */
static void m3_pose_frame(void)
{
    for (size_t i = 0; i < g_m3.size(); i++)
        g_m3[i].drawPosed = 0;
    /* Retain only completed, bit-identical poses observed in consecutive frames.
       This clears the old entries before any persistent record can be evicted. */
    m3_rest_frame();
    g_m3PoseCur = 0;
    g_m3PoseUsed = 0;
    g_m3PoseEnt.clear();
    std::fill(g_m3PoseTab.begin(), g_m3PoseTab.end(), -1);
    g_m3PoseSkins = g_m3PoseHits = g_m3PoseFull = g_m3PoseBad = 0;
    g_m3PoseUploads = g_m3PoseGpuDraws = 0;
}

/* At the end of a mission: the blocks go back, and the next frame starts empty. */
static void m3_pose_free(void)
{
    for (size_t i = 0; i < g_m3.size(); i++)
        g_m3[i].drawPosed = 0;
    g_m3PoseEnt.clear();             /* release references before their owners */
    m3_rest_free();
    m3_pose_frame();
    for (int b = 0; b < M3_POSE_BLOCKS; b++) {
        free(g_m3PoseBlk[b]);
        g_m3PoseBlk[b] = 0;
    }
    std::vector<M3PoseEnt>().swap(g_m3PoseEnt);
    std::vector<int>().swap(g_m3PoseTab);
    if (!g_m3PoseVbo.empty() && grass_buf_load())
        gr_glDeleteBuffers((GLsizei)g_m3PoseVbo.size(), &g_m3PoseVbo[0]);
    std::vector<GLuint>().swap(g_m3PoseVbo);
}

static float* m3_pose_alloc(size_t n)
{
    if (n > M3_POSE_BLOCK_FLOATS)
        return 0;
    while (g_m3PoseCur < M3_POSE_BLOCKS) {
        if (!g_m3PoseBlk[g_m3PoseCur]) {
            g_m3PoseBlk[g_m3PoseCur] = (float*)malloc(M3_POSE_BLOCK_FLOATS * sizeof(float));
            g_m3PoseUsed = 0;
            if (!g_m3PoseBlk[g_m3PoseCur]) {
                g_m3PoseCur = M3_POSE_BLOCKS;        /* no more this frame */
                return 0;
            }
        }
        if (g_m3PoseUsed + n <= M3_POSE_BLOCK_FLOATS) {
            float* p = g_m3PoseBlk[g_m3PoseCur] + g_m3PoseUsed;
            g_m3PoseUsed += n;
            return p;
        }
        g_m3PoseCur++;
        g_m3PoseUsed = 0;
    }
    return 0;
}

static void m3_pose_tab_put(int e)
{
    const size_t mask = g_m3PoseTab.size() - 1;
    size_t i = g_m3PoseEnt[e].hash & mask;
    while (g_m3PoseTab[i] >= 0) i = (i + 1) & mask;
    g_m3PoseTab[i] = e;
}

/* THE LOOKUP. 1: this pose was skinned earlier this frame, and *out is its vertices.
   0: it was not, and *out is a fresh entry for the skin to write. -1: the cache is full,
   *out is 0, and the skin writes m.posed. m.drawPosed is set to *out either way. */
static int m3_pose_find(M3Model& m, size_t nv, const float* key, size_t nkey, float** out)
{
    const M3PoseModelStamp stamp = m3_pose_model_stamp(m);
    unsigned h = 2166136261u;                    /* FNV-1a over the key's bits */
    for (size_t i = 0; i < nkey; i++) {
        unsigned w;
        memcpy(&w, key + i, sizeof w);
        h = (h ^ w) * 16777619u;
    }
    h = (h ^ (unsigned)nv) * 16777619u;
    if (g_m3PoseTab.empty())
        g_m3PoseTab.assign(1024, -1);
    const size_t mask = g_m3PoseTab.size() - 1;
    for (size_t i = h & mask; g_m3PoseTab[i] >= 0; i = (i + 1) & mask) {
        const M3PoseEnt& e = g_m3PoseEnt[g_m3PoseTab[i]];
        if (e.hash == h && e.m == &m && e.stamp == stamp && e.nv == nv && e.nkey == nkey
            && memcmp(e.key, key, nkey * sizeof(float)) == 0) {
            *out = e.out;
            m.drawPosed = e.out;
            m.drawPosedEnt = g_m3PoseTab[i];
            g_m3PoseHits++;
            return 1;
        }
    }
    M3RestPose* rest = m3_rest_find(&m, stamp, nv, key, nkey, h);
    float* k = m3_pose_alloc(nkey + (rest ? 0 : nv * 6));
    if (!k) {
        if (rest) {
            /* A full frame arena can still read a retained pose as a client array. */
            *out = &rest->out[0]; m.drawPosed = *out; m.drawPosedEnt = -1;
            g_m3PoseHits++;
            return 1;
        }
        *out = 0;
        m.drawPosed = 0;
        g_m3PoseFull++;
        return -1;
    }
    memcpy(k, key, nkey * sizeof(float));
    M3PoseEnt e;
    e.m = &m; e.stamp = stamp; e.nv = (unsigned)nv; e.nkey = (unsigned)nkey; e.hash = h;
    e.key = k; e.out = rest ? &rest->out[0] : k + nkey; e.gpu = 0; e.rest = rest;
    g_m3PoseEnt.push_back(e);
    if (g_m3PoseEnt.size() * 2 > g_m3PoseTab.size()) {
        g_m3PoseTab.assign(g_m3PoseTab.size() * 2, -1);
        for (size_t j = 0; j < g_m3PoseEnt.size(); j++)
            m3_pose_tab_put((int)j);
    } else {
        m3_pose_tab_put((int)g_m3PoseEnt.size() - 1);
    }
    *out = e.out;
    m.drawPosed = e.out;
    m.drawPosedEnt = (int)g_m3PoseEnt.size() - 1;
    if (rest) { g_m3PoseHits++; return 1; }
    g_m3PoseSkins++;
    return 0;
}

/* ---- the pose on the card -----------------------------------------------------------
   A POSED SOLDIER WAS DRAWN FROM SYSTEM MEMORY IN EVERY PASS THAT DRAWS HIM (his sun map,
   the water's reflection, the main view, the normals), and a driver copies a client array
   to the card on every draw call that names it. He is 5 to 13 thousand vertices by the
   level drawn and a torn body is always the full mesh, so every pass sent all of him again,
   and a battlefield's fallen, kept for minutes, were sent four times a frame for as long
   as they lay there. Apple's driver takes that cheaply; a Windows driver pays for it in
   full on every draw.

   So each entry of the frame's pose cache goes to the card once, into its own vertex
   buffer, by the first draw that reads it, and every later draw of that pose in the frame
   (the other passes, and any other soldier standing in the same pose) reads it from
   there. The texture coordinates and the index lists never change with a pose and go to
   the card once per model (m3_skin_buffers). The same floats reach the same pipeline.
   Measured against the system-memory draw on macOS: a scattering of single pixels on
   soldiers' edges moves by a few levels (24 pixels, at most 7 levels, in a 1920 x 1080
   frame of twenty bodies), and only when every attribute comes from a buffer, so it is
   the driver fetching vertices by another route; with any one attribute left in system
   memory the frame is identical to the last bit.

   A pose the cache had no room for (m.posed) is drawn from system memory as before, and so
   is every pose on a driver without buffer objects. OPENCNC3D_POSE_GPU=0 puts them all
   back in system memory. No size is read back after an upload, unlike the load-time
   buffers: on a driver that runs GL on its own thread a query waits for that thread, and
   this runs for every pose in every frame. */
static int m3_pose_gpu_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("OPENCNC3D_POSE_GPU");
        on = (e && *e && strcmp(e, "0") == 0) ? 0 : 1;
    }
    return on;
}

static bool m3_skin_buffers(M3Model& m)
{
    if (m.skinUv && m.skinUvN == m.vert.size() && m.skinIdxN == m.idx.size())
        return true;
    if (m.skinTried && !m.skinUv)
        return false;                            /* the driver would not make them */
    m.skinTried = true;
    if (m.vert.empty() || m.idx.empty() || !grass_buf_load())
        return false;
    const size_t nv = m.vert.size() / 8;
    std::vector<float> uv(nv * 2);
    for (size_t v = 0; v < nv; v++) {
        uv[v * 2]     = m.vert[v * 8 + 6];
        uv[v * 2 + 1] = m.vert[v * 8 + 7];
    }
    std::vector<unsigned int> all(m.idx);
    std::vector<unsigned> base;
    for (size_t l = 0; l < m.lod.size(); l++) {
        base.push_back((unsigned)all.size());
        all.insert(all.end(), m.lod[l].idx.begin(), m.lod[l].idx.end());
    }
    const unsigned long ubytes = (unsigned long)(uv.size() * sizeof(float));
    const unsigned long ibytes = (unsigned long)(all.size() * sizeof(unsigned int));
    GLuint b[2] = { m.skinUv, m.skinIbo };
    if (!b[0])
        gr_glGenBuffers(2, b);
    GLint gotU = 0, gotI = 0;
    gr_glBindBuffer(GL_ARRAY_BUFFER, b[0]);
    gr_glBufferData(GL_ARRAY_BUFFER, (FxGLsizeiptr)ubytes, &uv[0], GL_STATIC_DRAW);
    gr_glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &gotU);
    gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b[1]);
    gr_glBufferData(GL_ELEMENT_ARRAY_BUFFER, (FxGLsizeiptr)ibytes, &all[0], GL_STATIC_DRAW);
    gr_glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &gotI);
    gr_glBindBuffer(GL_ARRAY_BUFFER, 0);
    gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    if (gotU < 0 || (unsigned long)gotU != ubytes || gotI < 0 || (unsigned long)gotI != ibytes) {
        fprintf(stderr, "M3|%s|the driver made %ld of %lu texture-coordinate and %ld of %lu "
                        "index bytes; poses drawn from system memory\n", m.type.c_str(),
                (long)gotU, ubytes, (long)gotI, ibytes);
        gr_glDeleteBuffers(2, b);
        m.skinUv = m.skinIbo = 0;
        return false;
    }
    m.skinUv = b[0];
    m.skinIbo = b[1];
    m.skinUvN = m.vert.size();
    m.skinIdxN = m.idx.size();
    m.skinLodBase.swap(base);
    return true;
}

/* The buffer holding the pose the next draw of m reads, sent to the card if this is the
   frame's first draw of it; 0 to draw it from system memory. */
static GLuint m3_pose_gpu(M3Model& m)
{
    if (!m.drawPosed || !m3_pose_gpu_on())
        return 0;
    const int ei = m.drawPosedEnt;
    if (ei < 0 || ei >= (int)g_m3PoseEnt.size())
        return 0;
    M3PoseEnt& e = g_m3PoseEnt[ei];
    if (e.out != m.drawPosed || e.m != &m || e.gpu < 0 || !m3_skin_buffers(m))
        return 0;
    if (e.rest) {
        const GLuint held = m3_rest_gpu(*e.rest);
        if (held) return held;
        /* An unavailable persistent store retains the original streaming fallback. */
    }
    if (e.gpu == 0) {
        if (g_m3PoseVbo.size() <= (size_t)ei) {
            const size_t had = g_m3PoseVbo.size();
            size_t want = had ? had * 2 : 64;
            while (want <= (size_t)ei) want *= 2;
            g_m3PoseVbo.resize(want, 0);
            gr_glGenBuffers((GLsizei)(want - had), &g_m3PoseVbo[had]);
        }
        if (!g_m3PoseVbo[ei]) {
            e.gpu = -1;
            return 0;
        }
        gr_glBindBuffer(GL_ARRAY_BUFFER, g_m3PoseVbo[ei]);
        gr_glBufferData(GL_ARRAY_BUFFER, (FxGLsizeiptr)((size_t)e.nv * 6 * sizeof(float)),
                        e.out, GL_STREAM_DRAW);
        gr_glBindBuffer(GL_ARRAY_BUFFER, 0);
        e.gpu = 1;
        g_m3PoseUploads++;
    }
    return g_m3PoseVbo[ei];
}

/* Skin the bind pose by per-bone poses (8 floats a bone): into the frame's pose cache, or
   read back from it when this pose was skinned earlier in the frame, or into m.posed when
   the cache is full. m.drawPosed says which the draw reads. */
static void m3_skin(M3Model& m, const float* pose)
{
    const size_t nb = m.bone.size();
    /* A coarser level uses a prefix of the vertices, and only that prefix is posed. */
    const size_t nv = (m.drawLod >= 0 && m.drawLod < (int)m.lod.size())
                    ? (size_t)m.lod[m.drawLod].nvert : m.vert.size() / 8;
    if (m.posed.size() != m.vert.size())
        m.posed = m.vert;                        /* the UVs come across once, here */
    m.pose.assign(pose, pose + nb * 8);
    float M[M3_MAX_BONES][12];
    float* cached = 0;
    const int got = m3_pose_find(m, nv, pose, nb * 8, &cached);
    if (got > 0 && !m3_pose_check())
        return;
    for (size_t b = 0; b < nb; b++) {
        const float* p = pose + b * 8;
        float q[4] = { p[0], p[1], p[2], p[3] };
        float ql = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
        ql = ql > 1e-12f ? 1.0f / sqrtf(ql) : 0.0f;
        for (int k = 0; k < 4; k++) q[k] *= ql;
        const float tx = p[4], ty = p[5], tz = p[6], sc = p[7];
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        /* H = [R*s | t], row-major 3x4 */
        const float H[12] = {
            (1 - 2 * (y * y + z * z)) * sc, (2 * (x * y - z * w)) * sc, (2 * (x * z + y * w)) * sc, tx,
            (2 * (x * y + z * w)) * sc, (1 - 2 * (x * x + z * z)) * sc, (2 * (y * z - x * w)) * sc, ty,
            (2 * (x * z - y * w)) * sc, (2 * (y * z + x * w)) * sc, (1 - 2 * (x * x + y * y)) * sc, tz };
        const float* I = m.bone[b].ib;
        float* o = M[b];
        for (int r = 0; r < 3; r++) {
            for (int col = 0; col < 3; col++)
                o[r * 4 + col] = H[r * 4] * I[col] + H[r * 4 + 1] * I[4 + col]
                               + H[r * 4 + 2] * I[8 + col];
            o[r * 4 + 3] = H[r * 4] * I[3] + H[r * 4 + 1] * I[7] + H[r * 4 + 2] * I[11]
                         + H[r * 4 + 3];
        }
    }

    /* Where this skin goes: a fresh cache entry, m.posed when the cache is full, or for
       a hit being checked a scratch array compared with the entry below. */
    static std::vector<float> check;
    if (got > 0)
        check.resize(nv * 6);
    float* dst = got > 0 ? &check[0] : (cached ? cached : &m.posed[0]);
    const size_t ds = cached ? 6 : 8;
    const float* src = &m.vert[0];
    const unsigned char* sk = &m.skin[0];
    for (size_t v = 0; v < nv; v++, src += 8, dst += ds, sk += 8) {
        float px = 0, py = 0, pz = 0, nx = 0, ny = 0, nz = 0;
        for (int k = 0; k < 4; k++) {
            if (!sk[4 + k]) continue;
            const float w = (float)sk[4 + k] * (1.0f / 255.0f);
            const float* o = M[sk[k]];
            px += w * (o[0] * src[0] + o[1] * src[1] + o[2]  * src[2] + o[3]);
            py += w * (o[4] * src[0] + o[5] * src[1] + o[6]  * src[2] + o[7]);
            pz += w * (o[8] * src[0] + o[9] * src[1] + o[10] * src[2] + o[11]);
            nx += w * (o[0] * src[3] + o[1] * src[4] + o[2]  * src[5]);
            ny += w * (o[4] * src[3] + o[5] * src[4] + o[6]  * src[5]);
            nz += w * (o[8] * src[3] + o[9] * src[4] + o[10] * src[5]);
        }
        float nl = nx * nx + ny * ny + nz * nz;
        nl = nl > 1e-20f ? 1.0f / sqrtf(nl) : 0.0f;
        dst[0] = px; dst[1] = py; dst[2] = pz;
        dst[3] = nx * nl; dst[4] = ny * nl; dst[5] = nz * nl;
    }
    if (got > 0 && memcmp(&check[0], cached, nv * 6 * sizeof(float)) != 0)
        g_m3PoseBad++;
}

/* THE BLEND: up to M3_MAX_LAYERS clips, each at its own frame and weight, mixed per bone
   before skinning -- rotations by a weighted normalised sum (each aligned to the first
   layer's hemisphere), translation and scale linearly. This is what a cross-fade is: the
   outgoing clip's weight falls while the incoming one's rises, and every frame in between
   is a real mixture of the two rather than a cut. Weights need not sum to one. */
#define M3_MAX_LAYERS 4
static void m3_pose_blend(M3Model& m, int n, const int* ci, const float* frame,
                          const float* weight)
{
    const size_t nb = m.bone.size();
    const size_t nv = m.vert.size() / 8;
    /* Until a skin says otherwise the draw reads m.posed, so a call that returns below
       without skinning never leaves the draw pointed at another object's cached pose. */
    m.drawPosed = 0;
    if (!nb || m.skin.size() != nv * 8 || n <= 0)
        return;
    static float acc[M3_MAX_BONES * 8], one[M3_MAX_BONES * 8];
    float wsum = 0.0f;
    bool first = true;
    for (int l = 0; l < n && l < M3_MAX_LAYERS; l++) {
        if (ci[l] < 0 || ci[l] >= (int)m.clip.size() || !(weight[l] > 1e-4f))
            continue;
        m3_sample(m, ci[l], frame[l], one);
        const float w = weight[l];
        for (size_t b = 0; b < nb; b++) {
            float* a = acc + b * 8;
            const float* o = one + b * 8;
            if (first) {
                for (int k = 0; k < 8; k++) a[k] = o[k] * w;
            } else {
                const float sgn = (a[0] * o[0] + a[1] * o[1] + a[2] * o[2] + a[3] * o[3]) < 0.0f
                                  ? -1.0f : 1.0f;
                for (int k = 0; k < 4; k++) a[k] += sgn * o[k] * w;
                for (int k = 4; k < 8; k++) a[k] += o[k] * w;
            }
        }
        wsum += w;
        first = false;
    }
    if (first)
        return;
    const float inv = 1.0f / wsum;
    for (size_t b = 0; b < nb; b++)
        for (int k = 4; k < 8; k++) acc[b * 8 + k] *= inv;   /* rotations renormalise in skin */
    m3_skin(m, acc);
}

static void m3_pose(M3Model& m, int ci, float frame)
{
    const float w = 1.0f;
    m3_pose_blend(m, 1, &ci, &frame, &w);
}

static int m3_clip(const M3Model& m, const char* name)
{
    for (size_t i = 0; i < m.clip.size(); i++)
        if (m.clip[i].name == name)
            return (int)i;
    return -1;
}

/* ---- draw ---------------------------------------------------------------------------
   The caller owns the placement and hands it in, for the same reason draw_mesh's
   callers do: the facing, the turret delta, the lean and the lift are the engine's
   answers and this module has no business asking for them a second time.

   `turyaw` is the engine's own 0..255 turret facing delta, the same number the pack
   path turns a turret part by. `treadV` is the belt scroll, accumulated per tick in
   the object parse.  `shroud` is 0..1. */
struct M3Draw {
    float cx, cz;          /* world cells                                      */
    float ground;          /* terrain height under the anchor                  */
    float ylift;           /* altitude, cells                                  */
    float yaw;             /* degrees about +Y, from the engine's facing       */
    float wobX, wobZ;      /* the cartridge's washboard lean, radians          */
    float turyaw;          /* fractional render turret delta, 0..255                             */
    float treadV;          /* belt scroll                                      */
    float shroud;          /* 0..1                                             */
    float pr, pg, pb;      /* the house tint                                   */
    float tintR, tintG, tintB;
    float alpha;
    int   flash;
    float scale;           /* on top of MODEL_SCALE; 0 means 1                  */
    /* The side's cloth colour and how much of the fabric's own paint it replaces (0
       keeps the paint). Only a material flagged M3_MAT_CLOTH reads them, and every
       caller zeroes the struct first, so a draw that is not a soldier replaces nothing. */
    float cr, cg, cb, camt;
    int normalizeCloth;    /* source-led RA Allied material reference, 0 preserves legacy */
    float side;            /* the emblem an M3_MAT_EMBLEM material shows: 0 GDI, 1 Nod */
    int   soldier;         /* a 3D soldier: lit by the m3inf_ shares on top     */
    int   vehicle;         /* distinct from an authored building, for F5      */
};

/* Build the object -> world matrix in GL's column-major order.
   The order is the pack path's own, restated as matrices rather than as arithmetic on
   three floats: scale to cells, then the facing about Y, then the lean about world X
   and world Z, then the translation onto the ground. Changing this order changes where
   a leaning tank's tracks sit, so it is the one thing here that is not free. */
static void m3_matrix(const M3Draw& d, float* m)
{
    const float S = MODEL_SCALE * (d.scale > 0.0f ? d.scale : 1.0f);
    const float ry = d.yaw * (float)M_PI / 180.0f;
    const float cy = cosf(ry), sy = sinf(ry);
    const float cxr = cosf(d.wobX), sxr = sinf(d.wobX);
    const float czr = cosf(d.wobZ), szr = sinf(d.wobZ);

    /* R = Rz(wobZ) * Rx(wobX) * Ry(yaw), all column vectors below. */
    float ry3[9] = { cy, 0.0f, -sy,   0.0f, 1.0f, 0.0f,   sy, 0.0f, cy };
    float rx3[9] = { 1.0f, 0.0f, 0.0f,  0.0f, cxr, sxr,   0.0f, -sxr, cxr };
    float rz3[9] = { czr, szr, 0.0f,  -szr, czr, 0.0f,   0.0f, 0.0f, 1.0f };
    float t1[9], r[9];
    for (int c = 0; c < 3; c++)
        for (int rr = 0; rr < 3; rr++) {
            float s = 0.0f;
            for (int k = 0; k < 3; k++) s += rx3[k * 3 + rr] * ry3[c * 3 + k];
            t1[c * 3 + rr] = s;
        }
    for (int c = 0; c < 3; c++)
        for (int rr = 0; rr < 3; rr++) {
            float s = 0.0f;
            for (int k = 0; k < 3; k++) s += rz3[k * 3 + rr] * t1[c * 3 + k];
            r[c * 3 + rr] = s;
        }
    for (int c = 0; c < 3; c++)
        for (int rr = 0; rr < 3; rr++)
            m[c * 4 + rr] = r[c * 3 + rr] * S;
    m[3] = m[7] = m[11] = 0.0f;
    m[12] = d.cx;
    m[13] = d.ground + d.ylift;
    m[14] = d.cz;
    m[15] = 1.0f;
}

/* Multiply a column-major 4x4 by a rotation of `deg` about +Y through the pivot p,
   which is in MODEL units and therefore lives inside the scale the matrix already
   carries. The turret is the only user; a second part role would come through here. */
static void m3_pivot_yaw(float* m, const float* p, float deg)
{
    const float rr = deg * (float)M_PI / 180.0f;
    const float c = cosf(rr), s = sinf(rr);
    /* local: translate(+p) * Ry(c,s) * translate(-p), in the model's own units */
    float l[16] = { c, 0.0f, -s, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    s, 0.0f, c, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f };
    l[12] = p[0] - (c * p[0] + s * p[2]);
    l[13] = 0.0f;
    l[14] = p[2] - (-s * p[0] + c * p[2]);
    float out[16];
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) sum += m[k * 4 + row] * l[col * 4 + k];
            out[col * 4 + row] = sum;
        }
    memcpy(m, out, sizeof out);
}

/* THE THREE LIGHTS, AS IRRADIANCE, and the panel's two dials are multipliers of these
   so that 1.0 means the shipped picture rather than a number nobody can place.

   THE ABSOLUTE SCALE COMES FROM ONE MEASUREMENT, written down because it will otherwise
   read as arbitrary: a mid grey at 18 per cent reflectance, turned square at this sun,
   should come back at about half brightness on screen, and that fixes the sun at 3.6 in
   these units. EXPOSURE then carries that figure up to what this scene actually wants.
   The tactical view runs dark -- a temperate map's grass sits near a tenth of full
   brightness -- and a hull whose albedo is six per cent, which is what real olive drab
   measures, disappears into it at the physical number. The multiplier is named here
   rather than folded into the sun's colour so that it can be argued with and so that
   the sun keeps meaning irradiance.

   The sun is warm and the sky is cool, which is the only thing in here that is a
   choice about how it should look rather than a consequence of the arithmetic. */
/* THE SKY'S OWN BRIGHTNESS, NORMALISED TO THE FILL IT REPLACES.
   The hemisphere it stands in for was tuned by eye against this scene; a photograph of
   a canyon is not, and its absolute level is a property of the camera that took it. So
   the sky's irradiance is scaled so that a surface facing straight up receives what the
   hemisphere gave it, and everything that makes an environment worth having -- its
   colour, its direction, what a metal sees in it -- survives that scaling untouched.
   Swapping the sky image then changes the LOOK and not the exposure, which is the
   behaviour anyone changing one will expect.
   It also keeps the two dials meaning what they meant: this returns zero when
   m3_ambient is zero, which is what lets a gate put every light to zero and require a
   black silhouette. */
static float m3_env_scale(void)
{
    if (!g_sky3dHave)
        return 0.0f;
    /* The irradiance the sky gives a surface facing up, evaluated the same way the
       shader does: n = (0, 1, 0) kills every term but four. */
    float up[3];
    for (int c = 0; c < 3; c++)
        up[c] = g_sky3dSH[0][c] * 0.282095f
              + g_sky3dSH[1][c] * 0.488603f
              - g_sky3dSH[6][c] * 0.315392f
              - g_sky3dSH[8][c] * 0.546274f;
    const float luma = 0.2126f * up[0] + 0.7152f * up[1] + 0.0722f * up[2];
    if (luma <= 1e-4f)
        return 0.0f;
    /* What the hemisphere's sky half is worth at a dial of 1, by the same luma. */
    const float TARGET = 0.2126f * 0.85f + 0.7152f * 1.00f + 0.0722f * 1.28f;
    return TARGET / luma;
}

static void m3_ambient(float* sky, float* gnd, float* sun, float fillK = 1.0f,
                       float sunK = 1.0f)
{
    const float EXPOSURE = 6.9f;
    const float SUN_E = 3.6f * EXPOSURE;
    /* THE FILL AS A FRACTION OF THE SUN, which is the number that decides whether a
       hull has shape. It was chosen by looking: at a fifth the camouflage pattern
       washes out and the whole tank reads as one pale slab, and at a fifteenth the
       shaded side goes to mud. A ninth keeps the pattern and keeps the far side
       readable, which is what an overcast afternoon does. */
    const float FILL = 0.089f * SUN_E;
    const float amb = g_fx.m3_ambient * fillK * FILL;
    sky[0] = 0.85f * amb; sky[1] = 1.00f * amb; sky[2] = 1.28f * amb;
    gnd[0] = 0.58f * amb; gnd[1] = 0.49f * amb; gnd[2] = 0.38f * amb;
    const float s = g_fx.m3_sun * sunK * SUN_E;
    sun[0] = 1.00f * s; sun[1] = 0.97f * s; sun[2] = 0.90f * s;
}

/* Is the caller inside a pass that wants depth and nothing else -- the sun's shadow
   map, or the squashed silhouette the renderer draws when the chain is off. Both want
   the SHAPE of this model and neither wants its shading, so the shader is skipped and
   the geometry goes down the fixed pipeline. */
static bool m3_depth_only(void)
{
    return g_meshShadowPass != 0 || g_m3CasterDepth != 0;
}

static void m3_draw(int mi, const M3Draw& d)
{
    if (mi < 0 || mi >= (int)g_m3.size())
        return;
    M3Model& m = g_m3[mi];
    m3_upload(m);

    float mat[16];
    m3_matrix(d, mat);

    const bool depthOnly = m3_depth_only();
    const bool nrmPass   = (g_m3NormalPass != 0) && !g_m3LightSplit && !depthOnly;
    const GLuint prog = depthOnly ? 0 : (nrmPass ? m3_nrm_prog() : m3_prog());
    if (!depthOnly && !prog)
        return;                 /* no shader stage: the caller drew the pack model */
    /* The locations of whichever program draws; read only when there is one. */
    const M3Loc& L = nrmPass ? g_m3NrmLoc : g_m3Loc;
    /* COUNTED ONLY ON THE PASS THE PLAYER SEES. The caster is called twice a frame from
       two places -- the sun's depth box and the water's mirrored reflection -- and the
       reflection is a lit draw, so counting every lit draw reported eight Mammoths on a
       map that holds four. The water pass runs on a desert map with no water in it,
       which is the existing renderer's behaviour for every object and not something
       this art set introduced, but it does mean an authored model is submitted twice
       whenever the water shader is on. */
    /* The level the caller chose for this object, or the full mesh. */
    const M3Lod* lv = (m.drawLod >= 0 && m.drawLod < (int)m.lod.size()) ? &m.lod[m.drawLod] : 0;
    const std::vector<M3Sub>& subs = lv ? lv->sub : m.sub;
    const unsigned int* ib = lv ? &lv->idx[0] : &m.idx[0];
    /* A rigid model draws from GPU memory when it is there (m3_upload_buffers), and
       then an index list is a byte offset into the one index buffer. */
    const bool gpu = m.vbo != 0;
    /* A posed model whose pose is on the card (m3_pose_gpu), its indices in the model's
       own index buffer the same way. */
    const GLuint poseBuf = gpu ? 0 : m3_pose_gpu(m);
    const bool bufIdx = gpu || poseBuf != 0;
    const size_t ibase = (bufIdx && lv) ? (size_t)(gpu ? m.lodBase : m.skinLodBase)[m.drawLod] : 0;
    if (!depthOnly && !g_m3Mirrored && !nrmPass) {
        g_m3Drawn++;
        /* the triangles the draw list names, which for a soldier's appended torn
           ends and split seams (infdeath_mod.h) is not the whole index array */
        size_t nt = 0;
        for (size_t s = 0; s < subs.size(); s++) nt += subs[s].nidx;
        g_m3Tris += (int)(nt / 3);
        if (d.turyaw != 0) g_m3Turn++;
    }

    GLboolean sTex2D = glIsEnabled(GL_TEXTURE_2D);
    GLboolean sCull  = glIsEnabled(GL_CULL_FACE);
    /* This draw never changes current colour. Only an enabled colour array can
       make DrawElements leave it unspecified; retain the original capture/restore
       in that case. Disabled arrays preserve the caller's exact colour directly. */
    const bool restoreColour=glIsEnabled(GL_COLOR_ARRAY)==GL_TRUE;
    GLfloat sColour[4];
    if(restoreColour)glGetFloatv(GL_CURRENT_COLOR,sColour);
    const GlsClipRestore clipBefore;

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();          /* the camera's modelview, restored on the way out */

    /* BACKFACES ARE CULLED HERE AND NOWHERE ELSE IN THIS RENDERER. The cartridge's
       display lists carry their own two-sided faces and the pack path draws every
       triangle, so culling is off globally; an authored closed hull is the one thing
       in the scene that is genuinely solid, and culling it halves its triangle count
       for nothing. Restored on the way out so the next draw sees what it expects. */
    if (g_m3Mirrored) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
    }

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    const GLsizei stride = (GLsizei)(8 * sizeof(float));
    if (gpu) {
        gr_glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
        gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
        glVertexPointer(3, GL_FLOAT, stride, (const void*)0);
        glNormalPointer(GL_FLOAT, stride, (const void*)(3 * sizeof(float)));
        glTexCoordPointer(2, GL_FLOAT, stride, (const void*)(6 * sizeof(float)));
    } else if (poseBuf) {
        /* Each pointer takes the buffer bound when it is set: position and normal from
           the pose's, the texture coordinates from the model's own. */
        const GLsizei ps = (GLsizei)(6 * sizeof(float));
        gr_glBindBuffer(GL_ARRAY_BUFFER, poseBuf);
        glVertexPointer(3, GL_FLOAT, ps, (const void*)0);
        glNormalPointer(GL_FLOAT, ps, (const void*)(3 * sizeof(float)));
        gr_glBindBuffer(GL_ARRAY_BUFFER, m.skinUv);
        glTexCoordPointer(2, GL_FLOAT, 0, (const void*)0);
        gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.skinIbo);
        g_m3PoseGpuDraws++;
    } else if (m.drawPosed) {
        /* This object's pose out of the frame's cache (m3_skin): position and normal, 6
           floats a vertex. The texture coordinates are the bind pose's own, which no
           pose changes, so they are read from vert. */
        const GLsizei ps = (GLsizei)(6 * sizeof(float));
        glVertexPointer(3, GL_FLOAT, ps, m.drawPosed);
        glNormalPointer(GL_FLOAT, ps, m.drawPosed + 3);
        glTexCoordPointer(2, GL_FLOAT, stride, &m.vert[0] + 6);
    } else {
        /* A pose the cache could not hold was written into m.posed for this object just
           before this draw; before any pose that is the bind pose, never garbage. */
        const float* vb = (!m.bone.empty() && m.posed.size() == m.vert.size())
                          ? &m.posed[0] : &m.vert[0];
        glVertexPointer(3, GL_FLOAT, stride, vb);
        glNormalPointer(GL_FLOAT, stride, vb + 3);
        glTexCoordPointer(2, GL_FLOAT, stride, vb + 6);
    }

    if (prog && nrmPass) {
        /* The normals pass wants three uniforms and none of the light. */
        fx_glUseProgram(prog);
        m3_u1i(L.uNorm, 1);
        m3_u1f(L.uNormalStr, g_fx.m3_normal);
        m3_u1f(L.uClass, d.soldier ? M3_SOLDIER_CLASS
            : (d.vehicle && fx_vehicle_active(&g_fx) ? 0.34f : M3_NORMAL_CLASS));
    } else if (prog) {
        float sky[3], gnd[3], sun[3];
        /* A soldier's light is the models' light times his own shares (m3inf_sun in
           fx_state.h): one scale on the sun, one on every part of the fill. */
        const float fillK = d.soldier ? g_fx.m3inf_fill : 1.0f;
        const float sunK  = d.soldier ? g_fx.m3inf_sun  : 1.0f;
        m3_ambient(sky, gnd, sun, fillK*fx_sky_energy(), sunK*fx_sun_energy());
        fx_sun_dir();
        fx_glUseProgram(prog);
        m3_u1i(L.uLightSplit, g_m3LightSplit);
        m3_u1i(L.uBase, 0);
        m3_u1i(L.uNorm, 1);
        m3_u1i(L.uORM,  2);
        m3_u3f(L.uSunDir, g_fxSunDir[0], g_fxSunDir[1], g_fxSunDir[2]);
        m3_u3f(L.uSunCol, sun[0], sun[1], sun[2]);
        m3_u3f(L.uSkyCol, sky[0], sky[1], sky[2]);
        m3_u3f(L.uGndCol, gnd[0], gnd[1], gnd[2]);
        /* THE SKY, AND IT RIDES THE SAME DIAL AS THE FILL IT REPLACES. uEnvAmb is
           m3_ambient's own scalar times the normalisation above, so the panel's fill
           control still governs the whole ambient term and still turns it off. A gate
           that zeroes the lights to demand a black hull keeps working, which it would
           not if the sky were added outside those dials. */
        const int envOn = (g_sky3dHave && g_fx.m3_env) ? 1 : 0;
        m3_u1i(L.uEnvOn, envOn);
        if (envOn) {
            m3_u1i(L.uEnv, 3);
            m3_u1f(L.uEnvMaxLod, sky3d_max_lod());
            m3_u1f(L.uEnvAmb, g_fx.m3_ambient * fillK * fx_sky_energy() * m3_env_scale());
            float sh[36];
            for (int i = 0; i < 9; i++) {
                sh[i * 4 + 0] = g_sky3dSH[i][0];
                sh[i * 4 + 1] = g_sky3dSH[i][1];
                sh[i * 4 + 2] = g_sky3dSH[i][2];
                sh[i * 4 + 3] = 0.0f;
            }
            m3_u4fv(L.uSH, 9, sh);
            /* Bound once for the whole model: the cube never changes between submeshes
               and unit 3 is free, the draw using 0, 1 and 2. GL_TEXTURE_2D is
               deliberately NOT enabled here -- a samplerCube reads the cube binding and
               enabling the 2D target on the same unit is what makes a driver hand back
               white. */
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_CUBE_MAP, g_sky3dTex);
            glActiveTexture(GL_TEXTURE0);
        }
        m3_u3f(L.uCamPos, g_fxCamPos[0], g_fxCamPos[1], g_fxCamPos[2]);
        m3_u3f(L.uPlayer, d.pr, d.pg, d.pb);
        m3_u3f(L.uCloth,  d.cr, d.cg, d.cb);
        m3_u1f(L.uClothRef, M3_CLOTH_REF);
        m3_u1f(L.uSide, d.side);
        m3_u3f(L.uLogo, M3_LOGO_R, M3_LOGO_G, M3_LOGO_B);
        m3_u1f(L.uPlayerGain, d.soldier ? g_fx.m3inf_paint : 1.0f);
        m3_u3f(L.uTint,   d.tintR, d.tintG, d.tintB);
        m3_u1f(L.uShroud, d.shroud);
        m3_u1f(L.uFlash,  d.flash ? 1.0f : 0.0f);
        m3_u1f(L.uAlpha,  d.alpha);
        m3_u1f(L.uNormalStr, g_fx.m3_normal);
        m3_u1f(L.uAoStr,     g_fx.m3_ao);
        m3_u1f(L.uRoughMin,  g_fx.m3_roughmin);
        m3_u1f(L.uSpecAmb,   g_fx.m3_specamb);
        m3_u1f(L.uGamma,     2.2f);
        const FxObjectGrade grade=fx_infantry_grade(&g_fx);
        const float grade0[4]={grade.bright,grade.contrast,grade.exposure,grade.gamma};
        const float grade1[4]={grade.sat,grade.warmth,grade.tint,
            d.soldier && g_fx.enabled && fx_object_grade_active(grade) ? 1.0f : 0.0f};
        if (L.uObjectGrade0>=0) fx_glUniform4fv(L.uObjectGrade0,1,grade0);
        if (L.uObjectGrade1>=0) fx_glUniform4fv(L.uObjectGrade1,1,grade1);
        m3_u3f(L.uObjectGradeRGB,grade.red,grade.green,grade.blue);
    }

    for (size_t s = 0; s < subs.size(); s++) {
        const M3Sub& sb = subs[s];
        const M3Part& pt = m.part[sb.part];
        const M3Mat& mt = m.mat[sb.mat];

        float pm[16];
        memcpy(pm, mat, sizeof pm);
        if (pt.role == M3_ROLE_TURRET && d.turyaw != 0) {
            const float piv[3] = { pt.px, pt.py, pt.pz };
            /* The pack path turns a turret by -turyaw * 2pi/256 in the same sense as
               the facing, so the same number is used here and the two art sets aim at
               the same place. */
            m3_pivot_yaw(pm, piv, -(float)d.turyaw * (360.0f / 256.0f));
        }
        /* The stack is reloaded per submesh rather than pushed and popped around the
           turret ones: a part matrix is built either way, and one code path that is
           always taken cannot drift from a second that is taken sometimes. */
        glPopMatrix();
        glPushMatrix();
        glMultMatrixf(pm);
        if (prog) {
            m3_umat(L.uModel, pm);
            const bool tread = (mt.flags & M3_MAT_TREAD) != 0;
            /* MINUS, which is the belt's own authored contract: the material was built
               to sample (u, v - phase) and the unwrap's v runs along the run of the
               track, so the sign is the artist's statement about which way the cleats
               travel and not a guess made here. The phase itself is unsigned distance
               travelled, so a vehicle in reverse still runs its track forward -- that
               is the pack path's behaviour too and it is registered rather than fixed. */
            m3_u2f(L.uUVOffset, 0.0f, tread ? -d.treadV : 0.0f);
            m3_u1f(L.uPlayerOn, (mt.flags & M3_MAT_NO_PLAYER) ? 0.0f : 1.0f);
            m3_u1f(L.uClothOn,  (mt.flags & M3_MAT_CLOTH) ? d.camt : 0.0f);
            m3_u1f(L.uClothRef,d.normalizeCloth && (mt.flags&M3_MAT_CLOTH) && mt.clothRef>1e-6f
                ? mt.clothRef : M3_CLOTH_REF);
            m3_u1f(L.uEmblemOn, (mt.flags & M3_MAT_EMBLEM) ? 1.0f : 0.0f);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, mt.orm  >= 0 ? m.tex[mt.orm ].gl : 0);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, mt.norm >= 0 ? m.tex[mt.norm].gl : 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, mt.base >= 0 ? m.tex[mt.base].gl : 0);
            glEnable(GL_TEXTURE_2D);
        } else {
            glDisable(GL_TEXTURE_2D);
        }
        if (bufIdx)
            glDrawElements(GL_TRIANGLES, (GLsizei)sb.nidx, GL_UNSIGNED_INT,
                           (const void*)((ibase + sb.idx0) * sizeof(unsigned int)));
        else
            glDrawElements(GL_TRIANGLES, (GLsizei)sb.nidx, GL_UNSIGNED_INT, ib + sb.idx0);
    }
    /* Both targets back to 0 before anything else draws: every other client-array
       draw in the program hands GL a pointer, which a bound buffer would read as an
       offset into itself. */
    if (bufIdx) {
        gr_glBindBuffer(GL_ARRAY_BUFFER, 0);
        gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
    m.drawPosed = 0;          /* one pose, one draw: the next object poses its own */

    if (prog) {
        fx_glUseProgram(0);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
        glActiveTexture(GL_TEXTURE0);
    }
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glPopMatrix();
    /* Only MODELVIEW was touched, and the outer pop restored its original top.
       Recover a previously known clip answer without two driver reads per soldier. */
    clipBefore.restored();

    if (sCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (sTex2D) glEnable(GL_TEXTURE_2D); else glDisable(GL_TEXTURE_2D);
    if(restoreColour)glColor4f(sColour[0],sColour[1],sColour[2],sColour[3]);
}

#endif /* OPENCNC3D_MODEL3D_MOD_H */
