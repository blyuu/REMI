# Phase별 설계 로그

REMI를 처음부터 만들면서 각 단계에서 **무엇을 왜 선택했는지**, 실제로 무엇을 확인했는지 다시 읽기 쉽게 정리했음. 말투는 개발 메모처럼 `~했음`으로 맞췄음.

이 글은 당시 로컬 기록과 `versions/phase-XX/`의 테스트·빌드 로그를 바탕으로 **나중에 다듬은 회고**임. 개발 당일에 그대로 쓴 일지인 것처럼 보이게 하지는 않았음. 표의 테스트 수치는 해당 Phase 당시의 결과이고 현재 v0.11.0 전체 결과와 다름. 로컬 원시 로그·에셋·소스 스냅샷은 GitHub에 올리지 않았음. 현재 구조의 기준 문서는 [Architecture.md](Architecture.md), [RenderingPipeline.md](RenderingPipeline.md), [MemoryManagement.md](MemoryManagement.md)임.

| Phase | 그때 풀려고 한 문제 | 로그 |
| --- | --- | --- |
| 0 | 새 프로젝트 기준점·소유권·빌드 | [Phase 0](phase-00.md) |
| 1 | Win32 창·입력·고정 tick | [Phase 1](phase-01.md) |
| 2 | D3D11 최소 렌더러 | [Phase 2](phase-02.md) |
| 3 | Scene과 오래된 EntityId | [Phase 3](phase-03.md) |
| 4 | 메모리·GPU 객체 수명 검증 | [Phase 4](phase-04.md) |
| 5 | 파일·Mesh 캐시와 핸들 | [Phase 5](phase-05.md) |
| 6 | 방향광·그림자·컬링 | [Phase 6](phase-06.md) |
| 7 | 게임과 분리된 AABB 물리 | [Phase 7](phase-07.md) |
| 8 | 중력 반전 게임으로 시스템 연결 | [Phase 8](phase-08.md) |
| 9 | CPU/GPU 계측 | [Phase 9](phase-09.md) |
| 10 | 이전 버전과 비교하며 최적화 | [Phase 10](phase-10.md) |

Phase 11은 원래 계획상 포트폴리오 문서·실행 패키지 단계였음. 지금 문서 정리를 진행하고 있지만, 실행 패키지까지 포함한 Phase 11 완료 기록은 아직 만들지 않았음. Phase 10 이후의 Quinn 캐릭터, 코인 게임, RMCH v2 파이프라인은 이 Phase 로그에 억지로 소급하지 않았음.
