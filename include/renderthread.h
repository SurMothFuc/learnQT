#pragma once
#include "renderer.h"
#include "texturebuffer.h"
#include <QMutex>
#include <QOpenGLContext>
#include <QThread>
class RenderThread : public QThread
{
    Q_OBJECT
  public:
    RenderThread(QSurface *surface, QOpenGLContext *context, QObject *parent = nullptr);
    ~RenderThread() override;
    void setNewSize(int width, int height);
    void markSceneDirty(SceneDirtyFlags flags);
    void replaceScene(Scene &scene);
    void submitScene(std::shared_ptr<Scene> scene, quint64 version);
    void submitDocument(const SceneDocument &document, int change, quint64 version, bool final = true);
    void startJob(RenderJobSettings settings);
    void pauseJob(bool paused);
    void stopJob();
    bool jobActive() const
    {
        return m_jobActive.load();
    }
    void setPreviewVisible(bool visible)
    {
        const bool wasVisible = m_previewVisible.exchange(visible);
        if (visible != wasVisible)
            ++controlRevision;
        if (visible && !wasVisible)
            markSceneDirty(kInitialSceneDirty);
    }
    void pick(QPoint pixel, quint64 request, quint64 version);
  signals:
    void imageReady();
    void statsReady(RenderStats stats);
    void jobStateChanged(RenderJobState state, const QString &message);
    void resultReady(QImage image, bool final);
    void picked(QString id, quint64 request, quint64 version);

  protected:
    void run() override;

  private:
    std::atomic_bool m_running{true}, m_jobActive{false}, m_cancel{false}, m_paused{false};
    std::atomic_bool m_previewVisible{true};
    std::atomic<quint64> controlRevision{0};
    QMutex mutex;
    QSize viewport{100, 100};
    QSurface *surface;
    QOpenGLContext *context;
    std::shared_ptr<Scene> pendingScene;
    SceneDocument pendingDocument;
    bool hasDocument = false, finalTransform = true;
    int documentChanges = 0;
    quint64 pendingVersion = 0, currentVersion = 0;
    SceneDirtyFlags pendingDirty = kInitialSceneDirty;
    bool jobRequested = false;
    RenderJobSettings jobSettings;
    QPoint pickPixel;
    quint64 pickSerial = 0, pickSceneVersion = 0;
    bool pickPending = false;
};
