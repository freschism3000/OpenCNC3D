"""Fresh-reopen mesh-stage QA; source preservation, topology and measured fit."""
import bpy,bmesh,json,math,hashlib
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'art/refinery/final_low_v01';FILE=OUT/'PROC_FinalLow_v01.blend'
bpy.ops.wm.open_mainfile(filepath=str(FILE));scene=bpy.context.scene;low=bpy.data.collections['PROC_FinalLow_v01_RUNTIME'];high=bpy.data.collections['PROC_High_v04_NONEXPORT_BAKE_SOURCE']
bpy.context.view_layer.layer_collection.children[high.name].exclude=False
bpy.context.view_layer.update()
dg=bpy.context.evaluated_depsgraph_get();points=[];objects=[];errors=[];bevels=[];base_delta=[]
poly=json.loads((ROOT/'art/refinery/high_v04/ROUNDED_APRON.json').read_text())['foundation_contours'][0]['rounded_top_xy']
def clearance(p):return min(((b[0]-a[0])*(p.y-a[1])-(b[1]-a[1])*(p.x-a[0]))/math.hypot(b[0]-a[0],b[1]-a[1]) for a,b in zip(poly,poly[1:]+poly[:1]))
def bounds(ps):return {'min':[min(p[k] for p in ps) for k in range(3)],'max':[max(p[k] for p in ps) for k in range(3)]}
for o in low.objects:
    ev=o.evaluated_get(dg);me=ev.to_mesh();me.calc_loop_triangles();ps=[o.matrix_world@v.co for v in me.vertices];points+=ps
    bm=bmesh.new();bm.from_mesh(me)
    bad=sum(not e.is_manifold for e in bm.edges);deg=sum(t.area<1e-9 for t in me.loop_triangles)
    inverted=bm.calc_volume(signed=True)<-1e-8
    faces=[tuple(sorted(tuple(round(c,6) for c in me.vertices[i].co) for i in p.vertices)) for p in me.polygons]
    doubled=len(faces)-len(set(faces));bm.free()
    rec={'name':o.name,'source':o['high_source'],'triangles':len(me.loop_triangles),'nonmanifold_edges':bad,'degenerate_triangles':deg,'duplicate_faces':doubled,'negative_volume':inverted,'bounds':bounds(ps),'minimum_apron_clearance':min(clearance(p) for p in ps),'uv_layers':len(me.uv_layers),'modifiers':len(o.modifiers)}
    objects.append(rec)
    if bad or deg or doubled or inverted or not all(math.isfinite(c) for p in ps for c in p):errors.append(rec)
    src=bpy.data.objects[o['high_source']]
    sp=[];sf=[]
    for sn in json.loads(o.get('high_sources',json.dumps([src.name]))):
        so=bpy.data.objects[sn];se=so.evaluated_get(dg);sm=se.to_mesh();sm.calc_loop_triangles();offset=len(sp)
        sp.extend(so.matrix_world@v.co for v in sm.vertices);sf.extend(tuple(i+offset for i in t.vertices) for t in sm.loop_triangles);se.to_mesh_clear()
    st=BVHTree.FromPolygons(sp,sf,all_triangles=True)
    lt=BVHTree.FromPolygons(ps,[tuple(t.vertices) for t in me.loop_triangles],all_triangles=True)
    rec['max_low_vertex_to_high_surface']=max(st.find_nearest(v)[3] for v in ps)
    rec['max_high_vertex_to_low_surface']=max(lt.find_nearest(v)[3] for v in sp)
    if o['high_source']=='Foundation_FRONT_APRON_REVISED':
        # Every original contour vertex must still be present in the low slab.
        base_delta.append(max(min((o.matrix_world@v.co-src.matrix_world@s.co).length for v in me.vertices) for s in src.data.vertices))
    ev.to_mesh_clear()
# Chamfer sample: measured face setback at a box's unchanged max-x side, not slider width.
for name in ['Dock_shoulder_1','Dock_jamb_panel_-1_0','Core_access_tangent_cover']:
    lo=bpy.data.objects['LOW_'+name];hi=bpy.data.objects[name];raw=[v.co for v in hi.data.vertices]
    mx=max(v.x for v in raw);my=max(v.y for v in raw);z0=min(v.z for v in raw);z1=max(v.z for v in raw)
    w=next(m.width for m in hi.modifiers if m.type=='BEVEL')
    cuts=[v.co.y for v in lo.data.vertices if abs(v.co.x-mx)<1e-5 and z0+w*.5<v.co.z<z1-w*.5]
    setback=my-max(cuts) if cuts else None
    bevels.append({'object':lo.name,'nominal_high_width':w,'measured_low_local_setback':setback,'world_setback':setback*lo.matrix_world.to_scale().y if setback is not None else None,'pixels_at_review_1400_width_48_units':setback*1400/48 if setback else None,'pixels_at_320_width_48_units':setback*320/48 if setback else None})
hp=[]
for o in high.objects:
    e=o.evaluated_get(dg);m=e.to_mesh();hp.extend(o.matrix_world@v.co for v in m.vertices);e.to_mesh_clear()
hb=bounds(hp);lb=bounds(points)
outside=[x for x in objects if not x['source'].startswith('Foundation_') and x['minimum_apron_clearance']<-.015]
harv=bpy.data.collections['QA_HARV_SIZE_REFERENCE_NONEXPORT'];harvpts=[o.matrix_world@v.co for o in harv.objects for v in o.data.vertices]
core=(-5.1,6.15,5.29*5.35/4.95);mid=(4.9,-1.92,3.26);front=(2.55,-8.24,2.47)
gap={label:math.hypot(a[0]-b[0],a[1]-b[1])-a[2]-b[2] for a,b,label in [(core,mid,'core_middle'),(mid,front,'middle_front'),(core,front,'core_front')]}
count=sum(o['triangles'] for o in objects)
report={'file':str(FILE.relative_to(ROOT)),'sha256':hashlib.sha256(FILE.read_bytes()).hexdigest(),'runtime_meshes':len(objects),'evaluated_runtime_triangles':count,'ceiling':10000,'within_ceiling':count<=10000,'bounds':lb,'high_v04_bounds':hb,'high_v04_bound_deltas':{k:[lb[k][i]-hb[k][i] for i in range(3)] for k in lb},'approved_base_contour_max_vertex_delta':max(base_delta),'original_source_body_bounds':{'min':[-15.78,-14.64,-.49],'max':[11.25,13.76,13.82]},'source_footprint_amendment':'The design review approved 5.4-unit longer front apron plus rounded corners in High v04; that full shaped contour is retained. Original bounds are historical, not the amended outline.','topology_errors':errors,'objects_outside_approved_apron':outside,'harvester_static_minimum_outline_clearance':min(clearance(p) for p in harvpts),'conservative_tank_clearances':gap,'evaluated_chamfer_samples':bevels,'total_uv_layers':sum(o['uv_layers'] for o in objects),'objects':objects,'scope':'Mesh-stage checks; source HARV static outline reference only, not runtime navigation or docking validation. Fine bake relief not yet present.'}
(OUT/'FRESH_REOPEN_QA.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='objects'},indent=2))
