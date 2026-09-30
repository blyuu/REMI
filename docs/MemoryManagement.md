# 메모리와 객체 수명 관리

REMI 0.11.0은 C++ RAII를 소유권 기준으로 사용한다. 이 문서는 **누가 객체를 소유하는지, 오래된 참조를 어떻게 거부하는지, 종료 때 무엇을 검증하는지**를 설명한다. 메모리 풀이나 범용 allocator를 구현했다고 주장하지 않는다.

## 소유권 표

| 객체 | 소유자 | 참조 방식 | 해제 시점 |
| --- | --- | --- | --- |
| `Window` | `RunApplication`의 지역 객체 | 콜백의 참조 | 애플리케이션 종료 |
| `Renderer` | 앱의 `unique_ptr` | 렌더 호출의 참조 | Mesh 소유자 해제 후 |
| `Mesh` | `ResourceCache<Mesh>`의 `unique_ptr` | `MeshHandle`과 resolver | 캐시 unload/clear 또는 앱 종료 |
| `Scene` | 게임의 `unique_ptr` | `EntityId` | 게임 재시작 또는 종료 |
| `FileResource` | `ResourceManager` 내부 캐시 | `FileHandle` | unload/clear 또는 manager 종료 |
| `BakedAnimation` | 앱의 `unique_ptr` | 프레임 업데이트의 참조 | 앱 종료 |

Scene의 `MeshComponent`는 Mesh 객체를 소유하지 않는다. `MeshHandle`만 저장하고 그릴 때 캐시에서 해석한다. Renderer와 Mesh의 D3D COM 포인터는 `ComPtr`가 관리한다. 따라서 Mesh 캐시를 먼저 비우고 Renderer를 해제해야 디바이스 자식 객체가 남지 않는다.

## 오래된 핸들 방지

`EntityId = (index, generation, scene)`이다. 슬롯을 재사용할 때 generation을 올리고, Scene마다 별도 식별자를 부여하므로 삭제된 ID나 다른 Scene의 ID는 `Alive` 검사를 통과하지 못한다. generation 최대값에 도달한 슬롯은 재사용하지 않는다. Scene의 `Clear`는 capacity를 유지하고 `Reset`은 저장 공간과 Scene 식별자를 교체한다.

`ResourceCache<T>`의 핸들은 `(id, owner)`를 가진다. 캐시 owner가 다르면 조회에 실패한다. ID는 `Clear` 이후에도 단조 증가해 예전 핸들이 새 객체를 가리키지 않는다. 캐시가 객체를 단독 소유하며 중복 key 로딩은 기존 핸들을 반환한다. 현재 캐시는 메인 스레드 사용을 전제로 하며, 로더 재진입을 거부한다.

## 실패 처리와 크기 검사

`ResourceManager`는 파일을 읽기 전 정규화한 경로를 key로 사용하고 기본 64 MiB 크기 제한, 완전 읽기 여부, 읽는 중 파일 변경을 검사한다. 이 root는 경로 해석 기준이지 보안 샌드박스는 아니다. `ResourceCache::Load`는 로더나 map 삽입이 실패하면 부분 삽입을 되돌린다.

Renderer는 메시의 빈 데이터·범위 밖 인덱스·NaN/무한 값을 거부하고, 동적 vertex buffer 업데이트 시 같은 디바이스 소유인지와 정점 수가 일치하는지 검사한다. RMCH 로더는 헤더 버전, 파일 크기, 클립 디렉터리, 인덱스, 유한한 색·위치를 검사하고 파일 크기를 512 MiB로 제한한다. 다만 변형 프레임마다 노멀을 미리 계산해 별도 보관하므로 대형 캐릭터에는 CPU 메모리 비용이 크다.

## 종료 검증

`LifetimeToken`은 Window/Renderer/Mesh/Scene 엔진 소유자 수를 센다. 이 값은 **소유자 수**이고 실제 바이트·드라이버 내부 할당량은 아니다. 렌더러의 `ShutdownAndValidate`는 먼저 살아 있는 Mesh가 있으면 실패시키고, 컨텍스트 state와 자체 COM 자원을 해제한 뒤 D3D debug layer의 live-object 메시지를 검사한다. Debug layer가 설치되지 않은 환경에서는 그 검증을 수행할 수 없다.

테스트에는 오래된 핸들, 캐시 unload/clear, Scene 수명, D3D 리소스 해제, 메모리 회귀가 포함된다. CPU 대상 일부 테스트는 MSVC AddressSanitizer preset으로 실행할 수 있다. `REMIGravity`의 종료 로그에 기록되는 `live children=0`, `warnings=0`은 해당 실행에서 debug layer가 관찰한 결과이며 모든 실행의 무누수를 보증하는 수치는 아니다.

## 다음 개선 과제

Scene은 슬롯마다 `std::string`과 optional 컴포넌트를 보유한다. 큰 장면의 데이터 배치와 순회 효율은 별도 프로파일이 필요하다. 애니메이션은 프레임별 위치·노멀 배열로 메모리를 많이 사용하므로 압축 또는 GPU 스키닝이 다음 후보이다. 파일 바이트 캐시와 캐릭터 로더도 아직 통합되어 있지 않다.
