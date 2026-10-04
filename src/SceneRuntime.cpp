#include "Scene.h"
#include "SceneAssets.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSet>
#include <cmath>
#include <stdexcept>

namespace
{
const char *textureSlots[] = {"baseColor", "normal", "metallic", "roughness", "emissive", "opacity"};
int Material::*const textureMembers[] = {&Material::baseColorTex, &Material::normalTex,
                                         &Material::metallicTex,  &Material::roughnessTex,
                                         &Material::emissiveTex,  &Material::opacityTex};
void require(bool ok, const QString &error)
{
    if (!ok)
        throw std::runtime_error(error.toStdString());
}
QVector3D averageColor(const QImage &source)
{
    auto im = source.scaled(64, 64, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                  .convertToFormat(QImage::Format_RGB32);
    QVector3D sum;
    auto linear = [](float c) { return c <= .04045f ? c / 12.92f : std::pow((c + .055f) / 1.055f, 2.4f); };
    for (int y = 0; y < im.height(); ++y)
        for (int x = 0; x < im.width(); ++x)
        {
            auto p = im.pixel(x, y);
            sum += QVector3D(linear(qRed(p) / 255.f), linear(qGreen(p) / 255.f), linear(qBlue(p) / 255.f));
        }
    return sum / float(im.width() * im.height());
}
QString cacheKey(const QJsonObject &model)
{
    return model["source"].toString() + "|" + QString::number(model["smoothNormals"].toBool(true)) + "|" +
           QString::number(model["material"].toString().isEmpty()) + "|" +
           QString::fromUtf8(QJsonDocument(model["dependencies"].toObject()).toJson(QJsonDocument::Compact));
}
} // namespace
std::unique_ptr<Scene> Scene::prepareScene(const QString &path, bool model, QString &error,
                                           std::function<void(const QString &)> progress)
{
    SceneDocument doc;
    if (model)
    {
        doc = SceneDocument::model(path);
        doc.root["hdr"] = QString::fromStdString(getResourcePath("hdr/peppermint_powerplant_4k.hdr"));
        doc.root["fitModelOnImport"] = true;
    }
    else if (path.isEmpty())
        doc = SceneDocument::empty();
    else if (!SceneDocument::loadScene(path, doc, error))
        return {};
    return prepareDocument(doc, error, {}, progress);
}
std::unique_ptr<Scene> Scene::prepareDocument(SceneDocument doc, QString &error, AssetCache cache,
                                              std::function<void(const QString &)> progress,
                                              std::shared_ptr<const SceneAcceleration> acceleration)
{
    try
    {
        std::unique_ptr<Scene> s(new Scene(false));
        s->document = std::move(doc);
        s->assetCache = std::move(cache);
        s->accelerationCache = std::move(acceleration);
        s->buildDocument(progress);
        return s;
    }
    catch (const std::exception &e)
    {
        error = QString::fromUtf8(e.what());
        return {};
    }
}
void Scene::buildDocument(std::function<void(const QString &)> progress)
{
    document.migrate();
    auto models = document.root["models"].toArray(), defs = document.root["materials"].toArray(),
         texDefs = document.root["textures"].toArray();
    auto objects = document.root["objects"].toArray(), groups = document.root["groups"].toArray();
    QMap<QString, int> materialIds;
    QMap<QString, TextureAsset> images;
    QSet<QString> usedCache;
    for (int i = 0; i < defs.size(); ++i)
        materialIds[defs[i].toObject()["id"].toString()] = i;
    for (int mi = 0; mi < models.size(); ++mi)
    {
        auto model = models[mi].toObject();
        QString id = model["id"].toString();
        if (!model.contains("transform"))
            model["transform"] = sceneMatrixJson(QMatrix4x4());
        if (!model.contains("smoothNormals"))
            model["smoothNormals"] = true;
        if (!model.contains("normalize"))
            model["normalize"] = false;
        if (!model.contains("dependencies"))
            for (auto existing : models)
            {
                auto other = existing.toObject();
                if (other["source"] == model["source"] &&
                    other["smoothNormals"].toBool(true) == model["smoothNormals"].toBool(true) &&
                    other.contains("dependencies"))
                {
                    model["dependencies"] = other["dependencies"];
                    break;
                }
            }
        if (progress)
            progress(QString::fromUtf8("准备模型 %1/%2：%3")
                         .arg(mi + 1)
                         .arg(models.size())
                         .arg(QFileInfo(model["source"].toString()).fileName()));
        std::shared_ptr<const ImportedModel> imported = assetCache.value(cacheKey(model));
        if (!imported)
        {
            SceneAssets assets;
            assets.modelPath = model["source"].toString();
            assets.dependencies = model["dependencies"].toObject();
            assets.strictRoot = document.packageRoot();
            auto input = std::make_shared<ImportedModel>();
            std::vector<Triangle> scratch;
            std::vector<TextureAsset> tex;
            Material fallback;
            fallback.baseColor = QVector3D(.8, .8, .8);
            fallback.roughness = .7;
            MeshLoader::readModel(assets.modelPath.toStdString(), scratch, tex, fallback, QMatrix4x4(),
                                  model["smoothNormals"].toBool(true), false, &assets,
                                  model["material"].toString().isEmpty(), input.get());
            require(!input->nodes.empty(), "Cannot import model: " + assets.modelPath);
            model["dependencies"] = assets.dependencies;
            for (int i = 0; i < int(input->meshes.size()); ++i)
                if (input->meshes[i])
                {
                    input->meshes[i]->key = cacheKey(model) + "/" + QString::number(i);
                    ++blasBuildCount;
                    blasBuildMs += input->meshes[i]->buildMs;
                }
            imported = input;
            assetCache[cacheKey(model)] = imported;
        }
        usedCache.insert(cacheKey(model));
        modelAssets[id] = imported;
        QMap<int, QString> importedTextureIds;
        for (int i = 0; i < int(imported->textures.size()); ++i)
        {
            const auto &texture = imported->textures[i];
            QString source = QString::fromStdString(texture.sourcePath);
            int marker = source.indexOf("::");
            auto def = SceneDocument::textureJson(texture);
            if (marker >= 0)
            {
                def["model"] = id;
                def["embedded"] = source.mid(marker + 2);
            }
            else
                def["source"] = source;
            QString textureId;
            for (auto v : texDefs)
            {
                auto old = v.toObject();
                old.remove("id");
                if (old == def)
                {
                    textureId = v.toObject()["id"].toString();
                    break;
                }
            }
            if (textureId.isEmpty())
            {
                textureId = id + "/texture/" + QString::number(i);
                bool found = false;
                for (auto v : texDefs)
                    if (v.toObject()["id"] == textureId)
                        found = true;
                if (!found)
                {
                    def["id"] = textureId;
                    texDefs.append(def);
                }
            }
            importedTextureIds[i] = textureId;
            images[textureId] = texture;
            for (auto v : texDefs)
            {
                auto d = v.toObject();
                if (marker >= 0 && d["model"] == id && d["embedded"].toString() == source.mid(marker + 2))
                    images[d["id"].toString()] = texture;
            }
        }
        auto bindings = model["materialBindings"].toObject();
        for (const auto &mesh : imported->meshes)
            if (mesh && !mesh->triangles.empty())
            {
                QString sourceId = QString::number(mesh->sourceMaterial),
                        materialId = model["material"].toString();
                if (materialId.isEmpty())
                    materialId = bindings[sourceId].toString();
                if (materialId.isEmpty())
                {
                    materialId = id + "/material/" + sourceId;
                    if (!materialIds.contains(materialId))
                    {
                        const auto &m = mesh->triangles.front().material;
                        auto def = SceneDocument::materialJson(m);
                        def["id"] = materialId;
                        QJsonObject refs;
                        for (int j = 0; j < 6; ++j)
                            if (m.*textureMembers[j] >= 0)
                                refs[textureSlots[j]] = importedTextureIds[m.*textureMembers[j]];
                        def["textures"] = refs;
                        materialIds[materialId] = defs.size();
                        defs.append(def);
                    }
                    bindings[sourceId] = materialId;
                }
            }
        model["materialBindings"] = bindings;
        if (!model["expanded"].toBool())
        {
            SceneBounds bounds;
            int leafCount = 0;
            for (const auto &node : imported->nodes)
                for (int mesh : node.meshes)
                {
                    bounds.include(imported->meshes[mesh]->bounds.transformed(node.world));
                    ++leafCount;
                }
            require(bounds.valid, "Model contains no triangle meshes.");
            QMatrix4x4 base = sceneMatrix(model["transform"]);
            if (model["normalize"].toBool())
            {
                auto e = bounds.maximum - bounds.minimum;
                base.scale(1.f / std::max(1e-8f, std::max(e.x(), std::max(e.y(), e.z()))));
            }
            if (document.root["fitModelOnImport"].toBool())
            {
                auto b = bounds.transformed(base);
                auto e = b.maximum - b.minimum;
                QMatrix4x4 fit;
                fit.scale(3.f / std::max(1e-8f, std::max(e.x(), std::max(e.y(), e.z()))));
                fit.translate(-b.center());
                base = fit * base;
            }
            QString parent = model["parent"].toString("root"), fileGroup = id + "/group";
            if (leafCount > 1)
                groups.append(QJsonObject{{"id", fileGroup},
                                          {"name", QFileInfo(model["source"].toString()).completeBaseName()},
                                          {"parent", parent},
                                          {"order", mi}});
            int order = 0;
            for (const auto &node : imported->nodes)
            {
                QString nodeId = id + "/node/" + node.key;
                if (leafCount > 1)
                    groups.append(QJsonObject{
                        {"id", nodeId},
                        {"name", node.name.isEmpty() ? QStringLiteral("节点") : node.name},
                        {"parent", node.parent.isEmpty() ? fileGroup : id + "/node/" + node.parent},
                        {"order", order++}});
                for (int j = 0; j < int(node.meshes.size()); ++j)
                {
                    int index = node.meshes[j];
                    const auto &mesh = *imported->meshes[index];
                    QString mat = model["material"].toString();
                    if (mat.isEmpty())
                        mat = bindings[QString::number(mesh.sourceMaterial)].toString();
                    QString name = mesh.name.isEmpty() ? node.name : mesh.name;
                    if (name.isEmpty())
                        name = QFileInfo(model["source"].toString()).completeBaseName();
                    objects.append(QJsonObject{{"id", id + "/object/" + node.key + "/" + QString::number(j)},
                                               {"name", name},
                                               {"model", id},
                                               {"mesh", index},
                                               {"material", mat},
                                               {"transform", sceneMatrixJson(base * node.world)},
                                               {"parent", leafCount > 1 ? nodeId : parent},
                                               {"order", leafCount > 1 ? j : mi},
                                               {"visible", true},
                                               {"locked", false}});
                }
            }
            model["expanded"] = true;
        }
        models[mi] = model;
    }
    for (auto it = assetCache.begin(); it != assetCache.end();)
        if (!usedCache.contains(it.key()))
            it = assetCache.erase(it);
        else
            ++it;
    document.root["models"] = models;
    document.root["materials"] = defs;
    document.root["textures"] = texDefs;
    document.root["objects"] = objects;
    document.root["groups"] = groups;
    document.root.remove("fitModelOnImport");
    for (auto v : texDefs)
    {
        auto def = v.toObject();
        auto texture = images.value(def["id"].toString());
        if (!def["source"].toString().isEmpty())
        {
            texture.sourcePath = def["source"].toString().toStdString();
            texture.image = QImage(def["source"].toString());
        }
        require(!texture.image.isNull(), "Cannot decode texture: " + def["id"].toString());
        texture.width = texture.image.width();
        texture.height = texture.image.height();
        texture.averageLinearColor = averageColor(texture.image);
        SceneDocument::applySampling(def, texture);
        textures.push_back(texture);
    }
    QString error;
    require(document.validate(error), error);
    document.restoreCamera(camera);
    m_currentModelPath = models.size() == 1 ? models[0].toObject()["source"].toString().toStdString()
                                            : document.filePath.toStdString();
    encodeMaterials();
    rebuildInstances(true);
    finalizeScene();
    require(document.root["hdr"].toString().isEmpty() || hdrRes.cols, "Cannot decode HDR environment.");
    if (progress)
        progress(QString::fromUtf8("场景准备完成"));
}
void Scene::adoptPrepared(Scene &s)
{
    using std::swap;
#define SWAP(x) swap(x, s.x)
    SWAP(camera);
    SWAP(document);
    SWAP(m_currentModelPath);
    SWAP(triangles);
    SWAP(textures);
    SWAP(nodes);
    SWAP(triangles_encoded);
    SWAP(nodes_encoded);
    SWAP(lights_encoded);
    SWAP(lightPowerSum);
    SWAP(environmentLuminanceIntegral);
    SWAP(finiteIrradianceEstimate);
    SWAP(hdrRes);
    SWAP(cache);
    SWAP(hdrResolution);
    SWAP(assetCache);
    SWAP(modelAssets);
    SWAP(meshes);
    SWAP(instances);
    SWAP(materials);
    SWAP(tlas);
    SWAP(geometryData);
    SWAP(materialData);
    SWAP(instanceData);
    SWAP(tlasData);
    SWAP(surfaces);
    SWAP(surfacePdfs);
    SWAP(blasBuildCount);
    SWAP(tlasBuildCount);
    SWAP(blasBuildMs);
    SWAP(tlasUpdateMs);
#undef SWAP
    ++revision;
}
bool Scene::loadScene(const QString &path, QString &error)
{
    auto next = prepareScene(path, false, error);
    if (!next)
        return false;
    adoptPrepared(*next);
    return true;
}
SceneDocument Scene::snapshotDocument() const
{
    auto copy = document;
    copy.captureCamera(camera);
    copy.captureSettings(RenderParams::instance().snapshot());
    return copy;
}
bool Scene::saveScene(const QString &path, QString &error)
{
    auto copy = snapshotDocument();
    if (!copy.saveScene(path, error))
        return false;
    auto newPath = QFileInfo(path).absoluteFilePath();
    copy.root["portable"] = copy.root["portable"].toBool() && copy.filePath == newPath;
    copy.filePath = newPath;
    document = copy;
    return true;
}
bool Scene::exportScenePackage(const QString &path, QString &error) const
{
    return snapshotDocument().exportScenePackage(path, error);
}
