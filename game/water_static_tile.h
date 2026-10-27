#ifndef CNC3D_WATER_STATIC_TILE_H
#define CNC3D_WATER_STATIC_TILE_H
/* The Water checkbox disables surface effects, not material continuity. Its
   Enhanced fallback uses one bank's open-water painting in every wet icon.
   This is CPU atlas work at load time; no extra draw, geometry or frame cost. */
static std::vector<unsigned char> water_static_tile(const std::vector<unsigned char>& rgb,int side,int cell)
{
    if(side<2||cell<4||rgb.size()!=(size_t)side*side*3)return {};
    std::vector<unsigned char> tile((size_t)cell*cell*3);
    for(int y=0;y<cell;y++)for(int x=0;x<cell;x++) {
        const int sx=std::min(side-1,(int)((x+.5f)*side/cell)),sy=std::min(side-1,(int)((y+.5f)*side/cell));
        memcpy(&tile[((size_t)y*cell+x)*3],&rgb[((size_t)sy*side+sx)*3],3);
    }
    // Opposite edge bands share RGB. A C2 weight retains source grain inside
    // the tile; every placement has the same scale and an identical shared edge.
    const int rim=std::max(2,cell/12);
    for(int y=0;y<cell;y++)for(int x=0;x<rim;x++)for(int c=0;c<3;c++) {
        const size_t a=((size_t)y*cell+x)*3+c,b=((size_t)y*cell+cell-1-x)*3+c;
        const float t=(float)x/(rim-1),k=1-t*t*t*(t*(t*6-15)+10),mean=.5f*(tile[a]+tile[b]);
        const unsigned char av=tile[a],bv=tile[b];tile[a]=(unsigned char)(av*(1-k)+mean*k+.5f);tile[b]=(unsigned char)(bv*(1-k)+mean*k+.5f);
    }
    for(int y=0;y<rim;y++)for(int x=0;x<cell;x++)for(int c=0;c<3;c++) {
        const size_t a=((size_t)y*cell+x)*3+c,b=((size_t)(cell-1-y)*cell+x)*3+c;
        const float t=(float)y/(rim-1),k=1-t*t*t*(t*(t*6-15)+10),mean=.5f*(tile[a]+tile[b]);
        const unsigned char av=tile[a],bv=tile[b];tile[a]=(unsigned char)(av*(1-k)+mean*k+.5f);tile[b]=(unsigned char)(bv*(1-k)+mean*k+.5f);
    }
    return tile;
}
static void water_static_composite(unsigned char* rgba,int cell,size_t row,
        const std::vector<unsigned char>& tile,const unsigned char* land,size_t landRow,int landStep)
{
    if(tile.size()!=(size_t)cell*cell*3)return;
    for(int y=0;y<cell;y++)for(int x=0;x<cell;x++) {
        unsigned char* p=rgba+(size_t)y*row+x*4;const int a=land[(size_t)y*landRow+x*landStep];
        for(int c=0;c<3;c++)p[c]=(unsigned char)((p[c]*a+tile[((size_t)y*cell+x)*3+c]*(255-a)+127)/255);
    }
}
#endif
