#ifndef GLWIDGET_H
#define GLWIDGET_H

#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>

#include <QMutex>
#include <memory>
#include <qtimer.h>

#include "Scene.h"
#include "SceneDirty.h"

class RenderThread;

extern QMutex param_mutex;

class GLWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT
public:
    GLWidget(QWidget *parent = nullptr);
    ~GLWidget() override;

    // UI 线程只通过 dirty 标记通知渲染线程同步 Scene。
    void markSceneDirty(SceneDirtyFlags flags);
    void markSceneDirty(SceneDirtyFlag flag);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void initRenderThread();

private:
    unsigned m_vao = 0;
    unsigned m_vbo = 0;
    std::unique_ptr<QOpenGLShaderProgram> m_program;
    RenderThread *m_thread = nullptr;

    bool m_bLeftPressed = false;
    QPoint m_lastPos;
};

#endif // GLWIDGET_H
