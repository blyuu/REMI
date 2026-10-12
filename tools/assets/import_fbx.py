"""Blender-backed FBX to REMI GLB importer.

Run through import-fbx.ps1. FBX geometry, UVs, normals, skeleton and animation
are imported by Blender. Optional JSON material map reconnects image files:
{"MaterialName":{"base":"path.png","normal":"path.png","orm":"path.png","emissive":"path.png"}}.
The Raven three-rig asset uses the dedicated import_raven.py preset instead.
"""
import argparse
import json
import sys
from pathlib import Path

import bpy
from mathutils import Matrix


def main():
    cli = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--material-map", type=Path)
    parser.add_argument("--static-rest-pose", action="store_true")
    opt = parser.parse_args(cli)
    source = opt.source.resolve()
    output = opt.output.resolve()
    if source.suffix.lower() != ".fbx" or not source.is_file():
        raise ValueError("source must be an existing FBX file")
    if output.suffix.lower() != ".glb":
        raise ValueError("output must end in .glb")
    material_map = {}
    if opt.material_map:
        map_path = opt.material_map.resolve()
        material_map = json.loads(map_path.read_text(encoding="utf-8"))
        if not isinstance(material_map, dict):
            raise ValueError("material map root must be an object")
    else:
        map_path = source
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=str(source), use_custom_normals=False)
    if not any(obj.type == "MESH" for obj in bpy.data.objects):
        raise RuntimeError("FBX contains no meshes")
    for material in bpy.data.materials:
        binding = material_map.get(material.name)
        if binding is None:
            continue
        if not isinstance(binding, dict):
            raise ValueError(f"material {material.name} must map to an object")
        material.use_nodes = True
        nodes = material.node_tree.nodes
        nodes.clear()
        links = material.node_tree.links
        output_node = nodes.new("ShaderNodeOutputMaterial")
        pbr = nodes.new("ShaderNodeBsdfPrincipled")
        links.new(pbr.outputs["BSDF"], output_node.inputs["Surface"])

        def load(semantic, colorspace="sRGB"):
            filename = binding.get(semantic)
            if not filename:
                return None
            path = (map_path.parent / filename).resolve()
            if not path.is_file():
                raise FileNotFoundError(path)
            tex = nodes.new("ShaderNodeTexImage")
            tex.image = bpy.data.images.load(str(path), check_existing=True)
            tex.image.colorspace_settings.name = colorspace
            return tex

        if tex := load("base"):
            links.new(tex.outputs["Color"], pbr.inputs["Base Color"])
            if binding.get("alpha_mask"):
                links.new(tex.outputs["Alpha"], pbr.inputs["Alpha"])
                material.surface_render_method = "DITHERED"
        if tex := load("normal", "Non-Color"):
            normal = nodes.new("ShaderNodeNormalMap")
            links.new(tex.outputs["Color"], normal.inputs["Color"])
            links.new(normal.outputs["Normal"], pbr.inputs["Normal"])
        if tex := load("orm", "Non-Color"):
            channels = nodes.new("ShaderNodeSeparateColor")
            links.new(tex.outputs["Color"], channels.inputs["Color"])
            links.new(channels.outputs["Green"], pbr.inputs["Roughness"])
            links.new(channels.outputs["Blue"], pbr.inputs["Metallic"])
        if tex := load("emissive"):
            links.new(tex.outputs["Color"], pbr.inputs["Emission Color"])
            pbr.inputs["Emission Strength"].default_value = 1
    if opt.static_rest_pose:
        graph = bpy.context.evaluated_depsgraph_get()
        for obj in list(bpy.data.objects):
            if obj.type != "MESH":
                continue
            copy = bpy.data.meshes.new_from_object(obj.evaluated_get(graph),
                                                   preserve_all_data_layers=True, depsgraph=graph)
            obj.data = copy
            obj.modifiers.clear()
            copy.transform(obj.matrix_world)
            obj.parent = None
            obj.matrix_world = Matrix.Identity(4)
        for obj in list(bpy.data.objects):
            if obj.type != "MESH":
                bpy.data.objects.remove(obj, do_unlink=True)
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=str(output), export_format="GLB", export_yup=True)
    print("REMI_FBX_IMPORT", source, "->", output,
          "meshes", sum(obj.type == "MESH" for obj in bpy.data.objects),
          "materials", len(bpy.data.materials))


if __name__ == "__main__":
    main()
