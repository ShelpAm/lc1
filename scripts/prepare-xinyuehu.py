#!/usr/bin/env python3
"""Prepare Xinyuehu's normal form from the supplied ZIP (Assimp, Pillow, NumPy)."""

import argparse
import hashlib
import json
import math
import struct
import subprocess
import tempfile
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('archive', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1] / 'runtime/assets'
model_dir = root / 'models/xinyuehu'
texture_dir = root / 'textures/xinyuehu'


def quaternion(matrix):
    # Extract XYZW from an orthonormal rotation, including near-180-degree binds.
    candidates = [1 + matrix[0, 0] - matrix[1, 1] - matrix[2, 2],
                  1 - matrix[0, 0] + matrix[1, 1] - matrix[2, 2],
                  1 - matrix[0, 0] - matrix[1, 1] + matrix[2, 2],
                  1 + np.trace(matrix)]
    i = int(np.argmax(candidates))
    q = np.zeros(4)
    q[i] = math.sqrt(max(0, candidates[i])) / 2
    d = 4 * q[i]
    if i == 3:
        q[:3] = [matrix[2, 1] - matrix[1, 2], matrix[0, 2] - matrix[2, 0],
                 matrix[1, 0] - matrix[0, 1]]
        q[:3] /= d
    else:
        j, k = (i + 1) % 3, (i + 2) % 3
        q[j] = (matrix[j, i] + matrix[i, j]) / d
        q[k] = (matrix[k, i] + matrix[i, k]) / d
        q[3] = (matrix[k, j] - matrix[j, k]) / d
    return (q / np.linalg.norm(q)).tolist()


def accessor(values, kind):
    binary.extend(b'\0' * (-len(binary) % 4))
    offset = len(binary)
    for value in values:
        binary.extend(struct.pack('<' + 'f' * len(value), *value))
    view = len(document['bufferViews'])
    document['bufferViews'].append({'buffer': 0, 'byteOffset': offset,
                                  'byteLength': len(binary) - offset})
    record = {'bufferView': view, 'componentType': 5126, 'count': len(values), 'type': kind}
    if kind == 'SCALAR':
        record.update(min=[values[0][0]], max=[values[-1][0]])
    document['accessors'].append(record)
    return len(document['accessors']) - 1


with tempfile.TemporaryDirectory(prefix='lc1-xinyuehu-') as temporary:
    source = Path(temporary)
    with zipfile.ZipFile(args.archive) as archive:
        # Extract only the chosen form; shared UE shader masks are not PBR inputs.
        for entry in archive.infolist():
            if '心月狐（常驻形态）/' in entry.filename:
                archive.extract(entry, source)
    form = source / '鸣潮_心月狐/心月狐（常驻形态）'
    exported = source / 'model.gltf'
    subprocess.run(['assimp', 'export', str(form / 'Model/心月狐（常驻形态）.fbx'),
                    str(exported), '-fgltf2'], check=True, capture_output=True)
    document = json.loads(exported.read_text())
    binary = bytearray(exported.with_suffix('.bin').read_bytes())
    tile_size, padding = 1024, 4
    pitch = tile_size + 2 * padding
    atlas = Image.new('RGB', (4 * pitch, 2 * pitch))
    updated = set()
    for mesh in document['meshes']:
        # Facial morphs are not supported by lc1: retain the neutral base geometry.
        mesh.pop('weights', None)
        mesh.pop('extras', None)
        for primitive in mesh['primitives']:
            slot = primitive.pop('material')
            name = document['materials'][slot]['name'].removeprefix('MI_')
            tile = Image.open(form / 'Textures' / f'T_{name}_D.png').convert('RGB')
            tile = tile.resize((tile_size, tile_size), Image.Resampling.LANCZOS)
            x, y = slot % 4 * pitch, slot // 4 * pitch
            # Clamp-to-edge gutters, including corners, before atlas UV remapping.
            atlas.paste(tile.resize((pitch, pitch)), (x, y))
            atlas.paste(tile, (x + padding, y + padding))
            for box, size, target in [
                ((0, 0, 1, tile_size), (padding, tile_size), (x, y + padding)),
                ((tile_size - 1, 0, tile_size, tile_size), (padding, tile_size),
                 (x + padding + tile_size, y + padding)),
                ((0, 0, tile_size, 1), (tile_size, padding), (x + padding, y)),
                ((0, tile_size - 1, tile_size, tile_size), (tile_size, padding),
                 (x + padding, y + padding + tile_size))]:
                atlas.paste(tile.crop(box).resize(size), target)
            primitive.pop('targets', None)
            # These channels hold UE shader masks, not additional PBR UV/color inputs.
            attributes = primitive['attributes']
            for attribute in list(attributes):
                if attribute not in {'POSITION', 'NORMAL', 'TEXCOORD_0', 'JOINTS_0', 'WEIGHTS_0'}:
                    del attributes[attribute]
            uv = document['accessors'][attributes['TEXCOORD_0']]
            view = document['bufferViews'][uv['bufferView']]
            start = view.get('byteOffset', 0) + uv.get('byteOffset', 0)
            for vertex in range(uv['count']):
                offset = start + vertex * view.get('byteStride', 8)
                if offset in updated:
                    raise ValueError('unexpected shared atlas UV accessor')
                updated.add(offset)
                u, v = struct.unpack_from('<ff', binary, offset)
                if not (-0.001 <= u <= 1.001 and -0.001 <= v <= 1.001):
                    raise ValueError(f'UV outside tile: {u}, {v}')
                struct.pack_into('<ff', binary, offset,
                                 (x + padding + min(1, max(0, u)) * tile_size) / atlas.width,
                                 (y + padding + min(1, max(0, v)) * tile_size) / atlas.height)

# Preserve the bind skeleton and animate deltas in model space. Parent-local
# axes differ between the mirrored limbs; never overwrite their bind rotations.
nodes = document['nodes']
parents = {child: i for i, node in enumerate(nodes) for child in node.get('children', [])}
local = [np.array(n.get('matrix', np.eye(4).T.flatten())).reshape(4, 4).T for n in nodes]
world = []
for i, matrix in enumerate(local):
    world.append(world[parents[i]] @ matrix if i in parents else matrix)
indices = {n['name']: i for i, n in enumerate(nodes)}
document['animations'] = []
for clip, duration in [('Idle', 4), ('Walk', 1.2), ('Run', 0.8)]:
    times = accessor([(duration * f / 48,) for f in range(49)], 'SCALAR')
    tracks = {}
    for f in range(49):
        phase = 2 * math.pi * f / 48
        moving = clip != 'Idle'
        angles = {'Bip001Spine2': ((0, 0, 1), 0.7 * math.sin(phase))}
        for side, sign in [('L', 1), ('R', -1)]:
            stride = sign * math.sin(phase)
            angles[f'Bip001{side}UpperArm'] = ((0, 0, 1), -sign * 18)
            if moving:
                angles[f'Bip001{side}UpperArmSwing'] = ((1, 0, 0), -18 * stride)
                angles[f'Bip001{side}Thigh'] = ((1, 0, 0), 24 * stride)
                angles[f'Bip001{side}Calf'] = ((1, 0, 0), -35 * max(0, -stride))
        deltas = {}
        for name, (axis, degrees) in angles.items():
            actual = name.removesuffix('Swing')
            i = indices[actual]
            axis = world[parents[i]][:3, :3].T @ np.array(axis, dtype=float)
            axis /= np.linalg.norm(axis)
            a = math.radians(degrees)
            cross = np.array([[0, -axis[2], axis[1]], [axis[2], 0, -axis[0]],
                              [-axis[1], axis[0], 0]])
            rotation = np.eye(3) + math.sin(a) * cross + (1 - math.cos(a)) * (cross @ cross)
            deltas[i] = rotation @ deltas.get(i, local[i][:3, :3])
        for i, rotation in deltas.items():
            tracks.setdefault(i, []).append(quaternion(rotation))
    animation = {'name': clip, 'samplers': [], 'channels': []}
    for i, values in tracks.items():
        nodes[i].pop('matrix', None)
        nodes[i]['translation'] = local[i][:3, 3].tolist()
        nodes[i]['rotation'] = quaternion(local[i][:3, :3])
        animation['channels'].append({'sampler': len(animation['samplers']),
                                      'target': {'node': i, 'path': 'rotation'}})
        animation['samplers'].append({'input': times, 'output': accessor(values, 'VEC4'),
                                      'interpolation': 'LINEAR'})
    document['animations'].append(animation)
for key in ['images', 'textures', 'materials', 'samplers', 'extensionsUsed', 'extensionsRequired']:
    document.pop(key, None)
for node in nodes:
    node.pop('weights', None)
document['buffers'] = [{'byteLength': len(binary)}]
encoded = json.dumps(document, ensure_ascii=False, separators=(',', ':')).encode()
encoded += b' ' * (-len(encoded) % 4)
binary += b'\0' * (-len(binary) % 4)
result = struct.pack('<4sII', b'glTF', 2, 28 + len(encoded) + len(binary))
result += struct.pack('<I4s', len(encoded), b'JSON') + encoded
result += struct.pack('<I4s', len(binary), b'BIN\0') + binary
model_dir.mkdir(parents=True, exist_ok=True)
texture_dir.mkdir(parents=True, exist_ok=True)
(model_dir / 'xinyuehu.glb').write_bytes(result)
atlas.save(texture_dir / 'base-color.png')
(model_dir / 'SOURCE.md').write_text(
    '# 心月狐（常驻形态）\n\n来源：用户提供的 `鸣潮_心月狐.zip`。\n\n'
    f'SHA-256: `{hashlib.sha256(args.archive.read_bytes()).hexdigest()}`\n\n'
    '通过 `scripts/prepare-xinyuehu.py` 转换 FBX，保留骨骼与中性表情，合并八张漫反射贴图。\n'
    'Idle / Walk / Run 为本项目烘焙的基础 FK 动画，不是原作动画；未接入 UE 特效、透明材质和表情 morph。\n'
    '原始压缩包留在用户下载目录；权利归原作者，未附带再分发授权。\n')
print(f'Prepared {len(document["meshes"])} meshes, {len(nodes)} nodes, Idle/Walk/Run')
