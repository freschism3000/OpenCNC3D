/*
 * audio_win98.c -- the Windows 98 backend, over waveOut.
 *
 * Tier 1 is Windows 98 with a Voodoo 2, and audio_sdl.c cannot go there:
 * SDL2 needs Windows 2000 or later. This file is the whole of the difference. It
 * implements the same six functions audio_sdl.c does and touches nothing else, which
 * is the point the seam was built for.
 *
 * WHY waveOut AND NOT DirectSound. waveOut lives in winmm.dll, which is on every
 * Windows 98 install as shipped. DirectSound needs a DirectX runtime we would then
 * have to require, detect and fall back from, and it buys nothing here: we hand the
 * device a finished stereo stream, we do not want hardware mixing, 3D positioning or
 * a secondary buffer's notification positions. The mixer already did all of that in
 * plain C.
 *
 * THE THREADING, WHICH IS THE ONLY DELICATE PART. A waveOut callback runs in an
 * interrupt-like context and Microsoft names the only calls that are legal inside it:
 * EnterCriticalSection, LeaveCriticalSection, midiOut*, OutputDebugString, PostMessage,
 * PostThreadMessage, SetEvent, timeGetSystemTime, timeGetTime, timeKillEvent and
 * timeSetEvent. mixer_render is none of those, and calling it there would be the sort
 * of thing that works on the developer's machine and stutters on a project decision. So the
 * callback does exactly one legal thing, SetEvent, and a worker thread does the work:
 * it wakes, refills every buffer the device has finished with, and re-queues it.
 *
 * That makes audio_backend_lock/unlock a CRITICAL_SECTION around the render, which is
 * exactly the guarantee SDL_LockAudioDevice gives on the other backend: a control call
 * cannot land halfway through a mix.
 *
 * NOT YET RUN ON WINDOWS 98. The renderer's own Win98 and Glide port has not started
 * (docs/tier1-gap.md), so there is no build on that machine to hear this. What is
 * proven is that it compiles as 32-bit Windows code, which CI does on every push so
 * that it cannot rot while it waits. Registered as such as a known gap.
 */

#include "cncaudio.h"

#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Four buffers of 1024 frames: 46 ms each, 186 ms of queue. SDL asks for one 1024
   frame buffer and lets the OS keep its own queue behind it; waveOut has no queue of
   its own, so the depth has to be ours. Under Windows 98 with a compile running and
   a 3dfx driver in the way, three buffers was audibly tight in the shape this code
   was written to (a refill wakes on an event, not on a timer), so it is four. */
#define WIN98_BUFFERS 4
#define WIN98_FRAMES  1024
#define WIN98_BYTES   (WIN98_FRAMES * 4) /* stereo, 2 bytes per sample */

static HWAVEOUT       g_dev;
static WAVEHDR        g_hdr[WIN98_BUFFERS];
static short         *g_buf[WIN98_BUFFERS];
static Mixer         *g_mix;
static HANDLE         g_wake;
static HANDLE         g_thread;
static CRITICAL_SECTION g_lock;
static volatile LONG  g_running;
static int            g_haveLock;
static DWORD          g_framesWritten;

static void CALLBACK win98_wave_cb(HWAVEOUT dev, UINT msg, DWORD_PTR user,
                                   DWORD_PTR p1, DWORD_PTR p2)
{
    (void)dev; (void)user; (void)p1; (void)p2;
    /* SetEvent is on the legal list. Nothing else here is, so nothing else is here. */
    if (msg == WOM_DONE && g_wake)
        SetEvent(g_wake);
}

static DWORD WINAPI win98_worker(LPVOID arg)
{
    (void)arg;
    while (g_running) {
        int i;
        /* A timeout rather than an infinite wait: if a driver ever drops a WOM_DONE
           we recover on the next tick instead of going silent for the rest of the
           session. 20 ms is well inside one buffer. */
        WaitForSingleObject(g_wake, 20);
        for (i = 0; i < WIN98_BUFFERS; i++) {
            if (!(g_hdr[i].dwFlags & WHDR_DONE))
                continue;
            EnterCriticalSection(&g_lock);
            if (g_mix)
                mixer_render(g_mix, g_buf[i], WIN98_FRAMES);
            else
                memset(g_buf[i], 0, WIN98_BYTES);
            LeaveCriticalSection(&g_lock);
            g_hdr[i].dwFlags &= ~WHDR_DONE;
            if (waveOutWrite(g_dev, &g_hdr[i], sizeof g_hdr[i]) == MMSYSERR_NOERROR)
                g_framesWritten += WIN98_FRAMES;
        }
    }
    return 0;
}

int audio_backend_open(CncAudio *au, char *err, int errlen)
{
    WAVEFORMATEX fmt;
    MMRESULT     mr;
    int          i;
    DWORD        tid;

    if (!au) {
        snprintf(err, (size_t)errlen, "no audio engine");
        return 0;
    }

    g_mix = cnc_audio_mixer(au);

    /* The same refusal audio_sdl.c makes, for the same reason: we ask for exactly what
       the mixer produces and we do not let anything resample behind our back, because
       a silent conversion hides a decoder bug rather than fixing one. waveOut has no
       "allow changes" flag, it either opens this format or it does not. */
    memset(&fmt, 0, sizeof fmt);
    fmt.wFormatTag      = WAVE_FORMAT_PCM;
    fmt.nChannels       = 2;
    fmt.nSamplesPerSec  = MIX_RATE;
    fmt.wBitsPerSample  = 16;
    fmt.nBlockAlign     = (WORD)(fmt.nChannels * fmt.wBitsPerSample / 8);
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    fmt.cbSize          = 0;

    g_wake = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!g_wake) {
        snprintf(err, (size_t)errlen, "CreateEvent failed (%lu)", (unsigned long)GetLastError());
        return 0;
    }

    mr = waveOutOpen(&g_dev, WAVE_MAPPER, &fmt, (DWORD_PTR)win98_wave_cb, 0,
                     CALLBACK_FUNCTION);
    if (mr != MMSYSERR_NOERROR) {
        /* MMSYSERR_ALLOCATED is the one a user will actually hit: something else has
           the card. Say which it is rather than printing a number nobody can read. */
        snprintf(err, (size_t)errlen, "waveOutOpen: %s (%u), wanted %d Hz 2 ch s16",
                 mr == MMSYSERR_ALLOCATED  ? "device already in use"
                 : mr == MMSYSERR_NODRIVER ? "no wave device"
                 : mr == WAVERR_BADFORMAT  ? "device refuses 22050 Hz 16 bit stereo"
                                           : "failed",
                 (unsigned)mr, MIX_RATE);
        CloseHandle(g_wake);
        g_wake = NULL;
        return 0;
    }

    InitializeCriticalSection(&g_lock);
    g_haveLock = 1;

    for (i = 0; i < WIN98_BUFFERS; i++) {
        g_buf[i] = (short *)calloc(1, WIN98_BYTES);
        if (!g_buf[i]) {
            snprintf(err, (size_t)errlen, "out of memory for the wave buffers");
            audio_backend_close();
            return 0;
        }
        memset(&g_hdr[i], 0, sizeof g_hdr[i]);
        g_hdr[i].lpData         = (LPSTR)g_buf[i];
        g_hdr[i].dwBufferLength = WIN98_BYTES;
        if (waveOutPrepareHeader(g_dev, &g_hdr[i], sizeof g_hdr[i]) != MMSYSERR_NOERROR) {
            snprintf(err, (size_t)errlen, "waveOutPrepareHeader failed on buffer %d", i);
            audio_backend_close();
            return 0;
        }
    }

    g_framesWritten = 0;
    g_running = 1;
    g_thread = CreateThread(NULL, 0, win98_worker, NULL, 0, &tid);
    if (!g_thread) {
        snprintf(err, (size_t)errlen, "CreateThread for the audio refill failed (%lu)",
                 (unsigned long)GetLastError());
        g_running = 0;
        audio_backend_close();
        return 0;
    }
    /* Above normal, not time critical. The refill is 46 ms of work every 46 ms and it
       must beat the renderer to the CPU, but a starved main loop on a Pentium II is a
       worse bug than a late buffer. */
    SetThreadPriority(g_thread, THREAD_PRIORITY_ABOVE_NORMAL);

    /* Prime every buffer with real sound and hand them all to the device at once, so
       the first thing heard is the mix and not the gap before the first WOM_DONE. */
    for (i = 0; i < WIN98_BUFFERS; i++) {
        EnterCriticalSection(&g_lock);
        mixer_render(g_mix, g_buf[i], WIN98_FRAMES);
        LeaveCriticalSection(&g_lock);
        if (waveOutWrite(g_dev, &g_hdr[i], sizeof g_hdr[i]) == MMSYSERR_NOERROR)
            g_framesWritten += WIN98_FRAMES;
    }

    err[0] = 0;
    return 1;
}

void audio_backend_close(void)
{
    int i;

    if (g_running) {
        g_running = 0;
        if (g_wake)
            SetEvent(g_wake);
        if (g_thread) {
            WaitForSingleObject(g_thread, 1000);
            CloseHandle(g_thread);
            g_thread = NULL;
        }
    }

    if (g_dev) {
        waveOutReset(g_dev); /* marks every queued buffer done before we unprepare */
        for (i = 0; i < WIN98_BUFFERS; i++)
            if (g_hdr[i].dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(g_dev, &g_hdr[i], sizeof g_hdr[i]);
        waveOutClose(g_dev);
        g_dev = NULL;
    }

    for (i = 0; i < WIN98_BUFFERS; i++) {
        free(g_buf[i]);
        g_buf[i] = NULL;
    }
    memset(g_hdr, 0, sizeof g_hdr);

    if (g_wake) {
        CloseHandle(g_wake);
        g_wake = NULL;
    }
    if (g_haveLock) {
        DeleteCriticalSection(&g_lock);
        g_haveLock = 0;
    }
    g_mix = NULL;
    g_framesWritten = 0;
}

/* Frames handed to the device that the speaker has not reached yet. The movie player
   subtracts this from what it has pushed, or the picture runs ahead of the sound for
   the whole film. waveOutGetPosition in TIME_SAMPLES is the device's own answer; if a
   driver refuses that format we fall back to the nominal queue depth, which is what
   the SDL backend reports in every case. */
int audio_backend_latency(void)
{
    MMTIME mt;

    if (!g_dev)
        return 0;

    memset(&mt, 0, sizeof mt);
    mt.wType = TIME_SAMPLES;
    if (waveOutGetPosition(g_dev, &mt, sizeof mt) == MMSYSERR_NOERROR
        && mt.wType == TIME_SAMPLES) {
        long inflight = (long)g_framesWritten - (long)mt.u.sample;
        if (inflight < 0)
            inflight = 0;
        return (int)inflight;
    }
    return (WIN98_BUFFERS - 1) * WIN98_FRAMES;
}

void audio_backend_lock(void)
{
    if (g_haveLock)
        EnterCriticalSection(&g_lock);
}

void audio_backend_unlock(void)
{
    if (g_haveLock)
        LeaveCriticalSection(&g_lock);
}

const char *audio_backend_name(void) { return "waveOut (Win98)"; }
