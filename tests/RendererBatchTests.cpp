#include "renderer.h"
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QMutex>
#include <QOffscreenSurface>
#include <iostream>

QMutex param_mutex;
namespace
{
void require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
void finish(Renderer &renderer)
{
    renderer.submitGpuBoundary();
    while (!renderer.waitForGpuBoundary())
    {
    }
}
} // namespace
int main(int argc, char **argv)
{
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);
    const auto args = app.arguments();
    const bool benchmark = args.contains("--benchmark");
    try
    {
        if (benchmark)
            require(args.size() >= 8, "Usage: --benchmark scene output.json width height tile spp");
        Scene::setStartupScenePath(
            benchmark ? args[2] : QStringLiteral(RESOURCE_DIR "/scenes/glslpt_test_mis.scene.json"));
        auto &scene = Scene::getInstance();
        if (args.contains("--closeup"))
        {
            scene.camera.processMouseScroll(1080);
            scene.document.captureCamera(scene.camera);
        }
        auto snapshot = scene.document.settings();
        snapshot.denoise = false;
        snapshot.renderLow = false;
        snapshot.maxBounces = 4;
        const int width = benchmark ? args[4].toInt() : 265, height = benchmark ? args[5].toInt() : 139;
        const int measured = benchmark ? args[7].toInt() : 4;
        snapshot.tileSize = benchmark ? args[6].toInt() : 32;
        snapshot.maxRenderFrames = benchmark ? measured + 2 : 4;
        snapshot.useTileRendering = !args.contains("--full");
        require(width > 0 && height > 0 && measured > 0 && snapshot.tileSize > 0,
                "Invalid benchmark settings");
        RenderParams::instance().applySnapshot(snapshot);
        QOpenGLContext context;
        context.setFormat(format);
        require(context.create(), "Context failed");
        QOffscreenSurface surface;
        surface.setFormat(context.format());
        surface.create();
        require(context.makeCurrent(&surface), "makeCurrent failed");
        Renderer renderer(width, height, snapshot);
        int calls = 0, maxBatch = 0;
        auto step = [&](SceneDirtyFlags dirty = 0) {
            renderer.render(width, height, snapshot, dirty, args.contains("--single") ? 1 : 16);
            finish(renderer);
            ++calls;
            maxBatch = std::max(maxBatch, renderer.stats.batchTiles);
            require(renderer.stats.batchTiles <= 16, "Batch exceeded its tile cap");
        };
        step(kInitialSceneDirty);
        while (renderer.samples() < 2)
            step();
        if (benchmark)
        {
            calls = maxBatch = 0;
            const quint64 composites = renderer.stats.compositeCount;
            QElapsedTimer clock;
            clock.start();
            while (renderer.samples() < measured + 2)
                step();
            const double seconds = clock.nsecsElapsed() / 1e9;
            QJsonObject result{{"width", width},
                               {"height", height},
                               {"tileSize", snapshot.tileSize},
                               {"spp", measured},
                               {"bounces", 4},
                               {"denoise", false},
                               {"seconds", seconds},
                               {"fps", measured / seconds},
                               {"batches", calls},
                               {"maximumBatch", maxBatch},
                               {"composites", double(renderer.stats.compositeCount - composites)},
                               {"full", !snapshot.useTileRendering},
                               {"closeup", args.contains("--closeup")}};
            auto image = renderer.result(snapshot);
            require(!image.isNull(), "No benchmark result");
            require(image.save(args[3] + ".png"), "Cannot save benchmark image");
            QFile file(args[3]);
            require(file.open(QIODevice::WriteOnly), "Cannot write benchmark result");
            file.write(QJsonDocument(result).toJson());
            std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << std::endl;
            return 0;
        }
        const auto complete = renderer.result(snapshot);
        const int previousTiles = renderer.completedTiles();
        int interruptionChecks = 0;
        renderer.render(width, height, snapshot, 0, 16, [&] {
            ++interruptionChecks;
            return true;
        });
        finish(renderer);
        require(renderer.completedTiles() == previousTiles + 1 && !renderer.completeRound(),
                "Control interrupt did not stop the burst at a tile boundary");
        require(interruptionChecks == 1, "Burst interruption callback was not checked");
        require(renderer.result(snapshot) == complete, "Partial batch contaminated the completed snapshot");
        while (renderer.samples() < 4)
            step();
        require(maxBatch > 1, "Warm renderer never batched tiles");
        const int tilesPerRound = ((width + 31) / 32) * ((height + 31) / 32);
        require(renderer.completedTiles() == 4 * tilesPerRound, "Batching changed sample/tile accounting");
        const auto tiled = renderer.result(snapshot);
        snapshot.useTileRendering = false;
        step();
        while (renderer.samples() < 4)
            step();
        require(renderer.result(snapshot) == tiled, "Full and batched rendering differ at equal spp");
        const auto previousRevision = renderer.imageRevision(),
                   previousEpoch = renderer.stats.accumulationVersion;
        auto display = scene.document.root["display"].toObject();
        display["exposure"] = 1.;
        scene.document.root["display"] = display;
        step(toSceneDirtyFlags(SceneDirtyFlag::Display));
        require(renderer.imageRevision() > previousRevision &&
                    renderer.stats.accumulationVersion == previousEpoch && renderer.samples() == 4 &&
                    renderer.result(snapshot) != tiled,
                "Display settings did not refresh a stopped preview without restarting samples");
        snapshot.useTileRendering = true;
        step(toSceneDirtyFlags(SceneDirtyFlag::Camera));
        require(renderer.stats.batchTiles == 1, "New accumulation reused an old GPU budget");
        renderer.render(width + 7, height + 3, snapshot, 0, 16);
        finish(renderer);
        require(renderer.stats.batchTiles == 1 && renderer.renderSize() == QSize(width + 7, height + 3),
                "Resize reused an old batch budget or dimensions");
        std::cout << "Renderer batches: cap, interruption, complete snapshots, equal pixels and budget "
                     "invalidation passed"
                  << std::endl;
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
