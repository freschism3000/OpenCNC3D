#!/usr/bin/env python3
"""Check interpolated bank normals and local atlas albedo in production GLSL."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess
import numpy as np

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--runtime',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    raw=(a.source/'game/water_bed.h').read_text()
    def shader(start,end):
        text=raw[raw.index(start)+len(start):raw.index(end,raw.index(start))]
        return ''.join(json.loads(q)for q in re.findall(r'"(?:[^"\\]|\\.)*"',text))
    vs=shader('const char* vs=','const char* fs=');fs=shader('const char* fs=','p=water_link')
    code=r'''
#define GL_SILENCE_DEPRECATION 1
#include <SDL.h>
#include <OpenGL/gl.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
GLuint shader(GLenum type,const char*s){GLuint q=glCreateShader(type);glShaderSource(q,1,&s,0);glCompileShader(q);GLint ok;glGetShaderiv(q,GL_COMPILE_STATUS,&ok);if(!ok){char b[8192];glGetShaderInfoLog(q,sizeof b,0,b);puts(b);exit(5);}return q;}
int main(int argc,char**argv){SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP,"1");if(SDL_Init(SDL_INIT_VIDEO))return 2;SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,0);auto w=SDL_CreateWindow("hidden bank material gate",0,0,128,128,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);if(!w)return 3;auto ctx=SDL_GL_CreateContext(w);if(!ctx)return 4;
GLuint v=shader(GL_VERTEX_SHADER,VERTEX_SOURCE),f=shader(GL_FRAGMENT_SHADER,FRAGMENT_SOURCE),p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok)return 6;glUseProgram(p);glViewport(0,0,128,128);glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,1,0,1,-1,1);glMatrixMode(GL_MODELVIEW);glLoadIdentity();glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glColor3f(1,1,1);
GLuint tex[4];glGenTextures(4,tex);float colours[4][8]={{0,1,0,1,0,1,0,1},{0,0,1,1,0,0,1,1},{1,0,0,1,0,1,0,1},{0,0,.5f,1,.5f,0,.5f,1}};
for(int i=0;i<4;i++){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,tex[i]);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F_ARB,2,1,0,GL_RGBA,GL_FLOAT,colours[i]);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);}
const char* names[]={"uDirt","uLand","uAtlas","uCells"};for(int i=0;i<4;i++)glUniform1i(glGetUniformLocation(p,names[i]),i);glUniform2f(glGetUniformLocation(p,"uScale"),.5f,1);glUniform1f(glGetUniformLocation(p,"uHasDirt"),1);
const int dx[6]={0,0,1,1,0,1},dy[6]={0,1,0,0,1,1};
for(int k=0;k<4;k++){glUniform1f(glGetUniformLocation(p,"uNormalPass"),k==0?1:0);glUniform1f(glGetUniformLocation(p,"uHasAtlas"),k==3?0:1);glClear(GL_COLOR_BUFFER_BIT);glBegin(GL_TRIANGLES);for(int j=0;j<6;j++){float x=(float)dx[j],y=(float)dy[j],nx=.6f*x,nz=.6f*y,len=sqrtf(nx*nx+1+nz*nz);glNormal3f(nx/len,1/len,nz/len);glTexCoord3f(-1,1,0);glMultiTexCoord3f(GL_TEXTURE1,k==2?1.25f:.25f,0,.25f);glVertex3f(x,y,0);}glEnd();std::vector<float>b(128*128*4);glReadPixels(0,0,128,128,GL_RGBA,GL_FLOAT,b.data());char path[4096];snprintf(path,sizeof path,"%s/bank-%d.rgba",argv[1],k);FILE*out=fopen(path,"wb");fwrite(b.data(),4,b.size(),out);fclose(out);}SDL_GL_DeleteContext(ctx);SDL_DestroyWindow(w);SDL_Quit();return 0;}
'''.replace('VERTEX_SOURCE',json.dumps(vs)).replace('FRAGMENT_SOURCE',json.dumps(fs))
    cpp=a.output/'bank-material.cpp';cpp.write_text(code);exe=a.output/'bank-material';flags=subprocess.check_output(['sdl2-config','--cflags'],text=True).split()
    subprocess.run(['clang++','-std=c++14',str(cpp),'-o',str(exe),*flags,str(a.runtime/'libSDL2-2.0.0.dylib'),'-framework','OpenGL'],check=True)
    subprocess.run([str(exe),str(a.output)],check=True,env={**os.environ,'SDL_MAC_BACKGROUND_APP':'1','DYLD_LIBRARY_PATH':str(a.runtime)})
    y,x=np.indices((128,128));x=(x+.5)/128;y=(y+.5)/128
    corners=np.array([[0,1,0],[.6,1,0],[0,1,.6],[.6,1,.6]],float);corners/=np.linalg.norm(corners,axis=1)[:,None];aa,b,c,d=corners
    expected=np.where((x+y<=1)[...,None],aa+(b-aa)*x[...,None]+(c-aa)*y[...,None],d+(c-d)*(1-x[...,None])+(b-d)*(1-y[...,None]));expected/=np.linalg.norm(expected,axis=2)[...,None];expected=expected*.5+.5
    n=np.fromfile(a.output/'bank-0.rgba',dtype='f4').reshape(128,128,4)[...,:3];error=float(abs(n-expected).max());assert error<.0021,error
    jump=max(float(abs(np.diff(n,axis=0)).max()),float(abs(np.diff(n,axis=1)).max()));assert jump<.008,jump
    # A constant triangle-face normal cannot satisfy this curved shared-node field.
    negative=float(abs(expected-np.array([.5,.5,1])).max());assert negative>.3
    for k,colour in [(1,[1,0,0]),(2,[0,1,0]),(3,[0,0,1])]:
        pixels=np.fromfile(a.output/f'bank-{k}.rgba',dtype='f4').reshape(128,128,4);assert np.max(abs(pixels[...,:3]-colour))<.0021,(k,pixels[...,:3].mean((0,1)))
    result=dict(passed=True,source_sha256=hashlib.sha256(raw.encode()).hexdigest(),hidden=True,normal_max_error=error,normal_max_pixel_step=jump,face_normal_negative_control_error=negative,local_atlas_cells=2,missing_atlas_fallback=True,scope='Exact production bank GLSL, every GPU pixel; independent barycentric shared-node normal field and two distinct source atlas cells')
    (a.output/'RESULTS.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
