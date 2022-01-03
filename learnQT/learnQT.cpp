#include "learnQT.h"

#include <QPushButton>


learnQT::learnQT(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);    
    connect( ui.pushButton,SIGNAL(clicked(bool)), this, SLOT(print()));
    
}

