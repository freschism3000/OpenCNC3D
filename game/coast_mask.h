#ifndef OPENCNC3D_COAST_MASK_H
#define OPENCNC3D_COAST_MASK_H
/* Authored land coverage from complete native coast images, before cell slicing.
   C3DCOAS\0, LE version/cell/count, 24-byte keys matching the grass-mask contract,
   then A8 images. A missing/invalid pack keeps the original cut, never guesses RGB.
   One pack per Tiberian Dawn bank: TEMPERAT (which also carries Red Alert's coasts,
   looked up by template name), DESERT, SAND, and WINTER and SNOW, whose 1995 tiles paint
   their water and so have no cut of their own at all (tools/bakery/pack_coast_masks.py). */
struct CoastMasks {
    bool tried=false; int cell=0;
    std::string theater;
    std::vector<unsigned char> alpha;
    std::vector<unsigned char> wet;
    std::map<unsigned,int> td;
    std::map<std::string,int> ra;
};
static CoastMasks g_coastMasks;
static std::string coast_ra_key(const char* name,int icon,int var)
{ char b[48];snprintf(b,sizeof b,"/%d/%d",icon,var);return std::string(name)+b; }
static void coast_masks_ensure(void)
{
    CoastMasks& M=g_coastMasks;
    const std::string theater=!strcmp(g_pack.theater,"DESERT")?"DESERT":!strcmp(g_pack.theater,"SAND")?"SAND"
        :!strcmp(g_pack.theater,"WINTER")?"WINTER":!strcmp(g_pack.theater,"SNOW")?"SNOW":"TEMPERAT";
    if(M.tried&&M.theater==theater)return;
    M=CoastMasks();M.tried=true;M.theater=theater;
    std::vector<unsigned char> d;std::string path;
    const char* overridePath=getenv("OPENCNC3D_COASTMASK");
    if(overridePath&&(!strcmp(overridePath,"off")||!strcmp(overridePath,"none")))return;
    const char* roots[]={"","game/","../game/","../"};
    for(size_t i=0;i<sizeof roots/sizeof roots[0];i++) {
        path=overridePath&&*overridePath?overridePath:std::string(roots[i])+"coastmask_"+theater+".pack";
        FILE* f=fopen(path.c_str(),"rb");if(!f)continue;
        fseek(f,0,SEEK_END);long size=ftell(f);
        if(size>=20&&size<=96L*1024*1024&&fseek(f,0,SEEK_SET)==0) {
            d.resize((size_t)size);if(fread(&d[0],1,d.size(),f)!=d.size())d.clear();
        }
        fclose(f);break;
    }
    if(d.empty())return;
    unsigned cell=0,count=0;
    bool valid=d.size()>=20&&!memcmp(&d[0],"C3DCOAS\0",8)&&rt_le32(&d[8])==1;
    if(valid){cell=rt_le32(&d[12]);count=rt_le32(&d[16]);valid=cell>=24&&cell<=240&&count>0&&count<=4096&&d.size()==20+(size_t)count*24+(size_t)count*cell*cell;}
    if(valid)for(unsigned i=0;i<count;i++) {
        const unsigned char* k=&d[20+(size_t)i*24];char name[17];memcpy(name,k,16);name[16]=0;
        const unsigned tid=k[16]|(k[17]<<8),icon=k[18]|(k[19]<<8),var=k[20],game=k[21];
        if(!*name||var>3||game>1){valid=false;break;}
        if(game)M.ra[coast_ra_key(name,icon,var)]=i;else M.td[(tid<<16)|(icon<<2)|var]=i;
    }
    if(!valid){M.td.clear();M.ra.clear();fprintf(stderr,"COASTMASK|invalid|%s\n",path.c_str());return;}
    M.cell=(int)cell;M.alpha.assign(d.begin()+20+(size_t)count*24,d.end());
    M.wet.assign(count,0);
    for(unsigned i=0;i<count;i++)for(unsigned j=0;j<cell*cell;j++)
        if(M.alpha[(size_t)i*cell*cell+j]<128){M.wet[i]=1;break;}
    if(theater=="WINTER"||theater=="SNOW")
        fprintf(stderr,"COASTMASK|loaded|theater=%s|cells=%u|td=%lu|ra=%lu|cell=%u\n",theater.c_str(),count,(unsigned long)M.td.size(),(unsigned long)M.ra.size(),cell);
    else
        fprintf(stderr,"COASTMASK|loaded|cells=%u|td=%lu|ra=%lu|cell=%u\n",count,(unsigned long)M.td.size(),(unsigned long)M.ra.size(),cell);
}
static bool coast_mask_tile_wet(int tid,int icon,int var)
{
    coast_masks_ensure();const auto& M=g_coastMasks;
    auto i=M.td.find(((unsigned)tid<<16)|((unsigned)icon<<2)|(unsigned)var);
    return i!=M.td.end() && M.wet[i->second];
}
static bool coast_mask_apply(unsigned char* rgba,int w,int h,size_t row,int tid,const char* name,int icon,int var)
{
    coast_masks_ensure();const CoastMasks& M=g_coastMasks;
    if(w!=M.cell||h!=M.cell)return false;
    int slot=-1;
    if(name){auto i=M.ra.find(coast_ra_key(name,icon,var));if(i!=M.ra.end())slot=i->second;}
    else {auto i=M.td.find(((unsigned)tid<<16)|((unsigned)icon<<2)|var);if(i!=M.td.end())slot=i->second;}
    if(slot<0)return false;
    const unsigned char* a=&M.alpha[(size_t)slot*w*h];
    for(int y=0;y<h;y++)for(int x=0;x<w;x++)rgba[(size_t)y*row+x*4+3]=a[(size_t)y*w+x];
    return true;
}
#endif
