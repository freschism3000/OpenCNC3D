/*
 * dosopt.c -- the implementation of dosopt.h.
 *
 * Every layout number, colour and behaviour here is quoted in dosopt.h against its
 * line in the GPL Tiberian Dawn tree. This file is the transliteration; read the
 * header first, because the header is where the evidence is.
 *
 * The drawing is dosbar.c's, the same primitives the sidebar and the main menu use,
 * so this screen is one 8-bit surface, one texture upload and one quad. Tier 1 gap:
 * none. Nothing here knows about SDL, GL, the mixer or a clock.
 */

#include "dosopt.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------ *
 * Labels. Read out of the 1995 CONQUER.ENG in LOCAL.MIX, not typed from memory;
 * dosopt.h lists the conquer.h index of each one.
 * ------------------------------------------------------------------------ */

static const struct {
    const char *label;
    int disabled; /* drawn, never clickable: gadget.cpp:632 */
} dopt_items[DOPT_ITEM_COUNT] = {
    /* LIVE since the save layer landed. The engine has always been able to save -- the
       brain exports CNC_Save_Load -- and the renderer simply never called it. */
    {"Load Mission", 0},
    {"Save Mission", 0},
    {"Delete Mission", 1},  /* wants the slot dialog; left open deliberately */
    {"Game Controls", 0},
    {"Visuals", 0},        /* ours: the desktop presentation chain */
    {"Abort Mission", 0},
    {"Exit Game", 0},
    {"Resume Mission", 0},
    {"Restate", 1}         /* needs the mission briefing text, not loaded yet */
};

/* The Visuals pages. Every label is ours; there is no 1995 string for any of them. */
static const char *const dopt_vis_label[DOPT_V_COUNT] = {
    "Classic", "Enhanced", "Advanced...", "OK"
};

static const char *const dopt_vis_elem[DOPT_VE_COUNT] = {
    "Smooth animations",
    "New HUD",
    "Bilinear filtering",
    "Console output gamma",
    "Supersampling",
    "Sun shadows",
    "Ambient occlusion",
    "Dynamic light",
    "Bloom",
    "Colour grade",
    "CRT"
};

const char *dopt_vis_elem_label(int elem)
{
    return (elem >= 0 && elem < DOPT_VE_COUNT) ? dopt_vis_elem[elem] : "";
}

static const char *const dopt_ctrl_label[DOPT_C_COUNT] = {
    "Game Speed:", "Scroll Rate:", "Music Volume:", "Sound Volume:", "Speech Volume:",
    "Sound Controls", /* TXT_SOUND_CONTROLS conquer.h:198 */
    "Options Menu"
};

/* The jukebox page's own labels. TXT_SOUND_CONTROLS is the caption; the two toggles
 * print TXT_ON / TXT_OFF (conquer.h:200/201) rather than a label of their own. */
/* STOP and PLAY are drawn as glyphs, not text (see dopt_draw_shapebtn), but they still
 * carry a name: dopt_item_label is what the harness's `optclick LABEL` looks up, so a
 * nameless button is a button no gate can press. */
static const char *const dopt_snd_label[DOPT_S_COUNT] = {
    "Music Volume:", "Sound Volume:", "Track List", "Stop", "Play",
    "Off", "Off", "Options Menu"
};

/* ONE PLACE THAT KNOWS HOW LONG EACH PAGE IS. Every page test in this file used to be a
   binary `page == CONTROLS ? ... : <options>`, which meant a new page silently behaved
   like the Options page in a dozen places at once. With four pages that is no longer a
   safe default, so the count, the labels, the disabled rule and the rectangles each
   switch on the page explicitly and fall through to nothing rather than to page 0. */
int dopt_page_count(const DOPT_State *st)
{
    switch (st->page) {
    case DOPT_PAGE_CONTROLS: return DOPT_C_COUNT;
    case DOPT_PAGE_VISUALS:  return DOPT_V_COUNT;
    case DOPT_PAGE_ADVANCED: return DOPT_A_COUNT;
    case DOPT_PAGE_SOUND:    return DOPT_S_COUNT;
    case DOPT_PAGE_CHEATS:   return DOPT_CH_COUNT;
    case DOPT_PAGE_CONFIRM:  return DOPT_CF_COUNT;
    default:                 return DOPT_ITEM_COUNT;
    }
}

const char *dopt_item_label(const DOPT_State *st, int item)
{
    switch (st->page) {
    case DOPT_PAGE_CONTROLS:
        return (item >= 0 && item < DOPT_C_COUNT) ? dopt_ctrl_label[item] : "";
    case DOPT_PAGE_VISUALS:
        return (item >= 0 && item < DOPT_V_COUNT) ? dopt_vis_label[item] : "";
    case DOPT_PAGE_ADVANCED:
        if (item == DOPT_A_OK) return "OK";
        return dopt_vis_elem_label(item);
    case DOPT_PAGE_CHEATS:
        return dopt_cheat_label(item);
    case DOPT_PAGE_CONFIRM:
        if (item == DOPT_CF_ABORT)   return DOPT_CF_ABORT_S;
        if (item == DOPT_CF_RESTART) return DOPT_CF_RESTART_S;
        if (item == DOPT_CF_CANCEL)  return DOPT_CF_CANCEL_S;
        return "";
    case DOPT_PAGE_SOUND:
        if (item == DOPT_S_SHUFFLE) return st->shuffle ? "On" : "Off";
        if (item == DOPT_S_REPEAT)  return st->repeat  ? "On" : "Off";
        return (item >= 0 && item < DOPT_S_COUNT) ? dopt_snd_label[item] : "";
    default:
        return (item >= 0 && item < DOPT_ITEM_COUNT) ? dopt_items[item].label : "";
    }
}

int dopt_item_disabled(const DOPT_State *st, int item)
{
    switch (st->page) {
    case DOPT_PAGE_CONTROLS:
        return 0;
    case DOPT_PAGE_VISUALS:
        /* THE ONE RULE ON THIS PAGE: there is nothing to configure about a picture you
           have switched off, so ADVANCED greys out under CLASSIC. It is drawn either
           way -- 1995 draws a disabled gadget rather than hiding it (gadget.cpp:632) --
           so the player can see that turning ENHANCED on is what opens it. */
        if (item == DOPT_V_ADVANCED) return !st->vis.enhanced;
        return 0;
    case DOPT_PAGE_ADVANCED:
        return 0;
    case DOPT_PAGE_CHEATS:
        return 0;
    case DOPT_PAGE_CONFIRM:
        return 0;
    case DOPT_PAGE_SOUND:
        /* Play does nothing with an empty list, and 1995 draws a dead gadget rather
           than hiding it (gadget.cpp:632). Same rule here. */
        if (item == DOPT_S_PLAY || item == DOPT_S_LIST)
            return st->ntracks <= 0;
        return 0;
    default:
        return (item >= 0 && item < DOPT_ITEM_COUNT) ? dopt_items[item].disabled : 1;
    }
}

/* ------------------------------------------------------------------------ *
 * Layout.
 * ------------------------------------------------------------------------ */

void dopt_settings_init(DOPT_Settings *s)
{
    /* options.cpp:73-79 OptionsClass::OptionsClass. GameSpeed 3, ScrollRate 3, both
     * volumes 0xFF scaled: the engine stores them as a fixed point 0..255. */
    s->speed = 3;
    s->scrollrate = 3;
    s->music = DOPT_VOL_TOP;
    s->sound = DOPT_VOL_TOP;
    s->speech = DOPT_VOL_TOP;
}

void dopt_layout(DOPT_State *st, const DB_Pack *p)
{
    const DB_Font *f = p ? db_font(p, "GRAD6FNT") : 0;
    int i, w, maxw = 0;

    if (!f) {
        st->btnw = DOPT_MIN_BTN_W;
        st->okw = DOPT_MIN_BTN_W;
        st->chresetw = DOPT_MIN_BTN_W;
        st->laidout = 1;
        return;
    }

    /* goptions.cpp:137-159: every stacked button is as wide as the widest label in
     * the list, floored at 90. textbtn.cpp sizes a button to its text plus 8. */
    for (i = 0; i < DOPT_ITEM_COUNT; i++) {
        w = db_string_width(f, dopt_items[i].label, DB_FONT6_XSPACING) + 8;
        if (w > maxw)
            maxw = w;
    }
    st->btnw = maxw > DOPT_MIN_BTN_W ? maxw : DOPT_MIN_BTN_W;
    st->okw = db_string_width(f, dopt_ctrl_label[DOPT_C_OK], DB_FONT6_XSPACING) + 8;
    if (st->okw < 40)
        st->okw = 40;
    st->chresetw = db_string_width(f, dopt_cheat_label(DOPT_CH_RESET),
                                   DB_FONT6_XSPACING) + 8;
    if (st->chresetw < DOPT_MIN_BTN_W)
        st->chresetw = DOPT_MIN_BTN_W;

    /* THE CONFIRMATION BOX, measured exactly as msgbox.cpp measures it rather than
       written down as a literal, so it tracks the font the way the real one does.
       msgbox.cpp:157-159 is width = MAX(textwidth,50)+40 and height = textheight+60,
       centred; :123 floors a button at 30; textbtn.cpp:74-84 sizes one to its text + 8.
       Abort and Cancel share the wider of the two, which is msgbox's `bwidth`. */
    {
        int mw = db_string_width(f, DOPT_CF_MSG, DB_FONT6_XSPACING);
        int aw = db_string_width(f, DOPT_CF_ABORT_S, DB_FONT6_XSPACING) + 8;
        int cw = db_string_width(f, DOPT_CF_CANCEL_S, DB_FONT6_XSPACING) + 8;
        if (aw < DOPT_CF_BTN_MIN) aw = DOPT_CF_BTN_MIN;
        if (cw < DOPT_CF_BTN_MIN) cw = DOPT_CF_BTN_MIN;
        st->cfbw = cw > aw ? cw : aw;
        st->cf3w = db_string_width(f, DOPT_CF_RESTART_S, DB_FONT6_XSPACING) + 8;
        if (st->cf3w < DOPT_CF_BTN_MIN) st->cf3w = DOPT_CF_BTN_MIN;
        st->cfw = (mw < DOPT_CF_MIN_W ? DOPT_CF_MIN_W : mw) + DOPT_CF_PAD_W;
        st->cfh = (f->maxh + DB_FONT6_YSPACING) + DOPT_CF_PAD_H;
        st->cfx = (DOPT_SCREEN_W - st->cfw) / 2;
        st->cfy = (DOPT_SCREEN_H - st->cfh) / 2;
    }
    st->laidout = 1;
}

void dopt_open(DOPT_State *st, const DB_Pack *p)
{
    /* Whoever opens it owns this. The pause dialog walking to Visuals must not inherit
       "the main menu opened me" from an earlier visit, or OK would close the dialog
       instead of stepping back to the pause menu. */
    st->vis.from_menu = 0;
    st->page = DOPT_PAGE_OPTIONS;
    st->selected = DOPT_RESUME; /* goptions.cpp:104 curbutton = 6 */
    st->pressed = -1;
    st->drag = -1;
    st->dragdiff = 0;
    dopt_layout(st, p);
}

void dopt_bind(DOPT_State *st, void *user, void (*apply)(void *, const DOPT_Settings *))
{
    st->bind.user = user;
    st->bind.apply = apply;
    if (apply)
        apply(user, &st->set);
}

int dopt_bound(const DOPT_State *st) { return st->bind.apply != 0; }

void dopt_bind_jukebox(DOPT_State *st, void (*jb)(void *, int verb, int arg))
{
    st->jukebox = jb;
}

void dopt_set_tracks(DOPT_State *st, const DOPT_Track *tracks, int count, int playing)
{
    st->tracks = tracks;
    st->ntracks = count > 0 ? count : 0;
    if (st->ntracks <= 0) {
        st->trkSel = st->trkTop = 0;
        st->trkPlaying = -1;
        return;
    }
    st->trkPlaying = (playing >= 0 && playing < st->ntracks) ? playing : -1;
    /* sounddlg.cpp:281-283 opens with whatever is sounding already highlighted. */
    st->trkSel = st->trkPlaying >= 0 ? st->trkPlaying : 0;
    st->trkTop = 0;
    if (st->trkSel >= DOPT_SND_ROWS)
        st->trkTop = st->trkSel - DOPT_SND_ROWS + 1;
}

void dopt_set_playing(DOPT_State *st, int playing)
{
    st->trkPlaying = (playing >= 0 && playing < st->ntracks) ? playing : -1;
}

void dopt_scroll(DOPT_State *st, int delta)
{
    int last;
    if (st->page != DOPT_PAGE_SOUND || st->ntracks <= 0)
        return;
    last = st->ntracks - DOPT_SND_ROWS;
    if (last < 0) last = 0;
    st->trkTop += delta;
    if (st->trkTop < 0) st->trkTop = 0;
    if (st->trkTop > last) st->trkTop = last;
}

/* Keep the highlighted row on screen after the keyboard walk moved it. */
static void dopt_track_show(DOPT_State *st)
{
    if (st->trkSel < st->trkTop)
        st->trkTop = st->trkSel;
    if (st->trkSel >= st->trkTop + DOPT_SND_ROWS)
        st->trkTop = st->trkSel - DOPT_SND_ROWS + 1;
    if (st->trkTop < 0)
        st->trkTop = 0;
}

void dopt_bind_visuals(DOPT_State *st, void (*applyvis)(void *, const DOPT_Visuals *))
{
    st->bind.applyvis = applyvis;
    if (applyvis)
        applyvis(st->bind.user, &st->vis);
}

void dopt_set_visuals(DOPT_State *st, const DOPT_Visuals *v)
{
    const int from = st->vis.from_menu;   /* owned by whoever opened the screen */
    if (!v) return;
    st->vis = *v;
    st->vis.from_menu = from;
}

/* Written the way a switch reads when it is ON, so the row says what turning it on does
   rather than naming a subsystem. "Fog Of War" is the exception and is deliberate: it is
   the only one of the five whose ON state is the ordinary game, so it is named for the
   thing rather than for the cheat. */
static const char *dopt_cheat_row[DOPT_CH_TOGGLES] = {
    "Infinite Money",
    "Instant Build",
    "Unlock Tech Tree",
    "Unlock Superweapons",
    "Build Anywhere",
    "Fog Of War",
    "Invincibility"
};

const char *dopt_cheat_label(int item)
{
    if (item == DOPT_CH_WIN)   return "Instant Win";
    if (item == DOPT_CH_LOSE)  return "Instant Lose";
    if (item == DOPT_CH_RESET) return "Reset to Defaults";
    if (item == DOPT_CH_OK)    return "OK";
    return (item >= 0 && item < DOPT_CH_TOGGLES) ? dopt_cheat_row[item] : "";
}

/* THE DEFAULTS ARE THE ORDINARY GAME. Every cheat is off, and fog of war is on because
   ON is what fog of war means in a game nobody is cheating at. So "Reset to Defaults"
   means "play it straight", which is the only reading of the button that stays true as
   switches are added. */
void dopt_cheats_defaults(DOPT_Cheats *c)
{
    if (!c) return;
    c->on[DOPT_CH_MONEY]    = 0;
    c->on[DOPT_CH_INSTANT]  = 0;
    c->on[DOPT_CH_TECH]     = 0;
    /* DOPT_CH_SUPER was missing from this list until 26 Aug 2026. It happened to read
       as off because the only caller's struct is a zeroed static, but "Reset to
       Defaults" could not turn Unlock Superweapons back OFF once it was on -- the one
       job the button has. Every switch is written here now, on purpose. */
    c->on[DOPT_CH_SUPER]    = 0;
    c->on[DOPT_CH_BUILDANY] = 0;
    c->on[DOPT_CH_FOG]      = 1;  /* the ordinary game: the map starts hidden */
    c->on[DOPT_CH_INVULN]   = 0;
}

void dopt_set_cheats(DOPT_State *st, const DOPT_Cheats *c)
{
    if (!st || !c) return;
    st->cheat = *c;
}

const DOPT_Cheats *dopt_cheats(const DOPT_State *st)
{
    return st ? &st->cheat : 0;
}

void dopt_bind_cheats(DOPT_State *st, void (*applych)(void *, const DOPT_Cheats *))
{
    if (!st) return;
    st->bind.applych = applych;
}

/* Push the switches at whoever is listening. Called on every change rather than only on
   OK, because a testing switch that needs the dialog closed before it takes effect is a
   switch you cannot watch working. */
static void dopt_cheat_apply(DOPT_State *st)
{
    if (st && st->bind.applych)
        st->bind.applych(st->bind.user, &st->cheat);
}

void dopt_open_cheats(DOPT_State *st, const DB_Pack *p)
{
    dopt_open(st, p);
    st->page = DOPT_PAGE_CHEATS;
    st->selected = DOPT_CH_MONEY;
}

void dopt_open_visuals(DOPT_State *st, const DB_Pack *p)
{
    dopt_open(st, p);
    st->page = DOPT_PAGE_VISUALS;
    st->selected = st->vis.enhanced ? DOPT_V_ENHANCED : DOPT_V_CLASSIC;
    st->vis.from_menu = 1;
}

int dopt_item_rect(const DOPT_State *st, int item, int *x, int *y, int *w, int *h)
{
    /* THE CONFIRMATION BOX'S THREE BUTTONS, placed where msgbox.cpp places them.
       :177 button1 at x+10, :186 button2 at x + width - (bwidth + 10), and :196 button3
       centred on the box. All three share one row, msgbox.cpp:178 y + height - (h + 10),
       which is why the box's own height carries a 60 pad rather than a 30. */
    if (st->page == DOPT_PAGE_CONFIRM) {
        const int bh = DOPT_BTN_H;
        const int by = st->cfy + st->cfh - (bh + DOPT_CF_EDGE);
        if (item < 0 || item >= DOPT_CF_COUNT)
            return 0;
        *y = by; *h = bh;
        if (item == DOPT_CF_ABORT) {
            *x = st->cfx + DOPT_CF_EDGE; *w = st->cfbw; return 1;
        }
        if (item == DOPT_CF_CANCEL) {
            *x = st->cfx + st->cfw - (st->cfbw + DOPT_CF_EDGE); *w = st->cfbw; return 1;
        }
        *x = st->cfx + (st->cfw - st->cf3w) / 2; *w = st->cf3w; return 1;
    }
    if (st->page == DOPT_PAGE_CONTROLS) {
        if (item < 0 || item >= DOPT_C_COUNT)
            return 0;
        switch (item) {
        case DOPT_C_SPEED:
            *x = DOPT_GC_SPEED_X; *y = DOPT_GC_SPEED_Y;
            *w = DOPT_GC_SPEED_W; *h = DOPT_GC_SPEED_H;
            return 1;
        case DOPT_C_SCROLL:
            *x = DOPT_GC_SCROLL_X; *y = DOPT_GC_SCROLL_Y;
            *w = DOPT_GC_SCROLL_W; *h = DOPT_GC_SCROLL_H;
            return 1;
        case DOPT_C_MUSIC:
        case DOPT_C_SOUND:
        case DOPT_C_SPEECH:
            *x = DOPT_GC_VOL_X;
            *y = DOPT_GC_VOL_Y + DOPT_GC_VOL_STEP * (item - DOPT_C_MUSIC);
            *w = DOPT_GC_VOL_W; *h = DOPT_GC_VOL_H;
            return 1;
        case DOPT_C_SOUNDCTRL:
            /* The LEFT half of the bottom row's centred pair. It used to be a hard 64
               wide at DOPT_GC_X + 6, which is 26 pixels narrower than its own label
               needs, and db_print does not truncate: the text printed outside the box
               and off the dialog onto the battlefield. Width, gap and x are all derived
               at DOPT_GC_ROW_BTN_W in dosopt.h, which carries a project request and the
               1995/cartridge comparison. */
            *w = DOPT_GC_ROW_BTN_W;
            *h = DOPT_GC_OK_H;
            *x = DOPT_GC_SOUNDCTRL_X;
            *y = DOPT_GC_OK_Y;
            return 1;
        default:
            /* DOPT_C_OK: the RIGHT half of that same pair. This is the one arm that
               leaves gamedlg.cpp:151's screen-centred OK behind. DOPT_V_OK, DOPT_A_OK
               and DOPT_S_OK have their own branches below and are untouched, so the
               Visuals, Advanced and jukebox pages still place OK exactly as 1995 did,
               and st->okw is still live for them: do not delete it. */
            *w = DOPT_GC_ROW_BTN_W;
            *h = DOPT_GC_OK_H;
            *x = DOPT_GC_ROW_OK_X;
            *y = DOPT_GC_OK_Y;
            return 1;
        }
    }

    if (st->page == DOPT_PAGE_SOUND) {
        if (item < 0 || item >= DOPT_S_COUNT)
            return 0;
        switch (item) {
        case DOPT_S_MUSIC:
            *x = DOPT_SND_MVOL_X; *y = DOPT_SND_MVOL_Y;
            *w = DOPT_SND_VOL_W;  *h = DOPT_SND_VOL_H;  return 1;
        case DOPT_S_SOUND:
            *x = DOPT_SND_MVOL_X; *y = DOPT_SND_FXVOL_Y;
            *w = DOPT_SND_VOL_W;  *h = DOPT_SND_VOL_H;  return 1;
        case DOPT_S_LIST:
            *x = DOPT_SND_LIST_X; *y = DOPT_SND_LIST_Y;
            *w = DOPT_SND_LIST_W; *h = DOPT_SND_LIST_H; return 1;
        case DOPT_S_STOP:
            *x = DOPT_SND_STOP_X; *y = DOPT_SND_STOP_Y;
            *w = DOPT_SND_GLYPH_W; *h = DOPT_SND_BTN_H; return 1;
        case DOPT_S_PLAY:
            *x = DOPT_SND_PLAY_X; *y = DOPT_SND_PLAY_Y;
            *w = DOPT_SND_GLYPH_W; *h = DOPT_SND_BTN_H; return 1;
        case DOPT_S_SHUFFLE:
            *x = DOPT_SND_SHUFFLE_X; *y = DOPT_SND_SHUFFLE_Y;
            *w = DOPT_SND_ONOFF_W;   *h = DOPT_SND_BTN_H; return 1;
        case DOPT_S_REPEAT:
            *x = DOPT_SND_REPEAT_X; *y = DOPT_SND_REPEAT_Y;
            *w = DOPT_SND_ONOFF_W;  *h = DOPT_SND_BTN_H; return 1;
        default: /* DOPT_S_OK */
            *x = DOPT_SND_BTN_X; *y = DOPT_SND_BTN_Y;
            *w = DOPT_SND_BTN_W; *h = DOPT_SND_BTN_H; return 1;
        }
    }

    if (st->page == DOPT_PAGE_VISUALS) {
        if (item < 0 || item >= DOPT_V_COUNT)
            return 0;
        if (item == DOPT_V_OK) {
            *w = st->okw ? st->okw : DOPT_MIN_BTN_W;
            *h = DOPT_GC_OK_H;
            *x = (DOPT_SCREEN_W - *w) / 2;
            *y = DOPT_GC_OK_Y;
            return 1;
        }
        *x = DOPT_V_BTN_X; *w = DOPT_V_BTN_W; *h = DOPT_V_BTN_H;
        /* CLASSIC, ENHANCED, then a gap, then ADVANCED: the gap says the third one is
           not a third choice but a way further in. */
        *y = DOPT_V_TOP + DOPT_V_STEP * item + (item == DOPT_V_ADVANCED ? DOPT_V_GAP : 0);
        return 1;
    }

    if (st->page == DOPT_PAGE_CHEATS) {
        if (item < 0 || item >= DOPT_CH_COUNT)
            return 0;
        if (item == DOPT_CH_WIN || item == DOPT_CH_LOSE) {
            /* Their own row, one above Reset/OK. Two ordinary buttons centred as a
               pair, the same shape the row below uses. */
            const int bw = DOPT_GC_ROW_BTN_W;
            const int left = DOPT_GC_X + (DOPT_GC_W - (bw * 2 + DOPT_GC_ROW_GAP)) / 2;
            *w = bw;
            *h = DOPT_GC_OK_H;
            *y = DOPT_CH_BTN_Y;
            *x = (item == DOPT_CH_WIN) ? left : left + bw + DOPT_GC_ROW_GAP;
            return 1;
        }
        if (item == DOPT_CH_RESET || item == DOPT_CH_OK) {
            /* Centred as a PAIR, with the left half as wide as its own label needs and
               the right half the ordinary button width. Derived rather than fixed for
               the reason the field's comment gives. */
            const int rw = st->chresetw ? st->chresetw : DOPT_MIN_BTN_W;
            const int ow = DOPT_GC_ROW_BTN_W;
            const int left = DOPT_GC_X + (DOPT_GC_W - (rw + ow + DOPT_GC_ROW_GAP)) / 2;
            *h = DOPT_GC_OK_H;
            *y = DOPT_GC_OK_Y;
            if (item == DOPT_CH_RESET) { *w = rw; *x = left; }
            else                       { *w = ow; *x = left + rw + DOPT_GC_ROW_GAP; }
            return 1;
        }
        /* The whole row is the target, not the seven pixel box, for the reason the
           Advanced page gives. */
        *x = DOPT_A_BOX_X;
        *y = DOPT_CH_TOP + DOPT_CH_STEP * item;
        *w = DOPT_V_W - 32;
        *h = DOPT_A_BOX;
        return 1;
    }

    if (st->page == DOPT_PAGE_ADVANCED) {
        if (item < 0 || item >= DOPT_A_COUNT)
            return 0;
        if (item == DOPT_A_OK) {
            *w = st->okw ? st->okw : DOPT_MIN_BTN_W;
            *h = DOPT_GC_OK_H;
            *x = (DOPT_SCREEN_W - *w) / 2;
            *y = DOPT_GC_OK_Y;
            return 1;
        }
        /* The whole row is the target, not just the little box: a checkbox you have to
           hit within seven pixels is a checkbox nobody uses. */
        *x = DOPT_A_BOX_X;
        *y = DOPT_A_TOP + DOPT_A_STEP * item;
        *w = DOPT_V_W - 32;
        *h = DOPT_A_BOX;
        return 1;
    }

    if (item < 0 || item >= DOPT_ITEM_COUNT)
        return 0;
    *w = st->btnw ? st->btnw : DOPT_MIN_BTN_W;
    *h = DOPT_BTN_H;
    if (item <= DOPT_EXIT) {
        /* The stack. goptions.cpp:130, walking down in OButtonHeight + 2 steps. */
        *x = DOPT_X + (DOPT_W - *w) / 2;
        *y = DOPT_STACK_TOP + DOPT_STACK_STEP * item;
        return 1;
    }
    /* goptions.cpp:163-170: Resume pinned left, Restate pinned right, both 90 wide. */
    *w = DOPT_MIN_BTN_W;
    *y = DOPT_BOTTOM_Y;
    *x = (item == DOPT_RESUME) ? DOPT_X + DOPT_EDGE_MARGIN
                               : DOPT_X + DOPT_W - (DOPT_MIN_BTN_W + DOPT_EDGE_MARGIN);
    return 1;
}

int dopt_hit_test(const DOPT_State *st, int mx, int my)
{
    const int n = dopt_page_count(st);
    int i, x, y, w, h;

    for (i = 0; i < n; i++) {
        if (dopt_item_disabled(st, i))
            continue; /* gadget.cpp:632 */
        if (!dopt_item_rect(st, i, &x, &y, &w, &h))
            continue;
        if (mx >= x && mx < x + w && my >= y && my < y + h)
            return i;
    }
    return -1;
}

int dopt_next_item(const DOPT_State *st, int item, int delta)
{
    const int n = dopt_page_count(st);
    int i, k;

    if (delta == 0)
        return item;
    k = item;
    for (i = 0; i < n; i++) {
        k += (delta > 0) ? 1 : -1;
        if (k >= n)
            k = 0;
        if (k < 0)
            k = n - 1;
        if (!dopt_item_disabled(st, k))
            return k;
    }
    return item;
}

/* ------------------------------------------------------------------------ *
 * The sliders.
 *
 * GaugeClass::Value_To_Pixel / Pixel_To_Value, gauge.cpp:143-190, with the fixed
 * point arithmetic done in plain integers: the gauge's usable travel is Width - 2.
 * ------------------------------------------------------------------------ */

/* PAGE-AWARE, and it has to be: DOPT_S_MUSIC and DOPT_C_SPEED are both 0, so a
   page-blind version would give the Game Speed slider a range of 255. */
static int dopt_ctrl_max(const DOPT_State *st, int ctrl)
{
    /* Both jukebox sliders are volumes: Set_Maximum(255), sounddlg.cpp:229/232. */
    if (st->page == DOPT_PAGE_SOUND)
        return DOPT_VOL_TOP;
    switch (ctrl) {
    case DOPT_C_SPEED:  return DOPT_MAX_SPEED - 1;
    case DOPT_C_SCROLL: return DOPT_MAX_SCROLL - 1;
    default:            return DOPT_VOL_TOP;
    }
}

static int *dopt_ctrl_slot(DOPT_State *st, int ctrl)
{
    /* The jukebox's two sliders are the SAME two settings the Game Controls page
       carries -- sounddlg.cpp and gamedlg.cpp both drive Options.ScoreVolume and
       Options.Volume -- so they share the slot rather than getting a copy that could
       drift out of step with the other page. */
    if (st->page == DOPT_PAGE_SOUND) {
        if (ctrl == DOPT_S_MUSIC) return &st->set.music;
        if (ctrl == DOPT_S_SOUND) return &st->set.sound;
        return 0;
    }
    switch (ctrl) {
    case DOPT_C_SPEED:  return &st->set.speed;
    case DOPT_C_SCROLL: return &st->set.scrollrate;
    case DOPT_C_MUSIC:  return &st->set.music;
    case DOPT_C_SOUND:  return &st->set.sound;
    case DOPT_C_SPEECH: return &st->set.speech;
    default:            return 0;
    }
}

static int dopt_value_to_pixel(const DOPT_State *st, int ctrl, int value)
{
    int x, y, w, h, span, max;
    if (!dopt_item_rect(st, ctrl, &x, &y, &w, &h))
        return 0;
    span = w - 2;
    max = dopt_ctrl_max(st, ctrl);
    if (max <= 0)
        return x;
    if (value < 0) value = 0;
    if (value > max) value = max;
    return x + (int)(((long)span * value) / max);
}

static int dopt_pixel_to_value(const DOPT_State *st, int ctrl, int pixel)
{
    int x, y, w, h, span, max;
    if (!dopt_item_rect(st, ctrl, &x, &y, &w, &h))
        return 0;
    span = w - 2;
    max = dopt_ctrl_max(st, ctrl);
    pixel -= x + 1;
    if (pixel < 0) pixel = 0;
    if (pixel > span) pixel = span;
    if (span <= 0)
        return 0;
    return (int)(((long)max * pixel) / span);
}

static void dopt_set_value(DOPT_State *st, int ctrl, int value)
{
    int *slot = dopt_ctrl_slot(st, ctrl);
    int max = dopt_ctrl_max(st, ctrl);
    if (!slot)
        return;
    if (value < 0) value = 0;
    if (value > max) value = max;
    if (*slot == value)
        return;
    *slot = value;
    if (st->bind.apply)
        st->bind.apply(st->bind.user, &st->set);
}

/* ------------------------------------------------------------------------ *
 * Input.
 * ------------------------------------------------------------------------ */

int dopt_press(DOPT_State *st, int mx, int my)
{
    int hit = dopt_hit_test(st, mx, my);

    st->lastmy = my;
    st->drag = -1;
    if (hit < 0) {
        st->pressed = -1;
        return DOPT_ACT_NONE;
    }

    if ((st->page == DOPT_PAGE_CONTROLS && hit < DOPT_C_SOUNDCTRL) ||
        (st->page == DOPT_PAGE_SOUND && hit <= DOPT_S_SOUND)) {
        /* gauge.cpp:288-316: clicking ON the thumb drags it from where it was
           grabbed; clicking anywhere else jumps the value to the pointer. */
        int cur = *dopt_ctrl_slot(st, hit);
        int curpix = dopt_value_to_pixel(st, hit, cur);
        st->dragdiff = (mx > curpix && mx - curpix < 4) ? mx - curpix : 0;
        /* gauge.cpp:306-315: walk ClickDiff back until the pointer converts to the
           value that is already set, so a grab does not shift the slider by a pixel
           of rounding before it has moved at all. */
        while (st->dragdiff > 0 &&
               dopt_pixel_to_value(st, hit, mx - st->dragdiff) < cur)
            st->dragdiff--;
        st->drag = hit;
        st->selected = hit;
        dopt_set_value(st, hit, dopt_pixel_to_value(st, hit, mx - st->dragdiff));
        return DOPT_ACT_NONE;
    }

    st->pressed = hit;
    st->selected = hit;
    return DOPT_ACT_NONE;
}

int dopt_motion(DOPT_State *st, int mx, int my)
{
    (void)my;
    if (st->drag >= 0) {
        dopt_set_value(st, st->drag, dopt_pixel_to_value(st, st->drag, mx - st->dragdiff));
        return DOPT_ACT_NONE;
    }
    return DOPT_ACT_NONE;
}

/* The action of a button happens on RELEASE over the same button it went down on,
 * which is what the main menu does and what gadget.cpp's LEFTRELEASE amounts to. */
/* Tell the host what the checkboxes now say. Fired on every change rather than on OK,
   so the picture behind the dialog updates as it is clicked -- which is the whole point
   of putting these on a pause screen instead of a launcher. */
static void dopt_vis_apply(DOPT_State *st)
{
    if (st->bind.applyvis)
        st->bind.applyvis(st->bind.user, &st->vis);
}

static int dopt_activate(DOPT_State *st, int item)
{
    /* THE CONFIRMATION'S THREE ANSWERS. Abort and Restart both leave; Cancel puts the
       pause page back exactly as it was, which is what msgbox.cpp's caller does when
       Process returns the cancel value (goptions.cpp:425-440 simply falls through). */
    if (st->page == DOPT_PAGE_CONFIRM) {
        switch (item) {
        case DOPT_CF_ABORT:
            return DOPT_ACT_ABORT;
        case DOPT_CF_RESTART:
            return DOPT_ACT_RESTART;
        case DOPT_CF_CANCEL:
        default:
            st->page = DOPT_PAGE_OPTIONS;
            st->selected = st->cfprev;
            st->pressed = -1;
            return DOPT_ACT_NONE;
        }
    }
    if (st->page == DOPT_PAGE_VISUALS) {
        switch (item) {
        case DOPT_V_CLASSIC:
        case DOPT_V_ENHANCED:
            /* MUTUALLY EXCLUSIVE, and one of them is always on: this is a pair of radio
               buttons wearing the dialog's own button look, because the 1995 toolkit has
               no radio widget and inventing one would be the only unfamiliar thing on
               the screen. Clicking the one already chosen does nothing rather than
               turning the picture off. */
            st->vis.enhanced = (item == DOPT_V_ENHANCED);
            /* Leaving CLASSIC selected with ADVANCED highlighted would strand the
               keyboard on a button that just went dead. */
            if (!st->vis.enhanced && st->selected == DOPT_V_ADVANCED)
                st->selected = DOPT_V_ENHANCED;
            dopt_vis_apply(st);
            return DOPT_ACT_NONE;
        case DOPT_V_ADVANCED:
            if (dopt_item_disabled(st, item))
                return DOPT_ACT_NONE;     /* gadget.cpp:632, and ENTER has no guard */
            st->page = DOPT_PAGE_ADVANCED;
            st->selected = 0;
            st->pressed = -1;
            return DOPT_ACT_NONE;
        default:
            /* OK. Back to wherever this was opened from: the pause dialog in game, or
               out of the dialog altogether when the main menu opened it directly. */
            if (st->vis.from_menu)
                return DOPT_ACT_RESUME;
            st->page = DOPT_PAGE_OPTIONS;
            st->selected = DOPT_VISUALS;
            st->pressed = -1;
            return DOPT_ACT_NONE;
        }
    }

    if (st->page == DOPT_PAGE_CHEATS) {
        if (item == DOPT_CH_OK) {
            st->pressed = -1;
            return DOPT_ACT_RESUME;
        }
        if (item == DOPT_CH_WIN || item == DOPT_CH_LOSE) {
            /* One shot, and the dialog closes with it: the mission is ending, so
               leaving the cheat page open over the top of the score screen would be
               the wrong thing to look at. The host turns this into the engine's own
               Flag_To_Win / Flag_To_Lose. */
            st->pressed = -1;
            return (item == DOPT_CH_WIN) ? DOPT_ACT_CHEAT_WIN : DOPT_ACT_CHEAT_LOSE;
        }
        if (item == DOPT_CH_RESET) {
            dopt_cheats_defaults(&st->cheat);
            dopt_cheat_apply(st);
            return DOPT_ACT_NONE;
        }
        if (item >= 0 && item < DOPT_CH_TOGGLES) {
            st->cheat.on[item] = !st->cheat.on[item];
            dopt_cheat_apply(st);
        }
        return DOPT_ACT_NONE;
    }

    if (st->page == DOPT_PAGE_ADVANCED) {
        if (item == DOPT_A_OK) {
            st->page = DOPT_PAGE_VISUALS;
            st->selected = DOPT_V_ADVANCED;
            st->pressed = -1;
            return DOPT_ACT_NONE;
        }
        if (item >= 0 && item < DOPT_VE_COUNT) {
            st->vis.elem[item] = !st->vis.elem[item];
            dopt_vis_apply(st);
        }
        return DOPT_ACT_NONE;
    }

    if (st->page == DOPT_PAGE_SOUND) {
        switch (item) {
        case DOPT_S_OK:
            /* sounddlg.cpp's "Options Menu" button steps back to Game Controls, which
               is where gamedlg.cpp:433 opened this from. */
            st->page = DOPT_PAGE_CONTROLS;
            st->selected = DOPT_C_SOUNDCTRL;
            st->pressed = -1;
            return DOPT_ACT_NONE;
        case DOPT_S_STOP:
            if (st->jukebox) st->jukebox(st->bind.user, DOPT_JB_STOP, 0);
            st->trkPlaying = -1;
            break;
        case DOPT_S_PLAY:
            if (st->ntracks > 0 && st->trkSel >= 0 && st->trkSel < st->ntracks) {
                st->trkPlaying = st->trkSel;
                if (st->jukebox)
                    st->jukebox(st->bind.user, DOPT_JB_PLAY,
                                st->tracks[st->trkSel].index);
            }
            break;
        case DOPT_S_SHUFFLE:
            st->shuffle = !st->shuffle;
            if (st->jukebox) st->jukebox(st->bind.user, DOPT_JB_SHUFFLE, st->shuffle);
            break;
        case DOPT_S_REPEAT:
            st->repeat = !st->repeat;
            if (st->jukebox) st->jukebox(st->bind.user, DOPT_JB_REPEAT, st->repeat);
            break;
        case DOPT_S_LIST: {
            /* Which row was clicked. The press already set selected/pressed; the row
               comes from where in the box the pointer is. A second click on the row
               that is already chosen plays it, which is ListClass's own double-click
               shortcut (list.cpp:196) reduced to something a single pointer can do. */
            int row = (st->lastmy - DOPT_SND_LIST_Y) / DOPT_SND_ROW_H;
            int ix = st->trkTop + row;
            if (ix >= 0 && ix < st->ntracks) {
                if (ix == st->trkSel) {
                    st->trkPlaying = ix;
                    if (st->jukebox)
                        st->jukebox(st->bind.user, DOPT_JB_PLAY, st->tracks[ix].index);
                } else {
                    st->trkSel = ix;
                }
            }
            break;
        }
        default:
            break;
        }
        st->pressed = -1;
        return DOPT_ACT_NONE;
    }

    if (st->page == DOPT_PAGE_CONTROLS) {
        if (item == DOPT_C_SOUNDCTRL) {
            st->page = DOPT_PAGE_SOUND;
            st->selected = st->ntracks > 0 ? DOPT_S_LIST : DOPT_S_OK;
            st->pressed = -1;
            return DOPT_ACT_NONE;
        }
        if (item == DOPT_C_OK) {
            /* gamedlg.cpp: OK closes Game Controls and returns to the pause dialog. */
            st->page = DOPT_PAGE_OPTIONS;
            st->selected = DOPT_RESUME;
            st->pressed = -1;
        }
        return DOPT_ACT_NONE;
    }

    switch (item) {
    case DOPT_GAME:
        st->page = DOPT_PAGE_CONTROLS;
        st->selected = DOPT_C_MUSIC;
        st->pressed = -1;
        return DOPT_ACT_NONE;
    case DOPT_VISUALS:
        st->page = DOPT_PAGE_VISUALS;
        st->selected = st->vis.enhanced ? DOPT_V_ENHANCED : DOPT_V_CLASSIC;
        st->pressed = -1;
        return DOPT_ACT_NONE;
    case DOPT_SAVE:
        return DOPT_ACT_SAVE;
    case DOPT_LOAD:
        return DOPT_ACT_LOAD;
    case DOPT_RESUME:
        return DOPT_ACT_RESUME;
    case DOPT_ABORT:
        /* 1995 ASKS FIRST. goptions.cpp:421-447: the BUTTON_QUIT arm raises
           WWMessageBox().Process(TXT_CONFIRM_EXIT, TXT_ABORT, TXT_CANCEL, TXT_RESTART)
           and only queues the exit if the answer is Abort. The mission used to end on
           this click with nothing in between. `cfprev` remembers what was selected on the
           pause page so Cancel can put it back exactly. */
        st->cfprev = st->selected;
        st->page = DOPT_PAGE_CONFIRM;
        st->selected = DOPT_CF_ABORT;   /* msgbox.cpp:216 curbutton = 0 */
        st->pressed = -1;
        return DOPT_ACT_NONE;
    case DOPT_EXIT:
        return DOPT_ACT_EXIT;
    default:
        return DOPT_ACT_NONE;
    }
}

int dopt_release(DOPT_State *st, int mx, int my)
{
    int hit, was;

    if (st->drag >= 0) {
        dopt_set_value(st, st->drag, dopt_pixel_to_value(st, st->drag, mx - st->dragdiff));
        st->drag = -1;
        return DOPT_ACT_NONE;
    }

    hit = dopt_hit_test(st, mx, my);
    was = st->pressed;
    st->pressed = -1;
    if (hit < 0 || hit != was)
        return DOPT_ACT_NONE;
    return dopt_activate(st, hit);
}

int dopt_key(DOPT_State *st, int key)
{
    switch (key) {
    case DOPT_KEY_UP:
    case DOPT_KEY_DOWN: {
        const int d = (key == DOPT_KEY_DOWN) ? 1 : -1;
        /* WITH THE LIST FOCUSED the arrows walk the TRACKS, not the gadgets. That is
           what ListClass does with the keyboard (list.cpp:329-352) and it is the only
           way to reach track 30 of 37 without a wheel. Tab off the list by walking to
           either end, which is where the ordinary gadget walk resumes. */
        if (st->page == DOPT_PAGE_SOUND && st->selected == DOPT_S_LIST &&
            st->ntracks > 0) {
            if ((d < 0 && st->trkSel > 0) || (d > 0 && st->trkSel < st->ntracks - 1)) {
                st->trkSel += d;
                dopt_track_show(st);
                return DOPT_ACT_NONE;
            }
        }
        st->selected = dopt_next_item(st, st->selected, d);
        return DOPT_ACT_NONE;
    }
    case DOPT_KEY_LEFT:
    case DOPT_KEY_RIGHT: {
        /* On a slider the arrows nudge the value. sounddlg.cpp has no keyboard
           handling of its own; this is the obvious keyboard equivalent of dragging
           and it is ours, which is why the step is a plain eighth of travel. */
        if ((st->page == DOPT_PAGE_CONTROLS && st->selected < DOPT_C_SOUNDCTRL) ||
            (st->page == DOPT_PAGE_SOUND && st->selected <= DOPT_S_SOUND)) {
            int *slot = dopt_ctrl_slot(st, st->selected);
            int max = dopt_ctrl_max(st, st->selected);
            int step = max > 16 ? max / 8 : 1;
            if (slot)
                dopt_set_value(st, st->selected,
                               *slot + ((key == DOPT_KEY_RIGHT) ? step : -step));
        }
        return DOPT_ACT_NONE;
    }
    case DOPT_KEY_ENTER:
        return dopt_activate(st, st->selected);
    case DOPT_KEY_ESC:
        /* goptions.cpp:308 KN_ESC on the pause dialog is Resume; on the sub-dialog
           it goes back one level, which is what its OK button does. */
        if (st->page == DOPT_PAGE_CONTROLS)
            return dopt_activate(st, DOPT_C_OK);
        if (st->page == DOPT_PAGE_VISUALS)
            return dopt_activate(st, DOPT_V_OK);
        if (st->page == DOPT_PAGE_ADVANCED)
            return dopt_activate(st, DOPT_A_OK);
        if (st->page == DOPT_PAGE_SOUND)
            return dopt_activate(st, DOPT_S_OK);
        /* The cheat page is not walked to, so there is no level to go back to: Escape
           closes it exactly as its OK button does. */
        if (st->page == DOPT_PAGE_CHEATS)
            return dopt_activate(st, DOPT_CH_OK);
        /* On the confirmation, Escape is Cancel and NOT Resume. Leaving it as Resume
           would make the key that means "back out of this question" close the pause
           dialog and drop the player back into a mission they had just asked to leave,
           which is a worse answer than either button gives. */
        if (st->page == DOPT_PAGE_CONFIRM)
            return dopt_activate(st, DOPT_CF_CANCEL);
        return DOPT_ACT_RESUME;
    default:
        return DOPT_ACT_NONE;
    }
}

/* ------------------------------------------------------------------------ *
 * Drawing. Same primitives, same colours and the same two box routines the main
 * menu uses; see menu/dosmenu.c for the walk-through of each one.
 * ------------------------------------------------------------------------ */

static void dopt_button_box(DB_Surface *s, int x, int y, int w, int h, int pressed,
                            int disabled)
{
    unsigned char filler, shadow, hilite, corner;

    if (disabled) {
        filler = DOPT_DIS_FILL;
        shadow = DOPT_DIS_SHADOW;
        hilite = DOPT_DIS_HILITE;
        corner = DOPT_DIS_CORNERS;
    } else {
        filler = DOPT_GREEN_BKGD;
        shadow = pressed ? DOPT_LIGHT_GREEN : DOPT_GREEN_SHADOW;
        hilite = pressed ? DOPT_GREEN_SHADOW : DOPT_LIGHT_GREEN;
        corner = DOPT_GREEN_CORNERS;
    }
    w--;
    h--;
    db_fill_rect(s, x, y, x + w, y + h, filler);
    db_line_h(s, x, x + w, y + h, shadow);
    db_line_v(s, x + w, y, y + h, shadow);
    db_line_h(s, x, x + w, y, hilite);
    db_line_v(s, x, y, y + h, hilite);
    db_put_pixel(s, x, y + h, corner);
    db_put_pixel(s, x + w, y, corner);
}

/* dialog.cpp:64 Dialog_Box -> BOXSTYLE_GREEN_BORDER, an inset rectangle on black. */
static void dopt_dialog_box(DB_Surface *s, int x, int y, int w, int h)
{
    w--;
    h--;
    db_fill_rect(s, x, y, x + w, y + h, DB_BLACK);
    db_line_h(s, x + 1, x + w - 1, y + 1, DOPT_GREEN_BOX);
    db_line_h(s, x + 1, x + w - 1, y + h - 1, DOPT_GREEN_BOX);
    db_line_v(s, x + 1, y + 1, y + h - 1, DOPT_GREEN_BOX);
    db_line_v(s, x + w - 1, y + 1, y + h - 1, DOPT_GREEN_BOX);
}

/* goptions.cpp:507 Draw_Caption: two OPTIONS.SHP filigrees, the centred caption and
 * the bright underline the width of the text. */
static void dopt_caption(DB_Surface *s, const DB_Pack *p, const char *text, int x, int y,
                         int w)
{
    const DB_Shape *sh = db_shape(p, "OPTIONS");
    const DB_Font *f = db_font(p, "GRAD6FNT");
    unsigned char fp[16];
    int tw, tx, ty;

    if (sh) {
        db_draw_shape_centered(s, sh, DOPT_FILIGREE_LEFT, x + 12, y + 11);
        db_draw_shape_centered(s, sh, DOPT_FILIGREE_RIGHT, x + w - 14, y + 11);
    }
    if (!f || !text || !*text)
        return;

    db_font_palette_grad(fp, DOPT_CC_GREEN, DB_TBLACK);
    tw = db_string_width(f, text, DB_FONT6_XSPACING);
    tx = x + w / 2 - tw / 2;
    ty = y + DOPT_CAPTION_Y;
    db_print(s, f, text, tx, ty, fp, DB_FONT6_XSPACING);
    /* goptions.cpp:585-589: the rule sits FontHeight + FontYSpacing below the text. */
    db_line_h(s, tx, tx + tw, ty + f->maxh + DB_FONT6_YSPACING, DOPT_CC_GREEN);
}

static void dopt_draw_button(DB_Surface *s, const DB_Font *f, const DOPT_State *st,
                             int item, const char *label)
{
    unsigned char fp[16];
    int x, y, w, h, tx, i, pressed, on, disabled;

    if (!dopt_item_rect(st, item, &x, &y, &w, &h))
        return;
    disabled = dopt_item_disabled(st, item);
    pressed = (st->pressed == item);
    on = (st->selected == item);

    dopt_button_box(s, x, y, w, h, pressed, disabled);
    if (disabled) {
        for (i = 0; i < 16; i++)
            fp[i] = (i >= 4) ? DOPT_TEXT_DISABLED : DB_TBLACK;
    } else {
        db_font_palette_grad(fp, (pressed || on) ? DOPT_TEXT_BRIGHT : DOPT_TEXT_MEDIUM,
                             DB_TBLACK);
    }
    if (!f)
        return;
    tx = x + (w >> 1) - 1 - (db_string_width(f, label, DB_FONT6_XSPACING) >> 1);
    db_print(s, f, label, tx, y + 1, fp, DB_FONT6_XSPACING);
}

/* gauge.cpp:205-240 Draw_Me: a GREEN_DOWN body, the travelled part filled bright,
 * then a 4 pixel GREEN_RAISED thumb. */
static void dopt_draw_slider(DB_Surface *s, const DOPT_State *st, int ctrl, int value)
{
    int x, y, w, h, mid, thumb, max;

    if (!dopt_item_rect(st, ctrl, &x, &y, &w, &h))
        return;

    /* BOXSTYLE_GREEN_DOWN, dialog.cpp row 6: filler BKGD, shadow LIGHT, hilite SHADOW */
    db_fill_rect(s, x, y, x + w - 1, y + h - 1, DOPT_GREEN_BKGD);
    db_line_h(s, x, x + w - 1, y + h - 1, DOPT_LIGHT_GREEN);
    db_line_v(s, x + w - 1, y, y + h - 1, DOPT_LIGHT_GREEN);
    db_line_h(s, x, x + w - 1, y, DOPT_GREEN_SHADOW);
    db_line_v(s, x, y, y + h - 1, DOPT_GREEN_SHADOW);

    mid = dopt_value_to_pixel(st, ctrl, value);
    if (mid >= x + 1)
        db_fill_rect(s, x + 1, y + 1, mid, y + h - 2, DOPT_BRIGHT_GREEN);

    /* gauge.cpp:357-368: the thumb is pulled back so it cannot hang off the end. */
    max = dopt_value_to_pixel(st, ctrl, dopt_ctrl_max(st, ctrl));
    thumb = mid;
    if (thumb + 4 > max)
        thumb = max - 2;
    dopt_button_box(s, thumb, y, 4, h, 0, 0);
}

/* THE TWO CAPTION RULES, and they are not the same rule.
 *
 *  Game Speed and Scroll Rate are gamedlg.cpp's own sliders and it labels them ABOVE,
 *  left aligned, at (x, y - d_txt6_h), with Slower and Faster under the two ends
 *  (gamedlg.cpp:242-255 and :264-277).
 *
 *  The three volume rows are sounddlg.cpp's row, which labels RIGHT ALIGNED five
 *  pixels to the left of the slider and two rows up (sounddlg.cpp:332-343).
 *
 *  Using the volume rule for all five was the first version of this file and it is
 *  visibly wrong: those two sliders are the full width of the dialog, so a caption to
 *  their left starts off the edge of the screen and comes out as "ME SPEED:" and
 *  "OLL RATE:". Caught by looking at the screenshot, which is the only way it could
 *  have been caught: every number involved was individually correct. */
static void dopt_draw_slider_label(DB_Surface *s, const DB_Font *f, const DOPT_State *st,
                                   int ctrl)
{
    unsigned char fp[16];
    int x, y, w, h;
    const char *label = dopt_ctrl_label[ctrl];

    if (!f || !dopt_item_rect(st, ctrl, &x, &y, &w, &h))
        return;
    db_font_palette_grad(fp, (st->selected == ctrl) ? DOPT_TEXT_BRIGHT : DOPT_TEXT_MEDIUM,
                         DB_TBLACK);

    if (ctrl == DOPT_C_SPEED || ctrl == DOPT_C_SCROLL) {
        db_print(s, f, label, x, y - DOPT_GC_TXT6_H, fp, DB_FONT6_XSPACING);
        db_font_palette_grad(fp, DOPT_TEXT_MEDIUM, DB_TBLACK);
        db_print(s, f, "Slower", x, y + h + 1, fp, DB_FONT6_XSPACING);
        db_print(s, f, "Faster",
                 x + w - db_string_width(f, "Faster", DB_FONT6_XSPACING), y + h + 1, fp,
                 DB_FONT6_XSPACING);
        return;
    }

    db_print(s, f, label,
             x - DOPT_GC_VOL_LABEL_GAP - db_string_width(f, label, DB_FONT6_XSPACING),
             y - DOPT_GC_VOL_LABEL_RISE, fp, DB_FONT6_XSPACING);
}

/* A CHECKBOX, and there is no checkbox primitive in this toolkit because 1995 had none.
   Rather than invent a widget, this is the dialog's own inset well -- the same
   dopt_button_box(pressed) the slider thumb and every pressed button already use --
   with the bright green the slider fills its travelled part with. So a ticked box reads
   like a pressed control and an unticked one like a raised one, which is the only visual
   grammar this screen has. */
static void dopt_draw_check(DB_Surface *s, const DOPT_State *st, int item, int on)
{
    int x, y, w, h;
    if (!dopt_item_rect(st, item, &x, &y, &w, &h))
        return;
    dopt_button_box(s, x, y, DOPT_A_BOX, DOPT_A_BOX, on, 0);
    if (on)
        db_fill_rect(s, x + 2, y + 2, x + DOPT_A_BOX - 3, y + DOPT_A_BOX - 3,
                     DOPT_BRIGHT_GREEN);
}

/* The two Visuals pages. Everything on them is ours; see dosopt.h. */
static void dopt_draw_visuals(DB_Surface *s, const DB_Pack *p, const DOPT_State *st)
{
    const DB_Font *f = db_font(p, "GRAD6FNT");
    unsigned char fp[16];
    int i, x, y, w, h;

    dopt_dialog_box(s, DOPT_V_X, DOPT_V_Y, DOPT_V_W, DOPT_V_H);
    dopt_caption(s, p, "Visuals", DOPT_V_X, DOPT_V_Y, DOPT_V_W);

    for (i = 0; i < DOPT_V_COUNT; i++)
        dopt_draw_button(s, f, st, i, dopt_vis_label[i]);

    /* The chosen one of the pair is drawn HELD DOWN. dopt_draw_button only knows about
       st->pressed, which is a transient mouse state, so the latch is painted over the
       top: same box, pressed style, then the label again bright. */
    {
        const int sel = st->vis.enhanced ? DOPT_V_ENHANCED : DOPT_V_CLASSIC;
        if (dopt_item_rect(st, sel, &x, &y, &w, &h) && f) {
            int tx;
            dopt_button_box(s, x, y, w, h, 1, 0);
            db_font_palette_grad(fp, DOPT_TEXT_BRIGHT, DB_TBLACK);
            tx = x + (w >> 1) - 1
                 - (db_string_width(f, dopt_vis_label[sel], DB_FONT6_XSPACING) >> 1);
            db_print(s, f, dopt_vis_label[sel], tx, y + 1, fp, DB_FONT6_XSPACING);
        }
    }

    /* One line saying what the two words mean, because "Classic" and "Enhanced" do not
       say it on their own. */
    if (f) {
        /* Short enough to fit the 232 wide box at 6 point. The first version did not
           and ran out of both sides of the dialog. */
        const char *msg = st->vis.enhanced ? "Lighting, depth and colour."
                                           : "The cartridge's own picture.";
        db_font_palette_grad(fp, DOPT_TEXT_MEDIUM, DB_TBLACK);
        db_print(s, f, msg,
                 DOPT_V_X + (DOPT_V_W >> 1)
                     - (db_string_width(f, msg, DB_FONT6_XSPACING) >> 1),
                 DOPT_V_TOP + DOPT_V_STEP * DOPT_V_ADVANCED + DOPT_V_GAP + 16,
                 fp, DB_FONT6_XSPACING);
    }
}

static void dopt_draw_advanced(DB_Surface *s, const DB_Pack *p, const DOPT_State *st)
{
    const DB_Font *f = db_font(p, "GRAD6FNT");
    unsigned char fp[16];
    int i, x, y, w, h;

    dopt_dialog_box(s, DOPT_V_X, DOPT_V_Y, DOPT_V_W, DOPT_V_H);
    dopt_caption(s, p, "Advanced", DOPT_V_X, DOPT_V_Y, DOPT_V_W);

    for (i = 0; i < DOPT_VE_COUNT; i++) {
        if (!dopt_item_rect(st, i, &x, &y, &w, &h))
            continue;
        dopt_draw_check(s, st, i, st->vis.elem[i]);
        if (!f)
            continue;
        db_font_palette_grad(fp, (st->selected == i) ? DOPT_TEXT_BRIGHT
                                                     : DOPT_TEXT_MEDIUM, DB_TBLACK);
        db_print(s, f, dopt_vis_elem[i], DOPT_A_LABEL_X, y - 1, fp, DB_FONT6_XSPACING);
    }
    dopt_draw_button(s, f, st, DOPT_A_OK, "OK");
}

/* The cheat page. Same box, same caption, same checkbox and same button as the Advanced
   page draws, because it IS those, called with a different list. */
static void dopt_draw_cheats(DB_Surface *s, const DB_Pack *p, const DOPT_State *st)
{
    const DB_Font *f = db_font(p, "GRAD6FNT");
    unsigned char fp[16];
    int i, x, y, w, h;

    dopt_dialog_box(s, DOPT_V_X, DOPT_V_Y, DOPT_V_W, DOPT_V_H);
    dopt_caption(s, p, "Cheats", DOPT_V_X, DOPT_V_Y, DOPT_V_W);

    for (i = 0; i < DOPT_CH_TOGGLES; i++) {
        if (!dopt_item_rect(st, i, &x, &y, &w, &h))
            continue;
        dopt_draw_check(s, st, i, st->cheat.on[i]);
        if (!f)
            continue;
        db_font_palette_grad(fp, (st->selected == i) ? DOPT_TEXT_BRIGHT
                                                     : DOPT_TEXT_MEDIUM, DB_TBLACK);
        db_print(s, f, dopt_cheat_label(i), DOPT_A_LABEL_X, y - 1, fp, DB_FONT6_XSPACING);
    }
    dopt_draw_button(s, f, st, DOPT_CH_WIN, dopt_cheat_label(DOPT_CH_WIN));
    dopt_draw_button(s, f, st, DOPT_CH_LOSE, dopt_cheat_label(DOPT_CH_LOSE));
    dopt_draw_button(s, f, st, DOPT_CH_RESET, dopt_cheat_label(DOPT_CH_RESET));
    dopt_draw_button(s, f, st, DOPT_CH_OK, dopt_cheat_label(DOPT_CH_OK));
}

/* The two shape buttons 1995 had and we do not. BTN-ST.SHP and BTN-PL.SHP are not in
 * any archive this project bakes, so the rect and the press behaviour are the
 * cartridge's and the GLYPH inside is ours: a filled square for stop, a right-pointing
 * triangle for play, both in the same ink the button's label would have used. */
static void dopt_draw_shapebtn(DB_Surface *s, const DOPT_State *st, int item, int play)
{
    int x, y, w, h, cx, cy, i, ink;

    if (!dopt_item_rect(st, item, &x, &y, &w, &h))
        return;
    dopt_button_box(s, x, y, w, h, st->pressed == item, dopt_item_disabled(st, item));
    ink = dopt_item_disabled(st, item) ? DOPT_TEXT_DISABLED
        : ((st->pressed == item || st->selected == item) ? DOPT_TEXT_BRIGHT
                                                         : DOPT_TEXT_MEDIUM);
    cx = x + w / 2;
    cy = y + h / 2;
    if (play) {
        /* a triangle 5 wide, pointing right */
        for (i = 0; i < 5; i++)
            db_line_v(s, cx - 2 + i, cy - (4 - i), cy + (4 - i), (unsigned char)ink);
    } else {
        db_fill_rect(s, cx - 2, cy - 2, cx + 2, cy + 2, (unsigned char)ink);
    }
}

/* One row: "Track %d\t%d:%02d\t%s" against tabs 55/72/90, sounddlg.cpp:271-284. */
static void dopt_draw_track(DB_Surface *s, const DB_Font *f, const DOPT_State *st,
                            int ix, int y)
{
    const DOPT_Track *t = &st->tracks[ix];
    unsigned char fp[16];
    char num[24], len[16];
    int sel = (ix == st->trkSel);

    if (sel)
        db_fill_rect(s, DOPT_SND_LIST_X + 1, y, DOPT_SND_LIST_X + DOPT_SND_LIST_W - 2,
                     y + DOPT_SND_ROW_H - 1, DOPT_GREEN_BKGD);
    db_font_palette_grad(fp, sel ? DOPT_TEXT_BRIGHT : DOPT_TEXT_MEDIUM, DB_TBLACK);

    /* The 1995 row counts from one and shows the track's own length. */
    sprintf(num, "Track %d", ix + 1);
    sprintf(len, "%d:%02d", t->seconds / 60, t->seconds % 60);
    db_print(s, f, num, DOPT_SND_LIST_X + 4, y, fp, DB_FONT6_XSPACING);
    db_print(s, f, len, DOPT_SND_LIST_X + DOPT_SND_TAB1, y, fp, DB_FONT6_XSPACING);
    db_print(s, f, t->fullname && *t->fullname ? t->fullname : t->base,
             DOPT_SND_LIST_X + DOPT_SND_TAB3, y, fp, DB_FONT6_XSPACING);

    /* Ours, and worth the pixel: a bright bar in the left margin marks the track that
       is actually sounding, which is not always the one the cursor is on. */
    if (ix == st->trkPlaying)
        db_line_v(s, DOPT_SND_LIST_X + 2, y + 1, y + DOPT_SND_ROW_H - 3,
                  DOPT_BRIGHT_GREEN);
}

static void dopt_draw_sound(DB_Surface *s, const DB_Pack *p, const DOPT_State *st)
{
    const DB_Font *f = db_font(p, "GRAD6FNT");
    unsigned char fp[16];
    int i, row, y;

    dopt_dialog_box(s, DOPT_SND_X, DOPT_SND_Y, DOPT_SND_W, DOPT_SND_H);
    dopt_caption(s, p, "Sound Controls", DOPT_SND_X, DOPT_SND_Y, DOPT_SND_W);

    /* sounddlg.cpp:332-343, the two volume rows: caption right-aligned at (x-5, y-2). */
    dopt_draw_slider(s, st, DOPT_S_MUSIC, st->set.music);
    dopt_draw_slider(s, st, DOPT_S_SOUND, st->set.sound);
    if (f) {
        db_font_palette_grad(fp, DOPT_TEXT_MEDIUM, DB_TBLACK);
        db_print(s, f, "Music Volume:",
                 DOPT_SND_MVOL_X - 5
                     - db_string_width(f, "Music Volume:", DB_FONT6_XSPACING),
                 DOPT_SND_MVOL_Y - 2, fp, DB_FONT6_XSPACING);
        db_print(s, f, "Sound Volume:",
                 DOPT_SND_MVOL_X - 5
                     - db_string_width(f, "Sound Volume:", DB_FONT6_XSPACING),
                 DOPT_SND_FXVOL_Y - 2, fp, DB_FONT6_XSPACING);
    }

    /* The list well, then its rows. BOXSTYLE_GREEN_BOX around it, list.cpp's own frame. */
    db_fill_rect(s, DOPT_SND_LIST_X, DOPT_SND_LIST_Y,
                 DOPT_SND_LIST_X + DOPT_SND_LIST_W - 1,
                 DOPT_SND_LIST_Y + DOPT_SND_LIST_H - 1, DB_BLACK);
    db_line_h(s, DOPT_SND_LIST_X, DOPT_SND_LIST_X + DOPT_SND_LIST_W - 1,
              DOPT_SND_LIST_Y, DOPT_GREEN_BOX);
    db_line_h(s, DOPT_SND_LIST_X, DOPT_SND_LIST_X + DOPT_SND_LIST_W - 1,
              DOPT_SND_LIST_Y + DOPT_SND_LIST_H - 1, DOPT_GREEN_BOX);
    db_line_v(s, DOPT_SND_LIST_X, DOPT_SND_LIST_Y,
              DOPT_SND_LIST_Y + DOPT_SND_LIST_H - 1, DOPT_GREEN_BOX);
    db_line_v(s, DOPT_SND_LIST_X + DOPT_SND_LIST_W - 1, DOPT_SND_LIST_Y,
              DOPT_SND_LIST_Y + DOPT_SND_LIST_H - 1, DOPT_GREEN_BOX);

    if (f) {
        if (st->ntracks <= 0) {
            db_font_palette_grad(fp, DOPT_TEXT_DISABLED, DB_TBLACK);
            db_print(s, f, "No score tracks are installed.", DOPT_SND_LIST_X + 4,
                     DOPT_SND_LIST_Y + 3, fp, DB_FONT6_XSPACING);
        } else {
            for (row = 0; row < DOPT_SND_ROWS; row++) {
                i = st->trkTop + row;
                if (i >= st->ntracks)
                    break;
                y = DOPT_SND_LIST_Y + 1 + row * DOPT_SND_ROW_H;
                dopt_draw_track(s, f, st, i, y);
            }
        }
    }

    dopt_draw_shapebtn(s, st, DOPT_S_STOP, 0);
    dopt_draw_shapebtn(s, st, DOPT_S_PLAY, 1);

    /* sounddlg.cpp:206-212 prints SHUFFLE and REPEAT to the LEFT of their toggles. */
    if (f) {
        db_font_palette_grad(fp, DOPT_TEXT_MEDIUM, DB_TBLACK);
        db_print(s, f, "Shuffle",
                 DOPT_SND_SHUFFLE_X - 5
                     - db_string_width(f, "Shuffle", DB_FONT6_XSPACING),
                 DOPT_SND_SHUFFLE_Y + 1, fp, DB_FONT6_XSPACING);
        db_print(s, f, "Repeat",
                 DOPT_SND_REPEAT_X - 5
                     - db_string_width(f, "Repeat", DB_FONT6_XSPACING),
                 DOPT_SND_REPEAT_Y + 1, fp, DB_FONT6_XSPACING);
    }
    dopt_draw_button(s, f, st, DOPT_S_SHUFFLE, st->shuffle ? "On" : "Off");
    dopt_draw_button(s, f, st, DOPT_S_REPEAT, st->repeat ? "On" : "Off");
    dopt_draw_button(s, f, st, DOPT_S_OK, "Options Menu");
}

/* THE CONFIRMATION BOX. msgbox.cpp:239-252 draws Dialog_Box, then Draw_Caption with a
   TXT_NONE caption (which is why there is no title bar here, unlike every other page),
   then the message at x+20, y+25 in green on black. The three buttons come out of
   dopt_item_rect so the drawn box and the clickable box cannot drift apart.

   Drawn OVER the pause dialog rather than instead of it, because that is what a message
   box is: goptions.cpp:425 raises it from inside its own dialog loop and the dialog is
   still on the screen behind it. */
static void dopt_draw_confirm(DB_Surface *s, const DB_Pack *p, const DOPT_State *st)
{
    const DB_Font *f = db_font(p, "GRAD6FNT");
    unsigned char fp[16];
    int i;

    /* the pause dialog stays behind it */
    dopt_dialog_box(s, DOPT_X, DOPT_Y, DOPT_W, DOPT_H);
    dopt_caption(s, p, "Options", DOPT_X, DOPT_Y, DOPT_W);
    for (i = 0; i < DOPT_ITEM_COUNT; i++)
        dopt_draw_button(s, f, st, i, dopt_items[i].label);

    dopt_dialog_box(s, st->cfx, st->cfy, st->cfw, st->cfh);
    if (f) {
        /* GRAD6FNT IS A GRADIENT FONT and needs the gradient palette builder, not a
           single index. Setting fp[1] alone and leaving the rest DB_TBLACK, which is what
           the scenario-name print at the bottom of dopt_draw does, renders this string
           INVISIBLE: the glyphs span several palette entries and all but one of them stay
           transparent. Caught by looking at the picture, where the box and all three
           buttons drew correctly and the question was simply not there. */
        db_font_palette_grad(fp, DOPT_TEXT_MEDIUM, DB_TBLACK);
        db_print(s, f, DOPT_CF_MSG, st->cfx + DOPT_CF_TEXT_X,
                 st->cfy + DOPT_CF_TEXT_Y, fp, DB_FONT6_XSPACING);
    }
    for (i = 0; i < DOPT_CF_COUNT; i++)
        dopt_draw_button(s, f, st, i, dopt_item_label(st, i));
}

void dopt_draw(DB_Surface *s, const DB_Pack *p, const DOPT_State *st)
{
    const DB_Font *f = db_font(p, "GRAD6FNT");
    unsigned char fp[16];
    int i;

    if (st->page == DOPT_PAGE_VISUALS)  { dopt_draw_visuals(s, p, st);  return; }
    if (st->page == DOPT_PAGE_ADVANCED) { dopt_draw_advanced(s, p, st); return; }
    if (st->page == DOPT_PAGE_CHEATS)   { dopt_draw_cheats(s, p, st);   return; }
    if (st->page == DOPT_PAGE_SOUND)    { dopt_draw_sound(s, p, st);    return; }
    if (st->page == DOPT_PAGE_CONFIRM)  { dopt_draw_confirm(s, p, st);  return; }

    if (st->page == DOPT_PAGE_CONTROLS) {
        dopt_dialog_box(s, DOPT_GC_X, DOPT_GC_Y, DOPT_GC_W, DOPT_GC_H);
        dopt_caption(s, p, "Game Controls", DOPT_GC_X, DOPT_GC_Y, DOPT_GC_W);
        for (i = 0; i < DOPT_C_SOUNDCTRL; i++) {
            dopt_draw_slider_label(s, f, st, i);
            switch (i) {
            case DOPT_C_SPEED:  dopt_draw_slider(s, st, i, st->set.speed); break;
            case DOPT_C_SCROLL: dopt_draw_slider(s, st, i, st->set.scrollrate); break;
            case DOPT_C_MUSIC:  dopt_draw_slider(s, st, i, st->set.music); break;
            case DOPT_C_SOUND:  dopt_draw_slider(s, st, i, st->set.sound); break;
            default:            dopt_draw_slider(s, st, i, st->set.speech); break;
            }
        }
        dopt_draw_button(s, f, st, DOPT_C_SOUNDCTRL,
                         dopt_ctrl_label[DOPT_C_SOUNDCTRL]);
        dopt_draw_button(s, f, st, DOPT_C_OK, dopt_ctrl_label[DOPT_C_OK]);
        return;
    }

    dopt_dialog_box(s, DOPT_X, DOPT_Y, DOPT_W, DOPT_H);
    dopt_caption(s, p, "Options", DOPT_X, DOPT_Y, DOPT_W);
    for (i = 0; i < DOPT_ITEM_COUNT; i++)
        dopt_draw_button(s, f, st, i, dopt_items[i].label);

    /* goptions.cpp:253-263: scenario name over the version line, both right aligned
     * inside the box, 6 point GREEN with no shadow. */
    if (f) {
        int rx = DOPT_X + DOPT_W - 3;
        int ry = DOPT_Y + DOPT_H - 30;
        for (i = 0; i < 16; i++)
            fp[i] = DB_TBLACK;
        fp[1] = DOPT_CC_GREEN;
        if (st->scenario && *st->scenario)
            db_print(s, f, st->scenario,
                     rx - db_string_width(f, st->scenario, DB_FONT6_XSPACING), ry, fp,
                     DB_FONT6_XSPACING);
        if (st->version && *st->version)
            db_print(s, f, st->version,
                     rx - db_string_width(f, st->version, DB_FONT6_XSPACING),
                     ry + f->maxh + DB_FONT6_YSPACING, fp, DB_FONT6_XSPACING);
    }
}

void dopt_draw_cursor(DB_Surface *s, const DB_Pack *p, int mx, int my)
{
    const DB_Shape *sh = db_shape(p, "MOUSE");
    if (sh)
        db_draw_shape(s, sh, 0, mx, my);
}
