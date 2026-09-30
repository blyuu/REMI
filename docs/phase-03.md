# Phase 3 — Scene과 Entity/Component

## 해결하려던 문제

샘플이 Mesh를 직접 들고 그리는 방식으로는 장면의 생성·삭제와 부모 관계를 다루기 어려웠음. 그래서 렌더러와 독립적인 Scene을 만들고, 렌더러가 Scene을 읽는 방향으로 경계를 잡았음.

## 설계 선택

- `EntityId`를 index + generation + Scene 식별자로 만들었음. 슬롯을 재사용하더라도 삭제된 ID가 다시 살아나지 않고, 다른 Scene의 ID도 거부하도록 했음.
- 새 엔티티에는 Transform을 기본으로 넣었음. 이 단계의 컴포넌트는 Transform과 MeshComponent 두 종류로 제한했음. 범용 ECS를 만든 것은 아님.
- 부모를 바꿀 때는 로컬 Transform을 유지하는 `KeepLocal`로 정했음. 월드 행렬은 `local * parentWorld`로 계산함. 순환 부모 관계는 거부하고 부모 삭제 시 자식 트리도 삭제함.
- Scene은 GPU Mesh를 소유하지 않게 했음. 이때 MeshComponent는 숫자 키를 보관하고 샘플의 resolver가 실제 Mesh를 찾아줬음. 정식 `MeshHandle`은 Phase 5에서 붙였음.

## 확인했음

오래된 ID, 다른 Scene의 ID, 슬롯 성장·재사용, `Clear`, 중복 컴포넌트, 256단계 부모 계층, 순환 거부, 비정상 Transform을 테스트했음. GPU readback으로 부모 변환과 숨김 객체 처리도 확인했음. 샌드박스에서는 Q/E로 자식 큐브를 만들고 삭제할 수 있게 했음.

`Get`의 포인터와 `Name`의 string_view는 장기 소유 참조가 아님. 구조 변경 뒤 다시 조회해야 함. 월드 행렬 캐시, 직렬화, 임의 컴포넌트 등록은 이때 넣지 않았음.
