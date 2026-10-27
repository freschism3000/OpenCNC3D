/* Curated Enhanced tuning rows. Hidden legacy rows remain in the preset schema.
   Both layout and scripted navigation use this policy, so clicks and labels agree. */
#ifndef OPENCNC3D_FX_PANEL_POLICY_H
#define OPENCNC3D_FX_PANEL_POLICY_H

static int fxp_group_tab(const FxParam* group)
{
    if (!group) return 0;
    const char* label = group->label;
    return strstr(label,"CLOUD SHADOWS") || strstr(label,"MIST / FOG") ||
           strstr(label,"SAND DUST") || strstr(label,"DISTANCE FOG") ||
           strstr(label,"TIBERIUM FOG") || strstr(label,"WATER") ? 1 : 0;
}

static int fxp_row_visible(const FxParam* p, const FxParam* group, const FxState* state)
{
    if (!group) return 0;
    const char* modeOwned[] = { "einf_on", "m3inf_on", "m3_on", "uvfx_on", "curgreen_on",
        "efx_enh", "ecur_on", "grass", "new_audio", 0 };
    for (int index = 0; modeOwned[index]; index++) if (!strcmp(p->key,modeOwned[index])) return 0;
    const char* label = group->label;
    if (strstr(label,"ENHANCED TREES") && !strncmp(p->key,"tree3d_",7)) {
        const char* live[]={"tree3d_wind","tree3d_wind_speed","tree3d_wind_dir","tree3d_flutter",0};
        bool supported=false;for(int i=0;live[i];i++)if(!strcmp(p->key,live[i]))supported=true;
        if(!supported)return 0;
    }
    if (strstr(label,"VI OUTPUT GAMMA") || strstr(label,"CRT") ||
        strstr(label," RAIN ") || strstr(label,"RELIGHT THE CARTRIDGE") ||
        strstr(label,"DEBUG") || !strcmp(label,"NEW AUDIO")) return 0;
    if (strstr(label,"ISOMETRIC") && state->perspective != FX_PERSP_ISO) return 0;
    if (!strcmp(label,"3D GRASS") && !state->grass && !state->experimental_textures) return 0;
    if (strstr(label,"AUTHORED MODELS") && !state->experimental_textures) return 0;
    if (!strcmp(label,"EXPERIMENTAL") && !state->experimental_textures) {
        return !strncmp(p->key,"uvfx_",5) || !strcmp(p->key,"green_pointer") ||
               !strcmp(p->key,"pointer_scale");
    }
    /* Input, audio and display choices live in Options. These rows were saved by
       F5 but most did not apply live there, and do not tune the Enhanced picture. */
    if (!strcmp(label,"MASTER")) {
        const char* hidden[] = { "new_hud", "texset", "infset", "ui_scale", "display_mode",
            "res_w", "res_h", "swap_buttons", "right_drag_scroll", "cash_tick", 0 };
        for (int i = 0; hidden[i]; i++) if (!strcmp(p->key,hidden[i])) return 0;
    }
    return 1;
}

static int fxp_group_visible(const FxParam* group, const FxState* state)
{
    for (const FxParam* p = group+1; p->kind != FXP_END && p->kind != FXP_GROUP; p++)
        if (fxp_row_visible(p,group,state)) return 1;
    return 0;
}
#endif
