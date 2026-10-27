#!/usr/bin/env python3
from pathlib import Path
import numpy as np,sys,json,hashlib,shutil,io
from PIL import Image
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'game'));import fbx_read
r=Path(__file__).resolve().parents[1];out=r/'art/interface/enhanced-menu-logos-v01'
models={};manifest=json.loads((out/'manifest.json').read_text());header=['/* Exact website FBX geometry/normals/UVs and embedded textures. GDI gold warmed to orange. */']
for k in ('gdi','nod'):
 ms,root=fbx_read.meshes(str(out/(k+'.fbx')));m=ms[0];meta=manifest[k];t=m['T'];p=(m['P']-np.array(meta['center']))/meta['scale'];data=np.concatenate([p[t].reshape(-1,3),m['N'][t].reshape(-1,3),m['UV'][t].reshape(-1,2)],axis=1).astype(np.float32)
 mats=m['mat_per_poly'][m['poly_of_pv'][t[:,0]]];obj=next(n for n in root if n.name=='Objects');videos={v.find('RelativeFilename').props[0]:v.find('Content').props[0] for v in obj.findall('Video')};tx=fbx_read.textures(root);names=[tx[n]['DiffuseColor'] for n in m['slots']]
 models[k]={'vertices':data.round(7).reshape(-1).tolist(),'materials':mats.tolist(),'textures':names,'triangleCount':len(t)}
 def arr(name,data,typ): header.append('static const '+typ+' '+name+'[]={'+','.join(data)+'};')
 arr('menu_logo_'+k,[format(float(v),'.7g')+'f' if '.' in format(float(v),'.7g') or 'e' in format(float(v),'.7g') else format(float(v),'.7g')+'.0f' for v in data.reshape(-1)],'float')
 arr('menu_logo_'+k+'_materials',map(str,mats),'unsigned char');pairs=[]
 for i,name in enumerate(names):
  raw=videos[name];(out/name).write_bytes(raw);im=Image.open(io.BytesIO(raw)).convert('RGBA');assert im.size==(32,32);mask=name.replace('.png','_player-mask.png');Image.new('L',im.size,0).save(out/mask)
  rgba=np.array(im);q=rgba[:,:,:3].astype(float);gold=(q[:,:,0]>q[:,:,1]*1.1)&(q[:,:,0]>q[:,:,2]*1.4)
  if k=='gdi':q[:,:,1][gold]*=.75;q[:,:,2][gold]*=.45;rgba[:,:,:3]=q.astype('uint8')
  arr('menu_logo_'+k+'_texture'+str(i),map(str,rgba.reshape(-1)),'unsigned char');pairs.append({'albedo':name,'albedoSha256':hashlib.sha256(raw).hexdigest(),'playerMask':mask,'maskIntent':'all-black decorative emblem; no player recolouring','dimensions':[32,32]})
 meta['texturePairs']=pairs;meta['transform']='positions=(source-center)/scale; source triangles, corner normals, UVs and material indices unchanged';meta['displayPalette']='embedded source textures; GDI gold warmed to requested orange at render, silver/black preserved'
(out/'mesh.json').write_text(json.dumps(models,separators=(',',':')));(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');(out/'menu_faction_meshes.h').write_text('\n'.join(header)+'\n')
print({k:v['triangleCount'] for k,v in models.items()})
