#ifndef OPENCNC3D_IOS_GRAPHICS_H
#define OPENCNC3D_IOS_GRAPHICS_H
#include <SDL.h>
#ifdef __cplusplus
extern "C" {
#endif
int cnc_ios_graphics_init(SDL_Window*);
void cnc_ios_graphics_shutdown(void);
void* cnc_ios_gl_proc(const char*);
void cnc_ios_swap_window(SDL_Window*);
#ifdef __cplusplus
}
#endif
#endif
