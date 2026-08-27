#include "audioboot.h"
#include "audiotap.h"
#include "sfxtable.h"

#include <stdio.h>
#include <string.h>

static int g_have_device = 0;

int audio_boot_have_device(void) { return g_have_device; }

/* Where the 1995 archives are. The same idiom cnc_eyes.cpp uses for the brain: try
 * what was asked for, then the two or three places it is actually likely to be, and
 * SAY which one won. A play folder has dosdata/ beside the binary; a working tree has
 * data/dosdata a couple of levels up. Getting this wrong is silent by nature, which is
 * why it prints rather than guesses quietly.
 *
 * The guess list applies ONLY when nobody asked for anything. An EXPLICIT --dosdata
 * that has no SOUNDS.MIX is an error stated out loud, never papered over: falling back
 * from it made `--dosdata /nonexistent` boot a fully working bank from the working
 * directory, so the test that "verified the NULL guards" never tested them at all. */
static const char *find_dosdata(const char *want, int is_explicit)
{
    static const char *const places[] = {
        NULL, "dosdata", "../dosdata", "data/dosdata", "../data/dosdata",
        "../../data/dosdata"
    };
    static char chosen[600];
    char probe[600];
    unsigned i, n;
    FILE *f;

    n = is_explicit ? 1 : (unsigned)(sizeof places / sizeof places[0]);
    for (i = 0; i < n; i++) {
        const char *d = (i == 0) ? want : places[i];
        if (!d || !d[0])
            continue;
        snprintf(probe, sizeof probe, "%s/SOUNDS.MIX", d);
        f = fopen(probe, "rb");
        if (f) {
            fclose(f);
            snprintf(chosen, sizeof chosen, "%s", d);
            if (i != 0)
                fprintf(stderr, "audio: no SOUNDS.MIX in '%s'; using '%s'\n", want, d);
            return chosen;
        }
    }
    if (is_explicit)
        fprintf(stderr, "audio: no SOUNDS.MIX in '%s' and --dosdata was explicit: "
                        "NOT falling back anywhere. The game runs silent.\n", want);
    /* Nothing found. Return what was asked for so the miss log names it. */
    snprintf(chosen, sizeof chosen, "%s", want ? want : "dosdata");
    return chosen;
}

/* How much of the disc actually turned up. Printed once, because "the game is silent"
 * and "the game cannot find SOUNDS.MIX" look identical from the speakers. */
static void report_bank(CncAudio *au)
{
    SndBank *b = cnc_audio_bank(au);
    int i, have_sfx = 0, have_vox = 0, have_theme = 0;
    char name[64];

    for (i = 0; i < SFX_VOC_COUNT; i++) {
        sfx_voc_filename(i, 1, 0, name, (int)sizeof name);
        if (name[0] && bank_has(b, name))
            have_sfx++;
    }
    for (i = 0; i < SFX_VOX_COUNT; i++) {
        snprintf(name, sizeof name, "%s.AUD", sfx_vox[i]);
        if (bank_has(b, name))
            have_vox++;
    }
    for (i = 0; i < SFX_THEME_COUNT; i++) {
        snprintf(name, sizeof name, "%s.AUD", sfx_theme[i].name);
        if (bank_has(b, name))
            have_theme++;
        else {
            snprintf(name, sizeof name, "%s.VAR", sfx_theme[i].name);
            if (bank_has(b, name))
                have_theme++;
        }
    }
    printf("AUDIO|bank|sfx=%d/%d|speech=%d/%d|themes=%d/%d\n", have_sfx, SFX_VOC_COUNT,
           have_vox, SFX_VOX_COUNT, have_theme, SFX_THEME_COUNT);
    if (have_sfx == 0)
        fprintf(stderr, "audio: NO sound archives found. The game will be silent; "
                        "point --dosdata at the folder holding SOUNDS.MIX.\n");
    fflush(stdout);
}

CncAudio *audio_boot(const AudioBootOpts *o)
{
    static AudioBootOpts defaults;
    CncAudio *au;
    char err[256];
    char path[600];
    const char *dir;

    if (!o) {
        memset(&defaults, 0, sizeof defaults);
        o = &defaults;
    }
    {
        const int is_explicit = (o->dosdata && o->dosdata[0]);
        dir = find_dosdata(is_explicit ? o->dosdata : "dosdata", is_explicit);
    }

    au = cnc_audio_create(dir, err, sizeof err);
    if (!au) {
        fprintf(stderr, "audio: %s (the game will be silent)\n", err);
        return NULL;
    }

    /* TRANSIT.MIX is not next to the others: it comes off CD-1's INSTALL folder and
       carries MAP1 (the menu theme) and WIN1. cnc_audio_create looks for it beside
       SOUNDS.MIX; our layout keeps it in its own subfolder, so it is added here. */
    snprintf(path, sizeof path, "%s/transit/TRANSIT.MIX", dir);
    bank_add_mix(cnc_audio_bank(au), path, err, sizeof err);

    cnc_audio_set_music_volume(au, o->music_vol255 >= 0 ? o->music_vol255 : 255);
    cnc_audio_set_sound_volume(au, o->sound_vol255 >= 0 ? o->sound_vol255 : 255);

    report_bank(au);

    g_have_device = 0;
    if (o->wav && o->wav[0]) {
        tap_open(o->wav);
        printf("AUDIO|out|wav %s\n", o->wav);
    } else if (o->silent) {
        /* Silent is a normal outcome, not a failure: --nosound asked for it, or this
           is a measuring run (--shot, --picktest, a --script without --audiowav) that
           has no clock to play sound against. */
        printf("AUDIO|out|silent\n");
    } else if (audio_backend_open(au, err, sizeof err)) {
        g_have_device = 1;
        printf("AUDIO|out|device %s\n", audio_backend_name());
    } else {
        /* Not fatal, on purpose: CI has no sound card and the gates must not care. */
        fprintf(stderr, "audio: no device (%s); the game runs silent\n", err);
        printf("AUDIO|out|none (%s)\n", err);
    }
    fflush(stdout);
    return au;
}

void audio_frame(CncAudio *au, int ms)
{
    if (!au)
        return;
    if (tap_active())
        tap_pump(au, ms); /* tap_pump calls cnc_audio_update itself */
    else
        cnc_audio_update(au);
}

void audio_boot_shutdown(CncAudio *au)
{
    SndBank *b;
    int n, i;

    if (!au)
        return;
    if (tap_active())
        tap_close();
    if (g_have_device) {
        /* How much the sound card actually pulled. This is the difference between
           "a device opened" and "the callback ran and the mixer was drained", and
           only the second one means anything came out of the speakers. */
        long f = mixer_frames_rendered(cnc_audio_mixer(au));
        printf("AUDIO|device|%s|pulled %ld frames (%.1f s) from the mixer\n",
               audio_backend_name(), f, (double)f / MIX_RATE);
        audio_backend_close();
    }
    g_have_device = 0;

    /* Clipping, as a number rather than an argument. knee = samples the soft clipper
       bent (gentle), hard = samples that hit the ceiling anyway (real overload). The
       run that matters is the one a player runs: both sliders up. */
    {
        long f = mixer_frames_rendered(cnc_audio_mixer(au));
        long knee = 0, hard = 0;
        mixer_clip_stats(cnc_audio_mixer(au), &knee, &hard);
        if (f > 0)
            printf("AUDIO|clip|knee=%ld|hard=%ld|of %ld samples (%.3f%% bent, %.3f%% hard)\n",
                   knee, hard, f * 2, 100.0 * knee / (f * 2), 100.0 * hard / (f * 2));
    }

    /* a project rule made visible: anything the game asked for and the disc did not have
       is named here, never substituted and never quietly dropped. */
    b = cnc_audio_bank(au);
    n = bank_miss_count(b);
    for (i = 0; i < n; i++)
        printf("AUDIO|missing|%s\n", bank_miss(b, i));
    if (n)
        printf("AUDIO|missing|%d name(s) asked for and not on the disc\n", n);
    fflush(stdout);

    cnc_audio_destroy(au);
}
