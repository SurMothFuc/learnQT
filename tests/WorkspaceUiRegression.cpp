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
                outputDenoise->setChecked(false);
                startRender();
                if (!workspace->pendingRender)
                {
                    finish("Cold render request was dropped");
                    return;
                }
                startRender(); // Repeated click must not enqueue a second request.
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
            if (state->page == 1 && width() >= 1450 && !workspace->bottom->isVisible())
            {
                finish("Scene resource panel missing from default layout");
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
            if (++state->page < 9)
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
            if (!workspace->bottom->isVisible())
            {
                finish("Default layout reset failed");
                return;
            }
            state->phase = 5;
        }
        else if (state->phase == 5)
        {
            navigateWorkspace(WorkspacePage::Render);
            outputWidth->setValue(320);
            outputHeight->setValue(180);
            outputSamples->setValue(1000000);
            outputDenoise->setChecked(false);
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
            if (!start->isVisible() || !rect().contains(QRect(start->mapTo(this, QPoint()), start->size()))) { finish("Render command hidden in compact toolbar"); return; }
            capture("compact-");
            if (viewport->width() < 160 || viewport->height() < 120)
            {
                finish("Compact viewport unusable");
                return;
            }
            resize(1920, 1080);
            navigateWorkspace(WorkspacePage::Render);
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
            finish({});
        }
    });
    timer->start();
}
