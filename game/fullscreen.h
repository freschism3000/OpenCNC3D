/* ====================================================================================
 *  fullscreen.h -- one keystroke, every screen, one line per event loop.
 *
 *      CMD + F        on the Mac
 *      ALT + ENTER    on Windows
 *
 *  Both are accepted on both platforms. They are two conventions for one thing and
 *  refusing the other one buys nobody anything.
 *
 *  WHY IT TAKES NO WINDOW POINTER, which is the whole reason this is three lines
 *  rather than a refactor. This program has FIVE separate event loops -- the tactical
 *  view, the DOS main menu, the menu's fade loop, the campaign screens and the movie
 *  player -- and only two of them are handed the SDL_Window. A keyboard event already
 *  carries the id of the window that had focus, so SDL_GetWindowFromID answers the
 *  question from the event itself and every loop can call this with the event it is
 *  already holding.
 *
 *  SDL_WINDOW_FULLSCREEN_DESKTOP, not SDL_WINDOW_FULLSCREEN: it borrows the desktop's
 *  own resolution instead of asking the display to change mode. That is what "scale the
 *  window up" means, it toggles instantly, and it cannot leave the screen in a bad mode
 *  if the program dies while it is on.
 *
 *  Everything downstream already copes. The renderer reads SDL_GL_GetDrawableSize every
 *  frame, the camera clamp and the mouse scale are computed from it, the DOS sidebar
 *  picks its own whole-number magnification from the height, and the Tier 2 render
 *  targets are resized by fx_rt_init when the size changes. Nothing had to be told.
 * ==================================================================================== */
#ifndef CNC3D_FULLSCREEN_H
#define CNC3D_FULLSCREEN_H

#include <SDL.h>
#include <stdio.h>

#if defined(__GNUC__) || defined(__clang__)
#define FS_MAYBE_UNUSED __attribute__((unused))
#else
#define FS_MAYBE_UNUSED
#endif

/* Set by --fullscreen, read by whoever creates the window. */
static int fs_start_fullscreen FS_MAYBE_UNUSED = 0;

/* Hand it every event. Returns 1 if it was the fullscreen key and it has been dealt
   with, so the caller can stop looking at it; 0 for everything else. */
FS_MAYBE_UNUSED static int fs_handle_event(const SDL_Event *e)
{
    SDL_Window *w;
    Uint32 flags;
    int on;
    SDL_Keymod m;
    SDL_Keycode k;

    if (!e || e->type != SDL_KEYDOWN)
        return 0;

    m = (SDL_Keymod)e->key.keysym.mod;
    k = e->key.keysym.sym;
    if (!(((m & KMOD_GUI) && k == SDLK_f) ||
          ((m & KMOD_ALT) && (k == SDLK_RETURN || k == SDLK_KP_ENTER))))
        return 0;

    /* Held down, the key repeats several times a second, and each repeat would be
       another mode change. Swallow the repeats rather than flicker. */
    if (e->key.repeat)
        return 1;

    w = SDL_GetWindowFromID(e->key.windowID);
    if (!w)
        return 1;

    flags = SDL_GetWindowFlags(w);
    on = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : 1;
    if (SDL_SetWindowFullscreen(w, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
        fprintf(stderr, "FULLSCREEN|failed|%s\n", SDL_GetError());
        return 1;
    }
    fprintf(stderr, "FULLSCREEN|%s\n", on ? "on" : "off");
    return 1;
}

#endif /* CNC3D_FULLSCREEN_H */
