#include "glwidget.h"

#include "texturebuffer.h"
#include "renderthread.h"

#include <QDebug>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QSurface>
#include <QWindow>

#include "RenderParams.h"

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

void GLWidget::markSceneDirty(SceneDirtyFlags flags)
{
    if (m_thread == nullptr) {
        return;
    }
    m_thread->markSceneDirty(flags);
}

void GLWidget::markSceneDirty(SceneDirtyFlag flag)
{
    markSceneDirty(toSceneDirtyFlags(flag));
}

void GLWidget::initializeGL()
{
    initRenderThread();

    qDebug() << "initializeOpenGLFunctions:" << initializeOpenGLFunctions();

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
}

void GLWidget::paintGL()
{
    glViewport(0, 0, width(), height());

    m_program->bind();

    glBindVertexArray(m_vao);
    if (TextureBuffer::instance()->ready())
    {
        TextureBuffer::instance()->drawTexture(QOpenGLContext::currentContext(), sizeof(vertices) / sizeof(float) / 4);
    }
    glBindVertexArray(0);

    m_program->release();
}

void GLWidget::resizeGL(int w, int h)
{
    if (m_thread != nullptr) {
        m_thread->setNewSize(w, h);
    }
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
}

void GLWidget::keyPressEvent(QKeyEvent* event)
{
    const int key = event->key();
    if (key >= 0 && key < 1024) {
        QMutexLocker lock(&param_mutex);
        Scene::getInstance().camera.keys[key] = true;
        Scene::getInstance().camera.processInput(1.0f);
        lock.unlock();
        markSceneDirty(SceneDirtyFlag::Camera);
    }
}

void GLWidget::keyReleaseEvent(QKeyEvent* event)
{
    const int key = event->key();
    if (key >= 0 && key < 1024) {
        QMutexLocker lock(&param_mutex);
        Scene::getInstance().camera.keys[key] = false;
    }
}

void GLWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_bLeftPressed = true;
        m_lastPos = event->pos();
        RenderParams::instance().setRenderLow(true);
    }
    if (event->button() == Qt::MiddleButton) {
        m_bMiddlePressed = true;
        m_lastPos = event->pos();
        RenderParams::instance().setRenderLow(true);
    }
}

void GLWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_bLeftPressed = false;
    }
    if (event->button() == Qt::MiddleButton) {
        m_bMiddlePressed = false;
    }
    if (!m_bLeftPressed && !m_bMiddlePressed) {
        RenderParams::instance().setRenderLow(false);
    }
}

void GLWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_bLeftPressed || m_bMiddlePressed) {
        const int xpos = event->pos().x();
        const int ypos = event->pos().y();

        const int xoffset = xpos - m_lastPos.x();
        const int yoffset = m_lastPos.y() - ypos;
        m_lastPos = event->pos();

        QMutexLocker lock(&param_mutex);
        if (m_bLeftPressed) {
            Scene::getInstance().camera.processMouseMovement(xoffset, yoffset);
        }
        if (m_bMiddlePressed) {
            Scene::getInstance().camera.processMousePan(xoffset, yoffset);
        }
        lock.unlock();
        markSceneDirty(SceneDirtyFlag::Camera);
    }
}

void GLWidget::wheelEvent(QWheelEvent* event)
{
    const QPoint offset = event->angleDelta();
    QMutexLocker lock(&param_mutex);
    Scene::getInstance().camera.processMouseScroll(offset.y());
    lock.unlock();
    markSceneDirty(SceneDirtyFlag::Camera);
}
