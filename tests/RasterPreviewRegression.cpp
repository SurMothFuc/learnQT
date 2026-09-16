#include "WorkspaceUi.h"
#include "learnQT.h"
#include "SceneTreeModel.h"
#include "UiDiagnostics.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <cmath>
#include <iostream>

// 光栅化交互预览的界面回归：交互切换、停手回落、锁定、两种替代回退策略与设置往返。
// 全部通过控件/控制器 API 驱动，不使用屏幕坐标点击。
void learnQT::configureRasterRegression()
{
    const QStringList arguments = QCoreApplication::arguments();
    const int option = arguments.indexOf(QStringLiteral("--raster-regression"));
    if (option < 0 || option + 1 >= arguments.size())
    {
        return;
    }
    const QString output = QFileInfo(arguments[option + 1]).absoluteFilePath();
    QDir().mkpath(output);

    // Reproducible visible-window throughput measurement; also count producer notifications,
    // since widget repaints alone can redraw the same shared image.
    if (arguments.contains("--raster-profile"))
    {
        struct Profile {
            QElapsedTimer clock;
            int stage = -1, images = 0, paints = 0, fresh = 0;
            double gpuSum = 0;
            int reports = 0;
            double framesSum = 0, windowSum = 0, boundaryWaitSum = 0, rasterSubmitSum = 0;
            double ticksSum = 0, loopSum = 0, cadenceSleepSum = 0, tailSum = 0;
            int pickMaxPasses = 0;
            double pickMaxMs = 0;
            double pickSubmitSum = 0, pickGpuSum = 0, pickPassSum = 0, presentSum = 0;
            // 直接量墙钟间隔：发布间隔与 UI 心跳间隔，避免只靠率估计误判。
            QElapsedTimer sinceImage, sinceTick;
            double lastImageMs = 0, maxImageGapMs = 0;
            int uiTicks = 0;
            double lastUiTickMs = 0, maxUiGapMs = 0;
            UiDiagnostics editor, editorLive;
            RenderStats stats;
            QJsonObject editorSlots, editorSlotMax;
            int treeItems = 0;
            quint64 treeDataCalls = 0;
            QJsonArray results;
            Camera camera;
        };
        auto p = std::make_shared<Profile>();
        auto timer = new QTimer(this);
        timer->setInterval(16);
        auto attach = [this, p] {
            connect(viewport->renderThread(), &RenderThread::imageReady, this, [p] {
                ++p->images;
                if (p->sinceImage.isValid())
                    p->maxImageGapMs = std::max(p->maxImageGapMs, double(p->sinceImage.elapsed()));
                p->sinceImage.restart();
            });
            connect(viewport->renderThread(), &RenderThread::statsReady, this, [p](RenderStats s) {
                p->stats = s;
                p->gpuSum += s.rasterMs;
                p->framesSum += s.frames;
                p->ticksSum += s.ticks;
                p->windowSum += s.windowMs;
                p->loopSum += s.loopMs;
                p->cadenceSleepSum += s.cadenceSleepMs;
                p->tailSum += s.tailMs;
                p->boundaryWaitSum += s.boundaryWaitMs;
                p->rasterSubmitSum += s.rasterSubmitMs;
                p->pickSubmitSum += s.pickSubmitMs;
                p->pickGpuSum += s.pickWindowMs;
                p->pickPassSum += s.pickPasses;
                p->pickMaxPasses = std::max(p->pickMaxPasses, s.pickPasses);
                p->pickMaxMs = std::max(p->pickMaxMs, s.pickMaxMs);
                p->presentSum += s.presentMs;
                ++p->reports;
            });
        };
        if (viewport->renderThread()) attach();
        else connect(viewport, &GLWidget::renderThreadReady, this, attach);
        connect(viewport, &GLWidget::framePresented, this, [p] { ++p->paints; });
        connect(viewport, &GLWidget::freshFramePresented, this, [p] { ++p->fresh; });
        connect(timer, &QTimer::timeout, this, [this, p, timer, output] {
            if (!viewport->renderThread() || m_loading) return;
            // UI 线程上采样编辑器提交计数；相机拖动时这些槽函数是同步执行的。
            p->editorLive = UiDiagnostics::snapshot();
            if (p->stage == -1) {
                auto d = editor->document;
                auto s = d.settings();
                s.rasterLocked = true; s.renderLow = false; s.denoise = false;
                d.captureSettings(s);
                editor->submit(d, "Raster profile", EditorController::Display);
                p->camera = viewport->camera;
                UiDiagnostics::instance().reset();
                p->stage = 0; p->clock.start();
            }
            if (p->sinceTick.isValid())
                p->maxUiGapMs = std::max(p->maxUiGapMs, double(p->sinceTick.elapsed()));
            p->sinceTick.restart();
            ++p->uiTicks;
            if (p->stage == 2) {
                auto camera = p->camera;
                camera.processMouseScroll(12.0 * std::sin(p->clock.elapsed() / 500.0));
                editor->setCamera(camera);
            }
            if (p->stage == 0 && (!p->stats.rasterActive || p->images < 3)) return;
            if (p->clock.elapsed() < (p->stage == 0 ? 2000 : 5000)) return;
            if (p->stage > 0) {
                QJsonObject slotTotals;
                for (int i = 0; i < UiSlotCount; ++i)
                    slotTotals.insert(QString::fromLatin1(UiDiagnostics::slotName(i)),
                                      p->editorLive.slotMs[i]);
                p->editorSlots = slotTotals;
                QJsonObject slotMax;
                for (int i = 0; i < UiSlotCount; ++i)
                    slotMax.insert(QString::fromLatin1(UiDiagnostics::slotName(i)),
                                   p->editorLive.maxSlotMs[i]);
                p->editorSlotMax = slotMax;
                if (auto *model = findChild<SceneTreeModel *>())
                {
                    p->treeItems = model->itemCount();
                    p->treeDataCalls = model->dataCalls;
                }
                const double reports = std::max(1, p->reports);
                const char *stageName = p->stage == 1 ? "static" : (p->stage == 2 ? "camera" : "idle");
                p->results.append(QJsonObject{{"stage", stageName},
                    {"seconds", p->clock.elapsed() / 1000.}, {"images", p->images},
                    {"paints", p->paints}, {"publishedFps", p->images * 1000. / p->clock.elapsed()},
                    {"paintFps", p->paints * 1000. / p->clock.elapsed()},
                    {"displayedFps", p->fresh * 1000. / p->clock.elapsed()},
                    {"reportedRasterFps", p->stats.rasterFps},
                    {"rasterGpuMs", p->gpuSum / reports},
                    // 每帧耗时分解：窗口总时长、等待 GPU 边界、光栅化提交、拾取提交/GPU、显示拷贝。
                    {"framesPerReport", p->framesSum / reports},
                    {"ticksPerReport", p->ticksSum / reports},
                    {"windowMs", p->windowSum / reports},
                    {"loopMs", p->loopSum / reports},
                    {"cadenceSleepMs", p->cadenceSleepSum / reports},
                    {"tailMs", p->tailSum / reports},
                    {"boundaryWaitMs", p->boundaryWaitSum / reports},
                    {"rasterSubmitMs", p->rasterSubmitSum / reports},
                    {"pickSubmitMs", p->pickSubmitSum / reports},
                    {"pickGpuMs", p->pickGpuSum / reports},
                    {"pickPasses", p->pickPassSum / reports},
                    {"pickMaxPasses", p->pickMaxPasses},
                    {"pickMaxMs", p->pickMaxMs},
                    {"presentMs", p->presentSum / reports},
                    {"maxImageGapMs", p->maxImageGapMs},
                    {"uiTicks", p->uiTicks},
                    {"maxUiGapMs", p->maxUiGapMs},
                    // UI 相机提交路径的分解：比较文档、校验、undo、同步槽函数。
                    {"editorSubmits", p->editorLive.submits},
                    {"editorSkipped", p->editorLive.skipped},
                    {"editorCompareMs", p->editorLive.compareMs},
                    {"editorValidateMs", p->editorLive.validateMs},
                    {"editorPushMs", p->editorLive.pushMs},
                    {"editorSignalMs", p->editorLive.signalMs},
                    {"editorMaxSubmitMs", p->editorLive.maxSubmitMs},
                    {"editorSubmitMs", p->editorLive.submitMs},
                    {"editorMaxSubmitCallMs", p->editorLive.maxSubmitCallMs},
                    {"editorSlots", p->editorSlots},
                    {"editorSlotMax", p->editorSlotMax},
                    {"treeItems", p->treeItems},
                    {"treeDataCalls", double(p->treeDataCalls)},
                    {"width", p->stats.size.width()}, {"height", p->stats.size.height()}});
            }
            if (++p->stage == 4) {
                timer->stop();
                QFile f(output + "/profile.json");
                if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(p->results).toJson());
                editor->markSaved(); m_sceneDirty = false;
                QCoreApplication::exit(0);
                return;
            }
            p->clock.restart(); p->images = p->paints = p->reports = p->fresh = 0; p->gpuSum = 0;
            p->framesSum = p->windowSum = p->boundaryWaitSum = p->rasterSubmitSum = 0;
            p->ticksSum = p->loopSum = p->cadenceSleepSum = p->tailSum = 0;
            p->pickSubmitSum = p->pickGpuSum = p->pickPassSum = p->presentSum = 0;
            p->maxImageGapMs = p->maxUiGapMs = 0; p->uiTicks = 0;
            p->pickMaxPasses = 0; p->pickMaxMs = 0;
            p->sinceImage.restart(); p->sinceTick.restart();
        });
        timer->start();
        return;
    }

    struct State
    {
        int phase = 0;
        int freshFrames = 0, freshAtInteraction = 0;
        QElapsedTimer phaseClock, total;
        QElapsedTimer interaction;
        RenderStats stats;
        bool statsReady = false;
        int samplesAtRaster = 0;
        quint64 epochBeforeIdle = 0;
        int samplesBeforeIdle = 0;
        Camera base;
        QSize fullSize, lowSize;
        QStringList notes;
    };
    auto state = std::make_shared<State>();
    connect(viewport, &GLWidget::freshFramePresented, this, [state] { ++state->freshFrames; });
    state->total.start();
    state->base = viewport->camera;
    connect(viewport, &GLWidget::renderThreadReady, this, [state] { state->statsReady = false; });
    if (viewport->renderThread())
    {
        connect(viewport->renderThread(), &RenderThread::statsReady, this, [state](RenderStats s) {
            state->stats = s;
            state->statsReady = true;
        });
    }
    else
    {
        connect(viewport, &GLWidget::renderThreadReady, this, [this, state] {
            connect(viewport->renderThread(), &RenderThread::statsReady, this, [state](RenderStats s) {
                state->stats = s;
                state->statsReady = true;
            });
        });
    }

    auto timer = new QTimer(this);
    timer->setInterval(16);
    // 回归自己把相机对准场景：内置测试场景的默认相机可能根本不包含模型。
    auto sceneBounds = [this](QVector3D &minimum, QVector3D &maximum) {
        bool valid = false;
        const auto &scene = Scene::getInstance();
        for (const auto &instance : scene.instances)
        {
            if (!instance.visible || instance.mesh < 0 || instance.mesh >= int(scene.meshes.size()))
                continue;
            const auto &mesh = scene.meshes[instance.mesh];
            if (!mesh)
                continue;
            for (const auto &triangle : mesh->triangles)
            {
                const QVector3D positions[3] = {triangle.p1, triangle.p2, triangle.p3};
                for (const auto &position : positions)
                {
                    const QVector3D world = (instance.transform * QVector4D(position, 1.0f)).toVector3D();
                    minimum.setX(valid ? std::min(minimum.x(), world.x()) : world.x());
                    minimum.setY(valid ? std::min(minimum.y(), world.y()) : world.y());
                    minimum.setZ(valid ? std::min(minimum.z(), world.z()) : world.z());
                    maximum.setX(valid ? std::max(maximum.x(), world.x()) : world.x());
                    maximum.setY(valid ? std::max(maximum.y(), world.y()) : world.y());
                    maximum.setZ(valid ? std::max(maximum.z(), world.z()) : world.z());
                    valid = true;
                }
            }
        }
        return valid;
    };
    auto aimCamera = [this, sceneBounds, state](bool remember) {
        QVector3D minimum, maximum;
        if (!sceneBounds(minimum, maximum))
            return false;
        const QVector3D center = (minimum + maximum) * 0.5f;
        const float radius = std::max(0.1f, (maximum - minimum).length() * 0.5f);
        const QVector3D direction = QVector3D(-0.45f, 0.28f, -0.85f).normalized();
        viewport->camera.restoreState(center + direction * radius * 2.6f, center, QVector3D(0, 1, 0), 45.0f);
        editor->setCamera(viewport->camera);
        if (remember)
            state->base = viewport->camera;
        return true;
    };
    auto applySettings = [this](const RenderParams::Snapshot &settings) {
        auto d = editor->document;
        d.captureSettings(settings);
        editor->submit(d, QStringLiteral("Raster regression setup"), EditorController::Display);
    };
    // 相机变化是“交互”的真实来源，走编辑器命令而不是直接改渲染器内部状态。
    auto nudgeCamera = [this, state](double step) {
        viewport->camera.processMouseScroll(step);
        editor->setCamera(viewport->camera);
        state->interaction.restart();
    };
    auto restoreCamera = [this, state] {
        viewport->camera = state->base;
        editor->setCamera(viewport->camera);
    };
    auto finish = [this, timer, output, state](const QString &error) {
        timer->stop();
        // 回归结束前把交互策略恢复成默认值；直接写回文档设置，不再触发一次提交。
        RenderParams::Snapshot defaults;
        defaults.useTileRendering = editor->document.settings().useTileRendering;
        defaults.tileSize = editor->document.settings().tileSize;
        defaults.maxBounces = editor->document.settings().maxBounces;
        defaults.maxRenderFrames = editor->document.settings().maxRenderFrames;
        defaults.denoise = editor->document.settings().denoise;
        defaults.interactionMode = RenderParams::InteractionRaster;
        defaults.rasterLocked = false;
        defaults.renderLow = false;
        defaults.interactionIdleMs = 250;
        editor->document.captureSettings(defaults);
        RenderParams::instance().applySnapshot(defaults);
        m_sceneDirty = false;
        editor->markSaved();
        QJsonObject report{{"passed", error.isEmpty()},
                           {"error", error},
                           {"phase", state->phase},
                           {"notes", QJsonArray::fromStringList(state->notes)}};
        QFile file(output + "/report.json");
        if (file.open(QIODevice::WriteOnly))
        {
            file.write(QJsonDocument(report).toJson());
        }
        if (!error.isEmpty())
        {
            grab().save(output + "/failure.png");
        }
        std::cout << (error.isEmpty() ? "Raster preview regression passed" : error.toStdString()) << std::endl;
        QCoreApplication::exit(error.isEmpty() ? 0 : 12);
    };

    connect(timer, &QTimer::timeout, this,
            [this, state, timer, finish, applySettings, nudgeCamera, restoreCamera, aimCamera, output] {
        if (state->total.elapsed() > 90000)
        {
            finish(QStringLiteral("Raster preview regression timed out"));
            return;
        }
        if (m_loading || !viewport->renderThread() || !state->statsReady)
        {
            return;
        }

        // 1) 交互期间切到光栅化，且路径追踪采样不再增长。
        if (state->phase == 0)
        {
            if (!aimCamera(true))
            {
                finish(QStringLiteral("The regression scene has no visible geometry to frame"));
                return;
            }
            RenderParams::Snapshot settings = editor->document.settings();
            settings.interactionMode = RenderParams::InteractionRaster;
            settings.rasterLocked = false;
            settings.interactionIdleMs = 400;
            settings.maxRenderFrames = 16;
            settings.denoise = false;
            applySettings(settings);
            state->interaction.start();
            state->freshAtInteraction = state->freshFrames;
            nudgeCamera(60);
            state->phase = 1;
            state->phaseClock.start();
            return;
        }
        if (state->phase == 1)
        {
            if (state->phaseClock.elapsed() < 700)
            {
                nudgeCamera(0.1);
                return;
            }
            if (!state->stats.rasterActive)
            {
                finish(QStringLiteral("Camera interaction did not switch to the raster preview"));
                return;
            }
            if (state->freshFrames - state->freshAtInteraction < 3)
            {
                finish(QStringLiteral("Continuous camera input starved new viewport images"));
                return;
            }
            const QImage frame = viewport->grabFramebuffer().convertToFormat(QImage::Format_RGB32);
            frame.save(output + "/raster-frame.png");
            double sum = 0, square = 0;
            for (int y = 0; y < frame.height(); ++y)
                for (int x = 0; x < frame.width(); ++x)
                {
                    const double value = qGray(frame.pixel(x, y));
                    sum += value;
                    square += value * value;
                }
            const double count = double(frame.width()) * frame.height();
            const double mean = count > 0 ? sum / count : 0;
            const double variance = count > 0 ? square / count - mean * mean : 0;
            if (mean < 2.0 || variance < 4.0)
            {
                finish(QStringLiteral("Raster preview frame is black or uniform (mean %1 variance %2)")
                           .arg(mean)
                           .arg(variance));
                return;
            }
            if (state->stats.rasterMs <= 0.0)
            {
                finish(QStringLiteral("Raster preview did not report a GPU frame time"));
                return;
            }
            state->notes << QStringLiteral("rasterFrame mean=%1 variance=%2 rasterMs=%3 samples=%4")
                                .arg(mean)
                                .arg(variance)
                                .arg(state->stats.rasterMs)
                                .arg(state->stats.samples);
            state->samplesAtRaster = state->stats.samples;
            state->epochBeforeIdle = state->stats.accumulationVersion;
            state->samplesBeforeIdle = state->stats.samples;
            state->phase = 2;
            state->phaseClock.start();
            return;
        }

        // 2) 停手超过 interactionIdleMs 后回到路径追踪，并从新一轮累积重新开始。
        if (state->phase == 2)
        {
            if (state->phaseClock.elapsed() < 1500)
                return;
            restoreCamera();
            state->phase = 3;
            state->phaseClock.start();
            return;
        }
        if (state->phase == 3)
        {
            if (state->phaseClock.elapsed() < 2500)
                return;
            if (state->stats.rasterActive)
            {
                finish(QStringLiteral("Raster preview stayed active after the interaction window"));
                return;
            }
            if (state->stats.accumulationVersion <= state->epochBeforeIdle)
            {
                finish(QStringLiteral("Leaving the raster preview did not restart accumulation"));
                return;
            }
            if (state->stats.samples <= 0 || state->stats.samples > state->samplesBeforeIdle + 64)
            {
                finish(QStringLiteral("Path traced preview did not restart from a fresh accumulation"));
                return;
            }
            state->fullSize = state->stats.size;
            state->notes << QStringLiteral("resumed samples=%1 size=%2x%3")
                                .arg(state->stats.samples)
                                .arg(state->stats.size.width())
                                .arg(state->stats.size.height());
            // 3) 锁定光栅化后路径追踪完全不跑。
            RenderParams::Snapshot settings = editor->document.settings();
            settings.rasterLocked = true;
            applySettings(settings);
            state->phase = 4;
            state->phaseClock.start();
            return;
        }
        if (state->phase == 4)
        {
            if (state->phaseClock.elapsed() < 1800)
                return;
            if (!state->stats.rasterActive)
            {
                finish(QStringLiteral("Locked raster preview did not stay active without interaction"));
                return;
            }
            if (state->stats.rasterFps <= 0)
            {
                finish(QStringLiteral("Locked raster preview did not report completed-frame FPS"));
                return;
            }
            if (state->stats.samples != state->samplesAtRaster)
            {
                finish(QStringLiteral("Path tracing kept sampling while the raster preview was locked"));
                return;
            }
            RenderParams::Snapshot settings = editor->document.settings();
            settings.rasterLocked = false;
            applySettings(settings);
            state->phase = 5;
            state->phaseClock.start();
            return;
        }

        // 4) 交互策略 2：降低分辨率路径追踪——不切渲染模式，只降分辨率。
        if (state->phase == 5)
        {
            if (state->phaseClock.elapsed() < 1200)
                return;
            if (state->stats.rasterActive)
            {
                finish(QStringLiteral("Raster preview stayed active after unlocking"));
                return;
            }
            RenderParams::Snapshot settings = editor->document.settings();
            settings.interactionMode = RenderParams::InteractionLowResolution;
            settings.renderLow = false;
            settings.maxRenderFrames = 0;
            applySettings(settings);
            state->interaction.start();
            nudgeCamera(60);
            state->phase = 6;
            state->phaseClock.start();
            return;
        }
        if (state->phase == 6)
        {
            if (state->phaseClock.elapsed() < 1200)
            {
                nudgeCamera(0.1);
                return;
            }
            if (state->stats.rasterActive)
            {
                finish(QStringLiteral("Low resolution fallback must not enable the raster preview"));
                return;
            }
            state->lowSize = state->stats.size;
            if (state->fullSize.isEmpty() || state->lowSize.width() >= state->fullSize.width())
            {
                finish(QStringLiteral("Low resolution fallback did not lower the render resolution (%1 vs %2)")
                           .arg(state->lowSize.width())
                           .arg(state->fullSize.width()));
                return;
            }
            state->notes << QStringLiteral("lowResolution %1x%2 vs full %3x%4")
                                .arg(state->lowSize.width())
                                .arg(state->lowSize.height())
                                .arg(state->fullSize.width())
                                .arg(state->fullSize.height());
            // 5) 交互策略 0：什么都不做。
            RenderParams::Snapshot settings = editor->document.settings();
            settings.interactionMode = RenderParams::InteractionKeepPathtrace;
            applySettings(settings);
            state->phase = 7;
            state->phaseClock.start();
            return;
        }
        if (state->phase == 7)
        {
            if (state->phaseClock.elapsed() < 1500)
                return;
            const QSize before = state->stats.size;
            const int samplesBefore = state->stats.samples;
            state->interaction.start();
            nudgeCamera(60);
            state->notes << QStringLiteral("keepPathtrace before=%1x%2 samples=%3")
                                .arg(before.width())
                                .arg(before.height())
                                .arg(samplesBefore);
            state->phase = 8;
            state->phaseClock.start();
            return;
        }
        if (state->phase == 8)
        {
            if (state->phaseClock.elapsed() < 1200)
                return;
            if (state->stats.rasterActive)
            {
                finish(QStringLiteral("Keep-path-tracing mode must not enable the raster preview"));
                return;
            }
            if (state->stats.size.width() != state->fullSize.width())
            {
                finish(QStringLiteral("Keep-path-tracing mode changed the render resolution (%1 vs %2)")
                           .arg(state->stats.size.width())
                           .arg(state->fullSize.width()));
                return;
            }
            restoreCamera();
            // 6) 三个新字段的保存/加载往返。
            const QString scenePath = output + "/raster-settings.scene.json";
            QString error;
            RenderParams::Snapshot settings = editor->document.settings();
            settings.interactionMode = RenderParams::InteractionLowResolution;
            settings.rasterLocked = true;
            settings.interactionIdleMs = 333;
            applySettings(settings);
            if (!editor->document.saveScene(scenePath, error))
            {
                finish(QStringLiteral("Saving the raster settings failed: %1").arg(error));
                return;
            }
            SceneDocument reloaded;
            if (!SceneDocument::loadScene(scenePath, reloaded, error))
            {
                finish(QStringLiteral("Reloading the raster settings failed: %1").arg(error));
                return;
            }
            const RenderParams::Snapshot round = reloaded.settings();
            if (round.interactionMode != RenderParams::InteractionLowResolution || !round.rasterLocked ||
                round.interactionIdleMs != 333)
            {
                finish(QStringLiteral("Raster settings did not survive a scene round trip"));
                return;
            }
            state->notes << QStringLiteral("roundTrip mode=%1 locked=%2 idle=%3")
                                .arg(round.interactionMode)
                                .arg(round.rasterLocked)
                                .arg(round.interactionIdleMs);
            finish(QString());
        }
    });
    timer->start();
}
