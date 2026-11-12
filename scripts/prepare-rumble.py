#!/usr/bin/env python3
"""Prepare the supplied Rumble GLB for lc1's opaque PBR character path.

Keeps the original skin and selected animation tracks unchanged. The transparent
spell shield and clips using zero-scale visibility tricks need a separate VFX
path; they are not part of this character variant. Source assets are untouched.
"""
import argparse
import json
from pathlib import Path
import struct

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--assets", type=Path, default=Path("runtime/assets"))
args = parser.parse_args()
source = args.assets / "models/rumble/source.glb"
data = source.read_bytes()
magic, version, length = struct.unpack_from("<III", data)
if magic != 0x46546C67 or version != 2 or length != len(data):
    raise ValueError("expected a complete glTF 2 GLB")
json_length, json_type = struct.unpack_from("<II", data, 12)
if json_type != 0x4E4F534A:
    raise ValueError("expected GLB JSON chunk")
model = json.loads(data[20:20 + json_length])
binary_start = 20 + json_length
binary_length, binary_type = struct.unpack_from("<II", data, binary_start)
if binary_type != 0x004E4942:
    raise ValueError("expected GLB BIN chunk")
binary = data[binary_start + 8:binary_start + 8 + binary_length]
clips = {"Idle1_Base", "Run_Base", "Rumble_Skin03_Attack1.anm", "Death", "Rumble_Skin03_Joke.anm"}
model["animations"] = [clip for clip in model["animations"] if clip["name"] in clips]
if {clip["name"] for clip in model["animations"]} != clips:
    raise ValueError("source is missing expected Rumble animation clips")
for mesh in model["meshes"]:
    mesh["primitives"] = [primitive for primitive in mesh["primitives"]
                          if model["materials"][primitive["material"]].get("alphaMode", "OPAQUE") == "OPAQUE"]
# lc1 binds the common body atlas explicitly through ModelConfig.
texture = model["textures"][model["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"]["index"]]
image = model["images"][texture["source"]]
if image["mimeType"] != "image/png":
    raise ValueError("expected the supplied PNG body atlas")
view = model["bufferViews"][image["bufferView"]]
output = args.assets / "models/rumble"
output.mkdir(parents=True, exist_ok=True)
start = view.get("byteOffset", 0)
texture_output = args.assets / "textures/rumble"
texture_output.mkdir(parents=True, exist_ok=True)
(texture_output / "base-color.png").write_bytes(binary[start:start + view["byteLength"]])
encoded = json.dumps(model, ensure_ascii=False, separators=(",", ":")).encode()
encoded += b" " * (-len(encoded) % 4)
binary += b"\0" * (-len(binary) % 4)
result = struct.pack("<III", magic, 2, 28 + len(encoded) + len(binary))
result += struct.pack("<II", len(encoded), json_type) + encoded
result += struct.pack("<II", len(binary), binary_type) + binary
(output / "rumble.glb").write_bytes(result)
print(f"Prepared {output / 'rumble.glb'}: {len(clips)} clips, opaque body and original skin")
