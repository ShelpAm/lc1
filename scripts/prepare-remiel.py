#!/usr/bin/env python3
"""Prepare a local Remiel PMX for lc1. Source license forbids redistribution.

Usage: python scripts/prepare-remiel.py /path/to/蕾米埃尔-泳装
Requires Assimp CLI and Pillow. Does not copy or modify the original PMX.
"""

import argparse
import json
import math
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

from PIL import Image

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", type=Path)
args = parser.parse_args()
source = args.source.resolve()
root = Path(__file__).resolve().parents[1] / "runtime/assets"
model_dir = root / "models/remiel-swimsuit"
texture_dir = root / "textures/remiel-swimsuit"

with tempfile.TemporaryDirectory(prefix="lc1-remiel-") as temporary:
    exported = Path(temporary) / "model.gltf"
    subprocess.run(
        [
            "assimp",
            "export",
            str(source / "蕾米埃尔-泳装.pmx"),
            str(exported),
            "-fgltf2",
        ],
        check=True,
        capture_output=True,
    )
    document = json.loads(exported.read_text())
    binary = bytearray(exported.with_suffix(".bin").read_bytes())

# PMX's '+' materials are coincident transparent highlight overlays; lc1 has
# opaque PBR materials, so omit these and the semitransparent eye shadow.
# Material indices belong to this specific PMX, not a generic PMX convention.
omitted = {15, *range(37, 46)}
primitives = document["meshes"][0]["primitives"]
primitives[:] = [p for p in primitives if p["material"] not in omitted]
images = sorted(
    {
        document["textures"][
            document["materials"][p["material"]]["pbrMetallicRoughness"][
                "baseColorTexture"
            ]["index"]
        ]["source"]
        for p in primitives
    }
)
# 1024px tiles, padded to keep bilinear filtering within each tile.
tile_size, padding, columns, rows = 1024, 4, 3, 2
atlas = Image.new(
    "RGB", (columns * (tile_size + 2 * padding), rows * (tile_size + 2 * padding))
)
regions = {}
for slot, image_index in enumerate(images):
    path = source / document["images"][image_index]["uri"].replace("\\", "/")
    tile = (
        Image.open(path)
        .convert("RGB")
        .resize((tile_size, tile_size), Image.Resampling.LANCZOS)
    )
    x = slot % columns * (tile_size + 2 * padding)
    y = slot // columns * (tile_size + 2 * padding)
    # Duplicate boundary pixels into the gutter.
    atlas.paste(tile.resize((tile_size + 2 * padding, tile_size + 2 * padding)), (x, y))
    atlas.paste(tile, (x + padding, y + padding))
    for edge, box, target in [
        ("left", (0, 0, 1, tile_size), (x, y + padding)),
        (
            "right",
            (tile_size - 1, 0, tile_size, tile_size),
            (x + padding + tile_size, y + padding),
        ),
    ]:
        atlas.paste(tile.crop(box).resize((padding, tile_size)), target)
    atlas.paste(
        tile.crop((0, 0, tile_size, 1)).resize((tile_size, padding)), (x + padding, y)
    )
    atlas.paste(
        tile.crop((0, tile_size - 1, tile_size, tile_size)).resize(
            (tile_size, padding)
        ),
        (x + padding, y + padding + tile_size),
    )
    regions[image_index] = (x + padding, y + padding)

updated = set()
for primitive in primitives:
    attributes = primitive["attributes"]
    # Assimp duplicates PMX's UV channel for sphere mapping. Only UV0 is used.
    attributes.pop("TEXCOORD_1", None)
    image_index = document["textures"][
        document["materials"][primitive["material"]]["pbrMetallicRoughness"][
            "baseColorTexture"
        ]["index"]
    ]["source"]
    uv = document["accessors"][attributes["TEXCOORD_0"]]
    view = document["bufferViews"][uv["bufferView"]]
    start = view.get("byteOffset", 0) + uv.get("byteOffset", 0)
    stride = view.get("byteStride", 8)
    x, y = regions[image_index]
    for vertex in range(uv["count"]):
        offset = start + vertex * stride
        if offset in updated:
            raise ValueError("unexpected shared UVs between material primitives")
        updated.add(offset)
        u, v = struct.unpack_from("<ff", binary, offset)
        if not (-0.001 <= u <= 1.001 and -0.001 <= v <= 1.001):
            raise ValueError(f"UV outside atlas tile: {u}, {v}")
        struct.pack_into(
            "<ff",
            binary,
            offset,
            (x + min(1, max(0, u)) * tile_size) / atlas.width,
            (y + min(1, max(0, v)) * tile_size) / atlas.height,
        )
    primitive.pop("material")

# Materials are explicitly bound by YAML. No external images remain in the GLB.
for key in (
    "images",
    "textures",
    "materials",
    "samplers",
    "extensionsUsed",
    "extensionsRequired",
):
    document.pop(key, None)

# Bake FK clips into the imported skeleton. PMX IK/grant constraints are not
# exported by Assimp: animate the weighted D leg chains, not the IK controls.
nodes = {node["name"]: index for index, node in enumerate(document["nodes"])}


def accessor(values, kind):
    binary.extend(b"\0" * (-len(binary) % 4))
    offset = len(binary)
    for value in values:
        binary.extend(struct.pack("<" + "f" * len(value), *value))
    view = len(document["bufferViews"])
    document["bufferViews"].append(
        {"buffer": 0, "byteOffset": offset, "byteLength": len(binary) - offset}
    )
    index = len(document["accessors"])
    record = {
        "bufferView": view,
        "componentType": 5126,
        "count": len(values),
        "type": kind,
    }
    if kind == "SCALAR":
        record.update(min=[values[0][0]], max=[values[-1][0]])
    document["accessors"].append(record)
    return index


def rotation(x, y, z):
    # glTF stores XYZW; compose Rz * Ry * Rx, in degrees.
    sx, sy, sz = [math.sin(math.radians(a) / 2) for a in (x, y, z)]
    cx, cy, cz = [math.cos(math.radians(a) / 2) for a in (x, y, z)]
    return (
        sx * cy * cz - cx * sy * sz,
        cx * sy * cz + sx * cy * sz,
        cx * cy * sz - sx * sy * cz,
        cx * cy * cz + sx * sy * sz,
    )


document["animations"] = []
for clip_name, duration in [
    ("Idle", 4.0),
    ("Walk", 3.0),
    ("Run", 0.8),
    ("Wave", 2.4),
    ("Crouch", 1.0),
    ("StandUp", 1.0),
]:
    frames = 48
    time = accessor([(duration * i / frames,) for i in range(frames + 1)], "SCALAR")
    channels = {}
    for frame in range(frames + 1):
        phase = 2 * math.pi * frame / frames
        sway = math.sin(phase)
        moving = clip_name in ("Walk", "Run")
        running = clip_name == "Run"
        pose = {
            "上半身": (
                -5 if running else -2 if moving else 0.8 * sway,
                0,
                (1.5 if moving else 0.6) * sway,
            ),
            "上半身2": (0.7 * sway, (3 if moving else 1.5) * sway, 0),
            "頭": (0.8 * math.cos(phase), -1.5 * sway, 0),
            "Skn_L_WingA_01_1": (0, 2 * sway, -2 + 2 * sway),
            "Skn_R_WingA_01_1": (0, -2 * sway, 2 - 2 * sway),
        }
        for side, sign in [("左", 1), ("右", -1)]:
            stride = sign * sway
            hip = -(36 if running else 24) * stride if moving else 0
            knee = (10 + (60 if running else 38) * max(0, -stride)) if moving else 0
            pose[side + "足D"] = (hip, 0, 0)
            pose[side + "ひざD"] = (knee, 0, 0)
            pose[side + "足首D"] = (-hip - knee, 0, 0)
            pose[side + "腕"] = (
                (30 if running else 18) * stride if moving else sway,
                0,
                -sign * (24 + 1.5 * sway),
            )
            pose[side + "ひじ"] = (-35 if running else -10, 0, 0)
        if clip_name == "Wave":
            # Ease the greeting in and out; locomotion resumes after the clip.
            envelope = math.sin(math.pi * frame / frames) ** 2
            pose["右腕"] = (-10 * envelope, 0, 24 - 124 * envelope)
            pose["右ひじ"] = (-10, 0, (-35 + 20 * math.sin(phase * 3)) * envelope)
            pose["右手首"] = (0, 15 * math.sin(phase * 3) * envelope, 0)
        crouch = 0
        if clip_name in ("Crouch", "StandUp"):
            progress = frame / frames if clip_name == "Crouch" else 1 - frame / frames
            crouch = progress * progress * (3 - 2 * progress)
            # Fold the knees while dropping the pelvis by the lost leg extension.
            # Reach down with straight elbows; the deep squat brings the middle
            # fingertips just above the sole plane, ahead of the knees rather
            # than beside the feet, at full crouch.
            # Drive the same pose backward when releasing Ctrl mid-transition.
            pose["上半身"] = (50 * crouch, 0, 0)
            pose["上半身2"] = (0, 0, 0)
            pose["頭"] = (-8 * crouch, 0, 0)
            for side, sign in [("左", 1), ("右", -1)]:
                pose[side + "足D"] = (-84.34 * crouch, 0, 0)
                pose[side + "ひざD"] = (168.68 * crouch, 0, 0)
                pose[side + "足首D"] = (-84.34 * crouch, 0, 0)
                pose[side + "腕"] = (
                    -76.8 * crouch,
                    -42.8 * crouch * sign,
                    -(24 + 36.4 * crouch) * sign,
                )
                pose[side + "ひじ"] = (-10 * (1 - crouch), 0, 0)
        for name, angles in pose.items():
            channels.setdefault((name, "rotation"), []).append(rotation(*angles))
        pelvis = document["nodes"][nodes["腰"]]
        translation = pelvis.get(
            "translation", pelvis.get("matrix", [0] * 16)[12:15]
        ).copy()
        translation[1] -= 10.49 * (1 - math.cos(math.radians(84.34 * crouch)))
        channels.setdefault(("腰", "translation"), []).append(translation)
    animation = {"name": clip_name, "samplers": [], "channels": []}
    for (name, path), values in channels.items():
        node_index = nodes[name]
        node = document["nodes"][node_index]
        if "matrix" in node:
            matrix = node.pop("matrix")
            # This PMX export has translation-only bind transforms. Preserve them
            # when converting animated nodes to the TRS representation glTF requires.
            expected = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0]
            if any(abs(a - b) > 1e-5 for a, b in zip(matrix[:12], expected)):
                raise ValueError(f"unexpected nontranslation bind transform: {name}")
            node["translation"] = matrix[12:15]
        sampler = len(animation["samplers"])
        animation["samplers"].append(
            {
                "input": time,
                "output": accessor(values, "VEC4" if path == "rotation" else "VEC3"),
                "interpolation": "LINEAR",
            }
        )
        animation["channels"].append(
            {"sampler": sampler, "target": {"node": node_index, "path": path}}
        )
    document["animations"].append(animation)

document["buffers"] = [{"byteLength": len(binary)}]
encoded = json.dumps(document, ensure_ascii=False, separators=(",", ":")).encode()
encoded += b" " * (-len(encoded) % 4)
binary += b"\0" * (-len(binary) % 4)
result = struct.pack("<4sII", b"glTF", 2, 28 + len(encoded) + len(binary))
result += struct.pack("<I4s", len(encoded), b"JSON") + encoded
result += struct.pack("<I4s", len(binary), b"BIN\0") + binary
model_dir.mkdir(parents=True, exist_ok=True)
texture_dir.mkdir(parents=True, exist_ok=True)
(model_dir / "remiel.glb").write_bytes(result)
shutil.copyfile(source / "RM.txt", model_dir / "RM.txt")
atlas.save(texture_dir / "base-color.png")
print(
    f"Prepared {len(primitives)} primitives, {len(images)} atlas tiles; baked "
    "Idle/Walk/Run/Wave/Crouch/StandUp."
)
