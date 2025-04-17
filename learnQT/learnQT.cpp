#include "learnQT.h"

#include <QPushButton>


learnQT::learnQT(QWidget *parent)
    : QMainWindow(parent)
{
    Scene::getInstance();
    ui.setupUi(this);    

    connect( ui.pushButton,SIGNAL(clicked(bool)), this, SLOT(upoff()));
    connect(ui.roughnessSlider, SIGNAL(valueChanged(int)), this, SLOT(roughnessSliderUp()));
    connect(ui.metallicSlider, SIGNAL(valueChanged(int)), this, SLOT(metallicSliderUp()));
    connect(ui.subsurfaceSlider, SIGNAL(valueChanged(int)), this, SLOT(subsurfaceSliderUp()));
    connect(ui.specularSlider, SIGNAL(valueChanged(int)), this, SLOT(specularSliderUp()));
    connect(ui.specularTintSlider, SIGNAL(valueChanged(int)), this, SLOT(specularTintSliderUp()));
    connect(ui.sheenSlider, SIGNAL(valueChanged(int)), this, SLOT(sheenSliderUp()));
    connect(ui.sheenTintSlider, SIGNAL(valueChanged(int)), this, SLOT(sheenTintSliderUp()));
    connect(ui.clearcoatSlider, SIGNAL(valueChanged(int)), this, SLOT(clearcoatSliderUp()));
    connect(ui.clearcoatGlossSlider, SIGNAL(valueChanged(int)), this, SLOT(clearcoatGlossSliderUp()));

    
}

void learnQT::keyPressEvent(QKeyEvent* event) {
    QApplication::sendEvent(ui.openGLWidget,event);
}
void  learnQT::keyReleaseEvent(QKeyEvent* event) {
    QApplication::sendEvent(ui.openGLWidget, event);
}