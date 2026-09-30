#pragma once
#include <QAction>
#include <QComboBox>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QToolBar>
#include <QTabWidget>
#include <QPushButton>
#include <functional>

struct WorkspacePageDescription
{
    int id;
    const char *key, *title, *icon;
    int view;
    bool preview;
};

inline const QVector<WorkspacePageDescription> &workspacePages()
{
    static const QVector<WorkspacePageDescription> pages{
        {0, "home", "首页", "home", 2, false},
        {1, "scene", "场景", "scene", 0, true},
        {2, "material", "材质", "material", 0, true},
        {3, "lighting", "照明", "light", 0, true},
        {4, "camera", "相机", "camera", 0, true},
        {6, "render", "渲染", "play", 0, false},
        {7, "resources", "资源", "assets", 3, false},
        {8, "settings", "设置", "settings", 4, false}};
    return pages;
}

inline const WorkspacePageDescription &workspaceDescription(int id)
{
    for (const auto &page : workspacePages())
        if (page.id == id)
            return page;
    return workspacePages().first();
}

inline QString workspaceLayoutKey(int page, bool composition)
{
    const QString key = QString::fromLatin1(workspaceDescription(page).key);
    return page == 6 ? key + (composition ? "/composition" : "/results") : key;
}

// UI-only state. SceneDocument and the renderer remain the source of scene truth.
struct WorkspaceUi
{
    int page = -1;
    bool navigating = false, refreshing = false, pendingRender = false;
    bool welcome = true, tips = true, status = true;
    int recentLimit = 12;
    QStringList recent;
    QString selectedCameraId;
    QMap<QString, QByteArray> layouts;
    QString activeLayoutKey;
    QMap<int, QAction *> navigation;
    QMap<int, QWidget *> leftPages, rightPages, bottomPages;
    QDockWidget *left = nullptr, *bottom = nullptr;
    QStackedWidget *leftStack = nullptr, *rightStack = nullptr, *bottomStack = nullptr;
    QToolBar *rail = nullptr;
    QWidget *renderModes = nullptr;
    QTabWidget *lightingTabs = nullptr;
    QWidget *environmentProperties = nullptr, *lightProperties = nullptr;
    QWidget *outputProperties = nullptr, *resultProperties = nullptr;
    QLabel *taskProperties = nullptr, *queueBadge = nullptr, *cameraName = nullptr;
    QPushButton *compositionMode = nullptr, *resultsMode = nullptr;
    QAction *runQueue = nullptr, *stopQueue = nullptr;
    QWidget *home = nullptr, *resources = nullptr, *preferences = nullptr;
    QListWidget *materials = nullptr, *lights = nullptr, *recentList = nullptr, *presets = nullptr;
    QLineEdit *materialSearch = nullptr;
    QComboBox *materialFilter = nullptr;
    QLabel *materialContext = nullptr, *project = nullptr;
    QTableWidget *task = nullptr;
    QSize taskSize;
    int taskTarget = 0, taskSamples = 0;
    QElapsedTimer taskClock;
    QStandardItemModel *catalog = nullptr;
    QByteArray catalogSignature;
    QStringList hdrFiles;
    QList<QWidget *> mutationWidgets;
    QList<std::function<void()>> refreshers;
    std::function<void()> refreshCamera;
    QByteArray baseline;
};
