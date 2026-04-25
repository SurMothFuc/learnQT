#pragma once
#include <QImage>
#include <QMatrix4x4>
#include <QVector2D>
#include <QVector3D>
#include <string>
#include <vector>
#include "Material.h"

struct TextureAsset {
    std::string sourcePath;
    QImage image;
    int width = 0;
    int height = 0;
};

// 三角形定义
struct Triangle {
    QVector3D p1, p2, p3;    // 顶点坐标
    QVector3D n1, n2, n3;    // 顶点法线
    QVector2D uv1, uv2, uv3;  // UV0
    Material material;  // 材质
};


class MeshLoader
{
public:
    static void readModel(std::string filepath, std::vector<Triangle>& triangles, std::vector<TextureAsset>& textures, Material material, QMatrix4x4 trans, bool smoothNormal, bool enableNormalization);
    static QMatrix4x4 getTransformMatrix(QVector3D rotateCtrl, QVector3D translateCtrl, QVector3D scaleCtrl);
};
