#pragma once

#include "BVH.h"
#include "Mesh.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <memory>

QMatrix4x4 sceneMatrix(const QJsonValue &value);
QJsonArray sceneMatrixJson(const QMatrix4x4 &matrix);
QString sceneId();

struct SceneBounds
{
    QVector3D minimum, maximum;
    bool valid = false;
    void include(const QVector3D &point);
    void include(const SceneBounds &other);
    QVector3D center() const
    {
        return (minimum + maximum) * .5f;
    }
    SceneBounds transformed(const QMatrix4x4 &matrix) const;
};

// Geometry and its local-space BLAS are immutable and shared by instances and undo history.
struct MeshGeometry
{
    QString key, name;
    int sourceMaterial = -1;
    std::vector<Triangle> triangles;
    std::vector<BVHNode> nodes;
    SceneBounds bounds;
    double buildMs = 0;
    double traversalCost = 1;
    void build();
};

struct ImportedNode
{
    QString key, parent, name;
    QMatrix4x4 world;
    std::vector<int> meshes;
};

struct ImportedModel
{
    std::vector<std::shared_ptr<MeshGeometry>> meshes;
    std::vector<ImportedNode> nodes;
    std::vector<TextureAsset> textures;
};

struct SceneInstance
{
    QString id, meshKey;
    int mesh = -1, material = -1;
    int triangleOffset = 0, nodeRoot = 0, surfaceOffset = 0;
    QMatrix4x4 transform, inverse;
    SceneBounds bounds;
    bool visible = true, locked = false;
};

struct SurfaceReference
{
    unsigned geometry, instance;
};
struct SceneAcceleration
{
    std::vector<SceneInstance> instances;
    std::vector<BVHNode> nodes;
};
struct SceneHit
{
    int instance = -1, triangle = -1;
    float distance = 1e30f;
};

// SAH top-level tree; immutable BLAS costs weight the instance leaves.
std::vector<BVHNode> buildInstanceBvh(const std::vector<SceneInstance> &instances,
                                      const std::vector<std::shared_ptr<const MeshGeometry>> &meshes = {});
void refitInstanceBvh(std::vector<BVHNode> &nodes, const std::vector<SceneInstance> &instances);
SceneHit intersectScene(const std::vector<std::shared_ptr<const MeshGeometry>> &meshes,
                        const std::vector<SceneInstance> &instances, const std::vector<BVHNode> &tlas,
                        const QVector3D &origin, const QVector3D &direction, bool bruteForce = false);
