#pragma once

#include <QtWidgets/QMainWindow>

#include <QFileDialog>
#include <QMessageBox>
#include <QMutexLocker>
#include <QString>
#include <QComboBox>
#include <QThread>
#include <QCloseEvent>

#include <iostream>

#include "RenderParams.h"
#include "Scene.h"
#include "SceneDirty.h"
#include "ui_learnQT.h"

extern QMutex param_mutex;

class learnQT : public QMainWindow
{
    Q_OBJECT

public:
    learnQT(QWidget *parent = Q_NULLPTR);
    ~learnQT() override;

    // 隐藏布局中的所有控件
    void hideLayout(QLayout* layout);
    // 显示布局中的所有控件
    void showLayout(QLayout* layout);
    void toggleFullscreen();
    void exitFullscreen();

public slots:
    void loadModel();

    void saveGLImage() {
        // 获取保存路径
        QString filePath = QFileDialog::getSaveFileName(
            this,
            QObject::tr("Save Image"),
            QDir::homePath(),
            QObject::tr("PNG (*.png);;JPEG (*.jpg *.jpeg)")
        );

        if (filePath.isEmpty()) {
            // 用户取消操作
            return;
        }

        // 捕获当前帧
        QImage image = ui.openGLWidget->grabFramebuffer();
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
        QMutexLocker lock(&param_mutex);
        Scene::getInstance().updateMaterial(
            QVector3D(0.0f, 0.0f, 0.0f),
            QVector3D(1.0f, 1.0f, 1.0f),
            ui.subsurfacelineEdit->text().toFloat(),
            ui.metalliclineEdit->text().toFloat(),
            ui.specularTintlineEdit->text().toFloat(),
            ui.roughnesslineEdit->text().toFloat(),
            0.0f,
            ui.sheenlineEdit->text().toFloat(),
            ui.sheenTintlineEdit->text().toFloat(),
            ui.clearcoatlineEdit->text().toFloat(),
            ui.clearcoatGlosslineEdit->text().toFloat(),
            ui.IORlineEdit->text().toFloat(),
            ui.transmissionlineEdit->text().toFloat());
        lock.unlock();
        // 材质参数仍然直接写 Scene，但刷新统一走 Material dirty。
        ui.openGLWidget->markSceneDirty(SceneDirtyFlag::Material);
    }

    void roughnessSliderUp() {
        const int rou = ui.roughnessSlider->value();
        ui.roughnesslineEdit->setText(QString::number(rou / 100.0, 'f', 2));
        updateMaterial();
    }

    void metallicSliderUp() {
        const int meta = ui.metallicSlider->value();
        ui.metalliclineEdit->setText(QString::number(meta / 100.0, 'f', 2));
        updateMaterial();
    }

    void subsurfaceSliderUp() {
        const int sub = ui.subsurfaceSlider->value();
        ui.subsurfacelineEdit->setText(QString::number(sub / 100.0, 'f', 2));
        updateMaterial();
    }

    void specularTintSliderUp() {
        const int speT = ui.specularTintSlider->value();
        ui.specularTintlineEdit->setText(QString::number(speT / 100.0, 'f', 2));
        updateMaterial();
    }

    void sheenSliderUp() {
        const int shee = ui.sheenSlider->value();
        ui.sheenlineEdit->setText(QString::number(shee / 100.0, 'f', 2));
        updateMaterial();
    }

    void sheenTintSliderUp() {
        const int sheeT = ui.sheenTintSlider->value();
        ui.sheenTintlineEdit->setText(QString::number(sheeT / 100.0, 'f', 2));
        updateMaterial();
    }

    void clearcoatSliderUp() {
        const int cle = ui.clearcoatSlider->value();
        ui.clearcoatlineEdit->setText(QString::number(cle / 100.0, 'f', 2));
        updateMaterial();
    }

    void clearcoatGlossSliderUp() {
        const int cleG = ui.clearcoatGlossSlider->value();
        ui.clearcoatGlosslineEdit->setText(QString::number(cleG / 100.0, 'f', 2));
        updateMaterial();
    }

    void IORSliderUp() {
        const int cleG = ui.IORSlider->value();
        ui.IORlineEdit->setText(QString::number(cleG / 100.0, 'f', 2));
        updateMaterial();
    }

    void transmissionSliderUp() {
        const int cleG = ui.transmissionSlider->value();
        ui.transmissionlineEdit->setText(QString::number(cleG / 100.0, 'f', 2));
        updateMaterial();
    }

    void DenoiseCheckBoxChanged() {
        // 渲染开关直接写 RenderParams，不再经由 RenderThread 转发。
        RenderParams::instance().setDenoise(ui.DeNoisecheckBox->isChecked());
    }

    void useEnvironmentMapCheckBoxChanged() {
        RenderParams::instance().setUseEnvironmentMap(ui.useEnvironmentMapcheckBox->isChecked());
    }

    void maxBouncesSpinBoxChanged(int value) {
        RenderParams::instance().setMaxBounces(value);
    }

    void maxRenderFramesSpinBoxChanged(int value) {
        RenderParams::instance().setMaxRenderFrames(value);
    }

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    void setupSceneControls();
    void refreshScenes();
    void restoreSceneControls();
    void setSceneDirty();
    bool confirmDiscard();
    bool saveSceneDocument(bool saveAs = false);
    void beginSceneLoad(const QString& path, bool model = false);
    void setLoading(bool loading);
    QComboBox* m_sceneList = nullptr;
    QThread* m_loadWorker = nullptr;
    bool m_sceneDirty = false;
    bool m_restoring = false;
    bool m_loading = false;
    QStringList m_sessionScenes;
    void configureRegressionCapture();
    void configureSceneRegression();
    void captureRegressionFrame();

    Ui::learnQTClass ui;
    QString m_regressionOutputPath;
    int m_regressionTargetFrames = 12;
    int m_regressionPresentedFrames = 0;
    bool m_regressionCaptureQueued = false;
    bool m_validateLanternRegression = false;
};
