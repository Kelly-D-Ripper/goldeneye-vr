"""Fit locally supplied equipment prefabs to the Tactical campaign guard mounts."""
import argparse
from pathlib import Path

import numpy as np
from PIL import Image
import UnityPy
from UnityPy.helpers.MeshHelper import MeshHandler

from prepare_tactical_expanded import save_mesh
from prepare_tactical_roster import CORNER
from prepare_tactical_svd import rgba, transform
from seat_guard_helmets import blend


# Pickup vests stand Z-up and face -Y. The chest mount is Y-up and +Z-forward.
VEST_AXES = np.array(((1, 0, 0), (0, 0, -1), (0, 1, 0)), dtype=float)


def prefab(path, libraries=()):
    env = UnityPy.load(*(str(p) for p in (path, *libraries)))
    poses, materials = {}, {}
    for obj in env.objects:
        if obj.type.name == "Transform":
            node = obj.read()
            poses[node.m_GameObject.path_id] = node
        elif obj.type.name == "MeshRenderer":
            node = obj.read()
            materials[node.m_GameObject.path_id] = node.m_Materials

    meshes = []
    for obj in env.objects:
        if obj.type.name != "MeshFilter":
            continue
        node = obj.read()
        name = node.m_GameObject.read().m_Name.lower()
        if not name.endswith("lod0") or "_shadow" in name or not node.m_Mesh:
            continue
        mesh = MeshHandler(node.m_Mesh.read())
        mesh.process()
        pose = transform(poses[node.m_GameObject.path_id])
        points = np.asarray(mesh.m_Vertices) @ pose[:3, :3].T + pose[:3, 3]
        normals = np.asarray(mesh.m_Normals)[:, :3] @ np.linalg.inv(pose[:3, :3])
        uv = np.asarray(mesh.m_UV0)
        for faces, material in zip(mesh.get_triangles(), materials[node.m_GameObject.path_id]):
            texture = next(value.m_Texture.read() for key, value in
                           material.read().m_SavedProperties.m_TexEnvs if key == "_MainTex")
            meshes.append((points, normals, uv, np.asarray(faces), texture))
    if not meshes:
        raise ValueError(f"No wearable mesh in {path}")
    anchors = {node.m_GameObject.read().m_Name: transform(node) for node in poses.values()
               if node.m_GameObject.read().m_Name.startswith("mod_")}
    return meshes, anchors


def fit(meshes, lower, upper, axes=np.eye(3), reference=None):
    points = np.concatenate([m[0] @ axes for m in (reference if reference is not None else meshes)])
    low, high = points.min(axis=0), points.max(axis=0)
    scale = (np.asarray(upper) - lower) / (high - low)
    return [((v @ axes - low) * scale + lower, n @ axes / scale, uv, f, t)
            for v, n, uv, f, t in meshes]


def move(meshes, shift):
    return [(p + shift, n, uv, f, t) for p, n, uv, f, t in meshes]


def attach_trooper_mask(meshes, mask_mounts, helmet_mounts):
    # The mask's pickup origin is at its attachment. Its NVG socket gives
    # the asset's reference orientation, while the helmet socket sets the pitch.
    socket = helmet_mounts["mod_nvg"]
    rotation = socket[:3, :3] @ np.linalg.inv(mask_mounts["mod_nvg"][:3, :3])
    normal_matrix = np.linalg.inv(rotation)
    return [(v @ rotation.T + socket[:3, 3], n @ normal_matrix, uv, f, t)
            for v, n, uv, f, t in meshes]


def visor_clearance(meshes):
    fitted = []
    for points, normals, uv, faces, texture in meshes:
        # Keep the rear pivots seated. The face of the shield arcs just
        # ahead of the Trooper mask so the two surfaces do not overlap.
        bend = np.clip((points[:, 2] - .060) / .110, 0., 1.)
        points = points.copy()
        points[:, 1] -= .012 * bend
        points[:, 2] += .025 * bend
        fitted.append((points, normals, uv, faces, texture))
    return fitted


def jacket(texture, finish):
    picture = texture.image.convert("RGBA")
    data = np.asarray(picture).copy()
    if finish == "clear-visor":
        data[:, :, :3] = np.clip(data[:, :, :3] * .34 + (120, 137, 147), 0, 255)
        data[:, :, 3] = 255
        return Image.fromarray(data)
    if finish in ("black", "visor"):
        light = np.asarray(picture.convert("L"), dtype=np.float32)
        data[:, :, :3] = np.clip((light * .49 + 16)[..., None] + (0, 1, 2), 0, 255)
    if "glass" in texture.m_Name.lower():
        tint = (42, 51, 59) if finish == "visor" else (24, 27, 23)
        data[:, :, :3] = np.clip(data[:, :, :3] * .48 + tint, 0, 255)
        data[:, :, 3] = 255
    return Image.fromarray(data)


def bake(path, batches):
    textures, indexes, corners = [], {}, bytearray()
    for meshes, finish in batches:
        opacity = 75 if finish == "clear-visor" else 255
        for points, normals, uv, faces, texture in meshes:
            key = (texture.m_Name, finish)
            if key not in indexes:
                indexes[key] = len(textures)
                textures.append(rgba(jacket(texture, finish)))
            length = np.maximum(np.linalg.norm(normals, axis=1), 1e-6)
            lighting = np.maximum(0, normals @ (.27, .91, .31) / length)
            shade = np.rint(255 * (.73 + .27 * lighting)).clip(0, 255).astype(int)
            if not np.isfinite(points).all() or not np.isfinite(uv).all():
                raise ValueError(f"Invalid geometry in {path.name}")
            for face in faces:
                for i in face:
                    s, t = uv[i, :2]
                    k = int(shade[i])
                    corners.extend(CORNER.pack(*points[i], s, 1-t, k, k, k, opacity, indexes[key]))
    save_mesh(path, textures, corners)


def build_slick(equipment, output):
    parts, _ = prefab(equipment / "armor_slick/ar_slick.bundle")
    layers = []
    for points, normals, uv, faces, texture in fit(parts, (-.494,-.605,-.445), (.494,.430,.508), VEST_AXES):
        x, y, z = points.T
        # Lift the upper front panel toward the chin. Blend back into the
        # hem, arm openings and the existing shoulder strap anchors.
        front = blend((z - .015) / .240)
        shoulders = np.clip((.430 - y) / .210, 0., 1.)
        upper = blend((y + .310) / .530) * shoulders
        width = 1. - .60 * blend((np.abs(x) - .135) / .300)
        collar = front * upper * width
        fitted = points.copy()
        fitted[:, 1] += .160 * collar
        fitted[:, 2] -= .026 * blend((y - .035) / .185) * collar
        layers.append((fitted, normals, uv, faces, texture))
    bake(output, [(layers, "source")])


def build_fast_trooper(equipment, output):
    def helmet(name):
        return prefab(equipment / name / f"item_equipment_{name}.bundle")

    fast, fast_mounts = helmet("helmet_hops_core_fast")
    mask, mask_mounts = helmet("helmet_tk_heavy_trooper")
    visor, _ = helmet("helmet_opscore_fast_visor")
    headset, _ = helmet("helmet_opscore_rac_headset")
    fast_bounds = ((-.111,-.158,-.119), (.111,.111,.166))
    mask = fit(attach_trooper_mask(mask, mask_mounts, fast_mounts), *fast_bounds, reference=fast)
    closed_visor = fit(move(visor, fast_mounts["mod_equipment_000"][:3, 3]), *fast_bounds, reference=fast)
    ear_cups = fit(move(headset, fast_mounts["mod_equipment_001"][:3, 3]), *fast_bounds, reference=fast)
    bake(output, [
        (fit(fast, *fast_bounds), "black"), (mask, "black"),
        (ear_cups, "black"), (visor_clearance(closed_visor), "visor"),
    ])


def build(equipment, korund, output):
    output.mkdir(parents=True, exist_ok=True)
    # Use the pickup geometry for the attachments and the FAST shell as a
    # common reference so adding wearables cannot resize the fitted helmet.
    korund_parts, _ = prefab(korund)
    bake(output / "korund-vm-flora.bin", [
        (fit(korund_parts, (-.560,-.740,-.507), (.560,.540,.578), VEST_AXES), "source")])
    build_slick(equipment, output / "lbt-slick-black.bin")

    def helmet(name, libraries=()):
        return prefab(equipment / name / f"item_equipment_{name}.bundle", libraries)

    altyn_material = equipment / "customizable/item_equipment_helmet_altyn/helmet_altyn_textures"
    shell, anchors = helmet("helmet_altyn", (altyn_material,))
    shield, _ = helmet("helmet_altyn_shield", (altyn_material,))
    # Keep the shield in the closed pose at the shell's front attachment.
    combined = [*shell, *move(shield, anchors["mod_equipment"][:3, 3])]
    bake(output / "altyn-od-closed.bin", [
        (fit(combined, (-.126,-.170,-.146), (.126,.110,.198)), "source")])

    build_fast_trooper(equipment, output / "fast-mt-trooper-black.bin")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("equipment", type=Path)
    parser.add_argument("korund", type=Path)
    parser.add_argument("output", type=Path)
    build(**vars(parser.parse_args()))

