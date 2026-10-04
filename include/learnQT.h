#pragma once
#include "SceneTreeModel.h"
#include "WorkbenchPanels.h"
#include "glwidget.h"
#include "RenderQueueThread.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
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
    // Preserve legacy numeric page IDs; Lights/Environment are navigation aliases.
    enum class WorkspacePage { Home = 0, Scene = 1, Material = 2, Lighting = 3,
                               Camera = 4, Environment = 5, Render = 6,
                               Resources = 7, Settings = 8, Lights = 9 };
    explicit learnQT(QWidget *parent = nullptr);
    ~learnQT() override;
    void navigateWorkspace(WorkspacePage page);

  private:
    std::shared_ptr<WorkspaceUi> workspace;
    void setupWorkspace();
    void applyWorkspaceLayout();
    void refreshTaskProperties();
    void refreshWorkspace();
    void syncWorkspaceAvailability();
    void rememberScene(const QString &path);
    void configureWorkspaceRegression();
    void configureMaterialRegression();
    void configureRenderQueueRegression();
    void configurePreviewPanelRegression();
    void configureLargeScenePreviewRegression();
    void configureRasterRegression();
    void configureAaDenoiseRegression();
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
    QComboBox *outputDenoise;
    QCheckBox *outputAntialiasing;
    QSpinBox *outputWidth, *outputHeight, *outputSamples, *outputTile, *outputBounces, *outputRrMinDepth;
    PreviewSettingsPanel *previewChromePanel = nullptr, *previewDetailPanel = nullptr;
    QDialog *previewDialog = nullptr;
    QAction *renderAction, *pauseAction, *stopAction, *undoAction, *redoAction;
    QList<QAction *> editActions;
    QLabel *taskLabel, *statsLabel, *previewBadge = nullptr;
    QProgressBar *progress;
    QImage lastResult;
    RenderJobState jobState = RenderJobState::Idle;
    struct QueueItem
    {
        RenderQueueRequest request;
        QString name, cameraName, format = QStringLiteral("png"),
                status = QStringLiteral("等待中"), error;
        int samples = 0;
        double seconds = 0;
        QImage result;
        RenderResultPtr linear;
    };
    QVector<QueueItem> m_renderQueue;
    RenderQueueThread *m_queueWorker = nullptr;
    quint64 m_nextQueueId = 1, m_activeQueueId = 0;
    quint64 m_viewedTaskId = 0;
    bool m_resultBrowsingPinned = false;
    bool m_queueRunning = false, m_renderPreviewMode = true;
    QString m_queueOutputDirectory;
    QJsonObject m_draftCamera;
    QString m_draftSourceId;
    QComboBox *m_renderCameraChoice = nullptr, *m_renderFormat = nullptr, *m_aspectChoice = nullptr;
    void refreshRenderCameras();
    void addRenderTask();
    void runRenderQueue();
    void dispatchRenderTask();
    void refreshRenderQueue();
    void showRenderTaskResult();
    void setRenderPreviewMode(bool preview);
    void saveDraftCamera();
    void setupWorkbench();
    void setupTree();
    void setupDockTitle(QDockWidget *dock, const QString &icon);
    QWidget *createSettings();
    void commitOutputSettings();
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
    void commitPreviewSettings(const RenderParams::Snapshot &settings);
    // 供界面回归设置交互预览参数，走的正是 UI 的提交路径。
    void applyPreviewSettingsForTesting(const RenderParams::Snapshot &settings)
    {
        commitPreviewSettings(settings);
    }
    void syncPreviewControls(const RenderParams::Snapshot &settings);
    void showPreviewSettingsDialog();
    void configureRegressionCapture();
    void configureUiCapture();
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
