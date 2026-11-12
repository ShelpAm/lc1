"""Reimport the final runtime model and verify all action loop poses in Blender."""
import bpy,json,math
from pathlib import Path
from mathutils import Matrix

ROOT=Path(__file__).resolve().parents[1]
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'source/villager.blend'))
scene=bpy.context.scene; keep=set(bpy.data.collections['Presentation_only'].all_objects)
for ob in list(scene.objects):
    if ob not in keep: bpy.data.objects.remove(ob,do_unlink=True)
for action in list(bpy.data.actions): bpy.data.actions.remove(action)
bpy.ops.import_scene.gltf(filepath=str(ROOT/'runtime/villager.glb'))
root=bpy.data.objects['LC1_Forward']; root.matrix_world=Matrix.Rotation(math.pi,4,'Z')@root.matrix_world
rig=next(o for o in scene.objects if o.type=='ARMATURE'); rig.animation_data_create()
report={'joints':len(rig.data.bones),'clips':{}}
for name in ['Idle','Walk','Sprint']:
    action=bpy.data.actions[name]; rig.animation_data.action=action; rig.animation_data.action_slot=action.slots[0]
    start,end=action.frame_range; poses=[]
    for frame in [round(start),round(end)]:
        scene.frame_set(frame); bpy.context.view_layer.update(); poses.append({p.name:p.matrix.copy() for p in rig.pose.bones})
    seam=max(abs(poses[0][n][i][j]-poses[1][n][i][j]) for n in poses[0] for i in range(4) for j in range(4))
    assert seam<1e-5,(name,seam)
    report['clips'][name]={'start_frame':start,'end_frame':end,'loop_error':seam}
    if name in ['Idle','Sprint']:
        scene.frame_set(round(start)+(5 if name=='Sprint' else 0))
        scene.render.resolution_x=400; scene.render.resolution_y=560; scene.cycles.samples=12
        scene.render.filepath=str(ROOT/'validation'/('reimport_'+name.lower()+'.png')); bpy.ops.render.render(write_still=True)
assert report['joints']==53
(ROOT/'validation/villager_roundtrip.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('VILLAGER_ROUNDTRIP_VERIFIED',report)
