#include "Scene.h"
#include "iostream"



Scene::Scene(){
    camera = Camera(QVector3D(3.0f, 0.0f, 1.48f), QVector3D(0.0f, 1.0f, 0.0f));
    Material mt;
    mt = Material();
    mt.roughness = 0.0;
    mt.specular = 1.0;
    //mt.transmission = 1.0;
    mt.IOR = 1.5;
    mt.baseColor = QVector3D(0.0f, 0.0f, 1.0f);
    MeshLoader::readObj("models/sphere2.obj", triangles, mt, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0.0, 0.0, 0), QVector3D(1, 1, 1)), true);

    mt = Material();
    mt.roughness = 0.1;
    mt.specular = 1.0;
    //mt.subsurface = 1.0;
    //mt.anisotropic = 1.0;
    mt.baseColor = QVector3D(0.725, 0.71, 0.68);
    MeshLoader::readObj("models/quad.obj", triangles, mt, MeshLoader::getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0, -0.7, 0), QVector3D(18.83, 0.01, 18.83)), false);
     
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
    BuildBVH::buildBVHwithSAH(triangles, nodes, 0, triangles.size() - 1, 8);
    int nNodes = nodes.size();
    std::cout << "BVH 建立完成: 共 " << nNodes << " 个节点" << std::endl;
    //建立bvh需要在三角形编码之前，因为bvh的构建使用了排序

    
    DataEncode(nTriangles, nNodes);
    std::cout << "完成三角形、BVH编码" << std::endl;


    std::cout <<"load HDRtexture:" << HDRLoader::load("./peppermint_powerplant_4k.hdr", hdrRes) << std::endl;
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
        triangles_encoded[i].p1 = t.p1;
        triangles_encoded[i].p2 = t.p2;
        triangles_encoded[i].p3 = t.p3;
        // 顶点法线
        triangles_encoded[i].n1 = t.n1;
        triangles_encoded[i].n2 = t.n2;
        triangles_encoded[i].n3 = t.n3;
        // 材质
        triangles_encoded[i].emissive = m.emissive;
        triangles_encoded[i].baseColor = m.baseColor;
        triangles_encoded[i].param1 = QVector3D(m.subsurface, m.metallic, m.specular);
        triangles_encoded[i].param2 = QVector3D(m.specularTint, m.roughness, m.anisotropic);
        triangles_encoded[i].param3 = QVector3D(m.sheen, m.sheenTint, m.clearcoat);
        triangles_encoded[i].param4 = QVector3D(m.clearcoatGloss, m.IOR, m.transmission);
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
    int nTriangles = triangles.size();
    for (int i = 0; i < nTriangles; i++) {       
        triangles_encoded[i].emissive = emissive;
        //triangles_encoded[i].baseColor = baseColor;
        triangles_encoded[i].param1 = QVector3D(subsurface, metallic, specular);
        triangles_encoded[i].param2 = QVector3D(specularTint, roughness, anisotropic);
        triangles_encoded[i].param3 = QVector3D(sheen, sheenTint, clearcoat);
        triangles_encoded[i].param4 = QVector3D(clearcoatGloss, IOR, transmission);
    }
}
