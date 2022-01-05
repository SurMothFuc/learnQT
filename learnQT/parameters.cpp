#include "parameters.h"

Pass_parameters::Pass_parameters()
{
	camera = Camera(QVector3D(0.0f, 0.0f, 3.0f), QVector3D(0.0f, 1.0f, 0.0f));
    Material m;
    m.baseColor = QVector3D(1.0f, 1.0f, 1.0f);
    // readObj("models/model_o_from_paraview.obj", triangles, m, getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0.3, -1.3, 0), QVector3D(1.5, 1.5, 1.5)), true);
    readObj("models/model_o_from_paraview.obj", triangles, m, getTransformMatrix(QVector3D(0, 0, 0), QVector3D(-0.7, -0.2, 0), QVector3D(1.5, 1.5, 1.5)), true);

    m.baseColor = QVector3D(1, 1, 1);
    readObj("models/quad.obj", triangles, m, getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0, -1.4, 0), QVector3D(18.83, 0.01, 18.83)), false);
    m.baseColor = QVector3D(0.725, 0.71, 0.68);
    readObj("models/quad.obj", triangles, m, getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0, 1.381, 0), QVector3D(18.83, 0.01, 18.83)), false);

    m.baseColor = QVector3D(0.725, 0.71, 0.68);
    readObj("models/quad.obj", triangles, m, getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0, 0, -1.4), QVector3D(18.83, 18.83, 0.01)), false);

    m.baseColor = QVector3D(1.0, 0.0, 0.0);
    readObj("models/quad.obj", triangles, m, getTransformMatrix(QVector3D(0, 0, 0), QVector3D(-1.4, 0, 0), QVector3D(0.01, 18.83, 18.83)), false);

    m.baseColor = QVector3D(0.0, 1.0, 0.0);
    readObj("models/quad.obj", triangles, m, getTransformMatrix(QVector3D(0, 0, 0), QVector3D(1.4, 0, 0), QVector3D(0.01, 18.83, 18.83)), false);

    m.baseColor = QVector3D(1, 1, 1);
    m.emissive = QVector3D(20, 20, 20);
    readObj("models/quad.obj", triangles, m, getTransformMatrix(QVector3D(0, 0, 0), QVector3D(0.0, 1.38, -0.0), QVector3D(0.7, 0.01, 0.7)), false);

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







}

void Pass_parameters::readObj(std::string filepath, std::vector<Triangle>& triangles, Material material, QMatrix4x4 trans, bool smoothNormal)
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
        int v0, v1, v2;
        int vn0, vn1, vn2;
        int vt0, vt1, vt2;
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
QMatrix4x4 Pass_parameters::getTransformMatrix(QVector3D rotateCtrl, QVector3D translateCtrl, QVector3D scaleCtrl){
    QMatrix4x4 model;
    model.translate(translateCtrl);
    model.rotate(radians(rotateCtrl.x()), QVector3D(1.0f, 0.0f, 0.0f));
    model.rotate(radians(rotateCtrl.y()), QVector3D(0.0f, 1.0f, 0.0f));
    model.rotate(radians(rotateCtrl.z()), QVector3D(0.0f, 0.0f, 1.0f));
    model.scale(scaleCtrl);
    return model;
}