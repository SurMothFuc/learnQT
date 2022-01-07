#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_learnQT.h"
#include <iostream>
extern Pass_parameters param;
extern QMutex param_mutex;

class learnQT : public QMainWindow
{
    Q_OBJECT

public:
    learnQT(QWidget *parent = Q_NULLPTR);
public slots:
    void print() {        
        std::cout << 123 << std::endl;
    };
    void sed() {
        ui.openGLWidget->sendM();
    }
    void upoff() {
        QMutexLocker lock(&param_mutex);
        {            
            param.offx += 10.0;            
        }
    }
protected:
    void keyPressEvent(QKeyEvent* event) Q_DECL_OVERRIDE;
    void keyReleaseEvent(QKeyEvent* event) Q_DECL_OVERRIDE;
private:
    Ui::learnQTClass ui;
};
