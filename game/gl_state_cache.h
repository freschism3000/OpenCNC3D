/* ---------------------------------------------------------------------------------- *
 *  GL STATE THE RENDERER ALREADY KNOWS, kept on the CPU so it is not asked for.
 *
 *  Every glGet* and glIsEnabled is a question to the driver, and on a driver that runs
 *  its own worker thread (NVIDIA's threaded optimisation on Windows is the common case)
 *  the answer waits until that thread has executed every command queued before it. A
 *  frame that asks hundreds of times serialises the game and the driver hundreds of
 *  times. Measured on the 1995 GDI 1 map under Enhanced: about 1,900 queries a frame,
 *  1,700 of them from the model batcher (mesh_batch.h) asking the same four things for
 *  every model it draws, while two Windows PCs with current NVIDIA cards spent 40 to
 *  50 ms a frame on it with the GPU idle, and the same frame took under 10 ms on a Mac.
 *
 *  Three answers are cached here, each with the rule that makes it stale:
 *
 *    the clip matrix (projection x modelview) the batcher culls against: stale after
 *      anything that changes either matrix, counted by g_glsMatEpoch, which the macros
 *      below bump on every fixed-function matrix call in this translation unit;
 *    GL_CULL_FACE and GL_POLYGON_OFFSET_FILL: set by glEnable/glDisable, stale after
 *      glPopAttrib, which can restore either behind our back;
 *    the GL_ARRAY_BUFFER binding: set by gr_glBindBuffer (grass_bake.h), stale after
 *      glPopClientAttrib and after a glDeleteBuffers of the bound buffer.
 *
 *  Anything the cache cannot see (another translation unit drawing between frames, a
 *  library touching the context) is covered by gls_frame_begin(), which forgets all of
 *  it at the top of every draw_frame. A forgotten answer is asked for once, then known
 *  again: the cache can cost a query, never return a wrong value.
 *
 *  Included after every system GL header, because the macros would otherwise rewrite
 *  the headers' own declarations. Tier 1 is a separate program and never sees this.
 * ---------------------------------------------------------------------------------- */
#ifndef OPENCNC3D_GL_STATE_CACHE_H
#define OPENCNC3D_GL_STATE_CACHE_H

/* The frame profiler (cnc_eyes.cpp, OPENCNC3D_FRAMEPROF), declared here so the post chain
   and the water, which are included before it, can split their sections. */
static void fprof_mark(const char* name);
static bool mesh_perf_switch(const char* name);

/* ---- the matrices ---- */
static unsigned g_glsMatEpoch = 1;
static unsigned g_glsClipEpoch = 0;
static float    g_glsClip[16];

/* Each is the real call with the epoch bumped first. A function-like macro is not
   expanded again inside its own replacement, so the inner name is the GL function. */
#define glLoadIdentity()          (g_glsMatEpoch++, glLoadIdentity())
#define glLoadMatrixf(m)          (g_glsMatEpoch++, glLoadMatrixf(m))
#define glLoadMatrixd(m)          (g_glsMatEpoch++, glLoadMatrixd(m))
#define glMultMatrixf(m)          (g_glsMatEpoch++, glMultMatrixf(m))
#define glMultMatrixd(m)          (g_glsMatEpoch++, glMultMatrixd(m))
#define glTranslatef(x, y, z)     (g_glsMatEpoch++, glTranslatef(x, y, z))
#define glTranslated(x, y, z)     (g_glsMatEpoch++, glTranslated(x, y, z))
#define glRotatef(a, x, y, z)     (g_glsMatEpoch++, glRotatef(a, x, y, z))
#define glRotated(a, x, y, z)     (g_glsMatEpoch++, glRotated(a, x, y, z))
#define glScalef(x, y, z)         (g_glsMatEpoch++, glScalef(x, y, z))
#define glScaled(x, y, z)         (g_glsMatEpoch++, glScaled(x, y, z))
#define glOrtho(l, r, b, t, n, f) (g_glsMatEpoch++, glOrtho(l, r, b, t, n, f))
#define glFrustum(l, r, b, t, n, f) (g_glsMatEpoch++, glFrustum(l, r, b, t, n, f))
#define glPopMatrix()             (g_glsMatEpoch++, glPopMatrix())

/* Projection x modelview, column-major like GL's own, asked for only when a matrix has
   changed since the last answer. */
static const float* gls_clip(void)
{
    if (g_glsClipEpoch != g_glsMatEpoch) {
        float m[16], p[16];
        glGetFloatv(GL_MODELVIEW_MATRIX, m);
        glGetFloatv(GL_PROJECTION_MATRIX, p);
        for (int col = 0; col < 4; col++) for (int row = 0; row < 4; row++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++) s += p[k * 4 + row] * m[col * 4 + k];
            g_glsClip[col * 4 + row] = s;
        }
        g_glsClipEpoch = g_glsMatEpoch;
    }
    return g_glsClip;
}

/* A draw that owns a modelview push/pop and leaves projection untouched can
   restore the exact previously queried clip answer after popping. No CPU matrix
   mirror or multiplication is introduced. Unknown answers remain unknown. */
struct GlsClipRestore {
    bool known;
    float clip[16];
    GlsClipRestore():known(g_glsClipEpoch==g_glsMatEpoch) {
        if(known)memcpy(clip,g_glsClip,sizeof clip);
    }
    void restored() const {
        if(!known)return;
        memcpy(g_glsClip,clip,sizeof clip);g_glsClipEpoch=g_glsMatEpoch;
    }
};

/* ---- two capabilities ---- */
struct GlsCap { bool known, on; };
static GlsCap g_glsCull = { false, false }, g_glsOffset = { false, false };
static GlsCap g_glsBlend={false,false}, g_glsDepth={false,false}, g_glsAlpha={false,false};
struct GlsTextureUnit { bool boundKnown=false; GLuint bound=0; GlsCap enabled={false,false}; };
static GlsTextureUnit g_glsTexture[32];
static GlsCap g_glsVertex={false,false},g_glsColor={false,false},g_glsNormal={false,false},g_glsTexArray[32];
static bool g_glsClientKnown=false;static GLenum g_glsClient=GL_TEXTURE0;
static bool g_glsHasMultitex=false;
static void gls_texture_limits();
static void gls_forget_clients(){
    g_glsVertex.known=g_glsColor.known=g_glsNormal.known=false;g_glsClientKnown=false;
    for(auto& c:g_glsTexArray)c.known=false;
}
static GlsCap* gls_client_cap(GLenum cap) {
    if(cap==GL_VERTEX_ARRAY)return &g_glsVertex;
    if(cap==GL_COLOR_ARRAY)return &g_glsColor;
    if(cap==GL_NORMAL_ARRAY)return &g_glsNormal;
    if(cap==GL_TEXTURE_COORD_ARRAY) {
        if(!g_glsClientKnown){
            gls_texture_limits();GLint unit=GL_TEXTURE0;
            if(g_glsHasMultitex)glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE,&unit);
            g_glsClient=(GLenum)unit;g_glsClientKnown=true;
        }
        if(g_glsClient>=GL_TEXTURE0&&g_glsClient<GL_TEXTURE0+32)return &g_glsTexArray[g_glsClient-GL_TEXTURE0];
    }
    return NULL;
}
static bool g_glsActiveKnown=false;
static GLenum g_glsActive=GL_TEXTURE0;
static GLint g_glsActiveLimit=0,g_glsImageLimit=0,g_glsClientLimit=0,g_glsFixedLimit=0;
static SDL_GLContext g_glsLimitsContext=NULL;
#ifndef GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS
#define GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS 0x8B4D
#endif
#ifndef GL_MAX_TEXTURE_COORDS
#define GL_MAX_TEXTURE_COORDS 0x8871
#endif
static void gls_texture_limits() {
    if(g_glsFixedLimit)return;
    int major=0,minor=0;const char* v=(const char*)glGetString(GL_VERSION);
    if(v)sscanf(v,"%d.%d",&major,&minor);
    g_glsHasMultitex=major>1||(major==1&&minor>=3);
    if(!g_glsHasMultitex) {
        const char* ext=(const char*)glGetString(GL_EXTENSIONS);
        g_glsHasMultitex=ext&&strstr(ext,"GL_ARB_multitexture");
    }
    g_glsFixedLimit=1;
    if(g_glsHasMultitex)glGetIntegerv(GL_MAX_TEXTURE_UNITS,&g_glsFixedLimit);
    g_glsActiveLimit=g_glsImageLimit=g_glsClientLimit=g_glsFixedLimit;
    if(major>=2) {
        glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,&g_glsImageLimit);
        glGetIntegerv(GL_MAX_TEXTURE_COORDS,&g_glsClientLimit);
        g_glsActiveLimit=std::max(g_glsImageLimit,g_glsClientLimit);
    }
}
static std::unordered_map<uint64_t, GLint> g_glsWrap;
static bool gls_reduce_calls() {
    static const bool on=[](){const char* p=getenv("OPENCNC3D_STATE_CACHE");return !p||strcmp(p,"0");}();
    return on;
}
static GlsTextureUnit* gls_texture_unit() {
    gls_texture_limits();
    if (!g_glsActiveKnown) {
        GLint unit=GL_TEXTURE0;
        if(g_glsHasMultitex)glGetIntegerv(GL_ACTIVE_TEXTURE,&unit);
        g_glsActive=(GLenum)unit; g_glsActiveKnown=true;
    }
    return g_glsActive>=GL_TEXTURE0 && g_glsActive<GL_TEXTURE0+32
        && g_glsActive<GL_TEXTURE0+g_glsImageLimit ? &g_glsTexture[g_glsActive-GL_TEXTURE0] : NULL;
}
static void gls_forget_textures() {
    g_glsActiveKnown=false;
    for(auto& u:g_glsTexture) {u.boundKnown=false;u.enabled.known=false;}
    g_glsWrap.clear();
}
static inline void gls_active_texture(GLenum unit) {
    gls_texture_limits();
    /* Binding and client coordinates have different limits from fixed-function
       texture enables. Invalid selectors leave the previous selector unchanged. */
    if (unit<GL_TEXTURE0 || unit>=GL_TEXTURE0+g_glsActiveLimit) {
        glActiveTexture(unit); return;
    }
    if (!gls_reduce_calls() || !g_glsActiveKnown || g_glsActive!=unit) glActiveTexture(unit);
    g_glsActive=unit;g_glsActiveKnown=true;
}
static inline void gls_bind_texture(GLenum target, GLuint name) {
    GlsTextureUnit* u=target==GL_TEXTURE_2D?gls_texture_unit():NULL;
    if (!u || !gls_reduce_calls() || !u->boundKnown || u->bound!=name) glBindTexture(target,name);
    if(u){u->boundKnown=true;u->bound=name;}
}
static inline void gls_tex_parameter(GLenum target, GLenum parameter, GLint value) {
    if (target==GL_TEXTURE_2D && (parameter==GL_TEXTURE_WRAP_S || parameter==GL_TEXTURE_WRAP_T)) {
        GlsTextureUnit* u=gls_texture_unit();
        if (u && u->boundKnown) {
            const uint64_t key=((uint64_t)u->bound<<32)|parameter;
            auto i=g_glsWrap.find(key);
            if (gls_reduce_calls() && i!=g_glsWrap.end() && i->second==value) return;
            glTexParameteri(target,parameter,value);g_glsWrap[key]=value;return;
        }
    }
    glTexParameteri(target,parameter,value);
}
static inline void gls_tex_parameterf(GLenum target, GLenum parameter, GLfloat value) {
    if(parameter==GL_TEXTURE_WRAP_S || parameter==GL_TEXTURE_WRAP_T) g_glsWrap.clear();
    glTexParameterf(target,parameter,value);
}
/* Scalar fixed-function texture environment state belongs to the active server
   texture unit, not the texture object. Only recognized legal scalar values are
   remembered. All unsupported targets, parameters and values reach GL unchanged.
   No colour vector, shader value, material or texture pixel is altered. */
struct GlsEnvScalar { bool known=false; GLint value=0; };
static GlsEnvScalar g_glsEnv[32][17];
static bool gls_env_reduce_calls() {
    static const bool on=[](){const char* p=getenv("OPENCNC3D_TEXENV_CACHE");return !p||strcmp(p,"0");}();
    return on && gls_reduce_calls();
}
static void gls_forget_env() {
    for(auto& unit:g_glsEnv)for(auto& p:unit)p.known=false;
}
static int gls_env_slot(GLenum p) {
    switch(p) {
    case GL_TEXTURE_ENV_MODE:return 0;
    case GL_COMBINE_RGB:return 1;case GL_COMBINE_ALPHA:return 2;
    case GL_SOURCE0_RGB:return 3;case GL_SOURCE1_RGB:return 4;case GL_SOURCE2_RGB:return 5;
    case GL_SOURCE0_ALPHA:return 6;case GL_SOURCE1_ALPHA:return 7;case GL_SOURCE2_ALPHA:return 8;
    case GL_OPERAND0_RGB:return 9;case GL_OPERAND1_RGB:return 10;case GL_OPERAND2_RGB:return 11;
    case GL_OPERAND0_ALPHA:return 12;case GL_OPERAND1_ALPHA:return 13;case GL_OPERAND2_ALPHA:return 14;
    case GL_RGB_SCALE:return 15;case GL_ALPHA_SCALE:return 16;
    default:return -1;
    }
}
static bool gls_env_value(GLenum p,GLint v) {
    switch(p) {
    case GL_TEXTURE_ENV_MODE:return v==GL_MODULATE||v==GL_DECAL||v==GL_BLEND||v==GL_REPLACE||v==GL_ADD||v==GL_COMBINE;
    case GL_COMBINE_RGB:return v==GL_REPLACE||v==GL_MODULATE||v==GL_ADD||v==GL_ADD_SIGNED||v==GL_INTERPOLATE||v==GL_SUBTRACT||v==GL_DOT3_RGB||v==GL_DOT3_RGBA;
    case GL_COMBINE_ALPHA:return v==GL_REPLACE||v==GL_MODULATE||v==GL_ADD||v==GL_ADD_SIGNED||v==GL_INTERPOLATE||v==GL_SUBTRACT;
    case GL_SOURCE0_RGB:case GL_SOURCE1_RGB:case GL_SOURCE2_RGB:
    case GL_SOURCE0_ALPHA:case GL_SOURCE1_ALPHA:case GL_SOURCE2_ALPHA:
        return v==GL_TEXTURE||v==GL_CONSTANT||v==GL_PRIMARY_COLOR||v==GL_PREVIOUS;
    case GL_OPERAND0_RGB:case GL_OPERAND1_RGB:case GL_OPERAND2_RGB:
        return v==GL_SRC_COLOR||v==GL_ONE_MINUS_SRC_COLOR||v==GL_SRC_ALPHA||v==GL_ONE_MINUS_SRC_ALPHA;
    case GL_OPERAND0_ALPHA:case GL_OPERAND1_ALPHA:case GL_OPERAND2_ALPHA:
        return v==GL_SRC_ALPHA||v==GL_ONE_MINUS_SRC_ALPHA;
    case GL_RGB_SCALE:case GL_ALPHA_SCALE:return v==1||v==2||v==4;
    default:return false;
    }
}
static GlsEnvScalar* gls_env_scalar(GLenum target,GLenum p) {
    if(target!=GL_TEXTURE_ENV || !gls_env_reduce_calls())return NULL;
    const int slot=gls_env_slot(p);if(slot<0)return NULL;
    gls_texture_limits();gls_texture_unit();
    /* Combine requires GL1.3. An older context retains the unchanged driver path,
       including its unsupported-enum errors. The normal desktop context is2.1. */
    static SDL_GLContext checked=NULL;static bool supported=false;
    SDL_GLContext context=g_glsLimitsContext;
    if(checked!=context) {
        checked=context;int major=0,minor=0;const char* v=(const char*)glGetString(GL_VERSION);
        if(v)sscanf(v,"%d.%d",&major,&minor);
        supported=major>1||(major==1&&minor>=3);
    }
    if(!supported || !g_glsActiveKnown || g_glsActive<GL_TEXTURE0 || g_glsActive>=GL_TEXTURE0+32
       ||g_glsActive>=GL_TEXTURE0+g_glsFixedLimit)return NULL;
    return &g_glsEnv[g_glsActive-GL_TEXTURE0][slot];
}
static inline void gls_tex_env_i(GLenum target,GLenum p,GLint value) {
    GlsEnvScalar* s=gls_env_scalar(target,p);
    if(!s||!gls_env_value(p,value)){glTexEnvi(target,p,value);if(s)s->known=false;return;}
    if(!s->known||s->value!=value)glTexEnvi(target,p,value);
    s->known=true;s->value=value;
}
static inline void gls_tex_env_f(GLenum target,GLenum p,GLfloat value) {
    GlsEnvScalar* s=gls_env_scalar(target,p);
    /* Avoid converting NaN, infinities, huge floats or rounded enum values. */
    const bool integral=std::isfinite(value)&&value>=0.0f&&value<=65536.0f
        &&value==(GLfloat)(GLint)value;
    if(!s||!integral||!gls_env_value(p,(GLint)value)){glTexEnvf(target,p,value);if(s)s->known=false;return;}
    const GLint i=(GLint)value;
    if(!s->known||s->value!=i)glTexEnvf(target,p,value);
    s->known=true;s->value=i;
}
static inline void gls_tex_env_iv(GLenum target,GLenum p,const GLint* value) {
    if(gls_env_slot(p)>=0&&value)gls_tex_env_i(target,p,value[0]);
    else glTexEnviv(target,p,value);
}
static inline void gls_tex_env_fv(GLenum target,GLenum p,const GLfloat* value) {
    if(gls_env_slot(p)>=0&&value)gls_tex_env_f(target,p,value[0]);
    else glTexEnvfv(target,p,value);
}
static inline void gls_get_tex_env_i(GLenum target,GLenum p,GLint* value) {
    GlsEnvScalar* s=gls_env_scalar(target,p);
    if(s&&s->known&&value){*value=s->value;return;}
    glGetTexEnviv(target,p,value);
    if(s&&value&&gls_env_value(p,*value)){s->known=true;s->value=*value;}
}
static inline void gls_get_tex_env_f(GLenum target,GLenum p,GLfloat* value) {
    GlsEnvScalar* s=gls_env_scalar(target,p);
    if(s&&s->known&&value){*value=(GLfloat)s->value;return;}
    glGetTexEnvfv(target,p,value);
    if(s&&value&&std::isfinite(*value)&&*value>=0&&*value<=65536
        &&*value==(GLfloat)(GLint)*value&&gls_env_value(p,(GLint)*value)){
        s->known=true;s->value=(GLint)*value;
    }
}

/* ---- writes to one watched texture ----
   The relief surface (terrain_surface.h) keeps the coast cut it read back from the
   terrain atlas across building placements, instead of reading the whole atlas again.
   That copy is valid exactly as long as the texture it came from is not written, so
   every upload, copy or delete in this translation unit that reaches the watched name
   moves g_glsWatchGen, and the copy is read again. One name at a time is enough: only
   the atlas the current surface was built from is ever watched. Nothing renders into
   a terrain atlas, so a framebuffer attachment is not a write this has to see.
   A NEW WAY OF FILLING AN ATLAS must come through these macros (this translation unit,
   glTexImage2D/glTexSubImage2D/glCopyTex*Image2D, deletes through gls_delete_textures),
   or it must bump g_glsWatchGen itself: an upload from another file, through a function
   pointer, as compressed data or by rendering would leave the kept coast cut stale.
   OPENCNC3D_SURFACE_VERIFY=1 compares the kept cut with a fresh readback. */
static GLuint   g_glsWatchTex = 0;
static unsigned g_glsWatchGen = 1;
static inline void gls_watch_write(GLenum target)
{
    if (!g_glsWatchTex || target != GL_TEXTURE_2D) return;
    GlsTextureUnit* u = gls_texture_unit();
    GLuint bound = 0;
    if (u && u->boundKnown) bound = u->bound;
    else { GLint b = 0; glGetIntegerv(GL_TEXTURE_BINDING_2D, &b); bound = (GLuint)b; }
    if (bound == g_glsWatchTex) g_glsWatchGen++;
}
static inline void gls_delete_textures(GLsizei n, const GLuint* names) {
    if (g_glsWatchTex && names)
        for (GLsizei i = 0; i < n; i++) if (names[i] == g_glsWatchTex) g_glsWatchGen++;
    glDeleteTextures(n,names);gls_forget_textures();
}

static inline void gls_client_active(GLenum unit) {
    gls_texture_limits();
    if(unit<GL_TEXTURE0||unit>=GL_TEXTURE0+g_glsClientLimit){glClientActiveTexture(unit);return;}
    if(!gls_reduce_calls()||!g_glsClientKnown||g_glsClient!=unit)glClientActiveTexture(unit);
    g_glsClient=unit;g_glsClientKnown=true;
}
static inline void gls_client_enable(GLenum cap,bool on) {
    GlsCap* c=gls_client_cap(cap);
    if(!c||!gls_reduce_calls()||!c->known||c->on!=on){if(on)glEnableClientState(cap);else glDisableClientState(cap);}
    if(c){c->known=true;c->on=on;}
}
static inline GlsCap* gls_cap(GLenum cap)
{
    if(cap==GL_CULL_FACE)return &g_glsCull;
    if(cap==GL_POLYGON_OFFSET_FILL)return &g_glsOffset;
    if(cap==GL_BLEND)return &g_glsBlend;
    if(cap==GL_DEPTH_TEST)return &g_glsDepth;
    if(cap==GL_ALPHA_TEST)return &g_glsAlpha;
    if(cap==GL_TEXTURE_2D){
        gls_texture_limits();auto* u=gls_texture_unit();
        return u&&g_glsActive<GL_TEXTURE0+g_glsFixedLimit?&u->enabled:NULL;
    }
    return NULL;
}
static inline void gls_enable(GLenum cap)
{
    GlsCap* c=gls_cap(cap);
    if (!c || !gls_reduce_calls() || !c->known || !c->on) glEnable(cap);
    if (c) { c->known = true; c->on = true; }
}
static inline void gls_disable(GLenum cap)
{
    GlsCap* c=gls_cap(cap);
    if (!c || !gls_reduce_calls() || !c->known || c->on) glDisable(cap);
    if (c) { c->known = true; c->on = false; }
}
static inline GLboolean gls_is_enabled(GLenum cap)
{
    GlsCap* c = gls_cap(cap);
    if(!c)c=gls_client_cap(cap);
    if (!c) return glIsEnabled(cap);
    if (!c->known) { c->on = glIsEnabled(cap) == GL_TRUE; c->known = true; }
    return c->on ? GL_TRUE : GL_FALSE;
}
struct GlsSelectorSave { GLbitfield mask; bool known; GLenum unit; };
static std::vector<GlsSelectorSave> g_glsServerStack,g_glsClientStack;
static GLint g_glsServerStackLimit=0,g_glsClientStackLimit=0;
static inline void gls_push_attrib(GLbitfield mask)
{
    if(!g_glsServerStackLimit)glGetIntegerv(GL_MAX_ATTRIB_STACK_DEPTH,&g_glsServerStackLimit);
    glPushAttrib(mask);
    if(g_glsServerStack.size()<(size_t)g_glsServerStackLimit)
        g_glsServerStack.push_back({mask,g_glsActiveKnown,g_glsActive});
}
/* Polygon scalars used by the building/wall guards. These exact GL1.x state
   values are already set by this translation unit. Keep them until an attribute
   restore, context/frame boundary or invalid/unsuitable setter makes them unknown.
   Unknown values are queried once; no matrix/depth arithmetic is changed. */
struct GlsPolygonState {
    bool cullKnown=false,frontKnown=false,offsetKnown=false;
    GLint cull=GL_BACK,front=GL_CCW;
    GLfloat factor=0.0f,units=0.0f;
};
static GlsPolygonState g_glsPolygon;
static inline void gls_forget_polygon() {
    g_glsPolygon.cullKnown=g_glsPolygon.frontKnown=g_glsPolygon.offsetKnown=false;
}
static inline GLint gls_cull_mode() {
    if(!gls_reduce_calls()||!g_glsPolygon.cullKnown) {
        glGetIntegerv(GL_CULL_FACE_MODE,&g_glsPolygon.cull);g_glsPolygon.cullKnown=true;
    }
    return g_glsPolygon.cull;
}
static inline GLint gls_front_face() {
    if(!gls_reduce_calls()||!g_glsPolygon.frontKnown) {
        glGetIntegerv(GL_FRONT_FACE,&g_glsPolygon.front);g_glsPolygon.frontKnown=true;
    }
    return g_glsPolygon.front;
}
static inline void gls_polygon_offset_values(GLfloat* factor,GLfloat* units) {
    if(!gls_reduce_calls()||!g_glsPolygon.offsetKnown) {
        glGetFloatv(GL_POLYGON_OFFSET_FACTOR,&g_glsPolygon.factor);
        glGetFloatv(GL_POLYGON_OFFSET_UNITS,&g_glsPolygon.units);g_glsPolygon.offsetKnown=true;
    }
    *factor=g_glsPolygon.factor;*units=g_glsPolygon.units;
}
static inline void gls_cull_face(GLenum mode) {
    /* Invalid enums must reach GL, leave its error semantics intact and retain the
       last valid state. All callers are outside glBegin/glEnd. */
    if(mode!=GL_FRONT&&mode!=GL_BACK&&mode!=GL_FRONT_AND_BACK){glCullFace(mode);return;}
    if(!gls_reduce_calls()||!g_glsPolygon.cullKnown||g_glsPolygon.cull!=(GLint)mode)glCullFace(mode);
    g_glsPolygon.cull=(GLint)mode;g_glsPolygon.cullKnown=true;
}
static inline void gls_front_face_set(GLenum mode) {
    if(mode!=GL_CW&&mode!=GL_CCW){glFrontFace(mode);return;}
    if(!gls_reduce_calls()||!g_glsPolygon.frontKnown||g_glsPolygon.front!=(GLint)mode)glFrontFace(mode);
    g_glsPolygon.front=(GLint)mode;g_glsPolygon.frontKnown=true;
}
static inline void gls_polygon_offset(GLfloat factor,GLfloat units) {
    if(!std::isfinite(factor)||!std::isfinite(units)) {
        glPolygonOffset(factor,units);g_glsPolygon.offsetKnown=false;return;
    }
    const bool same=g_glsPolygon.offsetKnown
        &&memcmp(&g_glsPolygon.factor,&factor,sizeof factor)==0
        &&memcmp(&g_glsPolygon.units,&units,sizeof units)==0;
    if(!gls_reduce_calls()||!same)glPolygonOffset(factor,units);
    g_glsPolygon.factor=factor;g_glsPolygon.units=units;g_glsPolygon.offsetKnown=true;
}

static inline void gls_pop_attrib(void)
{
    bool known=false;GLenum unit=GL_TEXTURE0;
    if(!g_glsServerStack.empty()) {
        const auto save=g_glsServerStack.back();g_glsServerStack.pop_back();
        known=(save.mask&GL_TEXTURE_BIT)?save.known:g_glsActiveKnown;
        unit=(save.mask&GL_TEXTURE_BIT)?save.unit:g_glsActive;
    }
    glPopAttrib();
    gls_forget_polygon();gls_forget_env();
    g_glsCull.known = g_glsOffset.known = false;
    g_glsBlend.known=g_glsDepth.known=g_glsAlpha.known=false;
    gls_forget_textures();
    /* The paired push already recorded the selector that GL restored. Keep
       bindings/enables conservative without asking the driver for this again. */
    g_glsActiveKnown=known;g_glsActive=unit;
}

/* ---- the array buffer binding ---- */
static bool   g_glsArrayKnown = false;
static GLuint g_glsArray = 0;

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER          0x8892
#endif
#ifndef GL_ARRAY_BUFFER_BINDING
#define GL_ARRAY_BUFFER_BINDING  0x8894
#endif

static inline GLuint gls_array_binding(void)
{
    if (!g_glsArrayKnown) {
        GLint v = 0;
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &v);
        g_glsArray = (GLuint)v;
        g_glsArrayKnown = true;
    }
    return g_glsArray;
}
static inline void gls_push_client_attrib(GLbitfield mask)
{
    if(!g_glsClientStackLimit)glGetIntegerv(GL_MAX_CLIENT_ATTRIB_STACK_DEPTH,&g_glsClientStackLimit);
    glPushClientAttrib(mask);
    if(g_glsClientStack.size()<(size_t)g_glsClientStackLimit)
        g_glsClientStack.push_back({mask,g_glsClientKnown,g_glsClient});
}
static inline void gls_pop_client_attrib(void)
{
    bool known=false;GLenum unit=GL_TEXTURE0;
    if(!g_glsClientStack.empty()) {
        const auto save=g_glsClientStack.back();g_glsClientStack.pop_back();
        known=(save.mask&GL_CLIENT_VERTEX_ARRAY_BIT)?save.known:g_glsClientKnown;
        unit=(save.mask&GL_CLIENT_VERTEX_ARRAY_BIT)?save.unit:g_glsClient;
    }
    glPopClientAttrib();
    gls_forget_clients();
    g_glsClientKnown=known;g_glsClient=unit;
    g_glsArrayKnown = false;
}

/* ---- the bound program ----
   Every program bind in this file goes through fx_glUseProgram, which fx_gl.h routes to
   gls_use_program, so the bound program is known without asking: the building normal
   pass used to ask the driver for it at every textured run (stone_mesh_normal_bind), and
   each object pass asks again before it binds its own. A bind of the program already
   bound is a no-op in GL, so it is skipped too. Linking or deleting any program bumps
   g_glsProgramEpoch, which is what cached uniform locations are checked against.
   OPENCNC3D_PROGRAM_SHADOW=0 asks and binds every time, as before;
   OPENCNC3D_PROGRAM_SHADOW_VERIFY=1 asks anyway and reports any disagreement. */
#ifndef GL_CURRENT_PROGRAM
#define GL_CURRENT_PROGRAM 0x8B8D
#endif
static bool     g_glsProgramKnown = false;
static GLuint   g_glsProgram = 0;
static unsigned g_glsProgramEpoch = 1;
static unsigned long g_glsProgramBindsSkipped = 0, g_glsProgramQueriesSkipped = 0, g_glsProgramMismatches = 0;
static bool gls_program_shadow(void)
{
    static const bool on = mesh_perf_switch("OPENCNC3D_PROGRAM_SHADOW");
    return on;
}
static bool gls_program_verify(void)
{
    static const bool on = [](){ const char* p = getenv("OPENCNC3D_PROGRAM_SHADOW_VERIFY"); return p && !strcmp(p, "1"); }();
    return on;
}
static void gls_program_check(const char* where)
{
    GLint p = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &p);
    if ((GLuint)p != g_glsProgram) {
        if (g_glsProgramMismatches < 8)
            fprintf(stderr, "GLSHADOW|program|mismatch|%s|known=%u|bound=%d\n", where, g_glsProgram, (int)p);
        g_glsProgramMismatches++;
    }
}
static inline void gls_get_integerv(GLenum pname, GLint* out)
{
    if (pname == GL_CURRENT_PROGRAM && gls_program_shadow()) {
        if (g_glsProgramKnown) {
            if (gls_program_verify()) gls_program_check("query");
            *out = (GLint)g_glsProgram; g_glsProgramQueriesSkipped++;
            return;
        }
        glGetIntegerv(pname, out);
        g_glsProgram = (GLuint)*out; g_glsProgramKnown = true;
        return;
    }
    glGetIntegerv(pname, out);
}
#define glGetIntegerv(pname, out) gls_get_integerv(pname, out)

#define glCullFace(mode) gls_cull_face(mode)
#define glFrontFace(mode) gls_front_face_set(mode)
#define glPolygonOffset(factor,units) gls_polygon_offset(factor,units)
#define glEnable(cap)       gls_enable(cap)
#define glDisable(cap)      gls_disable(cap)
#define glIsEnabled(cap)    gls_is_enabled(cap)
#define glPushAttrib(mask)  gls_push_attrib(mask)
#define glPopAttrib()       gls_pop_attrib()
#define glPushClientAttrib(mask) gls_push_client_attrib(mask)
#define glPopClientAttrib() gls_pop_client_attrib()
#undef glClientActiveTexture
#define glClientActiveTexture(unit) gls_client_active(unit)
#define glEnableClientState(cap) gls_client_enable(cap,true)
#define glDisableClientState(cap) gls_client_enable(cap,false)
#undef glActiveTexture
#define glActiveTexture(unit) gls_active_texture(unit)
#define glBindTexture(target,name) gls_bind_texture(target,name)
#undef glTexEnvi
#undef glTexEnvf
#undef glGetTexEnviv
#undef glTexEnviv
#undef glTexEnvfv
#undef glGetTexEnvfv
#define glTexEnvi(target,param,value) gls_tex_env_i(target,param,value)
#define glTexEnvf(target,param,value) gls_tex_env_f(target,param,value)
#define glTexEnviv(target,param,value) gls_tex_env_iv(target,param,value)
#define glTexEnvfv(target,param,value) gls_tex_env_fv(target,param,value)
#define glGetTexEnviv(target,param,value) gls_get_tex_env_i(target,param,value)
#define glGetTexEnvfv(target,param,value) gls_get_tex_env_f(target,param,value)
#define glTexParameteri(target,param,value) gls_tex_parameter(target,param,value)
#define glTexParameterf(target,param,value) gls_tex_parameterf(target,param,value)
#define glDeleteTextures(n,names) gls_delete_textures(n,names)
#define glTexImage2D(t,l,i,w,h,b,f,y,p) (gls_watch_write(t), glTexImage2D(t,l,i,w,h,b,f,y,p))
#define glTexSubImage2D(t,l,x,y,w,h,f,ty,p) (gls_watch_write(t), glTexSubImage2D(t,l,x,y,w,h,f,ty,p))
#define glCopyTexSubImage2D(t,l,ox,oy,x,y,w,h) (gls_watch_write(t), glCopyTexSubImage2D(t,l,ox,oy,x,y,w,h))
#define glCopyTexImage2D(t,l,i,x,y,w,h,b) (gls_watch_write(t), glCopyTexImage2D(t,l,i,x,y,w,h,b))

/* The top of every frame: forget everything, so nothing drawn outside this file between
   two frames can leave a stale answer behind. */
static void gls_frame_begin(void)
{
    const SDL_GLContext context=SDL_GL_GetCurrentContext();
    if(context!=g_glsLimitsContext) {
        g_glsLimitsContext=context;
        g_glsActiveLimit=g_glsImageLimit=g_glsClientLimit=g_glsFixedLimit=0;
        g_glsServerStackLimit=g_glsClientStackLimit=0;
    }
    g_glsServerStack.clear();g_glsClientStack.clear();
    gls_forget_polygon();gls_forget_env();
    g_glsMatEpoch++;gls_forget_clients();
    g_glsCull.known = g_glsOffset.known = false;
    g_glsBlend.known=g_glsDepth.known=g_glsAlpha.known=false;
    gls_forget_textures();
    g_glsArrayKnown = false;
    g_glsProgramKnown = false; g_glsProgramEpoch++;
}

#endif /* OPENCNC3D_GL_STATE_CACHE_H */
