#import <Foundation/Foundation.h>
#include <SDL.h>
#include <GL/gl.h>
#include <stdio.h>
#include <unistd.h>
#include <vector>
#include <string>
#include "cnc_game.h"
#include "dossave.h"
#include "audioboot.h"
#include "ios_graphics.h"
#include "ios_controls.h"
#include "ios_game_bridge.h"

int main(int argc,char** argv){
    @autoreleasepool {
        setvbuf(stdout,NULL,_IONBF,0);setvbuf(stderr,NULL,_IONBF,0);
        NSString* data=[[[NSBundle mainBundle] resourcePath] stringByAppendingPathComponent:@"GameData"];
        if(chdir([data fileSystemRepresentation])){perror("IOS|resources");return 1;}
        NSString* documents=[NSHomeDirectory() stringByAppendingPathComponent:@"Documents"];
        fprintf(stderr,"IOS|boot|real-Tiberian-Dawn|resources=%s|documents=%s\n",[data UTF8String],[documents UTF8String]);
        std::vector<std::string> args={"OpenCNC3D","--dylib","static:TiberianDawn","--scen","SCG01EA","--pack","SCG01EA.pack","--dir","missions/","--content","content/","--dosdata","dosdata/"};
        bool mobileFixture=false,mobileSelftest=false;
        for(int i=1;i<argc;i++){
            if(!strcmp(argv[i],"--mobile-fixture"))mobileFixture=true;
            else if(!strcmp(argv[i],"--mobile-selftest")){mobileSelftest=true;mobileFixture=true;}
            else args.emplace_back(argv[i]);
        }
        std::vector<char*> pointers;for(auto& a:args)pointers.push_back(&a[0]);
        GameOpts opts={};if(game_parse_args((int)pointers.size(),pointers.data(),&opts))return 2;
        /* The first port proves the real baseline mission. Desktop Enhanced remains unchanged. */
        opts.models3d="";opts.mottle="";opts.sky3d=NULL;opts.tib3d=NULL;opts.efxEnh=NULL;
        SDL_SetHint(SDL_HINT_ORIENTATIONS,"LandscapeLeft LandscapeRight");
        SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR,"1");
        SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,"0");
        SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS,"0");
        if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_TIMER)){fprintf(stderr,"IOS|sdl|%s\n",SDL_GetError());return 1;}
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
        SDL_GL_SetAttribute(SDL_GL_RED_SIZE,8);SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,8);SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,8);SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE,8);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,8);SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
        SDL_Window* window=SDL_CreateWindow("OpenCNC3D",0,0,1280,720,SDL_WINDOW_OPENGL|SDL_WINDOW_ALLOW_HIGHDPI|SDL_WINDOW_FULLSCREEN);
        if(!window){fprintf(stderr,"IOS|window|%s\n",SDL_GetError());SDL_Quit();return 1;}
        SDL_GLContext context=SDL_GL_CreateContext(window);
        if(!context){fprintf(stderr,"IOS|context|%s\n",SDL_GetError());SDL_DestroyWindow(window);SDL_Quit();return 1;}
        SDL_GL_MakeCurrent(window,context);
        if(!cnc_ios_graphics_init(window))return 1;
        fprintf(stderr,"IOS|graphics|version=%s|renderer=%s\n",glGetString(GL_VERSION),glGetString(GL_RENDERER));
        SDL_GL_GetDrawableSize(window,&opts.w,&opts.h);
        char* pref=SDL_GetPrefPath("OpenCNC3D","iOS");if(pref){ds_set_dir(pref);SDL_free(pref);}
        AudioBootOpts sound={};sound.dosdata=opts.dosdata;sound.wav=opts.audiowav;sound.music_vol255=opts.musicvol;sound.sound_vol255=opts.soundvol;
        sound.silent=opts.nosound||((opts.shot||opts.script)&&!opts.audiowav);
        CncAudio* audio=audio_boot(&sound);game_set_audio(audio);
        int result=1;
        if(game_boot(window,&opts)){
            fprintf(stderr,"IOS|mission|booted|%s|drawable=%dx%d\n",opts.scen,opts.w,opts.h);
            if(mobileFixture&&!cnc_ios_prepare_mobile_fixture())result=1;
            else if(mobileSelftest)result=cnc_ios_game_selftest()?0:1;
            else {
                if(!opts.script&&!opts.shot)cnc_ios_controls_install(window);
                if(opts.script)result=game_run_script(window,&opts);
                else if(opts.shot)result=game_run_shot(window,&opts);
                else result=game_loop(window,&opts)==GAME_EXIT_ERROR?1:0;
                cnc_ios_controls_shutdown();
            }
            game_shutdown();
        }else fprintf(stderr,"IOS|mission|failed|%s\n",game_boot_refusal());
        game_set_audio(NULL);audio_boot_shutdown(audio);
        cnc_ios_graphics_shutdown();SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
        fprintf(stderr,"IOS|exit|%d\n",result);return result;
    }
}
