/*
 * playtest.c -- the only program in this directory that opens a sound device.
 *
 * It exists to prove the platform seam works, not to prove the mixer works: the
 * mixer is proved headlessly by banktest and mixtest, which measure samples. This
 * one just shows that the same engine, relinked against audio_sdl.c instead of
 * audio_null.c, feeds a real device without a single change to the core.
 *
 *   ./playtest DOSDATA_DIR                     AOI, plus a tank shot every second
 *   ./playtest DOSDATA_DIR --theme FWP         a different track
 *   ./playtest DOSDATA_DIR --seconds 20
 */

#include "cncaudio.h"
#include "sfxtable.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    char err[256];
    CncAudio *au;
    const char *dir = argc > 1 ? argv[1] : "data/dosdata";
    const char *theme = "AOI";
    int seconds = 12;
    int i, tank = -1, tick = 0;

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--theme") == 0 && i + 1 < argc)
            theme = argv[++i];
        else if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc)
            seconds = atoi(argv[++i]);
    }

    if (SDL_Init(0) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    au = cnc_audio_create(dir, err, (int)sizeof err);
    if (!au) {
        fprintf(stderr, "playtest: %s\n", err);
        return 1;
    }
    if (!audio_backend_open(au, err, (int)sizeof err)) {
        fprintf(stderr, "playtest: %s\n", err);
        return 1;
    }
    printf("backend: %s at %d Hz stereo\n", audio_backend_name(), MIX_RATE);

    for (i = 0; i < SFX_VOC_COUNT; i++)
        if (strcmp(sfx_voc[i].name, "TNKFIRE3") == 0)
            tank = i;

    cnc_audio_set_listener(au, 2000, 2000, 640, 400);
    if (!cnc_music_play_theme(au, theme, 1))
        printf("no such theme: %s\n", theme);
    else
        printf("music: %s\n", cnc_music_current(au));
    cnc_audio_update(au);

    /* The game loop, in miniature: pump the engine, fire the odd event, sleep. */
    for (tick = 0; tick < seconds * 20; tick++) {
        cnc_audio_update(au);
        if (tank >= 0 && tick % 20 == 10) {
            int x = 2000 + ((tick / 20) % 5 - 2) * 700; /* sweeps left to right */
            cnc_audio_on_sound_effect(au, tank, 0, x, 2000);
            printf("  t=%4.1fs  shot at world x %5d   voices %d  music ring %d\n", tick / 20.0, x,
                   mixer_active_voices(cnc_audio_mixer(au)),
                   mixer_music_level(cnc_audio_mixer(au)));
            fflush(stdout);
        }
        SDL_Delay(50);
    }

    cnc_music_fade_out(au, 800);
    for (tick = 0; tick < 20; tick++) {
        cnc_audio_update(au);
        SDL_Delay(50);
    }

    audio_backend_close();
    cnc_audio_destroy(au);
    SDL_Quit();
    return 0;
}
