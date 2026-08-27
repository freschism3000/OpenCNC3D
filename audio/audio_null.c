/*
 * audio_null.c -- the no device backend. Link this instead of audio_sdl.c and the
 * whole engine still runs; nothing pulls PCM, so the harness pulls it by hand with
 * mixer_render() and writes a WAV.
 *
 * It also demonstrates how small the seam is: five functions and no state.
 */

#include "cncaudio.h"

int audio_backend_open(CncAudio *au, char *err, int errlen)
{
    (void)au;
    (void)err;
    (void)errlen;
    return 1;
}

void audio_backend_close(void) {}
void audio_backend_lock(void) {}
void audio_backend_unlock(void) {}
const char *audio_backend_name(void) { return "null"; }
int audio_backend_latency(void) { return 0; }
