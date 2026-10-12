# 에셋 준비 도구

이 폴더의 스크립트는 실행 전에 외부 FBX·Unreal 에셋을 REMI용 GLB 또는 RMC로 변환한다. 원본은 수정하지 않으며 생성 결과는 `build/local-assets/` 또는 명시한 출력 경로에 둔다. 원본 에셋과 생성 결과는 공개 소스 ZIP에 포함되지 않는다.

| 도구 | 용도 |
| --- | --- |
| `prepare_ancient_ruins.py` | Ancient Ruins GLB 8종의 경량 사본 |
| `import-fbx.ps1`, `import_fbx.py` | 단일 FBX를 GLB로 변환 |
| `import-raven.ps1`, `import_raven.py` | Raven의 FBX 3종과 텍스처를 정적 GLB로 변환 |
| `build-quarry-assets.ps1`, `build_quarry_assets.py` | Quarry 장면과 Survival Character GLB |
| `bake_character.py`, `bake_quinn.py` | 베이크 애니메이션 RMC |
| `build-apocalypse-assets.ps1`, `build_apocalypse_assets.py` | 이전 Apocalypse 프로토타입용 변환 도구 |

Blender 또는 Unreal Editor가 필요한 스크립트는 해당 도구가 설치된 로컬 PC에서만 실행한다. 각 데모의 준비 절차는 `docs/MazeEscape.md`, `docs/RavenRendering.md`, `docs/AssetPipeline.md`에 있다.
