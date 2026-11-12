# Medieval character resources

Only model resources are managed here. No engine source, game saves, or other
project files are changed by this package.

Start with **manifest.json**. Every path in it is relative to this directory.

| Directory | Purpose |
| --- | --- |
| runtime/ | Self-contained skinned GLBs, with embedded textures and animations |
| runtime/static/ | Unskinned, vertex-colored compatibility models |
| source/ | Editable Blender files; images are packed |
| previews/ | Actual Blender renders and animated GIFs |
| validation/ | Authoring and final binary validation results |
| tools/ | Rebuild, export, packaging and validation scripts |
| licenses/ | MakeHuman CC0 text and source provenance |
| cache/ | Regenerable render frames, created only when rendering previews |

## Program-facing contract

| Character ID | Model | Available clips |
| --- | --- | --- |
| villager | runtime/villager.glb | Idle, Walk, Sprint |
| bandit | runtime/bandit.glb | Idle |
| knight | runtime/knight.glb | Idle |

Use exact clip names, not animation array indices. All clips loop, start at zero
seconds and have an explicit duplicate endpoint. The villager clips are:

| Clip | Duration | Matching world speed |
| --- | --- | --- |
| Idle | 4 seconds | 0 m/s |
| Walk | 1.2 seconds | 0.583333 m/s |
| Sprint | 0.666667 seconds | 4.5 m/s |

Clips are **in-place**. Gameplay supplies world movement. For a playback multiplier
`r`, also multiply the matching world speed by `r`. Footstep event times are listed
in the manifest; they are metadata, not an installed runtime event system.

All GLBs use meters, a right-handed coordinate system, +Y up and -Z forward. A
single LC1_Forward node transforms the entire hierarchy consistently. Keep it;
do not rotate only the mesh or discard it when reading the skin. Each rig has 53
joints. `Eyes` and `RightHandGrip` are the attachment nodes. Skeletal vertices have
at most four normalized influences. Textures and buffers are embedded in the GLB;
no Blender add-on is needed at runtime.

The villager's chest, neck and shoulders were corrected to a more upright posture.
Idle has breathing, relaxed arms and a small weight shift; Walk preserves the low
flat-ground step; Sprint has forefoot push-off, low foot recovery and flight phases.
Its leading knee unfolds before contact, and the rear leg retains a longer
extension through push-off and early recovery, with a small residual knee bend.
Sprint keeps a forward running lean from the hips rather than rounding the neck.
The pelvis moves 10 cm forward relative to the preceding clip. Its height is
fitted to the stance leg; the airborne leg adjusts its recovery height to avoid
pulling the hips down. Shoulder rotation opposes the hips, while compact arm
drive follows the opposite leg's swing timing. The terminal foot arc avoids an
exaggerated forward reach and sharp changes in body height.

The hand revision uses anatomical palm directions for the mirrored finger rigs.
Palms face inward, wrists stay aligned with the forearms during Sprint, and the
thumbs fold toward the index fingers. Sprint keeps the elbows close to the ribs,
with a compact shoulder swing and bent elbows. The fists are lowered and drawn
back toward the lower ribs, matching the accepted pose preview. Elbow flex
absorbs shoulder motion, keeping the hands near the sides. Idle and Walk also use the corrected palm and
finger axes. The animation names, timing, matching speeds and manifest paths are
unchanged. Superseded poses and historical comparison outputs have been removed.
The optional generated `previews/villager_sprint_review.gif` shows synchronized front and side views.
Preview GIFs are not included in this checkout; render and encode them with the tools below.

## Existing LC1 loader

The current LC1 loader rejects skins and does not import glTF material bindings.
This package does not change that code. Until those features are implemented,
use `runtime/static/<character>.glb`, with a white base-color material so COLOR_0
retains the palette. Static variants do not play animations and do not reproduce
the full Blender material quality. The villager static fallback is its new neutral
Idle pose, not the old A-pose.

For example, when this folder is `models/characters/` under your configured asset root:

```yaml
model:
  path: models/characters/runtime/static/villager.glb
  eyes: Eyes
  weapon_mount: RightHandGrip
  material:
    base_color: [1, 1, 1, 1]
    metallic: 0
    roughness: 0.85
```

This is a usage example only; no save or configuration file was modified.

## Authoring and checks

Open `source/villager.blend`, select villager_Rig, and choose Idle, Walk or Sprint
in the Action Editor. Set the playback interval to 1..120, 1..36 or 1..20 respectively.
In GIFs the character and tracking camera travel over a flat tiled floor; this
presentation movement is excluded from the exported animation clips.

Rebuild with Blender in background mode and `tools/build_villager_animations.py`.
Its required original Walk input is retained as `source/villager_animation_base.blend`;
this is a build dependency, separate from the current editable `source/villager.blend`.
Pass `-- render` to also regenerate preview frames, then run
`tools/assemble_previews.py` with a Python installation that has Pillow.
`tools/verify_package.py` checks the shipped package without Blender.
`tools/render_sprint_review.py` renders the additional motion review views.
Preview frames are generated under `cache/render_cache/` and may be deleted after encoding.

Human anatomy, rigging data, skin, eyes and hair originate from the official
MakeHuman/MPFB CC0 assets; provenance is in licenses/. Clothing, armor and animation
were authored for this project. The models still have procedural garment topology;
extreme poses, terrain adaptation, animation blending and in-engine GPU performance
have not been certified. Historical archives, draft outputs and old comparison
scripts have been removed; current models, authoring sources and license records remain.
