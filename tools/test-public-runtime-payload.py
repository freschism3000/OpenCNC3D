#!/usr/bin/env python3
"""CPU proof for fresh-public reductions and the actual SAND material loader."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import sys
import tempfile
from unittest.mock import patch
import zipfile
import zlib

ROOT=Path(__file__).resolve().parents[1]
def load(path,name):
    spec=importlib.util.spec_from_file_location(name,path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
def sha(path):
    with path.open('rb')as f:return hashlib.file_digest(f,'sha256').hexdigest()
def main():
    if not __debug__:raise RuntimeError('Assertions are required')
    sys.dont_write_bytecode=True
    helper=load(ROOT/'tools/launcher/prepare-public-runtime.py','public_stage')
    policy=load(ROOT/'tools/launcher/package-modules.py','public_modules')
    work=Path(tempfile.mkdtemp(prefix='public-runtime-contract-')).resolve()
    (work/'.cnc3d-cleanup.json').write_text(json.dumps({'kind':'completed-generated-staging','preserve_unique_authoring':True})+'\n')
    key=b'S1'.ljust(16,b'\0')+struct.pack('<HHBBH',1,0,0,0,0)
    surface=b'C3DSURF\0'+struct.pack('<III',1,8,1)+key+bytes(64)
    def material(value=100,keys=key):
        # Materials intentionally have a different resolution from physical cells.
        pixels=bytes([value,128,210,255])*144;compressed=zlib.compress(pixels,9)
        return b'C3DMATL\0'+struct.pack('<III',1,12,1)+keys+struct.pack('<I',len(compressed))+compressed
    def stage(name,marked=True):
        path=work/name;path.mkdir(parents=True)
        if marked:(path/helper.MARKER).write_bytes(helper.MARKER_BYTES)
        for folder in helper.AUTHORING:
            (path/folder).mkdir();(path/folder/'retained-source-copy.png').write_bytes(b'authoring')
        (path/'keep.pack').write_bytes(b'visual runtime bytes')
        for bank in ['DESERT','SAND']:
            (path/('terrainmaterial_'+bank+'.pack')).write_bytes(material())
            (path/('terrainsurface_'+bank+'.pack')).write_bytes(surface)
        return path
    def snapshot(path):return {str(p.relative_to(path)):sha(p)for p in path.rglob('*')if p.is_file()and not p.is_symlink()}
    checks=[]
    def refused(label,path):
        before=snapshot(path)
        try:helper.prepare(path)
        except ValueError:pass
        else:raise AssertionError(label)
        assert snapshot(path)==before,label+' mutated refused stage';checks.append(label)
    refused('unmarked-private-runtime-preserved',stage('unmarked',False))
    with patch.object(Path,'home',return_value=work/'home'):
        for folder in ['CNC3D','CNC3D-latest']:
            p=stage('home/'+folder+'/playable');refused('marked-installed-'+folder+'-preserved',p)
            refused('marked-installed-'+folder+'-descendant-preserved',stage('home/'+folder+'/playable/fresh'))
            (p.parent/helper.MARKER).write_bytes(helper.MARKER_BYTES)
            refused('marked-installed-'+folder+'-ancestor-preserved',p.parent)
    p=stage('source-checkout')
    with patch.object(helper,'ROOT',p):refused('marked-source-checkout-preserved',p)
    p=stage('distinct-sand');(p/'terrainmaterial_SAND.pack').write_bytes(material(105));refused('distinct-sand-preserved',p)
    p=stage('different-keys');(p/'terrainsurface_SAND.pack').write_bytes(surface[:20]+bytes(24)+surface[44:]);refused('different-physical-keys-preserved',p)
    p=stage('symlink-authoring');shutil.rmtree(p/'rock-materials');(p/'rock-materials').symlink_to(work/'unmarked'/'rock-materials');refused('symlink-authoring-refused',p)
    p=stage('fresh-public');before=snapshot(p);(p/'ra-enhanced-hud.pack').write_bytes(b'unused root copy');report=helper.prepare(p)
    assert not(p/'terrainmaterial_SAND.pack').exists()and not(p/'ra-enhanced-hud.pack').exists()
    assert all(not(p/n).exists()for n in helper.AUTHORING)and not(p/helper.MARKER).exists()
    assert sha(p/'terrainmaterial_DESERT.pack')==before['terrainmaterial_DESERT.pack']and sha(p/'keep.pack')==before['keep.pack']
    assert report['sand_desert_identical_material']['material_cell']==12
    assert report['sand_desert_identical_material']['physical_cell']==8
    checks.append('fresh-public-identical-donor-and-authoring-copies-only')
    for platform in ['macos','windows']:
        for folder in helper.AUTHORING:
            assert not policy.allowed(folder+'/map.png','td',platform)and not policy.allowed(folder+'/map.png','ra',platform)
        for folder,extension in [('ebld','ebld'),('eplc','eplc'),('evhc','evhc')]:
            assert not policy.allowed(folder+'/GUN.'+extension,'ra',platform)
            assert policy.allowed(folder+'/GUN.'+extension,'td',platform)
            assert policy.allowed('ra3d/'+folder+'/GUN.'+extension,'ra',platform)
        assert policy.allowed('models3d/E1.c3m','ra',platform)and policy.allowed('einf/E1.c3m','ra',platform)
        assert policy.allowed('terrainmaterial_SAND.pack','td',platform),'Only equality-verified stage copies may be omitted'
        assert not policy.allowed('ra-enhanced-hud.pack','td',platform)
    checks.append('module-policy-preserves-runtime-models-local-ra-and-explicit-sand')
    # Exact RA-only closure contract: no suffix/glob rule may remove other art.
    omitted=set()
    for bank,stems in {
        'DESERT':['terrainenh','terrainseam','clearblend','terrainmaterial','terrainsurface','coastmask','groundmask'],
        'SAND':['terrainenh','terrainseam','clearblend','terrainsurface','coastmask','groundmask'],
        'WINTER':['terrainenh','terrainseam','terrainmaterial','terrainsurface','coastmask','waterbase'],
    }.items():omitted.update(stem+'_'+bank+'.pack'for stem in stems)
    omitted.update(['foliage3d_DESERT.pack','mottle_desert.pack'])
    assert policy.RA_UNUSED_TD_PACKS==omitted and len(omitted)==21
    duplicate_roots={stem+'_RA_'+bank+'.pack'for stem in ['terrainenh','terrainseam']for bank in ['SNOW','TEMPERAT']}
    omitted.update(duplicate_roots);assert len(omitted)==25
    assert policy.RA_ROOT_PACKS=={'clearblend_RA_SNOW.pack','groundmask_RA_SNOW.pack','waterbase_RA_TEMPERAT.pack',
                                'terrainmaterial_RA_SNOW.pack','terrainmaterial_RA_TEMPERAT.pack',
                                'terrainsurface_RA_SNOW.pack','terrainsurface_RA_TEMPERAT.pack'}
    retained={'ra3d/'+name for name in omitted}|set(policy.RA_ROOT_PACKS)|{
        'rock3d_WINTER.pack','rock3d_TEMPERAT.pack','rock3d_DESERT.pack','foliage3d.pack','mottle.pack',
        'ambience/desert_zoomin.wav','ambience/desert_zoomout.wav','ambience/water_zoomin.wav',
        'models3d/E1.c3m','einf/E1.c3m','ra3d/ebld/BARL.ebld','ra3d/eplc/GUN.eplc','ra3d/evhc/1TNK.evhc',
        'future_DESERT.pack','future_WINTER.pack'}
    for bank in ['TEMPERAT','SNOW']:
        retained.update(stem+'_'+bank+'.pack'for stem in ['terrainenh','terrainseam','clearblend','terrainmaterial',
                                                       'terrainsurface','coastmask','groundmask','waterbase'])
    folder=work/'ra-closure';folder.mkdir()
    for name in sorted(omitted|retained):
        path=folder/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(('exact:'+name).encode())
    before=snapshot(folder)
    for platform in ['macos','windows']:
        ra=policy.collect(folder,'ra',platform);td=policy.collect(folder,'td',platform)
        assert set(ra)==retained and set(before)-set(ra)==omitted
        assert (omitted-duplicate_roots)<=td.keys(),'All21 TD-owned files remain in TD payloads'
        assert all(sha(path)==before[name]for name,path in td.items())
        archive=work/('ra-closure-'+platform+'.zip');policy.write_archive(ra,archive,'ra',platform,'0.9.0')
        with zipfile.ZipFile(archive)as z:
            assert set(z.namelist())==retained|{'opencnc3d-module-ra.txt'}
            assert all(hashlib.sha256(z.read(name)).hexdigest()==before[name]for name in retained)
    assert snapshot(folder)==before
    checks.append('ra-only-exact25-omitted-all-retained-hashes-and-td-files-preserved')
    # Exercise the real streamed writer. Supplying ZipInfo bypasses ZipFile's
    # global compression default, so compare the actual raw DEFLATE streams.
    rng=random.Random(20261025)
    payload=b''.join(bytes([rng.randrange(256)])*rng.randrange(1,1000)for _ in range(1000))
    def deflate(data,level):
        compressor=zlib.compressobj(level,zlib.DEFLATED,-15)
        return compressor.compress(data)+compressor.flush()
    assert deflate(payload,6)!=deflate(payload,9),'Fixture must distinguish the compression levels'
    source=work/'compression-proof.pack';source.write_bytes(payload);archive=work/'compression.zip'
    policy.write_archive({'compression-proof.pack':source},archive,'td','windows','0.9.0')
    packed=archive.read_bytes()
    with zipfile.ZipFile(archive)as z:
        for info in z.infolist():
            name_length,extra_length=struct.unpack_from('<HH',packed,info.header_offset+26)
            start=info.header_offset+30+name_length+extra_length
            assert info.compress_type==zipfile.ZIP_DEFLATED
            assert packed[start:start+info.compress_size]==deflate(z.read(info),9),info.filename
    checks.append('actual-streamed-payload-and-manifest-use-deflate9')
    # Run the release script's actual final upload-set/cap body, with sparse
    # boundary fixtures. No release command, tag, push or upload is executed.
    release=(ROOT/'tools/release.sh').read_text()
    marker="<<'PY_ASSET_LIMIT'"
    body=release.split(marker,1)[1].split('\n',2)[2].split('\nPY_ASSET_LIMIT',1)[0]
    assert release.index(marker)<release.index('git commit -q -m "Build $TAG')
    assert release.index('tools/launcher/make-manifest.sh "$TAG" "$MANIFEST"')<release.index(marker)
    cap=work/'actual-release-cap.py';cap.write_text(body)
    uploads=work/'uploads';uploads.mkdir();tag='v0.9.0';names=[]
    for platform in ['macos','windows']:
        names.append(f'OpenCNC3D-{platform}-{tag}.zip')
        names.extend(f'{name}-{platform}-{tag}.'+('pkg'if platform=='macos'else'exe')for name in
                     ['OpenCNC3DInstaller','OpenCNC3DComplete','RA3DComplete'])
        names.extend(f'{name}-{platform}-{tag}.zip'for name in ['OpenCNC3DBinaries','RA3DBinaries'])
        names.extend(f'OpenCNC3D-module-{module}-{platform}-{tag}.zip'for module in ['td','ra'])
    names.extend([f'OpenCNC3D-{tag}-modules.txt',f'OpenCNC3D-{tag}-manifest.txt'])
    paths=[uploads/name for name in names]
    for path in paths:path.write_bytes(b'fixture')
    def capped(label,items,expected):
        receipt=work/(label+'.json')
        run=subprocess.run([sys.executable,str(cap),str(receipt),tag,*map(str,items)],capture_output=True,text=True)
        assert(run.returncode==0)==expected,(label,run.stderr)
        assert receipt.exists()==expected,label;checks.append(label)
    capped('pretag-exact-eighteen-assets-pass',paths,True)
    capped('pretag-missing-asset-refused',paths[:-1],False)
    capped('pretag-duplicate-asset-refused',paths[:-1]+paths[:1],False)
    paths[0].write_bytes(b'');capped('pretag-empty-asset-refused',paths,False)
    with paths[0].open('wb')as stream:stream.truncate(2**31)
    capped('pretag-two-gib-boundary-refused',paths,False)
    paths[0].write_bytes(b'fixture')
    assert 'OUT=$(cd "$OUT" && pwd -P)'in(ROOT/'tools/mac/make-package-mac.sh').read_text()
    assert 'scratch = Path(scratch).resolve()'in(ROOT/'tools/launcher/package-modules.py').read_text()
    checks.append('fresh-system-temp-alias-canonicalized-before-strict-stage-guard')
    windows=(ROOT/'tools/win/make-build-win.sh').read_text()
    strip_at=windows.index('    python3 "$ROOT/tools/release/strip-windows-debug.py" prepare')
    block=windows[windows.rfind('if [ -n "$OPENCNC3D_COOKED" ]; then',0,strip_at):]
    block=block[:block.index('\nfi')+3]
    for cooked in ['', '1']:
        folder=work/('windows-cooked'if cooked else'windows-developer');folder.mkdir()
        (folder/helper.MARKER).write_bytes(helper.MARKER_BYTES)
        calls=folder/'calls.txt'
        shell='''python3(){
    printf '%s\\n' "$*" >> "$CALLS"
    case "$1" in */prepare-public-runtime.py) rm "$OUT/.opencnc3d-public-stage" ;; esac
}
'''+block
        subprocess.run(['sh','-e','-c',shell],check=True,env={**os.environ,'OPENCNC3D_COOKED':cooked,
                       'OUT':str(folder),'ROOT':str(ROOT),'STAGE':str(folder.parent),'CALLS':str(calls)})
        assert not(folder/helper.MARKER).exists()
        if cooked:
            lines=calls.read_text().splitlines();assert len(lines)==3
            assert 'strip-windows-debug.py prepare --stage 'in lines[0]
            assert 'strip-windows-debug.py apply --marker 'in lines[1]
            assert 'prepare-public-runtime.py'in lines[2]
        else:assert not calls.exists(),'Developer Windows packaging must retain unstripped inputs/provenance'
        checks.append('windows-cooked-strip-then-reduce'if cooked else'windows-developer-preserves-debug-donor-and-provenance')
    # Compile the production loader body itself. GL calls capture its final atlas
    # in CPU memory; there is no GL context, game process, geometry or shader.
    header=(ROOT/'game/terrain_surface.h').read_text()
    function=header[header.index('static void terrain_surface_material_load('):header.index('static GLuint terrain_surface_normal_program(')]
    prefix=r'''#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>
#include <zlib.h>
using GLuint=unsigned;using GLint=int;
enum { GL_MAX_TEXTURE_SIZE=1,GL_TEXTURE_BINDING_2D,GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_TEXTURE_MAG_FILTER,GL_LINEAR,GL_TEXTURE_WRAP_S,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE,GL_RGBA8,GL_RGBA,GL_UNSIGNED_BYTE };
struct State{std::string theater;GLuint material=0;int materialCell=0,materialCols=0,materialRows=0;};
static State g_terrainSurface;static std::vector<unsigned char> uploaded;
static bool stone_maps_enabled(){return true;}
static unsigned stone_u32(const unsigned char* p){return p[0]|unsigned(p[1])<<8|unsigned(p[2])<<16|unsigned(p[3])<<24;}
static void glGetIntegerv(int name,GLint* value){*value=name==GL_MAX_TEXTURE_SIZE?8192:0;}
static void glGenTextures(int,GLuint* value){*value=71;}
template<class... T>static void glBindTexture(T...){}
template<class... T>static void glTexParameteri(T...){}
static void glTexImage2D(int,int,int,int w,int h,int,int,int,const unsigned char* data){uploaded.assign(data,data+(size_t)w*h*4);}
'''
    suffix=r'''int main(int argc,char**argv){if(argc!=5)return 2;g_terrainSurface.theater=argv[1];if(chdir(argv[2]))return 3;FILE*f=fopen(argv[3],"rb");if(!f)return 4;std::vector<unsigned char>s;for(int c;(c=fgetc(f))!=EOF;)s.push_back(c);fclose(f);terrain_surface_material_load(s);f=fopen(argv[4],"wb");if(!f)return 5;if(!uploaded.empty())fwrite(uploaded.data(),1,uploaded.size(),f);fclose(f);return 0;}
'''
    cpp=work/'actual-loader.cpp';cpp.write_text(prefix+function+suffix);binary=work/'actual-loader'
    compiler=os.environ.get('CXX')or shutil.which('clang++')or shutil.which('g++');assert compiler
    subprocess.run([compiler,'-std=c++14','-O2',str(cpp),'-lz','-o',str(binary)],check=True)
    def native(name,theater='SAND',explicit=None,donor=material(),explicit_game=None,override=False,physical=surface):
        directory=work/name;directory.mkdir();(directory/'game').mkdir();s=directory/'surface.pack';s.write_bytes(physical)
        if donor is not None:(directory/'terrainmaterial_DESERT.pack').write_bytes(donor)
        if explicit is not None:(directory/'terrainmaterial_SAND.pack').write_bytes(explicit)
        if explicit_game is not None:(directory/'game/terrainmaterial_SAND.pack').write_bytes(explicit_game)
        output=directory/'uploaded.bin';env={**os.environ};env.pop('OPENCNC3D_TERRAIN_SURFACE_DIR',None)
        if override:env['OPENCNC3D_TERRAIN_SURFACE_DIR']=str(directory/'game')
        result=subprocess.run([str(binary),theater,str(directory),str(s),str(output)],env=env,capture_output=True,text=True)
        assert result.returncode==0,(name,result.stderr);checks.append(name);return output.read_bytes()
    exact=native('native-explicit-identical',explicit=material());assert exact
    assert native('native-missing-sand-donor')==exact
    distinct=native('native-explicit-later-root-preferred',explicit_game=material(110));assert distinct and distinct!=exact
    assert not native('native-corrupt-explicit-refuses-donor',explicit=b'bad')
    assert not native('native-invalid-explicit-keys-refuses-donor',explicit=material(keys=bytes(24)))
    assert not native('native-invalid-donor-keys-refused',donor=material(keys=bytes(24)))
    assert not native('native-other-theater-no-donor',theater='SNOW')
    assert native('native-override-explicit-preferred',explicit_game=material(110),override=True)==distinct
    result=dict(passed=True,checks=checks,actual_loader_body_sha256=hashlib.sha256(function.encode()).hexdigest(),
                fresh_public_only=True,explicit_sand_all_roots_preferred=True,missing_only_donor_fallback=True,
                original_key_decode_validation_retained=True,public_runtime_retained_bytes_exact=True,
                fixture_directory=str(work),gl_context_game_or_gpu_executed=False)
    (work/'RESULTS.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))

if __name__=='__main__':main()
