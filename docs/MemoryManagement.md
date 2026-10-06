# 메모리와 객체 수명 관리

REMI 0.11.0은 C++ RAII를 소유권 기준으로 사용한다. 이 문서는 **누가 객체를 소유하는지, 오래된 참조를 어떻게 거부하는지, 종료 때 무엇을 검증하는지**를 설명한다. 메모리 풀이나 범용 allocator를 구현했다고 주장하지 않는다.

## 소유권 표

| 객체 | 소유자 | 참조 방식 | 해제 시점 |
| --- | --- | --- | --- |
| `Window` | `RunApplication`의 지역 객체 | 콜백의 참조 | 애플리케이션 종료 |
| `Renderer` | Gravity/Relay에서는 `SceneRenderSession`의 `unique_ptr`; 검증 앱에서는 앱 직접 소유 | 렌더 호출의 참조 | Mesh 소유자 해제 후 |
| `Mesh` | `ResourceCache<Mesh>`의 `unique_ptr` | `MeshHandle`과 resolver | 캐시 unload/clear 또는 앱 종료 |
| `Scene` | 게임의 `unique_ptr` | `EntityId` | 게임 재시작 또는 종료 |
| `FileResource` | `ResourceManager` 내부 캐시 | `FileHandle` | unload/clear 또는 manager 종료 |
| `BakedAnimation` | 앱의 `unique_ptr` | 프레임 업데이트의 참조 | 앱 종료 |
| `GltfAsset` | 앱의 `unique_ptr` | 그림자·색상 패스의 참조 | Renderer 종료 전 |
| 기본색 `IRHITexture` | 해당 텍스처를 쓰는 Mesh들의 `shared_ptr` | 드로 시 RHI 참조 | 마지막 Mesh의 텍스처 참조 해제 |
| `IRHIPipeline`/`IRHIRenderTarget` | 생성자의 `unique_ptr` | RHI 명령의 참조 | 해당 소유자 종료 전 |

Scene의 `MeshComponent`는 Mesh 객체를 소유하지 않는다. `MeshHandle`만 저장하고 그릴 때 캐시에서 해석한다. Mesh 버퍼는 `unique_ptr`, 공유 가능한 기본색 텍스처는 `shared_ptr<IRHITexture>`로 소유하며 D3D11 내부 COM 포인터는 `ComPtr`가 관리한다. `ShareMeshTexture`는 같은 장치의 Mesh 사이에서만 텍스처를 공유한다. `GltfAsset`은 여러 GPU Mesh를 직접 소유한다. 따라서 캐시와 GltfAsset을 먼저 비우고 Renderer를 해제해야 디바이스 자식 객체가 남지 않는다.

두 게임의 Layer는 `SceneRenderSession`을 단독 소유한다. 세션이 Renderer와 Mesh 캐시를 소유하고, Layer가 쓰는 Renderer/캐시 포인터는 세션 수명 안에서만 유효한 비소유 별칭이다. 종료 시 HUD·게임·애니메이션·glTF 에셋을 먼저 해제한 뒤 세션이 캐시를 비우고 `Renderer::ShutdownAndValidate`를 호출한다. 검증 앱은 기존처럼 Renderer를 직접 소유할 수 있다.

## 오래된 핸들 방지

`EntityId = (index, generation, scene)`이다. 슬롯을 재사용할 때 generation을 올리고, Scene마다 별도 식별자를 부여하므로 삭제된 ID나 다른 Scene의 ID는 `Alive` 검사를 통과하지 못한다. generation 최대값에 도달한 슬롯은 재사용하지 않는다. Scene의 `Clear`는 capacity를 유지하고 `Reset`은 저장 공간과 Scene 식별자를 교체한다.

`ResourceCache<T>`의 핸들은 `(id, owner)`를 가진다. 캐시 owner가 다르면 조회에 실패한다. ID는 `Clear` 이후에도 단조 증가해 예전 핸들이 새 객체를 가리키지 않는다. 캐시가 객체를 단독 소유하며 중복 key 로딩은 기존 핸들을 반환한다. 현재 캐시는 메인 스레드 사용을 전제로 하며, 로더 재진입을 거부한다.

## 실패 처리와 크기 검사

`ResourceCache::Reload`와 `ResourceManager::ReloadFile`은 새 객체 생성에 성공한 뒤 교체하고 기존 핸들을 유지한다. 실패하면 이전 객체를 유지한다. `Get`으로 빌린 생 포인터는 성공한 reload/unload/clear 뒤 사용할 수 없다.

정적 glTF는 `StaticGltfCache`가 경로별 MeshHandle과 재질 메타데이터를 관리한다. 실제 Mesh는 공급한 `ResourceCache<Mesh>`가 소유한다. 여러 Scene이 핸들을 공유하고 Scene 파괴만으로는 캐시를 비우지 않는다. 명시적 모델 unload 또는 StaticGltfCache 파괴가 해당 메시들을 제거한다. 공급한 Renderer·메시 캐시는 StaticGltfCache보다 오래 살아야 한다. 모델 전체를 준비한 뒤 같은 primitive 수의 메시를 교체하므로 파일·이미지 실패가 기존 Scene의 표시를 깨뜨리지 않는다. 상세 계약과 테스트는 [AssetPipeline](AssetPipeline.md), [PerformanceEvidence](PerformanceEvidence.md)에 있다.

`ResourceManager`는 파일을 읽기 전 정규화한 경로를 key로 사용하고 기본 64 MiB 크기 제한, 완전 읽기 여부, 읽는 중 파일 변경을 검사한다. 이 root는 경로 해석 기준이지 보안 샌드박스는 아니다. `ResourceCache::Load`는 로더나 map 삽입이 실패하면 부분 삽입을 되돌린다.

Renderer는 메시의 빈 데이터·범위 밖 인덱스·NaN/무한 값을 거부하고, 동적 vertex buffer 업데이트 시 같은 디바이스 소유인지와 정점 수가 일치하는지 검사한다. RHI는 다른 디바이스 소유의 버퍼·텍스처·파이프라인 사용을 거부한다. 이미지 디코더는 64메가픽셀을 초과하는 입력을 거부한다. glTF 로더는 cgltf 형식 검사와 본 수·가중치 검사를 수행한다. RMCH 로더는 헤더 버전, 파일 크기, 클립 디렉터리, 인덱스, 유한한 색·위치를 검사하고 파일 크기를 512 MiB로 제한한다. 현재 CPU 스키닝과 RMCH 프레임 보관은 큰 캐릭터에서 메모리·업로드 비용이 높다.

## 종료 검증

`LifetimeToken`은 Window/Renderer/Mesh/Scene 엔진 소유자 수를 센다. 이 값은 **소유자 수**이고 실제 바이트·드라이버 내부 할당량은 아니다. 렌더러의 `ShutdownAndValidate`는 먼저 살아 있는 Mesh가 있으면 실패시키고, 컨텍스트 state와 자체 COM 자원을 해제한 뒤 D3D debug layer의 live-object 메시지를 검사한다. Debug layer가 설치되지 않은 환경에서는 그 검증을 수행할 수 없다.

테스트에는 오래된 핸들, 캐시 unload/clear, Scene 수명, D3D 리소스 해제, 메모리 회귀가 포함된다. CPU 대상 일부 테스트는 MSVC AddressSanitizer preset으로 실행할 수 있다. `REMIGravity`의 종료 로그에 기록되는 `live children=0`, `warnings=0`은 해당 실행에서 debug layer가 관찰한 결과이며 모든 실행의 무누수를 보증하는 수치는 아니다.

## 다음 개선 과제

Scene은 슬롯마다 `std::string`, optional 컴포넌트와 자식·형제 인덱스를 보유한다. 계층 삭제는 할당·재귀 없이 O(서브트리 크기)로 수행하며 큰 장면의 다른 순회 비용은 별도 프로파일이 필요하다. CPU 스키닝과 RMCH 프레임별 배열의 비용을 비교한 뒤 압축 또는 GPU 스키닝을 검토해야 한다. 정적 glTF의 GPU 수명은 공통 캐시로 관리하지만 파일 바이트 캐시와 애니메이션 glTF/RMCH 로더는 아직 통합되어 있지 않다.
