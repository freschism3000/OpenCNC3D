#ifndef CNC3D_WATER_STATIC_MATERIAL_H
#define CNC3D_WATER_STATIC_MATERIAL_H
#include "water_static_tile.h"
static std::vector<unsigned char> water_static_td_tile(int cell)
{
    const char* file=water_td_painting_file();if(!file)file="waterbase_RA_TEMPERAT.pack";
    const char* overridePath=getenv("OPENCNC3D_WATERBASE");
    if(overridePath && (!strcmp(overridePath,"off")||!strcmp(overridePath,"none")))return {};
    const char* roots[]={"","game/","../game/","../"};
    for(const char* root:roots) {
        const std::string path=overridePath&&*overridePath?overridePath:std::string(root)+file;FILE* f=fopen(path.c_str(),"rb");if(!f)continue;
        std::vector<unsigned char> d(16+288*288*3);const size_t n=fread(d.data(),1,d.size(),f);const bool exact=n==d.size()&&fgetc(f)==EOF;fclose(f);
        if(!exact||memcmp(d.data(),"C3DWATR\0",8)||rt_le32(d.data()+8)!=1||rt_le32(d.data()+12)!=288)return {};
        return water_static_tile(std::vector<unsigned char>(d.begin()+16,d.end()),288,cell);
    }
    return {};
}
static void water_static_atlas_prepare(RtAtlas& a)
{
    const int cell=144,pitch=156,gutter=6;if(a.uw!=TT_COLS*pitch)return;const auto tile=water_static_td_tile(cell);if(tile.empty())return;
    int count=0;const TtSlot* table=tt_table(g_pack.theater,&count);size_t painted=0;
    auto paint=[&](int atlasSlot,int tid,int icon,int var) {
        const int x0=(atlasSlot%TT_COLS)*pitch+gutter,y0=(atlasSlot/TT_COLS)*pitch+gutter;
        if(x0+cell>a.w||y0+cell>a.h)return;
        if(!coast_mask_tile_wet(tid,icon,var))return;
        std::vector<unsigned char> mask((size_t)cell*cell*4,255);
        if(!coast_mask_apply(mask.data(),cell,cell,cell*4,tid,NULL,icon,var))return;
        water_static_composite(a.rgba+((size_t)y0*a.w+x0)*4,cell,(size_t)a.w*4,tile,mask.data()+3,cell*4,4);
        // Reproduce only this icon's edge, including its original alpha.
        for(int y=-gutter;y<cell+gutter;y++)for(int x=-gutter;x<cell+gutter;x++)if(x<0||y<0||x>=cell||y>=cell)
            memcpy(a.rgba+((size_t)(y0+y)*a.w+x0+x)*4,a.rgba+((size_t)(y0+std::max(0,std::min(cell-1,y)))*a.w+x0+std::max(0,std::min(cell-1,x)))*4,4);
        painted++;
    };
    for(int i=0;i<count;i++)paint(table[i].slot,table[i].tmpl,table[i].icon,0);
    for(int i=0;i<a.nvar;i++){const int* v=a.var+(size_t)i*RT_VAR_INTS;for(int j=0;j<count;j++)if(table[j].slot==v[0]){paint(v[4],table[j].tmpl,table[j].icon,v[1]);break;}}
    fprintf(stderr,"WATER|static-material|td|theater=%s|cells=%zu|same-bank=1|land-alpha-unchanged=1\n",g_pack.theater,painted);
}
static void water_static_seam_alpha(int tid,int icon,int var,int side,unsigned char* rgba,int cell,int margin)
{
    if(!coast_mask_tile_wet(tid,icon,var))return;
    std::vector<unsigned char> mask((size_t)cell*cell*4,255);
    if(!coast_mask_apply(mask.data(),cell,cell,cell*4,tid,NULL,icon,var))return;
    for(int y=0;y<(side?margin:cell);y++)for(int x=0;x<(side?cell:margin);x++) {
        const int sx=side?x:0,sy=side?0:y;unsigned char& a=rgba[((size_t)y*(side?cell:margin)+x)*4+3];
        a=(unsigned char)((a*mask[((size_t)sy*cell+sx)*4+3]+127)/255);
    }
}
#endif
