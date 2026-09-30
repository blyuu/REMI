# Phase 5 — Resource Manager와 핸들

## 왜 분리했음

Scene이 GPU Mesh를 직접 소유하면 객체를 복제할 때 리소스도 중복될 수 있음. 파일 읽기와 GPU Mesh 공유를 별도 캐시로 분리해서, 장면에는 소유하지 않는 핸들만 남기기로 했음.

## 선택한 방식

- `ResourceManager`는 파일 원본 바이트를 소유하고, `ResourceCache<T>`는 key별 `unique_ptr<T>`를 소유하게 했음. 같은 key를 다시 요청하면 로더를 반복하지 않고 기존 핸들을 돌려줌.
- 핸들은 `(id, owner)`로 구성했음. 다른 캐시의 핸들을 거부하고 ID를 재사용하지 않으므로, unload/clear 후 옛 핸들이 새 리소스를 가리키지 않음.
- 캐시는 메인 스레드 전용으로 두고 로더의 재진입을 거부했음. 로더 실패·null 반환·등록 실패 시 반쯤 생성된 객체가 남지 않게 했음.
- 파일 경로는 정규화한 UTF-8 key를 사용했음. 기본 파일 크기 상한은 64 MiB이며, root는 보안 샌드박스가 아니라 상대 경로 기준임.

## 실제 연결과 검증

셰이더 바이트를 ResourceManager로 읽고 Renderer에서 VS/PS로 컴파일하게 했음. 큐브·바닥은 `ResourceCache<Mesh>`로 공유했고, Scene의 숫자 Mesh 키를 `MeshHandle`로 바꿨음. Q로 엔티티를 늘려도 같은 큐브 GPU 버퍼를 쓰는 구조임.

Debug/Release 각각 9개 테스트와 CPU ASan 4개 테스트가 통과했음. 중복 로드, 다른 캐시 핸들, 실패 후 재시도, 1,000회 load/unload, 경로 별칭과 파일 크기 제한을 확인했음. 당시 로그는 로컬 `versions/phase-05/`에 남겼음.

이때 만든 것은 리소스 관리의 기반임. 텍스처·glTF 로더, 비동기 로딩, 파일 변경 감지, hot reload, 캐시 메모리 예산은 아직 없었음. 지금도 캐릭터 RMCH 로더는 이 파일 캐시와 완전히 통합되지 않았음.
