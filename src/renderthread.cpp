#include "renderthread.h"

#include "RenderParams.h"
extern QMutex param_mutex;

RenderThread::RenderThread(QSurface *surface, QOpenGLContext *mainContext, QObject *parent)
    : QThread(parent)
    , m_mainContext(mainContext)
    , m_surface(surface)
{
    m_renderContext = new QOpenGLContext;
    m_renderContext->setFormat(m_mainContext->format());
    m_renderContext->setShareContext(m_mainContext);
    m_renderContext->create();
    m_renderContext->moveToThread(this);
}

RenderThread::~RenderThread()
{
    m_running.store(false, std::memory_order_relaxed);
    wait();
}

// called in UI thread
void RenderThread::setNewSize(int width, int height)
{
    QMutexLocker lock(&m_mutex);
    m_width = width;
    m_height = height;
}

void RenderThread::markSceneDirty(SceneDirtyFlags flags)
{
    m_pendingSceneDirty.fetch_or(flags, std::memory_order_relaxed);
}

void RenderThread::replaceScene(Scene& scene)
{
    QMutexLocker frame(&m_frameMutex);
    QMutexLocker data(&param_mutex);
    Scene::getInstance().adoptPrepared(scene);
    RenderParams::instance().applySnapshot(Scene::getInstance().document.settings());
    markSceneDirty(SceneDirtyFlag::SceneBuffers | SceneDirtyFlag::Camera);
}

// called in render thread
void RenderThread::run()
{
    // 延迟 400 毫秒再开始渲染，让主窗口先完成尺寸调整。
    Sleep(400);
    m_renderContext->makeCurrent(m_surface);

    TextureBuffer::instance()->createTexture(m_renderContext);

    QMutexLocker initialization(&m_frameMutex);
    const RenderParams::Snapshot initialSnapshot = RenderParams::instance().snapshot();
    Renderer renderer(m_width, m_height, initialSnapshot);
    initialization.unlock();

    while (m_running.load(std::memory_order_relaxed))
    {
        QMutexLocker frame(&m_frameMutex);
        int width = 0;
        int height = 0;
        {
            QMutexLocker lock(&m_mutex);
            width = m_width;
            height = m_height;
        }

        // 每帧只在这里快照一次 RenderParams，并消费一次 Scene dirty。
        const RenderParams::Snapshot snapshot = RenderParams::instance().snapshot();
        const SceneDirtyFlags dirtyFlags = m_pendingSceneDirty.exchange(0u, std::memory_order_relaxed);

        renderer.render(width, height, snapshot, dirtyFlags);
        TextureBuffer::instance()->updateTexture(m_renderContext, width, height);
        emit imageReady();
    }

    TextureBuffer::instance()->deleteTexture(m_renderContext);
}
