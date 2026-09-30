# Phase 9 — 디버그 화면과 CPU/GPU 계측

## 왜 넣었음

화면이 빨라 보인다는 느낌만으로 최적화 방향을 정할 수 없었음. FPS 하나 대신 CPU의 물리·그림자·색상·Present 구간과 GPU의 그림자·색상 구간을 분리해서 보려고 했음.

## 구현 방식

- F2 디버그 패널에 FPS/frame, CPU·GPU 구간, draw/cull, Scene·Physics 개수, Mesh 수를 표시했음. 별도 UI 라이브러리 대신 엔진 Mesh 경로로 작은 픽셀 글꼴을 그렸음. 그래서 패널 자체의 드로우 비용도 화면 경로에 포함됨.
- D3D11 timestamp/disjoint query 네 세트를 순환 사용했음. 결과가 준비되지 않으면 CPU가 기다리지 않고 해당 프레임의 샘플을 건너뜀. `GPU WAITING FOR SAMPLE`은 아직 유효한 결과가 없다는 뜻임.
- CSV의 `gpu_sample_id`는 비동기 query 순번임. 같은 결과가 여러 CSV 행에 반복될 수 있어서 GPU 평균은 유효한 ID를 중복 제거해서 계산했음. CPU 수치와 같은 행에 있다고 같은 프레임의 GPU 작업이라고 보지 않았음.

## 당시 측정

VS2022 Release, RTX 4070 Laptop GPU, 1280×720, VSync OFF, 숨김 창, 시작 장면, 240프레임 중 처음 30프레임 제외 조건이었음. 프레임 간격 210개 평균 0.2141ms, 완료된 고유 GPU query 188개 평균 0.1112ms였음. 원시 CSV는 로컬 `versions/phase-09/release-240frames.csv`에 있음. Debug/Release 각각 14개 테스트와 CPU ASan 6개 테스트가 통과했음.

이 값은 **숨김 창의 작은 고정 장면** 결과임. 실제 플레이 FPS나 입력 지연으로 쓰지 않기로 했음. 노트북 전력·온도와 외부 GPU 부하를 완전히 고정하지 못했고, Present/VSync 대기와 GPU timestamp의 의미도 다름. 이 불확실성을 줄이려고 Phase 10에서 구버전과 새 버전을 번갈아 측정했음.
