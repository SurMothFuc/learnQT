#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_learnQT.h"
#include "Scene.h"
#include <iostream>
#include <QString>
#include <QFileDialog>
#include <QMessageBox>

extern QMutex param_mutex;

class learnQT : public QMainWindow
{
    Q_OBJECT

public:
    learnQT(QWidget *parent = Q_NULLPTR);

    // 隐藏布局中的所有控件
    void hideLayout(QLayout* layout);

    // 显示布局中的所有控件
    void showLayout(QLayout* layout);
    void toggleFullscreen();
    void exitFullscreen();
public slots:
    void saveGLImage() {
        // 获取保存路径
        QString filePath = QFileDialog::getSaveFileName(
            this,
            QObject::tr("Save Image"),
            QDir::homePath(),
            QObject::tr("PNG (*.png);;JPEG (*.jpg *.jpeg)")
        );

        if (filePath.isEmpty()) return;  // 用户取消操作

        // 捕获当前帧
        QImage image = ui.openGLWidget->grabFramebuffer();
        QString("sadasd")+ filePath;
        // 保存文件
        if (!image.save(filePath)) {
            QMessageBox::critical(
                this,
                QObject::tr("Failed to save the image"),
                QObject::tr("Unable to save the image to drive: ") + filePath +
                    QObject::tr("\nPlease check the path permissions and disk space.")
            );
        }
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
                ui.clearcoatGlosslineEdit->text().toFloat(), ui.IORlineEdit->text().toFloat(), ui.transmissionlineEdit->text().toFloat());
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
    void IORSliderUp() {
        int cleG = ui.IORSlider->value();
        ui.IORlineEdit->setText(QString::number(cleG / 100.0, 'f', 2));
        updateMaterial();
    }
    void transmissionSliderUp() {
        int cleG = ui.transmissionSlider->value();
        ui.transmissionlineEdit->setText(QString::number(cleG / 100.0, 'f', 2));
        updateMaterial();
    }
    void DenoiseCheckBoxChanged() {
        bool ck = ui.DeNoisecheckBox->isChecked();
        emit ui.openGLWidget->sendSetDenoise(ck);
    }

protected:
    void keyPressEvent(QKeyEvent* event) Q_DECL_OVERRIDE;
    void keyReleaseEvent(QKeyEvent* event) Q_DECL_OVERRIDE;
private:
    Ui::learnQTClass ui;
};
