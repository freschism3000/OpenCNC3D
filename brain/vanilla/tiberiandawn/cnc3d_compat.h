//
// CNC3D — tiny Win32 compatibility shim so the CNC_* game-logic API builds off Windows.
//
// After supplying a null keyboard and enabling the Microsoft language extensions
// (-fms-extensions -fdeclspec), dllinterface.cpp needed only three Win32 conveniences.
// None of them touch game logic, so shimming them keeps the engine sources byte-identical
// and still able to cross-compile for the Win98 target later (where the real ones exist).
//
#ifndef CNC3D_COMPAT_H
#define CNC3D_COMPAT_H

#if !defined(_WIN32)

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>

#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(p) ((void)(p))
#endif

// winmm's millisecond tick. The engine uses it to seed RNG and to time frames; a
// monotonic clock is the faithful equivalent.
static inline unsigned int timeGetTime(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned int)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

// MSVC's non-standard itoa. Only base 10 is used by the callers here, but honour the
// documented signature so behaviour does not silently differ.
static inline char* itoa(int value, char* str, int base)
{
    if (base == 10) {
        sprintf(str, "%d", value);
    } else if (base == 16) {
        sprintf(str, "%x", value);
    } else {
        // Generic fallback for the bases MSVC would accept.
        char buf[64];
        int i = 0;
        unsigned int v = (value < 0 && base == 10) ? (unsigned int)(-value) : (unsigned int)value;
        if (v == 0) {
            buf[i++] = '0';
        }
        while (v) {
            int d = v % base;
            buf[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
            v /= base;
        }
        int n = 0;
        while (i > 0) {
            str[n++] = buf[--i];
        }
        str[n] = '\0';
    }
    return str;
}


// ---- shims used by startup.cpp -------------------------------------------------------
// startup.cpp builds argc/argv for the DLL and resolves its own module path so the engine
// can find its font files. Off Windows there is no HINSTANCE and no MessageBox; the host
// sets the content directory explicitly anyway (CNC_Init / Set_Content_Directory).

typedef void* HINSTANCE;
typedef unsigned long DWORD;

#ifndef MB_OK
#define MB_OK               0x0
#define MB_ICONEXCLAMATION  0x30
#endif

// A single process-wide "module handle". Nothing dereferences it off Windows.
static HINSTANCE ProgramInstance = 0;

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

// Return the running executable's path. On Windows this is the DLL's own path; the value
// is only used as argv[0] and to derive a resource directory, so the executable path is
// the faithful stand-in for a statically linked headless build.
static inline DWORD GetModuleFileNameA(HINSTANCE, char* out, DWORD size)
{
    if (out == 0 || size == 0) {
        return 0;
    }
    out[0] = '\0';
#if defined(__APPLE__)
    uint32_t sz = (uint32_t)size;
    if (_NSGetExecutablePath(out, &sz) != 0) {
        return size; // truncated -- caller treats >= size-1 as an error
    }
#else
    ssize_t n = readlink("/proc/self/exe", out, (size_t)size - 1);
    if (n <= 0) {
        return 0;
    }
    out[n] = '\0';
#endif
    return (DWORD)strlen(out);
}

// The engine only ever reports fatal startup problems this way.
static inline int MessageBoxA(void*, const char* text, const char* caption, unsigned int)
{
    fprintf(stderr, "[%s] %s\n", caption ? caption : "Command & Conquer", text ? text : "");
    return 1;
}

#endif // !_WIN32
#endif // CNC3D_COMPAT_H
