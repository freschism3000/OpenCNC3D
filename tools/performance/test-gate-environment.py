"""Verify G331 child controls and sticky failures under release sh and bash; no game runs."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import tempfile

ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--repo',type=Path,required=True)
ap.add_argument('--out',type=Path,required=True)
args=ap.parse_args()
repo=args.repo.resolve();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
source=(repo/'game/gates.sh').read_text()
a=source.index('gbegin shots/g331_rim.log');b=source.index('G331CHK=',a)
block=source[a:b]
a=source.index('grun() {');b=source.index('\ngshots()',a)
grun=source[a:b]
(out/'actual-grun.sh').write_text(grun)
(out/'actual-G331-block.sh').write_text(block)

def calls(block):
    logical=[];line=''
    for physical in block.splitlines():
        line+=physical[:-1]+' ' if physical.endswith('\\') else physical
        if physical.endswith('\\'):continue
        stripped=line.strip()
        if (stripped.startswith('grun shots/g331_') or
            stripped.startswith('OPENCNC3D_GENERATED_MODE_MASK=') or
            stripped.startswith('export OPENCNC3D_GENERATED_MODE_MASK=') or
            stripped.startswith('unset OPENCNC3D_GENERATED_MODE_MASK ')):
            logical.append(line)
        line=''
    return '\n'.join(logical)+'\n'

expected=[('1','1'),('1','1'),('1','0'),('1','0'),('1','1'),('0','1')]
results=[]
for version,block in [('current',block)]:
    for shell in ('/bin/sh','/bin/bash'):
        for fail in (0,1):
            with tempfile.TemporaryDirectory(prefix='case-',dir=out) as temp:
                d=Path(temp);(d/'shots/g331').mkdir(parents=True)
                mock=d/'cnc_eyes'
                mock.write_text('''#!/bin/sh
printf 'CHILD|gpu=%s|mode=%s\\n' "${OPENCNC3D_MESH_GPU-unset}" "${OPENCNC3D_GENERATED_MODE_MASK-unset}"
case "$*" in *g331/rim.txt*) [ "${MOCK_FAIL_FIRST-0}" = 1 ] && exit 7 ;; esac
exit 0
''');mock.chmod(0o755)
                script=d/'run.sh'
                script.write_text('GRC=0\nBASE=""\nOUT="all.log"\n: > "$OUT"\nunset OPENCNC3D_MESH_GPU OPENCNC3D_GENERATED_MODE_MASK\n'+grun+'\n'+calls(block)+
                                  'printf "AFTER|gpu=%s|mode=%s|GRC=%s\\n" "${OPENCNC3D_MESH_GPU-unset}" "${OPENCNC3D_GENERATED_MODE_MASK-unset}" "$GRC"\n')
                p=subprocess.run([shell,str(script)],cwd=d,env=dict(os.environ,MOCK_FAIL_FIRST=str(fail)),capture_output=True,text=True)
                assert p.returncode==0,p.stderr
                child=re.findall(r'^CHILD\|gpu=(\w+)\|mode=(\w+)$',(d/'all.log').read_text(),re.M)
                assert child==expected,(version,shell,child)
                m=re.search(r'^AFTER\|gpu=(\w+)\|mode=(\w+)\|GRC=(\d+)$',p.stdout,re.M)
                assert m
                gpu,mask,rc=m.groups()
                assert int(rc)==(7 if fail else 0),(version,shell,rc)
                restored=(gpu,mask)==('unset','unset')
                assert restored,(version,shell,gpu,mask,'G331 controls leaked into later gates')
                results.append({'version':version,'shell':shell,'failed_first_child':bool(fail),'six_child_environments_correct':True,
                                'controls_unset_afterward':restored,'sticky_GRC':int(rc),'passed':True})
receipt={'repo':str(repo),'gates_sha256':hashlib.sha256(source.encode()).hexdigest(),'actual_grun_extracted':True,'actual_G331_invocations_extracted':True,'native_game_launched':False,
         'cases':results,'all_passed':True}
(out/'verification.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('PASS: 4 actual-block cases under sh/bash; all six child environments, unset controls and sticky GRC verified; no game launched')
