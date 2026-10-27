/* F5 must expose independently saved effects/3D-soldier colour, even with
   experimental terrain off. Runs without SDL, GL or any installed game files. */
#include "../game/fx_state.h"
#include "../game/fx_panel_policy.h"
#include <assert.h>

static const FxParam* row(const char* key,const FxParam** group)
{
    const FxParam* found=NULL; *group=NULL;
    for (const FxParam* p=FX_PARAMS;p->kind!=FXP_END;p++) {
        if (p->kind==FXP_GROUP) { if (!found) *group=p; continue; }
        if (!strcmp(p->key,key)) { assert(!found); found=p; }
    }
    assert(found); return found;
}
static void check_group(const char* const* keys,int count,FxState& state,const char* name)
{
    for (int i=0;i<count;i++) {
        const FxParam* group=NULL; const FxParam* p=row(keys[i],&group);
        assert(strstr(group->label,name)); assert(fxp_group_tab(group)==0);
        assert(p->kind==FXP_FLOAT); assert(strlen(p->label)*6<=160);
        assert(*fx_fptr(&state,p)>=p->lo && *fx_fptr(&state,p)<=p->hi);
        assert(fxp_row_visible(p,group,&state));
        state.experimental_textures=1; assert(fxp_row_visible(p,group,&state));
        state.experimental_textures=0;
        /* Different in-range values make missing/aliased schema offsets observable. */
        *fx_fptr(&state,p)=p->lo+(p->hi-p->lo)*(0.21f+0.025f*i);
    }
}
int main(int argc,char** argv)
{
    assert(argc==2);
    const char* effects[]={"efx_bright","efx_contrast","efx_sat","efx_exposure","efx_gamma",
        "efx_warmth","efx_tint","efx_red","efx_green","efx_blue"};
    const char* infantry[]={"inf_bright","inf_contrast","inf_sat","inf_exposure","inf_gamma",
        "inf_warmth","inf_tint","inf_red","inf_green","inf_blue"};
    const char* surface[]={"m3inf_sun","m3inf_fill","m3inf_paint"};
    for (int module=0;module<2;module++) {
        fx_red_alert_dials(module); FxState state; fx_defaults(&state);
        assert(!fx_object_grade_active(fx_effect_grade(&state)));
        assert(!fx_object_grade_active(fx_infantry_grade(&state)));
        check_group(effects,10,state,"EFFECTS");
        assert(fx_object_grade_active(fx_effect_grade(&state)));
        assert(!fx_object_grade_active(fx_infantry_grade(&state)));
        check_group(infantry,10,state,"SOLDIERS");
        check_group(surface,3,state,"SOLDIERS");
        assert(fx_object_grade_active(fx_infantry_grade(&state)));
        assert(fx_save(&state,argv[1]));
        FxState loaded; fx_defaults(&loaded); assert(fx_load(&loaded,argv[1]));
        for (const FxParam* p=FX_PARAMS;p->kind!=FXP_END;p++) {
            if (p->kind==FXP_GROUP || p->kind==FXP_BOOL) continue;
            if (!strncmp(p->key,"efx_",4)||!strncmp(p->key,"inf_",4)||!strncmp(p->key,"m3inf_",6))
                assert(fabsf(*fx_fptr(&state,p)-*fx_fptr(&loaded,p))<0.000011f);
        }
    }
    puts("PASS: TD/RA effects and soldiers F5 visibility, independent neutral grades, unique schema and Save/Load");
}
