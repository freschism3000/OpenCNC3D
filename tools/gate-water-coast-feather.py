#!/usr/bin/env python3
"""Check native coast compositing, independently of shore-band brightness.

The hidden GPU fixture compiles the production water shaders and native GL alpha
blend, then exercises the stage order read from both production frontends. A
coarse stepped bank with a continuous partial-alpha fringe must reproduce the
independent equation bank*A + live_water*(1-A), once. Former late-water erasure,
four-ring substitution and doubled terrain are required failing controls.
This isolates compositing; the installed native/depth/material gates still apply.
"""

from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess

import numpy as np


def sha(data):
    return hashlib.sha256(data).hexdigest()


def bank_distance(solid, radius):
    """Exact Euclidean distance inside this finite test's feather support."""
    h, w = solid.shape
    out = np.full(solid.shape, float(radius))
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            distance = (dx * dx + dy * dy) ** .5
            if distance >= radius:
                continue
            y0, y1 = max(0, -dy), min(h, h - dy)
            x0, x1 = max(0, -dx), min(w, w - dx)
            target = out[y0:y1, x0:x1]
            np.minimum(target, np.where(solid[y0 + dy:y1 + dy, x0 + dx:x1 + dx],
                                        distance, radius), out=target)
    return out


def uncomment(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)


def function(text, name):
    """Read a named C++ function body; comments cannot supply false call evidence."""
    text = uncomment(text)
    match = re.search(r'\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{', text)
    if not match:
        raise ValueError('Cannot find production function ' + name)
    start = match.end()
    depth = 1
    for i in range(start, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[start:i]
    raise ValueError('Unclosed production function ' + name)


def production_order(source):
    td = function((source / 'game/cnc_eyes.cpp').read_text(), 'draw_frame')
    terrain = function((source / 'game/cnc_eyes.cpp').read_text(), 'draw_terrain')
    ra = function((source / 'game/ra3d_terrain.h').read_text(), 'ra3d_world_ground')
    td_start = td.index('draw_seabed();', td.index('ra3d_world_ground();'))
    td = td[td_start:td.index('water_shore_draw();', td_start)]
    ra_start = ra.index('ra3d_cache_frame();')
    ra = ra[ra_start:ra.index('water_shore_draw();', ra_start)]
    td_compact, terrain_compact, ra_compact = [re.sub(r'\s+', '', s) for s in (td, terrain, ra)]
    td_order = ('native', 'water')
    ra_order = ('native', 'water')
    td_once = ('draw_terrain(!coastColourDeferred);' in td_compact
               and 'if(colour)draw_terrain_layer(false);' in terrain_compact
               and td_compact.count('draw_terrain_layer(false);') == 1
               and td_compact.index('water_fx_draw_after_terrain();')
               < td_compact.index('draw_terrain_layer(false);')
               and 'glDepthMask(GL_FALSE);draw_terrain_layer(false);' in td_compact)
    if td_once:
        td_order = ('water', 'native')
    ra_once = ('ra3d_draw_ground(true,true,true);' in ra_compact
               and 'ra3d_draw_ground(sea,true,false,true,coastColourDeferred);' in ra_compact
               and ra_compact.index('water_fx_draw_after_terrain();')
               < ra_compact.index('ra3d_draw_ground(sea,true,false,true,coastColourDeferred);')
               and ra_compact.count('ra3d_draw_seams();') == 1
               and ra_compact.count('ra3d_draw_road_joins();') == 1
               and ra_compact.index('ra3d_draw_road_joins();')
               > ra_compact.index('ra3d_draw_seams();'))
    if ra_once:
        ra_order = ('water', 'native')
    return [td_order, td_order, ra_order], dict(
        td_native_colour_once_after_water=td_once,
        ra_native_colour_once_after_water=ra_once,
        stage_bodies_sha256=dict(td=sha(td.encode()), ra=sha(ra.encode())))


def declaration(raw, name):
    start = raw.index('static const char* ' + name + ' =')
    end = raw.index('\n    "}\\n";', start) + len('\n    "}\\n";')
    return raw[start:end] + '\n'


def macro(raw, name):
    start = raw.index('#define ' + name)
    return raw[start:raw.index('\n\n', start)] + '\n'


GPU = r'''
#define GL_SILENCE_DEPRECATION 1
#include <SDL.h>
#include <OpenGL/gl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "RAIN_HEADER"
#include "BED_HEADER"
SHADERS
static const int W=256,H=128;
GLuint shader(GLenum type,const char*s){GLuint q=glCreateShader(type);glShaderSource(q,1,&s,0);glCompileShader(q);GLint ok;glGetShaderiv(q,GL_COMPILE_STATUS,&ok);if(!ok){char b[8192];glGetShaderInfoLog(q,sizeof b,0,b);puts(b);exit(5);}return q;}
GLuint program(const char*v,const char*f){GLuint vs=shader(GL_VERTEX_SHADER,v),fs=shader(GL_FRAGMENT_SHADER,f),p=glCreateProgram();glAttachShader(p,vs);glAttachShader(p,fs);glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){char b[8192];glGetProgramInfoLog(p,sizeof b,0,b);puts(b);exit(6);}glDeleteShader(vs);glDeleteShader(fs);return p;}
void screen(){glBegin(GL_QUADS);glVertex2f(-1,-1);glVertex2f(1,-1);glVertex2f(1,1);glVertex2f(-1,1);glEnd();}
void surface(){glColor4f(1,1,1,1);glBegin(GL_QUADS);for(int j=0;j<4;j++){float x=j==1||j==2?4:0,z=j>=2?2:0;glMultiTexCoord2f(GL_TEXTURE0,x,z);glMultiTexCoord2f(GL_TEXTURE1,x,z);glMultiTexCoord3f(GL_TEXTURE2,0,0,0);glMultiTexCoord3f(GL_TEXTURE6,1,0,0);glVertex3f(x,0,z);}glEnd();}
void output(const char*dir,int v,int k){float data[W*H*4];glFinish();glReadPixels(0,0,W,H,GL_RGBA,GL_FLOAT,data);char path[4096];snprintf(path,sizeof path,"%s/v%d-k%d.rgba",dir,v,k);FILE*f=fopen(path,"wb");if(!f)exit(9);fwrite(data,4,W*H*4,f);fclose(f);}
int main(int argc,char**argv){SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP,"1");if(SDL_Init(SDL_INIT_VIDEO))return 2;SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,0);auto w=SDL_CreateWindow("hidden native coast feather check",0,0,W,H,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);if(!w)return 3;auto ctx=SDL_GL_CreateContext(w);if(!ctx)return 4;glViewport(0,0,W,H);glDisable(GL_DEPTH_TEST);glDisable(GL_DITHER);glClearColor(0,0,0,1);
GLuint tex[9];glGenTextures(9,tex);for(int i=0;i<9;i++){glActiveTexture(GL_TEXTURE0+(i<8?i:0));glBindTexture(GL_TEXTURE_2D,tex[i]);float q[4]={.22f,.35f,.48f,1};if(i==1){q[0]=q[1]=q[2]=1;}if(i==2){q[0]=.5f+.25f/32;q[1]=.5f;q[2]=1;q[3]=0;}if(i==3){q[0]=.68f;q[1]=.44f;q[2]=.24f;}if(i==4||i==5){q[0]=q[1]=.5f;q[2]=1;q[3]=0;}if(i>=7){float data[W*H*4];char path[4096];snprintf(path,sizeof path,"%s/%s.rgba",argv[1],i==7?"opaque":"native");FILE*f=fopen(path,"rb");if(!f)return 8;fread(data,4,W*H*4,f);fclose(f);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F_ARB,W,H,0,GL_RGBA,GL_FLOAT,data);}else glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F_ARB,1,1,0,GL_RGBA,GL_FLOAT,q);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,i==7?GL_LINEAR:GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,i==7?GL_LINEAR:GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);}
const char* nativeVS="#version 120\nvoid main(){gl_Position=gl_Vertex;}";const char* nativeFS="#version 120\nuniform sampler2D native;void main(){gl_FragColor=texture2D(native,gl_FragCoord.xy/vec2(256,128));}";GLuint bank=program(nativeVS,nativeFS);glUseProgram(bank);glUniform1i(glGetUniformLocation(bank,"native"),0);
const char* defs[]={"","#define WATER_PAINTED_BASE 1\n","#define WATER_PAINTED_BASE 1\n#define WATER_RA_BASE 1\n"};const int sourceOrder[3]={ORDERS};
for(int v=0;v<3;v++)for(int mode=0;mode<2;mode++){std::string body=std::string(defs[v])+WATER_FS;if(mode){const std::string old="surfaceCoverage=texture2D(uOpaqueLand,(floor(gl_FragCoord.xy)+0.5)/uViewport).a>0.5?0.0:1.0;";size_t at=body.find(old);if(at!=std::string::npos)body.replace(at,old.size(),"surfaceCoverage=waterProjectedCoverage(vWorld.xz,sd);");}char* joined=rain_glsl_source(body.c_str());GLuint water=program(WATER_VS,joined);free(joined);glUseProgram(water);auto set=[&](const char*n,float x){glUniform1f(glGetUniformLocation(water,n),x);};const char* samplers[]={"uW1","uW2","uField","uLand","uWave","uNormal","uScene","uOpaqueLand"};for(int i=0;i<8;i++)glUniform1i(glGetUniformLocation(water,samplers[i]),i);glUniform2f(glGetUniformLocation(water,"uFieldScale"),.25f,.5f);glUniform2f(glGetUniformLocation(water,"uViewport"),W,H);glUniform2f(glGetUniformLocation(water,"uCurrent"),0,1);glUniform3f(glGetUniformLocation(water,"uCamPos"),2,4,3);glUniform3f(glGetUniformLocation(water,"uSunDir"),0,1,0);glUniform3f(glGetUniformLocation(water,"uReflectionDir"),0,1,0);set("uRange",16);set("uAlpha",.7f);set("uFoamWidth",.4f);set("uFadeWidth",1);set("uProductMean",1);set("uWaveLen",1);set("uBaseScale",1);set("uBaseGain",1);set("uAlignLength",.35f);set("uAlignBlend",2);set("uRainRippleScale",1);set("uRainRippleRate",1);glUniform2f(glGetUniformLocation(water,"uWaveTexel"),.1f,.1f);
for(int k=0;k<5;k++){if(mode&&k!=4)continue;if(!mode&&k==4)continue;glClear(GL_COLOR_BUFFER_BIT);auto paintWater=[&](bool late){for(int i=0;i<8;i++){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,tex[i]);}glUseProgram(water);set("uLateSurface",late?1:0);glDisable(GL_BLEND);glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,4,0,2,-8,8);glMatrixMode(GL_MODELVIEW);glLoadIdentity();glRotatef(-90,1,0,0);surface();};auto paintBank=[&](){glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,tex[8]);glUseProgram(bank);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);screen();glDisable(GL_BLEND);};if(k==0)paintWater(false);else if(k==1){if(sourceOrder[v]){paintWater(true);paintBank();}else{paintBank();paintWater(true);}}else if(k==2){paintBank();paintWater(true);}else if(k==3){paintWater(true);paintBank();paintBank();}else{paintWater(true);paintBank();}output(argv[1],v,k);}glDeleteProgram(water);}
SDL_GL_DeleteContext(ctx);SDL_DestroyWindow(w);SDL_Quit();return 0;}
'''


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--runtime', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--prepare-only', action='store_true', help='Write/compile fixture without using the GPU')
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    raw = (args.source / 'game/water_mod.h').read_text()
    orders, structural = production_order(args.source)
    water_fs = declaration(raw, 'WATER_FS')
    structural['water_respects_exact_owner_without_replacing_native_feather'] = (
        'surfaceCoverage=waterProjectedCoverage(vWorld.xz,sd);' not in water_fs
        and 'texture2D(uOpaqueLand,(floor(gl_FragCoord.xy)+0.5)/uViewport)' in water_fs)
    shaders = ''.join(macro(raw, n) for n in ['WATER_CONTACT_COVERAGE_GLSL', 'WATER_COAST_FLOW_GLSL'])
    shaders += declaration(raw, 'WATER_VS') + water_fs
    yy, xx = np.indices((128, 256))
    # Native 16-pixel steps plus a concave notch and an isolated bank island.
    edge = 80 + ((yy // 16) % 4) * 12
    solid = xx < edge
    solid[40:88, 45:100] = False
    solid |= (xx >= 172) & (xx < 204) & (yy >= 48) & (yy < 80)
    distance = bank_distance(solid, 14)
    alpha = np.clip(1 - distance / 14, 0, 1)
    alpha = alpha * alpha * (3 - 2 * alpha)
    native = np.zeros((128, 256, 4), dtype='f4')
    native[:, :, :3] = [.68, .44, .24]
    native[:, :, 3] = alpha
    opaque = np.zeros_like(native)
    opaque[:, :, 3] = solid
    native.tofile(args.output / 'native.rgba')
    opaque.tofile(args.output / 'opaque.rgba')
    code = GPU.replace('RAIN_HEADER', str((args.source / 'game/rain_glsl.h').resolve()))
    code = code.replace('BED_HEADER', str((args.source / 'game/water_virtual_bed.h').resolve()))
    code = code.replace('SHADERS', shaders).replace('ORDERS', ','.join(str(int(o[0] == 'water')) for o in orders))
    cpp, exe = args.output / 'coast-feather.cpp', args.output / 'coast-feather'
    cpp.write_text(code)
    flags = subprocess.check_output(['sdl2-config', '--cflags'], text=True).split()
    subprocess.run(['clang++', '-std=c++14', str(cpp), '-o', str(exe), *flags,
                    str(args.runtime / 'libSDL2-2.0.0.dylib'), '-framework', 'OpenGL'], check=True)
    provenance = dict(hidden=True, source_sha256=sha(raw.encode()), fixture_sha256=sha(code.encode()),
                      native_matte_sha256=sha(native.tobytes()), approved_depth_sha256=sha((args.source / 'game/water_virtual_bed.h').read_bytes()),
                      structural=structural, observed_order=orders)
    (args.output / 'PREPARED.json').write_text(json.dumps(provenance, indent=2) + '\n')
    if args.prepare_only:
        print(json.dumps(dict(prepared=True, gpu_run=False, **provenance), indent=2))
        return
    subprocess.run([str(exe), str(args.output)], check=True,
                   env={**os.environ, 'SDL_MAC_BACKGROUND_APP': '1', 'DYLD_LIBRARY_PATH': str(args.runtime)})
    reports = []
    failures = []
    fringe = (alpha > .02) & (alpha < .98)
    for v in range(3):
        rows = [np.fromfile(args.output / f'v{v}-k{k}.rgba', dtype='f4').reshape(128, 256, 4)[:, :, :3]
                for k in range(5)]
        base = rows[0]
        expected = native[:, :, :3] * alpha[:, :, None] + base * (1 - alpha[:, :, None])
        metrics = []
        for observed in rows[1:]:
            delta = observed - expected
            error = float(abs(delta[fringe]).max())
            # A residual derivative catches hard replacements without a luma threshold.
            step = max(float(abs(np.diff(delta, axis=0)).max()), float(abs(np.diff(delta, axis=1)).max()))
            metrics.append(dict(max_partial_rgb_error=error, max_residual_step=step))
        positive = metrics[0]
        if positive['max_partial_rgb_error'] > .009 or positive['max_residual_step'] > .014:
            failures.append(f'variant {v}: native feather compositing is discontinuous')
        for name, metric in zip(['erased_native', 'doubled_native', 'four_ring_substitute'], metrics[1:]):
            if metric['max_partial_rgb_error'] <= .009 and metric['max_residual_step'] <= .014:
                failures.append(f'variant {v}: {name} negative control was not detected')
        reports.append(dict(variant=v, partial_pixels=int(fringe.sum()), native_once=positive,
                            negative_controls=dict(zip(['erased_native', 'doubled_native', 'four_ring_substitute'], metrics[1:]))))
    if not all(value for key, value in structural.items() if key != 'stage_bodies_sha256'):
        failures.append('production ordering/ownership structural audit failed')
    result = dict(passed=not failures, **provenance, reports=reports, failures=failures,
                  scope='Exact production water GLSL; stepped projected bank/native alpha; native SRC_ALPHA blend; all RGB pixels and derivative residuals; source-stage-order audit. Does not replace installed material/depth checks.')
    (args.output / 'RESULTS.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['passed'] else 1)


if __name__ == '__main__':
    main()
