#pragma once
#include "Camera.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include "hdrloader.h"

struct Material {
    QVector3D emissive = QVector3D(0, 0, 0);  // ��Ϊ��Դʱ�ķ�����ɫ
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

// �����ζ���
struct Triangle {
    QVector3D p1, p2, p3;    // ��������
    QVector3D n1, n2, n3;    // ���㷨��
    Material material;  // ����
};
struct Triangle_encoded {
    QVector3D p1, p2, p3;    // ��������
    QVector3D n1, n2, n3;    // ���㷨��
    QVector3D emissive;      // �Է������
    QVector3D baseColor;     // ��ɫ
    QVector3D param1;        // (subsurface, metallic, specular)
    QVector3D param2;        // (specularTint, roughness, anisotropic)
    QVector3D param3;        // (sheen, sheenTint, clearcoat)
    QVector3D param4;        // (clearcoatGloss, IOR, transmission)
};


// BVH ���ڵ�
struct BVHNode {
    int left, right;    // ������������
    int n, index;       // Ҷ�ӽڵ���Ϣ               
    QVector3D AA, BB;        // ��ײ��
};

struct BVHNode_encoded {
    QVector3D childs;        // (left, right, ����)
    QVector3D leafInfo;      // (n, index, ����)
    QVector3D AA, BB;
};



class Pass_parameters {
	public:
	Camera camera;
    // ���������ʶ���

    float offx = -1.5;
    std::vector<Triangle> triangles;
    std::vector<BVHNode> nodes;
    std::vector<Triangle_encoded> triangles_encoded;
    std::vector<BVHNode_encoded> nodes_encoded;
    HDRLoaderResult hdrRes;

	Pass_parameters();
    void readObj(std::string filepath, std::vector<Triangle>& triangles, Material material,QMatrix4x4 trans, bool smoothNormal);
    QMatrix4x4 getTransformMatrix(QVector3D rotateCtrl, QVector3D translateCtrl, QVector3D scaleCtrl);
};

int buildBVHwithSAH(std::vector<Triangle>& triangles, std::vector<BVHNode>& nodes, int l, int r, int n);
bool cmpz(const Triangle& t1, const Triangle& t2);
bool cmpx(const Triangle& t1, const Triangle& t2);
bool cmpy(const Triangle& t1, const Triangle& t2);