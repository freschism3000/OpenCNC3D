#ifndef OPENCNC3D_DECAL_DESERT_BLEND_H
#define OPENCNC3D_DECAL_DESERT_BLEND_H
#include <algorithm>
#include <math.h>
#include <stddef.h>
#include <vector>

/* Desert's painted crater bed used to replace the sand completely, over an
   additional soot multiply of about 0.2. Keep both original shapes and every RGB
   sample, while sharing the visible material with the actual ground beneath it.
   These are coverage/density controls, not a colour lift painted onto the crater. */
static void decal_desert_body_frame(unsigned char* rgba,int stride,int size)
{
    if(!rgba || size<=0 || stride<size)return;
    const float maxDistance=(float)(size*2), diagonal=1.41421356237f;
    std::vector<float> distance((size_t)size*size,maxDistance);
    for(int y=0;y<size;y++)for(int x=0;x<size;x++)
        if(rgba[((size_t)y*stride+x)*4+3]<=10)distance[(size_t)y*size+x]=0.0f;
    /* Distance inside the saved support only. No RGB filtering, outward blur,
       neighbour-frame sampling or new coverage is introduced. */
    for(int y=0;y<size;y++)for(int x=0;x<size;x++) {
        float& d=distance[(size_t)y*size+x];
        d=std::min(d,(float)std::min(x+1,y+1));
        if(x)d=std::min(d,distance[(size_t)y*size+x-1]+1.0f);
        if(y)d=std::min(d,distance[(size_t)(y-1)*size+x]+1.0f);
        if(x&&y)d=std::min(d,distance[(size_t)(y-1)*size+x-1]+diagonal);
        if(y&&x+1<size)d=std::min(d,distance[(size_t)(y-1)*size+x+1]+diagonal);
    }
    for(int y=size-1;y>=0;y--)for(int x=size-1;x>=0;x--) {
        float& d=distance[(size_t)y*size+x];
        d=std::min(d,(float)std::min(size-x,size-y));
        if(x+1<size)d=std::min(d,distance[(size_t)y*size+x+1]+1.0f);
        if(y+1<size)d=std::min(d,distance[(size_t)(y+1)*size+x]+1.0f);
        if(x+1<size&&y+1<size)d=std::min(d,distance[(size_t)(y+1)*size+x+1]+diagonal);
        if(x&&y+1<size)d=std::min(d,distance[(size_t)(y+1)*size+x-1]+diagonal);
    }
    const float width=30.0f*(float)size/144.0f;
    for(int y=0;y<size;y++)for(int x=0;x<size;x++) {
        unsigned char& alpha=rgba[((size_t)y*stride+x)*4+3];
        float t=std::min(1.0f,distance[(size_t)y*size+x]/width);
        t=t*t*(3.0f-2.0f*t);
        alpha=(unsigned char)((float)alpha*0.60f*t+0.5f);
    }
}
static void decal_desert_blend(unsigned theater,unsigned char* body,int w,int h,
                               int cell,int gutter,unsigned char* soot,size_t sootBytes)
{
    if(theater!=0)return; /* engine Desert0; every other theater is byte-identical */
    if(body&&w>0&&h>0&&cell>0&&gutter>=0) {
        const int slot=cell+2*gutter;
        for(int y=0;y+slot<=h;y+=slot)for(int x=0;x+slot<=w;x+=slot)
            decal_desert_body_frame(body+((size_t)(y+gutter)*w+x+gutter)*4,w,cell);
    }
    if(soot)for(size_t i=0;i<sootBytes;i++)
        /* 128 is the existing doubled-multiply neutral. A 35% density retains
           source-led char/rim modulation and the sand's grain through its centre. */
        soot[i]=(unsigned char)(128.0f+((float)soot[i]-128.0f)*0.35f+0.5f);
}
#endif
