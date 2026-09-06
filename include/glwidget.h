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
    Camera camera;
    quint64 sceneVersion() const
    {
        return version;
    }
  signals:
    void framePresented();
    void sceneEdited();
    void toolChanged(int tool);
    void renderThreadReady();

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
    bool selectionDirty = true, orbit = false, pan = false;
    QPoint lastPos, dragStart;
    quint64 version = 1, pickSerial = 0;
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
    void updateEditorOverlay();
};
