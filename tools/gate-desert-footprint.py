#!/usr/bin/env python3
"""Verify Desert/Sand use the shared Temperate cliff footprint and relief."""
import argparse,json,runpy,sys
from pathlib import Path
from PIL import Image

def run(source,install):
    sys.path.insert(0,str(source/'tools/bakery'))
    import terrain_daylight as day
    h=runpy.run_path(str(source/'tools/gate-shared-cliffs.py'))
    ground=h['masks'](install/'groundmask_DESERT.pack');sand=h['masks'](install/'groundmask_SAND.pack');height=h['masks'](install/'terrainsurface_DESERT.pack',True)
    count=cells=0;mapping=[]
    for r in day.rows('DESERT'):
        s=day.surface_source(r)
        if s is r:continue
        w,hh=r['cells'];coverage=day.checked(s,'ground_mask.png','L');relief=day.checked(s,'height.png','L')
        for icon in r['have']:
            y,x=divmod(icon,w);box=(x*144,y*144,(x+1)*144,(y+1)*144);key=r['tid'],icon,r['variant'],0
            expected=coverage.crop(box).tobytes()
            assert ground[key][::2]==sand[key][::2]==expected,(r['export'],icon,'coverage')
            assert height[key]==relief.crop(box).resize((48,48),Image.Resampling.BOX).tobytes(),(r['export'],icon,'relief')
            cells+=1
        count+=1;mapping.append(dict(desert=r['export'],shared=s['export']))
    assert count==56
    return dict(passed=True,cliff_versions=count,cells=cells,shared_temperate_ground_and_relief=True,mapping=mapping)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[1]);p.add_argument('--playable',type=Path,required=True);p.add_argument('--output',type=Path);a=p.parse_args();r=run(a.source,a.playable)
    if a.output:a.output.write_text(json.dumps(r,indent=2)+'\n')
    print(json.dumps({k:v for k,v in r.items()if k!='mapping'},indent=2))
