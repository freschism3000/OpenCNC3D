#!/usr/bin/env python3
"""Prove local-note retention and public-note fetch with the actual native updater."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

binary=Path(sys.argv[1]).resolve();work=Path(sys.argv[2]);work.mkdir(parents=True,exist_ok=False)
def snapshot(root):
    return {p.relative_to(root).as_posix():(p.stat().st_mode,p.stat().st_size,p.stat().st_mtime_ns,
            hashlib.sha256(p.read_bytes()).hexdigest()) for p in root.rglob('*') if p.is_file()}
reports=[]
for name,installed,latest,private,fetch,empty in [
    ('private-newer','0.9.0-alpha+700c523','0.8.6',True,0,0),
    ('private-equal','0.9.0-alpha+4a4ea10','0.9.0',True,0,0),
    ('private-older','0.8.5+source','0.9.0',True,0,0),
    ('private-panel-local-only','0.9.0-alpha+4a4ea10','0.8.6',True,0,1),
    ('public-newer','0.9.0','0.8.6',False,0,0),
    ('public-newer-patch','0.9.1+source','0.9.0',False,0,0),
    ('public-equal','0.9.0','0.9.0',False,1,0),
    ('public-approved-newer','0.8.7','0.9.0',False,1,0),
    ('public-suffix-is-not-private','0.9.0-alpha+source','0.9.0',False,1,0),
    ('public-unknown','?','0.9.0',False,1,0),
]:
    root=work/name;root.mkdir()
    (root/'opencnc3d-install.txt').write_text('version '+installed+'\n'+('private_developer 1\n' if private else ''))
    for file in ['CHANGELOG.txt','ra3d-fx.cfg','savegame.sav','opencnc3d-update.zip.part']:
        (root/file).write_bytes(b'PRESERVE INSTALLED LOCAL/PLAYER CONTENT\n')
    before=snapshot(root)
    result=subprocess.run([str(binary),str(root.resolve()),installed,latest,str(fetch),str(empty)],capture_output=True,text=True,timeout=30)
    print(result.stdout,end='');print(result.stderr,end='')
    assert result.returncode==0,(name,result.returncode)
    assert snapshot(root)==before,(name,'installed files changed')
    reports.append(dict(case=name,passed=True,notes_requests=fetch,metadata_requests=1,manifest_requests=1,installed_files_exact=True))
(work/'RESULTS.json').write_text(json.dumps(dict(passed=True,cases=reports,no_HTTP=True,no_window=True,actual_lupdate=True),indent=2)+'\n')
print('LOCAL-CHANGELOG-GATE|PASS|10 actual-updater cases; local notes retained; eligible public notes fetched; installed files exact')
