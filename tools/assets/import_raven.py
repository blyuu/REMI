"""Import the user-supplied Raven FBX/TGA archive into a local static GLB.

Run with Blender in background mode through import-raven.ps1. The three FBX
files have independent skeletons and no animation clips; the importer bakes
their rest pose to one static model and reconnects the supplied texture maps.
The source and generated model remain in ignored build/ directories.
"""
import argparse
import json
import subprocess
import struct
import sys
from pathlib import Path

import bpy
import numpy as np
from mathutils import Matrix, Vector


def args():
    values = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--texture-size", type=int, default=2048)
    return parser.parse_args(values)


def main():
    options = args()
    source = options.source.resolve()
    output = options.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if options.texture_size < 256 or options.texture_size > 4096:
        raise ValueError("texture-size must be between 256 and 4096")
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for name in ("Raven.fbx", "Raven Face.fbx", "Raven Hair.fbx"):
        path = source / name
        if not path.is_file():
            raise FileNotFoundError(path)
        bpy.ops.import_scene.fbx(filepath=str(path), use_custom_normals=False)

    # The extracted face defaults to a nearly closed eyelid pose. Bake an
    # attentive neutral expression before collapsing its facial shape keys.
    for obj in bpy.data.objects:
        if obj.type != "MESH" or not obj.data.shape_keys:
            continue
        keys = obj.data.shape_keys.key_blocks
        for side in ("L", "R"):
            keys[f"upLid_up_{side}"].value = .7
            keys[f"loLid_down_{side}"].value = .35

    maps = {
        "MI_CH_NPC_Raven_01_BaseBody": ("CH_NPC_Raven_01_BaseBody_A", "CH_NPC_Raven_01_BaseBody_N", "CH_NPC_Raven_01_BaseBody_ORM", None),
        "MI_CH_NPC_Raven_01_Suit01": ("CH_NPC_Raven_01_Suit01_A", "CH_NPC_Raven_01_Suit01_N", "CH_NPC_Raven_01_Suit01_ORM", "CH_NPC_Raven_01_Suit01_E"),
        "MI_CH_NPC_Raven_01_Suit02": ("MI_CH_NPC_Raven_01_Suit02_A", "MI_CH_NPC_Raven_01_Suit02_N", "MI_CH_NPC_Raven_01_Suit02_ORM", "MI_CH_NPC_Raven_01_Suit02_E"),
        "MI_RavensuitVinyl": ("CH_NPC_Raven_01_Suit01_A", "CH_NPC_Raven_01_Suit01_N", "CH_NPC_Raven_01_Suit01_ORM", None),
        "MI_CH_M_NA_53_Head": ("CH_M_NA_53_Head_A", "CH_M_NA_53_Head_N", None, None),
        "MI_CH_M_NA_53_Eyes": ("Raven_Eyes_Composite", None, None, None),
        "MI_CH_M_NA_53_Eyebrow": ("Raven_Eyelashes_RGBA", None, None, None),
        "MI_CH_M_NA_53_Eyelashes": ("Raven_Eyelashes_RGBA", None, None, None),
        "MI_CH_M_NA_53_Hair01": ("Raven_Hair01_RGBA", None, None, None),
        "MI_CH_M_NA_53_Hair02": ("CH_M_NA_53_Hair02_A", "CH_M_NA_53_Hair02_N", "CH_M_NA_53_Hair02_ORM", "CH_M_NA_53_Hair02_E"),
        "MI_CH_M_NA_53_Teeth": ("Tex_NA_21_Teeth", None, None, None),
    }
    prepared = {}

    hair_mask = source / "CH_M_NA_53_Hair01_Alpha.tga"
    if not hair_mask.is_file():
        raise FileNotFoundError(hair_mask)
    hair_rgba = output / "Raven_Hair01_RGBA.png"
    size = options.texture_size
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
                    f"color=c=0x171c22:s={size}x{size}:d=1", "-i", str(hair_mask),
                    "-filter_complex", f"[1:v]scale={size}:{size},format=gray,"
                    "dilation,dilation,dilation,dilation[mask];[0:v][mask]alphamerge",
                    "-frames:v", "1", str(hair_rgba)], check=True)
    lash_mask = source / "CH_M_NA_53_Eyebrow_Alpah.tga"
    if not lash_mask.is_file():
        raise FileNotFoundError(lash_mask)
    lash_rgba = output / "Raven_Eyelashes_RGBA.png"
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
                    f"color=c=0x151820:s={size}x{size}:d=1", "-i", str(lash_mask),
                    "-filter_complex", f"[1:v]scale={size}:{size},format=gray[mask];"
                    "[0:v][mask]alphamerge", "-frames:v", "1", str(lash_rgba)], check=True)

    def image(stem, linear=False):
        if not stem:
            return None
        key = (stem, linear)
        if key in prepared:
            return prepared[key]
        generated = {"Raven_Hair01_RGBA": hair_rgba,
                     "Raven_Eyelashes_RGBA": lash_rgba,
                     "Raven_Eyes_Composite": output / "Raven_Eyes_Composite.png"}
        path = generated.get(stem,source / (stem + ".tga"))
        if not path.is_file():
            raise FileNotFoundError(path)
        if stem in generated:
            img = bpy.data.images.load(str(path), check_existing=False)
            prepared[key] = img
            return img
        probe = bpy.data.images.load(str(path), check_existing=False)
        limit = options.texture_size
        width, height = probe.size
        bpy.data.images.remove(probe)
        if max(width, height) > limit:
            scale = limit / max(width, height)
            width, height = max(1, round(width * scale)), max(1, round(height * scale))
        converted = output / (stem + ".png")
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", str(path),
                        "-vf", f"scale={width}:{height}", str(converted)], check=True)
        img = bpy.data.images.load(str(converted), check_existing=False)
        if linear:
            img.colorspace_settings.name = "Non-Color"
        prepared[key] = img
        print("REMI_TEXTURE", stem, tuple(img.size), "linear" if linear else "sRGB")
        return img

    # The supplied eye albedo contains only a dark iris. Its UVs span most of
    # the eye sphere, so using it alone makes the sclera look almost black.
    # Blend the provided sclera albedo around the iris in the same UV space.
    iris = image("CH_M_NA_53_Eyes_A")
    sclera = image("T_EyeScleraBaseColor")
    eye_size = iris.size[0]
    if iris.size[0] != iris.size[1] or sclera.size[0] != sclera.size[1]:
        raise RuntimeError("Eye textures must be square")

    def pixels(img):
        width, height = img.size
        values = np.empty(width * height * 4, dtype=np.float32)
        img.pixels.foreach_get(values)
        return values.reshape((height, width, 4))

    iris_pixels = pixels(iris)
    sclera_pixels = pixels(sclera)
    if sclera.size[0] != eye_size:
        samples = np.linspace(0, sclera.size[0] - 1, eye_size).astype(np.int32)
        sclera_pixels = sclera_pixels[np.ix_(samples, samples)]
    # The standalone Unreal sclera texture is reddish because its material
    # graph normally tints it toward white. Preserve subtle vein variation.
    sclera_pixels[:, :, :3] = np.clip(
        np.array([.94, .92, .90], dtype=np.float32) +
        (sclera_pixels[:, :, :3] - sclera_pixels[:, :, :3].mean(axis=2, keepdims=True)) * .13,
        0, 1)
    coords = (np.arange(eye_size, dtype=np.float32) + .5) / eye_size - .5
    radius = np.sqrt(coords[:, None] ** 2 + coords[None, :] ** 2)
    blend = np.clip((radius - .13) / .065, 0, 1)
    blend = blend * blend * (3 - 2 * blend)
    merged = iris_pixels * (1 - blend[:, :, None]) + sclera_pixels * blend[:, :, None]
    merged[:, :, 3] = 1
    eye_image = bpy.data.images.new("Raven_Eyes_Composite", width=eye_size,
                                    height=eye_size, alpha=True)
    eye_image.pixels.foreach_set(merged.ravel())
    eye_image.filepath_raw = str(output / "Raven_Eyes_Composite.png")
    eye_image.file_format = "PNG"
    eye_image.save()
    bpy.data.images.remove(eye_image)

    for material in bpy.data.materials:
        base, normal, orm, emissive = maps.get(material.name, (None, None, None, None))
        material.use_nodes = True
        nodes = material.node_tree.nodes
        nodes.clear()
        links = material.node_tree.links
        out = nodes.new("ShaderNodeOutputMaterial")
        bsdf = nodes.new("ShaderNodeBsdfPrincipled")
        links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
        bsdf.inputs["Metallic"].default_value = 0
        bsdf.inputs["Roughness"].default_value = .72
        if base:
            tex = nodes.new("ShaderNodeTexImage")
            tex.image = image(base)
            links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
            if material.name in ("MI_CH_M_NA_53_Hair01", "MI_CH_M_NA_53_Hair02",
                                 "MI_CH_M_NA_53_Eyebrow", "MI_CH_M_NA_53_Eyelashes"):
                links.new(tex.outputs["Alpha"], bsdf.inputs["Alpha"])
                material.surface_render_method = "DITHERED"
        else:
            # Small facial details without a source color map remain dark.
            bsdf.inputs["Base Color"].default_value = (.025, .018, .02, 1)
        if normal:
            tex = nodes.new("ShaderNodeTexImage")
            tex.image = image(normal, linear=True)
            normal_map = nodes.new("ShaderNodeNormalMap")
            links.new(tex.outputs["Color"], normal_map.inputs["Color"])
            links.new(normal_map.outputs["Normal"], bsdf.inputs["Normal"])
        if orm:
            tex = nodes.new("ShaderNodeTexImage")
            tex.image = image(orm, linear=True)
            separate = nodes.new("ShaderNodeSeparateColor")
            links.new(tex.outputs["Color"], separate.inputs["Color"])
            links.new(separate.outputs["Green"], bsdf.inputs["Roughness"])
            links.new(separate.outputs["Blue"], bsdf.inputs["Metallic"])
        if emissive:
            tex = nodes.new("ShaderNodeTexImage")
            tex.image = image(emissive)
            links.new(tex.outputs["Color"], bsdf.inputs["Emission Color"])
            bsdf.inputs["Emission Strength"].default_value = 1
        print("REMI_MATERIAL", material.name, base, normal, orm, emissive)

    # Freeze the rest pose. REMI currently handles only one glTF skin; these
    # FBX files carry separate body, face, and hair rigs with no clips.
    depsgraph = bpy.context.evaluated_depsgraph_get()
    meshes = [obj for obj in bpy.data.objects if obj.type == "MESH"]
    for obj in meshes:
        evaluated = obj.evaluated_get(depsgraph)
        copy = bpy.data.meshes.new_from_object(evaluated, preserve_all_data_layers=True, depsgraph=depsgraph)
        old = obj.data
        obj.data = copy
        obj.modifiers.clear()
        copy.transform(obj.matrix_world)
        obj.parent = None
        obj.matrix_world = Matrix.Identity(4)
        if old.users == 0:
            bpy.data.meshes.remove(old)
    for obj in list(bpy.data.objects):
        if obj.type != "MESH":
            bpy.data.objects.remove(obj, do_unlink=True)

    bounds = [v.co for obj in meshes for v in obj.data.vertices]
    minimum = Vector(tuple(min(v[i] for v in bounds) for i in range(3)))
    maximum = Vector(tuple(max(v[i] for v in bounds) for i in range(3)))
    height = maximum.z - minimum.z
    if height <= 0:
        raise RuntimeError("Raven mesh has invalid bounds")
    center = Vector(((minimum.x + maximum.x) * .5, (minimum.y + maximum.y) * .5, minimum.z))
    scale = 1.8 / height
    for obj in meshes:
        for vertex in obj.data.vertices:
            vertex.co = (vertex.co - center) * scale
    bpy.ops.object.select_all(action="DESELECT")
    for obj in meshes:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    target = output / "raven.glb"
    bpy.ops.export_scene.gltf(filepath=str(target), export_format="GLB", use_selection=True,
                              export_apply=True, export_yup=True)
    # The current renderer supports alpha test for hair cards. Blender exports
    # alpha-connected materials as BLEND; replace only those with MASK so the
    # glTF importer sends their cutoff into the PBR pixel shader.
    data = target.read_bytes()
    json_length, json_type = struct.unpack_from("<II", data, 12)
    if json_type != 0x4E4F534A:
        raise RuntimeError("GLB JSON chunk missing")
    payload = json.loads(data[20:20 + json_length])
    for material in payload.get("materials", []):
        if material.get("alphaMode") == "BLEND":
            material["alphaMode"] = "MASK"
            material["alphaCutoff"] = .08 if "Hair01" in material.get("name", "") else .5
            material["doubleSided"] = True
    # The extracted tearline/eyeshadow overlays need Unreal translucent
    # shaders. REMI's opaque fallback draws them in front of both eyeballs.
    # Drop only these overlay primitives until their original shaders can be
    # reconstructed; glTF alpha factors alone are not supported by REMI.
    hidden = {i for i, material in enumerate(payload.get("materials", []))
              if material.get("name") in ("MI_CH_M_NA_53_Eyeshadow", "MI_CH_M_NA_53_Tearline")}
    for mesh in payload.get("meshes", []):
        mesh["primitives"] = [primitive for primitive in mesh.get("primitives", [])
                              if primitive.get("material") not in hidden]
    encoded = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    encoded += b" " * ((-len(encoded)) % 4)
    binary = data[20 + json_length:]
    target.write_bytes(struct.pack("<IIII", 0x46546C67, 2, 20 + len(encoded) + len(binary), len(encoded)) +
                       struct.pack("<I", 0x4E4F534A) + encoded + binary)
    print("REMI_RAVEN", target, "meshes", len(meshes), "materials", len(bpy.data.materials),
          "source_height", height, "scale", scale)


if __name__ == "__main__":
    main()
