# 재현 가능한 성능·수명 사례

측정일: 2026-10-05. Windows x64, Intel Core i9-14900HX, MSVC 19.38.33145, Release 빌드. 렌더링은 **D3D11 WARP 소프트웨어 드라이버, debug layer ON, 64×64, VSync OFF**다. 이 결과는 작은 진단 장면의 측정값이며 하드웨어 GPU 게임 프레임률이나 전체 엔진의 성능 향상률로 일반화하지 않는다.

## 1. 컬링: 제출을 줄이면서 같은 픽셀을 유지하기

문제는 화면 밖 객체도 드로 명령을 만드는 것이다. 하나의 정적 삼각형 Mesh를 1,000개 엔티티가 공유하고, 20개는 화면 안에, 980개는 오른쪽 화면 밖에 둔다. identity view-projection을 사용한다. 로컬 AABB의 8개 모서리에 대한 클립 공간 검사를 켜고 끈다.

| 측정 | 컬링 OFF | 컬링 ON |
| --- | ---: | ---: |
| 색상 draw 수 | 1,000 | 20 |
| CPU `DrawScene` 제출 중앙값 | 10.6082 ms | 0.3475 ms |
| WARP GPU query의 color 구간 중앙값 | 12.1184 ms | 0.1264 ms |
| 유효 GPU 샘플 수 | 100 | 100 |

각 설정을 10프레임 예열하고, 실행 순서를 번갈아 각 100개 샘플을 수집한다. CPU 측정은 `DrawScene`만 포함한다. GPU color 구간에는 색상 타깃 clear와 드로가 포함되며, profiler의 shadow 경계 생성을 위해 실행한 빈 shadow pass는 color 수치에 포함되지 않는다. CPU·GPU 시간은 서로 겹치는 구간이므로 합산하지 않는다.

각 프레임의 GPU readback과 Present는 CPU 측정 밖에서 수행한다. readback으로 작업을 완료시킨 뒤 `EndGpuProfile`이 반환한 제출 ID와 완료된 GPU 샘플 ID가 같은 경우만 기록한다. 모든 프레임의 RGBA가 기준 프레임과 같은지도 검사한다. 이는 샘플을 분리한 진단용 측정이며 일반 게임 루프에는 이 동기화를 넣지 않는다. WARP timestamp는 소프트웨어 렌더링 경로의 결과다.

## 2. 파일 캐시: 같은 파일 요청의 재읽기 피하기

984바이트 `static_triangle.glb`를 한 번 읽은 뒤 캐시 적중과 명시적 unload 후 재로드를 각각 2,000회 측정했다. 두 경로 모두 `ResourceManager::LoadFile`의 경로 정규화를 포함한다. unload 및 CSV 쓰기 시간은 제외한다.

| 경로 | 중앙값 |
| --- | ---: |
| 캐시 적중 | 0.0344 ms |
| 캐시 미적중·파일 재로드 | 0.0999 ms |

이 결과는 **OS 파일 캐시가 따뜻한 상태에서 엔진의 파일 바이트 캐시**를 비교한다. 디스크 cold load, glTF 파싱, 이미지 디코딩, GPU 업로드 시간은 측정하지 않는다. 작은 파일에서는 경로 정규화 비용도 상당 부분을 차지할 수 있다.

위 두 사례의 원시 샘플은 [culling-cache.csv](benchmarks/culling-cache.csv)에 있다. 마지막 `subtree_delete` 행은 현재 구현의 참고 측정이며, 아래 이전 소스 비교와는 별도 실행이다.

```powershell
cmake --preset vs2022-x64
cmake --build --preset release --target REMIBenchmarks
build\vs2022-x64\tools\benchmarks\Release\REMIBenchmarks.exe shaders\Basic.hlsl tests\fixtures\static_triangle.glb build\samples.csv
```

## 3. 계층 삭제: 전체 슬롯 반복 검색 제거

이전 `Scene::Destroy`는 자식을 찾을 때마다 전체 슬롯을 검색했다. 별도의 스택·힙 할당 없이 삭제한다는 장점은 있지만 넓은 트리의 삭제가 O(N²)이었다. 각 슬롯에 첫 자식·다음 형제·이전 형제 인덱스를 두고 `SetParent`와 삭제에서 연결을 갱신하도록 바꿨다. 이제 삭제는 서브트리의 각 노드를 방문하는 O(N) 순회이며, 삭제 중 할당과 재귀는 여전히 없다. 대신 슬롯당 32비트 인덱스 세 개와 구조체 정렬에 따른 저장 공간을 사용한다. `Children`의 반환 순서는 기존처럼 슬롯 인덱스 순으로 유지한다.

| 동일한 5,001개 엔티티 트리 | 삭제 중앙값 |
| --- | ---: |
| 이전 전체 슬롯 검색 | 23.5416 ms |
| 현재 자식·형제 연결 순회 | 0.0691 ms |

루트 하나에 자식 5,000개를 붙이고 생성·부모 설정 시간은 제외한다. 두 번 예열 후 20회 삭제를 측정한다. 이전 `Scene.hpp`/`Scene.cpp`는 커밋 `6d6c776ccf24af87952657bcb4ab44735c81b16a`에서 읽고, 현재 소스와 같은 C++ 측정 프로그램·Core 헤더·Math 소스·Release 옵션으로 각각 빌드한다. 이전 Scene.cpp의 Git blob은 `e1c862290445e96e4e1c37ca3a83654b5a9f58ea`다. 워킹 트리의 파일을 되돌릴 필요가 없다.

```powershell
tools\benchmarks\compare_scene.ps1 -OutputDirectory build\benchmark-results
```

원시 샘플: [이전 구현](benchmarks/scene-baseline.csv), [현재 구현](benchmarks/scene-current.csv). CPU 클럭·백그라운드 작업에 따라 절대 시간은 달라질 수 있다. SceneTests는 중간 형제 제거, 재부모 지정, detach, 깊은 계층, 오래된 ID를 검사하며 MemoryTests는 할당 실패와 할당 없는 정리를 검사한다.

## 4. 에셋 수명과 실패 복구

`StaticGltfCache`는 정규화한 경로별로 정적 모델을 한 번 로드하고, 여러 Scene에는 같은 MeshHandle을 배치한다. 하나의 모델 로드 안에서 같은 이미지를 쓰는 primitive들은 기본색 텍스처 할당도 공유한다. Scene을 지워도 공유 모델은 남으며 명시적 `Unload`, `Clear`, 캐시 파괴가 GPU 메시를 해제한다. `Renderer`와 `ResourceCache<Mesh>`는 이 캐시보다 오래 살아야 한다.

재로드는 새 모델·이미지·GPU 메시를 전부 준비한 뒤 기존 캐시 객체를 교체한다. 핸들은 유지되고 `Get`으로 얻었던 생 포인터는 무효화된다. primitive 수가 바뀌거나 스킨 모델이면 교체 전에 거부한다. 기존 엔티티의 재질 값은 인스턴스별 값이므로 유지하며, 이후 생성한 인스턴스에는 새 재질을 사용한다.

`Assets.StaticCache` 테스트는 두 Scene의 공유, Scene 하나 해제 후 렌더링, 잘못된 glTF, GPU 메시 생성 뒤 이미지 디코딩 실패, 정상 재로드, 명시적 제거와 D3D 잔존 객체 검사를 수행한다. `Phase2.Rendering`은 원본 Mesh 파괴 뒤 공유 텍스처가 계속 그려지는지 확인한다. 애니메이션 glTF·RMCH의 상태와 로딩은 기존 경로에 남아 있으며, 모든 에셋을 통합한 비동기 스트리밍 시스템은 아직 없다.

재로드 중에는 이전 자원과 새 자원이 함께 존재하므로 일시적인 메모리 사용량이 증가한다. 현재 로컬 검증은 Debug 25/25, Release 25/25, CPU AddressSanitizer 6/6 통과다. Inspector 캡처 실행에서도 D3D debug layer의 live children과 warning이 모두 0이었다. 테스트 개수에는 로컬 Quinn 에셋이 있을 때 등록되는 테스트 하나가 포함된다.
