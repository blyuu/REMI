cbuffer PerObject : register(b0) {
 row_major float4x4 g_MVP, g_World, g_LightMatrix;
 float3 g_LightDirection; float g_Intensity;
 float3 g_LightColor; float g_Ambient;
 float g_Lit, g_Shadows, g_Bias, g_Padding;
 float3 g_CameraPosition; float g_Padding2;
 float3 g_MaterialTint; float g_Metallic;
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
Texture2D<float4> g_Base : register(t1);
Texture2D<float4> g_EmissiveMap : register(t2);
Texture2D<float4> g_Normal : register(t3);
Texture2D<float4> g_ORM : register(t4);
SamplerState g_Sampler : register(s1);
struct VertexInput { float3 position:POSITION; float3 color:COLOR; float3 normal:NORMAL; float2 uv:TEXCOORD0; };
struct PixelInput { float4 position:SV_Position; float3 color:COLOR; float3 world:TEXCOORD0; float3 normal:TEXCOORD1; float2 uv:TEXCOORD2; };
struct GBufferOutput {
 float4 albedo:SV_Target0; // RGB linear albedo, A occlusion
 float4 normal:SV_Target1; // RGB encoded world normal, A roughness
 float4 position:SV_Target2; // RGB world position, A metallic
 float4 emissive:SV_Target3; // RGB linear emission, A occupancy
};
PixelInput VSMain(VertexInput v) {
 PixelInput p;
 p.position=mul(float4(v.position,1),g_MVP);
 p.world=mul(float4(v.position,1),g_World).xyz;
 p.normal=mul(v.normal,(float3x3)g_World);
 p.color=v.color; p.uv=v.uv;
 return p;
}
GBufferOutput PSMain(PixelInput p) {
 float4 base = g_HasBaseColorTexture>.5 ? g_Base.Sample(g_Sampler,p.uv) : float4(1,1,1,1);
 if (g_AlphaCutoff>0) clip(base.a-g_AlphaCutoff);
 float3 normal=normalize(cross(ddx(p.world),ddy(p.world)));
 if (dot(p.normal,p.normal)>.01) normal=normalize(p.normal);
 if (g_HasNormalTexture>.5 && dot(p.normal,p.normal)>.01) {
   float3 dx=ddx(p.world), dy=ddy(p.world);
   float2 tx=ddx(p.uv), ty=ddy(p.uv);
   float det=tx.x*ty.y-tx.y*ty.x;
   if (abs(det)>1e-7) {
     float3 tangent=normalize((dx*ty.y-dy*tx.y)*sign(det));
     tangent=normalize(tangent-normal*dot(normal,tangent));
     float3 bitangent=normalize(cross(normal,tangent));
     if (dot(bitangent,(dy*tx.x-dx*ty.x)*sign(det))<0) bitangent=-bitangent;
     float3 mapped=g_Normal.Sample(g_Sampler,p.uv).xyz*2-1;
     normal=normalize(tangent*mapped.x+bitangent*mapped.y+normal*mapped.z);
   }
 }
 float3 orm=g_HasOrmTexture>.5 ? g_ORM.Sample(g_Sampler,p.uv).rgb : float3(1,1,1);
 float3 emission=g_EmissiveFactor;
 if (g_HasEmissiveTexture>.5) emission*=g_EmissiveMap.Sample(g_Sampler,p.uv).rgb;
 emission+=g_Emissive*p.color*base.rgb*g_MaterialTint;
 GBufferOutput o;
 o.albedo=float4(p.color*base.rgb*g_MaterialTint,orm.r);
 o.normal=float4(normal*.5+.5,clamp(g_Roughness*orm.g,.04,1));
 o.position=float4(p.world,saturate(g_Metallic*orm.b));
 o.emissive=float4(emission,1);
 return o;
}
