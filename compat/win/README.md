# compat/win: the Windows shim that lets the Mac sources compile unchanged

This directory exists so that **not one line of `game/cnc_eyes.cpp` had to change** to
produce a Windows binary. It is put on the include path ahead of everything else by
`tools/win/build-win.sh`, and it satisfies the three macOS-only things that file asks for
by name.

Every other source in the project (`app/cnc3d.cpp`, `menu/dosmenu_shell.h`,
`video/movieplay.c`, `video/playvqa.c`, `app/campaign.c`) already carries its own
`#ifdef __APPLE__` GL guard and needs nothing from here. The renderer is the one file
that does not, and it is also the file most likely to have work in flight in it,
so a shim beats a patch.

| File | Stands in for | Why it is honest |
|---|---|---|
| `OpenGL/gl.h` | Apple's framework header | Includes `<GL/gl.h>`. Same fixed-function GL 1.x either way; this is a spelling difference, not a behaviour one. |
| `dlfcn.h` | POSIX dynamic loading | Maps `dlopen`/`dlsym`/`dlclose`/`dlerror` onto `LoadLibraryA`/`GetProcAddress`/`FreeLibrary`/`GetLastError`. The project already has this mapping, hand-written, in `brain/host/cnc_host.c`; this is the same one, reusable. |

## The one piece of behaviour here that is not a pure rename

`dlopen` in this shim retries a `.dylib` path as `.dll`.

`find_brain()` in `cnc_eyes.cpp` has two hardcoded candidate paths and both end in
`.dylib`, so on Windows the brain would never be found. Rather than edit that function
and disturb the renderer, the shim performs the substitution and says so
on stderr the first time it does it.

**This is a temporary measure and it is written down as one.** When `cnc_eyes.cpp` is free
to edit, `find_brain()` should learn the platform's own library extension and this retry
should be deleted. See `BUILDING.md`.
