#!/usr/bin/env python3
"""Bake/cache and stage mineral data AFTER all final albedo and relief stagers."""
import argparse, hashlib, importlib.util, json, os, shutil, subprocess, tempfile
from pathlib import Path

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def stage(root,runtime,banks=None):
 root=root.resolve();runtime=runtime.resolve();baker=root/'tools/bakery/stone_materials.py'
 spec=importlib.util.spec_from_file_location('stone_stage_daylight',root/'tools/bakery/terrain_daylight.py');day=importlib.util.module_from_spec(spec);spec.loader.exec_module(day)
 spec=importlib.util.spec_from_file_location('stone_stage_baker',baker);compiler=importlib.util.module_from_spec(spec);spec.loader.exec_module(compiler)
 selected=compiler.select_banks(day,runtime,banks)
 inputs=[Path(__file__).resolve(),baker,root/'tools/bakery/terrain_daylight.py',day.SOURCE/'MANIFEST.json',root/day.DESERT_MATERIAL]
 for r in day.manifest()['assets']:
  if r['theater'] not in selected.values():continue
  m=day.surface_source(r);inputs += [day.SOURCE/m['export']/n for n in ['albedo.png','rock_mask.png','player_colour_mask.png']]
 inputs += [runtime/f'terrainsurface_{bank}.pack' for bank in selected]+list(runtime.glob('rock3d*.pack'))+list((runtime/'ebld').glob('ROCK*.ebld'))
 signature=hashlib.sha256(('banks:'+json.dumps(list(selected))+'\n'+''.join(('runtime/'+str(p.relative_to(runtime)) if p.is_relative_to(runtime) else 'source/'+str(p.relative_to(root)))+':'+sha(p)+'\n' for p in sorted(set(inputs)))).encode()).hexdigest()
 cache=root/'build-native/stone-materials'/signature
 if not (cache/'MANIFEST.json').is_file():
  cache.parent.mkdir(parents=True,exist_ok=True);temp=Path(tempfile.mkdtemp(prefix='.mineral-bake-',dir=cache.parent))
  (temp/'.cnc3d-cleanup.json').write_text(json.dumps({'kind':'completed-generated-staging','preserve_unique_authoring':True})+'\n')
  # Freeze the pack inputs: a simultaneous asset refresh must not change half
  # of a bake, and the data gate needs the exact pre-normal binary streams.
  frozen=temp/'inputs';frozen.mkdir()
  for p in inputs:
   if p.is_relative_to(runtime):
    q=frozen/p.relative_to(runtime);q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,q)
  subprocess.run(['python3',str(baker),'--source',str(root),'--runtime',str(frozen),'--out',str(temp),'--banks',*selected],check=True)
  report=json.loads((temp/'MANIFEST.json').read_text());report['runtime_root']=str(cache/'inputs');(temp/'MANIFEST.json').write_text(json.dumps(report,indent=2)+'\n')
  (temp/'input-signature.txt').write_text(signature+'\n');temp.rename(cache)
 report=json.loads((cache/'MANIFEST.json').read_text())
 if report.get('selected_terrain_banks')!=list(selected) or set(report['terrain'])!=set(selected):
  raise ValueError('Cached material bank contract does not match the destination')
 def copy(p,q):
  q.parent.mkdir(parents=True,exist_ok=True);fd,name=tempfile.mkstemp(prefix='.mineral-stage-',dir=q.parent);os.close(fd);shutil.copy2(p,name);os.replace(name,q);assert sha(p)==sha(q)
 for p in cache.glob('*.pack'):copy(p,runtime/p.name)
 for p in (cache/'ebld').glob('*.spec'):copy(p,runtime/'ebld'/p.name)
 author=runtime/'stone-materials/v01'
 for p in (cache/'maps').rglob('*.png'):copy(p,author/p.relative_to(cache))
 copy(cache/'MANIFEST.json',author/'MANIFEST.json');copy(cache/'input-signature.txt',author/'input-signature.txt')
 print('Staged UV-paired mineral normals/specular from cache',signature[:12],flush=True)
 return cache
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('runtime',type=Path);p.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[3])
 p.add_argument('--banks',nargs='+',help='Required runtime bank names; default all source banks and available SAND')
 a=p.parse_args();stage(a.source,a.runtime,a.banks)
