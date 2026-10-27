#!/usr/bin/env python3
"""Capture the isolated swash renderer. Never launch a visible game window."""
from pathlib import Path
import argparse
import json
import os
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--renderer', required=True, type=Path,
                        help='Separately built renderer with water-swash-v06.patch applied')
    parser.add_argument('--playable', required=True, type=Path)
    parser.add_argument('--cfg', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    renderer, playable, cfg, output = [p.resolve() for p in
        (args.renderer, args.playable, args.cfg, args.output)]
    if output.exists() and any(output.iterdir()):
        parser.error('--output must be empty; preserve previous captures')
    output.mkdir(parents=True, exist_ok=True)
    (output / 'saves').mkdir()
    fixtures = [
        ('TD-Temperate', 'SCG01EA', False, None),
        ('TD-Desert', 'SCB01EA', False, (23, 28)),
        ('RA-Temperate', 'SCU02EA', True, None),
        ('RA-Snow', 'SCG03EA', True, (78, 64)),
    ]
    results = []
    for name, scene, ra, cam in fixtures:
        dst = output / name
        dst.mkdir()
        lines = ['gfxmode 2', 'gfx texset 3', 'gfx show_stats 0', 'reveal 1',
                 'gfx grass 0', 'gfx cloud_on 0', 'gfx fog_fx 0', 'gfx sand_fx 0',
                 'gfx bloom_on 0', 'gfx tree3d_wind 0', 'gfx fol_wind 0',
                 'gfx water_edge 0', 'dist 1920', 'tick 2', 'gfx water_foam 0']
        if cam:
            lines.append(f'cam {cam[0]} {cam[1]}')
        lines.extend([f'shot {dst}/control.png', 'gfx water_foam 1'])
        for i in range(45):
            lines.extend([f'shot {dst}/frame-{i:03d}.png', 'tick 2'])
        lines.append('quit')
        script = dst / 'commands.txt'
        script.write_text('\n'.join(lines) + '\n')
        command = [str(renderer), '--nosound', '--enhanced', '--gfx', str(cfg),
                   '--noshroud', '--nosidebar', '--nominimap', '--w', '1200', '--h', '800',
                   '--scen', scene, '--pack', ('ra3d/3d/' if ra else '') + scene + '.pack',
                   '--dir', 'ra3d/missions/' if ra else 'missions/',
                   '--dylib', 'ra3d/RedAlert.dylib' if ra else 'TiberianDawn.dylib',
                   '--content', 'ra3d/data' if ra else 'content/',
                   '--savedir', str(output/'saves'), '--gfxsave', str(output/'discard.cfg'),
                   '--script', str(script)]
        if ra:
            command.extend(['--ra3d', '--dosdata', 'ra3d/audio', '--models3d', str(playable)+'/'])
        env = {**os.environ, 'OPENCNC3D_BACKGROUND': '1', 'SDL_MAC_BACKGROUND_APP': '1',
               'DYLD_LIBRARY_PATH': str(playable)}
        with (dst/'run.log').open('w') as log:
            run = subprocess.run(command, cwd=playable, env=env, stdout=log,
                                 stderr=subprocess.STDOUT, timeout=240)
        log = (dst/'run.log').read_text()
        if run.returncode or not re.search(r'SCRIPT\|end .*0 failures', log) or re.search(
                r'ERROR: 0:|shader.*failed', log, re.I):
            raise RuntimeError(f'{name}: inspect {dst / "run.log"}')
        results.append(dict(name=name, scene=scene, frames=45, seconds=6,
                            script_failures=0, shader_failures=0))
        (output/'RESULTS.json').write_text(json.dumps(results, indent=2)+'\n')
        print(name, 'actual hidden six-second capture complete', flush=True)


if __name__ == '__main__':
    main()
