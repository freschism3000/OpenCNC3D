/* compat/win/OpenGL/gl.h
 *
 * cnc_eyes.cpp asks for Apple's framework header by name. On Windows the same
 * fixed-function GL lives in <GL/gl.h>, which mingw-w64 ships, so most of this is a
 * spelling difference. Two things are not.
 *
 * 1. MICROSOFT'S gl.h IS FROZEN AT OPENGL 1.1 (1995) and has never moved. Everything the
 *    renderer uses from 1.2 and 1.3 -- GL_CLAMP_TO_EDGE, the whole GL_COMBINE texture
 *    environment, GL_TEXTURE0 -- is simply absent from it. <GL/glext.h> carries those
 *    token definitions and mingw-w64 ships it, so including it supplies the names.
 *
 * 2. TOKENS ARE NOT ENTRY POINTS. On Windows, anything past 1.1 must be fetched from the
 *    driver at runtime through wglGetProcAddress; opengl32.dll exports only the 1.1 set.
 *    The renderer calls four such functions, all from GL 1.3 multitexture:
 *    glActiveTexture, glClientActiveTexture, glMultiTexCoord2f and glMultiTexCoord3f.
 *    They are resolved lazily below, on first call, which is safe because all four are
 *    only ever called with a context current.
 *
 *    ADDING A FIFTH IS A THREE LINE JOB AND FORGETTING IT BREAKS ONLY WINDOWS. A call
 *    past GL 1.1 that is not listed here compiles against a framework header that
 *    declares the whole of GL, and fails here with "was not declared in this scope".
 *    That has now happened twice: once when the water surface began sending a colour
 *    down the third texture unit, and again when the trees began feeding their bone
 *    indices in through a second texture COORDINATE ARRAY, which needs the client side
 *    selector rather than the server side one. Both times the other platform built
 *    clean and this one did not compile at all.
 *
 *    The cheapest way to catch the next one is to list every gl* call in the tree and
 *    subtract what <GL/gl.h> declares plus what this file defines; anything left over
 *    is a break waiting for the next push.
 *
 * Tier 1 note: neither function is new debt. Multitexture is already used on the Mac
 * build, and the Voodoo 2 has two texture units, so the Glide backend has an answer for
 * it. This header changes how the entry point is found, not what is drawn.
 *
 * See compat/win/README.md and BUILDING.md.
 */
#ifndef CNC3D_COMPAT_OPENGL_GL_H
#define CNC3D_COMPAT_OPENGL_GL_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#include <GL/gl.h>

/* glext.h declares prototypes for the 1.2+ entry points as well as the tokens, and a
   prototype would collide with the wrappers below. Only the tokens are wanted. */
#define GL_GLEXT_PROTOTYPES_ALREADY_HANDLED 1
#include <GL/glext.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Resolved on first use. A NULL result means the driver does not have the extension,
   which on any machine that can run this at all should not happen; the call then does
   nothing rather than jumping through a null pointer, so a missing entry point shows up
   as a texture drawn without its second unit and not as a crash. */
static PFNGLACTIVETEXTUREPROC       cnc3d_glActiveTexture_p;
static PFNGLCLIENTACTIVETEXTUREPROC cnc3d_glClientActiveTexture_p;
static PFNGLMULTITEXCOORD2FPROC   cnc3d_glMultiTexCoord2f_p;
static PFNGLMULTITEXCOORD3FPROC   cnc3d_glMultiTexCoord3f_p;

/* BOTH SPELLINGS, CORE FIRST AND THEN ARB, AND THIS IS NOT BELT AND BRACES.
 *
 * Multitexture arrived as the extension GL_ARB_multitexture and was promoted into core
 * OpenGL 1.3, and the two spellings are separate entry points as far as
 * wglGetProcAddress is concerned. A Windows ICD that advertises GL_ARB_multitexture but
 * reports a core version below 1.3 is entitled to export ONLY glActiveTextureARB, and
 * on such a driver asking for the core name alone returns NULL. That is not exotic; it
 * is the normal shape of an older or a fallback Windows driver.
 *
 * The first version of this header asked for the core name only and then did nothing
 * when it came back NULL. On a driver like that, the terrain pass would set up its two
 * texture units through calls that quietly evaporated and emit no texture coordinates
 * at all, so every terrain vertex would take whatever coordinate was last set: a black
 * tactical view, with the 2D sidebar, the HUD text and the radar frame all perfect
 * because none of them touches a second texture unit. That is exactly the picture
 * Windows produced.
 *
 * If neither spelling resolves, SAY SO ONCE AND LOUDLY. A silent no-op that turns the
 * game black is the worst of both worlds; the old behaviour cost a whole test round
 * because the failure looked like a rendering bug rather than a missing entry point.
 */
static int cnc3d_gl_mt_checked;

static void* cnc3d_gl_proc2(const char* core, const char* arb)
{
    void* p = (void*)wglGetProcAddress(core);
    if (!p) p = (void*)wglGetProcAddress(arb);
    return p;
}

static void cnc3d_gl_mt_complain(const char* what)
{
    if (cnc3d_gl_mt_checked) return;
    cnc3d_gl_mt_checked = 1;
    fprintf(stderr,
            "GL FATAL: %s could not be resolved under either its core or its ARB name.\n"
            "          This driver has no usable multitexture, and the terrain pass needs\n"
            "          two texture units. The 3D view will be black. Check GL_RENDERER\n"
            "          above: a software or fallback GL is the usual reason.\n", what);
    fflush(stderr);
}

static void cnc3d_gl_activetexture(GLenum tex)
{
    if (!cnc3d_glActiveTexture_p) {
        cnc3d_glActiveTexture_p = (PFNGLACTIVETEXTUREPROC)
            cnc3d_gl_proc2("glActiveTexture", "glActiveTextureARB");
        if (!cnc3d_glActiveTexture_p) cnc3d_gl_mt_complain("glActiveTexture");
    }
    if (cnc3d_glActiveTexture_p) cnc3d_glActiveTexture_p(tex);
}

/* THE CLIENT SIDE SELECTOR, WHICH IS A DIFFERENT SWITCH FROM ITS SERVER SIDE TWIN.
   glActiveTexture chooses which unit the texture state calls apply to; this one chooses
   which unit glTexCoordPointer and the GL_TEXTURE_COORD_ARRAY enable apply to. Vertex
   arrays that feed a second unit need this one, and setting the wrong switch silently
   points the array at unit zero, which draws the geometry with the first unit's
   coordinates replaced. */
static void cnc3d_gl_clientactivetexture(GLenum tex)
{
    if (!cnc3d_glClientActiveTexture_p) {
        cnc3d_glClientActiveTexture_p = (PFNGLCLIENTACTIVETEXTUREPROC)
            cnc3d_gl_proc2("glClientActiveTexture", "glClientActiveTextureARB");
        if (!cnc3d_glClientActiveTexture_p) cnc3d_gl_mt_complain("glClientActiveTexture");
    }
    if (cnc3d_glClientActiveTexture_p) cnc3d_glClientActiveTexture_p(tex);
}

static void cnc3d_gl_multitexcoord2f(GLenum tex, GLfloat s, GLfloat t)
{
    if (!cnc3d_glMultiTexCoord2f_p) {
        cnc3d_glMultiTexCoord2f_p = (PFNGLMULTITEXCOORD2FPROC)
            cnc3d_gl_proc2("glMultiTexCoord2f", "glMultiTexCoord2fARB");
        if (!cnc3d_glMultiTexCoord2f_p) cnc3d_gl_mt_complain("glMultiTexCoord2f");
    }
    if (cnc3d_glMultiTexCoord2f_p) cnc3d_glMultiTexCoord2f_p(tex, s, t);
}

/* THE THREE COMPONENT FORM, which the water surface uses to send a per vertex colour
   down the third texture unit rather than a coordinate. Same two spellings and the same
   silent-but-loud failure as its sibling above: an unresolved entry point draws without
   that unit instead of jumping through a null pointer. */
static void cnc3d_gl_multitexcoord3f(GLenum tex, GLfloat s, GLfloat t, GLfloat r)
{
    if (!cnc3d_glMultiTexCoord3f_p) {
        cnc3d_glMultiTexCoord3f_p = (PFNGLMULTITEXCOORD3FPROC)
            cnc3d_gl_proc2("glMultiTexCoord3f", "glMultiTexCoord3fARB");
        if (!cnc3d_glMultiTexCoord3f_p) cnc3d_gl_mt_complain("glMultiTexCoord3f");
    }
    if (cnc3d_glMultiTexCoord3f_p) cnc3d_glMultiTexCoord3f_p(tex, s, t, r);
}

#ifdef __cplusplus
}
#endif

#define glActiveTexture(t)          cnc3d_gl_activetexture(t)
#define glClientActiveTexture(t)    cnc3d_gl_clientactivetexture(t)
#define glMultiTexCoord2f(t, s, u)  cnc3d_gl_multitexcoord2f((t), (s), (u))
#define glMultiTexCoord3f(t, s, u, v) cnc3d_gl_multitexcoord3f((t), (s), (u), (v))

#endif
