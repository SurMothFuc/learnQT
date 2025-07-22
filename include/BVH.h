#pragma once

#include <QVector3D>
#include "Mesh.h"

// BVH 树节点
struct BVHNode {
    int left, right;    // 左右子树索引
    int n, index;       // 叶子节点信息               
    QVector3D AA, BB;        // 碰撞盒
};

class BuildBVH
{
public:
    static int buildBVHwithSAH(std::vector<Triangle>& triangles, std::vector<BVHNode>& nodes, int l, int r, int n,int deep, int& max_deep);

    static bool cmpz(const Triangle& t1, const Triangle& t2);
    static bool cmpx(const Triangle& t1, const Triangle& t2);
    static bool cmpy(const Triangle& t1, const Triangle& t2);
};
