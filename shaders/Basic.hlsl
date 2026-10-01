#ifndef SHADING_MODEL
#define SHADING_MODEL 0 // Standard
#endif

cbuffer PerObject : register(b0)
{
    row_major float4x4 g_ModelViewProjection;
    row_major float4x4 g_World;
    row_major float4x4 g_LightMatrix;
    float3 g_LightDirection; float g_Intensity;
    float3 g_LightColor; float g_Ambient;
    float g_Lit; float g_Shadows; float g_Bias; float g_Padding;
    float3 g_CameraPosition; float g_Padding2;
    float3 g_MaterialTint; float g_Metallic;
    float g_Roughness; float g_Emissive; float2 g_MaterialPadding;
    float g_HasBaseColorTexture; float3 g_TexturePadding;
};
Texture2D<float> g_Shadow : register(t0);
Texture2D<float4> g_BaseColorTexture : register(t1);
SamplerComparisonState g_ShadowSampler : register(s0);
SamplerState g_BaseColorSampler : register(s1);
struct VertexInput { float3 position : POSITION; float3 color : COLOR; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
struct PixelInput { float4 position : SV_Position; float3 color : COLOR; float3 world : TEXCOORD0; float4 light : TEXCOORD1; float3 normal : TEXCOORD2; float2 uv : TEXCOORD3; };
PixelInput VSMain(VertexInput input)
{
    PixelInput output;
    output.position = mul(float4(input.position, 1), g_ModelViewProjection);
    output.color = input.color;
    float4 world = mul(float4(input.position,1),g_World);
    output.world = world.xyz;
    output.light = mul(world,g_LightMatrix);
    output.normal = mul(input.normal,(float3x3)g_World);
    output.uv = input.uv;
    return output;
}
float4 PSMain(PixelInput input) : SV_Target
{
    float3 baseColor = input.color;
    if (g_HasBaseColorTexture > .5) baseColor *= g_BaseColorTexture.Sample(g_BaseColorSampler,input.uv).rgb;
    // Legacy meshes have no normals; animated characters supply smooth normals.
    float3 normal = normalize(cross(ddx(input.world),ddy(input.world)));
    bool smooth = dot(input.normal,input.normal) > .01;
    if (smooth) normal = normalize(input.normal);
    if (g_Lit < .5) return float4(baseColor,1);
#if SHADING_MODEL == 1 // Unlit: retains vertex color and tint without direct lighting.
    return float4(baseColor * g_MaterialTint + g_Emissive, 1);
#endif
    float diffuse = saturate(dot(normal,-normalize(g_LightDirection)));
    float visibility = 1;
    float3 light = input.light.xyz / input.light.w;
    float2 uv = light.xy * float2(.5,-.5) + .5;
    if (g_Shadows > .5 && all(uv >= 0) && all(uv <= 1) && light.z >= 0 && light.z <= 1) {
        visibility = 0;
        [unroll] for (int y = -1; y <= 1; ++y)
            [unroll] for (int x = -1; x <= 1; ++x)
                visibility += g_Shadow.SampleCmpLevelZero(g_ShadowSampler,uv+float2(x,y)/2048.0,light.z-g_Bias);
        visibility /= 9;
    }
#if SHADING_MODEL == 2 // Toon: a simple stepped diffuse term, not a character-specific shader.
    float stepped = diffuse >= .65 ? 1 : (diffuse >= .25 ? .55 : .16);
    float3 toonLight = g_Ambient + g_LightColor * g_Intensity * stepped * visibility;
    return float4(baseColor * g_MaterialTint * toonLight + g_Emissive, 1);
#endif
    if (!smooth)
        return float4(baseColor*g_MaterialTint * (g_Ambient + g_LightColor*g_Intensity*diffuse*visibility+g_Emissive),1);

    // Soft sky fill and a restrained broad highlight keep pale armor readable.
    float sky = .55 + .45*saturate(normal.y);
    float fill = .14*saturate(dot(normal,normalize(float3(.45,.55,-.65))));
    float3 view = normalize(g_CameraPosition-input.world);
    float3 halfVector = normalize(-normalize(g_LightDirection)+view);
    float glossPower = lerp(80,10,g_Roughness);
    float highlight = lerp(.09,.55,g_Metallic)*pow(saturate(dot(normal,halfVector)),glossPower)*visibility;
    float3 diffuseLight = g_Ambient*sky + fill*float3(.82,.90,1) + g_LightColor*g_Intensity*diffuse*visibility;
    float3 albedo = lerp(baseColor*g_MaterialTint,g_MaterialTint,g_Metallic*.7);
    return float4(albedo*(diffuseLight*(1-.35*g_Metallic)+g_Emissive) + highlight*g_LightColor,1);
}
