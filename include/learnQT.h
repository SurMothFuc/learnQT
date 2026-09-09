#pragma once
#include "SceneTreeModel.h"
#include "WorkbenchPanels.h"
#include "glwidget.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QTabWidget>
#include <QTreeView>
struct WorkspaceUi;
class learnQT : public QMainWindow
{
    Q_OBJECT
  public:
    enum class WorkspacePage { Home, Scene, Material, Lights, Camera, Environment, Render, Resources, Settings };
    explicit learnQT(QWidget *parent = nullptr);
    ~learnQT() override;
    void navigateWorkspace(WorkspacePage page);

  private:
    std::shared_ptr<WorkspaceUi> workspace;
    void setupWorkspace();
    void refreshWorkspace();
    void syncWorkspaceAvailability();
    void rememberScene(const QString &path);
    void configureWorkspaceRegression();
    EditorController *editor;
    GLWidget *viewport;
    SceneTreeModel *treeModel;
    QSortFilterProxyModel *treeFilter;
    QTreeView *tree;
    ObjectInspector *inspector;
    PerformancePanel *performance;
    ResultView *resultView;
    QTabWidget *views;
    QDockWidget *treeDock, *inspectorDock, *performanceDock, *logDock;
    QPlainTextEdit *log;
    QComboBox *m_sceneList = nullptr;
    QThread *m_loadWorker = nullptr;
    bool m_loading = false, m_sceneDirty = false, m_restoring = false;
    QStringList m_sessionScenes;
    QSet<QString> expanded;
    bool syncingSelection = false;
    QCheckBox *m_denoise, *outputDenoise;
    QSpinBox *outputWidth, *outputHeight, *outputSamples, *outputTile, *outputBounces;
    QSpinBox *previewSamples, *previewTile, *previewBounces;
    QCheckBox *previewTiled, *previewLow;
    QAction *renderAction, *pauseAction, *stopAction, *undoAction, *redoAction;
    QList<QAction *> editActions;
    QLabel *taskLabel, *statsLabel;
    QProgressBar *progress;
    QImage lastResult;
    RenderJobState jobState = RenderJobState::Idle;
    void setupWorkbench();
    void setupTree();
    void setupDockTitle(QDockWidget *dock, const QString &icon);
    QWidget *createSettings();
    void connectRenderThread();
    void startRender();
    void loadModel();
    void importPaths(const QStringList &paths);
    void saveGLImage();
    void beginSceneLoad(const QString &path, bool model = false);
    void refreshScenes();
    void setLoading(bool loading);
    void setSceneDirty();
    bool confirmDiscard();
    bool saveSceneDocument(bool saveAs = false);
    void exportPackage();
    void restoreSceneControls();
    void updateSelection();
    void contextMenu(QPoint position);
    void updateTitle();
    void logMessage(const QString &message);
    void configureRegressionCapture();
    void configureSceneRegression();
    void captureRegressionFrame();
    void configureWorkbenchRegression();
    void configureInteractionRegression();
    void configurePreviewRegression();
    void configurePreviewModeRegression();
    QString m_regressionOutputPath;
    int m_regressionTargetFrames = 12, m_regressionPresentedFrames = 0;
    bool m_regressionCaptureQueued = false, m_validateLanternRegression = false;

  protected:
    void closeEvent(QCloseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
};
