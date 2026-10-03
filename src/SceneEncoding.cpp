#include "Scene.h"
#include <QElapsedTimer>
#include <stdexcept>

void Scene::encodeMaterials()
{
    static const char *textureSlots[] = {"baseColor", "normal",   "metallic",
                                         "roughness", "emissive", "opacity"};
    static int Material::*const members[] = {&Material::baseColorTex, &Material::normalTex,
                                             &Material::metallicTex,  &Material::roughnessTex,
                                             &Material::emissiveTex,  &Material::opacityTex};
    materials.clear();
    materialData.clear();
    QMap<QString, int> ids;
    auto textures = document.root["textures"].toArray();
    for (int i = 0; i < textures.size(); ++i)
        ids[textures[i].toObject()["id"].toString()] = i;
    for (auto v : document.root["materials"].toArray())
    {
        auto def = v.toObject();
        Material m = SceneDocument::materialFromJson(def);
        auto refs = def["textures"].toObject();
        for (int j = 0; j < 6; ++j)
            if (refs.contains(textureSlots[j]))
                m.*members[j] = ids.value(refs[textureSlots[j]].toString(), -1);
        materials.push_back(m);
        const QVector4D values[] = {
            QVector4D(m.emissive, m.sheenTint),
            QVector4D(m.baseColor, m.clearcoat),
            QVector4D(m.mediumColor, m.mediumAnisotropy),
            QVector4D(m.clearcoatGloss, m.IOR, m.transmission, m.alphaMode),
            QVector4D(m.mediumtype, m.mediumDensity, m.subsurface, m.metallic),
            QVector4D(m.specularTint, m.roughness, m.anisotropic, m.sheen),
            QVector4D(0, 0, m.baseColorTex, m.normalTex),
            QVector4D(m.metallicTex, m.roughnessTex, m.emissiveTex, m.opacityTex),
            QVector4D(m.opacity, m.alphaCutoff, m.normalScale, m.normalMapFlipY ? 1 : 0),
            QVector4D(m.metallicChannel, m.roughnessChannel, 0, 0)};
        materialData.insert(materialData.end(), std::begin(values), std::end(values));
    }
}
void Scene::rebuildInstances(bool topology)
{
    QElapsedTimer timer;
    timer.start();
    auto defs = document.root["materials"].toArray();
    QMap<QString, int> ids;
    for (int i = 0; i < defs.size(); ++i)
        ids[defs[i].toObject()["id"].toString()] = i;
    if (topology)
    {
        meshes.clear();
        instances.clear();
    }
    QMap<const MeshGeometry *, int> meshIds;
    for (int i = 0; i < int(meshes.size()); ++i)
        meshIds[meshes[i].get()] = i;
    int objectIndex = 0;
    for (auto v : document.root["objects"].toArray())
    {
        auto o = v.toObject();
        SceneInstance instance;
        instance.id = o["id"].toString();
        auto imported = modelAssets.value(o["model"].toString());
        int index = o["mesh"].toInt(-1);
        if (!imported || index < 0 || index >= int(imported->meshes.size()) || !imported->meshes[index])
            throw std::runtime_error("Missing instance mesh");
        auto mesh = imported->meshes[index];
        if (!meshIds.contains(mesh.get()))
        {
            meshIds[mesh.get()] = int(meshes.size());
            meshes.push_back(mesh);
        }
        instance.mesh = meshIds[mesh.get()];
        instance.meshKey = mesh->key;
        instance.material = ids.value(o["material"].toString(), -1);
        if (instance.material < 0)
            throw std::runtime_error("Missing instance material");
        instance.transform = sceneMatrix(o["transform"]);
        instance.inverse = instance.transform.inverted();
        instance.bounds = mesh->bounds.transformed(instance.transform);
        instance.visible = o["visible"].toBool(true);
        instance.locked = o["locked"].toBool();
        if (topology)
            instances.push_back(instance);
        else
        {
            if (objectIndex >= int(instances.size()) || instances[objectIndex].id != instance.id)
                throw std::runtime_error("Unexpected topology change");
            auto &old = instances[objectIndex];
            instance.triangleOffset = old.triangleOffset;
            instance.nodeRoot = old.nodeRoot;
            instance.surfaceOffset = old.surfaceOffset;
            old = instance;
        }
        ++objectIndex;
    }
    if (topology)
        encodeGeometry();
    if (topology)
    {
        bool reusable = accelerationCache && accelerationCache->instances.size() == instances.size();
        bool refit = false;
        if (reusable)
            for (size_t i = 0; i < instances.size(); ++i)
            {
                const auto &a = instances[i], &b = accelerationCache->instances[i];
                if (a.id != b.id || a.visible != b.visible)
                {
                    reusable = false;
                    break;
                }
                refit |= a.bounds.minimum != b.bounds.minimum || a.bounds.maximum != b.bounds.maximum;
            }
        if (reusable)
        {
            tlas = accelerationCache->nodes;
            if (refit)
                refitInstanceBvh(tlas, instances);
        }
        else
        {
            tlas = buildInstanceBvh(instances, meshes);
            ++tlasBuildCount;
        }
        accelerationCache.reset();
    }
    else
        refitInstanceBvh(tlas, instances);
    encodeInstances();
    tlasUpdateMs = timer.nsecsElapsed() / 1e6;
}
void Scene::encodeGeometry()
{
    geometryData.clear();
    triangles.clear();
    nodes.assign(1, BVHNode());
    surfaces.clear();
    std::vector<int> bases, roots;
    for (const auto &mesh : meshes)
    {
        int triBase = int(triangles.size()), nodeBase = int(nodes.size()) - 1;
        bases.push_back(triBase);
        roots.push_back(nodeBase + 1);
        triangles.insert(triangles.end(), mesh->triangles.begin(), mesh->triangles.end());
        for (const auto &t : mesh->triangles)
        {
            const QVector4D v[] = {QVector4D(t.p1),
                                   QVector4D(t.p2),
                                   QVector4D(t.p3),
                                   QVector4D(t.n1),
                                   QVector4D(t.n2),
                                   QVector4D(t.n3),
                                   QVector4D(t.uv1.x(), t.uv1.y(), t.uv2.x(), t.uv2.y()),
                                   QVector4D(t.uv3.x(), t.uv3.y(), 0, 0),
                                   t.tangent1,
                                   t.tangent2,
                                   t.tangent3};
            geometryData.insert(geometryData.end(), std::begin(v), std::end(v));
        }
        for (int i = 1; i < int(mesh->nodes.size()); ++i)
        {
            auto n = mesh->nodes[i];
            if (n.n)
                n.index += triBase;
            else
            {
                n.left += nodeBase;
                n.right += nodeBase;
            }
            nodes.push_back(n);
        }
    }
    for (int i = 0; i < int(instances.size()); ++i)
    {
        auto &obj = instances[i];
        obj.triangleOffset = bases[obj.mesh];
        obj.nodeRoot = roots[obj.mesh];
        obj.surfaceOffset = int(surfaces.size());
        for (int j = 0; j < int(meshes[obj.mesh]->triangles.size()); ++j)
            surfaces.push_back({unsigned(obj.triangleOffset + j), unsigned(i)});
    }
    nodes_encoded.resize(nodes.size());
    for (int i = 0; i < int(nodes.size()); ++i)
        nodes_encoded[i] = {QVector3D(nodes[i].left, nodes[i].right, 0),
                            QVector3D(nodes[i].n, nodes[i].index, 0), nodes[i].AA, nodes[i].BB};
}
void Scene::encodeInstances()
{
    instanceData.clear();
    for (const auto &obj : instances)
    {
        for (int c = 0; c < 4; ++c)
            instanceData.push_back(obj.transform.column(c));
        for (int c = 0; c < 4; ++c)
            instanceData.push_back(obj.inverse.column(c));
        instanceData.emplace_back(obj.material, obj.nodeRoot, obj.surfaceOffset - obj.triangleOffset,
                                  obj.visible ? ((materials[obj.material].alphaMode!=Mask && materials[obj.material].alphaMode!=Blend) && (materials[obj.material].mediumtype!=None || materials[obj.material].transmission>0) &&
                                      meshes[obj.mesh]->closedBoundary()?2:1) : 0);
    }
    tlasData.resize(tlas.size());
    for (int i = 0; i < int(tlas.size()); ++i)
        tlasData[i] = {QVector3D(tlas[i].left, tlas[i].right, 0), QVector3D(tlas[i].n, tlas[i].index, 0),
                       tlas[i].AA, tlas[i].BB};
}
void Scene::applyEditorDocument(const SceneDocument &next, bool rebuildTlas, bool materialOnly)
{
    document = next;
    ++revision;
    encodeMaterials();
    if (materialOnly)
    {
        auto defs = document.root["materials"].toArray();
        QMap<QString, int> ids;
        for (int i = 0; i < defs.size(); ++i)
            ids[defs[i].toObject()["id"].toString()] = i;
        auto objects = document.root["objects"].toArray();
        for (int i = 0; i < int(instances.size()); ++i)
            instances[i].material = ids.value(objects[i].toObject()["material"].toString());
        encodeInstances();
        buildLightData();
        return;
    }
    rebuildInstances(false);
    if (rebuildTlas)
    {
        tlas = buildInstanceBvh(instances, meshes);
        ++tlasBuildCount;
        encodeInstances();
    }
    buildLightData();
}
std::vector<Triangle> Scene::worldTriangles() const
{
    std::vector<Triangle> result;
    for (const auto &obj : instances)
        if (obj.visible)
            for (auto t : meshes[obj.mesh]->triangles)
            {
                t.p1 = obj.transform.map(t.p1);
                t.p2 = obj.transform.map(t.p2);
                t.p3 = obj.transform.map(t.p3);
                auto n = obj.inverse.transposed();
                t.n1 = n.mapVector(t.n1).normalized();
                t.n2 = n.mapVector(t.n2).normalized();
                t.n3 = n.mapVector(t.n3).normalized();
                t.material = materials[obj.material];
                t.sceneMaterialIndex = obj.material;
                result.push_back(t);
            }
    return result;
}
