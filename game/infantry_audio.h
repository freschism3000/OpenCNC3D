#ifndef OPENCNC3D_INFANTRY_AUDIO_H
#define OPENCNC3D_INFANTRY_AUDIO_H
#include "infantry_audio_math.h"
struct InfantryAudioClip { Uint8* pcm; Uint32 bytes; bool tried; };
struct InfantryAudioStep { int beat; float phase; };
struct InfantryAudioVoice { int handle, id, gain, pan, variant, pitch, volume; float x, z; };
static InfantryAudioClip g_infantryClips[INFANTRY_AUDIO_VARIANTS] = {};
static std::map<int, InfantryAudioStep> g_infantrySteps;
static std::vector<InfantryAudioVoice> g_infantryVoices;
static unsigned g_infantryRng = 0, g_infantryPlays = 0;
/* The crowd limit's account (infantry_audio_math.h): steps it held back since boot, and
   this tick's walkers in earshot, their level and how many steps it started. */
static unsigned g_infantryHeld = 0;
static int g_infantryWalkers = 0, g_infantryStarted = 0;
static float g_infantryCrowd = 1.0f;
static int g_infantryFrame = -1;
static float g_infantryZoom = 0;

static InfantryAudioClip* infantry_audio_clip(int n)
{
    InfantryAudioClip& c = g_infantryClips[n];
    if (c.tried) return c.pcm ? &c : NULL;
    c.tried = true;
    const char* custom = getenv("OPENCNC3D_FOLEY_DIR");
    const char* dirs[] = {"foley", "data/foley", "../data/foley"};
    for (int i=0;i<(custom && *custom ? 1:3);++i) {
        char path[1024]; SDL_AudioSpec spec;
        snprintf(path,sizeof path,"%s/infantry_step_%02d.wav",custom && *custom ? custom:dirs[i],n+1);
        if (!SDL_LoadWAV(path,&spec,&c.pcm,&c.bytes)) continue;
        if (spec.freq==MIX_RATE && spec.channels==2 && spec.format==AUDIO_S16LSB && c.bytes && c.bytes%4==0) {
            fprintf(stderr,"INFANTRYAUDIO|loaded|variant=%d|frames=%u\n",n+1,c.bytes/4);
            return &c;
        }
        SDL_FreeWAV(c.pcm); c.pcm=NULL;
    }
    fprintf(stderr,"INFANTRYAUDIO|missing/invalid|variant=%d\n",n+1); return NULL;
}
static void infantry_audio_stop(void)
{
    if (g_au) {
        audio_backend_lock();
        for (size_t i=0;i<g_infantryVoices.size();++i)
            mixer_voice_stop(cnc_audio_mixer(g_au),g_infantryVoices[i].handle);
        audio_backend_unlock();
    }
    g_infantryVoices.clear(); g_infantrySteps.clear(); g_infantryFrame=-1;
}
static void infantry_audio_free(void)
{
    infantry_audio_stop();
    for (int i=0;i<INFANTRY_AUDIO_VARIANTS;++i) { SDL_FreeWAV(g_infantryClips[i].pcm); g_infantryClips[i]=InfantryAudioClip(); }
}
static void infantry_audio_update(int fbw,int fbh,bool running)
{
    if (!g_au || !g_fx.enabled || g_editOn) { infantry_audio_stop(); return; }
    if (g_infantryFrame>g_engineFrame) infantry_audio_stop();
    g_infantryZoom = g_camMode==CAM_N64
        ? ambience_zoom_fraction(g_dist,dist_min_now(),DIST_MAX_OURS)
        : ambience_zoom_fraction(g_zoom,zoom_max_now(),ZOOM_MIN);
    Mixer* mix=cnc_audio_mixer(g_au);
    float vx0,vx1,vz0,vz1; view_ground_bounds(fbw,fbh,&vx0,&vx1,&vz0,&vz1);
    audio_backend_lock();
    for (size_t i=0;i<g_infantryVoices.size();) {
        InfantryAudioVoice& v=g_infantryVoices[i];
        if (!mixer_voice_active(mix,v.handle)) {g_infantryVoices.erase(g_infantryVoices.begin()+i);continue;}
        float col,row;
        const float dx=v.x<vx0?vx0-v.x:v.x>vx1?v.x-vx1:0;
        const float dz=v.z<vz0?vz0-v.z:v.z>vz1?v.z-vz1:0;
        v.gain=infantry_audio_gain(g_infantryZoom,ambience_attenuation(sqrtf(dx*dx+dz*dz)),v.volume);
        if (project_point(v.x,terrain_y(v.x,v.z),v.z,fbw,fbh,&col,&row)) v.pan=vehicle_audio_screen_pan(col,(float)fbw);
        if (running) mixer_voice_fade(mix,v.handle,v.gain,v.pan,12);
        else mixer_voice_fade_stop(mix,v.handle,25);
        ++i;
    }
    audio_backend_unlock();
    if (!running) {g_infantrySteps.clear();g_infantryFrame=g_engineFrame;return;}
    if (g_infantryFrame==g_engineFrame) return;  // render/probe calls never duplicate steps
    g_infantryFrame=g_engineFrame;
    std::map<int,InfantryAudioStep> next;
    struct Candidate { int id, variant, pitch, volume, pan; float distance, x, z; };
    std::vector<Candidate> due;
    int walkers=0;
    for (size_t i=0;i<g_objects.size();++i) {
        const SimObject& o=g_objects[i];
        if (o.kind!=K_INFANTRY || o.id<0 || o.str<=0 || o.limbo || !visible(o) || !infantry_audio_walks(o.type, g_raModule)) continue;
        std::map<int,AnimState>::const_iterator anim=g_anim.find(o.id);
        if (anim==g_anim.end()) continue;
        const AnimState& a=anim->second;
        // Two contacts per authored gait cycle. Sprite-only soldiers use distance,
        // with a 0.6-cell stride, rather than a timer that runs while blocked.
        const float phase=m3inf_gait_stride(o,a.spd)>1e-4f ? a.gph : a.dist/0.6f;
        const int beat=(int)floorf(phase*2);
        next[o.id]=InfantryAudioStep{beat,phase};
        /* Red Alert's dog gallops on its CRAWL clip (DO_CRAWL is how its brain moves it);
           a crawling soldier still makes no step */
        const bool striding=a.slot==M3INF_RUN || (a.slot==M3INF_CRAWL && infantry_audio_is_paw(o.type));
        if (!striding || a.step<=0 || a.step>1.5f) continue;
        float col,row;
        if (!project_point(o.ex,terrain_y(o.ex,o.ez),o.ez,fbw,fbh,&col,&row)) continue;
        const float dx=o.ex<vx0?vx0-o.ex:o.ex>vx1?o.ex-vx1:0;
        const float dz=o.ez<vz0?vz0-o.ez:o.ez>vz1?o.ez-vz1:0;
        const float distance=ambience_attenuation(sqrtf(dx*dx+dz*dz));
        if (distance<=0) continue;
        ++walkers;   // in earshot and walking, whether or not his foot lands this tick
        std::map<int,InfantryAudioStep>::const_iterator previous=g_infantrySteps.find(o.id);
        if (previous!=g_infantrySteps.end() && (beat<=previous->second.beat || phase-previous->second.phase>2)) continue;
        if (!g_infantryRng) g_infantryRng=vehicle_audio_seed()^0x9e3779b9u;
        Candidate c;
        c.id=o.id;
        c.variant=(int)vehicle_audio_random_offset(g_infantryRng,INFANTRY_AUDIO_VARIANTS);
        c.pitch=900+(int)vehicle_audio_random_offset(g_infantryRng,201);
        c.volume=900+(int)vehicle_audio_random_offset(g_infantryRng,201);
        /* the dog: the mixer's highest pitch (it allows ten per cent either way) at 60% */
        if (infantry_audio_is_paw(o.type)) { c.pitch=1100; c.volume=c.volume*6/10; }
        c.pan=vehicle_audio_screen_pan(col,(float)fbw);
        c.distance=distance; c.x=o.ex; c.z=o.ez;
        due.push_back(c);
    }
    // THE CROWD LIMIT (infantry_audio_math.h): the crowd sets every step's level, the
    // steps still ringing set how many may start, and the loudest of this tick's go first.
    g_infantryWalkers=walkers;
    g_infantryCrowd=infantry_audio_crowd_gain(walkers);
    std::vector<int> gains(due.size()), ids(due.size()), order(due.size());
    for (size_t i=0;i<due.size();++i) {
        due[i].volume=(int)(due[i].volume*g_infantryCrowd+0.5f);
        gains[i]=infantry_audio_gain(g_infantryZoom,due[i].distance,due[i].volume);
        ids[i]=due[i].id;
    }
    const int chosen=due.empty() ? 0 :
        infantry_audio_pick(&gains[0],&ids[0],(int)due.size(),
                            infantry_audio_budget((int)g_infantryVoices.size()),&order[0]);
    g_infantryHeld+=(unsigned)(due.size()-chosen);
    g_infantryStarted=0;
    for (int k=0;k<chosen;++k) {
        const Candidate& c=due[order[k]];
        InfantryAudioClip* clip=infantry_audio_clip(c.variant);
        if (!clip) continue;
        audio_backend_lock();
        const int handle=mixer_play_foley_pitched(mix,(short*)clip->pcm,clip->bytes/4,c.pan,c.pitch);
        if (handle>=0) {
            mixer_voice_fade(mix,handle,gains[order[k]],c.pan,6);
            g_infantryVoices.push_back(InfantryAudioVoice{handle,c.id,gains[order[k]],c.pan,c.variant,c.pitch,c.volume,c.x,c.z});
            ++g_infantryPlays;
            ++g_infantryStarted;
        }
        audio_backend_unlock();
    }
    g_infantrySteps.swap(next);
}
static void infantry_audio_dump(void)
{
    printf("INFANTRYAUDIO|state|zoom=%.4f|zoomgain=%.4f|plays=%u|voices=%zu\n",
        g_infantryZoom,infantry_audio_zoom_gain(g_infantryZoom),g_infantryPlays,g_infantryVoices.size());
    printf("INFANTRYAUDIO|limit|walkers=%d|crowd=%.4f|started=%d|held=%u|maxvoices=%d|pertick=%d\n",
        g_infantryWalkers,g_infantryCrowd,g_infantryStarted,g_infantryHeld,
        (int)INFANTRY_AUDIO_MAX_VOICES,(int)INFANTRY_AUDIO_STARTS_PER_TICK);
    for (size_t i=0;i<g_infantryVoices.size();++i) {
        const InfantryAudioVoice& v=g_infantryVoices[i];
        printf("INFANTRYAUDIO|voice|id=%d|gain=%d|pan=%d|variant=%d|pitch=%d|volume=%d\n",v.id,v.gain,v.pan,v.variant+1,v.pitch,v.volume);
    }
}
#endif
