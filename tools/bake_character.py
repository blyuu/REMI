"""Bake a rigged Blender/glTF/FBX character into REMI's named-clip RMCH v2.

Run with Blender 4.3+:
  blender --background --python tools/bake_character.py -- \
    --source character.glb --clip idle=Idle --clip run=Run --output character.rmc

Separate animation FBX files can use --animation idle=idle.fbx instead of
--clip idle=Action. Bone names must match the model rig.

The source must contain one armature, skinned meshes, and named actions. This
exporter samples deformed vertices; REMI does not yet skin bones at runtime.
Material base colors, or explicitly supplied diffuse textures, are baked into
vertex colors. Texture images and UVs are not exported to REMI at runtime.
"""
import argparse
import re
import struct
import sys
from pathlib import Path

import bpy
import numpy as np


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--clip", action="append", default=[], help="REMI name=Blender action name")
    parser.add_argument("--animation", action="append", default=[], help="REMI name=animation FBX")
    parser.add_argument("--mesh", action="append", default=[], help="Mesh object name; repeat for multiple parts")
    parser.add_argument("--texture", action="append", default=[], help="Material slot index or name=diffuse image path")
    parser.add_argument("--fps", type=float, default=15.0)
    return parser.parse_args(argv)


def import_source(path):
    if not path.is_file():
        raise FileNotFoundError(path)
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    extension = path.suffix.lower()
    if extension in (".glb", ".gltf"):
        bpy.ops.import_scene.gltf(filepath=str(path))
    elif extension == ".fbx":
        bpy.ops.import_scene.fbx(filepath=str(path))
    elif extension == ".blend":
        bpy.ops.wm.open_mainfile(filepath=str(path))
    else:
        raise ValueError("Source must be .glb, .gltf, .fbx or .blend")


def main():
    args = parse_args()
    if not 1 <= args.fps <= 120:
        raise ValueError("fps must be between 1 and 120")
    clip_specs = []
    for item in args.clip + args.animation:
        if "=" not in item:
            raise ValueError("clip must be NAME=ACTION")
        name, value = item.split("=", 1)
        if not re.fullmatch(r"[A-Za-z0-9_-]{1,31}", name) or not value or name in [c[0] for c in clip_specs]:
            raise ValueError("clip names must be unique ASCII identifiers of at most 31 characters")
        clip_specs.append((name, value, item in args.animation))
    if not clip_specs or len(clip_specs) > 32:
        raise ValueError("Specify between 1 and 32 clips")

    import_source(args.source)
    source_objects = set(bpy.data.objects)
    rigs = [obj for obj in bpy.data.objects if obj.type == "ARMATURE"]
    meshes = [obj for obj in source_objects if obj.type == "MESH" and any(
        modifier.type == "ARMATURE" for modifier in obj.modifiers)]
    if args.mesh:
        meshes = [obj for obj in meshes if obj.name in args.mesh]
    if len(rigs) != 1 or not meshes:
        raise ValueError("Expected one armature and at least one selected skinned mesh")
    rig = rigs[0]
    rig.animation_data_create()
    textures = {}
    for mapping in args.texture:
        if "=" not in mapping:
            raise ValueError("texture must be SLOT_OR_MATERIAL=IMAGE")
        key, path = mapping.split("=", 1)
        image = bpy.data.images.load(str(Path(path).resolve()), check_existing=True)
        width, height = image.size
        pixels = np.empty(width * height * 4, dtype=np.float32)
        image.pixels.foreach_get(pixels)
        textures[key] = pixels.reshape((height, width, 4))
    external_actions = {}
    for name, path, external in clip_specs:
        if not external:
            continue
        previous = set(bpy.data.objects)
        bpy.ops.import_scene.fbx(filepath=str(Path(path).resolve()))
        imported = [obj for obj in bpy.data.objects if obj not in previous and obj.type == "ARMATURE"]
        if len(imported) != 1 or not imported[0].animation_data or not imported[0].animation_data.action:
            raise ValueError(f"Animation file must contain one animated armature: {path}")
        if set(rig.data.bones.keys()) - set(imported[0].data.bones.keys()):
            raise ValueError(f"Animation bone names differ from model: {path}")
        external_actions[name] = (imported[0].animation_data.action, imported[0].animation_data.action_slot)

    # Share source vertices within each material while preserving hard material borders.
    layout = []
    colors = []
    index_lookup = {}
    indices = []
    for obj in meshes:
        obj.data.calc_loop_triangles()
        for triangle in obj.data.loop_triangles:
            material = obj.material_slots[triangle.material_index].material if triangle.material_index < len(obj.material_slots) else None
            rgba = material.diffuse_color if material else (0.8, 0.8, 0.8, 1.0)
            base_color = tuple(max(0.0, min(1.0, float(component))) for component in rgba[:3])
            texture = textures.get(material.name if material else "")
            if texture is None:
                texture = textures.get(str(triangle.material_index))
            uv_layer = obj.data.uv_layers.active
            if texture is not None and uv_layer is None:
                raise ValueError(f"Mesh {obj.name} needs UVs for material {triangle.material_index}")
            corners = []
            for loop in triangle.loops:
                vertex = obj.data.loops[loop].vertex_index
                uv = uv_layer.data[loop].uv if texture is not None else None
                key = (obj, vertex, triangle.material_index, round(uv.x, 6), round(uv.y, 6)) if uv is not None else (obj, vertex, triangle.material_index)
                if key not in index_lookup:
                    index_lookup[key] = len(layout)
                    layout.append((obj, vertex))
                    if texture is not None:
                        x = min(int((uv.x % 1.0) * texture.shape[1]), texture.shape[1] - 1)
                        y = min(int((uv.y % 1.0) * texture.shape[0]), texture.shape[0] - 1)
                        colors.append(tuple(float(component) for component in texture[y, x, :3]))
                    else:
                        colors.append(base_color)
                corners.append(index_lookup[key])
            indices.extend((corners[0], corners[2], corners[1]))
    if not layout or len(layout) > 100000:
        raise ValueError(f"Character has no triangles or exceeds 100000 baked vertices ({len(layout)}); meshes: {[(o.name, len(o.data.vertices)) for o in meshes]}")
    # Handedness conversion reversed winding above.
    indices = np.asarray(indices, dtype="<u4")
    if len(indices) > 600000:
        raise ValueError("Character exceeds index limit")
    object_indices = {obj: np.array([vertex for owner, vertex in layout if owner == obj], dtype=np.intp) for obj in meshes}
    object_positions = {obj: np.array([i for i, (owner, _) in enumerate(layout) if owner == obj], dtype=np.intp) for obj in meshes}

    def sample(frame):
        integer = int(frame)
        bpy.context.scene.frame_set(integer, subframe=frame - integer)
        depsgraph = bpy.context.evaluated_depsgraph_get()
        result = np.empty((len(layout), 3), dtype=np.float32)
        for obj in meshes:
            evaluated = obj.evaluated_get(depsgraph)
            temporary = evaluated.to_mesh()
            try:
                if len(temporary.vertices) != len(obj.data.vertices):
                    raise ValueError(f"Modifier changes vertex count on {obj.name}")
                xyz = np.empty((len(temporary.vertices), 3), dtype=np.float32)
                temporary.vertices.foreach_get("co", xyz.ravel())
                matrix = np.array(evaluated.matrix_world, dtype=np.float32)
                world = xyz @ matrix[:3, :3].T + matrix[:3, 3]
                points = world[object_indices[obj]]
                result[object_positions[obj], 0] = points[:, 0]
                result[object_positions[obj], 1] = points[:, 2]
                result[object_positions[obj], 2] = -points[:, 1]
            finally:
                evaluated.to_mesh_clear()
        if not np.isfinite(result).all():
            raise ValueError("Non-finite deformed vertex")
        return result

    raw_clips = []
    scene_fps = bpy.context.scene.render.fps / bpy.context.scene.render.fps_base
    for name, action_name, external in clip_specs:
        action, slot = external_actions[name] if external else (bpy.data.actions.get(action_name), None)
        if action is None:
            raise ValueError(f"Action not found: {action_name}; available: {[a.name for a in bpy.data.actions]}")
        rig.animation_data.action = action
        if hasattr(rig.animation_data, "action_slot") and len(action.slots):
            rig.animation_data.action_slot = slot if slot else action.slots[0]
        start, end = action.frame_range
        count = max(1, int(np.ceil((end - start) * args.fps / scene_fps)))
        if count > 1000:
            raise ValueError(f"Clip {name} exceeds 1000 frames")
        raw_clips.append((name, [sample(float(start) + i * scene_fps / args.fps) for i in range(count)]))

    # Fit the first pose into REMI's existing player visual: local feet = -0.5,
    # local height = 2.0; the game scales the child visual to 0.7 x 0.8 x 0.7.
    first = raw_clips[0][1][0]
    low, high = first.min(axis=0), first.max(axis=0)
    height = high[1] - low[1]
    if height <= 1e-5:
        raise ValueError("Character has zero height")
    center_xz = (low[[0, 2]] + high[[0, 2]]) * 0.5
    scale = 2.0 / height

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("wb") as stream:
        stream.write(struct.pack("<4sIIIIIf", b"RMCH", 2, len(layout), len(indices), len(raw_clips), 0, args.fps))
        stream.write(np.asarray(colors, dtype="<f4").tobytes())
        stream.write(indices.tobytes())
        for name, poses in raw_clips:
            stream.write(struct.pack("<32sI", name.encode("ascii"), len(poses)))
        for _, poses in raw_clips:
            for pose in poses:
                pose = pose.copy()
                pose[:, 0] = (pose[:, 0] - center_xz[0]) * scale
                pose[:, 1] = (pose[:, 1] - low[1]) * scale - 0.5
                pose[:, 2] = (pose[:, 2] - center_xz[1]) * scale
                stream.write(pose.astype("<f4", copy=False).tobytes())
    print("REMI_BAKE", args.output, "vertices", len(layout), "clips", [(name, len(frames)) for name, frames in raw_clips])


if __name__ == "__main__":
    main()
