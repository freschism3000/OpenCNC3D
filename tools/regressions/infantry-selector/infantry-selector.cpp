#include <cstdio>
#include <cstddef>
#include <cstring>
#include <string>
#include "fx_state.h"
extern "C" {
#include "dosopt.h"
}
#include "production-adapters.inc"
static int checks=0,failures=0;
static void ck(bool ok,const char* name){checks++;if(!ok){failures++;printf("FAIL|%s\n",name);}}
static void apply(void*,const DOPT_Visuals* v){g_fx.enabled=v->mode==FX_MODE_ENHANCED;g_fx.dos_classic=v->mode==FX_MODE_CLASSIC;for(int m=0;m<FX_MODE_COUNT;m++)rows_from_prof(g_fx.mode_row[m],&v->prof[m],g_fx.ss_scale);for(int r=0;r<FXR_COUNT;r++)fx_mode_set_live(&g_fx,r,g_fx.mode_row[v->mode][r]);}
static void seed(DOPT_State* st){memset(st,0,sizeof*st);st->page=DOPT_PAGE_ADVANCED;st->advTab=st->vis.mode=FX_MODE_ENHANCED;st->vis.enh_tex_ok=st->vis.dos_tex_ok=1;st->vis.from_menu=1;for(int m=0;m<FX_MODE_COUNT;m++)prof_from_rows(&st->vis.prof[m],g_fx.mode_row[m],m);float live[FXR_COUNT];for(int r=0;r<FXR_COUNT;r++)live[r]=fx_mode_live(&g_fx,r);prof_from_rows(&st->vis.prof[2],live,2);dopt_bind_visuals(st,apply);}
static bool untouched(const FxState& before,const FxState& after){FxState x=after;x.infset=before.infset;x.m3inf_on=before.m3inf_on;x.einf_on=before.einf_on;for(int m=0;m<FX_MODE_COUNT;m++){x.mode_row[m][FXR_INFSET]=before.mode_row[m][FXR_INFSET];x.mode_row[m][FXR_SOLDIERS]=before.mode_row[m][FXR_SOLDIERS];x.mode_row[m][FXR_EINF]=before.mode_row[m][FXR_EINF];}return memcmp(&before,&x,sizeof x)==0;}
int main(int argc,char**argv){if(argc!=2)return 2;const std::string root=argv[1];
 fx_defaults(&g_fx);g_fx.enabled=1;DOPT_State defaults;seed(&defaults);
 ck(dopt_enh_drop_value(&defaults,DOPT_VE_INFSET)==DOPT_INF_ENHANCED&&g_fx.einf_on&&g_fx.m3inf_on,"Enhanced defaults to production 3D soldiers");
 for(int einf=0;einf<2;einf++)for(int legacy=0;legacy<2;legacy++)for(int fallback=0;fallback<2;fallback++){
  fx_defaults(&g_fx);g_fx.enabled=1;g_fx.einf_on=einf;g_fx.m3inf_on=legacy;g_fx.infset=(float)fallback;g_fx.gamma=1.25f;g_fx.sun_az=123.0f;fx_mode_stash_live(&g_fx);FxState before=g_fx;DOPT_State st;seed(&st);
  ck(dopt_enh_drop_value(&st,DOPT_VE_INFSET)==(einf?3:fallback),"getter names actual production path");
  ck(m3inf_set_now()==bool(einf),"selector reflects actual renderer path gate");
  ck(dopt_drop_count(&st,DOPT_VE_INFSET)==4&&!dopt_drop_listed(DOPT_VE_INFSET,2),"retired slot stays hidden");
  ck(!dopt_enh_drop_allowed(&st,DOPT_VE_INFSET,2),"retired sprites cannot be selected");
  st.page=DOPT_PAGE_VISUALS;dopt_enh_action(&st,DOPT_V_ADVANCED);dopt_drop_open(&st,DOPT_VE_INFSET);dopt_enh_action(&st,DOPT_VE_INFSET);dopt_enh_action(&st,DOPT_A_OK);dopt_enh_action(&st,DOPT_V_OK);
  ck(memcmp(&before,&g_fx,sizeof before)==0,"opening and closing without changes preserves complete FX state");
  st.page=DOPT_PAGE_ADVANCED;st.advTab=2;dopt_enh_choose(&st,DOPT_VE_INFSET,3);
  ck(g_fx.einf_on==1&&g_fx.m3inf_on==1&&m3inf_set_now(),"Enhanced choice enables both flags and production gate");
  ck(g_fx.infset==fallback&&dopt_enh_drop_value(&st,DOPT_VE_INFSET)==3,"Enhanced choice keeps previous sprite fallback");
  ck(untouched(before,g_fx),"choice cannot reset unrelated visual values");
  for(int sprite=0;sprite<2;sprite++){
   dopt_enh_choose(&st,DOPT_VE_INFSET,sprite);ck(g_fx.einf_on==0&&g_fx.m3inf_on==0&&!m3inf_set_now()&&g_fx.infset==sprite,"sprite choice disables production models and uses real fallback");
   ck(dopt_enh_drop_value(&st,DOPT_VE_INFSET)==sprite,"sprite getter agrees with selection");
   const FxState selected=g_fx;dopt_enh_action(&st,DOPT_VE_SOLDIERS);ck(memcmp(&selected,&g_fx,sizeof selected)==0,"retired soldier checkbox cannot override the selector");
  }
  dopt_enh_choose(&st,DOPT_VE_INFSET,3);const std::string file=root+"/roundtrip.cfg";ck(fx_save(&g_fx,file.c_str())!=0,"private save succeeds");FxState restored;fx_defaults(&restored);ck(fx_load(&restored,file.c_str())!=0,"private reload succeeds");ck(restored.einf_on==1&&restored.m3inf_on==1&&restored.infset==1&&restored.mode_row[2][FXR_EINF]==1,"model and fallback choices persist in mode rows");
  ck(g_fx.mode_row[0][FXR_EINF]==0&&g_fx.mode_row[1][FXR_EINF]==0,"Classic and N64 keep their own model choices");
  for(int mode=0;mode<2;mode++){st.advTab=mode;ck(dopt_drop_count(&st,DOPT_VE_INFSET)==2&&!dopt_enh_drop_allowed(&st,DOPT_VE_INFSET,3),"3D option is unavailable outside Enhanced");}
 }
 const int retired[]={DOPT_VE_NEWAUDIO,DOPT_VE_GRASS,DOPT_VE_SOLDIERS,DOPT_VE_MODELS,DOPT_VE_UVFX,DOPT_VE_POINTER};
 for(int item:retired){fx_defaults(&g_fx);g_fx.enabled=1;fx_mode_stash_live(&g_fx);DOPT_State st;seed(&st);const FxState before=g_fx;
  ck(!dopt_vis_in_mode(item,DOPT_MODE_ENHANCED)&&dopt_item_disabled(&st,item),"retired More control is not listed or actionable");
  dopt_enh_action(&st,item);ck(memcmp(&g_fx,&before,sizeof before)==0,"retired More action preserves all settings");
 }
 const int more[]={DOPT_VE_FOLIAGE,DOPT_VE_ROCKS,DOPT_VE_PATCHES,DOPT_VE_RELIEF};
 for(int item:more){fx_defaults(&g_fx);g_fx.enabled=1;g_fx.sun_az=125.0f;g_fx.gamma=1.2f;fx_mode_stash_live(&g_fx);DOPT_State st;seed(&st);FxState before=g_fx;int row=-1;for(auto link:VIS_FXR)if(link.elem==item)row=link.fxr;ck(row>=0,"More control has production field mapping");ck(!dopt_item_disabled(&st,item),"already-drawn More control is usable");
  const int value=!st.vis.prof[2].elem[item];FxState expected=before;fx_mode_set_live(&expected,row,(float)value);expected.mode_row[2][row]=(float)value;
  dopt_enh_action(&st,item);ck(memcmp(&g_fx,&expected,sizeof g_fx)==0,"More checkbox changes only its production field and mode row");dopt_enh_action(&st,item);ck(memcmp(&g_fx,&before,sizeof g_fx)==0,"More checkbox toggles back without resetting any setting");
  st.vis.mode=FX_MODE_CLASSIC;st.advTab=FX_MODE_ENHANCED;apply(NULL,&st.vis);const FxState inClassic=g_fx;dopt_enh_action(&st,item);ck(g_fx.mode_row[2][row]==value&&fx_mode_live(&g_fx,row)==inClassic.mode_row[0][row],"inactive Enhanced choice leaves current Classic picture unchanged");
  dopt_enh_mode(&st,FX_MODE_ENHANCED);ck(fx_mode_live(&g_fx,row)==value,"switching back restores Enhanced-only choice");
 }
 // Before per-mode EINF existed, the live global switch must migrate verbatim.
 for(int on=0;on<2;on++){const std::string file=root+"/old.cfg";FILE*f=fopen(file.c_str(),"w");fprintf(f,"enabled 1\neinf_on %d\nm3inf_on %d\ninfset 1\nmode.enhanced.m3inf_on %d\nenh_tree_toggle 1\nenh_textures 1\n",on,1-on,1-on);fclose(f);FxState s;fx_defaults(&s);ck(fx_load(&s,file.c_str())!=0&&s.einf_on==on&&s.mode_row[2][FXR_EINF]==on,"old production flag migrates without normalization");}
 for(int mode=0;mode<FX_MODE_COUNT;mode++)for(int choice=0;choice<2;choice++){
  const std::string file=root+"/inherent.cfg";FILE* preset=fopen(file.c_str(),"w");
  fprintf(preset,"enabled %d\ndos_classic %d\necur_on 0\nefx_enh 0\ngrass %d\nnew_audio %d\ncurgreen_on %d\nuvfx_on %d\nenh_tree_toggle 1\nenh_textures 1\n",mode==FX_MODE_ENHANCED,mode==FX_MODE_CLASSIC,choice,choice,choice,choice);fclose(preset);
  FxState loaded;fx_defaults(&loaded);ck(fx_load(&loaded,file.c_str())!=0,"stale feature preset loads");
  ck(loaded.ecur_on==1&&loaded.efx_enh==1&&fx_mode_of(&loaded)==mode,"production cursor and effects follow Enhanced without changing selected mode");
  ck(loaded.grass==choice&&loaded.new_audio==choice&&loaded.curgreen_on==choice&&loaded.uvfx_on==choice,"hidden legacy settings are preserved rather than reactivated or disabled");
  ck(!loaded.experimental_textures,"retired experimental art remains off");
 }
 printf("INFANTRY_RESULT|checks=%d|failures=%d\n",checks,failures);return failures?1:0;
}
