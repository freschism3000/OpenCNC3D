#!/usr/bin/env python3
"""CPU A/B of actual HUD layout/controllers and approved packs, without GL/SDL.

The production functions are extracted verbatim from current source and, when
requested, an explicit pre-fix Git revision retained for historical comparison.
Only the engine callback outside their active-HUD branch is stubbed. Actual
game/GPU/pixel-matrix verification remains a separate hidden runtime check.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('ra_pack', type=Path)
parser.add_argument('td_pack', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--baseline-ref', help='optional explicit pre-fix revision for historical TD/RA A/B')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
for pack in [args.ra_pack, args.td_pack]:
    assert pack.is_file() and not pack.is_symlink(), pack

def function(source, name):
    match = re.search(r'^static [^\n;]+\b' + re.escape(name) + r'\([^;]*?\)\s*\{', source, re.M)
    assert match, name
    # These functions contain no brace-valued strings. Keep comments in the exact
    # source span; balanced braces delimit the original function, not a rewrite.
    start = match.start()
    position = source.index('{', match.start())
    depth = 1
    position += 1
    while depth:
        depth += (source[position] == '{') - (source[position] == '}')
        position += 1
    return source[start:position]

prelude = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include "hud640.h"
#include "hud_enhanced.h"
#include "hud_enhanced_catalog.h"
#include "dosbar.h"
#include "ra_hud_pixels.h"
static H6_Pack *g_h6Pack=0;
static DB_Pack *g_ra95Pack=0;
static bool g_sbRa=false,g_hudNew=false,g_sbOn=true,g_editOn=false;
static int g_hudNewWish=-1,g_sbUiDiv=1,g_h6Rows=H6_ROWS,g_h6SlideX=0;
static float g_h6Scale=1,g_dbX0=0,g_dbY0=0,g_dbW=0,g_dbH=0;
static int g_dbScale=1,g_dbFbw=0,g_dbFbh=0;
static int g_ra95WorldScale=1,g_ra95Height=400,g_ra95ExtraRows=0,g_ra95Pad=0,g_ra95ViewW=0,g_ra95ViewH=0;
static float g_ra95UiScale=1;
static struct { int enabled,dos_classic; } g_fx;
static struct { void (*Layout)(int,int); } g_sb={0};
static int g_mapW=128,g_mapH=128,g_radOx=0,g_radOy=0,g_radZoom=1;
static int g_mmX=0,g_mmY=0,g_mmW=0,g_mmH=0;
static float g_mmScale=0;
static void radar_dos_transform(void) {}
'''

main = r'''
static uint64_t pixels_hash(const unsigned char *p,size_t n){uint64_t h=1469598103934665603ULL;for(size_t i=0;i<n;i++){h^=p[i];h*=1099511628211ULL;}return h;}
int main(int argc,char **argv){
 assert(argc==3);char err[256];H6_Pack *ra=hud640_load(argv[1],err,sizeof err),*td=hud640_load(argv[2],err,sizeof err);
 assert(ra&&rah_pack_ok(ra)&&td&&hud_enhanced_ok(td)&&hud640_card_ok(td));
 const int sizes[3][2]={{640,480},{1280,720},{2560,1440}};
 for(int module=0;module<2;module++)for(int foundation=0;foundation<2;foundation++)for(int want=0;want<2;want++)for(int enabled=0;enabled<2;enabled++)for(int pack=0;pack<3;pack++)for(int size=0;size<3;size++){
  g_sbRa=module!=0;g_h6Pack=foundation?td:0;g_raHudPack=pack==0?ra:0;g_fx.enabled=enabled;g_fx.dos_classic=!enabled;g_hudNew=false;
  g_h6Rows=H6_ROWS;g_h6Scale=1;g_dbScale=1;g_dbX0=g_dbY0=g_dbW=g_dbH=0;g_ra95UiScale=1;g_ra95Height=400;g_ra95ExtraRows=g_ra95Pad=0;
  if(pack==2){char saved=ra->assets[0].name[0];ra->assets[0].name[0]='!';assert(!rah_pack_ok(ra));g_raHudPack=rah_pack_ok(ra)?ra:0;ra->assets[0].name[0]=saved;assert(rah_pack_ok(ra));}
  sb_set_hud_new(want);const int w=sizes[size][0],h=sizes[size][1];sb_layout(w,h);minimap_layout(w,h);
  const int left=w<696*g_h6Scale?132:160;int hits[4];
  hits[0]=sb_chrome_hit(10*g_h6Scale,8*g_h6Scale,w,h);hits[1]=sb_chrome_hit((left+10)*g_h6Scale,8*g_h6Scale,w,h);
  hits[2]=sb_chrome_hit(w-300*g_h6Scale,8*g_h6Scale,w,h);hits[3]=sb_chrome_hit(w-50*g_h6Scale,8*g_h6Scale,w,h);
  uint64_t pixels=0;
  if(rah_enabled()){
   std::vector<unsigned char> image(RAH_W*rah_height(g_h6Rows)*4,0);RAH_Surface surface={image.data(),RAH_W,rah_height(g_h6Rows)};
   rah_art(surface,"map",0,18);char body[16]="body",glass[16]="glass";if(g_h6Rows<6){snprintf(body,sizeof body,"body%d",g_h6Rows);snprintf(glass,sizeof glass,"glass%d",g_h6Rows);}
   rah_art(surface,body,0,205);const H6_Asset *a=hud640_asset(g_raHudPack,glass);assert(a);rah_art(surface,glass,0,surface.h-a->h);pixels=pixels_hash(image.data(),image.size());
  }else if(!module&&sb_hud_is_new()){
   std::vector<unsigned char> image(EH_WIDTH*EH_MAX_H*4,0);EH_State state={};state.rows=g_h6Rows;hud_enhanced_bar(image.data(),g_h6Pack,&state);pixels=pixels_hash(image.data(),image.size());
  }
  printf("CASE|ra=%d|td=%d|wish=%d|enabled=%d|pack=%d|size=%d|active=%d|rows=%d|scale=%.0f|rect=%.0f,%.0f,%.0f,%.0f|radar=%d,%d,%d,%d,%.6f|hits=%d,%d,%d,%d|database=%d|card=%d|pixels=%llu\n",module,foundation,want,enabled,pack,size,sb_hud_is_new(),g_h6Rows,g_h6Scale,g_dbX0,g_dbY0,g_dbW,g_dbH,g_mmX,g_mmY,g_mmW,g_mmH,g_mmScale,hits[0],hits[1],hits[2],hits[3],codex_available()?1:0,uc_available()?1:0,(unsigned long long)pixels);
 }
 hud640_free(ra);hud640_free(td);return 0;
}
'''

inputs = {}
outputs = {}
for revision in (['baseline'] if args.baseline_ref else []) + ['current']:
    source = {}
    for name in ['cnc_sidebar.h', 'cnc_eyes.cpp', 'codex_mod.h', 'unitcard_mod.h']:
        path = 'game/' + name
        source[name] = (root / path).read_text() if revision == 'current' else subprocess.check_output(['git', 'show', args.baseline_ref + ':' + path], cwd=root, text=True)
    sidebar = source['cnc_sidebar.h']
    enum = re.search(r'enum SbHit\s*\{[^}]+\};', sidebar).group()
    defines = '\n'.join(re.search(r'^#define ' + name + r' .+$', sidebar, re.M).group()
                        for name in ['RA95_MAX_H', 'SB_HUD640_DOS_ZOOM'])
    functions = []
    names = (['sb_hud_active'] if revision == 'current' else []) + ['sb_set_hud_new', 'sb_hud_is_new', 'rah_enabled', 'eh_enabled']
    for name in names:
        functions.append(function(sidebar, name))
    functions.append(function(source['codex_mod.h'], 'codex_available'))
    functions.append(function(source['unitcard_mod.h'], 'uc_available'))
    for name in ['sb_layout', 'sb_scale', 'sb_dos_px', 'sb_dos_to_screen', 'sb_visible_rows', 'sb_chrome_hit']:
        functions.append(function(sidebar, name))
    functions.append(function(source['cnc_eyes.cpp'], 'minimap_layout'))
    program = out / (revision + '.cpp')
    program.write_text(prelude + defines + '\n' + enum + '\n' + '\n\n'.join(functions) + main)
    obj = out / 'hud640.o'
    if not obj.exists():
        subprocess.run(['cc', '-std=gnu89', '-O1', '-I' + str(root / 'game'), '-c', str(root / 'game/hud640.c'), '-o', str(obj)], check=True)
    binary = out / revision
    command = ['c++', '-std=c++11', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-variable', '-I' + str(root / 'game'), str(program), str(obj), '-o', str(binary)]
    subprocess.run(command, check=True)
    result = subprocess.run([str(binary), str(args.ra_pack.resolve()), str(args.td_pack.resolve())], check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (out / (revision + '.log')).write_text(result.stdout)
    cases = {}
    for line in result.stdout.splitlines():
        if not line.startswith('CASE|'):
            continue
        row = dict(part.split('=', 1) for part in line.split('|')[1:])
        key = tuple(int(row[k]) for k in ['ra', 'td', 'wish', 'enabled', 'pack', 'size'])
        assert key not in cases
        cases[key] = row
    assert len(cases) == 144, len(cases)
    outputs[revision] = cases
    inputs[revision] = {name: hashlib.sha256(text.encode()).hexdigest() for name, text in source.items()}

count = 0
for key, row in outputs['current'].items():
    module, foundation, wish, enabled, pack, size = key
    before = outputs['baseline'][key] if args.baseline_ref else None
    if not module:
        assert int(row['active']) == bool(wish and foundation), (key, row)
        if before is not None:
            assert row == before, ('TD changed', key, row, before)
    elif foundation and before is not None:
        assert row == before, ('RA with foundation changed', key, row, before)
    if module:
        expected = wish and enabled and pack == 0
        assert int(row['active']) == expected, (key, row)
        other = dict(outputs['current'][(module, 1 - foundation, wish, enabled, pack, size)])
        other['td'] = row['td']
        assert row == other, ('RA still depends on TD', key, row, other)
        if expected:
            assert row['database'] == row['card'] == '1' and int(row['pixels']) > 0
            assert row['hits'] == '8,11,10,9', row
            if args.baseline_ref:
                assert outputs['baseline'][(module, 0, wish, enabled, pack, size)]['active'] == '0'
        else:
            assert row['database'] == row['card'] == '0' and row['pixels'] == '0'
    count += 1

receipt = dict(scope='Actual-source native CPU A/B; no GL/SDL/game execution', cases_per_revision=count,
               historical_baseline=args.baseline_ref,
               source_sha256=inputs, packs=[dict(path=str(p.resolve()), sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in [args.ra_pack, args.td_pack]],
               assertions=(['All TD cases byte-identical to explicit historical baseline', 'RA with TD foundation byte-identical to explicit historical baseline'] if args.baseline_ref else ['TD availability retains its own-pack requirement']) +
                          ['RA layout/radar/header hits/Database/card/composed native art independent of TD pack',
                           'Real RA pack missing or corrupted disables RA Enhanced safely', 'Classic/opt-out preserved'],
               limitations=['CPU raster proof composes native housing/map/glass only; full HUD/card pixels and gameplay need the separate reserved hidden GPU matrix.'])
(out / 'RESULTS.json').write_text(json.dumps(receipt, indent=2) + '\n')
print('HUD-INDEPENDENCE|PASS|cases_per_revision=' + str(count) + '|historical_AB=' + str(int(bool(args.baseline_ref))) + '|RA_without_TD_exact=1|radar_headers_database_card=1|missing_corrupt_classic_safe=1|GPU_NOT_RUN=1')
