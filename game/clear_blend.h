#ifndef OPENCNC3D_CLEAR_BLEND_H
#define OPENCNC3D_CLEAR_BLEND_H

#include "clear_blend_field.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <map>

#ifndef GL_CURRENT_PROGRAM
#define GL_CURRENT_PROGRAM 0x8B8D
#endif
#ifndef GL_MAX_TEXTURE_IMAGE_UNITS
#define GL_MAX_TEXTURE_IMAGE_UNITS 0x8872
#endif
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif

/* Enhanced biome clear ground. It layers three independent material families
   over original clear cells and explicitly outlined feature ground. The normal terrain cache, map data, collision, ground
   normals and depth remain authoritative; the same draw also runs in reflections.

   The installed clearblend_<THEATER>.pack is selected by default. OPENCNC3D_CLEARBLEND
   overrides its path; off/none explicitly disable it. Missing/invalid packs or unavailable
   shaders leave the existing renderer unchanged. The file is:
   8 bytes "C3DCLR\0\0", LE u32 version, cell, width (4 or 8 cells), count, seed.
   v1: count=3; v2: count=15; v3: three extra LE u32 family counts followed by
   their family-major RGB fields. Original family albedos and selected additional layouts
   retain their exact pixels; randomized source-preserving patches break up their repeats.
   Every field is width*width RGB8. No alpha or mask
   is inferred from colour. Coverage comes solely from original map topology. */

struct ClearBlendCell {
    int x, z;
    int grassMask;                    /* zero is full clear ground */
    float groundEdges[4];              /* W/E/N/S neighbouring ground-mask slot + 1; zero disables */
    float height[4], light[4];           /* NW NE SE SW */
    unsigned char tint[4][3];
};

struct ClearBlendState {
    bool tried = false, ok = false, fieldFailed = false;
    std::string theater;
    GLuint material[3] = {0, 0, 0}, weights = 0, program = 0;
    unsigned seed = 0, fieldSignature = 0;
    int cell = 144, width = 0, gridW = 0, gridH = 0;
    int variants = 1, layouts[3] = {1,1,1};
    float saturation = 0.70f;
    float means[3][4] = {{0,0,0,0},{0,0,0,0},{0,0,0,0}};
    int columns = 1, rows = 1, pitch = 0, atlasW = 0, atlasH = 0;
    /* Immutable positions are kept by map cell, independently of the visible list.
       Only first use or an actually changed pad/corner height rewrites a cell. */
    std::vector<float> geometry, colours, heights, maskUV, groundEdges;
    std::vector<unsigned char> known, control;
};
static ClearBlendState g_clearBlend;
static void terrain_surface_clear_draw(const std::vector<ClearBlendCell>& cells);

/* Search once for the normal installed asset. No launch-only environment is needed.
   The Dock launcher starts in playable; the extra roots support repo and RA runs. */
static const char* clear_blend_path(void)
{
    const char* overridePath = getenv("OPENCNC3D_CLEARBLEND");
    if (overridePath && *overridePath)
        return (!strcmp(overridePath,"off") || !strcmp(overridePath,"none")) ? NULL : overridePath;
    const std::string theater = g_raModule && !strcmp(g_pack.theater,"RASNOW") ? "RA_SNOW"
        : !strcmp(g_pack.theater,"DESERT") ? "DESERT" : !strcmp(g_pack.theater,"SAND") ? "SAND" : "TEMPERAT";
    static std::map<std::string,std::string> paths;
    if (!paths.count(theater)) {
        std::string& installed=paths[theater];
        const std::string filename="clearblend_"+theater+".pack";
        const char* roots[] = {"", "game/", "../game/", "ra3d/", "../"};
        for (size_t i=0;i<sizeof roots/sizeof roots[0];++i) {
            const std::string path=std::string(roots[i])+filename;
            FILE* f=fopen(path.c_str(),"rb");
            if(f){fclose(f);installed=path;break;}
        }
        if(installed.empty()) {
            char* base=SDL_GetBasePath();
            if(base) {
                const std::string root=base;SDL_free(base);
                const char* relative[]={"","../"};
                for(size_t i=0;i<2;++i) {
                    const std::string path=root+relative[i]+filename;FILE* f=fopen(path.c_str(),"rb");
                    if(f){fclose(f);installed=path;break;}
                }
            }
        }
    }
    const std::string& installed=paths[theater];
    return installed.empty()?NULL:installed.c_str();
}
static bool clear_blend_requested(void) { return clear_blend_path()!=NULL; }

static void clear_blend_free(void)
{
    for (int k = 0; k < 3; ++k)
        if (g_clearBlend.material[k]) glDeleteTextures(1, &g_clearBlend.material[k]);
    if (g_clearBlend.weights) glDeleteTextures(1, &g_clearBlend.weights);
    if (g_clearBlend.program && fx_glDeleteProgram) fx_glDeleteProgram(g_clearBlend.program);
    g_clearBlend = ClearBlendState();
}

static unsigned cb_u32(const unsigned char* p)
{ return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24); }

#include "terrain_grass_mask.h"

/* Each family has its own atlas. The wide periodic gutters are rebuilt independently
   at EVERY mip level, from that layout alone. Stop before a gutter becomes smaller
   than one texel: automatic whole-atlas mipmaps would bleed other layouts into it. */
static bool cb_upload_family(const unsigned char* source, int width, int variants,
                              int columns, int rows)
{
    const int gutter = 64;
    int last = 0;
    while (last < 8 && width % (2 << last) == 0 && gutter % (2 << last) == 0) ++last;
    std::vector<std::vector<unsigned char> > pixels((size_t)variants);
    for (int v = 0; v < variants; ++v)
        pixels[v].assign(source + (size_t)v * width * width * 3,
                         source + (size_t)(v + 1) * width * width * 3);
    int side = width;
    for (int level = 0; level <= last; ++level) {
        const int g = gutter >> level, pitch = side + 2 * g;
        const int aw = columns * pitch, ah = rows * pitch;
        std::vector<unsigned char> atlas((size_t)aw * ah * 3);
        for (int slot = 0; slot < columns * rows; ++slot) {
            const int v = slot < variants ? slot : 0;
            const int ox = (slot % columns) * pitch, oy = (slot / columns) * pitch;
            for (int y = 0; y < pitch; ++y) {
                const int sy = ((y - g) % side + side) % side;
                for (int x = 0; x < pitch; ++x) {
                    const int sx = ((x - g) % side + side) % side;
                    memcpy(&atlas[((size_t)(oy + y) * aw + ox + x) * 3],
                           &pixels[v][((size_t)sy * side + sx) * 3], 3);
                }
            }
        }
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGB, aw, ah, 0, GL_RGB,
                     GL_UNSIGNED_BYTE, &atlas[0]);
        GLint actualW = 0, actualH = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_WIDTH, &actualW);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_HEIGHT, &actualH);
        if (actualW != aw || actualH != ah) return false;
        if (level == last) break;
        const int n = side / 2;
        for (int v = 0; v < variants; ++v) {
            std::vector<unsigned char> next((size_t)n * n * 3);
            for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x)
                for (int c = 0; c < 3; ++c) {
                    const size_t a = ((size_t)(2 * y) * side + 2 * x) * 3 + c;
                    const unsigned sum = pixels[v][a] + pixels[v][a + 3]
                                       + pixels[v][a + side * 3] + pixels[v][a + side * 3 + 3];
                    next[((size_t)y * n + x) * 3 + c] = (unsigned char)((sum + 2) / 4);
                }
            pixels[v].swap(next);
        }
        side = n;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, last);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const char* extensions=(const char*)glGetString(GL_EXTENSIONS);
    if(extensions && strstr(extensions,"GL_EXT_texture_filter_anisotropic")) {
        GLfloat maximum=1;glGetFloatv(0x84FF,&maximum);
        glTexParameterf(GL_TEXTURE_2D,0x84FE,maximum<8?maximum:8);
    }
    return true;
}

static GLuint cb_program(void)
{
    const char* vs =
        "#version 120\n"
        "varying vec2 vWorld,vMaskUV,vCoastUV; varying vec4 vColour,vGroundEdges;\n"
        "varying float vRockRise;\n"
        "uniform int uSurface;\n"
        "void main(){vWorld=gl_Vertex.xz;vMaskUV=gl_MultiTexCoord0.xy;vCoastUV=gl_MultiTexCoord3.xy;vColour=uSurface==1?vec4(0.0,0.0,0.0,gl_Color.r):gl_Color;vGroundEdges=gl_MultiTexCoord1;"
        "vRockRise=uSurface==1?clamp((1.0-gl_MultiTexCoord2.x)/0.08,0.0,1.0):0.0;"
        "gl_Position=ftransform();gl_ClipVertex=gl_ModelViewMatrix*gl_Vertex;}\n";
    const char* fs = R"CBGLSL(#version 120
#extension GL_ARB_shader_texture_lod : require
uniform sampler2D uClear1,uClear2,uClear3,uWeights,uFeatureMask,uCoast;
uniform int uSurface;
uniform vec2 uGrid;
uniform vec4 uAtlas,uMean1,uMean2,uMean3,uLayouts,uMaskGeometry;
uniform float uMaskCell,uMaskColumns;
uniform float uTinted,uColumns,uSeed,uSaturation,uWorldScale,uSiteScale,uSolidFeatureGround,uGrassPalette;
varying float vRockRise;
varying vec2 vWorld,vMaskUV,vCoastUV;
varying vec4 vColour,vGroundEdges;

vec3 cbHash(vec2 id,float salt) {
    vec3 p=fract(vec3(id.x,id.y,id.x)*vec3(0.1031,0.1030,0.0973)
                  +vec3(salt*0.0317+uSeed*0.013));
    p+=dot(p,p.yxz+33.33);
    return fract((p.xxy+p.yzz)*p.zyx);
}
float cbNoise(vec2 p,float salt) {
    vec2 i=floor(p),f=fract(p);f=f*f*f*(f*(f*6.0-15.0)+10.0);
    return mix(mix(cbHash(i,salt).x,cbHash(i+vec2(1,0),salt).x,f.x),
               mix(cbHash(i+vec2(0,1),salt).x,cbHash(i+vec2(1,1),salt).x,f.x),f.y);
}
vec2 cbRotate(vec2 uv,float turn) {
    if(turn<0.5)return uv;
    if(turn<1.5)return vec2(-uv.y,uv.x);
    if(turn<2.5)return -uv;
    return vec2(uv.y,-uv.x);
}
vec3 cbPatch(sampler2D material,vec2 id,float salt,float count,vec3 mean,
              vec2 dx,vec2 dy) {
    vec3 h=cbHash(id,salt+41.3);
    float turn=floor(cbHash(id,salt+73.1).y*4.0),layout=floor(h.z*count);
    vec2 tile=vec2(mod(layout,uColumns),floor(layout/uColumns));
    vec2 uv=fract(cbRotate(vWorld*uWorldScale,turn)+h.xy);
    vec2 ratio=vec2(uAtlas.x)/uAtlas.zw;
    vec2 atlasUV=(tile*uAtlas.y+vec2(64.0)+uv*uAtlas.x)/uAtlas.zw;
    vec3 fine=texture2DGradARB(material,atlasUV,cbRotate(dx,turn)*ratio,
                                                  cbRotate(dy,turn)*ratio).rgb;
    // Keep the full authored albedo: grass blades, mineral colour and local contrast.
    return fine;
}
vec3 cbMaterial(sampler2D material,float salt,float count,vec3 mean,
                 vec2 dx,vec2 dy) {
    // Crop below the source's internal two-cell repeat. Large stochastic regions
    // still revealed multiple copies of the same source motif inside one region.
    // This changes site spacing, not source texel density or the family/clump field.
    float angle=salt*0.137;
    mat2 rot=mat2(cos(angle),sin(angle),-sin(angle),cos(angle));
    vec2 p=rot*vWorld/((0.47+fract(salt*0.719)*0.19)*uSiteScale)+vec2(salt*0.419,salt*0.137);
    p+=1.9*vec2(cbNoise(p*0.237+vec2(7.1,17.3),salt+2.0)-0.5,
                 cbNoise(p*0.193+vec2(31.7,5.3),salt+7.0)-0.5);
    vec2 base=floor(p);
    vec4 distances=vec4(1.0e8);
    vec2 a=base,b=base,c=base,d=base;
    // 5x5 search encloses the four nearest jittered sites even at cell boundaries.
    // The fourth site has zero weight, so changing the nearest set is continuous.
    for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x) {
        vec2 id=base+vec2(float(x),float(y));
        vec2 site=id+vec2(0.5)+(cbHash(id,salt).xy-vec2(0.5))*0.8;
        vec2 delta=p-site;float q=dot(delta,delta);
        if(q<distances.x) { distances=vec4(q,distances.xyz);d=c;c=b;b=a;a=id; }
        else if(q<distances.y) { distances=vec4(distances.x,q,distances.yz);d=c;c=b;b=id; }
        else if(q<distances.z) { distances=vec4(distances.xy,q,distances.z);d=c;c=id; }
        else if(q<distances.w) { distances.w=q;d=id; }
    }
    vec3 w=max(vec3(distances.w)-distances.xyz,vec3(0));
    w/=max(max(w.x,max(w.y,w.z)),0.0000001);
    // Compact smooth joins preserve a single source copy through most of each patch.
    w*=w;w*=w;w*=w;
    float total=dot(w,vec3(1));w=total>0.0000001?w/total:vec3(1,0,0);
    vec3 colour=cbPatch(material,a,salt,count,mean,dx,dy)*w.x
               +cbPatch(material,b,salt,count,mean,dx,dy)*w.y
               +cbPatch(material,c,salt,count,mean,dx,dy)*w.z;
    // Grass families share a living-green midpoint. Keep their authored detail,
    // but do not amplify blended dark samples below zero: the old variance gain
    // clipped them to black and made islands beside the paler soil-rich family.
    if(uGrassPalette>0.5) {
        return colour*mix(vec3(1),vec3(0.140,0.245,0.050)/max(mean,vec3(0.025)),0.75);
    }
    float gain=min(1.5,inversesqrt(max(dot(w,w),0.000001)));
    return clamp(mean+(colour-mean)*gain,0.0,1.0);
}
float cbExposedGround(float encoded,vec2 uv,float distanceToEdge) {
    if(encoded<0.5 || distanceToEdge>=0.12)return 0.0;
    float slot=encoded-1.0;
    vec2 tile=vec2(mod(slot,uMaskColumns),floor(slot/uMaskColumns));
    vec2 st=(tile*uMaskGeometry.z+vec2(uMaskGeometry.w)+uv*uMaskCell)/uMaskGeometry.xy;
    return texture2D(uFeatureMask,st).r*(1.0-smoothstep(0.0,0.12,distanceToEdge));
}
void main() {
    vec2 dx=dFdx(vWorld*uWorldScale),dy=dFdy(vWorld*uWorldScale);
    vec3 m=texture2D(uWeights,vWorld/uGrid).rgb;m.rg/=max(1.0,m.r+m.g);
    float g=max(0.0,1.0-m.r-m.g);vec3 colour=vec3(0);
    // Explicit gradients permit coherent branches: pure regions sample one family.
    if(g>0.00001)colour+=cbMaterial(uClear1,1.7,uLayouts.x,uMean1.rgb,dx,dy)*g;
    if(m.r>0.00001)colour+=cbMaterial(uClear2,17.3,uLayouts.y,uMean2.rgb,dx,dy)*m.r;
    if(m.g>0.00001)colour+=cbMaterial(uClear3,39.1,uLayouts.z,uMean3.rgb,dx,dy)*m.g;
    if(uGrassPalette>0.5) {
        float y=dot(colour,vec3(0.2126,0.7152,0.0722));
        colour=mix(vec3(y),colour,uSaturation);
        // A soft material toe retains grain in the dark grass before lighting.
        // Apply it before corner shade/visibility so shroud and cast shade stay dark.
        vec3 green=vec3(0.140,0.245,0.050);
        green=mix(vec3(dot(green,vec3(0.2126,0.7152,0.0722))),green,uSaturation);
        vec3 chroma=mix(green/dot(green,vec3(0.2126,0.7152,0.0722)),
                        colour/max(y,0.001),smoothstep(0.0,0.035,y));
        colour=chroma*(0.110+0.700*y);
        colour=max(colour-vColour.rgb*uTinted,vec3(0))*vColour.a;
    } else {
        colour=max(colour-vColour.rgb*uTinted,vec3(0))*vColour.a;
        float luminance=dot(colour,vec3(0.2126,0.7152,0.0722));
        colour=mix(vec3(luminance),colour,uSaturation);
    }
    vec4 feature=texture2D(uFeatureMask,vMaskUV);
    // Only authored Desert rock cells sample their neighbour's ground coverage. A
    // continuing cliff keeps its existing matte across the shared cell edge.
    vec2 local=(mod(vMaskUV*uMaskGeometry.xy,vec2(uMaskGeometry.z))
                -vec2(uMaskGeometry.w))/uMaskCell;
    float exposed=max(max(cbExposedGround(vGroundEdges.x,vec2(1.0,local.y),local.x),
                          cbExposedGround(vGroundEdges.y,vec2(0.0,local.y),1.0-local.x)),
                      max(cbExposedGround(vGroundEdges.z,vec2(local.x,1.0),local.y),
                          cbExposedGround(vGroundEdges.w,vec2(local.x,0.0),1.0-local.y)));
    // Saved mattes retain full cliffs; only authored shore/river records provide
    // neighbouring ground slots, to feather an otherwise rectangular termination.
    vec2 maskTile=floor(vMaskUV*uMaskGeometry.xy/uMaskGeometry.z);
    // Only source shore/river records opt in; continuing rock samples zero
    // ground on the neighbouring face and keeps its full native contour.
    feature.r=max(feature.r,exposed);
    // The physical clear overlay must use the same saved land cut as colour.
    // Otherwise it paints grass over the newly depth-separated water holes.
    if(uSurface==1)feature.r=min(feature.r,texture2D(uCoast,vCoastUV).a);
    feature.a=mix(feature.a,128.0/255.0,exposed);
    // An explicit snow feature matte is authoritative inside its cell. Blending it
    // back toward the old painting at an eligibility boundary resurrected drift.
    float solid=uSolidFeatureGround*step(0.5,maskTile.x+maskTile.y);
    gl_FragColor=vec4(colour*(feature.a*(255.0/128.0)),mix(m.b,1.0,solid)*feature.r);
}
)CBGLSL";
    GLuint v = fx_compile(GL_VERTEX_SHADER, vs, "clear-blend-vs");
    if (!v) return 0;
    GLuint f = fx_compile(GL_FRAGMENT_SHADER, fs, "clear-blend-fs");
    if (!f) { fx_glDeleteShader(v); return 0; }
    GLuint p = fx_glCreateProgram();
    fx_glAttachShader(p, v); fx_glAttachShader(p, f); fx_glLinkProgram(p);
    GLint ok = 0; fx_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    fx_glDeleteShader(v); fx_glDeleteShader(f);
    if (!ok) {
        char log[2048] = {0}; GLsizei n = 0;
        if (fx_glGetProgramInfoLog) fx_glGetProgramInfoLog(p, sizeof log - 1, &n, log);
        fprintf(stderr, "CLEARBLEND|shader refused|%s\n", log);
        fx_glDeleteProgram(p); return 0;
    }
    return p;
}

static bool cb_load(void)
{
    ClearBlendState& B = g_clearBlend;
    const std::string theater = g_raModule && !strcmp(g_pack.theater,"RASNOW") ? "RA_SNOW"
        : !strcmp(g_pack.theater,"DESERT") ? "DESERT" : !strcmp(g_pack.theater,"SAND") ? "SAND" : "TEMPERAT";
    if (B.tried && B.theater != theater) clear_blend_free();
    if (B.tried) return B.ok;
    if (!clear_blend_requested() || !fx_gl_ready) return false;
    B.tried = true; B.theater = theater;
    const char* path = clear_blend_path();
    FILE* file = fopen(path, "rb");
    if (!file) { fprintf(stderr, "CLEARBLEND|no pack|%s\n", path); return false; }
    unsigned char header[28];
    bool valid = fread(header, 1, sizeof header, file) == sizeof header;
    unsigned version = 0, cell = 0, width = 0, count = 0;
    unsigned layoutCounts[3]={1,1,1};
    if (valid) {
        version = cb_u32(header + 8); cell = cb_u32(header + 12);
        width = cb_u32(header + 16); count = cb_u32(header + 20);
        valid = !memcmp(header, "C3DCLR\0\0", 8)
             && ((version == 1 && count == 3) || (version == 2 && count == 15)
                 || (version == 3 && count >= 3 && count <= 24))
             && cell >= 24 && cell <= 240 && cell % 24 == 0
             && (width == 4 * cell || width == 8 * cell);
    }
    if(valid && version==2)layoutCounts[0]=layoutCounts[1]=layoutCounts[2]=5;
    if(valid && version==3) {
        unsigned char counts[12];valid=fread(counts,1,12,file)==12;
        if(valid)for(int k=0;k<3;++k)layoutCounts[k]=cb_u32(counts+4*k);
        valid=valid && layoutCounts[0]>=1 && layoutCounts[0]<=8
                    && layoutCounts[1]>=1 && layoutCounts[1]<=8
                    && layoutCounts[2]>=1 && layoutCounts[2]<=8
                    && layoutCounts[0]+layoutCounts[1]+layoutCounts[2]==count;
    }
    std::vector<unsigned char> rgb;
    if (valid) {
        rgb.resize((size_t)width * width * 3 * count);
        valid = fread(&rgb[0], 1, rgb.size(), file) == rgb.size() && fgetc(file) == EOF;
    }
    fclose(file);
    if (!valid) { fprintf(stderr, "CLEARBLEND|invalid pack|%s\n", path); return false; }
    B.variants=1;
    for(int k=0;k<3;++k) { B.layouts[k]=(int)layoutCounts[k];B.variants=std::max(B.variants,B.layouts[k]); }

    B.columns = std::min(3,B.variants); B.rows = (B.variants + B.columns - 1) / B.columns;
    B.pitch = (int)width + 128; B.atlasW = B.pitch * B.columns; B.atlasH = B.pitch * B.rows;
    GLint units = 0, maxSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
    if (units < 5 || B.atlasW > maxSize || B.atlasH > maxSize) {
        fprintf(stderr, "CLEARBLEND|driver refused|units=%d max=%d\n", units, maxSize);
        return false;
    }
    const char* extensions = (const char*)glGetString(GL_EXTENSIONS);
    if (!extensions || !strstr(extensions, "GL_ARB_shader_texture_lod")) {
        fprintf(stderr, "CLEARBLEND|driver refused|stochastic sampling needs GL_ARB_shader_texture_lod\n");
        return false;
    }
    B.program = cb_program();
    if (!B.program) return false;
    GLint align = 4; glGetIntegerv(GL_UNPACK_ALIGNMENT, &align);
    glPushAttrib(GL_TEXTURE_BIT);
    glActiveTexture(GL_TEXTURE0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    bool uploaded = true;
    size_t fieldOffset=0;
    for (int k = 0; k < 3; ++k) {
        glGenTextures(1, &B.material[k]);
        if (!B.material[k]) { uploaded = false; break; }
        glBindTexture(GL_TEXTURE_2D, B.material[k]);
        const size_t pixels=(size_t)B.layouts[k]*width*width;
        double sums[3]={0,0,0};
        for(size_t p=0;p<pixels;++p)for(int c=0;c<3;++c)sums[c]+=rgb[(fieldOffset+p)*3+c];
        for(int c=0;c<3;++c)B.means[k][c]=(float)(sums[c]/(255.0*pixels));
        if (!cb_upload_family(&rgb[fieldOffset*3],
                               (int)width, B.layouts[k], B.columns, B.rows)) {
            uploaded = false; break;
        }
        fieldOffset+=pixels;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, align);
    glPopAttrib();
    if (!uploaded) {
        clear_blend_free(); g_clearBlend.tried = true; g_clearBlend.theater = theater;
        fprintf(stderr, "CLEARBLEND|driver refused|incomplete material mip allocation\n");
        return false;
    }
    B.seed = cb_u32(header + 24); B.cell=(int)cell; B.width = (int)width; B.ok = true;
    fprintf(stderr, "CLEARBLEND|pack valid|%s|v%u|%u a cell|%ux%u|%u RGB fields|%u seed|"
                    "layouts=%d,%d,%d|atlas=%dx%d|sampling=warped-nearest-sites|detail=source-preserving\n",
             path, version, cell, width, width, count, B.seed, B.layouts[0], B.layouts[1], B.layouts[2], B.atlasW, B.atlasH);
    return true;
}

static bool clear_blend_prepare(int width, int height,
                                const std::vector<unsigned char>& eligible, unsigned mapSeed,
                                float saturation = 0.70f)
{
    if (!cb_load() || g_clearBlend.fieldFailed) return false;
    ClearBlendState& B = g_clearBlend;
    B.saturation=std::isfinite(saturation)?std::max(0.0f,std::min(1.5f,saturation)):0.70f;
    if (width <= 0 || height <= 0 || width > 256 || height > 256
        || eligible.size() != (size_t)width * height) return false;
    const unsigned seed = clear_blend_hash(B.seed ^ mapSeed);
    const unsigned sig = cb_mask_signature(width, height, eligible, seed);
    if (B.weights && B.fieldSignature == sig && B.gridW == width && B.gridH == height) return true;
    ClearBlendField field = cb_make_field(width, height, eligible, seed);
    if((B.theater=="DESERT" || B.theater=="SAND"))cb_desert_material_weights(field,seed);
    if (field.rgba.empty()) return false;
    GLint maxSize = 0; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
    if (field.w > maxSize || field.h > maxSize) return false;
    glPushAttrib(GL_TEXTURE_BIT);
    glActiveTexture(GL_TEXTURE0);
    if (!B.weights) glGenTextures(1, &B.weights);
    if (!B.weights) {
        glPopAttrib(); B.fieldFailed = true;
        fprintf(stderr, "CLEARBLEND|driver refused|no weight texture name\n");
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, B.weights);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, field.w, field.h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, &field.rgba[0]);
    GLint actualW = 0, actualH = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &actualW);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &actualH);
    glPopAttrib();
    if (!B.weights || actualW != field.w || actualH != field.h) {
        if (B.weights) glDeleteTextures(1, &B.weights);
        B.weights = 0; B.fieldFailed = true;
        fprintf(stderr, "CLEARBLEND|driver refused|incomplete weight allocation\n");
        return false;
    }
    B.gridW = width; B.gridH = height; B.fieldSignature = sig;B.control=field.rgba;
    B.geometry.assign((size_t)width * height * 6 * 3, 0);
    B.colours.assign((size_t)width * height * 6 * 4, 0);
    B.maskUV.assign((size_t)width * height * 6 * 2, 0);
    B.groundEdges.assign((size_t)width * height * 6 * 4, 0);
    B.heights.assign((size_t)width * height * 4, 0);
    B.known.assign((size_t)width * height, 0);
    int eligibleCount = 0;
    for (size_t i = 0; i < eligible.size(); ++i) eligibleCount += eligible[i] != 0;
    double mix[3] = {0, 0, 0}, covered = 0;
    for (size_t i = 0; i < field.rgba.size(); i += 4) {
        const double a = field.rgba[i + 2] / 255.0;
        const double w2 = field.rgba[i] / 255.0, w3 = field.rgba[i + 1] / 255.0;
        mix[0] += a * (1 - w2 - w3); mix[1] += a * w2; mix[2] += a * w3; covered += a;
    }
    const double norm = covered > 0 ? 100.0 / covered : 0;
    fprintf(stderr, "CLEARBLEND|field|%dx%d|eligible=%d|seed=%u|hash=%u|regions=%d,%d|"
                    "control=%dx%d|mix=%.1f,%.1f,%.1f\n",
             width, height, eligibleCount, seed, sig, field.clumps[0], field.clumps[1],
             field.w, field.h, mix[0] * norm, mix[1] * norm, mix[2] * norm);
    const char* dump = getenv("OPENCNC3D_CLEARBLEND_DUMP");
    if (dump && *dump) {
        FILE* f = fopen(dump, "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", field.w, field.h);
            for (size_t i = 0; i < field.rgba.size(); i += 4) {
                const unsigned w2 = field.rgba[i], w3 = field.rgba[i + 1];
                const unsigned a = field.rgba[i + 2];
                const unsigned char rgb[3] = {(unsigned char)((255 - w2 - w3) * a / 255),
                    (unsigned char)(w2 * a / 255), (unsigned char)(w3 * a / 255)};
                fwrite(rgb, 1, 3, f);
            }
            fclose(f); fprintf(stderr, "CLEARBLEND|control dump|%s\n", dump);
        }
    }
    return true;
}

/* Grass roots use the same map-wide material and saturation as the surface,
   rather than inheriting the old two-cell colour cycle from the unchanged atlas. */
static unsigned clear_blend_colour_signature(void)
{ return g_clearBlend.ok && g_clearBlend.weights
       ? clear_blend_hash(g_clearBlend.fieldSignature ^ (unsigned)std::floor(g_clearBlend.saturation*1000+0.5f)) : 0; }
static bool clear_blend_grass_colour(int x,int z,unsigned char* colour)
{
    const ClearBlendState& B=g_clearBlend;
    if(B.theater != "TEMPERAT" || !B.ok || B.control.empty() || x<0 || z<0 || x>=B.gridW || z>=B.gridH)return false;
    const int sw=B.gridW*4;
    float w2=0,w3=0,coverage=0;
    // Average the central four control samples of this cell, matching bilinear use.
    for(int j=1;j<=2;++j)for(int i=1;i<=2;++i) {
        const unsigned char* q=&B.control[((size_t)(z*4+j)*sw+x*4+i)*4];
        w2+=q[0]/1020.0f;w3+=q[1]/1020.0f;coverage+=q[2]/1020.0f;
    }
    if(coverage<=0)return false;
    float mixed[3];
    const float target[3]={0.140f,0.245f,0.050f};
    for(int c=0;c<3;++c) {
        mixed[c]=0;
        const float weight[3]={1-w2-w3,w2,w3};
        for(int k=0;k<3;++k) {
            const float mean=B.means[k][c];
            const float gain=0.25f+0.75f*target[c]/std::max(mean,0.025f);
            mixed[c]+=255*mean*gain*weight[k];
        }
    }
    const float luminance=mixed[0]*0.2126f+mixed[1]*0.7152f+mixed[2]*0.0722f;
    for(int c=0;c<3;++c) {
        // Same material toe and saturation as cb_program, before blade lighting.
        const float value=(luminance+(mixed[c]-luminance)*B.saturation)
                           *(0.110f*255+0.700f*luminance)/std::max(luminance,0.001f);
        colour[c]=(unsigned char)std::floor(std::max(0.0f,std::min(255.0f,
                            value*coverage+colour[c]*(1-coverage)))+0.5f);
    }
    return true;
}

static void clear_blend_draw(const std::vector<ClearBlendCell>& cells, bool tinted)
{
    ClearBlendState& B = g_clearBlend;
    if (!B.ok || !B.weights || cells.empty()) return;
    terrain_grass_masks_ensure();
    /* The shader generates continuous world UVs. Positions stay cached at stable cell
       indices; camera changes only assemble a list of which triangles to draw. */
    static std::vector<unsigned> indices;
    indices.clear(); indices.reserve(cells.size() * 6);
    const int corner[6] = {0, 3, 1, 1, 3, 2};
    const float dx[4] = {0, 1, 1, 0}, dz[4] = {0, 0, 1, 1};
    for (size_t i = 0; i < cells.size(); ++i) {
        const ClearBlendCell& c = cells[i];
        // A raised cell replaces the native slab. Painting both leaves a second
        // cliff silhouette below the displaced foot and tears at template joins.
        if (terrain_surface_has_cell(c.x,c.z)) continue;
        if (c.x < 0 || c.z < 0 || c.x >= B.gridW || c.z >= B.gridH) continue;
        const size_t ci = (size_t)c.z * B.gridW + c.x;
        const bool changed = !B.known[ci]
            || memcmp(&B.heights[ci * 4], c.height, sizeof c.height) != 0;
        for (int j = 0; j < 6; ++j) {
            const int k = corner[j]; const size_t vi = ci * 6 + j;
            terrain_grass_mask_uv(c.grassMask,dx[k],dz[k],&B.maskUV[vi*2]);
            memcpy(&B.groundEdges[vi*4],c.groundEdges,sizeof c.groundEdges);
            indices.push_back((unsigned)vi);
            if (changed) {
                B.geometry[vi * 3] = c.x + dx[k];
                B.geometry[vi * 3 + 1] = c.height[k]; B.geometry[vi * 3 + 2] = c.z + dz[k];
            }
            B.colours[vi * 4] = c.tint[k][0] / 255.0f;
            B.colours[vi * 4 + 1] = c.tint[k][1] / 255.0f;
            B.colours[vi * 4 + 2] = c.tint[k][2] / 255.0f;
            B.colours[vi * 4 + 3] = c.light[k];
        }
        if (changed) { memcpy(&B.heights[ci * 4], c.height, sizeof c.height); B.known[ci] = 1; }
    }
    typedef void (APIENTRY *BindBuffer)(GLenum, GLuint);
    static BindBuffer bindBuffer = (BindBuffer)SDL_GL_GetProcAddress("glBindBuffer");
    if (!bindBuffer) bindBuffer = (BindBuffer)SDL_GL_GetProcAddress("glBindBufferARB");
    if (!bindBuffer) return;      /* original ground already drew */
    GLint oldProgram = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    const GLuint oldBuffer = gls_array_binding();
    GLint oldElement = 0; glGetIntegerv(0x8895 /* GL_ELEMENT_ARRAY_BUFFER_BINDING */, &oldElement);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE);
    glDisable(GL_ALPHA_TEST); glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1.0f, -1.0f);
    for (int k = 0; k < 4; ++k) {
        glActiveTexture(GL_TEXTURE0 + k);
        glBindTexture(GL_TEXTURE_2D, k == 3 ? B.weights : B.material[k]);
    }
    glActiveTexture(GL_TEXTURE0+4);
    glBindTexture(GL_TEXTURE_2D,g_terrainGrassMasks.texture);
    fx_glUseProgram(B.program);
    fx_set1i(B.program,"uSurface",0);
    fx_set1i(B.program,"uFeatureMask",4);
    const TerrainGrassMasks& M=g_terrainGrassMasks;
    const float maskGeometry[4]={(float)M.width,(float)M.height,(float)M.pitch,(float)M.gutter};
    fx_set4fv(B.program,"uMaskGeometry",1,maskGeometry);
    fx_set1f(B.program,"uMaskCell",(float)M.cell);
    fx_set1f(B.program,"uMaskColumns",(float)M.columns);
    fx_set1i(B.program, "uClear1", 0); fx_set1i(B.program, "uClear2", 1);
    fx_set1i(B.program, "uClear3", 2); fx_set1i(B.program, "uWeights", 3);
    fx_set2f(B.program, "uGrid", (float)B.gridW, (float)B.gridH);
    fx_set1f(B.program, "uTinted", tinted ? 1.0f : 0.0f);
    fx_set1f(B.program, "uSaturation", B.saturation);
    fx_set1f(B.program, "uGrassPalette", B.theater=="TEMPERAT" && !fx_snow_terrain()?1.0f:0.0f);
    fx_set1f(B.program, "uSolidFeatureGround", (B.theater=="RA_SNOW" || (B.theater=="DESERT" || B.theater=="SAND"))?1.0f:0.0f);
    const float atlas[4] = {(float)B.width, (float)B.pitch, (float)B.atlasW, (float)B.atlasH};
    fx_set4fv(B.program, "uAtlas", 1, atlas);
    const float layouts[4]={(float)B.layouts[0],(float)B.layouts[1],(float)B.layouts[2],0};
    fx_set4fv(B.program,"uLayouts",1,layouts);
    fx_set4fv(B.program,"uMean1",1,B.means[0]);
    fx_set4fv(B.program,"uMean2",1,B.means[1]);
    fx_set4fv(B.program,"uMean3",1,B.means[2]);
    fx_set1f(B.program, "uColumns", (float)B.columns);
    fx_set1f(B.program, "uWorldScale", B.cell/(float)B.width);
    fx_set1f(B.program, "uSiteScale", (B.theater=="DESERT" || B.theater=="SAND")?4.0f:1.0f);
    fx_set1f(B.program, "uSeed", (float)(B.seed & 65535u) * (1.0f / 97.0f));
    bindBuffer(GL_ARRAY_BUFFER, 0); g_glsArrayKnown = false;
    bindBuffer(0x8893 /* GL_ELEMENT_ARRAY_BUFFER */, 0);
    glDisableClientState(GL_NORMAL_ARRAY); glDisableClientState(GL_INDEX_ARRAY);
    glDisableClientState(GL_EDGE_FLAG_ARRAY);
    /* Enabled legacy arrays are still part of DrawElements' input state, even when
       this shader does not use them. They may belong to a shorter caller buffer. */
    for (int unit = 0; unit < g_glsClientLimit; ++unit) {
        glClientActiveTexture(GL_TEXTURE0 + unit);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }
    glClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, &B.geometry[0]);
    glColorPointer(4, GL_FLOAT, 0, &B.colours[0]);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2,GL_FLOAT,0,&B.maskUV[0]);
    glClientActiveTexture(GL_TEXTURE1);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(4,GL_FLOAT,0,&B.groundEdges[0]);
    glClientActiveTexture(GL_TEXTURE0);
    if(!indices.empty())glDrawElements(GL_TRIANGLES, (GLsizei)indices.size(), GL_UNSIGNED_INT, &indices[0]);
    terrain_surface_clear_draw(cells);
    glPopClientAttrib();
    bindBuffer(GL_ARRAY_BUFFER, oldBuffer); g_glsArray = oldBuffer; g_glsArrayKnown = true;
    bindBuffer(0x8893 /* GL_ELEMENT_ARRAY_BUFFER */, (GLuint)oldElement);
    fx_glUseProgram((GLuint)oldProgram);
    glPopAttrib();
}
#endif
