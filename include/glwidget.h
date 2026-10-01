#pragma once
#include "EditorController.h"
#include "renderthread.h"
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
extern QMutex param_mutex;
class GLWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT
  public:
    explicit GLWidget(QWidget *parent = nullptr);
    ~GLWidget() override;
    void attachEditor(EditorController *editor);
    void markSceneDirty(SceneDirtyFlags flags);
    void markSceneDirty(SceneDirtyFlag flag);
    void replaceScene(Scene &prepared);
    void submitPrepared(std::shared_ptr<Scene> scene);
    RenderThread *renderThread() const
    {
        return thread;
    }
    enum Tool
    {
        Select,
        Translate,
        Rotate,
        Scale
    };
    Tool tool = Select;
    bool localAxes = false, snap = false;
    void setLocalAxes(bool local)
    {
        localAxes = local;
        updateEditorOverlay();
    }
    double moveStep = .1, rotateStep = 15, scaleStep = .1;
    void setTool(Tool value)
    {
        cancelDrag();
        tool = value;
        updateEditorOverlay();
        emit toolChanged(int(tool));
    }
    void frameSelection();
    void setCompositionMode(bool active, const Camera &draft = Camera(), QSize aspect = {});
    void setCompositionAspect(QSize aspect);
    Camera camera;
    quint64 displayedNewFrames() const { return displayedFrames; }
    double displayedNewFps() const { return displayRateClock.isValid() && displayRateClock.elapsed()<2000 ? displayedFps : 0; }
    quint64 sceneVersion() const
    {
        return version;
    }
    // 描边绘制统计：只有 outstandingStaleIdsFrames() 是可观测的缺陷信号——它是「槽明明是最新版本、
    // 但 ID 图还是上一次拾取重绘的结果」却把描边放行的次数。相机拖动中允许出现
    // staleHiddenFrames()（描边被隐藏），但绝不允许前者。
    quint64 staleHiddenFrames() const
    {
        return staleHiddenCount;
    }
    quint64 overlayDrawnFrames() const
    {
        return overlayDrawnCount;
    }
    quint64 outstandingStaleIdsFrames() const
    {
        return outstandingStaleCount;
    }
    void resetOverlayCounters()
    {
        staleHiddenCount = overlayDrawnCount = outstandingStaleCount = 0;
    }
  signals:
    void framePresented();
    void freshFramePresented();
    void sceneEdited();
    void toolChanged(int tool);
    void renderThreadReady();
    void compositionCameraChanged();

  protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int, int) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;

  private:
    EditorController *editor = nullptr;
    RenderThread *thread = nullptr;
    GLuint vao = 0, vbo = 0, selectionBuffer = 0, selectionTexture = 0;
    std::unique_ptr<QOpenGLShaderProgram> program;
    QWidget *overlay = nullptr;
    bool compositionMode = false;
    QSize compositionAspect;
    QRect compositionFrame() const;
    bool selectionDirty = true, orbit = false, pan = false;
    QPoint lastPos, dragStart;
    quint64 version = 1, pickSerial = 0;
    // Completed interactive images may lag edits, but never cross scene/size replacement.
    quint64 minimumDisplayVersion = 1;
    quint64 lastPresentationSerial = 0, displayedFrames = 0, displayWindowFrames = 0;
    QElapsedTimer displayRateClock;
    double displayedFps = 0;
    bool pickCtrl = false;
    int dragAxis = -1;
    QVector3D dragCenter, dragDirection;
    float dragWorldSize = 1;
    QMap<QString, QMatrix4x4> dragBefore, dragCurrent;
    QPointF project(const QVector3D &p, bool *visible = nullptr) const;
    QVector3D axis(int i) const;
    SceneBounds selectedBounds() const;
    float gizmoSize() const;
    void drawOverlay(QPainter &painter);
    void cancelDrag();
    void publishCamera();
    bool hasSelection = false;
    quint64 staleHiddenCount = 0, overlayDrawnCount = 0, outstandingStaleCount = 0;
    void updateEditorOverlay();
};
