cbuffer PerFrame : register(b0) {
 row_major float4x4 g_MVP, g_World, g_LightMatrix;
 float3 g_LightDirection; float g_Intensity;
 float3 g_LightColor; float g_Ambient;
 float g_Lit, g_Shadows, g_Bias, g_Padding;
 float3 g_CameraPosition; float g_Padding2;
 float3 g_ClearColor; float g_Metallic;
 float g_Roughness, g_Emissive; float2 g_MaterialPadding;
 float g_HasBaseColorTexture; float3 g_TexturePadding;
 float3 g_EmissiveFactor; float g_HasEmissiveTexture;
 float g_HasNormalTexture; float3 g_NormalPadding;
 float3 g_SkyAmbient; float g_Exposure;
 float3 g_GroundAmbient; float g_EnvironmentPadding;
 float3 g_FogColor; float g_FogDensity;
 float4 g_PointPositions[8], g_PointColors[8];
 float g_PointCount; float3 g_PointPadding;
 float g_HasOrmTexture, g_AlphaCutoff; float2 g_PbrPadding;
};
Texture2D<float> g_Shadow:register(t0);
Texture2D<float4> g_Albedo:register(t1), g_Normal:register(t2), g_Position:register(t3), g_Emission:register(t4);
SamplerComparisonState g_ShadowSampler:register(s0);
struct VertexInput { float3 position:POSITION; float2 uv:TEXCOORD0; };
struct PixelInput { float4 position:SV_Position; float2 uv:TEXCOORD0; };
PixelInput VSMain(VertexInput v) { PixelInput p; p.position=float4(v.position.xy,0,1); p.uv=v.uv; return p; }
float3 Grade(float3 linearColor) {
 if (g_Exposure<=0) return linearColor;
 float3 x=max(linearColor*g_Exposure,0);
 return saturate((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14));
}
float3 Direct(float3 albedo,float metal,float rough,float3 normal,float3 view,float3 light,float3 radiance) {
 float ndl=saturate(dot(normal,light)), ndv=max(saturate(dot(normal,view)),.001);
 if (ndl<=0) return 0;
 float3 h=normalize(view+light);
 float ndh=saturate(dot(normal,h)), vdh=saturate(dot(view,h));
 float a=max(rough*rough,.0016), a2=a*a;
 float d=a2/max(3.14159265*pow(ndh*ndh*(a2-1)+1,2),.00001);
 float k=pow(rough+1,2)/8;
 float g=(ndv/(ndv*(1-k)+k))*(ndl/(ndl*(1-k)+k));
 float3 f0=lerp(float3(.04,.04,.04),albedo,metal);
 float3 f=f0+(1-f0)*pow(1-vdh,5);
 return ((1-f)*(1-metal)*albedo/3.14159265+d*g*f/max(4*ndv*ndl,.001))*radiance*ndl;
}
float4 PSMain(PixelInput p):SV_Target {
 int3 address=int3(int2(p.position.xy),0);
 float4 emission=g_Emission.Load(address);
 if (emission.a<.5) return float4(g_ClearColor,1);
 float4 a=g_Albedo.Load(address), n=g_Normal.Load(address), w=g_Position.Load(address);
 float3 normal=normalize(n.rgb*2-1), view=normalize(g_CameraPosition-w.xyz);
 float visibility=1;
 float4 light=mul(float4(w.xyz,1),g_LightMatrix);
 float3 projected=light.xyz/light.w;
 float2 uv=projected.xy*float2(.5,-.5)+.5;
 if (g_Shadows>.5 && all(uv>=0) && all(uv<=1) && projected.z>=0 && projected.z<=1) {
   visibility=0;
   [unroll] for (int y=-1;y<=1;++y)
    [unroll] for (int x=-1;x<=1;++x)
     visibility+=g_Shadow.SampleCmpLevelZero(g_ShadowSampler,uv+float2(x,y)/2048,projected.z-g_Bias);
   visibility/=9;
 }
 float3 color=Direct(a.rgb,w.a,n.a,normal,view,-normalize(g_LightDirection),g_LightColor*g_Intensity*visibility);
 [loop] for(int i=0;i<(int)g_PointCount;++i) {
   float3 delta=g_PointPositions[i].xyz-w.xyz;
   float dist=length(delta);
   float attenuation=pow(saturate(1-dist/g_PointPositions[i].w),2);
   color+=Direct(a.rgb,w.a,n.a,normal,view,delta/max(dist,.001),g_PointColors[i].rgb*g_PointColors[i].w*attenuation);
 }
 float sky=.5+.5*saturate(normal.y);
 float3 fill=g_Exposure>0 ? lerp(g_GroundAmbient,g_SkyAmbient,sky) : float3(g_Ambient,g_Ambient,g_Ambient);
 color+=fill*a.rgb*(1-w.a)*a.a;
 color+=fill*lerp(float3(.04,.04,.04),a.rgb,w.a)*pow(1-n.a,2)*a.a;
 color=Grade(color+emission.rgb);
 float fog=1-exp(-g_FogDensity*distance(w.xyz,g_CameraPosition));
 return float4(lerp(color,Grade(g_FogColor),fog),1);
}
