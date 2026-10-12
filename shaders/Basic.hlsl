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
    float3 g_EmissiveFactor; float g_HasEmissiveTexture;
    float g_HasNormalTexture; float3 g_NormalPadding;
    float3 g_SkyAmbient; float g_Exposure;
    float3 g_GroundAmbient; float g_EnvironmentPadding;
    float3 g_FogColor; float g_FogDensity;
    float4 g_PointPositions[8];
    float4 g_PointColors[8];
    float g_PointCount; float3 g_PointPadding;
    float g_HasOrmTexture; float g_AlphaCutoff; float2 g_PbrPadding;
};
Texture2D<float> g_Shadow : register(t0);
Texture2D<float4> g_BaseColorTexture : register(t1);
Texture2D<float4> g_EmissiveTexture : register(t2);
Texture2D<float4> g_NormalTexture : register(t3);
Texture2D<float4> g_OrmTexture : register(t4);
SamplerComparisonState g_ShadowSampler : register(s0);
SamplerState g_BaseColorSampler : register(s1);
struct VertexInput { float3 position : POSITION; float3 color : COLOR; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
struct PixelInput { float4 position : SV_Position; float3 color : COLOR; float3 world : TEXCOORD0; float4 light : TEXCOORD1; float3 normal : TEXCOORD2; float2 uv : TEXCOORD3; };
float3 Grade(float3 linearColor)
{
    if (g_Exposure <= 0) return linearColor;
    float3 x = max(linearColor*g_Exposure,0);
    return saturate((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14));
}
float3 PbrDirect(float3 albedo, float metallic, float roughness, float3 normal,
                 float3 view, float3 toLight, float3 radiance)
{
    float ndl = saturate(dot(normal,toLight));
    float ndv = max(saturate(dot(normal,view)),.001);
    if (ndl <= 0) return 0;
    float3 halfVector = normalize(view+toLight);
    float ndh = saturate(dot(normal,halfVector));
    float vdh = saturate(dot(view,halfVector));
    float a = max(roughness*roughness,.0016);
    float a2 = a*a;
    float d = a2 / max(3.14159265*pow(ndh*ndh*(a2-1)+1,2),.00001);
    float k = pow(roughness+1,2)/8;
    float g = (ndv/(ndv*(1-k)+k))*(ndl/(ndl*(1-k)+k));
    float3 f0 = lerp(float3(.04,.04,.04),albedo,metallic);
    float3 f = f0+(1-f0)*pow(1-vdh,5);
    float3 specular = d*g*f/max(4*ndv*ndl,.001);
    float3 diffuse = (1-f)*(1-metallic)*albedo/3.14159265;
    return (diffuse+specular)*radiance*ndl;
}
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
    float4 baseSample = g_HasBaseColorTexture > .5 ?
        g_BaseColorTexture.Sample(g_BaseColorSampler,input.uv) : float4(1,1,1,1);
    if (g_AlphaCutoff > 0) clip(baseSample.a-g_AlphaCutoff);
    baseColor *= baseSample.rgb;
    float3 emissiveColor = g_EmissiveFactor;
    if (g_HasEmissiveTexture > .5)
        emissiveColor *= g_EmissiveTexture.Sample(g_BaseColorSampler,input.uv).rgb;
    // Legacy meshes have no normals; animated characters supply smooth normals.
    float3 normal = normalize(cross(ddx(input.world),ddy(input.world)));
    bool smooth = dot(input.normal,input.normal) > .01;
    if (smooth) normal = normalize(input.normal);
    if (smooth && g_HasNormalTexture > .5) {
        float3 dpdx = ddx(input.world), dpdy = ddy(input.world);
        float2 dtdx = ddx(input.uv), dtdy = ddy(input.uv);
        float determinant = dtdx.x*dtdy.y - dtdx.y*dtdy.x;
        if (abs(determinant) > 1e-7) {
            float3 tangent = normalize((dpdx*dtdy.y - dpdy*dtdx.y)*sign(determinant));
            tangent = normalize(tangent - normal*dot(normal,tangent));
            float3 bitangent = normalize(cross(normal,tangent));
            if (dot(bitangent,(dpdy*dtdx.x - dpdx*dtdy.x)*sign(determinant)) < 0)
                bitangent = -bitangent;
            float3 mapped = g_NormalTexture.Sample(g_BaseColorSampler,input.uv).xyz*2-1;
            normal = normalize(tangent*mapped.x + bitangent*mapped.y + normal*mapped.z);
        }
    }
    if (g_Lit < .5) return float4(baseColor + emissiveColor,1);
#if SHADING_MODEL == 1 // Unlit: retains vertex color and tint without direct lighting.
    return float4(baseColor * g_MaterialTint + g_Emissive + emissiveColor, 1);
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
    return float4(baseColor * g_MaterialTint * toonLight + g_Emissive + emissiveColor, 1);
#endif
#if SHADING_MODEL == 3 // glTF-style metallic/roughness forward shading.
    float3 orm = g_HasOrmTexture > .5 ? g_OrmTexture.Sample(g_BaseColorSampler,input.uv).rgb : float3(1,1,1);
    float metallic = saturate(g_Metallic*orm.b);
    float roughness = clamp(g_Roughness*orm.g,.04,1);
    float3 albedo = baseColor*g_MaterialTint;
    float3 view = normalize(g_CameraPosition-input.world);
    float3 toSun = -normalize(g_LightDirection);
    float3 result = PbrDirect(albedo,metallic,roughness,normal,view,toSun,
                              g_LightColor*g_Intensity*visibility);
    [loop] for (int i = 0; i < (int)g_PointCount; ++i) {
        float3 delta = g_PointPositions[i].xyz-input.world;
        float distanceToLight = length(delta);
        float attenuation = pow(saturate(1-distanceToLight/g_PointPositions[i].w),2);
        result += PbrDirect(albedo,metallic,roughness,normal,view,
                            delta/max(distanceToLight,.001),
                            g_PointColors[i].rgb*g_PointColors[i].w*attenuation);
    }
    float sky = .5+.5*saturate(normal.y);
    float3 fill = g_Exposure > 0 ? lerp(g_GroundAmbient,g_SkyAmbient,sky) : float3(g_Ambient,g_Ambient,g_Ambient);
    result += fill*albedo*(1-metallic)*orm.r;
    // A restrained environment reflection approximation; this is not IBL.
    result += fill*lerp(float3(.04,.04,.04),albedo,metallic)*pow(1-roughness,2)*orm.r;
    result = Grade(result+emissiveColor+g_Emissive*albedo);
    float fogAmount = 1-exp(-g_FogDensity*distance(input.world,g_CameraPosition));
    return float4(lerp(result,Grade(g_FogColor),fogAmount),1);
#else
    if (!smooth)
        return float4(baseColor*g_MaterialTint * (g_Ambient + g_LightColor*g_Intensity*diffuse*visibility+g_Emissive) + emissiveColor,1);

    // Soft sky fill and a restrained broad highlight keep pale armor readable.
    float sky = .55 + .45*saturate(normal.y);
    float fill = .14*saturate(dot(normal,normalize(float3(.45,.55,-.65))));
    float3 view = normalize(g_CameraPosition-input.world);
    float3 halfVector = normalize(-normalize(g_LightDirection)+view);
    float glossPower = lerp(80,10,g_Roughness);
    float highlight = lerp(.09,.55,g_Metallic)*pow(saturate(dot(normal,halfVector)),glossPower)*visibility;
    float3 environment = g_Exposure > 0 ? lerp(g_GroundAmbient,g_SkyAmbient,sky) : g_Ambient*sky;
    float3 diffuseLight = environment + fill*float3(.82,.90,1) + g_LightColor*g_Intensity*diffuse*visibility;
    [loop] for (int i = 0; i < (int)g_PointCount; ++i) {
        float3 delta = g_PointPositions[i].xyz-input.world;
        float distanceToLight = length(delta);
        float reach = saturate(1-distanceToLight/(g_PointPositions[i].w*1.7));
        float lambert = saturate(dot(normal,delta/max(distanceToLight,.001)));
        diffuseLight += g_PointColors[i].rgb*g_PointColors[i].w*lambert*reach*reach;
    }
    float3 albedo = lerp(baseColor*g_MaterialTint,g_MaterialTint,g_Metallic*.7);
    float3 graded = Grade(albedo*(diffuseLight*(1-.35*g_Metallic)+g_Emissive) + highlight*g_LightColor + emissiveColor);
    float fog = 1-exp(-g_FogDensity*distance(input.world,g_CameraPosition));
    return float4(lerp(graded,Grade(g_FogColor),fog),1);
#endif
}
