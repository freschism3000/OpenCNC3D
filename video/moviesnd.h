/* ====================================================================================
 * moviesnd.h -- a movie's soundtrack, plugged into the one audio engine.
 *
 * movieplay.c knows nothing about the mixer: it asks a MOV_Audio for four things
 * (open, push, played, pump) and pushes decoded PCM at it. This is the object that
 * answers those four questions using audio/cncaudio.h, and it is the whole coupling
 * between the movie player and the sound engine.
 *
 * There is no device in here. The program opened exactly one, before the menu, and
 * the movie's audio goes onto the MOVIE bus of the same mixer as everything else.
 * That is what lets the score duck under a movie and come back afterwards instead of
 * being cut off by a second device grabbing the card.
 *
 * `played` is the bit that makes the picture line up with the sound: it is everything
 * pushed, minus what is still queued in the mixer ring, minus what the device has
 * taken but not yet made audible. Without that last term the picture runs one device
 * buffer ahead of the sound for the whole movie.
 * ==================================================================================== */

#ifndef MOVIESND_H
#define MOVIESND_H

#include "movieplay.h"

#ifdef __cplusplus
extern "C" {
#endif

struct CncAudio;

typedef struct MOV_Sink {
    struct CncAudio *au;
    long pushed;         /* samples handed to the mixer                          */
    long dropped;        /* samples the ring refused: the video is ahead         */
    int  duck_ms;        /* music fade under the movie, default 250 (theme.cpp)  */
    int  restore_music;  /* 1: ramp the score back up on close                   */
    int  pump_ms;        /* how much sound one pump is worth, default 16         */
} MOV_Sink;

void movsnd_init(MOV_Sink *s, struct CncAudio *au);
void movsnd_bind(MOV_Sink *s, MOV_Audio *a);

#ifdef __cplusplus
}
#endif

#endif /* MOVIESND_H */
