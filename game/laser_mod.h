/* laser_mod.h -- THE OBELISK OF LIGHT'S BEAM, in all three graphics modes.
 *
 * WHAT THE ENGINE DOES. The 1995 game does not draw the Obelisk's shot as an anim or as a
 * projectile. TechnoClass::Fire_At (techno.cpp, the BULLET_LASER block) stores three lines
 * on the firer in tactical pixels: two outer ones in palette colour 0x7D starting one pixel
 * either side of the source, and the core in 0x7F, all running from Fire_Coord to
 * As_Coord(target), and TechnoClass::Draw_It draws them on each of the next LineMaxFrames
 * (5) frames. The shot's damage is a real projectile (BULLET_LASER, invisible, light speed)
 * and arrives on its own; the lines are only the picture.
 *
 * WHY NOTHING WAS DRAWN. Neither half of that reaches a 3D picture: the pixels belong to the
 * 2D view they were measured against, and LineFrame advances only when Draw_It runs against
 * the 2D layer export, which only the Classic scene calls, and Classic ignored the lines.
 * The brain now copies each beam out in world coordinates (OpenCNC3D_Note_Line, printed as
 * EFX|LINE and parsed by effects_mod.h), and this module keeps it for its five frames.
 *
 * THE CLOCK IS THE ENGINE'S. A beam is stamped with the engine frame of the dump that
 * carried it and shows while (frame - born) < frames: the first picture after the shot and
 * the four after it, which is the 1995 count (Logic.AI fires in frame N, Map.Render draws
 * it in frames N+1..N+5, Main_Loop renders once per logic frame). Under N64 it is the
 * cartridge's count, six. No wall clock, so two --shot runs agree.
 *
 * THE THREE PICTURES.
 *   Classic   the engine's three lines, Bresenham in 1995 pixels, in the engine's own palette
 *             colours, drawn with the firer (dosclassic_mod.h, dos2d_draw_lasers).
 *   N64       the cartridge's own beam, decoded from the ROM: one flat opaque red
 *             triangle from high on the spire down to the target on the ground
 *             (laser_draw_n64 below; every address in docs/n64-laser.md). Fixed function.
 *   Enhanced  a bright camera-facing beam in a shader, white-hot core in a red glow, with a
 *             flare at each end, in the scene target so the bloom takes it, kept out of the
 *             light pass by the self-lit class (laser_draw_normals), and a red light where it
 *             lands (laser_gather_lights, called from fx_gather_lights). Tier 2 only, like
 *             everything under Enhanced; the Tier 1 answer for the other two modes is that
 *             both are fixed-function geometry with no shader at all (docs/tier1-gap.md).
 *
 * THE ENDS UNDER ENHANCED ARE OURS where the engine has nothing to say (N64's are the
 * cartridge's, above). The engine's two coordinates are ground positions. The source is put
 * on the model the Obelisk is drawn with, at its highest vertex (the tip of the spire, where
 * the remaster's red crystal is), because the engine's Fire_Coord is the sprite's tip moved
 * north 0xA8 leptons and west 0x18, a 2D projection of a height. The far end is the target's
 * own engine coordinate at a body height chosen per kind, where the cartridge puts it on the
 * ground. Both are written down in laser_source and laser_target.
 */
#ifndef OPENCNC3D_LASER_MOD_H
#define OPENCNC3D_LASER_MOD_H

/* The cartridge's own count of frames a beam shows: Laser_Add(src, dst, 444, 0, 6), the one
   call to it (ROM 0x03F3C4, BuildingClass::Fire_At). */
#define LASER_N64_FRAMES 6
struct LaserBeam {
    EfxLine l;
    int born;                 /* engine frame of the dump that carried it; -1 until stamped */
};
static std::vector<LaserBeam> g_lasers;
static bool g_laserOn = true;          /* --nolaser and the `laser 0` verb, the A/B */
static long g_laserNoted = 0;          /* beams parsed this run                     */
static long g_laserDrawn = 0;          /* beam draws (any mode) in the last frame   */
static int  g_laserDrawnMode = -1;     /* the mode that last drew one               */
/* the colours the Classic lines were last drawn in, as the palette gave them, per line */
static int  g_laserClassicRGB[3][3] = { { -1, -1, -1 }, { -1, -1, -1 }, { -1, -1, -1 } };

static void laser_on_line(const EfxLine& l)
{
    LaserBeam b;
    b.l = l;
    b.born = -1;
    g_lasers.push_back(b);
    g_laserNoted++;
}

/* Called from refresh_objects on every dump, with the frame the dump ended on. A beam is
   born on the first dump that carried it; a frame that runs backwards (a new mission, a
   load) clears the list, because a beam from another timeline must never show. */
static void laser_stamp(int frameno)
{
    for (size_t i = g_lasers.size(); i-- > 0; ) {
        LaserBeam& b = g_lasers[i];
        if (b.born < 0) b.born = frameno;
        const int keep = b.l.frames > LASER_N64_FRAMES ? b.l.frames : LASER_N64_FRAMES;
        if (b.born > frameno || frameno - b.born >= keep)
            g_lasers.erase(g_lasers.begin() + (long)i);
    }
}

/* HOW MANY FRAMES A BEAM SHOWS, which is the one thing the modes disagree on: the engine's
   LineMaxFrames (5) in Classic and Enhanced, the cartridge's own count (LASER_N64_FRAMES, 6)
   under N64. */
static int laser_frames(const LaserBeam& b)
{
    if (!g_fx.dos_classic && fx_mode_of(&g_fx) == FX_MODE_N64) return LASER_N64_FRAMES;
    return b.l.frames;
}

/* Is this beam on the picture now, and how far through its life (0 at the shot's first
   picture, up to 1 at the end). The integer age is the engine's; the fraction between two
   ticks is g_tickAlpha, which is 0 on every scripted shot. */
static bool laser_live(const LaserBeam& b, float* life)
{
    const int frames = laser_frames(b);
    if (!g_laserOn || b.born < 0 || frames <= 0) return false;
    const int age = g_engineFrame - b.born;
    if (age < 0 || age >= frames) return false;
    if (life) {
        float t = ((float)age + g_tickAlpha) / (float)frames;
        *life = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    }
    return true;
}

static int laser_live_count(void)
{
    int n = 0;
    for (size_t i = 0; i < g_lasers.size(); i++)
        if (laser_live(g_lasers[i], NULL)) n++;
    return n;
}

static ObjKind laser_kind_of_rtti(int rtti)
{
    switch (rtti) {
    case 1: return K_INFANTRY;     /* RTTI_INFANTRY */
    case 3: return K_UNIT;         /* RTTI_UNIT     */
    case 5: return K_AIRCRAFT;     /* RTTI_AIRCRAFT */
    case 7: return K_BUILDING;     /* RTTI_BUILDING */
    case 9: return K_TERRAIN;      /* RTTI_TERRAIN  */
    default: return K_OTHER;
    }
}

static const SimObject* laser_object(int rtti, int id)
{
    if (id < 0) return NULL;
    const ObjKind k = laser_kind_of_rtti(rtti);
    if (k == K_OTHER) return NULL;
    for (size_t i = 0; i < g_objects.size(); i++)
        if (g_objects[i].kind == k && g_objects[i].id == id) return &g_objects[i];
    return NULL;
}

/* THE HIGHEST POINT OF A MESH, in its own units: the spire's tip on the Obelisk. Shadow and
   translucent faces are not the body. Read afresh for every beam drawn, a few hundred
   corners at most a frame, so a pack reloaded for the next mission can never answer with
   the last one's mesh. */
static bool laser_mesh_tip(int mi, float out[3])
{
    if (mi < 0 || mi >= (int)g_pack.mesh.size()) return false;
    const std::vector<PackTri>& tris = g_pack.mesh[mi].tris;
    bool any = false;
    for (size_t i = 0; i < tris.size(); i++) {
        if (tris[i].mode == MODE_SHADOW || tris[i].mode == MODE_XLU) continue;
        for (int v = 0; v < 3; v++)
            if (!any || tris[i].v[v].y > out[1]) {
                out[0] = tris[i].v[v].x; out[1] = tris[i].v[v].y; out[2] = tris[i].v[v].z;
                any = true;
            }
    }
    return any;
}

/* The same for a remastered building's standing model (ebld_mod.h): the pack mesh its
   intact set was built into, which is drawn from the same anchor as the cartridge's. -1
   when Enhanced would not draw a remaster for this type. */
static int laser_ebld_mesh(const SimObject& o)
{
    if (!ebld_rule_now() || o.kind != K_BUILDING) return -1;
    const int m = ebld_model_of(o.type);
    if (m < 0 || object_model3d(o) >= 0) return -1;
    return g_ebld[m].mesh[EBM_INTACT];
}

/* WHERE THE BEAM LEAVES, in world cells (y up). The firer's drawn model's tip when the
   firer is on the list and has one; otherwise the engine's own Fire_Coord on the ground
   with a lift, so a beam whose firer has just died still has somewhere to start. Writes
   which rule answered into *how, for the dump. */
static void laser_source(const LaserBeam& b, bool enhanced, float out[3], const char** how)
{
    const SimObject* o = laser_object(b.l.kind, b.l.id);
    if (o) {
        float tip[3];
        const int rmi = enhanced ? laser_ebld_mesh(*o) : -1;
        const bool remaster = rmi >= 0 && laser_mesh_tip(rmi, tip);
        if (remaster || laser_mesh_tip(mesh_for(*o), tip)) {
            out[0] = o->wx + tip[0] * MODEL_SCALE;
            out[1] = terrain_y(o->wx, o->wz) + alt_lift(*o) + tip[1] * MODEL_SCALE;
            out[2] = o->wz + tip[2] * MODEL_SCALE;
            if (how) *how = remaster ? "remaster-tip" : "mesh-tip";
            return;
        }
    }
    out[0] = b.l.sx;
    out[2] = b.l.sz;
    out[1] = terrain_y(b.l.sx, b.l.sz) + 0.5f;
    if (how) *how = "engine";
}

/* WHERE IT LANDS: the engine's As_Coord(target), which is the target's centre on the
   ground, raised to about the middle of what stands there. A cell target is the ground. */
static void laser_target(const LaserBeam& b, float out[3])
{
    out[0] = b.l.tx;
    out[2] = b.l.tz;
    float lift = 0.0f;
    const SimObject* t = laser_object(b.l.tkind, b.l.tid);
    if (t) {
        switch (t->kind) {
        case K_INFANTRY: lift = 0.09f; break;
        case K_UNIT:     lift = 0.12f; break;
        case K_AIRCRAFT: lift = alt_lift(*t) + 0.08f; break;
        case K_BUILDING: lift = 0.20f; break;
        default:         lift = 0.05f; break;
        }
    }
    out[1] = terrain_y(b.l.tx, b.l.tz) + lift;
}

/* A quad along a->b that faces the camera, half-width h, written as four corners in the
   order a-left, b-left, b-right, a-right. The camera's axes are the billboard pair set for
   the effects pass this frame. */
static void laser_ribbon(const float* a, const float* b, float h, float q[4][3])
{
    float d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
    float f[3] = { g_bbRight[1] * g_bbUp[2] - g_bbRight[2] * g_bbUp[1],
                   g_bbRight[2] * g_bbUp[0] - g_bbRight[0] * g_bbUp[2],
                   g_bbRight[0] * g_bbUp[1] - g_bbRight[1] * g_bbUp[0] };
    float s[3] = { d[1] * f[2] - d[2] * f[1], d[2] * f[0] - d[0] * f[2], d[0] * f[1] - d[1] * f[0] };
    float sl = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    if (sl < 1e-6f) { s[0] = g_bbRight[0]; s[1] = g_bbRight[1]; s[2] = g_bbRight[2]; sl = 1.0f; }
    for (int k = 0; k < 3; k++) {
        s[k] *= h / sl;
        q[0][k] = a[k] - s[k]; q[1][k] = b[k] - s[k];
        q[2][k] = b[k] + s[k]; q[3][k] = a[k] + s[k];
    }
}

/* ======================================================================== ENHANCED */
static const char* LASER_VS =
    "#version 120\n"
    "varying vec2 vUV;\n"
    "void main() {\n"
    "    vUV = gl_MultiTexCoord0.xy;\n"
    "    gl_Position = ftransform();\n"
    "}\n";
/* uRadial 0: the beam, vUV.y across it in -1..1 and vUV.x along it in 0..1. uRadial 1: a
   flare, vUV a disc in -1..1. A narrow white-hot core inside a wide red glow, both scene
   referred so the core sits well over the bloom's threshold. uClass: when set, the pass
   writes only the class (the normal target's alpha) where the core is, nothing else. */
static const char* LASER_FS =
    "#version 120\n"
    "varying vec2 vUV;\n"
    "uniform int   uRadial;\n"
    "uniform float uI;\n"
    "uniform vec3  uCore, uGlow;\n"
    "uniform float uClass;\n"
    FX_OBJECT_GRADE_GLSL
    "void main() {\n"
    "    float r = uRadial == 1 ? length(vUV) : abs(vUV.y);\n"
    "    float core = exp(-r * r * (uRadial == 1 ? 9.0 : 30.0));\n"
    "    float glow = exp(-r * r * (uRadial == 1 ? 2.5 : 4.0)) * (1.0 - min(r, 1.0));\n"
    "    if (uClass > 0.0) {\n"
    "        if (core < 0.25) discard;\n"
    "        gl_FragColor = vec4(0.5, 0.5, 1.0, uClass);\n"
    "        return;\n"
    "    }\n"
    "    vec3 c = uCore * core + uGlow * glow;\n"
    "    gl_FragColor = vec4(objectGrade(c * uI), 1.0);\n"
    "}\n";
static GLuint g_laserProg = 0;
static bool g_laserProgTried = false;

static GLuint laser_prog(void)
{
    if (g_laserProg || g_laserProgTried) return g_laserProg;
    g_laserProgTried = true;
    if (!fx_gl_ready) return 0;
    GLuint vs = fx_compile(GL_VERTEX_SHADER, LASER_VS, "laser");
    if (!vs) return 0;
    GLuint fs = fx_compile(GL_FRAGMENT_SHADER, LASER_FS, "laser");
    if (!fs) { fx_glDeleteShader(vs); return 0; }
    GLuint p = fx_glCreateProgram();
    fx_glAttachShader(p, vs);
    fx_glAttachShader(p, fs);
    fx_glLinkProgram(p);
    GLint ok = 0;
    fx_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        fprintf(stderr, "laser: the beam's program FAILED TO LINK\n");
        fx_glDeleteProgram(p);
        p = 0;
    }
    fx_glDeleteShader(vs);
    fx_glDeleteShader(fs);
    g_laserProg = p;
    return p;
}

/* The beam's strength over its five frames: whole for the first two, then gone by the end
   along a square, so the last picture still carries a thin trace. */
static float laser_enh_strength(float life)
{
    if (life < 0.4f) return 1.0f;
    const float k = 1.0f - (life - 0.4f) / 0.6f;
    return k <= 0.0f ? 0.0f : 0.15f + 0.85f * k * k;
}

/* Widths in cells: the beam's quad (its glow reaches the edge) and the two flares. */
#define LASER_ENH_HALF     0.16f
#define LASER_ENH_FLARE_S  0.22f
#define LASER_ENH_FLARE_T  0.34f
#define LASER_ENH_I        5.0f

static void laser_enh_quads(const LaserBeam& b, float I, bool classPass)
{
    float S[3], T[3], q[4][3];
    laser_source(b, true, S, NULL);
    laser_target(b, T);
    const GLuint prog = g_laserProg;
    fx_set1i(prog, "uRadial", 0);
    fx_set1f(prog, "uI", I);
    laser_ribbon(S, T, LASER_ENH_HALF, q);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, -1.0f); glVertex3fv(q[0]);
    glTexCoord2f(1.0f, -1.0f); glVertex3fv(q[1]);
    glTexCoord2f(1.0f,  1.0f); glVertex3fv(q[2]);
    glTexCoord2f(0.0f,  1.0f); glVertex3fv(q[3]);
    glEnd();
    fx_set1i(prog, "uRadial", 1);
    for (int e = 0; e < 2; e++) {
        const float* c = e ? T : S;
        const float h = e ? LASER_ENH_FLARE_T : LASER_ENH_FLARE_S;
        if (classPass && e == 0) continue;     /* the lens is the building's own self-lit */
        glBegin(GL_QUADS);
        glTexCoord2f(-1.0f, -1.0f);
        glVertex3f(c[0] + (-g_bbRight[0] - g_bbUp[0]) * h, c[1] + (-g_bbRight[1] - g_bbUp[1]) * h,
                   c[2] + (-g_bbRight[2] - g_bbUp[2]) * h);
        glTexCoord2f(1.0f, -1.0f);
        glVertex3f(c[0] + (g_bbRight[0] - g_bbUp[0]) * h, c[1] + (g_bbRight[1] - g_bbUp[1]) * h,
                   c[2] + (g_bbRight[2] - g_bbUp[2]) * h);
        glTexCoord2f(1.0f, 1.0f);
        glVertex3f(c[0] + (g_bbRight[0] + g_bbUp[0]) * h, c[1] + (g_bbRight[1] + g_bbUp[1]) * h,
                   c[2] + (g_bbRight[2] + g_bbUp[2]) * h);
        glTexCoord2f(-1.0f, 1.0f);
        glVertex3f(c[0] + (-g_bbRight[0] + g_bbUp[0]) * h, c[1] + (-g_bbRight[1] + g_bbUp[1]) * h,
                   c[2] + (-g_bbRight[2] + g_bbUp[2]) * h);
        glEnd();
    }
}

static void laser_draw_enhanced(void)
{
    if (!laser_prog()) return;
    GLint was = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &was);
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_CURRENT_BIT);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    fx_glUseProgram(g_laserProg);
    fx_set1f(g_laserProg, "uClass", 0.0f);
    efx_grade_uniforms(g_laserProg);
    fx_set3f(g_laserProg, "uCore", 1.0f, 0.86f, 0.80f);
    fx_set3f(g_laserProg, "uGlow", 1.0f, 0.10f, 0.04f);
    for (size_t i = 0; i < g_lasers.size(); i++) {
        float life;
        if (!laser_live(g_lasers[i], &life)) continue;
        laser_enh_quads(g_lasers[i], LASER_ENH_I * laser_enh_strength(life), false);
        g_laserDrawn++;
    }
    fx_glUseProgram((GLuint)was);
    glPopAttrib();
}

/* THE SELF-LIT CLASS under the beam's core, in the normal target's alpha, so the light pass
   leaves it without sun, shadow, cloud, relight or occlusion (the 0.15 band ebld_mod.h's
   lit lenses use). Only the core: the glow over the ground keeps the ground's class and is
   lit with it. Against the scene's depth, as everything in that pass is. */
static void laser_draw_normals(void)
{
    if (!g_laserOn || g_lasers.empty() || fx_mode_of(&g_fx) != FX_MODE_ENHANCED) return;
    if (!laser_live_count() || !laser_prog()) return;
    GLint was = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &was);
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_CURRENT_BIT);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
    fx_glUseProgram(g_laserProg);
    fx_set1f(g_laserProg, "uClass", EBLD_SELFLIT_CLASS);
    for (size_t i = 0; i < g_lasers.size(); i++) {
        float life;
        if (!laser_live(g_lasers[i], &life)) continue;
        laser_enh_quads(g_lasers[i], 1.0f, true);
    }
    fx_glUseProgram((GLuint)was);
    glPopAttrib();
}

/* ============================================================================= N64 */
/* THE CARTRIDGE'S BEAM, as its own laser loop draws it (decoded from the ROM; the write-up
   with every address and word is docs/n64-laser.md). The console moved the 1995 laser out of
   TechnoClass::Fire_At into BuildingClass::Fire_At, which hands a world-space beam to a pool
   of twenty that Map.Render draws and counts down. Per beam, ONE TRIANGLE standing in the
   vertical plane through its two ends (not turned to the camera, so a beam fired due north
   or south is seen nearly edge on, as on the console):

     A  the source, 444 leptons above the ground there
     C  straight under A by w = 25 * (1 + 444 / sqrt(d*d + 444*444)) leptons, d the beam's
        length along the ground: 50 under the Obelisk, about 31 at its full range
     B  the target's own coordinate, ON the ground

   The source is the Obelisk's footprint centre moved 22 leptons west and 29 south (its
   Fire_Coord on the cartridge, which replaced the 1995 sprite lift with that height), and it
   does not turn toward the target. One flat colour, (255, 10, 10), opaque, both faces,
   depth tested and written; no texture, no shading, no glow at either end. Six frames at
   full colour, then gone (LASER_N64_FRAMES, where the 1995 engine says five). The console
   antialiases the triangle's edges (G_RM_RA_ZB_OPA_SURF); this draws them hard, as every
   cartridge model here is drawn. Fixed function throughout, so Tier 1 draws it exactly. */
#define LASER_N64_MAX     20         /* the pool at RAM 0x801FDD90: a 21st is not drawn    */
#define LASER_N64_HEIGHT  444.0f     /* leptons, *0x80096A54 from Fire_Coord's Obelisk arm */
#define LASER_N64_WIDTH   25.0f      /* *0x80097A28                                        */
#define LASER_N64_WEST    22.0f      /* Fire_Coord: Coord_Move(DIR_W, 0x16)                */
#define LASER_N64_SOUTH   29.0f      /* then Coord_Move(DIR_S, 0x1D)                       */

/* The triangle's three corners in world cells, A, B, C as above. The centre is the firer's
   when it fired; a brain that does not export it falls back to the engine's Fire_Coord,
   which is the right place on the ground give or take the 1995 sprite lift. */
static void laser_n64_tri(const LaserBeam& b, float A[3], float B[3], float C[3])
{
    float sx = b.l.sx, sz = b.l.sz;
    if (b.l.cx == b.l.cx && b.l.cz == b.l.cz) {
        sx = b.l.cx - LASER_N64_WEST / 256.0f;
        sz = b.l.cz + LASER_N64_SOUTH / 256.0f;
    }
    const float dx = (b.l.tx - sx) * 256.0f, dz = (b.l.tz - sz) * 256.0f;
    const float d2 = dx * dx + dz * dz;
    const float w = LASER_N64_WIDTH * (1.0f + LASER_N64_HEIGHT
                                      / sqrtf(d2 + LASER_N64_HEIGHT * LASER_N64_HEIGHT));
    A[0] = sx; A[2] = sz;
    A[1] = terrain_y(sx, sz) + LASER_N64_HEIGHT / 256.0f;
    C[0] = sx; C[2] = sz;
    C[1] = A[1] - w / 256.0f;
    B[0] = b.l.tx; B[2] = b.l.tz;
    B[1] = terrain_y(b.l.tx, b.l.tz);
}

static void laser_draw_n64(void)
{
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_CURRENT_BIT);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glColor3ub(255, 10, 10);
    glBegin(GL_TRIANGLES);
    int n = 0;
    for (size_t i = 0; i < g_lasers.size() && n < LASER_N64_MAX; i++) {
        if (!laser_live(g_lasers[i], NULL)) continue;
        float A[3], B[3], C[3];
        laser_n64_tri(g_lasers[i], A, B, C);
        glVertex3fv(A); glVertex3fv(B); glVertex3fv(C);
        n++;
    }
    glEnd();
    glColor4f(1, 1, 1, 1);
    glPopAttrib();
    g_laserDrawn += n;
}

/* The 3D modes' entry, from the effects pass (6b in draw_frame). */
static void laser_draw_3d(void)
{
    g_laserDrawn = 0;
    if (!g_laserOn || g_lasers.empty() || g_fx.dos_classic) return;
    if (fx_mode_of(&g_fx) == FX_MODE_ENHANCED && fx_gl_ready) {
        laser_draw_enhanced();
        g_laserDrawnMode = FX_MODE_ENHANCED;
    } else {
        laser_draw_n64();
        g_laserDrawnMode = FX_MODE_N64;
    }
}

/* THE LIGHT WHERE IT LANDS, Enhanced's dynamic light, red, for the beam's five frames and
   then gone with it (no afterglow: the 1995 beam has none). Appended to the candidates the
   explosions rank against, through the host's own push. */
static int laser_light_count(void)
{
    return (g_fx.enabled && g_fx.lights_on) ? laser_live_count() : 0;
}
static bool laser_light(int n, float* x, float* y, float* z, float* power)
{
    int k = 0;
    for (size_t i = 0; i < g_lasers.size(); i++) {
        float life;
        if (!laser_live(g_lasers[i], &life)) continue;
        if (k++ != n) continue;
        float T[3];
        laser_target(g_lasers[i], T);
        *x = T[0]; *y = T[1] + 0.20f; *z = T[2];
        *power = laser_enh_strength(life);
        return true;
    }
    return false;
}

/* LASER| lines for the gate: the beams on the list, whether each is on the picture, and
   both ends in world cells and, where the camera can see them, in window pixels. */
static bool laser_project(const float* p, float* sx, float* sy)
{
    return project_point(p[0], p[1], p[2], g_fbW, g_fbH, sx, sy);
}
static void laser_dump(void)
{
    printf("LASER|frame=%d|on=%d|noted=%ld|listed=%zu|live=%d|drawn=%ld|mode=%s"
           "|classicrgb=%d,%d,%d;%d,%d,%d;%d,%d,%d\n",
           g_engineFrame, g_laserOn ? 1 : 0, g_laserNoted, g_lasers.size(), laser_live_count(),
           g_laserDrawn, FX_MODE_NAME[fx_mode_of(&g_fx)],
           g_laserClassicRGB[0][0], g_laserClassicRGB[0][1], g_laserClassicRGB[0][2],
           g_laserClassicRGB[1][0], g_laserClassicRGB[1][1], g_laserClassicRGB[1][2],
           g_laserClassicRGB[2][0], g_laserClassicRGB[2][1], g_laserClassicRGB[2][2]);
    for (size_t i = 0; i < g_lasers.size(); i++) {
        const LaserBeam& b = g_lasers[i];
        float life = 0.0f, S[3], T[3], ssx = -1, ssy = -1, tsx = -1, tsy = -1;
        const char* how = "?";
        const bool live = laser_live(b, &life);
        laser_source(b, fx_mode_of(&g_fx) == FX_MODE_ENHANCED, S, &how);
        laser_target(b, T);
        if (!g_fx.dos_classic && fx_mode_of(&g_fx) == FX_MODE_N64) {
            float C[3];
            laser_n64_tri(b, S, T, C);
            how = "cartridge";
        }
        if (g_fx.dos_classic) {
            /* Classic draws the engine's own two coordinates, flat */
            S[0] = b.l.sx; S[1] = 0.0f; S[2] = b.l.sz;
            T[0] = b.l.tx; T[1] = 0.0f; T[2] = b.l.tz;
            how = "engine";
        }
        laser_project(S, &ssx, &ssy);
        laser_project(T, &tsx, &tsy);
        printf("LASER|beam|type=%s|id=%d|kind=%d|born=%d|age=%d|frames=%d|live=%d|life=%.3f"
               "|src=%.3f,%.3f|tgt=%.3f,%.3f|tkind=%d|tid=%d|colour=%d,%d,%d|off=%d,%d,%d"
               "|from=%s|S=%.3f,%.3f,%.3f|T=%.3f,%.3f,%.3f|Spx=%.1f,%.1f|Tpx=%.1f,%.1f\n",
               b.l.type, b.l.id, b.l.kind, b.born, b.born >= 0 ? g_engineFrame - b.born : -1,
               b.l.frames, live ? 1 : 0, life, b.l.sx, b.l.sz, b.l.tx, b.l.tz, b.l.tkind,
               b.l.tid, b.l.colour[0], b.l.colour[1], b.l.colour[2], b.l.off[0], b.l.off[1],
               b.l.off[2], how, S[0], S[1], S[2], T[0], T[1], T[2], ssx, ssy, tsx, tsy);
    }
    fflush(stdout);
}

#endif
