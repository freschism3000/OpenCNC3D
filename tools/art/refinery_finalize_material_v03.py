"""Bind verified material and review files; never grant artistic approval."""
from pathlib import Path
import json,hashlib,itertools
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'art/refinery/texture_v03'
read=lambda n:json.loads((OUT/n).read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
full=read('MATERIAL_FULL_REPORT.json');fresh=read('MATERIAL_FRESH_REOPEN_QA.json');source=read('MATERIAL_SOURCE_REOPEN_QA.json');revision=read('MATERIAL_REVISION_AUDIT.json');boundary=read('BOUNDARY_PIXEL_INSPECTION.json')
assert fresh['passed'] and source['passed'];assert boundary['black_base_samples']==0 and boundary['invalid_normal_samples']==0
assert sha(ROOT/full['blend'])==full['blend_sha256']==fresh['fresh_blend_sha256']
# Carry forward only an explicit approval bound to this exact candidate.
approval=read('APPROVAL.json') if (OUT/'APPROVAL.json').exists() else {}
approved=approval.get('status')=='approved' and approval.get('blend_sha256')==full['blend_sha256']
stage='approved_finished_asset_material_runtime_pending' if approved else 'pending_finished_asset_material_approval'
material_gate='Approved; see APPROVAL.json and APPROVAL.md' if approved else 'Pending renewed design-review approval'
next_step=('Resume from ../HANDOFF.md when requested: flatten the complete normal stack, prepare runtime delivery and perform separate in-game validation.' if approved else 'Renewed design-review approval of the actual finished-material version, then runtime delivery preparation and separate in-game validation. Flatten the complete material normal stack before export.')
channels={name:np.asarray(Image.open(OUT/'material_review'/f'hero_{name}.png').convert('RGB'),dtype=np.float32) for name in ['BaseColor','Roughness','Metallic','PlayerColorMask']}
comparisons={}
for a,b in itertools.combinations(channels,2):
 d=float(abs(channels[a]-channels[b]).mean());assert d>1,(a,b,d);comparisons[a+' vs '+b]=d
records=[]
for ch,row in full['files'].items():
 p=ROOT/row['path'];assert sha(p)==row['sha256'];records.append(dict(row,channel=ch,dimensions=[4096,4096],role='authoring world-vector detail; flatten into final tangent normal before runtime' if ch=='ConcreteDetailDelta' else ('semantic authoring/QA' if ch.startswith('Semantic') else 'PBR authoring channel')))
for file,role in [('PROC_FullMaterial_v03.blend','Full mapped material review candidate'),('PROC_MaterialAuthoring_v03.blend','Editable source graphs and source texture library'),('PROC_TextureUV_v02.blend','Preserved approved mesh with frozen BakeUV')]:
 p=OUT/file;records.append({'path':str(p.relative_to(ROOT)),'sha256':sha(p),'role':role})
(OUT/'FILE_MANIFEST.json').write_text(json.dumps({'stage':stage,'files':records,'player_mask_pairing':{'mask':'PROC_Full_PlayerColorMask.png','albedo':'PROC_Full_BaseColor.png','UV':'BakeUV','dimensions':[4096,4096],'mask_colorspace':'Non-Color','black':0,'white':1},'shared_concrete':'SHARED_YARD_CONCRETE.json','runtime_exported':False,'runtime_integrated':False},indent=2)+'\n')
reviews=[]
for p in sorted((OUT/'material_review').glob('*.png')):reviews.append({'file':str(p.relative_to(ROOT)),'sha256':sha(p)})
(OUT/'REVIEW_MANIFEST.json').write_text(json.dumps({'candidate_blend_sha256':full['blend_sha256'],'images':reviews,'sheets':read('REVIEW_SHEETS.json'),'channel_render_mean_absolute_RGB_differences':comparisons,'source_views':'Actual Blender renders; Yard foundation comparison uses its actual delivered mesh and material under the matching refinery review lights','material_gate':material_gate,'runtime_validation':False},indent=2)+'\n')
(OUT/'REVIEW_STATE.json').write_text(json.dumps({'stage':stage,'blend':full['blend'],'blend_sha256':full['blend_sha256'],'approved_mesh':'../final_low_v02/APPROVAL.md','runtime_triangles':9992,'runtime_meshes':240,'maps':11,'resolution':[4096,4096],'UV':'BakeUV','geometry_uv_normals_sha256':fresh['geometry_uv_normals_sha256'],'fresh_packed_map_check':True,'source_graph_check':True,'source_graphs':source['persistent_source_graphs'],'material_revision_audit':'MATERIAL_REVISION_AUDIT.json','concrete_source':'SHARED_YARD_CONCRETE.json','player_colours_reviewed':['GDI','Nod','Blue'],'next':next_step,'integration':False},indent=2)+'\n')
print('MATERIAL_V03_VERIFIED_AND_REVIEW_BOUND',full['blend_sha256'])
