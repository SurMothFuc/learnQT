#include "learnQT.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QPointF>
#include <QPushButton>
#include <QTimer>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QLabel>
#include <QStatusBar>
#include <QScrollBar>

#include <algorithm>
#include <array>
#include <cmath>


learnQT::learnQT(QWidget *parent)
    : QMainWindow(parent)
{
    Scene::getInstance();
    ui.setupUi(this);    

    const RenderParams::Snapshot initialRenderParams = RenderParams::instance().snapshot();
    ui.maxBouncesSpinBox->setValue(initialRenderParams.maxBounces);
    ui.maxRenderFramesSpinBox->setValue(initialRenderParams.maxRenderFrames);
    ui.modelPathLabel->setText(QString::fromStdString(Scene::getInstance().currentModelPath()));

    connect( ui.SaveImageButton,SIGNAL(clicked(bool)), this, SLOT(saveGLImage()));
    connect(ui.LoadModelButton, SIGNAL(clicked(bool)), this, SLOT(loadModel()));
    connect( ui.pushButton,SIGNAL(clicked(bool)), this, SLOT(upoff()));
    connect(ui.roughnessSlider, SIGNAL(valueChanged(int)), this, SLOT(roughnessSliderUp()));
    connect(ui.metallicSlider, SIGNAL(valueChanged(int)), this, SLOT(metallicSliderUp()));
    connect(ui.subsurfaceSlider, SIGNAL(valueChanged(int)), this, SLOT(subsurfaceSliderUp()));
    connect(ui.specularTintSlider, SIGNAL(valueChanged(int)), this, SLOT(specularTintSliderUp()));
    connect(ui.sheenSlider, SIGNAL(valueChanged(int)), this, SLOT(sheenSliderUp()));
    connect(ui.sheenTintSlider, SIGNAL(valueChanged(int)), this, SLOT(sheenTintSliderUp()));
    connect(ui.clearcoatSlider, SIGNAL(valueChanged(int)), this, SLOT(clearcoatSliderUp()));
    connect(ui.clearcoatGlossSlider, SIGNAL(valueChanged(int)), this, SLOT(clearcoatGlossSliderUp()));
    connect(ui.IORSlider, SIGNAL(valueChanged(int)), this, SLOT(IORSliderUp()));
    connect(ui.transmissionSlider, SIGNAL(valueChanged(int)), this, SLOT(transmissionSliderUp()));

    connect(ui.DeNoisecheckBox, SIGNAL(toggled(bool)), this, SLOT(DenoiseCheckBoxChanged()));
    connect(ui.useEnvironmentMapcheckBox, SIGNAL(toggled(bool)), this, SLOT(useEnvironmentMapCheckBoxChanged()));
    connect(ui.maxBouncesSpinBox, SIGNAL(valueChanged(int)), this, SLOT(maxBouncesSpinBoxChanged(int)));
    connect(ui.maxRenderFramesSpinBox, SIGNAL(valueChanged(int)), this, SLOT(maxRenderFramesSpinBoxChanged(int)));

    setupSceneControls();
    configureRegressionCapture();
#ifdef SCENE_TESTING
    configureSceneRegression();
#endif
}

void learnQT::configureRegressionCapture()
{
    const QStringList arguments = QCoreApplication::arguments();
    const int outputArgument = arguments.indexOf(QStringLiteral("--render-regression"));
    if (outputArgument < 0 || outputArgument + 1 >= arguments.size()) {
        return;
    }

    m_regressionOutputPath = QFileInfo(arguments[outputArgument + 1]).absoluteFilePath();
    m_validateLanternRegression = arguments.contains(QStringLiteral("--regression-lantern"));
    const int frameArgument = arguments.indexOf(QStringLiteral("--regression-frames"));
    if (frameArgument >= 0 && frameArgument + 1 < arguments.size()) {
        bool valid = false;
        const int requestedFrames = arguments[frameArgument + 1].toInt(&valid);
        if (valid) {
            m_regressionTargetFrames = std::max(1, std::min(requestedFrames, 512));
        }
    }

    const bool regressionDenoise = arguments.contains(QStringLiteral("--regression-denoise"));
    RenderParams::instance().setDenoise(regressionDenoise);
    ui.DeNoisecheckBox->setChecked(regressionDenoise);
    connect(ui.openGLWidget, &GLWidget::framePresented, this, [this]() {
        ++m_regressionPresentedFrames;
        if (!m_regressionCaptureQueued && m_regressionPresentedFrames >= m_regressionTargetFrames) {
            m_regressionCaptureQueued = true;
            QTimer::singleShot(0, this, [this]() { captureRegressionFrame(); });
        }
    }, Qt::QueuedConnection);
}

void learnQT::captureRegressionFrame()
{
    const QImage image = ui.openGLWidget->grabFramebuffer().convertToFormat(QImage::Format_RGB32);
    const qint64 pixelCount = static_cast<qint64>(image.width()) * image.height();
    double sum = 0.0;
    double sumSquared = 0.0;
    qint64 nonBlackPixels = 0;
    qint64 nonWhitePixels = 0;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const double luminance = static_cast<double>(qGray(row[x]));
            sum += luminance;
            sumSquared += luminance * luminance;
            nonBlackPixels += luminance >= 4.0 ? 1 : 0;
            nonWhitePixels += luminance <= 251.0 ? 1 : 0;
        }
    }

    const double mean = pixelCount > 0 ? sum / pixelCount : 0.0;
    const double variance = pixelCount > 0
        ? std::max(0.0, sumSquared / pixelCount - mean * mean)
        : 0.0;
    const double standardDeviation = std::sqrt(variance);
    const double nonBlackRatio = pixelCount > 0 ? static_cast<double>(nonBlackPixels) / pixelCount : 0.0;
    const double nonWhiteRatio = pixelCount > 0 ? static_cast<double>(nonWhitePixels) / pixelCount : 0.0;
    double lanternBrightYellowRatio = 1.0;
    if (m_validateLanternRegression && !image.isNull()) {
        const int x0 = static_cast<int>(0.642 * image.width());
        const int x1 = static_cast<int>(0.687 * image.width());
        const int y0 = static_cast<int>(0.360 * image.height());
        const int y1 = static_cast<int>(0.440 * image.height());
        qint64 brightYellowPixels = 0;
        qint64 sampledPixels = 0;
        for (int y = y0; y < y1; ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
            for (int x = x0; x < x1; ++x) {
                const int red = qRed(row[x]);
                const int green = qGreen(row[x]);
                const int blue = qBlue(row[x]);
                brightYellowPixels += red > 150 && green > 130 && blue * 10 < green * 7 ? 1 : 0;
                ++sampledPixels;
            }
        }
        lanternBrightYellowRatio = sampledPixels > 0
            ? static_cast<double>(brightYellowPixels) / sampledPixels
            : 0.0;
    }
    const std::array<QPointF, 9> keyLocations = {
        QPointF(0.2, 0.2), QPointF(0.5, 0.2), QPointF(0.8, 0.2),
        QPointF(0.2, 0.5), QPointF(0.5, 0.5), QPointF(0.8, 0.5),
        QPointF(0.2, 0.8), QPointF(0.5, 0.8), QPointF(0.8, 0.8)
    };
    int keyMin = 255;
    int keyMax = 0;
    int keyNonBlack = 0;
    for (const QPointF& location : keyLocations) {
        const int x = std::min(image.width() - 1, static_cast<int>(location.x() * image.width()));
        const int y = std::min(image.height() - 1, static_cast<int>(location.y() * image.height()));
        const int luminance = image.isNull() ? 0 : qGray(image.pixel(x, y));
        keyMin = std::min(keyMin, luminance);
        keyMax = std::max(keyMax, luminance);
        keyNonBlack += luminance >= 4 ? 1 : 0;
    }

    QDir().mkpath(QFileInfo(m_regressionOutputPath).absolutePath());
    const bool saved = !image.isNull() && image.save(m_regressionOutputPath);
    const bool validImage = image.width() >= 64 && image.height() >= 64 &&
        mean > 2.0 && mean < 253.0 && standardDeviation > 2.0 &&
        nonBlackRatio > 0.02 && nonWhiteRatio > 0.02 &&
        keyNonBlack >= 2 && keyMax - keyMin >= 2 &&
        (!m_validateLanternRegression || lanternBrightYellowRatio > 0.05);

    qInfo() << "Render regression metrics:"
            << "size" << image.size()
            << "mean" << mean
            << "stddev" << standardDeviation
            << "nonBlack" << nonBlackRatio
            << "nonWhite" << nonWhiteRatio
            << "keyRange" << keyMin << keyMax
            << "keyNonBlack" << keyNonBlack
            << "lanternBrightYellow" << lanternBrightYellowRatio
            << "saved" << saved;
    if (!saved || !validImage) {
        qCritical() << "Render regression failed: output is missing, black, white, or effectively constant";
        QCoreApplication::exit(2);
        return;
    }
    QCoreApplication::exit(0);
}

void learnQT::loadModel()
{
    if(m_loading || !confirmDiscard()) return;
    const QString currentPath = QString::fromStdString(Scene::getInstance().currentModelPath());
    const QFileInfo currentInfo(currentPath);
    const QString initialDirectory = currentInfo.isDir() ? currentInfo.absoluteFilePath() : currentInfo.absolutePath();
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Load 3D model"),
        initialDirectory,
        tr("3D models (*.gltf *.glb *.fbx *.obj);;glTF (*.gltf *.glb);;FBX (*.fbx);;Wavefront OBJ (*.obj);;All files (*.*)"));
    if (filePath.isEmpty()) {
        return;
    }

    beginSceneLoad(filePath,true);
}

learnQT::~learnQT()
{
    if(m_loadWorker) { m_loadWorker->wait(); delete m_loadWorker; }
}

void learnQT::setupSceneControls()
{
    ui.scrollArea->setFont(QFont("Microsoft YaHei UI",9));
    ui.scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui.DeNoisecheckBox->setText(tr("降噪"));
    ui.useEnvironmentMapcheckBox->setText(tr("环境贴图"));
    ui.maxBouncesLabel->setText(tr("最大反弹"));
    ui.maxRenderFramesLabel->setText(tr("最大帧数"));
    ui.maxRenderFramesSpinBox->setSpecialValueText(tr("不限"));
    ui.LoadModelButton->setText(tr("导入 / 替换模型…"));
    ui.pushButton->setText(tr("应用材质覆盖"));
    ui.SaveImageButton->setText(tr("保存截图"));
    for(auto edit:ui.scrollArea->findChildren<QLineEdit*>()) edit->setMaximumWidth(72);
    auto group=new QGroupBox(tr("场景"),this);
    group->setObjectName("sceneControls");
    auto layout=new QVBoxLayout(group);
    m_sceneList=new QComboBox(group); m_sceneList->setObjectName("sceneList");
    m_sceneList->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_sceneList->setMinimumContentsLength(14); layout->addWidget(m_sceneList);
    auto button=[&](const QString& name,const QString& text,const std::function<void()>& action) {
        auto b=new QPushButton(text,group); b->setObjectName(name); layout->addWidget(b); connect(b,&QPushButton::clicked,this,action);
    };
    button("openSceneButton",tr("打开场景"),[this] {
        if(!confirmDiscard()) return;
        auto path=QFileDialog::getOpenFileName(this,tr("打开场景"),QString::fromStdString(getResourcePath("scenes")),tr("Scene (*.scene.json *.json)"));
        if(!path.isEmpty()) beginSceneLoad(path);
    });
    button("saveSceneButton",tr("保存"),[this] { saveSceneDocument(); });
    button("saveSceneAsButton",tr("另存为"),[this] { saveSceneDocument(true); });
    button("reloadSceneButton",tr("重新加载场景"),[this] {
        if(!confirmDiscard()) return;
        const auto& s=Scene::getInstance();
        if(s.document.filePath.isEmpty()) beginSceneLoad(QString::fromStdString(s.currentModelPath()),true);
        else beginSceneLoad(s.document.filePath);
    });
    button("exportSceneButton",tr("导出便携包"),[this] {
        const auto path=QFileDialog::getExistingDirectory(this,tr("选择新建或空文件夹"));
        if(path.isEmpty()) return;
        SceneDocument document;
        { QMutexLocker lock(&param_mutex); document=Scene::getInstance().snapshotDocument(); }
        setLoading(true); statusBar()->showMessage(tr("正在复制并验证便携包…"));
        auto error=std::make_shared<QString>(); auto success=std::make_shared<bool>(false);
        m_loadWorker=QThread::create([document,path,error,success] { *success=document.exportScenePackage(path,*error); });
        connect(m_loadWorker,&QThread::finished,this,[this,error,success,path] {
            m_loadWorker->deleteLater(); m_loadWorker=nullptr; setLoading(false);
            if(!*success) QMessageBox::critical(this,tr("导出失败"),*error);
            else statusBar()->showMessage(tr("已导出：%1（未改变当前场景保存位置）").arg(path));
        });
        m_loadWorker->start();
    });
    ui.verticalLayout_3->insertWidget(0,group);
    connect(m_sceneList,QOverload<int>::of(&QComboBox::activated),this,[this](int index) {
        const auto path=m_sceneList->itemData(index).toString();
        if(path.isEmpty()) return;
        if(!confirmDiscard()) { refreshScenes(); return; }
        beginSceneLoad(path);
    });
    connect(ui.openGLWidget,&GLWidget::sceneEdited,this,&learnQT::setSceneDirty);
    auto& p=RenderParams::instance();
    connect(&p,&RenderParams::denoiseChanged,this,&learnQT::setSceneDirty);
    connect(&p,&RenderParams::useEnvironmentMapChanged,this,&learnQT::setSceneDirty);
    connect(&p,&RenderParams::useTileRenderingChanged,this,&learnQT::setSceneDirty);
    connect(&p,&RenderParams::tileSizeChanged,this,&learnQT::setSceneDirty);
    connect(&p,&RenderParams::maxBouncesChanged,this,&learnQT::setSceneDirty);
    connect(&p,&RenderParams::maxRenderFramesChanged,this,&learnQT::setSceneDirty);
    m_sceneDirty=Scene::getInstance().document.filePath.isEmpty();
    restoreSceneControls(); refreshScenes();
    ui.centralWidget->layout()->activate();
    const int panelWidth=std::max(280,ui.scrollArea->widget()->minimumSizeHint().width()+
        ui.scrollArea->verticalScrollBar()->sizeHint().width()+4);
    ui.scrollArea->setMinimumWidth(panelWidth);
    const auto margins=ui.centralWidget->layout()->contentsMargins();
    // Keep the original 640-pixel render viewport; only widen the controls.
    resize(640+panelWidth+margins.left()+margins.right()+ui.horizontalLayout->spacing(),height());
}

void learnQT::refreshScenes()
{
    const QSignalBlocker block(m_sceneList);
    const QDir directory(QString::fromStdString(getResourcePath("scenes")));
    QStringList paths;
    for(const auto& entry:directory.entryInfoList({"*.scene.json"},QDir::Files,QDir::Name)) paths.append(entry.absoluteFilePath());
    const auto& doc=Scene::getInstance().document;
    if(!doc.filePath.isEmpty() && !paths.contains(doc.filePath) && !m_sessionScenes.contains(doc.filePath)) m_sessionScenes.append(doc.filePath);
    for(const auto& path:m_sessionScenes) if(!paths.contains(path)) paths.append(path);
    m_sceneList->clear();
    for(const auto& path:paths) {
        SceneDocument item; QString error;
        const QString name=SceneDocument::loadScene(path,item,error) ? item.root["name"].toString() : QFileInfo(path).fileName();
        m_sceneList->addItem(name,path);
        m_sceneList->setItemData(m_sceneList->count()-1,path,Qt::ToolTipRole);
    }
    if(doc.filePath.isEmpty()) m_sceneList->addItem(tr("新场景（未保存）"),QString());
    m_sceneList->setCurrentIndex(m_sceneList->findData(doc.filePath));
    setWindowTitle(doc.root["name"].toString()+(m_sceneDirty ? " *" : "")+" — learnQT");
}

void learnQT::setSceneDirty()
{
    if(m_restoring || m_loading) return;
    m_sceneDirty=true;
    setWindowTitle(Scene::getInstance().document.root["name"].toString()+" * — learnQT");
}

bool learnQT::confirmDiscard()
{
    if(m_loading) return false;
    if(!m_sceneDirty) return true;
    const auto choice=QMessageBox::warning(this,tr("场景有未保存的修改"),tr("是否保存当前场景的修改？"),
        QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Save);
    if(choice==QMessageBox::Cancel) return false;
    return choice==QMessageBox::Discard || saveSceneDocument();
}

bool learnQT::saveSceneDocument(bool saveAs)
{
    if(m_loading) return false;
    QString path=Scene::getInstance().document.filePath;
    if(saveAs || path.isEmpty()) path=QFileDialog::getSaveFileName(this,tr("保存场景"),
        path.isEmpty() ? QString::fromStdString(getResourcePath("scenes/new.scene.json")) : path,tr("Scene (*.scene.json)"));
    if(path.isEmpty()) return false;
    if(!path.endsWith(".json",Qt::CaseInsensitive)) path+=".scene.json";
    QString error; bool saved;
    { QMutexLocker lock(&param_mutex); saved=Scene::getInstance().saveScene(path,error); }
    if(!saved) { QMessageBox::critical(this,tr("保存失败"),error); return false; }
    m_sceneDirty=false; refreshScenes(); statusBar()->showMessage(tr("场景已保存：%1").arg(path)); return true;
}

void learnQT::setLoading(bool loading)
{
    m_loading=loading;
    ui.scrollArea->setEnabled(!loading); ui.openGLWidget->setEnabled(!loading);
    if(loading) statusBar()->showMessage(tr("正在准备场景…（原场景仍保持显示）"));
}

void learnQT::beginSceneLoad(const QString& path,bool model)
{
    if(m_loading) return;
    setLoading(true);
    struct Result { std::unique_ptr<Scene> scene; QString error; };
    auto result=std::make_shared<Result>();
    m_loadWorker=QThread::create([this,path,model,result] {
        result->scene=Scene::prepareScene(path,model,result->error,[this](const QString& stage) {
            QMetaObject::invokeMethod(this,[this,stage] { statusBar()->showMessage(stage); },Qt::QueuedConnection);
        });
    });
    connect(m_loadWorker,&QThread::finished,this,[this,result,model] {
        m_loadWorker->deleteLater(); m_loadWorker=nullptr;
        if(!result->scene) {
            setLoading(false); refreshScenes();
            QMessageBox::critical(this,tr("场景加载失败（已保留原场景）"),result->error); return;
        }
        m_restoring=true;
        ui.openGLWidget->replaceScene(*result->scene);
        m_restoring=false;
        m_sceneDirty=model;
        restoreSceneControls(); setLoading(false); refreshScenes();
        statusBar()->showMessage(tr("场景已切换，累计帧和降噪历史已重置"));
    });
    m_loadWorker->start();
}

void learnQT::restoreSceneControls()
{
    m_restoring=true;
    // Block every restored widget, including sliders whose handlers write all materials.
    std::vector<std::unique_ptr<QSignalBlocker>> blocks;
    for(auto object:ui.scrollArea->findChildren<QWidget*>()) blocks.emplace_back(new QSignalBlocker(object));
    const auto s=RenderParams::instance().snapshot();
    ui.DeNoisecheckBox->setChecked(s.denoise); ui.useEnvironmentMapcheckBox->setChecked(s.useEnvironmentMap);
    ui.maxBouncesSpinBox->setValue(s.maxBounces); ui.maxRenderFramesSpinBox->setValue(s.maxRenderFrames);
    const auto& scene=Scene::getInstance();
    if(!scene.triangles.empty()) {
        const Material m=scene.triangles.front().material;
#define RESTORE_SLIDER(field,slider,edit) ui.slider->setValue(qRound(m.field*100)); ui.edit->setText(QString::number(m.field,'f',2));
        RESTORE_SLIDER(roughness,roughnessSlider,roughnesslineEdit)
        RESTORE_SLIDER(metallic,metallicSlider,metalliclineEdit)
        RESTORE_SLIDER(subsurface,subsurfaceSlider,subsurfacelineEdit)
        RESTORE_SLIDER(specularTint,specularTintSlider,specularTintlineEdit)
        RESTORE_SLIDER(sheen,sheenSlider,sheenlineEdit)
        RESTORE_SLIDER(sheenTint,sheenTintSlider,sheenTintlineEdit)
        RESTORE_SLIDER(clearcoat,clearcoatSlider,clearcoatlineEdit)
        RESTORE_SLIDER(clearcoatGloss,clearcoatGlossSlider,clearcoatGlosslineEdit)
        RESTORE_SLIDER(IOR,IORSlider,IORlineEdit)
        RESTORE_SLIDER(transmission,transmissionSlider,transmissionlineEdit)
#undef RESTORE_SLIDER
    }
    ui.modelPathLabel->setText(scene.document.filePath.isEmpty() ? tr("新场景：%1").arg(QString::fromStdString(scene.currentModelPath())) : scene.document.filePath);
    ui.modelPathLabel->setToolTip(ui.modelPathLabel->text());
    ui.scrollArea->horizontalScrollBar()->setValue(0);
    m_restoring=false;
}

void learnQT::closeEvent(QCloseEvent* event)
{
    if(m_loading) { event->ignore(); statusBar()->showMessage(tr("请等待当前加载或导出完成后关闭")); return; }
    if(confirmDiscard()) event->accept(); else event->ignore();
}

void learnQT::hideLayout(QLayout* layout)
{
    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem* item = layout->itemAt(i);
        if (item->widget()) {
            item->widget()->hide(); // 隐藏控件
        }
        else if (item->layout()) {
            hideLayout(item->layout()); // 递归处理子布局
        }
    }
}

void learnQT::showLayout(QLayout* layout)
{
    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem* item = layout->itemAt(i);
        if (item->widget()) {
            item->widget()->show(); // 显示控件
        }
        else if (item->layout()) {
            showLayout(item->layout()); // 递归处理子布局
        }
    }
}

void learnQT::toggleFullscreen()
{
    if (isFullScreen()) {
        exitFullscreen();
    }
    else {
        showFullScreen();
        statusBar()->hide();
        menuBar()->hide();
        ui.mainToolBar->hide();
        ui.scrollArea->hide();
        ui.centralWidget->layout()->setContentsMargins(0, 0, 0, 0);
    }
}

void learnQT::exitFullscreen()
{
    showNormal();
    statusBar()->show();
    menuBar()->show();
    ui.mainToolBar->show();
    ui.scrollArea->show();
    ui.centralWidget->layout()->setContentsMargins(9, 9, 9, 9);
}

void learnQT::keyPressEvent(QKeyEvent* event) {
    if(m_loading) { event->accept(); return; }
    if (event->key() == Qt::Key_F11) {
        toggleFullscreen();
        event->accept();
    }
    // 按ESC退出全屏
    else if (event->key() == Qt::Key_Escape && isFullScreen()) {
        exitFullscreen();
        event->accept();
    }
    else {
        QApplication::sendEvent(ui.openGLWidget, event);
    }
}
void  learnQT::keyReleaseEvent(QKeyEvent* event) {
    QApplication::sendEvent(ui.openGLWidget, event);
}
