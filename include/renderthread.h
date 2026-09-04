#ifndef RENDERTHREAD_H
#define RENDERTHREAD_H

#include <QImage>
#include <QMutex>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QThread>
#include <QTimer>
#include <memory>

#include <atomic>
#include <iostream>

#include "SceneDirty.h"
#include "renderer.h"
#include "texturebuffer.h"

class RenderThread : public QThread, public QOpenGLFunctions
{
    Q_OBJECT

public:
    RenderThread(QSurface *surface, QOpenGLContext *mainContext, QObject *parent = nullptr);
    ~RenderThread() override;

    void setNewSize(int width, int height);
    // UI 线程只做按位 OR，渲染线程在帧首统一消费。
    void markSceneDirty(SceneDirtyFlags flags);
    void replaceScene(Scene& scene);

signals:
    void imageReady();

protected:
    void run() override;

private:
    RenderThread(const RenderThread &) = delete;
    RenderThread &operator =(const RenderThread &) = delete;
    RenderThread(const RenderThread &&) = delete;
    RenderThread &operator =(const RenderThread &&) = delete;

private:
    std::atomic_bool m_running{true};

    int m_width = 100;
    int m_height = 100;
    QMutex m_mutex;
    QMutex m_frameMutex; // Scene replacement is serialized against the whole frame.

    QOpenGLContext *m_mainContext = nullptr;
    QOpenGLContext *m_renderContext = nullptr;
    QSurface *m_surface = nullptr;
    // 首帧默认全量同步，之后每帧 exchange(0) 消费。
    std::atomic<SceneDirtyFlags> m_pendingSceneDirty{kInitialSceneDirty};
};

#endif // RENDERTHREAD_H
