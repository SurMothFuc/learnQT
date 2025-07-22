#include "BVH.h"
#include "common.h"

int BuildBVH::buildBVHwithSAH(std::vector<Triangle>& triangles, std::vector<BVHNode>& nodes, int l, int r, int n,int deep,int& max_deep)
{
    if (l > r) return 0;
    if (deep > max_deep)
        max_deep = deep;
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
    int left = buildBVHwithSAH(triangles, nodes, l, Split, n,deep+1,max_deep);
    int right = buildBVHwithSAH(triangles, nodes, Split + 1, r, n, deep+1, max_deep);

    nodes[id].left = left;
    nodes[id].right = right;

    return id;
}

bool BuildBVH::cmpz(const Triangle& t1, const Triangle& t2)
{
    QVector3D center1 = (t1.p1 + t1.p2 + t1.p3) / QVector3D(3, 3, 3);
    QVector3D center2 = (t2.p1 + t2.p2 + t2.p3) / QVector3D(3, 3, 3);
    return center1.z() < center2.z();
}

bool BuildBVH::cmpx(const Triangle& t1, const Triangle& t2)
{
    QVector3D center1 = (t1.p1 + t1.p2 + t1.p3) / QVector3D(3, 3, 3);
    QVector3D center2 = (t2.p1 + t2.p2 + t2.p3) / QVector3D(3, 3, 3);
    return center1.x() < center2.x();
}

bool BuildBVH::cmpy(const Triangle& t1, const Triangle& t2)
{
    QVector3D center1 = (t1.p1 + t1.p2 + t1.p3) / QVector3D(3, 3, 3);
    QVector3D center2 = (t2.p1 + t2.p2 + t2.p3) / QVector3D(3, 3, 3);
    return center1.y() < center2.y();
}
