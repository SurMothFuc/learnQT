#include "WorkspaceUi.h"
#include "learnQT.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QSettings>
#include <QToolButton>
#include <QTimer>
#include <iostream>

void learnQT::configureRenderQueueRegression()
{
    const auto args = QCoreApplication::arguments();
    const int option = args.indexOf("--render-queue-regression");
    if (option < 0 || option + 1 >= args.size())
        return;
    const QString output = QFileInfo(args[option + 1]).absoluteFilePath();
    QDir().mkpath(output);
    struct State
    {
        int phase = 0;
        quint64 firstId = 0;
        int firstSamples = 0;
        bool visitedScene = false;
        bool visitedSettings = false;
        bool thirdEdited = false;
        bool cameraListSaved = false;
        bool draftSaved = false;
        bool capturedUi = false;
        QString collisionPath;
        QElapsedTimer clock;
        QElapsedTimer previewWarmup;
        QElapsedTimer pauseCheck;
        int pausedSamples = 0;
        bool blockedExport = false;
        QMetaObject::Connection exportPause;
    };
    auto state = std::make_shared<State>();
    state->clock.start();
    auto timer = new QTimer(this);
    timer->setInterval(100);
    auto finish = [this, timer, state, output](const QString &error) {
        timer->stop();
        QObject::disconnect(state->exportPause);
        if (!error.isEmpty())
            grab().save(output + "/failure.png");
        QJsonArray tasks;
        for (const auto &item : m_renderQueue)
            tasks.append(QJsonObject{{"id", qint64(item.request.id)}, {"status", item.status},
                                     {"samples", item.samples}, {"output", item.request.outputPath},
                                     {"error", item.error}});
        QFile report(output + "/report.json");
        if (report.open(QIODevice::WriteOnly))
            report.write(QJsonDocument(QJsonObject{{"passed", error.isEmpty()}, {"error", error},
                                               {"phase", state->phase}, {"tasks", tasks}}).toJson());
        editor->markSaved();
        QCoreApplication::exit(error.isEmpty() ? 0 : 14);
    };
    connect(timer, &QTimer::timeout, this, [this, state, output, finish] {
        if (state->clock.elapsed() > 180000)
            return finish("Render queue timed out");
        if (state->phase == 0)
        {
            navigateWorkspace(WorkspacePage::Scene);
            beginSceneLoad(QString::fromStdString(getResourcePath("scenes/lantern.scene.json")));
            state->phase = 1;
            return;
        }
        if (state->phase == 1)
        {
            if (m_loading || editor->busy || editor->document.root["objects"].toArray().isEmpty() ||
                !viewport->renderThread())
                return;
            if (!state->cameraListSaved)
            {
                navigateWorkspace(WorkspacePage::Camera);
                auto addCamera = findChild<QPushButton *>("savedCameraAdd");
                auto cameraList = findChild<QListWidget *>("savedCameraList");
                auto clearCamera = findChild<QToolButton *>("savedCameraClearSelection");
                if (!addCamera || !addCamera->isVisible() || !cameraList || !clearCamera)
                    return finish("Camera page save button unavailable");
                if (cameraList->currentRow() != -1 || clearCamera->isEnabled())
                    return finish("Camera list did not start unselected");
                addCamera->click();
                if (editor->document.root["cameras"].toArray().size() != 2 ||
                    cameraList->currentRow() != 1)
                    return finish("Camera page did not save a second view");
                const auto activeCamera = editor->document.root["activeCameraId"];
                clearCamera->click();
                refreshWorkspace();
                if (cameraList->currentRow() != -1 ||
                    editor->document.root["activeCameraId"] != activeCamera ||
                    findChild<QPushButton *>("savedCameraRename")->isEnabled() ||
                    findChild<QPushButton *>("savedCameraDelete")->isEnabled())
                    return finish("Clearing camera selection changed the editing camera");
                state->cameraListSaved = true;
                return;
            }
            QSettings preferences(QSettings::defaultFormat(), QSettings::UserScope, "learnQT", "SceneWorkbench");
            preferences.setValue("workspaceV4/autoExportPath", output + "/missing-directory");
            preferences.sync();
            QTimer::singleShot(0, this, [] {
                if (auto message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                    message->accept();
            });
            runRenderQueue();
            if (m_queueRunning || workspace->page != int(WorkspacePage::Settings))
                return finish("Invalid export directory was accepted");
            preferences.setValue("workspaceV4/autoExportPath", output);
            preferences.sync();
            outputWidth->setValue(96);
            outputHeight->setValue(54);
            // Keep the first job alive long enough for progress and cross-page
            // actions; 512 samples can finish before the 100 ms UI timer fires.
            outputSamples->setValue(8192);
            outputDenoise->setCurrentIndex(int(DenoiseMode::None));
            navigateWorkspace(WorkspacePage::Render);
            m_renderCameraChoice->setCurrentIndex(1);
            auto add = findChild<QToolButton *>("renderPrimary");
            if (!add || !add->isVisible())
                return finish("Queue add button unavailable");
            add->click();
            if (m_renderQueue.size() != 1)
                return finish("First queue task was not captured");
            outputSamples->setValue(8);
            state->firstId = m_renderQueue.first().request.id;
            auto original = m_renderQueue.first().request.document.root;
            auto next = editor->document;
            auto outputSettings = next.root["output"].toObject();
            outputSettings["width"] = outputWidth->value();
            outputSettings["height"] = outputHeight->value();
            outputSettings["samples"] = outputSamples->value();
            outputSettings["denoise"] = outputDenoise->currentIndex() != 0;
            outputSettings["denoiseMode"] = denoiseModeName(DenoiseMode(outputDenoise->currentIndex()));
            next.root["output"] = outputSettings;
            auto camera = next.root["camera"].toObject();
            camera["position"] = QJsonArray{2.5, 1.2, 5.0};
            auto cameras = next.root["cameras"].toArray();
            camera["id"] = "queue-camera-2";
            camera["name"] = "测试侧视角";
            cameras.append(camera);
            next.root["cameras"] = cameras;
            auto materials = next.root["materials"].toArray();
            auto material = materials.first().toObject();
            material["baseColor"] = QJsonArray{0.1, 0.8, 0.2};
            materials[0] = material;
            next.root["materials"] = materials;
            editor->submit(next, "Queue variant", EditorController::Organization);
            refreshRenderCameras();
            m_renderCameraChoice->setCurrentIndex(2);
            if (m_draftCamera["id"].toString() != "queue-camera-2")
                return finish("Saved camera selection did not update composition copy");
            add->click();
            if (m_renderQueue.size() != 2 || m_renderQueue.first().request.document.root != original ||
                m_renderQueue[1].request.document.root["materials"] == original["materials"])
                return finish("Queued scene snapshot changed after later edit");
            state->collisionPath = output + "/" + m_renderQueue.first().name + ".png";
            QFile collision(state->collisionPath);
            if (!collision.open(QIODevice::WriteOnly) || collision.write("collision sentinel") < 0)
                return finish("Could not create filename collision fixture");
            collision.close();
            workspace->runQueue->trigger();
            if (!m_queueRunning)
                return finish("Run queue did not start");
            state->phase = 2;
            return;
        }
        if (state->phase == 2)
        {
            if (m_activeQueueId && m_activeQueueId != state->firstId &&
                m_renderQueue[0].samples < 1)
                return finish("First task never reported formal samples");
            if (m_activeQueueId == state->firstId)
            {
                if (m_renderQueue[0].samples < state->firstSamples)
                    return finish("Formal spp decreased after page change");
                state->firstSamples = m_renderQueue[0].samples;
            }
            if (!state->visitedScene)
            {
                if (m_activeQueueId != state->firstId || state->firstSamples < 1)
                    return;
                navigateWorkspace(WorkspacePage::Scene);
                state->visitedScene = true;
                return;
            }
            if (!state->visitedSettings)
            {
                navigateWorkspace(WorkspacePage::Settings);
                state->visitedSettings = true;
                return;
            }
            navigateWorkspace(WorkspacePage::Render);
            if (!state->thirdEdited && !editor->renderLocked && m_activeQueueId)
            {
                auto next = editor->document;
                auto objects = next.root["objects"].toArray();
                if (!objects.isEmpty())
                {
                    auto object = objects.first().toObject();
                    object["visible"] = false;
                    objects[0] = object;
                    next.root["objects"] = objects;
                    editor->submit(next, "Queue third variant", EditorController::Topology);
                    state->thirdEdited = true;
                }
            }
            if (!state->thirdEdited || editor->busy)
                return;
            if (!m_renderPreviewMode) workspace->compositionMode->click();
            findChild<QToolButton *>("renderPrimary")->click();
            if (m_renderQueue.size() != 3)
                return finish(QString("Could not append after live edit: busy=%1 locked=%2 active=%3 draft=%4")
                                  .arg(editor->busy).arg(editor->renderLocked).arg(m_activeQueueId)
                                  .arg(!m_draftCamera.isEmpty()));
            state->phase = 3;
            return;
        }
        if (state->phase == 3)
        {
            if (m_activeQueueId || m_queueRunning || editor->busy)
                return;
            if (m_renderQueue.size() != 3)
                return finish("Queued task count changed");
            for (const auto &item : m_renderQueue)
                if (item.status != tr("完成") || item.result.size() != QSize(96, 54) ||
                    !QFileInfo::exists(item.request.outputPath))
                    return finish("Queued result or automatic export missing");
            QFile collision(state->collisionPath);
            if (m_renderQueue.first().request.outputPath == state->collisionPath ||
                !collision.open(QIODevice::ReadOnly) || collision.readAll() != "collision sentinel")
                return finish("Automatic export overwrote an existing file");
            if (m_renderQueue[0].result == m_renderQueue[1].result ||
                m_renderQueue[1].result == m_renderQueue[2].result)
                return finish("Scene variants produced identical images");
            if (m_renderQueue[0].request.document.root["camera"] ==
                    m_renderQueue[1].request.document.root["camera"] ||
                m_renderQueue[1].request.document.root["objects"] ==
                    m_renderQueue[2].request.document.root["objects"])
                return finish("Camera or object scene variant was not frozen");
            resize(1366, 768);
            navigateWorkspace(WorkspacePage::Render);
            setRenderPreviewMode(true);
            if (workspace->page != int(WorkspacePage::Render) || views->currentIndex() != 0)
                return finish("Composition switch left the render page");
            state->previewWarmup.start();
            state->phase = 4;
            return;
        }
        if (state->phase == 4)
        {
            if (state->previewWarmup.elapsed() < 1200)
                return;
            if (!state->capturedUi)
            {
                grab().save(output + "/render-page.png");
                viewport->grabFramebuffer().save(output + "/composition.png");
                workspace->task->selectRow(0);
                showRenderTaskResult();
                if (resultView->image.isNull() || workspace->page != int(WorkspacePage::Render) ||
                    views->currentIndex() != 1)
                    return finish("Session result browsing failed");
                if (!workspace->resultProperties->isVisible() || workspace->outputProperties->isVisible() ||
                    !workspace->taskProperties->text().contains(m_renderQueue[0].cameraName) ||
                    !workspace->taskProperties->text().contains(m_renderQueue[0].request.outputPath) ||
                    !statsLabel->text().contains("8192 / 8192 spp"))
                    return finish("Result inspector did not show selected task snapshot");
                grab().save(output + "/result-page.png");
                state->capturedUi = true;
            }
            if (!state->draftSaved)
            {
                const auto before = editor->document.root["cameras"].toArray();
                m_draftCamera["position"] = QJsonArray{3.0, 1.2, 5.0};
                workspace->compositionMode->click();
                findChild<QPushButton *>("saveCompositionCamera")->click();
                const auto after = editor->document.root["cameras"].toArray();
                if (after.size() != before.size() + 1 || after[2] != before[2] ||
                    m_draftSourceId != after.last().toObject()["id"].toString())
                    return finish("Saving composition changed the source camera");
                state->draftSaved = true;
                return;
            }
            if (editor->busy)
                return;
            outputSamples->setValue(1000000);
            if (!m_renderPreviewMode) workspace->compositionMode->click();
            findChild<QToolButton *>("renderPrimary")->click();
            outputSamples->setValue(2);
            if (!m_renderPreviewMode) workspace->compositionMode->click();
            findChild<QToolButton *>("renderPrimary")->click();
            if (m_renderQueue.size() != 5)
                return finish("Pause/stop queue fixtures were not captured");
            workspace->runQueue->trigger();
            state->phase = 5;
            return;
        }
        if (state->phase == 5)
        {
            if (m_renderQueue[3].status != tr("渲染中"))
                return;
            pauseAction->trigger();
            state->phase = 6;
            return;
        }
        if (state->phase == 6)
        {
            if (m_renderQueue[3].status != tr("已暂停"))
                return;
            if (!state->pauseCheck.isValid())
            {
                state->pausedSamples = m_renderQueue[3].samples;
                state->pauseCheck.start();
                return;
            }
            if (state->pauseCheck.elapsed() < 400)
                return;
            if (m_renderQueue[3].samples != state->pausedSamples)
                return finish("Paused task continued sampling");
            pauseAction->trigger();
            state->phase = 7;
            return;
        }
        if (state->phase == 7)
        {
            if (m_renderQueue[3].status != tr("渲染中"))
                return;
            stopAction->trigger();
            state->phase = 8;
            return;
        }
        if (state->phase == 8)
        {
            if (m_activeQueueId || m_queueRunning)
                return;
            if (m_renderQueue[3].status != tr("已停止") ||
                m_renderQueue[4].status != tr("完成"))
                return finish("Stopping current task did not advance to the next task");
            outputSamples->setValue(1000000);
            if (!m_renderPreviewMode) workspace->compositionMode->click();
            findChild<QToolButton *>("renderPrimary")->click();
            outputSamples->setValue(2);
            if (!m_renderPreviewMode) workspace->compositionMode->click();
            findChild<QToolButton *>("renderPrimary")->click();
            if (m_renderQueue.size() != 7)
                return finish("Stop-queue fixtures were not captured");
            workspace->runQueue->trigger();
            findChild<QPushButton *>("stopRenderQueue")->click();
            state->phase = 9;
            return;
        }
        if (state->phase == 9)
        {
            if (m_activeQueueId)
                return;
            if (m_queueRunning || m_renderQueue[5].status != tr("已停止") ||
                m_renderQueue[6].status != tr("等待中"))
                return finish("Stop queue did not retain waiting tasks");
            workspace->runQueue->trigger();
            state->phase = 10;
            return;
        }
        if (state->phase == 10)
        {
            if (m_activeQueueId || m_queueRunning)
                return;
            if (m_renderQueue[6].status != tr("完成"))
                return finish("Retained task did not run on queue restart");
            outputSamples->setValue(128);
            if (!m_renderPreviewMode) workspace->compositionMode->click();
            findChild<QToolButton *>("renderPrimary")->click();
            outputSamples->setValue(2);
            {
                const QSignalBlocker block(outputDenoise);
                outputDenoise->setCurrentIndex(int(DenoiseMode::OIDN));
            }
            m_renderFormat->setCurrentIndex(1);
            if (!m_renderPreviewMode) workspace->compositionMode->click();
            findChild<QToolButton *>("renderPrimary")->click();
            if (m_renderQueue.size() != 9 || !m_renderQueue[8].request.settings.denoise ||
                m_renderQueue[8].format != "jpg")
                return finish("Export-failure fixtures were not captured");
            // Stop at the worker's rendering boundary. A 100-ms UI poll can miss
            // the entire small task on a fast GPU, so the fault must be installed
            // before sampling is allowed to finish.
            const auto blockedId = m_renderQueue[7].request.id;
            auto worker = m_queueWorker;
            state->exportPause = connect(worker, &RenderQueueThread::jobState, worker,
                [worker, blockedId](quint64 id, RenderJobState status) {
                    if (id == blockedId && status == RenderJobState::Rendering)
                        worker->pauseCurrent(true);
                }, Qt::DirectConnection);
            workspace->runQueue->trigger();
            state->phase = 11;
            return;
        }
        if (state->phase == 11)
        {
            if (m_renderQueue[7].status != tr("已暂停"))
                return;
            if (!QDir().mkpath(m_renderQueue[7].request.outputPath))
                return finish("Could not block the first export output path");
            state->blockedExport = true;
            QObject::disconnect(state->exportPause);
            m_queueWorker->pauseCurrent(false);
            state->phase = 12;
            return;
        }
        if (state->phase == 12)
        {
            if (m_activeQueueId || m_queueRunning)
                return;
            if (!state->blockedExport || m_renderQueue[7].status != tr("导出失败") ||
                m_renderQueue[7].result.isNull() || m_renderQueue[8].status != tr("完成") ||
                !m_renderQueue[8].request.outputPath.endsWith(".jpg") ||
                !QFileInfo::exists(m_renderQueue[8].request.outputPath))
                return finish("Export failure did not retain the result and continue the queue");
            const QString assetPath = output + "/queued-resource.txt";
            QFile asset(assetPath);
            if (!asset.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
                asset.write("before") != 6)
                return finish("Could not create queued-resource fixture");
            asset.close();
            QueueItem changed;
            changed.request.id = m_nextQueueId++;
            changed.request.document = editor->document;
            changed.request.document.root["hdr"] = assetPath;
            changed.request.assets = editor->cache;
            changed.request.settings = RenderJobSettings{QSize(96, 54), 2, 128, 8, false};
            changed.request.resourceSignatures = renderResourceSignatures(changed.request.document);
            changed.name = "Changed resource";
            m_renderQueue.append(changed);
            refreshRenderQueue();
            if (!asset.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
                asset.write("after") != 5)
                return finish("Could not modify queued-resource fixture");
            asset.close();
            if (!m_renderPreviewMode) workspace->compositionMode->click();
            findChild<QToolButton *>("renderPrimary")->click();
            if (m_renderQueue.size() != 11)
                return finish("Follow-up task was not captured after changed resource");
            workspace->runQueue->trigger();
            state->phase = 13;
            return;
        }
        if (state->phase == 13)
        {
            if (m_activeQueueId || m_queueRunning)
                return;
            if (m_renderQueue[9].status != tr("失败") ||
                !m_renderQueue[9].error.contains("resource changed") ||
                m_renderQueue[10].status != tr("完成"))
                return finish("Changed queued resource did not fail independently");
            finish({});
        }
    });
    timer->start();
}

void learnQT::configureWorkspaceRegression()
{
    auto args = QCoreApplication::arguments();
    int option = args.indexOf("--workspace-regression");
    if (option < 0 || option + 1 >= args.size())
        return;
    auto output = QFileInfo(args[option + 1]).absoluteFilePath();
    bool coldHome = args.contains("--cold-home");
    if (coldHome)
        navigateWorkspace(WorkspacePage::Home);
    QDir().mkpath(output);
    struct State
    {
        int phase = 0, page = 0, visit = 0, frames = 0;
        QObject *parent = nullptr;
        RenderThread *thread = nullptr;
        QJsonObject document;
        int undo = 0;
        QElapsedTimer clock;
        QJsonArray captures;
        RenderStats stats;
        bool statsReady = false;
        bool coldRequested = false;
        bool compactRequested = false;
    };
    auto state = std::make_shared<State>();
    state->clock.start();
    connect(viewport, &GLWidget::framePresented, this, [state] { ++state->frames; });
    connect(viewport, &GLWidget::renderThreadReady, this, [this, state] {
        connect(viewport->renderThread(), &RenderThread::statsReady, this, [state](RenderStats stats) {
            state->stats = stats;
            state->statsReady = true;
        });
    });
    auto timer = new QTimer(this);
    timer->setInterval(180);
    auto finish = [this, timer, output, state](const QString &error) {
        timer->stop();
        if (!error.isEmpty()) {
            QJsonArray metrics;
            for (auto widget : findChildren<QWidget *>())
                if (widget->isVisible() && (widget->minimumSizeHint().height() > 250 || widget->minimumHeight() > 250))
                    metrics.append(QJsonObject{{"class", widget->metaObject()->className()}, {"name", widget->objectName()},
                        {"minHint", widget->minimumSizeHint().height()}, {"min", widget->minimumHeight()}, {"height", widget->height()}});
            QFile layout(output + "/layout-diagnostics.json");
            if (layout.open(QIODevice::WriteOnly)) layout.write(QJsonDocument(QJsonObject{{"height", height()}, {"minimum", minimumHeight()}, {"widgets", metrics}}).toJson());
        }
        if (!error.isEmpty())
            grab().save(output + "/failure.png");
        QFile f(output + "/report.json");
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(QJsonObject{{"passed", error.isEmpty()},
                                              {"error", error},
                                              {"captures", state->captures},
                                              {"phase", state->phase}})
                        .toJson());
        std::cout << (error.isEmpty() ? "Workspace regression passed" : error.toStdString()) << std::endl;
        m_sceneDirty = false;
        editor->markSaved();
        QCoreApplication::exit(error.isEmpty() ? 0 : 12);
    };
    connect(timer, &QTimer::timeout, this, [this, output, state, finish, coldHome] {
        if (state->clock.elapsed() > 90000)
        {
            finish("Workspace timed out");
            return;
        }
        if (m_loading)
            return;
        auto capture = [this, output, state](const QString &prefix) {
            QString file = prefix + QString::number(workspace->page) + ".png";
            auto shot = grab();
            shot.save(output + "/" + file);
            state->captures.append(QJsonObject{{"file", file},
                                               {"page", workspace->page},
                                               {"width", width()},
                                               {"height", height()},
                                               {"dpi", devicePixelRatioF()},
                                               {"bottomVisible", workspace->bottom->isVisible()},
                                               {"bottomHidden", workspace->bottom->isHidden()}});
        };
        if (state->phase == 0)
        {
            if (coldHome && !state->coldRequested)
            {
                if (viewport->renderThread())
                {
                    finish("Welcome page unexpectedly initialized GL");
                    return;
                }
                state->coldRequested = true;
                outputWidth->setValue(32);
                outputHeight->setValue(32);
                outputSamples->setValue(1);
                outputDenoise->setCurrentIndex(int(DenoiseMode::None));
                navigateWorkspace(WorkspacePage::Render);
                workspace->compositionMode->click();
                auto start = findChild<QToolButton *>("renderPrimary");
                if (!start || !start->isVisible())
                {
                    finish("Render page start button unavailable for cold render");
                    return;
                }
                startRender();
                // GL initialization may finish synchronously during navigation at high DPI.
                if (!workspace->pendingRender && !editor->renderLocked)
                {
                    finish("Cold render request was dropped");
                    return;
                }
                startRender(); // Repeated request must not enqueue a second legacy job.
                return;
            }
            if (coldHome && jobState != RenderJobState::Completed)
                return;
            if (!viewport->renderThread() || !state->statsReady)
                return;
            resize(1600, 900);
            state->parent = viewport->parent();
            state->thread = viewport->renderThread();
            state->document = editor->document.root;
            state->undo = editor->undo.index();
            state->phase = 1;
        }
        else if (state->phase == 1 || state->phase == 3)
        {
            if (state->visit % 2 == 0)
            {
                ++state->visit;
                workspace->navigation[state->page]->trigger();
                return;
            }
            if (state->phase == 3 && state->page >= 1 && state->page <= 5 &&
                (state->stats.version != viewport->sceneVersion() || state->stats.samples < 1 ||
                 state->stats.size != QSize(qRound(viewport->width() * viewport->devicePixelRatioF()),
                                            qRound(viewport->height() * viewport->devicePixelRatioF()))))
                return;
            ++state->visit;
            if (workspace->page != state->page || !workspace->navigation[state->page]->isChecked())
            {
                finish("Navigation mismatch");
                return;
            }
            if (state->page == 1 && workspace->bottom->isVisible())
            {
                finish("Scene resource panel is not collapsed by default");
                return;
            }
            if (state->page == 8 && !findChild<QSpinBox *>("recentFileLimit")->isVisible())
            {
                finish("Recent file limit setting not visible");
                return;
            }
            if (viewport->parent() != state->parent || viewport->renderThread() != state->thread)
            {
                finish("Viewport or render thread replaced");
                return;
            }
            if (editor->document.root != state->document || editor->undo.index() != state->undo)
            {
                finish("Navigation modified document or undo history");
                return;
            }
            for (auto dock : {treeDock, inspectorDock, workspace->left, workspace->bottom})
                if (dock->isFloating() || dock->features() != QDockWidget::NoDockWidgetFeatures)
                {
                    finish("Workspace allows floating");
                    return;
                }
            capture(state->phase == 1 ? "empty-" : "loaded-");
            ++state->page;
            if (state->page == 5) ++state->page;
            if (state->page < 9)
                return;
            if (state->phase == 1)
            {
                state->phase = 2;
                beginSceneLoad(QString::fromStdString(getResourcePath("scenes/lantern.scene.json")));
            }
            else
                state->phase = 4;
        }
        else if (state->phase == 2)
        {
            if (state->stats.version != viewport->sceneVersion() || state->stats.samples < 1)
                return;
            if (editor->document.root["objects"].toArray().isEmpty())
            {
                finish("Scene load failed");
                return;
            }
            auto objects = editor->document.root["objects"].toArray();
            auto id = objects.first().toObject()["id"].toString();
            editor->select({id}, id);
            state->document = editor->document.root;
            state->undo = editor->undo.index();
            state->page = 0;
            state->visit = 0;
            state->phase = 3;
        }
        else if (state->phase == 4)
        {
            if (workspace->navigation.size() != 8 || workspace->navigation.contains(5))
                return finish("Workspace navigation does not contain exactly eight pages");
            navigateWorkspace(WorkspacePage::Scene);
            if (dockWidgetArea(treeDock) != Qt::LeftDockWidgetArea || performanceDock->isVisible() ||
                inspector->findChild<QWidget *>("materialSection")->isVisible() || m_sceneList->isVisible())
                return finish("Scene panel responsibilities are incorrect");
            workspace->bottom->toggleViewAction()->trigger();
            resize(1366, 768);
            QApplication::processEvents();
            resize(1600, 900);
            if (!workspace->bottom->isVisible()) return finish("Resize hid explicitly expanded resources");
            workspace->bottom->hide();
            navigateWorkspace(WorkspacePage::Lights);
            if (workspace->page != int(WorkspacePage::Lighting) || workspace->lightingTabs->currentIndex() != 0)
                return finish("Legacy lights alias did not enter Lighting");
            navigateWorkspace(WorkspacePage::Environment);
            if (workspace->page != int(WorkspacePage::Lighting) || workspace->lightingTabs->currentIndex() != 1 ||
                workspace->bottom->isVisible()) return finish("Legacy environment alias or HDR panel failed");

            navigateWorkspace(WorkspacePage::Camera);
            auto fov = findChild<MixedSpin *>("cameraValue6");
            float before = viewport->camera.zoom;
            fov->setValue(before + 3);
            QMetaObject::invokeMethod(fov, "editingFinished");
            if (std::abs(viewport->camera.zoom - before - 3) > .001)
            {
                finish("Camera field not connected");
                return;
            }
            editor->undo.undo();
            if (std::abs(viewport->camera.zoom - before) > .001 || std::abs(fov->value() - before) > .001)
            {
                finish("Camera undo or field refresh failed");
                return;
            }
            navigateWorkspace(WorkspacePage::Material);
            workspace->materialFilter->setCurrentIndex(1);
            editor->select({});
            auto id = editor->document.root["materials"].toArray().first().toObject()["id"].toString();
            inspector->browseMaterial(id);
            if (inspector->findChild<MixedSpin *>("roughness")->isEnabled())
            {
                finish("Unselected material is writable");
                return;
            }
            navigateWorkspace(WorkspacePage::Lights);
            auto originalLights = editor->document.root["lights"].toArray();
            findChild<QPushButton *>("addSphereLight")->click();
            if (editor->document.root["lights"].toArray().size() != originalLights.size() + 1)
            {
                finish("Light creation not connected");
                return;
            }
            editor->undo.undo();
            if (editor->document.root["lights"].toArray() != originalLights)
            {
                finish("Light undo failed");
                return;
            }
            navigateWorkspace(WorkspacePage::Environment);
            auto enabled = findChild<QCheckBox *>("environmentEnabled");
            bool wasEnabled = enabled->isChecked();
            enabled->click();
            if (editor->document.settings().useEnvironmentMap == wasEnabled)
            {
                finish("Environment toggle not connected");
                return;
            }
            editor->undo.undo();
            if (enabled->isChecked() != wasEnabled)
            {
                finish("Environment undo did not refresh UI");
                return;
            }
            navigateWorkspace(WorkspacePage::Resources);
            auto search = findChild<QLineEdit *>("allResourcesSearch");
            search->setText("Lantern");
            navigateWorkspace(WorkspacePage::Home);
            navigateWorkspace(WorkspacePage::Resources);
            if (search->text() != "Lantern")
            {
                finish("Resource search lost on navigation");
                return;
            }
            navigateWorkspace(WorkspacePage::Scene);
            workspace->bottom->hide();
            navigateWorkspace(WorkspacePage::Material);
            navigateWorkspace(WorkspacePage::Scene);
            if (workspace->bottom->isVisible())
            {
                finish("Collapsed panel layout was not preserved");
                return;
            }
            findChild<QAction *>("resetWorkspaceLayout")->trigger();
            if (workspace->bottom->isVisible())
            {
                finish("Default layout reset failed");
                return;
            }
            navigateWorkspace(WorkspacePage::Render);
            workspace->compositionMode->click();
            if (workspace->bottom->isVisible() || !workspace->outputProperties->isVisible() ||
                !workspace->compositionMode->isChecked() || workspace->resultsMode->isChecked())
                return finish("Composition default layout failed");
            workspace->resultsMode->click();
            if (!workspace->bottom->isVisible() || !workspace->resultProperties->isVisible() ||
                workspace->compositionMode->isChecked() || !workspace->resultsMode->isChecked() ||
                workspace->outputProperties->isVisible()) return finish("Result layout did not separate task properties");
            workspace->bottom->toggleViewAction()->trigger();
            workspace->compositionMode->click();
            workspace->resultsMode->click();
            if (workspace->bottom->isVisible()) return finish("Result mode layout was not preserved independently");
            workspace->bottom->toggleViewAction()->trigger();
            workspace->compositionMode->click();
            if (workspace->bottom->isVisible()) return finish("Result layout leaked into composition");
            state->phase = 5;
        }
        else if (state->phase == 5)
        {
            navigateWorkspace(WorkspacePage::Render);
            outputWidth->setValue(320);
            outputHeight->setValue(180);
            outputSamples->setValue(1000000);
            outputDenoise->setCurrentIndex(int(DenoiseMode::None));
            workspace->compositionMode->click();
            auto start = findChild<QToolButton *>("renderPrimary");
            if (!start || !start->isVisible())
            {
                finish("Render page start button unavailable");
                return;
            }
            startRender();
            state->phase = 6;
        }
        else if (state->phase == 6)
        {
            if (jobState != RenderJobState::Rendering)
                return;
            navigateWorkspace(WorkspacePage::Settings);
            if (!editor->renderLocked || renderAction->isEnabled() || !stopAction->isEnabled())
            {
                finish("Cross-page render lock incorrect");
                return;
            }
            pauseAction->trigger();
            state->phase = 7;
        }
        else if (state->phase == 7)
        {
            if (jobState != RenderJobState::Paused)
                return;
            navigateWorkspace(WorkspacePage::Camera);
            if (findChild<MixedSpin *>("cameraValue6")->isEnabled())
            {
                finish("Paused camera writable");
                return;
            }
            pauseAction->trigger();
            state->phase = 8;
        }
        else if (state->phase == 8)
        {
            if (jobState != RenderJobState::Rendering)
                return;
            navigateWorkspace(WorkspacePage::Home);
            stopAction->trigger();
            state->phase = 9;
        }
        else if (state->phase == 9)
        {
            if (jobState != RenderJobState::Stopped)
                return;
            if (editor->renderLocked || !renderAction->isEnabled())
            {
                finish("Stop did not unlock editing");
                return;
            }
            outputWidth->setValue(64);
            outputHeight->setValue(64);
            outputSamples->setValue(2);
            navigateWorkspace(WorkspacePage::Render);
            startRender();
            state->phase = 10;
        }
        else if (state->phase == 10)
        {
            if (jobState != RenderJobState::Completed)
                return;
            if (lastResult.size() != QSize(64, 64) || !lastResult.save(output + "/result.png") ||
                !lastResult.save(output + "/result.jpg", "JPEG", 95))
            {
                finish("Completed output invalid");
                return;
            }
            navigateWorkspace(WorkspacePage::Scene);
            state->phase = 11;
        }
        else if (state->phase == 11)
        {
            // Allow the preceding page's hidden docks to release their minimum size.
            if (!state->compactRequested) { state->compactRequested = true; resize(1366, 768); return; }
            if (size() != QSize(1366, 768)) { finish("Compact window was enlarged by minimum layout size"); return; }
            auto start = findChild<QToolButton *>("renderPrimary");
            if (!start || start->isVisible() || workspace->renderModes->isVisible())
            {
                finish("Render controls visible outside the render page");
                return;
            }
            renderAction->trigger();
            if (jobState != RenderJobState::Completed || editor->renderLocked ||
                workspace->page != int(WorkspacePage::Render) || !m_renderPreviewMode)
            {
                finish("Render shortcut did not enter composition without starting a job");
                return;
            }
            navigateWorkspace(WorkspacePage::Scene);
            capture("compact-");
            if (viewport->width() < 160 || viewport->height() < 120)
            {
                finish("Compact viewport unusable");
                return;
            }
            navigateWorkspace(WorkspacePage::Render);
            if (!start->isVisible() || !rect().contains(QRect(start->mapTo(this, QPoint()), start->size())))
            {
                finish("Render page start button hidden in compact window");
                return;
            }
            capture("compact-render-");
            resize(1920, 1080);
            state->phase = 12;
        }
        else if (state->phase == 12)
        {
            if (workspace->task->item(0, 1)->text() != QString("64 × 64") ||
                workspace->task->item(0, 2)->text() != "2 / 2 spp")
            {
                finish("Completed task metadata overwritten by preview stats");
                return;
            }
            capture("large-");
            auto before = workspace->recent.size();
            auto path = output + "/saved.scene.json";
            QString error;
            if (!editor->document.saveScene(path, error))
            {
                finish(error);
                return;
            }
            rememberScene(path);
            rememberScene(path);
            if (workspace->recent.count(QFileInfo(path).absoluteFilePath()) != 1 ||
                workspace->recent.size() > before + 1)
            {
                finish("Recent file deduplication failed");
                return;
            }
            QSettings preferences(QSettings::defaultFormat(), QSettings::UserScope, "learnQT",
                                  "SceneWorkbench");
            preferences.sync();
            if (preferences.status() != QSettings::NoError || !preferences.fileName().startsWith(output) ||
                preferences.value("workspaceV4/recent").toStringList() != workspace->recent)
            {
                finish("Preferences not persisted in isolated test directory");
                return;
            }
            navigateWorkspace(WorkspacePage::Home);
            state->phase = 13;
        }
        else if (state->phase == 13)
        {
            capture("recent-");
            // Exercise the production close handler without closing the test window.
            editor->markSaved();
            m_sceneDirty = false;
            QCloseEvent close;
            closeEvent(&close);
            QSettings stored(QSettings::defaultFormat(), QSettings::UserScope, "learnQT", "SceneWorkbench");
            stored.sync();
            for (const auto &key : {"scene", "render/composition", "render/results"})
                if (stored.value(QString("workspaceV5/layout/") + key).toByteArray().isEmpty())
                    return finish("Stable page/mode layout was not persisted by close handler");
            if (!close.isAccepted() || stored.value("workspaceV5/renderComposition").toBool() != m_renderPreviewMode)
                return finish("Render mode preference was not persisted");
            finish({});
        }
    });
    timer->start();
}
