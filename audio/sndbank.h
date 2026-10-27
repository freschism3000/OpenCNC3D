/*
 * sndbank.h -- the named sound bank.
 *
 * Open the DOS archives once, index them by name, and hand back decoded PCM on
 * demand. Everything it returns is signed 16 bit mono at MIX_RATE, so the mixer
 * never resamples and never branches on format.
 *
 * Search order is the order the archives were added, first hit wins. The 1995 rules
 * that matter:
 *
 *   SOUNDS.MIX   the sound effects
 *   SPEECH.MIX   the EVA lines
 *   SCORES.MIX   the music (streamed, never cached)
 *   ZOUNDS.MIX   the juvenile alternates, the ".JUV" variants only
 *   AUD.MIX      overlaps SOUNDS.MIX; added last so it never shadows it
 *
 * Loose .AUD directories can be added too, and are searched before the archives, so
 * a modder or a test can drop a file in without rebuilding an archive.
 *
 * A name that is nowhere is not an error and is never substituted. bank_get returns
 * NULL, the caller plays silence, and bank_report_misses prints the list. That is
 * a project rule: no sound is better than the wrong sound.
 */

#ifndef SNDBANK_H
#define SNDBANK_H

#include "mixer.h"
#include "wsaud.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BANK_RATE MIX_RATE /* what the mixer plays, so it never resamples */

typedef struct SndBank SndBank;

typedef struct
{
    const short *pcm;  /* signed 16 bit mono at BANK_RATE */
    long samples;      /* length in samples               */
    int src_rate;      /* what the file said, before resampling */
    int src_bits;      /* 8 or 16                          */
    int src_channels;  /* 1 or 2                           */
    int src_comp;      /* 1 or 99                          */
} SndClip;

/* cache_budget_bytes caps the decoded PCM held in RAM; least recently used clips are
 * dropped past it. Pass 0 for the default (8 MB). */
SndBank *bank_create(long cache_budget_bytes);
void bank_destroy(SndBank *b);

int bank_add_mix(SndBank *b, const char *path, char *err, int errlen);
int bank_add_dir(SndBank *b, const char *path); /* loose .AUD files, searched first */
/* A language pack's archive, searched before every English one, its death sounds never
   used; the cache is emptied so no English clip outlives the switch. Returns 1 on success. */
int bank_add_language(SndBank *b, const char *path, char *err, int errlen);
/* Back to English: every language archive closed, the cache emptied. Returns how many. */
int bank_drop_languages(SndBank *b);

/* Is the name resolvable at all, without decoding it. */
int bank_has(SndBank *b, const char *name);

/* NEW AUDIO. While on, bank_get("XPLOS.AUD") first looks for XPLOS.wav in the folder
 * given here and plays that instead; a name with no file there keeps its 1995 sound.
 * XPLOS_2.wav, XPLOS_3.wav and on to _16 join the same pool, and each request draws
 * one of them at random. Off by default. Music streams through bank_open_stream and is
 * never replaced. */
void bank_set_new_audio_dir(SndBank *b, const char *dir);
void bank_set_new_audio(SndBank *b, int on);
int bank_new_audio(const SndBank *b);

/* Decode (or return cached). NULL means "not on the disc" or "failed to decode";
 * the reason lands in the miss log either way. The pointer stays valid until the
 * clip is evicted, which only happens inside another bank_get. Grab the samples you
 * need before calling bank_get again if you are keeping a raw pointer. */
const SndClip *bank_get(SndBank *b, const char *name);

/* A pinned clip is never evicted. Voices pin what they are playing. */
void bank_pin(SndBank *b, const char *name, int pinned);

/* Music: a streaming handle straight out of the archive, nothing cached. */
AudStream *bank_open_stream(SndBank *b, const char *name, char *err, int errlen);

/* Diagnostics. */
long bank_cache_bytes(const SndBank *b);
int bank_clip_count(const SndBank *b);
int bank_miss_count(const SndBank *b);
const char *bank_miss(const SndBank *b, int i);

#ifdef __cplusplus
}
#endif

#endif /* SNDBANK_H */
