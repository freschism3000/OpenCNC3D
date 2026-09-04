/*
 * cncaudio.h -- the whole audio engine behind one header. This is the only file the
 * app has to include.
 *
 * It owns a SndBank (the DOS archives) and a Mixer (voices and buses) and it turns
 * the engine's own callbacks into sound. Plain C, no SDL, no Windows: the device
 * lives behind audio_backend.h and is chosen at link time.
 *
 * Typical wiring in the app:
 *
 *     CncAudio *au = cnc_audio_create("playable/dosdata", err, sizeof err);
 *     audio_backend_open(au, err, sizeof err);          // SDL today
 *     cnc_music_play_theme(au, "AOI", 1);
 *     ...
 *     // in the engine's EventCallback:
 *     case CALLBACK_EVENT_SOUND_EFFECT:
 *         cnc_audio_on_sound_effect(au, ev.SoundEffect.SFXIndex,
 *                                   ev.SoundEffect.Variation,
 *                                   ev.SoundEffect.PixelX, ev.SoundEffect.PixelY);
 *         break;
 *     case CALLBACK_EVENT_SPEECH:
 *         cnc_audio_on_speech(au, ev.Speech.SpeechIndex);
 *         break;
 *     ...
 *     // once a frame, on the game thread:
 *     cnc_audio_set_listener(au, cam_x, cam_y, view_w, view_h);
 *     cnc_audio_update(au);
 *
 * cnc_audio_update() is where the music decoding happens. It must be called from
 * the game thread, never from the audio interrupt: it does file I/O.
 */

#ifndef CNCAUDIO_H
#define CNCAUDIO_H

#include "mixer.h"
#include "sndbank.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CncAudio CncAudio;

/* dosdata_dir holds SOUNDS.MIX, SPEECH.MIX, SCORES.MIX, ZOUNDS.MIX, AUD.MIX and a
 * music/ subdirectory of loose .AUD files. Missing archives are not fatal: whatever
 * is there is indexed and the rest reports as misses. */
CncAudio *cnc_audio_create(const char *dosdata_dir, char *err, int errlen);
void cnc_audio_destroy(CncAudio *au);

Mixer *cnc_audio_mixer(CncAudio *au);
SndBank *cnc_audio_bank(CncAudio *au);

/* Pump: refills the music ring and retires finished voices. Game thread only. */
void cnc_audio_update(CncAudio *au);

/* ---------------------------------------------------------------- listener */

/* World pixel coordinates of the view centre, and the view size in world pixels.
 * These are the same units the sound callback's PixelX / PixelY arrive in. */
void cnc_audio_set_listener(CncAudio *au, int cx, int cy, int view_w, int view_h);

/* --------------------------------------------------------- engine callbacks */

/* A NEW ENGINE TICK, and with it a new duplicate window. Call it immediately BEFORE the
 * advance that raises the tick's events, from whichever loop owns the clock.
 *
 * Effects arrive in bursts from one advance: a flame tank catching a squad raises a dozen
 * deaths inside a single call, they all reach the mixer before the next block is rendered,
 * and copies of ONE clip started that way begin at the same sample of it. Identical
 * waveforms in lockstep sum COHERENTLY -- twelve of them is twelve times the amplitude of
 * one, which is distortion rather than loudness. cnc_audio_on_sound_effect lets only a
 * couple of copies of any one clip start per window and refuses the rest.
 *
 * THE WINDOW IS THE ENGINE TICK, never wall time and never a count of ringing voices. A
 * script advances the same number of times whatever the frame rate and whatever the
 * machine, so the same script keeps producing the same recording, which is what the audio
 * gates compare. A voice count would not: voices retire only when something renders the
 * mix, so a run with a device open would refuse different sounds than a headless one, and
 * a two second clip would refuse the fifth shot of an ordinary exchange of fire.
 *
 * A host that never calls this is never capped, deliberately. The mixer harnesses fire
 * sixteen copies of one clip at once ON PURPOSE, to prove the voice pool steals correctly,
 * and they have no tick to key a window on. It is also the safe direction for a host that
 * forgets the call: it gets the old behaviour, never silence. */
void cnc_audio_begin_tick(CncAudio *au);

/* What cnc_audio_on_sound_effect returns for a copy refused by that rule. It is NOT a
 * missing sound and must never be reported as one: -1 keeps its old meaning of "not on the
 * disc, or not audible from here". A caller that only tests `>= 0` needs no change. */
#define CNC_SFX_DUPLICATE (-2)

/* CALLBACK_EVENT_SOUND_EFFECT. x and y are ev.SoundEffect.PixelX / PixelY; the
 * engine passes -1, -1 for a sound with no map position. Returns the voice handle,
 * -1 if it was dropped or the sound is not on the disc, or CNC_SFX_DUPLICATE if this
 * tick has already had its fill of that clip. */
int cnc_audio_on_sound_effect(CncAudio *au, int sfx_index, int variation, int x, int y);

/* CALLBACK_EVENT_SPEECH. EVA is one line at a time: a new line CUTS the current one
 * and starts immediately, so the newest always wins. That is a cut, not a crossfade,
 * because the 1995 behaviour is a cut. */
int cnc_audio_on_speech(CncAudio *au, int speech_index);

/* Call at every mission boundary. The audio engine outlives a mission (one device per
 * process), so EVA's queue and its "what is talking now" handle would otherwise carry
 * into the next game. A stale handle reads as still-talking forever and the second
 * mission is mute. */
void cnc_audio_reset_speech(CncAudio *au);

/* What the placement rule decided for the LAST effect, and where the listener was
 * standing when it did. Diagnostics only: it lets a headless run print the gain and
 * pan it computed instead of leaving them to be guessed from a recording. */
void cnc_audio_last_effect(const CncAudio *au, int *gain, int *pan);
void cnc_audio_listener(const CncAudio *au, int *cx, int *cy, int *vw, int *vh);

/* Play by bare name, for the shell: menu clicks, the sidebar, tests.
 * `name` includes the extension, e.g. "BLEEP2.AUD". */
int cnc_audio_play_named(CncAudio *au, const char *name, int bus, int gain, int pan, int priority);

/* Same, but the voice LOOPS seamlessly until it is stopped: see mixer_play_loop for
 * why a re-trigger cannot be gapless on a block-based output. Used for the side
 * select screen's STRUGGLE.AUD bed. The clip stays pinned in the bank for as long as
 * the voice lives, so the caller MUST mixer_voice_stop it on every exit path. */
int cnc_audio_play_named_loop(CncAudio *au, const char *name, int bus, int gain, int pan, int priority);

/* Where cnc_audio_create was pointed. The shell needs it for the non-audio data that
 * lives in the same folder (LOCAL.MIX carries CONQUER.ENG, the 1995 string table),
 * and it is the audio engine that already knows the path. "" if there is none. */
const char *cnc_audio_dosdata(const CncAudio *au);

/* ------------------------------------------------------------------- music */

/* `base` is a theme base name with no extension, e.g. "AOI". If the theme is flagged
 * as having a variation and `variation` is set, NAME.VAR is tried first, exactly as
 * ThemeClass::Theme_File_Name does. Returns 1 on success. */
int cnc_music_play_theme(CncAudio *au, const char *base, int loop);
int cnc_music_play_theme_var(CncAudio *au, const char *base, int loop, int variation);
void cnc_music_stop(CncAudio *au);
void cnc_music_fade_out(CncAudio *au, int ms); /* ThemeClass::Fade_Out is 1 second  */
int cnc_music_playing(const CncAudio *au);
const char *cnc_music_current(const CncAudio *au);

/* Still busy INCLUDING what is already queued ahead of the speaker. Use this, not
 * cnc_music_playing, to decide when the next track may start: the stream closes when
 * its last sample is decoded, up to three seconds before it is heard. */
int cnc_music_busy(const CncAudio *au);

/* ---------------------------------------------------- the score playlist
 *
 * ThemeClass, transliterated from theme.cpp. See the block comment in cncaudio.c for
 * the two rules and where they come from. Turn the playlist on and the engine walks
 * the 1995 score by itself, exactly as Main_Loop did; turn it off and whatever was
 * asked for last keeps playing (the menu, which loops one track).
 */
int cnc_music_theme_index(const char *base);          /* "AOI" -> ThemeType, or -1  */
int cnc_music_next_theme(CncAudio *au, int current);  /* ThemeClass::Next_Song      */
int cnc_music_play_index(CncAudio *au, int index);    /* play that ThemeType now    */
int cnc_music_index(const CncAudio *au);              /* what is playing, or -1     */
void cnc_music_set_playlist(CncAudio *au, int on);
void cnc_music_set_shuffle(CncAudio *au, int on);     /* Options.IsScoreShuffle      */
void cnc_music_seed_shuffle(CncAudio *au, const char *key);  /* per mission, reproducible */
int  cnc_music_shuffle(const CncAudio *au);

/* ------------------------------------------------------------------ movies */

/* The VQA player pushes decoded mono 22050 Hz PCM here as it goes. Returns samples
 * accepted; a short return means the ring is full and the video is ahead. */
int cnc_movie_push(CncAudio *au, const short *pcm, int samples);
void cnc_movie_reset(CncAudio *au);
int cnc_movie_pending(const CncAudio *au);

/* ------------------------------------------------------------------ volume */

/* The 1995 options dialog has two sliders, 0..255. These map them straight on. */
void cnc_audio_set_music_volume(CncAudio *au, int v255);
void cnc_audio_set_sound_volume(CncAudio *au, int v255);
int cnc_audio_get_music_volume(const CncAudio *au);
int cnc_audio_get_sound_volume(const CncAudio *au);

/* Special.IsJuvenile: switches IN_JUV effects to their .JUV alternates. */
void cnc_audio_set_juvenile(CncAudio *au, int on);

/* -------------------------------------------------------------- the device */

/* Implemented by exactly one backend object: audio_sdl.c today, audio_win98.c
 * later, audio_null.c for the headless harness. */
int audio_backend_open(CncAudio *au, char *err, int errlen);
void audio_backend_close(void);
void audio_backend_lock(void);   /* keeps a control call out of a render */
void audio_backend_unlock(void);
const char *audio_backend_name(void);

/* Frames the device has taken but not yet made audible: one callback buffer on SDL,
 * zero when nothing is open. The movie player subtracts it to keep the picture on the
 * sound rather than one buffer ahead of it. */
int audio_backend_latency(void);

#ifdef __cplusplus
}
#endif

#endif /* CNCAUDIO_H */
