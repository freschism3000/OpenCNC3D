#ifndef OPENCNC3D_AMBIENCE_MOD_H
#define OPENCNC3D_AMBIENCE_MOD_H
#include "ambience_math.h"

struct AmbienceClip {
    Uint8* pcm;
    Uint32 bytes;
    int voice;
    bool tried;
    long loop_start, loop_end; // the steady body a bed loops over; see ambience_body
};
static AmbienceClip g_ambClips[12] = {};
static const char* const g_ambNames[12] = {
    "desert_zoomin", "desert_zoomout", "temperate_zoomin", "temperate_zoomout",
    "river_zoomin", "water_zoomin", "water_zoomout", "hud_click", "hud_mouseover",
    "tank_drive", "small_tank_drive", "vehicle_drive"
};
static float g_ambZoom = 0, g_ambOcean = 0, g_ambRiver = 0;
static int g_ambHover = -1;
static unsigned g_ambHudPlays[2] = {0, 0};
static int g_ambHudLastClip = -1;   /* g_ambNames index of the last HUD sound started */

static AmbienceClip* ambience_clip(int index)
{
    AmbienceClip* c = &g_ambClips[index];
    if (c->tried) return c->pcm ? c : NULL;
    c->tried = true;
    c->voice = -1;
    const char* root = getenv("OPENCNC3D_AMBIENCE_DIR");
    const char* dirs[] = { "ambience", "data/ambience", "../data/ambience" };
    SDL_AudioSpec spec;
    for (int i = 0; i < (root && *root ? 1 : 3); ++i) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s.wav", root && *root ? root : dirs[i], g_ambNames[index]);
        if (!SDL_LoadWAV(path, &spec, &c->pcm, &c->bytes)) continue;
        if (spec.freq != MIX_RATE || spec.format != AUDIO_S16LSB || spec.channels != 2 ||
            !c->bytes || c->bytes % 4) {
            SDL_FreeWAV(c->pcm); c->pcm = NULL;
            fprintf(stderr, "AMBIENCE|invalid PCM16 stereo/%d: %s\n", MIX_RATE, path);
            continue;
        }
        c->loop_start = 0;
        c->loop_end = c->bytes / 4;
        // Beds only (the first seven names); HUD and driving clips are not looped here.
        if (index < 7)
            ambience_body((const short*)c->pcm, c->bytes / 4, MIX_RATE, &c->loop_start, &c->loop_end);
        fprintf(stderr, "AMBIENCE|loaded|%s|frames=%u|body=%ld..%ld\n", g_ambNames[index],
                c->bytes / 4, c->loop_start, c->loop_end);
        return c;
    }
    fprintf(stderr, "AMBIENCE|missing|%s.wav\n", g_ambNames[index]);
    return NULL;
}

static void vehicle_audio_stop(void);
static void infantry_audio_stop(void);
static void infantry_audio_free(void);
static void cinematic_audio_stop(void);
static void cinematic_audio_free(void);

static void ambience_stop(void)
{
    vehicle_audio_stop();
    infantry_audio_stop();
    cinematic_audio_stop();
    if (g_au) {
        audio_backend_lock();
        for (int i = 0; i < 12; ++i)
            if (g_ambClips[i].tried) mixer_voice_stop(cnc_audio_mixer(g_au), g_ambClips[i].voice);
        audio_backend_unlock();
    }
    for (int i = 0; i < 12; ++i) g_ambClips[i].voice = -1;
    g_ambHover = -1;
}

/* At mission teardown, stop before releasing anything the callback may still read. */
static void ambience_free(void)
{
    ambience_stop();
    infantry_audio_free();
    cinematic_audio_free();
    for (int i = 0; i < 12; ++i) {
        SDL_FreeWAV(g_ambClips[i].pcm);
        g_ambClips[i] = AmbienceClip();
    }
}

static void ambience_bed(int index, float level, int pan = 0)
{
    AmbienceClip* c = &g_ambClips[index];
    // Inactive theaters are not loaded. Active pairs stay clocked while inaudible.
    if (!c->pcm && level <= 0) return;
    c = ambience_clip(index);
    if (!c) return;
    Mixer* mix = cnc_audio_mixer(g_au);
    const int gain = (int)(ambience_clamp(level) * MIX_UNITY + 0.5f);
    audio_backend_lock();
    if (!mixer_voice_active(mix, c->voice) && gain > 0) {
        c->voice = mixer_play_stereo(mix, MIX_BUS_AMBIENCE, (short*)c->pcm,
                                    c->bytes / 4, 0, 0, 255, 1);
        // Each pass overlaps the next instead of ending into it. A body too short for
        // two crossfades is refused by the mixer and loops plainly, as before.
        mixer_voice_loop_xfade(mix, c->voice, c->loop_start, c->loop_end,
                               (long)(AMBIENCE_XFADE_SECONDS * MIX_RATE));
    }
    mixer_voice_fade(mix, c->voice, gain, pan, 180);
    audio_backend_unlock();
}

static void ambience_hud_sound(bool click)
{
    if (!g_fx.enabled || !g_au || g_editOn) return;
    /* Crossed on purpose: a hover plays hud_click and a press plays hud_mouseover.
       The files keep the names they were imported under, and their levels. */
    const int clip = click ? 8 : 7;
    AmbienceClip* c = ambience_clip(clip);
    if (!c) return;
    audio_backend_lock();
    // One hover or click of each kind at a time; moving across controls cannot pile up.
    mixer_voice_stop(cnc_audio_mixer(g_au), c->voice);
    c->voice = mixer_play_stereo(cnc_audio_mixer(g_au), MIX_BUS_FX, (short*)c->pcm,
                               c->bytes / 4, MIX_UNITY, 0, 255, 0);
    if (c->voice >= 0) { ++g_ambHudPlays[click ? 1 : 0]; g_ambHudLastClip = clip; }
    audio_backend_unlock();
}

/* Uses the renderer's exact CPU wet/river classification, cached when it builds its
   water field. It remains available even when the water shader's effect is disabled. */
struct AmbienceLevels {
    float zoom, ocean, river, gains[7];
    int riverPan;
};

static AmbienceLevels ambience_levels(int fbw, int fbh)
{
    AmbienceLevels levels = {};
    levels.zoom = g_camMode == CAM_N64
        ? ambience_zoom_fraction(g_dist, dist_min_now(), DIST_MAX_OURS)
        : ambience_zoom_fraction(g_zoom, zoom_max_now(), ZOOM_MIN);
    bool hasWater = false;
    for (size_t i = 0; i < water_cells().size(); ++i)
        if (water_cells()[i].holes) { hasWater = true; break; }
    if (hasWater && water_field_sync() && !g_waterAudioClass.empty()) {
        const int w = g_waterFieldW, h = g_waterFieldH, res = g_waterFieldRes;
        int ocean = 0;
        // Uniform screen samples measure screen coverage, not a world-space bounding box.
        for (int row = 0; row < 14; ++row) for (int col = 0; col < 24; ++col) {
            float x, z;
            if (!screen_to_world((col + 0.5f) * fbw / 24, (row + 0.5f) * fbh / 14,
                                 fbw, fbh, &x, &z)) continue;
            int ix = (int)floorf(x * res), iz = (int)floorf(z * res);
            if (ix >= 0 && iz >= 0 && ix < w && iz < h &&
                g_waterAudioClass[(size_t)iz * w + ix] == 1) ++ocean;
        }
        float seaD = 1e6f, riverD = 1e6f, rx = g_camX, rz = g_camZ;
        for (size_t i = 0; i < g_waterAudioSources.size(); ++i) {
            const WaterAudioSource& s = g_waterAudioSources[i];
            const float dx = s.x - g_camX, dz = s.z - g_camZ;
            const float d = sqrtf(dx * dx + dz * dz);
            if (s.river) {
                if (d < riverD) { riverD = d; rx = s.x; rz = s.z; }
            } else if (d < seaD) seaD = d;
        }
        // A quiet approach begins near shore; coverage takes over as the camera crosses it.
        levels.ocean = fmaxf(ocean / (24.0f * 14.0f), 0.20f * ambience_attenuation(seaD));
        levels.river = ambience_attenuation(riverD) * ambience_river_mix(levels.zoom);
        const float yaw = g_camMode == CAM_N64 ? g_camYaw : YAW_DEG * (float)M_PI / 180;
        const float side = (rx - g_camX) * cosf(yaw) + (rz - g_camZ) * sinf(yaw);
        levels.riverPan = (int)(fmaxf(-0.75f, fminf(0.75f, side / 8)) * 1000);
    }
    // Near and far are two unrelated recordings, so the zoom blend between them is
    // equal-power: a straight linear blend sags about 3 dB at mid zoom.
    const float nearMix = ambience_near_mix(levels.zoom);
    const float nearG = sinf(nearMix * (float)M_PI * 0.5f), farG = cosf(nearMix * (float)M_PI * 0.5f);
    bool desert = !strcmp(g_pack.theater, "DESERT");
    bool temperate = !strcmp(g_pack.theater, "TEMPERAT") || !strcmp(g_pack.theater, "RATEMP");
    levels.gains[0] = desert ? nearG * (1 - levels.ocean) : 0;
    levels.gains[1] = desert ? farG * (1 - levels.ocean) : 0;
    levels.gains[2] = temperate ? nearG * (1 - levels.ocean) : 0;
    levels.gains[3] = temperate ? farG * (1 - levels.ocean) : 0;
    levels.gains[4] = levels.river;
    levels.gains[5] = nearG * levels.ocean;
    levels.gains[6] = farG * levels.ocean;
    return levels;
}

static void ambience_prepare_loading(int fbw, int fbh)
{
    if (!g_au || !g_fx.enabled || g_editOn || !mesh_perf_switch("OPENCNC3D_AMBIENCE_PREPARE")) return;
    GLint active = GL_TEXTURE0, binding = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
    const Uint64 started = SDL_GetPerformanceCounter();
    Uint64 fieldReady = started;
    int pendingClip = -1, preparedClips = 0;
    bool complete = false;
    try {
        const AmbienceLevels levels = ambience_levels(fbw, fbh);
        fieldReady = SDL_GetPerformanceCounter();
        for (int clipIndex = 0; clipIndex < 7; clipIndex++) {
            if (levels.gains[clipIndex] <= 0 || g_ambClips[clipIndex].tried) continue;
            pendingClip = clipIndex;
            if (ambience_clip(clipIndex)) preparedClips++;
            pendingClip = -1;
        }
        complete = true;
    } catch (const std::bad_alloc&) {
        if (pendingClip >= 0) {
            SDL_FreeWAV(g_ambClips[pendingClip].pcm);
            g_ambClips[pendingClip] = AmbienceClip();
        } else {
            water_field_free();
            g_waterFieldNone.valid = false;
        }
    }
    glBindTexture(GL_TEXTURE_2D, (GLuint)binding);
    glActiveTexture((GLenum)active);
    const Uint64 finished = SDL_GetPerformanceCounter();
    const double milliseconds = 1000.0 / SDL_GetPerformanceFrequency();
    fprintf(stderr, "AMBIENCEPREPARE|ready=%d|field_ms=%.3f|clips_ms=%.3f|clips=%d|voices_started=0\n",
            complete ? 1 : 0, (fieldReady - started) * milliseconds,
            (finished - fieldReady) * milliseconds, preparedClips);
}

static void ambience_update(int fbw, int fbh)
{
    if (!g_au || !g_fx.enabled || g_editOn) {
        ambience_stop(); g_ambOcean = g_ambRiver = 0; return;
    }
    const AmbienceLevels levels = ambience_levels(fbw, fbh);
    g_ambZoom = levels.zoom;
    g_ambOcean = levels.ocean;
    g_ambRiver = levels.river;
    for (int clipIndex = 0; clipIndex < 7; clipIndex++)
        ambience_bed(clipIndex, levels.gains[clipIndex], clipIndex == 4 ? levels.riverPan : 0);
}
#endif
