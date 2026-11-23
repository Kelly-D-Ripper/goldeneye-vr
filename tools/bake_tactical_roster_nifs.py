"""Bake the extra PPK and Skorpion parts from the supplied Fallout archives."""
from pathlib import Path
import struct
import sys

import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bake_tactical_gear import texture_bytes


def import_meshes(path):
    before = set(bpy.data.objects)
    result = bpy.ops.import_scene.nif(filepath=str(path), animation=False,
                                      import_kf_files=False, use_custom_normals=True)
    if "FINISHED" not in result:
        raise ValueError(path)
    bpy.context.view_layer.update()
    return {obj.name.split(":")[0]: obj for obj in bpy.data.objects
            if obj not in before and obj.type == "MESH"}


def bake(path, pieces):
    textures, faces = {}, bytearray()
    light = Vector((-.4, -.25, .8)).normalized()
    for obj, fit, diffuse in pieces:
        slot = textures.setdefault(diffuse, len(textures))
        pose = fit @ obj.matrix_world
        normal_pose = pose.to_3x3().inverted_safe().transposed()
        mesh = obj.data
        mesh.calc_loop_triangles()
        uv = mesh.uv_layers.active.data
        for tri in mesh.loop_triangles:
            for loop in tri.loops:
                vertex = mesh.vertices[mesh.loops[loop].vertex_index]
                p = pose @ vertex.co
                normal = (normal_pose @ mesh.corner_normals[loop].vector).normalized()
                shade = round(255 * (.59 + .41 * max(0, normal.dot(light))))
                s, t = uv[loop].uv
                faces += struct.pack("<5f4BB3x", *p, s, 1 - t, shade, shade, shade, 255, slot)
    count = len(faces) // 84
    if not 0 < count <= 50000:
        raise ValueError(f"{path.name}: invalid triangle count {count}")
    path.write_bytes(b"GEVRWPN1" + struct.pack("<I", len(textures))
                     + b"".join(texture_bytes(tex) for tex in textures)
                     + struct.pack("<I", count) + faces)
    print(path.name, count, "triangles", flush=True)


def ppk(root, out):
    base = root / "private-assets/fallout-ppk-original"
    materials = base / "textures/tigerx87/weapons/ppk"
    parts = import_meshes(base / "meshes/tigerx87/weapons/ppk/ppk ext blk.nif")
    scale = .155 / (1.0834664 + .4371977)
    fit = Matrix(((0, 0, scale, 0), (scale, 0, 0, .109), (0, scale, 0, -.025), (0, 0, 0, 1)))
    bake(out / "ppk-brown-grip.bin", [(parts["PPK_Grip"], fit, materials / "ppk_D.dds")])
    bake(out / "ppk-extended-mag.bin", [
        (parts["PPK_mag"], fit, materials / "ppk_D.dds"),
        (parts["PPK_mag001"], fit, materials / "black/BlackGrip_D.dds"),
    ])


def skorpion(root, out):
    source = root / "private-assets/fallout-scorpio-original"
    nifs = source / "Meshes/Weapons/DamnApocalypse/Skorpion"
    diffuse = source / "Textures/Weapons/DamnApocalypse/Skorpion/SkorpionMain_d.dds"
    display = import_meshes(nifs / "LoadScreenSkorpion.nif")
    receiver = import_meshes(nifs / "SkorpionRecAuto.nif")
    scale = .270 / (1.595 + .897)
    fit = Matrix.Diagonal((scale, scale, scale, 1))
    fit.translation = Vector((0, .036, -.039))
    place_receiver = fit @ Matrix.Translation(Vector((0, -1.9, 0)))
    display_exclude = {"Vz61ReceiverMetal", "Vz61SelectorSemiMetal", "Vz61StockWireMetal",
                       "Vz61Mag20Metal", "Vz61BoltMetal"}
    bake(out / "skorpion-core.bin", [(obj, fit, diffuse) for key, obj in display.items()
                                   if key not in display_exclude]
         + [(receiver["Vz61ReceiverMetal"], place_receiver, diffuse)])
    bake(out / "skorpion-folded-stock.bin", [(display["Vz61StockWireMetal"], fit, diffuse)])
    extended = import_meshes(nifs / "Stocks/SkorpionStockWire.nif")
    attachment = fit @ Matrix.Translation(display["Vz61StockWireMetal"].matrix_world.translation)
    bake(out / "skorpion-extended-stock.bin", [(extended["Vz61StockWireMetal"], attachment, diffuse)])
    short_mag = import_meshes(nifs / "SkorpionMagSmall.nif")
    mag_fit = fit @ Matrix.Translation(display["Vz61Mag20Metal"].matrix_world.translation)
    bake(out / "skorpion-short-mag.bin", [(short_mag["Vz61Mag10Metal"], mag_fit, diffuse)])


def main(root, out):
    out.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(root / "tools/nif-blender-451"))
    import io_scene_niftools
    io_scene_niftools.register()
    from io_scene_niftools.modules.nif_import.property.nodes_wrapper import NodesWrapper
    NodesWrapper.link_normal_node = lambda self, node: None
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    ppk(root, out)
    skorpion(root, out)


if __name__ == "__main__":
    main(*map(Path, sys.argv[sys.argv.index("--") + 1:]))
