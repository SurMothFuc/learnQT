#include "learnQT.h"
#include <QApplication>
#include <QTimer>
#include <QFile>
#include <QElapsedTimer>
#include <QAbstractButton>
#include <QJsonDocument>
#include <QDebug>

namespace {
void answerDialog(QMessageBox::StandardButton answer) {
    auto poll=std::make_shared<std::function<void(int)>>();
    std::weak_ptr<std::function<void(int)>> weak=poll;
    *poll=[weak,answer](int remaining) {
        for(auto widget:QApplication::topLevelWidgets()) {
            auto box=qobject_cast<QMessageBox*>(widget);
            if(box && box->isVisible() && box->button(answer)) { box->button(answer)->click(); return; }
        }
        if(remaining>0) if(auto strong=weak.lock()) QTimer::singleShot(20,[strong,remaining] { (*strong)(remaining-1); });
    };
    QTimer::singleShot(20,[poll] { (*poll)(1000); });
}
}
void learnQT::configureSceneRegression()
{
    const auto args=QCoreApplication::arguments(); const int option=args.indexOf("--scene-switch-regression");
    if(option<0 || option+1>=args.size()) return;
    struct State { int stage=0,frames=0; QImage before; QElapsedTimer time; };
    auto state=std::make_shared<State>(); state->time.start();
    const QString output=QFileInfo(args[option+1]).absoluteFilePath(); QDir().mkpath(output);
    connect(ui.openGLWidget,&GLWidget::framePresented,this,[state] { ++state->frames; });
    auto timer=new QTimer(this); timer->setInterval(100);
    connect(timer,&QTimer::timeout,this,[this,state,output,timer] {
        auto fail=[timer](const QString& message) { std::cerr << "UI scene regression: " << message.toStdString() << std::endl; timer->stop(); QCoreApplication::exit(5); };
        if(state->time.elapsed()>240000) { fail("Timed out"); return; }
        if(m_loading) { state->frames=0; return; }
        if(state->frames<96) return;
        auto& scene=Scene::getInstance();
        const QString bedroom=QString::fromStdString(getResourcePath("scenes/bedroom.scene.json"));
        const QString lantern=QString::fromStdString(getResourcePath("scenes/lantern.scene.json"));
        auto select=[this](const QString& path) {
            const int index=m_sceneList->findData(path); m_sceneList->setCurrentIndex(index);
            QMetaObject::invokeMethod(m_sceneList,"activated",Qt::DirectConnection,Q_ARG(int,index));
        };
        if(state->stage==0) {
            if(scene.textures.size()!=4 || m_sceneDirty) { fail("Initial bedroom state is wrong"); return; }
            state->before=ui.openGLWidget->grabFramebuffer(); state->before.save(output+"/bedroom-before.png");
            { QMutexLocker lock(&param_mutex); scene.camera.processMousePan(3,2); }
            ui.openGLWidget->markSceneDirty(SceneDirtyFlag::Camera);
            answerDialog(QMessageBox::Cancel); select(lantern);
            if(m_loading || !m_sceneDirty || scene.document.filePath!=bedroom || m_sceneList->currentData().toString()!=bedroom) { fail("Cancel changed the scene or discarded dirty state"); return; }
            answerDialog(QMessageBox::Discard); select(lantern);
            if(!m_loading || ui.openGLWidget->isEnabled() || ui.scrollArea->isEnabled()) { fail("Loading did not disable conflicting operations"); return; }
            state->stage=1; state->frames=0;
        } else if(state->stage==1) {
            if(scene.textures.size()!=7 || scene.document.filePath!=lantern || m_sceneDirty) { fail("Lantern switch failed or inherited bedroom textures"); return; }
            ui.openGLWidget->grabFramebuffer().save(output+"/lantern.png");
            QFile bad(output+"/corrupt.scene.json"); bad.open(QIODevice::WriteOnly); bad.write("{broken"); bad.close();
            answerDialog(QMessageBox::Ok); beginSceneLoad(bad.fileName());
            state->stage=2; state->frames=0;
        } else if(state->stage==2) {
            if(scene.document.filePath!=lantern || scene.textures.size()!=7 || m_sceneDirty) { fail("Failed load destroyed the original scene"); return; }
            // Save prompt writes only a disposable test document, never a preset.
            QString error;
            { QMutexLocker lock(&param_mutex); if(!scene.saveScene(output+"/saved-lantern.scene.json",error)) { fail(error); return; } }
            refreshScenes(); ui.roughnessSlider->setValue(43);
            if(!m_sceneDirty) { fail("Material adjustment did not mark scene dirty"); return; }
            // Verify the close confirmation can be cancelled.
            answerDialog(QMessageBox::Cancel); QCloseEvent close; QApplication::sendEvent(this,&close);
            if(close.isAccepted()) { fail("Close ignored Cancel"); return; }
            answerDialog(QMessageBox::Save); select(bedroom);
            state->stage=3; state->frames=0;
        } else {
            if(scene.textures.size()!=4 || scene.document.filePath!=bedroom || m_sceneDirty) { fail("Return to bedroom retained the previous scene state"); return; }
            SceneDocument saved; QString error;
            if(!SceneDocument::loadScene(output+"/saved-lantern.scene.json",saved,error) || std::abs(saved.root["materials"].toArray()[0].toObject()["roughness"].toDouble()-.43)>.001) { fail("Save prompt did not persist material changes"); return; }
            const auto after=ui.openGLWidget->grabFramebuffer(); after.save(output+"/bedroom-after.png");
            const auto a=state->before.scaled(64,64).convertToFormat(QImage::Format_RGB32),b=after.scaled(64,64).convertToFormat(QImage::Format_RGB32);
            double difference=0;
            for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
                auto p=a.pixel(x,y),q=b.pixel(x,y); difference+=std::abs(qRed(p)-qRed(q))+std::abs(qGreen(p)-qGreen(q))+std::abs(qBlue(p)-qBlue(q));
            }
            difference/=64*64*3;
            if(difference>18) { fail(QString("Bedroom pixels changed after roundtrip: %1").arg(difference)); return; }
            grab().save(output+"/scene-management-ui.png");
            std::cout << "UI scene switch, Save/Discard/Cancel, close cancellation, failed-load rollback and render history isolation passed. Mean pixel difference=" << difference << std::endl;
            timer->stop(); QCoreApplication::exit(0);
        }
    });
    timer->start();
}
