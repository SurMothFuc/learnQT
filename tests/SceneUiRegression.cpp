#include "learnQT.h"
#include <QAbstractButton>
#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTimer>
#include <QUuid>

namespace
{
void answerDialog(QMessageBox::StandardButton answer)
{
    auto poll = std::make_shared<std::function<void(int)>>();
    std::weak_ptr<std::function<void(int)>> weak = poll;
    *poll = [weak, answer](int remaining) {
        for (auto widget : QApplication::topLevelWidgets())
        {
            auto box = qobject_cast<QMessageBox *>(widget);
            if (box && box->isVisible() && box->button(answer))
            {
                box->button(answer)->click();
                return;
            }
        }
        if (remaining > 0)
            if (auto strong = weak.lock())
                QTimer::singleShot(20, [strong, remaining] { (*strong)(remaining - 1); });
    };
    QTimer::singleShot(20, [poll] { (*poll)(1000); });
}
} // namespace
void learnQT::configureSceneRegression()
{
    // Opt-in asset acceptance run. It exercises the real scene selector and
    // render thread without changing production scene/render interfaces.
    const auto importedArgs = QCoreApplication::arguments();
    const int importedOption = importedArgs.indexOf("--imported-scene-regression");
    if (importedOption >= 0 && importedOption + 1 < importedArgs.size())
    {
        struct ImportedState
        {
            int index = -1, phase = 0, frames = 0, resizeAttempts = 0, failures = 0, captureEdge = 512;
            QStringList paths;
            QString output;
            QSize desired;
            QElapsedTimer time;
            QJsonArray results;
            QJsonObject manifest;
        };
        auto state = std::make_shared<ImportedState>();
        const int edgeOption = importedArgs.indexOf("--imported-capture-edge");
        if (edgeOption >= 0 && edgeOption + 1 < importedArgs.size())
            state->captureEdge = std::max(256, std::min(1600, importedArgs[edgeOption + 1].toInt()));
        state->output = QFileInfo(importedArgs[importedOption + 1]).absoluteFilePath();
        QDir().mkpath(state->output);
        QFile manifest(QString::fromStdString(getResourcePath("imported/glslpt/conversion_manifest.json")));
        if (!manifest.open(QIODevice::ReadOnly))
        {
            QTimer::singleShot(0, [] { QCoreApplication::exit(6); });
            return;
        }
        const auto entries = QJsonDocument::fromJson(manifest.readAll()).object()["scenes"].toArray();
        QStringList filters;
        for (int i = 0; i + 1 < importedArgs.size(); ++i)
            if (importedArgs[i] == "--imported-entry")
                filters.append(importedArgs[i + 1]);
        bool allDiscovered = entries.size() == 55;
        for (auto v : entries)
        {
            auto entry = v.toObject();
            const auto filename = QFileInfo(entry["output"].toString()).fileName();
            const auto path = QString::fromStdString(getResourcePath(("scenes/" + filename).toStdString()));
            allDiscovered = allDiscovered && m_sceneList->findData(path) >= 0;
            state->manifest[filename] = entry;
            if (filters.isEmpty() || filters.contains(filename))
                state->paths.append(path);
        }
        if (!allDiscovered || state->paths.isEmpty())
        {
            std::cerr << "Imported scenes: expected all 55 entries in the UI scene list" << std::endl;
            QTimer::singleShot(0, [] { QCoreApplication::exit(6); });
            return;
        }
        state->paths.append(state->paths.front()); // Return to first scene after all switches.
        connect(viewport, &GLWidget::framePresented, this, [state] { ++state->frames; });
        auto timer = new QTimer(this);
        timer->setInterval(100);
        state->time.start();
        connect(timer, &QTimer::timeout, this, [this, state, timer] {
            auto writeResults = [state] {
                QFile file(state->output + "/results.json");
                if (file.open(QIODevice::WriteOnly))
                    file.write(QJsonDocument(QJsonObject{{"scene_list_entries", 55},
                                                         {"capture_presentations", 96},
                                                         {"full_frame_accumulation_limit", 64},
                                                         {"results", state->results}})
                                   .toJson());
            };
            auto abort = [timer, state, writeResults](const QString &message) {
                std::cerr << "Imported scene regression: " << message.toStdString() << std::endl;
                state->results.append(QJsonObject{{"failure", message}});
                writeResults();
                timer->stop();
                QCoreApplication::exit(6);
            };
            if (state->time.elapsed() > 300000)
            {
                abort("Scene timed out");
                return;
            }
            if (m_loading)
            {
                state->frames = 0;
                return;
            }
            if (state->phase == 0)
            {
                if (++state->index >= state->paths.size())
                {
                    grab().save(state->output + "/scene-list-ui.png");
                    writeResults();
                    timer->stop();
                    QCoreApplication::exit(state->failures ? 6 : 0);
                    return;
                }
                state->time.restart();
                m_sceneDirty = false;
                const int index = m_sceneList->findData(state->paths[state->index]);
                m_sceneList->setCurrentIndex(index);
                QMetaObject::invokeMethod(m_sceneList, "activated", Qt::DirectConnection, Q_ARG(int, index));
                state->phase = 1;
                state->frames = 0;
                return;
            }
            auto &scene = Scene::getInstance();
            if (scene.document.filePath != state->paths[state->index])
            {
                abort("Scene selector did not load requested file");
                return;
            }
            const auto filename = QFileInfo(scene.document.filePath).fileName();
            if (state->phase == 1)
            {
                const auto resolution =
                    scene.document.root["conversion"].toObject()["referenceResolution"].toArray();
                const double w = resolution[0].toDouble(1280), h = resolution[1].toDouble(720);
                const double scale = state->captureEdge / std::max(w, h);
                state->desired = QSize(qRound(w * scale), qRound(h * scale));
                state->resizeAttempts = 0;
                auto settings = scene.document.settings();
                settings.useTileRendering = false;
                settings.renderLow = false;
                settings.denoise = false;
                settings.maxRenderFrames = 64;
                RenderParams::instance().applySnapshot(settings);
                applyPreviewSettingsForTesting(settings);
                state->phase = 2;
                state->frames = 0;
                return;
            }
            if (state->phase == 2)
            {
                const QSize current = viewport->size();
                if (current != state->desired && state->resizeAttempts++ < 4)
                {
                    resize(size() + state->desired - current);
                    state->frames = 0;
                    return;
                }
                state->phase = 3;
                state->frames = 0;
                return;
            }
            viewport->update();
            if (state->frames < 96)
                return;
            const QImage image = viewport->grabFramebuffer().convertToFormat(QImage::Format_RGB32);
            double sum = 0, squared = 0;
            int nonBlack = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                {
                    const double l = qGray(image.pixel(x, y));
                    sum += l;
                    squared += l * l;
                    nonBlack += l > 0;
                }
            const double count = double(image.width()) * image.height();
            const double mean = count > 0 ? sum / count : 0;
            const double deviation = count > 0 ? std::sqrt(std::max(0.0, squared / count - mean * mean)) : 0;
            const bool revisit = state->index == state->paths.size() - 1;
            const QString png = filename + (revisit ? ".revisit.png" : ".png");
            bool passed = !image.isNull() && image.save(state->output + "/" + png) &&
                          nonBlack > count * .001 && mean < 254.9 && deviation > .1;
            const auto expected = state->manifest[filename].toObject();
            passed = passed && scene.document.root["models"].toArray().size() == expected["models"].toInt() &&
                     int(scene.triangles.size()) == expected["runtime"].toObject()["triangles"].toInt() &&
                     int(scene.textures.size()) == expected["runtime"].toObject()["textures"].toInt();
            QJsonObject result{{"file", filename},
                               {"revisit", revisit},
                               {"png", png},
                               {"passed", passed},
                               {"width", image.width()},
                               {"height", image.height()},
                               {"mean", mean},
                               {"stddev", deviation},
                               {"triangles", int(scene.triangles.size())},
                               {"textures", int(scene.textures.size())},
                               {"materials", scene.document.root["materials"].toArray().size()},
                               {"encoded_lights", int(scene.lights_encoded.size())}};
            if (filename == "glslpt_Camera_01_4k_gltf.scene.json")
            {
                int gold = 0, brown = 0;
                for (int y = 0; y < image.height(); ++y)
                    for (int x = 0; x < image.width(); ++x)
                    {
                        const auto pixel = image.pixel(x, y);
                        const double r = qRed(pixel), g = qGreen(pixel), b = qBlue(pixel);
                        gold += r > 25 && g > 15 && r > g * 1.15 && g > b * 1.5;
                        brown += r > 8 && r > g * 1.1 && g > b * 1.1;
                    }
                // The camera's warm metal trim and leather strap must survive
                // the actual 4K texture resize/upload/material/shader path.
                passed = passed && gold > count * .0005 && brown > count * .002;
                result["gold_pixels"] = gold;
                result["brown_pixels"] = brown;
                result["passed"] = passed;
            }
            const bool packageCase = filename == "glslpt_cornell_box_orig.scene.json" ||
                                     filename == "glslpt_jinx_gltf.scene.json" ||
                                     filename == "glslpt_volume_cube.scene.json";
            if (packageCase && !revisit)
            {
                QString error;
                SceneDocument snapshot;
                {
                    QMutexLocker lock(&param_mutex);
                    snapshot = editor->document;
                }
                const QString saved = state->output + "/" + filename + ".roundtrip.json";
                bool roundtrip = snapshot.saveScene(saved, error);
                auto restored =
                    roundtrip ? Scene::prepareScene(saved, false, error) : std::unique_ptr<Scene>();
                roundtrip = restored && restored->triangles.size() == scene.triangles.size() &&
                            restored->textures.size() == scene.textures.size() &&
                            (restored->camera.position - scene.camera.position).length() < 1e-5;
                restored.reset();
                const QString package =
                    state->output + "/package-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
                bool portable = roundtrip && snapshot.exportScenePackage(package, error);
                const QString moved = package + "-moved";
                portable = portable && QDir().rename(package, moved);
                auto packaged = portable ? Scene::prepareScene(moved + "/scene.scene.json", false, error)
                                         : std::unique_ptr<Scene>();
                portable = packaged && packaged->triangles.size() == scene.triangles.size() &&
                           packaged->textures.size() == scene.textures.size();
                bool externalRejected = false;
                if (packaged)
                {
                    auto escaped = packaged->document;
                    auto models = escaped.root["models"].toArray();
                    auto model = models[0].toObject();
                    model["source"] = snapshot.root["models"].toArray()[0].toObject()["source"];
                    models[0] = model;
                    escaped.root["models"] = models;
                    QString rejection;
                    externalRejected = !escaped.validate(rejection);
                }
                result["roundtrip"] = roundtrip;
                result["portable_moved"] = portable;
                result["external_reference_rejected"] = externalRejected;
                result["package"] = moved;
                if (!roundtrip || !portable || !externalRejected)
                {
                    result["error"] = error;
                    passed = false;
                }
                result["passed"] = passed;
            }
            if (!passed)
                ++state->failures;
            state->results.append(result);
            writeResults();
            std::cout << "Imported render " << state->index + 1 << "/" << state->paths.size() << " "
                      << filename.toStdString() << " mean=" << mean << " stddev=" << deviation
                      << " passed=" << passed << std::endl;
            state->phase = 0;
            state->frames = 0;
            m_sceneDirty = false;
        });
        timer->start();
        return;
    }
    const auto args = QCoreApplication::arguments();
    const int option = args.indexOf("--scene-switch-regression");
    if (option < 0 || option + 1 >= args.size())
        return;
    viewport->setFixedSize(qRound(640 / viewport->devicePixelRatioF()),
                           qRound(480 / viewport->devicePixelRatioF()));
    struct State
    {
        int stage = 0, frames = 0;
        QImage before;
        RenderStats stats;
        QElapsedTimer time;
    };
    auto state = std::make_shared<State>();
    state->time.start();
    const QString output = QFileInfo(args[option + 1]).absoluteFilePath();
    QDir().mkpath(output);
    connect(viewport, &GLWidget::framePresented, this, [state] { ++state->frames; });
    auto attachStats = [this, state] {
        connect(viewport->renderThread(), &RenderThread::statsReady, this,
                [state](RenderStats stats) { state->stats = stats; });
    };
    if (viewport->renderThread()) attachStats();
    else connect(viewport, &GLWidget::renderThreadReady, this, attachStats);
    auto timer = new QTimer(this);
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, [this, state, output, timer] {
        auto fail = [timer](const QString &message) {
            std::cerr << "UI scene regression: " << message.toStdString() << std::endl;
            timer->stop();
            QCoreApplication::exit(5);
        };
        if (state->time.elapsed() > 240000)
        {
            fail("Timed out");
            return;
        }
        if (m_loading)
        {
            state->frames = 0;
            return;
        }
        viewport->update();
        // A private-desktop frame pump can repaint the same incomplete tile many
        // times. Wait for this scene's completed PT rounds before comparing images.
        if (state->stats.version != viewport->sceneVersion() || state->stats.rasterActive ||
            state->stats.samples < 4)
        {
            state->frames = 0;
            return;
        }
        if (state->frames < 24)
            return;
        auto &scene = Scene::getInstance();
        const QString bedroom = QString::fromStdString(getResourcePath("scenes/bedroom.scene.json"));
        const QString lantern = QString::fromStdString(getResourcePath("scenes/lantern.scene.json"));
        auto select = [this](const QString &path) {
            const int index = m_sceneList->findData(path);
            m_sceneList->setCurrentIndex(index);
            QMetaObject::invokeMethod(m_sceneList, "activated", Qt::DirectConnection, Q_ARG(int, index));
        };
        if (state->stage == 0)
        {
            if (scene.textures.size() != 4 || m_sceneDirty)
            {
                fail("Initial bedroom state is wrong");
                return;
            }
            state->before = viewport->grabFramebuffer();
            state->before.save(output + "/bedroom-before.png");
            {
                viewport->camera.processMousePan(3, 2);
                editor->setCamera(viewport->camera);
            }
            viewport->markSceneDirty(SceneDirtyFlag::Camera);
            answerDialog(QMessageBox::Cancel);
            select(lantern);
            if (m_loading || !m_sceneDirty || scene.document.filePath != bedroom ||
                m_sceneList->currentData().toString() != bedroom)
            {
                fail("Cancel changed the scene or discarded dirty state");
                return;
            }
            answerDialog(QMessageBox::Discard);
            select(lantern);
            if (!m_loading || viewport->isEnabled() || inspectorDock->isEnabled())
            {
                fail("Loading did not disable conflicting operations");
                return;
            }
            state->stage = 1;
            state->frames = 0;
        }
        else if (state->stage == 1)
        {
            if (scene.textures.size() != 7 || scene.document.filePath != lantern || m_sceneDirty)
            {
                fail("Lantern switch failed or inherited bedroom textures");
                return;
            }
            viewport->grabFramebuffer().save(output + "/lantern.png");
            QFile bad(output + "/corrupt.scene.json");
            bad.open(QIODevice::WriteOnly);
            bad.write("{broken");
            bad.close();
            answerDialog(QMessageBox::Ok);
            beginSceneLoad(bad.fileName());
            state->stage = 2;
            state->frames = 0;
        }
        else if (state->stage == 2)
        {
            if (scene.document.filePath != lantern || scene.textures.size() != 7 || m_sceneDirty)
            {
                fail("Failed load destroyed the original scene");
                return;
            }
            // Save prompt writes only a disposable test document, never a preset.
            QString error;
            {
                QMutexLocker lock(&param_mutex);
                if (!scene.saveScene(output + "/saved-lantern.scene.json", error))
                {
                    fail(error);
                    return;
                }
            }
            editor->document.filePath = output + "/saved-lantern.scene.json";
            refreshScenes();
            editor->select({editor->document.root["groups"].toArray()[0].toObject()["id"].toString()});
            editor->setMaterialField("roughness", .43);
            if (!m_sceneDirty)
            {
                fail("Material adjustment did not mark scene dirty");
                return;
            }
            // Verify the close confirmation can be cancelled.
            answerDialog(QMessageBox::Cancel);
            QCloseEvent close;
            QApplication::sendEvent(this, &close);
            if (close.isAccepted())
            {
                fail("Close ignored Cancel");
                return;
            }
            answerDialog(QMessageBox::Save);
            select(bedroom);
            state->stage = 3;
            state->frames = 0;
        }
        else
        {
            if (scene.textures.size() != 4 || scene.document.filePath != bedroom || m_sceneDirty)
            {
                fail(
                    QString(
                        "Return to bedroom retained previous state: textures=%1 render=%2 editor=%3 dirty=%4")
                        .arg(scene.textures.size())
                        .arg(scene.document.filePath, editor->document.filePath)
                        .arg(m_sceneDirty));
                return;
            }
            SceneDocument saved;
            QString error;
            if (!SceneDocument::loadScene(output + "/saved-lantern.scene.json", saved, error) ||
                std::abs(saved.root["materials"].toArray()[0].toObject()["roughness"].toDouble() - .43) >
                    .001)
            {
                fail("Save prompt did not persist material changes");
                return;
            }
            const auto after = viewport->grabFramebuffer();
            after.save(output + "/bedroom-after.png");
            const auto a = state->before.scaled(64, 64).convertToFormat(QImage::Format_RGB32),
                       b = after.scaled(64, 64).convertToFormat(QImage::Format_RGB32);
            double difference = 0;
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x)
                {
                    auto p = a.pixel(x, y), q = b.pixel(x, y);
                    difference += std::abs(qRed(p) - qRed(q)) + std::abs(qGreen(p) - qGreen(q)) +
                                  std::abs(qBlue(p) - qBlue(q));
                }
            difference /= 64 * 64 * 3;
            if (difference > 18)
            {
                fail(QString("Bedroom pixels changed after roundtrip: %1").arg(difference));
                return;
            }
            grab().save(output + "/scene-management-ui.png");
            std::cout << "UI scene switch, Save/Discard/Cancel, close cancellation, failed-load rollback and "
                         "render history isolation passed. Mean pixel difference="
                      << difference << std::endl;
            timer->stop();
            QCoreApplication::exit(0);
        }
    });
    timer->start();
}
