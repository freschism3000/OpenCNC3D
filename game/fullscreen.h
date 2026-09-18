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

/* THE THREE DISPLAY MODES, one function for both windows. app/cnc3d.cpp and
   cnc_eyes.cpp each create a window and the two had already drifted (one was resizable
   and the other was not); a mode switch written twice would drift the same way. So the
   flags a window is born with and the switch a live window makes are both here.

   WINDOWED     bordered, resizable by its corners, res_w x res_h if given.
   BORDERLESS   SDL_WINDOW_FULLSCREEN_DESKTOP, the desktop's own size, no mode change --
                what the game has always asked for, and what CMD+F toggles above.
   FULLSCREEN   a REAL mode change to the closest mode the display offers to res_w x
                res_h. It can leave the desktop in that mode if the program dies while it
                is on, which is the reason borderless was chosen in the first place and
                the reason it stays the default. */
enum { FS_MODE_WINDOWED = 0, FS_MODE_BORDERLESS = 1, FS_MODE_FULLSCREEN = 2 };

FS_MAYBE_UNUSED static Uint32 fs_initial_flags(int mode)
{
    if (mode == FS_MODE_FULLSCREEN) return SDL_WINDOW_FULLSCREEN | SDL_WINDOW_RESIZABLE;
    if (mode == FS_MODE_BORDERLESS) return SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_RESIZABLE;
    return SDL_WINDOW_RESIZABLE;
}

/* THE DISPLAY A WINDOW IS ON. SDL numbers displays from 0 and a window on the second
   monitor has to be asked about that monitor's modes and bounds, not the first one's.
   A hidden window in an automated run can answer -1, which is clamped to 0 so the
   readouts still describe a real display. */
FS_MAYBE_UNUSED static int fs_window_display(SDL_Window *w)
{
    int d = w ? SDL_GetWindowDisplayIndex(w) : 0;
    return d < 0 ? 0 : d;
}

/* THE ROOM A BORDERED WINDOW HAS: the desktop minus the menu bar, dock or task bar.
   SDL_GetDisplayUsableBounds is the one measure both the list and the clamp read, so an
   entry the list marks as fitting is never itself clamped. Falls back to the desktop
   mode when the bounds cannot be read, and to 0x0 when neither can. */
FS_MAYBE_UNUSED static void fs_usable_size(int display, int *uw, int *uh)
{
    SDL_Rect r;
    SDL_DisplayMode m;
    *uw = 0;
    *uh = 0;
    if (SDL_GetDisplayUsableBounds(display, &r) == 0) {
        *uw = r.w;
        *uh = r.h;
    } else if (SDL_GetDesktopDisplayMode(display, &m) == 0) {
        *uw = m.w;
        *uh = m.h;
    }
}

/* A WINDOWED SIZE IS NEVER LARGER THAN THE ROOM IT HAS TO STAND IN. A saved size larger
   than the desktop (a mode the driver lists above the panel's native size, or a size
   chosen on another display) would otherwise be handed straight to SDL, and come back
   on the next launch too, since it lives in the preset. Returns 1 when it changed
   anything, and says so on stderr so a gate can read the clamp rather than infer it. */
FS_MAYBE_UNUSED static int fs_clamp_windowed(int display, int *w, int *h)
{
    int uw, uh;
    const int aw = *w, ah = *h;
    fs_usable_size(display, &uw, &uh);
    if (uw <= 0 || uh <= 0)
        return 0;
    if (*w > uw) *w = uw;
    if (*h > uh) *h = uh;
    if (*w == aw && *h == ah)
        return 0;
    fprintf(stderr, "DISPLAY|clamped|asked=%dx%d|got=%dx%d\n", aw, ah, *w, *h);
    return 1;
}

FS_MAYBE_UNUSED static int fs_apply_mode(SDL_Window *w, int mode, int rw, int rh)
{
    const int display = fs_window_display(w);
    if (!w)
        return 0;
    if (mode == FS_MODE_BORDERLESS) {
        if (SDL_SetWindowFullscreen(w, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
            fprintf(stderr, "DISPLAY|failed|borderless|%s\n", SDL_GetError());
            return 0;
        }
    } else if (mode == FS_MODE_FULLSCREEN) {
        SDL_DisplayMode want, got;
        SDL_zero(want);
        want.w = rw;
        want.h = rh;
        SDL_SetWindowFullscreen(w, 0);
        if (rw > 0 && rh > 0 && SDL_GetClosestDisplayMode(display, &want, &got))
            SDL_SetWindowDisplayMode(w, &got);
        if (SDL_SetWindowFullscreen(w, SDL_WINDOW_FULLSCREEN) != 0) {
            fprintf(stderr, "DISPLAY|failed|fullscreen|%s\n", SDL_GetError());
            return 0;
        }
    } else {
        /* 0x0 is the dial's word for the desktop's, and under WINDOWED that is a real
           size: the usable room, which is what the Resolution list's Desktop entry
           promises. Anything larger than that room is clamped to it. */
        if (rw <= 0 || rh <= 0)
            fs_usable_size(display, &rw, &rh);
        SDL_SetWindowFullscreen(w, 0);
        SDL_SetWindowBordered(w, SDL_TRUE);
        SDL_SetWindowResizable(w, SDL_TRUE);
        if (rw > 0 && rh > 0) {
            fs_clamp_windowed(display, &rw, &rh);
            SDL_SetWindowSize(w, rw, rh);
        }
        SDL_SetWindowPosition(w, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    fprintf(stderr, "DISPLAY|mode=%d|asked=%dx%d\n", mode, rw, rh);
    return 1;
}

/* THE SIZES THE DISPLAY OFFERS, for the Resolution row.
 *
 * Entry 0 is always the desktop's own size, whether or not the driver lists it. Then
 * every distinct size at or above 640x480, largest first, refresh-rate duplicates folded
 * into one, the desktop's own pair skipped because entry 0 already is it. Nothing is
 * dropped for being larger than the desktop: a driver that upsamples (a 4K mode on a
 * 1080p panel, or a Retina display's full pixel count) lists real modes, and they are
 * real under a true fullscreen mode change. What such a size is NOT is a window that
 * fits, so each entry carries fits[], measured against the usable room; the dialog greys
 * an entry that does not fit under WINDOWED and refuses it with a reason, which is what
 * 1995 does with a gadget it will not take (drawn disabled, never removed).
 *
 * The list used to keep the first seven distinct sizes in the driver's largest-first
 * order with no desktop lookup and no bounds test, so on any display whose driver
 * advertises sizes above the panel the native size and everything under it fell off the
 * end: a 1080p display offered 3840x2160 down to 2048x1536 and nothing that fit.
 *
 * fs_pick_modes is pure (a list in, a list out) so a gate can feed it a display it does
 * not have; fs_enum_modes is the SDL wrapper over the window's own display. */
typedef struct {
    int display;          /* the SDL display index the list describes             */
    int deskw, deskh;     /* SDL_GetDesktopDisplayMode                             */
    int usablew, usableh; /* SDL_GetDisplayUsableBounds                            */
    int nmodes;           /* modes the driver listed, refresh duplicates included  */
    int ndistinct;        /* distinct sizes at or above 640x480 among them, the
                             desktop's own pair counted once when it is listed; so
                             the list holds ndistinct + (desktop_listed ? 0 : 1)
                             entries when nothing was cut by max                   */
    int desktop_listed;   /* the desktop's own pair was among them                 */
} FS_ModeInfo;

FS_MAYBE_UNUSED static int fs_pick_modes(const int *mw, const int *mh, int n,
                                         int deskw, int deskh, int usablew, int usableh,
                                         int *ws, int *hs, int *fits, int max,
                                         FS_ModeInfo *info)
{
    int i, k;
    if (info) {
        info->nmodes = n;
        info->ndistinct = 0;
        info->desktop_listed = 0;
        info->deskw = deskw;
        info->deskh = deskh;
        info->usablew = usablew;
        info->usableh = usableh;
    }
    if (max <= 0)
        return 0;
    ws[0] = deskw;
    hs[0] = deskh;
    fits[0] = 1;
    k = 1;
    for (i = 0; i < n; i++) {
        const int w = mw[i], h = mh[i];
        int j, at, dup = 0;
        if (w < 640 || h < 480)
            continue;
        if (w == deskw && h == deskh) {
            if (info && !info->desktop_listed) info->ndistinct++;
            if (info) info->desktop_listed = 1;
            continue;
        }
        for (j = 1; j < k; j++)
            if (ws[j] == w && hs[j] == h)
                dup = 1;
        if (dup)
            continue;
        if (info) info->ndistinct++;
        if (k >= max)
            continue;
        /* Largest first whatever order the driver used: behind every entry that is
           wider, or as wide and taller. */
        at = k;
        while (at > 1 && (ws[at - 1] < w || (ws[at - 1] == w && hs[at - 1] < h)))
            at--;
        for (j = k; j > at; j--) {
            ws[j] = ws[j - 1];
            hs[j] = hs[j - 1];
        }
        ws[at] = w;
        hs[at] = h;
        k++;
    }
    for (i = 1; i < k; i++)
        fits[i] = (usablew > 0 && usableh > 0 && ws[i] <= usablew && hs[i] <= usableh) ? 1 : 0;
    return k;
}

FS_MAYBE_UNUSED static int fs_enum_modes(SDL_Window *win, int *ws, int *hs, int *fits,
                                         int max, FS_ModeInfo *info)
{
    enum { FS_RAW_MAX = 256 };
    int mw[FS_RAW_MAX], mh[FS_RAW_MAX];
    const int display = fs_window_display(win);
    const int n = SDL_GetNumDisplayModes(display);
    SDL_DisplayMode desk;
    int i, raw = 0, deskw = 0, deskh = 0, uw, uh, k;
    if (SDL_GetDesktopDisplayMode(display, &desk) == 0) {
        deskw = desk.w;
        deskh = desk.h;
    }
    fs_usable_size(display, &uw, &uh);
    for (i = 0; i < n && raw < FS_RAW_MAX; i++) {
        SDL_DisplayMode m;
        if (SDL_GetDisplayMode(display, i, &m) != 0)
            continue;
        mw[raw] = m.w;
        mh[raw] = m.h;
        raw++;
    }
    if (deskw <= 0 || deskh <= 0) {
        /* No desktop mode to lead with: the largest listed size stands in, or nothing. */
        if (raw <= 0)
            return 0;
        deskw = mw[0];
        deskh = mh[0];
        for (i = 1; i < raw; i++)
            if (mw[i] > deskw || (mw[i] == deskw && mh[i] > deskh)) { deskw = mw[i]; deskh = mh[i]; }
    }
    k = fs_pick_modes(mw, mh, raw, deskw, deskh, uw, uh, ws, hs, fits, max, info);
    if (info)
        info->display = display;
    return k;
}

#endif /* CNC3D_FULLSCREEN_H */
