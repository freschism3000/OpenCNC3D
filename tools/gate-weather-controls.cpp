/* Headless coverage for F5's curated rows and the weather preset contract. */
#include "../game/fx_state.h"
#include "../game/fx_panel_policy.h"
#include <assert.h>

static int visible(const char* key, const FxState& state, int* tab = NULL)
{
    const FxParam* group = NULL;
    for (const FxParam* p = FX_PARAMS; p->kind != FXP_END; p++) {
        if (p->kind == FXP_GROUP) group = p;
        else if (!strcmp(p->key,key)) {
            if (tab) *tab = fxp_group_tab(group);
            return fxp_row_visible(p,group,&state);
        }
    }
    assert(!"Unknown preset key"); return 0;
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    FxState state; fx_defaults(&state);
    const char* hidden[] = { "gamma_on", "crt_on", "rain_fx", "relight_on", "debug",
        "grass", "m3_on", "new_audio", "res_w", "cash_tick", "m3inf_on", "einf_on",
        "uvfx_on", "curgreen_on", "efx_enh", "ecur_on", 0 };
    for (int i = 0; hidden[i]; i++) assert(!visible(hidden[i],state));
    const char* keep[] = { "shadow_on", "bloom_on", "ssao_on", "lights_on", "tree3d",
        "tib3d", "fol_on", "rock_on", "show_stats", "fog_fx", "fog_light", "sand_fx", "sand_wind", 0 };
    for (int i = 0; keep[i]; i++) assert(visible(keep[i],state));
    int tab = -1;
    assert(visible("fog_fx",state,&tab) && tab == 1);
    assert(visible("sand_fx",state,&tab) && tab == 1);
    assert(visible("dfog_on",state,&tab) && tab == 1);
    assert(visible("tibfog_on",state,&tab) && tab == 1);
    assert(visible("shadow_on",state,&tab) && tab == 0);
    state.experimental_textures = 1;
    assert(!visible("m3_on",state) && visible("fol_on",state));
    assert(!visible("uvfx_on",state) && !visible("curgreen_on",state));
    state.perspective = FX_PERSP_ISO; assert(visible("iso_yaw",state));
    state.experimental_textures = 0;

    /* Hidden legacy keys and new profiles must survive the same round trip. */
    state.fog_density = 0.125f; state.sand_speed = 2.5f;
    state.crt_on = 1; state.rain_fx = 1;
    assert(fx_save(&state,argv[1]));
    FxState loaded; fx_defaults(&loaded); assert(fx_load(&loaded,argv[1]));
    for (const FxParam* p = FX_PARAMS; p->kind != FXP_END; p++) {
        if (p->kind == FXP_GROUP) continue;
        if (p->kind == FXP_BOOL) assert(*fx_iptr(&state,p) == *fx_iptr(&loaded,p));
        else assert(fabsf(*fx_fptr(&state,p)-*fx_fptr(&loaded,p)) < 0.000011f);
    }
    puts("PASS: Enhanced/Weather row visibility and complete preset round trip");
}
