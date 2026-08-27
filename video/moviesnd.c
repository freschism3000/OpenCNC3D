#include "moviesnd.h"
#include "cncaudio.h"
#include "audioboot.h"
#include "audiotap.h"

#include <stddef.h>
#include <string.h>

/* One movie's audio, as movieplay.c sees it. Everything here is in SAMPLES until the
   last moment, because the mixer counts samples and the player counts bytes. */

static void ms_open(void *user, int rate, int channels)
{
    MOV_Sink *s = (MOV_Sink *)user;
    (void)rate;      /* everything on the 1995 discs is 22050 Hz mono, and vqaplay   */
    (void)channels;  /* has already refused anything else by the time we get here     */
    s->pushed = 0;
    s->dropped = 0;
    cnc_movie_reset(s->au);
    /* theme.cpp ThemeClass::Fade_Out before a movie: the score goes down over a
       quarter second rather than being cut, and the movie bus comes up to unity. */
    if (s->au) {
        Mixer *m = cnc_audio_mixer(s->au);
        mixer_bus_gain(m, MIX_BUS_MUSIC, 0, s->duck_ms);
        mixer_bus_gain(m, MIX_BUS_MOVIE, MIX_UNITY, 0);
    }
}

static void ms_push(void *user, const void *pcm, int bytes)
{
    MOV_Sink *s = (MOV_Sink *)user;
    int samples = bytes / 2;
    int took = cnc_movie_push(s->au, (const short *)pcm, samples);
    s->pushed += took;
    if (took < samples)
        s->dropped += samples - took;
}

/* Bytes of everything push() was given that have physically come out of the speakers.
   Three terms, and leaving any of them out shows up as the picture drifting:

     pushed            what the decoder has produced
   - ring level        decoded but still queued in the mixer
   - device latency    rendered by the mixer but still sitting in the sound card's
                       buffer. On a callback backend that is one buffer, 46 ms at the
                       1024 frames SDL is asked for, which is most of a movie frame. */
static long ms_played(void *user)
{
    MOV_Sink *s = (MOV_Sink *)user;
    long heard;

    /* -1 means "I cannot answer", and movieplay.c then paces off the wall clock.
     *
     * THIS IS NOT A CORNER CASE, it is the machine with no sound card and it is also
     * every CI run. Nothing drains the movie ring when there is no device and no
     * recording, so `pushed - pending` stays at zero forever, the audio clock never
     * advances, and the player sits on frame 0 until someone kills it. That is a
     * hang, not a silent movie, and it is exactly what happened the first time this
     * was wired up. */
    if (!s->au || !(audio_boot_have_device() || tap_active()))
        return -1;

    heard = s->pushed - (long)cnc_movie_pending(s->au) - (long)audio_backend_latency();
    if (heard < 0)
        heard = 0;
    return heard * 2; /* samples -> bytes, 16 bit mono */
}

static void ms_pump(void *user)
{
    MOV_Sink *s = (MOV_Sink *)user;
    audio_frame(s->au, s->pump_ms);
}

/* Nothing of this movie left in OUR queue (the mixer's movie ring). What has already
   been rendered into the device buffer does not need waiting for: ms_close never
   touches the device, and the menu theme that follows is mixed BEHIND the tail in the
   same FIFO. With no device and no tap the ring never drains, and saying "drained"
   there is correct: there is nothing to lose by closing. */
static int ms_drained(void *user)
{
    MOV_Sink *s = (MOV_Sink *)user;
    if (!s->au || !(audio_boot_have_device() || tap_active()))
        return 1;
    return cnc_movie_pending(s->au) == 0;
}

static void ms_close(void *user)
{
    MOV_Sink *s = (MOV_Sink *)user;
    if (!s->au)
        return;
    cnc_movie_reset(s->au);
    mixer_bus_gain(cnc_audio_mixer(s->au), MIX_BUS_MOVIE, 0, 0);
    if (s->restore_music) {
        /* Back to the SLIDER, not to unity. cnc_audio_set_music_volume is the one
           thing that owns this bus (Options.ScoreVolume, 0..255), and theme.cpp
           brings the score back at that volume after a movie, not at full. Restoring
           to MIX_UNITY here would hand music back to a player who had turned it off,
           and would also fight the Options dialog's music slider. Reading the bus
           back instead of the slider does not work: campaign movies come in chains,
           and the second one would open while the first one's restore ramp was still
           climbing and would save a gain of nearly zero. */
        int v = cnc_audio_get_music_volume(s->au);
        mixer_bus_gain(cnc_audio_mixer(s->au), MIX_BUS_MUSIC,
                       v * MIX_UNITY / 255, s->duck_ms);
    }
}

void movsnd_init(MOV_Sink *s, struct CncAudio *au)
{
    memset(s, 0, sizeof *s);
    s->au = au;
    s->duck_ms = 250;  /* defines.h FADE_PALETTE_MEDIUM, TIMER_SECOND/4 */
    s->restore_music = 1;
    s->pump_ms = 16;
}

void movsnd_bind(MOV_Sink *s, MOV_Audio *a)
{
    memset(a, 0, sizeof *a);
    a->user = s;
    a->open = ms_open;
    a->push = ms_push;
    a->played = ms_played;
    a->pump = ms_pump;
    a->close = ms_close;
    a->drained = ms_drained;
}
