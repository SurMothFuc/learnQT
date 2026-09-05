#include "Scene.h"
#include "SceneAssets.h"
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <stdexcept>
#include <limits>
#include <cmath>

namespace {
const char* textureSlots[] = {"baseColor", "normal", "metallic", "roughness", "emissive", "opacity"};
int Material::* const texMembers[] = {&Material::baseColorTex,&Material::normalTex,&Material::metallicTex,
    &Material::roughnessTex,&Material::emissiveTex,&Material::opacityTex};
void require(bool condition,const QString& error) { if (!condition) throw std::runtime_error(error.toStdString()); }
QMatrix4x4 matrixFromJson(const QJsonValue& value) {
    QMatrix4x4 m; auto a=value.toArray();
    if(a.size()==16) for(int r=0;r<4;++r) for(int c=0;c<4;++c) m(r,c)=a[r*4+c].toDouble();
    return m;
}
QJsonArray matrixJson(const QMatrix4x4& m) {
    QJsonArray a; for(int r=0;r<4;++r) for(int c=0;c<4;++c) a.append(m(r,c)); return a;
}
QVector3D averageColor(const QImage& source) {
    auto image=source.scaled(64,64,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
    QVector3D sum;
    auto linear=[](float c) { return c<=.04045f ? c/12.92f : std::pow((c+.055f)/1.055f,2.4f); };
    for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x) {
        auto p=image.pixel(x,y); sum+=QVector3D(linear(qRed(p)/255.f),linear(qGreen(p)/255.f),linear(qBlue(p)/255.f));
    }
    return sum/float(image.width()*image.height());
}
}

std::unique_ptr<Scene> Scene::prepareScene(const QString& path,bool model,QString& error,std::function<void(const QString&)> progress) {
    try {
        std::unique_ptr<Scene> candidate(new Scene(false));
        if(progress) progress("Reading scene document");
        if(model) {
            candidate->document=SceneDocument::model(path);
            candidate->document.root["hdr"]=QString::fromStdString(getResourcePath("hdr/peppermint_powerplant_4k.hdr"));
            candidate->document.root["fitModelOnImport"]=true;
        } else if(!SceneDocument::loadScene(path,candidate->document,error)) return {};
        candidate->buildDocument(progress);
        return candidate;
    } catch(const std::exception& e) { error=QString::fromUtf8(e.what()); return {}; }
}

void Scene::buildDocument(std::function<void(const QString&)> progress) {
    auto models=document.root["models"].toArray();
    auto materialDefs=document.root["materials"].toArray();
    auto textureDefs=document.root["textures"].toArray();
    QMap<QString,int> materialIndices;
    for(int i=0;i<materialDefs.size();++i) materialIndices[materialDefs[i].toObject()["id"].toString()]=i;
    QMap<QString,TextureAsset> capturedImages;
    for(int modelIndex=0;modelIndex<models.size();++modelIndex) {
        auto model=models[modelIndex].toObject(); const QString id=model["id"].toString();
        if(progress) progress(QString("Importing model %1/%2: %3").arg(modelIndex+1).arg(models.size()).arg(id));
        SceneAssets assets; assets.modelPath=model["source"].toString(); assets.strictRoot=document.packageRoot(); assets.dependencies=model["dependencies"].toObject();
        std::vector<Triangle> mesh; std::vector<TextureAsset> importedTextures;
        Material fallback; fallback.baseColor=QVector3D(.8,.8,.8); fallback.roughness=.7;
        MeshLoader::readModel(assets.modelPath.toStdString(),mesh,importedTextures,fallback,matrixFromJson(model.value("transform")),
            model["smoothNormals"].toBool(true),model["normalize"].toBool(false),&assets,model.value("material").toString().isEmpty());
        require(!mesh.empty(),"Cannot import model: "+assets.modelPath);
        model["dependencies"]=assets.dependencies;

        // Fit only newly imported models, once. Scene files always use explicit transforms.
        if(document.root["fitModelOnImport"].toBool()) {
            QVector3D lo(std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max()),hi=-lo;
            for(const auto& t:mesh) for(const auto& p:{t.p1,t.p2,t.p3}) for(int k=0;k<3;++k) { lo[k]=std::min(lo[k],p[k]); hi[k]=std::max(hi[k],p[k]); }
            const auto extent=hi-lo; const float scale=3.f/std::max(1e-8f,std::max(extent.x(),std::max(extent.y(),extent.z())));
            const auto center=(lo+hi)*.5f;
            for(auto& t:mesh) { t.p1=(t.p1-center)*scale; t.p2=(t.p2-center)*scale; t.p3=(t.p3-center)*scale; }
            QMatrix4x4 fit; fit.scale(scale); fit.translate(-center); model["transform"]=matrixJson(fit);
        }
        QMap<int,QString> importedTextureIds;
        for(int i=0;i<int(importedTextures.size());++i) {
            const auto& texture=importedTextures[i]; const QString source=QString::fromStdString(texture.sourcePath);
            const int marker=source.indexOf("::");
            auto def=SceneDocument::textureJson(texture);
            if(marker>=0) { def["model"]=id; def["embedded"]=source.mid(marker+2); }
            else def["source"]=source;
            QString textureId;
            for(auto v:textureDefs) {
                auto existing=v.toObject();
                // Match the source + sampler, allowing different uses of the same image.
                auto comparison=existing; comparison.remove("id");
                if(comparison==def) { textureId=existing["id"].toString(); break; }
            }
            if(textureId.isEmpty()) {
                textureId=id+"/texture/"+QString::number(i);
                // Existing saved sampler overrides must remain authoritative.
                bool found=false;
                for(auto v:textureDefs) if(v.toObject()["id"]==textureId) found=true;
                if(!found) { def["id"]=textureId; textureDefs.append(def); }
            }
            importedTextureIds[i]=textureId; capturedImages[textureId]=texture;
            // Also serve explicit IDs which refer to this embedded image.
            for(auto v:textureDefs) {
                auto d=v.toObject();
                if(marker>=0 && d["model"]==id && d["embedded"].toString()==source.mid(marker+2)) capturedImages[d["id"].toString()]=texture;
            }
        }
        auto bindings=model.value("materialBindings").toObject();
        for(auto& t:mesh) {
            const QString sourceId=QString::number(t.sourceMaterialIndex);
            QString materialId=model.value("material").toString();
            if(materialId.isEmpty()) materialId=bindings[sourceId].toString();
            if(materialId.isEmpty()) {
                materialId=id+"/material/"+sourceId;
                auto m=SceneDocument::materialJson(t.material); m["id"]=materialId;
                QJsonObject tex;
                for(int j=0;j<6;++j) if(t.material.*texMembers[j]>=0) tex[textureSlots[j]]=importedTextureIds[t.material.*texMembers[j]];
                m["textures"]=tex; materialIndices[materialId]=materialDefs.size(); materialDefs.append(m); bindings[sourceId]=materialId;
            }
            require(materialIndices.contains(materialId),"Unknown material: "+materialId);
            t.sceneMaterialIndex=materialIndices[materialId];
        }
        if(!bindings.isEmpty()) model["materialBindings"]=bindings;
        models[modelIndex]=model;
        triangles.insert(triangles.end(),std::make_move_iterator(mesh.begin()),std::make_move_iterator(mesh.end()));
    }
    document.root.remove("fitModelOnImport");
    document.root["models"]=models; document.root["materials"]=materialDefs; document.root["textures"]=textureDefs;
    if(progress) progress("Decoding material textures");
    QMap<QString,int> textureIndices;
    for(auto v:textureDefs) {
        const auto def=v.toObject(); const QString id=def["id"].toString();
        TextureAsset texture=capturedImages.value(id);
        if(!def["source"].toString().isEmpty()) { texture.sourcePath=def["source"].toString().toStdString(); texture.image=QImage(def["source"].toString()); }
        require(!texture.image.isNull(),"Cannot decode scene texture: "+id);
        texture.width=texture.image.width(); texture.height=texture.image.height(); texture.averageLinearColor=averageColor(texture.image);
        SceneDocument::applySampling(def,texture); textureIndices[id]=int(textures.size()); textures.push_back(texture);
    }
    std::vector<Material> materials;
    for(auto v:materialDefs) {
        auto def=v.toObject(); Material m=SceneDocument::materialFromJson(def); auto tex=def["textures"].toObject();
        for(int j=0;j<6;++j) if(tex.contains(textureSlots[j])) { const auto id=tex[textureSlots[j]].toString(); require(textureIndices.contains(id),"Unknown texture: "+id); m.*texMembers[j]=textureIndices[id]; }
        materials.push_back(m);
    }
    for(auto& t:triangles) t.material=materials.at(t.sceneMaterialIndex);
    document.restoreCamera(camera);
    m_currentModelPath=models.size()==1 ? models[0].toObject()["source"].toString().toStdString() : document.filePath.toStdString();
    QString error; require(document.validate(error),error);
    if(progress) progress("Building BVH and lights; preparing HDR cache");
    finalizeScene();
    require(document.root["hdr"].toString().isEmpty() || hdrRes.cols!=nullptr,"Cannot decode HDR environment.");
    if(progress) progress("Scene ready");
}

void Scene::adoptPrepared(Scene& s) {
    using std::swap;
    swap(camera,s.camera); swap(document,s.document); swap(m_currentModelPath,s.m_currentModelPath);
    swap(triangles,s.triangles); swap(textures,s.textures); swap(nodes,s.nodes);
    swap(triangles_encoded,s.triangles_encoded); swap(nodes_encoded,s.nodes_encoded); swap(lights_encoded,s.lights_encoded);
    swap(lightPowerSum,s.lightPowerSum); swap(hdrRes,s.hdrRes); swap(cache,s.cache); swap(hdrResolution,s.hdrResolution);
}
bool Scene::loadScene(const QString& path,QString& error) {
    auto next=prepareScene(path,false,error); if(!next) return false; adoptPrepared(*next); return true;
}
SceneDocument Scene::snapshotDocument() const {
    auto copy=document; copy.captureCamera(camera); copy.captureSettings(RenderParams::instance().snapshot());
    auto defs=copy.root["materials"].toArray(); QSet<int> seen;
    for(const auto& t:triangles) if(t.sceneMaterialIndex>=0 && !seen.contains(t.sceneMaterialIndex)) {
        const int i=t.sceneMaterialIndex; seen.insert(i); auto old=defs[i].toObject(); auto m=SceneDocument::materialJson(t.material);
        m["id"]=old["id"]; m["textures"]=old["textures"]; defs[i]=m;
    }
    copy.root["materials"]=defs; return copy;
}
bool Scene::saveScene(const QString& path,QString& error) {
    auto copy=snapshotDocument(); if(!copy.saveScene(path,error)) return false;
    const auto newPath=QFileInfo(path).absoluteFilePath();
    copy.root["portable"]=copy.root["portable"].toBool() && copy.filePath==newPath;
    copy.filePath=newPath; document=copy; return true;
}
bool Scene::exportScenePackage(const QString& path,QString& error) const { return snapshotDocument().exportScenePackage(path,error); }
