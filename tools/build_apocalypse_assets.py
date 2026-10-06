"""Convert the locally supplied apocalypse FBX kit and survivor FBX for REMI.

Run with Blender 5.1 in background mode. Outputs are local generated assets in
build/local-assets/apocalypse and are deliberately excluded from source ZIPs.
The kit supplies one building, so the game instances it around a playable road.
The character FBX contains no animation clips or usable texture files; its
authored material colors are retained and small idle/run actions are authored
on the supplied rig. This is a simple procedural preview, not source motion.
"""
import argparse
import sys
from pathlib import Path

import bpy


ROOT = Path(__file__).resolve().parent.parent
KIT = ROOT / "FREE_Post_Apocalypse_Survivor_Environment_Kitbash_set-93d57f55" / "fbx" / "Post_apocalypse_kitbash_free"
SURVIVOR = ROOT / "Survival_Character-11d20d01" / "fbx" / "survival_character.fbx"


def reset():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)


def export_selected(path, animated=False):
    path.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=str(path), export_format="GLB", use_selection=True,
                              export_texcoords=True, export_normals=True,
                              export_materials="EXPORT", export_animations=animated,
                              export_animation_mode="ACTIONS")
    print("REMI_ASSET", path, path.stat().st_size)


def building(out):
    reset()
    bpy.ops.import_scene.fbx(filepath=str(KIT / "Fbx" / "12.fbx"))
    meshes = [obj for obj in bpy.data.objects if obj.type == "MESH"]
    if not meshes:
        raise RuntimeError("Apocalypse kit has no meshes")
    image = bpy.data.images.load(str(KIT / "Textures" / "Post_apocalypse_city_color_dark.png"))
    material = bpy.data.materials.new("Apocalypse dark atlas")
    material.use_nodes = True
    nodes = material.node_tree.nodes
    nodes.clear()
    output = nodes.new("ShaderNodeOutputMaterial")
    shader = nodes.new("ShaderNodeBsdfPrincipled")
    shader.inputs["Roughness"].default_value = .84
    texture = nodes.new("ShaderNodeTexImage")
    texture.image = image
    material.node_tree.links.new(texture.outputs["Color"], shader.inputs["Base Color"])
    material.node_tree.links.new(shader.outputs["BSDF"], output.inputs["Surface"])
    for obj in meshes:
        obj.data.materials.clear()
        obj.data.materials.append(material)
    bpy.ops.object.select_all(action="DESELECT")
    for obj in meshes:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.join()
    joined = bpy.context.view_layer.objects.active
    joined.name = "Apocalypse building 12"
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    modifier = joined.modifiers.new("Runtime LOD", "DECIMATE")
    modifier.ratio = .25
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    export_selected(out / "apocalypse_building.glb")


def colorize_character(meshes):
    palette = {
        "head": (.70, .48, .35, 1), "arms": (.67, .45, .33, 1),
        "body2": (.34, .37, .29, 1), "jacket1": (.24, .30, .22, 1),
        "jeans1": (.20, .24, .29, 1), "backpack2": (.23, .23, .19, 1),
        "gloves1": (.18, .18, .16, 1), "shoes1": (.20, .17, .14, 1),
        "hair3": (.17, .13, .10, 1), "brows_leashes": (.15, .12, .10, 1),
        "mouth": (.43, .27, .25, 1), "body_arkit:eye": (.75, .74, .68, 1),
    }
    replacements = {}
    for obj in meshes:
        for slot in obj.material_slots:
            old = slot.material
            if old is None:
                continue
            if old.name not in replacements:
                color = palette.get(old.name.lower(), tuple(old.diffuse_color))
                material = bpy.data.materials.new(old.name + " REMI color")
                material.diffuse_color = color
                material.use_nodes = True
                shader = material.node_tree.nodes.get("Principled BSDF")
                shader.inputs["Base Color"].default_value = color
                shader.inputs["Roughness"].default_value = .85
                replacements[old.name] = material
            slot.material = replacements[old.name]


def character(out):
    reset()
    bpy.ops.import_scene.fbx(filepath=str(SURVIVOR))
    meshes = [obj for obj in bpy.data.objects if obj.type == "MESH"]
    if not meshes:
        raise RuntimeError("Survival character has no meshes")
    colorize_character(meshes)
    rig = next(obj for obj in bpy.data.objects if obj.type == "ARMATURE")
    bones = ["pelvis", "spine_03", "thigh_l", "thigh_r", "calf_l", "calf_r", "upperarm_l", "upperarm_r"]
    for name in bones:
        rig.pose.bones[name].rotation_mode = "XYZ"
    rig.animation_data_create()

    def clip(name, frames):
        action = bpy.data.actions.new(name)
        rig.animation_data.action = action
        for frame, values in frames:
            for bone in bones:
                pose = rig.pose.bones[bone]
                pose.rotation_euler = (values.get(bone, 0), 0, 0)
                pose.keyframe_insert(data_path="rotation_euler", frame=frame, group=bone)
            rig.pose.bones["pelvis"].location.z = values.get("bob", 0)
            rig.pose.bones["pelvis"].keyframe_insert(data_path="location", frame=frame, group="pelvis")

    clip("idle", [(1, {"spine_03": 0}), (16, {"spine_03": .025, "bob": .01}),
                  (31, {"spine_03": 0})])
    clip("run", [(1, {}), (5, {"thigh_l": .45, "thigh_r": -.45, "calf_r": .25,
                            "upperarm_l": -.3, "upperarm_r": .3, "bob": .05}),
                 (9, {}), (13, {"thigh_l": -.45, "thigh_r": .45, "calf_l": .25,
                                 "upperarm_l": .3, "upperarm_r": -.3, "bob": .05}),
                 (17, {})])
    bpy.ops.object.select_all(action="DESELECT")
    for obj in meshes:
        obj.select_set(True)
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    export_selected(out / "survival_character.glb", animated=True)


if __name__ == "__main__":
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "local-assets" / "apocalypse")
    options = parser.parse_args(args)
    building(options.output)
    character(options.output)
