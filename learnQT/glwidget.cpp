#include "glwidget.h"
#include "texturebuffer.h"
#include "renderthread.h"
//#include "debug.h"
//#include "fpscounter.h"

#include <QOpenGLContext>
#include <QDebug>
#include <QWindow>
#include <QSurface>

#include <iostream>

QMutex param_mutex;

namespace
{
    float vertices[] =
    {
        -1.0f, -1.0f, 0.0f, 0.0f,
        -1.0f, 1.0f, 0.0f, 1.0f,
        1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, -1.0f, 1.0f, 0.0f,
    };
}

GLWidget::GLWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
}

GLWidget::~GLWidget()
{
}

void GLWidget::sendM()
{
    emit sengMsgToThread();
}

void GLWidget::initializeGL()
{
    initRenderThread();

    qDebug() << "initializeOpenGLFunctions:" << initializeOpenGLFunctions();

//    glEnable(GL_DEBUG_OUTPUT);
//    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
//    glDebugMessageCallback(glDebugOutput, nullptr);

    char vertexShaderSource[] =
            "#version 330 core\n"
            "layout (location = 0) in vec2 vPos;\n"
            "layout (location = 1) in vec2 texCoord;\n"
            "out vec2 TexCoord;\n"
            "void main()\n"
            "{\n"
            "   gl_Position = vec4(vPos, 0.0, 1.0);\n"
            "   TexCoord = texCoord;\n"
            "}\n";
    char fragmentShaderSource[] =
            "#version 330 core\n"
            "out vec4 FragColor;\n"
            "in vec2 TexCoord;\n"
            "uniform sampler2D ourTexture;\n"
            "void main()\n"
            "{\n"
            "   FragColor = texture(ourTexture, TexCoord);\n"
            "}\n";

    m_program.reset(new QOpenGLShaderProgram);
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);
    m_program->link();

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void *>(0));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void *>(2 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    QTimer* m_pTimer = new QTimer(this);
    connect(m_pTimer, &QTimer::timeout, this, [=] {
        });
    m_pTimer->start(50);

    //glCheckError();
}

void GLWidget::paintGL()
{
   // RAIITimer t("GLWidget::paintGL");

    glViewport(0, 0, width(), height());

    m_program->bind();

    glBindVertexArray(m_vao);
    if (TextureBuffer::instance()->ready())
    {
        TextureBuffer::instance()->drawTexture(QOpenGLContext::currentContext(), sizeof(vertices) / sizeof(float) / 4);
    }
    glBindVertexArray(0);

    m_program->release();

  //  FpsCounter::instance()->frame(FpsCounter::Display);
}

void GLWidget::resizeGL(int w, int h)
{
    m_thread->setNewSize(w, h);
    qDebug() << "frame size:" << w << h;
}

void GLWidget::initRenderThread()
{
    auto context = QOpenGLContext::currentContext();
    auto mainSurface = context->surface();

    auto renderSurface = new QOffscreenSurface(nullptr, this);
    renderSurface->setFormat(context->format());
    renderSurface->create();

    context->doneCurrent();
    m_thread = new RenderThread(renderSurface, context, this);
    context->makeCurrent(mainSurface);

    connect(m_thread, &RenderThread::imageReady, this, [this](){
        update();
    }, Qt::QueuedConnection);
    m_thread->start();

    connect(this, &GLWidget::sengMsgToThread, m_thread, &RenderThread::recMegFromMain);
    
}

void GLWidget::keyPressEvent(QKeyEvent* event)
{
    int key = event->key();
    if (key >= 0 && key < 1024) {
        param_mutex.lock();
        {
            Pass_parameters::getInstance().camera.keys[key] = true;
            Pass_parameters::getInstance().camera.processInput(1.0f);
        }
        param_mutex.unlock();
        sendM();
    }
}

void GLWidget::keyReleaseEvent(QKeyEvent* event)
{
    int key = event->key();
    if (key >= 0 && key < 1024) {
        param_mutex.lock();
        {
            Pass_parameters::getInstance().camera.keys[key] = false;
        }
        param_mutex.unlock();
    }
}

void GLWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_bLeftPressed = true;
        m_lastPos = event->pos();
    }
}

void GLWidget::mouseReleaseEvent(QMouseEvent* event)
{
    Q_UNUSED(event);
    m_bLeftPressed = false;
}

void GLWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_bLeftPressed) {
        int xpos = event->pos().x();
        int ypos = event->pos().y();

        int xoffset = xpos - m_lastPos.x();
        int yoffset = m_lastPos.y() - ypos;
        m_lastPos = event->pos();
        param_mutex.lock();
       {
            Pass_parameters::getInstance().camera.processMouseMovement(xoffset, yoffset);
            //qDebug() << param.camera.yaw << "    " << param.camera.picth << "    " ;
       }
       param_mutex.unlock();
       sendM();
    }
}

void GLWidget::wheelEvent(QWheelEvent* event)
{
    QPoint offset = event->angleDelta();
    param_mutex.lock();
    {
        Pass_parameters::getInstance().camera.processMouseScroll(offset.y());
    }
    param_mutex.unlock();
    sendM();
}
