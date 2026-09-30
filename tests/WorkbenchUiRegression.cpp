#include "learnQT.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QImageWriter>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QTimer>
#include <iostream>
void learnQT::configureWorkbenchRegression()
{
    auto args = QCoreApplication::arguments();
    int option = args.indexOf("--workbench-regression");
    if (option < 0 || option + 1 >= args.size())
        return;
    QString output = QFileInfo(args[option + 1]).absoluteFilePath();
    QDir().mkpath(output);
    struct State
    {
        int phase = 0, frames = 0, lastPhase = -1;
        QElapsedTimer timer;
        QString a, b;
        RenderStats stats;
        RenderStats beforeTransform;
        QMatrix4x4 originalTransform;
        int undoIndex = 0;
        bool paused = false;
        int snapshots = 0;
        int beforeCancelledSnapshots = 0;
        std::atomic_bool cancelDenoise{false};
        qint64 retainedResult = 0;
    };
    auto state = std::make_shared<State>();
    state->timer.start();
    connect(viewport, &GLWidget::framePresented, this, [state] { ++state->frames; });
    connect(viewport, &GLWidget::renderThreadReady, this, [this, state] {
        connect(viewport->renderThread(), &RenderThread::statsReady, this,
                [state](RenderStats s) { state->stats = s; });
        connect(viewport->renderThread(), &RenderThread::resultReady, this, [state](QImage image, bool) {
            if (!image.isNull())
                ++state->snapshots;
        });
        auto worker = viewport->renderThread();
        connect(
            worker, &RenderThread::jobStateChanged, worker,
            [state, worker](RenderJobState stage, const QString &) {
                if (stage == RenderJobState::Denoising && state->cancelDenoise.exchange(false))
                    worker->stopJob();
            },
            Qt::DirectConnection);
    });
    auto timer = new QTimer(this);
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, [this, state, timer, output] {
        auto fail = [&](QString message) {
            std::cout << "Workbench failure: " << message.toStdString() << std::endl;
            grab().save(output + "/failure.png");
            timer->stop();
            QCoreApplication::exit(8);
        };
        if (state->lastPhase != state->phase)
        {
            state->lastPhase = state->phase;
            QFile trace(output + "/progress.json");
            if (trace.open(QIODevice::WriteOnly))
                trace.write(QJsonDocument(QJsonObject{{"phase", state->phase},
                                                      {"active", editor->active},
                                                      {"frames", state->frames},
                                                      {"scene", editor->document.root}})
                                .toJson());
            std::cout << "Workbench phase " << state->phase << std::endl;
        }
        if (state->timer.elapsed() > 120000)
        {
            fail("Timed out at phase " + QString::number(state->phase));
            return;
        }
        if (m_loading || !viewport->renderThread())
            return;
        auto click = [&](QPoint position, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
            QMouseEvent down(QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton, modifiers),
                up(QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton, modifiers);
            QApplication::sendEvent(viewport, &down);
            QApplication::sendEvent(viewport, &up);
        };
        if (state->phase == 0)
        {
            resize(1366, 768);
            QFile obj(output + "/quad.obj");
            obj.open(QIODevice::WriteOnly);
            obj.write("v -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 "
                      "3/3\nf 1/1 3/3 4/4\n");
            obj.close();
            auto d = SceneDocument::empty();
            d.root["name"] = "拾取与出图验收";
            Camera c;
            c.restoreState(QVector3D(0, 0, 4), QVector3D(0, 0, 0), QVector3D(0, 1, 0), 45);
            d.captureCamera(c);
            auto settings = d.settings();
            settings.useTileRendering = false;
            settings.denoise = false;
            settings.maxRenderFrames = 8;
            d.captureSettings(settings);
            Material material;
            material.baseColor = QVector3D(.5, .3, .1);
            material.emissive = QVector3D(2, 1, .2);
            auto m = SceneDocument::materialJson(material);
            m["id"] = "surface";
            d.root["materials"] = QJsonArray{m};
            QMatrix4x4 rear;
            rear.translate(0, 0, -1);
            d.root["models"] = QJsonArray{QJsonObject{{"id", "front"},
                                                      {"source", output + "/quad.obj"},
                                                      {"material", "surface"},
                                                      {"normalize", false},
                                                      {"transform", sceneMatrixJson(QMatrix4x4())}},
                                          QJsonObject{{"id", "rear"},
                                                      {"source", output + "/quad.obj"},
                                                      {"material", "surface"},
                                                      {"normalize", false},
                                                      {"transform", sceneMatrixJson(rear)}}};
            QString error;
            if (!d.saveScene(output + "/fixture.scene.json", error))
            {
                fail(error);
                return;
            }
            beginSceneLoad(output + "/fixture.scene.json");
            state->phase = 1;
            state->frames = 0;
            return;
        }
        if (state->frames < 1)
            return;
        if (state->phase == 1)
        {
            auto objects = editor->document.root["objects"].toArray();
            if (objects.size() != 2)
            {
                fail("Expected two instances");
                return;
            }
            state->a = objects[0].toObject()["id"].toString();
            state->b = objects[1].toObject()["id"].toString();
            QFile metrics(output + "/layout.json");
            metrics.open(QIODevice::WriteOnly);
            metrics.write(QJsonDocument(QJsonObject{{"dpr", viewport->devicePixelRatioF()},
                                                    {"windowWidth", width()},
                                                    {"windowHeight", height()},
                                                    {"viewportWidth", viewport->width()},
                                                    {"viewportHeight", viewport->height()}})
                              .toJson());
            click(viewport->rect().center());
            state->phase = 2;
            return;
        }
        if (state->phase == 2)
        {
            if (editor->active != state->a)
                return;
            grab().save(output + "/workbench-1366.png");
            click(viewport->rect().center(), Qt::ControlModifier);
            state->phase = 3;
            return;
        }
        if (state->phase == 3)
        {
            if (!editor->selection.isEmpty())
                return;
            editor->select({state->a});
            editor->setMaterialField("alphaMode", int(AlphaMode::Mask));
            editor->setMaterialField("opacity", 0.0);
            state->frames = 0;
            state->phase = 4;
            return;
        }
        if (state->phase == 4)
        {
            click(viewport->rect().center());
            state->phase = 5;
            return;
        }
        if (state->phase == 5)
        {
            if (editor->active != state->b)
                return;
            editor->select({state->a});
            editor->setMaterialField("alphaMode", int(AlphaMode::Blend));
            editor->setMaterialField("opacity", .49);
            state->frames = 0;
            state->phase = 6;
            return;
        }
        if (state->phase == 6)
        {
            click(viewport->rect().center());
            state->phase = 7;
            return;
        }
        if (state->phase == 7)
        {
            if (editor->active != state->b)
                return;
            editor->select({state->a});
            editor->setMaterialField("alphaMode", int(AlphaMode::Opaque));
            editor->setMaterialField("transmission", 1.0);
            state->frames = 0;
            state->phase = 8;
            return;
        }
        if (state->phase == 8)
        {
            click(viewport->rect().center());
            state->phase = 9;
            return;
        }
        if (state->phase == 9)
        {
            if (editor->active != state->a)
                return;
            tree->selectionModel()->clearSelection();
            for (auto id : {state->a, state->b})
                tree->selectionModel()->select(treeFilter->mapFromSource(treeModel->find(id)),
                                               QItemSelectionModel::Select | QItemSelectionModel::Rows);
            if (editor->selectedModels().size() != 2)
            {
                fail("Tree multi-selection did not reach viewport");
                return;
            }
            viewport->setTool(GLWidget::Translate);
            resize(1920, 1080);
            state->phase = 20;
            state->frames = 0;
            return;
        }
        if (state->phase == 20)
        {
            if (state->stats.version != viewport->sceneVersion())
                return;
            state->beforeTransform = state->stats;
            state->originalTransform = sceneMatrix(editor->node(state->a)["transform"]);
            state->undoIndex = editor->undo.index();
            QPoint start = viewport->rect().center() + QPoint(55, 0), end = start + QPoint(28, 0);
            QMouseEvent down(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent move(QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent up(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(viewport, &down);
            QApplication::sendEvent(viewport, &move);
            QApplication::sendEvent(viewport, &up);
            if (editor->undo.index() != state->undoIndex + 1 ||
                sceneMatrix(editor->node(state->a)["transform"]) == state->originalTransform)
            {
                fail("Gizmo drag did not produce one undoable transform");
                return;
            }
            state->phase = 21;
            state->frames = 0;
            return;
        }
        if (state->phase == 21)
        {
            if (state->stats.version != viewport->sceneVersion())
                return;
            if (state->stats.geometryUploadBytes != state->beforeTransform.geometryUploadBytes ||
                state->stats.blasBuilds != state->beforeTransform.blasBuilds)
            {
                fail("Transform uploaded geometry or rebuilt BLAS");
                return;
            }
            QFile metrics(output + "/transform-performance.json");
            metrics.open(QIODevice::WriteOnly);
            metrics.write(
                QJsonDocument(
                    QJsonObject{
                        {"geometryUploadBytes", 0},
                        {"blasRebuilds", 0},
                        {"initialGeometryUploadBytes", double(state->beforeTransform.geometryUploadBytes)},
                        {"initialBlasBuilds", state->beforeTransform.blasBuilds},
                        {"initialBlasMs", state->beforeTransform.blasMs},
                        {"gpuBeforeTransformMs", state->beforeTransform.gpuMs},
                        {"targetSpp", state->stats.target},
                        {"tlasMs", state->stats.tlasMs},
                        {"uploadMs", state->stats.uploadMs},
                        {"gpuMs", state->stats.gpuMs},
                        {"resolution",
                         QString("%1x%2").arg(state->stats.size.width()).arg(state->stats.size.height())}})
                    .toJson());
            editor->undo.undo();
            inspector->findChild<MixedSpin *>("transform3")->setValue(15);
            editor->undo.undo();
            inspector->findChild<MixedSpin *>("transform6")->setValue(1.2);
            editor->undo.undo();
            if (sceneMatrix(editor->node(state->a)["transform"]) != state->originalTransform)
            {
                fail("Combined transforms did not undo exactly");
                return;
            }
            viewport->setTool(GLWidget::Rotate);
            const auto pivot = editor->bounds(editor->selectedModels()).center();
            QMap<QString, QMatrix4x4> beforeRotation;
            for (const auto &id : editor->selectedModels())
                beforeRotation[id] = sceneMatrix(editor->node(id)["transform"]);
            // A clockwise quarter-circle drag must rotate clockwise on screen from either side of each axis.
            for (int axisIndex = 0; axisIndex < 3; ++axisIndex)
                for (int side : {-1, 1})
                    for (int clockwise : {-1, 1})
                    {
                        QVector3D axis;
                        axis[axisIndex] = 1;
                        Camera view;
                        view.restoreState(pivot + axis * (4.5f * side), pivot,
                                          axisIndex == 1 ? QVector3D(0, 0, 1) : QVector3D(0, 1, 0), 45);
                        const int beforeCamera = editor->undo.index();
                        ++editor->cameraCommand;
                        editor->setCamera(view);
                        const int beforeDrag = editor->undo.index();
                        const QPoint center = viewport->rect().center();
                        const QPoint start = center + QPoint(57, -57 * clockwise);
                        const QPoint end = center + QPoint(57, 57 * clockwise);
                        QMouseEvent down(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton,
                                         Qt::NoModifier);
                        QMouseEvent move(QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton,
                                         Qt::NoModifier);
                        QMouseEvent up(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton,
                                       Qt::NoModifier);
                        QApplication::sendEvent(viewport, &down);
                        QApplication::sendEvent(viewport, &move);
                        QApplication::sendEvent(viewport, &up);
                        if (editor->undo.index() != beforeDrag + 1)
                        {
                            fail("Rotation drag did not produce one undo command");
                            return;
                        }
                        QMatrix4x4 expected;
                        expected.translate(pivot);
                        expected.rotate(-90.f * clockwise * side, axis);
                        expected.translate(-pivot);
                        for (auto it = beforeRotation.cbegin(); it != beforeRotation.cend(); ++it)
                        {
                            const auto actual = sceneMatrix(editor->node(it.key())["transform"]);
                            const auto target = expected * it.value();
                            for (int row = 0; row < 4; ++row)
                                for (int column = 0; column < 4; ++column)
                                    if (qAbs(actual(row, column) - target(row, column)) > .03f)
                                    {
                                        fail(QString("Rotation opposed drag: axis %1, side %2, clockwise %3")
                                                 .arg(axisIndex)
                                                 .arg(side)
                                                 .arg(clockwise));
                                        return;
                                    }
                        }
                        editor->undo.undo();
                        for (auto it = beforeRotation.cbegin(); it != beforeRotation.cend(); ++it)
                            if (sceneMatrix(editor->node(it.key())["transform"]) != it.value())
                            {
                                fail("Rotation did not undo exactly");
                                return;
                            }
                        while (editor->undo.index() > beforeCamera)
                            editor->undo.undo();
                    }
            std::cout << "Rotation drag direction and undo passed for all axes and viewing sides"
                      << std::endl;
            editor->select({});
            viewport->setTool(GLWidget::Select);
            click(viewport->rect().center());
            editor->rename("root", "过期拾取验收");
            state->phase = 22;
            state->frames = 0;
            return;
        }
        if (state->phase == 22)
        {
            if (!editor->selection.isEmpty())
            {
                fail("Stale GPU pick selected an object");
                return;
            }
            editor->select({state->a, state->b}, state->a);
            viewport->setTool(GLWidget::Translate);
            state->phase = 10;
            return;
        }
        if (state->phase == 10)
        {
            const auto overlayImage = viewport->grab().toImage().convertToFormat(QImage::Format_RGB32);
            int redAxis = 0, greenAxis = 0;
            const int radius = qRound(120 * viewport->devicePixelRatioF());
            const QPoint center = overlayImage.rect().center();
            for (int y = std::max(0, center.y() - radius);
                 y < std::min(overlayImage.height(), center.y() + radius); ++y)
                for (int x = std::max(0, center.x() - radius);
                     x < std::min(overlayImage.width(), center.x() + radius); ++x)
                {
                    const auto pixel = overlayImage.pixel(x, y);
                    redAxis += qRed(pixel) > 170 && qGreen(pixel) < 140 && qBlue(pixel) < 150;
                    greenAxis += qGreen(pixel) > 160 && qRed(pixel) < 150 && qBlue(pixel) < 180;
                }
            if (redAxis < 10 || greenAxis < 10)
            {
                fail("Gizmo axes are not visibly drawn");
                return;
            }
            grab().save(output + "/workbench-1920.png");
            m_restoring = true;
            outputWidth->setValue(192);
            outputHeight->setValue(128);
            outputSamples->setValue(3);
            outputTile->setValue(32);
            outputDenoise->setChecked(true);
            m_restoring = false;
            startRender();
            viewport->renderThread()->pauseJob(true);
            state->phase = 11;
            return;
        }
        if (state->phase == 11)
        {
            if (jobState != RenderJobState::Paused)
                return;
            // Result metadata stays readable while all document mutation controls are locked.
            if (!editor->renderLocked || outputWidth->isEnabled() ||
                inspector->findChild<MixedSpin *>("roughness")->isEnabled())
            {
                fail("Paused job unlocked scene");
                return;
            }
            resize(1366, 768);
            viewport->renderThread()->pauseJob(false);
            state->phase = 12;
            return;
        }
        if (state->phase == 12)
        {
            if (jobState == RenderJobState::Failed)
            {
                fail(taskLabel->text());
                return;
            }
            if (jobState != RenderJobState::Completed)
                return;
            if (state->stats.denoisedVersion == 0)
                return;
            if (state->stats.denoisedVersion != state->stats.accumulationVersion ||
                state->stats.auxiliarySize != QSize(192, 128) || state->stats.normalMinimum < -1.0001 ||
                state->stats.normalMaximum > 1.0001)
            {
                fail("OIDN auxiliary version, resolution or normal range mismatch");
                return;
            }
            if (lastResult.size() != QSize(192, 128) || editor->renderLocked)
            {
                fail("Output resolution or editor restoration failed");
                return;
            }
            resultView->actualSize();
            if (std::abs(resultView->transform().m11() * resultView->devicePixelRatioF() - 1) > 1e-5)
            {
                fail("1:1 result view is not one physical pixel per image pixel");
                return;
            }
            resultView->fit();
            double sum = 0, square = 0;
            for (int y = 0; y < lastResult.height(); ++y)
                for (int x = 0; x < lastResult.width(); ++x)
                {
                    auto pixel = lastResult.pixel(x, y);
                    if (qAlpha(pixel) != 255)
                    {
                        fail("Output is not opaque");
                        return;
                    }
                    double l = qGray(pixel);
                    sum += l;
                    square += l * l;
                }
            double count = lastResult.width() * lastResult.height(), mean = sum / count;
            if (mean < 2 || mean > 253 || square / count - mean * mean < 1)
            {
                fail("Output is black, white or constant");
                return;
            }
            QImageWriter png(output + "/render.png"), jpeg(output + "/render.jpg");
            jpeg.setQuality(95);
            if (!png.write(lastResult) || !jpeg.write(lastResult) ||
                QImage(output + "/render.jpg").size() != lastResult.size())
            {
                fail("PNG/JPEG roundtrip failed");
                return;
            }
            grab().save(output + "/result.png");
            m_restoring = true;
            outputSamples->setValue(100000);
            outputDenoise->setChecked(false);
            m_restoring = false;
            startRender();
            state->phase = 13;
            return;
        }
        if (state->phase == 13)
        {
            if (jobState != RenderJobState::Rendering)
                return;
            viewport->renderThread()->stopJob();
            state->phase = 14;
            return;
        }
        if (state->phase == 14)
        {
            if (jobState != RenderJobState::Stopped)
                return;
            if (lastResult.size() != QSize(192, 128) || editor->renderLocked)
            {
                fail("Stop lost result or did not unlock");
                return;
            }
            RenderJobSettings invalid;
            invalid.size = {0, 0};
            viewport->renderThread()->startJob(invalid);
            state->phase = 15;
            return;
        }
        if (state->phase == 15)
        {
            if (jobState != RenderJobState::Failed)
                return;
            if (editor->renderLocked || lastResult.isNull())
            {
                fail("Failed job lost result or kept editing locked");
                return;
            }
            m_restoring = true;
            outputSamples->setValue(1);
            outputWidth->setValue(96);
            outputHeight->setValue(64);
            outputDenoise->setChecked(true);
            m_restoring = false;
            startRender();
            state->phase = 16;
            return;
        }
        if (state->phase == 16)
        {
            if (jobState != RenderJobState::Completed || state->stats.auxiliarySize != QSize(96, 64))
                return;
            if (lastResult.size() != QSize(96, 64) ||
                state->stats.denoisedVersion != state->stats.accumulationVersion)
            {
                fail("Restart after failure reused stale output or auxiliaries");
                return;
            }
            state->beforeCancelledSnapshots = state->snapshots;
            state->retainedResult = lastResult.cacheKey();
            state->cancelDenoise = true;
            startRender();
            state->phase = 17;
            return;
        }
        if (state->phase == 17)
        {
            if (jobState != RenderJobState::Stopped)
                return;
            if (editor->renderLocked || viewport->renderThread()->jobActive() ||
                state->snapshots <= state->beforeCancelledSnapshots ||
                lastResult.cacheKey() != state->retainedResult)
            {
                fail("OIDN cancellation did not preserve the completed round and previous result");
                return;
            }
            views->setCurrentIndex(0);
            viewport->setTool(GLWidget::Select);
            editor->select({});
            state->phase = 18;
            state->frames = 0;
            return;
        }
        if (state->phase == 18)
        {
            click(viewport->rect().center());
            state->phase = 19;
            return;
        }
        if (state->phase == 19)
        {
            if (editor->active != state->a)
                return;
            std::cout << "Workbench GPU pick, Ctrl/tree selection, mask/blend/glass, gizmo/undo, stale pick, "
                         "zero geometry uploads, independent resolution, pause/resume, OIDN, PNG/JPEG, stop "
                         "OIDN cancellation and failure recovery passed."
                      << std::endl;
            timer->stop();
            QCoreApplication::exit(0);
        }
    });
    timer->start();
}
