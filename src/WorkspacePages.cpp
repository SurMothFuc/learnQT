#include "WorkbenchStyle.h"
#include "UiDiagnostics.h"
#include "WorkspaceUi.h"
#include "learnQT.h"
#include <QActionGroup>
#include <QApplication>
#include <QDirIterator>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QImageReader>
#include <QInputDialog>
#include <QJsonDocument>
#include <QListView>
#include <QMenu>
#include <QMenuBar>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <cmath>

namespace
{
enum ResourceRole
{
    KindRole = Qt::UserRole + 1,
    SourceRole,
    IdRole,
    ReferencesRole
};
QVBoxLayout *column(QWidget *widget)
{
    auto layout = new QVBoxLayout(widget);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    return layout;
}
QLabel *label(const QString &text, const QString &name = "muted")
{
    auto w = new QLabel(text);
    w->setObjectName(name);
    w->setWordWrap(true);
    return w;
}
QWidget *scrolling(QWidget *content)
{
    auto scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    return scroll;
}
QGroupBox *group(const QString &title)
{
    auto g = new QGroupBox(title);
    column(g);
    return g;
}
QWidget *unavailable(const QString &title, const QString &description, const QStringList &controls = {})
{
    auto box = new QWidget;
    auto layout = column(box);
    layout->setContentsMargins(0, 4, 0, 4);
    box->setProperty("unavailable", true);
    auto toggle = new QToolButton;
    toggle->setText("功能边界 · " + title);
    toggle->setCheckable(true);
    toggle->setArrowType(Qt::RightArrow);
    toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto content = label(description + (controls.isEmpty() ? "" : "\n尚未开放：" + controls.join("、")));
    content->hide();
    layout->addWidget(toggle);
    layout->addWidget(content);
    QObject::connect(toggle, &QToolButton::toggled, box, [toggle, content](bool open) {
        content->setVisible(open);
        toggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });
    return box;
}
QListWidget *cards(const QString &name, int size = 86)
{
    auto list = new QListWidget;
    list->setObjectName(name);
    list->setViewMode(QListView::IconMode);
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setIconSize(QSize(size, size));
    list->setGridSize(QSize(size + 64, size + 52));
    list->setSpacing(6);
    list->setWordWrap(true);
    return list;
}
class ResourceFilter : public QSortFilterProxyModel
{
  public:
    QString kind, search;
    using QSortFilterProxyModel::QSortFilterProxyModel;
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        auto i = sourceModel()->index(row, 0, parent);
        return (kind.isEmpty() || i.data(KindRole).toString() == kind) &&
               (i.data().toString().contains(search, Qt::CaseInsensitive) ||
                i.data(SourceRole).toString().contains(search, Qt::CaseInsensitive));
    }
    void refresh()
    {
        invalidateFilter();
    }
};
QWidget *resourceBrowser(QStandardItemModel *model, const QString &initialKind,
                         std::function<void(QModelIndex)> activate, const QString &name, bool compact = false)
{
    auto panel = new QWidget;
    auto layout = column(panel);
    auto row = new QHBoxLayout;
    auto kind = new QComboBox;
    kind->addItem("全部资源", "");
    for (const auto &v : QStringList{"场景", "模型", "材质", "纹理", "HDR"})
        kind->addItem(v, v);
    if (!initialKind.isEmpty())
        kind->setCurrentIndex(kind->findData(initialKind));
    auto search = new QLineEdit;
    search->setObjectName(name + "Search");
    search->setPlaceholderText("搜索资源名称或路径…");
    search->setClearButtonEnabled(true);
    auto filters = new QWidget;
    auto filterLayout = new QVBoxLayout(filters);
    filterLayout->setContentsMargins(0, 0, 0, 0);
    filterLayout->addWidget(kind);
    filterLayout->addWidget(search);
    if (compact) {
        row->addWidget(kind);
        row->addWidget(search, 1);
        layout->addLayout(row);
        delete filters;
        if (initialKind == "HDR") kind->hide();
    }
    auto proxy = new ResourceFilter(panel);
    proxy->setSourceModel(model);
    proxy->kind = initialKind;
    auto list = new QListView;
    list->setObjectName(name);
    list->setModel(proxy);
    list->setViewMode(QListView::IconMode);
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setIconSize(QSize(compact ? 48 : 80, compact ? 48 : 80));
    list->setGridSize(QSize(compact ? 125 : 170, compact ? 92 : 132));
    list->setWordWrap(true);
    list->setSpacing(4);
    auto detail = label("选择资源查看来源；双击执行对应操作。");
    detail->setTextFormat(Qt::PlainText);
    detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto action = new QPushButton("选择资源");
    action->setProperty("resourceAction", true);
    action->setEnabled(false);
    auto footer = new QHBoxLayout;
    if (compact)
    {
        detail->hide();
        footer->addStretch();
    }
    footer->addWidget(action);
    if (compact) {
        layout->addWidget(list, 1);
        layout->addLayout(footer);
    } else {
        filters->setMaximumWidth(220);
        filterLayout->addStretch();
        auto splitter = new QSplitter;
        auto information = new QWidget;
        information->setMaximumWidth(300);
        auto infoLayout = column(information);
        infoLayout->addWidget(label("资源来源与引用", "sectionHeading"));
        infoLayout->addWidget(detail);
        infoLayout->addLayout(footer);
        infoLayout->addStretch();
        splitter->addWidget(filters);
        splitter->addWidget(list);
        splitter->addWidget(information);
        splitter->setStretchFactor(1, 1);
        splitter->setSizes({200, 700, 260});
        layout->addWidget(splitter, 1);
    }
    QObject::connect(kind, QOverload<int>::of(&QComboBox::currentIndexChanged), panel, [proxy, kind] {
        proxy->kind = kind->currentData().toString();
        proxy->refresh();
    });
    QObject::connect(search, &QLineEdit::textChanged, panel, [proxy](const QString &s) {
        proxy->search = s;
        proxy->refresh();
    });
    QObject::connect(
        list->selectionModel(), &QItemSelectionModel::currentChanged, panel,
        [detail, action, panel](const QModelIndex &i) {
            QString kind = i.data(KindRole).toString();
            detail->setText(i.isValid() ? kind + " · " + i.data().toString() + "\n\n来源路径：\n" +
                                        (i.data(SourceRole).toString().isEmpty() ? "场景文档内定义" : i.data(SourceRole).toString()) +
                                        "\n\n引用：\n" + i.data(ReferencesRole).toString()
                                        : "没有选中的资源");
            bool mutates = kind == "场景" || kind == "模型" || kind == "HDR";
            action->setProperty("requiresEditing", mutates);
            action->setProperty("resourceSelected", i.isValid());
            action->setEnabled(i.isValid() &&
                               (!mutates || panel->window()->property("workspaceEditable").toBool()));
            action->setText(kind == "场景"   ? "打开场景"
                            : kind == "模型" ? "追加导入"
                            : kind == "HDR"  ? "应用环境"
                                             : "定位使用者");
        });
    QObject::connect(action, &QPushButton::clicked, panel,
                     [activate, list] { activate(list->currentIndex()); });
    QObject::connect(list, &QListView::doubleClicked, panel, activate);
    return panel;
}
void addCard(QListWidget *list, const QString &title, const QString &icon, const QString &data)
{
    auto item = new QListWidgetItem(WorkbenchStyle::icon(icon), title, list);
    item->setData(Qt::UserRole, data);
    item->setToolTip(data.isEmpty() ? title : data);
}
} // namespace

void learnQT::setupWorkspace()
{
    workspace = std::make_shared<WorkspaceUi>();
    auto &w = *workspace;
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "learnQT", "SceneWorkbench");
    w.welcome = settings.value("workspaceV4/welcome", true).toBool();
    w.tips = settings.value("workspaceV4/tips", true).toBool();
    w.status = settings.value("workspaceV4/status", true).toBool();
    w.recentLimit = qBound(1, settings.value("workspaceV4/recentLimit", 12).toInt(), 50);
    w.recent = settings.value("workspaceV4/recent").toStringList().mid(0, w.recentLimit);
    m_renderPreviewMode = settings.value("workspaceV5/renderComposition", true).toBool();
    for (const auto &page : workspacePages())
        for (bool composition : {true, false}) {
            const auto key = workspaceLayoutKey(page.id, composition);
            const auto state = settings.value("workspaceV5/layout/" + key).toByteArray();
            if (!state.isEmpty()) w.layouts[key] = state;
        }
    qApp->installEventFilter(this);
    statusBar()->setVisible(w.status);
    views->tabBar()->hide();
    views->setDocumentMode(true);
    // Hidden full-page browsers must not impose their minimum height on the
    // editor viewport above a bottom panel. Each visible page handles scrolling.
    views->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    views->setMinimumHeight(240);
    w.catalog = new QStandardItemModel(this);
    QDirIterator hdrIterator(QString::fromStdString(getResourcePath("")), {"*.hdr"}, QDir::Files,
                             QDirIterator::Subdirectories);
    while (hdrIterator.hasNext())
        w.hdrFiles.append(hdrIterator.next());

    // Extract existing controls once; the OpenGL viewport is never reparented.
    auto oldTabs = qobject_cast<QTabWidget *>(inspectorDock->widget());
    auto settingsRoot = oldTabs->widget(1);
    auto objectScroll = oldTabs->widget(0);
    auto lightScroll = oldTabs->widget(2);
    auto settingsSection = [settingsRoot](const char *name) {
        return settingsRoot->findChild<QGroupBox *>(name);
    };
    w.rightStack = new QStackedWidget;
    objectScroll->setParent(w.rightStack);
    w.rightStack->addWidget(objectScroll);
    w.rightPages[1] = objectScroll;
    w.rightPages[2] = objectScroll;
    auto lightContent = qobject_cast<QScrollArea *>(lightScroll)->widget();
    lightContent->layout()->addWidget(
        unavailable("其他光源", "保留专用光源和预设入口。", {"矩形灯 / 聚光灯 / IES", "色温 / 灯光预设"}));
    lightScroll->setParent(w.rightStack);
    w.rightStack->addWidget(lightScroll);
    w.rightPages[3] = lightScroll;
    w.lightProperties = lightScroll;

    auto renderPanel = new QWidget;
    auto renderLayout = column(renderPanel);
    renderLayout->addWidget(label("路径追踪 · OpenGL 3.3\n当前上下文设备；降噪可选 GPU 实时或 CPU OIDN。"));
    auto framing = group("构图相机与画幅");
    auto framingForm = new QFormLayout;
    m_renderCameraChoice = new QComboBox;
    m_renderCameraChoice->setObjectName("renderCameraChoice");
    framingForm->addRow("来源相机", m_renderCameraChoice);
    m_aspectChoice = new QComboBox;
    m_aspectChoice->setObjectName("renderAspectChoice");
    m_aspectChoice->addItem("自定义", 0.0);
    for (const QPair<QString, double> &aspect :
         {QPair<QString, double>{"16:9", 16.0 / 9}, {"4:3", 4.0 / 3},
          {"1:1", 1.0}, {"9:16", 9.0 / 16}})
        m_aspectChoice->addItem(aspect.first, aspect.second);
    m_aspectChoice->setCurrentIndex(1);
    framingForm->addRow("画面比例", m_aspectChoice);
    static_cast<QVBoxLayout *>(framing->layout())->addLayout(framingForm);
    auto framingActions = new QHBoxLayout;
    auto compositionButton = new QPushButton("调整构图");
    auto resultButton = new QPushButton("查看结果");
    w.compositionMode = compositionButton;
    w.resultsMode = resultButton;
    auto saveCameraButton = new QPushButton("保存为新相机");
    saveCameraButton->setObjectName("saveCompositionCamera");
    for (auto button : {saveCameraButton})
        framingActions->addWidget(button);
    static_cast<QVBoxLayout *>(framing->layout())->addWidget(
        label("来源相机用于建立临时构图草稿；草稿不覆盖来源相机。加入队列后使用提交时的快照。"));
    static_cast<QVBoxLayout *>(framing->layout())->addLayout(framingActions);
    renderLayout->addWidget(framing);
    connect(m_renderCameraChoice, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                if (index < 0)
                    return;
                const auto id = m_renderCameraChoice->itemData(index).toString();
                for (auto value : editor->document.root["cameras"].toArray())
                    if (value.toObject()["id"].toString() == id)
                    {
                        m_draftSourceId = id;
                        m_draftCamera = value.toObject();
                        if (workspace && workspace->page == int(WorkspacePage::Render))
                            setRenderPreviewMode(true);
                        break;
                    }
            });
    connect(m_aspectChoice, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                if (index <= 0)
                    return;
                const double aspect = m_aspectChoice->itemData(index).toDouble();
                outputHeight->setValue(qMax(16, qRound(outputWidth->value() / aspect)));
                commitOutputSettings();
            });
    connect(outputWidth, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] {
        const double aspect = m_aspectChoice->currentData().toDouble();
        if (aspect > 0)
            outputHeight->setValue(qMax(16, qRound(outputWidth->value() / aspect)));
        viewport->setCompositionAspect(QSize(outputWidth->value(), outputHeight->value()));
    });
    connect(outputHeight, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] {
        const double aspect = m_aspectChoice->currentData().toDouble();
        if (aspect > 0 &&
            qAbs(outputHeight->value() - qRound(outputWidth->value() / aspect)) > 1)
        {
            const QSignalBlocker block(m_aspectChoice);
            m_aspectChoice->setCurrentIndex(0);
        }
        viewport->setCompositionAspect(QSize(outputWidth->value(), outputHeight->value()));
    });
    connect(compositionButton, &QPushButton::clicked, this, [this] { setRenderPreviewMode(true); });
    connect(resultButton, &QPushButton::clicked, this, [this] { setRenderPreviewMode(false); });
    connect(saveCameraButton, &QPushButton::clicked, this, &learnQT::saveDraftCamera);
    w.mutationWidgets << saveCameraButton << m_renderCameraChoice << m_aspectChoice;
    connect(editor, &EditorController::changed, this, [this](int change) {
        if (change == EditorController::CameraChange || change == EditorController::Topology ||
            change == EditorController::Organization)
        {
            if (change == EditorController::CameraChange && workspace &&
                workspace->page != int(WorkspacePage::Render) &&
                m_draftSourceId == editor->document.root["activeCameraId"].toString())
                m_draftCamera = {};
            refreshRenderCameras();
        }
    });
    refreshRenderCameras();
    // 交互预览设置已移出本页：入口在顶栏“预览设置”的弹出面板与详情弹窗。
    for (auto key : {"outputSection", "displaySection"})
        renderLayout->addWidget(settingsSection(key));
    auto formatRow = new QHBoxLayout;
    formatRow->addWidget(new QLabel("自动导出格式"));
    m_renderFormat = new QComboBox;
    m_renderFormat->addItems({"PNG", "JPEG"});
    formatRow->addWidget(m_renderFormat, 1);
    renderLayout->addLayout(formatRow);
    auto submit = new QToolButton;
    submit->setObjectName("renderPrimary");
    submit->setDefaultAction(renderAction);
    submit->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    renderLayout->addWidget(submit);
    auto outputDirectory = label("");
    outputDirectory->setObjectName("renderOutputDirectory");
    renderLayout->addWidget(outputDirectory);
    w.refreshers << [outputDirectory] {
        QSettings s(QSettings::defaultFormat(), QSettings::UserScope, "learnQT", "SceneWorkbench");
        const auto path = s.value("workspaceV4/autoExportPath").toString();
        outputDirectory->setText("自动导出目录：\n" + (path.isEmpty() ? "尚未设置" : path));
    };
    auto configureDirectory = new QPushButton("设置默认导出目录…");
    renderLayout->addWidget(configureDirectory);
    connect(configureDirectory, &QPushButton::clicked, this, [this] {
        navigateWorkspace(WorkspacePage::Settings);
        findChild<QListWidget *>("preferenceCategories")->setCurrentRow(4);
    });
    renderLayout->addWidget(unavailable("渲染扩展", "当前支持单张、不透明的 PNG / JPEG 输出。",
                                        {"AOV / EXR / 动画", "自适应采样 / 渲染器切换"}));
    renderLayout->addStretch();
    w.rightPages[6] = scrolling(renderPanel);
    w.rightStack->addWidget(w.rightPages[6]);
    w.outputProperties = w.rightPages[6];
    auto resultProperties = new QWidget;
    auto propertiesLayout = column(resultProperties);
    propertiesLayout->addWidget(label("任务快照 · 只读", "sectionHeading"));
    w.taskProperties = label("选择任务查看输出信息。");
    w.taskProperties->setObjectName("renderTaskProperties");
    w.taskProperties->setTextFormat(Qt::PlainText);
    w.taskProperties->setTextInteractionFlags(Qt::TextSelectableByMouse);
    propertiesLayout->addWidget(w.taskProperties);
    propertiesLayout->addStretch();
    w.resultProperties = scrolling(resultProperties);
    w.rightStack->addWidget(w.resultProperties);
    w.mutationWidgets << renderPanel;
    // 视口顶部 chrome 条的预览弹层也是场景变更入口，正式任务期间与右栏一起禁用。
    if (previewChromePanel)
        w.mutationWidgets << previewChromePanel;
    if (previewDetailPanel)
        w.mutationWidgets << previewDetailPanel;

    auto environment = new QWidget;
    auto envLayout = column(environment);
    auto envEnabled = new QCheckBox("启用 HDR 环境照明");
    envEnabled->setObjectName("environmentEnabled");
    envLayout->addWidget(envEnabled);
    connect(envEnabled, &QCheckBox::toggled, this, [this](bool on) {
        if (workspace->refreshing || m_loading || editor->renderLocked)
            return;
        auto d = editor->document;
        auto s = d.settings();
        s.useEnvironmentMap = on;
        d.captureSettings(s);
        editor->submit(d, tr("环境照明开关"), EditorController::Lighting);
    });
    w.refreshers << [this, envEnabled] {
        QSignalBlocker b(envEnabled);
        envEnabled->setChecked(editor->document.settings().useEnvironmentMap);
    };
    envLayout->addWidget(settingsSection("environmentSection"));
    envLayout->addWidget(unavailable("天空与大气", "当前照明来自 HDR 和场景灯光。",
                                     {"物理天空 / 云 / 雾", "独立背景 / 背景可见性"}));
    envLayout->addStretch();
    w.rightPages[5] = scrolling(environment);
    w.rightStack->addWidget(w.rightPages[5]);
    w.environmentProperties = w.rightPages.take(5);
    w.mutationWidgets << environment;

    auto cameraPanel = new QWidget;
    auto cameraLayout = column(cameraPanel);
    w.cameraName = label("当前编辑相机", "sectionHeading");
    cameraLayout->addWidget(w.cameraName);
    auto basic = new QGroupBox("当前编辑相机 · 针孔透视");
    auto cameraForm = new QFormLayout(basic);
    QVector<MixedSpin *> cameraValues;
    for (int i = 0; i < 7; ++i)
    {
        auto spin = new MixedSpin;
        spin->setObjectName(QString("cameraValue%1").arg(i));
        if (i == 6)
            spin->setRange(1, 175);
        cameraForm->addRow(
            QStringList{"位置 X", "位置 Y", "位置 Z", "目标 X", "目标 Y", "目标 Z", "垂直视场角 °"}[i], spin);
        cameraValues << spin;
    }
    w.refreshCamera = [this, cameraValues] {
        const auto active = editor->document.root["activeCameraId"].toString();
        for (const auto value : editor->document.root["cameras"].toArray()) {
            const auto camera = value.toObject();
            if (camera["id"].toString() == active)
                workspace->cameraName->setText("当前编辑相机 · " + camera["name"].toString());
        }
        for (int i = 0; i < 7; ++i)
            cameraValues[i]->showValue(i < 3   ? viewport->camera.position[i]
                                       : i < 6 ? viewport->camera.target[i - 3]
                                               : viewport->camera.zoom);
    };
    for (auto spin : cameraValues)
        connect(spin, &QDoubleSpinBox::editingFinished, this, [this, cameraValues] {
            if (m_loading || editor->renderLocked)
                return;
            QVector3D position, target;
            for (int i = 0; i < 3; ++i)
            {
                position[i] = cameraValues[i]->value();
                target[i] = cameraValues[i + 3]->value();
            }
            auto up = viewport->camera.up;
            if ((position - target).lengthSquared() < 1e-10 ||
                QVector3D::crossProduct(target - position, up).lengthSquared() < 1e-12)
            {
                taskLabel->setText("相机位置与目标必须不同，观察方向不能平行于上方向。");
                workspace->refreshCamera();
                return;
            }
            Camera c = viewport->camera;
            c.restoreState(position, target, up, cameraValues[6]->value());
            ++editor->cameraCommand;
            editor->setCamera(c);
        });
    cameraLayout->addWidget(basic);
    auto frame = new QPushButton("定位所选对象 · F");
    connect(frame, &QPushButton::clicked, viewport, &GLWidget::frameSelection);
    cameraLayout->addWidget(frame);
    cameraLayout->addWidget(unavailable("镜头与构图", "可在左侧保存并切换多个相机视角。",
                                        {"焦距 / 传感器 / 景深", "运动 / 构图辅助"}));
    cameraLayout->addStretch();
    w.rightPages[4] = scrolling(cameraPanel);
    w.rightStack->addWidget(w.rightPages[4]);
    w.mutationWidgets << cameraPanel;

    // App preferences are deliberately separate from document settings.
    auto preferences = new QWidget;
    auto prefLayout = column(preferences);
    prefLayout->addWidget(label("首选项", "pageHeading"));
    auto prefSplit = new QSplitter;
    auto prefCategories = new QListWidget;
    prefCategories->setObjectName("preferenceCategories");
    prefCategories->addItems(
        {"常规", "界面", "视口与快捷键", "性能", "路径", "自动保存", "插件", "语言与主题"});
    prefCategories->setMaximumWidth(200);
    prefSplit->addWidget(prefCategories);
    auto prefContent = new QWidget;
    auto prefColumn = column(prefContent);
    QList<QWidget *> prefSections;
    auto general = group("常规");
    auto welcome = new QCheckBox("启动时显示欢迎首页");
    welcome->setChecked(w.welcome);
    welcome->setObjectName("welcomePreference");
    general->layout()->addWidget(welcome);
    auto limit = new QSpinBox;
    limit->setRange(1, 50);
    limit->setValue(w.recentLimit);
    auto recentForm = new QFormLayout;
    limit->setObjectName("recentFileLimit");
    recentForm->addRow("最近文件数量", limit);
    static_cast<QVBoxLayout *>(general->layout())->addLayout(recentForm);
    general->layout()->addWidget(label("项目内容仍由场景文件保存；这里的偏好只保存在本机。"));
    prefSections << general;
    auto appearance = group("界面");
    auto tips = new QCheckBox("显示工具提示");
    tips->setChecked(w.tips);
    auto status = new QCheckBox("显示状态栏");
    status->setChecked(w.status);
    appearance->layout()->addWidget(tips);
    appearance->layout()->addWidget(status);
    auto reset = new QPushButton("恢复所有页面默认布局");
    appearance->layout()->addWidget(reset);
    prefSections << appearance;
    auto shortcuts = group("视口与快捷键");
    auto snap = settingsSection("snapSection");
    shortcuts->layout()->addWidget(snap);
    shortcuts->layout()->addWidget(
        label("Q 选择 · W 移动 · E 旋转 · R 缩放\nAlt + 左键环绕 · 中键平移 · 滚轮缩放\nF 定位 · Esc "
              "取消拖动 · Delete 删除\nCtrl+D 复制 · Ctrl+Z 撤销 · Ctrl+Shift+Z 重做\nCtrl+N 新建 · Ctrl+O "
              "打开 · Ctrl+S 保存 · 渲染页 F12 加入队列\n输入框保留文本编辑快捷键。"));
    prefSections << shortcuts;
    auto performanceSettings = group("性能");
    auto compute = new QCheckBox("使用计算着色器进行路径追踪（实验性）");
    compute->setObjectName("computePathtracePreference");
    compute->setChecked(settings.value("workspaceV4/computePathtrace", false).toBool());
    RenderParams::instance().setComputePathtrace(compute->isChecked());
    performanceSettings->layout()->addWidget(compute);
    performanceSettings->layout()->addWidget(label(
        "影响路径追踪预览及之后启动的正式任务。速度因显卡和场景而异，图像可能有微小数值差异。"
        "不支持时自动使用兼容模式。"));
    auto backend = label("返回场景视口后显示当前渲染方式。");
    backend->setObjectName("pathtraceBackendStatus");
    performanceSettings->layout()->addWidget(backend);
    connect(compute, &QCheckBox::toggled, this, [this, backend](bool enabled) {
        RenderParams::instance().setComputePathtrace(enabled);
        QSettings s(QSettings::defaultFormat(), QSettings::UserScope, "learnQT", "SceneWorkbench");
        s.setValue("workspaceV4/computePathtrace", enabled);
        backend->setText("已保存；返回场景视口后应用，正在运行的正式任务保持原设置。");
        if (viewport->renderThread())
            viewport->renderThread()->markSceneDirty(toSceneDirtyFlags(SceneDirtyFlag::Display));
    });
    connect(viewport, &GLWidget::renderThreadReady, this, [this, backend] {
        connect(viewport->renderThread(), &RenderThread::statsReady, backend,
                [backend](const RenderStats &stats) {
                    backend->setText("当前：" + stats.pathtraceBackend);
                });
    });
    prefSections << performanceSettings;
    auto paths = group("路径");
    paths->layout()->addWidget(
        label("模型、纹理和 HDR 使用文件选择器导入；正式任务完成后自动写入下方目录。"));
    auto exportPath = new QLineEdit(settings.value("workspaceV4/autoExportPath").toString());
    exportPath->setObjectName("autoExportPath");
    exportPath->setPlaceholderText("选择自动导出目录");
    auto browseExport = new QPushButton("选择目录…");
    auto exportPathRow = new QHBoxLayout;
    exportPathRow->addWidget(exportPath, 1);
    exportPathRow->addWidget(browseExport);
    static_cast<QVBoxLayout *>(paths->layout())->addLayout(exportPathRow);
    connect(exportPath, &QLineEdit::editingFinished, this, [exportPath] {
        QSettings s(QSettings::defaultFormat(), QSettings::UserScope, "learnQT", "SceneWorkbench");
        s.setValue("workspaceV4/autoExportPath", exportPath->text().trimmed());
    });
    connect(browseExport, &QPushButton::clicked, this, [this, exportPath] {
        const auto directory = QFileDialog::getExistingDirectory(this, tr("自动导出目录"), exportPath->text());
        if (directory.isEmpty())
            return;
        exportPath->setText(directory);
        QSettings s(QSettings::defaultFormat(), QSettings::UserScope, "learnQT", "SceneWorkbench");
        s.setValue("workspaceV4/autoExportPath", directory);
    });
    prefSections << paths;
    prefSections << unavailable("自动保存", "当前使用手动保存及未保存修改提示。",
                                {"自动保存间隔 / 保留版本"});
    prefSections << unavailable("插件", "插件系统尚未接入。", {"管理插件"});
    prefSections << unavailable("语言与主题", "当前使用简体中文与深蓝灰主题；缩放跟随 Windows DPI。",
                                {"语言 / 主题 / 界面缩放"});
    for (auto section : prefSections)
        prefColumn->addWidget(section);
    prefColumn->addStretch();
    auto prefScroll = qobject_cast<QScrollArea *>(scrolling(prefContent));
    prefSplit->addWidget(prefScroll);
    prefSplit->setStretchFactor(1, 1);
    prefLayout->addWidget(prefSplit, 1);
    connect(prefCategories, &QListWidget::currentRowChanged, this, [prefScroll, prefSections](int i) {
        if (i >= 0)
            prefScroll->ensureWidgetVisible(prefSections[i], 0, 12);
    });
    auto persist = [](const QString &key, const QVariant &value) {
        QSettings s(QSettings::defaultFormat(), QSettings::UserScope, "learnQT", "SceneWorkbench");
        s.setValue("workspaceV4/" + key, value);
    };
    connect(welcome, &QCheckBox::toggled, this, [this, persist](bool b) {
        workspace->welcome = b;
        persist("welcome", b);
    });
    connect(tips, &QCheckBox::toggled, this, [this, persist](bool b) {
        workspace->tips = b;
        persist("tips", b);
    });
    connect(status, &QCheckBox::toggled, this, [this, persist](bool b) {
        workspace->status = b;
        statusBar()->setVisible(b);
        persist("status", b);
    });
    connect(limit, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, persist](int n) {
        workspace->recentLimit = n;
        workspace->recent = workspace->recent.mid(0, n);
        persist("recentLimit", n);
        persist("recent", workspace->recent);
        refreshWorkspace();
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        workspace->layouts.clear();
        QSettings s(QSettings::defaultFormat(), QSettings::UserScope, "learnQT", "SceneWorkbench");
        s.remove("workspaceV5/layout");
        workspace->activeLayoutKey.clear();
        workspace->page = -1;
        navigateWorkspace(WorkspacePage::Settings);
    });
    auto snapFields = snap->findChildren<MixedSpin *>();
    QStringList snapKeys{"snapMove", "snapRotate", "snapScale"};
    for (int i = 0; i < snapFields.size(); ++i)
    {
        auto spin = snapFields[i];
        auto key = snapKeys[i];
        spin->setValue(settings.value("workspaceV4/" + key, spin->value()).toDouble());
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [persist, key](double v) { persist(key, v); });
    }
    w.preferences = preferences;

    inspectorDock->setWidget(w.rightStack);
    oldTabs->deleteLater();
    w.left = new QDockWidget("资源与内容", this);
    w.left->setObjectName("workspaceLeft");
    w.leftStack = new QStackedWidget;
    w.left->setWidget(w.leftStack);
    addDockWidget(Qt::LeftDockWidgetArea, w.left);
    w.bottom = new QDockWidget("资源浏览器", this);
    w.bottom->setObjectName("workspaceBottom");
    w.bottomStack = new QStackedWidget;
    w.bottom->setWidget(w.bottomStack);
    addDockWidget(Qt::BottomDockWidgetArea, w.bottom);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    auto materials = new QWidget;
    auto materialLayout = column(materials);
    w.materialSearch = new QLineEdit;
    w.materialSearch->setPlaceholderText("搜索当前场景材质…");
    materialLayout->addWidget(w.materialSearch);
    w.materialFilter = new QComboBox;
    w.materialFilter->addItems({"所选对象材质", "全部场景材质"});
    materialLayout->addWidget(w.materialFilter);
    w.materials = cards("workspaceMaterials", 48);
    materialLayout->addWidget(w.materials, 1);
    w.materialContext = label("选择对象后编辑其材质。没有通用材质库。");
    materialLayout->addWidget(w.materialContext);
    auto selectUsers = new QPushButton("选择使用此材质的对象");
    materialLayout->addWidget(selectUsers);
    connect(selectUsers, &QPushButton::clicked, this, [this] {
        auto item = workspace->materials->currentItem();
        if (!item)
            return;
        auto material = item->data(Qt::UserRole).toString();
        QSet<QString> ids;
        for (auto v : editor->document.root["objects"].toArray())
            if (v.toObject()["material"].toString() == material)
                ids.insert(v.toObject()["id"].toString());
        editor->select(ids);
    });
    w.leftPages[2] = materials;
    connect(w.materialSearch, &QLineEdit::textChanged, this, [this] { refreshWorkspace(); });
    connect(w.materialFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { refreshWorkspace(); });
    connect(w.materials, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (!workspace->refreshing && workspace->page == 2)
            inspector->browseMaterial(item ? item->data(Qt::UserRole).toString() : QString());
    });
    auto lightListPanel = new QWidget;
    auto lightListLayout = column(lightListPanel);
    lightListLayout->addWidget(label("场景灯光", "sectionHeading"));
    w.lights = new QListWidget;
    w.lights->setObjectName("workspaceLights");
    lightListLayout->addWidget(w.lights, 1);
    lightListLayout->addWidget(label("在右侧添加太阳盘或球形光，并调整真实辐亮度。"));
    auto oldLightList = lightContent->findChild<QComboBox *>();
    oldLightList->hide();
    if (auto form = lightContent->findChild<QFormLayout *>())
        if (auto caption = form->labelForField(oldLightList)) caption->hide();
    connect(w.lights, &QListWidget::currentItemChanged, this, [this, oldLightList](QListWidgetItem *item) {
        if (!item || workspace->refreshing)
            return;
        int i = oldLightList->findData(item->data(Qt::UserRole));
        if (i >= 0)
        {
            oldLightList->setCurrentIndex(i);
            QMetaObject::invokeMethod(oldLightList, "activated", Q_ARG(int, i));
        }
    });
    connect(oldLightList, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, oldLightList](int) {
        QSignalBlocker b(workspace->lights);
        for (int i = 0; i < workspace->lights->count(); ++i)
            if (workspace->lights->item(i)->data(Qt::UserRole) == oldLightList->currentData())
                workspace->lights->setCurrentRow(i);
    });
    w.lightingTabs = new QTabWidget;
    w.lightingTabs->setObjectName("lightingTabs");
    w.lightingTabs->addTab(lightListPanel, "场景灯光");
    w.leftPages[3] = w.lightingTabs;
    auto cameraList = new QWidget;
    auto cameraListLayout = column(cameraList);
    auto cameraHeading = new QHBoxLayout;
    cameraHeading->addWidget(label("场景相机", "sectionHeading"), 1);
    auto clearCamera = new QToolButton;
    clearCamera->setText("取消选择");
    clearCamera->setObjectName("savedCameraClearSelection");
    clearCamera->setToolTip("清除列表选择，保留当前编辑相机");
    cameraHeading->addWidget(clearCamera);
    cameraListLayout->addLayout(cameraHeading);
    auto currentCamera = new QListWidget;
    currentCamera->setObjectName("savedCameraList");
    cameraListLayout->addWidget(currentCamera, 1);
    auto cameraButtons = new QHBoxLayout;
    auto addCamera = new QPushButton("新建视角");
    addCamera->setObjectName("savedCameraAdd");
    addCamera->setToolTip("以当前画面创建一个新视角，不覆盖已有视角");
    auto renameCamera = new QPushButton("重命名");
    renameCamera->setObjectName("savedCameraRename");
    auto deleteCamera = new QPushButton("删除");
    deleteCamera->setObjectName("savedCameraDelete");
    for (auto button : {addCamera, renameCamera, deleteCamera})
        cameraButtons->addWidget(button);
    cameraListLayout->addLayout(cameraButtons);
    w.mutationWidgets << addCamera << renameCamera << deleteCamera;
    auto refreshCameraList = [this, currentCamera, renameCamera, deleteCamera, clearCamera] {
        QSignalBlocker block(currentCamera);
        currentCamera->clear();
        const auto cameras = editor->document.root["cameras"].toArray();
        bool selectedFound = false;
        for (int i = 0; i < cameras.size(); ++i)
        {
            auto camera = cameras[i].toObject();
            auto item = new QListWidgetItem(WorkbenchStyle::icon("camera"), camera["name"].toString(), currentCamera);
            item->setData(Qt::UserRole, camera["id"].toString());
            if (!workspace->selectedCameraId.isEmpty() &&
                camera["id"].toString() == workspace->selectedCameraId)
            {
                currentCamera->setCurrentRow(i);
                selectedFound = true;
            }
        }
        if (!selectedFound)
            workspace->selectedCameraId.clear();
        clearCamera->setEnabled(selectedFound);
        renameCamera->setEnabled(selectedFound && !m_loading && !editor->renderLocked);
        deleteCamera->setEnabled(selectedFound && cameras.size() > 1 &&
                                 !m_loading && !editor->renderLocked);
    };
    w.refreshers.push_back(refreshCameraList);
    refreshCameraList();
    connect(editor, &EditorController::changed, currentCamera, [refreshCameraList](int change) {
        if (change == EditorController::CameraChange)
            refreshCameraList();
    });
    connect(clearCamera, &QToolButton::clicked, currentCamera, [currentCamera] {
        currentCamera->clearSelection();
        currentCamera->setCurrentRow(-1);
    });
    connect(currentCamera, &QListWidget::currentRowChanged, this,
            [this, currentCamera, renameCamera, deleteCamera, clearCamera](int row) {
        const auto cameras = editor->document.root["cameras"].toArray();
        const bool selected = row >= 0 && row < cameras.size();
        workspace->selectedCameraId = selected ? cameras[row].toObject()["id"].toString() : QString();
        clearCamera->setEnabled(selected);
        renameCamera->setEnabled(selected && !m_loading && !editor->renderLocked);
        deleteCamera->setEnabled(selected && cameras.size() > 1 &&
                                 !m_loading && !editor->renderLocked);
        if (!selected || m_loading || editor->renderLocked)
            return;
        auto camera = cameras[row].toObject();
        if (editor->document.root["activeCameraId"] == camera["id"])
            return;
        auto next = editor->document;
        next.root["activeCameraId"] = camera["id"];
        QJsonObject legacy;
        for (auto key : {"position", "target", "up", "fov"})
            legacy[key] = camera[key];
        next.root["camera"] = legacy;
        editor->submit(next, tr("切换相机"), EditorController::CameraChange);
    });
    connect(currentCamera, &QListWidget::itemSelectionChanged, this, [currentCamera] {
        if (currentCamera->selectedItems().isEmpty() && currentCamera->currentRow() >= 0)
            currentCamera->setCurrentRow(-1);
    });
    connect(addCamera, &QPushButton::clicked, this, [this, refreshCameraList] {
        if (m_loading || editor->renderLocked)
            return;
        auto next = editor->document;
        auto cameras = next.root["cameras"].toArray();
        auto camera = next.root["camera"].toObject();
        camera["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        camera["name"] = tr("相机 %1").arg(cameras.size() + 1);
        cameras.append(camera);
        next.root["cameras"] = cameras;
        next.root["activeCameraId"] = camera["id"];
        workspace->selectedCameraId = camera["id"].toString();
        editor->submit(next, tr("新建相机视角"), EditorController::Organization);
        refreshCameraList();
    });
    connect(renameCamera, &QPushButton::clicked, this, [this, currentCamera] {
        const int row = currentCamera->currentRow();
        if (row < 0 || m_loading || editor->renderLocked)
            return;
        auto next = editor->document;
        auto cameras = next.root["cameras"].toArray();
        auto camera = cameras[row].toObject();
        bool ok = false;
        const auto name = QInputDialog::getText(this, tr("重命名相机"), tr("名称"), QLineEdit::Normal,
                                                camera["name"].toString(), &ok).trimmed();
        if (!ok || name.isEmpty())
            return;
        camera["name"] = name;
        cameras[row] = camera;
        next.root["cameras"] = cameras;
        editor->submit(next, tr("重命名相机"), EditorController::Organization);
    });
    connect(deleteCamera, &QPushButton::clicked, this, [this, currentCamera] {
        auto next = editor->document;
        auto cameras = next.root["cameras"].toArray();
        const int row = currentCamera->currentRow();
        if (row < 0 || cameras.size() <= 1 || m_loading || editor->renderLocked)
            return;
        const auto removedId = cameras[row].toObject()["id"].toString();
        cameras.removeAt(row);
        next.root["cameras"] = cameras;
        workspace->selectedCameraId.clear();
        if (next.root["activeCameraId"].toString() == removedId)
        {
            auto active = cameras[qMin(row, cameras.size() - 1)].toObject();
            next.root["activeCameraId"] = active["id"];
            QJsonObject legacy;
            for (auto key : {"position", "target", "up", "fov"})
                legacy[key] = active[key];
            next.root["camera"] = legacy;
        }
        editor->submit(next, tr("删除相机"), EditorController::CameraChange);
    });
    w.leftPages[4] = cameraList;

    auto activateResource = [this](const QModelIndex &i) {
        if (!i.isValid())
            return;
        auto kind = i.data(KindRole).toString(), source = i.data(SourceRole).toString(),
             id = i.data(IdRole).toString();
        if (kind == "场景")
        {
            if (confirmDiscard())
                beginSceneLoad(source);
        }
        else if (kind == "模型")
        {
            if (!m_loading && !editor->renderLocked)
                importPaths({source});
        }
        else if (kind == "HDR")
        {
            if (m_loading || editor->renderLocked)
                return;
            auto d = editor->document;
            d.root["hdr"] = source;
            editor->submit(d, tr("应用 HDR 环境"), EditorController::Environment);
        }
        else
        {
            QSet<QString> materialIds;
            if (kind == "材质")
                materialIds.insert(id);
            else
                for (auto v : editor->document.root["materials"].toArray())
                {
                    auto m = v.toObject();
                    auto refs = m["textures"].toObject();
                    for (auto it = refs.begin(); it != refs.end(); ++it)
                        if (it.value().toString() == id)
                            materialIds.insert(m["id"].toString());
                }
            QSet<QString> objects;
            for (auto v : editor->document.root["objects"].toArray())
                if (materialIds.contains(v.toObject()["material"].toString()))
                    objects.insert(v.toObject()["id"].toString());
            editor->select(objects);
            navigateWorkspace(WorkspacePage::Material);
            if (kind == "材质")
            {
                workspace->materialFilter->setCurrentIndex(1);
                inspector->browseMaterial(id);
            }
        }
    };
    w.bottomPages[1] = resourceBrowser(w.catalog, "", activateResource, "sceneResources", true);
    w.lightingTabs->addTab(resourceBrowser(w.catalog, "HDR", activateResource, "environmentLibrary", true), "HDR 环境");
    connect(w.lightingTabs, &QTabWidget::currentChanged, this, [this](int index) {
        QSettings s(QSettings::defaultFormat(), QSettings::UserScope, "learnQT", "SceneWorkbench");
        s.setValue("workspaceV5/lightingTab", index);
        workspace->rightPages[int(WorkspacePage::Lighting)] = index == 0
            ? workspace->lightProperties : workspace->environmentProperties;
        if (workspace->page == int(WorkspacePage::Lighting)) {
            workspace->rightStack->setCurrentWidget(workspace->rightPages[workspace->page]);
            inspectorDock->setWindowTitle(index == 0 ? "灯光属性" : "环境属性");
        }
    });
    w.lightingTabs->setCurrentIndex(qBound(0, settings.value("workspaceV5/lightingTab", 0).toInt(), 1));
    auto resources = new QWidget;
    auto resourceLayout = column(resources);
    resourceLayout->addWidget(label("资源浏览器", "pageHeading"));
    resourceLayout->addWidget(label("内置场景与当前文档资源 · 图片显示真实缩略图，其余显示类型图标"));
    resourceLayout->addWidget(resourceBrowser(w.catalog, "", activateResource, "allResources"), 1);
    resourceLayout->addWidget(unavailable("扩展资源库", "在线资源、收藏和拖放赋材质尚未开放。"));
    w.resources = resources;

    auto taskPanel = new QWidget;
    auto taskLayout = column(taskPanel);
    auto taskControls = new QHBoxLayout;
    for (auto a : {pauseAction, stopAction})
    {
        auto button = new QToolButton;
        button->setDefaultAction(a);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        taskControls->addWidget(button);
    }
    auto runQueueButton = new QPushButton("运行队列");
    runQueueButton->setObjectName("runRenderQueue");
    taskControls->addWidget(runQueueButton);
    connect(runQueueButton, &QPushButton::clicked, this, &learnQT::runRenderQueue);
    w.runQueue = new QAction("运行队列", this);
    connect(w.runQueue, &QAction::triggered, this, &learnQT::runRenderQueue);
    auto stopQueueButton = new QPushButton("停止队列");
    stopQueueButton->setObjectName("stopRenderQueue");
    taskControls->addWidget(stopQueueButton);
    w.stopQueue = new QAction("停止队列", this);
    connect(w.stopQueue, &QAction::triggered, this, [this] {
        m_queueRunning = false;
        if (m_queueWorker && m_activeQueueId)
            m_queueWorker->stopCurrent();
        refreshRenderQueue();
    });
    connect(stopQueueButton, &QPushButton::clicked, w.stopQueue, &QAction::trigger);
    w.stopQueue->setEnabled(false);
    auto moveUp = new QPushButton("上移");
    auto moveDown = new QPushButton("下移");
    auto removeTask = new QPushButton("移除");
    for (auto button : {moveUp, moveDown, removeTask})
        taskControls->addWidget(button);
    taskControls->addStretch();
    taskLayout->addLayout(taskControls);
    w.task = new QTableWidget(1, 5);
    w.task->setObjectName("currentRenderTask");
    w.task->setHorizontalHeaderLabels({"任务", "分辨率", "采样", "状态", "用时"});
    w.task->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    w.task->verticalHeader()->hide();
    w.task->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (int i = 0; i < 5; ++i)
        w.task->setItem(0, i, new QTableWidgetItem(i == 0 ? "当前单张输出" : "—"));
    taskLayout->addWidget(w.task);
    connect(w.task, &QTableWidget::cellClicked, this, [this](int, int) { showRenderTaskResult(); });
    connect(w.task, &QTableWidget::itemSelectionChanged, this,
            [this] { showRenderTaskResult(); });
    connect(w.task, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row < 0 || row >= m_renderQueue.size() || m_renderQueue[row].status != tr("等待中"))
            return;
        bool accepted = false;
        const auto name = QInputDialog::getText(this, tr("重命名渲染任务"), tr("任务名称"),
                                                QLineEdit::Normal, m_renderQueue[row].name,
                                                &accepted).trimmed();
        if (accepted && !name.isEmpty())
        {
            m_renderQueue[row].name = name;
            refreshRenderQueue();
        }
    });
    connect(moveUp, &QPushButton::clicked, this, [this] {
        const int row = workspace->task->currentRow();
        if (row <= 0 || row >= m_renderQueue.size() ||
            m_renderQueue[row].status != tr("等待中") || m_renderQueue[row - 1].status != tr("等待中"))
            return;
        m_renderQueue.swapItemsAt(row, row - 1);
        refreshRenderQueue();
        workspace->task->selectRow(row - 1);
    });
    connect(moveDown, &QPushButton::clicked, this, [this] {
        const int row = workspace->task->currentRow();
        if (row < 0 || row + 1 >= m_renderQueue.size() ||
            m_renderQueue[row].status != tr("等待中") || m_renderQueue[row + 1].status != tr("等待中"))
            return;
        m_renderQueue.swapItemsAt(row, row + 1);
        refreshRenderQueue();
        workspace->task->selectRow(row + 1);
    });
    connect(removeTask, &QPushButton::clicked, this, [this] {
        const int row = workspace->task->currentRow();
        if (row < 0 || row >= m_renderQueue.size() || m_renderQueue[row].status != tr("等待中"))
            return;
        m_renderQueue.removeAt(row);
        refreshRenderQueue();
    });
    taskLayout->addWidget(label("任务按顺序运行；可继续编辑场景并加入下一张图片。"));
    w.bottomPages[6] = taskPanel;
    connect(viewport, &GLWidget::renderThreadReady, this, [this] {
        connect(viewport->renderThread(), &RenderThread::statsReady, this, [this](const RenderStats &s) {
            if (!m_renderQueue.isEmpty() || !editor->renderLocked || s.target <= 0 ||
                s.size != workspace->taskSize)
                return;
            auto t = workspace->task;
            workspace->taskSamples = s.samples;
            t->item(0, 2)->setText(QString("%1 / %2 spp").arg(s.samples).arg(workspace->taskTarget));
            t->item(0, 3)->setText(renderJobText(s.state));
            t->item(0, 4)->setText(QString::number(s.jobSeconds, 'f', 1) + " 秒");
        });
        connect(viewport->renderThread(), &RenderThread::jobStateChanged, this,
                [this](RenderJobState state, const QString &) {
                    if (!m_renderQueue.isEmpty())
                        return;
                    workspace->task->item(0, 3)->setText(renderJobText(state));
                    if (state == RenderJobState::Completed)
                    {
                        workspace->taskSamples = workspace->taskTarget;
                        workspace->task->item(0, 2)->setText(
                            QString("%1 / %1 spp").arg(workspace->taskTarget));
                    }
                    if ((state == RenderJobState::Completed || state == RenderJobState::Stopped ||
                         state == RenderJobState::Failed) &&
                        workspace->taskClock.isValid())
                        workspace->task->item(0, 4)->setText(
                            QString::number(workspace->taskClock.elapsed() / 1000., 'f', 1) + " 秒");
                });
    });

    auto homeContent = new QWidget;
    auto homeLayout = column(homeContent);
    homeContent->setObjectName("homeContent");
    auto hero = new QFrame;
    hero->setObjectName("welcomeHero");
    auto heroLayout = column(hero);
    heroLayout->setContentsMargins(28, 24, 28, 24);
    heroLayout->addWidget(label("欢迎使用场景工作台", "heroHeading"));
    heroLayout->addWidget(label("从模型到光影，在一个工作台中完成场景编辑与图像输出。", "heroSubtitle"));
    homeLayout->addWidget(hero);
    auto actions = new QHBoxLayout;
    const QStringList titles{"新建场景", "打开项目", "浏览场景预设"};
    const QStringList descriptions{"从空场景开始创作", "继续编辑本地场景文件", "浏览内置场景与渲染示例"};
    for (int i = 0; i < 3; ++i)
    {
        auto button = new QPushButton(WorkbenchStyle::icon(QStringList{"new", "open", "assets"}[i]),
                                      titles[i] + "\n" + descriptions[i]);
        button->setObjectName(QString("homeAction%1").arg(i));
        button->setProperty("homeCard", true);
        button->setIconSize(QSize(38, 38));
        button->setMinimumHeight(100);
        actions->addWidget(button, 1);
        connect(button, &QPushButton::clicked, this, [this, i] {
            if (i == 0)
            {
                if (confirmDiscard())
                    beginSceneLoad({});
            }
            else if (i == 1)
            {
                for (auto a : editActions)
                    if (a->shortcut() == QKeySequence::Open)
                    {
                        a->trigger();
                        break;
                    }
            }
            else
                navigateWorkspace(WorkspacePage::Resources);
        });
        if (i < 2)
            w.mutationWidgets << button;
    }
    homeLayout->addLayout(actions);
    homeLayout->addWidget(label("最近项目", "sectionHeading"));
    w.recentList = cards("recentProjects", 46);
    w.recentList->setMinimumHeight(140);
    w.recentList->setMaximumHeight(185);
    homeLayout->addWidget(w.recentList);
    homeLayout->addWidget(label("内置场景", "sectionHeading"));
    w.presets = cards("homePresets", 54);
    w.presets->setMinimumHeight(195);
    homeLayout->addWidget(w.presets, 1);
    for (auto list : {w.recentList, w.presets})
        connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
            auto source = item->data(Qt::UserRole).toString();
            if (!source.isEmpty() && confirmDiscard())
                beginSceneLoad(source);
        });
    auto quick = group("快速开始");
    quick->layout()->addWidget(label("1  新建或打开场景     →     2  导入模型，编辑材质与灯光     →     3  "
                                     "设置输出参数     →     4  开始渲染并导出图片"));
    homeLayout->addWidget(quick);
    homeLayout->addWidget(unavailable("学习与社区 / 最新动态", "教程、社区作品和更新动态暂未接入。"));
    w.home = scrolling(homeContent);
    views->addTab(w.home, "首页");
    views->addTab(w.resources, "资源");
    views->addTab(w.preferences, "设置");
    auto central = new QWidget;
    auto centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    w.renderModes = new QWidget;
    w.renderModes->setObjectName("renderModeBar");
    auto modeLayout = new QHBoxLayout(w.renderModes);
    modeLayout->setContentsMargins(8, 4, 8, 4);
    compositionButton->setText("构图与提交");
    resultButton->setText("结果与队列");
    compositionButton->setObjectName("renderCompositionMode");
    resultButton->setObjectName("renderResultsMode");
    compositionButton->setCheckable(true);
    resultButton->setCheckable(true);
    compositionButton->setAutoExclusive(true);
    resultButton->setAutoExclusive(true);
    modeLayout->addWidget(compositionButton);
    modeLayout->addWidget(resultButton);
    modeLayout->addWidget(submit);
    modeLayout->addStretch();
    w.queueBadge = label("等待 0 · 共 0 个任务");
    w.queueBadge->setObjectName("renderQueueBadge");
    modeLayout->addWidget(w.queueBadge);
    auto toggleQueue = new QToolButton;
    toggleQueue->setText("队列面板");
    toggleQueue->setObjectName("toggleRenderQueue");
    modeLayout->addWidget(toggleQueue);
    connect(toggleQueue, &QToolButton::clicked, this, [this] {
        workspace->bottom->setVisible(!workspace->bottom->isVisible());
    });
    centralLayout->addWidget(w.renderModes);
    centralLayout->addWidget(views, 1);
    setCentralWidget(central);
    w.renderModes->hide();
    inspector->openMaterialPage = [this] { navigateWorkspace(WorkspacePage::Material); };
    auto renderMenu = menuBar()->addMenu("渲染");
    renderMenu->addAction(renderAction);
    renderMenu->addAction(w.runQueue);
    renderMenu->addAction(pauseAction);
    renderMenu->addAction(stopAction);
    renderMenu->addAction(w.stopQueue);

    for (auto p : w.leftPages)
        w.leftStack->addWidget(p);
    for (auto p : w.bottomPages)
        w.bottomStack->addWidget(p);
    w.rail = new QToolBar("功能导航", this);
    w.rail->setObjectName("navigationRail");
    w.rail->setMovable(false);
    w.rail->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    w.rail->setIconSize(QSize(22, 22));
    addToolBar(Qt::LeftToolBarArea, w.rail);
    auto navigationGroup = new QActionGroup(this);
    auto navigationMenu = menuBar()->addMenu("工作区");
    for (const auto &page : workspacePages())
    {
        const int i = page.id;
        if (i == int(WorkspacePage::Settings))
        {
            auto spacer = new QWidget;
            spacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
            w.rail->addWidget(spacer);
        }
        auto a = w.rail->addAction(WorkbenchStyle::icon(page.icon), QString::fromUtf8(page.title));
        a->setObjectName(QString("navigate%1").arg(i));
        a->setCheckable(true);
        navigationGroup->addAction(a);
        w.navigation[i] = a;
        navigationMenu->addAction(a);
        connect(a, &QAction::triggered, this, [this, i] { navigateWorkspace(WorkspacePage(i)); });
    }
    for (auto dock : {treeDock, inspectorDock, performanceDock, logDock, w.left, w.bottom})
    {
        dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
        dock->setMinimumWidth(dock == inspectorDock ? 320 : dock == performanceDock ? 280 : 205);
        setupDockTitle(dock, dock == w.bottom ? "assets" : "settings");
    }
    performance->setMinimumHeight(230);
    w.bottom->setMinimumHeight(140);
    w.left->setMaximumWidth(420);
    auto panelMenu = menuBar()->addMenu("面板");
    for (auto dock : {treeDock, inspectorDock, performanceDock, w.left, w.bottom, logDock})
    {
        auto action = dock->toggleViewAction();
        panelMenu->addAction(action);
        connect(panelMenu, &QMenu::aboutToShow, this, [this, dock, action] {
            int page = workspace->page;
            action->setEnabled(dock == logDock || dock == performanceDock ||
                               (dock == treeDock ? page == int(WorkspacePage::Scene)
                                : dock == workspace->left   ? workspace->leftPages.contains(page)
                                : dock == workspace->bottom ? workspace->bottomPages.contains(page)
                                                            : workspace->rightPages.contains(page)));
        });
    }
    w.baseline = saveState(5);
    connect(editor, &EditorController::changed, this, [this](int change) {
        UiSlotTimer timer(UiSlotWorkspacePages);
        if (change == EditorController::CameraChange)
        {
            workspace->refreshCamera();
            return;
        }
        if (change != EditorController::Transform && change != EditorController::Display)
            refreshWorkspace();
        for (auto &f : workspace->refreshers)
            f();
    });
    connect(editor, &EditorController::selectionChanged, this, &learnQT::refreshWorkspace);
    connect(editor, &EditorController::prepared, this, [this] { refreshWorkspace(); });
    connect(views, &QTabWidget::currentChanged, this, [this](int index) {
        if (workspace->navigating)
            return;
        if (index == 0 && (workspace->page < 1 || workspace->page > 5))
            navigateWorkspace(WorkspacePage::Scene);
        if (index == 1)
            navigateWorkspace(WorkspacePage::Render);
    });
    refreshWorkspace();
}

void learnQT::navigateWorkspace(WorkspacePage page)
{
    if (!workspace || workspace->navigating) return;
    if (page == WorkspacePage::Lights || page == WorkspacePage::Environment) {
        workspace->lightingTabs->setCurrentIndex(page == WorkspacePage::Environment ? 1 : 0);
        page = WorkspacePage::Lighting;
    }
    const int target = int(page);
    if (workspace->page == target) return;
    workspace->page = target;
    applyWorkspaceLayout();
}

void learnQT::applyWorkspaceLayout()
{
    auto &w = *workspace;
    w.navigating = true;
    if (!w.activeLayoutKey.isEmpty())
        w.layouts[w.activeLayoutKey] = saveState(5);
    const auto &description = workspaceDescription(w.page);
    const bool render = w.page == int(WorkspacePage::Render);
    const bool scene = w.page == int(WorkspacePage::Scene);
    const auto key = workspaceLayoutKey(w.page, m_renderPreviewMode);
    viewport->setTool(viewport->tool);
    restoreState(w.baseline, 5);
    for (auto dock : {treeDock, inspectorDock, performanceDock, logDock, w.left, w.bottom})
        dock->hide();
    if (w.rightPages.contains(w.page)) {
        w.rightStack->setCurrentWidget(render
            ? (m_renderPreviewMode ? w.outputProperties : w.resultProperties) : w.rightPages[w.page]);
        inspectorDock->show();
    }
    if (w.leftPages.contains(w.page)) {
        w.leftStack->setCurrentWidget(w.leftPages[w.page]);
        w.left->show();
    }
    if (w.bottomPages.contains(w.page)) {
        w.bottomStack->setCurrentWidget(w.bottomPages[w.page]);
        w.bottom->setVisible(render && !m_renderPreviewMode);
    }
    treeDock->setVisible(scene);
    inspector->setMaterialPage(w.page == int(WorkspacePage::Material));
    if (w.page == int(WorkspacePage::Material))
        inspector->browseMaterial(w.materials->currentItem()
            ? w.materials->currentItem()->data(Qt::UserRole).toString() : QString());
    inspectorDock->setWindowTitle(render ? (m_renderPreviewMode ? "正式输出与构图" : "任务快照")
        : w.page == int(WorkspacePage::Lighting) ? (w.lightingTabs->currentIndex() == 0 ? "灯光属性" : "环境属性")
        : QString::fromUtf8(description.title) + "属性");
    w.left->setWindowTitle(w.page == int(WorkspacePage::Material) ? "场景材质"
        : w.page == int(WorkspacePage::Lighting) ? "照明内容" : "场景相机");
    w.bottom->setWindowTitle(render ? "渲染队列" : "资源浏览器");
    w.renderModes->setVisible(render);
    w.compositionMode->setChecked(m_renderPreviewMode);
    w.resultsMode->setChecked(!m_renderPreviewMode);
    findChild<QToolButton *>("renderPrimary")->setVisible(render && m_renderPreviewMode);
    views->setCurrentIndex(render ? (m_renderPreviewMode ? 0 : 1) : description.view);
    if (render && !m_draftCamera.isEmpty()) {
        Camera draft;
        draft.restoreState(sceneVector(m_draftCamera["position"]), sceneVector(m_draftCamera["target"]),
                           sceneVector(m_draftCamera["up"]), m_draftCamera["fov"].toDouble());
        viewport->setCompositionMode(true, draft, QSize(outputWidth->value(), outputHeight->value()));
    } else {
        viewport->setCompositionMode(false);
    }
    resizeDocks({treeDock, inspectorDock, w.left},
        {width() < 1450 ? 220 : 240, width() < 1450 ? 320 : 340, width() < 1450 ? 220 : 240}, Qt::Horizontal);
    resizeDocks({w.bottom}, {200}, Qt::Vertical);
    if (w.layouts.contains(key)) restoreState(w.layouts[key], 5);
    w.activeLayoutKey = key;
    w.navigation[w.page]->setChecked(true);
    if (viewport->renderThread())
        viewport->renderThread()->setPreviewVisible(description.preview || (render && m_renderPreviewMode));
    w.navigating = false;
    for (const auto &refresh : w.refreshers) refresh();
    w.refreshCamera();
    refreshTaskProperties();
    syncWorkspaceAvailability();
}

void learnQT::rememberScene(const QString &path)
{
    if (!workspace || path.isEmpty())
        return;
    QString absolute = QFileInfo(path).absoluteFilePath();
    auto &recent = workspace->recent;
    for (int i = recent.size() - 1; i >= 0; --i)
        if (recent[i].compare(absolute, Qt::CaseInsensitive) == 0)
            recent.removeAt(i);
    recent.prepend(absolute);
    recent = recent.mid(0, workspace->recentLimit);
    QSettings s(QSettings::defaultFormat(), QSettings::UserScope, "learnQT", "SceneWorkbench");
    s.setValue("workspaceV4/recent", recent);
    refreshWorkspace();
}

void learnQT::refreshWorkspace()
{
    if (!workspace || workspace->refreshing)
        return;
    auto &w = *workspace;
    w.refreshing = true;
    QString material =
        w.materials->currentItem() ? w.materials->currentItem()->data(Qt::UserRole).toString() : QString();
    w.materials->clear();
    QSet<QString> selectedMaterials;
    for (auto id : editor->selectedModels())
        selectedMaterials.insert(editor->node(id)["material"].toString());
    for (auto v : editor->document.root["materials"].toArray())
    {
        auto m = v.toObject();
        auto id = m["id"].toString(), name = m["name"].toString(id);
        if (w.materialFilter->currentIndex() == 0 && !selectedMaterials.contains(id))
            continue;
        if (!name.contains(w.materialSearch->text(), Qt::CaseInsensitive) &&
            !id.contains(w.materialSearch->text(), Qt::CaseInsensitive))
            continue;
        addCard(w.materials, name, "material", id);
        if (id == material)
            w.materials->setCurrentRow(w.materials->count() - 1);
    }
    if (!w.materials->currentItem() && w.materials->count())
        w.materials->setCurrentRow(0);
    w.materialContext->setText(w.materials->count() ? "修改仅影响所选对象；共享材质按需隔离。"
                                                    : "没有匹配材质。选择模型或切换至全部场景材质。");
    if (w.page == 2)
        inspector->browseMaterial(w.materials->currentItem()
                                      ? w.materials->currentItem()->data(Qt::UserRole).toString()
                                      : QString());
    auto light = w.lights->currentItem() ? w.lights->currentItem()->data(Qt::UserRole).toString()
        : w.lightProperties->findChild<QComboBox *>()->currentData().toString();
    w.lights->clear();
    for (auto v : editor->document.root["lights"].toArray())
    {
        auto l = v.toObject();
        addCard(w.lights, l["name"].toString(l["id"].toString()), "light", l["id"].toString());
        if (l["id"].toString() == light)
            w.lights->setCurrentRow(w.lights->count() - 1);
    }
    w.recentList->clear();
    for (auto path : w.recent)
    {
        QFileInfo file(path);
        addCard(w.recentList,
                file.completeBaseName().replace(".scene", "") + (file.exists() ? "" : "\n文件不可用"),
                "scene", path);
    }
    if (w.recent.isEmpty())
    {
        auto item = new QListWidgetItem("暂无最近项目", w.recentList);
        item->setToolTip("打开或保存场景后会显示在这里。");
        item->setFlags(Qt::NoItemFlags);
    }
    QJsonArray signatureParts;
    for (auto key : QStringList{"models", "materials", "textures"})
        for (auto value : editor->document.root[key].toArray())
        {
            auto o = value.toObject();
            signatureParts.append(QJsonArray{key, o["id"], o["name"], o["source"]});
        }
    signatureParts.append(editor->document.root["hdr"]);
    for (const auto value : editor->document.root["objects"].toArray()) {
        const auto object = value.toObject();
        signatureParts.append(QJsonArray{object["id"], object["model"], object["material"]});
    }
    for (const auto value : editor->document.root["materials"].toArray())
        signatureParts.append(value.toObject()["textures"]);
    auto signature = QJsonDocument(signatureParts).toJson(QJsonDocument::Compact);
    if (w.catalogSignature != signature)
    {
        w.catalogSignature = signature;
        w.presets->clear();
        w.catalog->clear();
        auto add = [this, &w](const QString &kind, const QString &title, const QString &source, const QString &id,
                        const QString &icon) {
            auto item = new QStandardItem(WorkbenchStyle::icon(icon), title);
            if (kind == "纹理" && !source.isEmpty())
            {
                QImageReader reader(source);
                reader.setScaledSize(QSize(96, 96));
                auto image = reader.read();
                if (!image.isNull())
                    item->setIcon(QPixmap::fromImage(image));
            }
            item->setEditable(false);
            item->setData(kind, KindRole);
            item->setData(source, SourceRole);
            item->setData(id, IdRole);
            int references = 0;
            if (kind == "模型" || kind == "材质") {
                for (const auto value : editor->document.root["objects"].toArray())
                    if (value.toObject()[kind == "模型" ? "model" : "material"].toString() == id) ++references;
                item->setData(tr("%1 个对象").arg(references), ReferencesRole);
            } else if (kind == "纹理") {
                for (const auto value : editor->document.root["materials"].toArray()) {
                    const auto textures = value.toObject()["textures"].toObject();
                    for (auto it = textures.begin(); it != textures.end(); ++it)
                        if (it.value().toString() == id) ++references;
                }
                item->setData(tr("%1 个材质贴图槽").arg(references), ReferencesRole);
            } else if (kind == "HDR") {
                item->setData(editor->document.root["hdr"].toString() == source ? "当前环境" : "本地资源", ReferencesRole);
            } else {
                item->setData("内置场景预设", ReferencesRole);
            }
            item->setToolTip(kind + " · " + title + "\n" + source);
            w.catalog->appendRow(item);
        };
        QDir scenes(QString::fromStdString(getResourcePath("scenes")));
        for (const auto &file : scenes.entryList({"*.scene.json"}, QDir::Files, QDir::Name))
        {
            QString title = QFileInfo(file).completeBaseName().replace(".scene", "");
            add("场景", title, scenes.absoluteFilePath(file), "", "scene");
            addCard(w.presets, title, "scene", scenes.absoluteFilePath(file));
        }
        for (auto key : QStringList{"models", "materials", "textures"})
            for (auto v : editor->document.root[key].toArray())
            {
                auto o = v.toObject();
                auto source = o["source"].toString(), id = o["id"].toString();
                auto kind = key == "models" ? "模型" : key == "materials" ? "材质" : "纹理";
                add(kind, o["name"].toString(source.isEmpty() ? id : QFileInfo(source).fileName()), source,
                    id, key == "materials" ? "material" : "assets");
            }
        QSet<QString> hdrs;
        auto hdr = editor->document.root["hdr"].toString();
        if (!hdr.isEmpty())
            hdrs.insert(hdr);
        for (auto path : w.hdrFiles)
            hdrs.insert(path);
        auto ordered = hdrs.values();
        ordered.sort(Qt::CaseInsensitive);
        for (auto path : ordered)
            add("HDR", QFileInfo(path).completeBaseName(), path, "", "environment");
    }
    for (auto &f : w.refreshers)
        f();
    w.refreshCamera();
    w.refreshing = false;
    syncWorkspaceAvailability();
}

void learnQT::syncWorkspaceAvailability()
{
    if (!workspace)
        return;
    bool editable = !m_loading && !editor->renderLocked;
    setProperty("workspaceEditable", editable);
    for (auto action : editActions)
        action->setEnabled(editable && (!action->property("viewportEdit").toBool() ||
            (workspaceDescription(workspace->page).preview && workspace->page != int(WorkspacePage::Render))));
    for (auto widget : workspace->mutationWidgets)
        widget->setEnabled(editable);
    findChild<QComboBox *>("viewportAxes")->setEnabled(editable &&
        workspaceDescription(workspace->page).preview);
    if (auto cameraList = findChild<QListWidget *>("savedCameraList"))
    {
        const bool selected = cameraList->currentRow() >= 0;
        if (auto rename = findChild<QPushButton *>("savedCameraRename"))
            rename->setEnabled(editable && selected);
        if (auto remove = findChild<QPushButton *>("savedCameraDelete"))
            remove->setEnabled(editable && selected && cameraList->count() > 1);
        if (auto clear = findChild<QToolButton *>("savedCameraClearSelection"))
            clear->setEnabled(selected);
    }
    for (auto button : findChildren<QPushButton *>())
        if (button->property("resourceAction").toBool())
            button->setEnabled(button->property("resourceSelected").toBool() &&
                               (!button->property("requiresEditing").toBool() || editable));
    inspectorDock->setEnabled(editable || (workspace->page == int(WorkspacePage::Render) && !m_renderPreviewMode));
    // Lists remain browsable while mutations are guarded at their command entry.
    viewport->setEnabled(editable);
    inspector->refresh();
}

void learnQT::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (!workspace)
        return;
    workspace->rail->setIconSize(QSize(height() < 800 ? 18 : 22, height() < 800 ? 18 : 22));
}
bool learnQT::eventFilter(QObject *object, QEvent *event)
{
    if (workspace && !workspace->tips && event->type() == QEvent::ToolTip)
        return true;
    return QMainWindow::eventFilter(object, event);
}
