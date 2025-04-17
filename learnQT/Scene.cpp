#include "Scene.h"


Scene::Scene(){

    camera = Camera(QVector3D(0.0f, 0.0f, 1.48f), QVector3D(0.0f, 1.0f, 0.0f));
    Material mt;
    /*mt.roughness = 0.1;
    mt.specular = 1;
    mt.metallic = 0.0;
    mt.subsurface = 1.0;
    mt.baseColor = QVector3D(0.3f, 1.0f, 0.5f);
    readObj("models/Stanford Bunny.obj", triangles, mt, getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0.2, -0.5, 0), QVector3D(1, 1, 1)), true);*/
    //m.emissive = QVector3D(10, 10, 10);
    mt = Material();
    mt.roughness = 0.1;
    mt.specular = 1.0;
    mt.subsurface = 1.0;
    //mt.clearcoat = 1.0;
   // mt.metallic = 0.9;
   // mt.roughness = 0.5;
    //mt.anisotropic = 1.0;
    mt.baseColor = QVector3D(1.0f, 0.0f, 1.0f);
   // readObj("models/sphere2.obj", triangles, mt, getTransformMatrix(QVector3D(0, 0, 0), QVector3D(-1.3, 0.0, 0), QVector3D(1,1, 1)), true);
    
    mt = Material();
    //mt.roughness = 0.1;
   // mt.specular = 1.0;
    //mt.subsurface = 1.0;
   // mt.clearcoat = 1.0;
   // mt.metallic =0.8;
   // mt.roughness = 0.5;
    //mt.specular = 1.0;
    //mt.anisotropic = 1.0;
    mt.baseColor = QVector3D(1.0, 1.0, 1.0);
    readObj("models/10778_Toilet_V2.obj", triangles, mt, getTransformMatrix(QVector3D(-90.0, 0, 0), QVector3D(0.3,0, 0), QVector3D(1,1, 1)), true);
   // readObj("models/Stanford Bunny.obj", triangles, mt, getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0.2, -0.7, 0), QVector3D(1,1, 1)), true);
    
    mt = Material();
    mt.roughness = 0.1;
    mt.specular = 1.0;
    mt.subsurface = 1.0;
   // mt.clearcoat = 1.0;
    //mt.metallic = 0.7;
   // mt.roughness = 0.5;
    //mt.anisotropic = 1.0;
    mt.baseColor = QVector3D(1.0f, 1.0f, 0.0f);
    //readObj("models/sphere2.obj", triangles, mt, getTransformMatrix(QVector3D(0, 0, 0), QVector3D(1.3, 0.0, 0), QVector3D(1,1, 1)), true);
   // m.emissive = QVector3D(0, 0, 0);
   // m.baseColor = QVector3D(0.0f, 1.0f, 1.0f);
   // readObj("models/hexagon+sphere.obj", triangles, m, getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0.0, -0.45, 0), QVector3D(1, 1, 1)), false);

    mt = Material();
    //mt.clearcoat = 1.0;
   // mt.clearcoatGloss = 1.0;
   // mt.metallic = 0.5;
    mt.roughness = 0.1;
    mt.specular = 1.0;
    mt.subsurface = 1.0;
    //mt.anisotropic = 1.0;
    mt.baseColor = QVector3D(0.725, 0.71, 0.68);
    //readObj("models/quad.obj", triangles, mt, getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0, -0.7, 0), QVector3D(18.83, 0.01, 18.83)), false);

     mt = Material();
     mt.baseColor = QVector3D(1, 1, 1);
     mt.emissive = QVector3D(10, 10, 10);
     //readObj("models/quad.obj", triangles, mt, getTransformMatrix(QVector3D(0.0, 0.0, 0), QVector3D(0.0, 1.5, 0), QVector3D(0.7, 0.01, 0.7)), false);


   // mt = Material();
  //  mt.baseColor = QVector3D(1, 1, 1);
  //  mt.emissive = QVector3D(10, 10, 10);
   // readObj("models/quad.obj", triangles, mt, getTransformMatrix(QVector3D(45.0, 0.0, 0), QVector3D(0.0, 1.5, 1.5), QVector3D(5, 0.01, 0.7)), false);

   // mt = Material();
   // mt.baseColor = QVector3D(1, 1, 1);
   // mt.emissive = QVector3D(10, 10, 10);
   // readObj("models/quad.obj", triangles, mt, getTransformMatrix(QVector3D(-45.0, 0.0, 0), QVector3D(0.0, 1.5, -1.5), QVector3D(5, 0.01, 0.7)), false);
    mt = Material();
    mt.baseColor = QVector3D(0.725, 0.71, 0.68);
    //readObj("models/quad.obj", triangles, mt, getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0, 1.51, 0), QVector3D(3.0, 0.01, 3.0)), false);
   
    mt = Material();
    mt.baseColor = QVector3D(0.725, 0.71, 0.68);
    //readObj("models/quad.obj", triangles, mt, getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0, 0, -1.4), QVector3D(3.0, 3.0, 0.01)), false);
   
    mt = Material();
    mt.baseColor = QVector3D(1, 0, 0);
   // readObj("models/quad.obj", triangles, mt, getTransformMatrix(QVector3D(0, 0, 0), QVector3D(-1.4, 0, 0), QVector3D(0.01, 3.0, 3.0)), false);
   
    mt = Material();
    mt.baseColor = QVector3D(0, 1,0);
   // readObj("models/quad.obj", triangles, mt, getTransformMatrix(QVector3D(0, 0, 0), QVector3D(1.4, 0, 0), QVector3D(0.01, 3.0, 3.0)), false);
    /*m.emissive = QVector3D(0,0,0);
    m.baseColor = QVector3D(0.725, 0.71, 0.68);
    readObj("models/quad.obj", triangles, m, getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0, 1.381, 0), QVector3D(18.83, 0.01, 18.83)), false);

    m.baseColor = QVector3D(0.725, 0.71, 0.68);
    readObj("models/quad.obj", triangles, m, getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0, 0, -1.4), QVector3D(18.83, 18.83, 0.01)), false);

    m.baseColor = QVector3D(1.0, 0.0, 0.0);
    readObj("models/quad.obj", triangles, m, getTransformMatrix(QVector3D(0, 0, 0), QVector3D(-1.4, 0, 0), QVector3D(0.01, 18.83, 18.83)), false);

    m.baseColor = QVector3D(0.0, 1.0, 0.0);
    readObj("models/quad.obj", triangles, m, getTransformMatrix(QVector3D(0, 0, 0), QVector3D(1.4, 0, 0), QVector3D(0.01, 18.83, 18.83)), false);*/

   
    /* m.baseColor = QVector3D(1, 1, 1);
     m.emissive = QVector3D(0, 20,0);
     readObj("models/sphere.obj", triangles, m, getTransformMatrix(QVector3D(0, 0, 0), QVector3D(1, 0.7, 0.5), QVector3D(0.5, 0.5, 0.5)), false);
     m.baseColor = QVector3D(1, 1, 1);
     m.emissive = QVector3D(20, 0, 0);
     readObj("models/sphere.obj", triangles, m, getTransformMatrix(QVector3D(0, 0, 0), QVector3D(-1.0, 0.7,0.5), QVector3D(0.5, 0.5, 0.5)), false);
     m.baseColor = QVector3D(1, 1, 1);
     m.emissive = QVector3D(0, 0, 20);
     readObj("models/sphere.obj", triangles, m, getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0, 0.7, 0.5), QVector3D(0.5, 0.5, 0.5)), false);*/

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
    buildBVHwithSAH(triangles, nodes, 0, triangles.size() - 1, 8);
    int nNodes = nodes.size();
    std::cout << "BVH 建立完成: 共 " << nNodes << " 个节点" << std::endl;
    //建立bvh需要在三角形编码之前，因为bvh的构建使用了排序

    triangles_encoded= std::vector<Triangle_encoded>(nTriangles);
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
    nodes_encoded= std::vector<BVHNode_encoded>(nNodes);
    for (int i = 0; i < nNodes; i++) {
        nodes_encoded[i].childs = QVector3D(nodes[i].left, nodes[i].right, 0);
        nodes_encoded[i].leafInfo = QVector3D(nodes[i].n, nodes[i].index, 0);
        nodes_encoded[i].AA = nodes[i].AA;
        nodes_encoded[i].BB = nodes[i].BB;
    }

    qDebug()<<"load HDRtexture:" << HDRLoader::load("./peppermint_powerplant_4k.hdr", hdrRes);

    // hdr 重要性采样 cache
    std::cout << "计算 HDR 贴图重要性采样 Cache, 当前分辨率: " << hdrRes.width << " " << hdrRes.height << std::endl;
    cache = calculateHdrCache(hdrRes.cols, hdrRes.width, hdrRes.height);
    hdrResolution = hdrRes.width;
}

void Scene::readObj(std::string filepath, std::vector<Triangle>& triangles, Material material, QMatrix4x4 trans, bool smoothNormal)
{
    // 顶点位置，索引
    std::vector<QVector3D> vertices;
    std::vector<unsigned int> indices;

    // 打开文件流
    std::ifstream fin(filepath);
    std::string line;
    if (!fin.is_open()) {
        std::cout << "文件 " << filepath << " 打开失败" << std::endl;
        exit(-1);
    }

    // 计算 AABB 盒，归一化模型大小
    float maxx = -11451419.19;
    float maxy = -11451419.19;
    float maxz = -11451419.19;
    float minx = 11451419.19;
    float miny = 11451419.19;
    float minz = 11451419.19;

    // 按行读取
    while (std::getline(fin, line)) {
        std::istringstream sins(line);   // 以一行的数据作为 string stream 解析并且读取
        std::string type;
        float x, y, z;
        int v0, v1, v2,v3;
        int vn0, vn1, vn2,  vn3;
        int vt0, vt1, vt2, vt3;
        char slash;

        // 统计斜杆数目，用不同格式读取
        int slashCnt = 0;
        for (int i = 0; i < line.length(); i++) {
            if (line[i] == '/') slashCnt++;
        }

        // 读取obj文件
        sins >> type;
        if (type == "v") {
            sins >> x >> y >> z;
            vertices.push_back(QVector3D(x, y, z));
            maxx = std::max(maxx, x); maxy = std::max(maxx, y); maxz = std::max(maxx, z);
            minx = std::min(minx, x); miny = std::min(minx, y); minz = std::min(minx, z);
        }
        if (type == "f") {
            if (slashCnt == 6) {
                sins >> v0 >> slash >> vt0 >> slash >> vn0;
                sins >> v1 >> slash >> vt1 >> slash >> vn1;
                sins >> v2 >> slash >> vt2 >> slash >> vn2;
            }
            else if (slashCnt == 8) {
                sins >> v0 >> slash >> vt0 >> slash >> vn0 ;
                sins >> v1 >> slash >> vt1 >> slash >> vn1 ;
                sins >> v2 >> slash >> vt2 >> slash >> vn2 ;
                sins >> v3 >> slash >> vt3 >> slash >> vn3;
                indices.push_back(v2 - 1);
                indices.push_back(v3 - 1);
                indices.push_back(v0 - 1);
            }
            else if (slashCnt == 3) {
                sins >> v0 >> slash >> vt0;
                sins >> v1 >> slash >> vt1;
                sins >> v2 >> slash >> vt2;
            }
            else {
                sins >> v0 >> v1 >> v2;
            }
            indices.push_back(v0 - 1);
            indices.push_back(v1 - 1);
            indices.push_back(v2 - 1);
        }
    }

    // 模型大小归一化
    float lenx = maxx - minx;
    float leny = maxy - miny;
    float lenz = maxz - minz;
    float maxaxis = std::max(lenx, std::max(leny, lenz));
    for (auto& v : vertices) {
        v /= maxaxis;
    }

    // 通过矩阵进行坐标变换
    for (auto& v : vertices) {
        QVector4D vv = QVector4D(v.x(), v.y(), v.z(), 1.0);
        vv = trans * vv;
        v = QVector3D(vv.x(), vv.y(), vv.z());
    }

    // 生成法线
    std::vector<QVector3D> normals(vertices.size(), QVector3D(0, 0, 0));
    for (int i = 0; i < indices.size(); i += 3) {
        QVector3D p1 = vertices[indices[i]];
        QVector3D p2 = vertices[indices[i + 1]];
        QVector3D p3 = vertices[indices[i + 2]];
        QVector3D n = (QVector3D::crossProduct(p2 - p1, p3 - p1)).normalized();
        normals[indices[i]] += n;
        normals[indices[i + 1]] += n;
        normals[indices[i + 2]] += n;
    }

    // 构建 Triangle 对象数组
    int offset = triangles.size();  // 增量更新
    triangles.resize(offset + indices.size() / 3);
    for (int i = 0; i < indices.size(); i += 3) {
        Triangle& t = triangles[offset + i / 3];
        // 传顶点属性
        t.p1 = vertices[indices[i]];
        t.p2 = vertices[indices[i + 1]];
        t.p3 = vertices[indices[i + 2]];
        if (!smoothNormal) {
            QVector3D n = (QVector3D::crossProduct(t.p2 - t.p1, t.p3 - t.p1)).normalized();
            t.n1 = n; t.n2 = n; t.n3 = n;
        }
        else {
            t.n1 =(normals[indices[i]]).normalized();
            t.n2 =(normals[indices[i + 1]]).normalized();
            t.n3 = (normals[indices[i + 2]]).normalized();
        }

        // 传材质
        t.material = material;
    }
}
QMatrix4x4 Scene::getTransformMatrix(QVector3D rotateCtrl, QVector3D translateCtrl, QVector3D scaleCtrl){
    QMatrix4x4 model;
    model.translate(translateCtrl);
    model.rotate(rotateCtrl.x(), QVector3D(1.0f, 0.0f, 0.0f));
    model.rotate(rotateCtrl.y(), QVector3D(0.0f, 1.0f, 0.0f));
    model.rotate(rotateCtrl.z(), QVector3D(0.0f, 0.0f, 1.0f));
    model.scale(scaleCtrl);
    return model;
}

// 按照三角形中心排序 -- 比较函数
bool cmpx(const Triangle& t1, const Triangle& t2) {
    QVector3D center1 = (t1.p1 + t1.p2 + t1.p3) / QVector3D(3, 3, 3);
    QVector3D center2 = (t2.p1 + t2.p2 + t2.p3) / QVector3D(3, 3, 3);
    return center1.x() < center2.x();
}
bool cmpy(const Triangle& t1, const Triangle& t2) {
    QVector3D center1 = (t1.p1 + t1.p2 + t1.p3) / QVector3D(3, 3, 3);
    QVector3D center2 = (t2.p1 + t2.p2 + t2.p3) / QVector3D(3, 3, 3);
    return center1.y() < center2.y();
}
bool cmpz(const Triangle& t1, const Triangle& t2) {
    QVector3D center1 = (t1.p1 + t1.p2 + t1.p3) / QVector3D(3, 3, 3);
    QVector3D center2 = (t2.p1 + t2.p2 + t2.p3) / QVector3D(3, 3, 3);
    return center1.z() < center2.z();
}

// SAH 优化构建 BVH
int buildBVHwithSAH(std::vector<Triangle>& triangles, std::vector<BVHNode>& nodes, int l, int r, int n) {
    if (l > r) return 0;

    nodes.push_back(BVHNode());
    int id = nodes.size() - 1;
    nodes[id].left = nodes[id].right = nodes[id].n = nodes[id].index = 0;
    nodes[id].AA = QVector3D(1145141919, 1145141919, 1145141919);
    nodes[id].BB = QVector3D(-1145141919, -1145141919, -1145141919);

    // 计算 AABB
    for (int i = l; i <= r; i++) {
        // 最小点 AA
        float minx = std::min(triangles[i].p1.x(), std::min(triangles[i].p2.x(), triangles[i].p3.x()));
        float miny = std::min(triangles[i].p1.y(), std::min(triangles[i].p2.y(), triangles[i].p3.y()));
        float minz = std::min(triangles[i].p1.z(), std::min(triangles[i].p2.z(), triangles[i].p3.z()));
        nodes[id].AA[0] = std::min(nodes[id].AA.x(), minx);
        nodes[id].AA[1] = std::min(nodes[id].AA.y(), miny);
        nodes[id].AA[2] = std::min(nodes[id].AA.z(), minz);
        // 最大点 BB
        float maxx = std::max(triangles[i].p1.x(), std::max(triangles[i].p2.x(), triangles[i].p3.x()));
        float maxy = std::max(triangles[i].p1.y(), std::max(triangles[i].p2.y(), triangles[i].p3.y()));
        float maxz = std::max(triangles[i].p1.z(), std::max(triangles[i].p2.z(), triangles[i].p3.z()));
        nodes[id].BB[0] = std::max(nodes[id].BB.x(), maxx);
        nodes[id].BB[1] = std::max(nodes[id].BB.y(), maxy);
        nodes[id].BB[2] = std::max(nodes[id].BB.z(), maxz);
    }

    // 不多于 n 个三角形 返回叶子节点
    if ((r - l + 1) <= n) {
        nodes[id].n = r - l + 1;
        nodes[id].index = l;
        return id;
    }

    // 否则递归建树
    float Cost = INF;
    int Axis = 0;
    int Split = (l + r) / 2;
    for (int axis = 0; axis < 3; axis++) {
        // 分别按 x，y，z 轴排序
        if (axis == 0) std::sort(&triangles[0] + l, &triangles[0] + r + 1, cmpx);
        if (axis == 1) std::sort(&triangles[0] + l, &triangles[0] + r + 1, cmpy);
        if (axis == 2) std::sort(&triangles[0] + l, &triangles[0] + r + 1, cmpz);

        // leftMax[i]: [l, i] 中最大的 xyz 值
        // leftMin[i]: [l, i] 中最小的 xyz 值
        std::vector<QVector3D> leftMax(r - l + 1, QVector3D(-INF, -INF, -INF));
        std::vector<QVector3D> leftMin(r - l + 1, QVector3D(INF, INF, INF));
        // 计算前缀 注意 i-l 以对齐到下标 0
        for (int i = l; i <= r; i++) {
            Triangle& t = triangles[i];
            int bias = (i == l) ? 0 : 1;  // 第一个元素特殊处理

            leftMax[i - l][0] = std::max(leftMax[i - l - bias].x(), std::max(t.p1.x(), std::max(t.p2.x(), t.p3.x())));
            leftMax[i - l][1] = std::max(leftMax[i - l - bias].y(), std::max(t.p1.y(), std::max(t.p2.y(), t.p3.y())));
            leftMax[i - l][2] = std::max(leftMax[i - l - bias].z(), std::max(t.p1.z(), std::max(t.p2.z(), t.p3.z())));

            leftMin[i - l][0] = std::min(leftMin[i - l - bias].x(), std::min(t.p1.x(), std::min(t.p2.x(), t.p3.x())));
            leftMin[i - l][1] = std::min(leftMin[i - l - bias].y(), std::min(t.p1.y(), std::min(t.p2.y(), t.p3.y())));
            leftMin[i - l][2] = std::min(leftMin[i - l - bias].z(), std::min(t.p1.z(), std::min(t.p2.z(), t.p3.z())));
        }

        // rightMax[i]: [i, r] 中最大的 xyz 值
        // rightMin[i]: [i, r] 中最小的 xyz 值
        std::vector<QVector3D> rightMax(r - l + 1, QVector3D(-INF, -INF, -INF));
        std::vector<QVector3D> rightMin(r - l + 1, QVector3D(INF, INF, INF));
        // 计算后缀 注意 i-l 以对齐到下标 0
        for (int i = r; i >= l; i--) {
            Triangle& t = triangles[i];
            int bias = (i == r) ? 0 : 1;  // 第一个元素特殊处理

            rightMax[i - l][0] = std::max(rightMax[i - l + bias].x(), std::max(t.p1.x(), std::max(t.p2.x(), t.p3.x())));
            rightMax[i - l][1] = std::max(rightMax[i - l + bias].y(), std::max(t.p1.y(), std::max(t.p2.y(), t.p3.y())));
            rightMax[i - l][2] = std::max(rightMax[i - l + bias].z(), std::max(t.p1.z(), std::max(t.p2.z(), t.p3.z())));

            rightMin[i - l][0] = std::min(rightMin[i - l + bias].x(), std::min(t.p1.x(), std::min(t.p2.x(), t.p3.x())));
            rightMin[i - l][1] = std::min(rightMin[i - l + bias].y(), std::min(t.p1.y(), std::min(t.p2.y(), t.p3.y())));
            rightMin[i - l][2] = std::min(rightMin[i - l + bias].z(), std::min(t.p1.z(), std::min(t.p2.z(), t.p3.z())));
        }

        // 遍历寻找分割
        float cost = INF;
        int split = l;
        for (int i = l; i <= r - 1; i++) {
            float lenx, leny, lenz;
            // 左侧 [l, i]
            QVector3D leftAA = leftMin[i - l];
            QVector3D leftBB = leftMax[i - l];
            lenx = leftBB.x() - leftAA.x();
            leny = leftBB.y() - leftAA.y();
            lenz = leftBB.z() - leftAA.z();
            float leftS = 2.0 * ((lenx * leny) + (lenx * lenz) + (leny * lenz));
            float leftCost = leftS * (i - l + 1);

            // 右侧 [i+1, r]
            QVector3D rightAA = rightMin[i + 1 - l];
            QVector3D rightBB = rightMax[i + 1 - l];
            lenx = rightBB.x() - rightAA.x();
            leny = rightBB.y() - rightAA.y();
            lenz = rightBB.z() - rightAA.z();
            float rightS = 2.0 * ((lenx * leny) + (lenx * lenz) + (leny * lenz));
            float rightCost = rightS * (r - i);

            // 记录每个分割的最小答案
            float totalCost = leftCost + rightCost;
            if (totalCost < cost) {
                cost = totalCost;
                split = i;
            }
        }
        // 记录每个轴的最佳答案
        if (cost < Cost) {
            Cost = cost;
            Axis = axis;
            Split = split;
        }
    }

    // 按最佳轴分割
    if (Axis == 0) std::sort(&triangles[0] + l, &triangles[0] + r + 1, cmpx);
    if (Axis == 1) std::sort(&triangles[0] + l, &triangles[0] + r + 1, cmpy);
    if (Axis == 2) std::sort(&triangles[0] + l, &triangles[0] + r + 1, cmpz);

    // 递归
    int left = buildBVHwithSAH(triangles, nodes, l, Split, n);
    int right = buildBVHwithSAH(triangles, nodes, Split + 1, r, n);

    nodes[id].left = left;
    nodes[id].right = right;

    return id;
}
// 计算 HDR 贴图相关缓存信息
float* calculateHdrCache(float* HDR, int width, int height) {

    float lumSum = 0.0;

    // 初始化 h 行 w 列的概率密度 pdf 并 统计总亮度
    std::vector<std::vector<float>> pdf(height);
    for (auto& line : pdf) line.resize(width);
    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            float R = HDR[3 * (i * width + j)];
            float G = HDR[3 * (i * width + j) + 1];
            float B = HDR[3 * (i * width + j) + 2];
            float lum = 0.2 * R + 0.7 * G + 0.1 * B;
            pdf[i][j] = lum;
            lumSum += lum;
        }
    }

    // 概率密度归一化
    for (int i = 0; i < height; i++)
        for (int j = 0; j < width; j++)
            pdf[i][j] /= lumSum;

    // 累加每一列得到 x 的边缘概率密度
    std::vector<float> pdf_x_margin;
    pdf_x_margin.resize(width);
    for (int j = 0; j < width; j++)
        for (int i = 0; i < height; i++)
            pdf_x_margin[j] += pdf[i][j];

    // 计算 x 的边缘分布函数
    std::vector<float> cdf_x_margin = pdf_x_margin;
    for (int i = 1; i < width; i++)
        cdf_x_margin[i] += cdf_x_margin[i - 1];

    // 计算 y 在 X=x 下的条件概率密度函数
    std::vector<std::vector<float>> pdf_y_condiciton = pdf;
    for (int j = 0; j < width; j++)
        for (int i = 0; i < height; i++)
            pdf_y_condiciton[i][j] /= pdf_x_margin[j];

    // 计算 y 在 X=x 下的条件概率分布函数
    std::vector<std::vector<float>> cdf_y_condiciton = pdf_y_condiciton;
    for (int j = 0; j < width; j++)
        for (int i = 1; i < height; i++)
            cdf_y_condiciton[i][j] += cdf_y_condiciton[i - 1][j];

    // cdf_y_condiciton 转置为按列存储
    // cdf_y_condiciton[i] 表示 y 在 X=i 下的条件概率分布函数
    std::vector<std::vector<float>> temp = cdf_y_condiciton;
    cdf_y_condiciton = std::vector<std::vector<float>>(width);
    for (auto& line : cdf_y_condiciton) line.resize(height);
    for (int j = 0; j < width; j++)
        for (int i = 0; i < height; i++)
            cdf_y_condiciton[j][i] = temp[i][j];

    // 穷举 xi_1, xi_2 预计算样本 xy
    // sample_x[i][j] 表示 xi_1=i/height, xi_2=j/width 时 (x,y) 中的 x
    // sample_y[i][j] 表示 xi_1=i/height, xi_2=j/width 时 (x,y) 中的 y
    // sample_p[i][j] 表示取 (i, j) 点时的概率密度
    std::vector<std::vector<float>> sample_x(height);
    for (auto& line : sample_x) line.resize(width);
    std::vector<std::vector<float>> sample_y(height);
    for (auto& line : sample_y) line.resize(width);
    std::vector<std::vector<float>> sample_p(height);
    for (auto& line : sample_p) line.resize(width);
    for (int j = 0; j < width; j++) {
        for (int i = 0; i < height; i++) {
            float xi_1 = float(i) / height;
            float xi_2 = float(j) / width;

            // 用 xi_1 在 cdf_x_margin 中 lower bound 得到样本 x
            int x = std::lower_bound(cdf_x_margin.begin(), cdf_x_margin.end(), xi_1) - cdf_x_margin.begin();
            // 用 xi_2 在 X=x 的情况下得到样本 y
            int y = std::lower_bound(cdf_y_condiciton[x].begin(), cdf_y_condiciton[x].end(), xi_2) - cdf_y_condiciton[x].begin();

            // 存储纹理坐标 xy 和 xy 位置对应的概率密度
            sample_x[i][j] = float(x) / width;
            sample_y[i][j] = float(y) / height;
            sample_p[i][j] = pdf[i][j];

        }
    }

    // 整合结果到纹理
    // R,G 通道存储样本 (x,y) 而 B 通道存储 pdf(i, j)
    float* cache = new float[width * height * 3];
    //for (int i = 0; i < width * height * 3; i++) cache[i] = 0.0;

    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            cache[3 * (i * width + j)] = sample_x[i][j];        // R
            cache[3 * (i * width + j) + 1] = sample_y[i][j];    // G
            cache[3 * (i * width + j) + 2] = sample_p[i][j];    // B
        }
    }

    return cache;
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
