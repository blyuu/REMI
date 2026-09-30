# Phase 6 — 방향광, 그림자, 컬링

## 왜 이 구조로 갔음

Phase 2의 큐브는 면별 정점 색만 달랐음. 장면 전체가 같은 광원 규칙을 사용하도록 방향광을 엔진 공통 렌더링에 넣고, 같은 Scene을 광원과 카메라 시점에서 각각 그리기로 했음.

## 이 단계에서 구현했음

- `DirectionalShadowMatrix`로 LH 직교 광원 행렬을 만들고, 먼저 depth-only 그림자 패스를 그렸음. 2048² R32 typeless 텍스처를 D32 DSV/R32 SRV로 사용했음.
- 같은 텍스처를 DSV와 SRV에 동시에 바인딩하지 않도록 그림자 패스 시작 전에 SRV를 해제했음. 색상 패스에서는 3×3 comparison PCF로 가시도를 계산했음.
- 초기 조명은 Lambert 확산광과 상수 주변광으로 뒀음. 주변광은 그림자에 가리지 않도록 했음. PBR이나 IBL은 이 단계 목표가 아니었음.
- Mesh 생성 시 로컬 AABB를 구하고 여덟 꼭짓점을 clip space로 변환해 보수적으로 컬링했음. 카메라와 광원 패스에서 각각 검사했음. 카메라에서 보이지 않아도 그림자를 만드는 물체는 광원 패스에 남을 수 있음.

## 확인한 결과

WARP 64×64 픽셀 테스트에서 회색 물체의 조명 방향에 따라 R=196/63을 확인했고, 가림 물체가 있을 때 shadow 픽셀도 R=63이었음. 컬링 on/off의 draw 2→1과 최종 RGBA 완전 일치도 확인했음. Debug/Release 각각 10개 테스트가 통과했음. 당시 RTX 4070 Laptop GPU 실행에서는 shadow/color 각 5 draw, debug layer 경고와 종료 잔존 child 0을 기록했음. 스크린샷과 로그는 로컬 `versions/phase-06/`에 있음.

초기 화면에서 자기 그림자 줄무늬가 보여 slope bias를 추가했음. 그래도 bias와 PCF가 모든 거리에서 완벽한 것은 아님. 이때는 color target을 불필요하게 한 번 더 clear하는 경로도 있었는데 Phase 10에서 제거했음. CSM, 자동 shadow fitting, 점광원, HDR은 아직 없었음.
