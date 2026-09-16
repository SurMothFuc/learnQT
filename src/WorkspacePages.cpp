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
#include <QVBoxLayout>
#include <cmath>

namespace
{
enum ResourceRole
{
    KindRole = Qt::UserRole + 1,
    SourceRole,
    IdRole
};
QStringList pageNames()
{
    return {"首页", "场景", "材质", "灯光", "相机", "环境", "渲染", "资源", "设置"};
}
QStringList pageIcons()
{
    return {"home", "scene", "material", "light", "camera", "environment", "play", "assets", "settings"};
}
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
    auto box = group(title + " · 未开放");
    box->setProperty("unavailable", true);
    box->layout()->addWidget(label(description));
    for (const auto &text : controls)
    {
        auto b = new QPushButton(text);
        b->setEnabled(false);
        b->setToolTip("此功能尚未接入，当前操作不会改变场景。");
        box->layout()->addWidget(b);
    }
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
class MaterialGraphPlaceholder : public QWidget
{
  public:
    explicit MaterialGraphPlaceholder(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(280, 120);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor("#111c26"));
        p.setPen(QColor("#1c2a38"));
        for (int x = 0; x < width(); x += 20)
            p.drawLine(x, 0, x, height());
        for (int y = 0; y < height(); y += 20)
            p.drawLine(0, y, width(), y);
        qreal cardWidth = qMin(160., width() * .24), y = height() * .5 - 36;
        QVector<QRectF> cards;
        for (int i = 0; i < 3; ++i)
            cards << QRectF(14 + i * (width() - cardWidth - 28) / 2., y, cardWidth, 74);
        p.setPen(QPen(QColor("#91a3b7"), 1.4));
        for (int i = 0; i < 2; ++i)
        {
            QPointF a(cards[i].right(), y + 47), b(cards[i + 1].left(), y + 47);
            QPainterPath path(a);
            path.cubicTo(a + QPointF(32, 0), b - QPointF(32, 0), b);
            p.drawPath(path);
        }
        for (int i = 0; i < 3; ++i)
        {
            p.setPen(QColor("#53677b"));
            p.setBrush(QColor("#223141"));
            p.drawRoundedRect(cards[i], 5, 5);
            p.setPen(QColor("#dbe6f2"));
            p.drawText(cards[i].adjusted(10, 5, -5, -40), Qt::AlignVCenter,
                       QStringList{"纹理贴图", "PBR 材质", "材质输出"}[i]);
            p.setPen(QColor("#94a6ba"));
            p.drawText(cards[i].adjusted(10, 34, -5, -5), Qt::AlignVCenter,
                       QStringList{"颜色 / 法线", "基础色 / 粗糙度", "表面"}[i]);
            p.setBrush(QColor("#69abf6"));
            p.setPen(Qt::NoPen);
            if (i < 2)
                p.drawEllipse(QPointF(cards[i].right(), y + 47), 3, 3);
            if (i > 0)
                p.drawEllipse(QPointF(cards[i].left(), y + 47), 3, 3);
        }
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
    row->addWidget(kind);
    row->addWidget(search, 1);
    layout->addLayout(row);
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
    layout->addWidget(list, 1);
    auto detail = label("选择资源查看来源；双击执行对应操作。");
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
    else
        footer->addWidget(detail, 1);
    footer->addWidget(action);
    layout->addLayout(footer);
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
            detail->setText(i.isValid() ? i.data().toString() + "\n" + i.data(SourceRole).toString()
                                        : "没有选中的资源");
            bool mutates = kind == "场景" || kind == "模型" || kind == "HDR";
            action->setProperty("requiresEditing", mutates);
            action->setProperty("resourceSelected", i.isValid());
            action->setEnabled(i.isValid() &&
                               (!mutates || panel->window()->property("workspaceEditable").toBool()));
            action->setText(kind == "场景"   ? "打开场景"
                            : kind == "模型" ? "导入模型"
                            : kind == "HDR"  ? "应用环境"
                                             : "定位引用");
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
    for (int i = 0; i < 9; ++i)
    {
        auto state = settings.value(QString("workspaceV4/layout/%1").arg(i)).toByteArray();
        if (!state.isEmpty())
            w.layouts[i] = state;
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

    auto renderPanel = new QWidget;
    auto renderLayout = column(renderPanel);
    renderLayout->addWidget(label("路径追踪 · OpenGL 3.3\n当前上下文设备；最终降噪使用 CPU OIDN。"));
    // 交互预览设置已移出本页：入口在顶栏“预览设置”的弹出面板与详情弹窗。
    for (auto key : {"outputSection", "displaySection"})
        renderLayout->addWidget(settingsSection(key));
    auto exports = new QPushButton("导出当前结果 · PNG / JPEG…");
    connect(exports, &QPushButton::clicked, this, &learnQT::saveGLImage);
    renderLayout->addWidget(exports);
    renderLayout->addWidget(unavailable("渲染扩展", "当前支持单张、不透明的 PNG / JPEG 输出。",
                                        {"AOV / EXR / 动画", "自适应采样 / 渲染器切换"}));
    renderLayout->addStretch();
    w.rightPages[6] = scrolling(renderPanel);
    w.rightStack->addWidget(w.rightPages[6]);
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
    w.mutationWidgets << environment;

    auto cameraPanel = new QWidget;
    auto cameraLayout = column(cameraPanel);
    auto basic = new QGroupBox("当前相机 · 针孔透视");
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
    cameraLayout->addWidget(unavailable("镜头与构图", "只使用当前场景中的单相机，不保存演示相机。",
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
              "打开 · Ctrl+S 保存 · F12 渲染\n输入框保留文本编辑快捷键。"));
    prefSections << shortcuts;
    prefSections << unavailable("性能", "使用当前 OpenGL 上下文设备；尚无设备切换或显存预算控制。",
                                {"GPU 选择 / 缓存预算"});
    auto paths = group("路径");
    paths->layout()->addWidget(
        label("模型、纹理和 HDR 使用文件选择器导入；场景保存及图片导出时选择目标位置。"));
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
        s.remove("workspaceV4/layout");
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
    auto nodes = new QWidget;
    auto nodeLayout = column(nodes);
    nodeLayout->addWidget(label("结构示意 · 未开放，不参与渲染。独立材质球预览尚未接入。"));
    nodeLayout->addWidget(new MaterialGraphPlaceholder, 1);
    auto addNode = new QPushButton("添加节点 · 未开放");
    addNode->setEnabled(false);
    nodeLayout->addWidget(addNode);
    w.bottomPages[2] = nodes;

    auto lightListPanel = new QWidget;
    auto lightListLayout = column(lightListPanel);
    lightListLayout->addWidget(label("场景灯光", "sectionHeading"));
    w.lights = new QListWidget;
    w.lights->setObjectName("workspaceLights");
    lightListLayout->addWidget(w.lights, 1);
    lightListLayout->addWidget(label("在右侧添加太阳盘或球形光，并调整真实辐亮度。"));
    auto oldLightList = lightContent->findChild<QComboBox *>();
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
    connect(oldLightList, QOverload<int>::of(&QComboBox::activated), this, [this, oldLightList](int) {
        QSignalBlocker b(workspace->lights);
        for (int i = 0; i < workspace->lights->count(); ++i)
            if (workspace->lights->item(i)->data(Qt::UserRole) == oldLightList->currentData())
                workspace->lights->setCurrentRow(i);
    });
    w.leftPages[3] = lightListPanel;
    auto cameraList = new QWidget;
    auto cameraListLayout = column(cameraList);
    cameraListLayout->addWidget(label("场景相机", "sectionHeading"));
    auto currentCamera = new QListWidget;
    addCard(currentCamera, "当前相机", "camera", "");
    currentCamera->setCurrentRow(0);
    cameraListLayout->addWidget(currentCamera, 1);
    cameraListLayout->addWidget(unavailable("多相机", "当前文档仅保存一台相机。", {"新建相机"}));
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
    w.bottomPages[5] = resourceBrowser(w.catalog, "HDR", activateResource, "environmentResources", true);
    w.leftPages[5] = resourceBrowser(w.catalog, "HDR", activateResource, "environmentLibrary", true);
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
    for (auto a : {renderAction, pauseAction, stopAction})
    {
        auto button = new QToolButton;
        button->setDefaultAction(a);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        taskControls->addWidget(button);
    }
    taskControls->addStretch();
    auto queue = new QPushButton("添加到队列 · 未开放");
    queue->setEnabled(false);
    taskControls->addWidget(queue);
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
    taskLayout->addWidget(label("当前单任务 · 上次完成结果可在结果工具栏查看；历史队列与前后对比未开放。"));
    w.bottomPages[6] = taskPanel;
    connect(viewport, &GLWidget::renderThreadReady, this, [this] {
        connect(viewport->renderThread(), &RenderThread::statsReady, this, [this](const RenderStats &s) {
            if (!editor->renderLocked || s.target <= 0 || s.size != workspace->taskSize)
                return;
            auto t = workspace->task;
            workspace->taskSamples = s.samples;
            t->item(0, 2)->setText(QString("%1 / %2 spp").arg(s.samples).arg(workspace->taskTarget));
            t->item(0, 3)->setText(renderJobText(s.state));
            t->item(0, 4)->setText(QString::number(s.jobSeconds, 'f', 1) + " 秒");
        });
        connect(viewport->renderThread(), &RenderThread::jobStateChanged, this,
                [this](RenderJobState state, const QString &) {
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
    const QStringList titles{"新建场景", "打开项目", "从预设创建"};
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
    for (int i = 0; i < 9; ++i)
    {
        if (i == 8)
        {
            auto spacer = new QWidget;
            spacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
            w.rail->addWidget(spacer);
        }
        auto a = w.rail->addAction(WorkbenchStyle::icon(pageIcons()[i]), pageNames()[i]);
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
        dock->setMinimumWidth(dock == inspectorDock || dock == performanceDock ? 340 : 205);
        setupDockTitle(dock, dock == w.bottom ? "assets" : "settings");
    }
    performance->setMinimumHeight(280);
    w.bottom->setMinimumHeight(140);
    w.left->setMaximumWidth(420);
    auto panelMenu = menuBar()->addMenu("面板");
    for (auto dock : {treeDock, inspectorDock, performanceDock, w.left, w.bottom, logDock})
    {
        auto action = dock->toggleViewAction();
        panelMenu->addAction(action);
        connect(panelMenu, &QMenu::aboutToShow, this, [this, dock, action] {
            int page = workspace->page;
            action->setEnabled(dock == logDock ||
                               (dock == treeDock || dock == performanceDock ? page == 1
                                : dock == workspace->left   ? workspace->leftPages.contains(page)
                                : dock == workspace->bottom ? workspace->bottomPages.contains(page)
                                                            : workspace->rightPages.contains(page)));
        });
    }
    w.baseline = saveState(4);
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
    if (!workspace || workspace->navigating)
        return;
    auto &w = *workspace;
    int target = int(page);
    if (w.page == target)
        return;
    w.navigating = true;
    if (w.page >= 0)
        w.layouts[w.page] = saveState(4);
    w.page = target;
    // Cancel incomplete manipulation before hiding its input surface.
    viewport->setTool(viewport->tool);
    restoreState(w.baseline, 4);
    for (auto dock : {treeDock, inspectorDock, performanceDock, logDock, w.left, w.bottom})
        dock->hide();
    bool scene = target == 1;
    if (w.rightPages.contains(target))
    {
        w.rightStack->setCurrentWidget(w.rightPages[target]);
        inspectorDock->show();
    }
    if (w.leftPages.contains(target))
    {
        w.leftStack->setCurrentWidget(w.leftPages[target]);
        w.left->show();
    }
    if (w.bottomPages.contains(target))
    {
        w.bottomStack->setCurrentWidget(w.bottomPages[target]);
        w.bottom->show();
    }
    treeDock->setVisible(scene);
    performanceDock->setVisible(scene);
    inspector->setMaterialPage(target == 2);
    if (target == 2)
        inspector->browseMaterial(w.materials->currentItem()
                                      ? w.materials->currentItem()->data(Qt::UserRole).toString()
                                      : QString());
    inspectorDock->setWindowTitle(pageNames()[target] + "属性");
    w.left->setWindowTitle(target == 2   ? "场景材质"
                           : target == 3 ? "灯光列表"
                           : target == 4 ? "相机列表"
                                         : "环境资源");
    w.bottom->setWindowTitle(target == 2   ? "材质节点编辑器 · 未开放"
                             : target == 6 ? "当前渲染任务"
                                           : "资源浏览器");
    for (auto dock : {inspectorDock, w.left, w.bottom})
        setupDockTitle(dock, target == 6 ? "play" : "settings");
    views->setCurrentIndex(target == 0 ? 2 : target == 7 ? 3 : target == 8 ? 4 : target == 6 ? 1 : 0);
    resizeDocks({treeDock, inspectorDock, w.left}, {245, 360, 250}, Qt::Horizontal);
    resizeDocks({performanceDock, inspectorDock}, {280, 500}, Qt::Vertical);
    if (scene)
    {
        w.bottom->show();
        resizeDocks({w.bottom}, {185}, Qt::Vertical);
    }
    resizeDocks({w.bottom}, {target == 2 ? 200 : 185}, Qt::Vertical);
    if (w.layouts.contains(target))
        restoreState(w.layouts[target], 4);
    if (width() < 1450 && target != 6)
        w.bottom->hide();
    w.navigation[target]->setChecked(true);
    if (viewport->renderThread())
        viewport->renderThread()->setPreviewVisible(target >= 1 && target <= 5);
    w.navigating = false;
    w.refreshCamera();
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
    auto light = w.lights->currentItem() ? w.lights->currentItem()->data(Qt::UserRole).toString() : QString();
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
    auto signature = QJsonDocument(signatureParts).toJson(QJsonDocument::Compact);
    if (w.catalogSignature != signature)
    {
        w.catalogSignature = signature;
        w.presets->clear();
        w.catalog->clear();
        auto add = [&w](const QString &kind, const QString &title, const QString &source, const QString &id,
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
    for (auto widget : workspace->mutationWidgets)
        widget->setEnabled(editable);
    for (auto button : findChildren<QPushButton *>())
        if (button->property("resourceAction").toBool())
            button->setEnabled(button->property("resourceSelected").toBool() &&
                               (!button->property("requiresEditing").toBool() || editable));
    inspectorDock->setEnabled(editable);
    // Lists remain browsable while mutations are guarded at their command entry.
    viewport->setEnabled(editable);
    inspector->refresh();
}

void learnQT::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (!workspace)
        return;
    if (!workspace->navigating && width() < 1450 && workspace->page != 6)
        workspace->bottom->hide();
    workspace->rail->setIconSize(QSize(height() < 800 ? 18 : 22, height() < 800 ? 18 : 22));
}
bool learnQT::eventFilter(QObject *object, QEvent *event)
{
    if (workspace && !workspace->tips && event->type() == QEvent::ToolTip)
        return true;
    return QMainWindow::eventFilter(object, event);
}
