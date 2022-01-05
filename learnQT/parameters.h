#pragma once
#include "Camera.h"
#include <iostream>
#include <fstream>
#include <sstream>

struct Material {
    QVector3D emissive = QVector3D(0, 0, 0);  // 作为光源时的发光颜色
    QVector3D baseColor = QVector3D(1.0, 1.0, 1.0);
    float subsurface = 0.0;
    float metallic = 0.0;
    float specular = 0.0;
    float specularTint = 0.0;
    float roughness = 0.0;
    float anisotropic = 0.0;
    float sheen = 0.0;
    float sheenTint = 0.0;
    float clearcoat = 0.0;
    float clearcoatGloss = 0.0;
    float IOR = 1.0;
    float transmission = 0.0;
};

// 三角形定义
struct Triangle {
    QVector3D p1, p2, p3;    // 顶点坐标
    QVector3D n1, n2, n3;    // 顶点法线
    Material material;  // 材质
};

class Pass_parameters {
	public:
	Camera camera;
    // 物体表面材质定义

    float offx = -1.5;
    std::vector<Triangle> triangles;

	Pass_parameters();
    void readObj(std::string filepath, std::vector<Triangle>& triangles, Material material,QMatrix4x4 trans, bool smoothNormal);
    QMatrix4x4 getTransformMatrix(QVector3D rotateCtrl, QVector3D translateCtrl, QVector3D scaleCtrl);
};