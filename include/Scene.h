#pragma once
#include "Camera.h"
#include "hdrloader.h"
#include <Mesh.h>
#include "BVH.h"
#include "SceneDocument.h"
#include <memory>
#include <functional>

struct Triangle_encoded {
	QVector4D p1, p2, p3;   // 顶点坐标
	QVector4D n1, n2, n3;   // 顶点法线 
	QVector4D param1;       // 自发光参数 sheenTint
	QVector4D param2;       // 颜色 clearcoat
	QVector4D param3;       // mediumColor mediumAnisotropy                   
	QVector4D param4;       // (clearcoatGloss, IOR, transmission alphaMode)
	QVector4D param5;       // (mediumtype, mediumDensity,subsurface, metallic,)
	QVector4D param6;       // (specularTint, roughness, anisotropic,sheen)
	QVector4D uv12;         // (uv1.x, uv1.y, uv2.x, uv2.y)
	QVector4D uv3Tex0;      // (uv3.x, uv3.y, baseColorTex, normalTex)
	QVector4D tex1;         // (metallicTex, roughnessTex, emissiveTex, opacityTex)
	QVector4D textureParam0;// (opacity, alphaCutoff, normalScale, normalMapFlipY)
	QVector4D textureParam1;// (metallicChannel, roughnessChannel, lightSelectPdf, reserved)
	QVector4D tangent1;     // (tangent.xyz, handedness)
	QVector4D tangent2;
	QVector4D tangent3;
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
    EncodedLightSphere = 2,
    EncodedLightSunDisk = 3
};


class Scene {
public:

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    static Scene& getInstance() {
        static Scene instance; // 线程安全的静态局部变量
        return instance;
    }
    static void setStartupModelPath(const std::string& filepath);
    static void setStartupScenePath(const QString& filepath);

    explicit Scene(bool initialize = true);
    ~Scene();

    bool loadModelScene(const std::string& filepath, std::string* errorMessage = nullptr);
    bool loadScene(const QString& path, QString& error);
    bool saveScene(const QString& path, QString& error);
    bool exportScenePackage(const QString& path, QString& error) const;
    static std::unique_ptr<Scene> prepareScene(const QString& path, bool model, QString& error,
        std::function<void(const QString&)> progress = {});
    void adoptPrepared(Scene& prepared);
    SceneDocument snapshotDocument() const;
    SceneDocument document;
    const std::string& currentModelPath() const { return m_currentModelPath; }
    
    void DataEncode(int nTriangles, int nNodes);

    void updateMaterial(QVector3D emissive, QVector3D  baseColor,
        float subsurface, float metallic, float specularTint, float roughness, float anisotropic,
        float sheen, float sheenTint, float clearcoat,
        float clearcoatGloss, float IOR, float transmission);

private:
    void resetSceneData();
    void finalizeScene();
    void buildLightData();
    void addAnalyticLights(std::vector<float>& weights);
    void buildDocument(std::function<void(const QString&)> progress);

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

private:
    std::string m_currentModelPath;

};



