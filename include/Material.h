#pragma once

#include <QVector3D>



/**
 * baseColor（固有色）：表面颜色，通常由纹理贴图提供。
 * subsurface（次表面）：使用次表面近似控制漫反射形状。
 * metallic（金属度）：金属（0 = 电介质，1 =金属）。这是两种不同模型之间的线性混合。金属模型没有漫反射成分，并且还具有等于基础色的着色入射镜面反射。
 * specular（镜面反射强度）：入射镜面反射量。用于取代折射率。
 * specularTint（镜面反射颜色）：对美术控制的让步，用于对基础色（basecolor）的入射镜面反射进行颜色控制。掠射镜面反射仍然是非彩色的。
 * roughness（粗糙度）：表面粗糙度，控制漫反射和镜面反射。
 * anisotropic（各向异性强度）：各向异性程度。用于控制镜面反射高光的纵横比。（0 =各向同性，1 =最大各向异性。）
 * sheen（光泽度）：一种额外的掠射分量（grazing component），主要用于布料。
 * sheenTint（光泽颜色）：对sheen（光泽度）的颜色控制。
 * clearcoat（清漆强度）：有特殊用途的第二个镜面波瓣（specular lobe）。
 * clearcoatGloss（清漆光泽度）：控制透明涂层光泽度，0 = “缎面（satin）”外观，1 = “光泽（gloss）”外观。
 * IOR 折射率
 * transmission 透射率
 * alphaMode 表面alpha模式;
 * mediumtype 体积散射类型;
 * mediumDensity 介质密度;
 * mediumColor 介质颜色;
 * mediumAnisotropy 各向异性;
 */

enum AlphaMode {
    Opaque,
    Transparent,
};
enum MediumType
{
    None,
    Absorb,
    Scatter,
    Emissive
};

class Material {
public:

    QVector3D emissive = QVector3D(0, 0, 0);  // 作为光源时的发光颜色
    QVector3D baseColor = QVector3D(1.0, 1.0, 1.0);//表面颜色
    float subsurface = 0.0;//次表面散射参数
    float metallic = 0.0;//金属度，决定了漫反射的比例
    //float specular = 0.0;//镜面反射强度控制
    float specularTint = 0.0;//控制镜面反射的颜色，根据该参数，在 baseColor 和 vec3(1) 之间插值
    float roughness = 1.0;//粗糙度
    float anisotropic = 0.0;//各向异性参数
    float sheen = 0.0;//模拟织物布料边缘的透光
    float sheenTint = 0.0;//控制织物高光颜色在 baseColor 和 vec3(1) 之间插值
    float clearcoat = 0.0;//清漆强度，模拟粗糙物体表面的光滑涂层（比如木地板）
    float clearcoatGloss = 1.0;// 清漆的 “粗糙度”，或者说光泽程度
    float IOR = 1.5;//折射率
    float transmission = 0.0;
    int alphaMode=0;
    int mediumtype=0;
    float mediumDensity=0.0;
    QVector3D mediumColor = QVector3D(1.0, 1.0, 1.0);
    float mediumAnisotropy=0.0;

    int baseColorTex = -1;
    int normalTex = -1;
    int metallicTex = -1;
    int roughnessTex = -1;
    int emissiveTex = -1;
    int opacityTex = -1;

};
