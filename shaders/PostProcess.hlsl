Texture2D<float4> g_Scene : register(t1);
SamplerState g_Sampler : register(s1);

struct VertexInput { float3 position : POSITION; float2 uv : TEXCOORD0; };
struct PixelInput { float4 position : SV_Position; float2 uv : TEXCOORD0; };

PixelInput VSMain(VertexInput input)
{
    PixelInput output;
    output.position = float4(input.position,1);
    output.uv = input.uv;
    return output;
}

float3 Bright(float3 color)
{
    float peak = max(color.r,max(color.g,color.b));
    float low = min(color.r,min(color.g,color.b));
    float chroma = saturate((peak-low)*3);
    return color*saturate((peak-.58)*2.5)*chroma;
}

float4 PSMain(PixelInput input) : SV_Target
{
    uint width, height;
    g_Scene.GetDimensions(width,height);
    float2 pixel = 1.0/float2(width,height);
    float3 scene = g_Scene.Sample(g_Sampler,input.uv).rgb;
    float3 glow = 0;
    [unroll] for (int step = 1; step <= 3; ++step) {
        float2 offset = pixel*(step*3);
        glow += Bright(g_Scene.Sample(g_Sampler,input.uv+float2(offset.x,0)).rgb);
        glow += Bright(g_Scene.Sample(g_Sampler,input.uv-float2(offset.x,0)).rgb);
        glow += Bright(g_Scene.Sample(g_Sampler,input.uv+float2(0,offset.y)).rgb);
        glow += Bright(g_Scene.Sample(g_Sampler,input.uv-float2(0,offset.y)).rgb);
    }
    return float4(saturate(scene+glow*.09),1);
}
