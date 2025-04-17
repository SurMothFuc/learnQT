#pragma once
#include "Camera.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include "hdrloader.h"
#include "Material.h"

// 三角形定义
struct Triangle {
    QVector3D p1, p2, p3;    // 顶点坐标
    QVector3D n1, n2, n3;    // 顶点法线
    Material material;  // 材质
};
struct Triangle_encoded {
    QVector3D p1, p2, p3;    // 顶点坐标
    QVector3D n1, n2, n3;    // 顶点法线
    QVector3D emissive;      // 自发光参数
    QVector3D baseColor;     // 颜色
    QVector3D param1;        // (subsurface, metallic, specular)
    QVector3D param2;        // (specularTint, roughness, anisotropic)
    QVector3D param3;        // (sheen, sheenTint, clearcoat)
    QVector3D param4;        // (clearcoatGloss, IOR, transmission)
};


// BVH 树节点
struct BVHNode {
    int left, right;    // 左右子树索引
    int n, index;       // 叶子节点信息               
    QVector3D AA, BB;        // 碰撞盒
};

struct BVHNode_encoded {
    QVector3D childs;        // (left, right, 保留)
    QVector3D leafInfo;      // (n, index, 保留)
    QVector3D AA, BB;
};



class Scene {
	public:

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    static Scene& getInstance() {
        static Scene instance; // 线程安全的静态局部变量
        return instance;
    }

	Scene();

    void readObj(std::string filepath, std::vector<Triangle>& triangles, Material material,QMatrix4x4 trans, bool smoothNormal);
    QMatrix4x4 getTransformMatrix(QVector3D rotateCtrl, QVector3D translateCtrl, QVector3D scaleCtrl);
    void updateMaterial(QVector3D emissive, QVector3D  baseColor,
        float subsurface, float  metallic, float  specular,
        float specularTint, float roughness, float anisotropic,
        float sheen, float sheenTint, float clearcoat,
        float clearcoatGloss, float IOR, float transmission);

public:

    Camera camera;
    // 物体表面材质定义

    std::vector<Triangle> triangles;
    std::vector<BVHNode> nodes;
    std::vector<Triangle_encoded> triangles_encoded;
    std::vector<BVHNode_encoded> nodes_encoded;
    HDRLoaderResult hdrRes;
    float* cache;
    int hdrResolution;
};

int buildBVHwithSAH(std::vector<Triangle>& triangles, std::vector<BVHNode>& nodes, int l, int r, int n);
bool cmpz(const Triangle& t1, const Triangle& t2);
bool cmpx(const Triangle& t1, const Triangle& t2);
bool cmpy(const Triangle& t1, const Triangle& t2); 
float* calculateHdrCache(float* HDR, int width, int height);
