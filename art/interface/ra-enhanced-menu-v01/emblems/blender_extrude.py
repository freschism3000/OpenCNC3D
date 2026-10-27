"""Run by build_emblems.py inside a headless Blender (blender -b --python this -- in.json out.json).

Each part of an emblem is a flat outline with holes. Blender's 2D curve fill makes it solid:
the outline is extruded the same depth in front and behind (so the emblem reads the same from
both faces as it turns) and its edges chamfered by the bevel. The result is triangulated and
written back as triangles with their face normals: low poly, flat faced, like the Tiberian
Dawn menu emblems."""
import bpy, bmesh, json, sys

args = sys.argv[sys.argv.index('--') + 1:]
spec = json.load(open(args[0]))
out = []
for part in spec['parts']:
    cu = bpy.data.curves.new(part['name'], 'CURVE')
    cu.dimensions = '2D'; cu.fill_mode = 'BOTH'
    cu.extrude = part['extrude']; cu.bevel_depth = part.get('bevel', 0.0)
    cu.bevel_resolution = 0; cu.resolution_u = 1
    cu.offset = part.get('offset', 0.0)
    for loop in part['loops']:
        sp = cu.splines.new('POLY'); sp.points.add(len(loop) - 1)
        for i, (x, y) in enumerate(loop): sp.points[i].co = (x, y, 0.0, 1.0)
        sp.use_cyclic_u = True
    ob = bpy.data.objects.new(part['name'], cu); bpy.context.scene.collection.objects.link(ob)
    deps = bpy.context.evaluated_depsgraph_get()
    me = ob.evaluated_get(deps).to_mesh()
    bm = bmesh.new(); bm.from_mesh(me)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    bm.normal_update()
    tris = []
    for f in bm.faces:
        n = f.normal
        tris.append({'v': [[l.vert.co.x, l.vert.co.y, l.vert.co.z + part.get('z', 0.0)] for l in f.loops], 'n': [n.x, n.y, n.z]})
    bm.free()
    out.append({'name': part['name'], 'material': part['material'], 'tris': tris})
json.dump(out, open(args[1], 'w'))
