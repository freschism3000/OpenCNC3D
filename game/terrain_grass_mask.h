#ifndef OPENCNC3D_TERRAIN_GRASS_MASK_H
#define OPENCNC3D_TERRAIN_GRASS_MASK_H
#include <map>

/* Explicit ground coverage from the Remaster Lab's saved Outline Checks.
   No surface is classified from RGB at runtime. The two-channel pack has:
   C3DGRAS\0, LE u32 version=1, cell, count; count 24-byte keys (name[16],
   u16 template, u16 icon, u8 variant, u8 game (0 TD/1 RA), u16 reserved),
   then count cell*cell LA8 images. L is grass coverage; A is baked ground
   shade in units of 1/128. Slot zero is neutral white for ordinary CLEAR1. */
struct TerrainGrassMasks {
    bool tried=false;
    std::string theater;
    GLuint texture=0;
    int cell=1,pitch=1,columns=1,width=1,height=1,gutter=0;
    std::vector<unsigned char> coverage; // authored mask downsampled to 24x24, about 1.2 MiB
    std::map<unsigned,int> td,td3d;
    std::map<unsigned,bool> softBoundary; // authored shore/river faces can meet ordinary ground
    std::map<std::string,int> ra;
};
static TerrainGrassMasks g_terrainGrassMasks;

static std::string terrain_grass_ra_key(const char* name,int icon,int variant)
{ char end[40];snprintf(end,sizeof end,"/%d/%d",icon,variant);return std::string(name)+end; }

static void terrain_grass_masks_ensure(void)
{
    TerrainGrassMasks& M=g_terrainGrassMasks;
    const std::string theater = g_raModule && !strcmp(g_pack.theater,"RASNOW") ? "RA_SNOW"
        : !strcmp(g_pack.theater,"DESERT") ? "DESERT" : !strcmp(g_pack.theater,"SAND") ? "SAND" : "TEMPERAT";
    if(M.tried && M.theater==theater)return;
    if(M.texture)glDeleteTextures(1,&M.texture);
    M=TerrainGrassMasks(); M.theater=theater; M.tried=true;
    std::vector<unsigned char> data;
    const char* overridePath=getenv("OPENCNC3D_GRASSMASK");
    const bool disabled=overridePath && (!strcmp(overridePath,"off") || !strcmp(overridePath,"none"));
    std::string path;
    if(!disabled) {
        const char* roots[]={"","game/","../game/","../"};
        for(size_t i=0;i<sizeof roots/sizeof roots[0];++i) {
            path=(overridePath && *overridePath)?overridePath:std::string(roots[i])+(theater=="RA_SNOW"?"groundmask_RA_SNOW.pack":theater=="DESERT"?"groundmask_DESERT.pack":theater=="SAND"?"groundmask_SAND.pack":"grassmask_TEMPERAT.pack");
            FILE* f=fopen(path.c_str(),"rb");
            if(!f)continue;
            if(fseek(f,0,SEEK_END)==0) {
                const long size=ftell(f);
                if(size>=20 && size<=192L*1024*1024 && fseek(f,0,SEEK_SET)==0) {
                    data.resize((size_t)size);
                    if(fread(&data[0],1,data.size(),f)!=data.size())data.clear();
                }
            }
            fclose(f);break;
        }
    }
    unsigned count=0,cell=0;
    bool valid=data.size()>=20 && !memcmp(&data[0],"C3DGRAS\0",8) && cb_u32(&data[8])==1;
    if(valid) {
        cell=cb_u32(&data[12]);count=cb_u32(&data[16]);
        valid=cell>=24 && cell<=240 && count>0 && count<=4096
            && data.size()==20+(size_t)count*24+(size_t)count*cell*cell*2;
    }
    std::vector<unsigned char> pixels(2);pixels[0]=255;pixels[1]=128;
    if(valid) {
        M.cell=(int)cell;M.gutter=2;M.pitch=M.cell+4;
        M.columns=(int)std::ceil(std::sqrt((double)count+1));
        M.width=M.columns*M.pitch;M.height=((int)count+M.columns)/M.columns*M.pitch;
        GLint maxSize=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maxSize);
        valid=M.width<=maxSize && M.height<=maxSize;
    }
    if(valid) {
        M.coverage.assign(((size_t)count+1)*24*24,255);
        for(unsigned slot=1;slot<=count;slot++)for(int y=0;y<24;y++)for(int x=0;x<24;x++) {
            unsigned sum=0,samples=0;
            const int x0=x*(int)cell/24,x1=(x+1)*(int)cell/24,y0=y*(int)cell/24,y1=(y+1)*(int)cell/24;
            for(int sy=y0;sy<y1;sy++)for(int sx=x0;sx<x1;sx++) {
                sum+=data[20+(size_t)count*24+((size_t)(slot-1)*cell*cell+sy*cell+sx)*2];samples++;
            }
            M.coverage[((size_t)slot*24+y)*24+x]=(unsigned char)(sum/std::max(1u,samples));
        }
        pixels.assign((size_t)M.width*M.height*2,0);
        for(unsigned slot=0;slot<=count;++slot) {
            const int gx=(int)(slot%M.columns)*M.pitch,gy=(int)(slot/M.columns)*M.pitch;
            for(int y=0;y<M.pitch;++y)for(int x=0;x<M.pitch;++x) {
                const int sx=std::max(0,std::min(M.cell-1,x-M.gutter));
                const int sy=std::max(0,std::min(M.cell-1,y-M.gutter));
                unsigned char* dest=&pixels[((size_t)(gy+y)*M.width+gx+x)*2];
                if(!slot){dest[0]=255;dest[1]=128;}
                else {
                    const size_t offset=20+(size_t)count*24+((size_t)(slot-1)*cell*cell+sy*cell+sx)*2;
                    dest[0]=data[offset];dest[1]=data[offset+1];
                }
            }
            if(!slot)continue;
            const unsigned char* key=&data[20+(size_t)(slot-1)*24];
            char name[17];memcpy(name,key,16);name[16]=0;
            const unsigned tid=(unsigned)key[16]|((unsigned)key[17]<<8);
            const unsigned icon=(unsigned)key[18]|((unsigned)key[19]<<8),var=key[20];
            if(var>3 || key[21]>1 || !*name){valid=false;break;}
            if(key[22]==1 && key[21]==0)M.td3d[(tid<<16)|(icon<<2)|var]=(int)slot;
            else if(key[21])M.ra[terrain_grass_ra_key(name,(int)icon,(int)var)]=(int)slot;
            else M.td[(tid<<16)|(icon<<2)|var]=(int)slot;
            if(!key[21] && (!strncmp(name,"SH",2) || !strncmp(name,"RV",2)
                || !strncmp(name,"FORD",4) || !strncmp(name,"FALLS",5)))M.softBoundary[tid]=true;
        }
    }
    if(!valid) {
        M.coverage.clear();M.td.clear();M.td3d.clear();M.ra.clear();M.softBoundary.clear();M.cell=M.pitch=M.columns=M.width=M.height=1;M.gutter=0;
        pixels.assign(2,0);pixels[0]=255;pixels[1]=128;
        if(!data.empty())fprintf(stderr,"GRASSMASK|invalid pack|%s\n",path.c_str());
    }
    GLint active=GL_TEXTURE0;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
    glPushAttrib(GL_TEXTURE_BIT);glActiveTexture(GL_TEXTURE0);
    glGenTextures(1,&M.texture);glBindTexture(GL_TEXTURE_2D,M.texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    GLint unpack=4;glGetIntegerv(GL_UNPACK_ALIGNMENT,&unpack);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexImage2D(GL_TEXTURE_2D,0,GL_LUMINANCE_ALPHA,M.width,M.height,0,GL_LUMINANCE_ALPHA,GL_UNSIGNED_BYTE,&pixels[0]);
    glPixelStorei(GL_UNPACK_ALIGNMENT,unpack);glPopAttrib();glActiveTexture((GLenum)active);
    if(theater=="DESERT")fprintf(stderr,"GROUNDMASK|desert|cells=%u|neutral=%d|valid=%d\n",valid?count:0,data.empty()?1:0,valid?1:0);
    else fprintf(stderr,"GRASSMASK|loaded|cells=%u|td=%lu|ra=%lu|atlas=%dx%d|valid=%d\n",
            valid?count:0,(unsigned long)M.td.size(),(unsigned long)M.ra.size(),M.width,M.height,valid?1:0);
}

static int terrain_grass_mask_td(unsigned tid,unsigned icon,int variant)
{
    terrain_grass_masks_ensure();
    if(desert_detail_replaces(tid)) {
        const auto& alt=g_terrainGrassMasks.td3d;auto i=alt.find((tid<<16)|(icon<<2)|(unsigned)variant);
        if(i!=alt.end())return i->second;
    }
    const std::map<unsigned,int>& m=g_terrainGrassMasks.td;
    std::map<unsigned,int>::const_iterator it=m.find((tid<<16)|(icon<<2)|(unsigned)variant);
    return it==m.end()?-1:it->second;
}
static int terrain_grass_mask_ra(const char* name,int icon,int variant)
{
    terrain_grass_masks_ensure();
    const std::map<std::string,int>& m=g_terrainGrassMasks.ra;
    std::map<std::string,int>::const_iterator it=m.find(terrain_grass_ra_key(name,icon,variant));
    return it==m.end()?-1:it->second;
}
static void terrain_grass_mask_uv(int slot,float x,float y,float* out)
{
    const TerrainGrassMasks& M=g_terrainGrassMasks;
    if(slot<0)slot=0;
    out[0]=((slot%M.columns)*M.pitch+M.gutter+x*M.cell)/(float)M.width;
    out[1]=((slot/M.columns)*M.pitch+M.gutter+y*M.cell)/(float)M.height;
}
#endif
