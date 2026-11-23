"""Prepare the AK workbench optics from the local Zomboid pack.

Run with Blender's Python. Reads the pack's lens boundaries, glass atlas
ownership, optical modes and reticle assets as data.
"""
import argparse
import io
import json
import math
import re
import struct
import sys
from pathlib import Path
from zipfile import ZipFile

import bpy
import numpy as np
from mathutils import Vector


def sight_records(blob):
    magic, version, count, strings, pool, directory, total = struct.unpack_from("<8s6I", blob)
    if magic != b"TWSIGHT\0" or version != 1 or total != len(blob) or directory != count * 12:
        raise ValueError("Invalid sight catalogue")
    stream = io.BytesIO(blob)
    stream.seek(32)

    def uint():
        return struct.unpack("<I", stream.read(4))[0]

    names = [stream.read(uint()).decode("utf-8") for _ in range(strings)]
    if stream.tell() != 32 + pool:
        raise ValueError("Invalid sight string table")
    entries = [(names[uint()], uint(), uint()) for _ in range(count)]

    def value(depth=0):
        if depth > 64:
            raise ValueError("Invalid sight record nesting")
        tag = stream.read(1)[0]
        if tag in (0, 1, 6):
            return None if tag == 6 else bool(tag)
        if tag == 2:
            return struct.unpack("<d", stream.read(8))[0]
        if tag == 3:
            return names[uint()]
        if tag == 4:
            return [value(depth + 1) for _ in range(uint())]
        if tag == 5:
            return {names[uint()]: value(depth + 1) for _ in range(uint())}
        raise ValueError(f"Unknown sight record field {tag}")

    records = {}
    for name, offset, count in entries:
        stream.seek(offset)
        records[name] = value()
        if stream.tell() != offset + count:
            raise ValueError(f"Truncated sight record {name}")
    return records


def reticle_meshes(blob):
    magic, version, count, directory, total = struct.unpack_from("<8s4I", blob)
    if magic != b"TWRMESH\0" or version != 1 or total != len(blob):
        raise ValueError("Invalid reticle mesh catalogue")
    stream = io.BytesIO(blob)
    stream.seek(24)

    def uint():
        return struct.unpack("<I", stream.read(4))[0]

    entries = [(stream.read(uint()).decode("utf-8"), uint(), uint()) for _ in range(count)]
    if stream.tell() != 24 + directory:
        raise ValueError("Invalid reticle mesh directory")
    meshes = {}
    for key, offset, _ in entries:
        stream.seek(offset)
        stream.read(uint())  # Source object name.
        count, submeshes = uint(), uint()
        uint()  # Blend shape count. Bake the reference marking.
        points = np.frombuffer(stream.read(count * 12), dtype="<f4").reshape(-1, 3)
        stream.read(count * 8)  # UVs are unused by the camera-reticle meshes.
        triangles = np.concatenate([np.frombuffer(stream.read(uint() * 4), dtype="<u4")
                                    for _ in range(submeshes)]).reshape(-1, 3)
        meshes[key] = (points, triangles)
    return meshes


def picture(path):
    source = bpy.data.images.load(str(path))
    source.colorspace_settings.name = "Non-Color"
    size = min(256 / max(source.size), 1.0)
    width, height = (max(1, round(size * v)) for v in source.size)
    if tuple(source.size) != (width, height):
        source.scale(width, height)
    pixels = np.empty(width * height * 4, dtype=np.float32)
    source.pixels.foreach_get(pixels)
    bpy.data.images.remove(source)
    return np.clip(pixels.reshape(height, width, 4)[::-1], 0, 1)


def mesh_mark(layer, meshes, fov):
    points, triangles = meshes[layer["mesh"]]
    view = points @ np.asarray(layer["rotation"]).T + np.asarray(layer["position"])
    distance = -view[:, 2]
    focal = float(layer["renderScale"]) / math.tan(math.radians(fov / 2))
    xy = np.column_stack((view[:, 0], -view[:, 1])) * (focal / np.maximum(distance, 0.0001))[:, None]
    size = 256
    xy = (xy * 0.5 + 0.5) * size
    mask = np.zeros((size, size), dtype=np.float32)
    for indexes in triangles:
        if min(distance[indexes]) <= 0.0001:
            continue
        a, b, c = xy[indexes]
        sides = b-a, c-b, a-c
        area = sides[0][0]*(c[1]-a[1]) - sides[0][1]*(c[0]-a[0])
        if abs(area) < 1e-9:
            continue
        lo = np.clip(np.floor(np.min([a, b, c], axis=0)-1).astype(int), 0, size - 1)
        hi = np.clip(np.ceil(np.max([a, b, c], axis=0)+1).astype(int), 0, size - 1)
        yy, xx = np.mgrid[lo[1]:hi[1]+1, lo[0]:hi[0]+1]
        x, y = xx + 0.5, yy + 0.5
        # Preserve thin source strokes that sit between raster sample centres.
        edges = [np.sign(area)*(d[0]*(y-p[1])-d[1]*(x-p[0]))/np.linalg.norm(d)
                 for p, d in zip((a, b, c), sides)]
        coverage = np.clip(.8 + np.minimum.reduce(edges), 0, 1)
        mask[yy, xx] = np.maximum(mask[yy, xx], coverage)
    image = np.zeros((size, size, 4), dtype=np.float32)
    image[:, :, 3] = mask
    return image


def pose_for(mode):
    ocular = mode["lens"]["ocular"]
    centre = np.asarray(ocular["center"]) * [1, 1, -1]
    normal = np.asarray(ocular["normal"]) * [1, 1, -1]
    if normal[1] < 0:
        normal = -normal
    normal /= np.linalg.norm(normal)
    right = np.cross([0, 0, -1], normal)
    right /= np.linalg.norm(right)
    up = np.cross(normal, right)
    frame = mode.get("scopeFrame")
    forward = np.asarray(frame)[:, 2] * [1, 1, -1] if frame else np.array([0, 1, 0])
    if forward[1] < .5:
        forward = np.array([0, 1, 0])
    boundary = ocular.get("boundary")
    if boundary:
        delta = np.asarray(boundary) * [1, 1, -1] - centre
        points = np.column_stack((delta @ right, delta @ up))
        points = sorted(points, key=lambda p: math.atan2(p[1], p[0]))
        extent = np.max(np.abs(points), axis=0)
        if len(points) > 64:
            points = np.asarray(points)[np.linspace(0, len(points), 64, endpoint=False).astype(int)]
    else:
        radius = ocular["radius"] or min(ocular["halfExtents"])
        extent = np.array([radius, radius])
        points = [(radius * math.cos(2 * math.pi * i / 32),
                   radius * math.sin(2 * math.pi * i / 32)) for i in range(32)]
    return centre, right, up, forward, extent, np.asarray(points) / extent


def zoom_for(mode, nominal):
    fov = mode["camera"].get("fovDegrees")
    interval = mode["camera"].get("minMaxFov")
    if mode["kind"] == "collimator":
        return 1., 1., 26.
    if interval:
        low_fov, high_fov = float(interval["x"]), float(interval["y"])
        low_zoom = max(1., round(26. / low_fov, 1))
        return low_zoom, low_zoom * low_fov / high_fov, low_fov
    if not fov:
        fov = 26. / max(1., nominal or 1.)
    zoom = nominal if nominal and nominal > 1 else max(1., round(24. / fov, 1))
    return zoom, zoom, fov


def package_modes(record, records, meshes, asset_dir, name, flip_housing, palettes):
    nominal = {m["profile"]: m.get("nominalZoom") for s in record["scopes"] for m in s["modes"] if "profile" in m}
    modes = []
    for mode in record["modes"]:
        if not (mode.get("lens") or {}).get("ocular") or not mode.get("reticles"):
            continue
        centre, right, up, forward, extent, points = pose_for(mode)
        low_zoom, high_zoom, fov = zoom_for(mode, nominal.get(mode["id"]))
        layer = mode["reticles"][0]
        kind = {"collimator": 1, "optic": 2, "nv": 3, "thermal": 4}[mode["kind"]]
        flags = kind | (256 if flip_housing and mode["id"] == "c0_m1" else 0)
        if record.get("focalPlane", {}).get("kind") == "ffp":
            flags |= 1 << 11
        if kind == 4 and mode.get("imageProfile"):
            flags |= palettes[int(mode["imageProfile"]["mode"]) - 1] << 9
        angular = 0.
        uv = (.5, .5)
        if layer["kind"] == "texture":
            image = picture(asset_dir / Path(layer["texture"]).name)
            material = records["materials/" + layer["material"]]
            if kind == 1:
                projection = layer["projection"]
                angular = projection.get("uvPerTangent", 17.)
                uv = projection.get("uvAtAxis", uv)
                # Collimator artwork stores its pattern in alpha.
                color = material.get("properties", {}).get("_Color", [1., .2, .09, 1.])
                image[:, :, :3] = np.clip(color[:3], 0, 1)
        else:
            image = mesh_mark(layer, meshes, fov)
        if np.max(image[:, :, 3]) < .01:
            # Subpixel rasterisation can lose a central wire in the 256px bake.
            y, x = (s//2 for s in image.shape[:2])
            image[y, x-12:x+13, 3] = image[y-12:y+13, x, 3] = 1.
            print("FILLED RETICLE", name, mode["id"], flush=True)
        height, width = image.shape[:2]
        values = [*centre, *right, *up, *forward, *extent, *uv, angular, low_zoom, high_zoom]
        modes.append(struct.pack("<4I19f", flags, len(points), width, height, *values)
                     + np.asarray(points, dtype="<f4").tobytes()
                     + np.rint(image * 255).astype(np.uint8).tobytes())
    if not modes:
        raise ValueError(f"Missing optical mode for {name}")
    return b"GEVROPT4" + struct.pack("<I", len(modes)) + b"".join(modes)


def tile_rows(java):
    rows = re.findall(r'new Row\("([^"]+)", "[^"]+", (\d+), (\d+), (\d+), (\d+), new int\[\] \{([^}]+)\}', java)
    return {ident: (int(columns), int(mask), int(removed)) for ident, columns, mask, _, removed, _ in rows}


def shell(part, source, row, texture_cache, mesh_file):
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.fbx(filepath=str(mesh_file), use_anim=False)
    light = Vector((-.4, -.25, -.8)).normalized()
    packed = bytearray()
    removed = 0
    expected = row[2] if row else 0
    for obj in [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]:
        mesh = obj.data
        mesh.calc_loop_triangles()
        uv = mesh.uv_layers.active.data
        world = obj.matrix_world
        normal_matrix = world.to_3x3().inverted_safe().transposed()
        positions = [world @ v.co for v in mesh.vertices]
        faces = []
        for tri in mesh.loop_triangles:
            if row:
                columns, mask, _ = row
                xp, yp = (sum(uv[loop].uv[axis] for loop in tri.loops) * columns / 3 for axis in (0, 1))
                x = max(0, min(columns - 1, math.floor(xp)))
                y = columns - 1 - max(0, min(columns - 1, math.floor(yp)))
                if mask & (1 << (y * columns + x)):
                    removed += 1
                    continue
            faces.append(tri)
        # The source atlas isolates the housing from the glass, before any LOD.
        # Keep small, separate controls and ring cutouts on complex optics.
        for tri in faces:
            for loop in tri.loops:
                pos = positions[mesh.loops[loop].vertex_index]
                normal = (normal_matrix @ mesh.corner_normals[loop].vector).normalized()
                shade = round(255 * (.59 + .41 * max(0, normal.dot(light))))
                st = uv[loop].uv
                packed += struct.pack("<5f4BB3x", *pos, st.x, 1 - st.y, shade, shade, shade, 255, 0)
    if row and removed < max(1, expected * .5):
        raise ValueError(f"Lens tile count differs ({removed}, expected {expected})")
    count = len(packed) // 84
    if not 0 < count <= 50000:
        raise ValueError(f"Optic housing triangle count {count}")
    texture = source / part["texture"]
    if texture not in texture_cache:
        # picture() reads the atlas as encoded bytes. Apply the same lift as
        # the Workshop mesh, without converting to linear for this renderer.
        tex = picture(texture)
        tex[:, :, :3] = tex[:, :, :3] ** .82
        texture_cache[texture] = (struct.pack("<HH", tex.shape[1], tex.shape[0])
                                  + np.rint(tex * 255).astype(np.uint8).tobytes())
    return b"GEVRWPN1" + struct.pack("<I", 1) + texture_cache[texture] + struct.pack("<I", count) + packed, removed


def build(archive, source, output):
    parts = json.loads((source / "parts.json").read_text(encoding="utf-8"))
    output.mkdir(parents=True, exist_ok=True)
    marks = output.parent / "optic-reticles"
    marks.mkdir(exist_ok=True)
    textures = {}
    with ZipFile(archive) as pack:
        prefix = next(p.rsplit("/viewpoint/", 1)[0] for p in pack.namelist()
                      if "/42/media/viewpoint/TW_ViewpointOptics.bin" in p)
        data = sight_records(pack.read(prefix + "/viewpoint/TW_ViewpointOptics.bin"))
        meshes = reticle_meshes(pack.read(prefix + "/viewpoint/TW_ReticleMeshes.bin"))
        java = next(p for p in pack.namelist() if "/42/" in p and p.endswith("/LensGlassData.java"))
        rows = tile_rows(pack.read(java).decode("utf-8"))
        effects = pack.read(prefix + "/lua/shared/TW/Data/Optics.lua").decode("utf-8")
        palettes = {key: [int(n) for n in re.findall(r'kind="thermal",palette=(\d+)', line)]
                    for key, line in re.findall(r'\["([0-9a-f]{24})"\]=\{modes=\{([^\r\n]+)', effects)}
        for resource in data.values():
            if resource.get("texture", "").startswith("media/textures/TW_Reticles/"):
                name = Path(resource["texture"]).name
                path = marks / name
                if not path.is_file():
                    path.write_bytes(pack.read(prefix + "/textures/TW_Reticles/" + name))
        count = 0
        failed = []
        for part in parts:
            ident, name = part["id"], part["name"]
            record = data.get("items/" + ident)
            if not record:
                continue
            count += 1
            meta, body = output / f"{ident}.opt", output / f"{ident}.bin"
            try:
                flip = name.startswith("HHS-1")
                if not meta.is_file() or meta.read_bytes()[:8] != b"GEVROPT4":
                    meta.write_bytes(package_modes(record, data, meshes, marks, name, flip, palettes.get(ident, [])))
                if not body.is_file():
                    model, removed = shell(part, source, rows.get(ident), textures, source / part["mesh"])
                    body.write_bytes(model)
                    print("OPTIC", ident, name, "removed", removed, flush=True)
                folded = output / f"{ident}-folded.bin"
                if flip and not folded.is_file():
                    state = f"States/{ident}_mode1"
                    model_path = source / f"models_X/TW/{state}.fbx"
                    model_path.parent.mkdir(exist_ok=True)
                    if not model_path.is_file():
                        model_path.write_bytes(pack.read(prefix + f"/models_X/TW/{state}.fbx"))
                    model, removed = shell(part, source, rows[state], textures, model_path)
                    folded.write_bytes(model)
                    print("FOLDED", ident, name, "removed", removed, flush=True)
            except (IndexError, KeyError, ValueError, RuntimeError) as error:
                failed.append(name)
                print("OPTIC FAILED", ident, name, repr(error), flush=True)
    if failed:
        raise ValueError(f"Failed optics: {', '.join(failed)}")
    print(f"{count} AK workbench optic profiles")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    build(**vars(parser.parse_args(sys.argv[sys.argv.index("--")+1:])))
