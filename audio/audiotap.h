/*
 * audiotap.h -- record the mix to a WAV instead of sending it to a device.
 *
 * The whole audio path is only believable if a headless run can produce the exact
 * stereo stream a player would have heard and then be measured. That is what this
 * is: a tap that pulls from the SAME mixer the device pulls from, through the same
 * mixer_render(), and writes it to a RIFF file.
 *
 * It is not a backend. No device is open while it runs, so mixer_render() has one
 * caller and the recording is the mix, not a copy of it. Nothing here touches SDL,
 * Windows or any other platform API, so it builds on the Win98 tier too.
 *
 *     tap_open("mix.wav");
 *     ... every game tick:  tap_pump(au, 1000 / 15);
 *     tap_close();
 *
 * tap_pump renders `ms` worth of audio at MIX_RATE. Because the sim's clock and the
 * tap's clock are the same number of milliseconds, a scripted run of N ticks always
 * produces exactly the same file: the recording is deterministic, which is what makes
 * it usable as a gate.
 */

#ifndef AUDIOTAP_H
#define AUDIOTAP_H

#include "cncaudio.h"

#ifdef __cplusplus
extern "C" {
#endif

int  tap_open(const char *path);            /* 1 ok, 0 could not create the file  */
int  tap_active(void);
void tap_pump(CncAudio *au, int ms);        /* render ms of the mix into the file */
void tap_close(void);                       /* patch the RIFF sizes, print a line */

/* What has been written so far, for a harness that wants to assert on it. */
long tap_samples(void);                     /* stereo frames                      */
int  tap_peak(void);
double tap_rms(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIOTAP_H */
