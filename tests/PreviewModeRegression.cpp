#include "learnQT.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTimer>
#include <iostream>

void learnQT::configurePreviewModeRegression()
{
    const auto args = QCoreApplication::arguments();
    const int option = args.indexOf("--preview-mode-regression");
    if (option < 0 || option + 1 >= args.size())
        return;
    const QString output = QFileInfo(args[option + 1]).absoluteFilePath();
    const bool mis = args.contains("--mode-mis");
    QDir().mkpath(output);
    struct State
    {
        int phase = 0, presentations = 0, reports = 0;
        bool configured = false;
        RenderStats stats;
        QElapsedTimer clock;
        QImage empty, model;
        QJsonArray results;
    };
    auto state = std::make_shared<State>();
    state->clock.start();
    QTimer::singleShot(0, this, [this, args] {
        const QSize pixels = args.contains("--mode-large") ? QSize(2848, 1782) : QSize(1280, 720);
        viewport->setFixedSize(qRound(pixels.width() / viewport->devicePixelRatioF()),
                               qRound(pixels.height() / viewport->devicePixelRatioF()));
        logDock->hide();
    });
    connect(viewport, &GLWidget::framePresented, this, [state] { ++state->presentations; });
    connect(viewport, &GLWidget::renderThreadReady, this, [this, state] {
        connect(viewport->renderThread(), &RenderThread::statsReady, this, [this, state](RenderStats s) {
            state->stats = s;
            if (s.version == viewport->sceneVersion() && s.samples >= (state->phase < 3 ? 1 : 8))
                ++state->reports;
        });
    });
    auto timer = new QTimer(this);
    timer->setInterval(50);
    connect(timer, &QTimer::timeout, this, [this, state, timer, output, mis] {
        auto finish = [&](QString error) {
            QFile file(output + "/modes.json");
            file.open(QIODevice::WriteOnly);
            file.write(QJsonDocument(QJsonObject{{"error", error},
                                                 {"results", state->results},
                                                 {"phase", state->phase},
                                                 {"samples", state->stats.samples},
                                                 {"presentations", state->presentations}})
                           .toJson());
            std::cout << "Preview modes: " << (error.isEmpty() ? "passed" : error.toStdString()) << std::endl;
            timer->stop();
            QCoreApplication::exit(error.isEmpty() ? 0 : 10);
        };
        if (state->clock.elapsed() > 30000)
        {
            finish("Timed out waiting for the new mode to display");
            return;
        }
        if (m_loading || !viewport->renderThread())
            return;
        if (!state->configured)
        {
            if (state->phase == 3 && !mis)
            {
                viewport->camera.processMouseScroll(1080);
                editor->setCamera(viewport->camera);
            }
            auto d = editor->document;
            auto settings = d.settings();
            settings.denoise = state->phase == 8;
            settings.denoiseMode = settings.denoise ? DenoiseMode::OIDN : DenoiseMode::None;
            settings.renderLow = false;
            settings.maxRenderFrames = state->phase >= 6 ? 0 : 8;
            // Exercise the real settings commit path, not only RenderParams.
            settings.useTileRendering = state->phase < 6 ? state->phase % 3 != 1 : state->phase == 7;
            applyPreviewSettingsForTesting(settings);
            state->configured = true;
            state->reports = state->presentations = 0;
            std::cout << "Mode phase " << state->phase
                      << " tiled=" << editor->document.settings().useTileRendering << std::endl;
            return;
        }
        if (state->reports < (state->phase >= 6 ? 10 : 3) || state->stats.version != viewport->sceneVersion())
            return;
        if (state->phase == 8 && state->stats.denoisedVersion != state->stats.accumulationVersion)
            return;
        if (!state->presentations)
        {
            finish("New mode completed but never displayed a frame");
            return;
        }
        auto im = viewport->grabFramebuffer().convertToFormat(QImage::Format_RGB32);
        double sum = 0, square = 0;
        for (int y = 0; y < im.height(); ++y)
            for (int x = 0; x < im.width(); ++x)
            {
                double value = qGray(im.pixel(x, y));
                sum += value;
                square += value * value;
            }
        const double count = double(im.width()) * im.height(), mean = count ? sum / count : 0;
        const double variance = count ? square / count - mean * mean : 0;
        im.save(output + QString("/phase-%1.png").arg(state->phase));
        state->results.append(QJsonObject{{"phase", state->phase},
                                          {"mean", mean},
                                          {"variance", variance},
                                          {"spp", state->stats.samples},
                                          {"presentations", state->presentations}});
        if (state->phase < 3)
        {
            if (mean < 20)
            {
                finish("Empty preview is black after changing modes");
                return;
            }
            if (state->phase == 0)
                state->empty = im;
            else if (im != state->empty)
            {
                finish("Empty preview changed pixels with rendering mode");
                return;
            }
        }
        else
        {
            if (variance < 1)
            {
                finish("Model preview is blank after changing modes");
                return;
            }
            if (state->phase == 3)
                state->model = im;
            else if (state->phase < 6)
            {
                // Pixel-global sampling must keep all channels identical across tile layouts and batches.
                if (im != state->model)
                {
                    finish("Tiled and full-frame results differ substantially at equal spp");
                    return;
                }
            }
        }
        if (++state->phase == 9)
        {
            finish({});
            return;
        }
        state->configured = false;
        state->clock.restart();
        if (state->phase == 3)
            beginSceneLoad(mis ? QStringLiteral(RESOURCE_DIR "/scenes/glslpt_test_mis.scene.json")
                               : QStringLiteral(RESOURCE_DIR "/scenes/glslpt_Camera_01_4k_gltf.scene.json"));
    });
    timer->start();
}
