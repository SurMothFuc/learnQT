#ifndef RENDERER_H
#define RENDERER_H

//#include "sierpinski.h"

#include <QObject>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QImage>
#include <memory>
#include <QDebug>
#include <QElapsedTimer>
#include <QtMath>
#include <QMatrix4x4>
#include <iostream>
#include <qmutex.h>
#include "parameters.h"
#include <Eigen/Dense>

class Renderer : public QObject, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT
public:
    explicit Renderer(int width, int height,QObject *parent = nullptr);
    ~Renderer();

    void render(int width, int height);
    QOpenGLShaderProgram* getShaderProgram(std::string fshader, std::string vshader);
    GLuint getTextureRGB32F(int width, int height);
    GLuint bindData(std::vector<GLuint> colorAttachments);
    GLuint VBO, VAO, EBO;
    void updateprame();
    bool needupdate = true;
private:
    void init(int width, int height);
    void uninit();
    void adjustSize();

private://¾²Ö¹¸³Öµ²Ù×÷
    Renderer(const Renderer &) = delete;
    Renderer &operator =(const Renderer &) = delete;
    Renderer(const Renderer &&) = delete;
    Renderer &operator =(const Renderer &&) = delete;

private:
    int m_width = 0;
    int m_height = 0;
    int m_viewportX = 0;
    int m_viewportY = 0;
    int m_viewportWidth = 0;
    int m_viewportHeight = 0;
    bool m_sizeChanged = true;


    unsigned m_fbo = 0;
    unsigned pathtrace_fbo = 0;
    unsigned mixframe_fbo = 0;
    //unsigned m_rbo = 0;
    unsigned m_texture = 0;
    unsigned pathtrace_texture = 0;
    unsigned mixframe_texture = 0;

    unsigned int frameCounter = 0;
    unsigned int lastframeCounter = 0;

    
    int lasttime = 0;


    GLuint tbo0;
    GLuint tbo1;
    GLuint trianglesTextureBuffer;
    GLuint nodesTextureBuffer;
    GLuint hdrMap;
    GLuint hdrCache;

    std::unique_ptr<QOpenGLShaderProgram> m_program;
    std::unique_ptr<QOpenGLShaderProgram> pathtrace_program;
    std::unique_ptr<QOpenGLShaderProgram> mixframe_program;

    
   // std::unique_ptr<Sierpinski> m_sierpinski;
};

#endif // RENDERER_H
