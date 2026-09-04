#pragma once
#include <QImage>
#include <QMatrix4x4>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <string>
#include <vector>
#include "Material.h"
struct SceneAssets;

struct TextureAsset {
    std::string sourcePath;
    QImage image;
    int width = 0;
    int height = 0;
    QVector3D averageLinearColor = QVector3D(1.0f, 1.0f, 1.0f);
    QVector2D uvOffset = QVector2D(0.0f, 0.0f);
    QVector2D uvScale = QVector2D(1.0f, 1.0f);
    float uvRotation = 0.0f;
    int wrapS = 0;
    int wrapT = 0;
    int minFilter = 9987;
    int magFilter = 9729;
};

// 三角形定义
struct Triangle {
    int sourceMaterialIndex = -1; // Authored Assimp index, never a GPU handle.
    int sceneMaterialIndex = -1;  // Stable document table index, survives BVH sorting.
    QVector3D p1, p2, p3;    // 顶点坐标
    QVector3D n1, n2, n3;    // 顶点法线
    QVector2D uv1, uv2, uv3;  // UV0
    QVector4D tangent1, tangent2, tangent3; // xyz tangent, w handedness
    Material material;  // 材质
};


class MeshLoader
{
public:
    static void readModel(std::string filepath, std::vector<Triangle>& triangles, std::vector<TextureAsset>& textures, Material material, QMatrix4x4 trans, bool smoothNormal, bool enableNormalization, SceneAssets* assets = nullptr, bool useFileMaterials = true);
    static QMatrix4x4 getTransformMatrix(QVector3D rotateCtrl, QVector3D translateCtrl, QVector3D scaleCtrl);
};
