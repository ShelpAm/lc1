#!/usr/bin/env python3
"""Pack 北极星使者_彗's embedded textures into the renderer's one-texture model format."""

import io
import json
import struct
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1] / "runtime/assets"
SOURCE = ROOT / "models/winterblessed-hwei/source.glb"
MODEL = ROOT / "models/winterblessed-hwei/北极星使者_彗.glb"
TEXTURE = ROOT / "textures/winterblessed-hwei/base-color.png"

raw = SOURCE.read_bytes()
magic, version, length = struct.unpack_from("<4sII", raw)
if magic != b"glTF" or version != 2 or length != len(raw):
    raise ValueError("invalid source GLB")

json_length, json_type = struct.unpack_from("<I4s", raw, 12)
json_bytes = raw[20 : 20 + json_length]
document = json.loads(json_bytes)
binary_header = 20 + json_length
binary_length, binary_type = struct.unpack_from("<I4s", raw, binary_header)
if json_type != b"JSON" or binary_type != b"BIN\0":
    raise ValueError("expected JSON and BIN chunks")
binary_start = binary_header + 8
binary = bytearray(raw[binary_start : binary_start + binary_length])

atlas = Image.new("RGBA", (2048, 1024))
for image_index, image in enumerate(document["images"]):
    view = document["bufferViews"][image["bufferView"]]
    begin = view.get("byteOffset", 0)
    encoded = binary[begin : begin + view["byteLength"]]
    tile = Image.open(io.BytesIO(encoded)).convert("RGBA")
    atlas.paste(tile.resize((1024, 1024), Image.Resampling.LANCZOS),
                (image_index * 1024, 0))

accessors = document["accessors"]
views = document["bufferViews"]
updated_uvs = {}
for primitive in document["meshes"][0]["primitives"]:
    material = document["materials"][primitive["material"]]
    texture_index = material["pbrMetallicRoughness"]["baseColorTexture"]["index"]
    image_index = document["textures"][texture_index]["source"]
    uv = accessors[primitive["attributes"]["TEXCOORD_0"]]
    uv_view = views[uv["bufferView"]]
    uv_start = uv_view.get("byteOffset", 0) + uv.get("byteOffset", 0)
    stride = uv_view.get("byteStride", 8)
    indices = accessors[primitive["indices"]]
    index_view = views[indices["bufferView"]]
    index_start = index_view.get("byteOffset", 0) + indices.get("byteOffset", 0)
    index_format = {5121: "B", 5123: "H", 5125: "I"}[indices["componentType"]]
    index_size = struct.calcsize(index_format)
    # Primitives share a UV accessor, so transform only their referenced vertices once.
    for offset in range(indices["count"]):
        vertex = struct.unpack_from("<" + index_format, binary,
                                    index_start + offset * index_size)[0]
        position = uv_start + vertex * stride
        if position in updated_uvs:
            if updated_uvs[position] != image_index:
                raise ValueError("primitives with different textures share a UV vertex")
            continue
        updated_uvs[position] = image_index
        u, v = struct.unpack_from("<ff", binary, position)
        struct.pack_into("<ff", binary, position, (u + image_index) * 0.5, v)

encoded = json.dumps(document, ensure_ascii=False, separators=(",", ":")).encode()
encoded += b" " * (-len(encoded) % 4)
binary += b"\0" * (-len(binary) % 4)
result = struct.pack("<4sII", b"glTF", 2, 28 + len(encoded) + len(binary))
result += struct.pack("<I4s", len(encoded), b"JSON") + encoded
result += struct.pack("<I4s", len(binary), b"BIN\0") + binary

MODEL.parent.mkdir(parents=True, exist_ok=True)
MODEL.write_bytes(result)
TEXTURE.parent.mkdir(parents=True, exist_ok=True)
atlas.save(TEXTURE)
print(f"Prepared {MODEL}: {len(document['animations'])} clips")
