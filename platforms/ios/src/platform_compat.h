#ifndef OPENCNC3D_IOS_COMPAT_H
#define OPENCNC3D_IOS_COMPAT_H
#include <SDL.h>
#include <dlfcn.h>
#include "opencnc3d_build.h"
#ifdef __cplusplus
extern "C" {
#endif
void* cnc_ios_dlopen(const char*, int);
void* cnc_ios_dlsym(void*, const char*);
int cnc_ios_dlclose(void*);
const char* cnc_ios_dlerror(void);
void* cnc_ios_gl_proc(const char*);
void cnc_ios_swap_window(SDL_Window*);
int cnc_ios_poll_event(SDL_Event*);
#ifdef __cplusplus
}
#endif
/* These substitutions are confined to the iOS renderer target. */
#define dlopen cnc_ios_dlopen
#define dlsym cnc_ios_dlsym
#define dlclose cnc_ios_dlclose
#define dlerror cnc_ios_dlerror
#define SDL_PollEvent cnc_ios_poll_event
#define SDL_GL_GetProcAddress cnc_ios_gl_proc
#define SDL_GL_SwapWindow cnc_ios_swap_window
#endif
