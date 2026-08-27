/* t1_menu.c -- see t1_menu.h. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t1_menu.h"
#include "t1_glide.h"

#define MENU_W 320
#define MENU_H 200
#define MENU_SCALE 2
/* 320x200 at 2x is 640x400 in a 640x480 window, so it sits in the middle with a 40-row
 * letterbox. Deliberately not stretched to 480: a non-integer scale on a point-sampled
 * 1995 picture reads as a smeared one. */
#define MENU_TOP  ((480 - MENU_H * MENU_SCALE) / 2)

int t1_menu_load(T1_Menu *m, const char *packpath, char *err, int errlen)
{
    memset(m, 0, sizeof *m);
    m->pack = db_pack_load(packpath, err, errlen);
    if (!m->pack) return 0;
    m->px = (unsigned char *)malloc(MENU_W * MENU_H);
    if (!m->px) { _snprintf(err, errlen, "out of memory for the menu surface"); return 0; }
    db_surface_init(&m->surf, MENU_W, MENU_H, m->px);
    dm_state_init(&m->st);
    m->st.selected = dm_first_enabled();
    m->hover = -1;
    m->chosen = -1;
    m->dirty = 1;
    m->ok = 1;
    return 1;
}

void t1_menu_free(T1_Menu *m)
{
    if (m->px) free(m->px);
    /* The pack is the loader's; db_pack_load has no free in this build's header, and the
     * menu lives for the whole process anyway. */
    memset(m, 0, sizeof *m);
}

/* The 320-wide surface into the two pages. Index 0 is the pack's own colour and is NOT a
 * hole here: the menu is opaque and the chroma key is off for the whole pass. */
static void compose(T1_Menu *m)
{
    int y, x;
    dm_draw_menu(&m->surf, m->pack, &m->st);
    memset(m->pageA, 0, sizeof m->pageA);
    memset(m->pageB, 0, sizeof m->pageB);
    for (y = 0; y < MENU_H; ++y)
    {
        for (x = 0; x < 256; ++x)  m->pageA[y * 256 + x] = m->px[y * MENU_W + x];
        for (x = 0; x < 64; ++x)   m->pageB[y * 64 + x]  = m->px[y * MENU_W + 256 + x];
    }
    m->dirty = 0;
    if (m->uploaded)
    {
        t1_glide_reupload(&m->texA);
        t1_glide_reupload(&m->texB);
    }
}

int t1_menu_upload(T1_Menu *m, char *err, int errlen)
{
    if (!m->ok) return 0;
    compose(m);
    sr_texture(&m->texA, m->pageA, 256, 256);
    sr_texture(&m->texB, m->pageB, 64, 256);
    if (!t1_glide_upload(&m->texA, err, errlen)) return 0;
    if (!t1_glide_upload(&m->texB, err, errlen)) return 0;
    m->uploaded = 1;
    return 1;
}

int t1_menu_step(T1_Menu *m, int mx, int my, int lb, int waslb, int keydelta, int enter)
{
    int dx, dy, hit, out = -1;
    if (!m->ok) return -1;

    /* Screen pixels back into the module's own 320x200 space. The presentation is one
     * integer scale and one offset, so the inverse is exact and needs no fudge. */
    dx = mx / MENU_SCALE;
    dy = (my - MENU_TOP) / MENU_SCALE;
    hit = (dy >= 0 && dy < MENU_H) ? dm_hit_test(&m->st, dx, dy) : -1;

    if (keydelta)
    {
        int nx = dm_next_item(&m->st, m->st.selected, keydelta);
        if (nx != m->st.selected) { m->st.selected = nx; m->dirty = 1; }
    }
    if (hit != m->hover) { m->hover = hit; }

    /* The engine lights the button under the pointer as the SELECTED one, so moving the
     * mouse moves the keyboard selection with it, which is what a 1995 dialog does. */
    if (hit >= 0 && hit != m->st.selected) { m->st.selected = hit; m->dirty = 1; }

    if (lb && !waslb && hit >= 0) { m->st.pressed = hit; m->dirty = 1; }
    if (!lb && waslb)
    {
        if (m->st.pressed >= 0 && m->st.pressed == hit) { out = hit; ++m->clicks; }
        if (m->st.pressed >= 0) { m->st.pressed = -1; m->dirty = 1; }
    }
    if (enter && m->st.selected >= 0 && !dm_item_disabled(m->st.selected))
    { out = m->st.selected; ++m->clicks; }

    if (m->dirty) compose(m);
    m->chosen = out;
    return out;
}

void t1_menu_draw(T1_Menu *m, int mx, int my)
{
    if (!m->ok || !m->uploaded) return;
    /* THE MENU'S OWN PALETTE, which is TITLE.CPS's and not the mission's. The card holds
     * one palette at a time, so it is set here every frame rather than once: the world
     * passes set theirs and the menu is not always the only thing that has drawn. */
    t1_glide_palette(m->pack->pal8);
    t1_glide_ckey(0, 0);
    t1_glide_filter(0);
    t1_glide_depth(0);
    t1_glide_quad(0.0f, (float)MENU_TOP,
                  (float)(256 * MENU_SCALE), (float)(MENU_TOP + MENU_H * MENU_SCALE),
                  0.0f, 0.0f, 256.0f, (float)MENU_H, &m->texA, 1.0f);
    t1_glide_quad((float)(256 * MENU_SCALE), (float)MENU_TOP,
                  (float)(MENU_W * MENU_SCALE), (float)(MENU_TOP + MENU_H * MENU_SCALE),
                  0.0f, 0.0f, 64.0f, (float)MENU_H, &m->texB, 1.0f);
    /* THE POINTER IS THE CALLER'S. The HUD's aux page already carries MOUSE.SHP as true
     * colour, so it survives the menu owning the card's one palette; drawing it here
     * would mean a third texture, in the menu's palette, for one 30x24 sprite. */
    (void)mx; (void)my;
    t1_glide_depth(1);
}
