/* CPU coverage for the production Desert adaptation, including optional actual
   DECALEN3 sheets. It must mix source relief over sand without painting new pixels. */
#include "../game/decal_desert_blend.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

static void check_bytes(const std::vector<unsigned char>& before,const std::vector<unsigned char>& after)
{
    assert(before.size()==after.size());
    size_t kept=0,partial=0;
    for(size_t p=0;p<before.size()/4;p++) {
        for(int c=0;c<3;c++)assert(before[p*4+c]==after[p*4+c]);
        const unsigned a=before[p*4+3],b=after[p*4+3];
        assert(b<=a); if(!a)assert(!b);
        if(b)kept++;if(b&&b<153)partial++;
    }
    assert(kept>0&&partial>0);
}
static unsigned u32(FILE* f) { unsigned n=0;assert(fread(&n,4,1,f)==1);return n; }
static std::vector<unsigned char> sheet(FILE* f,size_t bytes)
{
    const unsigned size=u32(f);std::vector<unsigned char> z(size),out(bytes);
    assert(size&&fread(z.data(),1,size,f)==size);uLongf n=bytes;
    assert(uncompress(out.data(),&n,z.data(),size)==Z_OK&&n==bytes);return out;
}
static void actual_pack(const char* path)
{
    FILE* f=fopen(path,"rb");assert(f);char magic[8];assert(fread(magic,1,8,f)==8);
    assert(!memcmp(magic,"DECALEN3",8));unsigned hd[9];assert(fread(hd,4,9,f)==9);
    assert(hd[0]==3&&hd[1]==2);
    for(unsigned theater=0;theater<hd[1];theater++) {
        char name[13]={0};assert(fread(name,1,12,f)==12);const unsigned id=u32(f);
        unsigned wh[4],nf[12],layers[12];assert(fread(wh,4,4,f)==4);
        assert(fread(nf,4,12,f)==12&&fread(layers,4,12,f)==12);
        auto body=sheet(f,(size_t)wh[0]*wh[1]*4),soot=sheet(f,(size_t)wh[2]*wh[3]*3);
        const auto bodyBefore=body,sootBefore=soot;
        decal_desert_blend(id,body.data(),wh[0],wh[1],hd[2],hd[3],soot.data(),soot.size());
        if(id!=0) { assert(body==bodyBefore&&soot==sootBefore); }
        else {
            check_bytes(bodyBefore,body);
            size_t changed=0;
            for(size_t i=0;i<soot.size();i++) {
                assert(soot[i]>=sootBefore[i]);assert(soot[i]>=83&&soot[i]<=128);
                if(sootBefore[i]==128)assert(soot[i]==128);
                changed+=sootBefore[i]!=soot[i];
            }
            assert(changed>0);
            printf("PASS: actual %s RGB exact, alpha support contracts, soot density bounded; %zu soot samples changed\n",name,changed);
        }
    }
    assert(fgetc(f)==EOF);fclose(f);
}
int main(int argc,char** argv)
{
    const int n=144,gutter=8,w=n+2*gutter;
    std::vector<unsigned char> body((size_t)w*w*4,0),soot={0,22,64,100,127,128};
    for(int y=16;y<n-16;y++)for(int x=16;x<n-16;x++) {
        const size_t k=((size_t)(y+gutter)*w+x+gutter)*4;
        body[k]=(x*7+y)%255;body[k+1]=(x+y*3)%255;body[k+2]=(x*2+y*5)%255;body[k+3]=255;
    }
    const auto before=body,sootBefore=soot;
    auto other=body,otherSoot=soot;
    decal_desert_blend(2,other.data(),w,w,n,gutter,otherSoot.data(),otherSoot.size());
    assert(other==before&&otherSoot==sootBefore);
    decal_desert_blend(0,body.data(),w,w,n,gutter,soot.data(),soot.size());
    check_bytes(before,body);
    const size_t center=((size_t)(72+gutter)*w+72+gutter)*4+3;
    const size_t edge=((size_t)(20+gutter)*w+72+gutter)*4+3;
    assert(body[center]==153&&body[edge]>0&&body[edge]<body[center]/10);
    assert(soot.front()==83&&soot.back()==128);
    for(size_t i=1;i<soot.size();i++)assert(soot[i]>=soot[i-1]);
    /* At the deepest bed, the native sand still contributes 40% of its own
       texture contrast after soot. An opaque replacement would fail this. */
    const float a=(float)body[center]/255.0f,multiply=2.0f*soot[1]/255.0f;
    const float dark=60.0f*multiply*(1-a)+30.0f*a;
    const float light=100.0f*multiply*(1-a)+30.0f*a;
    assert(light-dark>10.0f);
    puts("PASS: Desert source RGB/footprint, inward rim falloff, retained centre relief/ground grain, neutral soot and other-theater isolation");
    if(argc==2)actual_pack(argv[1]);else assert(argc==1);
}
