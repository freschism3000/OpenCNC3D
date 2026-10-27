"""G322 controlled apron calibration using the actual source loader and GLSL.

The six calibration comparisons retain the original three-level bound. A separate
native grey-road witness exercises the old pink fault. World captures in
 gate_bibhue.py independently test the current terrain, lighting and post chain.
"""
from pathlib import Path
import argparse, hashlib, json, os, shlex, shutil, struct, subprocess, sys
import numpy as np
from PIL import Image, ImageFilter

HEAD = '#define GL_GLEXT_PROTOTYPES 1\n#define GL_SILENCE_DEPRECATION\n#include <SDL.h>\n#include <SDL_opengl.h>\n#include <vector>\n#include <cmath>\n#include <cstdio>\n#include <cstring>\n#include <algorithm>\n#include <string>\n#include <cassert>\nstatic int fx_filter_mode(int v){return v;}\nstatic void fx_filter_note(GLuint,int){}\n'
BODY = '\nstatic const int W=256,H=128;\nstatic GLuint tex(const std::vector<unsigned char>&v){GLuint t;glGenTextures(1,&t);glBindTexture(GL_TEXTURE_2D,t);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,v.data());return t;}\nstatic void quad(float u,float v){glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2i(0,0);glTexCoord2f(u,0);glVertex2i(W,0);glTexCoord2f(u,v);glVertex2i(W,H);glTexCoord2f(0,v);glVertex2i(0,H);glEnd();}\nstatic GLuint shader(GLenum type,const char*s){GLuint sh=glCreateShader(type);glShaderSource(sh,1,&s,nullptr);glCompileShader(sh);GLint ok=0;glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);if(!ok){char msg[4096];glGetShaderInfoLog(sh,sizeof msg,nullptr,msg);fprintf(stderr,"%s\\n",msg);abort();}return sh;}\nstatic std::vector<unsigned char> draw(const BibEnhTheater&T,const BibEnhApron&A,const int*rgb,GLuint prog){\n std::vector<unsigned char> ground(W*H*4);for(size_t i=0;i<ground.size();i+=4){for(int j=0;j<3;j++)ground[i+j]=rgb[j];ground[i+3]=255;}GLuint scene=tex(ground);\n glClearColor(rgb[0]/255.f,rgb[1]/255.f,rgb[2]/255.f,1);glClear(GL_COLOR_BUFFER_BIT);glColor4f(1,1,1,1);\n const float u=float(A.w)/A.texw,v=float(A.h)/A.texh;\n if(!prog){glUseProgram(0);glActiveTexture(GL_TEXTURE0);glEnable(GL_TEXTURE_2D);glEnable(GL_BLEND);glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,0);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glBindTexture(GL_TEXTURE_2D,A.cover);quad(u,v);glBindTexture(GL_TEXTURE_2D,A.mult);glBlendFunc(GL_DST_COLOR,GL_SRC_COLOR);quad(u,v);quad(u,v);}\n else{glUseProgram(prog);glDisable(GL_BLEND);glDisable(GL_ALPHA_TEST);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,A.cover);glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,A.mult);glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,scene);glActiveTexture(GL_TEXTURE0);glUniform1i(glGetUniformLocation(prog,"uCover"),0);glUniform1i(glGetUniformLocation(prog,"uMult"),1);glUniform1i(glGetUniformLocation(prog,"uScene"),2);glUniform4f(glGetUniformLocation(prog,"uVp"),0,0,W,H);glUniform3fv(glGetUniformLocation(prog,"uGround"),1,T.ground);float d[3];float l=.299f*A.mean[0]+.587f*A.mean[1]+.114f*A.mean[2];for(int c=0;c<3;c++)d[c]=A.mean[c]/l;glUniform3fv(glGetUniformLocation(prog,"uDirt"),1,d);quad(u,v);}\n std::vector<unsigned char> pixels(W*H*4);glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());glUseProgram(0);glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,0);glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,0);glActiveTexture(GL_TEXTURE0);glDeleteTextures(1,&scene);assert(glGetError()==GL_NO_ERROR);return pixels;\n}\nstatic void save(const std::string&p,const std::vector<unsigned char>&v){FILE*f=fopen(p.c_str(),"wb");assert(f);fprintf(f,"P6\\n%d %d\\n255\\n",W,H);for(int y=H-1;y>=0;y--)for(int x=0;x<W;x++)assert(fwrite(&v[(y*W+x)*4],1,3,f)==3);assert(!fclose(f));}\nint main(int argc,char**argv){assert(argc==3);assert(!SDL_Init(SDL_INIT_VIDEO));SDL_Window*w=SDL_CreateWindow("Apron shader gate",0,0,W,H,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);assert(w);SDL_GLContext ctx=SDL_GL_CreateContext(w);assert(ctx);glViewport(0,0,W,H);glDisable(GL_DITHER);glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,W,H,0,-1,1);glMatrixMode(GL_MODELVIEW);glLoadIdentity();glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_LIGHTING);glEnable(GL_TEXTURE_2D);\n assert(bibenh_load(argv[1]));bibenh_build(1,.5f);GLuint prog=glCreateProgram();GLuint vs=shader(GL_VERTEX_SHADER,BIBENH_VS),fs=shader(GL_FRAGMENT_SHADER,BIBENH_FS);glAttachShader(prog,vs);glAttachShader(prog,fs);glLinkProgram(prog);GLint ok=0;glGetProgramiv(prog,GL_LINK_STATUS,&ok);assert(ok);\n for(size_t t=0;t<g_beTh.size();t++)for(int b=0;b<3;b++){const BibEnhTheater&T=g_beTh[t];const BibEnhApron&A=T.ap[b];int calibration[3];for(int c=0;c<3;c++)calibration[c]=(int)floorf(T.ground[c]*.20f*255+.5f);int road[3]={56,51,42};for(int kind=0;kind<2;kind++){const int*rgb=kind?road:calibration;std::string stem=std::string(argv[2])+"/t"+std::to_string(t)+"-b"+std::to_string(b)+"-"+(kind?"road":"calibration");save(stem+"-old.ppm",draw(T,A,rgb,0));save(stem+"-new.ppm",draw(T,A,rgb,prog));printf("CASE|%zu|%d|%s|ground=%d,%d,%d\\n",t,b,kind?"road":"calibration",rgb[0],rgb[1],rgb[2]);}}\n glDeleteProgram(prog);glDeleteShader(vs);glDeleteShader(fs);bibenh_free();SDL_GL_DeleteContext(ctx);SDL_DestroyWindow(w);SDL_Quit();return 0;}\n'

def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def measure(output, pack):
    rows = []
    with Path(pack).open('rb') as stream:
        def take(n):
            data = stream.read(n)
            if len(data) != n:
                raise ValueError('calibration pack ends early')
            return data
        if take(8) != b'BIBENH4\0':
            raise ValueError('calibration requires the version-4 ground record')
        version, count, cell = struct.unpack('<III', take(12))
        if version != 4 or count != 2 or not 8 <= cell <= 256:
            raise ValueError('unexpected calibration theater/texel contract')
        names = []
        for theater in range(count):
            name = take(12).split(b'\0')[0].decode('ascii')
            names.append(name)
            take(4 + 12)  # source theater id and multiply ratio
            ground = np.array(struct.unpack('<3f', take(12)))
            if not np.isfinite(ground).all() or not (ground > 0).all():
                raise ValueError('invalid calibration ground')
            ground /= np.dot(ground, [.299, .587, .114])
            for apron in range(3):
                wc, hc = struct.unpack('<II', take(8))
                if not 1 <= wc <= 4 or not 1 <= hc <= 4:
                    raise ValueError('invalid apron dimensions')
                rgba = np.frombuffer(take(wc * hc * cell * cell * 4), dtype=np.uint8).reshape(hc * cell, wc * cell, 4)
                alpha = np.array(Image.fromarray(rgba[..., 3]).resize((256, 128), Image.Resampling.NEAREST))
                core = np.array(Image.fromarray(np.uint8(alpha > 200) * 255).filter(ImageFilter.MinFilter(5))) > 127
                outside = alpha == 0
                if core.sum() < 1000 or not outside.any():
                    raise ValueError('native apron has no measured core/transparent exterior')
                for kind in ('calibration', 'road'):
                    stem = Path(output) / ('t%d-b%d-%s' % (theater, apron, kind))
                    images = [np.asarray(Image.open(str(stem) + '-' + arm + '.ppm').convert('RGB')).astype(np.int32) for arm in ('old', 'new')]
                    old, new = images
                    if any(image.shape != (128, 256, 3) for image in images):
                        raise ValueError('native calibration readback has wrong dimensions')
                    background = np.array([56, 51, 42]) if kind == 'road' else np.floor(ground * .20 * 255 + .5).astype(int)
                    mean_old, mean_new = old[core].mean(0), new[core].mean(0)
                    rgb = old.astype(float) / 255
                    mx, mn = rgb.max(2), rgb.min(2)
                    delta = np.maximum(mx - mn, 1e-9)
                    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
                    hue = np.where(mx == r, 60 * np.mod((g-b)/delta, 6), np.where(mx == g, 60*((b-r)/delta+2), 60*((r-g)/delta+4)))
                    pink = (mx >= .10) & ((mx-mn)/np.maximum(mx, 1e-9) >= .12) & ((hue < 22) | (hue >= 330))
                    rr, gg, bb = mean_old
                    hi, lo = max(mean_old), min(mean_old)
                    old_hue = (60*((gg-bb)/(hi-lo))) % 360 if hi == rr else 60*((bb-rr)/(hi-lo)+2) if hi == gg else 60*((rr-gg)/(hi-lo)+4)
                    row = dict(theater=name, apron=apron+1, kind=kind, core=int(core.sum()), mean_delta_max=float(abs(mean_new-mean_old).max()), old_pink_fraction=float((pink & core).sum()/core.sum()), old_hue=float(old_hue), outside_max_error=max(int(abs(image[outside]-background).max()) for image in images))
                    rows.append(row)
        if stream.read(1) or set(names) != {'TEMPERATE', 'DESERT'}:
            raise ValueError('unexpected calibration theater names/trailing bytes')
    errors = []
    calibration = [r for r in rows if r['kind'] == 'calibration']
    witness = [r for r in rows if r['kind'] == 'road' and r['theater'] == 'TEMPERATE']
    if len(calibration) != 6 or any(r['mean_delta_max'] > 3 for r in calibration):
        errors.append('native six-calibration mean differs by more than 3 levels')
    if len(witness) != 3 or any(r['old_pink_fraction'] < .10 or r['old_hue'] >= 22 for r in witness):
        errors.append('native old grey-road drawing does not witness >=10% pink/hue<22')
    if any(r['outside_max_error'] != 0 for r in rows):
        errors.append('native drawing altered transparent exterior')
    return rows, errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--pack', type=Path, default=Path('bibenh.pack'))
    parser.add_argument('--prepare-only', action='store_true')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    # The gate is copied to playable/ by the build; the measured shader stays in game/.
    source = Path(__file__).resolve().parent.parent / 'game' / 'bibenh_mod.h'
    text = source.read_text()
    native = text[text.index('struct BibEnhApron {'):text.index('static GLuint g_beProg = 0;')]
    cpp = output / 'probe.cpp'
    cpp.write_text(HEAD + native + BODY)
    executable = output / 'probe'
    flags = shlex.split(subprocess.check_output(['sdl2-config', '--cflags'], text=True))
    if sys.platform == 'darwin':
        # Link the same shipped universal SDL shim as the tested renderer.
        library = Path.cwd() / 'libSDL2-2.0.0.dylib'
        if not library.is_file():
            raise ValueError('the tested runtime SDL shim is missing')
        graphics = [str(library), '-framework', 'Cocoa', '-framework', 'OpenGL']
    else:
        graphics = shlex.split(subprocess.check_output(['sdl2-config', '--libs'], text=True)) + ['-lGL']
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11', '-O2', '-Wno-deprecated-declarations', str(cpp), '-o', str(executable)] + flags + graphics, check=True)
    # The project's SDL shim uses executable-relative names on macOS.
    if sys.platform == 'darwin':
        for name in ('libSDL2-2.0.0.dylib', 'libSDL3.dylib'):
            library = Path.cwd() / name
            if library.is_file():
                shutil.copy2(library, output / name)
    if args.prepare_only:
        print('CALIBRATION|prepared|source=' + sha(source))
        return
    env = dict(os.environ, OPENCNC3D_BACKGROUND='1')
    run = subprocess.run([str(executable), str(args.pack.resolve()), str(output)], env=env, capture_output=True, text=True)
    (output / 'probe.log').write_text(run.stdout + run.stderr)
    if run.returncode:
        raise RuntimeError('hidden calibration renderer failed: %d' % run.returncode)
    rows, errors = measure(output, args.pack)
    result = dict(passed=not errors, source_sha256=sha(source), pack_sha256=sha(args.pack), helper_sha256=sha(__file__), generated_source_sha256=sha(cpp), executable_sha256=sha(executable), rows=rows, errors=errors)
    temporary = output / 'RESULTS.json.tmp'
    temporary.write_text(json.dumps(result, indent=2) + '\n')
    temporary.replace(output / 'RESULTS.json')
    print(('1' if not errors else '0') + ' native calibration: ' + ('six means within3, old grey-road pink witness, transparent exterior exact' if not errors else '; '.join(errors)))
    if errors:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
