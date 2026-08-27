# C&C 3D: the audio engine

Every sound the game makes, out of the 1995 MS-DOS Tiberian Dawn discs, mixed into one
stereo 16-bit stream at 22050 Hz.

**Nothing here can produce an N64 sound.** The bank only ever opens `SOUNDS.MIX`,
`SPEECH.MIX`, `SCORES.MIX`, `ZOUNDS.MIX`, `AUD.MIX` and `TRANSIT.MIX` from
`data/dosdata`, and the only decoders present are Westwood `.AUD` (both compressions).
When a sound is not on the disc, `bank_get` returns NULL, the caller plays silence, and
the name lands in the miss log. There is no fallback path that could reach anywhere else.

## Build and prove it, no sound card required

```
./build.sh                                    core + the three headless harnesses
./banktest ../../../data/dosdata --csv b.csv  decode every sound, measure every one
./mixtest  ../../../data/dosdata out.wav      render a scripted 10 s scene to a WAV
python3 tools/plot.py out.wav out.png         waveform + spectrogram, absolute dBFS

./build.sh sdl                                also builds ./playtest, the only
./playtest ../../../data/dosdata              program here that opens a device
```

`banktest` exits non-zero if any file decodes to silence, clips, or comes out with a DC
offset. Missing files are reported and do **not** fail it, because absence is a fact
about the disc rather than a bug in the code.

## What is proven, and how

Measured on the original discs, `data/dosdata`.

| Claim | Evidence |
|---|---|
| SOS ADPCM (type 99) is decoded correctly | Bit-identical to an independent Python transcription of `common/soscodec.cpp` on 8 files spanning effects, speech, both odd rates and two 2.6 to 3.6 M sample music tracks. `tools/ref_decode.py` |
| Westwood ADPCM (type 1) is decoded correctly | Bit-identical to an independent Python transcription of `common/auduncmp.cpp` on NUYELL1: 26244/26244 samples, max abs difference 0. Decoded mean 127.8 out of 255, i.e. properly centred |
| Every sound the engine can request decodes | 249 files: 0 silent, 0 clipped, 0 DC, 0 empty. `out/bank.csv` has rate, channels, bits, compression, length, peak, RMS, DC and longest rail run for each |
| No predictor runs away anywhere on the disc | Longest consecutive run at the rails across all 249 files is **27 samples** (NUKEXPLO, 1.2 ms). A saturated decoder parks for hundreds |
| Resampling to 22050 Hz is correct | Five non-22050 files checked: lengths within 1 sample of `n * 22050 / src`. For the one 44100 Hz file, band energies before and after match to 0.001, and only **0.1 %** of its energy is above 11.025 kHz so the naive 2:1 decimation aliases nothing audible |
| Music streams and loops seamlessly | IND2 decoded past its 38.08 s end: the 159744 samples after the seam are **bit-identical** to the file start, so the codec state reset on rewind works. Negative control with the reset removed: no longer identical, max deviation 121 |
| The music ring never starves | Across the whole 10 s `mixtest`, the ring stays between 60928 and 64512 of its 65535 samples. It is refilled on the game thread; the render path does no I/O |
| Panning and distance work | `mixtest` prints the placement table and then proves it in the output: 6 shots placed left give rms L 5932 / R 4736, the same 6 mirrored right give L 3314 / R 7615 |
| Fades reach real silence | Music faded to zero over 1 s at t=6.0: the second from 7.0 to 8.0 measures rms 46.5 against a bed of ~1500, and the spectrogram goes black |
| Voice stealing holds under load | 16 simultaneous effects against 32 voices: `mixer_active_voices` reports 16, nothing is dropped, nothing crashes |
| Output is deterministic | Two `mixtest` runs are byte-identical over 882044 bytes |
| No memory or UB faults | The full 249-file audit and the mix harness run clean under AddressSanitizer and UndefinedBehaviorSanitizer, zero diagnostics |

Pictures I actually looked at are in `out/`: `constru1.png` (EVA, clear formant bands and
syllable envelope), `nuyell1.png` (the type 1 scream, low-band energy with a smooth HF
rolloff), `aoi_12s.png` (music, rhythmic strikes and harmonic structure), `bleep2.png`
(a single 1.2 kHz decaying tone), `mixtest.png` (the whole scene, L and R in separate
lanes so the pan is visible, and the fade to silence plainly black).

`tools/plot.py` draws spectrograms on an **absolute** dBFS scale, not auto-gained per
file. This matters: the first version auto-gained, which stretched a quiet noise floor
into what looked like broadband garbage and nearly got a correct decode condemned.

## What the integration then added

The three items this section used to list as unproven are now two.

- **A real device HAS been driven.** `AUDIO|device|SDL2|pulled N frames` is printed at
  shutdown whenever a device was open, and a 12 second run of the merged app on the
  machine pulled 245760 frames, i.e. 11.1 seconds of audio, with no starvation. That
  proves the callback ran and the mixer was drained in real time. It does **not** prove
  what it sounded like: nobody has listened yet, and only listening can close that one.
- **A mission has now run with audio attached.** `SCG90EA` asks for 88 sound effects and
  24 EVA lines in 40 seconds of play, all 112 of them present on the disc, none silent.
  The recorded mix is `--audiowav`, and the 1995 placement rule is exercised through the
  real game path: the same fight heard from the west pans hard right at 0.35 gain and
  from the east pans left. See `docs/CHANGELOG.md` for the numbers.
- **No Win98 backend exists yet.** Still true. `audio_null.c` and `audio_sdl.c` are the
  only two backends. The core has no platform call in it, which is what makes the third
  one a new file, but that file is not written. `docs/tier1-gap.md` carries the contract.

## The pieces

```
sosadpcm.h/.c   SOS 4:1 ADPCM (common/soscodec.cpp), 16 bit and 8 bit outputs
wsadpcm.h/.c    Westwood ADPCM (common/auduncmp.cpp), AUD compression type 1
wsaud.h/.c      streaming .AUD reader: header, chunk walk, both codecs, raw chunks
mixfile.h/.c    MIX archive reader, file backed (SCORES.MIX is 54 MB)
sndbank.h/.c    named bank over the archives: decode on demand, resample, LRU cache
mixer.h/.c      the mixer: 32 voices, 4 buses, pan, ramps, music and movie rings
sfxtable.h/.c   GENERATED from the engine's own tables. Do not hand edit
sfxname.c       the Sound_Effect() extension rule, kept out of the generated file
cncaudio.h/.c   the facade: everything above behind one header
audio_null.c    no device (the harnesses)
audio_sdl.c     SDL2. The ONLY file in the engine that includes SDL
audioboot.h/.c  one engine per process: find the archives, open the device or not,
                report what the disc has, name every miss on the way out
audiotap.h/.c   render the mix into a WAV instead of a device, clocked off the SIM
wavio.h/.c      WAV writer, used by the harnesses and by audiotap.c
audtest.c banktest.c mixtest.c playtest.c   the harnesses
tools/          table extraction, MIX probes, the independent reference decoders, plot
```

`sfxtable.c` is regenerated with `python3 tools/gen_sfxtable.py`, which reads
`tools/tables.json` (from `tools/extract_tables.py`) and `tools/varuse.json` (from
`tools/varuse.py`). All three read the vanilla Tiberian Dawn source directly, so the
filenames and priorities are the engine's own, never retyped.

## The API the app calls

One header, `cncaudio.h`. Wiring in `app/cnc3d.cpp`:

```c
#include "cncaudio.h"

CncAudio *au = cnc_audio_create("dosdata", err, sizeof err);
audio_backend_open(au, err, sizeof err);
cnc_audio_set_sound_volume(au, 255);          /* the 1995 options sliders, 0..255 */
cnc_audio_set_music_volume(au, 180);
cnc_music_play_theme(au, "AOI", 1);           /* base name, no extension */
```

In the engine's `EventCallback`, which is where the sounds actually come from:

```c
case CALLBACK_EVENT_SOUND_EFFECT:
    cnc_audio_on_sound_effect(au, ev.SoundEffect.SFXIndex,
                                  ev.SoundEffect.Variation,
                                  ev.SoundEffect.PixelX,
                                  ev.SoundEffect.PixelY);
    break;
case CALLBACK_EVENT_SPEECH:
    cnc_audio_on_speech(au, ev.Speech.SpeechIndex);
    break;
```

`SFXIndex` and `SpeechIndex` index `sfx_voc[]` and `sfx_vox[]` directly: the engine's
own `VocType` and `VoxType`. The name in `ev.SoundEffect.SoundEffectName` is not needed
and is not used, because the DLL builds it with the extension rule already applied and
we would rather apply it ourselves from the table than parse a string.

`PixelX`/`PixelY` are world pixels and are `-1, -1` for a sound with no map position.
Feed the camera in once a frame so those pixels mean something:

```c
cnc_audio_set_listener(au, camera_centre_x, camera_centre_y, view_w, view_h);
cnc_audio_update(au);       /* game thread only: this is where music decoding happens */
```

`cnc_audio_update()` **must** be called from the game thread every frame. It refills the
music ring (file I/O) and retires finished voices. The render callback only reads the
ring, which is what keeps a Win98 backend from having to make stdio interrupt safe.

For the VQA player, push decoded mono 22050 Hz PCM as it goes:

```c
cnc_movie_push(au, pcm, samples);       /* returns samples accepted */
cnc_movie_reset(au);                    /* what an abort key should do */
```

Menu and sidebar sounds, which have no `VocType`, go by name:

```c
cnc_audio_play_named(au, "BLEEP2.AUD", MIX_BUS_FX, MIX_UNITY, 0, 20);
```

## How it is wired, now that it is wired

**ONE engine and ONE device for the whole process.** `audioboot.h` brings it up before
the menu and takes it down after; the menu, the movies and the tactical view all push
into the same mixer. The menu no longer opens a device of its own, which it did while
this was a standalone module: two devices on one sound card is the bug that shape of
code eventually produces.

```
app/cnc3d.cpp           audio_boot() -> DMS_Config.au and game_set_audio()
menu/dosmenu_shell.c    cnc_music_play_index() for MAP1, audio_frame() as its pump
video/moviesnd.c        binds movieplay.c's MOV_Audio to the MOVIE bus
game/cnc_eyes.cpp       ev_cb() -> cnc_audio_on_sound_effect / cnc_audio_on_speech,
                        audio_frame() once a frame, cnc_audio_set_listener from the camera
```

`mixcompat.c` is deliberately **not** here. It existed to keep the old mono `mix_*` API
alive for `dosmenu_shell.c`; that file now calls `cncaudio.h` directly, so the shim
would be dead code and dead code is worse than no code.

Two files were added by the integration and are not part of the original module:

```
audioboot.h/.c  one engine per process: find the archives, open the device (or not),
                report what the disc has, name every miss on the way out
audiotap.h/.c   render the mix into a WAV instead of a device, clocked off the SIM,
                which is what makes a headless run produce a measurable recording
```

The source list for anything that wants the full engine:

```
audio/sosadpcm.c audio/wsadpcm.c audio/wsaud.c audio/mixfile.c audio/sndbank.c \
audio/mixer.c audio/sfxtable.c audio/sfxname.c audio/cncaudio.c audio/wavio.c \
audio/audiotap.c audio/audioboot.c audio/audio_sdl.c
```

**`video/vqaplay.c` is unaffected.** It carries its own inline copy of the SOS codec and
does not include anything from here, contrary to what the old README claimed.

**One open decision:**

1. Crossing the view edge is an 8 dB step down in volume and a jump to ~83 % pan, because
   that is exactly what the 1995 code did. It is faithful and it is audible.
   Smoothing it is an open option, not a decision taken here.

(An earlier decision listed here -- loose `dosdata/music/MAP1.AUD` vs streaming from
`TRANSIT.MIX` -- was settled by the integration: the shell asks the bank for the theme
by BASE NAME (`MAP1`) and `audioboot.c` registers `TRANSIT.MIX` at boot, so the menu
theme streams from the archive. The play folder ships no loose music folder at all. A
loose file would still win if one were dropped in, and the bank now says so out loud;
see "loose files" below.)

## What the data actually contains

Facts established by reading the archives, not assumed:

- **SOUNDS.MIX holds ten compression type 1 files** and they are not decoration: they are
  `NUYELL1/3/4/5/6/7/10/11/12` and `YELL1`, which is `VOC_SCREAM1..12` and `VOC_YELL1`,
  every infantry death scream in the game. A decoder that only speaks type 99 plays every
  infantry death as silence. The old `audio/aud.c` refused type 1 outright.
- **Not everything is 22050 Hz.** `MGUN2`, `TONE2`, `TRANS1` and `YELL1` are 22222 Hz and
  `TNKFIRE2.JUV` is 44100 Hz. The old mixer rejected any file that was not 22050.
- **Chunks with `CompSize == UncompSize` are stored raw** and must be copied, not fed to a
  codec. `soundio_common.cpp` makes exactly that test.
- **`ROUT` and `HEART` exist only as `.VAR`.** There is no `ROUT.AUD` or `HEART.AUD` in
  `SCORES.MIX`, so `cnc_music_play_theme` falls back to `.VAR` or those two themes are
  silent. The `.VAR` is the DOS recording of the same track, so this is not a substitution.
- **ZOUNDS.MIX is the juvenile pack**: 49 of its entries are the `.JUV` alternates that
  `Special.IsJuvenile` selects. `OBELPOWR` has no `.JUV`, so that one falls back to `.AUD`.
- **The `.V0x` response variants split by caller, not evenly.** `Sound_Effect()` picks the
  extension from the *sign* of the variation: `InfantryClass` passes `+(ID+1)` and gets
  `.V01`/`.V03`, `UnitClass` and `AircraftClass` pass `-(ID+1)` and get `.V00`/`.V02`. So
  `ROGER`, `READY`, `UGOTIT`, `NOPROB`, `RITAWAY` and `REPORT1` exist only as `.V01/.V03`
  and `UNIT1`, `VEHIC1` only as `.V00/.V02`. Auditing all four for every entry invents 22
  missing files that the 1995 game never looked for; `sfx_voc[].varuse` records which
  signs each entry is actually used with, derived from the response tables in
  `infantry.cpp`, `unit.cpp` and `aircraft.cpp`.
- **`AUD.MIX` overlaps `SOUNDS.MIX`** (three ids match byte for byte) and none of its 32
  entries resolve to a name in the engine's tables. It is added last so it can never
  shadow `SOUNDS.MIX`. What it is has not been established.

### The 20 sounds that genuinely are not on the disc

These play as silence and say so. None of them is substituted.

| Name | Why |
|---|---|
| `DINOMOUT` `DINOYES` `DINOATK1` `DINODIE1` | the dinosaur missions' voices, absent from this disc |
| `BEACON` | multiplayer beacon, a Remaster-era addition |
| `TOSS.AUD` `TOSS.JUV` | grenade air swish (`WEAPON_GRENADE` names it) |
| `OBELPOWR.JUV` | falls back to `OBELPOWR.AUD`, which is present. Not audible as a gap |
| `TRANSSEE` `TRANLOAD` | two EVA lines: "Nod transport sighted" and "loaded" |
| `80MX226M` `CHRG226M` `CREP226M` `DRIL226M` `DRON226M` `FIST226M` `RECN226M` `VOIC226M` | eight themes the 1995 table lists that are not in `SCORES.MIX` on either disc |
| `ROUT.AUD` `HEART.AUD` | present as `.VAR`, handled by the fallback above. Not audible as a gap |

`2DANGR1` and `NEGATV1` are also absent in every form, but no response table in the
engine names them, so they are never requested and are reported as `UNUSED`, not missing.

## Design notes worth knowing

**Output is stereo.** The old mixer was mono. The engine hands a world position with
every sound effect and the 1995 game panned them, so mono would throw away information
the callback is giving us. Stereo costs one extra multiply per frame.

**The bank resamples at load, the mixer does not resample at all.** Everything in the
cache is already 22050 Hz mono, so the voice loop is a multiply and an add. Each voice
still carries a 16.16 step (fixed at unity today) so pitch variation is a one line change
later rather than a restructure.

**Gains ramp per 256-frame block, not per sample.** 11.6 ms per step at 22050 Hz, which is
inaudible on a linear fade and keeps the inner loop to two multiplies per frame. The step
is taken at the block midpoint so a ramp does not lag.

**Voice handles carry a serial.** A handle is `slot | serial << 8`, so holding a stale
handle after its voice finished and its slot was recycled is inert rather than dangerous.

**Clips are pinned while a voice plays them.** Voices read straight out of the bank cache,
so the LRU must not evict underneath one. `cnc_audio_update` unpins on retire.

**The cache is capped at 8 MB with LRU eviction.** Decoding all 249 sounds at once would
be about 12 MB, which is fine on a modern desktop and not fine on Win98.

**Speech cuts, it does not overlap.** EVA speaks one line at a time and a new line stops
the current one, matching `Speak_AI`.
