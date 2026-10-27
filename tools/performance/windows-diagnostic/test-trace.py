#!/usr/bin/env python3
"""Compile the actual trace header with fake SDL/GL and check its output contract."""
import csv,json,math,re,subprocess,tempfile
from pathlib import Path
here=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='opencnc3d-perftrace-test-') as temp:
    work=Path(temp); exe=work/'trace-test'
    subprocess.run(['c++','-std=c++14','-O2',str(here/'perf-trace-test.cpp'),'-o',str(exe)],check=True)
    decoded=0; outputs=0
    for mode in ('off','invalid','sessions','ring','cancel','gpu'):
        dest=work/mode;dest.mkdir()
        subprocess.run([str(exe),mode,str(dest)],check=True)
        for file in dest.glob('*.csv'):
            lines=file.read_text().splitlines()
            if lines==['keep me']:continue
            rows=list(csv.DictReader(line for line in lines if not line.startswith('#')))
            meta=dict(re.findall(r'([a-z_]+)=([^ ]+)',next(line for line in lines if line.startswith('# rows='))))
            assert len(rows)==int(meta['rows'])
            ids=[int(row['frame']) for row in rows];assert ids==sorted(set(ids))
            assert all(None not in row and None not in row.values() for row in rows)
            phases=('events_ms','camera_ms','tick_ms','prep_ms','draw_submit_ms','overlay_ms','swap_ms','post_swap_ms')
            for row in rows:
                values=[float(row[key]) for key in phases]
                assert all(math.isfinite(v) and v>=0 for v in values)
                assert abs(sum(values)-float(row['total_ms']))<0.00002
                assert row['missing']==''
                assert float(row['cpu:first draw pass'])==1.0
            decoded+=len(rows);outputs+=1
    print(json.dumps({'status':'PASS','CSV_files':outputs,'decoded_rows':decoded,'native_driver_test':False}))
