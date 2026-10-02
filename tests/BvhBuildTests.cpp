#include "BVH.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

static std::vector<Triangle> separatedClusters(float scale, float translation)
{
    std::vector<Triangle> triangles;
    for (int group = 0; group < 2; ++group)
        for (int i = 0; i < 16; ++i)
        {
            const QVector3D origin((i - 8) * .0625f, (group ? 10.f : -10.f) + i * .03125f,
                                   (i % 3) * .0625f);
            Triangle triangle;
            const QVector3D offset(translation, translation, translation);
            triangle.p1 = origin * scale + offset;
            triangle.p2 = (origin + QVector3D(.03125f, 0, .03125f)) * scale + offset;
            triangle.p3 = (origin + QVector3D(0, .03125f, .03125f)) * scale + offset;
            triangles.push_back(triangle);
        }
    return triangles;
}
int main()
{
    try
    {
        const struct { float scale, translation; } cases[] = {
            {1.f, 0.f}, {65536.f, 0.f}, {65536.f, 2147483648.f}, {std::ldexp(1.f, 70), 0.f}};
        for (const auto &test : cases)
        {
            auto triangles = separatedClusters(test.scale, test.translation);
            std::vector<BVHNode> nodes(1);
            int depth = 0;
            const int root = BuildBVH::buildBVHwithSAH(triangles, nodes, 0, int(triangles.size()) - 1, 8, 0, depth);
            const auto &left = nodes[nodes[root].left], &right = nodes[nodes[root].right];
            std::cout << "scale=" << test.scale << " translation=" << test.translation
                      << " nodes=" << nodes.size() << " depth=" << depth
                      << " leftY=" << left.AA.y() << ',' << left.BB.y()
                      << " rightY=" << right.AA.y() << ',' << right.BB.y() << '\n';
            if (!(left.BB.y() < test.translation && right.AA.y() > test.translation))
                throw std::runtime_error("SAH failed to separate disjoint clusters after changing model units");
        }
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
