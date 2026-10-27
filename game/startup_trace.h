#ifndef OPENCNC3D_STARTUP_TRACE_H
#define OPENCNC3D_STARTUP_TRACE_H

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

struct StartupTraceState {
    FILE* output = NULL;
    bool checked = false;
    bool active = false;
    bool ready = false;
    bool retireGpu = false;
    unsigned mission = 0;
    unsigned prepareCalls = 0;
    Uint64 frequency = 0;
    Uint64 bootBegin = 0;
    Uint64 bootEnd = 0;
    Uint64 prepareTicks = 0;
    Uint64 frameBegin = 0;
    Uint64 drawBegin = 0;
    Uint64 swapBegin = 0;
    Uint64 swapEnd = 0;
    Uint64 completed = 0;
    Uint64 retirementTicks = 0;
    char scene[64] = {};
};

static StartupTraceState g_startupTrace;

static void startup_trace_emit(const char* event, Uint64 counter, size_t objects = 0,
                               int engineFrame = -1, int simTicks = -1,
                               int width = 0, int height = 0, Uint32 flags = 0)
{
    StartupTraceState& state = g_startupTrace;
    if (!state.output) return;
    fprintf(state.output, "1,%s,%u,%llu,%llu,%llu,%llu,%llu,%u,%llu,%llu,%llu,%llu,%llu,%llu,%zu,%d,%d,%d,%d,%u,%d,%s\n",
            event, state.mission, (unsigned long long)counter,
            (unsigned long long)state.frequency, (unsigned long long)state.bootBegin,
            (unsigned long long)state.bootEnd, (unsigned long long)state.prepareTicks,
            state.prepareCalls, (unsigned long long)state.frameBegin,
            (unsigned long long)state.drawBegin, (unsigned long long)state.swapBegin,
            (unsigned long long)state.swapEnd, (unsigned long long)state.completed,
            (unsigned long long)state.retirementTicks, objects, engineFrame, simTicks,
            width, height, (unsigned)flags, state.retireGpu ? 1 : 0, state.scene);
    fflush(state.output);
}

static void startup_trace_begin(const char* scene)
{
    StartupTraceState& state = g_startupTrace;
    if (!state.checked) {
        state.checked = true;
        const char* path = getenv("OPENCNC3D_STARTUP_TRACE");
        if (!path || !*path) return;
        const int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0600);
        if (descriptor < 0) {
            fprintf(stderr, "STARTUPTRACE|unavailable|cannot create fresh trace\n");
            return;
        }
        state.output = fdopen(descriptor, "wb");
        if (!state.output) {
            close(descriptor);
            fprintf(stderr, "STARTUPTRACE|unavailable|cannot open trace stream\n");
            return;
        }
        state.frequency = SDL_GetPerformanceFrequency();
        const char* retire = getenv("OPENCNC3D_STARTUP_GPU_RETIRE");
        state.retireGpu = retire && !strcmp(retire, "1");
        fprintf(state.output, "schema,event,mission,counter,frequency,boot_begin,boot_end,prepare_ticks,prepare_calls,frame_begin,draw_begin,swap_begin,swap_end,completed,gpu_retire_ticks,objects,engine_frame,sim_ticks,width,height,window_flags,gpu_retire_enabled,scene\n");
    }
    if (!state.output) return;
    if (state.active && !state.ready) startup_trace_emit("abandoned", SDL_GetPerformanceCounter());
    state.active = true;
    state.ready = false;
    state.mission++;
    state.prepareCalls = 0;
    state.prepareTicks = 0;
    state.bootEnd = state.frameBegin = state.drawBegin = 0;
    state.swapBegin = state.swapEnd = state.completed = state.retirementTicks = 0;
    size_t length = 0;
    while (scene && scene[length] && length + 1 < sizeof state.scene) {
        const char value = scene[length];
        state.scene[length] = (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z')
            || (value >= '0' && value <= '9') || value == '_' || value == '-' || value == '.' ? value : '_';
        length++;
    }
    state.scene[length] = 0;
    state.bootBegin = SDL_GetPerformanceCounter();
    startup_trace_emit("boot_begin", state.bootBegin);
}

struct StartupBootScope {
    unsigned mission;
    bool completed = false;
    explicit StartupBootScope(const char* scene) {
        startup_trace_begin(scene);
        mission = g_startupTrace.mission;
    }
    void complete() {
        completed = true;
        if (!g_startupTrace.active || mission != g_startupTrace.mission) return;
        g_startupTrace.bootEnd = SDL_GetPerformanceCounter();
        startup_trace_emit("boot_end", g_startupTrace.bootEnd);
    }
    ~StartupBootScope() {
        if (!completed && g_startupTrace.active && mission == g_startupTrace.mission) {
            startup_trace_emit("boot_failed", SDL_GetPerformanceCounter());
            g_startupTrace.active = false;
        }
    }
};

static void startup_trace_prepare_loading(bool deferRequired = false)
{
    const bool measured = g_startupTrace.active && !g_startupTrace.ready;
    const Uint64 started = measured ? SDL_GetPerformanceCounter() : 0;
    ebld_prepare_loading(2000.0, deferRequired);
    if (measured) {
        g_startupTrace.prepareTicks += SDL_GetPerformanceCounter() - started;
        g_startupTrace.prepareCalls++;
        startup_trace_emit("prepare_end", SDL_GetPerformanceCounter());
    }
}

static void startup_trace_prepare_required(int width, int height)
{
    const bool measured = g_startupTrace.active && !g_startupTrace.ready;
    const Uint64 started = measured ? SDL_GetPerformanceCounter() : 0;
    ebld_prepare_required_loading();
    ambience_prepare_loading(width, height);
    water_soft_prepare_loading(g_editOn);
    if (measured) {
        g_startupTrace.prepareTicks += SDL_GetPerformanceCounter() - started;
        g_startupTrace.prepareCalls++;
        startup_trace_emit("prepare_end", SDL_GetPerformanceCounter());
    }
}

static void startup_trace_frame_begin()
{
    if (g_startupTrace.active && !g_startupTrace.ready) {
        g_startupTrace.frameBegin = SDL_GetPerformanceCounter();
        g_startupTrace.drawBegin = g_startupTrace.swapBegin = g_startupTrace.swapEnd = 0;
    }
}

static void startup_trace_draw_begin()
{
    if (g_startupTrace.active && !g_startupTrace.ready)
        g_startupTrace.drawBegin = SDL_GetPerformanceCounter();
}

static void startup_trace_swap_begin()
{
    if (g_startupTrace.active && !g_startupTrace.ready)
        g_startupTrace.swapBegin = SDL_GetPerformanceCounter();
}

static void startup_trace_swap_end()
{
    if (g_startupTrace.active && !g_startupTrace.ready)
        g_startupTrace.swapEnd = SDL_GetPerformanceCounter();
}

static void startup_trace_frame_completed(SDL_Window* window, size_t objects, int engineFrame,
                                          int simTicks, int width, int height)
{
    StartupTraceState& state = g_startupTrace;
    if (!state.active || state.ready) return;
    state.completed = SDL_GetPerformanceCounter();
    Uint64 readyCounter = state.completed;
    if (objects && state.retireGpu) {
        glFinish();
        readyCounter = SDL_GetPerformanceCounter();
        state.retirementTicks = readyCounter - state.completed;
    }
    startup_trace_emit(objects ? "ready" : "empty_frame", readyCounter, objects, engineFrame,
                       simTicks, width, height, SDL_GetWindowFlags(window));
    state.ready = objects != 0;
}

static void startup_trace_shutdown()
{
    if (!g_startupTrace.active) return;
    startup_trace_emit("shutdown", SDL_GetPerformanceCounter());
    g_startupTrace.active = false;
}

#endif
