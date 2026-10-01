#include <remi/render/ShaderCache.hpp>
#include <remi/render/Renderer.hpp>
#include <remi/rhi/RHIFactory.hpp>
#include <iostream>
#include <array>
#include <fstream>
#include <filesystem>
#include <stdexcept>

void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

int wmain(int argc, wchar_t** argv) {
    try {
        Check(argc == 2, "Shader path required");
        auto device = remi::rhi::CreateDevice({remi::rhi::Backend::D3D11, remi::rhi::Driver::Warp, false, false});
        Check(device->Info().backend == remi::rhi::Backend::D3D11 &&
              device->Info().driver == remi::rhi::Driver::Warp &&
              !device->Info().adapterName.empty(), "RHI factory did not create the requested backend");
        std::array<std::uint8_t, 32> data{};
        auto dynamic = device->CreateBuffer({data.size(), remi::rhi::BufferBind::Vertex, true, data.data()});
        Check(dynamic->ByteSize() == data.size() && dynamic->Dynamic(), "Dynamic RHI buffer creation failed");
        data[0] = 42;
        device->UpdateBuffer(*dynamic, data.data(), data.size());
        auto immutable = device->CreateBuffer({data.size(), remi::rhi::BufferBind::Index, false, data.data()});
        bool rejected = false;
        try { device->UpdateBuffer(*immutable, data.data(), data.size()); }
        catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected, "Immutable RHI buffer accepted an update");
        auto other = remi::rhi::CreateDevice({remi::rhi::Backend::D3D11, remi::rhi::Driver::Warp, false, false});
        rejected = false;
        try { other->UpdateBuffer(*dynamic, data.data(), data.size()); }
        catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected, "Another device updated the RHI buffer");
        const std::array<std::uint8_t, 4> white{255,255,255,255};
        auto texture = device->CreateTexture({1,1,remi::rhi::TextureFormat::RGBA8Srgb,false,white.data()});
        auto target = device->CreateRenderTarget({32,32,true});
        Check(texture->Width() == 1 && target->Width() == 32 && target->Color().Height() == 32,
              "RHI texture or offscreen target dimensions incorrect");

        remi::ShaderCache cache(argv[1]);
        const auto& vertex = cache.Get({remi::ShaderStage::Vertex});
        const auto& vertexAgain = cache.Get({remi::ShaderStage::Vertex, remi::ShadingModel::Toon});
        Check(!vertex.empty() && &vertex == &vertexAgain && cache.CompilationCount() == 1,
              "Vertex shader was compiled more than once");
        const auto& standard = cache.Get({remi::ShaderStage::Pixel, remi::ShadingModel::Standard});
        const auto& unlit = cache.Get({remi::ShaderStage::Pixel, remi::ShadingModel::Unlit});
        const auto& toon = cache.Get({remi::ShaderStage::Pixel, remi::ShadingModel::Toon});
        Check(!standard.empty() && !unlit.empty() && !toon.empty() &&
              standard != unlit && standard != toon && unlit != toon,
              "Shading model permutations produced identical bytecode");
        (void)cache.Get({remi::ShaderStage::Pixel, remi::ShadingModel::Toon});
        Check(cache.EntryCount() == 4 && cache.CompilationCount() == 4,
              "Cached shader permutation was recompiled");
        rejected = false;
        try { (void)cache.Get({remi::ShaderStage::Pixel, remi::ShadingModel::Count}); }
        catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected, "Unsupported shading model accepted");
        cache.Invalidate();
        Check(cache.EntryCount() == 0, "Shader invalidation left entries behind");
        (void)cache.Get({remi::ShaderStage::Pixel, remi::ShadingModel::Standard});
        Check(cache.CompilationCount() == 5, "Shader did not recompile after invalidation");
        remi::rhi::PipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = cache.Get({remi::ShaderStage::Vertex});
        pipelineDesc.pixelShader = cache.Get({remi::ShaderStage::Pixel,remi::ShadingModel::Standard});
        pipelineDesc.inputs = {{"POSITION",0,remi::rhi::VertexFormat::Float3},
            {"COLOR",12,remi::rhi::VertexFormat::Float3},
            {"NORMAL",24,remi::rhi::VertexFormat::Float3},
            {"TEXCOORD",36,remi::rhi::VertexFormat::Float2}};
        auto pipeline = device->CreatePipeline(pipelineDesc);
        const std::array<remi::Vertex,3> vertices{{{{-1,-1,0},{1,1,1},{0,0,1},{0,0}},
            {{0,1,0},{1,1,1},{0,0,1},{.5f,1}},{{1,-1,0},{1,1,1},{0,0,1},{1,0}}}};
        const std::array<std::uint32_t,3> indices{0,1,2};
        auto vb = device->CreateBuffer({sizeof(vertices),remi::rhi::BufferBind::Vertex,false,vertices.data()});
        auto ib = device->CreateBuffer({sizeof(indices),remi::rhi::BufferBind::Index,false,indices.data()});
        auto cb = device->CreateBuffer({304,remi::rhi::BufferBind::Constant,true,nullptr});
        std::array<float,76> constants{};
        auto* values = constants.data();
        for (unsigned matrix = 0; matrix < 3; ++matrix)
            for (unsigned diagonal = 0; diagonal < 4; ++diagonal) values[matrix*16 + diagonal*5] = 1.f;
        values[72] = 1.f; // Basic.hlsl g_HasBaseColorTexture
        device->UpdateBuffer(*cb,constants.data(),sizeof(constants));
        const float clear[]{.1f,.2f,.3f,1.f};
        auto& context = device->Context();
        context.BeginPass(*target,clear);
        context.SetPipeline(*pipeline);
        context.SetVertexBuffer(*vb,sizeof(remi::Vertex));
        context.SetIndexBuffer(*ib);
        context.SetConstantBuffer(0,*cb);
        context.SetTexture(1,texture.get());
        context.DrawIndexed(3);
        context.EndPass();
        const auto pixels = context.Readback(*target);
        const auto center = (16u * 32u + 16u) * 4u;
        Check(pixels.rgba[center] > 250 && pixels.rgba[center+1] > 250 && pixels.rgba[center+2] > 250,
              "RHI pipeline/texture draw did not reach offscreen target");

        std::ifstream shaderFile(argv[1],std::ios::binary);
        const std::string original{std::istreambuf_iterator<char>(shaderFile),std::istreambuf_iterator<char>()};
        Check(!original.empty(),"Could not read shader for reload test");
        Check(cache.ReplaceSource(original + "\n// replacement\n") && cache.Generation() == 1,
              "Shader source replacement failed");
        const auto generation = cache.Generation();
        rejected = false;
        try { (void)cache.ReplaceSource("invalid HLSL"); }
        catch (const std::runtime_error&) { rejected = true; }
        Check(rejected && cache.Generation() == generation && !cache.Get({remi::ShaderStage::Vertex}).empty(),
              "Failed shader replacement did not retain last-good bytecode");
        const auto temporary = std::filesystem::temp_directory_path() / "remi_shader_reload_test.hlsl";
        { std::ofstream out(temporary,std::ios::binary); out << original; }
        remi::ShaderCache fileCache(temporary);
        (void)fileCache.Get({remi::ShaderStage::Vertex});
        Check(!fileCache.ReloadIfChanged(),"Unchanged shader reloaded");
        { std::ofstream out(temporary,std::ios::binary|std::ios::trunc); out << original << "\n// changed\n"; }
        Check(fileCache.ReloadIfChanged() && fileCache.Generation() == 1,"File change was not detected");
        std::filesystem::remove(temporary);

        remi::ShaderCache missing(std::filesystem::path(argv[1]).concat(L".missing"));
        rejected = false;
        try { (void)missing.Get({remi::ShaderStage::Vertex}); }
        catch (const std::runtime_error&) { rejected = true; }
        Check(rejected && missing.EntryCount() == 0, "Failed shader compilation polluted cache");
        std::cout << "D3D11 WARP RHI buffers and four shader permutations passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
