#include "learnQT.h"
#include "WorkbenchStyle.h"
#include "WorkspaceUi.h"
#include <QActionGroup>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageWriter>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLineEdit>
#include <QLibrary>
#include <QMenuBar>
#include <QMimeData>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

learnQT::learnQT(QWidget *parent) : QMainWindow(parent)
{
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QApplication::setFont(QFont("Microsoft YaHei UI", 9));
    setFont(QApplication::font());
    setAcceptDrops(true);
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#1c202a"));
    palette.setColor(QPalette::WindowText, QColor("#cdd5e0"));
    palette.setColor(QPalette::Base, QColor("#161a23"));
    palette.setColor(QPalette::AlternateBase, QColor("#242e3a"));
    palette.setColor(QPalette::Text, QColor("#cdd5e0"));
    palette.setColor(QPalette::Button, QColor("#303d4e"));
    palette.setColor(QPalette::ButtonText, QColor("#cdd5e0"));
    palette.setColor(QPalette::Highlight, QColor("#246bfa"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, QColor("#303d4e"));
    palette.setColor(QPalette::ToolTipText, Qt::white);
    QApplication::setPalette(palette);
    setStyleSheet(WorkbenchStyle::stylesheet());
    setWindowIcon(WorkbenchStyle::icon("scene"));
    editor = new EditorController(this);
    editor->install(Scene::getInstance());
    viewport = new GLWidget;
    viewport->attachEditor(editor);
    setupWorkbench();
#ifdef Q_OS_WIN
    // Keep the native resize/snap/title-bar behavior, with a matching dark caption.
    using SetWindowAttribute = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
    const auto setWindowAttribute = reinterpret_cast<SetWindowAttribute>(
        QLibrary::resolve("dwmapi", "DwmSetWindowAttribute"));
    if (setWindowAttribute)
    {
        const BOOL dark = TRUE;
        const COLORREF caption = RGB(27, 31, 41);
        const COLORREF text = RGB(214, 219, 234);
        const auto window = reinterpret_cast<HWND>(winId());
        setWindowAttribute(window, 20, &dark, sizeof(dark));
        setWindowAttribute(window, 35, &caption, sizeof(caption));
        setWindowAttribute(window, 36, &text, sizeof(text));
    }
#endif
    connect(editor, &EditorController::changed, this, [this](int) {
        m_sceneDirty = !editor->undo.isClean();
        restoreSceneControls();
        updateTitle();
    });
    connect(&editor->undo, &QUndoStack::cleanChanged, this, [this](bool clean) {
        m_sceneDirty = !clean;
        updateTitle();
    });
    connect(editor, &EditorController::busyChanged, this, &learnQT::setLoading);
    connect(editor, &EditorController::failed, this, [this](const QString &error) {
        logMessage(error);
        QMessageBox::critical(this, tr("操作失败"), error);
    });
    connect(editor, &EditorController::progress, this,
            [this](const QString &message) { taskLabel->setText(message); });
    connect(viewport, &GLWidget::renderThreadReady, this, &learnQT::connectRenderThread);
    connect(viewport, &GLWidget::sceneEdited, this, &learnQT::setSceneDirty);
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "learnQT", "SceneWorkbench");
    if (!restoreGeometry(settings.value("geometry").toByteArray()))
        resize(1600, 900);
    refreshScenes();
    restoreSceneControls();
    updateTitle();
    if (QCoreApplication::arguments().size() == 1 && workspace->welcome)
        navigateWorkspace(WorkspacePage::Home);
    else
        navigateWorkspace(WorkspacePage::Scene);
    configureRegressionCapture();
    configureUiCapture();
#ifdef SCENE_TESTING
    configureSceneRegression();
    configureWorkbenchRegression();
    configureInteractionRegression();
    configurePreviewRegression();
    configurePreviewModeRegression();
    configureWorkspaceRegression();
    configurePreviewPanelRegression();
    configureRasterRegression();
    // 回归入口可关闭交互回退，避免默认的光栅化回退改变既有预览用例的判断。
    if (QCoreApplication::arguments().contains(QStringLiteral("--no-interaction-fallback")))
        connect(viewport, &GLWidget::renderThreadReady, this, [this] {
            if (viewport->renderThread())
                viewport->renderThread()->setInteractionFallbackDisabled(true);
        });
#endif
}
learnQT::~learnQT()
{
    if (m_loadWorker)
    {
        m_loadWorker->wait();
        delete m_loadWorker;
    }
    delete viewport;
    viewport = nullptr;
}
void learnQT::setupWorkbench()
{
    setDockOptions(AllowNestedDocks | AllowTabbedDocks | AnimatedDocks);
    views = new QTabWidget;
    views->setObjectName("workspaceViews");
    auto editorPage = new QWidget;
    auto editorLayout = new QVBoxLayout(editorPage);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(0);
    auto chrome = new QWidget;
    chrome->setObjectName("viewportChrome");
    auto chromeLayout = new QHBoxLayout(chrome);
    chromeLayout->setContentsMargins(8, 4, 8, 4);
    chromeLayout->addWidget(new QLabel(tr("透视")));
    previewBadge = new QLabel(tr("●  路径追踪预览"));
    previewBadge->setObjectName("muted");
    chromeLayout->addWidget(previewBadge);
    chromeLayout->addStretch();
    auto toolHint = new QLabel(tr("选择 Q"));
    toolHint->setObjectName("muted");
    chromeLayout->addWidget(toolHint);
    connect(viewport, &GLWidget::toolChanged, toolHint, [toolHint](int tool) {
        toolHint->setText(
            QStringList{tr("选择 Q"), tr("移动 W"), tr("旋转 E"), tr("缩放 R")}.value(tool));
    });
    auto focus = new QToolButton;
    focus->setIcon(WorkbenchStyle::icon("focus"));
    focus->setToolTip(tr("定位所选对象（F）"));
    connect(focus, &QToolButton::clicked, this, [this] { viewport->frameSelection(); });
    chromeLayout->addWidget(focus);
    editorLayout->addWidget(chrome);
    auto canvas = new QWidget;
    auto canvasLayout = new QGridLayout(canvas);
    canvasLayout->setContentsMargins(0, 0, 0, 0);
    canvasLayout->addWidget(viewport, 0, 0);
    auto emptySurface = new QWidget;
    emptySurface->setObjectName("emptySurface");
    auto emptySurfaceLayout = new QGridLayout(emptySurface);
    auto empty = new QFrame;
    empty->setObjectName("emptyCard");
    empty->setFixedWidth(380);
    auto emptyLayout = new QVBoxLayout(empty);
    emptyLayout->setContentsMargins(36, 32, 36, 32);
    emptyLayout->setSpacing(14);
    auto symbol = new QLabel;
    symbol->setPixmap(WorkbenchStyle::icon("scene").pixmap(48, 48));
    symbol->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(symbol);
    auto heading = new QLabel(tr("从一个场景开始"));
    heading->setObjectName("emptyTitle");
    heading->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(heading);
    auto hint = new QLabel(tr("拖入模型，或从预设中探索光影与材质"));
    hint->setObjectName("muted");
    hint->setWordWrap(true);
    hint->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(hint);
    auto import = new QPushButton(WorkbenchStyle::icon("import"), tr("导入模型…"));
    connect(import, &QPushButton::clicked, this, &learnQT::loadModel);
    emptyLayout->addWidget(import);
    auto preset = new QPushButton(tr("浏览场景预设"));
    connect(preset, &QPushButton::clicked, this, [this] {
        navigateWorkspace(WorkspacePage::Resources);
    });
    emptyLayout->addWidget(preset);
    emptySurfaceLayout->addWidget(empty, 0, 0, Qt::AlignCenter);
    canvasLayout->addWidget(emptySurface, 0, 0);
    auto refreshEmpty = [this, emptySurface] {
        emptySurface->setVisible(editor->document.root["objects"].toArray().isEmpty() &&
                                 editor->document.root["lights"].toArray().isEmpty() &&
                                 editor->document.root["hdr"].toString().isEmpty());
    };
    refreshEmpty();
    connect(editor, &EditorController::changed, this, [refreshEmpty](int) { refreshEmpty(); });
    connect(editor, &EditorController::busyChanged, empty,
            [empty](bool busy) { empty->setEnabled(!busy); });
    editorLayout->addWidget(canvas, 1);
    auto navigationHint =
        new QLabel(tr("  Alt + 左键  环绕    ·    中键  平移    ·    滚轮  缩放    ·    F  定位所选"));
    navigationHint->setObjectName("muted");
    navigationHint->setMinimumHeight(28);
    editorLayout->addWidget(navigationHint);
    views->addTab(editorPage, WorkbenchStyle::icon("scene"), tr("编辑视口"));
    auto results = new QWidget;
    auto resultLayout = new QVBoxLayout(results);
    resultLayout->setContentsMargins(0, 0, 0, 0);
    auto resultTools = new QToolBar;
    resultView = new ResultView;
    resultTools->addAction(tr("适应窗口"), resultView, &ResultView::fit);
    resultTools->addAction(tr("1:1"), resultView, &ResultView::actualSize);
    resultTools->addAction(tr("导出图片…"), this, &learnQT::saveGLImage);
    resultTools->addAction(tr("上次完成结果"), this, [this] {
        if (!lastResult.isNull())
            resultView->setImage(lastResult);
    });
    resultLayout->addWidget(resultTools);
    resultLayout->addWidget(resultView);
    views->addTab(results, tr("渲染结果"));
    setCentralWidget(views);
    connect(views, &QTabWidget::currentChanged, this, [this](int tab) {
        if (viewport->renderThread())
            viewport->renderThread()->setPreviewVisible(tab == 0);
    });
    auto file = menuBar()->addMenu(tr("文件"));
    auto toolbar = addToolBar(tr("工作台"));
    toolbar->setObjectName("workbenchToolbar");
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    toolbar->setIconSize(QSize(22, 22));
    auto action = [&](const QString &name, const QKeySequence &shortcut, std::function<void()> fn,
                      bool visible = true) {
        auto a = file->addAction(name, this, fn);
        a->setShortcut(shortcut);
        editActions.append(a);
        if (visible)
            toolbar->addAction(a);
        return a;
    };
    auto newAction = action(tr("新建"), QKeySequence::New, [this] {
        if (confirmDiscard())
            beginSceneLoad(QString());
    });
    auto openAction = action(tr("打开…"), QKeySequence::Open, [this] {
        if (!confirmDiscard())
            return;
        auto path = QFileDialog::getOpenFileName(this, tr("打开场景"),
                                                 QString::fromStdString(getResourcePath("scenes")),
                                                 tr("场景 (*.scene.json *.json)"));
        if (!path.isEmpty())
            beginSceneLoad(path);
    });
    auto saveAction = action(tr("保存"), QKeySequence::Save, [this] { saveSceneDocument(); });
    action(tr("另存为…"), QKeySequence::SaveAs, [this] { saveSceneDocument(true); }, false);
    auto importAction = action(tr("导入…"), QKeySequence("Ctrl+I"), [this] { loadModel(); });
    action(
        tr("导入并指定缩放…"), {},
        [this] {
            auto paths = QFileDialog::getOpenFileNames(this, tr("导入模型"), QString(),
                                                       tr("模型 (*.obj *.gltf *.glb *.fbx)"));
            if (paths.isEmpty())
                return;
            bool ok = false;
            double scale = QInputDialog::getDouble(
                this, tr("导入缩放"), tr("统一缩放倍数（1 保留源尺寸）"), 1, .000001, 1000000, 6, &ok);
            if (ok)
                editor->importFiles(paths, scale);
        },
        false);
    action(tr("导出便携包…"), {}, [this] { exportPackage(); }, false);
    newAction->setIcon(WorkbenchStyle::icon("new"));
    openAction->setIcon(WorkbenchStyle::icon("open"));
    saveAction->setIcon(WorkbenchStyle::icon("save"));
    importAction->setIcon(WorkbenchStyle::icon("import"));
    toolbar->addSeparator();
    undoAction = editor->undo.createUndoAction(this, tr("撤销"));
    undoAction->setShortcut(QKeySequence::Undo);
    redoAction = editor->undo.createRedoAction(this, tr("重做"));
    redoAction->setShortcut(QKeySequence::Redo);
    undoAction->setIcon(WorkbenchStyle::icon("undo"));
    redoAction->setIcon(WorkbenchStyle::icon("redo"));
    toolbar->addAction(undoAction);
    toolbar->addAction(redoAction);
    auto editMenu = menuBar()->addMenu(tr("编辑"));
    editMenu->addAction(undoAction);
    editMenu->addAction(redoAction);
    auto duplicate = editMenu->addAction(tr("复制模型"), editor, &EditorController::duplicate);
    duplicate->setShortcut(QKeySequence("Ctrl+D"));
    editActions.append(duplicate);
    toolbar->addSeparator();
    auto tools = new QActionGroup(this);
    QStringList labels = {tr("选择 Q"), tr("移动 W"), tr("旋转 E"), tr("缩放 R")};
    for (int i = 0; i < 4; ++i)
    {
        auto a = toolbar->addAction(
            WorkbenchStyle::icon(QStringList{"select", "move", "rotate", "scale"}[i]), labels[i]);
        a->setCheckable(true);
        tools->addAction(a);
        a->setChecked(i == 0);
        connect(a, &QAction::triggered, this, [this, i] {
            viewport->setTool(GLWidget::Tool(i));
            viewport->setFocus();
        });
        connect(viewport, &GLWidget::toolChanged, a, [a, i](int tool) { a->setChecked(tool == i); });
        editActions.append(a);
    }
    auto space = new QComboBox;
    space->addItems({tr("世界轴"), tr("局部轴")});
    space->setMaximumWidth(92);
    toolbar->addWidget(space);
    connect(space, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int i) { viewport->setLocalAxes(i == 1); });
    auto snapAction = toolbar->addAction(WorkbenchStyle::icon("snap"), tr("吸附"));
    snapAction->setCheckable(true);
    connect(snapAction, &QAction::toggled, this, [this](bool enabled) { viewport->snap = enabled; });
    editActions.append(snapAction);
    toolbar->addSeparator();
    renderAction = new QAction(WorkbenchStyle::icon("play"), tr("开始渲染"), this);
    connect(renderAction, &QAction::triggered, this, &learnQT::startRender);
    renderAction->setShortcut(QKeySequence("F12"));
    pauseAction = toolbar->addAction(tr("暂停"), this, [this] {
        if (viewport->renderThread())
            viewport->renderThread()->pauseJob(jobState != RenderJobState::Paused);
    });
    stopAction = toolbar->addAction(tr("停止"), this, [this] {
        if (viewport->renderThread())
            viewport->renderThread()->stopJob();
    });
    pauseAction->setIcon(WorkbenchStyle::icon("pause"));
    stopAction->setIcon(WorkbenchStyle::icon("stop"));
    // 顶栏的预览设置入口取代原“渲染设置”按钮：弹出面板里直接改常用项，更多设置走不跳页的弹窗。
    previewChromePanel = new PreviewSettingsPanel;
    previewDetailPanel = new PreviewSettingsPanel(this);
    previewDetailPanel->hide();
    connect(previewChromePanel, &PreviewSettingsPanel::changed, this,
            [this](const RenderParams::Snapshot &settings) { commitPreviewSettings(settings); });
    connect(previewDetailPanel, &PreviewSettingsPanel::changed, this,
            [this](const RenderParams::Snapshot &settings) { commitPreviewSettings(settings); });
    auto previewButton = new QToolButton(toolbar);
    previewButton->setObjectName("previewSettingsButton");
    previewButton->setText(tr("预览设置"));
    previewButton->setIcon(WorkbenchStyle::icon("settings"));
    previewButton->setToolTip(tr("交互预览的采样上限、反弹数、块大小与降噪"));
    previewButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    previewButton->setPopupMode(QToolButton::InstantPopup);
    auto previewMenu = new QMenu(previewButton);
    auto previewWidgetAction = new QWidgetAction(previewMenu);
    previewWidgetAction->setDefaultWidget(previewChromePanel);
    previewMenu->addAction(previewWidgetAction);
    previewMenu->addSeparator();
    previewMenu->addAction(tr("更多预览设置…"), this, [this] { showPreviewSettingsDialog(); });
    connect(previewMenu, &QMenu::aboutToShow, this, [this] {
        previewChromePanel->setValues(editor->document.settings());
    });
    previewButton->setMenu(previewMenu);
    auto previewWidgetActionForToolbar = new QWidgetAction(toolbar);
    previewWidgetActionForToolbar->setDefaultWidget(previewButton);
    toolbar->insertAction(pauseAction, previewWidgetActionForToolbar);
    toolbar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto renderControls = new QWidget;
    renderControls->setObjectName("renderControls");
    auto renderControlLayout = new QHBoxLayout(renderControls);
    renderControlLayout->setContentsMargins(6, 2, 6, 2);
    renderControlLayout->setSpacing(4);
    toolbar->removeAction(pauseAction);
    toolbar->removeAction(stopAction);
    for (auto action : {pauseAction, stopAction}) {
        auto control = new QToolButton;
        control->setDefaultAction(action);
        control->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        renderControlLayout->addWidget(control);
    }
    auto renderButton = new QToolButton;
    renderButton->setObjectName("renderPrimary");
    renderButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    renderButton->setDefaultAction(renderAction);
    renderControlLayout->addWidget(renderButton);
    menuBar()->setCornerWidget(renderControls, Qt::TopRightCorner);
    pauseAction->setEnabled(false);
    stopAction->setEnabled(false);
    setupTree();
    inspectorDock = new QDockWidget(tr("属性与设置"), this);
    inspectorDock->setObjectName("inspectorDock");
    auto tabs = new QTabWidget;
    tabs->setObjectName("inspectorTabs");
    auto scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    inspector = new ObjectInspector(editor);
    scroll->setWidget(inspector);
    tabs->addTab(scroll, tr("对象"));
    tabs->addTab(createSettings(), tr("渲染"));
    auto lightsScroll = new QScrollArea;
    lightsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lightsScroll->setWidgetResizable(true);
    lightsScroll->setWidget(new LightInspector(editor));
    tabs->addTab(lightsScroll, tr("灯光"));
    inspectorDock->setWidget(tabs);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);
    splitDockWidget(treeDock, inspectorDock, Qt::Horizontal);
    performanceDock = new QDockWidget(tr("性能"), this);
    performanceDock->setObjectName("performanceDock");
    performance = new PerformancePanel;
    performanceDock->setWidget(performance);
    addDockWidget(Qt::RightDockWidgetArea, performanceDock);
    splitDockWidget(inspectorDock, performanceDock, Qt::Vertical);
    // Put telemetry above the inspector, beside the full-height scene tree.
    splitDockWidget(performanceDock, inspectorDock, Qt::Vertical);
    logDock = new QDockWidget(tr("任务日志"), this);
    logDock->setObjectName("logDock");
    log = new QPlainTextEdit;
    log->setReadOnly(true);
    log->setMaximumBlockCount(1000);
    logDock->setWidget(log);
    addDockWidget(Qt::BottomDockWidgetArea, logDock);
    logDock->hide();
    auto viewMenu = menuBar()->addMenu(tr("视图"));

    auto help = menuBar()->addMenu(tr("帮助"));
    help->addAction(tr("操作说明"), this, [this] {
        QMessageBox::information(
            this, tr("工作台快捷键"),
            tr("左键选择 · Ctrl 追加 / 切换 · 树中 Shift 范围选择\nAlt + 左键环绕 · 中键平移 · "
               "滚轮缩放\nQ "
               "选择 · W 移动 · E 旋转 · R 缩放 · F 定位\nEsc 取消变换 · Delete 删除 · Ctrl+D "
               "复制\n\n组仅组织对象，换组不改变世界变换。\n正式渲染期间锁定编辑，完成后可导出 PNG / "
               "JPEG。"));
    });
    auto ready = new QLabel(tr("●"));
    ready->setStyleSheet("color:#59c99c;padding:0 7px;");
    statusBar()->addWidget(ready);
    taskLabel = new QLabel(tr("就绪 · 编辑预览"));
    progress = new QProgressBar;
    progress->setFixedWidth(145);
    progress->setRange(0, 100);
    progress->setTextVisible(false);
    progress->setToolTip(tr("正式渲染进度"));
    statsLabel = new QLabel;
    statusBar()->addWidget(taskLabel, 1);
    statusBar()->addPermanentWidget(statsLabel);
    statusBar()->addPermanentWidget(progress);
    auto logs = new QPushButton(tr("日志"));
    logs->setFlat(true);
    connect(logs, &QPushButton::clicked, this, [this] { logDock->setVisible(!logDock->isVisible()); });
    statusBar()->addPermanentWidget(logs);
    treeDock->setMinimumWidth(220);
    inspectorDock->setMinimumWidth(340);
    performanceDock->setMinimumWidth(340);
    resizeDocks({treeDock, inspectorDock}, {240, 350}, Qt::Horizontal);
    resizeDocks({performanceDock, inspectorDock}, {250, 500}, Qt::Vertical);
    setupDockTitle(treeDock, "scene");
    setupDockTitle(inspectorDock, "settings");
    setupDockTitle(performanceDock, "chart");
    setupDockTitle(logDock, "log");
    setupWorkspace();
    auto resetLayout = viewMenu->addAction(tr("恢复当前页面布局"), this, [this] {
        int page = workspace->page;
        workspace->page = -1;
        workspace->layouts.remove(page);
        navigateWorkspace(WorkspacePage(page));
    });
    resetLayout->setObjectName("resetWorkspaceLayout");
}
void learnQT::setupDockTitle(QDockWidget *dock, const QString &icon)
{
    if (dock->titleBarWidget()) dock->titleBarWidget()->deleteLater();
    auto title = new QWidget(dock);
    title->setObjectName("dockTitle");
    auto row = new QHBoxLayout(title);
    row->setContentsMargins(10, 5, 6, 5);
    row->setSpacing(7);
    auto symbol = new QLabel;
    symbol->setPixmap(WorkbenchStyle::icon(icon).pixmap(18, 18));
    row->addWidget(symbol);
    row->addWidget(new QLabel(dock->windowTitle()), 1);
    auto close = new QToolButton;
    close->setIcon(WorkbenchStyle::icon("close"));
    close->setIconSize(QSize(15, 15));
    close->setToolTip(tr("隐藏面板"));
    connect(close, &QToolButton::clicked, dock, &QDockWidget::hide);
    row->addWidget(close);
    dock->setTitleBarWidget(title);
}
void learnQT::setupTree()
{
    treeDock = new QDockWidget(tr("场景对象"), this);
    treeDock->setObjectName("treeDock");
    auto widget = new QWidget;
    auto layout = new QVBoxLayout(widget);
    layout->setContentsMargins(8, 8, 8, 8);
    m_sceneList = new QComboBox;
    m_sceneList->setObjectName("sceneList");
    m_sceneList->setMinimumContentsLength(12);
    m_sceneList->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    layout->addWidget(m_sceneList);
    connect(m_sceneList, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        auto path = m_sceneList->itemData(i).toString();
        if (path.isEmpty())
            return;
        if (confirmDiscard())
            beginSceneLoad(path);
        else
            refreshScenes();
    });
    auto search = new QLineEdit;
    search->setPlaceholderText(tr("搜索对象或组…"));
    search->setClearButtonEnabled(true);
    search->addAction(WorkbenchStyle::icon("search"), QLineEdit::LeadingPosition);
    layout->addWidget(search);
    treeModel = new SceneTreeModel(editor, this);
    treeFilter = new QSortFilterProxyModel(this);
    treeFilter->setSourceModel(treeModel);
    treeFilter->setRecursiveFilteringEnabled(true);
    treeFilter->setFilterCaseSensitivity(Qt::CaseInsensitive);
    connect(search, &QLineEdit::textChanged, treeFilter, &QSortFilterProxyModel::setFilterFixedString);
    tree = new QTreeView;
    tree->setObjectName("objectTree");
    tree->setModel(treeFilter);
    tree->setIndentation(16);
    tree->setIconSize(QSize(16, 16));
    tree->setUniformRowHeights(true);
    tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree->setDragDropMode(QAbstractItemView::InternalMove);
    tree->setDefaultDropAction(Qt::MoveAction);
    tree->setDragEnabled(true);
    tree->setAcceptDrops(true);
    tree->setDropIndicatorShown(true);
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    tree->header()->setStretchLastSection(false);
    tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    for (int i = 1; i < 3; ++i)
    {
        tree->header()->setSectionResizeMode(i, QHeaderView::Fixed);
        tree->setColumnWidth(i, 36);
    }
    layout->addWidget(tree);
    treeDock->setWidget(widget);
    addDockWidget(Qt::RightDockWidgetArea, treeDock);
    tree->expandAll();
    connect(tree->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        if (syncingSelection)
            return;
        QSet<QString> ids;
        for (auto i : tree->selectionModel()->selectedRows())
            ids.insert(treeModel->id(treeFilter->mapToSource(i)));
        editor->select(ids, treeModel->id(treeFilter->mapToSource(tree->currentIndex())));
    });
    connect(editor, &EditorController::selectionChanged, this, &learnQT::updateSelection);
    connect(tree, &QTreeView::customContextMenuRequested, this, &learnQT::contextMenu);
    connect(treeModel, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        syncingSelection = true;
        expanded.clear();
        std::function<void(QModelIndex)> collect = [&](QModelIndex parent) {
            for (int row = 0; row < treeFilter->rowCount(parent); ++row)
            {
                auto i = treeFilter->index(row, 0, parent);
                if (tree->isExpanded(i))
                    expanded.insert(i.data(Qt::UserRole).toString());
                collect(i);
            }
        };
        collect({});
    });
    connect(treeModel, &QAbstractItemModel::modelReset, this, [this] {
        for (auto id : expanded)
            tree->setExpanded(treeFilter->mapFromSource(treeModel->find(id)), true);
        tree->setExpanded(treeFilter->mapFromSource(treeModel->find("root")), true);
        syncingSelection = false;
        updateSelection();
    });
}
void learnQT::updateSelection()
{
    syncingSelection = true;
    tree->selectionModel()->clearSelection();
    for (auto id : editor->selection)
    {
        auto index = treeFilter->mapFromSource(treeModel->find(id));
        tree->selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
    auto active = treeFilter->mapFromSource(treeModel->find(editor->active));
    tree->selectionModel()->setCurrentIndex(active, QItemSelectionModel::NoUpdate);
    syncingSelection = false;
}
void learnQT::contextMenu(QPoint pos)
{
    if (m_loading || editor->renderLocked)
        return;
    auto index = treeFilter->mapToSource(tree->indexAt(pos));
    QString id = treeModel->id(index);
    if (id.isEmpty())
        id = "root";
    if (!editor->selection.contains(id))
        editor->select({id}, id);
    QMenu menu;
    if (editor->isGroup(id))
    {
        menu.addAction(tr("创建子组"), this, [this, id] { editor->createGroup(id); });
        menu.addAction(tr("导入到组…"), this, &learnQT::loadModel);
    }
    menu.addAction(tr("重命名"), this, [this, id] {
        auto index = treeFilter->mapFromSource(treeModel->find(id));
        tree->edit(index);
    });
    if (id != "root")
    {
        if (editor->isGroup(id))
        {
            menu.addAction(tr("删除组（保留内容）"), this, [this, id] { editor->remove({id}); });
            menu.addAction(tr("删除组及内容"), this, [this, id] { editor->remove({id}, true); });
        }
        else
        {
            menu.addAction(tr("复制模型"), editor, &EditorController::duplicate);
            menu.addAction(tr("删除模型"), this, [this] { editor->remove(editor->selection); });
            menu.addAction(
                editor->node(id)["visible"].toBool(true) ? tr("隐藏") : tr("显示"), this, [this, id] {
                    editor->setFlag(id, "visible", !editor->node(id)["visible"].toBool(true));
                });
            menu.addAction(
                editor->node(id)["locked"].toBool() ? tr("解锁") : tr("锁定"), this,
                [this, id] { editor->setFlag(id, "locked", !editor->node(id)["locked"].toBool()); });
        }
    }
    menu.exec(tree->viewport()->mapToGlobal(pos));
}
QWidget *learnQT::createSettings()
{
    auto scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto page = new QWidget;
    auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 8, 10, 12);
    layout->setSpacing(12);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto section = [&](const QString &title, const QString &name) {
        auto group = new QGroupBox(title);
        group->setObjectName(name);
        auto fields = new QFormLayout(group);
        fields->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        fields->setRowWrapPolicy(QFormLayout::WrapLongRows);
        fields->setVerticalSpacing(9);
        layout->addWidget(group);
        return fields;
    };
    auto form = section(tr("正式输出"), "outputSection");
    auto spin = [&](const QString &label, int lo, int hi, int value) {
        auto s = new QSpinBox;
        s->setRange(lo, hi);
        s->setValue(value);
        form->addRow(label, s);
        return s;
    };
    outputWidth = spin(tr("输出宽度"), 16, 16384, 1920);
    outputHeight = spin(tr("输出高度"), 16, 16384, 1080);
    outputSamples = spin(tr("目标 spp"), 1, 1000000, 256);
    outputTile = spin(tr("Tile 大小"), 16, 1024, 128);
    outputBounces = spin(tr("反弹数"), 1, 64, 8);
    outputDenoise = new QCheckBox(tr("正式出图降噪"));
    outputDenoise->setChecked(true);
    form->addRow(outputDenoise);
    auto commitOutput = [this] {
        if (m_restoring)
            return;
        auto d = editor->document;
        d.root["output"] =
            QJsonObject{{"width", outputWidth->value()},     {"height", outputHeight->value()},
                        {"samples", outputSamples->value()}, {"tileSize", outputTile->value()},
                        {"bounces", outputBounces->value()}, {"denoise", outputDenoise->isChecked()}};
        editor->submit(d, tr("输出设置"), EditorController::Display);
    };
    for (auto s : {outputWidth, outputHeight, outputSamples, outputTile, outputBounces})
        connect(s, &QSpinBox::editingFinished, this, commitOutput);
    connect(outputDenoise, &QCheckBox::toggled, this, [commitOutput] { commitOutput(); });
    form = section(tr("色彩管理"), "displaySection");
    auto exposure = new MixedSpin;
    exposure->setRange(-16, 16);
    exposure->setObjectName("exposure");
    form->addRow(tr("曝光 EV"), exposure);
    auto tonemap = new QComboBox;
    tonemap->addItems({tr("旧曲线"), tr("ACES 近似"), tr("线性裁切")});
    form->addRow(tr("色调映射"), tonemap);
    form = section(tr("环境照明"), "environmentSection");
    auto envIntensity = new MixedSpin;
    envIntensity->setRange(0, 10000);
    form->addRow(tr("环境强度"), envIntensity);
    auto envRotation = new MixedSpin;
    envRotation->setRange(-360, 360);
    form->addRow(tr("环境旋转 °"), envRotation);
    auto hdr = new QPushButton(tr("选择 HDR…"));
    form->addRow(tr("环境贴图"), hdr);
    connect(hdr, &QPushButton::clicked, this, [this] {
        auto path =
            QFileDialog::getOpenFileName(this, tr("选择环境"), QString(), tr("Radiance (*.hdr)"));
        if (path.isEmpty())
            return;
        auto d = editor->document;
        d.root["hdr"] = path;
        editor->submit(d, tr("更换环境"), EditorController::Environment);
    });
    auto envClear = new QPushButton(tr("清除环境贴图"));
    form->addRow(envClear);
    connect(envClear, &QPushButton::clicked, this, [this] {
        auto d = editor->document;
        d.root["hdr"] = "";
        editor->submit(d, tr("清除环境"), EditorController::Environment);
    });
    auto property = [this](QString group, QString key, QJsonValue value, int change) {
        if (m_restoring)
            return;
        auto d = editor->document;
        auto o = d.root[group].toObject();
        o[key] = value;
        d.root[group] = o;
        editor->submit(d, tr("调整 %1").arg(key), EditorController::Change(change));
    };
    connect(exposure, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [property](double v) { property("display", "exposure", v, EditorController::Display); });
    connect(tonemap, QOverload<int>::of(&QComboBox::activated), this,
            [property](int v) { property("display", "tonemap", v, EditorController::Display); });
    connect(
        envIntensity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [property](double v) { property("environment", "intensity", v, EditorController::Lighting); });
    connect(
        envRotation, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [property](double v) { property("environment", "rotation", v, EditorController::Lighting); });
    auto restoreEnvironment = [this, exposure, tonemap, envIntensity, envRotation] {
        m_restoring = true;
        auto display = editor->document.root["display"].toObject(),
             env = editor->document.root["environment"].toObject();
        exposure->showValue(display["exposure"].toDouble());
        tonemap->setCurrentIndex(display["tonemap"].toInt());
        envIntensity->showValue(env["intensity"].toDouble(1));
        envRotation->showValue(env["rotation"].toDouble());
        m_restoring = false;
    };
    restoreEnvironment();
    connect(editor, &EditorController::changed, this,
            [restoreEnvironment](int) { restoreEnvironment(); });
    form = section(tr("变换吸附"), "snapSection");
    auto snapMove = new MixedSpin;
    snapMove->setRange(.0001, 1e6);
    snapMove->setValue(.1);
    form->addRow(tr("移动吸附"), snapMove);
    auto snapRotate = new MixedSpin;
    snapRotate->setRange(.1, 180);
    snapRotate->setValue(15);
    form->addRow(tr("旋转吸附 °"), snapRotate);
    auto snapScale = new MixedSpin;
    snapScale->setRange(.1, 100);
    snapScale->setValue(10);
    form->addRow(tr("缩放吸附 %"), snapScale);
    connect(snapMove, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) { viewport->moveStep = v; });
    connect(snapRotate, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) { viewport->rotateStep = v; });
    connect(snapScale, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) { viewport->scaleStep = v / 100.; });
    layout->addStretch();
    scroll->setWidget(page);
    return scroll;
}
void learnQT::connectRenderThread()
{
    auto thread = viewport->renderThread();
    thread->setPreviewVisible(views->currentIndex() == 0);
    if (workspace->pendingRender)
        QTimer::singleShot(0, this, [this] { workspace->pendingRender = false; startRender(); });
    connect(thread, &RenderThread::statsReady, this, [this](RenderStats s) {
        performance->append(s);
        if (previewBadge)
            previewBadge->setText(s.rasterActive ? tr("●  光栅化预览")
                                                 : tr("●  路径追踪预览"));
        // 光栅化交互预览不累积采样，只显示帧耗时，避免把静态的 spp 当成卡住。
        statsLabel->setText(s.rasterActive
                                ? tr("%1 × %2  |  光栅化 %3 ms  |  已选 %4")
                                      .arg(s.size.width())
                                      .arg(s.size.height())
                                      .arg(s.rasterMs, 0, 'f', 1)
                                      .arg(editor->selectedModels().size())
                                : tr("%1 × %2  |  %3 spp  |  已选 %4")
                                          .arg(s.size.width())
                                          .arg(s.size.height())
                                          .arg(s.samples)
                                          .arg(editor->selectedModels().size()) +
                                      tr("  |  %1 秒").arg(s.jobSeconds, 0, 'f', 1));
        progress->setValue(s.target > 0 ? std::min(100, int(100. * s.samples / s.target)) : 0);
        if (workspace->page == int(WorkspacePage::Render) && workspace->taskTarget > 0) {
            statsLabel->setText(tr("输出 %1 × %2  |  %3 / %4 spp")
                .arg(workspace->taskSize.width()).arg(workspace->taskSize.height())
                .arg(workspace->taskSamples).arg(workspace->taskTarget));
            progress->setValue(int(100. * workspace->taskSamples / workspace->taskTarget));
        }
    });
    connect(thread, &RenderThread::resultReady, this, [this](QImage im, bool final) {
        if (im.isNull())
            return;
        resultView->setImage(im);
        if (final)
            lastResult = im;
    });
    connect(thread, &RenderThread::jobStateChanged, this,
            [this](RenderJobState state, const QString &message) {
                jobState = state;
                bool active = state == RenderJobState::Preparing ||
                              state == RenderJobState::Rendering || state == RenderJobState::Paused ||
                              state == RenderJobState::Denoising;
                editor->renderLocked = active;
                taskLabel->setText(renderJobText(state));
                pauseAction->setText(state == RenderJobState::Paused ? tr("继续") : tr("暂停"));
                pauseAction->setEnabled(active && state != RenderJobState::Denoising);
                stopAction->setEnabled(active);
                renderAction->setEnabled(!active && !m_loading);
                for (auto a : editActions)
                    a->setEnabled(!active && !m_loading);
                undoAction->setEnabled(!active && !m_loading && editor->undo.canUndo());
                redoAction->setEnabled(!active && !m_loading && editor->undo.canRedo());
                inspectorDock->setEnabled(!active && !m_loading);
                treeDock->setEnabled(!active && !m_loading);
                inspector->refresh();
                syncWorkspaceAvailability();
                if (state == RenderJobState::Failed && !lastResult.isNull())
                    resultView->setImage(lastResult);
                if (!message.isEmpty())
                    logMessage(message);
            });
}
void learnQT::startRender()
{
    if (m_loading || editor->renderLocked || workspace->pendingRender)
        return;
    if (!viewport->renderThread())
    {
        workspace->pendingRender = true;
        navigateWorkspace(WorkspacePage::Scene);
        return;
    }
    RenderJobSettings settings;
    settings.size = {outputWidth->value(), outputHeight->value()};
    settings.samples = outputSamples->value();
    settings.tileSize = outputTile->value();
    settings.bounces = outputBounces->value();
    settings.denoise = outputDenoise->isChecked();
    if (!settings.valid())
    {
        QMessageBox::warning(this, tr("输出设置"), tr("请使用有效设置；单张图最多 6710 万像素。"));
        return;
    }
    editor->renderLocked = true;
    workspace->taskSize = settings.size;
    workspace->taskTarget = settings.samples;
    workspace->taskSamples = 0;
    workspace->taskClock.start();
    workspace->task->item(0, 1)->setText(QString("%1 × %2").arg(settings.size.width()).arg(settings.size.height()));
    workspace->task->item(0, 2)->setText(QString("0 / %1 spp").arg(settings.samples));
    workspace->task->item(0, 4)->setText("0.0 秒");
    navigateWorkspace(WorkspacePage::Render);
    syncWorkspaceAvailability();
    viewport->renderThread()->startJob(settings);
}
void learnQT::loadModel()
{
    if (m_loading || editor->renderLocked)
        return;
    auto paths = QFileDialog::getOpenFileNames(this, tr("追加模型到当前场景"), QString(),
                                               tr("模型 (*.obj *.gltf *.glb *.fbx)"));
    importPaths(paths);
}
void learnQT::importPaths(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    navigateWorkspace(WorkspacePage::Scene);
    editor->importFiles(paths);
}
void learnQT::beginSceneLoad(const QString &path, bool model)
{
    if (m_loading || editor->renderLocked)
        return;
    if (model)
    {
        importPaths({path});
        return;
    }
    setLoading(true);
    auto result = std::make_shared<std::shared_ptr<Scene>>();
    auto error = std::make_shared<QString>();
    m_loadWorker =
        QThread::create([path, result, error] { *result = Scene::prepareScene(path, false, *error); });
    connect(m_loadWorker, &QThread::finished, this, [this, result, error, path] {
        m_loadWorker->deleteLater();
        m_loadWorker = nullptr;
        setLoading(false);
        if (!*result)
        {
            logMessage(*error);
            QMessageBox::critical(this, tr("加载失败"), *error);
            refreshScenes();
            return;
        }
        editor->install(**result);
        viewport->submitPrepared(*result);
        m_sceneDirty = false;
        if (!path.isEmpty() && !m_sessionScenes.contains(path))
            m_sessionScenes.append(path);
        refreshScenes();
        restoreSceneControls();
        updateTitle();
        rememberScene(path);
        navigateWorkspace(WorkspacePage::Scene);
    });
    m_loadWorker->start();
}
void learnQT::setLoading(bool loading)
{
    m_loading = loading;
    viewport->setEnabled(!loading);
    treeDock->setEnabled(!loading && !editor->renderLocked);
    inspectorDock->setEnabled(!loading && !editor->renderLocked);
    for (auto a : editActions)
        a->setEnabled(!loading && !editor->renderLocked);
    renderAction->setEnabled(!loading && !editor->renderLocked);
    undoAction->setEnabled(!loading && !editor->renderLocked && editor->undo.canUndo());
    redoAction->setEnabled(!loading && !editor->renderLocked && editor->undo.canRedo());
    taskLabel->setText(loading ? tr("准备资源…") : renderJobText(jobState));
    syncWorkspaceAvailability();
}
void learnQT::setSceneDirty()
{
    m_sceneDirty = true;
    editor->undo.resetClean();
    updateTitle();
}
void learnQT::updateTitle()
{
    setWindowTitle(tr("%1%2 · Scene Studio")
                       .arg(editor->document.root["name"].toString(), m_sceneDirty ? " *" : ""));
}
void learnQT::logMessage(const QString &message)
{
    log->appendPlainText(message);
    taskLabel->setText(message);
}
void learnQT::refreshScenes()
{
    QSignalBlocker block(m_sceneList);
    m_sceneList->clear();
    m_sceneList->addItem(tr("预设 / 打开过的场景…"), QString());
    auto directory = QDir(QString::fromStdString(getResourcePath("scenes")));
    QStringList paths = m_sessionScenes;
    for (auto entry : directory.entryList({"*.scene.json"}, QDir::Files, QDir::Name))
    {
        auto p = directory.absoluteFilePath(entry);
        if (!paths.contains(p))
            paths.append(p);
    }
    for (auto path : paths)
        m_sceneList->addItem(QFileInfo(path).completeBaseName().replace(".scene", ""), path);
    int current = m_sceneList->findData(editor->document.filePath);
    m_sceneList->setCurrentIndex(std::max(0, current));
}
void learnQT::commitPreviewSettings(const RenderParams::Snapshot &settings)
{
    if (m_restoring || editor->busy || editor->renderLocked)
        return;
    auto d = editor->document;
    d.captureSettings(settings);
    editor->submit(d, tr("预览设置"), EditorController::Display);
    syncPreviewControls(editor->document.settings());
}
// 文档设置变化后把顶栏弹出面板与详情弹窗对齐。控件信号在写入期间被屏蔽，不会回灌成新的提交。
void learnQT::syncPreviewControls(const RenderParams::Snapshot &settings)
{
    if (previewChromePanel)
        previewChromePanel->setValues(settings);
    if (previewDetailPanel)
        previewDetailPanel->setValues(settings);
}
// 不跳页的预览设置弹窗：非模态，改动即时提交，视口实时跟着变。
void learnQT::showPreviewSettingsDialog()
{
    if (!previewDialog)
    {
        previewDialog = new QDialog(this);
        previewDialog->setObjectName("previewSettingsDialog");
        previewDialog->setWindowTitle(tr("预览设置"));
        previewDialog->setModal(false);
        auto layout = new QVBoxLayout(previewDialog);
        layout->addWidget(new QLabel(tr("交互预览显示在场景页视口；改动即时生效，不会启动正式任务。")));
        layout->addWidget(previewDetailPanel);
        previewDetailPanel->show();
        layout->addStretch();
    }
    previewDetailPanel->setValues(editor->document.settings());
    previewDialog->show();
    previewDialog->raise();
    previewDialog->activateWindow();
}
void learnQT::restoreSceneControls()
{
    auto settings = editor->document.settings();
    m_restoring = true;
    auto output = RenderJobSettings::fromJson(editor->document.root["output"].toObject());
    outputWidth->setValue(output.size.width());
    outputHeight->setValue(output.size.height());
    outputSamples->setValue(output.samples);
    outputTile->setValue(output.tileSize);
    outputBounces->setValue(output.bounces);
    outputDenoise->setChecked(output.denoise);
    m_restoring = false;
    syncPreviewControls(settings);
    inspector->refresh();
}
bool learnQT::confirmDiscard()
{
    if (m_loading || editor->renderLocked)
        return false;
    if (!m_sceneDirty)
        return true;
    auto answer = QMessageBox::warning(this, tr("未保存的场景"), tr("保存当前修改？"),
                                       QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                       QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save)
        return saveSceneDocument();
    return true;
}
bool learnQT::saveSceneDocument(bool saveAs)
{
    auto path = editor->document.filePath;
    if (saveAs || path.isEmpty())
        path = QFileDialog::getSaveFileName(this, tr("保存场景"), path, tr("场景 (*.scene.json)"));
    if (path.isEmpty())
        return false;
    if (!path.endsWith(".json", Qt::CaseInsensitive))
        path += ".scene.json";
    QString error;
    if (!editor->document.saveScene(path, error))
    {
        QMessageBox::critical(this, tr("保存失败"), error);
        return false;
    }
    editor->document.root["portable"] = editor->document.root["portable"].toBool() &&
                                        editor->document.filePath == QFileInfo(path).absoluteFilePath();
    editor->document.filePath = QFileInfo(path).absoluteFilePath();
    editor->markSaved();
    m_sceneDirty = false;
    updateTitle();
    refreshScenes();
    rememberScene(path);
    return true;
}
void learnQT::exportPackage()
{
    if (m_loading || editor->renderLocked)
        return;
    auto path = QFileDialog::getExistingDirectory(this, tr("选择空文件夹"));
    if (path.isEmpty())
        return;
    auto d = editor->document;
    setLoading(true);
    auto error = std::make_shared<QString>();
    auto success = std::make_shared<bool>();
    m_loadWorker =
        QThread::create([d, path, error, success] { *success = d.exportScenePackage(path, *error); });
    connect(m_loadWorker, &QThread::finished, this, [this, error, success, path] {
        m_loadWorker->deleteLater();
        m_loadWorker = nullptr;
        setLoading(false);
        if (*success)
            logMessage(tr("已导出便携包：%1").arg(path));
        else
            QMessageBox::critical(this, tr("导出失败"), *error);
    });
    m_loadWorker->start();
}
void learnQT::saveGLImage()
{
    auto image = resultView->image;
    if (image.isNull())
        image = lastResult;
    if (image.isNull())
    {
        QMessageBox::information(this, tr("导出图片"), tr("请先运行正式渲染，生成完整采样轮次结果。"));
        return;
    }
    QString selectedFilter;
    auto path = QFileDialog::getSaveFileName(this, tr("导出渲染结果"), QString(),
                                             tr("PNG (*.png);;JPEG (*.jpg *.jpeg)"), &selectedFilter);
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += selectedFilter.startsWith("JPEG") ? ".jpg" : ".png";
    QImageWriter writer(path);
    writer.setQuality(95);
    if (!writer.write(image.convertToFormat(QImage::Format_RGB32)))
        QMessageBox::critical(this, tr("导出失败"), writer.errorString());
}
void learnQT::closeEvent(QCloseEvent *e)
{
    if (editor->renderLocked)
    {
        viewport->renderThread()->stopJob();
        e->ignore();
        taskLabel->setText(tr("正在停止任务，请稍后关闭"));
        return;
    }
    if (!confirmDiscard())
    {
        e->ignore();
        return;
    }
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "learnQT", "SceneWorkbench");
    settings.setValue("geometry", saveGeometry());
    workspace->layouts[workspace->page] = saveState(4);
    for (auto it = workspace->layouts.begin(); it != workspace->layouts.end(); ++it)
        settings.setValue(QString("workspaceV4/layout/%1").arg(it.key()), it.value());
    e->accept();
}
void learnQT::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_F11)
    {
        isFullScreen() ? showNormal() : showFullScreen();
        e->accept();
    }
    else
        QMainWindow::keyPressEvent(e);
}
void learnQT::dragEnterEvent(QDragEnterEvent *e)
{
    if (!m_loading && !editor->renderLocked && e->mimeData()->hasUrls())
        e->acceptProposedAction();
}
void learnQT::dropEvent(QDropEvent *e)
{
    QStringList paths;
    for (auto url : e->mimeData()->urls())
    {
        auto path = url.toLocalFile();
        auto ext = QFileInfo(path).suffix().toLower();
        if (QStringList{"obj", "gltf", "glb", "fbx"}.contains(ext))
            paths.append(path);
    }
    if (!paths.isEmpty())
    {
        importPaths(paths);
        e->acceptProposedAction();
    }
}
