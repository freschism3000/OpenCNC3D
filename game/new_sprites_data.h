/* Exact reviewed native pixels and action maps. No animation clock lives here. */
#ifndef OPENCNC3D_NEW_SPRITES_DATA_H
#define OPENCNC3D_NEW_SPRITES_DATA_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <zlib.h>
struct NsAction {
    unsigned count, facings, x0, y0, x1, y1;
    std::vector<unsigned short> frames;
};
struct NsPack {
    unsigned char palette[33][4], paint[33];
    std::vector<NsAction> actions;
    std::vector<unsigned char> pixels;
};
struct NsRead {
    const std::vector<unsigned char>& data; size_t at; bool good;
    unsigned u32() {
        if (at+4>data.size()) { good=false; return 0; }
        const unsigned char* p=&data[at];at+=4;
        return unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16)|(unsigned(p[3])<<24);
    }
    unsigned u16() {
        if (at+2>data.size()) { good=false; return 0; }
        unsigned v=data[at]|(unsigned(data[at+1])<<8);at+=2;return v;
    }
};
static bool ns_read_pack(const char* path, NsPack* result)
{
    FILE* f=fopen(path,"rb");if(!f)return false;
    fseek(f,0,SEEK_END);long length=ftell(f);rewind(f);
    if(length<240||length>2*1024*1024){fclose(f);return false;}
    std::vector<unsigned char> bytes((size_t)length);
    bool ok=fread(bytes.data(),1,bytes.size(),f)==bytes.size();fclose(f);
    if(!ok||memcmp(bytes.data(),"NSPRV015",8))return false;
    NsRead r={bytes,8,true};NsPack p;
    if(r.u32()!=1||r.u32()!=100||r.u32()!=78||r.u32()!=532||r.u32()!=33||r.u32()!=31||r.u32()!=54||r.u32()!=40)return false;
    memcpy(p.palette,&bytes[r.at],sizeof p.palette);r.at+=sizeof p.palette;
    memcpy(p.paint,&bytes[r.at],sizeof p.paint);r.at+=sizeof p.paint;
    if(p.palette[0][3]!=0||p.paint[0]!=0)return false;
    for(int i=1;i<33;i++)if(p.palette[i][3]!=255||p.paint[i]>16)return false;
    for(int i=0;i<31;i++) {
        NsAction a;a.count=r.u32();a.facings=r.u32();a.x0=r.u32();a.y0=r.u32();a.x1=r.u32();a.y1=r.u32();
        unsigned n=r.u32(),reserved=r.u32();
        if(!r.good||!a.count||a.count>32||(a.facings!=1&&a.facings!=2&&a.facings!=8)||n!=a.count*a.facings||reserved||a.x1>100||a.y1>78||a.x0>=a.x1||a.y0>=a.y1)return false;
        for(unsigned k=0;k<n;k++){unsigned frame=r.u16();if(!r.good||frame>=532)return false;a.frames.push_back((unsigned short)frame);}
        p.actions.push_back(a);
    }
    unsigned raw=r.u32(),compressed=r.u32(),crc=r.u32();
    if(!r.good||raw!=532*100*78||r.at+compressed!=bytes.size())return false;
    p.pixels.resize(raw);uLongf size=raw;
    if(uncompress(p.pixels.data(),&size,&bytes[r.at],compressed)!=Z_OK||size!=raw||crc32(0,p.pixels.data(),raw)!=crc)return false;
    for(size_t k=0;k<p.pixels.size();k++)if(p.pixels[k]>=33)return false;
    *result=std::move(p);return true;
}
static int ns_frame(const NsAction& a,int facing,int stage,bool terminal)
{
    if(a.frames.empty())return -1;
    if(facing<0)facing=0;if((unsigned)facing>=a.facings)facing=(int)a.facings-1;
    if(stage<0)stage=0;
    stage=terminal?(stage>=(int)a.count?(int)a.count-1:stage):stage%(int)a.count;
    return a.frames[(size_t)facing*a.count+stage];
}
#endif
