#version 330 core

// 光栅化交互预览：环境背景 + 直射主光 + 环境常数项 + 基础 GGX 高光。
// 明确不支持：阴影、环境反射（只取方向色作背景与环境项）、法线贴图、折射与透明混合、体积与 AO。
// 透明/玻璃材质按不透明处理，属刻意近似。
#include "include/defines.glsl"
#include "include/uniforms.glsl"
#include "include/utils.glsl"
#include "include/hdr_utils.glsl"
#include "include/material_texture_sampling.glsl"

in vec3 worldPosition;
in vec3 worldNormal;
in vec2 uv0;
flat in int materialIndex;

out vec4 fragColor;

uniform samplerBuffer materialTable;

// 材质表由材质下标索引，布局与路径追踪的 materialTable 一致（每个材质 10 个 vec4）。
vec4 FetchMaterialVector(int materialIndex, int slot)
{
    return texelFetch(materialTable, materialIndex * 10 + slot);
}

void main()
{
    vec4 param1 = FetchMaterialVector(materialIndex, 0); // emissive, sheenTint
    vec4 param2 = FetchMaterialVector(materialIndex, 1); // baseColor, clearcoat
    vec4 param5 = FetchMaterialVector(materialIndex, 4); // mediumtype, density, subsurface, metallic
    vec4 param6 = FetchMaterialVector(materialIndex, 5); // specularTint, roughness, anisotropic, sheen
    vec4 uv3Tex0 = FetchMaterialVector(materialIndex, 6);
    vec4 tex1 = FetchMaterialVector(materialIndex, 7);
    vec4 textureParam1 = FetchMaterialVector(materialIndex, 9);

    vec3 baseColor = param2.xyz;
    vec3 emissive = param1.xyz;
    float metallic = clamp(param5.w, 0.0, 1.0);
    float roughness = clamp(param6.y, 0.045, 1.0);

    int baseColorTex = int(uv3Tex0.z);
    int metallicTex = int(tex1.x);
    int roughnessTex = int(tex1.y);
    int emissiveTex = int(tex1.z);
    if (baseColorTex >= 0)
        baseColor *= SrgbToLinear(SampleMaterialTextureFiltered(baseColorTex, uv0).rgb);
    if (metallicTex >= 0)
        metallic = clamp(metallic * TextureChannel(SampleMaterialTextureFiltered(metallicTex, uv0),
                                                   int(textureParam1.x)), 0.0, 1.0);
    if (roughnessTex >= 0)
        roughness = clamp(roughness * TextureChannel(SampleMaterialTextureFiltered(roughnessTex, uv0),
                                                     int(textureParam1.y)), 0.045, 1.0);
    if (emissiveTex >= 0)
        emissive *= SrgbToLinear(SampleMaterialTextureFiltered(emissiveTex, uv0).rgb);

    vec3 normal = normalize(worldNormal);
    vec3 viewDirection = normalize(eye - worldPosition);
    // 双面着色：背面按朝向观察者的法线处理，避免薄片和未闭合网格全黑。
    if (dot(normal, viewDirection) < 0.0)
        normal = -normal;

    vec3 diffuseColor = baseColor * (1.0 - metallic);
    vec3 specularColor = mix(vec3(0.04), baseColor, metallic);
    float alpha = max(roughness * roughness, 0.002);
    float alphaSquared = alpha * alpha;

    vec3 radiance = emissive;
    vec3 directDirection = vec3(0.0, 0.0, 0.0);
    vec3 directIntensity = vec3(0.0);
    for (int i = nLights - nAnalyticLights; i < nLights; ++i)
    {
        vec4 param0 = texelFetch(lights, i * SIZE_LIGHT);
        if (int(param0.x + 0.5) != LIGHT_TYPE_SUN_DISK)
            continue;
        directDirection = normalize(-texelFetch(lights, i * SIZE_LIGHT + 1).xyz);
        directIntensity = texelFetch(lights, i * SIZE_LIGHT + 2).xyz;
        break;
    }
    if (dot(directIntensity, directIntensity) <= 0.0)
    {
        // 没有太阳时用相机方向作主光，保证任何场景都有可辨认的形状。
        directIntensity = vec3(3.0);
        directDirection = viewDirection;
    }

    float NoL = max(dot(normal, directDirection), 0.0);
    if (NoL > 0.0)
    {
        vec3 halfVector = normalize(directDirection + viewDirection);
        float NoV = max(dot(normal, viewDirection), 1.0e-4);
        float NoH = max(dot(normal, halfVector), 0.0);
        float distribution = alphaSquared /
                             max(3.14159265 * pow(NoH * NoH * (alphaSquared - 1.0) + 1.0, 2.0), 1.0e-6);
        float visibility = 0.5 / max(mix(2.0 * NoL * NoV, NoL + NoV, alpha), 1.0e-4);
        vec3 specular = distribution * visibility * specularColor;
        radiance += directIntensity * NoL * (diffuseColor / 3.14159265 + specular);
    }

    // 环境项：只取视线方向的环境色，不做反射（不做 IBL）。
    vec3 ambient = vec3(0.055, 0.06, 0.07);
#ifdef USEENVIRONMENTMAP
    ambient = max(hdrColor(-viewDirection), vec3(0.0));
#endif
    radiance += diffuseColor * ambient;
    radiance += specularColor * ambient * 0.3;

    fragColor = vec4(max(radiance, vec3(0.0)), 1.0);
}
