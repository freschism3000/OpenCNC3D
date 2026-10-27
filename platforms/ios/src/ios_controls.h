#ifndef CNC_IOS_CONTROLS_H
#define CNC_IOS_CONTROLS_H
#include <SDL.h>
#ifdef __cplusplus
extern "C" {
#endif
void cnc_ios_controls_install(SDL_Window *window);
void cnc_ios_controls_refresh(void);
void cnc_ios_controls_interrupt(void);
void cnc_ios_controls_shutdown(void);
#ifdef __cplusplus
}
#endif
#endif
