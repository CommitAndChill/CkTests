"""Author and render one seeded geometry-backed80cm woodland proof in Blender4.4.

Run only through the authorized factory-startup background Blender CLI. This
script uses no external textures, installs, live Blender connection or Unreal.
Default output is a new version folder; existing proof outputs are refused.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import sys
import time

import bpy
from mathutils import Vector
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
DEFAULT = ROOT / 'Plugins/CkTests/Resources/CkUsf/WoodlandPatchV1'
SEED = 781204
rng = random.Random(SEED)
MATERIALS = []
COUNTS = {}


def material(name, color, roughness):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    nodes.clear()
    output = nodes.new('ShaderNodeOutputMaterial')
    output.name = 'Output'
    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    bsdf.name = 'Beauty'
    bsdf.inputs['Base Color'].default_value = (*color, 1)
    bsdf.inputs['Roughness'].default_value = roughness
    emission = nodes.new('ShaderNodeEmission')
    emission.name = 'DataEmission'
    geometry = nodes.new('ShaderNodeNewGeometry')
    normal_scale = nodes.new('ShaderNodeVectorMath')
    normal_scale.operation = 'SCALE'
    normal_scale.inputs[3].default_value = .5
    mat.node_tree.links.new(geometry.outputs['Normal'], normal_scale.inputs[0])
    normal_bias = nodes.new('ShaderNodeVectorMath')
    normal_bias.operation = 'ADD'
    normal_bias.inputs[1].default_value = (.5, .5, .5)
    mat.node_tree.links.new(normal_scale.outputs[0], normal_bias.inputs[0])
    separate = nodes.new('ShaderNodeSeparateXYZ')
    mat.node_tree.links.new(geometry.outputs['Position'], separate.inputs[0])
    cm = nodes.new('ShaderNodeMath')
    cm.operation = 'MULTIPLY'
    cm.inputs[1].default_value = 100
    mat.node_tree.links.new(separate.outputs['Z'], cm.inputs[0])
    ao = nodes.new('ShaderNodeAmbientOcclusion')
    ao.samples = 16
    ao.inputs['Distance'].default_value = .035
    ao.inside = False
    ao.only_local = False
    mat.node_tree.links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])
    MATERIALS.append((mat, color, roughness, bsdf, emission, output, normal_bias, cm, ao))
    return mat


def set_pass(kind):
    for mat, color, roughness, bsdf, emission, output, normal, height, ao in MATERIALS:
        links = mat.node_tree.links
        for link in list(output.inputs['Surface'].links):
            links.remove(link)
        for link in list(emission.inputs['Color'].links):
            links.remove(link)
        if kind == 'Beauty':
            links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])
            continue
        links.new(emission.outputs[0], output.inputs['Surface'])
        if kind == 'BaseColor':
            emission.inputs['Color'].default_value = (*color, 1)
        elif kind == 'Roughness':
            emission.inputs['Color'].default_value = (roughness, roughness, roughness, 1)
        else:
            socket = normal.outputs[0] if kind == 'WorldNormal' else height.outputs[0] if kind == 'HeightCm' else ao.outputs['AO']
            links.new(socket, emission.inputs['Color'])


def mesh(name, vertices, faces, mats, indices=None, smooth=True):
    data = bpy.data.meshes.new(name)
    data.from_pydata(vertices, [], faces)
    data.materials.clear()
    for mat in mats:
        data.materials.append(mat)
    data.update()
    obj = bpy.data.objects.new(name, data)
    PATCH.objects.link(obj)
    for i, face in enumerate(data.polygons):
        face.use_smooth = smooth
        if indices is not None:
            face.material_index = indices[i]
    COUNTS[name.split('_')[0]] = COUNTS.get(name.split('_')[0], 0) + 1
    return obj


def ground_z(x, y):
    d = y - .85 * x - .015 * math.sin(x * 13)
    return .018 + .0015 * math.sin(x * 19 + y * 11) + .001 * math.sin(x * 39 - y * 23) + .0006 * math.sin(x * 83) * math.cos(y * 71) - .002 * math.exp(-(d / .075) ** 2)


def path_distance(x, y):
    return abs(y - .85 * x - .015 * math.sin(x * 13)) / 1.31


def curve(name, points, radius, mat):
    data = bpy.data.curves.new(name, 'CURVE')
    data.dimensions = '3D'
    data.resolution_u = 2
    data.bevel_depth = radius
    data.bevel_resolution = 1
    spline = data.splines.new('POLY')
    spline.points.add(len(points) - 1)
    for i, point in enumerate(points):
        spline.points[i].co = (*point, 1)
        spline.points[i].radius = 1 - .7 * i / max(1, len(points) - 1)
    obj = bpy.data.objects.new(name, data)
    PATCH.objects.link(obj)
    data.materials.append(mat)
    return obj


def stone(name, x, y, size, mat, grain=False):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1 if grain else 2, radius=1)
    obj = bpy.context.object
    obj.name = name
    for collection in list(obj.users_collection):
        collection.objects.unlink(obj)
    PATCH.objects.link(obj)
    sx, sy, sz = size * rng.uniform(.8, 1.2), size * rng.uniform(.65, .95), size * rng.uniform(.32, .5)
    for v in obj.data.vertices:
        distortion = rng.uniform(.87, 1.10)
        v.co *= distortion
    obj.scale = (sx, sy, sz)
    obj.rotation_euler = (rng.uniform(-.13, .13), rng.uniform(-.13, .13), rng.uniform(0, math.tau))
    obj.location = (x, y, ground_z(x, y) - sz * .45)
    obj.data.materials.append(mat)
    for face in obj.data.polygons:
        face.use_smooth = not grain
    COUNTS['grain' if grain else 'stone'] = COUNTS.get('grain' if grain else 'stone', 0) + 1
    return obj


def oak(name, x, y, length, width, angle, z_offset, mat, curl=.004):
    a = math.radians(angle)
    base = ground_z(x, y) + z_offset
    def point(t, side):
        profile = max(.0001, max(0.0, math.sin(math.pi * t)) ** .72) * (.72 + .28 * math.cos(10 * math.pi * t + .3))
        u, v = (t - .5) * length, side * width * .5 * profile
        z = base + .0017 * (1 - abs(side)) * math.sin(math.pi * t) + .002 * side ** 2 + curl * t ** 5 + .0008 * math.sin(t * 9) * side
        return (x + math.cos(a) * u - math.sin(a) * v, y + math.sin(a) * u + math.cos(a) * v, z)
    rows, cols = 45, 7
    vertices = [point(i / (rows - 1), -1 + 2 * j / (cols - 1)) for i in range(rows) for j in range(cols)]
    faces = [(i * cols + j, (i + 1) * cols + j, (i + 1) * cols + j + 1, i * cols + j + 1) for i in range(rows - 1) for j in range(cols - 1)]
    mesh(name, vertices, faces, [mat])
    curve(name + '_midrib', [(px, py, pz + .00045) for px, py, pz in [point(i / 24, 0) for i in range(25)]], .00047, VEIN)
    for i, t in enumerate([.18, .31, .44, .57, .70, .82]):
        for side in (-1, 1):
            pts = [point(t + .11 * k / 5, side * .90 * k / 5) for k in range(6)]
            curve(name + f'_branch{i}_{side}', [(px, py, pz + .0004) for px, py, pz in pts], .00026, VEIN)
    stem = [point(-.1 + .1 * k / 5, 0) for k in range(6)]
    curve(name + '_petiole', stem, .00065, TWIG)


MAPLE = [(0,-.42),(-.12,-.35),(-.42,-.45),(-.35,-.14),(-.72,-.16),(-.54,.08),(-.70,.3),(-.32,.25),(-.28,.6),(-.1,.45),(0,1),(.14,.48),(.35,.65),(.32,.27),(.72,.3),(.55,.07),(.74,-.15),(.4,-.13),(.45,-.43),(.14,-.34)]


def maple(name, x, y, length, angle, offset, mat):
    a = math.radians(angle)
    scale = length / 1.45
    base = ground_z(x, y) + offset
    def point(px, py):
        z = base + .0018 * (px ** 2 + py ** 2) + .0017 * math.sin(py * 3) + .0013 * math.sin(px * 5) * py
        return (x + scale * (math.cos(a) * px - math.sin(a) * py), y + scale * (math.sin(a) * px + math.cos(a) * py), z)
    vertices = [point(0, 0)]
    for ring in range(1, 9):
        vertices += [point(px * ring / 8, py * ring / 8) for px, py in MAPLE]
    faces = [(0, i + 1, (i + 1) % len(MAPLE) + 1) for i in range(len(MAPLE))]
    for ring in range(7):
        start = 1 + ring * len(MAPLE)
        faces += [(start + i, start + (i + 1) % len(MAPLE), start + len(MAPLE) + (i + 1) % len(MAPLE), start + len(MAPLE) + i) for i in range(len(MAPLE))]
    mesh(name, vertices, faces, [mat])
    for target in [MAPLE[i] for i in [2, 6, 8, 10, 12, 14, 18]]:
        points = [point(target[0] * t / 12, target[1] * t / 12) for t in range(13)]
        curve(name + '_vein', [(px, py, pz + .0005) for px, py, pz in points], .00034, VEIN)
    curve(name + '_petiole', [point(0, -.7), point(0, -.42), point(0, .15)], .00065, TWIG)


def blade(name, x, y, length, angle, width, height, mat):
    a = angle
    vertices = []
    for i in range(7):
        t = i / 6
        for side in (-1, 1):
            u, v = length * t, side * width * math.sin(math.pi * max(.02, t)) / 2
            vertices.append((x + math.cos(a) * u - math.sin(a) * v, y + math.sin(a) * u + math.cos(a) * v, ground_z(x, y) + .002 + height * math.sin(t * math.pi / 2)))
    mesh(name, vertices, [(i*2,i*2+1,i*2+3,i*2+2) for i in range(6)], [mat])


def moss(name, x, y, radius):
    vertices = [(x, y, ground_z(x, y) + .008)]
    faces = []
    for ring in range(1, 9):
        t = ring / 8
        for j in range(32):
            a = math.tau * j / 32
            r = radius * t * (1 + .10 * math.sin(5*a) + .07 * math.sin(9*a+.5))
            vertices.append((x+r*math.cos(a), y+r*math.sin(a), ground_z(x+r*math.cos(a),y+r*math.sin(a)) + .001 + .007 * math.sqrt(max(0,1-t*t)) + .0008*math.sin(7*a)*math.sin(t*math.pi)))
    faces += [(0,j+1,(j+1)%32+1) for j in range(32)]
    for ring in range(7):
        b = 1 + ring * 32
        faces += [(b+j,b+(j+1)%32,b+32+(j+1)%32,b+32+j) for j in range(32)]
    mesh(name, vertices, faces, [MOSS[0]])
    for i in range(34):
        a = rng.uniform(0, math.tau)
        r = radius * math.sqrt(rng.random()) * .9
        px, py = x+r*math.cos(a), y+r*math.sin(a)
        for j in range(5):
            blade(name+'_frond',px,py,rng.uniform(.003,.008),a+j*math.tau/5,rng.uniform(.0011,.0018),rng.uniform(.003,.008),rng.choice(MOSS))


def flower(name, x, y, radius):
    base = ground_z(x,y) + .006
    for j in range(6):
        a = math.tau * j / 6 + .12
        vertices = [(x,y,base)]
        for ring in range(1,5):
            t=ring/4
            for k in range(12):
                b=math.tau*k/12
                u=radius*(.32+.68*math.cos(b))*t
                v=radius*.34*math.sin(b)*t
                vertices.append((x+math.cos(a)*u-math.sin(a)*v,y+math.sin(a)*u+math.cos(a)*v,base+.0012*t*t+.0005*math.sin(b)))
        faces=[(0,k+1,(k+1)%12+1) for k in range(12)]
        for ring in range(3):
            b=1+ring*12
            faces += [(b+k,b+(k+1)%12,b+12+(k+1)%12,b+12+k) for k in range(12)]
        mesh(name+'_petal',vertices,faces,[PETAL])
    bpy.ops.mesh.primitive_uv_sphere_add(segments=12,ring_count=6,radius=1,location=(x,y,base+.0015))
    obj=bpy.context.object
    obj.name=name+'_pollen'
    for collection in list(obj.users_collection):collection.objects.unlink(obj)
    PATCH.objects.link(obj)
    obj.scale=(radius*.28,radius*.28,.0018)
    obj.data.materials.append(POLLEN)
    for k in range(5):
        blade(name+'_rosette',x,y,radius*1.1,k*math.tau/5,.002,.004,GRASS[0])
    COUNTS['flower']=COUNTS.get('flower',0)+1


def camera(name, location, target, ortho):
    data=bpy.data.cameras.new(name)
    data.type='ORTHO'
    data.ortho_scale=ortho
    obj=bpy.data.objects.new(name,data)
    bpy.context.scene.collection.objects.link(obj)
    obj.location=location
    obj.rotation_euler=(Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler()
    return obj


def load_pixels(path):
    image=bpy.data.images.load(str(path),check_existing=False)
    image.colorspace_settings.name='Non-Color'
    pixels=np.array(image.pixels[:],dtype=np.float32).reshape(image.size[1],image.size[0],4)
    bpy.data.images.remove(image)
    return pixels


def render_exr(scene, name, path):
    set_pass(name)
    scene.render.image_settings.file_format='OPEN_EXR'
    scene.render.image_settings.color_mode='RGBA'
    scene.render.image_settings.color_depth='32'
    scene.render.image_settings.exr_codec='ZIP'
    scene.render.filepath=str(path)
    bpy.ops.render.render(write_still=True)
    return load_pixels(path)


def save_png(scene, pixels, path, depth='8', gray=False):
    image=bpy.data.images.new(path.stem,pixels.shape[1],pixels.shape[0],alpha=True,float_buffer=True)
    image.colorspace_settings.name='Non-Color'
    image.pixels.foreach_set(pixels.astype(np.float32).ravel())
    scene.render.image_settings.file_format='PNG'
    scene.render.image_settings.color_mode='BW' if gray else 'RGB'
    scene.render.image_settings.color_depth=depth
    scene.render.image_settings.compression=40
    image.save_render(str(path),scene=scene)
    bpy.data.images.remove(image)


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    global PATCH,VEIN,TWIG,MOSS,GRASS,PETAL,POLLEN
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=DEFAULT)
    parser.add_argument('--resolution',type=int,default=1024,choices=[1024])
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    output=args.output.resolve()
    if (output/'Manifest.json').exists() or (output/'WoodlandPatchV1.blend').exists():
        raise RuntimeError('Refusing to replace an existing woodland proof')
    for directory in ['Masters','Maps','Proof']:(output/directory).mkdir(parents=True,exist_ok=True)
    started=time.time()
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    scene=bpy.context.scene
    scene.render.engine='CYCLES'
    scene.cycles.device='CPU'
    scene.cycles.samples=1
    scene.cycles.use_denoising=False
    scene.cycles.use_adaptive_sampling=False
    scene.cycles.seed=SEED
    scene.cycles.max_bounces=2
    scene.render.threads_mode='FIXED'
    scene.render.threads=4
    scene.render.resolution_x=scene.render.resolution_y=args.resolution
    scene.render.resolution_percentage=100
    scene.render.film_transparent=False
    scene.render.dither_intensity=0
    scene.view_settings.view_transform='Raw'
    scene.view_settings.exposure=0
    scene.view_settings.gamma=1
    scene.unit_settings.system='METRIC'
    PATCH=bpy.data.collections.new('AuthoredWoodland_80cm')
    scene.collection.children.link(PATCH)
    soil=[material('Soil_dry_'+str(i),c,.88-i*.035) for i,c in enumerate([(.071,.042,.023),(.083,.048,.026),(.056,.033,.018)])]
    moist=material('Soil_moist_clearing',(.055,.033,.022),.61)
    stone_mats=[material('Stone_neutral_'+str(i),c,r) for i,(c,r) in enumerate([((.18,.19,.18),.78),((.24,.23,.20),.83),((.12,.14,.14),.74),((.29,.27,.23),.87)])]
    leaves=[material('Oak_russet',(.20,.065,.022),.76),material('Oak_umber',(.13,.047,.019),.83),material('Oak_faded_russet',(.25,.091,.026),.79),material('Maple_ochre',(.34,.18,.038),.77)]
    VEIN=material('Leaf_raised_veins',(.30,.15,.042),.87)
    TWIG=material('Twig_bark',(.063,.034,.016),.91)
    MOSS=[material('Moss_'+str(i),c,.91) for i,c in enumerate([(.032,.070,.015),(.047,.085,.020),(.023,.049,.012)])]
    GRASS=[material('Short_grass_'+str(i),c,.84) for i,c in enumerate([(.065,.12,.026),(.10,.14,.035),(.042,.088,.020)])]
    PETAL=material('Flower_cream_petals',(.76,.73,.61),.69)
    POLLEN=material('Flower_sparse_yellow_centers',(.58,.34,.018),.83)
    # A continuous actual3D soil mesh, with the moist clearing slightly depressed.
    n=161
    vertices=[(-.4+.8*i/(n-1),-.4+.8*j/(n-1),ground_z(-.4+.8*i/(n-1),-.4+.8*j/(n-1))) for j in range(n) for i in range(n)]
    faces=[];indices=[]
    for j in range(n-1):
        for i in range(n-1):
            faces.append((j*n+i,j*n+i+1,(j+1)*n+i+1,(j+1)*n+i))
            x,y=-.4+.8*(i+.5)/(n-1),-.4+.8*(j+.5)/(n-1)
            indices.append(3 if path_distance(x,y)<.078+.01*math.sin(x*31) else rng.randrange(3))
    mesh('SoilTerrain',vertices,faces,soil+[moist],indices)
    for i in range(650):
        x,y=rng.uniform(-.395,.395),rng.uniform(-.395,.395)
        stone('SoilGranule_'+str(i),x,y,rng.uniform(.00065,.0014),rng.choice(soil),True)
    # Primary upper-left, secondary lower-right, tertiary upper-right anchors.
    for spec in [('OakA',-.21,.17,.12,.064,62,.003,leaves[0],.003),('OakB',-.255,.13,.105,.059,138,.006,leaves[1],.005),('OakC',-.155,.13,.10,.055,28,.008,leaves[2],.004)]:oak(*spec)
    maple('MaplePrimary',-.24,.235,.11,-28,.004,leaves[3])
    maple('MapleSecondary',.205,-.22,.11,25,.003,leaves[3])
    oak('OakSmallUpperRight',.29,.315,.085,.046,-18,.003,leaves[1],.010)
    oak('ConnectorLeaf',.015,.018,.096,.05,55,.003,leaves[0],.005)
    for i,(x,y,r) in enumerate([(-.285,.22,.035),(-.13,.235,.024),(.24,-.275,.021),(.26,.33,.018),(.33,.28,.015)]):stone('AnchorStone_'+str(i),x,y,r,stone_mats[i%4])
    for i in range(78):
        cx,cy=rng.choice([(-.22,.18),(.22,-.20),(.29,.30)])
        x,y=rng.gauss(cx,.075),rng.gauss(cy,.065)
        if abs(x)<.38 and abs(y)<.38 and path_distance(x,y)>.070:
            stone('EmbeddedGravel_'+str(i),x,y,rng.uniform(.0035,.012),rng.choice(stone_mats))
    for i,(x,y,r) in enumerate([(-.285,.165,.043),(-.16,.25,.032),(-.24,.29,.024),(.26,-.19,.028),(.15,-.27,.021),(.32,.335,.024)]):moss('MossCushion'+str(i),x,y,r)
    for i in range(26):
        cx,cy=rng.choice([(-.28,.22),(-.14,.20),(.24,-.25),(.30,.32)])
        x,y=rng.gauss(cx,.04),rng.gauss(cy,.035)
        if abs(x)<.38 and abs(y)<.38:
            for j in range(rng.randint(6,10)):blade('GrassClump',x,y,rng.uniform(.012,.028),rng.uniform(0,math.tau),rng.uniform(.0015,.0028),rng.uniform(.008,.018),rng.choice(GRASS))
    for i,(x,y) in enumerate([(-.135,.22),(-.115,.234),(-.145,.244),(-.12,.255),(.235,-.17),(.25,-.18),(.222,-.185)]):flower('Flower'+str(i),x,y,rng.uniform(.0055,.0075))
    curve('BrokenTwig',[(.16,-.30,ground_z(.16,-.30)+.004),(.21,-.286,ground_z(.21,-.286)+.005),(.28,-.272,ground_z(.28,-.272)+.004)],.0023,TWIG)
    curve('BrokenTwig_fork',[(.21,-.286,ground_z(.21,-.286)+.005),(.23,-.25,ground_z(.23,-.25)+.007)],.0011,TWIG)
    bake=camera('Bake_Ortho_TopDown',(0,0,1.5),(0,0,0),.8)
    beauty=camera('Beauty_Orthographic',(0,-.68,1.22),(0,0,.015),1.00)
    world=bpy.data.worlds.new('NeutralWorld')
    world.use_nodes=True
    world.node_tree.nodes['Background'].inputs[0].default_value=(.30,.36,.44,1)
    world.node_tree.nodes['Background'].inputs[1].default_value=.55
    scene.world=world
    light=bpy.data.lights.new('Large_soft_key','AREA');light.energy=90;light.shape='DISK';light.size=.75
    obj=bpy.data.objects.new('Large_soft_key',light);scene.collection.objects.link(obj);obj.location=(-.35,-.2,1.0);obj.rotation_euler=(Vector((0,0,0))-obj.location).to_track_quat('-Z','Y').to_euler()
    # Independent asymmetric plane tests channel transfer/sign before scene bake.
    probe_mat=material('Calibration_constant',(.18,.31,.47),.63)
    probe=mesh('CalibrationPlane',[(-.4,-.4,.05-.04*.4-.07*.4),(.4,-.4,.05+.04*.4-.07*.4),(.4,.4,.05+.04*.4+.07*.4),(-.4,.4,.05-.04*.4+.07*.4)],[(0,1,2,3)],[probe_mat],smooth=False)
    probe.hide_render=True
    scene.camera=beauty
    scene.view_settings.view_transform='AgX'
    scene.cycles.samples=16
    bpy.ops.wm.save_as_mainfile(filepath=str(output/'WoodlandPatchV1.blend'))
    scene.camera=bake;scene.view_settings.view_transform='Raw';scene.cycles.samples=1
    patch_objects=[obj for obj in PATCH.objects if obj!=probe]
    for obj in patch_objects:obj.hide_render=True
    probe.hide_render=False
    scene.render.resolution_x=scene.render.resolution_y=64
    probe_h=render_exr(scene,'HeightCm',output/'Proof/CalibrationHeightCm.exr')[:,:,0]
    probe_n=render_exr(scene,'WorldNormal',output/'Proof/CalibrationWorldNormal.exr')[:,:,:3]*2-1
    probe_bc=render_exr(scene,'BaseColor',output/'Proof/CalibrationLinearColor.exr')
    expected=np.array([-.04,-.07,1.0]);expected/=np.linalg.norm(expected)
    measured=np.median(probe_n[8:-8,8:-8],axis=(0,1))
    if np.max(np.abs(measured-expected))>.001 or np.max(np.abs(np.median(probe_bc[8:-8,8:-8,:3],axis=(0,1))-np.array([.18,.31,.47])))>.001:
        raise RuntimeError('Native emission normal/color calibration failed')
    for obj in patch_objects:obj.hide_render=False
    probe.hide_render=True
    scene.render.resolution_x=scene.render.resolution_y=args.resolution
    raw={}
    for channel in ['BaseColor','WorldNormal','Roughness','HeightCm','AO']:
        print('WOODLAND_PASS',channel,flush=True)
        raw[channel]=render_exr(scene,channel,output/'Masters'/(channel+'.exr'))
    h=raw['HeightCm'][:,:,0]
    min_cm,max_cm=float(h.min()),float(h.max())
    height=(h-min_cm)/(max_cm-min_cm)
    normals=raw['WorldNormal'][:,:,:3]*2-1
    lengths=np.linalg.norm(normals,axis=2)
    normals/=np.maximum(lengths[:,:,None],1e-8)
    dx=normals.copy();dx[:,:,1]*=-1
    exports={}
    for channel in ['BaseColor','Normal','Roughness','Height','AO']:
        values=raw['BaseColor'].copy() if channel=='BaseColor' else raw['Roughness'].copy()
        if channel=='BaseColor':values[:,:,:3]=np.where(values[:,:,:3]<=.0031308,values[:,:,:3]*12.92,1.055*np.maximum(values[:,:,:3],0)**(1/2.4)-.055)
        elif channel=='Normal':values[:,:,:3]=dx*.5+.5
        elif channel=='Height':values[:,:,:3]=height[:,:,None]
        elif channel=='AO':values=raw['AO'].copy()
        values[:,:,3]=1
        path=output/'Maps'/(channel+'.png')
        save_png(scene,values,path,'16' if channel=='Height' else '8',channel in ['Roughness','Height','AO'])
        exports[channel]={'path':path.relative_to(output).as_posix(),'sha256':digest(path),'width':args.resolution,'height':args.resolution,'bit_depth':16 if channel=='Height' else 8,'color_space':'sRGB' if channel=='BaseColor' else 'linear','runtime_name':'T_EnvironmentWoodland_'+channel}
    # Work in actual PNG row order: imageU=world+X, imageV=world-Y.
    ordered_h=np.flipud(h)/100
    ordered_n=np.flipud(dx)
    spacing=.8/args.resolution
    dhdu=(np.roll(ordered_h,-1,1)-np.roll(ordered_h,1,1))/(2*spacing)
    dhdv=(np.roll(ordered_h,-1,0)-np.roll(ordered_h,1,0))/(2*spacing)
    valid=(ordered_n[:,:,2]>.90)&(np.abs(dhdu)<.45)&(np.abs(dhdv)<.45)
    valid[:3]=False;valid[-3:]=False;valid[:,:3]=False;valid[:,-3:]=False
    correlation={axis:float(np.corrcoef(-slope[valid],ordered_n[:,:,index][valid])[0,1]) for axis,slope,index in [('R_vs_minus_dHdu',dhdu,0),('G_vs_minus_dHdv',dhdv,1)]}
    scene.camera=beauty;set_pass('Beauty');scene.view_settings.view_transform='AgX';scene.cycles.samples=16
    scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGB';scene.render.image_settings.color_depth='8';scene.render.filepath=str(output/'Proof/Beauty-Lit-Blender.png')
    bpy.ops.render.render(write_still=True)
    manifest={'schema_version':1,'look_id':'EnvironmentWoodland','seed':SEED,'prototype_resolution':args.resolution,'physical_width_cm':80,'physical_height_cm':80,
              'source':'Native authored3D geometry/materials; no external source texture or height inferred fromalbedo. Fine soil granules, veins and flowerpetals are actual meshes/curves.',
              'composition':'Three intentionalclusters; upperleft3russetoaks+ochremaple/moss/embeddedgraystones; lowerrightmaple/twig/3flowerrosettes; upperrightpebbles/curledleaf/moss; oneclearingconnectorleaf. Diagonalmoistsoilnegative-space clearing,7flowers in2offsetclusters.',
              'blend':{'path':'WoodlandPatchV1.blend','sha256':digest(output/'WoodlandPatchV1.blend')},'generator_source':'Tools/UsfMaterials/author_woodland_patch.py','generator_sha256':digest(Path(__file__)),
              'blender_version':bpy.app.version_string,'render_backend':'Cycles CPU,4threads; data1sample/noadaptive/no denoise/Raw; beauty16samplesAgX separately labeled',
              'data_orientation':'Orthographicimage-right=bake+X,image-up=bake+Y; PNGU+X,V-bakeY; allmapsidenticalorientation. WorldNormalEXR encodedN*.5+.5; NormalDXPNG=(Nx,-Ny,Nz)*.5+.5; do not flipgreen.',
              'height':{'z_min_cm':min_cm,'z_max_cm':max_cm,'range_cm':max_cm-min_cm,'master':'Masters/HeightCm.exr','master_precision':'32bitfloat actualtopmostGeometry.Position.Z*100 centimeters','decode':'height_cm=z_min_cm+sampleHeight16*range_cm','not_albedo_derived':True},
              'normal':{'master':'Masters/WorldNormal.exr','master_precision':'32bitfloat encodedworldspaceGeometry.Normal','normal_convention':'DirectX tangentbasis imageU/worldX,imageV/-worldY,Zup'},
              'ao':{'provenance':'ActualCyclesrayAOemission,ShaderNodeAmbientOcclusion16samples/distance.035m; no denoising. Not a neutralwhiteplaceholder.','radius_cm':3.5},
              'roughness':'Authoredmaterialvalues,notmeasured: drysoil.81-.88,moistsoil.61,stone.74-.87,leaves.76-.87,moss.91,grass.84,petals.69.',
              'calibration':{'known_plane':'z=.05+.04*x+.07*y meters','expected_world_normal':expected.tolist(),'measured_world_normal':measured.tolist(),'linear_color_probe':[.18,.31,.47],'measured_linear_color':np.median(probe_bc[8:-8,8:-8,:3],axis=(0,1)).tolist(),'normal_height_correlation_low_slope_noncliff':correlation,'correlation_pixels':int(valid.sum())},
              'counts':COUNTS,'maps':exports,'masters':{name:{'path':str((Path('Masters')/(name+'.exr')).as_posix()),'sha256':digest(output/'Masters'/(name+'.exr'))} for name in raw},
              'elapsed_seconds':time.time()-started,'limits':'One1Ksourceprototype,notUE/POMacceptance. Heightfield stores topmostsurface; doesnotpreserveseparateleafundersides,flowerpetaloverhangs,grasssilhouetteorhiddenlayers. Notseamless/notmirrored; no2Kfinalorvariantsbeforevisualdecision.'}
    (output/'Manifest.json').write_text(json.dumps(manifest,indent=2,sort_keys=True)+'\n')
    print('WOODLAND_COMPLETE',json.dumps({'manifest_sha256':digest(output/'Manifest.json'),'height_cm':[min_cm,max_cm],'correlation':correlation,'seconds':time.time()-started}),flush=True)


if __name__=='__main__':main()
