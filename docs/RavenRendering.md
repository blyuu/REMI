# Raven 캐릭터 렌더링과 재질 파이프라인

## 실행과 입력

**실행할 때는 로컬 `demoexecutionfiles/REMIRaven.exe`를 더블클릭한다.** 이 EXE는 Soul City와 Raven 모델을 기본값으로 선택하므로 실행 인자가 필요 없다. `Play-Raven.cmd`는 처음 에셋을 변환하거나 다시 빌드할 때만 쓰는 개발용 도구다. 사용자가 로컬에 둔 `Models/Stellar Blade - Raven.zip`을 Blender 5.1과 ffmpeg로 변환한다. 원본 ZIP과 변환 GLB는 저장소 및 공개 배포 패키지에 포함하지 않는다. `demoexecutionfiles`는 Git에서 제외된 로컬 실행 묶음이며, 원본 모델의 이용·배포 권한은 별도로 확인해야 한다.

```powershell
demoexecutionfiles\REMIRaven.exe
# 같은 장면의 포워드 경로를 명령행에서 비교할 때
demoexecutionfiles\REMIRaven.exe --forward
# 재현 가능한 WARP 캡처
build\vs2022-x64\bin\Release\REMIVillage.exe --soul-city --character build\local-assets\raven\raven.glb --camera-yaw 4.3 --camera-pitch 0.1 --camera-distance 3.2 --smoke --warp --capture build\raven-preview.bmp
```

일반 FBX 하나는 `tools/assets/import-fbx.ps1 -Source model.fbx -Output build/model.glb`로 GLB로 변환한다. 재질 이미지가 FBX에 연결되지 않았다면 `-MaterialMap mapping.json`을 준다. JSON의 재질 이름별 `base`, `normal`, `orm`, `emissive` 이미지 경로는 JSON 파일 기준이다. `-StaticRestPose`는 본 변형을 고정하고 스킨을 제거한다. Raven은 몸·얼굴·머리카락에 서로 다른 리그 3개가 있고 클립이 없으므로 `tools/assets/import-raven.ps1`이 세 FBX를 함께 가져와 정적 포즈로 베이크한다. 현재 Raven은 이동하는 플레이어에 부착된 **정적 시각 모델**이며, 걷기 애니메이션이 구현된 상태는 아니다.

## 재질 데이터

Raven 변환기는 원본 TGA의 기본색(A), 노멀(N), ORM, 발광(E)을 2048px 이하 PNG로 변환해 재질 슬롯에 연결한다. glTF metallic-roughness의 `R=occlusion, G=roughness, B=metallic`을 픽셀마다 샘플한다. 기본색은 sRGB, 노멀과 ORM은 선형 텍스처로 GPU에 올린다. 머리카락 방향 맵은 색상으로 사용하지 않고, 별도 alpha 마스크를 어두운 기본색과 합쳐 cutout 카드로 만든다. 머리카락 셰이더는 아직 이방성 반사나 alpha-to-coverage가 없으므로 가까운 화면에서 가닥이 거칠게 보일 수 있다.

눈은 제공된 홍채와 흰자위 텍스처를 같은 UV에 합치고, 속눈썹·눈썹 마스크를 cutout 재질로 사용한다. 얼굴 FBX의 `upLid_up`·`loLid_down` 표정 키를 정적 모델로 베이크해 눈꺼풀을 연다. 원본의 Tearline·Eyeshadow 오버레이는 별도 Unreal 투명 재질 그래프가 없어 불투명하게 그리면 눈동자를 가리므로 GLB 변환 시 해당 두 primitive만 제외한다. 눈의 원본 투명·굴절 효과까지 재현한 것은 아니다.

`MaterialGraph`는 현재 **CPU 수치 노드 그래프**다. 상수·기존 재질 입력·덧셈·곱셈·1에서 빼기를 연결하여 tint/metallic/roughness/emissive 계수를 계산한다. Raven은 가져온 roughness 계수에 0.9를 곱한 결과를 각 primitive에 적용한다. 그래프 노드는 세대 번호가 있는 핸들로 참조한다. 출력에서 닿지 않는 노드는 `Collect()`가 mark-sweep으로 회수하며, 순환 참조도 회수한다. GPU 텍스처와 Scene 엔티티는 이 GC의 대상이 아니며 기존 RAII·핸들 소유권을 따른다. 현재 그래프는 셰이더 코드를 생성하거나 UI에서 편집하는 범용 노드 에디터가 아니다.

## 포워드·디퍼드 패스

```text
공통:  Win32/Scene 업데이트 → CPU 스키닝(해당 모델) → 2048² 방향광 shadow depth
포워드: Scene 메시 → Basic.hlsl PBR/Standard/Unlit/Toon → 선택적 bloom → 설정 UI → Present
디퍼드: Scene 메시 → G-buffer MRT 4개+depth → 전체 화면 PBR 조명 → 선택적 bloom → 포워드 설정 UI → Present
```

Raven은 기본적으로 디퍼드 경로를 선택하고 `--forward`로 같은 장면을 포워드 경로에서 볼 수 있다. 디퍼드 지오메트리 패스는 선형 기본색+AO, 월드 노멀+roughness, 월드 위치+metallic, emission+점유 값을 4개의 RGBA16F 타깃에 저장한다. 화면 전체 조명 패스는 Cook–Torrance GGX, Schlick Fresnel, Smith 기하 항으로 방향광·최대 8개 점광원을 평가한다. 2048² 그림자는 3×3 PCF로 샘플하고, 환경광 근사·ACES형 톤 매핑·거리 안개를 적용한다. Soul City의 Raven 프리셋은 카메라 근처 키·필·림 조명을 추가해 검은 수트의 하이라이트를 보이게 한다. bloom은 장면 색을 합성한 후 적용되며 설정 UI에는 적용되지 않는다.

디퍼드 경로는 `RendererConfig::deferred`와 `ResolveDeferred(light)`로 명시한다. `SceneRenderSession::DrawColor`가 일반 장면과 추가 캐릭터를 G-buffer에 그린 뒤 한 번 조명을 해결한다. 설정 UI는 그 후 기존 포워드 파이프라인으로 그린다. 셰이더 수정 시 `ReloadShaders()`가 Basic·DeferredGeometry·DeferredLighting을 다시 컴파일하며, 실패하면 이전 파이프라인을 유지한다.

## 검증과 한계

`Rendering.MaterialGraphGC`는 노드 평가, 순환 노드 회수, 오래된 핸들 거부와 순환 평가 오류를 검사한다. `Phase6.Lighting`은 WARP에서 디퍼드 PBR 픽셀과 ORM 텍스처 연결의 출력 차이, D3D 경고와 종료 시 객체 누수를 검사한다. `REMIVillage --smoke --warp --character build/local-assets/raven/raven.glb`는 실제 모델의 로딩·드로·종료를 검사한다.

현재 환경 반사는 조사된 cubemap/IBL이 아닌 하늘·지면 색의 근사다. 피부의 subsurface scattering, 머리카락의 이방성·투명도 정렬, 스크린 공간 반사, TAA, Unreal의 Lumen/후처리와 원본 게임의 전용 재질 그래프는 구현하지 않았다. 따라서 원본 게임과 같은 렌더를 보장하지 않으며, 이 데모는 **제공된 메시·텍스처를 REMI의 PBR 및 조명 경로로 재현한 결과**로 설명해야 한다. Alpha 마스크 머리카락은 현재 그림자 패스에서 제외한다. 디퍼드 G-buffer는 네 장의 16비트 부동소수점 타깃이므로 포워드보다 메모리·대역폭 비용이 높고, 어느 경로가 더 빠른지는 실제 장면과 GPU에서 측정해야 한다.
