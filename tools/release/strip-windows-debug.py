#!/usr/bin/env python3
"""Strip debug information only from explicitly marked fresh Windows public stages.

Run prepare with a separate unstripped reference directory, then apply its external
one-shot marker. The public-stage token remains for the subsequent asset reducer
to consume. Build outputs and vendor libraries are never modified. Static PE
equivalence is a packaging check, not a native Windows execution test.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import subprocess
import tempfile

TOKEN = '.opencnc3d-public-stage'
TOKEN_BYTES = b'OpenCNC3D fresh public package v1\n'
PROGRAMS = frozenset({'OpenCNC3D.exe', 'opencnc3d-game.exe', 'cnc_eyes.exe',
    'OpenCNC3D-Diagnostics.exe', 'TiberianDawn.dll', 'RedAlert.dll',
    'netcheck.exe', 'ra_bootstrap.exe'})
FRONTENDS = frozenset({'opencnc3d-game.exe', 'cnc_eyes.exe', 'OpenCNC3D-Diagnostics.exe'})
ROOT = Path(__file__).resolve().parents[2]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def regular(path):
    s = path.lstat()
    require(stat.S_ISREG(s.st_mode) and not path.is_symlink(), 'not a regular file: ' + str(path))
    return s


def identity(path):
    s = regular(path)
    return dict(sha256=sha(path), bytes=s.st_size, mtime_ns=s.st_mtime_ns,
                mode=stat.S_IMODE(s.st_mode))


def real_path(value):
    path = Path(os.path.abspath(value))
    require(path == path.resolve(), 'symlink path component: ' + str(path))
    return path


def external(path, stage, reference):
    require(not path.is_relative_to(stage) and not path.is_relative_to(reference),
            'control files must be outside the stage and reference directory')
    require(path.parent.is_dir(), 'control-file parent does not exist')


def stage_guard(stage, reference):
    require(stage.is_dir() and reference.is_dir(), 'stage/reference must be real directories')
    require(not stage.is_relative_to(reference) and not reference.is_relative_to(stage),
            'stage and reference must be separate directories')
    require(stage != ROOT and not ROOT.is_relative_to(stage), 'source tree is not a public stage')
    for name in ('CNC3D', 'CNC3D-latest'):
        runtime = Path.home()/name/'playable'
        for installed in (runtime, runtime.resolve()):
            require(not stage.is_relative_to(installed) and not installed.is_relative_to(stage),
                    'an installed runtime or its ancestor is not a public stage')
    require(not (stage / '.git').exists(), 'a Git checkout is not a public stage')
    regular(stage / TOKEN)
    require((stage / TOKEN).read_bytes() == TOKEN_BYTES, 'missing fresh public-stage authorization')
    for folder, dirs, files in os.walk(stage, followlinks=False):
        for name in dirs+files:
            path = Path(folder)/name
            require(not path.is_symlink(), 'symlinks are not fresh public payloads')
            lower = name.lower()
            empty_public_maps = (path.relative_to(stage).as_posix() == 'missions/user_maps'
                and path.is_dir() and not any(path.iterdir()))
            require((lower not in ('saves', 'savegames', 'settings', 'user_maps') or empty_public_maps)
                    and not lower.endswith(('.cfg', '.sav'))
                    and not lower.startswith(('savegame', 'quicksave', 'autosave')),
                    'personal settings/saves are not public-stage inputs: ' + name)


def pe(path):
    """Parse bounded PE32 structures and hash exact loader-visible section bytes."""
    regular(path)
    require(path.stat().st_size <= 512 * 1024 * 1024, 'PE input exceeds the packaging limit')
    b = path.read_bytes()
    require(len(b) >= 64 and b[:2] == b'MZ', 'missing DOS header')
    off = struct.unpack_from('<I', b, 60)[0]
    require(64 <= off <= len(b)-24 and b[off:off+4] == b'PE\0\0', 'missing PE signature')
    machine, nsec, timestamp, symptr, nsyms, optsize, flags = struct.unpack_from('<HHIIIHH', b, off+4)
    opt = off+24
    require(machine == 0x14c and 1 <= nsec <= 96, 'expected bounded i686 PE')
    require(optsize == 224 and opt+optsize+40*nsec <= len(b), 'unexpected optional/section header')
    require(struct.unpack_from('<H', b, opt)[0] == 0x10b, 'expected PE32 optional header')
    require(struct.unpack_from('<I', b, opt+92)[0] == 16, 'unexpected PE directory count')
    directories = [list(struct.unpack_from('<II', b, opt+96+8*i)) for i in range(16)]
    require(directories[4] == [0, 0], 'signed inputs require a separate signing workflow')
    strings = b''
    if symptr:
        start = symptr+nsyms*18
        require(start+4 <= len(b), 'COFF symbol table out of bounds')
        size = struct.unpack_from('<I', b, start)[0]
        require(size >= 4 and start+size <= len(b), 'COFF string table out of bounds')
        strings = b[start:start+size]
    sections = {}
    for i in range(nsec):
        h = opt+optsize+40*i
        name = b[h:h+8].split(b'\0')[0]
        if name.startswith(b'/'):
            require(name[1:].isdigit(), 'invalid long section name')
            pos = int(name[1:])
            require(4 <= pos < len(strings) and b'\0' in strings[pos:], 'section name out of bounds')
            name = strings[pos:].split(b'\0')[0]
        name = name.decode('ascii')
        require(name and name not in sections, 'empty or duplicate section')
        vs, rva, size, ptr, relptr, lineptr, nrel, nline, sf = struct.unpack_from('<IIIIIIHHI', b, h+8)
        require(vs <= 512*1024*1024 and ptr+size <= len(b), 'section data out of bounds')
        require(nrel == 0 and nline == 0, 'executable contains COFF relocations/line tables')
        data = b[ptr:ptr+size] if size else b''
        sections[name] = dict(virtual_size=vs, rva=rva, raw_size=size, raw_offset=ptr,
            characteristics=sf, raw_sha256=hashlib.sha256(data).hexdigest(),
            virtual_sha256=hashlib.sha256(data[:vs]+b'\0'*max(0, vs-size)).hexdigest(),
            padding_bytes=max(0, size-vs), padding_zero=not any(data[vs:]))
    section_alignment, file_alignment = struct.unpack_from('<II', b, opt+32)
    image_size, header_size = struct.unpack_from('<II', b, opt+56)
    require(section_alignment and file_alignment and not file_alignment & (file_alignment-1),
            'invalid PE alignment')
    image_end = max(s['rva']+max(s['virtual_size'], s['raw_size']) for s in sections.values())
    expected_image_size = ((image_end+section_alignment-1)//section_alignment)*section_alignment
    require(image_size == expected_image_size, 'PE image size does not cover its section layout exactly')
    header_end = opt+optsize+40*nsec
    raw_offsets = [s['raw_offset'] for s in sections.values() if s['raw_size']]
    require(raw_offsets and header_end <= header_size <= min(raw_offsets)
            and header_size % file_alignment == 0, 'PE header size does not fit its file layout')
    marks = sorted(x.decode('ascii') for x in set(re.findall(
        rb'OPENCNC3D\|build\|v[0-9A-Za-z.+-]+\|[A-Za-z0-9_-]+', b)))
    return dict(machine=machine, timestamp=timestamp, flags=flags,
        symbol_pointer=symptr, symbol_count=nsyms, dos_sha256=hashlib.sha256(b[:off]).hexdigest(),
        optional_header=b[opt:opt+optsize].hex(), directories=directories,
        sections=sections, cooked_markers=marks)


def is_debug(name):
    return name.startswith(('.debug_', '.zdebug_'))


def equivalent(before, after):
    for key in ('machine', 'timestamp', 'dos_sha256', 'cooked_markers'):
        require(before[key] == after[key], 'PE runtime field changed: ' + key)
    require((before['flags'] ^ after['flags']) & ~0x20c == 0, 'PE loader characteristics changed')
    a = bytearray.fromhex(before['optional_header'])
    b = bytearray.fromhex(after['optional_header'])
    changes = []
    for pos, size, name in [(8, 4, 'SizeOfInitializedData'), (56, 4, 'SizeOfImage'),
            (60, 4, 'SizeOfHeaders'), (64, 4, 'CheckSum'), (144, 8, 'DebugDirectory')]:
        if a[pos:pos+size] != b[pos:pos+size]:
            changes.append(dict(field=name, before=a[pos:pos+size].hex(), after=b[pos:pos+size].hex()))
        a[pos:pos+size] = b'\0'*size
        b[pos:pos+size] = b'\0'*size
    require(a == b, 'PE optional runtime header changed')
    for i in range(16):
        if i != 6:
            require(before['directories'][i] == after['directories'][i], 'PE directory changed: ' + str(i))
    kept = {n: s for n, s in before['sections'].items() if not is_debug(n)}
    require(set(kept) == set(after['sections']), 'non-debug section set changed or debug sections remain')
    alignment = []
    for name, section in kept.items():
        result = after['sections'][name]
        for key in ('virtual_size', 'rva', 'characteristics', 'virtual_sha256'):
            require(section[key] == result[key], 'PE section changed: ' + name + '/' + key)
        if section['raw_size'] != result['raw_size']:
            require(section['padding_zero'] and result['padding_zero'], 'nonzero PE alignment padding changed')
            alignment.append(dict(section=name, before=section['raw_size'], after=result['raw_size']))
        else:
            require(section['raw_sha256'] == result['raw_sha256'], 'PE raw section bytes changed: ' + name)
    return dict(loader_payloads_exact=True, non_debug_directories_exact=True,
        sections=sorted(kept), removed_debug_sections=sorted(set(before['sections'])-set(kept)),
        permitted_header_changes=changes, zero_alignment_changes=alignment)


def write_new(path, value):
    data = (json.dumps(value, indent=2, sort_keys=True)+'\n').encode()
    with path.open('xb') as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())


def replace_json(path, value):
    temporary = path.with_name(path.name+'.ocpart')
    write_new(temporary, value)
    os.replace(temporary, path)


def prepare(a):
    stage, reference = real_path(a.stage), real_path(a.reference_root)
    marker, receipt = real_path(a.marker), real_path(a.receipt)
    stage_guard(stage, reference)
    for p in (marker, receipt):
        external(p, stage, reference)
        require(not p.exists(), 'control file already exists: ' + str(p))
    require(marker != receipt, 'marker and receipt must differ')
    tool_name = a.strip_tool or shutil.which('i686-w64-mingw32-strip')
    require(tool_name, 'i686-w64-mingw32-strip was not found')
    tool = Path(tool_name).resolve()
    regular(tool)
    version = subprocess.run([str(tool), '--version'], capture_output=True, text=True, check=True).stdout.splitlines()[0]
    require('GNU strip' in version, 'expected GNU i686 strip')
    programs = {}
    for path in sorted(stage.iterdir()):
        if path.suffix.lower() not in ('.exe', '.dll'):
            continue
        if path.name == 'SDL2.dll':
            regular(path)
            continue
        require(path.name in PROGRAMS, 'unexpected project PE in public stage: ' + path.name)
        regular(path)
        ref = reference/path.name
        require(identity(path)['sha256'] == identity(ref)['sha256'], 'stage differs from unstripped reference: ' + path.name)
        parsed = pe(path)
        if path.name in FRONTENDS:
            require(len(parsed['cooked_markers']) == 1 and parsed['cooked_markers'][0].endswith('|cooked'),
                    'missing unique cooked frontend marker: ' + path.name)
        programs[path.name] = dict(stage_before=identity(path), reference=identity(ref), pe_before=parsed)
    require(programs, 'no named project PE inputs in public stage')
    value = dict(schema='opencnc3d-public-debug-strip-1', state='prepared', stage=str(stage),
        reference_root=str(reference), receipt=str(receipt), helper_sha256=sha(Path(__file__).resolve()),
        token_sha256=sha(stage/TOKEN), tool=str(tool), tool_sha256=sha(tool), tool_version=version,
        arguments=['--preserve-dates', '--strip-debug'], programs=programs,
        vendor_sdl=identity(stage/'SDL2.dll') if (stage/'SDL2.dll').exists() else None)
    write_new(marker, value)
    print('PUBLIC-DEBUG-STRIP|prepared|programs=' + str(len(programs)), flush=True)


def apply(a):
    marker = real_path(a.marker)
    regular(marker)
    value = json.loads(marker.read_text())
    require(value['schema'] == 'opencnc3d-public-debug-strip-1', 'unknown marker schema')
    require(value['helper_sha256'] == sha(Path(__file__).resolve()), 'helper changed after preparation')
    stage, reference = real_path(value['stage']), real_path(value['reference_root'])
    receipt = real_path(value['receipt'])
    external(marker, stage, reference)
    external(receipt, stage, reference)
    stage_guard(stage, reference)
    require(sha(stage/TOKEN) == value['token_sha256'], 'stage token changed')
    require(value['arguments'] == ['--preserve-dates', '--strip-debug'], 'unexpected strip arguments')
    require(value['programs'] and set(value['programs']) <= PROGRAMS, 'unexpected marker program names')
    for name, entry in value['programs'].items():
        require(identity(reference/name) == entry['reference'], 'unstripped reference changed: ' + name)
    if value['vendor_sdl'] is not None:
        require(identity(stage/'SDL2.dll') == value['vendor_sdl'], 'vendor SDL changed')
    if value['state'] == 'applied':
        regular(receipt)
        result = json.loads(receipt.read_text())
        require(value['receipt_sha256'] == sha(receipt) and result['passed'], 'applied receipt changed')
        for name, entry in result['programs'].items():
            require(identity(stage/name) == entry['after'], 'completed stage changed: ' + name)
        print('PUBLIC-DEBUG-STRIP|already-verified', flush=True)
        return
    require(value['state'] == 'prepared' and not receipt.exists(), 'marker already consumed or receipt exists')
    tool = real_path(value['tool'])
    require(sha(tool) == value['tool_sha256'], 'strip tool changed')
    for name, entry in value['programs'].items():
        require(identity(stage/name) == entry['stage_before'], 'prepared stage changed: ' + name)
        require(pe(stage/name) == entry['pe_before'], 'prepared PE proof changed: ' + name)
    work = Path(tempfile.mkdtemp(prefix='.opencnc3d-public-strip-', dir=marker.parent))
    result = dict(schema=value['schema'], passed=False, stage=str(stage), reference_root=str(reference),
        helper_sha256=value['helper_sha256'], tool_sha256=value['tool_sha256'], arguments=value['arguments'],
        vendor_sdl_unchanged=True, native_windows_execution_verified=False, programs={})
    try:
        # Verify the whole batch before replacing any stage input. On interruption,
        # refuse the stale marker rather than treating a partial stage as complete.
        for name, entry in value['programs'].items():
            dest = work/name
            shutil.copy2(stage/name, dest)
            before = entry['pe_before']
            debug = any(is_debug(n) for n in before['sections'])
            if debug:
                subprocess.run([str(tool), '--preserve-dates', '--strip-debug', str(dest)], check=True)
            after = pe(dest)
            comparison = equivalent(before, after)
            require(identity(dest)['mtime_ns'] == entry['stage_before']['mtime_ns'], 'file timestamp changed')
            require(identity(dest)['mode'] == entry['stage_before']['mode'], 'file mode changed')
            result['programs'][name] = dict(before=entry['stage_before'], after=identity(dest),
                stripped=debug, comparison=comparison)
        for name, entry in value['programs'].items():
            require(identity(reference/name) == entry['reference'], 'reference changed during stripping')
            require(identity(stage/name) == entry['stage_before'], 'stage changed during stripping')
        stage_guard(stage, reference)
        for name in value['programs']:
            os.replace(work/name, stage/name)
        for name, entry in result['programs'].items():
            require(identity(stage/name) == entry['after'], 'installed stage hash mismatch')
            require(identity(reference/name) == value['programs'][name]['reference'], 'reference modified')
        if value['vendor_sdl'] is not None:
            require(identity(stage/'SDL2.dll') == value['vendor_sdl'], 'vendor SDL changed during stripping')
        result['passed'] = True
        write_new(receipt, result)
        value['state'] = 'applied'
        value['receipt_sha256'] = sha(receipt)
        replace_json(marker, value)
        print('PUBLIC-DEBUG-STRIP|verified|programs=' + str(len(result['programs'])), flush=True)
    finally:
        shutil.rmtree(work)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('prepare')
    for name in ('stage', 'reference-root', 'marker', 'receipt'):
        p.add_argument('--'+name, required=True)
    p.add_argument('--strip-tool')
    p = sub.add_parser('apply')
    p.add_argument('--marker', required=True)
    args = parser.parse_args()
    try:
        (prepare if args.command == 'prepare' else apply)(args)
    except (ValueError, KeyError, OSError, struct.error, subprocess.CalledProcessError) as e:
        parser.exit(1, 'PUBLIC-DEBUG-STRIP|refused|' + str(e)+'\n')


if __name__ == '__main__':
    main()
