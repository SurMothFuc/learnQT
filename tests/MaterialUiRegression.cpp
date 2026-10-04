#include "learnQT.h"
#include "WorkspaceUi.h"
#include "MaterialPreview.h"
#include "MaterialUi.h"
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTimer>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QScrollArea>
#include <QColorDialog>
#include <QMenu>
#include <QKeyEvent>
#include <iostream>

void learnQT::configureMaterialRegression()
{
    const auto args=QCoreApplication::arguments();const int option=args.indexOf("--material-regression");
    if(option<0 || option+1>=args.size())return;
    const auto output=QFileInfo(args[option+1]).absoluteFilePath();QDir().mkpath(output);
    struct State {int phase=0;QString object,other;SceneDocument original;QJsonObject stable;int undo=0,frames=0;quint64 version=0;QImage first;QElapsedTimer clock;RenderStats stats;QSize previewSize{0,0},captureSize;int finalSamples=0;quint64 finalVersion=0;};
    auto state=std::make_shared<State>();state->clock.start();
    auto timings=std::make_shared<QJsonObject>();
    auto previewClock=std::make_shared<QElapsedTimer>();
    connect(viewport,&GLWidget::framePresented,this,[state]{++state->frames;});
    auto watchStats=[this,state] {connect(viewport->renderThread(),&RenderThread::statsReady,this,[state](RenderStats stats){state->stats=stats;});};
    if(viewport->renderThread())watchStats();else connect(viewport,&GLWidget::renderThreadReady,this,watchStats);
    auto modalMonitor = new QTimer(this);modalMonitor->setInterval(50);
    connect(modalMonitor,&QTimer::timeout,this,[] {
        if (auto box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            std::cout << "Unexpected modal: " << box->text().toStdString() << std::endl;
            box->accept();
        }
    });modalMonitor->start();
    auto timer=new QTimer(this);timer->setInterval(100);
    auto finish=[this,state,timer,output,timings](QString error) {
        timer->stop();grab().save(output+(error.isEmpty()?"/final.png":"/failure.png"));
        QFile report(output+"/report.json");if(report.open(QIODevice::WriteOnly))report.write(QJsonDocument(QJsonObject{
            {"passed",error.isEmpty()},{"error",error},{"phase",state->phase},{"width",width()},{"height",height()},
            {"dpi",devicePixelRatioF()},{"compactWindow",QJsonArray{state->captureSize.width(),state->captureSize.height()}},{"previewSize",QJsonArray{state->previewSize.width(),state->previewSize.height()}},
            {"previewSamples",state->finalSamples},{"version",qint64(state->finalVersion)},{"timingsMs",*timings}}).toJson());
        std::cout<<(error.isEmpty()?"Material UI regression passed":error.toStdString())<<std::endl;
        editor->markSaved();m_sceneDirty=false;QCoreApplication::exit(error.isEmpty()?0:19);
    };
    connect(timer,&QTimer::timeout,this,[this,state,finish,output,timings,previewClock] {
        if(state->clock.elapsed()>150000)return finish("Material UI timed out: "+workspace->materialPreview->status());
        auto definition=[this](QString id) {const QString mat=editor->node(id)["material"].toString();
            for(auto v:editor->document.root["materials"].toArray())if(v.toObject()["id"].toString()==mat)return v.toObject();return QJsonObject();};
        auto activate=[this](const char *key,int value) {auto combo=inspector->findChild<QComboBox *>(key);
            if(!combo)return false;combo->setCurrentIndex(combo->findData(value));
            return QMetaObject::invokeMethod(combo,"activated",Qt::DirectConnection,Q_ARG(int,combo->currentIndex()));};
        auto hidden=[this](const char *key) {auto row=inspector->findChild<QWidget *>(QString(key)+"Row");return !row || row->isHidden();};
        if(state->phase==0) {
            navigateWorkspace(WorkspacePage::Scene);
            beginSceneLoad(QString::fromStdString(getResourcePath("scenes/lantern.scene.json")));state->phase=1;return;
        }
        if(m_loading || editor->busy)return;
        if(state->phase==1) {
            if(!viewport->renderThread() || editor->document.root["objects"].toArray().size()<2)return;
            resize(1600,900);navigateWorkspace(WorkspacePage::Material);state->original=editor->document;
            const auto objects=editor->document.root["objects"].toArray();
            state->object=objects[0].toObject()["id"].toString();state->other=objects[1].toObject()["id"].toString();
            editor->select({state->object},state->object);inspector->browseMaterial(editor->node(state->object)["material"].toString());
            for(auto button:inspector->findChildren<QToolButton *>())if(button->objectName().startsWith("materialGroup_"))button->setChecked(true);
            for(auto key:{"alphaMode","mediumtype"})if(inspector->findChild<QComboBox *>(key)->count()!=4)return finish("Mode includes a false fifth option");
            state->phase=10;return;
        }
        if(state->phase==10) {
            if(state->stats.version!=viewport->sceneVersion() || state->stats.samples<1 || state->frames<1 ||
                state->stats.size!=QSize(qRound(viewport->width()*viewport->devicePixelRatioF()),qRound(viewport->height()*viewport->devicePixelRatioF())))return;
            grab().save(output+"/before-operations.png");viewport->grabFramebuffer().save(output+"/scene-viewport.png");
            if(!activate("normalMapFlipY",1) || !definition(state->object)["normalMapFlipY"].toBool())return finish("Normal direction not connected");
            QTimer::singleShot(0,this,[] {
                if(auto menu=qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
                    menu->setActiveAction(menu->actions().last());
                    QKeyEvent key(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&key);
                }
            });
            inspector->findChild<QPushButton *>("texture_normal")->click();
            if(!definition(state->object)["textures"].toObject()["normal"].toString().isEmpty() || !hidden("normalScale") || !hidden("normalMapFlipY"))return finish("Clear normal texture did not update controls");
            editor->undo.undo();
            if(hidden("normalScale"))return finish("Texture undo did not restore controls");
            inspector->findChild<MixedSpin *>("clearcoat")->setValue(.5);
            if(hidden("clearcoatGloss"))return finish("Coat controls not enabled");
            inspector->findChild<MixedSpin *>("clearcoat")->setValue(0);
            if(!hidden("clearcoatGloss"))return finish("Inactive coat controls visible");
            for(int mode=0;mode<4;++mode) {
                std::cout << "Alpha mode " << mode << std::endl;
                if(!activate("alphaMode",mode) || definition(state->object)["alphaMode"].toInt()!=mode)return finish("Alpha mode signal not connected");
                if(hidden("opacity")!=(mode!=Mask && mode!=Blend) || hidden("alphaCutoff")!=(mode!=Mask) ||
                    inspector->findChild<QWidget *>("opacityLabel")->isHidden()!=hidden("opacity"))return finish("Alpha rows/labels visibility incorrect");
            }
            for(int mode=0;mode<4;++mode) {
                std::cout << "Medium type " << mode << std::endl;
                if(!activate("mediumtype",mode) || definition(state->object)["mediumtype"].toInt()!=mode)return finish("Medium type signal not connected");
                if(hidden("mediumColor")!=(mode==None) || hidden("mediumDensity")!=(mode==None) || hidden("mediumAnisotropy")!=(mode!=Scatter))return finish("Medium rows visibility incorrect");
            }
            activate("mediumtype",None);activate("alphaMode",Mask);
            std::cout << "Alpha persistence" << std::endl;
            inspector->findChild<MixedSpin *>("opacity")->setValue(.7);inspector->findChild<MixedSpin *>("alphaCutoff")->setValue(.4);
            activate("alphaMode",Opaque);activate("alphaMode",Mask);
            if(std::abs(definition(state->object)["opacity"].toDouble()-.7)>.0001 || std::abs(definition(state->object)["alphaCutoff"].toDouble()-.4)>.0001)return finish("Hiding destroyed alpha settings");
            auto slider=inspector->findChild<QSlider *>("roughnessSlider");const double old=definition(state->object)["roughness"].toDouble();
            std::cout << "Slider undo" << std::endl;
            slider->setValue(230);QMetaObject::invokeMethod(slider,"sliderReleased");
            if(std::abs(definition(state->object)["roughness"].toDouble()-.23)>.0001)return finish("Roughness slider did not edit");
            inspector->findChild<MixedSpin *>("roughness")->setFocus();
            editor->undo.undo();if(std::abs(definition(state->object)["roughness"].toDouble()-old)>.0001 ||
                std::abs(inspector->findChild<MixedSpin *>("roughness")->value()-old)>.0001)return finish("Focused slider undo failed");editor->undo.redo();
            editor->materialScope=editor->node(state->object)["material"].toString();
            std::cout << "Emission" << std::endl;
            editor->setMaterialField("emissive",QJsonArray{10,5,0});
            QTimer::singleShot(0,this,[] {
                if(auto dialog=qobject_cast<QColorDialog *>(QApplication::activeModalWidget())) {
                    dialog->setCurrentColor(materialDisplayColor({0,.5f,1}));dialog->accept();
                }
            });
            inspector->findChild<QPushButton *>("emissive")->click();
            auto chosenEmission=sceneVector(definition(state->object)["emissive"]);
            // The QColorDialog's RGB controls quantize the display color to 8 bits.
            if(std::abs(chosenEmission.z()-10)>.001f || (chosenEmission-QVector3D(0,5,10)).length()>.05f)return finish("Changing emission color lost strength");
            editor->setMaterialFields(QJsonObject{{"emissionStrength",0}},{state->object});
            editor->setMaterialFields(QJsonObject{{"emissionStrength",2}},{state->object});
            if((sceneVector(definition(state->object)["emissive"])-QVector3D(0,1,2)).length()>.01f)return finish("Zero strength lost hue");
            editor->select({state->object,state->other},state->object);
            std::cout << "Mixed fields" << std::endl;
            editor->setMaterialFields(QJsonObject{{"alphaMode",Opaque},{"mediumtype",None}},{state->object});
            editor->setMaterialFields(QJsonObject{{"alphaMode",Mask},{"mediumtype",Scatter}},{state->other});
            findChild<QPushButton *>("materialEditSelected")->click();
            if(inspector->findChild<QComboBox *>("alphaMode")->currentIndex()!=-1 || inspector->findChild<QComboBox *>("mediumtype")->currentIndex()!=-1)return finish("Mixed selection not indicated");
            const auto inactive=definition(state->object);
            inspector->findChild<MixedSpin *>("opacity")->setValue(.63);inspector->findChild<MixedSpin *>("mediumAnisotropy")->setValue(.31);
            if(definition(state->object)!=inactive || std::abs(definition(state->other)["opacity"].toDouble()-.63)>.0001 || std::abs(definition(state->other)["mediumAnisotropy"].toDouble()-.31)>.0001)return finish("Mixed edit changed inactive objects");
            editor->submit(state->original,"Restore material fixture",EditorController::MaterialChange);
            std::cout << "Restore" << std::endl;
            editor->select({state->object},state->object);workspace->batchMaterialEdit=false;
            inspector->browseMaterial(editor->node(state->object)["material"].toString());
            editor->setMaterialFields(QJsonObject{{"baseColor",QJsonArray{.8,.12,.05}},{"emissive",QJsonArray{0,0,0}},{"metallic",0},{"roughness",.3}}, {state->object});
            inspector->browseMaterial(editor->node(state->object)["material"].toString());
            state->stable=editor->document.root;state->undo=editor->undo.index();
            previewClock->start();findChild<QPushButton *>("materialBallMode")->click();state->version=workspace->materialPreview->requestVersion();state->phase=2;return;
        }
        if(state->phase==2) {
            auto preview=workspace->materialPreview;
            if(preview->status().startsWith("预览失败"))return finish(preview->status());
            if(preview->samples()>0 && preview->displayedVersion()==preview->requestVersion() && !timings->contains("coldFirstFrame"))
                (*timings)["coldFirstFrame"]=previewClock->elapsed();
            if(preview->samples()!=64 || preview->displayedVersion()!=preview->requestVersion())return;
            (*timings)["cold64spp"]=previewClock->elapsed();
            if(preview->image().size()!=QSize(384,384) || editor->document.root!=state->stable || editor->undo.index()!=state->undo)return finish("Material ball changed document or result invalid");
            state->first=preview->image();state->first.save(output+"/ball-red.png");grab().save(output+"/material-ball.png");
            // Continuous edits must cancel old requests and publish only the last one.
            previewClock->restart();for(int i=0;i<8;++i)editor->setMaterialFields(QJsonObject{{"baseColor",QJsonArray{.03,.2,double(.3+i*.07)}}},{state->object},7000);
            inspector->browseMaterial(editor->node(state->object)["material"].toString());
            state->stable=editor->document.root;state->undo=editor->undo.index();state->phase=3;return;
        }
        if(state->phase==3) {
            auto preview=workspace->materialPreview;
            if(preview->samples()>0 && preview->displayedVersion()==preview->requestVersion() && !timings->contains("editFirstFrame"))
                (*timings)["editFirstFrame"]=previewClock->elapsed();
            if(preview->samples()!=64 || preview->displayedVersion()!=preview->requestVersion())return;
            (*timings)["edit64spp"]=previewClock->elapsed();
            if(preview->image()==state->first)return finish("Material change did not change ball image");
            preview->image().save(output+"/ball-blue.png");
            QMouseEvent press(QEvent::MouseButtonPress,QPointF(preview->rect().center()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(preview,&press);
            QMouseEvent move(QEvent::MouseMove,QPointF(preview->rect().center()+QPoint(35,10)),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(preview,&move);
            QMouseEvent release(QEvent::MouseButtonRelease,QPointF(preview->rect().center()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(preview,&release);
            QWheelEvent wheel(QPointF(preview->rect().center()),QPointF(preview->rect().center()),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QApplication::sendEvent(preview,&wheel);
            if(editor->document.root!=state->stable || editor->undo.index()!=state->undo)return finish("Orbit changed editing scene");
            m_queueRunning=true;preview->setRenderingAllowed(false,"Regression pause");state->version=preview->requestVersion();state->phase=4;return;
        }
        if(state->phase==4) {
            if(workspace->materialPreview->requestVersion()!=state->version)return finish("Pause did not stay paused");
            previewClock->restart();m_queueRunning=false;workspace->materialPreview->setRenderingAllowed(true);findChild<QPushButton *>("materialBallReset")->click();
            resize(1366,768);state->phase=5;return;
        }
        if(state->phase==5) {
            auto preview=workspace->materialPreview;if(preview->samples()!=64 || preview->displayedVersion()!=preview->requestVersion())return;
            (*timings)["reset64spp"]=previewClock->elapsed();
            state->previewSize=preview->image().size();state->finalSamples=preview->samples();state->finalVersion=preview->displayedVersion();
            if(width()!=1366 || height()!=768)return finish("Compact window does not fit requested size");
            state->captureSize=size();
            grab().save(output+"/compact.png");findChild<QPushButton *>("materialSceneMode")->click();
            if(views->currentIndex()!=0 || editor->document.root!=state->stable || editor->undo.index()!=state->undo)return finish("Scene switch changed document");
            state->phase=6;return;
        }
        if(state->phase==6) {
            if(width()!=1366 || height()!=768) {
                QJsonArray widths;
                for(auto widget:findChildren<QWidget *>())if(widget->isVisible() && widget->minimumSizeHint().width()>300)
                    widths.append(QJsonObject{{"name",widget->objectName()},{"class",widget->metaObject()->className()},
                        {"minimumHint",widget->minimumSizeHint().width()},{"minimum",widget->minimumWidth()},{"width",widget->width()}});
                QFile diagnostics(output+"/width-diagnostics.json");if(diagnostics.open(QIODevice::WriteOnly))diagnostics.write(QJsonDocument(widths).toJson());
                return finish("Scene preview enlarged the compact material workspace");
            }
            grab().save(output+"/compact-scene.png");
            navigateWorkspace(WorkspacePage::Settings);navigateWorkspace(WorkspacePage::Material);
            editor->select({});workspace->materialFilter->setCurrentIndex(1);
            inspector->browseMaterial(editor->document.root["materials"].toArray().first().toObject()["id"].toString());
            if(inspector->findChild<MixedSpin *>("roughness")->isEnabled())return finish("Read-only material became editable");
            editor->select({state->object},state->object);
            inspector->browseMaterial(editor->node(state->object)["material"].toString());
            editor->renderLocked=true;inspector->refresh();
            const auto lockedDocument=editor->document.root;bool textureMenuLocked=false;
            QTimer::singleShot(0,this,[&textureMenuLocked] {
                if(auto menu=qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
                    textureMenuLocked=!menu->actions()[1]->isEnabled() && !menu->actions()[2]->isEnabled();
                    QKeyEvent key(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(menu,&key);
                }
            });
            inspector->findChild<QPushButton *>("texture_baseColor")->click();
            editor->setMaterialFields(QJsonObject{{"roughness",.9}},{state->object});
            editor->renderLocked=false;
            if(!textureMenuLocked || editor->document.root!=lockedDocument)return finish("Locked material accepted edits");
            finish({});
        }
    });timer->start();
}
