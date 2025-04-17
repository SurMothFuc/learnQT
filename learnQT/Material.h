#pragma once

#include <QVector3D>

class Material {
public:

    QVector3D emissive = QVector3D(0, 0, 0);  // 作为光源时的发光颜色
    QVector3D baseColor = QVector3D(1.0, 1.0, 1.0);//表面颜色
    float subsurface = 0.0;//次表面散射参数
    float metallic = 0.0;//金属度，决定了漫反射的比例
    float specular = 0.0;//镜面反射强度控制
    float specularTint = 0.0;//控制镜面反射的颜色，根据该参数，在 baseColor 和 vec3(1) 之间插值
    float roughness = 1.0;//粗糙度
    float anisotropic = 0.0;//各向异性参数
    float sheen = 0.0;//模拟织物布料边缘的透光
    float sheenTint = 0.0;//控制织物高光颜色在 baseColor 和 vec3(1) 之间插值
    float clearcoat = 0.0;//清漆强度，模拟粗糙物体表面的光滑涂层（比如木地板）
    float clearcoatGloss = 0.0;// 清漆的 “粗糙度”，或者说光泽程度
    float IOR = 1.0;
    float transmission = 0.0;
};