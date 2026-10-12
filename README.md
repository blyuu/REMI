# REMI Engine

한국어 · [English](README.en.md)

**C++20 · Direct3D 11 · HLSL · Win32 · Windows x64**  
현재 버전: **0.11.0**

REMI는 렌더링, 장면, 리소스 수명, 물리, 작업 분배를 직접 구현한 3D 게임 엔진입니다. Maze Escape는 엔진을 사용하는 3인칭 게임이고, Raven은 Forward/Deferred PBR과 진단 기능을 같은 장면에서 비교하는 기술 데모입니다. Gravity와 Relay는 공통 Scene·Physics·Renderer를 재사용하는 별도 게임입니다.

## 먼저 보기

| 항목 | 확인할 내용 |
| --- | --- |
| [Maze Escape](docs/MazeEscape.md) | Ancient Ruins 조각으로 구성한 13×13 미로, Quinn 마네킹, 충돌·카메라·게임 UI |
| [Raven Rendering](docs/RavenRendering.md) | Soul City + Raven, 재질 입력, PBR 패스, Forward/Deferred 전환과 한계 |
| [Engine Architecture](docs/Architecture.md) | 모듈 의존성, 프레임 루프, RHI와 D3D11 경계 |
| [Rendering Pipeline](docs/RenderingPipeline.md) | 그림자·G-buffer·조명·후처리 순서와 GPU 계측 범위 |
| [Resource & Memory](docs/MemoryManagement.md) | 소유권, 세대 번호 핸들, 캐시·재로드·실패 복구 |
| [GPU 실측](docs/benchmarks/RavenHardware-2026-10-11.md) · [Job System 실측](docs/benchmarks/JobSystemHardware-2026-10-11.md) | 측정 환경, 원시 CSV, 결과와 한계 |

## 구현 범위

| 시스템 | 현재 구현 |
| --- | --- |
| Render / RHI | D3D11 자원·명령 추상화, Forward 및 Deferred PBR, 4개 G-buffer, 방향광·점광원, 2048² 그림자 맵과 3×3 PCF, Bloom·톤 매핑, 정적 Scene 프러스텀 컬링 |
| Material / Asset | glTF/GLB 메시와 Base Color·Normal·ORM·Emissive 텍스처, 오프라인 FBX→GLB 변환, `.remimat`, CPU MaterialGraph와 노드 GC |
| Scene / Lifetime | 계층 Transform, 세대 번호 EntityId, MeshHandle 공유, 파일·메시·텍스처 캐시, 실패 시 기존 리소스를 유지하는 재로드 |
| Runtime / Jobs | Win32 입력, 고정 시간 물리 업데이트, AABB 충돌, 최대 8개 워커의 Job System으로 glTF primitive별 CPU 준비 처리 |
| Debug / Validation | Scene Inspector, 성능 오버레이, 컬링·와이어프레임·렌더 경로 전환, CPU/GPU CSV, 회귀 테스트 |

게임은 `Application`과 `Layer`를 사용해 공통 런타임을 조립합니다. `SceneRenderSession`이 그림자·색상·후처리 호출을 묶고 `Renderer`가 `IRHI*` 인터페이스로 D3D11 명령을 제출합니다. GPU 업로드와 드로 제출은 메인 스레드에서 수행합니다. glTF CPU 준비 작업만 워커에 분배합니다.

```text
Maze / Raven / Gravity / Relay
            ↓
Application · Input · Scene · Physics
            ↓
Resources · glTF Preparation · Job System
            ↓
SceneRenderSession · Renderer · RHI
            ↓
Direct3D 11 · Win32
```

## 실측 결과

### Raven 장면: Forward/Deferred와 컬링

2026-10-11, **RTX 4070 Laptop GPU / i9-14900HX**, Release x64, 1920×1080, VSync OFF, D3D11 Debug Layer ON. 같은 카메라에서 조건마다 120프레임 예열 후 300프레임을 수집했습니다. 표의 CPU 제출·GPU 구간·Draw Calls는 중앙값입니다.

| 경로 | 컬링 | CPU 제출 | GPU 구간 | Draw Calls |
| --- | --- | ---: | ---: | ---: |
| Forward | OFF | 5.905 ms | 5.919 ms | 1,484 |
| Forward | ON | 1.777 ms | 2.180 ms | 513 |
| Deferred | OFF | 4.909 ms | 4.975 ms | 1,485 |
| Deferred | ON | 1.754 ms | 2.108 ms | 514 |

정적 Scene 메시의 컬링으로 Draw Calls는 두 경로 모두 약 **65.4%** 줄었습니다. GPU 구간 중앙값은 Forward **63.2%**, Deferred **57.6%** 감소했고, CPU 렌더 제출 중앙값도 각각 **69.9%**, **64.3%** 감소했습니다. 조건별 실행은 한 번씩이며 반복 실행 간 변동성은 아직 검증하지 않았습니다. GPU timestamp 구간은 그림자 패스 시작부터 UI 제출 종료까지로 `Present`를 제외합니다. 따라서 이 값은 전체 프레임 시간이나 FPS 개선율이 아닙니다. [원시 CSV와 측정 범위](docs/benchmarks/RavenHardware-2026-10-11.md)

### Job System: glTF CPU 준비

같은 PC의 Release x64에서 Raven GLB를 한 번 파싱한 뒤 primitive 참조를 1·4·16회 사용했습니다. 8개 워커, 모드별 2쌍 예열과 30회 측정, 실행 순서 교대 조건에서 얻은 중앙값입니다.

| Raven 작업 크기 | 순차 | 병렬 | Speedup |
| --- | ---: | ---: | ---: |
| 1× · 124,242 vertices | 1.6080 ms | 0.7942 ms | 2.02× |
| 4× · 496,968 vertices | 6.6125 ms | 1.8412 ms | 3.59× |
| 16× · 1,987,872 vertices | 27.9521 ms | 6.8457 ms | 4.08× |

작은 fixture의 12 vertices 조건은 순차 **0.0011 ms**, 병렬 **0.0258 ms**로 큐 등록·동기화 비용이 더 컸습니다. 모든 순차·병렬 출력의 fingerprint가 일치했습니다. 측정 구간은 **정점 좌표계 변환과 인덱스 순서 변경**이며 파일 파싱, 텍스처 디코딩, GPU 업로드, 전체 로딩 시간은 포함하지 않습니다. [원시 CSV와 분석](docs/benchmarks/JobSystemHardware-2026-10-11.md)

이전의 WARP 64×64 컬링 진단, 리소스 캐시, 계층 삭제 전후 비교는 [PerformanceEvidence](docs/PerformanceEvidence.md)에 별도로 보관합니다. WARP 수치와 위 실제 GPU 수치를 같은 벤치마크로 합치지 않습니다.

## 데모와 실행

| 실행 파일 | 목적 | 에셋·공개 범위 |
| --- | --- | --- |
| `REMIMazeEscape.exe` | Ancient Ruins 3인칭 미로와 엔진 시스템 통합 | 로컬 Ancient Ruins GLB·Quinn 필요 |
| `REMIRaven.exe` | Soul City에서 Raven PBR·Forward/Deferred·컬링 비교 | 로컬 Soul City·Raven 필요 |
| `REMIGravity.exe` | 중력 반전 퍼즐 | 공개 기본 실행 패키지에 포함 |
| `REMIRelay.exe` | 스위치와 제한 시간 게임 | 공개 기본 실행 패키지에 포함 |
| `REMIVillage.exe` | African Slate Quarry 탐험. 실행 파일 이름은 이전 호환성을 유지 | 로컬 Quarry·Survival Character 필요 |
| `REMISandbox.exe` | Inspector·정적 에셋 재로드 검증 | 공개 기본 실행 패키지에 포함 |

![Gravity 게임 실제 실행](media/gravity-run-demo.gif)

### 로컬 실행 파일 폴더

`demoexecutionfiles/`는 이 PC에서 만든 **로컬 실행 묶음**입니다. 외부 에셋이 포함되므로 Git에서 제외하며 공개 배포 ZIP과 구분합니다. 파일이 준비되어 있으면 `REMIMazeEscape.exe`, `REMIRaven.exe`, `REMIGravity.exe` 등을 폴더 안에서 바로 실행할 수 있습니다. `Play-MazeEscape.cmd`, `Play-Quarry.cmd` 같은 실행기는 같은 폴더의 상대 경로를 설정합니다.

이미 Release 빌드가 있다면 아래 명령으로 실행 묶음을 갱신할 수 있습니다. `-SkipBuild`는 빌드나 게임 실행을 하지 않고 기존 실행 파일과 필요한 로컬 에셋을 복사합니다.

```powershell
powershell -ExecutionPolicy Bypass -File tools\package\build-demoexecutionfiles.ps1 -SkipBuild
```

### Visual Studio에서 빌드

Windows 10/11 x64, Visual Studio 2022의 **C++ 데스크톱 개발** 및 Windows SDK, CMake 3.25 이상이 필요합니다. 먼저 저장소 루트에서 `cmake --preset vs2022-x64`를 실행한 뒤 `build/vs2022-x64/REMI.sln`을 Visual Studio로 엽니다. 구성은 **Release | x64**, 시작 프로젝트는 원하는 게임 대상(`REMIMazeEscape`, `REMIRaven` 등)으로 선택합니다. 게임의 작업 디렉터리는 저장소 루트로 설정되어 있습니다.

```powershell
cmake --preset vs2022-x64
cmake --build --preset release --target REMIMazeEscape REMIRaven
ctest --preset release
```

Maze용 경량 GLB는 `tools/assets/prepare_ancient_ruins.py`, Raven 변환은 `tools/assets/import-raven.ps1`, Quarry 변환은 `tools/assets/build-quarry-assets.ps1`로 준비합니다. 원본 에셋은 수정하지 않고 변환 결과만 `build/local-assets/`에 둡니다. [Maze 준비와 조작](docs/MazeEscape.md) · [Raven 변환과 조작](docs/RavenRendering.md) · [glTF/FBX 파이프라인](docs/AssetPipeline.md)

Raven의 `F1`은 계측 오버레이, `F2`는 컬링, `F3`은 Inspector, `F4`는 와이어프레임, `F6`은 Forward/Deferred 전환입니다. Maze에서는 `F1`이 컬링, `F2`가 우측 상단 HUD이며 `F3`·`F4`·`F6`은 동일합니다. 설정·안내 패널은 Noto Sans KR을 사용합니다. Gravity의 별도 HUD는 Pretendard를 사용합니다.

### 공개 기본 실행 패키지

`tools/package/build-release.ps1`은 소스 스냅샷에서 Release 빌드·CTest·실행 검증을 수행하고 Gravity·Relay·Sandbox를 담은 Windows x64 ZIP을 만듭니다. Maze, Raven, Quarry와 해당 외부 에셋은 이 ZIP에 포함하지 않습니다. 현재 GitHub Release에 업로드된 실행 ZIP은 없습니다. [패키지 생성·구성 안내](tools/package/README.md)

## 저장소 구조

```text
engine/             공통 런타임, Scene, 자원, Renderer, D3D11 RHI
games/              Maze Escape, Gravity, Relay, Village/Quarry·Raven 데모
apps/               Sandbox Inspector와 최소 Bootstrap
shaders/            HLSL 렌더 패스
assets/fonts/       실행에 필요한 글꼴과 라이선스
tools/assets/       FBX·Unreal·Ancient Ruins 에셋 변환
tools/benchmarks/   GPU·Job System 측정 도구
tools/package/      실행 묶음과 공개 ZIP 생성
tests/              회귀 테스트와 작은 glTF fixture
docs/               설계 문서와 측정 원본
media/              공개 데모 이미지
```

`build/`, `demoexecutionfiles/`, `versions/`는 Git에서 제외한 로컬 산출물·개발 기록입니다. Ancient Ruins·Quarry·Survival Character 원본은 로컬 폴더에, Raven ZIP은 `Models/`에, Soul City 원본은 별도 Unreal 프로젝트에 있습니다. 이 외부 에셋은 공개 소스·기본 배포 ZIP에 넣지 않습니다. 저장소 코드는 자체 구현이고 `third_party/`와 글꼴은 각 라이선스를 따릅니다.

## 현재 한계

RHI는 자원·명령·표면 경계를 제공하지만 Shadow Map·G-buffer·GPU query 일부는 D3D11 구현에 묶여 있습니다. Raven은 정적 포즈이며 원본 게임의 피부 SSS, 머리카락 이방성, IBL과 조명을 재현하지 않습니다. glTF 본 스키닝은 CPU 경로이고, Inspector는 저장·Undo가 없는 개발 도구입니다. 다른 GPU나 반복 실행에서의 성능 일반화도 아직 검증하지 않았습니다.

더 자세한 구현 근거는 [ShaderImplementation](docs/ShaderImplementation.md), [AssetPipeline](docs/AssetPipeline.md), [PortfolioBenchmarks](docs/PortfolioBenchmarks.md)를 참고하세요.
