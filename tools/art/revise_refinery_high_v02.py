"""Versioned proportion correction from the preserved actual High v01."""
import bpy, json, hashlib, shutil, math
from pathlib import Path
from mathutils import Matrix, Vector

ROOT=Path(__file__).resolve().parents[2]
OLD=ROOT/'art/refinery/high_v01'; OUT=ROOT/'art/refinery/high_v02'
OUT.mkdir(exist_ok=True)
SOURCE=OLD/'PROC_High_v01.blend'
source_hash=hashlib.sha256(SOURCE.read_bytes()).hexdigest()
assert source_hash=='deab8fe83a098b603102b3fd4d0f5e4fcf0f6d2e5af84e818f1d466584fe61b0'
bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
scene=bpy.context.scene
high=bpy.data.collections['PROC_High_v01_NONEXPORT_BAKE_SOURCE']
# Save the unchanged opened-file inventory before touching geometry.
(OUT/'SOURCE_REVIEW.json').write_text(json.dumps({'source':str(SOURCE),'sha256':source_hash,'opened_meshes':len([o for o in high.objects if o.type=='MESH']),'preserved_source_views':str(OLD/'PROC_High_v01_contact_sheet.png'),'user_request':'The structure is now a bit too tall compared to the concept art. Also the large cylinder tank on the rear should be shorter and a bit larger'},indent=2)+'\n')

# 8% diameter growth fits the actual shaped apron; 12% failed its diagonal rear edge.
Z0=.23; BODY_Z=.90; CORE_Z=.82; RADIAL=5.35/4.95
def affine(sx,sy,sz,origin=(0,0,Z0),shift=(0,0,0)):
 return Matrix.Translation(Vector(origin)+Vector(shift)) @ Matrix.Diagonal((sx,sy,sz,1)) @ Matrix.Translation(-Vector(origin))
body=affine(1,1,BODY_Z)
core=affine(RADIAL,RADIAL,CORE_Z,(-3.5,5.75,Z0),(-1.6,.4,0))
# Original-reference triangular metal sheets. Each rectangular bay is divided
# into alternating, physically separated triangular plates with real thickness.
for o in list(high.objects):
 if '_curved_wall_panel_' in o.name:bpy.data.objects.remove(o,do_unlink=True)
def inset_triangle(points,amount):
 lines=[]
 for a,b in zip(points,points[1:]+points[:1]):
  a=Vector(a);b=Vector(b);d=(b-a).normalized();n=Vector((-d.y,d.x))
  lines.append((a+n*amount,d))
 result=[]
 for i,(p,d) in enumerate(lines):
  q,e=lines[i-1];delta=q-p;cross=d.x*e.y-d.y*e.x
  t=(delta.x*e.y-delta.y*e.x)/cross;result.append(p+d*t)
 return result
plate_count=0
for name,c,r,h,panels,tiers in [('Rear_processing_core',(-3.5,5.75),4.95,12,16,3),('Right_middle_vessel',(4.9,-1.92),2.92,9.5,12,3),('Right_front_vessel',(2.55,-8.24),2.13,6.15,10,2)]:
 for i in range(panels):
  da=2*math.pi/panels;a=(i+.5)*da;n=Vector((math.cos(a),math.sin(a),0));t=Vector((-math.sin(a),math.cos(a),0))
  half=(r+.10)*math.tan(da/2-.018)
  for j in range(tiers):
   lo=1.08+(h-.86-1.08)*j/tiers;hi=1.08+(h-.86-1.08)*(j+1)/tiers
   rect=[(-half,lo),(half,lo),(half,hi),(-half,hi)]
   ids=[(0,1,2),(0,2,3)] if (i+j)%2==0 else [(0,1,3),(1,2,3)]
   for k,ix in enumerate(ids):
    poly=inset_triangle([rect[x] for x in ix],.026)
    vs=[tuple(Vector((c[0],c[1],0))+n*(r+.065+depth)+t*p.x+Vector((0,0,p.y))) for depth in (0,.075) for p in poly]
    me=bpy.data.meshes.new('Interlocking_sheet');me.from_pydata(vs,[],[(2,1,0),(3,4,5),(0,1,4,3),(1,2,5,4),(2,0,3,5)]);me.update()
    o=bpy.data.objects.new(f'{name}_triangle_sheet_{i}_{j}_{k}',me);high.objects.link(o)
    me.materials.append(bpy.data.materials['HP_PREVIEW_vessel_steel_ID'] if (i+j+k)%5 else bpy.data.materials['HP_PREVIEW_dark_steel_ID'])
    o['assembly']=name;o['runtime_candidate']=False;o['allocation']='Bake fine triangular sheet seams; retain form-defining panel facets as needed in final low.'
    mod=o.modifiers.new('Designed_chamfer','BEVEL');mod.width=.014;mod.segments=3;mod.use_clamp_overlap=True
    mod=o.modifiers.new('Broad_planar_normals','WEIGHTED_NORMAL');mod.keep_sharp=True
    plate_count+=1
# Shorten only the rear end of the existing left support to clear wider core feet.
wing=bpy.data.objects['Left_wing_PROXY_CONTOUR_REBUILT_PLANAR']
for v in wing.data.vertices:
 if v.co.y>.50:v.co.y=.50
dg=bpy.context.evaluated_depsgraph_get()
changes=[]
for o in list(high.objects):
 if o.type!='MESH':continue
 a=o.get('assembly'); mode='body height'
 if a in {'foundation','docking_nozzle'}:
  # Preserve apron, markings and circular dock machinery, including seated feet.
  mode='unchanged'
 elif a in {'Rear_processing_core','core_access'} or o.name.startswith('Core_rear_'):
  o.matrix_world=core@o.matrix_world;mode='rear tank height, diameter and seat'
 elif o.name.startswith('Core_to_middle_') or o.name.startswith('Dock_to_core_'):
  # Existing pipe count/routes retained; refit their evaluated surfaces and sockets.
  ev=o.evaluated_get(dg)
  me=bpy.data.meshes.new_from_object(ev,preserve_all_data_layers=True,depsgraph=dg)
  mw=o.matrix_world.copy();me.transform(mw)
  for v in me.vertices:
   x,y,z=v.co
   if o.name.startswith('Core_to_middle_'):
    if '_start_' in o.name:t=1
    elif '_end_' in o.name:t=0
    else:t=max(0,min(1,(y-1.72)/(5.75-1.72)))
    v.co=(x-1.2*t,y+.4*t,Z0+(z-Z0)*(BODY_Z+(CORE_Z-BODY_Z)*t))
   else:
    if '_start_' in o.name:t=0
    elif '_end_' in o.name:t=1
    else:t=max(0,min(1,(y+.2)/1.02))
    # Keep the connection on the new tank centreline at the tank end.
    v.co=(x-1.6*t,y,Z0+(z-Z0)*(BODY_Z+(CORE_Z-BODY_Z)*t))
  o.modifiers.clear();o.data=me;o.matrix_world=Matrix.Identity(4)
  mode='existing connection refitted at both ends'
 else:o.matrix_world=body@o.matrix_world
 o['stage']='High v02 / NONEXPORT';changes.append({'object':o.name,'revision':mode})
high.name='PROC_High_v02_NONEXPORT_BAKE_SOURCE'
# Remastered sprite: stripes fill the shaped bay between the support toes,
# followed by three arrows along the approach, not two arrows across its width.
for o in list(high.objects):
 if o.name=='Dock_entry_threshold' or o.name.startswith(('Dock_hazard_stripe_ID_','Apron_direction_arrow_ID_')):
  bpy.data.objects.remove(o,do_unlink=True)
def planar(name,points,z,material):
 me=bpy.data.meshes.new(name+'_mesh');me.from_pydata([(x,y,z) for x,y in points],[],[tuple(range(len(points)))]);me.update()
 o=bpy.data.objects.new(name,me);high.objects.link(o);me.materials.append(material)
 o['assembly']='foundation';o['stage']='High v02 / NONEXPORT';o['runtime_candidate']=False
 o['allocation']='Planar preview paint; transfer layout to final textures after final-low approval.'
 return o
bay=[(-9.413,-10.60),(-1.05,-10.60),(-1.05,-6.13),(-6.795,-6.13)]
planar('Dock_hazard_stripe_ID_shaped_dark_field',bay,.226,bpy.data.materials['HP_PREVIEW_recess_ID'])
def clip_half(p,axis,limit,greater):
 out=[]
 for a,b in zip(p,p[1:]+p[:1]):
  va=a[0]-a[1];vb=b[0]-b[1]
  ia=va>=limit if greater else va<=limit;ib=vb>=limit if greater else vb<=limit
  if ia:out.append(a)
  if ia!=ib:
   t=(limit-va)/(vb-va);out.append(tuple(a[k]+t*(b[k]-a[k]) for k in range(2)))
 return out
for i in range(-6,10):
 p=clip_half(bay,0,i*2.1,True)
 if p:p=clip_half(p,0,i*2.1+1.0,False)
 if len(p)>2:planar(f'Dock_hazard_stripe_ID_{i}',p,.230,bpy.data.materials['HP_PREVIEW_fixed_hazard_yellow'])
arrows=[]
for i,y in enumerate((-13.04,-12.07,-11.10)):
 x=-4.0;points=[(x-.60,y-.25),(x+.60,y-.25),(x,y+.40)]
 planar(f'Apron_direction_arrow_ID_{i}',points,.232,bpy.data.materials['HP_PREVIEW_dark_steel_ID']);arrows.append(points)
(OUT/'APRON_LAYOUT.json').write_text(json.dumps({'source_url':'https://www.spriters-resource.com/pc_computer/commandconquerremasteredcollection/asset/141578/','image_url':'https://www.spriters-resource.com/media/assets/138/141578.png?updated=1755481974','observed':'Opened online sprite at full resolution: full shaped striped bay between support toes and three arrows in one approach line.','bay_polygon_xy':bay,'arrow_triangles_xy':arrows,'base_outline':'unchanged','status':'provisional geometry paint IDs; final texture/mask stages remain gated'},indent=2)+'\n')
scene['revision_source_sha256']=source_hash
scene['high_review_state']='High v02: revised proportions, pending design-review approval'
scene['pipe_perimeter_contract']='All existing pipes, sockets and supports inside original shaped apron; no new tube or perimeter extension.'
shutil.copyfile(OLD/'REVIEW_CAMERAS.json',OUT/'REVIEW_CAMERAS.json')
manifest={'source':str(SOURCE),'source_sha256':source_hash,'date':'2026-09-28','approval':'pending','base':'Exact full shaped source foundation unchanged','transforms':{'general_vertical_scale_above_z_0_23':BODY_Z,'rear_vertical_scale_above_z_0_23':CORE_Z,'rear_radius_before':4.95,'rear_radius_after':5.35,'rear_center_before':[-3.5,5.75],'rear_center_after':[-5.1,6.15]},'dimensions':{'dock_body_height_before':16.1,'dock_body_height_after':Z0+(16.1-Z0)*BODY_Z,'chimney_crown_before':19.15,'chimney_crown_after':Z0+(19.15-Z0)*BODY_Z,'rear_rim_before':12,'rear_rim_after':Z0+(12-Z0)*CORE_Z},'triangular_sheet_count':plate_count,'left_support_rear_trim_y':.5,'detail_plan':'../high_v01/DETAIL_PLAN.md; amended by triangular plates on all three vessels, full shaped hazard field, three approach arrows and fitted proportion change','objects':changes}
(OUT/'REVISION_MANIFEST.json').write_text(json.dumps(manifest,indent=2)+'\n')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'PROC_High_v02.blend'))
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest()==source_hash
print('High v02 saved. Preserved High v01 hash verified.')
