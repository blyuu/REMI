# Phase 0 — 새 엔진의 기준점 잡기

> 2026-09-29 당시 기록을 바탕으로 다시 정리했음. 이 문서는 지금 기능 목록이 아니라 Phase 0에서 내린 설계 선택의 로그임.

## 왜 이렇게 시작했음

이전 REMI를 그대로 옮기면 구조가 뒤섞일 것 같아서 새 프로젝트를 기준점으로 잡았음. 처음부터 Windows x64, C++20, Visual Studio 2022, CMake로 빌드 조건을 고정했음. 목표는 “창이나 렌더러를 만들기 전에 최소 EXE가 Debug/Release에서 확실히 빌드되는가”였음.

메모리 관리도 Phase 4까지 미루지 않기로 했음. 엔진 객체의 소유자는 하나로 두고, 나중에 Scene과 Renderer가 붙어도 해제 순서를 설명할 수 있도록 RAII와 모듈 경계를 먼저 정했음.

## 이 단계에서 했음

- Core 정적 라이브러리와 `REMIBootstrap` 콘솔 EXE를 만들었음.
- VS2022 x64 Debug/Release 프리셋을 만들고, 빌드·테스트 명령을 한 경로로 맞췄음.
- Core → Platform/Runtime → Scene/Renderer 같은 단계별 의존 방향과 소유권 원칙을 잡았음.

## 어떻게 확인했음

VS2022/MSVC 19.38, Windows SDK 10.0.22621.0에서 Debug와 Release를 빌드하고 Bootstrap 실행 테스트가 통과했음. 당시 로그와 소스 사본은 로컬 `versions/phase-00/`에 남겨 뒀음. 이 검증은 빌드 메타데이터 출력과 정상 종료만 확인한 것이고, 창·DirectX·게임 동작을 확인한 것은 아님.

다음 Phase에서는 Win32 창, 입력, 시간과 메인 루프를 붙이기로 했음.
