/* ====================================================================================
 *  unitcard_mod.h -- the UNIT CARD: what is selected, in the bottom-left corner.
 *
 *  A card in the corner of the Enhanced HUD showing the selected unit's cameo, name,
 *  health and damage; a row of smaller cameos for the rest of the selection, each of
 *  which can be clicked to make that unit the whole selection; and the ten control-group
 *  tabs, keyed 1..9 then 0 the way the keyboard is, each printing how many units the
 *  group holds and recalling the group when clicked.
 *
 *  ENHANCED ONLY, AND ONLY WITH THE 640x480 HUD, on exactly the gate the DATABASE plate
 *  uses (codex_available). Classic is the 1995 game and the 1995 game has no such card.
 *
 *  HOW IT DRAWS. The card is composed on the CPU by hud640_draw_card out of the HUD's
 *  own pack -- the radar bezel widened, slices of the blank tab plate, the well at half
 *  size, GRAD6FNT at the tab strip's scale -- and reaches the screen as one texture and
 *  one quad through the same h6_upload / h6_tex_quad the bar uses. So the Tier 1 answer
 *  is the bar's answer: a blit. Nothing here needs a shader.
 *
 *  WHAT IT READS. The selection is the engine's own CurrentObject list, mirrored into
 *  g_selected by sync_selection_from_brain after every selection change. Group
 *  membership is FootClass::Group, exported per object as group= on the OBJ| line; a
 *  brain that predates that export reads as no groups known and the tabs all show empty.
 *  Damage is the primary weapon's, out of the same 1995 table the DATABASE page prints
 *  (codex_table.h); -1 there means unarmed and the card says so.
 *
 *  WHAT IT SENDS. A tab click is Handle_Team's recall, through the same ui_group the
 *  number keys use. A click on a smaller cameo clears the selection and re-selects that
 *  one object through the engine's own CNC_Select_Object, then re-reads the list, so the
 *  card never keeps a selection of its own. A click on the main cameo centres the camera
 *  on that unit.
 *
 *  Included by cnc_eyes.cpp after codex_mod.h: it uses the sidebar's pack, the codex
 *  table, ui_group and the selection mirror, all of which are defined by then.
 * ==================================================================================== */

#ifndef UNITCARD_MOD_H
#define UNITCARD_MOD_H

static unsigned char g_ucBuf[H6_CARD_W * H6_CARD_H * 4];
static GLuint        g_ucTex = 0;
/* The selection keys behind the wells, filled at draw time and read by the click, so a
   click always lands on what the picture showed. -1 is an empty or overflow slot. */
static int  g_ucMainKey = -1;
static int  g_ucMiniKey[H6_CARD_MINIS];
static int  g_ucMiniN = 0;
static int  g_ucGroupCount[H6_CARD_TABS];
static bool g_ucShownNow = false;   /* what the last draw decided; the hit tests follow it */

/* Only in Enhanced, only on the 640x480 HUD, never over the editor or the codex page,
   and only when the pack carries the card's art. */
static bool uc_available(void)
{
    return codex_available() && !g_editOn && hud640_card_ok(g_h6Pack);
}

static bool uc_shown(void)
{
    return uc_available() && sb_enabled() && !g_cxOpen && !fxp_is_open() && g_selCount > 0;
}

/* Where the card is, in framebuffer pixels: pinned to the window's bottom-left corner
   at the bar's own zoom, so the card and the sidebar share one pixel size. */
typedef struct { float x0, y0, s; } UcLayout;

static UcLayout uc_layout(int fbw, int fbh)
{
    UcLayout L;
    sb_layout(fbw, fbh);
    L.s = g_h6Scale;
    L.x0 = 0.0f;
    L.y0 = (float)fbh - (float)H6_CARD_H * L.s;
    return L;
}

enum { UC_NONE = 0, UC_BODY, UC_MAIN, UC_MINI, UC_TAB };

/* What is under a point. `idx` is the tab slot (0..9, key 1..9 then 0) or the mini
   slot. Answers UC_NONE whenever the card is not on screen, so every caller can ask
   without first asking whether to ask. */
static int uc_hit(float col, float row, int fbw, int fbh, int* idx)
{
    if (idx) *idx = -1;
    if (!g_ucShownNow) return UC_NONE;
    const UcLayout L = uc_layout(fbw, fbh);
    const float cx = (col - L.x0) / L.s, cy = (row - L.y0) / L.s;   /* card pixels */
    if (cx < 0.0f || cy < 0.0f || cx >= (float)H6_CARD_W || cy >= (float)H6_CARD_H)
        return UC_NONE;
    if (cy >= (float)H6_CARD_BODY_H) {
        int t = (int)(cx / (float)H6_CARD_TAB_W);
        if (t < 0) t = 0;
        if (t >= H6_CARD_TABS) t = H6_CARD_TABS - 1;
        if (idx) *idx = t;
        return UC_TAB;
    }
    {
        const float wx = cx - (float)H6_CARD_WIN_X, wy = cy - (float)H6_CARD_WIN_Y;
        if (wx >= (float)H6_CARD_CELL_X && wx < (float)(H6_CARD_CELL_X + H6_CELL_W) &&
            wy >= (float)H6_CARD_CELL_Y && wy < (float)(H6_CARD_CELL_Y + H6_CELL_H))
            return UC_MAIN;
        if (wy >= (float)H6_CARD_MINI_Y && wy < (float)(H6_CARD_MINI_Y + H6_CARD_MINI_H)) {
            const float mx = wx - (float)H6_CARD_MINI_X;
            if (mx >= 0.0f) {
                const int m = (int)(mx / (float)H6_CARD_MINI_PITCH);
                if (m < g_ucMiniN && mx - (float)(m * H6_CARD_MINI_PITCH) < (float)H6_CARD_MINI_W) {
                    if (idx) *idx = m;
                    return UC_MINI;
                }
            }
        }
    }
    return UC_BODY;
}

static bool uc_over(float col, float row, int fbw, int fbh)
{
    return uc_hit(col, row, fbw, fbh, NULL) != UC_NONE;
}

static int uc_object_for_key(int key)
{
    if (key < 0) return -1;
    for (size_t i = 0; i < g_objects.size(); i++) {
        const SimObject& o = g_objects[i];
        if (o.id >= 0 && o.rideKey < 0 && (int)o.kind * 100000 + o.id == key) return (int)i;
    }
    return -1;
}

static int uc_damage_for(const char* code)
{
    for (size_t i = 0; i < sizeof(CODEX) / sizeof(CODEX[0]); i++)
        if (!strcasecmp(CODEX[i].code, code)) return CODEX[i].damage;
    return -1;
}

/* The health bar's own colour, reduced to the meter's three classes: green while the
   bar draws green, amber while it draws amber, red below that. Taking it from hb_colour
   rather than from a threshold typed here keeps the card and the bar on one rule. */
static int uc_health_class(int str, int maxstr)
{
    float r, g, b;
    hb_colour(hb_ratio256(str, maxstr), &r, &g, &b);
    if (r < g) return 0;
    return g > 0.25f ? 1 : 2;
}

static bool uc_groupable(const SimObject& o)
{
    return o.id >= 0 && o.rideKey < 0 && !o.limbo && is_players(o) &&
           (o.kind == K_UNIT || o.kind == K_INFANTRY || o.kind == K_AIRCRAFT);
}

/* Fill the card from the world. Also decides the hover frames, from the pointer. */
static void uc_fill(H6_Card* c, int fbw, int fbh)
{
    memset(c, 0, sizeof *c);
    c->damage = -1;
    g_ucMainKey = -1;
    g_ucMiniN = 0;
    for (int i = 0; i < H6_CARD_MINIS; i++) g_ucMiniKey[i] = -1;

    int hoverWhat, hoverIdx = -1;
    hoverWhat = (g_mouseScrC >= 0.0f) ? uc_hit(g_mouseScrC, g_mouseScrR, fbw, fbh, &hoverIdx)
                                      : UC_NONE;

    /* 1. the selection: the engine's list, first object first */
    c->count = (int)g_selected.size();
    if (c->count > 0) {
        const int oi = uc_object_for_key(g_selected[0]);
        if (oi >= 0) {
            const SimObject& o = g_objects[oi];
            const char* title = sb_title_for(o.type);
            g_ucMainKey = g_selected[0];
            snprintf(c->name, sizeof c->name, "%s", title ? title : o.type);
            snprintf(c->code, sizeof c->code, "%s", o.type);
            c->cameo = h6_tdr_find(o.type);
            c->str = o.str;
            c->maxstr = o.maxstr;
            c->health_color = uc_health_class(o.str, o.maxstr);
            c->damage = uc_damage_for(o.type);
        }
        /* The rest, one well each; the last well says how many did not fit. */
        const int rest = c->count - 1;
        int slots = rest;
        if (slots > H6_CARD_MINIS) slots = H6_CARD_MINIS;
        c->nmini = slots;
        if (rest > H6_CARD_MINIS) c->overflow = rest - (H6_CARD_MINIS - 1);
        for (int m = 0; m < slots; m++) {
            if (c->overflow > 0 && m == slots - 1) break;   /* the +N well */
            const int oi = uc_object_for_key(g_selected[(size_t)m + 1]);
            g_ucMiniKey[m] = g_selected[(size_t)m + 1];
            if (oi >= 0) c->mini[m].rgba = h6_tdr_find(g_objects[oi].type);
            c->mini[m].frame = (hoverWhat == UC_MINI && hoverIdx == m) ? 1 : 0;
        }
        g_ucMiniN = slots;
    }

    /* 2. the groups: counted off the brain's own field, player's units only */
    int selInGroup[H6_CARD_TABS];
    memset(selInGroup, 0, sizeof selInGroup);
    for (size_t i = 0; i < g_objects.size(); i++) {
        const SimObject& o = g_objects[i];
        if (!uc_groupable(o) || o.group < 0 || o.group >= H6_CARD_TABS) continue;
        c->group_count[o.group]++;
        if (is_selected(o)) selInGroup[o.group]++;
    }
    for (int g = 0; g < H6_CARD_TABS; g++) {
        g_ucGroupCount[g] = c->group_count[g];
        if (c->group_count[g] <= 0) { c->group_frame[g] = 0; continue; }
        /* ACTIVE when the selection IS the group: every member selected and nothing
           else. A selection that merely overlaps it is not that group. */
        const bool active = selInGroup[g] == c->group_count[g] && c->count == c->group_count[g];
        if (hoverWhat == UC_TAB && hoverIdx == g) c->group_frame[g] = 2;
        else if (active)                           c->group_frame[g] = 3;
        else                                       c->group_frame[g] = 1;
    }
}

/* Drawn inside the sidebar's overlay, right after the bar, so it shares its pixel
   space and its depth-off state. */
static void uc_draw(int fbw, int fbh)
{
    g_ucShownNow = uc_shown();
    if (!g_ucShownNow) return;
    H6_Card c;
    uc_fill(&c, fbw, fbh);
    hud640_draw_card(g_ucBuf, g_h6Pack, &c);
    h6_upload(&g_ucTex, g_ucBuf, H6_CARD_W, H6_CARD_H);
    const UcLayout L = uc_layout(fbw, fbh);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    h6_tex_quad(g_ucTex, H6_CARD_W, H6_CARD_H, L.x0, L.y0,
                L.x0 + (float)H6_CARD_W * L.s, L.y0 + (float)H6_CARD_H * L.s);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
}

/* Select exactly one object, through the engine, and re-read what it says. */
static void uc_select_only(int key)
{
    const int oi = uc_object_for_key(key);
    if (oi < 0 || !BrainSelect || !BrainClearSel) {
        printf("UNITCARD|pick|key=%d|MISSING\n", key);
        return;
    }
    const SimObject& o = g_objects[oi];
    BrainClearSel(0);
    const bool ok = BrainSelect(0, dll_type_of(o.kind), o.id) != 0;
    sync_selection_from_brain();
    printf("UNITCARD|pick|%s|id=%d|ok=%d|selection=%d\n", o.type, o.id, ok ? 1 : 0, g_selCount);
}

static void uc_centre_on(int key, int fbw, int fbh)
{
    const int oi = uc_object_for_key(key);
    if (oi < 0) return;
    g_camX = (float)g_objects[oi].ex;
    g_camZ = (float)g_objects[oi].ez;
    clamp_camera(fbw, fbh);
    printf("UNITCARD|centre|%s|id=%d|to=%.2f,%.2f\n",
           g_objects[oi].type, g_objects[oi].id, g_camX, g_camZ);
}

/* The left button on the card. Answers true when the press was the card's, whether or
   not it did anything, so nothing behind it -- the map, a band -- sees the press. */
static bool uc_click(float col, float row, int fbw, int fbh)
{
    int idx = -1;
    const int what = uc_hit(col, row, fbw, fbh, &idx);
    switch (what) {
    case UC_NONE:
        return false;
    case UC_TAB:
        if (g_ucGroupCount[idx] > 0) {
            printf("UNITCARD|tab|key=%d|slot=%d|count=%d\n", (idx + 1) % 10, idx, g_ucGroupCount[idx]);
            ui_group(idx, 0, fbw, fbh);
        } else {
            printf("UNITCARD|tab|key=%d|slot=%d|EMPTY\n", (idx + 1) % 10, idx);
        }
        return true;
    case UC_MINI:
        if (g_ucMiniKey[idx] >= 0) uc_select_only(g_ucMiniKey[idx]);
        else printf("UNITCARD|mini|slot=%d|overflow well, nothing to pick\n", idx);
        return true;
    case UC_MAIN:
        uc_centre_on(g_ucMainKey, fbw, fbh);
        return true;
    default:
        return true;                     /* the bezel: consumed, nothing to do */
    }
}

/* ---- the script's view of it -------------------------------------------------------
   `unitcard` prints the card's state and where its controls are, in framebuffer pixels.
   `cardclick tab N` / `cardclick mini N` / `cardclick main` press a control through
   uc_click at that control's own centre, so a gate exercises the same code a hand does
   without hard-coding pixels that move with the window size. */
static void uc_control_centre(int what, int idx, int fbw, int fbh, float* cx, float* cy)
{
    const UcLayout L = uc_layout(fbw, fbh);
    float x = 0.0f, y = 0.0f;
    if (what == UC_TAB) {
        x = (float)(idx * H6_CARD_TAB_W) + (float)H6_CARD_TAB_W * 0.5f;
        y = (float)H6_CARD_BODY_H + (float)H6_CARD_TAB_H * 0.5f;
    } else if (what == UC_MINI) {
        x = (float)(H6_CARD_WIN_X + H6_CARD_MINI_X + idx * H6_CARD_MINI_PITCH) + (float)H6_CARD_MINI_W * 0.5f;
        y = (float)(H6_CARD_WIN_Y + H6_CARD_MINI_Y) + (float)H6_CARD_MINI_H * 0.5f;
    } else {
        x = (float)(H6_CARD_WIN_X + H6_CARD_CELL_X) + (float)H6_CELL_W * 0.5f;
        y = (float)(H6_CARD_WIN_Y + H6_CARD_CELL_Y) + (float)H6_CELL_H * 0.5f;
    }
    *cx = L.x0 + x * L.s;
    *cy = L.y0 + y * L.s;
}

static void uc_script_report(int fbw, int fbh)
{
    if (!uc_available()) {
        printf("UNITCARD|unavailable|enhanced=%d|newhud=%d|pack=%d|edit=%d\n",
               g_fx.enabled ? 1 : 0, g_hudNew ? 1 : 0, g_h6Pack ? 1 : 0, g_editOn ? 1 : 0);
        return;
    }
    H6_Card c;
    g_ucShownNow = uc_shown();
    uc_fill(&c, fbw, fbh);
    const UcLayout L = uc_layout(fbw, fbh);
    printf("UNITCARD|shown=%d|count=%d|main=%s|name=%s|str=%d/%d|dmg=%d|cameo=%d"
           "|minis=%d|overflow=%d|groups=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
           g_ucShownNow ? 1 : 0, c.count, c.code[0] ? c.code : "-", c.name[0] ? c.name : "-",
           c.str, c.maxstr, c.damage, c.cameo ? 1 : 0, c.nmini, c.overflow,
           c.group_count[0], c.group_count[1], c.group_count[2], c.group_count[3],
           c.group_count[4], c.group_count[5], c.group_count[6], c.group_count[7],
           c.group_count[8], c.group_count[9]);
    printf("UNITCARD|rect|x=%.0f|y=%.0f|w=%.0f|h=%.0f|scale=%.2f\n",
           L.x0, L.y0, (float)H6_CARD_W * L.s, (float)H6_CARD_H * L.s, L.s);
    for (int g = 0; g < H6_CARD_TABS; g++) {
        float cx, cy;
        uc_control_centre(UC_TAB, g, fbw, fbh, &cx, &cy);
        printf("UNITCARD|tab|key=%d|slot=%d|count=%d|frame=%d|at=%.0f,%.0f\n",
               (g + 1) % 10, g, c.group_count[g], c.group_frame[g], cx, cy);
    }
    for (int m = 0; m < c.nmini; m++) {
        float cx, cy;
        const int oi = uc_object_for_key(g_ucMiniKey[m]);
        uc_control_centre(UC_MINI, m, fbw, fbh, &cx, &cy);
        printf("UNITCARD|mini|slot=%d|type=%s|id=%d|at=%.0f,%.0f\n", m,
               oi >= 0 ? g_objects[oi].type : "+",
               oi >= 0 ? g_objects[oi].id : -1, cx, cy);
    }
    fflush(stdout);
}

static void uc_script_click(const char* arg, int fbw, int fbh)
{
    char what[16] = {0};
    int idx = 0;
    if (!arg || sscanf(arg, "%15s %d", what, &idx) < 1) {
        printf("UNITCARD|cardclick wants tab N | mini N | main\n");
        return;
    }
    int w = UC_NONE;
    if (!strcmp(what, "tab"))  w = UC_TAB;
    if (!strcmp(what, "mini")) w = UC_MINI;
    if (!strcmp(what, "main")) w = UC_MAIN;
    if (w == UC_NONE) { printf("UNITCARD|cardclick|unknown control %s\n", what); return; }
    /* The picture decides what is clickable, so make sure it has been drawn once. */
    g_ucShownNow = uc_shown();
    if (g_ucShownNow) { H6_Card c; uc_fill(&c, fbw, fbh); }
    float cx, cy;
    uc_control_centre(w, idx, fbw, fbh, &cx, &cy);
    const bool took = uc_click(cx, cy, fbw, fbh);
    printf("UNITCARD|cardclick|%s|%d|at=%.0f,%.0f|%s\n", what, idx, cx, cy,
           took ? "taken" : "NOT ON THE CARD");
    fflush(stdout);
}

#endif /* UNITCARD_MOD_H */
