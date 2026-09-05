#include "SceneDocument.h"
#include "Scene.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QSet>
#include <functional>
#include <cmath>

QJsonArray jsonVector(const QVector3D& v) { return {v.x(), v.y(), v.z()}; }
QVector3D sceneVector(const QJsonValue& v, QVector3D fallback) {
    const auto a = v.toArray();
    return a.size() == 3 ? QVector3D(a[0].toDouble(), a[1].toDouble(), a[2].toDouble()) : fallback;
}
namespace {
void visitPaths(QJsonObject& root, const std::function<QString(const QString&)>& visit) {
    if (!root["hdr"].toString().isEmpty()) root["hdr"] = visit(root["hdr"].toString());
    for (const QString group : {QString("models"), QString("textures")}) {
        auto entries = root[group].toArray();
        for (int i = 0; i < entries.size(); ++i) {
            auto entry = entries[i].toObject();
            if (!entry["source"].toString().isEmpty()) entry["source"] = visit(entry["source"].toString());
            auto deps = entry["dependencies"].toObject();
            for (auto it = deps.begin(); it != deps.end(); ++it) it.value() = visit(it.value().toString());
            if (!deps.isEmpty()) entry["dependencies"] = deps;
            entries[i] = entry;
        }
        root[group] = entries;
    }
}
bool inside(const QString& path, const QString& directory) {
    const QString relative = QDir(directory).relativeFilePath(QFileInfo(path).canonicalFilePath());
    return !QDir::isAbsolutePath(relative) && relative != ".." && !relative.startsWith("../");
}
bool vectorValid(const QJsonValue& value, int size) {
    auto a = value.toArray();
    if (a.size() != size) return false;
    for (auto v : a) if (!v.isDouble() || !std::isfinite(v.toDouble())) return false;
    return true;
}
}

SceneDocument SceneDocument::model(const QString& path) {
    SceneDocument d;
    d.root = {{"version", 1}, {"name", QFileInfo(path).completeBaseName()},
        {"models", QJsonArray{QJsonObject{{"id", "model"}, {"source", QFileInfo(path).absoluteFilePath()},
            {"smoothNormals", true}, {"normalize", false}}}},
        {"materials", QJsonArray()}, {"textures", QJsonArray()},
        {"lights", QJsonArray{QJsonObject{{"id", "key-light"}, {"type", "sphere"}, {"radius", .01},
            {"position", QJsonArray{-1,4,0}}, {"radiance", QJsonArray{45/(PI*.0001),40/(PI*.0001),32/(PI*.0001)}}}}}};
    Camera c(QVector3D(0,.35f,4.5f)); d.captureCamera(c);
    d.captureSettings(RenderParams::Snapshot());
    return d;
}
QString SceneDocument::packageRoot() const {
    return root["portable"].toBool() ? QFileInfo(filePath).absolutePath() : QString();
}
bool SceneDocument::validate(QString& error, bool checkFiles) const {
    auto fail = [&](const QString& e) { error = e; return false; };
    if (root["version"].toInt(-1) != 1) return fail("Unsupported scene version (expected 1).");
    if (!root["name"].isString() || root["models"].toArray().isEmpty()) return fail("Scene requires a name and at least one model.");
    QSet<QString> modelIds, materialIds, textureIds;
    for (const auto& group : {QString("models"), QString("materials"), QString("textures"), QString("lights")}) {
        if (!root[group].isArray()) return fail("Missing array: " + group);
        QSet<QString> ids;
        for (auto value : root[group].toArray()) {
            auto o = value.toObject(); const auto id = o["id"].toString();
            if (id.isEmpty() || ids.contains(id)) return fail("Empty or duplicate ID in " + group);
            ids.insert(id);
            if (group == "models") {
                if (o["source"].toString().isEmpty()) return fail("Model source is missing: " + id);
                if (o.contains("transform") && !vectorValid(o["transform"],16)) return fail("Invalid model transform: " + id);
                if(o.contains("transform")) {
                    const auto a=o["transform"].toArray(); QMatrix4x4 m;
                    for(int r=0;r<4;++r) for(int c=0;c<4;++c) m(r,c)=a[r*4+c].toDouble();
                    if(std::abs(m.determinant())<1e-12 || m(3,0)!=0 || m(3,1)!=0 || m(3,2)!=0 || m(3,3)!=1)
                        return fail("Model transform must be invertible and affine: "+id);
                }
                for(auto key:{"smoothNormals","normalize"}) if(o.contains(key) && !o[key].isBool()) return fail("Invalid import option.");
            }
        }
        if (group == "models") modelIds = ids;
        if (group == "materials") materialIds = ids;
        if (group == "textures") textureIds = ids;
    }
    for (auto v : root["models"].toArray()) {
        auto o = v.toObject();
        if (!o["material"].toString().isEmpty() && !materialIds.contains(o["material"].toString())) return fail("Unknown material binding.");
        for (auto id : o["materialBindings"].toObject()) if (!materialIds.contains(id.toString())) return fail("Unknown imported material binding.");
    }
    for (auto v : root["materials"].toArray()) {
        auto m = v.toObject();
        const auto scalarDefaults=materialJson(Material());
        for(auto it=scalarDefaults.begin();it!=scalarDefaults.end();++it) if(m.contains(it.key())) {
            const auto value=m[it.key()];
            if((it.value().isDouble() && (!value.isDouble() || !std::isfinite(value.toDouble()))) ||
                (it.value().isBool() && !value.isBool())) return fail("Invalid material parameter: "+it.key());
        }
        for (auto id : m["textures"].toObject()) if (!textureIds.contains(id.toString())) return fail("Unknown texture binding.");
        for (const auto key : {"baseColor", "emissive", "mediumColor"})
            if (m.contains(key) && !vectorValid(m[key],3)) return fail("Invalid material color.");
        const Material mat = materialFromJson(m);
        if (mat.roughness < 0 || mat.roughness > 1 || mat.metallic < 0 || mat.metallic > 1 || mat.IOR <= 0 || mat.alphaMode < 0 || mat.alphaMode > 3)
            return fail("Material parameters out of range.");
    }
    for (auto v : root["textures"].toArray()) {
        auto o = v.toObject();
        if (o["source"].toString().isEmpty() && (!modelIds.contains(o["model"].toString()) || o["embedded"].toString().isEmpty()))
            return fail("Texture needs a source or an embedded model binding.");
        for (auto key : {"uvScale", "uvOffset"}) if (o.contains(key) && !vectorValid(o[key],2)) return fail("Invalid texture UV transform.");
        for(auto key:{"uvRotation","wrapS","wrapT","minFilter","magFilter"}) if(o.contains(key) && !o[key].isDouble()) return fail("Invalid texture sampler.");
        for(auto key:{"wrapS","wrapT"}) if(o.contains(key) && (o[key].toInt(-1)<0 || o[key].toInt(-1)>3)) return fail("Invalid texture wrap mode.");
        if(o.contains("minFilter") && !QSet<int>{9728,9729,9984,9985,9986,9987}.contains(o["minFilter"].toInt())) return fail("Invalid texture minification filter.");
        if(o.contains("magFilter") && !QSet<int>{9728,9729}.contains(o["magFilter"].toInt())) return fail("Invalid texture magnification filter.");
    }
    auto c = root["camera"].toObject();
    if (!vectorValid(c["position"],3) || !vectorValid(c["target"],3) || !vectorValid(c["up"],3) ||
        (sceneVector(c["position"]) - sceneVector(c["target"])).lengthSquared() < 1e-10 ||
        QVector3D::crossProduct(sceneVector(c["target"])-sceneVector(c["position"]),sceneVector(c["up"])).lengthSquared()<1e-10 ||
        c["fov"].toDouble() <= 0 || c["fov"].toDouble() >= 175) return fail("Invalid camera.");
    if (!root["render"].isObject()) return fail("Missing render settings.");
    auto render=root["render"].toObject();
    for(auto key:{"denoise","renderLow","useTileRendering","useEnvironmentMap"}) if(render.contains(key) && !render[key].isBool()) return fail("Invalid render toggle.");
    for(auto key:{"tileSize","maxBounces","maxRenderFrames"}) if(render.contains(key) && (!render[key].isDouble() || render[key].toInt(-1)<0)) return fail("Invalid render setting.");
    const auto s = settings();
    if (s.tileSize < 1 || s.tileSize > 16384 || s.maxBounces < 0 || s.maxBounces > int(MAX_BOUNCES_LIMIT) || s.maxRenderFrames < 0)
        return fail("Render settings out of range.");
    for (auto v : root["lights"].toArray()) {
        auto l=v.toObject(); auto type=l["type"].toString();
        if ((type!="sphere" && type!="sun") || l["radius"].toDouble()<=0 || !vectorValid(l["radiance"],3) ||
            !vectorValid(l[type=="sun" ? "direction":"position"],3)) return fail("Invalid analytic light.");
        const auto radiance=sceneVector(l["radiance"]);
        if(radiance.x()<0 || radiance.y()<0 || radiance.z()<0 || (type=="sun" && (sceneVector(l["direction"]).lengthSquared()<1e-10 || l["radius"].toDouble()>1.5707963))) return fail("Invalid light radiance or sun direction/radius.");
    }
    if (checkFiles) {
        QString missing; auto copy=root;
        visitPaths(copy, [&](const QString& p) {
            if (!QFileInfo(p).isFile()) missing="Missing resource: " + p;
            else if (!packageRoot().isEmpty() && !inside(p,packageRoot())) missing="Portable scene references an external resource: " + p;
            return p;
        });
        if (!missing.isEmpty()) return fail(missing);
    }
    return true;
}
bool SceneDocument::loadScene(const QString& path, SceneDocument& result, QString& error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { error=f.errorString(); return false; }
    QJsonParseError parse;
    const auto json=QJsonDocument::fromJson(f.readAll(),&parse);
    if (parse.error!=QJsonParseError::NoError || !json.isObject()) { error="Invalid scene JSON: "+parse.errorString(); return false; }
    SceneDocument candidate; candidate.root=json.object(); candidate.filePath=QFileInfo(path).absoluteFilePath();
    const QDir base(QFileInfo(path).absolutePath());
    visitPaths(candidate.root,[&](const QString& p) { return QDir::cleanPath(base.absoluteFilePath(p)); });
    if (!candidate.validate(error)) return false;
    result=candidate; return true;
}
bool SceneDocument::saveScene(const QString& path, QString& error) const {
    if (!validate(error)) return false;
    auto copy=root;
    const QDir base(QFileInfo(path).absolutePath());
    // Save As is a regular document; it may legitimately reference the old package.
    copy["portable"]=root["portable"].toBool() && QFileInfo(path).absoluteFilePath()==filePath;
    visitPaths(copy,[&](const QString& p) { return base.relativeFilePath(p); });
    QSaveFile out(path); out.setDirectWriteFallback(false);
    const auto bytes=QJsonDocument(copy).toJson();
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes)!=bytes.size() || !out.commit()) { error=out.errorString(); return false; }
    return true;
}
bool SceneDocument::exportScenePackage(const QString& directory, QString& error) const {
    if (!validate(error)) return false;
    const QFileInfo target(directory); const QDir targetDir(directory);
    if (target.exists() && (!target.isDir() || !targetDir.entryList(QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden|QDir::System).isEmpty())) {
        error="Export destination must be new or empty."; return false;
    }
    QTemporaryDir stage(target.absolutePath()+"/.scene-package-XXXXXX");
    if (!stage.isValid()) { error="Cannot create export staging directory."; return false; }
    auto copy=root; QMap<QString,QString> copied; bool ok=true;
    visitPaths(copy,[&](const QString& source) {
        if (copied.contains(source)) return copied[source];
        QFile input(source);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!input.open(QIODevice::ReadOnly) || !hash.addData(&input)) { ok=false; error="Cannot read "+source; return QString(); }
        const auto rel="assets/"+QString::fromLatin1(hash.result().toHex())+"/"+QFileInfo(source).fileName();
        const auto output=QDir(stage.path()).filePath(rel);
        QDir().mkpath(QFileInfo(output).absolutePath());
        if (!QFileInfo::exists(output) && !QFile::copy(source,output)) { ok=false; error="Cannot copy "+source; }
        copied[source]=rel; return rel;
    });
    if (!ok) return false;
    copy["portable"]=true;
    QSaveFile file(QDir(stage.path()).filePath("scene.scene.json"));
    const auto bytes=QJsonDocument(copy).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) { error=file.errorString(); return false; }
    SceneDocument check;
    if (!loadScene(file.fileName(),check,error)) return false;
    // Re-import under strict-root resolution before publishing the package.
    auto runtime=Scene::prepareScene(file.fileName(),false,error);
    if (!runtime) return false;
    runtime.reset();
    // Publish only after all files and references validate. Never remove a nonempty destination.
    if (target.exists() && !QDir().rmdir(target.absoluteFilePath())) { error="Destination is no longer empty."; return false; }
    if (!QDir().rename(stage.path(),target.absoluteFilePath())) {
        if (target.isDir()) QDir().mkpath(target.absoluteFilePath());
        error="Cannot publish export directory."; return false;
    }
    stage.setAutoRemove(false); return true;
}
void SceneDocument::captureCamera(const Camera& c) {
    root["camera"]=QJsonObject{{"position",jsonVector(c.position)},{"target",jsonVector(c.target)},{"up",jsonVector(c.worldUp)},{"fov",c.zoom}};
}
void SceneDocument::restoreCamera(Camera& c) const {
    auto o=root["camera"].toObject(); c.restoreState(sceneVector(o["position"]),sceneVector(o["target"]),sceneVector(o["up"]),o["fov"].toDouble(45));
}
void SceneDocument::captureSettings(const RenderParams::Snapshot& s) {
    root["render"]=QJsonObject{{"denoise",s.denoise},{"renderLow",s.renderLow},{"useTileRendering",s.useTileRendering},
        {"tileSize",s.tileSize},{"useEnvironmentMap",s.useEnvironmentMap},{"maxBounces",s.maxBounces},{"maxRenderFrames",s.maxRenderFrames}};
}
RenderParams::Snapshot SceneDocument::settings() const {
    RenderParams::Snapshot s; auto o=root["render"].toObject();
#define SETTING(name) if(o.contains(#name)) s.name=o[#name].toVariant().value<decltype(s.name)>();
    SETTING(denoise) SETTING(renderLow) SETTING(useTileRendering) SETTING(tileSize) SETTING(useEnvironmentMap) SETTING(maxBounces) SETTING(maxRenderFrames)
#undef SETTING
    return s;
}
QJsonObject SceneDocument::materialJson(const Material& m) {
    QJsonObject o{{"baseColor",jsonVector(m.baseColor)},{"emissive",jsonVector(m.emissive)},{"mediumColor",jsonVector(m.mediumColor)}};
#define SCALAR(name) o[#name]=m.name;
    SCALAR(subsurface) SCALAR(metallic) SCALAR(specularTint) SCALAR(roughness) SCALAR(anisotropic) SCALAR(sheen) SCALAR(sheenTint)
    SCALAR(clearcoat) SCALAR(clearcoatGloss) SCALAR(IOR) SCALAR(transmission) SCALAR(alphaMode) SCALAR(opacity) SCALAR(alphaCutoff)
    SCALAR(mediumtype) SCALAR(mediumDensity) SCALAR(mediumAnisotropy) SCALAR(metallicChannel) SCALAR(roughnessChannel) SCALAR(normalScale) SCALAR(normalMapFlipY)
#undef SCALAR
    return o;
}
Material SceneDocument::materialFromJson(const QJsonObject& o) {
    Material m; m.baseColor=sceneVector(o["baseColor"],m.baseColor); m.emissive=sceneVector(o["emissive"],m.emissive); m.mediumColor=sceneVector(o["mediumColor"],m.mediumColor);
#define SCALAR(name) if(o.contains(#name)) m.name=o[#name].toVariant().value<decltype(m.name)>();
    SCALAR(subsurface) SCALAR(metallic) SCALAR(specularTint) SCALAR(roughness) SCALAR(anisotropic) SCALAR(sheen) SCALAR(sheenTint)
    SCALAR(clearcoat) SCALAR(clearcoatGloss) SCALAR(IOR) SCALAR(transmission) SCALAR(alphaMode) SCALAR(opacity) SCALAR(alphaCutoff)
    SCALAR(mediumtype) SCALAR(mediumDensity) SCALAR(mediumAnisotropy) SCALAR(metallicChannel) SCALAR(roughnessChannel) SCALAR(normalScale) SCALAR(normalMapFlipY)
#undef SCALAR
    return m;
}
QJsonObject SceneDocument::textureJson(const TextureAsset& t) {
    return {{"uvOffset",QJsonArray{t.uvOffset.x(),t.uvOffset.y()}},{"uvScale",QJsonArray{t.uvScale.x(),t.uvScale.y()}},
        {"uvRotation",t.uvRotation},{"wrapS",t.wrapS},{"wrapT",t.wrapT},{"minFilter",t.minFilter},{"magFilter",t.magFilter}};
}
void SceneDocument::applySampling(const QJsonObject& o, TextureAsset& t) {
    if(o.contains("uvOffset")) { auto a=o["uvOffset"].toArray(); t.uvOffset=QVector2D(a[0].toDouble(),a[1].toDouble()); }
    if(o.contains("uvScale")) { auto a=o["uvScale"].toArray(); t.uvScale=QVector2D(a[0].toDouble(),a[1].toDouble()); }
#define SAMPLING(name) if(o.contains(#name)) t.name=o[#name].toVariant().value<decltype(t.name)>();
    SAMPLING(uvRotation) SAMPLING(wrapS) SAMPLING(wrapT) SAMPLING(minFilter) SAMPLING(magFilter)
#undef SAMPLING
}
