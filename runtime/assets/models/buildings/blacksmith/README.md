# Blacksmith kit assembly study

A 4 x 3 metre house with a 2 metre deep open working porch. Front is -Y in the
authoring OBJ, and +Z is up. The GLB is converted to glTF +Y up, with front +Z.
The ground plane is near zero; the paved presentation base is about 7 x 8 metres.

Clear dimensions measured above the paving surface (Z=0.09 m):

- Door leaf and lowest header trim: 2.10 m above paving; leaf width 0.90 m.
- Porch front beam underside: 2.30 m; diagonal braces reduce clearance near posts.
- Anvil working face: 0.90 m; its support crate was shortened with the anvil.
- Highest point: 4.21 m above paving (world Z=4.30 m); house scale is unchanged.

The opening trim is raised together with the door leaf, while the facade top stays
fixed. Porch posts, braces, beam and roof are raised together by 0.24 m.

## Files

- `blacksmith.glb`: self-contained model, palette materials and flat normals.
- `blacksmith.obj` and `blacksmith.mtl`: editable named parts, Z up.
- `preview.png`, `preview_front.png`, `preview_rear.png`: orthographic geometry renders.
- `assembly.json`: source kit, component counts and geometry totals.
- `tools/build_blacksmith.py`: reproducible assembly, export and preview script.

The assembly uses 125 instances of existing Modular Village components from
`runtime/assets/lc-res/Modular Village`: masonry, stucco, roof tiles, beams,
posts, chimney, paving, doors, windows, sign, lamp, crates, barrel and broom.
Some components are resized to fit this simple composition.
Gable infill, forge, glowing coal blocks, anvil and hammer are simple added meshes.
The porch workbench is assembled from kit beams and posts.

This is an exterior asset study. It is not installed into game saves or scenes;
collision, navigation, interaction, smoke and dynamic fire lighting are not included.
The previews use a small CPU rasterizer; they are not in-engine screenshots.
Source kit license terms are not established by this assembly.

Rebuild with Python, NumPy and Pillow:

```powershell
python tools/build_blacksmith.py
```

Open the OBJ in a 3D editor to edit named parts; keep its MTL in the same folder.
The GLB has one mesh grouped by material, for straightforward importing.
