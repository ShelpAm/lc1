"""Render synchronized front and side views for whole-cycle motion review."""
import bpy
from pathlib import Path
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[1]
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'source/villager.blend'))
scene=bpy.context.scene; rig=bpy.data.objects['villager_Rig']; camera=scene.camera
rig.animation_data.action=bpy.data.actions['Sprint']; rig.animation_data.action_slot=bpy.data.actions['Sprint'].slots[0]
scene.render.resolution_x=360; scene.render.resolution_y=500; scene.cycles.samples=8
ground=bpy.data.objects['Studio_floor']; ground.location.z=0
material=ground.data.materials[0]; nodes=material.node_tree.nodes; links=material.node_tree.links
geometry=nodes.new('ShaderNodeNewGeometry'); checker=nodes.new('ShaderNodeTexChecker')
checker.inputs['Color1'].default_value=(.12,.13,.14,1); checker.inputs['Color2'].default_value=(.17,.18,.19,1)
checker.inputs['Scale'].default_value=1/.375
links.new(geometry.outputs['Position'],checker.inputs['Vector']); links.new(checker.outputs['Color'],nodes.get('Principled BSDF').inputs['Base Color'])
lights=[o for o in bpy.data.collections['Presentation_only'].all_objects if o.type=='LIGHT']
starts={o.name:o.location.copy() for o in lights}
for view,position in [('front',Vector((0,-4,1.2))),('side',Vector((4,0,1.1)))]:
    folder=ROOT/'cache/render_cache'/('villager_sprint_'+view); folder.mkdir(parents=True,exist_ok=True)
    camera.location=position; camera.rotation_euler=(Vector((0,0,.9))-position).to_track_quat('-Z','Y').to_euler()
    for frame in range(1,21):
        shift=Vector((0,-4.5*(frame-1)/30,0)); rig.location=shift; camera.location=position+shift
        for ob in lights: ob.location=starts[ob.name]+shift
        scene.frame_set(frame); scene.render.filepath=str(folder/('%03d.png'%frame)); bpy.ops.render.render(write_still=True)
print('SPRINT_REVIEW_RENDERED')
