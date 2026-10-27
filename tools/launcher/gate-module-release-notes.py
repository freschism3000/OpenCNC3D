#!/usr/bin/env python3
"""Actual tiny module ZIP/native install proof for the shared offline changelog."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import zipfile

ROOT=Path(__file__).resolve().parents[2]


def require(ok,message):
    if not ok: raise ValueError(message)


def sha(path):
    with path.open('rb') as stream: return hashlib.file_digest(stream,'sha256').hexdigest()


def run(argv,log):
    result=subprocess.run([str(x) for x in argv],capture_output=True,text=True)
    log.write_text(result.stdout+result.stderr)
    require(result.returncode==0,'command failed: '+str(log))
    return result.stdout.strip()


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    work=a.out.resolve();require(not work.exists(),'preserve previous evidence');work.mkdir(parents=True)
    spec=importlib.util.spec_from_file_location('notes_packages',ROOT/'tools/launcher/package-modules.py')
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
    notes=ROOT/'docs/CHANGELOG.md';expected_notes=notes.read_bytes();expected_sha=sha(notes)
    first_release=next(line for line in expected_notes.decode('utf-8').splitlines() if line.startswith('## '))
    require('v'+(ROOT/'VERSION').read_text().strip() in first_release,'offline notes do not open with the current release')
    tracked=subprocess.check_output(['git','-C',str(ROOT),'ls-files','--error-unmatch','docs/CHANGELOG.md'],text=True)
    require(tracked.strip()=='docs/CHANGELOG.md','notes source is not tracked')
    source_hashes={n:sha(ROOT/n) for n in ('launcher/lmodule_remote.h','tools/launcher/package-modules.py',
        'tools/launcher/gate-module-release-notes.c','tools/launcher/gate-module-release-notes.py','launcher/launcher.c')}
    # The exact production file reader is independent of the module installer.
    source=(ROOT/'launcher/launcher.c').read_text();start=source.index('static char *l_read_file(')
    end=source.index('\n}\n',start)+3;reader=source[start:end]
    (work/'actual-launcher-reader.inc').write_text(reader)
    baseline=work/'baseline';baseline.mkdir()
    for name in ('lmodule.c','lmodule_remote.h'):
        (baseline/name).write_bytes(subprocess.check_output(['git','-C',str(ROOT),'show','HEAD:launcher/'+name]))
    binaries={}
    for label,module_source in [('current',ROOT/'launcher/lmodule.c'),('baseline',baseline/'lmodule.c')]:
        binary=work/('gate-'+label);binaries[label]=binary
        argv=['cc','-O1','-Wall','-Wextra','-Werror','-std=gnu89','-I'+str(ROOT/'launcher'),
              '-DMODULE_SOURCE="'+str(module_source)+'"','-DRELEASE_NOTES_READER="'+str(work/'actual-launcher-reader.inc')+'"',
              str(ROOT/'tools/launcher/gate-module-release-notes.c'),str(ROOT/'launcher/lzip.c'),
              str(ROOT/'launcher/lnet.c'),str(ROOT/'launcher/ljson.c'),'-lcurl','-lz','-o',str(binary)]
        run(argv,work/('compile-'+label+'.log'))
    paths=['CHANGELOG.txt','changelog.txt','Changelog.txt','CHANGELOG.TXT','CHANGELOG.txt.cfg','CHANGELOG.txt.sav',
           'CHANGELOG.txt/child','CHANGELOG.txt/../savegame.sav','./CHANGELOG.txt','../CHANGELOG.txt','/CHANGELOG.txt',
           'ra3d/CHANGELOG.txt','.CHANGELOG.txt','settings/CHANGELOG.txt','ra3d/settings/CHANGELOG.txt',
           'savegame.sav','ra3d-fx.cfg','ra3d/movies/CHANGELOG.txt','CHANGELOG.txt ',
           'missions/CHANGELOG.txt','ra3d/missions/CHANGELOG.txt']
    policy=[]
    for target,module in [(0,'td'),(1,'ra')]:
        for index,path in enumerate(paths):
            current=run([binaries['current'],'allowed',target,path],work/f'policy-new-{target}-{index}.log')=='1'
            old=run([binaries['baseline'],'allowed',target,path],work/f'policy-old-{target}-{index}.log')=='1'
            require(current==(True if path=='CHANGELOG.txt' else old),'native policy broadened beyond exact root name')
            for platform in ('macos','windows'):
                if path=='CHANGELOG.txt': require(m.allowed(path,module,platform),'Python rejects exact notes')
                elif not old: require(not m.allowed(path,module,platform),'Python accepts refused notes variant')
            policy.append(dict(target=module,path=path,old=old,current=current))
    require(m.shared('CHANGELOG.txt'),'notes must be shared')
    packages={}
    for module in ('td','ra'):
        names=['opencnc3d','TiberianDawn.dylib','CONQUER.MIX'] if module=='td' else re.findall(
            r'"(ra3d/[^\"]+)"',(ROOT/'launcher/lmodule.c').read_text().split('lmod_ra_needs[] = {',1)[1].split('};',1)[0])
        names=[n for n in names if not n.endswith(('.exe','.dll'))]
        files={}
        for rel in names:
            path=work/'inputs'/module/rel;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(('fixture '+rel).encode());files[rel]=path
        original=dict(files);files=m.add_release_notes(files)
        require(original.keys()==set(files)-{'CHANGELOG.txt'} and files['CHANGELOG.txt']==notes,'helper changes non-notes map')
        out=work/(module+'.zip');record=m.write_archive(files,out,module,'macos','0.9.0');m.verify_archive(out,module,'macos','0.9.0')
        with zipfile.ZipFile(out) as z:
            require(z.read('CHANGELOG.txt')==expected_notes and stat.S_IMODE(z.getinfo('CHANGELOG.txt').external_attr>>16)==0o644,'notes content/mode differ')
            require(sum(f['path']=='CHANGELOG.txt' and f['sha256']==expected_sha for f in record['files'])==1,'missing manifest ownership/hash')
        packages[module]=out
    fixtures=[]
    for label,existing in [('fresh',False),('existing-public-same',True)]:
        install=work/label;install.mkdir();(install/'.opencnc3d-modules').mkdir()
        for rel in ('savegame.sav','ra3d-fx.cfg'):(install/rel).write_text('PLAYER KEEP EXACT\n')
        if existing:
            (install/'CHANGELOG.txt').write_bytes(expected_notes);(install/'CHANGELOG.txt').chmod(0o644)
            os.utime(install/'CHANGELOG.txt',ns=(1690000000123456789,1690000000123456789))
        cache=install/'.opencnc3d-modules/ra3d.zip';shutil.copy2(packages['ra'],cache)
        Path(str(cache)+'.sha256').write_text(sha(cache)+'\n');Path(str(cache)+'.version').write_text('0.9.0\n')
        run([binaries['current'],'exercise',install,packages['td'],packages['ra'],notes],work/(label+'.log'))
        require((install/'savegame.sav').read_text()==(install/'ra3d-fx.cfg').read_text()=='PLAYER KEEP EXACT\n','player data changed')
        fixtures.append(label)
    rejected=[]
    for index,rel in enumerate(['changelog.txt','ra3d/CHANGELOG.txt','../CHANGELOG.txt','savegame.sav','ra3d-fx.cfg']):
        archive=work/f'bad-{index}.zip';data=b'bad injected payload';digest=hashlib.sha256(data).hexdigest()
        manifest=f'module-files 1\nmodule ra\nversion 0.9.0\nplatform macos\nfile {digest} {len(data)} {rel}\n'
        with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
            z.writestr(rel,data);z.writestr('opencnc3d-module-ra.txt',manifest)
        install=work/f'reject-{index}';install.mkdir();(install/'savegame.sav').write_bytes(b'KEEP')
        run([binaries['current'],'reject',install,1,archive],work/f'reject-{index}.log')
        require((install/'savegame.sav').read_bytes()==b'KEEP' and not (install/'CHANGELOG.txt').exists(),'refused package changed player/notes')
        rejected.append(rel)
    baseline_install=work/'baseline-refusal';baseline_install.mkdir()
    run([binaries['baseline'],'reject',baseline_install,1,packages['ra']],work/'baseline-notes-refusal.log')
    # Exercise the actual four-module main/receipt path with tiny inert inputs.
    # Only asset gathering and PE-tool execution are replaced for these non-PE fixtures.
    production=work/'producer-main';production.mkdir();main_inputs={}
    for platform in ('macos','windows'):
        package=production/platform;package.mkdir();main_inputs[platform]=package
        names=m.TD_PROGRAMS[platform]|{'CONQUER.MIX','dosmenu.pack','SCG01EA.pack','missions/SCG01EA.INI',
                                         'efx.pack','efx_enh.pack','sky.pack','vfx3d/claims.txt'}
        for rel in names:
            target=package/rel;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(b'inert tiny main input')
        (package/'opencnc3d-install.txt').write_text('version 0.9.0\n')
        app=production/('opencnc3d' if platform=='macos' else 'opencnc3d-game.exe')
        app.write_bytes(b'OPENCNC3D|build|v0.9.0|cooked');main_inputs[platform+'-app']=app
        brain=production/('RedAlert.dylib' if platform=='macos' else 'RedAlert.dll')
        brain.write_bytes(b'inert brain');main_inputs[platform+'-brain']=brain
    presentation=production/'presentation';presentation.mkdir()
    for name in m.RA_ROOT_PACKS:(presentation/name).write_bytes(b'inert current RA bank')
    def fake_stage(source,stage,app,brain):
        stage.mkdir(parents=True);shutil.copy2(app,stage/app.name);shutil.copy2(brain,stage/brain.name)
        for rel in ['SCG01EA.pack','dosmenu.pack','enhanced-menu.pack','einf/E1.c3m','einf/E7.c3m',
                    'ebld/BARL.ebld','3d/SCU01EA.pack','efx/efx_ra.pack','missions/SCU01EA.INI',
                    'audio/RA_LOCAL.MIX','terrainenh_RA_SNOW.pack','terrainenh_RA_TEMPERAT.pack',
                    'terrainseam_RA_SNOW.pack','terrainseam_RA_TEMPERAT.pack']:
            target=stage/rel;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(b'inert RA asset')
    def fake_pe_tool(argv,**kwargs):
        require(Path(argv[1])==ROOT/'tools/release/strip-windows-debug.py','unexpected fixture subprocess')
        return subprocess.CompletedProcess(argv,0)
    old_stage,old_run,old_argv=m.stage_ra,m.subprocess.run,sys.argv
    sys.argv=['package-modules.py','--version','0.9.0','--out',str(production/'modules'),
              '--ra-source',str(production),'--ra-presentation',str(presentation),'--source-commit','1'*40]
    for platform in ('macos','windows'):
        for kind in ('package','app','brain'):
            sys.argv += ['--'+platform+'-'+kind,str(main_inputs[platform if kind=='package' else platform+'-'+kind])]
    try:
        m.stage_ra,m.subprocess.run=fake_stage,fake_pe_tool;m.main()
    finally:
        m.stage_ra,m.subprocess.run,sys.argv=old_stage,old_run,old_argv
    main_receipt=json.loads((production/'modules/module-build-receipt.json').read_text())
    require(main_receipt['release_notes']==dict(source='docs/CHANGELOG.md',path='CHANGELOG.txt',
            sha256=expected_sha,size=len(expected_notes)),'actual main notes provenance differs')
    require(len(main_receipt['modules'])==4,'actual main lost a module')
    for record in main_receipt['modules']:
        require(sum(f['path']=='CHANGELOG.txt' and f['sha256']==expected_sha for f in record['files'])==1,
                'actual main omitted or changed release notes')
    require(sha(notes)==expected_sha and all(sha(ROOT/n)==h for n,h in source_hashes.items()),'source changed during proof')
    result=dict(passed=True,actual_module_installer=True,actual_launcher_file_reader=True,no_HTTP_no_game_no_window=True,
                source_files=source_hashes,release_notes=dict(source=str(notes),sha256=expected_sha,bytes=len(expected_notes),
                                                            first_release=first_release),
                exact_policy_matrix=policy,install_fixtures=fixtures,malicious_ZIP_refusals=rejected,
                actual_four_module_main_notes_provenance=True,main_fixture_asset_gathering_and_PE_tools_stubbed=True,
                baseline_rejects_notes=True,source_notes_mode_unchanged=oct(notes.stat().st_mode&0o777))
    (work/'RESULTS.json').write_text(json.dumps(result,indent=2)+'\n')
    print('MODULE-RELEASE-NOTES|PASS|policy42|fresh+same-existing|TD+RA-direct+cache|player-protection|old-refusal')


if __name__=='__main__':main()
