"""PROC high source from approved FirstLow v01 and generated design v03.

Run in Blender. Rebuilds only high_v01; the approved first low stays unchanged.
"""
import argparse, hashlib, json, math, sys
from pathlib import Path
from collections import defaultdict
import bpy, bmesh
from mathutils import Vector
from mathutils.bvhtree import BVHTree

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'art/refinery/high_v01';OUT.mkdir(exist_ok=True)
SOURCE=ROOT/'art/refinery/first_low_v01/PROC_FirstLow_v01.blend'
DESIGN=ROOT/'art/refinery/generated_design_v03/PROC_DesignDirection_v03.png'
TOP=[(-13.33,-10.63),(-.90,-14.55),(7.48,-7.35),(11.25,6.0),(-8.65,13.76),(-15.74,-.39)]
BOTTOM=[(-13.36,-10.72),(-.94,-14.64),(7.45,-7.44),(11.22,5.91),(-8.65,13.76),(-15.78,-.48)]
args=argparse.ArgumentParser();args.add_argument('--views',default='hero');opt=args.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
scene=bpy.context.scene
for o in list(scene.objects):
 if o.type in {'LIGHT','CAMERA'} or o.name.startswith('QA_'):bpy.data.objects.remove(o,do_unlink=True)
source_body=bpy.data.objects['PROC_FirstLow_ProxyDerived']
source_base=bpy.data.objects['PROC_SourceFitFoundation']
reference=bpy.data.collections.new('REFERENCE_ApprovedFirstLow_Proxy_NONEXPORT');scene.collection.children.link(reference)
for o in list(scene.objects):
 for c in list(o.users_collection):c.objects.unlink(o)
 reference.objects.link(o);o.hide_render=True;o.hide_set(True);o['runtime_candidate']=False
high=bpy.data.collections.new('PROC_High_v01_NONEXPORT_BAKE_SOURCE');scene.collection.children.link(high)
qa=bpy.data.collections.new('QA_Review_NONEXPORT');scene.collection.children.link(qa)
CURRENT='foundation';PARTS=defaultdict(list);bevels=[]

def link(o,collection=high):
 for c in list(o.users_collection):c.objects.unlink(o)
 collection.objects.link(o)
 return o

def tag(o,name,alloc='keep major form; bake fine relief'):
 o.name=name;o['assembly']=CURRENT;o['stage']='High v01 / NONEXPORT';o['allocation']=alloc;o['runtime_candidate']=False
 PARTS[CURRENT].append(o.name);return o

def mat(name,color,metal=0,rough=.62):
 m=bpy.data.materials.new('HP_PREVIEW_'+name);m.diffuse_color=(*color,1);m.use_nodes=True
 b=m.node_tree.nodes.get('Principled BSDF');b.inputs['Base Color'].default_value=(*color,1);b.inputs['Metallic'].default_value=metal;b.inputs['Roughness'].default_value=rough
 return m
paint=mat('GDI_paint_ID',(.40,.235,.058),0,.66)
paint2=mat('GDI_cover_ID',(.48,.295,.075),0,.62)
steel=mat('dark_steel_ID',(.085,.105,.118),.65,.47)
steel2=mat('vessel_steel_ID',(.33,.36,.37),.65,.52)
edge=mat('machined_steel_ID',(.43,.46,.47),.72,.43)
dark=mat('recess_ID',(.018,.024,.029),0,.83)
concrete=mat('concrete_ID',(.32,.32,.295),0,.88)
fixed=mat('fixed_hazard_yellow',(.64,.46,.06),0,.7)
process=mat('process_surface_ID',(.19,.31,.052),.12,.36)
clay=mat('neutral_clay',(.43,.45,.46),0,.67)


def bevel(o,w=.05,segments=3):
 if not w:return o
 m=o.modifiers.new('Designed_chamfer','BEVEL');m.width=w;m.segments=segments;m.limit_method='ANGLE';m.angle_limit=math.radians(27);m.use_clamp_overlap=True;m.harden_normals=True
 n=o.modifiers.new('Broad_planar_normals','WEIGHTED_NORMAL');n.keep_sharp=True;n.weight=40
 bevels.append((o.name,w));return o

def mesh(name,vs,fs,material,bev=0,smooth=False):
 me=bpy.data.meshes.new(name+'_mesh');me.from_pydata(vs,[],fs);me.update()
 bm=bmesh.new();bm.from_mesh(me);bmesh.ops.recalc_face_normals(bm,faces=bm.faces);bm.to_mesh(me);bm.free()
 o=bpy.data.objects.new(name,me);high.objects.link(o);tag(o,name);me.materials.append(material)
 if smooth:
  for p in me.polygons:p.use_smooth=True
  me.set_sharp_from_angle(angle=math.radians(32))
 bevel(o,bev);return o

def box(name,loc,size,material,bev=.05,rotation=None):
 bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=link(bpy.context.object);tag(o,name);o.dimensions=size
 bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 if rotation:o.rotation_euler=rotation
 o.data.materials.append(material);bevel(o,bev);return o

def cylinder(name,loc,r,depth,material,axis=(0,0,1),bev=.025,n=64):
 bpy.ops.mesh.primitive_cylinder_add(vertices=n,radius=r,depth=depth,location=loc)
 o=link(bpy.context.object);tag(o,name);o.rotation_mode='QUATERNION';o.rotation_quaternion=Vector(axis).to_track_quat('Z','Y');o.data.materials.append(material)
 for p in o.data.polygons:p.use_smooth=(len(p.vertices)==4)
 bevel(o,bev);return o

def torus(name,loc,r,minor,material,axis=(0,0,1)):
 bpy.ops.mesh.primitive_torus_add(major_radius=r,minor_radius=minor,major_segments=96,minor_segments=12,location=loc)
 o=link(bpy.context.object);tag(o,name);o.rotation_mode='QUATERNION';o.rotation_quaternion=Vector(axis).to_track_quat('Z','Y');o.data.materials.append(material)
 for p in o.data.polygons:p.use_smooth=True
 return o

def lathe(name,center,profile,material,n=96,bev=.035):
 # closed cross-section profile of (radius,z); fully closed annular solid.
 vs=[(center[0]+r*math.cos(2*math.pi*i/n),center[1]+r*math.sin(2*math.pi*i/n),z) for r,z in profile for i in range(n)]
 fs=[]
 for j in range(len(profile)):
  k=(j+1)%len(profile)
  for i in range(n):a=j*n+i;b=j*n+(i+1)%n;fs.append((a,b,k*n+(i+1)%n,k*n+i))
 return mesh(name,vs,fs,material,bev,True)

def sector(name,c,r0,r1,z0,z1,a0,a1,material,bev=.035,steps=8):
 vs=[]
 for z in (z0,z1):
  for r in (r0,r1):
   for i in range(steps+1):
    a=a0+(a1-a0)*i/steps;vs.append((c[0]+r*math.cos(a),c[1]+r*math.sin(a),z))
 s=steps+1;fs=[]
 for i in range(steps):
  fs += [(i,i+1,s+i+1,s+i),(2*s+i,3*s+i,3*s+i+1,2*s+i+1),(i,2*s+i,2*s+i+1,i+1),(s+i,s+i+1,3*s+i+1,3*s+i)]
 fs += [(0,s,3*s,2*s),(steps,2*s+steps,3*s+steps,s+steps)]
 return mesh(name,vs,fs,material,bev,True)

def extrude_xz(name,poly,y0,y1,material,bev=.06):
 n=len(poly);vs=[(x,y,z) for y in(y0,y1) for x,z in poly]
 fs=[tuple(reversed(range(n))),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
 return mesh(name,vs,fs,material,bev)

def tube(name,pts,r,material,rounding=.5):
 pts=[Vector(p) for p in pts];smooth=[pts[0]]
 for a,b,c in zip(pts,pts[1:],pts[2:]):
  dist=min(rounding,(b-a).length*.35,(c-b).length*.35)
  p=b+(a-b).normalized()*dist;q=b+(c-b).normalized()*dist
  smooth.append(p)
  for i in range(1,9):
   t=i/8;smooth.append((1-t)**2*p+2*t*(1-t)*b+t*t*q)
 smooth.append(pts[-1])
 cu=bpy.data.curves.new(name,'CURVE');cu.dimensions='3D';cu.bevel_depth=r;cu.bevel_resolution=4;cu.resolution_u=1;cu.use_fill_caps=True
 sp=cu.splines.new('POLY');sp.points.add(len(smooth)-1)
 for p,v in zip(sp.points,smooth):p.co=(*v,1)
 o=bpy.data.objects.new(name,cu);high.objects.link(o);tag(o,name);cu.materials.append(material);return o

def beam(name,a,b,width,depth,material,bev=.03):
 a,b=Vector(a),Vector(b);o=box(name,(a+b)/2,(width,depth,(b-a).length),material,bev)
 o.rotation_mode='QUATERNION';o.rotation_quaternion=(b-a).to_track_quat('Z','Y');return o

def bolt(name,loc,axis=(0,0,1),r=.085):
 axis=Vector(axis).normalized();cylinder(name+'_washer',loc,r*1.35,.034,steel,axis,.008,32)
 return cylinder(name+'_hex',Vector(loc)+axis*.045,r,.075,edge,axis,.012,6)

def flange(name,loc,axis,r=.38):
 axis=Vector(axis).normalized();cylinder(name+'_seat',Vector(loc)-axis*.04,r*1.08,.16,dark,axis,.02)
 cylinder(name+'_plate',loc,r,.12,steel2,axis,.025)
 u=axis.cross(Vector((0,0,1)))
 if u.length<.1:u=axis.cross(Vector((0,1,0)))
 u.normalize();v=axis.cross(u)
 for i in range(6):
  a=2*math.pi*i/6;bolt(name+f'_bolt_{i}',Vector(loc)+r*.74*(u*math.cos(a)+v*math.sin(a))+axis*.065,axis,.06)

components=json.loads((OUT/'FIRST_LOW_COMPONENTS.json').read_text())
bodycomps=next(c for c in components if c['object']=='PROC_FirstLow_ProxyDerived')['components']
def extract(index,name,material,zscale=1):
 ids=bodycomps[index]['ids'];remap={i:j for j,i in enumerate(ids)};ps=[p for p in source_body.data.polygons if all(i in remap for i in p.vertices)]
 vs=[tuple(source_body.data.vertices[i].co) for i in ids];vs=[(x,y,.13+(z-.13)*zscale) for x,y,z in vs]
 o=mesh(name,vs,[tuple(remap[i] for i in p.vertices) for p in ps],material)
 # Remove duplicate coordinates inherited from OBJ seams, then close simple holes.
 bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.remove_doubles(bm,verts=bm.verts,dist=.0001)
 boundary=[e for e in bm.edges if e.is_boundary]
 if boundary:bmesh.ops.holes_fill(bm,edges=boundary,sides=0)
 bmesh.ops.recalc_face_normals(bm,faces=bm.faces);bm.to_mesh(o.data);bm.free()
 o['source_component']=index;o['source_mesh']='PROC_FirstLow_ProxyDerived';return o

# Exact source footprint is a retained duplicate, including original underside.
base=source_base.copy();base.data=source_base.data.copy();high.objects.link(base);base.hide_render=False;base.hide_set(False);tag(base,'Foundation_SOURCE_CONTOUR_EXACT');base.data.materials.clear();base.data.materials.append(concrete)
base['source_preserved_exactly']=True
# Broad inset top bevel course does not alter the source outer footprint.
c=Vector((-2.8,-.1));inset=[(c.x+(x-c.x)*.982,c.y+(y-c.y)*.982) for x,y in TOP]
vs=[(x,y,z) for z in(.09,.22) for x,y in inset];n=6
mesh('Foundation_inset_top_course',vs,[tuple(reversed(range(n))),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)],concrete,.04)
# Entry deck has thin geometry ID markings for this review, not shipped textures.
box('Dock_entry_threshold',(-3.4,-8.36,.255),(6.0,4.2,.075),dark,.035)
# Clipped diagonal stripes inside the entry rectangle.
def clip(poly,axis,limit,greater):
 out=[]
 for a,b in zip(poly,poly[1:]+poly[:1]):
  ina=(a[axis]>=limit) if greater else (a[axis]<=limit);inb=(b[axis]>=limit) if greater else (b[axis]<=limit)
  if ina:out.append(a)
  if ina!=inb:
   t=(limit-a[axis])/(b[axis]-a[axis]);out.append(tuple(a[k]+t*(b[k]-a[k]) for k in range(2)))
 return out
for i in range(-5,7):
 x=-3.4+i*1.7;p=[(x,-10.42),(x+.72,-10.42),(x+4.92,-6.3),(x+4.2,-6.3)]
 for axis,limit,g in [(0,-6.34,True),(0,-.46,False),(1,-10.42,True),(1,-6.3,False)]:p=clip(p,axis,limit,g) if p else []
 if len(p)>=3:mesh(f'Dock_hazard_stripe_ID_{i}',[(x,y,.299) for x,y in p],[tuple(range(len(p)))],fixed)
for i in range(2):
 x=-5.7+i*2.1;y=-11.55
 mesh(f'Apron_direction_arrow_ID_{i}',[(x-.6,y-.2,.23),(x+.6,y-.2,.23),(x,y+.6,.23)],[(0,1,2)],steel)

# Preserve the proxy wing footprint; rebuild its folded/notched shell with planar faces.
CURRENT='left_support'
wingxy=[(-11.871596,-9.438),(-9.5329,-10.807211),(-6.78696,-6.11628),(-6.78696,1.07688),(-10.0298,1.07688),(-10.935,.172),(-10.935,-6.11628)]
def wingz(y):return 1.22+3.63*min(1,max(0,(y+10.807211)/4.690931))
vs=[(x,y,.22) for x,y in wingxy]+[(x,y,wingz(y)) for x,y in wingxy]
fs=[tuple(reversed(range(7))),(7,8,9,13),(13,9,10,11,12)]+[(i,(i+1)%7,(i+1)%7+7,i+7) for i in range(7)]
left=mesh('Left_wing_PROXY_CONTOUR_REBUILT_PLANAR',vs,fs,paint,.10)
left['source_component']=6;left['reason_rebuilt']='Inherited non-planar side and access notch produced a folded visible face in close-up; source outline retained.'
# Raised access tray seated on the left wing's vertical outside face.
box('Left_wing_service_recess',(-10.97,-1.131,1.586),(.16,2.13,2.94),dark,.055)
box('Left_wing_service_cover',(-11.06,-1.131,1.586),(.14,2.03,2.84),steel,.045)
for y in[-1.87,-.39]:
 for z in[.40,2.77]:bolt('Left_wing_cover_fastener',(-11.15,y,z),(-1,0,0),.07)
# Sloped right service shoe replaces the proxy wedge that intersected the vessels.
CURRENT='right_support'
poly=[(-1.05,.22),(.2,.22),(.2,3.9),(-1.05,3.9)]
vs=[(-1.05,-10.6,.22),(-.15,-10.6,.22),(.05,-6.25,.22),(-1.05,-6.25,.22),(-1.05,-10.6,.72),(-.15,-10.6,.72),(.05,-6.25,5.3),(-1.05,-6.25,5.3)]
mesh('Right_service_shoe',vs,[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],paint2,.10)
# Panel on the sloped upper plane, fitted from its four source corner positions.
normal=Vector((0,-4.58,4.35)).normalized();vs2=[]
for dz in(0,.09):
 for x,t in[(-.86,.12),(-.30,.12),(-.12,.89),(-.86,.89)]:vs2.append(tuple(Vector((x,-10.6+4.35*t,.72+4.58*t))+normal*dz))
mesh('Right_service_shoe_access_lid',vs2,[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],paint,.04)

# Tall industrial arch. Its coordinates derive from the original proxy jambs.
CURRENT='dock_arch';CX=-3.4;H=16.1;SPRING=10.9;RX=2.38;RZ=3.45;HW=4.12;YF=-6.13;YB=-.25
curve=[(CX+RX*math.cos(math.pi-i*math.pi/40),SPRING+RZ*math.sin(math.pi-i*math.pi/40)) for i in range(41)]
poly=[(CX-RX,.22)]+curve+[(CX+RX,.22),(CX+HW,.22),(CX+HW,H),(CX-HW,H),(CX-HW,.22)]
arch=extrude_xz('Dock_arch_proxy_footprint_rebuilt_taller',poly,YF,YB,paint,.145)
arch['source_components']='8,9,17,18';arch['opening_width']=2*RX;arch['opening_apex']=SPRING+RZ
# Real front armour courses and curved recess trim.
for side in(-1,1):
 xa=CX+side*(RX+.15);xb=CX+side*(HW-.10);lo,hi=sorted([xa,xb])
 for j in range(4):
  z0=.38+j*2.58;z1=z0+2.49
  panel=box(f'Dock_jamb_panel_{side}_{j}',((lo+hi)/2,YF-.105,(z0+z1)/2),(hi-lo,.22,z1-z0),paint2,.07)
  for x in(lo+.18,hi-.18):
   for z in(z0+.22,z1-.22):bolt(f'Jamb_bolt_{side}_{j}',(x,YF-.242,z),(0,-1,0),.063)
# Two broad steel eyebrow plates avoid the masonry-like radial seam pattern.
for side in(-1,1):
 angles=[math.pi-i*math.pi/40 for i in range(21)] if side==-1 else [math.pi/2-i*math.pi/40 for i in range(21)]
 inner=[(CX+(RX+.14)*math.cos(a),SPRING+(RZ+.14)*math.sin(a)) for a in angles]
 if side==-1:
  pts=[(CX-HW+.10,10.73),(CX-RX-.14,10.73)]+inner+[(CX-.035,H-.10),(CX-HW+.10,H-.10)]
 else:
  pts=inner+[(CX+RX+.14,10.73),(CX+HW-.10,10.73),(CX+HW-.10,H-.10),(CX+.035,H-.10)]
 extrude_xz(f'Dock_header_broad_steel_plate_{side}',pts,YF-.22,YF-.035,paint2,.045)
 for dx,z in [(side*3.60,11.2),(side*3.60,15.55),(side*.45,15.55)]:bolt('Dock_header_plate_fastener',(CX+dx,YF-.25,z),(0,-1,0),.07)
trimpts=[(CX-RX,YF-.24,.3)]+[(x,YF-.24,z) for x,z in curve]+[(CX+RX,YF-.24,.3)]
tube('Dock_arch_continuous_inner_liner',trimpts,.115,steel,rounding=.18)
# Dark rear machinery bulkhead is behind the open bay and feeds the nozzle.
box('Dock_recess_back_wall',(CX,-.42,7.05),(4.65,.32,13.55),dark,.08)
box('Dock_closed_rear_casing',(CX,-.12,8.12),(8.02,.36,15.78),paint,.10)
for x in(CX-1.73,CX+1.73):box('Dock_recess_inner_rail',(x,-.68,6.8),(.16,.2,12.8),steel,.035)
# Roof service grille in its own recessed framed tray.
def roof_grille(name,cx,cy,z,sx,sy):
 box(name+'_seat',(cx,cy,z+.07),(sx+.25,sy+.25,.14),steel,.045)
 box(name+'_dark_tray',(cx,cy,z+.15),(sx,sy,.075),dark,.02)
 for dx,dy,wx,wy in[(-sx/2,0,.12,sy),(sx/2,0,.12,sy),(0,-sy/2,sx,.12),(0,sy/2,sx,.12)]:box(name+'_frame',(cx+dx,cy+dy,z+.25),(wx,wy,.20),edge,.028)
 for j in range(9):box(name+f'_slat_{j}',(cx-sx*.40+j*sx*.1,cy,z+.235),(.08,sy*.82,.105),steel2,.025)
roof_grille('Dock_roof_grille',CX-1.2,-4.2,H,2.2,1.5)
fx,fy=CX+1.95,-4.1
cylinder('Dock_roof_fan_dark_well',(fx,fy,H+.08),.82,.16,dark)
torus('Dock_roof_fan_seat',(fx,fy,H+.22),.85,.09,edge)
cylinder('Dock_roof_fan_hub',(fx,fy,H+.18),.20,.17,steel2)
for i in range(8):
 a=2*math.pi*i/8
 o=box(f'Dock_roof_fan_blade_{i}',(fx+.47*math.cos(a),fy+.47*math.sin(a),H+.17),(.52,.22,.07),steel2,.025);o.rotation_euler.z=a+.3
for i in range(4):
 a=math.pi/4+i*math.pi/2;bolt('Dock_roof_fan_fastener',(fx+.83*math.cos(a),fy+.83*math.sin(a),H+.29),r=.065)
# Side shoulders and seated supporting brackets.
for side in(-1,1):
 x=CX+side*(HW+.7);z=12.8
 box(f'Dock_shoulder_{side}',(x,-2.7,z),(2.0,3.4,1.0),paint2,.14)
 roof_grille(f'Shoulder_vent_{side}',x,-2.7,z+.5,1.15,1.75)
 beam(f'Shoulder_bracket_{side}',(CX+side*HW,-1.25,10.6),(x,-1.25,12.3),.34,.44,steel,.06)
 # Vertical side door sits on the actual x-facing arch surface.
 sx=CX+side*(HW+.06)
 box(f'Dock_side_recess_{side}',(sx,-2.9,7.2),(.14,2.0,3.4),dark,.045)
 box(f'Dock_side_access_cover_{side}',(sx+side*.09,-2.9,7.2),(.12,1.75,3.14),paint2,.055)
 for yy in(-3.55,-2.25):
  for zz in(5.85,8.55):bolt(f'Dock_side_bolt_{side}',(sx+side*.18,yy,zz),(side,0,0),.07)
# Rear housing vertical pipe bank / control covers.
for x,z,sx,sz in[(CX-2.7,7.5,1.0,5.4),(CX+2.7,7.5,1.0,5.4)]:
 box('Dock_rear_service_box',(x,.03,z),(sx,.48,sz),paint2,.09)
 for zz in(z-sz/2+.3,z+sz/2-.3):bolt('Dock_rear_box_bolt',(x,.3,zz),(0,1,0),.07)

# Tapered panelled chimney on a fitted seat over the retained left support.
CURRENT='chimney';CHX=-9.95;CHY=-8.45;ZFOOT=4.058382511;ZTOP=19.0
# Sample the actual retained support top for each socket corner.
bpy.context.view_layer.update();dg=bpy.context.evaluated_depsgraph_get();tree=BVHTree.FromObject(left,dg)
seatxy=[(CHX-.90,CHY-1.15),(CHX+.90,CHY-1.15),(CHX+.90,CHY+1.15),(CHX-.90,CHY+1.15)]
bottom=[]
for x,y in seatxy:
 hit=tree.ray_cast(Vector((x,y,12)),Vector((0,0,-1)),20)[0]
 bottom.append((x,y,hit.z-.018 if hit else .22))
ZFOOT=max(ZFOOT,max(p[2] for p in bottom)+.14)
seat=mesh('Chimney_socket_fitted_to_proxy_support',bottom+[(x,y,ZFOOT) for x,y in seatxy],[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],steel,.055)
def chimrect(z):
 t=(z-ZFOOT)/(ZTOP-ZFOOT);hx=.75-.18*t;hy=1.13-.51*t;cy=CHY-.28*t
 cut=.12
 return [(CHX-hx+cut,cy-hy),(CHX+hx-cut,cy-hy),(CHX+hx,cy-hy+cut),(CHX+hx,cy+hy-cut),(CHX+hx-cut,cy+hy),(CHX-hx+cut,cy+hy),(CHX-hx,cy+hy-cut),(CHX-hx,cy-hy+cut)]
for i in range(9):
 z0=ZFOOT+i*(ZTOP-ZFOOT)/9+.026;z1=ZFOOT+(i+1)*(ZTOP-ZFOOT)/9-.026
 vs=[(x,y,z) for z in(z0,z1) for x,y in chimrect(z)]
 mesh(f'Chimney_tapered_panel_course_{i}',vs,[tuple(reversed(range(8))),tuple(range(8,16))]+[(j,(j+1)%8,(j+1)%8+8,j+8) for j in range(8)],steel2,.045)
 if i in(1,3,5,7):
  for dz in(0,.16):
   pass
  r0=chimrect(z1-.16);r1=chimrect(z1)
  vs=[(CHX+(x-CHX)*1.026,CHY+(y-CHY)*1.026,z) for z,points in[(z1-.16,r0),(z1,r1)] for x,y in points]
  mesh(f'Chimney_fixed_yellow_safety_band_{i}',vs,[tuple(reversed(range(8))),tuple(range(8,16))]+[(j,(j+1)%8,(j+1)%8+8,j+8) for j in range(8)],fixed,.012)
 # two seam fasteners on exposed front panel per course
 for dx in(-.35,.35):bolt(f'Chimney_panel_rivet_{i}',(CHX+dx,chimrect((z0+z1)/2)[0][1]-.025,z0+.26),(0,-1,0),.05)
# Chamfered cap, no extra chimney stack.
p=chimrect(ZTOP);vs=[(CHX+(x-CHX)*1.08,CHY+(y-CHY)*1.08,z) for z in(ZTOP-.08,ZTOP+.15) for x,y in p]
mesh('Chimney_crown_cap',vs,[tuple(reversed(range(8))),tuple(range(8,16))]+[(j,(j+1)%8,(j+1)%8+8,j+8) for j in range(8)],edge,.035)

# All vessels share physical construction, with a large core and two open wells.
def vessel(name,c,r,h,panels,main=False):
 global CURRENT
 CURRENT=name
 wall=lathe(name+'_continuous_sealed_wall',c,[(r-.16,.23),(r,.5),(r,h-.55),(r+.075,h-.35),(r+.075,h),(r-.34,h),(r-.38,.45)],steel,n=128,bev=.055)
 for z in(.48,1.0,h-.8):
  lathe(name+'_rolled_band_'+str(z),c,[(r-.015,z-.075),(r+.075,z-.075),(r+.075,z+.075),(r-.015,z+.075)],edge,n=128,bev=.02)
 for i in range(panels):
  a=2*math.pi*i/panels;da=2*math.pi/panels
  sector(f'{name}_curved_wall_panel_{i}',c,r-.025,r+.09,1.08,h-.86,a+.018,a+da-.018,steel2 if i%4 else steel,.030,8)
  # Exact radial seats: ribs rise off the cylindrical wall, foot seated on slab.
  mx,my=c[0]+(r+.13)*math.cos(a),c[1]+(r+.13)*math.sin(a)
  rib=box(f'{name}_vertical_seam_rib_{i}',(mx,my,(h-.8+.45)/2),(.26,.17,h-1.25),steel,.04);rib.rotation_euler.z=a-math.pi/2
  foot=box(f'{name}_seated_rib_foot_{i}',(mx,my,.38),(.43,.36,.30),steel2,.045);foot.rotation_euler.z=a-math.pi/2
  axis=(math.cos(a),math.sin(a),0)
  for z in(1.35,h-1.12):bolt(f'{name}_panel_fastener_{i}_{z}',(c[0]+(r+.24)*axis[0],c[1]+(r+.24)*axis[1],z),axis,.075)
 # Segmented thick rim panels and protective clips are actual high source.
 for i in range(panels):
  a=2*math.pi*i/panels;da=2*math.pi/panels
  sector(f'{name}_rim_panel_{i}',c,r-.41,r+.10,h-.50,h+.12,a+.016,a+da-.016,edge,.045,8)
  x,y=c[0]+(r-.05)*math.cos(a),c[1]+(r-.05)*math.sin(a)
  clip=box(f'{name}_rim_clamp_{i}',(x,y,h+.22),(.35,.55,.52),steel,.055);clip.rotation_euler.z=a-math.pi/2
  bolt(f'{name}_rim_clamp_bolt_{i}',(x,y,h+.50),r=.075)
 # Visible cavity floor below rim; no hidden internal machines.
 cylinder(name+'_inner_well_floor',(c[0],c[1],h-1.02),r-.40,.16,process if not main else dark,bev=.04,n=128)
 torus(name+'_inner_well_seat',(c[0],c[1],h-.72),r-.42,.08,steel2)
 if main:
  for rr in(1.0,2.45,3.90,r-.55):torus(name+'_core_concentric_grille',(c[0],c[1],h-.77),rr,.047,edge)
  for j in range(48):
   a=2*math.pi*j/48
   beam(name+f'_core_radial_grille_{j}',(c[0]+1.0*math.cos(a),c[1]+1.0*math.sin(a),h-.76),(c[0]+(r-.56)*math.cos(a),c[1]+(r-.56)*math.sin(a),h-.76),.055,.08,steel2,.014)
  cylinder(name+'_core_hub',(c[0],c[1],h-.54),.77,.60,steel,bev=.045)
  cylinder(name+'_core_process_socket',(c[0],c[1],h-.19),.37,.14,process,bev=.025)
  for j in range(3):
   a=math.pi/2+2*math.pi*j/3
   aa=(c[0]+.7*math.cos(a),c[1]+.7*math.sin(a),h-.44);bb=(c[0]+(r-.43)*math.cos(a),c[1]+(r-.43)*math.sin(a),h-.28)
   beam(name+f'_three_spoke_processing_strut_{j}',aa,bb,.37,.30,steel,.07)
   for t in(.25,.7):
    v=Vector(aa).lerp(Vector(bb),t);cylinder(name+'_strut_collar',v,.255,.19,edge,Vector(bb)-Vector(aa),.025)
 else:
  for j in range(12):
   a=2*math.pi*j/12
   o=box(name+f'_inner_process_node_{j}',(c[0]+(r-.53)*math.cos(a),c[1]+(r-.53)*math.sin(a),h-.50),(.14,.17,.32),process,.025);o.rotation_euler.z=a
 return wall
core=vessel('Rear_processing_core',(-3.5,5.75),4.95,12.0,16,True)
mid=vessel('Right_middle_vessel',(4.90,-1.92),2.92,9.5,12)
front=vessel('Right_front_vessel',(2.55,-8.24),2.13,6.15,10)

# Rear-core service door: tangential cover plate and fitted radial seat.
CURRENT='core_access';cx,cy=-3.5,5.75;r=4.95;a=math.pi/2
# Rear visible access cover on the core's north-facing surface.
box('Core_access_tangent_seat',(cx,cy+r+.08,4.4),(1.6,.22,3.5),dark,.065)
box('Core_access_tangent_cover',(cx,cy+r+.23,4.4),(1.38,.18,3.28),paint,.075)
for dx in(-.5,.5):
 for z in(3.0,5.8):bolt('Core_access_bolt',(cx+dx,cy+r+.35,z),(0,1,0),.075)
box('Core_access_recess_handle',(cx+.39,cy+r+.36,4.5),(.12,.11,.65),steel,.02)
# Attached left/rear utility riser returns into the core through matching radial sockets.
CURRENT='pipework'
a=math.radians(150);rad=Vector((math.cos(a),math.sin(a),0));wall=Vector((cx,cy,0))+rad*r;outer=Vector((cx,cy,0))+rad*(r+.55)
pts=[(wall.x,wall.y,2.0),(outer.x,outer.y,2.0),(outer.x,outer.y,7.6),(wall.x,wall.y,7.6)]
tube('Core_rear_external_riser',pts,.23,steel,.45)
for z in(2.0,7.6):flange('Core_rear_riser_socket',(wall.x,wall.y,z),rad,.42)
for z in(3.1,6.5):
 cylinder('Core_rear_riser_sleeve',(outer.x,outer.y,z),.32,.3,edge,bev=.03)
 beam('Core_rear_riser_saddle',(wall.x,wall.y,z),(outer.x,outer.y,z),.20,.20,steel,.035)
# Tank-to-tank routes, with all ends embedded only in deliberate flanged seats.
routes=[('Core_to_middle',[(1.45,5.75,5.0),(2.45,5.75,5.0),(2.45,1.72,5.0),(4.9,1.72,5.0),(4.9,1.0,5.0)],.23),('Middle_to_front',[(4.9,-4.84,4.0),(4.9,-5.52,4.0),(4.9,-5.52,2.7),(2.55,-5.52,2.7),(2.55,-6.11,2.7)],.25),('Dock_to_middle',[(.72,-3.2,6.3),(1.46,-3.2,6.3),(1.46,-1.92,6.3),(1.98,-1.92,6.3)],.22),('Dock_to_core',[(CX,-.2,7.4),(CX,.82,7.4)],.32)]
for name,pts,radius in routes:
 tube(name+'_rounded_pipe',pts,radius,steel,.45)
 for i in(0,-1):
  v=Vector(pts[i]);n=(Vector(pts[1])-v).normalized() if i==0 else (Vector(pts[-2])-v).normalized()
  flange(name+('_start' if i==0 else '_end'),v,n,radius*1.85)
# Low front process pipe along the middle/front vessel gap.

CURRENT='docking_nozzle'
box('Nozzle_floor_seated_plinth',(CX,-3.95,.44),(2.75,2.70,.42),steel,.095)
for dx in(-.98,.98):
 box('Nozzle_bearing_pedestal',(CX+dx,-3.8,1.15),(.30,1.25,1.12),steel2,.06)
 for yy in(-4.38,-3.25):bolt('Nozzle_plinth_bolt',(CX+dx,yy,.70),r=.10)
axis=(0,-1,0)
cylinder('Nozzle_main_body',(CX,-3.75,2.30),.88,1.65,steel,axis,.10,96)
# Ringed open bore, actual inner wall and dark recessed backing.
nozzle=lathe('Nozzle_open_bore_Z_template',(0,0),[(.82,-.25),(.94,-.12),(.94,.12),(.80,.25),(.59,.25),(.59,-.25)],edge,n=96,bev=.045)
# lathe Z axis reoriented into forward (-Y) nozzle axis.
nozzle.location=(CX,-4.73,2.3);nozzle.rotation_euler.x=math.pi/2
cylinder('Nozzle_bore_dark_depth',(CX,-4.45,2.3),.60,.035,dark,axis,.01)
for i in range(8):
 a=2*math.pi*i/8;bolt(f'Nozzle_flange_hex_{i}',(CX+.79*math.cos(a),-4.98,2.3+.79*math.sin(a)),axis,.09)
tube('Nozzle_back_feeder',[(CX,-.56,3.8),(CX,-2.20,3.8),(CX,-2.9,3.05),(CX,-2.9,2.3)],.40,steel,.60)
flange('Nozzle_back_bulkhead_socket',(CX,-.68,3.8),(0,-1,0),.65)
for dx in(-1.3,1.3):tube('Nozzle_floor_hydraulic',[(CX+dx,-.60,.9),(CX+dx,-4.1,.9),(CX+dx*.65,-4.1,1.9)],.105,steel2,.28)

# Freeze all curve sources to meshes for physical-source QA and later bakes.
bpy.ops.object.select_all(action='DESELECT')
for o in list(high.objects):
 if o.type=='CURVE':
  o.select_set(True);bpy.context.view_layer.objects.active=o;bpy.ops.object.convert(target='MESH');o.select_set(False)
  bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.remove_doubles(bm,verts=bm.verts,dist=.00001);bmesh.ops.recalc_face_normals(bm,faces=bm.faces);bm.to_mesh(o.data);bm.free()
# Collapse zero-area remnants of the inherited proxy's rounded toe after evaluation.
bpy.context.view_layer.objects.active=left
for modifier in list(left.modifiers):bpy.ops.object.modifier_apply(modifier=modifier.name)
bm=bmesh.new();bm.from_mesh(left.data);bmesh.ops.remove_doubles(bm,verts=bm.verts,dist=.00001);bmesh.ops.dissolve_degenerate(bm,edges=bm.edges,dist=.00001);bmesh.ops.recalc_face_normals(bm,faces=bm.faces);bm.to_mesh(left.data);bm.free()
# Retain approved image within the .blend as an explicit reference datablock.
im=bpy.data.images.load(str(DESIGN),check_existing=True);im.pack()
scene['approved_design']='PROC DesignDirection v03';scene['source_first_low_sha256']=hashlib.sha256(SOURCE.read_bytes()).hexdigest();scene['approved_design_sha256']=hashlib.sha256(DESIGN.read_bytes()).hexdigest()
scene['status']='High v01 actual geometry, pending design-review approval; provisional ID materials only'

# Deterministic studio setup; review colours are untextured IDs.
scene.render.engine='CYCLES';scene.cycles.samples=32;scene.cycles.use_denoising=True
scene.render.resolution_x=1400;scene.render.resolution_y=1100;scene.render.resolution_percentage=100;scene.render.image_settings.file_format='PNG'
scene.view_settings.view_transform='AgX';scene.view_settings.look='AgX - Medium High Contrast';scene.view_settings.exposure=.5
world=bpy.data.worlds.new('High_review_world');world.use_nodes=True;world.node_tree.nodes['Background'].inputs['Color'].default_value=(.42,.47,.52,1);world.node_tree.nodes['Background'].inputs['Strength'].default_value=.38;scene.world=world
bpy.ops.mesh.primitive_plane_add(size=250,location=(0,0,-.515));ground=link(bpy.context.object,qa);ground.name='QA_Ground';ground.data.materials.append(mat('studio_ground',(.11,.125,.14),0,.85))
def aim(o,t):o.rotation_euler=(Vector(t)-o.location).to_track_quat('-Z','Y').to_euler()
for name,loc,power,size in[('KEY',(-20,-30,42),16000,20),('FILL',(25,-12,24),8500,18),('RIM',(-12,26,35),18000,15)]:
 bpy.ops.object.light_add(type='AREA',location=loc);l=link(bpy.context.object,qa);l.name=name;l.data.energy=power;l.data.shape='DISK';l.data.size=size;aim(l,(-3,0,6))
bpy.ops.object.camera_add();cam=link(bpy.context.object,qa);cam.name='QA_Camera';cam.data.type='ORTHO';cam.data.clip_end=500;scene.camera=cam
views={
 'hero':((22,-50,32),(-2.8,-.2,7.8),43),
 'concept_match':((32,-48,35),(-2.8,-.2,7.8),43),
 'rear':((-36,44,33),(-2.8,-.2,7.8),43),
 'side':((65,-.2,9.0),(-2.8,-.2,9.0),40),
 'front':((-3.4,-60,10),(-3.4,0,8.5),38),
 'top':((-2.3,-.4,75),(-2.3,-.4,0),40),
 'dock_detail':((12,-30,18),(-3.3,-3.1,8.0),23),
 'core_detail':((16,27,27),(-2.6,5.4,8.2),24),
 'pipe_detail':((26,-16,16),(3.0,-3.0,4.8),20),
 'chimney_seat_detail':((-28,-26,15),(-10,-7,2.8),13),
}
cam.location=views['hero'][0];aim(cam,views['hero'][1]);cam.data.ortho_scale=views['hero'][2]
# Per-source manifest and numeric reports are evaluated by the separate verifier.
manifest={'source':str(SOURCE),'source_sha256':scene['source_first_low_sha256'],'design':str(DESIGN),'design_sha256':scene['approved_design_sha256'],'assemblies':dict(PARTS),'bevel_nominal_widths':bevels,'parameters':{'arch_height':H,'arch_opening_width':2*RX,'arch_opening_apex':SPRING+RZ,'chimney_top':ZTOP+.15,'core_rim':12,'middle_vessel_rim':9.5,'front_vessel_rim':6.15},'views':{k:{'location':loc,'target':target,'ortho_scale':scale} for k,(loc,target,scale) in views.items()},'intended_contacts':'Flange plates, cladding, fasteners and buried support stock overlap their receiving seats; no moving clearance claim.'}
(OUT/'BUILD_MANIFEST.json').write_text(json.dumps(manifest,indent=2)+'\n')
(OUT/'REVIEW_CAMERAS.json').write_text(json.dumps(manifest['views'],indent=2)+'\n')
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'PROC_High_v01.blend'))
selected=list(views) if opt.views=='all' else opt.views.split(',')
for name in selected:
 loc,target,scale=views[name];cam.location=loc;aim(cam,target);cam.data.ortho_scale=scale
 scene.render.filepath=str(OUT/f'PROC_High_v01_{name}.png');bpy.ops.render.render(write_still=True)
print('PROC_HIGH_BUILT',len(high.objects),dict(manifest['parameters']))
