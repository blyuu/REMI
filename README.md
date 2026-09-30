# REMI Engine

**C++20 · DirectX 11 · HLSL · Windows x64**

REMI는 렌더링, 장면, 리소스, 물리, 수명 관리를 직접 구현하며 확장하는 소형 게임 엔진입니다. `REMIGravity`는 엔진으로 만든 중력 반전 퍼즐 데모입니다. 천장의 코인 3개를 모아 출구에 도착하는 플레이를 확인할 수 있습니다.

![REMI Gravity Run 플레이 데모](media/gravity-run-demo.gif)

## 주요 기능

| 분야 | 현재 구현 |
| --- | --- |
| 렌더링 | D3D11 forward 렌더러, HLSL, 방향광, 2048² shadow map과 3×3 PCF, static mesh 프러스텀 컬링 |
| 장면·리소스 | 계층 Transform, 세대 검증 EntityId, 핸들 기반 Mesh/파일 캐시 |
| 캐릭터 | 이름 있는 베이크 애니메이션 클립, Blender 변환 도구, 실행 시 캐릭터 파일 선택 |
| 게임·진단 | AABB 물리와 중력 반전 데모, Pretendard HUD, CPU/GPU 시간 CSV, D3D debug layer 검사 |

현재 셰이더의 `metallic`·`roughness`는 **간이 조명 모델의 조정값**입니다. PBR·IBL·런타임 스켈레탈 스키닝이나 콘솔 지원을 구현했다고 주장하지 않습니다.

## 실행

Windows 10/11 x64, Visual Studio 2022의 C++ 데스크톱 도구와 Windows SDK, CMake 3.25 이상이 필요합니다.

```powershell
cmake --preset vs2022-x64
cmake --build --preset debug
ctest --preset debug
build\vs2022-x64\bin\Debug\REMIGravity.exe
```

`WASD` 이동, `Space` 중력 반전, `Q` 재질 프리셋, 우클릭 드래그 카메라 회전, 휠 확대/축소, `Enter` 재시작, `F2` 성능 표시, `Esc` 종료입니다. 로컬 캐릭터 에셋이 없으면 박스 캐릭터로 실행됩니다. 다른 베이크 캐릭터는 `--character <파일> --idle-clip <이름> --move-clip <이름>`으로 선택할 수 있습니다.

Release/AddressSanitizer 빌드, Blender 변환 과정과 에셋 형식은 아래 기술 문서에 정리했습니다. 기본 실행 파일 외에 `REMISandbox` 렌더링·물리 데모와 `REMIBootstrap` 최소 실행 검증 앱이 있습니다.

## 기술 문서

- [Architecture](docs/Architecture.md) — 모듈 관계, 프레임 루프, 장면·리소스·캐릭터 경계
- [RenderingPipeline](docs/RenderingPipeline.md) — DirectX 11 초기화, 좌표 변환, 컬링, 그림자·색상 패스, 계측
- [ShaderImplementation](docs/ShaderImplementation.md) — HLSL 조명 수식, 재질 변수와 시각 품질의 한계
- [MemoryManagement](docs/MemoryManagement.md) — 객체 소유권, 핸들 무효화, 종료·누수 검증
- [Phase별 설계 로그](docs/PhaseLogs.md) — Phase 0~10에서 선택한 구조, 검증 결과와 당시 한계

코드의 출발점은 `engine/include/remi/`, `engine/src/`, `shaders/Basic.hlsl`, `games/gravity/`입니다. 현재 버전은 **0.11.0**입니다.
