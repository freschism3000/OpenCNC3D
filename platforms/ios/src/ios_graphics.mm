#include "ios_graphics.h"
#include "ios_controls.h"
#include <SDL_syswm.h>
#include <OpenGLES/ES2/gl.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gl4esinit.h>

static SDL_Window* game_window;
static void (*compat_flush)(void);
static GLuint screen_fbo, screen_color;
static void (*native_bind_fb)(GLenum, GLuint);
static void (*native_bind_rb)(GLenum, GLuint);

static void refresh_screen_targets(){
    SDL_SysWMinfo info; SDL_VERSION(&info.version);
    if(SDL_GetWindowWMInfo(game_window,&info)&&info.subsystem==SDL_SYSWM_UIKIT){
        screen_fbo=info.info.uikit.framebuffer;
        screen_color=info.info.uikit.colorbuffer;
    }
}
static void bind_framebuffer(GLenum target,GLuint buffer){
    /* SDL's iOS screen is an FBO, never desktop framebuffer zero. */
    native_bind_fb(target,buffer?buffer:screen_fbo);
}
static void* native_proc(const char* name){
    if(!strcmp(name,"glBindFramebuffer")||!strcmp(name,"glBindFramebufferOES"))
        return (void*)bind_framebuffer;
    return SDL_GL_GetProcAddress(name);
}
static void screen_size(int* w,int* h){SDL_GL_GetDrawableSize(game_window,w,h);}
int cnc_ios_graphics_init(SDL_Window* window){
    game_window=window;
    native_bind_fb=(void(*)(GLenum,GLuint))SDL_GL_GetProcAddress("glBindFramebuffer");
    native_bind_rb=(void(*)(GLenum,GLuint))SDL_GL_GetProcAddress("glBindRenderbuffer");
    if(!native_bind_fb||!native_bind_rb)return 0;
    refresh_screen_targets();
    if(!screen_fbo||!screen_color){fprintf(stderr,"IOS|gles|missing SDL drawable\n");return 0;}
    int width=0,height=0;SDL_GL_GetDrawableSize(window,&width,&height);
    glViewport(0,0,width,height);glScissor(0,0,width,height);
    setenv("LIBGL_ES","2",1);setenv("LIBGL_FB","0",1);
    set_getprocaddress(native_proc);set_getmainfbsize(screen_size);initialize_gl4es();
    compat_flush=(void(*)(void))gl4es_GetProcAddress("glFlush");
    fprintf(stderr,"IOS|gles|drawable-fbo=%u color=%u\n",screen_fbo,screen_color);
    return 1;
}
void* cnc_ios_gl_proc(const char* name){return gl4es_GetProcAddress(name);}
void cnc_ios_swap_window(SDL_Window* window){
    if(compat_flush)compat_flush();
    refresh_screen_targets();
    native_bind_fb(GL_FRAMEBUFFER,screen_fbo);
    native_bind_rb(GL_RENDERBUFFER,screen_color);
    SDL_GL_SwapWindow(window);
    cnc_ios_controls_refresh();
}
void cnc_ios_graphics_shutdown(){close_gl4es();game_window=NULL;}
