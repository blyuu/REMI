"""Prepare local, reduced Ancient Ruins GLBs without changing the purchased originals.

Run with Blender: blender --background --python tools/assets/prepare_ancient_ruins.py
Output stays in build/local-assets/ancient-ruins (ignored by Git).
"""
from pathlib import Path
import json
import struct
import zipfile

import bpy

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "Environment Assets" / "glb.zip"
OUTPUT = ROOT / "build" / "local-assets" / "ancient-ruins"
LETTERS = "ABCDEFGH"
TARGET_TRIANGLES = 16000


def triangles(obj):
    return sum(len(poly.vertices) - 2 for poly in obj.data.polygons)


def glb_triangles(data):
    json_length, json_type = struct.unpack_from("<II", data, 12)
    if json_type != 0x4E4F534A:
        raise ValueError("GLB JSON chunk missing")
    document = json.loads(data[20:20 + json_length])
    return sum(document["accessors"][primitive["indices"]]["count"] // 3
               for mesh in document["meshes"] for primitive in mesh["primitives"])


def main():
    if not SOURCE.is_file():
        raise FileNotFoundError(f"Original asset missing: {SOURCE}")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    manifest = []
    with zipfile.ZipFile(SOURCE) as archive:
        entries = {Path(name).stem.lower(): name for name in archive.namelist()
                   if name.lower().endswith(".glb")}
        for letter in LETTERS:
            stem = f"sm_iso{letter.lower()}"
            name = entries.get(stem)
            if name is None:
                raise RuntimeError(f"Missing Ancient Ruins piece {letter} in {SOURCE}")
            destination = OUTPUT / f"ruin_{letter.lower()}.glb"
            if destination.is_file():
                manifest.append({"piece": letter, "source": name,
                                 "triangles_original": glb_triangles(archive.read(name)),
                                 "triangles_local_lod": glb_triangles(destination.read_bytes()),
                                 "file": destination.name})
                print(f"Keeping prepared {destination.name}", flush=True)
                continue
            bpy.ops.wm.read_factory_settings(use_empty=True)
            # Blender's importer needs a file. The extracted copy is local/ignored.
            source_file = OUTPUT / f"source_{letter.lower()}.glb"
            source_file.write_bytes(archive.read(name))
            bpy.ops.import_scene.gltf(filepath=str(source_file))
            objects = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
            before = sum(triangles(obj) for obj in objects)
            if not before:
                raise RuntimeError(f"No geometry in {name}")
            ratio = min(1.0, TARGET_TRIANGLES / before)
            for obj in objects:
                modifier = obj.modifiers.new("REMI local LOD", "DECIMATE")
                modifier.ratio = ratio
                bpy.context.view_layer.objects.active = obj
                bpy.ops.object.modifier_apply(modifier=modifier.name)
            after = sum(triangles(obj) for obj in objects)
            bpy.ops.export_scene.gltf(filepath=str(destination), export_format="GLB",
                                      export_apply=True, export_texcoords=True,
                                      export_normals=True, export_materials="EXPORT")
            source_file.unlink()
            manifest.append({"piece": letter, "source": name,
                             "triangles_original": before, "triangles_local_lod": after,
                             "file": destination.name})
            print(f"{letter}: {before} -> {after} triangles", flush=True)
    (OUTPUT / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
