"""Refit baked guard helmets to the native head and jaw landmarks."""

import argparse
from dataclasses import dataclass
from pathlib import Path
from struct import Struct, pack, unpack_from

import numpy as np


CORNER = Struct("<5f5B3x")


@dataclass(frozen=True)
class HelmetFit:
    drop: float
    chin_lift: float = 0.
    mask_texture: int = -1
    tuck_visor_latch: bool = False


OPEN_HELMETS = {
    "zsh-kamysh-grey": HelmetFit(.043, tuck_visor_latch=True),
    "fast-black-side": HelmetFit(.049),
    "fast-black-side-trooper": HelmetFit(.048, .016, mask_texture=3),
    "fast-mt-trooper-black": HelmetFit(.047, .016, mask_texture=2),
    "airframe-tan": HelmetFit(.047, .018),
    "airframe-tan-ears": HelmetFit(.047, .018),
    "6b47-emr-arctic": HelmetFit(.053),
}
HELMET_NAMES = ("ssh68", "altyn-od-closed", *OPEN_HELMETS)


def blend(value):
    t = np.clip(value, 0., 1.)
    return t * t * (3. - 2. * t)


def fit_open_helmet(points, material, fit):
    x, y, z = points.T
    # Draw in the oversized crown and brim around the forehead. Keep the
    # rear of the shell deep enough to cover the back of the native skull.
    crown = blend((y - .018) / .092)
    lower_jaw = blend((-.058 - y) / .068)
    front = blend((z - .032) / .075)
    back = blend((-z - .034) / .064)
    narrow = 1. - blend((np.abs(x) - .056) / .044)
    webbing = lower_jaw * front * narrow
    webbing[material == fit.mask_texture] = 0.
    if fit.tuck_visor_latch:
        latch = blend((x - .083) / .015) * lower_jaw * blend((z - .063) / .043)

    # Chin padding and buckles move in together while their loops retain
    # their upper anchors. Rigid cheek protection keeps its own shape.
    x *= (.91 + .065 * back) * (1. - .15 * webbing)
    z *= np.where(z >= 0., .81, .98)
    z -= .024 * webbing + .022 * back
    y += fit.chin_lift * webbing + .010 * back * (1. - lower_jaw) - fit.drop - .016 * crown
    if fit.tuck_visor_latch:
        x *= 1. - .119 * latch
        y += .034 * latch
        z -= .043 * latch


def fit_ssh_shell(points):
    x, y, z = points.T
    dome = blend((y + .005) / .050)
    back = blend((-z - .045) / .055)
    # Leave space for the temples without raising the rim or flattening the dome.
    y += .008 * dome - .036
    z *= np.where(z >= 0., .75 + .36 * dome, .84 + .12 * back)
    x *= .94 + .09 * dome + .12 * back


def ssh_webbing(texture):
    # The baked SSh-68 shell has no webbing. Add a slim strap from the
    # inside of each ear rim, over the jaw corners and under the chin.
    right = np.array([
        (0.,   -.180, .064),
        (.029, -.168, .077),
        (.047, -.150, .069),
        (.061, -.127, .053),
        (.075, -.096, .028),
        (.089, -.064, .005),
    ])
    left = right[:0:-1] * (-1., 1., 1.)
    curve = np.vstack((left, right))
    tangents = np.gradient(curve, axis=0)
    outward = np.stack((curve[:, 0] / .069, np.full(len(curve), -.28),
                        np.full(len(curve), .8)), axis=1)
    span = np.cross(tangents, outward)
    span *= .006 / np.linalg.norm(span, axis=1, keepdims=True)
    edges = np.stack((curve - span, curve + span), axis=1)
    corners = bytearray()
    for i in range(len(curve) - 1):
        distance = .5 * i
        for j, edge in ((0, 0), (1, 0), (1, 1), (0, 0), (1, 1), (0, 1)):
            shade = 222 if edge else 198
            corners.extend(CORNER.pack(*edges[i + j, edge], distance + .5*j,
                                       .75 if edge else .25,
                                       shade, shade, shade, 255, texture))
    # Small repeating weave in olive drab.
    texels = bytes(value for row in range(8) for col in range(8)
                   for value in ((77, 73, 51, 255) if (row + col) % 2
                                 else (66, 65, 45, 255)))
    return pack("<HH", 8, 8) + texels, corners


def fit_helmet(source, target):
    data = bytearray(source.read_bytes())
    if data[:8] != b"GEVRWPN1":
        raise ValueError(f"Unexpected helmet format in {source}")
    textures, = unpack_from("<I", data, 8)
    offset = 12
    for _ in range(textures):
        width, height = unpack_from("<HH", data, offset)
        offset += 4 + width * height * 4
    triangles, = unpack_from("<I", data, offset)
    mesh_offset = offset + 4
    if not triangles or len(data) != mesh_offset + 3 * triangles * CORNER.size:
        raise ValueError(f"Invalid helmet triangle data in {source}")

    points = np.ndarray((triangles * 3, 3), dtype="<f4", buffer=data,
                        offset=mesh_offset, strides=(CORNER.size, 4))
    if source.stem == "altyn-od-closed":
        # Keep the established visor-to-collar coverage. Draw down the
        # crown without pushing the visor or chin skirt into the vest.
        y, z = points[:, 1], points[:, 2]
        toward_chin = np.clip((.005 - y) / .145, 0., 1.)
        toward_face = np.clip((z + .055) / .150, 0., 1.)
        y -= .027 + .047 * toward_chin * toward_face + .024 * blend((y - .020) / .090)
        points[:, 0] *= .97
    elif source.stem == "ssh68":
        fit_ssh_shell(points)
        texture, strap = ssh_webbing(textures)
        target.write_bytes(data[:8] + pack("<I", textures + 1) + data[12:offset] + texture
                           + pack("<I", triangles + len(strap) // (3 * CORNER.size))
                           + data[mesh_offset:] + strap)
        return
    else:
        materials = np.ndarray(triangles * 3, dtype=np.uint8, buffer=data,
                               offset=mesh_offset + 24, strides=(CORNER.size,))
        fit_open_helmet(points, materials, OPEN_HELMETS[source.stem])
    target.write_bytes(data)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("helmets", nargs="+", choices=HELMET_NAMES)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error("Choose an output folder separate from the original meshes.")
    args.output.mkdir(parents=True, exist_ok=True)
    for helmet in args.helmets:
        file = helmet + ".bin"
        fit_helmet(args.source / file, args.output / file)
