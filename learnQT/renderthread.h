#ifndef RENDERTHREAD_H
#define RENDERTHREAD_H

#include <QThread>
#include <QImage>
#include <QMutex>
#include <QOpenGLFunctions>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include "renderer.h"
#include "texturebuffer.h"
#include <QDebug>
#include <QOpenGLContext>
#include <memory>
#include <QTimer>
#include <iostream>

class RenderThread : public QThread, public QOpenGLFunctions
{
    Q_OBJECT

public:
    RenderThread(QSurface *surface, QOpenGLContext *mainContext, QObject *parent = nullptr);
    ~RenderThread();

    void setNewSize(int width, int height);
    

signals:
    void imageReady();

public slots:
    //接收主线程的消息
    void recMegFromMain();

protected:
    void run() override;

private:
    RenderThread(const RenderThread &) = delete;
    RenderThread &operator =(const RenderThread &) = delete;
    RenderThread(const RenderThread &&) = delete;
    RenderThread &operator =(const RenderThread &&) = delete;
    

private:
    bool m_running = true;

    int m_width = 100;
    int m_height = 100;
    QMutex m_mutex;

    QOpenGLContext *m_mainContext;
    QOpenGLContext *m_renderContext = nullptr;
    QSurface *m_surface;
};

#endif // RENDERTHREAD_H
