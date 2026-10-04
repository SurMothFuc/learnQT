#include "Scene.h"
#include "EmissionTexturePower.h"
#include <QProcessEnvironment>
#include <map>
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
        environmentLuminanceIntegral=4*PI*.35;
        cache = new float[3]{1, 1, 1};
        return;
    }
    if (!HDRLoader::load(path.toUtf8().constData(), hdrRes))
        throw std::runtime_error("Cannot decode environment image: " + path.toStdString());
    cache = calculateHdrCache(hdrRes.cols, hdrRes.width, hdrRes.height);
    hdrResolution = hdrRes.width;
    environmentLuminanceIntegral=0;
    for(int y=0;y<hdrRes.height;++y) {
        const double omega=2*PI/hdrRes.width*(std::cos(PI*y/hdrRes.height)-std::cos(PI*(y+1)/hdrRes.height));
        for(int x=0;x<hdrRes.width;++x) {
            const float *p=hdrRes.cols+3*(y*hdrRes.width+x);
            environmentLuminanceIntegral+=std::max(0.f,luminance({p[0],p[1],p[2]}))*omega;
        }
    }
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
    EmissionTexturePower texturePower(textures);
    std::map<std::pair<int,int>,std::vector<float>> uvPowers;
    const bool localPower=!qEnvironmentVariableIsSet("LEARNQT_LEGACY_EMISSION_POWER");
    for (const auto &obj : instances)
        if (obj.visible)
        {
            const auto &material = materials[obj.material];
            QVector3D emission = material.emissive;
            if (material.emissiveTex >= 0 && material.emissiveTex < int(textures.size()))
                emission *= textures[material.emissiveTex].averageLinearColor;
            float power = luminance(emission);
            if (localPower?material.emissive.lengthSquared()==0:power<=0)
                continue;
            const auto &mesh = *meshes[obj.mesh];
            for (int i = 0; i < int(mesh.triangles.size()); ++i)
            {
                const auto &t = mesh.triangles[i];
                float trianglePower=power;
                if(localPower) {
                    auto &cached=uvPowers[std::make_pair(obj.mesh,obj.material)];
                    if(cached.empty()) {
                        cached.reserve(mesh.triangles.size());
                        for(const auto &triangle:mesh.triangles)cached.push_back(texturePower.estimate(triangle,material));
                    }
                    trianglePower=cached[i];
                    if(trianglePower<=0)continue;
                }
                float area = .5f * QVector3D::crossProduct(obj.transform.mapVector(t.p2 - t.p1),
                                                           obj.transform.mapVector(t.p3 - t.p1))
                                       .length();
                if (area <= 1e-12f)
                    continue;
                Light_encoded light;
                light.param0 = QVector4D(EncodedLightTriangle, obj.surfaceOffset + i, 0, 0);
                light.param1 = QVector4D(0, 0, 0, area);
                light.param2 = QVector4D(material.emissive, 0);
                light.param3 = QVector4D(0,0,trianglePower,0);
                lights_encoded.push_back(light);
                weights.push_back(area * trianglePower);
            }
        }
    addAnalyticLights(weights);
    // Group selection uses a solid-angle radiance proxy at the scene center,
    // rather than comparing environment radiance directly to emitter area.
    // This is a global heuristic; it is not a position-dependent Light Tree.
    finiteIrradianceEstimate=0;
    const QVector3D center=tlas.size()>1?(tlas[1].AA+tlas[1].BB)*.5f:QVector3D();
    for(const auto &light:lights_encoded) {
        const int type=int(light.param0.x());const double radius=light.param0.w();
        double omega=0,brightness=luminance(light.param2.toVector3D());
        if(type==EncodedLightSunDisk)omega=4*PI*std::pow(std::sin(.5*radius),2);
        else if(type==EncodedLightSphere) {
            const double d2=(light.param1.toVector3D()-center).lengthSquared();
            omega=d2<=radius*radius?4*PI:2*PI*(1-std::sqrt(std::max(0.,1-radius*radius/d2)));
        } else {
            const auto &ref=surfaces.at(int(light.param0.y()));
            const auto &object=instances.at(ref.instance);
            const auto &t=triangles.at(ref.geometry);
            const double d2=(object.transform.map((t.p1+t.p2+t.p3)/3.f)-center).lengthSquared();
            omega=std::min(2*PI,double(light.param1.w())/std::max(1e-30,d2));
            brightness=light.param3.z();
        }
        finiteIrradianceEstimate+=brightness*omega;
    }
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
float Scene::environmentSelectionProbability() const
{
    if(lights_encoded.empty())return 1;
    const double env=environmentLuminanceIntegral*document.root["environment"].toObject()["intensity"].toDouble(1);
    if(env<=0)return 0;
    if(finiteIrradianceEstimate<=0)return 1;
    // Dyadic boundaries preserve complete group strata at 16/32/... samples.
    // Arbitrary 5%/95% boundaries can add count variance even for a nearly
    // constant sun integrand. Keep one stratum of support for either group.
    const double probability=env/(env+finiteIrradianceEstimate);
    return float(std::max(1.,std::min(15.,std::round(probability*16)))/16);
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
