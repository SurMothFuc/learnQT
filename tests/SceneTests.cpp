#include "Scene.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QFileInfo>
#include <iostream>
#include <stdexcept>
#include <cmath>

static void check(bool ok,const QString& message) { if(!ok) throw std::runtime_error(message.toStdString()); }
static QByteArray read(const QString& path) { QFile f(path); check(f.open(QIODevice::ReadOnly),f.errorString()); return f.readAll(); }
static void write(const QString& path,const QByteArray& data) { QFile f(path); check(f.open(QIODevice::WriteOnly),f.errorString()); check(f.write(data)==data.size(),f.errorString()); }
static void json(const QString& path,const QJsonObject& o) { write(path,QJsonDocument(o).toJson()); }
static void near(float actual,float expected,const char* name) { check(std::abs(actual-expected)<2e-4,QString("Mismatch: ")+name); }
static void ground(const Scene& s) {
    const auto materials=s.document.root["materials"].toArray(); int groundIndex=-1;
    for(int i=0;i<materials.size();++i) if(materials[i].toObject()["id"]=="stone-ground") groundIndex=i;
    check(groundIndex>=0,"Ground material missing");
    float modelMin=1e30f,minX=1e30f,maxX=-1e30f,minZ=1e30f,maxZ=-1e30f,groundY=0; int count=0;
    for(const auto& t:s.triangles) {
        if(t.sceneMaterialIndex!=groundIndex) { modelMin=std::min(modelMin,std::min(t.p1.y(),std::min(t.p2.y(),t.p3.y()))); continue; }
        ++count; const auto a=t.uv2-t.uv1,b=t.uv3-t.uv1;
        check(std::abs(a.x()*b.y()-a.y()*b.x())>.9f,"Degenerate ground UVs");
        check(t.n1.y()>.999f && t.n2.y()>.999f && t.n3.y()>.999f,"Ground normals not up");
        near(t.material.metallic,0,"ground metallic"); near(t.material.transmission,0,"ground transmission");
        check(t.material.baseColorTex>=0 && t.material.normalTex>=0 && t.material.roughnessTex>=0,"Ground PBR maps missing");
        for(int index:{t.material.baseColorTex,t.material.normalTex,t.material.roughnessTex}) {
            const auto& tex=s.textures[index]; near(tex.uvScale.x(),6,"repeat U"); near(tex.uvScale.y(),6,"repeat V");
            check(tex.width==2048 && tex.height==2048,"Ground map not 2K");
        }
        for(const auto& p:{t.p1,t.p2,t.p3}) { minX=std::min(minX,p.x()); maxX=std::max(maxX,p.x()); minZ=std::min(minZ,p.z()); maxZ=std::max(maxZ,p.z()); groundY=p.y(); }
    }
    check(count==2,"Ground must have exactly two triangles"); near(maxX-minX,12,"ground width"); near(maxZ-minZ,12,"ground depth"); near(groundY,modelMin-.001f,"ground contact");
}

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        QTemporaryDir temporary(QCoreApplication::applicationDirPath()+"/scene-tests-XXXXXX"); check(temporary.isValid(),"Cannot create test directory");
        const QString base=temporary.path()+QString::fromUtf8("/中文 空格"); check(QDir().mkpath(base),"mkdir failed");
        QString error;
        auto scene=Scene::prepareScene(QString::fromStdString(getResourcePath("scenes/lantern.scene.json")),false,error); check(bool(scene),error); ground(*scene);
        const auto original=scene->document;
        auto settings=original.settings(); settings.denoise=false; settings.useTileRendering=false; settings.tileSize=137; settings.useEnvironmentMap=false; settings.maxBounces=7; settings.maxRenderFrames=19; settings.renderLow=true;
        RenderParams::instance().applySnapshot(settings);
        scene->camera.restoreState(QVector3D(3,2,5),QVector3D(1,.5f,-.2f),QVector3D(0,1,0),61);
        scene->camera.processMouseMovement(0,0); near(scene->camera.position.x(),3,"orbit restore X"); near(scene->camera.position.y(),2,"orbit restore Y");
        scene->updateMaterial(QVector3D(.1f,.2f,.3f),QVector3D(.7f,.6f,.5f),.2f,.3f,.4f,.5f,.2f,.3f,.4f,.5f,.6f,1.7f,.1f);
        const QString saved=base+"/保存.scene.json";
        check(scene->saveScene(saved,error),error);
        const auto expected=scene->snapshotDocument();
        auto loaded=Scene::prepareScene(saved,false,error); check(bool(loaded),error);
        check(loaded->snapshotDocument().root==expected.root,"Roundtrip changed models/materials/textures/lights/camera/settings");
        check(loaded->document.settings()==settings,"Render settings roundtrip");
        const QString other=temporary.path()+"/另存为"; QDir().mkpath(other);
        check(loaded->saveScene(other+"/copy.scene.json",error),error);
        SceneDocument rebased; check(SceneDocument::loadScene(other+"/copy.scene.json",rebased,error),error);
        check(rebased.root==expected.root,"Save As changed resource references");
        QTemporaryDir crossDisk(QDir::tempPath()+"/learnqt-scene-crossdisk-XXXXXX"); check(crossDisk.isValid(),"Cannot create cross-disk test directory");
        check(expected.saveScene(crossDisk.path()+"/copy.scene.json",error),error);
        SceneDocument crossRead; check(SceneDocument::loadScene(crossDisk.path()+"/copy.scene.json",crossRead,error),error);
        check(crossRead.root==expected.root,"Cross-disk Save As changed resources");
        const auto old=read(saved);
        check(!scene->saveScene(base+"/missing/fail.scene.json",error),"Writing to missing parent succeeded"); check(read(saved)==old,"Failed save damaged old file");
        check(!scene->saveScene(base,error),"Writing to a directory succeeded");
        const QString invalid=base+"/bad.scene.json";
        write(invalid,"{broken"); SceneDocument ignored;
        check(!SceneDocument::loadScene(invalid,ignored,error),"Corrupt JSON accepted");
        auto bad=expected.root; bad["version"]=999; json(invalid,bad);
        check(!SceneDocument::loadScene(invalid,ignored,error),"Unknown version accepted");
        bad=expected.root; auto models=bad["models"].toArray(); auto model=models[0].toObject(); model["source"]=base+"/missing.glb"; models[0]=model; bad["models"]=models; json(invalid,bad);
        check(!scene->loadScene(invalid,error),"Missing resource accepted"); check(scene->snapshotDocument().root==expected.root,"Failed load replaced old scene");
        // Restore the untouched preset for geometry and package tests.
        scene=Scene::prepareScene(QString::fromStdString(getResourcePath("scenes/lantern.scene.json")),false,error); check(bool(scene),error);
        RenderParams::instance().applySnapshot(scene->document.settings());
        const auto before=scene->snapshotDocument(); const auto location=scene->document.filePath;
        const QString package=base+"/package";
        check(scene->exportScenePackage(package,error),error);
        check(scene->document.filePath==location && scene->snapshotDocument().root==before.root,"Export changed current document");
        check(!scene->exportScenePackage(package,error),"Nonempty export destination accepted");
        const QString moved=temporary.path()+"/moved package"; check(QDir().rename(package,moved),"Package relocation failed");
        auto movedScene=Scene::prepareScene(moved+"/scene.scene.json",false,error); check(bool(movedScene),error); ground(*movedScene);
        QString stone;
        for(auto v:movedScene->document.root["textures"].toArray()) if(v.toObject()["id"]=="stone-color") stone=v.toObject()["source"].toString();
        check(QFile::rename(stone,stone+".hidden"),"Cannot hide packaged texture");
        check(!Scene::prepareScene(moved+"/scene.scene.json",false,error),"Package fell back to original texture");
        check(QFile::rename(stone+".hidden",stone),"Cannot restore packaged texture");
        auto escape=QJsonDocument::fromJson(read(moved+"/scene.scene.json")).object(); auto ts=escape["textures"].toArray(); auto t=ts.last().toObject();
        t["source"]=QString::fromStdString(getResourcePath("textures/stone_floor/stone_floor_diff_2k.jpg")); ts[ts.size()-1]=t; escape["textures"]=ts; json(moved+"/escape.scene.json",escape);
        check(!SceneDocument::loadScene(moved+"/escape.scene.json",ignored,error),"Portable document escaped package root");
        // Record OBJ -> MTL -> absolute texture dependencies in a Unicode directory.
        const QString author=base+"/source"; QDir().mkpath(author);
        QImage pixel(4,4,QImage::Format_RGB32); pixel.fill(QColor(100,150,200)); check(pixel.save(author+"/stone.png"),"Fixture image save failed");
        write(author+"/mesh.obj","mtllib material.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nusemtl stone\nf 1/1 2/2 3/3\n");
        write(author+"/material.mtl",("newmtl stone\nKd 1 1 1\nmap_Kd "+author+"/stone.png\n").toUtf8());
        auto obj=Scene::prepareScene(author+"/mesh.obj",true,error); check(bool(obj),error); check(obj->textures.size()==1,"OBJ texture dependency not imported");
        const auto deps=obj->document.root["models"].toArray()[0].toObject()["dependencies"].toObject(); check(deps.size()>=2,"OBJ/MTL/texture dependency closure incomplete");
        check(obj->exportScenePackage(base+"/obj-package",error),error);
        const QString hidden=base+"/source-hidden"; check(QDir().rename(author,hidden),"Cannot hide authored files");
        auto packagedObj=Scene::prepareScene(base+"/obj-package/scene.scene.json",false,error); check(bool(packagedObj),error); check(packagedObj->textures.size()==1,"Packaged OBJ texture lost");
        // glTF with real external bin/image files and two different same-name images.
        const QString gltfDir=base+"/gltf-source"; QDir().mkpath(gltfDir+"/other");
        auto gltf=QJsonDocument::fromJson(read(QString::fromStdString(getResourcePath("../tests/assets/texture_transform_triangle.gltf")))).object();
        auto buffers=gltf["buffers"].toArray(); auto buffer=buffers[0].toObject();
        write(gltfDir+"/mesh.bin",QByteArray::fromBase64(buffer["uri"].toString().section(',',1).toLatin1())); buffer["uri"]="mesh.bin"; buffers[0]=buffer; gltf["buffers"]=buffers;
        auto images=gltf["images"].toArray(); auto image=images[0].toObject();
        write(gltfDir+"/albedo.png",QByteArray::fromBase64(image["uri"].toString().section(',',1).toLatin1())); image["uri"]="albedo.png"; images[0]=image; gltf["images"]=images;
        json(gltfDir+"/mesh.gltf",gltf);
        pixel.fill(QColor(30,40,50)); check(pixel.save(gltfDir+"/other/albedo.png"),"Collision fixture image save failed");
        auto external=Scene::prepareScene(gltfDir+"/mesh.gltf",true,error); check(bool(external),error);
        auto textureDefs=external->document.root["textures"].toArray(); textureDefs.append(QJsonObject{{"id","collision"},{"source",gltfDir+"/other/albedo.png"}});
        external->document.root["textures"]=textureDefs;
        auto defs=external->document.root["materials"].toArray(); auto mat=defs[0].toObject(); auto bindings=mat["textures"].toObject(); bindings["roughness"]="collision"; mat["textures"]=bindings; defs[0]=mat; external->document.root["materials"]=defs;
        // Document-only export intentionally exercises authored texture bindings too.
        check(external->document.exportScenePackage(base+"/gltf-package",error),error);
        check(QDir().rename(gltfDir,base+"/gltf-source-hidden"),"Cannot hide glTF originals");
        auto packagedGltf=Scene::prepareScene(base+"/gltf-package/scene.scene.json",false,error); check(bool(packagedGltf),error);
        check(packagedGltf->textures.size()==2,"Same-name texture collision lost an image");
        check(packagedGltf->textures[0].sourcePath!=packagedGltf->textures[1].sourcePath,"Package texture paths collided");
        auto gd=packagedGltf->document.root["models"].toArray()[0].toObject()["dependencies"].toObject(); QString bin;
        for(auto it=gd.begin();it!=gd.end();++it) if(it.value().toString().endsWith("mesh.bin")) bin=it.value().toString();
        check(!bin.isEmpty(),"glTF bin not tracked"); check(QFile::rename(bin,bin+".hidden"),"Cannot hide packaged bin");
        check(!Scene::prepareScene(base+"/gltf-package/scene.scene.json",false,error),"Package used original glTF bin");
        // Black HDR and malformed HDR must terminate without accepting partial pixels.
        write(base+"/broken.hdr","#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 8 +X 8\n");
        HDRLoaderResult hdr{}; check(!HDRLoader::load((base+"/broken.hdr").toUtf8().constData(),hdr),"Truncated HDR accepted");
        std::cout << "Scene roundtrip, save-as, error recovery, camera, ground and portable dependency tests passed.\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
