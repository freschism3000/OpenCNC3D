/* ====================================================================================
 * movieplay.h -- playing a Westwood movie inside a window somebody else owns.
 *
 * video/vqaplay.c decodes VQA. It does not know about windows, clocks or speakers.
 * This is the layer above it: the thing the app calls when the flow reaches a movie.
 *
 *      mov_play(win, "LOGO.VQA", &opts, &audio)
 *
 * It borrows the caller's SDL window and its already-current GL context, runs its own
 * event loop until the movie ends or the player skips it, and gives the window back.
 * The caller does not have to prepare anything and does not have to clean anything up.
 *
 * THREE RULES, and they are the whole reason this file exists rather than a loop
 * pasted into the menu:
 *
 * 1. IT SETS ITS OWN COMPLETE GL STATE, every frame, the same discipline
 *    dosmenu_shell.c's dms_draw follows. A movie can be entered from the menu (which
 *    leaves a textured quad bound) or from the tactical view (which leaves the depth
 *    test on, a depth buffer full of terrain and a tinted glColor). Neither may show
 *    through. See mov_present.
 *
 * 2. THE AUDIO IS THE CLOCK, not the loop. target frame = movie PCM already played /
 *    bytes per frame. If the machine cannot decode fast enough the player DROPS video
 *    frames to stay with the sound rather than letting the movie run slow, and if the
 *    device has not started yet it holds frame 0. A wall clock is used only when the
 *    movie is silent or the sink cannot say how much it has played.
 *
 * 3. THE AUDIO DEVICE IS NOT ITS BUSINESS. Everything the player needs from the sound
 *    system is the four function pointers in MOV_Audio, so it compiles against the
 *    mixer, against a WAV file, or against nothing at all. Pass NULL for a silent
 *    movie paced off the wall clock.
 *
 * THE ABORT KEY IS ESC AND ONLY ESC. conquer.cpp:2935 VQ_Call_Back reads whatever is in
 * the keyboard queue, throws it away with Keyboard->Clear(), and breaks out only when
 * the key was KN_ESC. A mouse click is KN_LMOUSE, so in 1995 clicking during a movie did
 * nothing at all. mov_play also drains pending keys and mouse buttons immediately before
 * it opens the file, which is conquer.cpp:2190 / intro.cpp:250. When a console-style
 * controller is wired up later, map its abort button onto this same ESC path rather than
 * onto "any input", or the briefing dies on the first impatient press again.
 *
 * Tier 1 note (Win98 / Voodoo 2): mov_present is the only function that touches GL and
 * it uses GL 1.1 only, one GL_NEAREST texture and one quad, which is what Glide does
 * natively. The palette-to-RGBA conversion is a plain loop on the CPU, no shader. The
 * fade is done on the CPU pixels, again no shader, because that is what the DOS
 * original did to the VGA palette. Nothing in here is Tier 2.
 * ==================================================================================== */

#ifndef MOVIEPLAY_H
#define MOVIEPLAY_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------- the sound seam */

/* Where a movie's PCM goes. 16-bit signed, mono, at the movie's own rate, which is
 * 22050 Hz for everything on the 1995 discs.
 *
 * `played` is what makes the sync work: bytes of everything push() was given that have
 * physically come out of the speakers. Return -1 (or leave the pointer NULL) if that
 * cannot be answered and the player will pace off the wall clock instead and say so. */
typedef struct MOV_Audio {
    void *user;
    void (*open)(void *user, int rate, int channels);
    void (*push)(void *user, const void *pcm, int bytes);
    long (*played)(void *user);
    void (*pump)(void *user);   /* called every loop pass: keep the device fed  */
    void (*close)(void *user);  /* movie over: drop what is queued, unduck music */

    /* OPTIONAL. 1 when everything push() was given has left the sink's OWN queue and
     * closing now loses nothing, 0 while a tail is still queued, -1/-NULL for "cannot
     * say" (the player then falls back to comparing played against pushed, bounded by
     * drain_ms). This exists because `played` deliberately subtracts the device
     * latency for frame pacing, so it NEVER reaches pushed and the fallback always
     * burns the whole drain window: 1.49 s of dead silence after every movie. A sink
     * that can see its ring answers precisely and the silence collapses to nothing. */
    int (*drained)(void *user);
} MOV_Audio;

/* --------------------------------------------------------------------- what to play */

typedef struct MOV_Opts {
    /* Presentation. 0 means the default. */
    int  plate_w, plate_h;   /* the virtual screen the movie sits on, default 320x200,
                                so a 320x156 intro is letterboxed inside the same plate
                                the menu uses instead of being stretched              */
    int  fade_out_ms;        /* fade the last frame to black, default 250 which is
                                defines.h FADE_PALETTE_MEDIUM (TIMER_SECOND/4)        */
    int  no_skip;            /* 1: unskippable (nothing in the game needs this yet)   */

    /* Pacing knobs, exposed because the balance of them is a judgement call and the
       proof harness sweeps them. 0 means the default. */
    int  catchup_cap;        /* max frames decoded without presenting in one pass, 8  */
    int  drain_ms;           /* how long to wait for the tail of the audio, 1500      */

    /* Stop cleanly after this many frames have reached the glass and report MOV_DONE.
       0 plays the whole thing. This is for a harness that wants the movie path
       exercised without spending two minutes on it, not for the player. */
    int  stop_after;

    /* Diagnostics. NULL/0 in the shipping build. */
    const char *log_path;    /* per-frame CSV: clock, target, decoded, dropped        */
    const char *shot_dir;    /* dump the BACK BUFFER as PNG at shot_frames            */
    const int  *shot_frames; /* -1 terminated list of frame indices                   */
    int   stall_every;       /* inject a stall every N presented frames: the late
                                frame test. 0 in any build anyone plays.              */
    int   stall_ms;
} MOV_Opts;

/* ------------------------------------------------------------------------ the result */

#define MOV_DONE     1   /* played to the last frame                                  */
#define MOV_SKIPPED  0   /* ESC ended it early (conquer.cpp:2949; clicks do not)      */
#define MOV_QUIT   (-1)  /* the window was closed: the caller should shut down         */
#define MOV_ERROR  (-2)  /* the file would not open; a message is on stderr            */

/* What actually happened, for the caller that wants to assert on it. Optional. */
typedef struct MOV_Stats {
    int  frames_decoded;
    int  frames_presented;
    int  frames_dropped;     /* decoded to keep up with the sound, never shown        */
    int  audio_underruns;    /* passes where the sound ran dry before the video did   */
    long audio_bytes;        /* movie PCM handed to the sink                          */
    long wall_ms;            /* how long it took on the clock on the wall             */
    long clock_ms;           /* where the movie clock finished                        */
    int  used_audio_clock;   /* 0 means it fell back to the wall clock                */
    int  worst_late_ms;      /* furthest a presented frame was behind its due time    */
} MOV_Stats;

int mov_play(SDL_Window *win, const char *path, const MOV_Opts *opts,
             const MOV_Audio *audio, MOV_Stats *stats);

/* Probe without playing: 1 and fills the numbers, 0 if the file will not open. */
int mov_probe(const char *path, int *w, int *h, int *frames, int *fps, int *rate);

#ifdef __cplusplus
}
#endif

#endif /* MOVIEPLAY_H */
