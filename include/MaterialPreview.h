#pragma once
#include "Scene.h"
#include <QWidget>
#include <QTimer>
#include <QImage>
#include <functional>
class MaterialPreviewWorker;

class MaterialPreview : public QWidget
{
public:
    explicit MaterialPreview(QWidget *parent = nullptr);
    ~MaterialPreview() override;
    void setMaterial(const SceneDocument &source, const QString &id, Scene::AssetCache assets);
    void setRenderingAllowed(bool allowed, const QString &reason = {});
    void resetView();
    void retry();
    QImage image() const { return frame; }
    int samples() const { return frameSamples; }
    quint64 requestVersion() const { return revision; }
    quint64 displayedVersion() const { return frameRevision; }
    QString status() const { return message; }
    std::function<void()> stateChanged;
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
private:
    friend class MaterialPreviewWorker;
    MaterialPreviewWorker *worker = nullptr;
    QTimer debounce;
    SceneDocument source;
    Scene::AssetCache assets;
    QString materialId, message, pauseReason;
    QByteArray signature;
    QImage frame;
    int frameSamples = 0;
    quint64 revision = 0, frameRevision = 0;
    bool allowed = true, dragging = false;
    QPoint lastMouse;
    float azimuth = .35f, elevation = .18f, distance = 4.6f;
    void invalidate();
    void schedule();
    void receive(quint64 version, QImage image, int samples, const QString &error);
};
