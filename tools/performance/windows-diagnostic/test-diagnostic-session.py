#!/usr/bin/env python3
"""Data-only tests of the actual automatic logger; no SDL windows or GL drivers."""
import argparse,csv,json,subprocess,tempfile,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[3]);p.add_argument('--out',type=Path);p.add_argument('--cxx',default='c++');a=p.parse_args()
def run(out):
    out.mkdir(parents=True,exist_ok=True);exe=out/'diagnostic-session-test'
    src=a.repo/'tools/performance/windows-diagnostic/diagnostic-session-test.cpp'
    subprocess.run([a.cxx,'-std=c++14','-O2','-pthread',str(src),'-o',str(exe)],check=True)
    checks=0
    for mode in ('off','permission','queue','writeerror','long','gaps','missions','privacy','caps','collision','partial','blocked'):
        root=out/mode;root.mkdir();started=time.monotonic();subprocess.run([str(exe),mode,str(root)],check=True,timeout=30)
        sessions=sorted(root.glob('session-*'))
        if mode in ('off','permission','queue','writeerror'):assert not sessions;checks+=1;continue
        for directory in sessions:
            status=json.loads((directory/'status.json').read_text());assert status['clean_exit']==(mode!='partial');checks+=1
            assert status['queue_capacity']==1024 and status['queue_bytes']<16*1024*1024;checks+=1
            frames=list(csv.DictReader((directory/'frames-context.csv').open()));seconds=list(csv.DictReader((directory/'seconds.csv').open()))
            for rows in (frames,seconds):
                if mode=='partial' and rows and (None in rows[-1] or None in rows[-1].values()):rows.pop()
                for row in rows:assert None not in row and None not in row.values()
            if mode=='long':
                assert status['frame_rows_seen']==20000
                assert sum(int(r['frames']) for r in seconds)==20000 and len(seconds)>350
                assert int(seconds[0]['second'])==0 and int(seconds[-1]['second'])>400
                assert {int(r['mission']) for r in seconds}=={1,2}
                assert all(abs(float(r['inter_present_mean_ms'])-20)<.01 for r in seconds if r['inter_present_mean_ms'] and float(r['inter_present_worst_ms'])<30)
                assert any(r['cause']=='spike_context' and 1440<=int(r['frame'])<=1501 for r in frames)
                assert any(r['cause']=='spike_after' and 1502<=int(r['frame'])<=1580 for r in frames)
                assert len(list(directory.glob('settings-*.cfg')))==2
                assert status['gpu_samples_seen']>19000 and status['context_truncated']==0
                assert int(seconds[0]['frames'])>0 and seconds[0]['extra_2_max']==''
                assert all(int(r['mesh_admission_refused'])>=0x100000000 for r in frames)
                assert all(int(r['mesh_admission_refused_max'])>=0x100000000 for r in seconds)
                checks+=12
            if mode=='gaps':
                assert status['frame_rows_seen']==8 and status['unobserved_or_cancelled_frames']==5
                assert status['unmatched_gpu_samples']>=5
                assert all(float(r['inter_present_worst_ms'])<21 for r in seconds)
                assert all(r['cause']=='second_worst' for r in frames)
                assert any('frame_record_gap' in line for line in (directory/'timeline.csv').read_text().splitlines());checks+=5
            if mode=='missions':assert len(seconds)==2 and {(r['second'],r['mission']) for r in seconds}=={('0','1'),('0','2')};checks+=1
            if mode=='privacy':
                text=''.join(x.read_text() for x in directory.iterdir())
                assert 'private-name' not in text and 'user@example.invalid' not in text and 'redacted' in text
                snapshot=next(directory.glob('settings-*.cfg')).read_text();assert 'mode.enhanced.water_fx' in snapshot or 'mode.enhanced.water' in snapshot
                checks+=2
            if mode=='caps':
                assert status['detail_rows_dropped']>0 and status['aggregate_seconds_dropped']>0 and status['config_snapshots_dropped']>0
                assert (directory/'seconds.csv').stat().st_size<=1500
                assert (directory/'frames-context.csv').stat().st_size+(directory/'gpu-completions.csv').stat().st_size<=2500
                assert sum(x.stat().st_size for x in directory.glob('settings-*.cfg'))<=10
                checks+=4
            if mode=='partial':assert status['frame_rows_seen']>0 and len(seconds)>0;checks+=1
        if mode=='collision':assert len(sessions)==2;checks+=1
        if mode=='blocked':assert time.monotonic()-started<2;checks+=1
    print(json.dumps({'status':'PASS','checks':checks,'frames':'20000+','native_Windows_driver_test':False}))
if a.out:run(a.out)
else:
    with tempfile.TemporaryDirectory(prefix='opencnc3d-diagnostic-session-') as t:run(Path(t))
