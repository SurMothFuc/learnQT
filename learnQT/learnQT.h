#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_learnQT.h"
#include <iostream>

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

private:
    Ui::learnQTClass ui;
};
