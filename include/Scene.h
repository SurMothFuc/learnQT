#pragma once
#include "Camera.h"
#include "hdrloader.h"
#include <Mesh.h>
#include "BVH.h"

struct Triangle_encoded {
	QVector4D p1, p2, p3;   // 顶点坐标
	QVector4D n1, n2, n3;   // 顶点法线 
	QVector4D param1;       // 自发光参数 sheenTint
	QVector4D param2;       // 颜色 clearcoat
	QVector4D param3;       // mediumColor mediumAnisotropy                   
	QVector4D param4;       // (clearcoatGloss, IOR, transmission alphaMode)
	QVector4D param5;       // (mediumtype, mediumDensity,subsurface, metallic,)
	QVector4D param6;       // (specularTint, roughness, anisotropic,sheen)
};

struct BVHNode_encoded {
    QVector3D childs;        // (left, right, 保留)
    QVector3D leafInfo;      // (n, index, 保留)
    QVector3D AA, BB;
};

struct Light_encoded {
    QVector4D param0;       // (type, triangleIndex, selectPdf, radius)
    QVector4D param1;       // position or direction
    QVector4D param2;       // color / radiance
    QVector4D param3;       // (cdf, range, reserved, reserved)
};

enum EncodedLightType {
    EncodedLightTriangle = 1,
    EncodedLightPoint = 2,
    EncodedLightDirectional = 3,
    EncodedLightSphere = 4
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
    
    void DataEncode(int nTriangles, int nNodes);

    void updateMaterial(QVector3D emissive, QVector3D  baseColor,
        float subsurface, float metallic, float specularTint, float roughness, float anisotropic,
        float sheen, float sheenTint, float clearcoat,
        float clearcoatGloss, float IOR, float transmission);

private:
    void resetSceneData();
    void buildLegacyGlassScene();
    void buildImportanceSamplingBenchmarkScene();
    void finalizeScene();
    void buildLightData();
    void addAnalyticLights(std::vector<float>& weights);

public:

    Camera camera;

    //data store
    std::vector<Triangle> triangles;
    std::vector<TextureAsset> textures;
    std::vector<BVHNode> nodes;
    std::vector<Triangle_encoded> triangles_encoded;
    std::vector<BVHNode_encoded> nodes_encoded;
    std::vector<Light_encoded> lights_encoded;
    float lightPowerSum = 0.0f;
    HDRLoaderResult hdrRes = {};
    float* cache = nullptr;
    int hdrResolution = 0;

};



