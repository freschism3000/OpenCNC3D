"""Build the proxy-derived PROC first-low blockout and actual clay review views."""

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector


TOP = [
    (-13.33, -10.63), (-0.90, -14.55), (7.48, -7.35),
    (11.25, 6.00), (-8.65, 13.76), (-15.74, -0.39),
]
BOTTOM = [
    (-13.36, -10.72), (-0.94, -14.64), (7.45, -7.44),
    (11.22, 5.91), (-8.65, 13.76), (-15.78, -0.48),
]
SOURCE_BOUNDS = {'x': (-15.78, 11.25), 'y': (-14.64, 13.76)}


def components(mesh):
    parent = list(range(len(mesh.vertices)))

    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for edge in mesh.edges:
        a, b = edge.vertices
        parent[root(a)] = root(b)
    groups = defaultdict(set)
    for i in range(len(mesh.vertices)):
        groups[root(i)].add(i)
    return list(groups.values())


def group_bounds(mesh, ids):
    return [(min(mesh.vertices[i].co[j] for i in ids),
             max(mesh.vertices[i].co[j] for i in ids)) for j in range(3)]


def aim(obj, target):
    obj.rotation_euler = (Vector(target) - obj.location).to_track_quat('-Z', 'Y').to_euler()


def inside_polygon(x, y, polygon):
    inside = False
    for a, b in zip(polygon, polygon[1:] + polygon[:1]):
        if (a[1] > y) != (b[1] > y):
            crossed_x = a[0] + (y - a[1]) * (b[0] - a[0]) / (b[1] - a[1])
            if x < crossed_x:
                inside = not inside
    return inside


def material(name, color):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*color, 1)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get('Principled BSDF')
    bsdf.inputs['Base Color'].default_value = (*color, 1)
    bsdf.inputs['Roughness'].default_value = .75
    return mat


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument('--proxy', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args(argv)
    proxy = args.proxy.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    bpy.ops.wm.obj_import(filepath=str(proxy))
    bpy.context.view_layer.update()
    imported = [o for o in bpy.context.scene.objects if o.type == 'MESH']
    if len(imported) != 1:
        raise RuntimeError(f'Expected one archived proxy object, found {len(imported)}')
    work = imported[0]
    work.name = 'PROC_FirstLow_ProxyDerived'
    work['source_proxy'] = str(proxy)
    work['source_sha256'] = hashlib.sha256(proxy.read_bytes()).hexdigest()
    work['stage'] = 'first-low v01; design-review approval pending'

    archive = work.copy()
    archive.data = work.data.copy()
    archive.name = 'REF_ArchivedProxyGDI_Unchanged_NONEXPORT'
    bpy.context.scene.collection.objects.link(archive)
    archive.hide_render = True
    archive.hide_set(True)

    # Reconstruct the editable copy from the exact archived OBJ vertex/face
    # indices. Blender's importer may weld touching islands, which obscures
    # which vertices belonged to the original foundation and vessel parts.
    proxy_vertices = []
    proxy_faces = []
    for line in proxy.read_text().splitlines():
        words = line.split()
        if not words:
            continue
        if words[0] == 'v':
            x, up, south = map(float, words[1:4])
            proxy_vertices.append((x, -south, up))
        elif words[0] == 'f':
            proxy_faces.append(tuple(int(token.split('/')[0]) - 1 for token in words[1:]))
    source_mesh = bpy.data.meshes.new('PROC_ProxyIndexedWorkingMesh')
    source_mesh.from_pydata(proxy_vertices, [], proxy_faces)
    source_mesh.update()
    work.data = source_mesh
    work.matrix_world = Matrix.Identity(4)

    groups = components(work.data)
    base = max((ids for ids in groups if group_bounds(work.data, ids)[2][1]
                - group_bounds(work.data, ids)[2][0] < .25),
               key=lambda ids: (group_bounds(work.data, ids)[0][1] - group_bounds(work.data, ids)[0][0])
               * (group_bounds(work.data, ids)[1][1] - group_bounds(work.data, ids)[1][0]))
    tank_groups = []
    for ids in groups:
        (xmin, xmax), (ymin, ymax), (zmin, zmax) = group_bounds(work.data, ids)
        if xmin >= 4.0 and xmax <= 9.85 and ymin >= -1.0 and ymax <= 5.0 and zmin >= .1 and zmax <= 6.5:
            tank_groups.append(ids)
    tank_ids = set().union(*tank_groups)
    large_groups = []
    for ids in groups:
        (xmin, xmax), (ymin, ymax), (zmin, zmax) = group_bounds(work.data, ids)
        if xmax - xmin > 12.0 and ymin >= 2.0 and ymax > 14.0 and zmax <= 6.5:
            large_groups.append(ids)
    large_ids = set().union(*large_groups)
    if not tank_ids or len(base) < 50:
        raise RuntimeError(f'Could not identify proxy foundation/tank: base={len(base)}, tank={len(tank_ids)}')
    if len(large_groups) != 2:
        raise RuntimeError(f'Expected two rear-basin islands, found {len(large_groups)}')

    # The missing front vessel is a reduced copy of the archived proxy's existing
    # connected vessel islands. The active source stays preserved unchanged above.
    tank_faces = [p for p in work.data.polygons if set(p.vertices) <= tank_ids]
    used = sorted(set(i for p in tank_faces for i in p.vertices))
    remap = {old: new for new, old in enumerate(used)}
    tank_center = Vector((6.9, 2.1, 0))
    tank_vertices = []
    for i in used:
        co = work.data.vertices[i].co
        local = Vector((tank_center.x + (co.x - tank_center.x) * .75 - 2.4,
                        tank_center.y + (co.y - tank_center.y) * .75 - 5.27,
                        co.z * .75))
        tank_vertices.append((1.2 * local.x - 3.4, 1.2 * local.y - 4.4, 1.16 * local.z))
    outside = [(x, y) for x, y, z in tank_vertices if not inside_polygon(x, y, BOTTOM)]
    if outside:
        raise RuntimeError(f'Second vessel extends outside source foundation: {len(outside)} vertices; sample {outside[:3]}')
    tank_mesh = bpy.data.meshes.new('PROC_ProxyDerivedSmallVessel_Mesh')
    tank_mesh.from_pydata(tank_vertices, [], [tuple(remap[i] for i in p.vertices) for p in tank_faces])
    tank_mesh.update()
    tank = bpy.data.objects.new('PROC_ProxyDerivedSecondVessel', tank_mesh)
    bpy.context.scene.collection.objects.link(tank)

    bm = bmesh.new()
    bm.from_mesh(work.data)
    bm.verts.ensure_lookup_table()
    large_verts = {bm.verts[i] for i in large_ids}
    bmesh.ops.delete(bm, geom=[bm.verts[i] for i in base], context='VERTS')
    for vert in bm.verts:
        if vert in large_verts:
            vert.co.x = vert.co.x * .8 - 3.5
            vert.co.y = vert.co.y * .8 - 2.5
        else:
            vert.co.x = vert.co.x * 1.2 - 3.4
            vert.co.y = vert.co.y * 1.2 - 4.4
        vert.co.z *= 1.16
    rear_contact_outside = [(v.co.x, v.co.y) for v in large_verts
                            if v.is_valid and v.co.z <= .4 and
                            not inside_polygon(v.co.x, v.co.y, BOTTOM)]
    if rear_contact_outside:
        raise RuntimeError(f'Rear basin contact outside foundation: {rear_contact_outside[:3]}')
    bm.to_mesh(work.data)
    bm.free()
    work.data.update()

    # The archived base is the starting component. It is replaced only because
    # its rounded contour cannot satisfy the measured original six-corner apron.
    foundation_verts = [(x, y, -.49) for x, y in BOTTOM] + [(x, y, .09) for x, y in TOP]
    foundation_faces = [tuple(reversed(range(6))), tuple(range(6, 12))]
    foundation_faces += [(i, (i + 1) % 6, (i + 1) % 6 + 6, i + 6) for i in range(6)]
    foundation_mesh = bpy.data.meshes.new('PROC_SourceFitFoundation_Mesh')
    foundation_mesh.from_pydata(foundation_verts, [], foundation_faces)
    foundation_mesh.update()
    foundation = bpy.data.objects.new('PROC_SourceFitFoundation', foundation_mesh)
    bpy.context.scene.collection.objects.link(foundation)
    bm = bmesh.new()
    bm.from_mesh(foundation_mesh)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(foundation_mesh)
    bm.free()

    clay = material('FirstLow neutral clay', (.48, .50, .51))
    vessel_clay = material('FirstLow vessel clay', (.55, .56, .55))
    base_clay = material('FirstLow foundation clay', (.34, .36, .37))
    for obj, mat in [(work, clay), (tank, vessel_clay), (foundation, base_clay)]:
        obj.data.materials.clear()
        obj.data.materials.append(mat)
        for face in obj.data.polygons:
            face.material_index = 0
        obj['runtime_candidate'] = True

    runtime = [work, tank, foundation]
    depsgraph = bpy.context.evaluated_depsgraph_get()
    triangles = 0
    points = []
    object_counts = {}
    for obj in runtime:
        evaluated = obj.evaluated_get(depsgraph)
        mesh = evaluated.to_mesh()
        mesh.calc_loop_triangles()
        count = len(mesh.loop_triangles)
        object_counts[obj.name] = count
        triangles += count
        points.extend(obj.matrix_world @ v.co for v in mesh.vertices)
        evaluated.to_mesh_clear()
    mins = [min(v[i] for v in points) for i in range(3)]
    maxs = [max(v[i] for v in points) for i in range(3)]
    for i, axis in enumerate(('x', 'y')):
        lo, hi = SOURCE_BOUNDS[axis]
        if abs(mins[i] - lo) > .01 or abs(maxs[i] - hi) > .01:
            raise RuntimeError(f'{axis} footprint mismatch: {(mins[i], maxs[i])} vs {(lo, hi)}')
    if mins[2] < -.50 or maxs[2] < 13.0:
        raise RuntimeError(f'Unexpected vertical bounds: {(mins[2], maxs[2])}')
    if triangles > 10000:
        raise RuntimeError(f'Structure ceiling exceeded: {triangles}')

    report = {
        'stage': 'first-low v01; pending design-review approval',
        'source_proxy': str(proxy),
        'source_proxy_sha256': hashlib.sha256(proxy.read_bytes()).hexdigest(),
        'first_low_is_proxy_derived': True,
        'proxy_foundation_replaced_to_match_cartridge': True,
        'retained_proxy_vertices_before_fit': len(work.data.vertices) + len(base),
        'removed_proxy_foundation_vertices': len(base),
        'duplicated_proxy_vessel_components': len(tank_groups),
        'duplicated_proxy_vessel_faces': len(tank_faces),
        'upper_fit': {'xy_uniform_scale': 1.2, 'xy_translation': [-3.4, -4.4], 'z_scale': 1.16},
        'rear_basin_fit': {'source_components': len(large_groups), 'xy_uniform_scale': .8,
                           'xy_translation': [-3.5, -2.5]},
        'foundation_top_xy': TOP, 'foundation_bottom_xy': BOTTOM,
        'source_body_xy_bounds': SOURCE_BOUNDS,
        'evaluated_runtime_triangles': triangles,
        'runtime_objects': object_counts,
        'runtime_bounds': {'min': mins, 'max': maxs},
        'shadow_excluded': True,
        'guide_objects_excluded': [archive.name],
    }
    (out / 'FIRST_LOW_REPORT.json').write_text(json.dumps(report, indent=2) + '\n')

    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = 24
    scene.render.resolution_x = 1200
    scene.render.resolution_y = 900
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.view_settings.view_transform = 'Standard'
    world = bpy.data.worlds.new('FirstLow review world')
    world.use_nodes = True
    world.node_tree.nodes['Background'].inputs['Color'].default_value = (.50, .52, .54, 1)
    world.node_tree.nodes['Background'].inputs['Strength'].default_value = .9
    scene.world = world
    ground_mat = material('QA ground NONEXPORT', (.27, .29, .31))
    bpy.ops.mesh.primitive_plane_add(size=200, location=(0, 0, -.51))
    ground = bpy.context.object
    ground.name = 'QA_Ground_NONEXPORT'
    ground.data.materials.append(ground_mat)
    ground['runtime_candidate'] = False
    bpy.ops.object.light_add(type='AREA', location=(-25, -30, 40))
    light = bpy.context.object
    light.data.energy = 3400
    light.data.size = 24
    aim(light, (-2, 0, 0))
    bpy.ops.object.camera_add(location=(0, 0, 40))
    cam = bpy.context.object
    cam.data.type = 'ORTHO'
    cam.data.clip_end = 300
    scene.camera = cam
    target = Vector((-2.3, -.4, 5.5))
    cam.location = target + Vector((34, -45, 36))
    aim(cam, target)
    cam.data.ortho_scale = 43
    bpy.context.preferences.filepaths.save_version = 0
    scene.render.filepath = str(out / 'first_low_hero.png')
    bpy.ops.wm.save_as_mainfile(filepath=str(out / 'PROC_FirstLow_v01.blend'))

    views = {
        'hero': ((34, -45, 36), 43),
        'opposite': ((-35, 38, 35), 43),
        'front': ((0, -55, 17), 40),
        'side': ((55, 0, 17), 40),
        'top': ((0, 0, 75), 43),
    }
    cameras = {}
    for name, (position, scale) in views.items():
        cam.location = target + Vector(position)
        aim(cam, target)
        cam.data.ortho_scale = scale
        cameras[name] = {'location': list(cam.location), 'target': list(target), 'ortho_scale': scale}
        scene.render.filepath = str(out / f'first_low_{name}.png')
        bpy.ops.render.render(write_still=True)
    (out / 'REVIEW_CAMERAS.json').write_text(json.dumps(cameras, indent=2) + '\n')
    print('PROC_FIRST_LOW', json.dumps({'triangles': triangles, 'bounds': report['runtime_bounds'],
                                         'vessel_faces': len(tank_faces)}))


if __name__ == '__main__':
    main()
