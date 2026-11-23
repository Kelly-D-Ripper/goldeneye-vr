"""Convert private Zomboid attachment meshes for the Tactical workbench."""
import json
import math
from pathlib import Path
import struct
import sys

import bpy
from mathutils import Vector


def texture_bytes(path, max_size=192):
    image = bpy.data.images.load(str(path))
    width, height = image.size
    size = min(max_size / max(width, height), 1.0)
    width, height = max(1, round(width * size)), max(1, round(height * size))
    if tuple(image.size) != (width, height):
        image.scale(width, height)
    raw = image.pixels[:]
    result = bytearray(struct.pack("<HH", width, height))
    for row in range(height - 1, -1, -1):
        line = raw[row * width * 4:(row + 1) * width * 4]
        for i, channel in enumerate(line):
            value = max(0.0, min(1.0, channel))
            result.append(round(255 * (value ** .82 if i % 4 != 3 else value)))
    bpy.data.images.remove(image)
    return result


def bake(part, source):
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.fbx(filepath=str(source / part["mesh"]), use_anim=False)
    light = Vector((-.4, -.25, -.8)).normalized()
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    if not meshes:
        raise ValueError("No imported mesh")
    packed = bytearray()
    for obj in meshes:
        if not obj.data.uv_layers.active:
            raise ValueError(f"Missing UV channel on {obj.name}")
        obj.data.calc_loop_triangles()
        count = len(obj.data.loop_triangles)
        limit = part.get("triangle_limit", 4800 if part["id"] in ROOTS else 2200)
        if count > limit:
            bpy.context.view_layer.objects.active = obj
            decimate = obj.modifiers.new("Workbench LOD", "DECIMATE")
            decimate.ratio = limit / count
            bpy.ops.object.modifier_apply(modifier=decimate.name)
        mesh = obj.data
        mesh.calc_loop_triangles()
        uv = mesh.uv_layers.active.data
        normal_matrix = obj.matrix_world.to_3x3().inverted_safe().transposed()
        for triangle in mesh.loop_triangles:
            for loop in triangle.loops:
                vertex = mesh.vertices[mesh.loops[loop].vertex_index]
                pos = obj.matrix_world @ vertex.co
                normal = (normal_matrix @ mesh.corner_normals[loop].vector).normalized()
                color = round(255 * (.59 + .41 * max(0, normal.dot(light))))
                st = uv[loop].uv
                if not all(math.isfinite(n) for n in (*pos, *st)):
                    raise ValueError("Invalid vertex")
                packed += struct.pack("<5f4BB3x", *pos, st.x, 1 - st.y, color, color, color, 255, 0)
    count = len(packed) // 84
    if not 0 < count <= 50000:
        raise ValueError(f"Unexpected triangle count {count}")
    texture = texture_bytes(source / part["texture"], part.get("texture_size", 192))
    return b"GEVRWPN1" + struct.pack("<I", 1) + texture + struct.pack("<I", count) + packed


ROOTS = {"5644bd2b4bdc2d3b4c8b4572", "583990e32459771419544dd2"}


def main(source, output):
    output.mkdir(parents=True, exist_ok=True)
    parts = json.loads((source / "parts.json").read_text(encoding="utf-8"))
    failures = []
    for part in parts:
        if not part["mesh"]:
            continue
        path = output / (part["id"] + ".bin")
        if path.is_file():
            continue
        try:
            data = bake(part, source)
            temporary = path.with_suffix(".tmp")
            temporary.write_bytes(data)
            temporary.replace(path)
            print("BAKED", part["id"], part["name"], len(data), flush=True)
        except (ValueError, RuntimeError) as exc:
            failures.append((part["id"], str(exc)))
            print("FAILED", part["id"], exc, flush=True)
    if failures:
        raise RuntimeError(f"{len(failures)} parts failed conversion")


if __name__ == "__main__":
    main(*map(Path, sys.argv[sys.argv.index("--") + 1:]))
