"""Read-only native Blender check; writes only this source folder's JSON report."""
import hashlib
import json
from pathlib import Path
import bpy

directory=Path(__file__).resolve().parent
manifest=json.loads((directory/'Manifest.json').read_text())
records={}
for kind,info in manifest['materials'].items():
    path=directory/info['blend']['path']
    if hashlib.sha256(path.read_bytes()).hexdigest()!=info['blend']['sha256']:
        raise RuntimeError('Selected source changed')
    bpy.ops.wm.open_mainfile(filepath=str(path))
    images=[{'name':im.name,'packed':bool(im.packed_file)} for im in bpy.data.images if im.source=='FILE']
    if not images or not all(im['packed'] for im in images):
        raise RuntimeError('Source has an unpacked file image: '+kind)
    records[kind]={'blend_sha256':info['blend']['sha256'],'all_file_images_packed':True,'file_images':len(images)}
(directory/'PackedSourceVerification.json').write_text(json.dumps({'verified':True,'native_blender':bpy.app.version_string,'sources':records,'scope':'Native file load and packing only; no render/Unreal/artistic acceptance'},indent=2)+'\n')
print('DIORAMA_PACKED_SOURCES_VERIFIED',json.dumps(records),flush=True)
