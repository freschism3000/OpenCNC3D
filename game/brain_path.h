/* game/brain_path.h -- WHERE THE BRAIN IS, ANSWERED IN ONE PLACE.
 *
 * There used to be two answers to this question and they did not agree, which is a
 * whole class of bug rather than one bug. The renderer's answer (cnc_eyes.cpp) knew
 * about Windows, about the shipped layout where the library sits beside the
 * executable, and about SDL_GetBasePath. The LOBBY's answer (app/cnc3d.cpp,
 * mp_brain_abi) knew none of those: it probed three hardcoded ".dylib" names with
 * fopen and gave up.
 *
 * WHAT THAT COST, and it is worth writing down because nothing in the code said it.
 * The lobby's probe reads CNC3D_Event_ABI out of the brain and sends it in the HELLO;
 * the host refuses any joiner whose value differs (netmatch.c, reason 2, "order-wire
 * layout"). On Windows all three fopen candidates missed, so the probe returned 0 and
 * the joiner announced 0. Against a Mac host holding a real value that is a mismatch,
 * so a WINDOWS PLAYER COULD NOT JOIN A MAC HOST AT ALL, and was told it was an
 * engine-compatibility problem -- when the two brains are in fact identical, proven
 * over twenty thousand ticks. Windows against Windows was the mirror of it: both sides
 * 0, so they matched, and the check that exists to stop two different builds playing
 * together was switched off precisely where nobody could see it.
 *
 * So the list lives here now and both callers include it. A third caller cannot drift
 * from the other two, because there is nothing left to drift from.
 *
 * ORDER MATTERS AND THE REPO PATHS STAY FIRST. Probing beside the binary first would
 * change which brain the Mac gates load: both game/ and playable/ have a
 * TiberianDawn.dylib sitting in them, so a developer who had just rebuilt
 * brain/vanilla would silently get whichever stale copy was nearest instead of the one
 * just built. Beside-the-binary is the last resort, not the first.
 *
 * SDL_GetBasePath rather than the bare filename, because it answers "beside the
 * executable" even when the working directory is somewhere else -- which is what
 * happens when a build is started from a shortcut rather than from its own folder. The
 * bare name is tried too, for the case where the base path cannot be determined.
 */
#ifndef CNC3D_BRAIN_PATH_H
#define CNC3D_BRAIN_PATH_H

#include <SDL.h>
#include <stdio.h>
#include <unistd.h>

/* The platform's own name for the brain. Everything that is not an explicit --dylib is
   derived from this, so a shipped build never has to be told where its own brain is. */
#ifdef _WIN32
#  define CNC3D_BRAIN_LIB "TiberianDawn.dll"
#else
#  define CNC3D_BRAIN_LIB "TiberianDawn.dylib"
#endif

/* The brain lives in one place in the working tree, another in the installed
   share/OpenCNC 3D tree, and a third beside the binary in a shipped build. Probe all of
   them rather than making the caller care.

   NEVER RETURNS NULL. With nothing found it names the platform's own library, so the
   error a player sees is about a file that could plausibly have existed rather than
   about "(none)". */
static const char* cnc3d_find_brain(const char* explicitPath)
{
    static const char* candidates[] = {
        "../brain/lib/" CNC3D_BRAIN_LIB,
        "../brain/vanilla/build-native/tiberiandawn/" CNC3D_BRAIN_LIB,
        CNC3D_BRAIN_LIB,
        NULL
    };
    static char beside[1024];
    int i;

    if (explicitPath && *explicitPath)
        return explicitPath;
    for (i = 0; candidates[i]; i++)
        if (access(candidates[i], R_OK) == 0)
            return candidates[i];

    {
        char* base = SDL_GetBasePath();
        if (base) {
            snprintf(beside, sizeof beside, "%s%s", base, CNC3D_BRAIN_LIB);
            SDL_free(base);
            if (access(beside, R_OK) == 0)
                return beside;
        }
    }

    return CNC3D_BRAIN_LIB;
}

#endif /* CNC3D_BRAIN_PATH_H */
