#!/usr/bin/env python3
"""Render the production shoreline shader with controlled physical bank heights.

Tests beach run-up, raised-bank exclusion and the narrow water-contact rim on the
actual GPU. Constant textures remove artistic noise, not shader branches.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess
import numpy as np

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True);p.add_argument('--runtime',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);raw=(a.source/'game/water_mod.h').read_text();blocks=[]
    for name in ['WATER_COAST_FLOW_GLSL','WATER_CONTACT_COVERAGE_GLSL','WATER_SWASH_FOAM_GLSL']:
        start=raw.index('#define '+name);blocks.append(raw[start:raw.index('\n\n',start)]+'\n')
    for name in ['SHORE_VS','SHORE_VS_RA','SHORE_FS']:
        start=raw.index('static const char* '+name+' =');end=raw.index('\n    "}\\n";',start)+len('\n    "}\\n";');blocks.append(raw[start:end]+'\n')
    code=r'''
#define GL_SILENCE_DEPRECATION 1
#include <SDL.h>
#include <OpenGL/gl.h>
#include <string>
#include <cstdio>
#include <cstdlib>
'''+''.join(blocks)+r'''
GLuint shader(GLenum type,const char*s){GLuint q=glCreateShader(type);glShaderSource(q,1,&s,0);glCompileShader(q);GLint ok;glGetShaderiv(q,GL_COMPILE_STATUS,&ok);if(!ok){char b[8192];glGetShaderInfoLog(q,sizeof b,0,b);puts(b);exit(5);}return q;}
int main(int argc,char**argv){SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP,"1");if(SDL_Init(SDL_INIT_VIDEO))return 2;SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,0);auto w=SDL_CreateWindow("hidden bank contact gate",0,0,256,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);if(!w)return 3;auto ctx=SDL_GL_CreateContext(w);if(!ctx)return 4;
glViewport(0,0,256,64);glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,1,0,1,-1,1);glMatrixMode(GL_MODELVIEW);glLoadIdentity();glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glClearColor(0,0,0,0);
GLuint tex[7];glGenTextures(7,tex);for(int i=0;i<7;i++){float c[4]={1,1,1,1};if(i==2){c[0]=.5f-.12f/32;c[1]=.5f;c[2]=1;c[3]=0;}if(i==3){c[0]=.3f;c[1]=.25f;c[2]=.2f;}if(i==5)c[3]=0;glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,tex[i]);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F_ARB,1,1,0,GL_RGBA,GL_FLOAT,c);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);}
const char* defs[]={"","#define WATER_PAINTED_BASE 1\n","#define WATER_PAINTED_BASE 1\n#define WATER_RA_BASE 1\n"};
for(int variant=0;variant<3;variant++){std::string fs(SHORE_FS);fs.replace(fs.find("%s"),2,defs[variant]);GLuint v=shader(GL_VERTEX_SHADER,variant?SHORE_VS_RA:SHORE_VS),f=shader(GL_FRAGMENT_SHADER,fs.c_str()),p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok)return 6;glUseProgram(p);
auto set=[&](const char*n,float x){glUniform1f(glGetUniformLocation(p,n),x);};
const char* samplers[]={"uW1","uW2","uField","uLand","uFoamTex","uOpaqueLand","uSheet"};for(int i=0;i<7;i++)glUniform1i(glGetUniformLocation(p,samplers[i]),i);
glUniform2f(glGetUniformLocation(p,"uFieldScale"),.01f,.01f);glUniform2f(glGetUniformLocation(p,"uViewport"),256,64);
set("uRange",16);set("uSwash",1);set("uSwashOpacity",1);set("uFoamCross",.3f);set("uFoamWidth",.2f);set("uWaveLen",1.6f);set("uWaveSpeed",.2f);set("uFade",1);set("uProductMean",1);set("uPan",.35f);set("uFlow",.25f);set("uFlowRate",.02f);set("uAlignLength",.35f);set("uAlignBlend",2);set("uBaseRepeat",1.92f);set("uBaseGain",1);
for(int kind=0;kind<4;kind++){
// Match the production plane: vLift.xy=0 and signed distance from the field,
// not the retired per-vertex vBank contact override. Native beach uses -.12.
float field[256*4];for(int x=0;x<256;x++){float sd=kind>=2?-.01f+.30f*(x+.5f)/256:-.12f;field[x*4]=.5f+sd/32;field[x*4+1]=.5f;field[x*4+2]=1;field[x*4+3]=0;}
glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,tex[2]);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F_ARB,256,1,0,GL_RGBA,GL_FLOAT,field);glUniform2f(glGetUniformLocation(p,"uFieldScale"),1,1);
float all[256]={0};for(int phase=0;phase<32;phase++){set("uTime",phase*.4f);set("uShorePlane",kind>=2?1:0);set("uSwash",kind==3?0:1);glClear(GL_COLOR_BUFFER_BIT);glColor4f(1,1,1,1);glBegin(GL_QUADS);for(int j=0;j<4;j++){float x=j==1||j==2?1:0,y=j>=2?1:0;float raised=kind==0?0:.3f;glMultiTexCoord3f(GL_TEXTURE6,0,kind>=2?0:raised,1);glMultiTexCoord3f(GL_TEXTURE7,kind>=2?x*.15f:0,raised,1);glVertex3f(x,y,0);}glEnd();float row[256*4];glReadPixels(0,32,256,1,GL_RGBA,GL_FLOAT,row);for(int x=0;x<256;x++)if(row[x*4+3]>all[x])all[x]=row[x*4+3];}char path[4096];snprintf(path,sizeof path,"%s/v%d-k%d.alpha",argv[1],variant,kind);FILE*out=fopen(path,"wb");fwrite(all,4,256,out);fclose(out);}glDeleteProgram(p);glDeleteShader(v);glDeleteShader(f);}
SDL_GL_DeleteContext(ctx);SDL_DestroyWindow(w);SDL_Quit();return 0;}
'''
    cpp=a.output/'bank-contact.cpp';cpp.write_text(code);exe=a.output/'bank-contact';flags=subprocess.check_output(['sdl2-config','--cflags'],text=True).split()
    subprocess.run(['clang++','-std=c++14',str(cpp),'-o',str(exe),*flags,str(a.runtime/'libSDL2-2.0.0.dylib'),'-framework','OpenGL'],check=True)
    subprocess.run([str(exe),str(a.output)],check=True,env={**os.environ,'SDL_MAC_BACKGROUND_APP':'1','DYLD_LIBRARY_PATH':str(a.runtime)})
    rows=[]
    for v in range(3):
        beach,raised,contact,disabled=[np.fromfile(a.output/f'v{v}-k{k}.alpha',dtype='f4')for k in range(4)]
        assert beach.min()>.05 and raised.max()==0,(v,beach.min(),raised.max())
        assert contact[:16].min()>.1 and contact[128:].max()==0,(v,contact[:16].min(),contact[128:].max())
        assert disabled.max()==0,(v,disabled.max())
        rows.append(dict(disabled_foam_max_alpha=float(disabled.max()),variant=v,low_beach_min_alpha=float(beach.min()),raised_beach_max_alpha=float(raised.max()),front_contact_min_alpha=float(contact[:16].min()),offshore_contact_max_alpha=float(contact[128:].max())))
    result=dict(passed=True,source_sha256=hashlib.sha256(raw.encode()).hexdigest(),hidden=True,cycle_samples=32,variants=rows,scope='Exact production GLSL; low/raised native beach and raised plane contact; constant textures, all row pixels and full cycle maxima')
    (a.output/'RESULTS.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
