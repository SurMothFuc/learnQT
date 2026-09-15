#include "renderer.h"
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QMutex>
#include <QOffscreenSurface>
#include <QThread>
#include <QDir>
#include <cmath>
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
    const bool environmentRegression = args.contains("--environment-regression");
    try
    {
        if (benchmark)
            require(args.size() >= 8, "Usage: --benchmark scene output.json width height tile spp");
        Scene::setStartupScenePath(
            benchmark ? args[2] : QStringLiteral(RESOURCE_DIR) +
                (environmentRegression ? "/scenes/glslpt_teapot.scene.json" : "/scenes/glslpt_test_mis.scene.json"));
        auto &scene = Scene::getInstance();
        if (environmentRegression)
        {
            auto document = scene.document;
            document.root["lights"] = QJsonArray{QJsonObject{{"id", "sphere"}, {"type", "sphere"},
                {"position", QJsonArray{0, 9.5, 0}}, {"radius", 2.7}, {"radiance", QJsonArray{10, 10, 10}}}};
            scene.applyEditorDocument(document);
        }
        if (args.contains("--closeup"))
        {
            scene.camera.processMouseScroll(1080);
            scene.document.captureCamera(scene.camera);
        }
        auto snapshot = scene.document.settings();
        snapshot.denoise = false;
        snapshot.renderLow = false;
        snapshot.maxBounces = 4;
        const int width = benchmark ? args[4].toInt() : (environmentRegression ? 905 : 265),
                  height = benchmark ? args[5].toInt() : (environmentRegression ? 666 : 139);
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
        if (environmentRegression)
        {
            const QString output = args.value(args.indexOf("--environment-regression") + 1);
            require(!output.isEmpty() && QDir().mkpath(output), "Missing environment regression output");
            const auto view = scene.camera.getViewMatrix().inverted();
            const QVector3D oc = scene.camera.position - QVector3D(0, 9.5f, 0);
            auto checkSphere = [&](const char *name) {
                while (renderer.samples() < 4) step();
                const auto image = renderer.result(snapshot);
                require(image.save(output + "/" + name + ".png"), "Save sphere image");
                int tested = 0, black = 0;
                for (int y = 0; y < height / 3; ++y)
                    for (int x = 0; x < width; ++x)
                    {
                        const auto direction = (view * QVector4D(
                            (2.f * (x + .5f) / width - 1.f) * width / height,
                            1.f - 2.f * (y + .5f) / height,
                            -1.f / std::tan(scene.camera.zoom * 3.141592653589793 / 360.), 0)).toVector3D().normalized();
                        const float b = QVector3D::dotProduct(oc, direction);
                        const float disc = 2.7f * 2.7f - QVector3D::crossProduct(oc, direction).lengthSquared();
                        // Exclude only the numerically ambiguous silhouette; no geometry
                        // lies in front of this upper part of the fixture's sphere.
                        if (b >= 0 || disc < .001f) continue;
                        ++tested;
                        const auto pixel = image.pixelColor(x, y);
                        if (pixel.red() < 250 || pixel.green() < 250 || pixel.blue() < 250) ++black;
                    }
                std::cout << name << ": sphere pixels=" << tested << " incorrect=" << black << std::endl;
                require(tested > 10000 && black == 0, "Camera-visible sphere contains dark pixels");
            };
            checkSphere("sphere-env-on");
            snapshot.useEnvironmentMap = false; step(); checkSphere("sphere-env-off");

            // An empty scene tests every background pixel, including pixels for which
            // no mesh fragment runs. Compare both renderers through the same display pass.
            auto empty = scene.document;
            auto objects = empty.root["objects"].toArray();
            for (int i = 0; i < objects.size(); ++i)
            {
                auto object = objects[i].toObject();
                object["visible"] = false;
                objects[i] = object;
            }
            empty.root["objects"] = objects;
            empty.root["lights"] = QJsonArray{};
            scene.applyEditorDocument(empty);
            auto gl = context.versionFunctions<QOpenGLFunctions_3_3_Core>();
            require(gl && gl->initializeOpenGLFunctions(), "OpenGL functions unavailable");
            QImage first;
            for (int test = 0; test < 4; ++test)
            {
                snapshot.useEnvironmentMap = test != 3;
                scene.document.root["environment"] = QJsonObject{
                    {"intensity", test == 2 ? .25 : 1.}, {"rotation", test == 1 ? 90. : 0.}};
                renderer.setRasterActive(false);
                step(kInitialSceneDirty);
                while (renderer.samples() < 1) step();
                const auto reference = renderer.result(snapshot).convertToFormat(QImage::Format_RGBA8888);
                renderer.setRasterActive(true); step();
                require(renderer.rasterActive(), "Raster background shader failed");
                QThread::msleep(20); step();
                finish(renderer);
                gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, renderer.displayFramebuffer());
                QImage raster(width, height, QImage::Format_RGBA8888);
                gl->glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, raster.bits());
                raster = raster.mirrored();
                require(raster.save(output + QString("/background-%1.png").arg(test)), "Save background");
                int different = 0;
                int maxDifference = 0;
                if (test < 3)
                {
                    for (int i = 0; i < width * height * 4; ++i)
                    {
                        const int delta = std::abs(int(raster.constBits()[i]) - int(reference.constBits()[i]));
                        different += delta > 1;
                        maxDifference = std::max(maxDifference, delta);
                    }
                    std::cout << "background " << test << ": channels differing by >1=" << different
                              << " max=" << maxDifference << std::endl;
                    // Separate shader programs may round a ray/UV differently at a
                    // high-contrast HDR texel; allow a few 8-bit quantization steps.
                    require(different <= width * height / 10000 && maxDifference <= 8,
                            "Raster HDR does not match path traced background");
                    if (test == 0) first = raster;
                    else require(raster != first, "Environment rotation/intensity had no effect");
                }
                else
                    require(qGray(raster.pixel(0, 0)) > 10 && raster.pixel(0, 0) == raster.pixel(width/2, height/2),
                            "Disabled environment must have a uniform gray raster background");
                require(gl->glGetError() == GL_NO_ERROR, "Environment regression GL error");
            }
            std::cout << "Environment and sphere regression passed" << std::endl;
            return 0;
        }
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
        // Exercise the actual raster -> composite target, then return to the exact PT snapshot.
        auto gl = context.versionFunctions<QOpenGLFunctions_3_3_Core>();
        require(gl && gl->initializeOpenGLFunctions(), "OpenGL functions unavailable");
        auto rasterImage = [&] {
            QThread::msleep(20);
            step();
            gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, renderer.displayFramebuffer());
            require(gl->glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
                    "Raster display framebuffer incomplete");
            QImage image(width, height, QImage::Format_RGBA8888);
            gl->glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
            require(gl->glGetError() == GL_NO_ERROR, "Raster preview left an OpenGL error");
            return image;
        };
        renderer.setRasterActive(true);
        step();
        require(renderer.rasterActive() && renderer.samples() == 0,
                "Raster preview accumulated path tracing samples");
        const auto raster = rasterImage();
        int litPixels = 0;
        for (int y = 0; y < raster.height(); ++y)
            for (int x = 0; x < raster.width(); ++x)
                litPixels += qGray(raster.pixel(x, y)) > 5;
        require(litPixels > 100, "Raster composite is black");
        const auto instances = scene.instances;
        std::reverse(scene.instances.begin(), scene.instances.end());
        step(toSceneDirtyFlags(SceneDirtyFlag::Material));
        require(rasterImage() == raster, "Raster rendering depends on instance document order");
        scene.instances = instances;
        renderer.setRasterActive(false);
        snapshot.useTileRendering = true;
        step(toSceneDirtyFlags(SceneDirtyFlag::Material));
        while (renderer.samples() < 4)
            step();
        const auto resumed = renderer.result(snapshot);
        snapshot.useTileRendering = false;
        step();
        while (renderer.samples() < 4)
            step();
        require(renderer.result(snapshot) == resumed,
                "Raster preview contaminated subsequent full/tiled path tracing");
        renderer.setRasterActive(true);
        step();
        renderer.prepareJob(QSize(width, height), snapshot, 0);
        while (renderer.samples() < 4)
            step();
        require(!renderer.rasterActive() && renderer.result(snapshot) == resumed,
                "Formal render inherited raster preview state");
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
