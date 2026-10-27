#include <SDL.h>
#include <OpenGL/gl.h>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <stdint.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <limits>
static unsigned queries=0,sets=0,checks=0;
static void counted_get_i(GLenum p,GLint* v){queries++;glGetIntegerv(p,v);}
static void counted_get_f(GLenum p,GLfloat* v){queries++;glGetFloatv(p,v);}
static void counted_cull(GLenum p){sets++;glCullFace(p);}
static void counted_front(GLenum p){sets++;glFrontFace(p);}
static void counted_offset(GLfloat f,GLfloat u){sets++;glPolygonOffset(f,u);}
#define glGetIntegerv(p,v) counted_get_i(p,v)
#define glGetFloatv(p,v) counted_get_f(p,v)
#define glCullFace(p) counted_cull(p)
#define glFrontFace(p) counted_front(p)
#define glPolygonOffset(f,u) counted_offset(f,u)
#include "gl_state_cache.h"
static void check(bool v,const char* msg){checks++;if(!v){fprintf(stderr,"FAIL|%s\n",msg);exit(2);}}
static void verify(){GLint c=0,f=0;GLfloat a=0,b=0,x=0,y=0;(glGetIntegerv)(GL_CULL_FACE_MODE,&c);(glGetIntegerv)(GL_FRONT_FACE,&f);(glGetFloatv)(GL_POLYGON_OFFSET_FACTOR,&a);(glGetFloatv)(GL_POLYGON_OFFSET_UNITS,&b);check(gls_cull_mode()==c,"cull scalar exact");check(gls_front_face()==f,"front scalar exact");gls_polygon_offset_values(&x,&y);check(!memcmp(&a,&x,sizeof a),"offset factor float bits exact");check(!memcmp(&b,&y,sizeof b),"offset units float bits exact");}
static unsigned rng=982451653;static unsigned next(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(int argc,char** argv){SDL_SetMainReady();if(argc>1)SDL_setenv("OPENCNC3D_STATE_CACHE",argv[1],1);SDL_setenv("OPENCNC3D_BACKGROUND","1",1);SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP,"1");check(SDL_Init(SDL_INIT_VIDEO)==0,"SDL init");SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);SDL_Window* w=SDL_CreateWindow("private hidden GL state proof",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);check(w!=NULL,"hidden window");SDL_GLContext ctx=SDL_GL_CreateContext(w);check(ctx!=NULL,"context");gls_frame_begin();verify();
 for(int i=0;i<10000;i++){unsigned v=next();switch(v%8){case 0:glCullFace((v&1)?GL_FRONT:GL_BACK);break;case 1:glFrontFace((v&1)?GL_CW:GL_CCW);break;case 2:glPolygonOffset((float)(int)(v%8193)-4096.0f,(float)(int)((v>>16)%1025)-512.0f);break;case 3:glPolygonOffset((v&1)?-0.0f:0.0f,(v&2)?-0.0f:0.0f);break;case 4:glPushAttrib(GL_POLYGON_BIT);glCullFace(GL_FRONT_AND_BACK);glFrontFace(GL_CW);glPolygonOffset(2.0f,4.0f);glPopAttrib();break;case 5:glPushAttrib(GL_ENABLE_BIT);glCullFace(GL_FRONT);glPolygonOffset(1.0f,2.0f);glPopAttrib();break;case 6:(glCullFace)(GL_BACK);(glFrontFace)(GL_CCW);(glPolygonOffset)(3.5f,-2.5f);gls_frame_begin();break;case 7:gls_frame_begin();break;}verify();}
 for(int i=0;i<1000;i++){
  glMatrixMode(GL_PROJECTION);glLoadIdentity();glFrustum(-1,1,-1,1,1.0+(i%7),1000.0);glMatrixMode(GL_MODELVIEW);glLoadIdentity();glTranslatef((float)i/11,2.0f,-3.5f);glRotatef((float)i,0.25f,1.0f,-0.125f);float wanted[16];memcpy(wanted,gls_clip(),sizeof wanted);GlsClipRestore save;queries=0;glPushMatrix();glTranslatef(0.3f,1.75f,2.25f);glRotatef((float)(i%41),1,0,1);glPopMatrix();save.restored();check(!memcmp(wanted,gls_clip(),sizeof wanted),"owned modelview restores exact clip bits");check(queries==0,"known restored clip has no queries");
  g_glsClipEpoch=0;GlsClipRestore unknown;queries=0;glPushMatrix();glScalef(2,3,4);glPopMatrix();unknown.restored();check(!memcmp(wanted,gls_clip(),sizeof wanted),"unknown clip correctly read after restore");check(queries==2,"unknown answer requires both matrix reads");
 }
 // Driver enum errors must still be reported and must not corrupt the last state.
 while(glGetError()!=GL_NO_ERROR){}glCullFace(0xdead);check(glGetError()==GL_INVALID_ENUM,"invalid cull forwarded");verify();glFrontFace(0xdead);check(glGetError()==GL_INVALID_ENUM,"invalid front forwarded");verify();
 glPolygonOffset(std::numeric_limits<float>::infinity(),1.0f);verify();glPolygonOffset(1.0f,std::numeric_limits<float>::quiet_NaN());verify();
 // A new context and an external owner at the next frame each require fresh answers.
 SDL_GLContext other=SDL_GL_CreateContext(w);check(other!=NULL,"second context");SDL_GL_MakeCurrent(w,other);gls_frame_begin();verify();SDL_GL_MakeCurrent(w,ctx);gls_frame_begin();verify();SDL_GL_DeleteContext(other);
 glCullFace(GL_BACK);glFrontFace(GL_CCW);glPolygonOffset(2.0f,4.0f);queries=sets=0;
 for(int i=0;i<500;i++){GLint c=gls_cull_mode(),f=gls_front_face();GLfloat a,b;gls_polygon_offset_values(&a,&b);glCullFace(GL_BACK);glFrontFace(GL_CCW);glPolygonOffset(2.0f,4.0f);glCullFace((GLenum)c);glFrontFace((GLenum)f);glPolygonOffset(a,b);}
 const char* ext=(const char*)glGetString(GL_EXTENSIONS);printf("SHADOWCAPS|ext_fbo=%d|ext_blit=%d|core_blit_proc=%d|ext_blit_proc=%d\n",ext&&strstr(ext,"GL_EXT_framebuffer_object")!=NULL,ext&&strstr(ext,"GL_EXT_framebuffer_blit")!=NULL,SDL_GL_GetProcAddress("glBlitFramebuffer")!=NULL,SDL_GL_GetProcAddress("glBlitFramebufferEXT")!=NULL);
 printf("POLYGONSTATE|cache=%s|checks=%u|guard500_queries=%u|guard500_sets=%u|version=%s|renderer=%s\n",argc>1?argv[1]:"1",checks,queries,sets,glGetString(GL_VERSION),glGetString(GL_RENDERER));check(glGetError()==GL_NO_ERROR,"no remaining errors");SDL_GL_DeleteContext(ctx);SDL_DestroyWindow(w);SDL_Quit();return 0;}
