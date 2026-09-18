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
#include <ctype.h>

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
    {"Gameplay", 0},       /* ours: how the game is driven, not how it is drawn */
    {"Abort Mission", 0},
    {"Exit Game", 0},
    {"Resume Mission", 0},
    {"Restate", 0}         /* live when the mission has a briefing: dopt_item_disabled */
};

/* The slot dialog's caption and its action button, by mode: TXT_LOAD_MISSION 53,
   TXT_SAVE_MISSION 54, TXT_DELETE_MISSION 55 and TXT_LOAD_BUTTON 56, TXT_SAVE_BUTTON 57,
   TXT_DELETE_BUTTON 58 (loaddlg.cpp:206-226). */
static const char *const dopt_sl_caption[3] = {
    "Load Mission", "Save Mission", "Delete Mission"
};
static const char *const dopt_sl_verb[3] = { "Load", "Save", "Delete" };

/* The Visuals pages. Every label is ours; there is no 1995 string for any of them. */
static const char *const dopt_vis_label[DOPT_V_COUNT] = {
    "Classic", "Enhanced", "Advanced...", "OK"
};

static const char *const dopt_vis_elem[DOPT_VE_COUNT] = {
    /* POSITIONAL, like the enum, and nothing checks the pairing: these seven are the seven
       at the top of DOPT_VisElem in the same order. */
    "True fullscreen",
    "Windowed",
    "Windowed borderless",
    "Resolution",
    "Reset to defaults",
    "UI scaling",
    "Perspective",
    "Smooth animations",
    "New HUD",
    "Bilinear filtering",
    "Water Shader",
    "3D Trees",
    /* Short, because unlike every other row on this page this one carries a control
       with words in it: the entries all end in "Textures", so the row says which
       textures and the control says which set. "Terrain textures / DOS Textures"
       collided at the widest entry. */
    "Terrain",
    "Infantry",
    "Console output gamma",
    "Supersampling",
    "Sun shadows",
    "Ambient occlusion",
    "Dynamic light",
    "Bloom",
    "Colour grade",
    "CRT"
};

/* The entries name the ART, and the noun follows the row: the terrain row picks between
   tile textures and the infantry row between sprites, so "Remastered Textures" under
   INFANTRY would be simply wrong. Same three choices, same order, same numbering. */
static const char *const dopt_texset_name[DOPT_TEX_COUNT] = {
    "N64 Textures", "DOS Textures", "Remastered Textures"
};
static const char *const dopt_infset_name[DOPT_TEX_COUNT] = {
    "N64 Sprites", "DOS Sprites", "Remastered Sprites"
};

/* The Gameplay page's labels. All three are ours. Measured against GRAD6FNT: 104, 116
   and 98 pixels, printed from DOPT_A_LABEL_X, so they end well inside the box inner edge.
   db_print clips to the surface and not to the button, so this measurement is the only
   thing between a long label and ink on the next control. */
static const char *const dopt_gp_row[DOPT_G_TOGGLES] = {
    "Swap mouse buttons",
    "Right button scrolls",
    "Credit tick sound"
};

const char *dopt_gp_label(int item)
{
    if (item == DOPT_G_OK) return "OK";
    return (item >= 0 && item < DOPT_G_TOGGLES) ? dopt_gp_row[item] : "";
}

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
static void dopt_style_greenbox(DB_Surface *s, int x, int y, int w, int h);

int dopt_page_count(const DOPT_State *st)
{
    switch (st->page) {
    case DOPT_PAGE_CONTROLS: return DOPT_C_COUNT;
    case DOPT_PAGE_VISUALS:  return DOPT_V_COUNT;
    case DOPT_PAGE_ADVANCED: return DOPT_A_COUNT;
    case DOPT_PAGE_SOUND:    return DOPT_S_COUNT;
    case DOPT_PAGE_CHEATS:   return DOPT_CH_COUNT;
    case DOPT_PAGE_GAMEPLAY: return DOPT_G_COUNT;
    case DOPT_PAGE_CONFIRM:  return DOPT_CF_COUNT;
    case DOPT_PAGE_RESTATE:  return DOPT_R_COUNT;
    case DOPT_PAGE_NOTICE:   return DOPT_R_COUNT;
    case DOPT_PAGE_SLOTS:    return DOPT_SL_COUNT;
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
        /* The bar prints nothing; the name is here because the scripted click verb looks
           items up by label, and an item with no name is an item no gate can reach. The
           layout gate exempts it from the printed-label leg by name for that reason. */
        if (item == DOPT_A_BAR) return "Scroll";
        return dopt_vis_elem_label(item);
    case DOPT_PAGE_CHEATS:
        return dopt_cheat_label(item);
    case DOPT_PAGE_GAMEPLAY:
        return dopt_gp_label(item);
    case DOPT_PAGE_CONFIRM:
        if (item == DOPT_CF_ABORT)   return dopt_confirm_yes(st);
        if (item == DOPT_CF_RESTART) return DOPT_CF_RESTART_S;
        if (item == DOPT_CF_CANCEL)  return dopt_confirm_no(st);
        return "";
    case DOPT_PAGE_RESTATE:
        /* scenario.cpp:797-803: with no movie to offer the box is a single OK. */
        if (item == DOPT_R_LEFT)  return st->rvideo ? DOPT_R_VIDEO_S : DOPT_R_OK_S;
        if (item == DOPT_R_RIGHT) return st->rvideo ? DOPT_R_OPTIONS_S : "";
        return "";
    case DOPT_PAGE_NOTICE:
        /* Always the single-OK form: a notice offers nothing but to be dismissed. */
        return (item == DOPT_R_LEFT) ? DOPT_R_OK_S : "";
    case DOPT_PAGE_SLOTS:
        /* The list and the field print no centred label; they carry a name so the
           scripted click verb can address them. */
        if (item == DOPT_SL_LIST)   return "Slots";
        if (item == DOPT_SL_EDIT)   return "Description";
        if (item == DOPT_SL_OK)     return dopt_sl_verb[st->sl.mode < 3 ? st->sl.mode : 0];
        if (item == DOPT_SL_CANCEL) return DOPT_CF_CANCEL_S;
        return "";
    case DOPT_PAGE_SOUND:
        if (item == DOPT_S_SHUFFLE) return st->shuffle ? "On" : "Off";
        if (item == DOPT_S_REPEAT)  return st->repeat  ? "On" : "Off";
        return (item >= 0 && item < DOPT_S_COUNT) ? dopt_snd_label[item] : "";
    default:
        /* The one row whose name depends on the game it is in: see dopt_abort_label. */
        if (item == DOPT_ABORT) return dopt_abort_label(st);
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
        /* UI scaling is Enhanced only AND the new HUD's only (the DOS bar keeps its own
           zoom), so it greys out with New HUD off; the Resolution row is the desktop's
           under Windowed Borderless and has nothing to offer with no sizes enumerated.
           Drawn greyed rather than hidden, as every disabled gadget here is. */
        if (item == DOPT_VE_UISCALE)
            return (!st->vis.enhanced || !st->vis.elem[DOPT_VE_NEWHUD]) ? 1 : 0;
        /* Perspective is Enhanced only for the reason the terrain art is: CLASSIC is
           the cartridge's picture, north straight up. */
        if (item == DOPT_VE_PERSPECTIVE)
            return st->vis.enhanced ? 0 : 1;
        if (item == DOPT_VE_RESOLUTION)
            return (st->vis.dispmode == DOPT_DISP_BORDERLESS || st->vis.nres <= 0) ? 1 : 0;
        return 0;
    case DOPT_PAGE_GAMEPLAY:
        return 0;
    case DOPT_PAGE_CHEATS:
        /* LOCKED IN A MATCH: OK stays live or the dialog cannot be closed with the
           pointer. dopt_hit_test and dopt_next_item already honour this, so the mouse
           and the keyboard walk both go dead together. */
        return (st->cheats_locked && item != DOPT_CH_OK) ? 1 : 0;
    case DOPT_PAGE_CONFIRM:
        /* NOTHING TO RESTART IN A GAME OTHER PEOPLE ARE PLAYING. Disabled rather than
           removed, because dopt_hit_test and dopt_next_item both already skip a disabled
           item, so the mouse and the keyboard walk go dead together and no index moves.
           The draw skips it outright instead of greying it: a dead third button is a
           question the player has to read and dismiss. */
        if (item == DOPT_CF_RESTART) return (st && (st->match || st->cfkind == 1)) ? 1 : 0;
        return 0;
    case DOPT_PAGE_SOUND:
        /* Play does nothing with an empty list, and 1995 draws a dead gadget rather
           than hiding it (gadget.cpp:632). Same rule here. */
        if (item == DOPT_S_PLAY || item == DOPT_S_LIST)
            return st->ntracks <= 0;
        return 0;
    case DOPT_PAGE_RESTATE:
        /* The second button exists only alongside Video; a lone OK has no partner. */
        if (item == DOPT_R_RIGHT) return st->rvideo ? 0 : 1;
        return (item == DOPT_R_LEFT) ? 0 : 1;
    case DOPT_PAGE_NOTICE:
        return (item == DOPT_R_LEFT) ? 0 : 1;
    case DOPT_PAGE_SLOTS:
        /* Nothing to pick from is a dead list and a dead action button; a save with
           nothing typed is refused by greying Save (1995 raised "You must enter a
           description!" instead, loaddlg.cpp:424-429, and this is the same refusal
           without the second box). The field exists only in SAVE mode. */
        if (item == DOPT_SL_LIST) return st->sl.nrows <= 0;
        if (item == DOPT_SL_EDIT) return st->sl.mode != DOPT_SL_SAVE;
        if (item == DOPT_SL_OK) {
            if (st->sl.nrows <= 0 || st->sl.sel < 0) return 1;
            if (st->sl.mode == DOPT_SL_SAVE && !st->sl.descr[0]) return 1;
            return 0;
        }
        return 0;
    default:
        /* THE THREE SLOT BUTTONS. With no save system bound none is live; with one,
           Load and Delete need a slot to name and Save does not. 1995 raised "No saved
           games available." (648) on an empty list; a greyed button says it first. */
        if (item == DOPT_LOAD || item == DOPT_DELETE)
            return (!st->bind.slots || st->sl.have <= 0) ? 1 : 0;
        if (item == DOPT_SAVE)
            return st->bind.slots ? 0 : 1;
        /* RESTATE IS LIVE WHEN THERE IS SOMETHING TO RESTATE: briefing text to show,
           or a briefing movie to play. With neither, 1995's button does nothing at all
           (Restate_Mission returns false and Play_Movie finds no file), and this dialog
           draws a dead gadget rather than a live one that does nothing. */
        if (item == DOPT_RESTATE)
            return (st->brief[0] || st->rvideo) ? 0 : 1;
        return (item >= 0 && item < DOPT_ITEM_COUNT) ? dopt_items[item].disabled : 1;
    }
}

/* ------------------------------------------------------------------------ *
 * Layout.
 * ------------------------------------------------------------------------ */

void dopt_settings_init(DOPT_Settings *s)
{
    /* THE SHIPPED GAME CONTROLS, and the ONE place the five of them are chosen. A player
     * with no settings file beside the game gets exactly this block, and the renderer's
     * pre-dialog tick rate reads it rather than repeating the number, so the value the
     * first mission runs at and the value the slider shows cannot disagree.
     *
     * SPEED IS DOPT_DEFAULT_SPEED (4), NOT THE 1995 DEFAULT OF 3 (options.cpp:73-79
     * GameSpeed). One step up the slider, from a tuned session, and the only one of the
     * five that stands off the 1995 block. It is not cosmetic: in this renderer the
     * setting IS the engine's clock, and SPEED_HZ makes 3 15.0 Hz and 4 18.0 Hz, so this
     * line moves how fast the whole game runs out of the box. 15.0 Hz remains the rate
     * the renderer was built and gated against; a gate that needs it asks for speed 3
     * rather than relying on the default.
     *
     * A MATCH DOES NOT READ THIS BLOCK, and is meant to run at the same pace anyway. The
     * tick rate of a skirmish or network game is a property of the match: it is decided
     * by whoever hosts, travels in the handshake and is adopted by every joiner, so it
     * cannot be one machine's slider. Its own starting value is NM_DEFAULT_SPEED, kept
     * beside the wire field it fills rather than taken from here, and the two are held
     * equal by a compile-time check where both headers are in scope. Moving this number
     * without moving that one gives a player one pace in the campaign and another in
     * multiplayer.
     *
     * THE OTHER FOUR ARE THE 1995 VALUES AND WERE ALREADY WHAT WAS WANTED, so none of
     * them moved: ScrollRate 3, and all three volumes at the top of the slider's travel,
     * which is what options.cpp's 0xFF becomes once the thumb's own width is taken off
     * the bar (DOPT_VOL_TOP, 239). The engine stores a volume as fixed point 0..255. */
    s->speed = DOPT_DEFAULT_SPEED;
    s->scrollrate = 3;
    s->music = DOPT_VOL_TOP;
    s->sound = DOPT_VOL_TOP;
    s->speech = DOPT_VOL_TOP;
}

/* THE CONFIRMATION BOX, measured exactly as msgbox.cpp measures it rather than
   written down as a literal, so it tracks the font the way the real one does.
   msgbox.cpp:157-159 is width = MAX(textwidth,50)+40 and height = textheight+60,
   centred; :123 floors a button at 30; textbtn.cpp:74-84 sizes one to its text + 8.
   Abort and Cancel share the wider of the two, which is msgbox's `bwidth`.
   A function of its own because the question is not fixed any more: the same box asks
   the abort question and the delete question, and it is re-measured whenever cfkind
   changes, from the font dopt_layout kept. */
static void dopt_confirm_layout(DOPT_State *st)
{
    const DB_Font *f = st->font;
    int mw, aw, cw;
    if (!f) return;
    mw = db_string_width(f, dopt_confirm_msg(st), DB_FONT6_XSPACING);
    /* The wider of the two lines is what the box has to hold. */
    {
        const char *m2 = dopt_confirm_msg2(st);
        if (m2) {
            const int w2 = db_string_width(f, m2, DB_FONT6_XSPACING);
            if (w2 > mw) mw = w2;
        }
    }
    aw = db_string_width(f, dopt_confirm_yes(st), DB_FONT6_XSPACING) + 8;
    cw = db_string_width(f, dopt_confirm_no(st), DB_FONT6_XSPACING) + 8;
    if (aw < DOPT_CF_BTN_MIN) aw = DOPT_CF_BTN_MIN;
    if (cw < DOPT_CF_BTN_MIN) cw = DOPT_CF_BTN_MIN;
    st->cfbw = cw > aw ? cw : aw;
    st->cf3w = db_string_width(f, DOPT_CF_RESTART_S, DB_FONT6_XSPACING) + 8;
    if (st->cf3w < DOPT_CF_BTN_MIN) st->cf3w = DOPT_CF_BTN_MIN;
    st->cfw = (mw < DOPT_CF_MIN_W ? DOPT_CF_MIN_W : mw) + DOPT_CF_PAD_W;
    st->cfh = (f->maxh + DB_FONT6_YSPACING) + DOPT_CF_PAD_H;
    /* msgbox.cpp:159 is textheight + 60, and textheight is however many lines the
       question runs to. One extra row, so the buttons keep their own margin. */
    if (dopt_confirm_msg2(st)) st->cfh += f->maxh + DB_FONT6_YSPACING;
    st->cfx = (DOPT_SCREEN_W - st->cfw) / 2;
    st->cfy = (DOPT_SCREEN_H - st->cfh) / 2;
}

/* ------------------------------------------------------------------------ *
 * THE SLOT DIALOG. See the header block in dosopt.h for the 1995 routine it
 * transcribes and the numbers.
 * ------------------------------------------------------------------------ */

static int dopt_sl_list_h(const DOPT_State *st)
{
    return st->sl.mode == DOPT_SL_SAVE ? DOPT_SL_LIST_H_SAVE : DOPT_SL_LIST_H;
}

static int dopt_sl_rows_shown(const DOPT_State *st)
{
    return dopt_sl_list_h(st) / DOPT_SL_ROW_H;
}

/* Keep the highlighted row inside the well: ListClass::Set_Selected_Index's own
   rule, the one the jukebox's dopt_track_show already applies. */
static void dopt_sl_show(DOPT_State *st)
{
    const int shown = dopt_sl_rows_shown(st);
    if (st->sl.sel < st->sl.top) st->sl.top = st->sl.sel;
    if (st->sl.sel >= st->sl.top + shown) st->sl.top = st->sl.sel - shown + 1;
    if (st->sl.top < 0) st->sl.top = 0;
}

/* loaddlg.cpp:466-495: choosing a row in SAVE mode copies its description into the
   field, with any leading "(...)" stripped; choosing the empty slot clears it. This
   build fills the empty slot's field with the host's default text instead of leaving
   it empty, so a player who just wants a save can press Save. */
static void dopt_sl_take_descr(DOPT_State *st)
{
    const DOPT_SlotRow *r;
    if (st->sl.mode != DOPT_SL_SAVE || st->sl.sel < 0 || st->sl.sel >= st->sl.nrows) return;
    r = &st->sl.rows[st->sl.sel];
    if (!strcmp(r->text, DOPT_SL_EMPTY_S)) {
        snprintf(st->sl.descr, sizeof st->sl.descr, "%s", st->sl.deflt);
        return;
    }
    {
        const char *src = r->text;
        if (src[0] == '(') {
            const char *close = strchr(src, ')');
            if (close) {
                src = close + 1;
                while (*src == ' ') src++;
            }
        }
        snprintf(st->sl.descr, sizeof st->sl.descr, "%s", src);
    }
}

/* Ask the host for the rows, and in SAVE mode put the empty slot first with the
   lowest file number no row is using (loaddlg.cpp:661-680). Sixteen slots are all the
   index holds, so with all sixteen taken there is no empty row and a save overwrites. */
static void dopt_sl_fill(DOPT_State *st)
{
    DOPT_SlotRow got[DOPT_SL_ROWS_MAX];
    int n = 0, i, k = 0;
    if (st->bind.slots)
        n = st->bind.slots(st->bind.user, got, DOPT_SL_ROWS_MAX - 1);
    if (n < 0) n = 0;
    if (n > DOPT_SL_ROWS_MAX - 1) n = DOPT_SL_ROWS_MAX - 1;
    st->sl.have = n;
    if (st->sl.mode == DOPT_SL_SAVE && n < 16) {
        int num = 0, again = 1;
        while (again) {
            again = 0;
            for (i = 0; i < n; i++)
                if (got[i].slot == num) { num++; again = 1; break; }
        }
        st->sl.rows[k].slot = num;
        snprintf(st->sl.rows[k].text, sizeof st->sl.rows[k].text, "%s", DOPT_SL_EMPTY_S);
        k++;
    }
    for (i = 0; i < n && k < DOPT_SL_ROWS_MAX; i++) st->sl.rows[k++] = got[i];
    st->sl.nrows = k;
    if (st->sl.sel >= k) st->sl.sel = k - 1;
    if (st->sl.sel < 0) st->sl.sel = k > 0 ? 0 : -1;
    dopt_sl_show(st);
}

static void dopt_sl_open(DOPT_State *st, int mode, int from_menu)
{
    st->sl.mode = mode;
    st->sl.from_menu = from_menu;
    st->sl.prev = st->selected;
    st->sl.sel = 0;
    st->sl.top = 0;
    st->sl.pick = -1;
    st->sl.descr[0] = 0;
    dopt_sl_fill(st);
    if (mode == DOPT_SL_SAVE) dopt_sl_take_descr(st);
    st->page = DOPT_PAGE_SLOTS;
    /* loaddlg.cpp:247-249: the edit field takes the focus in SAVE mode; the list has
       the first row current in every mode. */
    st->selected = (mode == DOPT_SL_SAVE) ? DOPT_SL_EDIT : DOPT_SL_LIST;
    st->pressed = -1;
}

/* Back to wherever the dialog was opened from. */
static int dopt_sl_close(DOPT_State *st)
{
    if (st->sl.from_menu) {
        st->pressed = -1;
        return DOPT_ACT_RESUME;
    }
    st->page = DOPT_PAGE_OPTIONS;
    st->selected = st->sl.prev;
    st->pressed = -1;
    return DOPT_ACT_NONE;
}

void dopt_bind_slots(DOPT_State *st, int (*slots)(void *, DOPT_SlotRow *, int))
{
    if (!st) return;
    st->bind.slots = slots;
    /* How many there are decides whether Load and Delete are live on the pause page,
       so the count is taken now rather than on the first click. */
    {
        DOPT_SlotRow got[DOPT_SL_ROWS_MAX];
        st->sl.have = slots ? slots(st->bind.user, got, DOPT_SL_ROWS_MAX - 1) : 0;
        if (st->sl.have < 0) st->sl.have = 0;
    }
}

void dopt_slots_default(DOPT_State *st, const char *text)
{
    if (!st) return;
    snprintf(st->sl.deflt, sizeof st->sl.deflt, "%s", text ? text : "");
}

void dopt_slots_reload(DOPT_State *st)
{
    if (!st) return;
    dopt_sl_fill(st);
    /* loaddlg.cpp:456-458: a delete that emptied the list ends the dialog. */
    if (st->page == DOPT_PAGE_SLOTS && st->sl.nrows == 0)
        dopt_sl_close(st);
}

void dopt_open_slots(DOPT_State *st, const DB_Pack *p, int mode)
{
    dopt_open(st, p);
    dopt_sl_open(st, mode, 1);
}

int dopt_slot_pick(const DOPT_State *st) { return st ? st->sl.pick : -1; }
const char *dopt_slot_descr(const DOPT_State *st) { return st ? st->sl.descr : ""; }

const char *dopt_slot_row_text(const DOPT_State *st, int row)
{
    if (!st || row < 0 || row >= st->sl.nrows) return 0;
    return st->sl.rows[row].text;
}

void dopt_text(DOPT_State *st, const char *utf8)
{
    if (!st || !utf8 || st->page != DOPT_PAGE_SLOTS || st->sl.mode != DOPT_SL_SAVE)
        return;
    for (; *utf8; utf8++) {
        const unsigned char c = (unsigned char)*utf8;
        size_t n = strlen(st->sl.descr);
        /* EditClass::Handle_Key (edit.cpp): printable only, no leading space, at most
           MaxLength, and never wider than the field less two. ALPHANUMERIC lets
           letters, digits and spaces through and nothing else. */
        if (c < ' ' || c > '~') continue;
        if (c == ' ' && n == 0) continue;
        if (c != ' ' && !isalnum(c)) continue;
        if (n >= DOPT_SL_DESCR_MAX) break;
        if (st->font) {
            char probe[DOPT_SL_DESCR_MAX + 2];
            memcpy(probe, st->sl.descr, n);
            probe[n] = (char)c;
            probe[n + 1] = 0;
            if (db_string_width(st->font, probe, DB_FONT6_XSPACING) >= DOPT_SL_EDIT_W - 2)
                break;
        }
        st->sl.descr[n] = (char)c;
        st->sl.descr[n + 1] = 0;
    }
}

/* ------------------------------------------------------------------------ *
 * RESTATE: the wrap and the box.
 *
 * dialog.cpp:205-247 Format_Window_String walks the string adding up character
 * widths until a line reaches `maxlinelen`, backs up to the last space and puts a
 * break there. That is a greedy word wrap with the lines measured by the same
 * font that prints them, so it is done with db_string_width on the candidate line
 * rather than by summing per-character widths: the answer is the same and the
 * width the box is sized from is the width that will be drawn. A character below
 * ' ' in the source text is a forced break there, as in 1995. A single word wider
 * than the wrap is cut at the width and continued on the next line (1995 gave it a
 * line of its own and let the box grow; see the note at the cut below).
 * ------------------------------------------------------------------------ */
static void dopt_restate_wrap(DOPT_State *st, const DB_Font *f)
{
    const char *src = st->brief;
    char line[DOPT_R_TEXT_MAX], word[DOPT_R_TEXT_MAX], cand[DOPT_R_TEXT_MAX * 2];
    int linelen = 0, out = 0;

    st->brlines = 0;
    st->brwrap[0] = 0;
    if (!f || !src[0])
        return;
    line[0] = 0;
    for (;;) {
        int wl = 0, forced = 0;
        /* the next word: up to a space, a control character or the end */
        while (*src == ' ')
            src++;
        while (*src && *src != ' ' && (unsigned char)*src >= ' ' && wl < DOPT_R_TEXT_MAX - 1)
            word[wl++] = *src++;
        word[wl] = 0;
        /* A WORD WIDER THAN THE WRAP is cut at the width and the rest goes back for the
           next line. 1995 gave such a word a line of its own and let the box grow to it,
           which no briefing on either disc ever asks for; a notice carrying a file path
           does, and a box sized to a 60 character path is wider than the plate. The cut
           piece ends its line, so the remainder starts the next one. */
        if (wl > 0 && db_string_width(f, word, DB_FONT6_XSPACING) >= DOPT_R_WRAP_W) {
            int k, keep = 1;
            for (k = 1; k < wl; k++) {
                memcpy(cand, word, (size_t)k);
                cand[k] = 0;
                if (db_string_width(f, cand, DB_FONT6_XSPACING) >= DOPT_R_WRAP_W)
                    break;
                keep = k;
            }
            src -= wl - keep;
            wl = keep;
            word[wl] = 0;
            forced = 1;
        } else if (*src && (unsigned char)*src < ' ') {
            forced = 1;
            src++;
        }
        if (wl > 0) {
            if (linelen > 0) {
                sprintf(cand, "%s %s", line, word);
                if (db_string_width(f, cand, DB_FONT6_XSPACING) >= DOPT_R_WRAP_W) {
                    /* the line is full: emit it and start again with this word */
                    if (st->brlines < DOPT_R_MAX_LINES && out + linelen + 1 < DOPT_R_TEXT_MAX) {
                        st->brline[st->brlines++] = out;
                        memcpy(st->brwrap + out, line, (size_t)linelen + 1);
                        out += linelen + 1;
                    }
                    strcpy(line, word);
                    linelen = wl;
                } else {
                    strcpy(line, cand);
                    linelen = (int)strlen(line);
                }
            } else {
                strcpy(line, word);
                linelen = wl;
            }
        }
        if (forced || !*src) {
            if (linelen > 0 && st->brlines < DOPT_R_MAX_LINES
                && out + linelen + 1 < DOPT_R_TEXT_MAX) {
                st->brline[st->brlines++] = out;
                memcpy(st->brwrap + out, line, (size_t)linelen + 1);
                out += linelen + 1;
            }
            line[0] = 0;
            linelen = 0;
            if (!*src)
                break;
        }
    }
}

/* msgbox.cpp:150-158: width is the widest line floored at 50 plus 40, height is the
   lines plus 60 (two buttons or one, it is 60 whenever there is any button), and the
   box is centred on the screen. msgbox.cpp:118-127: every button is the widest
   label plus 8, floored at 30, and they all share that width. */
static void dopt_restate_layout(DOPT_State *st, const DB_Font *f)
{
    int i, mw = 0, lh, bw, w2;

    dopt_restate_wrap(st, f);
    if (!f) {
        st->rw = st->rh = st->rx = st->ry = st->rbw = 0;
        return;
    }
    for (i = 0; i < st->brlines; i++) {
        const int w = db_string_width(f, st->brwrap + st->brline[i], DB_FONT6_XSPACING);
        if (w > mw) mw = w;
    }
    lh = f->maxh + DB_FONT6_YSPACING;
    st->rw = (mw < DOPT_CF_MIN_W ? DOPT_CF_MIN_W : mw) + DOPT_CF_PAD_W;
    st->rh = st->brlines * lh + DOPT_CF_PAD_H;
    st->rx = (DOPT_SCREEN_W - st->rw) / 2;
    st->ry = (DOPT_SCREEN_H - st->rh) / 2;
    bw = db_string_width(f, st->rvideo ? DOPT_R_VIDEO_S : DOPT_R_OK_S, DB_FONT6_XSPACING) + 8;
    w2 = st->rvideo ? db_string_width(f, DOPT_R_OPTIONS_S, DB_FONT6_XSPACING) + 8 : 0;
    if (w2 > bw) bw = w2;
    if (bw < DOPT_CF_BTN_MIN) bw = DOPT_CF_BTN_MIN;
    st->rbw = bw;
}

void dopt_set_briefing(DOPT_State *st, const char *text, int video)
{
    if (!st) return;
    if (text) {
        /* Cut where the engine's own buffer cuts: it copies with strncpy into
           char[512] (scenarioini.cpp:472, ini.cpp Get_TextBlock's len). */
        size_t n = strlen(text);
        if (n > DOPT_R_TEXT_MAX - 1) n = DOPT_R_TEXT_MAX - 1;
        memcpy(st->brief, text, n);
        st->brief[n] = 0;
    } else {
        st->brief[0] = 0;
    }
    st->rvideo = video ? 1 : 0;
    st->brlines = 0;   /* re-wrapped on the next dopt_layout */
}

const char *dopt_brief_line(const DOPT_State *st, int i)
{
    if (!st || i < 0 || i >= st->brlines) return NULL;
    return st->brwrap + st->brline[i];
}

void dopt_open_notice(DOPT_State *st, const DB_Pack *p, const char *caption,
                      const char *text)
{
    if (!st) return;
    dopt_open(st, p);
    /* The box is the objective box with no movie: dopt_set_briefing stores the text
       and dopt_layout wraps it and sizes the box, exactly as Restate is measured. */
    dopt_set_briefing(st, text, 0);
    snprintf(st->ncaption, sizeof st->ncaption, "%s", caption ? caption : "");
    dopt_layout(st, p);
    st->page = DOPT_PAGE_NOTICE;
    st->selected = DOPT_R_LEFT;   /* msgbox.cpp:216 curbutton = 0 */
    st->pressed = -1;
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
        /* THROUGH THE LABEL RESOLVER, not the table. One row's name depends on the
           game it is in, and measuring the table's fixed string while DRAWING the
           resolver's answer is how a button ends up narrower than the word in it. */
        w = db_string_width(f, dopt_item_label(st, i), DB_FONT6_XSPACING) + 8;
        if (w > maxw)
            maxw = w;
    }
    st->btnw = maxw > DOPT_MIN_BTN_W ? maxw : DOPT_MIN_BTN_W;
    st->okw = db_string_width(f, dopt_ctrl_label[DOPT_C_OK], DB_FONT6_XSPACING) + 8;
    if (st->okw < 40)
        st->okw = 40;
    /* EVERY ROW'S LABEL, MEASURED: a drop row's value box starts after ITS label and
       runs to the row's edge, rather than at one right-aligned width for all of them.
       One width, taken from the widest art entry, put "Resolution", "UI scaling" and
       "Perspective" under the plate; per row, every label clears its box and every box
       still holds its widest entry ("Remastered Textures" against the short TERRAIN),
       which is goptions.cpp's own measure-the-widest rule applied to both halves. */
    {
        int k;
        for (k = 0; k < DOPT_VE_COUNT; k++)
            st->labw[k] = db_string_width(f, dopt_vis_elem[k], DB_FONT6_XSPACING);
    }
    st->chresetw = db_string_width(f, dopt_cheat_label(DOPT_CH_RESET),
                                   DB_FONT6_XSPACING) + 8;
    if (st->chresetw < DOPT_MIN_BTN_W)
        st->chresetw = DOPT_MIN_BTN_W;

    st->font = f;   /* kept so a question that changes later can be re-measured */
    dopt_confirm_layout(st);
    dopt_restate_layout(st, f);
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
    st->advTop = 0;
    /* -1 and not 0: texdrop names the ROW whose list is open, and 0 is a real row. */
    st->texdrop = -1;
    st->texhot = -1;
    st->restop = 0;
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

/* ------------------------------------------------------------------------ *
 * THE ADVANCED PAGE'S SCROLL BAR, which is SliderClass in list mode.
 *
 * In the 1995 slider's terms (list.cpp:595-597): MaxValue is the number of elements,
 * Thumb is the number of rows the well shows, and CurValue is advTop. Every function
 * below is one of that class's, done in plain integers on the vertical axis.
 * ------------------------------------------------------------------------ */

/* The bottom of travel. Floored at zero so a list shorter than its own well is not a
 * special case anywhere else. */
static int dopt_adv_last(void)
{
    const int last = DOPT_VE_COUNT - DOPT_A_VIEW_ROWS;
    return last > 0 ? last : 0;
}

static void dopt_adv_clamp(DOPT_State *st)
{
    const int last = dopt_adv_last();
    if (st->advTop > last)
        st->advTop = last;
    if (st->advTop < 0)
        st->advTop = 0;
}

/* SliderClass::Recalc_Thumb, slider.cpp:181-188, vertical: the thumb is the same
 * fraction of the well that the well is of the list, floored at 4 (slider.cpp:185), and
 * its start is pulled back so it cannot hang off the end (slider.cpp:187). */
static void dopt_adv_thumb(const DOPT_State *st, int *ty, int *th)
{
    int size = (DOPT_A_BAR_H * DOPT_A_VIEW_ROWS) / DOPT_VE_COUNT;
    int start;
    if (size < DOPT_A_THUMB_MIN)
        size = DOPT_A_THUMB_MIN;
    if (size > DOPT_A_BAR_H)
        size = DOPT_A_BAR_H;
    start = (DOPT_A_BAR_H * st->advTop) / DOPT_VE_COUNT;
    if (start > DOPT_A_BAR_H - size)
        start = DOPT_A_BAR_H - size;
    if (start < 0)
        start = 0;
    *ty = DOPT_A_BAR_Y + start;
    *th = size;
}

/* GaugeClass::Pixel_To_Value, gauge.cpp:143-190, on the other axis: where the TOP of the
 * thumb lands is what decides the top row, so the grab offset inside the thumb is taken
 * off first and the bar does not jump under the hand at the moment it is seized. */
static void dopt_adv_drag(DOPT_State *st, int my)
{
    int top = my - st->dragdiff - DOPT_A_BAR_Y;
    if (top < 0)
        top = 0;
    st->advTop = (top * DOPT_VE_COUNT) / DOPT_A_BAR_H;
    dopt_adv_clamp(st);
}

/* Keep the highlighted row inside the well after the keyboard walk moved it, which is
 * the same job dopt_track_show does for the jukebox. OK and the bar are not in the well,
 * so they leave it where it is. */
static void dopt_adv_show(DOPT_State *st)
{
    if (st->selected < 0 || st->selected >= DOPT_VE_COUNT)
        return;
    if (st->selected < st->advTop)
        st->advTop = st->selected;
    if (st->selected >= st->advTop + DOPT_A_VIEW_ROWS)
        st->advTop = st->selected - DOPT_A_VIEW_ROWS + 1;
    dopt_adv_clamp(st);
}

/* THE RESOLUTION LIST'S OWN SCROLL. It holds up to DOPT_RES_MAX entries and shows
 * DOPT_RES_VIEW, so it has a first visible entry (restop), a clamp, a "keep the current
 * entry in view" and a thumb, each the Advanced well's own rule restated for a list whose
 * length is only known at run time. */
static int dopt_res_shown(const DOPT_State *st)
{
    const int n = st->vis.nres > 0 ? st->vis.nres : 0;
    return n < DOPT_RES_VIEW ? n : DOPT_RES_VIEW;
}

static void dopt_res_clamp(DOPT_State *st)
{
    const int last = st->vis.nres - dopt_res_shown(st);
    if (st->restop > last)
        st->restop = last;
    if (st->restop < 0)
        st->restop = 0;
}

static void dopt_res_show(DOPT_State *st)
{
    const int shown = dopt_res_shown(st);
    if (st->vis.residx < st->restop)
        st->restop = st->vis.residx;
    if (st->vis.residx >= st->restop + shown)
        st->restop = st->vis.residx - shown + 1;
    dopt_res_clamp(st);
}

void dopt_scroll(DOPT_State *st, int delta)
{
    int last;
    /* THE OPEN RESOLUTION LIST TAKES THE WHEEL FIRST, and swallows it even when it has
       nothing to scroll: the Advanced arm below would otherwise move the well, and the
       row's box with it, out from under the list that is open on it. */
    if (st->texdrop == DOPT_VE_RESOLUTION) {
        st->restop += delta;
        dopt_res_clamp(st);
        return;
    }
    /* The Advanced page's column is the second thing on this screen that scrolls, and it
       goes through the same door as the first rather than growing a door of its own. */
    if (st->page == DOPT_PAGE_ADVANCED) {
        st->advTop += delta;
        dopt_adv_clamp(st);
        return;
    }
    /* And the slot list, the third. */
    if (st->page == DOPT_PAGE_SLOTS) {
        last = st->sl.nrows - dopt_sl_rows_shown(st);
        if (last < 0) last = 0;
        st->sl.top += delta;
        if (st->sl.top < 0) st->sl.top = 0;
        if (st->sl.top > last) st->sl.top = last;
        return;
    }
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

void dopt_bind_visuals_reset(DOPT_State *st, void (*resetvis)(void *, DOPT_Visuals *))
{
    if (!st) return;
    st->bind.resetvis = resetvis;
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
    /* DOPT_CH_SUPER was missing from this list. It happened to read
       as off because the only caller's struct is a zeroed static, but "Reset to
       Defaults" could not turn Unlock Superweapons back OFF once it was on -- the one
       job the button has. Every switch is written here now, on purpose. */
    c->on[DOPT_CH_SUPER]    = 0;
    c->on[DOPT_CH_BUILDANY] = 0;
    c->on[DOPT_CH_FOG]      = 1;  /* the ordinary game: the map starts hidden */
    c->on[DOPT_CH_INVULN]   = 0;
}

void dopt_set_cheats_locked(DOPT_State *st, int locked)
{
    if (!st)
        return;
    st->cheats_locked = locked ? 1 : 0;
}

/* WHAT KIND OF GAME THIS IS. Fed from the renderer every time the dialog opens, exactly
   as cheats_locked is: the dialog itself knows nothing about matches, and this is the one
   channel through which it is told. */
void dopt_set_match(DOPT_State *st, int match, int surrendered)
{
    if (!st)
        return;
    st->match = match ? 1 : 0;
    st->surrendered = surrendered ? 1 : 0;
    /* A layout measured against "Abort Mission" is the wrong width for "Leave Match", and
       the measurement is cached. */
    st->laidout = 0;
}

/* THE THREE FACES OF ONE ROW. A campaign mission aborts; a match is surrendered; and once
   you have surrendered the only thing left is to walk out. Read by the label, by the
   width measurement and by the activation, so those three can never disagree. */
const char *dopt_abort_label(const DOPT_State *st)
{
    if (!st || !st->match) return "Abort Mission";
    return st->surrendered ? "Leave Match" : "Surrender";
}

/* The confirmation's question and the button that answers it, chosen the same way. */
const char *dopt_confirm_msg(const DOPT_State *st)
{
    if (st && st->cfkind == 1) return DOPT_SL_DELQ_S;   /* loaddlg.cpp:450 */
    if (!st || !st->match) return DOPT_CF_MSG;
    return st->surrendered ? DOPT_CF_MSG_LEAVE : DOPT_CF_MSG_SURR;
}

const char *dopt_confirm_yes(const DOPT_State *st)
{
    if (st && st->cfkind == 1) return DOPT_CF_YES_S;    /* TXT_YES, loaddlg.cpp:450 */
    if (!st || !st->match) return DOPT_CF_ABORT_S;
    /* The question already names the deed ("Do you want to surrender?"), so the button
       that agrees with it says Yes rather than saying the verb a second time. */
    return DOPT_CF_YES_S;
}

/* The second line of the question, NULL when there is only one. Every measurement and
   every draw asks this rather than testing the state itself, so a question that grows a
   second line cannot be sized on one and drawn on two. */
const char *dopt_confirm_msg2(const DOPT_State *st)
{
    if (!st || !st->match || st->cfkind == 1) return 0;
    return st->surrendered ? DOPT_CF_MSG_LEAVE2 : 0;
}

/* The button that declines, which is Cancel outside a match and No inside one, and No
   under the delete question (TXT_NO, loaddlg.cpp:450). */
const char *dopt_confirm_no(const DOPT_State *st)
{
    if (st && st->cfkind == 1) return DOPT_CF_NO_S;
    return (st && st->match) ? DOPT_CF_NO_S : DOPT_CF_CANCEL_S;
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
    /* THE SLOT DIALOG: loaddlg.cpp:120-147, every number derived in dosopt.h. */
    if (st->page == DOPT_PAGE_SLOTS) {
        switch (item) {
        case DOPT_SL_LIST:
            *x = DOPT_SL_LIST_X; *y = DOPT_SL_LIST_Y;
            *w = DOPT_SL_LIST_W; *h = dopt_sl_list_h(st);
            return 1;
        case DOPT_SL_EDIT:
            if (st->sl.mode != DOPT_SL_SAVE) return 0;
            *x = DOPT_SL_EDIT_X; *y = DOPT_SL_EDIT_Y;
            *w = DOPT_SL_EDIT_W; *h = DOPT_SL_EDIT_H;
            return 1;
        case DOPT_SL_OK:
            *x = DOPT_SL_BTN_X; *y = DOPT_SL_BTN_Y;
            *w = DOPT_SL_BTN_W; *h = DOPT_SL_BTN_H;
            return 1;
        case DOPT_SL_CANCEL:
            *x = DOPT_SL_CANCEL_X; *y = DOPT_SL_BTN_Y;
            *w = DOPT_SL_BTN_W; *h = DOPT_SL_BTN_H;
            return 1;
        default:
            return 0;
        }
    }
    /* THE RESTATE BOX'S BUTTONS, the same msgbox.cpp arithmetic as the confirmation
       below: button1 at x+10, button2 at x + width - (bwidth + 10), and a lone button
       centred (msgbox.cpp:177 `(numbuttons == 1) ? ((width - bwidth) >> 1) : 10`). */
    if (st->page == DOPT_PAGE_RESTATE || st->page == DOPT_PAGE_NOTICE) {
        const int bh = DOPT_BTN_H;
        const int by = st->ry + st->rh - (bh + DOPT_CF_EDGE);
        if (item < 0 || item >= DOPT_R_COUNT || st->rw <= 0)
            return 0;
        *y = by; *h = bh; *w = st->rbw;
        if (item == DOPT_R_LEFT) {
            *x = st->rvideo ? st->rx + DOPT_CF_EDGE : st->rx + ((st->rw - st->rbw) >> 1);
            return 1;
        }
        if (!st->rvideo)
            return 0;
        *x = st->rx + st->rw - (st->rbw + DOPT_CF_EDGE);
        return 1;
    }
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

    if (st->page == DOPT_PAGE_GAMEPLAY) {
        if (item < 0 || item >= DOPT_G_COUNT)
            return 0;
        if (item == DOPT_G_OK) {
            *w = st->okw ? st->okw : DOPT_MIN_BTN_W;
            *h = DOPT_GC_OK_H;
            *x = (DOPT_SCREEN_W - *w) / 2;   /* gamedlg.cpp:151, as V_OK and A_OK do */
            *y = DOPT_GC_OK_Y;
            return 1;
        }
        /* The whole row is the target, not the seven pixel box, for the reason the
           Advanced page gives: a checkbox you have to hit within seven pixels is a
           checkbox nobody uses. */
        *x = DOPT_A_BOX_X;
        *y = DOPT_G_TOP + DOPT_G_STEP * item;
        *w = DOPT_G_ROW_W;
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
        if (item == DOPT_A_BAR) {
            *x = DOPT_A_BAR_X; *y = DOPT_A_BAR_Y;
            *w = DOPT_A_BAR_W; *h = DOPT_A_BAR_H;
            return 1;
        }
        {
            /* A ROW OUTSIDE THE WELL HAS NO RECTANGLE AT ALL, and that is the whole of
               the scrolling. dopt_hit_test skips an item that answers with no rectangle,
               so a row the player cannot see is a row no click can reach; the row loop in
               dopt_draw_advanced skips one too, so nothing is painted outside the well.
               Both come free rather than needing a clip. */
            const int row = item - st->advTop;
            if (row < 0 || row >= DOPT_A_VIEW_ROWS)
                return 0;
            /* The whole row is the target, not just the little box: a checkbox you have
               to hit within seven pixels is a checkbox nobody uses. It stops short of
               the bar, which is list.cpp:566's own `Width -= ScrollGadget.Width`. */
            *x = DOPT_A_BOX_X;
            *y = DOPT_A_TOP + DOPT_A_STEP * row;
            *w = DOPT_A_ROW_W;
            *h = DOPT_A_BOX;
            return 1;
        }
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
        /* THE SCROLL BAR IS NOT A STOP ON THE WALK. gauge.cpp:59 gives a gauge
           LEFTHELD|LEFTPRESS|LEFTRELEASE and no KEYBOARD, so 1995's list slider is not
           in the tab order either. Landing on it would give the player a highlighted
           control that Enter does nothing with. */
        if (st->page == DOPT_PAGE_ADVANCED && k == DOPT_A_BAR)
            continue;
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

/* The DOPT_VE_TEXSET drop list. Defined further down with the rest of the drawing, and
   declared here because the input handlers above it are its first users. */
static void dopt_vis_apply(DOPT_State *st);
static int  dopt_texset_hit(const DOPT_State *st, int mx, int my);
static int  dopt_texset_pickable(const DOPT_State *st, int which);
static int  dopt_is_droprow(int item);
static int  dopt_drop_value(const DOPT_State *st, int item);
static void dopt_drop_set(DOPT_State *st, int item, int v);
static void dopt_res_thumb(const DOPT_State *st, int *ty, int *th);

int dopt_press(DOPT_State *st, int mx, int my)
{
    int hit;

    /* THE DROP LIST FIRST, because it is drawn over the rows and has to take their
       clicks back. An entry that cannot be chosen swallows the click and leaves the
       list open, which is what lets the player read the tooltip rather than having the
       list shut in their face. A click anywhere else closes it and then falls through
       to the normal handling, so one click can close the list and press what is under
       the pointer. */
    if (dopt_is_droprow(st->texdrop)) {
        int bx, by, bw, bh, pick;
        /* THE RESOLUTION LIST'S SLIDER WELL, ahead of its entries: SliderClass::Action
           again (slider.cpp:212-246), a press above the thumb bumps one listful up and a
           press below it one listful down. There is no drag on this thumb; the wheel and
           the two halves of the well are the whole of it, and a press on the thumb
           itself does nothing. */
        if (dopt_res_bar_rect(st, &bx, &by, &bw, &bh)
            && mx >= bx && mx < bx + bw && my >= by && my < by + bh) {
            int ty, th;
            dopt_res_thumb(st, &ty, &th);
            if (my < ty)
                st->restop -= dopt_res_shown(st);
            else if (my >= ty + th)
                st->restop += dopt_res_shown(st);
            dopt_res_clamp(st);
            st->lastmy = my;
            st->drag = -1;
            st->pressed = -1;
            return DOPT_ACT_NONE;
        }
        pick = dopt_texset_hit(st, mx, my);
        if (pick >= 0) {
            st->lastmy = my;
            st->drag = -1;
            st->pressed = -1;
            if (!dopt_texset_pickable(st, pick))
                return DOPT_ACT_NONE;
            dopt_drop_set(st, st->texdrop, pick);
            st->texdrop = -1;
            st->texhot = -1;
            dopt_vis_apply(st);
            return DOPT_ACT_NONE;
        }
        st->texdrop = -1;
        st->texhot = -1;
    }

    hit = dopt_hit_test(st, mx, my);

    st->lastmy = my;
    st->drag = -1;
    if (hit < 0) {
        st->pressed = -1;
        return DOPT_ACT_NONE;
    }

    /* THE SCROLL BAR, and it is SliderClass::Action (slider.cpp:212-246) rather than the
       gauge press below it: a click ABOVE the thumb bumps one wellful up, a click BELOW
       bumps one wellful down, and a click ON the thumb sticks to it. Bump is
       Set_Value(CurValue -/+ Thumb) (slider.cpp:267-273), and Thumb here is the number of
       rows the well shows. The selection is deliberately left where it was: this gadget
       is not a stop on the keyboard walk, so moving the highlight onto it would strand
       the walk on a control it cannot reach again. */
    if (st->page == DOPT_PAGE_ADVANCED && hit == DOPT_A_BAR) {
        int ty, th;
        dopt_adv_thumb(st, &ty, &th);
        if (my < ty) {
            st->advTop -= DOPT_A_VIEW_ROWS;
            dopt_adv_clamp(st);
        } else if (my >= ty + th) {
            st->advTop += DOPT_A_VIEW_ROWS;
            dopt_adv_clamp(st);
        } else {
            st->drag = hit;
            st->dragdiff = my - ty; /* GaugeClass::ClickDiff, on the other axis */
        }
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
    /* The entry under the pointer, which is what the tooltip and the highlight follow.
       Recomputed on every motion and cleared when the list is shut, so a tooltip can
       never outlive the list that explains it. */
    st->texhot = dopt_is_droprow(st->texdrop) ? dopt_texset_hit(st, mx, my) : -1;

    /* The one drag on this screen that reads y instead of x. Everything else here is a
       horizontal gauge, which is why `my` used to be discarded outright. */
    if (st->page == DOPT_PAGE_ADVANCED && st->drag == DOPT_A_BAR) {
        dopt_adv_drag(st, my);
        return DOPT_ACT_NONE;
    }
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
    /* THE RESTATE BOX'S ANSWERS. Video closes everything and plays (goptions.cpp:375-390
       ends the dialog loop after the movie); Options, or the lone OK, puts the pause
       page back exactly as it was. */
    /* THE NOTICE has one answer and it is "close me", whichever way it was given. */
    if (st->page == DOPT_PAGE_NOTICE) {
        st->pressed = -1;
        return DOPT_ACT_RESUME;
    }
    if (st->page == DOPT_PAGE_RESTATE) {
        if (item == DOPT_R_LEFT && st->rvideo) {
            st->pressed = -1;
            return DOPT_ACT_VIDEO;
        }
        st->page = DOPT_PAGE_OPTIONS;
        st->selected = st->rprev;
        st->pressed = -1;
        return DOPT_ACT_NONE;
    }
    /* THE CONFIRMATION'S THREE ANSWERS. Abort and Restart both leave; Cancel puts the
       pause page back exactly as it was, which is what msgbox.cpp's caller does when
       Process returns the cancel value (goptions.cpp:425-440 simply falls through). */
    if (st->page == DOPT_PAGE_CONFIRM) {
        if (st->cfkind == 1) {
            /* "Delete this file?" Yes names the row for the host and goes back to the
               slot list, which the host then reloads (loaddlg.cpp:450-458). */
            st->page = DOPT_PAGE_SLOTS;
            st->selected = DOPT_SL_OK;
            st->pressed = -1;
            st->cfkind = 0;
            dopt_confirm_layout(st);
            if (item == DOPT_CF_ABORT && st->sl.sel >= 0 && st->sl.sel < st->sl.nrows) {
                st->sl.pick = st->sl.rows[st->sl.sel].slot;
                return DOPT_ACT_DELETE;
            }
            return DOPT_ACT_NONE;
        }
        switch (item) {
        case DOPT_CF_ABORT:
            /* Three outcomes behind one button, matching the three questions it can be
               asked under: abort a mission, resign a match, or leave one already lost. */
            if (!st->match)        return DOPT_ACT_ABORT;
            return st->surrendered ? DOPT_ACT_LEAVE : DOPT_ACT_SURRENDER;
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
    if (st->page == DOPT_PAGE_SLOTS) {
        switch (item) {
        case DOPT_SL_LIST: {
            /* The row under the press, from the y the press recorded, the way the
               jukebox's list finds its track. */
            const int row = st->sl.top + (st->lastmy - DOPT_SL_LIST_Y) / DOPT_SL_ROW_H;
            if (st->lastmy >= DOPT_SL_LIST_Y && row >= 0 && row < st->sl.nrows) {
                st->sl.sel = row;
                dopt_sl_take_descr(st);
            }
            return DOPT_ACT_NONE;
        }
        case DOPT_SL_EDIT:
            return DOPT_ACT_NONE;          /* the click gave it the focus already */
        case DOPT_SL_OK:
            if (dopt_item_disabled(st, item))
                return DOPT_ACT_NONE;      /* ENTER has no guard of its own */
            st->sl.pick = st->sl.rows[st->sl.sel].slot;
            st->pressed = -1;
            if (st->sl.mode == DOPT_SL_LOAD) return DOPT_ACT_LOAD;
            if (st->sl.mode == DOPT_SL_SAVE) return DOPT_ACT_SAVE;
            /* Delete asks first, through the same box the abort uses. */
            st->cfkind = 1;
            dopt_confirm_layout(st);
            st->cfprev = st->selected;
            st->page = DOPT_PAGE_CONFIRM;
            st->selected = DOPT_CF_ABORT;  /* msgbox.cpp:216 curbutton = 0, which is Yes */
            return DOPT_ACT_NONE;
        case DOPT_SL_CANCEL:
        default:
            return dopt_sl_close(st);
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
            /* The page opens at the top of its list, because the selection opens on the
               first row and the two must agree: row 0 highlighted with the well halfway
               down is a page with no visible selection on it. */
            st->advTop = 0;
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
        /* ENTER has no guard of its own, and dopt_open_cheats lands the selection on the
           first switch, so a locked page would otherwise flip it from the keyboard. */
        if (dopt_item_disabled(st, item))
            return DOPT_ACT_NONE;
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
        /* THE DISPLAY TRIPLE IS A RADIO: the clicked one is lit and the other two are
           not, and clicking the lit one changes nothing. Applied at once, so the window
           changes while the page is still up and the player can see what they chose. */
        if (item == DOPT_VE_FULLSCREEN || item == DOPT_VE_WINDOWED || item == DOPT_VE_BORDERLESS) {
            st->vis.dispmode = (item == DOPT_VE_FULLSCREEN) ? DOPT_DISP_FULLSCREEN
                             : (item == DOPT_VE_WINDOWED) ? DOPT_DISP_WINDOWED
                                                          : DOPT_DISP_BORDERLESS;
            st->texdrop = -1;
            st->texhot = -1;
            dopt_vis_apply(st);
            return DOPT_ACT_NONE;
        }
        if (item == DOPT_VE_RESET) {
            st->texdrop = -1;
            st->texhot = -1;
            if (st->bind.resetvis)
                st->bind.resetvis(st->bind.user, &st->vis);
            dopt_vis_apply(st);
            return DOPT_ACT_NONE;
        }
    }
    if (st->page == DOPT_PAGE_ADVANCED && dopt_is_droprow(item)) {
        /* The whole row opens the list, not just the little box -- the same reasoning
           dopt_item_rect gives for making the whole row the checkbox target. Opening one
           closes the other: two open lists would overlap and neither could say which of
           them a click belonged to. */
        if (st->texdrop == item) {
            st->texdrop = -1;
            st->texhot = -1;
        } else {
            dopt_drop_open(st, item);
        }
        return DOPT_ACT_NONE;
    }
    if (st->page == DOPT_PAGE_GAMEPLAY) {
        if (item == DOPT_G_OK) {
            st->page = DOPT_PAGE_OPTIONS;
            st->selected = DOPT_GAMEPLAY;
            st->pressed = -1;
            return DOPT_ACT_NONE;
        }
        if (item >= 0 && item < DOPT_G_TOGGLES) {
            st->gp.on[item] = !st->gp.on[item];
            if (st->bind.applygp)
                st->bind.applygp(st->bind.user, &st->gp);
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
        if (item >= DOPT_VE_SMOOTH && item < DOPT_VE_COUNT) {
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
    case DOPT_GAMEPLAY:
        st->page = DOPT_PAGE_GAMEPLAY;
        st->selected = DOPT_G_SWAPBTN;
        st->pressed = -1;
        return DOPT_ACT_NONE;
    case DOPT_SAVE:
    case DOPT_LOAD:
    case DOPT_DELETE:
        if (dopt_item_disabled(st, item))
            return DOPT_ACT_NONE;         /* gadget.cpp:632, and ENTER has no guard */
        dopt_sl_open(st, item == DOPT_LOAD ? DOPT_SL_LOAD
                         : item == DOPT_SAVE ? DOPT_SL_SAVE : DOPT_SL_DELETE, 0);
        return DOPT_ACT_NONE;
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
    case DOPT_RESTATE:
        if (dopt_item_disabled(st, item))
            return DOPT_ACT_NONE;     /* gadget.cpp:632, and ENTER has no guard */
        /* NO TEXT, ONLY A MOVIE: scenario.cpp:815 shows a box only when there is
           briefing text, and Restate_Mission then returns false, so goptions.cpp:375
           plays the movie with no box in between. */
        if (!st->brief[0])
            return DOPT_ACT_VIDEO;
        st->rprev = st->selected;
        st->page = DOPT_PAGE_RESTATE;
        st->selected = DOPT_R_LEFT;   /* msgbox.cpp:216 curbutton = 0 */
        st->pressed = -1;
        return DOPT_ACT_NONE;
    default:
        return DOPT_ACT_NONE;
    }
}

int dopt_release(DOPT_State *st, int mx, int my)
{
    int hit, was;

    /* Same split as dopt_motion: the bar's drag ends on the y, not the x. Without this
       arm the release would fall into dopt_set_value with the bar's index, which finds
       no slot and silently does nothing -- correct by accident is not correct. */
    if (st->page == DOPT_PAGE_ADVANCED && st->drag == DOPT_A_BAR) {
        dopt_adv_drag(st, my);
        st->drag = -1;
        return DOPT_ACT_NONE;
    }

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
        /* The slot list is walked the same way, and in SAVE mode the field follows
           the row (loaddlg.cpp:466-495 does this on a click; the keyboard is ours). */
        if (st->page == DOPT_PAGE_SLOTS && st->selected == DOPT_SL_LIST &&
            st->sl.nrows > 0) {
            if ((d < 0 && st->sl.sel > 0) || (d > 0 && st->sl.sel < st->sl.nrows - 1)) {
                st->sl.sel += d;
                dopt_sl_show(st);
                dopt_sl_take_descr(st);
                return DOPT_ACT_NONE;
            }
        }
        st->selected = dopt_next_item(st, st->selected, d);
        /* The walk can move the highlight past the bottom of the well, so the well
           follows it. Same rule as the jukebox's dopt_track_show, and it is what makes
           the last element reachable without a wheel or a hand on the bar. */
        if (st->page == DOPT_PAGE_ADVANCED)
            dopt_adv_show(st);
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
        /* loaddlg.cpp:262-276: RETURN on the slot dialog is the action button whatever
           has the focus, and it is what ends typing a description. */
        if (st->page == DOPT_PAGE_SLOTS &&
            (st->selected == DOPT_SL_LIST || st->selected == DOPT_SL_EDIT))
            return dopt_activate(st, DOPT_SL_OK);
        return dopt_activate(st, st->selected);
    case DOPT_KEY_BACKSPACE:
        if (st->page == DOPT_PAGE_SLOTS && st->sl.mode == DOPT_SL_SAVE) {
            const size_t n = strlen(st->sl.descr);
            if (n) st->sl.descr[n - 1] = 0;
        }
        return DOPT_ACT_NONE;
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
        if (st->page == DOPT_PAGE_GAMEPLAY)
            return dopt_activate(st, DOPT_G_OK);
        if (st->page == DOPT_PAGE_CHEATS)
            return dopt_activate(st, DOPT_CH_OK);
        /* On the confirmation, Escape is Cancel and NOT Resume. Leaving it as Resume
           would make the key that means "back out of this question" close the pause
           dialog and drop the player back into a mission they had just asked to leave,
           which is a worse answer than either button gives. */
        if (st->page == DOPT_PAGE_CONFIRM)
            return dopt_activate(st, DOPT_CF_CANCEL);
        /* On the objective box Escape is the way back to the pause page, which is
           the Options button when there are two and the OK when there is one; it is
           never the movie. */
        if (st->page == DOPT_PAGE_RESTATE)
            return dopt_activate(st, st->rvideo ? DOPT_R_RIGHT : DOPT_R_LEFT);
        if (st->page == DOPT_PAGE_NOTICE)
            return dopt_activate(st, DOPT_R_LEFT);
        /* loaddlg.cpp:498-502: KN_ESC on the slot dialog is Cancel. */
        if (st->page == DOPT_PAGE_SLOTS)
            return dopt_activate(st, DOPT_SL_CANCEL);
        return DOPT_ACT_RESUME;
    default:
        return DOPT_ACT_NONE;
    }
}

/* ------------------------------------------------------------------------ *
 * Drawing. Same primitives, same colours and the same two box routines the main
 * menu uses; see menu/dosmenu.c for the walk-through of each one.
 * ------------------------------------------------------------------------ */

/* EXPORTED. The green gauge: BOXSTYLE_GREEN_DOWN's sunken track (dialog.cpp row 6 --
 * filler BKGD, shadow LIGHT, hilite SHADOW) with the bright green filled up to
 * `fill_to_x`, which is an absolute surface column and not a width. Pass anything below
 * x + 1 for an empty gauge. This is the volume slider's own body, and the DATABASE
 * codex's stat bars are drawn with it. */
void dopt_style_track(DB_Surface *s, int x, int y, int w, int h, int fill_to_x)
{
    db_fill_rect(s, x, y, x + w - 1, y + h - 1, DOPT_GREEN_BKGD);
    db_line_h(s, x, x + w - 1, y + h - 1, DOPT_LIGHT_GREEN);
    db_line_v(s, x + w - 1, y, y + h - 1, DOPT_LIGHT_GREEN);
    db_line_h(s, x, x + w - 1, y, DOPT_GREEN_SHADOW);
    db_line_v(s, x, y, y + h - 1, DOPT_GREEN_SHADOW);
    if (fill_to_x >= x + 1)
        db_fill_rect(s, x + 1, y + 1, fill_to_x, y + h - 2, DOPT_BRIGHT_GREEN);
}

/* EXPORTED. The green button plate, and it is the only one in the program: the DATABASE
 * codex (game/codex_mod.h) draws its category tabs with this rather than with a second
 * copy of the same five lines, so the two screens cannot drift apart the day somebody
 * retunes the bevel. Same reason for dopt_style_dialog, dopt_style_caption and
 * dopt_style_track below. */
void dopt_style_plate(DB_Surface *s, int x, int y, int w, int h, int pressed,
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
void dopt_style_dialog(DB_Surface *s, int x, int y, int w, int h)
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
void dopt_style_caption(DB_Surface *s, const DB_Pack *p, const char *text, int x, int y,
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

    dopt_style_plate(s, x, y, w, h, pressed, disabled);
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

    mid = dopt_value_to_pixel(st, ctrl, value);
    dopt_style_track(s, x, y, w, h, mid);

    /* gauge.cpp:357-368: the thumb is pulled back so it cannot hang off the end. */
    max = dopt_value_to_pixel(st, ctrl, dopt_ctrl_max(st, ctrl));
    thumb = mid;
    if (thumb + 4 > max)
        thumb = max - 2;
    dopt_style_plate(s, thumb, y, 4, h, 0, 0);
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
    dopt_style_plate(s, x, y, DOPT_A_BOX, DOPT_A_BOX, on, dopt_item_disabled(st, item));
    if (on)
        db_fill_rect(s, x + 2, y + 2, x + DOPT_A_BOX - 3, y + DOPT_A_BOX - 3,
                     dopt_item_disabled(st, item) ? DOPT_TEXT_DISABLED : DOPT_BRIGHT_GREEN);
}

/* ---- DOPT_VE_TEXSET, the one drop list on the Advanced page ------------------------
 *
 * Three choices where the rest of the page has checkboxes, so it needs a control 1995
 * never drew. It is built out of the same primitives the page already uses -- the
 * button box, GRAD6FNT and the disabled ink -- rather than a new widget vocabulary.
 *
 * The list is drawn LAST and hit-tested FIRST, and that pair is the whole mechanism:
 * drawn last it covers the rows beneath it, tested first it takes their clicks back.
 * Nothing else on the page has to know it exists.
 *
 * REMASTERED IS DRAWN EVEN WHEN IT CANNOT BE CHOSEN, in the disabled ink and with a
 * tooltip explaining what would unlock it. That is the page's own rule (see
 * dopt_item_disabled) and 1995's before it: a disabled gadget rather than a missing
 * one, so a player can see the option exists.
 */

/* Two lines, because one is wider than the 232-pixel dialog it has to sit inside. */
/* Three lines, because the sentence is wider than the 232-pixel dialog it sits in and
   wider than a comfortable share of the 320-pixel screen. Broken at phrase boundaries
   rather than by measure, so it reads. */
#define DOPT_TIP_L1 "Available if you have"
#define DOPT_TIP_L2 "Command & Conquer"
#define DOPT_TIP_L3 "Remastered Collection installed"

/* The DOS entry's own three lines. Two theaters never had a 1995 original, and a pack
   baked before that art existed carries none either; both looked identical from here --
   the entry took the click and the ground did not change. */
#define DOPT_DTIP_L1 "This map's art pack has"
#define DOPT_DTIP_L2 "no 1995 tiles for this"
#define DOPT_DTIP_L3 "theater"

/* A resolution entry larger than the desktop's usable room, under WINDOWED. */
#define DOPT_RTIP_L1 "Larger than the desktop."
#define DOPT_RTIP_L2 "A window this size cannot fit."
#define DOPT_RTIP_L3 "Pick it under True fullscreen."

const char *dopt_texset_label(int which)
{
    return (which >= 0 && which < DOPT_TEX_COUNT) ? dopt_texset_name[which] : "";
}

const char *dopt_drop_label(int item, int which)
{
    if (which < 0 || which >= DOPT_TEX_COUNT) return "";
    return item == DOPT_VE_INFSET ? dopt_infset_name[which] : dopt_texset_name[which];
}

/* THE THREE LINES OF WHICHEVER TIP APPLIES, or 0 for none. The drawing used to print
   DOPT_TIP_L1/2/3 by name, which is why there could only ever be one tip on this screen. */
int dopt_texset_tip_lines(const DOPT_State *st, int which,
                          const char **l1, const char **l2, const char **l3)
{
    /* THE RESOLUTION LIST'S ONE REFUSAL: under WINDOWED, a size the host measured as
       larger than the desktop's usable room. A bordered window that size cannot fit, so
       the entry is greyed with the reason rather than taken and then clamped, and the
       same size is pickable under True fullscreen, where it is a real mode. The Desktop
       entry itself always fits, by construction. */
    if (st && st->texdrop == DOPT_VE_RESOLUTION) {
        if (st->vis.dispmode == DOPT_DISP_WINDOWED && which > 0 && which < st->vis.nres
            && !st->vis.res_fit[which]) {
            *l1 = DOPT_RTIP_L1; *l2 = DOPT_RTIP_L2; *l3 = DOPT_RTIP_L3;
            return 1;
        }
        return 0;
    }
    /* The tips are the ART rows' own. The UI scale and perspective lists share the widget
       and their entry numbers, so a tip keyed on the number alone would grey "-3x"
       because Remastered is not installed. */
    if (st && (st->texdrop == DOPT_VE_UISCALE || st->texdrop == DOPT_VE_PERSPECTIVE))
        return 0;
    if (st && which == DOPT_TEX_REMASTER && !st->vis.remaster_ok) {
        *l1 = DOPT_TIP_L1; *l2 = DOPT_TIP_L2; *l3 = DOPT_TIP_L3;
        return 1;
    }
    /* The DOS entry, and only on the TERRAIN row: the infantry sprites are shipped and
       are never missing, so the same value on that row means something else. */
    if (st && which == DOPT_TEX_DOS && st->texdrop == DOPT_VE_TEXSET && !st->vis.dos_tex_ok) {
        *l1 = DOPT_DTIP_L1; *l2 = DOPT_DTIP_L2; *l3 = DOPT_DTIP_L3;
        return 1;
    }
    return 0;
}

const char *dopt_texset_tooltip(const DOPT_State *st, int which)
{
    const char *a, *b, *c;
    static char joined[128];
    if (!dopt_texset_tip_lines(st, which, &a, &b, &c)) return NULL;
    snprintf(joined, sizeof joined, "%s %s %s", a, b, c);
    return joined;
}

/* THE DESKTOP ENTRY'S OWN LINES: it prints as "Desktop" because the size with the word
   after it did not fit the value box, so the size goes in a tip under the pointer. Two
   lines and an empty third, which dopt_draw_tip prints as a two-line box. */
static int dopt_texset_info_lines(const DOPT_State *st, int which, char *buf, int cap,
                                  const char **l1, const char **l2, const char **l3)
{
    if (!st || st->texdrop != DOPT_VE_RESOLUTION || which != 0 || st->vis.nres <= 0)
        return 0;
    snprintf(buf, (size_t)cap, "%d x %d.", st->vis.res_w[0], st->vis.res_h[0]);
    *l1 = "The desktop is";
    *l2 = buf;
    *l3 = "";
    return 1;
}

const char *dopt_texset_infotip(const DOPT_State *st, int which)
{
    const char *a, *b, *c;
    char size[32];
    static char joined[96];
    if (!dopt_texset_info_lines(st, which, size, (int)sizeof size, &a, &b, &c)) return NULL;
    snprintf(joined, sizeof joined, "%s %s", a, b);
    return joined;
}

static int dopt_texset_pickable(const DOPT_State *st, int which)
{
    const char *a, *b, *c;
    /* PICKABLE IS THE TIP'S OWN ANSWER, so an entry can never be refused without a
       reason or explained without being refused. They were two lists and the DOS entry
       was about to be added to only one of them. */
    return !dopt_texset_tip_lines(st, which, &a, &b, &c);
}

/* THE ROWS THAT ARE DROP LISTS (five now), and where each keeps its value. Everything below is
   written against `item` rather than against the terrain row, so a third list is a line
   here and nothing else. */
static int dopt_is_droprow(int item)
{
    return item == DOPT_VE_TEXSET || item == DOPT_VE_INFSET
        || item == DOPT_VE_RESOLUTION || item == DOPT_VE_UISCALE
        || item == DOPT_VE_PERSPECTIVE;
}

static int dopt_drop_value(const DOPT_State *st, int item)
{
    if (item == DOPT_VE_INFSET) return st->vis.infset;
    if (item == DOPT_VE_RESOLUTION) return st->vis.residx;
    if (item == DOPT_VE_UISCALE) return st->vis.uiscale;
    if (item == DOPT_VE_PERSPECTIVE) return st->vis.perspective;
    return st->vis.texset;
}

static void dopt_drop_set(DOPT_State *st, int item, int v)
{
    if (item == DOPT_VE_INFSET)          st->vis.infset = v;
    else if (item == DOPT_VE_RESOLUTION) st->vis.residx = v;
    else if (item == DOPT_VE_UISCALE)    st->vis.uiscale = v;
    else if (item == DOPT_VE_PERSPECTIVE) st->vis.perspective = v;
    else                                 st->vis.texset = v;
}

int dopt_drop_count(const DOPT_State *st, int item)
{
    if (item == DOPT_VE_RESOLUTION) return (st && st->vis.nres > 0) ? st->vis.nres : 0;
    if (item == DOPT_VE_UISCALE) return DOPT_UI_COUNT;
    if (item == DOPT_VE_PERSPECTIVE) return DOPT_PERSP_COUNT;
    return DOPT_TEX_COUNT;
}

const char *dopt_drop_text(const DOPT_State *st, int item, int which)
{
    static const char *const ui[DOPT_UI_COUNT] = { "1x", "-2x" };
    static const char *const persp[DOPT_PERSP_COUNT] = { "Classic", "Isometric" };
    static char res[24];
    if (item == DOPT_VE_UISCALE)
        return (which >= 0 && which < DOPT_UI_COUNT) ? ui[which] : "";
    if (item == DOPT_VE_PERSPECTIVE)
        return (which >= 0 && which < DOPT_PERSP_COUNT) ? persp[which] : "";
    if (item == DOPT_VE_RESOLUTION) {
        if (!st || which < 0 || which >= st->vis.nres) return "";
        /* THE DESKTOP ENTRY IS THE WORD ALONE. The size with "(desktop)" after it runs
           past the value box and into the scroll bar's column; the size itself is in
           the entry's tip. */
        if (which == 0) return "Desktop";
        snprintf(res, sizeof res, "%d x %d", st->vis.res_w[which], st->vis.res_h[which]);
        return res;
    }
    return dopt_drop_label(item, which);
}

/* The closed value box. No rectangle at all when the texture row is scrolled out of the
   well, which is the same contract dopt_item_rect keeps for every row and is what stops
   an invisible control taking a click. */
static int dopt_texset_box_rect_of(const DOPT_State *st, int item,
                                   int *x, int *y, int *w, int *h)
{
    int rx, ry, rw, rh;
    if (st->page != DOPT_PAGE_ADVANCED || !dopt_is_droprow(item))
        return 0;
    if (!dopt_item_rect(st, item, &rx, &ry, &rw, &rh))
        return 0;
    if (st->labw[item] > 0) {
        /* After this row's own label, to the row's right edge. */
        *x = DOPT_A_LABEL_X + st->labw[item] + DOPT_A_DROP_GAP;
        *w = DOPT_A_BOX_X + DOPT_A_ROW_W - *x;
    } else {
        *w = DOPT_A_DROP_W;                       /* no font: nothing prints anyway */
        *x = DOPT_A_BOX_X + DOPT_A_ROW_W - *w;
    }
    *y = ry - 1;
    *h = DOPT_A_DROP_H;
    return 1;
}

/* One open entry, stacked directly under the value box. The resolution list shows a
   window of DOPT_RES_VIEW entries starting at restop, and an entry outside that window
   has no rectangle, the same contract a row scrolled out of the well keeps; when the
   list holds more than it shows, the entries give up DOPT_A_BAR_W on the right for the
   slider well, list.cpp:566's own `Width -= ScrollGadget.Width`, and the well sits flush
   against them: a gap column would show the page underneath through the open list. */
static int dopt_texset_item_rect_of(const DOPT_State *st, int item, int i,
                                    int *x, int *y, int *w, int *h)
{
    int bx, by, bw, bh, slot = i, shown;
    if (st->texdrop != item || i < 0 || i >= dopt_drop_count(st, item))
        return 0;
    if (!dopt_texset_box_rect_of(st, item, &bx, &by, &bw, &bh))
        return 0;
    shown = dopt_drop_count(st, item);
    if (item == DOPT_VE_RESOLUTION) {
        shown = dopt_res_shown(st);
        slot = i - st->restop;
        if (slot < 0 || slot >= shown)
            return 0;
        if (st->vis.nres > shown)
            bw -= DOPT_A_BAR_W;
    }
    *x = bx; *w = bw; *h = bh;
    {
        /* DOWN when the whole list clears the OK button, else UP from the box: the
           infantry row sits on the well's last visible line, and a list that could only
           open downward there had no entries at all (G202's script found that). */
        if (by + bh + shown * bh <= DOPT_GC_OK_Y - 1)
            *y = by + bh + slot * bh;
        else
            *y = by - (shown - slot) * bh;
        if (*y < DOPT_V_Y + 2)
            return 0;
    }
    return 1;
}

int dopt_texset_box_rect_pub(const DOPT_State *st, int item, int *x, int *y, int *w, int *h)
{
    return dopt_texset_box_rect_of(st, item, x, y, w, h);
}

int dopt_drop_need_pub(const DOPT_State *st, const DB_Pack *p, int item)
{
    const DB_Font *f = p ? db_font(p, "GRAD6FNT") : 0;
    int i, n, w, need = 0;
    if (!f || !dopt_is_droprow(item)) return 0;
    n = dopt_drop_count(st, item);
    for (i = 0; i < n; i++) {
        w = db_string_width(f, dopt_drop_text(st, item, i), DB_FONT6_XSPACING);
        if (w > need) need = w;
    }
    /* The resolution list is the host's and may be empty at layout time; floor it at
       the widest size the format prints with four digits a side. */
    if (item == DOPT_VE_RESOLUTION) {
        w = db_string_width(f, "9999 x 9999", DB_FONT6_XSPACING);
        if (w > need) need = w;
    }
    return need + DOPT_A_DROP_PAD;
}

/* The public face of dopt_texset_item_rect, for the headless driver: a gate has to be
   able to put the pointer on an entry, and the entries are not dialog items. */
int dopt_texset_item_rect_pub(const DOPT_State *st, int item, int i,
                              int *x, int *y, int *w, int *h)
{
    return dopt_texset_item_rect_of(st, item, i, x, y, w, h);
}

void dopt_drop_open(DOPT_State *st, int item)
{
    if (!st || !dopt_is_droprow(item))
        return;
    st->texdrop = item;
    st->texhot = -1;
    st->restop = 0;
    if (item == DOPT_VE_RESOLUTION)
        dopt_res_show(st);
}

/* THE RESOLUTION LIST'S SLIDER WELL: DOPT_A_BAR_W wide, flush against the narrowed
   entries, as tall as the rows the list shows. The Advanced page's own bar is
   the model (dosopt.h, THE WELL, AND THE SCROLL BAR); this one exists only while the
   list holds more than it shows. */
int dopt_res_bar_rect(const DOPT_State *st, int *x, int *y, int *w, int *h)
{
    int ex, ey, ew, eh, ly, hy, i, any = 0;
    const int shown = st ? dopt_res_shown(st) : 0;
    if (!st || st->texdrop != DOPT_VE_RESOLUTION || st->vis.nres <= shown)
        return 0;
    ly = 0; hy = 0; ex = 0; ew = 0; ey = 0; eh = 0;
    for (i = st->restop; i < st->restop + shown; i++) {
        if (!dopt_texset_item_rect_of(st, DOPT_VE_RESOLUTION, i, &ex, &ey, &ew, &eh))
            continue;
        if (!any || ey < ly) ly = ey;
        if (!any || ey + eh > hy) hy = ey + eh;
        any = 1;
    }
    if (!any)
        return 0;
    *x = ex + ew;
    *y = ly;
    *w = DOPT_A_BAR_W;
    *h = hy - ly;
    return 1;
}

/* SliderClass::Recalc_Thumb (slider.cpp:181-188) for this list, the Advanced bar's own
   arithmetic with the list's count in place of DOPT_VE_COUNT. */
static void dopt_res_thumb(const DOPT_State *st, int *ty, int *th)
{
    int x, y, w, h, size, start;
    const int n = st->vis.nres > 0 ? st->vis.nres : 1;
    const int shown = dopt_res_shown(st);
    if (!dopt_res_bar_rect(st, &x, &y, &w, &h)) {
        *ty = 0;
        *th = 0;
        return;
    }
    size = (h * shown) / n;
    if (size < DOPT_A_THUMB_MIN)
        size = DOPT_A_THUMB_MIN;
    if (size > h)
        size = h;
    start = (h * st->restop) / n;
    if (start > h - size)
        start = h - size;
    if (start < 0)
        start = 0;
    *ty = y + start;
    *th = size;
}

/* Which open entry the point is over, or -1. Also answers -1 when the list is shut, so
   every caller can ask without first testing texdrop. */
static int dopt_texset_hit(const DOPT_State *st, int mx, int my)
{
    int i, x, y, w, h;
    if (!dopt_is_droprow(st->texdrop)) return -1;
    for (i = 0; i < dopt_drop_count(st, st->texdrop); i++)
        if (dopt_texset_item_rect_of(st, st->texdrop, i, &x, &y, &w, &h)
            && mx >= x && mx < x + w && my >= y && my < y + h)
            return i;
    return -1;
}


/* The closed control: the current choice, and a small triangle to say it opens. */
static void dopt_draw_texset_box(DB_Surface *s, const DB_Pack *p, const DOPT_State *st,
                                 int item)
{
    const DB_Font *f = db_font(p, "GRAD6FNT");
    unsigned char fp[16];
    int x, y, w, h, i, ink;
    const int open = (st->texdrop == item);

    if (!dopt_texset_box_rect_of(st, item, &x, &y, &w, &h))
        return;
    /* Greyed when the row is refused, so a control that will not take a click does not
       draw as one that will. */
    dopt_style_plate(s, x, y, w, h, open, dopt_item_disabled(st, item));
    ink = dopt_item_disabled(st, item) ? DOPT_TEXT_DISABLED
        : (st->selected == item || open) ? DOPT_TEXT_BRIGHT : DOPT_TEXT_MEDIUM;
    if (f) {
        db_font_palette_grad(fp, (unsigned char)ink, DB_TBLACK);
        db_print(s, f, dopt_drop_text(st, item, dopt_drop_value(st, item)), x + 3, y, fp,
                 DB_FONT6_XSPACING);
    }
    /* A 5-wide triangle pointing down, drawn the way dopt_draw_shapebtn draws its
       play arrow: rows of a horizontal line, narrowing. */
    for (i = 0; i < 3; i++)
        db_line_h(s, x + w - 8 + i, x + w - 4 - i, y + 2 + i, (unsigned char)ink);
}

/* A tooltip, two lines in a boxed panel, placed under the entry it explains and nudged
   back inside the dialog if that would push it off the right edge. */
static void dopt_draw_tip(DB_Surface *s, const DB_Pack *p, int ax, int ay,
                          const char *tl1, const char *tl2, const char *tl3)
{
    const DB_Font *f = db_font(p, "GRAD6FNT");
    unsigned char fp[16];
    int w1, w2, w3, w, h, x, y;

    if (!f || !tl1 || !tl2 || !tl3)
        return;
    w1 = db_string_width(f, tl1, DB_FONT6_XSPACING);
    w2 = db_string_width(f, tl2, DB_FONT6_XSPACING);
    w3 = db_string_width(f, tl3, DB_FONT6_XSPACING);
    w = (w1 > w2 ? w1 : w2);
    if (w3 > w) w = w3;
    w += 6;
    /* An empty third line is a two-line tip, not a box with a blank row in it. */
    h = (*tl3 ? 3 : 2) * DOPT_A_STEP + 2;
    x = ax;
    y = ay;
    /* CLAMPED TO THE SCREEN, not to the dialog. A tooltip is allowed to overhang the box
       it explains -- that is what a tooltip does -- and clamping it to the dialog is what
       chopped the first version off at both ends. */
    if (x + w > DOPT_SCREEN_W - 2) x = DOPT_SCREEN_W - 2 - w;
    if (x < 2) x = 2;
    if (y + h > DOPT_SCREEN_H - 2) y = ay - h - DOPT_A_DROP_H;
    if (y < 2) y = 2;
    dopt_style_plate(s, x, y, w, h, 0, 0);
    db_font_palette_grad(fp, DOPT_TEXT_BRIGHT, DB_TBLACK);
    db_print(s, f, tl1, x + 3, y + 1, fp, DB_FONT6_XSPACING);
    db_print(s, f, tl2, x + 3, y + 1 + DOPT_A_STEP, fp, DB_FONT6_XSPACING);
    if (*tl3)
        db_print(s, f, tl3, x + 3, y + 1 + 2 * DOPT_A_STEP, fp, DB_FONT6_XSPACING);
}

/* A VERTICAL SLIDER WELL WITH ITS THUMB, the two boxes this dialog already has: the body
 * BOXSTYLE_GREEN_DOWN (slider.cpp:339), the thumb BOXSTYLE_GREEN_RAISED (slider.cpp:310),
 * drawn pressed while it is held. The Advanced page's bar and the resolution list's are
 * both this, so the two cannot drift apart. */
static void dopt_draw_vbar(DB_Surface *s, int x, int y, int w, int h, int ty, int th,
                           int held)
{
    db_fill_rect(s, x, y, x + w - 1, y + h - 1, DOPT_GREEN_BKGD);
    db_line_h(s, x, x + w - 1, y + h - 1, DOPT_LIGHT_GREEN);
    db_line_v(s, x + w - 1, y, y + h - 1, DOPT_LIGHT_GREEN);
    db_line_h(s, x, x + w - 1, y, DOPT_GREEN_SHADOW);
    db_line_v(s, x, y, y + h - 1, DOPT_GREEN_SHADOW);
    dopt_style_plate(s, x, ty, w, th, held, 0);
}

/* The open list. Drawn after everything else on the page so it covers it. */
static void dopt_draw_texset_drop(DB_Surface *s, const DB_Pack *p, const DOPT_State *st)
{
    const DB_Font *f = db_font(p, "GRAD6FNT");
    unsigned char fp[16];
    char info[32];
    int i, x, y, w, h, ink, tipx = 0, tipy = 0, tip = 0;
    const char *tipl1 = 0, *tipl2 = 0, *tipl3 = 0;
    const int item = st->texdrop;

    if (!dopt_is_droprow(item))
        return;
    for (i = 0; i < dopt_drop_count(st, item); i++) {
        if (!dopt_texset_item_rect_of(st, item, i, &x, &y, &w, &h))
            continue;
        dopt_style_plate(s, x, y, w, h, i == dopt_drop_value(st, item), 0);
        if (!dopt_texset_pickable(st, i))       ink = DOPT_TEXT_DISABLED;
        else if (i == st->texhot)               ink = DOPT_TEXT_BRIGHT;
        else                                    ink = DOPT_TEXT_MEDIUM;
        if (f) {
            db_font_palette_grad(fp, (unsigned char)ink, DB_TBLACK);
            db_print(s, f, dopt_drop_text(st, item, i), x + 3, y, fp, DB_FONT6_XSPACING);
        }
        if (i == st->texhot
            && (dopt_texset_tip_lines(st, i, &tipl1, &tipl2, &tipl3)
                || dopt_texset_info_lines(st, i, info, (int)sizeof info,
                                          &tipl1, &tipl2, &tipl3))) {
            tip = 1; tipx = x; tipy = y + h + 1;
        }
    }
    /* THE RESOLUTION LIST'S SLIDER WELL, when it holds more than it shows. */
    if (dopt_res_bar_rect(st, &x, &y, &w, &h)) {
        int ty, th;
        dopt_res_thumb(st, &ty, &th);
        dopt_draw_vbar(s, x, y, w, h, ty, th, 0);
    }
    /* After the loop, so the tooltip is over the entries and not under the next one. */
    if (tip)
        dopt_draw_tip(s, p, tipx, tipy, tipl1, tipl2, tipl3);
}

/* The two Visuals pages. Everything on them is ours; see dosopt.h. */
static void dopt_draw_visuals(DB_Surface *s, const DB_Pack *p, const DOPT_State *st)
{
    const DB_Font *f = db_font(p, "GRAD6FNT");
    unsigned char fp[16];
    int i, x, y, w, h;

    dopt_style_dialog(s, DOPT_V_X, DOPT_V_Y, DOPT_V_W, DOPT_V_H);
    dopt_style_caption(s, p, "Visuals", DOPT_V_X, DOPT_V_Y, DOPT_V_W);

    for (i = 0; i < DOPT_V_COUNT; i++)
        dopt_draw_button(s, f, st, i, dopt_vis_label[i]);

    /* The chosen one of the pair is drawn HELD DOWN. dopt_draw_button only knows about
       st->pressed, which is a transient mouse state, so the latch is painted over the
       top: same box, pressed style, then the label again bright. */
    {
        const int sel = st->vis.enhanced ? DOPT_V_ENHANCED : DOPT_V_CLASSIC;
        if (dopt_item_rect(st, sel, &x, &y, &w, &h) && f) {
            int tx;
            dopt_style_plate(s, x, y, w, h, 1, 0);
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

/* THE SCROLL BAR. SliderClass in list mode, drawn with the two boxes this dialog already
 * has: slider.cpp:339 draws the body BOXSTYLE_GREEN_DOWN, which is dialog.cpp row 6 and
 * is the same well dopt_draw_slider paints under the volume rows, and slider.cpp:310
 * draws the thumb `Draw_Box(X, Y + ThumbStart, Width, ThumbSize, BOXSTYLE_GREEN_RAISED)`,
 * which is dopt_button_box unpressed. Nothing here is a new shape, a new colour or a new
 * primitive, and there are no arrow buttons because a list-mode slider has none
 * (slider.cpp:68-82) -- which is fortunate, since BTN-UP.SHP and BTN-DN.SHP are in no
 * archive this project bakes.
 * The thumb is drawn PRESSED while it is being dragged, which is the same feedback every
 * other control on this screen gives under the hand. */
static void dopt_draw_adv_bar(DB_Surface *s, const DOPT_State *st)
{
    int x, y, w, h, ty, th;

    if (!dopt_item_rect(st, DOPT_A_BAR, &x, &y, &w, &h))
        return;
    dopt_adv_thumb(st, &ty, &th);
    dopt_draw_vbar(s, x, y, w, h, ty, th, st->drag == DOPT_A_BAR);
}

static void dopt_draw_advanced(DB_Surface *s, const DB_Pack *p, const DOPT_State *st)
{
    const DB_Font *f = db_font(p, "GRAD6FNT");
    unsigned char fp[16];
    int i, x, y, w, h;

    dopt_style_dialog(s, DOPT_V_X, DOPT_V_Y, DOPT_V_W, DOPT_V_H);
    dopt_style_caption(s, p, "Advanced", DOPT_V_X, DOPT_V_Y, DOPT_V_W);

    for (i = 0; i < DOPT_VE_COUNT; i++) {
        int on;
        if (!dopt_item_rect(st, i, &x, &y, &w, &h))
            continue;
        /* The display triple lights the one that is chosen; Reset draws a raised plate
           where a box would be, so it reads as a button and not a switch. */
        if (i == DOPT_VE_FULLSCREEN)      on = (st->vis.dispmode == DOPT_DISP_FULLSCREEN);
        else if (i == DOPT_VE_WINDOWED)   on = (st->vis.dispmode == DOPT_DISP_WINDOWED);
        else if (i == DOPT_VE_BORDERLESS) on = (st->vis.dispmode == DOPT_DISP_BORDERLESS);
        else                              on = st->vis.elem[i];
        if (dopt_is_droprow(i))
            dopt_draw_texset_box(s, p, st, i);
        else if (i == DOPT_VE_RESET)
            dopt_style_plate(s, x, y, DOPT_A_BOX, DOPT_A_BOX, st->pressed == i, 0);
        else
            dopt_draw_check(s, st, i, on);
        if (!f)
            continue;
        db_font_palette_grad(fp, dopt_item_disabled(st, i) ? DOPT_TEXT_DISABLED
                               : (st->selected == i) ? DOPT_TEXT_BRIGHT
                                                     : DOPT_TEXT_MEDIUM, DB_TBLACK);
        db_print(s, f, dopt_vis_elem[i], DOPT_A_LABEL_X, y - 1, fp, DB_FONT6_XSPACING);
    }
    dopt_draw_adv_bar(s, st);
    dopt_draw_button(s, f, st, DOPT_A_OK, "OK");
    /* LAST, so it covers the rows and the bar beneath it. */
    dopt_draw_texset_drop(s, p, st);
}

/* The cheat page. Same box, same caption, same checkbox and same button as the Advanced
   page draws, because it IS those, called with a different list. */
void dopt_gameplay_defaults(DOPT_Gameplay *g)
{
    if (!g) return;
    /* OFF: nobody who does not come looking for it gets a different set of buttons. */
    g->on[DOPT_G_SWAPBTN] = 0;
    /* ON: the gesture was asked for, and a player should not have to go looking for it. */
    g->on[DOPT_G_RPUSH] = 1;
    /* ON: both source games sounded it; the switch exists for the player who tires of it. */
    g->on[DOPT_G_CASHTICK] = 1;
}

void dopt_set_gameplay(DOPT_State *st, const DOPT_Gameplay *g)
{ if (st && g) st->gp = *g; }

const DOPT_Gameplay *dopt_gameplay(const DOPT_State *st) { return st ? &st->gp : 0; }

void dopt_bind_gameplay(DOPT_State *st, void (*applygp)(void *, const DOPT_Gameplay *))
{
    if (!st) return;
    st->bind.applygp = applygp;
    if (applygp)
        applygp(st->bind.user, &st->gp);
}

int dopt_gp_head_rect(const DOPT_State *st, const DB_Pack *p, int *x, int *y, int *w,
                      int *h)
{
    const DB_Font *f = p ? db_font(p, "GRAD6FNT") : 0;
    (void)st;
    if (!f) return 0;
    *w = db_string_width(f, DOPT_G_HEADING, DB_FONT6_XSPACING);
    *h = f->maxh;
    *x = DOPT_V_X + (DOPT_V_W - *w) / 2;
    *y = DOPT_G_HEAD_Y;
    return 1;
}

/* The Gameplay page. Same box, same caption, same checkbox and same button the Advanced
   page draws, because it IS those, called with a different list. */
static void dopt_draw_gameplay(DB_Surface *s, const DB_Pack *p, const DOPT_State *st)
{
    const DB_Font *f = db_font(p, "GRAD6FNT");
    unsigned char fp[16];
    int i, x, y, w, h;

    dopt_style_dialog(s, DOPT_V_X, DOPT_V_Y, DOPT_V_W, DOPT_V_H);
    dopt_style_caption(s, p, "Gameplay", DOPT_V_X, DOPT_V_Y, DOPT_V_W);

    if (f && dopt_gp_head_rect(st, p, &x, &y, &w, &h)) {
        db_font_palette_grad(fp, DOPT_TEXT_MEDIUM, DB_TBLACK);
        db_print(s, f, DOPT_G_HEADING, x, y, fp, DB_FONT6_XSPACING);
    }

    for (i = 0; i < DOPT_G_TOGGLES; i++) {
        if (!dopt_item_rect(st, i, &x, &y, &w, &h))
            continue;
        dopt_draw_check(s, st, i, st->gp.on[i]);
        if (!f)
            continue;
        db_font_palette_grad(fp, (st->selected == i) ? DOPT_TEXT_BRIGHT
                                                     : DOPT_TEXT_MEDIUM, DB_TBLACK);
        db_print(s, f, dopt_gp_label(i), DOPT_A_LABEL_X, y - 1, fp, DB_FONT6_XSPACING);
    }
    dopt_draw_button(s, f, st, DOPT_G_OK, "OK");
}

static void dopt_draw_cheats(DB_Surface *s, const DB_Pack *p, const DOPT_State *st)
{
    const DB_Font *f = db_font(p, "GRAD6FNT");
    unsigned char fp[16];
    int i, x, y, w, h;

    dopt_style_dialog(s, DOPT_V_X, DOPT_V_Y, DOPT_V_W, DOPT_V_H);
    dopt_style_caption(s, p, "Cheats", DOPT_V_X, DOPT_V_Y, DOPT_V_W);

    for (i = 0; i < DOPT_CH_TOGGLES; i++) {
        if (!dopt_item_rect(st, i, &x, &y, &w, &h))
            continue;
        dopt_draw_check(s, st, i, st->cheat.on[i]);
        if (!f)
            continue;
        db_font_palette_grad(fp, dopt_item_disabled(st, i) ? DOPT_TEXT_DISABLED
                               : (st->selected == i) ? DOPT_TEXT_BRIGHT
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
    dopt_style_plate(s, x, y, w, h, st->pressed == item, dopt_item_disabled(st, item));
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

    dopt_style_dialog(s, DOPT_SND_X, DOPT_SND_Y, DOPT_SND_W, DOPT_SND_H);
    dopt_style_caption(s, p, "Sound Controls", DOPT_SND_X, DOPT_SND_Y, DOPT_SND_W);

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
    dopt_style_greenbox(s, DOPT_SND_LIST_X, DOPT_SND_LIST_Y, DOPT_SND_LIST_W,
                        DOPT_SND_LIST_H);

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

/* BOXSTYLE_GREEN_BOX: list.cpp:323 draws the list well with it and edit.cpp:311 the
   field. The same four lines the jukebox's well is drawn with, lifted out so the slot
   dialog's two boxes cannot drift from it. */
static void dopt_style_greenbox(DB_Surface *s, int x, int y, int w, int h)
{
    db_fill_rect(s, x, y, x + w - 1, y + h - 1, DB_BLACK);
    db_line_h(s, x, x + w - 1, y, DOPT_GREEN_BOX);
    db_line_h(s, x, x + w - 1, y + h - 1, DOPT_GREEN_BOX);
    db_line_v(s, x, y, y + h - 1, DOPT_GREEN_BOX);
    db_line_v(s, x + w - 1, y, y + h - 1, DOPT_GREEN_BOX);
}

/* THE SLOT DIALOG. loaddlg.cpp:355-372: Dialog_Box, Draw_Caption with the mode's own
   caption, and in SAVE mode the "Mission Description" label centred over the field;
   then the gadgets draw themselves: the list (list.cpp:323-343, a GREEN_BOX well with
   the selected line filled and printed bright), the field (edit.cpp:309-346, a
   GREEN_BOX with the text and a trailing "_" while it has the focus) and the two
   buttons. The pause dialog stays on the screen behind it, exactly as it stays behind
   the confirmation box, because loaddlg.cpp is run from inside goptions.cpp's loop. */
static void dopt_draw_slots(DB_Surface *s, const DB_Pack *p, const DOPT_State *st)
{
    const DB_Font *f = db_font(p, "GRAD6FNT");
    unsigned char fp[16], fpsel[16];
    int i, row, y, lh, shown;

    if (!st->sl.from_menu) {
        dopt_style_dialog(s, DOPT_X, DOPT_Y, DOPT_W, DOPT_H);
        dopt_style_caption(s, p, "Options", DOPT_X, DOPT_Y, DOPT_W);
        for (i = 0; i < DOPT_ITEM_COUNT; i++)
            dopt_draw_button(s, f, st, i, dopt_item_label(st, i));
    }

    dopt_style_dialog(s, DOPT_SL_X, DOPT_SL_Y, DOPT_SL_W, DOPT_SL_H);
    dopt_style_caption(s, p, dopt_sl_caption[st->sl.mode < 3 ? st->sl.mode : 0],
                       DOPT_SL_X, DOPT_SL_Y, DOPT_SL_W);

    lh = dopt_sl_list_h(st);
    shown = dopt_sl_rows_shown(st);
    dopt_style_greenbox(s, DOPT_SL_LIST_X, DOPT_SL_LIST_Y, DOPT_SL_LIST_W, lh);
    if (f) {
        db_font_palette_grad(fp, DOPT_TEXT_MEDIUM, DB_TBLACK);
        db_font_palette_grad(fpsel, DOPT_TEXT_BRIGHT, DB_TBLACK);
        for (row = 0; row < shown; row++) {
            i = st->sl.top + row;
            if (i >= st->sl.nrows) break;
            y = DOPT_SL_LIST_Y + 1 + row * DOPT_SL_ROW_H;
            if (i == st->sl.sel)
                db_fill_rect(s, DOPT_SL_LIST_X + 1, y, DOPT_SL_LIST_X + DOPT_SL_LIST_W - 2,
                             y + DOPT_SL_ROW_H - 1, DOPT_GREEN_BKGD);
            db_print(s, f, st->sl.rows[i].text, DOPT_SL_LIST_X + 4, y,
                     i == st->sl.sel ? fpsel : fp, DB_FONT6_XSPACING);
        }
        if (st->sl.nrows <= 0) {
            /* loaddlg.cpp:237-241 raises TXT_NO_SAVES (648) in a box of its own and
               does not open the dialog at all; the main menu's button opens this one
               regardless, so the words go in the empty well, the way the jukebox says
               it has no tracks. */
            unsigned char fpdim[16];
            db_font_palette_grad(fpdim, DOPT_TEXT_DISABLED, DB_TBLACK);
            db_print(s, f, "No saved games available.", DOPT_SL_LIST_X + 4,
                     DOPT_SL_LIST_Y + 3, fpdim, DB_FONT6_XSPACING);
        }
        if (st->sl.mode == DOPT_SL_SAVE) {
            const int focus = (st->selected == DOPT_SL_EDIT);
            int tw;
            /* the label, centred on the dialog (loaddlg.cpp:367-372, TPF_CENTER) */
            db_print(s, f, DOPT_SL_DESCR_S,
                     DOPT_SL_CX - (db_string_width(f, DOPT_SL_DESCR_S, DB_FONT6_XSPACING) >> 1),
                     DOPT_SL_LABEL_Y, fp, DB_FONT6_XSPACING);
            dopt_style_greenbox(s, DOPT_SL_EDIT_X, DOPT_SL_EDIT_Y, DOPT_SL_EDIT_W,
                                DOPT_SL_EDIT_H);
            /* edit.cpp:332-346: bright while focused, and the cursor only while the
               text is short enough for one more character. */
            db_print(s, f, st->sl.descr, DOPT_SL_EDIT_X + 1, DOPT_SL_EDIT_Y + 3,
                     focus ? fpsel : fp, DB_FONT6_XSPACING);
            tw = db_string_width(f, st->sl.descr, DB_FONT6_XSPACING);
            if (focus && strlen(st->sl.descr) < DOPT_SL_DESCR_MAX
                && tw + db_string_width(f, "_", DB_FONT6_XSPACING) < DOPT_SL_EDIT_W - 2)
                db_print(s, f, "_", DOPT_SL_EDIT_X + 1 + tw, DOPT_SL_EDIT_Y + 3, fpsel,
                         DB_FONT6_XSPACING);
        }
    }
    dopt_draw_button(s, f, st, DOPT_SL_OK, dopt_item_label(st, DOPT_SL_OK));
    dopt_draw_button(s, f, st, DOPT_SL_CANCEL, dopt_item_label(st, DOPT_SL_CANCEL));
}

/* THE CONFIRMATION BOX. msgbox.cpp:239-252 draws Dialog_Box, then Draw_Caption with a
   TXT_NONE caption (which is why there is no title bar here, unlike every other page),
   then the message at x+20, y+25 in green on black. The three buttons come out of
   dopt_item_rect so the drawn box and the clickable box cannot drift apart.

   Drawn OVER the pause dialog rather than instead of it, because that is what a message
   box is: goptions.cpp:425 raises it from inside its own dialog loop and the dialog is
   still on the screen behind it. Under the delete question it is the slot dialog that
   stays behind, for the same reason (loaddlg.cpp:450). */
static void dopt_draw_confirm(DB_Surface *s, const DB_Pack *p, const DOPT_State *st)
{
    const DB_Font *f = db_font(p, "GRAD6FNT");
    unsigned char fp[16];
    int i;

    if (st->cfkind == 1) {
        dopt_draw_slots(s, p, st);
    } else {
        /* the pause dialog stays behind it */
        dopt_style_dialog(s, DOPT_X, DOPT_Y, DOPT_W, DOPT_H);
        dopt_style_caption(s, p, "Options", DOPT_X, DOPT_Y, DOPT_W);
        for (i = 0; i < DOPT_ITEM_COUNT; i++)
            dopt_draw_button(s, f, st, i, dopt_item_label(st, i));
    }

    dopt_style_dialog(s, st->cfx, st->cfy, st->cfw, st->cfh);
    if (f) {
        /* GRAD6FNT IS A GRADIENT FONT and needs the gradient palette builder, not a
           single index. Setting fp[1] alone and leaving the rest DB_TBLACK, which is what
           the scenario-name print at the bottom of dopt_draw does, renders this string
           INVISIBLE: the glyphs span several palette entries and all but one of them stay
           transparent. Caught by looking at the picture, where the box and all three
           buttons drew correctly and the question was simply not there. */
        db_font_palette_grad(fp, DOPT_TEXT_MEDIUM, DB_TBLACK);
        db_print(s, f, dopt_confirm_msg(st), st->cfx + DOPT_CF_TEXT_X,
                 st->cfy + DOPT_CF_TEXT_Y, fp, DB_FONT6_XSPACING);
        {
            const char *m2 = dopt_confirm_msg2(st);
            if (m2)
                db_print(s, f, m2, st->cfx + DOPT_CF_TEXT_X,
                         st->cfy + DOPT_CF_TEXT_Y + f->maxh + DB_FONT6_YSPACING,
                         fp, DB_FONT6_XSPACING);
        }
    }
    for (i = 0; i < DOPT_CF_COUNT; i++) {
        /* See dopt_item_disabled: in a match, and under the delete question, this box
           is a plain Yes and No. */
        if ((st->match || st->cfkind == 1) && i == DOPT_CF_RESTART)
            continue;
        dopt_draw_button(s, f, st, i, dopt_item_label(st, i));
    }
}

/* THE OBJECTIVE BOX. msgbox.cpp:239-252 again, and unlike the confirmation this one
   HAS a caption: WWMessageBox(TXT_OBJECTIVE) draws "Mission Objective" through
   Draw_Caption, then the wrapped text at x+20, y+25 in CC_GREEN through the gradient
   palette, one line per FontHeight + FontYSpacing. Drawn over the pause dialog, which
   stays on the screen behind it (goptions.cpp:373 raises it from inside its own loop). */
static void dopt_draw_restate(DB_Surface *s, const DB_Pack *p, const DOPT_State *st)
{
    const DB_Font *f = db_font(p, "GRAD6FNT");
    unsigned char fp[16];
    int i;

    dopt_style_dialog(s, DOPT_X, DOPT_Y, DOPT_W, DOPT_H);
    dopt_style_caption(s, p, "Options", DOPT_X, DOPT_Y, DOPT_W);
    for (i = 0; i < DOPT_ITEM_COUNT; i++)
        dopt_draw_button(s, f, st, i, dopt_item_label(st, i));

    if (st->rw <= 0)
        return;
    dopt_style_dialog(s, st->rx, st->ry, st->rw, st->rh);
    dopt_style_caption(s, p, DOPT_R_CAPTION, st->rx, st->ry, st->rw);
    if (f) {
        const int lh = f->maxh + DB_FONT6_YSPACING;
        db_font_palette_grad(fp, DOPT_TEXT_MEDIUM, DB_TBLACK);
        for (i = 0; i < st->brlines; i++)
            db_print(s, f, st->brwrap + st->brline[i], st->rx + DOPT_CF_TEXT_X,
                     st->ry + DOPT_CF_TEXT_Y + i * lh, fp, DB_FONT6_XSPACING);
    }
    for (i = 0; i < DOPT_R_COUNT; i++)
        dopt_draw_button(s, f, st, i, dopt_item_label(st, i));
}

/* THE NOTICE BOX: the objective box's geometry with the caller's caption and nothing
   drawn under it. It stands on whatever the caller left on the glass, which for a
   refused mission start is black. */
static void dopt_draw_notice(DB_Surface *s, const DB_Pack *p, const DOPT_State *st)
{
    const DB_Font *f = db_font(p, "GRAD6FNT");
    unsigned char fp[16];
    int i;

    if (st->rw <= 0)
        return;
    dopt_style_dialog(s, st->rx, st->ry, st->rw, st->rh);
    dopt_style_caption(s, p, st->ncaption, st->rx, st->ry, st->rw);
    if (f) {
        const int lh = f->maxh + DB_FONT6_YSPACING;
        db_font_palette_grad(fp, DOPT_TEXT_MEDIUM, DB_TBLACK);
        for (i = 0; i < st->brlines; i++)
            db_print(s, f, st->brwrap + st->brline[i], st->rx + DOPT_CF_TEXT_X,
                     st->ry + DOPT_CF_TEXT_Y + i * lh, fp, DB_FONT6_XSPACING);
    }
    dopt_draw_button(s, f, st, DOPT_R_LEFT, dopt_item_label(st, DOPT_R_LEFT));
}

void dopt_draw(DB_Surface *s, const DB_Pack *p, const DOPT_State *st)
{
    const DB_Font *f = db_font(p, "GRAD6FNT");
    unsigned char fp[16];
    int i;

    if (st->page == DOPT_PAGE_RESTATE)  { dopt_draw_restate(s, p, st);  return; }
    if (st->page == DOPT_PAGE_NOTICE)   { dopt_draw_notice(s, p, st);   return; }
    if (st->page == DOPT_PAGE_SLOTS)    { dopt_draw_slots(s, p, st);    return; }
    if (st->page == DOPT_PAGE_VISUALS)  { dopt_draw_visuals(s, p, st);  return; }
    if (st->page == DOPT_PAGE_ADVANCED) { dopt_draw_advanced(s, p, st); return; }
    if (st->page == DOPT_PAGE_CHEATS)   { dopt_draw_cheats(s, p, st);   return; }
    if (st->page == DOPT_PAGE_GAMEPLAY) { dopt_draw_gameplay(s, p, st); return; }
    if (st->page == DOPT_PAGE_SOUND)    { dopt_draw_sound(s, p, st);    return; }
    if (st->page == DOPT_PAGE_CONFIRM)  { dopt_draw_confirm(s, p, st);  return; }

    if (st->page == DOPT_PAGE_CONTROLS) {
        dopt_style_dialog(s, DOPT_GC_X, DOPT_GC_Y, DOPT_GC_W, DOPT_GC_H);
        dopt_style_caption(s, p, "Game Controls", DOPT_GC_X, DOPT_GC_Y, DOPT_GC_W);
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

    dopt_style_dialog(s, DOPT_X, DOPT_Y, DOPT_W, DOPT_H);
    dopt_style_caption(s, p, "Options", DOPT_X, DOPT_Y, DOPT_W);
    for (i = 0; i < DOPT_ITEM_COUNT; i++)
        dopt_draw_button(s, f, st, i, dopt_item_label(st, i));

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
