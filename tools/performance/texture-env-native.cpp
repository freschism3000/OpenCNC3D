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
static unsigned checks=0,setters=0,queries=0;
static void counted_i(GLenum a,GLenum b,GLint c){setters++;glTexEnvi(a,b,c);}
static void counted_f(GLenum a,GLenum b,GLfloat c){setters++;glTexEnvf(a,b,c);}
static void counted_iv(GLenum a,GLenum b,const GLint* c){setters++;glTexEnviv(a,b,c);}
static void counted_fv(GLenum a,GLenum b,const GLfloat* c){setters++;glTexEnvfv(a,b,c);}
static void counted_get_i(GLenum a,GLenum b,GLint* c){queries++;glGetTexEnviv(a,b,c);}
static void counted_get_f(GLenum a,GLenum b,GLfloat* c){queries++;glGetTexEnvfv(a,b,c);}
#define glTexEnvi(a,b,c) counted_i(a,b,c)
#define glTexEnvf(a,b,c) counted_f(a,b,c)
#define glTexEnviv(a,b,c) counted_iv(a,b,c)
#define glTexEnvfv(a,b,c) counted_fv(a,b,c)
#define glGetTexEnviv(a,b,c) counted_get_i(a,b,c)
#define glGetTexEnvfv(a,b,c) counted_get_f(a,b,c)
#include "gl_state_cache.h"
static bool mesh_perf_switch(const char*){return true;}
static void check(bool ok,const char* text){checks++;if(!ok){fprintf(stderr,"FAIL|%s\n",text);exit(2);}}
static const GLenum parameters[]={GL_TEXTURE_ENV_MODE,GL_COMBINE_RGB,GL_COMBINE_ALPHA,GL_SOURCE0_RGB,GL_SOURCE1_RGB,GL_SOURCE2_RGB,GL_SOURCE0_ALPHA,GL_SOURCE1_ALPHA,GL_SOURCE2_ALPHA,GL_OPERAND0_RGB,GL_OPERAND1_RGB,GL_OPERAND2_RGB,GL_OPERAND0_ALPHA,GL_OPERAND1_ALPHA,GL_OPERAND2_ALPHA,GL_RGB_SCALE,GL_ALPHA_SCALE};
static void verify(){for(GLenum p:parameters){GLint want=-1,got=-2;GLfloat f=-3; (glGetTexEnviv)(GL_TEXTURE_ENV,p,&want);glGetTexEnviv(GL_TEXTURE_ENV,p,&got);glGetTexEnvfv(GL_TEXTURE_ENV,p,&f);check(want==got&&f==(GLfloat)want,"actual scalar matches cached integer/float");}check(glGetError()==GL_NO_ERROR,"valid calls remain error-free");}
static unsigned rng=1234567;static unsigned next(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static GLint legal(GLenum p,unsigned v){
 if(p==GL_TEXTURE_ENV_MODE){GLint x[]={GL_MODULATE,GL_DECAL,GL_BLEND,GL_REPLACE,GL_ADD,GL_COMBINE};return x[v%6];}
 if(p==GL_COMBINE_RGB){GLint x[]={GL_REPLACE,GL_MODULATE,GL_ADD,GL_ADD_SIGNED,GL_INTERPOLATE,GL_SUBTRACT,GL_DOT3_RGB,GL_DOT3_RGBA};return x[v%8];}
 if(p==GL_COMBINE_ALPHA){GLint x[]={GL_REPLACE,GL_MODULATE,GL_ADD,GL_ADD_SIGNED,GL_INTERPOLATE,GL_SUBTRACT};return x[v%6];}
 if(p==GL_RGB_SCALE||p==GL_ALPHA_SCALE)return 1<<(v%3);
 if(p==GL_SOURCE0_RGB||p==GL_SOURCE1_RGB||p==GL_SOURCE2_RGB||p==GL_SOURCE0_ALPHA||p==GL_SOURCE1_ALPHA||p==GL_SOURCE2_ALPHA){GLint x[]={GL_TEXTURE,GL_CONSTANT,GL_PRIMARY_COLOR,GL_PREVIOUS};return x[v%4];}
 if(p==GL_OPERAND0_ALPHA||p==GL_OPERAND1_ALPHA||p==GL_OPERAND2_ALPHA)return v&1?GL_SRC_ALPHA:GL_ONE_MINUS_SRC_ALPHA;
 GLint x[]={GL_SRC_COLOR,GL_ONE_MINUS_SRC_COLOR,GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA};return x[v%4];
}
int main(int argc,char** argv){SDL_SetMainReady();SDL_setenv("OPENCNC3D_TEXENV_CACHE",argc>1?argv[1]:"1",1);SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP,"1");check(SDL_Init(SDL_INIT_VIDEO)==0,"SDL");SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);SDL_Window* w=SDL_CreateWindow("hidden private texture environment proof",0,0,64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);check(w!=NULL,"hidden window");SDL_GLContext ctx=SDL_GL_CreateContext(w);check(ctx!=NULL,"GLcontext");gls_frame_begin();gls_texture_limits();verify();
 for(int i=0;i<3000;i++){unsigned v=next();glActiveTexture(GL_TEXTURE0+((v>>8)%std::min(4,g_glsFixedLimit)));GLenum p=parameters[v%17];GLint value=legal(p,v>>16);GLfloat f=(GLfloat)value;switch((v>>4)%4){case 0:glTexEnvi(GL_TEXTURE_ENV,p,value);break;case 1:glTexEnvf(GL_TEXTURE_ENV,p,f);break;case 2:glTexEnviv(GL_TEXTURE_ENV,p,&value);break;case 3:glTexEnvfv(GL_TEXTURE_ENV,p,&f);break;}verify();if(i%17==0){glPushAttrib(GL_TEXTURE_BIT|GL_ENABLE_BIT);glActiveTexture(GL_TEXTURE0);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);glPushAttrib(GL_POLYGON_BIT);glTexEnvf(GL_TEXTURE_ENV,GL_RGB_SCALE,4);glPopAttrib();verify();glPopAttrib();verify();}if(i%23==0){(glTexEnvi)(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);gls_frame_begin();verify();}}
 glActiveTexture(GL_TEXTURE0);
 GLfloat colour[4]={.125f,.25f,.5f,.75f},actual[4]={};glTexEnvfv(GL_TEXTURE_ENV,GL_TEXTURE_ENV_COLOR,colour);(glGetTexEnvfv)(GL_TEXTURE_ENV,GL_TEXTURE_ENV_COLOR,actual);check(!memcmp(colour,actual,sizeof colour),"all colour vector components preserved");
 for(int i=0;i<4;i++){while(glGetError()!=GL_NO_ERROR){}glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,-1);check(glGetError()==GL_INVALID_ENUM,"invalid enum always forwarded");glTexEnvf(GL_TEXTURE_ENV,GL_RGB_SCALE,3);check(glGetError()==GL_INVALID_VALUE,"invalid scale always forwarded");glTexEnvi(0xdead,GL_TEXTURE_ENV_MODE,GL_MODULATE);check(glGetError()==GL_INVALID_ENUM,"invalid target forwarded");glGetTexEnviv(GL_TEXTURE_ENV,0xdead,(GLint*)actual);check(glGetError()==GL_INVALID_ENUM,"invalid query forwarded");}verify();
 SDL_GLContext other=SDL_GL_CreateContext(w);check(other!=NULL,"second context");SDL_GL_MakeCurrent(w,other);gls_frame_begin();verify();SDL_GL_MakeCurrent(w,ctx);gls_frame_begin();verify();SDL_GL_DeleteContext(other);
 glActiveTexture(GL_TEXTURE0);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);glTexEnvf(GL_TEXTURE_ENV,GL_RGB_SCALE,1);setters=queries=0;
 for(int i=0;i<500;i++){GLint saved;glGetTexEnviv(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,&saved);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_COMBINE);glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_RGB,GL_MODULATE);glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_RGB,GL_TEXTURE);glTexEnvi(GL_TEXTURE_ENV,GL_OPERAND0_RGB,GL_SRC_COLOR);glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_RGB,GL_PRIMARY_COLOR);glTexEnvi(GL_TEXTURE_ENV,GL_OPERAND1_RGB,GL_SRC_COLOR);glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_ALPHA,GL_MODULATE);glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_ALPHA,GL_TEXTURE);glTexEnvi(GL_TEXTURE_ENV,GL_OPERAND0_ALPHA,GL_SRC_ALPHA);glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_ALPHA,GL_PRIMARY_COLOR);glTexEnvi(GL_TEXTURE_ENV,GL_OPERAND1_ALPHA,GL_SRC_ALPHA);glTexEnvf(GL_TEXTURE_ENV,GL_RGB_SCALE,2);glTexEnvf(GL_TEXTURE_ENV,GL_RGB_SCALE,1);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,saved);}
 printf("TEXENVTEST|cache=%s|checks=%u|x2_500_setters=%u|x2_500_queries=%u|version=%s|renderer=%s\n",argc>1?argv[1]:"1",checks,setters,queries,glGetString(GL_VERSION),glGetString(GL_RENDERER));check(glGetError()==GL_NO_ERROR,"final errors");SDL_GL_DeleteContext(ctx);SDL_DestroyWindow(w);SDL_Quit();return 0;}
