#include "learnQT.h"
#include <QActionGroup>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageWriter>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMenuBar>
#include <QMimeData>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

learnQT::learnQT(QWidget *parent) : QMainWindow(parent)
{
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QApplication::setFont(QFont("Microsoft YaHei UI", 9));
    setFont(QApplication::font());
    setAcceptDrops(true);
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#1b222c"));
    palette.setColor(QPalette::WindowText, QColor("#cdd5e0"));
    palette.setColor(QPalette::Base, QColor("#131a23"));
    palette.setColor(QPalette::AlternateBase, QColor("#242e3a"));
    palette.setColor(QPalette::Text, QColor("#cdd5e0"));
    palette.setColor(QPalette::Button, QColor("#303d4e"));
    palette.setColor(QPalette::ButtonText, QColor("#cdd5e0"));
    palette.setColor(QPalette::Highlight, QColor("#405975"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, QColor("#303d4e"));
    palette.setColor(QPalette::ToolTipText, Qt::white);
    QApplication::setPalette(palette);
    setStyleSheet(QStringLiteral(
        "QMainWindow,QDialog{background:#171c23;color:#dce2eb;} QWidget{color:#cdd5e0;}"
        "QDockWidget{font-weight:600;}QDockWidget::title{background:#252e39;padding:8px;}"
        "QToolBar{background:#202833;border:0;spacing:5px;padding:6px;}QToolButton{padding:6px "
        "9px;border:1px solid "
        "transparent;border-radius:4px;}QToolButton:hover{background:#344052;}QToolButton:checked{background:"
        "#435a74;color:#fff;}"
        "QMenuBar,QMenu,QStatusBar{background:#202833;color:#cdd5e0;}QMenu::item:selected{background:#40546e;"
        "}"
        "QTreeView,QPlainTextEdit,QScrollArea,QTabWidget::pane{background:#1b222c;border:1px solid "
        "#303a48;}QTreeView::item{height:27px;}QTreeView::item:selected{background:#3e536c;}QTreeView::item:"
        "hover{background:#303c4c;}"
        "QHeaderView::section{background:#252f3b;color:#99a8bc;border:0;padding:6px;}"
        "QLineEdit,QAbstractSpinBox,QComboBox{background:#131a23;border:1px solid "
        "#3a4657;border-radius:4px;padding:5px;color:#e3e8ef;selection-background-color:#4a6582;}QLineEdit:"
        "focus,QAbstractSpinBox:focus{border-color:#7193bd;}"
        "QPushButton{background:#303d4e;border:1px solid "
        "#47596e;border-radius:4px;padding:6px;}QPushButton:hover{background:#40536b;}QPushButton:pressed{"
        "background:#243144;}"
        "QWidget:disabled{color:#646f7e;}QGroupBox{border:1px solid "
        "#344051;border-radius:5px;margin-top:12px;padding-top:8px;}QGroupBox::title{subcontrol-origin:"
        "margin;left:10px;color:#93a7c1;}"
        "QTabBar::tab{background:#242e3a;color:#9cadc1;padding:9px "
        "14px;}QTabBar::tab:selected{background:#35465b;color:#f4f6fa;}"
        "QScrollBar:vertical{background:#1b222c;width:10px;}QScrollBar::handle:vertical{background:#4a5666;"
        "min-height:30px;border-radius:4px;}QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{"
        "height:0;}"
        "QProgressBar{border:0;background:#131a23;color:#ced7e5;text-align:center;max-height:14px;}"
        "QProgressBar::chunk{background:#5484a8;}"));
    editor = new EditorController(this);
    editor->install(Scene::getInstance());
    viewport = new GLWidget;
    viewport->attachEditor(editor);
    setupWorkbench();
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
    QSettings settings("learnQT", "SceneWorkbench");
    if (!restoreGeometry(settings.value("geometry").toByteArray()))
        resize(1366, 850);
    restoreState(settings.value("docks").toByteArray(), 2);
    refreshScenes();
    restoreSceneControls();
    updateTitle();
    configureRegressionCapture();
#ifdef SCENE_TESTING
    configureSceneRegression();
    configureWorkbenchRegression();
    configureInteractionRegression();
    configurePreviewRegression();
    configurePreviewModeRegression();
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
    views->addTab(viewport, tr("编辑视口"));
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
    auto file = menuBar()->addMenu(tr("场景"));
    auto toolbar = addToolBar(tr("工作台"));
    toolbar->setObjectName("workbenchToolbar");
    toolbar->setMovable(false);
    auto action = [&](const QString &name, const QKeySequence &shortcut, std::function<void()> fn,
                      bool visible = true) {
        auto a = file->addAction(name, this, fn);
        a->setShortcut(shortcut);
        editActions.append(a);
        if (visible)
            toolbar->addAction(a);
        return a;
    };
    action(tr("新建"), QKeySequence::New, [this] {
        if (confirmDiscard())
            beginSceneLoad(QString());
    });
    action(tr("打开…"), QKeySequence::Open, [this] {
        if (!confirmDiscard())
            return;
        auto path = QFileDialog::getOpenFileName(this, tr("打开场景"),
                                                 QString::fromStdString(getResourcePath("scenes")),
                                                 tr("场景 (*.scene.json *.json)"));
        if (!path.isEmpty())
            beginSceneLoad(path);
    });
    action(tr("保存"), QKeySequence::Save, [this] { saveSceneDocument(); });
    action(tr("另存为…"), QKeySequence::SaveAs, [this] { saveSceneDocument(true); }, false);
    action(tr("导入…"), QKeySequence("Ctrl+I"), [this] { loadModel(); });
    action(
        tr("导入并指定缩放…"), {},
        [this] {
            auto paths = QFileDialog::getOpenFileNames(this, tr("导入模型"), QString(),
                                                       tr("模型 (*.obj *.gltf *.glb *.fbx)"));
            if (paths.isEmpty())
                return;
            bool ok = false;
            double scale = QInputDialog::getDouble(this, tr("导入缩放"), tr("统一缩放倍数（1 保留源尺寸）"),
                                                   1, .000001, 1000000, 6, &ok);
            if (ok)
                editor->importFiles(paths, scale);
        },
        false);
    action(tr("导出便携包…"), {}, [this] { exportPackage(); }, false);
    toolbar->addSeparator();
    undoAction = editor->undo.createUndoAction(this, tr("撤销"));
    undoAction->setShortcut(QKeySequence::Undo);
    redoAction = editor->undo.createRedoAction(this, tr("重做"));
    redoAction->setShortcut(QKeySequence::Redo);
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
        auto a = toolbar->addAction(labels[i]);
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
    connect(space, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        viewport->setLocalAxes(i == 1);
    });
    auto snapAction = toolbar->addAction(tr("吸附"));
    snapAction->setCheckable(true);
    connect(snapAction, &QAction::toggled, this, [this](bool enabled) { viewport->snap = enabled; });
    editActions.append(snapAction);
    toolbar->addSeparator();
    renderAction = toolbar->addAction(tr("正式渲染"), this, &learnQT::startRender);
    renderAction->setShortcut(QKeySequence("F12"));
    pauseAction = toolbar->addAction(tr("暂停"), this, [this] {
        if (viewport->renderThread())
            viewport->renderThread()->pauseJob(jobState != RenderJobState::Paused);
    });
    stopAction = toolbar->addAction(tr("停止"), this, [this] {
        if (viewport->renderThread())
            viewport->renderThread()->stopJob();
    });
    pauseAction->setEnabled(false);
    stopAction->setEnabled(false);
    setupTree();
    inspectorDock = new QDockWidget(tr("属性与设置"), this);
    inspectorDock->setObjectName("inspectorDock");
    auto tabs = new QTabWidget;
    auto scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    inspector = new ObjectInspector(editor);
    scroll->setWidget(inspector);
    tabs->addTab(scroll, tr("对象 / 材质"));
    tabs->addTab(createSettings(), tr("渲染 / 输出"));
    auto lightsScroll = new QScrollArea;
    lightsScroll->setWidgetResizable(true);
    lightsScroll->setWidget(new LightInspector(editor));
    tabs->addTab(lightsScroll, tr("灯光"));
    inspectorDock->setWidget(tabs);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);
    splitDockWidget(treeDock, inspectorDock, Qt::Vertical);
    performanceDock = new QDockWidget(tr("性能"), this);
    performanceDock->setObjectName("performanceDock");
    performance = new PerformancePanel;
    performanceDock->setWidget(performance);
    addDockWidget(Qt::RightDockWidgetArea, performanceDock);
    splitDockWidget(inspectorDock, performanceDock, Qt::Vertical);
    logDock = new QDockWidget(tr("任务日志"), this);
    logDock->setObjectName("logDock");
    log = new QPlainTextEdit;
    log->setReadOnly(true);
    log->setMaximumBlockCount(1000);
    logDock->setWidget(log);
    addDockWidget(Qt::BottomDockWidgetArea, logDock);
    logDock->hide();
    auto viewMenu = menuBar()->addMenu(tr("视图"));
    for (auto dock : {treeDock, inspectorDock, performanceDock, logDock})
        viewMenu->addAction(dock->toggleViewAction());
    auto layout = saveState(2);
    viewMenu->addAction(tr("恢复默认布局"), this, [this, layout] { restoreState(layout, 2); });
    auto help = menuBar()->addMenu(tr("帮助"));
    help->addAction(tr("操作说明"), this, [this] {
        QMessageBox::information(
            this, tr("工作台快捷键"),
            tr("左键选择 · Ctrl 追加 / 切换 · 树中 Shift 范围选择\nAlt + 左键环绕 · 中键平移 · 滚轮缩放\nQ "
               "选择 · W 移动 · E 旋转 · R 缩放 · F 定位\nEsc 取消变换 · Delete 删除 · Ctrl+D "
               "复制\n\n组仅组织对象，换组不改变世界变换。\n正式渲染期间锁定编辑，完成后可导出 PNG / "
               "JPEG。"));
    });
    taskLabel = new QLabel(tr("编辑预览"));
    progress = new QProgressBar;
    progress->setFixedWidth(145);
    progress->setRange(0, 100);
    statsLabel = new QLabel;
    statusBar()->addWidget(taskLabel, 1);
    statusBar()->addPermanentWidget(progress);
    statusBar()->addPermanentWidget(statsLabel);
    auto logs = new QPushButton(tr("日志"));
    logs->setFlat(true);
    connect(logs, &QPushButton::clicked, this, [this] { logDock->setVisible(!logDock->isVisible()); });
    statusBar()->addPermanentWidget(logs);
    treeDock->setMinimumHeight(210);
    resizeDocks({treeDock, inspectorDock, performanceDock}, {240, 300, 145}, Qt::Vertical);
    resizeDocks({treeDock}, {360}, Qt::Horizontal);
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
    tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree->setDragDropMode(QAbstractItemView::InternalMove);
    tree->setDefaultDropAction(Qt::MoveAction);
    tree->setDragEnabled(true);
    tree->setAcceptDrops(true);
    tree->setDropIndicatorShown(true);
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 3; ++i)
    {
        tree->header()->setSectionResizeMode(i, QHeaderView::Fixed);
        tree->setColumnWidth(i, 43);
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
                editor->node(id)["visible"].toBool(true) ? tr("隐藏") : tr("显示"), this,
                [this, id] { editor->setFlag(id, "visible", !editor->node(id)["visible"].toBool(true)); });
            menu.addAction(editor->node(id)["locked"].toBool() ? tr("解锁") : tr("锁定"), this, [this, id] {
                editor->setFlag(id, "locked", !editor->node(id)["locked"].toBool());
            });
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
    auto form = new QFormLayout;
    layout->addLayout(form);
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
    m_denoise = new QCheckBox(tr("预览降噪"));
    form->addRow(m_denoise);
    connect(m_denoise, &QCheckBox::toggled, this, [this](bool b) {
        if (m_restoring)
            return;
        auto d = editor->document;
        auto settings = d.root["render"].toObject();
        settings["denoise"] = b;
        d.root["render"] = settings;
        editor->submit(d, tr("预览降噪"), EditorController::Display);
    });
    previewSamples = spin(tr("预览 spp（0 无限）"), 0, 1000000, 0);
    previewBounces = spin(tr("预览反弹数"), 1, 64, 4);
    previewTile = spin(tr("预览块大小"), 16, 1024, 128);
    previewTiled = new QCheckBox(tr("分块预览"));
    previewLow = new QCheckBox(tr("降低预览分辨率"));
    form->addRow(previewTiled);
    form->addRow(previewLow);
    auto commitPreview = [this] {
        if (m_restoring)
            return;
        auto d = editor->document;
        auto settings = d.settings();
        settings.maxRenderFrames = previewSamples->value();
        settings.maxBounces = previewBounces->value();
        settings.tileSize = previewTile->value();
        settings.useTileRendering = previewTiled->isChecked();
        settings.renderLow = previewLow->isChecked();
        d.captureSettings(settings);
        editor->submit(d, tr("预览设置"), EditorController::Display);
    };
    for (auto s : {previewSamples, previewBounces, previewTile})
        connect(s, &QSpinBox::editingFinished, this, commitPreview);
    for (auto s : {previewTiled, previewLow})
        connect(s, &QCheckBox::toggled, this, [commitPreview] { commitPreview(); });
    auto exposure = new MixedSpin;
    exposure->setRange(-16, 16);
    exposure->setObjectName("exposure");
    form->addRow(tr("曝光 EV"), exposure);
    auto tonemap = new QComboBox;
    tonemap->addItems({tr("旧曲线"), tr("ACES 近似"), tr("线性裁切")});
    form->addRow(tr("色调映射"), tonemap);
    auto envIntensity = new MixedSpin;
    envIntensity->setRange(0, 10000);
    form->addRow(tr("环境强度"), envIntensity);
    auto envRotation = new MixedSpin;
    envRotation->setRange(-360, 360);
    form->addRow(tr("环境旋转 °"), envRotation);
    auto hdr = new QPushButton(tr("选择 HDR…"));
    form->addRow(tr("环境贴图"), hdr);
    connect(hdr, &QPushButton::clicked, this, [this] {
        auto path = QFileDialog::getOpenFileName(this, tr("选择环境"), QString(), tr("Radiance (*.hdr)"));
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
    connect(envIntensity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [property](double v) { property("environment", "intensity", v, EditorController::Lighting); });
    connect(envRotation, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
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
    connect(editor, &EditorController::changed, this, [restoreEnvironment](int) { restoreEnvironment(); });
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
    connect(thread, &RenderThread::statsReady, this, [this](RenderStats s) {
        performance->append(s);
        statsLabel->setText(tr("%1 × %2  |  %3 spp  |  已选 %4")
                                .arg(s.size.width())
                                .arg(s.size.height())
                                .arg(s.samples)
                                .arg(editor->selectedModels().size()) +
                            tr("  |  %1 秒").arg(s.jobSeconds, 0, 'f', 1));
        progress->setValue(s.target > 0 ? std::min(100, int(100. * s.samples / s.target)) : 0);
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
                bool active = state == RenderJobState::Preparing || state == RenderJobState::Rendering ||
                              state == RenderJobState::Paused || state == RenderJobState::Denoising;
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
                if (state == RenderJobState::Failed && !lastResult.isNull())
                    resultView->setImage(lastResult);
                if (!message.isEmpty())
                    logMessage(message);
            });
}
void learnQT::startRender()
{
    if (m_loading || editor->renderLocked || !viewport->renderThread())
        return;
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
    views->setCurrentIndex(1);
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
        views->setCurrentIndex(0);
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
}
void learnQT::setSceneDirty()
{
    m_sceneDirty = true;
    editor->undo.resetClean();
    updateTitle();
}
void learnQT::updateTitle()
{
    setWindowTitle(
        tr("%1%2 · Scene Studio").arg(editor->document.root["name"].toString(), m_sceneDirty ? " *" : ""));
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
void learnQT::restoreSceneControls()
{
    m_restoring = true;
    auto output = RenderJobSettings::fromJson(editor->document.root["output"].toObject());
    outputWidth->setValue(output.size.width());
    outputHeight->setValue(output.size.height());
    outputSamples->setValue(output.samples);
    outputTile->setValue(output.tileSize);
    outputBounces->setValue(output.bounces);
    outputDenoise->setChecked(output.denoise);
    m_denoise->setChecked(editor->document.settings().denoise);
    auto preview = editor->document.settings();
    previewSamples->setValue(preview.maxRenderFrames);
    previewBounces->setValue(preview.maxBounces);
    previewTile->setValue(preview.tileSize);
    previewTiled->setChecked(preview.useTileRendering);
    previewLow->setChecked(preview.renderLow);
    m_restoring = false;
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
    QSettings settings("learnQT", "SceneWorkbench");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("docks", saveState(2));
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
