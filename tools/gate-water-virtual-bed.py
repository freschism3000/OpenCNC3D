#!/usr/bin/env python3
"""Hidden GPU check of the production virtual slope, without a seabed mesh."""
from pathlib import Path
import argparse,json,os,subprocess,hashlib
import numpy as np

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--runtime',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
 h=a.source/'game/water_virtual_bed.h'
 code=r'''
#define GL_SILENCE_DEPRECATION 1
#include <SDL.h>
#include <OpenGL/gl.h>
#include <cstdio>
#include <cstdlib>
#include "HEADER"
GLuint shader(GLenum t,const char*s){GLuint q=glCreateShader(t);glShaderSource(q,1,&s,0);glCompileShader(q);GLint ok;glGetShaderiv(q,GL_COMPILE_STATUS,&ok);if(!ok){char b[8192];glGetShaderInfoLog(q,sizeof b,0,b);puts(b);exit(5);}return q;}
int main(int argc,char**argv){SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP,"1");if(SDL_Init(SDL_INIT_VIDEO))return 2;SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);auto w=SDL_CreateWindow("hidden virtual bed check",0,0,512,1,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);if(!w)return 3;auto c=SDL_GL_CreateContext(w);if(!c)return 4;
const char*vs="#version 120\nvoid main(){gl_Position=gl_Vertex;}";
const char*fs="#version 120\nuniform sampler2D uLand;uniform vec2 uFieldScale;uniform float river;uniform float light;\n" WATER_VIRTUAL_BED_GLSL "void main(){gl_FragColor=vec4(waterVirtualBed(vec2(0),vec2(0),gl_FragCoord.x/512.0*4.0,river,light),1.0);}";
GLuint v=shader(GL_VERTEX_SHADER,vs),f=shader(GL_FRAGMENT_SHADER,fs),p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok)return 6;glUseProgram(p);glViewport(0,0,512,1);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);GLuint tex;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);float bank[]={.6f,.4f,.2f,1};glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F_ARB,1,1,0,GL_RGBA,GL_FLOAT,bank);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glUniform1i(glGetUniformLocation(p,"uLand"),0);glUniform2f(glGetUniformLocation(p,"uFieldScale"),1,1);
for(int i=0;i<5;i++){if(i==2){float snow[]={.95f,.95f,.95f,1};glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F_ARB,1,1,0,GL_RGBA,GL_FLOAT,snow);}if(i==3){float dark[]={0,0,0,1};glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F_ARB,1,1,0,GL_RGBA,GL_FLOAT,dark);}glUniform1f(glGetUniformLocation(p,"river"),i==1?1.0f:0.0f);glUniform1f(glGetUniformLocation(p,"light"),i==4?.2f:1.0f);glBegin(GL_QUADS);glVertex2f(-1,-1);glVertex2f(1,-1);glVertex2f(1,1);glVertex2f(-1,1);glEnd();float pixels[512*4];glReadPixels(0,0,512,1,GL_RGBA,GL_FLOAT,pixels);char path[4096];snprintf(path,sizeof path,"%s/slope-%d.rgba",argv[1],i);FILE*out=fopen(path,"wb");fwrite(pixels,4,512*4,out);fclose(out);}SDL_GL_DeleteContext(c);SDL_DestroyWindow(w);SDL_Quit();}
'''.replace('HEADER',str(h.resolve()))
 cpp=a.output/'virtual-bed.cpp';cpp.write_text(code);exe=a.output/'virtual-bed';flags=subprocess.check_output(['sdl2-config','--cflags'],text=True).split();subprocess.run(['clang++','-std=c++14',str(cpp),'-o',str(exe),*flags,str(a.runtime/'libSDL2-2.0.0.dylib'),'-framework','OpenGL'],check=True)
 subprocess.run([str(exe),str(a.output)],check=True,env={**os.environ,'SDL_MAC_BACKGROUND_APP':'1','DYLD_LIBRARY_PATH':str(a.runtime)})
 rows=[]
 for i in range(5):
  colour=np.fromfile(a.output/f'slope-{i}.rgba',dtype='f4').reshape(512,4);sd=(np.arange(512)+.5)/512*4;t=np.clip(sd/(1.0 if i==1 else 3.0),0,1);depth=3*t*t*(3-2*t);q=np.clip((depth-1.25)/(2.7-1.25),0,1);trans=np.exp(-depth[:,None]*[2.8,1.8,1.3])*(1-q*q*(3-2*q))[:,None];bank=np.array([.6,.4,.2])if i<2 else np.array([.95,.95,.95])if i==2 else np.zeros(3);gain=min(.70,.42/max(.001,float(bank@[.299,.587,.114])));expected=np.array([.008,.018,.024])*(1-trans)+np.maximum(bank*gain*.96,np.array([.16,.19,.22])*(.2 if i==4 else 1))*trans;error=float(abs(colour[:,:3]-expected).max());assert error<.0045,error
  assert np.max(abs(colour[sd>=(1.0 if i==1 else 3.0),:3]-[.008,.018,.024]))<.0045
  assert np.all(colour[:,3]==1) and np.max(colour[0,:3])<.43
  if i<3:assert np.min(colour[0,:3])>.12
  elif i==3:
   assert np.min(colour[0,:3])>.11 and np.min(colour[:,:3])>.004
  else:
   assert .02<np.min(colour[0,:3]) and np.max(colour[0,:3])<.05
  assert np.max(np.diff(colour[:,:3],axis=0))<.0045
  assert np.max(abs(np.diff(colour[:,:3],axis=0)))<.025
  if i<3:
   # A visible submerged slope must survive beyond a contact-only hairline.
   mid=int((.25 if i==1 else .75)/4*512);luma=colour[:,:3]@[.299,.587,.114]
   assert luma[mid]>.09 and luma[0]-luma[mid]>.12,(i,luma[0],luma[mid])
  rows.append(dict(river=bool(i==1),snow_bank=bool(i==2),black_bank=bool(i>=3),scene_light=.2 if i==4 else 1.0,max_gpu_error=error,opaque_pixels=len(colour),deep_colour=colour[-1,:3].tolist()))
 r=dict(passed=True,hidden=True,source_sha256=hashlib.sha256(h.read_bytes()).hexdigest(),reports=rows);(a.output/'RESULTS.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
if __name__=='__main__':main()
