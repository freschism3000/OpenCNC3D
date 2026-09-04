#include "cncaudio.h"
#include "sfxtable.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MUSIC_CHUNK 4096 /* samples decoded per top-up */

/* THE MOST TOP-UPS ANY ONE CALL MAY DO, so that starting a track costs an ordinary frame.
 *
 * Music is streamed, not loaded: the ring holds 65536 samples and an ordinary frame decodes
 * either nothing or one chunk, because the device drains only 22050 samples a second. But
 * starting a track EMPTIES the ring, and the top-up loop below used to refill the whole
 * thing before returning: 15 iterations, 61440 samples, 2.79 seconds of audio decoded inside
 * one frame, on the game thread, immediately before that frame is drawn. That is about 167
 * times the average frame's music work and it lands on exactly the frame a song changes,
 * which is the frame a player reported the game hitching on.
 *
 * Two rather than one, because the ring has to gain more than the device drains between two
 * consecutive calls. Two chunks is 8192 samples, 371 ms of audio, against 22050 samples a
 * second: 5.6x headroom at the 15 Hz brain tick the interactive loop is built around, and
 * still 1.9x at 5 fps. The ring fills over 8 calls, about 130 ms at 60 fps, and never holds
 * less than 371 ms while it does. The worst frame's music work goes from 15 chunks to 2,
 * against the 1 a busy ordinary frame already did.
 *
 * This is a Tier 1 fix as much as a Tier 2 one. The Win98 backend holds a shorter queue than
 * this ring and runs on a far slower machine, so a burst that measures a fraction of a
 * millisecond on a modern desktop is not a fraction of a millisecond there.
 *
 * Deliberately NOT a worker thread, and deliberately not decoding in the audio callback: the
 * callback may only signal, and keeping every decode on the game thread is the whole reason
 * stdio never has to be interrupt safe here. The ring already IS the read-ahead. Only the
 * priming of it was wrong. */
#define MUSIC_TOPUPS 2

/* HOW MANY COPIES OF ONE CLIP MAY START IN ONE ENGINE TICK.
 *
 * Every effect an advance raises lands in the mixer before the next block is rendered, so
 * N copies of one clip started in one tick begin at the SAME sample of that clip. They are
 * then bit-identical waveforms in lockstep and they sum COHERENTLY: N times the amplitude
 * of one, which is what turns a squad caught by a flame tank into distortion rather than
 * into a loud squad. Copies started one tick apart are 1470 samples out of step (22050 Hz
 * against the 15 Hz brain tick) and do not do this, which is why the window is exactly one
 * tick wide and no wider.
 *
 * Two, not one. The first copy is the event; the second is the audible difference between
 * one thing happening and more than one, and two coherent copies are 6 dB, which the
 * output stage's knee is there for. A third adds 3.5 dB of exactly the same waveform and
 * no information at all.
 *
 * This is the one number to sweep if the rule proves too tight or too loose, and it is
 * deliberately a count of STARTS rather than of ringing voices: see cnc_audio_begin_tick
 * in the header for why a voice count refuses ordinary combat and answers differently
 * depending on whether anything is rendering the mix. */
#define SFX_DUPES_PER_TICK 2

/* Distinct clip names tracked inside one tick. A tick that raises more different sounds
 * than this lets the surplus through uncapped, which is the safe direction: a full table
 * behaves like the code did before this rule existed, never like silence. */
#define SFX_DUPES_TRACKED 16

struct CncAudio
{
    SndBank *bank;
    Mixer *mix;

    AudStream *music;
    int music_loop;
    char music_name[32];

    int speech_voice;
    int speech_queued;  /* SpeakQueue: one deep, -1 = empty */
    int speech_current; /* CurrentVoice: what EVA is saying now */
    char speech_pending[32];
    int speech_ducking;

    int listener_cx, listener_cy, view_w, view_h;
    int juvenile;
    int music_vol255, sound_vol255;

    /* What the placement rule decided for the most recent effect. Diagnostics only,
       so a headless run can PRINT the gain and pan instead of inferring them from the
       loudness of a whole recording. */
    int last_gain, last_pan;

    /* The score playlist: which ThemeType is playing, and whether the next one should
       start by itself when it ends. Off for the menu, which loops one track. */
    int theme_index;
    int playlist;
    /* SHUFFLE, which until now did not exist. Options.IsScoreShuffle in 1995
       (theme.cpp:222-252) picks the next track at random instead of walking the table;
       this engine had no random branch at all, so the two jukebox toggles overloaded
       `playlist`, the ADVANCE flag, and SHUFFLE OFF stopped the score dead instead of
       playing in order. Both are now what they say they are.
       shuffle_seed is a plain LCG and NOT the C library's rand(): two --shot runs must
       produce the same score, and five gates read the MUSIC|play lines. */
    int shuffle;
    unsigned int shuffle_seed;

    char pinned[MIX_VOICES][32];
    int pinned_handle[MIX_VOICES];

    /* The duplicate window, one engine tick wide. sfx_ticked stays 0 until a host calls
       cnc_audio_begin_tick, and while it is 0 nothing is capped: the mixer harnesses fire
       sixteen copies of one clip at once on purpose and have no tick to count against.
       calloc zeroes all four, which is the correct starting state for every one. */
    int sfx_ticked;
    int sfx_names;
    char sfx_name[SFX_DUPES_TRACKED][32];
    int sfx_count[SFX_DUPES_TRACKED];

    /* The folder cnc_audio_create was pointed at. Kept because LOCAL.MIX sits in it
       and carries CONQUER.ENG, which the campaign screens read their text out of. */
    char dosdata[512];

    short scratch[MUSIC_CHUNK];
};

static void try_add_mix(CncAudio *au, const char *dir, const char *file)
{
    char path[600];
    char err[256];
    snprintf(path, sizeof path, "%s/%s", dir, file);
    bank_add_mix(au->bank, path, err, (int)sizeof err);
    /* Not fatal. A build without SCORES.MIX simply has no music, and the miss log
     * says so rather than the engine guessing at a substitute. */
}

/* EVERY entry point below tolerates a NULL engine and does nothing. That is not
 * defensive habit, it is the contract: a machine with no dosdata folder gets NULL
 * back from cnc_audio_create, and the game must still boot, still run its gates and
 * still be playable in silence. Without this the first gunshot dereferences NULL. */
CncAudio *cnc_audio_create(const char *dosdata_dir, char *err, int errlen)
{
    CncAudio *au = (CncAudio *)calloc(1, sizeof(CncAudio));
    char path[600];
    int i;

    if (!au) {
        snprintf(err, (size_t)errlen, "out of memory");
        return NULL;
    }
    au->bank = bank_create(0);
    au->mix = mixer_create();
    if (!au->bank || !au->mix) {
        snprintf(err, (size_t)errlen, "out of memory");
        cnc_audio_destroy(au);
        return NULL;
    }
    au->speech_voice = -1;
    au->speech_queued = -1;
    au->speech_current = -1;
    au->theme_index = -1;
    au->music_vol255 = 255;
    au->sound_vol255 = 255;
    au->view_w = 640;
    au->view_h = 400;
    for (i = 0; i < MIX_VOICES; i++)
        au->pinned_handle[i] = -1;

    if (dosdata_dir) {
        strncpy(au->dosdata, dosdata_dir, sizeof au->dosdata - 1);
        au->dosdata[sizeof au->dosdata - 1] = 0;
    }

    /* Loose files win, so a test or a fix can drop a .AUD in without rebaking. */
    snprintf(path, sizeof path, "%s/music", dosdata_dir);
    bank_add_dir(au->bank, path);
    bank_add_dir(au->bank, dosdata_dir);

    /* Order matters: SOUNDS before ZOUNDS so the ordinary effects win and only the
     * .JUV alternates come out of ZOUNDS; AUD.MIX last because it overlaps SOUNDS. */
    try_add_mix(au, dosdata_dir, "SOUNDS.MIX");
    try_add_mix(au, dosdata_dir, "SPEECH.MIX");
    try_add_mix(au, dosdata_dir, "SCORES.MIX");
    try_add_mix(au, dosdata_dir, "ZOUNDS.MIX");
    try_add_mix(au, dosdata_dir, "AUD.MIX");
    try_add_mix(au, dosdata_dir, "TRANSIT.MIX"); /* MAP1 and WIN1 live here */

    err[0] = 0;
    return au;
}

void cnc_audio_destroy(CncAudio *au)
{
    if (!au)
        return;
    if (au->music)
        aud_close(au->music);
    if (au->mix)
        mixer_destroy(au->mix);
    if (au->bank)
        bank_destroy(au->bank);
    free(au);
}

Mixer *cnc_audio_mixer(CncAudio *au) { return au ? au->mix : NULL; }
SndBank *cnc_audio_bank(CncAudio *au) { return au ? au->bank : NULL; }

void cnc_audio_set_listener(CncAudio *au, int cx, int cy, int view_w, int view_h)
{
    if (!au)
        return;

    au->listener_cx = cx;
    au->listener_cy = cy;
    if (view_w > 0)
        au->view_w = view_w;
    if (view_h > 0)
        au->view_h = view_h;
}

void cnc_audio_set_juvenile(CncAudio *au, int on)
{
    if (!au)
        return;
    au->juvenile = on ? 1 : 0;
}

/* ------------------------------------------------------------- pinning */

/* A voice plays straight out of the bank's cache, so the clip must not be evicted
 * underneath it. Pin on start, unpin when the voice retires. */
static void pin_for_voice(CncAudio *au, int handle, const char *name)
{
    int slot = handle & 0xFF;
    if (handle < 0 || slot >= MIX_VOICES)
        return;
    if (au->pinned_handle[slot] >= 0 && au->pinned[slot][0])
        bank_pin(au->bank, au->pinned[slot], 0);
    strncpy(au->pinned[slot], name, sizeof au->pinned[0] - 1);
    au->pinned[slot][sizeof au->pinned[0] - 1] = 0;
    au->pinned_handle[slot] = handle;
    bank_pin(au->bank, name, 1);
}

static void retire_finished(CncAudio *au)
{
    int i;
    for (i = 0; i < MIX_VOICES; i++) {
        if (au->pinned_handle[i] >= 0 && !mixer_voice_active(au->mix, au->pinned_handle[i])) {
            bank_pin(au->bank, au->pinned[i], 0);
            au->pinned[i][0] = 0;
            au->pinned_handle[i] = -1;
        }
    }
}

/* ----------------------------------------------------------- named play */

int cnc_audio_play_named(CncAudio *au, const char *name, int bus, int gain, int pan, int priority)
{
    if (!au)
        return -1;

    const SndClip *c = bank_get(au->bank, name);
    int h;
    if (!c)
        return -1; /* not on the disc: silence, and the miss log has the name */
    audio_backend_lock();
    h = mixer_play(au->mix, bus, c->pcm, c->samples, gain, pan, priority);
    if (h >= 0)
        pin_for_voice(au, h, name);
    audio_backend_unlock();
    return h;
}

int cnc_audio_play_named_loop(CncAudio *au, const char *name, int bus, int gain, int pan, int priority)
{
    if (!au)
        return -1;

    {
        const SndClip *c = bank_get(au->bank, name);
        int h;
        if (!c)
            return -1; /* not on the disc: silence, and the miss log has the name */
        audio_backend_lock();
        h = mixer_play_loop(au->mix, bus, c->pcm, c->samples, gain, pan, priority);
        if (h >= 0)
            pin_for_voice(au, h, name);
        audio_backend_unlock();
        return h;
    }
}

const char *cnc_audio_dosdata(const CncAudio *au) { return au ? au->dosdata : ""; }

/* ------------------------------------------------------ the duplicate window */

void cnc_audio_begin_tick(CncAudio *au)
{
    if (!au)
        return;
    au->sfx_ticked = 1;
    au->sfx_names = 0;
}

/* Count one start against the current tick and say whether it may go ahead. Names are
   compared whole, so BLEEP2.AUD and BLEEP2.JUV are two clips, which is what they are on
   the disc, and a response that resolved to a different .V0x is its own clip too: a
   different waveform does not stack coherently with the others and must not be counted
   against them. */
static int sfx_dupe_ok(CncAudio *au, const char *name)
{
    int i;

    if (!au->sfx_ticked)
        return 1; /* no host tick: no window to count in, and nothing to cap */

    for (i = 0; i < au->sfx_names; i++) {
        if (strcmp(au->sfx_name[i], name) == 0) {
            if (au->sfx_count[i] >= SFX_DUPES_PER_TICK)
                return 0;
            au->sfx_count[i]++;
            return 1;
        }
    }
    if (au->sfx_names >= SFX_DUPES_TRACKED)
        return 1; /* table full: let it through rather than cap what was never counted */

    strncpy(au->sfx_name[au->sfx_names], name, sizeof au->sfx_name[0] - 1);
    au->sfx_name[au->sfx_names][sizeof au->sfx_name[0] - 1] = 0;
    au->sfx_count[au->sfx_names] = 1;
    au->sfx_names++;
    return 1;
}

int cnc_audio_on_sound_effect(CncAudio *au, int sfx_index, int variation, int x, int y)
{
    if (!au)
        return -1;

    char name[32];
    int gain = MIX_UNITY, pan = 0, pri = 20;

    if (sfx_index < 0 || sfx_index >= SFX_VOC_COUNT)
        return -1;
    sfx_voc_filename(sfx_index, variation, au->juvenile, name, (int)sizeof name);
    pri = sfx_voc[sfx_index].priority;

    if (!mixer_effect_place(au->listener_cx, au->listener_cy, au->view_w, au->view_h, x, y, &gain,
                            &pan))
        return -1;

    /* Kept so a headless run can PRINT what the placement rule decided, instead of
       leaving it to be inferred from the loudness of a whole recording. */
    au->last_gain = gain;
    au->last_pan = pan;

    /* A .JUV that is not on this disc falls back to the plain .AUD, which is what
     * the 1995 loader did when Special.IsJuvenile was on but the pack was absent. */
    if (!bank_has(au->bank, name) && sfx_voc[sfx_index].where == SFX_JUV) {
        snprintf(name, sizeof name, "%s.AUD", sfx_voc[sfx_index].name);
    }

    /* The duplicate window is applied LAST: after the placement test, so a sound the
     * listener cannot hear never spends a slot the audible copy needs, and after the .JUV
     * fallback, so the name counted is the file that would really be played.
     *
     * A name the disc does not have is never refused here. It has to reach the bank so the
     * miss log records it and so --dumpsound still says SILENT for it: a duplicate is a
     * rule working, a missing file is a fact about the disc, and the two must not be
     * spelled the same way in a log that two gates read. */
    if (bank_has(au->bank, name) && !sfx_dupe_ok(au, name))
        return CNC_SFX_DUPLICATE;

    return cnc_audio_play_named(au, name, MIX_BUS_FX, gain, pan, pri);
}

void cnc_audio_last_effect(const CncAudio *au, int *gain, int *pan)
{
    if (gain) *gain = au ? au->last_gain : 0;
    if (pan) *pan = au ? au->last_pan : 0;
}

void cnc_audio_listener(const CncAudio *au, int *cx, int *cy, int *vw, int *vh)
{
    if (cx) *cx = au ? au->listener_cx : 0;
    if (cy) *cy = au ? au->listener_cy : 0;
    if (vw) *vw = au ? au->view_w : 0;
    if (vh) *vh = au ? au->view_h : 0;
}

/* EVA speech, following the 1995 rules exactly.
 *
 * Speak() (audio.cpp:480) queues a line only when ALL of these hold:
 *     voice != SpeakQueue      not the line already waiting
 *     voice != CurrentVoice    not the line already talking
 *     SpeakQueue == VOX_NONE   nothing is waiting yet
 * so the queue is exactly one deep and duplicates are dropped. Speak_AI() starts the
 * waiting line once the current one ends. EVA is never interrupted mid-word.
 *
 * We previously stopped the current line and started the new one on every callback,
 * so a busy firefight cut EVA off repeatedly and no sentence ever finished. */
int cnc_audio_on_speech(CncAudio *au, int speech_index)
{
    if (!au)
        return -1;
    if (speech_index < 0 || speech_index >= SFX_VOX_COUNT)
        return -1;

    audio_backend_lock();
    if (speech_index == au->speech_queued || speech_index == au->speech_current
        || au->speech_queued >= 0) {
        audio_backend_unlock();
        return -1; /* dropped, exactly as Speak() drops it */
    }
    au->speech_queued = speech_index;
    audio_backend_unlock();
    return 0;
}

/* --------------------------------------------------------------- music */

static int music_open(CncAudio *au, const char *filename, int loop)
{
    char err[256];
    AudStream *a = bank_open_stream(au->bank, filename, err, (int)sizeof err);
    if (!a)
        return 0;
    if (aud_channels(a) != 1) {
        aud_close(a);
        return 0;
    }
    audio_backend_lock();
    if (au->music)
        aud_close(au->music);
    au->music = a;
    au->music_loop = loop;
    mixer_music_reset(au->mix);
    strncpy(au->music_name, filename, sizeof au->music_name - 1);
    au->music_name[sizeof au->music_name - 1] = 0;
    audio_backend_unlock();
    return 1;
}

int cnc_music_play_theme_var(CncAudio *au, const char *base, int loop, int variation)
{
    if (!au)
        return 0;

    char name[40];

    if (variation) {
        snprintf(name, sizeof name, "%s.VAR", base);
        if (bank_has(au->bank, name) && music_open(au, name, loop))
            return 1;
    }
    snprintf(name, sizeof name, "%s.AUD", base);
    if (music_open(au, name, loop))
        return 1;

    /* ROUT and HEART are on the 1995 disc as .VAR ONLY: there is no ROUT.AUD or
     * HEART.AUD in SCORES.MIX. Without this fallback those two themes are silent
     * whenever the variation flag happens to be off. The .VAR is still the DOS
     * recording of the same track, so this is not a substitution. */
    snprintf(name, sizeof name, "%s.VAR", base);
    return music_open(au, name, loop);
}

int cnc_music_play_theme(CncAudio *au, const char *base, int loop)
{
    return cnc_music_play_theme_var(au, base, loop, 0);
}

void cnc_music_stop(CncAudio *au)
{
    if (!au)
        return;

    audio_backend_lock();
    if (au->music) {
        aud_close(au->music);
        au->music = NULL;
    }
    au->music_name[0] = 0;
    mixer_music_reset(au->mix);
    audio_backend_unlock();
}

void cnc_music_fade_out(CncAudio *au, int ms)
{
    if (!au)
        return;

    mixer_bus_gain(au->mix, MIX_BUS_MUSIC, 0, ms);
}

int cnc_music_playing(const CncAudio *au) { return au && au->music != NULL; }
const char *cnc_music_current(const CncAudio *au) { return au ? au->music_name : ""; }

/* Still playing includes the ring, not just the open file. The stream closes as soon
 * as its last sample has been DECODED, but up to three seconds of it are still queued
 * ahead of the speaker at that point, and starting the next track then would cut the
 * end off every single one (music_open resets the ring). */
int cnc_music_busy(const CncAudio *au)
{
    if (!au)
        return 0;
    if (au->music)
        return 1;
    return mixer_music_level(au->mix) > 0;
}

/* ---------------------------------------------------- the score playlist
 *
 * ThemeClass, transliterated. Two rules and nothing else:
 *
 *   Is_Allowed (theme.cpp:457)  a theme may play if the file is on the disc and its
 *                               Normal flag is set. The other arms of that test are
 *                               all under Special.IsVariation / Is_Demo, which are
 *                               off in the shipping single player game.
 *   Next_Song  (theme.cpp:222)  a theme whose Repeat flag is set plays again; anything
 *                               else walks FORWARD through the table to the next
 *                               allowed entry, wrapping at the end. Sequential, not
 *                               shuffled, because Options.IsScoreShuffle defaults off.
 *
 * The 1995 game runs this from Main_Loop (conquer.cpp:1602): "if there is no theme
 * playing, but it looks like one is required, then start one playing". The Remaster
 * DLL has that line commented out (dllinterface.cpp:1546), which is exactly why the
 * host has to do it, and this is the host doing it.
 */
int cnc_music_theme_index(const char *base)
{
    int i;
    if (!base)
        return -1;
    for (i = 0; i < SFX_THEME_COUNT; i++) {
        const char *a = sfx_theme[i].name, *b = base;
        while (*a && *b && ((*a | 32) == (*b | 32))) {
            a++;
            b++;
        }
        if (!*a && !*b)
            return i;
    }
    return -1;
}

static int theme_allowed(CncAudio *au, int i)
{
    char name[40];
    if (i < 0 || i >= SFX_THEME_COUNT || !sfx_theme[i].normal)
        return 0;
    snprintf(name, sizeof name, "%s.AUD", sfx_theme[i].name);
    if (bank_has(au->bank, name))
        return 1;
    snprintf(name, sizeof name, "%s.VAR", sfx_theme[i].name);
    return bank_has(au->bank, name);
}

int cnc_music_next_theme(CncAudio *au, int current)
{
    int i, n;
    if (!au)
        return -1;
    if (current >= 0 && current < SFX_THEME_COUNT && sfx_theme[current].repeat)
        return current;
    if (au->shuffle) {
        /* Sim_Random_Pick over the ALLOWED themes, which is what 1995 does: it picks a
           number and then finds a playable track, rather than picking a slot that may be
           empty. Counting first means a disc with three tracks shuffles between three,
           not between thirty with twenty-seven retries.
           The same track twice running is refused when there is more than one to choose
           from -- the 1995 pick can repeat, but on a nine-track disc that reads as the
           music having stopped, which is the complaint shuffle exists to answer. */
        int allowed[SFX_THEME_COUNT], na = 0;
        for (i = 0; i < SFX_THEME_COUNT; i++)
            if (theme_allowed(au, i))
                allowed[na++] = i;
        if (na <= 0)
            return -1;
        if (na == 1)
            return allowed[0];
        for (n = 0; n < 16; n++) {
            au->shuffle_seed = au->shuffle_seed * 1103515245u + 12345u;
            i = allowed[(au->shuffle_seed >> 16) % (unsigned)na];
            if (i != current)
                return i;
        }
        return allowed[0];
    }
    i = current;
    for (n = 0; n < SFX_THEME_COUNT; n++) {
        i++;
        if (i >= SFX_THEME_COUNT || i < 0)
            i = 0;
        if (theme_allowed(au, i))
            return i;
    }
    return -1; /* no score on this disc at all */
}

int cnc_music_play_index(CncAudio *au, int index)
{
    if (!au || index < 0 || index >= SFX_THEME_COUNT)
        return 0;
    if (!cnc_music_play_theme(au, sfx_theme[index].name, sfx_theme[index].repeat))
        return 0;
    au->theme_index = index;
    printf("MUSIC|play|%s|theme=%d|%ds|%s\n", sfx_theme[index].name, index,
           sfx_theme[index].duration, sfx_theme[index].repeat ? "repeats" : "once");
    fflush(stdout);
    return 1;
}

int cnc_music_index(const CncAudio *au) { return au ? au->theme_index : -1; }

void cnc_music_set_playlist(CncAudio *au, int on)
{
    if (!au)
        return;
    au->playlist = on ? 1 : 0;
}

void cnc_music_set_shuffle(CncAudio *au, int on)
{
    if (!au)
        return;
    au->shuffle = on ? 1 : 0;
    if (!au->shuffle_seed)
        au->shuffle_seed = 0x9E3779B9u;
}

/* SEEDED FROM THE MISSION, which is the only way to have both of the things wanted here.
   A wall-clock seed would give a different score every time and break the determinism law
   -- two --shot runs of the same mission must be identical, and five gates read MUSIC|.
   A single fixed seed is reproducible but plays the SAME "random" order in every mission
   for ever, so two missions in a row open on the same track and it does not read as
   shuffled at all. Hashing the scenario name gives a different order per mission and the
   same order every time for one mission. FNV-1a, because it is four lines. */
void cnc_music_seed_shuffle(CncAudio *au, const char *key)
{
    unsigned int h = 2166136261u;
    if (!au)
        return;
    while (key && *key) {
        h ^= (unsigned char)*key++;
        h *= 16777619u;
    }
    /* AND MIXED, because the names differ by one or two characters. SCM01EA, SCM02EA and
       SCG90EA all opened on the same track before this: FNV over near-identical strings
       leaves the low bits near-identical, and one LCG step plus a shift was not enough to
       pull them apart. Four warm-up steps and an xorshift finisher decorrelate them.
       Measured after: SCM01EA, SCM02EA, SCM03EA and SCG90EA all open on different tracks. */
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    h *= 3266489917u;
    h ^= h >> 16;
    au->shuffle_seed = h ? h : 0x9E3779B9u;
}

int cnc_music_shuffle(const CncAudio *au) { return au ? au->shuffle : 0; }

/* --------------------------------------------------------------- movies */

int cnc_movie_push(CncAudio *au, const short *pcm, int samples)
{
    if (!au)
        return 0;

    int n;
    audio_backend_lock();
    n = mixer_movie_push(au->mix, pcm, samples);
    audio_backend_unlock();
    return n;
}

void cnc_movie_reset(CncAudio *au)
{
    if (!au)
        return;

    audio_backend_lock();
    mixer_movie_reset(au->mix);
    audio_backend_unlock();
}

int cnc_movie_pending(const CncAudio *au) { return au ? mixer_movie_level(au->mix) : 0; }

/* --------------------------------------------------------------- volume */

void cnc_audio_set_music_volume(CncAudio *au, int v255)
{
    if (!au)
        return;

    if (v255 < 0)
        v255 = 0;
    if (v255 > 255)
        v255 = 255;
    au->music_vol255 = v255;
    mixer_bus_gain(au->mix, MIX_BUS_MUSIC, v255 * MIX_UNITY / 255, 0);
}

void cnc_audio_set_sound_volume(CncAudio *au, int v255)
{
    if (!au)
        return;

    if (v255 < 0)
        v255 = 0;
    if (v255 > 255)
        v255 = 255;
    au->sound_vol255 = v255;
    mixer_bus_gain(au->mix, MIX_BUS_FX, v255 * MIX_UNITY / 255, 0);
    mixer_bus_gain(au->mix, MIX_BUS_SPEECH, v255 * MIX_UNITY / 255, 0);
}

int cnc_audio_get_music_volume(const CncAudio *au) { return au ? au->music_vol255 : 0; }
int cnc_audio_get_sound_volume(const CncAudio *au) { return au ? au->sound_vol255 : 0; }

/* ----------------------------------------------------------------- pump */


void cnc_audio_reset_speech(CncAudio *au)
{
    if (!au)
        return;
    audio_backend_lock();
    if (au->speech_voice >= 0)
        mixer_voice_stop(au->mix, au->speech_voice);
    mixer_stop_bus(au->mix, MIX_BUS_SPEECH);
    au->speech_voice = -1;
    au->speech_queued = -1;
    au->speech_current = -1;
    audio_backend_unlock();
}

/* Speak_AI: start the queued line once the current one has finished. Once per frame. */
static void speech_ai(CncAudio *au)
{
    char name[32];
    const SndClip *c;
    int h;

    if (!au)
        return;

    /* CurrentVoice only means "talking right now". The 1995 engine clears it when the
     * line ends, so the same line may be said again later. Without this a battle that
     * raises "unit lost" repeatedly says it once and is mute for the rest of the game. */
    if (au->speech_voice >= 0 && !mixer_voice_active(au->mix, au->speech_voice)) {
        au->speech_voice = -1;
        au->speech_current = -1;
    }

    if (au->speech_queued < 0)
        return;
    if (au->speech_voice >= 0)
        return; /* EVA is still talking; the queued line waits its turn */

    snprintf(name, sizeof name, "%s.AUD", sfx_vox[au->speech_queued]);
    c = bank_get(au->bank, name);
    if (!c) {
        au->speech_queued = -1; /* not on the disc; drop it, never substitute */
        return;
    }
    h = mixer_play(au->mix, MIX_BUS_SPEECH, c->pcm, c->samples, MIX_UNITY, 0, 255);
    if (h >= 0)
        pin_for_voice(au, h, name);
    au->speech_voice = h;
    au->speech_current = au->speech_queued;
    au->speech_queued = -1;
}

void cnc_audio_update(CncAudio *au)
{
    if (!au)
        return;

    retire_finished(au);
    speech_ai(au); /* Speak_AI: start the queued EVA line once the current one ends */

    /* ThemeClass::AI. Nothing playing and the score is switched on: pick the next
     * allowed track. Score volume at zero counts as switched off, which is what
     * `Options.ScoreVolume &&` does in theme.cpp:185. */
    if (au->playlist && au->music_vol255 > 0 && !cnc_music_busy(au)) {
        int next = cnc_music_next_theme(au, au->theme_index);
        if (next >= 0)
            cnc_music_play_index(au, next);
        else
            au->playlist = 0; /* no score on this disc; stop asking every frame */
    }

    /* Top the music ring up. Decoding happens here, on the game thread, never in the
     * audio interrupt, which is what makes a Win98 backend a matter of writing a
     * double buffer rather than making stdio interrupt safe. */
    int topups = 0;
    while (au->music && topups < MUSIC_TOPUPS) {
        int want;
        int n;

        topups++;
        audio_backend_lock();
        want = mixer_music_want(au->mix);
        audio_backend_unlock();
        if (want < MUSIC_CHUNK)
            break;

        n = aud_read(au->music, au->scratch, MUSIC_CHUNK, au->music_loop);
        if (n <= 0) {
            audio_backend_lock();
            aud_close(au->music);
            au->music = NULL;
            au->music_name[0] = 0;
            audio_backend_unlock();
            break;
        }
        audio_backend_lock();
        mixer_music_push(au->mix, au->scratch, n);
        audio_backend_unlock();
    }
}
