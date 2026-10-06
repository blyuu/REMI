[한국어](README.md) · [日本語](README.ja.md) · [简体中文](README.zh.md)

# REMI Engine

**C++20 · DirectX 11 · HLSL · Windows x64**

REMI는 렌더링, 장면, 리소스, 물리, 수명 관리를 직접 구현하며 확장하는 소형 게임 엔진입니다. `REMIGravity`는 중력 반전 퍼즐, `REMIRelay`는 스위치와 제한 시간이 있는 별도 게임입니다. `REMIVillage` 실행 파일은 현재 로컬 포스트아포칼립스 맵과 Survival Character를 사용하는 3인칭 탐험 프로토타입입니다.

![REMI Gravity Run 플레이 데모](media/gravity-run-demo.gif)

## 주요 기능

| 분야 | 현재 구현 |
| --- | --- |
| 렌더링 | D3D11 RHI 버퍼·텍스처·파이프라인·명령·오프스크린 타깃, 변경 감지 ShaderCache, Standard/Unlit/Toon 셰이딩, 방향광, 2048² shadow map과 3×3 PCF, static mesh 프러스텀 컬링 |
| 장면·리소스 | 계층 Transform, 세대 검증 EntityId, 선형 시간 서브트리 삭제, 핸들 기반 Mesh/파일 캐시와 실패 복구 reload |
| 에셋·캐릭터 | 런타임 glTF/GLB 메시·기본색 텍스처·재질 임포트, `.remimat` 재질 파일, CPU 본 스키닝·클립 전환 상태 머신, 기존 RMCH 베이크 애니메이션 |
| 멀티스레딩 | 종료 시 대기 중인 작업을 마치는 CPU Job System; Apocalypse 건물·캐릭터 glTF의 primitive별 정점·인덱스 준비를 워커에 분배하고 D3D11 업로드는 메인 스레드에서 수행 |
| 게임·진단 | AABB 물리, Gravity/Relay 게임과 로컬 Apocalypse 프로토타입, Noto Sans KR 설정 UI, Sandbox 위치 Inspector, CPU/GPU CSV와 재현 벤치마크, D3D debug layer 검사 |

현재 셰이더의 `metallic`·`roughness`는 **간이 조명 모델의 조정값**입니다. RHI가 리소스·드로 명령과 swap chain 표면을 담당하며, 기본 그림자 타깃·GPU 진단은 아직 D3D11 전용입니다. glTF 본 스키닝은 CPU에서 계산하며 PBR·IBL·GPU 스키닝·콘솔 지원은 아직 없습니다.

## 측정으로 확인한 개선

2026-10-05, i9-14900HX·Release 기준. 렌더링 값은 **64×64·D3D11 WARP·debug layer ON·VSync OFF** 진단 장면이다.

| 사례 | 비교 결과 |
| --- | --- |
| 1,000개 엔티티 컬링 OFF → ON | draw 1,000 → 20, CPU 제출 중앙값 10.6082 → 0.3475 ms, WARP GPU color 12.1184 → 0.1264 ms; 동일 픽셀 |
| 984바이트 파일 재로드 → 캐시 적중 | 중앙값 0.0999 → 0.0344 ms; OS 파일 캐시는 예열된 상태 |
| 5,001개 계층 삭제 | 이전 슬롯 검색 23.5416 → 자식 연결 순회 0.0691 ms; 이전 커밋과 동일 프로그램으로 비교 |

[측정 조건·재현 명령·원시 CSV·설계 이유](docs/PerformanceEvidence.md)에 범위와 한계를 기록했다. 이 값은 하드웨어 GPU의 게임 FPS를 뜻하지 않는다.

## 실행

Windows 10/11 x64, Visual Studio 2022의 C++ 데스크톱 도구와 Windows SDK, CMake 3.25 이상이 필요합니다.

### 실행 ZIP과 소스 ZIP 만들기

```powershell
powershell -ExecutionPolicy Bypass -File tools\package\build-release.ps1
```

스크립트는 현재 작업 트리의 소스 스냅샷을 만든 뒤, 그 스냅샷에서 `/MT` Release 빌드와 CTest를 수행합니다. `build\distributions\<생성 시각>\`에 Windows x64 실행 ZIP, 소스 ZIP, `SHA256SUMS.csv`, 검증 로그를 남깁니다. 실행 ZIP에는 세 프로그램과 필요한 셰이더·폰트·샘플·실행 안내만 들어갑니다. 로컬 Quinn 캐릭터 내보내기 파일은 포함하지 않으며, Gravity는 박스 캐릭터로 실행됩니다. 스크립트는 ZIP을 저장소 밖의 경로에 풀고 WARP 및 하드웨어 렌더링 smoke 실행까지 확인합니다. 배포 방법과 파일 목록은 [패키지 안내](tools/package/README.md)를 참고하세요.

소스 ZIP을 푼 뒤 Visual Studio 2022 C++ 도구와 Windows SDK가 있는 PC에서 `cmake --preset portable`, `cmake --build --preset portable`, `ctest --preset portable` 순서로 빌드와 테스트를 재현할 수 있습니다. 실행 ZIP은 별도 빌드 도구 없이 `START-HERE.txt`와 `.cmd` 실행기로 시작할 수 있습니다.

### 개발 빌드

```powershell
cmake --preset vs2022-x64
cmake --build --preset debug
ctest --preset debug
build\vs2022-x64\bin\Debug\REMIGravity.exe
build\vs2022-x64\bin\Debug\REMIRelay.exe
```

`WASD` 이동, `Space` 중력 반전, `Q` 재질 프리셋, 우클릭 드래그 카메라 회전, 휠 확대/축소, `Enter` 재시작, `F2` 성능 표시, `F5` 셰이더 재로드, `Esc` 종료입니다. [gravity.project.json](games/gravity/gravity.project.json)에서 셰이더·캐릭터·폰트 경로와 주요 레벨/물리 수치를 바꿀 수 있고 `--project <파일>`로 다른 프로젝트를 실행할 수 있습니다. 파일의 상대 경로는 해당 프로젝트 파일의 폴더를 기준으로 해석합니다. 로컬 캐릭터 에셋이 없으면 박스 캐릭터로 실행됩니다. `--character <파일>`에 `.rmc`, `.gltf`, `.glb`를 지정할 수 있습니다. `--idle-clip <이름> --move-clip <이름>`으로 동작 이름을 선택하며, glTF는 이름이 없으면 Stand/Idle 및 Run/Walk를 찾아 사용합니다. 자세한 임포트 규칙은 [AssetPipeline](docs/AssetPipeline.md)에 있습니다.

기본 코스 외에 `REMIGravity.exe --level 2`로 같은 엔진·게임 규칙을 재사용한 두 번째 코스를 실행할 수 있습니다. `--project`의 레벨 값을 바탕으로 발판 너비와 천장 높이를 조정합니다. 물리 계산은 엔진 `PhysicsWorld`, 중력 방향 전환은 `GravityControl`, 코인·승패 판정은 게임의 `GravityRules`에 분리했습니다.

`REMIRelay`는 `WASD` 이동, 우클릭 드래그 카메라, 휠 확대/축소, `Enter` 재시작, `Esc` 종료를 사용합니다. 청록색 스위치를 먼저 밟고 초록색 출구에 가면 성공합니다. 이 게임은 공통 Scene·Physics·Renderer·박스 메시 생성 경로를 사용하지만 자체 레벨 구성과 `RelayRules`를 가집니다. `--smoke --warp`로 자동 경로와 렌더링을 확인할 수 있습니다.

세 게임의 조작 안내는 화면 오른쪽 위 **설정** 아이콘을 눌렀을 때만 표시됩니다. 안내 패널과 Relay 상태 카드에는 Noto Sans KR을 사용합니다. Gravity의 게임 상태 HUD도 같은 색 계열로 맞췄습니다.

### 로컬 3인칭 포스트아포칼립스 탐험 프로토타입

실행 파일 이름은 호환성을 위해 `REMIVillage.exe`를 유지하지만 게임 화면과 기본 장면은 **REMI Apocalypse**로 바꿨습니다. 로컬 `FREE_Post_Apocalypse_Survivor_Environment_Kitbash_set-93d57f55`의 건물 FBX와 `Survival_Character-11d20d01`의 캐릭터 FBX를 Blender 5.1로 변환합니다. 건물 하나를 공유 메시로 네 곳에 배치하고 도로·바닥·거친 충돌 프록시를 코드에서 구성합니다.

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-apocalypse-assets.ps1
cmake --build --preset release --target REMIVillage
Play-Apocalypse.cmd
build\vs2022-x64\bin\Release\REMIVillage.exe --smoke --warp
```

`Play-Apocalypse.cmd`는 생성된 GLB가 없으면 변환을 실행하고, exe가 없으면 Release 빌드를 실행합니다. `Play-Village.cmd`도 호환용 별칭으로 남겨뒀습니다. `WASD`는 카메라 방향 기준 이동, `Shift`는 달리기, `Space`는 점프, 마우스 이동은 카메라 회전, 휠은 줌입니다. `Esc`로 커서를 꺼내 오른쪽 위 **설정** 아이콘을 클릭하면 조작 안내가 열립니다. 설정을 닫고 빈 곳을 클릭하면 게임으로 돌아가고, 커서가 나온 상태에서 `Esc`를 한 번 더 누르면 종료합니다. 게임 창에서 다른 창으로 전환해도 커서를 돌려줍니다.

Survival Character 원본 FBX에는 애니메이션 클립과 실제 텍스처 파일이 들어 있지 않아 변환 도구가 원본 리그에 간단한 `idle`/`run` 동작을 만들고 FBX 재질 색으로 GLB를 내보냅니다. `.glb/.gltf` 또는 기존 `.rmc`를 `--character <파일>`로 지정할 수 있으며 `--no-character`는 박스 캐릭터로 실행합니다.

Apocalypse의 정적 GLB 및 캐릭터 임포트는 최대 8개 워커(기본값: 논리 프로세서 수보다 하나 적게)를 사용하는 `JobSystem`에 primitive별 정점 좌표·인덱스 변환을 분배합니다. 준비된 결과를 모두 받은 뒤 렌더 스레드에서 GPU 메시·텍스처를 만들고 Scene/캐시를 변경합니다. 현재 Job System은 범용 프레임 그래프나 병렬 렌더러가 아닙니다. 작업의 예외는 `future`로 호출자에게 전달되며 풀을 파괴할 때 제출된 작업을 끝낸 뒤 스레드를 합칩니다. 작은 에셋은 Job System 없이 기존 순차 경로도 사용할 수 있습니다. [구현·동기화 경계](docs/Architecture.md)를 참고하세요.

건물은 시각용 정적 메시이고 바닥과 네 건물의 충돌은 수작업 AABB 프록시입니다. 작은 잔해와 건물 내부 충돌은 아직 없습니다. 건물의 어두운 컬러 아틀라스를 기본색 텍스처로 쓰지만 법선 맵·완전한 PBR 조명은 지원하지 않아 원본 외형과 차이가 있습니다. 원본 FBX와 변환된 GLB는 저장소 및 배포 ZIP에 포함하지 않으며, 이 외부 에셋이 필요한 `REMIVillage`도 배포 ZIP 대상에서 제외했습니다.

두 게임은 `SceneRenderSession`으로 렌더러·메시 캐시 수명과 그림자/색상 패스 조립을 공유합니다. 각 게임의 카메라, HUD, 애니메이션과 규칙은 별도 코드에 남겨 두었습니다.

Release/AddressSanitizer 빌드, Blender 변환 과정과 에셋 형식은 아래 기술 문서에 정리했습니다. 기본 실행 파일 외에 `REMISandbox` 렌더링·물리 데모와 `REMIBootstrap` 최소 실행 검증 앱이 있습니다.

## 기술 문서

- [Architecture](docs/Architecture.md) — 모듈 관계, 프레임 루프, 장면·리소스·캐릭터 경계
- [RenderingPipeline](docs/RenderingPipeline.md) — DirectX 11 초기화, 좌표 변환, 컬링, 그림자·색상 패스, 계측
- [ShaderImplementation](docs/ShaderImplementation.md) — HLSL 조명 수식, 재질 변수와 시각 품질의 한계
- [MemoryManagement](docs/MemoryManagement.md) — 객체 소유권, 핸들 무효화, 종료·누수 검증
- [AssetPipeline](docs/AssetPipeline.md) — glTF/GLB, 텍스처·재질 파일, 정적 장면·본 애니메이션 사용법과 제한
- [PerformanceEvidence](docs/PerformanceEvidence.md) — 컬링·캐시 비교, 계층 삭제 개선, 에셋 수명·실패 복구 검증

## Sandbox Inspector와 정적 에셋 재로드

```powershell
build\vs2022-x64\bin\Debug\REMISandbox.exe --inspector --static-gltf tests\fixtures\static_triangle.glb
```

`F3`으로 Inspector를 열고 닫는다. 편집 중에는 시뮬레이션이 멈춘다. `↑/↓`로 엔티티 선택, `Ctrl+방향키`로 로컬 X/Y 이동, `Shift+←/→`로 Z 이동, 우클릭·휠로 카메라를 조정한다. 편집은 메모리에만 적용되며 저장·undo는 없다. `F5`는 지정한 정적 glTF를 다시 읽는다. 실패하면 이전 GPU 자원을 유지한다.

`StaticGltfCache`는 여러 Scene에 같은 MeshHandle을 배치하고, Scene 해제와 에셋 제거를 분리한다. 같은 모델 안의 공통 이미지는 GPU 텍스처를 공유한다. 재로드는 primitive 수를 유지하는 정적 모델을 대상으로 한다.

![Sandbox Scene Inspector](media/scene-inspector.png)

코드의 출발점은 `engine/include/remi/`, `engine/src/`, `shaders/Basic.hlsl`, `games/gravity/`입니다. 현재 버전은 **0.11.0**입니다.
