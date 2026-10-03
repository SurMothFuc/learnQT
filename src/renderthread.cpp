#include "renderthread.h"
#include "EditorController.h"
#include "RenderDiagnostics.h"
#include "RenderRateTracker.h"
#include <QElapsedTimer>
#include <cmath>
RenderThread::RenderThread(QSurface *s, QOpenGLContext *shared, QObject *p) : QThread(p), surface(s)
{
    context = new QOpenGLContext;
    context->setFormat(shared->format());
    context->setShareContext(shared);
    context->create();
    context->moveToThread(this);
    qRegisterMetaType<RenderStats>();
    qRegisterMetaType<RenderResultPtr>();
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
void RenderThread::setPreviewAspect(QSize aspect)
{
    {
        QMutexLocker lock(&mutex);
        if (previewAspect == aspect)
            return;
        previewAspect = aspect;
        ++controlRevision;
    }
    if (m_jobActive)
        m_previewRefreshPending = true;
    else
        markSceneDirty(toSceneDirtyFlags(SceneDirtyFlag::Camera));
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
        RenderRateTracker completionRates, publicationRates;
        RenderRateTracker rates;
        RenderRateTracker rasterRates;
        int rasterFrames = 0;
        bool wasRaster = false;
        QElapsedTimer rasterCadence;
        // 拾取补绘用的相机运动计时：版本一旦变化就重启，静默一段时间后补一次 ID 图。
        // 不能依赖 interactionActive 或"本轮是否收到文档"，因为停手后两者都会先失效。
        QElapsedTimer pickMotionClock;
        constexpr qint64 pickSettleMs = 80;
        bool wasSampling = true;
        bool presentationPending = false;
        // 每帧耗时分解，只服务性能剖析与统计，不参与渲染决策。
        RenderLoopDiagnostics diagnostics;
        QElapsedTimer diagnosticsClock;
        diagnosticsClock.start();
        quint64 presentedRevision = 0, presentedVersion = 0;
        quint64 minimumPresentationVersion = 1;
        QSize presentationSize;
        double jobSeconds = 0;
        while (m_running)
        {
            if (!job && !m_jobActive && m_previewRefreshPending.exchange(false))
                markSceneDirty(kInitialSceneDirty);
            // Wait for the bounded tile burst before applying scene or output changes.
            // A bounded driver wait avoids adding a coarse OS sleep after each tile.
            QElapsedTimer boundaryClock;
            boundaryClock.start();
            if (!renderer.waitForGpuBoundary())
            {
                // 超时只是这一批还没结束：本轮不产出统计，等下一次真正进入帧体能。
                diagnostics.reset();
                diagnosticsClock.restart();
                continue;
            }
            diagnostics.boundaryWaitMs += boundaryClock.nsecsElapsed() / 1e6;
            ++diagnostics.ticks;
            QElapsedTimer iterationClock;
            iterationClock.start();
            // Pace from submission start, including GPU work, rather than adding 16 ms
            // to every frame. The previous frame's fence was submitted before this wait.
            if (wasRaster && rasterCadence.isValid())
            {
                const qint64 remainingMs = 16 - rasterCadence.elapsed();
                if (remainingMs > 0)
                {
                    QElapsedTimer cadenceClock;
                    cadenceClock.start();
                    msleep(static_cast<unsigned long>(remainingMs));
                    diagnostics.cadenceSleepMs += cadenceClock.nsecsElapsed() / 1e6;
                }
            }
            if (presentationPending)
            {
                emit imageReady();
                presentationPending = false;
            }
            completionRates.observe(clock.nsecsElapsed()/1e9, int(renderer.stats.completedRounds), 0, 0, true);
            publicationRates.observe(clock.nsecsElapsed()/1e9, int(renderer.stats.publishedFrames), 0, 0, true);
            rates.observe(clock.nsecsElapsed() / 1e9, renderer.samples(), renderer.completedTiles(),
                          renderer.stats.accumulationVersion, wasSampling);
            if (wasRaster)
                ++rasterFrames;
            rasterRates.observe(clock.nsecsElapsed() / 1e9, rasterFrames, 0, 0, wasRaster);
            wasRaster = false;
            wasSampling = false;
            bool gpuWork = false;
            // 本轮刚补绘过拾取缓冲：要求显示端再发布一次，让新的 ID 图进入显示槽。
            bool pickRepublish = false;
            // 本轮拾取缓冲的过期原因与「相机是否已停」，显示阶段也要用到。
            bool pickCameraSettled = false, pickGeometryChanged = false;
            SceneDirtyFlags dirty;
            std::shared_ptr<Scene> prepared;
            SceneDocument doc;
            int changes = 0;
            bool final = true, has = false, start = false, deferredMotion = false;
            quint64 v = 0, batchRevision = 0;
            const QSize previousSize = size;
            const quint64 previousVersion = currentVersion;
            {
                QMutexLocker lock(&mutex);
                batchRevision = controlRevision.load();
                size = viewport;
                if (!m_jobActive && previewAspect.width() > 0 && previewAspect.height() > 0)
                {
                    const double aspect = double(previewAspect.width()) / previewAspect.height();
                    if (double(size.width()) / size.height() > aspect)
                        size.setWidth(std::max(1, int(std::round(size.height() * aspect))));
                    else
                        size.setHeight(std::max(1, int(std::round(size.width() / aspect))));
                }
                const int motionMask=(1 << EditorController::CameraChange) | (1 << EditorController::Transform);
                deferredMotion = (hasDocument || pendingDirty != 0) && !job && !jobRequested && !pendingScene && size == previousSize &&
                    renderer.roundInProgress() && !renderer.rasterActive() &&
                    RenderParams::instance().snapshot().effectiveDenoiseMode() == DenoiseMode::Realtime &&
                    (documentChanges & ~motionMask) == 0 &&
                    (pendingDirty & ~toSceneDirtyFlags(SceneDirtyFlag::Camera)) == 0;
                dirty = deferredMotion ? 0 : pendingDirty;
                if (!deferredMotion) pendingDirty=0;
                prepared = std::move(pendingScene);
                has = hasDocument && !deferredMotion;
                if (has) { doc=pendingDocument; hasDocument=false; changes=documentChanges; documentChanges=0; }
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
                        QMap<QString,bool> previousVisibility;
                        for(const auto &instance:scene.instances) previousVisibility[instance.id]=instance.visible;
                        scene.applyEditorDocument(doc, final,
                                                  (changes & (1 << EditorController::Transform)) == 0);
                        dirty |= (changes & ((1 << EditorController::MaterialChange) | (1 << EditorController::Lighting)))
                                     ? toSceneDirtyFlags(SceneDirtyFlag::Material) : toSceneDirtyFlags(SceneDirtyFlag::Transform);
                        for(const auto &instance:scene.instances)
                            if(previousVisibility.value(instance.id,instance.visible)!=instance.visible)
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
                        jobSnapshot.computePathtrace = RenderParams::instance().computePathtrace();
                        jobSnapshot.renderLow = false;
                        jobSnapshot.useTileRendering = true;
                        jobSnapshot.tileSize = settings.tileSize;
                        jobSnapshot.maxRenderFrames = settings.samples;
                        jobSnapshot.maxBounces = settings.bounces;
                        jobSnapshot.denoise = settings.denoise;
                        jobSnapshot.denoiseMode = settings.denoiseMode;
                        jobSnapshot.antialiasing = settings.antialiasing;
                        jobSnapshot.sampleSeed=settings.sampleSeed;
                        jobSnapshot.rrMinDepth=settings.rrMinDepth;
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
                const bool interactionWasActive = interactionActive;
                if (!job && !m_interactionFallbackDisabled &&
                    (deferredMotion || hasSceneDirtyFlag(dirty, SceneDirtyFlag::Transform) || hasSceneDirtyFlag(dirty, SceneDirtyFlag::Camera) ||
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
                        QElapsedTimer segmentClock;
                        segmentClock.start();
                        renderer.render(size.width(), size.height(), snapshot, dirty, 16, [&] {
                            return !m_running || (job && (m_cancel || m_paused)) ||
                                   controlRevision.load() != batchRevision;
                        });
                        const double renderMs = segmentClock.nsecsElapsed() / 1e6;
                        wasSampling = !renderer.rasterActive() && renderer.samplingActive(snapshot);
                        wasRaster = renderer.rasterActive();
                        ++diagnostics.frames;
                        if (wasRaster)
                            diagnostics.rasterSubmitMs += renderMs;
                        if (!job)
                        {
                            // 拾取缓冲只在几何、相机、材质或分辨率变化后失效。分辨率变化会重建
                            // 拾取纹理，所以必须立刻重绘，否则整张 ID 图是未初始化的。
                            const bool pickTargetsRecreated = size != previousSize;
                            pickGeometryChanged = prepared || pickTargetsRecreated ||
                                                  (changes & ((1 << EditorController::Topology) |
                                                              (1 << EditorController::MaterialChange))) != 0;
                            const bool pickVersionChanged = currentVersion != previousVersion;
                            if (pickVersionChanged)
                                pickMotionClock.restart();
                            // 相机停手后不再有新文档到达（has 为 false），interactionActive 也要等
                            // interactionIdleMs 才落，所以这里用「版本静默了多久」判断停稳：
                            // 拖动期间它一直为假，停手约 80 ms 后为真，只补绘一次。
                            pickCameraSettled = pickMotionClock.isValid() &&
                                                pickMotionClock.elapsed() >= pickSettleMs;
                            if (pickGeometryChanged || currentVersion != pickVersion)
                                pickNeedsRedraw = true;
                            // pickVersion 只在真正重绘后同步（见下），所以 pickNeedsRedraw
                            // 在整个相机拖动期间一直为真，停稳后才会被补绘清除。
                            bool pickDemanded = false;
                            QPoint demandPixel;
                            quint64 demandSerial = 0;
                            {
                                QMutexLocker lock(&mutex);
                                if (pickPending)
                                {
                                    // 请求带了发起时的场景版本：版本已经被后续编辑覆盖时直接丢弃。
                                    pickDemanded = pickSceneVersion == currentVersion;
                                    if (pickDemanded)
                                    {
                                        demandPixel = pickPixel;
                                        // 序列号与像素一起在锁内取走，避免在锁外读被 UI 线程改写的字段。
                                        demandSerial = pickSerial;
                                    }
                                    pickPending = false;
                                }
                            }
                            // 整屏 BVH 拾取 pass 比光栅化预览本身还贵（集显上约 20 ms），
                            // 所以相机拖动期间不按帧重绘；停手（版本静默）或几何变化时补一次，
                            // 否则显示用 ID 图会一直停在旧相机上，选中描边与物体错位。
                            // 注意读回与重绘是两件事：拾取缓冲已经是最新时同样要提交像素读回。
                            const bool interactionEnded =
                                interactionWasActive && !interactionActive && !job;
                            segmentClock.restart();
                            const bool redrawn = pickNeedsRedraw &&
                                                 (pickGeometryChanged || pickDemanded || interactionEnded ||
                                                  pickCameraSettled) &&
                                                 renderer.updatePick(size.width(), size.height(), currentVersion);
                            diagnostics.pickSubmitMs += segmentClock.nsecsElapsed() / 1e6;
                            if (redrawn)
                            {
                                ++diagnostics.pickPasses;
                                pickNeedsRedraw = false;
                                // 只有重绘成功才记录这个版本，否则补绘条件会被自己抹掉。
                                pickVersion = currentVersion;
                            }
                            // 取走本帧完成的拾取计时；没有新结果时返回 0，同一帧不会被重复累计。
                            const double completedPickMs = renderer.takePickGpuMs();
                            diagnostics.pickGpuMs += completedPickMs;
                            if (completedPickMs > diagnostics.pickMaxMs)
                                diagnostics.pickMaxMs = completedPickMs;
                            // 拾取缓冲已经是当前相机与几何，可以立刻读回这一像素。
                            if (pickDemanded)
                                renderer.requestPick(demandPixel, demandSerial);
                            // 本帧的显示复制发生在这之后：它会把已更新的 ID 图一并拷进显示槽。
                            // 但补绘前可能已经发过一版旧 ID 图，所以补绘后必须再发布一次；
                            // pickNeedsRedraw 在补绘后清除，同一段静止期因此只补发布一次。
                            pickRepublish = redrawn;
                            quint64 request, revision;
                            unsigned id;
                            if (renderer.pollPick(request, id, revision) && revision == currentVersion)
                                emit picked(id > 0 && id <= scene.instances.size()
                                                ? scene.instances[id - 1].id
                                                : QString(),
                                            request, revision);
                        }
                        if (!job && (renderer.rasterActive() || renderer.completeRound()) &&
                            (pickRepublish || renderer.rasterActive() ||
                                     presentationClock.elapsed() >= 16) &&
                            (pickRepublish || presentedRevision != renderer.imageRevision() ||
                             presentedVersion != currentVersion || renderer.rasterActive()))
                        {
                            if (presentationSize != size)
                            {
                                presentationSize = size;
                                minimumPresentationVersion = currentVersion;
                            }
                            segmentClock.restart();
                            if (TextureBuffer::instance()->updateTexture(
                                    context, size.width(), size.height(),
                                    job ? 0 : renderer.pickFramebuffer(), currentVersion,
                                    renderer.displayFramebuffer(), minimumPresentationVersion,
                                    job ? ~quint64(0) : renderer.pickBufferVersion(), renderer.imageRevision()))
                            {
                                diagnostics.presentMs += segmentClock.nsecsElapsed() / 1e6;
                                presentationPending = true;
                                if (presentedRevision != renderer.imageRevision()) ++renderer.stats.publishedFrames;
                                presentedRevision = renderer.imageRevision();
                                presentedVersion = currentVersion;
                                presentationClock.restart();
                                pickRepublish = false;
                            }
                        }
                        if (job && renderer.samples() >= settings.samples)
                        {
                            renderer.finishDenoise(snapshot);
                            emit resultReady(renderer.result(snapshot), !m_cancel.load());
                            if(!m_cancel.load())emit linearResultReady(renderer.linearResult(snapshot));
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
            diagnostics.loopMs += iterationClock.nsecsElapsed() / 1e6;
            if (gpuWork)
                renderer.submitGpuBoundary();
            QElapsedTimer tailClock;
            tailClock.start();
            auto snapshot = showingJob ? jobSnapshot : RenderParams::instance().snapshot();
            if (interval.elapsed() >= 200)
            {
                renderer.pollGpuTimers();
                auto s = renderer.stats;
                interval.restart();
                s.seconds = clock.elapsed() / 1000.;
                s.version = currentVersion;
                s.fps = wasSampling ? rates.fps(s.seconds) : 0;
                s.completedFps=completionRates.fps(s.seconds); s.publishedFps=publicationRates.fps(s.seconds);
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
                s.pickMs = renderer.pickGpuMs();
                // 与其他字段一致：这是本统计窗口内的拾取重绘次数与累计 GPU 耗时，不是启动以来的累计值。
                s.pickPasses = diagnostics.pickPasses;
                s.pickWindowMs = diagnostics.pickGpuMs;
                s.pickMaxMs = diagnostics.pickMaxMs;
                s.frames = diagnostics.frames;
                s.ticks = diagnostics.ticks;
                s.boundaryWaitMs = diagnostics.boundaryWaitMs;
                s.cadenceSleepMs = diagnostics.cadenceSleepMs;
                s.tailMs = diagnostics.tailMs;
                s.loopMs = diagnostics.loopMs;
                s.rasterSubmitMs = diagnostics.rasterSubmitMs;
                s.pickSubmitMs = diagnostics.pickSubmitMs;
                s.presentMs = diagnostics.presentMs;
                s.windowMs = diagnosticsClock.elapsed() / 1e6;
                diagnostics.reset();
                diagnosticsClock.restart();
                emit statsReady(s);
            }
            diagnostics.tailMs += tailClock.nsecsElapsed() / 1e6;
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
