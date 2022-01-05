#include "renderer.h"
//#include "debug.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QtMath>
#include <QMatrix4x4>
#include <iostream>
static GLuint VBO, VAO, EBO;

Renderer::Renderer(QObject *parent)
    : QObject(parent)
{  
    init();
}

Renderer::~Renderer()
{
    uninit();
}

void Renderer::render(int width, int height)
{
   // RAIITimer t("Renderer::render");
    if (m_width != width || m_height != height)
    {
        m_width = width;
        m_height = height;
        adjustSize();
    }

   /* static float degree = 0.0f;
    degree += 1.0f;

    QMatrix4x4 rotate;
    rotate.setToIdentity();
    rotate.rotate(degree, 0, 0, 1);*/

    glViewport(m_viewportX, m_viewportY, m_viewportWidth, m_viewportHeight);

    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    m_program->bind();
    {
        int xOffsetLocation = m_program->uniformLocation("xOffset");
        glUniform1f(xOffsetLocation, m_uniformValue);
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    m_program->release();
    glFinish();
}

void Renderer::init()
{
    initializeOpenGLFunctions();

//    glEnable(GL_DEBUG_OUTPUT);
//    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
//    glDebugMessageCallback(glDebugOutput, nullptr);

    qDebug() << reinterpret_cast<const char *>(glGetString(GL_VERSION));

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenRenderbuffers(1, &m_rbo);

    adjustSize();

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_rbo);
    m_program.reset(new QOpenGLShaderProgram);

    //之前的操作应该是绑定纹理


    bool success = m_program->addShaderFromSourceFile(QOpenGLShader::Vertex, "./triangle.vert");
    if (!success) {
        qDebug() << "shaderProgram addShaderFromSourceFile failed!" << m_program->log();
        return;
    }

    success = m_program->addShaderFromSourceFile(QOpenGLShader::Fragment, "./triangle.frag");
    if (!success) {
        qDebug() << "shaderProgram addShaderFromSourceFile failed!" << m_program->log();
        return;
    }

    success = m_program->link();
    if (!success) {
        qDebug() << "shaderProgram link failed!" << m_program->log();
    }

    //VAO，VBO数据部分
    float vertices[] = {
        // positions         // colors
        0.5f, -0.5f, 0.0f,  1.0f, 0.0f, 0.0f,  // bottom right
        -0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,  // bottom left
        0.0f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f   // top
    };

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    // bind the Vertex Array Object first, then bind and set vertex buffer(s), and then configure vertex attributes(s).
    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);  //顶点数据复制到缓冲

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (GLvoid*)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);//取消VBO的绑定, glVertexAttribPointer已经把顶点属性关联到顶点缓冲对象了

//    remember: do NOT unbind the EBO while a VAO is active as the bound element buffer object IS stored in the VAO; keep the EBO bound.
//    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

//    You can unbind the VAO afterwards so other VAO calls won't accidentally modify this VAO, but this rarely happens. Modifying other
//    VAOs requires a call to glBindVertexArray anyways so we generally don't unbind VAOs (nor VBOs) when it's not directly necessary.
    glBindVertexArray(0);   //取消VAO绑定





}

void Renderer::uninit()
{
    glDeleteRenderbuffers(1, &m_rbo);
    glDeleteTextures(1, &m_texture);
    glDeleteFramebuffers(1, &m_fbo);
}

void Renderer::adjustSize()
{
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindRenderbuffer(GL_RENDERBUFFER, m_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_width, m_height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    if (m_width > m_height)
    {
        m_viewportX = (m_width - m_height) / 2;
        m_viewportY = 0;
        m_viewportWidth = m_height;
        m_viewportHeight = m_height;
    }
    else
    {
        m_viewportX = 0;
        m_viewportY = (m_height - m_width) / 2;
        m_viewportWidth = m_width;
        m_viewportHeight = m_width;
    }
}
