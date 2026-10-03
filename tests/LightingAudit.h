#pragma once
#include "MaterialTextureImage.h"
#include "RasterEnvironment.h"
#include "common.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QRegularExpression>
#include <QVector4D>
#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

static void check(bool value, const std::string &text)
{
    if (!value)
        throw std::runtime_error(text);
}
static void checkNear(double actual, double expected, double tolerance, const std::string &name)
{
    std::cout << name << ": " << actual << " (expected " << expected << ")\n";
    check(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, name);
}
static QString read(const QString &path)
{
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), path.toStdString());
    QString source = QString::fromUtf8(file.readAll());
    QRegularExpression expression("#include\\s+\"([^\"]+)\"");
    for (auto match = expression.match(source); match.hasMatch(); match = expression.match(source))
        source.replace(match.capturedStart(), match.capturedLength(),
                       read(QFileInfo(path).dir().filePath(match.captured(1))));
    return source;
}

class Audit : public QOpenGLFunctions_3_3_Core
{
  public:
    QOpenGLContext context;
    QOffscreenSurface surface;
    std::unique_ptr<QOpenGLFramebufferObject> target;
    QString modules;
    GLuint vao = 0, hdr = 0, cache = 0;
    GLuint triangleBuffer = 0, triangleTexture = 0, nodeBuffer = 0, nodeTexture = 0, lightBuffer = 0,
           lightTexture = 0;
    int lightCount = 0, analyticCount = 0, triangleCount = 0;
    int textureCount = 0;
    int textureInfoStride = 0;
    unsigned sampleIndex = 7;
    bool unifiedSampler=false;
    unsigned samplerSeedValue=0;
    bool etaScaleRR=false;
    int rrDepth=3;
    bool powerGroups=false;
    float groupProbability=.5f;
    int texturePoolCount = 1;
    QString extraDefines;
    std::map<QString, std::unique_ptr<QOpenGLShaderProgram>> programs;
    static constexpr int resolution = 256;
    explicit Audit(const QString &shaderRoot = QString())
    {
        QSurfaceFormat format;
        format.setVersion(3, 3);
        format.setProfile(QSurfaceFormat::CoreProfile);
        context.setFormat(format);
        check(context.create(), "create GL context");
        surface.setFormat(context.format());
        surface.create();
        check(context.makeCurrent(&surface), "make current");
        initializeOpenGLFunctions();
        std::cout << "OpenGL: " << glGetString(GL_RENDERER) << "\n";
        QOpenGLFramebufferObjectFormat f;
        f.setInternalTextureFormat(GL_RGBA32F);
        target.reset(new QOpenGLFramebufferObject(resolution, resolution, f));
        check(target->isValid(), "float FBO");
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glGenTextures(1, &hdr);
        glGenTextures(1, &cache);
        for (const char *name : {"defines", "structs", "uniforms", "utils", "bvh_material", "hdr_utils",
                                 "bsdf", "light_sampling", "medium", "pathtrace"})
            modules +=
                read(shaderRoot.isEmpty()
                         ? QString::fromStdString(getShaderPath(std::string("include/") + name + ".glsl"))
                         : QDir(shaderRoot).filePath(QString("include/%1.glsl").arg(name))) +
                "\n";
        setGeometry({});
        setLights({}, 0);
    }
    ~Audit()
    {
        programs.clear();
        target.reset();
        for (GLuint value : {hdr, cache, triangleTexture, nodeTexture, lightTexture})
            glDeleteTextures(1, &value);
        for (GLuint value : {triangleBuffer, nodeBuffer, lightBuffer})
            glDeleteBuffers(1, &value);
        glDeleteVertexArrays(1, &vao);
        context.doneCurrent();
    }
    void image(GLuint id, int unit, int w, int h, const float *data)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, w, h, 0, GL_RGB, GL_FLOAT, data);
    }
    void environment(int w, int h, const std::vector<float> &rgb)
    {
        std::unique_ptr<float[]> c(calculateHdrCache(const_cast<float *>(rgb.data()), w, h));
        double sum = 0;
        for (int i = 0; i < w * h; ++i)
        {
            check(std::isfinite(c[3 * i + 2]) && c[3 * i + 2] >= 0, "finite HDR mass");
            sum += c[3 * i + 2];
        }
        checkNear(sum, 1, 1e-6, "CDF probability sum");
        image(hdr, 0, w, h, rgb.data());
        image(cache, 1, w, h, c.get());
    }
    void buffer(GLuint &buffer, GLuint &texture, int unit, GLenum format, const std::vector<float> &data)
    {
        if (!buffer)
            glGenBuffers(1, &buffer);
        if (!texture)
            glGenTextures(1, &texture);
        glBindBuffer(GL_TEXTURE_BUFFER, buffer);
        const std::vector<float> dummy(16, 0);
        const auto &actual = data.empty() ? dummy : data;
        glBufferData(GL_TEXTURE_BUFFER, actual.size() * sizeof(float), actual.data(), GL_STATIC_DRAW);
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_BUFFER, texture);
        glTexBuffer(GL_TEXTURE_BUFFER, format, buffer);
    }
    void setLights(const std::vector<float> &data, int analytic)
    {
        lightCount = int(data.size() / 16);
        analyticCount = analytic;
        buffer(lightBuffer, lightTexture, 4, GL_RGBA32F, data);
    }
    void setGeometry(const std::vector<float> &data)
    {
        triangleCount = int(data.size() / 80);
        buffer(triangleBuffer, triangleTexture, 2, GL_RGBA32F, data);
        // One leaf, reserved node zero. It is sufficient for all small analytic fixtures.
        std::vector<float> nodes(24, 0);
        nodes[15] = float(triangleCount);
        buffer(nodeBuffer, nodeTexture, 3, GL_RGB32F, nodes);
    }
    std::vector<float> run(const QString &body, bool environmentEnabled = true)
    {
        const QString key = extraDefines + QString::number(environmentEnabled) + "\n" + body;
        auto &cached = programs[key];
        if (!cached)
        {
            cached = std::make_unique<QOpenGLShaderProgram>();
            auto &program = *cached;
            const char *vertex =
                "#version 330 core\nout vec3 pix; void main(){vec2 "
                "p=vec2((gl_VertexID<<1)&2,gl_VertexID&2)*2.0-1.0;pix=vec3(p,0);gl_Position=vec4(p,0,1);}";
            check(program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertex),
                  program.log().toStdString());
            QString fragment = "#version 330 core\nin vec3 pix;\nlayout(location=0) out vec4 outputColor;\n";
            if (environmentEnabled)
                fragment += "#define USEENVIRONMENTMAP\n";
            fragment += extraDefines + "\n" + modules + "\n" + body;
            check(program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragment),
                  program.log().toStdString());
            check(program.link(), program.log().toStdString());
        }
        auto &program = *cached;
        program.bind();
        program.setUniformValue("width", resolution);
        program.setUniformValue("height", resolution);
        glUniform1ui(program.uniformLocation("frameCounter"), sampleIndex);
        program.setUniformValue("useEtaScaleRR",etaScaleRR);
        program.setUniformValue("rrMinDepth",rrDepth);
        program.setUniformValue("usePowerLightGroups",powerGroups);
        program.setUniformValue("environmentSelectProbability",groupProbability);
        if(program.uniformLocation("useUnifiedSampler")>=0) {
            program.setUniformValue("useUnifiedSampler",unifiedSampler);
            glUniform1ui(program.uniformLocation("samplerIndex"),sampleIndex);
            glUniform1ui(program.uniformLocation("samplerSeed"),samplerSeedValue);
            const auto bits=getSobolBits(sampleIndex);
            check(bits.size()==120,"Expected 120 fixed Sobol dimensions");
            glUniform4uiv(program.uniformLocation("samplerSobol"),int(bits.size()/4),bits.data());
        }
        program.setUniformValue("hdrResolution", 64);
        program.setUniformValue("hdrMap", 0);
        program.setUniformValue("hdrCache", 1);
        program.setUniformValue("triangles", 2);
        program.setUniformValue("nodes", 3);
        program.setUniformValue("lights", 4);
        program.setUniformValue("nTriangles", triangleCount);
        program.setUniformValue("nNodes", triangleCount > 0 ? 2 : 0);
        program.setUniformValue("nLights", lightCount);
        program.setUniformValue("nAnalyticLights", analyticCount);
        program.setUniformValue("materialTextureCount", textureCount);
        program.setUniformValue("materialTextureInfoStride", textureInfoStride);
        // Distinct sampler types need distinct units even when the test has no material images.
        program.setUniformValue("materialTextures", 5);
        program.setUniformValue("materialTextureInfo", 6);
        for (int i = 1; i < texturePoolCount; ++i)
            program.setUniformValue(("materialTextures" + std::to_string(i)).c_str(), 6 + i);
        std::vector<float> sobol = getSobelRandomNumber(sampleIndex, 60);
        program.setUniformValueArray("sobelNumber", sobol.data(), int(sobol.size()), 1);
        check(glGetError() == GL_NO_ERROR, "GL setup/uniform error");
        target->bind();
        glViewport(0, 0, resolution, resolution);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        std::vector<float> pixels(resolution * resolution * 4);
        glReadPixels(0, 0, resolution, resolution, GL_RGBA, GL_FLOAT, pixels.data());
        GLenum error = glGetError();
        check(error == GL_NO_ERROR, "OpenGL draw/read error " + std::to_string(error));
        for (float v : pixels)
            check(std::isfinite(v), "NaN/Inf pixel");
        program.release();
        target->release();
        return pixels;
    }
    std::array<double, 4> mean(const QString &body, bool env = true)
    {
        auto pixels = run(body, env);
        std::array<double, 4> sum{};
        for (size_t i = 0; i < pixels.size(); ++i)
            sum[i % 4] += pixels[i] / double(resolution * resolution);
        return sum;
    }
};
