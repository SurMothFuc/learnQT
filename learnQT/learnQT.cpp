#include "learnQT.h"

#include <QPushButton>


learnQT::learnQT(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);    
    ;
    connect( ui.pushButton,SIGNAL(clicked(bool)), this, SLOT(upoff()));
    
}

void learnQT::keyPressEvent(QKeyEvent* event) {
    QApplication::sendEvent(ui.openGLWidget,event);
}
void  learnQT::keyReleaseEvent(QKeyEvent* event) {
    QApplication::sendEvent(ui.openGLWidget, event);
}