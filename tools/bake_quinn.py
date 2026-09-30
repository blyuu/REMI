"""Bake UE5.5 Quinn FBX mesh, idle and run into REMI vertex animation.

Input: build/ue-export/output/{quinn_mesh,quinn_idle,quinn_run}.fbx and
T_Quinn_{01,02}ID_D.png exported from the UE 5.5 third-person template.
Run with Blender 5.1: blender --background --python tools/bake_quinn.py
"""
import struct
from pathlib import Path

import bpy
import numpy as np

repo = Path(__file__).resolve().parent.parent
source = repo / "build" / "ue-export" / "output"
destination = repo / "assets" / "Quinn" / "quinn.rmc"

bpy.ops.import_scene.fbx(filepath=str(source / "quinn_mesh.fbx"))
model_rig = next(obj for obj in bpy.data.objects if obj.type == "ARMATURE")
mesh = next(obj for obj in bpy.data.objects if obj.type == "MESH" and obj.name.endswith("LOD1"))

clips = {}
for name in ("idle", "run"):
    previous = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=str(source / f"quinn_{name}.fbx"))
    rig = next(obj for obj in bpy.data.objects if obj not in previous and obj.type == "ARMATURE")
    clips[name] = (rig.animation_data.action, rig.animation_data.action_slot)
    if set(model_rig.data.bones.keys()) - set(rig.data.bones.keys()):
        raise RuntimeError(name + " animation lacks model bones")

images = []
for part in ("01", "02"):
    image = bpy.data.images.load(str(source / f"T_Quinn_{part}ID_D.png"))
    width, height = image.size
    pixels = np.empty(width * height * 4, dtype=np.float32)
    image.pixels.foreach_get(pixels)
    images.append(pixels.reshape((height, width, 4)))

mesh.data.calc_loop_triangles()
uvs = mesh.data.uv_layers.active.data
colors = np.zeros((len(mesh.data.vertices), 3), dtype=np.float32)
seen = np.zeros(len(mesh.data.vertices), dtype=bool)
indices = []
for triangle in mesh.data.loop_triangles:
    material = min(triangle.material_index, 1)
    image = images[material]
    for loop_index in triangle.loops:
        vertex = mesh.data.loops[loop_index].vertex_index
        indices.append(vertex)
        if not seen[vertex]:
            uv = uvs[loop_index].uv
            x = min(int((uv.x % 1.0) * image.shape[1]), image.shape[1] - 1)
            y = min(int((uv.y % 1.0) * image.shape[0]), image.shape[0] - 1)
            colors[vertex] = image[y, x, :3]
            seen[vertex] = True
if not seen.all():
    raise RuntimeError("Mesh has vertices without texture coordinates")

def frames(name):
    action, slot = clips[name]
    model_rig.animation_data_create()
    model_rig.animation_data.action = action
    model_rig.animation_data.action_slot = slot
    start, end = map(int, action.frame_range)
    samples = []
    for frame in range(start, end, 2):
        bpy.context.scene.frame_set(frame)
        # Animation-only FBX contains a 0.01 object scale and root motion.
        # The game owns movement; retain only the in-place bone pose.
        model_rig.scale = (1, 1, 1)
        model_rig.location = (0, 0, 0)
        bpy.context.view_layer.update()
        evaluated = mesh.evaluated_get(bpy.context.evaluated_depsgraph_get())
        xyz = np.empty(len(mesh.data.vertices) * 3, dtype=np.float32)
        evaluated.data.vertices.foreach_get("co", xyz)
        xyz = xyz.reshape((-1, 3))
        matrix = np.array(evaluated.matrix_world, dtype=np.float32)
        world = xyz @ matrix[:3, :3].T + matrix[:3, 3]
        # Blender Z-up/right-handed -> REMI Y-up/left-handed. Fit current
        # 0.7 x 0.8 x 0.7 player visual transform and place feet at -0.4.
        converted = np.empty_like(world)
        converted[:, 0] = world[:, 0]
        converted[:, 1] = (0.7 * world[:, 2] - 0.4) / 0.8
        converted[:, 2] = -world[:, 1]
        if not np.isfinite(converted).all():
            raise RuntimeError("Non-finite animation pose")
        samples.append(converted.astype("<f4", copy=False))
    return samples

idle = frames("idle")
run = frames("run")
destination.parent.mkdir(parents=True, exist_ok=True)
with destination.open("wb") as stream:
    stream.write(struct.pack("<4sIIIII f", b"RMCH", 1, len(colors), len(indices), len(idle), len(run), 15.0))
    stream.write(colors.astype("<f4", copy=False).tobytes())
    stream.write(np.asarray(indices, dtype="<u4").tobytes())
    for pose in idle + run:
        stream.write(pose.tobytes())
print("REMI_BAKE", destination, "vertices", len(colors), "triangles", len(indices) // 3,
      "idle", len(idle), "run", len(run), "color range", colors.min(axis=0), colors.max(axis=0))
