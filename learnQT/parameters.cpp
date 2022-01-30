#include "parameters.h"


Pass_parameters::Pass_parameters(){



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
    mt.baseColor = QVector3D(0.0f, 1.0f, 1.0f);
    readObj("models/sphere2.obj", triangles, mt, getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0,0, 0), QVector3D(1,1, 1)), true);
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
    std::cout << "ģ�Ͷ�ȡ���: �� " << nTriangles << " ��������" << std::endl;

    // ���� bvh
    BVHNode testNode;
    testNode.left = 255;
    testNode.right = 128;
    testNode.n = 30;
    testNode.AA = QVector3D(1, 1, 0);
    testNode.BB = QVector3D(0, 1, 0);
    nodes= std::vector<BVHNode>{ testNode };
    buildBVHwithSAH(triangles, nodes, 0, triangles.size() - 1, 8);
    int nNodes = nodes.size();
    std::cout << "BVH �������: �� " << nNodes << " ���ڵ�" << std::endl;
    //����bvh��Ҫ�������α���֮ǰ����Ϊbvh�Ĺ���ʹ��������

    triangles_encoded= std::vector<Triangle_encoded>(nTriangles);
    for (int i = 0; i < nTriangles; i++) {
        Triangle& t = triangles[i];
        Material& m = t.material;
        // ����λ��
        triangles_encoded[i].p1 = t.p1;
        triangles_encoded[i].p2 = t.p2;
        triangles_encoded[i].p3 = t.p3;
        // ���㷨��
        triangles_encoded[i].n1 = t.n1;
        triangles_encoded[i].n2 = t.n2;
        triangles_encoded[i].n3 = t.n3;
        // ����
        triangles_encoded[i].emissive = m.emissive;
        triangles_encoded[i].baseColor = m.baseColor;
        triangles_encoded[i].param1 = QVector3D(m.subsurface, m.metallic, m.specular);
        triangles_encoded[i].param2 = QVector3D(m.specularTint, m.roughness, m.anisotropic);
        triangles_encoded[i].param3 = QVector3D(m.sheen, m.sheenTint, m.clearcoat);
        triangles_encoded[i].param4 = QVector3D(m.clearcoatGloss, m.IOR, m.transmission);
    }

    // ���� BVHNode, aabb
    nodes_encoded= std::vector<BVHNode_encoded>(nNodes);
    for (int i = 0; i < nNodes; i++) {
        nodes_encoded[i].childs = QVector3D(nodes[i].left, nodes[i].right, 0);
        nodes_encoded[i].leafInfo = QVector3D(nodes[i].n, nodes[i].index, 0);
        nodes_encoded[i].AA = nodes[i].AA;
        nodes_encoded[i].BB = nodes[i].BB;
    }

    qDebug()<<"load HDRtexture:" << HDRLoader::load("./peppermint_powerplant_4k.hdr", hdrRes);

    // hdr ��Ҫ�Բ��� cache
    std::cout << "���� HDR ��ͼ��Ҫ�Բ��� Cache, ��ǰ�ֱ���: " << hdrRes.width << " " << hdrRes.height << std::endl;
    cache = calculateHdrCache(hdrRes.cols, hdrRes.width, hdrRes.height);
    hdrResolution = hdrRes.width;
}

void Pass_parameters::readObj(std::string filepath, std::vector<Triangle>& triangles, Material material, QMatrix4x4 trans, bool smoothNormal)
{
    // ����λ�ã�����
    std::vector<QVector3D> vertices;
    std::vector<unsigned int> indices;

    // ���ļ���
    std::ifstream fin(filepath);
    std::string line;
    if (!fin.is_open()) {
        std::cout << "�ļ� " << filepath << " ��ʧ��" << std::endl;
        exit(-1);
    }

    // ���� AABB �У���һ��ģ�ʹ�С
    float maxx = -11451419.19;
    float maxy = -11451419.19;
    float maxz = -11451419.19;
    float minx = 11451419.19;
    float miny = 11451419.19;
    float minz = 11451419.19;

    // ���ж�ȡ
    while (std::getline(fin, line)) {
        std::istringstream sins(line);   // ��һ�е�������Ϊ string stream �������Ҷ�ȡ
        std::string type;
        float x, y, z;
        int v0, v1, v2;
        int vn0, vn1, vn2;
        int vt0, vt1, vt2;
        char slash;

        // ͳ��б����Ŀ���ò�ͬ��ʽ��ȡ
        int slashCnt = 0;
        for (int i = 0; i < line.length(); i++) {
            if (line[i] == '/') slashCnt++;
        }

        // ��ȡobj�ļ�
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

    // ģ�ʹ�С��һ��
    float lenx = maxx - minx;
    float leny = maxy - miny;
    float lenz = maxz - minz;
    float maxaxis = std::max(lenx, std::max(leny, lenz));
    for (auto& v : vertices) {
        v /= maxaxis;
    }

    // ͨ�������������任
    for (auto& v : vertices) {
        QVector4D vv = QVector4D(v.x(), v.y(), v.z(), 1.0);
        vv = trans * vv;
        v = QVector3D(vv.x(), vv.y(), vv.z());
    }

    // ���ɷ���
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

    // ���� Triangle ��������
    int offset = triangles.size();  // ��������
    triangles.resize(offset + indices.size() / 3);
    for (int i = 0; i < indices.size(); i += 3) {
        Triangle& t = triangles[offset + i / 3];
        // ����������
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

        // ������
        t.material = material;
    }
}
QMatrix4x4 Pass_parameters::getTransformMatrix(QVector3D rotateCtrl, QVector3D translateCtrl, QVector3D scaleCtrl){
    QMatrix4x4 model;
    model.translate(translateCtrl);
    model.rotate(rotateCtrl.x(), QVector3D(1.0f, 0.0f, 0.0f));
    model.rotate(rotateCtrl.y(), QVector3D(0.0f, 1.0f, 0.0f));
    model.rotate(rotateCtrl.z(), QVector3D(0.0f, 0.0f, 1.0f));
    model.scale(scaleCtrl);
    return model;
}

// ������������������ -- �ȽϺ���
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

// SAH �Ż����� BVH
int buildBVHwithSAH(std::vector<Triangle>& triangles, std::vector<BVHNode>& nodes, int l, int r, int n) {
    if (l > r) return 0;

    nodes.push_back(BVHNode());
    int id = nodes.size() - 1;
    nodes[id].left = nodes[id].right = nodes[id].n = nodes[id].index = 0;
    nodes[id].AA = QVector3D(1145141919, 1145141919, 1145141919);
    nodes[id].BB = QVector3D(-1145141919, -1145141919, -1145141919);

    // ���� AABB
    for (int i = l; i <= r; i++) {
        // ��С�� AA
        float minx = std::min(triangles[i].p1.x(), std::min(triangles[i].p2.x(), triangles[i].p3.x()));
        float miny = std::min(triangles[i].p1.y(), std::min(triangles[i].p2.y(), triangles[i].p3.y()));
        float minz = std::min(triangles[i].p1.z(), std::min(triangles[i].p2.z(), triangles[i].p3.z()));
        nodes[id].AA[0] = std::min(nodes[id].AA.x(), minx);
        nodes[id].AA[1] = std::min(nodes[id].AA.y(), miny);
        nodes[id].AA[2] = std::min(nodes[id].AA.z(), minz);
        // ���� BB
        float maxx = std::max(triangles[i].p1.x(), std::max(triangles[i].p2.x(), triangles[i].p3.x()));
        float maxy = std::max(triangles[i].p1.y(), std::max(triangles[i].p2.y(), triangles[i].p3.y()));
        float maxz = std::max(triangles[i].p1.z(), std::max(triangles[i].p2.z(), triangles[i].p3.z()));
        nodes[id].BB[0] = std::max(nodes[id].BB.x(), maxx);
        nodes[id].BB[1] = std::max(nodes[id].BB.y(), maxy);
        nodes[id].BB[2] = std::max(nodes[id].BB.z(), maxz);
    }

    // ������ n �������� ����Ҷ�ӽڵ�
    if ((r - l + 1) <= n) {
        nodes[id].n = r - l + 1;
        nodes[id].index = l;
        return id;
    }

    // ����ݹ齨��
    float Cost = INF;
    int Axis = 0;
    int Split = (l + r) / 2;
    for (int axis = 0; axis < 3; axis++) {
        // �ֱ� x��y��z ������
        if (axis == 0) std::sort(&triangles[0] + l, &triangles[0] + r + 1, cmpx);
        if (axis == 1) std::sort(&triangles[0] + l, &triangles[0] + r + 1, cmpy);
        if (axis == 2) std::sort(&triangles[0] + l, &triangles[0] + r + 1, cmpz);

        // leftMax[i]: [l, i] ������ xyz ֵ
        // leftMin[i]: [l, i] ����С�� xyz ֵ
        std::vector<QVector3D> leftMax(r - l + 1, QVector3D(-INF, -INF, -INF));
        std::vector<QVector3D> leftMin(r - l + 1, QVector3D(INF, INF, INF));
        // ����ǰ׺ ע�� i-l �Զ��뵽�±� 0
        for (int i = l; i <= r; i++) {
            Triangle& t = triangles[i];
            int bias = (i == l) ? 0 : 1;  // ��һ��Ԫ�����⴦��

            leftMax[i - l][0] = std::max(leftMax[i - l - bias].x(), std::max(t.p1.x(), std::max(t.p2.x(), t.p3.x())));
            leftMax[i - l][1] = std::max(leftMax[i - l - bias].y(), std::max(t.p1.y(), std::max(t.p2.y(), t.p3.y())));
            leftMax[i - l][2] = std::max(leftMax[i - l - bias].z(), std::max(t.p1.z(), std::max(t.p2.z(), t.p3.z())));

            leftMin[i - l][0] = std::min(leftMin[i - l - bias].x(), std::min(t.p1.x(), std::min(t.p2.x(), t.p3.x())));
            leftMin[i - l][1] = std::min(leftMin[i - l - bias].y(), std::min(t.p1.y(), std::min(t.p2.y(), t.p3.y())));
            leftMin[i - l][2] = std::min(leftMin[i - l - bias].z(), std::min(t.p1.z(), std::min(t.p2.z(), t.p3.z())));
        }

        // rightMax[i]: [i, r] ������ xyz ֵ
        // rightMin[i]: [i, r] ����С�� xyz ֵ
        std::vector<QVector3D> rightMax(r - l + 1, QVector3D(-INF, -INF, -INF));
        std::vector<QVector3D> rightMin(r - l + 1, QVector3D(INF, INF, INF));
        // �����׺ ע�� i-l �Զ��뵽�±� 0
        for (int i = r; i >= l; i--) {
            Triangle& t = triangles[i];
            int bias = (i == r) ? 0 : 1;  // ��һ��Ԫ�����⴦��

            rightMax[i - l][0] = std::max(rightMax[i - l + bias].x(), std::max(t.p1.x(), std::max(t.p2.x(), t.p3.x())));
            rightMax[i - l][1] = std::max(rightMax[i - l + bias].y(), std::max(t.p1.y(), std::max(t.p2.y(), t.p3.y())));
            rightMax[i - l][2] = std::max(rightMax[i - l + bias].z(), std::max(t.p1.z(), std::max(t.p2.z(), t.p3.z())));

            rightMin[i - l][0] = std::min(rightMin[i - l + bias].x(), std::min(t.p1.x(), std::min(t.p2.x(), t.p3.x())));
            rightMin[i - l][1] = std::min(rightMin[i - l + bias].y(), std::min(t.p1.y(), std::min(t.p2.y(), t.p3.y())));
            rightMin[i - l][2] = std::min(rightMin[i - l + bias].z(), std::min(t.p1.z(), std::min(t.p2.z(), t.p3.z())));
        }

        // ����Ѱ�ҷָ�
        float cost = INF;
        int split = l;
        for (int i = l; i <= r - 1; i++) {
            float lenx, leny, lenz;
            // ��� [l, i]
            QVector3D leftAA = leftMin[i - l];
            QVector3D leftBB = leftMax[i - l];
            lenx = leftBB.x() - leftAA.x();
            leny = leftBB.y() - leftAA.y();
            lenz = leftBB.z() - leftAA.z();
            float leftS = 2.0 * ((lenx * leny) + (lenx * lenz) + (leny * lenz));
            float leftCost = leftS * (i - l + 1);

            // �Ҳ� [i+1, r]
            QVector3D rightAA = rightMin[i + 1 - l];
            QVector3D rightBB = rightMax[i + 1 - l];
            lenx = rightBB.x() - rightAA.x();
            leny = rightBB.y() - rightAA.y();
            lenz = rightBB.z() - rightAA.z();
            float rightS = 2.0 * ((lenx * leny) + (lenx * lenz) + (leny * lenz));
            float rightCost = rightS * (r - i);

            // ��¼ÿ���ָ����С��
            float totalCost = leftCost + rightCost;
            if (totalCost < cost) {
                cost = totalCost;
                split = i;
            }
        }
        // ��¼ÿ�������Ѵ�
        if (cost < Cost) {
            Cost = cost;
            Axis = axis;
            Split = split;
        }
    }

    // �������ָ�
    if (Axis == 0) std::sort(&triangles[0] + l, &triangles[0] + r + 1, cmpx);
    if (Axis == 1) std::sort(&triangles[0] + l, &triangles[0] + r + 1, cmpy);
    if (Axis == 2) std::sort(&triangles[0] + l, &triangles[0] + r + 1, cmpz);

    // �ݹ�
    int left = buildBVHwithSAH(triangles, nodes, l, Split, n);
    int right = buildBVHwithSAH(triangles, nodes, Split + 1, r, n);

    nodes[id].left = left;
    nodes[id].right = right;

    return id;
}
// ���� HDR ��ͼ��ػ�����Ϣ
float* calculateHdrCache(float* HDR, int width, int height) {

    float lumSum = 0.0;

    // ��ʼ�� h �� w �еĸ����ܶ� pdf �� ͳ��������
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

    // �����ܶȹ�һ��
    for (int i = 0; i < height; i++)
        for (int j = 0; j < width; j++)
            pdf[i][j] /= lumSum;

    // �ۼ�ÿһ�еõ� x �ı�Ե�����ܶ�
    std::vector<float> pdf_x_margin;
    pdf_x_margin.resize(width);
    for (int j = 0; j < width; j++)
        for (int i = 0; i < height; i++)
            pdf_x_margin[j] += pdf[i][j];

    // ���� x �ı�Ե�ֲ�����
    std::vector<float> cdf_x_margin = pdf_x_margin;
    for (int i = 1; i < width; i++)
        cdf_x_margin[i] += cdf_x_margin[i - 1];

    // ���� y �� X=x �µ����������ܶȺ���
    std::vector<std::vector<float>> pdf_y_condiciton = pdf;
    for (int j = 0; j < width; j++)
        for (int i = 0; i < height; i++)
            pdf_y_condiciton[i][j] /= pdf_x_margin[j];

    // ���� y �� X=x �µ��������ʷֲ�����
    std::vector<std::vector<float>> cdf_y_condiciton = pdf_y_condiciton;
    for (int j = 0; j < width; j++)
        for (int i = 1; i < height; i++)
            cdf_y_condiciton[i][j] += cdf_y_condiciton[i - 1][j];

    // cdf_y_condiciton ת��Ϊ���д洢
    // cdf_y_condiciton[i] ��ʾ y �� X=i �µ��������ʷֲ�����
    std::vector<std::vector<float>> temp = cdf_y_condiciton;
    cdf_y_condiciton = std::vector<std::vector<float>>(width);
    for (auto& line : cdf_y_condiciton) line.resize(height);
    for (int j = 0; j < width; j++)
        for (int i = 0; i < height; i++)
            cdf_y_condiciton[j][i] = temp[i][j];

    // ��� xi_1, xi_2 Ԥ�������� xy
    // sample_x[i][j] ��ʾ xi_1=i/height, xi_2=j/width ʱ (x,y) �е� x
    // sample_y[i][j] ��ʾ xi_1=i/height, xi_2=j/width ʱ (x,y) �е� y
    // sample_p[i][j] ��ʾȡ (i, j) ��ʱ�ĸ����ܶ�
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

            // �� xi_1 �� cdf_x_margin �� lower bound �õ����� x
            int x = std::lower_bound(cdf_x_margin.begin(), cdf_x_margin.end(), xi_1) - cdf_x_margin.begin();
            // �� xi_2 �� X=x ������µõ����� y
            int y = std::lower_bound(cdf_y_condiciton[x].begin(), cdf_y_condiciton[x].end(), xi_2) - cdf_y_condiciton[x].begin();

            // �洢�������� xy �� xy λ�ö�Ӧ�ĸ����ܶ�
            sample_x[i][j] = float(x) / width;
            sample_y[i][j] = float(y) / height;
            sample_p[i][j] = pdf[i][j];

        }
    }

    // ���Ͻ��������
    // R,G ͨ���洢���� (x,y) �� B ͨ���洢 pdf(i, j)
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

void Pass_parameters::updateMaterial(QVector3D emissive, QVector3D  baseColor,
    float subsurface, float  metallic, float  specular,
    float specularTint, float roughness, float anisotropic,
    float sheen, float sheenTint, float clearcoat, 
    float clearcoatGloss, float IOR, float transmission)
{
    int nTriangles = triangles.size();
    for (int i = 0; i < nTriangles; i++) {       
        triangles_encoded[i].emissive = emissive;
        triangles_encoded[i].baseColor = baseColor;
        triangles_encoded[i].param1 = QVector3D(subsurface, metallic, specular);
        triangles_encoded[i].param2 = QVector3D(specularTint, roughness, anisotropic);
        triangles_encoded[i].param3 = QVector3D(sheen, sheenTint, clearcoat);
        triangles_encoded[i].param4 = QVector3D(clearcoatGloss, IOR, transmission);
    }
}
