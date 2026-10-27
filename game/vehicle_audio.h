#ifndef OPENCNC3D_VEHICLE_AUDIO_H
#define OPENCNC3D_VEHICLE_AUDIO_H
#include "vehicle_audio_math.h"
struct VehicleAudioEmitter {
    int clip, voice, gain, pan;
    bool seen, driving;
    long offset;
};
static std::map<int, VehicleAudioEmitter> g_vehicleAudio;
static int g_vehicleAudioFrame = -1;
static std::vector<int> g_vehicleAudioTails;
static unsigned g_vehicleAudioRng = 0;
/* The private audio RNGs' first state. The renderer clock, so two players never hear the
   same phase; OPENCNC3D_AUDIO_SEED=<n> fixes it, so two recordings of one script are the
   same samples and a sound's own level can be read as the difference between them. */
static unsigned vehicle_audio_seed(void)
{
    const char* fixed = getenv("OPENCNC3D_AUDIO_SEED");
    if (fixed && *fixed) { const unsigned s = (unsigned)strtoul(fixed, NULL, 0); return s ? s : 1u; }
    const Uint64 clock = SDL_GetPerformanceCounter();
    return (unsigned)clock ^ (unsigned)(clock >> 32) ^ SDL_GetTicks();
}

static void vehicle_audio_stop(void)
{
    if (g_au) {
        audio_backend_lock();
        for (std::map<int, VehicleAudioEmitter>::iterator it = g_vehicleAudio.begin();
             it != g_vehicleAudio.end(); ++it)
            mixer_voice_stop(cnc_audio_mixer(g_au), it->second.voice);
        for (size_t i = 0; i < g_vehicleAudioTails.size(); ++i)
            mixer_voice_stop(cnc_audio_mixer(g_au), g_vehicleAudioTails[i]);
        audio_backend_unlock();
    }
    g_vehicleAudioTails.clear();
    g_vehicleAudio.clear();
    g_vehicleAudioFrame = -1;
}

static void vehicle_audio_update(int fbw, int fbh, bool running)
{
    if (!g_au || !g_fx.enabled || g_editOn) { vehicle_audio_stop(); return; }
    // Save/restart time discontinuities must not carry the previous journey's emitters.
    if (g_vehicleAudioFrame > g_engineFrame) vehicle_audio_stop();
    g_vehicleAudioFrame = g_engineFrame;
    const float zoom = g_camMode == CAM_N64
        ? ambience_zoom_fraction(g_dist, dist_min_now(), DIST_MAX_OURS)
        : ambience_zoom_fraction(g_zoom, zoom_max_now(), ZOOM_MIN);
    float vx0, vx1, vz0, vz1;
    view_ground_bounds(fbw, fbh, &vx0, &vx1, &vz0, &vz1);
    for (std::map<int, VehicleAudioEmitter>::iterator it = g_vehicleAudio.begin();
         it != g_vehicleAudio.end(); ++it) it->second.seen = false;
    if (running) for (size_t i = 0; i < g_objects.size(); ++i) {
        const SimObject& o = g_objects[i];
        if (o.kind != K_UNIT || o.id < 0 || o.str <= 0 || o.limbo || !visible(o)) continue;
        const int clipIndex = g_raModule ? vehicle_audio_clip_index_ra(o.type)
                                         : vehicle_audio_clip_index(o.type);
        if (clipIndex < 0 || unit_is_still(o.id)) continue;
        float x = o.ex, z = o.ez;
        if (g_smoothMove) {
            std::map<int, SmoothPos>::const_iterator p = g_smoothPos.find(smooth_key(o));
            if (p != g_smoothPos.end()) smooth_at(p->second, g_tickAlpha, &x, &z);
        }
        float col, row;
        if (!project_point(x, terrain_y(x,z), z, fbw, fbh, &col, &row)) continue;
        // On-screen vehicles retain the requested zoom gain. Outside the view, fade
        // to silence over eight ground cells so a distant army cannot fill the mix.
        float dx = x < vx0 ? vx0-x : x > vx1 ? x-vx1 : 0;
        float dz = z < vz0 ? vz0-z : z > vz1 ? z-vz1 : 0;
        float placement = ambience_attenuation(sqrtf(dx*dx + dz*dz));
        const int gain = (int)(MIX_UNITY * vehicle_audio_zoom_gain(zoom) * placement + 0.5f);
        if (gain <= 0) continue;
        const int pan = vehicle_audio_screen_pan(col, (float)fbw);
        AmbienceClip* clip = ambience_clip(clipIndex);
        if (!clip) continue;
        std::map<int, VehicleAudioEmitter>::iterator found = g_vehicleAudio.find(o.id);
        if (found == g_vehicleAudio.end()) {
            VehicleAudioEmitter emitter = {clipIndex, -1, gain, pan, true, false, 0};
            found = g_vehicleAudio.insert(std::make_pair(o.id, emitter)).first;
        }
        VehicleAudioEmitter& emitter = found->second;
        emitter.seen = true; emitter.gain = gain; emitter.pan = pan;
        audio_backend_lock();
        Mixer* mix = cnc_audio_mixer(g_au);
        if (emitter.clip != clipIndex) {
            mixer_voice_stop(mix, emitter.voice); emitter.voice = -1; emitter.clip = clipIndex;
        }
        if (!emitter.driving && mixer_voice_active(mix, emitter.voice)) {
            // A quick restart gets a fresh random start too. Let the previous journey's
            // tail finish its fade instead of cutting audible PCM at the restart.
            mixer_voice_fade_stop(mix, emitter.voice, 320);
            g_vehicleAudioTails.push_back(emitter.voice);
            emitter.voice = -1;
        }
        if (!mixer_voice_active(mix, emitter.voice)) {
            if (!g_vehicleAudioRng) g_vehicleAudioRng = vehicle_audio_seed();
            emitter.offset = vehicle_audio_random_offset(g_vehicleAudioRng, clip->bytes / 4);
            emitter.voice = mixer_play_vehicle_loop(mix, (short*)clip->pcm, clip->bytes/4,
                                                     pan, emitter.offset);
        }
        emitter.driving = emitter.voice >= 0;
        mixer_voice_fade(mix, emitter.voice, gain, pan, 220);
        audio_backend_unlock();
    }
    audio_backend_lock();
    Mixer* mix = cnc_audio_mixer(g_au);
    for (size_t i = 0; i < g_vehicleAudioTails.size();) {
        if (!mixer_voice_active(mix, g_vehicleAudioTails[i]))
            g_vehicleAudioTails.erase(g_vehicleAudioTails.begin() + i);
        else ++i;
    }
    for (std::map<int, VehicleAudioEmitter>::iterator it = g_vehicleAudio.begin();
         it != g_vehicleAudio.end();) {
        if (!it->second.seen) {
            it->second.driving = false;
            it->second.gain = 0;
            mixer_voice_fade_stop(mix, it->second.voice, 320);
        }
        if (!mixer_voice_active(mix, it->second.voice)) g_vehicleAudio.erase(it++);
        else ++it;
    }
    audio_backend_unlock();
}

static void vehicle_audio_dump(void)
{
    const float zoom = g_camMode == CAM_N64
        ? ambience_zoom_fraction(g_dist, dist_min_now(), DIST_MAX_OURS)
        : ambience_zoom_fraction(g_zoom, zoom_max_now(), ZOOM_MIN);
    printf("VEHICLEAUDIO|state|zoom=%.4f|zoomgain=%.4f|emitters=%zu\n",
           zoom, vehicle_audio_zoom_gain(zoom), g_vehicleAudio.size());
    for (std::map<int, VehicleAudioEmitter>::const_iterator it = g_vehicleAudio.begin();
         it != g_vehicleAudio.end(); ++it)
        printf("VEHICLEAUDIO|emitter|id=%d|clip=%s|gain=%d|pan=%d|moving=%d|offset=%ld\n",
               it->first, g_ambNames[it->second.clip], it->second.gain,
               it->second.pan, it->second.seen ? 1 : 0, it->second.offset);
}
#endif
