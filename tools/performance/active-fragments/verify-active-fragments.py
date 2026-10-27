#!/usr/bin/env python3
from pathlib import Path
import os,sys,json,subprocess,time,hashlib,gzip,csv,statistics,re,fcntl,argparse,tempfile,shutil
from PIL import Image
HERE=Path(__file__).resolve().parent

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def run(arm,case,repeat=0):
 out=R/os.environ.get('ACTIVE_RUN_ROOT','runs')/arm/(case+('-'+str(repeat) if repeat else ''));out.mkdir(parents=True,exist_ok=True)
 if case in ('fidelity','lifecycle'):
  script=(HERE/'scripts'/(case+'.txt')).read_text().replace('@OUT@',str(out))
 elif case=='matrix':
  script='dosdrawprobe\ngfxmode 2\ngfx show_stats 0\ncheatset fog 0\ntick 3\ncam 12 16\ndist 2700\nhurtcell 15 15\nhurtcell 15 15\ntick 8\n'
  def cap(name,raw=True):
   result='shot '+str(out/(name+'.png'))+'\n'
   if raw:result+='renderdump '+str(out/name)+'\n'
   return result+'objdump\nebldump\nebldbodies\nphysicsdump\nefxdump\nmeshperfdump\n'
  script+='tickalpha 0.25\n'+cap('active')
  script+='tick 45\ntickalpha 0.75\n'+cap('sink')
  script+='shrouddarktest\n'+cap('shroud-dark')
  script+='cam 19 14\ndist 1200\n'+cap('edge-camera')
  script+='gfx relight_on 1\ncam 12 16\ndist 2700\n'+cap('relight')
  script+='gfxmode 0\n'+cap('classic',False)
  script+='gfxmode 1\n'+cap('n64',False)
  script+='gfxmode 2\ntick 30\n'+cap('enhanced-return')
  script+='quit\n'
 elif case=='activebench':
  script='dosdrawprobe\ngfxmode 2\ngfx show_stats 0\ncheatset fog 0\ntick 3\ncam 12 16\ndist 2700\nframebench 3\nhurtcell 15 15\nhurtcell 15 15\ntick 1\nebldump\n'
  for i in range(140):
   script+='tick 1\ntickalpha '+str(.25 if i%2 else .75)+'\necho ACTIVEFRAME|index='+str(i)+'\nframebench 1\n'
   if i in [0,10,30,40,55,65,80,100,120,139]:script+='objdump\nebldump\nebldbodies\nphysicsdump\nefxdump\n'
  script+='meshperfdump\nquit\n'
 elif case.startswith('bench'):
  phase=case.split('-')[1] if '-' in case else 'moving';start={'moving':8,'settling':40,'sinking':60,'fading':85}[phase]
  script='dosdrawprobe\ngfxmode 2\ngfx show_stats 0\ncheatset fog 0\ntick 3\ncam 12 16\ndist 2700\nframes 3\nhurtcell 15 15\nhurtcell 15 15\ntick '+str(start)+'\nframes 3\nmeshperfdump\nlivebench 2\nmeshperfdump\nobjdump\nebldump\nebldbodies\nphysicsdump\nefxdump\nquit\n'
 else:
  script='dosdrawprobe\ngfxmode 2\ngfx show_stats 0\ncheatset fog 0\ntick 3\ncam 12 16\ndist 2700\nhurtcell 15 15\nhurtcell 15 15\ntick 1\n'
  for i in range(150):script+='tick 1\ntickalpha '+str(.25 if i%2 else .75)+'\nframes 1\n'
  script+='objdump\nebldump\nebldbodies\nphysicsdump\nefxdump\nmeshperfdump\nquit\n'
 (out/'script.txt').write_text(script)
 env=dict(os.environ,OPENCNC3D_FRAMEPROF='1' if case=='activebench' else '',OPENCNC3D_PERFDETAIL='1' if case=='activebench' else '',OPENCNC3D_BACKGROUND='1',OPENCNC3D_PERFTRACE=str(out/'frames.csv'),OPENCNC3D_MESH_FRAGMENT_PREPARE='0' if arm=='off' else '1',OPENCNC3D_MESH_FRAGMENT_COALESCE='0' if arm in ('off','prepare') else '1',OPENCNC3D_MESH_FRAGMENT_PACK='0' if arm in ('off','prepare') else '1')
 if case!='activebench':env.pop('OPENCNC3D_FRAMEPROF',None);env.pop('OPENCNC3D_PERFDETAIL',None)
 if arm=='budget':env['OPENCNC3D_MESH_PREPARED_BUDGET_FULL']='0'
 if arm=='gpu-failure':env['OPENCNC3D_MESH_FRAGMENT_GPU_OK']='0'
 if arm=='pack-failure':env['OPENCNC3D_MESH_FRAGMENT_PACK_OK']='0'
 if arm=='source-failure':env['OPENCNC3D_MESH_PREPARED_ALLOC_OK']='0'
 if arm=='pack-alloc-failure':env['OPENCNC3D_MESH_FRAGMENT_PACK_ALLOC_OK']='0'
 if arm=='buffer-failure':env['OPENCNC3D_MESH_FRAGMENT_BUFFER_OK']='0'
 if arm=='store-budget':env['OPENCNC3D_MESH_FRAGMENT_BUDGET_FULL']='0'
 if arm=='input-cap':env['OPENCNC3D_MESH_FRAGMENT_INPUT_FULL']='0'
 if arm=='immediate':env['OPENCNC3D_MESH_GPU']='0'
 args=[str(B),'--build','98','--scen','SCG94EA','--pack',str(P/'SCB01EA.pack'),'--dir',str(F)+'/', '--models3d',str(F)+'/', '--content',str(P/'content')+'/', '--dylib',str(BRAIN),'--nosound','--enhanced','--texset','3','--gfx',str(CFG),'--gfxsave',str(out/'private.cfg'),'--savedir',str(out/'saves'),'--ticks','0','--w','1280','--h','720','--script',str(out/'script.txt')]
 begin=time.monotonic()
 with (out/'run.log').open('w') as log:r=subprocess.run(args,cwd=P,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=300)
 lines=(out/'run.log').read_text().splitlines();ends=[x for x in lines if x.startswith('SCRIPT|end')];maps=[x for x in lines if x.startswith('DOSDRAW|view=')];prep=[x for x in lines if x.startswith('FRAGMENTPREP|')];nuke=[x for x in lines if x.startswith('EBLDOBJ|NUKE|') and '|stage=WRECKED|' in x and '|bodies=321|' in x]
 receipt={'arm':arm,'case':case,'args':args,'controls':{k:v for k,v in env.items() if k.startswith('OPENCNC3D_MESH_FRAGMENT') or k.startswith('OPENCNC3D_MESH_PREPARED')},'exit':r.returncode,'elapsed_seconds':time.monotonic()-begin,'script_end':ends,'originalmap':maps,'actual_nuke321':bool(nuke),'preparation':prep,'binary_sha256':sha(B),'brain_sha256':sha(BRAIN),'NUKE_asset_sha256':sha(P/'ebld/NUKE.ebld')}
 (out/'launch.json').write_text(json.dumps(receipt,indent=2)+'\n');print(arm,case,r.returncode,receipt['elapsed_seconds'],'end',ends[-1:] if ends else [],'prep',prep[-1:] if prep else [],flush=True)
 assert r.returncode==0 and len(ends)==1 and ends[0].endswith('0 failures') and bool(nuke)
 assert maps and all(x.endswith('|map=3,3,57,58') for x in maps)
 return out

def compare(a,b):
 png=[];targets=[]
 assert {x.name for x in a.glob('*.png')}=={x.name for x in b.glob('*.png')}
 assert {x.name for x in a.glob('*.bin.gz')}=={x.name for x in b.glob('*.bin.gz')}
 for p in sorted(a.glob('*.png')):
  aa=Image.open(p).convert('RGBA');bb=Image.open(b/p.name).convert('RGBA');ad=aa.tobytes();bd=bb.tobytes();png.append({'name':p.name,'exact':aa.size==bb.size and ad==bd,'off':hashlib.sha256(ad).hexdigest(),'on':hashlib.sha256(bd).hexdigest()})
 for p in sorted(a.glob('*.bin.gz')):
  ad=gzip.decompress(p.read_bytes());bd=gzip.decompress((b/p.name).read_bytes());targets.append({'name':p.name,'exact':ad==bd,'off':hashlib.sha256(ad).hexdigest(),'on':hashlib.sha256(bd).hexdigest()})
 def state(p):return [x for x in p.read_text().splitlines() if x.split('|',1)[0] in {'OBJ','HOUSE','VIEW','PHYSICSCAPTURE','BUILDINGPHYSICSCAPTURE','M3','M3E','SMUDGE'} or (x.startswith(('EBLD','EFX')) and not x.startswith('EBLDPREPARE|'))]
 sa=state(a/'run.log');sb=state(b/'run.log');r={'passed':all(x['exact'] for x in png+targets) and sa==sb,'PNG_count':len(png),'target_count':len(targets),'state_count_off':len(sa),'state_count_on':len(sb),'state_exact':sa==sb,'PNGs':png,'targets':targets};(b/'comparison.json').write_text(json.dumps(r,indent=2)+'\n');print('comparison',a,b,r['passed'],len(png),len(targets),len(sa),len(sb),flush=True);assert r['passed'];return r
if __name__=='__main__':
 ap=argparse.ArgumentParser(description='Hidden real321-body renderer parity and active CPU/upload/draw regression.')
 ap.add_argument('--binary',type=Path,required=True);ap.add_argument('--playable',type=Path,required=True)
 ap.add_argument('--brain',type=Path);ap.add_argument('--gfx',type=Path,required=True);ap.add_argument('--output',type=Path,required=True)
 ap.add_argument('--suite',choices=['fidelity','lifecycle','matrix','abba','all'],default='all');ap.add_argument('--repo',type=Path)
 a=ap.parse_args();R=a.output.resolve();R.mkdir(parents=True,exist_ok=True);P=a.playable.resolve();B=a.binary.resolve();CFG=a.gfx.resolve();BRAIN=a.brain.resolve() if a.brain else P/'TiberianDawn.dylib'
 # Legacy engine file buffers require a genuinely short scenario path.
 F=Path(tempfile.mkdtemp(prefix='oc3d-af-',dir='/private/tmp'))
 for ext in ['INI','BIN']:shutil.copy2(HERE/'fixtures'/('SCG94EA.'+ext),F/('SCG94EA.'+ext))
 (F/'ebld').symlink_to(P/'ebld',target_is_directory=True)
 before={str(p):sha(p) for p in [B,BRAIN,CFG,P/'ebld/NUKE.ebld',P/'ebld/PYLE.ebld']}
 provenance={'binary':str(B),'playable':str(P),'brain':str(BRAIN),'input_gfx':str(CFG),'fixture':str(F),'hashes_before':before,'background':True,'visible_window':False}
 if a.repo:
  provenance['commit']=subprocess.check_output(['git','rev-parse','HEAD'],cwd=a.repo,text=True).strip()
  provenance['source_hashes']={str(p):sha(a.repo/p) for p in ['game/mesh_batch.h','game/cnc_eyes.cpp']}
 (R/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
 reports={}
 with open('/private/tmp/opencnc3d-performance-gpu.lock','a') as lock:
  fcntl.flock(lock,fcntl.LOCK_EX)
  for case in ['fidelity','lifecycle','matrix','abba']:
   if a.suite not in [case,'all']:continue
   if case=='abba':
    paths=[run(arm,'activebench',i+1) for i,arm in enumerate(['off','on','on','off'])]
   else:
    arms=['off','on'] if case!='matrix' else ['off','on','budget','source-failure','pack-failure','pack-alloc-failure','buffer-failure','store-budget','input-cap']
    paths=[run(arm,case) for arm in arms]
   reports[case]=[compare(paths[0],p) for p in paths[1:]]
   expected={'fidelity':28,'lifecycle':2,'matrix':8,'abba':0}[case]
   assert all(x['PNG_count']==expected for x in reports[case]), 'Missing expected screenshots'
   if case!='abba':
    for path in paths:
     for png in path.glob('*.png'):
      if case=='matrix' and png.stem in ('classic','n64'):continue
      for target in ('scene','depth','normals','sun','terrain-sun'):
       assert (path/(png.stem+'-'+target+'.bin.gz')).is_file(), 'Missing required populated render target'
 after={p:sha(Path(p)) for p in before};assert before==after,'Binary/brain/input/assets changed during test.'
 (R/'verification-summary.json').write_text(json.dumps({'passed':True,'suite':a.suite,'provenance':provenance,'hashes_after':after,'cases':reports},indent=2)+'\n')
 print('PASS: exact pixels/float targets/state; private outputs only.')
