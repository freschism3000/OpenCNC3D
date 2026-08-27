/*
 * audioboot.h -- bringing the audio engine up, once, for whichever program is
 * running: the merged app (app/cnc3d.cpp), the standalone renderer (cnc_eyes) or
 * the menu preview. All three want the same thing and none of them should have its
 * own opinion about it, because "the menu opened a second device" is exactly the
 * kind of bug that only shows up on someone else's sound card.
 *
 * ONE engine, ONE device, for the whole process. The menu, the tactical view and
 * the movie player all push into the same mixer; nothing else opens a device.
 *
 * Three modes, and the choice is made here rather than by a backend swap, because
 * the engine links exactly one backend object (audio_sdl.c):
 *
 *   device    the normal case: SDL2 opens the sound card and pulls from the mixer.
 *   wav       --audiowav FILE: no device at all. The game thread renders the mix
 *             into a RIFF file through audiotap.c, so a headless script run
 *             produces the exact stream a player would have heard, for measuring.
 *   silent    --nosound, or a machine with no sound card. The bank is still opened
 *             and the callbacks still resolve names, so a missing sound is still
 *             reported; nothing is rendered.
 *
 * A failure to open the device is NEVER fatal. CI has no sound card and the gates
 * must not care.
 */

#ifndef AUDIOBOOT_H
#define AUDIOBOOT_H

#include "cncaudio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AudioBootOpts
{
    const char *dosdata; /* directory holding SOUNDS.MIX etc. NULL -> "dosdata" */
    const char *wav;     /* record the mix here instead of opening a device      */
    int silent;          /* 1: no device, no recording                           */
    /* The 1995 Game Controls sliders, 0..255. ZERO IS A REAL VALUE and means muted,
       so the "not specified" case is -1, not 0: treating 0 as unset is exactly how a
       mute silently stops working. */
    int music_vol255;
    int sound_vol255;
} AudioBootOpts;

/* Returns NULL only if the bank itself could not be created (out of memory). A
 * missing archive, a missing sound card and a refused device all return a usable
 * engine that simply makes no noise. */
CncAudio *audio_boot(const AudioBootOpts *o);
void audio_boot_shutdown(CncAudio *au);

/* Did a real device open. The app uses this to decide whether to pace itself on the
 * sound clock. */
int audio_boot_have_device(void);

/* ONE frame's worth of sound work, from whichever loop is running: the menu, the
 * tactical view or a scripted run. It refills the music ring and retires finished
 * voices, and when the mix is being recorded instead of played it also renders `ms`
 * of it into the WAV. Every loop in the program calls this and none of them has to
 * know which of the two is happening. Game thread only: it does file I/O. */
void audio_frame(CncAudio *au, int ms);

#ifdef __cplusplus
}
#endif

#endif /* AUDIOBOOT_H */
