#ifndef OPENCNC3D_CINEMATIC_AUDIO_H
#define OPENCNC3D_CINEMATIC_AUDIO_H
#include "cinematic_audio_math.h"
struct CinematicClip { Uint8* pcm; Uint32 bytes; bool tried; };
struct CinematicEvent { int kind; float x,z,mass; bool splash; };
struct CinematicVoice { int handle,kind,gain,pan; float x,z; };
static CinematicClip g_cinematicClips[CIN_COUNT]={};
static std::vector<CinematicEvent> g_cinematicEvents;
static std::vector<CinematicVoice> g_cinematicVoices;
static unsigned g_cinematicPlays[CIN_COUNT]={};
static unsigned g_cinematicSplashes=0;   /* placement impacts played as the water splash */
static int g_cinematicLast[CIN_COUNT]={-1000,-1000,-1000,-1000,-1000};
static int g_cinematicRumble=-1, g_cinematicGain=0;
static long g_cinematicAt[CIN_COUNT]={-1,-1,-1,-1,-1};  /* output frame each kind last started */
/* Under a placement's impact the rest of the mix drops 8 dB for a quarter second and
   comes back over 1.1 s; under its rumble, 4.4 dB while it sounds. EVA is never ducked. */
#define CIN_IMPACT_DUCK .60f
#define CIN_RUMBLE_DUCK .40f
#define CIN_RUMBLE_GAIN 1200
static const char* g_cinematicNames[CIN_COUNT]={"placement_impact","building_collapse","vehicle_impact","structural_damage","construction_rumble"};
static CinematicClip* cinematic_audio_clip(int kind)
{
    CinematicClip& c=g_cinematicClips[kind];
    if (c.tried) return c.pcm?&c:NULL;
    c.tried=true;
    const char* custom=getenv("OPENCNC3D_CINEMATIC_DIR");
    const char* dirs[]={"cinematic","data/cinematic","../data/cinematic"};
    for(int i=0;i<(custom && *custom?1:3);++i) {
        char path[1024]; SDL_AudioSpec spec;
        snprintf(path,sizeof path,"%s/%s.wav",custom && *custom?custom:dirs[i],g_cinematicNames[kind]);
        if (!SDL_LoadWAV(path,&spec,&c.pcm,&c.bytes)) continue;
        if (spec.freq==MIX_RATE && spec.channels==2 && spec.format==AUDIO_S16LSB && c.bytes && c.bytes%4==0) {
            fprintf(stderr,"CINEMATICAUDIO|loaded|%s|frames=%u\n",g_cinematicNames[kind],c.bytes/4);return &c;
        }
        SDL_FreeWAV(c.pcm);c.pcm=NULL;
    }
    fprintf(stderr,"CINEMATICAUDIO|missing/invalid|%s\n",g_cinematicNames[kind]);return NULL;
}
// `splash`: a placement impact played as Red Alert's water splash (VOC_SPLASH, SPLASH9) in
// place of placement_impact, at the same gain and with the same duck: a building in the sea.
static void cinematic_audio_note_as(int kind,float x,float z,float mass,bool splash)
{
    if (!g_au || !g_fx.enabled || g_editOn || kind<0 || kind>=CIN_RUMBLE || !cell_shown((int)x,(int)z) || shroud_cell_hidden((int)x,(int)z)) return;
    // Keep the strongest of same-kind simultaneous impacts, avoiding coherent stacking.
    for(size_t i=0;i<g_cinematicEvents.size();++i)
        if (g_cinematicEvents[i].kind==kind) {
            if (mass>g_cinematicEvents[i].mass) g_cinematicEvents[i]=CinematicEvent{kind,x,z,mass,splash};
            return;
        }
    g_cinematicEvents.push_back(CinematicEvent{kind,x,z,mass,splash});
}
static void cinematic_audio_note(int kind,float x,float z,float mass)
{
    cinematic_audio_note_as(kind,x,z,mass,false);
}
static void cinematic_audio_stop(void)
{
    if (g_au) {
        audio_backend_lock();Mixer* mix=cnc_audio_mixer(g_au);
        for(size_t i=0;i<g_cinematicVoices.size();++i) mixer_voice_stop(mix,g_cinematicVoices[i].handle);
        mixer_voice_stop(mix,g_cinematicRumble);mixer_duck_reset(mix);audio_backend_unlock();
    }
    g_cinematicEvents.clear();g_cinematicVoices.clear();g_cinematicRumble=-1;g_cinematicGain=0;
    for(int i=0;i<CIN_COUNT;++i) g_cinematicLast[i]=-1000;
}
static void cinematic_audio_free(void)
{
    cinematic_audio_stop();
    for(int i=0;i<CIN_COUNT;++i) { SDL_FreeWAV(g_cinematicClips[i].pcm);g_cinematicClips[i]=CinematicClip(); }
}
static float cinematic_audio_place(float x,float z,int fbw,int fbh,float x0,float x1,float z0,float z1,int* pan)
{
    if (!cell_shown((int)x,(int)z) || shroud_cell_hidden((int)x,(int)z)) return 0;
    const float dx=x<x0?x0-x:x>x1?x-x1:0, dz=z<z0?z0-z:z>z1?z-z1:0;
    float col,row;*pan=0;
    if (project_point(x,terrain_y(x,z),z,fbw,fbh,&col,&row)) *pan=vehicle_audio_screen_pan(col,(float)fbw);
    return cinematic_audio_distance(sqrtf(dx*dx+dz*dz));
}
static void cinematic_audio_update(int fbw,int fbh,bool running)
{
    if (!g_au || !g_fx.enabled || g_editOn) {cinematic_audio_stop();return;}
    Mixer* mix=cnc_audio_mixer(g_au);
    float x0,x1,z0,z1;view_ground_bounds(fbw,fbh,&x0,&x1,&z0,&z1);
    if (!running) {
        g_cinematicEvents.clear();audio_backend_lock();
        for(size_t i=0;i<g_cinematicVoices.size();++i) mixer_voice_fade_stop(mix,g_cinematicVoices[i].handle,40);
        mixer_voice_fade_stop(mix,g_cinematicRumble,80);audio_backend_unlock();g_cinematicGain=0;return;
    }
    audio_backend_lock();
    for(size_t i=0;i<g_cinematicVoices.size();) {
        CinematicVoice& v=g_cinematicVoices[i];
        if (!mixer_voice_active(mix,v.handle)) {g_cinematicVoices.erase(g_cinematicVoices.begin()+i);continue;}
        int pan;float distance=cinematic_audio_place(v.x,v.z,fbw,fbh,x0,x1,z0,z1,&pan);
        mixer_voice_fade(mix,v.handle,(int)(v.gain*distance),pan,25);++i;
    }
    audio_backend_unlock();
    for(size_t i=0;i<g_cinematicEvents.size();++i) {
        const CinematicEvent& e=g_cinematicEvents[i];int pan;
        const float distance=cinematic_audio_place(e.x,e.z,fbw,fbh,x0,x1,z0,z1,&pan);
        if (distance<=0 || g_engineFrame-g_cinematicLast[e.kind]<3) continue;
        const int gain=cinematic_audio_impact_gain(e.kind==CIN_PLACE,e.mass);
        if (e.splash) {
            // the 1996 clip from the sound bank, which pins it while it plays (and locks the
            // backend itself); a disc without it is silent here, as the bank's other misses are
            int h=cnc_audio_play_named(g_au,"SPLASH9.AUD",MIX_BUS_FX,0,pan,200);
            if (h<0) continue;
            audio_backend_lock();
            mixer_voice_fade(mix,h,(int)(gain*distance),pan,4);g_cinematicVoices.push_back(CinematicVoice{h,e.kind,gain,pan,e.x,e.z});
            ++g_cinematicPlays[e.kind];++g_cinematicSplashes;g_cinematicLast[e.kind]=g_engineFrame;g_cinematicAt[e.kind]=mixer_frames_rendered(mix);
            mixer_duck(mix,cinematic_audio_duck_depth(CIN_IMPACT_DUCK,gain*distance/1000.0f,mixer_bus_gain_get(mix,MIX_BUS_FX)),12,230,1100);
            audio_backend_unlock();
            continue;
        }
        CinematicClip* c=cinematic_audio_clip(e.kind);if (!c) continue;
        audio_backend_lock();int h=mixer_play_cinematic(mix,(short*)c->pcm,c->bytes/4,pan,0);
        if (h>=0) {
            mixer_voice_fade(mix,h,(int)(gain*distance),pan,4);g_cinematicVoices.push_back(CinematicVoice{h,e.kind,gain,pan,e.x,e.z});
            ++g_cinematicPlays[e.kind];g_cinematicLast[e.kind]=g_engineFrame;g_cinematicAt[e.kind]=mixer_frames_rendered(mix);
            // The placement lands in a hush: the bed steps back so the hit has headroom.
            if (e.kind==CIN_PLACE)
                mixer_duck(mix,cinematic_audio_duck_depth(CIN_IMPACT_DUCK,gain*distance/1000.0f,mixer_bus_gain_get(mix,MIX_BUS_FX)),12,230,1100);
        }
        audio_backend_unlock();
    }
    g_cinematicEvents.clear();
    // Use the actual placement rig timing and strength, including late Yard landing,
    // cut-short construction and the post-rise fade. Only owned visible placements.
    float best=0;int bestPan=0;
    for(std::map<long long,EplcBld>::const_iterator it=g_eplcBld.begin();eplc_rule_now() && it!=g_eplcBld.end();++it) {
        const EplcBld& E=it->second;
        if (E.mode!=EPS_PLACE || !E.mine || E.cut || E.model<0 || E.model>=(int)g_eplc.size()) continue;
        const EplcModel& P=g_eplc[E.model];const EplcRig& R=P.rig[0];int pan;
        const float env=cinematic_audio_rise((engine_time()-(float)E.start)/15,R.r0,R.r1,P.cam[2]);
        const float mass=(float)(E.fw>E.fh?E.fw:E.fh);
        const float gain=cinematic_audio_rumble_level(P.cam[1],mass,env)*cinematic_audio_place(E.wx,E.wz,fbw,fbh,x0,x1,z0,z1,&pan);
        if(gain>best) {best=gain;bestPan=pan;}
    }
    g_cinematicGain=(int)(best*CIN_RUMBLE_GAIN);
    if(g_cinematicGain>0 && !mixer_voice_active(mix,g_cinematicRumble)) {
        CinematicClip* c=cinematic_audio_clip(CIN_RUMBLE);
        if(c) {
            long s0,s1,xf;cinematic_audio_rumble_body(c->bytes/4,&s0,&s1,&xf);
            audio_backend_lock();g_cinematicRumble=mixer_play_cinematic(mix,(short*)c->pcm,c->bytes/4,bestPan,1);
            if(g_cinematicRumble>=0) mixer_voice_loop_xfade(mix,g_cinematicRumble,s0,s1,xf);
            audio_backend_unlock();if(g_cinematicRumble>=0) {++g_cinematicPlays[CIN_RUMBLE];g_cinematicAt[CIN_RUMBLE]=mixer_frames_rendered(mix);}}
    }
    audio_backend_lock();
    if(g_cinematicGain>0) {
        mixer_voice_fade(mix,g_cinematicRumble,g_cinematicGain,bestPan,60);
        if(mixer_voice_active(mix,g_cinematicRumble))
            mixer_duck(mix,cinematic_audio_duck_depth(CIN_RUMBLE_DUCK,g_cinematicGain/1000.0f,mixer_bus_gain_get(mix,MIX_BUS_FX)),150,150,600);
    }
    else mixer_voice_fade_stop(mix,g_cinematicRumble,100);
    audio_backend_unlock();
}
static void cinematic_audio_dump(void)
{
    printf("CINEMATICAUDIO|state|place=%u|collapse=%u|vehicle=%u|damage=%u|rumble=%u|rumblegain=%d|voices=%zu|pending=%zu|duck=%d|placeat=%ld|rumbleat=%ld|splash=%u\n",g_cinematicPlays[0],g_cinematicPlays[1],g_cinematicPlays[2],g_cinematicPlays[3],g_cinematicPlays[4],g_cinematicGain,g_cinematicVoices.size(),g_cinematicEvents.size(),
           g_au?mixer_duck_get(cnc_audio_mixer(g_au)):1000,g_cinematicAt[CIN_PLACE],g_cinematicAt[CIN_RUMBLE],g_cinematicSplashes);
}
#endif
