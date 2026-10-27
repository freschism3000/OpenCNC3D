#!/usr/bin/env python3
"""Exercise the real RA startup function without a window, GL context or personal CFG.

With --baseline-ref, also compile that revision's function and compare every byte of
all saved-choice, automated and explicit-preset cases. Only the fresh interactive
fallback may differ. Test files and native probe outputs stay in --out.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def extract(source, start, end):
    assert source.count(start) == 1
    a = source.index(start)
    return source[a:source.index(end, a)]


def probe(source, baseline):
    startup = extract(source, 'void game_visuals_default_ra(', '\nint game_display_wanted(')
    menu = extract(source, 'extern "C" int game_enhanced_menu_enabled(void)', '\n/* The menu uses')
    prefix = r'''
#include "fx_state.h"
#include <assert.h>
static int g_fxPresetNamed=0, g_raModule=1, g_optRedAlert=1, pushed=0;
static int em_red_alert(){return 1;}
static const char* SDL_getenv(const char*){return NULL;}
// These two side effects are outside startup-mode selection. No GL or save-path IO.
static void vis_push_mode(){pushed++;}
static void fxp_set_save_path(const char*){}
'''
    main = r'''
static void file(const char* name,const char* text){FILE*f=fopen(name,"w");assert(f);fputs(text,f);assert(fclose(f)==0);}
struct Case{const char* name;const char* ra;const char* td;int automated,named,namedMode,want,changed;};
int main(){
 file("enhanced.cfg","enabled 1\ndos_classic 0\nsun_az 42\n");
 file("classic.cfg","enabled 0\ndos_classic 1\nsun_az 73\n");
 file("n64.cfg","enabled 0\ndos_classic 0\nsun_az 51\n");
 file("empty.cfg","");file("unknown.cfg","unrecognized_test_key 1\n");
 file("display.cfg","display_mode 0\nres_w 1024\nres_h 768\n");
 const Case cases[]={
  {"fresh",NULL,NULL,0,0,0,FX_MODE_ENHANCED,1},
  {"missing-ra","missing.cfg",NULL,0,0,0,FX_MODE_ENHANCED,1},
  {"empty-ra","empty.cfg",NULL,0,0,0,FX_MODE_CLASSIC,0},
  {"unknown-ra","unknown.cfg",NULL,0,0,0,FX_MODE_CLASSIC,0},
  {"td-enhanced-only",NULL,"enhanced.cfg",0,0,0,FX_MODE_ENHANCED,1},
  {"td-classic-only",NULL,"classic.cfg",0,0,0,FX_MODE_ENHANCED,1},
  {"saved-enhanced","enhanced.cfg",NULL,0,0,0,FX_MODE_ENHANCED,0},
  {"saved-classic","classic.cfg",NULL,0,0,0,FX_MODE_CLASSIC,0},
  {"saved-n64","n64.cfg",NULL,0,0,0,FX_MODE_CLASSIC,0},
  {"display-only-saved-n64","display.cfg",NULL,0,0,0,FX_MODE_CLASSIC,0},
  {"ra-over-td","classic.cfg","enhanced.cfg",0,0,0,FX_MODE_CLASSIC,0},
  {"automated-fresh",NULL,NULL,1,0,0,FX_MODE_CLASSIC,0},
  {"automated-enhanced-save","enhanced.cfg",NULL,1,0,0,FX_MODE_CLASSIC,0},
  {"automated-classic-save","classic.cfg",NULL,1,0,0,FX_MODE_CLASSIC,0},
  {"named-enhanced","classic.cfg",NULL,0,1,FX_MODE_ENHANCED,FX_MODE_ENHANCED,0},
  {"named-classic","enhanced.cfg",NULL,0,1,FX_MODE_CLASSIC,FX_MODE_CLASSIC,0},
  {"named-n64","enhanced.cfg",NULL,0,1,FX_MODE_N64,FX_MODE_CLASSIC,0},
  {"named-enhanced-automated","classic.cfg",NULL,1,1,FX_MODE_ENHANCED,FX_MODE_ENHANCED,0}
 };
 for(const auto& c:cases){
  fx_red_alert_dials(1);fx_defaults(&g_fx);g_fxPresetNamed=c.named;pushed=0;
  const FxState compiled=g_fx;
  if(c.named){g_fx.enabled=c.namedMode==FX_MODE_ENHANCED;g_fx.dos_classic=c.namedMode==FX_MODE_CLASSIC;g_fx.sun_az=19;}
  game_visuals_default_ra(c.ra,c.td,c.automated);
  int want=c.want;
  if(BASELINE&&c.changed)want=FX_MODE_CLASSIC;
  assert(fx_mode_of(&g_fx)==want);
  assert(game_enhanced_menu_enabled()==(want==FX_MODE_ENHANCED));
  assert(pushed==1);
  if(!BASELINE&&c.changed){assert(g_fx.enabled&&g_fx.new_hud&&g_fx.bilinear);assert(g_fx.texset==FX_TEX_ENH);}
  if(!strcmp(c.name,"fresh")){
   FxState expected=compiled;const int m=BASELINE?FX_MODE_CLASSIC:FX_MODE_ENHANCED;
   expected.mode_row[FX_MODE_CLASSIC][FXR_TEXSET]=FX_TEX_DOS;
   expected.mode_row[FX_MODE_CLASSIC][FXR_NEWHUD]=0;
   expected.mode_row[FX_MODE_CLASSIC][FXR_BILINEAR]=0;
   for(int r=0;r<FXR_COUNT;r++)fx_mode_set_live(&expected,r,expected.mode_row[m][r]);
   expected.dos_classic=m==FX_MODE_CLASSIC;expected.enabled=m==FX_MODE_ENHANCED;
   assert(memcmp(&expected,&g_fx,sizeof g_fx)==0);
  }
  if(c.named)assert(g_fx.sun_az==19);
  char path[128];snprintf(path,sizeof path,"%s.state",c.name);FILE*f=fopen(path,"wb");assert(f);assert(fwrite(&g_fx,1,sizeof g_fx,f)==sizeof g_fx);assert(fclose(f)==0);
  printf("RADEFAULT|%s|mode=%s|menu=%d|changed=%d\n",c.name,FX_MODE_NAME[fx_mode_of(&g_fx)],game_enhanced_menu_enabled(),c.changed);
 }
}
'''
    return '#define BASELINE %d\n' % baseline + prefix + startup + '\n' + menu + '\n' + main


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--baseline-ref')
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    current = (ROOT / 'game/cnc_eyes.cpp').read_text()
    sources = {'current': (current, 0)}
    if args.baseline_ref:
        old = subprocess.check_output(['git', 'show', args.baseline_ref + ':game/cnc_eyes.cpp'], cwd=ROOT, text=True)
        sources['baseline'] = (old, 1)
    reports = {}
    for name, (source, baseline) in sources.items():
        stage = out / name
        stage.mkdir()
        cpp = stage / 'probe.cpp'
        cpp.write_text(probe(source, baseline))
        compiler = shlex.split(os.environ.get('CXX', 'c++'))
        argv = compiler + ['-std=c++14', '-O0', '-I' + str(ROOT / 'game'), str(cpp), '-o', str(stage / 'probe')]
        result = subprocess.run(argv, capture_output=True, text=True)
        (stage / 'compile.log').write_text(result.stdout + result.stderr)
        assert result.returncode == 0, result.stderr
        result = subprocess.run([str(stage / 'probe')], cwd=stage, capture_output=True, text=True)
        (stage / 'stdout.log').write_text(result.stdout)
        (stage / 'stderr.log').write_text(result.stderr)
        assert result.returncode == 0, result.stderr
        rows = [line.split('|') for line in result.stdout.splitlines()]
        assert len(rows) == 18
        reports[name] = {row[1]: {'changed': row[-1] == 'changed=1', 'state_sha256': hashlib.sha256((stage / (row[1] + '.state')).read_bytes()).hexdigest()} for row in rows}
    if 'baseline' in reports:
        assert reports['current'].keys() == reports['baseline'].keys()
        for name, row in reports['current'].items():
            assert (row['state_sha256'] != reports['baseline'][name]['state_sha256']) == row['changed'], name
    receipt = {'passed': True, 'source_sha256': hashlib.sha256(current.encode()).hexdigest(),
               'cases_per_revision': 18, 'baseline_ref': args.baseline_ref,
               'all_14_preserved_cases_raw_FxState_identical': 'baseline' in reports,
               'four_fresh_interactive_cases_changed_to_Enhanced': True,
               'fresh_state_exactly_compiled_defaults_with_Enhanced_mode_rows': True,
               'actual_source_function_and_menu_gate_extracted': True,
               'no_game_window_GL_or_native_Windows_execution': True, 'cases': reports}
    (out / 'RESULTS.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('RADEFAULT|PASS|18 real-function cases; saved choices, automation and named presets preserved')


if __name__ == '__main__':
    main()
