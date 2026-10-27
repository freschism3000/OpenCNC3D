#!/usr/bin/env python3
"""Summarize same-build scripted rendering samples, never presented/displayed FPS."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import statistics

PHASE=re.compile(r'^ECHO\|BENCH\|phase=([a-z-]+)$')
SAMPLE=re.compile(r'^FRAMEBENCH\|n=(\d+)\|median=([0-9.]+)\|p90=([0-9.]+)$')


def read(path):
    phase=None;values={};counters={};hardware=[]
    for line in path.read_text(encoding='utf-8-sig',errors='replace').splitlines():
        m=PHASE.fullmatch(line)
        if m:phase=m[1];continue
        m=SAMPLE.fullmatch(line)
        if m and phase:
            # A median of several frozen draws is not an active-frame sample.
            if int(m[1])!=1 or not math.isfinite(float(m[2])):continue
            values.setdefault(phase,[]).append(float(m[2]))
        for prefix in ('FRAGMENTPREP','SUNSTATIC','EBLDPREPARE','MESHPERF','MESHPOOL'):
            if line.startswith(prefix+'|'):
                fields={k:float(v) for k,v in re.findall(r'([a-z_]+)=(-?\d+(?:\.\d+)?)',line)}
                counters.setdefault(prefix,[]).append({'phase':phase,'fields':fields})
        if line.startswith(('GL_RENDERER','GL_VERSION')):hardware.append(line)
    return values,counters,hardware


def stats(values):
    ordered=sorted(values)
    return {'n':len(values),'median_ms':statistics.median(ordered),
            'p95_ms':ordered[min(len(ordered)-1,math.ceil(len(ordered)*.95)-1)],
            'min_ms':ordered[0],'max_ms':ordered[-1]}


def analyze(folder):
    runs={};warnings=[]
    expected={'active':{'intact':1,'first-damage':1,'moving':32,'settling':32,'sinking':32,'late':32},
              'siege':{'active-siege':80}}
    for variant in ('baseline-1','optimized-1','optimized-2','baseline-2'):
        for case in ('active','siege'):
            path=folder/(variant+'-'+case+'.log')
            if not path.is_file():raise ValueError('Missing comparison log: '+path.name)
            phases,counters,hardware=read(path)
            for name,want in expected[case].items():
                if len(phases.get(name,[]))!=want:
                    raise ValueError(path.name+': '+name+' requires '+str(want)+' single-frame samples')
            runs[(variant,case)]={'phases':phases,'counters':counters,'hardware':hardware,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
    result={}
    for case,names in expected.items():
        result[case]={}
        for phase in names:
            base=sum((runs[(v,case)]['phases'][phase] for v in ('baseline-1','baseline-2')),[])
            opt=sum((runs[(v,case)]['phases'][phase] for v in ('optimized-1','optimized-2')),[])
            before,after=stats(base),stats(opt)
            med=before['median_ms'];saving=med-after['median_ms']
            b1=statistics.median(runs[('baseline-1',case)]['phases'][phase]);b2=statistics.median(runs[('baseline-2',case)]['phases'][phase])
            drift=abs(b1-b2)
            if abs(saving)<=drift and phase not in ('intact','first-damage'):
                warnings.append(case+'/'+phase+': observed median difference does not exceed baseline run drift')
            result[case][phase]={'baseline':before,'optimized':after,'median_saved_ms':saving,
                'median_reduction_percent':100*saving/med if med else None,'baseline_run_drift_ms':drift}
    return {'format':1,'complete':True,'measurement':'glFinish rendering-completion samples; not live-game or displayed FPS',
            'cold_scope':'First-damage sample is draw completion; loading/demand CPU work must be assessed from resource counters separately.',
            'paired_order':['baseline-1','optimized-1','optimized-2','baseline-2'],
            'statistics':result,'warnings':warnings,'runs':{v+'-'+c:{'counters':x['counters'],'hardware':x['hardware'],'sha256':x['sha256']} for (v,c),x in runs.items()}}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('report',type=Path);p.add_argument('--output',type=Path);a=p.parse_args()
    result=analyze(a.report)
    if a.output:a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'complete':True,'statistics':result['statistics'],'warnings':result['warnings']},indent=2))
