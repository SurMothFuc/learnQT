#include "learnQT.h"
#include <QApplication>
#include <QCheckBox>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QListWidget>
#include <QMouseEvent>
#include <QTimer>
#include <iostream>

// Opt-in regression for actual preview controls at the requested DPI/window size.
// It is deliberately not part of default CTest: large external scenes are expensive.
void learnQT::configureLargeScenePreviewRegression()
{
    const auto args = QCoreApplication::arguments();
    const int option = args.indexOf("--large-scene-preview-regression");
    if (option < 0 || option + 1 >= args.size()) return;
    const QString output = QFileInfo(args[option + 1]).absoluteFilePath();
    QDir().mkpath(output);
    resize(1600, 900);
    const int sizeOption = args.indexOf("--large-scene-viewport-size");
    if (sizeOption >= 0 && sizeOption + 2 < args.size())
        viewport->setFixedSize(args[sizeOption + 1].toInt(), args[sizeOption + 2].toInt());
    struct State {
        RenderStats stats;
        int phase = 0;
        quint64 epoch = 0;
        QElapsedTimer total, stage, progress;
        QJsonArray cases;
    };
    auto state = std::make_shared<State>();
    const bool openPreset = args.contains("--large-scene-open-preset");
    if (openPreset) state->phase = -1;
    state->total.start(); state->stage.start(); state->progress.start();
    auto subscribe = [this, state] {
        connect(viewport->renderThread(), &RenderThread::statsReady, this,
                [state](RenderStats stats) { state->stats = stats; });
    };
    if (viewport->renderThread()) subscribe();
    else connect(viewport, &GLWidget::renderThreadReady, this, subscribe);
    auto timer = new QTimer(this); timer->setInterval(200);
    auto finish = [this, state, timer, output](QString error) {
        timer->stop(); m_sceneDirty = false; editor->markSaved();
        QFile file(output + "/report.json");
        if (file.open(QIODevice::WriteOnly))
            file.write(QJsonDocument(QJsonObject{{"passed", error.isEmpty()}, {"error", error},
                {"phase", state->phase}, {"cases", state->cases}}).toJson());
        qInfo() << "Large scene preview regression finished:" << error;
        QCoreApplication::exit(error.isEmpty() ? 0 : 13);
    };
    connect(timer, &QTimer::timeout, this, [this, state, finish, output] {
        if (state->total.elapsed() > 900000 || state->stage.elapsed() > 240000) {
            finish("Preview stage timed out"); return;
        }
        if (state->phase == -1) {
            if (m_loading) return;
            if (QFileInfo(editor->document.filePath).fileName() != "mcguire_powerplant.scene.json") {
                finish("Preset double click failed to load Powerplant"); return;
            }
            state->phase = 0; state->stage.restart();
            qInfo() << "Powerplant preset loaded through double-click events";
        }
        auto low = previewChromePanel->findChild<QCheckBox *>("previewLowResolution");
        auto lock = previewChromePanel->findChild<QCheckBox *>("rasterLock");
        QCheckBox *tiled = nullptr;
        for (auto check : previewChromePanel->findChildren<QCheckBox *>())
            if (check->text() == QStringLiteral("分块预览")) tiled = check;
        if (!low || !lock || !tiled || !previewChromePanel->isEnabled()) {
            finish("Preview controls unavailable"); return;
        }
        const auto &stats = state->stats;
        if (state->progress.elapsed() >= 5000) {
            qInfo() << "Preview progress:" << state->phase << "stageMs" << state->stage.elapsed()
                    << "spp" << stats.samples << "gpuMs" << stats.gpuMs
                    << "tilesPerSec" << stats.tileFps << "ticks" << stats.ticks
                    << "sleepMs" << stats.cadenceSleepMs;
            state->progress.restart();
        }
        const bool fresh = stats.accumulationVersion > state->epoch;
        const bool traced = fresh && !stats.rasterActive && stats.samples >= 2;
        const bool denoised = traced && stats.samples >= 4 &&
                             stats.denoisedVersion == stats.accumulationVersion;
        auto record = [&](const char *name) {
            viewport->grabFramebuffer().save(output + "/" + name + "-viewport.png");
            grab().save(output + "/" + name + ".png");
            state->cases.append(QJsonObject{{"name", name}, {"width", stats.size.width()},
                {"height", stats.size.height()}, {"samples", stats.samples},
                {"raster", stats.rasterActive}, {"epoch", double(stats.accumulationVersion)},
                {"denoisedEpoch", double(stats.denoisedVersion)}, {"oidnMs", stats.oidnMs},
                {"low", low->isChecked()}, {"tiled", tiled->isChecked()}, {"locked", lock->isChecked()}});
            qInfo() << "Preview stage passed:" << name << stats.size << stats.samples;
        };
        auto advance = [&](const char *name) {
            state->epoch = stats.accumulationVersion; ++state->phase; state->stage.restart();
            qInfo() << "Preview stage begin:" << name;
        };
        if (state->phase == 0 && denoised) {
            record("low-tiled"); advance("full-resolution-tiled"); low->click();
        } else if (state->phase == 1 && traced) {
            if (low->isChecked() || stats.size.width() < 500) {
                finish("Full resolution was not applied"); return;
            }
            record("full-resolution-tiled"); advance("full-resolution-full-frame"); tiled->click();
        } else if (state->phase == 2 && traced) {
            if (tiled->isChecked()) { finish("Full-frame mode was not applied"); return; }
            record("full-resolution-full-frame"); advance("raster-locked"); lock->click();
        } else if (state->phase == 3 && stats.rasterActive && state->stage.elapsed() > 2000) {
            record("raster-locked"); advance("raster-settings-toggle"); tiled->click(); low->click();
        } else if (state->phase == 4 && stats.rasterActive && state->stage.elapsed() > 2000) {
            record("raster-settings-toggle"); advance("return-to-low-pathtrace"); lock->click();
        } else if (state->phase == 5 && denoised) {
            record("return-to-low-pathtrace"); finish({});
        }
    });
    if (openPreset) QTimer::singleShot(100, this, [this, finish] {
        navigateWorkspace(WorkspacePage::Home);
        auto list = findChild<QListWidget *>("homePresets");
        QListWidgetItem *target = nullptr;
        if (list) for (int i = 0; i < list->count(); ++i)
            if (QFileInfo(list->item(i)->data(Qt::UserRole).toString()).fileName() == "mcguire_powerplant.scene.json")
                target = list->item(i);
        if (!target) { finish("Powerplant preset card unavailable"); return; }
        list->scrollToItem(target, QAbstractItemView::PositionAtCenter);
        const auto position = list->visualItemRect(target).center();
        for (auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease,
                          QEvent::MouseButtonDblClick, QEvent::MouseButtonRelease}) {
            QMouseEvent event(type, position, Qt::LeftButton,
                type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(list->viewport(), &event);
        }
        if (!m_loading) finish("Double-click event did not begin scene loading");
    });
    timer->start();
}
