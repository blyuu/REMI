<div align="center">

<pre>
 ____   _____  __  __  ___
|  _ \ | ____||  \/  ||_ _|
| |_) ||  _|  | |\/| | | |
|  _ < | |___ | |  | | | |
|_| \_\|_____||_|  |_||___|
</pre>

# REMI

**Windows / DirectX 11 커스텀 게임 엔진 — 단계별 개발 프로젝트**

C++20 · DirectX 11 · CMake · 외부 의존성 없음

</div>

---

## Overview

REMI는 외부 엔진(Unreal/Unity) 없이 밑바닥부터 만드는 **Windows x64 / DirectX 11 게임 엔진**입니다.

---

## Key Features

| 모듈 | 설명 |
|---|---|
| **Core** | 시간(고정 tick 누적), 로그, 입력 상태, 수명(Lifetime) 유틸리티, 리소스 핸들, 공용 수학(`Math`) |
| **Platform** | Win32 창 생성/이벤트/리사이즈, 입력 수집 |
| **Runtime** | `Application` 기반 프레임 루프(입력 → 고정 tick → 렌더 → Present), 시스템 생성/종료 순서 관리 |
| **Scene** | index+generation `EntityId`, 컴포넌트 저장, 부모 Transform — 오래된 핸들 참조를 generation 검증으로 거부 |
| **Resources** | `ResourceManager`(파일 바이트 로딩) + 타입별 `ResourceCache<T>`(중복 로드 방지, 핸들 기반 공유) |
| **Physics** | `PhysicsWorld` — 방향 전환 가능한 중력, 정적/동적 AABB 박스, semi-implicit Euler 적분, 8회 반복 접촉 보정, 접지(Supported) 판정. Scene만 의존, 게임 규칙과 분리 |
| **Renderer (D3D11)** | 디바이스/스왑체인/깊이버퍼, `Camera`(궤도/이동), 방향광 Lambert 셰이딩, 단일 shadow map + PCF, 보수적 AABB 프러스텀 컬링, `SceneRenderer`로 Scene을 그대로 드로우 |
| **Game — REMI Gravity** | 엔진 위에서 동작하는 3D 중력 반전 퍼즐 게임(`REMIGravity`). 바닥/천장을 오가며 목표 지점 도달, 승패 판정 |
| **Testing** | CTest 기반 자동 회귀 13종(Core/Platform/Scene/Memory/Resources/Lighting/Physics/Game + 스모크 테스트) + ASan CPU 수명 검증 |

---

## Getting Started

### 요구 환경

Windows 10/11 x64, Visual Studio 2022(Desktop development with C++, Windows 10/11 SDK), CMake 3.25 이상.

### 빌드 & 테스트

```powershell
cmake --preset vs2022-x64
cmake --build --preset debug
ctest --preset debug

cmake --build --preset release
ctest --preset release
```

CMake가 PATH에 없다면 설치 경로의 `cmake.exe` / `ctest.exe`를 직접 사용하세요.
생성된 `build/vs2022-x64/REMI.sln`을 Visual Studio 2022에서 열어도 됩니다. 시작 프로젝트는 `REMIGravity`입니다.

실행 파일 위치: `build/vs2022-x64/bin/Debug/REMIGravity.exe` (`REMISandbox.exe`, `REMIBootstrap.exe`도 같은 위치). 셰이더는 빌드 시 EXE 옆 `shaders/` 폴더로 자동 복사됩니다.

### AddressSanitizer 빌드 (메모리/수명 검증)

```powershell
cmake --preset vs2022-x64-asan
cmake --build --preset asan
ctest --preset asan
```

---

## Usage

### REMIGravity — 중력 반전 게임

바닥과 천장을 오가며 목표 지점(초록 패드)에 도달하는 3D 퍼즐입니다.

| 입력 | 동작 |
|---|---|
| `W A S D` | 이동 |
| `Space` | 접지 상태에서 중력 반전 |
| `Enter` | 재시작 |
| 마우스 우클릭 드래그 | 카메라 궤도 |
| 휠 | 줌 |
| `F1` | 카메라 초기화 |
| `Esc` | 종료 |

### REMISandbox — 렌더링/물리 데모

회전 큐브, 바닥, 방향광 그림자, 박스 물리를 확인하는 데모입니다.

| 입력 | 동작 |
|---|---|
| 마우스 우클릭 드래그 | 카메라 궤도 |
| 휠 | 줌 |
| `W A S D` | 이동 |
| `Space` | 큐브 회전 정지/재개 |
| `Q` / `E` | 자식 큐브 생성 / 마지막 생성 큐브 삭제 |
| `Enter` | 물리 큐브 재낙하 |
| `F1` | 카메라 초기화 |
| `Esc` | 종료 |

`REMIBootstrap`은 최소 빌드 확인용 EXE로, 별도 조작 없이 부트스트랩 성공 여부만 확인합니다.

---

## Project Structure

```
REMI/
├─ engine/                     엔진 코어 (정적 라이브러리 모음)
│  ├─ include/remi/            공개 헤더 (core / platform / runtime / scene / resources / physics / render)
│  └─ src/                     구현
├─ apps/
│  ├─ bootstrap/                최소 빌드 확인 EXE (REMIBootstrap)
│  └─ sandbox/                  렌더링·물리 데모 EXE (REMISandbox)
├─ games/
│  └─ gravity/                  중력 반전 퍼즐 게임 (REMIGravity)
├─ shaders/                     HLSL 셰이더 (Basic.hlsl)
├─ tests/                       CTest 기반 회귀 테스트
├─ docs/                        설계·단계별 문서 (로컬 전용, Git 제외)
├─ versions/                    단계별 빌드/테스트 로그·소스 스냅샷 (로컬 전용, Git 제외)
├─ CMakeLists.txt
└─ CMakePresets.json
```

---

## Requirements

| | |
|---|---|
| **언어/표준** | C++20 |
| **그래픽 API** | DirectX 11 |
| **컴파일러** | MSVC (Visual Studio 2022) |
| **빌드 시스템** | CMake ≥ 3.25 |
| **플랫폼** | Windows x64 |
| **외부 의존성** | 없음 — 표준 라이브러리 + Win32 + D3D11만 사용 |
| **상태** | 진행 중 (v0.8.0) |
