#!/usr/bin/env python3
"""Compile the actual particle mask writer and grade-selection expression.

The isolation texture is metadata, red=explosion and green=muzzle, rather than
colour. A colour control must change visible light without rerouting this metadata.
No SDL/GL context is created; glColor4f is captured as the writer's output.
"""
from pathlib import Path
import argparse,re,subprocess,tempfile

ROOT=Path(__file__).resolve().parents[1]

def function(source,name):
    start=source.index('static void '+name+'(');a=source.index('{',start);depth=0
    for i in range(a,len(source)):
        if source[i]=='{':depth+=1
        if source[i]=='}':
            depth-=1
            if not depth:return source[start:i+1]
    raise AssertionError('unterminated production function')

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--negative-control',action='store_true');args=parser.parse_args()
    source=(ROOT/'game/effects_mod.h').read_text();writer=function(source,'efx_particle_colour')
    draw=source[source.index('static void efx_draw_particles('):]
    match=re.search(r'const bool graded\s*=\s*(.*?);',draw);assert match,'missing production grade selection'
    expression=match[1]
    # The replacement is deliberately the rejected implementation, proving this
    # check distinguishes grade leakage into the semantic texture.
    if args.negative_control:expression='efx_grade_begin()'
    code=r'''
#include "fx_state.h"
#include <assert.h>
#include <stdio.h>
static bool g_efxEnhOn=true;
struct EfxPart { unsigned char r,g,b; bool explosionBloom,muzzleFlash; };
static float captured[4];
static void glColor4f(float r,float g,float b,float a)
{ captured[0]=r;captured[1]=g;captured[2]=b;captured[3]=a; }
static int calls=0;
static bool efx_grade_begin(){calls++;return true;}
'''+writer+'\nstatic bool production_grade_selection(bool explosionOnly) { return '+expression+'; }\n'+r'''
int main()
{
    assert(production_grade_selection(false)&&calls==1);
    calls=0;assert(!production_grade_selection(true)&&calls==0);
    fx_defaults(&g_fx);
    const EfxPart explosion={64,200,96,true,false},muzzle={64,200,96,false,true};
    const EfxPart both={64,200,96,true,true};
    for(int setting=0;setting<11;setting++) {
        fx_defaults(&g_fx);
        switch(setting) {
        case 0:g_fx.efx_sat=0;break;case 1:g_fx.efx_red=0;break;
        case 2:g_fx.efx_green=0;break;case 3:g_fx.efx_blue=0;break;
        case 4:g_fx.efx_warmth=1;break;case 5:g_fx.efx_tint=1;break;
        case 6:g_fx.efx_bright=1.6f;break;case 7:g_fx.efx_exposure=1;break;
        case 8:g_fx.efx_gamma=.4f;break;case 9:g_fx.efx_contrast=2;break;
        default:break;
        }
        const float peak=200.0f/255.0f;
        efx_particle_colour(explosion,.37f,true);
        assert(captured[0]==peak&&captured[1]==0&&captured[2]==0&&captured[3]==.37f);
        efx_particle_colour(muzzle,.37f,true);
        assert(captured[0]==0&&captured[1]==peak&&captured[2]==0&&captured[3]==.37f);
        efx_particle_colour(both,.37f,true);
        assert(captured[0]==peak&&captured[1]==peak&&captured[2]==0&&captured[3]==.37f);
    }
    puts("PASS: production bloom mask retains red/green ownership and alpha under every Effects grade; visible particles still select grading");
}
'''
    with tempfile.TemporaryDirectory(prefix='cnc3d-bloom-mask-',dir='/private/tmp') as temp:
        temp=Path(temp);src=temp/'gate.cpp';binary=temp/'gate';src.write_text(code)
        subprocess.run(['clang++','-std=c++14','-Wno-null-pointer-subtraction','-Wno-unused-function','-I'+str(ROOT/'game'),str(src),'-o',str(binary)],check=True)
        result=subprocess.run([str(binary)],capture_output=True,text=True)
        if args.negative_control:
            assert result.returncode!=0,'negative control unexpectedly passed';print('PASS: rejected full-RGB grading of the semantic bloom target')
        else:
            assert result.returncode==0,result.stderr;print(result.stdout.strip())

if __name__=='__main__':main()
