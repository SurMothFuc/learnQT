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
#include <functional>

// UI-only state. SceneDocument and the renderer remain the source of scene truth.
struct WorkspaceUi
{
    int page = -1;
    bool navigating = false, refreshing = false, pendingRender = false;
    bool welcome = true, tips = true, status = true;
    int recentLimit = 12;
    QStringList recent;
    QString selectedCameraId;
    QMap<int, QByteArray> layouts;
    QMap<int, QAction *> navigation;
    QMap<int, QWidget *> leftPages, rightPages, bottomPages;
    QDockWidget *left = nullptr, *bottom = nullptr;
    QStackedWidget *leftStack = nullptr, *rightStack = nullptr, *bottomStack = nullptr;
    QToolBar *rail = nullptr;
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
