/* The user's experimental Minigunner. Included after livery and billboard helpers. */
#ifndef OPENCNC3D_NEW_SPRITES_MOD_H
#define OPENCNC3D_NEW_SPRITES_MOD_H
#include "new_sprites_data.h"
#include "new_sprites_colour.h"
static NsPack g_nsPack;
static bool g_nsTried=false,g_nsReady=false;
struct NsCachedStrip { int slot; NsGrade grade; };
static std::map<int,NsCachedStrip> g_nsStrips;
static bool new_sprite_active(const SimObject& o)
{
    if(!g_fx.new_sprites||g_raModule||g_fx.dos_classic||o.kind!=K_INFANTRY||strcmp(o.type,"E1"))return false;
    if(!g_nsTried){
        g_nsTried=true;g_nsReady=ns_read_pack("sprites/e1-v15.pack",&g_nsPack);
        fprintf(stderr,"NEWSPRITES|pack|%s|frames=532|actions=31|anchor=54,40|tpu=48\n",g_nsReady?"loaded":"unavailable; original art retained");
    }
    return g_nsReady;
}
static void new_sprites_close()
{
    NsPack empty;g_nsPack=std::move(empty);g_nsStrips.clear();g_nsReady=g_nsTried=false;
    /* GL textures belong to g_dosStrips and are released by dosinf_free. */
}
static int new_sprite_strip(int action,int livery)
{
    if(action<0||action>=31||!g_nsReady)return -1;
    if(livery<0||livery>=LIVERY_COUNT)livery=0;
    int key=action*LIVERY_COUNT+livery;
    const NsGrade grade={g_fx.ns_bright,g_fx.ns_contrast,g_fx.ns_exposure,g_fx.ns_gamma,g_fx.ns_sat};
    std::map<int,NsCachedStrip>::iterator found=g_nsStrips.find(key);
    bool fresh=found==g_nsStrips.end();
    if(!fresh&&ns_grade_equal(found->second.grade,grade))return found->second.slot;
    const NsAction& a=g_nsPack.actions[action];
    DosStrip s={};snprintf(s.name,sizeof s.name,"E1_NEW_V15");
    s.fw=a.x1-a.x0;s.fh=a.y1-a.y0;s.frames=(int)a.frames.size();s.stages=s.src_stages=a.count;s.facings=a.facings;
    s.cols=1;while((s.cols*2)*s.fw<=512&&s.cols*2<=s.frames)s.cols*=2;
    s.texw=s.texh=1;while(s.texw<s.cols*s.fw)s.texw*=2;
    while(s.texh<((s.frames+s.cols-1)/s.cols)*s.fh)s.texh*=2;
    s.tpu=48;s.ox=(a.x0+a.x1)*.5f-54;s.drop=(float)a.y1-40;
    /* Always derive from the original palette, after explicit player paint. */
    unsigned char palette[33][4];memcpy(palette,g_nsPack.palette,sizeof palette);
    for(int i=0;i<33;i++) {
        if(livery&&g_nsPack.paint[i])memcpy(palette[i],LIVERY_BAND[livery][g_nsPack.paint[i]-1],3);
        ns_grade_colour(palette[i],grade);
    }
    std::vector<unsigned char> rgba((size_t)s.texw*s.texh*4,0);
    for(int frame=0;frame<s.frames;frame++)for(int y=0;y<s.fh;y++)for(int x=0;x<s.fw;x++) {
        unsigned index=g_nsPack.pixels[(size_t)a.frames[frame]*7800+(y+a.y0)*100+x+a.x0];
        unsigned char* dst=&rgba[((frame/s.cols*s.fh+y)*(size_t)s.texw+frame%s.cols*s.fw+x)*4];
        memcpy(dst,palette[index],4);
    }
    GLint previous=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&previous);
    if(fresh)glGenTextures(1,&s.gl);else s.gl=g_dosStrips[found->second.slot].gl;
    glBindTexture(GL_TEXTURE_2D,s.gl);
    /* Native pixels remain crisp regardless of terrain filtering. */
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    if(fresh)glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,s.texw,s.texh,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
    else glTexSubImage2D(GL_TEXTURE_2D,0,0,0,s.texw,s.texh,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
    glBindTexture(GL_TEXTURE_2D,(GLuint)previous);
    int slot=fresh?(int)g_dosStrips.size():found->second.slot;
    if(fresh) {
        g_dosStrips.push_back(s);
        fprintf(stderr,"NEWSPRITES|strip|action=%d|livery=%d|frames=%d|cell=%dx%d|ox=%.1f|drop=%.1f\n",action,livery,s.frames,s.fw,s.fh,s.ox,s.drop);
    }
    g_nsStrips[key]={slot,grade};
    fprintf(stderr,"NEWSPRITES|grade|action=%d|livery=%d|slot=%d|brightness=%.2f|contrast=%.2f|exposure=%.2f|gamma=%.2f|saturation=%.2f|cached=%zu\n",action,livery,slot,grade.brightness,grade.contrast,grade.exposure,grade.gamma,grade.saturation,g_nsStrips.size());
    return slot;
}
#endif
