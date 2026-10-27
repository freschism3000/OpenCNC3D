#ifndef OPENCNC3D_RA3D_CLIFF_MATERIAL_H
#define OPENCNC3D_RA3D_CLIFF_MATERIAL_H
/* Remove the static surf drawn inside cliff albedo. Native coast coverage is
   immutable; only RGB in a narrow authored-water neighbourhood is repaired.
   The caller assembles the whole template before invoking this operation. */
static size_t ra3d_cliff_dry_material(std::vector<unsigned char>& rgb,
        const std::vector<unsigned char>& wet,int w,int h,int scale,
        std::vector<unsigned char>* repaired=NULL)
{
    const size_t size=(size_t)w*h;if(rgb.size()!=size*3||wet.size()!=size)return 0;
    bool hasWater=false;for(unsigned char v:wet)hasWater|=v!=0;if(!hasWater)return 0;
    std::vector<float> distance(size);std::vector<int> nearest(size);
    water_edt_2d(wet.data(),distance.data(),nearest.data(),w,h);
    std::vector<unsigned char> mask(size,0),white(size,0),land(size,0);
    const float radius=(float)(4*scale);
    std::vector<int> queue;
    for(size_t i=0;i<size;i++) {
        if(distance[i]>radius)continue;
        const int r=rgb[i*3],g=rgb[i*3+1],b=rgb[i*3+2];
        if(b-r>=7 && b-g>=-7){mask[i]=1;queue.push_back((int)i);}
        white[i]=r+g+b>=510 && std::max(r,std::max(g,b))-std::min(r,std::min(g,b))<75 && b-r>=-12;
    }
    // Foam is a thin fringe attached to blue paint, never a white Snow region.
    size_t first=0,last=queue.size();
    for(int ring=0;ring<std::max(2,scale);ring++) {
        for(size_t q=first;q<last;q++) {
            const int i=queue[q],x=i%w,y=i/w,nb[4]={x?i-1:-1,x+1<w?i+1:-1,y?i-w:-1,y+1<h?i+w:-1};
            for(int j:nb)if(j>=0&&!mask[j]&&white[j]){mask[j]=1;queue.push_back(j);}
        }
        first=last;last=queue.size();
    }
    if(queue.empty())return 0;
    bool hasLand=false;for(size_t i=0;i<size;i++){land[i]=!wet[i]&&!mask[i];hasLand|=land[i]!=0;}if(!hasLand)return 0;
    water_edt_2d(land.data(),distance.data(),nearest.data(),w,h);
    std::vector<unsigned char> fill(rgb);
    for(int i:queue)if(nearest[i]>=0)memcpy(&fill[(size_t)i*3],&rgb[(size_t)nearest[i]*3],3);
    // Harmonic continuation removes nearest-donor streaks. Fixed boundary values
    // come only from actual dry material; painted water cannot bleed back in.
    for(int pass=0;pass<24;pass++)for(size_t q=0;q<queue.size();q++) {
        const int i=queue[pass&1?queue.size()-1-q:q],x=i%w,y=i/w,nb[4]={x?i-1:-1,x+1<w?i+1:-1,y?i-w:-1,y+1<h?i+w:-1};
        int sum[3]={0,0,0},n=0;
        for(int j:nb)if(j>=0&&(land[j]||mask[j])){for(int c=0;c<3;c++)sum[c]+=fill[(size_t)j*3+c];n++;}
        if(n)for(int c=0;c<3;c++)fill[(size_t)i*3+c]=(unsigned char)((sum[c]+n/2)/n);
    }
    for(int i:queue)memcpy(&rgb[(size_t)i*3],&fill[(size_t)i*3],3);
    if(repaired)*repaired=mask;
    return queue.size();
}
/* A join must not paint the removed surf back over the neighbouring cell.
   Recolour only surf-coloured strip pixels whose corresponding source edge was
   repaired. The authored blend alpha and every other strip pixel stay exact. */
static size_t ra3d_cliff_strip_material(unsigned char* rgba,int cell,int margin,int side,
        const std::vector<unsigned char>& edge,const std::vector<unsigned char>& mask)
{
    size_t changed=0;
    for(int y=0;y<(side?margin:cell);y++)for(int x=0;x<(side?cell:margin);x++) {
        const int j=side?x:y;if(!mask[j])continue;
        unsigned char* q=rgba+((size_t)y*(side?cell:margin)+x)*4;
        const int r=q[0],g=q[1],b=q[2];
        const bool blue=b-r>=7&&b-g>=-7;
        const bool white=r+g+b>=510&&std::max(r,std::max(g,b))-std::min(r,std::min(g,b))<75&&b-r>=-12;
        if(blue||white){memcpy(q,&edge[(size_t)j*3],3);changed++;}
    }
    return changed;
}
#endif
