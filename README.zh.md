[한국어](README.md) · [日本語](README.ja.md) · 简体中文

# REMI Engine

**C++20 · DirectX 11 · HLSL · Windows x64**

REMI 是一款以 C++20 和 DirectX 11 构建的小型自研游戏引擎，实现并持续扩展了渲染、场景、资源、物理及对象生命周期管理。`REMIGravity` 是基于该引擎制作的重力反转解谜演示：玩家需要收集天花板上的 3 枚硬币并到达终点。

![REMI Gravity Run 游玩演示](media/gravity-run-demo.gif)

## 主要功能

| 领域 | 当前实现 |
| --- | --- |
| 渲染 | D3D11 RHI 缓冲区、纹理、管线、绘制命令与离屏目标，支持热重载的 ShaderCache，Standard/Unlit/Toon 着色、方向光与阴影 |
| 场景与资源 | 层级 Transform、带代数校验的 EntityId、基于句柄的网格和文件缓存 |
| 资源与角色 | 运行时 glTF/GLB 导入、基础色纹理与 `.remimat` 材质、CPU 骨骼蒙皮和动画状态机，以及原有 RMCH 动画 |
| 游戏与诊断 | AABB 物理与重力反转演示、使用 Pretendard 字体的 HUD、CPU/GPU 耗时 CSV、D3D 调试层检查 |

当前 `metallic` 和 `roughness` 只是**简化光照模型的调节参数**。交换链、默认阴影目标和 GPU 诊断仍依赖 D3D11。骨骼蒙皮在 CPU 上执行；PBR、IBL、GPU 蒙皮和主机平台支持尚未实现。

## 构建与运行

需要 Windows 10/11 x64、Visual Studio 2022 的“使用 C++ 的桌面开发”组件及 Windows SDK，以及 CMake 3.25 或更高版本。

```powershell
cmake --preset vs2022-x64
cmake --build --preset debug
ctest --preset debug
build\vs2022-x64\bin\Debug\REMIGravity.exe
```

使用 `WASD` 移动、`Space` 反转重力、`Q` 切换材质、右键拖动旋转镜头、滚轮缩放、`Enter` 重新开始、`F2` 显示性能信息、`F5` 重载着色器、`Esc` 退出。`--character <文件>` 支持 `.rmc`、`.gltf` 和 `.glb`；可用 `--idle-clip`、`--move-clip` 指定动画。详情见[资源管线文档](docs/AssetPipeline.md)。

Release／AddressSanitizer 构建、Blender 转换流程和资源格式见下方技术文档。除主程序外，项目还包含渲染与物理演示 `REMISandbox`，以及用于验证最小启动流程的 `REMIBootstrap`。

## 技术文档

以下技术文档使用韩语编写。

- [Architecture](docs/Architecture.md) — 模块关系、帧循环，以及场景、资源和角色的职责边界
- [RenderingPipeline](docs/RenderingPipeline.md) — DirectX 11 初始化、坐标变换、剔除、阴影与颜色通道、性能测量
- [ShaderImplementation](docs/ShaderImplementation.md) — HLSL 光照计算、材质参数及画质限制
- [MemoryManagement](docs/MemoryManagement.md) — 对象所有权、过期句柄失效机制、关闭流程与泄漏检查

代码可从 `engine/include/remi/`、`engine/src/`、`shaders/Basic.hlsl` 和 `games/gravity/` 开始阅读。当前版本为 **0.11.0**。如译文与原文存在差异，以[韩语 README](README.md)为准。
