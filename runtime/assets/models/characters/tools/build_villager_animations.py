"""Build Idle/Walk/Sprint on the existing medieval villager; no engine edits.
Blender --background --factory-startup --python this_file.py -- [draft|render]
"""
import sys, math, json, runpy
from pathlib import Path
import bpy
from mathutils import Vector, Matrix, Quaternion

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(Path(__file__).resolve().parent))
from gltf_asset import finish_glb
BASE=ROOT/'source/villager_animation_base.blend'
bpy.ops.wm.open_mainfile(filepath=str(BASE))
scene=bpy.context.scene; rig=bpy.data.objects['villager_Rig']
bpy.context.preferences.filepaths.save_version=0
FPS=30
RUN_DRIVE_PHASE=.65
meshes=[o for o in scene.objects if o.type=='MESH' and o.parent==rig and 'FullAnatomy_SOURCE' not in o.name]
rest={b.name:b.matrix_local.copy() for b in rig.data.bones}
for folder in ['runtime/static','source','previews','validation','cache/render_cache']:
    (ROOT/folder).mkdir(parents=True,exist_ok=True)
args=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []

def update(): bpy.context.view_layer.update()
def smooth(t): return t*t*(3-2*t)
def orient(name,head,direction):
    original=rest[name].to_3x3().col[1].normalized()
    rotation=original.rotation_difference(Vector(direction).normalized()).to_matrix()
    matrix=(rotation@rest[name].to_3x3()).to_4x4(); matrix.translation=head
    rig.pose.bones[name].matrix=matrix

def solve_limb(upper,lower,start,end,pole):
    a=rig.data.bones[upper].length; b=rig.data.bones[lower].length
    axis=end-start; distance=axis.length
    assert distance<a+b-1e-5,(upper,distance,a+b)
    axis.normalize(); pole=Vector(pole)
    bend=(pole-axis*pole.dot(axis)).normalized()
    along=(a*a-b*b+distance*distance)/(2*distance)
    middle=start+axis*along+bend*math.sqrt(max(0,a*a-along*along))
    orient(upper,start,middle-start); update()
    orient(lower,middle,end-middle); update()

def bone_rotation(name,rotation):
    pb=rig.pose.bones[name]
    matrix=(rotation@rest[name].to_3x3()).to_4x4()
    matrix.translation=pb.matrix.translation
    pb.matrix=matrix; update()

def upper_body(phase,lean=0,breath=0,running=False):
    # Distribute thoracic extension; do not compensate rounded shoulders merely
    # by tilting the head. Sprint's uniform hip lean retains an extended back.
    twist_phase=phase+RUN_DRIVE_PHASE if running else phase-.12
    for name,extension,yaw in [('spine_01',-.012,-.009),('spine_02',-.035,-.018),('spine_03',-.065,-.027)]:
        if running: yaw={'spine_01':-.012,'spine_02':.025,'spine_03':.060}[name]
        bone_rotation(name,Matrix.Rotation(yaw*math.cos(twist_phase),3,'Z')@Matrix.Rotation(lean+extension+breath,3,'X'))
    bone_rotation('neck_01',Matrix.Rotation(-.10+lean*.35,3,'X'))
    bone_rotation('head',Matrix.Rotation(.005*math.sin(phase-.2),3,'Z'))
    for side,sign in [('l',1),('r',-1)]:
        pb=rig.pose.bones['clavicle_'+side]
        shoulder_drive=.006*sign*math.cos(twist_phase) if running else 0
        direction=(pb.tail-pb.head)+Vector((0,.008+shoulder_drive,0))
        orient(pb.name,pb.head.copy(),direction); update()

# Derive the anatomical palm side; mirrored finger bones do not share a local
# curl axis. A fixed positive local-X bend was turning the hands into claws.
palm_normals={}
for side,sign in [('l',1),('r',-1)]:
    across=rest['pinky_01_'+side].translation-rest['index_01_'+side].translation
    forward=rest['middle_01_'+side].translation-rest['hand_'+side].translation
    palm_normals[side]=across.cross(forward).normalized()*sign

def hand_pose(side,wrist,direction):
    source_y=rest['hand_'+side].to_3x3().col[1].normalized()
    source_z=(palm_normals[side]-source_y*palm_normals[side].dot(source_y)).normalized()
    source_x=source_y.cross(source_z).normalized()
    target_y=Vector(direction).normalized()
    inward=Vector((-1 if side=='l' else 1,0,0))
    target_z=(inward-target_y*inward.dot(target_y)).normalized()
    target_x=target_y.cross(target_z).normalized()
    source=Matrix((source_x,source_y,source_z)).transposed()
    target=Matrix((target_x,target_y,target_z)).transposed()
    m=(target@source.transposed()@rest['hand_'+side].to_3x3()).to_4x4()
    m.translation=wrist
    rig.pose.bones['hand_'+side].matrix=m

def fingers(side,curl,phase):
    for pb in rig.pose.bones:
        if not pb.name.endswith('_'+side): continue
        if any(pb.name.startswith(p) for p in ['index_','middle_','ring_','pinky_']):
            flex=curl*(1 if '_01_' in pb.name else 1.4 if '_02_' in pb.name else .8)+.008*math.cos(phase)
            direction=rest[pb.name].to_3x3().col[1].normalized()
            axis=rest[pb.name].to_3x3().transposed()@direction.cross(palm_normals[side]).normalized()
            pb.rotation_quaternion=Quaternion(axis,flex)
        elif pb.name.startswith('thumb_'):
            direction=rest[pb.name].to_3x3().col[1].normalized()
            if '_01_' in pb.name:
                toward_index=(rest['index_01_'+side].translation-rest[pb.name].translation).normalized()
                axis=rest[pb.name].to_3x3().transposed()@direction.cross(toward_index).normalized()
                angle=curl*.72
            else:
                axis=rest[pb.name].to_3x3().transposed()@direction.cross(palm_normals[side]).normalized()
                angle=curl*.20
            pb.rotation_quaternion=Quaternion(axis,angle)

def relaxed_arms(phase,amplitude=.095):
    for side,offset,sign in [('l',0,1),('r',math.pi,-1)]:
        shoulder=rig.pose.bones['upperarm_'+side].matrix.translation.copy()
        p=phase+offset-.18
        wrist=shoulder+Vector((sign*.078,amplitude*math.cos(p)-.012,-.470+.004*math.cos(2*p)))
        solve_limb('upperarm_'+side,'lowerarm_'+side,shoulder,wrist,(0,1,0))
        hand_pose(side,wrist,(sign*.025,-.07+.05*math.cos(p-.18),-1))
        fingers(side,.16,p)
    update()

def snapshot(): return {pb.name:pb.matrix_basis.copy() for pb in rig.pose.bones}
def write_action(name,poses):
    action=bpy.data.actions.new(name); action.use_fake_user=True
    rig.animation_data.action=action
    for frame,pose in enumerate(poses,1):
        for pb in rig.pose.bones:
            pb.rotation_mode='QUATERNION'; pb.matrix_basis=pose[pb.name]
            pb.keyframe_insert(data_path='location',frame=frame,group=pb.name)
            pb.keyframe_insert(data_path='rotation_quaternion',frame=frame,group=pb.name)
    for layer in action.layers:
        for strip in layer.strips:
            for bag in strip.channelbags:
                for fc in bag.fcurves:
                    for key in fc.keyframe_points: key.interpolation='LINEAR'
                    fc.modifiers.new('CYCLES')
    return action

# Fix Walk's posture while preserving its flat-ground leg motion exactly.
old_walk=bpy.data.actions['Walk']; rig.animation_data.action=old_walk
walk_poses=[]; head_changes=[]
for frame in range(1,38):
    scene.frame_set(frame); update()
    before=rig.pose.bones['head'].matrix.translation.copy()
    phase=math.tau*(frame-1)/36
    upper_body(phase); relaxed_arms(phase)
    head_changes.append(rig.pose.bones['head'].matrix.translation.y-before.y)
    walk_poses.append(snapshot())
rig.animation_data.action=None
for action in list(bpy.data.actions): bpy.data.actions.remove(action)
actions={'Walk':write_action('Walk',walk_poses)}
rig.animation_data.action=None

shoe_data={}
for side in ['l','r']:
    points=[]; weights=[]
    for ob in meshes:
        if ob.name not in ['Turnshoe_'+side,'Leather_sole_'+side]: continue
        for v in ob.data.vertices:
            points.append(ob.matrix_world@v.co-rest['foot_'+side].translation)
            weights.append(sum(g.weight for g in v.groups if ob.vertex_groups[g.group].name=='ball_'+side))
    shoe_data[side]=(points,weights)

def foot_pose(side,phase,mode):
    sign=1 if side=='l' else -1
    yaw=Matrix.Rotation(sign*math.radians(3),3,'Z')
    stance=.24; speed=4.5; period=20/FPS; travel=speed*period*stance
    lift=0.; pitch=0.; toe_pitch=0.; correction=Vector()
    if mode=='Idle':
        y=rest['foot_'+side].translation.y
    elif phase<stance:
        y=rest['foot_'+side].translation.y-.34+travel*phase/stance
        pitch=math.radians(-6)*(1-smooth(min(1,phase/.055)))
        if phase>stance-.15: pitch=math.radians(40)*smooth((phase-(stance-.15))/.15)
        toe_pitch=min(0,pitch)
    else:
        s=(phase-stance)/(1-stance); start=-.34+travel; end=-.34
        # Short Hermite sections bound the recovery overshoot while preserving
        # ground-speed tangents at toe-off and the next contact.
        tangent=travel*(1-stance)/stance
        keys=[(0,start,tangent),(.24,start+.09,0),(.35,start+.07,-.30),(.86,end-.035,0),(1,end,tangent)]
        for (a,ya,va),(b,yb,vb) in zip(keys,keys[1:]):
            if a<=s<=b:
                t=(s-a)/(b-a); h=b-a
                y=(2*t**3-3*t*t+1)*ya+(t**3-2*t*t+t)*h*va+(-2*t**3+3*t*t)*yb+(t**3-t*t)*h*vb
                break
        y+=rest['foot_'+side].translation.y
        # Low recovery arc, then lower the foot early enough for the shin to
        # unfold before contact instead of carrying a bent knee into landing.
        descent=smooth(max(0,min(1,(s-.35)/.65)))
        lift=.08*math.sin(math.pi*s)**1.5*(1-.65*descent)
        pitch=math.radians(40-46*smooth(s))
        toe_pitch=pitch-math.radians(40)*(1-smooth(min(1,s/.18)))
    rotation=yaw@Matrix.Rotation(pitch,3,'X')
    toe_rotation=yaw@Matrix.Rotation(toe_pitch,3,'X')
    ball=rest['ball_'+side].translation-rest['foot_'+side].translation
    heel=Vector((0,.045,-.046))
    if mode!='Idle':
        if phase<stance:
            pivot=heel if pitch<0 else ball
            correction=yaw@pivot-rotation@pivot
        else:
            s=(phase-stance)/(1-stance)
            correction=(yaw@ball-(yaw@Matrix.Rotation(math.radians(40),3,'X'))@ball).lerp(yaw@heel-(yaw@Matrix.Rotation(math.radians(-6),3,'X'))@heel,smooth(s))
    points,weights=shoe_data[side]
    shaped=[(rotation@v)*(1-w)+(rotation@ball+toe_rotation@(v-ball))*w for v,w in zip(points,weights)]
    ankle=Vector((sign*(.145 if mode=='Idle' else .135)+correction.x,y+correction.y,-min(v.z for v in shaped)+.002+lift))
    return ankle,rotation,toe_rotation,lift,phase<stance

def place_feet(targets):
    for side,(ankle,rotation,toe_rotation,_,_) in targets.items():
        hip=rig.pose.bones['thigh_'+side].matrix.translation.copy()
        solve_limb('thigh_'+side,'calf_'+side,hip,ankle,(0,-1,0))
        m=(rotation@rest['foot_'+side].to_3x3()).to_4x4(); m.translation=ankle
        rig.pose.bones['foot_'+side].matrix=m; update()
        m=(toe_rotation@rest['ball_'+side].to_3x3()).to_4x4()
        m.translation=ankle+rotation@(rest['ball_'+side].translation-rest['foot_'+side].translation)
        rig.pose.bones['ball_'+side].matrix=m; update()

def sprint_body(u,targets):
    phase=math.tau*u
    delta=Vector((.007*math.sin(phase),-.10,-.045+.012*math.cos(2*phase+.75)))
    rot=Matrix.Rotation(-.045*math.cos(phase+RUN_DRIVE_PHASE),3,'Z')@Matrix.Rotation(.12,3,'X')
    for side,(ankle,_,_,_,planted) in targets.items():
        if not planted: continue
        hip=rest['pelvis'].translation+rot@(rest['thigh_'+side].translation-rest['pelvis'].translation)
        hip.x+=delta.x; hip.y+=delta.y
        length=(rig.data.bones['thigh_'+side].length+rig.data.bones['calf_'+side].length)*.995
        limit=ankle.z+math.sqrt(max(0,length*length-(ankle.x-hip.x)**2-(ankle.y-hip.y)**2))-hip.z
        delta.z=min(delta.z,limit)-.002*math.log1p(math.exp(-abs(delta.z-limit)/.002))
    return delta,rot

# Fit one periodic body trajectory before posing the limbs. Per-frame reach
# clamping used to produce a visible upward pop immediately after contact.
sprint_caps=[]
for frame in range(20):
    u=frame/20
    targets={side:foot_pose(side,(u+offset)%1,'Sprint') for side,offset in [('l',0),('r',.5)]}
    sprint_caps.append(sprint_body(u,targets)[0].z)
sprint_heights=sprint_caps[:]
sprint_heights=[sum(sprint_heights[(i+k)%20]*w for k,w in [(-1,1),(0,2),(1,1)])/4 for i in range(20)]
sprint_heights=[min(h,cap)-.001 for h,cap in zip(sprint_heights,sprint_caps)]

for mode,cycle in [('Idle',120),('Sprint',20)]:
    poses=[]
    rig.animation_data.action=None
    for frame in range(cycle+1):
        for pb in rig.pose.bones: pb.matrix_basis=Matrix.Identity(4)
        u=frame/cycle; phase=math.tau*u
        targets={side:foot_pose(side,(u+offset)%1,mode) for side,offset in [('l',0),('r',.5)]}
        if mode=='Idle':
            delta=Vector((.004*math.sin(phase),0,.001+.001*math.sin(phase)))
            rot=Matrix.Rotation(.006*math.sin(phase),3,'Y')
        else:
            delta,rot=sprint_body(u,targets)
            delta.z=sprint_heights[frame%cycle]
        m=(rot@rest['pelvis'].to_3x3()).to_4x4(); m.translation=rest['pelvis'].translation+delta
        rig.pose.bones['pelvis'].matrix=m; update()
        upper_body(phase,lean=.25 if mode=='Sprint' else 0,breath=.004*math.sin(phase) if mode=='Idle' else 0,running=mode=='Sprint')
        if mode=='Sprint':
            for side,offset in [('l',0),('r',.5)]:
                ankle,rotation,toe_rotation,lift,planted=targets[side]
                if planted: continue
                hip=rig.pose.bones['thigh_'+side].head
                reach=(rig.data.bones['thigh_'+side].length+rig.data.bones['calf_'+side].length)*.994
                horizontal=(ankle.x-hip.x)**2+(ankle.y-hip.y)**2
                extension_z=hip.z-math.sqrt(max(0,reach*reach-horizontal))
                p=(u+offset)%1
                weight=smooth(max(0,min(1,(p-.24)/.10)))*(1-smooth(max(0,min(1,(p-.54)/.14))))
                floor_z=ankle.z-lift
                ankle=ankle.copy(); ankle.z=max(floor_z,extension_z,ankle.z+weight*(extension_z-ankle.z))
                targets[side]=(ankle,rotation,toe_rotation,ankle.z-floor_z,False)
        place_feet(targets)
        if mode=='Idle': relaxed_arms(phase,amplitude=.002)
        else:
            for side,sign,offset in [('l',1,0),('r',-1,math.pi)]:
                rhythm=math.cos(phase+offset+RUN_DRIVE_PHASE)
                # Keep elbows close to the ribs and use a compact shoulder arc.
                # Stabilize the forearm direction rather than swinging it with
                # the upper arm; elbow flex absorbs most of the shoulder motion.
                a=.24*rhythm+.35
                chest_twist=Matrix.Rotation(.036*math.cos(phase+RUN_DRIVE_PHASE),3,'Z')
                swing=chest_twist@Matrix.Rotation(a,3,'X')
                shoulder=rig.pose.bones['upperarm_'+side].matrix.translation.copy()
                upper=(swing@Vector((sign*.035,0,-1))).normalized()
                elbow=shoulder+upper*rig.data.bones['upperarm_'+side].length
                forearm_angle=-1.72+.045*math.cos(phase+offset+RUN_DRIVE_PHASE-.18)
                lower=(chest_twist@Matrix.Rotation(forearm_angle,3,'X')@Vector((sign*.015,0,-1))).normalized()
                wrist=elbow+lower*rig.data.bones['lowerarm_'+side].length
                orient('upperarm_'+side,shoulder,upper); update()
                orient('lowerarm_'+side,elbow,lower); update()
                # The accepted preview lowers each fist and draws it back to
                # the lower ribs. Re-solve the bent elbow around that target.
                wrist+=Vector((-sign*.015,.095,-.055))
                solve_limb('upperarm_'+side,'lowerarm_'+side,shoulder,wrist,(sign*.06,1,-.25))
                lower=(rig.pose.bones['lowerarm_'+side].tail-rig.pose.bones['lowerarm_'+side].head).normalized()
                hand_pose(side,wrist,lower)
                fingers(side,.75,phase+offset)
            update()
        poses.append(snapshot())
    actions[mode]=write_action(mode,poses)
    rig.animation_data.action=None

clip_info={'Idle':{'frames':120,'duration_s':4.,'speed_mps':0.,'loop':True},'Walk':{'frames':36,'duration_s':1.2,'speed_mps':7/12,'loop':True},'Sprint':{'frames':20,'duration_s':20/30,'speed_mps':4.5,'loop':True}}
validation={'posture_head_retraction_m':sum(head_changes)/len(head_changes),'clips':{}}
for name,info in clip_info.items():
    rig.animation_data.action=actions[name]
    records=[]; endpoints=[]
    for frame in range(1,info['frames']+2):
        scene.frame_set(frame); update(); deps=bpy.context.evaluated_depsgraph_get()
        soles={}
        for side in ['l','r']:
            soles[side]=min((ob.matrix_world@v.co).z for ob in meshes if ob.name in ['Turnshoe_'+side,'Leather_sole_'+side] for v in ob.evaluated_get(deps).data.vertices)
        assert min(soles.values())>-.004,(name,frame,soles)
        assert all(math.isfinite(c) for pb in rig.pose.bones for row in pb.matrix for c in row)
        hand_checks={}
        if name=='Sprint':
            for side,sign in [('l',1),('r',-1)]:
                hand=rig.pose.bones['hand_'+side]; forearm=rig.pose.bones['lowerarm_'+side]
                axis=hand.matrix.to_3x3().col[1].normalized()
                bend=math.degrees(axis.angle((forearm.tail-forearm.head).normalized()))
                local_palm=rest[hand.name].to_3x3().transposed()@palm_normals[side]
                inward=(hand.matrix.to_3x3()@local_palm).normalized().dot(Vector((-sign,0,0)))
                assert bend<.1,(side,frame,bend)
                assert inward>.97,(side,frame,inward)
                hand_checks[side]={'wrist_bend_degrees':bend,'palm_inward_dot':inward}
        records.append({'frame':frame,'soles_m':soles,'pelvis_z':rig.pose.bones['pelvis'].matrix.translation.z,'hands':hand_checks})
        if frame in [1,info['frames']+1]: endpoints.append({pb.name:pb.matrix.copy() for pb in rig.pose.bones})
    seam=max(abs(endpoints[0][n][i][j]-endpoints[1][n][i][j]) for n in endpoints[0] for i in range(4) for j in range(4))
    assert seam<1e-5,(name,seam)
    validation['clips'][name]={**info,'loop_error':seam,'minimum_sole_z_m':min(min(r['soles_m'].values()) for r in records),'airborne_frames':sum(min(r['soles_m'].values())>.015 for r in records[:-1]),'pelvis_range_m':max(r['pelvis_z'] for r in records)-min(r['pelvis_z'] for r in records),'evaluated_frames':records}
(ROOT/'validation/villager_authoring.json').write_text(json.dumps(validation,indent=2),encoding='utf-8')
rig.animation_data.action=actions['Idle']; scene.frame_start=1; scene.frame_end=120; scene.frame_set(1)
scene.render.fps=FPS; scene.render.resolution_x=500; scene.render.resolution_y=700; scene.render.resolution_percentage=100; scene.cycles.samples=12
camera=scene.camera; camera.location=(2.6,-4.4,1.85)
camera.rotation_euler=(Vector((0,0,.87))-camera.location).to_track_quat('-Z','Y').to_euler(); camera.data.ortho_scale=2.12
rig['animation_contract']='Idle/Walk/Sprint; in-place; Y-up/-Z-forward in GLB'
rig['walk_speed_mps']=7/12; rig['sprint_speed_mps']=4.5
for area in bpy.context.screen.areas:
    if area.type=='VIEW_3D': area.spaces.active.region_3d.view_perspective='CAMERA'
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'source/villager.blend'))

if 'draft' in args:
    for name,frames in [('Idle',[1]),('Walk',[10]),('Sprint',[1,6,11,16])]:
        rig.animation_data.action=actions[name]
        for frame in frames:
            scene.frame_set(frame); scene.render.filepath=str(ROOT/'validation'/('draft_%s_%02d.png'%(name,frame))); bpy.ops.render.render(write_still=True)
    rig.animation_data.action=actions['Sprint']
    camera.location=(4,0,1.1)
    camera.rotation_euler=(Vector((0,0,.90))-camera.location).to_track_quat('-Z','Y').to_euler()
    for frame in [1,6,11,16]:
        scene.frame_set(frame)
        scene.render.filepath=str(ROOT/'validation'/('draft_sprint_side_%02d.png'%frame)); bpy.ops.render.render(write_still=True)
    rig.animation_data.action=actions['Sprint']; scene.frame_set(1); update()
    hand=rig.pose.bones['hand_r']; center=hand.matrix.translation+hand.matrix.to_3x3().col[1]*.065
    camera.location=center+Vector((-.30,-.6,.20))
    camera.rotation_euler=(center-camera.location).to_track_quat('-Z','Y').to_euler()
    camera.data.ortho_scale=.26; scene.render.resolution_x=600; scene.render.resolution_y=600
    scene.render.filepath=str(ROOT/'validation/draft_hand_close.png'); bpy.ops.render.render(write_still=True)
    print('ANIMATION_DRAFT_READY',validation['posture_head_retraction_m'])
    raise SystemExit(0)

def select_asset():
    bpy.ops.object.select_all(action='DESELECT')
    for ob in [rig]+meshes+[bpy.data.objects['Eyes'],bpy.data.objects['RightHandGrip']]: ob.select_set(True)
    bpy.context.view_layer.objects.active=rig

# Export the same geometry budget used in the previous delivery.
rig.animation_data.action=actions['Idle']; scene.frame_set(1); select_asset()
mods=[]
for ob in meshes:
    tris=sum(len(p.vertices)-2 for p in ob.evaluated_get(bpy.context.evaluated_depsgraph_get()).data.polygons)
    budget=18000 if 'FaceAndHands' in ob.name else 14000 if 'Hemp_knee_tunic' in ob.name else 6500
    if tris>budget:
        mod=ob.modifiers.new('Runtime_density','DECIMATE'); mod.ratio=budget/tris; mods.append((ob,mod))
bpy.ops.export_scene.gltf(filepath=str(ROOT/'runtime/villager.glb'),export_format='GLB',use_selection=True,export_animations=True,export_animation_mode='ACTIONS',export_apply=True,export_yup=True,export_skins=True,export_influence_nb=4,export_vertex_color='NONE',export_all_vertex_colors=False,export_morph=False,export_tangents=True,export_force_sampling=True,export_frame_range=False,export_optimize_animation_size=False,export_optimize_disable_viewport=True)
finish_glb(ROOT/'runtime/villager.glb')
rig.animation_data.action=actions['Idle']; scene.frame_set(1); update()
static=[]; deps=bpy.context.evaluated_depsgraph_get()
for ob in meshes:
    mesh=bpy.data.meshes.new_from_object(ob.evaluated_get(deps)); cp=bpy.data.objects.new(ob.name+'_static',mesh); bpy.context.collection.objects.link(cp); cp.matrix_world=ob.matrix_world.copy()
    for c in list(mesh.color_attributes): mesh.color_attributes.remove(c)
    colors=mesh.color_attributes.new(name='Palette',type='FLOAT_COLOR',domain='CORNER')
    for p in mesh.polygons:
        color=(.48,.28,.18,1) if 'FaceAndHands' in ob.name else mesh.materials[p.material_index].diffuse_color
        for li in p.loop_indices: colors.data[li].color=(*color[:3],1)
    static.append(cp)
for name in ['Eyes','RightHandGrip']:
    orig=bpy.data.objects[name]; cp=bpy.data.objects.new(name+'_static',None); bpy.context.collection.objects.link(cp); cp.matrix_world=orig.matrix_world.copy(); orig.name=name+'_rig'; cp.name=name; static.append(cp)
bpy.ops.object.select_all(action='DESELECT')
for ob in static: ob.select_set(True)
bpy.ops.export_scene.gltf(filepath=str(ROOT/'runtime/static/villager.glb'),export_format='GLB',use_selection=True,export_animations=False,export_skins=False,export_yup=True,export_materials='NONE',export_vertex_color='ACTIVE',export_active_vertex_color_when_no_material=True,export_all_vertex_colors=False,export_tangents=False)
finish_glb(ROOT/'runtime/static/villager.glb')
for ob in static: bpy.data.objects.remove(ob,do_unlink=True)
for name in ['Eyes','RightHandGrip']: bpy.data.objects[name+'_rig'].name=name
for ob,mod in mods: ob.modifiers.remove(mod)

if 'render' in args:
    stage=bpy.data.collections['Presentation_only']; ground=bpy.data.objects['Studio_floor']; ground.location.z=0
    mat=ground.data.materials[0]; mat.use_nodes=True; nodes=mat.node_tree.nodes; links=mat.node_tree.links
    geometry=nodes.new('ShaderNodeNewGeometry'); checker=nodes.new('ShaderNodeTexChecker')
    checker.inputs['Color1'].default_value=(.12,.13,.14,1); checker.inputs['Color2'].default_value=(.17,.18,.19,1)
    links.new(geometry.outputs['Position'],checker.inputs['Vector']); links.new(checker.outputs['Color'],nodes.get('Principled BSDF').inputs['Base Color'])
    moving=[camera]+[o for o in stage.objects if o.type=='LIGHT']; starts={o.name:o.location.copy() for o in moving}
    for name,info in clip_info.items():
        rig.animation_data.action=actions[name]
        folder=ROOT/'cache/render_cache'/('villager_'+name.lower()); folder.mkdir(parents=True,exist_ok=True)
        travel=info['speed_mps']*info['duration_s']; tile=travel/(2*max(1,round(travel/.7))) if travel else .35
        checker.inputs['Scale'].default_value=1/tile
        step=4 if name=='Idle' else 1
        for frame in range(1,info['frames']+1,step):
            shift=-info['speed_mps']*(frame-1)/FPS; rig.location.y=shift
            for ob in moving: ob.location=starts[ob.name]+Vector((0,shift,0))
            scene.frame_set(frame); scene.render.filepath=str(folder/('%03d.png'%frame)); bpy.ops.render.render(write_still=True)
        rig.location.y=0
        for ob in moving: ob.location=starts[ob.name]
print('VILLAGER_ANIMATIONS_COMPLETE',json.dumps({k:{p:v for p,v in d.items() if p!='evaluated_frames'} for k,d in validation['clips'].items()}))
