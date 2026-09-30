# HLSL 셰이더 구현

REMI 0.11.0의 현재 HLSL은 `shaders/Basic.hlsl` 한 파일에 `VSMain`과 `PSMain`으로 구현되어 있다. 이 문서는 화면 결과가 **어떤 계산에서 나오는지**와 아직 구현하지 않은 것을 구분한다. 셰이더는 D3DCompile로 `vs_5_0`·`ps_5_0`에 맞춰 컴파일한다.

## 입력과 상수

Vertex 입력은 `POSITION`, `COLOR`, `NORMAL` float3이다. `PerObject` constant buffer에는 모델-뷰-투영 행렬, 월드 행렬, 광원 행렬, 방향광 색·강도, ambient, 카메라 위치, 재질 tint/metallic/roughness/emissive 값이 들어간다. `row_major` 행렬과 `mul(rowVector, matrix)`를 함께 사용한다. Shadow map은 `t0`, 비교 sampler는 `s0`에 바인딩한다.

`MaterialProperties`의 기본값은 tint `(1,1,1)`, metallic `0`, roughness `0.6`, emissive `0`이다. 렌더러는 metallic 0~1, roughness 0.04~1, emissive 0~2 범위를 검사한다. 이 값들은 **현재 간이 조명 모델의 조정 변수**이며 금속/거칠기 PBR 재질을 의미하지 않는다.

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

RMCH 캐릭터는 정점 색을 저장한다. Blender 베이크 도구는 재질 기본색이나 지정한 diffuse 이미지의 UV 샘플을 정점 색에 기록한다. 런타임 셰이더가 이미지 텍스처를 샘플링하는 것은 아니다. `BakedAnimation`은 CPU에서 두 프레임의 위치·노멀을 보간하고 동적 vertex buffer에 업로드한다. 본 행렬이나 skinning weight는 HLSL에 전달하지 않는다.

## 정확성·품질 한계

- sRGB render-target view는 사용하지만 전체 색 관리 파이프라인과 HDR tone mapping은 없다.
- 에너지 보존 BRDF, GGX, Fresnel, IBL, normal map, 실시간 텍스처 샘플링은 없다.
- 방향광 그림자는 단일 2048² 맵이다. bias와 고정 PCF 커널 때문에 acne, peter-panning, 먼 거리 해상도 저하가 나타날 수 있다.
- 현재 `metallic`/`roughness` UI 값이나 은색 프리셋은 PBR 정확성의 증거가 아니다.

품질을 올릴 때는 임의로 수식을 늘리기보다 기준 장면과 측정 조건을 먼저 고정해야 한다. 같은 카메라·광원에서 diffuse/normal/shadow/roughness 진단 화면을 만들고, 색 공간과 normal transform을 검증한 뒤 텍스처·BRDF·IBL 순서로 확장하는 것이 다음 단계다. 렌더링 흐름은 [RenderingPipeline.md](RenderingPipeline.md)에 정리했다.
