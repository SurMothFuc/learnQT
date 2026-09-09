#include "SceneGraph.h"
#include <QElapsedTimer>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>

namespace
{
double surfaceArea(const QVector3D &minimum, const QVector3D &maximum)
{
    const auto extent = maximum - minimum;
    return 2.0 * (double(extent.x()) * extent.y() + double(extent.x()) * extent.z() +
                  double(extent.y()) * extent.z());
}
} // namespace

QMatrix4x4 sceneMatrix(const QJsonValue &value)
{
    QMatrix4x4 result;
    const auto values = value.toArray();
    if (values.size() == 16)
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                result(r, c) = values[r * 4 + c].toDouble();
    return result;
}
QJsonArray sceneMatrixJson(const QMatrix4x4 &matrix)
{
    QJsonArray result;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            result.append(matrix(r, c));
    return result;
}
QString sceneId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
void SceneBounds::include(const QVector3D &p)
{
    if (!valid)
    {
        minimum = maximum = p;
        valid = true;
        return;
    }
    for (int k = 0; k < 3; ++k)
    {
        minimum[k] = std::min(minimum[k], p[k]);
        maximum[k] = std::max(maximum[k], p[k]);
    }
}
void SceneBounds::include(const SceneBounds &other)
{
    if (other.valid)
    {
        include(other.minimum);
        include(other.maximum);
    }
}
SceneBounds SceneBounds::transformed(const QMatrix4x4 &matrix) const
{
    SceneBounds result;
    if (valid)
        for (int mask = 0; mask < 8; ++mask)
            result.include(matrix.map(QVector3D(mask & 1 ? maximum.x() : minimum.x(),
                                                mask & 2 ? maximum.y() : minimum.y(),
                                                mask & 4 ? maximum.z() : minimum.z())));
    return result;
}
void MeshGeometry::build()
{
    QElapsedTimer timer;
    timer.start();
    bounds = {};
    for (const auto &t : triangles)
    {
        bounds.include(t.p1);
        bounds.include(t.p2);
        bounds.include(t.p3);
    }
    nodes.assign(1, BVHNode());
    int depth = 0;
    BuildBVH::buildBVHwithSAH(triangles, nodes, 0, int(triangles.size()) - 1, 8, 0, depth);
    double cost = 0;
    for (size_t i = 1; i < nodes.size(); ++i)
        cost += surfaceArea(nodes[i].AA, nodes[i].BB) * (nodes[i].n ? nodes[i].n : 1);
    const double rootArea = nodes.size() > 1 ? surfaceArea(nodes[1].AA, nodes[1].BB) : 0;
    traversalCost = rootArea > 0 && std::isfinite(cost / rootArea) ? std::max(1.0, cost / rootArea) : 1;
    buildMs = timer.nsecsElapsed() / 1e6;
}
std::vector<BVHNode> buildInstanceBvh(const std::vector<SceneInstance> &instances,
                                      const std::vector<std::shared_ptr<const MeshGeometry>> &meshes)
{
    std::vector<BVHNode> result(1);
    std::vector<int> ids;
    std::vector<double> costs(instances.size(), 1);
    for (int i = 0; i < int(instances.size()); ++i)
        if (instances[i].visible && instances[i].bounds.valid)
        {
            ids.push_back(i);
            const int mesh = instances[i].mesh;
            if (mesh >= 0 && mesh < int(meshes.size()) && meshes[mesh])
                costs[i] = meshes[mesh]->traversalCost;
        }
    result.reserve(std::max(size_t(1), ids.size() * 2));
    std::function<int(std::vector<int>, int)> build = [&](std::vector<int> list, int depth) {
        if (list.empty())
            return 0;
        int index = int(result.size());
        result.emplace_back();
        SceneBounds bounds;
        for (int id : list)
            bounds.include(instances[id].bounds);
        result[index].AA = bounds.minimum;
        result[index].BB = bounds.maximum;
        if (list.size() == 1)
        {
            result[index].n = 1;
            result[index].index = list.front();
            return index;
        }
        double bestCost = std::numeric_limits<double>::infinity();
        int split = int(list.size() / 2);
        auto bestOrder = list;
        int balancedLevels = 0;
        for (size_t remaining = list.size() - 1; remaining; remaining >>= 1)
            ++balancedLevels;
        // Keep enough headroom for the GPU's 64-entry traversal stack.
        for (int axis = 0; axis < 3 && depth < 48 && depth + balancedLevels < 60; ++axis)
        {
            auto order = list;
            std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
                return instances[a].bounds.center()[axis] < instances[b].bounds.center()[axis];
            });
            std::vector<SceneBounds> leftBounds(order.size()), rightBounds(order.size());
            std::vector<double> cumulativeCost(order.size());
            for (int i = 0; i < int(order.size()); ++i)
            {
                if (i)
                    leftBounds[i] = leftBounds[i - 1];
                leftBounds[i].include(instances[order[i]].bounds);
                cumulativeCost[i] = (i ? cumulativeCost[i - 1] : 0) + costs[order[i]];
            }
            for (int i = int(order.size()) - 1; i >= 0; --i)
            {
                if (i + 1 < int(order.size()))
                    rightBounds[i] = rightBounds[i + 1];
                rightBounds[i].include(instances[order[i]].bounds);
            }
            for (int i = 1; i < int(order.size()); ++i)
            {
                const double cost = surfaceArea(leftBounds[i - 1].minimum, leftBounds[i - 1].maximum) *
                                        cumulativeCost[i - 1] +
                                    surfaceArea(rightBounds[i].minimum, rightBounds[i].maximum) *
                                        (cumulativeCost.back() - cumulativeCost[i - 1]);
                if (cost < bestCost)
                {
                    bestCost = cost;
                    split = i;
                    bestOrder = order;
                }
            }
        }
        // Resolve both recursive calls before indexing the growable node array.
        const int left = build(std::vector<int>(bestOrder.begin(), bestOrder.begin() + split), depth + 1);
        const int right = build(std::vector<int>(bestOrder.begin() + split, bestOrder.end()), depth + 1);
        result[index].left = left;
        result[index].right = right;
        return index;
    };
    build(ids, 0);
    return result;
}
void refitInstanceBvh(std::vector<BVHNode> &nodes, const std::vector<SceneInstance> &instances)
{
    for (int i = int(nodes.size()) - 1; i > 0; --i)
    {
        auto &n = nodes[i];
        SceneBounds b;
        if (n.n)
            b = instances[n.index].bounds;
        else
        {
            b.include(nodes[n.left].AA);
            b.include(nodes[n.left].BB);
            b.include(nodes[n.right].AA);
            b.include(nodes[n.right].BB);
        }
        n.AA = b.minimum;
        n.BB = b.maximum;
    }
}
namespace
{
bool boxHit(const QVector3D &o, const QVector3D &d, const QVector3D &lo, const QVector3D &hi, float limit)
{
    float near = 0, far = limit;
    for (int k = 0; k < 3; ++k)
    {
        if (std::abs(d[k]) < 1e-30f)
        {
            if (o[k] < lo[k] || o[k] > hi[k])
                return false;
            continue;
        }
        float a = (lo[k] - o[k]) / d[k], b = (hi[k] - o[k]) / d[k];
        if (a > b)
            std::swap(a, b);
        near = std::max(near, a);
        far = std::min(far, b);
        if (near > far)
            return false;
    }
    return far > 0;
}
} // namespace
SceneHit intersectScene(const std::vector<std::shared_ptr<const MeshGeometry>> &meshes,
                        const std::vector<SceneInstance> &instances, const std::vector<BVHNode> &tlas,
                        const QVector3D &origin, const QVector3D &direction, bool bruteForce)
{
    SceneHit hit;
    auto instanceHit = [&](int id) {
        const auto &instance = instances[id];
        if (!instance.visible)
            return;
        const auto &mesh = *meshes[instance.mesh];
        const auto o = instance.inverse.map(origin), d = instance.inverse.mapVector(direction);
        auto triangleHit = [&](int index) {
            const auto &t = mesh.triangles[index];
            auto a = t.p2 - t.p1, b = t.p3 - t.p1, p = QVector3D::crossProduct(d, b);
            float det = QVector3D::dotProduct(a, p);
            if (std::abs(det) < 1e-12f)
                return;
            auto v = o - t.p1, q = QVector3D::crossProduct(v, a);
            float u = QVector3D::dotProduct(v, p) / det;
            float w = QVector3D::dotProduct(d, q) / det, dist = QVector3D::dotProduct(b, q) / det;
            const bool nearer = dist < hit.distance;
            const bool preferredTie = dist == hit.distance && (hit.instance < 0 || id < hit.instance ||
                                                               (id == hit.instance && index < hit.triangle));
            if (u >= 0 && w >= 0 && u + w <= 1 && dist > 0 && (nearer || preferredTie))
                hit = {id, index, dist};
        };
        if (bruteForce)
        {
            for (int i = 0; i < int(mesh.triangles.size()); ++i)
                triangleHit(i);
            return;
        }
        std::vector<int> stack;
        if (mesh.nodes.size() > 1)
            stack.push_back(1);
        while (!stack.empty())
        {
            int index = stack.back();
            stack.pop_back();
            const auto &n = mesh.nodes[index];
            if (!boxHit(o, d, n.AA, n.BB, hit.distance))
                continue;
            if (n.n)
                for (int i = n.index; i < n.index + n.n; ++i)
                    triangleHit(i);
            else
            {
                stack.push_back(n.left);
                stack.push_back(n.right);
            }
        }
    };
    if (bruteForce)
    {
        for (int i = 0; i < int(instances.size()); ++i)
            instanceHit(i);
        return hit;
    }
    std::vector<int> stack;
    if (tlas.size() > 1)
        stack.push_back(1);
    while (!stack.empty())
    {
        int index = stack.back();
        stack.pop_back();
        const auto &n = tlas[index];
        if (!boxHit(origin, direction, n.AA, n.BB, hit.distance))
            continue;
        if (n.n)
            instanceHit(n.index);
        else
        {
            stack.push_back(n.left);
            stack.push_back(n.right);
        }
    }
    return hit;
}
