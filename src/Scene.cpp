#include "Scene.h"
#include "iostream"
#include <algorithm>
#include <array>
#include <cmath>
#include <direct.h>  // POSIX 标准


Scene::Scene()
{
    resetSceneData();
    //camera = Camera(QVector3D(0.0f, 1.17f, 4.0f), QVector3D(0.0f, 1.0f, 0.0f));
    

    // Replace this one call with buildLegacyGlassScene() to restore the old glass scene.
    //buildImportanceSamplingBenchmarkScene();
    buildLegacyGlassScene();
    finalizeScene();
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

void Scene::buildImportanceSamplingBenchmarkScene()
{
    camera = Camera(QVector3D(18.0f/1.5f, 4.0f/1.5f, 0.0f/1.5f), QVector3D(0.0f, 1.0f, 0.0f));
    std::cout << "Building importance sampling benchmark scene" << std::endl;

    Material mtWall;
    mtWall.baseColor=QVector3D(1.0f, 1.0f, 1.0f);

    MeshLoader::readModel(getResourcePath("models/veach/wall.obj"), triangles, textures, mtWall, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0, 0, 0), QVector3D(1.0, 1.0, 1.0)), false,false);

    MeshLoader::readModel(getResourcePath("models/veach/floor.obj"), triangles, textures, mtWall, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0, 0, 0), QVector3D(1.0, 1.0, 1.0)), false,false);


    Material mtPlate;
    mtPlate.baseColor=QVector3D(1.0f, 1.0f, 1.0f);
    mtPlate.metallic=1.0f;

    mtPlate.roughness=0.01f;
    MeshLoader::readModel(getResourcePath("models/veach/plate1.obj"), triangles, textures, mtPlate, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0, 0, 0), QVector3D(1.0, 1.0, 1.0)), false,false);

    mtPlate.roughness=0.04f;
    MeshLoader::readModel(getResourcePath("models/veach/plate2.obj"), triangles, textures, mtPlate, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0, 0, 0), QVector3D(1.0, 1.0, 1.0)), false,false);

    mtPlate.roughness=0.09f;
    MeshLoader::readModel(getResourcePath("models/veach/plate3.obj"), triangles, textures, mtPlate, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0, 0, 0), QVector3D(1.0, 1.0, 1.0)), false,false);

    mtPlate.roughness=0.16f;
    MeshLoader::readModel(getResourcePath("models/veach/plate4.obj"), triangles, textures, mtPlate, MeshLoader::getTransformMatrix(QVector3D(0, 0, -3.5f), QVector3D(0, 0, 0), QVector3D(1.0, 1.0, 1.0)), false,false);

    Material mtLight;
    mtLight.baseColor=QVector3D(1.0f, 1.0f, 1.0f);

    mtLight.emissive = QVector3D(2.0f, 0.64f, 0.174f)*10.0f;
    MeshLoader::readModel(getResourcePath("models/sphere.obj"), triangles, textures, mtLight, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0),QVector3D(-3.3724, 5.5, -3.74116)- QVector3D(0, 0.5f, 0), QVector3D(1.2, 1.2, 1.2)), true,true);

    mtLight.emissive = QVector3D(0.87f, 2.0f, 0.28f)*10.0f;
    MeshLoader::readModel(getResourcePath("models/sphere.obj"), triangles, textures, mtLight, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0),QVector3D(-3.3724, 5.5, -1.24)- QVector3D(0, 0.5f, 0), QVector3D(0.6, 0.6, 0.6)), true,true);

    mtLight.emissive = QVector3D(0.41f, 1.88f, 2.0f)*10.0f;
    MeshLoader::readModel(getResourcePath("models/sphere.obj"), triangles, textures, mtLight, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0),QVector3D(-3.3724, 5.5, 1.24)- QVector3D(0, 0.5f, 0), QVector3D(0.3, 0.3, 0.3)), true,true);

    mtLight.emissive = QVector3D(1.43f, 0.46f, 2.0f)*10.0f;
    MeshLoader::readModel(getResourcePath("models/sphere.obj"), triangles, textures, mtLight, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0),QVector3D(-3.3724, 5.5, 3.74116)- QVector3D(0, 0.5f, 0), QVector3D(0.1, 0.1, 0.1)), true,true);


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

    DataEncode(nTriangles, nNodes);
    std::cout << "Triangle and BVH encoding completed" << std::endl;
    buildLightData();
    std::cout << "Light encoding completed: total " << lights_encoded.size() << " lights" << std::endl;

    const bool hdrLoaded = HDRLoader::load(getResourcePath("hdr/peppermint_powerplant_4k.hdr").c_str(), hdrRes);
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
    const auto luminance = [](const QVector3D& color) {
        return 0.212671f * color.x() + 0.715160f * color.y() + 0.072169f * color.z();
    };

    Light_encoded pointLight;
    pointLight.param0 = QVector4D(EncodedLightPoint, -1.0f, 0.0f, 0.0f);
    pointLight.param1 = QVector4D(-1.0f, 4.0f, 0.0f, 12.0f);
    pointLight.param2 = QVector4D(45.0f, 40.0f, 32.0f, 0.0f);
    pointLight.param3 = QVector4D(0.0f, 0.0f, 0.0f, 0.0f);
    lights_encoded.push_back(pointLight);
    weights.push_back(std::max(luminance(QVector3D(45.0f, 40.0f, 32.0f)), 0.0f));

    Light_encoded directionalLight;
    directionalLight.param0 = QVector4D(EncodedLightDirectional, -1.0f, 0.0f, 0.0f);
    directionalLight.param1 = QVector4D(QVector3D(-0.45f, -1.0f, 0.2f).normalized(), 0.0f);
    directionalLight.param2 = QVector4D(0.25f, 0.28f, 0.35f, 0.0f);
    directionalLight.param3 = QVector4D(0.0f, 0.0f, 0.0f, 0.0f);
    lights_encoded.push_back(directionalLight);
    weights.push_back(std::max(luminance(QVector3D(0.25f, 0.28f, 0.35f)), 0.0f));

    Light_encoded sphereLight;
    sphereLight.param0 = QVector4D(EncodedLightSphere, -1.0f, 0.0f, 0.45f);
    sphereLight.param1 = QVector4D(1.8f, 3.2f, 1.0f, 0.0f);
    sphereLight.param2 = QVector4D(7.0f, 5.2f, 3.6f, 0.0f);
    sphereLight.param3 = QVector4D(0.0f, 0.0f, 0.0f, 0.0f);
    lights_encoded.push_back(sphereLight);
    const float sphereArea = 4.0f * PI * sphereLight.param0.w() * sphereLight.param0.w();
    weights.push_back(std::max(sphereArea * luminance(QVector3D(7.0f, 5.2f, 3.6f)), 0.0f));
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

    for (int i = 0; i < static_cast<int>(triangles.size()); ++i) {
        const Triangle& triangle = triangles[i];
        const QVector3D e0 = triangle.p2 - triangle.p1;
        const QVector3D e1 = triangle.p3 - triangle.p1;
        const float area = 0.5f * QVector3D::crossProduct(e0, e1).length();
        const float emissionLum = luminance(triangle.material.emissive);
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

    //addAnalyticLights(weights);

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
    }

    if (!lights_encoded.empty()) {
        lights_encoded.back().param3.setX(1.0f);
    }
}

void Scene::buildLegacyGlassScene()
{
    camera = Camera(QVector3D(0.0f, 1.17f, 4.0f), QVector3D(0.0f, 1.0f, 0.0f));
    Material mt;
    //light
    mt = Material();
    mt.emissive = QVector3D(10.0, 10.0, 10.0);
    //mt.roughness = 0.1;
    //mt.subsurface = 1.0;
    //mt.anisotropic = 1.0;
    mt.baseColor = QVector3D(1.0, 1.0, 1.0);
    //MeshLoader::readModel(getResourcePath("models/quad.obj"), triangles, textures, mt, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0, 2.0, 0), QVector3D(1.0, 0.01, 1.0)), false,true);

    //box    
    mt = Material();
    mt.roughness = 0.1;
    //mt.subsurface = 1.0;
    //mt.anisotropic = 1.0;
    mt.baseColor = QVector3D(0.725, 0.71, 0.68);
    //MeshLoader::readModel(getResourcePath("models/quad.obj"), triangles, textures, mt, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0, -0.7, 0), QVector3D(4, 0.01, 4)), false);
    MeshLoader::readModel(getResourcePath("models/quad.obj"), triangles, textures, mt, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0, -0.01, 0), QVector3D(4, 0.01, 4)), false,true);
    mt = Material();
    mt.roughness = 0.1;
    //mt.subsurface = 1.0;
    //mt.anisotropic = 1.0;
    mt.baseColor = QVector3D(0, 1, 0);
  //  MeshLoader::readModel(getResourcePath("models/quad.obj"), triangles, textures, mt, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0), QVector3D(-2, 0.0, 0), QVector3D(0.01,4,  4)), false);
    mt = Material();
    mt.roughness = 0.1;
    //mt.subsurface = 1.0;
    //mt.anisotropic = 1.0;
    mt.baseColor = QVector3D(1, 0,0);
 //   MeshLoader::readModel(getResourcePath("models/quad.obj"), triangles, textures, mt, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0), QVector3D(2, 0.0, 0), QVector3D(0.01, 4, 4)), false);
    mt = Material();
    mt.roughness = 0.1;
    //mt.subsurface = 1.0;
    //mt.anisotropic = 1.0;
    mt.baseColor = QVector3D(0.725, 0.71, 0.68);
  //  MeshLoader::readModel(getResourcePath("models/quad.obj"), triangles, textures, mt, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0, 2.001, 0), QVector3D(4, 0.01, 4)), false);
    mt = Material();
    mt.roughness = 0.1;
    //mt.subsurface = 1.0;
    //mt.anisotropic = 1.0;
    mt.baseColor = QVector3D(0.725, 0.71, 0.68);
  //  MeshLoader::readModel(getResourcePath("models/quad.obj"), triangles, textures, mt, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0, 0, -2), QVector3D(4,  4,0.01)), false);




    //object
    mt = Material();
    mt.roughness = 0.001;
    mt.transmission = 1.0;
    mt.IOR = 1.5;
    mt.baseColor = QVector3D(1.0f, 1.0f, 1.0f);
    //MeshLoader::readModel(getResourcePath("models/sphere2.obj"), triangles, textures, mt, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0.6, 0.2, 0.6), QVector3D(1, 1, 1)), true);

    mt = Material();;
    //mt.roughness = 0.1;
    mt.alphaMode = (int)AlphaMode::Transparent;
    mt.mediumColor = QVector3D(0.458, 0.95, 1.0);
    mt.mediumDensity =5.0;
    mt.mediumtype = (int)MediumType::Scatter;
    //mt.metallic = 1.0;
    //mt.subsurface = 1.0;
    //mt.anisotropic = 1.0;
    //mt.baseColor = QVector3D(1.0, 1.0, 1.0);
    //MeshLoader::readModel(getResourcePath("models/quad.obj"), triangles, textures, mt, MeshLoader::getTransformMatrix(QVector3D(0, 45, 0), QVector3D(-0.6, -0.1, 0.2), QVector3D(1.0, 1.0, 1.0)), false);

    mt = Material();;
    mt.roughness = 0.001;
    mt.transmission = 1.0;
    mt.IOR = 1.5;
    //mt.metallic = 1.0;
    //mt.subsurface = 1.0;
    //mt.anisotropic = 1.0;
    mt.baseColor = QVector3D(1.0, 1.0, 1.0);
   // MeshLoader::readModel(getResourcePath("models/10778_Toilet_V2.obj"), triangles, textures, mt, MeshLoader::getTransformMatrix(QVector3D(-90, 0, 0), QVector3D(0.2, 0,0), QVector3D(1.0, 1.0, 1.0)), true);
    
    mt = Material();;
    mt.roughness = 0.001; 
    mt.transmission = 1.0;
    mt.IOR = 1.5;
    //mt.metallic = 1.0;
    //mt.subsurface = 1.0;
    //mt.anisotropic = 1.0;
    mt.baseColor = QVector3D(1.0, 1.0, 1.0);
    //MeshLoader::readModel(getResourcePath("models/untitld.obj"), triangles, textures, mt, MeshLoader::getTransformMatrix(QVector3D(-90, 0, 0), QVector3D(0, 0,0), QVector3D(1.2, 1.2, 1.2)),true);

    mt = Material();
    mt.roughness = 0.001;
    mt.transmission = 1.0;
    mt.IOR = 1.5;
    mt.baseColor = QVector3D(1.0f, 1.0f, 1.0f);
    MeshLoader::readModel(getResourcePath("models/glass.obj"), triangles, textures, mt, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0, 0, 0), QVector3D(1, 1, 1)), true,true);



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


