[한국어](README.md) · 日本語 · [简体中文](README.zh.md)

# REMI Engine

**C++20 · DirectX 11 · HLSL · Windows x64**

REMIは、レンダリング、シーン、リソース、物理演算、オブジェクトのライフサイクル管理を実装・拡張する小規模な自作ゲームエンジンです。`REMIGravity`は、このエンジンで制作した重力反転パズルのデモです。天井に配置された3枚のコインを集め、ゴールに到達するまでのプレイを確認できます。

![REMI Gravity Runのプレイデモ](media/gravity-run-demo.gif)

## 主な機能

| 分野 | 現在の実装 |
| --- | --- |
| レンダリング | D3D11 RHIのバッファ・テクスチャ・パイプライン・描画コマンド・オフスクリーンターゲット、ホットリロード対応ShaderCache、Standard/Unlit/Toonシェーディング、方向光と影 |
| シーン・リソース | 親子階層を持つTransform、世代番号で有効性を検証するEntityId、ハンドルベースのメッシュ／ファイルキャッシュ |
| アセット・キャラクター | 実行時glTF/GLB読み込み、基本色テクスチャ・`.remimat`材質、CPU骨スキニングとアニメーション状態機械、従来のRMCHアニメーション |
| ゲーム・診断 | AABB物理演算と重力反転デモ、PretendardフォントのHUD、CPU/GPU時間のCSV記録、D3Dデバッグレイヤーによる検査 |

現在の`metallic`と`roughness`は**簡易ライティングモデルの調整値**です。スワップチェーン、標準の影ターゲット、GPU診断はまだD3D11専用です。骨スキニングはCPUで実行します。PBR、IBL、GPUスキニング、コンソール対応は実装していません。

## ビルドと実行

Windows 10/11 x64、Visual Studio 2022のC++デスクトップ開発ツールとWindows SDK、CMake 3.25以降が必要です。

```powershell
cmake --preset vs2022-x64
cmake --build --preset debug
ctest --preset debug
build\vs2022-x64\bin\Debug\REMIGravity.exe
```

`WASD`で移動、`Space`で重力反転、`Q`でマテリアル切り替え、右ドラッグでカメラ回転、ホイールでズーム、`Enter`でリスタート、`F2`で性能表示、`F5`でシェーダー再読み込み、`Esc`で終了します。`--character <ファイル>`で`.rmc`、`.gltf`、`.glb`を指定できます。クリップは`--idle-clip`と`--move-clip`で指定できます。詳細は[アセットパイプライン](docs/AssetPipeline.md)を参照してください。

Release／AddressSanitizerビルド、Blenderでの変換手順、アセット形式については、以下の技術文書を参照してください。メインの実行ファイルのほかに、レンダリング・物理演算のデモ`REMISandbox`と、最小構成の起動確認アプリ`REMIBootstrap`があります。

## 技術文書

以下の技術文書は韓国語です。

- [Architecture](docs/Architecture.md) — モジュール間の関係、フレームループ、シーン・リソース・キャラクターの境界
- [RenderingPipeline](docs/RenderingPipeline.md) — DirectX 11の初期化、座標変換、カリング、シャドウ／カラーパス、計測
- [ShaderImplementation](docs/ShaderImplementation.md) — HLSLのライティング式、マテリアルパラメータ、画質上の制約
- [MemoryManagement](docs/MemoryManagement.md) — オブジェクトの所有権、古いハンドルの無効化、終了時とリークの検証

コードの入口は`engine/include/remi/`、`engine/src/`、`shaders/Basic.hlsl`、`games/gravity/`です。現在のバージョンは**0.11.0**です。記述に相違がある場合は、[韓国語README](README.md)を正とします。
