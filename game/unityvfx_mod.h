/* ================================================================================== *
 *  unityvfx_mod.h -- Unity particle effects, replayed. EXPERIMENTAL ART ONLY.
 *
 *  WHAT THIS IS
 *    An explosion authored in Unity as a Shuriken particle-system prefab (the Vefects
 *    "Explosions VFX - URP" pack, VFX_Explosion_Generic), drawn in the game so that it
 *    looks the way Unity draws it. It replaces the cartridge's explosion for the anims
 *    listed in UVFX_CLAIM below, and only while experimental art is on.
 *
 *  THE SPLIT, and why it is where it is.
 *    Unity's SIMULATION is not reproduced. The prefab's noise module, its collisions
 *    and its private per-particle random streams cannot be matched outside Unity, so
 *    Unity runs it: tools/art/unity_vfx/Editor/OpenCNC3DVfxBake.cs steps the prefab at 60 Hz
 *    with fixed seeds and records every live particle's state, for several seeds. This
 *    file replays those recordings. Which seed and which heading an explosion gets is
 *    hashed from the engine anim's id, so the same game draws the same explosions.
 *
 *    Everything AFTER the simulation is reproduced here, and was checked against Unity
 *    rather than written from its documentation:
 *      - geometry: billboards, horizontal billboards, stretched billboards and mesh
 *        particles, view or locally aligned, rebuilt from the recorded state. Checked
 *        against Unity's own baked geometry (tools/art/unity_vfx/check_recon.py):
 *        under 2 mm off at every vertex of every system, streams exact. Three things
 *        Unity does that nobody would guess are in there because of that check: a
 *        horizontal billboard is 1/sqrt(2) of its nominal size; a view-aligned billboard
 *        turns by its Euler rotation with the ROLL negated; mesh normals take the
 *        inverse transpose of the per-axis size.
 *      - the seven shaders the explosion uses, transcribed from their URP forward pass.
 *      - URP's bloom and ACES tonemap, checked against Unity's own post-processed frames
 *        (tools/art/unity_vfx/urp_post.py): 0.08 of a level mean difference over six
 *        frames from flash to smoke. The bloom scatter is the MEASURED 0.5 rather than
 *        the 0.7 the pack's profile leaves at its default: 0.7 was two to twenty-seven
 *        levels out, 0.5 is exact.
 *
 *  THE LAYER. Unity blends these particles in linear light, in a high range buffer, and
 *    then blooms and tonemaps the result. The game's picture is none of those things: it
 *    is display encoded and graded by its own chain. So the explosion is drawn into a
 *    layer of its own -- sixteen bit float, the scene's size, depth tested against the
 *    scene's depth by hand in the shader -- that holds, per pixel, the light the effect
 *    adds (rgb) and how much of what is behind it survives (a). Alpha blended particles
 *    blend into it with the separate function (SRC_ALPHA, ONE_MINUS_SRC_ALPHA | ZERO,
 *    ONE_MINUS_SRC_ALPHA), additive ones add to rgb and leave a alone. That makes
 *    "background * a + rgb" exactly what Unity's blending would have produced over that
 *    background, whatever the background turns out to be. The layer is then bloomed,
 *    tonemapped and laid over the game's FINISHED tactical view, after the game's own
 *    grade and gamma and before the sidebar, so the game's look does not change and the
 *    explosion's does not either.
 *
 *  WHAT IS NOT REPRODUCED:
 *    the particle point light (the game's own explosion light still fires), the
 *    screen-wide vignette and chromatic aberration of the pack's demo profile, and
 *    Unity's max particle size clamp. The bloom is the effect's own light only: Unity
 *    would also bloom a background brighter than 1.0, and the game has none.
 *
 *  TIER 1: absent. No shaders, no float targets. The cartridge's explosion draws, as it
 *    always has. docs/tier1-gap.md has the row.
 * ================================================================================== */
#ifndef OPENCNC3D_UNITYVFX_MOD_H
#define OPENCNC3D_UNITYVFX_MOD_H

#include <zlib.h>
#include <vector>
#include <string>
#include <map>

#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D     /* GL 1.2; the Windows header stops at 1.1 */
#endif
#ifndef GL_FUNC_ADD
#define GL_FUNC_ADD 0x8006
#endif

/* WHICH ENGINE EXPLOSION BECOMES WHICH UNITY ONE is data, not code: vfx3d/claims.txt, one
   line per engine AnimType, "TYPE PACK LIFT", LIFT in Unity metres above the ground (the
   "Aerial" prefabs are authored for mid-air and were chosen as seen three metres up).
   Claimed by TYPE rather than by art name because two anims share one: ANIM_GRENADE and
   ANIM_VEH_HIT2 are both "VEH-HIT2" to the renderer. */
struct UvfxRule { int type, pack; float liftM, size; };   /* size: on top of the scale, 1 as made */

/* ------------------------------------------------------------------ the pack */
enum UvfxKind { UVK_PYRO, UVK_FLASH, UVK_WICKS, UVK_DECAL, UVK_SPRITEDEBRIS, UVK_CONCRETE,
                UVK_DISTORT, UVK_PYRONEW, UVK_PYROFIRE, UVK_DIRTDEBRIS,
                /* the gun pack's Shader Graphs */
                UVK_FUMES, UVK_SMOKEM, UVK_SMOKETRAIL, UVK_SPARK, UVK_EMBER,
                UVK_PYRONEWU, UVK_COUNT };
enum UvfxMode { UVM_BILLBOARD, UVM_HORIZONTAL, UVM_STRETCH, UVM_MESH };

struct UvfxTex {
    std::string name;
    int w, h, srgb, wrap, point, mips;
    GLuint gl;
    std::vector<unsigned char> px;     /* freed after upload */
};
struct UvfxMesh {
    std::vector<float> v;              /* 16 floats a vertex: pos3 nrm3 tan4 uv2 col4 */
    std::vector<unsigned> idx;
};
struct UvfxMat {
    UvfxKind kind;
    std::map<std::string, std::vector<float> > p;   /* every float and colour of the .mat */
    std::map<std::string, int> tex;                 /* slot -> texture index */
    float f(const char* k, float d) const {
        std::map<std::string, std::vector<float> >::const_iterator it = p.find(k);
        return (it == p.end() || it->second.empty()) ? d : it->second[0];
    }
    void v4(const char* k, float* o, float d) const {
        std::map<std::string, std::vector<float> >::const_iterator it = p.find(k);
        for (int i = 0; i < 4; i++)
            o[i] = (it != p.end() && (int)it->second.size() > i) ? it->second[i] : d;
    }
};
struct UvfxSys {
    int index;                         /* the system's slot in the recording */
    UvfxMode mode;
    int viewAligned;
    int mat;
    std::vector<int> meshes;
    float fudge, lengthScale, velocityScale;
    /* the system's transform scale (a "Large" variant is its prefab at x2): it multiplies
       the particle SIZE, except for a mesh aligned to the system, and nothing else --
       fitted against Unity's own geometry (tools/art/unity_vfx/recon.py system_scale) */
    float scale;
    int tilesX, tilesY;
    /* A LOCAL-SPACE system records its particles in its own space; where that space sits
       off the prefab's origin (the gun pack's flash and smoke sit centimetres along the
       barrel) its offset and turn come with it, in Unity metres. */
    int local;
    float loff[3], lrot[9];
    int simLocal;                      /* simulates in its own space (moves with its emitter) */
    std::string name;
};
/* One recorded seed: frame f's system s is recs[off[f*nsys+s] .. +cnt] of 30 floats. */
struct UvfxSeed {
    std::vector<float> recs;
    std::vector<unsigned> off, cnt;
};
enum { UVR_SEED = 0, UVR_POS = 1, UVR_ROT = 4, UVR_SIZE = 7, UVR_COL = 10, UVR_VEL = 14,
       UVR_LIFE = 17, UVR_START = 18, UVR_C1 = 19, UVR_C2 = 23, UVR_TILE = 27, UVR_MESH = 29,
       UVR_N = 30 };

/* One Unity prefab, recorded: its systems, materials, meshes and seeds. Textures are
   not here: several prefabs share the same 2048 flipbooks, so they live once, by name,
   in g_uvTex (vfx3d/_textures/). */
/* A system with a Lights module: each of its live particles is a point light. */
struct UvfxLightSpec {
    int sys;
    UvfxSys where;                     /* only its local offset and turn are used */
    float col[3];                      /* linear colour times intensity */
    float range, intensityMul, rangeMul;
    int useParticleColor, sizeAffectsRange, alphaAffectsIntensity;
};
/* A TRAILS MODULE in Ribbon mode: the system's live particles joined oldest first into a
   strip facing the camera, and (attach) on to the emitter itself. Unity's curves along
   the trail are sampled at 33 points. */
struct UvfxRibbonSpec {
    int sys, mat, mode, texMode, sizeWidth, inherit, attach;
    float widthMul;
    UvfxSys where;
    float width[33], color[33][4];
};
/* A TRAIL RENDERER (the component): a ribbon through the emitter's own past positions,
   each kept `time` seconds, its width and colour read off Unity's curves by the point's
   place along the line, 0 at the newest end. */
struct UvfxLineSpec {
    int mat, texMode;
    float time, widthMul, texScale[2];
    float width[33], color[33][4];
};
/* ONE PARTICLE'S WHOLE LIFE in a trail recording: born at tau seconds into the bake, when
   the emitter had flown bdist metres; its records are consecutive frames from `first`. */
struct UvfxLife {
    float tau, bdist;
    int first;
    std::vector<unsigned> rec;         /* index of its record in each frame, first.. */
};
struct UvfxPack {
    std::string name;
    std::vector<UvfxLightSpec> lights;
    std::vector<UvfxRibbonSpec> ribbons;
    std::vector<UvfxLineSpec> lines;
    float move[3], moveStop;           /* a trail recording's flight; move 0 for a still one */
    int trail;
    std::vector<std::vector<std::vector<UvfxLife> > > lives;   /* [seed][system], by bdist */
    int fps, frames, nsys;
    std::vector<UvfxMesh> mesh;
    std::vector<UvfxMat>  mat;
    std::vector<UvfxSys>  sys;
    std::vector<UvfxSeed> seed;
};
static std::vector<UvfxTex>      g_uvTex;
static std::map<std::string, int> g_uvTexIdx;
static std::vector<UvfxPack>     g_uvPacks;
static std::vector<UvfxRule>     g_uvRules;
static struct { int loaded, failed, uploaded; } g_uv;

/* ------------------------------------------------------------------ instances */
struct UvfxInst {
    int   animId, seed, pack;
    int   anyShroud;                   /* a superweapon: drawn under the shroud too */
    int   surfaced;                    /* its start point has been checked against the picture */
    float lift;                        /* Unity metres above the ground */
    float spawnT;                      /* engine time (ticks) of first sight */
    float x, y, z, yaw;
    /* WHERE UNITY'S AXES GO in the game's world, row-major (game = A * unity), with the
       handedness flip in it. An explosion stands upright and is only turned about the
       vertical (uvfx_basis_yaw); a muzzle flash points down its barrel. */
    float A[9];
    float k;                           /* size on top of the metres-to-cells scale */
    /* A MUZZLE FLASH RIDES ITS GUN: the firer's socket key and the socket's slot in that
       key's list, re-read every frame the firer is drawn (-1: stays where it started). */
    int   follow, sock;
};
static std::vector<UvfxInst> g_uvInst;

/* THE TEST STAGE (script: uvfxstage SEED SECONDS / uvfxstage off). One explosion at a
   fixed age, seen from EXACTLY the camera Unity's reference frames were rendered from
   (OpenCNC3DVfxBake.cs defaults: 50 degree field, pitched 48.7 degrees, 36 m back, the
   pack's demo sun), over black and with no scene depth, so a shot of it can be diffed
   pixel for pixel against Unity's own frame. Nothing else in the picture changes: the
   game still draws underneath and the composite simply ignores it. */
static int g_uvStageTrail = 0;        /* the stage is showing a trail (uvfx_after_resolve) */
static struct { int on, seed, pack; float t; float dist; int aim; float lift; } g_uvStage =
    { 0, 0, 0, 0.0f, 36.0f, 0, 0.0f };
/* WHAT HAS BEEN CLAIMED, by engine anim id. The engine REUSES an anim's slot as soon as
   it is done with it, so a remembered id alone would swallow whatever anim takes the
   slot next -- a muzzle flash, a fire -- and draw nothing for it. An entry therefore
   holds the anim's type and last stage: the same type at the same or a later stage is
   the same anim; anything else in that slot is a new one. */
struct UvfxClaimed { int type, lastStage, lastSeen; };
static std::map<int, UvfxClaimed> g_uvClaimed;
/* AND IT MUST HAVE BEEN THERE AT THE LAST STEP. A live anim is seen at every step, so a
   slot empty at the previous step holds a new anim whatever its stage says. The stage
   alone was not enough: a tank shell's hit (VEH-HIT3) reports stage 0 all its short life,
   so every later hit landing in the same slot read as the first one still playing, and a
   tank firing steadily at one target drew its first explosion and then none at all. */
static int g_uvClaimStep = -1, g_uvClaimPrev = -1;
static int g_uvSpawned = 0, g_uvDrawnFrames = 0, g_uvLastLive = 0;

/* ------------------------------------------------------------------ loading */
static bool uvfx_read_file(const std::string& path, std::vector<unsigned char>& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    size_t r = n > 0 ? fread(&out[0], 1, (size_t)n, f) : 0;
    fclose(f);
    return (long)r == n;
}
static unsigned uvfx_u32(const unsigned char* p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}
static bool uvfx_inflate(const unsigned char* src, size_t n, std::vector<unsigned char>& out, size_t raw)
{
    out.resize(raw);
    uLongf dl = (uLongf)raw;
    return uncompress(&out[0], &dl, src, (uLong)n) == Z_OK && dl == raw;
}

static bool uvfx_load_tex(const std::string& dir, UvfxTex& t)
{
    std::vector<unsigned char> b;
    if (!uvfx_read_file(dir + "_textures/tex_" + t.name + ".c3t", b) || b.size() < 20 || memcmp(&b[0], "C3TX", 4))
        return false;
    const int w = (int)uvfx_u32(&b[4]), h = (int)uvfx_u32(&b[8]), c = (int)uvfx_u32(&b[12]);
    if (w != t.w || h != t.h || c != 4) return false;
    return uvfx_inflate(&b[20], b.size() - 20, t.px, (size_t)w * h * 4);
}

static bool uvfx_load_seed(const std::string& path, UvfxSeed& s, const UvfxPack& pk)
{
    std::vector<unsigned char> z, b;
    if (!uvfx_read_file(path, z) || z.size() < 8 || memcmp(&z[0], "C3VZ", 4)) return false;
    if (!uvfx_inflate(&z[8], z.size() - 8, b, uvfx_u32(&z[4]))) return false;
    if (b.size() < 24 || memcmp(&b[0], "C3VX", 4)) return false;
    const unsigned nsys = uvfx_u32(&b[8]), nfr = uvfx_u32(&b[12]), nf = uvfx_u32(&b[20]);
    if ((int)nsys != pk.nsys || (int)nfr != pk.frames || nf != UVR_N) return false;
    size_t o = 24;
    s.off.resize(nfr * nsys); s.cnt.resize(nfr * nsys);
    for (unsigned f = 0; f < nfr; f++) {
        if (o + 4 > b.size() || uvfx_u32(&b[o]) != nsys) return false;
        o += 4;
        for (unsigned k = 0; k < nsys; k++) {
            const unsigned n = uvfx_u32(&b[o]); o += 4;
            if (o + (size_t)n * nf * 4 > b.size()) return false;
            s.off[f * nsys + k] = (unsigned)(s.recs.size() / UVR_N);
            s.cnt[f * nsys + k] = n;
            const size_t base = s.recs.size();
            s.recs.resize(base + (size_t)n * nf);
            memcpy(&s.recs[base], &b[o], (size_t)n * nf * 4);
            o += (size_t)n * nf * 4;
        }
    }
    return o == b.size();
}

static bool uvfx_load_mesh(const std::string& path, UvfxMesh& m)
{
    std::vector<unsigned char> b;
    if (!uvfx_read_file(path, b) || b.size() < 16 || memcmp(&b[0], "C3VM", 4)) return false;
    const unsigned vc = uvfx_u32(&b[8]), ic = uvfx_u32(&b[12]);
    if (b.size() != 16 + (size_t)vc * 64 + (size_t)ic * 4) return false;
    m.v.resize((size_t)vc * 16);
    memcpy(&m.v[0], &b[16], (size_t)vc * 64);
    m.idx.resize(ic);
    memcpy(&m.idx[0], &b[16 + (size_t)vc * 64], (size_t)ic * 4);
    return true;
}

/* The next token of s split on ch, in place; NULL at the end. strtok_r by hand, because
   the Windows toolchain does not have it. */
static char* uvfx_tok(char** s, char ch)
{
    while (**s == ch) (*s)++;
    if (!**s) return NULL;
    char* t = *s;
    while (**s && **s != ch) (*s)++;
    if (**s) *(*s)++ = 0;
    return t;
}

/* key=a,b,c -> the numbers */
static std::vector<float> uvfx_nums(const char* s)
{
    std::vector<float> v;
    while (*s) {
        char* e = NULL;
        float x = (float)strtod(s, &e);
        if (e == s) break;
        v.push_back(x);
        s = (*e == ',') ? e + 1 : e;
        if (*e != ',') break;
    }
    return v;
}

static void uvfx_upload(void);
/* One pack, vfx3d/<name>/fx.txt and its files; its textures into the shared table. */
static float uvfx_srgb2lin(float c);
static bool uvfx_life_before(const UvfxLife& a, const UvfxLife& b) { return a.bdist < b.bdist; }
static bool uvfx_load_pack(const std::string& root, const std::string& name, UvfxPack& pk)
{
    const std::string dir = root + name + "/";
    pk.name = name; pk.fps = pk.frames = pk.nsys = 0;
    pk.move[0] = pk.move[1] = pk.move[2] = 0.0f; pk.moveStop = 1e9f; pk.trail = 0;
    std::vector<unsigned char> txt;
    if (!uvfx_read_file(dir + "fx.txt", txt)) {
        fprintf(stderr, "unityvfx: no %sfx.txt\n", dir.c_str());
        return false;
    }
    txt.push_back(0);
    int nseeds = 0, maxsys = 0;
    char* cur = (char*)&txt[0];
    for (char* line = uvfx_tok(&cur, '\n'); line; line = uvfx_tok(&cur, '\n')) {
        char* w[128];
        int nw = 0;
        char* lc = line;
        for (char* t = uvfx_tok(&lc, ' '); t && nw < 128; t = uvfx_tok(&lc, ' ')) w[nw++] = t;
        if (!nw) continue;
        if (!strcmp(w[0], "fps") && nw > 1) pk.fps = atoi(w[1]);
        else if (!strcmp(w[0], "move") && nw > 3) {
            for (int k = 0; k < 3; k++) pk.move[k] = (float)atof(w[1 + k]);
            pk.trail = 1;
        }
        else if (!strcmp(w[0], "movestop") && nw > 1) pk.moveStop = (float)atof(w[1]);
        else if (!strcmp(w[0], "line") && nw >= 3) {
            UvfxLineSpec L;
            L.mat = 0; L.texMode = 0; L.time = 1.0f; L.widthMul = 1.0f; L.texScale[0] = L.texScale[1] = 1.0f;
            for (int k = 0; k < 33; k++) { L.width[k] = 1.0f; for (int c = 0; c < 4; c++) L.color[k][c] = 1.0f; }
            for (int i = 2; i < nw; i++) {
                char* eq = strchr(w[i], '=');
                if (!eq) continue;
                *eq = 0;
                const char* v = eq + 1;
                if (!strcmp(w[i], "mat")) L.mat = atoi(v);
                else if (!strcmp(w[i], "tex")) L.texMode = atoi(v);
                else if (!strcmp(w[i], "time")) L.time = (float)atof(v);
                else if (!strcmp(w[i], "widthMul")) L.widthMul = (float)atof(v);
                else if (!strcmp(w[i], "texScale")) {
                    std::vector<float> n = uvfx_nums(v);
                    if (n.size() == 2) { L.texScale[0] = n[0]; L.texScale[1] = n[1]; }
                } else if (!strcmp(w[i], "width")) {
                    std::vector<float> n = uvfx_nums(v);
                    for (int k = 0; k < 33 && k < (int)n.size(); k++) L.width[k] = n[k];
                } else if (!strcmp(w[i], "color")) {
                    std::vector<float> n = uvfx_nums(v);
                    for (int k = 0; k < 33 && k * 4 + 3 < (int)n.size(); k++)
                        for (int c = 0; c < 4; c++) L.color[k][c] = n[k * 4 + c];
                }
            }
            pk.lines.push_back(L);
            pk.trail = 1;
        }
        else if (!strcmp(w[0], "ribbon") && nw >= 3) {
            UvfxRibbonSpec R;
            R.sys = atoi(w[1]); R.mat = 0; R.mode = 1; R.texMode = 3; R.sizeWidth = 1; R.inherit = 1;
            R.attach = 1; R.widthMul = 1.0f;
            R.where.local = 0; R.where.scale = 1;
            for (int k = 0; k < 3; k++) R.where.loff[k] = 0.0f;
            for (int k = 0; k < 9; k++) R.where.lrot[k] = (k % 4 == 0) ? 1.0f : 0.0f;
            for (int k = 0; k < 33; k++) { R.width[k] = 1.0f; for (int c = 0; c < 4; c++) R.color[k][c] = 1.0f; }
            for (int i = 2; i < nw; i++) {
                char* eq = strchr(w[i], '=');
                if (!eq) continue;
                *eq = 0;
                const char* v = eq + 1;
                if (!strcmp(w[i], "mat")) R.mat = atoi(v);
                else if (!strcmp(w[i], "mode")) R.mode = atoi(v);
                else if (!strcmp(w[i], "tex")) R.texMode = atoi(v);
                else if (!strcmp(w[i], "sizeWidth")) R.sizeWidth = atoi(v);
                else if (!strcmp(w[i], "inherit")) R.inherit = atoi(v);
                else if (!strcmp(w[i], "attach")) R.attach = atoi(v);
                else if (!strcmp(w[i], "widthMul")) R.widthMul = (float)atof(v);
                else if (!strcmp(w[i], "width")) {
                    std::vector<float> n = uvfx_nums(v);
                    for (int k = 0; k < 33 && k < (int)n.size(); k++) R.width[k] = n[k];
                } else if (!strcmp(w[i], "color")) {
                    std::vector<float> n = uvfx_nums(v);
                    for (int k = 0; k < 33 && k * 4 + 3 < (int)n.size(); k++)
                        for (int c = 0; c < 4; c++) R.color[k][c] = n[k * 4 + c];
                } else if (!strcmp(w[i], "local")) {
                    std::vector<float> n = uvfx_nums(v);
                    if (n.size() == 12) {
                        R.where.local = 1;
                        for (int k = 0; k < 3; k++) R.where.loff[k] = n[k];
                        for (int k = 0; k < 9; k++) R.where.lrot[k] = n[3 + k];
                    }
                }
            }
            pk.ribbons.push_back(R);
        }
        else if (!strcmp(w[0], "frames") && nw > 1) pk.frames = atoi(w[1]);
        else if (!strcmp(w[0], "seeds") && nw > 1) nseeds = atoi(w[1]);
        else if (!strcmp(w[0], "tex") && nw >= 8) {
            if (g_uvTexIdx.count(w[1])) continue;          /* another pack brought it */
            UvfxTex t;
            t.name = w[1]; t.w = atoi(w[2]); t.h = atoi(w[3]);
            t.srgb = atoi(w[4] + 5); t.wrap = atoi(w[5] + 5); t.point = atoi(w[6] + 7) == 0;
            t.mips = atoi(w[7] + 5); t.gl = 0;
            if (!uvfx_load_tex(root, t)) {
                fprintf(stderr, "unityvfx: texture %s unreadable\n", w[1]);
                return false;
            }
            g_uvTexIdx[t.name] = (int)g_uvTex.size();
            g_uvTex.push_back(t);
        } else if (!strcmp(w[0], "mesh") && nw >= 3) {
            UvfxMesh m;
            if (!uvfx_load_mesh(dir + "mesh_" + w[2] + ".bin", m)) {
                fprintf(stderr, "unityvfx: %s mesh %s unreadable\n", name.c_str(), w[2]);
                return false;
            }
            pk.mesh.push_back(m);
        } else if (!strcmp(w[0], "mat") && nw >= 3) {
            UvfxMat m;
            static const char* const K[UVK_COUNT] = { "PYRO", "FLASH", "WICKS", "DECAL",
                                                      "SPRITEDEBRIS", "CONCRETE", "DISTORT",
                                                      "PYRONEW", "PYROFIRE", "DIRTDEBRIS",
                                                      "FUMES", "SMOKEM", "SMOKETRAIL", "SPARK",
                                                      "EMBER", "PYRONEWU" };
            m.kind = UVK_COUNT;
            for (int k = 0; k < UVK_COUNT; k++) if (!strcmp(w[2], K[k])) m.kind = (UvfxKind)k;
            if (m.kind == UVK_COUNT) {
                fprintf(stderr, "unityvfx: %s material %s uses shader kind %s, which this "
                                "renderer does not have\n", name.c_str(), w[1], w[2]);
                return false;
            }
            for (int i = 3; i < nw; i++) {
                char* eq = strchr(w[i], '=');
                if (!eq) continue;
                *eq = 0;
                std::map<std::string, int>::iterator ti = g_uvTexIdx.find(eq + 1);
                if (ti != g_uvTexIdx.end()) m.tex[w[i]] = ti->second;
                else m.p[w[i]] = uvfx_nums(eq + 1);
            }
            pk.mat.push_back(m);
        } else if (!strcmp(w[0], "light") && nw >= 3) {
            UvfxLightSpec L;
            L.sys = atoi(w[1]);
            L.col[0] = L.col[1] = L.col[2] = 1.0f;
            L.range = 10.0f; L.intensityMul = L.rangeMul = 1.0f;
            L.useParticleColor = L.sizeAffectsRange = 0; L.alphaAffectsIntensity = 1;
            L.where.local = 0; L.where.scale = 1;
            for (int k = 0; k < 3; k++) L.where.loff[k] = 0.0f;
            for (int k = 0; k < 9; k++) L.where.lrot[k] = (k % 4 == 0) ? 1.0f : 0.0f;
            float inten = 1.0f;
            for (int i = 2; i < nw; i++) {
                char* eq = strchr(w[i], '=');
                if (!eq) continue;
                *eq = 0;
                const char* v = eq + 1;
                if (!strcmp(w[i], "color")) {
                    std::vector<float> n = uvfx_nums(v);
                    /* a light's colour reaches URP linearised, times its intensity */
                    for (int k = 0; k < 3 && k < (int)n.size(); k++) L.col[k] = uvfx_srgb2lin(n[k]);
                } else if (!strcmp(w[i], "intensity")) inten = (float)atof(v);
                else if (!strcmp(w[i], "range")) L.range = (float)atof(v);
                else if (!strcmp(w[i], "intensityMultiplier")) L.intensityMul = (float)atof(v);
                else if (!strcmp(w[i], "rangeMultiplier")) L.rangeMul = (float)atof(v);
                else if (!strcmp(w[i], "useParticleColor")) L.useParticleColor = !strcmp(v, "1") || !strcmp(v, "True");
                else if (!strcmp(w[i], "sizeAffectsRange")) L.sizeAffectsRange = !strcmp(v, "1") || !strcmp(v, "True");
                else if (!strcmp(w[i], "alphaAffectsIntensity")) L.alphaAffectsIntensity = !strcmp(v, "1") || !strcmp(v, "True");
                else if (!strcmp(w[i], "local")) {
                    std::vector<float> n = uvfx_nums(v);
                    if (n.size() == 12) {
                        L.where.local = 1;
                        for (int k = 0; k < 3; k++) L.where.loff[k] = n[k];
                        for (int k = 0; k < 9; k++) L.where.lrot[k] = n[3 + k];
                    }
                }
            }
            for (int k = 0; k < 3; k++) L.col[k] *= inten;
            pk.lights.push_back(L);
        } else if (!strcmp(w[0], "sys") && nw >= 3) {
            UvfxSys s;
            s.index = atoi(w[1]);
            s.mode = UVM_BILLBOARD; s.viewAligned = 1; s.mat = 0; s.fudge = 0;
            s.lengthScale = 1; s.velocityScale = 0; s.tilesX = s.tilesY = 1; s.scale = 1;
            s.local = 0; s.simLocal = 0;
            for (int k = 0; k < 3; k++) s.loff[k] = 0.0f;
            for (int k = 0; k < 9; k++) s.lrot[k] = (k % 4 == 0) ? 1.0f : 0.0f;
            for (int i = 2; i < nw; i++) {
                char* eq = strchr(w[i], '=');
                if (!eq) continue;
                *eq = 0;
                const char* v = eq + 1;
                if (!strcmp(w[i], "mode"))
                    s.mode = !strcmp(v, "Mesh") ? UVM_MESH : !strcmp(v, "Stretch") ? UVM_STRETCH
                           : !strcmp(v, "HorizontalBillboard") ? UVM_HORIZONTAL : UVM_BILLBOARD;
                else if (!strcmp(w[i], "align")) s.viewAligned = !strcmp(v, "View");
                else if (!strcmp(w[i], "mat")) s.mat = atoi(v);
                else if (!strcmp(w[i], "meshes") && strcmp(v, "-")) {
                    std::vector<float> n = uvfx_nums(v);
                    for (size_t k = 0; k < n.size(); k++) s.meshes.push_back((int)n[k]);
                } else if (!strcmp(w[i], "fudge")) s.fudge = (float)atof(v);
                else if (!strcmp(w[i], "lengthScale")) s.lengthScale = (float)atof(v);
                else if (!strcmp(w[i], "velocityScale")) s.velocityScale = (float)atof(v);
                else if (!strcmp(w[i], "scale")) s.scale = (float)atof(v);
                else if (!strcmp(w[i], "sim")) s.simLocal = atoi(v) == 0;
                else if (!strcmp(w[i], "local")) {
                    std::vector<float> n = uvfx_nums(v);
                    if (n.size() == 12) {
                        s.local = 1;
                        for (int k = 0; k < 3; k++) s.loff[k] = n[k];
                        for (int k = 0; k < 9; k++) s.lrot[k] = n[3 + k];
                    }
                }
                else if (!strcmp(w[i], "tiles")) {
                    std::vector<float> n = uvfx_nums(v);
                    if (n.size() == 2) { s.tilesX = (int)n[0]; s.tilesY = (int)n[1]; }
                } else if (!strcmp(w[i], "name")) s.name = v;
            }
            if (s.index + 1 > maxsys) maxsys = s.index + 1;
            pk.sys.push_back(s);
        }
    }
    /* the recording says how many systems it has; the text only lists the drawn ones */
    {
        std::vector<unsigned char> z, b;
        if (!uvfx_read_file(dir + "seed0.c3vz", z) || z.size() < 8
            || !uvfx_inflate(&z[8], z.size() - 8, b, uvfx_u32(&z[4])) || b.size() < 24) {
            fprintf(stderr, "unityvfx: %sseed0.c3vz unreadable\n", dir.c_str());
            return false;
        }
        pk.nsys = (int)uvfx_u32(&b[8]);
    }
    for (int k = 0; k < nseeds; k++) {
        char p[64];
        snprintf(p, sizeof p, "seed%d.c3vz", k);
        UvfxSeed sd;
        if (!uvfx_load_seed(dir + p, sd, pk)) {
            fprintf(stderr, "unityvfx: %s%s unreadable or does not match fx.txt\n", dir.c_str(), p);
            return false;
        }
        pk.seed.push_back(sd);
    }
    /* A TRAIL'S LIVES: every particle of every seed, its birth and its records in order,
       so a projectile can lay the same particles down along its own path. */
    if (pk.trail && !pk.seed.empty()) {
        const float speed = sqrtf(pk.move[0] * pk.move[0] + pk.move[1] * pk.move[1] + pk.move[2] * pk.move[2]);
        pk.lives.resize(pk.seed.size());
        for (size_t k = 0; k < pk.seed.size(); k++) {
            const UvfxSeed& sd = pk.seed[k];
            pk.lives[k].resize(pk.nsys);
            for (int s = 0; s < pk.nsys; s++) {
                std::map<unsigned, int> byseed;
                std::vector<UvfxLife>& L = pk.lives[k][s];
                for (int f = 0; f < pk.frames; f++) {
                    const unsigned off = sd.off[f * pk.nsys + s], cnt = sd.cnt[f * pk.nsys + s];
                    for (unsigned i = 0; i < cnt; i++) {
                        const float* r = &sd.recs[(size_t)(off + i) * UVR_N];
                        unsigned key;
                        memcpy(&key, r + UVR_SEED, 4);
                        std::map<unsigned, int>::iterator it = byseed.find(key);
                        if (it == byseed.end()) {
                            UvfxLife l;
                            l.first = f;
                            l.tau = (float)f / (float)pk.fps - (r[UVR_START] - r[UVR_LIFE]);
                            l.bdist = speed * (l.tau < pk.moveStop ? l.tau : pk.moveStop);
                            if (l.tau > pk.moveStop) continue;      /* born after the flight */
                            byseed[key] = (int)L.size();
                            L.push_back(l);
                            it = byseed.find(key);
                        }
                        UvfxLife& l = L[it->second];
                        if ((int)l.rec.size() == f - l.first) l.rec.push_back(off + i);
                    }
                }
                std::stable_sort(L.begin(), L.end(), uvfx_life_before);   /* by birth distance */
            }
        }
    }
    if (pk.seed.empty() || maxsys > pk.nsys || pk.fps <= 0) {
        fprintf(stderr, "unityvfx: %s inconsistent (%d seeds, %d/%d systems)\n", name.c_str(),
                (int)pk.seed.size(), maxsys, pk.nsys);
        return false;
    }
    return true;
}

static int uvfx_pack_index(const char* name)
{
    for (size_t i = 0; i < g_uvPacks.size(); i++) if (g_uvPacks[i].name == name) return (int)i;
    return -1;
}
static std::string g_uvRoot;
/* defined with the guns and the maths further down */
static bool uvfx_has_muzzle(const char* type);
static bool uvfx_any_muzzle(void);
static bool uvfx_trail_near(float x, float z);
static int g_uvPuffsTaken = 0;
static int  uvfx_rtti_of(ObjKind k);
static void uvfx_basis_yaw(float yaw, float* A);
/* A pack by name, loaded the first time it is asked for; -1 when it cannot be. */
static int uvfx_pack_get(const char* name)
{
    int pi = uvfx_pack_index(name);
    if (pi >= 0) return pi;
    UvfxPack pk;
    if (!uvfx_load_pack(g_uvRoot, name, pk)) return -1;
    g_uvPacks.push_back(pk);
    g_uv.uploaded = 0;                  /* its textures go up at the next draw */
    return (int)g_uvPacks.size() - 1;
}
static void uvfx_load_guns(const std::string& root);
static void uvfx_load_trails(const std::string& root);
static bool uvfx_any_trail(void);
static void uvfx_trail_start(const EfxFire& f, int frame, const float* from);

static void uvfx_load(const char* base)
{
    if (g_uv.loaded || g_uv.failed) return;
    const std::string root = std::string(base && *base ? base : "") + "vfx3d/";
    g_uvRoot = root;
    std::vector<unsigned char> txt;
    if (!uvfx_read_file(root + "claims.txt", txt)) {
        fprintf(stderr, "unityvfx: no %sclaims.txt -- no Unity explosions installed, "
                        "explosions stay the cartridge's\n", root.c_str());
        g_uv.failed = 1;
        return;
    }
    txt.push_back(0);
    char* cur = (char*)&txt[0];
    for (char* line = uvfx_tok(&cur, '\n'); line; line = uvfx_tok(&cur, '\n')) {
        if (line[0] == '#') continue;
        int ty = -1; float lift = 0, size = 1.0f; char pack[128];
        if (sscanf(line, "%d %127s %f %f", &ty, pack, &lift, &size) < 2) continue;
        int pi = uvfx_pack_index(pack);
        if (pi < 0) {
            UvfxPack pk;
            if (!uvfx_load_pack(root, pack, pk)) {
                fprintf(stderr, "unityvfx: %s not loaded; anim type %d keeps the cartridge's "
                                "explosion\n", pack, ty);
                continue;
            }
            g_uvPacks.push_back(pk);
            pi = (int)g_uvPacks.size() - 1;
        }
        UvfxRule r; r.type = ty; r.pack = pi; r.liftM = lift; r.size = size > 0.0f ? size : 1.0f;
        g_uvRules.push_back(r);
    }
    if (g_uvRules.empty()) {
        fprintf(stderr, "unityvfx: claims.txt names no usable explosion\n");
        g_uv.failed = 1;
        return;
    }
    g_uv.loaded = 1;
    uvfx_load_guns(root);
    /* UPLOADED NOW, not at the first explosion: building the mipmaps of the 2048
       flipbooks is a visible stall, and the start of a mission is where a stall belongs. */
    uvfx_upload();
    const int gen = uvfx_pack_index("generic");
    g_uvStage.pack = gen >= 0 ? gen : 0;
    fprintf(stderr, "unityvfx: %d Unity explosions loaded for %d engine explosion types, "
                    "%d textures shared between them. Under experimental art only.\n",
            (int)g_uvPacks.size(), (int)g_uvRules.size(), (int)g_uvTex.size());
    for (size_t i = 0; i < g_uvRules.size(); i++)
        printf("UVFX|rule|type=%d|pack=%s|lift=%.1f\n", g_uvRules[i].type,
               g_uvPacks[g_uvRules[i].pack].name.c_str(), g_uvRules[i].liftM);
    fflush(stdout);
}

/* ------------------------------------------------------------------ switch */
static bool uvfx_available(void)
{
    return g_uv.loaded && g_fx.enabled && g_fx.experimental_textures && g_fx.uvfx_on
           && fx_gl_ready;
}

/* THE CLAIM, called by efx_step for every live anim on every step. An anim this takes
   draws nothing through the cartridge's recipe: the Unity explosion is instead. Once an
   anim is claimed it stays claimed for its whole life -- if the switch goes off mid
   explosion the cartridge's recipe does not start halfway through -- and a NEW anim in a
   reused slot is judged afresh (see UvfxClaimed). */
static const UvfxRule* uvfx_rule_for(int type)
{
    for (size_t i = 0; i < g_uvRules.size(); i++) if (g_uvRules[i].type == type) return &g_uvRules[i];
    return NULL;
}
/* SCRIPT: uvfxclaim TYPE PACK LIFT makes engine anim TYPE draw that pack from now on
   (the picker films every candidate this way); uvfxclaim TYPE off leaves it to the
   cartridge. Returns false when the pack cannot be loaded. */
static bool uvfx_claim_override(int type, const char* pack, float lift, float size)
{
    for (size_t i = 0; i < g_uvRules.size();)
        if (g_uvRules[i].type == type) g_uvRules.erase(g_uvRules.begin() + i); else i++;
    if (!strcmp(pack, "off")) return true;
    const int pi = uvfx_pack_get(pack);
    if (pi < 0) return false;
    UvfxRule r; r.type = type; r.pack = pi; r.liftM = lift; r.size = size > 0.0f ? size : 1.0f;
    g_uvRules.push_back(r);
    printf("UVFX|rule|type=%d|pack=%s|lift=%.1f|size=%.2f\n", type, pack, lift, r.size);
    fflush(stdout);
    return true;
}

static bool uvfx_claim(const EfxAnim& an, int frame)
{
    /* the step before this one: efx_step calls this for every live anim once a step */
    if (frame != g_uvClaimStep) { g_uvClaimPrev = g_uvClaimStep; g_uvClaimStep = frame; }
    std::map<int, UvfxClaimed>::iterator it = g_uvClaimed.find(an.id);
    if (it != g_uvClaimed.end()) {
        if (it->second.type == an.type && an.stage >= it->second.lastStage
            && it->second.lastSeen >= g_uvClaimPrev) {
            it->second.lastStage = an.stage;
            it->second.lastSeen = frame;
            return true;
        }
        g_uvClaimed.erase(it);                        /* a new anim in the slot */
    }
    /* forget anims long gone, so the table stays the size of a battle */
    if ((frame & 63) == 0)
        for (std::map<int, UvfxClaimed>::iterator j = g_uvClaimed.begin(); j != g_uvClaimed.end();)
            if (frame - j->second.lastSeen > 600) g_uvClaimed.erase(j++); else ++j;
    if (!uvfx_available()) return false;
    /* THE ENGINE'S OWN GUN FLASH (ANIM_MUZZLE_FLASH, ANIM_GUN_N..NW), riding a firer that
       has a Unity muzzle flash: taken and drawn as nothing, so a gun does not flash twice. */
    if ((an.type == 34 || (an.type >= 66 && an.type <= 73)) && an.attref >= 0 && uvfx_any_muzzle()) {
        for (size_t i = 0; i < g_objects.size(); i++) {
            const SimObject& o = g_objects[i];
            if (o.id != an.attref || uvfx_rtti_of(o.kind) != an.attkind) continue;
            if (!uvfx_has_muzzle(o.type)) break;
            UvfxClaimed c; c.type = an.type; c.lastStage = an.stage; c.lastSeen = frame;
            g_uvClaimed[an.id] = c;
            return true;
        }
    }
    /* THE ENGINE'S ROCKET SMOKE (ANIM_SMOKE_PUFF, laid behind a smoking projectile) where a
       Unity trail is already flying: taken and drawn as nothing */
    if (an.type == 11 && uvfx_trail_near(an.wx, an.wz)) {
        UvfxClaimed c; c.type = an.type; c.lastStage = an.stage; c.lastSeen = frame;
        g_uvClaimed[an.id] = c;
        g_uvPuffsTaken++;
        return true;
    }
    const UvfxRule* r = uvfx_rule_for(an.type);
    if (!r) return false;
    const UvfxPack& pk = g_uvPacks[r->pack];
    /* one per anim, placed where the engine put it; the seed and the heading are hashed
       from the id, so they are the same on every run of the same input */
    unsigned h = (unsigned)an.id * 2654435761u ^ (unsigned)frame * 40503u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    UvfxInst in;
    in.animId = an.id;
    in.pack   = r->pack;
    in.lift   = r->liftM;
    in.anyShroud = (an.type == 60);                   /* ANIM_ATOM_BLAST: see efx_draw_nuke_meshes */
    in.surfaced = 0;
    in.seed   = (int)(h % (unsigned)pk.seed.size());
    in.yaw    = (float)((h >> 8) & 1023) * (6.28318531f / 1024.0f);
    uvfx_basis_yaw(in.yaw, in.A);
    in.k = r->size; in.follow = -1; in.sock = -1;
    in.spawnT = engine_time();
    in.x = an.wx; in.z = an.wz; in.y = efx_ground(an.wx, an.wz);
    g_uvInst.push_back(in);
    UvfxClaimed c; c.type = an.type; c.lastStage = an.stage; c.lastSeen = frame;
    g_uvClaimed[an.id] = c;
    g_uvSpawned++;
    printf("UVFX|spawn|anim=%s|type=%d|id=%d|frame=%d|pack=%s|seed=%d|yaw=%.3f|at=%.2f,%.2f,%.2f\n",
           an.name, an.type, an.id, frame, pk.name.c_str(), in.seed, in.yaw, in.x, in.y, in.z);
    fflush(stdout);
    return true;
}
/* For the effects module: is this live anim one the Unity explosion took? */
static bool uvfx_is_claimed(const EfxAnim& an)
{
    std::map<int, UvfxClaimed>::const_iterator it = g_uvClaimed.find(an.id);
    return it != g_uvClaimed.end() && it->second.type == an.type;
}

/* ------------------------------------------------------------------ camera */
static float g_uvMV[16], g_uvP[16], g_uvVP[16], g_uvInvP[16];
static float g_uvR[3], g_uvU[3], g_uvF[3], g_uvCam[3];
static int   g_uvHaveCam = 0;

static void uvfx_capture_basis(void);
/* Taken where the cartridge's effects are drawn, so it is the world camera exactly. */
static void uvfx_muzzle_fires(void);
static int  g_uvFiresTaken = 0;           /* the shots were taken this frame, before the effects */
static void uvfx_capture_camera(void)
{
    /* THE SHOTS ARE TAKEN HERE, BEFORE THE EFFECTS DRAW, not after them. Every firer has
       been drawn by now, so its barrel socket is this frame's, and a round's launch record
       (uvfx_launch_note) exists when efx_draw first draws the round. Taken after, a new
       rocket was drawn at the engine's launch point at the soldier's feet until the next
       frame, then jumped up to the launcher. */
    if (!g_uvStage.on) { uvfx_muzzle_fires(); g_uvFiresTaken = 1; }
    glGetFloatv(GL_MODELVIEW_MATRIX, g_uvMV);
    glGetFloatv(GL_PROJECTION_MATRIX, g_uvP);
    fx_mat_mul(g_uvP, g_uvMV, g_uvVP);
    fx_mat_invert(g_uvP, g_uvInvP);
    uvfx_capture_basis();
    g_uvHaveCam = 1;
}
static void uvfx_capture_basis(void)
{
    for (int i = 0; i < 3; i++) {
        g_uvR[i] = g_uvMV[i * 4 + 0];
        g_uvU[i] = g_uvMV[i * 4 + 1];
        g_uvF[i] = -g_uvMV[i * 4 + 2];
    }
    for (int i = 0; i < 3; i++)
        g_uvCam[i] = -(g_uvMV[12] * g_uvMV[i * 4 + 0] + g_uvMV[13] * g_uvMV[i * 4 + 1]
                       + g_uvMV[14] * g_uvMV[i * 4 + 2]);
}

/* ------------------------------------------------------------------ maths */
/* Unity's Quaternion.Euler: Z, then X, then Y. m is row-major 3x3. */
static void uvfx_euler(float rxd, float ryd, float rzd, float* m)
{
    const float d = 3.14159265f / 180.0f;
    const float cx = cosf(rxd * d), sx = sinf(rxd * d), cy = cosf(ryd * d), sy = sinf(ryd * d);
    const float cz = cosf(rzd * d), sz = sinf(rzd * d);
    /* Ry * Rx * Rz */
    const float Rx[9] = { 1, 0, 0, 0, cx, -sx, 0, sx, cx };
    const float Ry[9] = { cy, 0, sy, 0, 1, 0, -sy, 0, cy };
    const float Rz[9] = { cz, -sz, 0, sz, cz, 0, 0, 0, 1 };
    float t[9];
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) {
        t[r * 3 + c] = 0;
        for (int k = 0; k < 3; k++) t[r * 3 + c] += Rx[r * 3 + k] * Rz[k * 3 + c];
    }
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) {
        m[r * 3 + c] = 0;
        for (int k = 0; k < 3; k++) m[r * 3 + c] += Ry[r * 3 + k] * t[k * 3 + c];
    }
}
static void uvfx_m3mul(const float* a, const float* b, float* o)
{
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) {
        o[r * 3 + c] = 0;
        for (int k = 0; k < 3; k++) o[r * 3 + c] += a[r * 3 + k] * b[k * 3 + c];
    }
}
static float uvfx_srgb2lin(float c)
{
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

/* THE MAPPING FROM UNITY'S NUMBERS TO THE GAME'S. Unity's world is left handed and GL's
   right handed; taking Unity's coordinates with z negated (M = diag(1,1,-1)) and giving
   the camera the game's own right/up/forward reproduces Unity's picture, not its
   mirror image -- a point's screen x and y are the same dot products either way. The
   winding a face shows on screen is therefore Unity's too, which is why the draw sets
   glFrontFace(GL_CW): Unity calls a clockwise face the front. Then the effect is
   turned by its heading, scaled from metres to cells, and put where the engine put the
   anim. */
struct UvfxFrame {
    float S;                 /* cells per Unity metre */
    float A[9];              /* Unity's axes in the game's world, row-major, M included */
    float ox, oy, oz;        /* the anim's place */
};
static inline void uvfx_dir(const UvfxFrame& F, float ux, float uy, float uz, float* o)
{
    o[0] = F.A[0] * ux + F.A[1] * uy + F.A[2] * uz;
    o[1] = F.A[3] * ux + F.A[4] * uy + F.A[5] * uz;
    o[2] = F.A[6] * ux + F.A[7] * uy + F.A[8] * uz;
}
/* An upright effect turned by `yaw` about the vertical: M = diag(1,1,-1), then the
   heading. What every explosion is. */
static void uvfx_basis_yaw(float yaw, float* A)
{
    const float c = cosf(yaw), s = sinf(yaw);
    A[0] = c;  A[1] = 0; A[2] = -s;
    A[3] = 0;  A[4] = 1; A[5] = 0;
    A[6] = -s; A[7] = 0; A[8] = -c;
}
/* AN EFFECT POINTED DOWN A BARREL: Unity's +z (the way every gun pack prefab fires)
   along `f`, its +y along `u` (made square to f), and its +x along f x u, which keeps
   the handedness flip M carries (at f = -z, u = +y this is exactly M). */
static void uvfx_basis_aim(const float* f0, const float* u0, float* A)
{
    float f[3] = { f0[0], f0[1], f0[2] };
    float l = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (l < 1e-9f) { uvfx_basis_yaw(0.0f, A); return; }
    for (int i = 0; i < 3; i++) f[i] /= l;
    float d = u0[0] * f[0] + u0[1] * f[1] + u0[2] * f[2];
    float u[3] = { u0[0] - d * f[0], u0[1] - d * f[1], u0[2] - d * f[2] };
    l = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    if (l < 1e-6f) {                                 /* straight up or down: any up will do */
        const float alt[3] = { 0.0f, 0.0f, -1.0f };
        d = alt[0] * f[0] + alt[1] * f[1] + alt[2] * f[2];
        for (int i = 0; i < 3; i++) u[i] = alt[i] - d * f[i];
        l = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    }
    for (int i = 0; i < 3; i++) u[i] /= l;
    const float r[3] = { f[1] * u[2] - f[2] * u[1], f[2] * u[0] - f[0] * u[2], f[0] * u[1] - f[1] * u[0] };
    for (int i = 0; i < 3; i++) { A[i * 3 + 0] = r[i]; A[i * 3 + 1] = u[i]; A[i * 3 + 2] = f[i]; }
}
/* A local-space system's own frame: its offset and turn inside the prefab. */
static UvfxFrame uvfx_sys_frame(const UvfxFrame& F, const UvfxSys& s)
{
    if (!s.local) return F;
    UvfxFrame G = F;
    float o[3];
    uvfx_dir(F, s.loff[0], s.loff[1], s.loff[2], o);
    G.ox += o[0] * F.S; G.oy += o[1] * F.S; G.oz += o[2] * F.S;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            G.A[r * 3 + c] = F.A[r * 3] * s.lrot[c] + F.A[r * 3 + 1] * s.lrot[3 + c] + F.A[r * 3 + 2] * s.lrot[6 + c];
    return G;
}
static inline void uvfx_point(const UvfxFrame& F, const float* u, float* o)
{
    uvfx_dir(F, u[0], u[1], u[2], o);
    o[0] = F.ox + o[0] * F.S; o[1] = F.oy + o[1] * F.S; o[2] = F.oz + o[2] * F.S;
}

/* ------------------------------------------------------------------ GL: textures, meshes */
static void uvfx_upload(void)
{
    if (g_uv.uploaded) return;
    g_uv.uploaded = 1;
    const Uint64 t0 = SDL_GetPerformanceCounter();
    for (size_t i = 0; i < g_uvTex.size(); i++) {
        UvfxTex& t = g_uvTex[i];
        if (t.gl) continue;             /* uploaded with an earlier pack */
        glGenTextures(1, &t.gl);
        glBindTexture(GL_TEXTURE_2D, t.gl);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        const GLint ifmt = t.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
        /* MIPMAPS BUILT HERE, box filtered in LINEAR light for an sRGB texture, the way
           the importer builds them; not left to the driver, whose filter is its own. */
        static float dec[256];
        static int decReady = 0;
        if (!decReady) { for (int k = 0; k < 256; k++) dec[k] = uvfx_srgb2lin(k / 255.0f); decReady = 1; }
        std::vector<unsigned char> cur = t.px;
        int w = t.w, h = t.h, level = 0;
        for (;;) {
            glTexImage2D(GL_TEXTURE_2D, level, ifmt, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, &cur[0]);
            if (!t.mips || (w == 1 && h == 1)) break;
            const int nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
            std::vector<unsigned char> nx((size_t)nw * nh * 4);
            for (int y = 0; y < nh; y++) for (int x = 0; x < nw; x++) for (int c = 0; c < 4; c++) {
                float s = 0; int n = 0;
                for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 2; dx++) {
                    const int sx = x * 2 + dx < w ? x * 2 + dx : w - 1;
                    const int sy = y * 2 + dy < h ? y * 2 + dy : h - 1;
                    const unsigned char b8 = cur[((size_t)sy * w + sx) * 4 + c];
                    const float v = (t.srgb && c < 3) ? dec[b8] : b8 / 255.0f;
                    s += v; n++;
                }
                s /= (float)n;
                if (t.srgb && c < 3)
                    s = s <= 0.0031308f ? s * 12.92f : 1.055f * powf(s, 1.0f / 2.4f) - 0.055f;
                int q = (int)(s * 255.0f + 0.5f);
                nx[((size_t)y * nw + x) * 4 + c] = (unsigned char)(q < 0 ? 0 : q > 255 ? 255 : q);
            }
            cur.swap(nx); w = nw; h = nh; level++;
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, level);
        /* Unity's "Bilinear" is bilinear WITHIN a mip, nearest BETWEEN them. */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        t.point ? GL_NEAREST : (t.mips ? GL_LINEAR_MIPMAP_NEAREST : GL_LINEAR));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, t.point ? GL_NEAREST : GL_LINEAR);
        const GLint wrap = t.wrap ? GL_CLAMP_TO_EDGE : GL_REPEAT;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
        std::vector<unsigned char>().swap(t.px);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    fprintf(stderr, "unityvfx: %d textures uploaded with their mipmaps in %.0f ms\n", (int)g_uvTex.size(),
            (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency());
}

/* ------------------------------------------------------------------ GL: programs */
static const char* UVFX_VS =
    "#version 120\n"
    "uniform mat4 uVP;\n"
    "uniform mat4 uMV;\n"
    "uniform mat4 uModel;\n"
    "uniform mat3 uNrm;\n"
    "uniform vec3 uQuadN;\n"
    "uniform float uIsMesh;\n"
    "varying vec3 vN;\n"
    "varying vec3 vW;\n"
    "varying vec2 vUV;\n"
    "varying float vEye;\n"
    "varying vec4 vC;\n"
    "void main() {\n"
    "    vC = gl_Color;\n"
    "    vec4 w = uModel * vec4(gl_Vertex.xyz, 1.0);\n"
    "    vW = w.xyz;\n"
    "    vN = uIsMesh > 0.5 ? uNrm * gl_Normal : uQuadN;\n"
    "    vUV = gl_MultiTexCoord0.xy;\n"
    "    vEye = -(uMV * w).z;\n"
    "    gl_Position = uVP * w;\n"
    "}\n";

/* The part every particle shader shares: the scene's depth read back by hand, both for
   the depth test (the layer has no depth buffer of its own) and for Unity's soft
   particles, which fade by how far the scene lies behind the particle in METRES. */
static const char* UVFX_FS_COMMON =
    "#version 120\n"
    "uniform sampler2D uDepth;\n"
    "uniform vec2  uLayer;\n"
    "uniform mat4  uInvP;\n"
    "uniform float uMPC;\n"          /* metres per cell */
    /* the particle colour, linear, as Unity hands it: one per quad, or per vertex on a
       trail's ribbon (uUseVC) */
    "uniform vec4  uColorU;\n"
    "uniform float uUseVC;\n"
    "varying vec4  vC;\n"
    "#define uColor (uUseVC > 0.5 ? vC : uColorU)\n"
    "uniform vec4  uC1, uC2;\n"
    "uniform float uTime;\n"
    "uniform vec3  uToSun, uSunCol, uCam;\n"
    "varying vec3 vN;\n"
    "varying vec3 vW;\n"
    "varying vec2 vUV;\n"
    "varying float vEye;\n"
    "float uv_eye(float d) {\n"
    "    vec2 ndc = gl_FragCoord.xy / uLayer * 2.0 - 1.0;\n"
    "    vec4 v = uInvP * vec4(ndc, d * 2.0 - 1.0, 1.0);\n"
    "    return -v.z / v.w;\n"
    "}\n"
    /* metres of scene behind this fragment; the fragment is discarded if it is behind */
    "uniform float uNoDepth;\n"
    "float uv_depth_gap() {\n"
    "    if (uNoDepth > 0.5) return 1.0e6;\n"
    "    float d = texture2D(uDepth, gl_FragCoord.xy / uLayer).r;\n"
    "    if (gl_FragCoord.z > d) discard;\n"
    "    return (uv_eye(d) - uv_eye(gl_FragCoord.z)) * uMPC;\n"
    "}\n"
    /* HLSL's saturate(gap / fade), including what a GPU does with fade == 0 */
    "float uv_fade(float gap, float fade) {\n"
    "    if (fade <= 0.0) return gap > 0.0 ? 1.0 : 0.0;\n"
    "    return clamp(gap / fade, 0.0, 1.0);\n"
    "}\n"
    "float sat(float x) { return clamp(x, 0.0, 1.0); }\n";

/* SH_Vefects_URP_VFX_Pyro_MV_Emission_Dissolve, forward pass. The flipbook frame and its
   motion-vector partner are computed exactly as the shader does, including its quirk:
   the partner frame is (time + 1) * speed, which with a speed equal to the frame count
   lands on the SAME frame, and the blend weight is frac(time), not the fraction between
   frames. Lit by the sun only, which is what URP's PBR reduces to at specular 0 and
   smoothness 0 under the pack's flat black ambient. */
/* The flipbook read the three pyro shaders share, exactly as each of them writes it. */
static const char* UVFX_FS_FLIPBOOK =
    "uniform sampler2D uFlip, uMVTex, uLUT;\n"
    "uniform float uCols, uRows, uSpeed, uStart, uMVInf, uMVOff;\n"
    "uniform float uDFEroSS, uDFEroSSS, uDFLerp, uEmMult, uLUTRange, uLUTOff;\n"
    "uniform float uEEroS, uEroS, uCamLen, uCamOff, uNearM;\n"
    "uniform vec3  uSmokeTint, uSSS, uEmTint;\n"
    "vec2 fb(vec2 uv, float t) {\n"
    "    float total = uCols * uRows;\n"
    "    float idx = floor(mod(t * uSpeed + uStart, total));\n"
    "    float x = floor(mod(idx, uCols) + 0.5);\n"
    "    float y = floor(mod((idx - x) / uCols, uRows) + 0.5);\n"
    "    y = (uRows - 1.0) - y;\n"
    "    return uv * vec2(1.0 / uCols, 1.0 / uRows) + vec2(x / uCols, y / uRows);\n"
    "}\n"
    "vec4 flipbook(float t) {\n"
    "    float fr = fract(t);\n"
    "    vec2 a = fb(vUV, t), b = fb(vUV, t + uMVOff);\n"
    "    vec2 ma = texture2D(uMVTex, a).rg * 2.0 - 1.0;\n"
    "    vec2 mb = texture2D(uMVTex, b).rg * 2.0 - 1.0;\n"
    "    return mix(texture2D(uFlip, a - ma * fr * uMVInf),\n"
    "               texture2D(uFlip, b + mb * (1.0 - fr) * uMVInf), fr);\n"
    "}\n";

static const char* UVFX_FS_PYRO =
    "void main() {\n"
    "    float gap = uv_depth_gap();\n"
    "    vec4 f = flipbook(uC1.x);\n"
    "    float ss = smoothstep(uDFEroSS, uDFEroSS + uDFEroSSS, f.r);\n"
    "    float dfade = mix(1.0, uv_fade(gap, ss), uDFLerp);\n"
    "    float a182 = sat(sat(f.a) * dfade);\n"
    "    float em = smoothstep(uC1.z, uC1.z + uEEroS, f.b);\n"
    "    float lx = sat(sat(em) * f.g) * uLUTRange + uLUTOff;\n"
    "    vec3 lut = texture2D(uLUT, vec2(lx, lx)).rgb;\n"
    "    vec3 e0 = clamp(uColor.rgb * lut * uEmTint, 0.0, 1.0);\n"
    "    float camFade = sat((vEye * uMPC - uNearM - uCamOff) / uCamLen);\n"
    "    float ero = smoothstep(uC1.w, uC1.w + uEroS, f.g);\n"
    "    vec3 base = uSmokeTint * sat(f.r) + uSSS;\n"
    "    vec3 emission = a182 * (e0 * uC1.y * uEmMult) + uSSS;\n"
    "    float alpha = sat(sat(sat(a182 * uColor.a) * camFade) * sat(ero));\n"
    "    vec3 N = normalize(vN);\n"
    "    vec3 lit = base * uSunCol * sat(dot(N, uToSun));\n"
    "    gl_FragColor = vec4(lit + emission, alpha);\n"
    "}\n";

/* SH_Vefects_URP_VFX_Pyro_MV_New: the smoke of the Molotov. The Generic pyro without the
   erosion and the fake scattering, soft against the scene at every material, lit the
   same way. */
static const char* UVFX_FS_PYRONEW =
    "uniform float uSpec, uSmooth;\n"
    "void main() {\n"
    "    float gap = uv_depth_gap();\n"
    "    vec4 f = flipbook(uC1.x);\n"
    "    float ss = smoothstep(uDFEroSS, uDFEroSS + uDFEroSSS, f.r);\n"
    "    float a182 = sat(sat(f.a) * uv_fade(gap, ss));\n"
    "    float lx = sat(f.g) * uLUTRange + uLUTOff;\n"
    "    vec3 e0 = clamp(uColor.rgb * texture2D(uLUT, vec2(lx, lx)).rgb * uEmTint, 0.0, 1.0);\n"
    "    float camFade = sat((vEye * uMPC - uNearM - uCamOff) / uCamLen);\n"
    "    vec3 base = uSmokeTint * sat(f.r);\n"
    "    vec3 emission = a182 * (e0 * uC1.y * uEmMult);\n"
    "    float alpha = sat(sat(a182 * uColor.a) * camFade);\n"
    "    gl_FragColor = vec4(uv_direct(base, uSpec, uSmooth, normalize(vN)) + emission, alpha);\n"
    "}\n";

/* SH_Vefects_URP_VFX_Pyro_MV_New_Unlit (the chaff flare's smoke): the lit one's flipbook,
   erosion and LUT glow, with no light at all: its smoke is the tint times _BaseEmission.
   Its hue shift is left out; every material using it sets _EmissiveHueShift 0, and the
   HSV round trip is then the identity. */
static const char* UVFX_FS_PYRONEWU =
    "uniform float uBaseEm;\n"
    "void main() {\n"
    "    float gap = uv_depth_gap();\n"
    "    vec4 f = flipbook(uC1.x);\n"
    "    float ss = smoothstep(uDFEroSS, uDFEroSS + uDFEroSSS, f.r);\n"
    "    float a182 = sat(sat(f.a) * uv_fade(gap, ss));\n"
    "    float lx = sat(f.g) * uLUTRange + uLUTOff;\n"
    "    vec3 e0 = clamp(uColor.rgb * texture2D(uLUT, vec2(lx, lx)).rgb * uEmTint, 0.0, 1.0);\n"
    "    float camFade = sat((vEye * uMPC - uNearM - uCamOff) / uCamLen);\n"
    "    vec3 c = uSmokeTint * sat(f.r) * uBaseEm + a182 * (e0 * uC1.y * uEmMult);\n"
    "    gl_FragColor = vec4(c, sat(sat(a182 * uColor.a) * camFade));\n"
    "}\n";

/* SH_Vefects_URP_VFX_Pyro_MV_Fire: the Molotov's flames. Unlit light only: the LUT colour
   of the green channel, eroded through the blue one by the particle's third custom value. */
static const char* UVFX_FS_PYROFIRE =
    "void main() {\n"
    "    float gap = uv_depth_gap();\n"
    "    vec4 f = flipbook(uC1.x);\n"
    "    float lx = sat(f.g) * uLUTRange + uLUTOff;\n"
    "    vec3 e0 = clamp(uColor.rgb * texture2D(uLUT, vec2(lx, lx)).rgb * uEmTint, 0.0, 1.0);\n"
    "    float ss = smoothstep(uDFEroSS, uDFEroSS + uDFEroSSS, f.r);\n"
    "    float ero = smoothstep(uC1.z, uC1.z + uEroS, f.b);\n"
    "    float camFade = sat((vEye * uMPC - uNearM - uCamOff) / uCamLen);\n"
    "    float k = sat(sat(sat(sat(f.a) * uv_fade(gap, ss)) * sat(ero)) * camFade);\n"
    "    gl_FragColor = vec4(e0 * uC1.y * uEmMult * k, 1.0);\n"
    "}\n";

/* SH_Vefects_URP_VFX_Dirt_Debris: the Artillery's clods, lit, eroding away. */
static const char* UVFX_FS_DIRTDEBRIS =
    "uniform sampler2D uTex;\n"
    "uniform vec4 uST;\n"
    "uniform float uSpec, uSmooth, uES;\n"
    "void main() {\n"
    "    uv_depth_gap();\n"
    "    float sm = sat(smoothstep(uC1.x, uC1.x + uES, texture2D(uTex, vUV * uST.xy + uST.zw).g));\n"
    "    gl_FragColor = vec4(uv_direct(uColor.rgb * sm, uSpec, uSmooth, normalize(vN)), sm);\n"
    "}\n";

/* SH_Vefects_URP_VFX_Flash: additive, the texture's green channel as the shape. */
static const char* UVFX_FS_FLASH =
    "uniform sampler2D uTex;\n"
    "uniform vec4 uST;\n"
    "uniform float uDF, uIsAdd, uEmission;\n"
    "void main() {\n"
    "    float gap = uv_depth_gap();\n"
    "    float g = texture2D(uTex, vUV * uST.xy + uST.zw).g;\n"
    "    vec3 c = uColor.rgb * g;\n"
    "    float a86 = sat(sat(g * uColor.a) * uv_fade(gap, uDF));\n"
    "    gl_FragColor = vec4(mix(c, c * a86, uIsAdd) * uEmission, 1.0);\n"
    "}\n";

/* SH_Vefects_URP_VFX_Emissive_Wicks: the sparks, additive, eroded by the particle's
   own alpha and broken up by a noise texture offset per particle. */
static const char* UVFX_FS_WICKS =
    "uniform sampler2D uTex, uNoise;\n"
    "uniform vec4 uST, uNoiseUVP;\n"
    "uniform float uES, uNoiseUVS, uEmission;\n"
    "void main() {\n"
    "    uv_depth_gap();\n"
    "    float t61 = 1.0 - uColor.a;\n"
    "    float sm = smoothstep(t61, t61 + uES, texture2D(uTex, vUV * uST.xy + uST.zw).g);\n"
    "    vec2 pan = uTime * (uNoiseUVP.xy * (uC1.x + 0.1)) + (((vUV - 0.5) * uNoiseUVS) + 0.5) + uC1.x;\n"
    "    vec3 c = clamp(clamp(uColor.rgb * sat(sm), 0.0, 1.0) * texture2D(uNoise, pan).rgb, 0.0, 1.0);\n"
    "    gl_FragColor = vec4(c * uEmission, 1.0);\n"
    "}\n";

/* SH_Vefects_URP_VFX_Fake_Decal: the scorch. Visible only where the scene lies within
   _DepthFade metres behind it, i.e. on the ground. */
static const char* UVFX_FS_DECAL =
    "uniform sampler2D uTex;\n"
    "uniform vec4 uST;\n"
    "uniform float uES, uDF, uIsAdd, uEmission;\n"
    "void main() {\n"
    "    float gap = uv_depth_gap();\n"
    "    float sm = sat(smoothstep(uC1.x, uC1.x + uES, texture2D(uTex, vUV * uST.xy + uST.zw).g));\n"
    "    vec3 c = uColor.rgb * sm;\n"
    "    float a86 = sat(sat(sm * uColor.a) * sat(1.0 - uv_fade(gap, uDF)));\n"
    "    gl_FragColor = vec4(mix(c, c * a86, uIsAdd) * uEmission, a86);\n"
    "}\n";

/* URP's direct light for a specular-setup surface, the only light these two see. */
/* PLUS THE PARTICLE LIGHTS: every live particle of a system with a Lights module (the gun
   pack's flashes carry one) is a point light, lit exactly as URP lights an additional
   light: 1/d^2 with the smooth window saturate(1 - (d^2/range^2)^2)^2, d in the metres
   of the effect that owns the light. uLP is its place (cells) and range (cells), uLC
   its colour times intensity and that effect's cells per metre. */
static const char* UVFX_FS_LIT =
    "uniform int  uNL;\n"
    "uniform vec4 uLP[8], uLC[8];\n"
    "vec3 uv_brdf(vec3 albedo, float spec, float smooth, vec3 N, vec3 L, vec3 V) {\n"
    "    vec3 diffuse = albedo * (1.0 - spec);\n"
    "    float pr = 1.0 - smooth;\n"
    "    float r = max(pr * pr, 0.0078125);\n"
    "    float r2 = r * r;\n"
    "    vec3 H = normalize(L + V);\n"
    "    float NoH = sat(dot(N, H)), LoH = sat(dot(L, H));\n"
    "    float d = NoH * NoH * (r2 - 1.0) + 1.00001;\n"
    "    float st = r2 / ((d * d) * max(0.1, LoH * LoH) * (r * 4.0 + 2.0));\n"
    "    return diffuse + vec3(spec) * st;\n"
    "}\n"
    "vec3 uv_direct(vec3 albedo, float spec, float smooth, vec3 N) {\n"
    "    vec3 V = normalize(uCam - vW);\n"
    "    vec3 c = uv_brdf(albedo, spec, smooth, N, uToSun, V) * uSunCol * sat(dot(N, uToSun));\n"
    "    for (int i = 0; i < 8; i++) {\n"
    "        if (i >= uNL) break;\n"
    "        vec3 lv = (uLP[i].xyz - vW) / uLC[i].w;\n"
    "        float d2 = max(dot(lv, lv), 6.103515625e-5);\n"
    "        float rm = uLP[i].w / uLC[i].w;\n"
    "        float f = d2 / (rm * rm);\n"
    "        float sm = sat(1.0 - f * f);\n"
    "        vec3 L = lv * inversesqrt(d2);\n"
    "        c += uv_brdf(albedo, spec, smooth, N, L, V) * uLC[i].rgb * (sm * sm / d2) * sat(dot(N, L));\n"
    "    }\n"
    "    return c;\n"
    "}\n";

/* SH_Vefects_URP_VFX_Sprite_Debris: dark chips, lit, alpha from the texture. */
static const char* UVFX_FS_SPRITEDEBRIS =
    "uniform sampler2D uTex;\n"
    "uniform vec4 uST;\n"
    "uniform float uSpec, uSmooth;\n"
    "void main() {\n"
    "    uv_depth_gap();\n"
    "    float a = sat(texture2D(uTex, vUV * uST.xy + uST.zw).g);\n"
    "    gl_FragColor = vec4(uv_direct(uColor.rgb, uSpec, uSmooth, normalize(vN)), a);\n"
    "}\n";

/* SH_Vefects_URP_VFX_Concrete: opaque tumbling chunks. */
static const char* UVFX_FS_CONCRETE =
    "uniform sampler2D uTex;\n"
    "uniform vec4 uST;\n"
    "uniform float uSpec, uSmooth;\n"
    "void main() {\n"
    "    uv_depth_gap();\n"
    "    vec3 albedo = uColor.rgb * texture2D(uTex, vUV * uST.xy + uST.zw).rgb;\n"
    "    gl_FragColor = vec4(uv_direct(albedo, uSpec, uSmooth, normalize(vN)), 1.0);\n"
    "}\n";

/* SH_Vefects_URP_VFX_Distortion_Shockwave. It does not add light: it bends the picture
   behind it. Drawn into its own target as (screen offset, coverage), which the composite
   turns back into a displaced background. */
static const char* UVFX_FS_DISTORT =
    "uniform sampler2D uCookie, uNoise;\n"
    "uniform vec4 uChan, uNoiseUVPan, uNoiseUVScale;\n"
    "uniform float uNoiseMul, uCorePow, uCoreMul, uOverall, uDF, uOpacity, uCamLen, uCamOff, uNearM;\n"
    "void main() {\n"
    "    float gap = uv_depth_gap();\n"
    "    vec2 pan = uTime * uNoiseUVPan.xy + vUV * uNoiseUVScale.xy;\n"
    "    float n = texture2D(uNoise, pan).r;\n"
    "    vec2 cuv = vUV + vec2(1.0) * ((n - 0.5) * 2.0) * uNoiseMul;\n"
    "    float ck = dot(texture2D(uCookie, cuv), uChan);\n"
    "    float t100 = uColor.a * sat(sat(pow(sat(ck), uCorePow)) * uCoreMul) * (uOverall * uv_fade(gap, uDF));\n"
    "    float camFade = sat((vEye * uMPC - uNearM - uCamOff) / uCamLen);\n"
    "    float alpha = sat(sat(uOpacity * sat(t100 * 100.0)) * camFade);\n"
    "    gl_FragColor = vec4(t100, 1.0, 0.0, alpha);\n"
    "}\n";

/* ---- THE GUN PACK'S FIVE SHADER GRAPHS (Realistic Gun VFX, URP), ported from the
   shader text Shader Graph itself generates (OpenCNC3DVfxBake.DumpShaderGraphs), node for
   node. None of them applies the material's _Texture_ST: each graph samples at the raw
   particle UV. Blend and cull come from the pack (make_pack.py): fixed in the three
   unlit graphs, the material's own in the two lit ones. */

/* S_Fumes_Master, the flash itself. Unlit, Blend One OneMinusSrcAlpha, and the colour is
   NOT multiplied by alpha: URP's unlit pass hands BaseColor straight out. */
static const char* UVFX_FS_FUMES =
    "uniform sampler2D uTex;\n"
    "uniform vec4 uEmCol;\n"
    "uniform float uColPow, uAlphaPow, uEmInt;\n"
    "void main() {\n"
    "    uv_depth_gap();\n"
    "    vec4 t = texture2D(uTex, vUV);\n"
    "    vec3 c = uEmCol.rgb * (uColor.rgb * pow(max(t.rgb, vec3(0.0)), vec3(uColPow))) * uEmInt;\n"
    "    gl_FragColor = vec4(c, uColor.a * pow(max(t.a, 0.0), uAlphaPow));\n"
    "}\n";

/* S_Spark: unlit, Blend SrcAlpha OneMinusSrcAlpha, the texture's red as both shape and
   coverage. */
static const char* UVFX_FS_SPARK =
    "uniform sampler2D uTex;\n"
    "uniform float uPow, uBoost;\n"
    "void main() {\n"
    "    uv_depth_gap();\n"
    "    float r = texture2D(uTex, vUV).r;\n"
    "    gl_FragColor = vec4(uColor.rgb * pow(max(r, 0.0), uPow) * uBoost, r * uColor.a);\n"
    "}\n";

/* S_Ember: no texture at all, a round dot cut by Shader Graph's Sphere Mask on the
   four-component UV (the default particle streams leave its z and w at zero). */
static const char* UVFX_FS_EMBER =
    "uniform float uBoost;\n"
    "void main() {\n"
    "    uv_depth_gap();\n"
    "    float d = distance(vec4(vUV, 0.0, 0.0), vec4(0.5));\n"
    "    gl_FragColor = vec4(uBoost * uColor.rgb, 1.0 - sat((d - 0.79) / (1.0 - 0.99)));\n"
    "}\n";

/* S_Smoke_Master: LIT smoke. Metallic 0, smoothness 0 and occlusion 0, so URP's PBR
   comes down to the sun on a dielectric (no ambient: GI is multiplied by the occlusion)
   plus an emission equal to the base colour itself. Soft against the scene over
   _FadeDistance metres (the graph's depth-fade sub-graph). */
static const char* UVFX_FS_SMOKEM =
    "uniform sampler2D uTex;\n"
    "uniform vec4 uCol1, uCol2;\n"
    "uniform float uColPow, uAlphaPow, uSoften, uFade, uRGBMask;\n"
    "void main() {\n"
    "    float gap = uv_depth_gap();\n"
    "    vec4 t = texture2D(uTex, vUV);\n"
    "    vec3 base = (uColor * mix(uCol1, uCol2, pow(max(t, vec4(0.0)), vec4(uColPow))) * uSoften).rgb;\n"
    "    float m = uRGBMask > 0.5 ? t.r : t.a;\n"
    "    float a = uColor.a * pow(max(m, 0.0), uAlphaPow) * uv_fade(gap, uFade);\n"
    "    gl_FragColor = vec4(uv_direct(base, 0.04, 0.0, normalize(vN)) + base, a);\n"
    "}\n";

/* S_SmokeTrail: LIT, the particle colour as base, its colour times _SoftenShadows as
   emission, coverage from the texture's red at a tiled, centre-scaled and scrolling UV.
   The scroll runs on the effect's own age (the graph's _Time, which in Unity is the
   editor's clock and no part of the effect). */
static const char* UVFX_FS_SMOKETRAIL =
    "uniform sampler2D uTex;\n"
    "uniform float uUT, uVT, uSX, uSY, uSpeed, uSoften, uAlphaMul;\n"
    "void main() {\n"
    "    uv_depth_gap();\n"
    "    vec2 sc = vec2(uSX, uSY);\n"
    "    vec2 uv = (vUV * vec2(uUT, uVT)) / sc + vec2(0.5) - vec2(0.5) / sc;\n"
    "    uv.x += uSpeed * uTime;\n"
    "    float r = texture2D(uTex, uv).r;\n"
    /* a ribbon carries no normals (the trail module's lighting data is off in every
       prefab using this graph), so it gets no direct light, only its emission */
    "    vec3 lit = dot(vN, vN) > 1e-8 ? uv_direct(uColor.rgb, 0.04, 0.0, normalize(vN)) : vec3(0.0);\n"
    /* uAlphaMul is the trail rule's OPACITY, 1 unless a rule turns a faint trail up */
    "    gl_FragColor = vec4(lit + uColor.rgb * uSoften, clamp(uColor.a * r * uAlphaMul, 0.0, 1.0));\n"
    "}\n";

static GLuint g_uvProg[UVK_COUNT];
static int    g_uvProgTried = 0;

/* glBlendFuncSeparate is GL 1.4; the Windows header stops at 1.1, so it is resolved here
   the way fx_gl.h resolves the rest. Without it the layer cannot keep its coverage and
   the feature stays off rather than drawing wrong. */
typedef void (APIENTRY *UVPFNBLENDFUNCSEPARATE)(GLenum, GLenum, GLenum, GLenum);
static UVPFNBLENDFUNCSEPARATE uv_glBlendFuncSeparate = NULL;

static GLuint uvfx_link(const char* fs1, const char* fs2, const char* label, const char* fs3 = NULL)
{
    GLuint vs = fx_compile(GL_VERTEX_SHADER, UVFX_VS, label);
    std::string src = std::string(UVFX_FS_COMMON) + (fs3 ? fs3 : "") + (fs2 ? fs2 : "") + fs1;
    GLuint fs = fx_compile(GL_FRAGMENT_SHADER, src.c_str(), label);
    if (!vs || !fs) return 0;
    GLuint p = fx_glCreateProgram();
    fx_glAttachShader(p, vs);
    fx_glAttachShader(p, fs);
    fx_glLinkProgram(p);
    GLint ok = 0;
    fx_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    fx_glDeleteShader(vs);
    fx_glDeleteShader(fs);
    if (!ok) {
        char log[4096];
        GLsizei n = 0;
        if (fx_glGetProgramInfoLog) fx_glGetProgramInfoLog(p, (GLsizei)sizeof log, &n, log);
        log[n < (GLsizei)sizeof log ? n : (GLsizei)sizeof log - 1] = 0;
        fprintf(stderr, "unityvfx: %s failed to link\n%s\n", label, log);
        fx_glDeleteProgram(p);
        return 0;
    }
    return p;
}

/* ---- the post programs: bloom (URP Gaussian) and the composite with URP's ACES ---- */
static const char* UVFX_FS_PREFILTER =
    "#version 120\n"
    "varying vec2 uv;\n"
    "uniform sampler2D uTex;\n"
    "uniform vec2 uSrcTexel;\n"      /* the layer's texel */
    "uniform int uTaps;\n"
    "uniform vec2 uDstTexel;\n"
    "uniform float uThreshold, uKnee, uClamp;\n"
    "void main() {\n"
    /* Unity's prefilter is one bilinear tap of a full-resolution source at half
       resolution, i.e. a 2x2 box. The layer is supersampled, so the same footprint
       takes a grid of taps. */
    "    vec3 c = vec3(0.0);\n"
    "    float n = 0.0;\n"
    "    for (int y = 0; y < 8; y++) { if (y >= uTaps) break;\n"
    "      for (int x = 0; x < 8; x++) { if (x >= uTaps) break;\n"
    "        vec2 o = ((vec2(float(x), float(y)) + 0.5) / float(uTaps) - 0.5) * uDstTexel;\n"
    "        c += texture2D(uTex, uv + o).rgb; n += 1.0; } }\n"
    "    c /= max(n, 1.0);\n"
    "    c = min(vec3(uClamp), c);\n"
    "    float br = max(c.r, max(c.g, c.b));\n"
    "    float soft = clamp(br - uThreshold + uKnee, 0.0, 2.0 * uKnee);\n"
    "    soft = (soft * soft) / (4.0 * uKnee + 1e-4);\n"
    "    float m = max(br - uThreshold, soft) / max(br, 1e-4);\n"
    "    gl_FragColor = vec4(max(c * m, 0.0), 1.0);\n"
    "}\n";
static const char* UVFX_FS_BLURH =
    "#version 120\n"
    "varying vec2 uv;\n"
    "uniform sampler2D uTex;\n"
    "uniform vec2 uSrcTexel;\n"
    "void main() {\n"
    "    float t = uSrcTexel.x * 2.0;\n"
    "    vec3 c = texture2D(uTex, uv - vec2(t * 4.0, 0.0)).rgb * 0.01621622\n"
    "           + texture2D(uTex, uv - vec2(t * 3.0, 0.0)).rgb * 0.05405405\n"
    "           + texture2D(uTex, uv - vec2(t * 2.0, 0.0)).rgb * 0.12162162\n"
    "           + texture2D(uTex, uv - vec2(t * 1.0, 0.0)).rgb * 0.19459459\n"
    "           + texture2D(uTex, uv).rgb * 0.22702703\n"
    "           + texture2D(uTex, uv + vec2(t * 1.0, 0.0)).rgb * 0.19459459\n"
    "           + texture2D(uTex, uv + vec2(t * 2.0, 0.0)).rgb * 0.12162162\n"
    "           + texture2D(uTex, uv + vec2(t * 3.0, 0.0)).rgb * 0.05405405\n"
    "           + texture2D(uTex, uv + vec2(t * 4.0, 0.0)).rgb * 0.01621622;\n"
    "    gl_FragColor = vec4(c, 1.0);\n"
    "}\n";
static const char* UVFX_FS_BLURV =
    "#version 120\n"
    "varying vec2 uv;\n"
    "uniform sampler2D uTex;\n"
    "uniform vec2 uSrcTexel;\n"
    "void main() {\n"
    "    float t = uSrcTexel.y;\n"
    "    vec3 c = texture2D(uTex, uv - vec2(0.0, t * 3.23076923)).rgb * 0.07027027\n"
    "           + texture2D(uTex, uv - vec2(0.0, t * 1.38461538)).rgb * 0.31621622\n"
    "           + texture2D(uTex, uv).rgb * 0.22702703\n"
    "           + texture2D(uTex, uv + vec2(0.0, t * 1.38461538)).rgb * 0.31621622\n"
    "           + texture2D(uTex, uv + vec2(0.0, t * 3.23076923)).rgb * 0.07027027;\n"
    "    gl_FragColor = vec4(c, 1.0);\n"
    "}\n";
static const char* UVFX_FS_UPSAMPLE =
    "#version 120\n"
    "varying vec2 uv;\n"
    "uniform sampler2D uTex;\n"      /* high mip */
    "uniform sampler2D uLow;\n"
    "uniform float uScatter;\n"
    "void main() {\n"
    "    gl_FragColor = vec4(mix(texture2D(uTex, uv).rgb, texture2D(uLow, uv).rgb, uScatter), 1.0);\n"
    "}\n";

/* THE COMPOSITE. Unity's final pixel is ACES(background + effect + bloom). The game's
   background is already a finished, display-encoded picture, so it is taken as it is
   and the effect is laid over it in linear light: background * coverage + ACES(effect
   + bloom). Where the effect is opaque that is Unity's pixel exactly; where it is
   absent it is the game's pixel exactly, bit for bit (the early return). */
static const char* UVFX_FS_COMPOSITE =
    "#version 120\n"
    "varying vec2 uv;\n"
    "uniform sampler2D uTex;\n"      /* the game's finished view */
    "uniform sampler2D uLayer, uBloom, uDist;\n"
    "uniform vec2 uDstTexel;\n"
    "uniform int uTaps;\n"
    "uniform int uBloomOn;\n"
    "uniform int uBlackBg;\n"
    FX_OBJECT_GRADE_GLSL
    "vec3 dec(vec3 c) { c = clamp(c, 0.0, 1.0);\n"
    "    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c)); }\n"
    "vec3 enc(vec3 c) { c = clamp(c, 0.0, 1.0);\n"
    "    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c)); }\n"
    "vec3 mul3(mat3 m, vec3 v) { return m * v; }\n"
    /* URP ApplyTonemap(_TONEMAP_ACES): AcesTonemap(unity_to_ACES(x)), from Color.hlsl.
       GLSL's mat3 takes COLUMNS, so every matrix below is written transposed. */
    "vec3 aces(vec3 lin) {\n"
    "    const mat3 sRGB_2_AP0 = mat3(0.4397010, 0.0897923, 0.0175440, 0.3829780, 0.8134230, 0.1115440, 0.1773350, 0.0967616, 0.8707040);\n"
    "    const mat3 AP0_2_AP1 = mat3(1.4514393161, -0.0765537734, 0.0083161484, -0.2365107469, 1.1762296998, -0.0060324498, -0.2149285693, -0.0996759264, 0.9977163014);\n"
    "    const mat3 AP1_2_XYZ = mat3(0.6624541811, 0.2722287168, -0.0055746495, 0.1340042065, 0.6740817658, 0.0040607335, 0.1561876870, 0.0536895174, 1.0103391003);\n"
    "    const mat3 XYZ_2_AP1 = mat3(1.6410233797, -0.6636628587, 0.0117218943, -0.3248032942, 1.6153315917, -0.0082844420, -0.2364246952, 0.0167563477, 0.9883948585);\n"
    "    const mat3 D60_2_D65 = mat3(0.98722400, -0.00759836, 0.00307257, -0.00611327, 1.00186000, -0.00509595, 0.0159533, 0.0053302, 1.0816800);\n"
    "    const mat3 XYZ_2_709 = mat3(3.2409699419, -0.9692436363, 0.0556300797, -1.5373831776, 1.8759675015, -0.2039769589, -0.4986107603, 0.0415550574, 1.0569715142);\n"
    "    const vec3 AP1_RGB2Y = vec3(0.272229, 0.674082, 0.0536895);\n"
    "    vec3 a = sRGB_2_AP0 * lin;\n"
    "    float mi = min(a.r, min(a.g, a.b)), ma = max(a.r, max(a.g, a.b));\n"
    "    float sat = (max(ma, 1e-4) - max(mi, 1e-4)) / max(ma, 1e-2);\n"
    "    float k = max(a.b * (a.b - a.g) + a.g * (a.g - a.r) + a.r * (a.r - a.b), 0.0);\n"
    "    float yc = (a.b + a.g + a.r + 1.75 * sqrt(k)) / 3.0;\n"
    "    float x = (sat - 0.4) / 0.2;\n"
    "    float t = max(1.0 - abs(x / 2.0), 0.0);\n"
    "    float s = (1.0 + (x >= 0.0 ? 1.0 : -1.0) * (1.0 - t * t)) / 2.0;\n"
    "    float gain = 0.05 * s;\n"
    "    float glow = yc <= 2.0 / 3.0 * 0.08 ? gain : (yc >= 2.0 * 0.08 ? 0.0 : gain * (0.08 / yc - 0.5));\n"
    "    a *= 1.0 + glow;\n"
    "    float hue = 0.0;\n"
    "    if (!(a.r == a.g && a.g == a.b)) hue = degrees(atan(sqrt(3.0) * (a.g - a.b), 2.0 * a.r - a.g - a.b));\n"
    "    if (hue < 0.0) hue += 360.0;\n"
    "    float ch = hue;\n"
    "    if (ch < -180.0) ch += 360.0; else if (ch > 180.0) ch -= 360.0;\n"
    "    float hw = smoothstep(0.0, 1.0, 1.0 - abs(2.0 * ch / 135.0));\n"
    "    hw *= hw;\n"
    "    a.r += hw * sat * (0.03 - a.r) * (1.0 - 0.82);\n"
    "    vec3 cg = max(vec3(0.0), AP0_2_AP1 * a);\n"
    "    cg = mix(vec3(dot(cg, AP1_RGB2Y)), cg, 0.96);\n"
    "    vec3 p = (cg * (cg + 0.0245786) - 0.000090537) / (cg * (0.983729 * cg + 0.4329510) + 0.238081);\n"
    "    vec3 XYZ = AP1_2_XYZ * p;\n"
    "    float dv = max(XYZ.x + XYZ.y + XYZ.z, 1e-4);\n"
    "    vec3 xyY = vec3(XYZ.xy / dv, clamp(XYZ.y, 0.0, 65504.0));\n"
    "    xyY.z = pow(xyY.z, 0.9811);\n"
    "    float m = xyY.z / max(xyY.y, 1e-4);\n"
    "    XYZ = vec3(xyY.x * m, xyY.z, (1.0 - xyY.x - xyY.y) * m);\n"
    "    vec3 l = XYZ_2_AP1 * XYZ;\n"
    "    l = mix(vec3(dot(l, AP1_RGB2Y)), l, 0.93);\n"
    "    return clamp(XYZ_2_709 * (D60_2_D65 * (AP1_2_XYZ * l)), 0.0, 1.0);\n"
    "}\n"
    "void main() {\n"
    "    vec4 L = vec4(0.0); vec2 D = vec2(0.0); float n = 0.0;\n"
    "    for (int y = 0; y < 4; y++) { if (y >= uTaps) break;\n"
    "      for (int x = 0; x < 4; x++) { if (x >= uTaps) break;\n"
    "        vec2 o = ((vec2(float(x), float(y)) + 0.5) / float(uTaps) - 0.5) * uDstTexel;\n"
    "        L += texture2D(uLayer, uv + o); D += texture2D(uDist, uv + o).rg; n += 1.0; } }\n"
    "    L /= n; D /= n;\n"
    "    vec3 bl = uBloomOn == 1 ? texture2D(uBloom, uv).rgb : vec3(0.0);\n"
    "    vec3 bg = uBlackBg == 1 ? vec3(0.0) : texture2D(uTex, uv).rgb;\n"
    "    if (L.a >= 1.0 && max(L.r, max(L.g, L.b)) <= 0.0 && max(bl.r, max(bl.g, bl.b)) <= 1e-5 && D.g <= 0.0) {\n"
    "        gl_FragColor = vec4(bg, 1.0); return; }\n"
    "    vec3 lin = dec(bg);\n"
    "    if (D.g > 0.0) {\n"
    "        vec3 off = uBlackBg == 1 ? vec3(0.0) : dec(texture2D(uTex, uv + vec2(D.r / D.g)).rgb);\n"
    "        lin = mix(lin, off, clamp(D.g, 0.0, 1.0));\n"
    "    }\n"
    "    vec3 effect = aces(max(L.rgb, 0.0) + bl);\n"
    /* Grade only the effect's complete radiance signal, including additive glow.
       Its independent transmittance stays untouched, with no coverage threshold. */
    "    if (uObjectGrade1.w > 0.5) effect=objectDecode(objectGrade(objectEncode(effect)));\n"
    "    vec3 o = lin * clamp(L.a, 0.0, 1.0) + effect;\n"
    "    gl_FragColor = vec4(enc(o), 1.0);\n"
    "}\n";

static GLuint g_uvPPre, g_uvPBlurH, g_uvPBlurV, g_uvPUp, g_uvPComp;
static FxRT   g_uvLayer, g_uvDist, g_uvOut;
static FxRT   g_uvDown[8], g_uvUp[8];

static bool uvfx_programs(void)
{
    if (g_uvProgTried) return g_uvProg[UVK_PYRO] != 0 && g_uvPComp != 0 && uv_glBlendFuncSeparate;
    g_uvProgTried = 1;
    uv_glBlendFuncSeparate = (UVPFNBLENDFUNCSEPARATE)fx_proc("glBlendFuncSeparate",
                                                             "glBlendFuncSeparateEXT", FX_OPT);
    g_uvProg[UVK_PYRO]         = uvfx_link(UVFX_FS_PYRO, NULL, "uvfx pyro", UVFX_FS_FLIPBOOK);
    g_uvProg[UVK_PYRONEW]      = uvfx_link(UVFX_FS_PYRONEW, UVFX_FS_LIT, "uvfx pyro new", UVFX_FS_FLIPBOOK);
    g_uvProg[UVK_PYROFIRE]     = uvfx_link(UVFX_FS_PYROFIRE, NULL, "uvfx pyro fire", UVFX_FS_FLIPBOOK);
    g_uvProg[UVK_PYRONEWU]     = uvfx_link(UVFX_FS_PYRONEWU, NULL, "uvfx pyro new unlit", UVFX_FS_FLIPBOOK);
    g_uvProg[UVK_DIRTDEBRIS]   = uvfx_link(UVFX_FS_DIRTDEBRIS, UVFX_FS_LIT, "uvfx dirt debris");
    g_uvProg[UVK_FLASH]        = uvfx_link(UVFX_FS_FLASH, NULL, "uvfx flash");
    g_uvProg[UVK_WICKS]        = uvfx_link(UVFX_FS_WICKS, NULL, "uvfx wicks");
    g_uvProg[UVK_DECAL]        = uvfx_link(UVFX_FS_DECAL, NULL, "uvfx decal");
    g_uvProg[UVK_SPRITEDEBRIS] = uvfx_link(UVFX_FS_SPRITEDEBRIS, UVFX_FS_LIT, "uvfx sprite debris");
    g_uvProg[UVK_CONCRETE]     = uvfx_link(UVFX_FS_CONCRETE, UVFX_FS_LIT, "uvfx concrete");
    g_uvProg[UVK_DISTORT]      = uvfx_link(UVFX_FS_DISTORT, NULL, "uvfx distortion");
    g_uvProg[UVK_FUMES]        = uvfx_link(UVFX_FS_FUMES, NULL, "uvfx gun fumes");
    g_uvProg[UVK_SPARK]        = uvfx_link(UVFX_FS_SPARK, NULL, "uvfx gun spark");
    g_uvProg[UVK_EMBER]        = uvfx_link(UVFX_FS_EMBER, NULL, "uvfx gun ember");
    g_uvProg[UVK_SMOKEM]       = uvfx_link(UVFX_FS_SMOKEM, UVFX_FS_LIT, "uvfx gun smoke");
    g_uvProg[UVK_SMOKETRAIL]   = uvfx_link(UVFX_FS_SMOKETRAIL, UVFX_FS_LIT, "uvfx gun smoke trail");
    g_uvPPre   = fx_program(UVFX_FS_PREFILTER, "uvfx bloom prefilter");
    g_uvPBlurH = fx_program(UVFX_FS_BLURH, "uvfx bloom blur h");
    g_uvPBlurV = fx_program(UVFX_FS_BLURV, "uvfx bloom blur v");
    g_uvPUp    = fx_program(UVFX_FS_UPSAMPLE, "uvfx bloom upsample");
    g_uvPComp  = fx_program(UVFX_FS_COMPOSITE, "uvfx composite");
    bool ok = uv_glBlendFuncSeparate != NULL && g_uvPPre && g_uvPBlurH && g_uvPBlurV && g_uvPUp && g_uvPComp;
    for (int k = 0; k < UVK_COUNT; k++) ok = ok && g_uvProg[k] != 0;
    fprintf(stderr, "unityvfx: programs %s%s\n", ok ? "ready" : "NOT ready -- explosions stay the cartridge's",
            uv_glBlendFuncSeparate ? "" : " (no glBlendFuncSeparate)");
    if (!ok) g_uvProg[UVK_PYRO] = 0;
    return ok;
}

/* ------------------------------------------------------------------ one particle */
struct UvfxP {                                     /* one particle at the draw's instant */
    float r[UVR_N];
};

/* The recorded state at time t (seconds since the explosion): the two recorded frames
   either side, matched particle by particle on Unity's own per-particle seed and
   blended. A particle in the earlier frame and not the later has died between them and
   is kept at its last state; one only in the later frame has not been born yet. */
static void uvfx_state(const UvfxPack& pk, const UvfxSeed& sd, int sys, float t, std::vector<UvfxP>& out)
{
    out.clear();
    /* a hair past the frame: an age written as a rounded decimal (0.0833333 for 5/60)
       must not fall back to the frame before, whose flipbook tile is a different picture */
    float ff = t * (float)pk.fps + 1e-3f;
    if (ff < 0) return;
    int f0 = (int)ff;
    if (f0 >= pk.frames - 1) return;
    const float a = ff - (float)f0;
    const int n = pk.nsys;
    const float* A = &sd.recs[0] + (size_t)sd.off[f0 * n + sys] * UVR_N;
    const float* B = &sd.recs[0] + (size_t)sd.off[(f0 + 1) * n + sys] * UVR_N;
    const unsigned na = sd.cnt[f0 * n + sys], nb = sd.cnt[(f0 + 1) * n + sys];
    unsigned hint = 0;
    for (unsigned i = 0; i < na; i++) {
        const float* pa = A + (size_t)i * UVR_N;
        const float* pb = NULL;
        for (unsigned k = 0; k < nb; k++) {
            const unsigned j = (hint + k) % nb;
            if (!memcmp(&B[(size_t)j * UVR_N], pa, 4)) { pb = B + (size_t)j * UVR_N; hint = j + 1; break; }
        }
        UvfxP p;
        memcpy(p.r, pa, sizeof p.r);
        if (pb) {
            for (int k = 1; k < UVR_N; k++) {
                if (k == UVR_TILE || k == UVR_TILE + 1 || k == UVR_MESH) continue;
                float d = pb[k] - pa[k];
                if (k >= UVR_ROT && k < UVR_ROT + 3) {          /* shortest way round */
                    while (d > 180.0f) d -= 360.0f;
                    while (d < -180.0f) d += 360.0f;
                }
                p.r[k] = pa[k] + d * a;
            }
        }
        out.push_back(p);
    }
}

/* ------------------------------------------------------------------ drawing */
struct UvfxDrawItem { int inst, sys; float key; float c[3]; int trail, ribbon, line; };
/* One particle light at the draw's instant: place and range in cells, linear colour times
   intensity, and the cells per metre of the effect it belongs to. */
struct UvfxLight { float p[3], range, col[3], cpm; };
static std::vector<UvfxLight> g_uvLights;
static int g_uvLightsLast = 0;

static void uvfx_set_common(GLuint p, const UvfxFrame& F, float t)
{
    fx_setmat(p, "uVP", g_uvVP);
    fx_setmat(p, "uMV", g_uvMV);
    fx_setmat(p, "uInvP", g_uvInvP);
    fx_set2f(p, "uLayer", (float)g_uvLayer.w, (float)g_uvLayer.h);
    fx_set1f(p, "uMPC", 1.0f / F.S);
    fx_set1f(p, "uTime", t);
    /* THE SMOKE'S SUN. By default the pack's demo sun, placed where it stands relative
       to the CAMERA in the scene the effect was authored and judged in: Euler(135,0,0)
       travels down at 45 degrees TOWARDS the viewer, so the camera sees the smoke's
       shaded side and the smoke reads dark. The game's own sun lights it from the front
       and turns it pale, which is the other row of the panel (uvfx_gamesun). */
    float sun[3];
    if (g_fx.uvfx_gamesun && !g_uvStage.on) {
        sun[0] = g_fxSunDir[0]; sun[1] = g_fxSunDir[1]; sun[2] = g_fxSunDir[2];
    } else {
        float fh[3] = { g_uvF[0], 0.0f, g_uvF[2] };
        const float l = sqrtf(fh[0] * fh[0] + fh[2] * fh[2]);
        if (l > 1e-6f) { fh[0] /= l; fh[2] /= l; } else { fh[0] = 0; fh[2] = -1; }
        sun[0] = -0.70710678f * fh[0]; sun[1] = -0.70710678f; sun[2] = -0.70710678f * fh[2];
    }
    fx_set3f(p, "uToSun", -sun[0], -sun[1], -sun[2]);
    fx_set1f(p, "uNoDepth", g_uvStage.on ? 1.0f : 0.0f);
    /* The pack's demo sun is white at 3.14. */
    const float sc = 3.14f * g_fx.uvfx_sun;
    fx_set3f(p, "uSunCol", sc, sc, sc);
    fx_set3f(p, "uCam", g_uvCam[0], g_uvCam[1], g_uvCam[2]);
    fx_set1i(p, "uDepth", 0);
}

static void uvfx_bind_tex(GLuint p, const UvfxMat& m, const char* slot, const char* uni, int unit)
{
    std::map<std::string, int>::const_iterator it = m.tex.find(slot);
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, it == m.tex.end() ? 0 : g_uvTex[it->second].gl);
    fx_set1i(p, uni, unit);
}

static void uvfx_set_material(GLuint p, const UvfxMat& m)
{
    float v[4];
    switch (m.kind) {
    case UVK_PYRO:
    case UVK_PYRONEW:
    case UVK_PYRONEWU:
    case UVK_PYROFIRE: {
        uvfx_bind_tex(p, m, "_Flipbook", "uFlip", 1);
        uvfx_bind_tex(p, m, "_MotionVector", "uMVTex", 2);
        uvfx_bind_tex(p, m, "_EmissiveLUT", "uLUT", 3);
        fx_set1f(p, "uCols", m.f("_FlipbookColumns", 8));
        fx_set1f(p, "uRows", m.f("_FlipbookRows", 8));
        fx_set1f(p, "uSpeed", m.f("_FlipbookSpeed", 64));
        fx_set1f(p, "uStart", m.f("_FlipbookStartFrame", 0));
        fx_set1f(p, "uMVInf", m.f("_MVInfluence", 0.003f));
        fx_set1f(p, "uMVOff", m.f("_MVOffsetFrame", 1));
        fx_set1f(p, "uDFEroSS", m.f("_DFEroSS", 0));
        fx_set1f(p, "uDFEroSSS", m.f("_DFEroSSS", 1));
        fx_set1f(p, "uDFLerp", m.f("_DepthFadeLerp", 1));
        fx_set1f(p, "uEmMult", m.f("_EmissiveMult", 1));
        fx_set1f(p, "uLUTRange", m.f("_EmissiveLUTRange", 1));
        fx_set1f(p, "uLUTOff", m.f("_EmissiveLUTOffset", 0));
        fx_set1f(p, "uEEroS", m.f("_EmissiveErosionSmoothness", 1));
        fx_set1f(p, "uEroS", m.f("_ErosionSmoothness", 1));
        fx_set1f(p, "uCamLen", m.f("_CamDepthFadeLength", 1));
        fx_set1f(p, "uCamOff", m.f("_CamDepthFadeOffset", 0));
        fx_set1f(p, "uNearM", 0.3f);                    /* Unity's default near plane */
        m.v4("_SmokeTint", v, 1); fx_set3f(p, "uSmokeTint", v[0], v[1], v[2]);
        m.v4("_EmissiveTint", v, 1); fx_set3f(p, "uEmTint", v[0], v[1], v[2]);
        m.v4("_FakeSubsurfaceScatteringSmokeBiasColor", v, 0);
        const float s = m.f("_FakeSubsurfaceScatteringSmokeBiasScale", 0);
        fx_set3f(p, "uSSS", v[0] * s, v[1] * s, v[2] * s);
        fx_set1f(p, "uSpec", m.f("_Specular", 0));
        fx_set1f(p, "uSmooth", m.f("_Smoothness", 0));
        fx_set1f(p, "uBaseEm", m.f("_BaseEmission", 1));
    } break;
    case UVK_DIRTDEBRIS:
        uvfx_bind_tex(p, m, "_Texture", "uTex", 1);
        m.v4("_Texture_ST", v, 0); if (v[0] == 0 && v[1] == 0) { v[0] = v[1] = 1; }
        fx_set4fv(p, "uST", 1, v);
        fx_set1f(p, "uSpec", m.f("_Specular", 0));
        fx_set1f(p, "uSmooth", m.f("_Smoothness", 0));
        fx_set1f(p, "uES", m.f("_EroSmoothness", 1));
        break;
    case UVK_FLASH:
        uvfx_bind_tex(p, m, "_Texture", "uTex", 1);
        m.v4("_Texture_ST", v, 0); if (v[0] == 0 && v[1] == 0) { v[0] = v[1] = 1; }
        fx_set4fv(p, "uST", 1, v);
        fx_set1f(p, "uDF", m.f("_DepthFade", 1));
        fx_set1f(p, "uIsAdd", m.f("_IsAdditive", 1));
        fx_set1f(p, "uEmission", m.f("_Emission", 1));
        break;
    case UVK_WICKS:
        uvfx_bind_tex(p, m, "_Texture", "uTex", 1);
        uvfx_bind_tex(p, m, "_Noise", "uNoise", 2);
        m.v4("_Texture_ST", v, 0); if (v[0] == 0 && v[1] == 0) { v[0] = v[1] = 1; }
        fx_set4fv(p, "uST", 1, v);
        m.v4("_NoiseUVP", v, 0); fx_set4fv(p, "uNoiseUVP", 1, v);
        fx_set1f(p, "uES", m.f("_ErosionSmoothness", 1));
        fx_set1f(p, "uNoiseUVS", m.f("_NoiseUVS", 1));
        fx_set1f(p, "uEmission", m.f("_Emission", 1));
        break;
    case UVK_DECAL:
        uvfx_bind_tex(p, m, "_Decal", "uTex", 1);
        m.v4("_Decal_ST", v, 0); if (v[0] == 0 && v[1] == 0) { v[0] = v[1] = 1; }
        fx_set4fv(p, "uST", 1, v);
        fx_set1f(p, "uES", m.f("_ErosionSmoothness", 1));
        fx_set1f(p, "uDF", m.f("_DepthFade", 1));
        fx_set1f(p, "uIsAdd", m.f("_IsAdditive", 0));
        fx_set1f(p, "uEmission", m.f("_Emission", 1));
        break;
    case UVK_SPRITEDEBRIS:
    case UVK_CONCRETE:
        uvfx_bind_tex(p, m, "_Texture", "uTex", 1);
        m.v4("_Texture_ST", v, 0); if (v[0] == 0 && v[1] == 0) { v[0] = v[1] = 1; }
        fx_set4fv(p, "uST", 1, v);
        fx_set1f(p, "uSpec", m.f("_Specular", 0));
        fx_set1f(p, "uSmooth", m.f("_Smoothness", 0));
        break;
    case UVK_DISTORT:
        uvfx_bind_tex(p, m, "_CookieTexture", "uCookie", 1);
        uvfx_bind_tex(p, m, "_DistortionNoise", "uNoise", 2);
        m.v4("_CookieTextureChannel", v, 0); fx_set4fv(p, "uChan", 1, v);
        m.v4("_DistortionNoiseUVPan", v, 0); fx_set4fv(p, "uNoiseUVPan", 1, v);
        m.v4("_DistortionNoiseUVScale", v, 1); fx_set4fv(p, "uNoiseUVScale", 1, v);
        fx_set1f(p, "uNoiseMul", m.f("_DistortionNoiseMultiply", 0));
        fx_set1f(p, "uCorePow", m.f("_CorePower", 1));
        fx_set1f(p, "uCoreMul", m.f("_CoreMultiply", 1));
        fx_set1f(p, "uOverall", m.f("_OverallIntensity", 0.03f));
        fx_set1f(p, "uDF", m.f("_DepthFade", 1));
        fx_set1f(p, "uOpacity", m.f("_Opacity", 1));
        fx_set1f(p, "uCamLen", m.f("_CamDepthFadeLength", 1));
        fx_set1f(p, "uCamOff", m.f("_CamDepthFadeOffset", 0));
        fx_set1f(p, "uNearM", 0.3f);
        break;
    case UVK_FUMES:
        uvfx_bind_tex(p, m, "_Texture", "uTex", 1);
        m.v4("_EmissiveColor", v, 1); fx_set4fv(p, "uEmCol", 1, v);
        fx_set1f(p, "uColPow", m.f("_Color_Pow", 1));
        fx_set1f(p, "uAlphaPow", m.f("_Alpha_Pow", 1));
        fx_set1f(p, "uEmInt", m.f("_EmissiveIntensity", 5));
        break;
    case UVK_SPARK:
        uvfx_bind_tex(p, m, "_Texture", "uTex", 1);
        fx_set1f(p, "uPow", m.f("_Pow", 1));
        fx_set1f(p, "uBoost", m.f("_EmissiveBoost", 1));
        break;
    case UVK_EMBER:
        fx_set1f(p, "uBoost", m.f("_ColorBoost", 1));
        break;
    case UVK_SMOKEM:
        uvfx_bind_tex(p, m, "_Texture", "uTex", 1);
        m.v4("_Color1", v, 1); fx_set4fv(p, "uCol1", 1, v);
        m.v4("_Color2", v, 1); fx_set4fv(p, "uCol2", 1, v);
        fx_set1f(p, "uColPow", m.f("_Color_Pow", 1));
        fx_set1f(p, "uAlphaPow", m.f("_Alpha_Pow", 1));
        fx_set1f(p, "uSoften", m.f("_SoftenShadows", 1));
        fx_set1f(p, "uFade", m.f("_FadeDistance", 0.3f));
        fx_set1f(p, "uRGBMask", m.f("_USERGBMASK", 0));
        break;
    case UVK_SMOKETRAIL:
        uvfx_bind_tex(p, m, "_Texture", "uTex", 1);
        fx_set1f(p, "uUT", m.f("_UTiling", 1));
        fx_set1f(p, "uVT", m.f("_VTiling", 1));
        fx_set1f(p, "uSX", m.f("_ScaleX", 1));
        fx_set1f(p, "uSY", m.f("_ScaleY", 1));
        fx_set1f(p, "uSpeed", m.f("_speed", 0));
        fx_set1f(p, "uSoften", m.f("_SoftenShadows", 1));
        break;
    default: break;
    }
    glActiveTexture(GL_TEXTURE0);
}

/* BLEND AND CULL AS THE MATERIAL SAYS, from its own _Src/_Dst (or _Src1/_Dst1) and _Cull,
   the numbers its Forward pass reads -- not assumed per shader, so a material that blends
   differently from its siblings draws the way it does in Unity. Unity's BlendMode numbers
   to GL. The layer's ALPHA is the share of the background that survives, so it takes
   (ZERO, whatever the colour's destination factor does to what is behind). */
static GLenum uvfx_gl_blend(int b)
{
    switch (b) {
    case 0: return GL_ZERO;              case 1: return GL_ONE;
    case 2: return GL_DST_COLOR;         case 3: return GL_SRC_COLOR;
    case 4: return GL_ONE_MINUS_DST_COLOR; case 5: return GL_SRC_ALPHA;
    case 6: return GL_ONE_MINUS_SRC_COLOR; case 7: return GL_DST_ALPHA;
    case 8: return GL_ONE_MINUS_DST_ALPHA; case 9: return GL_SRC_ALPHA_SATURATE;
    default: return GL_ONE_MINUS_SRC_ALPHA;
    }
}
static void uvfx_blend_of(const UvfxMat& m, int* src, int* dst)
{
    const bool one = m.p.count("_Src1") > 0;
    const bool lit = m.kind == UVK_CONCRETE;          /* declares no blend: opaque */
    *src = (int)m.f(one ? "_Src1" : "_Src", lit ? 1.0f : 5.0f);
    *dst = (int)m.f(one ? "_Dst1" : "_Dst", lit ? 0.0f : 10.0f);
}
static bool uvfx_opaque(const UvfxMat& m)
{
    int a, b;
    uvfx_blend_of(m, &a, &b);
    return b == 0;
}
static void uvfx_state_for(const UvfxMat& m)
{
    int src, dst;
    uvfx_blend_of(m, &src, &dst);
    const GLenum adst = dst == 10 ? GL_ONE_MINUS_SRC_ALPHA : dst == 0 ? GL_ZERO : GL_ONE;
    if (m.kind == UVK_DISTORT)             /* offset in red, coverage in green */
        uv_glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    else
        uv_glBlendFuncSeparate(uvfx_gl_blend(src), uvfx_gl_blend(dst), GL_ZERO, adst);
    const int cull = (int)m.f(m.p.count("_Cull1") ? "_Cull1" : "_Cull", 2);
    /* Unity: 0 off, 1 front, 2 back */
    if (cull == 0) glDisable(GL_CULL_FACE);
    else { glEnable(GL_CULL_FACE); glCullFace(cull == 1 ? GL_FRONT : GL_BACK); }
}

static void uvfx_quad(const float* c, const float* uv0, float tu, float tv, float tw, float th)
{
    glBegin(GL_QUADS);
    for (int k = 0; k < 4; k++) {
        glTexCoord2f(tu + uv0[k * 2] * tw, tv + uv0[k * 2 + 1] * th);
        glVertex3f(c[k * 3], c[k * 3 + 1], c[k * 3 + 2]);
    }
    glEnd();
}

static void uvfx_draw_system(const UvfxInst& in, const UvfxSys& S, const UvfxFrame& F,
                             float t, const std::vector<UvfxP>& P)
{
    const UvfxPack& pk = g_uvPacks[in.pack];
    const UvfxMat& m = pk.mat[S.mat];
    const GLuint p = g_uvProg[m.kind];
    static const float QUV[8] = { 0, 1, 1, 1, 1, 0, 0, 0 };
    static const float Q[8] = { -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f, -0.5f, -0.5f };
    static const float I16[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    const float tw = 1.0f / (float)S.tilesX, th = 1.0f / (float)S.tilesY;
    fx_set1f(p, "uUseVC", 0.0f);
    for (size_t i = 0; i < P.size(); i++) {
        const float* r = P[i].r;
        float col[4];
        /* Unity hands the particle colour to the shader linearised, in eight bits */
        for (int k = 0; k < 3; k++) col[k] = floorf(uvfx_srgb2lin(r[UVR_COL + k]) * 255.0f + 0.5f) / 255.0f;
        col[3] = r[UVR_COL + 3];
        fx_set4fv(p, "uColorU", 1, col);
        fx_set4fv(p, "uC1", 1, r + UVR_C1);
        fx_set4fv(p, "uC2", 1, r + UVR_C2);
        float pos[3];
        uvfx_point(F, r + UVR_POS, pos);
        const float ks = (S.mode == UVM_MESH && !S.viewAligned) ? 1.0f : S.scale;
        const float sz[3] = { r[UVR_SIZE] * ks, r[UVR_SIZE + 1] * ks, r[UVR_SIZE + 2] * ks };
        const float tu = r[UVR_TILE] >= 0 ? r[UVR_TILE] : 0.0f, tv = r[UVR_TILE + 1] >= 0 ? r[UVR_TILE + 1] : 0.0f;
        const float tsw = r[UVR_TILE] >= 0 ? tw : 1.0f, tsh = r[UVR_TILE] >= 0 ? th : 1.0f;

        if (S.mode == UVM_MESH) {
            const int mi = S.meshes.empty() ? -1
                         : S.meshes[(size_t)r[UVR_MESH] < S.meshes.size() ? (size_t)r[UVR_MESH] : 0];
            if (mi < 0) continue;
            const UvfxMesh& mesh = pk.mesh[mi];
            float E[9], B[9], L[9], Nm[9];
            uvfx_euler(r[UVR_ROT], r[UVR_ROT + 1], r[UVR_ROT + 2], E);
            if (S.viewAligned) {
                for (int k = 0; k < 3; k++) { B[k * 3] = g_uvR[k]; B[k * 3 + 1] = g_uvU[k]; B[k * 3 + 2] = g_uvF[k]; }
            } else {
                memcpy(B, F.A, sizeof B);              /* the effect's own axes, M included */
            }
            uvfx_m3mul(B, E, L);
            float model[16];
            for (int c = 0; c < 3; c++) {
                for (int rr = 0; rr < 3; rr++) {
                    model[c * 4 + rr] = L[rr * 3 + c] * sz[c] * F.S;
                    Nm[c * 3 + rr] = L[rr * 3 + c] / (sz[c] != 0 ? sz[c] : 1e-6f);
                }
                model[c * 4 + 3] = 0;
            }
            model[12] = pos[0]; model[13] = pos[1]; model[14] = pos[2]; model[15] = 1;
            fx_setmat(p, "uModel", model);
            GLint ln = fx_glGetUniformLocation(p, "uNrm");
            if (ln >= 0) {
                typedef void (APIENTRY *PM3)(GLint, GLsizei, GLboolean, const GLfloat*);
                static PM3 um3 = (PM3)fx_proc("glUniformMatrix3fv", "glUniformMatrix3fvARB", FX_OPT);
                if (um3) um3(ln, 1, GL_FALSE, Nm);
            }
            fx_set1f(p, "uIsMesh", 1.0f);
            glEnableClientState(GL_VERTEX_ARRAY);
            glEnableClientState(GL_NORMAL_ARRAY);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glVertexPointer(3, GL_FLOAT, 64, &mesh.v[0]);
            glNormalPointer(GL_FLOAT, 64, &mesh.v[3]);
            glTexCoordPointer(2, GL_FLOAT, 64, &mesh.v[10]);
            glDrawElements(GL_TRIANGLES, (GLsizei)mesh.idx.size(), GL_UNSIGNED_INT, &mesh.idx[0]);
            glDisableClientState(GL_VERTEX_ARRAY);
            glDisableClientState(GL_NORMAL_ARRAY);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            continue;
        }

        fx_setmat(p, "uModel", I16);
        fx_set1f(p, "uIsMesh", 0.0f);
        float c[12], n[3];
        if (S.mode == UVM_BILLBOARD) {
            float E[9];
            uvfx_euler(r[UVR_ROT], r[UVR_ROT + 1], -r[UVR_ROT + 2], E);
            for (int k = 0; k < 4; k++) {
                const float lx = Q[k * 2] * sz[0], ly = Q[k * 2 + 1] * sz[1];
                const float ex = E[0] * lx + E[1] * ly, ey = E[3] * lx + E[4] * ly, ez = E[6] * lx + E[7] * ly;
                for (int a = 0; a < 3; a++)
                    c[k * 3 + a] = pos[a] + (g_uvR[a] * ex + g_uvU[a] * ey + g_uvF[a] * ez) * F.S;
            }
            n[0] = -g_uvF[0]; n[1] = -g_uvF[1]; n[2] = -g_uvF[2];
        } else if (S.mode == UVM_HORIZONTAL) {
            const float rz = -r[UVR_ROT + 2] * 3.14159265f / 180.0f;
            const float cr = cosf(rz), sr = sinf(rz), k2 = 0.70710678f;
            for (int k = 0; k < 4; k++) {
                /* rotated, THEN sized per axis, as Unity's own vertices came out */
                const float qx = Q[k * 2], qy = Q[k * 2 + 1];
                const float ox = (cr * qx - sr * qy) * sz[0] * k2, oy = (sr * qx + cr * qy) * sz[1] * k2;
                float d[3];
                uvfx_dir(F, ox, 0.0f, oy, d);
                for (int a = 0; a < 3; a++) c[k * 3 + a] = pos[a] + d[a] * F.S;
                /* A LIFT OFF THE GROUND. Unity lays these 1 cm above a flat floor; the
                   game's ground is not flat and its depth is a fixed-function draw, so
                   the ring would tear where the terrain rises by a hair. */
                c[k * 3 + 1] += 0.02f;
            }
            n[0] = 0; n[1] = 1; n[2] = 0;
        } else {                                         /* UVM_STRETCH */
            float v[3];
            uvfx_dir(F, r[UVR_VEL], r[UVR_VEL + 1], r[UVR_VEL + 2], v);
            const float sp = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            const float inv = sp > 1e-6f ? 1.0f / sp : 0.0f;
            const float d[3] = { v[0] * inv, v[1] * inv, v[2] * inv };
            float w[3] = { -(d[1] * g_uvF[2] - d[2] * g_uvF[1]), -(d[2] * g_uvF[0] - d[0] * g_uvF[2]),
                           -(d[0] * g_uvF[1] - d[1] * g_uvF[0]) };
            const float wl = sqrtf(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
            if (wl < 1e-9f) continue;
            for (int a = 0; a < 3; a++) w[a] /= wl;
            const float len = (sz[1] * S.lengthScale + sp * S.velocityScale) * F.S;
            const float hw = sz[0] * 0.5f * F.S;
            for (int a = 0; a < 3; a++) {
                const float head = pos[a], tail = pos[a] - d[a] * len;
                c[0 + a] = head + w[a] * hw;
                c[3 + a] = tail + w[a] * hw;
                c[6 + a] = tail - w[a] * hw;
                c[9 + a] = head - w[a] * hw;
            }
            n[0] = -g_uvF[0]; n[1] = -g_uvF[1]; n[2] = -g_uvF[2];
        }
        fx_set3f(p, "uQuadN", n[0], n[1], n[2]);
        uvfx_quad(c, QUV, tu, tv, tsw, tsh);
    }
    (void)in; (void)t;
}


/* ================================================================== GUNS
   MUZZLE FLASHES, played where the engine says a shot left, at the gun that fired it.

   THE SHOT is the engine's own record (EFX|FIRE, one per projectile TechnoClass::Fire_At
   launches): who fired, which weapon, and which barrel of a two-shooter. So a flash
   lands on the tick the round leaves, on small arms whose rounds are never seen in
   flight as much as on a cannon.

   THE GUN is a SOCKET on the firer's authored model (vfx3d/sockets.txt, measured off the
   models by tools/art/unity_vfx/sockets.py): the tip of the rifle in a soldier's hands,
   carried by his weapon bone through whatever clip he is playing, or a barrel's muzzle
   ring on a tank's turret, turned with the turret. Each drawn frame, every socketed
   object that is drawn leaves its sockets here in world space; a shot then starts its
   flash at the socket, pointing out of the barrel, and the flash RIDES the socket for
   as long as it lives (every system in the gun pack simulates in its own local space,
   so in Unity too it moves with the gun).

   WHICH FLASH is data (vfx3d/muzzles.txt): "TYPE WEAPON SET PACK SIZE [BURST GAP]", the
   engine type, 0 primary or 1 secondary, the socket set, the Unity prefab and a size on
   top of the metres-to-cells scale (the gun pack is authored at real size, a rifle's flash
   a few centimetres across). Several lines for one weapon all play. BURST flashes play
   per shot, GAP engine ticks apart (default one): the engine fires the rifleman's gun
   once, but his 3D shoot clip kicks the rifle three times, 3 ticks apart (measured on
   the muzzle socket), and one flash against three kicks reads as a misfire. A set of two sockets is
   picked by the engine's barrel (0 the one a first shot leaves, 1 a second); a set of
   four is two per side, taken in turn.

   An object with no socket this frame (off screen, or drawn as a sprite) gets no flash:
   there is no gun to put it on. */
struct UvfxSockDef {
    std::string model, set;
    int index;
    int bone;                          /* -1 when the socket rides a part            */
    std::string boneName, partName;
    float p[3], f[3], u[3];
};
static std::vector<UvfxSockDef> g_uvSockDefs;
struct UvfxSockXf { float p[3], f[3], u[3], k; };
struct UvfxSockRec {
    int serial;                        /* the frame these were placed on */
    std::vector<int> def;              /* g_uvSockDefs index of each     */
    std::vector<UvfxSockXf> xf;        /* world space                    */
};
static std::map<int, UvfxSockRec> g_uvSock;       /* key: RTTI kind * 100000 + heap id */
static int g_uvSerial = 1;
struct UvfxMuzzleRule { std::string type, set; int which, pack; float size; int burst; float gap; };
/* the rest of a burst, waiting for its tick: the socket is looked up again then, so each
   flash starts where the barrel is when it plays */
struct UvfxBurstQ { EfxFire f; int key, slot, rule, k; float due; };
static std::vector<UvfxBurstQ> g_uvBursts;
static std::vector<UvfxMuzzleRule> g_uvMuzzles;
struct UvfxFireQ { EfxFire f; int frame; };
static std::vector<UvfxFireQ> g_uvFires;
static std::map<int, int> g_uvTurn;               /* key * 2 + weapon -> shots taken */
static int g_uvMuzzleSpawned = 0, g_uvFiresNoSocket = 0;

static int uvfx_rtti_of(ObjKind k)
{
    switch (k) {
    case K_INFANTRY: return 1;
    case K_UNIT:     return 3;
    case K_AIRCRAFT: return 5;
    case K_BUILDING: return 7;
    default:         return 0;
    }
}

static void uvfx_load_guns(const std::string& root)
{
    std::vector<unsigned char> txt;
    if (uvfx_read_file(root + "sockets.txt", txt)) {
        txt.push_back(0);
        char* cur = (char*)&txt[0];
        for (char* line = uvfx_tok(&cur, '\n'); line; line = uvfx_tok(&cur, '\n')) {
            if (line[0] == '#' || !line[0]) continue;
            char model[32], set[32], space[64];
            UvfxSockDef d;
            if (sscanf(line, "%31s %31s %d %63s %f %f %f %f %f %f %f %f %f", model, set, &d.index,
                       space, &d.p[0], &d.p[1], &d.p[2], &d.f[0], &d.f[1], &d.f[2],
                       &d.u[0], &d.u[1], &d.u[2]) != 13) continue;
            d.model = model; d.set = set; d.bone = -1;
            if (!strncmp(space, "bone:", 5)) d.boneName = space + 5;
            else if (!strncmp(space, "part:", 5)) d.partName = space + 5;
            else continue;
            g_uvSockDefs.push_back(d);
        }
    }
    txt.clear();
    if (uvfx_read_file(root + "muzzles.txt", txt)) {
        txt.push_back(0);
        char* cur = (char*)&txt[0];
        for (char* line = uvfx_tok(&cur, '\n'); line; line = uvfx_tok(&cur, '\n')) {
            if (line[0] == '#' || !line[0]) continue;
            char type[32], set[32], pack[128];
            UvfxMuzzleRule r;
            r.burst = 1; r.gap = 0.0f;
            if (sscanf(line, "%31s %d %31s %127s %f %d %f", type, &r.which, set, pack, &r.size, &r.burst, &r.gap) < 5)
                continue;
            if (r.burst < 1) r.burst = 1;
            r.pack = uvfx_pack_get(pack);
            if (r.pack < 0) {
                fprintf(stderr, "unityvfx: muzzle flash %s not loaded; %s fires without one\n", pack, type);
                continue;
            }
            r.type = type; r.set = set;
            g_uvMuzzles.push_back(r);
        }
    }
    for (size_t i = 0; i < g_uvMuzzles.size(); i++)
        printf("UVFX|muzzlerule|type=%s|which=%d|set=%s|pack=%s|size=%.2f|burst=%d|gap=%.2f\n",
               g_uvMuzzles[i].type.c_str(), g_uvMuzzles[i].which, g_uvMuzzles[i].set.c_str(),
               g_uvPacks[g_uvMuzzles[i].pack].name.c_str(), g_uvMuzzles[i].size, g_uvMuzzles[i].burst,
               g_uvMuzzles[i].gap);
    uvfx_load_trails(root);
    fprintf(stderr, "unityvfx: %d gun sockets, %d muzzle flash rules, projectile trails %s\n",
            (int)g_uvSockDefs.size(), (int)g_uvMuzzles.size(), uvfx_any_trail() ? "on" : "off");
}

/* Does anything want this model's sockets? Cheap enough to ask on every draw. */
static bool uvfx_sock_wanted(const M3Model& m)
{
    if (g_uvMuzzles.empty() || !uvfx_available()) return false;
    for (size_t i = 0; i < g_uvSockDefs.size(); i++) if (g_uvSockDefs[i].model == m.type) return true;
    return false;
}

static void uvfx_sock_put(UvfxSockRec& rec, int di, const float* mat, const float* p, const float* f,
                          const float* u, float k)
{
    UvfxSockXf x;
    for (int r = 0; r < 3; r++) {
        x.p[r] = mat[r] * p[0] + mat[4 + r] * p[1] + mat[8 + r] * p[2] + mat[12 + r];
        x.f[r] = mat[r] * f[0] + mat[4 + r] * f[1] + mat[8 + r] * f[2];
        x.u[r] = mat[r] * u[0] + mat[4 + r] * u[1] + mat[8 + r] * u[2];
    }
    x.k = k;
    rec.def.push_back(di);
    rec.xf.push_back(x);
}

/* A 3D SOLDIER'S SOCKETS, on the pose m3_skin just used: bone space -> the bone's posed
   transform (its quaternion, translation and scale in model space) -> the draw's own
   model matrix. Called by the soldier draw right after it posed and drew him. */
static void uvfx_sock_soldier(const SimObject& o, const M3Model& m, const M3Draw& d)
{
    if (!uvfx_sock_wanted(m) || m.pose.size() != m.bone.size() * 8) return;
    const int key = uvfx_rtti_of(o.kind) * 100000 + o.id;
    UvfxSockRec& rec = g_uvSock[key];
    rec.serial = g_uvSerial;
    rec.def.clear(); rec.xf.clear();
    float mat[16];
    m3_matrix(d, mat);
    for (size_t i = 0; i < g_uvSockDefs.size(); i++) {
        UvfxSockDef& sd = g_uvSockDefs[i];
        if (sd.model != m.type || sd.boneName.empty()) continue;
        if (sd.bone < 0)
            for (size_t b = 0; b < m.bone.size(); b++) if (sd.boneName == m.bone[b].name) sd.bone = (int)b;
        if (sd.bone < 0) continue;
        const float* P = &m.pose[(size_t)sd.bone * 8];
        float q[4] = { P[0], P[1], P[2], P[3] };
        const float ql = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        if (ql < 1e-9f) continue;
        for (int k = 0; k < 4; k++) q[k] /= ql;
        const float x = q[0], y = q[1], z = q[2], w = q[3], s = P[7];
        const float R[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
                             2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                             2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) };
        float pm[3], fm[3], um[3];
        for (int r = 0; r < 3; r++) {
            pm[r] = (R[r * 3] * sd.p[0] + R[r * 3 + 1] * sd.p[1] + R[r * 3 + 2] * sd.p[2]) * s + P[4 + r];
            fm[r] = R[r * 3] * sd.f[0] + R[r * 3 + 1] * sd.f[1] + R[r * 3 + 2] * sd.f[2];
            um[r] = R[r * 3] * sd.u[0] + R[r * 3 + 1] * sd.u[1] + R[r * 3 + 2] * sd.u[2];
        }
        /* the soldier's own size dial scales his flash with his rifle */
        uvfx_sock_put(rec, (int)i, mat, pm, fm, um, d.scale > 0.0f ? d.scale : 1.0f);
    }
}

/* A VEHICLE'S SOCKETS: a part's socket turned with that part (the turret) exactly as the
   draw turns its triangles (m3_pivot_yaw about the part's pivot). */
static void uvfx_sock_vehicle(const SimObject& o, const M3Model& m, const M3Draw& d)
{
    if (!uvfx_sock_wanted(m)) return;
    const int key = uvfx_rtti_of(o.kind) * 100000 + o.id;
    UvfxSockRec& rec = g_uvSock[key];
    rec.serial = g_uvSerial;
    rec.def.clear(); rec.xf.clear();
    float mat[16];
    m3_matrix(d, mat);
    for (size_t i = 0; i < g_uvSockDefs.size(); i++) {
        const UvfxSockDef& sd = g_uvSockDefs[i];
        if (sd.model != m.type || sd.partName.empty()) continue;
        float pm[16];
        memcpy(pm, mat, sizeof pm);
        for (size_t pi = 0; pi < m.part.size(); pi++) {
            const M3Part& pt = m.part[pi];
            if (sd.partName != pt.name) continue;
            if (pt.role == M3_ROLE_TURRET && d.turyaw != 0) {
                const float piv[3] = { pt.px, pt.py, pt.pz };
                m3_pivot_yaw(pm, piv, -(float)d.turyaw * (360.0f / 256.0f));
            }
            break;
        }
        uvfx_sock_put(rec, (int)i, pm, sd.p, sd.f, sd.u, 1.0f);
    }
}

/* WHERE EACH FLYING ROUND LEFT FROM. The engine launches a tank shell or an infantry
   rocket from a point of its own at altitude 0, so its model skims the ground from in front
   of the firer while the muzzle flash plays at the barrel or up on the launcher: three
   places for one shot. A shot with a socket records the barrel (s) and the engine's launch
   point as the model would be drawn there (e); the model and its trail are then drawn
   offset by s - e, fading along the first UVFX_LAUNCH_EASE cells flown so the round
   leaves the barrel and joins the engine's line of flight. A tank shell keeps the
   barrel's height for the whole flight: it flies level from the gun, and its Unity hit is
   set off above the ground (vfx3d/claims.txt, VEH-HIT3), so a shell dropped to the ground
   ran in under its own explosion. Rockets and missiles sink to the ground they skim,
   where their hits are set off.
   Keyed by the bullet's heap id and type; a new shot with the same id replaces it. */
static float efx_bullet_centre_lift(float ylift, bool lay_flat);   /* cnc_eyes.cpp, after draw_mesh */
struct UvfxLaunch { int bullet, btype, hold; float s[3], e[3]; };
static std::vector<UvfxLaunch> g_uvLaunch;
static const float UVFX_LAUNCH_EASE = 1.0f;
/* rounds the engine has just fired, by id: a trail still following an older round with
   the same id stops, since that id now names the new one */
struct UvfxNewRound { int bullet, btype, frame; };
static std::vector<UvfxNewRound> g_uvNewRounds;

static void uvfx_launch_forget(int bullet, int btype)
{
    for (size_t i = 0; i < g_uvLaunch.size(); i++)
        if (g_uvLaunch[i].bullet == bullet && g_uvLaunch[i].btype == btype) {
            g_uvLaunch.erase(g_uvLaunch.begin() + (long)i);
            return;
        }
}

static void uvfx_launch_note(const EfxFire& f, const float* from)
{
    uvfx_launch_forget(f.bullet, f.btype);
    if (f.bullet < 0 || !from) return;
    UvfxLaunch L;
    L.bullet = f.bullet; L.btype = f.btype; L.hold = efx_bullet_lays_flat(f.bname) ? 1 : 0;
    memcpy(L.s, from, sizeof L.s);
    L.e[0] = f.wx; L.e[2] = f.wz;
    L.e[1] = efx_ground(f.wx, f.wz) + efx_bullet_centre_lift(0.0f, efx_bullet_lays_flat(f.bname));
    g_uvLaunch.push_back(L);
}

/* the offset to draw bullet (id, type) at, now that it is at (wx, wz) */
static bool uvfx_launch_offset(int id, int type, float wx, float wz, float out[3])
{
    if (!uvfx_available()) return false;
    for (size_t i = 0; i < g_uvLaunch.size(); i++) {
        const UvfxLaunch& L = g_uvLaunch[i];
        if (L.bullet != id || L.btype != type) continue;
        const float dx = wx - L.e[0], dz = wz - L.e[2];
        float w = 1.0f - sqrtf(dx * dx + dz * dz) / UVFX_LAUNCH_EASE;
        if (w < 0.0f) w = 0.0f;
        out[0] = (L.s[0] - L.e[0]) * w;
        out[1] = (L.s[1] - L.e[1]) * (L.hold ? 1.0f : w);
        out[2] = (L.s[2] - L.e[2]) * w;
        return true;
    }
    return false;
}

/* drop the records of rounds that have landed */
static void uvfx_launch_prune(void)
{
    size_t w = 0;
    for (size_t i = 0; i < g_uvLaunch.size(); i++) {
        bool live = false;
        for (size_t j = 0; j < g_efxBullets.size() && !live; j++)
            live = g_efxBullets[j].id == g_uvLaunch[i].bullet && g_efxBullets[j].type == g_uvLaunch[i].btype;
        if (live) g_uvLaunch[w++] = g_uvLaunch[i];
    }
    g_uvLaunch.resize(w);
}

/* The engine's shot, straight from the dump: queued for the next drawn frame, where the
   firer's sockets are known. */
static void uvfx_on_fire(const EfxFire& f)
{
    /* every shot the engine reports, drawn or not, for a harness to time things by */
    printf("UVFX|fire|type=%s|id=%d|which=%d|second=%d|btype=%d|bullet=%d|frame=%d\n", f.type, f.id, f.which,
           f.second, f.btype, f.bullet, g_engineFrame);
    if ((g_uvMuzzles.empty() && !uvfx_any_trail()) || !uvfx_available()) return;
    /* this id is a new round now: the old one's launch and trail are finished with */
    uvfx_launch_forget(f.bullet, f.btype);
    UvfxNewRound nr = { f.bullet, f.btype, g_engineFrame };
    g_uvNewRounds.push_back(nr);
    UvfxFireQ q;
    q.f = f;
    q.frame = g_engineFrame;
    g_uvFires.push_back(q);
}

static const char* uvfx_type_alias(const char* t)
{
    return !strcmp(t, "E1TR") ? "E1" : t;
}

static bool uvfx_any_muzzle(void) { return !g_uvMuzzles.empty(); }

/* Does this engine type have a Unity muzzle flash for any weapon? */
static bool uvfx_has_muzzle(const char* type)
{
    for (size_t i = 0; i < g_uvMuzzles.size(); i++)
        if (g_uvMuzzles[i].type == type || g_uvMuzzles[i].type == uvfx_type_alias(type)) return true;
    return false;
}

static void uvfx_place_on_socket(UvfxInst& in, const UvfxSockXf& x, float size)
{
    in.x = x.p[0]; in.y = x.p[1]; in.z = x.p[2];
    uvfx_basis_aim(x.f, x.u, in.A);
    in.k = x.k * size;
}

/* Which socket of `set` a shot left from: a pair by the engine's barrel, four as two a
   side taken in turn, anything else in turn. -1 when the firer has no such set. */
static int uvfx_pick_slot(const UvfxSockRec& rec, const std::string& set, const EfxFire& f, int turn)
{
    std::vector<int> slots;
    for (size_t k = 0; k < rec.def.size(); k++)
        if (g_uvSockDefs[rec.def[k]].set == set) slots.push_back((int)k);
    if (slots.empty()) return -1;
    const int n = (int)slots.size();
    int pick = 0;
    if (n == 2) pick = f.second ? 1 : 0;
    else if (n == 4) pick = (f.second ? 2 : 0) + (turn / 2) % 2;
    else pick = turn % n;
    return slots[pick];
}

/* One flash of rule r on socket `slot` of the firer `key`, flash k of its burst, playing
   from engine time `at`. */
static void uvfx_spawn_flash(const EfxFire& f, int key, const UvfxSockRec& rec, int slot, int r, int k, float at)
{
    const UvfxMuzzleRule& mr = g_uvMuzzles[r];
    const UvfxPack& pk = g_uvPacks[mr.pack];
    unsigned h = (unsigned)f.id * 2654435761u ^ (unsigned)(at * 2.0f) * 40503u ^ (unsigned)r * 977u
               ^ (unsigned)k * 7919u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    UvfxInst in;
    memset(&in, 0, sizeof in);
    in.animId = -1;
    in.pack = mr.pack;
    in.seed = (int)(h % (unsigned)pk.seed.size());
    in.surfaced = 1;                               /* it starts on the gun, not the ground */
    in.spawnT = at;
    in.follow = key; in.sock = slot;
    uvfx_place_on_socket(in, rec.xf[slot], mr.size * (g_fx.uvfx_gunsize > 0.01f ? g_fx.uvfx_gunsize : 1.0f));
    g_uvInst.push_back(in);
    g_uvMuzzleSpawned++;
    printf("UVFX|muzzle|type=%s|id=%d|which=%d|second=%d|set=%s|sock=%d|pack=%s|seed=%d|burst=%d|"
           "at=%.3f,%.3f,%.3f|dir=%.3f,%.3f,%.3f|engine=%.3f,%.3f|frame=%.2f\n",
           f.type, f.id, f.which, f.second, mr.set.c_str(), g_uvSockDefs[rec.def[slot]].index,
           pk.name.c_str(), in.seed, k, in.x, in.y, in.z, rec.xf[slot].f[0], rec.xf[slot].f[1],
           rec.xf[slot].f[2], f.wx, f.wz, at);
}

/* THE SHOTS OF THIS FRAME, into flashes and trails. A shot whose firer has no socket on
   this frame waits one more frame (the draw may not have reached it yet); after that it
   gets no flash, and its trail starts from the engine's own launch point. */
static void uvfx_muzzle_fires(void)
{
    std::vector<UvfxFireQ> keep;
    for (size_t i = 0; i < g_uvFires.size(); i++) {
        const UvfxFireQ& q = g_uvFires[i];
        const EfxFire& f = q.f;
        const int key = f.kind * 100000 + f.id;
        std::map<int, UvfxSockRec>::iterator si = g_uvSock.find(key);
        if (si == g_uvSock.end() || si->second.serial != g_uvSerial) {
            if (g_engineFrame - q.frame <= 1) keep.push_back(q);
            else { g_uvFiresNoSocket++; uvfx_launch_forget(f.bullet, f.btype); uvfx_trail_start(f, q.frame, NULL); }
            continue;
        }
        const UvfxSockRec& rec = si->second;
        const int turn = g_uvTurn[key * 2 + (f.which ? 1 : 0)]++;
        float from[3];
        int haveFrom = 0;
        for (size_t r = 0; r < g_uvMuzzles.size(); r++) {
            const UvfxMuzzleRule& mr = g_uvMuzzles[r];
            if (mr.which != f.which) continue;
            if (mr.type != f.type && mr.type != uvfx_type_alias(f.type)) continue;
            const int slot = uvfx_pick_slot(rec, mr.set, f, turn);
            if (slot < 0) continue;
            if (!haveFrom) { memcpy(from, rec.xf[slot].p, sizeof from); haveFrom = 1; }
            /* THE FLASH STARTS NOW, on the first frame it can be drawn, not at the tick the
               shot was reported: the report reaches the renderer a tick after the shot,
               and a flash timed from the shot had already spent its brightest tick
               unseen (measured: nothing on the shot's tick, the flash first on the next). */
            const float t0 = engine_time();
            uvfx_spawn_flash(f, key, rec, slot, (int)r, 0, t0);
            for (int k = 1; k < mr.burst; k++) {
                UvfxBurstQ b;
                b.f = f; b.key = key; b.slot = slot; b.rule = (int)r; b.k = k;
                b.due = t0 + k * mr.gap;
                g_uvBursts.push_back(b);
            }
        }
        if (!haveFrom) {
            /* no flash on this weapon: its trail still starts at the barrel */
            const char* set = f.kind == 1 ? "muzzle" : (f.which ? "tusk" : "cannon");
            const int slot = uvfx_pick_slot(rec, set, f, turn);
            if (slot >= 0) { memcpy(from, rec.xf[slot].p, sizeof from); haveFrom = 1; }
        }
        /* THE ROUND LEAVES FROM THE BARREL, a soldier's from his launcher: its model and
           its trail start there and ease into the engine's flight (uvfx_launch_offset).
           This used to put a soldier's trail on the ground under his launcher, which was
           neither where his rocket was drawn nor where the flash played. */
        uvfx_launch_note(f, haveFrom ? from : NULL);
        uvfx_trail_start(f, q.frame, haveFrom ? from : NULL);
    }
    g_uvFires.swap(keep);
    /* THE REST OF EACH BURST, on its tick, on the barrel as it is then. A firer not drawn
       on that tick loses the flash, as a first shot would. A rule changed since (the
       picker switches them) drops what it left behind. */
    std::vector<UvfxBurstQ> later;
    for (size_t i = 0; i < g_uvBursts.size(); i++) {
        const UvfxBurstQ& b = g_uvBursts[i];
        if (engine_time() < b.due) { later.push_back(b); continue; }
        std::map<int, UvfxSockRec>::iterator si = g_uvSock.find(b.key);
        if (b.rule >= (int)g_uvMuzzles.size() || si == g_uvSock.end() || si->second.serial != g_uvSerial
            || b.slot >= (int)si->second.xf.size())
            continue;
        uvfx_spawn_flash(b.f, b.key, si->second, b.slot, b.rule, b.k, b.due);
    }
    g_uvBursts.swap(later);
    /* the flashes ride their guns: every one whose firer was drawn this frame moves with it */
    for (size_t i = 0; i < g_uvInst.size(); i++) {
        UvfxInst& in = g_uvInst[i];
        if (in.follow < 0) continue;
        std::map<int, UvfxSockRec>::iterator si = g_uvSock.find(in.follow);
        if (si == g_uvSock.end() || si->second.serial != g_uvSerial || in.sock >= (int)si->second.xf.size())
            continue;
        const float size = in.k / (si->second.xf[in.sock].k > 0 ? si->second.xf[in.sock].k : 1.0f);
        uvfx_place_on_socket(in, si->second.xf[in.sock], size);
    }
    /* forget firers not drawn for a while */
    if ((g_uvSerial & 255) == 0)
        for (std::map<int, UvfxSockRec>::iterator j = g_uvSock.begin(); j != g_uvSock.end();)
            if (g_uvSerial - j->second.serial > 600) g_uvSock.erase(j++); else ++j;
}

/* SCRIPT: uvfxmuzzle TYPE WEAPON SET PACK SIZE [BURST GAP] replaces that weapon's flashes
   with this one (the picker films each candidate this way); uvfxmuzzle TYPE WEAPON off
   removes them. Returns false when the pack cannot be loaded. */
static bool uvfx_muzzle_override(const char* type, int which, const char* set, const char* pack, float size,
                                 int burst = 1, float gap = 0.0f)
{
    g_uvBursts.clear();
    for (size_t i = 0; i < g_uvMuzzles.size();)
        if (g_uvMuzzles[i].type == type && g_uvMuzzles[i].which == which) g_uvMuzzles.erase(g_uvMuzzles.begin() + i);
        else i++;
    if (!strcmp(set, "off")) return true;
    const int pi = uvfx_pack_get(pack);
    if (pi < 0) return false;
    UvfxMuzzleRule r;
    r.type = type; r.which = which; r.set = set; r.pack = pi; r.size = size;
    r.burst = burst < 1 ? 1 : burst; r.gap = gap;
    g_uvMuzzles.push_back(r);
    printf("UVFX|muzzlerule|type=%s|which=%d|set=%s|pack=%s|size=%.2f|burst=%d|gap=%.2f\n", type, which, set,
           pack, size, r.burst, gap);
    fflush(stdout);
    return true;
}


/* ================================================================== TRAILS
   A PROJECTILE'S TRAIL, from a Unity prefab recorded in flight (the bake flies it at a
   steady speed, -vfxMove, and a world-space system leaves its particles behind). Every
   particle of that recording is a LIFE: when and where along the flight it was born, and
   its state every frame from then on (UvfxLife). In the game the same lives are laid down
   along the projectile's own path, each at the distance the recording's emitter had
   flown when it was born, turned to the projectile's heading there; from then on each
   plays its recorded life, relative to that birth point. So rate over distance, the
   drift, the rise and the fade are Unity's, and the path is the engine's.

   The recording's flight is finite; a longer path starts the lives again from the first
   (the seam is one particle spacing). A trail keeps laying particles while its
   projectile exists and stops when it is gone; the ones already laid live out their
   recorded lives, as Unity's do when an emitter stops.

   WHICH TRAIL is data (vfx3d/trails.txt): "BULLETTYPE PACK SIZE [OPACITY]", by the engine's
   BulletType number, because two projectiles share one name (the Mammoth's tusk missile
   and the rocket soldier's rocket are both "DRAGON"). The trail starts at the barrel the
   shot left (the same socket its muzzle flash plays at), else at the engine's own launch
   point. OPACITY (default 1) multiplies a smoke ribbon's alpha: the overheat ribbons were
   made as wisps off a hot barrel, their texture's smoke band averaging 0.02 in linear, and
   behind a rocket at the game's distance they all but vanish. */
struct UvfxTrailRule { int btype, pack; float size, opacity, rise, spread, age; };
static std::vector<UvfxTrailRule> g_uvTrailRules;
struct UvfxTrailPt { float t, p[3], D; };            /* engine ticks, world, cells flown */
struct UvfxTrailP { int sys, life; float birthT; float o[3]; float A[9]; };
struct UvfxTrail {
    int pack, seed, bullet, btype;
    float k, op = 1.0f;
    float rise = 1.0f;                             /* of the smoke's climb (UvfxTrailRule) */
    float spread = 1.0f;                           /* of its drift across and along the flight */
    float age = 0.0f;                              /* seconds its smoke is already old when laid */
    int flying;
    std::vector<UvfxTrailPt> path;
    std::vector<int> next;                         /* per recorded system: next life      */
    std::vector<float> cyc;                        /* per recorded system: metres added   */
    std::vector<UvfxTrailP> parts;
};
static std::vector<UvfxTrail> g_uvTrails;
static int g_uvTrailsSpawned = 0, g_uvTrailParts = 0;
/* Is a flying Unity trail's projectile within reach of (x, z)? */
static bool uvfx_trail_near(float x, float z)
{
    for (size_t i = 0; i < g_uvTrails.size(); i++) {
        const UvfxTrail& tr = g_uvTrails[i];
        if (!tr.flying || tr.path.empty()) continue;
        const float dx = tr.path.back().p[0] - x, dz = tr.path.back().p[2] - z;
        if (dx * dx + dz * dz < 0.75f * 0.75f) return true;
    }
    return false;
}

static bool uvfx_any_trail(void) { return !g_uvTrailRules.empty(); }

static void uvfx_load_trails(const std::string& root)
{
    std::vector<unsigned char> txt;
    if (!uvfx_read_file(root + "trails.txt", txt)) return;
    txt.push_back(0);
    char* cur = (char*)&txt[0];
    for (char* line = uvfx_tok(&cur, '\n'); line; line = uvfx_tok(&cur, '\n')) {
        if (line[0] == '#' || !line[0]) continue;
        char pack[128];
        UvfxTrailRule r;
        r.opacity = 1.0f; r.rise = 1.0f; r.spread = 1.0f; r.age = 0.0f;
        if (sscanf(line, "%d %127s %f %f %f %f %f", &r.btype, pack, &r.size, &r.opacity, &r.rise, &r.spread,
                   &r.age) < 3) continue;
        r.pack = uvfx_pack_get(pack);
        if (r.pack < 0 || !g_uvPacks[r.pack].trail) {
            fprintf(stderr, "unityvfx: trail %s not loaded (or not a trail recording)\n", pack);
            continue;
        }
        g_uvTrailRules.push_back(r);
        printf("UVFX|trailrule|btype=%d|pack=%s|size=%.2f|opacity=%.2f|rise=%.2f|spread=%.2f|age=%.2f\n", r.btype,
               pack, r.size, r.opacity, r.rise, r.spread, r.age);
    }
}

/* SCRIPT: uvfxtrail BULLETTYPE PACK SIZE [OPACITY [RISE]] (or +PACK to add one), or BULLETTYPE off. */
static bool uvfx_trail_override(int btype, const char* pack, float size, float opacity = 1.0f, float rise = 1.0f)
{
    /* "+PACK" adds a trail to the ones this projectile already has; plain PACK replaces them */
    const bool add = pack[0] == '+';
    if (add) pack++;
    for (size_t i = 0; !add && i < g_uvTrailRules.size();)
        if (g_uvTrailRules[i].btype == btype) g_uvTrailRules.erase(g_uvTrailRules.begin() + i); else i++;
    if (!strcmp(pack, "off")) return true;
    const int pi = uvfx_pack_get(pack);
    if (pi < 0 || !g_uvPacks[pi].trail) return false;
    UvfxTrailRule r; r.btype = btype; r.pack = pi; r.size = size; r.opacity = opacity; r.rise = rise; r.spread = 1.0f; r.age = 0.0f;
    g_uvTrailRules.push_back(r);
    printf("UVFX|trailrule|btype=%d|pack=%s|size=%.2f|opacity=%.2f|rise=%.2f\n", btype, pack, size, opacity, rise);
    fflush(stdout);
    return true;
}

static void uvfx_trail_start(const EfxFire& f, int frame, const float* from)
{
    if (f.bullet < 0) return;
    /* every rule for this projectile type lays its own trail: a smoke and a spark trail
       can fly together */
    for (size_t ri = 0; ri < g_uvTrailRules.size(); ri++) {
        const UvfxTrailRule* r = &g_uvTrailRules[ri];
        if (r->btype != f.btype) continue;
        const UvfxPack& pk = g_uvPacks[r->pack];
        UvfxTrail tr;
        tr.pack = r->pack; tr.k = r->size; tr.op = r->opacity; tr.rise = r->rise; tr.spread = r->spread; tr.age = r->age; tr.bullet = f.bullet; tr.btype = f.btype; tr.flying = 1;
        unsigned h = (unsigned)f.id * 2654435761u ^ (unsigned)frame * 40503u ^ (unsigned)f.bullet * 977u
                   ^ (unsigned)ri * 7919u;
        h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
        tr.seed = (int)(h % (unsigned)(pk.lives.empty() ? 1 : pk.lives.size()));
        UvfxTrailPt p0;
        p0.t = (float)frame; p0.D = 0.0f;
        if (from) { p0.p[0] = from[0]; p0.p[1] = from[1]; p0.p[2] = from[2]; }
        else {
            p0.p[0] = f.wx; p0.p[2] = f.wz;
            p0.p[1] = efx_ground(f.wx, f.wz) + efx_bullet_centre_lift(0.0f, efx_bullet_lays_flat(f.bname));
        }
        tr.path.push_back(p0);
        tr.next.assign(pk.nsys, 0);
        tr.cyc.assign(pk.nsys, 0.0f);
        g_uvTrails.push_back(tr);
        g_uvTrailsSpawned++;
        printf("UVFX|trail|btype=%d|bullet=%d|by=%s#%d|pack=%s|seed=%d|from=%.3f,%.3f,%.3f|frame=%d\n", f.btype,
               f.bullet, f.type, f.id, pk.name.c_str(), tr.seed, p0.p[0], p0.p[1], p0.p[2], frame);
    }
}

/* Is recorded system s drawn by this pack, as particles or as a ribbon? */
static bool uvfx_sys_drawn(const UvfxPack& pk, int s)
{
    for (size_t i = 0; i < pk.sys.size(); i++) if (pk.sys[i].index == s) return true;
    for (size_t i = 0; i < pk.ribbons.size(); i++) if (pk.ribbons[i].sys == s) return true;
    return false;
}

/* Has this trail's trail renderer still got a point younger than its time? */
static bool uvfx_line_alive(const UvfxTrail& tr, float now)
{
    const UvfxPack& pk = g_uvPacks[tr.pack];
    if (pk.lines.empty() || tr.path.size() < 2) return false;
    float tmax = 0.0f;
    for (size_t i = 0; i < pk.lines.size(); i++) if (pk.lines[i].time > tmax) tmax = pk.lines[i].time;
    return (now - tr.path[tr.path.size() - 2].t) / 15.0f <= tmax;
}

/* Follow the projectiles, lay down new particles, retire the dead ones. Once a drawn frame. */
static void uvfx_trails_step(float S)
{
    const float now = engine_time();
    /* A ROUND THAT HAS LANDED STOPS ITS TRAIL, even when the engine hands its id straight
       to the next shot. The id test alone could not see that: a Mammoth's next shell
       leaves the barrel well within the 4 cells the jump test allows, and the old trail
       ran from the hit back to the tank and out along the new shell's flight. */
    for (size_t n = 0; n < g_uvNewRounds.size(); n++)
        for (size_t ti = 0; ti < g_uvTrails.size(); ti++) {
            UvfxTrail& tr = g_uvTrails[ti];
            if (tr.flying && tr.bullet == g_uvNewRounds[n].bullet && tr.btype == g_uvNewRounds[n].btype
                && !tr.path.empty() && tr.path[0].t < (float)g_uvNewRounds[n].frame) {
                tr.flying = 0;
                printf("UVFX|trailstop|why=newround|btype=%d|bullet=%d|frame=%d\n", tr.btype, tr.bullet,
                       g_engineFrame);
            }
        }
    g_uvNewRounds.clear();
    for (size_t ti = 0; ti < g_uvTrails.size(); ti++) {
        UvfxTrail& tr = g_uvTrails[ti];
        const UvfxPack& pk = g_uvPacks[tr.pack];
        const float cpm = S * tr.k;
        if (tr.flying) {
            const EfxBullet* b = NULL;
            for (size_t i = 0; i < g_efxBullets.size(); i++)
                if (g_efxBullets[i].id == tr.bullet && g_efxBullets[i].type == tr.btype) { b = &g_efxBullets[i]; break; }
            if (!b) tr.flying = 0;
            else {
                UvfxTrailPt q;
                q.t = (float)g_engineFrame;
                /* through the middle of the model as it is drawn, launch offset and all */
                float off[3] = { 0.0f, 0.0f, 0.0f };
                uvfx_launch_offset(b->id, b->type, b->wx, b->wz, off);
                q.p[0] = b->wx + off[0]; q.p[2] = b->wz + off[2];
                q.p[1] = efx_ground(b->wx, b->wz)
                       + efx_bullet_centre_lift(efx_bullet_lift(*b), efx_bullet_lays_flat(b->name)) + off[1];
                const UvfxTrailPt& l = tr.path.back();
                const float dx = q.p[0] - l.p[0], dy = q.p[1] - l.p[1], dz = q.p[2] - l.p[2];
                const float d = sqrtf(dx * dx + dy * dy + dz * dz);
                /* a round never turns back on itself: a step against the last one is the id
                   gone to another round the new-round list did not catch */
                bool back = false;
                if (tr.path.size() > 1) {
                    const UvfxTrailPt& g = tr.path[tr.path.size() - 2];
                    back = dx * (l.p[0] - g.p[0]) + dz * (l.p[2] - g.p[2]) < 0.0f;
                }
                if ((d > 4.0f || back) && tr.path.size() > 1) {                /* the id went to another round */
                    tr.flying = 0;
                    printf("UVFX|trailstop|why=%s|btype=%d|bullet=%d|frame=%d\n", back ? "back" : "jump", tr.btype,
                           tr.bullet, g_engineFrame);
                }
                else if (d > 1e-4f) { q.D = l.D + d; tr.path.push_back(q); }
            }
        }
        /* lay down every life whose birth distance the projectile has now flown */
        if (tr.path.size() >= 2 && !pk.lives.empty()) {
            const float flown = tr.path.back().D / cpm;                       /* metres */
            for (int s = 0; s < pk.nsys; s++) {
                const std::vector<UvfxLife>& L = pk.lives[tr.seed][s];
                if (L.empty() || !uvfx_sys_drawn(pk, s)) continue;
                const float span = L.back().bdist - L.front().bdist;
                const float window = L.back().bdist + (L.size() > 1 ? span / (float)(L.size() - 1) : 1.0f);
                for (int guard = 0; guard < 4096; guard++) {
                    if (tr.next[s] >= (int)L.size()) { tr.next[s] = 0; tr.cyc[s] += window; }
                    const float bd = L[tr.next[s]].bdist + tr.cyc[s];
                    if (bd > flown) break;
                    const float dc = bd * cpm;
                    size_t j = 0;
                    while (j + 2 < tr.path.size() && tr.path[j + 1].D < dc) j++;
                    const UvfxTrailPt& a = tr.path[j];
                    const UvfxTrailPt& c = tr.path[j + 1];
                    const float seg = c.D - a.D;
                    const float u = seg > 1e-6f ? (dc - a.D) / seg : 1.0f;
                    UvfxTrailP P;
                    P.sys = s; P.life = tr.next[s];
                    P.birthT = a.t + (c.t - a.t) * u;
                    for (int k = 0; k < 3; k++) P.o[k] = a.p[k] + (c.p[k] - a.p[k]) * u;
                    const float dir[3] = { c.p[0] - a.p[0], c.p[1] - a.p[1], c.p[2] - a.p[2] };
                    const float up[3] = { 0.0f, 1.0f, 0.0f };
                    uvfx_basis_aim(dir, up, P.A);
                    /* the bake flew along Unity's +z, which basis_aim puts down the heading */
                    tr.parts.push_back(P);
                    tr.next[s]++;
                }
            }
        }
        /* retire lives that have run out */
        size_t w = 0;
        for (size_t i = 0; i < tr.parts.size(); i++) {
            const UvfxTrailP& P = tr.parts[i];
            const UvfxLife& L = pk.lives[tr.seed][P.sys][P.life];
            const float f = (L.tau + tr.age + (now - P.birthT) / 15.0f) * (float)pk.fps;
            if (f < (float)(L.first + (int)L.rec.size() - 1)) tr.parts[w++] = P;
        }
        tr.parts.resize(w);
    }
    size_t w = 0;
    for (size_t i = 0; i < g_uvTrails.size(); i++)
        if (g_uvTrails[i].flying || !g_uvTrails[i].parts.empty() || uvfx_line_alive(g_uvTrails[i], now)) {
            if (w != i) g_uvTrails[w] = g_uvTrails[i];
            w++;
        }
    g_uvTrails.resize(w);
}

/* One life's state now, in the game's world divided by cells per metre: drawn through a
   frame with no turn of its own (uvfx_trail_frame), since each life has its own. */
static bool uvfx_trail_state(const UvfxPack& pk, const UvfxTrail& tr, const UvfxTrailP& P, float now, UvfxP& out)
{
    const UvfxLife& L = pk.lives[tr.seed][P.sys][P.life];
    /* AGE: the smoke is laid already that many seconds into its life. A recording's
       newest smoke starts small and clear and grows in, which at a large rule size left
       the stretch behind the round empty and read as a trail that stops short of it. */
    float f = (L.tau + tr.age + (now - P.birthT) / 15.0f) * (float)pk.fps - (float)L.first;
    if (f < 0.0f) f = 0.0f;
    const int k0 = (int)f;
    if (k0 + 1 >= (int)L.rec.size()) return false;
    const float a = f - (float)k0;
    const UvfxSeed& sd = pk.seed[tr.seed];
    const float* A = &sd.recs[(size_t)L.rec[k0] * UVR_N];
    const float* B = &sd.recs[(size_t)L.rec[k0 + 1] * UVR_N];
    memcpy(out.r, A, sizeof out.r);
    for (int k = 1; k < UVR_N; k++) {
        if (k == UVR_TILE || k == UVR_TILE + 1 || k == UVR_MESH) continue;
        float d = B[k] - A[k];
        if (k >= UVR_ROT && k < UVR_ROT + 3) { while (d > 180.0f) d -= 360.0f; while (d < -180.0f) d += 360.0f; }
        out.r[k] = A[k] + d * a;
    }
    const float cpm = (g_fx.uvfx_scale > 0.01f ? g_fx.uvfx_scale : 0.33f) * tr.k;
    /* A WORLD-SPACE LIFE stays where it was born: relative to the emitter at its birth. A
       LOCAL-SPACE one rides the emitter: relative to where the emitter is now. */
    bool local = false;
    for (size_t q = 0; q < pk.sys.size(); q++) if (pk.sys[q].index == P.sys) local = pk.sys[q].simLocal != 0;
    const float tnow = L.tau + tr.age + (now - P.birthT) / 15.0f;
    const float bt0 = local ? tnow : L.tau;
    const float bt = bt0 < pk.moveStop ? bt0 : pk.moveStop;
    UvfxTrailP Q = P;
    if (local && !tr.path.empty()) {
        const UvfxTrailPt& h = tr.path.back();
        for (int k = 0; k < 3; k++) Q.o[k] = h.p[k];
        if (tr.path.size() >= 2) {
            const UvfxTrailPt& g = tr.path[tr.path.size() - 2];
            const float dir[3] = { h.p[0] - g.p[0], h.p[1] - g.p[1], h.p[2] - g.p[2] };
            const float up[3] = { 0.0f, 1.0f, 0.0f };
            uvfx_basis_aim(dir, up, Q.A);
        }
    }
    const float rel[3] = { out.r[UVR_POS] - pk.move[0] * bt, out.r[UVR_POS + 1] - pk.move[1] * bt,
                           out.r[UVR_POS + 2] - pk.move[2] * bt };
    const float v[3] = { out.r[UVR_VEL], out.r[UVR_VEL + 1], out.r[UVR_VEL + 2] };
    /* RISE scales the world-vertical part of a life's travel from where it was laid, and
       SPREAD the two ground directions: the recording's climb, and its drift across and
       along the flight. Both grow with the rule's size, so a recording made for a flight
       metres up, drawn several times its own scale, climbed clear of a round that skims the
       ground and wandered off its line and well behind it. The rule's two numbers pull the
       smoke back to the path the round actually flew. */
    for (int r = 0; r < 3; r++) {
        const float g = r == 1 ? tr.rise : tr.spread;
        const float wv = Q.A[r * 3] * rel[0] + Q.A[r * 3 + 1] * rel[1] + Q.A[r * 3 + 2] * rel[2];
        out.r[UVR_POS + r] = Q.o[r] / cpm + wv * g;
        out.r[UVR_VEL + r] = (Q.A[r * 3] * v[0] + Q.A[r * 3 + 1] * v[1] + Q.A[r * 3 + 2] * v[2]) * g;
    }
    return true;
}

/* The frame a trail's already-placed particles are drawn through: world axes, its scale. */
static UvfxFrame uvfx_trail_frame(const UvfxTrail& tr, float S)
{
    UvfxFrame F;
    F.S = S * tr.k;
    for (int k = 0; k < 9; k++) F.A[k] = (k % 4 == 0) ? 1.0f : 0.0f;
    F.ox = F.oy = F.oz = 0.0f;
    return F;
}

/* THE RIBBON (the Trails module, Ribbon mode), rebuilt as Unity builds it, checked against
   BakeTrailsMesh: one vertex pair per live particle, oldest first, on the particle
   itself and half its width either side, then (attach) one more on the emitter; the U
   coordinate counts particles (RepeatPerSegment) or runs 0..1 (Stretch), V is 1 on one
   side and 0 on the other; colour is the particle's, times the curve along the trail.
   Unity's centres and widths are matched exactly. Its side direction is not quite: taken
   here square to the view and to the line through the two neighbours, which is Unity's
   to a median of 1.6 degrees and 40 at worst, where the ribbon runs into the view. */
static void uvfx_draw_ribbon(GLuint p, const UvfxRibbonSpec& R, const std::vector<UvfxP>& P,
                             const UvfxFrame& F, const float* attach)
{
    std::vector<float> pts, wid, col;
    for (size_t i = 0; i < P.size(); i++) {
        const float* r = P[i].r;
        for (int k = 0; k < 3; k++) pts.push_back(F.ox + r[UVR_POS + k] * F.S);
        wid.push_back((R.sizeWidth ? r[UVR_SIZE] : 1.0f) * R.widthMul * F.S);
        for (int k = 0; k < 3; k++) col.push_back(floorf(uvfx_srgb2lin(r[UVR_COL + k]) * 255.0f + 0.5f) / 255.0f);
        col.push_back(r[UVR_COL + 3]);
    }
    if (attach && !P.empty()) {
        for (int k = 0; k < 3; k++) pts.push_back(attach[k]);
        wid.push_back(wid.back());
        for (int k = 0; k < 4; k++) col.push_back(col[col.size() - 4]);
    }
    const int n = (int)wid.size();
    if (n < 2) return;
    static const float I16[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    fx_setmat(p, "uModel", I16);
    fx_set1f(p, "uIsMesh", 0.0f);
    fx_set1f(p, "uUseVC", 1.0f);
    fx_set3f(p, "uQuadN", 0.0f, 0.0f, 0.0f);        /* the module generates no lighting data */
    glBegin(GL_TRIANGLE_STRIP);
    for (int k = 0; k < n; k++) {
        const int a = k > 0 ? k - 1 : 0, b = k + 1 < n ? k + 1 : n - 1;
        const float tg[3] = { pts[b * 3] - pts[a * 3], pts[b * 3 + 1] - pts[a * 3 + 1], pts[b * 3 + 2] - pts[a * 3 + 2] };
        float s[3] = { -(tg[1] * g_uvF[2] - tg[2] * g_uvF[1]), -(tg[2] * g_uvF[0] - tg[0] * g_uvF[2]),
                       -(tg[0] * g_uvF[1] - tg[1] * g_uvF[0]) };
        const float l = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
        if (l > 1e-9f) for (int c = 0; c < 3; c++) s[c] /= l;
        /* along the trail, 0 at its newest end: Unity's curves read that way round */
        const float along = n > 1 ? (float)(n - 1 - k) / (float)(n - 1) : 0.0f;
        const int ci = (int)(along * 32.0f + 0.5f);
        const float hw = 0.5f * wid[k] * R.width[ci];
        float c4[4];
        for (int c = 0; c < 4; c++) c4[c] = (R.inherit ? col[k * 4 + c] : 1.0f) * R.color[ci][c];
        const float u = R.texMode == 0 ? (n > 1 ? (float)k / (float)(n - 1) : 0.0f) : (float)k;
        glColor4f(c4[0], c4[1], c4[2], c4[3]);
        glTexCoord2f(u, 1.0f);
        glVertex3f(pts[k * 3] + s[0] * hw, pts[k * 3 + 1] + s[1] * hw, pts[k * 3 + 2] + s[2] * hw);
        glTexCoord2f(u, 0.0f);
        glVertex3f(pts[k * 3] - s[0] * hw, pts[k * 3 + 1] - s[1] * hw, pts[k * 3 + 2] - s[2] * hw);
    }
    glEnd();
    glColor4f(1, 1, 1, 1);
}

/* THE TRAIL RENDERER, rebuilt as Unity bakes it (checked against TrailRenderer.BakeMesh):
   the newest end first, a vertex pair per point, half the curve's width either side and
   square to the view; U runs 0 at the newest end to 1 at the oldest (Stretch) or counts
   metres over the texture scale (Tile); colour and width are the curves at the point's
   place along the line. A point is dropped once it is older than the component's time. */
static void uvfx_draw_line(GLuint p, const UvfxLineSpec& L, const UvfxTrail& tr, float cpm, float now)
{
    std::vector<const UvfxTrailPt*> q;
    for (int i = (int)tr.path.size() - 1; i >= 0; i--) {
        if (i < (int)tr.path.size() - 1 && (now - tr.path[i].t) / 15.0f > L.time) break;
        q.push_back(&tr.path[i]);
    }
    const int n = (int)q.size();
    if (n < 2) return;
    std::vector<float> d(n, 0.0f);
    for (int k = 1; k < n; k++) {
        const float dx = q[k]->p[0] - q[k - 1]->p[0], dy = q[k]->p[1] - q[k - 1]->p[1], dz = q[k]->p[2] - q[k - 1]->p[2];
        d[k] = d[k - 1] + sqrtf(dx * dx + dy * dy + dz * dz);
    }
    const float total = d[n - 1] > 1e-6f ? d[n - 1] : 1.0f;
    static const float I16[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    fx_setmat(p, "uModel", I16);
    fx_set1f(p, "uIsMesh", 0.0f);
    fx_set1f(p, "uUseVC", 1.0f);
    fx_set3f(p, "uQuadN", 0.0f, 0.0f, 0.0f);
    glBegin(GL_TRIANGLE_STRIP);
    for (int k = 0; k < n; k++) {
        const int a = k > 0 ? k - 1 : 0, b = k + 1 < n ? k + 1 : n - 1;
        const float tg[3] = { q[b]->p[0] - q[a]->p[0], q[b]->p[1] - q[a]->p[1], q[b]->p[2] - q[a]->p[2] };
        float s[3] = { -(tg[1] * g_uvF[2] - tg[2] * g_uvF[1]), -(tg[2] * g_uvF[0] - tg[0] * g_uvF[2]),
                       -(tg[0] * g_uvF[1] - tg[1] * g_uvF[0]) };
        const float l = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
        if (l > 1e-9f) for (int c = 0; c < 3; c++) s[c] /= l;
        const float along = d[k] / total;
        const int ci = (int)(along * 32.0f + 0.5f);
        const float hw = 0.5f * L.width[ci] * L.widthMul * cpm;
        float c4[4];
        for (int c = 0; c < 3; c++) c4[c] = floorf(uvfx_srgb2lin(L.color[ci][c]) * 255.0f + 0.5f) / 255.0f;
        c4[3] = L.color[ci][3];
        const float u = L.texMode == 1 ? d[k] / cpm / (L.texScale[0] > 1e-6f ? L.texScale[0] : 1.0f) : along;
        glColor4f(c4[0], c4[1], c4[2], c4[3]);
        glTexCoord2f(u, 1.0f);
        glVertex3f(q[k]->p[0] + s[0] * hw, q[k]->p[1] + s[1] * hw, q[k]->p[2] + s[2] * hw);
        glTexCoord2f(u, 0.0f);
        glVertex3f(q[k]->p[0] - s[0] * hw, q[k]->p[1] - s[1] * hw, q[k]->p[2] - s[2] * hw);
    }
    glEnd();
    glColor4f(1, 1, 1, 1);
}

/* THE WHOLE PASS, called by the chain right after its resolve has written the finished
   tactical view into g_fxNativeA and before the CRT and the sidebar. */
static void uvfx_after_resolve_run(void);
static double g_uvPassMs = 0.0;
/* THE WARM-UP. The first explosion of a mission used to cost two frames of 24 and 105
   ms, measured: the programs compiling, then the driver's first real use of them and of
   100 MB of textures. So on the first frame the chain runs with the feature available,
   one explosion is drawn from the test stage into the layer and thrown away (never
   swapped in), and the stall lands at the start of the mission instead of in a fight. */
static int g_uvWarmed = 0, g_uvWarmRun = 0;
static int    g_uvTime = -1;       /* OPENCNC3D_UVFX_TIME set: finish the GPU around the pass and time it */
static void uvfx_after_resolve(void)
{
    if (g_uvTime < 0) g_uvTime = getenv("OPENCNC3D_UVFX_TIME") ? 1 : 0;
    if (!g_uvWarmed && !g_uvStage.on && uvfx_available() && g_uvHaveCam && g_fxScene.depth) {
        g_uvWarmed = 1;
        const Uint64 t0 = SDL_GetPerformanceCounter();
        g_uvWarmRun = 1;
        const int keepPack = g_uvStage.pack;
        for (size_t pi = 0; pi < g_uvPacks.size(); pi++) {      /* each pack's textures */
            g_uvStage.on = 1; g_uvStage.seed = 0; g_uvStage.t = 0.3f; g_uvStage.pack = (int)pi;
            uvfx_after_resolve();
        }
        g_uvStage.on = 0;
        g_uvStage.pack = keepPack;
        g_uvWarmRun = 0;
        glFinish();
        fprintf(stderr, "unityvfx: warmed up in %.0f ms (programs, textures, targets)\n",
                (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency());
    }
    if (g_uvTime && !g_uvStage.on) {
        glFinish();
        const Uint64 t0 = SDL_GetPerformanceCounter();
        uvfx_after_resolve_run();
        glFinish();
        g_uvPassMs = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
        g_uvSerial++;
        return;
    }
    if (!g_uvStage.on) { uvfx_after_resolve_run(); g_uvSerial++; return; }
    /* THE STAGE: the reference camera, in the same numbers the bake used, mapped into
       the game the way every particle is (z negated, metres to cells), around a point
       well inside the map so nothing clips it. */
    std::vector<UvfxInst> keep;
    keep.swap(g_uvInst);
    float mv[16], pr[16], vp[16], ip[16];
    memcpy(mv, g_uvMV, sizeof mv); memcpy(pr, g_uvP, sizeof pr);
    memcpy(vp, g_uvVP, sizeof vp); memcpy(ip, g_uvInvP, sizeof ip);
    const float S = g_fx.uvfx_scale > 0.01f ? g_fx.uvfx_scale : 0.33f;
    const float O[3] = { 32.0f, 0.0f, 32.0f };
    const float d2r = 3.14159265f / 180.0f, pitch = 48.7f * d2r, dist = g_uvStage.dist;
    /* Unity: forward (0,-sin p, cos p), up (0, cos p, sin p); z negated for the game */
    const float Fw[3] = { 0.0f, -sinf(pitch), -cosf(pitch) };
    const float Up[3] = { 0.0f, cosf(pitch), -sinf(pitch) };
    const float Rt[3] = { 1.0f, 0.0f, 0.0f };
    const float E[3] = { O[0] - Fw[0] * dist * S, O[1] - Fw[1] * dist * S, O[2] - Fw[2] * dist * S };
    float m[16] = { Rt[0], Up[0], -Fw[0], 0,  Rt[1], Up[1], -Fw[1], 0,  Rt[2], Up[2], -Fw[2], 0,  0, 0, 0, 1 };
    m[12] = -(Rt[0] * E[0] + Rt[1] * E[1] + Rt[2] * E[2]);
    m[13] = -(Up[0] * E[0] + Up[1] * E[1] + Up[2] * E[2]);
    m[14] = (Fw[0] * E[0] + Fw[1] * E[1] + Fw[2] * E[2]);
    /* Unity's near plane is 0.3 m: a stage close enough to see a muzzle flash needs it */
    const float n = (dist < 10.0f ? 0.05f : 0.5f) * S, f = 1000.0f * S, t = 1.0f / tanf(25.0f * d2r);
    const float asp = (float)g_fxW / (float)g_fxH;
    float P[16] = { t / asp, 0, 0, 0,  0, t, 0, 0,  0, 0, (f + n) / (n - f), -1,  0, 0, 2 * f * n / (n - f), 0 };
    memcpy(g_uvMV, m, sizeof m); memcpy(g_uvP, P, sizeof P);
    fx_mat_mul(g_uvP, g_uvMV, g_uvVP);
    fx_mat_invert(g_uvP, g_uvInvP);
    for (int i = 0; i < 3; i++) { g_uvR[i] = Rt[i]; g_uvU[i] = Up[i]; g_uvF[i] = Fw[i]; g_uvCam[i] = E[i]; }
    const int spk = g_uvStage.pack >= 0 && g_uvStage.pack < (int)g_uvPacks.size() ? g_uvStage.pack : 0;
    if (g_uvPacks[spk].trail) {
        /* A TRAIL ON THE STAGE: the bake's own flight (along Unity's +z at its speed, from
           the stage's origin, stopping where the bake stopped), so the frame at age T is
           the one Unity rendered at T */
        const UvfxPack& pk = g_uvPacks[spk];
        UvfxTrail tr;
        tr.pack = spk; tr.k = 1.0f; tr.bullet = -1; tr.btype = -1; tr.flying = 0;
        tr.seed = g_uvStage.seed % (int)(pk.lives.empty() ? 1 : pk.lives.size());
        const float spd = sqrtf(pk.move[0] * pk.move[0] + pk.move[1] * pk.move[1] + pk.move[2] * pk.move[2]);
        const float dg[3] = { spd > 0 ? pk.move[0] / spd : 0, spd > 0 ? pk.move[1] / spd : 0, spd > 0 ? -pk.move[2] / spd : -1 };
        const float T = g_uvStage.t, tm = T < pk.moveStop ? T : pk.moveStop;
        const float now = engine_time();
        for (int i = 0; ; i++) {
            const float ti = (float)i / (float)pk.fps < tm ? (float)i / (float)pk.fps : tm;
            UvfxTrailPt q;
            q.t = now - (T - ti) * 15.0f;
            q.D = spd * ti * S;
            for (int k = 0; k < 3; k++) q.p[k] = O[k] + dg[k] * q.D;
            tr.path.push_back(q);
            if (ti >= tm) break;
        }
        tr.next.assign(pk.nsys, 0);
        tr.cyc.assign(pk.nsys, 0.0f);
        std::vector<UvfxTrail> keepT;
        keepT.swap(g_uvTrails);
        g_uvTrails.push_back(tr);
        g_uvStageTrail = 1;
        uvfx_after_resolve_run();
        g_uvStageTrail = 0;
        g_uvTrails.swap(keepT);
        g_uvInst.swap(keep);
        memcpy(g_uvMV, mv, sizeof mv); memcpy(g_uvP, pr, sizeof pr);
        memcpy(g_uvVP, vp, sizeof vp); memcpy(g_uvInvP, ip, sizeof ip);
        uvfx_capture_basis();
        return;
    }
    UvfxInst in;
    in.animId = -1; in.pack = spk; in.lift = 0.0f; in.anyShroud = 1; in.surfaced = 1;
    in.seed = g_uvStage.seed % (int)(g_uvPacks[spk].seed.empty() ? 1 : g_uvPacks[spk].seed.size());
    in.yaw = 0.0f; in.x = O[0]; in.y = O[1]; in.z = O[2];
    in.follow = -1; in.sock = -1; in.k = 1.0f;
    if (g_uvStage.aim) {
        /* a gun effect on the stage fires as the bake saw it: along Unity's +z */
        const float f[3] = { 0.0f, 0.0f, -1.0f }, u[3] = { 0.0f, 1.0f, 0.0f };
        uvfx_basis_aim(f, u, in.A);
        in.y = O[1] + g_uvStage.lift * S;
    } else {
        uvfx_basis_yaw(0.0f, in.A);
    }
    in.spawnT = engine_time() - g_uvStage.t * 15.0f;
    g_uvInst.push_back(in);
    uvfx_after_resolve_run();
    g_uvInst.swap(keep);
    memcpy(g_uvMV, mv, sizeof mv); memcpy(g_uvP, pr, sizeof pr);
    memcpy(g_uvVP, vp, sizeof vp); memcpy(g_uvInvP, ip, sizeof ip);
    uvfx_capture_basis();
}

static void uvfx_after_resolve_run(void)
{
    g_uvLastLive = 0;
    if (!g_uvStage.on && !g_uvFiresTaken) uvfx_muzzle_fires();
    g_uvFiresTaken = 0;
    uvfx_launch_prune();
    if (g_uvInst.empty() && g_uvTrails.empty()) return;
    const float now = engine_time();
    /* retire finished explosions */
    {
        size_t w = 0;
        for (size_t i = 0; i < g_uvInst.size(); i++)
            if (g_uvStage.on || ((now - g_uvInst[i].spawnT) / 15.0f
                                     < (float)(g_uvPacks[g_uvInst[i].pack].frames - 1) / (float)g_uvPacks[g_uvInst[i].pack].fps
                                 && now >= g_uvInst[i].spawnT - 1.0f))
                g_uvInst[w++] = g_uvInst[i];
        g_uvInst.resize(w);
    }
    if ((g_uvInst.empty() && g_uvTrails.empty())
        || !(uvfx_available() || (g_uvStage.on && g_uv.loaded && fx_gl_ready))
        || !g_uvHaveCam || !g_fxScene.depth) return;
    if (!uvfx_programs()) return;
    uvfx_upload();
    const int lw = g_fxScene.w, lh = g_fxScene.h;
    if (!fx_rt_init(&g_uvLayer, lw, lh, FX_RT_COLOUR | FX_RT_FLOAT, GL_LINEAR) || !g_uvLayer.hdr) return;
    if (!fx_rt_init(&g_uvDist, lw, lh, FX_RT_COLOUR | FX_RT_FLOAT, GL_LINEAR)) return;
    if (!fx_rt_init(&g_uvOut, g_fxW, g_fxH, FX_RT_COLOUR | FX_RT_DEPTH, GL_LINEAR)) return;

    const float S = g_fx.uvfx_scale > 0.01f ? g_fx.uvfx_scale : 0.33f;
    if (!g_uvStage.on || g_uvStageTrail) uvfx_trails_step(S);

    /* what to draw, in Unity's order: every (explosion, system) pair sorted back to
       front on camera distance plus the renderer's sorting fudge */
    /* ON WHAT WAS HIT, NOT INSIDE IT. The engine puts a hit where the shot lands, which
       for a turret, a tank or a boat is the middle of the thing hit; a Unity explosion
       started there at ground level is inside that model and the model hides most of it
       (measured in GDI 1: the gunboat's missiles on the shore turret showed nothing but
       the light, and the same explosion at three times the size showed around it). The
       cartridge's sprites fly out of it; these do not. So the first time an explosion is
       drawn, the scene's own depth at its ground point is read back, and if something
       stands in front of that point, the explosion starts on that thing's visible
       surface, a tenth of a cell toward the camera. On open ground the depth IS the
       ground and nothing moves. One pixel read, once per explosion. */
    if (!g_uvStage.on) {
        float ivp[16];
        bool haveInv = fx_mat_invert(g_uvVP, ivp) != 0;
        for (size_t i = 0; haveInv && i < g_uvInst.size(); i++) {
            UvfxInst& in = g_uvInst[i];
            if (in.surfaced) continue;
            in.surfaced = 1;
            const float p[4] = { in.x, in.y, in.z, 1.0f };
            float c[4];
            for (int r = 0; r < 4; r++)
                c[r] = g_uvVP[r] * p[0] + g_uvVP[4 + r] * p[1] + g_uvVP[8 + r] * p[2] + g_uvVP[12 + r];
            if (c[3] <= 1e-6f) continue;
            const float nx = c[0] / c[3], ny = c[1] / c[3], nz = c[2] / c[3];
            if (nx < -1 || nx > 1 || ny < -1 || ny > 1) continue;
            const int px = (int)((nx * 0.5f + 0.5f) * (float)g_fxScene.w);
            const int py = (int)((ny * 0.5f + 0.5f) * (float)g_fxScene.h);
            float d = 1.0f;
            fx_glBindFramebuffer(GL_FRAMEBUFFER, g_fxScene.fbo);
            glReadPixels(px, py, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &d);
            const float dGround = nz * 0.5f + 0.5f;
            if (d >= dGround - 1e-5f) continue;              /* nothing in front of it */
            const float q[4] = { nx, ny, d * 2.0f - 1.0f, 1.0f };
            float w[4];
            for (int r = 0; r < 4; r++)
                w[r] = ivp[r] * q[0] + ivp[4 + r] * q[1] + ivp[8 + r] * q[2] + ivp[12 + r] * q[3];
            if (fabsf(w[3]) < 1e-9f) continue;
            float sx = w[0] / w[3], sy = w[1] / w[3], sz = w[2] / w[3];
            float tx = g_uvCam[0] - sx, ty = g_uvCam[1] - sy, tz = g_uvCam[2] - sz;
            const float tl = sqrtf(tx * tx + ty * ty + tz * tz);
            if (tl > 1e-6f) { sx += tx / tl * 0.1f; sy += ty / tl * 0.1f; sz += tz / tl * 0.1f; }
            printf("UVFX|surface|id=%d|from=%.2f,%.2f,%.2f|to=%.2f,%.2f,%.2f\n",
                   in.animId, in.x, in.y, in.z, sx, sy, sz);
            in.x = sx; in.y = sy; in.z = sz;
            /* the thing hit gives the height; an Aerial pick's lift is for open ground,
               where it was chosen, and on top of a boat it floats a cell above it */
            in.lift = 0.0f;
        }
    }

    std::vector<UvfxDrawItem> items;
    std::vector<UvfxFrame> frames(g_uvInst.size());
    std::vector<float> times(g_uvInst.size());
    std::vector<std::vector<std::vector<UvfxP> > > state(g_uvInst.size());
    int live = 0;
    for (size_t i = 0; i < g_uvInst.size(); i++) {
        const UvfxInst& in = g_uvInst[i];
        if (!g_uvStage.on && !in.anyShroud && efx_hidden(in.x, in.z)) continue;
        const UvfxPack& pk = g_uvPacks[in.pack];
        UvfxFrame& F = frames[i];
        F.S = S * in.k; memcpy(F.A, in.A, sizeof F.A);
        F.ox = in.x; F.oy = in.y + in.lift * S * in.k; F.oz = in.z;   /* the lift grows with it */
        times[i] = (now - in.spawnT) / 15.0f;
        state[i].resize(pk.sys.size());
        for (size_t s = 0; s < pk.sys.size(); s++) {
            uvfx_state(pk, pk.seed[in.seed], pk.sys[s].index, times[i], state[i][s]);
            if (state[i][s].empty()) continue;
            float cx = 0, cy = 0, cz = 0;
            const UvfxFrame Fs = uvfx_sys_frame(F, pk.sys[s]);
            for (size_t k = 0; k < state[i][s].size(); k++) {
                float q[3];
                uvfx_point(Fs, state[i][s][k].r + UVR_POS, q);
                cx += q[0]; cy += q[1]; cz += q[2];
            }
            const float inv = 1.0f / (float)state[i][s].size();
            const float dx = cx * inv - g_uvCam[0], dy = cy * inv - g_uvCam[1], dz = cz * inv - g_uvCam[2];
            UvfxDrawItem it;
            it.inst = (int)i; it.sys = (int)s; it.trail = 0; it.ribbon = 0; it.line = 0;
            it.c[0] = cx * inv; it.c[1] = cy * inv; it.c[2] = cz * inv;
            it.key = sqrtf(dx * dx + dy * dy + dz * dz) / F.S + pk.sys[s].fudge;   /* metres */
            /* opaque first, as Unity's opaque queue runs before its transparent one */
            if (uvfx_opaque(pk.mat[pk.sys[s].mat])) it.key = 1e9f;
            items.push_back(it);
            live++;
        }
    }
    g_uvLastLive = live;
    /* THE PARTICLE LIGHTS of every live effect, as Unity's Lights module makes them */
    g_uvLights.clear();
    static int noLights = -1;
    if (noLights < 0) noLights = getenv("OPENCNC3D_UVFX_NOLIGHTS") ? 1 : 0;
    for (size_t i = 0; !noLights && i < g_uvInst.size(); i++) {
        const UvfxInst& in = g_uvInst[i];
        const UvfxPack& pk = g_uvPacks[in.pack];
        if (pk.lights.empty() || state[i].empty()) continue;      /* not drawn: hidden */
        for (size_t l = 0; l < pk.lights.size(); l++) {
            const UvfxLightSpec& L = pk.lights[l];
            std::vector<UvfxP> lp;
            uvfx_state(pk, pk.seed[in.seed], L.sys, times[i], lp);
            const UvfxFrame Fl = uvfx_sys_frame(frames[i], L.where);
            for (size_t k = 0; k < lp.size(); k++) {
                const float* r = lp[k].r;
                UvfxLight g;
                uvfx_point(Fl, r + UVR_POS, g.p);
                float kI = L.intensityMul * (L.alphaAffectsIntensity ? r[UVR_COL + 3] : 1.0f);
                for (int c = 0; c < 3; c++)
                    g.col[c] = L.col[c] * kI * (L.useParticleColor ? uvfx_srgb2lin(r[UVR_COL + c]) : 1.0f);
                g.range = L.range * L.rangeMul * (L.sizeAffectsRange ? r[UVR_SIZE] : 1.0f) * Fl.S;
                g.cpm = Fl.S;
                if (g.col[0] + g.col[1] + g.col[2] > 0.0f && g.range > 0.0f) g_uvLights.push_back(g);
            }
        }
    }
    g_uvLightsLast = (int)g_uvLights.size();
    /* THE TRAILS: each life's state now, per recorded system, oldest first */
    std::vector<std::vector<std::vector<UvfxP> > > tstate(g_uvTrails.size());
    std::vector<UvfxFrame> tframes(g_uvTrails.size());
    g_uvTrailParts = 0;
    for (size_t ti = 0; (!g_uvStage.on || g_uvStageTrail) && ti < g_uvTrails.size(); ti++) {
        const UvfxTrail& tr = g_uvTrails[ti];
        const UvfxPack& pk = g_uvPacks[tr.pack];
        tframes[ti] = uvfx_trail_frame(tr, S);
        tstate[ti].resize(pk.nsys);
        for (size_t i = 0; i < tr.parts.size(); i++) {
            UvfxP P;
            if (uvfx_trail_state(pk, tr, tr.parts[i], now, P)) tstate[ti][tr.parts[i].sys].push_back(P);
        }
        for (int s = 0; s < pk.nsys; s++) {
            const std::vector<UvfxP>& L = tstate[ti][s];
            if (L.empty()) continue;
            g_uvTrailParts += (int)L.size();
            float c[3] = { 0, 0, 0 };
            for (size_t k = 0; k < L.size(); k++)
                for (int a = 0; a < 3; a++) c[a] += L[k].r[UVR_POS + a] * tframes[ti].S;
            for (int a = 0; a < 3; a++) c[a] /= (float)L.size();
            if (!g_uvStageTrail && efx_hidden(c[0], c[2])) continue;
            const float dx = c[0] - g_uvCam[0], dy = c[1] - g_uvCam[1], dz = c[2] - g_uvCam[2];
            const float dm = sqrtf(dx * dx + dy * dy + dz * dz) / tframes[ti].S;
            for (size_t q = 0; q < pk.sys.size(); q++) {
                if (pk.sys[q].index != s) continue;
                UvfxDrawItem it;
                it.inst = (int)ti; it.sys = (int)q; it.trail = 1; it.ribbon = 0; it.line = 0;
                for (int a = 0; a < 3; a++) it.c[a] = c[a];
                it.key = dm + pk.sys[q].fudge;
                items.push_back(it);
            }
            for (size_t q = 0; q < pk.ribbons.size(); q++) {
                if (pk.ribbons[q].sys != s) continue;
                UvfxDrawItem it;
                it.inst = (int)ti; it.sys = (int)q; it.trail = 1; it.ribbon = 1; it.line = 0;
                for (int a = 0; a < 3; a++) it.c[a] = c[a];
                it.key = dm;
                items.push_back(it);
            }
        }
    }
    /* the trails' trail renderers: one strip each, placed at their newest point */
    for (size_t ti = 0; (!g_uvStage.on || g_uvStageTrail) && ti < g_uvTrails.size(); ti++) {
        const UvfxTrail& tr = g_uvTrails[ti];
        const UvfxPack& pk = g_uvPacks[tr.pack];
        if (tr.path.size() < 2) continue;
        const float* h = tr.path.back().p;
        if (!g_uvStageTrail && efx_hidden(h[0], h[2])) continue;
        for (size_t q = 0; q < pk.lines.size(); q++) {
            UvfxDrawItem it;
            it.inst = (int)ti; it.sys = (int)q; it.trail = 1; it.ribbon = 0; it.line = 1;
            for (int a = 0; a < 3; a++) it.c[a] = h[a];
            const float dx = h[0] - g_uvCam[0], dy = h[1] - g_uvCam[1], dz = h[2] - g_uvCam[2];
            it.key = sqrtf(dx * dx + dy * dy + dz * dz) / (S * tr.k);
            items.push_back(it);
        }
    }
    if (items.empty() && !g_uvStage.on) return;     /* the stage composites its black regardless */
    for (size_t a = 1; a < items.size(); a++)          /* stable insertion sort, far first */
        for (size_t b = a; b > 0 && items[b].key > items[b - 1].key; b--) {
            UvfxDrawItem t = items[b]; items[b] = items[b - 1]; items[b - 1] = t;
        }

    /* ---- the layer: (0,0,0,1) is "nothing here, everything behind survives" ---- */
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_BLEND);
    glFrontFace(GL_CW);
    fx_rt_bind(&g_uvDist);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    fx_rt_bind(&g_uvLayer);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(0, 0, lw, lh);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_fxScene.depth);
    for (size_t k = 0; k < items.size(); k++) {
        const UvfxDrawItem& it = items[k];
        const UvfxPack& pk = g_uvPacks[it.trail ? g_uvTrails[it.inst].pack : g_uvInst[it.inst].pack];
        static UvfxSys noSys;                                  /* a ribbon-only pack has none */
        const UvfxSys& S2 = (it.ribbon || it.line) ? noSys : pk.sys[it.sys];
        const UvfxMat& m = pk.mat[it.line ? pk.lines[it.sys].mat : it.ribbon ? pk.ribbons[it.sys].mat : S2.mat];
        const bool dist = m.kind == UVK_DISTORT;
        fx_rt_bind(dist ? &g_uvDist : &g_uvLayer);
        glViewport(0, 0, lw, lh);
        const GLuint p = g_uvProg[m.kind];
        fx_glUseProgram(p);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_fxScene.depth);
        const UvfxFrame Fs = it.trail ? tframes[it.inst] : uvfx_sys_frame(frames[it.inst], S2);
        /* a trail's shader clock is its age */
        const float tclock = it.trail ? (now - g_uvTrails[it.inst].path[0].t) / 15.0f : times[it.inst];
        uvfx_set_common(p, Fs, tclock);
        {
            /* the eight lights nearest this system, as URP picks per renderer */
            int idx[8], n = 0;
            float dd[8];
            for (size_t l = 0; l < g_uvLights.size(); l++) {
                const UvfxLight& g = g_uvLights[l];
                const float dx = g.p[0] - it.c[0], dy = g.p[1] - it.c[1], dz = g.p[2] - it.c[2];
                const float d = dx * dx + dy * dy + dz * dz;
                if (d > g.range * g.range * 4.0f) continue;
                int j = n < 8 ? n++ : 7;
                if (j == 7 && n == 8 && d >= dd[7]) continue;
                dd[j] = d; idx[j] = (int)l;
                for (; j > 0 && dd[j] < dd[j - 1]; j--) {
                    float td = dd[j]; dd[j] = dd[j - 1]; dd[j - 1] = td;
                    int ti = idx[j]; idx[j] = idx[j - 1]; idx[j - 1] = ti;
                }
            }
            float lp[32], lc[32];
            for (int k = 0; k < n; k++) {
                const UvfxLight& g = g_uvLights[idx[k]];
                lp[k * 4] = g.p[0]; lp[k * 4 + 1] = g.p[1]; lp[k * 4 + 2] = g.p[2]; lp[k * 4 + 3] = g.range;
                lc[k * 4] = g.col[0]; lc[k * 4 + 1] = g.col[1]; lc[k * 4 + 2] = g.col[2]; lc[k * 4 + 3] = g.cpm;
            }
            fx_set1i(p, "uNL", n);
            if (n) { fx_set4fv(p, "uLP", n, lp); fx_set4fv(p, "uLC", n, lc); }
        }
        uvfx_set_material(p, m);
        uvfx_state_for(m);
        fx_set1f(p, "uAlphaMul", it.trail ? g_uvTrails[it.inst].op : 1.0f);
        if (it.trail) {
            const UvfxTrail& tr = g_uvTrails[it.inst];
            if (it.line) {
                uvfx_draw_line(p, pk.lines[it.sys], tr, Fs.S, now);
            } else if (it.ribbon) {
                const UvfxRibbonSpec& R = pk.ribbons[it.sys];
                uvfx_draw_ribbon(p, R, tstate[it.inst][R.sys], Fs, R.attach ? tr.path.back().p : NULL);
            } else {
                UvfxInst ti;
                memset(&ti, 0, sizeof ti);
                ti.pack = tr.pack;
                uvfx_draw_system(ti, S2, Fs, tclock, tstate[it.inst][S2.index]);
            }
        } else
            uvfx_draw_system(g_uvInst[it.inst], S2, Fs, times[it.inst], state[it.inst][it.sys]);
    }
    fx_glUseProgram(0);
    for (int u = 3; u >= 0; u--) { glActiveTexture(GL_TEXTURE0 + u); glBindTexture(GL_TEXTURE_2D, 0); }
    glFrontFace(GL_CCW);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    /* ---- URP's Gaussian bloom on the effect's own light ---- */
    int bloomOn = 0;
    {
        const int tw = g_fxW >> 1 > 0 ? g_fxW >> 1 : 1, th = g_fxH >> 1 > 0 ? g_fxH >> 1 : 1;
        int mips = (int)floorf(log2f((float)(tw > th ? tw : th)) - 1.0f);
        if (mips < 1) mips = 1;
        if (mips > 6) mips = 6;
        int sw[8], sh[8];
        sw[0] = tw; sh[0] = th;
        bool ok = true;
        for (int i = 1; i < mips; i++) { sw[i] = sw[i - 1] >> 1 > 0 ? sw[i - 1] >> 1 : 1; sh[i] = sh[i - 1] >> 1 > 0 ? sh[i - 1] >> 1 : 1; }
        for (int i = 0; i < mips && ok; i++)
            ok = fx_rt_init(&g_uvDown[i], sw[i], sh[i], FX_RT_COLOUR | FX_RT_FLOAT, GL_LINEAR)
              && fx_rt_init(&g_uvUp[i], sw[i], sh[i], FX_RT_COLOUR | FX_RT_FLOAT, GL_LINEAR);
        if (ok) {
            glActiveTexture(GL_TEXTURE0);
            glEnable(GL_TEXTURE_2D);
            /* prefilter */
            fx_glUseProgram(g_uvPPre);
            fx_set1i(g_uvPPre, "uTex", 0);
            int taps = (int)((float)lw / (float)tw + 0.5f);
            if (taps < 1) taps = 1; if (taps > 8) taps = 8;
            fx_set1i(g_uvPPre, "uTaps", taps);
            fx_set2f(g_uvPPre, "uDstTexel", 1.0f / (float)tw, 1.0f / (float)th);
            fx_set1f(g_uvPPre, "uThreshold", 1.0f);
            fx_set1f(g_uvPPre, "uKnee", 0.5f);
            fx_set1f(g_uvPPre, "uClamp", 65472.0f);
            glBindTexture(GL_TEXTURE_2D, g_uvLayer.tex);
            fx_rt_bind(&g_uvDown[0]); glViewport(0, 0, sw[0], sh[0]);
            fx_fullscreen_quad();
            /* down */
            for (int i = 1; i < mips; i++) {
                fx_glUseProgram(g_uvPBlurH);
                fx_set1i(g_uvPBlurH, "uTex", 0);
                fx_set2f(g_uvPBlurH, "uSrcTexel", 1.0f / (float)sw[i - 1], 1.0f / (float)sh[i - 1]);
                glBindTexture(GL_TEXTURE_2D, g_uvDown[i - 1].tex);
                fx_rt_bind(&g_uvUp[i]); glViewport(0, 0, sw[i], sh[i]);
                fx_fullscreen_quad();
                fx_glUseProgram(g_uvPBlurV);
                fx_set1i(g_uvPBlurV, "uTex", 0);
                fx_set2f(g_uvPBlurV, "uSrcTexel", 1.0f / (float)sw[i], 1.0f / (float)sh[i]);
                glBindTexture(GL_TEXTURE_2D, g_uvUp[i].tex);
                fx_rt_bind(&g_uvDown[i]); glViewport(0, 0, sw[i], sh[i]);
                fx_fullscreen_quad();
            }
            /* up */
            fx_glUseProgram(g_uvPUp);
            fx_set1i(g_uvPUp, "uTex", 0);
            fx_set1i(g_uvPUp, "uLow", 1);
            fx_set1f(g_uvPUp, "uScatter", g_fx.uvfx_scatter);
            for (int i = mips - 2; i >= 0; i--) {
                glActiveTexture(GL_TEXTURE1);
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, (i == mips - 2) ? g_uvDown[i + 1].tex : g_uvUp[i + 1].tex);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, g_uvDown[i].tex);
                fx_rt_bind(&g_uvUp[i]); glViewport(0, 0, sw[i], sh[i]);
                fx_fullscreen_quad();
            }
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
            glActiveTexture(GL_TEXTURE0);
            fx_glUseProgram(0);
            bloomOn = 1;
        }
        /* ---- composite over the finished view, into g_uvOut, then swap it in ---- */
        const GLuint bloomTex = !bloomOn ? 0 : (mips == 1 ? g_uvDown[0].tex : g_uvUp[0].tex);
        fx_glUseProgram(g_uvPComp);
        fx_set1i(g_uvPComp, "uTex", 0);
        fx_set1i(g_uvPComp, "uLayer", 1);
        fx_set1i(g_uvPComp, "uBloom", 2);
        fx_set1i(g_uvPComp, "uDist", 3);
        fx_set1i(g_uvPComp, "uBloomOn", bloomOn);
        fx_set1i(g_uvPComp, "uBlackBg", g_uvStage.on ? 1 : 0);
        const FxObjectGrade grade=fx_effect_grade(&g_fx);
        const float grade0[4]={grade.bright,grade.contrast,grade.exposure,grade.gamma};
        const float grade1[4]={grade.sat,grade.warmth,grade.tint,
            g_fx.enabled && fx_object_grade_active(grade) ? 1.0f : 0.0f};
        fx_set4fv(g_uvPComp,"uObjectGrade0",1,grade0);
        fx_set4fv(g_uvPComp,"uObjectGrade1",1,grade1);
        fx_set3f(g_uvPComp,"uObjectGradeRGB",grade.red,grade.green,grade.blue);
        fx_set2f(g_uvPComp, "uDstTexel", 1.0f / (float)g_fxW, 1.0f / (float)g_fxH);
        int tap = (int)((float)lw / (float)g_fxW + 0.5f);
        if (tap < 1) tap = 1; if (tap > 4) tap = 4;
        fx_set1i(g_uvPComp, "uTaps", tap);
        const GLuint tx[4] = { g_fxNativeA.tex, g_uvLayer.tex, bloomTex, g_uvDist.tex };
        for (int u = 3; u >= 0; u--) {
            glActiveTexture(GL_TEXTURE0 + u);
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, tx[u]);
        }
        fx_rt_bind(&g_uvOut); glViewport(0, 0, g_fxW, g_fxH);
        fx_fullscreen_quad();
        fx_glUseProgram(0);
        for (int u = 3; u >= 1; u--) {
            glActiveTexture(GL_TEXTURE0 + u); glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glPopAttrib();
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    /* THE SWAP. The composite could not write where it reads, so it wrote a second
       target of the same size and format (colour and depth, as the HUD needs), and that
       target now becomes the one the rest of the frame draws into. */
    if (g_uvWarmRun) return;                         /* drawn, and thrown away */
    FxRT tmp = g_fxNativeA; g_fxNativeA = g_uvOut; g_uvOut = tmp;
    g_uvDrawnFrames++;
}

/* For the harness: how many explosions are live and how many systems drew. */
static void uvfx_dump(void)
{
    printf("UVFX|state|loaded=%d|packs=%d|available=%d|instances=%d|systems_drawn=%d|spawned=%d|frames_drawn=%d|pass_ms=%.3f"
           "|fires=%d|muzzles=%d|nosocket=%d|sockets=%d|lights=%d|trails=%d|trail_live=%d|trail_parts=%d"
           "|puffs_taken=%d\n",
           g_uv.loaded, (int)g_uvPacks.size(), uvfx_available() ? 1 : 0, (int)g_uvInst.size(), g_uvLastLive,
           g_uvSpawned, g_uvDrawnFrames, g_uvPassMs, g_efxFiresSeen, g_uvMuzzleSpawned, g_uvFiresNoSocket,
           (int)g_uvSock.size(), g_uvLightsLast, g_uvTrailsSpawned, (int)g_uvTrails.size(), g_uvTrailParts,
           g_uvPuffsTaken);
    /* EACH LIVE TRAIL'S HEIGHT above the ground under it, in cells: its head (where the
       projectile is) and the lowest and highest of its placed particles, so a trail that
       stands off its projectile or climbs away from it is a number and not a judgement. */
    const float now = engine_time();
    for (size_t i = 0; i < g_uvTrails.size(); i++) {
        const UvfxTrail& tr = g_uvTrails[i];
        if (tr.path.empty()) continue;
        const UvfxTrailPt& h = tr.path.back();
        const UvfxPack& pk = g_uvPacks[tr.pack];
        const float cpm = (g_fx.uvfx_scale > 0.01f ? g_fx.uvfx_scale : 0.33f) * tr.k;
        float lo = 1e9f, hi = -1e9f;
        int n = 0;
        for (size_t j = 0; j < tr.parts.size(); j++) {
            UvfxP P;
            if (!uvfx_trail_state(pk, tr, tr.parts[j], now, P)) continue;
            const float x = P.r[UVR_POS] * cpm, y = P.r[UVR_POS + 1] * cpm, z = P.r[UVR_POS + 2] * cpm;
            const float a = y - efx_ground(x, z);
            if (a < lo) lo = a;
            if (a > hi) hi = a;
            n++;
        }
        /* and where its head and its round are across the ground, and the newest particle */
        float bx = -1.0f, bz = -1.0f;
        for (size_t b = 0; b < g_efxBullets.size(); b++)
            if (g_efxBullets[b].id == tr.bullet && g_efxBullets[b].type == tr.btype) { bx = g_efxBullets[b].wx; bz = g_efxBullets[b].wz; }
        float nd = -1.0f;
        if (!tr.parts.empty()) {
            const UvfxTrailP& np = tr.parts.back();
            nd = sqrtf((np.o[0] - h.p[0]) * (np.o[0] - h.p[0]) + (np.o[2] - h.p[2]) * (np.o[2] - h.p[2]));
        }
        printf("UVFX|trailh|btype=%d|pack=%s|flying=%d|head=%.3f|parts=%d|lo=%.3f|hi=%.3f|at=%.3f,%.3f|round=%.3f,%.3f"
               "|newest_behind=%.3f\n", tr.btype, pk.name.c_str(), tr.flying, h.p[1] - efx_ground(h.p[0], h.p[2]), n,
               n ? lo : 0.0f, n ? hi : 0.0f, h.p[0], h.p[2], bx, bz, nd);
    }
    fflush(stdout);
}

#endif /* OPENCNC3D_UNITYVFX_MOD_H */
