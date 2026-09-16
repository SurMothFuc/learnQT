#pragma once
#include "renderer.h"
#include "texturebuffer.h"
#include <QElapsedTimer>
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
    // 拾取缓冲的失效标记：相机/几何/材质/尺寸变化后置位，只在有拾取请求或几何/尺寸变化时才重绘。
    bool pickNeedsRedraw = true;
    quint64 pickVersion = 0;
    // 交互回退状态：interactionClock 在相机/变换变更时重启，interactionActive 表示仍在交互窗口内。
    QElapsedTimer interactionClock;
    bool interactionActive = false, rasterRequested = false, keepPathtraceApplied = false;
    // 截图等入口可强制打开光栅化交互预览，忽略交互窗口。
    std::atomic_bool m_forceRaster{false};
    std::atomic_bool m_interactionFallbackDisabled{false};

  public:
    void setForceRaster(bool forced)
    {
        m_forceRaster.store(forced);
    }
    // 回归入口用来关闭交互回退，专心验证路径追踪预览路径。
    void setInteractionFallbackDisabled(bool disabled)
    {
        m_interactionFallbackDisabled.store(disabled);
    }
};
