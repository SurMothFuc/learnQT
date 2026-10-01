#pragma once
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QMap>
#include <QSize>
#include <memory>
#include "Scene.h"

class Renderer;
// Render-context-owned resources. No CPU image readback, scene mutation or UI access.
class GpuDenoiser : protected QOpenGLFunctions_3_3_Core
{
public:
    ~GpuDenoiser();
    void ensure(Renderer &renderer, QSize size);
    void release();
    void invalidate();
    void prepare(const Scene &scene);
    void filter(GLuint vao, const Scene &scene, GLuint accumulated, unsigned samples, bool final, GLuint accumulatedNormal = 0, GLuint accumulatedAlbedo = 0);
    void poll();
    GLuint sample(int i) const { return sampleTextures[i]; }
    GLuint transformTexture() const { return transformTex; }
    GLuint output() const { return filtered; }
    quint64 allocatedBytes() const;
    double milliseconds = 0, acceptance = 0;
    quint64 rounds = 0;
    bool ready() const { return !size.isEmpty(); }
private:
    void bind(QOpenGLShaderProgram &program, const char *name, int unit, GLuint texture, GLenum target = GL_TEXTURE_2D);
    void draw(QOpenGLShaderProgram &program, GLuint fbo, GLuint vao);
    QSize size;
    GLuint sampleTextures[5] = {}, previousGuides[4] = {};
    GLuint colors[2] = {}, moments[2] = {}, spatial[2] = {};
    GLuint temporalFbos[2] = {}, spatialFbos[2] = {}, sampleFbo = 0;
    GLuint transformBuffer = 0, transformTex = 0, filtered = 0;
    GLuint timer = 0, acceptedQuery = 0;
    bool timerPending = false, queryPending = false, history = false, moving = false;
    int read = 0;
    std::unique_ptr<QOpenGLShaderProgram> temporalProgram, spatialProgram, acceptanceProgram;
    QMatrix4x4 previousView;
    float previousFov = 45;
    QMap<QString, QMatrix4x4> previousTransforms;
    QMap<QString, unsigned> stableIds;
    unsigned nextId = 1;
    quint64 historyRevision=0, queryRevision=0;
};
