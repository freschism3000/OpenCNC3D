#!/usr/bin/env python3
"""Stage an existing RA module with binaries for the target platform, without its movies
and score.

No package carries the 1995 movies or the score, Red Alert's included. The paths that
stay out are the ones tools/media/media-paths.txt names, the list both packagers and the
release read; the lines of it under ra3d/ are this module's, and they are read from the
list rather than written here, so the two cannot disagree. The game plays without them by
file presence alone: a missing movie is skipped and a missing score is silence.
"""
import argparse
from pathlib import Path
import shutil
import sys
from importlib import import_module

ROOT = Path(__file__).resolve().parents[2]
MEDIA_LIST = ROOT / 'tools/media/media-paths.txt'
MODULE = 'ra3d'


def module_media(media_list=MEDIA_LIST, module=MODULE):
    """The media list's paths inside the module, relative to it, in lower case and with
    no trailing slash. Read the way every reader of the list reads it: '#' starts a
    comment, and the leading '/' and a trailing '/' are optional. A name compares without
    regard to case, as a player's disk may compare it."""
    if not media_list.is_file():
        sys.exit(f'stage-runtime: no media list at {media_list}; it is part of this tree, '
                 'and without it nothing keeps the movies and score out of the module')
    prefix = module.lower() + '/'
    paths = set()
    for line in media_list.read_text(encoding='utf-8').splitlines():
        path = line.split('#', 1)[0].strip()
        if path.startswith('./'):
            path = path[2:]
        path = path.lstrip('/').rstrip('/').lower()
        if path.startswith(prefix) and len(path) > len(prefix):
            paths.add(path[len(prefix):])
    return paths


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--source', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--app', type=Path, required=True)
    ap.add_argument('--brain', type=Path, required=True)
    args = ap.parse_args()
    dll = args.app.suffix == '.exe'
    if args.brain.name != ('RedAlert.dll' if dll else 'RedAlert.dylib'):
        ap.error('app and Red Alert brain must target the same platform')
    for path in (args.app, args.brain, args.source / 'data/GENERAL.MIX', args.source / 'dosmenu.pack'):
        if not path.is_file():
            ap.error(f'missing runtime input: {path}')
    media = module_media()
    left_out = set()

    def is_media(rel):
        return rel.lower() in media

    def skip_media(directory, names):
        rel_dir = Path(directory).relative_to(args.source).as_posix()
        skipped = []
        for name in names:
            rel = name if rel_dir == '.' else f'{rel_dir}/{name}'
            if is_media(rel):
                skipped.append(name)
                left_out.add(rel + ('/' if (Path(directory) / name).is_dir() else ''))
        return skipped

    args.out.mkdir(parents=True, exist_ok=True)
    for name in ('data', 'movies', 'audio', 'missions', 'einf', '3d', 'previews'):
        src = args.source / name
        if not src.is_dir():
            continue
        if is_media(name):
            left_out.add(name + '/')
            continue
        shutil.copytree(src, args.out / name, dirs_exist_ok=True, ignore=skip_media)
    # The multiplayer map list a skirmish screen reads (bake-ra-map-previews.py).
    if (args.source / 'skirmish-maps.txt').is_file():
        shutil.copy2(args.source / 'skirmish-maps.txt', args.out / 'skirmish-maps.txt')
    for pack in args.source.glob('*.pack'):
        if is_media(pack.name):
            left_out.add(pack.name)
            continue
        shutil.copy2(pack, args.out / pack.name)
    for binary in (args.app, args.brain):
        shutil.copy2(binary, args.out / binary.name)
    libraries = ('SDL2.dll',) if dll else ('libSDL2-2.0.0.dylib', 'libSDL3.dylib')
    for name in libraries:
        shutil.copy2(args.app.parent / name, args.out / name)
    # RA can now be installed into an empty launcher. Its shared soldiers must
    # therefore live in the RA-first search path instead of depending on Dawn3D
    # also being installed. The RA-specific approved files staged below win.
    infantry = args.out / 'einf'
    infantry.mkdir(exist_ok=True)
    for model in (ROOT / 'game/einf').glob('*.c3m'):
        shutil.copy2(model, infantry / model.name)
    import_module('stage-structures').stage(args.out)

    # ASKED OF THE RESULT, not trusted to the copy: a folder staged into one that already
    # held a movie or the score would keep it, and the packagers would then refuse the
    # whole package for a file this script was meant to leave behind.
    present = sorted(p.relative_to(args.out).as_posix() for p in args.out.rglob('*')
                     if is_media(p.relative_to(args.out).as_posix()))
    if present:
        sys.exit(f'stage-runtime: {args.out} holds media no package carries: '
                 f'{", ".join(present)}. Stage into an empty folder.')
    if left_out:
        print(f'RA media left out ({MEDIA_LIST.relative_to(ROOT)}): {", ".join(sorted(left_out))}')
    print(f'RA runtime staged in {args.out}')


if __name__ == '__main__':
    main()
