#include "learnQT.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <iostream>

// A visible Qt window exercises widget composition, worker progress and UI timer latency together.
void learnQT::configureInteractionRegression()
{
    auto args = QCoreApplication::arguments();
    int option = args.indexOf("--interaction-regression");
    if (option < 0 || option + 1 >= args.size())
        return;
    const QString output = QFileInfo(args[option + 1]).absoluteFilePath();
    QDir().mkpath(output);
    struct State
    {
        QElapsedTimer clock, stageClock;
        qint64 lastTick = 0;
        int phase = -1, frames = 0, images = 0;
        RenderStats stats;
        QVector<double> delays;
        QJsonArray samples, stages;
    };
    auto state = std::make_shared<State>();
    state->clock.start();
    connect(viewport, &GLWidget::framePresented, this, [state] { ++state->frames; });
    connect(viewport, &GLWidget::renderThreadReady, this, [this, state] {
        connect(viewport->renderThread(), &RenderThread::imageReady, this, [state] { ++state->images; });
        connect(viewport->renderThread(), &RenderThread::statsReady, this, [state](RenderStats s) {
            state->stats = s;
            state->samples.append(QJsonObject{{"phase", state->phase},
                                              {"time", state->clock.elapsed()},
                                              {"spp", s.samples},
                                              {"fps", s.fps},
                                              {"tiles", s.tileFps},
                                              {"oidnMs", s.oidnMs},
                                              {"width", s.size.width()},
                                              {"height", s.size.height()},
                                              {"accumulation", double(s.accumulationVersion)},
                                              {"denoised", double(s.denoisedVersion)}});
        });
    });
    auto timer = new QTimer(this);
    timer->setTimerType(Qt::PreciseTimer);
    timer->setInterval(16);
    connect(timer, &QTimer::timeout, this, [this, state, timer, output] {
        qint64 now = state->clock.elapsed();
        if (state->lastTick && state->phase >= 0)
            state->delays.append(now - state->lastTick);
        state->lastTick = now;
        if (!viewport->renderThread() || m_loading)
            return;
        auto next = [&] {
            ++state->phase;
            state->stageClock.restart();
            state->frames = state->images = 0;
            state->delays.clear();
            std::cout << "Interaction phase " << state->phase << std::endl;
        };
        if (state->phase == -1)
        {
            resize(1920, 1080);
            treeDock->show();
            inspectorDock->show();
            performanceDock->show();
            splitDockWidget(treeDock, inspectorDock, Qt::Vertical);
            splitDockWidget(inspectorDock, performanceDock, Qt::Vertical);
            resizeDocks({treeDock, inspectorDock, performanceDock}, {240, 300, 170}, Qt::Vertical);
            next();
            return;
        }
        if (state->stageClock.elapsed() < (state->phase == 3 ? 4000 : 8000))
            return;
        auto sorted = state->delays;
        std::sort(sorted.begin(), sorted.end());
        auto percentile = [&](double p) {
            return sorted.isEmpty() ? 0. : sorted[int((sorted.size() - 1) * p)];
        };
        state->stages.append(QJsonObject{{"phase", state->phase},
                                         {"duration", state->stageClock.elapsed()},
                                         {"frames", state->frames},
                                         {"images", state->images},
                                         {"uiMedianMs", percentile(.5)},
                                         {"uiP95Ms", percentile(.95)},
                                         {"uiP99Ms", percentile(.99)},
                                         {"uiMaxMs", percentile(1)},
                                         {"panelWidth", performance->width()},
                                         {"panelHeight", performance->height()}});
        if (state->phase == 0)
        {
            beginSceneLoad(QStringLiteral(RESOURCE_DIR "/scenes/glslpt_Camera_01_4k_gltf.scene.json"));
            next();
        }
        else if (state->phase == 1)
        {
            treeDock->hide();
            inspectorDock->hide();
            performanceDock->show();
            performanceDock->raise();
            next();
        }
        else if (state->phase == 2)
        {
            grab().save(output + "/performance-full.png");
            RenderParams::instance().setDenoise(false);
            RenderParams::instance().setMaxRenderFrames(1);
            next();
        }
        else
        {
            QFile report(output + "/metrics.json");
            report.open(QIODevice::WriteOnly);
            report.write(
                QJsonDocument(QJsonObject{{"stages", state->stages}, {"samples", state->samples}}).toJson());
            std::cout << QJsonDocument(state->stages).toJson().constData() << std::endl;
            timer->stop();
            QCoreApplication::exit(0);
        }
    });
    timer->start();
}

void learnQT::configurePreviewRegression()
{
    const auto args = QCoreApplication::arguments();
    int option = args.indexOf("--preview-regression");
    if (option < 0 || option + 1 >= args.size())
        return;
    const QString output = QFileInfo(args[option + 1]).absoluteFilePath();
    QDir().mkpath(output);
    struct State
    {
        int phase = 0, images = 0, idleImages = 0;
        quint64 oldEpoch = 0, presentedVersion = 0;
        qint64 lastImageTime = 0;
        QElapsedTimer clock, phaseClock;
        RenderStats stats;
        QJsonArray observations;
        bool overlapped = false;
    };
    auto state = std::make_shared<State>();
    state->clock.start();
    state->phaseClock.start();
    connect(viewport, &GLWidget::framePresented, this, [this, state] {
        state->presentedVersion = viewport->sceneVersion();
    });
    connect(viewport, &GLWidget::renderThreadReady, this, [this, state] {
        connect(viewport->renderThread(), &RenderThread::imageReady, this, [state] {
            ++state->images;
            state->lastImageTime = state->clock.elapsed();
        });
        connect(viewport->renderThread(), &RenderThread::statsReady, this, [state](RenderStats s) {
            state->stats = s;
            state->observations.append(QJsonObject{{"time", state->clock.elapsed()},
                                                   {"phase", state->phase},
                                                   {"spp", s.samples},
                                                   {"denoisedSpp", s.denoisedSamples},
                                                   {"busy", s.previewDenoising},
                                                   {"snapshotSpp", s.previewDenoiseSamples},
                                                   {"epoch", double(s.accumulationVersion)},
                                                   {"denoisedEpoch", double(s.denoisedVersion)},
                                                   {"oidnMs", s.oidnMs}});
        });
    });
    QTimer::singleShot(0, this, [this] {
        logDock->hide();
        treeDock->hide();
        inspectorDock->hide();
        performanceDock->show();
        viewport->setFixedSize(qRound(1280 / viewport->devicePixelRatioF()),
                               qRound(720 / viewport->devicePixelRatioF()));
        resize(1366, 768);
    });
    auto timer = new QTimer(this);
    timer->setInterval(30);
    connect(timer, &QTimer::timeout, this, [this, state, timer, output] {
        auto finish = [&](const QString &error) {
            QFile report(output + "/preview.json");
            report.open(QIODevice::WriteOnly);
            report.write(QJsonDocument(QJsonObject{{"error", error},
                                                   {"overlapped", state->overlapped},
                                                   {"observations", state->observations}})
                             .toJson());
            if (!error.isEmpty())
                std::cerr << "Preview regression: " << error.toStdString() << std::endl;
            else
                std::cout << "Preview: empty scene, asynchronous OIDN, stale-result rejection, resize and "
                             "wheel passed"
                          << std::endl;
            timer->stop();
            QCoreApplication::exit(error.isEmpty() ? 0 : 9);
        };
        if (state->clock.elapsed() > 60000)
        {
            finish("Timeout at phase " + QString::number(state->phase));
            return;
        }
        if (m_loading || !viewport->renderThread() || state->stats.version != viewport->sceneVersion())
            return;
        const auto &s = state->stats;
        if (s.denoisedVersion && s.denoisedVersion != s.accumulationVersion)
        {
            finish("Stale denoise reached the display");
            return;
        }
        auto next = [&] {
            ++state->phase;
            state->phaseClock.restart();
            std::cout << "Preview phase " << state->phase << std::endl;
        };
        if (state->phase == 0)
        {
            if (s.samples != 1)
                return;
            if (s.oidnMs != 0 || s.previewDenoising || s.fps != 0)
            {
                finish("Empty scene repeatedly renders or denoises");
                return;
            }
            beginSceneLoad(QStringLiteral(RESOURCE_DIR "/scenes/glslpt_Camera_01_4k_gltf.scene.json"));
            next();
        }
        else if (state->phase == 1)
        {
            if (editor->document.root["objects"].toArray().isEmpty() || !s.samples)
                return;
            const auto original = viewport->camera;
            for (int i = 0; i < 300; ++i)
            {
                QWheelEvent wheel(QPointF(viewport->rect().center()),
                                  QPointF(viewport->mapToGlobal(viewport->rect().center())), QPoint(),
                                  QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(viewport, &wheel);
                if (QVector3D::dotProduct(original.front, viewport->camera.front) < .99f ||
                    viewport->camera.r <= 0)
                {
                    finish("Actual viewport wheel event flipped the camera");
                    return;
                }
            }
            editor->setCamera(original);
            auto denoiseSettings = editor->document.settings();
            denoiseSettings.denoise = true;
            applyPreviewSettingsForTesting(denoiseSettings);
            next();
        }
        else if (state->phase == 2)
        {
            if (!s.previewDenoising || s.previewDenoiseSamples <= 0 ||
                s.samples <= s.previewDenoiseSamples + 1)
                return;
            state->overlapped = true;
            state->oldEpoch = s.accumulationVersion;
            // Change both scene version and resolution while OIDN still owns the previous snapshot.
            viewport->camera.processMousePan(3, 2);
            editor->setCamera(viewport->camera);
            viewport->setFixedSize(qRound(800 / viewport->devicePixelRatioF()),
                                   qRound(500 / viewport->devicePixelRatioF()));
            next();
        }
        else if (state->phase == 3)
        {
            if (s.accumulationVersion <= state->oldEpoch || !s.denoisedVersion)
                return;
            if (s.auxiliarySize != QSize(800, 500) || s.normalMinimum < -1.00001 ||
                s.normalMaximum > 1.00001 || !s.denoisedSamples || s.samples < s.denoisedSamples)
            {
                finish("New preview has mismatched auxiliary dimensions, normals or samples");
                return;
            }
            grab().save(output + "/denoised-preview.png");
            beginSceneLoad(QString());
            next();
        }
        else if (state->phase == 4)
        {
            if (!editor->document.root["objects"].toArray().isEmpty() || s.samples != 1)
                return;
            if (s.denoisedVersion || s.oidnMs || s.previewDenoising)
                return;
            // Completion stats can precede the last GPU/display handoff. Start idle observation after it drains.
            if (state->presentedVersion != s.version || state->clock.elapsed() - state->lastImageTime < 250)
                return;
            state->idleImages = state->images;
            next();
        }
        else if (state->phase == 5 && state->phaseClock.elapsed() >= 1000)
        {
            if (s.samples != 1 || s.fps || s.tileFps || state->images != state->idleImages)
            {
                finish(QString("Idle empty preview: spp=%1 fps=%2 tiles=%3 images=%4->%5")
                    .arg(s.samples).arg(s.fps).arg(s.tileFps).arg(state->idleImages).arg(state->images));
                return;
            }
            auto image = viewport->grabFramebuffer();
            if (image.isNull() || qGray(image.pixel(image.width() / 2, image.height() / 2)) < 20)
            {
                finish("Final empty background was not published");
                return;
            }
            finish({});
        }
    });
    timer->start();
}
