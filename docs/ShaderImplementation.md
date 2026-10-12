# HLSL 셰이더 구현

REMI 0.11.0의 포워드 HLSL은 `shaders/Basic.hlsl`, 디퍼드 지오메트리/조명은 `DeferredGeometry.hlsl`·`DeferredLighting.hlsl`에 구현되어 있다. 셰이더는 D3DCompile로 `vs_5_0`·`ps_5_0`에 맞춰 컴파일한다.

`ShaderCache`는 정점 셰이더와 `ShadingModel`별 픽셀 셰이더 바이트코드를 캐시한다. 포워드는 Standard(기존 조명), Unlit(직접광 무시), Toon(확산광 단계화), PBR(GGX)을 지원한다. MeshComponent의 `MaterialProperties.shadingModel`로 선택한다. 디퍼드 장면은 지오메트리 버퍼를 거쳐 모두 PBR로 평가한다. Toon은 범용 단계형 확산광이고, Raven의 머리카락·피부 전용 셰이더는 아니다.

## 입력과 상수

Vertex 입력은 `POSITION`, `COLOR`, `NORMAL` float3과 `TEXCOORD0` float2이다. `PerObject` constant buffer에는 모델-뷰-투영 행렬, 월드 행렬, 광원 행렬, 방향광 색·강도, ambient, 카메라 위치, 재질 tint/metallic/roughness/emissive와 텍스처 유무가 들어간다. `row_major` 행렬과 `mul(rowVector, matrix)`를 함께 사용한다. 포워드의 shadow map은 `t0/s0`, 기본색·발광·노멀·ORM 텍스처는 `t1`~`t4`다. 기본색/발광은 sRGB, 노멀/ORM은 선형으로 읽는다.

`MaterialProperties`의 기본값은 tint `(1,1,1)`, metallic `0`, roughness `0.6`, emissive `0`이다. 렌더러는 metallic 0~1, roughness 0.04~1, emissive 0~2 범위를 검사한다. Standard/Toon에서는 간이 조명 조정값이고 PBR에서는 BRDF의 금속성·거칠기 인자다.

## 정점 단계

VS는 정점 위치를 카메라 clip space로 변환한다. 동시에 월드 위치와 광원 clip 위치를 PS로 넘긴다. 노멀은 월드 행렬의 3×3 부분으로 변환한다. 이 방식은 비균일 스케일에 대한 inverse-transpose normal matrix를 쓰지 않으므로 일반적인 비균일 스케일에서 정확하지 않을 수 있다.

```hlsl
output.position = mul(float4(input.position, 1), g_ModelViewProjection);
output.world = mul(float4(input.position, 1), g_World).xyz;
output.light = mul(float4(output.world, 1), g_LightMatrix);
```

## 픽셀 단계

노멀 입력이 유효하면 보간된 smooth normal을 정규화한다. 그렇지 않으면 `cross(ddx(world), ddy(world))`로 삼각형 면 노멀을 만든다. 확산광은 `saturate(dot(normal, -lightDirection))`이다. Shadow map 바깥이나 깊이 범위 바깥이면 가시도를 1로 놓고, 안쪽이면 3×3 비교 샘플의 평균을 쓴다.

노멀이 없는 기존 메시 경로는 정점 색×tint에 ambient, 방향광 확산광, emissive를 합친다. Smooth normal 경로는 여기에 위쪽을 향한 노멀에 따른 sky fill, 약한 보조광, Blinn 형태의 half-vector 하이라이트를 더한다. `roughness`는 하이라이트 지수를, `metallic`은 하이라이트 강도와 색상 보간을 조정한다. 이는 스타일을 맞추기 위한 경험적 수식이다.

```text
diffuse = max(0, dot(N, -L))
visibility = average(3×3 shadow comparisons)
highlight ≈ strength(metallic) × max(0, dot(N, H))^power(roughness)
```

## 캐릭터 색과 애니메이션

PBR 경로는 ORM의 roughness/metallic을 재질 계수와 곱하고, GGX 분포·Smith 기하 항·Schlick Fresnel로 직접광을 계산한다. 방향광 그림자와 최대 8개 점광원이 적용된다. 하늘/지면 색에서 환경 확산광과 넓은 반사를 근사하며, 이는 조사된 IBL이 아니다. 필요하면 ACES형 곡선으로 톤 매핑하고 거리 안개를 합성한다. 디퍼드에서는 같은 역할을 G-buffer 기록과 전체 화면 조명으로 나눠 계산한다. 자세한 채널·패스 순서는 [RavenRendering.md](RavenRendering.md)에 있다.

RMCH 캐릭터는 정점 색을 저장한다. Blender 베이크 도구는 재질 기본색이나 지정한 diffuse 이미지의 UV 샘플을 정점 색에 기록한다. `BakedAnimation`은 CPU에서 두 프레임의 위치·노멀을 보간한다. glTF/GLB는 UV·정점 색·기본색 텍스처를 런타임에서 읽는다. `Animator`가 glTF 관절 행렬을 계산하고 `GltfAsset`이 네 개의 가중치를 CPU에서 적용해 동적 vertex buffer에 업로드한다. HLSL 본 팔레트나 GPU 스키닝은 아직 없다.

## 정확성·품질 한계

- sRGB render-target view와 ACES형 톤 매핑은 사용하지만 HDR 디스플레이 출력과 정확한 색 보정 파이프라인은 없다.
- PBR 경로는 GGX·Fresnel·normal/ORM map을 사용한다. 조사된 IBL, 피부 SSS, 이방성 머리카락 반사, alpha-to-coverage는 없다.
- 방향광 그림자는 단일 2048² 맵이다. bias와 고정 PCF 커널 때문에 acne, peter-panning, 먼 거리 해상도 저하가 나타날 수 있다.
- Standard/Toon의 `metallic`/`roughness` UI 값이나 은색 프리셋은 PBR 정확성의 증거가 아니다. Raven PBR 경로는 별도 픽셀 검증이 있다.

품질을 더 올리려면 같은 카메라·광원에서 기본색/normal/shadow/roughness 진단 화면을 고정하고, tangent 공간과 색 공간을 검증한 뒤 IBL·피부·머리카락 조명을 확장해야 한다. 렌더링 흐름은 [RenderingPipeline.md](RenderingPipeline.md)에 정리했다.
