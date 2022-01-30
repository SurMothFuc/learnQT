#pragma once
#include "Camera.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include "hdrloader.h"

struct Material {
    QVector3D emissive = QVector3D(0, 0, 0);  // ��Ϊ��Դʱ�ķ�����ɫ
    QVector3D baseColor = QVector3D(1.0, 1.0, 1.0);//������ɫ
    float subsurface = 0.0;//�α���ɢ�����
    float metallic = 0.0;//�����ȣ�������������ı���
    float specular = 0.0;//���淴��ǿ�ȿ���
    float specularTint = 0.0;//���ƾ��淴�����ɫ�����ݸò������� baseColor �� vec3(1) ֮���ֵ
    float roughness = 1.0;//�ֲڶ�
    float anisotropic = 0.0;//�������Բ���
    float sheen = 0.0;//ģ��֯�ﲼ�ϱ�Ե��͸��
    float sheenTint = 0.0;//����֯��߹���ɫ�� baseColor �� vec3(1) ֮���ֵ
    float clearcoat = 0.0;//����ǿ�ȣ�ģ��ֲ��������Ĺ⻬Ϳ�㣨����ľ�ذ壩
    float clearcoatGloss = 0.0;// ����� ���ֲڶȡ�������˵����̶�
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
    float* cache;
    int hdrResolution;

	Pass_parameters();
    void readObj(std::string filepath, std::vector<Triangle>& triangles, Material material,QMatrix4x4 trans, bool smoothNormal);
    QMatrix4x4 getTransformMatrix(QVector3D rotateCtrl, QVector3D translateCtrl, QVector3D scaleCtrl);



    void updateMaterial(QVector3D emissive, QVector3D  baseColor,
        float subsurface, float  metallic, float  specular,
        float specularTint, float roughness, float anisotropic,
        float sheen, float sheenTint, float clearcoat,
        float clearcoatGloss, float IOR, float transmission);
};

int buildBVHwithSAH(std::vector<Triangle>& triangles, std::vector<BVHNode>& nodes, int l, int r, int n);
bool cmpz(const Triangle& t1, const Triangle& t2);
bool cmpx(const Triangle& t1, const Triangle& t2);
bool cmpy(const Triangle& t1, const Triangle& t2); 
float* calculateHdrCache(float* HDR, int width, int height);
