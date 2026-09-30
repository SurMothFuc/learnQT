#include "RenderQueueThread.h"
#include "renderer.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QSet>
#include <stdexcept>

QMap<QString, QString> renderResourceSignatures(const SceneDocument &document)
{
    QSet<QString> paths;
    const auto root = document.root;
    if (!root["hdr"].toString().isEmpty())
        paths.insert(root["hdr"].toString());
    for (auto group : {"models", "textures"})
        for (auto value : root[group].toArray())
        {
            const auto item = value.toObject();
            if (!item["source"].toString().isEmpty())
                paths.insert(item["source"].toString());
            for (auto dependency : item["dependencies"].toObject())
                if (!dependency.toString().isEmpty())
                    paths.insert(dependency.toString());
        }
    QMap<QString, QString> signatures;
    for (const auto &path : paths)
    {
        const QFileInfo info(path);
        QFile file(path);
        if (!info.isFile() || !file.open(QIODevice::ReadOnly))
        {
            signatures[path] = QStringLiteral("missing");
            continue;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        signatures[path] = hash.addData(&file)
                               ? QString::fromLatin1(hash.result().toHex())
                               : QStringLiteral("unreadable");
    }
    return signatures;
}

RenderQueueThread::RenderQueueThread(QOpenGLContext *shared, QObject *parent) : QThread(parent)
{
    surface = new QOffscreenSurface(nullptr, parent);
    surface->setFormat(shared->format());
    surface->create();
    context = new QOpenGLContext;
    context->setFormat(shared->format());
    context->setShareContext(shared);
    context->create();
    context->moveToThread(this);
    qRegisterMetaType<RenderJobState>();
}

RenderQueueThread::~RenderQueueThread()
{
    running = false;
    cancel = true;
    wake.wakeAll();
    wait();
    delete surface;
}

bool RenderQueueThread::submit(RenderQueueRequest request)
{
    QMutexLocker lock(&mutex);
    if (hasPending || !isRunning())
        return false;
    pending = std::move(request);
    hasPending = true;
    paused = false;
    cancel = false;
    wake.wakeAll();
    return true;
}

void RenderQueueThread::pauseCurrent(bool value)
{
    paused = value;
    wake.wakeAll();
}

void RenderQueueThread::stopCurrent()
{
    cancel = true;
    wake.wakeAll();
}

void RenderQueueThread::run()
{
    if (!context->makeCurrent(surface))
    {
        emit workerFailed(QStringLiteral("Cannot create render queue OpenGL context"));
        delete context;
        context = nullptr;
        return;
    }
    while (running)
    {
        RenderQueueRequest job;
        {
            QMutexLocker lock(&mutex);
            while (running && !hasPending)
                wake.wait(&mutex);
            if (!running)
                break;
            job = std::move(pending);
            hasPending = false;
        }
        const bool computePathtrace = RenderParams::instance().computePathtrace();
        QImage result;
        QString error;
        bool rendered = false;
        try
        {
            emit jobState(job.id, RenderJobState::Preparing);
            if (renderResourceSignatures(job.document) != job.resourceSignatures)
                throw std::runtime_error("Queued resource changed or disappeared before rendering");
            auto scene = Scene::prepareDocument(job.document, error, job.assets);
            if (!scene)
                throw std::runtime_error(error.toStdString());
            if (cancel || !running)
                throw std::runtime_error("Render stopped during preparation");
            auto snapshot = scene->document.settings();
            snapshot.computePathtrace = computePathtrace;
            snapshot.renderLow = false;
            snapshot.useTileRendering = true;
            snapshot.tileSize = job.settings.tileSize;
            snapshot.maxRenderFrames = job.settings.samples;
            snapshot.maxBounces = job.settings.bounces;
            snapshot.denoise = job.settings.denoise;
            Renderer renderer(job.settings.size.width(), job.settings.size.height(), snapshot, nullptr, scene.get());
            renderer.cancel = &cancel;
            renderer.formal = true;
            renderer.prepareJob(job.settings.size, snapshot, kInitialSceneDirty);
            QElapsedTimer elapsed, update;
            elapsed.start();
            update.start();
            emit jobState(job.id, RenderJobState::Rendering);
            bool reportedPause = false;
            while (running && !cancel && renderer.samples() < job.settings.samples)
            {
                if (paused)
                {
                    if (!reportedPause)
                        emit jobState(job.id, RenderJobState::Paused);
                    reportedPause = true;
                    msleep(20);
                    continue;
                }
                if (reportedPause)
                    emit jobState(job.id, RenderJobState::Rendering);
                reportedPause = false;
                if (!renderer.waitForGpuBoundary())
                    continue;
                renderer.render(job.settings.size.width(), job.settings.size.height(), snapshot, 0, 16,
                                [this] { return !running || cancel || paused; });
                if (update.elapsed() >= 200)
                {
                    QImage frame;
                    if (renderer.completeRound() && renderer.samples() > 0)
                        frame = renderer.result(snapshot);
                    emit jobProgress(job.id, renderer.samples(), job.settings.samples,
                                     elapsed.elapsed() / 1000., frame);
                    update.restart();
                }
                renderer.submitGpuBoundary();
            }
            if (running && !cancel && renderer.samples() >= job.settings.samples)
            {
                emit jobState(job.id, RenderJobState::Denoising);
                while (running && !cancel && !renderer.waitForGpuBoundary())
                    QThread::msleep(1);
                if (cancel || !running)
                    throw std::runtime_error("Render stopped before final denoising");
                renderer.finishDenoise(snapshot);
                result = renderer.result(snapshot);
                rendered = !result.isNull();
                if (rendered)
                {
                    QImageWriter writer(job.outputPath);
                    writer.setQuality(95);
                    if (!writer.write(result.convertToFormat(QImage::Format_RGB32)))
                        error = writer.errorString();
                }
            }
            else if (renderer.samples() > 0)
                result = renderer.result(snapshot);
        }
        catch (const std::exception &e)
        {
            error = QString::fromUtf8(e.what());
        }
        emit jobFinished(job.id, rendered, cancel || !running, result, job.outputPath, error);
    }
    context->doneCurrent();
    delete context;
    context = nullptr;
}
