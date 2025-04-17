#pragma once
#include "Camera.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include "hdrloader.h"

struct Material {
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
