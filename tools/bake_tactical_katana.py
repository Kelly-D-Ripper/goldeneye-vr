"""Bake the supplied blade, wrapped grip and scabbard for the VR mod."""
from pathlib import Path
import sys

import bpy
from mathutils import Matrix

project, source, out = map(Path, sys.argv[sys.argv.index("--") + 1:])
sys.path[:0] = [str(Path(__file__).resolve().parent), str(project / "tools/nif-blender-451")]
import io_scene_niftools
from io_scene_niftools.file_io.nif import NifFile
from nifgen.formats.nif import classes

from bake_tactical_roster_nifs import bake


def mesh_piece(block, name, limit):
    vertices = block.get_vertex_data()
    data = bpy.data.meshes.new(name)
    data.from_pydata([tuple(v.vertex) for v in vertices], [], block.get_triangles())
    uv = data.uv_layers.new()
    for i, loop in enumerate(data.loops):
        value = vertices[loop.vertex_index].uv
        uv.data[i].uv = (value.u, 1.0 - value.v)
    obj = bpy.data.objects.new(name, data)
    bpy.context.collection.objects.link(obj)
    if len(data.polygons) > limit:
        modifier = obj.modifiers.new("Mesh simplification", "DECIMATE")
        modifier.ratio = limit / len(data.polygons)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    return obj


# Imported vertices share the same pivot. Skip the duplicate blood layer.
data = NifFile.load_nif(str(next(source.rglob("katanaNsaya.nif"))))
shapes = [p for p in data.blocks if isinstance(p, classes.BSTriShape) and
          "blood" not in str(p.shader_property.name).lower()]
blade = mesh_piece(shapes[0], "Blade", 2400)
handle = mesh_piece(shapes[1], "Wrapped handle", 4200)
saya = mesh_piece(shapes[2], "Scabbard", 3200)
diffuse = next(source.rglob("blossomkatana_d.dds"))
scabbard = next(source.rglob("saya_d.dds"))
s = .0104
held = Matrix(((0,0,-s,0), (0,s,0,.105), (s,0,0,-.031), (0,0,0,1)))
stowed = Matrix(((0,0,-s,0), (0,-s,0,0), (s,0,0,0), (0,0,0,1)))
out.mkdir(parents=True, exist_ok=True)
bake(out / "katana.bin", [(blade, held, diffuse), (handle, held, diffuse)])
bake(out / "katana-scabbard.bin", [(saya, stowed, scabbard), (handle, stowed, diffuse)])
