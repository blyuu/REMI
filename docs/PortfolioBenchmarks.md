# 포트폴리오 실측 절차

이 문서는 **측정 방법**이다. 2026-10-11에 수집한 실제 GPU 결과는 [Raven 하드웨어 측정](benchmarks/RavenHardware-2026-10-11.md), Job System 결과는 [glTF CPU 준비 측정](benchmarks/JobSystemHardware-2026-10-11.md)에 원시 CSV와 함께 보관한다. 기존 `PerformanceEvidence.md`의 64×64 WARP 결과와 실제 GPU 결과를 섞어 비교하지 않는다.

## 1. Raven 장면의 실제 GPU 비교

1. 본인 PC에서 CMake 프리셋 `vs2022-x64`로 솔루션을 다시 생성한 다음 `build/vs2022-x64/REMI.sln`을 Visual Studio로 열고 **Release x64**의 `REMIRaven`을 빌드한다. 새 벤치마크 타깃을 솔루션에 넣으려면 이 재생성이 필요하다. 기존 `demoexecutionfiles`의 EXE는 새 빌드 전까지 갱신되지 않으므로 아래 스크립트는 `build/vs2022-x64/bin/Release/REMIRaven.exe`를 사용한다. 로컬 Soul City/Raven 에셋은 기존 위치인 `build/local-assets/soul-city/soul_city_neighborhood.glb`, `build/local-assets/raven/raven.glb`에 둔다. 두 에셋은 저장소·공개 패키지에 포함하지 않는다.
2. GPU 드라이버 버전, GPU 모델, CPU 모델, RAM, Windows 버전, 빌드 커밋을 별도로 적는다. 엔진 로그의 `Adapter:`가 원하는 실제 GPU인지 확인한다.
3. 다음 스크립트를 **직접 실행**한다. 스크립트는 빌드하지 않고 이미 만든 EXE를 네 번 실행한다. 각 실행은 Raven의 같은 Soul City 장면, 1920×1080, 하드웨어 D3D11, VSync OFF, 120프레임 예열과 300프레임 수집이다. 계측 모드는 게임 입력·단축키를 무시해 카메라와 경로를 고정한다. 창 크기는 바꾸지 않는다. 스크립트는 프레임 수와 CSV의 경로·컬링·해상도를 검사한다.

```powershell
.\tools\benchmarks\capture-portfolio.ps1
```

출력은 `build/portfolio-evidence/{deferred,forward}-culling-{on,off}.csv` 네 파일이다. 필요한 경우 `-Frames`, `-Warmup`, `-OutputDirectory`, `-Executable`을 지정한다. 원시 CSV와 측정 환경을 함께 보관한다. 추가 반복 측정은 출력 폴더를 다르게 지정해 원본을 보존한다.

CSV의 `cpu_update_ms`는 한 렌더 프레임 사이에 수행한 고정 업데이트 함수의 누적 CPU 시간이다. `cpu_render_submit_ms`는 그림자·장면 색상·디퍼드 조명·후처리를 CPU가 제출하는 시간이다. UI 제출과 `Present` 대기 시간은 포함하지 않는다. `gpu_total_ms`는 그림자 시작부터 UI 제출 종료까지의 GPU timestamp 구간이다. Present 대기나 디스플레이 표시 시간은 아니다. CPU와 GPU 시간은 겹칠 수 있으므로 더하지 않는다.

`draw_calls`는 해당 프레임에서 Renderer가 실제로 제출한 indexed draw 수다. 그림자·장면·디퍼드 전체 화면 패스·후처리·UI가 포함된다. `color_draws`와 `shadow_draws`는 Scene과 Raven primitive의 드로만 센다. `color_culled`는 정적 Scene 메시 컬링 수다. 현재 Raven은 Scene의 추가 캐릭터 드로 경로에 있으므로 F2 컬링 대상이 아니다.

`gpu_valid=0`이거나 GPU 시간 셀이 비어 있으면 측정 실패·쿼리 지연·disjoint·프로파일 슬롯 부족 등으로 유효한 GPU 샘플을 얻지 못한 프레임이다. **0ms로 취급하거나 이전 프레임 값을 대입하지 않는다.** 동일 조건의 유효 샘플만 중앙값으로 요약하고, 유효 샘플 수를 함께 적는다. 네 조건 중 하나라도 유효 GPU 샘플이 부족하면 GPU 비교 결론을 보류한다.

첫 하드웨어 측정의 4조건 표, 유효 샘플 수, 실행 파일 해시와 원시 CSV는 [Raven 하드웨어 측정](benchmarks/RavenHardware-2026-10-11.md)에 기록했다. CPU Update는 고정 tick이 없는 프레임에서 0이 되므로 해당 표에 **평균**으로 표시한다.

## 2. glTF CPU 준비의 Job System 비교

같은 Visual Studio 솔루션에서 Release x64의 `REMIGltfPrepBenchmark`를 빌드한다. 아래 스크립트는 작은 fixture와 Raven 모델을 순서대로 측정하며, 빌드를 자동으로 시작하지 않는다. 2026-10-11 실측 결과는 [별도 문서](benchmarks/JobSystemHardware-2026-10-11.md)에 있다.

```powershell
.\tools\benchmarks\capture-jobs.ps1
```

출력은 `build/portfolio-evidence/jobs-small.csv`와 `jobs-model.csv`다. 다른 glTF는 `-Model <파일>`로 지정한다. `-Repeats`, `-OutputDirectory`, `-Executable`도 바꿀 수 있다.

모델은 **한 번만 파싱**하고, 같은 primitive 데이터를 1·4·16회 참조해 작업 크기를 늘린다. 각 크기에서 순차/병렬의 실행 순서를 번갈아 측정한다. 처음 두 쌍은 예열이고, 이후 각 모드 30회 원시 시간이 CSV에 기록된다. 동일한 `source`, `primitive_copies`, `vertices`, `indices`, `grain` 조건끼리 비교한다. CPU 변환 결과의 fingerprint가 다르면 프로그램이 실패한다. 준비 결과가 512MiB를 넘는 큰 조건은 건너뛴다. 입력 primitive가 하나뿐인 1회 조건에서는 엔진의 기존 규칙대로 병렬 모드도 순차 처리로 돌아가므로, 스케줄링 오버헤드는 4회·16회 조건에서 본다.

측정 시간은 **glTF 정점 좌표계 변환과 삼각형 인덱스 순서 변경**에 한정된다. 파일 읽기·파싱, 텍스처 디코딩, D3D 메시 생성, 전체 게임 로딩 시간의 가속률로 표현하지 않는다. 작은 입력에서 병렬 쪽이 느리면 그 오버헤드를 그대로 기록한다.

## 3. 통합 시연

Release `REMIRaven.exe`를 실행하면 Soul City + Raven의 디퍼드 PBR 장면이 시작된다. `--forward`는 포워드로 시작한다. 키는 `F1` 성능 오버레이, `F2` 정적 Scene 프러스텀 컬링 ON/OFF, `F3` Inspector, `F4` Wireframe, `F6` Forward/Deferred 전환이다. Inspector가 열리면 물리 업데이트가 멈추며 방향키로 엔티티를 고를 수 있다. `Esc`로 커서를 풀고 빈 곳을 클릭하면 카메라 조작으로 돌아간다. `F5`는 이 데모에서 사용하지 않는다.

시연 영상에서는 먼저 정적 카메라에서 F2와 F6을 보여주고, 각 상태의 오버레이와 실제 GPU CSV 요약을 붙인다. F4와 F3은 구조 설명용으로 별도 촬영한다. Wireframe·Inspector·오버레이 자체의 드로 비용이 비교 결과에 섞이지 않도록 정량 측정 중에는 이들을 끈다.
