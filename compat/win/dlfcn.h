/* compat/win/dlfcn.h
 *
 * POSIX dynamic loading, expressed in the Win32 calls that do the same job, so that
 * cnc_eyes.cpp's brain loader compiles for Windows without being edited.
 *
 * The mapping itself is not new: brain/host/cnc_host.c has carried the same one since
 * the host tool was written. This header is that mapping made reusable.
 *
 * See compat/win/README.md, and BUILDING.md for what should replace it.
 */
#ifndef CNC3D_COMPAT_DLFCN_H
#define CNC3D_COMPAT_DLFCN_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#include <stdio.h>
#include <string.h>

/* The flags exist so the call sites compile. Windows has no equivalent knobs:
   LoadLibrary always resolves eagerly, and there is no RTLD_GLOBAL notion because
   symbols are never merged into a process-wide namespace in the first place. */
#define RTLD_NOW    0
#define RTLD_LAZY   0
#define RTLD_LOCAL  0
#define RTLD_GLOBAL 0

static char cnc3d_dl_err[512];

/* Loading the brain: TiberianDawn.dylib does not exist on Windows, and find_brain() in
   cnc_eyes.cpp hardcodes that extension in both of its candidate paths. Rather than edit
   that file (see README.md), retry the same path with .dll and announce the substitution
   once, so nobody debugging a load failure has to guess that a rename happened. */
static void* cnc3d_dlopen(const char* path, int flags)
{
    HMODULE h;
    char alt[1024];
    int tried_alt = 0;
    (void)flags;
    if (!path) return NULL;

    h = LoadLibraryA(path);
    if (!h) {
        size_t n = strlen(path);
        if (n > 6 && strcmp(path + n - 6, ".dylib") == 0) {
            static int told = 0;
            if (n - 6 + 5 < sizeof(alt)) {
                memcpy(alt, path, n - 6);
                memcpy(alt + n - 6, ".dll", 5);
                tried_alt = 1;
                h = LoadLibraryA(alt);
                if (h && !told) {
                    told = 1;
                    fprintf(stderr, "[win] brain requested as %s, loaded %s\n", path, alt);
                }
            }
        }
    }
    /* Name BOTH paths when the retry happened. The first version of this printed only
       the requested path, so a failure read as "LoadLibrary(.../TiberianDawn.dylib)
       failed" on a machine that has no .dylib anywhere and never would, which says
       nothing about the .dll that was actually looked for and not found.

       GetLastError 126 is ERROR_MOD_NOT_FOUND and is worth spelling out, because it
       means either the library or ONE OF ITS OWN DEPENDENCIES is missing, and those two
       cases look identical from here. */
    if (!h) {
        DWORD e = GetLastError();
        snprintf(cnc3d_dl_err, sizeof cnc3d_dl_err,
                 "LoadLibrary(%s%s%s) failed, GetLastError=%lu%s",
                 path, tried_alt ? " and " : "", tried_alt ? alt : "",
                 (unsigned long)e,
                 e == 126 ? " (ERROR_MOD_NOT_FOUND: the file itself, or a DLL it depends"
                            " on, is not where the loader looked)" : "");
    }
    return (void*)h;
}

static void* cnc3d_dlsym(void* handle, const char* name)
{
    void* p = (void*)GetProcAddress((HMODULE)handle, name);
    if (!p)
        snprintf(cnc3d_dl_err, sizeof cnc3d_dl_err,
                 "GetProcAddress(%s) failed, GetLastError=%lu",
                 name, (unsigned long)GetLastError());
    return p;
}

static int cnc3d_dlclose(void* handle)
{
    return FreeLibrary((HMODULE)handle) ? 0 : -1;
}

static const char* cnc3d_dlerror(void)
{
    return cnc3d_dl_err[0] ? cnc3d_dl_err : NULL;
}

#define dlopen(p, f) cnc3d_dlopen((p), (f))
#define dlsym(h, n)  cnc3d_dlsym((h), (n))
#define dlclose(h)   cnc3d_dlclose(h)
#define dlerror()    cnc3d_dlerror()

#endif
