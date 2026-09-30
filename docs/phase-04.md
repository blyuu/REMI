# Phase 4 — 메모리와 객체 수명 검증

> 2026-09-29, v0.4.0 당시 기록임. Phase 0에서 잡은 소유권 원칙을 실제 실패·반복 경로에서 검증했음.

## 왜 손봤음

엔진은 한 번 화면이 나오는 것보다 여러 번 생성·삭제해도 오래된 참조와 GPU 자원이 남지 않는 게 중요하다고 봤음. Scene의 삭제 경로와 Renderer 종료 순서를 이 단계에서 구체적으로 확인했음.

## 바꾼 것

- Scene 슬롯 안에 free-list를 넣어 `Destroy/Clear`에서 새 메모리를 할당하지 않게 했음. 자식을 먼저 지우되 재귀는 사용하지 않았음. 대신 큰 트리에서 탐색이 O(n²)인 한계는 남겨 뒀음.
- `Clear`는 슬롯 capacity를 재사용하고, `Reset`은 저장 공간을 돌려주면서 Scene 식별자를 바꿔 기존 ID를 무효화함.
- `LifetimeToken`은 Window/Renderer/Mesh/Scene 래퍼의 생존 수를 세게 했음. 이 수치를 전체 메모리 바이트나 드라이버 내부 할당으로 해석하지 않기로 했음.
- Mesh가 남아 있으면 Renderer의 `ShutdownAndValidate`가 종료를 거부하게 했음. 정상 해제 순서는 Scene → Mesh 캐시 → Renderer 내부 D3D 자원 → Window임.
- MSVC AddressSanitizer 프리셋을 추가했음. 별도 allocator/pool은 이 단계에서 만들지 않았음.

## 어떻게 확인했음

Debug/Release 각각 8개 테스트, CPU ASan 3개 테스트가 통과했음. 총 25,600개 엔티티의 생성·Clear, 오래된 ID, Reset 공간 반환과 실패 주입을 확인했음. WARP에서 8회 GPU 생성·잘못된 종료 순서·정상 종료를 반복했고, 해당 환경의 live child와 사전 경고가 0인 것을 확인했음. 로그는 로컬 `versions/phase-04/`에 보관했음.

ASan 통과와 debug layer 결과를 “모든 누수가 없다”는 증거로 쓰지는 않음. 검사한 경로와 환경에서 문제가 검출되지 않았다는 뜻임. 현재 소유권 규칙은 [MemoryManagement.md](MemoryManagement.md)에 따로 정리했음.
