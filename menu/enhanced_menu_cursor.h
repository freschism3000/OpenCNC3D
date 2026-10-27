/* Read the unchanged approved CUR00 from the game's ECUR asset. The title menu
   has no mission pack; it must not depend on world cursor initialization. */
namespace emcursor {
struct Vertex {float x,y,z,u,v;unsigned char r,g,b,a;};
struct Tri {short tex;unsigned char mode,part;Vertex v[3];};
struct Texture {int w,h;std::vector<unsigned char> rgba;GLuint gl=0;};
static std::vector<Texture> textures;
static std::vector<Tri> mesh;
static bool tried=false;
static bool load(){
 if(tried)return !mesh.empty();tried=true;
 FILE *f=fopen("ecur/ecur.bin","rb");if(!f)return false;
 std::vector<unsigned char> bytes;unsigned char chunk[65536];size_t n;
 while((n=fread(chunk,1,sizeof chunk,f))>0){bytes.insert(bytes.end(),chunk,chunk+n);if(bytes.size()>64*1024*1024){fclose(f);return false;}}fclose(f);
 size_t pos=0;
 auto read=[&](void *dst,size_t len){if(len>bytes.size()-pos)return false;memcpy(dst,bytes.data()+pos,len);pos+=len;return true;};
 char magic[4];unsigned ver,nt,nm;
 if(!read(magic,4)||memcmp(magic,"ECUR",4)||!read(&ver,4)||ver!=1||!read(&nt,4)||nt>256||!read(&nm,4)||nm>256)return false;
 std::vector<Texture> tx;std::vector<Tri> pointer;
 for(unsigned i=0;i<nt;i++){unsigned short w,h;if(!read(&w,2)||!read(&h,2)||!w||!h||w>256||h>256)return false;
  Texture t;t.w=w;t.h=h;t.rgba.resize(size_t(w)*h*4);if(!read(t.rgba.data(),t.rgba.size()))return false;tx.push_back(std::move(t));}
 for(unsigned i=0;i<nm;i++){char name[16];unsigned count;if(!read(name,16)||!read(&count,4)||count>100000)return false;
  bool wanted=!memcmp(name,"CUR00\0",6);
  for(unsigned j=0;j<count;j++){Tri t;if(!read(&t.tex,2)||!read(&t.mode,1)||!read(&t.part,1)||t.tex < -1||t.tex>=int(nt)||(t.mode!=0&&t.mode!=3))return false;
   for(int k=0;k<3;k++){Vertex &p=t.v[k];if(!read(&p.x,20)||!read(&p.r,4)||!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||!std::isfinite(p.u)||!std::isfinite(p.v))return false;}
   if(wanted)pointer.push_back(t);
  }
 }
 if(pos!=bytes.size()||pointer.empty())return false;
 textures.swap(tx);mesh.swap(pointer);fprintf(stderr,"ENMENU|cursor|approved CUR00|triangles=%zu\n",mesh.size());return true;
}
static void draw(int x,int y,int scale){
 if(!load())return;
 /* Same south-facing 55 degree camera as ECUR's authored triangle order.
    CUR00's top-left tip is the hotspot, independent of its glow margin. */
 const float unit=40.f*scale/(770.949f*.819152f);
 glDisable(GL_DEPTH_TEST);glDisable(GL_SCISSOR_TEST);glDepthMask(GL_FALSE);
 for(const Tri &t:mesh){
  glBlendFunc(t.mode==3?GL_ONE:GL_SRC_ALPHA,t.mode==3?GL_ONE:GL_ONE_MINUS_SRC_ALPHA);
  if(t.tex>=0){Texture &a=textures[t.tex];if(!a.gl){glGenTextures(1,&a.gl);glBindTexture(GL_TEXTURE_2D,a.gl);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,a.w,a.h,0,GL_RGBA,GL_UNSIGNED_BYTE,a.rgba.data());}glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,a.gl);}else glDisable(GL_TEXTURE_2D);
  glBegin(GL_TRIANGLES);for(const Vertex &p:t.v){glColor4ub(p.r,p.g,p.b,p.a);glTexCoord2f(p.u,p.v);glVertex2f(x+(p.x-16.9984f)*unit,y+((p.z-7.00416f)*.819152f-(p.y-88.4019f)*.573576f)*unit);}glEnd();
 }
 glEnable(GL_TEXTURE_2D);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glColor4f(1,1,1,1);
}
}
