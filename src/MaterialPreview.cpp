#include "MaterialPreview.h"
#include "MaterialUi.h"
#include "renderer.h"
#include "common.h"
#include <QJsonDocument>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QThread>
#include <QWaitCondition>
#include <stdexcept>

namespace {
QJsonObject findDefinition(QJsonArray values, const QString &id) {
    for (auto v : values) if (v.toObject()["id"].toString()==id) return v.toObject();
    return {};
}
SceneDocument studioDocument() {
    auto d=SceneDocument::empty();
    Material ball, floor, wall;ball.baseColor={.5f,.5f,.5f};ball.roughness=.3f;
    floor.baseColor={.18f,.18f,.18f};floor.roughness=.7f;wall.baseColor={.5f,.5f,.5f};
    QJsonArray mats;
    for (auto p : {qMakePair(QString("preview-surface"),ball),qMakePair(QString("preview-floor"),floor),qMakePair(QString("preview-wall"),wall)}) {
        auto m=SceneDocument::materialJson(p.second);m["id"]=p.first;mats.append(m);
    }
    QJsonArray models,objects;
    auto add=[&](QString id,QString path,QString material,QMatrix4x4 transform) {
        models.append(QJsonObject{{"id",id},{"source",QString::fromStdString(getResourcePath(path.toStdString()))},
            {"material",material},{"expanded",true},{"smoothNormals",true},{"normalize",false}});
        objects.append(QJsonObject{{"id",id+"/object"},{"name",id},{"model",id},{"mesh",0},{"material",material},
            {"parent","root"},{"order",objects.size()},{"visible",true},{"locked",false},{"transform",sceneMatrixJson(transform)}});
    };
    QMatrix4x4 ballTransform;ballTransform.translate(0,1.02f,0);
    add("preview-ball","material_preview/sphere.obj","preview-surface",ballTransform);
    QMatrix4x4 floorTransform;floorTransform.scale(14,1,14);
    add("preview-ground","models/plane.obj","preview-floor",floorTransform);
    QMatrix4x4 back;back.translate(0,2,-2.3f);back.rotate(90,1,0,0);back.scale(8,1,4);
    add("preview-backdrop","models/plane.obj","preview-wall",back);
    d.root["models"]=models;d.root["objects"]=objects;d.root["materials"]=mats;
    d.root["hdr"]=QString::fromStdString(getResourcePath("imported/glslpt/assets/HDR/material-test.hdr"));
    d.root["environment"]=QJsonObject{{"intensity",.35},{"rotation",0.0}};
    d.root["lights"]=QJsonArray{
        QJsonObject{{"id","preview-key"},{"type","sphere"},{"position",QJsonArray{-3,4,3}},{"radius",1.0},{"radiance",QJsonArray{12,12,12}}},
        QJsonObject{{"id","preview-fill"},{"type","sphere"},{"position",QJsonArray{3,2,1}},{"radius",.8},{"radiance",QJsonArray{5,5,5}}}};
    return d;
}
struct PreviewRequest {
    quint64 version=0;
    SceneDocument source;
    QString material;
    Scene::AssetCache assets;
    QVector3D eye;
};
}

// A latest-request mailbox; all Scene and GL state is owned by this worker.
class MaterialPreviewWorker : public QThread {
public:
    explicit MaterialPreviewWorker(MaterialPreview *target) : owner(target) {
        surface=new QOffscreenSurface;
        QSurfaceFormat format;format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setVersion(3,3);format.setProfile(QSurfaceFormat::CoreProfile);surface->setFormat(format);surface->create();
    }
    ~MaterialPreviewWorker() override { running=false;cancel=true;wake.wakeAll();wait();delete surface; }
    void submit(PreviewRequest r) {
        QMutexLocker lock(&mutex);pending=std::move(r);hasPending=true;cancel=true;wake.wakeAll();
    }
    void stop() { QMutexLocker lock(&mutex);hasPending=false;cancel=true;wake.wakeAll(); }
protected:
    void run() override {
        QOpenGLContext context;context.setFormat(surface->format());
        if (!context.create() || !context.makeCurrent(surface)) { post(0,{},0,"无法创建材质预览 OpenGL 上下文");return; }
        std::unique_ptr<Scene> scene;
        std::unique_ptr<Renderer> renderer;
        QByteArray textureSignature;
        try {
            while (running) {
                PreviewRequest r;
                {
                    QMutexLocker lock(&mutex);
                    while (running && !hasPending) wake.wait(&mutex);
                    if (!running) break;
                    r=std::move(pending);hasPending=false;cancel=false;
                }
                try {
                    if (!scene) { QString error;scene=Scene::prepareDocument(studioDocument(),error);if(!scene)throw std::runtime_error(error.toStdString()); }
                    if (cancel || !running) continue;
                    auto mat=findDefinition(r.source.root["materials"].toArray(),r.material);
                    if (mat.isEmpty()) throw std::runtime_error("材质不存在");
                    mat["id"]="preview-surface";
                    auto next=scene->document;auto materials=next.root["materials"].toArray();materials[0]=mat;next.root["materials"]=materials;
                    QJsonArray definitions;
                    QSet<QString> refs;
                    for (auto value : mat["textures"].toObject()) refs.insert(value.toString());
                    for (auto v : r.source.root["textures"].toArray()) if (refs.contains(v.toObject()["id"].toString())) definitions.append(v);
                    const auto textureKey=QJsonDocument(definitions).toJson(QJsonDocument::Compact);
                    const bool texturesChanged=textureKey!=textureSignature || !renderer;
                    if (texturesChanged) {
                        std::vector<TextureAsset> images;
                        for (auto v : definitions) {
                            auto def=v.toObject();TextureAsset texture;
                            auto path=def["source"].toString();
                            if (!path.isEmpty()) { texture.image=QImage(path);texture.sourcePath=path.toStdString(); }
                            else {
                                auto model=findDefinition(r.source.root["models"].toArray(),def["model"].toString());
                                auto embedded=model["source"].toString()+"::"+def["embedded"].toString();
                                for (auto asset:r.assets) for (const auto &t:asset->textures)
                                    if (QString::fromStdString(t.sourcePath)==embedded) texture=t;
                            }
                            if(texture.image.isNull()) throw std::runtime_error((QString("无法读取贴图：")+def["id"].toString()).toStdString());
                            texture.width=texture.image.width();texture.height=texture.image.height();SceneDocument::applySampling(def,texture);
                            auto thumbnail=texture.image.scaled(16,16).convertToFormat(QImage::Format_RGB32);QVector3D sum;
                            for(int y=0;y<thumbnail.height();++y) for(int x=0;x<thumbnail.width();++x) sum+=materialLinearColor(thumbnail.pixelColor(x,y));
                            texture.averageLinearColor=sum/float(thumbnail.width()*thumbnail.height());images.push_back(texture);
                        }
                        scene->textures=std::move(images);textureSignature=textureKey;
                    }
                    next.root["textures"]=definitions;
                    scene->applyEditorDocument(next,false,true);
                    scene->camera.restoreState(r.eye,{0,1.02f,0},{0,1,0},38);
                    auto settings=scene->document.settings();settings.denoise=false;settings.renderLow=false;
                    settings.useTileRendering=true;settings.tileSize=64;settings.maxRenderFrames=64;settings.maxBounces=8;
                    settings.rasterLocked=false;settings.computePathtrace=false;
                    SceneDirtyFlags dirty=SceneDirtyFlag::Camera|SceneDirtyFlag::Material;
                    if(texturesChanged) dirty=kInitialSceneDirty;
                    if (!renderer) { renderer.reset(new Renderer(384,384,settings,nullptr,scene.get()));renderer->formal=true;renderer->cancel=&cancel; }
                    renderer->prepareJob(QSize(384,384),settings,dirty);
                    QElapsedTimer update;update.start();int published=0;
                    while(running && !cancel && renderer->samples()<64) {
                        if(!renderer->waitForGpuBoundary()) continue;
                        renderer->render(384,384,settings,0,1,[this] {return !running || cancel.load();});
                        if(renderer->completeRound() && renderer->samples()>published && (update.elapsed()>=200 || renderer->samples()==64)) {
                            post(r.version,renderer->result(settings),renderer->samples(),{});published=renderer->samples();update.restart();
                        }
                        renderer->submitGpuBoundary();
                        msleep(1);
                    }
                    // Never mutate or release resources while a cancelled batch is in flight.
                    while(!renderer->waitForGpuBoundary()) msleep(1);
                } catch(const std::exception &e) { post(r.version,{},0,QString::fromUtf8(e.what()));renderer.reset();scene.reset();textureSignature.clear(); }
            }
        } catch(const std::exception &e) { post(0,{},0,QString::fromUtf8(e.what())); }
        renderer.reset();scene.reset();context.doneCurrent();
    }
private:
    MaterialPreview *owner;
    QOffscreenSurface *surface;
    QMutex mutex;QWaitCondition wake;PreviewRequest pending;bool hasPending=false;
    std::atomic_bool running{true},cancel{false};
    void post(quint64 version,QImage frame,int samples,QString error) {
        QMetaObject::invokeMethod(owner,[=] {owner->receive(version,frame,samples,error);},Qt::QueuedConnection);
    }
};

MaterialPreview::MaterialPreview(QWidget *parent):QWidget(parent) {
    setObjectName("materialBallPreview");setMinimumSize(240,240);setMouseTracking(true);
    debounce.setSingleShot(true);debounce.setInterval(150);
    connect(&debounce,&QTimer::timeout,this,[this] {schedule();});
    message=tr("选择单个材质以预览");
}
MaterialPreview::~MaterialPreview() { delete worker; }
void MaterialPreview::setMaterial(const SceneDocument &doc,const QString &id,Scene::AssetCache cache) {
    auto mat=findDefinition(doc.root["materials"].toArray(),id);QJsonArray textures;
    QSet<QString> refs;for(auto v:mat["textures"].toObject())refs.insert(v.toString());
    for(auto v:doc.root["textures"].toArray())if(refs.contains(v.toObject()["id"].toString()))textures.append(v);
    auto key=QJsonDocument(QJsonObject{{"material",mat},{"textures",textures},{"models",doc.root["models"]}}).toJson(QJsonDocument::Compact);
    source=doc;assets=std::move(cache);materialId=id;
    if(key==signature) return;
    signature=key;frame={};frameSamples=0;frameRevision=0;invalidate();
}
void MaterialPreview::setRenderingAllowed(bool value,const QString &reason) {
    pauseReason=reason;if(allowed==value) return;allowed=value;
    if(!allowed) {++revision;debounce.stop();if(worker)worker->stop();message=reason;update();if(stateChanged)stateChanged();}
    else invalidate();
}
void MaterialPreview::invalidate() {
    ++revision;debounce.stop();if(worker)worker->stop();
    message=materialId.isEmpty()?tr("选择单个材质以预览"):!allowed?pauseReason:tr("正在更新材质球…");
    if(allowed && isVisible() && !materialId.isEmpty())debounce.start();update();
    if(stateChanged)stateChanged();
}
void MaterialPreview::schedule() {
    if(!allowed || !isVisible() || materialId.isEmpty()) return;
    if(!worker || worker->isFinished()) {delete worker;worker=new MaterialPreviewWorker(this);worker->start();}
    const QVector3D eye(distance*std::cos(elevation)*std::sin(azimuth),1.02f+distance*std::sin(elevation),distance*std::cos(elevation)*std::cos(azimuth));
    worker->submit(PreviewRequest{revision,source,materialId,assets,eye});
}
void MaterialPreview::receive(quint64 version,QImage image,int count,const QString &error) {
    if((version && version!=revision) || !allowed || !isVisible())return;
    if(!error.isEmpty()) message=tr("预览失败：%1 · 点击“重试”").arg(error);
    else {frame=image;frameSamples=count;frameRevision=version;message=tr("固定摄影棚 · %1 / 64 spp").arg(count);}
    update();if(stateChanged)stateChanged();
}
void MaterialPreview::retry() {invalidate();}
void MaterialPreview::resetView() {azimuth=.35f;elevation=.18f;distance=4.6f;invalidate();}
void MaterialPreview::paintEvent(QPaintEvent *) {
    QPainter p(this);p.fillRect(rect(),QColor("#141c27"));
    if(!frame.isNull()) {
        auto imageSize=frame.size().scaled(this->size(),Qt::KeepAspectRatio);QRect target(QPoint((width()-imageSize.width())/2,(height()-imageSize.height())/2),imageSize);
        p.setRenderHint(QPainter::SmoothPixmapTransform);p.drawImage(target,frame);
    }
    p.setPen(QColor("#aebdce"));p.drawText(rect().adjusted(14,14,-14,-14),Qt::AlignBottom|Qt::AlignHCenter|Qt::TextWordWrap,message);
    if(frame.isNull())p.drawText(rect().adjusted(20,20,-20,-55),Qt::AlignCenter|Qt::TextWordWrap,message);
}
void MaterialPreview::mousePressEvent(QMouseEvent *e) {if(e->button()==Qt::LeftButton){dragging=true;lastMouse=e->pos();e->accept();}}
void MaterialPreview::mouseMoveEvent(QMouseEvent *e) {if(dragging){auto delta=e->pos()-lastMouse;lastMouse=e->pos();azimuth-=delta.x()*.008f;elevation=qBound(-.3f,elevation+delta.y()*.008f,1.3f);invalidate();}}
void MaterialPreview::mouseReleaseEvent(QMouseEvent *) {dragging=false;}
void MaterialPreview::wheelEvent(QWheelEvent *e) {distance=qBound(2.6f,distance*std::pow(.9f,e->angleDelta().y()/120.f),9.f);invalidate();e->accept();}
void MaterialPreview::showEvent(QShowEvent *) {invalidate();}
void MaterialPreview::hideEvent(QHideEvent *) {++revision;debounce.stop();if(worker)worker->stop();}
