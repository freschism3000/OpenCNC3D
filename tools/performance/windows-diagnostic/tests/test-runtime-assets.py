#!/usr/bin/env python3
"""Offline packager and actual PowerShell helper tests, with no game/Windows execution."""
import argparse,hashlib,importlib.util,json,shutil,struct,subprocess,zipfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--tool-dir',type=Path,required=True);p.add_argument('--asset',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--pwsh',required=True);a=p.parse_args()
a.out.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('maker',a.tool_dir/'make-bundle.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
passed=[]
def check(v,n):
 if not v:raise AssertionError(n)
 passed.append(n);print('PACKAGERTEST|pass|'+n)
def reject(fn,n):
 try:fn()
 except (ValueError,OSError):check(True,n)
 else:check(False,n)
relative,data,entry=m.inspect_runtime_asset(a.asset)
check(relative=='ebld/FACT.ebld.fclv' and len(data)==149365,'approved asset inspected')
unknown=a.out/'PROC.ebld.fclv';unknown.write_bytes(data);reject(lambda:m.inspect_runtime_asset(unknown),'unknown source asset name rejected')
wrong=a.out/'FACT.ebld.fclv';wrong.write_bytes(data[:-1]+bytes([data[-1]^1]));reject(lambda:m.inspect_runtime_asset(wrong),'wrong asset SHA rejected')
wrong.write_bytes(data[:-1]);reject(lambda:m.inspect_runtime_asset(wrong),'wrong asset size rejected')
reject(lambda:m.inspect_runtime_asset(a.out/'missing/FACT.ebld.fclv'),'missing source asset rejected')
# Tiny PE identity fixtures exercise maker validation; they are never executed.
bindir=a.out/'fake-binaries';bindir.mkdir(exist_ok=True);blob=bytearray(512);blob[:2]=b'MZ';struct.pack_into('<I',blob,60,128);blob[128:132]=b'PE\0\0';struct.pack_into('<H',blob,132,0x14c);struct.pack_into('<H',blob,150,0x20);struct.pack_into('<H',blob,152,0x10b);blob.extend(b'OPENCNC3D|build|v0.8.4+aaaaaaa|cooked\0OPENCNC3D_PERFTRACE\0')
for name in ('opencnc3d-game.exe','cnc_eyes.exe'):(bindir/name).write_bytes(blob)
for tag,assets in [('with-asset',['--runtime-asset',str(a.asset)]),('without-asset',[])]:
 output=a.out/(tag+'.zip')
 r=subprocess.run(['python3',str(a.tool_dir/'make-bundle.py'),'--binary-dir',str(bindir),'--source-commit','a'*40,'--output',str(output)]+assets,text=True,capture_output=True)
 check(r.returncode==0,'packager '+tag+' succeeds')
 with zipfile.ZipFile(output) as z:
  manifest=json.loads(z.read('diagnostic-manifest.json'))
  check(z.testzip() is None and len(z.namelist())==(7 if assets else 6),'ZIP member count and CRC '+tag)
  if assets:
   check(z.read('assets/ebld/FACT.ebld.fclv')==data and manifest['runtime_assets'][relative]==entry,'asset ZIP bytes and manifest fingerprint match approved input')
  else:check(manifest.get('runtime_assets')=={},'optional assets empty for legacy-compatible no-overlay build')
# Test actual helper source through PowerShell AST extraction, never its main launcher.
fixtures=a.out/'fixtures';(fixtures/'bundle/assets/ebld').mkdir(parents=True,exist_ok=True)
(fixtures/'bundle/assets/ebld/FACT.ebld.fclv').write_bytes(data)
for name in ('original','private'):
 (fixtures/name/'ebld').mkdir(parents=True,exist_ok=True);(fixtures/name/'ebld/FACT.ebld.fclv').write_bytes(b'original installed sidecar')
(fixtures/'manifest.json').write_text(json.dumps({'runtime_assets':{relative:entry}}))
(fixtures/'legacy.json').write_text(json.dumps({'format':1,'launcher_revision':2}))
result=subprocess.run([a.pwsh,'-NoLogo','-NoProfile','-NonInteractive','-File',str(Path(__file__).with_suffix('.ps1')),'-ScriptPath',str(a.tool_dir/'Run-Windows-Diagnostic.ps1'),'-FixtureRoot',str(fixtures)],text=True,capture_output=True)
(a.out/'powershell.log').write_text(result.stdout+result.stderr);print(result.stdout);print(result.stderr)
check(result.returncode==0,'PowerShell helper/parser checks passed under strict mode')
script=(a.tool_dir/'Run-Windows-Diagnostic.ps1').read_text()
check(script.index('\nCopy-VerifiedRuntimeAssets $verifiedRuntimeAssets')>script.index('foreach ($key in @($before.Keys'),'asset overlay occurs after original-copy protected hash verification')
check(script.index('$verifiedRuntimeAssets = @(Get-VerifiedRuntimeAssets')<script.index('if (-not $InstallDir)'),'bundle asset validation precedes picker and source copying')
summary={'packager_passes':len(passed),'powershell_passes':result.stdout.count('ASSETTEST|pass|'),'no_native_game_or_installer_run':True,'asset':{'path':relative,**entry},'checks':passed}
(a.out/'checks.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
