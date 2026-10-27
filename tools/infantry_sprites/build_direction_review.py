#!/usr/bin/env python3
"""Package exact generated design directions and source references for Lab review.

Copies original PNG bytes. Canvas normalization is presentation only; it never
produces native/runtime pixels or converts design approval into animation approval.
"""
from pathlib import Path
import argparse, hashlib, json, shutil

NAMES={'E2':'Grenadier','E3':'Rocket Soldier','E4':'Flamethrower','E5':'Chemical Warrior','E6':'Engineer','RMBO':'Commando',**{f'C{i}':f'Civilian {i}'for i in range(1,11)},'MOEBIUS':'Dr. Moebius','DELPHI':'Delphi','CHAN':'Dr. Chan'}

HTML='''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Infantry designs · Remaster Lab</title>
<style>
:root{color-scheme:dark;font-family:system-ui,sans-serif;background:#171b20;color:#e0e5e8}*{box-sizing:border-box}body{max-width:1480px;margin:auto;padding:24px}a{color:#e7bd70}header{display:flex;justify-content:space-between;gap:24px;align-items:start}h1{font-size:25px;margin:8px 0}p{color:#aeb8bf;line-height:1.5}header p{margin:4px 0}.controls{display:flex;gap:18px;align-items:end;flex-wrap:wrap;margin:24px 0}label{display:grid;gap:6px;font-size:12px;color:#aeb8bf}select,button{font:inherit;font-size:15px;background:#252c34;border:1px solid #48515c;border-radius:5px;color:#e0e5e8;padding:9px 12px}button{cursor:pointer}.state{color:#e7bd70;align-self:center;font-size:13px}.comparison{display:grid;grid-template-columns:1fr 1fr;gap:16px}article,.reference{background:#1b2027;border:1px solid #343e47;border-radius:8px;overflow:hidden}h2{font-size:16px;margin:0;padding:14px 18px;border-bottom:1px solid #343e47}h2 small{color:#9faab3;font-weight:400}article canvas{display:block;width:100%;height:auto;image-rendering:pixelated}.viewlabels{display:flex;justify-content:space-around;font-size:11px;letter-spacing:.1em;color:#aeb8bf;padding-bottom:12px}.reference{margin-top:18px;display:flex;align-items:center;gap:16px;padding:12px 18px}.reference p{font-size:13px;max-width:360px}.reference canvas{width:480px;max-width:100%;height:auto;image-rendering:pixelated}footer{margin-top:18px;font-size:12px}#identity{min-height:24px}#error{color:#ffac8f}details{margin-top:12px}summary{cursor:pointer}details p{max-width:900px}nav{display:flex;gap:20px;margin-top:16px}a:focus-visible,button:focus-visible,select:focus-visible{outline:2px solid #e7bd70;outline-offset:3px}@media(max-width:780px){body{padding:16px}.comparison{grid-template-columns:1fr}.reference{flex-direction:column;align-items:start}header{display:block}.reference canvas{width:100%}h1{font-size:22px}}
</style></head><body>
<header><div><a href="../">← Sprite animations</a><h1>Infantry · character designs</h1><p>Original identity, revised front and profile.</p></div><span class="state">Design direction · v07 / Flamethrower v08</span></header>
<div class="controls"><label>Group<select id="group"><option value="all">All infantry</option><option value="military">Military</option><option value="civilian">Civilians</option><option value="mission">Mission characters</option></select></label><label>Character<select id="unit"></select></label><button id="previous" aria-label="Previous character">←</button><button id="next" aria-label="Next character">→</button><span id="status" class="state"></span></div>
<p id="identity"></p><p id="error" role="alert" hidden></p>
<main class="comparison"><article><h2>Before <small>1995 MS-DOS</small></h2><canvas id="before" width="640" height="390" aria-label="Original front and east profile"></canvas><div class="viewlabels"><span>FRONT · SOUTH</span><span>EAST PROFILE</span></div></article><article><h2>After <small>Generated design direction</small></h2><canvas id="after" width="640" height="390" aria-label="Revised generated front and east profile"></canvas><div class="viewlabels"><span>FRONT · SOUTH</span><span>EAST PROFILE</span></div></article></main>
<aside class="reference"><div><strong>Approved Minigunner</strong><p>Exact v15 native artwork. Compare connected body shapes, restrained cloth shading and readable equipment.</p></div><canvas id="baseline" width="480" height="260" aria-label="Exact approved Minigunner front and side"></canvas></aside>
<footer><p>The two sides are displayed at comparable character height for design review. Generated design sheets are visual references; native animation frames still require source-motion analysis and individual frame audits.</p><nav><a id="full" href="#" target="_blank">Open exact design image</a><a id="source" href="#" target="_blank">Open DOS source sheet</a><a href="manifest.json" target="_blank">Versions and approval records</a><a id="motion" href="study-e2-run-v28/" hidden>Review Grenadier run revision</a></nav></footer>
<script type="module">
const q=s=>document.querySelector(s),manifest=await fetch('manifest.json').then(r=>{if(!r.ok)throw Error('Manifest unavailable');return r.json()}),group=q('#group'),unit=q('#unit'),cache=new Map();let seq=0;
async function img(path){if(!cache.has(path))cache.set(path,new Promise((resolve,reject)=>{const i=new Image;i.onload=()=>resolve(i);i.onerror=()=>{cache.delete(path);reject(Error('Image unavailable: '+path))};i.src=path}));return cache.get(path)}
function bounds(image,rect){const c=document.createElement('canvas');c.width=rect.w;c.height=rect.h;const x=c.getContext('2d');x.drawImage(image,rect.x,rect.y,rect.w,rect.h,0,0,rect.w,rect.h);const d=x.getImageData(0,0,c.width,c.height).data;let l=c.width,t=c.height,r=-1,b=-1;for(let y=0;y<c.height;y++)for(let xx=0;xx<c.width;xx++)if(d[(y*c.width+xx)*4+3]>32){l=Math.min(l,xx);r=Math.max(r,xx);t=Math.min(t,y);b=Math.max(b,y)}if(r<l)throw Error('Empty reference image');return{x:rect.x+l,y:rect.y+t,w:r-l+1,h:b-t+1}}
function clear(c){const x=c.getContext('2d');x.imageSmoothingEnabled=false;x.fillStyle='#191d23';x.fillRect(0,0,c.width,c.height);return x}
function pair(c,image,rects,native=false,height=264){const x=clear(c);for(let n=0;n<rects.length;n++){const b=bounds(image,rects[n]),s=native?Math.max(1,Math.floor(height/b.h)):height/b.h,w=b.w*s,h=b.h*s;x.drawImage(image,b.x,b.y,b.w,b.h,(n+.5)*c.width/2-w/2,c.height-42-h,w,h)}}
async function render(){const token=++seq,data=manifest.units.find(u=>u.id===unit.value);if(!data)return;q('#error').hidden=true;q('#status').textContent=data.approval;q('#identity').textContent=data.name+' · '+data.identity;q('#motion').hidden=data.id!=='E2';q('#full').href=data.design;q('#source').href=data.original;history.replaceState(null,'','#'+data.id);try{const [old,newer]=await Promise.all([img(data.original),img(data.design)]);if(token!==seq)return;pair(q('#before'),old,[{x:4*50,y:0,w:50,h:39},{x:6*50,y:0,w:50,h:39}],true);pair(q('#after'),newer,[{x:0,y:0,w:Math.floor(newer.width/2),h:newer.height},{x:Math.floor(newer.width/2),y:0,w:newer.width-Math.floor(newer.width/2),h:newer.height}]);}catch(e){if(token!==seq)return;clear(q('#before'));clear(q('#after'));q('#error').hidden=false;q('#error').textContent=e.message}}
function options(prefer){const list=manifest.units.filter(u=>group.value==='all'||u.group===group.value);unit.replaceChildren(...list.map(u=>new Option(u.id+' · '+u.name,u.id)));if(list.some(u=>u.id===prefer))unit.value=prefer;render()}
group.onchange=()=>options(unit.value);unit.onchange=render;for(const [id,step]of[['previous',-1],['next',1]])q('#'+id).onclick=()=>{unit.selectedIndex=(unit.selectedIndex+step+unit.options.length)%unit.options.length;render()};
const initial=decodeURIComponent(location.hash.slice(1));options(initial);const e1=await img('approved-e1.png');pair(q('#baseline'),e1,[{x:400,y:0,w:100,h:78},{x:600,y:0,w:100,h:78}],true,192);
</script></body></html>
'''

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--root',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);a=ap.parse_args()
    ref=a.root/'revision-v07-reference';source=a.root/'source-study-v02';out=a.out
    if out.exists():raise RuntimeError('Versioned output already exists')
    out.mkdir(parents=True);(out/'images').mkdir();(out/'originals').mkdir();(out/'records').mkdir()
    approval=json.loads((ref/'all-shown-direction-acceptance-2026-10-20.json').read_text())['accepted'];units=[]
    if (ref/'final-nine-acceptance-2026-10-20.json').exists():approval.update(json.loads((ref/'final-nine-acceptance-2026-10-20.json').read_text())['accepted'])
    for uid,name in NAMES.items():
        selected='E4-v08'if uid=='E4'else uid;design=ref/f'{selected}-generated-front-side-direction.png';old=source/uid/'original.png'
        shutil.copy2(design,out/'images'/f'{uid}.png');shutil.copy2(old,out/'originals'/f'{uid}.png')
        units.append({'id':uid,'name':name,'group':'military'if uid.startswith('E')or uid=='RMBO'else'mission'if uid in('MOEBIUS','DELPHI','CHAN')else'civilian','design':f'images/{uid}.png','original':f'originals/{uid}.png','sha256':sha(design),'original_sha256':sha(old),'version':'v08'if uid=='E4'else'v07','approval':'Design approved'if uid in approval else'Design awaiting feedback','identity':IDENTITY[uid]})
    for p in ref.glob('*acceptance*.json'):shutil.copy2(p,out/'records'/p.name)
    shutil.copy2(ref/'all-generation-prompts.json',out/'records/prompts.json')
    shutil.copy2(Path(__file__).resolve().parents[2]/'tools/sprite_lab/public/data/e1-motion-v15.png',out/'approved-e1.png')
    (out/'index.html').write_text(HTML)
    (out/'manifest.json').write_text(json.dumps({'date':'2026-10-20','kind':'Generated static design review; not native animation or runtime','baseline':'Exact E1v15','units':units},indent=2)+'\n')
    print('Packaged19exact design PNGs, original sources, baseline and scoped acceptance records:',out)

IDENTITY={
 'E2':'Khaki uniform, helmet, webbing and hand grenade.',
 'E3':'Khaki soldier with a shouldered anti-tank launcher.',
 'E4':'Fuel tank, connected hose and a clearer steel flamethrower gun.',
 'E5':'Protective green suit, dark respirator and chemical sprayer.',
 'E6':'Yellow hardhat, pale shirt, brown vest and red-brown tool case.',
 'RMBO':'Red beret, khaki short sleeves, green webbing and suppressed rifle.',
 'C1':'Dark hair, burgundy shirt and blue trousers.',
 'C2':'Blond hair, teal shirt and purple trousers.',
 'C3':'Blond woman, khaki blouse and brick-red trousers.',
 'C4':'Dark-haired woman, burgundy blouse and teal trousers.',
 'C5':'Dark skin and hair, blue-grey shirt and olive trousers.',
 'C6':'Brown hair, red shirt and teal trousers.',
 'C7':'Auburn hair, lime shirt and mauve trousers.',
 'C8':'Dark skin and hair, orange shirt and teal trousers.',
 'C9':'Bald crown, lavender-grey shirt and mauve trousers.',
 'C10':'Grey hair, pale shirt and blue-violet trousers.',
 'MOEBIUS':'Broad light-brown bob, white lab coat, tie and green trousers.',
 'DELPHI':'Original portrait: dark hair/moustache; pale shirt and violet trousers.',
 'CHAN':'Short black hair, white laboratory coat and blue-grey trousers.',
}
if __name__=='__main__':main()
