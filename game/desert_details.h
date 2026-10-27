#ifndef CNC3D_DESERT_DETAILS_H
#define CNC3D_DESERT_DETAILS_H

// Saved source-template placements, using the two shipped bush mesh families.
// The model pack, alternate ground matte and placement table are one capability:
// an unavailable family leaves the original source painting as the fallback.
struct DesertDetail { unsigned tid,icon,var,model;float x,z,scale,yaw; };
static std::map<unsigned,std::vector<DesertDetail> > g_desertDetails;
static std::map<unsigned,unsigned> g_desertDetailNeeds;
static bool g_desertDetailHave=false;
static unsigned desert_detail_state(void)
{
    if(!g_desertDetailHave || g_raModule || !g_fx.enabled || g_fx.dos_classic || fx_texset_get()!=FX_TEX_ENH
       || (strcmp(g_pack.theater,"DESERT") && strcmp(g_pack.theater,"SAND")))return 0;
    return g_folHave && g_fx.fol_on?1u:0u;
}
static bool desert_detail_replaces(unsigned tid)
{
    auto it=g_desertDetailNeeds.find(tid);const unsigned s=desert_detail_state();
    return it!=g_desertDetailNeeds.end() && it->second && (s&it->second)==it->second;
}
static void desert_details_load(void)
{
    g_desertDetails.clear();g_desertDetailNeeds.clear();g_desertDetailHave=false;
    if(g_raModule || (strcmp(g_pack.theater,"DESERT") && strcmp(g_pack.theater,"SAND")))return;
    std::vector<unsigned char> raw;FILE* f=fopen("desert_details.pack","rb");if(!f)return;
    if(fseek(f,0,SEEK_END)==0) {long n=ftell(f);if(n>=16 && n<1024*1024 && fseek(f,0,SEEK_SET)==0){raw.resize((size_t)n);if(fread(raw.data(),1,raw.size(),f)!=raw.size())raw.clear();}}fclose(f);
    if(raw.size()<16 || memcmp(raw.data(),"C3DDECO\0",8) || cb_u32(raw.data()+8)!=1)return;
    const unsigned count=cb_u32(raw.data()+12);if(raw.size()!=16+(size_t)count*24)return;
    for(unsigned i=0;i<count;i++) {
        const unsigned char* p=raw.data()+16+i*24;DesertDetail d;
        d.tid=p[0]|(p[1]<<8);d.icon=p[2]|(p[3]<<8);d.var=p[4];d.model=p[5];
        memcpy(&d.x,p+8,4);memcpy(&d.z,p+12,4);memcpy(&d.scale,p+16,4);memcpy(&d.yaw,p+20,4);
        if(d.var>3 || d.model>1 || !std::isfinite(d.x) || !std::isfinite(d.z) || !std::isfinite(d.scale) || !std::isfinite(d.yaw) || d.scale<=0 || d.scale>8)return;
        g_desertDetails[(d.tid<<16)|(d.icon<<2)|d.var].push_back(d);
        g_desertDetailNeeds[d.tid]|=1u;
    }
    // The alternate matte must actually be installed with the meshes. A code-only
    // copy without it must not suppress the source relief or duplicate its paint.
    FILE* masks=fopen("groundmask_DESERT.pack","rb");bool matte=false;
    if(masks){unsigned char h[20];if(fread(h,1,20,masks)==20 && !memcmp(h,"C3DGRAS\0",8)) {
        const unsigned n=cb_u32(h+16);if(n<=8192)for(unsigned i=0;i<n;i++){unsigned char k[24];if(fread(k,1,24,masks)!=24)break;if(k[22]==1)matte=true;}
    }fclose(masks);}
    if(!matte)return;
    g_desertDetailHave=true;fprintf(stderr,"DESERTDETAIL|loaded|placements=%u|templates=%zu|families=2\n",count,g_desertDetailNeeds.size());
}
struct DesertDetailCell { int x,z;unsigned tid,key; };
static std::vector<DesertDetailCell> g_desertDetailCells;
static void desert_details_prepare(void)
{
    g_desertDetailCells.clear();if(!desert_detail_state())return;
    int n=0;const TtSlot* table=tt_table(g_pack.theater,&n);std::vector<unsigned> tid(n,65535),icon(n,0);
    for(int i=0;i<n;i++)if(table[i].slot<n){tid[table[i].slot]=table[i].tmpl;icon[table[i].slot]=table[i].icon;}
    for(const auto& c:g_pack.cell) {
        const int s=ts_slot_of_cell(c,n);if(s<0 || !desert_detail_replaces(tid[s]) || !cell_shown(c.x,c.y) || shroud_cell_hidden(c.x,c.y))continue;
        const unsigned key=(tid[s]<<16)|(icon[s]<<2)|(unsigned)enh_cell_variant(c,s);
        if(g_desertDetails.count(key))g_desertDetailCells.push_back({c.x,c.y,tid[s],key});
    }
}
static void desert_details_place_chunk(int cx,int cy,std::vector<FolInst>& list,FolChunk& ch)
{
    for(const auto& c:g_desertDetailCells) {
        if(c.x/FOL_CHUNK!=cx || c.z/FOL_CHUNK!=cy)continue;
        const auto& data=g_desertDetails[c.key];int k=0;
        for(const auto& d:data) {
            const int model=g_folBush[d.model];if(model<0)continue;
            const float x=c.x+d.x,z=c.z+d.z;
            bool blocked=false;for(const auto& o:g_objects)if(o.kind==K_BUILDING && !o.limbo && fabsf(x-o.ex)<o.fw*.5f && fabsf(z-o.ez)<o.fh*.5f){blocked=true;break;}
            if(blocked)continue;
            FolInst fi;fi.cx=c.x;fi.cy=c.z;fi.model=model;fi.x=x;fi.z=z;
            fi.h=d.scale*(g_fx.fol_bush_size/.325f);
            fi.id=fol_id_for(c.x,c.z,0,128+k++,x,z);g_rockIdYaw[fi.id]=d.yaw*.0174532925f;
            const int gx=(int)floorf(x),gz=(int)floorf(z);
            if(gx>=0&&gz>=0&&gx<g_gridW&&gz<g_gridH)g_folCellIds[(size_t)gz*g_gridW+gx].push_back(fi.id);
            list.push_back(fi);ch.nbush++;
        }
    }
}
#endif
