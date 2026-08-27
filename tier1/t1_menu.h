/*
 * t1_menu.h -- the 1995 MS-DOS main menu, on the Voodoo.
 *
 * Menus were asked for. The game booted straight into SCG01EA: there was no title
 * screen, no way to choose anything, and no way out except closing the process.
 *
 * NOTHING HERE DRAWS THE MENU. menu/dosmenu.c already does, it is pure C89 built on the
 * same game/dosbar.c primitives this build already compiles for the sidebar, and it is
 * taken UNEDITED exactly as game/dosbar.c, game/hud640.c and game/efx_recipes.h are. It
 * renders into an 8-bit 320x200 surface and hit-tests it; it owns no loop, no clock and
 * no input. This file is the other half: the surface onto the card, and the pointer and
 * the click back into it.
 *
 * THE SURFACE IS 320 WIDE AND THE CARD'S LIMIT IS 256, so it goes up as two pages -- a
 * 256x256 and a 64x256 -- and comes back as two quads that meet on an exact texel
 * boundary. That is the same split the 640x480 HUD already uses for its 160x480 bar, for
 * the same reason.
 *
 * It is presented at 2x into the middle of the 640x480 screen, which is 640x400 with a
 * 40-row letterbox above and below. Not stretched to 480: the art is 320x200 and a
 * non-integer scale on a point-sampled 1995 picture reads as a smeared one.
 */

#ifndef T1_MENU_H
#define T1_MENU_H

#include "softras.h"
#include "dosbar.h"
#include "dosmenu.h"

typedef struct
{
    DB_Pack     *pack;              /* dosmenu.pack: the title plate, the fonts */
    DB_Surface   surf;
    unsigned char *px;              /* 320x200 8-bit, the module's target */
    DM_State     st;

    unsigned char pageA[256 * 256];
    unsigned char pageB[64 * 256];
    SR_Texture   texA, texB;
    int          ok, uploaded, dirty;

    int          hover, chosen;     /* DM_* item under the pointer / just clicked */
    long         clicks;
} T1_Menu;

/* Opens dosmenu.pack. 0 and a reason if it is not there, in which case the caller should
 * boot straight into the mission the way this build always has. */
int  t1_menu_load(T1_Menu *m, const char *packpath, char *err, int errlen);
void t1_menu_free(T1_Menu *m);

/* Uploads the two pages. Call once the Glide context is open. */
int  t1_menu_upload(T1_Menu *m, char *err, int errlen);

/* One frame of menu. Returns the DM_ item the player just committed to, or -1.
 * `keydelta` walks the selection (-1 up, +1 down) and `enter` commits it, so the menu is
 * drivable from the keyboard exactly as the DOS one is. */
int  t1_menu_step(T1_Menu *m, int mx, int my, int lb, int waslb, int keydelta, int enter);

/* Draws it: the two pages, then the DOS pointer at its own hotspot. */
void t1_menu_draw(T1_Menu *m, int mx, int my);

#endif /* T1_MENU_H */
