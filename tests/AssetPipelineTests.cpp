#include <remi/assets/GltfAsset.hpp>
#include <remi/assets/MaterialAsset.hpp>
#include <remi/assets/TextureLoader.hpp>
#include <remi/render/SceneRenderer.hpp>
#include <remi/platform/Window.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>

void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

int wmain(int argc, wchar_t** argv) {
    try {
        Check(argc == 3, "Shader and fixture directory required");
        const std::filesystem::path fixtures(argv[2]);
        const auto image = remi::assets::LoadImage(fixtures / "sample.png");
        Check(image.width == 1 && image.height == 1 && image.pixels[0] == 255 && image.pixels[1] == 128,
              "PNG texture decode failed");
        const auto materialFile = std::filesystem::temp_directory_path() / "remi_asset_pipeline_test.remimat";
        remi::assets::MaterialAsset material;
        material.properties = {{.7f,.8f,.9f},.2f,.4f,.1f,remi::ShadingModel::Toon};
        material.baseColorTexture = fixtures / "sample.png";
        remi::assets::SaveMaterial(materialFile,material);
        const auto roundtrip = remi::assets::LoadMaterial(materialFile);
        Check(roundtrip.properties.shadingModel == remi::ShadingModel::Toon &&
              roundtrip.properties.tint.x == .7f && roundtrip.baseColorTexture == material.baseColorTexture,
              "Material file roundtrip failed");
        std::filesystem::remove(materialFile);

        remi::WindowConfig wc; wc.visible = false; wc.graphicsSurface = true; wc.width = 96; wc.height = 96;
        remi::Window window(wc);
        remi::RendererConfig config; config.nativeWindow = window.NativeHandle(); config.width = 96; config.height = 96;
        config.shaderFile = argv[1]; config.useWarp = true; config.vsync = false;
        remi::Renderer renderer(config);
        {
            auto asset = remi::assets::GltfAsset::Load(renderer,fixtures / "skinned_triangle.gltf");
            Check(asset->HasSkeleton() && asset->PrimitiveCount() == 1 && asset->ClipNames() == std::vector<std::string>{"move"},
                  "Skinned glTF import lost primitive or clip");
            Check(asset->SetClip("move",0),"Could not select imported clip");
            remi::DirectionalLight light; light.ambient = 1;
            auto draw = [&] {
                renderer.BeginFrame({0,0,0});
                asset->Draw(renderer,remi::Matrix4::Identity(),remi::Matrix4::Identity(),light);
                auto frame = renderer.Readback(); renderer.Present(); return frame;
            };
            asset->Update(renderer,0);
            const auto before = draw();
            asset->Update(renderer,.5f);
            std::vector<DirectX::XMFLOAT4X4> bones;
            asset->Animation().ComputeBoneMatrices(bones);
            Check(bones.size() == 1 && bones[0]._42 > .2f && bones[0]._42 < .3f,
                  "Animation palette did not translate joint");
            const auto after = draw();
            const auto lit = [](const remi::FrameImage& frame) {
                std::size_t count = 0;
                for (std::size_t i = 0; i < frame.rgba.size(); i += 4)
                    if (frame.rgba[i] || frame.rgba[i+1] || frame.rgba[i+2]) ++count;
                return count;
            };
            Check(lit(before) > 0 && lit(after) > 0 && before.rgba != after.rgba,
                  "Bone animation did not move rendered pixels");
            Check(renderer.CheckDiagnostics() == 0,"Skinned glTF generated D3D warnings");
        }
        {
            remi::Scene scene;
            remi::ResourceCache<remi::Mesh> meshes;
            auto staticAsset = remi::assets::GltfAsset::Load(renderer,fixtures / "static_triangle.gltf");
            Check(staticAsset->MaterialAt(0).shadingModel == remi::ShadingModel::Toon &&
                  staticAsset->MaterialAt(0).tint.x == .6f,"glTF material override was not applied");
            staticAsset.reset();
            auto binaryAsset = remi::assets::GltfAsset::Load(renderer,fixtures / "static_triangle.glb");
            Check(binaryAsset->PrimitiveCount() == 1,"GLB container import failed");
            binaryAsset.reset();
            const auto handles = remi::assets::ImportStaticGltfScene(renderer,scene,meshes,fixtures / "static_triangle.gltf");
            Check(handles.size() == 1 && scene.Size() == 1 && meshes.Get(handles[0]),"Static glTF scene import failed");
            renderer.BeginFrame({0,0,0});
            const auto stats = remi::DrawScene(renderer,scene,remi::Matrix4::Identity(),
                [&](remi::MeshHandle handle) { return meshes.Get(handle); },nullptr,false);
            const auto frame = renderer.Readback(); renderer.Present();
            Check(stats.draws == 1 && frame.rgba.size() == 96u*96u*4u,"Imported scene did not draw");
            Check(renderer.CheckDiagnostics() == 0,"Static glTF generated D3D warnings");
            scene.Clear(); meshes.Clear();
        }
        const auto report = renderer.ShutdownAndValidate();
        Check(report.liveChildren == 0 && report.priorWarnings == 0,"Asset pipeline leaked D3D objects");
        std::cout << "glTF import, textures, materials, CPU skinning, and static scene passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
