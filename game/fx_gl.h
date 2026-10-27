/* ====================================================================================
 *  fx_gl.h -- the shader and framebuffer plumbing for the TIER 2 post chain.
 *
 *  TIER 2 ONLY. docs/tier1-gap.md's opening line is the authority: "Tier 2 is the
 *  modern desktop build and may use shaders freely." Nothing in this file, or in any
 *  file that includes it, may ever be reached on the Win98 / Voodoo 2 path. Every
 *  feature built on it carries a row in that file with its Tier 1 fallback, and the
 *  fallback is always the same one: the chain does not run, and the picture is exactly
 *  the picture this project has been shipping since the renderer was written.
 *
 *  WHY THIS EXISTS AT ALL, given the renderer is 12,000 lines of glBegin.
 *
 *  It does not touch them. The post chain renders the EXISTING immediate-mode world
 *  into an offscreen buffer and then works on the RESULT. Not one draw call in
 *  cnc_eyes.cpp changes shape, which is the only reason a feature this size can be
 *  added to a renderer this carefully measured without putting every gate at risk.
 *
 *  TWO PLATFORMS, ONE SPELLING. macOS hands us a 2.1 legacy context where the shader
 *  entry points are in the framework and the framebuffer ones are EXT; mingw hands us
 *  a frozen 1.1 header where NOTHING past 1.1 is exported and everything must come
 *  from the driver at runtime. compat/win/OpenGL/gl.h already solved that for the two
 *  multitexture calls, and this file solves it the same way for the forty-odd calls
 *  the chain needs: SDL_GL_GetProcAddress, core name first and then the ARB or EXT
 *  spelling, because a driver is entitled to export only one of them.
 *
 *  If ANY entry point is missing, fx_gl_load() returns 0 and the whole chain stays
 *  off. That is deliberate and it is the trap this project keeps naming: a post pass
 *  that half-loads and silently draws nothing looks exactly like a rendering bug.
 *
 *  The two info-log queries are the ONLY exception, and they are marked as such where
 *  they are resolved: they are diagnostics that only run after a shader has already
 *  failed, they are tested for NULL at both of their call sites, and losing the text
 *  of a compile error is not a reason to switch a working chain off. Everything else
 *  is required, including the entry points that only run on the teardown path, because
 *  a call through a null teardown pointer ends the process just as thoroughly as a
 *  call through a null draw pointer.
 *
 *  That promise used to be made by a hand-written expression listing 18 of the 26
 *  pointers this file resolves. Eight were absent from it, and five of those eight are
 *  called with no null guard: glGetShaderiv and glGetProgramiv the moment the first
 *  shader is compiled, glDeleteShader, glDeleteProgram and glDeleteFramebuffers on the
 *  teardown paths. A driver missing any of the five gave a jump through zero rather
 *  than the clean fallback promised here. The required/optional decision now lives on
 *  the resolve line itself and the test is built from it, so a pointer added later
 *  cannot be forgotten by a list somewhere else in the file.
 * ==================================================================================== */
#ifndef OPENCNC3D_FX_GL_H
#define OPENCNC3D_FX_GL_H

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Tokens ---------------------------------------------------------------------
   Guarded one by one rather than pulled from a glext, because the two platforms
   disagree about which header already carries which. A token is a number; getting it
   from here or from the system header is the same number either way. */
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER                0x8B30
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER                  0x8B31
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS                 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS                    0x8B82
#endif
#ifndef GL_INFO_LOG_LENGTH
#define GL_INFO_LOG_LENGTH                0x8B84
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER                    0x8D40
#endif
#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER                   0x8D41
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0              0x8CE0
#endif
#ifndef GL_DEPTH_ATTACHMENT
#define GL_DEPTH_ATTACHMENT               0x8D00
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE           0x8CD5
#endif
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24              0x81A6
#endif
#ifndef GL_TEXTURE_COMPARE_MODE
#define GL_TEXTURE_COMPARE_MODE           0x884C
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE                  0x812F
#endif
#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER                0x812D
#endif
#ifndef GL_TEXTURE_BORDER_COLOR
#define GL_TEXTURE_BORDER_COLOR           0x1004
#endif
/* THE ONE TOKEN IN THIS FILE THAT IS NEWER THAN BOTH SYSTEM HEADERS AGREE ON, and the
   one that proved the paragraph above is not hypothetical. GL_RGBA16F is OpenGL 3.0. On
   Windows it arrives through the <GL/glext.h> the compat header includes, so the first
   build of the high-range target compiled there and looked finished. A compatibility
   profile Mac is served the 2.1 header, which carries this extension under its ARB
   spelling and nothing else, so the same line failed there with an undeclared
   identifier. Same number, 0x881A, either way. Whether the DRIVER will accept the format
   is a separate question and is asked separately, at the glTexImage2D that uses this. */
#ifndef GL_RGBA16F
#define GL_RGBA16F                        0x881A
#endif

typedef char           FxGLchar;
typedef ptrdiff_t      FxGLsizeiptr;

/* ---- Entry points ----------------------------------------------------------------
   Own typedefs with an FX prefix: glext.h on the Windows side already declares the
   PFNGL* names, and a second definition of those would be a redefinition rather than
   a convenience. */
#ifndef APIENTRY
#define APIENTRY
#endif

typedef GLuint   (APIENTRY *FXPFNCREATESHADER)(GLenum);
typedef void     (APIENTRY *FXPFNDELETESHADER)(GLuint);
typedef void     (APIENTRY *FXPFNSHADERSOURCE)(GLuint, GLsizei, const FxGLchar* const*, const GLint*);
typedef void     (APIENTRY *FXPFNCOMPILESHADER)(GLuint);
typedef void     (APIENTRY *FXPFNGETSHADERIV)(GLuint, GLenum, GLint*);
typedef void     (APIENTRY *FXPFNGETSHADERINFOLOG)(GLuint, GLsizei, GLsizei*, FxGLchar*);
typedef GLuint   (APIENTRY *FXPFNCREATEPROGRAM)(void);
typedef void     (APIENTRY *FXPFNDELETEPROGRAM)(GLuint);
typedef void     (APIENTRY *FXPFNATTACHSHADER)(GLuint, GLuint);
typedef void     (APIENTRY *FXPFNLINKPROGRAM)(GLuint);
typedef void     (APIENTRY *FXPFNGETPROGRAMIV)(GLuint, GLenum, GLint*);
typedef void     (APIENTRY *FXPFNGETPROGRAMINFOLOG)(GLuint, GLsizei, GLsizei*, FxGLchar*);
typedef void     (APIENTRY *FXPFNUSEPROGRAM)(GLuint);
typedef GLint    (APIENTRY *FXPFNGETUNIFORMLOCATION)(GLuint, const FxGLchar*);
typedef void     (APIENTRY *FXPFNUNIFORM1I)(GLint, GLint);
typedef void     (APIENTRY *FXPFNUNIFORM1F)(GLint, GLfloat);
typedef void     (APIENTRY *FXPFNUNIFORM2F)(GLint, GLfloat, GLfloat);
typedef void     (APIENTRY *FXPFNUNIFORM3F)(GLint, GLfloat, GLfloat, GLfloat);
typedef void     (APIENTRY *FXPFNUNIFORM4F)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void     (APIENTRY *FXPFNUNIFORM4FV)(GLint, GLsizei, const GLfloat*);
typedef void     (APIENTRY *FXPFNUNIFORMMATRIX4FV)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void     (APIENTRY *FXPFNGENFRAMEBUFFERS)(GLsizei, GLuint*);
typedef void     (APIENTRY *FXPFNDELETEFRAMEBUFFERS)(GLsizei, const GLuint*);
typedef void     (APIENTRY *FXPFNBINDFRAMEBUFFER)(GLenum, GLuint);
typedef void     (APIENTRY *FXPFNFRAMEBUFFERTEXTURE2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum   (APIENTRY *FXPFNCHECKFRAMEBUFFERSTATUS)(GLenum);

static FXPFNCREATESHADER            fx_glCreateShader;
static FXPFNDELETESHADER            fx_glDeleteShader;
static FXPFNSHADERSOURCE            fx_glShaderSource;
static FXPFNCOMPILESHADER           fx_glCompileShader;
static FXPFNGETSHADERIV             fx_glGetShaderiv;
static FXPFNGETSHADERINFOLOG        fx_glGetShaderInfoLog;
static FXPFNCREATEPROGRAM           fx_glCreateProgram;
static FXPFNDELETEPROGRAM           fx_glDeleteProgram;
static FXPFNATTACHSHADER            fx_glAttachShader;
static FXPFNLINKPROGRAM             fx_glLinkProgram;
static FXPFNGETPROGRAMIV            fx_glGetProgramiv;
static FXPFNGETPROGRAMINFOLOG       fx_glGetProgramInfoLog;
static FXPFNUSEPROGRAM              fx_glUseProgram;
static FXPFNGETUNIFORMLOCATION      fx_glGetUniformLocation;
static FXPFNUNIFORM1I               fx_glUniform1i;
static FXPFNUNIFORM1F               fx_glUniform1f;
static FXPFNUNIFORM2F               fx_glUniform2f;
static FXPFNUNIFORM3F               fx_glUniform3f;
static FXPFNUNIFORM4F               fx_glUniform4f;
static FXPFNUNIFORM4FV              fx_glUniform4fv;
static FXPFNUNIFORMMATRIX4FV        fx_glUniformMatrix4fv;
static FXPFNGENFRAMEBUFFERS         fx_glGenFramebuffers;
static FXPFNDELETEFRAMEBUFFERS      fx_glDeleteFramebuffers;
static FXPFNBINDFRAMEBUFFER         fx_glBindFramebuffer;
static FXPFNFRAMEBUFFERTEXTURE2D    fx_glFramebufferTexture2D;
static FXPFNCHECKFRAMEBUFFERSTATUS  fx_glCheckFramebufferStatus;

/* PROGRAM BINDS GO THROUGH THE STATE CACHE (gl_state_cache.h, the bound program). The
   macros below are function-like, so the loader's assignments and the pointers' own
   names are untouched; every call in this file reaches the wrapper, and the wrapper calls
   the real entry point through the parenthesised name. Linking and deleting (below) bump
   the cache's program epoch. */
static inline void gls_use_program(GLuint p)
{
    if (gls_program_shadow() && g_glsProgramKnown && g_glsProgram == p) {
        if (gls_program_verify()) gls_program_check("bind");
        g_glsProgramBindsSkipped++;
        return;
    }
    (fx_glUseProgram)(p);
    g_glsProgram = p; g_glsProgramKnown = true;
}
#define fx_glUseProgram(p)    gls_use_program(p)

/* A UNIFORM'S LOCATION IS FIXED WHEN ITS PROGRAM LINKS, so it is asked for once per
   program and name and then remembered. The fx_set* helpers below look a uniform up by
   name every time they set it, about 300 lookups a frame under Enhanced, and on a driver
   with its own worker thread each lookup can wait for that thread to drain. Relinking or
   deleting a program forgets its names, so a reused program id never inherits them. */
static std::map<GLuint, std::map<std::string, GLint> > g_fxUniformLoc;
static GLint fx_uniform_location(GLuint p, const FxGLchar* n)
{
    std::map<std::string, GLint>& names = g_fxUniformLoc[p];
    const std::string key(n);
    std::map<std::string, GLint>::const_iterator it = names.find(key);
    if (it != names.end()) return it->second;
    const GLint l = (fx_glGetUniformLocation)(p, n);
    names[key] = l;
    return l;
}
static void fx_link_program(GLuint p)   { g_fxUniformLoc.erase(p); (fx_glLinkProgram)(p); g_glsProgramEpoch++; }
static void fx_delete_program(GLuint p)
{
    g_fxUniformLoc.erase(p); (fx_glDeleteProgram)(p); g_glsProgramEpoch++;
    if (p == g_glsProgram) g_glsProgramKnown = false;
}
#define fx_glGetUniformLocation(p, n) fx_uniform_location((p), (n))
#define fx_glLinkProgram(p)           fx_link_program(p)
#define fx_glDeleteProgram(p)         fx_delete_program(p)

static int  fx_gl_ready = 0;      /* 1 = every required entry point resolved */
static int  fx_gl_tried = 0;
static int  fx_gl_missing = 0;    /* a REQUIRED entry point came back NULL */
static char fx_gl_why[256];       /* why not, for the panel and the log */

/* Third argument to fx_proc. FX_REQ means the chain cannot run without this pointer,
   which is every pointer here except the two info-log queries: see the exception at the
   top of the file. Putting the decision on the resolve line is what keeps the readiness
   test honest, because the test is now derived from these flags instead of from a
   separately maintained list that can fall behind the pointers it is meant to cover. */
#define FX_REQ 1
#define FX_OPT 0

/* Core name first, then the extension spelling. See the long note in
   compat/win/OpenGL/gl.h: a driver that advertises the extension but reports a core
   version below the promotion is entitled to export only the suffixed name, and asking
   for the core name alone comes back NULL on exactly the drivers we care about. */
static void* fx_proc(const char* core, const char* alt, int required)
{
    void* p = SDL_GL_GetProcAddress(core);
    if (!p && alt) p = SDL_GL_GetProcAddress(alt);
    if (!p) {
        if (required) {
            fx_gl_missing = 1;
            /* First required casualty wins the message. Only required ones are eligible,
               so the reason printed is always something that actually stopped the chain. */
            if (fx_gl_why[0] == 0)
                snprintf(fx_gl_why, sizeof fx_gl_why, "%s is not exported by this driver", core);
        } else {
            fprintf(stderr, "FX|note|%s is absent; shader error text will be unavailable\n", core);
        }
    }
    return p;
}

static int fx_gl_load(void)
{
    if (fx_gl_tried) return fx_gl_ready;
    fx_gl_tried = 1;
    fx_gl_why[0] = 0;
    fx_gl_missing = 0;

    fx_glCreateShader       = (FXPFNCREATESHADER)          fx_proc("glCreateShader", "glCreateShaderObjectARB", FX_REQ);
    fx_glDeleteShader       = (FXPFNDELETESHADER)          fx_proc("glDeleteShader", "glDeleteObjectARB", FX_REQ);
    fx_glShaderSource       = (FXPFNSHADERSOURCE)          fx_proc("glShaderSource", "glShaderSourceARB", FX_REQ);
    fx_glCompileShader      = (FXPFNCOMPILESHADER)         fx_proc("glCompileShader", "glCompileShaderARB", FX_REQ);
    fx_glGetShaderiv        = (FXPFNGETSHADERIV)           fx_proc("glGetShaderiv", NULL, FX_REQ);
    fx_glGetShaderInfoLog   = (FXPFNGETSHADERINFOLOG)      fx_proc("glGetShaderInfoLog", NULL, FX_OPT);
    fx_glCreateProgram      = (FXPFNCREATEPROGRAM)         fx_proc("glCreateProgram", "glCreateProgramObjectARB", FX_REQ);
    fx_glDeleteProgram      = (FXPFNDELETEPROGRAM)         fx_proc("glDeleteProgram", NULL, FX_REQ);
    fx_glAttachShader       = (FXPFNATTACHSHADER)          fx_proc("glAttachShader", "glAttachObjectARB", FX_REQ);
    fx_glLinkProgram        = (FXPFNLINKPROGRAM)           fx_proc("glLinkProgram", "glLinkProgramARB", FX_REQ);
    fx_glGetProgramiv       = (FXPFNGETPROGRAMIV)          fx_proc("glGetProgramiv", NULL, FX_REQ);
    fx_glGetProgramInfoLog  = (FXPFNGETPROGRAMINFOLOG)     fx_proc("glGetProgramInfoLog", NULL, FX_OPT);
    fx_glUseProgram         = (FXPFNUSEPROGRAM)            fx_proc("glUseProgram", "glUseProgramObjectARB", FX_REQ);
    fx_glGetUniformLocation = (FXPFNGETUNIFORMLOCATION)    fx_proc("glGetUniformLocation", "glGetUniformLocationARB", FX_REQ);
    fx_glUniform1i          = (FXPFNUNIFORM1I)             fx_proc("glUniform1i", "glUniform1iARB", FX_REQ);
    fx_glUniform1f          = (FXPFNUNIFORM1F)             fx_proc("glUniform1f", "glUniform1fARB", FX_REQ);
    fx_glUniform2f          = (FXPFNUNIFORM2F)             fx_proc("glUniform2f", "glUniform2fARB", FX_REQ);
    fx_glUniform3f          = (FXPFNUNIFORM3F)             fx_proc("glUniform3f", "glUniform3fARB", FX_REQ);
    /* Resolved and required but not currently called by any pass. Kept required so the
       promise at the top of the file stays literally true, and because a driver that
       exports glUniform3f and glUniform4fv but not glUniform4f has a broken GLSL
       implementation the rest of this chain should not be trusting either. */
    fx_glUniform4f          = (FXPFNUNIFORM4F)             fx_proc("glUniform4f", "glUniform4fARB", FX_REQ);
    fx_glUniform4fv         = (FXPFNUNIFORM4FV)            fx_proc("glUniform4fv", "glUniform4fvARB", FX_REQ);
    fx_glUniformMatrix4fv   = (FXPFNUNIFORMMATRIX4FV)      fx_proc("glUniformMatrix4fv", "glUniformMatrix4fvARB", FX_REQ);
    fx_glGenFramebuffers    = (FXPFNGENFRAMEBUFFERS)       fx_proc("glGenFramebuffers", "glGenFramebuffersEXT", FX_REQ);
    fx_glDeleteFramebuffers = (FXPFNDELETEFRAMEBUFFERS)    fx_proc("glDeleteFramebuffers", "glDeleteFramebuffersEXT", FX_REQ);
    fx_glBindFramebuffer    = (FXPFNBINDFRAMEBUFFER)       fx_proc("glBindFramebuffer", "glBindFramebufferEXT", FX_REQ);
    fx_glFramebufferTexture2D = (FXPFNFRAMEBUFFERTEXTURE2D) fx_proc("glFramebufferTexture2D", "glFramebufferTexture2DEXT", FX_REQ);
    fx_glCheckFramebufferStatus = (FXPFNCHECKFRAMEBUFFERSTATUS) fx_proc("glCheckFramebufferStatus", "glCheckFramebufferStatusEXT", FX_REQ);

    /* One flag set by the resolver itself, rather than a list of pointers repeated here.
       The old expression named 18 of the 26 and left the other eight out, five of them
       called with no null guard: see the note at the top of the file. */
    fx_gl_ready = fx_gl_missing ? 0 : 1;

    if (!fx_gl_ready && fx_gl_why[0] == 0)
        snprintf(fx_gl_why, sizeof fx_gl_why, "shaders or framebuffer objects are absent");
    if (!fx_gl_ready)
        fprintf(stderr, "FX|unavailable|%s\n", fx_gl_why);
    return fx_gl_ready;
}

/* ---- Programs -------------------------------------------------------------------- */

/* The vertex half is the same for every pass in the chain: a screen-aligned quad that
   passes its texture coordinate through untouched. Written out rather than left to the
   fixed function pipeline because a GLSL program replaces BOTH stages, not one. */
static const char* FX_VS =
    "#version 120\n"
    "varying vec2 uv;\n"
    "void main() {\n"
    "    uv = gl_MultiTexCoord0.xy;\n"
    "    gl_Position = gl_Vertex;\n"   /* the quad is already in clip space */
    "}\n";

static GLuint fx_compile(GLenum kind, const char* src, const char* label)
{
    GLuint s = fx_glCreateShader(kind);
    fx_glShaderSource(s, 1, &src, NULL);
    fx_glCompileShader(s);
    GLint ok = 0;
    fx_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        GLsizei n = 0;
        if (fx_glGetShaderInfoLog) fx_glGetShaderInfoLog(s, (GLsizei)sizeof log, &n, log);
        log[n < (GLsizei)sizeof log ? n : (GLsizei)sizeof log - 1] = 0;
        fprintf(stderr, "FX|shader|%s FAILED TO COMPILE\n%s\n", label, log);
        fx_glDeleteShader(s);
        return 0;
    }
    return s;
}

/* One fragment source in, one linked program out. A failure is LOUD and returns 0; the
   caller then leaves that effect switched off rather than drawing through a zero
   program, which on most drivers is a black screen and on some is undefined. */
static GLuint fx_program(const char* fs_src, const char* label)
{
    if (!fx_gl_ready) return 0;
    GLuint vs = fx_compile(GL_VERTEX_SHADER, FX_VS, label);
    if (!vs) return 0;
    GLuint fs = fx_compile(GL_FRAGMENT_SHADER, fs_src, label);
    if (!fs) { fx_glDeleteShader(vs); return 0; }
    GLuint p = fx_glCreateProgram();
    fx_glAttachShader(p, vs);
    fx_glAttachShader(p, fs);
    fx_glLinkProgram(p);
    GLint ok = 0;
    fx_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        GLsizei n = 0;
        if (fx_glGetProgramInfoLog) fx_glGetProgramInfoLog(p, (GLsizei)sizeof log, &n, log);
        log[n < (GLsizei)sizeof log ? n : (GLsizei)sizeof log - 1] = 0;
        fprintf(stderr, "FX|program|%s FAILED TO LINK\n%s\n", label, log);
        fx_glDeleteProgram(p);
        p = 0;
    }
    fx_glDeleteShader(vs);
    fx_glDeleteShader(fs);
    return p;
}

static inline void fx_set1i(GLuint p, const char* n, int v)
{ GLint l = fx_glGetUniformLocation(p, n); if (l >= 0) fx_glUniform1i(l, v); }
static inline void fx_set1f(GLuint p, const char* n, float v)
{ GLint l = fx_glGetUniformLocation(p, n); if (l >= 0) fx_glUniform1f(l, v); }
static inline void fx_set2f(GLuint p, const char* n, float a, float b)
{ GLint l = fx_glGetUniformLocation(p, n); if (l >= 0) fx_glUniform2f(l, a, b); }
static inline void fx_set3f(GLuint p, const char* n, float a, float b, float c)
{ GLint l = fx_glGetUniformLocation(p, n); if (l >= 0) fx_glUniform3f(l, a, b, c); }
static inline void fx_set4fv(GLuint p, const char* n, int count, const float* v)
{ GLint l = fx_glGetUniformLocation(p, n); if (l >= 0) fx_glUniform4fv(l, count, v); }
static inline void fx_setmat(GLuint p, const char* n, const float* m)
{ GLint l = fx_glGetUniformLocation(p, n); if (l >= 0) fx_glUniformMatrix4fv(l, 1, GL_FALSE, m); }

/* ---- Render targets --------------------------------------------------------------
   Colour is RGBA8, deliberately, not a float format. Bloom on an 8-bit buffer is what
   this game's palette wants (nothing in a 1995 sprite or an N64 texture is brighter
   than white) and it costs the chain one whole class of extension checks. If HDR is
   ever wanted, this is the struct that grows a format field. */
/* FX_RT_FLOAT asks for a SIXTEEN BIT FLOAT colour attachment instead of eight bit
   fixed. It is a request and not a demand: `hdr` below records what the driver actually
   gave, because a target that silently came back eight bit while the chain believes it
   is working in high range would clip every highlight and look like a shader bug. */
#define FX_RT_COLOUR 1
#define FX_RT_DEPTH  2
#define FX_RT_FLOAT  4

struct FxRT {
    GLuint fbo, tex, depth;
    int    w, h;
    int    hdr;      /* 1 if the colour attachment really is float */
};

static void fx_rt_free(FxRT* rt)
{
    if (rt->fbo)   { fx_glDeleteFramebuffers(1, &rt->fbo); rt->fbo = 0; }
    if (rt->tex)   { glDeleteTextures(1, &rt->tex); rt->tex = 0; }
    if (rt->depth) { glDeleteTextures(1, &rt->depth); rt->depth = 0; }
    rt->w = rt->h = 0;
    rt->hdr = 0;
}

/* want: bit 0 = a colour attachment, bit 1 = a sampleable depth attachment.
   Returns 0 and frees everything if the driver calls the combination incomplete. */
static int fx_rt_init(FxRT* rt, int w, int h, int want, GLint filter)
{
    /* A target already at this size AND in the format now being asked for is kept. The
       format test is why the high-range switch can be thrown from the panel mid-game:
       without it the old eight bit target would survive the toggle and nothing would
       appear to happen. */
    if (rt->fbo && rt->w == w && rt->h == h
        && rt->hdr == ((want & FX_RT_FLOAT) ? 1 : 0)) return 1;
    fx_rt_free(rt);
    if (w < 1 || h < 1) return 0;
    rt->w = w; rt->h = h;

    fx_glGenFramebuffers(1, &rt->fbo);
    fx_glBindFramebuffer(GL_FRAMEBUFFER, rt->fbo);

    if (want & 1) {
        glGenTextures(1, &rt->tex);
        glBindTexture(GL_TEXTURE_2D, rt->tex);
        /* THE FLOAT ATTEMPT, AND THE FALLBACK THAT MAKES IT SAFE TO ASK FOR. GL_RGBA16F
           is OpenGL 3.0 (ARB_texture_float before that) and the token comes from
           <GL/glext.h>, which compat/win/OpenGL/gl.h already includes; the CALL is
           glTexImage2D, which is 1.0, so nothing new has to be resolved by hand on
           Windows. What can still fail is the driver, so the error queue is drained,
           the format is requested, and the queue is read again: a driver that will not
           take it says so there and this falls back to the eight bit format rather than
           handing the chain a target it cannot render to. */
        rt->hdr = 0;
        if (want & FX_RT_FLOAT) {
            while (glGetError() != GL_NO_ERROR) { }
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
            if (glGetError() == GL_NO_ERROR)
                rt->hdr = 1;
        }
        if (!rt->hdr)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        fx_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_TEXTURE_2D, rt->tex, 0);
    } else {
        /* Depth only. Both of these are core 1.1 and both are required: an FBO with no
           colour attachment is incomplete unless the draw and read buffers say so. */
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }

    if (want & 2) {
        glGenTextures(1, &rt->depth);
        glBindTexture(GL_TEXTURE_2D, rt->depth);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0,
                     GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        /* Sampled as a plain value, never through the comparison unit: the shadow test
           is done in the shader so its bias and its PCF taps are OUR arithmetic and can
           be read in the panel. GL_NONE is the default but drivers have been known to
           inherit it from a previously bound texture object. */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        fx_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_TEXTURE_2D, rt->depth, 0);
    }

    GLenum st = fx_glCheckFramebufferStatus(GL_FRAMEBUFFER);
    fx_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK);
    glReadBuffer(GL_BACK);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (st != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "FX|rt|%dx%d want=%d incomplete (0x%04X)\n", w, h, want, (unsigned)st);
        fx_rt_free(rt);
        return 0;
    }
    return 1;
}

static inline void fx_rt_bind(const FxRT* rt)
{
    fx_glBindFramebuffer(GL_FRAMEBUFFER, rt->fbo);
    glViewport(0, 0, rt->w, rt->h);
}

/* ---- The fullscreen quad ---------------------------------------------------------
   Emitted in CLIP SPACE with both matrix stacks pushed to identity, so no pass in the
   chain has to care what the world left on them. Depth test and blend off, because
   every post pass writes every pixel exactly once. */
static void fx_fullscreen_quad(void)
{
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, 0.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f, -1.0f, 0.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, 0.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f, 0.0f);
    glEnd();

    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);  glPopMatrix();
    glDepthMask(GL_TRUE);
}

/* ---- 4x4 matrix helpers ----------------------------------------------------------
   Column major, the way GL stores them, so a matrix read back with glGetFloatv can be
   handed straight to a uniform. The inverse is the general one (Cramer over the 4x4)
   rather than the fast affine shortcut: the projection matrix is not affine. */
static void fx_mat_mul(const float* a, const float* b, float* out)
{
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            out[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1] +
                             a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
}

static int fx_mat_invert(const float* m, float* inv)
{
    float a[16];
    a[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] +
             m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    a[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] -
             m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    a[8]  =  m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] +
             m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    a[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] -
             m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    a[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] -
             m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    a[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] +
             m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    a[9]  = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] -
             m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    a[13] =  m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] +
             m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    a[2]  =  m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] +
             m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    a[6]  = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] -
             m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    a[10] =  m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] +
             m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    a[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] -
             m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    a[3]  = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] -
             m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    a[7]  =  m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] +
             m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    a[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] -
             m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    a[15] =  m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] +
             m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];

    float det = m[0]*a[0] + m[1]*a[4] + m[2]*a[8] + m[3]*a[12];
    if (det > -1e-12f && det < 1e-12f) return 0;
    det = 1.0f / det;
    for (int i = 0; i < 16; i++) inv[i] = a[i] * det;
    return 1;
}

#endif /* OPENCNC3D_FX_GL_H */
