# Model assets

Paths below are relative to the game's `runtime/assets` asset root.

| Asset | Path | Contents |
| --- | --- | --- |
| Villager | `models/characters/runtime/villager.glb` | Skin, materials, Idle / Walk / Sprint |
| Bandit | `models/characters/runtime/bandit.glb` | Skin, materials, Idle |
| Knight | `models/characters/runtime/knight.glb` | Skin, materials, Idle |
| Static characters | `models/characters/runtime/static/*.glb` | Unskinned vertex-colored fallback models |
| Blacksmith | `models/buildings/blacksmith/blacksmith.glb` | Exterior building with working porch |

Character metadata and exact clip names are in `characters/manifest.json`.
Editable sources, authoring tools, validation records and character license records
remain alongside the models. Blender source files are authoring inputs, not game
runtime dependencies. Generated caches, logs and Blender backups are ignored.

This commit packages assets only; it does not install them into game scenes or add
skinning support. The blacksmith OBJ is Z up; its GLB is Y up. The blacksmith
rebuild script also needs the local `lc-res/Modular Village` kit, which is not
included here. Its source kit license has not been established by this package.

Validate characters from the repository root:

```sh
python runtime/assets/models/characters/tools/verify_package.py
```

See each package's README for editing and rebuilding instructions.
