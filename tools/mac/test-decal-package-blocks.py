#!/usr/bin/env python3
"""Exercise the actual narrow Mac asset and bins shell blocks in private fixtures.

Never invokes release.sh, publishes, tags, compiles or runs a game. Positive decal
production uses the real helper, image baker and authored finals. Synthetic files
stand in for executable entries only when exercising the extracted bins list.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

CHECKS=[]
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def check(ok,name):
    CHECKS.append({'check':name,'pass':bool(ok)})
    if not ok: raise AssertionError(name)
def write(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_bytes(data.encode() if isinstance(data,str) else data)
def run(command,cwd=None):
    return subprocess.run(command,cwd=cwd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
def extract(source,start,end):
    begin=source.index(start); stop=source.index(end,begin)+len(end)
    return source[begin:stop]+'\n'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--approved-sha',default='fb7325f350762e6ca5141c3e489b1736f2eaedcd07b78aab615df714d73985c9')
    args=parser.parse_args();repo=args.repo.resolve();out=args.output.resolve()
    assert Path('/private/tmp') in out.parents and not out.exists(), 'Use fresh private output'
    out.mkdir(parents=True)
    mac=repo/'tools/mac/make-package-mac.sh';release=repo/'tools/release.sh'
    mtext=mac.read_text();rtext=release.read_text()
    full=extract(mtext,'python3 "$ROOT/tools/art/remaster/decals/build-runtime.py" --pack "$OUT/decalenh.pack" >/dev/null',
                 '[ "$_n" -gt 0 ] || die "there are no .pack files in $SRC."')
    bins=extract(rtext,'MACBIN_LIST="cnc_eyes opencnc3d opencnc3d-launcher',
                 'echo "   bins zip: $MACBIN_GOT entries ($MACBIN_LIST)"')
    cursor=mtext[mtext.index('# THE REMASTERED CURSORS'):mtext.index('# THE UNITY EXPLOSION')]
    die=next(line for line in mtext.splitlines() if line.startswith('die()'))
    write(out/'actual-full-decal-block.sh',full);write(out/'actual-mac-bins-block.sh',bins)
    check('decalenh.pack' in bins.splitlines()[0].split(), 'actual Mac bins include-list explicitly names required decals')
    check('git ' not in bins and 'gh ' not in bins and 'release.sh' not in bins and 'tools/win/' not in bins,
          'extracted actual bins block contains no release, compiler or network invocation')
    inputs=['tools/art/remaster/decals/build-runtime.py','tools/art/remaster/decals/bake_decalenh.py']
    for theater in ('TEM','DES'):
        for code in ['CR'+str(i) for i in range(1,7)]+['SC'+str(i) for i in range(1,7)]:
            for suffix in (('.png','_soot.png') if code.startswith('CR') else ('_soot.png',)):
                inputs.append('art/remaster/decals/final/'+theater+'_'+code+suffix)
    inputs.append('game/ecur/ecur.bin')
    source_hashes={name:sha(repo/name) for name in inputs}
    for name in ('tools/launcher/data-id.sh','tools/launcher/make-manifest.sh','launcher/lupdate.c'):
        source_hashes[name]=sha(repo/name)
    source_hashes['tools/mac/make-package-mac.sh']=sha(mac);source_hashes['tools/release.sh']=sha(release)
    check(len(inputs)==39 and all((repo/name).stat().st_size>0 for name in inputs), 'freeze real helper, baker, 36 nonempty authored finals and cursors')
    root=out/'frozen-repo'
    for name in inputs:
        target=root/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(repo/name,target)
    assert all(sha(root/name)==source_hashes[name] for name in inputs)
    # The cursor block is mandatory and reads the source tree, not stale assets
    # in playable. Exercise the unchanged extracted shell in tiny private folders.
    write(out/'actual-full-cursor-block.sh',cursor)
    cursor_wrapper=out/'cursor-block-wrapper.sh'
    prefix='#!/bin/sh\nset -e\nROOT="$1"\nOUT="$2"\nmkdir -p "$OUT"\n'+die+'\n'
    write(cursor_wrapper,prefix+cursor)
    source_cursor=root/'game/ecur/ecur.bin';cursor_bytes=source_cursor.read_bytes()
    cursor_stage=out/'cursor-positive';write(cursor_stage/'ecur/ecur.bin',b'stale installation cursor')
    result=run(['sh',str(cursor_wrapper),str(root),str(cursor_stage)])
    write(out/'cursor-positive.log',result.stdout)
    check(result.returncode==0 and (cursor_stage/'ecur/ecur.bin').read_bytes()==cursor_bytes,
          'actual cursor block replaces stale staged bytes with the exact source cursor')
    for mode in ('missing','empty'):
        if mode=='missing':source_cursor.unlink()
        else:write(source_cursor,b'')
        stage=out/('cursor-'+mode)
        result=run(['sh',str(cursor_wrapper),str(root),str(stage)])
        write(out/('cursor-'+mode+'.log'),result.stdout)
        check(result.returncode!=0 and 'missing or empty' in result.stdout,
              'actual cursor block refuses '+mode+' mandatory source cursor')
        check(not (stage/'ecur/ecur.bin').exists(),mode+' source cannot stage a fallback cursor')
    write(source_cursor,cursor_bytes)
    # A copy operation that incorrectly succeeds after producing nothing must be
    # caught by the caller's output guard as well as its source guard.
    empty_copy=out/'cursor-empty-copy-wrapper.sh'
    write(empty_copy,prefix+'cp() { : > "$2/ecur.bin"; }\n'+cursor)
    result=run(['sh',str(empty_copy),str(root),str(out/'cursor-empty-copy')])
    write(out/'cursor-empty-copy.log',result.stdout)
    check(result.returncode!=0 and 'missing or empty in the macOS package' in result.stdout,
          'actual cursor block rejects an empty successful copy')
    # Cursor data is deliberately outside the bins allow-list. The real data-id
    # helper changes when the missing cursor is added; the manifest reads that
    # exact record, so older installations receive the full package.
    data_stage=out/'cursor-data-fingerprint';write(data_stage/'content/fixture.pack',b'unchanged fixture data')
    def fingerprint():
        got=run(['sh',str(repo/'tools/launcher/data-id.sh'),str(data_stage)])
        check(got.returncode==0 and re.fullmatch(r'[0-9a-f]{64}\n?',got.stdout) is not None,
              'actual data fingerprint helper succeeds')
        return got.stdout.strip()
    old_data=fingerprint();write(data_stage/'ecur/ecur.bin',cursor_bytes);new_data=fingerprint()
    check(old_data!=new_data,'adding required cursor changes actual data identity and prevents an old-data bins update')
    (data_stage/'ecur/ecur.bin').unlink()
    check(fingerprint()==old_data,'removing cursor restores the original fixture data identity')
    manifest_zips=[]
    for platform,data_id in [('macos',new_data),('windows',old_data)]:
        archive=out/('OpenCNC3D-'+platform+'-v0.0.0.zip')
        with zipfile.ZipFile(archive,'w') as z:
            z.writestr('OpenCNC3D-'+platform+'-v0.0.0/opencnc3d-install.txt',
                       'version 0.0.0\ndata_id '+data_id+'\n')
        manifest_zips.append(str(archive))
    manifest=out/'cursor-fixture-manifest.txt'
    result=run(['sh',str(repo/'tools/launcher/make-manifest.sh'),'v0.0.0',str(manifest),*manifest_zips])
    write(out/'cursor-manifest.log',result.stdout)
    check(result.returncode==0 and ('macos_data_id '+new_data+'\n') in manifest.read_text(),
          'actual manifest helper publishes the corrected full-package cursor data identity')
    check('ecur/ecur.bin' not in bins.splitlines()[0].split(),
          'cursor remains full-package data instead of being added to the flat bins allow-list')

    src=root/'source-playable';src.mkdir()
    stale=b'STALE-INSTALLATION-DECAL-BYTES-DO-NOT-SHIP'
    other=b'ordinary fixture top-level pack'
    write(src/'decalenh.pack',stale);write(src/'dosclassic.pack',other)
    wrapper=out/'full-block-wrapper.sh'
    write(wrapper,'#!/bin/sh\nset -e\nROOT="$1"\nSRC="$2"\nOUT="$3"\nmkdir -p "$OUT"\n'+die+'\n'+full)
    positive=out/'full-positive';result=run(['sh',str(wrapper),str(root),str(src),str(positive)])
    write(out/'full-positive.log',result.stdout)
    check(result.returncode==0,'actual full decal/copy shell block succeeds with real authored bake')
    approved=positive/'decalenh.pack'
    check(sha(approved)==args.approved_sha,'fresh actual Mac full package decal bytes match exact approved SHA')
    check((positive/'dosclassic.pack').read_bytes()==other,'ordinary source packs retain their bytes')
    check((src/'decalenh.pack').read_bytes()==stale and sha(approved)!=sha(src/'decalenh.pack'), 'stale source decal is ignored and left unchanged')
    result=run([sys.executable,str(root/'tools/art/remaster/decals/build-runtime.py'),'--check','--pack',str(approved)])
    check(result.returncode==0,'actual verifier accepts freshly baked Mac package runtime pack')
    # Check source-copy support when an older installation has no decal file.
    (src/'decalenh.pack').unlink();absent=out/'full-source-missing';result=run(['sh',str(wrapper),str(root),str(src),str(absent)])
    write(out/'full-source-missing.log',result.stdout)
    check(result.returncode==0 and sha(absent/'decalenh.pack')==args.approved_sha, 'actual block succeeds without a source installation decal pack')
    write(src/'decalenh.pack',stale)
    # Only the image baker is replaced for failure injection. The actual helper
    # performs validation and the unchanged extracted package block stops on failure.
    baker=root/'tools/art/remaster/decals/bake_decalenh.py';real_baker=baker.read_bytes()
    for mode,body in [('failed',"raise SystemExit('injected image bake failure')\n"),
                      ('empty',"import argparse\nfrom pathlib import Path\np=argparse.ArgumentParser();p.add_argument('--pack',required=True);a=p.parse_args();Path(a.pack).write_bytes(b'')\n")]:
        write(baker,body);stage=out/('full-baker-'+mode)
        result=run(['sh',str(wrapper),str(root),str(src),str(stage)]);write(out/('full-baker-'+mode+'.log'),result.stdout)
        check(result.returncode!=0,'actual helper and full shell block refuse '+mode+' image baker')
        check(not (stage/'decalenh.pack').exists(),'refused '+mode+' image baker cannot stage a decal pack')
        check(not (stage/'dosclassic.pack').exists(),'refused '+mode+' image baker stops before source-pack copying')
        check(not list(stage.glob('.decalenh-*')),'refused '+mode+' image baker cleans temporary output')
    write(baker,real_baker)
    # This additional empty-success producer proves the caller's explicit -s guard,
    # independently of the real helper's structural validation.
    helper=root/'tools/art/remaster/decals/build-runtime.py';real_helper=helper.read_bytes()
    write(helper,"import argparse\nfrom pathlib import Path\np=argparse.ArgumentParser();p.add_argument('--pack',required=True);a=p.parse_args();Path(a.pack).write_bytes(b'')\n")
    stage=out/'full-empty-success-producer';result=run(['sh',str(wrapper),str(root),str(src),str(stage)])
    write(out/'full-empty-success-producer.log',result.stdout)
    check(result.returncode!=0 and 'missing or empty' in result.stdout,'actual Mac package caller rejects empty successful producer output')
    check(not (stage/'dosclassic.pack').exists(),'empty successful producer stops before source-pack copy')
    write(helper,real_helper)
    names=re.search(r'^MACBIN_LIST="([^\n"]+)"',bins,re.M).group(1).split()
    assert names.count('decalenh.pack')==1
    bins_wrapper=out/'bins-block-wrapper.sh'
    write(bins_wrapper,'#!/bin/sh\nset -e\nMACBINS="$1"\n'+die+'\n'+bins)
    for suffix,sdls in [('one-sdl',['libSDL2-2.0.0.dylib']),('two-sdl',['libSDL2-2.0.0.dylib','libSDL3.0.dylib'])]:
        base=out/('bins-'+suffix);play=base/'playable';play.mkdir(parents=True)
        for name in names+sdls:
            write(play/name,approved.read_bytes() if name=='decalenh.pack' else ('nonexecuted '+name+'\n').encode())
        archive=base/'fixture-mac-bins.zip';result=run(['sh',str(bins_wrapper),str(archive)],cwd=base)
        write(base/'positive.log',result.stdout)
        check(result.returncode==0,'actual Mac bins block succeeds with '+suffix)
        with zipfile.ZipFile(archive) as z:
            check(z.testzip() is None,'Mac bins '+suffix+' all member CRCs pass')
            check(set(z.namelist())==set(names+sdls) and len(z.namelist())==len(names+sdls),'Mac bins '+suffix+' exact include-list and flat shape')
            check(hashlib.sha256(z.read('decalenh.pack')).hexdigest()==args.approved_sha,'Mac bins '+suffix+' carries exact approved decal bytes')
            check(all('/' not in name for name in z.namelist()),'Mac bins '+suffix+' no directories, Logs or unrelated data')
        (play/'decalenh.pack').unlink();missing=base/'missing-decal.zip'
        result=run(['sh',str(bins_wrapper),str(missing)],cwd=base);write(base/'missing.log',result.stdout)
        check(result.returncode!=0 and 'missing: decalenh.pack' in result.stdout,'actual Mac bins '+suffix+' refuses missing decal before zip')
        check(not missing.exists(),'missing decal '+suffix+' cannot publish a partial bins archive')
    check(all(sha(repo/name)==wanted for name,wanted in source_hashes.items()),'authoritative source and authored finals remain unchanged')
    check(all(sha(root/name)==source_hashes[name] for name in inputs),'private frozen real helper, baker and authored finals restored unchanged')
    result={'status':'PASS','checks_passed':len(CHECKS),'checks':CHECKS,'approved_pack_sha256':args.approved_sha,
            'fresh_actual_bake_sha256':sha(approved),'source_hashes':source_hashes,
            'extracted_shell_sha256':{'full':hashlib.sha256(full.encode()).hexdigest(),'cursor':hashlib.sha256(cursor.encode()).hexdigest(),'bins':hashlib.sha256(bins.encode()).hexdigest()},
            'cursor_data_identity':{'without_cursor':old_data,'with_cursor':new_data},
            'scope':'Actual narrow extracted Mac shell blocks and real data fingerprint/manifest helpers only; real authored image baking in positive cases and explicit private failure injection. Bins executable entries are nonexecuted synthetic fixtures. Full unrelated Mac assembly, Mach-O validation and native gameplay are outside this suite.',
            'release_script_invoked':False,'publication_or_tags_performed':False,'native_game_executed':False,'source_or_installed_files_written':False}
    write(out/'verification.json',json.dumps(result,indent=2)+'\n')
    print(json.dumps({key:result[key] for key in ('status','checks_passed','approved_pack_sha256','release_script_invoked','source_or_installed_files_written')},indent=2))

if __name__=='__main__':main()
