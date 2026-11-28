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
        return .018+relief-(.012*wetness(x,y) if kind=='mud' else 0)
    w.ground_z=height
    mat=w.material('Soil_Diorama_'+kind,(.07,.04,.02),.84)
    nodes=mat.node_tree.nodes;links=mat.node_tree.links
    for node in nodes:
        if node.bl_idname=='ShaderNodeTexImage' and node.image and 'BaseColor.png' in node.image.filepath:
            node.image=bpy.data.images.load(str(SOURCES/'WoodlandSourcesV1/gravelly_sand_diff_4k.jpg'),check_existing=True);node.image.colorspace_settings.name='sRGB'
    color=nodes['StructuralColor'];color.inputs[2].default_value={'grass':(.20,.23,.16,1),'dirt':(.34,.29,.22,1),'mud':(.16,.13,.10,1)}[kind]
    if kind=='mud':
        attr=nodes.new('ShaderNodeAttribute');attr.attribute_name='Wetness'
        rough=nodes['StructuralRoughness'];rough.name='UnusedOriginalRoughness'
        mix=nodes.new('ShaderNodeMapRange');mix.name='StructuralRoughness';mix.inputs['To Min'].default_value=.82;mix.inputs['To Max'].default_value=.10
        links.new(attr.outputs['Fac'],mix.inputs['Value']);links.new(mix.outputs[0],nodes['Beauty'].inputs['Roughness'])
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
    vertices=[];faces=[];indices=[];uvs=[];clumps=950 if kind=='grass' else 110 if kind=='dirt' else 65
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
            a=angle+j*2.399+rng.uniform(-.3,.3);length=rng.uniform(.035,.09);width=rng.uniform(.0017,.0032)
            rise=rng.uniform(.013,.039) if kind=='grass' else rng.uniform(.009,.024)
            px=x+rng.uniform(-.005,.005);py=y+rng.uniform(-.005,.005);z=w.ground_z(px,py)+.0007
            start=len(vertices);mat_index=rng.randrange(3)
            for k in range(9):
                t=k/8;lean=length*(t**1.3);taper=(1-t)**.8
                for side in [-1,0,1]:
                    lateral=side*width*.5*taper
                    vertices.append((px+math.cos(a)*lean-math.sin(a)*lateral,py+math.sin(a)*lean+math.cos(a)*lateral,z+rise*math.sin(t*math.pi*.68)+.0004*(1-abs(side))*math.sin(t*math.pi)))
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
    for i,tint in enumerate([(.19,.21,.22,1),(.29,.25,.20,1),(.13,.15,.16,1)]):
        mat=w.material('EmbeddedMineral_'+str(i),(.1,.1,.1),.78);nodes=mat.node_tree.nodes;links=mat.node_tree.links
        geo=nodes.new('ShaderNodeNewGeometry');scale=nodes.new('ShaderNodeVectorMath');scale.operation='SCALE';scale.inputs[3].default_value=5;links.new(geo.outputs['Position'],scale.inputs[0])
        tex=nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(str(SOURCES/'Batch3Textures/granite/Maps/BaseColor.png'),check_existing=True);tex.image.colorspace_settings.name='sRGB';tex.projection='BOX';tex.projection_blend=.25;links.new(scale.outputs[0],tex.inputs[0])
        gray=nodes.new('ShaderNodeRGBToBW');links.new(tex.outputs[0],gray.inputs[0]);color=nodes.new('ShaderNodeMixRGB');color.name='StructuralColor';color.blend_type='MULTIPLY';color.inputs[0].default_value=1;color.inputs[2].default_value=tint;links.new(gray.outputs[0],color.inputs[1]);links.new(color.outputs[0],nodes['Beauty'].inputs['Base Color']);mats.append(mat)
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2,radius=1);temporary=bpy.context.object
    template=[v.co.copy() for v in temporary.data.vertices];triangles=[tuple(p.vertices) for p in temporary.data.polygons];bpy.data.objects.remove(temporary,do_unlink=True)
    vertices=[];faces=[];indices=[];count=180 if kind=='grass' else 390 if kind=='dirt' else 260
    for i in range(count):
        x,y=rng.uniform(-.38,.38),rng.uniform(-.38,.38)
        size=rng.uniform(.002,.013)*(1.4 if i%17==0 else 1);a=rng.uniform(0,math.tau)
        sx,sy,sz=size*rng.uniform(.8,1.25),size*rng.uniform(.7,1.15),size*rng.uniform(.35,.65)
        center=w.ground_z(x,y)-sz*.40;start=len(vertices);mi=rng.randrange(3)
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
        w.flower('SmallFlower_'+str(i),x,y,rng.uniform(.0045,.0065))
    counts['flowers']=flowers
    twig=w.material('SmallTwigBark',(.033,.017,.007),.92)
    for i in range({'grass':4,'dirt':9,'mud':6}[kind]):
        x,y=rng.uniform(-.29,.29),rng.uniform(-.29,.29);a=rng.uniform(0,math.tau);length=rng.uniform(.035,.085)
        w.curve('ShortTwig_'+str(i),[(x+math.cos(a)*length*t,y+math.sin(a)*length*t,w.ground_z(x+math.cos(a)*length*t,y+math.sin(a)*length*t)+.0015+.001*math.sin(t*math.pi)) for t in [0,.2,.4,.6,.8,1]],.001,twig)
    w.camera('Bake_Ortho_TopDown',(0,0,1.5),(0,0,0),.8);beauty=w.camera('Beauty_Orthographic',(0,-.62,1.3),(0,0,.02),.94);scene.camera=beauty
    world=bpy.data.worlds.new('NeutralDioramaWorld');world.use_nodes=True;world.node_tree.nodes['Background'].inputs[0].default_value=(.30,.36,.44,1);world.node_tree.nodes['Background'].inputs[1].default_value=.24;scene.world=world
    light=bpy.data.lights.new('SoftDaylightKey','AREA');light.energy=28;light.shape='DISK';light.size=.65;obj=bpy.data.objects.new('SoftDaylightKey',light);scene.collection.objects.link(obj);obj.location=(-.33,-.21,1);obj.rotation_euler=(Vector((0,0,0))-obj.location).to_track_quat('-Z','Y').to_euler()
    scene.view_settings.view_transform='AgX';scene.view_settings.exposure=0;scene.view_settings.gamma=1
    output=OUTPUT/kind;output.mkdir(parents=True,exist_ok=True);(output/'Proof').mkdir(exist_ok=True)
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
        path=OUTPUT/kind/'Proof/Beauty-Lit-Blender.png';im=Image.open(path).convert('RGB');sheet.paste(im.resize((512,512)),(i*512,24));draw.text((i*512+10,6),kind+' / native Blender beauty',fill='white')
        sheet.paste(im.crop((256,256,768,768)).resize((512,512)),(i*512,544));draw.text((i*512+10,526),'2x crop / same pixels, no grading',fill='white')
    sheet.save(OUTPUT/'BeautyContactSheet.png');print(OUTPUT/'BeautyContactSheet.png')


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--stage',choices=['beauty','bake','sheets'],default='beauty');parser.add_argument('--kind',choices=['all',*IDS],default='all');args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else None)
    if args.stage=='sheets':return sheets()
    if args.stage=='bake':raise RuntimeError('Data bake awaits first source-art review; initialbeautyonly')
    provenance()
    for kind in IDS if args.kind=='all' else [args.kind]:build_patch(kind)


if __name__=='__main__':main()
