cbuffer PerObject : register(b0)
{
    row_major float4x4 g_ModelViewProjection;
    row_major float4x4 g_World;
    row_major float4x4 g_LightMatrix;
    float3 g_LightDirection; float g_Intensity;
    float3 g_LightColor; float g_Ambient;
    float g_Lit; float g_Shadows; float g_Bias; float g_Padding;
};
Texture2D<float> g_Shadow : register(t0);
SamplerComparisonState g_ShadowSampler : register(s0);
struct VertexInput { float3 position : POSITION; float3 color : COLOR; };
struct PixelInput { float4 position : SV_Position; float3 color : COLOR; float3 world : TEXCOORD0; float4 light : TEXCOORD1; };
PixelInput VSMain(VertexInput input)
{
    PixelInput output;
    output.position = mul(float4(input.position, 1), g_ModelViewProjection);
    output.color = input.color;
    float4 world = mul(float4(input.position,1),g_World);
    output.world = world.xyz;
    output.light = mul(world,g_LightMatrix);
    return output;
}
float4 PSMain(PixelInput input) : SV_Target
{
    // Geometric face normal in world space: flat shading, including non-uniform scale.
    float3 normal = normalize(cross(ddx(input.world),ddy(input.world)));
    if (g_Lit < .5) return float4(input.color,1);
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
    return float4(input.color * (g_Ambient + g_LightColor*g_Intensity*diffuse*visibility),1);
}
