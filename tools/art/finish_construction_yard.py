"""Match the Yard to Mammoth reflectance under the SAME runtime lighting.

Run in Blender after prepare_construction_yard.py, before blender_c3m.py.
The approved authoring source is untouched. This is the requested runtime
material correction, not an exposure override in the renderer.
"""
import hashlib
import json
from pathlib import Path

import bpy
import numpy as np

root = Path(bpy.data.filepath).parent
scene = bpy.context.scene
assert not scene.get('yard_runtime_finish'), 'Refuse to grade the maps twice'
assert scene.get('source_sha256') == 'd25c5b5211d6d0faab18da9b45c6179d75eb313f378462a25e5b4674ac9b0a23'


def pixels(image):
    values = np.empty(image.size[0] * image.size[1] * 4, np.float32)
    image.pixels.foreach_get(values)
    return values.reshape(image.size[1], image.size[0], 4)


for atlas in ['Structure', 'Mechanisms']:
    stem = 'Yard_' + atlas + '_'
    metal = pixels(bpy.data.images[stem + 'Metallic'])[:, :, :1]
    for channel in ['BaseColor', 'Roughness']:
        image = bpy.data.images[stem + channel]
        values = pixels(image)
        if channel == 'BaseColor':
            # The byte source was decoded through Cycles into these float images.
            # Work in linear reflectance; preserve hue, stains and worn-metal masks.
            values[:, :, :3] *= .18 + .50 * metal
            rgb = values[:, :, :3]
            values[:, :, :3] = np.where(rgb <= .0031308, rgb * 12.92,
                                       1.055 * np.maximum(rgb, 0) ** (1 / 2.4) - .055)
        else:
            values[:, :, :3] = np.clip(np.maximum(values[:, :, :3] + .06,
                                                  .58 - .12 * metal), 0, .98)
        # Store encoded RGB without a view transform, then reload the file with its
        # correct sampling tag. Do not bake the scene's display exposure into albedo.
        image.colorspace_settings.name = 'Non-Color'
        image.pixels.foreach_set(values.ravel())
        image.update()
        image.filepath_raw = str(root / (stem + channel + '.png'))
        image.file_format = 'PNG'
        image.save()
        image.reload()
        image.colorspace_settings.name = 'sRGB' if channel == 'BaseColor' else 'Non-Color'

scene['yard_runtime_finish'] = 'Mammoth shared-light value match 01'
scene['runtime_integrated'] = True
for image in bpy.data.images:
    if image.source == 'FILE':
        image.filepath = '//' + Path(bpy.path.abspath(image.filepath)).name
bpy.context.preferences.filepaths.save_version = 0
bpy.ops.wm.save_as_mainfile(filepath=str(root / 'ConstructionYard_Runtime01.blend'), compress=True)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
report = json.loads((root / 'bake_manifest.json').read_text())
report['output_sha256'] = sha(root / report['output'])
report['runtime_finish'] = {
    'basis': 'The design review requested Mammoth-matched values and reduced shine after first engine preview',
    'linear_albedo_gain': '0.18 + 0.50 * metallic',
    'roughness': 'clamp(max(source + 0.06, 0.58 - 0.12 * metallic), 0, 0.98)',
    'lighting': 'Unchanged shared Mammoth renderer, exposure and environment',
    'geometry_uv_normals_masks_metallic': 'Unchanged',
}
for row in report['maps']:
    row['sha256'] = sha(root / row['file'])
(root / 'bake_manifest.json').write_text(json.dumps(report, indent=2) + '\n')
print('YARD_FINISH_READY', flush=True)
