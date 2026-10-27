/* Shared Enhanced defaults agree in both games; RA feature migration and saved choices survive. */
#include <cassert>
#include <cstdio>
#include <string>
#include "../../game/fx_state.h"
static void fixture(const std::string& path, bool classic) {
    FILE* f=fopen(path.c_str(),"w");assert(f);
    fprintf(f,"enh_textures 1\nenh_tree_toggle 1\nenabled %d\ndos_classic %d\n"
              "new_hud 0\ntree3d 0\ngrass 0\ntib3d 0\nfol_on 0\nrock_on 0\nmottle_on 0\n"
              "mode.enhanced.new_hud 0\nmode.enhanced.tree3d 0\nmode.enhanced.grass 0\n"
              "vsync 2\nres_w 1777\nres_h 999\nshadow_strength 0.37\n"
              "object_shadow_fill 0.42\ngrass_terrain_tint -0.25\n",!classic,classic);
    fclose(f);
}
static void check(const FxState& s, int value) {
    assert(s.new_hud==value&&s.tree3d==value&&s.grass==value);
    for(int r:{FXR_NEWHUD,FXR_TREES,FXR_GRASS})assert(s.mode_row[FX_MODE_ENHANCED][r]==value);
    assert(s.vsync==2&&s.res_w==1777&&s.res_h==999&&fabs(s.shadow_strength-0.37)<1e-5);
    assert(fabs(s.object_shadow_fill-0.42f)<1e-5&&fabs(s.grass_terrain_tint+0.25f)<1e-5);
}
static void check_shared_defaults(const FxState& s) {
    assert(fabs(s.sun_energy-1.07143f)<1e-5);
    assert(fabs(s.shadow_black_limit-0.02813f)<1e-5);
    assert(fabs(s.terrain_brightness-1.14286f)<1e-5);
    assert(fabs(s.terrain_shadow_strength-0.60714f)<1e-5);
    assert(fabs(s.desert_shadow_strength-0.60714f)<1e-5);
    assert(fabs(s.sand_shadow_strength-0.60714f)<1e-5);
    assert(fabs(s.tree3d_ao_floor-0.63393f)<1e-5);
    assert(fabs(s.object_shadow_fill-0.30f)<1e-5);
}
int main(int argc,char**argv) {
    assert(argc==2);const std::string path=argv[1];FxState s;
    fx_red_alert_dials(1);fx_defaults(&s);
    check_shared_defaults(s);
    assert(s.new_hud&&s.tree3d&&s.grass);
    for(int r:{FXR_NEWHUD,FXR_TREES,FXR_GRASS}){
        assert(s.mode_row[FX_MODE_ENHANCED][r]==1);
        assert(s.mode_row[FX_MODE_CLASSIC][r]==0&&s.mode_row[FX_MODE_N64][r]==0);
    }
    fx_red_alert_dials(0);fx_defaults(&s);check_shared_defaults(s);
    for(int game=0;game<2;game++) {
        fx_red_alert_dials(game);fx_defaults(&s);
        for(int r:{FXR_TREES,FXR_TIB3D,FXR_FOLIAGE,FXR_ROCKS,FXR_PATCHES}) {
            assert(fx_mode_live(&s,r)==1&&s.mode_row[FX_MODE_ENHANCED][r]==1);
            assert(s.mode_row[FX_MODE_CLASSIC][r]==0&&s.mode_row[FX_MODE_N64][r]==0);
        }
        fixture(path,false);assert(fx_load(&s,path.c_str()));
        for(int r:{FXR_TREES,FXR_TIB3D,FXR_FOLIAGE,FXR_ROCKS,FXR_PATCHES}) {
            assert(fx_mode_live(&s,r)==1&&s.mode_row[FX_MODE_ENHANCED][r]==1);
            fx_mode_set_live(&s,r,0);
        }
        assert(fx_save(&s,path.c_str()));fx_defaults(&s);assert(fx_load(&s,path.c_str()));
        for(int r:{FXR_TREES,FXR_TIB3D,FXR_FOLIAGE,FXR_ROCKS,FXR_PATCHES})
            assert(fx_mode_live(&s,r)==0&&s.mode_row[FX_MODE_ENHANCED][r]==0);
    }
    fx_red_alert_dials(1);fx_defaults(&s);
    fixture(path,false);assert(fx_load(&s,path.c_str()));check(s,1);
    s.new_hud=s.tree3d=s.grass=0;assert(fx_save(&s,path.c_str()));
    fx_defaults(&s);assert(fx_load(&s,path.c_str()));check(s,0);
    fixture(path,true);fx_defaults(&s);assert(fx_load(&s,path.c_str()));
    assert(s.new_hud==0&&s.tree3d==0&&s.grass==0&&fx_mode_of(&s)==FX_MODE_CLASSIC);
    for(int r:{FXR_NEWHUD,FXR_TREES,FXR_GRASS})assert(s.mode_row[FX_MODE_ENHANCED][r]==1);
    fx_red_alert_dials(0);fixture(path,false);fx_defaults(&s);assert(fx_load(&s,path.c_str()));
    assert(!s.new_hud&&s.tree3d&&!s.grass);
    remove(path.c_str());puts("RASETTINGS|PASS|shared RA3D CFG defaults in TD and RA; one-time RA feature migration; saved opt-outs, Classic, per-user display and visual choices preserved");
}
