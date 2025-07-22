#pragma once
#include <QVector3D>
#include "Material.h"

// 三角形定义
struct Triangle {
    QVector3D p1, p2, p3;    // 顶点坐标
    QVector3D n1, n2, n3;    // 顶点法线
    Material material;  // 材质
};


class MeshLoader
{
public:
    static void readObj(std::string filepath, std::vector<Triangle>& triangles, Material material, QMatrix4x4 trans, bool smoothNormal);
    static QMatrix4x4 getTransformMatrix(QVector3D rotateCtrl, QVector3D translateCtrl, QVector3D scaleCtrl);
};
