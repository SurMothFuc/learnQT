#include "renderer.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

#include <algorithm>
#include <ctime>
#include <cstring>
#include <regex>

extern QMutex param_mutex;

namespace {
int clampMaxBounces(int maxBounces) {
    return std::max(0, std::min(maxBounces, static_cast<int>(MAX_BOUNCES_LIMIT)));
}

bool isRenderFrameLimitReached(const RenderParams::Snapshot& snapshot, unsigned int frameCounter) {
    return snapshot.maxRenderFrames > 0 &&
           frameCounter >= static_cast<unsigned int>(snapshot.maxRenderFrames);
}
}

std::string processIncludes(const std::string& source, const std::string& shaderPath) {
    static std::unordered_map<std::string, std::string> includeCache;
    static std::unordered_map<std::string, bool> processing; // 防止循环包含

    // 主文件缓存检查
    if (includeCache.find(shaderPath) != includeCache.end()) {
        return includeCache[shaderPath];
    }
    processing[shaderPath] = true;

    QDir dir = QFileInfo(QString::fromStdString(shaderPath)).dir().path();
    std::regex includeRegex(R"(^\s*#include\s*\"([^\"]+)\")", std::regex::ECMAScript);
    std::smatch match;
    std::string result = source;

    while (std::regex_search(result, match, includeRegex)) {
        std::string includeFile = match[1].str();
        std::string includePath = dir.filePath(QString::fromStdString(includeFile)).toStdString();

        // 检查循环包含
        if (processing[includePath]) {
            qWarning() << "Circular include detected: " << QString::fromStdString(includePath);
            result = match.prefix().str() + match.suffix().str();
            continue;
        }

        // 读取包含文件
        std::string includeContent;
        if (includeCache.find(includePath) != includeCache.end()) {
            includeContent = includeCache[includePath];
        } else {
            QFile file(QString::fromStdString(includePath));
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                qWarning() << "Failed to open include file: " << QString::fromStdString(includePath);
                result = match.prefix().str() + match.suffix().str();
                includeCache[includePath] = "";
                continue;
            }
            includeContent = QTextStream(&file).readAll().toStdString();
            file.close();

            // 递归处理包含文件中的 #include
            includeContent = processIncludes(includeContent, includePath);
        }

        // 替换 #include 指令为文件内容
        result = match.prefix().str() + includeContent + match.suffix().str();
    }

    includeCache[shaderPath] = result;
    processing[shaderPath] = false;

    return result;
}

std::string injectDefines(const std::string& source, const std::unordered_map<std::string, std::string>& defines) {
    // 注入动态 #define，确保在 #version 之后添加
    std::string definesStr;
    for (const auto& keyValue : defines) {
        definesStr += "#define " + keyValue.first + " " + keyValue.second + "\n";
    }

    const size_t versionPos = source.find("#version");
    if (versionPos != std::string::npos) {
        size_t lineEnd = source.find("\n", versionPos);
        if (lineEnd == std::string::npos) {
            lineEnd = source.size();
        }
        return source.substr(0, lineEnd + 1) + definesStr + source.substr(lineEnd + 1);
    }

    return definesStr + source;
}

GLuint Renderer::getTextureRGB32F(int width, int height) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

QOpenGLShaderProgram* Renderer::getShaderProgram(
    std::string fshader,
    std::string vshader,
    const std::unordered_map<std::string, std::string>& defines_Vertex,
    const std::unordered_map<std::string, std::string>& defines_Fragment) {
    QOpenGLShaderProgram* shaderProgram = new QOpenGLShaderProgram;

    // 加载并处理顶点着色器
    QFile vFile(QString::fromStdString(vshader));
    if (!vFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Failed to open vertex shader: " << vshader.c_str();
        return shaderProgram;
    }
    std::string vSource = QTextStream(&vFile).readAll().toStdString();
    vFile.close();
    vSource = processIncludes(vSource, vshader);
    vSource = injectDefines(vSource, defines_Vertex);

    bool success = shaderProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, vSource.c_str());
    if (!success) {
        qDebug() << "Vertex shader compilation failed:\n" << shaderProgram->log();
        return shaderProgram;
    }

    // 加载并处理片段着色器
    QFile fFile(QString::fromStdString(fshader));
    if (!fFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Failed to open fragment shader: " << fshader.c_str();
        return shaderProgram;
    }
    std::string fSource = QTextStream(&fFile).readAll().toStdString();
    fFile.close();
    fSource = processIncludes(fSource, fshader);
    fSource = injectDefines(fSource, defines_Fragment);

    success = shaderProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, fSource.c_str());
    if (!success) {
        qDebug() << "Fragment shader compilation failed:\n" << shaderProgram->log();
        return shaderProgram;
    }

    success = shaderProgram->link();
    if (!success) {
        qDebug() << "Shader linking failed:\n" << shaderProgram->log();
    }
    return shaderProgram;
}

GLuint Renderer::bindData(std::vector<GLuint> colorAttachments) {
    // colorAttachments 为颜色缓冲，返回值为 FBO。
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    if (!colorAttachments.empty()) {
        std::vector<GLenum> attachments;
        attachments.reserve(colorAttachments.size());
        for (int i = 0; i < static_cast<int>(colorAttachments.size()); ++i) {
            glBindTexture(GL_TEXTURE_2D, colorAttachments[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, colorAttachments[i], 0);
            attachments.push_back(GL_COLOR_ATTACHMENT0 + i);
        }
        glDrawBuffers(static_cast<GLsizei>(attachments.size()), attachments.data());
    }

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        qDebug() << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << glCheckFramebufferStatus(GL_FRAMEBUFFER);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return fbo;
}

Renderer::Renderer(int width, int height, const RenderParams::Snapshot& initialSnapshot, QObject *parent)
    : QObject(parent)
{
    init(width, height, initialSnapshot);
    initOIDN();
}

Renderer::~Renderer()
{
    uninit();
}

void Renderer::render(int width, int height, const RenderParams::Snapshot& snapshot, SceneDirtyFlags dirtyFlags)
{
    // 1. 帧首统一决策并执行刷新
    const RefreshActions actions = resolveRefreshActions(width, height, snapshot, dirtyFlags);
    applyRefreshActions(width, height, snapshot, actions);

    // 2. 常规渲染与后处理阶段全部只读本帧 snapshot
    displayRenderingStats();
    if (isRenderFrameLimitReached(snapshot, frameCounter)) {
        performDenoising(snapshot, true);
        compositeToScreen(snapshot);
        return;
    }

    executeRenderPass(snapshot);
    processHistorySaving(snapshot);
    const bool reachedFrameLimitAfterPass = isRenderFrameLimitReached(snapshot, frameCounter);
    performDenoising(snapshot, reachedFrameLimitAfterPass);
    compositeToScreen(snapshot);
}

void Renderer::init(int width, int height, const RenderParams::Snapshot& snapshot)
{
    batchTextureSettings.clear();
    m_width = width;
    m_height = height;
    calResolution(snapshot.renderLow);
    updateTileGrid(snapshot.tileSize);
    m_viewportX = 0;
    m_viewportY = 0;
    initializeOpenGLFunctions();

    qDebug() << reinterpret_cast<const char *>(glGetString(GL_VERSION));

    rebuildPathtraceProgram(snapshot);

    preRenderColorTex = getTextureRGB32F(render_width, render_height);
    RenderColorTex = getTextureRGB32F(render_width, render_height);
    normal_texture = getTextureRGB32F(render_width, render_height);
    baseColorTex = getTextureRGB32F(render_width, render_height);
    batchTextureSettings.insert(batchTextureSettings.end(),
        { preRenderColorTex, RenderColorTex, normal_texture, baseColorTex });

    pathtrace_fbo = bindData(std::vector<GLuint>{
        RenderColorTex, normal_texture, baseColorTex });

    historysave_program.reset(getShaderProgram(getShaderPath("historysave.frag"), getShaderPath("triangle.vert")));
    historysave_fbo = bindData(std::vector<GLuint>{preRenderColorTex});

    RenderColorTexfiltered = getTextureRGB32F(render_width, render_height);
    batchTextureSettings.push_back(RenderColorTexfiltered);

    m_program.reset(getShaderProgram(getShaderPath("triangle.frag"), getShaderPath("triangle.vert")));
    m_texture = getTextureRGB32F(m_width, m_height);
    m_fbo = bindData(std::vector<GLuint>{m_texture});

    glEnable(GL_DEPTH_TEST);

    const std::vector<QVector3D> square = {
        QVector3D(-1, -1, 0), QVector3D(1, -1, 0), QVector3D(-1, 1, 0),
        QVector3D(1, 1, 0), QVector3D(-1, 1, 0), QVector3D(1, -1, 0)
    };

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QVector3D) * square.size(), nullptr, GL_STATIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(QVector3D) * square.size(), square.data());

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, reinterpret_cast<GLvoid*>(0));
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_lastAppliedSnapshot = snapshot;
    m_forceDenoiseRefresh = true;
}

void Renderer::initOIDN()
{
    oidnDevice = oidn::newDevice();
    oidnDevice.commit();

    oidnAlbedoFilter = oidnDevice.newFilter("RT");
    oidnNormalFilter = oidnDevice.newFilter("RT");

    oidnMainFilter = oidnDevice.newFilter("RT");
    oidnMainFilter.set("hdr", true);
    oidnMainFilter.set("cleanAux", true);

    glGenBuffers(3, pboIds);

    updateOIDNBuffers();
}

void Renderer::uninit()
{
    glDeleteFramebuffers(1, &m_fbo);
    glDeleteFramebuffers(1, &pathtrace_fbo);
    glDeleteFramebuffers(1, &historysave_fbo);

    glDeleteTextures(1, &m_texture);
    for (auto& perTex : batchTextureSettings) {
        glDeleteTextures(1, &perTex);
        perTex = 0;
    }

    glDeleteTextures(1, &hdrMap);
    glDeleteTextures(1, &hdrCache);
    glDeleteTextures(1, &trianglesTextureBuffer);
    glDeleteTextures(1, &nodesTextureBuffer);
    glDeleteTextures(1, &lightsTextureBuffer);

    if (VAO) {
        glDeleteVertexArrays(1, &VAO);
    }
    if (VBO) {
        glDeleteBuffers(1, &VBO);
    }

    if (tbo0) {
        glDeleteBuffers(1, &tbo0);
    }
    if (tbo1) {
        glDeleteBuffers(1, &tbo1);
    }
    if (tboLights) {
        glDeleteBuffers(1, &tboLights);
    }

    if (pboIds[0] != 0 || pboIds[1] != 0 || pboIds[2] != 0) {
        glDeleteBuffers(3, pboIds);
        std::fill(std::begin(pboIds), std::end(pboIds), 0);
    }

    historysave_fbo = m_fbo = pathtrace_fbo = 0;
    m_texture = 0;
    hdrMap = hdrCache = trianglesTextureBuffer = nodesTextureBuffer = lightsTextureBuffer = 0;
    VAO = VBO = tbo0 = tbo1 = tboLights = 0;
}

void Renderer::updateOIDNBuffers()
{
    const size_t bufferSize = static_cast<size_t>(render_width) * static_cast<size_t>(render_height) * 3 * sizeof(float);

    oidnColorBuf = oidnDevice.newBuffer(bufferSize);
    oidnAlbedoBuf = oidnDevice.newBuffer(bufferSize);
    oidnNormalBuf = oidnDevice.newBuffer(bufferSize);
    oidnOutputBuf = oidnDevice.newBuffer(bufferSize);

    oidnAlbedoFilter.setImage("albedo", oidnAlbedoBuf, oidn::Format::Float3, render_width, render_height);
    oidnAlbedoFilter.setImage("output", oidnAlbedoBuf, oidn::Format::Float3, render_width, render_height);
    oidnAlbedoFilter.commit();

    oidnNormalFilter.setImage("normal", oidnNormalBuf, oidn::Format::Float3, render_width, render_height);
    oidnNormalFilter.setImage("output", oidnNormalBuf, oidn::Format::Float3, render_width, render_height);
    oidnNormalFilter.commit();

    oidnMainFilter.setImage("color", oidnColorBuf, oidn::Format::Float3, render_width, render_height);
    oidnMainFilter.setImage("albedo", oidnAlbedoBuf, oidn::Format::Float3, render_width, render_height);
    oidnMainFilter.setImage("normal", oidnNormalBuf, oidn::Format::Float3, render_width, render_height);
    oidnMainFilter.setImage("output", oidnOutputBuf, oidn::Format::Float3, render_width, render_height);
    oidnMainFilter.commit();

    for (int i = 0; i < 3; i++) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pboIds[i]);
        glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(bufferSize), nullptr, GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

void Renderer::adjustSize()
{
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, m_width, m_height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    for (auto& perTex : batchTextureSettings) {
        glBindTexture(GL_TEXTURE_2D, perTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, render_width, render_height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    updateOIDNBuffers();

    m_viewportX = 0;
    m_viewportY = 0;
}

void Renderer::updateSizeParam()
{
    if (!pathtrace_program) {
        return;
    }
    pathtrace_program->bind();
    pathtrace_program->setUniformValue("width", render_width);
    pathtrace_program->setUniformValue("height", render_height);
    pathtrace_program->release();
}

void Renderer::calResolution(bool renderLow)
{
    if (renderLow) {
        if (m_width <= MAX_LOW_RESOLUTION && m_height <= MAX_LOW_RESOLUTION) {
            render_width = m_width;
            render_height = m_height;
        } else {
            const double scaleWidth = static_cast<double>(MAX_LOW_RESOLUTION) / m_width;
            const double scaleHeight = static_cast<double>(MAX_LOW_RESOLUTION) / m_height;
            const double scale = std::min(scaleWidth, scaleHeight);

            render_width = static_cast<int>(std::round(m_width * scale));
            render_height = static_cast<int>(std::round(m_height * scale));
        }
    } else {
        render_width = m_width;
        render_height = m_height;
    }
}

void Renderer::updateTileGrid(int tileSize)
{
    const int safeTileSize = std::max(1, tileSize);
    tilesX = (render_width + safeTileSize - 1) / safeTileSize;
    tilesY = (render_height + safeTileSize - 1) / safeTileSize;
}

void Renderer::renderTile(int tileX, int tileY, int tileWidth, int tileHeight, int maxBounces)
{
    const unsigned int sobolBounceCount = static_cast<unsigned int>(std::max(1, maxBounces));
    const auto sobelNumber = getSobelRandomNumber(frameCounter, sobolBounceCount);

    pathtrace_program->bind();
    {
        const GLint frameLocation = pathtrace_program->uniformLocation("frameCounter");
        glUniform1ui(frameLocation, frameCounter);
        const GLint sobelLocation = pathtrace_program->uniformLocation("sobelNumber");
        glUniform1fv(sobelLocation, static_cast<GLsizei>(sobolBounceCount * 2u), sobelNumber.data());
        pathtrace_program->setUniformValue("maxBounces", maxBounces);

        glBindFramebuffer(GL_FRAMEBUFFER, pathtrace_fbo);

        pathtrace_program->setUniformValue("triangles", 0);
        pathtrace_program->setUniformValue("nodes", 1);
        pathtrace_program->setUniformValue("hdrMap", 2);
        pathtrace_program->setUniformValue("hdrCache", 3);
        pathtrace_program->setUniformValue("lights", 5);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_BUFFER, trianglesTextureBuffer);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_BUFFER, nodesTextureBuffer);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, hdrMap);

        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, hdrCache);

        pathtrace_program->setUniformValue("preRenderColor", 4);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, preRenderColorTex);

        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_BUFFER, lightsTextureBuffer);

        glViewport(tileX, tileY, tileWidth, tileHeight);
        glEnable(GL_SCISSOR_TEST);
        glScissor(tileX, tileY, tileWidth, tileHeight);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisable(GL_SCISSOR_TEST);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    pathtrace_program->release();
}

void Renderer::renderFullImage(int maxBounces)
{
    const unsigned int sobolBounceCount = static_cast<unsigned int>(std::max(1, maxBounces));
    const auto sobelNumber = getSobelRandomNumber(frameCounter, sobolBounceCount);

    pathtrace_program->bind();
    {
        const GLint frameLocation = pathtrace_program->uniformLocation("frameCounter");
        glUniform1ui(frameLocation, frameCounter);
        const GLint sobelLocation = pathtrace_program->uniformLocation("sobelNumber");
        glUniform1fv(sobelLocation, static_cast<GLsizei>(sobolBounceCount * 2u), sobelNumber.data());
        pathtrace_program->setUniformValue("maxBounces", maxBounces);

        glBindFramebuffer(GL_FRAMEBUFFER, pathtrace_fbo);

        pathtrace_program->setUniformValue("triangles", 0);
        pathtrace_program->setUniformValue("nodes", 1);
        pathtrace_program->setUniformValue("hdrMap", 2);
        pathtrace_program->setUniformValue("hdrCache", 3);
        pathtrace_program->setUniformValue("lights", 5);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_BUFFER, trianglesTextureBuffer);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_BUFFER, nodesTextureBuffer);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, hdrMap);

        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, hdrCache);

        pathtrace_program->setUniformValue("preRenderColor", 4);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, preRenderColorTex);

        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_BUFFER, lightsTextureBuffer);

        glViewport(m_viewportX, m_viewportY, render_width, render_height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    pathtrace_program->release();

    renderComplete = true;
}

void Renderer::rebuildPathtraceProgram(const RenderParams::Snapshot& snapshot)
{
    std::unordered_map<std::string, std::string> defines_Fragment = {};
    std::unordered_map<std::string, std::string> defines_Vertex = {};
    defines_Fragment.insert({"MAX_BOUNCES_LIMIT", std::to_string(MAX_BOUNCES_LIMIT)});
    if (snapshot.useEnvironmentMap) {
        defines_Fragment.insert({"USEENVIRONMENTMAP", ""});
    }
    pathtrace_program.reset(getShaderProgram(getShaderPath("pathtrace.frag"), getShaderPath("triangle.vert"), defines_Vertex, defines_Fragment));
}

void Renderer::adjustScreenResolution(int width, int height, bool renderLow)
{
    const int oldWidth = m_width;
    const int oldHeight = m_height;
    const int oldRenderWidth = render_width;
    const int oldRenderHeight = render_height;

    m_width = width;
    m_height = height;
    calResolution(renderLow);

    if (oldRenderWidth != render_width || oldRenderHeight != render_height || oldWidth != m_width || oldHeight != m_height) {
        qDebug() << "Adjust frame size to:" << width << height << "Adjust render size to:" << render_width << render_height;
        adjustSize();
        updateSizeParam();
    }
}

Renderer::RefreshActions Renderer::resolveRefreshActions(
    int width,
    int height,
    const RenderParams::Snapshot& snapshot,
    SceneDirtyFlags dirtyFlags) const
{
    // 所有刷新动作都在帧首一次性决策，后续阶段只执行这个结果。
    RefreshActions actions;

    const bool environmentMapChanged = snapshot.useEnvironmentMap != m_lastAppliedSnapshot.useEnvironmentMap;
    const bool renderLowChanged = snapshot.renderLow != m_lastAppliedSnapshot.renderLow;
    const bool tileModeChanged = snapshot.useTileRendering != m_lastAppliedSnapshot.useTileRendering;
    const bool tileSizeChanged = snapshot.tileSize != m_lastAppliedSnapshot.tileSize;
    const bool denoiseChanged = snapshot.denoise != m_lastAppliedSnapshot.denoise;
    const bool maxBouncesChanged = snapshot.maxBounces != m_lastAppliedSnapshot.maxBounces;
    const bool sizeChanged = width != m_width || height != m_height;

    if (environmentMapChanged) {
        actions.rebuildShader = true;
        actions.syncSceneBuffers = true;
        actions.syncCameraUniforms = true;
        actions.resetAccumulation = true;
    }

    if (renderLowChanged || sizeChanged) {
        actions.resizeTargets = true;
        actions.resetAccumulation = true;
    }

    if (tileModeChanged || tileSizeChanged) {
        actions.resetAccumulation = true;
    }

    if (maxBouncesChanged) {
        actions.resetAccumulation = true;
    }

    if (denoiseChanged) {
        actions.refreshDenoisePolicy = true;
    }

    if (hasSceneDirtyFlag(dirtyFlags, SceneDirtyFlag::Camera)) {
        actions.syncCameraUniforms = true;
        actions.resetAccumulation = true;
    }

    if (hasSceneDirtyFlag(dirtyFlags, SceneDirtyFlag::Material)) {
        actions.syncMaterialBuffer = true;
        actions.resetAccumulation = true;
    }

    if (hasSceneDirtyFlag(dirtyFlags, SceneDirtyFlag::SceneBuffers)) {
        actions.syncSceneBuffers = true;
        actions.resetAccumulation = true;
    }

    // 全量场景同步已经覆盖材质缓冲，不再重复走材质脏路径。
    if (actions.syncSceneBuffers) {
        actions.syncMaterialBuffer = false;
    }

    return actions;
}

void Renderer::applyRefreshActions(int width, int height, const RenderParams::Snapshot& snapshot, const RefreshActions& actions)
{
    // 这里只执行帧首已经决策好的刷新动作。
    if (actions.rebuildShader) {
        rebuildPathtraceProgram(snapshot);
    }

    if (actions.resizeTargets) {
        adjustScreenResolution(width, height, snapshot.renderLow);
    }

    if (actions.resizeTargets ||
        snapshot.tileSize != m_lastAppliedSnapshot.tileSize ||
        snapshot.useTileRendering != m_lastAppliedSnapshot.useTileRendering) {
        updateTileGrid(snapshot.tileSize);
    }

    if (actions.refreshDenoisePolicy) {
        m_forceDenoiseRefresh = true;
    }

    if (actions.syncSceneBuffers) {
        syncSceneBuffers();
    } else if (actions.syncMaterialBuffer) {
        syncMaterialBuffer();
    }

    if (actions.syncCameraUniforms) {
        syncCameraUniforms();
    }

    if (actions.resetAccumulation) {
        resetAccumulation();
    }

    m_lastAppliedSnapshot = snapshot;
}

void Renderer::clearTexture(GLuint texture)
{
    if (texture == 0) {
        return;
    }

    GLuint clearFbo = 0;
    glGenFramebuffers(1, &clearFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, clearFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        const GLfloat clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        glClearBufferfv(GL_COLOR, 0, clearColor);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &clearFbo);
}

void Renderer::resetAccumulation()
{
    // 重置累计状态，并清空历史纹理避免旧帧残留参与新累计。
    clearTexture(preRenderColorTex);
    clearTexture(RenderColorTex);
    clearTexture(normal_texture);
    clearTexture(baseColorTex);
    clearTexture(RenderColorTexfiltered);
    clearTexture(m_texture);

    lasttime = clock();
    lastframeCounter = 0;
    frameCounter = 0;
    chunkedRenderingCount = 0;
    lastChunkedRenderingCount = 0;
    nowChunkedCount = 0;
    currentTileX = 0;
    currentTileY = 0;
    renderComplete = false;
    m_forceDenoiseRefresh = true;
    m_hasDenoisedFrame = false;
    m_lastDenoisedFrameCounter = 0;
}

void Renderer::uploadTriangleBuffer(bool recreateResources)
{
    const auto& trianglesEncoded = Scene::getInstance().triangles_encoded;
    const GLsizeiptr bufferSize = static_cast<GLsizeiptr>(trianglesEncoded.size() * sizeof(Triangle_encoded));
    const void* data = trianglesEncoded.empty() ? nullptr : trianglesEncoded.data();

    if (recreateResources && tbo0 != 0) {
        glDeleteBuffers(1, &tbo0);
        tbo0 = 0;
    }
    if (recreateResources && trianglesTextureBuffer != 0) {
        glDeleteTextures(1, &trianglesTextureBuffer);
        trianglesTextureBuffer = 0;
    }

    if (tbo0 == 0) {
        glGenBuffers(1, &tbo0);
    }
    glBindBuffer(GL_TEXTURE_BUFFER, tbo0);
    glBufferData(GL_TEXTURE_BUFFER, bufferSize, data, GL_STATIC_DRAW);

    if (trianglesTextureBuffer == 0) {
        glGenTextures(1, &trianglesTextureBuffer);
    }
    glBindTexture(GL_TEXTURE_BUFFER, trianglesTextureBuffer);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, tbo0);
}

void Renderer::uploadNodeBuffer(bool recreateResources)
{
    const auto& nodesEncoded = Scene::getInstance().nodes_encoded;
    const GLsizeiptr bufferSize = static_cast<GLsizeiptr>(nodesEncoded.size() * sizeof(BVHNode_encoded));
    const void* data = nodesEncoded.empty() ? nullptr : nodesEncoded.data();

    if (recreateResources && tbo1 != 0) {
        glDeleteBuffers(1, &tbo1);
        tbo1 = 0;
    }
    if (recreateResources && nodesTextureBuffer != 0) {
        glDeleteTextures(1, &nodesTextureBuffer);
        nodesTextureBuffer = 0;
    }

    if (tbo1 == 0) {
        glGenBuffers(1, &tbo1);
    }
    glBindBuffer(GL_TEXTURE_BUFFER, tbo1);
    glBufferData(GL_TEXTURE_BUFFER, bufferSize, data, GL_STATIC_DRAW);

    if (nodesTextureBuffer == 0) {
        glGenTextures(1, &nodesTextureBuffer);
    }
    glBindTexture(GL_TEXTURE_BUFFER, nodesTextureBuffer);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGB32F, tbo1);
}

void Renderer::uploadLightBuffer(bool recreateResources)
{
    const auto& lightsEncoded = Scene::getInstance().lights_encoded;
    const GLsizeiptr bufferSize = static_cast<GLsizeiptr>(lightsEncoded.size() * sizeof(Light_encoded));
    const void* data = lightsEncoded.empty() ? nullptr : lightsEncoded.data();

    if (recreateResources && tboLights != 0) {
        glDeleteBuffers(1, &tboLights);
        tboLights = 0;
    }
    if (recreateResources && lightsTextureBuffer != 0) {
        glDeleteTextures(1, &lightsTextureBuffer);
        lightsTextureBuffer = 0;
    }

    if (tboLights == 0) {
        glGenBuffers(1, &tboLights);
    }
    glBindBuffer(GL_TEXTURE_BUFFER, tboLights);
    glBufferData(GL_TEXTURE_BUFFER, bufferSize, data, GL_STATIC_DRAW);

    if (lightsTextureBuffer == 0) {
        glGenTextures(1, &lightsTextureBuffer);
    }
    glBindTexture(GL_TEXTURE_BUFFER, lightsTextureBuffer);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, tboLights);
}

void Renderer::uploadHdrTextures(bool recreateResources)
{
    const auto uploadTexture = [&](GLuint& texture, float* data) {
        if (recreateResources && texture != 0) {
            glDeleteTextures(1, &texture);
            texture = 0;
        }
        if (texture == 0) {
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        } else {
            glBindTexture(GL_TEXTURE_2D, texture);
        }

        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGB32F,
            Scene::getInstance().hdrRes.width,
            Scene::getInstance().hdrRes.height,
            0,
            GL_RGB,
            GL_FLOAT,
            data);
    };

    uploadTexture(hdrMap, Scene::getInstance().hdrRes.cols);
    uploadTexture(hdrCache, Scene::getInstance().cache);
}

void Renderer::syncCameraUniforms()
{
    QMatrix4x4 inverseView;
    QVector3D eye;
    {
        QMutexLocker lock(&param_mutex);
        const QMatrix4x4 view = Scene::getInstance().camera.getViewMatrix();
        inverseView = view.inverted();
        eye = Scene::getInstance().camera.position;
    }

    pathtrace_program->bind();
    pathtrace_program->setUniformValue("view", inverseView);
    pathtrace_program->setUniformValue("eye", eye);
    pathtrace_program->release();
}

void Renderer::syncMaterialBuffer()
{
    QMutexLocker lock(&param_mutex);
    // 材质脏路径只重传三角形编码缓冲，不触碰 BVH / HDR 资源。
    const bool recreateTriangleResources = (tbo0 == 0 || trianglesTextureBuffer == 0);
    uploadTriangleBuffer(recreateTriangleResources);
    uploadLightBuffer(tboLights == 0 || lightsTextureBuffer == 0);
    pathtrace_program->bind();
    pathtrace_program->setUniformValue("nLights", static_cast<int>(Scene::getInstance().lights_encoded.size()));
    pathtrace_program->release();
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
}

void Renderer::syncSceneBuffers()
{
    QMutexLocker lock(&param_mutex);

    // 全量场景同步：triangles / nodes / HDR / 相关 uniform 一次性更新。
    uploadTriangleBuffer(tbo0 == 0 || trianglesTextureBuffer == 0);
    uploadNodeBuffer(tbo1 == 0 || nodesTextureBuffer == 0);
    uploadLightBuffer(tboLights == 0 || lightsTextureBuffer == 0);
    uploadHdrTextures(hdrMap == 0 || hdrCache == 0);

    pathtrace_program->bind();
    pathtrace_program->setUniformValue("nTriangles", static_cast<int>(Scene::getInstance().triangles.size()));
    pathtrace_program->setUniformValue("nNodes", static_cast<int>(Scene::getInstance().nodes_encoded.size()));
    pathtrace_program->setUniformValue("nLights", static_cast<int>(Scene::getInstance().lights_encoded.size()));
    pathtrace_program->setUniformValue("width", render_width);
    pathtrace_program->setUniformValue("height", render_height);
    pathtrace_program->setUniformValue("hdrResolution", Scene::getInstance().hdrResolution);
    pathtrace_program->release();

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
}

void Renderer::displayRenderingStats()
{
    const int nowtime = clock();
    if (nowtime - lasttime > 200) {
        printf("\r                                                                                                  ");
        std::cout << "\rframeCounter: " << frameCounter
                  << " FPS: " << int((frameCounter - lastframeCounter) / (1.0 * (nowtime - lasttime) / 1000.0))
                  << " | Chunked Rendering: " << chunkedRenderingCount
                  << " Chunked Rendering FPS: " << int((chunkedRenderingCount - lastChunkedRenderingCount) / (1.0 * (nowtime - lasttime) / 1000.0));

        lastframeCounter = frameCounter;
        lastChunkedRenderingCount = chunkedRenderingCount;
        lasttime = nowtime;
    }
}

void Renderer::executeRenderPass(const RenderParams::Snapshot& snapshot)
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    const int maxBounces = clampMaxBounces(snapshot.maxBounces);

    if (snapshot.useTileRendering) {
        // 分块渲染模式
        if (currentTileX < tilesX && currentTileY < tilesY) {
            const int tileSize = std::max(1, snapshot.tileSize);
            const int tileWidth = std::min(tileSize, render_width - currentTileX * tileSize);
            const int tileHeight = std::min(tileSize, render_height - currentTileY * tileSize);

            renderTile(currentTileX * tileSize, currentTileY * tileSize, tileWidth, tileHeight, maxBounces);
            updateTileRenderingState();
        }
    } else {
        // 完整图像渲染模式
        renderFullImage(maxBounces);
    }
}

void Renderer::updateTileRenderingState()
{
    // 更新下一个要渲染的块的位置。
    currentTileX++;
    if (currentTileX >= tilesX) {
        currentTileX = 0;
        currentTileY++;

        // 如果所有块都渲染完成，标记一轮分块累计结束。
        if (currentTileY >= tilesY) {
            renderComplete = true;
            currentTileX = 0;
            currentTileY = 0;
        }
    }
}

void Renderer::processHistorySaving(const RenderParams::Snapshot& snapshot)
{
    chunkedRenderingCount++;
    nowChunkedCount++;

    // 只有完整图像或完成一整轮分块后才保存历史帧。
    if (!snapshot.useTileRendering || renderComplete) {
        nowChunkedCount = 0;
        frameCounter++;

        historysave_program->bind();
        {
            glBindFramebuffer(GL_FRAMEBUFFER, historysave_fbo);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, RenderColorTex);
            historysave_program->setUniformValue("RenderColor", 0);

            glViewport(m_viewportX, m_viewportY, render_width, render_height);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        historysave_program->release();

        if (snapshot.useTileRendering) {
            renderComplete = false;
        }
    }
}

void Renderer::performDenoising(const RenderParams::Snapshot& snapshot, bool forceCurrentFrame)
{
    if (!snapshot.denoise) {
        m_forceDenoiseRefresh = false;
        return;
    }

    const bool hasCompleteFrame = (nowChunkedCount == 0 && frameCounter > 0);
    if (!hasCompleteFrame) {
        return;
    }

    if (m_hasDenoisedFrame && m_lastDenoisedFrameCounter == frameCounter) {
        m_forceDenoiseRefresh = false;
        return;
    }

    const bool shouldDenoise =
        forceCurrentFrame || m_forceDenoiseRefresh || frameCounter % 100 == 0 || frameCounter == 1;
    if (!shouldDenoise) {
        return;
    }

    const bool refreshAuxiliaryBuffers = (frameCounter == 1 || !m_hasDenoisedFrame);

    // 使用 PBO 异步读取数据；降噪只在完整累计帧上触发。
    const GLenum formats[] = { GL_RGB, GL_RGB, GL_RGB };
    const GLuint textures[] = { normal_texture, baseColorTex, RenderColorTex };
    float* srcPtrs[3] = { nullptr, nullptr, nullptr };

    for (int i = refreshAuxiliaryBuffers ? 0 : 2; i < 3; i++) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pboIds[i]);
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glGetTexImage(GL_TEXTURE_2D, 0, formats[i], GL_FLOAT, 0);
        srcPtrs[i] = reinterpret_cast<float*>(glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY));
    }

    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glFinish();

    if (refreshAuxiliaryBuffers) {
        std::memcpy(oidnNormalBuf.getData(), srcPtrs[0], oidnNormalBuf.getSize());
        std::memcpy(oidnAlbedoBuf.getData(), srcPtrs[1], oidnAlbedoBuf.getSize());
    }
    std::memcpy(oidnColorBuf.getData(), srcPtrs[2], oidnColorBuf.getSize());

    for (int i = refreshAuxiliaryBuffers ? 0 : 2; i < 3; i++) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pboIds[i]);
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }

    if (refreshAuxiliaryBuffers) {
        oidnAlbedoFilter.execute();
        oidnNormalFilter.execute();
    }

    oidnMainFilter.execute();

    const char* errorMessage = nullptr;
    if (oidnDevice.getError(errorMessage) != oidn::Error::None) {
        std::cerr << "OIDN Error: " << errorMessage << std::endl;
    }

    glBindTexture(GL_TEXTURE_2D, RenderColorTexfiltered);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, render_width, render_height, GL_RGB, GL_FLOAT, oidnOutputBuf.getData());
    m_hasDenoisedFrame = true;
    m_lastDenoisedFrameCounter = frameCounter;
    m_forceDenoiseRefresh = false;
}

void Renderer::compositeToScreen(const RenderParams::Snapshot& snapshot)
{
    m_program->bind();
    {
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

        glActiveTexture(GL_TEXTURE5);
        if (snapshot.denoise) {
            glBindTexture(GL_TEXTURE_2D, RenderColorTexfiltered);
        } else {
            glBindTexture(GL_TEXTURE_2D, RenderColorTex);
        }
        m_program->setUniformValue("texPass1", 5);
        glActiveTexture(GL_TEXTURE6);

        // 渲染到屏幕尺寸，所以这里使用窗口尺寸。
        glViewport(m_viewportX, m_viewportY, m_width, m_height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    m_program->release();
}
