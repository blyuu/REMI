# Phase 10 — 측정하면서 정리한 렌더 경로

## 문제와 수정

Phase 9에서 계측 기반을 만들었으니, Phase 10에서는 작은 고정 장면의 불필요한 작업을 실제로 줄여 봤음.

- `BeginShadow`가 메인 color target까지 먼저 clear하던 경로를 없앴음. Shadow DSV만 초기화하고 `EndShadow` 뒤 color pass에서 메인 화면을 한 번만 clear하도록 했음.
- 장면 순회에서 엔티티 벡터 스냅샷을 매번 만드는 대신 할당 없는 `ForEachEntity`를 사용했음. 순회 중 Scene 변경은 금지하는 계약임.
- 컬링에 이미 계산한 `world * viewProjection`을 `DrawLitPrepared`에 넘겨 같은 행렬 곱셈을 반복하지 않았음.

## 비교를 어떻게 했음

Phase 9의 보관 소스를 별도 Release 빌드해 이전 버전으로 썼음. RTX 4070 Laptop GPU, 1280×720, 숨김 창, VSync OFF, 동일 시작 장면·UI 켬·디버그 레이어 ON 조건에서 old→new 세 쌍, new→old 세 쌍으로 측정했음. 각 실행은 1200프레임, 처음 60프레임을 제외했음. GPU query는 `sample_id`를 중복 제거했음.

| 지표 | 이전 6회 평균 | Phase 10 6회 평균 | 쌍별 개선율 평균 |
| --- | ---: | ---: | ---: |
| 평활 프레임 간격 | 0.5126 ms | 0.4423 ms | 12.5% |
| CPU 그림자 구간 | 0.0405 ms | 0.0327 ms | 19.0% |
| 완료된 고유 GPU query total | 0.3075 ms | 0.1854 ms | 38.7% |

여섯 쌍에서 새 버전의 프레임·CPU 그림자 평균은 모두 낮았음. 하지만 노트북 전력·온도·드라이버 상태를 고정하지 못했고 GPU 값도 실행 간 편차가 컸음. 이 수치를 모든 장면에서 보장되는 개선율로 말하지 않기로 했음. CSV와 분석 스크립트는 로컬 `versions/phase-10/`에 보관했음.

## 안정성도 확인했음

당시 Debug/Release 각각 15/15 테스트, CPU ASan 6/6 테스트가 통과했음. WARP에서 128회 resize·shadow·readback, 게임 1000회 Restart, RTX에서 숨김 창 10000프레임 실행을 확인했음. 해당 검증에서 D3D 경고와 종료 잔존 child가 0이었음.

Scene이 작아서 대규모 장면의 컬링·메모리 효과는 아직 검증하지 못했음. 이 단계 이후에도 캐릭터, 코인, HUD가 추가됐으므로 이 표를 현재 버전 전체의 성능 수치로 쓰면 안 됨.
