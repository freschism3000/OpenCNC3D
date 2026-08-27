/*
 * t1_glide_compat.h -- include this INSTEAD of <glide.h>.
 *
 * 3dfx.h predates GCC on Windows. Its compiler ladder ends in
 *
 *     #warning define FX_ENTRY & FX_CALL for your compiler
 *     #define FX_ENTRY extern
 *     #define FX_CALL
 *
 * and mingw falls straight into it, so every Glide entry point gets declared CDECL.
 * The driver exports them STDCALL: the real names in glide2x.dll are decorated
 * `_grDrawTriangle@12` and so on. A cdecl declaration against a stdcall export means the
 * callee pops the arguments and so does the caller, and the stack unwinds twice per call.
 *
 * It does not fail to link, because dlltool built the import library from those same
 * decorated names, so the symbols resolve. It fails at RUN TIME, on the real card, some
 * calls in, with the stack quietly eaten. That is the worst possible failure mode on a
 * machine whose screen output cannot even be seen over VNC, so the convention is pinned
 * here rather than discovered there.
 */

#ifndef T1_GLIDE_COMPAT_H
#define T1_GLIDE_COMPAT_H

/* Pre-defining the macros does not work: 3dfx.h's final else branch defines FX_CALL
 * unconditionally and simply wins. So include 3dfx.h FIRST, let it be wrong, correct it,
 * and only then include glide.h. glide.h includes 3dfx.h itself, but its include guard
 * makes that a no-op, so the corrected macros are the ones every prototype is built with. */
#include <3dfx.h>

#if defined(__GNUC__) && (defined(_WIN32) || defined(__WIN32__))
#  undef  FX_ENTRY
#  define FX_ENTRY extern
#  undef  FX_CALL
#  define FX_CALL __stdcall
#endif

#include <glide.h>

#endif /* T1_GLIDE_COMPAT_H */
