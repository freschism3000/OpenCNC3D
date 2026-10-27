/* Regression gate: legacy supersampling and imported-tree preferences must not
   override the new native-resolution / accepted-tree defaults. */
#include "fx_state.h"
#include <cassert>
int main(int argc, char **argv) {
 assert(argc==2); FxState s; fx_defaults(&s);
 assert(s.ss_scale==1.0f && s.tree3d==1);
 for(int m=0;m<FX_MODE_COUNT;m++) assert(s.mode_row[m][FXR_SS]==1.0f);
 FILE *f=fopen(argv[1],"w"); assert(f);
 fputs("ss_scale 2\nmode.enhanced.ss_scale 3\ntree3d 0\nmode.enhanced.tree3d 0\n",f); fclose(f);
 assert(fx_load(&s,argv[1])); assert(s.ss_scale==1.0f && s.tree3d==1);
 assert(s.mode_row[FX_MODE_ENHANCED][FXR_SS]==1.0f);
 assert(s.mode_row[FX_MODE_ENHANCED][FXR_TREES]==1.0f);
 s.tree3d=0; s.mode_row[FX_MODE_ENHANCED][FXR_TREES]=0;
 assert(fx_save(&s,argv[1])); fx_defaults(&s); assert(fx_load(&s,argv[1]));
 assert(s.tree3d==0 && s.mode_row[FX_MODE_ENHANCED][FXR_TREES]==0 && s.ss_scale==1.0f);
 remove(argv[1]); puts("VISDEFAULTS|PASS|native resolution, legacy migration, persistent Enhanced tree choice");
}
