# Ancient Ruins Maze Escape — 로컬 데모

이 프로젝트는 외부 Ancient Ruins 조각을 REMI의 기존 glTF·Scene·ResourceCache·PhysicsWorld·PBR 렌더러로 조립한 3인칭 미로입니다. 기존 UE5 Quinn 마네킹의 베이크된 idle/run 동작을 재사용합니다. 원본 에셋은 `Environment Assets/`에 남겨 두고, 실행용 경량 사본은 Git에서 제외된 `build/local-assets/ancient-ruins/`에 둡니다. 별도 에셋을 다운로드하지 않았습니다.

## Phase 1 — 실제 에셋과 호환성

| 원본 | 확인 내용 |
| --- | --- |
| `Environment Assets/glb.zip` | 26개의 개별 GLB. 각 파일은 대체로 메시 1개, 삼각형 약 50만 개, PBR 재질 1개, 내장 JPEG 3개(기본색·노멀·metallic/roughness) |
| `Environment Assets/fbx.zip` | 동일한 26개 조각의 FBX 판본 |
| `Environment Assets/iso_pack1.blend` | Blender 원본 장면 |

GLB에는 `POSITION`, `NORMAL`, `TEXCOORD_0`와 인덱스가 있고, 재질에는 glTF `baseColorTexture`, `normalTexture`, `metallicRoughnessTexture`가 지정되어 있습니다. REMI의 `GltfAsset`과 `StaticGltfCache`는 이 정적 glTF 구성과 내장 이미지를 처리합니다. FBX 경유 형식 변환은 필요하지 않습니다. 다만 원본 하나가 약 50만 삼각형이므로 반복 배치를 위해 **별도 16,000삼각형 수준의 사본**을 만들었습니다. A–H 8종의 변환 결과와 원본별 삼각형 수는 로컬 `manifest.json`에서 확인할 수 있습니다. 원본 ZIP은 수정하지 않습니다.

## Phase 2 — 장면과 이동

`games/maze/main.cpp`는 고정 시드의 13×13 미로를 생성합니다. 입구는 북쪽 `(1,0)`, 출구는 남쪽 `(11,12)`입니다. 길이 아닌 칸에 A–H 폐허 구조물을 배치하고, 원경에는 같은 조각의 큰 실루엣을 재사용합니다. 통로와 출구 경로는 항상 연결됩니다. 색이 조금씩 다른 타일을 합친 돌바닥, 정점 색을 사용한 흐린 하늘 돔, 출구의 발광 룬은 별도 다운로드 없이 생성합니다.

플레이어는 UE5 Quinn 마네킹과 3인칭 궤도 카메라, 엔진의 동적 AABB Body를 사용합니다. 마우스로 시선을 회전하고 휠로 줌하며, 뒤쪽 벽에 카메라가 들어가지 않도록 미로 칸을 따라 거리를 줄입니다. 벽 조각마다 별도의 보이지 않는 AABB Body가 있고, 바닥도 AABB입니다. 콜리전은 폐허의 정확한 형상과 일치하지 않는 **게임용 프록시**입니다. 출구 가까이 접근하면 중앙에 완료 화면이 나타나고 `Retry` 또는 `Quit`을 선택할 수 있습니다.

바닥은 평평한 단색 쿼드 대신, 균열이 보이는 틈·서로 다른 높이·깨진 가장자리를 가진 석판 메시로 만들었습니다. 각 석판은 색과 면 노멀을 달리하며, 로컬에서 생성한 타일형 돌 기본색/노멀 텍스처를 PBR 재질로 사용합니다. 물리 충돌은 계속 평평한 바닥 프록시로 처리합니다.

## Phase 3–4 — 엔진 시스템 재사용

- `StaticGltfCache`가 같은 폐허 GLB의 메시·재질을 한 번 로드하고 여러 Scene 엔티티가 같은 핸들을 참조합니다.
- GLB의 기본색·노멀·metallic/roughness 텍스처를 기존 PBR 경로로 전달합니다. Shadow Map, Frustum Culling, Forward/Deferred 전환도 기존 `SceneRenderSession`과 `Renderer`를 사용합니다.
- 방향광은 흐린 낮으로 조정했고, 출구 주변에 기존 PointLight를 배치했습니다. 하늘 돔은 게임 장면의 시각적 배경이며 HDRI/IBL 구현을 뜻하지 않습니다.
- 평소 HUD는 숨겨져 있습니다. `F2`를 누르면 우측 상단에 검은 배경·흰 글씨의 조작/성능 패널이 나타납니다. `F1` 컬링 전환, `F3` Scene Inspector, `F4` 와이어프레임, `F6` Forward/Deferred 전환을 기존 시스템에 연결했습니다.

## Phase 5 — 직접 실행과 확인

에셋 경량화는 Blender 5.1에서 `tools/assets/prepare_ancient_ruins.py`를 백그라운드 스크립트로 실행합니다. 현재 작업 공간에는 이미 `build/local-assets/ancient-ruins/ruin_a.glb`부터 `ruin_h.glb`까지 준비되어 있습니다. 원본 ZIP이 필요한 새 체크아웃에서는 Visual Studio 또는 터미널에서 다음을 실행할 수 있습니다.

```powershell
& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' --background --python tools/assets/prepare_ancient_ruins.py
```

Visual Studio의 CMake 대상에서 `REMIMazeEscape`를 선택해 **Release**로 빌드하고 시작하면 됩니다. 또는 기존 프로젝트의 CMake 프리셋을 사용해 `REMIMazeEscape` 대상만 빌드한 뒤 `build/vs2022-x64/bin/Release/REMIMazeEscape.exe`를 실행할 수 있습니다. 작업 디렉터리는 저장소 루트로 설정합니다. 셰이더는 기존 `remi_sandbox_shaders` 빌드 대상이 실행 폴더에 준비합니다. 이 데모와 외부 GLB는 기본 배포 ZIP 대상에 추가하지 않았습니다.

| 조작 | 동작 |
| --- | --- |
| `WASD` / 마우스 / 휠 | 이동 / 3인칭 카메라 회전 / 줌 |
| `Shift` | 달리기 |
| `Esc` | 커서 해제, 다시 누르면 종료. 화면 클릭으로 시선 잠금 복귀 |
| `←` / `→` 또는 `A` / `D` | 완료 화면에서 Retry / Quit 선택 |
| `Enter` / `Space` 또는 버튼 클릭 | 완료 화면에서 선택 실행 |
| `F1` / `F2` / `F3` | 컬링 전환 / 우측 상단 UI 표시·숨김 / Inspector |
| `F4` / `F6` | 와이어프레임 / Forward–Deferred 전환 |

직접 실행 시 확인할 순서는 (1) Quinn의 idle/run 동작·크기·방향과 카메라 벽 충돌, (2) 바닥의 색·거칠기·균열과 원본 폐허의 톤, (3) 시작 시 UI 숨김·`F2` 패널 전환, (4) 입구에서 출구까지 이동 후 중앙 완료 화면과 Retry/Quit, (5) `F1`·`F3`·`F4`·`F6` 단축키와 Inspector 복귀, (6) Forward/Deferred 양쪽에서 하늘·출구 발광·그림자 표시, (7) 디버그 레이어 경고와 실제 GPU 성능입니다. **새 UI 수정본의 빌드와 실행 결과는 아직 확인하지 않았습니다.**

Ancient Ruins 원본의 정확한 배포 라이선스는 구입한 Fab 라이브러리 항목에서 확인해야 합니다. 원본 ZIP이나 변환 GLB를 공개 배포물에 넣기 전에 그 권한을 확인하세요.
