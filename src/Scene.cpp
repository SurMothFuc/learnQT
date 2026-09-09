#include "Scene.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
std::string startupModel;
QString startupScene;
float luminance(const QVector3D &c)
{
    return .212671f * c.x() + .715160f * c.y() + .072169f * c.z();
}
} // namespace
Scene::Scene(bool initialize)
{
    if (!initialize)
        return;
    QString error;
    auto prepared = prepareScene(startupModel.empty() ? startupScene : QString::fromStdString(startupModel),
                                 !startupModel.empty(), error);
    if (!prepared)
        throw std::runtime_error(error.toStdString());
    adoptPrepared(*prepared);
    RenderParams::instance().applySnapshot(document.settings());
}
Scene::~Scene()
{
    resetSceneData();
}
void Scene::setStartupScenePath(const QString &path)
{
    startupScene = path;
}
void Scene::setStartupModelPath(const std::string &path)
{
    startupModel = path;
}
bool Scene::loadModelScene(const std::string &path, std::string *errorMessage)
{
    QString error;
    auto candidate = prepareScene(QString::fromStdString(path), true, error);
    if (!candidate)
    {
        if (errorMessage)
            *errorMessage = error.toStdString();
        return false;
    }
    adoptPrepared(*candidate);
    return true;
}
void Scene::resetSceneData()
{
    delete[] cache;
    cache = nullptr;
    delete[] hdrRes.cols;
    hdrRes = {};
}
void Scene::finalizeScene()
{
    buildLightData();
    const QString path = document.root["hdr"].toString();
    if (path.isEmpty())
    {
        hdrRes.width = hdrRes.height = hdrResolution = 1;
        hdrRes.cols = new float[3]{.35f, .35f, .35f};
        cache = new float[3]{1, 1, 1};
        return;
    }
    if (!HDRLoader::load(path.toUtf8().constData(), hdrRes))
        throw std::runtime_error("Cannot decode environment image: " + path.toStdString());
    cache = calculateHdrCache(hdrRes.cols, hdrRes.width, hdrRes.height);
    hdrResolution = hdrRes.width;
    std::cout << "Scene: " << instances.size() << " instances, " << triangles.size() << " unique triangles, "
              << meshes.size() << " BLAS\n";
}
void Scene::addAnalyticLights(std::vector<float> &weights)
{
    const float pi = float(PI);
    for (auto v : document.root["lights"].toArray())
    {
        auto d = v.toObject();
        bool sun = d["type"].toString() == "sun";
        float r = d["radius"].toDouble();
        auto color = sceneVector(d["radiance"]), p = sceneVector(d[sun ? "direction" : "position"]);
        Light_encoded light;
        light.param0 = QVector4D(sun ? EncodedLightSunDisk : EncodedLightSphere, -1, 0, r);
        light.param1 = QVector4D(sun ? p.normalized() : p, 0);
        light.param2 = QVector4D(color, 0);
        light.param3 = {};
        lights_encoded.push_back(light);
        weights.push_back(std::max(
            0.f, float(luminance(color) * (sun ? 4 * pi * std::pow(std::sin(.5f * r), 2) : 4 * pi * r * r))));
    }
}
void Scene::buildLightData()
{
    lights_encoded.clear();
    surfacePdfs.assign(surfaces.size(), 0);
    std::vector<float> weights;
    for (const auto &obj : instances)
        if (obj.visible)
        {
            const auto &material = materials[obj.material];
            QVector3D emission = material.emissive;
            if (material.emissiveTex >= 0 && material.emissiveTex < int(textures.size()))
                emission *= textures[material.emissiveTex].averageLinearColor;
            float power = luminance(emission);
            if (power <= 0)
                continue;
            const auto &mesh = *meshes[obj.mesh];
            for (int i = 0; i < int(mesh.triangles.size()); ++i)
            {
                const auto &t = mesh.triangles[i];
                float area = .5f * QVector3D::crossProduct(obj.transform.mapVector(t.p2 - t.p1),
                                                           obj.transform.mapVector(t.p3 - t.p1))
                                       .length();
                if (area <= 1e-12f)
                    continue;
                Light_encoded light;
                light.param0 = QVector4D(EncodedLightTriangle, obj.surfaceOffset + i, 0, 0);
                light.param1 = QVector4D(0, 0, 0, area);
                light.param2 = QVector4D(material.emissive, 0);
                light.param3 = {};
                lights_encoded.push_back(light);
                weights.push_back(area * power);
            }
        }
    addAnalyticLights(weights);
    double total = 0;
    for (float w : weights)
        total += w;
    lightPowerSum = float(total);
    if (total <= 0)
    {
        lights_encoded.clear();
        return;
    }
    double cumulative = 0;
    float previous = 0;
    for (int i = 0; i < int(weights.size()); ++i)
    {
        cumulative += weights[i];
        float cdf = i + 1 == int(weights.size()) ? 1.f : float(cumulative / total);
        float pdf = cdf - previous;
        previous = cdf;
        lights_encoded[i].param0.setZ(pdf);
        lights_encoded[i].param3.setX(cdf);
        if (int(lights_encoded[i].param0.x()) == EncodedLightTriangle)
            surfacePdfs[int(lights_encoded[i].param0.y())] = pdf;
    }
}
void Scene::updateMaterial(QVector3D emissive, QVector3D baseColor, float subsurface, float metallic,
                           float specularTint, float roughness, float anisotropic, float sheen,
                           float sheenTint, float clearcoat, float clearcoatGloss, float IOR,
                           float transmission)
{
    auto next = document;
    auto defs = next.root["materials"].toArray();
    for (int i = 0; i < defs.size(); ++i)
    {
        auto old = defs[i].toObject();
        auto m = SceneDocument::materialFromJson(old);
        m.emissive = emissive;
        m.baseColor = baseColor;
        m.subsurface = subsurface;
        m.metallic = metallic;
        m.specularTint = specularTint;
        m.roughness = roughness;
        m.anisotropic = anisotropic;
        m.sheen = sheen;
        m.sheenTint = sheenTint;
        m.clearcoat = clearcoat;
        m.clearcoatGloss = clearcoatGloss;
        m.IOR = IOR;
        m.transmission = transmission;
        auto value = SceneDocument::materialJson(m);
        value["id"] = old["id"];
        value["textures"] = old["textures"];
        defs[i] = value;
    }
    next.root["materials"] = defs;
    applyEditorDocument(next);
}
