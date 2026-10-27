"""Derive the review-stage PROC low from approved High v04, preserving sources.

No UVs, bakes or production textures. Explicit allocation, fixed triangles and
regular radial topology replace the high's dense construction where appropriate.
"""
import bpy, bmesh, math, json, hashlib
from pathlib import Path
from collections import Counter
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'art/refinery/final_low_v01'; OUT.mkdir(exist_ok=True)
SOURCE=ROOT/'art/refinery/high_v04/PROC_High_v04.blend'
SHA='f457ee9dbe5ae29a2189e17a0d63300fc340d3d71d5d1ac72059f1e560d2fb5c'
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest()==SHA
assert (ROOT/'art/refinery/generated_finish_v01/APPROVAL.md').exists()
bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
scene=bpy.context.scene
high=bpy.data.collections['PROC_High_v04_NONEXPORT_BAKE_SOURCE']
original_matrices={o.name:o.matrix_world.copy() for o in high.objects}
# Retain the archive in-file, but avoid reevaluating its 454k triangles after
# every low modifier. Restore its layer when explicitly rendering/checking high.
bpy.context.view_layer.layer_collection.children[high.name].exclude=True
low=bpy.data.collections.new('PROC_FinalLow_v01_RUNTIME'); scene.collection.children.link(low)
markers=bpy.data.collections.new('QA_PaintLayout_NONEXPORT');scene.collection.children.link(markers)
records=[]; dropped=[]

def replace(o,vs,fs):
    old=o.data; me=bpy.data.meshes.new(o.name+'_regular_low');me.from_pydata(vs,[],fs);me.update()
    for m in old.materials:me.materials.append(m)
    o.data=me
    if old.users==0:bpy.data.meshes.remove(old)

def ring_sample(o,old_n,new_n):
    """Closed annular profile; preserve each original radius and height."""
    p=o.data.vertices; rows=len(p)//old_n; assert rows*old_n==len(p)
    vs=[]
    for j in range(rows):
        ring=[p[j*old_n+i].co for i in range(old_n)]
        c=sum(ring,Vector())/old_n;r=math.hypot(ring[0].x-c.x,ring[0].y-c.y)
        start=math.atan2(ring[0].y-c.y,ring[0].x-c.x)
        for i in range(new_n):
            a=start+2*math.pi*i/new_n;vs.append((c.x+r*math.cos(a),c.y+r*math.sin(a),c.z))
    fs=[(j*new_n+i,j*new_n+(i+1)%new_n,((j+1)%rows)*new_n+(i+1)%new_n,((j+1)%rows)*new_n+i) for j in range(rows) for i in range(new_n)]
    replace(o,vs,fs)

def sector_sample(o):
    # High sectors have four rows of nine vertices, eight angular intervals.
    assert len(o.data.vertices)==36
    vs=[tuple(o.data.vertices[j*9+i].co) for j in range(4) for i in [0,4,8]]
    s=3;fs=[]
    for i in range(2):fs += [(i,i+1,s+i+1,s+i),(2*s+i,3*s+i,3*s+i+1,2*s+i+1),(i,2*s+i,2*s+i+1,i+1),(s+i,s+i+1,3*s+i+1,3*s+i)]
    fs += [(0,s,3*s,2*s),(2,5,11,8)];replace(o,vs,fs)

def cylinder_sample(o,n):
    vs0=[v.co for v in o.data.vertices];lo=min(v.z for v in vs0);hi=max(v.z for v in vs0)
    cx=sum(v.x for v in vs0)/len(vs0);cy=sum(v.y for v in vs0)/len(vs0)
    r=max(math.hypot(v.x-cx,v.y-cy) for v in vs0)
    vs=[(cx+r*math.cos(2*math.pi*i/n),cy+r*math.sin(2*math.pi*i/n),z) for z in (lo,hi) for i in range(n)]
    fs=[tuple(reversed(range(n))),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    replace(o,vs,fs)

def torus_sample(o,n=20):
    pts=[v.co for v in o.data.vertices];rs=[math.hypot(p.x,p.y) for p in pts]
    major=(max(rs)+min(rs))*.5;minor=(max(rs)-min(rs))*.5
    vs=[]
    for i in range(n):
        a=2*math.pi*i/n
        for j in range(4):
            b=2*math.pi*j/4;rr=major+minor*math.cos(b);vs.append((rr*math.cos(a),rr*math.sin(a),minor*math.sin(b)))
    fs=[(i*4+j,((i+1)%n)*4+j,((i+1)%n)*4+(j+1)%4,i*4+(j+1)%4) for i in range(n) for j in range(4)]
    replace(o,vs,fs)

def cladding_shell(o,name):
    """Closed faceted skin, matching approved sheet planes and triangle layout.

    Fine inset gaps/thickness and fasteners transfer from the retained high.
    Outer faces remain real form-defining facets rather than a smooth cylinder.
    """
    if name.startswith('Rear'):c,r,h,n,tiers=(-3.5,5.75),4.95,12,16,3
    elif 'middle' in name:c,r,h,n,tiers=(4.9,-1.92),2.92,9.5,12,3
    else:c,r,h,n,tiers=(2.55,-8.24),2.13,6.15,10,2
    outer=(r+.14)/math.cos(math.pi/n);inner=r+.04
    zs=[1.08+(h-.86-1.08)*j/tiers for j in range(tiers+1)]
    vs=[(c[0]+outer*math.cos(2*math.pi*i/n),c[1]+outer*math.sin(2*math.pi*i/n),z) for z in zs for i in range(n)]
    vs += [(c[0]+inner*math.cos(2*math.pi*i/n),c[1]+inner*math.sin(2*math.pi*i/n),z) for z in (zs[0],zs[-1]) for i in range(n)]
    fs=[];mats=[]
    for j in range(tiers):
        for i in range(n):
            a=j*n+i;b=j*n+(i+1)%n;d=(j+1)*n+i;cc=(j+1)*n+(i+1)%n
            fs += [(a,b,cc),(a,cc,d)] if (i+j)%2==0 else [(a,b,d),(b,cc,d)]
            mats += [0 if (i+j+k)%5 else 1 for k in range(2)]
    ib=(tiers+1)*n;it=ib+n;ot=tiers*n
    for i in range(n):
        q=(i+1)%n
        fs += [(ib+i,it+i,it+q,ib+q),(i,ib+i,ib+q,q),(ot+i,ot+q,it+q,it+i)];mats += [1,1,1]
    replace(o,vs,fs);o.data.materials.clear()
    o.data.materials.append(bpy.data.materials['HP_PREVIEW_vessel_steel_ID']);o.data.materials.append(bpy.data.materials['HP_PREVIEW_dark_steel_ID'])
    for f,mi in zip(o.data.polygons,mats):f.material_index=mi
    group=name.split('_triangle_sheet_')[0]
    o['high_sources']=json.dumps([s.name for s in high.objects if s.name.startswith(group+'_triangle_sheet_')])

def is_cylinder(o):
    """Geometric check: two axial rings of equal XY radius. Never infer by counts."""
    vs=[v.co for v in o.data.vertices]
    if len(vs)<64 or len({round(v.z,5) for v in vs})!=2:return False
    cx=sum(v.x for v in vs)/len(vs);cy=sum(v.y for v in vs)/len(vs)
    rs=[math.hypot(v.x-cx,v.y-cy) for v in vs]
    return max(rs)-min(rs)<1e-4

def arch_liner(o):
    # Regular 24-interval ellipse and eight-sided tube avoid collapse-induced
    # ripples in the prominent inner arch silhouette. Source coordinates/pose.
    cs=[Vector((-3.4-2.38,-6.37,.3))]
    cs += [Vector((-3.4+2.38*math.cos(math.pi-j*math.pi/24),-6.37,10.9+3.45*math.sin(math.pi-j*math.pi/24))) for j in range(25)]
    cs += [Vector((-3.4+2.38,-6.37,.3))];vs=[]
    for i,c in enumerate(cs):
        t=(cs[min(i+1,len(cs)-1)]-cs[max(0,i-1)]).normalized();u=Vector((0,1,0));v=t.cross(u).normalized()
        for k in range(8):
            a=2*math.pi*k/8;vs.append(tuple(c+.115*(u*math.cos(a)+v*math.sin(a))))
    n=len(cs);fs=[tuple(reversed(range(8))),tuple(range((n-1)*8,n*8))]
    fs += [(j*8+i,j*8+(i+1)%8,(j+1)*8+(i+1)%8,(j+1)*8+i) for j in range(n-1) for i in range(8)]
    replace(o,vs,fs)

def apply(o,mod):
    bpy.context.view_layer.objects.active=o;bpy.ops.object.modifier_apply(modifier=mod.name)

def reduce(o,target):
    d=o.modifiers.new('Selected_pipe_surface_reduction','DECIMATE');d.ratio=min(1,target/max(1,sum(len(p.vertices)-2 for p in o.data.polygons)));d.use_collapse_triangulate=True;apply(o,d)

def bevel(o,width,segments=1):
    b=o.modifiers.new('Retained_form_chamfer','BEVEL');b.width=width;b.segments=segments;b.limit_method='ANGLE';b.angle_limit=math.radians(27);b.use_clamp_overlap=True;b.harden_normals=True;apply(o,b)

def allocation(n):
    if 'hazard_stripe_ID' in n or 'direction_arrow_ID' in n:return 'marker'
    if any(x in n.lower() for x in ['washer','_hex','_bolt','fastener','_rivet']):return 'bake: small hardware to named assembly receiver'
    if any(x in n for x in ['_rolled_band_','_vertical_seam_rib_','_inner_well_seat','_core_concentric_grille','_core_radial_grille_','_strut_collar','_inner_process_node_']):return 'bake: fine bands, ribs, grille or well details to wall/well receiver'
    if '_slat_' in n or '_fan_blade_' in n:return 'bake: fine roof vent relief to dark tray/well'
    if '_triangle_sheet_' in n and not n.endswith('_triangle_sheet_0_0_0'):return 'bake: sheet seams and corner roundness to the closed faceted cladding shell'
    return 'geometry'

for src in list(high.objects):
    if src.type!='MESH':continue
    n=src.name;role=allocation(n)
    if role.startswith('bake:'):
        dropped.append({'source':n,'assembly':src.get('assembly'),'role':role});continue
    o=src.copy();o.data=src.data.copy();o.modifiers.clear();o.name='LOW_'+n
    (markers if role=='marker' else low).objects.link(o);o.hide_render=False;o.hide_set(False)
    o.matrix_world=original_matrices[n]
    o['high_source']=n;o['stage']='FinalLow v01 / mesh review';o['runtime_candidate']=role!='marker'
    o['allocation']='paint layout guide; texture after low approval' if role=='marker' else 'runtime geometry'
    for uv in list(o.data.uv_layers):o.data.uv_layers.remove(uv)
    method='retained raw topology';width=None
    old_bev=next((m for m in src.modifiers if m.type=='BEVEL'),None)
    # Regular ring topology, matched radii/profile and transforms.
    if n.startswith('Foundation_'):
        method='exact approved rounded slab contour and tier; fine top edge bevel bakes'
    elif '_triangle_sheet_' in n:
        cladding_shell(o,n);o.name='LOW_'+n.split('_triangle_sheet_')[0]+'_faceted_cladding'
        method='closed faceted shell with approved alternating triangular layout; shallow seams bake'
    elif '_continuous_sealed_wall' in n:
        ring_sample(o,128,32 if n.startswith('Rear') else (24 if 'middle' in n else 20));method='regular radial wall profile'
    elif '_rim_panel_' in n:
        sector_sample(o);method='two curved intervals per rim panel; fine edge roundness bakes'
    elif n=='Nozzle_open_bore_Z_template':
        ring_sample(o,96,16);method='16-sided open bore, full shaped profile'
    elif n=='Dock_roof_fan_seat':
        torus_sample(o);method='20 x 4 fan rim'
    elif n=='Dock_arch_continuous_inner_liner':
        arch_liner(o);method='regular elliptical arch liner, 24 curve intervals and eight-sided tube'
    elif any(x in n for x in ['rounded_pipe','external_riser','Nozzle_back_feeder','Nozzle_floor_hydraulic']):
        reduce(o,150 if 'rounded_pipe' in n or 'back_feeder' in n else 100);method='source pipe route retained, reduced surface'
    elif n.startswith(('Core_to_middle_','Dock_to_core_')) and n.endswith(('_seat','_plate')):
        reduce(o,36);method='source fitted flange reduced'
    elif is_cylinder(o):
        cylinder_sample(o,32 if '_inner_well_floor' in n else (16 if 'Nozzle_main' in n or '_core_hub' in n else 8));method='regular cylinder verified by axial rings'
        if n=='Nozzle_main_body':width=.10
    elif any(x in n for x in ['_rim_clamp_','_seated_rib_foot_']):
        method='physical silhouette hardware retained; fine corner rounding bakes'
    elif any(x in n for x in ['_frame','_dark_tray','_grille_seat','_vent_','_inner_rail','_side_recess','_recess_back_wall','_saddle','_bearing_pedestal']):
        method='retained small fitted hardware; subpixel chamfer assigned to bake'
    elif 'Dock_header_broad_steel_plate' in n:
        method='retained curved header plates; shallow edge roundness assigned to bake'
    elif n.startswith('Chimney_'):
        # Existing octagonal section already carries the form-defining chamfer.
        width=.035 if n=='Chimney_crown_cap' else None;method='original octagonal silhouette and fitted socket'
    elif old_bev:
        width=old_bev.width;method='retained structural chamfer, one segment'
    if width:bevel(o,width)
    bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.recalc_face_normals(bm,faces=bm.faces);bm.to_mesh(o.data);bm.free()
    # Deliberate corner normals: broad planar panels flat; cylindrical walls smooth
    # only across low-angle joins. Weighted normals keep chamfered planes calm.
    for p in o.data.polygons:p.use_smooth=role!='marker' and 'faceted_cladding' not in o.name
    o.data.set_sharp_from_angle(angle=math.radians(1 if 'faceted_cladding' in o.name else (60 if n=='Dock_arch_continuous_inner_liner' else 35)))
    if role!='marker':
        w=o.modifiers.new('Planar_and_cylinder_corner_normals','WEIGHTED_NORMAL');w.keep_sharp=True;w.weight=50;apply(o,w)
    tr=o.modifiers.new('Frozen_bake_target_triangles','TRIANGULATE');tr.keep_custom_normals=True;apply(o,tr)
    records.append({'low':o.name,'source':n,'assembly':src.get('assembly'),'triangles':len(o.data.polygons),'method':method,'retained_bevel_width_local':width,'role':role})
    if len(records)%50==0:print('PROC_LOW_PROGRESS',len(records),n,flush=True)

for o in high.objects:o.hide_render=True
high.hide_render=True
scene['final_low_review_state']='FinalLow v01 pending explicit mesh approval; no final UV/bake/texture work'
scene['approved_high_sha256']=SHA
scene['approved_finish']='generated_finish_v01/APPROVAL.md'
scene['runtime_triangle_ceiling']=10000
scene['foundation_contract']='High v04 rounded approved extended apron, exact outline retained'
bpy.context.view_layer.update()
counts=Counter()
for r in records:
    if r['role']=='geometry':counts[r['assembly']]+=r['triangles']
total=sum(counts.values())
manifest={'source':str(SOURCE.relative_to(ROOT)),'source_sha256':SHA,'finish_approval':'../generated_finish_v01/APPROVAL.md','runtime_triangles':total,'ceiling':10000,'assembly_triangles':dict(counts),'objects':records,'bake_only_sources':dropped,'preview_marker_triangles':sum(r['triangles'] for r in records if r['role']=='marker'),'stage':'actual final-low candidate; no final UVs/bakes/textures'}
(OUT/'BUILD_MANIFEST.json').write_text(json.dumps(manifest,indent=2)+'\n')
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'PROC_FinalLow_v01.blend'))
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest()==SHA
print('PROC_LOW_COUNTS',total,dict(counts))
