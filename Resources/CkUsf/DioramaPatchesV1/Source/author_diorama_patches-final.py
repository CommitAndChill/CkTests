"""Native Blender CPU authoring of three new80cm nonrepeating ground patches.

Run --stage beauty first in factory-startup background Blender4.4,4threads.
After source-art review, --stage bake loads the saved sources without rebuilding.
Host --stage sheets only composes existing native pixels; it never creates maps.
All writes stay in the new DioramaPatchesV1 folder. No Unreal/live Blender/UI.
"""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import random
import sys
import time

ROOT=Path(__file__).resolve().parents[2]
OUTPUT=ROOT/'Plugins/CkTests/Resources/CkUsf/DioramaPatchesV1'
HELPER=ROOT/'Tools/UsfMaterials/author_woodland_patch.py'
SOURCES=ROOT/'Plugins/CkTests/Resources/CkUsf'
IDS={'grass':'EnvironmentDioramaGrass','dirt':'EnvironmentDioramaDirt','mud':'EnvironmentDioramaMud'}
SEEDS={'grass':904731,'dirt':904733,'mud':904739}
VERSION='V2'
CROPS=[(40,30,275,475),(340,135,570,425),(690,75,925,480),(45,565,275,965),(370,550,600,980),(715,565,935,970)]


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def load_helper():
    spec=importlib.util.spec_from_file_location('ck_diorama_native_helpers',HELPER)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module


def provenance():
    records={}
    for relative in ['Environment1Textures/Manifest.json','Environment1Textures/License/polyhaven.html','Environment1Textures/gravel/Maps/Height.png','Environment1Textures/gravel/Maps/Roughness.png',
                     'WoodlandSourcesV1/Soil4KReceipt.json','WoodlandSourcesV1/gravelly_sand_diff_4k.jpg','WoodlandSourcesV1/Receipt.json','WoodlandSourcesV1/License.html',
                     'WoodlandSourcesV1/LeafSet001_1K-PNG_Color.png','WoodlandSourcesV1/LeafSet001_1K-PNG_Opacity.png','WoodlandSourcesV1/LeafSet001_1K-PNG_Displacement.png','WoodlandSourcesV1/LeafSet001_1K-PNG_Roughness.png',
                     'Batch3Textures/Manifest.json','Batch3Textures/granite/Maps/BaseColor.png']:
        path=SOURCES/relative
        if not path.exists():raise RuntimeError('Required licensed source missing: '+str(path))
        records[path.relative_to(ROOT).as_posix()]=sha(path)
    return records


def wetness(x,y):
    return max(math.exp(-(((x-cx)/rx)**4+((y-cy)/ry)**4)) for cx,cy,rx,ry in [(-.13,.075,.14,.105),(.155,-.15,.115,.13),(.20,.22,.09,.07)])


def setup_ground(w,kind):
    import bpy
    import numpy as np
    height_image=bpy.data.images.load(str(SOURCES/'Environment1Textures/gravel/Maps/Height.png'))
    height_image.colorspace_settings.name='Non-Color'
    h=np.array(height_image.pixels[:],dtype=np.float32).reshape(height_image.size[1],height_image.size[0],4)[:,:,0]
    def height(x,y):
        # Original2.48m source scale; no height inferred from color.
        fx=(x/2.48+.5)*(h.shape[1]-1);fy=(y/2.48+.5)*(h.shape[0]-1)
        ix,iy=int(fx),int(fy);tx,ty=fx-ix,fy-iy
        scan=(1-tx)*(1-ty)*h[iy,ix]+tx*(1-ty)*h[iy,ix+1]+(1-tx)*ty*h[iy+1,ix]+tx*ty*h[iy+1,ix+1]
        relief=.007*(float(scan)-.5)+.0012*math.sin(21*x+13*y)+.0009*math.sin(65*x-31*y)
        wet=wetness(x,y) if kind=='mud' else 0
        # Flatten genuine basin floors and retain scan relief on their rough rims.
        return .018+relief*(1-.90*wet)-.012*wet
    w.ground_z=height
    mat=w.material('Soil_Diorama_'+kind,(.07,.04,.02),.84)
    nodes=mat.node_tree.nodes;links=mat.node_tree.links
    for node in nodes:
        if node.bl_idname=='ShaderNodeTexImage' and node.image and 'BaseColor.png' in node.image.filepath:
            node.image=bpy.data.images.load(str(SOURCES/'WoodlandSourcesV1/gravelly_sand_diff_4k.jpg'),check_existing=True);node.image.colorspace_settings.name='sRGB'
    color=nodes['StructuralColor'];color.inputs[2].default_value={'grass':(.20,.23,.16,1),'dirt':(.22,.22,.20,1),'mud':(.19,.17,.15,1)}[kind]
    if kind=='mud':
        attr=nodes.new('ShaderNodeAttribute');attr.attribute_name='Wetness'
        rough=nodes['StructuralRoughness'];rough.name='UnusedOriginalRoughness'
        mix=nodes.new('ShaderNodeMapRange');mix.name='StructuralRoughness';mix.inputs['To Min'].default_value=.82;mix.inputs['To Max'].default_value=.10
        links.new(attr.outputs['Fac'],mix.inputs['Value']);links.new(mix.outputs[0],nodes['Beauty'].inputs['Roughness'])
        color.name='UnderlyingSoilColor'
        wetcolor=nodes.new('ShaderNodeMixRGB');wetcolor.name='StructuralColor';wetcolor.blend_type='MULTIPLY';wetcolor.inputs[2].default_value=(.24,.29,.32,1)
        links.new(attr.outputs['Fac'],wetcolor.inputs[0]);links.new(color.outputs[0],wetcolor.inputs[1]);links.new(wetcolor.outputs[0],nodes['Beauty'].inputs['Base Color'])
    count=321
    vertices=[(-.4+.8*i/(count-1),-.4+.8*j/(count-1),height(-.4+.8*i/(count-1),-.4+.8*j/(count-1))) for j in range(count) for i in range(count)]
    faces=[(j*count+i,j*count+i+1,(j+1)*count+i+1,(j+1)*count+i) for j in range(count-1) for i in range(count-1)]
    obj=w.mesh('ScanDisplacedGround',vertices,faces,[mat]);attr=obj.data.attributes.new(name='Wetness',type='FLOAT',domain='POINT')
    for item,(x,y,z) in zip(attr.data,vertices):item.value=wetness(x,y) if kind=='mud' else 0
    return height


def grass_material(w,index):
    mat=w.material('Blade_'+str(index),(.035,.09,.012),.8);nodes=mat.node_tree.nodes;links=mat.node_tree.links
    uv=nodes.new('ShaderNodeTexCoord');sep=nodes.new('ShaderNodeSeparateXYZ');links.new(uv.outputs['UV'],sep.inputs[0])
    ramp=nodes.new('ShaderNodeValToRGB');ramp.name='StructuralColor'
    base=[(.016,.043,.006),(.024,.059,.009),(.033,.060,.009)][index]
    tip=[(.12,.15,.025),(.075,.13,.018),(.16,.15,.035)][index]
    ramp.color_ramp.elements[0].color=(*base,1);ramp.color_ramp.elements[1].color=(*tip,1)
    ramp.color_ramp.elements.new(.55).color=(base[0]*2.6,base[1]*2.1,base[2]*2.5,1)
    links.new(sep.outputs['Y'],ramp.inputs[0]);links.new(ramp.outputs[0],nodes['Beauty'].inputs['Base Color'])
    return mat


def grass_batch(w,rng,kind,mats):
    vertices=[];faces=[];indices=[];uvs=[];clumps=550 if kind=='grass' else 35 if kind=='dirt' else 15
    centers=[]
    while len(centers)<clumps:
        x,y=rng.uniform(-.385,.385),rng.uniform(-.385,.385)
        if kind=='grass':
            quiet=abs(y+.32*x-.045*math.sin(x*13))<.025
            density=.74+.24*math.sin(x*15+y*4)*math.cos(y*11)
            if rng.random()>(.15 if quiet else density):continue
        elif kind=='dirt':
            if abs(x)<.27 and abs(y)<.26 and rng.random()<.85:continue
        elif wetness(x,y)>.25:continue
        centers.append((x,y))
    for x,y in centers:
        angle=rng.uniform(0,math.tau)
        for j in range(rng.randint(8,14)):
            a=angle+j*2.399+rng.uniform(-.3,.3);length=rng.uniform(.035,.09)*.65;width=rng.uniform(.0017,.0032)*1.8
            rise=rng.uniform(.013,.039) if kind=='grass' else rng.uniform(.009,.024)
            px=x+rng.uniform(-.005,.005);py=y+rng.uniform(-.005,.005);z=w.ground_z(px,py)+.0007
            start=len(vertices);mat_index=rng.randrange(3);bend=rng.uniform(-.010,.010)
            for k in range(9):
                t=k/8;lean=length*(t**1.15);taper=(1-t)**.75
                for side in [-1,0,1]:
                    lateral=side*width*.5*taper+bend*math.sin(t*math.pi*.8)**2
                    vertices.append((px+math.cos(a)*lean-math.sin(a)*lateral,py+math.sin(a)*lean+math.cos(a)*lateral,z+rise*math.sin(t*math.pi*.91)+.0006*(1-abs(side))*math.sin(t*math.pi)))
                    uvs.append(((side+1)/2,t))
            for k in range(8):
                for c in range(2):faces.append((start+k*3+c,start+k*3+c+1,start+(k+1)*3+c+1,start+(k+1)*3+c));indices.append(mat_index)
    obj=w.mesh('RecognizableBladeClumps',vertices,faces,mats,indices);uv=obj.data.uv_layers.new(name='BladeLength')
    for polygon in obj.data.polygons:
        for loop in polygon.loop_indices:uv.data[loop].uv=uvs[obj.data.loops[loop].vertex_index]
    return {'clumps':len(centers),'blades':len(vertices)//27}


def gravel_batch(w,rng,kind):
    import bpy
    mats=[]
    for i,tint in enumerate([(.17,.19,.20,1),(.25,.19,.13,1),(.12,.14,.14,1),(.27,.25,.20,1),(.13,.105,.08,1)]):
        mat=w.material('EmbeddedMineral_'+str(i),(.1,.1,.1),.78);nodes=mat.node_tree.nodes;links=mat.node_tree.links
        geo=nodes.new('ShaderNodeNewGeometry');scale=nodes.new('ShaderNodeVectorMath');scale.operation='SCALE';scale.inputs[3].default_value=5;links.new(geo.outputs['Position'],scale.inputs[0])
        tex=nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(str(SOURCES/'Batch3Textures/granite/Maps/BaseColor.png'),check_existing=True);tex.image.colorspace_settings.name='sRGB';tex.projection='BOX';tex.projection_blend=.25;links.new(scale.outputs[0],tex.inputs[0])
        gray=nodes.new('ShaderNodeRGBToBW');links.new(tex.outputs[0],gray.inputs[0]);color=nodes.new('ShaderNodeMixRGB');color.name='StructuralColor';color.blend_type='MULTIPLY';color.inputs[0].default_value=1;color.inputs[2].default_value=tint;links.new(gray.outputs[0],color.inputs[1]);links.new(color.outputs[0],nodes['Beauty'].inputs['Base Color']);mats.append(mat)
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2,radius=1);temporary=bpy.context.object
    template=[v.co.copy() for v in temporary.data.vertices];triangles=[tuple(p.vertices) for p in temporary.data.polygons];bpy.data.objects.remove(temporary,do_unlink=True)
    vertices=[];faces=[];indices=[];count=180 if kind=='grass' else 390 if kind=='dirt' else 260
    for i in range(count):
        x,y=rng.uniform(-.38,.38),rng.uniform(-.38,.38)
        size=rng.uniform(.002,.013)*(1.8 if i%17==0 else 1);a=rng.uniform(0,math.tau)
        sx,sy,sz=size*rng.uniform(.8,1.25),size*rng.uniform(.7,1.15),size*rng.uniform(.55,.85)
        center=w.ground_z(x,y)-sz*(.60 if kind=='mud' else .28);start=len(vertices);mi=rng.randrange(len(mats))
        for v in template:
            d=1+.06*math.sin(v.x*7+v.y*5+i);px,py=v.x*sx*d,v.y*sy*d
            vertices.append((x+math.cos(a)*px-math.sin(a)*py,y+math.sin(a)*px+math.cos(a)*py,center+v.z*sz*d))
        faces.extend(tuple(start+k for k in t) for t in triangles);indices.extend([mi]*len(triangles))
    w.mesh('PartlyBuriedMineralGravel',vertices,faces,mats,indices)
    return count


def leaf_litter(w,rng,kind):
    count={'grass':11,'dirt':40,'mud':19}[kind]
    for i in range(count):
        if kind=='dirt':
            cx,cy=[(-.18,.13),(.13,-.16),(.19,.23)][i%3];x,y=rng.gauss(cx,.082),rng.gauss(cy,.071)
        else:x,y=rng.uniform(-.32,.32),rng.uniform(-.32,.32)
        x,y=max(-.32,min(.32,x)),max(-.32,min(.32,y))
        length=rng.uniform(.055,.12) if kind!='grass' else rng.uniform(.045,.08)
        tint=[(1.18,.70,.28),(.90,.86,.45),(.83,.96,.61),(.88,.46,.17)][i%4]
        offset=(.0015+.001*(i%4)) if kind!='mud' else .001
        w.photo_leaf('CurledPhotoLitter_'+str(i),x,y,length,rng.uniform(0,360),CROPS[i%6],tint,offset,rng.uniform(.004,.011))
    return count


def build_patch(kind):
    import bpy
    from mathutils import Vector
    w=load_helper();rng=random.Random(SEEDS[kind]);w.rng=rng;w.MATERIALS.clear();w.COUNTS.clear()
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=16;scene.cycles.use_denoising=False;scene.cycles.use_adaptive_sampling=False;scene.cycles.seed=SEEDS[kind]
    scene.render.threads_mode='FIXED';scene.render.threads=4;scene.render.resolution_x=scene.render.resolution_y=1024;scene.render.resolution_percentage=100;scene.render.dither_intensity=0
    scene.cycles.max_bounces=3;scene.cycles.transparent_max_bounces=12
    w.PATCH=bpy.data.collections.new('AuthoredDiorama_'+kind);scene.collection.children.link(w.PATCH)
    setup_ground(w,kind);grass=[grass_material(w,i) for i in range(3)];w.GRASS=grass
    w.PETAL=w.material('CreamFlowerPetals',(.64,.64,.54),.75);w.POLLEN=w.material('TinyYellowCenters',(.45,.27,.012),.84)
    counts=grass_batch(w,rng,kind,grass);counts['stones']=gravel_batch(w,rng,kind);counts['photo_leaves']=leaf_litter(w,rng,kind)
    flowers={'grass':16,'dirt':9,'mud':5}[kind]
    for i in range(flowers):
        cx,cy=[(-.13,.16),(.19,-.18)][i%2];x,y=rng.gauss(cx,.055),rng.gauss(cy,.045)
        if kind=='mud' and wetness(x,y)>.4:x,y=-.29+rng.uniform(-.03,.03),.22+rng.uniform(-.03,.03)
        before=set(bpy.data.objects);name='SmallFlower_'+str(i)
        w.flower(name,x,y,rng.uniform(.009,.012))
        lift=.039 if kind=='grass' else .009
        for flower_object in set(bpy.data.objects)-before:
            if '_petal' in flower_object.name or '_pollen' in flower_object.name:flower_object.location.z+=lift
        stem=w.material('FlowerStem_'+str(i),(.025,.065,.007),.83)
        w.curve(name+'_stem',[(x,y,w.ground_z(x,y)+.001),(x,y,w.ground_z(x,y)+.006+lift)],.00055,stem)
    counts['flowers']=flowers
    twig=w.material('SmallTwigBark',(.033,.017,.007),.92)
    for i in range({'grass':4,'dirt':9,'mud':6}[kind]):
        x,y=rng.uniform(-.29,.29),rng.uniform(-.29,.29);a=rng.uniform(0,math.tau);length=rng.uniform(.035,.085)
        w.curve('ShortTwig_'+str(i),[(x+math.cos(a)*length*t,y+math.sin(a)*length*t,w.ground_z(x+math.cos(a)*length*t,y+math.sin(a)*length*t)+.0015+.001*math.sin(t*math.pi)) for t in [0,.2,.4,.6,.8,1]],.001,twig)
    w.camera('Bake_Ortho_TopDown',(0,0,1.5),(0,0,0),.8);beauty=w.camera('Beauty_Orthographic',(0,-.62,1.3),(0,0,.02),.94);scene.camera=beauty
    world=bpy.data.worlds.new('NeutralDioramaWorld');world.use_nodes=True;world.node_tree.nodes['Background'].inputs[0].default_value=(.30,.36,.44,1);world.node_tree.nodes['Background'].inputs[1].default_value=.24;scene.world=world
    light=bpy.data.lights.new('SoftDaylightKey','AREA');light.energy=28;light.shape='DISK';light.size=.65;obj=bpy.data.objects.new('SoftDaylightKey',light);scene.collection.objects.link(obj);obj.location=(-.33,-.21,1);obj.rotation_euler=(Vector((0,0,0))-obj.location).to_track_quat('-Z','Y').to_euler()
    scene.view_settings.view_transform='AgX';scene.view_settings.exposure=0;scene.view_settings.gamma=1
    output=OUTPUT/VERSION/kind;output.mkdir(parents=True,exist_ok=True);(output/'Proof').mkdir(exist_ok=True)
    bpy.ops.file.pack_all();blend=output/(IDS[kind]+'.blend')
    if blend.exists():raise RuntimeError('Refusing to replace native source '+str(blend))
    bpy.ops.wm.save_as_mainfile(filepath=str(blend))
    scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGB';scene.render.image_settings.color_depth='8';scene.render.filepath=str(output/'Proof/Beauty-Lit-Blender.png');bpy.ops.render.render(write_still=True)
    receipt={'kind':kind,'look_id':IDS[kind],'seed':SEEDS[kind],'physical_width_cm':80,'physical_height_cm':80,'resolution':1024,'stage':'Native source beauty only; maps not yet baked; visualreview pending','blend':{'path':blend.relative_to(OUTPUT).as_posix(),'sha256':sha(blend)},'beauty':{'path':(output/'Proof/Beauty-Lit-Blender.png').relative_to(OUTPUT).as_posix(),'sha256':sha(output/'Proof/Beauty-Lit-Blender.png')},'counts':counts,'generator_source':'Tools/UsfMaterials/author_diorama_patches.py','generator_sha256':sha(Path(__file__)),'helper_source':HELPER.relative_to(ROOT).as_posix(),'helper_sha256':sha(HELPER),'licensed_source_sha256':provenance(),'source_identity_notes':'PolyHaven gravelly_sand scanned foundation (DarioBarresi,CC0), ambientCG LeafSet001 species-unverified photos(approximatePBR,CC0), Granite002A mineralBC(CC0). Grass blades/flowers/placement are authored geometry, not a grass scan. Mud wetness is authored soil deformation/roughness, not a calibrated wet-mud scan.','composition':'Newkind-specific connectedbladefield or clusteredlitter/wetdepressions; no reusedWoodlandV3arrangement.','limits':'Non-tileable80cm source; heightfield later retains topmostsurface only, losesoverhangs/undersides/independentgrasssilhouettes. NoUE/performance/userartacceptance.'}
    (output/'BeautyReceipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print('DIORAMA_BEAUTY_READY',kind,str(output/'Proof/Beauty-Lit-Blender.png'),flush=True)


def sheets():
    from PIL import Image,ImageDraw
    sheet=Image.new('RGB',(1536,1056),(23,27,31));draw=ImageDraw.Draw(sheet)
    for i,kind in enumerate(IDS):
        path=OUTPUT/VERSION/kind/'Proof/Beauty-Lit-Blender.png';im=Image.open(path).convert('RGB');sheet.paste(im.resize((512,512)),(i*512,24));draw.text((i*512+10,6),kind+' / native Blender beauty',fill='white')
        sheet.paste(im.crop((256,256,768,768)).resize((512,512)),(i*512,544));draw.text((i*512+10,526),'2x crop / same pixels, no grading',fill='white')
    sheet.save(OUTPUT/('BeautyContactSheet-'+VERSION+'.png'));print(OUTPUT/('BeautyContactSheet-'+VERSION+'.png'))


def bind_materials(w):
    import bpy
    import numpy as np
    w.MATERIALS.clear()
    for mat in bpy.data.materials:
        if not mat.use_nodes or 'Beauty' not in mat.node_tree.nodes:continue
        nodes=mat.node_tree.nodes;bsdf=nodes['Beauty']
        normal=next(n for n in nodes if n.bl_idname=='ShaderNodeVectorMath' and n.operation=='ADD' and np.allclose(n.inputs[1].default_value[:],(.5,.5,.5)))
        height=next(n for n in nodes if n.bl_idname=='ShaderNodeMath' and n.operation=='MULTIPLY' and abs(n.inputs[1].default_value-100)<1e-6)
        ao=next(n for n in nodes if n.bl_idname=='ShaderNodeAmbientOcclusion')
        w.MATERIALS.append((mat,tuple(bsdf.inputs['Base Color'].default_value[:3]),bsdf.inputs['Roughness'].default_value,bsdf,nodes['DataEmission'],nodes['Output'],normal,height,ao))


def bake_patch(kind):
    import bpy
    import numpy as np
    started=time.time();out=OUTPUT/VERSION/kind;beauty=json.loads((out/'BeautyReceipt.json').read_text());blend=OUTPUT/beauty['blend']['path']
    if sha(blend)!=beauty['blend']['sha256']:raise RuntimeError('Reviewed source blend changed')
    if (out/'Maps').exists() and any((out/'Maps').iterdir()):raise RuntimeError('Refusing to overwrite existing maps')
    for name in ['Maps','Masters','Calibration']:(out/name).mkdir(exist_ok=True)
    bpy.ops.wm.open_mainfile(filepath=str(blend));w=load_helper();bind_materials(w)
    scene=bpy.context.scene;scene.camera=bpy.data.objects['Bake_Ortho_TopDown'];scene.view_settings.view_transform='Raw';scene.view_settings.exposure=0;scene.view_settings.gamma=1
    scene.render.dither_intensity=0;scene.cycles.samples=1;scene.cycles.use_denoising=False;scene.cycles.use_adaptive_sampling=False;scene.cycles.device='CPU';scene.render.threads_mode='FIXED';scene.render.threads=4
    w.PATCH=bpy.data.collections['AuthoredDiorama_'+kind]
    probe_mat=w.material('AsymmetricTransferCalibration',(.18,.31,.47),.63)
    probe=w.mesh('AsymmetricPlane',[(-.4,-.4,.05-.04*.4-.07*.4),(.4,-.4,.05+.04*.4-.07*.4),(.4,.4,.05+.04*.4+.07*.4),(-.4,.4,.05-.04*.4+.07*.4)],[(0,1,2,3)],[probe_mat],smooth=False)
    hidden={obj:obj.hide_render for obj in bpy.data.objects if obj.type in {'MESH','CURVE'} and obj!=probe}
    for obj in hidden:obj.hide_render=True
    scene.render.resolution_x=scene.render.resolution_y=64
    ph=w.render_exr(scene,'HeightCm',out/'Calibration/HeightCm.exr')[:,:,0]
    pn=w.render_exr(scene,'WorldNormal',out/'Calibration/WorldNormal.exr')[:,:,:3]*2-1
    pc=w.render_exr(scene,'BaseColor',out/'Calibration/LinearColor.exr')
    expected=np.array([-.04,-.07,1.]);expected/=np.linalg.norm(expected)
    measured=np.median(pn[8:-8,8:-8],axis=(0,1));color=np.median(pc[8:-8,8:-8,:3],axis=(0,1))
    if max(abs(measured-expected))>.001 or max(abs(color-[.18,.31,.47]))>.001:raise RuntimeError('Native emission transfer/sign calibration failed')
    cp=pc.copy();cp[:,:,:3]=np.where(cp[:,:,:3]<=.0031308,cp[:,:,:3]*12.92,1.055*np.maximum(cp[:,:,:3],0)**(1/2.4)-.055)
    w.save_png(scene,cp,out/'Calibration/BaseColor-sRGB8.png')
    cp[:,:,:3]=pn*np.array([1,-1,1])*.5+.5;w.save_png(scene,cp,out/'Calibration/NormalDX-linear16.png','16')
    for obj,value in hidden.items():obj.hide_render=value
    probe.hide_render=True;scene.render.resolution_x=scene.render.resolution_y=1024
    raw={}
    for channel in ['BaseColor','WorldNormal','Roughness','HeightCm','AO']:
        print('DIORAMA_DATA',kind,channel,flush=True);raw[channel]=w.render_exr(scene,channel,out/'Masters'/(channel+'.exr'))
    h=raw['HeightCm'][:,:,0];minimum,maximum=float(h.min()),float(h.max());span=maximum-minimum
    if not np.isfinite(h).all() or not 0<span<=6:raise RuntimeError('Actual relief exceeds existing6cm grid: '+str(span))
    normals=raw['WorldNormal'][:,:,:3]*2-1;normals/=np.maximum(np.linalg.norm(normals,axis=2)[:,:,None],1e-8);dx=normals.copy();dx[:,:,1]*=-1
    maps={}
    for channel in ['BaseColor','Normal','Roughness','Height','AO']:
        values=raw['BaseColor'].copy() if channel=='BaseColor' else raw['Roughness'].copy()
        if channel=='BaseColor':values[:,:,:3]=np.where(values[:,:,:3]<=.0031308,values[:,:,:3]*12.92,1.055*np.maximum(values[:,:,:3],0)**(1/2.4)-.055)
        elif channel=='Normal':values[:,:,:3]=dx*.5+.5
        elif channel=='Height':values[:,:,:3]=((h-minimum)/span)[:,:,None]
        elif channel=='AO':values=raw['AO'].copy()
        values[:,:,3]=1;bits=16 if channel in ['Normal','Height'] else 8
        path=out/'Maps'/(channel+'.png');w.save_png(scene,values,path,str(bits),channel in ['Height','Roughness','AO'])
        maps[channel]={'path':path.relative_to(OUTPUT).as_posix(),'sha256':sha(path),'runtime_name':'T_'+IDS[kind]+'_'+channel,'width':1024,'height':1024,'bit_depth':bits,'color_space':'sRGB' if channel=='BaseColor' else 'linear','normal_convention':'DirectX imageU/worldX,imageV/-worldY,Zup; no green flip' if channel=='Normal' else None,'import_settings':{'sRGB':channel=='BaseColor','address_x':'Clamp','address_y':'Clamp','mips':'TMGS_SimpleAverage','flip_green_channel':False}}
    ordered_h=np.flipud(h)/100;ordered_n=np.flipud(dx);spacing=.8/1024
    u=(np.roll(ordered_h,-1,1)-np.roll(ordered_h,1,1))/(2*spacing);v=(np.roll(ordered_h,-1,0)-np.roll(ordered_h,1,0))/(2*spacing)
    valid=(ordered_n[:,:,2]>.9)&(abs(u)<.45)&(abs(v)<.45);valid[:3]=False;valid[-3:]=False;valid[:,:3]=False;valid[:,-3:]=False
    corr=[float(np.corrcoef(-a[valid],ordered_n[:,:,i][valid])[0,1]) for i,a in enumerate([u,v])]
    # The mud attribute is the same field that flattens/depresses true terrain.
    wet_stats=None
    if kind=='mud':
        yy,xx=np.mgrid[0:1024,0:1024];xs=-.4+.8*(xx+.5)/1024;ys=.4-.8*(yy+.5)/1024
        field=np.maximum.reduce([np.exp(-(((xs-cx)/rx)**4+((ys-cy)/ry)**4)) for cx,cy,rx,ry in [(-.13,.075,.14,.105),(.155,-.15,.115,.13),(.20,.22,.09,.07)]])
        rough=np.flipud(raw['Roughness'][:,:,0]);nz=ordered_n[:,:,2]
        wet_stats={'pool_pixels':int((field>.85).sum()),'rim_pixels':int(((field>.10)&(field<.35)).sum()),'pool_roughness_mean':float(rough[field>.85].mean()),'rough_rim_roughness_mean':float(rough[(field>.10)&(field<.35)].mean()),'pool_normal_z_mean':float(nz[field>.85].mean()),'rough_rim_normal_z_mean':float(nz[(field>.10)&(field<.35)].mean()),'note':'Includes any visible gravel/leaves/blades overlaying basins; no isolated watervolume or opticalscan claim.'}
    record={'look_id':IDS[kind],'physical_width_cm':80,'physical_height_cm':80,'seed':SEEDS[kind],'blend':beauty['blend'],'beauty':beauty['beauty'],'maps':maps,
            'masters':{k:{'path':(out/'Masters'/(k+'.exr')).relative_to(OUTPUT).as_posix(),'sha256':sha(out/'Masters'/(k+'.exr')),'bit_depth':32,'color_space':'linear'} for k in raw},
            'height':{'z_min_cm':minimum,'z_max_cm':maximum,'range_cm':span,'decode':'height_cm=z_min_cm+sampleHeight16*range_cm','master_precision':'32bitfloat actual topmostGeometry.Position.Z in centimeters','not_albedo_derived':True},
            'calibration':{'expected_world_normal':expected.tolist(),'measured_world_normal':measured.tolist(),'known_linear_color':[.18,.31,.47],'measured_linear_color':color.tolist(),'low_slope_noncliff_normal_height_correlations_RG':corr,'correlation_pixels':int(valid.sum()),'basis':'image-right/world+X,image-up/world+Y; PNG U+X,V-Y; DX=(Nx,-Ny,Nz)*.5+.5. Allmaps identicalorientation; no solitary flips.'},
            'ao':'Actual Cycles rayAO emission16samples,3.5cmradius, no denoising; not placeholderwhite.',
            'roughness':'Authoredgrass/flower/mineralresponse; source leafR and soilR, with mud wetness interpolated from actualgroundvertexattribute. No measuredopticalroughness claim.',
            'mud_wetness_statistics':wet_stats,'counts':beauty['counts'],'source_identity_notes':beauty['source_identity_notes'],'elapsed_bake_seconds':time.time()-started,
            'limits':'Internal source-art review only, not userapproval or UE/POM acceptance. Heightfield records topmostsurfaces; grass/leaves/petals undersides/overhangs lost. No realwatervolume/collision/performance proof.'}
    (out/'BakeReceipt.json').write_text(json.dumps(record,indent=2)+'\n');print('DIORAMA_MAPS_READY',kind,span,flush=True);return record


def bake_all():
    if (OUTPUT/'Manifest.json').exists():raise RuntimeError('Refusing to overwrite finalized rootmanifest')
    inputs=provenance();materials={kind:bake_patch(kind) for kind in IDS}
    directory=OUTPUT/'Provenance';directory.mkdir(exist_ok=True);evidence=[]
    # Copy license/metadata receipts locally; source bytes remain at immutable paths.
    receipt_paths=[path for path in inputs if path.endswith(('.json','.html'))]
    granite=json.loads((SOURCES/'Batch3Textures/Manifest.json').read_text())
    for info in granite.get('licenses',[]):
        if info.get('path'):receipt_paths.append((SOURCES/'Batch3Textures'/info['path']).relative_to(ROOT).as_posix())
    for i,relative in enumerate(receipt_paths):
        original=ROOT/relative;target=directory/(str(i)+'-'+original.name)
        if target.exists():raise RuntimeError('Existing localprovenance receipt')
        target.write_bytes(original.read_bytes());evidence.append({'path':target.relative_to(OUTPUT).as_posix(),'sha256':sha(target),'original_source':relative})
    input_receipt=directory/'SourceInputHashes.json';input_receipt.write_text(json.dumps(inputs,indent=2)+'\n');evidence.append({'path':input_receipt.relative_to(OUTPUT).as_posix(),'sha256':sha(input_receipt)})
    manifest={'schema_version':1,'selected_source_version':VERSION,'generator_source':'Tools/UsfMaterials/author_diorama_patches.py','generator_sha256':sha(Path(__file__)),'helper_source':HELPER.relative_to(ROOT).as_posix(),'helper_sha256':sha(HELPER),'provenance_evidence':evidence,'licensed_source_sha256':inputs,'materials':materials,'render_backend':'Blender4.4 CyclesCPU4threads,1024²; actualgeometry Emission data1sample Raw, noadaptivesampling/no denoise. Beauty16samplesAgX separate. BC alone manuallylinear-to-sRGBencodedPNG8; N/HlinearPNG16, R/AOlinearPNG8.','acceptance':'V2 source direction approved internally; user visual and Unreal/runtime acceptance pending. V1firstbeauties preserved.'}
    (OUTPUT/'Source/author_diorama_patches-final.py').write_bytes(Path(__file__).read_bytes());(OUTPUT/'Manifest.json').write_text(json.dumps(manifest,indent=2,sort_keys=True)+'\n');print('DIORAMAS_COMPLETE',sha(OUTPUT/'Manifest.json'),flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--stage',choices=['beauty','bake','sheets'],default='beauty');parser.add_argument('--kind',choices=['all',*IDS],default='all');args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else None)
    if args.stage=='sheets':return sheets()
    if args.stage=='bake':return bake_all()
    provenance()
    for kind in IDS if args.kind=='all' else [args.kind]:build_patch(kind)


if __name__=='__main__':main()
