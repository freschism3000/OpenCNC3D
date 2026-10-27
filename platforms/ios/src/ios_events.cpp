#include <SDL.h>
#include <stdio.h>
#include "ios_controls.h"
/* The legacy mission loop owns its frames. Block it while iOS suspends the app,
   so neither simulation ticks nor GL work continue after the background event. */
extern "C" int cnc_ios_poll_event(SDL_Event* event){
    static bool background=false;
    int found=SDL_PollEvent(event);
    if(found&&(event->type==SDL_APP_WILLENTERBACKGROUND||event->type==SDL_APP_DIDENTERBACKGROUND)){
        cnc_ios_controls_interrupt();
        background=true;fprintf(stderr,"IOS|lifecycle|paused\n");
    }
    while(background){
        if(!SDL_WaitEventTimeout(event,100))continue;
        if(event->type==SDL_APP_WILLENTERFOREGROUND||event->type==SDL_APP_DIDENTERFOREGROUND){
            background=false;fprintf(stderr,"IOS|lifecycle|resumed\n");return 1;
        }
        if(event->type==SDL_APP_TERMINATING||event->type==SDL_QUIT){background=false;event->type=SDL_QUIT;return 1;}
    }
    return found;
}
