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
#include "Scene.h"
#include <Eigen/Dense>

class Renderer : public QObject, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT
public:
    explicit Renderer(int width, int height, QObject* parent = nullptr);
    ~Renderer();

    void render(int width, int height);
    QOpenGLShaderProgram* getShaderProgram(std::string fshader, std::string vshader);
    GLuint getTextureRGB32F(int width, int height);
    GLuint bindData(std::vector<GLuint> colorAttachments);
    GLuint VBO = 0, VAO = 0, EBO = 0;
    void updateparam();
    void updateSizeParam();

    bool needupdate = true;
    bool renderLow = false;
private:
    void init(int width, int height);
    void uninit();
    void adjustSize();
    void calResolution();
private://静止赋值操作
    Renderer(const Renderer&) = delete;
    Renderer& operator =(const Renderer&) = delete;
    Renderer(const Renderer&&) = delete;
    Renderer& operator =(const Renderer&&) = delete;

private:

    int m_width = 0;
    int m_height = 0;
    int render_width = 0;
    int render_height = 0;
    int m_viewportX = 0;
    int m_viewportY = 0;
    bool m_sizeChanged = true;


    unsigned m_fbo = 0;
    unsigned pathtrace_fbo = 0;

    unsigned mixframe_fbo_ping = 0;
    unsigned mixframe_fbo_pong = 0;

    unsigned directLight_fbo_filtered = 0;
    unsigned indirectLight_fbo_filtered = 0;

    unsigned historysave_fbo = 0;

    //unsigned m_rbo = 0;
    unsigned m_texture = 0;
    //unsigned pathtrace_texture = 0;
    //unsigned mixframe_texture = 0;

    unsigned preDirectLightTex = 0;
    unsigned preIndirectLightTex = 0;
    unsigned preMovmentTex = 0;

    unsigned directLightTex = 0;//最后一位是方差
    unsigned indirectLightTex = 0;//最后一位是方差
    unsigned movmentTex = 0;
    unsigned emissionTex = 0;
    unsigned filteredTexture_ping = 0;
    unsigned filteredTexture_pong = 0;
    unsigned normal_depth_texture = 0;
    unsigned baseColorTex = 0;
    unsigned directLightTexfiltered = 0;
    unsigned indirectLightTexfiltered = 0;

    unsigned int frameCounter = 0;
    unsigned int lastframeCounter = 0;


    int lasttime = 0;
    bool first_render = true;

    GLuint tbo0 = 0;
    GLuint tbo1 = 0;
    GLuint trianglesTextureBuffer = 0;
    GLuint nodesTextureBuffer = 0;
    GLuint hdrMap = 0;
    GLuint hdrCache = 0;

    std::unique_ptr<QOpenGLShaderProgram> m_program = nullptr;
    std::unique_ptr<QOpenGLShaderProgram> pathtrace_program = nullptr;
    std::unique_ptr<QOpenGLShaderProgram> mixframe_program = nullptr;
    std::unique_ptr<QOpenGLShaderProgram> historysave_program = nullptr;

    std::vector<unsigned> batchTextureSettings;
    // std::unique_ptr<Sierpinski> m_sierpinski;
};

#endif // RENDERER_H
