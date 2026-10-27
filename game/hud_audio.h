#ifndef OPENCNC3D_HUD_AUDIO_H
#define OPENCNC3D_HUD_AUDIO_H
/* THE HUD SOUNDS PLAY ON THE SIDEBAR'S OWN BUTTONS AND NOWHERE ELSE: each production
   entry, the scroll arrows, repair, sell, power, map and the category tabs. Empty
   panel, radar and credit surfaces are not buttons. The screen-edge plates along the
   top (OPTIONS, DATABASE, SIDEBAR) are menu buttons, not part of the bar, and stay
   silent; so does the selected-unit card. Enhanced menus own their separate
   feedback route while Options or the Database is open. */
static int hud_audio_hit(float x, float y, int w, int h)
{
    if (!g_fx.enabled || g_editOn || fxp_is_open() || g_optOpen || g_cxOpen) return -1;
    sb_layout(w, h);
    int col = -1, slot = -1;
    const SbHit hit = sb_hit_at(x, y, &col, &slot);
    if (hit == SBH_ITEM) {
        const EH_Display* display = eh_enabled() ? eh_display(col, slot) : NULL;
        const int idx = eh_enabled() ? (display ? display->engine : -1)
                                    : sb_col_base(col) + g_sbTop[col] + slot;
        if (idx < 0 || idx >= (int)g_sbState.entry.size() ||
            (!eh_enabled() && g_sbState.entry[idx].column != col)) return -1;
        return 1000 + idx;
    }
    if (hit == SBH_NONE || hit == SBH_RADAR || hit == SBH_CREDITS || hit == SBH_LOCKED ||
        hit == SBH_OPTIONS || hit == SBH_DATABASE || hit == SBH_SIDEBAR) return -1;
    return 100 + hit * 4 + (col >= 0 ? col : 0);
}

static void hud_audio_event(const SDL_Event& e, int w, int h, float sx, float sy)
{
    if (e.type == SDL_MOUSEMOTION) {
        int hit = hud_audio_hit(e.motion.x * sx, e.motion.y * sy, w, h);
        if (hit >= 0 && hit != g_ambHover) ambience_hud_sound(false);
        g_ambHover = hit;
    } else if (e.type == SDL_MOUSEBUTTONDOWN &&
               (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT)) {
        int hit = hud_audio_hit(e.button.x * sx, e.button.y * sy, w, h);
        if (hit >= 0) ambience_hud_sound(true);
        g_ambHover = hit;
    } else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_LEAVE) {
        g_ambHover = -1;
    }
}
#endif
