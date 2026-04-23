#include "learnQT.h"

#include <QPushButton>


learnQT::learnQT(QWidget *parent)
    : QMainWindow(parent)
{
    Scene::getInstance();
    ui.setupUi(this);    

    const RenderParams::Snapshot initialRenderParams = RenderParams::instance().snapshot();
    ui.maxBouncesSpinBox->setValue(initialRenderParams.maxBounces);
    ui.maxRenderFramesSpinBox->setValue(initialRenderParams.maxRenderFrames);

    connect( ui.SaveImageButton,SIGNAL(clicked(bool)), this, SLOT(saveGLImage()));
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
