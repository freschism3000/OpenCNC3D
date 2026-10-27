#!/usr/bin/env python3
"""Exercise full and filtered destination bank contracts using the real baker/stager."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
TD=['TEMPERAT','DESERT','WINTER','SNOW','SAND']
FULL=TD+['RA_TEMPERAT','RA_SNOW']

def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def main():
    if not __debug__:raise RuntimeError('Assertions are required for this gate')
    work=Path(tempfile.mkdtemp(prefix='stone-bank-contract-'))
    (work/'.cnc3d-cleanup.json').write_text(json.dumps({'kind':'completed-generated-staging','preserve_unique_authoring':True})+'\n')
    source=work/'source';assets=source/'assets';assets.mkdir(parents=True)
    for rel in ['tools/bakery/stone_materials.py','tools/art/rocks/stage_stone_materials.py']:
        target=source/rel;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/rel,target)
    # Each bank owns one explicit UV/mask pair. The tiny source adapter isolates
    # destination selection while the actual material compiler remains unchanged.
    rows=[];yy,xx=np.indices((144,144))
    for number,bank in enumerate(['TEMPERAT','DESERT','WINTER','SNOW','RATEMP','RASNOW']):
        dest=assets/bank;dest.mkdir()
        rgb=np.uint8(np.dstack(((xx*7+yy*3+number*11)%256,(xx*2+yy*5)%256,(xx*3+yy*7)%256)))
        Image.fromarray(rgb).save(dest/'albedo.png')
        Image.fromarray(np.full((144,144),255,np.uint8)).save(dest/'rock_mask.png')
        Image.fromarray(np.zeros((144,144),np.uint8)).save(dest/'player_colour_mask.png')
        rows.append(dict(theater=bank,export=bank,name='X1',variant=0,cells=[1,1],have=[0],tid=1,
                         albedo_sha256=sha(dest/'albedo.png'),rock_mask_sha256=sha(dest/'rock_mask.png'),
                         player_colour_mask_sha256=sha(dest/'player_colour_mask.png')))
    (assets/'MANIFEST.json').write_text(json.dumps({'assets':rows}))
    Image.new('RGB',(2,2),(100,90,80)).save(assets/'shared.png')
    (source/'tools/bakery/terrain_daylight.py').write_text('''from pathlib import Path
import json
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'assets'
DESERT_MATERIAL=SOURCE/'shared.png'
def manifest():return json.loads((SOURCE/'MANIFEST.json').read_text())
def rows(bank):return [r for r in manifest()['assets'] if r['theater']==bank]
def surface_source(row):return row
def checked(row,name,mode):return Image.open(SOURCE/row['export']/name).convert(mode)
def temperate_stone(image,mask):return image
def shared_stone(image,mask):return image
def desert_stone(row):return checked(row,'albedo.png','RGB'),checked(row,'rock_mask.png','L')
''')
    full=work/'full';partial=work/'td';full.mkdir();partial.mkdir()
    for bank in FULL:
        key=b'X1'.ljust(16,b'\0')+struct.pack('<HHBBH',1,0,0,int(bank.startswith('RA_')),0)
        raw=b'C3DSURF\0'+struct.pack('<III',1,144,1)+key
        (full/f'terrainsurface_{bank}.pack').write_bytes(raw)
        if bank in TD:(partial/f'terrainsurface_{bank}.pack').write_bytes(raw)
    binpath=work/'bin';binpath.mkdir();(binpath/'python3').symlink_to(sys.executable)
    env={**os.environ,'PATH':str(binpath)+os.pathsep+os.environ.get('PATH',''),'PYTHONDONTWRITEBYTECODE':'1'}
    checks=[]
    def run(label,command,success=True):
        result=subprocess.run(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        (work/(label+'.log')).write_text(result.stdout)
        assert (result.returncode==0)==success,(label,result.stdout)
        checks.append(label)
        return result.stdout
    def bake(label,runtime,banks=None,success=True):
        out=work/label
        args=[sys.executable,str(source/'tools/bakery/stone_materials.py'),'--source',str(source),'--runtime',str(runtime),'--out',str(out)]
        if banks is not None:args+=['--banks',*banks]
        run(label,args,success)
        return out
    def material_hashes(path):return {p.name:sha(p) for p in path.glob('terrainmaterial_*.pack')}
    allout=bake('default-full',full)
    assert set(json.loads((allout/'MANIFEST.json').read_text())['selected_terrain_banks'])==set(FULL)
    tdout=bake('explicit-td-on-filtered-destination',partial,TD)
    tdhash=material_hashes(tdout)
    assert set(tdhash)=={f'terrainmaterial_{b}.pack' for b in TD}
    assert all(material_hashes(allout)[p]==h for p,h in tdhash.items())
    explicit=bake('explicit-td-with-unused-ra-inputs',full,TD)
    assert material_hashes(explicit)==tdhash
    bake('default-refuses-missing-ra',partial,success=False)
    bake('explicit-refuses-missing-ra',partial,['RA_SNOW'],False)
    bake('explicit-refuses-unknown-bank',full,['NO_SUCH_BANK'],False)
    bake('explicit-refuses-duplicate-bank',full,['TEMPERAT','TEMPERAT'],False)
    bake('sand-requires-desert-donor',full,['SAND'],False)
    one=bake('explicit-single-bank',partial,['TEMPERAT'])
    assert set(material_hashes(one))=={'terrainmaterial_TEMPERAT.pack'}
    (partial/'terrainsurface_SAND.pack').unlink()
    bake('declared-sand-must-exist',partial,TD,False)
    shutil.copy2(full/'terrainsurface_SAND.pack',partial/'terrainsurface_SAND.pack')
    sand=full/'terrainsurface_SAND.pack';saved=sand.read_bytes();sand.unlink()
    six=bake('default-preserves-optional-sand-contract',full)
    assert set(material_hashes(six))=={f'terrainmaterial_{b}.pack' for b in FULL if b!='SAND'}
    sand.write_bytes(saved)
    stager=source/'tools/art/rocks/stage_stone_materials.py'
    before={p.name:sha(p) for p in full.glob('terrainsurface_*.pack')}
    run('stage-full',[sys.executable,str(stager),str(full),'--source',str(source)])
    run('stage-td',[sys.executable,str(stager),str(partial),'--source',str(source),'--banks',*TD])
    caches=list((source/'build-native/stone-materials').glob('*/MANIFEST.json'))
    assert len(caches)==2
    contracts={tuple(json.loads(p.read_text())['selected_terrain_banks']):p.parent for p in caches}
    assert set(contracts)=={tuple(sorted(FULL)),tuple(sorted(TD))}
    for contract,cache in contracts.items():
        report=json.loads((cache/'MANIFEST.json').read_text())
        assert set(report['terrain'])==set(contract)
        assert {p.stem.removeprefix('terrainsurface_') for p in (cache/'inputs').glob('terrainsurface_*.pack')}==set(contract)
    run('stage-td-cache-reuse',[sys.executable,str(stager),str(partial),'--source',str(source),'--banks',*TD])
    assert len(list((source/'build-native/stone-materials').glob('*/MANIFEST.json')))==2
    assert before=={p.name:sha(p) for p in full.glob('terrainsurface_*.pack')}
    assert not list(partial.glob('terrainmaterial_RA_*.pack'))
    assert not (partial/'stone-materials/v01/maps/RA_SNOW').exists()
    assert material_hashes(partial)==tdhash
    run('stager-refuses-missing-selected-bank',[sys.executable,str(stager),str(partial),'--source',str(source),'--banks','RA_SNOW'],False)
    # Run the actual package branches with a recording Python command: both must
    # restore final Desert overrides before deriving maps from those final bytes.
    recorder=work/'record-bin';recorder.mkdir();record=work/'package-stage-argv.jsonl'
    (recorder/'python3').write_text('#!'+sys.executable+'\nimport json,os,sys\nwith open(os.environ["STONE_STAGE_RECORD"],"a") as f:f.write(json.dumps(sys.argv[1:])+"\\n")\n')
    (recorder/'python3').chmod(0o755)
    for platform,rel,endmarker in [('macos','tools/mac/make-package-mac.sh','\n\n# THE PLACEMENTS'),('windows','tools/win/make-build-win.sh','\n\n    # The placements')]:
        text=(ROOT/rel).read_text();start=text.index('python3 "$ROOT/tools/art/terrain/stage_desert_details.py" "$OUT"')
        assert text.rfind('"$OUT/ebld/"',0,start)>0,'Raw placed art must precede the final material stagers'
        snippet=text[start:text.index(endmarker,start)]
        for no_ra in (0,1):
            record.unlink(missing_ok=True)
            shellenv={**env,'PATH':str(recorder)+os.pathsep+env['PATH'],'ROOT':str(source),'OUT':str(work/'package'),
                      'NO_RA':str(no_ra),'OPENCNC3D_NO_RA':str(no_ra),'STONE_STAGE_RECORD':str(record)}
            result=subprocess.run(['sh','-c',snippet],env=shellenv,capture_output=True,text=True)
            assert result.returncode==0,(platform,no_ra,result.stderr)
            argv=[json.loads(line)for line in record.read_text().splitlines()]
            assert len(argv)==2 and Path(argv[0][0]).name=='stage_desert_details.py' and Path(argv[1][0]).name=='stage_stone_materials.py'
            assert argv[1][2:]==(['--banks',*TD]if no_ra else[]),(platform,no_ra,argv)
            checks.append(platform+('-filtered-stage-order'if no_ra else'-full-stage-order'))
    report=dict(passed=True,checks=checks,full_default_banks=sorted(FULL),filtered_td_banks=sorted(TD),
                retained_bank_packs_byte_identical=True,selected_banks_in_cache_identity_and_manifest=True,
                required_missing_unknown_duplicate_banks_rejected=True,physical_surface_inputs_unchanged=True,
                fixture_directory=str(work),game_or_gpu_executed=False)
    (work/'RESULTS.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
