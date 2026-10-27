#!/usr/bin/env python3
"""Exercise actual generated script bodies in disposable installs with an inert CLI.
The native destination resolver is substituted in the fixture only; no user-home
folder, actual launcher, network, game or GUI is touched.
"""
from pathlib import Path
import os,shutil,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[3];HERE=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='mac-install-script-fixture-',dir=ROOT/'build')as name:
 work=Path(name);install=work/'install with spaces';install.mkdir();scripts=work/'scripts';scripts.mkdir();log=work/'calls.txt'
 # This fixture replaces only destination resolution and privilege dropping. The
 # packaged postinstall/preinstall templates below remain the source under test.
 (scripts/'install-location.sh').write_text('resolve_install(){ install="$FIXTURE_INSTALL"; }; assert_real_parents(){ :; }; run_as_recipient(){ "$@"; };\n')
 launcher=install/'opencnc3d-launcher';launcher.write_text('''#!/bin/sh
printf '%s\\n' "$*" >> "$FIXTURE_LOG"
case "$*" in
 *--install-module*) exit "${FIXTURE_INSTALL_RC:-0}" ;;
 *) case " $* " in *' --media-add-only '*) ;; *)
      rm -f "$FIXTURE_INSTALL/dosdata/movies/existing.VQA"
      printf 'movies no\\nmusic no\\n' > "$FIXTURE_INSTALL/opencnc3d-media-td.txt" ;;
    esac
    exit "${FIXTURE_MEDIA_RC:-0}" ;;
esac
''');launcher.chmod(0o755)
 env=dict(os.environ,FIXTURE_INSTALL=str(install),FIXTURE_LOG=str(log))
 existing={}
 for game in ['td','ra']:
  for rel in ([f'dosdata/movies/existing.VQA','dosdata/SCORES.MIX']if game=='td'else['ra3d/movies/existing.VQA','ra3d/audio/SCORES.MIX']):
   q=install/rel;q.parent.mkdir(parents=True,exist_ok=True);q.write_bytes(('KEEP '+rel).encode());existing[rel]=q.read_bytes()
  rel=f'opencnc3d-media-{game}.txt';q=install/rel;q.write_text('movies yes\nmusic yes\nmovies_state complete\nmusic_state complete\n');existing[rel]=q.read_bytes()

 for game,cli,cache in [('td','td','td'),('ra','ra3d','ra3d')]:
  (install/'.opencnc3d-modules').mkdir(exist_ok=True);(install/'.opencnc3d-modules'/(cache+'.zip')).write_bytes(b'inert')
  for media in ['none','music','movies','movies,music']:
   text=(HERE/'media-postinstall.in').read_text()
   for key,val in {'CACHE':cache,'CLI_MODULE':cli,'SHA':'a'*64,'MEDIA':media,'GAME':game,'TITLE':'Fixture'}.items():text=text.replace('@'+key+'@',val)
   script=scripts/'postinstall';script.write_text(text);script.chmod(0o755)
   log.write_text('');subprocess.run([str(script),'fixture.pkg',str(install),str(work)],env=env,check=True,capture_output=True)
   calls=log.read_text().splitlines();assert len(calls)==2 and f'--install-module {cli}'in calls[0]and'--module-sha '+'a'*64 in calls[0]
   assert f'--media-install {media} --media-game {game} --media-add-only'in calls[1]
   assert all((install/rel).read_bytes()==data for rel,data in existing.items()),'Existing media/preference changed'
  log.write_text('');r=subprocess.run([str(script),'fixture.pkg',str(install),str(work)],env=dict(env,FIXTURE_INSTALL_RC='1'),capture_output=True);assert r.returncode!=0 and len(log.read_text().splitlines())==1
  log.write_text('');r=subprocess.run([str(script),'fixture.pkg',str(install),str(work)],env=dict(env,FIXTURE_MEDIA_RC='1'),capture_output=True);assert r.returncode==0 and b'needs retry'in r.stderr
 # Negative control: the old destructive invocation must fail preservation.
 old=scripts/'old-postinstall';old.write_text(script.read_text().replace(' --media-add-only',''));old.chmod(0o755)
 subprocess.run([str(old),'fixture.pkg',str(install),str(work)],env=env,check=True,capture_output=True)
 assert not all((install/rel).is_file()and(install/rel).read_bytes()==data for rel,data in existing.items())
 for rel,data in existing.items():(install/rel).write_bytes(data)
 # Keep native preflight path/link/journal logic; replace only the OS process
 # inventory command so this CPU fixture needs no process-visibility privilege.
 text=(HERE/'preinstall').read_text().replace('/usr/sbin/lsof -t +D "$install"',':')
 pre=scripts/'preinstall';pre.write_text(text);pre.chmod(0o755)
 (scripts/'payload-files.txt').write_text('opencnc3d-launcher\nsub/asset.pack\n')
 protected=install/'ra3d-fx.cfg';protected.write_text('CURRENT USER CFG\n')
 args=[str(pre),'fixture.pkg',str(install),str(work)]
 subprocess.run(args,env=env,check=True,capture_output=True)
 (install/'sub').symlink_to(work)
 assert subprocess.run(args,env=env,capture_output=True).returncode!=0
 (install/'sub').unlink();(install/'opencnc3d-update.journal').write_text('incomplete')
 assert subprocess.run(args,env=env,capture_output=True).returncode!=0
 assert protected.read_text()=='CURRENT USER CFG\n'
 print('MAC-INSTALLER-SCRIPTS|PASS|eight additive media combinations preserve existing media/preferences; destructive-call negative control; offline-before-media order; base-failure stop; media retry; linked target/journal refusal; private CFG preserved; no real destination/network/GUI')
