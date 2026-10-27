"""Fresh-decode, pack and render the representative PROC mapped-material trial.
No high/low source is overwritten; whole-asset propagation remains gated.
"""
import bpy,sys,json,hashlib,struct,zlib,argparse
from pathlib import Path
import numpy as np
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'art/refinery/texture_v01';REVIEW=OUT/'material_review';REVIEW.mkdir(exist_ok=True)
p=argparse.ArgumentParser();p.add_argument('--assemble',action='store_true');p.add_argument('--reverse',action='store_true');p.add_argument('--views',nargs='+',default=['hero','tank','paint','apron','core']);p.add_argument('--variants',nargs='+',default=['GDI']);p.add_argument('--samples',type=int,default=20);a=p.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
UV=OUT/'PROC_TextureUV_v01.blend';DEST=OUT/'PROC_MaterialTrial_v01.blend'
CAMS={'hero':((22,-50,32),(-2.8,-.2,7.8),48,(1200,1000)),
 'tank':((22,-18,15),(4.9,-1.92,4.8),12,(1000,800)),
 'paint':((18,-24,24),(-3.3,-1,10.8),18,(1000,800)),
 'apron':((9,-32,29),(-4.5,-13.5,.5),23,(1000,800)),
 'core':((10,22,28),(-5.1,6.15,6.8),15,(1000,800))}
TINTS={'GDI':(1,.65,.20,1),'Nod':(.82,.07,.045,1),'Blue':(.065,.27,.85,1)}

def geometry_hash(objects):
 h=hashlib.sha256()
 for o in objects:
  h.update(o.name.encode())
  for v in o.data.vertices:h.update(struct.pack('<3f',*v.co))
  for p in o.data.polygons:h.update(struct.pack('<3I',*p.vertices));h.update(struct.pack('<?',p.use_smooth))
  for n in o.data.corner_normals:h.update(struct.pack('<3f',*n.vector))
  for uv in o.data.uv_layers['BakeUV'].data:h.update(struct.pack('<2f',*uv.uv))
  h.update(np.asarray(o.matrix_world,np.float64).tobytes())
 return h.hexdigest()

def assemble():
 bpy.ops.wm.open_mainfile(filepath=str(UV));s=bpy.context.scene
 low=bpy.data.collections['PROC_FinalLow_v01_RUNTIME'];objects=sorted([o for o in low.objects if o.type=='MESH'],key=lambda o:o.name)
 fingerprint=geometry_hash(objects)
 author=json.loads((OUT/'MATERIAL_AUTHORING_REPORT.json').read_text());physical=json.loads((OUT/'PHYSICAL_BAKE_REPORT.json').read_text())
 assert fingerprint==physical['geometry_uv_corner_normal_hash']
 trial=set(author['trial_receivers']);assert trial==set(json.loads((OUT/'SOURCE_RECEIVER_MAP.json').read_text())['receiver_to_sources'])
 maps={k:dict(v) for k,v in author['files'].items()}
 for r in physical['output_maps']:maps[r['channel']]=dict(r)
 images={};values={};checks={}
 for channel,row in maps.items():
  f=ROOT/row['path'];assert sha(f)==row['sha256'];im=bpy.data.images.load(str(f),check_existing=False);im.name='PROC_Trial_'+channel
  im.colorspace_settings.name='sRGB' if channel=='BaseColor' else 'Non-Color';im.reload()
  assert tuple(im.size)==(4096,4096)
  buf=np.empty(4096*4096*4,np.float32);im.pixels.foreach_get(buf);assert np.isfinite(buf).all()
  checks[channel]={'decoded_min':float(buf.min()),'decoded_max':float(buf.max()),'packed':True,'sha256':sha(f),'bit_depth':16,'colorspace':im.colorspace_settings.name,'path':row['path'],'UV':'BakeUV'}
  if channel in ('PlayerColorMask','SemanticA','SemanticB','SemanticC','Metallic','Roughness'):values[channel]=buf.reshape(4096,4096,4)[:,:,:3].copy()
  im.pack();images[channel]=im
 mask=values['PlayerColorMask'];assert np.max(np.abs(mask[:,:,0]-mask[:,:,1]))<2/65535 and np.max(np.abs(mask[:,:,1]-mask[:,:,2]))<2/65535
 strong=mask[:,:,0]>.9;fixed=values['SemanticC'][:,:,0];concrete=values['SemanticC'][:,:,1];primer=values['SemanticA'][:,:,1];exposed=values['SemanticA'][:,:,2];deposits=values['SemanticB'].max(2)
 conflict=int(np.sum(strong&((fixed>.1)|(concrete>.1)|(primer>.1)|(exposed>.1)|(deposits>.1))))
 assert conflict==0,('Strong player mask overlaps excluded material',conflict)
 distributions={}
 for name,selection in [('paint',values['SemanticA'][:,:,0]>.9),('bare_metal',(fixed+exposed)>.9),('concrete',concrete>.9),('deposits',deposits>.5)]:
  if selection.any():distributions[name]={'pixels':int(selection.sum()),'roughness_percentiles':np.percentile(values['Roughness'][:,:,0][selection],[0,10,50,90,100]).tolist(),'metallic_percentiles':np.percentile(values['Metallic'][:,:,0][selection],[0,10,50,90,100]).tolist()}
 del values
 mat=bpy.data.materials.new('PROC_Representative_MappedMaterial');mat.use_nodes=True;n=mat.node_tree.nodes;l=mat.node_tree.links;n.clear()
 def node(t,name=None):
  q=n.new(t)
  if name:q.name=name
  return q
 uv=node('ShaderNodeUVMap');uv.uv_map='BakeUV';tex={}
 for channel,im in images.items():
  t=node('ShaderNodeTexImage','MAP_'+channel);t.image=im;t.interpolation='Linear';t.extension='EXTEND';l.new(uv.outputs['UV'],t.inputs['Vector']);tex[channel]=t.outputs['Color']
 out=node('ShaderNodeOutputMaterial');bs=node('ShaderNodeBsdfPrincipled');l.new(bs.outputs['BSDF'],out.inputs['Surface'])
 tint=node('ShaderNodeRGB','PLAYER_TINT');tint.outputs[0].default_value=TINTS['GDI']
 multiply=node('ShaderNodeMixRGB');multiply.blend_type='MULTIPLY';multiply.inputs[0].default_value=1;l.new(tex['BaseColor'],multiply.inputs[1]);l.new(tint.outputs[0],multiply.inputs[2])
 recolor=node('ShaderNodeMixRGB','PLAYER_MIX');l.new(tex['PlayerColorMask'],recolor.inputs[0]);l.new(tex['BaseColor'],recolor.inputs[1]);l.new(multiply.outputs[0],recolor.inputs[2])
 ao=node('ShaderNodeMath');ao.operation='MULTIPLY_ADD';l.new(tex['AO'],ao.inputs[0]);ao.inputs[1].default_value=.28;ao.inputs[2].default_value=.72
 composite=node('ShaderNodeMixRGB');composite.blend_type='MULTIPLY';composite.inputs[0].default_value=1;l.new(recolor.outputs[0],composite.inputs[1]);l.new(ao.outputs[0],composite.inputs[2]);l.new(composite.outputs[0],bs.inputs['Base Color'])
 l.new(tex['Roughness'],bs.inputs['Roughness']);l.new(tex['Metallic'],bs.inputs['Metallic'])
 normal=node('ShaderNodeNormalMap','STRUCTURAL_NORMAL');normal.uv_map='BakeUV';l.new(tex['Normal'],normal.inputs['Color'])
 bump=node('ShaderNodeBump','MATERIAL_MICROHEIGHT');bump.inputs['Strength'].default_value=1;bump.inputs['Distance'].default_value=.03;l.new(tex['SurfaceHeight'],bump.inputs['Height']);l.new(normal.outputs['Normal'],bump.inputs['Normal']);l.new(bump.outputs['Normal'],bs.inputs['Normal'])
 mat['player_formula']='mix(BaseColor, BaseColor*PlayerTint, PlayerColorMask)';mat['AO_formula']='RecolouredBaseColor * (.72 + .28 * AO), once';mat['trial_receivers_json']=json.dumps(sorted(trial))
 for o in objects:
  if o.name in trial:
   for slot in o.material_slots:slot.material=mat
 for o in s.objects:
  if o.type=='LIGHT':o.hide_render=False
 bpy.data.collections['QA_PaintLayout_NONEXPORT'].hide_render=True
 bpy.data.collections['PROC_High_v04_NONEXPORT_BAKE_SOURCE'].hide_render=True
 assert geometry_hash(objects)==fingerprint
 s['material_trial_scope']='Six representative receivers only; remaining surfaces retain provisional ID materials. Awaiting design-review material approval.'
 s['material_trial_geometry_hash']=fingerprint
 s.render.engine='CYCLES';s.cycles.device='CPU';s.render.threads_mode='FIXED';s.render.threads=4
 bpy.ops.wm.save_as_mainfile(filepath=str(DEST),compress=True)
 report={'status':'Representative mapped trial pending approval','blend':str(DEST.relative_to(ROOT)),'blend_sha256':sha(DEST),'source_uv_sha256':sha(UV),'geometry_uv_normals_sha256':fingerprint,'runtime_triangles':9796,'trial_receivers':sorted(trial),'files':checks,'player_mask':{'grayscale':True,'linear_non_color':True,'strong_conflicts_with_excluded_classes':conflict,'pairing':'PROC_Trial_BaseColor.png / BakeUV / 4096x4096','tints_linear':TINTS},'material_distributions':distributions,'microheight':{'independently_authored':True,'nominal_distance':.03,'after_physical_normal':True,'not_in_structural_normal_png':True},'runtime_integration':False}
 (OUT/'MATERIAL_TRIAL_REPORT.json').write_text(json.dumps(report,indent=2)+'\n')
 print('PROC_MATERIAL_ASSEMBLED',flush=True)

if a.assemble:assemble()
# Reopen fresh packed scene for all review evidence.
bpy.ops.wm.open_mainfile(filepath=str(DEST));s=bpy.context.scene;s.cycles.samples=a.samples;s.cycles.use_denoising=True
s.render.resolution_percentage=100;s.render.image_settings.file_format='PNG';s.render.image_settings.color_mode='RGBA';s.render.image_settings.color_depth='8'
if a.reverse:
 lights=[o for o in s.objects if o.type=='LIGHT'];key=max(lights,key=lambda o:o.data.energy);center=Vector((-2.8,-.2,7.8));key.location=(2*center.x-key.location.x,2*center.y-key.location.y,key.location.z);key.rotation_euler=(center-key.location).to_track_quat('-Z','Y').to_euler()
mat=bpy.data.materials['PROC_Representative_MappedMaterial'];n=mat.node_tree.nodes;l=mat.node_tree.links;bs=next(x for x in n if x.type=='BSDF_PRINCIPLED');out=next(x for x in n if x.type=='OUTPUT_MATERIAL');em=n.new('ShaderNodeEmission')
rows=[]
for variant in a.variants:
 l.new(bs.outputs['BSDF'],out.inputs['Surface'])
 n['PLAYER_TINT'].outputs[0].default_value=TINTS.get(variant,TINTS['GDI'])
 if variant in ('BaseColor','Roughness','Metallic','PlayerColorMask'):
  l.new(n['MAP_'+variant].outputs['Color'],em.inputs['Color']);l.new(em.outputs[0],out.inputs['Surface'])
 for view in a.views:
  loc,target,scale,res=CAMS[view];cam=s.camera;cam.location=loc;cam.rotation_euler=(Vector(target)-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=scale;s.render.resolution_x,s.render.resolution_y=res
  f=REVIEW/(view+'_'+variant+('_reverse' if a.reverse else '')+'.png');s.render.filepath=str(f);bpy.ops.render.render(write_still=True);rows.append({'view':view,'variant':variant,'file':str(f.relative_to(ROOT)),'sha256':sha(f)});print('PROC_MATERIAL_RENDER',view,variant,flush=True)
report={'source_blend_sha256':sha(DEST),'scope':'Actual Blender material trial; six selected receiver surfaces, remaining provisional ID materials','cameras':CAMS,'view_settings':{'transform':s.view_settings.view_transform,'look':s.view_settings.look,'exposure':s.view_settings.exposure},'renders':rows}
(REVIEW/('RENDER_'+'_'.join(a.variants)+('_reverse' if a.reverse else '')+'.json')).write_text(json.dumps(report,indent=2)+'\n')
