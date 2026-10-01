# glTF·재질·애니메이션 파이프라인

REMI 0.11.0은 `.gltf`/`.glb`를 실행 중에 읽는다. `GltfAsset::Load(renderer, path)`는 glTF의 삼각형 primitive마다 Mesh를 만들고 기본색 재질·이미지를 연결한다. PNG/JPEG는 stb_image로 RGBA8에 디코딩한다. GLB 버퍼에 내장된 이미지, 외부 파일, base64 data URI 이미지를 읽는다. glTF의 오른손 좌표는 REMI의 왼손 좌표로 변환하며 삼각형 winding도 뒤집는다.

```powershell
build\vs2022-x64\bin\Debug\REMIGravity.exe --character "C:\models\character.glb"
build\vs2022-x64\bin\Debug\REMIGravity.exe --character "C:\models\character.glb" --idle-clip Idle --move-clip Run
```

게임은 glTF가 가진 클립 이름을 먼저 찾는다. 기본 이름이 없다면 Idle/Stand와 Run/Walk가 들어간 클립을 찾고, 그래도 없으면 파일의 앞쪽 클립을 사용한다. 실제 선택된 이름은 시작 로그에 기록한다. `--idle-clip`·`--move-clip`을 명시하면 정확히 일치하는 클립이 없을 때 오류를 낸다. RMCH 경로도 정확한 클립 이름을 요구한다. `F5`로 주 HLSL 파일을 재로드할 수 있고 게임은 파일 변경을 1초 간격으로 확인한다. 컴파일이 실패하면 이전 파이프라인을 유지한다.

## 재질 파일

glTF의 기본색 계수, metallic, roughness, 기본색 텍스처를 읽는다. 재질을 덮어쓸 때는 모델 파일 옆의 `materials/` 폴더에 `<모델파일명>_<glTF 재질 인덱스>.remimat`를 둔다. 예를 들어 `character.glb`의 재질 인덱스 2는 `materials/character_2.remimat`다. `baseColorTexture` 경로는 `.remimat` 파일 위치를 기준으로 해석한다.

```json
{
  "version": 1,
  "shadingModel": "Standard",
  "tint": [1.0, 0.85, 0.78],
  "metallic": 0.0,
  "roughness": 0.7,
  "emissive": 0.0,
  "baseColorTexture": "../textures/face.png"
}
```

`shadingModel`은 `Standard`, `Unlit`, `Toon` 중 하나다. 옆 파일이 없으면 glTF 재질을 사용한다. 옆 파일에 기본색 이미지 경로를 적으면 glTF 기본색 이미지 대신 사용한다. `LoadMaterial`과 `SaveMaterial` API로 같은 형식을 읽고 쓸 수 있다.

## 정적 장면과 본 애니메이션

`ImportStaticGltfScene(renderer, scene, meshCache, path)`는 정적 primitive를 별도 엔티티와 MeshHandle로 등록한다. 노드 월드 변환은 임포트할 때 정점에 반영된다. 반환된 핸들은 호출자가 캐시에서 해제한다. 스킨이 있는 모델은 `GltfAsset`을 사용해 그린다.

`Animator`는 glTF의 노드 계층과 역바인드 행렬로 본 팔레트를 계산한다. STEP, LINEAR, CUBICSPLINE 위치·회전·스케일 샘플링과 클립 간 크로스페이드를 지원한다. `AnimStateMachine`은 파라미터 조건으로 클립을 전환한다. `GltfAsset::Update`는 최대 네 개의 joint/weight를 사용해 CPU에서 위치·노멀을 변형하고 동적 vertex buffer에 올린다. 기존 `.rmc` 경로는 프레임별 정점 베이크 방식으로 계속 지원한다.

## 현재 경계와 검증

- glTF는 삼각형 primitive와 첫 번째 skin(최대 512개 joint)을 대상으로 한다. 다중 skin, morph target, GPU 스키닝, 메시 노드의 특수 bind 변환은 지원하지 않는다.
- 기본색 이미지만 셰이더에 바인딩한다. normal/occlusion/metallic-roughness map, alpha blending/masking, 양면 재질, 정식 glTF PBR BRDF는 아직 적용하지 않는다. 따라서 복잡한 캐릭터는 상용 엔진과 같은 외관이 나오지 않는다.
- 리깅이나 축 방향이 다른 모델은 임포트 후 카메라·스케일·방향을 확인해야 한다. 주어진 Rover GLB는 런타임 로딩과 이동/중력 반전/WARP·하드웨어 종료 검사를 통과했지만, 원본 GLB에 기본색 이미지가 없어 별도 `.remimat`와 텍스처 지정이 필요하다.
- 테스트의 작은 glTF/GLB fixture는 외부·내장 이미지, 재질 파일 덮어쓰기, 스키닝 후 화면 픽셀 변화, 정적 Scene 임포트, D3D 경고·잔존 객체를 검사한다. Debug와 Release 각각 CTest 19개를 실행한다.

포함한 외부 라이브러리는 `third_party/cgltf`, `third_party/stb`, `third_party/json`에 있다. 세 라이브러리의 MIT 라이선스 표기는 각 헤더에 보존했다.
