#!/usr/bin/env python3
"""Exercise the real Windows packager with small PE/data fixtures; no game is run."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import re
import struct
import subprocess
import tempfile
import zipfile

HERE = Path(__file__).resolve().parent
FILES = ('tools/win/make-build-win.sh', 'tools/win/check-diagnostics.py',
         'tools/win/installer/readme-windows.sh', 'tools/launcher/data-id.sh',
         # The package's NOTICE.md is written by make-package-notice.py from these.
         'tools/make-package-notice.py', 'brain/patches/vanilla-opencnc3d.patch',
         'brain/patches/UPSTREAM.txt', 'brain/vanilla/License.txt')
CHECKS = []
FRESH_DECALS = b'fresh authored decal producer fixture; package bytes only'
SESSION_SENTINEL = b'NEVER_SHIP_RUNTIME_DIAGNOSTICS: private fixture contents'


def check(condition, name, details=''):
    CHECKS.append({'check': name, 'pass': bool(condition)})
    if not condition:
        raise AssertionError(name + ('\n' + details if details else ''))


def pe(mark, diagnostic=False, machine=0x14c, laa=True, subsystem=3, extra=b''):
    data = bytearray(512)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 60, 128)
    data[128:132] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', data, 132, machine, 1, 0, 0, 0, 224, 0x102 | (0x20 if laa else 0))
    struct.pack_into('<H', data, 152, 0x10b)
    struct.pack_into('<H', data, 220, subsystem)
    data.extend(b'\0' + mark.encode() + b'\0unavailable in a cooked build\0')
    if diagnostic:
        data.extend(b'OPENCNC3D|diagnostics|1\0')
    data.extend(extra)
    return bytes(data)


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(value if isinstance(value, bytes) else value.encode())


def run(command, env=None):
    return subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


def prepare(root, repo):
    for name in FILES:
        write(root / name, (repo / name).read_bytes())
    for name in ('LICENSE', 'NOTICE.md'):
        write(root / name, (repo / name).read_bytes())
    # The notice names every engine file the patch modifies, and refuses one that is not
    # in the tree, so the fixture carries those files too.
    patch = (repo / 'brain/patches/vanilla-opencnc3d.patch').read_text(encoding='utf-8')
    for modified in sorted(set(re.findall(r'^diff --git a/\S+ b/(\S+)$', patch, re.MULTILINE))):
        if (repo / 'brain/vanilla' / modified).is_file():
            write(root / 'brain/vanilla' / modified, (repo / 'brain/vanilla' / modified).read_bytes())
    write(root / 'tools/win/build-win.sh', '''#!/bin/sh
set -e
mkdir -p build/win
cp "$PACKAGE_TEST_BIN"/* build/win/
''')
    write(root / 'tools/win/build-launcher-win.sh', '#!/bin/sh\nexit 0\n')
    # Asset production is stubbed here; the actual structural builder is tested separately.
    write(root / 'tools/art/remaster/decals/build-runtime.py', """#!/usr/bin/env python3
import argparse,os,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--pack',required=True);a=p.parse_args()
mode=os.environ.get('PACKAGE_TEST_DECAL_FAILURE','')
if mode=='broken':sys.exit('Enhanced decal build refused: broken authored-input fixture')
if mode=='empty':Path(a.pack).write_bytes(b'');sys.exit(0)
if mode=='missing':sys.exit(0)
Path(a.pack).write_bytes(b'fresh authored decal producer fixture; package bytes only')
""")
    write(root / 'tools/art/rocks/stones.sh', '''#!/bin/sh
printf stones > "$1/rock3d_fixture.pack"
if [ "${PACKAGE_TEST_INJECT_LOGS:-0}" = 1 ]; then
    mkdir -p "$1/Logs/session-leak"
    printf 'must not ship' > "$1/Logs/session-leak/seconds.csv"
fi
''')
    write(root / 'tools/launcher/make-changelog-txt.sh', '#!/bin/sh\nprintf "Fixture changelog\\n" > "$1"\n')
    for name in ('tools/win/build-win.sh', 'tools/win/build-launcher-win.sh',
                 'tools/launcher/data-id.sh', 'tools/launcher/make-changelog-txt.sh'):
        (root / name).chmod(0o755)
    source = root / 'playable-fixture'
    for name in ('dosclassic.pack', 'CONQUER.MIX', 'dosmenu.pack', 'enhanced-menu.pack',
                 'dossidebar.pack', 'dosinfantry.pack', 'cameos.pack'):
        write(source / name, name + ': fixture')
    write(source / 'decalenh.pack', b'stale installation decal bytes must never ship')
    for code in ('SCG01EA', 'SCB01EA', 'SCW01EA', 'SCS01EA', 'SCA01EA', 'SCM01EA'):
        write(source / (code + '.pack'), code + ': fixture pack')
        write(source / 'missions' / (code + '.INI'), '[Basic]\nName=Fixture\n')
    for name in ('TEMPERAT.MIX', 'DESERT.MIX'):
        write(source / 'content' / name, name + ': fixture')
    for name in ('SOUNDS.MIX', 'SPEECH.MIX'):
        write(source / 'dosdata' / name, name + ': fixture')
    write(source / 'dosdata/movies/fixture.VQA', b'media must be excluded')
    write(source / 'missions/user_maps/private.INI', 'personal map must be excluded')
    for name in ('proof.png', 'settings.cfg', 'SAVEG000.SAV', 'ignored-binary.dll',
                 'Logs/session-old/private.txt', 'other.app/Contents/MacOS/other'):
        write(source / name, SESSION_SENTINEL if name.startswith('Logs/') else 'unrelated fixture must not enter the package')
    for name in ('seconds.csv', 'hardware-and-process.csv', 'settings-0.cfg', 'status.json'):
        write(source / 'Logs/session-fixture-UTC' / name, SESSION_SENTINEL)
    for directory, suffix in (('ebld', '.ebld'), ('eplc', '.eplc'), ('evhc', '.evhc'),
                              ('einf', '.c3m'), ('n64fix', '.n64x')):
        write(root / 'game' / directory / ('FIXTURE' + suffix), directory + ': fixture')
    write(root / 'game/ecur/ecur.bin', b'cursor fixture')
    return source


def binaries(root, version):
    target = root / 'input-bin'
    mark = 'OPENCNC3D|build|v' + version + '|cooked'
    write(target / 'opencnc3d-game.exe', pe(mark))
    write(target / 'cnc_eyes.exe', pe(mark, extra=b'edit: world grid\0'))
    write(target / 'OpenCNC3D-Diagnostics.exe', pe(mark, True, subsystem=2))
    write(target / 'OpenCNC3D.exe', pe(mark, extra=b'/api/builds\0cnc_eyes\0--edit\0'))
    for name in ('netcheck.exe', 'SDL2.dll', 'TiberianDawn.dll', 'ignored-binary.dll'):
        write(target / name, b'fixture ' + name.encode())
    write(root / 'VERSION', version + '\n')
    return target, mark


def test_packages(repo, work):
    root = work / 'fixture-repo'
    source = prepare(root, repo)
    patch = (root / 'brain/patches/vanilla-opencnc3d.patch').read_text(encoding='utf-8')
    notice_inventory = list(dict.fromkeys(name for name in re.findall(r'^diff --git a/\S+ b/(\S+)$', patch, re.MULTILINE)
                                         if name.startswith(('common/', 'tiberiandawn/'))))
    check(bool(notice_inventory), 'notice: real source patch contains packaged-module inventory')
    zips = work / 'archives'
    zips.mkdir()
    for version in ('0.8.4', '0.9.0', '12.45.67'):
        if version == '0.9.0':
            (source / 'decalenh.pack').unlink(missing_ok=True)
        else:
            write(source / 'decalenh.pack', b'stale installation decal bytes must never ship')
        target, mark = binaries(root, version)
        env = dict(os.environ, OPENCNC3D_PLAYABLE=str(source), OPENCNC3D_WIN_ZIP=str(zips),
                   OPENCNC3D_BUILD_ID='v' + version, OPENCNC3D_COOKED='1',
                   OPENCNC3D_NO_RA='1', PACKAGE_TEST_BIN=str(target),PACKAGE_TEST_DECAL_FAILURE='')
        for kind, args in (('full', []), ('bins', ['--bins-only'])):
            name = version + '-' + kind
            result = run(['sh', str(root / 'tools/win/make-build-win.sh')] + args, env)
            write(work / (name + '.log'), result.stdout)
            check(result.returncode == 0, name + ': real packager succeeds', result.stdout)
            archive = zips / ('OpenCNC3D-windows-v' + version + ('-bins' if kind == 'bins' else '') + '.zip')
            with zipfile.ZipFile(archive) as z:
                members = z.namelist()
                prefix = '' if kind == 'bins' else 'OpenCNC3D-windows-v' + version + '/'
                check(z.testzip() is None, name + ': complete ZIP CRCs')
                check(z.read(prefix+'decalenh.pack') == FRESH_DECALS, name + ': freshly baked required decal pack is present and stale source ignored')
                expected = prefix + 'OpenCNC3D-Diagnostics.exe'
                check(members.count(expected) == 1, name + ': dedicated EXE present exactly once')
                check(z.read(expected) == (target / 'OpenCNC3D-Diagnostics.exe').read_bytes(), name + ': EXE bytes preserved')
                check(mark.encode() in z.read(expected), name + ': requested bare release build mark')
                check(b'OPENCNC3D|diagnostics|1' in z.read(expected), name + ': automatic entry marker')
                check(all('/' not in member for member in members) if kind == 'bins' else all(member.startswith(prefix) for member in members), name + ': expected archive shape')
                check(members[-2:] == [prefix + 'BUILD-ID.txt', prefix + 'SDL2.dll'], name + ': legacy update tail remains last')
                if kind == 'full':
                    check(members[-4:] == [prefix + item for item in ('CHANGELOG.txt', 'opencnc3d-install.txt', 'BUILD-ID.txt', 'SDL2.dll')], name + ': full install record tail unchanged')
                readme = z.read(prefix + ('READ-ME-FIRST.txt' if kind == 'bins' else 'READ-ME-WINDOWS.txt')).decode()
                check(all(word in readme for word in ('OpenCNC3D-Diagnostics.exe', 'Logs/session-', '%LOCALAPPDATA%', 'sharing')), name + ': normal collection instructions')
                check(not any('/Logs/' in ('/' + member) for member in members), name + ': runtime Logs/session directory absent')
                check(not any(SESSION_SENTINEL in z.read(member) for member in members if not member.endswith('/')), name + ': sensitive diagnostic sentinel absent from all member bytes')
                check(not any(token in member for member in members for token in ('private.INI', 'private.txt', 'proof.png', 'settings.cfg', 'SAVEG000', 'ignored-binary', 'other.app', '.VQA')), name + ': personal/media/unrelated inputs excluded')
                if kind == 'bins':
                    expected_files = {'OpenCNC3D.exe', 'opencnc3d-game.exe', 'OpenCNC3D-Diagnostics.exe', 'cnc_eyes.exe',
                                      'netcheck.exe', 'SDL2.dll', 'TiberianDawn.dll', 'dosclassic.pack', 'CONQUER.MIX',
                                      'dosmenu.pack', 'enhanced-menu.pack', 'decalenh.pack', 'READ-ME-FIRST.txt', 'BUILD-ID.txt'}
                    check(set(members) == expected_files and len(members)==14, name + ': exact fourteen-file flat update include list')
                    check('decalenh.pack' in readme,name+': binaries-only instructions carry the required decal update')
                else:
                    check(all(prefix + item in members for item in ('ebld/FIXTURE.ebld', 'evhc/FIXTURE.evhc', 'ecur/ecur.bin', 'content/TEMPERAT.MIX', 'missions/SCG01EA.INI', 'LICENSE', 'NOTICE.md')), name + ': normal runtime data and legal files retained')
                    notice = z.read(prefix + 'NOTICE.md').decode()
                    terms = (root / 'brain/vanilla/License.txt').read_text(encoding='utf-8')
                    terms = terms[terms.index('ADDITIONAL TERMS per GNU GPL Section 7'):].rstrip()
                    original = (root / 'NOTICE.md').read_text(encoding='utf-8')
                    art_start = original.index('## Third-party art committed here')
                    art_end = original.index('\n## ', art_start + 1)
                    check(terms in notice and original[art_start:art_end].rstrip() in notice,
                          name + ': generated notice retains original EA terms and art attribution')
                    check(re.findall(r'^    ((?:common|tiberiandawn)/[^\r\n]+)$', notice, re.MULTILINE) == notice_inventory
                          and not re.search(r'red\s*alert|redalert|ra3d|ra_bootstrap', notice, re.IGNORECASE),
                          name + ': generated notice inventory is limited to packaged modules')
                    notice_words = ' '.join(notice.split())
                    check('modified versions' in notice_words and 'not the original program' in notice_words,
                          name + ': generated notice marks source modifications')
            fingerprint = run([str(root / 'tools/launcher/data-id.sh'), str(root / 'build/win-dist' / ('OpenCNC3D-windows-v' + version))]).stdout.strip()
            staged = root / 'build/win-dist' / ('OpenCNC3D-windows-v' + version)
            write(staged / 'OpenCNC3D-Diagnostics.exe', b'different logger size and code')
            changed = run([str(root / 'tools/launcher/data-id.sh'), str(staged)]).stdout.strip()
            check(fingerprint == changed and len(fingerprint) == 64, name + ': logger updates do not alter data fingerprint')
            write(staged / 'Logs/session-runtime/seconds.csv', SESSION_SENTINEL)
            write(staged / 'lOgS/session-other/status.json', SESSION_SENTINEL)
            logged = run([str(root / 'tools/launcher/data-id.sh'), str(staged)]).stdout.strip()
            check(fingerprint == logged, name + ': runtime diagnostic Logs do not alter data fingerprint')
            write(staged / 'unrelated-runtime-data.bin', b'changed data')
            data_changed = run([str(root / 'tools/launcher/data-id.sh'), str(staged)]).stdout.strip()
            check(fingerprint != data_changed, name + ': real data change still alters fingerprint')
        if version == '12.45.67':
            for corrupt in ('missing-entry-marker', 'matching-stale-version'):
                binaries(root, version)
                if corrupt == 'missing-entry-marker':
                    write(target / 'OpenCNC3D-Diagnostics.exe', pe(mark, subsystem=2))
                else:
                    stale = 'OPENCNC3D|build|v12.45.66|cooked'
                    write(target / 'opencnc3d-game.exe', pe(stale))
                    write(target / 'cnc_eyes.exe', pe(stale, extra=b'edit: world grid\0'))
                    write(target / 'OpenCNC3D-Diagnostics.exe', pe(stale, True, subsystem=2))
                for kind, args in (('full', []), ('bins', ['--bins-only'])):
                    result = run(['sh', str(root / 'tools/win/make-build-win.sh')] + args, env)
                    write(work / (version + '-' + corrupt + '-' + kind + '.log'), result.stdout)
                    check(result.returncode != 0 and 'Windows diagnostic package refused' in result.stdout, version + '-' + kind + ': actual packager rejects ' + corrupt)
            binaries(root, version)
        for failure in ('broken','empty','missing'):
            for kind,args in (('full',[]),('bins',['--bins-only'])):
                # Remove previous successful output so an old archive cannot mask a failure.
                archive=zips/('OpenCNC3D-windows-v'+version+('-bins' if kind=='bins' else '')+'.zip')
                archive.unlink(missing_ok=True)
                result=run(['sh',str(root/'tools/win/make-build-win.sh')]+args,dict(env,PACKAGE_TEST_DECAL_FAILURE=failure))
                write(work/(version+'-'+failure+'-decals-'+kind+'.log'),result.stdout)
                check(result.returncode!=0 and (('Enhanced decal build refused' in result.stdout) if failure=='broken' else ('decal' in result.stdout.lower())),version+'-'+kind+': required decal production rejects '+failure+' producer result')
                check(not archive.exists(),version+'-'+kind+': failed decal production cannot publish an archive')
        injection_env = dict(env, PACKAGE_TEST_INJECT_LOGS='1')
        result = run(['sh', str(root / 'tools/win/make-build-win.sh')], injection_env)
        write(work / (version + '-injected-logs.log'), result.stdout)
        check(result.returncode != 0 and 'runtime diagnostic Logs' in result.stdout, version + ': accidentally staged Logs refuse full distribution')
        (target / 'OpenCNC3D-Diagnostics.exe').unlink()
        for kind, args in (('full', []), ('bins', ['--bins-only'])):
            shutil.rmtree(root / 'build/win')
            result = run(['sh', str(root / 'tools/win/make-build-win.sh')] + args, env)
            write(work / (version + '-missing-' + kind + '.log'), result.stdout)
            check(result.returncode != 0 and 'OpenCNC3D-Diagnostics.exe' in result.stdout, version + '-' + kind + ': missing dedicated EXE fails closed')
    binaries(root, version)
    archive = zips / ('OpenCNC3D-windows-v' + version + '.zip')
    archive.unlink(missing_ok=True)
    missing_notice_source = notice_inventory[0]
    (root / 'brain/vanilla' / missing_notice_source).unlink()
    result = run(['sh', str(root / 'tools/win/make-build-win.sh')], env)
    write(work / 'missing-notice-source.log', result.stdout)
    check(result.returncode != 0 and 'source inventory names a missing file: ' + missing_notice_source in result.stdout,
          'notice: missing inventory source fails closed', result.stdout)
    check(not archive.exists(), 'notice: failed notice cannot publish an archive')


def test_identity(repo, work):
    spec = importlib.util.spec_from_file_location('check_diagnostics', repo / 'tools/win/check-diagnostics.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    target = work / 'identity-bin'
    mark = 'OPENCNC3D|build|v27.0.123|cooked'
    def reset():
        write(target / 'opencnc3d-game.exe', pe(mark))
        write(target / 'cnc_eyes.exe', pe(mark))
        write(target / 'OpenCNC3D-Diagnostics.exe', pe(mark, True, subsystem=2))
    reset()
    check(module.validate(target, True, 'v27.0.123') == mark, 'identity: arbitrary future bare cooked release accepted')
    try:module.validate(target, True, 'v27.0.124')
    except ValueError:rejected=True
    else:rejected=False
    check(rejected, 'identity: matching but stale entries reject requested release version')
    reset()
    for name in module.NAMES:
        write(target / name, pe('OPENCNC3D|build|v27.0.123+abcdef0-dirty', name == module.NAMES[2], subsystem=2 if name == module.NAMES[2] else 3))
    check(module.validate(target, False).endswith('-dirty'), 'identity: matching development build accepted')
    cases = {
        'wrong version': pe('OPENCNC3D|build|v0.0.1|cooked', True, subsystem=2),
        'missing entry marker': pe(mark, subsystem=2),
        'wrong architecture': pe(mark, True, machine=0x8664, subsystem=2),
        'missing LAA': pe(mark, True, laa=False, subsystem=2),
        'console instead of GUI': pe(mark, True),
        'conflicting build marks': pe(mark, True, subsystem=2, extra=b'OPENCNC3D|build|v0.0.1|cooked\0'),
        'truncated PE': b'MZ',
    }
    for name, data in cases.items():
        reset();write(target / 'OpenCNC3D-Diagnostics.exe', data)
        try:module.validate(target, True)
        except (OSError, ValueError, UnicodeError, struct.error):rejected=True
        else:rejected=False
        check(rejected, 'identity: reject ' + name)
    reset();write(target / 'opencnc3d-game.exe', pe(mark, True))
    try:module.validate(target, True)
    except ValueError:rejected=True
    else:rejected=False
    check(rejected, 'identity: automatic logging cannot replace the normal entry')
    reset()
    for name in module.NAMES:
        write(target / name, pe('OPENCNC3D|build|v27.0.123', name == module.NAMES[2], subsystem=2 if name == module.NAMES[2] else 3))
    try:module.validate(target, True)
    except ValueError:rejected=True
    else:rejected=False
    check(rejected, 'identity: requested cooked build rejects uncooked entries')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=HERE.parents[1])
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='opencnc3d-package-test-') as temporary:
        work = Path(temporary)
        try:
            test_packages(args.repo.resolve(), work)
            test_identity(args.repo.resolve(), work)
        finally:
            if args.output:
                args.output.mkdir(parents=True, exist_ok=True)
                for log in work.glob('*.log'):shutil.copy2(log, args.output / log.name)
        result = {'status': 'PASS', 'checks_passed': len(CHECKS), 'checks': CHECKS,
                  'game_executed': False, 'scope': 'Actual package assembly with stub compiler and asset-producer commands and bounded synthetic PE/data fixtures; decal structural production is tested separately, and no native Windows game is executed.',
                  'source_hashes': {name: hashlib.sha256((args.repo / name).read_bytes()).hexdigest() for name in FILES}}
        if args.output:
            (args.output / 'verification.json').write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
