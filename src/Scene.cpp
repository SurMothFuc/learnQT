#include "Scene.h"
#include "iostream"
#include <stdexcept>
#include <algorithm>
#include <array>
#include <cmath>
#include <direct.h>  // POSIX 标准
#include <limits>

namespace {

std::string g_startupModelPath;
QString g_startupScenePath;

} // namespace


Scene::Scene(bool initialize)
{
    if (!initialize) return;
    QString error;
    const QString path = g_startupScenePath.isEmpty()
        ? QString::fromStdString(getResourcePath("scenes/bedroom.scene.json")) : g_startupScenePath;
    auto prepared = prepareScene(g_startupModelPath.empty() ? path : QString::fromStdString(g_startupModelPath), !g_startupModelPath.empty(), error);
    if (!prepared) throw std::runtime_error(error.toStdString());
    adoptPrepared(*prepared);
    RenderParams::instance().applySnapshot(document.settings());
}

void Scene::setStartupScenePath(const QString& path) { g_startupScenePath=path; }

void Scene::setStartupModelPath(const std::string& filepath)
{
    g_startupModelPath = filepath;
}

Scene::~Scene()
{
    resetSceneData();
}

bool Scene::loadModelScene(const std::string& filepath, std::string* errorMessage)
{
    QString error;
    auto candidate=prepareScene(QString::fromStdString(filepath),true,error);
    if(!candidate) { if(errorMessage) *errorMessage=error.toStdString(); return false; }
    adoptPrepared(*candidate); return true;
}

void Scene::resetSceneData()
{
    triangles.clear();
    textures.clear();
    nodes.clear();
    triangles_encoded.clear();
    nodes_encoded.clear();
    lights_encoded.clear();
    lightPowerSum = 0.0f;

    if (cache != nullptr) {
        delete[] cache;
        cache = nullptr;
    }

    if (hdrRes.cols != nullptr) {
        delete[] hdrRes.cols;
        hdrRes.cols = nullptr;
    }

    hdrRes.width = 0;
    hdrRes.height = 0;
    hdrResolution = 0;
}

void Scene::finalizeScene()
{
    const int nTriangles = static_cast<int>(triangles.size());
    std::cout << "Model loading completed: total " << nTriangles << " triangles" << std::endl;

    if (nTriangles == 0) {
        std::cout << "Scene finalization skipped: no triangles loaded" << std::endl;
        return;
    }

    // 建立 bvh
    BVHNode testNode;
    testNode.left = 255;
    testNode.right = 128;
    testNode.n = 30;
    testNode.AA = QVector3D(1, 1, 0);
    testNode.BB = QVector3D(0, 1, 0);
    nodes = std::vector<BVHNode>{ testNode };
    int max_deep = 0;
    BuildBVH::buildBVHwithSAH(triangles, nodes, 0, nTriangles - 1, 8, 0, max_deep);
    const int nNodes = static_cast<int>(nodes.size());
    std::cout << "BVH construction completed: total " << nNodes << " nodes, depth " << max_deep << std::endl;
    //建立bvh需要在三角形编码之前，因为bvh的构建使用了排序

    buildLightData();
    std::cout << "Light encoding completed: total " << lights_encoded.size() << " lights" << std::endl;
    DataEncode(nTriangles, nNodes);
    std::cout << "Triangle and BVH encoding completed" << std::endl;

    const QString hdrPath=document.root["hdr"].toString();
    if(hdrPath.isEmpty()) {
        hdrRes.width=hdrRes.height=hdrResolution=1;
        hdrRes.cols=new float[3]{0,0,0}; cache=new float[3]{0,0,0}; return;
    }
    const bool hdrLoaded = HDRLoader::load(hdrPath.toUtf8().constData(), hdrRes);
    std::cout << "load HDRtexture:" << hdrLoaded << std::endl;
    if (!hdrLoaded) {
        std::cout << "HDR texture loading failed, HDR cache is unavailable" << std::endl;
        return;
    }

    // hdr 重要性采样 cache
    std::cout << "Calculating HDR texture importance sampling cache, current resolution: " << hdrRes.width << " " << hdrRes.height << std::endl;
    cache = calculateHdrCache(hdrRes.cols, hdrRes.width, hdrRes.height);
    hdrResolution = hdrRes.width;
}

void Scene::addAnalyticLights(std::vector<float>& weights)
{
    const float pi = static_cast<float>(PI);
    const auto luminance = [](const QVector3D& color) {
        return 0.212671f * color.x() + 0.715160f * color.y() + 0.072169f * color.z();
    };

    for(const auto value : document.root["lights"].toArray()) {
        const auto definition=value.toObject();
        const bool sun=definition["type"].toString()=="sun";
        const float radius=definition["radius"].toDouble();
        const QVector3D radiance=sceneVector(definition["radiance"]);
        const QVector3D position=sceneVector(definition[sun ? "direction" : "position"]);
        Light_encoded light;
        light.param0=QVector4D(sun ? EncodedLightSunDisk : EncodedLightSphere,-1,0,radius);
        light.param1=QVector4D(sun ? position.normalized() : position,0);
        light.param2=QVector4D(radiance,0); light.param3=QVector4D();
        const float sunSolidAngle=2*pi*(1-std::cos(radius));
        const QVector3D sunIrradiance=radiance*sunSolidAngle;
        lights_encoded.push_back(light);
        weights.push_back(std::max(0.f,sun ? luminance(sunIrradiance) : 4*pi*radius*radius*luminance(radiance)));
    }
}

void Scene::buildLightData()
{
    lights_encoded.clear();
    lightPowerSum = 0.0f;

    std::vector<float> weights;
    weights.reserve(triangles.size() + 3);

    const auto luminance = [](const QVector3D& color) {
        return 0.212671f * color.x() + 0.715160f * color.y() + 0.072169f * color.z();
    };

    for (Triangle& triangle : triangles) {
        triangle.material.lightSelectPdf = 0.0f;
    }

    for (int i = 0; i < static_cast<int>(triangles.size()); ++i) {
        const Triangle& triangle = triangles[i];
        const QVector3D e0 = triangle.p2 - triangle.p1;
        const QVector3D e1 = triangle.p3 - triangle.p1;
        const float area = 0.5f * QVector3D::crossProduct(e0, e1).length();
        QVector3D averageEmission = triangle.material.emissive;
        const int emissiveTextureIndex = triangle.material.emissiveTex;
        if (emissiveTextureIndex >= 0 && emissiveTextureIndex < static_cast<int>(textures.size())) {
            const QVector3D& textureAverage = textures[emissiveTextureIndex].averageLinearColor;
            averageEmission = QVector3D(
                averageEmission.x() * textureAverage.x(),
                averageEmission.y() * textureAverage.y(),
                averageEmission.z() * textureAverage.z());
        }
        const float emissionLum = luminance(averageEmission);
        const float weight = area * emissionLum;
        if (area <= 1e-8f || weight <= 0.0f) {
            continue;
        }

        Light_encoded light;
        light.param0 = QVector4D(EncodedLightTriangle, static_cast<float>(i), 0.0f, 0.0f);
        light.param1 = QVector4D(0.0f, 0.0f, 0.0f, area);
        light.param2 = QVector4D(triangle.material.emissive, 0.0f);
        light.param3 = QVector4D(0.0f, 0.0f, 0.0f, 0.0f);
        lights_encoded.push_back(light);
        weights.push_back(weight);
    }

    addAnalyticLights(weights);

    for (float weight : weights) {
        lightPowerSum += weight;
    }

    if (lightPowerSum <= 0.0f) {
        lights_encoded.clear();
        lightPowerSum = 0.0f;
        return;
    }

    float cdf = 0.0f;
    for (int i = 0; i < static_cast<int>(lights_encoded.size()); ++i) {
        const float selectPdf = weights[i] / lightPowerSum;
        cdf = std::min(1.0f, cdf + selectPdf);
        lights_encoded[i].param0.setZ(selectPdf);
        lights_encoded[i].param3.setX(cdf);
        const int triangleIndex = static_cast<int>(lights_encoded[i].param0.y());
        if (static_cast<int>(lights_encoded[i].param0.x()) == EncodedLightTriangle &&
            triangleIndex >= 0 && triangleIndex < static_cast<int>(triangles.size())) {
            triangles[triangleIndex].material.lightSelectPdf = selectPdf;
        }
    }

    if (!lights_encoded.empty()) {
        lights_encoded.back().param3.setX(1.0f);
    }
}

void Scene::DataEncode(int nTriangles,int nNodes)
{
    triangles_encoded = std::vector<Triangle_encoded>(nTriangles);
    for (int i = 0; i < nTriangles; i++) {
        Triangle& t = triangles[i];
        Material& m = t.material;
        // 顶点位置
        triangles_encoded[i].p1 = QVector4D(t.p1);
        triangles_encoded[i].p2 = QVector4D(t.p2) ;
        triangles_encoded[i].p3 = QVector4D(t.p3) ;
        // 顶点法线
        triangles_encoded[i].n1 = QVector4D(t.n1) ;
        triangles_encoded[i].n2 = QVector4D(t.n2);
        triangles_encoded[i].n3 = QVector4D(t.n3);
        // 材质
        triangles_encoded[i].param1 = QVector4D(m.emissive,m.sheenTint);
        triangles_encoded[i].param2 = QVector4D(m.baseColor,m.clearcoat);
        triangles_encoded[i].param3 = QVector4D(m.mediumColor,m.mediumAnisotropy);
        triangles_encoded[i].param4 = QVector4D(m.clearcoatGloss, m.IOR, m.transmission,m.alphaMode);
        triangles_encoded[i].param5 = QVector4D(m.mediumtype, m.mediumDensity, m.subsurface, m.metallic);
        triangles_encoded[i].param6 = QVector4D(m.specularTint,m.roughness, m.anisotropic, m.sheen);
        triangles_encoded[i].uv12 = QVector4D(t.uv1.x(), t.uv1.y(), t.uv2.x(), t.uv2.y());
        triangles_encoded[i].uv3Tex0 = QVector4D(t.uv3.x(), t.uv3.y(), m.baseColorTex, m.normalTex);
        triangles_encoded[i].tex1 = QVector4D(m.metallicTex, m.roughnessTex, m.emissiveTex, m.opacityTex);
        triangles_encoded[i].textureParam0 = QVector4D(
            m.opacity,
            m.alphaCutoff,
            m.normalScale,
            m.normalMapFlipY ? 1.0f : 0.0f);
        triangles_encoded[i].textureParam1 = QVector4D(
            m.metallicChannel,
            m.roughnessChannel,
            m.lightSelectPdf,
            0.0f);
        triangles_encoded[i].tangent1 = t.tangent1;
        triangles_encoded[i].tangent2 = t.tangent2;
        triangles_encoded[i].tangent3 = t.tangent3;
    }

    // 编码 BVHNode, aabb
    nodes_encoded = std::vector<BVHNode_encoded>(nNodes);
    for (int i = 0; i < nNodes; i++) {
        nodes_encoded[i].childs = QVector3D(nodes[i].left, nodes[i].right, 0);
        nodes_encoded[i].leafInfo = QVector3D(nodes[i].n, nodes[i].index, 0);
        nodes_encoded[i].AA = nodes[i].AA;
        nodes_encoded[i].BB = nodes[i].BB;
    }

}


void Scene::updateMaterial(QVector3D emissive, QVector3D  baseColor,
    float subsurface, float metallic, float specularTint, float roughness, float anisotropic,
    float sheen, float sheenTint, float clearcoat, 
    float clearcoatGloss, float IOR, float transmission)
{
    const int nTriangles = static_cast<int>(triangles.size());
    for (int i = 0; i < nTriangles; ++i) {
        Material& material = triangles[i].material;
        material.emissive = emissive;
        material.baseColor = baseColor;
        material.subsurface = subsurface;
        material.metallic = metallic;
        material.specularTint = specularTint;
        material.roughness = roughness;
        material.anisotropic = anisotropic;
        material.sheen = sheen;
        material.sheenTint = sheenTint;
        material.clearcoat = clearcoat;
        material.clearcoatGloss = clearcoatGloss;
        material.IOR = IOR;
        material.transmission = transmission;

        if (i >= static_cast<int>(triangles_encoded.size())) {
            continue;
        }

        Triangle_encoded& encoded = triangles_encoded[i];
        encoded.param1 = QVector4D(material.emissive, material.sheenTint);
        encoded.param2 = QVector4D(material.baseColor, material.clearcoat);
        encoded.param3 = QVector4D(material.mediumColor, material.mediumAnisotropy);
        encoded.param4 = QVector4D(material.clearcoatGloss, material.IOR, material.transmission, material.alphaMode);
        encoded.param5 = QVector4D(material.mediumtype, material.mediumDensity, material.subsurface, material.metallic);
        encoded.param6 = QVector4D(material.specularTint, material.roughness, material.anisotropic, material.sheen);
    }

    buildLightData();
}


