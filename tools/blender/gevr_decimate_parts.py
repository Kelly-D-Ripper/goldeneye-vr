"""Reduce dense attachment meshes while retaining their UVs and fit coordinates."""
from pathlib import Path
import sys

import bpy
import numpy as np


directory = Path(sys.argv[sys.argv.index("--") + 1])
for path in sorted(directory.glob("part-*.npz")):
    with np.load(path) as data:
        points, indices, uv = data["points"], data["indices"], data["uv"]
    mesh = bpy.data.meshes.new(path.stem)
    mesh.from_pydata(points.tolist(), [], indices.tolist())
    obj = bpy.data.objects.new(path.stem, mesh)
    bpy.context.collection.objects.link(obj)
    bpy.context.view_layer.objects.active = obj
    uv_layer = mesh.uv_layers.new()
    uv_layer.data.foreach_set("uv", uv[indices.flatten(), :2].astype(np.float32).flatten())
    mesh.polygons.foreach_set("use_smooth", np.ones(len(indices), dtype=np.bool_))
    modifier = obj.modifiers.new("Tactical attachment mesh", "DECIMATE")
    modifier.ratio = 6500 / len(indices)
    modifier.use_collapse_triangulate = True
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    mesh = obj.data
    mesh.calc_loop_triangles()
    loops = [index for triangle in mesh.loop_triangles for index in triangle.loops]
    vertices = [mesh.loops[index].vertex_index for index in loops]
    np.savez(path.with_name("reduced-" + path.name),
             points=np.asarray([mesh.vertices[index].co[:] for index in vertices]),
             normals=np.asarray([mesh.corner_normals[index].vector[:] for index in loops]),
             uv=np.asarray([mesh.uv_layers.active.data[index].uv[:] for index in loops]),
             indices=np.arange(len(loops)).reshape(-1, 3))
    print(f"{path.stem} {len(indices)} -> {len(loops) // 3} triangles", flush=True)
    bpy.data.objects.remove(obj, do_unlink=True)
    bpy.data.meshes.remove(mesh)
