#ifndef RENDERER_H
#define RENDERER_H

//#include "sierpinski.h"

#include <QObject>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QImage>
#include <memory>

class Renderer : public QObject, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT
public:
    explicit Renderer(QObject *parent = nullptr);
    ~Renderer();

    void render(int width, int height);

    QTimer* m_pTimer = nullptr;
    float m_uniformValue = 0.0f;

private:
    void init();
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

    unsigned m_vao = 0;
    unsigned m_vbo = 0;
    unsigned m_fbo = 0;
    unsigned m_rbo = 0;
    unsigned m_texture = 0;

    std::unique_ptr<QOpenGLShaderProgram> m_program;
   // std::unique_ptr<Sierpinski> m_sierpinski;
};

#endif // RENDERER_H
