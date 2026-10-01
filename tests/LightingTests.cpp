#include <remi/render/SceneRenderer.hpp>
#include <remi/platform/Window.hpp>
#include <array>
#include <iostream>
#include <stdexcept>
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
int wmain(int argc,wchar_t** argv) {
    try {
        Check(argc == 2,"Shader required");
        remi::WindowConfig wc; wc.visible = false; wc.graphicsSurface = true; wc.width = wc.height = 64;
        remi::Window window(wc);
        remi::RendererConfig config; config.nativeWindow = window.NativeHandle(); config.width = config.height = 64;
        config.shaderFile = argv[1]; config.useWarp = true; config.vsync = false;
        remi::Renderer renderer(config);
        const std::array<remi::Vertex,4> vertices{{{{-.8f,-.8f,.6f},{.5f,.5f,.5f}},{{-.8f,.8f,.6f},{.5f,.5f,.5f}},{{.8f,-.8f,.6f},{.5f,.5f,.5f}},{{.8f,.8f,.6f},{.5f,.5f,.5f}}}};
        const std::array<std::uint32_t,6> indices{0,1,2,2,1,3};
        auto mesh = renderer.CreateMesh(vertices,indices);
        const auto identity = remi::Matrix4::Identity();
        Check(mesh->IntersectsClip(identity),"Visible bounds culled");
        for (auto p : {remi::Vec3{3,0,0},{-3,0,0},{0,3,0},{0,-3,0},{0,0,-2},{0,0,2}})
            Check(!mesh->IntersectsClip(remi::ModelMatrix(p,0,{1,1,1})),"Outside clip plane retained");
        Check(mesh->IntersectsClip(remi::ModelMatrix({1,0,0},0,{1,1,1})),"Intersecting bounds culled");
        remi::DirectionalLight light; light.direction = {0,0,1}; light.color = {1,1,1}; light.ambient = .1f;
        const auto center = [](const remi::FrameImage& image) { return image.rgba[(32*image.width+32)*4]; };
        renderer.BeginFrame(); renderer.DrawLit(*mesh,identity,identity,light); const auto bright = center(renderer.Readback()); renderer.Present();
        light.direction = {0,0,-1};
        renderer.BeginFrame(); renderer.DrawLit(*mesh,identity,identity,light); const auto dark = center(renderer.Readback()); renderer.Present();
        Check(bright > 190 && bright < 200 && dark > 59 && dark < 67,"Lambert direction or sRGB output incorrect");
        light.direction = {0,.94f,.35f};
        remi::MaterialProperties unlit;
        unlit.shadingModel = remi::ShadingModel::Unlit;
        remi::MaterialProperties toon;
        toon.shadingModel = remi::ShadingModel::Toon;
        renderer.BeginFrame(); renderer.DrawLit(*mesh,identity,identity,light);
        const auto standardPixel = center(renderer.Readback()); renderer.Present();
        renderer.BeginFrame(); renderer.DrawLit(*mesh,identity,identity,light,unlit);
        const auto unlitPixel = center(renderer.Readback()); renderer.Present();
        renderer.BeginFrame(); renderer.DrawLit(*mesh,identity,identity,light,toon);
        const auto toonPixel = center(renderer.Readback()); renderer.Present();
        Check(unlitPixel != standardPixel && toonPixel > standardPixel + 10,
              "Shading model selection did not change rendered pixels");
        auto smoothVertices = vertices;
        for (auto& vertex : smoothVertices) vertex.normal = {0,1,0};
        auto smoothMesh = renderer.CreateMesh(smoothVertices,indices);
        light.direction = {0,0,1};
        renderer.BeginFrame(); renderer.DrawLit(*smoothMesh,identity,identity,light);
        const auto smooth = center(renderer.Readback()); renderer.Present();
        Check(smooth > dark+10 && smooth < bright-10,"Smooth-normal character lighting did not affect pixels");
        remi::MaterialProperties tinted{{.2f,.3f,.5f},.8f,.25f,0};
        renderer.BeginFrame(); renderer.DrawLit(*smoothMesh,identity,identity,light,tinted);
        const auto materialPixel = center(renderer.Readback()); renderer.Present();
        Check(materialPixel < smooth-10,"Material selection did not change rendered pixels");
        renderer.BeginFrame(); renderer.DrawLitPrepared(*mesh,identity,identity,light);
        Check(center(renderer.Readback()) == bright,"Prepared lit transform changed output"); renderer.Present();
        light.direction = {0,0,1};
        renderer.BeginShadow(identity);
        bool duplicateShadowRejected = false;
        try { renderer.BeginShadow(identity); } catch (const std::logic_error&) { duplicateShadowRejected = true; }
        Check(duplicateShadowRejected,"Nested shadow pass accepted");
        renderer.Draw(*mesh,remi::ModelMatrix({0,0,-.3f},0,{1,1,1}));
        renderer.EndShadow(); renderer.DrawLit(*mesh,identity,identity,light);
        const auto shadowed = center(renderer.Readback()); renderer.Present();
        Check(shadowed >= dark-2 && shadowed <= dark+2,"Shadow depth comparison failed");
        renderer.BeginShadow(identity); renderer.EndShadow(); renderer.DrawLit(*mesh,identity,identity,light);
        Check(center(renderer.Readback()) >= bright-2,"Empty shadow map darkened receiver"); renderer.Present();
        remi::Scene scene; const auto on = scene.Create(), off = scene.Create();
        scene.Add<remi::MeshComponent>(on,{{1,1},true}); scene.Add<remi::MeshComponent>(off,{{1,1},true});
        scene.Get<remi::TransformComponent>(off)->position.x = 5;
        const auto resolver = [&](remi::MeshHandle) { return mesh.get(); };
        renderer.BeginFrame(); const auto culled = remi::DrawScene(renderer,scene,identity,resolver,&light,true);
        const auto withCulling = renderer.Readback(); renderer.Present();
        renderer.BeginFrame(); const auto all = remi::DrawScene(renderer,scene,identity,resolver,&light,false);
        const auto withoutCulling = renderer.Readback(); renderer.Present();
        Check(culled.draws == 1 && culled.culled == 1 && all.draws == 2 && withCulling.rgba == withoutCulling.rgba,"Culling changed output");
        scene.Get<remi::MeshComponent>(on)->material.tint = {.2f,1,1};
        renderer.BeginFrame(); (void)remi::DrawScene(renderer,scene,identity,resolver,&light);
        Check(center(renderer.Readback()) < center(withCulling)-30,"Scene material was not applied to mesh instance");
        renderer.Present();
        Check(renderer.CheckDiagnostics() == 0,"D3D warnings");
        mesh.reset(); smoothMesh.reset(); const auto audit = renderer.ShutdownAndValidate();
        Check(audit.liveChildren == 0 && audit.priorWarnings == 0,"Shadow resources leaked");
        std::cout << "Lambert pixels bright=" << static_cast<unsigned>(bright) << " ambient=" << static_cast<unsigned>(dark)
            << " shadow=" << static_cast<unsigned>(shadowed) << "; six planes passed; culling 2 -> 1 draws, identical pixels\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
