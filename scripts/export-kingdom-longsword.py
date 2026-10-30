#!/usr/bin/env python3
"""Extract the static KingdomLongsword from the supplied legacy Blender 2.48 asset.

This is an offline asset conversion, not a general Blender importer. Object transforms,
modifiers, UVs and materials are intentionally omitted; the game adapts the local mesh.
"""

import argparse
import math
from pathlib import Path
import re
import struct


def export(source: Path, destination: Path) -> None:
    raw = source.read_bytes()
    if raw[:12] != b"BLENDER_v248":
        raise ValueError("expected the supplied 32-bit little-endian Blender 2.48 asset")
    blocks = []
    cursor = 12
    while cursor + 20 <= len(raw):
        code, size, address, dna, count = struct.unpack_from("<4sIIII", raw, cursor)
        cursor += 20
        if cursor + size > len(raw):
            raise ValueError("truncated Blender block")
        blocks.append((code, address, raw[cursor:cursor + size]))
        cursor += size
        if code == b"ENDB":
            break
    data = next(payload for code, _, payload in blocks if code == b"DNA1")
    cursor = 0

    def marker(value):
        nonlocal cursor
        cursor = (cursor + 3) & ~3
        if data[cursor:cursor + 4] != value:
            raise ValueError(f"missing DNA marker {value!r}")
        cursor += 4

    def read(fmt):
        nonlocal cursor
        result = struct.unpack_from(fmt, data, cursor)
        cursor += struct.calcsize(fmt)
        return result

    def strings():
        nonlocal cursor
        result = []
        for _ in range(read("<I")[0]):
            end = data.index(b"\0", cursor)
            result.append(data[cursor:end].decode("ascii"))
            cursor = end + 1
        return result

    marker(b"SDNA")
    marker(b"NAME")
    names = strings()
    marker(b"TYPE")
    types = strings()
    marker(b"TLEN")
    lengths = read("<" + "H" * len(types))
    marker(b"STRC")
    layouts = {}
    for _ in range(read("<I")[0]):
        type_index, count = read("<HH")
        offset = 0
        fields = {}
        for _ in range(count):
            field_type, field_name = read("<HH")
            name = names[field_name]
            fields[name] = offset
            elements = math.prod(int(n) for n in re.findall(r"\[(\d+)\]", name))
            offset += elements * (4 if "*" in name else lengths[field_type])
        if offset != lengths[type_index]:
            raise ValueError(f"unsupported DNA layout: {types[type_index]}")
        layouts[types[type_index]] = fields

    memory = {address: payload for _, address, payload in blocks}

    def integer(payload, layout, field):
        return struct.unpack_from("<I", payload, layouts[layout][field])[0]

    name_offset = layouts["Object"]["id"] + layouts["ID"]["name[24]"]
    obj = next(payload for code, _, payload in blocks
               if code == b"OB\0\0" and
               payload[name_offset:name_offset + 24].split(b"\0")[0] == b"OBKingdomLongsword")
    mesh = memory[integer(obj, "Object", "*data")]
    vertices = memory[integer(mesh, "Mesh", "*mvert")]
    faces = memory[integer(mesh, "Mesh", "*mface")]
    vertex_count = integer(mesh, "Mesh", "totvert")
    face_count = integer(mesh, "Mesh", "totface")
    lines = ["# KingdomLongsword from weapon_set_kingdom.blend, by pfunked.",
             "# Local Z-up coordinates; static mesh only, materials omitted.", "o KingdomLongsword"]
    for index in range(vertex_count):
        point = struct.unpack_from("<3f", vertices, index * 24)
        if not all(math.isfinite(value) for value in point):
            raise ValueError("nonfinite vertex")
        lines.append("v " + " ".join(format(value, ".9g") for value in point))
    for index in range(face_count):
        a, b, c, d = struct.unpack_from("<4I", faces, index * 20)
        corners = [a, b, c] + ([d] if d else [])
        if any(corner >= vertex_count for corner in corners):
            raise ValueError("invalid face index")
        lines.append("f " + " ".join(str(corner + 1) for corner in corners))
    destination.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Exported {vertex_count} vertices and {face_count} faces to {destination}")


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1] / "runtime/assets/lc-res"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=root / "weapon_set_kingdom.blend")
    parser.add_argument("--output", type=Path, default=root / "KingdomLongsword.obj")
    args = parser.parse_args()
    export(args.source, args.output)
