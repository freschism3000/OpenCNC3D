/* The post chain's water normal, independent of the bank's native heightfield.
   Include at the end of water_mod.h after water_link/source/coast helpers. */
#ifndef OPENCNC3D_WATER_PLANE_RENDER_H
#define OPENCNC3D_WATER_PLANE_RENDER_H

static GLuint g_waterPlaneNormalProgram = 0;
static bool g_waterPlaneNormalTried = false;
static FxRT g_waterOpaqueLand;
static GLuint water_opaque_land_texture(void) { return g_waterOpaqueLand.tex; }

/* A render serial, not the engine tick: two draws of a paused frame still need
   independent ownership. g_drawFrameSerial is declared before this header and
   advances at draw_frame entry; the later foliage serial is not in scope here.
   Store the composed current matrix as well as the captured camera: the normal
   callback uses identity P + g_fxVP MV, unlike the colour pass's P/MV stacks. */
struct WaterOpaqueLandKey {
    unsigned frame, ground, holes, pad, surface, plane, details;
    GLuint sceneFbo, sceneTex, sceneDepth, maskFbo, maskTex;
    GLuint terrainAtlas, sourceAtlas, softAtlas;
    int sceneW, sceneH, maskW, maskH, viewport[4];
    int gridW, gridH, rect[4], margin, clip, cullOn, cullValid;
    int ra, sheetKey, sheetW, sheetH, sheetUW, sheetUH, texset, bilinear;
    int active, water, relief, experimentalRelief, experimentalTextures, unified;
    float base, reliefHeight, cull[4], capturedVP[16], currentVP[16];
    char scene[sizeof g_pack.scen];
};
static WaterOpaqueLandKey g_waterOpaqueLandKey;
static bool g_waterOpaqueLandKeyValid = false;

static WaterOpaqueLandKey water_opaque_land_key(void)
{
    WaterOpaqueLandKey key; memset(&key, 0, sizeof key);
    key.frame = g_drawFrameSerial; key.ground = g_groundGen;
    key.holes = g_cellHolesGen; key.pad = g_padEpoch;
    key.surface = terrain_surface_generation(); key.plane = water_plane_epoch();
    key.details = desert_detail_state();
    key.sceneFbo = g_fxScene.fbo; key.sceneTex = g_fxScene.tex; key.sceneDepth = g_fxScene.depth;
    key.sceneW = g_fxScene.w; key.sceneH = g_fxScene.h;
    key.maskFbo = g_waterOpaqueLand.fbo; key.maskTex = g_waterOpaqueLand.tex;
    key.maskW = g_waterOpaqueLand.w; key.maskH = g_waterOpaqueLand.h;
    key.terrainAtlas = g_terrainSurfaceAtlas;
    key.ra = water_src_ra() ? 1 : 0;
    if (key.ra) {
        key.sourceAtlas = g_waterRa.sheet.gl; key.sheetKey = g_waterRa.sheetKey;
        key.sheetW = g_waterRa.sheet.w; key.sheetH = g_waterRa.sheet.h;
        key.sheetUW = g_waterRa.sheet.uw; key.sheetUH = g_waterRa.sheet.uh;
    } else {
        const PackTex& atlas = g_pack.tex[terrain_atlas_index()];
        key.sourceAtlas = atlas.gl;
        key.sheetW = atlas.w; key.sheetH = atlas.h; key.sheetUW = atlas.uw; key.sheetUH = atlas.uh;
    }
    key.softAtlas = g_waterSoftTex;
    key.gridW = g_gridW; key.gridH = g_gridH;
    key.rect[0] = g_mapX; key.rect[1] = g_mapY; key.rect[2] = g_mapW; key.rect[3] = g_mapH;
    key.margin = g_terrainMargin; key.clip = g_clip_to_map ? 1 : 0;
    key.cullOn = g_cullOn ? 1 : 0; key.cullValid = g_cullValid ? 1 : 0;
    key.cull[0] = g_cullX0; key.cull[1] = g_cullX1; key.cull[2] = g_cullZ0; key.cull[3] = g_cullZ1;
    key.texset = fx_texset_get(); key.bilinear = g_fx.bilinear;
    key.active = g_fxActive; key.water = g_fx.water_fx; key.relief = g_fx.terrain_relief;
    key.experimentalRelief = g_fx.experimental_relief; key.experimentalTextures = g_fx.experimental_textures;
    key.unified = fx_unified_lighting() ? 1 : 0;
    key.base = g_terrainBase; key.reliefHeight = g_fx.terrain_relief_height;
    memcpy(key.scene, g_pack.scen, sizeof key.scene);
    memcpy(key.capturedVP, g_fxVP, sizeof key.capturedVP);
    GLfloat modelView[16], projection[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, modelView); glGetFloatv(GL_PROJECTION_MATRIX, projection);
    fx_mat_mul(projection, modelView, key.currentVP);
    glGetIntegerv(GL_VIEWPORT, key.viewport);
    return key;
}

static bool water_opaque_land_key_equal(const WaterOpaqueLandKey& a, const WaterOpaqueLandKey& b)
{
    return memcmp(&a, &b, sizeof a) == 0;
}

static bool water_opaque_land_can_reuse(const WaterOpaqueLandKey& key, bool cacheOn)
{
    return cacheOn && key.frame && g_waterOpaqueLandKeyValid && g_waterOpaqueLand.fbo
        && g_waterOpaqueLand.tex && water_opaque_land_key_equal(key, g_waterOpaqueLandKey);
}

/* Read the actual stored GPU mask on both hits and rebuilds. Diagnostics retain
   their format and current output path without making a cache hit draw terrain. */
static void water_opaque_land_dump(void)
{
    const char* path = getenv("OPENCNC3D_WATER_OPAQUE_DUMP");
    static bool seenFrame = false;
    static unsigned lastFrame = 0;
    const bool firstCall = !seenFrame || lastFrame != g_drawFrameSerial;
    seenFrame = true; lastFrame = g_drawFrameSerial;
    const char* earlyPath = firstCall ? getenv("OPENCNC3D_WATER_OPAQUE_DUMP_EARLY") : NULL;
    if ((!path && !earlyPath) || !g_waterOpaqueLand.tex) return;
    std::vector<unsigned char> pixels((size_t)g_waterOpaqueLand.w * g_waterOpaqueLand.h * 4);
    glPushAttrib(GL_TEXTURE_BIT); glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_waterOpaqueLand.tex);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glPixelStorei(GL_PACK_ALIGNMENT, 1); glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0); glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glPopClientAttrib(); glPopAttrib();
    const char* paths[2] = {path, earlyPath};
    for (int i = 0; i < 2; ++i) if (paths[i]) {
        FILE* file = fopen(paths[i], "wb");
        if (file) {
            const unsigned header[3] = {1u, (unsigned)g_waterOpaqueLand.w, (unsigned)g_waterOpaqueLand.h};
            fwrite("C3DOPAQ1", 1, 8, file); fwrite(header, sizeof(unsigned), 3, file);
            fwrite(pixels.data(), 1, pixels.size(), file); fclose(file);
        }
    }
}

/* A separate colour attachment records the production opaque-land prepass at
   the land's own projected height. The scene depth is borrowed, never sampled
   or written. Colour and normals share one mask only within the same rendered
   frame and exact camera/viewport/source/target identity. */
static bool water_opaque_land_mask(void)
{
    if (!g_fxScene.depth || g_fxScene.w < 1 || g_fxScene.h < 1) {
        g_waterOpaqueLandKeyValid = false; return false;
    }
    const char* cacheSwitch = getenv("OPENCNC3D_WATER_OPAQUE_CACHE");
    const char* cacheDisable = getenv("OPENCNC3D_WATER_OPAQUE_CACHE_DISABLE");
    const bool cacheOn = !(cacheSwitch && !strcmp(cacheSwitch, "0"))
        && !(cacheDisable && !strcmp(cacheDisable, "1"));
    const WaterOpaqueLandKey key = water_opaque_land_key();
    if (water_opaque_land_can_reuse(key, cacheOn)) {
        water_opaque_land_dump();
        if (getenv("OPENCNC3D_WATER_OPAQUE_CACHE_AUDIT"))
            fprintf(stderr, "WATER|opaque-cache|frame=%u|reuse=1\n", key.frame);
        return true;
    }
    g_waterOpaqueLandKeyValid = false;
    GLint framebuffer = 0, program = 0, matrixMode = 0, viewport[4];
    glGetIntegerv(0x8CA6 /* GL_FRAMEBUFFER_BINDING */, &framebuffer);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_MATRIX_MODE, &matrixMode);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glPushAttrib(GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_CURRENT_BIT | GL_DEPTH_BUFFER_BIT
                 | GL_COLOR_BUFFER_BIT | GL_PIXEL_MODE_BIT | GL_VIEWPORT_BIT
                 | GL_SCISSOR_BIT | GL_POLYGON_BIT);
    glMatrixMode(GL_PROJECTION); glPushMatrix();
    glMatrixMode(GL_MODELVIEW); glPushMatrix();
    glActiveTexture(GL_TEXTURE0);
    if (g_waterOpaqueLand.w != g_fxScene.w || g_waterOpaqueLand.h != g_fxScene.h)
        fx_rt_free(&g_waterOpaqueLand);
    if (!g_waterOpaqueLand.fbo) {
        g_waterOpaqueLand.w = g_fxScene.w; g_waterOpaqueLand.h = g_fxScene.h;
        fx_glGenFramebuffers(1, &g_waterOpaqueLand.fbo);
        glGenTextures(1, &g_waterOpaqueLand.tex);
        glBindTexture(GL_TEXTURE_2D, g_waterOpaqueLand.tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_waterOpaqueLand.w,
                     g_waterOpaqueLand.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }
    fx_glBindFramebuffer(GL_FRAMEBUFFER, g_waterOpaqueLand.fbo);
    fx_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                              g_waterOpaqueLand.tex, 0);
    fx_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                              g_fxScene.depth, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0); glReadBuffer(GL_COLOR_ATTACHMENT0);
    const bool complete = fx_glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (complete) {
        /* Match the caller's viewport/matrices exactly, including editor offsets.
           Clearing colour only cannot disturb the borrowed scene depth. */
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glDisable(GL_SCISSOR_TEST); glDisable(GL_STENCIL_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE);
        glDisable(GL_BLEND); glDisable(GL_LIGHTING);
        /* Same small bias as the normal pass: exact visible terrain must survive
           the fixed-pipeline/VP rounding, without changing its stored depth. */
        glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1.0f, -2.0f);
        fx_glUseProgram(0);
        water_opaque_land_draw();
    }
    /* Detach before sampling this colour texture from the normal framebuffer. */
    fx_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
    fx_glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)framebuffer);
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode((GLenum)matrixMode);
    fx_glUseProgram((GLuint)program);
    glPopAttrib();
    if (!complete) {
        fx_rt_free(&g_waterOpaqueLand);
        fprintf(stderr, "WATER|opaque-mask|incomplete; planar normals skipped\n");
    } else {
        /* Lazy terrain preparation may have advanced a source epoch while the
           mask was drawn. Commit the identity actually rendered, after all GL
           state is restored, including newly allocated target names. */
        g_waterOpaqueLandKey = water_opaque_land_key();
        g_waterOpaqueLandKeyValid = cacheOn;
        water_opaque_land_dump();
        if (getenv("OPENCNC3D_WATER_OPAQUE_CACHE_AUDIT"))
            fprintf(stderr, "WATER|opaque-cache|frame=%u|reuse=0\n", key.frame);
    }
    return complete;
}

static GLuint water_plane_normal_program(void)
{
    if (g_waterPlaneNormalTried) return g_waterPlaneNormalProgram;
    g_waterPlaneNormalTried = true;
    static const char* vs =
        "#version 120\n"
        "void main(){gl_Position=gl_ModelViewProjectionMatrix*gl_Vertex;}\n";
    static const char* fs =
        "#version 120\n"
        "uniform sampler2D uOpaqueLand; uniform vec2 uOpaqueInvSize;\n"
        "void main(){vec2 p=(floor(gl_FragCoord.xy)+0.5)*uOpaqueInvSize;"
        "if(texture2D(uOpaqueLand,p).a>0.5)discard;"
        "gl_FragColor=vec4(0.5,1.0,0.5,0.28);}\n";
    g_waterPlaneNormalProgram = water_link(vs, fs, "planar water normal");
    return g_waterPlaneNormalProgram;
}

/* Called after terrain/physical-bank normals and before object normals.  The
   caller owns the normal target and scene-depth attachment.  Use the exact
   plane coverage as the water colour pass, with actual screen-space opaque land
   ownership taking priority even when a dry cliff lies below a nearby plane.
   Partial-alpha projected bank feathers do not own depth and remain water.
   Object normals drawn afterward still take priority. */
static void water_plane_draw_normals(void)
{
    if (!water_fx_wanted() || !water_plane_on()) return;
    const GLuint program = water_plane_normal_program();
    if (!program) return;
    if (!water_opaque_land_mask()) return;
    GLint oldProgram = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    glPushAttrib(GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_CURRENT_BIT | GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, g_waterOpaqueLand.tex);
    glDisable(GL_ALPHA_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE); glDisable(GL_LIGHTING);
    glDisable(GL_POLYGON_OFFSET_FILL);
    // The bed now lies at physical depth. Post lighting/fog must reconstruct
    // the visible water surface rather than that distant opaque floor.
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE);
    fx_glUseProgram(program);
    fx_set1i(program, "uOpaqueLand", 0);
    fx_set2f(program, "uOpaqueInvSize", 1.0f / g_waterOpaqueLand.w, 1.0f / g_waterOpaqueLand.h);
    const std::vector<PackCell>& cells = water_cells();
    static const int dx[6] = {0,0,1,1,0,1}, dz[6] = {0,1,0,0,1,1};
    glBegin(GL_TRIANGLES);
    for (size_t i = 0; i < cells.size(); ++i) {
        const PackCell& c = cells[i];
        if ((!water_plane_covers(c) && !water_cell_wet(c)) || !cell_shown(c.x, c.y) || !cell_in_view(c.x, c.y)) continue;
        for (int k = 0; k < 6; ++k) {
            const int x = c.x + dx[k], z = c.y + dz[k];
            glVertex3f((float)x, water_plane_cell_y(c, x, z), (float)z);
        }
    }
    glEnd();
    fx_glUseProgram((GLuint)oldProgram);
    glPopAttrib();
}

#endif
