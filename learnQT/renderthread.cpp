﻿#include "renderthread.h"


RenderThread::RenderThread(QSurface *surface, QOpenGLContext *mainContext, QObject *parent)
    : QThread(parent)
    , m_mainContext(mainContext)
    , m_surface(surface)
{
    m_renderContext = new QOpenGLContext;
    m_renderContext->setFormat(m_mainContext->format());
    m_renderContext->setShareContext(m_mainContext);
    m_renderContext->create();
    m_renderContext->moveToThread(this);
}

RenderThread::~RenderThread()
{
    m_running = false;
    wait();
}

// called in UI thread
void RenderThread::setNewSize(int width, int height)
{
    QMutexLocker lock(&m_mutex);
    m_width = width;
    m_height = height;
}

// called in render thread
void RenderThread::run()
{
    //延迟400毫秒在进行渲染，让主窗口调整好大小
    Sleep(400);
    m_renderContext->makeCurrent(m_surface);

    TextureBuffer::instance()->createTexture(m_renderContext);

    Renderer renderer(m_width, m_height);

    point_render = &renderer;

    while (m_running)
    {
        //qDebug()<<1;
        int width = 0;
        int height = 0;
        {
            QMutexLocker lock(&m_mutex);
            width = m_width;
            height = m_height;
        }
        renderer.render(width, height);
        TextureBuffer::instance()->updateTexture(m_renderContext, width, height);
        emit imageReady();
    }

    TextureBuffer::instance()->deleteTexture(m_renderContext);
}
void RenderThread::recMegFromMain()
{
    //qDebug() << "update parameters" ;
    if (point_render == NULL)
        return;
    point_render->needupdate = true;
}