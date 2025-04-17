#ifndef GLWIDGET_H
#define GLWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>

#include <QMutex>
#include <memory>
#include <qtimer.h>
#include "Scene.h"

class RenderThread;

extern QMutex param_mutex;

class GLWidget : public QOpenGLWidget,protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT
public:
    GLWidget(QWidget *parent = nullptr);
    ~GLWidget() override;
    void sendM();

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

    void keyPressEvent(QKeyEvent* event) Q_DECL_OVERRIDE;
    void keyReleaseEvent(QKeyEvent* event) Q_DECL_OVERRIDE;
    void mousePressEvent(QMouseEvent* event) Q_DECL_OVERRIDE;
    void mouseReleaseEvent(QMouseEvent* event) Q_DECL_OVERRIDE;
    void mouseMoveEvent(QMouseEvent* event) Q_DECL_OVERRIDE;
    void wheelEvent(QWheelEvent* event) Q_DECL_OVERRIDE;

private:
    void initRenderThread();

private:
    unsigned m_vao = 0;
    unsigned m_vbo = 0;
    std::unique_ptr<QOpenGLShaderProgram> m_program;
    RenderThread *m_thread = nullptr;

    bool m_bLeftPressed;
    QPoint m_lastPos;
signals:
    //给子线程发消息
    void sengMsgToThread();
};

#endif // GLWIDGET_H
