#!/usr/bin/env python3
"""Run actual infantry controller/FX adapters without a window or installed writes."""
import argparse,hashlib,json,pathlib,re,shutil,subprocess,tempfile,sys
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--repo',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path)
p.add_argument('--cc',default='clang');p.add_argument('--cxx',default='clang++')
p.add_argument('--include-dir',action='append',type=pathlib.Path,default=[]);p.add_argument('--library-dir',action='append',type=pathlib.Path,default=[])
a=p.parse_args();repo=a.repo.resolve()
includes=['-I'+str(path.resolve()) for path in a.include_dir];libraries=['-L'+str(path.resolve()) for path in a.library_dir]
# Balanced extraction of actual production adapters avoids compiling a second model
# renderer or copying the logic into a fake implementation.
s=(repo/'game/cnc_eyes.cpp').read_text()
def function(name):
 start=s.rfind('\n',0,s.index(name+'('))+1;brace=s.index('{',start);depth=1;i=brace+1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[start:i]+'\n'
start=s.index('static const struct { int elem; int fxr; } VIS_FXR[]');end=s.index('\n};',start)+3
adapters=s[start:end]+'\n'+''.join(function(n) for n in ['vis_clampi','prof_from_rows','rows_from_prof'])
model=(repo/'game/model3d_mod.h').read_text();m=re.search(r'static bool m3inf_set_now\(void\)\s*\{[^}]*\}',model);assert m;adapters+=m.group(0)+'\n'
with tempfile.TemporaryDirectory(prefix='opencnc3d-infantry-') as d:
 work=pathlib.Path(d);(work/'production-adapters.inc').write_text(adapters)
 for f in ['fx_state.h','dosopt.h']:shutil.copy2(repo/'game'/f,work/f)
 shutil.copy2(pathlib.Path(__file__).with_name('infantry-selector.cpp'),work/'test.cpp')
 log=work/'test.log'
 def checked(cmd,out):
  result=subprocess.run(cmd,stdout=out,stderr=subprocess.STDOUT)
  if result.returncode:
   out.flush();message=log.read_text();print(message[-6000:])
   if a.output:a.output.mkdir(parents=True,exist_ok=True);shutil.copy2(log,a.output/'test.log')
   raise RuntimeError('Compiler failed; see test.log')
 executable=work/('test.exe' if sys.platform=='win32' else 'test')
 with log.open('wb') as out:
  for name in ['dosopt','dosbar']:
   source=repo/'game'/(name+'.c');checked([a.cc,'-std=gnu89','-O2','-ffunction-sections','-fdata-sections','-I'+str(work),'-I'+str(repo/'game'),*includes,'-c',str(source),'-o',str(work/(name+'.o'))],out)
  linker=['-Wl,-dead_strip'] if sys.platform=='darwin' else ['-Wl,--gc-sections']
  if sys.platform=='win32':linker+=['-static']
  checked([a.cxx,'-std=c++14','-O2','-I'+str(work),'-I'+str(repo/'game'),*includes,str(work/'test.cpp'),str(work/'dosopt.o'),str(work/'dosbar.o'),*libraries,'-lz',*linker,'-o',str(executable)],out)
  run=subprocess.run([str(executable),str(work)],stdout=out,stderr=subprocess.STDOUT)
 text=log.read_text();result=re.search(r'INFANTRY_RESULT\|checks=(\d+)\|failures=(\d+)',text);assert result,text[-3000:]
 verification={'passed':run.returncode==0,'checks':int(result[1]),'failures':int(result[2]),'window_opened':False,'installed_files_written':False,'actual_controller_and_adapters':True,'source_hashes':{f:hashlib.sha256((repo/f).read_bytes()).hexdigest() for f in ['game/cnc_eyes.cpp','game/fx_state.h','game/dosopt.h','game/dosopt.c','game/model3d_mod.h']}}
 if a.output:a.output.mkdir(parents=True,exist_ok=True);(a.output/'verification.json').write_text(json.dumps(verification,indent=2)+'\n');shutil.copy2(log,a.output/'test.log');(a.output/'production-adapters.inc').write_text(adapters)
 print(json.dumps(verification,indent=2));assert verification['passed']
