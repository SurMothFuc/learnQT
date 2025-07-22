#pragma once
#ifndef QTFUNCTIONWIDGET_H
#define QTFUNCTIONWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShader>
#include <QOpenGLShaderProgram>
#include <qtimer.h>

class QtFunctionWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
public:
    QtFunctionWidget(QWidget* parent = nullptr);
    ~QtFunctionWidget();

protected:
    virtual void initializeGL();
    virtual void resizeGL(int w, int h);
    virtual void paintGL();

private:
    QOpenGLShaderProgram shaderProgram;
    QTimer* m_pTimer = nullptr;
};

#endif // QTFUNCTIONWIDGET_H
