#!/usr/bin/env python3
"""Pack accepted v06 native RGBA artwork; never resample chrome or ship Lab fixtures."""
import argparse, hashlib, json, struct
from pathlib import Path
from PIL import Image, ImageChops

def bake(source, output):
    m = json.loads((source / 'manifest.json').read_text())
    eva = Image.open(source / m['assets']['eva']['file']).convert('RGBA')
    # Exact browser v04 fractional silhouette, then the accepted internal recess walls.
    mask = Image.new('L', (80, 30))
    p = eva.load()
    for y in range(30):
        for x in range(80):
            samples = [p[sx,sy] for sy in range(y*eva.height//30,(y+1)*eva.height//30)
                       for sx in range(x*eva.width//80,(x+1)*eva.width//80)]
            mask.putpixel((x,y), round(255*sum(all(c>245 for c in v[:3]) for v in samples)/len(samples)))
    def wall(dx,dy):
        shifted=Image.new('L',mask.size);shifted.paste(mask,(dx,dy))
        # Canvas destination-out preserves fractional edge coverage multiplicatively.
        return ImageChops.multiply(mask, ImageChops.invert(shifted))
    records=[]; proofs={}
    for name, a in m['assets'].items():
        if name in ('gameplay','eva','eva-source','title'): continue
        path=source/a['file']
        assert hashlib.sha256(path.read_bytes()).hexdigest()==a['sha256'], name
        im=Image.open(path).convert('RGBA')
        if name.endswith('-frame') and name not in ('intro-frame',):
            for alpha,col in [(mask.point(lambda v:round(v*.34)),(0,0,0)),
                              (wall(2,2).point(lambda v:round(v*.9)),(0,0,0)),
                              (wall(-1,-1).point(lambda v:round(v*.68)),(133,139,125))]:
                layer=Image.new('RGBA',mask.size,col+(255,));layer.putalpha(alpha)
                im.alpha_composite(layer,(32,24))
        data=im.tobytes()
        records.append((name,im.width,im.height,data))
        proofs[name]={'source_sha256':a['sha256'],'rgba_sha256':hashlib.sha256(data).hexdigest(),
                      'width':im.width,'height':im.height}
    out=bytearray(b'ENMENU06'+struct.pack('<I',len(records)))
    for name,w,h,data in records:
        key=name.encode('ascii'); assert len(key)<64
        out+=key.ljust(64,b'\0')+struct.pack('<III',w,h,len(data))+data
    output.write_bytes(out)
    output.with_suffix('.json').write_text(json.dumps({'version':6,'assets':proofs,
        'pack_sha256':hashlib.sha256(out).hexdigest(),'fixture_backdrop_shipped':False},indent=2)+'\n')
    print(f'Enhanced menu v06: {len(records)} assets, {len(out):,} bytes -> {output}')

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('source',type=Path);ap.add_argument('output',type=Path)
    a=ap.parse_args();bake(a.source,a.output)
