"""Assemble the locally supplied African Slate Quarry FBX pieces for REMI.

The pack contains separate Megascans rocks and foliage, not a finished level.
This script arranges textured rocks around a walkable quarry floor and exports
one static GLB. Source FBX/JPG and generated GLB stay local and out of Git.
"""
import argparse
import math
import sys
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_apocalypse_assets import character, export_selected, reset


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "African_Slate_Quarry-578d0ceb" / "fbx" / "mid"


def asset(asset_id):
    folder = SOURCE / asset_id
    fbx = folder / f"{asset_id}.fbx"
    if not fbx.exists():
        raise FileNotFoundError(fbx)
    old = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=str(fbx), use_custom_normals=False)
    meshes = [obj for obj in set(bpy.data.objects) - old if obj.type == "MESH"]
    if not meshes:
        raise RuntimeError(f"No meshes in {fbx}")

    # Megascans FBX often has parent empties and nonzero source pivots. Bake
    # the full transform into local vertices so placement uses a stable origin.
    for obj in meshes:
        obj.data.transform(obj.matrix_world.copy())
        obj.parent = None
        obj.matrix_world.identity()
    points = [v.co for obj in meshes for v in obj.data.vertices]
    minimum = Vector(tuple(min(p[i] for p in points) for i in range(3)))
    maximum = Vector(tuple(max(p[i] for p in points) for i in range(3)))
    offset = Vector((-(minimum.x + maximum.x) * .5,
                     -(minimum.y + maximum.y) * .5, -minimum.z))
    for obj in meshes:
        obj.data.transform(Matrix.Translation(offset))

    image_path = next((p for p in folder.glob("*.jpg") if
                       "basecolor" in p.name.lower() or "albedo" in p.name.lower()), None)
    if image_path is None:
        raise RuntimeError(f"Missing base-color texture in {folder}")
    image = bpy.data.images.load(str(image_path), check_existing=True)
    material = bpy.data.materials.new(f"Quarry {asset_id}")
    material.use_nodes = True
    bsdf = material.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Roughness"].default_value = .9
    texture = material.node_tree.nodes.new("ShaderNodeTexImage")
    texture.image = image
    material.node_tree.links.new(texture.outputs["Color"], bsdf.inputs["Base Color"])
    for obj in meshes:
        obj.data.materials.clear()
        obj.data.materials.append(material)
    bpy.ops.object.select_all(action="DESELECT")
    for obj in meshes:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.join()
    result = bpy.context.view_layer.objects.active
    result.name = f"Quarry rock {asset_id}"
    return result, material


def quarry(out):
    reset()
    ids = ("xb5ebhf", "xb5gbjo", "xbkgdbu", "xbnjbc3", "xbrdfck",
           "xbrgfep", "xbteejw", "xcdeajb", "xcghcfe", "xckifaf", "xckjagi")
    sources = {name: asset(name) for name in ids}
    placed = []

    def place(name, x, z, scale, angle=0, height=0):
        template, _ = sources[name]
        obj = template.copy()
        obj.data = template.data.copy()
        bpy.context.collection.objects.link(obj)
        obj.name = f"Quarry {name} {len(placed):03d}"
        obj.location = (x, -z, height)
        obj.rotation_euler.z = math.radians(angle)
        obj.scale = (scale, scale, scale)
        placed.append(obj)

    # Dense terraces frame an approximately 12 m wide path. Near rocks,
    # elevated benches, and outer ridge overlap so the horizon is filled.
    # Visual geometry remains separate from coarse runtime colliders.
    left = ("xbrdfck", "xcghcfe", "xckjagi", "xbnjbc3", "xckifaf", "xbteejw", "xbrdfck")
    right = ("xckjagi", "xbrdfck", "xbteejw", "xcghcfe", "xbnjbc3", "xckifaf", "xbrdfck")
    for i, z in enumerate(range(-27, 30, 8)):
        a, b = left[i % len(left)], right[i % len(right)]
        place(a, -10.8 - (i % 2) * .5, z, 2.7, 20 + i * 13, -.4)
        place(b, 10.8 + (i % 3) * .3, z + 2, 2.75, -28 - i * 11, -.4)
        place(right[(i + 2) % len(right)], -14.0, z + 3, 3.15, -14 + i * 9, 1.6)
        place(left[(i + 3) % len(left)], 14.0, z - 2, 3.1, 12 - i * 8, 1.6)
        place("xb5gbjo", -7.8, z + 3, 3.0, 17 * i, -.12)
        place("xbkgdbu", 7.8, z - 1, 3.0, -19 * i, -.12)
        place("xbrgfep", -5.7, z + 5, 1.6, 37 * i, -.10)
        place("xbrgfep", 5.8, z + 4, 1.4, -31 * i, -.10)
        place(a, -18.0, z + 1, 3.4, -12 + i * 5, 3.2)
        place(b, 18.0, z - 1, 3.5, 8 - i * 7, 3.1)

    # A layered end wall, rather than an empty far horizon, reads as a pit.
    for name, x, z, scale, angle in (
        ("xcghcfe", -9, 34, 3.3, 15), ("xckjagi", 0, 35, 3.6, -22),
        ("xbteejw", 9, 34, 3.5, 35), ("xbrdfck", -5, 38, 3.6, 12),
        ("xbnjbc3", 5, 38, 3.5, -18), ("xbrgfep", -5.6, 12, 2.7, 72),
        ("xcdeajb", 6.6, 18, 2.7, -42), ("xb5ebhf", -6.4, -9, 2.1, 13),
        ("xb5ebhf", 6.7, -3, 2.4, -23)):
        place(name, x, z, scale, angle, -.15)
    for name, x, z, scale, angle in (
        ("xcghcfe", -14, 41, 5.0, 11),
        ("xckjagi", 0, 42, 5.1, -24),
        ("xbteejw", 14, 41, 5.0, 21)):
        place(name, x, z, scale, angle, 2.6)

    # Broken stone lies in the route, leaving a walkable weaving line.
    for i, (x, z) in enumerate(((-2.8, -6), (2.6, -1), (-2.7, 6),
                                (2.6, 12), (-2.6, 18), (2.8, 24))):
        place("xbkgdbu" if i % 2 else "xbrgfep", x, z, 1.25,
              47 * i, -.08)

    # Textured quarry floor. The texture repeats instead of stretching once
    # across the full 52-by-66 metre scene.
    bpy.ops.mesh.primitive_plane_add(size=2, location=(0, -2, -.08))
    ground = bpy.context.object
    ground.name = "Quarry gravel floor"
    ground.scale = (26, 33, 1)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    ground.data.uv_layers.active.name = sources["xb5ebhf"][0].data.uv_layers.active.name
    for uv in ground.data.uv_layers.active.data:
        uv.uv *= 15
    ground.data.materials.append(sources["xb5ebhf"][1])
    placed.append(ground)
    print(f"REMI_SCENE {len(placed) - 1} rock placements, 1 quarry floor, {len(ids)} materials")

    # The source templates are never exported; only their placed copies are.
    bpy.ops.object.select_all(action="DESELECT")
    for obj in placed:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = placed[0]
    bpy.ops.object.join()
    joined = bpy.context.view_layer.objects.active
    joined.name = "African Slate Quarry walkable set"
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    export_selected(out / "quarry_map.glb")


if __name__ == "__main__":
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "local-assets" / "quarry")
    options = parser.parse_args(args)
    quarry(options.output)
    if not (options.output / "survival_character.glb").exists():
        character(options.output)
