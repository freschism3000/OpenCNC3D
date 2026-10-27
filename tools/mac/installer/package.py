#!/usr/bin/env python3
"""Build the five named macOS release artifacts from vetted immutable inputs.

Native .pkg products install in the user's Applications/OpenCNC3D. Complete
products carry a verified offline module cache and native Music/Movies choices.
The shared launcher controller owns installation, rollback, ownership and media.
No installer, game or GUI is run by this build tool.
"""
from __future__ import annotations
import argparse
import hashlib
import html
import json
import os
from pathlib import Path, PurePosixPath
import plistlib
import re
import shutil
import stat
import subprocess
import tempfile
import xml.etree.ElementTree as ET
import zipfile

ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).resolve().parent
NAMES=('OpenCNC3DInstaller','OpenCNC3DComplete','RA3DComplete','OpenCNC3DBinaries','RA3DBinaries')
BOOTSTRAP=('opencnc3d-launcher','libSDL2-2.0.0.dylib','libSDL3.dylib','dosmenu.pack','enhanced-menu.pack','ecur/ecur.bin','LICENSE','NOTICE.md')
ART={'td':ROOT/'art/interface/launcher-enhanced-v05/CNC-native.png',
     'ra':ROOT/'art/interface/launcher-enhanced-v05/RA-native.png'}


def sha(path):
    with Path(path).open('rb')as f:return hashlib.file_digest(f,'sha256').hexdigest()


def run(*args):
    subprocess.run([str(x)for x in args],check=True)


def safe_name(name):
    p=PurePosixPath(name)
    return bool(name and name.isascii()and not re.search(r'[\s\\:]',name)
                and not p.is_absolute()and str(p)==name
                and all(x not in ('','.','..')for x in p.parts))


def file_rows(root):
    rows=[]
    for current,dirs,files in os.walk(root,followlinks=False):
        current=Path(current)
        for name in dirs+files:
            if(current/name).is_symlink():raise ValueError('Linked build input: '+str(current/name))
        dirs[:]=sorted(dirs)
        for name in sorted(files):
            p=current/name;rel=p.relative_to(root).as_posix()
            if not safe_name(rel)or not p.is_file():raise ValueError('Unsafe package input: '+str(p))
            rows.append(dict(path=rel,size=p.stat().st_size,mode=stat.S_IMODE(p.stat().st_mode),sha256=sha(p)))
    return rows


def copy_file(source,dest):
    if source.is_symlink()or not source.is_file():raise ValueError('Not a regular input: '+str(source))
    dest.parent.mkdir(parents=True,exist_ok=True)
    if dest.exists():raise ValueError('No build output overwrite: '+str(dest))
    shutil.copy2(source,dest)
    if sha(source)!=sha(dest):raise ValueError('Copy hash mismatch: '+str(source))


def module_record(directory,game,version):
    path=directory/f'OpenCNC3D-module-{game}-macos-v{version}.zip'
    catalog=directory/f'OpenCNC3D-v{version}-modules.txt'
    lines=catalog.read_text(encoding='ascii').splitlines()
    if lines[:2]!=['modules 1',f'version {version}']:raise ValueError('Catalog version mismatch')
    matching=[x.split()for x in lines[2:]if x.startswith(f'module {game} macos ')]
    if len(matching)!=1:raise ValueError('Missing or repeated Mac catalog entry: '+game)
    _,code,platform,name,size,digest=matching[0]
    if name!=path.name or int(size)!=path.stat().st_size or digest!=sha(path):raise ValueError('Module/catalog SHA mismatch: '+str(path))
    manifest_name=f'opencnc3d-module-{game}.txt'
    with zipfile.ZipFile(path)as z:
        names=z.namelist()
        if len(set(n.lower()for n in names))!=len(names)or any(not safe_name(n)for n in names):raise ValueError('Unsafe/repeated module ZIP paths')
        text=z.read(manifest_name).decode('ascii').splitlines()
        if text[:4]!=['module-files 1',f'module {game}',f'version {version}','platform macos']:raise ValueError('Module identity mismatch')
        owned={}
        for line in text[4:]:
            kind,h,n,rel=line.split()
            if kind!='file'or not safe_name(rel)or rel in owned:raise ValueError('Invalid ownership manifest')
            info=z.getinfo(rel)
            if stat.S_IFMT(info.external_attr>>16)not in (0,stat.S_IFREG)or info.is_dir():raise ValueError('Module input is not regular')
            if PurePosixPath(rel).suffix.lower()in {'.cfg','.sav','.vqa','.log'}or any(p.lower()in {'saves','user_maps','settings','movies','music'}for p in PurePosixPath(rel).parts):raise ValueError('Player state or optional media in module')
            with z.open(info)as f:actual=hashlib.file_digest(f,'sha256').hexdigest()
            if actual!=h or info.file_size!=int(n):raise ValueError('Module member SHA mismatch: '+rel)
            owned[rel]=dict(size=int(n),sha256=h,mode=(info.external_attr>>16)&0o777)
        if set(names)!=set(owned)|{manifest_name}:raise ValueError('Unlisted module ZIP entries')
    required=({'ra3d/opencnc3d','ra3d/RedAlert.dylib','ra3d/dosmenu.pack','ra3d/SCG01EA.pack'}if game=='ra'else{'opencnc3d','TiberianDawn.dylib','cnc_eyes','CONQUER.MIX','dosmenu.pack'})
    if not required<=owned.keys():raise ValueError('Incomplete module: '+game)
    if game=='td'and any(p.startswith('ra3d/')or'RedAlert'in p for p in owned):raise ValueError('TD module contains RA')
    if game=='ra'and any('TiberianDawn'in p for p in owned):raise ValueError('RA module contains TD brain')
    return dict(game=game,path=str(path),size=path.stat().st_size,sha256=digest,files=owned)


def bootstrap(base,dest,version,kind):
    if(base/'ra3d').exists():raise ValueError('The base stage must be vetted without RA')
    for rel in BOOTSTRAP:
        p=base/rel
        if rel=='libSDL3.dylib'and not p.exists():continue
        copy_file(p,dest/rel)
    app=base/'OpenCNC3D.app'
    if not app.is_dir()or app.is_symlink():raise ValueError('Native app bundle missing')
    for row in file_rows(app):copy_file(app/row['path'],dest/'OpenCNC3D.app'/row['path'])
    # Fixed native bundle relocation rules are applied to pkgbuild separately.
    for name in ['CHANGELOG.txt']:
        if(base/name).is_file():copy_file(base/name,dest/name)
    (dest/'READ-ME-INSTALLATION.txt').write_text(
        f'{kind} v{version} for macOS\n\nOpen OpenCNC3D.app in your Applications/OpenCNC3D folder.\n'
        'The launcher manages Dawn3D and RA3D independently. Optional music and movies\n'
        'can be retried in the selected game\'s content settings. Saves, settings and\n'
        'custom maps are not part of this installer. No game starts during installation.\n'
        'These packages are unsigned unless the release receipt explicitly records a\n'
        'Developer ID Installer signature. Signing/notarisation are separate checks.\n',encoding='utf-8')
    marks=(dest/'opencnc3d-launcher').read_bytes()
    if version.encode()+b'\0'not in marks:raise ValueError('Launcher release version differs from package version')
    for key in [b'/api/builds',b'--module-file',b'--media-game',b'--media-add-only']:
        if key not in marks:raise ValueError('Launcher lacks the required native installer CLI: '+key.decode())
    for row in file_rows(dest):
        p=dest/row['path'];magic=p.read_bytes()[:4]if p.stat().st_size<64 else p.open('rb').read(4)
        if magic in [b'\xca\xfe\xba\xbe',b'\xcf\xfa\xed\xfe']:
            run('lipo',p,'-verify_arch','arm64','x86_64')


def welcome(resources,game,title,version,complete):
    resources.mkdir(parents=True)
    copy_file(ART[game],resources/'module.png')
    body=('This installer includes the complete '+('RA3D'if game=='ra'else'Dawn3D')+
          ' game. Choose optional Music and Movies in Customize. The game installs offline; only selected media downloads need the internet.'if complete else
          'Install the empty OpenCNC3D launcher. Open Add Game afterward to choose Dawn3D or RA3D, then select optional Music and Movies. This installer downloads no game or media.')
    page=f'''<!doctype html><html><head><meta charset="utf-8"><style>body{{font:13px -apple-system,Helvetica,sans-serif;color:#222;margin:12px}}td{{vertical-align:top}}h1{{font-size:20px;margin:3px 0 12px}}p{{line-height:1.45}}img{{width:160px;height:160px}}</style></head><body><table cellspacing="0" cellpadding="0"><tr><td style="padding-right:16px"><img src="module.png" alt="{'RA3D'if game=='ra'else'OpenCNC3D'}"></td><td><h1>{html.escape(title)}</h1><p>Version {html.escape(version)}</p><p>{html.escape(body)}</p><p>Installs for you in<br>Applications/OpenCNC3D.</p></td></tr></table></body></html>'''
    (resources/'welcome.html').write_text(page)
    (resources/'conclusion.html').write_text(page.replace(body,'Open OpenCNC3D.app in your Applications/OpenCNC3D folder. If optional media could not finish downloading, use the selected game’s content settings to retry. Your installed game remains usable.'))


def make_component(payload,scripts,out,identifier,version,components):
    args=['pkgbuild','--identifier',identifier,'--version',version,'--ownership','recommended','--install-location','/Applications/OpenCNC3D']
    if payload:args+=['--root',str(payload),'--component-plist',str(components)]
    else:args+=['--nopayload']
    if scripts:args+=['--scripts',str(scripts)]
    run(*args,out)


def verify_component(pkg,payload,work):
    expanded=work/'expanded-core'
    run('pkgutil','--expand-full',pkg,expanded)
    if file_rows(expanded/'Payload')!=file_rows(payload):raise ValueError('Native PKG payload differs from vetted inputs')
    info=ET.parse(expanded/'PackageInfo').getroot()
    if info.get('install-location')!='/Applications/OpenCNC3D':raise ValueError('Unexpected installation root')
    shutil.rmtree(expanded)


def distribution(path,title,version,core,media,identifiers):
    root=ET.Element('installer-gui-script',{'minSpecVersion':'2'})
    ET.SubElement(root,'title').text=title
    ET.SubElement(root,'options',{'customize':'always'if media else'never','require-scripts':'true','hostArchitectures':'arm64,x86_64'})
    ET.SubElement(root,'domains',{'enable_currentUserHome':'true','enable_localSystem':'false','enable_anywhere':'false'})
    ET.SubElement(root,'allowed-os-versions').append(ET.Element('os-version',{'min':'14.0'}))
    for kind in ['welcome','conclusion']:ET.SubElement(root,kind,{'file':kind+'.html','mime-type':'text/html'})
    outline=ET.SubElement(root,'choices-outline');ET.SubElement(outline,'line',{'choice':'core'})
    choice=ET.SubElement(root,'choice',{'id':'core','title':title,'start_selected':'true','enabled':'false'});ET.SubElement(choice,'pkg-ref',{'id':identifiers['core']})
    refs=[('core',core)]
    if media:
        for key,title0 in [('music','Music (optional download)'),('movies','Movies (optional download)')]:
            ET.SubElement(outline,'line',{'choice':key})
            ET.SubElement(root,'choice',{'id':key,'title':title0,'start_selected':'false','description':'Download only this game’s selected optional content. Missing media never prevents play.'})
        expressions={'none':'!choices.music.selected && !choices.movies.selected','music':'choices.music.selected && !choices.movies.selected','movies':'!choices.music.selected && choices.movies.selected','both':'choices.music.selected && choices.movies.selected'}
        for key,pkg in media.items():
            ET.SubElement(outline,'line',{'choice':'media_'+key})
            c=ET.SubElement(root,'choice',{'id':'media_'+key,'visible':'false','selected':expressions[key]})
            ET.SubElement(c,'pkg-ref',{'id':identifiers[key]});refs.append((key,pkg))
    for key,pkg in refs:ET.SubElement(root,'pkg-ref',{'id':identifiers[key],'version':version,'auth':'none','onConclusion':'None'}).text=pkg.name
    ET.ElementTree(root).write(path,encoding='utf-8',xml_declaration=True)


def installer_output(*args):
    result=subprocess.run(['installer',*map(str,args)],capture_output=True)
    if result.returncode:raise ValueError('Native Installer metadata query failed: '+result.stdout.decode(errors='replace')+result.stderr.decode(errors='replace'))
    return result.stdout

def native_choices(pkg,work,complete):
    result={}
    domains=installer_output('-dominfo','-pkg',pkg).decode().splitlines()
    if domains!=['CurrentUserHomeDirectory']:raise ValueError('Native installer offers an unexpected install domain: '+str(domains))
    if not complete:
        data=installer_output('-showChoicesXML','-pkg',pkg,'-target','CurrentUserHomeDirectory');(work/'choices-empty.plist').write_bytes(data)
        result['empty']=plistlib.loads(data)
        if any(x.get('choiceIdentifier')in ['music','movies']for x in result['empty']):raise ValueError('Empty installer must not offer media')
        return result
    for music in [False,True]:
        for movies in [False,True]:
            key=f'{int(music)}{int(movies)}';changes=work/(key+'.plist')
            changes.write_bytes(plistlib.dumps([dict(choiceIdentifier=n,choiceAttribute='selected',attributeSetting=int(v))for n,v in [('music',music),('movies',movies)]]))
            data=installer_output('-showChoicesAfterApplyingChangesXML',changes,'-pkg',pkg,'-target','CurrentUserHomeDirectory')
            rows=plistlib.loads(data)
            state={(x['choiceIdentifier'],x['choiceAttribute']):x['attributeSetting']for x in rows}
            for name in ['music','movies']:
                if not state.get((name,'enabled'))or not state.get((name,'visible')):raise ValueError('Native media choice is not interactive: '+str(state))
            selected={x['choiceIdentifier']:x['choiceAttribute']=='selected'and bool(x['attributeSetting'])for x in rows if x.get('choiceAttribute')=='selected'}
            expected='both'if music and movies else'music'if music else'movies'if movies else'none'
            if not selected.get('core')or [n for n in selected if n.startswith('media_')and selected[n]]!=['media_'+expected]:raise ValueError('Native Installer choice routing mismatch: '+str(selected))
            result[key]=rows
    return result


def build_installer(base,module,version,out,scratch,title,game):
    key='empty'if module is None else game;work=scratch/key;work.mkdir();payload=work/'payload';payload.mkdir()
    bootstrap(base,payload,version,title)
    if module:
        cache='ra3d'if game=='ra'else'td';directory=payload/'.opencnc3d-modules';directory.mkdir()
        copy_file(Path(module['path']),directory/(cache+'.zip'))
        if sha(directory/(cache+'.zip'))!=module['sha256']:raise ValueError('Module changed before native packaging')
        (directory/(cache+'.zip.sha256')).write_text(module['sha256']+'\n')
        (directory/(cache+'.zip.version')).write_text(version+'\n')
    rows=file_rows(payload)
    scripts=work/'core-scripts';scripts.mkdir()
    for name in ['preinstall','install-location.sh']:copy_file(HERE/name,scripts/name)
    (scripts/'payload-files.txt').write_text(''.join(r['path']+'\n'for r in rows))
    component_list=work/'components.plist'
    component_list.write_bytes(plistlib.dumps([dict(RootRelativeBundlePath='OpenCNC3D.app',BundleIsRelocatable=False,BundleIsVersionChecked=False,BundleOverwriteAction='update')]))
    core=work/'core.pkg';ids={'core':'com.slipgate.opencnc3d.native.'+key}
    make_component(payload,scripts,core,ids['core'],version,component_list);verify_component(core,payload,work)
    media={}
    if module:
        for key0,flags in [('none','none'),('music','music'),('movies','movies'),('both','movies,music')]:
            d=work/('scripts-'+key0);d.mkdir();copy_file(HERE/'install-location.sh',d/'install-location.sh')
            text=(HERE/'media-postinstall.in').read_text()
            for old,new in {'CACHE':cache,'CLI_MODULE':'ra3d'if game=='ra'else'td','GAME':game,'SHA':module['sha256'],'MEDIA':flags,'TITLE':title}.items():text=text.replace('@'+old+'@',new)
            (d/'postinstall').write_text(text);(d/'postinstall').chmod(0o755)
            media[key0]=work/('media-'+key0+'.pkg');ids[key0]='com.slipgate.opencnc3d.native.'+game+'.media.'+key0
            make_component(None,d,media[key0],ids[key0],version,component_list)
    resources=work/'resources';welcome(resources,game,title,version,module is not None)
    dist=work/'Distribution';distribution(dist,title,version,core,media,ids)
    artifact=out/f'{title}-macos-v{version}.pkg'
    command=['productbuild','--distribution',dist,'--resources',resources,'--package-path',work]
    identity=os.environ.get('OPENCNC3D_MAC_INSTALLER_IDENTITY')
    if identity:command+=['--sign',identity]
    run(*command,artifact)
    choices=native_choices(artifact,work,module is not None)
    # Reopen the actual product: verify Distribution and every included package
    # entry before reporting it. This reads a container, never runs Installer.
    expanded=work/'product-expanded';run('pkgutil','--expand',artifact,expanded)
    if ET.parse(expanded/'Distribution').getroot().find('domains').attrib!={'enable_currentUserHome':'true','enable_localSystem':'false','enable_anywhere':'false'}:raise ValueError('Native domains changed')
    for pkg in [core,*media.values()]:
        expected=work/(pkg.stem+'-original');run('pkgutil','--expand',pkg,expected)
        if file_rows(expected)!=file_rows(expanded/pkg.name):raise ValueError('Product changed component package: '+pkg.name)
        shutil.rmtree(expected)
    shutil.rmtree(expanded)
    return dict(name=artifact.name,path=str(artifact),size=artifact.stat().st_size,sha256=sha(artifact),kind='native-installer',game=module['game']if module else None,payload_files=rows,module_sha256=module['sha256']if module else None,media_choices=choices,install_domain='CurrentUserHomeDirectory',install_location='~/Applications/OpenCNC3D',bundle_relocation=False,media_invokes_scoped_headless_controller=True if module else False,source_art=str(ART[game].relative_to(ROOT)),source_art_sha256=sha(ART[game]),signature='Developer ID Installer requested'if identity else'unsigned; not notarised',gui_launched=False,system_install_performed=False)


def binary_zip(base,module,out,version,title):
    artifact=out/f'{title}-macos-v{version}.zip';included={}
    with zipfile.ZipFile(module['path'])as source,zipfile.ZipFile(artifact,'x',zipfile.ZIP_DEFLATED,compresslevel=6)as z:
        for rel,r in module['files'].items():
            p=PurePosixPath(rel)
            if not(p.suffix=='.dylib'or p.name in {'opencnc3d','cnc_eyes','netcheck'}):continue
            info=zipfile.ZipInfo(rel,(2026,1,1,0,0,0));info.external_attr=(stat.S_IFREG|r['mode'])<<16;info.compress_type=zipfile.ZIP_DEFLATED
            with source.open(rel)as src,z.open(info,'w')as dest:shutil.copyfileobj(src,dest)
            included[rel]=r
        extra=[base/'opencnc3d-launcher',base/'LICENSE',base/'NOTICE.md']+[base/r for r in BOOTSTRAP if r.endswith('.dylib')and(base/r).is_file()]
        app=base/'OpenCNC3D.app';extra += [app/r['path']for r in file_rows(app)]
        for p in extra:
            rel=p.relative_to(base).as_posix()
            if rel in included:continue
            info=zipfile.ZipInfo(rel,(2026,1,1,0,0,0));info.external_attr=(stat.S_IFREG|stat.S_IMODE(p.stat().st_mode))<<16;info.compress_type=zipfile.ZIP_DEFLATED
            with p.open('rb')as src,z.open(info,'w')as dest:shutil.copyfileobj(src,dest)
            included[rel]=dict(size=p.stat().st_size,sha256=sha(p),mode=stat.S_IMODE(p.stat().st_mode))
        z.writestr('BINARIES-README.txt',f'{title} v{version}\nApply over a complete matching module installation. This archive contains no game data, artwork packs, optional media, saves or settings.\n')
        z.writestr('BINARIES-MANIFEST.json',json.dumps(dict(version=version,game=module['game'],files=included),indent=2)+'\n')
    with zipfile.ZipFile(artifact)as z:
        if set(z.namelist())!=set(included)|{'BINARIES-README.txt','BINARIES-MANIFEST.json'}:raise ValueError('Unexpected binary ZIP entries')
        for rel,r in included.items():
            with z.open(rel)as f:
                if hashlib.file_digest(f,'sha256').hexdigest()!=r['sha256']:raise ValueError('Binary ZIP payload mismatch: '+rel)
    return dict(name=artifact.name,path=str(artifact),size=artifact.stat().st_size,sha256=sha(artifact),kind='binary-patch',game=module['game'],files=included,no_game_data_or_player_state=True)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--version',required=True);ap.add_argument('--base-stage',type=Path,required=True);ap.add_argument('--module-dir',type=Path,required=True);ap.add_argument('--out',type=Path,required=True)
    a=ap.parse_args();version=a.version.removeprefix('v')
    if not re.fullmatch(r'0\.\d+\.\d+',version):ap.error('exact release version required')
    a.base_stage=a.base_stage.resolve();a.module_dir=a.module_dir.resolve();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=True)
    for name in NAMES:
        if list(a.out.glob(name+f'-macos-v{version}.*')):ap.error('refusing to overwrite an existing artifact: '+name)
    stamp=(a.base_stage/'opencnc3d-install.txt').read_text().splitlines()
    if f'version {version}'not in stamp:ap.error('base package release version mismatch')
    modules={g:module_record(a.module_dir,g,version)for g in ['td','ra']}
    source_before=file_rows(a.base_stage);records=[]
    scratch_parent=ROOT/'build';scratch_parent.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='mac-native-release-',dir=scratch_parent)as temp:
        scratch=Path(temp)
        for name,game in [('OpenCNC3DInstaller',None),('OpenCNC3DComplete','td'),('RA3DComplete','ra')]:records.append(build_installer(a.base_stage,modules.get(game),version,a.out,scratch,name,game or'td'))
        for game,name in [('td','OpenCNC3DBinaries'),('ra','RA3DBinaries')]:records.append(binary_zip(a.base_stage,modules[game],a.out,version,name))
    if file_rows(a.base_stage)!=source_before:raise ValueError('Vetted source package changed during build')
    if any(sha(r['path'])!=r['sha256']for r in modules.values()):raise ValueError('Module ZIP changed during build')
    receipt=dict(version=version,source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),source_base_stage=str(a.base_stage),base_stage_files=source_before,catalog_path=str(a.module_dir/f'OpenCNC3D-v{version}-modules.txt'),catalog_sha256=sha(a.module_dir/f'OpenCNC3D-v{version}-modules.txt'),modules={g:{k:v for k,v in r.items()if k!='files'}for g,r in modules.items()},artifacts=records,all_five_named_outputs=True,native_container_and_choice_checks_passed=True,actual_user_install_not_performed=True,gui_launched=False,temporary_payloads_retired=True)
    receipt['builder_sources']={str(p.relative_to(ROOT)):sha(p)for p in sorted(HERE.iterdir())if p.is_file()and p.suffix!='.pyc'}
    receipt['wrapper_source_sha256']=sha(ROOT/'launcher/mac_launch.c')
    result=a.out/f'macos-package-receipt-v{version}.json'
    with result.open('x')as f:json.dump(receipt,f,indent=2);f.write('\n')
    for r in records:print(f"MAC-PACKAGE|{r['name']}|bytes={r['size']}|sha256={r['sha256']}")
    print('MAC-PACKAGES|PASS|five native/complete/binary artifacts; no GUI or system installation')

if __name__=='__main__':main()
