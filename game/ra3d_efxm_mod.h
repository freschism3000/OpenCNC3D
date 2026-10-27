/* =====================================================================================
 *  ra3d_efxm_mod.h -- Red Alert under Enhanced: its own effects, in particles and in 3D.
 *
 *  Red Alert's anims with a Tiberian Dawn counterpart play that counterpart's approved
 *  effect (ra3d_effects_mod.h maps the numbers). The rest have effects of their own, made
 *  in the Red Alert lab, and this file is how the game plays them:
 *
 *    - THE PARTICLES. Each such effect's emitters are a recipe of its own in
 *      efx_enhanced.h (FBALL1_RA, ELECTRO_RA, H2O_EXP1_RA and the rest; EFX_RA_RECIPE names
 *      them by the anim), generated from the effect's fx.json by
 *      tools/art/remaster/fx/gen_efx_enhanced.py. The anim fires that recipe in place of
 *      its Tiberian Dawn one (g_efxRecipeOf), on its own clock, rebuilt from scratch. An
 *      anim with no Tiberian Dawn counterpart rides an empty one (RA3D_EFXM_CARRIER) so the
 *      particle tick takes it at all. The sheets painted for them (the Tesla arcs, the
 *      water splash, the twinkles) are in the module's efx/efx_ra.pack, read beside
 *      efx_enh.pack in a Red Alert mission only, and worn through the source-sheet table
 *      like every other Enhanced sheet.
 *    - THE FORMS. The crate and power-up pick-ups, the parachutes, the capture-the-flag
 *      flag, the move order's flash, the GPS launch's rocket and the stain a dead soldier
 *      lies in are 3D parts out of <ANIM>.efxm (the atom bomb's format: parts, a frame a
 *      tick of a matrix, visibility, tint, alpha, texture scroll and alpha-test level),
 *      written into the module's efx/ by their generators
 *      (tools/art/remaster/ra/crates3d.py, others3d.py) when the module is staged. Each is
 *      drawn by draw_mesh frame by frame as the lab plays it, where the engine raised the
 *      anim (Ra3dFormDef says how each is placed and how long it lives).
 *    - THE CARDS. Under Enhanced the 1996 card of an anim drawn here stands down
 *      (ra3d_anim_card), with two exceptions kept on purpose: an electrocution keeps its
 *      card, since the engine deletes the soldier or the dog and the card is the only body
 *      there is, the new arcs and glow over it; and a corpse's card stands down only where
 *      a 3D soldier's body lies (the stain is drawn under that body), and stays where none
 *      does (a 1996 soldier's body is in the card).
 *
 *  THE CLOCK of a form. A tick of the file is a tick of the 1996 anim at its own delay
 *  (adata.cpp, the lab's timing), but the engine runs some anims at another pace (a corpse
 *  holds each frame twice as long). So while the anim lives the form's frame follows the
 *  anim's own stage: stage * delay, plus the ticks spent in that stage scaled to the
 *  delay by how long the stage before it lasted. That keeps a form in step with the
 *  engine's loops too (a parachute sways while its stages loop, a flag waves for as long
 *  as it flies). Once the anim is gone a one-shot form plays out its file on the tick
 *  clock (the move flash fades) and a looping one goes with it. Armed and stepped on the
 *  tick (ra3d_efxm_track, beside efx_step), never from the draw.
 *
 *  Red Alert, Enhanced effects only. Classic draws the 1996 anims itself and the 3D view
 *  without Enhanced effects keeps the cards. Tiberian Dawn never reaches any of this: the
 *  hooks are installed for a Red Alert mission (ra3d_efxm_install) and the files are read
 *  only by one.
 *
 *  Fixed function throughout, as the atom bomb's: a matrix, a vertex colour, a texture
 *  offset, an alpha test, two blend modes and backface culling.
 * ===================================================================================== */
#pragma once

/* ------------------------------------------------------------------ the EFXM file */

struct Ra3dEfxmPart {
    char name[17];
    int tex;                        /* into the file's textures */
    unsigned char blend;            /* 0 alpha, 1 additive */
    unsigned char depth;            /* writes depth */
    unsigned char cutout;           /* alpha tested at its level; else blended */
    unsigned char twosided;
    unsigned order;                 /* below 10 drawn before the particles, else after */
    int tri0, ntris;                /* its run of the mesh's triangles */
};
/* one part on one frame */
struct Ra3dEfxmKey {
    unsigned char vis, r, g, b;
    float a, u, v, cut;
};
struct Ra3dEfxmTex { int w, h; std::vector<unsigned char> rgba; };
/* the triangles as the file has them: 3 corners of (x y z u v r g b a), in cells */
struct Ra3dEfxmCorner { float x, y, z, u, v; unsigned char r, g, b, a; };

/* One effect's file, and its mesh in the pack once attached. */
struct Ra3dEfxm {
    std::vector<Ra3dEfxmPart> parts;
    std::vector<Ra3dEfxmKey> keys;      /* frames * parts */
    std::vector<float> mats;            /* frames * parts * 12, in cells */
    std::vector<Ra3dEfxmTex> tex;
    std::vector<Ra3dEfxmCorner> corners;
    int frames = 0;
    int mesh = -1;                      /* in this pack, once attached */
    long partDraws = 0, triDraws = 0;
};

/* Read an EFXM file (layout in tools/art/remaster/ra/fxra.py's write_efxm). False, and
   E untouched, for a missing or damaged one. */
static bool ra3d_efxm_read(const char* path, Ra3dEfxm& E, bool* missing)
{
    if (missing) *missing = false;
    FILE* f = fopen(path, "rb");
    if (!f) { if (missing) *missing = true; return false; }
    bool ok = true;
    char magic[4];
    unsigned hdr[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "EFXM", 4)
        || fread(hdr, 4, 4, f) != 4 || hdr[0] != 1 || hdr[1] < 2 || hdr[1] > 1000
        || hdr[2] > 32 || hdr[3] == 0 || hdr[3] > 64)
        ok = false;
    const int frames = ok ? (int)hdr[1] : 0, ntex = ok ? (int)hdr[2] : 0;
    const int nparts = ok ? (int)hdr[3] : 0;
    std::vector<Ra3dEfxmTex> texs;
    for (int t = 0; ok && t < ntex; t++) {
        char name[8];
        unsigned wh[2];
        if (fread(name, 1, 8, f) != 8 || fread(wh, 4, 2, f) != 2 || !wh[0] || !wh[1]
            || wh[0] > 2048 || wh[1] > 2048) {
            ok = false;
            break;
        }
        Ra3dEfxmTex T;
        T.w = (int)wh[0]; T.h = (int)wh[1];
        T.rgba.resize((size_t)T.w * T.h * 4);
        if (fread(&T.rgba[0], 1, T.rgba.size(), f) != T.rgba.size()) { ok = false; break; }
        texs.push_back(T);
    }
    std::vector<Ra3dEfxmPart> parts;
    std::vector<Ra3dEfxmCorner> corners;
    for (int p = 0; ok && p < nparts; p++) {
        unsigned char rec[32];
        if (fread(rec, 1, 32, f) != 32) { ok = false; break; }
        Ra3dEfxmPart P;
        memset(&P, 0, sizeof P);
        memcpy(P.name, rec, 16);
        unsigned tex, order, ntris;
        memcpy(&tex, rec + 16, 4);
        P.blend = rec[20]; P.depth = rec[21]; P.cutout = rec[22]; P.twosided = rec[23];
        memcpy(&order, rec + 24, 4);
        memcpy(&ntris, rec + 28, 4);
        if ((int)tex >= ntex || ntris == 0 || ntris > 200000) { ok = false; break; }
        P.tex = (int)tex; P.order = order;
        P.tri0 = (int)(corners.size() / 3);
        P.ntris = (int)ntris;
        const size_t n0 = corners.size();
        corners.resize(n0 + (size_t)ntris * 3);
        for (size_t c = 0; c < (size_t)ntris * 3; c++) {
            unsigned char v[24];
            if (fread(v, 1, 24, f) != 24) { ok = false; break; }
            Ra3dEfxmCorner& C = corners[n0 + c];
            memcpy(&C.x, v, 4); memcpy(&C.y, v + 4, 4); memcpy(&C.z, v + 8, 4);
            memcpy(&C.u, v + 12, 4); memcpy(&C.v, v + 16, 4);
            C.r = v[20]; C.g = v[21]; C.b = v[22]; C.a = v[23];
        }
        parts.push_back(P);
    }
    std::vector<float> mats;
    std::vector<Ra3dEfxmKey> keys;
    if (ok) {
        mats.resize((size_t)frames * nparts * 12);
        keys.resize((size_t)frames * nparts);
        for (int fr = 0; ok && fr < frames; fr++)
            for (int p = 0; p < nparts; p++) {
                unsigned char rec[48 + 4 + 16];
                if (fread(rec, 1, sizeof rec, f) != sizeof rec) { ok = false; break; }
                memcpy(&mats[((size_t)fr * nparts + p) * 12], rec, 48);
                Ra3dEfxmKey& K = keys[(size_t)fr * nparts + p];
                K.vis = rec[48]; K.r = rec[49]; K.g = rec[50]; K.b = rec[51];
                memcpy(&K.a, rec + 52, 4); memcpy(&K.u, rec + 56, 4);
                memcpy(&K.v, rec + 60, 4); memcpy(&K.cut, rec + 64, 4);
            }
    }
    fclose(f);
    if (!ok) return false;
    E.tex.swap(texs);
    E.parts.swap(parts);
    E.corners.swap(corners);
    E.mats.swap(mats);
    E.keys.swap(keys);
    E.frames = frames;
    E.mesh = -1;
    return true;
}

/* One texture, mipmapped, repeating. Uploaded bottom row first: the file's rows run top to
   bottom as the lab's PNGs do, and the lab's viewer (three.js) reads a texture with v up
   from its bottom row, so this keeps every v and every scroll exactly as the lab plays
   them. */
static GLuint ra3d_efxm_upload(const Ra3dEfxmTex& T)
{
    GLuint gl = 0;
    glGenTextures(1, &gl);
    glBindTexture(GL_TEXTURE_2D, gl);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    /* a side that is not a power of two (a corpse's stain is the 1996 frame's own shape,
       eight times over) is resampled up to the next one, bilinear, so the chain of halvings
       below holds; texture coordinates are fractions and do not change */
    int w = 1, h = 1, level = 0;
    while (w < T.w) w *= 2;
    while (h < T.h) h *= 2;
    std::vector<unsigned char> cur((size_t)w * h * 4);
    for (int y = 0; y < h; y++) {
        const float sy = ((float)y + 0.5f) * (float)T.h / (float)h - 0.5f;
        const int y0 = sy < 0.0f ? 0 : (int)sy, y1 = y0 + 1 < T.h ? y0 + 1 : y0;
        const float fy = sy < 0.0f ? 0.0f : sy - (float)y0;
        for (int x = 0; x < w; x++) {
            const float sx = ((float)x + 0.5f) * (float)T.w / (float)w - 0.5f;
            const int x0 = sx < 0.0f ? 0 : (int)sx, x1 = x0 + 1 < T.w ? x0 + 1 : x0;
            const float fx = sx < 0.0f ? 0.0f : sx - (float)x0;
            /* (rows bottom first: the file's row h - 1 - y) */
            const unsigned char* r0 = &T.rgba[(size_t)(T.h - 1 - y0) * T.w * 4];
            const unsigned char* r1 = &T.rgba[(size_t)(T.h - 1 - y1) * T.w * 4];
            for (int c = 0; c < 4; c++) {
                const float a = r0[x0 * 4 + c] + (r0[x1 * 4 + c] - (float)r0[x0 * 4 + c]) * fx;
                const float b = r1[x0 * 4 + c] + (r1[x1 * 4 + c] - (float)r1[x0 * 4 + c]) * fx;
                cur[((size_t)y * w + x) * 4 + c] = (unsigned char)(a + (b - a) * fy + 0.5f);
            }
        }
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, &cur[0]);
    while (w > 1 || h > 1) {
        const int nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
        std::vector<unsigned char> next((size_t)nw * nh * 4);
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++) {
                const int sx = x * 2, sy = y * 2;
                const int sx1 = w > 1 ? sx + 1 : sx, sy1 = h > 1 ? sy + 1 : sy;
                for (int c = 0; c < 4; c++)
                    next[((size_t)y * nw + x) * 4 + c] = (unsigned char)((
                        cur[((size_t)sy * w + sx) * 4 + c] + cur[((size_t)sy * w + sx1) * 4 + c] +
                        cur[((size_t)sy1 * w + sx) * 4 + c] + cur[((size_t)sy1 * w + sx1) * 4 + c] + 2) / 4);
            }
        cur.swap(next);
        w = nw; h = nh; level++;
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, &cur[0]);
    }
    return gl;
}

/* Into the pack just loaded, beside the remastered buildings: the textures, and one mesh
   whose parts are the file's parts and whose node animation is the file's matrices. The
   file's matrices place each part in cells (rest pose identity: a part's vertices are its
   own, unposed); the pack's are in mesh units, so a vertex and a translation are divided
   by MODEL_SCALE and the turn and scale are kept. pack_free takes all of it away. */
static void ra3d_efxm_attach(Ra3dEfxm& E, const char* label)
{
    E.mesh = -1;
    if (E.parts.empty()) return;
    const int tex0 = (int)g_pack.tex.size();
    for (size_t t = 0; t < E.tex.size(); t++) {
        PackTex pt;
        pt.w = pt.uw = E.tex[t].w;
        pt.h = pt.uh = E.tex[t].h;
        pt.gl = ra3d_efxm_upload(E.tex[t]);
        pt.gl_gdi = 0;
        for (int e = 0; e < 7; e++) pt.gl_extra[e] = 0;
        g_pack.tex.push_back(pt);
    }
    const float inv = 1.0f / MODEL_SCALE;
    const int np = (int)E.parts.size();
    PackMesh pm;
    memset(pm.name, 0, sizeof pm.name);
    snprintf(pm.name, sizeof pm.name, "efxm:%s", label);
    pm.tris.resize(E.corners.size() / 3);
    unsigned mask = 0;
    for (int p = 0; p < np; p++) {
        const Ra3dEfxmPart& P = E.parts[p];
        /* a cut cloud is the cutout pass's; a soft one the translucent pass's */
        const unsigned char mode = P.cutout ? MODE_CUTOUT : MODE_XLU;
        mask |= 1u << mode;
        for (int t = P.tri0; t < P.tri0 + P.ntris; t++) {
            PackTri& T = pm.tris[(size_t)t];
            T.tex = tex0 + P.tex;
            T.mode = mode;
            T.wrap = 0;                                   /* repeat both ways */
            for (int k = 0; k < 3; k++) {
                const Ra3dEfxmCorner& C = E.corners[(size_t)t * 3 + k];
                PackVert& V = T.v[k];
                V.x = C.x * inv; V.y = C.y * inv; V.z = C.z * inv;
                V.u = C.u; V.v = C.v;
                V.r = C.r; V.g = C.g; V.b = C.b; V.a = C.a;
            }
        }
        PackPart pp;
        pp.tri0 = P.tri0; pp.ntris = P.ntris; pp.role = ROLE_STATIC;
        pp.px = pp.py = pp.pz = 0.0f;
        pm.parts.push_back(pp);
    }
    pm.modeMask = mask;
    pm.animFrames = E.frames;
    pm.animTicksPerFrame = 1;
    pm.animMat = E.mats;
    pm.animVis.resize((size_t)E.frames * np);
    for (size_t i = 0; i < E.keys.size(); i++) {
        float* m = &pm.animMat[i * 12];
        m[3] *= inv; m[7] *= inv; m[11] *= inv;
        pm.animVis[i] = E.keys[i].vis;
    }
    g_pack.mesh.push_back(pm);
    E.mesh = (int)g_pack.mesh.size() - 1;
}

/* Whether part p is drawn on frame f (visible, alpha above nothing). */
static bool ra3d_efxm_on(const Ra3dEfxm& E, int f, int p)
{
    const Ra3dEfxmKey& K = E.keys[(size_t)f * E.parts.size() + p];
    return K.vis && K.a > 0.002f;
}

/* One part at clock t (frames, fractional), at (x, z) and ylift cells over the ground, with
   the part's tracks lerped between the two frames either side, as the lab's viewer plays
   them. */
static void ra3d_efxm_draw_part(Ra3dEfxm& E, int p, float x, float z, float ylift, float t)
{
    const int np = (int)E.parts.size();
    const Ra3dEfxmPart& P = E.parts[p];
    int f0 = (int)floorf(t);
    if (f0 < 0) f0 = 0;
    if (f0 > E.frames - 1) f0 = E.frames - 1;
    const int f1 = f0 + 1 < E.frames ? f0 + 1 : f0;
    float k = t - (float)f0;
    if (k < 0.0f) k = 0.0f;
    if (k > 1.0f) k = 1.0f;
    const Ra3dEfxmKey& K0 = E.keys[(size_t)f0 * np + p];
    const Ra3dEfxmKey& K1 = E.keys[(size_t)f1 * np + p];
    if (!K0.vis) return;
    const float a = K0.a + (K1.a - K0.a) * k;
    if (a <= 0.002f) return;
    g_meshTintR = (unsigned char)(K0.r + ((float)K1.r - K0.r) * k + 0.5f);
    g_meshTintG = (unsigned char)(K0.g + ((float)K1.g - K0.g) * k + 0.5f);
    g_meshTintB = (unsigned char)(K0.b + ((float)K1.b - K0.b) * k + 0.5f);
    g_meshAlphaScale = (unsigned char)(a >= 1.0f ? 255 : (int)(a * 255.0f + 0.5f));
    g_meshUVOffU = K0.u + (K1.u - K0.u) * k;
    g_meshUVOffV = K0.v + (K1.v - K0.v) * k;
    if (P.cutout) {
        /* drawn as the cutout mode draws a model: opaque, its texture's alpha for the test
           alone, at the part's own level; blended only while the part itself fades */
        const float cut = K0.cut + (K1.cut - K0.cut) * k;
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GEQUAL, cut > 0.01f ? cut : 0.01f);
        if (a < 0.999f) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
        else glDisable(GL_BLEND);
    } else {
        glDisable(GL_ALPHA_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, P.blend ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
    }
    glDepthMask(P.depth ? GL_TRUE : GL_FALSE);
    if (P.twosided) glDisable(GL_CULL_FACE);
    else { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); }
    glEnable(GL_TEXTURE_2D);
    draw_mesh(E.mesh, x, z, -1, P.cutout ? MODE_CUTOUT : MODE_XLU, 0, 0.0f, 0, 1.0f,
              ylift, false, WOBBLE_NONE, t, NULL, (size_t)P.tri0, (size_t)(P.tri0 + P.ntris));
    E.partDraws++;
    E.triDraws += P.ntris;
}

/* The parts of one phase in their drawing order: phase 0 is every part ordered below 10,
   drawn before the particles, phase 1 the rest, after them (the lab's renderOrder); in
   each, the cut parts first, writing depth, then the soft ones by their order. */
static int ra3d_efxm_sequence(const Ra3dEfxm& E, int phase, int* seq, int max)
{
    const int np = (int)E.parts.size();
    int n = 0;
    for (int pass = 0; pass < 2; pass++)
        for (unsigned o = 0; o < 256; o++)
            for (int p = 0; p < np && n < max; p++) {
                const Ra3dEfxmPart& P = E.parts[p];
                if (P.order != o || ((P.order < 10) != (phase == 0))) continue;
                if ((pass == 0) != (P.cutout != 0)) continue;
                seq[n++] = p;
            }
    return n;
}

/* The state every draw of parts sets and hands back. */
static void ra3d_efxm_begin(bool noShroud)
{
    glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT
                 | GL_POLYGON_BIT | GL_TEXTURE_BIT);
    glEnable(GL_DEPTH_TEST);
    glFrontFace(GL_CCW);
    g_meshNoShroud = noShroud;
    g_meshTransientColor = true;
}

static void ra3d_efxm_end(void)
{
    g_meshTintR = g_meshTintG = g_meshTintB = 255;
    g_meshAlphaScale = 255;
    g_meshUVOffU = g_meshUVOffV = 0.0f;
    g_meshNoShroud = false;
    g_meshTransientColor = false;
    glPopAttrib();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

/* ------------------------------------------------------------------ the forms */

/* How a form is placed and how long it lives. */
enum {
    RA3D_FORM_ONESHOT,      /* on the ground at the anim's point; plays its file out */
    RA3D_FORM_RIDER,        /* over the object it is attached to, at that object's height
                               (a parachute); goes with its anim */
    RA3D_FORM_LOOP,         /* on the ground at the anim's point; goes with its anim */
    RA3D_FORM_CORPSE        /* a one-shot drawn only where a 3D soldier's body lies */
};

/* Red Alert's anims with a form, by AnimType (redalert/defines.h) and IniName, how each is
   placed, and the 1996 delay its file was timed to (adata.cpp, the generators' timing). */
struct Ra3dFormDef { int etype; const char* anim; unsigned char kind; unsigned char delay; };
static const Ra3dFormDef RA3D_FORMS[] = {
    { 42, "DEVIATOR", RA3D_FORM_ONESHOT, 2 }, { 43, "DOLLAR",   RA3D_FORM_ONESHOT, 2 },
    { 44, "EARTH",    RA3D_FORM_ONESHOT, 2 }, { 45, "EMPULSE",  RA3D_FORM_ONESHOT, 2 },
    { 46, "INVUN",    RA3D_FORM_ONESHOT, 2 }, { 47, "MINE",     RA3D_FORM_ONESHOT, 2 },
    { 48, "RAPID",    RA3D_FORM_ONESHOT, 2 }, { 49, "STEALTH2", RA3D_FORM_ONESHOT, 2 },
    { 50, "MISSILE2", RA3D_FORM_ONESHOT, 2 }, { 61, "CHRONBOX", RA3D_FORM_ONESHOT, 2 },
    { 62, "GPSBOX",   RA3D_FORM_ONESHOT, 2 }, { 63, "INVULBOX", RA3D_FORM_ONESHOT, 2 },
    { 64, "PARABOX",  RA3D_FORM_ONESHOT, 2 }, { 65, "SONARBOX", RA3D_FORM_ONESHOT, 2 },
    { 73, "ARMOR",    RA3D_FORM_ONESHOT, 2 }, { 74, "SPEED",    RA3D_FORM_ONESHOT, 2 },
    { 75, "FPOWER",   RA3D_FORM_ONESHOT, 2 }, { 76, "TQUAKE",   RA3D_FORM_ONESHOT, 2 },
    { 51, "MOVEFLSH", RA3D_FORM_ONESHOT, 1 },  /* the move order's flash */
    { 59, "SPUTDOOR", RA3D_FORM_ONESHOT, 1 },  /* the GPS launch's rocket */
    { 54, "PARACH",   RA3D_FORM_RIDER,   4 },  /* a paratrooper's or a dropped crate's */
    { 77, "PARABOMB", RA3D_FORM_RIDER,   4 },  /* a parabomb's */
    { 79, "FLAGFLY",  RA3D_FORM_LOOP,    1 },  /* capture the flag */
    { 56, "CORPSE1",  RA3D_FORM_CORPSE, 15 }, { 57, "CORPSE2", RA3D_FORM_CORPSE, 15 },
    { 58, "CORPSE3",  RA3D_FORM_CORPSE, 15 },
};
static const int RA3D_NFORMS = (int)(sizeof RA3D_FORMS / sizeof RA3D_FORMS[0]);
static Ra3dEfxm g_ra3dForm[sizeof RA3D_FORMS / sizeof RA3D_FORMS[0]];
/* A rider's hang: how far over its origin the form's lowest point is once open (the shroud
   lines' meeting point), read off the file at attach, so the load hangs from the lines. */
static float g_ra3dFormHang[sizeof RA3D_FORMS / sizeof RA3D_FORMS[0]];
static bool g_ra3dFormsLoaded = false;

/* A form being drawn: its anim, the engine frame its clock counts from, where it stands. */
struct Ra3dFormInst {
    int def, id, seen;
    float t;                            /* its frame (file ticks) on the last tick */
    float rate;                         /* file ticks a tick while its anim lives */
    int stage, stageAt, stageLen;       /* the anim's stage, since when, the last one's length */
    bool alive;                         /* its anim was in this tick's dump */
    float wx, wz, ylift;
    bool body;                          /* a corpse: a 3D soldier's body lies under it */
    int said;                           /* the last frame reported */
    int drawn;                          /* parts drawn this frame, both phases */
};
static std::vector<Ra3dFormInst> g_ra3dForms;
static long g_ra3dFormArmed = 0;

static int ra3d_form_def(const EfxAnim& a)
{
    for (int i = 0; i < RA3D_NFORMS; i++)
        if (RA3D_FORMS[i].etype == a.etype && !strcmp(RA3D_FORMS[i].anim, a.name))
            return g_ra3dForm[i].mesh >= 0 ? i : -1;
    return -1;
}

/* The forms' files and the own sheets, read once (the first Red Alert mission). */
static void ra3d_efxm_load(const char* dir)
{
    if (g_ra3dFormsLoaded || !g_raModule) return;
    g_ra3dFormsLoaded = true;
    int ok = 0;
    std::string bad;
    for (int i = 0; i < RA3D_NFORMS; i++) {
        char path[512];
        snprintf(path, sizeof path, "%sra3d/efx/%s.efxm", dir && *dir ? dir : "", RA3D_FORMS[i].anim);
        bool missing = false;
        if (ra3d_efxm_read(path, g_ra3dForm[i], &missing)) { ok++; continue; }
        bad += bad.empty() ? "" : ",";
        bad += RA3D_FORMS[i].anim;
        bad += missing ? "(missing)" : "(damaged)";
    }
    printf("RA3DEFXM|loaded|forms=%d/%d|missing=%s\n", ok, RA3D_NFORMS, bad.empty() ? "none" : bad.c_str());
    if (!bad.empty())
        fprintf(stderr, "ra3d effects: %s not read from %sra3d/efx/; those anims keep their 1996 "
                        "cards under Enhanced\n", bad.c_str(), dir && *dir ? dir : "");
    fflush(stdout);
}

/* Red Alert's own sheets, beside efx_enh.pack's (efx_load_pack_enh has just read it, and
   efx_free takes both away). Called after it, for a Red Alert mission only. */
static void ra3d_efxm_sheets(const char* dir)
{
    if (!g_raModule || !g_efxHaveEnh) return;
    char path[512];
    snprintf(path, sizeof path, "%sra3d/efx/efx_ra.pack", dir && *dir ? dir : "");
    efx_read_pack(path, g_efxSeqsEnh, "Red Alert's own effect art");
}

static void ra3d_efxm_attach_all(void)
{
    g_ra3dForms.clear();
    if (!g_raModule) return;
    for (int i = 0; i < RA3D_NFORMS; i++) {
        Ra3dEfxm& E = g_ra3dForm[i];
        ra3d_efxm_attach(E, RA3D_FORMS[i].anim);
        g_ra3dFormHang[i] = 0.0f;
        if (RA3D_FORMS[i].kind != RA3D_FORM_RIDER || E.mesh < 0) continue;
        /* open on the 1996 anim's first frame of its sway (stage 7), as others3d.py times it */
        int f = 7 * RA3D_FORMS[i].delay;
        if (f > E.frames - 1) f = E.frames - 1;
        float lo = 1e9f;
        const int np = (int)E.parts.size();
        for (int p = 0; p < np; p++) {
            if (!E.keys[(size_t)f * np + p].vis) continue;
            const float* m = &E.mats[((size_t)f * np + p) * 12];
            const Ra3dEfxmPart& P = E.parts[p];
            for (int c = P.tri0 * 3; c < (P.tri0 + P.ntris) * 3; c++) {
                const Ra3dEfxmCorner& C = E.corners[(size_t)c];
                const float y = m[4] * C.x + m[5] * C.y + m[6] * C.z + m[7];
                if (y < lo) lo = y;
            }
        }
        g_ra3dFormHang[i] = lo < 1e8f ? lo : 0.0f;
    }
}

/* A 3D soldier's body at (x, z): one of the kept corpses, or a man with a model dying
   there. Within half a cell, the corpse anim being raised a few pixels off his centre. */
static bool ra3d_efxm_body_at(float x, float z)
{
    const float r2 = 0.5f * 0.5f;
    for (size_t i = 0; i < g_m3Corpses.size(); i++) {
        const float dx = g_m3Corpses[i].o.ex - x, dz = g_m3Corpses[i].o.ez - z;
        if (dx * dx + dz * dz < r2) return true;
    }
    for (size_t i = 0; i < g_objects.size(); i++) {
        const SimObject& o = g_objects[i];
        if (o.kind != K_INFANTRY || !o.dying || m3inf_model_any(o) < 0) continue;
        const float dx = o.ex - x, dz = o.ez - z;
        if (dx * dx + dz * dz < r2) return true;
    }
    return false;
}

/* WHERE A RIDER'S FORM STANDS: over the object its anim is attached to. The engine puts the
   anim (ObjectClass::Paradrop) 0x30 leptons over the falling object, and the object its
   height (ObjectClass::Height, leptons) over its ground point, which the 2D picture draws
   as that many leptons up the screen; so the dump's anim point is that far north of the
   object's ground point, and the height is read back off the two. Heights here are the
   projectiles' scale (efx_bullet_lift: a lepton up is a lepton across). A paratrooper's 3D
   body is lifted to his height on the same tick (the infantry pass draws him at o.ylift,
   the lift an LST's riders stand on), so he hangs under his canopy and comes down with it
   rather than standing on the ground below it; a parabomb is a projectile, drawn at its
   own height already, and its canopy rides it. The canopy is hung so its shroud lines
   meet the load (the hang, read off the file), where the 1996 picture has them meet. An
   anim on nothing (one raised by itself) stands where it is. */
static const int RA3D_RTTI_BULLET = 7;          /* redalert/defines.h RTTI_BULLET */
/* where a load meets the lines: a 3D paratrooper's shoulders over his feet, a bomb's nose
   over its middle (cells) */
static const float RA3D_RIDER_SHOULDER = 0.22f, RA3D_RIDER_NOSE = 0.05f;
static void ra3d_form_rider(const EfxAnim& a, float hang, float* x, float* z, float* ylift)
{
    *x = a.wx; *z = a.wz; *ylift = 0.0f;
    const float over = 48.0f / 256.0f;           /* the 0x30 leptons over the load */
    if (a.attkind == RA3D_RTTI_BULLET) {
        /* Match the engine's ground point; an aerial shot's drawn point may already
           have moved to its model's muzzle. The canopy follows that drawn point. */
        const EfxBullet* best = NULL;
        float err = 0.35f;
        for (size_t i = 0; i < g_efxBullets.size(); i++) {
            const EfxBullet& b = g_efxBullets[i];
            const float e = fabsf(b.ewx - a.wx) + fabsf((b.ewz - a.wz) - ((float)b.alt / 256.0f + over));
            if (e < err) { err = e; best = &b; }
        }
        if (best) { *x = best->wx; *z = best->wz; *ylift = efx_bullet_lift(*best) + RA3D_RIDER_NOSE - hang; }
        return;
    }
    ObjKind kind; int layer;
    if (a.attref < 0 || !ra3d_rtti_kind(a.attkind, &kind, &layer)) return;
    for (size_t i = 0; i < g_objects.size(); i++) {
        SimObject& o = g_objects[i];
        if (o.id != a.attref || o.kind != kind) continue;
        float h = (o.ez - a.wz) - over;           /* cells */
        if (h < 0.0f) h = 0.0f;
        *x = o.wx; *z = o.wz;
        *ylift = h + RA3D_RIDER_SHOULDER - hang;
        if (g_efxEnhOn && o.ylift < h) o.ylift = h;
        return;
    }
}

/* ON THE TICK, beside efx_step: arm a form at its anim's first sighting, follow its anim's
   stage while it lives, play a one-shot out on the tick clock once it is gone, and drop
   what is done. Never from the draw, so a frame drawn twice or ticks with no frame drawn
   change nothing. */
static void ra3d_efxm_track(void)
{
    if (!g_raModule) { g_ra3dForms.clear(); return; }
    for (size_t b = 0; b < g_ra3dForms.size(); b++) g_ra3dForms[b].alive = false;
    for (size_t i = 0; i < g_efxAnims.size(); i++) {
        const EfxAnim& a = g_efxAnims[i];
        const int d = ra3d_form_def(a);
        if (d < 0) continue;
        const Ra3dFormDef& D = RA3D_FORMS[d];
        Ra3dFormInst* F = NULL;
        for (size_t b = 0; b < g_ra3dForms.size(); b++)
            if (g_ra3dForms[b].id == a.id && g_ra3dForms[b].def == d
                && g_engineFrame - g_ra3dForms[b].seen <= 2) { F = &g_ra3dForms[b]; break; }
        const int stage = a.stage > 0 ? a.stage : 0;
        if (!F) {
            Ra3dFormInst N;
            N.def = d; N.id = a.id; N.seen = g_engineFrame;
            N.stage = stage; N.stageAt = g_engineFrame; N.stageLen = D.delay;
            N.t = (float)(stage * D.delay); N.rate = 1.0f;
            N.alive = true; N.body = false; N.said = -1; N.drawn = 0;
            N.wx = a.wx; N.wz = a.wz; N.ylift = 0.0f;
            g_ra3dForms.push_back(N);
            F = &g_ra3dForms.back();
            g_ra3dFormArmed++;
            printf("RA3DEFXM|armed|%s|id=%d|stage=%d|cell=%.2f,%.2f\n", D.anim, a.id, a.stage, a.wx, a.wz);
        } else if (stage != F->stage) {
            /* a new stage: its length is the one just ended (a loop's jump back counts as a
               new stage like any other) */
            F->stageLen = g_engineFrame - F->stageAt;
            if (F->stageLen < 1) F->stageLen = 1;
            F->stage = stage;
            F->stageAt = g_engineFrame;
        }
        F->seen = g_engineFrame;
        F->alive = true;
        /* the frame: the stage's own, and the ticks into it at the pace the stage before ran */
        const float into = (float)(g_engineFrame - F->stageAt) * (float)D.delay / (float)F->stageLen;
        F->rate = (float)D.delay / (float)F->stageLen;
        F->t = (float)(stage * D.delay) + (into < (float)D.delay ? into : (float)D.delay - 0.001f);
        if (D.kind == RA3D_FORM_RIDER) ra3d_form_rider(a, g_ra3dFormHang[d], &F->wx, &F->wz, &F->ylift);
        else { F->wx = a.wx; F->wz = a.wz; }
        if (D.kind == RA3D_FORM_CORPSE && !F->body && ra3d_efxm_body_at(a.wx, a.wz)) {
            F->body = true;
            printf("RA3DEFXM|body|%s|id=%d|cell=%.2f,%.2f\n", D.anim, a.id, a.wx, a.wz);
        }
    }
    size_t w = 0;
    for (size_t b = 0; b < g_ra3dForms.size(); b++) {
        Ra3dFormInst F = g_ra3dForms[b];
        const Ra3dFormDef& D = RA3D_FORMS[F.def];
        const Ra3dEfxm& E = g_ra3dForm[F.def];
        if (!F.alive) {
            /* its anim is gone: a looping form goes with it, a one-shot plays out its file */
            if (D.kind == RA3D_FORM_RIDER || D.kind == RA3D_FORM_LOOP) continue;
            F.t += 1.0f;
            F.rate = 1.0f;
            if (F.t > (float)(E.frames - 1)) continue;
        }
        g_ra3dForms[w++] = F;
    }
    g_ra3dForms.resize(w);
    fflush(stdout);
}

/* Whether a corpse anim stands on a 3D soldier's body (its form drawn, its card not). */
static bool ra3d_efxm_corpse_body(const EfxAnim& a)
{
    for (size_t b = 0; b < g_ra3dForms.size(); b++)
        if (g_ra3dForms[b].id == a.id && RA3D_FORMS[g_ra3dForms[b].def].etype == a.etype)
            return g_ra3dForms[b].body;
    return false;
}

/* THE FORMS' DRAW, phase 0 before the particles and phase 1 after them. */
static long g_ra3dFormDraws = 0;
static void ra3d_efxm_draw_forms(int phase)
{
    if (g_ra3dForms.empty() || !g_efxEnhOn) return;
    ra3d_efxm_begin(false);
    for (size_t b = 0; b < g_ra3dForms.size(); b++) {
        Ra3dFormInst& F = g_ra3dForms[b];
        const Ra3dFormDef& D = RA3D_FORMS[F.def];
        if (D.kind == RA3D_FORM_CORPSE && !F.body) continue;
        if (shroud_cell_hidden((int)floorf(F.wx), (int)floorf(F.wz))) continue;
        Ra3dEfxm& E = g_ra3dForm[F.def];
        int seq[64];
        const int n = ra3d_efxm_sequence(E, phase, seq, 64);
        float t = F.t + g_tickAlpha * F.rate;
        if (t > (float)(E.frames - 1)) t = (float)(E.frames - 1);
        const long before = E.partDraws;
        for (int i = 0; i < n; i++)
            ra3d_efxm_draw_part(E, seq[i], F.wx, F.wz, F.ylift, t);
        g_ra3dFormDraws += E.partDraws - before;
        if (phase == 0) F.drawn = 0;
        F.drawn += (int)(E.partDraws - before);
        /* ONE LINE A FORM A FRAME, from the draw, so a gate reading it knows the parts
           reached draw_mesh rather than that an anim existed */
        const int fi = (int)t;
        if (phase == 1 && fi != F.said) {
            F.said = fi;
            int on = 0;
            for (int p = 0; p < (int)E.parts.size(); p++) on += ra3d_efxm_on(E, fi, p) ? 1 : 0;
            printf("RA3DEFXM|draw|%s|id=%d|frame=%d|on=%d|parts=%d|at=%.2f,%.2f,%.2f\n", D.anim, F.id,
                   fi, on, F.drawn, F.wx, F.wz, F.ylift);
        }
    }
    ra3d_efxm_end();
}

/* "efxmdump": the forms being drawn, for the gate. */
static void ra3d_efxm_dump(void)
{
    printf("RA3DEFXM|dump|forms=%zu|armed=%ld|partdraws=%ld|enh=%d\n", g_ra3dForms.size(),
           g_ra3dFormArmed, g_ra3dFormDraws, g_efxEnhOn ? 1 : 0);
    /* the anims the last frame's sprite pass still drew as their 1996 cards */
    printf("RA3DEFXM|cards|%s\n", g_ra3dAnimCardNames.c_str());
    for (size_t b = 0; b < g_ra3dForms.size(); b++) {
        const Ra3dFormInst& F = g_ra3dForms[b];
        printf("RA3DEFXM|form|%s|id=%d|t=%.2f|alive=%d|body=%d|at=%.2f,%.2f,%.2f\n",
               RA3D_FORMS[F.def].anim, F.id, F.t, F.alive ? 1 : 0, F.body ? 1 : 0, F.wx, F.wz, F.ylift);
    }
    fflush(stdout);
}

/* ------------------------------------------------------------------ the hooks */

/* The Tiberian Dawn anim an own effect with no counterpart rides, so the particle tick
   takes it: SMOKE_M, whose cartridge recipe is empty and fires nothing of its own. */
static const int RA3D_EFXM_CARRIER = 35;

/* Red Alert's recipe of its own for this anim, or NULL. */
static const char* ra3d_efxm_own_recipe(const char* anim)
{
    if (!strcmp(anim, "ATOMSFX")) return NULL;          /* the atom bomb's file decides */
    for (int i = 0; i < EFX_RA_NRECIPE; i++)
        if (!strcmp(EFX_RA_RECIPE[i].anim, anim)) return EFX_RA_RECIPE[i].recipe;
    return NULL;
}

/* For ra3d_efx_type: an anim with no Tiberian Dawn counterpart that has an effect of its
   own here rides the carrier; anything else keeps its card. */
static int ra3d_efxm_type(int etype, const char* anim)
{
    if (ra3d_efxm_own_recipe(anim)) return RA3D_EFXM_CARRIER;
    for (int i = 0; i < RA3D_NFORMS; i++)
        if (RA3D_FORMS[i].etype == etype && !strcmp(RA3D_FORMS[i].anim, anim) && g_ra3dForm[i].mesh >= 0)
            return RA3D_EFXM_CARRIER;
    return EFX_TYPE_HOST;
}

static bool ra3d_atom_is(const EfxAnim& a);   /* ra3d_atom_mod.h, below */
static void ra3d_atom_draw(int phase);        /* ra3d_atom_mod.h, below */

/* g_efxRecipeOf: the recipe of its own an anim fires. The large fireball off a dying
   building's plot stays the building's blast (FRAG3, ra3d_effects_mod.h); every other
   large fireball is Red Alert's own. */
static const char* ra3d_efxm_recipe(const EfxAnim& a, const char* rname)
{
    if (!g_raModule || !g_efxEnhOn) return rname;
    if (ra3d_atom_is(a)) return "ATOM_BLAST_RA";
    if (a.etype == 0 && !strcmp(rname, "FRAG3")) return rname;
    const char* own = ra3d_efxm_own_recipe(a.name);
    return own ? own : rname;
}

/* g_efxHostMeshes: the atom bomb's parts and the forms'. */
static void ra3d_efxm_host_meshes(int phase)
{
    ra3d_atom_draw(phase);
    ra3d_efxm_draw_forms(phase);
}

/* For ra3d_anim_card: whether an anim drawn here keeps its 1996 card all the same. */
static bool ra3d_efxm_keeps_card(const EfxAnim& a)
{
    /* an electrocution: the engine has deleted the soldier or the dog, and the card is his
       body, so it stays under the arcs */
    if (a.etype == 53 || a.etype == 55) return true;
    /* a corpse: where no 3D soldier's body lies, the card is the body */
    if (a.etype >= 56 && a.etype <= 58) return !ra3d_efxm_corpse_body(a);
    return false;
}

/* The hooks, for a Red Alert mission (after ra3d_efx_install's). The atom bomb's own
   (ra3d_atom_mod.h) are reached through these. */
static void ra3d_efxm_install(void)
{
    if (!g_raModule) return;
    g_efxRecipeOf = ra3d_efxm_recipe;
    g_efxHostMeshes = ra3d_efxm_host_meshes;
}
