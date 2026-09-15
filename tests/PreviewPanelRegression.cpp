#include "WorkspaceUi.h"
#include "learnQT.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QSpinBox>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>
#include <iostream>

// 顶栏“预览设置”入口的界面回归：弹出面板、更多设置弹窗、提交与同步。
// 全部通过对象名与控件 API 驱动，不使用屏幕坐标点击，也不依赖窗口位置。
void learnQT::configurePreviewPanelRegression()
{
    const QStringList arguments = QCoreApplication::arguments();
    const int option = arguments.indexOf(QStringLiteral("--preview-panel-regression"));
    if (option < 0 || option + 1 >= arguments.size())
    {
        return;
    }
    const QString output = QFileInfo(arguments[option + 1]).absoluteFilePath();
    QDir().mkpath(output);

    struct State
    {
        int phase = 0;
        int commits = 0;
        EditorController::Change firstChange = EditorController::Display;
        int pageBeforeDialog = -1;
        int dialogSamples = 0;
        EditorController::Change dialogChange = EditorController::Display;
        QElapsedTimer clock;
    };
    auto state = std::make_shared<State>();
    state->clock.start();
    connect(editor, &EditorController::changed, this, [state](int change) {
        ++state->commits;
        if (state->commits == 1)
        {
            state->firstChange = EditorController::Change(change);
        }
        state->dialogChange = EditorController::Change(change);
    });

    auto timer = new QTimer(this);
    timer->setInterval(150);
    auto finish = [this, timer, output, state](const QString &error) {
        timer->stop();
        // 回归产生的未保存状态不应触发关闭确认或写入用户偏好。
        m_sceneDirty = false;
        editor->markSaved();
        QJsonObject report{{"passed", error.isEmpty()},
                           {"error", error},
                           {"phase", state->phase},
                           {"commits", state->commits},
                           {"dialogSamples", state->dialogSamples}};
        QFile file(output + "/report.json");
        if (file.open(QIODevice::WriteOnly))
        {
            file.write(QJsonDocument(report).toJson());
        }
        if (!error.isEmpty())
        {
            grab().save(output + "/failure.png");
        }
        std::cout << (error.isEmpty() ? "Preview panel regression passed" : error.toStdString()) << std::endl;
        QCoreApplication::exit(error.isEmpty() ? 0 : 12);
    };

    connect(timer, &QTimer::timeout, this, [this, state, finish] {
        if (state->clock.elapsed() > 60000)
        {
            finish(QStringLiteral("Preview panel regression timed out"));
            return;
        }
        if (m_loading)
        {
            return;
        }

        // 1) 顶栏入口存在，替代了原“渲染设置”，并且挂的是即时弹出菜单。
        if (state->phase == 0)
        {
            auto toolbar = findChild<QToolBar *>(QStringLiteral("workbenchToolbar"));
            if (!toolbar)
            {
                finish(QStringLiteral("Workbench toolbar is missing"));
                return;
            }
            for (auto action : toolbar->actions())
            {
                if (action->text().contains(QStringLiteral("渲染设置")))
                {
                    finish(QStringLiteral("The removed 渲染设置 action is still on the toolbar"));
                    return;
                }
            }
            auto button = findChild<QToolButton *>(QStringLiteral("previewSettingsButton"));
            if (!button)
            {
                finish(QStringLiteral("Toolbar preview settings button is missing"));
                return;
            }
            if (button->text() != QStringLiteral("预览设置") || !toolbar->isAncestorOf(button))
            {
                finish(QStringLiteral("Toolbar preview settings button is not placed on the toolbar"));
                return;
            }
            if (!button->menu() || button->popupMode() != QToolButton::InstantPopup)
            {
                finish(QStringLiteral("Toolbar preview settings button has no instant popup menu"));
                return;
            }
            if (!previewChromePanel || !previewDetailPanel)
            {
                finish(QStringLiteral("Preview settings panels were not created"));
                return;
            }
            bool hasInlinePanel = false, hasDialogAction = false;
            for (auto action : button->menu()->actions())
            {
                if (action->isSeparator())
                {
                    continue;
                }
                if (action->text().startsWith(QStringLiteral("更多预览设置")))
                {
                    hasDialogAction = action->isEnabled();
                    continue;
                }
                auto widgetAction = qobject_cast<QWidgetAction *>(action);
                if (widgetAction && widgetAction->defaultWidget() == previewChromePanel)
                {
                    hasInlinePanel = true;
                }
            }
            if (!hasInlinePanel || !hasDialogAction)
            {
                finish(QStringLiteral("Popup menu is missing the inline panel or the dialog action"));
                return;
            }
            state->phase = 1;
            return;
        }

        // 2) 弹出面板与文档设置一致。
        if (state->phase == 1)
        {
            const auto documentSettings = editor->document.settings();
            const auto shown = previewChromePanel->values();
            if (shown.maxRenderFrames != documentSettings.maxRenderFrames ||
                shown.maxBounces != documentSettings.maxBounces || shown.tileSize != documentSettings.tileSize ||
                shown.useTileRendering != documentSettings.useTileRendering ||
                shown.renderLow != documentSettings.renderLow || shown.denoise != documentSettings.denoise)
            {
                finish(QStringLiteral("Popup values diverged from the document settings"));
                return;
            }
            state->phase = 2;
            return;
        }

        // 3) 弹出面板里改一个常用项：恰好提交一次，而且是 display 变更。
        if (state->phase == 2)
        {
            const int before = state->commits;
            auto samples = previewChromePanel->findChild<QSpinBox *>();
            if (!samples)
            {
                finish(QStringLiteral("Popup settings panel has no spin box"));
                return;
            }
            samples->setValue(3);
            if (state->commits != before + 1 || state->firstChange != EditorController::Display)
            {
                finish(QStringLiteral("Popup edit did not commit exactly one display change"));
                return;
            }
            state->phase = 3;
            return;
        }

        // 4) “更多预览设置…”打开的是不跳页的非模态弹窗，且显示当前值。
        if (state->phase == 3)
        {
            state->pageBeforeDialog = workspace->page;
            showPreviewSettingsDialog();
            if (!previewDialog)
            {
                finish(QStringLiteral("Preview settings dialog was not created"));
                return;
            }
            if (!previewDialog->isVisible() || previewDialog->isModal())
            {
                finish(QStringLiteral("Preview settings dialog is not a visible non-modal window"));
                return;
            }
            if (workspace->page != state->pageBeforeDialog)
            {
                finish(QStringLiteral("Opening the dialog navigated away from the current page"));
                return;
            }
            if (previewDetailPanel->values().maxRenderFrames !=
                editor->document.settings().maxRenderFrames)
            {
                finish(QStringLiteral("Dialog did not load the current preview settings"));
                return;
            }
            state->phase = 4;
            return;
        }

        // 5) 弹窗里的改动同样即时提交，两个面板保持同值。
        if (state->phase == 4)
        {
            const int before = state->commits;
            auto samples = previewDetailPanel->findChild<QSpinBox *>();
            if (!samples)
            {
                finish(QStringLiteral("Dialog settings panel has no spin box"));
                return;
            }
            samples->setValue(5);
            if (state->commits != before + 1 || state->dialogChange != EditorController::Display)
            {
                finish(QStringLiteral("Dialog edit did not commit exactly one display change"));
                return;
            }
            if (editor->document.settings().maxRenderFrames != 5)
            {
                finish(QStringLiteral("Dialog edit did not reach the document settings"));
                return;
            }
            if (previewChromePanel->values().maxRenderFrames != 5)
            {
                finish(QStringLiteral("Popup panel did not follow the dialog edit"));
                return;
            }
            state->dialogSamples = editor->document.settings().maxRenderFrames;
            auto settings = editor->document.settings();
            settings.useEnvironmentMap = false;
            settings.renderLow = false;
            applyPreviewSettingsForTesting(settings);
            samples->setValue(6);
            if (editor->document.settings().useEnvironmentMap)
            {
                finish("Editing preview samples re-enabled the environment");
                return;
            }
            auto mode = previewDetailPanel->findChild<QComboBox *>("interactionMode");
            auto low = previewDetailPanel->findChild<QCheckBox *>("previewLowResolution");
            if (!mode || !low)
            {
                finish("Missing interaction/resolution controls");
                return;
            }
            mode->setCurrentIndex(RenderParams::InteractionLowResolution);
            if (editor->document.settings().renderLow || !low->isEnabled())
            {
                finish("Temporary interaction resolution overwrote persistent resolution");
                return;
            }
            low->click();
            mode->setCurrentIndex(RenderParams::InteractionRaster);
            if (!editor->document.settings().renderLow || editor->document.settings().useEnvironmentMap)
            {
                finish("Changing interaction mode discarded another preview setting");
                return;
            }
            const auto beforeLock = editor->document.root;
            editor->renderLocked = true;
            syncWorkspaceAvailability();
            const bool panelsDisabled = !previewChromePanel->isEnabled() && !previewDetailPanel->isEnabled();
            settings.maxRenderFrames = 11;
            applyPreviewSettingsForTesting(settings);
            editor->renderLocked = false;
            syncWorkspaceAvailability();
            if (!panelsDisabled || editor->document.root != beforeLock)
            {
                finish("Preview controls remained editable during a render job");
                return;
            }
            finish(QString());
        }
    });
    timer->start();
}
