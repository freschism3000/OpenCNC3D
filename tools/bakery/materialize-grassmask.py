#!/usr/bin/env python3
"""Verify and atomically unpack the single committed Temperate grass-mask source.

Only an absent output, the current approved bytes, or an explicitly listed prior
approved version may be used. Local artist overrides are never inferred from an
ignored provenance record and are never overwritten.
"""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import stat
import sys
import tempfile

ASSET = 'game/grassmask_TEMPERAT.pack'
SOURCE = ASSET + '.gz'
MANIFEST = 'game/grassmask_TEMPERAT.source.json'
RECORD = 'game/.grassmask_TEMPERAT.materialized.json'
BLOCK = 1024 * 1024


def regular(path, optional=False):
    try:
        info = path.lstat()
    except FileNotFoundError:
        if optional:
            return False
        raise ValueError('required source is absent: ' + str(path))
    if not stat.S_ISREG(info.st_mode):
        raise ValueError('refusing nonregular or symlink path: ' + str(path))
    return True


def identity(path, optional=False):
    if not regular(path, optional):
        return None
    digest = hashlib.sha256()
    size = 0
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(BLOCK), b''):
            size += len(chunk)
            digest.update(chunk)
    return {'sha256': digest.hexdigest(), 'bytes': size}


def valid_identity(value):
    return (isinstance(value, dict) and set(value) == {'sha256', 'bytes'} and
            isinstance(value['sha256'], str) and len(value['sha256']) == 64 and
            all(c in '0123456789abcdef' for c in value['sha256']) and
            type(value['bytes']) is int and value['bytes'] > 0)


def load_manifest(root):
    path = root / MANIFEST
    regular(path)
    value = json.loads(path.read_text(encoding='utf-8'))
    if (value.get('format') != 'opencnc3d-compressed-asset' or
            value.get('version') != 1 or value.get('asset') != ASSET or
            value.get('source') != SOURCE or
            not valid_identity(value.get('raw')) or
            not valid_identity(value.get('gzip')) or
            not isinstance(value.get('previous_approved_raw'), list) or
            not all(valid_identity(p) for p in value['previous_approved_raw'])):
        raise ValueError('invalid pinned grass-mask manifest')
    return value


def publish_record(path, value):
    raw = (json.dumps(value, indent=2, sort_keys=True) + '\n').encode('utf-8')
    if regular(path, optional=True) and path.read_bytes() == raw:
        return
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix='.grassmask-record-', delete=False) as output:
            temporary = Path(output.name)
            output.write(raw)
            output.flush()
            os.fsync(output.fileno())
        os.chmod(temporary, 0o644)
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def materialize(root, check=False):
    root = Path(root).resolve()
    if (root / 'game').is_symlink() or not (root / 'game').is_dir():
        raise ValueError('game source directory must be a real directory')
    manifest = load_manifest(root)
    source, output, record = (root / name for name in (SOURCE, ASSET, RECORD))
    if identity(source) != manifest['gzip']:
        raise ValueError('compressed grass-mask source SHA/size mismatch')
    regular(record, optional=True)
    before = identity(output, optional=True)
    approved = manifest['raw']
    if before == approved:
        if not check:
            publish_record(record, {'format': 1, 'asset': ASSET, 'raw': approved,
                                    'source': SOURCE, 'gzip': manifest['gzip']})
        return 'verified'
    if before is not None and before not in manifest['previous_approved_raw']:
        raise ValueError('local grass-mask override preserved; SHA/size is not an approved source version: ' + before['sha256'])
    if check:
        raise ValueError('approved grass-mask output is absent or needs materialization')
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent, prefix='.grassmask-unpack-', delete=False) as target:
            temporary = Path(target.name)
            digest, size = hashlib.sha256(), 0
            with gzip.open(source, 'rb') as compressed:
                while True:
                    chunk = compressed.read(min(BLOCK, approved['bytes'] - size + 1))
                    if not chunk:
                        break
                    size += len(chunk)
                    if size > approved['bytes']:
                        raise ValueError('decompressed grass mask exceeds pinned size')
                    digest.update(chunk)
                    target.write(chunk)
            if {'sha256': digest.hexdigest(), 'bytes': size} != approved:
                raise ValueError('decompressed grass-mask SHA/size mismatch')
            target.flush()
            os.fsync(target.fileno())
        # Catch another writer between validation and publication. The record is
        # informational; it can never authorize overwriting a local raw override.
        if identity(output, optional=True) != before:
            raise ValueError('grass-mask output changed while unpacking; preserved')
        os.chmod(temporary, 0o644)
        os.replace(temporary, output)
        publish_record(record, {'format': 1, 'asset': ASSET, 'raw': approved,
                                'source': SOURCE, 'gzip': manifest['gzip']})
        return 'materialized'
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--check', action='store_true', help='verify without writing raw output or provenance')
    args = parser.parse_args()
    print('grassmask_TEMPERAT: ' + materialize(args.root, args.check))


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, EOFError, json.JSONDecodeError) as error:
        print('Grass-mask source refused: ' + str(error), file=sys.stderr)
        sys.exit(1)
