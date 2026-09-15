#include "renderthread.h"
#include "EditorController.h"
#include "RenderRateTracker.h"
#include <QElapsedTimer>
RenderThread::RenderThread(QSurface *s, QOpenGLContext *shared, QObject *p) : QThread(p), surface(s)
{
    context = new QOpenGLContext;
    context->setFormat(shared->format());
    context->setShareContext(shared);
    context->create();
    context->moveToThread(this);
    qRegisterMetaType<RenderStats>();
    qRegisterMetaType<RenderJobState>();
}
RenderThread::~RenderThread()
{
    m_running = false;
    ++controlRevision;
    m_cancel = true;
    ++controlRevision;
    wait();
}
void RenderThread::setNewSize(int w, int h)
{
    QMutexLocker lock(&mutex);
    viewport = {std::max(1, w), std::max(1, h)};
    ++controlRevision;
}
void RenderThread::markSceneDirty(SceneDirtyFlags f)
{
    QMutexLocker lock(&mutex);
    pendingDirty |= f;
    ++controlRevision;
}
void RenderThread::replaceScene(Scene &s)
{
    auto ready = std::make_shared<Scene>(false);
    ready->adoptPrepared(s);
    submitScene(ready, currentVersion + 1);
}
void RenderThread::submitScene(std::shared_ptr<Scene> s, quint64 v)
{
    QMutexLocker lock(&mutex);
    if (m_jobActive)
        return;
    pendingScene = s;
    ++controlRevision;
    pendingVersion = v;
    hasDocument = false;
    documentChanges = 0;
}
void RenderThread::submitDocument(const SceneDocument &d, int change, quint64 v, bool final)
{
    QMutexLocker lock(&mutex);
    if (m_jobActive)
        return;
    pendingDocument = d;
    ++controlRevision;
    hasDocument = true;
    documentChanges |= 1 << change;
    pendingVersion = v;
    finalTransform = final;
}
void RenderThread::startJob(RenderJobSettings s)
{
    QMutexLocker lock(&mutex);
    if (m_jobActive)
        return;
    jobSettings = s;
    ++controlRevision;
    jobRequested = true;
    m_jobActive = true;
    m_cancel = false;
    m_paused = false;
}
void RenderThread::pauseJob(bool paused)
{
    m_paused = paused;
    ++controlRevision;
}
void RenderThread::stopJob()
{
    m_cancel = true;
    ++controlRevision;
}
void RenderThread::pick(QPoint p, quint64 r, quint64 v)
{
    QMutexLocker lock(&mutex);
    pickPixel = p;
    pickSerial = r;
    pickSceneVersion = v;
    pickPending = true;
    ++controlRevision;
}
void RenderThread::run()
{
    msleep(100);
    if (!context->makeCurrent(surface))
    {
        emit jobStateChanged(RenderJobState::Failed, tr("无法创建渲染上下文"));
        delete context;
        return;
    }
    TextureBuffer::instance()->createTexture(context);
    try
    {
        QSize size;
        {
            QMutexLocker lock(&mutex);
            size = viewport;
        }
        Renderer renderer(size.width(), size.height(), RenderParams::instance().snapshot());
        renderer.cancel = &m_cancel;
        RenderParams::Snapshot jobSnapshot;
        RenderJobState state = RenderJobState::Idle;
        bool job = false;
        RenderJobSettings settings;
        auto setState = [&](RenderJobState s, const QString &msg = QString()) {
            state = s;
            emit jobStateChanged(s, msg);
        };
        renderer.denoising = [&] {
            if (job)
                setState(RenderJobState::Denoising);
        };
        QElapsedTimer clock, interval, jobClock, presentationClock;
        clock.start();
        interval.start();
        presentationClock.start();
        RenderRateTracker rates;
        RenderRateTracker rasterRates;
        int rasterFrames = 0;
        bool wasRaster = false;
        QElapsedTimer rasterCadence;
        bool wasSampling = true;
        bool presentationPending = false;
        quint64 presentedRevision = 0, presentedVersion = 0;
        quint64 minimumPresentationVersion = 1;
        QSize presentationSize;
        double jobSeconds = 0;
        while (m_running)
        {
            // Wait for the bounded tile burst before applying scene or output changes.
            // A bounded driver wait avoids adding a coarse OS sleep after each tile.
            if (!renderer.waitForGpuBoundary())
                continue;
            // Pace from submission start, including GPU work, rather than adding 16 ms
            // to every frame. The previous frame's fence was submitted before this wait.
            if (wasRaster && rasterCadence.isValid())
            {
                const qint64 remainingMs = 16 - rasterCadence.elapsed();
                if (remainingMs > 0)
                    msleep(static_cast<unsigned long>(remainingMs));
            }
            if (presentationPending)
            {
                emit imageReady();
                presentationPending = false;
            }
            rates.observe(clock.nsecsElapsed() / 1e9, renderer.samples(), renderer.completedTiles(),
                          renderer.stats.accumulationVersion, wasSampling);
            if (wasRaster)
                ++rasterFrames;
            rasterRates.observe(clock.nsecsElapsed() / 1e9, rasterFrames, 0, 0, wasRaster);
            wasRaster = false;
            wasSampling = false;
            bool gpuWork = false;
            SceneDirtyFlags dirty;
            std::shared_ptr<Scene> prepared;
            SceneDocument doc;
            int changes = 0;
            bool final = true, has = false, start = false;
            quint64 v = 0, batchRevision = 0;
            {
                QMutexLocker lock(&mutex);
                batchRevision = controlRevision.load();
                size = viewport;
                dirty = pendingDirty;
                pendingDirty = 0;
                prepared = std::move(pendingScene);
                has = hasDocument;
                hasDocument = false;
                if (has)
                    doc = pendingDocument;
                changes = documentChanges;
                documentChanges = 0;
                final = finalTransform;
                v = pendingVersion;
                start = jobRequested;
                jobRequested = false;
                if (start)
                    settings = jobSettings;
            }
            auto &scene = Scene::getInstance();
            try
            {
                if (prepared)
                {
                    scene.adoptPrepared(*prepared);
                    currentVersion = v;
                    minimumPresentationVersion = v;
                    RenderParams::instance().applySnapshot(scene.document.settings());
                    dirty |= kInitialSceneDirty;
                }
                if (has)
                {
                    currentVersion = v;
                    // resizeGL submits Organization even when rapid resizes return to the same size.
                    if (changes & (1 << EditorController::Organization))
                        minimumPresentationVersion = v;
                    if (changes & (1 << EditorController::Display))
                        dirty |= toSceneDirtyFlags(SceneDirtyFlag::Display);
                    if (changes &
                        ((1 << EditorController::Transform) | (1 << EditorController::MaterialChange) |
                         (1 << EditorController::Lighting)))
                    {
                        scene.applyEditorDocument(doc, final,
                                                  (changes & (1 << EditorController::Transform)) == 0);
                        dirty |= toSceneDirtyFlags(SceneDirtyFlag::Material);
                    }
                    else
                        scene.document = doc;
                    if (changes & (1 << EditorController::CameraChange))
                    {
                        doc.restoreCamera(scene.camera);
                        dirty |= toSceneDirtyFlags(SceneDirtyFlag::Camera);
                    }
                    RenderParams::instance().applySnapshot(doc.settings());
                }
                if (start)
                {
                    if (!settings.valid())
                    {
                        m_jobActive = false;
                        setState(RenderJobState::Failed, tr("输出尺寸或采样设置无效"));
                    }
                    else
                    {
                        job = true;
                        renderer.formal = true;
                        jobClock.start();
                        jobSnapshot = scene.document.settings();
                        jobSnapshot.renderLow = false;
                        jobSnapshot.useTileRendering = true;
                        jobSnapshot.tileSize = settings.tileSize;
                        jobSnapshot.maxRenderFrames = settings.samples;
                        jobSnapshot.maxBounces = settings.bounces;
                        jobSnapshot.denoise = settings.denoise;
                        dirty |= toSceneDirtyFlags(SceneDirtyFlag::Camera);
                        setState(RenderJobState::Preparing);
                        gpuWork = true;
                        renderer.prepareJob(settings.size, jobSnapshot, dirty);
                        dirty = 0;
                    }
                }
                auto snapshot = job ? jobSnapshot : RenderParams::instance().snapshot();
                if (job)
                    size = settings.size;
                // 回归入口可显式关闭交互回退，专心验证路径追踪预览。
                if (!job && m_interactionFallbackDisabled)
                {
                    interactionActive = false;
                    snapshot.interactionMode = RenderParams::InteractionKeepPathtrace;
                    snapshot.rasterLocked = false;
                }
                // 交互判定：相机变更或对象/材质更新到达即视为交互开始，停手由 interactionIdleMs 控制。
                // 正式任务不参与，避免出图期间被交互回退打断。
                if (!job && !m_interactionFallbackDisabled &&
                    (hasSceneDirtyFlag(dirty, SceneDirtyFlag::Camera) ||
                     hasSceneDirtyFlag(dirty, SceneDirtyFlag::SceneBuffers) ||
                     hasSceneDirtyFlag(dirty, SceneDirtyFlag::Material)))
                {
                    interactionActive = true;
                    interactionClock.restart();
                }
                else if (interactionActive && (!interactionClock.isValid() ||
                                               interactionClock.elapsed() >= snapshot.interactionIdleMs))
                {
                    interactionActive = false;
                }
                if (job && m_cancel)
                {
                    if (renderer.samples() > 0)
                        emit resultReady(renderer.result(snapshot), false);
                    job = false;
                    renderer.formal = false;
                    m_jobActive = false;
                    m_cancel = false;
                    setState(RenderJobState::Stopped);
                    markSceneDirty(toSceneDirtyFlags(SceneDirtyFlag::Camera));
                }
                else if (!job && !m_previewVisible)
                {
                    // Keep updates until a preview frame or the next job actually consumes them.
                    markSceneDirty(dirty);
                    msleep(16);
                }
                else if (job && m_paused)
                {
                    if (state != RenderJobState::Paused)
                    {
                        if (renderer.samples() > 0)
                            emit resultReady(renderer.result(snapshot), false);
                        setState(RenderJobState::Paused);
                    }
                    msleep(16);
                }
                else
                {
                    try
                    {
                        if (job && state != RenderJobState::Rendering)
                            setState(RenderJobState::Rendering);
                        // 交互回退：只有非正式任务才参与，正式出图必须走完整路径追踪。
                        bool raster = false, keepPathtrace = false;
                        if (!job)
                        {
                            const bool interacting = interactionClock.isValid() &&
                                                     interactionClock.elapsed() < snapshot.interactionIdleMs;
                            keepPathtrace = interactionActive && interacting &&
                                            snapshot.interactionMode == RenderParams::InteractionLowResolution;
                            raster = !m_interactionFallbackDisabled &&
                                     (snapshot.rasterLocked || m_forceRaster.load() ||
                                      (interactionActive && interacting &&
                                       snapshot.interactionMode == RenderParams::InteractionRaster));
                            if (raster || keepPathtrace)
                            {
                                snapshot.renderLow = !raster && (snapshot.renderLow || keepPathtrace);
                                snapshot.interactionMode = raster ? RenderParams::InteractionRaster
                                                                  : RenderParams::InteractionKeepPathtrace;
                            }
                            if (!raster && rasterRequested)
                            {
                                // 离开光栅化：重置累积，避免显示上一帧光栅结果。
                                dirty |= toSceneDirtyFlags(SceneDirtyFlag::Camera);
                            }
                            if (keepPathtrace != keepPathtraceApplied)
                                dirty |= toSceneDirtyFlags(SceneDirtyFlag::Camera);
                            keepPathtraceApplied = keepPathtrace;
                            rasterRequested = raster;
                            renderer.setRasterActive(raster);
                        }
                        else
                        {
                            rasterRequested = false;
                            keepPathtraceApplied = false;
                            renderer.setRasterActive(false);
                        }
                        const int before = renderer.samples(), tilesBefore = renderer.completedTiles();
                        if (renderer.rasterActive())
                            rasterCadence.restart();
                        gpuWork = true;
                        renderer.render(size.width(), size.height(), snapshot, dirty, 16, [&] {
                            return !m_running || (job && (m_cancel || m_paused)) ||
                                   controlRevision.load() != batchRevision;
                        });
                        wasSampling = !renderer.rasterActive() && renderer.samplingActive(snapshot);
                        wasRaster = renderer.rasterActive();
                        if (!job)
                        {
                            renderer.updatePick(size.width(), size.height(), currentVersion);
                            {
                                QMutexLocker lock(&mutex);
                                if (pickPending)
                                {
                                    if (pickSceneVersion == currentVersion)
                                        renderer.requestPick(pickPixel, pickSerial);
                                    pickPending = false;
                                }
                            }
                            quint64 request, revision;
                            unsigned id;
                            if (renderer.pollPick(request, id, revision) && revision == currentVersion)
                                emit picked(id > 0 && id <= scene.instances.size()
                                                ? scene.instances[id - 1].id
                                                : QString(),
                                            request, revision);
                        }
                        if (!job && (renderer.rasterActive() || presentationClock.elapsed() >= 16) &&
                            (presentedRevision != renderer.imageRevision() ||
                             presentedVersion != currentVersion || renderer.rasterActive()))
                        {
                            if (presentationSize != size)
                            {
                                presentationSize = size;
                                minimumPresentationVersion = currentVersion;
                            }
                            if (TextureBuffer::instance()->updateTexture(context, size.width(), size.height(),
                                                                         job ? 0 : renderer.pickFramebuffer(),
                                                                         currentVersion,
                                                                         renderer.displayFramebuffer(),
                                                                         minimumPresentationVersion))
                            {
                                presentationPending = true;
                                presentedRevision = renderer.imageRevision();
                                presentedVersion = currentVersion;
                                presentationClock.restart();
                            }
                        }
                        if (job && renderer.samples() >= settings.samples)
                        {
                            renderer.finishDenoise(snapshot);
                            emit resultReady(renderer.result(snapshot), !m_cancel.load());
                            setState(m_cancel ? RenderJobState::Stopped : RenderJobState::Completed);
                            jobSeconds = jobClock.elapsed() / 1000.;
                            job = false;
                            renderer.formal = false;
                            m_jobActive = false;
                            m_cancel = false;
                            markSceneDirty(toSceneDirtyFlags(SceneDirtyFlag::Camera));
                        }
                        else if (job && renderer.completeRound() && interval.elapsed() >= 200)
                            emit resultReady(renderer.result(snapshot), false);
                        // 光栅化每帧都是新画面，没有完整轮次概念，不能按采样进度限流。
                        if (!renderer.rasterActive() && renderer.samples() == before &&
                            renderer.completedTiles() == tilesBefore)
                            msleep(16);
                    }
                    catch (const std::exception &e)
                    {
                        job = false;
                        renderer.formal = false;
                        m_jobActive = false;
                        m_cancel = false;
                        setState(RenderJobState::Failed, QString::fromUtf8(e.what()));
                        markSceneDirty(kInitialSceneDirty);
                        msleep(50);
                    }
                }
                if (job)
                    jobSeconds = jobClock.elapsed() / 1000.;
            }
            catch (const std::exception &e)
            {
                jobSeconds = jobClock.isValid() ? jobClock.elapsed() / 1000. : 0;
                job = false;
                renderer.formal = false;
                m_jobActive = false;
                m_cancel = false;
                setState(RenderJobState::Failed, QString::fromUtf8(e.what()));
                markSceneDirty(kInitialSceneDirty);
            }
            const bool showingJob = job || (!m_previewVisible && jobClock.isValid());
            if (gpuWork)
                renderer.submitGpuBoundary();
            auto snapshot = showingJob ? jobSnapshot : RenderParams::instance().snapshot();
            if (interval.elapsed() >= 200)
            {
                renderer.pollGpuTimers();
                auto s = renderer.stats;
                interval.restart();
                s.seconds = clock.elapsed() / 1000.;
                s.version = currentVersion;
                s.fps = wasSampling ? rates.fps(s.seconds) : 0;
                s.rasterFps = wasRaster ? rasterRates.fps(s.seconds) : 0;
                s.tileFps = wasSampling ? rates.tileFps(s.seconds) : 0;
                s.samples = renderer.samples();
                s.target = snapshot.maxRenderFrames;
                s.tiled = snapshot.useTileRendering;
                s.previewDenoising = renderer.previewDenoising();
                s.size = showingJob ? settings.size : renderer.renderSize();
                s.state = state;
                s.blasMs = scene.blasBuildMs;
                s.blasBuilds = scene.blasBuildCount;
                s.tlasMs = scene.tlasUpdateMs;
                s.allocatedBytes = renderer.allocatedBytes() + quint64(size.width()) * size.height() * 3 * 8;
                s.jobSeconds = jobSeconds;
                emit statsReady(s);
            }
        }
    }
    catch (const std::exception &e)
    {
        m_jobActive = false;
        emit jobStateChanged(RenderJobState::Failed, QString::fromUtf8(e.what()));
    }
    TextureBuffer::instance()->deleteTexture(context);
    context->doneCurrent();
    delete context;
    context = nullptr;
}
