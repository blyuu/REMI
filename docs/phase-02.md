# Phase 2 — DirectX 11 화면 출력

## 왜 이 순서로 했음

먼저 큐브와 바닥을 확실히 그리는 최소 렌더러가 필요했음. Scene이나 모델 로더까지 한꺼번에 붙이지 않고, 샘플 앱이 직접 Mesh를 만들고 그리게 해서 D3D11 장치·셰이더·깊이·resize 경로를 따로 검증했음.

## 구현한 구조

- D3D11 feature level 11.0 디바이스, backbuffer 2개의 flip-discard swap chain, D24S8 깊이 버퍼를 만들었음. 출력 RTV는 sRGB로 설정했음.
- CPU/HLSL에서 row-major 행렬과 행 벡터 규칙을 맞췄음. `position * model * view * projection` 순서임. 카메라는 LH 원근 투영과 타깃 공전으로 구성했음.
- Window가 resize를 알려 주면 기존 출력 바인딩과 RTV/DSV 참조를 풀고 `ResizeBuffers`를 호출하게 했음. 최소화로 크기가 0일 때는 재생성하지 않음.
- Renderer/Mesh는 pImpl과 `ComPtr`로 D3D 자원을 소유함. 샘플에서는 Mesh → Renderer → Window 순서로 해제했음. 다른 디바이스의 Mesh를 그리면 거부함.
- 셰이더는 EXE 위치의 `shaders/Basic.hlsl`에서 읽도록 했음. 실행 작업 디렉터리에 의존하지 않게 하려는 선택이었음.

## 검증했음

WARP에서 실제 D3D 셰이더와 swap chain을 사용해 픽셀 readback, 깊이 가림, 행렬 이동, near clipping, sRGB 출력, resize를 확인했음. Debug/Release 각각 5개 테스트가 통과했음. RTX 4070 Laptop GPU에서도 큐브 3개와 바닥이 출력됐고 당시 debug layer 경고/오류는 0이었음. 캡처와 로그는 로컬 `versions/phase-02/`에 남겼음.

이때 큐브의 면별 명암은 조명이 아니라 정점 색이었음. 텍스처, Scene, 그림자, 물리, 셰이더 hot reload는 아직 없었음. 장치 제거 시 자동 복구도 만들지 않았음.
