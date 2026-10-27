#!/usr/bin/env python3
"""Exercise real sidebar hit tests, tooltip resolution and action clicks, hidden.

Uses the existing two-base SCG90EA fixture with controlled player/theater fields.
Run against an installed content directory; output/settings/saves stay private.
Example: python3 tools/gate-sidebar-actions.py --runtime playable --out /tmp/sidebar-actions
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, default=Path('playable'))
    parser.add_argument('--exe', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    runtime, output = args.runtime.resolve(), args.out.resolve()
    executable = args.exe.resolve() if args.exe else runtime / ('cnc_eyes.exe' if os.name == 'nt' else 'cnc_eyes')
    source = (runtime / 'missions/SCG90EA.INI').read_text()
    assert 'Player=GoodGuy' in source and 'Theater=TEMPERATE' in source
    records = []
    for house in ('GoodGuy', 'BadGuy'):
        for theater in ('TEMPERATE', 'DESERT'):
            for hud in ('dos', 'enhanced'):
                name = '{}-{}-{}'.format(house, theater, hud)
                run = output / name
                run.mkdir(parents=True, exist_ok=True)
                (run / 'saves').mkdir(exist_ok=True)
                fixture = run / 'missions'
                fixture.mkdir(exist_ok=True)
                # The scene prefix chooses the campaign player in the real engine.
                scene = 'SCB88EA' if house == 'BadGuy' else 'SCG88EA'
                ini = source.replace('Player=GoodGuy', 'Player=' + house).replace('Theater=TEMPERATE', 'Theater=' + theater)
                (fixture / (scene + '.INI')).write_text(ini)
                repair, sell = ((1094, 315), (1178, 315)) if hud == 'dos' else ((1071, 69), (1071, 111))
                lines = ['tick 20', 'gfx new_hud ' + str(int(hud == 'enhanced')), 'shot ' + str(run / 'off.png'), 'player', 'sbstates']
                for action, position in (('repair', repair), ('sell', sell)):
                    xy = '{} {}'.format(*position)
                    lines += ['echo ' + action, 'cursor ' + xy, 'sbhit ' + xy,
                              'shot ' + str(run / (action + '.png')), 'sbtip', 'sbstates',
                              'sbclick ' + xy, 'sbstates', 'sbclick ' + xy, 'sbstates']
                lines += ['cursor 400 400', 'sbtip', 'quit']
                script = run / 'probe.txt'
                script.write_text('\n'.join(lines) + '\n')
                cmd = [str(executable), '--scen', scene, '--pack', 'SCG01EA.pack',
                       '--dir', str(fixture) + '/', '--content', 'content/',
                       '--cameos', 'cameos.pack', '--dospack', 'dossidebar.pack',
                       '--dosinf', 'dosinfantry.pack', '--dylib', 'TiberianDawn.' + ('dll' if os.name == 'nt' else 'dylib'),
                       '--nosound', '--noshroud', '--w', '1280', '--h', '720',
                       '--gfx', str(run / 'fx.cfg'), '--savedir', str(run / 'saves'),
                       '--script', str(script)]
                if hud == 'enhanced':
                    cmd += ['--enhanced']
                with (run / 'run.log').open('w') as stream:
                    result = subprocess.run(cmd, cwd=runtime, env={**os.environ, 'OPENCNC3D_BACKGROUND': '1'},
                                            stdout=stream, stderr=subprocess.STDOUT, timeout=180)
                log = (run / 'run.log').read_text(errors='replace')
                tips = re.findall(r'^SBTIP\|.*$', log, re.M)
                hits = re.findall(r'^SBHIT\|.*$', log, re.M)
                states = re.findall(r'^SBSTATE\|.*$', log, re.M)
                checks = {
                    'process': result.returncode == 0,
                    'script': bool(re.search(r'SCRIPT\|end .*0 failures', log)),
                    'actual_player': 'PLAYER|house=' + house + '|' in log,
                    'tooltips': len(tips) == 3 and '|name=Repair Structure|cost=-1' in tips[0]
                                and '|name=Sell Structure|cost=-1' in tips[1] and '|name=-|cost=-1' in tips[2],
                    'hit_tests': len(hits) == 2 and '|repair|column=-1|slot=-1|' in hits[0]
                                 and '|sell|column=-1|slot=-1|' in hits[1],
                    'repair_click': 'SIDEBAR-INPUT|repair|on' in log and 'SIDEBAR-INPUT|repair|off' in log
                                    and any('|repair=active|sell=normal|' in s for s in states),
                    'sell_click': 'SIDEBAR-INPUT|sell|on' in log and 'SIDEBAR-INPUT|sell|off' in log
                                  and any('|repair=normal|sell=active|' in s for s in states),
                }
                record = {'case': name, 'checks': checks, 'pass': all(checks.values()), 'tips': tips, 'hits': hits, 'states': states}
                records.append(record)
                print('{}: {}'.format(name, 'PASS' if record['pass'] else 'FAIL'), flush=True)
    (output / 'results.json').write_text(json.dumps(records, indent=2) + '\n')
    return 0 if all(r['pass'] for r in records) else 1


if __name__ == '__main__':
    raise SystemExit(main())
