"""Fit the locally available open-faced helmet mesh to GoldenEye's helmet mount."""
import argparse
from pathlib import Path
import struct

import numpy as np
from PIL import Image
import UnityPy
from UnityPy.helpers.MeshHelper import MeshHandler


def prepare(bundle: Path, output: Path) -> None:
    env = UnityPy.load(str(bundle))
    asset_name = "item_equipment_helmet_ssh-68_LOD0"
    assets = {
        asset.m_Name: asset
        for reader in env.objects
        if reader.type.name in {"Mesh", "Texture2D"}
        for asset in [reader.read()]
    }
    mesh = MeshHandler(assets[asset_name])
    mesh.process()
    texture = assets[f"{asset_name}_diff"].image.convert("RGBA")
    texture.thumbnail((512, 512), Image.Resampling.LANCZOS)

    # Convert from Z-up with its visor at -Y to the native hat's Y-up,
    # +Z-forward mount. The crown and brim are fitted within the stock hat
    # envelope, with a little more room alongside the head.
    points = np.asarray(mesh.m_Vertices)[:, (0, 2, 1)] * (1, 1, -1)
    low, high = points.min(axis=0), points.max(axis=0)
    points = (points - low) / (high - low)
    points = points * (.222, .155, .299) - (.111, .071, .146)

    normals = np.asarray(mesh.m_Normals)[:, (0, 2, 1)] * (1, 1, -1)
    light = np.clip(normals @ np.array((.25, .90, .36)), 0, 1)
    shade = np.rint(255 * (.69 + .31 * light)).astype(int)
    uv = np.asarray(mesh.m_UV0)
    if not np.isfinite(points).all() or not np.isfinite(uv).all():
        raise ValueError("Helmet has invalid geometry or UVs")

    corner = struct.Struct("<5f4BB3x")
    faces = bytearray()
    for group in mesh.get_triangles():
        for triangle in group:
            for index in triangle:
                tint = int(shade[index])
                faces.extend(corner.pack(*points[index], uv[index, 0], 1 - uv[index, 1],
                                         tint, tint, tint, 255, 0))
    triangles = len(faces) // (3 * corner.size)
    if not 0 < triangles <= 50000:
        raise ValueError(f"Invalid helmet triangle count: {triangles}")

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(b"GEVRWPN1" + struct.pack("<IHH", 1, *texture.size) + texture.tobytes()
                       + struct.pack("<I", triangles) + faces)
    print(f"{output.name}: {triangles} triangles, {texture.size} texture")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("output", type=Path)
    prepare(**vars(parser.parse_args()))
