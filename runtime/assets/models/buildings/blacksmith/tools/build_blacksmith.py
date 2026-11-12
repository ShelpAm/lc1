"""Assemble the local Modular Village kit; export OBJ/GLB and CPU-render previews."""
from pathlib import Path
from collections import Counter, defaultdict
import json, math, struct
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
KIT = ROOT.parents[2] / 'lc-res/Modular Village'
ROOT.mkdir(parents=True, exist_ok=True)
materials = {}
current = None
for line in (KIT/'Materials_Modular_Village.mtl').read_text().splitlines():
    words = line.split()
    if words and words[0] == 'newmtl': current = words[1]
    if words and words[0] == 'Kd': materials[current] = list(map(float, words[1:4]))
materials.update(ForgeStone=[.25,.27,.28], Iron=[.13,.16,.18], Coal=[.045,.035,.03], Ember=[1,.24,.025], Ground=[.29,.34,.28])
objects = []
counts = Counter()
cache = {}

def load(name):
    if name in cache: return cache[name]
    vertices, faces, mat = [], [], 'Wood_Dark'
    for line in (KIT/(name+'.obj')).read_text().splitlines():
        w = line.split()
        if not w: continue
        if w[0] == 'v': vertices.append([float(w[1]), -float(w[3]), float(w[2])])
        elif w[0] == 'usemtl': mat = w[1]
        elif w[0] == 'f':
            indices = [int(x.split('/')[0])-1 for x in w[1:]]
            for i in range(1,len(indices)-1): faces.append(([indices[0],indices[i],indices[i+1]], mat))
    cache[name] = np.array(vertices), faces
    return cache[name]

def add(name, size, center, angle=0, flip=False):
    v, f = load(name)
    lo, hi = v.min(0), v.max(0)
    v = ((v-(lo+hi)/2)/np.maximum(hi-lo,1e-6))*np.array(size)
    if flip: v[:,0] *= -1
    a=math.radians(angle); r=np.array([[math.cos(a),-math.sin(a),0],[math.sin(a),math.cos(a),0],[0,0,1]])
    v=v@r.T+center
    if flip: f=[([ids[0],ids[2],ids[1]],m) for ids,m in f]
    objects.append((name+'_'+str(counts[name]),v,f)); counts[name]+=1

def box(name, size, center, mat):
    v=np.array([[x,y,z] for x in [-.5,.5] for y in [-.5,.5] for z in [-.5,.5]])*size+center
    quads=[[0,1,3,2],[4,6,7,5],[0,4,5,1],[2,3,7,6],[0,2,6,4],[1,5,7,3]]
    f=[([q[0],q[i],q[i+1]],mat) for q in quads for i in (1,2)]
    objects.append((name,v,f))

# Four by three metre workshop, front toward -Y; open working porch extends two metres.
for x in range(-3,4):
    for y in range(-3,5): add('Stone_Floor_1',(1,1,.16),(x,y,.01))
for x in [-1.5,-.5,.5,1.5]:
    for z in [.55,1.55,2.35]:
        add('Stucco_Block',(1,.20,1 if z<2 else .6),(x,3,z))
for x in [-2,2]:
    for y in [.5,1.5,2.5]:
        add('Stone_Wall_1',(.20,1,.65),(x,y,.4))
        add('Stucco_Window_Single_Wide',(.23,1,1.8),(x,y,1.65),angle=0 if x>0 else 180)
# Doorway and one facade window use the kit's original openings.
add('Stucco_Doorway_Wide_Tall',(.25,2,2.6),(-.9,0,1.35),90)
# Raise the opening and its lower wood trim, keeping the facade top unchanged.
# The lowest original header trim is at source Y=1.35, below the masonry opening.
doorway = objects[-1][1]
doorway[:,2] = np.interp(doorway[:,2], [.05,1.805,2.0,2.065,2.65], [.09,2.19,2.34,2.39,2.65])
add('Wall_Prop_Door_Simple',(.13,.9,2.10),(-.9,-.15,1.14),90)
add('Stucco_Window_Single_Wide',(.25,2,2.6),(1.05,0,1.35),90)
for x in [-2,0,2]:
    for y in [0,3]: add('Wood_Post_Large',(.19,.19,2.7),(x,y,1.4))
for y in [0,3]:
    for z in [.72,2.65]: add('Canopy_Beam',(4.18,.18,.18),(0,y,z))
# Opposing kit roof pitches; repeated along depth, overhanging the walls.
for y in [-.15,.95,2.05,3.15]:
    add('Roof_Straight_Side_Eave',(2.35,1.1,1.55),(1.175,y,3.40))
    add('Roof_Straight_Side_Eave',(2.35,1.1,1.55),(-1.175,y,3.40),flip=True)
for y in [-.1,.9,1.9,2.9]:
    add('Roof_Accent_Ridge',(1,.22,.22),(0,y,4.18),90)
# Gable infill beneath the roof, constructed from a triangular section.
for y in [-.02,3.02]:
    v=np.array([[-2,y,2.65],[2,y,2.65],[0,y,4.1]])
    objects.append(('Gable',v,[([0,1,2],'Stucco'),([2,1,0],'Stucco')]))
# The porch roof uses the same red tile modules and slopes away from the house.
for x in [-1.65,-.55,.55,1.65]:
    add('Roof_Straight_Side_Eave',(2.3,1.1,.60),(x,-1.15,2.84),-90)
for x in [-2.15,2.15]:
    add('Wood_Post_Large',(.19,.19,2.54),(x,-2.15,1.32))
    add('Stucco_Prop_Support_Angled_Small',(.48,.18,.48),(x+(.16 if x<0 else -.16),-2.15,2.37))
add('Canopy_Beam',(4.5,.19,.20),(0,-2.15,2.49))
# Stone forge and chimney; the supplied kit has no dedicated forge or anvil.
box('Forge_base',(1.05,.86,.66),(1.35,-.65,.43),'ForgeStone')
box('Forge_hearth',(1.14,.95,.12),(1.35,-.65,.82),'Stone_Brit_Gray')
box('Coal_bed',(.68,.57,.055),(1.35,-.65,.91),'Coal')
for x,y in [(1.15,-.65),(1.43,-.71),(1.6,-.52),(1.35,-.42)]:
    box('Hot_coal',(.15,.12,.045),(x,y,.952),'Ember')
for x in [.86,1.84]: box('Forge_side',(.13,.85,.34),(x,-.62,1.03),'ForgeStone')
box('Forge_back',(1.1,.16,.92),(1.35,-.14,1.26),'ForgeStone')
box('Smoke_hood',(1.15,.65,.28),(1.35,-.42,1.80),'Iron')
add('Roof_Prop_Chimney_Stone',(.55,.55,2.5),(1.35,-.20,3.05))
add('Prop_Crate_1',(.57,.57,.43),(-.25,-1.45,.305))
box('Anvil_foot',(.48,.36,.09),(-.25,-1.45,.58),'Iron')
box('Anvil_waist',(.24,.24,.24),(-.25,-1.45,.74),'Iron')
box('Anvil_face',(.69,.30,.13),(-.25,-1.45,.925),'Iron')
v=np.array([[.095,-1.60,1.03],[.095,-1.30,1.03],[.095,-1.60,1.16],[.095,-1.30,1.16],[.51,-1.45,1.105]])
v[:,2] -= .17
objects.append(('Anvil_horn',v,[([0,1,4],'Iron'),([2,4,3],'Iron'),([0,4,2],'Iron'),([1,3,4],'Iron')]))
add('Prop_Barrel_1',(.65,.65,.76),(2.65,-.65,.47))
add('Prop_Crate_1',(.65,.65,.65),(-2.7,.25,.42))
add('Prop_Crate_1',(.48,.48,.48),(-2.7,.25,.985),12)
add('Prop_Broom',(.22,.25,1.35),(-1.72,-.40,.79))
add('Wall_Prop_Sign_1',(1.05,.15,.65),(-2.3,-.4,2.45),90)
add('Wall_Prop_Lamp',(.27,.25,.50),(-1.65,-.28,1.90),90)
# Workbench made from kit beams and posts.
add('Canopy_Beam',(1.0,.45,.12),(-1.2,-.70,.96))
for x in [-1.57,-.83]: add('Wood_Post_Large',(.13,.30,.76),(x,-.7,.51))
box('Hammer_head',(.22,.12,.12),(-1.2,-.7,1.09),'Iron')
box('Hammer_handle',(.06,.37,.055),(-1.2,-.87,1.06),'Wood_Dark')

def export():
    (ROOT/'blacksmith.mtl').write_text('\n'.join(f'newmtl {m}\nKd {c[0]} {c[1]} {c[2]}\nNs 10\n' for m,c in materials.items()))
    lines=['# LC1 blacksmith; metres, Z up; assembled from local Modular Village kit','mtllib blacksmith.mtl']; offset=1
    for name,v,f in objects:
        lines.append('o '+name)
        lines.extend('v %.6f %.6f %.6f'%tuple(p) for p in v)
        for ids,mat in f: lines.extend(['usemtl '+mat,'f '+' '.join(str(i+offset) for i in ids)])
        offset+=len(v)
    (ROOT/'blacksmith.obj').write_text('\n'.join(lines))
    # GLB uses glTF Y up; material primitives retain the supplied palette.
    groups=defaultdict(list)
    for name,v,faces in objects:
        for ids,m in faces:
            p=v[ids][:,[0,2,1]].copy(); p[:,2]*=-1
            groups[m].append(p)
    blob=bytearray(); views=[]; access=[]; primitives=[]; used=list(groups)
    def buffer(data,typ,count,bounds=None):
        while len(blob)%4: blob.append(0)
        off=len(blob); blob.extend(data)
        views.append(dict(buffer=0,byteOffset=off,byteLength=len(data)))
        a=dict(bufferView=len(views)-1,componentType=5126,count=count,type=typ)
        if bounds: a.update(min=bounds[0],max=bounds[1])
        access.append(a); return len(access)-1
    for m,tri in groups.items():
        tri=np.array(tri,dtype=np.float32); n=np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]); n/=np.maximum(np.linalg.norm(n,axis=1,keepdims=True),1e-9)
        p=tri.reshape(-1,3).astype('<f4'); normals=np.repeat(n,3,axis=0).astype('<f4')
        pi=buffer(p.tobytes(),'VEC3',len(p),(p.min(0).tolist(),p.max(0).tolist())); ni=buffer(normals.tobytes(),'VEC3',len(p))
        primitives.append(dict(attributes=dict(POSITION=pi,NORMAL=ni),material=used.index(m)))
    mats=[]
    for m in used:
        entry=dict(name=m,doubleSided=True,pbrMetallicRoughness=dict(baseColorFactor=materials.get(m,[.5]*3)+[1],metallicFactor=.65 if m=='Iron' else 0,roughnessFactor=.85))
        if m=='Ember': entry['emissiveFactor']=[1,.16,.01]
        mats.append(entry)
    doc=dict(asset=dict(version='2.0',generator='LC1 Modular Village blacksmith assembler'),scene=0,scenes=[dict(nodes=[0])],nodes=[dict(mesh=0,name='Blacksmith')],meshes=[dict(primitives=primitives)],materials=mats,buffers=[dict(byteLength=len(blob))],bufferViews=views,accessors=access)
    raw=json.dumps(doc,separators=(',',':')).encode(); raw+=b' '*((-len(raw))%4); blob+=b'\0'*((-len(blob))%4)
    glb=struct.pack('<4sII',b'glTF',2,12+8+len(raw)+8+len(blob))+struct.pack('<I4s',len(raw),b'JSON')+raw+struct.pack('<I4s',len(blob),b'BIN\0')+blob
    (ROOT/'blacksmith.glb').write_bytes(glb)
    (ROOT/'assembly.json').write_text(json.dumps(dict(units='metres',obj_up='Z',glb_up='Y',kit='lc-res/Modular Village',kit_instances=dict(counts),objects=len(objects),triangles=sum(len(f) for _,v,f in objects)),indent=2))

def render(filename, eye, width=1400,height=1200):
    # Orthographic z-buffer rasterization of the exported geometry, no invented image.
    target=np.array([0,.65,1.65]); direction=target-np.array(eye); direction/=np.linalg.norm(direction)
    right=np.cross(direction,[0,0,1]); right/=np.linalg.norm(right); up=np.cross(right,direction)
    scale=width/10.8
    rgb=np.zeros((height,width,3),dtype=np.uint8); rgb[:]=[228,230,222]
    depth=np.full((height,width),np.inf)
    light=np.array([-3,-4,8.]); light/=np.linalg.norm(light)
    all_objects=objects+[('Ground',np.array([[-200,-200,-.1],[200,-200,-.1],[200,200,-.1],[-200,200,-.1]]),[([0,1,2],'Ground'),([0,2,3],'Ground')])]
    for _,vertices,faces in all_objects:
        centered=vertices-target; camera=np.column_stack((centered@right,centered@up,centered@direction))
        projected=np.column_stack((camera[:,0]*scale+width/2,-camera[:,1]*scale+height/2,camera[:,2]))
        for ids,mat in faces:
            p=projected[ids]; a,b,c=p
            xmin=max(0,int(np.floor(p[:,0].min()))); xmax=min(width-1,int(np.ceil(p[:,0].max())))
            ymin=max(0,int(np.floor(p[:,1].min()))); ymax=min(height-1,int(np.ceil(p[:,1].max())))
            if xmin>xmax or ymin>ymax: continue
            denominator=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
            if abs(denominator)<1e-7: continue
            yy,xx=np.mgrid[ymin:ymax+1,xmin:xmax+1]; xx=xx+.5; yy=yy+.5
            w0=((b[1]-c[1])*(xx-c[0])+(c[0]-b[0])*(yy-c[1]))/denominator
            w1=((c[1]-a[1])*(xx-c[0])+(a[0]-c[0])*(yy-c[1]))/denominator; w2=1-w0-w1
            z=w0*a[2]+w1*b[2]+w2*c[2]; sub=depth[ymin:ymax+1,xmin:xmax+1]
            mask=(w0>=-1e-5)&(w1>=-1e-5)&(w2>=-1e-5)&(z<sub)
            if not mask.any(): continue
            tri=vertices[ids]; normal=np.cross(tri[1]-tri[0],tri[2]-tri[0]); normal/=max(np.linalg.norm(normal),1e-9)
            shade=.54+.46*abs(float(normal@light))
            color=np.array(materials.get(mat,[.5]*3)); color=np.clip((color*shade)**(1/1.65)*255,0,255).astype(np.uint8)
            rgb[ymin:ymax+1,xmin:xmax+1][mask]=color; sub[mask]=z[mask]
    image=Image.fromarray(rgb)
    draw=ImageDraw.Draw(image)
    fontpath='C:/Windows/Fonts/arial.ttf'
    title=ImageFont.truetype(fontpath,34); small=ImageFont.truetype(fontpath,18)
    draw.text((48,36),'BLACKSMITH / MODULAR VILLAGE',fill=(35,45,40),font=title)
    draw.text((50,82),'Kit assembly study  |  4 x 3 m house + 2 m working porch',fill=(65,75,65),font=small)
    draw.text((50,height-50),'Existing kit: walls, roof, posts, chimney, floor, barrels and crates. Added: forge, anvil and hammer.',fill=(225,230,219),font=small)
    image.save(ROOT/filename)

if __name__=='__main__':
    export()
    render('preview.png',(9,-12,8))
    render('preview_front.png',(1,-14,6))
    render('preview_rear.png',(-10,12,8))
    print(json.dumps(dict(objects=len(objects),kit_instances=sum(counts.values()),triangles=sum(len(f) for _,v,f in objects),output=str(ROOT))))
