#ifndef OPENCNC3D_WATER_BED_MATERIAL_H
#define OPENCNC3D_WATER_BED_MATERIAL_H
/* Reuse detail from the current map's actual cliff albedo. The saved ground
   matte and land alpha select a solid mineral patch; RGB never classifies rock.
   Only luminance detail tiles vertically. The nearest cliff retains its own
   colour, without repeating a whole painted cliff silhouette below the water. */
static void water_bed_material_make(const std::vector<unsigned char>& atlas,
                                    const PackTex& sheet,const std::vector<PackCell>& cells)
{
    g_waterBedPatternTex=0;
    if(!water_src_ra() || atlas.empty() || g_terrainGrassMasks.coverage.empty())return;
    const auto& masks=g_terrainGrassMasks.coverage;
    int chosen=-1,bx=0,bz=0;float best=-1;
    for(size_t i=0;i<cells.size() && i<g_waterRa.bedMasks.size();i++) {
        const int slot=g_waterRa.bedMasks[i];if(slot<=0 || (size_t)(slot+1)*576>masks.size())continue;
        const auto& c=cells[i];
        // A six-native-texel square is one quarter of a cell, large enough to
        // contain source cracks while excluding painted shore water and snow.
        for(int z=1;z<=17;z+=2)for(int x=1;x<=17;x+=2) {
            bool rock=true;
            for(int dz=0;dz<6&&rock;dz++)for(int dx=0;dx<6;dx++) {
                if(masks[((size_t)slot*24+z+dz)*24+x+dx]>8){rock=false;break;}
                const int tx=std::max(0,std::min(sheet.w-1,(int)((c.u0+(c.u1-c.u0)*(x+dx+.5f)/24)*sheet.uw)));
                const int tz=std::max(0,std::min(sheet.h-1,(int)((c.v0+(c.v1-c.v0)*(z+dz+.5f)/24)*sheet.uh)));
                if(atlas[((size_t)tz*sheet.w+tx)*4+3]<250){rock=false;break;}
            }
            if(!rock)continue;
            float sum=0,sq=0;
            for(int dz=0;dz<6;dz++)for(int dx=0;dx<6;dx++) {
                const int tx=(int)((c.u0+(c.u1-c.u0)*(x+dx+.5f)/24)*sheet.uw),tz=(int)((c.v0+(c.v1-c.v0)*(z+dz+.5f)/24)*sheet.uh);
                const auto* p=&atlas[((size_t)tz*sheet.w+tx)*4];const float l=(.299f*p[0]+.587f*p[1]+.114f*p[2])/255;
                sum+=l;sq+=l*l;
            }
            const float variance=sq/36-(sum/36)*(sum/36);
            const float score=1.0f/(.001f+fabsf(variance-.004f));
            if(score>best){best=score;chosen=(int)i;bx=x;bz=z;}
        }
    }
    if(chosen<0)return;
    const int N=64;std::vector<float> detail(N*N);double mean=0;
    const auto& c=cells[chosen];
    for(int z=0;z<N;z++)for(int x=0;x<N;x++) {
        const float fx=(bx+(x+.5f)*6/N)/24,fz=(bz+(z+.5f)*6/N)/24;
        const int tx=(int)((c.u0+(c.u1-c.u0)*fx)*sheet.uw),tz=(int)((c.v0+(c.v1-c.v0)*fz)*sheet.uh);
        const auto* p=&atlas[((size_t)tz*sheet.w+tx)*4];detail[z*N+x]=(.299f*p[0]+.587f*p[1]+.114f*p[2])/255;mean+=detail[z*N+x];
    }
    mean/=N*N;
    // Mirrored quarters give matching values and tangents at all repeat edges.
    // Store source luminance relative to its mean, retaining the real crack
    // contrast rather than flattening the newly exposed vertical cliff sides.
    std::vector<unsigned char> pixels(N*N);
    for(int z=0;z<N;z++)for(int x=0;x<N;x++) {
        const int sx=x<N/2?x*2:(N-1-x)*2,sz=z<N/2?z*2:(N-1-z)*2;
        const float l=.5f*detail[sz*N+sx]/std::max(.02f,(float)mean);
        pixels[z*N+x]=(unsigned char)(std::max(.125f,std::min(1.0f,l))*255+.5f);
    }
    glGenTextures(1,&g_waterBedPatternTex);glBindTexture(GL_TEXTURE_2D,g_waterBedPatternTex);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D,0,GL_LUMINANCE,N,N,0,GL_LUMINANCE,GL_UNSIGNED_BYTE,pixels.data());glBindTexture(GL_TEXTURE_2D,0);
    fprintf(stderr,"WATER|bed-material|source-cell=%d,%d|native-patch=%d,%d,6,6|explicit-rock-matte|detail=64x64|colour=local-cliff\n",c.x,c.y,bx,bz);
}
#endif
