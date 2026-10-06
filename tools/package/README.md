# REMI 실행 패키지

Windows 10/11 x64 배포용 ZIP을 `build-release.ps1`로 만듭니다. Visual Studio 2022 C++ 데스크톱 도구, Windows SDK, CMake 3.25 이상, PowerShell이 필요한 것은 **패키지를 만드는 PC**뿐입니다. 실행 ZIP은 해당 도구 없이 사용할 수 있습니다.

```powershell
powershell -ExecutionPolicy Bypass -File tools\package\build-release.ps1
```

출력은 `build\distributions\<생성 시각>\` 아래에 저장됩니다. 다른 위치를 쓰려면 `-OutputDirectory <경로>`를 지정하세요. 이미 존재하는 출력 폴더에는 덮어쓰지 않습니다.

| 산출물 | 용도 |
| --- | --- |
| `REMI-0.11.0-Windows-x64.zip` | 실행 파일, 셰이더, 폰트, 샘플, 실행기, 안내 및 라이선스 |
| `REMI-0.11.0-source.zip` | 실행 패키지에 사용한 소스 스냅샷과 파일별 SHA-256 목록 |
| `SHA256SUMS.csv` | 두 ZIP의 SHA-256과 크기 |
| `verification/` | 빌드·CTest·압축 해제 후 실행 로그와 검증 결과 |

실행 ZIP을 풀어 `START-HERE.txt`를 읽고 `Play-Gravity.cmd`, `Play-Relay.cmd`, `Open-Inspector.cmd` 중 하나를 실행합니다. `Run-Smoke-Tests.cmd`는 WARP를 사용해 기본 자동 실행을 확인합니다. 각 실행기는 `logs/`에 출력을 기록합니다. Inspector 샘플은 패키지 안의 `samples/static_triangle.glb`입니다.

패키지는 명시한 파일만 설치합니다. 저장소에만 있는 Quinn 캐릭터 내보내기(`.rmc`)와 개발용 PDB·중간 파일은 포함하지 않습니다. Gravity는 기본 박스 캐릭터로 실행됩니다. 외부 폰트와 라이브러리 고지는 `THIRD-PARTY-NOTICES.txt`와 `assets/fonts/LICENSE.txt`에 있습니다.

스크립트는 현재 Git 작업 트리의 추적 파일과 무시되지 않은 새 파일을 소스 스냅샷에 복사합니다. 따라서 미커밋 변경도 포함하며, 배포 기준 커밋과 파일별 해시는 소스 ZIP의 `SOURCE-MANIFEST.json`에 기록합니다. 빌드는 이 독립 스냅샷에서 이루어집니다. 소스 ZIP을 풀어 아래처럼 재현할 수 있습니다.

```powershell
cd REMI-0.11.0-source
cmake --preset portable
cmake --build --preset portable
ctest --preset portable
```

검증은 CTest 다음 실행 ZIP을 저장소 밖의 공백·한글이 포함된 경로로 풀어 파일별 해시를 확인합니다. 작업 디렉터리와 PATH를 바꾼 상태에서 Gravity·Relay·Inspector의 WARP와 하드웨어 smoke 실행을 검사합니다. 동적 MSVC 런타임 DLL을 참조하지 않는지도 검사합니다. 결과는 `verification/VERIFICATION.json`에 기록합니다.
