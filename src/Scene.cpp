#include "Scene.h"
#include "iostream"
#include <direct.h>  // POSIX 标准


Scene::Scene(){
    camera = Camera(QVector3D(0.0f, 1.17f, 4.0f), QVector3D(0.0f, 1.0f, 0.0f));
    Material mt;
    //light
    mt = Material();
    mt.emissive = QVector3D(10.0, 10.0, 10.0);
    //mt.roughness = 0.1;
    //mt.subsurface = 1.0;
    //mt.anisotropic = 1.0;
    mt.baseColor = QVector3D(1.0, 1.0, 1.0);
    MeshLoader::readObj(getResourcePath("models/quad.obj"), triangles, mt, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0, 2.0, 0), QVector3D(1.0, 0.01, 1.0)), false);

    //box    
    mt = Material();
    mt.roughness = 0.1;
    //mt.subsurface = 1.0;
    //mt.anisotropic = 1.0;
    mt.baseColor = QVector3D(0.725, 0.71, 0.68);
    MeshLoader::readObj(getResourcePath("models/quad.obj"), triangles, mt, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0, -0.7, 0), QVector3D(4, 0.01, 4)), false);
    mt = Material();
    mt.roughness = 0.1;
    //mt.subsurface = 1.0;
    //mt.anisotropic = 1.0;
    mt.baseColor = QVector3D(0, 1, 0);
    MeshLoader::readObj(getResourcePath("models/quad.obj"), triangles, mt, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0), QVector3D(-2, 0.0, 0), QVector3D(0.01,4,  4)), false);
    mt = Material();
    mt.roughness = 0.1;
    //mt.subsurface = 1.0;
    //mt.anisotropic = 1.0;
    mt.baseColor = QVector3D(1, 0,0);
    MeshLoader::readObj(getResourcePath("models/quad.obj"), triangles, mt, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0), QVector3D(2, 0.0, 0), QVector3D(0.01, 4, 4)), false);
    mt = Material();
    mt.roughness = 0.1;
    //mt.subsurface = 1.0;
    //mt.anisotropic = 1.0;
    mt.baseColor = QVector3D(0.725, 0.71, 0.68);
    MeshLoader::readObj(getResourcePath("models/quad.obj"), triangles, mt, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0, 2.001, 0), QVector3D(4, 0.01, 4)), false);
    mt = Material();
    mt.roughness = 0.1;
    //mt.subsurface = 1.0;
    //mt.anisotropic = 1.0;
    mt.baseColor = QVector3D(0.725, 0.71, 0.68);
    MeshLoader::readObj(getResourcePath("models/quad.obj"), triangles, mt, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0, 0, -2), QVector3D(4,  4,0.01)), false);




    //object
    mt = Material();
    mt.roughness = 0.001;
    mt.transmission = 1.0;
    mt.IOR = 1.5;
    mt.baseColor = QVector3D(1.0f, 1.0f, 1.0f);
    MeshLoader::readObj(getResourcePath("models/sphere2.obj"), triangles, mt, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0.6, 0.2, 0.6), QVector3D(1, 1, 1)), true);

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
    MeshLoader::readObj(getResourcePath("models/quad.obj"), triangles, mt, MeshLoader::getTransformMatrix(QVector3D(0, 45, 0), QVector3D(-0.6, -0.1, 0.2), QVector3D(1.0, 1.0, 1.0)), false);

    mt = Material();;
    mt.roughness = 0.001;
    mt.transmission = 1.0;
    mt.IOR = 1.5;
    //mt.metallic = 1.0;
    //mt.subsurface = 1.0;
    //mt.anisotropic = 1.0;
    mt.baseColor = QVector3D(1.0, 1.0, 1.0);
   // MeshLoader::readObj(getResourcePath("models/10778_Toilet_V2.obj"), triangles, mt, MeshLoader::getTransformMatrix(QVector3D(-90, 0, 0), QVector3D(0.2, 0,0), QVector3D(1.0, 1.0, 1.0)), true);
    
    mt = Material();;
    mt.roughness = 0.001; 
    mt.transmission = 1.0;
    mt.IOR = 1.5;
    //mt.metallic = 1.0;
    //mt.subsurface = 1.0;
    //mt.anisotropic = 1.0;
    mt.baseColor = QVector3D(1.0, 1.0, 1.0);
    //MeshLoader::readObj(getResourcePath("models/untitld.obj"), triangles, mt, MeshLoader::getTransformMatrix(QVector3D(-90, 0, 0), QVector3D(0, 0,0), QVector3D(1.2, 1.2, 1.2)),true);

    


    int nTriangles = triangles.size();
    std::cout << "模型读取完成: 共 " << nTriangles << " 个三角形" << std::endl;

    // 建立 bvh
    BVHNode testNode;
    testNode.left = 255;
    testNode.right = 128;
    testNode.n = 30;
    testNode.AA = QVector3D(1, 1, 0);
    testNode.BB = QVector3D(0, 1, 0);
    nodes= std::vector<BVHNode>{ testNode };
    int max_deep = 0;
    BuildBVH::buildBVHwithSAH(triangles, nodes, 0, triangles.size() - 1, 8,0, max_deep);
    int nNodes = nodes.size();
    std::cout << "BVH 建立完成: 共 " << nNodes << " 个节点， 深度 " <<max_deep <<std::endl;
    //建立bvh需要在三角形编码之前，因为bvh的构建使用了排序

    
    DataEncode(nTriangles, nNodes);
    std::cout << "完成三角形、BVH编码" << std::endl;


    std::cout <<"load HDRtexture:" << HDRLoader::load(getResourcePath("hdr/peppermint_powerplant_4k.hdr").c_str(), hdrRes) << std::endl;
    // hdr 重要性采样 cache
    std::cout << "计算 HDR 贴图重要性采样 Cache, 当前分辨率: " << hdrRes.width << " " << hdrRes.height << std::endl;
    cache = calculateHdrCache(hdrRes.cols, hdrRes.width, hdrRes.height);
    hdrResolution = hdrRes.width;
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
    float subsurface, float  metallic, float  specular,
    float specularTint, float roughness, float anisotropic,
    float sheen, float sheenTint, float clearcoat, 
    float clearcoatGloss, float IOR, float transmission)
{
    //int nTriangles = triangles.size();
    //for (int i = 0; i < nTriangles; i++) {       
    //    triangles_encoded[i].emissive = emissive;
    //    //triangles_encoded[i].baseColor = baseColor;
    //    triangles_encoded[i].param1 = QVector3D(subsurface, metallic, specular);
    //    triangles_encoded[i].param2 = QVector3D(specularTint, roughness, anisotropic);
    //    triangles_encoded[i].param3 = QVector3D(sheen, sheenTint, clearcoat);
    //    triangles_encoded[i].param4 = QVector3D(clearcoatGloss, IOR, transmission);
    //}
}


