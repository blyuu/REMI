# Raven 실제 GPU 렌더링 측정 — 2026-10-11

같은 Soul City + Raven 장면을 **하드웨어 D3D11**에서 측정했다. 이 결과는 64×64 WARP 진단 장면의 수치와 별개다. 각 조건은 Release x64 `REMIRaven.exe`를 새로 실행했으며, 1920×1080, VSync OFF, D3D11 debug layer ON, 카메라·경로 고정, 120프레임 예열 후 300프레임을 기록했다. 실행 순서는 Deferred ON, Deferred OFF, Forward ON, Forward OFF였다. 한 번씩 실행한 결과이므로 반복 실행 간 변동성은 아직 측정하지 않았다.

측정 장비: NVIDIA GeForce RTX 4070 Laptop GPU (드라이버 `32.0.15.9579`), Intel Core i9-14900HX, RAM 32 GiB, Windows 11 Pro `10.0.26200`. 측정 당시 Git HEAD는 `cddbb8aa91a4aac367e293a3029141e768a72763`이며 실행 파일에는 작업 트리의 커밋되지 않은 변경 사항도 포함된다. 실행 파일 SHA-256은 `08905BA290DC96B89A49403DBE62C359335C966EB31D08DA1C584F1BF76A592A`다.

| 경로 / 컬링 | CPU Update 평균 (ms) | CPU Render Submission 중앙값 (ms) | GPU 구간 중앙값 (ms) | 유효 GPU 샘플 | Draw Calls 중앙값 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Forward / OFF | 0.000804 | 5.904850 | 5.918720 | 299/300 | 1,484 |
| Forward / ON | 0.000307 | 1.777200 | 2.180096 | 297/300 | 513 |
| Deferred / OFF | 0.000609 | 4.909250 | 4.974592 | 299/300 | 1,485 |
| Deferred / ON | 0.000305 | 1.753950 | 2.108416 | 297/300 | 514 |

CPU Update는 고정 tick이 실행되지 않은 렌더 프레임에서 0ms다. 그래서 이 열만 **300프레임 평균**을 표시했다. CPU Render Submission과 Draw Calls는 300프레임 중앙값이고, GPU 중앙값은 `gpu_valid=1`이며 `gpu_total_ms`가 비어 있지 않은 프레임만 사용했다. GPU 시간은 그림자 패스 시작부터 UI 제출 종료까지의 timestamp 구간으로, `Present` 대기나 화면 표시 시간은 아니다. CPU와 GPU 시간은 겹칠 수 있으므로 합산하지 않는다.

원시 CSV 네 파일은 모두 300행이며 유효 GPU 샘플의 시간은 양수이고 `gpu_sample_id` 중복이 없다.

동일한 카메라에서 컬링 ON은 Forward GPU 중앙값을 5.918720→2.180096ms, Deferred GPU 중앙값을 4.974592→2.108416ms로 낮췄다. 전체 Draw Calls 중앙값도 각각 1,484→513, 1,485→514로 감소했다. 컬링은 정적 Scene 메시를 대상으로 하며 Raven 캐릭터 추가 드로에는 적용되지 않는다. 이 수치는 해당 장면·장비·설정에 한정된다.

감소율은 Forward GPU 구간 **63.2%**, Deferred GPU 구간 **57.6%**, Draw Calls 양쪽 약 **65.4%**다. CPU Render Submission 중앙값도 Forward **69.9%**, Deferred **64.3%** 감소했다. 이 GPU 구간 수치를 전체 프레임 시간이나 FPS 개선율로 해석하지 않는다.

원시 CSV: [Forward OFF](raven-forward-culling-off-2026-10-11.csv) · [Forward ON](raven-forward-culling-on-2026-10-11.csv) · [Deferred OFF](raven-deferred-culling-off-2026-10-11.csv) · [Deferred ON](raven-deferred-culling-on-2026-10-11.csv). 재현 방법과 각 열의 범위는 [PortfolioBenchmarks](../PortfolioBenchmarks.md)에 있다.
