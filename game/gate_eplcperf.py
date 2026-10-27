#!/usr/bin/env python3
"""G361: a structure's placement animation is not rebuilt every frame
(game/ebld_place.h: eplc_draw_run, the rigid parts drawn at rest and moved on the card).

  gate_eplcperf.py script DIR    write DIR/run.txt, the run's script
  gate_eplcperf.py check LOG     read the run's log: one line, "1|0 message"

One hidden run of the Red Alert snow skirmish SCM03EA, Allied, seeded, under Enhanced with
the frame profiler on (OPENCNC3D_FRAMEPROF=1 OPENCNC3D_PERFDETAIL=1): the MCV deploys, then a
Power Plant, an Ore Refinery and a War Factory are built from the sidebar and placed at known
cells with the camera on them. Before each placement 30 frames of the base standing are
profiled, one frame an engine tick (STANDING), and from the placement on, the frames of its
animation the same way (DURING: 50, 55 and 54 frames, inside the animations). PROC uses a
second seeded replay at the exact same55ticks with only its placement omitted, preserving
the two sidebar-command ticks; the37shared objects must match and only the exact placed
PROC and its engine-granted Harvester may differ. This removes time-varying background
uploads from that comparison. Every existing upload/byte/time budget is retained. Each window is bracketed by the mesh
batch's counters (meshperfdump) and closed by the profiler's window (fprofreport); label lines
mark them in the log.

CLAIMS, per type, from the engine's own counts (machine independent) and one ratio:
  (1) the placement plays over the frames profiled: an EPLC|start line of the type in place mode,
      and its EPLCMODEL drawn count over the window is at least the window's frames less five
      (the colour picture drew it on every frame of the animation; the window runs a few frames
      past its end) with normal runs drawn (the normal target drew it);
  (2) the mesh batch's vertex uploads per frame during the animation exceed the standing window's
      by at most UPLOADS_MARGIN, and the bytes uploaded per frame by at most BYTES_MARGIN;
  (3) the window's mean frame time during the animation is at most RATIO times the standing
      window's (a bound against a frame that doubles, not a measure of the speed-up: the
      frames are measured one after the other on the same machine, so a loaded machine slows
      both, and noise moves the ratio by a fifth).
PROVEN TO FAIL on the binary before the card path, which rebuilt and re-uploaded every moving
part in every pass each frame: in this scene the Power Plant's placement sent 528 uploads a frame
against 6 standing, the Refinery's 89 against 27, the War Factory's 110 against 30. With the card
path the Power Plant sends 32, the Refinery 28, the War Factory 38 (the numbers of this gate's own
run are in its check line).
NOT COVERED: how the placement looks (G348 and the proof sheets cover that); Tiberian Dawn's
placements, which share the code; Classic, which has no placement.
"""
import os
import sys

# the type, the frames of its animation to profile, and the cell the sidebar's own auto
# placement chooses for it on this seeded map (the camera stands on it: the auto placement
# puts the structures north of the yard, where a camera left on the yard sees only their foot)
# (the windows end inside the animations: 3.45, 3.85 and 3.75 seconds at 15 ticks a second)
TYPES = [('POWR', 50, (46, 75)), ('PROC', 55, (51, 74)), ('WEAP', 54, (54, 75))]
STAND = 30
# (a placement still sends each of its runs once as it starts, the at-rest copy every pass
# draws, which averaged over the window is about 26 a frame for the Power Plant's 284 runs,
# and its ring of broken earth, whose fade is in its key, every frame; the margin sits well
# above that and well below the per-frame rebuild's 90 to 530 a frame. The draw calls are
# the model's own runs, in its own order, which is the order its faces were approved in, so
# they are not claimed here.)
UPLOADS_MARGIN = 60.0
BYTES_MARGIN = 512 * 1024.0
RATIO = 2.0


def script(d):
    os.makedirs(d, exist_ok=True)
    v = ['tick 5', 'look MCV:@me', 'tick 1', 'lclickobj MCV:@me', 'actclickobj MCV:@me',
         'tick 120', 'look FACT:@me', 'dist 1600',
         # (the Red Alert strip lists no structure but the fake yard and the walls until the
         # house owns a Power Plant: one is spawned beside the yard, and from then on the
         # sidebar builds)
         'spawnobj POWR 44 78', 'tick 90']
    for t, n, (cx, cy) in TYPES:
        # (warm-up frames after the ready wait, which ticks thousands of times without a
        # draw: the first frame after it rebuilds the shroud corners and takes seconds)
        v += ['build %s' % t, 'ready %s 6000' % t, 'tick 30',
              'cam %d %d' % (cx + 1, cy + 1), 'dist 1400', 'frames 3']
        v += ['label WINDOW STANDING %s' % t, 'fprofreport', 'meshperfdump']
        v += ['tick 1', 'frames 1'] * STAND
        v += ['label END STANDING %s' % t, 'fprofreport', 'meshperfdump', 'eplcdump']
        v += ['startplace %s' % t, 'place %d %d' % (cx, cy)]
        if t == 'PROC':
            v += ['ra3dprobe', 'objdump']
        v += ['label WINDOW DURING %s' % t, 'fprofreport', 'meshperfdump', 'eplcdump']
        v += ['tick 1', 'frames 1'] * n
        v += ['label END DURING %s' % t, 'fprofreport', 'meshperfdump', 'eplcdump']
        if t == 'PROC':
            v += ['ra3dprobe', 'objdump']
        v += ['tick 150']
    v += ['quit']
    open(os.path.join(d, 'run.txt'), 'w').write('\n'.join(v) + '\n')
    # The same seeded timeline, camera and ticks, with only the target placement absent.
    # Both sidebar commands advance one tick; the control must keep those two ticks.
    control = '\n'.join(v[:v.index('build WEAP')])
    command = 'startplace PROC\nplace 51 74\n'
    if control.count(command) != 1:
        raise ValueError('the matched PROC control no longer identifies one placement')
    control = control.replace(command, 'tick 2\n')
    open(os.path.join(d, 'proc-standing.txt'), 'w').write(control + '\nquit\n')



def kv(line):
    out = {}
    for p in line.strip().split('|')[1:]:
        if '=' in p:
            k, val = p.split('=', 1)
            out[k] = val
    return out


def windows(lines):
    """label -> dict(frames, mean, uploads, bytes, draws, drawn, normal) per window."""
    out = {}
    label = None
    mp0 = None
    i = 0
    while i < len(lines):
        l = lines[i]
        if l.startswith('LABEL|WINDOW '):
            label = l[len('LABEL|WINDOW '):]
            mp0 = None
            for q in range(i + 1, min(i + 40, len(lines))):
                if lines[q].startswith('MESHPERF|'):
                    mp0 = kv(lines[q])
                    break
        elif l.startswith('LABEL|END ') and label:
            prof = mp1 = None
            model = None
            t = label.split()[-1]
            for q in range(i + 1, len(lines)):
                if lines[q].startswith('FRAMEPROF|') and prof is None:
                    prof = kv(lines[q])
                if lines[q].startswith('MESHPERF|') and mp1 is None:
                    mp1 = kv(lines[q])
                if lines[q].startswith('EPLCMODEL|%s|' % t) and model is None:
                    model = kv(lines[q])
                if lines[q].startswith('LABEL|'):
                    break
            w = {}
            if prof and mp0 and mp1:
                n = int(prof['frames'])
                w['frames'] = n
                w['mean'] = float(prof['mean'])
                for key in ('uploads', 'upload_bytes', 'draws'):
                    w[key] = (int(mp1[key]) - int(mp0[key])) / float(max(n, 1))
            if model:
                w['drawn'] = int(model.get('drawn', 0))
                w['normal'] = int(model.get('normal_runs', 0))
            out[label] = w
            label = None
        i += 1
    return out



def matched_proc(main, control):
    """Require the actual paired scene before using its same-time standing counters."""
    def snapshot(lines, phase):
        marker = 'LABEL|' + ('WINDOW DURING PROC' if phase == 0 else 'END DURING PROC')
        at = lines.index(marker)
        if phase == 0:
            lo = max(i for i in range(at) if lines[i].startswith('LABEL|'))
            hi = at
        else:
            lo = at
            hi = next((i for i in range(at + 1, len(lines)) if lines[i].startswith('LABEL|')), len(lines))
        block = lines[lo:hi]
        frames = [int(kv(l)['engineframe']) for l in block if l.startswith('RA3DPROBE|frame|')]
        objects = [l for l in block if l.startswith('OBJ|')]
        if len(frames) != 1 or not objects:
            raise ValueError('missing unique matched PROC frame/object census')
        return frames[0], objects
    def target(line):
        return line.startswith('OBJ|BUILDING|PROC|Multi1|') and '|id=7|' in line
    for prefix in ('skirmish: seat=', 'MAP theater=', 'STATICCELLS|', 'RA3D|scene|'):
        a = [l for l in main if l.startswith(prefix)]
        b = [l for l in control if l.startswith(prefix)]
        if not a or a != b:
            raise ValueError('matched PROC initial scene differs: ' + prefix)
    times = []
    for phase, expected in enumerate((2162, 2217)):
        frame, a = snapshot(main, phase)
        otherframe, b = snapshot(control, phase)
        if frame != expected or otherframe != expected:
            raise ValueError('matched PROC engine clocks differ from the seeded window')
        times.append(frame)
        ta, tb = [l for l in a if target(l)], [l for l in b if target(l)]
        if len(ta) != 1 or len(tb) != 1 or '|x=51|y=74|' not in ta[0] or '|limbo=0|' not in ta[0] or '|limbo=1|' not in tb[0]:
            raise ValueError('matched PROC target/queued-building counterpart differs')
        aa, bb = [l for l in a if not target(l)], [l for l in b if not target(l)]
        # The completed Refinery grants this exact engine-owned Harvester. Count its
        # upload cost as part of placement; permit no other new unit or shared-state drift.
        if phase == 1:
            harvesters = [l for l in aa if l.startswith('OBJ|UNIT|HARV|Multi1|') and '|id=1|' in l]
            if len(harvesters) != 1 or '|x=52|y=76|' not in harvesters[0] or '|limbo=0|' not in harvesters[0] or '|str=600|max=600|' not in harvesters[0]:
                raise ValueError('matched PROC free-Harvester consequence differs')
            aa.remove(harvesters[0])
        if len(aa) != 37 or len(bb) != 37 or aa != bb:
            raise ValueError('matched PROC shared object census differs')
    if times[1] - times[0] != 55:
        raise ValueError('matched PROC profile is not exactly55ticks')
    standing = windows(control).get('DURING PROC', {})
    active = windows(main).get('DURING PROC', {})
    if standing.get('frames') != 55 or active.get('frames') != 55:
        raise ValueError('matched PROC profiler windows must each contain exactly55frames')
    return standing


def check(path):
    lines = open(path, errors='replace').read().splitlines()
    if not any(l.startswith('SCRIPT|begin') for l in lines):
        return 0, 'the run did not start (no SCRIPT|begin in %s)' % path
    W = windows(lines)
    try:
        control_path = os.path.join(os.path.dirname(path), 'proc-standing.log')
        control = open(control_path, errors='replace').read().splitlines()
        proc_standing = matched_proc(lines, control)
    except (OSError, ValueError, KeyError) as error:
        return 0, 'the matched-time PROC control failed: ' + str(error)
    notes = []
    for t, n, _ in TYPES:
        s = proc_standing if t == 'PROC' else W.get('STANDING %s' % t, {})
        d = W.get('DURING %s' % t, {})
        if 'mean' not in s or 'mean' not in d:
            return 0, '%s: a profiled window is missing from the log (standing %s, during %s)' % (t, bool(s), bool(d))
        started = any(l.startswith('EPLC|start|') and '|type=%s|' % t in l and '|mode=place|' in l for l in lines)
        if not started:
            return 0, '%s: its placement never started (no EPLC|start ... mode=place)' % t
        # (the window runs a few frames past the animation's end, where the placement no
        # longer draws the building: five frames of slack)
        if d.get('drawn', 0) < d['frames'] - 5 or d.get('normal', 0) <= 0:
            return 0, ('%s: the placement was not drawn on the frames of its window (drawn %d of %d frames, '
                       'normal runs %d)' % (t, d.get('drawn', 0), d['frames'], d.get('normal', 0)))
        du, su = d['uploads'], s['uploads']
        db, sb = d['upload_bytes'], s['upload_bytes']
        ratio = d['mean'] / s['mean'] if s['mean'] > 0 else 99.0
        note = ('%s: uploads/frame %.1f vs %.1f standing, %.2f vs %.2f MB/frame, %.2f vs %.2f ms (x%.2f)'
                % (t, du, su, db / 1048576.0, sb / 1048576.0, d['mean'], s['mean'], ratio))
        if du > su + UPLOADS_MARGIN:
            return 0, note + ': the animation uploads more than %.0f a frame over the standing base' % UPLOADS_MARGIN
        if db > sb + BYTES_MARGIN:
            return 0, note + ': the animation uploads more than %.1f MB a frame over the standing base' % (BYTES_MARGIN / 1048576.0)
        if ratio > RATIO:
            return 0, note + ': the animation costs more than %.1f times the standing frame' % RATIO
        notes.append(note)
    return 1, '; '.join(notes)


if __name__ == '__main__':
    if len(sys.argv) >= 3 and sys.argv[1] == 'script':
        script(sys.argv[2])
    elif len(sys.argv) >= 3 and sys.argv[1] == 'check':
        v, msg = check(sys.argv[2])
        print('%d %s' % (v, msg))
    else:
        print(__doc__)
        sys.exit(2)
