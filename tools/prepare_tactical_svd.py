"""Build the Tactical SVD from the locally supplied WTT Armory prefabs."""
import argparse
import json
import struct
from pathlib import Path

import numpy as np
import UnityPy
from PIL import Image
from UnityPy.helpers.MeshHelper import MeshHandler
from prepare_tactical_gear import extract_parts


SVD_ID = "6657bc8faeddd6b0a9b40224"
ORIGIN = np.array((.1327, -.060, .269))
SCALE = 1.17


def transform(node):
    p, q, s = node.m_LocalPosition, node.m_LocalRotation, node.m_LocalScale
    x, y, z, w = q.x, q.y, q.z, q.w
    rotation = np.array(((1 - 2*(y*y + z*z), 2*(x*y - z*w), 2*(x*z + y*w)),
                         (2*(x*y + z*w), 1 - 2*(x*x + z*z), 2*(y*z - x*w)),
                         (2*(x*z - y*w), 2*(y*z + x*w), 1 - 2*(x*x + y*y))))
    matrix = np.eye(4)
    matrix[:3, :3] = rotation * (s.x, s.y, s.z)
    matrix[:3, 3] = (p.x, p.y, p.z)
    return transform(node.m_Father.read()) @ matrix if node.m_Father else matrix


def prefabs(path, skip_unmapped=False, anchor_frames=False, resources=(), lod0=False, min_height=None):
    env = UnityPy.load(str(path), *map(str, resources))
    primary = env.files[str(path)]
    contents = tuple(primary.files.values())
    objects = [reader for reader in env.objects if reader.assets_file in contents]
    transforms = {reader.read().m_GameObject.path_id: reader.read()
                  for reader in objects if reader.type.name == "Transform"}
    materials = {reader.read().m_GameObject.path_id: reader.read().m_Materials
                 for reader in objects if reader.type.name == "MeshRenderer"}
    poses = {ident: transform(node) for ident, node in transforms.items()}
    anchors = {node.m_GameObject.read().m_Name: poses[ident] for ident, node in transforms.items()
               if min_height is None or poses[ident][1, 3] >= min_height}
    if not anchor_frames:
        anchors = {name: frame[:3, 3] for name, frame in anchors.items()}
    meshes = []
    for reader in objects:
        if reader.type.name != "MeshFilter":
            continue
        item = reader.read()
        if not item.m_Mesh:
            continue
        name = item.m_GameObject.read().m_Name
        pose = poses[item.m_GameObject.path_id]
        if min_height is not None and pose[1, 3] < min_height:
            continue
        if lod0 and name.lower().rsplit("lod", 1)[-1] in ("1", "2", "3", "4"):
            continue
        try:
            mesh = MeshHandler(item.m_Mesh.read())
        except (FileNotFoundError, KeyError):
            if skip_unmapped:
                continue
            raise
        mesh.process()
        vertices = np.asarray(mesh.m_Vertices) @ pose[:3, :3].T + pose[:3, 3]
        normals = np.asarray(mesh.m_Normals)[:, :3] @ np.linalg.inv(pose[:3, :3])
        uv = np.asarray(mesh.m_UV0)
        for triangles, material in zip(mesh.get_triangles(), materials[item.m_GameObject.path_id]):
            try:
                properties = material.read().m_SavedProperties
            except (FileNotFoundError, ValueError, KeyError):
                if skip_unmapped:
                    continue
                raise
            for key, value in properties.m_TexEnvs:
                if key == "_MainTex" and value.m_Texture:
                    try:
                        tex = value.m_Texture.read()
                    except (FileNotFoundError, ValueError, KeyError):
                        if skip_unmapped:
                            continue
                        raise
                    meshes.append((name, vertices, normals, uv, np.asarray(triangles), tex))
                    break
            else:
                if not skip_unmapped:
                    raise ValueError(f"Missing diffuse texture on {name}")
    return meshes, anchors


def rgba(picture):
    picture = picture.copy()
    picture.thumbnail((512, 512), Image.Resampling.LANCZOS)
    return struct.pack("<HH", *picture.size) + picture.tobytes()


def cover_finish(picture):
    # WTT supplies a green camo cover. Retain its surface detail in blued steel.
    strength = np.asarray(picture.convert("L"), dtype=np.float32) * .48 + 9
    steel = np.asarray(picture).copy()
    steel[:, :, :3] = np.clip(strength[:, :, None] + (0, 1, 2), 0, 255).astype(np.uint8)
    return Image.fromarray(steel)


def write_mesh(path, parts, flip_up):
    textures = []
    materials = {}
    atlas = Image.new("RGBA", (512, 512)) if flip_up else None
    faces = bytearray()
    corner = struct.Struct("<5f5B3x").pack
    light = np.array((.28, .87, -.4))
    for group, model, offset, long_shift in parts:
        name, points, normals, uv, triangles, tex = model
        key = (group, tex.m_Name)
        if key not in materials:
            picture = tex.image.convert("RGBA")
            if group == "cover":
                picture = cover_finish(picture)
            materials[key] = len(textures)
            textures.append(rgba(picture))
            if atlas:
                tile = materials[key]
                if tile >= 16:
                    raise ValueError("SVD gear preview exceeds its texture atlas")
                atlas.paste(picture.resize((124, 124), Image.Resampling.LANCZOS),
                            ((tile % 4) * 128 + 2, (tile // 4) * 128 + 2))
        index = materials[key]
        pos = points + offset
        if long_shift:
            # Extend the straight section between the furniture and the front sight.
            pos[:, 2] += np.clip((pos[:, 2] - .83) / .10, 0, 1) * long_shift
        pos = (pos - ORIGIN)[:, (0, 2, 1)] * SCALE
        if flip_up:
            pos[:, 2] *= -1
        shades = np.rint(255 * (.63 + .37 * np.maximum(0, normals @ light))).clip(0, 255)
        for triangle in triangles:
            for vertex in triangle:
                x, y, z = pos[vertex]
                s, t = uv[vertex][:2]
                t = 1 - t
                if atlas:
                    s = ((index % 4) * 128 + 2 + 123 * np.clip(s, 0, 1)) / 512
                    t = ((index // 4) * 128 + 2 + 123 * np.clip(t, 0, 1)) / 512
                shade = int(shades[vertex])
                faces.extend(corner(x, y, z, s, t, shade, shade, shade, 255, 0 if atlas else index))
    if atlas:
        textures = [struct.pack("<HH", *atlas.size) + atlas.tobytes()]
    ntri = len(faces) // 84
    if not 0 < ntri <= 50000 or len(textures) > 32:
        raise ValueError(f"Unsupported SVD dimensions: {ntri} triangles, {len(textures)} textures")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"GEVRWPN1" + struct.pack("<I", len(textures)) + b"".join(textures)
                     + struct.pack("<I", ntri) + faces)
    print(f"{path.name}: {ntri} triangles, {len(textures)} textures")


def add_catalog(input_file, output_file, source, archive, anchors):
    lines = input_file.read_text(encoding="utf-8").splitlines()
    count = int(lines[1])
    if lines[0] != "GEVRGEAR2" or len(lines) < count:
        raise ValueError("Unsupported Tactical gear catalog")
    rows = lines[2:2+count]
    indices = {row.split("\t", 1)[0]: i for i, row in enumerate(rows)}
    props = json.loads((source / "WeaponDragunov.json").read_text(encoding="utf-8-sig"))[SVD_ID]["overrideProperties"]
    slots = {slot["_name"]: slot["_props"]["filters"][0]["Filter"] for slot in props["Slots"]}
    side = slots["mod_mount_000"]
    forward = slots["mod_mount_002"]
    added = list(dict.fromkeys(part for part in side + forward if part not in indices))
    mount_source = output_file.parent.parent / "mount-source"
    mounts, _ = extract_parts(archive, mount_source, added, include_children=False)
    (mount_source / "parts.json").write_text(json.dumps(mounts, indent=2), encoding="utf-8")
    for mount in mounts:
        if not mount["mesh"]:
            raise ValueError(f"Missing mesh for SVD mount {mount['name']}")
        indices[mount["id"]] = len(rows)
        rows.append(f"{mount['id']}\t{mount['capacity']}\t1\t{mount['name']}")

    tail = lines[2+count:]
    p = 0
    for _ in range(count):
        conflicts = int(tail[p].split()[0])
        if len(tail[p].split()) != conflicts + 1:
            raise ValueError("Unexpected catalog conflict row")
        p += 2 + int(tail[p + 1].split()[0])
    sockets = []
    for mount in mounts:
        conflicts = [indices[part] for part in mount["conflicts"] if part in indices]
        sockets.append(" ".join(map(str, [len(conflicts), *conflicts])))
        sockets.append(str(len(mount["sockets"])))
        for slot in mount["sockets"]:
            allowed = [indices[part] for part in slot["parts"] if part in indices]
            frame = " ".join(f"{n:.9g}" for n in slot["frame"])
            options = " ".join(map(str, allowed))
            sockets.append(f"{slot['name']}\t{slot['label']}\t{int(slot['required'])}\t{len(allowed)}\t{frame}\t{options}")

    # Compare the same two side rail anchors on the WTT and Zomboid receivers.
    # The optic pack's lateral axis runs opposite the WTT stock and receiver.
    anchor = anchors["mod_mount_000"]
    delta = (anchors["mod_mount_002"] - anchor) * SCALE
    size = delta[2] / .0245815
    x, y, z = (anchor - ORIGIN) * SCALE
    frame = f"{-size:.6f} 0 0 {x:.6f} 0 {size:.6f} 0 {z:.6f} 0 0 {size:.6f} {y:.6f}"
    alternatives = f"offset {len(side)} {delta[0]:.6f} {delta[2]:.6f} {delta[1]:.6f}"
    opts = " ".join(str(indices[part]) for part in side + forward)
    sockets += ["0", "1", f"mod_mount_000\tDovetail\t0\t{len(side)+len(forward)}\t{frame}\t{opts} {alternatives}"]
    root_index = len(rows)
    rows.append(f"{SVD_ID}\t0\t1\tSVD Dragunov")
    tail[p:p] = sockets
    scope = indices["5c82342f2e221644f31c060e"]  # PSO-1 model
    tail += [f"{root_index} 1", f"mod_mount_000 {scope}"]
    output_file.parent.mkdir(parents=True, exist_ok=True)
    output_file.write_text("\n".join([lines[0], str(len(rows)), *rows, *tail]) + "\n", encoding="utf-8")
    print(f"SVD side rail: {len(side)} dovetail options, {len(forward)} forward fittings; mesh scale {size:.4f}")


def build(source, destination, input_catalog, optic_archive):
    body, anchors = prefabs(source / "weapon_dragunov_container.bundle")
    magazine, _ = prefabs(source / "svd_polymermag10.bundle")
    barrel, barrel_anchors = prefabs(source / "svdshortenedbarrel.bundle")
    muzzle, muzzle_anchors = prefabs(source / "dragunovflashhider.bundle")
    stock, _ = prefabs(source / "svdwoodenstock.bundle")
    handguard, _ = prefabs(source / "svdwoodenhandguard.bundle")
    cover, _ = prefabs(source / "svdgreencamodustcover.bundle")
    barrel_socket = anchors["mod_barrel"]
    barrel_origin = next(v for k, v in barrel_anchors.items() if "565mm" in k)
    barrel_offset = barrel_socket - barrel_origin
    extension = .145
    muzzle_socket = barrel_anchors["mod_muzzle"] + barrel_offset + (0, 0, extension)
    muzzle_origin = next(v for k, v in muzzle_anchors.items() if "flashhider" in k.lower())
    muzzle_offset = muzzle_socket - muzzle_origin
    zero = np.zeros(3)
    parts = [("magazine", mesh, anchors["mod_magazine"], 0.) for mesh in magazine]
    parts += [("bolt", mesh, zero, 0.) for mesh in body if "_bolt" in mesh[0]]
    parts += [("body", mesh, zero, 0.) for mesh in body if "_bolt" not in mesh[0] and mesh[0] != "FrameInterior"]
    parts += [("cover", mesh, anchors["mod_reciever"], 0.)
              for mesh in cover if mesh[0] == "dragunovgreendustcover"]
    parts += [("barrel", mesh, barrel_offset, extension) for mesh in barrel]
    parts += [("muzzle", mesh, muzzle_offset, 0.) for mesh in muzzle]
    for meshes in (stock, handguard):
        parts += [("wood", mesh, zero, 0.) for mesh in meshes]
    write_mesh(destination / "svd-wood.bin", parts, False)
    write_mesh(destination / "gear/meshes" / f"{SVD_ID}.bin", parts, True)
    add_catalog(input_catalog, destination / "gear/catalog.txt", source, optic_archive, anchors)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("input_catalog", type=Path)
    parser.add_argument("optic_archive", type=Path)
    build(**vars(parser.parse_args()))
