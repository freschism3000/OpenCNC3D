#ifndef OPENCNC3D_CLEAR_BLEND_FIELD_H
#define OPENCNC3D_CLEAR_BLEND_FIELD_H

/* Presentation-only, deterministic continuous ground weights. No simulation RNG,
   camera, shroud, unit or building state enters this field. Four samples per cell
   describe the shapes; they do not replace the high-resolution material images. */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

static unsigned clear_blend_hash(unsigned x)
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu;
    return x ^ (x >> 16);
}

static unsigned clear_blend_seed(const char* name)
{
    unsigned h = 2166136261u;
    if (name) for (; *name; ++name) h = (h ^ (unsigned char)*name) * 16777619u;
    return h;
}

static float cb_random(unsigned key, unsigned salt)
{ return (clear_blend_hash(key ^ (salt * 0x9e3779b9u)) & 0xffffffu) / 16777216.0f; }

static float cb_smooth(float lo, float hi, float x)
{
    float t = std::max(0.0f, std::min(1.0f, (x - lo) / (hi - lo)));
    return t * t * (3.0f - 2.0f * t);
}

static float cb_noise(float x, float z, unsigned seed)
{
    const int ix = (int)std::floor(x), iz = (int)std::floor(z);
    const float fx = cb_smooth(0, 1, x - ix), fz = cb_smooth(0, 1, z - iz);
    float v[4];
    for (int k = 0; k < 4; ++k)
        v[k] = cb_random(seed ^ (unsigned)(ix + (k & 1)) * 0x85ebca6bu
                              ^ (unsigned)(iz + (k >> 1)) * 0xc2b2ae35u, 17);
    return (v[0] + (v[1] - v[0]) * fx) * (1 - fz)
         + (v[2] + (v[3] - v[2]) * fx) * fz;
}

struct ClearBlendField {
    int w = 0, h = 0, samples = 4;
    unsigned signature = 0;
    int clumps[2] = {0, 0};
    std::vector<unsigned char> rgba; /* R CLEAR2, G CLEAR3, B overlay coverage, A 255 */
};

static unsigned cb_mask_signature(int w, int h, const std::vector<unsigned char>& mask,
                                  unsigned seed)
{
    unsigned hash = clear_blend_hash(seed ^ (unsigned)w ^ ((unsigned)h << 16));
    for (size_t i = 0; i < mask.size(); ++i) hash = (hash ^ mask[i]) * 16777619u;
    return hash;
}

static bool cb_is_clear(int x, int z, int w, int h, const std::vector<unsigned char>& mask)
{ return x >= 0 && z >= 0 && x < w && z < h && mask[(size_t)z * w + x] != 0; }

/* Distance to the actual non-clear CELL RECTANGLE, not its centre. The collar leaves
   the existing painted grass beside roads/cliffs intact until those assets change. */
static float cb_edge_distance(float x, float z, int w, int h,
                              const std::vector<unsigned char>& mask)
{
    const int ix = (int)std::floor(x), iz = (int)std::floor(z);
    if (!cb_is_clear(ix, iz, w, h, mask)) return 0;
    float d2 = 4.0f;
    for (int dz = -2; dz <= 2; ++dz) for (int dx = -2; dx <= 2; ++dx) {
        const int cx = ix + dx, cz = iz + dz;
        if (cb_is_clear(cx, cz, w, h, mask)) continue;
        const float qx = std::max((float)cx - x, std::max(x - (cx + 1.0f), 0.0f));
        const float qz = std::max((float)cz - z, std::max(z - (cz + 1.0f), 0.0f));
        d2 = std::min(d2, qx * qx + qz * qz);
    }
    return std::sqrt(d2);
}


/* Non-periodic, rotated octaves with independent phases. The result is a single
   coverage landscape, rather than a collection of spaced grass/dirt stamps. */
static float cb_landscape(float x, float z, unsigned seed)
{
    float sum=0, norm=0, amp=1;
    for(int octave=0;octave<5;++octave) {
        sum += amp * cb_noise(x+13.71f*octave,z-8.39f*octave,
                               clear_blend_hash(seed+(unsigned)octave*0x9e3779b9u));
        norm += amp; amp *= 0.48f;
        const float qx=x*1.7073f-z*1.1031f;
        z=x*1.1031f+z*1.7073f; x=qx;
    }
    return sum/norm;
}

static ClearBlendField cb_make_field(int w, int h,
                                     const std::vector<unsigned char>& mask, unsigned seed)
{
    ClearBlendField out;
    if(w<=0 || h<=0 || w>256 || h>256 || mask.size()!=(size_t)w*h) return out;
    out.w=w*out.samples;out.h=h*out.samples;
    out.signature=cb_mask_signature(w,h,mask,seed);
    out.rgba.assign((size_t)out.w*out.h*4,0);
    std::vector<float> values((size_t)out.w*out.h,0), eligibleValues;
    eligibleValues.reserve(values.size());
    for(int pz=0;pz<out.h;++pz) for(int px=0;px<out.w;++px) {
        const float x=(px+0.5f)/out.samples,z=(pz+0.5f)/out.samples;
        if(!cb_is_clear((int)x,(int)z,w,h,mask)) continue;
        const float wx=5.2f*(cb_landscape(x/21.3f,z/21.3f,seed^0x4528u)-0.5f);
        const float wz=5.2f*(cb_landscape(x/18.7f+19.3f,z/18.7f-7.1f,seed^0x9317u)-0.5f);
        const float u=x+wx,v=z+wz;
        const float large=cb_landscape((u*0.857f-v*0.515f)/10.7f,
                                      (u*0.515f+v*0.857f)/10.7f,seed^0x3275u);
        const float medium=cb_landscape((u*0.61f+v*0.7924f)/4.13f,
                                       (-u*0.7924f+v*0.61f)/4.13f,seed^0x6819u);
        const float small=cb_noise(u/1.37f+37.1f,v/1.37f-13.7f,seed^0x7139u);
        const float f=0.70f*large+0.23f*medium+0.07f*small;
        values[(size_t)pz*out.w+px]=f;
        eligibleValues.push_back(f);
    }
    if(eligibleValues.empty()) return out;
    std::sort(eligibleValues.begin(),eligibleValues.end());
    const float lo=eligibleValues[(size_t)((eligibleValues.size()-1)*0.23)];
    const float hi=eligibleValues[(size_t)((eligibleValues.size()-1)*0.55)];
    const float range=std::max(0.045f,hi-lo);
    for(int z=0;z<out.h;++z) for(int x=0;x<out.w;++x) {
        unsigned char* q=&out.rgba[((size_t)z*out.w+x)*4];q[3]=255;
        const float wx=(x+0.5f)/out.samples,wz=(z+0.5f)/out.samples;
        if(!cb_is_clear((int)wx,(int)wz,w,h,mask)) continue;
        const float d=cb_edge_distance(wx,wz,w,h,mask);
        const float f=values[(size_t)z*out.w+x];
        const float transition=range*(0.16f+0.19f*cb_noise(wx/3.71f,wz/3.71f,seed^0x8813u));
        const float dirt=1-cb_smooth(lo-transition,lo+transition,f);
        const float grass=cb_smooth(hi-transition,hi+transition,f);
        const float room=cb_smooth(0.5f,1.05f,d);
        const float w2=dirt*room,w3=std::max(0.0f,1-dirt-grass)*room;
        q[0]=(unsigned char)std::floor(w2*255+0.5f);
        q[1]=(unsigned char)std::floor(w3*255+0.5f);
        if(q[0]+q[1]>255)q[1]=255-q[0];
        q[2]=(unsigned char)std::floor(cb_smooth(0.08f,0.72f,d)*255+0.5f);
    }
    /* Legacy log counters now count connected strong regions, not placed blobs. */
    for(int material=0;material<2;++material) {
        std::vector<unsigned char> seen(values.size(),0);
        std::vector<size_t> stack;
        for(size_t i=0;i<values.size();++i) {
            if(seen[i] || out.rgba[i*4+material]<180 || !out.rgba[i*4+2])continue;
            ++out.clumps[material];seen[i]=1;stack.clear();stack.push_back(i);
            while(!stack.empty()) {
                const size_t at=stack.back();stack.pop_back();
                const int px=(int)(at%out.w),pz=(int)(at/out.w);
                const int nx[4]={px-1,px+1,px,px},nz[4]={pz,pz,pz-1,pz+1};
                for(int k=0;k<4;++k)if(nx[k]>=0&&nz[k]>=0&&nx[k]<out.w&&nz[k]<out.h) {
                    const size_t next=(size_t)nz[k]*out.w+nx[k];
                    if(!seen[next]&&out.rgba[next*4+material]>=180&&out.rgba[next*4+2]) {
                        seen[next]=1;stack.push_back(next);
                    }
                }
            }
        }
    }
    return out;
}
/* Desert keeps broad quiet sand and sparse, unequal mineral coverage. This changes
   presentation weights only: authored feature coverage, density and map cells stay
   exact. Large independent fields vary visibility without stamping spots or using
   the simulation RNG. Temperate retains its existing field byte for byte. */
static void cb_desert_material_weights(ClearBlendField& field,unsigned seed)
{
    for(int z=0;z<field.h;++z)for(int x=0;x<field.w;++x) {
        unsigned char* q=&field.rgba[((size_t)z*field.w+x)*4];
        if(!q[2])continue;
        const float wx=(x+0.5f)/field.samples,wz=(z+0.5f)/field.samples;
        const float mineral=cb_landscape(wx/9.1f+11.7f,wz/9.1f-6.3f,seed^0xd358u);
        const float dust=cb_landscape(wx/13.7f-21.3f,wz/13.7f+9.7f,seed^0xa192u);
        const float strength2=0.06f+0.84f*cb_smooth(0.35f,0.70f,mineral);
        const float strength3=0.04f+0.43f*cb_smooth(0.30f,0.70f,dust);
        q[0]=(unsigned char)std::floor(q[0]*strength2+0.5f);
        q[1]=(unsigned char)std::floor(q[1]*strength3+0.5f);
    }
}
#endif
