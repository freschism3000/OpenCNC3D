/* The renderer state cache replaces GL function macros with its own wrappers.
 * Give those wrappers real local functions so their inner calls still reach
 * GL4ES rather than nonexistent desktop functions or raw native GLES. */
#ifndef OPENCNC3D_IOS_GL_CACHE_BRIDGE_H
#define OPENCNC3D_IOS_GL_CACHE_BRIDGE_H
#undef glLoadIdentity
static inline void glLoadIdentity(void) { gl4es_glLoadIdentity(); }
#undef glLoadMatrixf
static inline void glLoadMatrixf(const GLfloat* m) { gl4es_glLoadMatrixf(m); }
#undef glLoadMatrixd
static inline void glLoadMatrixd(const GLdouble* m) { gl4es_glLoadMatrixd(m); }
#undef glMultMatrixf
static inline void glMultMatrixf(const GLfloat* m) { gl4es_glMultMatrixf(m); }
#undef glMultMatrixd
static inline void glMultMatrixd(const GLdouble* m) { gl4es_glMultMatrixd(m); }
#undef glTranslatef
static inline void glTranslatef(GLfloat x,GLfloat y,GLfloat z) { gl4es_glTranslatef(x,y,z); }
#undef glTranslated
static inline void glTranslated(GLdouble x,GLdouble y,GLdouble z) { gl4es_glTranslated(x,y,z); }
#undef glRotatef
static inline void glRotatef(GLfloat a,GLfloat x,GLfloat y,GLfloat z) { gl4es_glRotatef(a,x,y,z); }
#undef glRotated
static inline void glRotated(GLdouble a,GLdouble x,GLdouble y,GLdouble z) { gl4es_glRotated(a,x,y,z); }
#undef glScalef
static inline void glScalef(GLfloat x,GLfloat y,GLfloat z) { gl4es_glScalef(x,y,z); }
#undef glScaled
static inline void glScaled(GLdouble x,GLdouble y,GLdouble z) { gl4es_glScaled(x,y,z); }
#undef glOrtho
static inline void glOrtho(GLdouble l,GLdouble r,GLdouble b,GLdouble t,GLdouble n,GLdouble f) { gl4es_glOrtho(l,r,b,t,n,f); }
#undef glFrustum
static inline void glFrustum(GLdouble l,GLdouble r,GLdouble b,GLdouble t,GLdouble n,GLdouble f) { gl4es_glFrustum(l,r,b,t,n,f); }
#undef glPopMatrix
static inline void glPopMatrix(void) { gl4es_glPopMatrix(); }
#undef glCullFace
static inline void glCullFace(GLenum mode) { gl4es_glCullFace(mode); }
#undef glFrontFace
static inline void glFrontFace(GLenum mode) { gl4es_glFrontFace(mode); }
#undef glPolygonOffset
static inline void glPolygonOffset(GLfloat factor,GLfloat units) { gl4es_glPolygonOffset(factor,units); }
#undef glEnable
static inline void glEnable(GLenum cap) { gl4es_glEnable(cap); }
#undef glDisable
static inline void glDisable(GLenum cap) { gl4es_glDisable(cap); }
#undef glIsEnabled
static inline GLboolean glIsEnabled(GLenum cap) { return gl4es_glIsEnabled(cap); }
#undef glPushAttrib
static inline void glPushAttrib(GLbitfield mask) { gl4es_glPushAttrib(mask); }
#undef glPopAttrib
static inline void glPopAttrib(void) { gl4es_glPopAttrib(); }
#undef glPushClientAttrib
static inline void glPushClientAttrib(GLbitfield mask) { gl4es_glPushClientAttrib(mask); }
#undef glPopClientAttrib
static inline void glPopClientAttrib(void) { gl4es_glPopClientAttrib(); }
#undef glClientActiveTexture
static inline void glClientActiveTexture(GLenum unit) { gl4es_glClientActiveTexture(unit); }
#undef glEnableClientState
static inline void glEnableClientState(GLenum cap) { gl4es_glEnableClientState(cap); }
#undef glDisableClientState
static inline void glDisableClientState(GLenum cap) { gl4es_glDisableClientState(cap); }
#undef glActiveTexture
static inline void glActiveTexture(GLenum unit) { gl4es_glActiveTexture(unit); }
#undef glBindTexture
static inline void glBindTexture(GLenum target,GLuint name) { gl4es_glBindTexture(target,name); }
#undef glTexEnvi
static inline void glTexEnvi(GLenum target,GLenum param,GLint value) { gl4es_glTexEnvi(target,param,value); }
#undef glTexEnvf
static inline void glTexEnvf(GLenum target,GLenum param,GLfloat value) { gl4es_glTexEnvf(target,param,value); }
#undef glTexEnviv
static inline void glTexEnviv(GLenum target,GLenum param,const GLint* value) { gl4es_glTexEnviv(target,param,value); }
#undef glTexEnvfv
static inline void glTexEnvfv(GLenum target,GLenum param,const GLfloat* value) { gl4es_glTexEnvfv(target,param,value); }
#undef glGetTexEnviv
static inline void glGetTexEnviv(GLenum target,GLenum param,GLint* value) { gl4es_glGetTexEnviv(target,param,value); }
#undef glGetTexEnvfv
static inline void glGetTexEnvfv(GLenum target,GLenum param,GLfloat* value) { gl4es_glGetTexEnvfv(target,param,value); }
#undef glTexParameteri
static inline void glTexParameteri(GLenum target,GLenum param,GLint value) { gl4es_glTexParameteri(target,param,value); }
#undef glTexParameterf
static inline void glTexParameterf(GLenum target,GLenum param,GLfloat value) { gl4es_glTexParameterf(target,param,value); }
#undef glDeleteTextures
static inline void glDeleteTextures(GLsizei n,const GLuint* names) { gl4es_glDeleteTextures(n,names); }
#endif
