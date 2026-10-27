"""Front-apron enlargement proposal; existing High v02 structure is preserved."""
import bpy,json,hashlib,math
from pathlib import Path
from mathutils import Vector,Matrix
from mathutils.bvhtree import BVHTree
ROOT=Path(__file__).resolve().parents[2];OLD=ROOT/'art/refinery/high_v02';OUT=ROOT/'art/refinery/high_v03';OUT.mkdir(exist_ok=True)
SOURCE=OLD/'PROC_High_v02.blend';expected='efa6212b9441eaae34ae4bffdfd2cdf748eda9cfc8c95418077b2a638df6ff51'
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest()==expected
bpy.ops.wm.open_mainfile(filepath=str(SOURCE));scene=bpy.context.scene
high=bpy.data.collections['PROC_High_v02_NONEXPORT_BAKE_SOURCE'];high.name='PROC_High_v03_NONEXPORT_BAKE_SOURCE'
source_objects={o.name:{'matrix':list(sum((list(row) for row in o.matrix_world),[])),'mesh':hashlib.sha256(b''.join(__import__('struct').pack('3f',*v.co) for v in o.data.vertices)).hexdigest()} for o in high.objects if o.type=='MESH'}
(OUT/'SOURCE_REVIEW.json').write_text(json.dumps({'opened_source':str(SOURCE),'sha256':expected,'actual_source_views':str(OLD/'PROC_High_v02_contact_sheet.png'),'object_count':len(source_objects)},indent=2)+'\n')
EXTENSION=5.4
base=bpy.data.objects['Foundation_SOURCE_CONTOUR_EXACT'];old_vertices=[list(v.co) for v in base.data.vertices]
for name in ['Foundation_SOURCE_CONTOUR_EXACT','Foundation_inset_top_course']:
 o=bpy.data.objects[name]
 for v in o.data.vertices:
  if v.index%6<3:v.co.y-=EXTENSION
base.name='Foundation_FRONT_APRON_REVISED';base['source_preserved_exactly']=False
base['footprint_status']='Front extension proposed by current apron revision; needs acceptance.'
for i,y in enumerate((-17.55,-15.25,-12.95)):
 o=bpy.data.objects[f'Apron_direction_arrow_ID_{i}'];points=[(-4.7,y-.35,.232),(-3.3,y-.35,.232),(-4,y+.55,.232)]
 for v,p in zip(o.data.vertices,points):v.co=p
new_vertices=[list(v.co) for v in base.data.vertices];poly=[p[:2] for p in new_vertices[6:]]
def clear(x,y):
 return min(((b[0]-a[0])*(y-a[1])-(b[1]-a[1])*(x-a[0]))/math.hypot(b[0]-a[0],b[1]-a[1]) for a,b in zip(poly,poly[1:]+poly[:1]))
# Import actual original harvester, at the same FBX scale; retain only as QA reference.
before=set(bpy.data.objects);fbx=ROOT/'art/refinery/harvester_fit_reference/HARV.fbx'
bpy.ops.import_scene.fbx(filepath=str(fbx));scene.frame_set(0);bpy.context.view_layer.update()
imported=[o for o in bpy.data.objects if o not in before];meshes=[o for o in imported if o.type=='MESH' and 'SHADOW' not in o.name]
harv=bpy.data.collections.new('QA_HARV_SIZE_REFERENCE_NONEXPORT');scene.collection.children.link(harv)
dg=bpy.context.evaluated_depsgraph_get();raw=[]
for o in meshes:
 e=o.evaluated_get(dg);m=bpy.data.meshes.new_from_object(e,preserve_all_data_layers=True,depsgraph=dg);m.transform(o.matrix_world);raw.append((o.name,m))
for o in imported:bpy.data.objects.remove(o,do_unlink=True)
raw_points=[v.co.copy() for _,m in raw for v in m.vertices]
raw_bounds={'min':[min(p[k] for p in raw_points) for k in range(3)],'max':[max(p[k] for p in raw_points) for k in range(3)]}
rot=Matrix.Rotation(math.pi,4,'Z');rotpts=[rot@p for p in raw_points]
pos=Vector((-4,-6.65-max(p.y for p in rotpts),.24-min(p.z for p in rotpts)))
mat=bpy.data.materials.new('QA_HARV_muted_blue');mat.use_nodes=True
bs=mat.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(.09,.22,.30,1);bs.inputs['Roughness'].default_value=.68
for name,m in raw:
 m.transform(Matrix.Translation(pos)@rot);o=bpy.data.objects.new('QA_SIZE_'+name,m);harv.objects.link(o);m.materials.clear();m.materials.append(mat)
 for p in m.polygons:p.material_index=0
 o['runtime_candidate']=False;o['role']='Actual original harvester static size reference; not part of refinery asset.'
pts=[v.co.copy() for o in harv.objects for v in o.data.vertices]
fit={'source':str(fbx),'source_sha256':hashlib.sha256(fbx.read_bytes()).hexdigest(),'raw_bounds':raw_bounds,'placed_bounds':{'min':[min(p[k] for p in pts) for k in range(3)],'max':[max(p[k] for p in pts) for k in range(3)]},'minimum_planar_apron_edge_clearance':min(clear(p.x,p.y) for p in pts),'reference_only':True,'validation_scope':'Static original harvester in approach position, not engine navigation or docking animation.'}
def tree(objects):
 vs=[];fs=[]
 for o in objects:
  e=o.evaluated_get(dg);m=e.to_mesh();n=len(vs);vs.extend(o.matrix_world@v.co for v in m.vertices);fs.extend([n+i for i in p.vertices] for p in m.polygons);e.to_mesh_clear()
 return BVHTree.FromPolygons(vs,fs)
bpy.context.view_layer.update();dg=bpy.context.evaluated_depsgraph_get()
obstacles=[o for o in high.objects if o.type=='MESH' and o.get('assembly')!='foundation']
fit['harvester_to_structure_surface_intersection_pairs']=len(tree(list(harv.objects)).overlap(tree(obstacles)))
assert fit['minimum_planar_apron_edge_clearance']>=0,fit
assert fit['harvester_to_structure_surface_intersection_pairs']==0,fit
(OUT/'HARV_STATIC_FIT.json').write_text(json.dumps(fit,indent=2)+'\n')
for o in harv.objects:o.hide_render=True;o.hide_set(True)
for o in high.objects:o['stage']='High v03 / NONEXPORT'
changed={'Foundation_SOURCE_CONTOUR_EXACT','Foundation_inset_top_course',*[f'Apron_direction_arrow_ID_{i}' for i in range(3)]}
for name,record in source_objects.items():
 if name in changed:continue
 o=bpy.data.objects[name]
 assert list(sum((list(row) for row in o.matrix_world),[]))==record['matrix'],name
 assert hashlib.sha256(b''.join(__import__('struct').pack('3f',*v.co) for v in o.data.vertices)).hexdigest()==record['mesh'],name
manifest={'source_sha256':expected,'approval':'pending; explicit acceptance of front footprint extension required','extension_towards_front':EXTENSION,'old_foundation_vertices':old_vertices,'new_foundation_vertices':new_vertices,'unchanged_above_apron_geometry':True,'arrows':'Three arrows spaced along enlarged concrete approach','xy_original':{'min':[-15.78,-14.64],'max':[11.25,13.76]},'xy_proposed':{'min':[-15.78,-20.04],'max':[11.25,13.76]},'note':'This is a proposed exception to the original full shaped footprint, prompted by the design-review request for a larger harvester approach. Width, rear edge, building, tanks, pipes, supports and filled hazard field are unchanged.'}
(OUT/'REVISION_MANIFEST.json').write_text(json.dumps(manifest,indent=2)+'\n')
views=json.loads((OLD/'REVIEW_CAMERAS.json').read_text());views['apron_detail']={'location':[9,-32,29],'target':[-4.5,-13.5,.5],'ortho_scale':23}
# Wider framing used consistently for before/after views so extension stays uncropped.
for name in ['hero','top','side']:
 views[name]['ortho_scale']+=5
views['top']={'location':[-2.3,-3.14,75],'target':[-2.3,-3.14,0],'ortho_scale':48}
views['harvester_fit']=views['hero'];views['harvester_fit_top']=views['top']
(OUT/'REVIEW_CAMERAS.json').write_text(json.dumps(views,indent=2)+'\n')
scene['high_review_state']='High v03 front apron extension proposal; approval pending'
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'PROC_High_v03.blend'))
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest()==expected
print(json.dumps(fit,indent=2));print('Saved High v03; v02 and above-apron geometry preserved.')
