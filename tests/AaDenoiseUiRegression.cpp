#include "learnQT.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QSettings>
#include <QTimer>
#include <QWheelEvent>
#include <cmath>
#include <iostream>

// Runs through BackgroundTestSession on a private Windows desktop.
void learnQT::configureAaDenoiseRegression()
{
    const auto args=QCoreApplication::arguments();
    const int option=args.indexOf("--aa-denoise-ui-regression");
    if(option<0 || option+1>=args.size()) return;
    const auto output=QFileInfo(args[option+1]).absoluteFilePath(); QDir().mkpath(output);
    struct State {
        QElapsedTimer clock,phaseClock; int phase=0,fresh=0,startFresh=0,undoIndex=0;
        RenderStats stats; QPoint start,last; QString object; QMatrix4x4 original;
        QJsonArray observations; int cameraFrames=0,objectFrames=0; double acceptance=0;
    };
    auto state=std::make_shared<State>();state->clock.start();state->phaseClock.start();
    connect(viewport,&GLWidget::freshFramePresented,this,[state]{++state->fresh;});
    connect(viewport,&GLWidget::renderThreadReady,this,[this,state]{
        connect(viewport->renderThread(),&RenderThread::statsReady,this,[state](RenderStats s){
            state->stats=s;
            if(state->phase==2 || state->phase==4) state->acceptance=std::max(state->acceptance,s.historyAcceptance);
            state->observations.append(QJsonObject{{"phase",state->phase},{"timeMs",state->clock.elapsed()},
                {"version",double(s.version)},{"spp",s.samples},{"published",double(s.publishedFrames)},
                {"completed",double(s.completedRounds)},{"completedFps",s.completedFps},{"publishedFps",s.publishedFps},{"denoiseRounds",double(s.denoiseRounds)},{"acceptance",s.historyAcceptance},
                {"gpuDenoiseMs",s.realtimeDenoiseMs},{"mode",s.denoiseMode},{"raster",s.rasterActive}});
        });
    });
    auto timer=new QTimer(this);timer->setInterval(25);
    connect(timer,&QTimer::timeout,this,[this,state,timer,output]{
        auto finish=[&](QString error){
            QFile f(output+"/report.json");f.open(QIODevice::WriteOnly);
            f.write(QJsonDocument(QJsonObject{{"error",error},{"cameraFreshFrames",state->cameraFrames},
                {"objectFreshFrames",state->objectFrames},{"maxMotionAcceptance",state->acceptance},
                {"dpi",viewport->devicePixelRatioF()},{"width",viewport->width()},{"height",viewport->height()},
                {"observations",state->observations}}).toJson());
            std::cout<<"AA/denoise UI: "<<error.toStdString()<<std::endl;
            timer->stop();QCoreApplication::exit(error.isEmpty()?0:17);
        };
        if(state->clock.elapsed()>90000){finish("Timeout phase "+QString::number(state->phase));return;}
        if(m_loading || !viewport->renderThread())return;
        auto next=[&]{++state->phase;state->phaseClock.restart();state->startFresh=state->fresh;
            std::cout<<"AA/denoise UI phase "<<state->phase<<std::endl;};
        auto mouse=[&](QEvent::Type type,QPoint point,Qt::KeyboardModifiers modifiers){
            QMouseEvent e(type,point,type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,modifiers);
            QApplication::sendEvent(viewport,&e);state->last=point;
        };
        auto mode=[&](int i){previewDetailPanel->findChild<QComboBox*>("previewDenoiseMode")->setCurrentIndex(i);};
        auto wheel=[&]{
            QWheelEvent e(QPointF(viewport->rect().center()),QPointF(viewport->mapToGlobal(viewport->rect().center())),
                QPoint(),QPoint(0,5),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QApplication::sendEvent(viewport,&e);
        };
        auto interaction=[&](int i){previewDetailPanel->findChild<QComboBox*>("interactionMode")->setCurrentIndex(i);};
        if(state->phase==0){
            navigateWorkspace(WorkspacePage::Scene);resize(1366,768);
            // Keep the GPU workload at 512x384 physical pixels across DPI settings.
            const qreal dpi=viewport->devicePixelRatioF();
            viewport->setFixedSize(qRound(512/dpi),qRound(384/dpi));
            auto s=editor->document.settings();s.renderLow=false;s.maxRenderFrames=0;s.maxBounces=4;
            s.tileSize=64;s.useTileRendering=true;s.antialiasing=true;s.denoise=true;s.denoiseMode=DenoiseMode::Realtime;
            s.interactionMode=RenderParams::InteractionKeepPathtrace;commitPreviewSettings(s);showPreviewSettingsDialog();
            auto rr=previewDetailPanel->findChild<QSpinBox*>("previewRrMinDepth");
            if(!rr || !outputRrMinDepth){finish("RR controls missing");return;}
            rr->setValue(5);
            if(editor->document.settings().rrMinDepth!=5){finish("Preview RR control not connected");return;}
            outputRrMinDepth->setValue(7);QMetaObject::invokeMethod(outputRrMinDepth,"editingFinished");
            if(editor->document.root["output"].toObject()["rrMinDepth"].toInt()!=7 || editor->document.settings().rrMinDepth!=5) {
                finish("Output RR control not connected or altered preview");return;
            }
            rr->setValue(3);outputRrMinDepth->setValue(3);QMetaObject::invokeMethod(outputRrMinDepth,"editingFinished");
            next();return;
        }
        const auto &s=state->stats;
        if(!s.denoiseError.isEmpty()){finish(s.denoiseError);return;}
        if(state->phase==1){
            if(s.version!=viewport->sceneVersion() || !s.denoisedVersion || state->fresh-state->startFresh<2)return;
            previewDialog->grab().save(output+"/settings-before.png");previewDialog->hide();
            viewport->grabFramebuffer().save(output+"/camera-before.png");
            state->start=viewport->rect().center();mouse(QEvent::MouseButtonPress,state->start,Qt::AltModifier);next();
        }else if(state->phase==2 || state->phase==4){
            const bool camera=state->phase==2;
            const int elapsed=int(state->phaseClock.elapsed());
            const QPoint point=state->start+QPoint(qRound(12*std::sin(elapsed*.004)),camera?qRound(5*std::sin(elapsed*.003)):0);
            mouse(QEvent::MouseMove,point,camera?Qt::AltModifier:Qt::NoModifier);
            if(elapsed<2500)return;
            mouse(QEvent::MouseButtonRelease,state->last,camera?Qt::AltModifier:Qt::NoModifier);
            const int frames=state->fresh-state->startFresh;
            if(frames<3){finish("Continuous input starved complete images");return;}
            if(camera)state->cameraFrames=frames;else{
                state->objectFrames=frames;
                if(editor->undo.index()!=state->undoIndex+1 || sceneMatrix(editor->node(state->object)["transform"])==state->original){
                    finish("Object drag did not produce one undoable transform");return;}
            }
            next();
        }else if(state->phase==3){
            if(s.version!=viewport->sceneVersion() || !s.denoisedVersion)return;
            viewport->grabFramebuffer().save(output+"/camera-after.png");
            for(auto value:editor->document.root["objects"].toArray()){
                const auto o=value.toObject();if(o["type"]=="mesh" || o["type"]=="model") {state->object=o["id"].toString();break;}
            }
            if(state->object.isEmpty()){
                const auto ids=editor->localBounds.keys();if(!ids.isEmpty())state->object=ids.front();
            }
            if(state->object.isEmpty()){finish("Missing drag fixture");return;}
            editor->select({state->object},state->object);viewport->setTool(GLWidget::Translate);
            const auto center=editor->bounds({state->object}).center();
            const float size=(viewport->camera.position-center).length()*2*std::tan(viewport->camera.zoom*3.14159265/360)*80/viewport->height();
            QMatrix4x4 projection;projection.perspective(viewport->camera.zoom,float(viewport->width())/viewport->height(),.0001f,1e9f);
            auto q=projection*viewport->camera.getViewMatrix()*QVector4D(center+QVector3D(size*.65f,0,0),1);q/=q.w();
            state->start=QPoint(qRound((q.x()+1)*viewport->width()*.5),qRound((1-q.y())*viewport->height()*.5));
            state->original=sceneMatrix(editor->node(state->object)["transform"]);state->undoIndex=editor->undo.index();
            viewport->grabFramebuffer().save(output+"/object-before.png");mouse(QEvent::MouseButtonPress,state->start,Qt::NoModifier);next();
        }else if(state->phase==5){
            if(s.version!=viewport->sceneVersion() || !s.denoisedVersion)return;
            viewport->grabFramebuffer().save(output+"/object-after.png");editor->undo.undo();mode(2);next();
        }else if(state->phase==6){
            if(s.denoiseMode!="oidn" || s.oidnMs<=0 || !s.denoisedVersion)return;
            mode(0);next();
        }else if(state->phase==7){
            if(s.denoiseMode!="none" || s.denoisedVersion)return;
            auto aa=previewDetailPanel->findChild<QCheckBox*>("previewAntialiasing");aa->click();
            if(editor->document.settings().antialiasing){finish("AA control did not submit setting");return;}
            aa->click();mode(1);next();
        }else if(state->phase==8){
            if(s.version!=viewport->sceneVersion() || !s.denoisedVersion)return;
            showPreviewSettingsDialog();previewDialog->grab().save(output+"/settings-after.png");previewDialog->hide();
            interaction(1);wheel();next();
        }else if(state->phase==9){
            wheel();if(!s.rasterActive)return;
            if(s.rasterSamples<2){finish("Raster AA failed to use MSAA");return;}
            viewport->grabFramebuffer().save(output+"/raster-msaa.png");interaction(2);wheel();next();
        }else if(state->phase==10){
            wheel();if(s.rasterActive || s.size.width()>200 || !s.denoisedVersion)return;
            viewport->grabFramebuffer().save(output+"/low-resolution.png");interaction(0);next();
        }else if(state->phase==11){
            const QSize expectedSize(qRound(viewport->width()*viewport->devicePixelRatioF()),
                                     qRound(viewport->height()*viewport->devicePixelRatioF()));
            if(s.rasterActive || s.size!=expectedSize || s.version!=viewport->sceneVersion() || !s.denoisedVersion)return;
            navigateWorkspace(WorkspacePage::Render);
            outputWidth->setValue(320);outputHeight->setValue(240);outputSamples->setValue(4);outputBounces->setValue(4);
            outputDenoise->setCurrentIndex(1);outputAntialiasing->setChecked(true);refreshRenderCameras();
            outputDenoise->parentWidget()->grab().save(output+"/output-settings-before.png");
            m_renderFormat->setCurrentIndex(0);addRenderTask();m_renderFormat->setCurrentIndex(1);addRenderTask();
            if(m_renderQueue.size()!=2){finish("Queue capture failed");return;}
            outputDenoise->setCurrentIndex(2);outputAntialiasing->click();
            outputDenoise->parentWidget()->grab().save(output+"/output-settings-after.png");
            for(const auto &item:m_renderQueue)if(!item.request.settings.antialiasing || item.request.settings.effectiveDenoiseMode()!=DenoiseMode::Realtime){finish("Queue settings were not frozen");return;}
            QSettings preferences(QSettings::defaultFormat(),QSettings::UserScope,"learnQT","SceneWorkbench");
            preferences.setValue("workspaceV4/autoExportPath",output);runRenderQueue();next();
        }else if(state->phase==12){
            if(m_queueRunning || m_activeQueueId)return;
            for(const auto &item:m_renderQueue)if(!item.error.isEmpty() || item.samples!=4 || QImage(item.request.outputPath).size()!=QSize(320,240)){
                finish("Realtime PNG/JPEG task failed: "+item.error);return;}
            finish({});
        }
    });timer->start();
}
