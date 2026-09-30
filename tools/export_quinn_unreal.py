"""Run from UE 5.5 with -ExecutePythonScript after preparing a temp project.

See docs/character-quinn.md. Does not alter the Wuwa project.
"""
from pathlib import Path
import unreal

repo = Path(__file__).resolve().parent.parent
output = repo / "build" / "ue-export" / "output"
output.mkdir(parents=True, exist_ok=True)
root = "/Game/Characters/Mannequins/"
assets = (
    ("Meshes/SKM_Quinn_Simple", "quinn_mesh.fbx", unreal.SkeletalMeshExporterFBX),
    ("Animations/Quinn/MF_Idle", "quinn_idle.fbx", unreal.AnimSequenceExporterFBX),
    ("Animations/Quinn/MF_Run_Fwd", "quinn_run.fbx", unreal.AnimSequenceExporterFBX),
    ("Textures/Quinn/T_Quinn_01ID_D", "T_Quinn_01ID_D.png", unreal.TextureExporterPNG),
    ("Textures/Quinn/T_Quinn_02ID_D", "T_Quinn_02ID_D.png", unreal.TextureExporterPNG),
)
for asset_name, filename, exporter_type in assets:
    asset = unreal.EditorAssetLibrary.load_asset(root + asset_name)
    if not asset:
        raise RuntimeError("Cannot load " + root + asset_name)
    task = unreal.AssetExportTask()
    task.object = asset
    task.filename = str(output / filename)
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    task.exporter = exporter_type()
    if filename.endswith(".fbx"):
        options = unreal.FbxExportOption()
        options.export_preview_mesh = False
        task.options = options
    result = unreal.Exporter.run_asset_export_task(task)
    if not result or not (output / filename).is_file():
        raise RuntimeError(f"Cannot export {asset_name}: {list(task.errors)}")
    print("REMI_EXPORT", filename, (output / filename).stat().st_size)
