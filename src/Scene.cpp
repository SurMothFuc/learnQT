#include "Scene.h"
#include "iostream"
#include <array>
#include <direct.h>  // POSIX 标准

namespace {

Material makeBedroomMaterial(const QVector3D& baseColor,
                             float roughness = 1.0f,
                             float metallic = 0.0f,
                             float transmission = 0.0f)
{
    Material material;
    material.baseColor = baseColor;
    material.roughness = roughness;
    material.metallic = metallic;
    material.transmission = transmission;
    return material;
}

} // namespace


Scene::Scene()
{
    resetSceneData();
    //camera = Camera(QVector3D(0.0f, 1.17f, 4.0f), QVector3D(0.0f, 1.0f, 0.0f));
    

    // Manual switch: comment buildLegacyGlassScene() and uncomment buildBedroomScene().
    //buildImportanceSamplingBenchmarkScene();
    //buildLegacyGlassScene();
    buildBedroomScene();
    finalizeScene();
}

void Scene::resetSceneData()
{
    triangles.clear();
    textures.clear();
    nodes.clear();
    triangles_encoded.clear();
    nodes_encoded.clear();

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

void Scene::buildBedroomScene()
{
    const QVector3D referenceLookAt(0.0f, 1.0f, 0.31f);
    const QVector3D sceneOffset = -referenceLookAt;
    camera = Camera(QVector3D(3.0f, 0.5f, 3.49f), QVector3D(0.0f, 1.0f, 0.0f));
    std::cout << "Building bedroom scene" << std::endl;

    const QMatrix4x4 bedroomTransform = MeshLoader::getTransformMatrix(
        QVector3D(0.0f, 0.0f, 0.0f),
        sceneOffset,
        QVector3D(1.0f, 1.0f, 1.0f));
    const bool smoothNormal = true;
    const bool enableNormalization = false;

    const Material boxes = makeBedroomMaterial(QVector3D(0.483044f, 0.384664f, 0.301561f));
    const Material plasticCable = makeBedroomMaterial(QVector3D(0.558543f, 0.558543f, 0.558543f));
    const Material lampEmitter = makeBedroomMaterial(QVector3D(0.64f, 0.64f, 0.64f));
    const Material blankets = makeBedroomMaterial(QVector3D(0.485435f, 0.456263f, 0.428075f));
    const Material bedsheets = makeBedroomMaterial(QVector3D(0.908f, 0.922f, 0.946f));
    const Material window = makeBedroomMaterial(QVector3D(0.48173f, 0.48173f, 0.48173f));
    const Material pictureBacking = makeBedroomMaterial(QVector3D(0.111567f, 0.037068f, 0.017016f));
    const Material picture = makeBedroomMaterial(QVector3D(0.590f, 0.578f, 0.556f));
    const Material rocks1 = makeBedroomMaterial(QVector3D(0.350827f, 0.242986f, 0.17883f));
    const Material rocks2 = makeBedroomMaterial(QVector3D(0.098964f, 0.098964f, 0.098964f));
    const Material rocks3 = makeBedroomMaterial(QVector3D(0.558544f, 0.558544f, 0.558544f));
    const Material decoPlant = makeBedroomMaterial(QVector3D(0.041772f, 0.011306f, 0.007575f));
    const Material black = makeBedroomMaterial(QVector3D(0.015396f, 0.015396f, 0.015396f));
    const Material carpet = makeBedroomMaterial(QVector3D(0.034499f, 0.034499f, 0.034499f));
    const Material matress = makeBedroomMaterial(QVector3D(0.893289f, 0.893289f, 0.893289f));
    const Material woodFloor = makeBedroomMaterial(QVector3D(0.710f, 0.523f, 0.361f), 0.15f);
    const Material walls = makeBedroomMaterial(QVector3D(0.799999f, 0.799999f, 0.799999f));
    const Material walls2 = makeBedroomMaterial(QVector3D(0.799999f, 0.799999f, 0.799999f));
    const Material woodFurniture = makeBedroomMaterial(QVector3D(0.774f, 0.484f, 0.154f), 0.15f);
    const Material mirror = makeBedroomMaterial(QVector3D(1.0f, 1.0f, 1.0f), 0.0f, 1.0f);
    const Material aluminium = makeBedroomMaterial(QVector3D(1.0f, 1.0f, 1.0f), 0.2f, 1.0f);
    const Material bookCover = makeBedroomMaterial(QVector3D(0.0f, 0.0f, 0.0f));
    const Material bookPages = makeBedroomMaterial(QVector3D(0.567027f, 0.567027f, 0.567027f));
    const Material lampMetal = makeBedroomMaterial(QVector3D(1.0f, 1.0f, 1.0f), 0.1f);
    const Material glass = makeBedroomMaterial(QVector3D(1.0f, 1.0f, 1.0f), 0.0f, 0.0f, 1.0f);
    const Material roughGlass = makeBedroomMaterial(QVector3D(1.0f, 1.0f, 1.0f), 0.1f, 0.0f, 1.0f);
    const Material pictureFrame = makeBedroomMaterial(QVector3D(1.0f, 1.0f, 1.0f), 0.1f, 1.0f);
    const Material curtainRod = makeBedroomMaterial(QVector3D(0.5f, 0.5f, 0.5f), 0.1f, 1.0f);
    const Material stainlessSmooth = makeBedroomMaterial(QVector3D(1.0f, 1.0f, 1.0f), 0.0f, 1.0f);

    auto loadMesh = [&](const char* filename, const Material& material) {
        MeshLoader::readModel(
            getResourcePath(std::string("models/bedroom/") + filename),
            triangles,
            textures,
            material,
            bedroomTransform,
            smoothNormal,
            enableNormalization);
    };

    loadMesh("Mesh044.obj", aluminium);
    loadMesh("Mesh047.obj", aluminium);
    loadMesh("Mesh032.obj", woodFurniture);
    loadMesh("Mesh028.obj", stainlessSmooth);
    loadMesh("Mesh046.obj", aluminium);
    loadMesh("Mesh027.obj", aluminium);
    loadMesh("Mesh022.obj", aluminium);
    loadMesh("Mesh042.obj", aluminium);
    loadMesh("Mesh036.obj", aluminium);
    loadMesh("Mesh043.obj", aluminium);
    loadMesh("Mesh040.obj", aluminium);
    loadMesh("Mesh037.obj", glass);
    loadMesh("Mesh026.obj", glass);
    loadMesh("Mesh023.obj", lampMetal);
    loadMesh("Mesh059.obj", lampEmitter);
    loadMesh("Mesh049.obj", roughGlass);
    loadMesh("Mesh060.obj", woodFloor);
    loadMesh("Mesh033.obj", decoPlant);
    loadMesh("Mesh025.obj", rocks1);
    loadMesh("Mesh055.obj", rocks2);
    loadMesh("Mesh035.obj", rocks3);
    loadMesh("Mesh048.obj", glass);
    loadMesh("Mesh056.obj", lampMetal);
    loadMesh("Mesh058.obj", plasticCable);
    loadMesh("Mesh061.obj", lampEmitter);
    loadMesh("Mesh051.obj", glass);
    loadMesh("Mesh066.obj", lampMetal);
    loadMesh("Mesh062.obj", plasticCable);
    loadMesh("Mesh054.obj", lampEmitter);
    loadMesh("Mesh063.obj", bookCover);
    loadMesh("Mesh064.obj", bookPages);
    loadMesh("Mesh041.obj", bedsheets);
    loadMesh("Mesh052.obj", glass);
    loadMesh("Mesh065.obj", glass);
    loadMesh("Mesh067.obj", glass);
    loadMesh("Mesh068.obj", bedsheets);
    loadMesh("Mesh034.obj", bedsheets);
    loadMesh("Mesh021.obj", matress);
    loadMesh("Mesh020.obj", carpet);
    loadMesh("Mesh019.obj", carpet);
    loadMesh("Mesh018.obj", black);
    loadMesh("Mesh017.obj", black);
    loadMesh("Mesh069.obj", black);
    loadMesh("Mesh015.obj", curtainRod);
    loadMesh("Mesh014.obj", curtainRod);
    loadMesh("Mesh012.obj", woodFurniture);
    loadMesh("Mesh011.obj", mirror);
    loadMesh("Mesh013.obj", walls);
    loadMesh("Mesh039.obj", window);
    loadMesh("Mesh010.obj", window);
    loadMesh("Mesh031.obj", woodFurniture);
    loadMesh("Mesh045.obj", stainlessSmooth);
    loadMesh("Mesh038.obj", mirror);
    loadMesh("Mesh009.obj", woodFurniture);
    loadMesh("Mesh024.obj", stainlessSmooth);
    loadMesh("Mesh030.obj", woodFurniture);
    loadMesh("Mesh029.obj", stainlessSmooth);
    loadMesh("Mesh008.obj", walls2);
    loadMesh("Mesh007.obj", woodFurniture);
    loadMesh("Mesh006.obj", woodFurniture);
    loadMesh("Mesh005.obj", stainlessSmooth);
    loadMesh("Mesh050.obj", pictureFrame);
    loadMesh("Mesh053.obj", pictureBacking);
    loadMesh("Mesh003.obj", picture);
    loadMesh("Mesh002.obj", boxes);
    loadMesh("Mesh016.obj", blankets);
    loadMesh("Mesh001.obj", blankets);
    loadMesh("Mesh000.obj", blankets);

    Material bedroomLight;
    bedroomLight.baseColor = QVector3D(1.0f, 1.0f, 1.0f);
    bedroomLight.emissive = QVector3D(1.0f, 1.0f, 1.0f);
    const QVector3D lightScale(1.064823f, 1.815584f, 0.01f);

    MeshLoader::readModel(
        getResourcePath("models/quad.obj"),
        triangles,
        textures,
        bedroomLight,
        MeshLoader::getTransformMatrix(
            QVector3D(0.0f, 0.0f, 0.0f),
            sceneOffset + QVector3D(-1.4754385f, 1.189678f, -1.26735f),
            lightScale),
        false,
        true);
    MeshLoader::readModel(
        getResourcePath("models/quad.obj"),
        triangles,
        textures,
        bedroomLight,
        MeshLoader::getTransformMatrix(
            QVector3D(0.0f, 0.0f, 0.0f),
            sceneOffset + QVector3D(1.443792f, 1.189678f, -1.26735f),
            lightScale),
        false,
        true);
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
    MeshLoader::readModel(getResourcePath("models/quad.obj"), triangles, textures, mt, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0, 2.0, 0), QVector3D(1.0, 0.01, 1.0)), false,true);

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
}


