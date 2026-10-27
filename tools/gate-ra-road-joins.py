#!/usr/bin/env python3
"""Hidden GPU regression: diagonal/non-joined snow must not cut a road."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess
import numpy as np
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--runtime',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
h=a.source/'game/ra3d_road_join.h';text=h.read_text();fs=re.search(r'R"RJGLSL\((.*?)\)RJGLSL"',text,re.S).group(1);bad=fs.replace('if(hasMask)weight*=1.0-texture2D(groundMasks,mix(maskTile[k].xy,maskTile[k].zw,st)).r;','if(hasMask)weight*=1.0;');assert bad!=fs
bad_coverage=fs.replace('if(maskTile[4].z>maskTile[4].x)zone*=1.0-texture2D(groundMasks,mix(maskTile[4].xy,maskTile[4].zw,clamp(p,vec2(0),vec2(1)))).r;','zone*=1.0;');assert bad_coverage!=fs
code=r'''
#define GL_SILENCE_DEPRECATION 1
#include <SDL.h>
#include <OpenGL/gl.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
GLuint shader(GLenum t,const char*s){GLuint q=glCreateShader(t);glShaderSource(q,1,&s,0);glCompileShader(q);GLint ok;glGetShaderiv(q,GL_COMPILE_STATUS,&ok);if(!ok){char b[8192];glGetShaderInfoLog(q,sizeof b,0,b);puts(b);exit(5);}return q;}
int main(int argc,char**argv){SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP,"1");if(SDL_Init(SDL_INIT_VIDEO))return 2;SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);auto w=SDL_CreateWindow("hidden road join regression",0,0,256,256,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);if(!w)return 3;auto c=SDL_GL_CreateContext(w);if(!c)return 4;
const char*vs="#version 120\nvarying vec2 world;varying vec4 shade;void main(){world=gl_Vertex.xy*.5+.5;shade=vec4(1);gl_Position=gl_Vertex;}";
const char* sources[]={R"SHADER(PRODUCTION)SHADER",R"SHADER(NEGATIVE)SHADER",R"SHADER(COVERAGE)SHADER"};
glViewport(0,0,256,256);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glClearColor(0,0,0,0);
for(int variant=0;variant<3;variant++)for(int arm=0;arm<4;arm++){
 GLuint v=shader(GL_VERTEX_SHADER,vs),f=shader(GL_FRAGMENT_SHADER,sources[variant]),p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok)return 6;glUseProgram(p);
 const int size=48;std::vector<unsigned char> pixels(size*size*4,255),masks(size*size,255);float tiles[36],crop[36]={0};float joins[4]={0};if(arm==0||arm==3)joins[3]=1;if(arm==1)joins[0]=joins[2]=1;if(arm==2)joins[1]=joins[3]=1;
 for(int k=0;k<9;k++){
  const bool road=(k==4&&arm!=3)||(k==3&&joins[0])||(k==5&&joins[1])||(k==1&&joins[2])||(k==7&&joins[3]);
  for(int y=0;y<16;y++)for(int x=0;x<16;x++){auto*q=&pixels[((k/3*16+y)*size+k%3*16+x)*4];q[0]=road?89:245;q[1]=road?64:245;q[2]=road?38:245;masks[(k/3*16+y)*size+k%3*16+x]=road?0:255;}
  tiles[k*4]=(k%3*16+.5f)/size;tiles[k*4+1]=(k/3*16+.5f)/size;tiles[k*4+2]=(k%3*16+15.5f)/size;tiles[k*4+3]=(k/3*16+15.5f)/size;
 }
 GLuint tex;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 GLuint mask;glGenTextures(1,&mask);glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,mask);glTexImage2D(GL_TEXTURE_2D,0,GL_LUMINANCE,size,size,0,GL_LUMINANCE,GL_UNSIGNED_BYTE,masks.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glActiveTexture(GL_TEXTURE0);glUniform1i(glGetUniformLocation(p,"groundMasks"),1);glUniform4fv(glGetUniformLocation(p,"maskTile[0]"),9,tiles);
 glUniform1i(glGetUniformLocation(p,"atlas"),0);glUniform2f(glGetUniformLocation(p,"origin"),0,0);glUniform4fv(glGetUniformLocation(p,"tile[0]"),9,tiles);glUniform4fv(glGetUniformLocation(p,"crop[0]"),9,crop);glUniform4fv(glGetUniformLocation(p,"joins"),1,joins);
 glClear(GL_COLOR_BUFFER_BIT);glBegin(GL_QUADS);glVertex2f(-1,-1);glVertex2f(1,-1);glVertex2f(1,1);glVertex2f(-1,1);glEnd();std::vector<unsigned char> output(256*256*4);glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,output.data());char name[4096];snprintf(name,sizeof name,"%s/join-%d-%d.rgba",argv[1],variant,arm);FILE*out=fopen(name,"wb");fwrite(output.data(),1,output.size(),out);fclose(out);glDeleteTextures(1,&tex);glDeleteTextures(1,&mask);glDeleteProgram(p);glDeleteShader(v);glDeleteShader(f);
}SDL_GL_DeleteContext(c);SDL_DestroyWindow(w);SDL_Quit();}
'''.replace('PRODUCTION',fs).replace('NEGATIVE',bad).replace('COVERAGE',bad_coverage)
cpp=a.output/'road-joins.cpp';cpp.write_text(code);exe=a.output/'road-joins';flags=subprocess.check_output(['sdl2-config','--cflags'],text=True).split();subprocess.run(['clang++','-std=c++14',str(cpp),'-o',str(exe),*flags,str(a.runtime/'libSDL2-2.0.0.dylib'),'-framework','OpenGL'],check=True);subprocess.run([str(exe),str(a.output)],check=True,env={**os.environ,'SDL_MAC_BACKGROUND_APP':'1','DYLD_LIBRARY_PATH':str(a.runtime)})
rows=[]
for arm in range(3):
 prod=np.fromfile(a.output/f'join-0-{arm}.rgba',np.uint8).reshape(256,256,4);bad=np.fromfile(a.output/f'join-1-{arm}.rgba',np.uint8).reshape(256,256,4);mask=prod[...,3]>4;error=np.abs(prod[mask,:3].astype(int)-[89,64,38]);negative=(np.abs(bad[...,:3].astype(int)-[89,64,38]).max(2)>16)&mask;assert error.max()==0,error.max();assert negative.sum()>200,negative.sum();rows.append(dict(arm=arm,drawn_pixels=int(mask.sum()),max_rgb_error=int(error.max()),negative_control_artifact_pixels=int(negative.sum())))
prod=np.fromfile(a.output/'join-0-3.rgba',np.uint8).reshape(256,256,4);bad=np.fromfile(a.output/'join-2-3.rgba',np.uint8).reshape(256,256,4);assert not prod[...,3].any();assert (bad[...,3]>4).sum()>200
rows.append(dict(control='Snow foreground stays clear',drawn_pixels=int((prod[...,3]>4).sum()),negative_control_artifact_pixels=int((bad[...,3]>4).sum())))
r=dict(passed=True,hidden=True,production_shader_sha256=hashlib.sha256(h.read_bytes()).hexdigest(),rows=rows);(a.output/'RESULTS.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
