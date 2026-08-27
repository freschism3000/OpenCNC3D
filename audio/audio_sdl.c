/*
 * audio_sdl.c -- the SDL2 backend. The ONLY file in the engine that includes SDL.
 *
 * Everything platform specific lives here: opening the device, the callback thread,
 * and the lock that keeps a control call from landing halfway through a render.
 * A Windows 98 backend is a new file with the same five functions: open a
 * DirectSound secondary buffer or a waveOut double buffer, call mixer_render() from
 * the refill point, and make lock/unlock a critical section (or nothing at all, if
 * the refill happens on the main loop, which is the sane Win98 shape).
 *
 * Note the request: 22050 Hz, signed 16 bit, 2 channels, and SDL_AUDIO_ALLOW no
 * changes. The mixer produces exactly that and we refuse to let SDL resample behind
 * our back, because a silent conversion is precisely the kind of thing that hides a
 * decoder bug.
 */

#include "cncaudio.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

static SDL_AudioDeviceID g_dev = 0;
static Mixer *g_mix = NULL;
static int g_latency = 0; /* frames the device has taken but not yet made audible */

static void sdl_audio_cb(void *userdata, Uint8 *stream, int len)
{
    Mixer *m = (Mixer *)userdata;
    int frames = len / 4; /* stereo, 2 bytes per sample */
    if (!m) {
        memset(stream, 0, (size_t)len);
        return;
    }
    mixer_render(m, (short *)stream, frames);
}

int audio_backend_open(CncAudio *au, char *err, int errlen)
{
    SDL_AudioSpec want, have;

    if (!au) {
        snprintf(err, (size_t)errlen, "no audio engine");
        return 0;
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        snprintf(err, (size_t)errlen, "SDL_InitSubSystem(AUDIO): %s", SDL_GetError());
        return 0;
    }

    g_mix = cnc_audio_mixer(au);

    SDL_zero(want);
    want.freq = MIX_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024; /* 46 ms, comfortably above the 11.6 ms gain block */
    want.callback = sdl_audio_cb;
    want.userdata = g_mix;

    g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (g_dev == 0) {
        snprintf(err, (size_t)errlen, "SDL_OpenAudioDevice: %s", SDL_GetError());
        return 0;
    }
    if (have.freq != MIX_RATE || have.channels != 2 || have.format != AUDIO_S16SYS) {
        snprintf(err, (size_t)errlen, "device gave %d Hz %d ch fmt 0x%04X, wanted %d Hz 2 ch s16",
                 have.freq, have.channels, (unsigned)have.format, MIX_RATE);
        SDL_CloseAudioDevice(g_dev);
        g_dev = 0;
        return 0;
    }

    /* One callback buffer of sound is always ahead of the speaker. The movie player
       subtracts this from what it has pushed, or the picture runs 46 ms early for the
       whole film. */
    g_latency = have.samples > 0 ? have.samples : 1024;

    SDL_PauseAudioDevice(g_dev, 0);
    err[0] = 0;
    return 1;
}

void audio_backend_close(void)
{
    if (g_dev) {
        SDL_CloseAudioDevice(g_dev);
        g_dev = 0;
    }
    g_mix = NULL;
    g_latency = 0;
}

/* Zero when no device is open, which is the right answer for a recording run: the tap
 * renders and writes in the same call, so nothing is in flight. */
int audio_backend_latency(void) { return g_dev ? g_latency : 0; }

void audio_backend_lock(void)
{
    if (g_dev)
        SDL_LockAudioDevice(g_dev);
}

void audio_backend_unlock(void)
{
    if (g_dev)
        SDL_UnlockAudioDevice(g_dev);
}

const char *audio_backend_name(void) { return "SDL2"; }
