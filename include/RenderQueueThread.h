#pragma once

#include "RenderJob.h"
#include "RenderResult.h"
#include "Scene.h"
#include <QImage>
#include <QMutex>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QThread>
#include <QWaitCondition>

struct RenderQueueRequest
{
    quint64 id = 0;
    SceneDocument document;
    Scene::AssetCache assets;
    RenderJobSettings settings;
    QString outputPath;
    QMap<QString, QString> resourceSignatures;
};

QMap<QString, QString> renderResourceSignatures(const SceneDocument &document);

class RenderQueueThread : public QThread
{
    Q_OBJECT
  public:
    RenderQueueThread(QOpenGLContext *shared, QObject *parent = nullptr);
    ~RenderQueueThread() override;
    bool submit(RenderQueueRequest request);
    void pauseCurrent(bool paused);
    void stopCurrent();

  signals:
    void workerFailed(QString error);
    void jobState(quint64 id, RenderJobState state);
    void jobProgress(quint64 id, int samples, int target, double seconds, QImage image);
    void jobLinearResult(quint64 id,RenderResultPtr result);
    void jobFinished(quint64 id, bool rendered, bool stopped, QImage image,
                     QString outputPath, QString error);

  protected:
    void run() override;

  private:
    QOffscreenSurface *surface = nullptr;
    QOpenGLContext *context = nullptr;
    QMutex mutex;
    QWaitCondition wake;
    RenderQueueRequest pending;
    bool hasPending = false;
    std::atomic_bool running{true}, cancel{false}, paused{false};
};
