#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_learnQT.h"
#include "Scene.h"
#include <iostream>
#include <QString>

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
        updateMaterial();
    }
    void updateMaterial()
    {
        param_mutex.lock();
        {
            Scene::getInstance().updateMaterial(QVector3D(0.0f, 0.0f, 0.0f), QVector3D(1.0, 1.0, 1.0),
                ui.subsurfacelineEdit->text().toFloat(),ui.metalliclineEdit->text().toFloat(), ui.specularlineEdit->text().toFloat(),
                ui.specularTintlineEdit->text().toFloat(),ui.roughnesslineEdit->text().toFloat(), 0,
                ui.sheenlineEdit->text().toFloat(), ui.sheenTintlineEdit->text().toFloat(), ui.clearcoatlineEdit->text().toFloat(),
                ui.clearcoatGlosslineEdit->text().toFloat(),1.0,0);
        }
        param_mutex.unlock();
        ui.openGLWidget->sendM();
    }
    void roughnessSliderUp() {
        int rou = ui.roughnessSlider->value();
        ui.roughnesslineEdit->setText(QString::number(rou/100.0, 'f', 2));
        updateMaterial();
    }
    void metallicSliderUp() {
        int meta = ui.metallicSlider->value();
        ui.metalliclineEdit->setText(QString::number(meta/ 100.0, 'f', 2));
        updateMaterial();
    }
    void subsurfaceSliderUp() {
        int sub = ui.subsurfaceSlider->value();
        ui.subsurfacelineEdit->setText(QString::number(sub / 100.0, 'f', 2));
        updateMaterial();
    }
    void specularSliderUp() {
        int spec = ui.specularSlider->value();
        ui.specularlineEdit->setText(QString::number(spec / 100.0, 'f', 2));
        updateMaterial();
    }
    void specularTintSliderUp() {
        int speT = ui.specularTintSlider->value();
        ui.specularTintlineEdit->setText(QString::number(speT / 100.0, 'f', 2));
        updateMaterial();
    }
    void sheenSliderUp() {
        int shee = ui.sheenSlider->value();
        ui.sheenlineEdit->setText(QString::number(shee / 100.0, 'f', 2));
        updateMaterial();
    }
    void sheenTintSliderUp() {
        int sheeT = ui.sheenTintSlider->value();
        ui.sheenTintlineEdit->setText(QString::number(sheeT / 100.0, 'f', 2));
        updateMaterial();
    }
    void clearcoatSliderUp() {
        int cle = ui.clearcoatSlider->value();
        ui.clearcoatlineEdit->setText(QString::number(cle / 100.0, 'f', 2));
        updateMaterial();
    }
    void clearcoatGlossSliderUp() {
        int cleG= ui.clearcoatGlossSlider->value();
        ui.clearcoatGlosslineEdit->setText(QString::number(cleG / 100.0, 'f', 2));
        updateMaterial();
    }
protected:
    void keyPressEvent(QKeyEvent* event) Q_DECL_OVERRIDE;
    void keyReleaseEvent(QKeyEvent* event) Q_DECL_OVERRIDE;
private:
    Ui::learnQTClass ui;
};
