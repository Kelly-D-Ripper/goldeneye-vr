"""Bake the supplied Couturier KLMK finish onto a local 6B23 character mesh."""
import argparse
from pathlib import Path
import struct

import numpy as np
from PIL import Image
import UnityPy
from UnityPy.helpers.MeshHelper import MeshHandler


def vest_renderer(env):
    for obj in env.objects:
        if obj.type.name != "SkinnedMeshRenderer":
            continue
        renderer = obj.read()
        node = renderer.m_GameObject.read()
        if node.m_Name != "AR_6B23_lod0":
            continue
        transform = next(x.component.read() for x in node.m_Component if x.component.type.name == "Transform")
        if transform.m_Father.read().m_GameObject.read().m_Name.lower() in {"skin", "ar_6b23.skin"}:
            return renderer
    raise ValueError("6B23 character LOD0 missing from the bundle")


def prepare(finish, donor, output):
    paint = UnityPy.load(str(finish))
    desired = vest_renderer(paint)
    material = desired.m_Materials[0].read()
    diffuse = dict(material.m_SavedProperties.m_TexEnvs)["_MainTex"].m_Texture.read().image.convert("RGBA")
    diffuse.thumbnail((512, 512), Image.Resampling.LANCZOS)

    source = UnityPy.load(str(donor))
    reader = next((obj for obj in source.objects if obj.type.name == "Mesh" and obj.path_id == desired.m_Mesh.path_id), None)
    mesh = MeshHandler(reader.read() if reader else vest_renderer(source).m_Mesh.read())
    mesh.process()
    # The source mesh is Z-up with its chest towards -Y. Make it Y-up and
    # normalize the shell so the game can fit it to the animated chest bounds.
    points = np.asarray(mesh.m_Vertices)[:, (0, 2, 1)] * (1, 1, -1)
    minimum, maximum = points.min(axis=0), points.max(axis=0)
    points = (points - .5 * (minimum + maximum)) / (maximum - minimum)
    # Lengthen the skirt towards the shirt hem and let it wrap around the
    # flared coat. End the blend below the rig to retain the chest fit.
    skirt = np.clip((-.16 - points[:, 1]) / .34, 0, 1)
    points[:, 1] -= .22 * skirt
    points[:, 0] *= 1 + .14 * skirt
    points[:, 2] += .045 * skirt * np.sign(points[:, 2])
    normals = np.asarray(mesh.m_Normals)[:, (0, 2, 1)] * (1, 1, -1)
    shade = np.rint(255 * (.72 + .28 * np.clip(normals @ np.array((.23, .74, .63)), 0, 1))).astype(int)
    uv = np.asarray(mesh.m_UV0)
    if not np.isfinite(points).all() or not np.isfinite(uv).all():
        raise ValueError("6B23 mesh contains invalid coordinates")
    corner = struct.Struct("<5f4BB3x")
    faces = bytearray()
    for group in mesh.get_triangles():
        for triangle in group:
            for i in triangle:
                tint = int(shade[i])
                faces += corner.pack(*points[i], uv[i, 0], 1 - uv[i, 1], tint, tint, tint, 255, 0)
    triangles = len(faces) // (3 * corner.size)
    if not 0 < triangles <= 50000:
        raise ValueError(f"6B23 triangle count is invalid: {triangles}")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(b"GEVRWPN1" + struct.pack("<IHH", 1, *diffuse.size)
                       + diffuse.tobytes() + struct.pack("<I", triangles) + faces)
    print(f"{output.name}: {triangles} triangles, size {(maximum - minimum).round(3)} m, {diffuse.size} texture")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("finish", type=Path)
    parser.add_argument("donor", type=Path)
    parser.add_argument("output", type=Path)
    prepare(**vars(parser.parse_args()))
