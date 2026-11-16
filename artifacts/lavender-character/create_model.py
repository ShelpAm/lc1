"""Standalone reference-inspired character. Run with Blender 4.5 --background --python."""
import bpy, math, json
from pathlib import Path
from mathutils import Vector
from math import sin, cos, pi
OUT = Path(__file__).resolve().parent
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)

def material(name, color, rough=.55, metal=0):
    m=bpy.data.materials.new(name); m.diffuse_color=(*color,1); m.use_nodes=True
    bs=m.node_tree.nodes.get('Principled BSDF'); bs.inputs['Base Color'].default_value=(*color,1); bs.inputs['Roughness'].default_value=rough; bs.inputs['Metallic'].default_value=metal
    return m
skin=material('Porcelain blush',(.92,.64,.60)); white=material('Ivory silk',(.89,.86,.98)); purple=material('Amethyst ribbons',(.19,.065,.32)); hair=material('Lavender hair',(.43,.27,.64),.38); hairlight=material('Lilac strands',(.58,.38,.76),.4); eye_white=material('Eye whites',(.97,.92,.98)); iris=material('Violet iris',(.30,.075,.53),.28); pupil=material('Deep violet pupils',(.035,.012,.065)); shine=material('Catchlights',(1,1,1),.15); blush=material('Cheek blush',(.88,.36,.43)); mouth=material('Rose lips',(.40,.075,.13)); gold=material('Pearl accents',(.93,.78,.48),.3,.3)
parts=[]
def mesh(name, verts, faces, mat):
    me=bpy.data.meshes.new(name); me.from_pydata(verts,[],faces); me.update(); ob=bpy.data.objects.new(name,me); bpy.context.collection.objects.link(ob); ob.data.materials.append(mat)
    for p in me.polygons:p.use_smooth=True
    parts.append(ob); return ob

def ell(name,loc,scale,mat,seg=40,rings=24):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=seg,ring_count=rings,location=loc); ob=bpy.context.object; ob.name=name; ob.scale=scale; bpy.ops.object.transform_apply(location=False,rotation=False,scale=True); ob.data.materials.append(mat)
    for p in ob.data.polygons:p.use_smooth=True
    parts.append(ob); return ob

def tube(name, points, widths, depths, mat, sides=16):
    verts=[]
    for i,p in enumerate(points):
        for j in range(sides):
            a=2*pi*j/sides; verts.append((p[0]+widths[i]*cos(a),p[1]+depths[i]*sin(a),p[2]))
    faces=[]
    for i in range(len(points)-1):
        for j in range(sides): a=i*sides+j; b=i*sides+(j+1)%sides; faces.append((a,b,b+sides,a+sides))
    faces += [tuple(reversed(range(sides))),tuple((len(points)-1)*sides+j for j in range(sides))]
    ob=mesh(name,verts,[tuple(reversed(f)) for f in faces],mat)
    sub=ob.modifiers.new('Smooth silhouette','SUBSURF'); sub.levels=2; sub.render_levels=2
    return ob

def curve(name,pts,radius,mat):
    cu=bpy.data.curves.new(name,'CURVE'); cu.dimensions='3D'; cu.resolution_u=16; cu.bevel_depth=radius; cu.bevel_resolution=3
    sp=cu.splines.new('BEZIER'); sp.bezier_points.add(len(pts)-1)
    for bp,p in zip(sp.bezier_points,pts):bp.co=p;bp.handle_left_type='AUTO';bp.handle_right_type='AUTO'
    ob=bpy.data.objects.new(name,cu);bpy.context.collection.objects.link(ob);ob.data.materials.append(mat);parts.append(ob);return ob

def ribbon(name,c,size):
    x,y,z=c
    for s in (-1,1):
        ob=ell(name+' loop',(x+s*size*.65,y,z+size*.12),(size*.72,size*.18,size*.36),purple,24,12); ob.rotation_euler[1]=s*-.3
        mesh(name+' tail',[(x+s*.015,y-.003,z),(x+s*size*.30,y-.01,z-size*1.35),(x+s*size*.75,y,z-size*1.12),(x+s*size*.35,y,z)],[(0,1,2,3)],purple)
    ell(name+' knot',c,(size*.23,size*.25,size*.25),purple,24,12)

# All lengths are metres. The model is an approximately 1.8 m adult-proportioned stylization.
# Front is -Y, Z is up in Blender. Exporter converts to glTF Y-up.
for s,side in [(-1,'R'),(1,'L')]:
    # Slight inward knee alignment without extreme deformation.
    tube(side+' leg',[(s*.135,.02,.91),(s*.14,.015,.83),(s*.126,.008,.70),(s*.083,0,.54),(s*.075,-.012,.48),(s*.10,.005,.37),(s*.115,.015,.25),(s*.12,.01,.080)], [.092,.091,.075,.052,.049,.054,.041,.029],[.09,.088,.073,.052,.048,.055,.043,.033],skin,32)
    ell(side+' foot',(s*.12,-.057,.061),(.05,.115,.047),skin)
    for j in range(5):
        x=s*(.085+j*.017); ell(side+' toe '+str(j),(x,-.148+j*.004,.041),(.013-j*.001,.032-j*.002,.021-j*.0015),skin,24,14)
    for z in (.13,.155):
        pts=[(s*.12+.033*cos(a),.01+.039*sin(a),z+.005*sin(2*a)) for a in [2*pi*j/24 for j in range(25)]];curve(side+' ankle ribbon',pts,.007,purple)
    ribbon(side+' ankle bow',(s*.16,-.005,.14),.041)
# Torso under dress.
tube('Torso',[(0,.02,.83),(0,.02,.96),(0,.02,1.08),(0,.015,1.20),(0,.02,1.28)],[.17,.13,.105,.16,.155],[.105,.09,.075,.09,.075],skin,40)
ell('Neck',(0,.01,1.33),(.047,.045,.09),skin)
# Bodice with panel seams.
tube('Corseted ivory bodice',[(0,.017,.94),(0,.015,1.015),(0,.01,1.10),(0,.01,1.20),(0,.015,1.255)],[.143,.117,.115,.167,.151],[.105,.088,.086,.105,.087],white,64)
for s in (-1,1):
    curve('Bodice lilac seam',[(s*.09,-.077,.96),(s*.07,-.077,1.055),(s*.09,-.09,1.16),(s*.11,-.064,1.25)],.003,hairlight)
    ell('Puffed off shoulder sleeve',(s*.193,.01,1.232),(.07,.075,.05),white)
    tube('Arm',[(s*.205,.005,1.22),(s*.216,-.025,1.13),(s*.183,-.08,1.04),(s*.125,-.135,.95),(s*.054,-.17,.90)],[.043,.041,.033,.027,.024],[.043,.041,.033,.027,.022],skin,28)
    hand=ell('Hand',(s*.034,-.18,.878),(.029,.022,.047),skin);hand.rotation_euler[1]=s*-.25
    for j in range(4):
        curve('Finger',[(s*(.015+j*.012),-.19,.884),(s*(.010+j*.012),-.196,.854),(s*(.009+j*.012),-.187,.839+j*.002)],.0055,skin)
    ell('Thumb',(s*.061,-.194,.878),(.009,.012,.025),skin,24,12)
# Flared double skirt, corrugated hem geometry with actual fabric thickness.
def skirt(name,top,bottom,r0,r1,mat,ruffle):
    n=144; rows=16; v=[]
    for k in range(rows+1):
        t=k/rows
        for j in range(n):
            a=2*pi*j/n; r=r0+(r1-r0)*t+(.003+.012*t*t)*cos(18*a); z=top+(bottom-top)*t+ruffle*t**5*cos(18*a)
            v.append((r*cos(a),.018+r*.72*sin(a),z))
    f=[]
    for k in range(rows):
        for j in range(n):a=k*n+j;b=k*n+(j+1)%n;f.append((a,b,b+n,a+n))
    ob=mesh(name,v,[tuple(reversed(face)) for face in f],mat); so=ob.modifiers.new('Fabric thickness','SOLIDIFY');so.thickness=.002
    pts=[(v[rows*n+j][0],v[rows*n+j][1],v[rows*n+j][2]+.012) for j in range(n)];pts.append(pts[0]);curve(name+' lilac piping',pts,.004,hairlight)
skirt('Underskirt ruffle',.96,.80,.14,.275,white,.022)
skirt('Main pleated skirt',1.015,.845,.12,.264,white,.017)
for s in (-1,1):
    for i in range(9):
        a=pi*(i/8);ell('Sleeve scallop',(s*(.19+.06*cos(a)),.01+.055*sin(a),1.219),(.018,.02,.018),white,20,12)
ribbon('Chest bow',(0,-.112,1.205),.069)
ell('Amethyst brooch',(0,-.132,1.228),(.016,.009,.019),iris)
# Head and facial surfaces, deliberately sculpted as a stylized blockout.
ell('Head',(0,-.002,1.49),(.122,.097,.151),skin,64,40)
# Chin is part of the head silhouette.
for s in (-1,1):
    ell('Ear',(s*.119,.002,1.474),(.018,.025,.032),skin)
    # Eye discs curve around the face, with raised upper eyeliner.
    e=ell('Eye white',(s*.050,-.086,1.505),(.043,.019,.044),eye_white);e.rotation_euler[2]=s*.18
    ell('Iris',(s*.050,-.103,1.503),(.028,.008,.035),iris)
    ell('Pupil',(s*.050,-.110,1.505),(.012,.003,.023),pupil)
    ell('Large catchlight',(s*.050-.009,-.114,1.520),(.009,.002,.011),shine,24,14)
    ell('Small catchlight',(s*.050+.011,-.114,1.489),(.004,.002,.005),shine,20,12)
    curve('Upper lashes',[(s*.014,-.101,1.523),(s*.040,-.108,1.546),(s*.071,-.10,1.544),(s*.093,-.087,1.531)],.004,pupil)
    curve('Outer eyelash',[(s*.078,-.096,1.54),(s*.105,-.086,1.551)],.003,pupil)
    curve('Brow',[(s*.024,-.087,1.565),(s*.051,-.091,1.574),(s*.079,-.077,1.565)],.003,hair)
    ell('Soft cheek',(s*.077,-.081,1.448),(.018,.001,.005),blush,24,12)
ell('Nose',(0,-.101,1.465),(.011,.014,.016),skin,24,16)
curve('Smile',[(-.025,-.090,1.423),(0,-.099,1.417),(.025,-.090,1.427)],.0025,mouth)
# Hair cap excludes face; individual tapered locks build a layered silhouette.
v=[];f=[];N=96;R=20
for k in range(R+1):
    for j in range(N):
        a=2*pi*j/N; front=max(0,-sin(a)); theta=(.90+(.84*(1-front)))*k/R
        v.append((.129*sin(theta)*cos(a),.008+.105*sin(theta)*sin(a),1.499+.159*cos(theta)))
for k in range(R):
    for j in range(N):a=k*N+j;b=k*N+(j+1)%N;f.append((a,b,b+N,a+N))
mesh('Hair crown',v,[tuple(reversed(face)) for face in f],hair)
for j in range(25):
    a=pi*j/24; x=.106*cos(a); y=.015+.066*sin(a)
    points=[]; widths=[]; depths=[]
    for k in range(17):
        t=k/16;points.append((x*(1+.85*t)+.012*sin(7*t+j), y+.038*sin(pi*t)+.027*t,1.60-.73*t))
        widths.append((.021+.011*sin(pi*t))*max(.025,(1-t)**.35));depths.append(.016*max(.02,(1-t)**.4))
    tube('Back hair lock %02d'%j,points,widths,depths,hairlight if j%4==0 else hair,12)
for s in (-1,1):
    for j in range(4):
        tube('Face framing lock',[(s*(.104+j*.008),-.02,1.61),(s*(.123+j*.01),-.047,1.48),(s*(.133+j*.013),-.045,1.32),(s*(.16+j*.014),-.055,1.13),(s*(.145+j*.025),-.07,1.01)],[.025,.025,.022,.016,.001],[.017,.018,.016,.013,.001],hairlight if j==0 else hair,16)
    ribbon('Hair bow',(s*.134,.005,1.568),.049)
    for j in range(5):
        a=j*2*pi/5;ell('Flower petal',(s*.132+.018*cos(a),-.043,1.571+.018*sin(a)),(.013,.008,.009),white,20,12)
    ell('Flower center',(s*.132,-.051,1.571),(.007,.005,.007),gold,20,12)
for j in range(9):
    x=-.105+j*.025
    tube('Swept fringe',[(x*.65,-.040,1.620),(x,-.086,1.60),(x+.022,-.103,1.55),(x+.028,-.104,1.516+abs(x)*.30)],[.024,.025,.019,.001],[.012,.014,.01,.001],hairlight if j%3==0 else hair,16)
curve('Ahoge',[(0,.012,1.645),(.011,.005,1.696),(.06,-.003,1.68)],.006,hairlight)
# Keep model and presentation objects separate for clean export.
model=bpy.data.collections.new('CHARACTER');bpy.context.scene.collection.children.link(model)
for ob in parts:
    for col in list(ob.users_collection):col.objects.unlink(ob)
    model.objects.link(ob)
# Convert curves at export (Blender glTF ignores unconverted curves).
bpy.ops.object.select_all(action='DESELECT')
for ob in parts:ob.select_set(True)
bpy.context.view_layer.objects.active=parts[0];bpy.ops.object.convert(target='MESH')
# Export only character, with all materials embedded.
bpy.ops.export_scene.gltf(filepath=str(OUT/'lavender-character.glb'),use_selection=True,export_format='GLB',export_apply=True)
bpy.ops.wm.obj_export(filepath=str(OUT/'lavender-character.obj'),export_selected_objects=True,apply_modifiers=True)
# Studio staging.
floor=material('Stage',(.032,.043,.068),.72)
bpy.ops.mesh.primitive_cylinder_add(vertices=96,radius=.54,depth=.045,location=(0,0,-.025));bpy.context.object.data.materials.append(floor)
be=bpy.context.object.modifiers.new('Soft edge','BEVEL');be.width=.025;be.segments=3
world=bpy.context.scene.world;world.use_nodes=True;world.node_tree.nodes['Background'].inputs[0].default_value=(.12,.15,.22,1);world.node_tree.nodes['Background'].inputs[1].default_value=.45

def aim(ob,point):ob.rotation_euler=(Vector(point)-ob.location).to_track_quat('-Z','Y').to_euler()
for name,loc,power,size,color in [('Key',(-3,-4,5),450,4,(.87,.91,1)),('Fill',(3,-2,3),230,3,(1,.80,.87)),('Rim',(1,3,4),550,3,(.65,.60,1))]:
    bpy.ops.object.light_add(type='AREA',location=loc);ob=bpy.context.object;ob.name=name;ob.data.energy=power;ob.data.shape='DISK';ob.data.size=size;ob.data.color=color;aim(ob,(0,0,1))
bpy.ops.object.camera_add(location=(2.2,-4,2.0));cam=bpy.context.object;aim(cam,(0,0,.86));cam.data.type='ORTHO';cam.data.ortho_scale=2.03;bpy.context.scene.camera=cam
scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=32;scene.cycles.use_denoising=True;scene.render.resolution_x=850;scene.render.resolution_y=1050;scene.render.resolution_percentage=100
scene.view_settings.view_transform='AgX';scene.render.image_settings.file_format='PNG'
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'lavender-character.blend'))
for name,loc in [('preview',(2.2,-4,2.0)),('front',(0,-4,1.35)),('back',(0,4,1.35))]:
    cam.location=loc;aim(cam,(0,0,.86));scene.render.filepath=str(OUT/(name+'.png'));bpy.ops.render.render(write_still=True)
print('MODEL_COMPLETE',len(parts))
