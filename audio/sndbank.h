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

#include "wsaud.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BANK_RATE 22050

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

/* Is the name resolvable at all, without decoding it. */
int bank_has(SndBank *b, const char *name);

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
